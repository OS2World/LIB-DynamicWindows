#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dw.h"

/* Select a fixed width font for our platform */
#ifdef __OS2__
#define FIXEDFONT "5.System VIO"
#elif defined(__WIN32__)
#define FIXEDFONT "10.Terminal"
#elif GTK_MAJOR_VERSION > 1
#define FIXEDFONT "monospace 10"
#else
#define FIXEDFONT "fixed"
#endif

#define SCROLLBARWIDTH 14

unsigned long flStyle = DW_FCF_SYSMENU | DW_FCF_TITLEBAR |
	DW_FCF_SHELLPOSITION | DW_FCF_TASKLIST | DW_FCF_DLGBORDER;

HWND mainwindow,
     entryfield,
     okbutton,
     cancelbutton,
     lbbox,
     notebookbox,
     notebookbox1,
     notebookbox2,
     notebookbox3,
     notebook,
     vscrollbar,
     hscrollbar,
     status,
     stext,
     tree,
     pagebox,
     treebox,
     textbox1, textbox2, textboxA,
     gap_box,
     buttonbox;

HPIXMAP text1pm,text2pm;

int font_width = 8;
int font_height=12;
int font_gap = 2;
int rows=100,width1=6,cols=80;
char *current_file = NULL;
int timerid;
int num_lines=0;
int max_linewidth=0;
int current_row=0,current_col=0;

FILE *fp=NULL;
char **lp;

/* This gets called when a part of the graph needs to be repainted. */
int DWSIGNAL text_expose(HWND hwnd, DWExpose *exp, void *data)
{
	HPIXMAP hpm;
	int width,height;

	if ( hwnd == textbox1 )
		hpm = text1pm;
	else if ( hwnd == textbox2 )
		hpm = text2pm;
	else
		return TRUE;

	width = DW_PIXMAP_WIDTH(hpm);
	height = DW_PIXMAP_HEIGHT(hpm);

	dw_pixmap_bitblt(hwnd, NULL, 0, 0, width, height, 0, hpm, 0, 0 );
	dw_flush();
	return TRUE;
}

void read_file( void )
{
	int i,len;
	fp = fopen( current_file, "r" );
	lp = (char **)calloc( 1000,sizeof(char *));
	/* should test for out of memory */
	max_linewidth=0;
	for ( i = 0; i < 1000; i++ )
	{
		lp[i] = (char *)calloc(1, 1025);
		if ( fgets( lp[i], 1024, fp ) == NULL )
			break;
		len = strlen( lp[i] );
		if ( len > max_linewidth )
			max_linewidth = len;
		if ( lp[i][len - 1] == '\n' )
			lp[i][len - 1] = '\0';
	}
	num_lines = i;
	fclose( fp );
	dw_scrollbar_set_range(hscrollbar, max_linewidth, cols);
	dw_scrollbar_set_pos(hscrollbar, 0);
	dw_scrollbar_set_range(vscrollbar, num_lines, rows);
	dw_scrollbar_set_pos(vscrollbar, 0);
}

void draw_file( int row, int col )
{
	char buf[10];
	int i,y;
	char *pLine;

	if ( current_file )
	{
		dw_color_foreground_set(DW_CLR_WHITE);
		dw_draw_rect(0, text1pm, TRUE, 0, 0, DW_PIXMAP_WIDTH(text1pm), DW_PIXMAP_HEIGHT(text1pm));
		dw_draw_rect(0, text2pm, TRUE, 0, 0, DW_PIXMAP_WIDTH(text2pm), DW_PIXMAP_HEIGHT(text2pm));
		for ( i = 0;(i < rows) && (i+row < num_lines); i++)
		{
			y = i*(font_height+font_gap);
			dw_color_foreground_set( i );
			sprintf( buf, "%6.6d", i+row );
			dw_draw_text( 0, text1pm, 0, y, buf);
			pLine = lp[i+row];
			dw_draw_text( 0, text2pm, 0, y, pLine+col );
		}
		text_expose( textbox1, NULL, NULL);
		text_expose( textbox2, NULL, NULL);
	}
}

int DWSIGNAL beep_callback(HWND window, void *data)
{
	dw_timer_disconnect( timerid );
	return TRUE;
}

int DWSIGNAL keypress_callback(HWND window, char ch, int vk, int state, void *data)
{
	FILE *fp = fopen("log", "a+");

	if(fp)
	{
		fprintf(fp,"got keypress %c 0x%x %d %d\n", ch, ch, vk, state);
		fclose(fp);
	}
	return 0;
}

int DWSIGNAL exit_callback(HWND window, void *data)
{
	dw_window_destroy((HWND)data);
	exit(0);
	return -1;
}

int DWSIGNAL test_callback(HWND window, void *data)
{
	dw_window_destroy((HWND)data);
	if ( current_file )
		dw_free( current_file );
	exit(0);
	return -1;
}

int DWSIGNAL browse_callback(HWND window, void *data)
{
	char *tmp;
	tmp = dw_file_browse("test string", NULL, "c", DW_FILE_OPEN );
	if ( tmp )
	{
		if ( current_file )
		{
			dw_free( current_file );
		}
		current_file = tmp;
		dw_window_set_text( entryfield, current_file );
		read_file();
		draw_file(0,0);
	}
	return 0;
}

/* Callback to handle user selection of the scrollbar position */
void DWSIGNAL scrollbar_valuechanged(HWND hwnd, int value, void *data)
{
	if(data)
	{
		HWND stext = (HWND)data;
		char tmpbuf[100];
		if ( hwnd == vscrollbar )
		{
			current_row = value;
		}
		else
		{
			current_col = value;
		}
		sprintf(tmpbuf, "Row:%d Col:%d Lines:%d Cols:%d", current_row,current_col,num_lines,max_linewidth);
		dw_window_set_text(stext, tmpbuf);
		draw_file( current_row, current_col);
	}
}

/* Handle size change of the main render window */
int DWSIGNAL configure_event(HWND hwnd, int width, int height, void *data)
{
	HPIXMAP old1 = text1pm, old2 = text2pm;
	int depth = dw_color_depth();

	rows = height / (font_height+font_gap);
	cols = width / font_width;

	/* Create new pixmaps with the current sizes */
	text1pm = dw_pixmap_new(textbox1, (font_width*(width1+1)), height+1, depth);
	text2pm = dw_pixmap_new(textbox2, width, height, depth);

	/* Destroy the old pixmaps */
	dw_pixmap_destroy(old1);
	dw_pixmap_destroy(old2);

	/* Update scrollbar ranges with new values */
	dw_scrollbar_set_range(hscrollbar, max_linewidth, cols);
	dw_scrollbar_set_range(vscrollbar, num_lines, rows);

	/* Redraw the window */
	draw_file( current_row, current_col);
	return TRUE;
}

void archive_add(void)
{
	HWND browsebutton, browsebox;

	lbbox = dw_box_new(BOXVERT, 10);

	dw_box_pack_start(notebookbox1, lbbox, 150, 70, TRUE, TRUE, 0);

	/* Archive Name */
	stext = dw_text_new("File to browse", 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(lbbox, stext, 130, 15, TRUE, TRUE, 2);

	browsebox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(lbbox, browsebox, 0, 0, TRUE, TRUE, 0);

	entryfield = dw_entryfield_new("", 100L);

	dw_box_pack_start(browsebox, entryfield, 100, 15, TRUE, TRUE, 4);

	browsebutton = dw_button_new("Browse", 1001L);

	dw_box_pack_start(browsebox, browsebutton, 30, 15, TRUE, TRUE, 0);

	dw_window_set_color(browsebox, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
	dw_window_set_color(stext, DW_CLR_BLACK, DW_CLR_PALEGRAY);

	/* Buttons */
	buttonbox = dw_box_new(BOXHORZ, 10);

	dw_box_pack_start(lbbox, buttonbox, 0, 0, TRUE, TRUE, 0);

	okbutton = dw_button_new("Turn Off Annoying Beep!", 1001L);

	dw_box_pack_start(buttonbox, okbutton, 130, 30, TRUE, TRUE, 2);

	cancelbutton = dw_button_new("Exit", 1002L);

	dw_box_pack_start(buttonbox, cancelbutton, 130, 30, TRUE, TRUE, 2);

	/* Set some nice fonts and colors */
	dw_window_set_color(lbbox, DW_CLR_DARKCYAN, DW_CLR_PALEGRAY);
	dw_window_set_color(buttonbox, DW_CLR_DARKCYAN, DW_CLR_PALEGRAY);
	dw_window_set_color(okbutton, DW_CLR_PALEGRAY, DW_CLR_DARKCYAN);

	dw_signal_connect(browsebutton, "clicked", DW_SIGNAL_FUNC(browse_callback), (void *)notebookbox1);
	dw_signal_connect(okbutton, "clicked", DW_SIGNAL_FUNC(beep_callback), (void *)notebookbox1);
	dw_signal_connect(cancelbutton, "clicked", DW_SIGNAL_FUNC(exit_callback), (void *)mainwindow);
}


void text_add(void)
{
	int depth = dw_color_depth();
	HWND vscrollbox;

	/* create a box to pack into the notebook page */
	pagebox = dw_box_new(BOXHORZ, 2);
	dw_box_pack_start( notebookbox2, pagebox, 0, 0, TRUE, TRUE, 0);
	/* now a status area under this box */
	status = dw_status_text_new("", 0);
	dw_box_pack_start( notebookbox2, status, 100, 20, TRUE, FALSE, 1);

	/* create render box for number pixmap */
	textbox1 = dw_render_new( 100 );
	dw_window_set_font(textbox1, FIXEDFONT);
	dw_font_text_extents(textbox1, NULL, "O", &font_width, &font_height);
	vscrollbox = dw_box_new(BOXVERT, 0);
	dw_box_pack_start(vscrollbox, textbox1, (font_width*(width1+1)), font_height*rows, FALSE, TRUE, 0);
	dw_box_pack_start(vscrollbox, 0, (font_width*(width1+1)), SCROLLBARWIDTH, FALSE, FALSE, 0);
	dw_box_pack_start(pagebox, vscrollbox, 0, 0, FALSE, TRUE, 0);

	/* create render box for gap pixmap */
	/* create box for filecontents and horz scrollbar */
	textboxA = dw_box_new( BOXVERT,0 );
	dw_box_pack_start( pagebox, textboxA, 0, 0, TRUE, TRUE, 0);

	/* create render box for filecontents pixmap */
	textbox2 = dw_render_new( 101 );
	dw_box_pack_start( textboxA, textbox2, 10, 10, TRUE, TRUE, 0);
	dw_window_set_font(textbox2, FIXEDFONT);
	/* create horizonal scrollbar */
	hscrollbar = dw_scrollbar_new(FALSE, 100, 50);
	dw_box_pack_start( textboxA, hscrollbar, 100, SCROLLBARWIDTH, TRUE, FALSE, 0);

	/* create vertical scrollbar */
	vscrollbox = dw_box_new(BOXVERT, 0);
	vscrollbar = dw_scrollbar_new(TRUE, 100, 50);
	dw_box_pack_start(vscrollbox, vscrollbar, SCROLLBARWIDTH, 100, FALSE, TRUE, 0);
	/* Pack an area of empty space 14x14 pixels */
	dw_box_pack_start(vscrollbox, 0, SCROLLBARWIDTH, SCROLLBARWIDTH, FALSE, FALSE, 0);
	dw_box_pack_start(pagebox, vscrollbox, 0, 0, FALSE, TRUE, 0);

	text1pm = dw_pixmap_new( textbox1, (font_width*(width1+1)), font_height*rows, depth );
	text2pm = dw_pixmap_new( textbox2, font_width*cols, font_height*rows, depth );

	dw_messagebox("DWTest", "Width: %d Height: %d\n", font_width, font_height);
	dw_draw_rect(0, text1pm, TRUE, 0, 0, font_width*width1, font_height*rows);
	dw_draw_rect(0, text2pm, TRUE, 0, 0, font_width*cols, font_height*rows);
	dw_signal_connect(textbox1, "expose_event", DW_SIGNAL_FUNC(text_expose), NULL);
	dw_signal_connect(textbox2, "expose_event", DW_SIGNAL_FUNC(text_expose), NULL);
	dw_signal_connect(textbox2, "configure_event", DW_SIGNAL_FUNC(configure_event), text2pm);
	dw_signal_connect(hscrollbar, "value_changed", DW_SIGNAL_FUNC(scrollbar_valuechanged), (void *)status);
	dw_signal_connect(vscrollbar, "value_changed", DW_SIGNAL_FUNC(scrollbar_valuechanged), (void *)status);
	dw_signal_connect(textbox1, "key_press_event", DW_SIGNAL_FUNC(keypress_callback), text1pm);
	dw_signal_connect(textbox2, "key_press_event", DW_SIGNAL_FUNC(keypress_callback), text2pm);
}

void tree_add(void)
{
	HWND t1,t2,t3;

	/* create a box to pack into the notebook page */
	treebox = dw_box_new(BOXHORZ, 2);
	dw_box_pack_start( notebookbox3, treebox, 500, 200, TRUE, TRUE, 0);

	/* now a tree area under this box */
	tree = dw_tree_new(0);
	dw_box_pack_start( notebookbox3, tree, 500, 200, TRUE, FALSE, 1);

	t1 = dw_tree_insert(tree, "tree item 1", 0, 0, 0 );
	t2 = dw_tree_insert(tree, "tree item 2", 0, 0, 0 );
	t3 = dw_tree_insert(tree, "tree item 3", 0, t2, 0 );

/*
	dw_signal_connect(textbox1, "expose_event", DW_SIGNAL_FUNC(text_expose), NULL);
	dw_signal_connect(textbox2, "expose_event", DW_SIGNAL_FUNC(text_expose), NULL);
	dw_signal_connect(textbox2, "configure_event", DW_SIGNAL_FUNC(configure_event), text2pm);
	dw_signal_connect(hscrollbar, "value_changed", DW_SIGNAL_FUNC(scrollbar_valuechanged), (void *)status);
	dw_signal_connect(vscrollbar, "value_changed", DW_SIGNAL_FUNC(scrollbar_valuechanged), (void *)status);
	dw_signal_connect(textbox1, "key_press_event", DW_SIGNAL_FUNC(keypress_callback), text1pm);
	dw_signal_connect(textbox2, "key_press_event", DW_SIGNAL_FUNC(keypress_callback), text2pm);
*/
}

/* Beep every second */
int DWSIGNAL timer_callback(void *data)
{
	dw_beep(200, 200);

	/* Return TRUE so we get called again */
	return TRUE;
}

/*
 * Let's demonstrate the functionality of this library. :)
 */
int main(int argc, char *argv[])
{
	ULONG notebookpage1;
	ULONG notebookpage2;
	ULONG notebookpage3;

	dw_init(TRUE, argc, argv);

	mainwindow = dw_window_new( HWND_DESKTOP, "dwindows test", flStyle | DW_FCF_SIZEBORDER | DW_FCF_MINMAX );

	notebookbox = dw_box_new( BOXVERT, 5 );
	dw_box_pack_start( mainwindow, notebookbox, 0, 0, TRUE, TRUE, 0);

	notebook = dw_notebook_new( 1, TRUE );
	dw_box_pack_start( notebookbox, notebook, 100, 100, TRUE, TRUE, 0);

	notebookbox1 = dw_box_new( BOXVERT, 5 );
	notebookpage1 = dw_notebook_page_new( notebook, 0, TRUE );
	dw_notebook_pack( notebook, notebookpage1, notebookbox1 );
	dw_notebook_page_set_text( notebook, notebookpage1, "first page");
	archive_add();

	notebookbox2 = dw_box_new( BOXVERT, 5 );
	notebookpage2 = dw_notebook_page_new( notebook, 1, FALSE );
	dw_notebook_pack( notebook, notebookpage2, notebookbox2 );
	dw_notebook_page_set_text( notebook, notebookpage2, "second page");
	text_add();

	notebookbox3 = dw_box_new( BOXVERT, 5 );
	notebookpage3 = dw_notebook_page_new( notebook, 1, FALSE );
	dw_notebook_pack( notebook, notebookpage3, notebookbox3 );
	dw_notebook_page_set_text( notebook, notebookpage3, "third page");
	tree_add();

	dw_signal_connect(mainwindow, "delete_event", DW_SIGNAL_FUNC(exit_callback), (void *)mainwindow);
	timerid = dw_timer_connect(1000, DW_SIGNAL_FUNC(timer_callback), 0);
	dw_window_set_usize(mainwindow, 640, 480);
	dw_window_show(mainwindow);

	dw_main();

	return 0;
}
