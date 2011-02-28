/*
 * Dynamic Windows:
 *          A GTK like implementation of the MacOS GUI using Cocoa
 *
 * (C) 2011 Brian Smith <brian@dbsoft.org>
 *
 * Using garbage collection so requires 10.5 or later.
 * clang -std=c99 -g -o dwtest -D__MAC__ -I. dwtest.c mac/dw.m -framework Cocoa -framework WebKit -fobjc-gc-only
 */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h> 
#include "dw.h"
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>

unsigned long _colors[] =
{
	0x00000000,   /* 0  black */
	0x00bb0000,   /* 1  red */
	0x0000bb00,   /* 2  green */
	0x00aaaa00,   /* 3  yellow */
	0x000000cc,   /* 4  blue */
	0x00bb00bb,   /* 5  magenta */
	0x0000bbbb,   /* 6  cyan */
	0x00bbbbbb,   /* 7  white */
	0x00777777,   /* 8  grey */
	0x00ff0000,   /* 9  bright red */
	0x0000ff00,   /* 10 bright green */
	0x00eeee00,   /* 11 bright yellow */
	0x000000ff,   /* 12 bright blue */
	0x00ff00ff,   /* 13 bright magenta */
	0x0000eeee,   /* 14 bright cyan */
	0x00ffffff,   /* 15 bright white */
	0xff000000	  /* 16 default color */
};

unsigned long _get_color(unsigned long thiscolor)
{
	if(thiscolor & DW_RGB_COLOR)
	{
		return thiscolor & ~DW_RGB_COLOR;
	}
	else if(thiscolor < 17)
	{
		return _colors[thiscolor];
	}
	return 0;
}

typedef struct _sighandler
{
	struct _sighandler   *next;
	ULONG message;
	HWND window;
	int id;
	void *signalfunction;
	void *data;
	
} SignalHandler;

SignalHandler *Root = NULL;

static void _do_resize(Box *thisbox, int x, int y);
SignalHandler *_get_handler(HWND window, int messageid)
{
	SignalHandler *tmp = Root;
	
	/* Find any callbacks for this function */
	while(tmp)
	{
		if(tmp->message == messageid && window == tmp->window)
		{
			return tmp;
		}
		tmp = tmp->next;
	}
	return NULL;
}

int _event_handler(id object, NSEvent *event, int message)
{
	SignalHandler *handler = _get_handler(object, message);
	/*NSLog(@"Event handler - type %d\n", message);*/
	
	if(handler)
	{
		switch(message)
		{
			case 0:
			{
				int (* API timerfunc)(void *) = (int (* API)(void *))handler->signalfunction;
				
				if(!timerfunc(handler->data))
					dw_timer_disconnect(handler->id);
				return 0;
			}
			case 1:
			{
				int (*sizefunc)(HWND, int, int, void *) = handler->signalfunction;
				NSSize size;
				
				if([object isMemberOfClass:[NSWindow class]])
				{
					NSWindow *window = object;
					size = [[window contentView] frame].size;
				}
				else
				{
					NSView *view = object;
					size = [view frame].size;
				}
				
				return sizefunc(object, size.width, size.height, handler->data);
			}	
			case 3:
			case 4:
			{
				int (* API buttonfunc)(HWND, int, int, int, void *) = (int (* API)(HWND, int, int, int, void *))handler->signalfunction;
				int flags = (int)[object pressedMouseButtons];
				NSPoint point = [object mouseLocation];
				int button = 0;
			
				if(flags & 1)
				{
					button = 1;
				}
				else if(flags & (1 << 1))
				{
					button = 2;
				}
				else if(flags & (1 << 2))
				{
					button = 3;
				}
			
				return buttonfunc(object, point.x, point.y, button, handler->data);
			}
			case 6:
			{
				int (* API closefunc)(HWND, void *) = (int (* API)(HWND, void *))handler->signalfunction;
				return closefunc(object, handler->data);
			}
			case 7:
			{
				DWExpose exp;
				int (* API exposefunc)(HWND, DWExpose *, void *) = (int (* API)(HWND, DWExpose *, void *))handler->signalfunction;
				NSRect rect = [object frame];
			
				exp.x = rect.origin.x;
				exp.y = rect.origin.y;
				exp.width = rect.size.width;
				exp.height = rect.size.height;
				int result = exposefunc(object, &exp, handler->data);
				[[object window] flushWindow];
				return result;
			}
			case 8:
			{
				int (* API clickfunc)(HWND, void *) = (int (* API)(HWND, void *))handler->signalfunction;

				return clickfunc(object, handler->data);
			}
            case 9:
            {
                int (*containerselectfunc)(HWND, char *, void *) = handler->signalfunction;
                
                return containerselectfunc(handler->window, (char *)event, handler->data);
            }
			case 10:
			{
				int (* API containercontextfunc)(HWND, char *, int, int, void *, void *) = (int (* API)(HWND, char *, int, int, void *, void *))handler->signalfunction;
				char *text = (char *)event;
				void *user = NULL;
				LONG x,y;
				
				dw_pointer_query_pos(&x, &y);
				
				return containercontextfunc(handler->window, text, (int)x, (int)y, handler->data, user);
			}
			case 12:
			{
				int (* API treeselectfunc)(HWND, HTREEITEM, char *, void *, void *) = (int (* API)(HWND, HTREEITEM, char *, void *, void *))handler->signalfunction;
				char *text = (char *)event;
				void *user = NULL;
				
				return treeselectfunc(handler->window, NULL, text, handler->data, user);
			}
		}
	}
	return -1;
}

/* Subclass for the Timer type */
@interface DWTimerHandler : NSObject { }
-(void)runTimer:(id)sender;
@end

@implementation DWTimerHandler
-(void)runTimer:(id)sender { _event_handler(sender, nil, 0); }
@end

NSApplication *DWApp;
NSRunLoop *DWRunLoop;
NSMenu *DWMainMenu;
DWTimerHandler *DWHandler;
#if !defined(GARBAGE_COLLECT)
NSAutoreleasePool *pool;
#endif
HWND _DWLastDrawable;

/* So basically to implement our event handlers...
 * it looks like we are going to have to subclass
 * basically everything.  Was hoping to add methods
 * to the superclasses but it looks like you can 
 * only add methods and no variables, which isn't 
 * going to work. -Brian
 */

/* Subclass for a box type */
@interface DWBox : NSView <NSWindowDelegate>
{
	Box *box;
	void *userdata;
	NSColor *bgcolor;
}
- (id)init;
-(void) dealloc; 
-(Box *)box;
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(void)drawRect:(NSRect)rect;
-(BOOL)isFlipped;
-(void)mouseDown:(NSEvent *)theEvent;
-(void)mouseUp:(NSEvent *)theEvent;
-(void)setColor:(unsigned long)input;
@end

@implementation DWBox
- (id)init 
{
    self = [super init];
    
    if (self) 
    {
        box = calloc(1, sizeof(Box));
    }
    return self;
}
-(void) dealloc 
{
    free(box);
    [super dealloc];
}
-(Box *)box { return box; }
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(void)drawRect:(NSRect)rect 
{
	if(bgcolor)
	{
		[bgcolor set];
		NSRectFill( [self bounds] );
	}
}
-(BOOL)isFlipped { return YES; }
-(void)mouseDown:(NSEvent *)theEvent { _event_handler(self, theEvent, 3); }
-(void)mouseUp:(NSEvent *)theEvent { _event_handler(self, theEvent, 4); }
-(void)setColor:(unsigned long)input
{
	if(input == _colors[DW_CLR_DEFAULT])
	{
		bgcolor = nil;
	}
	else
	{
		bgcolor = [NSColor colorWithDeviceRed: DW_RED_VALUE(input)/255.0 green: DW_GREEN_VALUE(input)/255.0 blue: DW_BLUE_VALUE(input)/255.0 alpha: 1];
	}
}
@end

/* Subclass for a top-level window */
@interface DWView : DWBox  
{
	NSMenu *windowmenu;
}
-(BOOL)windowShouldClose:(id)sender;
-(void)setMenu:(NSMenu *)input;
-(void)windowDidBecomeMain:(id)sender;
-(void)menuHandler:(id)sender;
@end

@implementation DWView
-(BOOL)windowShouldClose:(id)sender 
{
    if(_event_handler(self, nil, 6) == FALSE)
		return NO;
	return YES;
}
- (void)viewDidMoveToWindow
{
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowResized:) name:NSWindowDidResizeNotification object:[self window]];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(windowDidBecomeMain:) name:NSWindowDidBecomeMainNotification object:[self window]];
}
- (void)dealloc
{
        [[NSNotificationCenter defaultCenter] removeObserver:self];
        [super dealloc];
}
- (void)windowResized:(NSNotification *)notification;
{
	NSSize size = [self frame].size;
	_do_resize(box, size.width, size.height);
	_event_handler([self window], nil, 1);
}
-(void)windowDidBecomeMain:(id)sender
{
	if(windowmenu)
	{
		[DWApp setMainMenu:windowmenu];
	}
	else 
	{
		[DWApp setMainMenu:DWMainMenu];
	}

}
-(void)setMenu:(NSMenu *)input { windowmenu = input; }
-(void)menuHandler:(id)sender { _event_handler(sender, nil, 8); }
@end

/* Subclass for a button type */
@interface DWButton : NSButton
{
	void *userdata; 
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(void)buttonClicked:(id)sender;
@end

@implementation DWButton
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(void)buttonClicked:(id)sender { _event_handler(self, nil, 8); }
@end

/* Subclass for a progress type */
@interface DWPercent : NSProgressIndicator
{
	void *userdata; 
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWPercent
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a entryfield type */
@interface DWEntryField : NSTextField
{
	void *userdata; 
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWEntryField
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a entryfield password type */
@interface DWEntryFieldPassword : NSSecureTextField
{
	void *userdata; 
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWEntryFieldPassword
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a Notebook control type */
@interface DWNotebook : NSTabView <NSTabViewDelegate>
{
	void *userdata;
	int pageid;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(int)pageid;
-(void)setPageid:(int)input;
-(void)tabView:(NSTabView *)notebook didSelectTabViewItem:(NSTabViewItem *)notepage;
@end

@implementation DWNotebook
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(int)pageid { return pageid; }
-(void)setPageid:(int)input { pageid = input; }
-(void)tabView:(NSTabView *)notebook didSelectTabViewItem:(NSTabViewItem *)notepage
{
	id object = [notepage view];
	
	if([object isMemberOfClass:[DWBox class]])
	{
		DWBox *view = object;
		Box *box = [view box];
		NSSize size = [view frame].size;
		_do_resize(box, size.width, size.height);
	}
}	
@end

/* Subclass for a Notebook page type */
@interface DWNotebookPage : NSTabViewItem
{
	void *userdata;
	int pageid;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(int)pageid;
-(void)setPageid:(int)input;
@end

@implementation DWNotebookPage
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(int)pageid { return pageid; }
-(void)setPageid:(int)input { pageid = input; }
@end

/* Subclass for a color chooser type */
@interface DWColorChoose : NSColorPanel 
{
	DWDialog *dialog;
}
-(void)changeColor:(id)sender;
-(void)setDialog:(DWDialog *)input;
@end

@implementation DWColorChoose
- (void)changeColor:(id)sender
{
	dw_dialog_dismiss(dialog, [self color]);
}
-(void)setDialog:(DWDialog *)input { dialog = input; }
@end

/* Subclass for a splitbar type */
@interface DWSplitBar : NSSplitView <NSSplitViewDelegate> { }
- (void)splitViewDidResizeSubviews:(NSNotification *)aNotification;
@end

@implementation DWSplitBar
- (void)splitViewDidResizeSubviews:(NSNotification *)aNotification
{
	NSArray *views = [self subviews];
	id object;
	
	for(object in views)
	{
		if([object isMemberOfClass:[DWBox class]])
		{
			DWBox *view = object;
			Box *box = [view box];
			NSSize size = [view frame].size;
			_do_resize(box, size.width, size.height);
		}
	}		
}
@end

/* Subclass for a slider type */
@interface DWSlider : NSSlider
{
	void *userdata; 
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWSlider
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a slider type */
@interface DWScrollbar : NSScroller
{
	void *userdata;
	float range;
	float visible;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(float)range;
-(float)visible;
-(void)setRange:(float)input1 andVisible:(float)input2;
-(void)changed:(id)sender;
@end

@implementation DWScrollbar
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(float)range { return range; }
-(float)visible { return visible; }
-(void)setRange:(float)input1 andVisible:(float)input2 { range = input1; visible = input2; }
-(void)changed:(id)sender { /*NSNumber *num = [NSNumber numberWithDouble:[scroller floatValue]]; NSLog([num stringValue]);*/ }
@end

/* Subclass for a render area type */
@interface DWRender : NSView 
{
	void *userdata;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(void)drawRect:(NSRect)rect;
-(void)mouseDown:(NSEvent *)theEvent;
-(void)mouseUp:(NSEvent *)theEvent;
-(BOOL)isFlipped;
@end

@implementation DWRender
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(void)drawRect:(NSRect)rect { _event_handler(self, nil, 7); }
-(void)mouseDown:(NSEvent *)theEvent { _event_handler(self, theEvent, 3); }
-(void)mouseUp:(NSEvent *)theEvent { _event_handler(self, theEvent, 4); }
-(BOOL)isFlipped { return NO; }
@end

/* Subclass for a MLE type */
@interface DWMLE : NSTextView 
{
	void *userdata;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWMLE
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a Container/List type */
@interface DWContainer : NSTableView <NSTableViewDataSource>
{
	void *userdata;
	NSMutableArray *tvcols;
	NSMutableArray *data;
	NSMutableArray *types;
	NSPointerArray *titles;
	int lastAddPoint, lastQueryPoint;
    id scrollview;
}
-(NSInteger)numberOfRowsInTableView:(NSTableView *)aTable;
-(id)tableView:(NSTableView *)aTable objectValueForTableColumn:(NSTableColumn *)aCol row:(NSInteger)aRow;
/*-(void)tableView:(NSTableView *)aTableView setObjectValue:(id)anObject forTableColumn:(NSTableColumn *)aTableColumn row:(NSInteger)rowIndex;*/
-(void *)userdata;
-(void)setUserdata:(void *)input;
-(id)scrollview;
-(void)setScrollview:(id)input;
-(void)addColumn:(NSTableColumn *)input andType:(int)type;
-(NSTableColumn *)getColumn:(int)col;
-(int)addRow:(NSArray *)input;
-(int)addRows:(int)number;
-(void)editCell:(id)input at:(int)row and:(int)col;
-(void)removeRow:(int)row;
-(void)setRow:(int)row title:(void *)input;
-(void *)getRowTitle:(int)row;
-(id)getRow:(int)row and:(int)col;
-(int)cellType:(int)col;
-(int)lastAddPoint;
-(int)lastQueryPoint;
-(void)setLastQueryPoint:(int)input;
-(void)clear;
-(void)setup;
-(void)selectionChanged:(id)sender;
-(NSMenu *)menuForEvent:(NSEvent *)event;
@end

@implementation DWContainer
-(NSInteger)numberOfRowsInTableView:(NSTableView *)aTable
{
	if(tvcols && data)
	{
		int cols = (int)[tvcols count];
		if(cols)
		{
			return [data count] / cols;
		}
	}
	return 0;
}
-(id)tableView:(NSTableView *)aTable objectValueForTableColumn:(NSTableColumn *)aCol row:(NSInteger)aRow
{
	if(tvcols)
	{
		int z, col = -1;
		int count = (int)[tvcols count];
		
		for(z=0;z<count;z++)
		{
			if([tvcols objectAtIndex:z] == aCol)
			{
				col = z;
				break;
			}
		}
		if(col != -1)
		{
			int index = (int)(aRow * count) + col;
			id this = [data objectAtIndex:index];
			return ([this isKindOfClass:[NSNull class]]) ? nil : this;
		}
	}
	return nil;
}
/*-(void)tableView:(NSTableView *)aTableView setObjectValue:(id)anObject forTableColumn:(NSTableColumn *)aTableColumn row:(NSInteger)rowIndex
{
	if(tvcols)
	{
		int z, col = -1;
		int count = (int)[tvcols count];
		
		for(z=0;z<count;z++)
		{
			if([tvcols objectAtIndex:z] == aTableColumn)
			{
				col = z;
				break;
			}
		}
		if(col != -1)
		{
			int index = (int)(rowIndex * count) + col;
			[data replaceObjectAtIndex:index withObject:anObject];
		}
	}
}*/
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
-(NSScrollView *)scrollview { return scrollview; }
-(void)setScrollview:(NSScrollView *)input { scrollview = input; }
-(void)addColumn:(NSTableColumn *)input andType:(int)type { if(tvcols) { [tvcols addObject:input]; [types addObject:[NSNumber numberWithInt:type]]; } }
-(NSTableColumn *)getColumn:(int)col { if(tvcols) { return [tvcols objectAtIndex:col]; } return nil; }
-(int)addRow:(NSArray *)input { if(data) { lastAddPoint = (int)[titles count]; [data addObjectsFromArray:input]; [titles addPointer:NULL]; return (int)[titles count]; } return 0; }
-(int)addRows:(int)number
{
	if(tvcols)
	{
		int count = (int)(number * [tvcols count]);
		int z;
		
		lastAddPoint = (int)[titles count];
		
		for(z=0;z<count;z++)
		{
			[data addObject:[NSNull null]];
		}
		for(z=0;z<number;z++)
		{
			[titles addPointer:NULL];
		}
		return (int)[titles count];
	}
	return 0;
}		
-(void)editCell:(id)input at:(int)row and:(int)col
{
	if(tvcols && input)
	{
		int index = (int)(row * [tvcols count]) + col;
		[data replaceObjectAtIndex:index withObject:input];
	}
}
-(void)removeRow:(int)row
{
	if(tvcols)
	{
		int z, start, end;
		int count = (int)[tvcols count];
		
		start = count * row;
		end = start + count;
		
		for(z=start;z<end;z++)
		{
			[data removeObjectAtIndex:z];
		}
		[titles removePointerAtIndex:row];
		if(lastAddPoint > 0 && lastAddPoint < row)
		{
			lastAddPoint--;
		}
	}
}
-(void)setRow:(int)row title:(void *)input { if(titles && input) { [titles replacePointerAtIndex:row withPointer:input]; } }
-(void *)getRowTitle:(int)row { if(titles) { return [titles pointerAtIndex:row]; } return NULL; }
-(id)getRow:(int)row and:(int)col { if(data) { int index = (int)(row * [tvcols count]) + col; return [data objectAtIndex:index]; } return nil; }
-(int)cellType:(int)col { return [[types objectAtIndex:col] intValue]; }
-(int)lastAddPoint { return lastAddPoint; }
-(int)lastQueryPoint { return lastQueryPoint; }
-(void)setLastQueryPoint:(int)input { lastQueryPoint = input; }
-(void)clear { if(data) { [data removeAllObjects]; while([titles count]) { [titles removePointerAtIndex:0]; } } lastAddPoint = 0; }
-(void)setup 
{ 
	tvcols = [[NSMutableArray alloc] init]; 
	data = [[NSMutableArray alloc] init]; 
	types = [[NSMutableArray alloc] init]; 
	titles = [NSPointerArray pointerArrayWithWeakObjects];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(selectionChanged:) name:NSTableViewSelectionDidChangeNotification object:[self window]];
}
-(void)selectionChanged:(id)sender
{
	_event_handler(self, (NSEvent *)[self getRowTitle:(int)[self selectedRow]], 9);
}
-(NSMenu *)menuForEvent:(NSEvent *)event 
{
	int row;
	NSPoint where = [self convertPoint:[event locationInWindow] fromView:nil];
	row = (int)[self rowAtPoint:where];
	_event_handler(self, (NSEvent *)[self getRowTitle:row], 10);
	return nil;
}
@end

/* Subclass for a Calendar type */
@interface DWCalendar : NSDatePicker 
{
	void *userdata;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWCalendar
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a Combobox type */
@interface DWComboBox : NSComboBox 
{
	void *userdata;
}
-(void *)userdata;
-(void)setUserdata:(void *)input;
@end

@implementation DWComboBox
-(void *)userdata { return userdata; }
-(void)setUserdata:(void *)input { userdata = input; }
@end

/* Subclass for a test object type */
@interface DWObject : NSObject {}
-(void)uselessThread:(id)sender;
@end

@implementation DWObject
-(void)uselessThread:(id)sender { /* Thread only to initialize threading */ } 
@end

typedef struct
{
	ULONG message;
	char name[30];
	
} SignalList;

/* List of signals */
#define SIGNALMAX 16

SignalList SignalTranslate[SIGNALMAX] = {
	{ 1,	DW_SIGNAL_CONFIGURE },
	{ 2,	DW_SIGNAL_KEY_PRESS },
	{ 3,	DW_SIGNAL_BUTTON_PRESS },
	{ 4,	DW_SIGNAL_BUTTON_RELEASE },
	{ 5,	DW_SIGNAL_MOTION_NOTIFY },
	{ 6,	DW_SIGNAL_DELETE },
	{ 7,	DW_SIGNAL_EXPOSE },
	{ 8,	DW_SIGNAL_CLICKED },
	{ 9,	DW_SIGNAL_ITEM_ENTER },
	{ 10,	DW_SIGNAL_ITEM_CONTEXT },
	{ 11,	DW_SIGNAL_LIST_SELECT },
	{ 12,	DW_SIGNAL_ITEM_SELECT },
	{ 13,	DW_SIGNAL_SET_FOCUS },
	{ 14,	DW_SIGNAL_VALUE_CHANGED },
	{ 15,	DW_SIGNAL_SWITCH_PAGE },
	{ 16,	DW_SIGNAL_TREE_EXPAND }
};

/* This function adds a signal handler callback into the linked list.
 */
void _new_signal(ULONG message, HWND window, int id, void *signalfunction, void *data)
{
	SignalHandler *new = malloc(sizeof(SignalHandler));
	
	new->message = message;
	new->window = window;
	new->id = id;
	new->signalfunction = signalfunction;
	new->data = data;
	new->next = NULL;
	
	if (!Root)
		Root = new;
	else
	{
		SignalHandler *prev = NULL, *tmp = Root;
		while(tmp)
		{
			if(tmp->message == message &&
			   tmp->window == window &&
			   tmp->id == id &&
			   tmp->signalfunction == signalfunction)
			{
				tmp->data = data;
				free(new);
				return;
			}
			prev = tmp;
			tmp = tmp->next;
		}
		if(prev)
			prev->next = new;
		else
			Root = new;
	}
}

/* Finds the message number for a given signal name */
ULONG _findsigmessage(char *signame)
{
	int z;
	
	for(z=0;z<SIGNALMAX;z++)
	{
		if(strcasecmp(signame, SignalTranslate[z].name) == 0)
			return SignalTranslate[z].message;
	}
	return 0L;
}

unsigned long _foreground = 0xAAAAAA, _background = 0;

/* This function calculates how much space the widgets and boxes require
 * and does expansion as necessary.
 */
static int _resize_box(Box *thisbox, int *depth, int x, int y, int *usedx, int *usedy,
            int pass, int *usedpadx, int *usedpady)
{
   int z, currentx = 0, currenty = 0;
   int uymax = 0, uxmax = 0;
   int upymax = 0, upxmax = 0;
   /* Used for the SIZEEXPAND */
   int nux = *usedx, nuy = *usedy;
   int nupx = *usedpadx, nupy = *usedpady;
	
   (*usedx) += (thisbox->pad * 2);
   (*usedy) += (thisbox->pad * 2);

   for(z=0;z<thisbox->count;z++)
   {
      if(thisbox->items[z].type == TYPEBOX)
      {
		int initialx, initialy;
		DWBox *box = thisbox->items[z].hwnd;
		Box *tmp = [box box];

         initialx = x - (*usedx);
         initialy = y - (*usedy);

         if(tmp)
         {
            int newx, newy;
            int nux = *usedx, nuy = *usedy;
            int upx = *usedpadx + (tmp->pad*2), upy = *usedpady + (tmp->pad*2);

            /* On the second pass we know how big the box needs to be and how
             * much space we have, so we can calculate a ratio for the new box.
             */
            if(pass == 2)
            {
               int deep = *depth + 1;

               _resize_box(tmp, &deep, x, y, &nux, &nuy, 1, &upx, &upy);

               tmp->upx = upx - *usedpadx;
               tmp->upy = upy - *usedpady;

               newx = x - nux;
               newy = y - nuy;

               tmp->width = thisbox->items[z].width = initialx - newx;
               tmp->height = thisbox->items[z].height = initialy - newy;

               tmp->parentxratio = thisbox->xratio;
               tmp->parentyratio = thisbox->yratio;

               tmp->parentpad = tmp->pad;

               /* Just in case */
               tmp->xratio = thisbox->xratio;
               tmp->yratio = thisbox->yratio;

               if(thisbox->type == DW_VERT)
               {
                  if((thisbox->items[z].width-((thisbox->items[z].pad*2)+(tmp->pad*2)))!=0)
                     tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-((thisbox->items[z].pad*2)+(tmp->pad*2))))/((float)(thisbox->items[z].width-((thisbox->items[z].pad*2)+(tmp->pad*2))));
               }
               else
               {
                  if((thisbox->items[z].width-tmp->upx)!=0)
                     tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-tmp->upx))/((float)(thisbox->items[z].width-tmp->upx));
               }
               if(thisbox->type == DW_HORZ)
               {
                  if((thisbox->items[z].height-((thisbox->items[z].pad*2)+(tmp->pad*2)))!=0)
                     tmp->yratio = ((float)((thisbox->items[z].height * thisbox->yratio)-((thisbox->items[z].pad*2)+(tmp->pad*2))))/((float)(thisbox->items[z].height-((thisbox->items[z].pad*2)+(tmp->pad*2))));
               }
               else
               {
                  if((thisbox->items[z].height-tmp->upy)!=0)
                     tmp->yratio = ((float)((thisbox->items[z].height * thisbox->yratio)-tmp->upy))/((float)(thisbox->items[z].height-tmp->upy));
               }

               nux = *usedx; nuy = *usedy;
               upx = *usedpadx + (tmp->pad*2); upy = *usedpady + (tmp->pad*2);
            }

            (*depth)++;

            _resize_box(tmp, depth, x, y, &nux, &nuy, pass, &upx, &upy);

            (*depth)--;

            newx = x - nux;
            newy = y - nuy;

            tmp->minwidth = thisbox->items[z].width = initialx - newx;
            tmp->minheight = thisbox->items[z].height = initialy - newy;
         }
      }

      if(pass > 1 && *depth > 0)
      {
         if(thisbox->type == DW_VERT)
         {
            if((thisbox->minwidth-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))) == 0)
               thisbox->items[z].xratio = 1.0;
            else
               thisbox->items[z].xratio = ((float)((thisbox->width * thisbox->parentxratio)-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))))/((float)(thisbox->minwidth-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))));
         }
         else
         {
            if(thisbox->minwidth-thisbox->upx == 0)
               thisbox->items[z].xratio = 1.0;
            else
               thisbox->items[z].xratio = ((float)((thisbox->width * thisbox->parentxratio)-thisbox->upx))/((float)(thisbox->minwidth-thisbox->upx));
         }

         if(thisbox->type == DW_HORZ)
         {
            if((thisbox->minheight-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))) == 0)
               thisbox->items[z].yratio = 1.0;
            else
               thisbox->items[z].yratio = ((float)((thisbox->height * thisbox->parentyratio)-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))))/((float)(thisbox->minheight-((thisbox->items[z].pad*2)+(thisbox->parentpad*2))));
         }
         else
         {
            if(thisbox->minheight-thisbox->upy == 0)
               thisbox->items[z].yratio = 1.0;
            else
               thisbox->items[z].yratio = ((float)((thisbox->height * thisbox->parentyratio)-thisbox->upy))/((float)(thisbox->minheight-thisbox->upy));
         }

         if(thisbox->items[z].type == TYPEBOX)
         {
			DWBox *box = thisbox->items[z].hwnd;
			Box *tmp = [box box];

            if(tmp)
            {
               tmp->parentxratio = thisbox->items[z].xratio;
               tmp->parentyratio = thisbox->items[z].yratio;
            }
         }
      }
      else
      {
         thisbox->items[z].xratio = thisbox->xratio;
         thisbox->items[z].yratio = thisbox->yratio;
      }

      if(thisbox->type == DW_VERT)
      {
         if((thisbox->items[z].width + (thisbox->items[z].pad*2)) > uxmax)
            uxmax = (thisbox->items[z].width + (thisbox->items[z].pad*2));
         if(thisbox->items[z].hsize != SIZEEXPAND)
         {
            if(((thisbox->items[z].pad*2) + thisbox->items[z].width) > upxmax)
               upxmax = (thisbox->items[z].pad*2) + thisbox->items[z].width;
         }
         else
         {
            if(thisbox->items[z].pad*2 > upxmax)
               upxmax = thisbox->items[z].pad*2;
         }
      }
      else
      {
         if(thisbox->items[z].width == -1)
         {
            /* figure out how much space this item requires */
            /* thisbox->items[z].width = */
         }
         else
         {
            (*usedx) += thisbox->items[z].width + (thisbox->items[z].pad*2);
            if(thisbox->items[z].hsize != SIZEEXPAND)
               (*usedpadx) += (thisbox->items[z].pad*2) + thisbox->items[z].width;
            else
               (*usedpadx) += thisbox->items[z].pad*2;
         }
      }
      if(thisbox->type == DW_HORZ)
      {
         if((thisbox->items[z].height + (thisbox->items[z].pad*2)) > uymax)
            uymax = (thisbox->items[z].height + (thisbox->items[z].pad*2));
         if(thisbox->items[z].vsize != SIZEEXPAND)
         {
            if(((thisbox->items[z].pad*2) + thisbox->items[z].height) > upymax)
               upymax = (thisbox->items[z].pad*2) + thisbox->items[z].height;
         }
         else
         {
            if(thisbox->items[z].pad*2 > upymax)
               upymax = thisbox->items[z].pad*2;
         }
      }
      else
      {
         if(thisbox->items[z].height == -1)
         {
            /* figure out how much space this item requires */
            /* thisbox->items[z].height = */
         }
         else
         {
            (*usedy) += thisbox->items[z].height + (thisbox->items[z].pad*2);
            if(thisbox->items[z].vsize != SIZEEXPAND)
               (*usedpady) += (thisbox->items[z].pad*2) + thisbox->items[z].height;
            else
               (*usedpady) += thisbox->items[z].pad*2;
         }
      }
   }

   (*usedx) += uxmax;
   (*usedy) += uymax;
   (*usedpadx) += upxmax;
   (*usedpady) += upymax;

   currentx += thisbox->pad;
   currenty += thisbox->pad;

   /* The second pass is for expansion and actual placement. */
   if(pass > 1)
   {
      /* Any SIZEEXPAND items should be set to uxmax/uymax */
      for(z=0;z<thisbox->count;z++)
      {
         if(thisbox->items[z].hsize == SIZEEXPAND && thisbox->type == DW_VERT)
            thisbox->items[z].width = uxmax-(thisbox->items[z].pad*2);
         if(thisbox->items[z].vsize == SIZEEXPAND && thisbox->type == DW_HORZ)
            thisbox->items[z].height = uymax-(thisbox->items[z].pad*2);
         /* Run this code segment again to finalize the sized after setting uxmax/uymax values. */
         if(thisbox->items[z].type == TYPEBOX)
         {
			DWBox *box = thisbox->items[z].hwnd;
			Box *tmp = [box box];

            if(tmp)
            {
               if(*depth > 0)
               {
                  float calcval;

                  if(thisbox->type == DW_VERT)
                  {
                     calcval = (float)(tmp->minwidth-((thisbox->items[z].pad*2)+(thisbox->pad*2)));
                     if(calcval == 0.0)
                        tmp->xratio = thisbox->xratio;
                     else
                        tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-((thisbox->items[z].pad*2)+(thisbox->pad*2))))/calcval;
                     tmp->width = thisbox->items[z].width;
                  }
                  if(thisbox->type == DW_HORZ)
                  {
                     calcval = (float)(tmp->minheight-((thisbox->items[z].pad*2)+(thisbox->pad*2)));
                     if(calcval == 0.0)
                        tmp->yratio = thisbox->yratio;
                     else
                        tmp->yratio = ((float)((thisbox->items[z].height * thisbox->yratio)-((thisbox->items[z].pad*2)+(thisbox->pad*2))))/calcval;
                     tmp->height = thisbox->items[z].height;
                  }
               }

               (*depth)++;

               _resize_box(tmp, depth, x, y, &nux, &nuy, 3, &nupx, &nupy);

               (*depth)--;

            }
         }
      }

      for(z=0;z<(thisbox->count);z++)
      {
         int height = thisbox->items[z].height;
         int width = thisbox->items[z].width;
         int pad = thisbox->items[z].pad;
         NSView *handle = thisbox->items[z].hwnd;
		 NSPoint point;
		 NSSize size;
         int vectorx, vectory;

         /* When upxmax != pad*2 then ratios are incorrect. */
         vectorx = (int)((width*thisbox->items[z].xratio)-width);
         vectory = (int)((height*thisbox->items[z].yratio)-height);

         if(width > 0 && height > 0)
         {
            /* This is a hack to fix rounding of the sizing */
            if(*depth == 0)
            {
               vectorx++;
               vectory++;
            }

            /* If this item isn't going to expand... reset the vectors to 0 */
            if(thisbox->items[z].vsize != SIZEEXPAND)
               vectory = 0;
            if(thisbox->items[z].hsize != SIZEEXPAND)
               vectorx = 0;

			point.x = currentx + pad;
			point.y = currenty + pad;
			size.width = width + vectorx;
			size.height = height + vectory;
			[handle setFrameOrigin:point];
			[handle setFrameSize:size];

			/* Special handling for notebook controls */
			if([handle isMemberOfClass:[DWNotebook class]])
			{
				DWNotebook *notebook = (DWNotebook *)handle;
				DWNotebookPage *notepage = (DWNotebookPage *)[notebook selectedTabViewItem];
				DWBox *view = [notepage view];
					 
				if(view != nil)
				{
					Box *box = [view box];
					NSSize size = [view frame].size;
					_do_resize(box, size.width, size.height);
				}
			}
			else if([handle isMemberOfClass:[DWRender class]])
			{
				_event_handler(handle, nil, 1);
			}
			 
			if(thisbox->type == DW_HORZ)
               currentx += width + vectorx + (pad * 2);
            if(thisbox->type == DW_VERT)
               currenty += height + vectory + (pad * 2);
         }
      }
   }
   return 0;
}

static void _do_resize(Box *thisbox, int x, int y)
{
   if(x != 0 && y != 0)
   {
      if(thisbox)
      {
         int usedx = 0, usedy = 0, usedpadx = 0, usedpady = 0, depth = 0;

         _resize_box(thisbox, &depth, x, y, &usedx, &usedy, 1, &usedpadx, &usedpady);

         if(usedx-usedpadx == 0 || usedy-usedpady == 0)
            return;

         thisbox->xratio = ((float)(x-usedpadx))/((float)(usedx-usedpadx));
         thisbox->yratio = ((float)(y-usedpady))/((float)(usedy-usedpady));

         usedx = usedy = usedpadx = usedpady = depth = 0;

         _resize_box(thisbox, &depth, x, y, &usedx, &usedy, 2, &usedpadx, &usedpady);
      }
   }
}

NSMenu *_generate_main_menu()
{
	/* This only works on 10.6 so we have a backup method */
	NSString * applicationName = [[NSRunningApplication currentApplication] localizedName];
	if(applicationName == nil)
	{
		applicationName = [[NSProcessInfo processInfo] processName];
	}
	
	/* Create the main menu */
	NSMenu * mainMenu = [[[NSMenu alloc] initWithTitle:@"MainMenu"] autorelease];
	
	NSMenuItem * mitem = [mainMenu addItemWithTitle:@"Apple" action:NULL keyEquivalent:@""];
	NSMenu * menu = [[[NSMenu alloc] initWithTitle:@"Apple"] autorelease];
	
	[DWApp performSelector:@selector(setAppleMenu:) withObject:menu];
	
	/* Setup the Application menu */
	NSMenuItem * item = [menu addItemWithTitle:[NSString stringWithFormat:@"%@ %@", NSLocalizedString(@"About", nil), applicationName]
										action:@selector(orderFrontStandardAboutPanel:)
								 keyEquivalent:@""];
	[item setTarget:DWApp];
	
	[menu addItem:[NSMenuItem separatorItem]];
	
	item = [menu addItemWithTitle:NSLocalizedString(@"Preferences...", nil)
						   action:NULL
					keyEquivalent:@","];
	
	[menu addItem:[NSMenuItem separatorItem]];
	
	item = [menu addItemWithTitle:NSLocalizedString(@"Services", nil)
						   action:NULL
					keyEquivalent:@""];
	NSMenu * servicesMenu = [[[NSMenu alloc] initWithTitle:@"Services"] autorelease];
	[menu setSubmenu:servicesMenu forItem:item];
	[DWApp setServicesMenu:servicesMenu];
	
	[menu addItem:[NSMenuItem separatorItem]];
	
	item = [menu addItemWithTitle:[NSString stringWithFormat:@"%@ %@", NSLocalizedString(@"Hide", nil), applicationName]
						   action:@selector(hide:)
					keyEquivalent:@"h"];
	[item setTarget:DWApp];
	
	item = [menu addItemWithTitle:NSLocalizedString(@"Hide Others", nil)
						   action:@selector(hideOtherApplications:)
					keyEquivalent:@"h"];
	[item setKeyEquivalentModifierMask:NSCommandKeyMask | NSAlternateKeyMask];
	[item setTarget:DWApp];
	
	item = [menu addItemWithTitle:NSLocalizedString(@"Show All", nil)
						   action:@selector(unhideAllApplications:)
					keyEquivalent:@""];
	[item setTarget:DWApp];
	
	[menu addItem:[NSMenuItem separatorItem]];
	
	item = [menu addItemWithTitle:[NSString stringWithFormat:@"%@ %@", NSLocalizedString(@"Quit", nil), applicationName]
						   action:@selector(terminate:)
					keyEquivalent:@"q"];
	[item setTarget:DWApp];
	
	[mainMenu setSubmenu:menu forItem:mitem];
	
	return mainMenu;
}

/*
 * Initializes the Dynamic Windows engine.
 * Parameters:
 *           newthread: True if this is the only thread.
 *                      False if there is already a message loop running.
 */
int API dw_init(int newthread, int argc, char *argv[])
{
	/* Create the application object */
	DWApp = [NSApplication sharedApplication];
	/* Create a run loop for doing manual loops */
	DWRunLoop = [NSRunLoop alloc];
	/* Create object for handling timers */
	DWHandler = [[DWTimerHandler alloc] init];
    /* If we aren't using garbage collection we need autorelease pools */
#if !defined(GARBAGE_COLLECT)
	pool = [[NSAutoreleasePool alloc] init];
#endif
    /* Create a default main menu, with just the application menu */
	DWMainMenu = _generate_main_menu();
	[DWApp setMainMenu:DWMainMenu];
	DWObject *test = [[DWObject alloc] init];
    /* Use NSThread to start a dummy thread to initialize the threading subsystem */
	NSThread *thread = [[ NSThread alloc] initWithTarget:test selector:@selector(uselessThread:) object:nil];
	[thread start];
	return 0;
}

/*
 * Runs a message loop for Dynamic Windows.
 */
void API dw_main(void)
{
    [DWApp run];
}

/*
 * Runs a message loop for Dynamic Windows, for a period of milliseconds.
 * Parameters:
 *           milliseconds: Number of milliseconds to run the loop for.
 */
void API dw_main_sleep(int milliseconds)
{
	double seconds = (double)milliseconds/1000.0;
	NSDate *time = [[NSDate alloc] initWithTimeIntervalSinceNow:seconds];
	[DWRunLoop runUntilDate:time];
}

/*
 * Processes a single message iteration and returns.
 */
void API dw_main_iteration(void)
{
	[DWRunLoop limitDateForMode:NSDefaultRunLoopMode];
}

/*
 * Cleanly terminates a DW session, should be signal handler safe.
 * Parameters:
 *       exitcode: Exit code reported to the operating system.
 */
void API dw_exit(int exitcode)
{
	exit(exitcode);
}

/*
 * Free's memory allocated by dynamic windows.
 * Parameters:
 *           ptr: Pointer to dynamic windows allocated
 *                memory to be free()'d.
 */
void API dw_free(void *ptr)
{
   free(ptr);
}

/*
 * Returns a pointer to a static buffer which containes the
 * current user directory.  Or the root directory (C:\ on
 * OS/2 and Windows).
 */
char *dw_user_dir(void)
{
	static char _user_dir[1024] = "";
	
	if(!_user_dir[0])
	{
		char *home = getenv("HOME");
		
		if(home)
			strcpy(_user_dir, home);
		else
			strcpy(_user_dir, "/");
	}
	return _user_dir;
}

/*
 * Displays a Message Box with given text and title..
 * Parameters:
 *           title: The title of the message box.
 *           flags: flags to indicate buttons and icon
 *           format: printf style format string.
 *           ...: Additional variables for use in the format.
 */
int API dw_messagebox(char *title, int flags, char *format, ...)
{
	int iResponse;
	NSString *button1 = @"OK";
	NSString *button2 = nil;
	NSString *button3 = nil;
	va_list args;
	char outbuf[1000];

	va_start(args, format);
	vsprintf(outbuf, format, args);
	va_end(args);
	
	if(flags & DW_MB_OKCANCEL)
	{
		button2 = @"Cancel";
	}
	else if(flags & DW_MB_YESNO)
	{
		button1 = @"Yes";
		button2 = @"No";
	}
	else if(flags & DW_MB_YESNOCANCEL)
	{
		button1 = @"Yes";
		button2 = @"No";
		button3 = @"Cancel";
	}
	
	if(flags & DW_MB_ERROR)
	{
		iResponse = (int)
		NSRunCriticalAlertPanel([ NSString stringWithUTF8String:title ], 
								[ NSString stringWithUTF8String:outbuf ],
								button1, button2, button3);	}
	else 
	{
		iResponse = (int)
		NSRunAlertPanel([ NSString stringWithUTF8String:title ], 
						[ NSString stringWithUTF8String:outbuf ],
						button1, button2, button3);
	}
	
	switch(iResponse) 
	{
		case NSAlertDefaultReturn:    /* user pressed OK */
			if(flags & DW_MB_YESNO || flags & DW_MB_YESNOCANCEL)
			{
				return DW_MB_RETURN_YES;
			}
			return DW_MB_RETURN_OK;
		case NSAlertAlternateReturn:  /* user pressed Cancel */
			if(flags & DW_MB_OKCANCEL)
			{
				return DW_MB_RETURN_CANCEL;
			}
			return DW_MB_RETURN_NO;
		case NSAlertOtherReturn:      /* user pressed the third button */
			return DW_MB_RETURN_CANCEL;
		case NSAlertErrorReturn:      /* an error occurred */
			break;
	}
	return 0;
}

/*
 * Opens a file dialog and queries user selection.
 * Parameters:
 *       title: Title bar text for dialog.
 *       defpath: The default path of the open dialog.
 *       ext: Default file extention.
 *       flags: DW_FILE_OPEN or DW_FILE_SAVE.
 * Returns:
 *       NULL on error. A malloced buffer containing
 *       the file path on success.
 *
 */
char * API dw_file_browse(char *title, char *defpath, char *ext, int flags)
{
	if(flags == DW_FILE_OPEN)
	{
		/* Create the File Open Dialog class. */
		NSOpenPanel* openDlg = [NSOpenPanel openPanel];
	
		/* Enable the selection of files in the dialog. */
		[openDlg setCanChooseFiles:YES];
		[openDlg setCanChooseDirectories:NO];
	
		/* Disable multiple selection */
		[openDlg setAllowsMultipleSelection:NO];
	
		/* Display the dialog.  If the OK button was pressed,
		 * process the files.
		 */
		if([openDlg runModal] == NSOKButton)
		{
			/* Get an array containing the full filenames of all
			 * files and directories selected.
			 */
			NSArray* files = [openDlg filenames];
			NSString* fileName = [files objectAtIndex:0];
			return strdup([ fileName UTF8String ]);		
		}
	}
	else
	{
		/* Create the File Save Dialog class. */
		NSSavePanel* saveDlg = [NSSavePanel savePanel];
		
		/* Enable the creation of directories in the dialog. */
		[saveDlg setCanCreateDirectories:YES];
		
		/* Display the dialog.  If the OK button was pressed,
		 * process the files.
		 */
		if([saveDlg runModal] == NSFileHandlingPanelOKButton)
		{
			/* Get an array containing the full filenames of all
			 * files and directories selected.
			 */
			NSString* fileName = [saveDlg filename];
			return strdup([ fileName UTF8String ]);		
		}		
	}

	return NULL;
}

/*
 * Gets the contents of the default clipboard as text.
 * Parameters:
 *       None.
 * Returns:
 *       Pointer to an allocated string of text or NULL if clipboard empty or contents could not
 *       be converted to text.
 */
char *dw_clipboard_get_text()
{
	NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
	NSString *str = [pasteboard stringForType:NSStringPboardType];
	if(str != nil)
	{
		return strdup([ str UTF8String ]);
	}
	return NULL;
}

/*
 * Sets the contents of the default clipboard to the supplied text.
 * Parameters:
 *       Text.
 */
void dw_clipboard_set_text( char *str, int len)
{
	NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
	
	[pasteboard clearContents];
	
	[pasteboard setString:[ NSString stringWithUTF8String:str ] forType:NSStringPboardType];
}


/*
 * Allocates and initializes a dialog struct.
 * Parameters:
 *           data: User defined data to be passed to functions.
 */
DWDialog * API dw_dialog_new(void *data)
{
	DWDialog *tmp = malloc(sizeof(DWDialog));
	
	if(tmp)
	{
		tmp->eve = dw_event_new();
		dw_event_reset(tmp->eve);
		tmp->data = data;
		tmp->done = FALSE;
		tmp->result = NULL;
	}
	return tmp;	
}

/*
 * Accepts a dialog struct and returns the given data to the
 * initial called of dw_dialog_wait().
 * Parameters:
 *           dialog: Pointer to a dialog struct aquired by dw_dialog_new).
 *           result: Data to be returned by dw_dialog_wait().
 */
int API dw_dialog_dismiss(DWDialog *dialog, void *result)
{
	dialog->result = result;
	dw_event_post(dialog->eve);
	dialog->done = TRUE;
	return 0;
}

/*
 * Accepts a dialog struct waits for dw_dialog_dismiss() to be
 * called by a signal handler with the given dialog struct.
 * Parameters:
 *           dialog: Pointer to a dialog struct aquired by dw_dialog_new).
 */
void * API dw_dialog_wait(DWDialog *dialog)
{
	void *tmp;
	
	while(!dialog->done)
	{
		dw_main_iteration();
	}
	dw_event_close(&dialog->eve);
	tmp = dialog->result;
	free(dialog);
	return tmp;
}

/*
 * Create a new Box to be packed.
 * Parameters:
 *       type: Either DW_VERT (vertical) or DW_HORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 */
HWND API dw_box_new(int type, int pad)
{
    DWBox *view = [[DWBox alloc] init];  
	Box *newbox = [view box];
	memset(newbox, 0, sizeof(Box));
   	newbox->pad = pad;
   	newbox->type = type;
	return view;
}

/*
 * Create a new Group Box to be packed.
 * Parameters:
 *       type: Either DW_VERT (vertical) or DW_HORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 *       title: Text to be displayined in the group outline.
 */
HWND API dw_groupbox_new(int type, int pad, char *title)
{
	DWBox *box = dw_box_new(type, pad);
	[box setFocusRingType:NSFocusRingTypeExterior];
	return box;
}

/*
 * Pack windows (widgets) into a box from the end (or bottom).
 * Parameters:
 *       box: Window handle of the box to be packed into.
 *       item: Window handle of the item to be back.
 *       width: Width in pixels of the item or -1 to be self determined.
 *       height: Height in pixels of the item or -1 to be self determined.
 *       hsize: TRUE if the window (widget) should expand horizontally to fill space given.
 *       vsize: TRUE if the window (widget) should expand vertically to fill space given.
 *       pad: Number of pixels of padding around the item.
 */
void API dw_box_pack_end(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad)
{
	NSObject *object = box;
	DWBox *view = box;
	DWBox *this = item;
	Box *thisbox;
    int z;
    Item *tmpitem, *thisitem;

	/* Query the objects */
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = box;
		view = [window contentView];
	}
	
	thisbox = [view box];
	thisitem = thisbox->items;
	object = item;

	/* Query the objects */
	if([ object isKindOfClass:[ DWContainer class ] ])
	{
		DWContainer *cont = item;
		this = item = [cont scrollview];
	}
    
	/* Duplicate the existing data */
    tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

    for(z=0;z<thisbox->count;z++)
    {
       tmpitem[z+1] = thisitem[z];
    }

	/* Sanity checks */
    if(vsize && !height)
       height = 1;
    if(hsize && !width)
       width = 1;

	/* Fill in the item data appropriately */
	if([ object isKindOfClass:[ DWBox class ] ])
       tmpitem[0].type = TYPEBOX;
    else
       tmpitem[0].type = TYPEITEM;

    tmpitem[0].hwnd = item;
    tmpitem[0].origwidth = tmpitem[0].width = width;
    tmpitem[0].origheight = tmpitem[0].height = height;
    tmpitem[0].pad = pad;
    if(hsize)
       tmpitem[0].hsize = SIZEEXPAND;
    else
       tmpitem[0].hsize = SIZESTATIC;

    if(vsize)
       tmpitem[0].vsize = SIZEEXPAND;
    else
       tmpitem[0].vsize = SIZESTATIC;

    thisbox->items = tmpitem;

	/* Update the item count */
    thisbox->count++;

	/* Add the item to the box */
	[view addSubview:this];
	
	/* Free the old data */
    if(thisbox->count)
       free(thisitem);
}

/*
 * Pack windows (widgets) into a box from the start (or top).
 * Parameters:
 *       box: Window handle of the box to be packed into.
 *       item: Window handle of the item to be back.
 *       width: Width in pixels of the item or -1 to be self determined.
 *       height: Height in pixels of the item or -1 to be self determined.
 *       hsize: TRUE if the window (widget) should expand horizontally to fill space given.
 *       vsize: TRUE if the window (widget) should expand vertically to fill space given.
 *       pad: Number of pixels of padding around the item.
 */
void API dw_box_pack_start(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad)
{
	NSObject *object = box;
	DWBox *view = box;
	DWBox *this = item;
	Box *thisbox;
    int z;
    Item *tmpitem, *thisitem;

	/* Query the objects */
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = box;
		view = [window contentView];
	}
	
	thisbox = [view box];
	thisitem = thisbox->items;
	object = item;

	/* Query the objects */
	if([ object isKindOfClass:[ DWContainer class ] ])
	{
		DWContainer *cont = item;
		this = item = [cont scrollview];
	}

	/* Duplicate the existing data */
    tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

    for(z=0;z<thisbox->count;z++)
    {
       tmpitem[z] = thisitem[z];
    }

	/* Sanity checks */
    if(vsize && !height)
       height = 1;
    if(hsize && !width)
       width = 1;

	/* Fill in the item data appropriately */
	if([ object isKindOfClass:[ DWBox class ] ])
       tmpitem[thisbox->count].type = TYPEBOX;
    else
       tmpitem[thisbox->count].type = TYPEITEM;

    tmpitem[thisbox->count].hwnd = item;
    tmpitem[thisbox->count].origwidth = tmpitem[thisbox->count].width = width;
    tmpitem[thisbox->count].origheight = tmpitem[thisbox->count].height = height;
    tmpitem[thisbox->count].pad = pad;
    if(hsize)
       tmpitem[thisbox->count].hsize = SIZEEXPAND;
    else
       tmpitem[thisbox->count].hsize = SIZESTATIC;

    if(vsize)
       tmpitem[thisbox->count].vsize = SIZEEXPAND;
    else
       tmpitem[thisbox->count].vsize = SIZESTATIC;

	thisbox->items = tmpitem;

	/* Update the item count */
    thisbox->count++;

	/* Add the item to the box */
	[view addSubview:this];
	
	/* Free the old data */
    if(thisbox->count)
       free(thisitem);
}

HWND _button_new(char *text, ULONG id)
{
	DWButton *button = [[DWButton alloc] init];
	if(text && *text)
	{
		[button setTitle:[ NSString stringWithUTF8String:text ]];
	}
	[button setTarget:button];
	[button setAction:@selector(buttonClicked:)];
	[button setTag:id];
	[button setButtonType:NSMomentaryPushInButton];
	[button setBezelStyle:NSThickerSquareBezelStyle];
    return button;
}

/*
 * Create a new button window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_button_new(char *text, ULONG id)
{
    DWButton *button = _button_new(text, id);
    [button setButtonType:NSMomentaryPushInButton];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setImagePosition:NSNoImage];
    [button setAlignment:NSCenterTextAlignment];
    [[button cell] setControlTint:NSBlueControlTint];
	return button;
}

/*
 * Create a new Entryfield window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_entryfield_new(char *text, ULONG id)
{
	DWEntryField *entry = [[DWEntryField alloc] init];
	[entry setStringValue:[ NSString stringWithUTF8String:text ]];
	[entry setTag:id];
	return entry;
}

/*
 * Create a new Entryfield (password) window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_entryfield_password_new(char *text, ULONG id)
{
	DWEntryFieldPassword *entry = [[DWEntryFieldPassword alloc] init];
	[entry setStringValue:[ NSString stringWithUTF8String:text ]];
	[entry setTag:id];
	return entry;
}

/*
 * Sets the entryfield character limit.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          limit: Number of characters the entryfield will take.
 */
void API dw_entryfield_set_limit(HWND handle, ULONG limit)
{
	NSLog(@"dw_entryfield_set_limit() unimplemented\n");
}

/*
 * Create a new bitmap button window (widget) to be packed.
 * Parameters:
 *       text: Bubble help text to be displayed.
 *       id: An ID of a bitmap in the resource file.
 */
HWND API dw_bitmapbutton_new(char *text, ULONG resid)
{
    /* TODO: Implement tooltips */
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *respath = [bundle resourcePath];
    NSString *filepath = [respath stringByAppendingFormat:@"/%u.png", resid]; 
	NSImage *image = [[NSImage alloc] initWithContentsOfFile:filepath];
	DWButton *button = _button_new("", resid);
	[button setImage:image];
    //[button setBezelStyle:0];
    [button setButtonType:NSMomentaryLight];
    [button setBordered:NO];
    [bundle release];
    [respath release];
    [filepath release];
    [image release];
	return button;
}

/*
 * Create a new bitmap button window (widget) to be packed from a file.
 * Parameters:
 *       text: Bubble help text to be displayed.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 *       filename: Name of the file, omit extention to have
 *                 DW pick the appropriate file extension.
 *                 (BMP on OS/2 or Windows, XPM on Unix)
 */
HWND API dw_bitmapbutton_new_from_file(char *text, unsigned long id, char *filename)
{
    NSString *nstr = [ NSString stringWithUTF8String:filename ];
    NSImage *image = [[NSImage alloc] initWithContentsOfFile:nstr];
    if(!image)
    {
        nstr = [nstr stringByAppendingString:@".png"];
        image = [[NSImage alloc] initWithContentsOfFile:nstr];
    }
    [nstr release];
	DWButton *button = _button_new("", id);
	[button setImage:image];
	return button;
}

/*
 * Create a new bitmap button window (widget) to be packed from data.
 * Parameters:
 *       text: Bubble help text to be displayed.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 *       data: The contents of the image
 *            (BMP or ICO on OS/2 or Windows, XPM on Unix)
 *       len: length of str
 */
HWND API dw_bitmapbutton_new_from_data(char *text, unsigned long id, char *data, int len)
{
	NSData *thisdata = [[NSData alloc] dataWithBytes:data length:len];
	NSImage *image = [[NSImage alloc] initWithData:thisdata];
	DWButton *button = _button_new("", id);
	[button setImage:image];
	return button;
}

/*
 * Create a new spinbutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_spinbutton_new(char *text, ULONG id)
{
	NSLog(@"dw_spinbutton_new() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Sets the spinbutton value.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          position: Current value of the spinbutton.
 */
void API dw_spinbutton_set_pos(HWND handle, long position)
{
	NSLog(@"dw_spinbutton_set_pos() unimplemented\n");
}

/*
 * Sets the spinbutton limits.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          upper: Upper limit.
 *          lower: Lower limit.
 */
void API dw_spinbutton_set_limits(HWND handle, long upper, long lower)
{
	NSLog(@"dw_spinbutton_set_limits() unimplemented\n");
}

/*
 * Returns the current value of the spinbutton.
 * Parameters:
 *          handle: Handle to the spinbutton to be queried.
 */
long API dw_spinbutton_get_pos(HWND handle)
{
	NSLog(@"dw_spinbutton_get_pos() unimplemented\n");
    return 0;
}

/*
 * Create a new radiobutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_radiobutton_new(char *text, ULONG id)
{
	DWButton *button = _button_new(text, id);
	[button setButtonType:NSRadioButton];
	return button;
}

/*
 * Create a new slider window (widget) to be packed.
 * Parameters:
 *       vertical: TRUE or FALSE if slider is vertical.
 *       increments: Number of increments available.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_slider_new(int vertical, int increments, ULONG id)
{
	DWSlider *slider = [[DWSlider alloc] init];
	[slider setMaxValue:(double)increments];
	[slider setMinValue:0];
	return slider;
}

/*
 * Returns the position of the slider.
 * Parameters:
 *          handle: Handle to the slider to be queried.
 */
unsigned int API dw_slider_get_pos(HWND handle)
{
	DWSlider *slider = handle;
	double val = [slider doubleValue];
	return (int)val;
}

/*
 * Sets the slider position.
 * Parameters:
 *          handle: Handle to the slider to be set.
 *          position: Position of the slider withing the range.
 */
void API dw_slider_set_pos(HWND handle, unsigned int position)
{
	DWSlider *slider = handle;
	[slider setDoubleValue:(double)position];
}

/*
 * Create a new scrollbar window (widget) to be packed.
 * Parameters:
 *       vertical: TRUE or FALSE if scrollbar is vertical.
 *       increments: Number of increments available.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_scrollbar_new(int vertical, ULONG id)
{
	DWScrollbar *scrollbar = [[DWScrollbar alloc] init];
	[scrollbar setTarget:scrollbar]; 
	[scrollbar setAction:@selector(changed:)];
	[scrollbar setRange:0.0 andVisible:0.0];
	[scrollbar setKnobProportion:1.0];
	return scrollbar;
}

/*
 * Returns the position of the scrollbar.
 * Parameters:
 *          handle: Handle to the scrollbar to be queried.
 */
unsigned int API dw_scrollbar_get_pos(HWND handle)
{
	DWScrollbar *scrollbar = handle;
	float range = [scrollbar range];
	float fresult = [scrollbar doubleValue] * range;
	return (int)fresult;
}

/*
 * Sets the scrollbar position.
 * Parameters:
 *          handle: Handle to the scrollbar to be set.
 *          position: Position of the scrollbar withing the range.
 */
void API dw_scrollbar_set_pos(HWND handle, unsigned int position)
{
	DWScrollbar *scrollbar = handle;
	double range = (double)[scrollbar range];
	double newpos = (double)position/range;
	[scrollbar setDoubleValue:newpos];
}

/*
 * Sets the scrollbar range.
 * Parameters:
 *          handle: Handle to the scrollbar to be set.
 *          range: Maximum range value.
 *          visible: Visible area relative to the range.
 */
void API dw_scrollbar_set_range(HWND handle, unsigned int range, unsigned int visible)
{
	DWScrollbar *scrollbar = handle;
	float knob = (float)visible/(float)range;
	[scrollbar setRange:(float)range andVisible:(float)visible];
	[scrollbar setKnobProportion:knob];
}

/*
 * Create a new percent bar window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_percent_new(ULONG id)
{
	DWPercent *percent = [[DWPercent alloc] init];
	[percent setBezeled:YES];
	[percent setMaxValue:100];
	[percent setMinValue:0];
	/*[percent setTag:id]; Why doesn't this work? */
	return percent;
}

/*
 * Sets the percent bar position.
 * Parameters:
 *          handle: Handle to the percent bar to be set.
 *          position: Position of the percent bar withing the range.
 */
void API dw_percent_set_pos(HWND handle, unsigned int position)
{
	DWPercent *percent = handle;
	[percent setDoubleValue:(double)position];
}

/*
 * Create a new checkbox window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_checkbox_new(char *text, ULONG id)
{
	DWButton *button = _button_new(text, id);
	[button setButtonType:NSSwitchButton];
	[button setBezelStyle:NSRegularSquareBezelStyle];
	return button;
}

/*
 * Returns the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 */
int API dw_checkbox_get(HWND handle)
{
	DWButton *button = handle;
	if([button state])
	{
		return TRUE;
	}
	return FALSE;
}

/*
 * Sets the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 *          value: TRUE for checked, FALSE for unchecked.
 */
void API dw_checkbox_set(HWND handle, int value)
{
	DWButton *button = handle;
	if(value)
	{
		[button setState:NSOnState];
	}
	else 
	{
		[button setState:NSOffState];
	}

}

/* Common code for containers and listboxes */
HWND _cont_new(ULONG id, int multi)
{
    NSScrollView *scrollview  = [[NSScrollView alloc] init];    
	DWContainer *cont = [[DWContainer alloc] init];
    
    [cont setScrollview:scrollview];
    [scrollview setBorderType:NSBezelBorder];
    [scrollview setHasVerticalScroller:YES];
    [scrollview setAutohidesScrollers:YES];
    
	if(multi)
	{
		[cont setAllowsMultipleSelection:YES];
	}
	else
	{
		[cont setAllowsMultipleSelection:NO];
	}
	[cont setDataSource:cont];
    [scrollview setDocumentView:cont];
	return cont;
}

/*
 * Create a new listbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 *       multi: Multiple select TRUE or FALSE.
 */
HWND API dw_listbox_new(ULONG id, int multi)
{
	DWContainer *cont = _cont_new(id, multi);
	[cont setHeaderView:nil];
	int type = DW_CFA_STRING;
	[cont setup];
	NSTableColumn *column = [[NSTableColumn alloc] init];
	[cont addTableColumn:column];
	[cont addColumn:column andType:type];
	return cont;
}

/*
 * Appends the specified text to the listbox's (or combobox) entry list.
 * Parameters:
 *          handle: Handle to the listbox to be appended to.
 *          text: Text to append into listbox.
 */
void API dw_listbox_append(HWND handle, char *text)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo addItemWithObjectValue:[ NSString stringWithUTF8String:text ]];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        NSString *nstr = [ NSString stringWithUTF8String:text ];
        NSArray *newrow = [NSArray arrayWithObject:nstr];
        
        [cont addRow:newrow];
        [cont reloadData];
        
        [newrow release];
    }
}

/*
 * Inserts the specified text into the listbox's (or combobox) entry list.
 * Parameters:
 *          handle: Handle to the listbox to be inserted into.
 *          text: Text to insert into listbox.
 *          pos: 0-based position to insert text
 */
void API dw_listbox_insert(HWND handle, char *text, int pos)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo insertItemWithObjectValue:[ NSString stringWithUTF8String:text ] atIndex:pos];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        NSLog(@"dw_listbox_insert() unimplemented\n");
    }
}

/*
 * Appends the specified text items to the listbox's (or combobox) entry list.
 * Parameters:
 *          handle: Handle to the listbox to be appended to.
 *          text: Text strings to append into listbox.
 *          count: Number of text strings to append
 */
void API dw_listbox_list_append(HWND handle, char **text, int count)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		int z;
		
		for(z=0;z<count;z++)
		{		
			[combo addItemWithObjectValue:[ NSString stringWithUTF8String:text[z] ]];
		}
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
		int z;
		
		for(z=0;z<count;z++)
		{		
            NSString *nstr = [ NSString stringWithUTF8String:text[z] ];
            NSArray *newrow = [[NSArray alloc] arrayWithObject:nstr];
        
            [cont addRow:newrow];
            
            [newrow release];
        }
        [cont reloadData];
    }
}

/*
 * Clears the listbox's (or combobox) list of all entries.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
void API dw_listbox_clear(HWND handle)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo removeAllItems];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        
        [cont clear];
        [cont reloadData];
    }
}

/*
 * Returns the listbox's item count.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
int API dw_listbox_count(HWND handle)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		return (int)[combo numberOfItems];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        
        return (int)[cont numberOfRowsInTableView:cont];
    }
	return 0;
}

/*
 * Sets the topmost item in the viewport.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 *          top: Index to the top item.
 */
void API dw_listbox_set_top(HWND handle, int top)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo scrollItemAtIndexToTop:top];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        
        [cont scrollRowToVisible:top];
    }
}

/*
 * Copies the given index item's text into buffer.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          index: Index into the list to be queried.
 *          buffer: Buffer where text will be copied.
 *          length: Length of the buffer (including NULL).
 */
void API dw_listbox_get_text(HWND handle, unsigned int index, char *buffer, unsigned int length)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		NSString *nstr = [combo itemObjectValueAtIndex:index];
		strncpy(buffer, [ nstr UTF8String ], length - 1);
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        NSString *nstr = [cont getRow:index and:0];
        
        strncpy(buffer, [ nstr UTF8String ], length - 1);
    }
}

/*
 * Sets the text of a given listbox entry.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          index: Index into the list to be queried.
 *          buffer: Buffer where text will be copied.
 */
void API dw_listbox_set_text(HWND handle, unsigned int index, char *buffer)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo removeItemAtIndex:index];
		[combo insertItemWithObjectValue:[ NSString stringWithUTF8String:buffer ] atIndex:index];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        NSString *nstr = [ NSString stringWithUTF8String:buffer ];
        
        [cont editCell:nstr at:index and:0];
        [cont reloadData];
    }
        
}

/*
 * Returns the index to the item in the list currently selected.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 */
unsigned int API dw_listbox_selected(HWND handle)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		return (int)[combo indexOfSelectedItem];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        
        return (int)[cont selectedRow];
    }
	return -1;
}

/*
 * Returns the index to the current selected item or -1 when done.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          where: Either the previous return or -1 to restart.
 */
int API dw_listbox_selected_multi(HWND handle, int where)
{
	id object = handle;
    int retval = -1;
    
    if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        NSIndexSet *selected = [cont selectedRowIndexes];
        NSUInteger result = [selected indexGreaterThanIndex:where];
        
        if(result != NSNotFound)
        {
            retval = (int)result;
        }
        [selected release];
    }        
	return retval;
}

/*
 * Sets the selection state of a given index.
 * Parameters:
 *          handle: Handle to the listbox to be set.
 *          index: Item index.
 *          state: TRUE if selected FALSE if unselected.
 */
void API dw_listbox_select(HWND handle, int index, int state)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		if(state)
			[combo selectItemAtIndex:index];
		else
			[combo deselectItemAtIndex:index];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        NSIndexSet *selected = [[NSIndexSet alloc] initWithIndex:(NSUInteger)index];
        
        [cont selectRowIndexes:selected byExtendingSelection:YES];
        [selected release];
    }        
}

/*
 * Deletes the item with given index from the list.
 * Parameters:
 *          handle: Handle to the listbox to be set.
 *          index: Item index.
 */
void API dw_listbox_delete(HWND handle, int index)
{
	id object = handle;
	
	if([object isMemberOfClass:[DWComboBox class]])
	{
		DWComboBox *combo = handle;
		
		[combo removeItemAtIndex:index];
	}
    else if([object isMemberOfClass:[DWContainer class]])
    {
        DWContainer *cont = handle;
        
        [cont removeRow:index];
        [cont reloadData];
    }
}

/*
 * Create a new Combobox window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the combpbox widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_combobox_new(char *text, ULONG id)
{
	DWComboBox *combo = [[DWComboBox alloc] init];
	return combo;
}

/*
 * Create a new Multiline Editbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_mle_new(ULONG id)
{
	DWMLE *mle = [[DWMLE alloc] init];
	return mle;
}	

/*
 * Adds text to an MLE box and returns the current point.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          buffer: Text buffer to be imported.
 *          startpoint: Point to start entering text.
 */
unsigned int API dw_mle_import(HWND handle, char *buffer, int startpoint)
{
	DWMLE *mle = handle;
    NSTextStorage *ts = [mle textStorage];
    NSString *nstr = [NSString stringWithUTF8String:buffer];
    NSMutableString *ms = [ts mutableString];
    [ms insertString:nstr atIndex:(startpoint+1)];
	return (unsigned int)strlen(buffer) + startpoint;
}

/*
 * Grabs text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          buffer: Text buffer to be exported.
 *          startpoint: Point to start grabbing text.
 *          length: Amount of text to be grabbed.
 */
void API dw_mle_export(HWND handle, char *buffer, int startpoint, int length)
{
	DWMLE *mle = handle;
    NSTextStorage *ts = [mle textStorage];
    NSMutableString *ms = [ts mutableString];
    strncpy(buffer, [ms UTF8String], length);
}

/*
 * Obtains information about an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          bytes: A pointer to a variable to return the total bytes.
 *          lines: A pointer to a variable to return the number of lines.
 */
void API dw_mle_get_size(HWND handle, unsigned long *bytes, unsigned long *lines)
{
	DWMLE *mle = handle;
    NSTextStorage *ts = [mle textStorage];
    NSMutableString *ms = [ts mutableString];
    
    *bytes = [ms length];
    *lines = 0; /* TODO: Line count */
}

/*
 * Deletes text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be deleted from.
 *          startpoint: Point to start deleting text.
 *          length: Amount of text to be deleted.
 */
void API dw_mle_delete(HWND handle, int startpoint, int length)
{
	DWMLE *mle = handle;
    NSTextStorage *ts = [mle textStorage];
    NSMutableString *ms = [ts mutableString];
    [ms deleteCharactersInRange:NSMakeRange(startpoint+1, length)];
}

/*
 * Clears all text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be cleared.
 */
void API dw_mle_clear(HWND handle)
{
	DWMLE *mle = handle;
    NSTextStorage *ts = [mle textStorage];
    NSMutableString *ms = [ts mutableString];
    NSUInteger length = [ms length];
    [ms deleteCharactersInRange:NSMakeRange(0, length)];
}

/*
 * Sets the visible line of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be positioned.
 *          line: Line to be visible.
 */
void API dw_mle_set_visible(HWND handle, int line)
{
	NSLog(@"dw_mle_set_visible() unimplemented\n");
}

/*
 * Sets the editablity of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it can be edited, FALSE for readonly.
 */
void API dw_mle_set_editable(HWND handle, int state)
{
	DWMLE *mle = handle;
	if(state)
	{
		[mle setEditable:YES];
	}
	else 
	{
		[mle setEditable:NO];
	}
}

/*
 * Sets the word wrap state of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it wraps, FALSE if it doesn't.
 */
void API dw_mle_set_word_wrap(HWND handle, int state)
{
    DWMLE *mle = handle;
    if(state)
    {
        [mle setHorizontallyResizable:NO];
    }
    else
    {
        [mle setHorizontallyResizable:YES];
    }
}

/*
 * Sets the current cursor position of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be positioned.
 *          point: Point to position cursor.
 */
void API dw_mle_set_cursor(HWND handle, int point)
{
	NSLog(@"dw_mle_set_cursor() unimplemented\n");
}

/*
 * Finds text in an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be cleared.
 *          text: Text to search for.
 *          point: Start point of search.
 *          flags: Search specific flags.
 */
int API dw_mle_search(HWND handle, char *text, int point, unsigned long flags)
{
	NSLog(@"dw_mle_search() unimplemented\n");
	return 0;
}

/*
 * Stops redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to freeze.
 */
void API dw_mle_freeze(HWND handle)
{
	/* Don't think this is necessary */
}

/*
 * Resumes redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to thaw.
 */
void API dw_mle_thaw(HWND handle)
{
	/* Don't think this is necessary */
}

/*
 * Create a new status text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_status_text_new(char *text, ULONG id)
{
	NSTextField *textfield = dw_text_new(text, id);
	[textfield setBordered:YES];
	[textfield setBezeled:YES];
	[textfield setBezelStyle:NSTextFieldSquareBezel];
    [textfield setBackgroundColor:[NSColor colorWithDeviceRed:1 green:1 blue:1 alpha: 0]];
    [textfield setDrawsBackground:NO];
	return textfield;
}

/*
 * Create a new static text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_text_new(char *text, ULONG id)
{
	NSTextField *textfield = [[NSTextField alloc] init];
	[textfield setEditable:NO];
	[textfield setSelectable:NO];
	[textfield setBordered:NO];
	[textfield setDrawsBackground:NO];
	[textfield setStringValue:[ NSString stringWithUTF8String:text ]];
	return textfield;
}

/*
 * Creates a rendering context widget (window) to be packed.
 * Parameters:
 *       id: An id to be used with dw_window_from_id.
 * Returns:
 *       A handle to the widget or NULL on failure.
 */
HWND API dw_render_new(unsigned long id)
{
	DWRender *render = [[DWRender alloc] init];
	return render;
}

/* Sets the current foreground drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void API dw_color_foreground_set(unsigned long value)
{
    /* This may need to be thread specific */
	_foreground = _get_color(value);
}

/* Sets the current background drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void API dw_color_background_set(unsigned long value)
{
    /* This may need to be thread specific */
	_background = _get_color(value);
}

/* Allows the user to choose a color using the system's color chooser dialog.
 * Parameters:
 *       value: current color
 * Returns:
 *       The selected color or the current color if cancelled.
 */
unsigned long API dw_color_choose(unsigned long value)
{
	/* Create the File Save Dialog class. */
	DWColorChoose *colorDlg = (DWColorChoose *)[DWColorChoose sharedColorPanel];
    NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
	DWDialog *dialog = dw_dialog_new(colorDlg);
	
	/* Set defaults for the dialog. */
	[colorDlg setColor:color];
	[colorDlg setDialog:dialog];
	
	color = (NSColor *)dw_dialog_wait(dialog);
	[color release];
	return _foreground;
}

/* Draw a point on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void API dw_draw_point(HWND handle, HPIXMAP pixmap, int x, int y)
{
	id image = handle;
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
	}
	else
	{
		[image lockFocusIfCanDraw];
		_DWLastDrawable = handle;
	}
	NSBezierPath* aPath = [NSBezierPath bezierPath];
	[aPath setLineWidth: 0.5];
    NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
    [color set];
    [color release];
	
	[aPath moveToPoint:NSMakePoint(x, y)];	
	[aPath stroke];
    [aPath release];
	[image unlockFocus];
}

/* Draw a line on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x1: First X coordinate.
 *       y1: First Y coordinate.
 *       x2: Second X coordinate.
 *       y2: Second Y coordinate.
 */
void API dw_draw_line(HWND handle, HPIXMAP pixmap, int x1, int y1, int x2, int y2)
{
	id image = handle;
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
	}
	else
	{
		[image lockFocusIfCanDraw];
		_DWLastDrawable = handle;
	}
	NSBezierPath* aPath = [NSBezierPath bezierPath];
	[aPath setLineWidth: 0.5];
    NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
    [color set];
    [color release];
	
	[aPath moveToPoint:NSMakePoint(x1, y1)];	
	[aPath lineToPoint:NSMakePoint(x2, y2)];
	[aPath stroke];
    [aPath release];
	
	[image unlockFocus];
}

/* Draw text on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x: X coordinate.
 *       y: Y coordinate.
 *       text: Text to be displayed.
 */
void API dw_draw_text(HWND handle, HPIXMAP pixmap, int x, int y, char *text)
{
	id image = handle;
	NSString *nstr = [ NSString stringWithUTF8String:text ];
	if(image)
	{
		if([image isMemberOfClass:[NSView class]])
		{
			[image lockFocusIfCanDraw];
            NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
            NSDictionary *dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                  /*[NSFont fontWithName:@"Helvetica" size:26], NSFontAttributeName,*/
                                  color, NSForegroundColorAttributeName, nil];
			[nstr drawAtPoint:NSMakePoint(x, y) withAttributes:dict];
            [dict release];
            [color release];
			[image unlockFocus];
		}
		_DWLastDrawable = handle;
	}
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
		NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
        NSDictionary *dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    /*[NSFont fontWithName:@"Helvetica" size:26], NSFontAttributeName,*/
                                    color, NSForegroundColorAttributeName, nil];
		[nstr drawAtPoint:NSMakePoint(x, y) withAttributes:dict];
        [dict release];
        [color release];
		[image unlockFocus];
	}
    [nstr release];
}

/* Query the width and height of a text string.
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       text: Text to be queried.
 *       width: Pointer to a variable to be filled in with the width.
 *       height Pointer to a variable to be filled in with the height.
 */
void API dw_font_text_extents_get(HWND handle, HPIXMAP pixmap, char *text, int *width, int *height)
{
	id image = handle;
	NSString *nstr = [[ NSString stringWithUTF8String:text ] autorelease];
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
	}
	else
	{
		[image lockFocusIfCanDraw];
	}
	NSDictionary *dict = [[NSDictionary alloc] init];
	NSSize size = [nstr sizeWithAttributes:dict];
    [dict release];
	if(width)
	{
		*width = size.width;
	}
	if(height)
	{
		*height = size.height;
	}
    [nstr release];
	[image unlockFocus];
}

/* Draw a polygon on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       fill: Fill box TRUE or FALSE.
 *       x: X coordinate.
 *       y: Y coordinate.
 *       width: Width of rectangle.
 *       height: Height of rectangle.
 */
void API dw_draw_polygon( HWND handle, HPIXMAP pixmap, int fill, int npoints, int *x, int *y )
{
	id image = handle;
	int z;
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
	}
	else
	{
		[image lockFocusIfCanDraw];
		_DWLastDrawable = handle;
	}
	NSBezierPath* aPath = [NSBezierPath bezierPath];
	[aPath setLineWidth: 0.5];
    NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
    [color set];
    [color release];
	
	[aPath moveToPoint:NSMakePoint(*x, *y)];
	for(z=1;z<npoints;z++)
	{
		[aPath lineToPoint:NSMakePoint(x[z], y[z])];
	}
	[aPath closePath];
	if(fill)
	{
		[aPath fill];
	}
	[aPath stroke];
    [aPath release];
	[image unlockFocus];
}

/* Draw a rectangle on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       fill: Fill box TRUE or FALSE.
 *       x: X coordinate.
 *       y: Y coordinate.
 *       width: Width of rectangle.
 *       height: Height of rectangle.
 */
void API dw_draw_rect(HWND handle, HPIXMAP pixmap, int fill, int x, int y, int width, int height)
{
	id image = handle;
	if(pixmap)
	{
		image = (id)pixmap->handle;
		[image lockFocus];
	}
	else
	{
		[image lockFocusIfCanDraw];
		_DWLastDrawable = handle;
	}
	NSBezierPath* aPath = [NSBezierPath bezierPath];
	[aPath setLineWidth: 0.5];
    NSColor *color = [NSColor colorWithDeviceRed: DW_RED_VALUE(_foreground)/255.0 green: DW_GREEN_VALUE(_foreground)/255.0 blue: DW_BLUE_VALUE(_foreground)/255.0 alpha: 1];
    [color set];
    [color release];
	
	[aPath moveToPoint:NSMakePoint(x, y)];	
	[aPath lineToPoint:NSMakePoint(x, y + height)];
	[aPath lineToPoint:NSMakePoint(x + width, y + height)];
	[aPath lineToPoint:NSMakePoint(x + width, y)];
    [aPath closePath];
    [aPath fill];
	[aPath stroke];
    [aPath release];
	[image unlockFocus];
}

/*
 * Create a tree object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_tree_new(ULONG id)
{
	NSLog(@"dw_tree_new() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Inserts an item into a tree window (widget) after another item.
 * Parameters:
 *          handle: Handle to the tree to be inserted.
 *          item: Handle to the item to be positioned after.
 *          title: The text title of the entry.
 *          icon: Handle to coresponding icon.
 *          parent: Parent handle or 0 if root.
 *          itemdata: Item specific data.
 */
HTREEITEM API dw_tree_insert_after(HWND handle, HTREEITEM item, char *title, HICN icon, HTREEITEM parent, void *itemdata)
{
	NSLog(@"dw_tree_insert_item_after() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Inserts an item into a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree to be inserted.
 *          title: The text title of the entry.
 *          icon: Handle to coresponding icon.
 *          parent: Parent handle or 0 if root.
 *          itemdata: Item specific data.
 */
HTREEITEM API dw_tree_insert(HWND handle, char *title, HICN icon, HTREEITEM parent, void *itemdata)
{
	NSLog(@"dw_tree_insert_item() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Gets the text an item in a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 */
char * API dw_tree_get_title(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_get_title() unimplemented\n");
	return NULL;
}

/*
 * Gets the text an item in a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 */
HTREEITEM API dw_tree_get_parent(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_get_parent() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Sets the text and icon of an item in a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 *          title: The text title of the entry.
 *          icon: Handle to coresponding icon.
 */
void API dw_tree_item_change(HWND handle, HTREEITEM item, char *title, HICN icon)
{
	NSLog(@"dw_tree_item_change() unimplemented\n");
}

/*
 * Sets the item data of a tree item.
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 *          itemdata: User defined data to be associated with item.
 */
void API dw_tree_item_set_data(HWND handle, HTREEITEM item, void *itemdata)
{
	NSLog(@"dw_tree_item_set_data() unimplemented\n");
}

/*
 * Gets the item data of a tree item.
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 */
void * API dw_tree_item_get_data(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_item_get_data() unimplemented\n");
	return NULL;
}

/*
 * Sets this item as the active selection.
 * Parameters:
 *       handle: Handle to the tree window (widget) to be selected.
 *       item: Handle to the item to be selected.
 */
void API dw_tree_item_select(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_item_select() unimplemented\n");
}

/*
 * Removes all nodes from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 */
void API dw_tree_clear(HWND handle)
{
	NSLog(@"dw_tree_clear() unimplemented\n");
}

/*
 * Expands a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be expanded.
 */
void API dw_tree_item_expand(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_item_expand() unimplemented\n");
}

/*
 * Collapses a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be collapsed.
 */
void API dw_tree_item_collapse(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_item_collapse() unimplemented\n");
}

/*
 * Removes a node from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       item: Handle to node to be deleted.
 */
void API dw_tree_item_delete(HWND handle, HTREEITEM item)
{
	NSLog(@"dw_tree_item_delete() unimplemented\n");
}

/*
 * Create a container object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_container_new(ULONG id, int multi)
{
	DWContainer *cont = _cont_new(id, multi);
    NSScrollView *scrollview = [cont scrollview];
    [scrollview setHasHorizontalScroller:YES];
	NSTableHeaderView *header = [[NSTableHeaderView alloc] init];
	[cont setHeaderView:header];
	return cont;
}

/*
 * Sets up the container columns.
 * Parameters:
 *          handle: Handle to the container to be configured.
 *          flags: An array of unsigned longs with column flags.
 *          titles: An array of strings with column text titles.
 *          count: The number of columns (this should match the arrays).
 *          separator: The column number that contains the main separator.
 *                     (this item may only be used in OS/2)
 */
int API dw_container_setup(HWND handle, unsigned long *flags, char **titles, int count, int separator)
{
	int z;
	DWContainer *cont = handle;
	
	[cont setup];
	
	for(z=0;z<count;z++)
	{
		NSTableColumn *column = [[NSTableColumn alloc] init];
		[[column headerCell] setStringValue:[ NSString stringWithUTF8String:titles[z] ]];
        if(flags[z] & DW_CFA_BITMAPORICON)
        {
            NSImageCell *imagecell = [[[NSImageCell alloc] init] autorelease];
            [column setDataCell:imagecell];
            if(z == 0 && titles[z] && strcmp(titles[z], "Icon") == 0)
            {
                [column setResizingMask:NSTableColumnNoResizing];
                [column setWidth:20];
            }
        }
		[cont addTableColumn:column];
		[cont addColumn:column andType:(int)flags[z]];
	}	

	return TRUE;
}

/*
 * Sets up the filesystem columns, note: filesystem always has an icon/filename field.
 * Parameters:
 *          handle: Handle to the container to be configured.
 *          flags: An array of unsigned longs with column flags.
 *          titles: An array of strings with column text titles.
 *          count: The number of columns (this should match the arrays).
 */
int API dw_filesystem_setup(HWND handle, unsigned long *flags, char **titles, int count)
{
	char **newtitles = malloc(sizeof(char *) * (count + 2));
	unsigned long *newflags = malloc(sizeof(unsigned long) * (count + 2));
	
	newtitles[0] = "Icon";
	newtitles[1] = "Filename";
	
	newflags[0] = DW_CFA_BITMAPORICON | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR;
	newflags[1] = DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR;	
	
	memcpy(&newtitles[2], titles, sizeof(char *) * count);
	memcpy(&newflags[2], flags, sizeof(unsigned long) * count);
	
	dw_container_setup(handle, newflags, newtitles, count + 2, 0);
	
	free(newtitles);
	free(newflags);
	return TRUE;
}

/*
 * Allocates memory used to populate a container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          rowcount: The number of items to be populated.
 */
void * API dw_container_alloc(HWND handle, int rowcount)
{
	DWContainer *cont = handle;
	[cont addRows:rowcount];
	return cont;
}

/*
 * Sets an item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_container_set_item(HWND handle, void *pointer, int column, int row, void *data)
{
	DWContainer *cont = handle;
	id object = nil;
	int type = [cont cellType:column];
	int lastadd = [cont lastAddPoint];
    
    if(!data)
    {
        return;
    }
	if(type & DW_CFA_BITMAPORICON)
	{
		object = *((NSImage **)data);
	}
	else if(type & DW_CFA_STRING)
	{
		object = [ NSString stringWithUTF8String:data ];
	}
	else 
	{
		char textbuffer[100];
		
		if(type & DW_CFA_ULONG)
		{
			ULONG tmp = *((ULONG *)data);
			
			sprintf(textbuffer, "%lu", tmp);
		}
		else if(type & DW_CFA_DATE)
		{
			struct tm curtm;
			CDATE cdate = *((CDATE *)data);
			
			memset( &curtm, 0, sizeof(curtm) );
			curtm.tm_mday = cdate.day;
			curtm.tm_mon = cdate.month - 1;
			curtm.tm_year = cdate.year - 1900;
			
			strftime(textbuffer, 100, "%x", &curtm);
		}
		else if(type & DW_CFA_TIME)
		{
			struct tm curtm;
			CTIME ctime = *((CTIME *)data);
			
			memset( &curtm, 0, sizeof(curtm) );
			curtm.tm_hour = ctime.hours;
			curtm.tm_min = ctime.minutes;
			curtm.tm_sec = ctime.seconds;
			
			strftime(textbuffer, 100, "%X", &curtm);
		}
		else 
		{
			return;
		}
		object = [ NSString stringWithUTF8String:textbuffer ];
	}
	
	[cont editCell:object at:(row+lastadd) and:column];
}

/*
 * Changes an existing item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_container_change_item(HWND handle, int column, int row, void *data)
{
	dw_container_set_item(handle, NULL, column, row, data);
}

/*
 * Changes an existing item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_filesystem_change_item(HWND handle, int column, int row, void *data)
{
	dw_container_change_item(handle, column+2, row, data);
}

/*
 * Changes an item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_filesystem_change_file(HWND handle, int row, char *filename, HICN icon)
{
    dw_container_change_item(handle, 0, row, &icon);
	dw_container_change_item(handle, 1, row, filename);
}

/*
 * Sets an item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_filesystem_set_file(HWND handle, void *pointer, int row, char *filename, HICN icon)
{
    dw_container_set_item(handle, pointer, 0, row, &icon);
	dw_container_set_item(handle, pointer, 1, row, filename);
}

/*
 * Sets an item in specified row and column to the given data.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          column: Zero based column of data being set.
 *          row: Zero based row of data being set.
 *          data: Pointer to the data to be added.
 */
void API dw_filesystem_set_item(HWND handle, void *pointer, int column, int row, void *data)
{
	dw_container_set_item(handle, pointer, column+2, row, data);
}

/*
 * Gets column type for a container column
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column.
 */
int API dw_container_get_column_type(HWND handle, int column)
{
	DWContainer *cont = handle;
	return [cont cellType:column];
}

/*
 * Gets column type for a filesystem container column
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column.
 */
int API dw_filesystem_get_column_type(HWND handle, int column)
{
	DWContainer *cont = handle;
	return [cont cellType:column+2];
}

/*
 * Sets the width of a column in the container.
 * Parameters:
 *          handle: Handle to window (widget) of container.
 *          column: Zero based column of width being set.
 *          width: Width of column in pixels.
 */
void API dw_container_set_column_width(HWND handle, int column, int width)
{
    DWContainer *cont = handle;
    NSTableColumn *col = [cont getColumn:column];
    
    [col setWidth:width];
}

/*
 * Sets the title of a row in the container.
 * Parameters:
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          row: Zero based row of data being set.
 *          title: String title of the item.
 */
void API dw_container_set_row_title(void *pointer, int row, char *title)
{
	DWContainer *cont = pointer;
	return [cont setRow:row title:title];
}

/*
 * Sets the title of a row in the container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          rowcount: The number of rows to be inserted.
 */
void API dw_container_insert(HWND handle, void *pointer, int rowcount)
{
	DWContainer *cont = handle;
	return [cont reloadData];
}

/*
 * Removes all rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       redraw: TRUE to cause the container to redraw immediately.
 */
void API dw_container_clear(HWND handle, int redraw)
{
	DWContainer *cont = handle;
	[cont clear];
    if(redraw)
    {
        [cont reloadData];
    }
}

/*
 * Removes the first x rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be deleted from.
 *       rowcount: The number of rows to be deleted.
 */
void API dw_container_delete(HWND handle, int rowcount)
{
    DWContainer *cont = handle;
    int x;
    
    for(x=0;x<rowcount;x++)
    {
        [cont removeRow:0];
    }
}

/*
 * Scrolls container up or down.
 * Parameters:
 *       handle: Handle to the window (widget) to be scrolled.
 *       direction: DW_SCROLL_UP, DW_SCROLL_DOWN, DW_SCROLL_TOP or
 *                  DW_SCROLL_BOTTOM. (rows is ignored for last two)
 *       rows: The number of rows to be scrolled.
 */
void API dw_container_scroll(HWND handle, int direction, long rows)
{
	NSLog(@"dw_container_scroll() unimplemented\n");
}

/*
 * Starts a new query of a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       flags: If this parameter is DW_CRA_SELECTED it will only
 *              return items that are currently selected.  Otherwise
 *              it will return all records in the container.
 */
char * API dw_container_query_start(HWND handle, unsigned long flags)
{
    DWContainer *cont = handle;
    NSIndexSet *selected = [cont selectedRowIndexes];
    NSUInteger result = [selected indexGreaterThanOrEqualToIndex:0];
    char *retval = NULL;
    
    if(result != NSNotFound)
    {
        retval = [cont getRowTitle:(int)result];
        [cont setLastQueryPoint:(int)result];
    }
    [selected release];
	return retval;
}

/*
 * Continues an existing query of a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       flags: If this parameter is DW_CRA_SELECTED it will only
 *              return items that are currently selected.  Otherwise
 *              it will return all records in the container.
 */
char * API dw_container_query_next(HWND handle, unsigned long flags)
{
    DWContainer *cont = handle;
    int lastQueryPoint = [cont lastQueryPoint];
    NSIndexSet *selected = [cont selectedRowIndexes];
    NSUInteger result = [selected indexGreaterThanIndex:lastQueryPoint];
    char *retval = NULL;
    
    if(result != NSNotFound)
    {
        retval = [cont getRowTitle:(int)result];
        [cont setLastQueryPoint:(int)result];
    }
    [selected release];
	return retval;
}

/*
 * Cursors the item with the text speficied, and scrolls to that item.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       text:  Text usually returned by dw_container_query().
 */
void API dw_container_cursor(HWND handle, char *text)
{
    DWContainer *cont = handle;
    char *thistext;
    int x, count = (int)[cont numberOfRowsInTableView:cont];
    
    for(x=0;x<count;x++)
    {
        thistext = [cont getRowTitle:x];
        
        if(thistext == text)
        {
            NSIndexSet *selected = [[NSIndexSet alloc] initWithIndex:(NSUInteger)x];
            
            [cont selectRowIndexes:selected byExtendingSelection:YES];
            [selected release];
        }
    }
}

/*
 * Deletes the item with the text speficied.
 * Parameters:
 *       handle: Handle to the window (widget).
 *       text:  Text usually returned by dw_container_query().
 */
void API dw_container_delete_row(HWND handle, char *text)
{
    DWContainer *cont = handle;
    char *thistext;
    int x, count = (int)[cont numberOfRowsInTableView:cont];
    
    for(x=0;x<count;x++)
    {
        thistext = [cont getRowTitle:x];
        
        if(thistext == text)
        {
            [cont removeRow:x];
            return;
        }
    }
}

/*
 * Optimizes the column widths so that all data is visible.
 * Parameters:
 *       handle: Handle to the window (widget) to be optimized.
 */
void API dw_container_optimize(HWND handle)
{
	DWContainer *cont = handle;
	/*[cont sizeToFit];*/
	[cont setColumnAutoresizingStyle:NSTableViewUniformColumnAutoresizingStyle];
}

/*
 * Inserts an icon into the taskbar.
 * Parameters:
 *       handle: Window handle that will handle taskbar icon messages.
 *       icon: Icon handle to display in the taskbar.
 *       bubbletext: Text to show when the mouse is above the icon.
 */
void API dw_taskbar_insert(HWND handle, HICN icon, char *bubbletext)
{
	NSLog(@"dw_taskbar_insert() unimplemented\n");
}

/*
 * Deletes an icon from the taskbar.
 * Parameters:
 *       handle: Window handle that was used with dw_taskbar_insert().
 *       icon: Icon handle that was used with dw_taskbar_insert().
 */
void API dw_taskbar_delete(HWND handle, HICN icon)
{
	NSLog(@"dw_taskbar_delete() unimplemented\n");
}

/*
 * Obtains an icon from a module (or header in GTK).
 * Parameters:
 *          module: Handle to module (DLL) in OS/2 and Windows.
 *          id: A unsigned long id int the resources on OS/2 and
 *              Windows, on GTK this is converted to a pointer
 *              to an embedded XPM.
 */
HICN API dw_icon_load(unsigned long module, unsigned long resid)
{
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *respath = [bundle resourcePath];
    NSString *filepath = [respath stringByAppendingFormat:@"/%u.png", resid]; 
	NSImage *image = [[NSImage alloc] initWithContentsOfFile:filepath];
    [bundle release];
    [respath release];
    [filepath release];
	return image;
}

/*
 * Obtains an icon from a file.
 * Parameters:
 *       filename: Name of the file, omit extention to have
 *                 DW pick the appropriate file extension.
 *                 (ICO on OS/2 or Windows, XPM on Unix)
 */
HICN API dw_icon_load_from_file(char *filename)
{
    NSString *nstr = [ NSString stringWithUTF8String:filename ];
    NSImage *image = [[NSImage alloc] initWithContentsOfFile:nstr];
    if(!image)
    {
        nstr = [nstr stringByAppendingString:@".png"];
        image = [[NSImage alloc] initWithContentsOfFile:nstr];
    }
    [nstr release];
	return image;
}

/*
 * Obtains an icon from data
 * Parameters:
 *       filename: Name of the file, omit extention to have
 *                 DW pick the appropriate file extension.
 *                 (ICO on OS/2 or Windows, XPM on Unix)
 */
HICN API dw_icon_load_from_data(char *data, int len)
{
	NSData *thisdata = [[[NSData alloc] dataWithBytes:data length:len] autorelease];
	NSImage *image = [[NSImage alloc] initWithData:thisdata];
    [thisdata release];
	return image;
}

/*
 * Frees a loaded resource in OS/2 and Windows.
 * Parameters:
 *          handle: Handle to icon returned by dw_icon_load().
 */
void API dw_icon_free(HICN handle)
{
    NSImage *image = handle;
    [image release];
}

/*
 * Create a new MDI Frame to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id or 0L.
 */
HWND API dw_mdi_new(unsigned long id)
{
	NSLog(@"dw_mdi_new() unimplemented\n");
	return HWND_DESKTOP;
}

/*
 * Creates a splitbar window (widget) with given parameters.
 * Parameters:
 *       type: Value can be DW_VERT or DW_HORZ.
 *       topleft: Handle to the window to be top or left.
 *       bottomright:  Handle to the window to be bottom or right.
 * Returns:
 *       A handle to a splitbar window or NULL on failure.
 */
HWND API dw_splitbar_new(int type, HWND topleft, HWND bottomright, unsigned long id)
{
	DWSplitBar *split = [[DWSplitBar alloc] init];
    HWND tmpbox = dw_box_new(DW_VERT, 0); 
    [split setDelegate:split];
    dw_box_pack_start(tmpbox, topleft, 0, 0, TRUE, TRUE, 0);
	[split addSubview:tmpbox];
    tmpbox = dw_box_new(DW_VERT, 0);    
    dw_box_pack_start(tmpbox, bottomright, 0, 0, TRUE, TRUE, 0);
	[split addSubview:tmpbox];
	if(type == DW_VERT)
	{
		[split setVertical:NO];
	}
    else
    {
        [split setVertical:YES];
    }
	return split;
}

/*
 * Sets the position of a splitbar (pecentage).
 * Parameters:
 *       handle: The handle to the splitbar returned by dw_splitbar_new().
 */
void API dw_splitbar_set(HWND handle, float percent)
{
	DWSplitBar *split = handle;
	[split setPosition:percent ofDividerAtIndex:0];
}

/*
 * Gets the position of a splitbar (pecentage).
 * Parameters:
 *       handle: The handle to the splitbar returned by dw_splitbar_new().
 */
float API dw_splitbar_get(HWND handle)
{
	NSLog(@"dw_splitbar_get() unimplemented\n");
	return 0.0;
}

/*
 * Create a bitmap object to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_bitmap_new(ULONG id)
{
	NSImageView *bitmap = [[NSImageView alloc] init];
	[bitmap setImageFrameStyle:NSImageFrameNone];
	[bitmap setEditable:NO];
	return bitmap;
}

/*
 * Creates a pixmap with given parameters.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       width: Width of the pixmap in pixels.
 *       height: Height of the pixmap in pixels.
 *       depth: Color depth of the pixmap.
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP API dw_pixmap_new(HWND handle, unsigned long width, unsigned long height, int depth)
{
	NSSize size = { (float)width, (float)height };
	HPIXMAP pixmap;
	
	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;
	pixmap->width = width;
	pixmap->height = height;
	NSImage *image = pixmap->handle = [[NSImage alloc] initWithSize:size];
    [image setFlipped:YES];
	return pixmap;
}

/*
 * Creates a pixmap from a file.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       filename: Name of the file, omit extention to have
 *                 DW pick the appropriate file extension.
 *                 (BMP on OS/2 or Windows, XPM on Unix)
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP API dw_pixmap_new_from_file(HWND handle, char *filename)
{
	HPIXMAP pixmap;
	
	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;
    NSString *nstr = [ NSString stringWithUTF8String:filename ];
    NSImage *image = [[NSImage alloc] initWithContentsOfFile:nstr];
    if(!image)
    {
        nstr = [nstr stringByAppendingString:@".png"];
        image = [[NSImage alloc] initWithContentsOfFile:nstr];
    }
    [nstr release];
	NSSize size = [image size];
	pixmap->width = size.width;
	pixmap->height = size.height;
	pixmap->handle = image;
	return pixmap;
}

/*
 * Creates a pixmap from memory.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       data: Source of the image data
 *                 (BMP on OS/2 or Windows, XPM on Unix)
 *       le: length of data
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP API dw_pixmap_new_from_data(HWND handle, char *data, int len)
{
	HPIXMAP pixmap;
	
	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;
	NSData *thisdata = [[[NSData alloc] dataWithBytes:data length:len] autorelease];
	NSImage *image = [[NSImage alloc] initWithData:thisdata];
	NSSize size = [image size];
	pixmap->width = size.width;
	pixmap->height = size.height;
	pixmap->handle = image;
    [thisdata release];
	return pixmap;
}

/*
 * Creates a bitmap mask for rendering bitmaps with transparent backgrounds
 */
void API dw_pixmap_set_transparent_color( HPIXMAP pixmap, ULONG color )
{
	NSLog(@"dw_pixmap_set_transparent_color() unimplemented\n");
}

/*
 * Creates a pixmap from internal resource graphic specified by id.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       id: Resource ID associated with requested pixmap.
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP API dw_pixmap_grab(HWND handle, ULONG resid)
{
	HPIXMAP pixmap;
	
	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;
    
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *respath = [bundle resourcePath];
    NSString *filepath = [respath stringByAppendingFormat:@"/%u.png", resid]; 
	NSImage *image = [[NSImage alloc] initWithContentsOfFile:filepath];
	NSSize size = [image size];
	pixmap->width = size.width;
	pixmap->height = size.height;
	pixmap->handle = image;
    [bundle release];
    [respath release];
    [filepath release];
    [image release];
	return pixmap;
}

/*
 * Destroys an allocated pixmap.
 * Parameters:
 *       pixmap: Handle to a pixmap returned by
 *               dw_pixmap_new..
 */
void API dw_pixmap_destroy(HPIXMAP pixmap)
{
	NSImage *image = (NSImage *)pixmap->handle;
	[image dealloc];
	free(pixmap);
}

/*
 * Copies from one item to another.
 * Parameters:
 *       dest: Destination window handle.
 *       destp: Destination pixmap. (choose only one).
 *       xdest: X coordinate of destination.
 *       ydest: Y coordinate of destination.
 *       width: Width of area to copy.
 *       height: Height of area to copy.
 *       src: Source window handle.
 *       srcp: Source pixmap. (choose only one).
 *       xsrc: X coordinate of source.
 *       ysrc: Y coordinate of source.
 */
void API dw_pixmap_bitblt(HWND dest, HPIXMAP destp, int xdest, int ydest, int width, int height, HWND src, HPIXMAP srcp, int xsrc, int ysrc)
{
	id bltdest = dest;
	id bltsrc = src;
	if(destp)
	{
		bltdest = (id)destp->handle;
		[bltdest lockFocus];
	}
	else
	{
		[bltdest lockFocusIfCanDraw];
		_DWLastDrawable = dest;
	}
	if(srcp)
	{
		bltsrc = (id)srcp->handle;
		NSImage *image = bltsrc;
		[image drawAtPoint:NSMakePoint(xdest, ydest) fromRect:NSMakeRect(xsrc, ysrc, width, height) operation:NSCompositeCopy fraction:1.0];
	}
	[bltdest unlockFocus];
}

/*
 * Create a new static text window (widget) to be packed.
 * Not available under OS/2, eCS
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_calendar_new(ULONG id)
{
	DWCalendar *calendar = [[DWCalendar alloc] init];
	/*[calendar setDatePickerMode:UIDatePickerModeDate];*/
	return calendar;
}

/*
 * Sets the current date of a calendar
 * Parameters:
 *       handle: The handle to the calendar returned by dw_calendar_new().
 *       year...
 */
void dw_calendar_set_date(HWND handle, unsigned int year, unsigned int month, unsigned int day)
{
	DWCalendar *calendar = handle;
	NSDate *date;
	char buffer[100];
	
	sprintf(buffer, "%04d-%02d-%02d 00:00:00 +0600", year, month, day);
	
	date = [[NSDate alloc] initWithString:[ NSString stringWithUTF8String:buffer ]];
	[calendar setDateValue:date];
}

/*
 * Gets the position of a splitbar (pecentage).
 * Parameters:
 *       handle: The handle to the splitbar returned by dw_splitbar_new().
 */
void dw_calendar_get_date(HWND handle, unsigned int *year, unsigned int *month, unsigned int *day)
{
	DWCalendar *calendar = handle;
	NSDate *date = [calendar dateValue];
	NSDateFormatter *df = [[NSDateFormatter alloc] init];
	NSString *nstr = [df stringFromDate:date];
	sscanf([ nstr UTF8String ], "%d-%d-%d", year, month, day);
}

/*
 * Causes the embedded HTML widget to take action.
 * Parameters:
 *       handle: Handle to the window.
 *       action: One of the DW_HTML_* constants.
 */
void API dw_html_action(HWND handle, int action)
{
	WebView *html = handle;
	switch(action)
	{
		case DW_HTML_GOBACK:
			[html goBack];
			break;
		case DW_HTML_GOFORWARD:
			[html goForward];
			break;
		case DW_HTML_GOHOME:
			break;
		case DW_HTML_SEARCH:
			break;
		case DW_HTML_RELOAD:
			[html reload:html];
			break;
		case DW_HTML_STOP:
			[html stopLoading:html];
			break;
		case DW_HTML_PRINT:
			break;
	}
}

/*
 * Render raw HTML code in the embedded HTML widget..
 * Parameters:
 *       handle: Handle to the window.
 *       string: String buffer containt HTML code to
 *               be rendered.
 * Returns:
 *       0 on success.
 */
int API dw_html_raw(HWND handle, char *string)
{
	WebView *html = handle;
	[[html mainFrame] loadHTMLString:[ NSString stringWithUTF8String:string ] baseURL:nil];
	return 0;
}

/*
 * Render file or web page in the embedded HTML widget..
 * Parameters:
 *       handle: Handle to the window.
 *       url: Universal Resource Locator of the web or
 *               file object to be rendered.
 * Returns:
 *       0 on success.
 */
int API dw_html_url(HWND handle, char *url)
{
	WebView *html = handle;
	[[html mainFrame] loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[ NSString stringWithUTF8String:url ]]]];
	return 0;
}

/*
 * Create a new HTML window (widget) to be packed.
 * Not available under OS/2, eCS
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_html_new(unsigned long id)
{
	WebView *web = [[WebView alloc] init];
	return web;
}

/*
 * Returns the current X and Y coordinates of the mouse pointer.
 * Parameters:
 *       x: Pointer to variable to store X coordinate.
 *       y: Pointer to variable to store Y coordinate.
 */
void API dw_pointer_query_pos(long *x, long *y)
{
    NSPoint mouseLoc; 
    mouseLoc = [NSEvent mouseLocation];
	if(x)
	{
		*x = mouseLoc.x;
	}
	if(y)
	{
		*y = mouseLoc.y;
	}
}

/*
 * Sets the X and Y coordinates of the mouse pointer.
 * Parameters:
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void API dw_pointer_set_pos(long x, long y)
{
	/* From what I have read this isn't possible, agaist human interface rules */
}

/*
 * Create a menu object to be popped up.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HMENUI API dw_menu_new(ULONG id)
{
	NSMenu *menu = [[[NSMenu alloc] init] autorelease];
	[menu setAutoenablesItems:NO];
	return menu;
}

/*
 * Create a menubar on a window.
 * Parameters:
 *       location: Handle of a window frame to be attached to.
 */
HMENUI API dw_menubar_new(HWND location)
{
	NSWindow *window = location;
	NSMenu *windowmenu = _generate_main_menu();
	[[window contentView] setMenu:windowmenu];
	return (HMENUI)windowmenu;
}

/*
 * Destroys a menu created with dw_menubar_new or dw_menu_new.
 * Parameters:
 *       menu: Handle of a menu.
 */
void API dw_menu_destroy(HMENUI *menu)
{
	NSMenu *thismenu = *menu;
	[thismenu release];
}

/*
 * Pops up a context menu at given x and y coordinates.
 * Parameters:
 *       menu: The handle the the existing menu.
 *       parent: Handle to the window initiating the popup.
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void API dw_menu_popup(HMENUI *menu, HWND parent, int x, int y)
{
	NSMenu *thismenu = (NSMenu *)menu;
	NSView *view = parent;
	NSEvent* fake = [[NSEvent alloc]
					mouseEventWithType:NSLeftMouseDown
					location:NSMakePoint(x,y)
					modifierFlags:0
					timestamp:1
					windowNumber:[[view window] windowNumber]
					context:[NSGraphicsContext currentContext]
					eventNumber:1
					clickCount:1
					pressure:0.0];
	[NSMenu popUpContextMenu:thismenu withEvent:fake forView:view];
}

char _removetilde(char *dest, char *src)
{
	int z, cur=0;
	char accel = '\0';
	
	for(z=0;z<strlen(src);z++)
	{
		if(src[z] != '~')
		{
			dest[cur] = src[z];
			cur++;
		}
		else
		{
			accel = src[z+1];
		}
	}
	dest[cur] = 0;
	return accel;
}

/*
 * Adds a menuitem or submenu to an existing menu.
 * Parameters:
 *       menu: The handle the the existing menu.
 *       title: The title text on the menu item to be added.
 *       id: An ID to be used for message passing.
 *       flags: Extended attributes to set on the menu.
 *       end: If TRUE memu is positioned at the end of the menu.
 *       check: If TRUE menu is "check"able.
 *       flags: Extended attributes to set on the menu.
 *       submenu: Handle to an existing menu to be a submenu or NULL.
 */
HWND API dw_menu_append_item(HMENUI menux, char *title, ULONG itemid, ULONG flags, int end, int check, HMENUI submenux)
{
	NSMenu *menu = menux;
	NSMenu *submenu = submenux;
	NSMenuItem *item = NULL;
	if(strlen(title) == 0)
	{
		[menu addItem:[NSMenuItem separatorItem]];
	}
	else 
	{
		char accel[2];
		char *newtitle = malloc(strlen(title)+1);
		NSString *nstr;
	
		accel[0] = _removetilde(newtitle, title);
		accel[1] = 0;
	
		nstr = [ NSString stringWithUTF8String:newtitle ];
		free(newtitle);

		item = [menu addItemWithTitle:	nstr
										action:@selector(menuHandler:)
										keyEquivalent:[ NSString stringWithUTF8String:accel ]];
		[item setTag:itemid];
		if(flags & DW_MIS_CHECKED)
		{
			[item setState:NSOnState];
		}
		if(flags & DW_MIS_DISABLED)
		{
			[item setEnabled:NO];
		}			
		
		if(submenux)
		{
			[submenu setTitle:nstr];
			[menu setSubmenu:submenu forItem:item];
		}
		return item;
	}
	return item;
}

/*
 * Sets the state of a menu item check.
 * Deprecated; use dw_menu_item_set_state()
 * Parameters:
 *       menu: The handle the the existing menu.
 *       id: Menuitem id.
 *       check: TRUE for checked FALSE for not checked.
 */
void API dw_menu_item_set_check(HMENUI menux, unsigned long itemid, int check)
{
	id menu = menux;
	NSMenuItem *menuitem = (NSMenuItem *)[menu itemWithTag:itemid];
	
	if(menuitem != nil)
	{
		if(check)
		{
			[menuitem setState:NSOnState];
		}
		else
		{	
			[menuitem setState:NSOffState];
		}
	}
}

/*
 * Sets the state of a menu item.
 * Parameters:
 *       menu: The handle to the existing menu.
 *       id: Menuitem id.
 *       flags: DW_MIS_ENABLED/DW_MIS_DISABLED
 *              DW_MIS_CHECKED/DW_MIS_UNCHECKED
 */
void API dw_menu_item_set_state(HMENUI menux, unsigned long itemid, unsigned long state)
{
	id menu = menux;
	NSMenuItem *menuitem = (NSMenuItem *)[menu itemWithTag:itemid];
	
	if(menuitem != nil)
	{
		if(state & DW_MIS_CHECKED)
		{
			[menuitem setState:NSOnState];
		}
		else if(state & DW_MIS_UNCHECKED)
		{	
			[menuitem setState:NSOffState];
		}
		if(state & DW_MIS_ENABLED)
		{
			[menuitem setEnabled:YES];
		}
		else if(state & DW_MIS_DISABLED)
		{
			[menuitem setEnabled:NO];
		}
	}
}

/* Gets the notebook page from associated ID */
DWNotebookPage *_notepage_from_id(DWNotebook *notebook, unsigned long pageid)
{
	NSArray *pages = [notebook tabViewItems];
	for(DWNotebookPage *notepage in pages) 
	{
		if([notepage pageid] == pageid)
		{
			return notepage;
		}
	}
	return nil;
}

/*
 * Create a notebook object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_notebook_new(ULONG id, int top)
{
	DWNotebook *notebook = [[DWNotebook alloc] init];
	[notebook setDelegate:notebook];
	return notebook;
}

/*
 * Adds a new page to specified notebook.
 * Parameters:
 *          handle: Window (widget) handle.
 *          flags: Any additional page creation flags.
 *          front: If TRUE page is added at the beginning.
 */
unsigned long API dw_notebook_page_new(HWND handle, ULONG flags, int front)
{
	DWNotebook *notebook = handle;
	NSInteger page = [notebook pageid];
	DWNotebookPage *notepage = [[DWNotebookPage alloc] initWithIdentifier:nil];
	[notepage setPageid:(NSInteger)page];
	if(front)
	{
		[notebook insertTabViewItem:notepage atIndex:(NSInteger)0];
	}
	else
	{
		[notebook addTabViewItem:notepage];
	}
	[notebook setPageid:(page+1)];
	return (unsigned long)page;
}

/*
 * Remove a page from a notebook.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be destroyed.
 */
void API dw_notebook_page_destroy(HWND handle, unsigned int pageid)
{
	DWNotebook *notebook = handle;
	DWNotebookPage *notepage = _notepage_from_id(notebook, pageid);
	
	if(notepage != nil)
	{
		[notebook removeTabViewItem:notepage];
		[notepage release];
	}
}

/*
 * Queries the currently visible page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 */
unsigned long API dw_notebook_page_get(HWND handle)
{
	DWNotebook *notebook = handle;
	DWNotebookPage *notepage = (DWNotebookPage *)[notebook selectedTabViewItem];
	return [notepage pageid];
}

/*
 * Sets the currently visibale page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be made visible.
 */
void API dw_notebook_page_set(HWND handle, unsigned int pageid)
{
	DWNotebook *notebook = handle;
	DWNotebookPage *notepage = _notepage_from_id(notebook, pageid);
	
	if(notepage != nil)
	{
		[notebook selectTabViewItem:notepage];
	}
}

/*
 * Sets the text on the specified notebook tab.
 * Parameters:
 *          handle: Notebook handle.
 *          pageid: Page ID of the tab to set.
 *          text: Pointer to the text to set.
 */
void API dw_notebook_page_set_text(HWND handle, ULONG pageid, char *text)
{
	DWNotebook *notebook = handle;
	DWNotebookPage *notepage = _notepage_from_id(notebook, pageid);
	
	if(notepage != nil)
	{
		[notepage setLabel:[ NSString stringWithUTF8String:text ]];
	}
}

/*
 * Sets the text on the specified notebook tab status area.
 * Parameters:
 *          handle: Notebook handle.
 *          pageid: Page ID of the tab to set.
 *          text: Pointer to the text to set.
 */
void API dw_notebook_page_set_status_text(HWND handle, ULONG pageid, char *text)
{
	/* Note supported here... do nothing */
}

/*
 * Packs the specified box into the notebook page.
 * Parameters:
 *          handle: Handle to the notebook to be packed.
 *          pageid: Page ID in the notebook which is being packed.
 *          page: Box handle to be packed.
 */
void API dw_notebook_pack(HWND handle, ULONG pageid, HWND page)
{
	DWNotebook *notebook = handle;
	DWNotebookPage *notepage = _notepage_from_id(notebook, pageid);
	
	if(notepage != nil)
	{
        HWND tmpbox = dw_box_new(DW_VERT, 0);
        DWBox *box = tmpbox;
        
        dw_box_pack_start(tmpbox, page, 0, 0, TRUE, TRUE, 0);
		[notepage setView:box];
	}
}

/*
 * Create a new Window Frame.
 * Parameters:
 *       owner: The Owner's window handle or HWND_DESKTOP.
 *       title: The Window title.
 *       flStyle: Style flags, see the PM reference.
 */
HWND API dw_window_new(HWND hwndOwner, char *title, ULONG flStyle)
{
	NSRect frame = NSMakeRect(1,1,1,1);
    NSWindow *window = [[NSWindow alloc]
						initWithContentRect:frame
						styleMask:(flStyle | NSTexturedBackgroundWindowMask)
						backing:NSBackingStoreBuffered
						defer:false];

    [window setTitle:[ NSString stringWithUTF8String:title ]];
	
    DWView *view = [[DWView alloc] init];
	
    [window setContentView:view];
    [window setDelegate:view];
    [window makeKeyAndOrderFront:nil];
	[window setAllowsConcurrentViewDrawing:NO];
	
	return (HWND)window;
}

/*
 * Call a function from the window (widget)'s context.
 * Parameters:
 *       handle: Window handle of the widget.
 *       function: Function pointer to be called.
 *       data: Pointer to the data to be passed to the function.
 */
void API dw_window_function(HWND handle, void *function, void *data)
{
	void (* windowfunc)(void *);
	
	windowfunc = function;
	
	if(windowfunc)
		windowfunc(data);	
}


/*
 * Changes the appearance of the mouse pointer.
 * Parameters:
 *       handle: Handle to widget for which to change.
 *       cursortype: ID of the pointer you want.
 */
void API dw_window_set_pointer(HWND handle, int pointertype)
{
    id object = handle;
    
	if([ object isKindOfClass:[ NSView class ] ])
	{
		NSView *view = handle;
        
        if(pointertype == DW_POINTER_DEFAULT)
        {
            [view discardCursorRects];
        }
        else if(pointertype == DW_POINTER_ARROW)
        {
            NSRect rect = [view frame];
            NSCursor *cursor = [NSCursor arrowCursor];
        
            [view addCursorRect:rect cursor:cursor];
        }
        /* No cursor for DW_POINTER_CLOCK? */
    }
}

/*
 * Makes the window visible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int API dw_window_show(HWND handle)
{
	NSObject *object = handle;
	
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		if([window isMiniaturized])
		{
			[window deminiaturize:nil];
		}
		[[window contentView] windowResized:nil];
        [[window contentView] windowDidBecomeMain:nil];
	}
	return 0;
}

/*
 * Makes the window invisible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int API dw_window_hide(HWND handle)
{
	NSLog(@"dw_window_hide() unimplemented\n");
	return 0;
}

/*
 * Sets the colors used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          fore: Foreground color in DW_RGB format or a default color index.
 *          back: Background color in DW_RGB format or a default color index.
 */
int API dw_window_set_color(HWND handle, ULONG fore, ULONG back)
{
	id object = handle;
	unsigned long _fore = _get_color(fore);
	unsigned long _back = _get_color(back);
	
	if([object isMemberOfClass:[NSTextFieldCell class]])
	{
		NSTextFieldCell *text = object;
		[text setTextColor:[NSColor colorWithDeviceRed: DW_RED_VALUE(_fore)/255.0 green: DW_GREEN_VALUE(_fore)/255.0 blue: DW_BLUE_VALUE(_fore)/255.0 alpha: 1]];
	}
	else if([object isMemberOfClass:[DWBox class]])
	{
		DWBox *box = object;
		
		[box setColor:_back];
	}
	return 0;
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          border: Size of the window border in pixels.
 */
int API dw_window_set_border(HWND handle, int border)
{
	return 0;
}

/*
 * Sets the style of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          width: New width in pixels.
 *          height: New height in pixels.
 */
void API dw_window_set_style(HWND handle, ULONG style, ULONG mask)
{
	id object = handle;
	
	if([object isMemberOfClass:[NSWindow class]])
	{
		NSWindow *window = object;
		int currentstyle = (int)[window styleMask];
		int tmp;
		
		tmp = currentstyle | (int)mask;
		tmp ^= mask;
		tmp |= style;
		
		[window setStyleMask:tmp];
	}
    else if([object isMemberOfClass:[NSTextView class]])
    {
        NSTextView *tv = handle;
        [tv setAlignment:(style & mask)];
    }
}

/*
 * Sets the default focus item for a window/dialog.
 * Parameters:
 *         window: Toplevel window or dialog.
 *         defaultitem: Handle to the dialog item to be default.
 */
void API dw_window_default(HWND window, HWND defaultitem)
{
	NSLog(@"dw_window_default() unimplemented\n");
}

/*
 * Sets window to click the default dialog item when an ENTER is pressed.
 * Parameters:
 *         window: Window (widget) to look for the ENTER press.
 *         next: Window (widget) to move to next (or click)
 */
void API dw_window_click_default(HWND window, HWND next)
{
	NSLog(@"dw_window_click_default() unimplemented\n");
}

/*
 * Captures the mouse input to this window.
 * Parameters:
 *       handle: Handle to receive mouse input.
 */
void API dw_window_capture(HWND handle)
{
	NSLog(@"dw_window_capture() unimplemented\n");
}

/*
 * Releases previous mouse capture.
 */
void API dw_window_release(void)
{
	NSLog(@"dw_window_release() unimplemented\n");
}

/*
 * Tracks this window movement.
 * Parameters:
 *       handle: Handle to frame to be tracked.
 */
void API dw_window_track(HWND handle)
{
	NSLog(@"dw_window_track() unimplemented\n");
}

/*
 * Changes a window's parent to newparent.
 * Parameters:
 *           handle: The window handle to destroy.
 *           newparent: The window's new parent window.
 */
void API dw_window_reparent(HWND handle, HWND newparent)
{
	/* Is this even possible? */
	NSLog(@"dw_window_reparent() unimplemented\n");
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          fontname: Name and size of the font in the form "size.fontname"
 */
int API dw_window_set_font(HWND handle, char *fontname)
{
	char *fontcopy = strdup(fontname);
	char *name = strchr(fontcopy, '.');
	if(name)
	{
		int size = atoi(fontcopy); 
		*name = 0;
		name++;
		NSFont *font = [NSFont fontWithName:[ NSString stringWithUTF8String:name ] size:(float)size];
        if(font)
        {
            id object = handle;
            if([object window])
            {
                [object lockFocus];
                [font set];
                [object unlockFocus];
            }
            if([object isMemberOfClass:[NSControl class]])
            {
                [object setFont:font];
            }
        }
	}
	free(fontcopy);
	return 0;
}

/*
 * Destroys a window and all of it's children.
 * Parameters:
 *           handle: The window handle to destroy.
 */
int API dw_window_destroy(HWND handle)
{
	NSObject *object = handle;
	
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		[window close];
	}
	return 0;
}

/*
 * Gets the text used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 * Returns:
 *       text: The text associsated with a given window.
 */
char * API dw_window_get_text(HWND handle)
{
	NSObject *object = handle;
	
	if([ object isKindOfClass:[ NSControl class ] ])
	{
		NSControl *control = handle;
		NSString *nsstr = [ control stringValue];
		
		return strdup([ nsstr UTF8String ]);
	}
	else if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		NSString *nsstr = [ window title];
	
		return strdup([ nsstr UTF8String ]);
	}
	return NULL;
}

/*
 * Sets the text used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       text: The text associsated with a given window.
 */
void API dw_window_set_text(HWND handle, char *text)
{
	NSObject *object = handle;
	
	if([ object isKindOfClass:[ NSControl class ] ])
	{
		NSControl *control = handle;
		[control setStringValue:[ NSString stringWithUTF8String:text ]];
	}
	else if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		[window setTitle:[ NSString stringWithUTF8String:text ]];
	}
}

/*
 * Disables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void API dw_window_disable(HWND handle)
{
	NSObject *object = handle;
	if([ object isKindOfClass:[ NSControl class ] ])
	{
		NSControl *control = handle;
		[control setEnabled:NO];
	}
}

/*
 * Enables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void API dw_window_enable(HWND handle)
{
	NSObject *object = handle;
	if([ object isKindOfClass:[ NSControl class ] ])
	{
		NSControl *control = handle;
		[control setEnabled:YES];
	}
}

/*
 * Sets the bitmap used for a given static window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon,
 *           (pass 0 if you use the filename param)
 *       filename: a path to a file (Bitmap on OS/2 or
 *                 Windows and a pixmap on Unix, pass
 *                 NULL if you use the id param)
 */
void API dw_window_set_bitmap_from_data(HWND handle, unsigned long id, char *data, int len)
{
	NSObject *object = handle;
	if([ object isKindOfClass:[ NSImageView class ] ])
	{
		NSImageView *iv = handle;
		NSData *thisdata = [[NSData alloc] dataWithBytes:data length:len];
		NSImage *pixmap = [[NSImage alloc] initWithData:thisdata];
		
        if(pixmap)
        {
            [iv setImage:pixmap];
        }
	}
}

/*
 * Sets the bitmap used for a given static window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon,
 *           (pass 0 if you use the filename param)
 *       filename: a path to a file (Bitmap on OS/2 or
 *                 Windows and a pixmap on Unix, pass
 *                 NULL if you use the id param)
 */
void API dw_window_set_bitmap(HWND handle, unsigned long resid, char *filename)
{
	NSObject *object = handle;
	if([ object isKindOfClass:[ NSImageView class ] ])
	{
		NSImageView *iv = handle;
        NSImage *bitmap = NULL;
        
        if(filename)
        {
             bitmap = [[NSImage alloc] initWithContentsOfFile:[ NSString stringWithUTF8String:filename ]];
        }
        else if(resid > 0 && resid < 65536)
        {
            bitmap = dw_icon_load(0, resid);
        }
		
        if(bitmap)
        {
            [iv setImage:bitmap];
        }
	}
}

/*
 * Sets the icon used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon.
 */
void API dw_window_set_icon(HWND handle, HICN icon)
{
	/* This isn't needed, it is loaded from the bundle */
}

/*
 * Gets the child window handle with specified ID.
 * Parameters:
 *       handle: Handle to the parent window.
 *       id: Integer ID of the child.
 */
HWND API dw_window_from_id(HWND handle, int id)
{
	NSObject *object = handle;
	NSView *view = handle;
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		view = [window contentView];
	}
	return [view viewWithTag:id];
}

/*
 * Minimizes or Iconifies a top-level window.
 * Parameters:
 *           handle: The window handle to minimize.
 */
int API dw_window_minimize(HWND handle)
{
	NSWindow *window = handle;
	[window miniaturize:nil];
	return 0;
}

/* Causes entire window to be invalidated and redrawn.
 * Parameters:
 *           handle: Toplevel window handle to be redrawn.
 */
void API dw_window_redraw(HWND handle)
{
	NSWindow *window = handle;
	[window flushWindow];
}

/*
 * Makes the window topmost.
 * Parameters:
 *           handle: The window handle to make topmost.
 */
int API dw_window_raise(HWND handle)
{
	NSWindow *window = handle;
	[window orderFront:nil];
	return 0;
}

/*
 * Makes the window bottommost.
 * Parameters:
 *           handle: The window handle to make bottommost.
 */
int API dw_window_lower(HWND handle)
{
	NSWindow *window = handle;
	[window orderBack:nil];
	return 0;
}

/*
 * Sets the size of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          width: New width in pixels.
 *          height: New height in pixels.
 */
void API dw_window_set_size(HWND handle, ULONG width, ULONG height)
{
	NSObject *object = handle;
	NSSize size;
	size.width = width;
	size.height = height; 
	
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		[window setContentSize:size];
	}
}

/*
 * Sets the position of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          x: X location from the bottom left.
 *          y: Y location from the bottom left.
 */
void API dw_window_set_pos(HWND handle, LONG x, LONG y)
{
	NSObject *object = handle;
	NSPoint point;
	point.x = x;
	point.y = y; 
	
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		[window setFrameOrigin:point];
	}
}

/*
 * Sets the position and size of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          x: X location from the bottom left.
 *          y: Y location from the bottom left.
 *          width: Width of the widget.
 *          height: Height of the widget.
 */
void API dw_window_set_pos_size(HWND handle, LONG x, LONG y, ULONG width, ULONG height)
{
	dw_window_set_pos(handle, x, y);
	dw_window_set_size(handle, width, height);
}

/*
 * Gets the position and size of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          x: X location from the bottom left.
 *          y: Y location from the bottom left.
 *          width: Width of the widget.
 *          height: Height of the widget.
 */
void API dw_window_get_pos_size(HWND handle, LONG *x, LONG *y, ULONG *width, ULONG *height)
{
	NSObject *object = handle;
	
	if([ object isKindOfClass:[ NSWindow class ] ])
	{
		NSWindow *window = handle;
		NSRect rect = [window frame];
		if(x)
			*x = rect.origin.x;
		if(y)
			*y = rect.origin.y;
		if(width)
			*width = rect.size.width;
		if(height)
			*height = rect.size.height;
		return; 
	}
}

/*
 * Returns the width of the screen.
 */
int API dw_screen_width(void)
{
	NSRect screenRect = [[NSScreen mainScreen] frame]; 
	return screenRect.size.width;
}

/*
 * Returns the height of the screen.
 */
int API dw_screen_height(void)
{
	NSRect screenRect = [[NSScreen mainScreen] frame]; 
	return screenRect.size.height;
}

/* This should return the current color depth */
unsigned long API dw_color_depth_get(void)
{
	NSWindowDepth screenDepth = [[NSScreen mainScreen] depth];
	return NSBitsPerPixelFromDepth(screenDepth);
}

/*
 * Returns some information about the current operating environment.
 * Parameters:
 *       env: Pointer to a DWEnv struct.
 */
void dw_environment_query(DWEnv *env)
{
	struct utsname name;
	char tempbuf[100];
	int len, z;
	
	uname(&name);
	strcpy(env->osName, "MacOS");
	strcpy(tempbuf, name.release);
	
	env->MajorBuild = env->MinorBuild = 0;
	
	len = (int)strlen(tempbuf);
	
	strcpy(env->buildDate, __DATE__);
	strcpy(env->buildTime, __TIME__);
	env->DWMajorVersion = DW_MAJOR_VERSION;
	env->DWMinorVersion = DW_MINOR_VERSION;
	env->DWSubVersion = DW_SUB_VERSION;
	
	for(z=1;z<len;z++)
	{
		if(tempbuf[z] == '.')
		{
			int ver = atoi(tempbuf);
			tempbuf[z] = '\0';
			if(ver > 4)
			{
				env->MajorVersion = 10;
				env->MinorVersion = ver - 4;
				env->MajorBuild = atoi(&tempbuf[z+1]);
			}
			else
			{
				env->MajorVersion = ver;
				env->MinorVersion = atoi(&tempbuf[z+1]);
			}

			return;
		}
	}
	env->MajorVersion = atoi(tempbuf);
	env->MinorVersion = 0;
}

/*
 * Emits a beep.
 * Parameters:
 *       freq: Frequency.
 *       dur: Duration.
 */
void API dw_beep(int freq, int dur)
{
	NSBeep();
}

/* Call this after drawing to the screen to make sure
 * anything you have drawn is visible.
 */
void API dw_flush(void)
{
    /* This may need to be thread specific */
	if(_DWLastDrawable)
	{
		id object = _DWLastDrawable;
		NSWindow *window = [object window];
		[window flushWindow];
	}
}

/* Functions for managing the user data lists that are associated with
 * a given window handle.  Used in dw_window_set_data() and
 * dw_window_get_data().
 */
UserData *_find_userdata(UserData **root, char *varname)
{
	UserData *tmp = *root;
	
	while(tmp)
	{
		if(strcasecmp(tmp->varname, varname) == 0)
			return tmp;
		tmp = tmp->next;
	}
	return NULL;
}

int _new_userdata(UserData **root, char *varname, void *data)
{
	UserData *new = _find_userdata(root, varname);
	
	if(new)
	{
		new->data = data;
		return TRUE;
	}
	else
	{
		new = malloc(sizeof(UserData));
		if(new)
		{
			new->varname = strdup(varname);
			new->data = data;
			
			new->next = NULL;
			
			if (!*root)
				*root = new;
			else
			{
				UserData *prev = NULL, *tmp = *root;
				while(tmp)
				{
					prev = tmp;
					tmp = tmp->next;
				}
				if(prev)
					prev->next = new;
				else
					*root = new;
			}
			return TRUE;
		}
	}
	return FALSE;
}

int _remove_userdata(UserData **root, char *varname, int all)
{
	UserData *prev = NULL, *tmp = *root;
	
	while(tmp)
	{
		if(all || strcasecmp(tmp->varname, varname) == 0)
		{
			if(!prev)
			{
				*root = tmp->next;
				free(tmp->varname);
				free(tmp);
				if(!all)
					return 0;
				tmp = *root;
			}
			else
			{
				/* If all is true we should
				 * never get here.
				 */
				prev->next = tmp->next;
				free(tmp->varname);
				free(tmp);
				return 0;
			}
		}
		else
		{
			prev = tmp;
			tmp = tmp->next;
		}
	}
	return 0;
}

/*
 * Add a named user data item to a window handle.
 * Parameters:
 *       window: Window handle of signal to be called back.
 *       dataname: A string pointer identifying which signal to be hooked.
 *       data: User data to be passed to the handler function.
 */
void dw_window_set_data(HWND window, char *dataname, void *data)
{
	id object = window;
	WindowData *blah = (WindowData *)[object userdata];
	
	if(!blah)
	{
		if(!dataname)
			return;
		
		blah = calloc(1, sizeof(WindowData));
		[object setUserdata:blah];
	}
	
	if(data)
		_new_userdata(&(blah->root), dataname, data);
	else
	{
		if(dataname)
			_remove_userdata(&(blah->root), dataname, FALSE);
		else
			_remove_userdata(&(blah->root), NULL, TRUE);
	}
}

/*
 * Gets a named user data item to a window handle.
 * Parameters:
 *       window: Window handle of signal to be called back.
 *       dataname: A string pointer identifying which signal to be hooked.
 *       data: User data to be passed to the handler function.
 */
void *dw_window_get_data(HWND window, char *dataname)
{
	id object = window;
	WindowData *blah = (WindowData *)[object userdata];
	
	if(blah && blah->root && dataname)
	{
		UserData *ud = _find_userdata(&(blah->root), dataname);
		if(ud)
			return ud->data;
	}
	return NULL;
}

#define DW_TIMER_MAX 64
NSTimer *DWTimers[DW_TIMER_MAX];

/*
 * Add a callback to a timer event.
 * Parameters:
 *       interval: Milliseconds to delay between calls.
 *       sigfunc: The pointer to the function to be used as the callback.
 *       data: User data to be passed to the handler function.
 * Returns:
 *       Timer ID for use with dw_timer_disconnect(), 0 on error.
 */
int API dw_timer_connect(int interval, void *sigfunc, void *data)
{
	int z;
	
	for(z=0;z<DW_TIMER_MAX;z++)
	{
		if(!DWTimers[z])
		{
			break;
		}
	}
	
	if(sigfunc && !DWTimers[z])
	{
		NSTimeInterval seconds = (double)interval / 1000.0;
		NSTimer *thistimer = DWTimers[z] = [NSTimer timerWithTimeInterval:seconds target:DWHandler selector:@selector(runTimer:) userInfo:nil repeats:YES];
		_new_signal(0, thistimer, z+1, sigfunc, data);
		return z+1;
	}
	return 0;
}

/*
 * Removes timer callback.
 * Parameters:
 *       id: Timer ID returned by dw_timer_connect().
 */
void API dw_timer_disconnect(int timerid)
{
	SignalHandler *prev = NULL, *tmp = Root;
	NSTimer *thistimer;
	
	/* 0 is an invalid timer ID */
	if(timerid < 1 || !DWTimers[timerid-1])
		return;
	
	thistimer = DWTimers[timerid-1];
	DWTimers[timerid-1] = nil;
	
	[thistimer release];
	
	while(tmp)
	{
		if(tmp->id == (timerid-1) && tmp->window == thistimer)
		{
			if(prev)
			{
				prev->next = tmp->next;
				free(tmp);
				tmp = prev->next;
			}
			else
			{
				Root = tmp->next;
				free(tmp);
				tmp = Root;
			}
		}
		else
		{
			prev = tmp;
			tmp = tmp->next;
		}
	}
}

/*
 * Add a callback to a window event.
 * Parameters:
 *       window: Window handle of signal to be called back.
 *       signame: A string pointer identifying which signal to be hooked.
 *       sigfunc: The pointer to the function to be used as the callback.
 *       data: User data to be passed to the handler function.
 */
void API dw_signal_connect(HWND window, char *signame, void *sigfunc, void *data)
{
	ULONG message = 0, msgid = 0;
	
	if(window && signame && sigfunc)
	{
		if((message = _findsigmessage(signame)) != 0)
		{
			_new_signal(message, window, (int)msgid, sigfunc, data);
		}
	}
}

/*
 * Removes callbacks for a given window with given name.
 * Parameters:
 *       window: Window handle of callback to be removed.
 */
void API dw_signal_disconnect_by_name(HWND window, char *signame)
{
	SignalHandler *prev = NULL, *tmp = Root;
	ULONG message;
	
	if(!window || !signame || (message = _findsigmessage(signame)) == 0)
		return;
	
	while(tmp)
	{
		if(tmp->window == window && tmp->message == message)
		{
			if(prev)
			{
				prev->next = tmp->next;
				free(tmp);
				tmp = prev->next;
			}
			else
			{
				Root = tmp->next;
				free(tmp);
				tmp = Root;
			}
		}
		else
		{
			prev = tmp;
			tmp = tmp->next;
		}
	}
}

/*
 * Removes all callbacks for a given window.
 * Parameters:
 *       window: Window handle of callback to be removed.
 */
void API dw_signal_disconnect_by_window(HWND window)
{
	SignalHandler *prev = NULL, *tmp = Root;
	
	while(tmp)
	{
		if(tmp->window == window)
		{
			if(prev)
			{
				prev->next = tmp->next;
				free(tmp);
				tmp = prev->next;
			}
			else
			{
				Root = tmp->next;
				free(tmp);
				tmp = Root;
			}
		}
		else
		{
			prev = tmp;
			tmp = tmp->next;
		}
	}
}

/*
 * Removes all callbacks for a given window with specified data.
 * Parameters:
 *       window: Window handle of callback to be removed.
 *       data: Pointer to the data to be compared against.
 */
void API dw_signal_disconnect_by_data(HWND window, void *data)
{
	SignalHandler *prev = NULL, *tmp = Root;
	
	while(tmp)
	{
		if(tmp->window == window && tmp->data == data)
		{
			if(prev)
			{
				prev->next = tmp->next;
				free(tmp);
				tmp = prev->next;
			}
			else
			{
				Root = tmp->next;
				free(tmp);
				tmp = Root;
			}
		}
		else
		{
			prev = tmp;
			tmp = tmp->next;
		}
	}
}

void _my_strlwr(char *buf)
{
   int z, len = strlen(buf);

   for(z=0;z<len;z++)
   {
      if(buf[z] >= 'A' && buf[z] <= 'Z')
         buf[z] -= 'A' - 'a';
   }
}

/* Open a shared library and return a handle.
 * Parameters:
 *         name: Base name of the shared library.
 *         handle: Pointer to a module handle,
 *                 will be filled in with the handle.
 */
int dw_module_load(char *name, HMOD *handle)
{
   int len;
   char *newname;
   char errorbuf[1024];


   if(!handle)
      return -1;

   if((len = strlen(name)) == 0)
      return   -1;

   /* Lenth + "lib" + ".dylib" + NULL */
   newname = malloc(len + 10);

   if(!newname)
      return -1;

   sprintf(newname, "lib%s.dylib", name);
   _my_strlwr(newname);

   *handle = dlopen(newname, RTLD_NOW);
   if(*handle == NULL)
   {
      strncpy(errorbuf, dlerror(), 1024);
      printf("%s\n", errorbuf);
      sprintf(newname, "lib%s.dylib", name);
      *handle = dlopen(newname, RTLD_NOW);
   }

   free(newname);

   return (NULL == *handle) ? -1 : 0;
}

/* Queries the address of a symbol within open handle.
 * Parameters:
 *         handle: Module handle returned by dw_module_load()
 *         name: Name of the symbol you want the address of.
 *         func: A pointer to a function pointer, to obtain
 *               the address.
 */
int dw_module_symbol(HMOD handle, char *name, void**func)
{
   if(!func || !name)
      return   -1;

   if(strlen(name) == 0)
      return   -1;

   *func = (void*)dlsym(handle, name);
   return   (NULL == *func);
}

/* Frees the shared library previously opened.
 * Parameters:
 *         handle: Module handle returned by dw_module_load()
 */
int dw_module_close(HMOD handle)
{
   if(handle)
      return dlclose(handle);
   return 0;
}

/*
 * Returns the handle to an unnamed mutex semaphore.
 */
HMTX dw_mutex_new(void)
{
   HMTX mutex = malloc(sizeof(pthread_mutex_t));

   pthread_mutex_init(mutex, NULL);
   return mutex;
}

/*
 * Closes a semaphore created by dw_mutex_new().
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_close(HMTX mutex)
{
   if(mutex)
   {
      pthread_mutex_destroy(mutex);
      free(mutex);
   }
}

/*
 * Tries to gain access to the semaphore, if it can't it blocks.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_lock(HMTX mutex)
{
   pthread_mutex_lock(mutex);
}

/*
 * Reliquishes the access to the semaphore.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_unlock(HMTX mutex)
{
   pthread_mutex_unlock(mutex);
}

/*
 * Returns the handle to an unnamed event semaphore.
 */
HEV dw_event_new(void)
{
   HEV eve = (HEV)malloc(sizeof(struct _dw_unix_event));

   if(!eve)
      return NULL;

   /* We need to be careful here, mutexes on Linux are
    * FAST by default but are error checking on other
    * systems such as FreeBSD and OS/2, perhaps others.
    */
   pthread_mutex_init (&(eve->mutex), NULL);
   pthread_mutex_lock (&(eve->mutex));
   pthread_cond_init (&(eve->event), NULL);

   pthread_mutex_unlock (&(eve->mutex));
   eve->alive = 1;
   eve->posted = 0;

   return eve;
}

/*
 * Resets a semaphore created by dw_event_new().
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_reset (HEV eve)
{
   if(!eve)
      return FALSE;

   pthread_mutex_lock (&(eve->mutex));
   pthread_cond_broadcast (&(eve->event));
   pthread_cond_init (&(eve->event), NULL);
   eve->posted = 0;
   pthread_mutex_unlock (&(eve->mutex));
   return 0;
}

/*
 * Posts a semaphore created by dw_event_new(). Causing all threads
 * waiting on this event in dw_event_wait to continue.
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_post (HEV eve)
{
   if(!eve)
      return FALSE;

   pthread_mutex_lock (&(eve->mutex));
   pthread_cond_broadcast (&(eve->event));
   eve->posted = 1;
   pthread_mutex_unlock (&(eve->mutex));
   return 0;
}

/*
 * Waits on a semaphore created by dw_event_new(), until the
 * event gets posted or until the timeout expires.
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_wait(HEV eve, unsigned long timeout)
{
   int rc;
   struct timeval now;
   struct timespec timeo;

   if(!eve)
      return FALSE;

   if(eve->posted)
      return 0;

   pthread_mutex_lock (&(eve->mutex));
   gettimeofday(&now, 0);
   timeo.tv_sec = now.tv_sec + (timeout / 1000);
   timeo.tv_nsec = now.tv_usec * 1000;
   rc = pthread_cond_timedwait (&(eve->event), &(eve->mutex), &timeo);
   pthread_mutex_unlock (&(eve->mutex));
   if(!rc)
      return 1;
   if(rc == ETIMEDOUT)
      return -1;
   return 0;
}

/*
 * Closes a semaphore created by dw_event_new().
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_close(HEV *eve)
{
   if(!eve || !(*eve))
      return FALSE;

   pthread_mutex_lock (&((*eve)->mutex));
   pthread_cond_destroy (&((*eve)->event));
   pthread_mutex_unlock (&((*eve)->mutex));
   pthread_mutex_destroy (&((*eve)->mutex));
   free(*eve);
   *eve = NULL;

   return TRUE;
}

struct _seminfo {
   int fd;
   int waiting;
};

static void _handle_sem(int *tmpsock)
{
   fd_set rd;
   struct _seminfo *array = (struct _seminfo *)malloc(sizeof(struct _seminfo));
   int listenfd = tmpsock[0];
   int bytesread, connectcount = 1, maxfd, z, posted = 0;
   char command;
   sigset_t mask;

   sigfillset(&mask); /* Mask all allowed signals */
   pthread_sigmask(SIG_BLOCK, &mask, NULL);

   /* problems */
   if(tmpsock[1] == -1)
   {
      free(array);
      return;
   }

   array[0].fd = tmpsock[1];
   array[0].waiting = 0;

   /* Free the memory allocated in dw_named_event_new. */
   free(tmpsock);

   while(1)
   {
      FD_ZERO(&rd);
      FD_SET(listenfd, &rd);

      maxfd = listenfd;

      /* Added any connections to the named event semaphore */
      for(z=0;z<connectcount;z++)
      {
         if(array[z].fd > maxfd)
            maxfd = array[z].fd;

         FD_SET(array[z].fd, &rd);
      }

      if(select(maxfd+1, &rd, NULL, NULL, NULL) == -1)
         return;

      if(FD_ISSET(listenfd, &rd))
      {
         struct _seminfo *newarray;
            int newfd = accept(listenfd, 0, 0);

         if(newfd > -1)
         {
            /* Add new connections to the set */
            newarray = (struct _seminfo *)malloc(sizeof(struct _seminfo)*(connectcount+1));
            memcpy(newarray, array, sizeof(struct _seminfo)*(connectcount));

            newarray[connectcount].fd = newfd;
            newarray[connectcount].waiting = 0;

            connectcount++;

            /* Replace old array with new one */
            free(array);
            array = newarray;
         }
      }

      /* Handle any events posted to the semaphore */
      for(z=0;z<connectcount;z++)
      {
         if(FD_ISSET(array[z].fd, &rd))
         {
            if((bytesread = read(array[z].fd, &command, 1)) < 1)
            {
               struct _seminfo *newarray;

               /* Remove this connection from the set */
               newarray = (struct _seminfo *)malloc(sizeof(struct _seminfo)*(connectcount-1));
               if(!z)
                  memcpy(newarray, &array[1], sizeof(struct _seminfo)*(connectcount-1));
               else
               {
                  memcpy(newarray, array, sizeof(struct _seminfo)*z);
                  if(z!=(connectcount-1))
                     memcpy(&newarray[z], &array[z+1], sizeof(struct _seminfo)*(z-connectcount-1));
               }
               connectcount--;

               /* Replace old array with new one */
               free(array);
               array = newarray;
            }
            else if(bytesread == 1)
            {
               switch(command)
               {
               case 0:
                  {
                  /* Reset */
                  posted = 0;
                  }
                  break;
               case 1:
                  /* Post */
                  {
                     int s;
                     char tmp = (char)0;

                     posted = 1;

                     for(s=0;s<connectcount;s++)
                     {
                        /* The semaphore has been posted so
                         * we tell all the waiting threads to
                         * continue.
                         */
                        if(array[s].waiting)
                           write(array[s].fd, &tmp, 1);
                     }
                  }
                  break;
               case 2:
                  /* Wait */
                  {
                     char tmp = (char)0;

                     array[z].waiting = 1;

                     /* If we are posted exit immeditately */
                     if(posted)
                        write(array[z].fd, &tmp, 1);
                  }
                  break;
               case 3:
                  {
                     /* Done Waiting */
                     array[z].waiting = 0;
                  }
                  break;
               }
            }
         }
      }

   }

}

/* Using domain sockets on unix for IPC */
/* Create a named event semaphore which can be
 * opened from other processes.
 * Parameters:
 *         eve: Pointer to an event handle to receive handle.
 *         name: Name given to semaphore which can be opened
 *               by other processes.
 */
HEV dw_named_event_new(char *name)
{
	struct sockaddr_un un;
	int ev, *tmpsock = (int *)malloc(sizeof(int)*2);
	HEV eve;
	DWTID dwthread;

	if(!tmpsock)
		return NULL;

	eve = (HEV)malloc(sizeof(struct _dw_unix_event));

	if(!eve)
	{
		free(tmpsock);
     	return NULL;
	}
	
	tmpsock[0] = socket(AF_UNIX, SOCK_STREAM, 0);
	ev = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&un, 0, sizeof(un));
	un.sun_family=AF_UNIX;
	mkdir("/tmp/.dw", S_IWGRP|S_IWOTH);
	strcpy(un.sun_path, "/tmp/.dw/");
	strcat(un.sun_path, name);

	/* just to be safe, this should be changed
	 * to support multiple instances.
	 */
	remove(un.sun_path);

	bind(tmpsock[0], (struct sockaddr *)&un, sizeof(un));
	listen(tmpsock[0], 0);
	connect(ev, (struct sockaddr *)&un, sizeof(un));
	tmpsock[1] = accept(tmpsock[0], 0, 0);

	if(tmpsock[0] < 0 || tmpsock[1] < 0 || ev < 0)
	{
		if(tmpsock[0] > -1)
			close(tmpsock[0]);
		if(tmpsock[1] > -1)
			close(tmpsock[1]);
		if(ev > -1)
			close(ev);
		free(tmpsock);
		free(eve);
		return NULL;
	}

	/* Create a thread to handle this event semaphore */
	pthread_create(&dwthread, NULL, (void *)_handle_sem, (void *)tmpsock);
	eve->alive = ev;
	return eve;
}

/* Open an already existing named event semaphore.
 * Parameters:
 *         eve: Pointer to an event handle to receive handle.
 *         name: Name given to semaphore which can be opened
 *               by other processes.
 */
HEV dw_named_event_get(char *name)
{
	struct sockaddr_un un;
	HEV eve;
	int ev = socket(AF_UNIX, SOCK_STREAM, 0);
	if(ev < 0)
		return NULL;

	eve = (HEV)malloc(sizeof(struct _dw_unix_event));

	if(!eve)
	{
		close(ev);
     	return NULL;
	}

	un.sun_family=AF_UNIX;
	mkdir("/tmp/.dw", S_IWGRP|S_IWOTH);
	strcpy(un.sun_path, "/tmp/.dw/");
	strcat(un.sun_path, name);
	connect(ev, (struct sockaddr *)&un, sizeof(un));
	eve->alive = ev;
	return eve;
}

/* Resets the event semaphore so threads who call wait
 * on this semaphore will block.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int dw_named_event_reset(HEV eve)
{
   /* signal reset */
   char tmp = (char)0;

   if(!eve || eve->alive < 0)
      return 0;

   if(write(eve->alive, &tmp, 1) == 1)
      return 0;
   return 1;
}

/* Sets the posted state of an event semaphore, any threads
 * waiting on the semaphore will no longer block.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int dw_named_event_post(HEV eve)
{

   /* signal post */
   char tmp = (char)1;

   if(!eve || eve->alive < 0)
      return 0;

   if(write(eve->alive, &tmp, 1) == 1)
      return 0;
   return 1;
}

/* Waits on the specified semaphore until it becomes
 * posted, or returns immediately if it already is posted.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 *         timeout: Number of milliseconds before timing out
 *                  or -1 if indefinite.
 */
int dw_named_event_wait(HEV eve, unsigned long timeout)
{
   fd_set rd;
   struct timeval tv, *useme;
   int retval = 0;
   char tmp;

   if(!eve || eve->alive < 0)
      return DW_ERROR_NON_INIT;

   /* Set the timout or infinite */
   if(timeout == -1)
      useme = NULL;
   else
   {
      tv.tv_sec = timeout / 1000;
      tv.tv_usec = timeout % 1000;

      useme = &tv;
   }

   FD_ZERO(&rd);
   FD_SET(eve->alive, &rd);

   /* Signal wait */
   tmp = (char)2;
   write(eve->alive, &tmp, 1);

   retval = select(eve->alive+1, &rd, NULL, NULL, useme);

   /* Signal done waiting. */
   tmp = (char)3;
   write(eve->alive, &tmp, 1);

   if(retval == 0)
      return DW_ERROR_TIMEOUT;
   else if(retval == -1)
      return DW_ERROR_INTERRUPT;

   /* Clear the entry from the pipe so
    * we don't loop endlessly. :)
    */
   read(eve->alive, &tmp, 1);
   return 0;
}

/* Release this semaphore, if there are no more open
 * handles on this semaphore the semaphore will be destroyed.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int dw_named_event_close(HEV eve)
{
   /* Finally close the domain socket,
    * cleanup will continue in _handle_sem.
    */
	if(eve)
	{
		close(eve->alive);
		free(eve);
	}
	return 0;
}

/*
 * Setup thread independent color sets.
 */
void _dwthreadstart(void *data)
{
   void (*threadfunc)(void *) = NULL;
   void **tmp = (void **)data;
    /* If we aren't using garbage collection we need autorelease pools */
#if !defined(GARBAGE_COLLECT)
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#endif
	
   threadfunc = (void (*)(void *))tmp[0];

   threadfunc(tmp[1]);
    /* Release the pool when we are done so we don't leak */
#if !defined(GARBAGE_COLLECT)
	[pool release];
#endif
   free(tmp);
}

/*
 * Allocates a shared memory region with a name.
 * Parameters:
 *         handle: A pointer to receive a SHM identifier.
 *         dest: A pointer to a pointer to receive the memory address.
 *         size: Size in bytes of the shared memory region to allocate.
 *         name: A string pointer to a unique memory name.
 */
HSHM dw_named_memory_new(void **dest, int size, char *name)
{
   char namebuf[1024];
   struct _dw_unix_shm *handle = malloc(sizeof(struct _dw_unix_shm));

   mkdir("/tmp/.dw", S_IWGRP|S_IWOTH);
   sprintf(namebuf, "/tmp/.dw/%s", name);

   if((handle->fd = open(namebuf, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0)
   {
      free(handle);
      return NULL;
   }

   ftruncate(handle->fd, size);

   /* attach the shared memory segment to our process's address space. */
   *dest = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);

   if(*dest == MAP_FAILED)
   {
      close(handle->fd);
      *dest = NULL;
      free(handle);
      return NULL;
   }

   handle->size = size;
   handle->sid = getsid(0);
   handle->path = strdup(namebuf);

   return handle;
}

/*
 * Aquires shared memory region with a name.
 * Parameters:
 *         dest: A pointer to a pointer to receive the memory address.
 *         size: Size in bytes of the shared memory region to requested.
 *         name: A string pointer to a unique memory name.
 */
HSHM dw_named_memory_get(void **dest, int size, char *name)
{
   char namebuf[1024];
   struct _dw_unix_shm *handle = malloc(sizeof(struct _dw_unix_shm));

   mkdir("/tmp/.dw", S_IWGRP|S_IWOTH);
   sprintf(namebuf, "/tmp/.dw/%s", name);

   if((handle->fd = open(namebuf, O_RDWR)) < 0)
   {
      free(handle);
      return NULL;
   }

   /* attach the shared memory segment to our process's address space. */
   *dest = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);

   if(*dest == MAP_FAILED)
   {
      close(handle->fd);
      *dest = NULL;
      free(handle);
      return NULL;
   }

   handle->size = size;
   handle->sid = -1;
   handle->path = NULL;

   return handle;
}

/*
 * Frees a shared memory region previously allocated.
 * Parameters:
 *         handle: Handle obtained from DB_named_memory_allocate.
 *         ptr: The memory address aquired with DB_named_memory_allocate.
 */
int dw_named_memory_free(HSHM handle, void *ptr)
{
   struct _dw_unix_shm *h = handle;
   int rc = munmap(ptr, h->size);

   close(h->fd);
   if(h->path)
   {
      /* Only remove the actual file if we are the
       * creator of the file.
       */
      if(h->sid != -1 && h->sid == getsid(0))
         remove(h->path);
      free(h->path);
   }
   return rc;
}
/*
 * Creates a new thread with a starting point of func.
 * Parameters:
 *       func: Function which will be run in the new thread.
 *       data: Parameter(s) passed to the function.
 *       stack: Stack size of new thread (OS/2 and Windows only).
 */
DWTID dw_thread_new(void *func, void *data, int stack)
{
	DWTID thread;
	void **tmp = malloc(sizeof(void *) * 2);
	int rc;

   tmp[0] = func;
   tmp[1] = data;

   rc = pthread_create(&thread, NULL, (void *)_dwthreadstart, (void *)tmp);
   if(rc == 0)
      return thread;
   return (DWTID)-1;
}

/*
 * Ends execution of current thread immediately.
 */
void dw_thread_end(void)
{
   pthread_exit(NULL);
}

/*
 * Returns the current thread's ID.
 */
DWTID dw_thread_id(void)
{
   return (DWTID)pthread_self();
}

/*
 * Execute and external program in a seperate session.
 * Parameters:
 *       program: Program name with optional path.
 *       type: Either DW_EXEC_CON or DW_EXEC_GUI.
 *       params: An array of pointers to string arguements.
 * Returns:
 *       -1 on error.
 */
int dw_exec(char *program, int type, char **params)
{
	int ret = -1;
	
	if((ret = fork()) == 0)
	{
		int i;
		
		for (i = 3; i < 256; i++)
			close(i);
		setsid();
		if(type == DW_EXEC_GUI)
		{
			execvp(program, params);
		}
		else if(type == DW_EXEC_CON)
		{
			char **tmpargs;
			
			if(!params)
			{
				tmpargs = malloc(sizeof(char *));
				tmpargs[0] = NULL;
			}
			else
			{
				int z = 0;
				
				while(params[z])
				{
					z++;
				}
				tmpargs = malloc(sizeof(char *)*(z+3));
				z=0;
				tmpargs[0] = "xterm";
				tmpargs[1] = "-e";
				while(params[z])
				{
					tmpargs[z+2] = params[z];
					z++;
				}
				tmpargs[z+2] = NULL;
			}
			execvp("xterm", tmpargs);
			free(tmpargs);
		}
		/* If we got here exec failed */
		_exit(-1);
	}
	return ret;
}

/*
 * Loads a web browser pointed at the given URL.
 * Parameters:
 *       url: Uniform resource locator.
 */
int dw_browse(char *url)
{
	/* Is there a way to find the webbrowser in Unix? */
	char *execargs[3], *browser = "netscape", *tmp;
	
	tmp = getenv( "DW_BROWSER" );
	if(tmp) browser = tmp;
	execargs[0] = browser;
	execargs[1] = url;
	execargs[2] = NULL;
	
	return dw_exec(browser, DW_EXEC_GUI, execargs);
}
