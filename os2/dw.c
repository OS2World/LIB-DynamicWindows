/*
 * Dynamic Windows:
 *          A GTK like implementation of the PM GUI
 *
 * (C) 2000-2011 Brian Smith <brian@dbsoft.org>
 * (C) 2003-2008 Mark Hessling <m.hessling@qut.edu.au>
 * (C) 2000 Achim Hasenmueller <achimha@innotek.de>
 * (C) 2000 Peter Nielsen <peter@pmview.com>
 * (C) 1998 Sergey I. Yevtushenko (some code borrowed from cell toolkit)
 *
 */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_GPI

#include <os2.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <ctype.h>
#include <process.h>
#include <time.h>
#include <io.h>
#ifndef __EMX__
#include <direct.h>
#endif
#include <sys/time.h>
#include "dw.h"

#define QWP_USER 0

/* The toolkit headers don't seem to have this */
BOOL APIENTRY WinStretchPointer(HPS hps, LONG x, LONG y, LONG cx, LONG cy, HPOINTER hptr, ULONG fs);

MRESULT EXPENTRY _run_event(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2);
MRESULT EXPENTRY _wndproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2);
void _do_resize(Box *thisbox, int x, int y);
void _handle_splitbar_resize(HWND hwnd, float percent, int type, int x, int y);
int _load_bitmap_file(char *file, HWND handle, HBITMAP *hbm, HDC *hdc, HPS *hps, unsigned long *width, unsigned long *height);
void _dw_box_pack_start(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad, char *functionname);
void _dw_box_pack_end(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad, char *functionname);
void _free_menu_data(HWND menu);

char ClassName[] = "dynamicwindows";
char SplitbarClassName[] = "dwsplitbar";
char *DefaultFont = "9.WarpSans";

HAB dwhab = 0;
HMQ dwhmq = 0;
DWTID _dwtid = 0;
LONG _foreground = 0xAAAAAA, _background = DW_CLR_DEFAULT;

HWND hwndApp = NULLHANDLE, hwndBubble = NULLHANDLE, hwndBubbleLast = NULLHANDLE, hwndEmph = NULLHANDLE;
PRECORDCORE pCoreEmph = NULL;
ULONG aulBuffer[4], GlobalID = 10000;
HWND lasthcnr = 0, lastitem = 0, popup = 0, desktop;
HMOD wpconfig = 0;

unsigned long _colors[] = {
   CLR_BLACK,
   CLR_DARKRED,
   CLR_DARKGREEN,
   CLR_BROWN,
   CLR_DARKBLUE,
   CLR_DARKPINK,
   CLR_DARKCYAN,
   CLR_PALEGRAY,
   CLR_DARKGRAY,
   CLR_RED,
   CLR_GREEN,
   CLR_YELLOW,
   CLR_BLUE,
   CLR_PINK,
   CLR_CYAN,
   CLR_WHITE
};

#define IS_WARP4() (aulBuffer[0] == 20 && aulBuffer[1] >= 40)

#ifndef min
#define min(a, b) (((a < b) ? a : b))
#endif

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

typedef struct
{
   ULONG message;
   char name[30];

} SignalList;

/* List of signals and their equivilent OS/2 message */
#define SIGNALMAX 16

SignalList SignalTranslate[SIGNALMAX] = {
   { WM_SIZE,         DW_SIGNAL_CONFIGURE },
   { WM_CHAR,         DW_SIGNAL_KEY_PRESS },
   { WM_BUTTON1DOWN,  DW_SIGNAL_BUTTON_PRESS },
   { WM_BUTTON1UP,    DW_SIGNAL_BUTTON_RELEASE },
   { WM_MOUSEMOVE,    DW_SIGNAL_MOTION_NOTIFY },
   { WM_CLOSE,        DW_SIGNAL_DELETE },
   { WM_PAINT,        DW_SIGNAL_EXPOSE },
   { WM_COMMAND,      DW_SIGNAL_CLICKED },
   { CN_ENTER,        DW_SIGNAL_ITEM_ENTER },
   { CN_CONTEXTMENU,  DW_SIGNAL_ITEM_CONTEXT },
   { LN_SELECT,       DW_SIGNAL_LIST_SELECT },
   { CN_EMPHASIS,     DW_SIGNAL_ITEM_SELECT },
   { WM_SETFOCUS,     DW_SIGNAL_SET_FOCUS },
   { SLN_SLIDERTRACK, DW_SIGNAL_VALUE_CHANGED },
   { BKN_PAGESELECTED,DW_SIGNAL_SWITCH_PAGE },
   { CN_EXPANDTREE,   DW_SIGNAL_TREE_EXPAND }
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
      if(stricmp(signame, SignalTranslate[z].name) == 0)
         return SignalTranslate[z].message;
   }
   return 0L;
}

typedef struct _CNRITEM
{
   MINIRECORDCORE rc;
   HPOINTER       hptrIcon;
   PVOID          user;
   HTREEITEM      parent;

} CNRITEM, *PCNRITEM;


int _null_key(HWND window, int key, void *data)
{
   window = window; /* keep compiler happy */
   key = key; /* keep compiler happy */
   data = data; /* keep compiler happy */
   return TRUE;
}

/* Find the desktop window handle */
HWND _toplevel_window(HWND handle)
{
   HWND box, lastbox = WinQueryWindow(handle, QW_PARENT);

   /* Find the toplevel window */
   while((box = WinQueryWindow(lastbox, QW_PARENT)) != desktop && box > 0)
   {
      lastbox = box;
   }
   if(box > 0)
      return lastbox;
   return handle;
}


/* Returns height of specified window. */
int _get_height(HWND handle)
{
   unsigned long height;
   dw_window_get_pos_size(handle, NULL, NULL, NULL, &height);
   return (int)height;
}

/* Find the height of the frame a desktop style window is sitting on */
int _get_frame_height(HWND handle)
{
   while(handle)
   {
      HWND client;
      if((client = WinWindowFromID(handle, FID_CLIENT)) != NULLHANDLE)
      {
            return _get_height(WinQueryWindow(handle, QW_PARENT));
      }
        handle = WinQueryWindow(handle, QW_PARENT);
   }
   return dw_screen_height();
}

/* A "safe" WinSendMsg() that tries multiple times in case the
 * queue is blocked for one reason or another.
 */
MRESULT _dw_send_msg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2, int failure)
{
   MRESULT res;
   int z = 0;

   while((int)(res = WinSendMsg(hwnd, msg, mp1, mp2)) == failure)
   {
      z++;
      if(z > 5000000)
         return (MRESULT)failure;
      dw_main_sleep(1);
   }
   return res;
}

/* Used in the slider and percent classes internally */
unsigned int _dw_percent_get_range(HWND handle)
{
   return SHORT2FROMMP(WinSendMsg(handle, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION,SMA_RANGEVALUE), 0));
}

/* Return the entryfield child of a window */
HWND _find_entryfield(HWND handle)
{
   HENUM henum;
   HWND child, entry = 0;

   henum = WinBeginEnumWindows(handle);
   while((child = WinGetNextWindow(henum)) != NULLHANDLE)
   {
      char tmpbuf[100];

      WinQueryClassName(child, 99, tmpbuf);

      if(strncmp(tmpbuf, "#6", 3)==0)  /* Entryfield */
      {
         entry = child;
         break;
      }
   }
   WinEndEnumWindows(henum);
   return entry;
}

/* This function changes the owner of buttons in to the
 * dynamicwindows handle to fix a problem in notebooks.
 */
void _fix_button_owner(HWND handle, HWND dw)
{
   HENUM henum;
   HWND child;

   henum = WinBeginEnumWindows(handle);
   while((child = WinGetNextWindow(henum)) != NULLHANDLE)
   {
      char tmpbuf[100];

      WinQueryClassName(child, 99, tmpbuf);

      if(strncmp(tmpbuf, "#3", 3)==0 && dw)  /* Button */
         WinSetOwner(child, dw);
      else if(strncmp(tmpbuf, "dynamicwindows", 14) == 0)
         dw = child;

      _fix_button_owner(child, dw);
   }
   WinEndEnumWindows(henum);
   return;
}

/* Free bitmap data associated with a window */
void _free_bitmap(HWND handle)
{
   HBITMAP hbm = (HBITMAP)dw_window_get_data(handle, "_dw_bitmap");
   HPS hps = (HPS)dw_window_get_data(handle, "_dw_hps");
   HDC hdc = (HDC)dw_window_get_data(handle, "_dw_hdc");
   HPIXMAP pixmap = (HPIXMAP)dw_window_get_data(handle, "_dw_hpixmap");
   HPIXMAP disable = (HPIXMAP)dw_window_get_data(handle, "_dw_hpixmap_disabled");
   HPOINTER icon = (HPOINTER)dw_window_get_data(handle, "_dw_button_icon");

   if(icon)
      WinDestroyPointer(icon);

   if(pixmap)
      dw_pixmap_destroy(pixmap);

   if(disable)
      dw_pixmap_destroy(disable);

   if(hps)
   {
      GpiSetBitmap(hps, NULLHANDLE);
      GpiAssociate(hps, NULLHANDLE);
      GpiDestroyPS(hps);
   }

   if(hdc)
      DevCloseDC(hdc);

   if(hbm)
      GpiDeleteBitmap(hbm);
}

/* This function removes any handlers on windows and frees
 * the user memory allocated to it.
 */
void _free_window_memory(HWND handle)
{
   HENUM henum;
   HWND child;
   void *ptr = (void *)WinQueryWindowPtr(handle, QWP_USER);

   dw_signal_disconnect_by_window(handle);

   if((child = WinWindowFromID(handle, FID_MENU)) != NULLHANDLE)
      _free_menu_data(child);

   if((child = WinWindowFromID(handle, FID_CLIENT)) != NULLHANDLE)
   {
      Box *box = (Box *)WinQueryWindowPtr(child, QWP_USER);

      if(box)
      {
         if(box->count && box->items)
            free(box->items);

         WinSetWindowPtr(child, QWP_USER, 0);
         free(box);
      }
   }

   if(ptr)
   {
      WindowData *wd = (WindowData *)ptr;
      char tmpbuf[100];

      WinQueryClassName(handle, 99, tmpbuf);

      if(strncmp(tmpbuf, "ColorSelectClass", 17)!=0)
      {
         /* If this window has an associate bitmap destroy it. */
         _free_bitmap(handle);

         if(strncmp(tmpbuf, "#1", 3)==0 && !WinWindowFromID(handle, FID_CLIENT))
         {
            Box *box = (Box *)ptr;

            if(box->count && box->items)
               free(box->items);
         }
         else if(strncmp(tmpbuf, SplitbarClassName, strlen(SplitbarClassName)+1)==0)
         {
            void *data = dw_window_get_data(handle, "_dw_percent");

            if(data)
               free(data);
         }
         else if(strncmp(tmpbuf, "#37", 4)==0)
         {
            dw_container_clear(handle, FALSE);
            if(wd && dw_window_get_data(handle, "_dw_container"))
            {
               void *oldflags = wd->data;

               wd->data = NULL;
               free(oldflags);
            }
         }

         if(wd->oldproc)
            WinSubclassWindow(handle, wd->oldproc);

         dw_window_set_data(handle, NULL, NULL);
         WinSetWindowPtr(handle, QWP_USER, 0);
         free(ptr);
      }
   }

   henum = WinBeginEnumWindows(handle);
   while((child = WinGetNextWindow(henum)) != NULLHANDLE)
      _free_window_memory(child);

   WinEndEnumWindows(henum);
   return;
}

void _free_menu_data(HWND menu)
{
   int i, count = (int)WinSendMsg(menu, MM_QUERYITEMCOUNT, 0, 0);

   dw_signal_disconnect_by_name(menu, DW_SIGNAL_CLICKED);
   _free_window_memory(menu);

   for(i=0;i<count;i++)
   {
      SHORT menuid = (SHORT)WinSendMsg(menu, MM_ITEMIDFROMPOSITION, MPFROMSHORT(i), 0);
      MENUITEM mi;

      if(WinSendMsg(menu, MM_QUERYITEM, MPFROMSHORT(menuid), MPFROMP(&mi))
         && mi.hwndSubMenu)
         _free_menu_data(mi.hwndSubMenu);
   }
}

/* This function returns 1 if the window (widget) handle
 * passed to it is a valid window that can gain input focus.
 */
int _validate_focus(HWND handle)
{
   char tmpbuf[100];

   if(!handle)
      return 0;

   WinQueryClassName(handle, 99, tmpbuf);

   if(!WinIsWindowEnabled(handle) ||
      (strncmp(tmpbuf, "ColorSelectClass", 17) && dw_window_get_data(handle, "_dw_disabled")))
      return 0;

   /* These are the window classes which can
    * obtain input focus.
    */
   if(strncmp(tmpbuf, "#2", 3)==0 ||  /* Combobox */
      strncmp(tmpbuf, "#3", 3)==0 ||  /* Button */
      strncmp(tmpbuf, "#6", 3)==0 ||  /* Entryfield */
      strncmp(tmpbuf, "#7", 3)==0 ||  /* List box */
      strncmp(tmpbuf, "#10", 4)==0 || /* MLE */
      strncmp(tmpbuf, "#32", 4)==0 || /* Spinbutton */
      strncmp(tmpbuf, "#37", 4)==0 || /* Container */
      strncmp(tmpbuf, "#38", 4)== 0)  /* Slider */
      return 1;
   return 0;
}

int _focus_check_box(Box *box, HWND handle, int start, HWND defaultitem)
{
   int z;
   static HWND lasthwnd, firsthwnd;
    static int finish_searching;

   /* Start is 2 when we have cycled completely and
    * need to set the focus to the last widget we found
    * that was valid.
    */
   if(start == 2)
   {
      if(lasthwnd)
         WinSetFocus(HWND_DESKTOP, lasthwnd);
      return 0;
   }

   /* Start is 1 when we are entering the function
    * for the first time, it is zero when entering
    * the function recursively.
    */
   if(start == 1)
   {
      lasthwnd = handle;
      finish_searching = 0;
      firsthwnd = 0;
   }

   for(z=box->count-1;z>-1;z--)
   {
      if(box->items[z].type == TYPEBOX)
      {
         Box *thisbox = WinQueryWindowPtr(box->items[z].hwnd, QWP_USER);

         if(thisbox && _focus_check_box(thisbox, handle, start == 3 ? 3 : 0, defaultitem))
            return 1;
      }
      else
      {
         if(box->items[z].hwnd == handle)
         {
            if(lasthwnd == handle && firsthwnd)
               WinSetFocus(HWND_DESKTOP, firsthwnd);
            else if(lasthwnd == handle && !firsthwnd)
               finish_searching = 1;
            else
               WinSetFocus(HWND_DESKTOP, lasthwnd);

            /* If we aren't looking for the last handle,
             * return immediately.
             */
            if(!finish_searching)
               return 1;
         }
         if(_validate_focus(box->items[z].hwnd))
         {
            /* Start is 3 when we are looking for the
             * first valid item in the layout.
             */
            if(start == 3)
            {
               if(!defaultitem || (defaultitem && defaultitem == box->items[z].hwnd))
               {
                  WinSetFocus(HWND_DESKTOP, box->items[z].hwnd);
                  return 1;
               }
            }

            if(!firsthwnd)
               firsthwnd = box->items[z].hwnd;

            lasthwnd = box->items[z].hwnd;
         }
         else
         {
            char tmpbuf[100] = "";

            WinQueryClassName(box->items[z].hwnd, 99, tmpbuf);
            if(strncmp(tmpbuf, SplitbarClassName, strlen(SplitbarClassName)+1)==0)
            {
               /* Then try the bottom or right box */
               HWND mybox = (HWND)dw_window_get_data(box->items[z].hwnd, "_dw_bottomright");

               if(mybox)
               {
                  Box *splitbox = (Box *)WinQueryWindowPtr(mybox, QWP_USER);

                  if(splitbox && _focus_check_box(splitbox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }

               /* Try the top or left box */
               mybox = (HWND)dw_window_get_data(box->items[z].hwnd, "_dw_topleft");

               if(mybox)
               {
                  Box *splitbox = (Box *)WinQueryWindowPtr(mybox, QWP_USER);

                  if(splitbox && _focus_check_box(splitbox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }
            }
            else if(strncmp(tmpbuf, "#40", 4)==0) /* Notebook */
            {
               Box *notebox;
               HWND page = (HWND)WinSendMsg(box->items[z].hwnd, BKM_QUERYPAGEWINDOWHWND,
                                     (MPARAM)dw_notebook_page_get(box->items[z].hwnd), 0);

               if(page)
               {
                  notebox = (Box *)WinQueryWindowPtr(page, QWP_USER);

                  if(notebox && _focus_check_box(notebox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }
            }
         }
      }
   }
   return 0;
}

int _focus_check_box_back(Box *box, HWND handle, int start, HWND defaultitem)
{
   int z;
   static HWND lasthwnd, firsthwnd;
    static int finish_searching;

   /* Start is 2 when we have cycled completely and
    * need to set the focus to the last widget we found
    * that was valid.
    */
   if(start == 2)
   {
      if(lasthwnd)
         WinSetFocus(HWND_DESKTOP, lasthwnd);
      return 0;
   }

   /* Start is 1 when we are entering the function
    * for the first time, it is zero when entering
    * the function recursively.
    */
   if(start == 1)
   {
      lasthwnd = handle;
      finish_searching = 0;
      firsthwnd = 0;
   }

   for(z=0;z<box->count;z++)
   {
      if(box->items[z].type == TYPEBOX)
      {
         Box *thisbox = WinQueryWindowPtr(box->items[z].hwnd, QWP_USER);

         if(thisbox && _focus_check_box_back(thisbox, handle, start == 3 ? 3 : 0, defaultitem))
            return 1;
      }
      else
      {
         if(box->items[z].hwnd == handle)
         {
            if(lasthwnd == handle && firsthwnd)
               WinSetFocus(HWND_DESKTOP, firsthwnd);
            else if(lasthwnd == handle && !firsthwnd)
               finish_searching = 1;
            else
               WinSetFocus(HWND_DESKTOP, lasthwnd);

            /* If we aren't looking for the last handle,
             * return immediately.
             */
            if(!finish_searching)
               return 1;
         }
         if(_validate_focus(box->items[z].hwnd))
         {
            /* Start is 3 when we are looking for the
             * first valid item in the layout.
             */
            if(start == 3)
            {
               if(!defaultitem || (defaultitem && defaultitem == box->items[z].hwnd))
               {
                  WinSetFocus(HWND_DESKTOP, box->items[z].hwnd);
                  return 1;
               }
            }

            if(!firsthwnd)
               firsthwnd = box->items[z].hwnd;

            lasthwnd = box->items[z].hwnd;
         }
         else
         {
            char tmpbuf[100] = "";

            WinQueryClassName(box->items[z].hwnd, 99, tmpbuf);
            if(strncmp(tmpbuf, SplitbarClassName, strlen(SplitbarClassName)+1)==0)
            {
               /* Try the top or left box */
               HWND mybox = (HWND)dw_window_get_data(box->items[z].hwnd, "_dw_topleft");

               if(mybox)
               {
                  Box *splitbox = (Box *)WinQueryWindowPtr(mybox, QWP_USER);

                  if(splitbox && _focus_check_box_back(splitbox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }

               /* Then try the bottom or right box */
               mybox = (HWND)dw_window_get_data(box->items[z].hwnd, "_dw_bottomright");

               if(mybox)
               {
                  Box *splitbox = (Box *)WinQueryWindowPtr(mybox, QWP_USER);

                  if(splitbox && _focus_check_box_back(splitbox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }
            }
            else if(strncmp(tmpbuf, "#40", 4)==0) /* Notebook */
            {
               Box *notebox;
               HWND page = (HWND)WinSendMsg(box->items[z].hwnd, BKM_QUERYPAGEWINDOWHWND,
                                     (MPARAM)dw_notebook_page_get(box->items[z].hwnd), 0);

               if(page)
               {
                  notebox = (Box *)WinQueryWindowPtr(page, QWP_USER);

                  if(notebox && _focus_check_box_back(notebox, handle, start == 3 ? 3 : 0, defaultitem))
                     return 1;
               }
            }
         }
      }
   }
   return 0;
}

/* This function finds the first widget in the
 * layout and moves the current focus to it.
 */
int _initial_focus(HWND handle)
{
   Box *thisbox = NULL;
   HWND box;

   box = WinWindowFromID(handle, FID_CLIENT);
   if(box)
      thisbox = WinQueryWindowPtr(box, QWP_USER);
   else
      return 1;

   if(thisbox)
      _focus_check_box(thisbox, handle, 3, thisbox->defaultitem);
   return 0;
}

/* This function finds the current widget in the
 * layout and moves the current focus to the next item.
 */
void _shift_focus(HWND handle)
{
   Box *thisbox;
   HWND box, lastbox = _toplevel_window(handle);

   box = WinWindowFromID(lastbox, FID_CLIENT);
   if(box)
      thisbox = WinQueryWindowPtr(box, QWP_USER);
   else
      thisbox = WinQueryWindowPtr(lastbox, QWP_USER);

   if(thisbox)
   {
      if(_focus_check_box(thisbox, handle, 1, 0)  == 0)
         _focus_check_box(thisbox, handle, 2, 0);
   }
}

/* This function finds the current widget in the
 * layout and moves the current focus to the next item.
 */
void _shift_focus_back(HWND handle)
{
   Box *thisbox;
   HWND box, lastbox = _toplevel_window(handle);

   box = WinWindowFromID(lastbox, FID_CLIENT);
   if(box)
      thisbox = WinQueryWindowPtr(box, QWP_USER);
   else
      thisbox = WinQueryWindowPtr(lastbox, QWP_USER);

   if(thisbox)
   {
      if(_focus_check_box_back(thisbox, handle, 1, 0)  == 0)
         _focus_check_box_back(thisbox, handle, 2, 0);
   }
}

/* This function will recursively search a box and add up the total height of it */
void _count_size(HWND box, int type, int *xsize, int *xorigsize)
{
   int size = 0, origsize = 0, z;
   Box *tmp = WinQueryWindowPtr(box, QWP_USER);

   if(!tmp)
   {
      *xsize = *xorigsize = 0;
      return;
   }

   if(type == tmp->type)
   {
      /* If the box is going in the direction we want, then we
       * return the entire sum of the items.
       */
      for(z=0;z<tmp->count;z++)
      {
         if(tmp->items[z].type == TYPEBOX)
         {
            int s, os;

            _count_size(tmp->items[z].hwnd, type, &s, &os);
            size += s;
            origsize += os;
         }
         else
         {
            size += (type == DW_HORZ ? tmp->items[z].width : tmp->items[z].height);
            origsize += (type == DW_HORZ ? tmp->items[z].origwidth : tmp->items[z].origheight);
         }
      }
   }
   else
   {
      /* If the box is not going in the direction we want, then we only
       * want to return the maximum value.
       */
      int tmpsize = 0, tmporigsize = 0;

      for(z=0;z<tmp->count;z++)
      {
         if(tmp->items[z].type == TYPEBOX)
            _count_size(tmp->items[z].hwnd, type, &tmpsize, &tmporigsize);
         else
         {
            tmpsize = (type == DW_HORZ ? tmp->items[z].width : tmp->items[z].height);
            tmporigsize = (type == DW_HORZ ? tmp->items[z].origwidth : tmp->items[z].origheight);
         }

         if(tmpsize > size)
            size = tmpsize;
      }
   }

   *xsize = size;
   *xorigsize = origsize;
}


/* Function: TrackRectangle
 * Abstract: Tracks given rectangle.
 *
 * If rclBounds is NULL, then track rectangle on entire desktop.
 * rclTrack is in window coorditates and will be mapped to
 * desktop.
 */

BOOL _TrackRectangle(HWND hwndBase, RECTL* rclTrack, RECTL* rclBounds)
{
   TRACKINFO track;
   APIRET rc;

   track.cxBorder = 1;
   track.cyBorder = 1;
   track.cxGrid   = 1;
   track.cyGrid   = 1;
   track.cxKeyboard = 8;
   track.cyKeyboard = 8;

   if(!rclTrack)
      return FALSE;

   if(rclBounds)
   {
      track.rclBoundary = *rclBounds;
   }
   else
   {
      track.rclBoundary.yTop    =
         track.rclBoundary.xRight  = 3000;
      track.rclBoundary.yBottom =
         track.rclBoundary.xLeft   = -3000;
   }

   track.rclTrack = *rclTrack;

   WinMapWindowPoints(hwndBase,
                  HWND_DESKTOP,
                  (PPOINTL)&track.rclTrack,
                  2);

   track.ptlMinTrackSize.x = track.rclTrack.xRight
      - track.rclTrack.xLeft;
   track.ptlMinTrackSize.y = track.rclTrack.yTop
      - track.rclTrack.yBottom;
   track.ptlMaxTrackSize.x = track.rclTrack.xRight
      - track.rclTrack.xLeft;
   track.ptlMaxTrackSize.y = track.rclTrack.yTop
      - track.rclTrack.yBottom;

   track.fs = TF_MOVE | TF_ALLINBOUNDARY;

   rc = WinTrackRect(HWND_DESKTOP, 0, &track);

   if(rc)
      *rclTrack = track.rclTrack;

   return rc;
}

void _check_resize_notebook(HWND hwnd)
{
   char tmpbuf[100];

   WinQueryClassName(hwnd, 99, tmpbuf);

   /* If we have a notebook we resize the page again. */
   if(strncmp(tmpbuf, "#40", 4)==0)
   {
      long x, y;
      unsigned long width, height;
      ULONG page = (ULONG)WinSendMsg(hwnd, BKM_QUERYPAGEID, 0, MPFROM2SHORT(BKA_FIRST, BKA_MAJOR));

      while(page)
      {
         HWND pagehwnd = (HWND)WinSendMsg(hwnd, BKM_QUERYPAGEWINDOWHWND, MPFROMLONG(page), 0);
         RECTL rc;

         Box *pagebox = (Box *)WinQueryWindowPtr(pagehwnd, QWP_USER);
         if(pagebox)
         {
            dw_window_get_pos_size(hwnd, &x, &y, &width, &height);

            rc.xLeft = x;
            rc.yBottom = y;
            rc.xRight = x + width;
            rc.yTop = y + height;

            WinSendMsg(hwnd, BKM_CALCPAGERECT, (MPARAM)&rc, (MPARAM)TRUE);

            _do_resize(pagebox, rc.xRight - rc.xLeft, rc.yTop - rc.yBottom);
         }
         page = (ULONG)WinSendMsg(hwnd, BKM_QUERYPAGEID, (MPARAM)page, MPFROM2SHORT(BKA_NEXT, BKA_MAJOR));
      }

   }
}

/* Return the OS/2 color from the DW color */
unsigned long _internal_color(unsigned long color)
{
   if(color < 16)
      return _colors[color];
   return color;
}

unsigned long _os2_color(unsigned long color)
{
   return DW_RED_VALUE(color) << 16 | DW_GREEN_VALUE(color) << 8 | DW_BLUE_VALUE(color);
}

BOOL _MySetWindowPos(HWND hwnd, HWND parent, HWND behind, LONG x, LONG y, LONG cx, LONG cy, ULONG fl)
{
   int height = _get_height(parent);

   return WinSetWindowPos(hwnd, behind, x, height - y - cy, cx, cy, fl);
}

/* This function calculates how much space the widgets and boxes require
 * and does expansion as necessary.
 */
int _resize_box(Box *thisbox, int *depth, int x, int y, int *usedx, int *usedy,
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

   if(thisbox->grouphwnd)
   {
      char *text = dw_window_get_text(thisbox->grouphwnd);

      thisbox->grouppady = 0;

      if(text)
      {
         dw_font_text_extents_get(thisbox->grouphwnd, 0, text, NULL, &thisbox->grouppady);
         dw_free(text);
      }

      if(thisbox->grouppady)
         thisbox->grouppady += 3;
      else
         thisbox->grouppady = 6;

      thisbox->grouppadx = 6;

      (*usedx) += thisbox->grouppadx;
      (*usedpadx) += thisbox->grouppadx;
      (*usedy) += thisbox->grouppady;
      (*usedpady) += thisbox->grouppady;
   }

   for(z=0;z<thisbox->count;z++)
   {
      if(thisbox->items[z].type == TYPEBOX)
      {
         int initialx, initialy;
         Box *tmp = WinQueryWindowPtr(thisbox->items[z].hwnd, QWP_USER);

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
                  int tmppad = (thisbox->items[z].pad*2)+(tmp->pad*2)+tmp->grouppady;

                  if((thisbox->items[z].width - tmppad)!=0)
                     tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-tmppad))/((float)(thisbox->items[z].width-tmppad));
               }
               else
               {
                  if((thisbox->items[z].width-tmp->upx)!=0)
                     tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-tmp->upx))/((float)(thisbox->items[z].width-tmp->upx));
               }
               if(thisbox->type == DW_HORZ)
               {
                  int tmppad = (thisbox->items[z].pad*2)+(tmp->pad*2)+tmp->grouppadx;

                  if((thisbox->items[z].height-tmppad)!=0)
                     tmp->yratio = ((float)((thisbox->items[z].height * thisbox->yratio)-tmppad))/((float)(thisbox->items[z].height-tmppad));
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
            int tmppad = (thisbox->items[z].pad*2)+(thisbox->parentpad*2)+thisbox->grouppadx;

            if((thisbox->minwidth-tmppad) == 0)
               thisbox->items[z].xratio = 1.0;
            else
               thisbox->items[z].xratio = ((float)((thisbox->width * thisbox->parentxratio)-tmppad))/((float)(thisbox->minwidth-tmppad));
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
            int tmppad = (thisbox->items[z].pad*2)+(thisbox->parentpad*2)+thisbox->grouppady;

            if((thisbox->minheight-tmppad) == 0)
               thisbox->items[z].yratio = 1.0;
            else
               thisbox->items[z].yratio = ((float)((thisbox->height * thisbox->parentyratio)-tmppad))/((float)(thisbox->minheight-tmppad));
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
            Box *tmp = WinQueryWindowPtr(thisbox->items[z].hwnd, QWP_USER);

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
         int itemwidth = thisbox->items[z].width + (thisbox->items[z].pad*2);

         if(itemwidth > uxmax)
            uxmax = itemwidth;
         if(thisbox->items[z].hsize != SIZEEXPAND)
         {
            if(itemwidth > upxmax)
               upxmax = itemwidth;
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
         int itemheight = thisbox->items[z].height + (thisbox->items[z].pad*2);

         if(itemheight > uymax)
            uymax = itemheight;
         if(thisbox->items[z].vsize != SIZEEXPAND)
         {
            if(itemheight > upymax)
               upymax = itemheight;
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

   if(thisbox->grouphwnd)
   {
      currentx += 3;
      currenty += thisbox->grouppady - 3;
   }

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
            Box *tmp = WinQueryWindowPtr(thisbox->items[z].hwnd, QWP_USER);

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
         HWND handle = thisbox->items[z].hwnd;
         int vectorx, vectory;

         /* When upxmax != pad*2 then ratios are incorrect. */
         vectorx = (int)((width*thisbox->items[z].xratio)-width);
         vectory = (int)((height*thisbox->items[z].yratio)-height);

         if(width > 0 && height > 0)
         {
            char tmpbuf[100];
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

            WinQueryClassName(handle, 99, tmpbuf);

            if(strncmp(tmpbuf, "#2", 3)==0)
            {
               HWND frame = (HWND)dw_window_get_data(handle, "_dw_combo_box");
               /* Make the combobox big enough to drop down. :) */
               WinSetWindowPos(handle, HWND_TOP, 0, -100,
                           width + vectorx, (height + vectory) + 100, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
               _MySetWindowPos(frame, thisbox->hwnd, HWND_TOP, currentx + pad, currenty + pad,
                           width + vectorx, height + vectory, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
            }
            else if(strncmp(tmpbuf, "#6", 3)==0)
            {
               /* Entryfields on OS/2 have a thick border that isn't on Windows and GTK */
               _MySetWindowPos(handle, thisbox->hwnd, HWND_TOP, (currentx + pad) + 3, (currenty + pad) + 3,
                           (width + vectorx) - 6, (height + vectory) - 6, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
            }
            else if(strncmp(tmpbuf, "#40", 5)==0)
            {
               _MySetWindowPos(handle, thisbox->hwnd, HWND_TOP, currentx + pad, currenty + pad,
                           width + vectorx, height + vectory, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
               _check_resize_notebook(handle);
            }
            else if(strncmp(tmpbuf, SplitbarClassName, strlen(SplitbarClassName)+1)==0)
            {
               /* Then try the bottom or right box */
               float *percent = (float *)dw_window_get_data(handle, "_dw_percent");
               int type = (int)dw_window_get_data(handle, "_dw_type");
               int cx = width + vectorx;
               int cy = height + vectory;

               _MySetWindowPos(handle, thisbox->hwnd, HWND_TOP, currentx + pad, currenty + pad,
                           cx, cy, SWP_MOVE | SWP_SIZE | SWP_ZORDER);

               if(cx > 0 && cy > 0 && percent)
                  _handle_splitbar_resize(handle, *percent, type, cx, cy);
            }
            else
            {
               _MySetWindowPos(handle, thisbox->hwnd, HWND_TOP, currentx + pad, currenty + pad,
                           width + vectorx, height + vectory, SWP_MOVE | SWP_SIZE | SWP_ZORDER);
               if(thisbox->items[z].type == TYPEBOX)
               {
                  Box *boxinfo = WinQueryWindowPtr(handle, QWP_USER);

                  if(boxinfo && boxinfo->grouphwnd)
                     WinSetWindowPos(boxinfo->grouphwnd, HWND_TOP, 0, 0,
                                 width + vectorx, height + vectory, SWP_MOVE | SWP_SIZE);

               }

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

void _do_resize(Box *thisbox, int x, int y)
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

/* This procedure handles WM_QUERYTRACKINFO requests from the frame */
MRESULT EXPENTRY _sizeproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   PFNWP *blah = WinQueryWindowPtr(hWnd, QWP_USER);
   Box *thisbox = NULL;
   HWND box;

   box = WinWindowFromID(hWnd, FID_CLIENT);
   if(box)
      thisbox = WinQueryWindowPtr(box, QWP_USER);

   if(thisbox && !thisbox->titlebar)
   {
      switch(msg)
      {
      case WM_QUERYTRACKINFO:
         {
            if(blah && *blah)
            {
               PTRACKINFO ptInfo;
               int res;
               PFNWP myfunc = *blah;
               res = (int)myfunc(hWnd, msg, mp1, mp2);

               ptInfo = (PTRACKINFO)(mp2);

               ptInfo->ptlMinTrackSize.y = 8;
               ptInfo->ptlMinTrackSize.x = 8;

               return (MRESULT)res;
            }
         }
      }
   }

   if(blah && *blah)
   {
      PFNWP myfunc = *blah;
      return myfunc(hWnd, msg, mp1, mp2);
   }

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

void _Top(HPS hpsPaint, RECTL rclPaint)
{
   POINTL ptl1, ptl2;

   ptl1.x = rclPaint.xLeft;
   ptl2.y = ptl1.y = rclPaint.yTop - 1;
   ptl2.x = rclPaint.xRight - 1;
   GpiMove(hpsPaint, &ptl1);
   GpiLine(hpsPaint, &ptl2);
}

/* Left hits the bottom */
void _Left(HPS hpsPaint, RECTL rclPaint)
{
   POINTL ptl1, ptl2;

   ptl2.x = ptl1.x = rclPaint.xLeft;
   ptl1.y = rclPaint.yTop - 1;
   ptl2.y = rclPaint.yBottom;
   GpiMove(hpsPaint, &ptl1);
   GpiLine(hpsPaint, &ptl2);
}

void _Bottom(HPS hpsPaint, RECTL rclPaint)
{
   POINTL ptl1, ptl2;

   ptl1.x = rclPaint.xRight - 1;
   ptl1.y = ptl2.y = rclPaint.yBottom;
   ptl2.x = rclPaint.xLeft;
   GpiMove(hpsPaint, &ptl1);
   GpiLine(hpsPaint, &ptl2);
}

/* Right hits the top */
void _Right(HPS hpsPaint, RECTL rclPaint)
{
   POINTL ptl1, ptl2;

   ptl2.x = ptl1.x = rclPaint.xRight - 1;
   ptl1.y = rclPaint.yBottom + 1;
   ptl2.y = rclPaint.yTop - 1;
   GpiMove(hpsPaint, &ptl1);
   GpiLine(hpsPaint, &ptl2);
}

/* This procedure handles drawing of a status border */
MRESULT EXPENTRY _statusproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   PFNWP *blah = WinQueryWindowPtr(hWnd, QWP_USER);

   if(msg == WM_MOUSEMOVE && _wndproc(hWnd, msg, mp1, mp2))
      return MPFROMSHORT(FALSE);

   if(blah && *blah)
   {
      PFNWP myfunc = *blah;

      switch(msg)
      {
      case WM_PAINT:
         {
            HPS hpsPaint;
            RECTL rclPaint;
            char buf[1024];

            hpsPaint = WinBeginPaint(hWnd, 0, 0);
            WinQueryWindowRect(hWnd, &rclPaint);
            WinFillRect(hpsPaint, &rclPaint, CLR_PALEGRAY);

            GpiSetColor(hpsPaint, CLR_DARKGRAY);
            _Top(hpsPaint, rclPaint);
            _Left(hpsPaint, rclPaint);

            GpiSetColor(hpsPaint, CLR_WHITE);
            _Right(hpsPaint, rclPaint);
            _Bottom(hpsPaint, rclPaint);

            WinQueryWindowText(hWnd, 1024, buf);
            rclPaint.xLeft += 3;
            rclPaint.xRight--;
            rclPaint.yTop--;
            rclPaint.yBottom++;

            GpiSetColor(hpsPaint, CLR_BLACK);
            WinDrawText(hpsPaint, -1, buf, &rclPaint, DT_TEXTATTRS, DT_TEXTATTRS, DT_VCENTER | DT_LEFT | DT_TEXTATTRS);
            WinEndPaint(hpsPaint);

            return (MRESULT)TRUE;
         }
      }
      return myfunc(hWnd, msg, mp1, mp2);
   }

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

/* This procedure handles pointer changes */
MRESULT EXPENTRY _textproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   PFNWP *blah = WinQueryWindowPtr(hWnd, QWP_USER);

   if(msg == WM_MOUSEMOVE &&_wndproc(hWnd, msg, mp1, mp2))
      return MPFROMSHORT(FALSE);

   if(blah && *blah)
   {
      PFNWP myfunc = *blah;

      return myfunc(hWnd, msg, mp1, mp2);
   }

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

void _click_default(HWND handle)
{
   char tmpbuf[100];

   WinQueryClassName(handle, 99, tmpbuf);

   /* These are the window classes which can
    * obtain input focus.
    */
   if(strncmp(tmpbuf, "#3", 3)==0)
   {
      /* Generate click on default item */
      SignalHandler *tmp = Root;

      /* Find any callbacks for this function */
      while(tmp)
      {
         if(tmp->message == WM_COMMAND)
         {
            int (* API clickfunc)(HWND, void *) = (int (* API)(HWND, void *))tmp->signalfunction;

            /* Make sure it's the right window, and the right ID */
            if(tmp->window == handle)
            {
               clickfunc(tmp->window, tmp->data);
               tmp = NULL;
            }
         }
         if(tmp)
            tmp= tmp->next;
      }
   }
   else
      WinSetFocus(HWND_DESKTOP, handle);
}

#define ENTRY_CUT   1001
#define ENTRY_COPY  1002
#define ENTRY_PASTE 1003
#define ENTRY_UNDO  1004
#define ENTRY_SALL  1005

/* Originally just intended for entryfields, it now serves as a generic
 * procedure for handling TAB presses to change input focus on controls.
 */
MRESULT EXPENTRY _entryproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(hWnd, QWP_USER);
   PFNWP oldproc = 0;
   char tmpbuf[100];

   if(blah)
      oldproc = blah->oldproc;

   WinQueryClassName(hWnd, 99, tmpbuf);

   /* These are the window classes which should get a menu */
   if(strncmp(tmpbuf, "#2", 3)==0 ||  /* Combobox */
      strncmp(tmpbuf, "#6", 3)==0 ||  /* Entryfield */
      strncmp(tmpbuf, "#10", 4)==0 || /* MLE */
      strncmp(tmpbuf, "#32", 4)==0)   /* Spinbutton */
   {
      switch(msg)
      {
      case WM_CONTEXTMENU:
         {
            HMENUI hwndMenu = dw_menu_new(0L);
            long x, y;

            if(strncmp(tmpbuf, "#10", 4)==0 && !WinSendMsg(hWnd, MLM_QUERYREADONLY, 0, 0))
            {
               dw_menu_append_item(hwndMenu, "Undo", ENTRY_UNDO, 0L, TRUE, FALSE, 0L);
               dw_menu_append_item(hwndMenu, "", 0L, 0L, TRUE, FALSE, 0L);
            }
            dw_menu_append_item(hwndMenu, "Copy", ENTRY_COPY, 0L, TRUE, FALSE, 0L);
            if((strncmp(tmpbuf, "#10", 4)!=0  && !dw_window_get_data(hWnd, "_dw_disabled")) || (strncmp(tmpbuf, "#10", 4)==0 && !WinSendMsg(hWnd, MLM_QUERYREADONLY, 0, 0)))
            {
               dw_menu_append_item(hwndMenu, "Cut", ENTRY_CUT, 0L, TRUE, FALSE, 0L);
               dw_menu_append_item(hwndMenu, "Paste", ENTRY_PASTE, 0L, TRUE, FALSE, 0L);
            }
            dw_menu_append_item(hwndMenu, "", 0L, 0L, TRUE, FALSE, 0L);
            dw_menu_append_item(hwndMenu, "Select All", ENTRY_SALL, 0L, TRUE, FALSE, 0L);

            WinSetFocus(HWND_DESKTOP, hWnd);
            dw_pointer_query_pos(&x, &y);
            dw_menu_popup(&hwndMenu, hWnd, x, y);
         }
         break;
      case WM_COMMAND:
         {
            ULONG command = COMMANDMSG(&msg)->cmd;

            /* MLE */
            if(strncmp(tmpbuf, "#10", 4)==0)
            {
               switch(command)
               {
               case ENTRY_CUT:
                  return WinSendMsg(hWnd, MLM_CUT, 0, 0);
               case ENTRY_COPY:
                  return WinSendMsg(hWnd, MLM_COPY, 0, 0);
               case ENTRY_PASTE:
                  return WinSendMsg(hWnd, MLM_PASTE, 0, 0);
               case ENTRY_UNDO:
                  return WinSendMsg(hWnd, MLM_UNDO, 0, 0);
               case ENTRY_SALL:
                  {
                     ULONG len = (ULONG)WinSendMsg(hWnd, MLM_QUERYTEXTLENGTH, 0, 0);
                     return WinSendMsg(hWnd, MLM_SETSEL, 0, (MPARAM)len);
                  }
               }
            }
            else /* Other */
            {
               HWND handle = hWnd;

               /* Get the entryfield handle from multi window controls */
               if(strncmp(tmpbuf, "#2", 3)==0)
                  handle = WinWindowFromID(hWnd, 667);

               if(handle)
               {
                  switch(command)
                  {
                  case ENTRY_CUT:
                     return WinSendMsg(handle, EM_CUT, 0, 0);
                  case ENTRY_COPY:
                     return WinSendMsg(handle, EM_COPY, 0, 0);
                  case ENTRY_PASTE:
                     return WinSendMsg(handle, EM_PASTE, 0, 0);
                  case ENTRY_SALL:
                     {
                        LONG len = WinQueryWindowTextLength(hWnd);
                        return WinSendMsg(hWnd, EM_SETSEL, MPFROM2SHORT(0, (SHORT)len), 0);
                     }
                  }
               }
            }
         }
         break;
      }
   }

   switch(msg)
   {
   case WM_BUTTON1DOWN:
   case WM_BUTTON2DOWN:
   case WM_BUTTON3DOWN:
      {
         if(strncmp(tmpbuf, "#32", 4)==0)
            _run_event(hWnd, WM_SETFOCUS, (MPARAM)FALSE, (MPARAM)TRUE);
      }
      break;
   case WM_CONTROL:
      {
         if(strncmp(tmpbuf, "#38", 4)==0)
            _run_event(hWnd, msg, mp1, mp2);
      }
      break;
   case WM_SETFOCUS:
      _run_event(hWnd, msg, mp1, mp2);
      break;
   case WM_CHAR:
      if(_run_event(hWnd, msg, mp1, mp2) == (MRESULT)TRUE)
         return (MRESULT)TRUE;
      if(SHORT1FROMMP(mp2) == '\t')
      {
         if(CHARMSG(&msg)->fs & KC_SHIFT)
            _shift_focus_back(hWnd);
         else
            _shift_focus(hWnd);
         return FALSE;
      }
      else if(SHORT1FROMMP(mp2) == '\r' && blah && blah->clickdefault)
         _click_default(blah->clickdefault);
      /* When you hit escape we get this value and the
       * window hangs for reasons unknown. (in an MLE)
       */
      else if(SHORT1FROMMP(mp2) == 283)
         return (MRESULT)TRUE;

      break;
   case WM_SIZE:
      {
         /* If it's a slider... make sure it shows the correct value */
         if(strncmp(tmpbuf, "#38", 4)==0)
            WinPostMsg(hWnd, WM_USER+7, 0, 0);
      }
      break;
   case WM_USER+7:
      {
         int pos = (int)dw_window_get_data(hWnd, "_dw_slider_value");
         WinSendMsg(hWnd, SLM_SETSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), (MPARAM)pos);
      }
      break;
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   }

   if(oldproc)
      return oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

/*  Deal with combobox specifics and enhancements */
MRESULT EXPENTRY _comboentryproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(hWnd, QWP_USER);

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_CONTEXTMENU:
   case WM_COMMAND:
      return _entryproc(hWnd, msg, mp1, mp2);
   case WM_SETFOCUS:
      _run_event(hWnd, msg, mp1, mp2);
      break;
   case WM_CHAR:
      if(_run_event(hWnd, msg, mp1, mp2) == (MRESULT)TRUE)
         return (MRESULT)TRUE;
      /* A Similar problem to the MLE, if ESC just return */
      if(SHORT1FROMMP(mp2) == 283)
         return (MRESULT)TRUE;
      break;
   }

   if(blah && blah->oldproc)
      return blah->oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

/* Enhance the standard OS/2 MLE control */
MRESULT EXPENTRY _mleproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   switch(msg)
   {
   case WM_VSCROLL:
      if(SHORT2FROMMP(mp2) == SB_SLIDERTRACK)
      {
         USHORT pos = SHORT1FROMMP(mp2);

         WinSendMsg(hWnd, msg, mp1, MPFROM2SHORT(pos, SB_SLIDERPOSITION));
      }
      break;
   }
   return _entryproc(hWnd, msg, mp1, mp2);
}

/* Handle special messages for the spinbutton's entryfield */
MRESULT EXPENTRY _spinentryproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(hWnd, QWP_USER);
   PFNWP oldproc = 0;

   if(blah)
      oldproc = blah->oldproc;

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_CONTEXTMENU:
   case WM_COMMAND:
      return _entryproc(hWnd, msg, mp1, mp2);
   }

   if(oldproc)
      return oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

int _dw_int_pos(HWND hwnd)
{
   int pos = (int)dw_window_get_data(hwnd, "_dw_percent_value");
   int range = _dw_percent_get_range(hwnd);
   float fpos = (float)pos;
   float frange = (float)range;
   float fnew = (fpos/1000.0)*frange;
   return (int)fnew;
}

void _dw_int_set(HWND hwnd, int pos)
{
   int inew, range = _dw_percent_get_range(hwnd);
   if(range)
   {
      float fpos = (float)pos;
      float frange = (float)range;
      float fnew = (fpos/frange)*1000.0;
      inew = (int)fnew;
      dw_window_set_data(hwnd, "_dw_percent_value", (void *)inew);
   }
}

/* Handle size changes in the percent class */
MRESULT EXPENTRY _percentproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(hWnd, QWP_USER);
   PFNWP oldproc = 0;

   if(blah)
      oldproc = blah->oldproc;

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_SIZE:
      WinPostMsg(hWnd, WM_USER+7, 0, 0);
      break;
   case WM_USER+7:
      WinSendMsg(hWnd, SLM_SETSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION,SMA_RANGEVALUE), (MPARAM)_dw_int_pos(hWnd));
      break;
   }

   if(oldproc)
      return oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

/* Handle correct painting of a combobox with the WS_CLIPCHILDREN
 * flag enabled, and also handle TABs to switch input focus.
 */
MRESULT EXPENTRY _comboproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = WinQueryWindowPtr(hWnd, QWP_USER);
   PFNWP oldproc = 0;

   if(blah)
      oldproc = blah->oldproc;

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_CHAR:
      if(SHORT1FROMMP(mp2) == '\t')
      {
         if(CHARMSG(&msg)->fs & KC_SHIFT)
            _shift_focus_back(hWnd);
         else
            _shift_focus(hWnd);
         return FALSE;
      }
      else if(SHORT1FROMMP(mp2) == '\r' && blah && blah->clickdefault)
         _click_default(blah->clickdefault);
      break;
   case WM_BUTTON1DBLCLK:
   case WM_BUTTON2DBLCLK:
   case WM_BUTTON3DBLCLK:
      if(dw_window_get_data(hWnd, "_dw_disabled"))
         return (MRESULT)TRUE;
      break;
   case WM_BUTTON1DOWN:
   case WM_BUTTON2DOWN:
   case WM_BUTTON3DOWN:
      if(_run_event(hWnd, msg, mp1, mp2) == (MRESULT)TRUE)
         return (MRESULT)TRUE;
      _run_event(hWnd, WM_SETFOCUS, (MPARAM)FALSE, (MPARAM)TRUE);
      break;
   case WM_SETFOCUS:
      _run_event(hWnd, msg, mp1, mp2);
      break;
   case WM_PAINT:
      {
         HWND entry, frame = (HWND)dw_window_get_data(hWnd, "_dw_combo_box"), parent = WinQueryWindow(frame, QW_PARENT);
         HPS hpsPaint;
         POINTL ptl;
         unsigned long width, height, thumbheight = 0;
         ULONG color;

         if((entry = (HWND)dw_window_get_data(hWnd, "_dw_comboentry")) != NULLHANDLE)
            dw_window_get_pos_size(entry, 0, 0, 0, &thumbheight);

         if(!thumbheight)
            thumbheight = WinQuerySysValue(HWND_DESKTOP, SV_CYVSCROLLARROW);

         /* Add 6 because it has a thick border like the entryfield */
         thumbheight += 6;

         color = (ULONG)dw_window_get_data(parent, "_dw_fore");
         dw_window_get_pos_size(hWnd, 0, 0, &width, &height);

         if(height > thumbheight)
         {
            hpsPaint = WinGetPS(hWnd);
            if(color)
               GpiSetColor(hpsPaint, _internal_color(color-1));
            else
               GpiSetColor(hpsPaint, CLR_PALEGRAY);

            ptl.x = ptl.y = 0;
            GpiMove(hpsPaint, &ptl);

            ptl.x = width;
            ptl.y = height - thumbheight;
            GpiBox(hpsPaint, DRO_FILL, &ptl, 0, 0);

            WinReleasePS(hpsPaint);
         }
      }
      break;
   }
   if(oldproc)
      return oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

void _GetPPFont(HWND hwnd, char *buff)
{
    ULONG AttrFound;
    BYTE  AttrValue[128];
    ULONG cbRetLen;

    cbRetLen = WinQueryPresParam(hwnd,
                                 PP_FONTNAMESIZE,
                                 0,
                                 &AttrFound,
                                 sizeof(AttrValue),
                                 &AttrValue,
                                 QPF_NOINHERIT);

    if(PP_FONTNAMESIZE == AttrFound && cbRetLen)
    {
        memcpy(buff, AttrValue, cbRetLen);
    }
}

int _HandleScroller(HWND handle, int pos, int which)
{
   MPARAM res;
   int min, max, page;

   if(which == SB_SLIDERTRACK)
      return pos;

   pos = dw_scrollbar_get_pos(handle);
   res = WinSendMsg(handle, SBM_QUERYRANGE, 0, 0);

   min = SHORT1FROMMP(res);
   max = SHORT2FROMMP(res);
   page = (int)dw_window_get_data(handle, "_dw_scrollbar_visible");

   switch(which)
   {
   case SB_LINEUP:
      pos = pos - 1;
      if(pos < min)
         pos = min;
      dw_scrollbar_set_pos(handle, pos);
      return pos;
   case SB_LINEDOWN:
      pos = pos + 1;
      if(pos > max)
         pos = max;
      dw_scrollbar_set_pos(handle, pos);
      return pos;
   case SB_PAGEUP:
      pos = pos - page;
      if(pos < min)
         pos = min;
      dw_scrollbar_set_pos(handle, pos);
      return pos;
   case SB_PAGEDOWN:
      pos = pos + page;
      if(pos > max)
         pos = max;
      dw_scrollbar_set_pos(handle, pos);
      return pos;
   }
   return -1;
}

void _clear_emphasis(void)
{
   if(hwndEmph && WinIsWindow(dwhab, hwndEmph) && pCoreEmph)
      WinSendMsg(hwndEmph, CM_SETRECORDEMPHASIS, pCoreEmph, MPFROM2SHORT(FALSE, CRA_SOURCE));
   hwndEmph = NULLHANDLE;
   pCoreEmph = NULL;
}

/* Find the desktop window handle */
HWND _menu_owner(HWND handle)
{
   HWND menuowner = NULLHANDLE, lastowner = (HWND)dw_window_get_data(handle, "_dw_owner");
   int menubar = (int)dw_window_get_data(handle, "_dw_menubar");

   /* Find the toplevel window */
   while(!menubar && (menuowner = (HWND)dw_window_get_data(lastowner, "_dw_owner")) != NULLHANDLE)
   {
      menubar = (int)dw_window_get_data(lastowner, "_dw_menubar");
      lastowner = menuowner;
   }
   if(menuowner && menubar)
   {
      HWND client = WinWindowFromID(menuowner, FID_CLIENT);

      return client ? client : menuowner;
   }
   return NULLHANDLE;
}

MRESULT EXPENTRY _run_event(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   int result = -1;
   SignalHandler *tmp = Root;
   ULONG origmsg = msg;

   if(msg == WM_BUTTON2DOWN || msg == WM_BUTTON3DOWN)
      msg = WM_BUTTON1DOWN;
   if(msg == WM_BUTTON2UP || msg == WM_BUTTON3UP)
      msg = WM_BUTTON1UP;
   if(msg == WM_VSCROLL || msg == WM_HSCROLL)
      msg = WM_CONTROL;

   /* Find any callbacks for this function */
   while(tmp)
   {
      if(tmp->message == msg || msg == WM_CONTROL || tmp->message == WM_USER+1)
      {
         switch(msg)
         {
         case WM_SETFOCUS:
            {
               if((mp2 && tmp->message == WM_SETFOCUS) || (!mp2 && tmp->message == WM_USER+1))
               {
                  int (* API setfocusfunc)(HWND, void *) = (int (* API)(HWND, void *))tmp->signalfunction;

                  if(hWnd == tmp->window || WinWindowFromID(tmp->window, FID_CLIENT) == hWnd)
                  {
                     result = setfocusfunc(tmp->window, tmp->data);
                     tmp = NULL;
                  }
               }
            }
            break;
         case WM_TIMER:
            {
               int (* API timerfunc)(void *) = (int (* API)(void *))tmp->signalfunction;
               if(tmp->id == (int)mp1)
               {
                  if(!timerfunc(tmp->data))
                     dw_timer_disconnect(tmp->id);
                  tmp = NULL;
               }
               result = 0;
            }
            break;
         case WM_SIZE:
            {
               int (* API sizefunc)(HWND, int, int, void *) = (int (* API)(HWND, int, int, void *))tmp->signalfunction;

               if((hWnd == tmp->window || WinWindowFromID(tmp->window, FID_CLIENT) == hWnd) && SHORT1FROMMP(mp2) && SHORT2FROMMP(mp2))
               {
                  result = sizefunc(tmp->window, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2), tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_BUTTON1DOWN:
            {
               POINTS pts = (*((POINTS*)&mp1));
               int (* API buttonfunc)(HWND, int, int, int, void *) = (int (* API)(HWND, int, int, int, void *))tmp->signalfunction;

               if(hWnd == tmp->window || WinWindowFromID(tmp->window, FID_CLIENT) == hWnd || WinQueryCapture(HWND_DESKTOP) == tmp->window)
               {
                  int button = 0;

                  switch(origmsg)
                  {
                  case WM_BUTTON1DOWN:
                     button = 1;
                     break;
                  case WM_BUTTON2DOWN:
                     button = 2;
                     break;
                  case WM_BUTTON3DOWN:
                     button = 3;
                     break;
                  }

                  result = buttonfunc(tmp->window, pts.x, _get_frame_height(tmp->window) - pts.y, button, tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_BUTTON1UP:
            {
               POINTS pts = (*((POINTS*)&mp1));
               int (* API buttonfunc)(HWND, int, int, int, void *) = (int (* API)(HWND, int, int, int, void *))tmp->signalfunction;

               if(hWnd == tmp->window || WinWindowFromID(tmp->window, FID_CLIENT) == hWnd || WinQueryCapture(HWND_DESKTOP) == tmp->window)
               {
                  int button = 0;

                  switch(origmsg)
                  {
                  case WM_BUTTON1UP:
                     button = 1;
                     break;
                  case WM_BUTTON2UP:
                     button = 2;
                     break;
                  case WM_BUTTON3UP:
                     button = 3;
                     break;
                  }

                  result = buttonfunc(tmp->window, pts.x, WinQueryWindow(tmp->window, QW_PARENT) == HWND_DESKTOP ? dw_screen_height() - pts.y : _get_height(tmp->window) - pts.y, button, tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_MOUSEMOVE:
            {
               int (* API motionfunc)(HWND, int, int, int, void *) = (int (* API)(HWND, int, int, int, void *))tmp->signalfunction;

               if(hWnd == tmp->window || WinWindowFromID(tmp->window, FID_CLIENT) == hWnd || WinQueryCapture(HWND_DESKTOP) == tmp->window)
               {
                  int keys = 0;
                  SHORT x = SHORT1FROMMP(mp1), y = SHORT2FROMMP(mp1);

                  if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON1) & 0x8000)
                     keys = DW_BUTTON1_MASK;
                  if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON2) & 0x8000)
                     keys |= DW_BUTTON2_MASK;
                  if (WinGetKeyState(HWND_DESKTOP, VK_BUTTON3) & 0x8000)
                     keys |= DW_BUTTON3_MASK;

                  result = motionfunc(tmp->window, x, _get_frame_height(tmp->window) - y, keys, tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_CHAR:
            {
               int (* API keypressfunc)(HWND, char, int, int, void *) = (int (* API)(HWND, char, int, int, void *))tmp->signalfunction;

               if((hWnd == tmp->window || _toplevel_window(hWnd) == tmp->window) && !(SHORT1FROMMP(mp1) & KC_KEYUP))
               {
                  int vk;
                  char ch = 0;

                  if(SHORT1FROMMP(mp1) & KC_CHAR)
                     ch = (char)SHORT1FROMMP(mp2);
                  if(SHORT1FROMMP(mp1) & KC_VIRTUALKEY)
                     vk = SHORT2FROMMP(mp2);
                  else
                     vk = SHORT1FROMMP(mp2) + 128;

                  /* This is a hack to fix shift presses showing
                   * up as tabs!
                   */
                  if(ch == '\t' && !(SHORT1FROMMP(mp1) & KC_CHAR))
                  {
                     ch = 0;
                     vk = VK_SHIFT;
                  }

                  result = keypressfunc(tmp->window, ch, vk,
                                   SHORT1FROMMP(mp1) & (KC_ALT | KC_SHIFT | KC_CTRL), tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_CLOSE:
            {
               int (* API closefunc)(HWND, void *) = (int (* API)(HWND, void *))tmp->signalfunction;

               if(hWnd == tmp->window || hWnd == WinWindowFromID(tmp->window, FID_CLIENT))
               {
                  result = closefunc(tmp->window, tmp->data);
                  if(result)
                     result = FALSE;
                  tmp = NULL;
               }
            }
            break;
         case WM_PAINT:
            {
               HPS hps;
               DWExpose exp;
               int (* API exposefunc)(HWND, DWExpose *, void *) = (int (* API)(HWND, DWExpose *, void *))tmp->signalfunction;
               RECTL  rc;

               if(hWnd == tmp->window)
               {
                  int height = _get_height(hWnd);

                  hps = WinBeginPaint(hWnd, 0L, &rc);
                  exp.x = rc.xLeft;
                  exp.y = height - rc.yTop - 1;
                  exp.width = rc.xRight - rc. xLeft;
                  exp.height = rc.yTop - rc.yBottom;
                  result = exposefunc(hWnd, &exp, tmp->data);
                  WinEndPaint(hps);
               }
            }
            break;
         case WM_COMMAND:
            {
               int (* API clickfunc)(HWND, void *) = (int (* API)(HWND, void *))tmp->signalfunction;
               ULONG command = COMMANDMSG(&msg)->cmd;

               if(tmp->id && command == tmp->id)
               {
                  HWND menuowner = _menu_owner(tmp->window);

                  if(menuowner == hWnd || menuowner == NULLHANDLE)
                  {
                     result = clickfunc(tmp->window, tmp->data);
                     tmp = NULL;
                  }
               }
               else if(tmp->window < 65536 && command == tmp->window)
               {
                  result = clickfunc(popup ?  popup : tmp->window, tmp->data);
                  tmp = NULL;
               }
            }
            break;
         case WM_CONTROL:
            if(origmsg == WM_VSCROLL || origmsg == WM_HSCROLL || tmp->message == SHORT2FROMMP(mp1) ||
               (tmp->message == SLN_SLIDERTRACK && SHORT2FROMMP(mp1) == SLN_CHANGE))
            {
               int svar = SLN_SLIDERTRACK;
               if(origmsg == WM_CONTROL)
                  svar = SHORT2FROMMP(mp1);

               switch(svar)
               {
               case CN_ENTER:
                  {
                     int (* API containerselectfunc)(HWND, char *, void *) = (int (* API)(HWND, char *, void *))tmp->signalfunction;
                     int id = SHORT1FROMMP(mp1);
                     HWND conthwnd = dw_window_from_id(hWnd, id);
                     char *text = NULL;

                     if(mp2)
                     {
                        PRECORDCORE pre;

                        pre = ((PNOTIFYRECORDENTER)mp2)->pRecord;
                        if(pre)
                           text = pre->pszIcon;
                     }

                     if(tmp->window == conthwnd)
                     {
                        result = containerselectfunc(tmp->window, text, tmp->data);
                        tmp = NULL;
                     }
                  }
                  break;
               case CN_EXPANDTREE:
                  {
                     int (* API treeexpandfunc)(HWND, HTREEITEM, void *) = (int (* API)(HWND, HTREEITEM, void *))tmp->signalfunction;
                     int id = SHORT1FROMMP(mp1);
                     HWND conthwnd = dw_window_from_id(hWnd, id);

                     if(tmp->window == conthwnd)
                     {
                        result = treeexpandfunc(tmp->window, (HTREEITEM)mp2, tmp->data);
                        tmp = NULL;
                     }
                  }
                  break;
               case CN_CONTEXTMENU:
                  {
                     int (* API containercontextfunc)(HWND, char *, int, int, void *, void *) = (int (* API)(HWND, char *, int, int, void *, void *))tmp->signalfunction;
                     int id = SHORT1FROMMP(mp1);
                     HWND conthwnd = dw_window_from_id(hWnd, id);
                     char *text = NULL;
                     void *user = NULL;
                     LONG x,y;

                     if(mp2)
                     {
                        PCNRITEM pci;

                        pci = (PCNRITEM)mp2;

                        text = pci->rc.pszIcon;
                        user = pci->user;
                     }

                     dw_pointer_query_pos(&x, &y);

                     if(tmp->window == conthwnd)
                     {
                        int container = (int)dw_window_get_data(tmp->window, "_dw_container");

                        if(mp2)
                        {
                           if(!container)
                           {
                              NOTIFYRECORDEMPHASIS pre;

                              dw_tree_item_select(tmp->window, (HTREEITEM)mp2);
                              pre.pRecord = mp2;
                              pre.fEmphasisMask = CRA_CURSORED;
                              pre.hwndCnr = tmp->window;
                              _run_event(hWnd, WM_CONTROL, MPFROM2SHORT(0, CN_EMPHASIS), (MPARAM)&pre);
                              pre.pRecord->flRecordAttr |= CRA_CURSORED;
                           }
                           else
                           {
                              if(pCoreEmph)
                                 _clear_emphasis();
                              hwndEmph = tmp->window;
                              pCoreEmph = mp2;
                              WinSendMsg(tmp->window, CM_SETRECORDEMPHASIS, mp2, MPFROM2SHORT(TRUE, CRA_SOURCE));
                           }
                        }
                        result = containercontextfunc(tmp->window, text, x, y, tmp->data, user);
                        tmp = NULL;
                     }
                  }
                  break;
               case CN_EMPHASIS:
                  {
                     PNOTIFYRECORDEMPHASIS pre = (PNOTIFYRECORDEMPHASIS)mp2;
                     static int emph_recurse = 0;

                     if(!emph_recurse)
                     {
                        emph_recurse = 1;

                        if(mp2)
                        {
                           if(tmp->window == pre->hwndCnr)
                           {
                              PCNRITEM pci = (PCNRITEM)pre->pRecord;

                              if(pci && pre->fEmphasisMask & CRA_CURSORED && (pci->rc.flRecordAttr & CRA_CURSORED))
                              {
                                 int (* API treeselectfunc)(HWND, HTREEITEM, char *, void *, void *) = (int (* API)(HWND, HTREEITEM, char *, void *, void *))tmp->signalfunction;

                                 if(dw_window_get_data(tmp->window, "_dw_container"))
                                    result = treeselectfunc(tmp->window, 0, pci->rc.pszIcon, tmp->data, 0);
                                 else
                                 {
                                    if(lasthcnr == tmp->window && lastitem == (HWND)pci)
                                    {
                                       lasthcnr = 0;
                                       lastitem = 0;
                                    }
                                    else
                                    {
                                       lasthcnr = tmp->window;
                                       lastitem = (HWND)pci;
                                       result = treeselectfunc(tmp->window, (HTREEITEM)pci, pci->rc.pszIcon, tmp->data, pci->user);
                                    }
                                 }
                                 tmp = NULL;
                              }
                           }
                        }
                        emph_recurse = 0;
                     }
                  }
                  break;
               case LN_SELECT:
                  {
                     char classbuf[100];

                     WinQueryClassName(tmp->window, 99, classbuf);

                     if(strncmp(classbuf, "#38", 4) == 0)
                     {
                        int (* API valuechangedfunc)(HWND, int, void *) = (int (* API)(HWND, int, void *))tmp->signalfunction;

                        if(tmp->window == hWnd || WinQueryWindow(tmp->window, QW_PARENT) == hWnd)
                        {
                           static int lastvalue = -1;
                           static HWND lasthwnd = NULLHANDLE;
                           int ulValue = (int)WinSendMsg(tmp->window, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), 0);
                           if(lastvalue != ulValue || lasthwnd != tmp->window)
                           {
                              result = valuechangedfunc(tmp->window, ulValue, tmp->data);
                              lastvalue = ulValue;
                              lasthwnd = tmp->window;
                           }
                           tmp = NULL;
                        }
                     }
                     else
                     {
                        int (* API listboxselectfunc)(HWND, int, void *) = (int (* API )(HWND, int, void *))tmp->signalfunction;
                        int id = SHORT1FROMMP(mp1);
                        HWND conthwnd = dw_window_from_id(hWnd, id);
                        static int _recursing = 0;

                        if(_recursing == 0 && (tmp->window == conthwnd || (!id && tmp->window == (HWND)mp2)))
                        {
                           char buf1[500];
                           unsigned int index = dw_listbox_selected(tmp->window);

                           dw_listbox_get_text(tmp->window, index, buf1, 500);

                           _recursing = 1;

                           if(id && strncmp(classbuf, "#2", 3)==0)
                           {
                              char *buf2;

                              buf2 = dw_window_get_text(tmp->window);

                              /* This is to make sure the listboxselect function doesn't
                               * get called if the user is modifying the entry text.
                               */
                              if(buf2 && *buf2 && *buf1 && strncmp(buf1, buf2, 500) == 0)
                                 result = listboxselectfunc(tmp->window, index, tmp->data);

                              if(buf2)
                                 free(buf2);
                           }
                           else
                              result = listboxselectfunc(tmp->window, index, tmp->data);

                           _recursing = 0;
                           tmp = NULL;
                        }
                     }
                  }
                  break;
               case SLN_SLIDERTRACK:
                  {
                     int (* API valuechangedfunc)(HWND, int, void *) = (int (* API)(HWND, int, void *))tmp->signalfunction;

                     if(origmsg == WM_CONTROL)
                     {
                        /* Handle Slider control */
                        if(tmp->window == hWnd || WinQueryWindow(tmp->window, QW_PARENT) == hWnd)
                        {
                           static int lastvalue = -1;
                           static HWND lasthwnd = NULLHANDLE;
                           int ulValue = (int)WinSendMsg(tmp->window, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), 0);
                           if(lastvalue != ulValue || lasthwnd != tmp->window)
                           {
                              dw_window_set_data(tmp->window, "_dw_slider_value", (void *)ulValue);
                              result = valuechangedfunc(tmp->window, ulValue, tmp->data);
                              lastvalue = ulValue;
                              lasthwnd = tmp->window;
                           }
                           tmp = NULL;
                        }
                     }
                     else
                     {
                        /* Handle scrollbar control */
                        if(tmp->window > 65535 && tmp->window == WinWindowFromID(hWnd, (ULONG)mp1))
                        {
                           int pos = _HandleScroller(tmp->window, (int)SHORT1FROMMP(mp2), (int)SHORT2FROMMP(mp2));;

                           if(pos > -1)
                           {
                              dw_window_set_data(tmp->window, "_dw_scrollbar_value", (void *)pos);
                              result = valuechangedfunc(tmp->window, pos, tmp->data);
                           }
                           result = 0;
                           tmp = NULL;
                        }
                     }
                  }
                  break;
               case BKN_PAGESELECTED:
                  {
                     PAGESELECTNOTIFY *psn = (PAGESELECTNOTIFY *)mp2;

                     if(psn && tmp->window == psn->hwndBook)
                     {
                        int (* API switchpagefunc)(HWND, unsigned long, void *) = (int (* API)(HWND, unsigned long, void *))tmp->signalfunction;

                        result = switchpagefunc(tmp->window, psn->ulPageIdNew, tmp->data);
                        tmp = NULL;
                     }
                  }
                  break;
               }
            }
            break;
         }
      }

      if(tmp)
         tmp = tmp->next;

   }
   return (MRESULT)result;
}

/* Gets a DW_RGB value from the three spinbuttons */
unsigned long _dw_color_spin_get(HWND window)
{
   HWND button = (HWND)dw_window_get_data(window, "_dw_red_spin");
   long red, green, blue;

   red = dw_spinbutton_get_pos(button);
   button = (HWND)dw_window_get_data(window, "_dw_green_spin");
   green = dw_spinbutton_get_pos(button);
   button = (HWND)dw_window_get_data(window, "_dw_blue_spin");
   blue = dw_spinbutton_get_pos(button);

   return DW_RGB(red, green, blue);
}

/* Set the three spinbuttons from a DW_RGB value */
void _dw_color_spin_set(HWND window, unsigned long value)
{
   HWND button = (HWND)dw_window_get_data(window, "_dw_red_spin");
   dw_window_set_data(window, "_dw_updating", (void *)1);
   dw_spinbutton_set_pos(button, DW_RED_VALUE(value));
   button = (HWND)dw_window_get_data(window, "_dw_green_spin");
   dw_spinbutton_set_pos(button, DW_GREEN_VALUE(value));
   button = (HWND)dw_window_get_data(window, "_dw_blue_spin");
   dw_spinbutton_set_pos(button, DW_BLUE_VALUE(value));
   dw_window_set_data(window, "_dw_updating", NULL);
}

/* Sets the color selection control to be a DW_RGB value */
void _dw_col_set(HWND col, unsigned long value)
{
   WinSendMsg(col, 0x0602, MPFROMLONG(_os2_color(value)), 0);
   if(!IS_WARP4())
      WinSendMsg(col, 0x1384, MPFROMLONG(_os2_color(value)), 0);
}

/* Handles control messages sent to the box (owner). */
MRESULT EXPENTRY _controlproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   Box *blah = WinQueryWindowPtr(hWnd, QWP_USER);

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hWnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_VSCROLL:
   case WM_HSCROLL:
      if(_run_event(hWnd, msg, mp1, mp2))
      {
         HWND window = WinWindowFromID(hWnd, (ULONG)mp1);
         _HandleScroller(window, (int)SHORT1FROMMP(mp2), (int)SHORT2FROMMP(mp2));
      }
      break;
      /* Handles Color Selection control messages */
   case 0x0601:
   case 0x130C:
      {
         HWND window = (HWND)dw_window_get_data(hWnd, "_dw_window");
         unsigned long val = (unsigned long)mp1;

         if(window)
            _dw_color_spin_set(window, DW_RGB((val & 0xFF0000) >> 16, (val & 0xFF00) >> 8, val & 0xFF));
      }
      break;
   case WM_CONTROL:
      if((SHORT2FROMMP(mp1) == SPBN_CHANGE || SHORT2FROMMP(mp1) == SPBN_ENDSPIN))
      {
         HWND window = (HWND)dw_window_get_data(hWnd, "_dw_window");

         if(window && !dw_window_get_data(window, "_dw_updating"))
         {
            unsigned long val = _dw_color_spin_get(window);
            HWND col = (HWND)dw_window_get_data(window, "_dw_col");

            _dw_col_set(col, val);
         }
      }
      _run_event(hWnd, msg, mp1, mp2);
      break;
   }

   if(blah && blah->oldproc)
      return blah->oldproc(hWnd, msg, mp1, mp2);

   return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

/* The main window procedure for Dynamic Windows, all the resizing code is done here. */
MRESULT EXPENTRY _wndproc(HWND hWnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   int result = -1;
   static int command_active = 0;
   void (* API windowfunc)(PVOID) = 0L;

   if(!command_active)
   {
        /* Make sure we don't end up in infinite recursion */
      command_active = 1;

      result = (int)_run_event(hWnd, msg, mp1, mp2);

      command_active = 0;
   }

   /* Now that any handlers are done... do normal processing */
   switch( msg )
   {
   case WM_ERASEBACKGROUND:
      return 0;

   case WM_PAINT:
      {
      HPS    hps;
      RECTL  rc;

      hps = WinBeginPaint( hWnd, 0L, &rc );
      WinEndPaint( hps );
      break;
      }

   case WM_SIZE:
      {
         Box *mybox = (Box *)WinQueryWindowPtr(hWnd, QWP_USER);

         if(!SHORT1FROMMP(mp2) && !SHORT2FROMMP(mp2))
            return (MPARAM)TRUE;

         if(mybox && mybox->flags != DW_MINIMIZED)
         {
            /* Hide the window when recalculating to reduce
             * CPU load.
             */
            WinShowWindow(hWnd, FALSE);

                if(mybox->items)
               WinSetWindowPos(mybox->items[0].hwnd, HWND_TOP, 0, 0, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2), SWP_MOVE | SWP_SIZE);

            _do_resize(mybox, SHORT1FROMMP(mp2), SHORT2FROMMP(mp2));

            WinShowWindow(hWnd, TRUE);
         }
      }
      break;
   case WM_MINMAXFRAME:
      {
         Box *mybox = (Box *)WinQueryWindowPtr(hWnd, QWP_USER);
         SWP *swp = (SWP *)mp1;

         if(mybox && (swp->fl & SWP_MINIMIZE))
            mybox->flags = DW_MINIMIZED;

         if(mybox && (swp->fl & SWP_RESTORE))
         {
            if(!mybox->titlebar && mybox->hwndtitle)
               WinSetParent(mybox->hwndtitle, HWND_OBJECT, FALSE);
            mybox->flags = 0;
         }

         if(mybox && (swp->fl & (SWP_MAXIMIZE | SWP_RESTORE)))
         {
            int z;
            SWP swp2;

            WinQueryWindowPos(swp->hwnd, &swp2);

            if(swp2.cx == swp->cx && swp2.cy == swp->cy)
               return FALSE;

            mybox->flags = 0;

            /* Hide the window when recalculating to reduce
             * CPU load.
             */
            WinShowWindow(hWnd, FALSE);

            _do_resize(mybox, swp->cx, swp->cy);

            if(mybox->count == 1 && mybox->items[0].type == TYPEBOX)
            {
               mybox = (Box *)WinQueryWindowPtr(mybox->items[0].hwnd, QWP_USER);

               for(z=0;z<mybox->count;z++)
                  _check_resize_notebook(mybox->items[z].hwnd);

            }

            WinShowWindow(hWnd, TRUE);
         }
      }
      break;
   case WM_CONTROL:
      switch(SHORT2FROMMP(mp1))
      {
      case BKN_PAGESELECTEDPENDING:
         {
            PAGESELECTNOTIFY *psn = (PAGESELECTNOTIFY *)mp2;
            HWND pagehwnd = (HWND)WinSendMsg(psn->hwndBook, BKM_QUERYPAGEWINDOWHWND, MPFROMLONG(psn->ulPageIdNew), 0);
            Box *pagebox = (Box *)WinQueryWindowPtr(pagehwnd, QWP_USER);
            long x, y;
            unsigned long width, height;
            RECTL rc;

            if(pagebox && psn->ulPageIdNew != psn->ulPageIdCur)
            {
               dw_window_get_pos_size(psn->hwndBook, &x, &y, &width, &height);

               rc.xLeft = x;
               rc.yBottom = y;
               rc.xRight = x + width;
               rc.yTop = y + height;

               WinSendMsg(psn->hwndBook, BKM_CALCPAGERECT, (MPARAM)&rc, (MPARAM)TRUE);

               _do_resize(pagebox, rc.xRight - rc.xLeft, rc.yTop - rc.yBottom);
            }
         }
         break;
      }
      break;
   case WM_CLOSE:
      if(result == -1)
      {
         dw_window_destroy(WinQueryWindow(hWnd, QW_PARENT));
         return (MRESULT)TRUE;
      }
      break;
   case WM_MOUSEMOVE:
      {
         HPOINTER pointer;

         if((pointer = (HPOINTER)dw_window_get_data(hWnd, "_dw_pointer")) ||
            (pointer = (HPOINTER)dw_window_get_data(_toplevel_window(hWnd), "_dw_pointer")))
         {
            WinSetPointer(HWND_DESKTOP, pointer);
            return MRFROMSHORT(TRUE);
         }
      }
      return MRFROMSHORT(FALSE);
   case WM_USER:
      windowfunc = (void (* API)(void *))mp1;

      if(windowfunc)
         windowfunc((void *)mp2);
      break;
   case WM_CHAR:
      if(SHORT1FROMMP(mp2) == '\t')
      {
         if(CHARMSG(&msg)->fs & KC_SHIFT)
            _shift_focus_back(hWnd);
         else
            _shift_focus(hWnd);
         return FALSE;
      }
      break;
   case WM_DESTROY:
      {
         HWND parent = WinQueryWindow(hWnd, QW_PARENT);

         /* Free memory before destroying */
         if(parent && WinWindowFromID(parent, FID_CLIENT) == hWnd)
            _free_window_memory(parent);
         else
            _free_window_memory(hWnd);
      }
      break;
   case WM_MENUEND:
      /* Delay removing the signal until we've executed
       * the signal handler.
       */
      WinPostMsg(hWnd, WM_USER+2, mp1, mp2);
      break;
   case WM_USER+2:
      _clear_emphasis();
      if(dw_window_get_data((HWND)mp2, "_dw_popup"))
         _free_menu_data((HWND)mp2);
      break;
   }

   if(result != -1)
      return (MRESULT)result;
   else
      return WinDefWindowProc(hWnd, msg, mp1, mp2);
}

void _changebox(Box *thisbox, int percent, int type)
{
   int z;

   for(z=0;z<thisbox->count;z++)
   {
      if(thisbox->items[z].type == TYPEBOX)
      {
         Box *tmp = WinQueryWindowPtr(thisbox->items[z].hwnd, QWP_USER);
         _changebox(tmp, percent, type);
      }
      else
      {
         if(type == DW_HORZ)
         {
            if(thisbox->items[z].hsize == SIZEEXPAND)
               thisbox->items[z].width = (int)(((float)thisbox->items[z].origwidth) * (((float)percent)/((float)100.0)));
         }
         else
         {
            if(thisbox->items[z].vsize == SIZEEXPAND)
               thisbox->items[z].height = (int)(((float)thisbox->items[z].origheight) * (((float)percent)/((float)100.0)));
         }
      }
   }
}

void _handle_splitbar_resize(HWND hwnd, float percent, int type, int x, int y)
{
   float ratio = (float)percent/(float)100.0;
   HWND handle1 = (HWND)dw_window_get_data(hwnd, "_dw_topleft");
   HWND handle2 = (HWND)dw_window_get_data(hwnd, "_dw_bottomright");
   Box *tmp = WinQueryWindowPtr(handle1, QWP_USER);

   WinShowWindow(handle1, FALSE);
   WinShowWindow(handle2, FALSE);

   if(type == DW_HORZ)
   {
      int newx = (int)((float)x * ratio) - (SPLITBAR_WIDTH/2);

      WinSetWindowPos(handle1, NULLHANDLE, 0, 0, newx, y, SWP_MOVE | SWP_SIZE);
      _do_resize(tmp, newx - 1, y - 1);

      dw_window_set_data(hwnd, "_dw_start", (void *)newx);

      tmp = WinQueryWindowPtr(handle2, QWP_USER);

      newx = x - newx - SPLITBAR_WIDTH;

      WinSetWindowPos(handle2, NULLHANDLE, x - newx, 0, newx, y, SWP_MOVE | SWP_SIZE);
      _do_resize(tmp, newx - 1, y - 1);
   }
   else
   {
      int newy = (int)((float)y * ratio) - (SPLITBAR_WIDTH/2);

      WinSetWindowPos(handle1, NULLHANDLE, 0, y - newy, x, newy, SWP_MOVE | SWP_SIZE);
      _do_resize(tmp, x - 1, newy - 1);

      tmp = WinQueryWindowPtr(handle2, QWP_USER);

      newy = y - newy - SPLITBAR_WIDTH;

      WinSetWindowPos(handle2, NULLHANDLE, 0, 0, x, newy, SWP_MOVE | SWP_SIZE);
      _do_resize(tmp, x - 1, newy - 1);

      dw_window_set_data(hwnd, "_dw_start", (void *)newy);
   }

   WinShowWindow(handle1, TRUE);
   WinShowWindow(handle2, TRUE);
}


/* This handles any activity on the splitbars (sizers) */
MRESULT EXPENTRY _splitwndproc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   switch (msg)
   {
   case WM_ACTIVATE:
   case WM_SETFOCUS:
      return (MRESULT)(FALSE);

   case WM_PAINT:
      {
         HPS hps;
         POINTL ptl[2];
         RECTL rcl;
         int type = (int)dw_window_get_data(hwnd, "_dw_type");
         int start = (int)dw_window_get_data(hwnd, "_dw_start");

         hps = WinBeginPaint(hwnd, 0, 0);

         WinQueryWindowRect(hwnd, &rcl);

         if(type == DW_HORZ)
         {
            ptl[0].x = rcl.xLeft + start;
            ptl[0].y = rcl.yBottom;
            ptl[1].x = rcl.xRight + start + 3;
            ptl[1].y = rcl.yTop;
         }
         else
         {
            ptl[0].x = rcl.xLeft;
            ptl[0].y = rcl.yBottom + start;
            ptl[1].x = rcl.xRight;
            ptl[1].y = rcl.yTop + start + 3;
         }


         GpiSetColor(hps, CLR_PALEGRAY);
         GpiMove(hps, &ptl[0]);
         GpiBox(hps, DRO_OUTLINEFILL, &ptl[1], 0, 0);
         WinEndPaint(hps);
      }
      return MRFROMSHORT(FALSE);

   case WM_MOUSEMOVE:
      {
         int type = (int)dw_window_get_data(hwnd, "_dw_type");

         if(type == DW_HORZ)
            WinSetPointer(HWND_DESKTOP,
                       WinQuerySysPointer(HWND_DESKTOP,
                                     SPTR_SIZEWE,
                                     FALSE));
         else
            WinSetPointer(HWND_DESKTOP,
                       WinQuerySysPointer(HWND_DESKTOP,
                                     SPTR_SIZENS,
                                     FALSE));
      }
      return MRFROMSHORT(FALSE);
   case WM_BUTTON1DOWN:
      {
         APIRET rc;
         RECTL  rclFrame;
         RECTL  rclBounds;
         float *percent = (float *)dw_window_get_data(hwnd, "_dw_percent");
         int type = (int)dw_window_get_data(hwnd, "_dw_type");
         int start = (int)dw_window_get_data(hwnd, "_dw_start");

         WinQueryWindowRect(hwnd, &rclFrame);
         WinQueryWindowRect(hwnd, &rclBounds);

         WinMapWindowPoints(hwnd, HWND_DESKTOP,
                        (PPOINTL)&rclBounds, 2);


         if(type == DW_HORZ)
         {
            rclFrame.xLeft = start;
            rclFrame.xRight = start + SPLITBAR_WIDTH;
         }
         else
         {
            rclFrame.yBottom = start;
            rclFrame.yTop = start + SPLITBAR_WIDTH;
         }

         if(percent)
         {
            rc = _TrackRectangle(hwnd, &rclFrame, &rclBounds);

            if(rc == TRUE)
            {
               int width = (rclBounds.xRight - rclBounds.xLeft);
               int height = (rclBounds.yTop - rclBounds.yBottom);

               if(type == DW_HORZ)
               {
                  start = rclFrame.xLeft - rclBounds.xLeft;
                  if(width - SPLITBAR_WIDTH > 1 && start < width - SPLITBAR_WIDTH)
                     *percent = ((float)start / (float)(width - SPLITBAR_WIDTH)) * 100.0;
               }
               else
               {
                  start = rclFrame.yBottom - rclBounds.yBottom;
                  if(height - SPLITBAR_WIDTH > 1 && start < height - SPLITBAR_WIDTH)
                     *percent = 100.0 - (((float)start / (float)(height - SPLITBAR_WIDTH)) * 100.0);
               }
               _handle_splitbar_resize(hwnd, *percent, type, width, height);
               _handle_splitbar_resize(hwnd, *percent, type, width, height);
            }
         }
      }
      return MRFROMSHORT(FALSE);
   }
   return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

/* Function: BubbleProc
 * Abstract: Subclass procedure for bubble help
 */
MRESULT EXPENTRY _BubbleProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   MRESULT res;
   PFNWP proc = (PFNWP)WinQueryWindowPtr(hwnd, QWL_USER);

   if(proc)
      res = proc(hwnd, msg, mp1, mp2);
   else
      res = WinDefWindowProc(hwnd, msg, mp1, mp2);

   if(msg == WM_PAINT)
   {
      POINTL ptl;
      HPS hpsTemp;
      RECTL rcl;
      int height, width;

      WinQueryWindowRect(hwnd, &rcl);
      height = rcl.yTop - rcl.yBottom - 1;
      width = rcl.xRight - rcl.xLeft - 1;

      /* Draw a border around the bubble help */
      hpsTemp = WinGetPS(hwnd);
      GpiSetColor(hpsTemp, CLR_BLACK);
      ptl.x = ptl.y = 0;
      GpiMove(hpsTemp, &ptl);
      ptl.x = 0;
      ptl.y = height;
      GpiLine(hpsTemp, &ptl);
      ptl.x = ptl.y = 0;
      GpiMove(hpsTemp, &ptl);
      ptl.y = 0;
      ptl.x = width;
      GpiLine(hpsTemp, &ptl);
      ptl.x = width;
      ptl.y = height;
      GpiMove(hpsTemp, &ptl);
      ptl.x = 0;
      ptl.y = height;
      GpiLine(hpsTemp, &ptl);
      ptl.x = width;
      ptl.y = height;
      GpiMove(hpsTemp, &ptl);
      ptl.y = 0;
      ptl.x = width;
      GpiLine(hpsTemp, &ptl);
      WinReleasePS(hpsTemp);
   }
   return res;
}

MRESULT EXPENTRY _button_draw(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2, PFNWP oldproc, int indent)
{
   HPIXMAP pixmap = (HPIXMAP)dw_window_get_data(hwnd, "_dw_hpixmap");
   HPIXMAP disable = (HPIXMAP)dw_window_get_data(hwnd, "_dw_hpixmap_disabled");
   HPOINTER icon = (HPOINTER)dw_window_get_data(hwnd, "_dw_button_icon");
   MRESULT res;
   unsigned long width, height;
   int x = 5, y = 5;

   dw_window_get_pos_size(hwnd, NULL, NULL, &width, &height);

   if(!oldproc)
      res = WinDefWindowProc(hwnd, msg, mp1, mp2);
   res = oldproc(hwnd, msg, mp1, mp2);

   if(icon)
   {
      ULONG halftone = DP_NORMAL;
      HPS hps = WinGetPS(hwnd);
      POINTERINFO pi;
      int cx, cy;

      if(dw_window_get_data(hwnd, "_dw_disabled"))
         halftone = DP_HALFTONED;

      cx = width - 10;
      cy = height - 10;

      if(WinQueryPointerInfo(icon, &pi))
      {
         BITMAPINFOHEADER sl;
         int newcx = cx, newcy = cy;

         /* Check the mini icon first */
         if(GpiQueryBitmapParameters(pi.hbmMiniColor, &sl))
         {
            if(sl.cx && sl.cy && cx > sl.cx && cy > sl.cy)
            {
               newcx = sl.cx;
               newcy = sl.cy;
            }
         }
         /* Check the normal icon second */
         if(GpiQueryBitmapParameters(pi.hbmColor, &sl))
         {
            if(sl.cx && sl.cy && cx > sl.cx && cy > sl.cy)
            {
               newcx = sl.cx;
               newcy = sl.cy;
            }
         }
         cx = newcx; cy = newcy;
         x = (width - cx)/2;
         y = (height - cy)/2;
      }
      WinStretchPointer(hps, x + indent, y - indent, cx, cy, icon, halftone);
      WinReleasePS(hps);
   }
   else if(pixmap)
   {
      x = (width - pixmap->width)/2;
      y = (height - pixmap->height)/2;

      if(disable && dw_window_get_data(hwnd, "_dw_disabled"))
         dw_pixmap_bitblt(hwnd, 0, x + indent, y + indent, pixmap->width, pixmap->height, 0, disable, 0, 0);
      else
         dw_pixmap_bitblt(hwnd, 0, x + indent, y + indent, pixmap->width, pixmap->height, 0, pixmap, 0, 0);
   }
   return res;
}

/* Function: BtProc
 * Abstract: Subclass procedure for buttons
 */

MRESULT EXPENTRY _BtProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   BubbleButton *bubble;
   PFNWP oldproc;

   bubble = (BubbleButton *)WinQueryWindowPtr(hwnd, QWL_USER);

   if(!bubble)
      return WinDefWindowProc(hwnd, msg, mp1, mp2);

   oldproc = bubble->pOldProc;

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hwnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_PAINT:
      return _button_draw(hwnd, msg, mp1, mp2, oldproc, 0);
   case BM_SETHILITE:
      return _button_draw(hwnd, msg, mp1, mp2, oldproc, (int)mp1);
   case WM_SETFOCUS:
      if(mp2)
         _run_event(hwnd, msg, mp1, mp2);
      else
         WinSendMsg(hwnd, BM_SETDEFAULT, 0, 0);
      break;
   case WM_BUTTON1DOWN:
   case WM_BUTTON2DOWN:
   case WM_BUTTON3DOWN:
   case WM_BUTTON1DBLCLK:
   case WM_BUTTON2DBLCLK:
   case WM_BUTTON3DBLCLK:
      if(dw_window_get_data(hwnd, "_dw_disabled"))
         return (MRESULT)FALSE;
      break;
   case WM_BUTTON1UP:
      {
         SignalHandler *tmp = Root;

         if(WinIsWindowEnabled(hwnd) && !dw_window_get_data(hwnd, "_dw_disabled"))
         {
            /* Find any callbacks for this function */
            while(tmp)
            {
               if(tmp->message == WM_COMMAND)
               {
                  /* Make sure it's the right window, and the right ID */
                  if(tmp->window == hwnd)
                  {
                     /* Due to the fact that if we run the function
                      * here, finishing actions on the button will occur
                      * after we run the signal handler.  So we post the
                      * message so the button can finish what it needs to
                      * do before we run our handler.
                      */
                     WinPostMsg(hwnd, WM_USER, (MPARAM)tmp, 0);
                     tmp = NULL;
                  }
               }
               if(tmp)
                  tmp= tmp->next;
            }
         }
      }
      break;
   case WM_USER:
      {
            SignalHandler *tmp = (SignalHandler *)mp1;
         int (* API clickfunc)(HWND, void *) = NULL;

         if(tmp)
         {
            clickfunc = (int (* API)(HWND, void *))tmp->signalfunction;

            clickfunc(tmp->window, tmp->data);
         }
      }
        break;
   case WM_CHAR:
      {
         /* A button press should also occur for an ENTER or SPACE press
          * while the button has the active input focus.
          */
         if(SHORT1FROMMP(mp2) == '\r' || SHORT1FROMMP(mp2) == ' ')
         {
            SignalHandler *tmp = Root;

            /* Find any callbacks for this function */
            while(tmp)
            {
               if(tmp->message == WM_COMMAND)
               {
                  /* Make sure it's the right window, and the right ID */
                  if(tmp->window == hwnd)
                  {
                     WinPostMsg(hwnd, WM_USER, (MPARAM)tmp, 0);
                     tmp = NULL;
                  }
               }
               if(tmp)
                  tmp= tmp->next;
            }
         }
         if(SHORT1FROMMP(mp2) == '\t')
         {
            if(CHARMSG(&msg)->fs & KC_SHIFT)
               _shift_focus_back(hwnd);
            else
               _shift_focus(hwnd);
            WinSendMsg(hwnd, BM_SETDEFAULT, 0, 0);
            return FALSE;
         }
         else if(!(CHARMSG(&msg)->fs & KC_KEYUP) && (CHARMSG(&msg)->vkey == VK_LEFT || CHARMSG(&msg)->vkey == VK_UP))
         {
            _shift_focus_back(hwnd);
            return FALSE;
         }
         else if(!(CHARMSG(&msg)->fs & KC_KEYUP) && (CHARMSG(&msg)->vkey == VK_RIGHT || CHARMSG(&msg)->vkey == VK_DOWN))
         {
            _shift_focus(hwnd);
            return FALSE;
         }
      }
      break;
   case 0x041f:
      if (hwndBubble)
      {
         WinDestroyWindow(hwndBubble);
         hwndBubble = 0;
      }
      break;

   case 0x041e:

      if(!*bubble->bubbletext)
         break;

      if(hwndBubble)
      {
         WinDestroyWindow(hwndBubble);
         hwndBubble = 0;
      }

      if(!hwndBubble)
      {
         HPS   hpsTemp = 0;
         LONG  lHight;
         LONG  lWidth;
         POINTL txtPointl[TXTBOX_COUNT];
         POINTL ptlWork = {0,0};
         ULONG ulColor = CLR_YELLOW;
         void *blah;

         hwndBubbleLast   = hwnd;
         hwndBubble = WinCreateWindow(HWND_DESKTOP,
                               WC_STATIC,
                               "",
                               SS_TEXT |
                               DT_CENTER |
                               DT_VCENTER,
                                         0,0,0,0,
                               HWND_DESKTOP,
                               HWND_TOP,
                               0,
                               NULL,
                               NULL);

         WinSetPresParam(hwndBubble,
                     PP_FONTNAMESIZE,
                     strlen(DefaultFont)+1,
                     DefaultFont);


         WinSetPresParam(hwndBubble,
                     PP_BACKGROUNDCOLORINDEX,
                     sizeof(ulColor),
                     &ulColor);

         WinSetWindowText(hwndBubble,
                      bubble->bubbletext);

         WinMapWindowPoints(hwnd, HWND_DESKTOP, &ptlWork, 1);

         hpsTemp = WinGetPS(hwndBubble);
         GpiQueryTextBox(hpsTemp,
                     strlen(bubble->bubbletext),
                     bubble->bubbletext,
                     TXTBOX_COUNT,
                     txtPointl);
         WinReleasePS(hpsTemp);

         lWidth = txtPointl[TXTBOX_TOPRIGHT].x -
            txtPointl[TXTBOX_TOPLEFT ].x + 8;

         lHight = txtPointl[TXTBOX_TOPLEFT].y -
            txtPointl[TXTBOX_BOTTOMLEFT].y + 8;

         ptlWork.y -= lHight;

         blah = (void *)WinSubclassWindow(hwndBubble, _BubbleProc);

         if(blah)
            WinSetWindowPtr(hwndBubble, QWP_USER, blah);

         WinSetWindowPos(hwndBubble,
                     HWND_TOP,
                     ptlWork.x,
                     ptlWork.y,
                     lWidth,
                     lHight,
                     SWP_SIZE | SWP_MOVE | SWP_SHOW);
      }
      break;
   }

   if(!oldproc)
      return WinDefWindowProc(hwnd, msg, mp1, mp2);
   return oldproc(hwnd, msg, mp1, mp2);
}

MRESULT EXPENTRY _RendProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   int res = 0;
   res = (int)_run_event(hwnd, msg, mp1, mp2);
   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hwnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_BUTTON1DOWN:
   case WM_BUTTON2DOWN:
   case WM_BUTTON3DOWN:
      if(res == -1)
         WinSetFocus(HWND_DESKTOP, hwnd);
      else if(res)
         return (MPARAM)TRUE;
   }
   return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

MRESULT EXPENTRY _TreeProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(hwnd, QWP_USER);
   PFNWP oldproc = 0;

   if(blah)
      oldproc = blah->oldproc;

   switch(msg)
   {
   case WM_MOUSEMOVE:
      if(_wndproc(hwnd, msg, mp1, mp2))
         return MPFROMSHORT(FALSE);
      break;
   case WM_PAINT:
      {
         HPS hps;
         RECTL rcl;
         POINTL ptl[2];

         if(oldproc)
            oldproc(hwnd, msg, mp1, mp2);

         hps = WinBeginPaint(hwnd, 0, 0);
         WinQueryWindowRect(hwnd, &rcl);
         ptl[0].x = rcl.xLeft + 1;
         ptl[0].y = rcl.yBottom + 1;
         ptl[1].x = rcl.xRight - 1;
         ptl[1].y = rcl.yTop - 1;

         GpiSetColor(hps, CLR_BLACK);
         GpiMove(hps, &ptl[0]);
         GpiBox(hps, DRO_OUTLINE, &ptl[1], 0, 0);
         WinEndPaint(hps);
      }
      return MRFROMSHORT(FALSE);
   case WM_SETFOCUS:
      _run_event(hwnd, msg, mp1, mp2);
      break;
   case WM_CHAR:
      if(SHORT1FROMMP(mp2) == '\t')
      {
         if(CHARMSG(&msg)->fs & KC_SHIFT)
            _shift_focus_back(hwnd);
         else
            _shift_focus(hwnd);
         return FALSE;
      }
      break;
   }

   _run_event(hwnd, msg, mp1, mp2);

   if(oldproc)
      return oldproc(hwnd, msg, mp1, mp2);

   return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

/*
 * Initializes the Dynamic Windows engine.
 * Parameters:
 *           newthread: True if this is the only thread.
 *                      False if there is already a message loop running.
 */
int API dw_init(int newthread, int argc, char *argv[])
{
   APIRET rc;
   char objnamebuf[300] = "";

   argc = argc; /* keep compiler happy */
   argv = argv; /* keep compiler happy */
   if(newthread)
   {
      dwhab = WinInitialize(0);
      dwhmq = WinCreateMsgQueue(dwhab, 0);
   }

   rc = WinRegisterClass(dwhab, ClassName, _wndproc, CS_SIZEREDRAW | CS_CLIPCHILDREN, 32);
   rc = WinRegisterClass(dwhab, SplitbarClassName, _splitwndproc, 0L, 32);

   /* Get the OS/2 version. */
   DosQuerySysInfo(QSV_VERSION_MAJOR, QSV_MS_COUNT,(void *)aulBuffer, 4*sizeof(ULONG));

   desktop = WinQueryDesktopWindow(dwhab, NULLHANDLE);

   if(!IS_WARP4())
      DefaultFont = "8.Helv";

   /* This is a window that hangs around as long as the
    * application does and handles menu messages.
    */
   hwndApp = dw_window_new(HWND_OBJECT, "", 0);
   DosLoadModule(objnamebuf, sizeof(objnamebuf), "WPCONFIG", &wpconfig);

   return rc;
}

/*
 * Runs a message loop for Dynamic Windows.
 */
void API dw_main(void)
{
   QMSG qmsg;

   _dwtid = dw_thread_id();

   while(WinGetMsg(dwhab, &qmsg, 0, 0, 0))
   {
      if(qmsg.msg == WM_TIMER && qmsg.hwnd == NULLHANDLE)
         _run_event(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
      WinDispatchMsg(dwhab, &qmsg);
   }

   WinDestroyMsgQueue(dwhmq);
   WinTerminate(dwhab);
}

/*
 * Runs a message loop for Dynamic Windows, for a period of milliseconds.
 * Parameters:
 *           milliseconds: Number of milliseconds to run the loop for.
 */
void API dw_main_sleep(int milliseconds)
{
   QMSG qmsg;
#ifdef __EMX__
   struct timeval tv, start;

   gettimeofday(&start, NULL);
   gettimeofday(&tv, NULL);

   while(((tv.tv_sec - start.tv_sec)*1000) + ((tv.tv_usec - start.tv_usec)/1000) <= milliseconds)
   {
      if(WinPeekMsg(dwhab, &qmsg, 0, 0, 0, PM_NOREMOVE))
      {
         WinGetMsg(dwhab, &qmsg, 0, 0, 0);
         if(qmsg.msg == WM_TIMER && qmsg.hwnd == NULLHANDLE)
            _run_event(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
         WinDispatchMsg(dwhab, &qmsg);
      }
      else
         DosSleep(1);
      gettimeofday(&tv, NULL);
   }
#else
   double start = (double)clock();

   while(((clock() - start) / (CLOCKS_PER_SEC/1000)) <= milliseconds)
   {
      if(WinPeekMsg(dwhab, &qmsg, 0, 0, 0, PM_NOREMOVE))
      {
         WinGetMsg(dwhab, &qmsg, 0, 0, 0);
         if(qmsg.msg == WM_TIMER && qmsg.hwnd == NULLHANDLE)
            _run_event(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
         WinDispatchMsg(dwhab, &qmsg);
      }
      else
         DosSleep(1);
   }
#endif
}

/*
 * Processes a single message iteration and returns.
 */
void API dw_main_iteration(void)
{
   QMSG qmsg;

   _dwtid = dw_thread_id();

   if(WinGetMsg(dwhab, &qmsg, 0, 0, 0))
   {
      if(qmsg.msg == WM_TIMER && qmsg.hwnd == NULLHANDLE)
         _run_event(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
      WinDispatchMsg(dwhab, &qmsg);
   }
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
 * Allocates and initializes a dialog struct.
 * Parameters:
 *           data: User defined data to be passed to functions.
 */
DWDialog * API dw_dialog_new(void *data)
{
   DWDialog *tmp = malloc(sizeof(DWDialog));

   tmp->eve = dw_event_new();
   dw_event_reset(tmp->eve);
   tmp->data = data;
   tmp->done = FALSE;
   tmp->result = NULL;

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
   QMSG qmsg;
   void *tmp;

   while (WinGetMsg(dwhab, &qmsg, 0, 0, 0))
   {
      if(qmsg.msg == WM_TIMER && qmsg.hwnd == NULLHANDLE)
         _run_event(qmsg.hwnd, qmsg.msg, qmsg.mp1, qmsg.mp2);
      WinDispatchMsg(dwhab, &qmsg);
      if(dialog->done)
         break;
   }
   dw_event_close(&dialog->eve);
   tmp = dialog->result;
   free(dialog);
   return tmp;
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
   va_list args;
   char outbuf[1024];
   int rc;

   va_start(args, format);
   vsprintf(outbuf, format, args);
   va_end(args);

   rc = WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, outbuf, title, 0, flags | MB_MOVEABLE);
   if(rc == MBID_OK)
      return DW_MB_RETURN_OK;
   else if(rc == MBID_YES)
      return DW_MB_RETURN_YES;
   else if(rc == MBID_NO)
      return DW_MB_RETURN_NO;
   else if(rc == MBID_CANCEL)
      return DW_MB_RETURN_CANCEL;
   else return 0;
}

/*
 * Makes the window topmost.
 * Parameters:
 *           handle: The window handle to make topmost.
 */
int API dw_window_raise(HWND handle)
{
   return WinSetWindowPos(handle, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
}

/*
 * Makes the window bottommost.
 * Parameters:
 *           handle: The window handle to make bottommost.
 */
int API dw_window_lower(HWND handle)
{
   return WinSetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_ZORDER);
}

/*
 * Makes the window visible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int API dw_window_show(HWND handle)
{
   int rc = WinSetWindowPos(handle, NULLHANDLE, 0, 0, 0, 0, SWP_SHOW);
   HSWITCH hswitch;
   SWCNTRL swcntrl;

   _fix_button_owner(_toplevel_window(handle), 0);
   WinSetFocus(HWND_DESKTOP, handle);
   _initial_focus(handle);

   /* If this window has a  switch list entry make sure it is visible */
   hswitch = WinQuerySwitchHandle(handle, 0);
   if(hswitch)
   {
      WinQuerySwitchEntry(hswitch, &swcntrl);
      swcntrl.uchVisibility = SWL_VISIBLE;
      WinChangeSwitchEntry(hswitch, &swcntrl);
   }
   if(WinWindowFromID(handle, FID_CLIENT))
   {
      WindowData *blah = WinQueryWindowPtr(handle, QWP_USER);

      if(blah && !(blah->flags & DW_OS2_NEW_WINDOW))
      {
         ULONG cx = dw_screen_width(), cy = dw_screen_height();
         int newx, newy, changed = 0;
         SWP swp;

         blah->flags |= DW_OS2_NEW_WINDOW;

         WinQueryWindowPos(handle, &swp);

         newx = swp.x;
         newy = swp.y;

         if((swp.x+swp.cx) > cx)
         {
            newx = (cx - swp.cx)/2;
            changed = 1;
         }
         if((swp.y+swp.cy) > cy)
         {
            newy = (cy - swp.cy)/2;
            changed = 1;
         }
         if(changed)
            WinSetWindowPos(handle, NULLHANDLE, newx, newy, 0, 0, SWP_MOVE);
      }
   }
   return rc;
}

/*
 * Minimizes or Iconifies a top-level window.
 * Parameters:
 *           handle: The window handle to minimize.
 */
int API dw_window_minimize(HWND handle)
{
   HWND hwndclient = WinWindowFromID(handle, FID_CLIENT);

   if(hwndclient)
   {
      Box *box = (Box *)WinQueryWindowPtr(hwndclient, QWP_USER);

      if(box)
      {
         if(!box->titlebar && box->hwndtitle)
            WinSetParent(box->hwndtitle, handle, FALSE);
      }
   }

   return WinSetWindowPos(handle, NULLHANDLE, 0, 0, 0, 0, SWP_MINIMIZE);
}

/*
 * Makes the window invisible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int API dw_window_hide(HWND handle)
{
   HSWITCH hswitch;
   SWCNTRL swcntrl;

   /* If this window has a  switch list entry make sure it is invisible */
   hswitch = WinQuerySwitchHandle(handle, 0);
   if(hswitch)
   {
      WinQuerySwitchEntry(hswitch, &swcntrl);
      swcntrl.uchVisibility = SWL_INVISIBLE;
      WinChangeSwitchEntry(hswitch, &swcntrl);
   }
   return WinShowWindow(handle, FALSE);
}

/*
 * Destroys a window and all of it's children.
 * Parameters:
 *           handle: The window handle to destroy.
 */
int API dw_window_destroy(HWND handle)
{
   HWND frame, menu, parent = WinQueryWindow(handle, QW_PARENT);
   Box *thisbox = WinQueryWindowPtr(parent, QWP_USER);

   if(!handle)
      return -1;

   frame = (HWND)dw_window_get_data(handle, "_dw_combo_box");

   if((menu = WinWindowFromID(handle, FID_MENU)) != NULLHANDLE)
      _free_menu_data(menu);

   if(parent != desktop && thisbox && thisbox->count)
   {
      int z, index = -1;
      Item *tmpitem, *thisitem = thisbox->items;

      for(z=0;z<thisbox->count;z++)
      {
         if(thisitem[z].hwnd == handle)
            index = z;
      }

      if(index == -1)
         return 0;

      tmpitem = malloc(sizeof(Item)*(thisbox->count-1));

      /* Copy all but the current entry to the new list */
      for(z=0;z<index;z++)
      {
         tmpitem[z] = thisitem[z];
      }
      for(z=index+1;z<thisbox->count;z++)
      {
         tmpitem[z-1] = thisitem[z];
      }

      thisbox->items = tmpitem;
      free(thisitem);
      thisbox->count--;
      _free_window_memory(frame ? frame : handle);
   }
   return WinDestroyWindow(frame ? frame : handle);
}

/* Causes entire window to be invalidated and redrawn.
 * Parameters:
 *           handle: Toplevel window handle to be redrawn.
 */
void API dw_window_redraw(HWND handle)
{
   HWND client = WinWindowFromID(handle, FID_CLIENT);
   HWND window = client ? client : handle;
   Box *mybox = (Box *)WinQueryWindowPtr(window, QWP_USER);

   _fix_button_owner(_toplevel_window(handle), 0);
   if(window && mybox)
   {
      unsigned long width, height;

      dw_window_get_pos_size(window, NULL, NULL, &width, &height);

      WinShowWindow(client ? mybox->items[0].hwnd : handle, FALSE);
      _do_resize(mybox, width, height);
      WinShowWindow(client ? mybox->items[0].hwnd : handle, TRUE);
   }
}

/*
 * Changes a window's parent to newparent.
 * Parameters:
 *           handle: The window handle to destroy.
 *           newparent: The window's new parent window.
 */
void API dw_window_reparent(HWND handle, HWND newparent)
{
   HWND blah = WinWindowFromID(newparent, FID_CLIENT);
   WinSetParent(handle, blah ? blah : newparent, TRUE);
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          fontname: Name and size of the font in the form "size.fontname"
 */
int API dw_window_set_font(HWND handle, char *fontname)
{
   return WinSetPresParam(handle, PP_FONTNAMESIZE, strlen(fontname)+1, fontname);
}

/* Internal version */
int _dw_window_set_color(HWND handle, ULONG fore, ULONG back)
{
   if((fore & DW_RGB_COLOR) == DW_RGB_COLOR)
   {
      RGB2 rgb2;

      rgb2.bBlue = DW_BLUE_VALUE(fore);
      rgb2.bGreen = DW_GREEN_VALUE(fore);
      rgb2.bRed = DW_RED_VALUE(fore);
      rgb2.fcOptions = 0;

      WinSetPresParam(handle, PP_FOREGROUNDCOLOR, sizeof(RGB2), &rgb2);

   }
   else if(fore != DW_CLR_DEFAULT)
   {
      fore = _internal_color(fore);

      WinSetPresParam(handle, PP_FOREGROUNDCOLORINDEX, sizeof(ULONG), &fore);
   }
   if((back & DW_RGB_COLOR) == DW_RGB_COLOR)
   {
      RGB2 rgb2;

      rgb2.bBlue = DW_BLUE_VALUE(back);
      rgb2.bGreen = DW_GREEN_VALUE(back);
      rgb2.bRed = DW_RED_VALUE(back);
      rgb2.fcOptions = 0;

      WinSetPresParam(handle, PP_BACKGROUNDCOLOR, sizeof(RGB2), &rgb2);
      return 0;
   }
   else if(back != DW_CLR_DEFAULT)
   {
      back = _internal_color(back);

      WinSetPresParam(handle, PP_BACKGROUNDCOLORINDEX, sizeof(ULONG), &back);
   }
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
   dw_window_set_data(handle, "_dw_fore", (void *)(fore+1));
   dw_window_set_data(handle, "_dw_back", (void *)(back+1));

   return _dw_window_set_color(handle, fore, back);
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          border: Size of the window border in pixels.
 */
int API dw_window_set_border(HWND handle, int border)
{
   WinSendMsg(handle, WM_SETBORDERSIZE, MPFROMSHORT(border), MPFROMSHORT(border));
   return 0;
}

/*
 * Captures the mouse input to this window.
 * Parameters:
 *       handle: Handle to receive mouse input.
 */
void API dw_window_capture(HWND handle)
{
   WinSetCapture(HWND_DESKTOP, handle);
}

/*
 * Releases previous mouse capture.
 */
void API dw_window_release(void)
{
   WinSetCapture(HWND_DESKTOP, NULLHANDLE);
}

/*
 * Tracks this window movement.
 * Parameters:
 *       handle: Handle to frame to be tracked.
 */
void API dw_window_track(HWND handle)
{
   WinSendMsg(handle, WM_TRACKFRAME, MPFROMSHORT(TF_MOVE), 0);
}

/*
 * Changes the appearance of the mouse pointer.
 * Parameters:
 *       handle: Handle to widget for which to change.
 *       cursortype: ID of the pointer you want.
 */
void API dw_window_set_pointer(HWND handle, int pointertype)
{
   HPOINTER pointer = pointertype < 65535 ?
      WinQuerySysPointer(HWND_DESKTOP, pointertype, FALSE)
      : (HPOINTER)pointertype;

   if(!pointertype)
      dw_window_set_data(handle, "_dw_pointer", 0);
   else
   {
      WinSetPointer(HWND_DESKTOP, pointer);

      if(handle != HWND_DESKTOP)
         dw_window_set_data(handle, "_dw_pointer", (void *)pointer);
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
   HWND hwndframe;
   Box *newbox = calloc(1, sizeof(Box));
   WindowData *blah = calloc(1, sizeof(WindowData));
   ULONG winStyle = 0L;

   newbox->pad = 0;
   newbox->type = DW_VERT;
   newbox->count = 0;

   flStyle |= FCF_NOBYTEALIGN;

   if(flStyle & DW_FCF_TITLEBAR)
      newbox->titlebar = 1;
   else
      flStyle |= FCF_TITLEBAR;

   if(!(flStyle & FCF_SHELLPOSITION))
      blah->flags |= DW_OS2_NEW_WINDOW;

   if(flStyle & WS_MAXIMIZED)
   {
      winStyle |= WS_MAXIMIZED;
      flStyle &= ~WS_MAXIMIZED;
   }
   if(flStyle & WS_MINIMIZED)
   {
      winStyle |= WS_MINIMIZED;
      flStyle &= ~WS_MINIMIZED;
   }

   hwndframe = WinCreateStdWindow(hwndOwner, winStyle, &flStyle, ClassName, title, 0L, NULLHANDLE, 0L, &newbox->hwnd);
   newbox->hwndtitle = WinWindowFromID(hwndframe, FID_TITLEBAR);
   if(!newbox->titlebar && newbox->hwndtitle)
      WinSetParent(newbox->hwndtitle, HWND_OBJECT, FALSE);
   blah->oldproc = WinSubclassWindow(hwndframe, _sizeproc);
   WinSetWindowPtr(hwndframe, QWP_USER, blah);
   WinSetWindowPtr(newbox->hwnd, QWP_USER, newbox);

   return hwndframe;
}

/*
 * Create a new Box to be packed.
 * Parameters:
 *       type: Either DW_VERT (vertical) or DW_HORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 */
HWND API dw_box_new(int type, int pad)
{
   Box *newbox = calloc(1, sizeof(Box));

   newbox->pad = pad;
   newbox->type = type;
   newbox->count = 0;
    newbox->grouphwnd = NULLHANDLE;

   newbox->hwnd = WinCreateWindow(HWND_OBJECT,
                           WC_FRAME,
                           NULL,
                           WS_VISIBLE | WS_CLIPCHILDREN |
                           FS_NOBYTEALIGN,
                           0,0,2000,1000,
                           NULLHANDLE,
                           HWND_TOP,
                           0L,
                           NULL,
                           NULL);

   newbox->oldproc = WinSubclassWindow(newbox->hwnd, _controlproc);
   WinSetWindowPtr(newbox->hwnd, QWP_USER, newbox);
   dw_window_set_color(newbox->hwnd, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
   return newbox->hwnd;
}

/*
 * INCOMPLETE
 * Create a new scroll Box to be packed.
 * Parameters:
 *       type: Either DW_VERT (vertical) or DW_HORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 */
HWND API dw_scrollbox_new(int type, int pad)
{
    return dw_box_new(type, pad);
}

int API dw_scrollbox_get_pos( HWND handle, int orient )
{
   return 0;
}

int API dw_scrollbox_get_range( HWND handle, int orient )
{
   return 0;
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
   Box *newbox = calloc(1, sizeof(Box));
   newbox->pad = pad;
   newbox->type = type;
   newbox->count = 0;

   newbox->hwnd = WinCreateWindow(HWND_OBJECT,
                           WC_FRAME,
                           NULL,
                           WS_VISIBLE |
                           FS_NOBYTEALIGN,
                           0,0,2000,1000,
                           NULLHANDLE,
                           HWND_TOP,
                           0L,
                           NULL,
                           NULL);

   newbox->grouphwnd = WinCreateWindow(newbox->hwnd,
                              WC_STATIC,
                              title,
                              WS_VISIBLE | SS_GROUPBOX |
                              WS_GROUP,
                              0,0,2000,1000,
                              NULLHANDLE,
                              HWND_TOP,
                              0L,
                              NULL,
                              NULL);

   WinSetWindowPtr(newbox->hwnd, QWP_USER, newbox);
   dw_window_set_color(newbox->hwnd, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
   dw_window_set_color(newbox->grouphwnd, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   dw_window_set_font(newbox->grouphwnd, DefaultFont);
   return newbox->hwnd;
}

/*
 * Create a new MDI Frame to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id or 0L.
 */
HWND API dw_mdi_new(unsigned long id)
{
   HWND hwndframe;
   ULONG back = CLR_DARKGRAY;

   hwndframe = WinCreateWindow(HWND_OBJECT,
                        WC_FRAME,
                        NULL,
                        WS_VISIBLE | WS_CLIPCHILDREN |
                        FS_NOBYTEALIGN,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   /* Make the MDI Client area the same color as Windows and Unix */
   WinSetPresParam(hwndframe, PP_BACKGROUNDCOLORINDEX, sizeof(ULONG), &back);
   return hwndframe;
}

/*
 * Create a bitmap object to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_bitmap_new(ULONG id)
{
   return WinCreateWindow(HWND_OBJECT,
                     WC_STATIC,
                     NULL,
                     WS_VISIBLE | SS_TEXT,
                     0,0,2000,1000,
                     NULLHANDLE,
                     HWND_TOP,
                     id,
                     NULL,
                     NULL);
}

/*
 * Create a notebook object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_notebook_new(ULONG id, int top)
{
   ULONG flags;
   HWND tmp;

   if(top)
      flags = BKS_MAJORTABTOP;
   else
      flags = BKS_MAJORTABBOTTOM;

   tmp = WinCreateWindow(HWND_OBJECT,
                    WC_NOTEBOOK,
                    NULL,
                    WS_VISIBLE |
#ifdef BKS_TABBEDDIALOG
                    BKS_TABBEDDIALOG |
#endif
                    flags,
                    0,0,2000,1000,
                    NULLHANDLE,
                    HWND_TOP,
                    id,
                    NULL,
                    NULL);

   /* Fix tab sizes on Warp 3 */
   if(!IS_WARP4())
   {
      /* best sizes to be determined by trial and error */
      WinSendMsg(tmp, BKM_SETDIMENSIONS,MPFROM2SHORT(102, 28), MPFROMSHORT( BKA_MAJORTAB));
   }

   dw_window_set_font(tmp, DefaultFont);
   return tmp;
}

/*
 * Create a menu object to be popped up.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HMENUI API dw_menu_new(ULONG id)
{
   HMENUI tmp = WinCreateWindow(HWND_OBJECT,
                         WC_MENU,
                         NULL,
                         WS_VISIBLE,
                         0,0,2000,1000,
                         NULLHANDLE,
                         HWND_TOP,
                         id,
                         NULL,
                         NULL);
   return tmp;
}

/*
 * Create a menubar on a window.
 * Parameters:
 *       location: Handle of a window frame to be attached to.
 */
HMENUI API dw_menubar_new(HWND location)
{
   HMENUI tmp = WinCreateWindow(location,
                         WC_MENU,
                         NULL,
                         WS_VISIBLE | MS_ACTIONBAR,
                         0,0,2000,1000,
                         location,
                         HWND_TOP,
                         FID_MENU,
                         NULL,
                         NULL);
   dw_window_set_data(tmp, "_dw_owner", (void *)location);
   dw_window_set_data(tmp, "_dw_menubar", (void *)location);
   return tmp;
}

/*
 * Destroys a menu created with dw_menubar_new or dw_menu_new.
 * Parameters:
 *       menu: Handle of a menu.
 */
void API dw_menu_destroy(HMENUI *menu)
{
   if(menu)
      WinDestroyWindow(*menu);
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
HWND API dw_menu_append_item(HMENUI menux, char *title, ULONG id, ULONG flags, int end, int check, HMENUI submenu)
{
   MENUITEM miSubMenu;
   char buffer[30];
   int is_checked, is_disabled;

   if ( !menux || id > 65536 )
      return NULLHANDLE;

   if ( end )
      miSubMenu.iPosition=MIT_END;
   else
      miSubMenu.iPosition=0;
   /*
    * Handle flags
   */
   miSubMenu.afAttribute = 0;
   is_checked = (flags & DW_MIS_CHECKED) ? 1 : 0;
   if ( is_checked )
      miSubMenu.afAttribute |= MIA_CHECKED;
   is_disabled = (flags & DW_MIS_DISABLED) ? 1 : 0;
   if ( is_disabled )
      miSubMenu.afAttribute |= MIA_DISABLED;

   if ( strlen( title ) == 0 )
      miSubMenu.afStyle = MIS_SEPARATOR;
   else
      miSubMenu.afStyle = MIS_TEXT;
   miSubMenu.id=id;
   miSubMenu.hwndSubMenu = submenu;
   miSubMenu.hItem=NULLHANDLE;

   WinSendMsg(menux, MM_INSERTITEM, MPFROMP(&miSubMenu), MPFROMP(title));

   sprintf(buffer, "_dw_id%d", (int)id);
   dw_window_set_data(hwndApp, buffer, (void *)menux);
   sprintf(buffer, "_dw_checkable%ld", id);
   dw_window_set_data( hwndApp, buffer, (void *)check );
   sprintf(buffer, "_dw_ischecked%ld", id);
   dw_window_set_data( hwndApp, buffer, (void *)is_checked );
   sprintf(buffer, "_dw_isdisabled%ld", id);
   dw_window_set_data( hwndApp, buffer, (void *)is_disabled );

   if ( submenu )
      dw_window_set_data(submenu, "_dw_owner", (void *)menux);
   return (HWND)id;
}

/*
 * Sets the state of a menu item check.
 * Deprecated; use dw_menu_item_set_state()
 * Parameters:
 *       menu: The handle the the existing menu.
 *       id: Menuitem id.
 *       check: TRUE for checked FALSE for not checked.
 */
void API dw_menu_item_set_check(HMENUI menux, unsigned long id, int check)
{
   if ( check )
      WinSendMsg(menux, MM_SETITEMATTR, MPFROM2SHORT(id, TRUE),MPFROM2SHORT(MIA_CHECKED, MIA_CHECKED));
   else
      WinSendMsg(menux, MM_SETITEMATTR, MPFROM2SHORT(id, TRUE),MPFROM2SHORT(MIA_CHECKED, 0));
}

/*
 * Sets the state of a menu item.
 * Parameters:
 *       menu: The handle to the existing menu.
 *       id: Menuitem id.
 *       flags: DW_MIS_ENABLED/DW_MIS_DISABLED
 *              DW_MIS_CHECKED/DW_MIS_UNCHECKED
 */
void API dw_menu_item_set_state( HMENUI menux, unsigned long id, unsigned long state)
{
   char buffer1[30],buffer2[30];
   int check;
   int disabled;
   USHORT fAttribute=0;

   sprintf( buffer1, "_dw_ischecked%ld", id );
   check = (int)dw_window_get_data( hwndApp, buffer1 );
   sprintf( buffer2, "_dw_isdisabled%ld", id );
   disabled = (int)dw_window_get_data( hwndApp, buffer2 );

   if ( (state & DW_MIS_CHECKED) || (state & DW_MIS_UNCHECKED) )
   {
      /*
       * If we are changing state of "checked" base our setting on the passed flag...
       */
      if ( state & DW_MIS_CHECKED )
      {
         fAttribute |= MIA_CHECKED;
         check = 1;
      }
      else
      {
         check = 0;
      }
   }
   else
   {
      /*
       * ...otherwise base our setting on the current "checked" state.
       */
      if ( check )
      {
         fAttribute |= MIA_CHECKED;
      }
   }
   if ( (state & DW_MIS_ENABLED) || (state & DW_MIS_DISABLED) )
   {
      if ( state & DW_MIS_DISABLED )
      {
         fAttribute |= MIA_DISABLED;
         disabled = 1;
      }
      else
      {
         disabled = 0;
      }
   }
   else
   {
      /*
       * ...otherwise base our setting on the current "disabled" state.
       */
      if ( disabled )
      {
         fAttribute |= MIA_DISABLED;
      }
   }
   WinSendMsg( menux, MM_SETITEMATTR, MPFROM2SHORT(id, TRUE), MPFROM2SHORT( MIA_CHECKED|MIA_DISABLED, fAttribute ) );
   /*
    * Keep our internal checked state consistent
    */
   dw_window_set_data( hwndApp, buffer1, (void *)check );
   dw_window_set_data( hwndApp, buffer2, (void *)disabled );
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
   if(menu)
   {
      popup = parent;
      dw_window_set_data(*menu, "_dw_popup", (void *)1);
      WinPopupMenu(HWND_DESKTOP, parent, *menu, x, dw_screen_height() - y, 0, PU_KEYBOARD | PU_MOUSEBUTTON1 | PU_VCONSTRAIN | PU_HCONSTRAIN);
   }
}

/*
 * Returns the current X and Y coordinates of the mouse pointer.
 * Parameters:
 *       x: Pointer to variable to store X coordinate.
 *       y: Pointer to variable to store Y coordinate.
 */
void API dw_pointer_query_pos(long *x, long *y)
{
   POINTL ptl;

   WinQueryPointerPos(HWND_DESKTOP, &ptl);
   if(x && y)
   {
      *x = ptl.x;
      *y = dw_screen_height() - ptl.y;
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
   WinSetPointerPos(HWND_DESKTOP, x, dw_screen_height() - y);
}


/*
 * Create a container object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_container_new(ULONG id, int multi)
{
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_CONTAINER,
                        NULL,
                        WS_VISIBLE | CCS_READONLY |
                        (multi ? CCS_EXTENDSEL : CCS_SINGLESEL) |
                        CCS_AUTOPOSITION,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id ? id : (GlobalID++),
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _TreeProc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_data(tmp, "_dw_container", (void *)1);
   return tmp;
}

/*
 * Create a tree object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND API dw_tree_new(ULONG id)
{
   CNRINFO cnrinfo;
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_CONTAINER,
                        NULL,
                        WS_VISIBLE | CCS_READONLY |
                        CCS_SINGLESEL | CCS_AUTOPOSITION,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id ? id : (GlobalID++),
                        NULL,
                        NULL);

   cnrinfo.flWindowAttr = CV_TREE | CA_TREELINE;
   cnrinfo.slBitmapOrIcon.cx = 16;
   cnrinfo.slBitmapOrIcon.cy = 16;
   cnrinfo.cyLineSpacing = 0;
   cnrinfo.cxTreeIndent = 16;
   cnrinfo.cxTreeLine = 1;

   WinSendMsg(tmp, CM_SETCNRINFO, &cnrinfo, MPFROMLONG(CMA_FLWINDOWATTR | CMA_SLBITMAPORICON |
                                          CMA_LINESPACING | CMA_CXTREEINDENT | CMA_CXTREELINE));
   blah->oldproc = WinSubclassWindow(tmp, _TreeProc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   return tmp;
}

/*
 * Create a new static text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_text_new(char *text, ULONG id)
{
   WindowData *blah = calloc(sizeof(WindowData), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_STATIC,
                        text,
                        WS_VISIBLE | SS_TEXT,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _textproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   return tmp;
}

/*
 * Create a new status text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_status_text_new(char *text, ULONG id)
{
   WindowData *blah = calloc(sizeof(WindowData), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_STATIC,
                        text,
                        WS_VISIBLE | SS_TEXT,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _statusproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   return tmp;
}

#ifndef MLS_LIMITVSCROLL
#define MLS_LIMITVSCROLL           0x00000080L
#endif

/*
 * Create a new Multiline Editbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_mle_new(ULONG id)
{
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_MLE,
                        "",
                        WS_VISIBLE |
                        MLS_BORDER | MLS_IGNORETAB |
                        MLS_READONLY | MLS_VSCROLL |
                        MLS_LIMITVSCROLL,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _mleproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   return tmp;
}

/*
 * Create a new Entryfield window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_entryfield_new(char *text, ULONG id)
{

   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_ENTRYFIELD,
                        text,
                        WS_VISIBLE | ES_MARGIN |
                        ES_AUTOSCROLL | WS_TABSTOP,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_WHITE);
   return tmp;
}

/*
 * Create a new Entryfield (password) window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_entryfield_password_new(char *text, ULONG id)
{
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_ENTRYFIELD,
                        text,
                        WS_VISIBLE | ES_MARGIN | ES_UNREADABLE |
                        ES_AUTOSCROLL | WS_TABSTOP,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_WHITE);
   return tmp;
}

/*
 * Create a new Combobox window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the combpbox widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_combobox_new(char *text, ULONG id)
{
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND frame = dw_box_new(DW_HORZ, 0);
   HWND tmp = WinCreateWindow(frame,
                        WC_COMBOBOX,
                        text,
                        WS_VISIBLE | CBS_DROPDOWN | WS_GROUP,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   HENUM henum = WinBeginEnumWindows(tmp);
   HWND child, last = NULLHANDLE;

   while((child = WinGetNextWindow(henum)) != NULLHANDLE)
   {
      WindowData *moreblah = calloc(1, sizeof(WindowData));
      moreblah->oldproc = WinSubclassWindow(child, _comboentryproc);
      WinSetWindowPtr(child, QWP_USER, moreblah);
      dw_window_set_color(child, DW_CLR_BLACK, DW_CLR_WHITE);
      last = child;
   }
   WinEndEnumWindows(henum);
   blah->oldproc = WinSubclassWindow(tmp, _comboproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_WHITE);
   dw_window_set_data(tmp, "_dw_comboentry", (void *)last);
   dw_window_set_data(tmp, "_dw_combo_box", (void *)frame);
   WinSetOwner(tmp, frame);
   return tmp;
}

/*
 * Create a new button window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_button_new(char *text, ULONG id)
{
   BubbleButton *bubble = calloc(sizeof(BubbleButton), 1);

   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_BUTTON,
                        text,
                        WS_VISIBLE,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);

   bubble->id = id;
   bubble->bubbletext[0] = '\0';
   bubble->pOldProc = WinSubclassWindow(tmp, _BtProc);

   WinSetWindowPtr(tmp, QWP_USER, bubble);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   return tmp;
}

/* Function: GenResIDStr
** Abstract: Generate string '#nnnn' for a given ID for using with Button
**           controls
*/

void _GenResIDStr(CHAR *buff, ULONG ulID)
{
   char *str;
   int  slen = 0;

   *buff++ = '#';

   str = buff;

   do
   {
      *str++ = (ulID % 10) + '0';
      ulID /= 10;
      slen++;
   }
   while(ulID);

   *str-- = 0;

   for(; str > buff; str--, buff++)
   {
      *buff ^= *str;
      *str  ^= *buff;
      *buff ^= *str;
   }
}


/*
 * Create a new bitmap button window (widget) to be packed.
 * Parameters:
 *       text: Bubble help text to be displayed.
 *       id: An ID of a bitmap in the resource file.
 */
HWND API dw_bitmapbutton_new(char *text, ULONG id)
{
   char idbuf[256], *name = NULL;
   HWND tmp;
   BubbleButton *bubble = calloc(sizeof(BubbleButton), 1);
   HPOINTER icon = WinLoadPointer(HWND_DESKTOP, 0L, id);

   if(!icon)
   {
      name = idbuf;
      _GenResIDStr(idbuf, id);
   }

   tmp = WinCreateWindow(HWND_OBJECT,
                    WC_BUTTON,
                    name,
                    WS_VISIBLE | BS_PUSHBUTTON |
                    BS_NOPOINTERFOCUS | BS_AUTOSIZE |
                    (icon ? 0 : BS_BITMAP),
                    0,0,2000,1000,
                    NULLHANDLE,
                    HWND_TOP,
                    id,
                    NULL,
                    NULL);

   bubble->id = id;
   strncpy(bubble->bubbletext, text, BUBBLE_HELP_MAX - 1);
   bubble->bubbletext[BUBBLE_HELP_MAX - 1] = '\0';
   bubble->pOldProc = WinSubclassWindow(tmp, _BtProc);

   WinSetWindowPtr(tmp, QWP_USER, bubble);

   if(icon)
      dw_window_set_data(tmp, "_dw_button_icon", (void *)icon);
   dw_window_set_data(tmp, "_dw_bitmapbutton", (void *)1);
   return tmp;
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
   BubbleButton *bubble = calloc(sizeof(BubbleButton), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_BUTTON,
                        "",
                        WS_VISIBLE | BS_PUSHBUTTON |
                        BS_AUTOSIZE | BS_NOPOINTERFOCUS,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   char *file = alloca(strlen(filename) + 5);
   HPIXMAP pixmap = NULL, disabled = NULL;
   HPOINTER icon = 0;

   if(file && (pixmap = calloc(1,sizeof(struct _hpixmap))))
   {
      int z, j, lim, len;
      LONG fore;

      strcpy(file, filename);

      /* check if we can read from this file (it exists and read permission) */
      if(access(file, 04) == 0)
      {
         len = strlen( file );
         if(len > 4)
         {
            if(stricmp(file + len - 4, ".ico") == 0)
               icon = WinLoadFileIcon(file, FALSE);
            else
               _load_bitmap_file(file, tmp, &pixmap->hbm, &pixmap->hdc, &pixmap->hps, &pixmap->width, &pixmap->height);
         }
      }
      else
      {
         /* Try with .ico extension first...*/
         strcat(file, ".ico");
         if(access(file, 04) == 0)
            icon = WinLoadFileIcon(file, FALSE);
         else
         {
            strcpy(file, filename);
            strcat(file, ".bmp");
            if(access(file, 04) == 0)
               _load_bitmap_file(file, tmp, &pixmap->hbm, &pixmap->hdc, &pixmap->hps, &pixmap->width, &pixmap->height);
         }
      }

      if(icon)
      {
         free(pixmap);
         pixmap = NULL;
      }
      else
      {
         /* Create a disabled style pixmap */
         disabled = dw_pixmap_new(tmp, pixmap->width, pixmap->height, dw_color_depth_get());
         dw_pixmap_bitblt(0, disabled, 0, 0, pixmap->width, pixmap->height, 0, pixmap, 0, 0);

         fore = _foreground;
         dw_color_foreground_set(DW_CLR_PALEGRAY);
         lim = pixmap->width/2;
         for(j=0;j<pixmap->height;j++)
         {
            int mod = j%2;

            for(z=0;z<lim;z++)
               dw_draw_point(0, disabled, (z*2)+mod, j);
         }
         _foreground = fore;
      }
   }

   bubble->id = id;
   strncpy(bubble->bubbletext, text, BUBBLE_HELP_MAX - 1);
   bubble->bubbletext[BUBBLE_HELP_MAX - 1] = '\0';
   bubble->pOldProc = WinSubclassWindow(tmp, _BtProc);

   WinSetWindowPtr(tmp, QWP_USER, bubble);

   if(icon)
      dw_window_set_data(tmp, "_dw_button_icon", (void *)icon);
   else
   {
      dw_window_set_data(tmp, "_dw_hpixmap", (void *)pixmap);
      dw_window_set_data(tmp, "_dw_hpixmap_disabled", (void *)disabled);
   }
   dw_window_set_data(tmp, "_dw_bitmapbutton", (void *)1);
   return tmp;
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
   FILE *fp;
   BubbleButton *bubble = calloc(sizeof(BubbleButton), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_BUTTON,
                        "",
                        WS_VISIBLE | BS_PUSHBUTTON |
                        BS_AUTOSIZE | BS_NOPOINTERFOCUS,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   char *file;
   HPIXMAP pixmap = NULL, disabled = NULL;
   HPOINTER icon = 0;

   if ( ( pixmap = calloc( 1, sizeof(struct _hpixmap) ) ) )
   {
      int z, j, lim;
      LONG fore;
      file = tmpnam( NULL );
      if ( file != NULL )
      {
         fp = fopen( file, "wb" );
         if ( fp != NULL )
         {
            fwrite( data, 1, len, fp );
            fclose( fp );
            if ( len > 1 && data[0] == 'B' && data[1] == 'M' ) /* first 2 chars of data is BM, then its a BMP */
            {
               _load_bitmap_file( file, tmp, &pixmap->hbm, &pixmap->hdc, &pixmap->hps, &pixmap->width, &pixmap->height );
            }
            else /* otherwise its assumed to be an ico */
            {
               icon = WinLoadFileIcon(file, FALSE);
            }
         }
         else
         {
            unlink( file );
            return 0;
         }
         unlink( file );
      }

      if ( icon )
      {
         free(pixmap);
         pixmap = NULL;
      }
      else
      {
         /* Create a disabled style pixmap */
         disabled = dw_pixmap_new(tmp, pixmap->width, pixmap->height, dw_color_depth_get());
         dw_pixmap_bitblt(0, disabled, 0, 0, pixmap->width, pixmap->height, 0, pixmap, 0, 0);

         fore = _foreground;
         dw_color_foreground_set(DW_CLR_PALEGRAY);
         lim = pixmap->width/2;
         for(j=0;j<pixmap->height;j++)
         {
            int mod = j%2;

            for(z=0;z<lim;z++)
               dw_draw_point(0, disabled, (z*2)+mod, j);
         }
         _foreground = fore;
      }
   }

   bubble->id = id;
   strncpy(bubble->bubbletext, text, BUBBLE_HELP_MAX - 1);
   bubble->bubbletext[BUBBLE_HELP_MAX - 1] = '\0';
   bubble->pOldProc = WinSubclassWindow(tmp, _BtProc);

   WinSetWindowPtr(tmp, QWP_USER, bubble);

   if(icon)
      dw_window_set_data(tmp, "_dw_button_icon", (void *)icon);
   else
   {
      dw_window_set_data(tmp, "_dw_hpixmap", (void *)pixmap);
      dw_window_set_data(tmp, "_dw_hpixmap_disabled", (void *)disabled);
   }
   dw_window_set_data(tmp, "_dw_bitmapbutton", (void *)1);
   return tmp;
}

/*
 * Create a new spinbutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_spinbutton_new(char *text, ULONG id)
{
   WindowData *blah = calloc(sizeof(WindowData), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_SPINBUTTON,
                        text,
                        WS_VISIBLE | SPBS_MASTER,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   HWND entry = _find_entryfield(tmp);
   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   blah = calloc(sizeof(WindowData), 1);
   blah->oldproc = WinSubclassWindow(entry, _spinentryproc);
   WinSetWindowPtr(entry, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(entry, DW_CLR_BLACK, DW_CLR_WHITE);
   return tmp;
}

/*
 * Create a new radiobutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_radiobutton_new(char *text, ULONG id)
{
   WindowData *blah = calloc(sizeof(WindowData), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_BUTTON,
                        text,
                        WS_VISIBLE |
                        BS_AUTORADIOBUTTON,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   return tmp;
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
   WindowData *blah = calloc(1, sizeof(WindowData));
   SLDCDATA sldcData = { 0, 0, 0, 0, 0 };
   HWND tmp;

   sldcData.cbSize = sizeof(SLDCDATA);
    sldcData.usScale1Increments = increments;

   tmp = WinCreateWindow(HWND_OBJECT,
                    WC_SLIDER,
                    "",
                    WS_VISIBLE | SLS_SNAPTOINCREMENT |
                    (vertical ? SLS_VERTICAL : SLS_HORIZONTAL),
                    0,0,2000,1000,
                    NULLHANDLE,
                    HWND_TOP,
                    id,
                    &sldcData,
                    NULL);

   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   return tmp;
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
   return WinCreateWindow(HWND_OBJECT,
                     WC_SCROLLBAR,
                     "",
                     WS_VISIBLE | SBS_AUTOTRACK |
                     (vertical ? SBS_VERT : SBS_HORZ),
                     0,0,2000,1000,
                     NULLHANDLE,
                     HWND_TOP,
                     id ? id : (GlobalID++),
                     NULL,
                     NULL);
}

/*
 * Create a new percent bar window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_percent_new(ULONG id)
{
   WindowData *blah = calloc(1, sizeof(WindowData));
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_SLIDER,
                        "",
                        WS_VISIBLE | SLS_READONLY
                        | SLS_RIBBONSTRIP,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _percentproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_disable(tmp);
   return tmp;
}

/*
 * Create a new checkbox window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_checkbox_new(char *text, ULONG id)
{
   BubbleButton *bubble = calloc(sizeof(BubbleButton), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_BUTTON,
                        text,
                        WS_VISIBLE | BS_AUTOCHECKBOX,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   bubble->id = id;
   bubble->bubbletext[0] = '\0';
   bubble->pOldProc = WinSubclassWindow(tmp, _BtProc);
   WinSetWindowPtr(tmp, QWP_USER, bubble);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
   return tmp;
}

/*
 * Create a new listbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id() or 0L.
 *       multi: Multiple select TRUE or FALSE.
 */
HWND API dw_listbox_new(ULONG id, int multi)
{
   WindowData *blah = calloc(sizeof(WindowData), 1);
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_LISTBOX,
                        NULL,
                        WS_VISIBLE | LS_NOADJUSTPOS |
                        (multi ? LS_MULTIPLESEL : 0),
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id ? id : (GlobalID++),
                        NULL,
                        NULL);
   blah->oldproc = WinSubclassWindow(tmp, _entryproc);
   WinSetWindowPtr(tmp, QWP_USER, blah);
   dw_window_set_font(tmp, DefaultFont);
   dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_WHITE);
   return tmp;
}

/*
 * Sets the icon used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon.
 */
void API dw_window_set_icon(HWND handle, HICN icon)
{
   HPOINTER hptr = icon < 65536 ? WinLoadPointer(HWND_DESKTOP,NULLHANDLE,icon) : (HPOINTER)icon;
   WinSendMsg(handle, WM_SETICON, (MPARAM)hptr, 0);
}

/* Internal function to load a bitmap from a file and return handles
 * to the bitmap, presentation space etc.
 */
int _load_bitmap_file(char *file, HWND handle, HBITMAP *hbm, HDC *hdc, HPS *hps, unsigned long *width, unsigned long *height)
{
   HFILE BitmapFileHandle = NULLHANDLE; /* handle for the file */
   ULONG OpenAction = 0;
   PBYTE BitmapFileBegin; /* pointer to the first byte of bitmap data  */
   FILESTATUS BitmapStatus;
   ULONG cbRead;
   PBITMAPFILEHEADER2 pBitmapFileHeader;
   PBITMAPINFOHEADER2 pBitmapInfoHeader;
   ULONG ScanLines, ulFlags;
   HPS hps1;
   HDC hdc1;
   SIZEL sizl = { 0, 0 };

   /* open bitmap file */
   DosOpen(file, &BitmapFileHandle, &OpenAction, 0L,
         FILE_ARCHIVED | FILE_NORMAL | FILE_READONLY,
         OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
         OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY |
         OPEN_FLAGS_NOINHERIT, 0L);

   if(!BitmapFileHandle)
      return 0;

   /* find out how big the file is  */
   DosQueryFileInfo(BitmapFileHandle, 1, &BitmapStatus,
                sizeof(BitmapStatus));

   /* allocate memory to load the bitmap */
   DosAllocMem((PPVOID)&BitmapFileBegin, (ULONG)BitmapStatus.cbFile,
            PAG_READ | PAG_WRITE | PAG_COMMIT);

   /* read bitmap file into memory buffer */
   DosRead(BitmapFileHandle, (PVOID)BitmapFileBegin,
         BitmapStatus.cbFile, &cbRead);

   /* access first bytes as bitmap header */
   pBitmapFileHeader = (PBITMAPFILEHEADER2)BitmapFileBegin;

   /* check if it's a valid bitmap data file */
   if((pBitmapFileHeader->usType != BFT_BITMAPARRAY) &&
      (pBitmapFileHeader->usType != BFT_BMAP))
   {
      /* free memory of bitmap file buffer */
      DosFreeMem(BitmapFileBegin);
      /* close the bitmap file */
      DosClose(BitmapFileHandle);
      return 0;
   }

   /* check if it's a file with multiple bitmaps */
   if(pBitmapFileHeader->usType == BFT_BITMAPARRAY)
   {
      /* we'll just use the first bitmap and ignore the others */
      pBitmapFileHeader = &(((PBITMAPARRAYFILEHEADER2)BitmapFileBegin)->bfh2);
   }

   /* set pointer to bitmap information block */
   pBitmapInfoHeader = &pBitmapFileHeader->bmp2;

   /* find out if it's the new 2.0 format or the old format */
   /* and query number of lines */
   if(pBitmapInfoHeader->cbFix == sizeof(BITMAPINFOHEADER))
   {
      *height = ScanLines = (ULONG)((PBITMAPINFOHEADER)pBitmapInfoHeader)->cy;
      *width = (ULONG)((PBITMAPINFOHEADER)pBitmapInfoHeader)->cx;
   }
   else
   {
      *height = ScanLines = pBitmapInfoHeader->cy;
      *width = pBitmapInfoHeader->cx;
   }

   /* now we need a presentation space, get it from static control */
   hps1 = WinGetPS(handle);

   hdc1    = GpiQueryDevice(hps1);
   ulFlags = GpiQueryPS(hps1, &sizl);

   *hdc = DevOpenDC(dwhab, OD_MEMORY, "*", 0L, NULL, hdc1);
   *hps = GpiCreatePS (dwhab, *hdc, &sizl, ulFlags | GPIA_ASSOC);

   /* create bitmap now using the parameters from the info block */
   *hbm = GpiCreateBitmap(*hps, pBitmapInfoHeader, 0L, NULL, NULL);

   /* select the new bitmap into presentation space */
   GpiSetBitmap(*hps, *hbm);

   /* now copy the bitmap data into the bitmap */
   GpiSetBitmapBits(*hps, 0L, ScanLines,
                BitmapFileBegin + pBitmapFileHeader->offBits,
                (PBITMAPINFO2)pBitmapInfoHeader);

   WinReleasePS(hps1);

   /* free memory of bitmap file buffer */
   DosFreeMem(BitmapFileBegin);
   /* close the bitmap file */
   DosClose(BitmapFileHandle);
   return 1;
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
void API dw_window_set_bitmap(HWND handle, unsigned long id, char *filename)
{
   HBITMAP hbm;
   HPS     hps;

   /* Destroy any old bitmap data */
   _free_bitmap(handle);

   /* If id is non-zero use the resource */
   if ( id )
   {
      hps = WinGetPS( handle );
      hbm = GpiLoadBitmap( hps, NULLHANDLE, id, 0, 0 );
   }
   else if ( filename )
   {
      HDC hdc;
      unsigned long width, height;
      char *file = alloca(strlen(filename) + 5);

      if(!file)
         return;

      strcpy(file, filename);

      /* check if we can read from this file (it exists and read permission) */
      if(access(file, 04) != 0)
      {
         /* Try with .bmp extention */
         strcat(file, ".bmp");
         if(access(file, 04) != 0)
            return;
      }

      if(!_load_bitmap_file(file, handle, &hbm, &hdc, &hps, &width, &height))
         return;

      dw_window_set_data(handle, "_dw_hps", (void *)hps);
      dw_window_set_data(handle, "_dw_hdc", (void *)hdc);
      dw_window_set_data(handle, "_dw_width", (void *)width);
      dw_window_set_data(handle, "_dw_height", (void *)height);
   }
   else
      return;

   WinSetWindowBits(handle,QWL_STYLE,SS_BITMAP,SS_BITMAP | 0x7f);
   WinSendMsg( handle, SM_SETHANDLE, MPFROMP(hbm), NULL );
   if ( id )
      WinReleasePS(hps);
   dw_window_set_data(handle, "_dw_bitmap", (void *)hbm);
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
   HBITMAP hbm;
   HPS     hps;
   HDC hdc;
   unsigned long width, height;
   char *file;
   FILE *fp;

   /* Destroy any old bitmap data */
   _free_bitmap(handle);

   /* If id is non-zero use the resource */
   if ( id )
   {
      hps = WinGetPS( handle );
      hbm = GpiLoadBitmap( hps, NULLHANDLE, id, 0, 0 );
   }
   else if ( data )
   {
      file = tmpnam( NULL );
      if ( file != NULL )
      {
         fp = fopen( file, "wb" );
         if ( fp != NULL )
         {
            fwrite( data, 1, len, fp );
            fclose( fp );
            if ( len > 1 && data[0] == 'B' && data[1] == 'M' ) /* first 2 chars of data is BM, then its a BMP */
               _load_bitmap_file(file, handle, &hbm, &hdc, &hps, &width, &height);
            else /* otherwise its assumed to be an ico */
            {
               /* con't use ICO ? */
               unlink( file );
               return;
            }
         }
         else
         {
            unlink( file );
            return;
         }
         unlink( file );
      }

      dw_window_set_data(handle, "_dw_hps", (void *)hps);
      dw_window_set_data(handle, "_dw_hdc", (void *)hdc);
      dw_window_set_data(handle, "_dw_width", (void *)width);
      dw_window_set_data(handle, "_dw_height", (void *)height);
   }
   else
      return;

   WinSetWindowBits(handle,QWL_STYLE,SS_BITMAP,SS_BITMAP | 0x7f);
   WinSendMsg( handle, SM_SETHANDLE, MPFROMP(hbm), NULL );
   if ( id )
      WinReleasePS(hps);
   dw_window_set_data(handle, "_dw_bitmap", (void *)hbm);
}

/*
 * Sets the text used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       text: The text associsated with a given window.
 */
void API dw_window_set_text(HWND handle, char *text)
{
   WinSetWindowText(handle, text);
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
   int len = WinQueryWindowTextLength(handle);
   char *tempbuf = calloc(1, len + 2);

   WinQueryWindowText(handle, len + 1, tempbuf);

   return tempbuf;
}

/*
 * Disables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void API dw_window_disable(HWND handle)
{
   char tmpbuf[100];

   if(dw_window_get_data(handle, "_dw_disabled"))
      return;

   WinQueryClassName(handle, 99, tmpbuf);
   dw_window_set_data(handle, "_dw_disabled", (void *)1);

   if(tmpbuf[0] == '#')
   {
      int val = atoi(&tmpbuf[1]);
      HWND hwnd;

      switch(val)
      {
      case 2:
      case 6:
      case 10:
      case 32:
      case 7:
         hwnd = _find_entryfield(handle);
         _dw_window_set_color(hwnd ? hwnd : handle, DW_CLR_BLACK, DW_CLR_PALEGRAY);
         dw_signal_connect(hwnd ? hwnd : handle, DW_SIGNAL_KEY_PRESS, DW_SIGNAL_FUNC(_null_key), (void *)100);
            if(val == 2)
            dw_signal_connect(handle, DW_SIGNAL_BUTTON_PRESS, DW_SIGNAL_FUNC(_null_key), (void *)100);
         if(hwnd)
            dw_window_set_data(hwnd, "_dw_disabled", (void *)1);
         return;
      case 3:
         if(dw_window_get_data(handle, "_dw_bitmapbutton") && !dw_window_get_data(handle, "_dw_hpixmap"))
            WinEnableWindow(handle, FALSE);
         else if(dw_window_get_data(handle, "_dw_bitmapbutton") && dw_window_get_data(handle, "_dw_hpixmap_disabled"))
            WinInvalidateRect(handle, NULL, FALSE);
         else
            _dw_window_set_color(handle, DW_CLR_DARKGRAY, DW_CLR_PALEGRAY);
         dw_signal_connect(handle, DW_SIGNAL_KEY_PRESS, DW_SIGNAL_FUNC(_null_key), (void *)100);
         dw_signal_connect(handle, DW_SIGNAL_BUTTON_PRESS, DW_SIGNAL_FUNC(_null_key), (void *)100);
         return;
      }
   }
   WinEnableWindow(handle, FALSE);
}

/*
 * Enables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void API dw_window_enable(HWND handle)
{
   ULONG fore = (ULONG)dw_window_get_data(handle, "_dw_fore");
   ULONG back = (ULONG)dw_window_get_data(handle, "_dw_back");
   HWND hwnd = _find_entryfield(handle);

   dw_window_set_data(handle, "_dw_disabled", 0);
   if(hwnd)
      dw_window_set_data(hwnd, "_dw_disabled", 0);
   if(fore && back)
      _dw_window_set_color(hwnd ? hwnd : handle, fore-1, back-1);
   dw_signal_disconnect_by_data(handle, (void *)100);
   WinEnableWindow(handle, TRUE);
   if(dw_window_get_data(handle, "_dw_bitmapbutton") && dw_window_get_data(handle, "_dw_hpixmap_disabled"))
      WinInvalidateRect(handle, NULL, FALSE);
}

/*
 * Gets the child window handle with specified ID.
 * Parameters:
 *       handle: Handle to the parent window.
 *       id: Integer ID of the child.
 */
HWND API dw_window_from_id(HWND handle, int id)
{
   HENUM henum;
   HWND child;
   char tmpbuf[100];

   henum = WinBeginEnumWindows(handle);
   while((child = WinGetNextWindow(henum)) != NULLHANDLE)
   {
      int windowid = WinQueryWindowUShort(child, QWS_ID);
      HWND found;

      WinQueryClassName(child, 99, tmpbuf);

      /* If the child is a box (frame) then recurse into it */
      if(strncmp(tmpbuf, "#1", 3)==0)
         if((found = dw_window_from_id(child, id)) != NULLHANDLE)
            return found;

      if(windowid && windowid == id)
      {
         WinEndEnumWindows(henum);
         return child;
      }
   }
   WinEndEnumWindows(henum);
   return NULLHANDLE;
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
   char *funcname = "dw_box_pack_end()";

      /*
       * If you try and pack an item into itself VERY bad things can happen; like at least an
       * infinite loop on GTK! Lets be safe!
       */
   if(box == item)
   {
      dw_messagebox(funcname, DW_MB_OK|DW_MB_ERROR, "Danger! Danger! Will Robinson; box and item are the same!");
      return;
   }

   if(WinWindowFromID(box, FID_CLIENT))
   {
      box = WinWindowFromID(box, FID_CLIENT);
      hsize = TRUE;
      vsize = TRUE;
   }
   _dw_box_pack_end(box, item, width, height, hsize, vsize, pad, funcname);
}

void _dw_box_pack_end(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad, char *functionname)
{
   Box *thisbox = WinQueryWindowPtr(box, QWP_USER);

   if(thisbox)
   {
      int z;
      Item *tmpitem, *thisitem = thisbox->items;
      char tmpbuf[100];
      HWND frame = (HWND)dw_window_get_data(item, "_dw_combo_box");

      tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

      for(z=0;z<thisbox->count;z++)
      {
         tmpitem[z+1] = thisitem[z];
      }

      WinQueryClassName(item, 99, tmpbuf);

      if(vsize && !height)
         height = 1;
      if(hsize && !width)
         width = 1;

      if(strncmp(tmpbuf, "#1", 3)==0)
         tmpitem[0].type = TYPEBOX;
      else
      {
         if ( width == 0 && hsize == FALSE )
            dw_messagebox(functionname, DW_MB_OK|DW_MB_ERROR, "Width and expand Horizonal both unset for box: %x item: %x",box,item);
         if ( height == 0 && vsize == FALSE )
            dw_messagebox(functionname, DW_MB_OK|DW_MB_ERROR, "Height and expand Vertical both unset for box: %x item: %x",box,item);

         tmpitem[0].type = TYPEITEM;
      }

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

      if(thisbox->count)
         free(thisitem);

      thisbox->count++;

      WinQueryClassName(item, 99, tmpbuf);
      /* Don't set the ownership if it's an entryfield or spinbutton */
      if(strncmp(tmpbuf, "#6", 3)!=0 && strncmp(tmpbuf, "#32", 4)!=0 && strncmp(tmpbuf, "#2", 3)!=0)
         WinSetOwner(item, box);
      WinSetParent(frame ? frame : item, box, FALSE);
   }
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
   WinSetWindowPos(handle, NULLHANDLE, 0, 0, width, height, SWP_SHOW | SWP_SIZE);
}

/*
 * Returns the width of the screen.
 */
int API dw_screen_width(void)
{
   return WinQuerySysValue(HWND_DESKTOP,SV_CXSCREEN);
}

/*
 * Returns the height of the screen.
 */
int API dw_screen_height(void)
{
   return WinQuerySysValue(HWND_DESKTOP,SV_CYSCREEN);
}

/* This should return the current color depth */
unsigned long API dw_color_depth_get(void)
{
   HDC hdc = WinOpenWindowDC(HWND_DESKTOP);
   long colors;

   DevQueryCaps(hdc, CAPS_COLOR_BITCOUNT, 1, &colors);
   DevCloseDC(hdc);
   return colors;
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
   int myy = _get_frame_height(handle) - (y + _get_height(handle));

   WinSetWindowPos(handle, NULLHANDLE, x, myy, 0, 0, SWP_MOVE);
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
   int myy = _get_frame_height(handle) - (y + height);

   WinSetWindowPos(handle, NULLHANDLE, x, myy, width, height, SWP_MOVE | SWP_SIZE | SWP_SHOW);
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
   SWP swp;
   WinQueryWindowPos(handle, &swp);
   if(x)
      *x = swp.x;
   if(y)
      *y = _get_frame_height(handle) - (swp.y + swp.cy);
   if(width)
      *width = swp.cx;
   if(height)
      *height = swp.cy;
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
   WinSetWindowBits(handle, QWL_STYLE, style, mask);
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
   return (ULONG)WinSendMsg(handle, BKM_INSERTPAGE, 0L,
                      MPFROM2SHORT((BKA_STATUSTEXTON | BKA_AUTOPAGESIZE | BKA_MAJOR | flags), front ? BKA_FIRST : BKA_LAST));
}

/*
 * Remove a page from a notebook.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be destroyed.
 */
void API dw_notebook_page_destroy(HWND handle, unsigned int pageid)
{
   WinSendMsg(handle, BKM_DELETEPAGE,
            MPFROMLONG(pageid),  (MPARAM)BKA_SINGLE);
}

/*
 * Queries the currently visible page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 */
unsigned long API dw_notebook_page_get(HWND handle)
{
   return (unsigned long)WinSendMsg(handle, BKM_QUERYPAGEID,0L, MPFROM2SHORT(BKA_TOP, BKA_MAJOR));
}

/*
 * Sets the currently visibale page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be made visible.
 */
void API dw_notebook_page_set(HWND handle, unsigned int pageid)
{
   WinSendMsg(handle, BKM_TURNTOPAGE, MPFROMLONG(pageid), 0L);
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
   WinSendMsg(handle, BKM_SETTABTEXT,
            MPFROMLONG(pageid),  MPFROMP(text));
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
   WinSendMsg(handle, BKM_SETSTATUSLINETEXT,
            MPFROMLONG(pageid),  MPFROMP(text));
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
   HWND tmpbox = dw_box_new(DW_VERT, 0);

   dw_box_pack_start(tmpbox, page, 0, 0, TRUE, TRUE, 0);
   WinSubclassWindow(tmpbox, _wndproc);
   WinSendMsg(handle, BKM_SETPAGEWINDOWHWND,
            MPFROMLONG(pageid),  MPFROMHWND(tmpbox));
}

/*
 * Appends the specified text to the listbox's (or combobox) entry list.
 * Parameters:
 *          handle: Handle to the listbox to be appended to.
 *          text: Text to append into listbox.
 */
void API dw_listbox_append(HWND handle, char *text)
{
   WinSendMsg(handle,
            LM_INSERTITEM,
            MPFROMSHORT(LIT_END),
            MPFROMP(text));
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
   WinSendMsg(handle,
            LM_INSERTITEM,
            MPFROMSHORT(pos),
            MPFROMP(text));
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
   int i;
   for(i=0;i<count;i++)
      WinSendMsg(handle,
               LM_INSERTITEM,
               MPFROMSHORT(LIT_END),
               MPFROMP(text[i]));
}

/*
 * Clears the listbox's (or combobox) list of all entries.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
void API dw_listbox_clear(HWND handle)
{
   WinSendMsg(handle,
            LM_DELETEALL, 0L, 0L);
}

/*
 * Returns the listbox's item count.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
int API dw_listbox_count(HWND handle)
{
   return (int)WinSendMsg(handle,
                     LM_QUERYITEMCOUNT,0L, 0L);
}

/*
 * Sets the topmost item in the viewport.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 *          top: Index to the top item.
 */
void API dw_listbox_set_top(HWND handle, int top)
{
   WinSendMsg(handle,
            LM_SETTOPINDEX,
            MPFROMSHORT(top),
            0L);
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
   WinSendMsg(handle, LM_QUERYITEMTEXT, MPFROM2SHORT(index, length), (MPARAM)buffer);
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
   WinSendMsg(handle, LM_SETITEMTEXT, MPFROMSHORT(index), (MPARAM)buffer);
}

/*
 * Returns the index to the item in the list currently selected.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 */
unsigned int API dw_listbox_selected(HWND handle)
{
      return (unsigned int)WinSendMsg(handle,
                              LM_QUERYSELECTION,
                              MPFROMSHORT(LIT_CURSOR),
                              0);
}

/*
 * Returns the index to the current selected item or -1 when done.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          where: Either the previous return or -1 to restart.
 */
int API dw_listbox_selected_multi(HWND handle, int where)
{
   int place = where;

   if(where == -1)
      place = LIT_FIRST;

   place = (int)WinSendMsg(handle,
                     LM_QUERYSELECTION,
                     MPFROMSHORT(place),0L);
   if(place == LIT_NONE)
      return -1;
   return place;
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
   char tmpbuf[100];

   WinSendMsg(handle, LM_SELECTITEM, MPFROMSHORT(index), (MPARAM)state);

   WinQueryClassName(handle, 99, tmpbuf);

   /* If we are setting a combobox call the event handler manually */
   if(strncmp(tmpbuf, "#6", 3)==0)
      _run_event(handle, WM_CONTROL, MPFROM2SHORT(0, LN_SELECT), (MPARAM)handle);
}

/*
 * Deletes the item with given index from the list.
 * Parameters:
 *          handle: Handle to the listbox to be set.
 *          index: Item index.
 */
void API dw_listbox_delete(HWND handle, int index)
{
   WinSendMsg(handle, LM_DELETEITEM, MPFROMSHORT(index), 0);
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
   unsigned long point = startpoint;
   PBYTE mlebuf;

   /* Work around 64K limit */
   if(!DosAllocMem((PPVOID) &mlebuf, 65536, PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_TILE))
   {
      int amount, len = strlen(buffer), written = 0;

      while(written < len)
      {
         if((len - written) > 65535)
            amount = 65535;
         else
            amount = len - written;

         memcpy(mlebuf, &buffer[written], amount);
         mlebuf[amount] = '\0';

         WinSendMsg(handle, MLM_SETIMPORTEXPORT, MPFROMP(mlebuf), MPFROMLONG(amount+1));
         WinSendMsg(handle, MLM_IMPORT, MPFROMP(&point), MPFROMLONG(amount + 1));
         dw_mle_delete(handle, point, 1);

         written += amount;
      }
      DosFreeMem(mlebuf);
   }
   return point - 1;
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
   PBYTE mlebuf;

   /* Work around 64K limit */
   if(!DosAllocMem((PPVOID) &mlebuf, 65535, PAG_COMMIT | PAG_READ | PAG_WRITE | OBJ_TILE))
   {
      int amount, copied, written = 0;

      while(written < length)
      {
         if((length - written) > 65535)
            amount = 65535;
         else
            amount = length - written;

         WinSendMsg(handle, MLM_SETIMPORTEXPORT, MPFROMP(mlebuf), MPFROMLONG(amount));
         copied = (int)WinSendMsg(handle, MLM_EXPORT, MPFROMP(&startpoint), MPFROMLONG(&amount));

         if(copied)
         {
            memcpy(&buffer[written], mlebuf, copied);

            written += copied;
         }
         else
            break;
      }
      DosFreeMem(mlebuf);
   }
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
   if(bytes)
      *bytes = (unsigned long)WinSendMsg(handle, MLM_QUERYTEXTLENGTH, 0, 0);
   if(lines)
      *lines = (unsigned long)WinSendMsg(handle, MLM_QUERYLINECOUNT, 0, 0);
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
   char *buf = malloc(length+1);
   int z, dellen = length;

   dw_mle_export(handle, buf, startpoint, length);

   for(z=0;z<length-1;z++)
   {
      if(strncmp(&buf[z], "\r\n", 2) == 0)
         dellen--;
   }
   WinSendMsg(handle, MLM_DELETE, MPFROMLONG(startpoint), MPFROMLONG(dellen));
   free(buf);
}

/*
 * Clears all text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be cleared.
 */
void API dw_mle_clear(HWND handle)
{
   unsigned long bytes;

   dw_mle_get_size(handle, &bytes, NULL);

   WinSendMsg(handle, MLM_DELETE, MPFROMLONG(0), MPFROMLONG(bytes));
}

/*
 * Sets the visible line of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be positioned.
 *          line: Line to be visible.
 */
void API dw_mle_set_visible(HWND handle, int line)
{
   int tmppnt = (int)WinSendMsg(handle, MLM_CHARFROMLINE, MPFROMLONG(line), 0);
   WinSendMsg(handle, MLM_SETSEL, MPFROMLONG(tmppnt), MPFROMLONG(tmppnt));
}

/*
 * Sets the editablity of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it can be edited, FALSE for readonly.
 */
void API dw_mle_set_editable(HWND handle, int state)
{
   WinSendMsg(handle, MLM_SETREADONLY, MPFROMLONG(state ? FALSE : TRUE), 0);
}

/*
 * Sets the word wrap state of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it wraps, FALSE if it doesn't.
 */
void API dw_mle_set_word_wrap(HWND handle, int state)
{
   WinSendMsg(handle, MLM_SETWRAP, MPFROMLONG(state), 0);
}

/*
 * Sets the current cursor position of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be positioned.
 *          point: Point to position cursor.
 */
void API dw_mle_set_cursor(HWND handle, int point)
{
   WinSendMsg(handle, MLM_SETSEL, MPFROMLONG(point), MPFROMLONG(point));
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
   MLE_SEARCHDATA msd;

   /* This code breaks with structure packing set to 1 (/Sp1 in VAC)
    * if this is needed we need to add a pragma here.
    */
   msd.cb = sizeof(msd);
   msd.pchFind = text;
   msd.pchReplace = NULL;
   msd.cchFind = strlen(text);
   msd.cchReplace = 0;
   msd.iptStart = point;
   msd.iptStop = -1;

   if(WinSendMsg(handle, MLM_SEARCH, MPFROMLONG(MLFSEARCH_SELECTMATCH | flags), (MPARAM)&msd))
      return (int)WinSendMsg(handle, MLM_QUERYSEL,(MPARAM)MLFQS_MAXSEL, 0);
   return 0;
}

/*
 * Stops redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to freeze.
 */
void API dw_mle_freeze(HWND handle)
{
   WinSendMsg(handle, MLM_DISABLEREFRESH, 0, 0);
}

/*
 * Resumes redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to thaw.
 */
void API dw_mle_thaw(HWND handle)
{
   WinSendMsg(handle, MLM_ENABLEREFRESH, 0, 0);
}

/*
 * Sets the percent bar position.
 * Parameters:
 *          handle: Handle to the percent bar to be set.
 *          position: Position of the percent bar withing the range.
 */
void API dw_percent_set_pos(HWND handle, unsigned int position)
{
   int range = _dw_percent_get_range(handle);
   int mypos = ((float)position/100)*range;

   if(range)
   {
      _dw_int_set(handle, mypos);
      WinSendMsg(handle, SLM_SETSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION,SMA_RANGEVALUE), (MPARAM)mypos);
   }
}

/*
 * Returns the position of the slider.
 * Parameters:
 *          handle: Handle to the slider to be queried.
 */
unsigned int API dw_slider_get_pos(HWND handle)
{
   return (unsigned int)WinSendMsg(handle, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), 0);
}

/*
 * Sets the slider position.
 * Parameters:
 *          handle: Handle to the slider to be set.
 *          position: Position of the slider withing the range.
 */
void API dw_slider_set_pos(HWND handle, unsigned int position)
{
   dw_window_set_data(handle, "_dw_slider_value", (void *)position);
   WinSendMsg(handle, SLM_SETSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), (MPARAM)position);
}

/*
 * Returns the position of the scrollbar.
 * Parameters:
 *          handle: Handle to the scrollbar to be queried.
 */
unsigned int API dw_scrollbar_get_pos(HWND handle)
{
   return (unsigned int)WinSendMsg(handle, SBM_QUERYPOS, 0, 0);
}

/*
 * Sets the scrollbar position.
 * Parameters:
 *          handle: Handle to the scrollbar to be set.
 *          position: Position of the scrollbar withing the range.
 */
void API dw_scrollbar_set_pos(HWND handle, unsigned int position)
{
   dw_window_set_data(handle, "_dw_scrollbar_value", (void *)position);
   WinSendMsg(handle, SBM_SETPOS, (MPARAM)position, 0);
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
   unsigned int pos = (unsigned int)dw_window_get_data(handle, "_dw_scrollbar_value");
   WinSendMsg(handle, SBM_SETSCROLLBAR, (MPARAM)pos, MPFROM2SHORT(0, (unsigned short)range - visible));
   WinSendMsg(handle, SBM_SETTHUMBSIZE, MPFROM2SHORT((unsigned short)visible, range), 0);
   dw_window_set_data(handle, "_dw_scrollbar_visible", (void *)visible);
}

/*
 * Sets the spinbutton value.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          position: Current value of the spinbutton.
 */
void API dw_spinbutton_set_pos(HWND handle, long position)
{
   WinSendMsg(handle, SPBM_SETCURRENTVALUE, MPFROMLONG((long)position), 0L);
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
   WinSendMsg(handle, SPBM_SETLIMITS, MPFROMLONG(upper), MPFROMLONG(lower));
}

/*
 * Sets the entryfield character limit.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          limit: Number of characters the entryfield will take.
 */
void API dw_entryfield_set_limit(HWND handle, ULONG limit)
{
   WinSendMsg(handle, EM_SETTEXTLIMIT, (MPARAM)limit, (MPARAM)0);
}


/*
 * Returns the current value of the spinbutton.
 * Parameters:
 *          handle: Handle to the spinbutton to be queried.
 */
long API dw_spinbutton_get_pos(HWND handle)
{
   long tmpval = 0L;

   WinSendMsg(handle, SPBM_QUERYVALUE, (MPARAM)&tmpval,0L);
    return tmpval;
}

/*
 * Returns the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 */
int API dw_checkbox_get(HWND handle)
{
   return (int)WinSendMsg(handle,BM_QUERYCHECK,0,0);
}

/*
 * Sets the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 *          value: TRUE for checked, FALSE for unchecked.
 */
void API dw_checkbox_set(HWND handle, int value)
{
   WinSendMsg(handle,BM_SETCHECK,MPFROMSHORT(value),0);
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
   ULONG        cbExtra;
   PCNRITEM     pci;
   RECORDINSERT ri;

   if(!item)
      item = (HTREEITEM)CMA_FIRST;

   /* Calculate extra bytes needed for each record besides that needed for the
    * MINIRECORDCORE structure
    */

   cbExtra = sizeof(CNRITEM) - sizeof(MINIRECORDCORE);

   /* Allocate memory for the parent record */

   if((pci = (PCNRITEM)_dw_send_msg(handle, CM_ALLOCRECORD, MPFROMLONG(cbExtra), MPFROMSHORT(1), 0)) == 0)
      return 0;

   /* Fill in the parent record data */

   pci->rc.cb          = sizeof(MINIRECORDCORE);
   pci->rc.pszIcon     = strdup(title);
   pci->rc.hptrIcon    = icon;

   pci->hptrIcon       = icon;
   pci->user           = itemdata;
   pci->parent         = parent;

   memset(&ri, 0, sizeof(RECORDINSERT));

   ri.cb                 = sizeof(RECORDINSERT);
   ri.pRecordOrder       = (PRECORDCORE)item;
   ri.zOrder             = (USHORT)CMA_TOP;
   ri.cRecordsInsert     = 1;
   ri.fInvalidateRecord  = TRUE;

   /* We are about to insert the child records. Set the parent record to be
    * the one we just inserted.
    */
   ri.pRecordParent = (PRECORDCORE)parent;

   /* Insert the record */
   WinSendMsg(handle, CM_INSERTRECORD, MPFROMP(pci), MPFROMP(&ri));

   return (HTREEITEM)pci;
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
   return dw_tree_insert_after(handle, (HTREEITEM)CMA_END, title, icon, parent, itemdata);
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
   PCNRITEM pci = (PCNRITEM)item;

   if(!pci)
      return;

   if(pci->rc.pszIcon)
      free(pci->rc.pszIcon);

   pci->rc.pszIcon     = strdup(title);
   pci->rc.hptrIcon    = icon;

   pci->hptrIcon       = icon;

   WinSendMsg(handle, CM_INVALIDATERECORD, (MPARAM)&pci, MPFROM2SHORT(1, CMA_TEXTCHANGED));
}

/*
 * Gets the text an item in a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 */
char * API dw_tree_get_title(HWND handle, HTREEITEM item)
{
   PCNRITEM pci = (PCNRITEM)item;

   handle = handle; /* keep compiler happy */
   if(pci)
      return pci->rc.pszIcon;
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
   PCNRITEM pci = (PCNRITEM)item;

   handle = handle; /* keep compiler happy */
   if(pci)
      return pci->parent;
   return (HTREEITEM)0;
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
   PCNRITEM pci = (PCNRITEM)item;

   handle = handle; /* keep compiler happy */
   if(!pci)
      return;

   pci->user = itemdata;
}

/*
 * Gets the item data of a tree item.
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 */
void * API dw_tree_item_get_data(HWND handle, HTREEITEM item)
{
   PCNRITEM pci = (PCNRITEM)item;

   handle = handle; /* keep compiler happy */
   if(!pci)
      return NULL;
   return pci->user;
}

/*
 * Sets this item as the active selection.
 * Parameters:
 *       handle: Handle to the tree window (widget) to be selected.
 *       item: Handle to the item to be selected.
 */
void API dw_tree_item_select(HWND handle, HTREEITEM item)
{
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   while(pCore)
   {
      if(pCore->flRecordAttr & CRA_SELECTED)
         WinSendMsg(handle, CM_SETRECORDEMPHASIS, (MPARAM)pCore, MPFROM2SHORT(FALSE, CRA_SELECTED | CRA_CURSORED));
      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
   }
   WinSendMsg(handle, CM_SETRECORDEMPHASIS, (MPARAM)item, MPFROM2SHORT(TRUE, CRA_SELECTED | CRA_CURSORED));
   lastitem = 0;
   lasthcnr = 0;
}

/*
 * Removes all nodes from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 */
void API dw_tree_clear(HWND handle)
{
   dw_container_clear(handle, TRUE);
}

/*
 * Expands a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be expanded.
 */
void API dw_tree_item_expand(HWND handle, HTREEITEM item)
{
   WinSendMsg(handle, CM_EXPANDTREE, MPFROMP(item), 0);
}

/*
 * Collapses a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be collapsed.
 */
void API dw_tree_item_collapse(HWND handle, HTREEITEM item)
{
   WinSendMsg(handle, CM_COLLAPSETREE, MPFROMP(item), 0);
}

/*
 * Removes a node from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       item: Handle to node to be deleted.
 */
void API dw_tree_item_delete(HWND handle, HTREEITEM item)
{
   PCNRITEM     pci = (PCNRITEM)item;

   if(!item)
      return;

   if(pci->rc.pszIcon)
   {
      free(pci->rc.pszIcon);
      pci->rc.pszIcon = 0;
   }

   WinSendMsg(handle, CM_REMOVERECORD, (MPARAM)&pci, MPFROM2SHORT(1, CMA_INVALIDATE | CMA_FREE));
}

/* Some OS/2 specific container structs */
typedef struct _containerinfo {
   int count;
   void *data;
   HWND handle;
} ContainerInfo;

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
   PFIELDINFO details, first, left = NULL;
   FIELDINFOINSERT detin;
   CNRINFO cnri;
   int z;
   ULONG size = sizeof(RECORDCORE);
   ULONG *offStruct = malloc(count * sizeof(ULONG));
   ULONG *tempflags = malloc((count+1) * sizeof(ULONG));
   WindowData *blah = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   ULONG *oldflags = blah ? blah->data : 0;

   if(!offStruct || !tempflags)
      return FALSE;

   memcpy(tempflags, flags, count * sizeof(ULONG));
   tempflags[count] = 0;

   blah->data = tempflags;
   blah->flags = separator;

   if(oldflags)
      free(oldflags);

   while((first = (PFIELDINFO)WinSendMsg(handle, CM_QUERYDETAILFIELDINFO,  0, MPFROMSHORT(CMA_FIRST))) != NULL)
   {
      WinSendMsg(handle, CM_REMOVEDETAILFIELDINFO, (MPARAM)&first, MPFROM2SHORT(1, CMA_FREE));
   }

   /* Figure out the offsets to the items in the struct */
   for(z=0;z<count;z++)
   {
      offStruct[z] = size;
      if(flags[z] & DW_CFA_BITMAPORICON)
         size += sizeof(HPOINTER);
      else if(flags[z] & DW_CFA_STRING)
         size += sizeof(char *);
      else if(flags[z] & DW_CFA_ULONG)
         size += sizeof(ULONG);
      else if(flags[z] & DW_CFA_DATE)
         size += sizeof(CDATE);
      else if(flags[z] & DW_CFA_TIME)
         size += sizeof(CTIME);
   }

   first = details = (PFIELDINFO)WinSendMsg(handle, CM_ALLOCDETAILFIELDINFO, MPFROMLONG(count), 0L);

   if(!first)
   {
      free(offStruct);
      return FALSE;
   }

   for(z=0;z<count;z++)
   {
      if(z==separator-1)
         left=details;
      details->cb = sizeof(FIELDINFO);
      details->flData = flags[z];
      details->flTitle = CFA_FITITLEREADONLY;
      details->pTitleData = titles[z];
      details->offStruct = offStruct[z];
      details = details->pNextFieldInfo;
   }

   detin.cb = sizeof(FIELDINFOINSERT);
   detin.fInvalidateFieldInfo = FALSE;
   detin.pFieldInfoOrder = (PFIELDINFO) CMA_FIRST;
   detin.cFieldInfoInsert = (ULONG)count;

   WinSendMsg(handle, CM_INSERTDETAILFIELDINFO, MPFROMP(first), MPFROMP(&detin));

   if(count > separator && separator > 0)
   {
      cnri.cb = sizeof(CNRINFO);
      cnri.pFieldInfoLast = left;
      cnri.xVertSplitbar  = 150;

      WinSendMsg(handle, CM_SETCNRINFO, MPFROMP(&cnri),  MPFROMLONG(CMA_PFIELDINFOLAST | CMA_XVERTSPLITBAR));
   }

   cnri.flWindowAttr = CV_DETAIL | CV_MINI | CA_DETAILSVIEWTITLES;
   cnri.slBitmapOrIcon.cx = 16;
   cnri.slBitmapOrIcon.cy = 16;

   WinSendMsg(handle, CM_SETCNRINFO, &cnri, MPFROMLONG(CMA_FLWINDOWATTR | CMA_SLBITMAPORICON));

   free(offStruct);
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

   newflags[0] = DW_CFA_BITMAPORICON | DW_CFA_CENTER | DW_CFA_HORZSEPARATOR | DW_CFA_SEPARATOR;
   newflags[1] = DW_CFA_STRING | DW_CFA_LEFT | DW_CFA_HORZSEPARATOR;

   memcpy(&newtitles[2], titles, sizeof(char *) * count);
   memcpy(&newflags[2], flags, sizeof(unsigned long) * count);

   dw_container_setup(handle, newflags, newtitles, count + 2, count ? 2 : 0);

   free(newtitles);
   free(newflags);
   return TRUE;
}

/*
 * Obtains an icon from a module (or header in GTK).
 * Parameters:
 *          module: Handle to module (DLL) in OS/2 and Windows.
 *          id: A unsigned long id int the resources on OS/2 and
 *              Windows, on GTK this is converted to a pointer
 *              to an embedded XPM.
 */
HICN API dw_icon_load(unsigned long module, unsigned long id)
{
   return WinLoadPointer(HWND_DESKTOP,module,id);
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
   char *file = alloca(strlen(filename) + 5);

   if(!file)
      return 0;

   strcpy(file, filename);

   /* check if we can read from this file (it exists and read permission) */
   if(access(file, 04) != 0)
   {
      /* Try with .bmp extention */
      strcat(file, ".ico");
      if(access(file, 04) != 0)
         return 0;
   }
   return WinLoadFileIcon(file, FALSE);
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
   HICN icon=0;
   char *file;
   FILE *fp;

   if ( !data )
      return 0;
   file = tmpnam( NULL );
   if ( file != NULL )
   {
      fp = fopen( file, "wb" );
      if ( fp != NULL )
      {
         fwrite( data, 1, len, fp );
         fclose( fp );
         icon = WinLoadFileIcon( file, FALSE );
      }
      else
      {
         unlink( file );
         return 0;
      }
      unlink( file );
   }
   return icon;
}

/*
 * Frees a loaded resource in OS/2 and Windows.
 * Parameters:
 *          handle: Handle to icon returned by dw_icon_load().
 */
void API dw_icon_free(HICN handle)
{
   WinDestroyPointer(handle);
}

/*
 * Allocates memory used to populate a container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          rowcount: The number of items to be populated.
 */
void * API dw_container_alloc(HWND handle, int rowcount)
{
   WindowData *wd = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   ULONG *flags = wd ? wd->data : 0;
   int z, size = 0, totalsize, count = 0;
   PRECORDCORE temp;
   ContainerInfo *ci;
   void *blah = NULL;

   if(!flags || rowcount < 1)
      return NULL;

   while(flags[count])
      count++;

   /* Figure out the offsets to the items in the struct */
   for(z=0;z<count;z++)
   {
      if(flags[z] & DW_CFA_BITMAPORICON)
         size += sizeof(HPOINTER);
      else if(flags[z] & DW_CFA_STRING)
         size += sizeof(char *);
      else if(flags[z] & DW_CFA_ULONG)
         size += sizeof(ULONG);
      else if(flags[z] & DW_CFA_DATE)
         size += sizeof(CDATE);
      else if(flags[z] & DW_CFA_TIME)
         size += sizeof(CTIME);
   }

   totalsize = size + sizeof(RECORDCORE);

   z = 0;

   if(!(blah = (void *)_dw_send_msg(handle, CM_ALLOCRECORD, MPFROMLONG(size), MPFROMLONG(rowcount), 0)))
      return NULL;

   temp = (PRECORDCORE)blah;

   for(z=0;z<rowcount;z++)
   {
      temp->cb = totalsize;
      temp = temp->preccNextRecord;
   }

   ci = malloc(sizeof(struct _containerinfo));

   ci->count = rowcount;
   ci->data = blah;
   ci->handle = handle;

   return (void *)ci;
}

/* Internal function that does the work for set_item and change_item */
void _dw_container_set_item(HWND handle, PRECORDCORE temp, int column, int row, void *data)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   ULONG totalsize, size = 0, *flags = blah ? blah->data : 0;
   int z, currentcount;
   CNRINFO cnr;
    void *dest;

   if(!flags)
      return;

   if(!_dw_send_msg(handle, CM_QUERYCNRINFO, (MPARAM)&cnr, MPFROMSHORT(sizeof(CNRINFO)), 0))
      return;

   currentcount = cnr.cRecords;

   /* Figure out the offsets to the items in the struct */
   for(z=0;z<column;z++)
   {
      if(flags[z] & DW_CFA_BITMAPORICON)
         size += sizeof(HPOINTER);
      else if(flags[z] & DW_CFA_STRING)
         size += sizeof(char *);
      else if(flags[z] & DW_CFA_ULONG)
         size += sizeof(ULONG);
      else if(flags[z] & DW_CFA_DATE)
         size += sizeof(CDATE);
      else if(flags[z] & DW_CFA_TIME)
         size += sizeof(CTIME);
   }

   totalsize = size + sizeof(RECORDCORE);

   for(z=0;z<(row-currentcount);z++)
      temp = temp->preccNextRecord;

   dest = (void *)(((ULONG)temp)+((ULONG)totalsize));

   if(flags[column] & DW_CFA_BITMAPORICON)
      memcpy(dest, data, sizeof(HPOINTER));
   else if(flags[column] & DW_CFA_STRING)
   {
      char **newstr = (char **)data, **str = dest;

      if(*str)
         free(*str);

      if(newstr && *newstr)
         *str = strdup(*newstr);
      else
         *str = NULL;
   }
   else if(flags[column] & DW_CFA_ULONG)
      memcpy(dest, data, sizeof(ULONG));
   else if(flags[column] & DW_CFA_DATE)
      memcpy(dest, data, sizeof(CDATE));
   else if(flags[column] & DW_CFA_TIME)
      memcpy(dest, data, sizeof(CTIME));
}

/* Internal function that free()s any strings allocated for a container item */
void _dw_container_free_strings(HWND handle, PRECORDCORE temp)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   ULONG totalsize, size = 0, *flags = blah ? blah->data : 0;
   int z, count = 0;

   if(!flags)
      return;

   while(flags[count])
      count++;

   /* Figure out the offsets to the items in the struct */
   for(z=0;z<count;z++)
   {
      if(flags[z] & DW_CFA_BITMAPORICON)
         size += sizeof(HPOINTER);
      else if(flags[z] & DW_CFA_STRING)
      {
         char **str;

         totalsize = size + sizeof(RECORDCORE);

         str = (char **)(((ULONG)temp)+((ULONG)totalsize));

         if(*str)
         {
            free(*str);
            *str = NULL;
         }
         size += sizeof(char *);
      }
      else if(flags[z] & DW_CFA_ULONG)
         size += sizeof(ULONG);
      else if(flags[z] & DW_CFA_DATE)
         size += sizeof(CDATE);
      else if(flags[z] & DW_CFA_TIME)
         size += sizeof(CTIME);
   }
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
   ContainerInfo *ci = (ContainerInfo *)pointer;

   if(!ci)
      return;

   _dw_container_set_item(handle, (PRECORDCORE)ci->data, column, row, data);
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
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));
   int count = 0;

   while(pCore)
   {
      if(count == row)
      {
         _dw_container_set_item(handle, pCore, column, 0, data);
         WinSendMsg(handle, CM_INVALIDATERECORD, (MPARAM)&pCore, MPFROM2SHORT(1, CMA_NOREPOSITION | CMA_TEXTCHANGED));
         return;
      }
      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
      count++;
   }
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
   dw_container_change_item(handle, column + 2, row, data);
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
   dw_container_change_item(handle, 0, row, (void *)&icon);
   dw_container_change_item(handle, 1, row, (void *)&filename);
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
   dw_container_set_item(handle, pointer, 0, row, (void *)&icon);
   dw_container_set_item(handle, pointer, 1, row, (void *)&filename);
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
   dw_container_set_item(handle, pointer, column + 2, row, data);
}

/*
 * Gets column type for a container column
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column.
 */
int API dw_container_get_column_type(HWND handle, int column)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   ULONG *flags = blah ? blah->data : 0;
   int rc;

   if(!flags)
      return 0;

   if(flags[column] & DW_CFA_BITMAPORICON)
      rc = DW_CFA_BITMAPORICON;
   else if(flags[column] & DW_CFA_STRING)
      rc = DW_CFA_STRING;
   else if(flags[column] & DW_CFA_ULONG)
      rc = DW_CFA_ULONG;
   else if(flags[column] & DW_CFA_DATE)
      rc = DW_CFA_DATE;
   else if(flags[column] & DW_CFA_TIME)
      rc = DW_CFA_TIME;
   else
      rc = 0;
   return rc;
}

/*
 * Gets column type for a filesystem container column
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          column: Zero based column.
 */
int API dw_filesystem_get_column_type(HWND handle, int column)
{
   return dw_container_get_column_type( handle, column + 2 );
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
   handle = handle; /* keep compiler happy */
   column = column; /* keep compiler happy */
   width = width; /* keep compiler happy */
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
   ContainerInfo *ci = (ContainerInfo *)pointer;
   PRECORDCORE temp;
   int z, currentcount;
   CNRINFO cnr;

   if(!ci)
      return;

   temp = (PRECORDCORE)ci->data;

   z = 0;

   if(!_dw_send_msg(ci->handle, CM_QUERYCNRINFO, (MPARAM)&cnr, MPFROMSHORT(sizeof(CNRINFO)), 0))
      return;

   currentcount = cnr.cRecords;

   for(z=0;z<(row-currentcount);z++)
      temp = temp->preccNextRecord;

   temp->pszIcon = title;
   temp->pszName = title;
   temp->pszText = title;
}

/*
 * Changes the title of a row already inserted in the container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          row: Zero based row of data being set.
 *          title: String title of the item.
 */
void API dw_container_change_row_title(HWND handle, int row, char *title)
{
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));
   int count = 0;

   while(pCore)
   {
      if(count == row)
      {
         pCore->pszIcon = title;
         pCore->pszName = title;
         pCore->pszText = title;

         WinSendMsg(handle, CM_INVALIDATERECORD, (MPARAM)&pCore, MPFROM2SHORT(1, CMA_NOREPOSITION | CMA_TEXTCHANGED));
         return;
      }
      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
      count++;
   }
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
   RECORDINSERT recin;
   ContainerInfo *ci = (ContainerInfo *)pointer;

   if(!ci)
      return;

   recin.cb = sizeof(RECORDINSERT);
   recin.pRecordOrder = (PRECORDCORE)CMA_END;
   recin.pRecordParent = NULL;
   recin.zOrder = CMA_TOP;
   recin.fInvalidateRecord = TRUE;
   recin.cRecordsInsert = rowcount;

   _dw_send_msg(handle, CM_INSERTRECORD, MPFROMP(ci->data), MPFROMP(&recin), 0);

   free(ci);
}

/*
 * Removes all rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       redraw: TRUE to cause the container to redraw immediately.
 */
void API dw_container_clear(HWND handle, int redraw)
{
   PCNRITEM pCore;
   int container = (int)dw_window_get_data(handle, "_dw_container");

   if(hwndEmph == handle)
      _clear_emphasis();

   pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   while(pCore)
   {
      if(container)
         _dw_container_free_strings(handle, (PRECORDCORE)pCore);
      else
      {
         /* Free icon text */
         if(pCore->rc.pszIcon)
         {
            free(pCore->rc.pszIcon);
            pCore->rc.pszIcon = 0;
         }
      }
      pCore = (PCNRITEM)pCore->rc.preccNextRecord;/*WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));*/
   }
   _dw_send_msg(handle, CM_REMOVERECORD, (MPARAM)0L, MPFROM2SHORT(0, (redraw ? CMA_INVALIDATE : 0) | CMA_FREE), -1);
}

/*
 * Removes the first x rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be deleted from.
 *       rowcount: The number of rows to be deleted.
 */
void API dw_container_delete(HWND handle, int rowcount)
{
   RECORDCORE *last, **prc = malloc(sizeof(RECORDCORE *) * rowcount);
   int current = 1;

   prc[0] = last = (RECORDCORE *)WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   while(last && current < rowcount)
   {
      _dw_container_free_strings(handle, last);
      prc[current] = last = (RECORDCORE *)WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)last, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
      current++;
   }

   _dw_send_msg(handle, CM_REMOVERECORD, (MPARAM)prc, MPFROM2SHORT(current, CMA_INVALIDATE | CMA_FREE), -1);

   free(prc);
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
   rows = rows; /* keep compiler happy */
   switch(direction)
   {
   case DW_SCROLL_TOP:
      WinSendMsg(handle, CM_SCROLLWINDOW, MPFROMSHORT(CMA_VERTICAL), MPFROMLONG(-10000000));
        break;
   case DW_SCROLL_BOTTOM:
      WinSendMsg(handle, CM_SCROLLWINDOW, MPFROMSHORT(CMA_VERTICAL), MPFROMLONG(10000000));
      break;
   }
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
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   if(pCore)
   {
      if(flags)
      {
         while(pCore)
         {
            if(pCore->flRecordAttr & flags)
            {
               dw_window_set_data(handle, "_dw_pcore", (void *)pCore);
               return pCore->pszIcon;
            }
            pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
         }
      }
      else
      {
         dw_window_set_data(handle, "_dw_pcore", (void *)pCore);
         return pCore->pszIcon;
      }
   }
    return NULL;
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
   PRECORDCORE pCore = (PRECORDCORE)dw_window_get_data(handle, "_dw_pcore");

   pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));

   if(pCore)
   {
      if(flags)
      {
         while(pCore)
         {
            if(pCore->flRecordAttr & flags)
            {
               dw_window_set_data(handle, "_dw_pcore", (void *)pCore);
               return pCore->pszIcon;
            }

            pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
         }
      }
      else
      {
         dw_window_set_data(handle, "_dw_pcore", (void *)pCore);
         return pCore->pszIcon;
      }
   }
    return NULL;
}

/*
 * Cursors the item with the text speficied, and scrolls to that item.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       text:  Text usually returned by dw_container_query().
 */
void API dw_container_cursor(HWND handle, char *text)
{
   RECTL viewport, item;
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   while(pCore)
   {
      if((char *)pCore->pszIcon == text)
      {
         QUERYRECORDRECT qrr;
         int scrollpixels = 0, midway;

         qrr.cb = sizeof(QUERYRECORDRECT);
         qrr.pRecord = pCore;
         qrr.fRightSplitWindow = 0;
         qrr.fsExtent = CMA_TEXT;

         WinSendMsg(handle, CM_SETRECORDEMPHASIS, (MPARAM)pCore, MPFROM2SHORT(TRUE, CRA_CURSORED));
         WinSendMsg(handle, CM_QUERYVIEWPORTRECT, (MPARAM)&viewport, MPFROM2SHORT(CMA_WORKSPACE, FALSE));
         WinSendMsg(handle, CM_QUERYRECORDRECT, (MPARAM)&item, (MPARAM)&qrr);

         midway = (viewport.yTop - viewport.yBottom)/2;
         scrollpixels = viewport.yTop - (item.yTop + midway);

         WinSendMsg(handle, CM_SCROLLWINDOW, MPFROMSHORT(CMA_VERTICAL),  MPFROMLONG(scrollpixels));
         return;
      }

      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
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
   PRECORDCORE pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));

   while(pCore)
   {
      if((char *)pCore->pszIcon == text)
      {
         WinSendMsg(handle, CM_REMOVERECORD, (MPARAM)&pCore, MPFROM2SHORT(1, CMA_FREE | CMA_INVALIDATE));
         return;
      }
      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
   }
}

/*
 * Optimizes the column widths so that all data is visible.
 * Parameters:
 *       handle: Handle to the window (widget) to be optimized.
 */
void API dw_container_optimize(HWND handle)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(handle, QWP_USER);
   RECTL item;
   PRECORDCORE pCore = NULL;
   int max = 0;

   if(blah && !blah->flags)
      return;

   pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)0L, MPFROM2SHORT(CMA_FIRST, CMA_ITEMORDER));
   while(pCore)
   {
      QUERYRECORDRECT qrr;
      int vector;

      qrr.cb = sizeof(QUERYRECORDRECT);
      qrr.pRecord = pCore;
      qrr.fRightSplitWindow = 0;
      qrr.fsExtent = CMA_TEXT;

      WinSendMsg(handle, CM_QUERYRECORDRECT, (MPARAM)&item, (MPARAM)&qrr);

      vector = item.xRight - item.xLeft;

      if(vector > max)
         max = vector;

      pCore = WinSendMsg(handle, CM_QUERYRECORD, (MPARAM)pCore, MPFROM2SHORT(CMA_NEXT, CMA_ITEMORDER));
   }

   if(max)
   {
      CNRINFO cnri;

      cnri.cb = sizeof(CNRINFO);
      cnri.xVertSplitbar  = max;

      WinSendMsg(handle, CM_SETCNRINFO, MPFROMP(&cnri),  MPFROMLONG(CMA_XVERTSPLITBAR));
   }
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
   handle = handle;
   icon = icon;
   bubbletext = bubbletext;
   /* TODO */
}

/*
 * Deletes an icon from the taskbar.
 * Parameters:
 *       handle: Window handle that was used with dw_taskbar_insert().
 *       icon: Icon handle that was used with dw_taskbar_insert().
 */
void API dw_taskbar_delete(HWND handle, HICN icon)
{
   handle = handle;
   icon = icon;
   /* TODO */
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
   HWND hwndframe = WinCreateWindow(HWND_OBJECT,
                            WC_FRAME,
                            NULL,
                            WS_VISIBLE |
                            FS_NOBYTEALIGN,
                            0,0,2000,1000,
                            NULLHANDLE,
                            HWND_TOP,
                            id,
                            NULL,
                            NULL);
   WinSubclassWindow(hwndframe, _RendProc);
   return hwndframe;
}

/* Sets the current foreground drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void API dw_color_foreground_set(unsigned long value)
{
   _foreground = value;
}

/* Sets the current background drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void API dw_color_background_set(unsigned long value)
{
   _background = value;
}

int DWSIGNAL _dw_color_cancel_func(HWND window, void *data)
{
   DWDialog *dwwait = (DWDialog *)data;
   HMTX mtx = (HMTX)dw_window_get_data((HWND)dwwait->data, "_dw_mutex");
   void *val;

   window = (HWND)dwwait->data;
   val = dw_window_get_data(window, "_dw_val");

   dw_mutex_lock(mtx);
   dw_mutex_close(mtx);
   dw_window_destroy(window);
   dw_dialog_dismiss((DWDialog *)data, val);
   return FALSE;
}

int DWSIGNAL _dw_color_ok_func(HWND window, void *data)
{
   DWDialog *dwwait = (DWDialog *)data;
   HMTX mtx = (HMTX)dw_window_get_data((HWND)dwwait->data, "_dw_mutex");
   unsigned long val;

   window = (HWND)dwwait->data;
   val = _dw_color_spin_get(window);

   dw_mutex_lock(mtx);
   dw_mutex_close(mtx);
   dw_window_destroy(window);
   dw_dialog_dismiss((DWDialog *)data, (void *)val);
   return FALSE;
}

/* Allows the user to choose a color using the system's color chooser dialog.
 * Parameters:
 *       value: current color
 * Returns:
 *       The selected color or the current color if cancelled.
 */
unsigned long API dw_color_choose(unsigned long value)
{
   HWND window, hbox, vbox, col, button, text;
   DWDialog *dwwait;
   HMTX mtx = dw_mutex_new();

   window = dw_window_new( HWND_DESKTOP, "Choose Color", FCF_SHELLPOSITION | FCF_TITLEBAR | FCF_DLGBORDER | FCF_CLOSEBUTTON | FCF_SYSMENU);

   vbox = dw_box_new(DW_VERT, 5);

   dw_box_pack_start(window, vbox, 0, 0, TRUE, TRUE, 0);

   hbox = dw_box_new(DW_HORZ, 0);

   dw_box_pack_start(vbox, hbox, 0, 0, FALSE, FALSE, 0);
   dw_window_set_style(hbox, 0, WS_CLIPCHILDREN);

   col = WinCreateWindow(vbox, "ColorSelectClass", "", WS_VISIBLE | WS_GROUP, 0, 0, 390, 300, vbox, HWND_TOP, 266, NULL,NULL);
   dw_box_pack_start(hbox, col, 390, 300, FALSE, FALSE, 0);

   dw_window_set_data(hbox, "_dw_window", (void *)window);
   dw_window_set_data(window, "_dw_mutex", (void *)mtx);
   dw_window_set_data(window, "_dw_col", (void *)col);
   dw_window_set_data(window, "_dw_val", (void *)value);

   hbox = dw_box_new(DW_HORZ, 0);
   dw_window_set_data(hbox, "_dw_window", (void *)window);

   dw_box_pack_start(vbox, hbox, 0, 0, TRUE, FALSE, 0);

   text = dw_text_new("Red:", 0);
   dw_window_set_style(text, DW_DT_VCENTER, DW_DT_VCENTER);
   dw_box_pack_start(hbox, text, 30, 20, FALSE, FALSE, 3);

   button = dw_spinbutton_new("", 1001L);
   dw_spinbutton_set_limits(button, 255, 0);
   dw_box_pack_start(hbox, button, 20, 20, TRUE, FALSE, 3);
   WinSetOwner(button, hbox);
   dw_window_set_data(window, "_dw_red_spin", (void *)button);

   text = dw_text_new("Green:", 0);
   dw_window_set_style(text, DW_DT_VCENTER, DW_DT_VCENTER);
   dw_box_pack_start(hbox, text, 30, 20, FALSE, FALSE, 3);

   button = dw_spinbutton_new("", 1002L);
   dw_spinbutton_set_limits(button, 255, 0);
   dw_box_pack_start(hbox, button, 20, 20, TRUE, FALSE, 3);
   WinSetOwner(button, hbox);
   dw_window_set_data(window, "_dw_green_spin", (void *)button);

   text = dw_text_new("Blue:", 0);
   dw_window_set_style(text, DW_DT_VCENTER, DW_DT_VCENTER);
   dw_box_pack_start(hbox, text, 30, 20, FALSE, FALSE, 3);

   button = dw_spinbutton_new("", 1003L);
   dw_spinbutton_set_limits(button, 255, 0);
   dw_box_pack_start(hbox, button, 20, 20, TRUE, FALSE, 3);
   WinSetOwner(button, hbox);
   dw_window_set_data(window, "_dw_blue_spin", (void *)button);

   hbox = dw_box_new(DW_HORZ, 0);

   dw_box_pack_start(vbox, hbox, 0, 0, TRUE, FALSE, 0);
   dw_box_pack_start(hbox, 0, 100, 1, TRUE, FALSE, 0);

   button = dw_button_new("Ok", 1001L);
   dw_box_pack_start(hbox, button, 50, 30, TRUE, FALSE, 3);

   dwwait = dw_dialog_new((void *)window);

   dw_signal_connect(button, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(_dw_color_ok_func), (void *)dwwait);

   button = dw_button_new("Cancel", 1002L);
   dw_box_pack_start(hbox, button, 50, 30, TRUE, FALSE, 3);

   dw_signal_connect(button, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(_dw_color_cancel_func), (void *)dwwait);
   dw_signal_connect(window, DW_SIGNAL_DELETE, DW_SIGNAL_FUNC(_dw_color_cancel_func), (void *)dwwait);

   dw_window_set_size(window, 400, 400);

   _dw_col_set(col, value);
   _dw_color_spin_set(window, value);

   dw_window_show(window);

   return (unsigned long)dw_dialog_wait(dwwait);
}

HPS _set_hps(HPS hps)
{
   LONG alTable[2];

   alTable[0] = DW_RED_VALUE(_foreground) << 16 | DW_GREEN_VALUE(_foreground) << 8 | DW_BLUE_VALUE(_foreground);
   alTable[1] = DW_RED_VALUE(_background) << 16 | DW_GREEN_VALUE(_background) << 8 | DW_BLUE_VALUE(_background);

   GpiCreateLogColorTable(hps,
                     LCOL_RESET,
                     LCOLF_CONSECRGB,
                     16,
                     2,
                     alTable);
   if(_foreground & DW_RGB_COLOR)
      GpiSetColor(hps, 16);
   else
      GpiSetColor(hps, _internal_color(_foreground));
   if(_background & DW_RGB_COLOR)
      GpiSetBackColor(hps, 17);
   else
      GpiSetBackColor(hps, _internal_color(_background));
   return hps;
}

HPS _set_colors(HWND handle)
{
   HPS hps = WinGetPS(handle);

   _set_hps(hps);
   return hps;
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
   HPS hps;
   int height;
   POINTL ptl;

   if(handle)
   {
      hps = _set_colors(handle);
        height = _get_height(handle);
   }
   else if(pixmap)
   {
      hps = _set_hps(pixmap->hps);
      height = pixmap->height;
   }
   else
      return;

   ptl.x = x;
   ptl.y = height - y - 1;

   GpiSetPel(hps, &ptl);
   if(!pixmap)
      WinReleasePS(hps);
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
   HPS hps;
   int height;
   POINTL ptl[2];

   if(handle)
   {
      hps = _set_colors(handle);
        height = _get_height(handle);
   }
   else if(pixmap)
   {
      hps = _set_hps(pixmap->hps);
      height = pixmap->height;
   }
   else
      return;

   ptl[0].x = x1;
   ptl[0].y = height - y1 - 1;
   ptl[1].x = x2;
   ptl[1].y = height - y2 - 1;

   GpiMove(hps, &ptl[0]);
   GpiLine(hps, &ptl[1]);

   if(!pixmap)
      WinReleasePS(hps);
}


void _CopyFontSettings(HPS hpsSrc, HPS hpsDst)
{
   FONTMETRICS fm;
   FATTRS fat;
   SIZEF sizf;

   GpiQueryFontMetrics(hpsSrc, sizeof(FONTMETRICS), &fm);

    memset(&fat, 0, sizeof(fat));

   fat.usRecordLength  = sizeof(FATTRS);
   fat.lMatch          = fm.lMatch;
   strcpy(fat.szFacename, fm.szFacename);

   GpiCreateLogFont(hpsDst, 0, 1L, &fat);
   GpiSetCharSet(hpsDst, 1L);

   sizf.cx = MAKEFIXED(fm.lEmInc,0);
   sizf.cy = MAKEFIXED(fm.lMaxBaselineExt,0);
   GpiSetCharBox(hpsDst, &sizf );
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
   HPS hps;
   int z, height;
   RECTL rcl;
   char fontname[128];
   POINTL aptl[TXTBOX_COUNT];

   if(handle)
   {
      hps = _set_colors(handle);
      height = _get_height(handle);
      _GetPPFont(handle, fontname);
   }
   else if(pixmap)
   {
      HPS pixmaphps = WinGetPS(pixmap->handle);

      hps = _set_hps(pixmap->hps);
      height = pixmap->height;
      _GetPPFont(pixmap->handle, fontname);
      _CopyFontSettings(pixmaphps, hps);
      WinReleasePS(pixmaphps);
   }
   else
      return;

   for(z=0;z<strlen(fontname);z++)
   {
      if(fontname[z]=='.')
         break;
   }

   GpiQueryTextBox(hps, strlen(text), text, TXTBOX_COUNT, aptl);

   rcl.xLeft = x;
   rcl.yTop = height - y;
   rcl.yBottom = rcl.yTop - (aptl[TXTBOX_TOPLEFT].y - aptl[TXTBOX_BOTTOMLEFT].y);
   rcl.xRight = rcl.xLeft + (aptl[TXTBOX_TOPRIGHT].x - aptl[TXTBOX_TOPLEFT].x);

   if(_background == DW_CLR_DEFAULT)
      WinDrawText(hps, -1, text, &rcl, DT_TEXTATTRS, DT_TEXTATTRS, DT_VCENTER | DT_LEFT | DT_TEXTATTRS);
   else
      WinDrawText(hps, -1, text, &rcl, _internal_color(_foreground), _internal_color(_background), DT_VCENTER | DT_LEFT | DT_ERASERECT);

   if(!pixmap)
      WinReleasePS(hps);
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
   HPS hps;
   POINTL aptl[TXTBOX_COUNT];

   if(handle)
   {
      hps = _set_colors(handle);
   }
   else if(pixmap)
   {
      HPS pixmaphps = WinGetPS(pixmap->handle);

      hps = _set_hps(pixmap->hps);
      _CopyFontSettings(pixmaphps, hps);
      WinReleasePS(pixmaphps);
   }
   else
      return;

   GpiQueryTextBox(hps, strlen(text), text, TXTBOX_COUNT, aptl);

   if(width)
      *width = aptl[TXTBOX_TOPRIGHT].x - aptl[TXTBOX_TOPLEFT].x;

   if(height)
      *height = aptl[TXTBOX_TOPLEFT].y - aptl[TXTBOX_BOTTOMLEFT].y;

   if(!pixmap)
      WinReleasePS(hps);
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
   HPS hps;
   int thisheight;
   POINTL *pptl;
   POINTL start;
   int i;

   if(handle)
   {
      hps = _set_colors(handle);
      thisheight = _get_height(handle);
   }
   else if(pixmap)
   {
      hps = _set_hps(pixmap->hps);
      thisheight = pixmap->height;
   }
   else
      return;
   if ( npoints == 0 )
      return;
   pptl = (POINTL *)malloc(sizeof(POINTL)*npoints);
   if ( pptl == NULL )
      return;
   /*
    * For a filled polygon we need to start an area
    */
   if ( fill )
      GpiBeginArea( hps, 0L );
   if ( npoints )
   {
      /*
       * Move to the first point of the polygon
       */
      start.x = x[0];
      start.y = y[0];
      GpiMove( hps, &start );
      /*
       * Convert the remainder of the x and y points
       */
      for ( i = 1; i < npoints; i++ )
      {
         pptl[i-1].x = x[i];
         pptl[i-1].y = y[i];
      }
      GpiPolyLine( hps, npoints-1, pptl );

      if ( fill )
         GpiEndArea( hps );
   }
   if ( !pixmap )
      WinReleasePS(hps);
   free( pptl );
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
   HPS hps;
   int thisheight;
   POINTL ptl[2];

   if(handle)
   {
      hps = _set_colors(handle);
        thisheight = _get_height(handle);
   }
   else if(pixmap)
   {
      hps = _set_hps(pixmap->hps);
      thisheight = pixmap->height;
   }
   else
      return;

   ptl[0].x = x;
   ptl[0].y = thisheight - y - 1;
   ptl[1].x = x + width - 1;
   ptl[1].y = thisheight - y - height;

   GpiMove(hps, &ptl[0]);
   GpiBox(hps, fill ? DRO_OUTLINEFILL : DRO_OUTLINE, &ptl[1], 0, 0);

   if(!pixmap)
      WinReleasePS(hps);
}

/* Call this after drawing to the screen to make sure
 * anything you have drawn is visible.
 */
void API dw_flush(void)
{
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
   BITMAPINFOHEADER bmih;
   SIZEL sizl = { 0, 0 };
   HPIXMAP pixmap;
   HDC hdc;
   HPS hps;
   ULONG ulFlags;
    LONG cPlanes, cBitCount;

   if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
      return NULL;

   hps = WinGetPS(handle);

   hdc     = GpiQueryDevice(hps);
   ulFlags = GpiQueryPS(hps, &sizl);

   pixmap->handle = handle;
   pixmap->hdc = DevOpenDC(dwhab, OD_MEMORY, "*", 0L, NULL, hdc);
   pixmap->hps = GpiCreatePS (dwhab, pixmap->hdc, &sizl, ulFlags | GPIA_ASSOC);

   DevQueryCaps(hdc, CAPS_COLOR_PLANES  , 1L, &cPlanes);
   if (!depth)
   {
      DevQueryCaps(hdc, CAPS_COLOR_BITCOUNT, 1L, &cBitCount);
      depth = cBitCount;
   }

   memset(&bmih, 0, sizeof(BITMAPINFOHEADER));
   bmih.cbFix     = sizeof(BITMAPINFOHEADER);
   bmih.cx        = (SHORT)width;
   bmih.cy        = (SHORT)height;
   bmih.cPlanes   = (SHORT)cPlanes;
   bmih.cBitCount = (SHORT)depth;

   pixmap->width = width; pixmap->height = height;

   pixmap->hbm = GpiCreateBitmap(pixmap->hps, (PBITMAPINFOHEADER2)&bmih, 0L, NULL, NULL);

   GpiSetBitmap(pixmap->hps, pixmap->hbm);

   if (depth>8)
      GpiCreateLogColorTable(pixmap->hps, LCOL_PURECOLOR, LCOLF_RGB, 0, 0, NULL );

   WinReleasePS(hps);

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
   char *file = alloca(strlen(filename) + 5);

   if ( !file || !(pixmap = calloc(1,sizeof(struct _hpixmap))) )
      return NULL;

   strcpy(file, filename);

   /* check if we can read from this file (it exists and read permission) */
   if ( access(file, 04) != 0 )
   {
      /* Try with .bmp extention */
      strcat(file, ".bmp");
      if ( access(file, 04) != 0 )
      {
         free(pixmap);
         return NULL;
      }
   }

   /* Try to load the bitmap from file */
   if ( !_load_bitmap_file(file, handle, &pixmap->hbm, &pixmap->hdc, &pixmap->hps, &pixmap->width, &pixmap->height) )
   {
      free(pixmap);
      return NULL;
   }

   /* Success fill in other values */
   pixmap->handle = handle;

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
   char *file;
   FILE *fp;

   if ( !(pixmap = calloc(1,sizeof(struct _hpixmap))) )
      return NULL;

   file = tmpnam( NULL );
   if ( file != NULL )
   {
      fp = fopen( file, "wb" );
      if ( fp != NULL )
      {
         fwrite( data, 1, len, fp );
         fclose( fp );
         if ( len > 1 && data[0] == 'B' && data[1] == 'M' ) /* first 2 chars of data is BM, then its a BMP */
         {
            /* Try to load the bitmap from file */
            if ( !_load_bitmap_file(file, handle, &pixmap->hbm, &pixmap->hdc, &pixmap->hps, &pixmap->width, &pixmap->height) )
            {
               free(pixmap);
               return NULL;
            }
         }
         else /* otherwise its assumed to be an ico */
         {
            /* con't use ICO ? */
            unlink( file );
            return NULL;
         }
      }
      else
      {
         unlink( file );
         return NULL;
      }
      unlink( file );
   }

   /* Success fill in other values */
   pixmap->handle = handle;

   return pixmap;
}

/*
 * Creates a bitmap mask for rendering bitmaps with transparent backgrounds
 */
void API dw_pixmap_set_transparent_color( HPIXMAP pixmap, ULONG color )
{
   if ( pixmap )
   {
      pixmap->transcolor = _internal_color(color);
   }
}

/*
 * Creates a pixmap from internal resource graphic specified by id.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       id: Resource ID associated with requested pixmap.
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP API dw_pixmap_grab(HWND handle, ULONG id)
{
   BITMAPINFOHEADER bmih;
   SIZEL sizl = { 0, 0 };
   HPIXMAP pixmap;
   HDC hdc;
   HPS hps;
   ULONG ulFlags;

   if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
      return NULL;

   hps = WinGetPS(handle);

   hdc     = GpiQueryDevice(hps);
   ulFlags = GpiQueryPS(hps, &sizl);

   pixmap->hdc = DevOpenDC(dwhab, OD_MEMORY, "*", 0L, NULL, hdc);
   pixmap->hps = GpiCreatePS (dwhab, pixmap->hdc, &sizl, ulFlags | GPIA_ASSOC);

   pixmap->hbm = GpiLoadBitmap(pixmap->hps, NULLHANDLE, id, 0, 0);

   GpiQueryBitmapParameters(pixmap->hbm, &bmih);

   GpiSetBitmap(pixmap->hps, pixmap->hbm);

   pixmap->width = bmih.cx; pixmap->height = bmih.cy;

   WinReleasePS(hps);

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
   GpiSetBitmap(pixmap->hps, NULLHANDLE);
   GpiDeleteBitmap(pixmap->hbm);
   GpiAssociate(pixmap->hps, NULLHANDLE);
   GpiDestroyPS(pixmap->hps);
   DevCloseDC(pixmap->hdc);
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
   HPS hpsdest;
   HPS hpssrc;
   POINTL ptl[4];
    int destheight, srcheight;

   if(dest)
   {
      hpsdest = WinGetPS(dest);
      destheight = _get_height(dest);
   }
   else if(destp)
   {
      hpsdest = destp->hps;
      destheight = destp->height;
   }
   else
      return;

   if(src)
   {
      hpssrc = WinGetPS(src);
      srcheight = _get_height(src);
   }
   else if(srcp)
   {
      hpssrc = srcp->hps;
      srcheight = srcp->height;
   }
   else
   {
      if(!destp)
         WinReleasePS(hpsdest);
      return;
   }

   ptl[0].x = xdest;
   ptl[0].y = (destheight - ydest) - height;
   ptl[1].x = ptl[0].x + width;
   ptl[1].y = destheight - ydest;
   ptl[2].x = xsrc;
   ptl[2].y = srcheight - (ysrc + height);
   ptl[3].x = ptl[2].x + width;
   ptl[3].y = ptl[2].y + height;

   GpiBitBlt(hpsdest, hpssrc, 4, ptl, ROP_SRCCOPY, BBO_IGNORE);

   if(!destp)
      WinReleasePS(hpsdest);
   if(!srcp)
      WinReleasePS(hpssrc);
}

/* Run DosBeep() in a separate thread so it doesn't block */
void _beepthread(void *data)
{
   int *info = (int *)data;

   if(data)
   {
      DosBeep(info[0], info[1]);
      free(data);
   }
}

/*
 * Emits a beep.
 * Parameters:
 *       freq: Frequency.
 *       dur: Duration.
 */
void API dw_beep(int freq, int dur)
{
   int *info = malloc(sizeof(int) * 2);

   if(info)
   {
      info[0] = freq;
      info[1] = dur;

      _beginthread(_beepthread, NULL, 100, (void *)info);
   }
}

/* Open a shared library and return a handle.
 * Parameters:
 *         name: Base name of the shared library.
 *         handle: Pointer to a module handle,
 *                 will be filled in with the handle.
 */
int API dw_module_load(char *name, HMOD *handle)
{
   char objnamebuf[300] = "";

   return DosLoadModule(objnamebuf, sizeof(objnamebuf), name, handle);
}

/* Queries the address of a symbol within open handle.
 * Parameters:
 *         handle: Module handle returned by dw_module_load()
 *         name: Name of the symbol you want the address of.
 *         func: A pointer to a function pointer, to obtain
 *               the address.
 */
int API dw_module_symbol(HMOD handle, char *name, void**func)
{
   return DosQueryProcAddr(handle, 0, name, (PFN*)func);
}

/* Frees the shared library previously opened.
 * Parameters:
 *         handle: Module handle returned by dw_module_load()
 */
int API dw_module_close(HMOD handle)
{
   DosFreeModule(handle);
   return 0;
}

/*
 * Returns the handle to an unnamed mutex semaphore.
 */
HMTX API dw_mutex_new(void)
{
   HMTX mutex;

   DosCreateMutexSem(NULL, &mutex, 0, FALSE);
   return mutex;
}

/*
 * Closes a semaphore created by dw_mutex_new().
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void API dw_mutex_close(HMTX mutex)
{
   DosCloseMutexSem(mutex);
}

/*
 * Tries to gain access to the semaphore, if it can't it blocks.
 * If we are in a callback we must keep the message loop running
 * while blocking.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void API dw_mutex_lock(HMTX mutex)
{
   if(_dwtid == dw_thread_id())
   {
      int rc = DosRequestMutexSem(mutex, SEM_IMMEDIATE_RETURN);

      while(rc == ERROR_TIMEOUT)
      {
         dw_main_sleep(10);
         rc = DosRequestMutexSem(mutex, SEM_IMMEDIATE_RETURN);
      }
   }
    else
      DosRequestMutexSem(mutex, SEM_INDEFINITE_WAIT);
}

/*
 * Reliquishes the access to the semaphore.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void API dw_mutex_unlock(HMTX mutex)
{
   DosReleaseMutexSem(mutex);
}

/*
 * Returns the handle to an unnamed event semaphore.
 */
HEV API dw_event_new(void)
{
   HEV blah;

   if(DosCreateEventSem (NULL, &blah, 0L, FALSE))
      return 0;

   return blah;
}

/*
 * Resets a semaphore created by dw_event_new().
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int API dw_event_reset(HEV eve)
{
   ULONG count;

   if(DosResetEventSem(eve, &count))
      return FALSE;
   return TRUE;
}

/*
 * Posts a semaphore created by dw_event_new(). Causing all threads
 * waiting on this event in dw_event_wait to continue.
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int API dw_event_post(HEV eve)
{
   if(DosPostEventSem(eve))
      return FALSE;
   return TRUE;
}


/*
 * Waits on a semaphore created by dw_event_new(), until the
 * event gets posted or until the timeout expires.
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int API dw_event_wait(HEV eve, unsigned long timeout)
{
   int rc = DosWaitEventSem(eve, timeout);
   if(!rc)
      return 1;
   if(rc == ERROR_TIMEOUT)
      return -1;
   return 0;
}

/*
 * Closes a semaphore created by dw_event_new().
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int API dw_event_close(HEV *eve)
{
   if(!eve || ~DosCloseEventSem(*eve))
      return FALSE;
   return TRUE;
}

/* Create a named event semaphore which can be
 * opened from other processes.
 * Parameters:
 *         eve: Pointer to an event handle to receive handle.
 *         name: Name given to semaphore which can be opened
 *               by other processes.
 */
HEV API dw_named_event_new(char *name)
{
   char *semname = malloc(strlen(name)+8);
   HEV ev = 0;

   if(!semname)
      return 0;

   strcpy(semname, "\\sem32\\");
   strcat(semname, name);

   DosCreateEventSem(semname, &ev, 0L, FALSE);

   free(semname);
   return ev;
}

/* Open an already existing named event semaphore.
 * Parameters:
 *         eve: Pointer to an event handle to receive handle.
 *         name: Name given to semaphore which can be opened
 *               by other processes.
 */
HEV API dw_named_event_get(char *name)
{
   char *semname = malloc(strlen(name)+8);
   HEV ev;

   if(!semname)
      return 0;

   strcpy(semname, "\\sem32\\");
   strcat(semname, name);

   DosOpenEventSem(semname, &ev);

   free(semname);
   return ev;
}

/* Resets the event semaphore so threads who call wait
 * on this semaphore will block.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int API dw_named_event_reset(HEV eve)
{
   ULONG count;

   return DosResetEventSem(eve, &count);
}

/* Sets the posted state of an event semaphore, any threads
 * waiting on the semaphore will no longer block.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int API dw_named_event_post(HEV eve)
{
   return DosPostEventSem(eve);
}


/* Waits on the specified semaphore until it becomes
 * posted, or returns immediately if it already is posted.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 *         timeout: Number of milliseconds before timing out
 *                  or -1 if indefinite.
 */
int API dw_named_event_wait(HEV eve, unsigned long timeout)
{
   int rc;

   rc = DosWaitEventSem(eve, timeout);
   switch (rc)
   {
   case ERROR_INVALID_HANDLE:
      rc = DW_ERROR_NON_INIT;
      break;
   case ERROR_NOT_ENOUGH_MEMORY:
      rc = DW_ERROR_NO_MEM;
      break;
   case ERROR_INTERRUPT:
      rc = DW_ERROR_INTERRUPT;
      break;
   case ERROR_TIMEOUT:
      rc = DW_ERROR_TIMEOUT;
      break;
   }

   return rc;
}

/* Release this semaphore, if there are no more open
 * handles on this semaphore the semaphore will be destroyed.
 * Parameters:
 *         eve: Handle to the semaphore obtained by
 *              an open or create call.
 */
int API dw_named_event_close(HEV eve)
{
   int rc;

   rc = DosCloseEventSem(eve);
   switch (rc)
   {
   case ERROR_INVALID_HANDLE:
      rc = DW_ERROR_NON_INIT;
      break;

   case ERROR_SEM_BUSY:
      rc = DW_ERROR_INTERRUPT;
      break;
   }

   return rc;
}

/*
 * Allocates a shared memory region with a name.
 * Parameters:
 *         handle: A pointer to receive a SHM identifier.
 *         dest: A pointer to a pointer to receive the memory address.
 *         size: Size in bytes of the shared memory region to allocate.
 *         name: A string pointer to a unique memory name.
 */
HSHM API dw_named_memory_new(void **dest, int size, char *name)
{
   char namebuf[1024];

   sprintf(namebuf, "\\sharemem\\%s", name);

   if(DosAllocSharedMem((void *)dest, namebuf, size, PAG_COMMIT | PAG_WRITE | PAG_READ) != NO_ERROR)
      return 0;

   return 1;
}

/*
 * Aquires shared memory region with a name.
 * Parameters:
 *         dest: A pointer to a pointer to receive the memory address.
 *         size: Size in bytes of the shared memory region to requested.
 *         name: A string pointer to a unique memory name.
 */
HSHM API dw_named_memory_get(void **dest, int size, char *name)
{
   char namebuf[1024];

   size = size;
   sprintf(namebuf, "\\sharemem\\%s", name);

   if(DosGetNamedSharedMem((void *)dest, namebuf, PAG_READ | PAG_WRITE) != NO_ERROR)
      return 0;

   return 1;
}

/*
 * Frees a shared memory region previously allocated.
 * Parameters:
 *         handle: Handle obtained from DB_named_memory_allocate.
 *         ptr: The memory address aquired with DB_named_memory_allocate.
 */
int API dw_named_memory_free(HSHM handle, void *ptr)
{
   handle = handle;

   if(DosFreeMem(ptr) != NO_ERROR)
      return -1;
   return 0;
}

/*
 * Encapsulate the message queues on OS/2.
 */
void _dwthreadstart(void *data)
{
   HAB thishab = WinInitialize(0);
   HMQ thishmq = WinCreateMsgQueue(dwhab, 0);
   void (* API threadfunc)(void *) = NULL;
   void **tmp = (void **)data;

   threadfunc = (void (* API)(void *))tmp[0];
   threadfunc(tmp[1]);

   free(tmp);

   WinDestroyMsgQueue(thishmq);
   WinTerminate(thishab);
}

/*
 * Creates a new thread with a starting point of func.
 * Parameters:
 *       func: Function which will be run in the new thread.
 *       data: Parameter(s) passed to the function.
 *       stack: Stack size of new thread (OS/2 and Windows only).
 */
DWTID API dw_thread_new(void *func, void *data, int stack)
{
   void **tmp = malloc(sizeof(void *) * 2);

   tmp[0] = func;
   tmp[1] = data;

   return (DWTID)_beginthread((void (*)(void *))_dwthreadstart, NULL, stack, (void *)tmp);
}

/*
 * Ends execution of current thread immediately.
 */
void API dw_thread_end(void)
{
   _endthread();
}

/*
 * Returns the current thread's ID.
 */
DWTID API dw_thread_id(void)
{
   return (DWTID)_threadid;
}

/*
 * Cleanly terminates a DW session, should be signal handler safe.
 * Parameters:
 *       exitcode: Exit code reported to the operating system.
 */
void API dw_exit(int exitcode)
{
   /* Destroy the menu message window */
   dw_window_destroy(hwndApp);

   /* In case we are in a signal handler, don't
    * try to free memory that could possibly be
    * free()'d by the runtime already.
    */
   Root = NULL;

   DosFreeModule(wpconfig);
   exit(exitcode);
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
   HWND tmp = WinCreateWindow(HWND_OBJECT,
                        SplitbarClassName,
                        NULL,
                        WS_VISIBLE | WS_CLIPCHILDREN,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
   if(tmp)
   {
      HWND tmpbox = dw_box_new(DW_VERT, 0);
        float *percent = malloc(sizeof(float));

      dw_box_pack_start(tmpbox, topleft, 1, 1, TRUE, TRUE, 0);
      WinSetParent(tmpbox, tmp, FALSE);
      dw_window_set_data(tmp, "_dw_topleft", (void *)tmpbox);

      tmpbox = dw_box_new(DW_VERT, 0);
      dw_box_pack_start(tmpbox, bottomright, 1, 1, TRUE, TRUE, 0);
      WinSetParent(tmpbox, tmp, FALSE);
      *percent = 50.0;
      dw_window_set_data(tmp, "_dw_bottomright", (void *)tmpbox);
      dw_window_set_data(tmp, "_dw_percent", (void *)percent);
      dw_window_set_data(tmp, "_dw_type", (void *)type);
   }
   return tmp;
}

/*
 * Sets the position of a splitbar (pecentage).
 * Parameters:
 *       handle: The handle to the splitbar returned by dw_splitbar_new().
 */
void API dw_splitbar_set(HWND handle, float percent)
{
   float *mypercent = (float *)dw_window_get_data(handle, "_dw_percent");
   int type = (int)dw_window_get_data(handle, "_dw_type");
    unsigned long width, height;

   if(mypercent)
      *mypercent = percent;

   dw_window_get_pos_size(handle, NULL, NULL, &width, &height);

   _handle_splitbar_resize(handle, percent, type, width, height);
}

/*
 * Gets the position of a splitbar (pecentage).
 * Parameters:
 *       handle: The handle to the splitbar returned by dw_splitbar_new().
 */
float API dw_splitbar_get(HWND handle)
{
   float *percent = (float *)dw_window_get_data(handle, "_dw_percent");

   if(percent)
      return *percent;
   return 0.0;
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
   char *funcname = "dw_box_pack_start()";

   /*
    * If you try and pack an item into itself VERY bad things can happen; like at least an
    * infinite loop on GTK! Lets be safe!
    */
   if(box == item)
   {
      dw_messagebox(funcname, DW_MB_OK|DW_MB_ERROR, "Danger! Danger! Will Robinson; box and item are the same!");
      return;
   }

   if(WinWindowFromID(box, FID_CLIENT))
   {
      box = WinWindowFromID(box, FID_CLIENT);
      hsize = TRUE;
      vsize = TRUE;
   }
   _dw_box_pack_start(box, item, width, height, hsize, vsize, pad, funcname);
}

void _dw_box_pack_start(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad, char *functionname)
{
   Box *thisbox = WinQueryWindowPtr(box, QWP_USER);

   if(thisbox)
   {
      int z;
      Item *tmpitem, *thisitem = thisbox->items;
      char tmpbuf[100];
      HWND frame = (HWND)dw_window_get_data(item, "_dw_combo_box");

      tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

      for(z=0;z<thisbox->count;z++)
      {
         tmpitem[z] = thisitem[z];
      }

      WinQueryClassName(item, 99, tmpbuf);

      if(vsize && !height)
         height = 1;
      if(hsize && !width)
         width = 1;

      if(strncmp(tmpbuf, "#1", 3)==0)
         tmpitem[thisbox->count].type = TYPEBOX;
      else
      {
         if ( width == 0 && hsize == FALSE )
            dw_messagebox(functionname, DW_MB_OK|DW_MB_ERROR, "Width and expand Horizonal both unset for box: %x item: %x",box,item);
         if ( height == 0 && vsize == FALSE )
            dw_messagebox(functionname, DW_MB_OK|DW_MB_ERROR, "Height and expand Vertical both unset for box: %x item: %x",box,item);

         tmpitem[thisbox->count].type = TYPEITEM;
      }

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

      if(thisbox->count)
         free(thisitem);

      thisbox->count++;

        /* Don't set the ownership if it's an entryfield  or spinbutton */
      WinQueryClassName(item, 99, tmpbuf);
      if(strncmp(tmpbuf, "#6", 3)!=0 && strncmp(tmpbuf, "#32", 4)!=0 && strncmp(tmpbuf, "#2", 3)!=0)
         WinSetOwner(item, box);
      WinSetParent(frame ? frame : item, box, FALSE);
   }
}

/* The following two functions graciously contributed by Peter Nielsen. */
static ULONG _ParseBuildLevel (char* pchBuffer, ULONG ulSize) {
   char* pchStart = pchBuffer;
   char* pchEnd = pchStart + ulSize - 2;

   while (pchEnd >= pchStart)
   {
      if ((pchEnd[0] == '#') && (pchEnd[1] == '@'))
      {
         *pchEnd-- = '\0';
         while (pchEnd >= pchStart)
         {
            if ((pchEnd[0] == '@') && (pchEnd[1] == '#'))
            {
               ULONG ulMajor = 0;
               ULONG ulMinor = 0;

               char* pch = pchEnd + 2;
               while (!isdigit ((int)*pch) && *pch)
                  pch++;

               while (isdigit ((int)*pch))
                  ulMajor = ulMajor * 10 + *pch++ - '0';

               if (*pch == '.')
               {
                  while (isdigit ((int)*++pch))
                     ulMinor = ulMinor * 10 + *pch - '0';
               }
               return ((ulMajor << 16) | ulMinor);
            }
            pchEnd--;
         }
      }
      pchEnd--;
   }
   return (0);
}

ULONG _GetSystemBuildLevel(void) {
   /* The build level info is normally available in the end of the OS2KRNL file. However, this is not the case in some beta versions of OS/2.
    * We first try to find the info in the 256 last bytes of the file. If that fails, we load the entire file and search it completely.
    */
   ULONG ulBootDrive = 0;
   ULONG ulBuild = 0;
   if (DosQuerySysInfo (QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, &ulBootDrive, sizeof (ulBootDrive)) == NO_ERROR)
   {
      char achFileName[11] = "C:\\OS2KRNL";
      HFILE hfile;
      ULONG ulResult;

        achFileName[0] = (char)('A'+ulBootDrive-1);

      if (DosOpen (achFileName, &hfile, &ulResult, 0, 0, OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS, OPEN_FLAGS_FAIL_ON_ERROR | OPEN_FLAGS_NO_CACHE | OPEN_FLAGS_SEQUENTIAL | OPEN_SHARE_DENYNONE | OPEN_ACCESS_READONLY, NULL) == NO_ERROR)
      {
         ULONG ulFileSize = 0;
         if (DosSetFilePtr (hfile, 0, FILE_END, &ulFileSize) == NO_ERROR)
         {
            const ULONG ulFirstTry = min (256, ulFileSize);
            if (DosSetFilePtr (hfile, -(LONG)ulFirstTry, FILE_END, &ulResult) == NO_ERROR)
            {
               char *pchBuffer = malloc(ulFirstTry);
               if (DosRead (hfile, pchBuffer, ulFirstTry, &ulResult) == NO_ERROR)
               {
                  ulBuild = _ParseBuildLevel (pchBuffer, ulFirstTry);
                  if (ulBuild == 0)
                  {
                     if (DosSetFilePtr (hfile, 0, FILE_BEGIN, &ulResult) == NO_ERROR)
                     {
                        free(pchBuffer);
                        pchBuffer = malloc(ulFileSize);

                        if (DosRead (hfile, pchBuffer, ulFileSize, &ulResult) == NO_ERROR)
                           ulBuild = _ParseBuildLevel (pchBuffer, ulFileSize);
                     }
                  }
               }
               free(pchBuffer);
            }
         }
         DosClose (hfile);
      }
   }
   return (ulBuild);
}

/*
 * Sets the default focus item for a window/dialog.
 * Parameters:
 *         window: Toplevel window or dialog.
 *         defaultitem: Handle to the dialog item to be default.
 */
void API dw_window_default(HWND window, HWND defaultitem)
{
   Box *thisbox = NULL;
   HWND box;

   box = WinWindowFromID(window, FID_CLIENT);
   if(box)
      thisbox = WinQueryWindowPtr(box, QWP_USER);

   if(thisbox)
      thisbox->defaultitem = defaultitem;
}

/*
 * Sets window to click the default dialog item when an ENTER is pressed.
 * Parameters:
 *         window: Window (widget) to look for the ENTER press.
 *         next: Window (widget) to move to next (or click)
 */
void API dw_window_click_default(HWND window, HWND next)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(window, QWP_USER);

   if(blah)
      blah->clickdefault = next;
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
    APIRET rc;
    char *retbuf = NULL;
    ULONG fmtInfo;

    WinOpenClipbrd(dwhab);

    rc = WinQueryClipbrdFmtInfo(dwhab, CF_TEXT, &fmtInfo);
    if (rc) /* Text data in clipboard */
    {
        PSZ pszClipText = (PSZ)WinQueryClipbrdData(dwhab, CF_TEXT); /* Query data handle */
        retbuf = strdup(pszClipText);
    }
    WinCloseClipbrd(dwhab);
    return retbuf;
}

/*
 * Sets the contents of the default clipboard to the supplied text.
 * Parameters:
 *       Text.
 */
void dw_clipboard_set_text( char *str, int len )
{
    APIRET rc;
    static PVOID shared;

    WinOpenClipbrd(dwhab); /* Open clipboard */
    WinEmptyClipbrd(dwhab); /* Empty clipboard */

    /* Ok, clipboard wants giveable unnamed shared memory */

    shared = NULL;
    rc = DosAllocSharedMem(&shared, NULL, len, OBJ_GIVEABLE | PAG_COMMIT | PAG_READ | PAG_WRITE);

    if (rc == 0)
    {
        memcpy(shared, str, len);

        WinSetClipbrdData(dwhab, (ULONG)shared, CF_TEXT, CFI_POINTER);
    }

    WinCloseClipbrd(dwhab); /* Close clipboard */
    return;
}

/*
 * Returns some information about the current operating environment.
 * Parameters:
 *       env: Pointer to a DWEnv struct.
 */
void API dw_environment_query(DWEnv *env)
{
   ULONG Build;

   if(!env)
      return;

   /* The default is OS/2 2.0 */
   strcpy(env->osName,"OS/2");
   env->MajorVersion = 2;
   env->MinorVersion = 0;

   Build = _GetSystemBuildLevel();
   env->MinorBuild =  Build & 0xFFFF;
   env->MajorBuild =  Build >> 16;

   if (aulBuffer[0] == 20)
   {
      int i = (unsigned int)aulBuffer[1];
      if (i > 20)
      {
         strcpy(env->osName,"Warp");
         env->MajorVersion = (int)i/10;
         env->MinorVersion = i-(((int)i/10)*10);
      }
      else if (i == 10)
         env->MinorVersion = 1;
   }
   strcpy(env->buildDate, __DATE__);
   strcpy(env->buildTime, __TIME__);
   env->DWMajorVersion = DW_MAJOR_VERSION;
   env->DWMinorVersion = DW_MINOR_VERSION;
   env->DWSubVersion = DW_SUB_VERSION;
}

/* The next few functions are support functions for the OS/2 folder browser */
void _populate_directory(HWND tree, HTREEITEM parent, char *path)
{
   FILEFINDBUF3 ffbuf;
   HTREEITEM item;
   ULONG count = 1;
   HDIR hdir = HDIR_CREATE;

   if(DosFindFirst(path, &hdir, FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED | MUST_HAVE_DIRECTORY,
               &ffbuf, sizeof(FILEFINDBUF3), &count, FIL_STANDARD) == NO_ERROR)
   {
      while(DosFindNext(hdir, &ffbuf, sizeof(FILEFINDBUF3), &count) == NO_ERROR)
      {
         if(strcmp(ffbuf.achName, ".") && strcmp(ffbuf.achName, ".."))
         {
            int len = strlen(path);
            char *folder = malloc(len + ffbuf.cchName + 2);
            HTREEITEM tempitem;

            strcpy(folder, path);
            strcpy(&folder[len-1], ffbuf.achName);

            item = dw_tree_insert(tree, ffbuf.achName, WinLoadFileIcon(folder, TRUE), parent, (void *)parent);
            tempitem = dw_tree_insert(tree, "", 0, item, 0);
            dw_tree_item_set_data(tree, item, (void *)tempitem);
         }
      }
      DosFindClose(hdir);
   }
}

void _populate_tree_thread(void *data)
{
   HWND window = (HWND)data, tree = (HWND)dw_window_get_data(window, "_dw_tree");
   HMTX mtx = (HMTX)dw_window_get_data(window, "_dw_mutex");
   int drive;
   HTREEITEM items[26];
   FSINFO  volinfo;

   DosError(FERR_DISABLEHARDERR);

   dw_mutex_lock(mtx);
   for(drive=0;drive<26;drive++)
   {
      if(DosQueryFSInfo(drive+1, FSIL_VOLSER,(PVOID)&volinfo, sizeof(FSINFO)) == NO_ERROR)
      {
         char folder[5] = "C:\\", name[9] = "Drive C:";
         HTREEITEM tempitem;

         folder[0] = name[6] = 'A' + drive;

         items[drive] = dw_tree_insert(tree, name, WinLoadFileIcon(folder, TRUE), NULL, 0);
         tempitem = dw_tree_insert(tree, "", 0, items[drive], 0);
         dw_tree_item_set_data(tree, items[drive], (void *)tempitem);
      }
      else
         items[drive] = 0;
   }
   dw_mutex_unlock(mtx);

   DosError(FERR_ENABLEHARDERR);
}

int DWSIGNAL _dw_ok_func(HWND window, void *data)
{
   DWDialog *dwwait = (DWDialog *)data;
   HMTX mtx = (HMTX)dw_window_get_data((HWND)dwwait->data, "_dw_mutex");
   void *treedata;

   window = window;
   if(!dwwait)
      return FALSE;

   dw_mutex_lock(mtx);
   treedata = dw_window_get_data((HWND)dwwait->data, "_dw_tree_selected");
   dw_mutex_close(mtx);
   dw_window_destroy((HWND)dwwait->data);
   dw_dialog_dismiss((DWDialog *)data, treedata);
   return FALSE;
}

int DWSIGNAL _dw_cancel_func(HWND window, void *data)
{
   DWDialog *dwwait = (DWDialog *)data;
   HMTX mtx = (HMTX)dw_window_get_data((HWND)dwwait->data, "_dw_mutex");

   window = window;
   if(!dwwait)
      return FALSE;

   dw_mutex_lock(mtx);
   dw_mutex_close(mtx);
   dw_window_destroy((HWND)dwwait->data);
   dw_dialog_dismiss((DWDialog *)data, NULL);
   return FALSE;
}

char *_tree_folder(HWND tree, HTREEITEM item)
{
   char *folder=strdup("");
   HTREEITEM parent = item;

   while(parent)
   {
      char *temp, *text = dw_tree_get_title(tree, parent);

      if(text)
      {
         if(strncmp(text, "Drive ", 6) == 0)
            text = &text[6];

         temp = malloc(strlen(text) + strlen(folder) + 3);
         strcpy(temp, text);
         strcat(temp, "\\");
         strcat(temp, folder);
         free(folder);
         folder = temp;
      }
      parent = dw_tree_get_parent(tree, parent);
   }
   return folder;
}

int DWSIGNAL _item_select(HWND window, HTREEITEM item, char *text, void *data, void *itemdata)
{
   DWDialog *dwwait = (DWDialog *)data;
   char *treedata = (char *)dw_window_get_data((HWND)dwwait->data, "_dw_tree_selected");

   text = text; itemdata = itemdata;
   if(treedata)
      free(treedata);

   treedata = _tree_folder(window, item);
   dw_window_set_data((HWND)dwwait->data, "_dw_tree_selected", (void *)treedata);

   return FALSE;
}

int DWSIGNAL _tree_expand(HWND window, HTREEITEM item, void *data)
{
   HTREEITEM tempitem = (HTREEITEM)dw_tree_item_get_data(window, item);

   data = data;
   if(tempitem)
   {
      char *folder = _tree_folder(window, item);

      dw_tree_item_set_data(window, item, 0);
      dw_tree_item_delete(window, tempitem);

      if(*folder)
      {
         strcat(folder, "*");
         _populate_directory(window, item, folder);
      }
      free(folder);
   }

   return FALSE;
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
   if(flags == DW_DIRECTORY_OPEN)
   {
      HWND window, hbox, vbox, tree, button;
      DWDialog *dwwait;
      HMTX mtx = dw_mutex_new();

      window = dw_window_new( HWND_DESKTOP, title, FCF_SHELLPOSITION | FCF_TITLEBAR | FCF_SIZEBORDER | FCF_MINMAX);

      vbox = dw_box_new(DW_VERT, 5);

      dw_box_pack_start(window, vbox, 0, 0, TRUE, TRUE, 0);

      tree = dw_tree_new(60);

      dw_box_pack_start(vbox, tree, 1, 1, TRUE, TRUE, 0);
      dw_window_set_data(window, "_dw_mutex", (void *)mtx);
      dw_window_set_data(window, "_dw_tree", (void *)tree);

      hbox = dw_box_new(DW_HORZ, 0);

      dw_box_pack_start(vbox, hbox, 0, 0, TRUE, FALSE, 0);

      dwwait = dw_dialog_new((void *)window);

      dw_signal_connect(tree, DW_SIGNAL_ITEM_SELECT, DW_SIGNAL_FUNC(_item_select), (void *)dwwait);
      dw_signal_connect(tree, DW_SIGNAL_TREE_EXPAND, DW_SIGNAL_FUNC(_tree_expand), (void *)dwwait);

      button = dw_button_new("Ok", 1001L);
      dw_box_pack_start(hbox, button, 50, 30, TRUE, FALSE, 3);
      dw_signal_connect(button, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(_dw_ok_func), (void *)dwwait);

      button = dw_button_new("Cancel", 1002L);
      dw_box_pack_start(hbox, button, 50, 30, TRUE, FALSE, 3);
      dw_signal_connect(button, DW_SIGNAL_CLICKED, DW_SIGNAL_FUNC(_dw_cancel_func), (void *)dwwait);
      dw_signal_connect(window, DW_SIGNAL_DELETE, DW_SIGNAL_FUNC(_dw_cancel_func), (void *)dwwait);

      dw_window_set_size(window, 225, 300);
      dw_window_show(window);

      dw_thread_new((void *)_populate_tree_thread, (void *)window, 0xff);
      return (char *)dw_dialog_wait(dwwait);
   }
   else
   {
      FILEDLG fild;
      HWND hwndFile;
      int len;

      if(defpath)
         strcpy(fild.szFullFile, defpath);
      else
         strcpy(fild.szFullFile, "");

      len = strlen(fild.szFullFile);

      if(len)
      {
         if(fild.szFullFile[len-1] != '\\')
            strcat(fild.szFullFile, "\\");
      }
      strcat(fild.szFullFile, "*");

      if(ext)
      {
         strcat(fild.szFullFile, ".");
         strcat(fild.szFullFile, ext);
      }

      memset(&fild, 0, sizeof(FILEDLG));
      fild.cbSize = sizeof(FILEDLG);
      fild.fl = FDS_CENTER | FDS_OPEN_DIALOG;
      fild.pszTitle = title;
      fild.pszOKButton = ((flags & DW_FILE_SAVE) ? "Save" : "Open");
      fild.pfnDlgProc = (PFNWP)WinDefFileDlgProc;

      hwndFile = WinFileDlg(HWND_DESKTOP, HWND_DESKTOP, &fild);
      if(hwndFile)
      {
         switch(fild.lReturn)
         {
         case DID_OK:
            return strdup(fild.szFullFile);
         case DID_CANCEL:
            return NULL;
         }
      }
   }
   return NULL;
}

/* Internal function to set drive and directory */
int _SetPath(char *path)
{
#ifndef __WATCOMC__
   if(strlen(path)   > 2)
   {
      if(path[1] == ':')
      {
         char drive = toupper(path[0]);
         _chdrive((drive - 'A')+1);
      }
   }
#endif
   return chdir(path);
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
int API dw_exec(char *program, int type, char **params)
{
   type = type; /* keep compiler happy */
   return spawnvp(P_NOWAIT, program, (const char **)params);
}

/*
 * Loads a web browser pointed at the given URL.
 * Parameters:
 *       url: Uniform resource locator.
 */
int API dw_browse(char *url)
{
   char *execargs[3], browser[1024], *olddir, *newurl = NULL;
   int len, ret;

   olddir = _getcwd(NULL, 1024);

   PrfQueryProfileString(HINI_USERPROFILE, "WPURLDEFAULTSETTINGS",
                    "DefaultWorkingDir", NULL, browser, 1024);

   if(browser[0])
      _SetPath(browser);

   PrfQueryProfileString(HINI_USERPROFILE, "WPURLDEFAULTSETTINGS",
                    "DefaultBrowserExe", NULL, browser, 1024);

   len = strlen(browser) - strlen("explore.exe");

   execargs[0] = browser;
   execargs[1] = url;
   execargs[2] = NULL;

   /* Special case for Web Explorer, it requires file:/// instead
    * of file:// so I am handling it here.
    */
   if(len > 0)
   {
      if(stricmp(&browser[len], "explore.exe") == 0 && stricmp(url, "file://") == 0)
      {
         int newlen, z;
         newurl = malloc(strlen(url) + 2);
         sprintf(newurl, "file:///%s", &url[7]);
         newlen = strlen(newurl);
         for(z=8;z<(newlen-8);z++)
         {
            if(newurl[z] == '|')
               newurl[z] = ':';
            if(newurl[z] == '/')
               newurl[z] = '\\';
         }
         execargs[1] = newurl;
      }
   }

   ret = dw_exec(browser, DW_EXEC_GUI, execargs);

   if(olddir)
   {
      _SetPath(olddir);
      free(olddir);
   }
   if(newurl)
      free(newurl);
   return ret;
}

/*
 * Causes the embedded HTML widget to take action.
 * Parameters:
 *       handle: Handle to the window.
 *       action: One of the DW_HTML_* constants.
 */
void API dw_html_action(HWND handle, int action)
{
   handle = handle;
   action = action;
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
   handle = handle;
   string = string;
   return -1;
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
   handle = handle;
   url = url;
   return -1;
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
   id = id;
   return dw_box_new(DW_HORZ, 0);
}

/*
 * Returns a pointer to a static buffer which containes the
 * current user directory.  Or the root directory (C:\ on
 * OS/2 and Windows).
 */
char * API dw_user_dir(void)
{
   static char _user_dir[1024] = "";

   if(!_user_dir[0])
   {
      char *home = getenv("HOME");

      if(home)
         strcpy(_user_dir, home);
      else
         strcpy(_user_dir, "C:\\");
   }
   return _user_dir;
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
   WinSendMsg(handle, WM_USER, (MPARAM)function, (MPARAM)data);
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
      if(stricmp(tmp->varname, varname) == 0)
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
      if(all || stricmp(tmp->varname, varname) == 0)
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
void API dw_window_set_data(HWND window, char *dataname, void *data)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(window, QWP_USER);

   if(!blah)
   {
      if(!dataname)
         return;

      blah = calloc(1, sizeof(WindowData));
      WinSetWindowPtr(window, QWP_USER, blah);
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
void * API dw_window_get_data(HWND window, char *dataname)
{
   WindowData *blah = (WindowData *)WinQueryWindowPtr(window, QWP_USER);

   if(blah && blah->root && dataname)
   {
      UserData *ud = _find_userdata(&(blah->root), dataname);
      if(ud)
         return ud->data;
   }
   return NULL;
}

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
   if(sigfunc)
   {
      int timerid = WinStartTimer(dwhab, NULLHANDLE, 0, interval);

      if(timerid)
      {
         _new_signal(WM_TIMER, NULLHANDLE, timerid, sigfunc, data);
         return timerid;
      }
   }
   return 0;
}

/*
 * Removes timer callback.
 * Parameters:
 *       id: Timer ID returned by dw_timer_connect().
 */
void API dw_timer_disconnect(int id)
{
   SignalHandler *prev = NULL, *tmp = Root;

   /* 0 is an invalid timer ID */
   if(!id)
      return;

   WinStopTimer(dwhab, NULLHANDLE, id);

   while(tmp)
   {
      if(tmp->id == id)
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
   ULONG message = 0, id = 0;

   if(window && signame && sigfunc)
   {
      if((message = _findsigmessage(signame)) != 0)
      {
         /* Handle special case of the menu item */
         if(message == WM_COMMAND && window < 65536)
         {
            char buffer[15];
            HWND owner;

            sprintf(buffer, "_dw_id%d", (int)window);
            owner = (HWND)dw_window_get_data(hwndApp, buffer);

            if(owner)
            {
               id = window;
               window = owner;
               dw_window_set_data(hwndApp, buffer, 0);
            }
            else
            {
               /* If it is a popup menu clear all entries */
               dw_signal_disconnect_by_window(window);
            }
         }
         _new_signal(message, window, id, sigfunc, data);
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



/*
 * Create a new static text window (widget) to be packed.
 * Not available under OS/2, eCS
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with dw_window_from_id() or 0L.
 */
HWND API dw_calendar_new(ULONG id)
{
    char *text = "dummy calendar";
    WindowData *blah = calloc(sizeof(WindowData), 1);
    HWND tmp = WinCreateWindow(HWND_OBJECT,
                        WC_STATIC,
                        text,
                        WS_VISIBLE | SS_TEXT,
                        0,0,2000,1000,
                        NULLHANDLE,
                        HWND_TOP,
                        id,
                        NULL,
                        NULL);
    blah->oldproc = WinSubclassWindow(tmp, _textproc);
    WinSetWindowPtr(tmp, QWP_USER, blah);
    dw_window_set_font(tmp, DefaultFont);
    dw_window_set_color(tmp, DW_CLR_BLACK, DW_CLR_PALEGRAY);
    WinSetWindowText(tmp, text);
    return tmp;
}

/*
 * The following are stubs
 */
void API dw_calendar_set_date( HWND window, unsigned int year, unsigned int month, unsigned int day )
{
    char tmp[30];
    sprintf( tmp, "%4.4d-%2.2d-%2.2d", year, month, day);
    WinSetWindowText(window, tmp);
}

void API dw_calendar_get_date( HWND window, unsigned int *year, unsigned int *month, unsigned int *day )
{
    window = window;
    *year = *month = *day = 0;
}