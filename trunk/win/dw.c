/*
 * Dynamic Windows:
 *          A GTK like implementation of the Win32 GUI
 *
 * (C) 2000-2002 Brian Smith <dbsoft@technologist.com>
 *
 */
#define _WIN32_IE 0x0500
#define WINVER 0x400
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <process.h>
#include <time.h>
#include "dw.h"

/* this is the callback handle for the window procedure */
/* make sure you always match the calling convention! */
int (*filterfunc)(HWND, UINT, WPARAM, LPARAM) = 0L;

HWND popup = (HWND)NULL, hwndBubble = (HWND)NULL, hwndBubbleLast, DW_HWND_OBJECT = (HWND)NULL;

HINSTANCE DWInstance = NULL;

DWORD dwVersion = 0, dwComctlVer = 0;
DWTID _dwtid = -1;

#define PACKVERSION(major,minor) MAKELONG(minor,major)

#define IS_IE5PLUS (dwComctlVer >= PACKVERSION(5,80))

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

char monthlist[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
                        "Sep", "Oct", "Nov", "Dec" };

int main(int argc, char *argv[]);

#define ICON_INDEX_LIMIT 200
HICON lookup[200];
HIMAGELIST hSmall  = 0, hLarge = 0;

/* Special flag used for internal tracking */
#define DW_CFA_RESERVED (1 << 30)

#define THREAD_LIMIT 128
COLORREF _foreground[THREAD_LIMIT];
COLORREF _background[THREAD_LIMIT];
HPEN _hPen[THREAD_LIMIT];
HBRUSH _hBrush[THREAD_LIMIT];

#ifdef DWDEBUG
FILE *f;

void reopen(void)
{
	fclose(f);
	f = fopen("dw.log", "at");
}
#endif

BYTE _red[] = { 	0x00, 0xbb, 0x00, 0xaa, 0x00, 0xbb, 0x00, 0xaa, 0x77,
			  0xff, 0x00, 0xee, 0x00, 0xff, 0x00, 0xff, 0xaa, 0x00 };
BYTE _green[] = {	0x00, 0x00, 0xbb, 0xaa, 0x00, 0x00, 0xbb, 0xaa, 0x77,
			  0x00, 0xff, 0xee, 0x00, 0x00, 0xee, 0xff, 0xaa, 0x00 };
BYTE _blue[] = { 	0x00, 0x00, 0x00, 0x00, 0xcc, 0xbb, 0xbb, 0xaa, 0x77,
		      0x00, 0x00, 0x00, 0xff, 0xff, 0xee, 0xff, 0xaa, 0x00};

HBRUSH _colors[18];


void _resize_notebook_page(HWND handle, int pageid);
int _lookup_icon(HWND handle, HICON hicon, int type);

typedef struct _sighandler
{
	struct _sighandler	*next;
	ULONG message;
	HWND window;
	void *signalfunction;
	void *data;

} SignalHandler;

SignalHandler *Root = NULL;
int _index;

typedef struct
{
	ULONG message;
	char name[30];

} SignalList;

static int in_checkbox_handler = 0;

/* List of signals and their equivilent Win32 message */
#define SIGNALMAX 15

SignalList SignalTranslate[SIGNALMAX] = {
	{ WM_SIZE, "configure_event" },
	{ WM_CHAR, "key_press_event" },
	{ WM_LBUTTONDOWN, "button_press_event" },
	{ WM_LBUTTONUP, "button_release_event" },
	{ WM_MOUSEMOVE, "motion_notify_event" },
	{ WM_CLOSE, "delete_event" },
	{ WM_PAINT, "expose_event" },
	{ WM_COMMAND, "clicked" },
	{ NM_DBLCLK, "container-select" },
	{ NM_RCLICK, "container-context" },
	{ LBN_SELCHANGE, "item-select" },
	{ TVN_SELCHANGED, "tree-select" },
	{ WM_SETFOCUS, "set-focus" },
	{ WM_USER+1, "lose-focus" },
	{ WM_VSCROLL, "value_changed" }
};

#ifdef BUILD_DLL
void Win32_Set_Instance(HINSTANCE hInstance)
{
	DWInstance = hInstance;
}
#else
char **_convertargs(int *count, char *start)
{
	char *tmp, *argstart, **argv;
	int loc = 0, inquotes = 0;

	(*count) = 1;

	tmp = start;

	/* Count the number of entries */
	if(*start)
	{
		(*count)++;

		while(*tmp)
		{
			if(*tmp == '"' && inquotes)
				inquotes = 0;
			else if(*tmp == '"' && !inquotes)
				inquotes = 1;
			else if(*tmp == ' ' && !inquotes)
			{
				/* Push past any white space */
				while(*(tmp+1) == ' ')
					tmp++;
				/* If we aren't at the end of the command
				 * line increment the count.
				 */
				if(*(tmp+1))
					(*count)++;
			}
			tmp++;
		}
	}

	argv = (char **)malloc(sizeof(char *) * ((*count)+1));
	argv[0] = malloc(260);
	GetModuleFileName(DWInstance, argv[0], 260);

	argstart = tmp = start;

	if(*start)
	{
		loc = 1;

		while(*tmp)
		{
			if(*tmp == '"' && inquotes)
			{
				*tmp = 0;
				inquotes = 0;
			}
			else if(*tmp == '"' && !inquotes)
			{
				argstart = tmp+1;
				inquotes = 1;
			}
			else if(*tmp == ' ' && !inquotes)
			{
				*tmp = 0;
				argv[loc] = strdup(argstart);

				/* Push past any white space */
				while(*(tmp+1) == ' ')
					tmp++;

				/* Move the start pointer */
				argstart = tmp+1;

				/* If we aren't at the end of the command
				 * line increment the count.
				 */
				if(*(tmp+1))
					loc++;
			}
			tmp++;
		}
		if(*argstart)
			argv[loc] = strdup(argstart);
	}
	argv[loc+1] = NULL;
	return argv;
}

/* Ok this is a really big hack but what the hell ;) */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	char **argv;
    int argc;

	DWInstance = hInstance;

	argv = _convertargs(&argc, lpCmdLine);

	return main(argc, argv);
}
#endif

/* This should return true for WinNT/2K/XP and false on Win9x */
int IsWinNT(void)
{
	static int isnt = -1;

	if(isnt == -1)
	{
		if (GetVersion() < 0x80000000)
			isnt = 1;
		else
			isnt = 0;
	}
	return isnt;
}

DWORD GetDllVersion(LPCTSTR lpszDllName)
{

	HINSTANCE hinstDll;
	DWORD dwVersion = 0;

	hinstDll = LoadLibrary(lpszDllName);

	if(hinstDll)
	{
		DLLGETVERSIONPROC pDllGetVersion;

		pDllGetVersion = (DLLGETVERSIONPROC) GetProcAddress(hinstDll, "DllGetVersion");

		/* Because some DLLs might not implement this function, you
		 * must test for it explicitly. Depending on the particular
		 * DLL, the lack of a DllGetVersion function can be a useful
		 * indicator of the version.
		 */
		if(pDllGetVersion)
		{
			DLLVERSIONINFO dvi;
			HRESULT hr;

			ZeroMemory(&dvi, sizeof(dvi));
			dvi.cbSize = sizeof(dvi);

            hr = (*pDllGetVersion)(&dvi);

			if(SUCCEEDED(hr))
			{
				dwVersion = PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
			}
		}

		FreeLibrary(hinstDll);
	}
    return dwVersion;
}

/* This function adds a signal handler callback into the linked list.
 */
void _new_signal(ULONG message, HWND window, void *signalfunction, void *data)
{
	SignalHandler *new = malloc(sizeof(SignalHandler));

	if(message == WM_COMMAND)
		dw_signal_disconnect_by_window(window);

	new->message = message;
	new->window = window;
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

/* This function removes and handlers on windows and frees
 * the user memory allocated to it.
 */
BOOL CALLBACK _free_window_memory(HWND handle, LPARAM lParam)
{
	ColorInfo *thiscinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

	dw_signal_disconnect_by_window(handle);

	if(thiscinfo)
	{
		/* Delete the brush so as not to leak GDI objects */
		if(thiscinfo->hbrush)
			DeleteObject(thiscinfo->hbrush);

		/* Free user data linked list memory */
		if(thiscinfo->root)
			dw_window_set_data(handle, NULL, NULL);

		SetWindowLong(handle, GWL_USERDATA, 0);
#if 0
		free(thiscinfo);
#endif
	}
	return TRUE;
}

/* This function returns 1 if the window (widget) handle
 * passed to it is a valid window that can gain input focus.
 */
int _validate_focus(HWND handle)
{
	char tmpbuf[100];

	if(!handle)
		return 0;

	if(!IsWindowEnabled(handle))
		return 0;

	GetClassName(handle, tmpbuf, 99);

	/* These are the window classes which can
	 * obtain input focus.
	 */
	if(strnicmp(tmpbuf, EDITCLASSNAME, strlen(EDITCLASSNAME)+1)==0 ||          /* Entryfield */
	   strnicmp(tmpbuf, BUTTONCLASSNAME, strlen(BUTTONCLASSNAME)+1)==0 ||      /* Button */
	   strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME)+1)==0 ||  /* Combobox */
	   strnicmp(tmpbuf, LISTBOXCLASSNAME, strlen(LISTBOXCLASSNAME)+1)==0 ||    /* List box */
	   strnicmp(tmpbuf, UPDOWN_CLASS, strlen(UPDOWN_CLASS)+1)==0 ||            /* Spinbutton */
	   strnicmp(tmpbuf, TRACKBAR_CLASS, strlen(TRACKBAR_CLASS)+1)==0 ||        /* Slider */
	   strnicmp(tmpbuf, WC_LISTVIEW, strlen(WC_LISTVIEW)+1)== 0 ||             /* Container */
	   strnicmp(tmpbuf, WC_TREEVIEW, strlen(WC_TREEVIEW)+1)== 0)               /* Tree */
		return 1;
	return 0;
}

HWND _normalize_handle(HWND handle)
{
	char tmpbuf[100] = "";

	GetClassName(handle, tmpbuf, 99);
	if(strnicmp(tmpbuf, UPDOWN_CLASS, strlen(UPDOWN_CLASS))==0) /* Spinner */
	{
		ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

		if(cinfo && cinfo->buddy)
			return cinfo->buddy;
	}
	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0) /* Combobox */
	{
		ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

		if(cinfo && cinfo->buddy)
			return cinfo->buddy;
	}
	return handle;
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
			SetFocus(lasthwnd);
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
			Box *thisbox = (Box *)GetWindowLong(box->items[z].hwnd, GWL_USERDATA);

			if(thisbox && _focus_check_box(thisbox, handle, start == 3 ? 3 : 0, defaultitem))
				return 1;
		}
		else
		{
			if(box->items[z].hwnd == handle)
			{
				if(lasthwnd == handle && firsthwnd)
					SetFocus(firsthwnd);
				else if(lasthwnd == handle && !firsthwnd)
					finish_searching = 1;
				else
					SetFocus(lasthwnd);

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
					if(!defaultitem || (defaultitem && box->items[z].hwnd == defaultitem))
					{
						SetFocus(_normalize_handle(box->items[z].hwnd));
						return 1;
					}
				}

				if(!firsthwnd)
					firsthwnd = _normalize_handle(box->items[z].hwnd);

				lasthwnd = _normalize_handle(box->items[z].hwnd);
			}
			else
			{
				char tmpbuf[100] = "";

				GetClassName(box->items[z].hwnd, tmpbuf, 99);

				if(strnicmp(tmpbuf, WC_TABCONTROL, strlen(WC_TABCONTROL))==0) /* Notebook */
				{
					NotebookPage **array = (NotebookPage **)GetWindowLong(box->items[z].hwnd, GWL_USERDATA);
					int pageid = TabCtrl_GetCurSel(box->items[z].hwnd);

					if(pageid > -1 && array && array[pageid])
					{
						Box *notebox;

						if(array[pageid]->hwnd)
						{
							notebox = (Box *)GetWindowLong(array[pageid]->hwnd, GWL_USERDATA);

							if(notebox && _focus_check_box(notebox, handle, start == 3 ? 3 : 0, defaultitem))
								return 1;
						}
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
			SetFocus(lasthwnd);
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
			Box *thisbox = (Box *)GetWindowLong(box->items[z].hwnd, GWL_USERDATA);

			if(thisbox && _focus_check_box_back(thisbox, handle, start == 3 ? 3 : 0, defaultitem))
				return 1;
		}
		else
		{
			if(box->items[z].hwnd == handle)
			{
				if(lasthwnd == handle && firsthwnd)
					SetFocus(firsthwnd);
				else if(lasthwnd == handle && !firsthwnd)
					finish_searching = 1;
				else
					SetFocus(lasthwnd);

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
					if(!defaultitem || (defaultitem && box->items[z].hwnd == defaultitem))
					{
						SetFocus(_normalize_handle(box->items[z].hwnd));
						return 1;
					}
				}

				if(!firsthwnd)
					firsthwnd = _normalize_handle(box->items[z].hwnd);

				lasthwnd = _normalize_handle(box->items[z].hwnd);
			}
			else
			{
				char tmpbuf[100] = "";

				GetClassName(box->items[z].hwnd, tmpbuf, 99);

				if(strnicmp(tmpbuf, WC_TABCONTROL, strlen(WC_TABCONTROL))==0) /* Notebook */
				{
					NotebookPage **array = (NotebookPage **)GetWindowLong(box->items[z].hwnd, GWL_USERDATA);
					int pageid = TabCtrl_GetCurSel(box->items[z].hwnd);

					if(pageid > -1 && array && array[pageid])
					{
						Box *notebox;

						if(array[pageid]->hwnd)
						{
							notebox = (Box *)GetWindowLong(array[pageid]->hwnd, GWL_USERDATA);

							if(notebox && _focus_check_box_back(notebox, handle, start == 3 ? 3 : 0, defaultitem))
								return 1;
						}
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
void _initial_focus(HWND handle)
{
	Box *thisbox;
	char tmpbuf[100];

	if(!handle)
		return;

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, ClassName, strlen(ClassName))!=0)
		return;


	if(handle)
		thisbox = (Box *)GetWindowLong(handle, GWL_USERDATA);

	if(thisbox)
	{
		_focus_check_box(thisbox, handle, 3, thisbox->defaultitem);
	}
}

/* This function finds the current widget in the
 * layout and moves the current focus to the next item.
 */
void _shift_focus(HWND handle)
{
	Box *thisbox;

	HWND box, lastbox = GetParent(handle);

	/* Find the toplevel window */
	while((box = GetParent(lastbox)))
	{
		lastbox = box;
	}

	thisbox = (Box *)GetWindowLong(lastbox, GWL_USERDATA);
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

	HWND box, lastbox = GetParent(handle);

	/* Find the toplevel window */
	while((box = GetParent(lastbox)))
	{
		lastbox = box;
	}

	thisbox = (Box *)GetWindowLong(lastbox, GWL_USERDATA);
	if(thisbox)
	{
		if(_focus_check_box_back(thisbox, handle, 1, 0)  == 0)
			_focus_check_box_back(thisbox, handle, 2, 0);
	}
}

/* ResetWindow:
 *         Resizes window to the exact same size to trigger
 *         recalculation of frame.
 */
void _ResetWindow(HWND hwndFrame)
{
	RECT rcl;

	GetWindowRect(hwndFrame, &rcl);
	SetWindowPos(hwndFrame, HWND_TOP, 0, 0, rcl.right - rcl.left,
				 rcl.bottom - rcl.top - 1, SWP_NOMOVE | SWP_NOZORDER);
	SetWindowPos(hwndFrame, HWND_TOP, 0, 0, rcl.right - rcl.left,
				 rcl.bottom - rcl.top, SWP_NOMOVE | SWP_NOZORDER);
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

	for(z=0;z<thisbox->count;z++)
	{
		if(thisbox->items[z].type == TYPEBOX)
		{
			int initialx, initialy;
			Box *tmp = (Box *)GetWindowLong(thisbox->items[z].hwnd, GWL_USERDATA);

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

#ifdef DWDEBUG
					if(pass > 1)
					{
						fprintf(f, "FARK! depth %d\r\nwidth = %d, height = %d, nux = %d, nuy = %d, upx = %d, upy = %d xratio = %f, yratio = %f\r\n\r\n",
								*depth, thisbox->items[z].width, thisbox->items[z].height, nux, nuy, tmp->upx, tmp->upy, tmp->xratio, tmp->yratio);
						reopen();
					}
#endif
					if(thisbox->type == BOXVERT)
					{
						if((thisbox->items[z].width-((thisbox->items[z].pad*2)+(tmp->pad*2)))!=0)
							tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-((thisbox->items[z].pad*2)+(tmp->pad*2))))/((float)(thisbox->items[z].width-((thisbox->items[z].pad*2)+(tmp->pad*2))));
					}
					else
					{
						if((thisbox->items[z].width-tmp->upx)!=0)
							tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-tmp->upx))/((float)(thisbox->items[z].width-tmp->upx));
					}
					if(thisbox->type == BOXHORZ)
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

#ifdef DWDEBUG
				if(pass > 1)
				{
					fprintf(f, "Before Resize Box depth %d\r\nx = %d, y = %d, usedx = %d, usedy = %d, usedpadx = %d, usedpady = %d xratio = %f, yratio = %f\r\n\r\n",
							*depth, x, y, *usedx, *usedy, *usedpadx, *usedpady, tmp->xratio, tmp->yratio);
					reopen();
				}
#endif

				_resize_box(tmp, depth, x, y, &nux, &nuy, pass, &upx, &upy);

				(*depth)--;

				newx = x - nux;
				newy = y - nuy;

				tmp->minwidth = thisbox->items[z].width = initialx - newx;
				tmp->minheight = thisbox->items[z].height = initialy - newy;

#ifdef DWDEBUG
				if(pass > 1)
				{
					fprintf(f, "After Resize Box depth %d\r\nx = %d, y = %d, usedx = %d, usedy = %d, usedpadx = %d, usedpady = %d width = %d, height = %d\r\n\r\n",
							*depth, x, y, *usedx, *usedy, *usedpadx, *usedpady, thisbox->items[z].width, thisbox->items[z].height);
					reopen();
				}
#endif
			}
		}

		if(pass > 1 && *depth > 0)
		{
			if(thisbox->type == BOXVERT)
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

			if(thisbox->type == BOXHORZ)
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
				Box *tmp = (Box *)GetWindowLong(thisbox->items[z].hwnd, GWL_USERDATA);

				if(tmp)
				{
					tmp->parentxratio = thisbox->items[z].xratio;
					tmp->parentyratio = thisbox->items[z].yratio;
				}
			}

#ifdef DWDEBUG
			fprintf(f, "RATIO- xratio = %f, yratio = %f, width = %d, height = %d, pad = %d, box xratio = %f, box yratio = %f, parent xratio = %f, parent yratio = %f, minwidth = %d, minheight = %d, width = %d, height = %d, upx = %d, upy = %d\r\n\r\n",
					thisbox->items[z].xratio, thisbox->items[z].yratio, thisbox->items[z].width, thisbox->items[z].height, thisbox->items[z].pad, thisbox->xratio, thisbox->yratio, thisbox->parentxratio, thisbox->parentyratio, thisbox->minwidth, thisbox->minheight, thisbox->width, thisbox->height, thisbox->upx, thisbox->upy);
			reopen();
#endif
		}
		else
		{
			thisbox->items[z].xratio = thisbox->xratio;
			thisbox->items[z].yratio = thisbox->yratio;
		}

		if(thisbox->type == BOXVERT)
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
		if(thisbox->type == BOXHORZ)
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

#ifdef DWDEBUG
	fprintf(f, "Done Calc depth %d\r\nusedx = %d, usedy = %d, usedpadx = %d, usedpady = %d, currentx = %d, currenty = %d, uxmax = %d, uymax = %d\r\n\r\n",
			*depth, *usedx, *usedy, *usedpadx, *usedpady, currentx, currenty, uxmax, uymax);
	reopen();
#endif

	/* The second pass is for expansion and actual placement. */
	if(pass > 1)
	{
		/* Any SIZEEXPAND items should be set to uxmax/uymax */
		for(z=0;z<thisbox->count;z++)
		{
			if(thisbox->items[z].hsize == SIZEEXPAND && thisbox->type == BOXVERT)
				thisbox->items[z].width = uxmax-(thisbox->items[z].pad*2);
			if(thisbox->items[z].vsize == SIZEEXPAND && thisbox->type == BOXHORZ)
				thisbox->items[z].height = uymax-(thisbox->items[z].pad*2);
			/* Run this code segment again to finalize the sized after setting uxmax/uymax values. */
			if(thisbox->items[z].type == TYPEBOX)
			{
				Box *tmp = (Box *)GetWindowLong(thisbox->items[z].hwnd, GWL_USERDATA);

				if(tmp)
				{
					if(*depth > 0)
					{
						if(thisbox->type == BOXVERT)
						{
							tmp->xratio = ((float)((thisbox->items[z].width * thisbox->xratio)-((thisbox->items[z].pad*2)+(thisbox->pad*2))))/((float)(tmp->minwidth-((thisbox->items[z].pad*2)+(thisbox->pad*2))));
							tmp->width = thisbox->items[z].width;
						}
						if(thisbox->type == BOXHORZ)
						{
							tmp->yratio = ((float)((thisbox->items[z].height * thisbox->yratio)-((thisbox->items[z].pad*2)+(thisbox->pad*2))))/((float)(tmp->minheight-((thisbox->items[z].pad*2)+(thisbox->pad*2))));
							tmp->height = thisbox->items[z].height;
						}
					}

					(*depth)++;

#ifdef DWDEBUG
					fprintf(f, "2- Resize Box depth %d\r\nx = %d, y = %d, usedx = %d, usedy = %d, usedpadx = %d, usedpady = %d xratio = %f, yratio = %f,\r\nupx = %d, upy = %d, width = %d, height = %d, minwidth = %d, minheight = %d, box xratio = %f, box yratio = %f\r\n\r\n",
						*depth, x, y, *usedx, *usedy, *usedpadx, *usedpady, tmp->xratio, tmp->yratio, tmp->upx, tmp->upy, thisbox->items[z].width, thisbox->items[z].height, tmp->minwidth, tmp->minheight, thisbox->xratio, thisbox->yratio);
					reopen();
#endif

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

				GetClassName(handle, tmpbuf, 99);

				if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME)+1)==0)
				{
					/* Handle special case Combobox */
					MoveWindow(handle, currentx + pad, currenty + pad,
							   width + vectorx, (height + vectory) + 400, TRUE);
				}
				else if(strnicmp(tmpbuf, UPDOWN_CLASS, strlen(UPDOWN_CLASS)+1)==0)
				{
					/* Handle special case Spinbutton */
					ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

					MoveWindow(handle, currentx + pad + ((width + vectorx) - 20), currenty + pad,
							   20, height + vectory, TRUE);

					if(cinfo)
					{
						MoveWindow(cinfo->buddy, currentx + pad, currenty + pad,
								   (width + vectorx) - 20, height + vectory, TRUE);
					}
				}
				else if(strnicmp(tmpbuf, STATICCLASSNAME, strlen(STATICCLASSNAME)+1)==0)
				{
					/* Handle special case Vertically Center static text */
					ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

					if(cinfo && cinfo->vcenter)
					{
						/* We are centered so calculate a new position */
						char tmpbuf[1024];
						int textheight, diff, total = height + vectory;

						GetWindowText(handle, tmpbuf, 1023);

						/* Figure out how big the text is */
						dw_font_text_extents(handle, 0, tmpbuf, 0, &textheight);

						diff = (total - textheight) / 2;

						MoveWindow(handle, currentx + pad, currenty + pad + diff,
								   width + vectorx, height + vectory - diff, TRUE);
					}
					else
					{
						MoveWindow(handle, currentx + pad, currenty + pad,
								   width + vectorx, height + vectory, TRUE);
					}
				}
				else
				{
					/* Everything else */
					MoveWindow(handle, currentx + pad, currenty + pad,
							   width + vectorx, height + vectory, TRUE);
					if(thisbox->items[z].type == TYPEBOX)
					{
						Box *boxinfo = (Box *)GetWindowLong(handle, GWL_USERDATA);

						if(boxinfo && boxinfo->grouphwnd)
							MoveWindow(boxinfo->grouphwnd, 0, 0,
									   width + vectorx, height + vectory, TRUE);

					}
				}

				/* Notebook dialog requires additional processing */
				if(strncmp(tmpbuf, WC_TABCONTROL, strlen(WC_TABCONTROL))==0)
				{
					RECT rect;
					NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
					int pageid = TabCtrl_GetCurSel(handle);

					if(pageid > -1 && array && array[pageid])
					{
						GetClientRect(handle,&rect);
						TabCtrl_AdjustRect(handle,FALSE,&rect);
						MoveWindow(array[pageid]->hwnd, rect.left, rect.top,
								   rect.right - rect.left, rect.bottom-rect.top, TRUE);
					}
				}

#ifdef DWDEBUG
				fprintf(f, "Window Pos depth %d\r\ncurrentx = %d, currenty = %d, pad = %d, width = %d, height = %d, vectorx = %d, vectory = %d, Box type = %s\r\n\r\n",
						*depth, currentx, currenty, pad, width, height, vectorx, vectory,thisbox->type == BOXHORZ ? "Horizontal" : "Vertical");
				reopen();
#endif

				if(thisbox->type == BOXHORZ)
					currentx += width + vectorx + (pad * 2);
				if(thisbox->type == BOXVERT)
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
			int usedx = 0, usedy = 0, depth = 0, usedpadx = 0, usedpady = 0;

			_resize_box(thisbox, &depth, x, y, &usedx, &usedy, 1, &usedpadx, &usedpady);

			if(usedx-usedpadx == 0 || usedy-usedpady == 0)
				return;

			thisbox->xratio = ((float)(x-usedpadx))/((float)(usedx-usedpadx));
			thisbox->yratio = ((float)(y-usedpady))/((float)(usedy-usedpady));

#ifdef DWDEBUG
			fprintf(f, "WM_SIZE Resize Box Pass 1\r\nx = %d, y = %d, usedx = %d, usedy = %d, usedpadx = %d, usedpady = %d xratio = %f, yratio = %f\r\n\r\n",
					x, y, usedx, usedy, usedpadx, usedpady, thisbox->xratio, thisbox->yratio);
			reopen();
#endif

			usedpadx = usedpady = usedx = usedy = depth = 0;

			_resize_box(thisbox, &depth, x, y, &usedx, &usedy, 2, &usedpadx, &usedpady);

#ifdef DWDEBUG
			fprintf(f, "WM_SIZE Resize Box Pass 2\r\nx = %d, y = %d, usedx = %d, usedy = %d, usedpadx = %d, usedpady = %d\r\n",
					x, y, usedx, usedy, usedpadx, usedpady);
			reopen();
#endif
		}
	}
}

/* The main window procedure for Dynamic Windows, all the resizing code is done here. */
BOOL CALLBACK _wndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	int result = -1;
	static int command_active = 0;
	SignalHandler *tmp = Root;
	void (* windowfunc)(PVOID);
	ULONG origmsg = msg;

	if(msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
		msg = WM_LBUTTONDOWN;
	if(msg == WM_RBUTTONUP || msg == WM_MBUTTONUP)
		msg = WM_LBUTTONUP;
	if(msg == WM_HSCROLL)
		msg = WM_VSCROLL;

	if(filterfunc)
		result = filterfunc(hWnd, msg, mp1, mp2);

	if(result == -1)
	{
		/* Avoid infinite recursion */
		command_active = 1;

		/* Find any callbacks for this function */
		while(tmp)
		{
			if(tmp->message == msg || msg == WM_COMMAND || msg == WM_NOTIFY || tmp->message == WM_USER+1)
			{
				switch(msg)
				{
				case WM_SETFOCUS:
					{
						int (*setfocusfunc)(HWND, void *) = (int (*)(HWND, void *))tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							result = setfocusfunc(tmp->window, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_SIZE:
					{
						int (*sizefunc)(HWND, int, int, void *) = tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							result = sizefunc(tmp->window, LOWORD(mp2), HIWORD(mp2), tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_LBUTTONDOWN:
					{
						POINTS pts = MAKEPOINTS(mp2);
						int (*buttonfunc)(HWND, int, int, int, void *) = (int (*)(HWND, int, int, int, void *))tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							int button;

							switch(origmsg)
							{
							case WM_LBUTTONDOWN:
								button = 1;
								break;
							case WM_RBUTTONDOWN:
								button = 2;
								break;
							case WM_MBUTTONDOWN:
								button = 3;
								break;
							}
							result = buttonfunc(tmp->window, pts.x, pts.y, button, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_LBUTTONUP:
					{
						POINTS pts = MAKEPOINTS(mp2);
						int (*buttonfunc)(HWND, int, int, int, void *) = (int (*)(HWND, int, int, int, void *))tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							int button;

							switch(origmsg)
							{
							case WM_LBUTTONUP:
								button = 1;
								break;
							case WM_RBUTTONUP:
								button = 2;
								break;
							case WM_MBUTTONUP:
								button = 3;
								break;
							}
							result = buttonfunc(tmp->window, pts.x, pts.y, button, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_MOUSEMOVE:
					{
						POINTS pts = MAKEPOINTS(mp2);
						int (*motionfunc)(HWND, int, int, int, void *) = (int (*)(HWND, int, int, int, void *))tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							int keys = 0;

							if (mp1 & MK_LBUTTON)
								keys = DW_BUTTON1_MASK;
							if (mp1 & MK_RBUTTON)
								keys |= DW_BUTTON2_MASK;
							if (mp1 & MK_MBUTTON)
								keys |= DW_BUTTON3_MASK;

							result = motionfunc(tmp->window, pts.x, pts.y, keys, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_CHAR:
					{
						int (*keypressfunc)(HWND, int, void *) = tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							result = keypressfunc(tmp->window, LOWORD(mp2), tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_CLOSE:
					{
						int (*closefunc)(HWND, void *) = tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							result = closefunc(tmp->window, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_PAINT:
					{
						PAINTSTRUCT ps;
						DWExpose exp;
						int (*exposefunc)(HWND, DWExpose *, void *) = tmp->signalfunction;

						if(hWnd == tmp->window)
						{
							BeginPaint(hWnd, &ps);
							exp.x = ps.rcPaint.left;
							exp.y = ps.rcPaint.top;
							exp.width = ps.rcPaint.right - ps.rcPaint.left;
							exp.height = ps.rcPaint.bottom - ps.rcPaint.top;
							result = exposefunc(hWnd, &exp, tmp->data);
							EndPaint(hWnd, &ps);
						}
					}
					break;
				case WM_NOTIFY:
					{
						if(tmp->message == TVN_SELCHANGED || tmp->message == NM_RCLICK)
						{
							NMTREEVIEW FAR *tem=(NMTREEVIEW FAR *)mp2;
							char tmpbuf[100];

							GetClassName(tem->hdr.hwndFrom, tmpbuf, 99);

							if(strnicmp(tmpbuf, WC_TREEVIEW, strlen(WC_TREEVIEW))==0)
							{
								if(tem->hdr.code == TVN_SELCHANGED && tmp->message == TVN_SELCHANGED)
								{
									if(tmp->window == tem->hdr.hwndFrom)
									{
										int (*treeselectfunc)(HWND, HWND, char *, void *, void *) = tmp->signalfunction;
										TVITEM tvi;
										void **ptrs;

										tvi.mask = TVIF_HANDLE;
										tvi.hItem = tem->itemNew.hItem;

										TreeView_GetItem(tmp->window, &tvi);

										ptrs = (void **)tvi.lParam;
										if(ptrs)
											result = treeselectfunc(tmp->window, (HWND)tem->itemNew.hItem, (char *)ptrs[0], (void *)ptrs[1], tmp->data);

										tmp = NULL;
									}
								}
								else if(tem->hdr.code == NM_RCLICK && tmp->message == NM_RCLICK)
								{
									if(tmp->window == tem->hdr.hwndFrom)
									{
										int (*containercontextfunc)(HWND, char *, int, int, void *, void *) = tmp->signalfunction;
										HTREEITEM hti;
										TVITEM tvi;
										TVHITTESTINFO thi;
										void **ptrs = NULL;
										LONG x, y;

										dw_pointer_query_pos(&x, &y);

										thi.pt.x = x;
										thi.pt.y = y;

										MapWindowPoints(HWND_DESKTOP, tmp->window, &thi.pt, 1);

										hti = TreeView_HitTest(tmp->window, &thi);

										if(hti)
										{
											tvi.mask = TVIF_HANDLE;
											tvi.hItem = hti;

											TreeView_GetItem(tmp->window, &tvi);
											dw_tree_item_select(tmp->window, (HWND)hti);

											ptrs = (void **)tvi.lParam;

										}
										containercontextfunc(tmp->window, ptrs ? (char *)ptrs[0] : NULL, x, y, tmp->data, ptrs ? ptrs[1] : NULL);
										tmp = NULL;
									}
								}
							}
						}
					}
					break;
				case WM_COMMAND:
					{
						int (*clickfunc)(HWND, void *) = tmp->signalfunction;
						HWND command;
						ULONG passthru = (ULONG)LOWORD(mp1);
						ULONG message = HIWORD(mp1);

						command = (HWND)passthru;

						if(message == LBN_SELCHANGE || message == CBN_SELCHANGE)
						{
							int (*listboxselectfunc)(HWND, int, void *) = tmp->signalfunction;

							if(tmp->message == LBN_SELCHANGE && tmp->window == (HWND)mp2)
							{
								result = listboxselectfunc(tmp->window, dw_listbox_selected(tmp->window), tmp->data);
								tmp = NULL;
							}
						} /* Make sure it's the right window, and the right ID */
						else if(tmp->window < (HWND)65536 && command == tmp->window)
						{
							result = clickfunc(popup ? popup : tmp->window, tmp->data);
							tmp = NULL;
						}
					}
					break;
				case WM_HSCROLL:
				case WM_VSCROLL:
					{
						char tmpbuf[100];
                        HWND handle = (HWND)mp2;

						GetClassName(handle, tmpbuf, 99);

						if(strnicmp(tmpbuf, TRACKBAR_CLASS, strlen(TRACKBAR_CLASS)+1)==0)
						{
							int (*valuechangefunc)(HWND, int, void *) = tmp->signalfunction;

							if(handle == tmp->window)
							{
								int value = (int)SendMessage(handle, TBM_GETPOS, 0, 0);
								int max = (int)SendMessage(handle, TBM_GETRANGEMAX, 0, 0);
								ULONG currentstyle = GetWindowLong(handle, GWL_STYLE);

								if(currentstyle & TBS_VERT)
									result = valuechangefunc(tmp->window, max - value, tmp->data);
								else
									result = valuechangefunc(tmp->window, value, tmp->data);
								tmp = NULL;
							}
						}
					}
					break;
				}
			}
			if(tmp)
				tmp = tmp->next;
		}
		command_active = 0;
	}

	/* Now that any handlers are done... do normal processing */
	switch( msg )
	{
	case WM_PAINT:
		{
			PAINTSTRUCT ps;

			BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
		}
		break;
	case WM_SIZE:
		{
			static int lastx = -1, lasty = -1;
			static HWND lasthwnd = 0;

			if(lastx != LOWORD(mp2) || lasty != HIWORD(mp2) || lasthwnd != hWnd)
			{
				Box *mybox = (Box *)GetWindowLong(hWnd, GWL_USERDATA);

				if(mybox && mybox->count)
				{
					lastx = LOWORD(mp2);
					lasty = HIWORD(mp2);
					lasthwnd = hWnd;

					ShowWindow(mybox->items[0].hwnd, SW_HIDE);
					_do_resize(mybox,LOWORD(mp2),HIWORD(mp2));
					ShowWindow(mybox->items[0].hwnd, SW_SHOW);
					return 0;
				}
			}
		}
		break;
	case WM_CHAR:
		if(LOWORD(mp1) == '\t')
		{
			if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
				_shift_focus_back(hWnd);
			else
				_shift_focus(hWnd);
			return TRUE;
		}
		break;
	case WM_USER:
		windowfunc = (void *)mp1;

		if(windowfunc)
			windowfunc((void *)mp2);
		break;
	case WM_NOTIFY:
		{
			NMHDR FAR *tem=(NMHDR FAR *)mp2;

			if(tem->code == TCN_SELCHANGING)
			{
				int num=TabCtrl_GetCurSel(tem->hwndFrom);
				NotebookPage **array = (NotebookPage **)GetWindowLong(tem->hwndFrom, GWL_USERDATA);

				if(num > -1 && array && array[num])
					SetParent(array[num]->hwnd, DW_HWND_OBJECT);

			}
			else if(tem->code == TCN_SELCHANGE)
			{
				int num=TabCtrl_GetCurSel(tem->hwndFrom);
				NotebookPage **array = (NotebookPage **)GetWindowLong(tem->hwndFrom, GWL_USERDATA);

				if(num > -1 && array && array[num])
					SetParent(array[num]->hwnd, tem->hwndFrom);

				_resize_notebook_page(tem->hwndFrom, num);
			}
		}
		break;
	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *info = (MINMAXINFO *)mp2;
			info->ptMinTrackSize.x = 8;
			info->ptMinTrackSize.y = 8;
			return 0;
		}
		break;
	case WM_DESTROY:
		/* Free memory before destroying */
		_free_window_memory(hWnd, 0);
		EnumChildWindows(hWnd, _free_window_memory, 0);
		break;
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORMSGBOX:
	case WM_CTLCOLORSCROLLBAR:
	case WM_CTLCOLORDLG:
		{
			ColorInfo *thiscinfo = (ColorInfo *)GetWindowLong((HWND)mp2, GWL_USERDATA);
			if(thiscinfo && thiscinfo->fore != -1 && thiscinfo->back != -1)
			{
				if(thiscinfo->fore > -1 && thiscinfo->back > -1 &&
				   thiscinfo->fore < 18 && thiscinfo->back < 18)
				{
					SetTextColor((HDC)mp1, RGB(_red[thiscinfo->fore],
											   _green[thiscinfo->fore],
											   _blue[thiscinfo->fore]));
					SetBkColor((HDC)mp1, RGB(_red[thiscinfo->back],
											 _green[thiscinfo->back],
											 _blue[thiscinfo->back]));
					if(thiscinfo->hbrush)
						DeleteObject(thiscinfo->hbrush);
					thiscinfo->hbrush = CreateSolidBrush(RGB(_red[thiscinfo->back],
															 _green[thiscinfo->back],
															 _blue[thiscinfo->back]));
					SelectObject((HDC)mp1, thiscinfo->hbrush);
					return (LONG)thiscinfo->hbrush;
				}
				if((thiscinfo->fore & DW_RGB_COLOR) == DW_RGB_COLOR && (thiscinfo->back & DW_RGB_COLOR) == DW_RGB_COLOR)
				{
					SetTextColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->fore),
											   DW_GREEN_VALUE(thiscinfo->fore),
											   DW_BLUE_VALUE(thiscinfo->fore)));
					SetBkColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->back),
												 DW_GREEN_VALUE(thiscinfo->back),
												 DW_BLUE_VALUE(thiscinfo->back)));
					if(thiscinfo->hbrush)
						DeleteObject(thiscinfo->hbrush);
					thiscinfo->hbrush = CreateSolidBrush(RGB(DW_RED_VALUE(thiscinfo->back),
															 DW_GREEN_VALUE(thiscinfo->back),
															 DW_BLUE_VALUE(thiscinfo->back)));
					SelectObject((HDC)mp1, thiscinfo->hbrush);
					return (LONG)thiscinfo->hbrush;
				}
			}

		}
		break;
	}
	if(result != -1)
		return result;
	else
		return DefWindowProc(hWnd, msg, mp1, mp2);
}

BOOL CALLBACK _framewndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	switch( msg )
	{
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		SetActiveWindow(hWnd);
		SetFocus(hWnd);
		break;
	case WM_COMMAND:
	case WM_NOTIFY:
		_wndproc(hWnd, msg, mp1, mp2);
		break;
#if 0
	case WM_ERASEBKGND:
		{
			ColorInfo *thiscinfo = (ColorInfo *)GetWindowLong(hWnd, GWL_USERDATA);

			if(thiscinfo && thiscinfo->fore != -1 && thiscinfo->back != -1)
				return FALSE;
		}
		break;
#endif
	case WM_PAINT:
		{
			ColorInfo *thiscinfo = (ColorInfo *)GetWindowLong(hWnd, GWL_USERDATA);

			if(thiscinfo && thiscinfo->fore != -1 && thiscinfo->back != -1)
			{
				PAINTSTRUCT ps;
				HDC hdcPaint = BeginPaint(hWnd, &ps);
				int success = FALSE;

				if(thiscinfo->fore > -1 && thiscinfo->back > -1 &&
				   thiscinfo->fore < 18 && thiscinfo->back < 18)
				{
					SetTextColor((HDC)mp1, RGB(_red[thiscinfo->fore],
											   _green[thiscinfo->fore],
											   _blue[thiscinfo->fore]));
					SetBkColor((HDC)mp1, RGB(_red[thiscinfo->back],
											 _green[thiscinfo->back],
											 _blue[thiscinfo->back]));
					DeleteObject(thiscinfo->hbrush);
					thiscinfo->hbrush = CreateSolidBrush(RGB(_red[thiscinfo->back],
															 _green[thiscinfo->back],
															 _blue[thiscinfo->back]));
					SelectObject(hdcPaint, thiscinfo->hbrush);
					Rectangle(hdcPaint, ps.rcPaint.left - 1, ps.rcPaint.top - 1, ps.rcPaint.right + 1, ps.rcPaint.bottom + 1);
					success = TRUE;
				}
				if((thiscinfo->fore & DW_RGB_COLOR) == DW_RGB_COLOR && (thiscinfo->back & DW_RGB_COLOR) == DW_RGB_COLOR)
				{
					SetTextColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->fore),
											   DW_GREEN_VALUE(thiscinfo->fore),
											   DW_BLUE_VALUE(thiscinfo->fore)));
					SetBkColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->back),
											 DW_GREEN_VALUE(thiscinfo->back),
											 DW_BLUE_VALUE(thiscinfo->back)));
					DeleteObject(thiscinfo->hbrush);
					thiscinfo->hbrush = CreateSolidBrush(RGB(DW_RED_VALUE(thiscinfo->back),
															 DW_GREEN_VALUE(thiscinfo->back),
															 DW_BLUE_VALUE(thiscinfo->back)));
					SelectObject(hdcPaint, thiscinfo->hbrush);
					Rectangle(hdcPaint, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom);
					success = TRUE;
				}

				EndPaint(hWnd, &ps);
				if(success)
					return FALSE;
			}

		}
		break;
	}
	return DefWindowProc(hWnd, msg, mp1, mp2);
}

BOOL CALLBACK _rendwndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	switch( msg )
	{
	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		SetActiveWindow(hWnd);
		_wndproc(hWnd, msg, mp1, mp2);
		break;
	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MOUSEMOVE:
	case WM_PAINT:
	case WM_SIZE:
	case WM_COMMAND:
		_wndproc(hWnd, msg, mp1, mp2);
		break;
	}
	return DefWindowProc(hWnd, msg, mp1, mp2);
}

BOOL CALLBACK _spinnerwndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	ColorInfo *cinfo;

	cinfo = (ColorInfo *)GetWindowLong(hWnd, GWL_USERDATA);

	if(cinfo)
	{
		switch( msg )
		{
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_KEYDOWN:
			{
				BOOL ret;

				if(!cinfo || !cinfo->pOldProc)
					ret = DefWindowProc(hWnd, msg, mp1, mp2);
				ret = CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);

				/* Tell the edit control that a buttonpress has
				 * occured and to update it's window title.
				 */
				if(cinfo && cinfo->buddy)
					SendMessage(cinfo->buddy, WM_USER+10, 0, 0);

				SetTimer(hWnd, 100, 100, (TIMERPROC)NULL);

				return ret;
			}
			break;
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
		case WM_KEYUP:
			{
				BOOL ret;

				if(!cinfo || !cinfo->pOldProc)
					ret = DefWindowProc(hWnd, msg, mp1, mp2);
				ret = CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);

				/* Tell the edit control that a buttonpress has
				 * occured and to update it's window title.
				 */
				if(cinfo && cinfo->buddy)
					SendMessage(cinfo->buddy, WM_USER+10, 0, 0);

				KillTimer(hWnd, 100);

				return ret;
			}
			break;
		case WM_TIMER:
			{
				if(mp1 == 100)
				{
					BOOL ret;

					if(cinfo && cinfo->buddy)
						SendMessage(cinfo->buddy, WM_USER+10, 0, 0);

					if(!cinfo || !cinfo->pOldProc)
						ret = DefWindowProc(hWnd, msg, mp1, mp2);
					ret = CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);

					/* Tell the edit control that a buttonpress has
					 * occured and to update it's window title.
					 */
					if(cinfo && cinfo->buddy)
						SendMessage(cinfo->buddy, WM_USER+10, 0, 0);

					return ret;
				}
			}
			break;
		case WM_USER+10:
			{
				if(cinfo->buddy)
				{
					char tempbuf[100] = "";
					long position;

					GetWindowText(cinfo->buddy, tempbuf, 99);

					position = atol(tempbuf);

					if(IS_IE5PLUS)
						SendMessage(hWnd, UDM_SETPOS32, 0, (LPARAM)position);
					else
						SendMessage(hWnd, UDM_SETPOS, 0, (LPARAM)MAKELONG((short)position, 0));
				}
			}
			break;
		}
	}

	if(!cinfo || !cinfo->pOldProc)
		return DefWindowProc(hWnd, msg, mp1, mp2);
	return CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);
}

void _click_default(HWND handle)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	/* These are the window classes which can
	 * obtain input focus.
	 */
	if(strnicmp(tmpbuf, BUTTONCLASSNAME, strlen(BUTTONCLASSNAME))==0)
	{
		/* Generate click on default item */
		SignalHandler *tmp = Root;

		/* Find any callbacks for this function */
		while(tmp)
		{
			if(tmp->message == WM_COMMAND)
			{
				int (*clickfunc)(HWND, void *) = tmp->signalfunction;

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
		SetFocus(handle);
}

BOOL CALLBACK _colorwndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	ColorInfo *cinfo;
	char tmpbuf[100];
	WNDPROC pOldProc = 0;

	cinfo = (ColorInfo *)GetWindowLong(hWnd, GWL_USERDATA);

	GetClassName(hWnd, tmpbuf, 99);
	if(strcmp(tmpbuf, FRAMECLASSNAME) == 0)
		cinfo = &(((Box *)cinfo)->cinfo);

	if(cinfo)
	{
		pOldProc = cinfo->pOldProc;

		switch( msg )
		{
		case WM_SETFOCUS:
            if(cinfo->combo)
				_wndproc(cinfo->combo, msg, mp1, mp2);
			else
				_wndproc(hWnd, msg, mp1, mp2);
			break;
		case WM_VSCROLL:
		case WM_HSCROLL:
			_wndproc(hWnd, msg, mp1, mp2);
			break;
		case WM_KEYDOWN:
		case WM_KEYUP:
			{
				if(mp1 == VK_UP || mp1 == VK_DOWN)
				{
					BOOL ret;

					if(!cinfo || !cinfo->pOldProc)
						ret = DefWindowProc(hWnd, msg, mp1, mp2);
					ret = CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);

					/* Tell the spinner control that a keypress has
					 * occured and to update it's internal value.
					 */
					if(cinfo && cinfo->buddy && !cinfo->combo)
						PostMessage(hWnd, WM_USER+10, 0, 0);

                    if(msg == WM_KEYDOWN)
						SetTimer(hWnd, 101, 100, (TIMERPROC)NULL);
                    else
						KillTimer(hWnd, 101);

					return ret;
				}
			}
			break;
		case WM_TIMER:
			{
				if(mp1 == 101)
				{
					BOOL ret;

					if(!cinfo || !cinfo->pOldProc)
						ret = DefWindowProc(hWnd, msg, mp1, mp2);
					ret = CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);

					/* Tell the spinner control that a keypress has
					 * occured and to update it's internal value.
					 */
					if(cinfo && cinfo->buddy && !cinfo->combo)
						PostMessage(hWnd, WM_USER+10, 0, 0);

					return ret;
				}
			}
			break;
		case WM_CHAR:
			if(LOWORD(mp1) == '\t')
			{
				if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
				{
					if(cinfo->combo)
						_shift_focus_back(cinfo->combo);
					else if(cinfo->buddy)
						_shift_focus_back(cinfo->buddy);
					else
						_shift_focus_back(hWnd);
				}
				else
				{
					if(cinfo->combo)
						_shift_focus(cinfo->combo);
					else if(cinfo->buddy)
						_shift_focus(cinfo->buddy);
					else
						_shift_focus(hWnd);
				}
				return FALSE;
			}
			else if(LOWORD(mp1) == '\r')
			{
				if(cinfo->clickdefault)
					_click_default(cinfo->clickdefault);

			}

			/* Tell the spinner control that a keypress has
			 * occured and to update it's internal value.
			 */
			if(cinfo->buddy && !cinfo->combo)
			{
				if(IsWinNT())
					PostMessage(cinfo->buddy, WM_USER+10, 0, 0);
				else
					SendMessage(cinfo->buddy, WM_USER+10, 0, 0);
			}
			break;
		case WM_USER+10:
			{
				if(cinfo->buddy)
				{
					long val;

					if(IS_IE5PLUS)
						val = (long)SendMessage(cinfo->buddy, UDM_GETPOS32, 0, 0);
					else
						val = (long)SendMessage(cinfo->buddy, UDM_GETPOS, 0, 0);

					sprintf(tmpbuf, "%d", val);
					SetWindowText(hWnd, tmpbuf);
				}
			}
			break;
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORLISTBOX:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORMSGBOX:
		case WM_CTLCOLORSCROLLBAR:
		case WM_CTLCOLORDLG:
			{
				ColorInfo *thiscinfo = (ColorInfo *)GetWindowLong((HWND)mp2, GWL_USERDATA);
				if(thiscinfo && thiscinfo->fore != -1 && thiscinfo->back != -1)
				{
					if(thiscinfo->fore > -1 && thiscinfo->back > -1 &&
					   thiscinfo->fore < 18 && thiscinfo->back < 18)
					{
						SetTextColor((HDC)mp1, RGB(_red[thiscinfo->fore],
												   _green[thiscinfo->fore],
											   _blue[thiscinfo->fore]));
						SetBkColor((HDC)mp1, RGB(_red[thiscinfo->back],
												 _green[thiscinfo->back],
												 _blue[thiscinfo->back]));
						if(thiscinfo->hbrush)
							DeleteObject(thiscinfo->hbrush);
						thiscinfo->hbrush = CreateSolidBrush(RGB(_red[thiscinfo->back],
																 _green[thiscinfo->back],
																 _blue[thiscinfo->back]));
						SelectObject((HDC)mp1, thiscinfo->hbrush);
						return (LONG)thiscinfo->hbrush;
					}
					if((thiscinfo->fore & DW_RGB_COLOR) == DW_RGB_COLOR && (thiscinfo->back & DW_RGB_COLOR) == DW_RGB_COLOR)
					{
						SetTextColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->fore),
												   DW_GREEN_VALUE(thiscinfo->fore),
												   DW_BLUE_VALUE(thiscinfo->fore)));
						SetBkColor((HDC)mp1, RGB(DW_RED_VALUE(thiscinfo->back),
												 DW_GREEN_VALUE(thiscinfo->back),
												 DW_BLUE_VALUE(thiscinfo->back)));
						if(thiscinfo->hbrush)
							DeleteObject(thiscinfo->hbrush);
						thiscinfo->hbrush = CreateSolidBrush(RGB(DW_RED_VALUE(thiscinfo->back),
																 DW_GREEN_VALUE(thiscinfo->back),
																 DW_BLUE_VALUE(thiscinfo->back)));
						SelectObject((HDC)mp1, thiscinfo->hbrush);
						return (LONG)thiscinfo->hbrush;
					}
				}

			}
			break;
		}
	}

	if(!pOldProc)
		return DefWindowProc(hWnd, msg, mp1, mp2);
	return CallWindowProc(pOldProc, hWnd, msg, mp1, mp2);
}

BOOL CALLBACK _containerwndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	ContainerInfo *cinfo;

	cinfo = (ContainerInfo *)GetWindowLong(hWnd, GWL_USERDATA);

	switch( msg )
	{
	case WM_COMMAND:
	case WM_NOTIFY:
		_wndproc(hWnd, msg, mp1, mp2);
		break;
	case WM_LBUTTONDBLCLK:
	case WM_CHAR:
		{
			LV_ITEM lvi;
			int iItem;

			if(LOWORD(mp1) == '\t')
			{
				if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
					_shift_focus_back(hWnd);
				else
					_shift_focus(hWnd);
				return FALSE;
			}

			if(msg == WM_CHAR && (char)mp1 != '\r')
				break;

			iItem = ListView_GetNextItem(hWnd, -1, LVNI_FOCUSED);

			memset(&lvi, 0, sizeof(LV_ITEM));

			if(iItem > -1)
			{
				lvi.iItem = iItem;
				lvi.mask = LVIF_PARAM;

				ListView_GetItem(hWnd, &lvi);
			}

			{
				SignalHandler *tmp = Root;

				while(tmp)
				{
					if(tmp->message == NM_DBLCLK && tmp->window == hWnd)
					{
						int (*containerselectfunc)(HWND, char *, void *) = tmp->signalfunction;

						/* Seems to be having lParam as 1 which really sucks */
						if(lvi.lParam < 100)
							lvi.lParam = 0;

						containerselectfunc(tmp->window, (char *)lvi.lParam, tmp->data);
						tmp = NULL;
					}
					if(tmp)
						tmp = tmp->next;
				}
			}
		}
		break;
	case WM_CONTEXTMENU:
		{
			SignalHandler *tmp = Root;

			while(tmp)
			{
				if(tmp->message == NM_RCLICK && tmp->window == hWnd)
				{
					int (*containercontextfunc)(HWND, char *, int, int, void *) = tmp->signalfunction;
					LONG x,y;
					LV_ITEM lvi;
					int iItem;
					LVHITTESTINFO lhi;

					dw_pointer_query_pos(&x, &y);

					lhi.pt.x = x;
					lhi.pt.y = y;

					MapWindowPoints(HWND_DESKTOP, tmp->window, &lhi.pt, 1);

					iItem = ListView_HitTest(tmp->window, &lhi);

					memset(&lvi, 0, sizeof(LV_ITEM));

					if(iItem > -1)
					{
						lvi.iItem = iItem;
						lvi.mask = LVIF_PARAM;

						ListView_GetItem(tmp->window, &lvi);
						ListView_SetSelectionMark(tmp->window, iItem);
					}

					/* Seems to be having lParam as 1 which really sucks */
					if(lvi.lParam < 100)
						lvi.lParam = 0;

					containercontextfunc(tmp->window, (char *)lvi.lParam, x, y, tmp->data);
					tmp = NULL;
				}
				if(tmp)
					tmp = tmp->next;
			}
		}
		break;
	}

	if(!cinfo || !cinfo->pOldProc)
		return DefWindowProc(hWnd, msg, mp1, mp2);
	return CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);
}

BOOL CALLBACK _treewndproc(HWND hWnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	ContainerInfo *cinfo;

	cinfo = (ContainerInfo *)GetWindowLong(hWnd, GWL_USERDATA);

	switch( msg )
	{
	case WM_CHAR:
		if(LOWORD(mp1) == '\t')
		{
			if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
				_shift_focus_back(hWnd);
			else
				_shift_focus(hWnd);
			return FALSE;
		}
		break;
	}

	if(!cinfo || !cinfo->pOldProc)
		return DefWindowProc(hWnd, msg, mp1, mp2);
	return CallWindowProc(cinfo->pOldProc, hWnd, msg, mp1, mp2);
}

void _changebox(Box *thisbox, int percent, int type)
{
	int z;

	for(z=0;z<thisbox->count;z++)
	{
		if(thisbox->items[z].type == TYPEBOX)
		{
			Box *tmp = (Box*)GetWindowLong(thisbox->items[z].hwnd, GWL_USERDATA);
			_changebox(tmp, percent, type);
		}
		else
		{
			if(type == BOXHORZ)
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
	if(type == BOXHORZ)
	{
		int newx = x - SPLITBAR_WIDTH, newy = y;
		float ratio = (float)percent/(float)100.0;
		HWND handle = (HWND)dw_window_get_data(hwnd, "_dw_topleft");
		Box *tmp = (Box *)GetWindowLong(handle, GWL_USERDATA);

		newx = (int)((float)newx * ratio);

		SetWindowPos(handle, (HWND)NULL, 0, 0, newx, y, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		_do_resize(tmp, newx, y);

		handle = (HWND)dw_window_get_data(hwnd, "_dw_bottomright");
		tmp = (Box *)GetWindowLong(handle, GWL_USERDATA);

		newx = x - newx - SPLITBAR_WIDTH;

		SetWindowPos(handle, (HWND)NULL, x - newx, 0, newx, y, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		_do_resize(tmp, newx, y);

		dw_window_set_data(hwnd, "_dw_start", (void *)newx);
	}
	else
	{
		int newx = x, newy = y - SPLITBAR_WIDTH;
		float ratio = (float)(100.0-percent)/(float)100.0;
		HWND handle = (HWND)dw_window_get_data(hwnd, "_dw_bottomright");
		Box *tmp = (Box *)GetWindowLong(handle, GWL_USERDATA);

		newy = (int)((float)newy * ratio);

		SetWindowPos(handle, (HWND)NULL, 0, y - newy, x, newy, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		_do_resize(tmp, x, newy);

		handle = (HWND)dw_window_get_data(hwnd, "_dw_topleft");
		tmp = (Box *)GetWindowLong(handle, GWL_USERDATA);

		newy = y - newy - SPLITBAR_WIDTH;

		SetWindowPos(handle, (HWND)NULL, 0, 0, x, newy, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		_do_resize(tmp, x, newy);

		dw_window_set_data(hwnd, "_dw_start", (void *)newy);
	}
}

/* This handles any activity on the splitbars (sizers) */
BOOL CALLBACK _splitwndproc(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	float *percent = (float *)dw_window_get_data(hwnd, "_dw_percent");
	int type = (int)dw_window_get_data(hwnd, "_dw_type");
	int start = (int)dw_window_get_data(hwnd, "_dw_start");

	switch (msg)
	{
	case WM_ACTIVATE:
	case WM_SETFOCUS:
		return FALSE;

	case WM_SIZE:
		{
			int x = LOWORD(mp2), y = HIWORD(mp2);

			if(x > 0 && y > 0 && percent)
				_handle_splitbar_resize(hwnd, *percent, type, x, y);
    	}
		break;
	case WM_LBUTTONDOWN:
		{
			SetCapture(hwnd);
			break;
		}
	case WM_LBUTTONUP:
		{
            if(GetCapture() == hwnd)
				ReleaseCapture();
		}
		break;
	case WM_MOUSEMOVE:
		{
			if(type == BOXHORZ)
				SetCursor(LoadCursor(NULL, IDC_SIZEWE));
			else
				SetCursor(LoadCursor(NULL, IDC_SIZENS));

			if(GetCapture() == hwnd && percent)
			{
				POINT point;
				RECT rect;

				GetCursorPos(&point);
				GetWindowRect(hwnd, &rect);

				if(PtInRect(&rect, point))
				{
					int width = (rect.right - rect.left);
					int height = (rect.bottom - rect.top);

					if(type == BOXHORZ)
					{
						start = point.x - rect.left;
						if(width - SPLITBAR_WIDTH > 1 && start < width - SPLITBAR_WIDTH)
							*percent = ((float)start / (float)(width - SPLITBAR_WIDTH)) * 100.0;
					}
					else
					{
						start = point.y - rect.top;
						if(height - SPLITBAR_WIDTH > 1 && start < height - SPLITBAR_WIDTH)
							*percent = ((float)start / (float)(height - SPLITBAR_WIDTH)) * 100.0;
					}
					_handle_splitbar_resize(hwnd, *percent, type, width, height);
				}
			}
			break;
		}
	}
	return DefWindowProc(hwnd, msg, mp1, mp2);
}

/* This handles drawing the status text areas */
BOOL CALLBACK _statuswndproc(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	switch (msg)
	{
	case WM_SETTEXT:
		{
			/* Make sure the control redraws when there is a text change */
			int ret = (int)DefWindowProc(hwnd, msg, mp1, mp2);

			InvalidateRgn(hwnd, NULL, TRUE);
			return ret;
		}
	case WM_PAINT:
		{
			HDC hdcPaint;
			PAINTSTRUCT ps;
			RECT rc;
			HFONT hFont;
			HBRUSH oldBrush;
			HPEN oldPen;
			unsigned long cx, cy;
			int threadid = dw_thread_id();
			char tempbuf[1024] = "";

			if(threadid < 0 || threadid >= THREAD_LIMIT)
				threadid = 0;

			hdcPaint = BeginPaint(hwnd, &ps);
			EndPaint(hwnd, &ps);

			hdcPaint = GetDC(hwnd);

			oldBrush = _hBrush[threadid];
			oldPen = _hPen[threadid];

			dw_window_get_pos_size(hwnd, NULL, NULL, &cx, &cy);

 
			_hBrush[threadid] = CreateSolidBrush(GetSysColor(COLOR_3DFACE));

			dw_draw_rect(hwnd, 0, TRUE, 0, 0, cx, cy);

			_hPen[threadid] = CreatePen(PS_SOLID, 1, RGB(_red[DW_CLR_DARKGRAY],
														 _green[DW_CLR_DARKGRAY],
														 _blue[DW_CLR_DARKGRAY]));

			dw_draw_line(hwnd, 0, 0, 0, cx, 0);
			dw_draw_line(hwnd, 0, 0, 0, 0, cy);

			DeleteObject(_hPen[threadid]);

			_hPen[threadid] = GetStockObject(WHITE_PEN);

			dw_draw_line(hwnd, 0, cx - 1, cy - 1, cx - 1, 0);
			dw_draw_line(hwnd, 0, cx - 1, cy - 1, 0, cy - 1);

			rc.left = 3;
			rc.top = 1;
			rc.bottom = cy - 1;
			rc.right = cx - 1;

			GetWindowText(hwnd, tempbuf, 1024);

			hFont = (HFONT)SelectObject(hdcPaint, GetStockObject(DEFAULT_GUI_FONT));

			SetTextColor(hdcPaint, RGB(0,0,0));
			SetBkMode(hdcPaint, TRANSPARENT);

			ExtTextOut(hdcPaint, 3, 1, ETO_CLIPPED, &rc, tempbuf, strlen(tempbuf), NULL);

			SelectObject(hdcPaint, hFont);

			DeleteObject(_hBrush[threadid]);
			_hBrush[threadid] = oldBrush;
			_hPen[threadid] = oldPen;
			ReleaseDC(hwnd, hdcPaint);
		}
		return FALSE;
	}
	return DefWindowProc(hwnd, msg, mp1, mp2);
}

/* Function: _BtProc
 * Abstract: Subclass procedure for buttons
 */

BOOL CALLBACK _BtProc(HWND hwnd, ULONG msg, WPARAM mp1, LPARAM mp2)
{
	BubbleButton *bubble;
	static int bMouseOver = 0;
	POINT point;
	RECT rect;
	WNDPROC pOldProc;

	bubble = (BubbleButton *)GetWindowLong(hwnd, GWL_USERDATA);

	if(!bubble)
		return DefWindowProc(hwnd, msg, mp1, mp2);

	/* We must save a pointer to the old
	 * window procedure because if a signal
	 * handler attached here destroys this
	 * window it will then be invalid.
	 */
	pOldProc = bubble->pOldProc;

	switch(msg)
	{
	case WM_SETFOCUS:
		_wndproc(hwnd, msg, mp1, mp2);
		break;
	case WM_LBUTTONUP:
		{
			SignalHandler *tmp = Root;

			/* Find any callbacks for this function */
			while(tmp)
			{
				if(tmp->message == WM_COMMAND)
				{
					int (*clickfunc)(HWND, void *) = tmp->signalfunction;

					/* Make sure it's the right window, and the right ID */
					if(tmp->window == hwnd)
					{
						if(bubble->checkbox)
							in_checkbox_handler = 1;

						clickfunc(tmp->window, tmp->data);

						if(bubble->checkbox)
							in_checkbox_handler = 0;
						tmp = NULL;
					}
				}
				if(tmp)
					tmp= tmp->next;
			}
		}
		break;
	case WM_CHAR:
		{
			/* A button press should also occur for an ENTER or SPACE press
			 * while the button has the active input focus.
			 */
			if(LOWORD(mp1) == '\r' || LOWORD(mp1) == ' ')
			{
				SignalHandler *tmp = Root;

				/* Find any callbacks for this function */
				while(tmp)
				{
					if(tmp->message == WM_COMMAND)
					{
						int (*clickfunc)(HWND, void *) = tmp->signalfunction;

						/* Make sure it's the right window, and the right ID */
						if(tmp->window == hwnd)
						{
							clickfunc(tmp->window, tmp->data);
							tmp = NULL;
						}
					}
					if(tmp)
						tmp= tmp->next;
				}
			}
			if(LOWORD(mp1) == '\t')
			{
				if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
					_shift_focus_back(hwnd);
				else
					_shift_focus(hwnd);
				return FALSE;
			}
		}
		break;
	case WM_KEYDOWN:
		if(mp1 == VK_LEFT || mp1 == VK_UP)
			_shift_focus_back(hwnd);
		if(mp1 == VK_RIGHT || mp1 == VK_DOWN)
			_shift_focus(hwnd);
		break;
	case WM_TIMER:
		if (hwndBubble)
		{
			_free_window_memory(hwndBubble, 0);
			DestroyWindow(hwndBubble);
			hwndBubble = 0;
			KillTimer(hwnd, 1);
		}
		break;

	case WM_MOUSEMOVE:
		GetCursorPos(&point);
		GetWindowRect(hwnd, &rect);

		if(PtInRect(&rect, point))
		{
			if(hwnd != GetCapture())
			{
				SetCapture(hwnd);
			}
			if(!bMouseOver)
			{
				bMouseOver = 1;
				if(!*bubble->bubbletext)
					break;

				if(hwndBubble)
				{
					_free_window_memory(hwndBubble, 0);
					DestroyWindow(hwndBubble);
					hwndBubble = 0;
					KillTimer(hwndBubbleLast, 1);
				}

				if(!hwndBubble)
				{
					POINTL ptlWork = {0,0};
					ULONG ulColor = DW_CLR_YELLOW;
					SIZE size;
					HFONT hFont, oldFont = (HFONT)0;
					HDC hdc;
					RECT rect;
					void *oldproc;

					/* Use the WS_EX_TOOLWINDOW extended style
					 * so the window doesn't get listed in the
					 * taskbar.
					 */
					hwndBubble = CreateWindowEx(WS_EX_TOOLWINDOW,
												STATICCLASSNAME,
												bubble->bubbletext,
												BS_TEXT | WS_POPUP |
												WS_BORDER |
												SS_CENTER,
												0,0,50,20,
												HWND_DESKTOP,
												NULL,
												DWInstance,
												NULL);

					dw_window_set_font(hwndBubble, DefaultFont);
					dw_window_set_color(hwndBubble, DW_CLR_BLACK, DW_CLR_YELLOW);

					hwndBubbleLast = hwnd;

					SetTimer(hwnd, 1, 3000, NULL);

					hFont = (HFONT)SendMessage(hwndBubble, WM_GETFONT, 0, 0);

					hdc = GetDC(hwndBubble);

					if(hFont)
						oldFont = (HFONT)SelectObject(hdc, hFont);

					GetTextExtentPoint32(hdc, bubble->bubbletext, strlen(bubble->bubbletext), &size);

					if(hFont)
						SelectObject(hdc, oldFont);

					MapWindowPoints(hwnd, HWND_DESKTOP, (LPPOINT)&ptlWork, 1);

					GetWindowRect(hwnd, &rect);

					SetWindowPos(hwndBubble,
								 HWND_TOP,
								 ptlWork.x,
								 ptlWork.y + (rect.bottom-rect.top) + 1,
								 size.cx + 8,
								 size.cy + 2,
								 SWP_NOACTIVATE | SWP_SHOWWINDOW);

					ReleaseDC(hwndBubble, hdc);
				}
			}
		}
		else{
			/* Calling ReleaseCapture in Win95 also causes WM_CAPTURECHANGED
			 * to be sent.  Be sure to account for that.
			 */
			ReleaseCapture();

			if(bMouseOver)
			{
				bMouseOver = 0;
				_free_window_memory(hwndBubble, 0);
				DestroyWindow(hwndBubble);
				hwndBubble = 0;
				KillTimer(hwndBubbleLast, 1);
			}
		}
		break;
	case WM_CAPTURECHANGED:
		/* This message means we are losing the capture for some reason
		 * Either because we intentionally lost it or another window
		 * stole it
		 */
		if(bMouseOver)
		{
			bMouseOver = 0;
			_free_window_memory(hwndBubble, 0);
			DestroyWindow(hwndBubble);
			hwndBubble = 0;
			KillTimer(hwndBubbleLast, 1);
		}
		break;
	}

	if(!pOldProc)
		return DefWindowProc(hwnd, msg, mp1, mp2);
	return CallWindowProc(pOldProc, hwnd, msg, mp1, mp2);
}

/* This function recalculates a notebook page for example
 * during switching of notebook pages.
 */
void _resize_notebook_page(HWND handle, int pageid)
{
	RECT rect;
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);

	if(array && array[pageid])
	{
		Box *box = (Box *)GetWindowLong(array[pageid]->hwnd, GWL_USERDATA);

		GetClientRect(handle,&rect);
		TabCtrl_AdjustRect(handle,FALSE,&rect);
		MoveWindow(array[pageid]->hwnd, rect.left, rect.top,
				   rect.right - rect.left, rect.bottom-rect.top, TRUE);
		if(box && box->count)
		{
			ShowWindow(box->items[0].hwnd, SW_HIDE);
			_do_resize(box, rect.right - rect.left, rect.bottom - rect.top);
			ShowWindow(box->items[0].hwnd, SW_SHOW);
		}

		ShowWindow(array[pageid]->hwnd, SW_SHOWNORMAL);
	}
}

/*
 * Initializes the Dynamic Windows engine.
 * Parameters:
 *           newthread: True if this is the only thread.
 *                      False if there is already a message loop running.
 */
int dw_init(int newthread, int argc, char *argv[])
{
	WNDCLASS wc;
	int z;
	INITCOMMONCONTROLSEX icc;

	icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icc.dwICC = ICC_WIN95_CLASSES;

	InitCommonControlsEx(&icc);

	memset(lookup, 0, sizeof(HICON) * ICON_INDEX_LIMIT);

	/* Register the generic Dynamic Windows class */
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)_wndproc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 32;
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = ClassName;

	RegisterClass(&wc);

	/* Register the splitbar control */
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC)_splitwndproc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_3DFACE);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = SplitbarClassName;

	RegisterClass(&wc);

	/* Register a frame control like on OS/2 */
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.style = CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)_framewndproc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 32;
	wc.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_3DFACE);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = FRAMECLASSNAME;

	RegisterClass(&wc);

	/* Create a set of brushes using the default OS/2 and DOS colors */
	for(z=0;z<18;z++)
		_colors[z] = CreateSolidBrush(RGB(_red[z],_green[z],_blue[z]));

	/* Register an Object Windows class like OS/2 and Win2k+
	 * so similar functionality can be used on earlier releases
	 * of Windows.
	 */
	memset(&wc, 0, sizeof(WNDCLASS));
	wc.style = 0;
	wc.lpfnWndProc = (WNDPROC)_wndproc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = ObjectClassName;

	RegisterClass(&wc);

	/* Since Windows 95/98/NT don't have a HWND_OBJECT class
	 * also known as a input only window, I will create a
	 * temporary window that isn't visible and really does nothing
	 * except temporarily hold the child windows before they are
	 * packed into their correct parent.
	 */

	DW_HWND_OBJECT = CreateWindow(ObjectClassName, "", 0, 0, 0,
								  0, 0, HWND_DESKTOP, NULL, DWInstance, NULL);

	if(!DW_HWND_OBJECT)
	{
		dw_messagebox("Dynamic Windows", "Could not initialize the object window. error code %d", GetLastError());
		exit(1);
	}

#ifdef DWDEBUG
	f = fopen("dw.log", "wt");
#endif
	/* We need the version to check capability like up-down controls */
	dwVersion = GetVersion();
	dwComctlVer = GetDllVersion(TEXT("comctl32.dll"));

	for(z=0;z<THREAD_LIMIT;z++)
	{
		_foreground[z] = RGB(128,128,128);
		_background[z] = 0;
		_hPen[z] = CreatePen(PS_SOLID, 1, _foreground[z]);
		_hBrush[z] = CreateSolidBrush(_foreground[z]);
	}

	return 0;
}

/*
 * Runs a message loop for Dynamic Windows.
 * Parameters:
 *           currenthab: The handle to the current anchor block
 *                       or NULL if this DW is handling the message loop.
 *           func: Function pointer to the message filter function.
 */
void dw_main(HAB currenthab, void *func)
{
	MSG msg;

	/* Setup the filter function */
	filterfunc = func;

	_dwtid = dw_thread_id();

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

#ifdef DWDEBUG
	fclose(f);
#endif
}

/*
 * Runs a message loop for Dynamic Windows, for a period of milliseconds.
 * Parameters:
 *           milliseconds: Number of milliseconds to run the loop for.
 */
void dw_main_sleep(int milliseconds)
{
	MSG msg;
	double start = (double)clock();

	while(((clock() - start) / (CLOCKS_PER_SEC/1000)) <= milliseconds)
	{
		if(PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
		{
			GetMessage(&msg, NULL, 0, 0);
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
			Sleep(1);
	}
}

/*
 * Free's memory allocated by dynamic windows.
 * Parameters:
 *           ptr: Pointer to dynamic windows allocated
 *                memory to be free()'d.
 */
void dw_free(void *ptr)
{
	free(ptr);
}

/*
 * Allocates and initializes a dialog struct.
 * Parameters:
 *           data: User defined data to be passed to functions.
 */
DWDialog *dw_dialog_new(void *data)
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
int dw_dialog_dismiss(DWDialog *dialog, void *result)
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
void *dw_dialog_wait(DWDialog *dialog)
{
	MSG msg;
	void *tmp;

	while (GetMessage(&msg,NULL,0,0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
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
 *           format: printf style format string.
 *           ...: Additional variables for use in the format.
 */
int dw_messagebox(char *title, char *format, ...)
{
	va_list args;
	char outbuf[256];

	va_start(args, format);
	vsprintf(outbuf, format, args);
	va_end(args);

	MessageBox(HWND_DESKTOP, outbuf, title, MB_OK);

	return strlen(outbuf);
}

/*
 * Displays a Message Box with given text and title..
 * Parameters:
 *           title: The title of the message box.
 *           text: The text to display in the box.
 * Returns:
 *           True if YES False of NO.
 */
int dw_yesno(char *title, char *text)
{
	if(MessageBox(HWND_DESKTOP, text, title, MB_YESNO) == IDYES)
		return TRUE;
	return FALSE;
}

/*
 * Minimizes or Iconifies a top-level window.
 * Parameters:
 *           handle: The window handle to minimize.
 */
int dw_window_minimize(HWND handle)
{
	return ShowWindow(handle, SW_MINIMIZE);
}

/*
 * Makes the window topmost.
 * Parameters:
 *           handle: The window handle to make topmost.
 */
int dw_window_raise(HWND handle)
{
	return SetWindowPos(handle, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Makes the window bottommost.
 * Parameters:
 *           handle: The window handle to make bottommost.
 */
int dw_window_lower(HWND handle)
{
	return SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Makes the window visible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int dw_window_show(HWND handle)
{
	int rc = ShowWindow(handle, SW_SHOW);
	SetFocus(handle);
	_initial_focus(handle);
	return rc;
}

/*
 * Makes the window invisible.
 * Parameters:
 *           handle: The window handle to make visible.
 */
int dw_window_hide(HWND handle)
{
	return ShowWindow(handle, SW_HIDE);
}

/*
 * Destroys a window and all of it's children.
 * Parameters:
 *           handle: The window handle to destroy.
 */
int dw_window_destroy(HWND handle)
{
	HWND parent = GetParent(handle);
	Box *thisbox = (Box *)GetWindowLong(parent, GWL_USERDATA);

	if(parent != HWND_DESKTOP && thisbox && thisbox->count)
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
	}
	return DestroyWindow(handle);
}

/* Causes entire window to be invalidated and redrawn.
 * Parameters:
 *           handle: Toplevel window handle to be redrawn.
 */
void dw_window_redraw(HWND handle)
{
	Box *mybox = (Box *)GetWindowLong(handle, GWL_USERDATA);

	if(mybox)
	{
		RECT rect;

		GetClientRect(handle, &rect);

		ShowWindow(mybox->items[0].hwnd, SW_HIDE);
		_do_resize(mybox, rect.right - rect.left, rect.bottom - rect.top);
		ShowWindow(mybox->items[0].hwnd, SW_SHOW);
	}
}

/*
 * Changes a window's parent to newparent.
 * Parameters:
 *           handle: The window handle to destroy.
 *           newparent: The window's new parent window.
 */
void dw_window_reparent(HWND handle, HWND newparent)
{
	SetParent(handle, newparent);
}

HFONT _acquire_font(HWND handle, char *fontname)
{
	HFONT hfont;
	int z, size = 9;
	LOGFONT lf;

	if(fontname == DefaultFont || !fontname[0])
		hfont = GetStockObject(DEFAULT_GUI_FONT);
	else
	{
#if 0
		HDC hDC = GetDC(handle);
#endif
		for(z=0;z<strlen(fontname);z++)
		{
			if(fontname[z]=='.')
				break;
		}
		size = atoi(fontname) + 5;

#if 0
		lf.lfHeight = -MulDiv(size, GetDeviceCaps(hDC, LOGPIXELSY), 72);
#endif
		lf.lfHeight = size;
		lf.lfWidth = 0;
		lf.lfEscapement = 0;
		lf.lfOrientation = 0;
		lf.lfItalic = 0;
		lf.lfUnderline = 0;
		lf.lfStrikeOut = 0;
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfOutPrecision = 0;
		lf.lfClipPrecision = 0;
		lf.lfQuality = DEFAULT_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FW_DONTCARE;
		strcpy(lf.lfFaceName, &fontname[z+1]);

		hfont = CreateFontIndirect(&lf);
#if 0
		ReleaseDC(handle, hDC);
#endif
	}
	return hfont;
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          fontname: Name and size of the font in the form "size.fontname"
 */
int dw_window_set_font(HWND handle, char *fontname)
{
	HFONT hfont = _acquire_font(handle, fontname);
	ColorInfo *cinfo;

	cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

	if(fontname)
	{
		if(cinfo)
		{
			strcpy(cinfo->fontname, fontname);
		}
		else
		{
			cinfo = calloc(1, sizeof(ColorInfo));
			cinfo->fore = cinfo->back = -1;
			cinfo->buddy = 0;

			strcpy(cinfo->fontname, fontname);

			cinfo->pOldProc = SubclassWindow(handle, _colorwndproc);
			SetWindowLong(handle, GWL_USERDATA, (ULONG)cinfo);
		}
	}
	SendMessage(handle, WM_SETFONT, (WPARAM)hfont, (LPARAM)TRUE);
	return 0;
}

/*
 * Sets the colors used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          fore: Foreground color in RGB format.
 *          back: Background color in RGB format.
 */
int dw_window_set_color(HWND handle, ULONG fore, ULONG back)
{
	ColorInfo *cinfo;
	char tmpbuf[100];

	cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, WC_LISTVIEW, strlen(WC_LISTVIEW))==0)
	{
		ListView_SetTextColor(handle, RGB(DW_RED_VALUE(fore),
										  DW_GREEN_VALUE(fore),
										  DW_BLUE_VALUE(fore)));
		ListView_SetTextBkColor(handle, RGB(DW_RED_VALUE(back),
											DW_GREEN_VALUE(back),
											DW_BLUE_VALUE(back)));
		ListView_SetBkColor(handle, RGB(DW_RED_VALUE(back),
										DW_GREEN_VALUE(back),
										DW_BLUE_VALUE(back)));
		InvalidateRgn(handle, NULL, TRUE);
		return TRUE;
	}

	if(cinfo)
	{
		cinfo->fore = fore;
		cinfo->back = back;
	}
	else
	{
		cinfo = calloc(1, sizeof(ColorInfo));

		cinfo->fore = fore;
		cinfo->back = back;
		cinfo->buddy = 0;

		cinfo->pOldProc = SubclassWindow(handle, _colorwndproc);
		SetWindowLong(handle, GWL_USERDATA, (ULONG)cinfo);
	}
	InvalidateRgn(handle, NULL, TRUE);
	return TRUE;
}

/*
 * Sets the font used by a specified window (widget) handle.
 * Parameters:
 *          handle: The window (widget) handle.
 *          border: Size of the window border in pixels.
 */
int dw_window_set_border(HWND handle, int border)
{
	return 0;
}

/*
 * Captures the mouse input to this window.
 * Parameters:
 *       handle: Handle to receive mouse input.
 */
void dw_window_capture(HWND handle)
{
	SetCapture(handle);
}

/*
 * Releases previous mouse capture.
 */
void dw_window_release(void)
{
	ReleaseCapture();
}

/*
 * Changes the appearance of the mouse pointer.
 * Parameters:
 *       handle: Handle to widget for which to change.
 *       cursortype: ID of the pointer you want.
 */
void dw_window_pointer(HWND handle, int pointertype)
{
	SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(pointertype)));
}

/*
 * Create a new Window Frame.
 * Parameters:
 *       owner: The Owner's window handle or HWND_DESKTOP.
 *       title: The Window title.
 *       flStyle: Style flags, see the DW reference.
 */
HWND dw_window_new(HWND hwndOwner, char *title, ULONG flStyle)
{
	HWND hwndframe;
	Box *newbox = calloc(sizeof(Box), 1);
	ULONG flStyleEx = 0;

	newbox->pad = 0;
	newbox->type = BOXVERT;
	newbox->count = 0;
	newbox->cinfo.fore = newbox->cinfo.back = -1;

	if(hwndOwner)
		flStyleEx |= WS_EX_MDICHILD;

	if(!(flStyle & WS_CAPTION))
		flStyle |= WS_POPUPWINDOW;

	if(flStyle & DW_FCF_TASKLIST)
	{
		ULONG newflags = (flStyle | WS_CLIPCHILDREN) & ~DW_FCF_TASKLIST;

		hwndframe = CreateWindowEx(flStyleEx, ClassName, title, newflags, CW_USEDEFAULT, CW_USEDEFAULT,
								   CW_USEDEFAULT, CW_USEDEFAULT, hwndOwner, NULL, DWInstance, NULL);
	}
	else
	{
		flStyleEx |= WS_EX_TOOLWINDOW;

		hwndframe = CreateWindowEx(flStyleEx, ClassName, title, flStyle | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
								   CW_USEDEFAULT, CW_USEDEFAULT, hwndOwner, NULL, DWInstance, NULL);
	}
	SetWindowLong(hwndframe, GWL_USERDATA, (ULONG)newbox);

#if 0
	if(hwndOwner)
		SetParent(hwndframe, hwndOwner);
#endif

	return hwndframe;
}

/*
 * Create a new Box to be packed.
 * Parameters:
 *       type: Either BOXVERT (vertical) or BOXHORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 */
HWND dw_box_new(int type, int pad)
{
	Box *newbox = calloc(sizeof(Box), 1);
	HWND hwndframe;

	newbox->pad = pad;
	newbox->type = type;
	newbox->count = 0;
	newbox->grouphwnd = (HWND)NULL;
	newbox->cinfo.fore = newbox->cinfo.back = -1;

	hwndframe = CreateWindow(FRAMECLASSNAME,
							 "",
							 WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
							 0,0,2000,1000,
							 DW_HWND_OBJECT,
							 NULL,
							 DWInstance,
							 NULL);

	newbox->cinfo.pOldProc = SubclassWindow(hwndframe, _colorwndproc);
	newbox->cinfo.fore = newbox->cinfo.back = -1;

	SetWindowLong(hwndframe, GWL_USERDATA, (ULONG)newbox);
	return hwndframe;
}

/*
 * Create a new Group Box to be packed.
 * Parameters:
 *       type: Either BOXVERT (vertical) or BOXHORZ (horizontal).
 *       pad: Number of pixels to pad around the box.
 *       title: Text to be displayined in the group outline.
 */
HWND dw_groupbox_new(int type, int pad, char *title)
{
	Box *newbox = calloc(sizeof(Box), 1);
	HWND hwndframe;

	newbox->pad = pad;
	newbox->type = type;
	newbox->count = 0;
	newbox->cinfo.fore = newbox->cinfo.back = -1;

	hwndframe = CreateWindow(FRAMECLASSNAME,
							 "",
							 WS_VISIBLE | WS_CHILD,
							 0,0,2000,1000,
							 DW_HWND_OBJECT,
							 NULL,
							 DWInstance,
							 NULL);

	newbox->grouphwnd = CreateWindow(BUTTONCLASSNAME,
									 title,
									 WS_CHILD | BS_GROUPBOX |
									 WS_VISIBLE | WS_CLIPCHILDREN,
									 0,0,2000,1000,
									 hwndframe,
									 NULL,
									 DWInstance,
									 NULL);

	SetWindowLong(hwndframe, GWL_USERDATA, (ULONG)newbox);
	dw_window_set_font(newbox->grouphwnd, DefaultFont);
	return hwndframe;
}

/*
 * Create a new MDI Frame to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id or 0L.
 */
HWND dw_mdi_new(unsigned long id)
{
	CLIENTCREATESTRUCT ccs;
	HWND hwndframe;

	ccs.hWindowMenu = NULL;
	ccs.idFirstChild = 0;

	hwndframe = CreateWindow("MDICLIENT",
							 "",
							 WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS,
							 0,0,2000,1000,
							 DW_HWND_OBJECT,
							 (HMENU)id,
							 DWInstance,
							 &ccs);
	return hwndframe;
}

/*
 * Create a bitmap object to be packed.
 * Parameters:
 *       id: An ID to be used with dw_window_from_id or 0L.
 */
HWND dw_bitmap_new(ULONG id)
{
	return CreateWindow(STATICCLASSNAME,
						"",
						SS_BITMAP | WS_VISIBLE |
						WS_CHILD | WS_CLIPCHILDREN,
						0,0,2000,1000,
						DW_HWND_OBJECT,
						(HMENU)id,
						DWInstance,
						NULL);
}

/*
 * Create a notebook object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND dw_notebook_new(ULONG id, int top)
{
	ULONG flags = 0;
	HWND tmp;
	NotebookPage **array = calloc(256, sizeof(NotebookPage *));

	if(!top)
		flags = TCS_BOTTOM;

	tmp = CreateWindow(WC_TABCONTROL,
					   "",
					   WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
					   0,0,2000,1000,
					   DW_HWND_OBJECT,
					   (HMENU)id,
					   DWInstance,
					   NULL);
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)array);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a menu object to be popped up.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HMENUI dw_menu_new(ULONG id)
{
	HMENUI tmp = malloc(sizeof(struct _hmenui));

	if(!tmp)
		return NULL;

	tmp->menu = CreatePopupMenu();
	tmp->hwnd = NULL;
	return tmp;
}

/*
 * Create a menubar on a window.
 * Parameters:
 *       location: Handle of a window frame to be attached to.
 */
HMENUI dw_menubar_new(HWND location)
{
	HMENUI tmp = malloc(sizeof(struct _hmenui));

	if(!tmp)
		return NULL;

	tmp->menu = CreateMenu();
	tmp->hwnd = location;

	SetMenu(location, tmp->menu);
	return tmp;
}

/*
 * Destroys a menu created with dw_menubar_new or dw_menu_new.
 * Parameters:
 *       menu: Handle of a menu.
 */
void dw_menu_destroy(HMENUI *menu)
{
	if(menu && *menu)
	{
		DestroyMenu((*menu)->menu);
		free(*menu);
		*menu = NULL;
	}
}

/*
 * Adds a menuitem or submenu to an existing menu.
 * Parameters:
 *       menu: The handle the the existing menu.
 *       title: The title text on the menu item to be added.
 *       id: An ID to be used for message passing.
 *       end: If TRUE memu is positioned at the end of the menu.
 *       check: If TRUE menu is "check"able.
 *       flags: Extended attributes to set on the menu.
 *       submenu: Handle to an existing menu to be a submenu or NULL.
 */
HWND dw_menu_append_item(HMENUI menux, char *title, ULONG id, ULONG flags, int end, int check, HMENUI submenu)
{
	MENUITEMINFO mii;
	HMENU menu;

	if(!menux)
		return NULL;

	menu = menux->menu;

	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_TYPE;

	/* Convert from OS/2 style accellerators to Win32 style */
	if(title)
	{
		char *tmp = title;

		while(*tmp)
		{
			if(*tmp == '~')
				*tmp = '&';
			tmp++;
		}
	}

	if(title && *title)
		mii.fType = MFT_STRING;
	else
		mii.fType = MFT_SEPARATOR;

	mii.wID = id;
	mii.hSubMenu = submenu ? submenu->menu : 0;
	mii.dwTypeData = title;
	mii.cch = strlen(title);

	InsertMenuItem(menu, 65535, TRUE, &mii);
	if(menux->hwnd)
		DrawMenuBar(menux->hwnd);
	return (HWND)id;
}

/*
 * Sets the state of a menu item check.
 * Parameters:
 *       menu: The handle the the existing menu.
 *       id: Menuitem id.
 *       check: TRUE for checked FALSE for not checked.
 */
void dw_menu_item_set_check(HMENUI menux, unsigned long id, int check)
{
	MENUITEMINFO mii;
	HMENU menu;

	if(!menux)
		return;

	menu = menux->menu;

	mii.cbSize = sizeof(MENUITEMINFO);
	mii.fMask = MIIM_STATE;
	if(check)
		mii.fState = MFS_CHECKED;
	else
		mii.fState = MFS_UNCHECKED;
	SetMenuItemInfo(menu, id, FALSE, &mii);
}

/*
 * Pops up a context menu at given x and y coordinates.
 * Parameters:
 *       menu: The handle the the existing menu.
 *       parent: Handle to the window initiating the popup.
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void dw_menu_popup(HMENUI *menu, HWND parent, int x, int y)
{
	if(menu && *menu)
	{
		popup = parent;
		TrackPopupMenu((*menu)->menu, 0, x, y, 0, parent, NULL);
		free(*menu);
		*menu = NULL;
	}
}


/*
 * Create a container object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND dw_container_new(ULONG id)
{
	HWND tmp = CreateWindow(WC_LISTVIEW,
							"",
							WS_VISIBLE | WS_CHILD |
							LVS_REPORT | LVS_SHOWSELALWAYS |
							LVS_SHAREIMAGELISTS | WS_BORDER |
							WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	ContainerInfo *cinfo = (ContainerInfo *)calloc(1, sizeof(ContainerInfo));

	if(!cinfo)
	{
		DestroyWindow(tmp);
		return NULL;
	}

	cinfo->pOldProc = (WNDPROC)SubclassWindow(tmp, _containerwndproc);
	cinfo->cinfo.fore = cinfo->cinfo.back = -1;

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a tree object to be packed.
 * Parameters:
 *       id: An ID to be used for getting the resource from the
 *           resource file.
 */
HWND dw_tree_new(ULONG id)
{
	HWND tmp = CreateWindow(WC_TREEVIEW,
							"",
							WS_VISIBLE | WS_CHILD |
							TVS_HASLINES | TVS_SHOWSELALWAYS |
							TVS_HASBUTTONS | TVS_LINESATROOT |
							WS_BORDER | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	ContainerInfo *cinfo = (ContainerInfo *)calloc(1, sizeof(ContainerInfo));
	TreeView_SetItemHeight(tmp, 16);

	if(!cinfo)
	{
		DestroyWindow(tmp);
		return NULL;
	}

	cinfo->pOldProc = (WNDPROC)SubclassWindow(tmp, _treewndproc);
	cinfo->cinfo.fore = cinfo->cinfo.back = -1;

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Returns the current X and Y coordinates of the mouse pointer.
 * Parameters:
 *       x: Pointer to variable to store X coordinate.
 *       y: Pointer to variable to store Y coordinate.
 */
void dw_pointer_query_pos(long *x, long *y)
{
	POINT ptl;

	GetCursorPos(&ptl);
	if(x && y)
	{
		*x = ptl.x;
		*y = ptl.y;
	}
}

/*
 * Sets the X and Y coordinates of the mouse pointer.
 * Parameters:
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void dw_pointer_set_pos(long x, long y)
{
	SetCursorPos(x, y);
}

/*
 * Create a new static text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_text_new(char *text, ULONG id)
{
	HWND tmp = CreateWindow(STATICCLASSNAME,
							text,
							BS_TEXT | WS_VISIBLE |
							WS_CHILD | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new status text window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_status_text_new(char *text, ULONG id)
{
	HWND tmp = CreateWindow(STATICCLASSNAME,
							text,
							BS_TEXT | WS_VISIBLE |
							WS_CHILD | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	dw_window_set_font(tmp, DefaultFont);
	SubclassWindow(tmp, _statuswndproc);
	return tmp;
}

/*
 * Create a new Multiline Editbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_mle_new(ULONG id)
{
    
	HWND tmp = CreateWindowEx(WS_EX_CLIENTEDGE,
							  EDITCLASSNAME,
							  "",
							  WS_VISIBLE | WS_BORDER |
							  WS_VSCROLL | ES_MULTILINE |
							  ES_WANTRETURN | WS_CHILD |
							  WS_CLIPCHILDREN,
							  0,0,2000,1000,
							  DW_HWND_OBJECT,
							  (HMENU)id,
							  DWInstance,
							  NULL);
	ContainerInfo *cinfo = (ContainerInfo *)calloc(1, sizeof(ContainerInfo));

	if(!cinfo)
	{
		DestroyWindow(tmp);
		return NULL;
	}

	cinfo->pOldProc = (WNDPROC)SubclassWindow(tmp, _treewndproc);
	cinfo->cinfo.fore = cinfo->cinfo.back = -1;

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new Entryfield window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_entryfield_new(char *text, ULONG id)
{
	HWND tmp = CreateWindowEx(WS_EX_CLIENTEDGE,
							  EDITCLASSNAME,
							  text,
							  ES_WANTRETURN | WS_CHILD |
							  WS_BORDER | ES_AUTOHSCROLL |
							  WS_VISIBLE | WS_CLIPCHILDREN,
							  0,0,2000,1000,
							  DW_HWND_OBJECT,
							  (HMENU)id,
							  DWInstance,
							  NULL);
	ColorInfo *cinfo = calloc(1, sizeof(ColorInfo));

	cinfo->back = cinfo->fore = -1;
	cinfo->buddy = 0;

	cinfo->pOldProc = SubclassWindow(tmp, _colorwndproc);
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new Entryfield passwird window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the entryfield widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_entryfield_password_new(char *text, ULONG id)
{
	HWND tmp = CreateWindowEx(WS_EX_CLIENTEDGE,
							  EDITCLASSNAME,
							  text,
							  ES_WANTRETURN | WS_CHILD |
							  ES_PASSWORD | WS_BORDER | WS_VISIBLE |
							  ES_AUTOHSCROLL | WS_CLIPCHILDREN,
							  0,0,2000,1000,
							  DW_HWND_OBJECT,
							  (HMENU)id,
							  DWInstance,
							  NULL);
	ColorInfo *cinfo = calloc(1, sizeof(ColorInfo));

	cinfo->back = cinfo->fore = -1;
	cinfo->buddy = 0;

	cinfo->pOldProc = SubclassWindow(tmp, _colorwndproc);
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

BOOL CALLBACK _subclass_child(HWND handle, LPARAM lp)
{
	ColorInfo *cinfo = (ColorInfo *)lp;

	if(cinfo)
	{
		cinfo->buddy = handle;
		cinfo->pOldProc = (WNDPROC)SubclassWindow(handle, _colorwndproc);
		SetWindowLong(handle, GWL_USERDATA, (ULONG)cinfo);
	}
	return FALSE;
}

/*
 * Create a new Combobox window (widget) to be packed.
 * Parameters:
 *       text: The default text to be in the combpbox widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_combobox_new(char *text, ULONG id)
{
	HWND tmp = CreateWindow(COMBOBOXCLASSNAME,
							text,
							WS_CHILD | CBS_DROPDOWN | WS_VSCROLL |
							WS_CLIPCHILDREN | WS_VISIBLE,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	ColorInfo *cinfo = (ColorInfo *)calloc(1, sizeof(ColorInfo));
	ColorInfo *cinfo2 = (ColorInfo *)calloc(1, sizeof(ColorInfo));

	if(!cinfo || !cinfo2)
	{
		if(cinfo)
			free(cinfo);
		if(cinfo2)
			free(cinfo2);
		DestroyWindow(tmp);
		return NULL;
	}

	cinfo2->fore = cinfo->fore = -1;
	cinfo2->back = cinfo->back = -1;
	cinfo2->combo = cinfo->combo = tmp;
	EnumChildWindows(tmp, _subclass_child, (LPARAM)cinfo2);

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new button window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_button_new(char *text, ULONG id)
{
	BubbleButton *bubble = calloc(1, sizeof(BubbleButton));

	HWND tmp = CreateWindow(BUTTONCLASSNAME,
							text,
							WS_CHILD | BS_PUSHBUTTON |
							WS_VISIBLE | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);

	bubble->id = id;
	bubble->bubbletext[0] = '\0';
	bubble->pOldProc = (WNDPROC)SubclassWindow(tmp, _BtProc);

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)bubble);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new bitmap button window (widget) to be packed.
 * Parameters:
 *       text: Bubble help text to be displayed.
 *       id: An ID of a bitmap in the resource file.
 */
HWND dw_bitmapbutton_new(char *text, ULONG id)
{
	HWND tmp;
	BubbleButton *bubble = calloc(1, sizeof(BubbleButton));
	HBITMAP hbitmap = LoadBitmap(DWInstance, MAKEINTRESOURCE(id));

	tmp = CreateWindow(BUTTONCLASSNAME,
					   "",
					   WS_CHILD | BS_PUSHBUTTON |
					   BS_BITMAP | WS_CLIPCHILDREN |
					   WS_VISIBLE,
					   0,0,2000,1000,
					   DW_HWND_OBJECT,
					   (HMENU)id,
					   DWInstance,
					   NULL);

	bubble->id = id;
	strncpy(bubble->bubbletext, text, BUBBLE_HELP_MAX - 1);
	bubble->bubbletext[BUBBLE_HELP_MAX - 1] = '\0';
	bubble->pOldProc = (WNDPROC)SubclassWindow(tmp, _BtProc);

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)bubble);

	SendMessage(tmp, BM_SETIMAGE,
				(WPARAM) IMAGE_BITMAP,
				(LPARAM) hbitmap);
	return tmp;
}

/*
 * Create a new spinbutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_spinbutton_new(char *text, ULONG id)
{
	ULONG *data = malloc(sizeof(ULONG));
	HWND buddy = CreateWindowEx(WS_EX_CLIENTEDGE,
								EDITCLASSNAME,
								text,
								WS_CHILD | WS_BORDER | WS_VISIBLE |
								ES_NUMBER | WS_CLIPCHILDREN,
								0,0,2000,1000,
								DW_HWND_OBJECT,
								NULL,
								DWInstance,
								NULL);
	HWND tmp = CreateWindowEx(WS_EX_CLIENTEDGE,
							  UPDOWN_CLASS,
							  NULL,
							  WS_CHILD | UDS_ALIGNRIGHT | WS_BORDER |
							  UDS_ARROWKEYS | UDS_SETBUDDYINT |
							  UDS_WRAP | UDS_NOTHOUSANDS | WS_VISIBLE,
							  0,0,2000,1000,
							  DW_HWND_OBJECT,
							  (HMENU)id,
							  DWInstance,
							  NULL);
	ColorInfo *cinfo = calloc(1, sizeof(ColorInfo));

	SendMessage(tmp, UDM_SETBUDDY, (WPARAM)buddy, 0);
	cinfo->back = cinfo->fore = -1;
	cinfo->buddy = tmp;

	cinfo->pOldProc = SubclassWindow(buddy, _colorwndproc);
	SetWindowLong(buddy, GWL_USERDATA, (ULONG)cinfo);

	cinfo = calloc(1, sizeof(ColorInfo));
	cinfo->buddy = buddy;
	cinfo->pOldProc = SubclassWindow(tmp, _spinnerwndproc);

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(buddy, DefaultFont);
	return tmp;
}

/*
 * Create a new radiobutton window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_radiobutton_new(char *text, ULONG id)
{
	HWND tmp = CreateWindow(BUTTONCLASSNAME,
							text,
							WS_CHILD | BS_AUTORADIOBUTTON |
							WS_CLIPCHILDREN | WS_VISIBLE,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	BubbleButton *bubble = calloc(1, sizeof(BubbleButton));
	bubble->id = id;
	bubble->pOldProc = (WNDPROC)SubclassWindow(tmp, _BtProc);
	bubble->cinfo.fore = -1;
	bubble->cinfo.back = -1;
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)bubble);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}


/*
 * Create a new slider window (widget) to be packed.
 * Parameters:
 *       vertical: TRUE or FALSE if slider is vertical.
 *       increments: Number of increments available.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_slider_new(int vertical, int increments, ULONG id)
{
	HWND tmp = CreateWindow(TRACKBAR_CLASS,
							"",
							WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE |
							(vertical ? TBS_VERT : TBS_HORZ),
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	ColorInfo *cinfo = calloc(1, sizeof(ColorInfo));

	cinfo->back = cinfo->fore = -1;
	cinfo->buddy = 0;
	cinfo->user = 0;

	cinfo->pOldProc = SubclassWindow(tmp, _colorwndproc);
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	SendMessage(tmp, TBM_SETRANGE, (WPARAM)FALSE, (LPARAM)MAKELONG(0, increments-1));
	return tmp;
}

/*
 * Create a new percent bar window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_percent_new(ULONG id)
{
	return CreateWindow(PROGRESS_CLASS,
						"",
						WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
						0,0,2000,1000,
						DW_HWND_OBJECT,
						(HMENU)id,
						DWInstance,
						NULL);
}

/*
 * Create a new checkbox window (widget) to be packed.
 * Parameters:
 *       text: The text to be display by the static text widget.
 *       id: An ID to be used with WinWindowFromID() or 0L.
 */
HWND dw_checkbox_new(char *text, ULONG id)
{
	BubbleButton *bubble = calloc(1, sizeof(BubbleButton));
	HWND tmp = CreateWindow(BUTTONCLASSNAME,
							text,
							WS_CHILD | BS_AUTOCHECKBOX |
							BS_TEXT | WS_CLIPCHILDREN | WS_VISIBLE,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	bubble->id = id;
	bubble->checkbox = 1;
	bubble->pOldProc = (WNDPROC)SubclassWindow(tmp, _BtProc);
	bubble->cinfo.fore = -1;
	bubble->cinfo.back = -1;
	SetWindowLong(tmp, GWL_USERDATA, (ULONG)bubble);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Create a new listbox window (widget) to be packed.
 * Parameters:
 *       id: An ID to be used with WinWindowFromID() or 0L.
 *       multi: Multiple select TRUE or FALSE.
 */
HWND dw_listbox_new(ULONG id, int multi)
{
	HWND tmp = CreateWindow(LISTBOXCLASSNAME,
							"",
							WS_VISIBLE | LBS_NOINTEGRALHEIGHT |
							WS_CHILD | LBS_HASSTRINGS |
							LBS_NOTIFY | WS_BORDER  | WS_CLIPCHILDREN |
							WS_VSCROLL | (multi ? LBS_MULTIPLESEL : 0) ,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	ContainerInfo *cinfo = (ContainerInfo *)calloc(1, sizeof(ContainerInfo));

	if(!cinfo)
	{
		DestroyWindow(tmp);
		return NULL;
	}

	cinfo->cinfo.fore = -1;
	cinfo->cinfo.back = -1;
	cinfo->pOldProc = (WNDPROC)SubclassWindow(tmp, _containerwndproc);

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)cinfo);
	dw_window_set_font(tmp, DefaultFont);
	return tmp;
}

/*
 * Sets the icon used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon.
 */
void dw_window_set_icon(HWND handle, ULONG id)
{
	HICON hicon = LoadIcon(DWInstance, MAKEINTRESOURCE(id));

	SendMessage(handle, WM_SETICON,
				(WPARAM) IMAGE_ICON,
				(LPARAM) hicon);
}

/*
 * Sets the bitmap used for a given static window.
 * Parameters:
 *       handle: Handle to the window.
 *       id: An ID to be used to specify the icon.
 */
void dw_window_set_bitmap(HWND handle, ULONG id)
{
	HBITMAP hbitmap = LoadBitmap(DWInstance, MAKEINTRESOURCE(id));

	SendMessage(handle, STM_SETIMAGE,
				(WPARAM) IMAGE_BITMAP,
				(LPARAM) hbitmap);
}

/*
 * Sets the text used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 *       text: The text associsated with a given window.
 */
void dw_window_set_text(HWND handle, char *text)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	SetWindowText(handle, text);

	/* Combobox */
	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME)+1)==0)
		SendMessage(handle, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));
}

/*
 * Gets the text used for a given window.
 * Parameters:
 *       handle: Handle to the window.
 * Returns:
 *       text: The text associsated with a given window.
 */
char *dw_window_get_text(HWND handle)
{
	char tempbuf[4096] = "";

	GetWindowText(handle, tempbuf, 4095);
	tempbuf[4095] = 0;

	return strdup(tempbuf);
}

/*
 * Disables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void dw_window_disable(HWND handle)
{
	EnableWindow(handle, FALSE);
}

/*
 * Enables given window (widget).
 * Parameters:
 *       handle: Handle to the window.
 */
void dw_window_enable(HWND handle)
{
	EnableWindow(handle, TRUE);
}

/*
 * Gets the child window handle with specified ID.
 * Parameters:
 *       handle: Handle to the parent window.
 *       id: Integer ID of the child.
 */
HWND dw_window_from_id(HWND handle, int id)
{
    return 0L;
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
void dw_box_pack_start(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad)
{
	Box *thisbox;

	thisbox = (Box *)GetWindowLong(box, GWL_USERDATA);
	if(thisbox)
	{
		int z;
		Item *tmpitem, *thisitem = thisbox->items;
		char tmpbuf[100];

		tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

		for(z=0;z<thisbox->count;z++)
		{
			tmpitem[z] = thisitem[z];
		}

		GetClassName(item, tmpbuf, 99);

		if(strnicmp(tmpbuf, FRAMECLASSNAME, 2)==0)
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

		if(thisbox->count)
			free(thisitem);

		thisbox->count++;

		SetParent(item, box);
		if(strncmp(tmpbuf, UPDOWN_CLASS, strlen(UPDOWN_CLASS))==0)
		{
			ColorInfo *cinfo = (ColorInfo *)GetWindowLong(item, GWL_USERDATA);

			if(cinfo)
			{
				SetParent(cinfo->buddy, box);
				ShowWindow(cinfo->buddy, SW_SHOW);
				SendMessage(item, UDM_SETBUDDY, (WPARAM)cinfo->buddy, 0);
			}
		}
	}
}

/*
 * Sets the size of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          width: New width in pixels.
 *          height: New height in pixels.
 */
void dw_window_set_usize(HWND handle, ULONG width, ULONG height)
{
	SetWindowPos(handle, (HWND)NULL, 0, 0, width, height, SWP_SHOWWINDOW | SWP_NOZORDER | SWP_NOMOVE);
}

/*
 * Returns the width of the screen.
 */
int dw_screen_width(void)
{
	return GetSystemMetrics(SM_CXSCREEN);
}

/*
 * Returns the height of the screen.
 */
int dw_screen_height(void)
{
	return GetSystemMetrics(SM_CYSCREEN);
}

/* This should return the current color depth */
unsigned long dw_color_depth(void)
{
	int bpp;
	HDC hdc = GetDC(HWND_DESKTOP);
    
	bpp = GetDeviceCaps(hdc, BITSPIXEL);

	ReleaseDC(HWND_DESKTOP, hdc);

	return bpp;
}


/*
 * Sets the position of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          x: X location from the bottom left.
 *          y: Y location from the bottom left.
 */
void dw_window_set_pos(HWND handle, ULONG x, ULONG y)
{
	SetWindowPos(handle, (HWND)NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
void dw_window_set_pos_size(HWND handle, ULONG x, ULONG y, ULONG width, ULONG height)
{
	SetWindowPos(handle, (HWND)NULL, x, y, width, height, SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
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
void dw_window_get_pos_size(HWND handle, ULONG *x, ULONG *y, ULONG *width, ULONG *height)
{
	WINDOWPLACEMENT wp;

	wp.length = sizeof(WINDOWPLACEMENT);

	GetWindowPlacement(handle, &wp);
	if(x)
		*x = wp.rcNormalPosition.left;
	if(y)
		*y = wp.rcNormalPosition.top;
	if(width)
		*width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
	if(height)
		*height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

}

/*
 * Sets the style of a given window (widget).
 * Parameters:
 *          handle: Window (widget) handle.
 *          width: New width in pixels.
 *          height: New height in pixels.
 */
void dw_window_set_style(HWND handle, ULONG style, ULONG mask)
{
	ULONG tmp, currentstyle = GetWindowLong(handle, GWL_STYLE);
	ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

	tmp = currentstyle | mask;
	tmp ^= mask;
	tmp |= style;


	/* We are using SS_NOPREFIX as a VCENTER flag */
	if(tmp & SS_NOPREFIX)
	{

		if(cinfo)
			cinfo->vcenter = 1;
		else
		{
			cinfo = calloc(1, sizeof(ColorInfo));
			cinfo->fore = cinfo->back = -1;
			cinfo->vcenter = 1;

			cinfo->pOldProc = SubclassWindow(handle, _colorwndproc);
			SetWindowLong(handle, GWL_USERDATA, (ULONG)cinfo);
		}
	}
	else if(cinfo)
		cinfo->vcenter = 0;

	SetWindowLong(handle, GWL_STYLE, tmp);
}

/* Finds the physical ID from the reference ID */
int _findnotebookid(NotebookPage **array, int pageid)
{
	int z;

	for(z=0;z<256;z++)
	{
		if(array[z] && array[z]->realid == pageid)
			return z;
	}
	return -1;
}

/*
 * Adds a new page to specified notebook.
 * Parameters:
 *          handle: Window (widget) handle.
 *          flags: Any additional page creation flags.
 *          front: If TRUE page is added at the beginning.
 */
ULONG dw_notebook_page_new(HWND handle, ULONG flags, int front)
{
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);

	if(array)
	{
		int z, refid = -1;

		for(z=0;z<256;z++)
		{
			if(_findnotebookid(array, z) == -1)
			{
				refid = z;
				break;
			}
		}

		if(refid == -1)
			return -1;

		for(z=0;z<256;z++)
		{
			if(!array[z])
			{
				int oldpage = TabCtrl_GetCurSel(handle);

				array[z] = calloc(1, sizeof(NotebookPage));
				array[z]->realid = refid;
				array[z]->item.mask = TCIF_TEXT;
				array[z]->item.iImage = -1;
				array[z]->item.pszText = "";
				TabCtrl_InsertItem(handle, z, &(array[z]->item));

				if(oldpage > -1 && array[oldpage])
					SetParent(array[oldpage]->hwnd, DW_HWND_OBJECT);

				TabCtrl_SetCurSel(handle, z);
				return refid;
			}
		}
	}
	return -1;
}

/*
 * Sets the text on the specified notebook tab.
 * Parameters:
 *          handle: Notebook handle.
 *          pageid: Page ID of the tab to set.
 *          text: Pointer to the text to set.
 */
void dw_notebook_page_set_text(HWND handle, ULONG pageidx, char *text)
{

	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
	int pageid;

	if(!array)
		return;

	pageid = _findnotebookid(array, pageidx);

	if(pageid > -1 && array[pageid])
	{
		array[pageid]->item.mask = TCIF_TEXT;
		array[pageid]->item.pszText = text;
		TabCtrl_SetItem(handle, pageid, &(array[pageid]->item));
		_resize_notebook_page(handle, pageid);
	}
}

/*
 * Sets the text on the specified notebook tab status area.
 * Parameters:
 *          handle: Notebook handle.
 *          pageid: Page ID of the tab to set.
 *          text: Pointer to the text to set.
 */
void dw_notebook_page_set_status_text(HWND handle, ULONG pageid, char *text)
{
}

/*
 * Packs the specified box into the notebook page.
 * Parameters:
 *          handle: Handle to the notebook to be packed.
 *          pageid: Page ID in the notebook which is being packed.
 *          page: Box handle to be packed.
 */
void dw_notebook_pack(HWND handle, ULONG pageidx, HWND page)
{
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
	int pageid;

	if(!array)
		return;

	pageid = _findnotebookid(array, pageidx);

	if(pageid > -1 && array[pageid])
	{
		HWND tmpbox = dw_box_new(BOXVERT, 0);

		dw_box_pack_start(tmpbox, page, 0, 0, TRUE, TRUE, 0);
		SubclassWindow(tmpbox, _wndproc);
		if(array[pageid]->hwnd)
			dw_window_destroy(array[pageid]->hwnd);
		array[pageid]->hwnd = tmpbox;
		if(pageidx == dw_notebook_page_query(handle))
		{
			SetParent(tmpbox, handle);
			_resize_notebook_page(handle, pageid);
		}
	}
}

/*
 * Remove a page from a notebook.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be destroyed.
 */
void dw_notebook_page_destroy(HWND handle, unsigned int pageidx)
{
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
	int newid = -1, z, pageid;

	if(!array)
		return;

	pageid = _findnotebookid(array, pageidx);

	if(pageid < 0)
		return;

	if(array[pageid])
	{
		SetParent(array[pageid]->hwnd, DW_HWND_OBJECT);
		free(array[pageid]);
		array[pageid] = NULL;
	}

	TabCtrl_DeleteItem(handle, pageid);

	/* Shift the pages over 1 */
	for(z=pageid;z<255;z++)
		array[z] = array[z+1];
	array[255] = NULL;

	for(z=0;z<256;z++)
	{
		if(array[z])
		{
			newid = z;
			break;
		}
	}
	if(newid > -1)
	{
		SetParent(array[newid]->hwnd, handle);
		_resize_notebook_page(handle, newid);
		dw_notebook_page_set(handle, array[newid]->realid);
	}
}

/*
 * Queries the currently visible page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 */
unsigned int dw_notebook_page_query(HWND handle)
{
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
	int physid = TabCtrl_GetCurSel(handle);

	if(physid > -1 && physid < 256 && array && array[physid])
		return array[physid]->realid;
	return -1;
}

/*
 * Sets the currently visible page ID.
 * Parameters:
 *          handle: Handle to the notebook widget.
 *          pageid: ID of the page to be made visible.
 */
void dw_notebook_page_set(HWND handle, unsigned int pageidx)
{
	NotebookPage **array = (NotebookPage **)GetWindowLong(handle, GWL_USERDATA);
	int pageid;

	if(!array)
		return;

	pageid = _findnotebookid(array, pageidx);

	if(pageid > -1 && pageid < 256)
	{
		int oldpage = TabCtrl_GetCurSel(handle);

		if(oldpage > -1 && array && array[oldpage])
			SetParent(array[oldpage]->hwnd, DW_HWND_OBJECT);

		TabCtrl_SetCurSel(handle, pageid);

		SetParent(array[pageid]->hwnd, handle);
		_resize_notebook_page(handle, pageid);
	}
}

/*
 * Appends the specified text to the listbox's (or combobox) entry list.
 * Parameters:
 *          handle: Handle to the listbox to be appended to.
 *          text: Text to append into listbox.
 */
void dw_listbox_append(HWND handle, char *text)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0)
		SendMessage(handle,
					CB_ADDSTRING,
					0, (LPARAM)text);
	else
		SendMessage(handle,
					LB_ADDSTRING,
					0, (LPARAM)text);
}

/*
 * Clears the listbox's (or combobox) list of all entries.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
void dw_listbox_clear(HWND handle)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0)
	{
		char *buf = dw_window_get_text(handle);

		SendMessage(handle,
					CB_RESETCONTENT, 0L, 0L);

		if(buf)
		{
			dw_window_set_text(handle, buf);
			free(buf);
		}
	}
	else
		SendMessage(handle,
					LB_RESETCONTENT, 0L, 0L);
}

/*
 * Sets the text of a given listbox entry.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          index: Index into the list to be queried.
 *          buffer: Buffer where text will be copied.
 */
void dw_listbox_set_text(HWND handle, unsigned int index, char *buffer)
{
	unsigned int sel = (unsigned int)SendMessage(handle, LB_GETCURSEL, 0, 0);
	SendMessage(handle,	LB_DELETESTRING, (WPARAM)index, 0);
	SendMessage(handle, LB_INSERTSTRING, (WPARAM)index, (LPARAM)buffer);
	SendMessage(handle, LB_SETCURSEL, (WPARAM)sel, 0);
	SendMessage(handle, LB_SETSEL, (WPARAM)TRUE, (LPARAM)sel);
}

/*
 * Copies the given index item's text into buffer.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          index: Index into the list to be queried.
 *          buffer: Buffer where text will be copied.
 *          length: Length of the buffer (including NULL).
 */
void dw_listbox_query_text(HWND handle, unsigned int index, char *buffer, unsigned int length)
{
	SendMessage(handle,
				LB_GETTEXT, (WPARAM)index, (LPARAM)buffer);
}

/*
 * Returns the index to the item in the list currently selected.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 */
unsigned int dw_listbox_selected(HWND handle)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0)
		return (unsigned int)SendMessage(handle,
										 CB_GETCURSEL,
										 0, 0);

	return (unsigned int)SendMessage(handle,
									 LB_GETCURSEL,
									 0, 0);
}

/*
 * Returns the index to the current selected item or -1 when done.
 * Parameters:
 *          handle: Handle to the listbox to be queried.
 *          where: Either the previous return or -1 to restart.
 */
int dw_listbox_selected_multi(HWND handle, int where)
{
	int *array, count, z;

	count = (int)SendMessage(handle, LB_GETSELCOUNT, 0, 0);
	if(count > 0)
	{
		array = malloc(sizeof(int)*count);
		SendMessage(handle, LB_GETSELITEMS, (WPARAM)count, (LPARAM)array);

		if(where == -1)
		{
			int ret = array[0];
			free(array);
			return ret;
		}
		for(z=0;z<count;z++)
		{
			if(array[z] == where && (z+1) < count)
			{
				int ret = array[z+1];
				free(array);
				return ret;
			}
		}
		free(array);
	}
	return -1;
}

/*
 * Sets the selection state of a given index.
 * Parameters:
 *          handle: Handle to the listbox to be set.
 *          index: Item index.
 *          state: TRUE if selected FALSE if unselected.
 */
void dw_listbox_select(HWND handle, int index, int state)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0)
		SendMessage(handle, CB_SETCURSEL, (WPARAM)index, 0);
	else
	{
		SendMessage(handle, LB_SETCURSEL, (WPARAM)index, 0);
		SendMessage(handle, LB_SETSEL, (WPARAM)state, (LPARAM)index);
	}
	_wndproc(handle, WM_COMMAND, (WPARAM)(LBN_SELCHANGE << 16), (LPARAM)handle);
}

/*
 * Deletes the item with given index from the list.
 * Parameters:
 *          handle: Handle to the listbox to be set.
 *          index: Item index.
 */
void dw_listbox_delete(HWND handle, int index)
{
	SendMessage(handle, LB_DELETESTRING, (WPARAM)index, 0);
}

/*
 * Returns the listbox's item count.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 */
int dw_listbox_count(HWND handle)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, COMBOBOXCLASSNAME, strlen(COMBOBOXCLASSNAME))==0)
		return (int)SendMessage(handle,
								CB_GETCOUNT,0L, 0L);

	return (int)SendMessage(handle,
							LB_GETCOUNT,0L, 0L);
}

/*
 * Sets the topmost item in the viewport.
 * Parameters:
 *          handle: Handle to the listbox to be cleared.
 *          top: Index to the top item.
 */
void dw_listbox_set_top(HWND handle, int top)
{
	SendMessage(handle, LB_SETTOPINDEX, (WPARAM)top, 0);
}

/*
 * Adds text to an MLE box and returns the current point.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          buffer: Text buffer to be imported.
 *          startpoint: Point to start entering text.
 */
unsigned int dw_mle_import(HWND handle, char *buffer, int startpoint)
{
	int textlen, len = GetWindowTextLength(handle);
	char *tmpbuf;

	if((textlen = strlen(buffer)) < 1)
		return startpoint;

	startpoint++;
	tmpbuf = calloc(1, len + textlen + startpoint + 2);

	if(startpoint < 0)
		startpoint = 0;

	if(len)
	{
		char *dest, *start;
		int copylen = len - startpoint;

		GetWindowText(handle, tmpbuf, len+1);

		dest = &tmpbuf[startpoint+textlen-1];
		start = &tmpbuf[startpoint];

		if(copylen > 0)
			memcpy(dest, start, copylen);
	}
	memcpy(&tmpbuf[startpoint], buffer, textlen);

	SetWindowText(handle, tmpbuf);

	free(tmpbuf);
	return (startpoint + textlen - 1);
}

/*
 * Grabs text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          buffer: Text buffer to be exported.
 *          startpoint: Point to start grabbing text.
 *          length: Amount of text to be grabbed.
 */
void dw_mle_export(HWND handle, char *buffer, int startpoint, int length)
{
	int max, len = GetWindowTextLength(handle);
	char *tmpbuf = calloc(1, len+2);

	if(len)
		GetWindowText(handle, tmpbuf, len+1);

	buffer[0] = 0;

	if(startpoint < len)
	{
		max = MIN(length, len - startpoint);

		memcpy(buffer, &tmpbuf[startpoint], max);
	}

	free(tmpbuf);
}

/*
 * Obtains information about an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be queried.
 *          bytes: A pointer to a variable to return the total bytes.
 *          lines: A pointer to a variable to return the number of lines.
 */
void dw_mle_query(HWND handle, unsigned long *bytes, unsigned long *lines)
{
	if(bytes)
		*bytes = GetWindowTextLength(handle);
	if(lines)
		*lines = (unsigned long)SendMessage(handle, EM_GETLINECOUNT, 0, 0);
}

/*
 * Deletes text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be deleted from.
 *          startpoint: Point to start deleting text.
 *          length: Amount of text to be deleted.
 */
void dw_mle_delete(HWND handle, int startpoint, int length)
{
	int len = GetWindowTextLength(handle);
	char *tmpbuf = calloc(1, len+2);

	GetWindowText(handle, tmpbuf, len+1);

	if(startpoint + length < len)
	{
		strcpy(&tmpbuf[startpoint], &tmpbuf[startpoint+length]);

		SetWindowText(handle, tmpbuf);
	}

	free(tmpbuf);
}

/*
 * Clears all text from an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be cleared.
 */
void dw_mle_clear(HWND handle)
{
	SetWindowText(handle, "");
}

/*
 * Sets the visible line of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          line: Line to be visible.
 */
void dw_mle_set_visible(HWND handle, int line)
{
	int point = (int)SendMessage(handle, EM_LINEINDEX, (WPARAM)line, 0);
	dw_mle_set(handle, point);
}

/*
 * Sets the editablity of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it can be edited, FALSE for readonly.
 */
void dw_mle_set_editable(HWND handle, int state)
{
	SendMessage(handle, EM_SETREADONLY, (WPARAM)(state ? FALSE : TRUE), 0);
}

/*
 * Sets the word wrap state of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE.
 *          state: TRUE if it wraps, FALSE if it doesn't.
 */
void dw_mle_set_word_wrap(HWND handle, int state)
{
	/* If ES_AUTOHSCROLL is not set and there is not
	 * horizontal scrollbar it word wraps.
	 */
	if(state)
		dw_window_set_style(handle, 0, ES_AUTOHSCROLL);
	else
		dw_window_set_style(handle, ES_AUTOHSCROLL, ES_AUTOHSCROLL);
}

/*
 * Sets the current cursor position of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be positioned.
 *          point: Point to position cursor.
 */
void dw_mle_set(HWND handle, int point)
{
	SendMessage(handle, EM_SETSEL, (WPARAM)point, (LPARAM)point);
	SendMessage(handle, EM_SCROLLCARET, 0, 0);
}

/*
 * Finds text in an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to be cleared.
 *          text: Text to search for.
 *          point: Start point of search.
 *          flags: Search specific flags.
 */
int dw_mle_search(HWND handle, char *text, int point, unsigned long flags)
{
	int len = GetWindowTextLength(handle);
	char *tmpbuf = calloc(1, len+2);
	int z, textlen, retval = 0;

	GetWindowText(handle, tmpbuf, len+1);

	textlen = strlen(text);

	if(flags & DW_MLE_CASESENSITIVE)
	{
		for(z=point;z<(len-textlen) && !retval;z++)
		{
			if(strncmp(&tmpbuf[z], text, textlen) == 0)
				retval = z + textlen;
		}
	}
	else
	{
		for(z=point;z<(len-textlen) && !retval;z++)
		{
			if(strnicmp(&tmpbuf[z], text, textlen) == 0)
				retval = z + textlen;
		}
	}

	if(retval)
	{
		SendMessage(handle, EM_SETSEL, (WPARAM)retval - textlen, (LPARAM)retval);
		SendMessage(handle, EM_SCROLLCARET, 0, 0);
	}

	free(tmpbuf);

	return retval;
}

/*
 * Stops redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to freeze.
 */
void dw_mle_freeze(HWND handle)
{
}

/*
 * Resumes redrawing of an MLE box.
 * Parameters:
 *          handle: Handle to the MLE to thaw.
 */
void dw_mle_thaw(HWND handle)
{
}

/*
 * Returns the range of the percent bar.
 * Parameters:
 *          handle: Handle to the percent bar to be queried.
 */
unsigned int dw_percent_query_range(HWND handle)
{
	return (unsigned int)SendMessage(handle, PBM_GETRANGE, (WPARAM)FALSE, 0);
}

/*
 * Sets the percent bar position.
 * Parameters:
 *          handle: Handle to the percent bar to be set.
 *          position: Position of the percent bar withing the range.
 */
void dw_percent_set_pos(HWND handle, unsigned int position)
{
	SendMessage(handle, PBM_SETPOS, (WPARAM)position, 0);
}

/*
 * Returns the position of the slider.
 * Parameters:
 *          handle: Handle to the slider to be queried.
 */
unsigned int dw_slider_query_pos(HWND handle)
{
	int max = (int)SendMessage(handle, TBM_GETRANGEMAX, 0, 0);
	ULONG currentstyle = GetWindowLong(handle, GWL_STYLE);

	if(currentstyle & TBS_VERT)
		return max - (unsigned int)SendMessage(handle, TBM_GETPOS, 0, 0);
	return (unsigned int)SendMessage(handle, TBM_GETPOS, 0, 0);
}

/*
 * Sets the slider position.
 * Parameters:
 *          handle: Handle to the slider to be set.
 *          position: Position of the slider withing the range.
 */
void dw_slider_set_pos(HWND handle, unsigned int position)
{
	int max = (int)SendMessage(handle, TBM_GETRANGEMAX, 0, 0);
	ULONG currentstyle = GetWindowLong(handle, GWL_STYLE);

	if(currentstyle & TBS_VERT)
		SendMessage(handle, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)max - position);
	else
		SendMessage(handle, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)position);
}

/*
 * Sets the spinbutton value.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          position: Current value of the spinbutton.
 */
void dw_spinbutton_set_pos(HWND handle, long position)
{
	char tmpbuf[100];
	ColorInfo *cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);

	sprintf(tmpbuf, "%d", position);

	if(cinfo && cinfo->buddy)
		SetWindowText(cinfo->buddy, tmpbuf);

	if(IS_IE5PLUS)
		SendMessage(handle, UDM_SETPOS32, 0, (LPARAM)position);
	else
		SendMessage(handle, UDM_SETPOS, 0, (LPARAM)MAKELONG((short)position, 0));
}

/*
 * Sets the spinbutton limits.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          position: Current value of the spinbutton.
 *          position: Current value of the spinbutton.
 */
void dw_spinbutton_set_limits(HWND handle, long upper, long lower)
{
	if(IS_IE5PLUS)
		SendMessage(handle, UDM_SETRANGE32, (WPARAM)lower,(LPARAM)upper);
	else
		SendMessage(handle, UDM_SETRANGE32, (WPARAM)((short)lower),
					(LPARAM)((short)upper));
}

/*
 * Sets the entryfield character limit.
 * Parameters:
 *          handle: Handle to the spinbutton to be set.
 *          limit: Number of characters the entryfield will take.
 */
void dw_entryfield_set_limit(HWND handle, ULONG limit)
{
	SendMessage(handle, EM_SETLIMITTEXT, (WPARAM)limit, 0);
}

/*
 * Returns the current value of the spinbutton.
 * Parameters:
 *          handle: Handle to the spinbutton to be queried.
 */
long dw_spinbutton_query(HWND handle)
{
	if(IS_IE5PLUS)
		return (long)SendMessage(handle, UDM_GETPOS32, 0, 0);
	else
		return (long)SendMessage(handle, UDM_GETPOS, 0, 0);
}

/*
 * Returns the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 */
int dw_checkbox_query(HWND handle)
{
	if(SendMessage(handle, BM_GETCHECK, 0, 0) == BST_CHECKED)
		return (in_checkbox_handler ? FALSE : TRUE);
	return (in_checkbox_handler ? TRUE : FALSE);
}

/* This function unchecks all radiobuttons on a box */
BOOL CALLBACK _uncheck_radios(HWND handle, LPARAM lParam)
{
	char tmpbuf[100];

	GetClassName(handle, tmpbuf, 99);

	if(strnicmp(tmpbuf, BUTTONCLASSNAME, strlen(BUTTONCLASSNAME)+1)==0)
	{
		BubbleButton *bubble= (BubbleButton *)GetWindowLong(handle, GWL_USERDATA);

		if(bubble && !bubble->checkbox)
			SendMessage(handle, BM_SETCHECK, 0, 0);
	}
	return TRUE;
}
/*
 * Sets the state of the checkbox.
 * Parameters:
 *          handle: Handle to the checkbox to be queried.
 *          value: TRUE for checked, FALSE for unchecked.
 */
void dw_checkbox_set(HWND handle, int value)
{
	BubbleButton *bubble= (BubbleButton *)GetWindowLong(handle, GWL_USERDATA);

	if(bubble && !bubble->checkbox)
	{
		HWND parent = GetParent(handle);

		if(parent)
			EnumChildWindows(parent, _uncheck_radios, 0);
	}
	SendMessage(handle, BM_SETCHECK, (WPARAM)value, 0);
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
HWND dw_tree_insert_after(HWND handle, HWND item, char *title, unsigned long icon, HWND parent, void *itemdata)
{
	TVITEM tvi;
	TVINSERTSTRUCT tvins;
	HTREEITEM hti;
	void **ptrs= malloc(sizeof(void *) * 2);

	ptrs[0] = title;
	ptrs[1] = itemdata;

	tvi.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
	tvi.pszText = title;
	tvi.lParam = (LONG)ptrs;
	tvi.cchTextMax = strlen(title);
	tvi.iSelectedImage = tvi.iImage = _lookup_icon(handle, (HICON)icon, 1);

	tvins.item = tvi;
	tvins.hParent = (HTREEITEM)parent;
	tvins.hInsertAfter = item ? (HTREEITEM)item : TVI_FIRST;

	hti = TreeView_InsertItem(handle, &tvins);

	return (HWND)hti;
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
HWND dw_tree_insert(HWND handle, char *title, unsigned long icon, HWND parent, void *itemdata)
{
	TVITEM tvi;
	TVINSERTSTRUCT tvins;
	HTREEITEM hti;
	void **ptrs= malloc(sizeof(void *) * 2);

	ptrs[0] = title;
	ptrs[1] = itemdata;

	tvi.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
	tvi.pszText = title;
	tvi.lParam = (LONG)ptrs;
	tvi.cchTextMax = strlen(title);
	tvi.iSelectedImage = tvi.iImage = _lookup_icon(handle, (HICON)icon, 1);

	tvins.item = tvi;
	tvins.hParent = (HTREEITEM)parent;
	tvins.hInsertAfter = TVI_LAST;

	hti = TreeView_InsertItem(handle, &tvins);

	return (HWND)hti;
}

/*
 * Sets the text and icon of an item in a tree window (widget).
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 *          title: The text title of the entry.
 *          icon: Handle to coresponding icon.
 */
void dw_tree_set(HWND handle, HWND item, char *title, unsigned long icon)
{
	TVITEM tvi;
	void **ptrs;

	tvi.mask = TVIF_HANDLE;
	tvi.hItem = (HTREEITEM)item;

	if(TreeView_GetItem(handle, &tvi))
	{

		ptrs = (void **)tvi.lParam;
		ptrs[0] = title;

		tvi.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		tvi.pszText = title;
		tvi.cchTextMax = strlen(title);
		tvi.iSelectedImage = tvi.iImage = _lookup_icon(handle, (HICON)icon, 1);
		tvi.hItem = (HTREEITEM)item;

		TreeView_SetItem(handle, &tvi);
	}
}

/*
 * Sets the item data of a tree item.
 * Parameters:
 *          handle: Handle to the tree containing the item.
 *          item: Handle of the item to be modified.
 *          itemdata: User defined data to be associated with item.
 */
void dw_tree_set_data(HWND handle, HWND item, void *itemdata)
{
	TVITEM tvi;
	void **ptrs;

	tvi.mask = TVIF_HANDLE;
	tvi.hItem = (HTREEITEM)item;

	if(TreeView_GetItem(handle, &tvi))
	{
		ptrs = (void **)tvi.lParam;
		ptrs[1] = itemdata;
	}
}

/*
 * Sets this item as the active selection.
 * Parameters:
 *       handle: Handle to the tree window (widget) to be selected.
 *       item: Handle to the item to be selected.
 */
void dw_tree_item_select(HWND handle, HWND item)
{
	TreeView_SelectItem(handle, (HTREEITEM)item);
}

/*
 * Removes all nodes from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 */
void dw_tree_clear(HWND handle)
{
	TreeView_DeleteAllItems(handle);
}

/*
 * Expands a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be expanded.
 */
void dw_tree_expand(HWND handle, HWND item)
{
	TreeView_Expand(handle, (HTREEITEM)item, TVE_EXPAND);
}

/*
 * Collapses a node on a tree.
 * Parameters:
 *       handle: Handle to the tree window (widget).
 *       item: Handle to node to be collapsed.
 */
void dw_tree_collapse(HWND handle, HWND item)
{
	TreeView_Expand(handle, (HTREEITEM)item, TVE_COLLAPSE);
}

/*
 * Removes a node from a tree.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       item: Handle to node to be deleted.
 */
void dw_tree_delete(HWND handle, HWND item)
{
	if((HTREEITEM)item == TVI_ROOT || !item)
		return;

	TreeView_DeleteItem(handle, (HTREEITEM)item);
}

/*
 * Sets up the container columns.
 * Parameters:
 *          handle: Handle to the container to be configured.
 *          flags: An array of unsigned longs with column flags.
 *          titles: An array of strings with column text titles.
 *          count: The number of columns (this should match the arrays).
 *          separator: The column number that contains the main separator.
 *                     (only used on OS/2 but must be >= 0 on all)
 */
int dw_container_setup(HWND handle, unsigned long *flags, char **titles, int count, int separator)
{
	ContainerInfo *cinfo = (ContainerInfo *)GetWindowLong(handle, GWL_USERDATA);
	int z, l = 0;
	unsigned long *tempflags = calloc(sizeof(unsigned long), count + 2);
	LV_COLUMN lvc;

	if(separator == -1)
	{
		tempflags[0] = DW_CFA_RESERVED;
		l = 1;
	}

	memcpy(&tempflags[l], flags, sizeof(unsigned long) * count);
	tempflags[count + l] = 0;
	cinfo->flags = tempflags;
	cinfo->columns = count + l;


	for(z=0;z<count;z++)
	{
		if(titles[z])
		{
			lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvc.pszText = titles[z];
			lvc.cchTextMax = strlen(titles[z]);
			lvc.fmt = flags[z];
			lvc.cx = 75;
			lvc.iSubItem = count;
			SendMessage(handle, LVM_INSERTCOLUMN, (WPARAM)z + l, (LPARAM)&lvc);
		}
	}
	ListView_SetExtendedListViewStyle(handle, LVS_EX_FULLROWSELECT);
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
int dw_filesystem_setup(HWND handle, unsigned long *flags, char **titles, int count)
{
	LV_COLUMN lvc;

	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
	lvc.pszText = "Filename";
	lvc.cchTextMax = 8;
	lvc.fmt = 0;
	if(!count)
		lvc.cx = 300;
	else
		lvc.cx = 150;
	lvc.iSubItem = count;
	SendMessage(handle, LVM_INSERTCOLUMN, (WPARAM)0, (LPARAM)&lvc);
	dw_container_setup(handle, flags, titles, count, -1);
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
unsigned long dw_icon_load(unsigned long module, unsigned long id)
{
	return (unsigned long)LoadIcon(DWInstance, MAKEINTRESOURCE(id));
}

/*
 * Frees a loaded resource in OS/2 and Windows.
 * Parameters:
 *          handle: Handle to icon returned by dw_icon_load().
 */
void dw_icon_free(unsigned long handle)
{
	DestroyIcon((HICON)handle);
}

/*
 * Allocates memory used to populate a container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          rowcount: The number of items to be populated.
 */
void *dw_container_alloc(HWND handle, int rowcount)
{
	LV_ITEM lvi;
	int z;

	lvi.mask = LVIF_DI_SETITEM | LVIF_TEXT | LVIF_IMAGE;
	lvi.iSubItem = 0;
	/* Insert at the end */
	lvi.iItem = 1000000;
	lvi.pszText = "";
	lvi.cchTextMax = 1;
	lvi.iImage = -1;

	ShowWindow(handle, SW_HIDE);
	for(z=0;z<rowcount;z++)
		ListView_InsertItem(handle, &lvi);
	return (void *)handle;
}

/* Finds a icon in the table, otherwise it adds it to the table
 * and returns the index in the table.
 */
int _lookup_icon(HWND handle, HICON hicon, int type)
{
	int z;
	static HWND lasthwnd = NULL;

	if(!hSmall || !hLarge)
	{
		hSmall = ImageList_Create(16, 16, ILC_COLOR16 | ILC_MASK, ICON_INDEX_LIMIT, 0);
		hLarge = ImageList_Create(32, 32, ILC_COLOR16 | ILC_MASK, ICON_INDEX_LIMIT, 0);
	}
	for(z=0;z<ICON_INDEX_LIMIT;z++)
	{
		if(!lookup[z])
		{
			lookup[z] = hicon;
			ImageList_AddIcon(hSmall, hicon);
			ImageList_AddIcon(hLarge, hicon);
			if(type)
			{
				TreeView_SetImageList(handle, hSmall, TVSIL_NORMAL);
			}
			else
			{
				ListView_SetImageList(handle, hSmall, LVSIL_SMALL);
				ListView_SetImageList(handle, hLarge, LVSIL_NORMAL);
			}
			lasthwnd = handle;
			return z;
		}

		if(hicon == lookup[z])
		{
			if(lasthwnd != handle)
			{
				if(type)
				{
					TreeView_SetImageList(handle, hSmall, TVSIL_NORMAL);
				}
				else
				{
					ListView_SetImageList(handle, hSmall, LVSIL_SMALL);
					ListView_SetImageList(handle, hLarge, LVSIL_NORMAL);
				}
                lasthwnd = handle;
			}
			return z;
		}
	}
	return -1;
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
void dw_filesystem_set_file(HWND handle, void *pointer, int row, char *filename, unsigned long icon)
{
	LV_ITEM lvi;

	lvi.iItem = row;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_DI_SETITEM | LVIF_IMAGE | LVIF_TEXT;
	lvi.pszText = filename;
	lvi.cchTextMax = strlen(filename);
	lvi.iImage = _lookup_icon(handle, (HICON)icon, 0);

	ListView_SetItem(handle, &lvi);
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
void dw_filesystem_set_item(HWND handle, void *pointer, int column, int row, void *data)
{
	dw_container_set_item(handle, pointer, column + 1, row, data);
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
void dw_container_set_item(HWND handle, void *pointer, int column, int row, void *data)
{
	ContainerInfo *cinfo = (ContainerInfo *)GetWindowLong(handle, GWL_USERDATA);
	ULONG *flags;
	LV_ITEM lvi;
	char textbuffer[100], *destptr = textbuffer;

	if(!cinfo || !cinfo->flags || !data)
		return;

	flags = cinfo->flags;

	lvi.mask = LVIF_DI_SETITEM | LVIF_TEXT;
	lvi.iItem = row;
	lvi.iSubItem = column;

	if(flags[column] & DW_CFA_BITMAPORICON)
	{
		HICON hicon = *((HICON *)data);

		lvi.mask = LVIF_DI_SETITEM | LVIF_IMAGE;
		lvi.pszText = NULL;
		lvi.cchTextMax = 0;

		lvi.iImage = _lookup_icon(handle, hicon, 0);
	}
	else if(flags[column] & DW_CFA_STRING)
	{
		char *tmp = *((char **)data);

		if(!tmp)
			tmp = "";

		lvi.pszText = tmp;
		lvi.cchTextMax = strlen(tmp);
		destptr = tmp;
	}
	else if(flags[column] & DW_CFA_ULONG)
	{
		ULONG tmp = *((ULONG *)data);

		sprintf(textbuffer, "%lu", tmp);

		lvi.pszText = textbuffer;
		lvi.cchTextMax = strlen(textbuffer);
	}
	else if(flags[column] & DW_CFA_DATE)
	{
		CDATE fdate = *((CDATE *)data);

		if(fdate.month > -1 && fdate.month < 12 && fdate.day > 0 && fdate.year > 0)
			sprintf(textbuffer, "%s %d, %d", monthlist[fdate.month], fdate.day, fdate.year);
        else
			strcpy(textbuffer, "");
		lvi.pszText = textbuffer;
		lvi.cchTextMax = strlen(textbuffer);
	}
	else if(flags[column] & DW_CFA_TIME)
	{
		CTIME ftime = *((CTIME *)data);

		if(ftime.hours > 12)
			sprintf(textbuffer, "%d:%s%dpm", ftime.hours - 12, (ftime.minutes < 10) ? "0" : "", ftime.minutes);
		else
			sprintf(textbuffer, "%d:%s%dam", ftime.hours ? ftime.hours : 12, (ftime.minutes < 10) ? "0" : "", ftime.minutes);
		lvi.pszText = textbuffer;
		lvi.cchTextMax = strlen(textbuffer);
	}

	ListView_SetItem(handle, &lvi);
}

/*
 * Sets the width of a column in the container.
 * Parameters:
 *          handle: Handle to window (widget) of container.
 *          column: Zero based column of width being set.
 *          width: Width of column in pixels.
 */
void dw_container_set_column_width(HWND handle, int column, int width)
{
	ListView_SetColumnWidth(handle, column, width);
}

/*
 * Sets the title of a row in the container.
 * Parameters:
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          row: Zero based row of data being set.
 *          title: String title of the item.
 */
void dw_container_set_row_title(void *pointer, int row, char *title)
{
	LV_ITEM lvi;
	HWND container = (HWND)pointer;

	lvi.iItem = row;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_PARAM;
	lvi.lParam = (LPARAM)title;

	if(!ListView_SetItem(container, &lvi) && lvi.lParam)
		lvi.lParam = 0;

}

/*
 * Sets the title of a row in the container.
 * Parameters:
 *          handle: Handle to the container window (widget).
 *          pointer: Pointer to the allocated memory in dw_container_alloc().
 *          rowcount: The number of rows to be inserted.
 */
void dw_container_insert(HWND handle, void *pointer, int rowcount)
{
	ShowWindow(handle, SW_SHOW);
}

/*
 * Removes all rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 *       redraw: TRUE to cause the container to redraw immediately.
 */
void dw_container_clear(HWND handle, int redraw)
{
	ListView_DeleteAllItems(handle);
}

/*
 * Removes the first x rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be deleted from.
 *       rowcount: The number of rows to be deleted.
 */
void dw_container_delete(HWND handle, int rowcount)
{
	int z;

	for(z=0;z<rowcount;z++)
	{
		ListView_DeleteItem(handle, 0);
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
void dw_container_scroll(HWND handle, int direction, long rows)
{
	switch(direction)
	{
	case DW_SCROLL_TOP:
		ListView_Scroll(handle, 0, -10000000);
        break;
	case DW_SCROLL_BOTTOM:
		ListView_Scroll(handle, 0, 10000000);
		break;
	}
}

/*
 * Removes all rows from a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be cleared.
 */
void dw_container_set_view(HWND handle, unsigned long flags, int iconwidth, int iconheight)
{
}

/*
 * Starts a new query of a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       flags: If this parameter is DW_CRA_SELECTED it will only
 *              return items that are currently selected.  Otherwise
 *              it will return all records in the container.
 */
char *dw_container_query_start(HWND handle, unsigned long flags)
{
	LV_ITEM lvi;

	_index = ListView_GetNextItem(handle, -1, flags);

	if(_index == -1)
		return NULL;

	memset(&lvi, 0, sizeof(LV_ITEM));

	lvi.iItem = _index;
	lvi.mask = LVIF_PARAM;

	ListView_GetItem(handle, &lvi);

	return (char *)lvi.lParam;
}

/*
 * Continues an existing query of a container.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       flags: If this parameter is DW_CRA_SELECTED it will only
 *              return items that are currently selected.  Otherwise
 *              it will return all records in the container.
 */
char *dw_container_query_next(HWND handle, unsigned long flags)
{
	LV_ITEM lvi;

	_index = ListView_GetNextItem(handle, _index, flags);

	if(_index == -1)
		return NULL;

	memset(&lvi, 0, sizeof(LV_ITEM));

	lvi.iItem = _index;
	lvi.mask = LVIF_PARAM;

	ListView_GetItem(handle, &lvi);

	return (char *)lvi.lParam;
}

/*
 * Cursors the item with the text speficied, and scrolls to that item.
 * Parameters:
 *       handle: Handle to the window (widget) to be queried.
 *       text:  Text usually returned by dw_container_query().
 */
void dw_container_cursor(HWND handle, char *text)
{
	int index = ListView_GetNextItem(handle, -1, LVNI_ALL);

	while(index != -1)
	{
		LV_ITEM lvi;

		memset(&lvi, 0, sizeof(LV_ITEM));

		lvi.iItem = index;
		lvi.mask = LVIF_PARAM;

		ListView_GetItem(handle, &lvi);

		if((char *)lvi.lParam == text)
		{
			RECT viewport, item;

			ListView_SetItemState(handle, index, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
			ListView_EnsureVisible(handle, index, TRUE);
			return;
		}

        index = ListView_GetNextItem(handle, index, LVNI_ALL);
	}
}

/*
 * Optimizes the column widths so that all data is visible.
 * Parameters:
 *       handle: Handle to the window (widget) to be optimized.
 */
void dw_container_optimize(HWND handle)
{
	ContainerInfo *cinfo = (ContainerInfo *)GetWindowLong(handle, GWL_USERDATA);
	ULONG *flags;
	LV_ITEM lvi;

	if(cinfo && cinfo->columns == 1)
	{
		ListView_SetColumnWidth(handle, 0, LVSCW_AUTOSIZE);
	}
	else if(cinfo && cinfo->columns > 1)
	{
		int z, index;
		ULONG *flags = cinfo->flags, *columns = calloc(sizeof(ULONG), cinfo->columns);
		char *text = malloc(1024);

		/* Initialize with sizes of column labels */
		for(z=0;z<cinfo->columns;z++)
		{
			if(flags[z] & DW_CFA_BITMAPORICON)
				columns[z] = 5;
			else
			{
				LVCOLUMN lvc;

				lvc.mask = LVCF_TEXT;
				lvc.cchTextMax = 1023;
				lvc.pszText = text;

				if(ListView_GetColumn(handle, z, &lvc))
					columns[z] = ListView_GetStringWidth(handle, lvc.pszText);

				if(flags[z] & DW_CFA_RESERVED)
					columns[z] += 20;
			}
		}

		index = ListView_GetNextItem(handle, -1, LVNI_ALL);

		/* Query all the item texts */
		while(index != -1)
		{
			for(z=0;z<cinfo->columns;z++)
			{
				LV_ITEM lvi;

				memset(&lvi, 0, sizeof(LV_ITEM));

				lvi.iItem = index;
				lvi.iSubItem = z;
				lvi.mask = LVIF_TEXT;
				lvi.cchTextMax = 1023;
				lvi.pszText = text;

				if(ListView_GetItem(handle, &lvi))
				{
					int width = ListView_GetStringWidth(handle, lvi.pszText);
					if(width > columns[z])
					{
						if(z == 0)
							columns[z] = width + 20;
						else
							columns[z] = width;
					}
				}
			}

			index = ListView_GetNextItem(handle, index, LVNI_ALL);
		}

		/* Set the new sizes */
		for(z=0;z<cinfo->columns;z++)
			ListView_SetColumnWidth(handle, z, columns[z] + 16);

		free(columns);
		free(text);
	}
}

/*
 * Creates a rendering context widget (window) to be packed.
 * Parameters:
 *       id: An id to be used with dw_window_from_id.
 * Returns:
 *       A handle to the widget or NULL on failure.
 */
HWND dw_render_new(unsigned long id)
{
	Box *newbox = calloc(sizeof(Box), 1);
	HWND tmp = CreateWindow(ObjectClassName,
							"",
							WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);
	newbox->pad = 0;
	newbox->type = 0;
	newbox->count = 0;
	newbox->grouphwnd = (HWND)NULL;
	newbox->cinfo.pOldProc = SubclassWindow(tmp, _rendwndproc);
	newbox->cinfo.fore = newbox->cinfo.back = -1;

	SetWindowLong(tmp, GWL_USERDATA, (ULONG)newbox);
	return tmp;
}

/* Sets the current foreground drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void dw_color_foreground_set(unsigned long value)
{
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	DeleteObject(_hPen[threadid]);
	DeleteObject(_hBrush[threadid]);
	_foreground[threadid] = RGB(DW_RED_VALUE(value), DW_GREEN_VALUE(value), DW_BLUE_VALUE(value));
	_hPen[threadid] = CreatePen(PS_SOLID, 1, _foreground[threadid]);
	_hBrush[threadid] = CreateSolidBrush(_foreground[threadid]);
}

/* Sets the current background drawing color.
 * Parameters:
 *       red: red value.
 *       green: green value.
 *       blue: blue value.
 */
void dw_color_background_set(unsigned long value)
{
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	_background[threadid] = RGB(DW_RED_VALUE(value), DW_GREEN_VALUE(value), DW_BLUE_VALUE(value));
}

/* Draw a point on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x: X coordinate.
 *       y: Y coordinate.
 */
void dw_draw_point(HWND handle, HPIXMAP pixmap, int x, int y)
{
	HDC hdcPaint;
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	if(handle)
		hdcPaint = GetDC(handle);
	else if(pixmap)
		hdcPaint = pixmap->hdc;
	else
		return;

	SetPixel(hdcPaint, x, y, _foreground[threadid]);
	if(!pixmap)
		ReleaseDC(handle, hdcPaint);
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
void dw_draw_line(HWND handle, HPIXMAP pixmap, int x1, int y1, int x2, int y2)
{
	HDC hdcPaint;
	HPEN oldPen;
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	if(handle)
		hdcPaint = GetDC(handle);
	else if(pixmap)
		hdcPaint = pixmap->hdc;
	else
		return;

	oldPen = SelectObject(hdcPaint, _hPen[threadid]);
	MoveToEx(hdcPaint, x1, y1, NULL);
	LineTo(hdcPaint, x2, y2);
	SelectObject(hdcPaint, oldPen);
	/* For some reason Win98 (at least) fails
	 * to draw the last pixel.  So I do it myself.
	 */
	SetPixel(hdcPaint, x2, y2, _foreground[threadid]);
	if(!pixmap)
		ReleaseDC(handle, hdcPaint);
}

/* Draw a rectangle on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x: X coordinate.
 *       y: Y coordinate.
 *       width: Width of rectangle.
 *       height: Height of rectangle.
 */
void dw_draw_rect(HWND handle, HPIXMAP pixmap, int fill, int x, int y, int width, int height)
{
	HDC hdcPaint;
	HPEN oldPen;
	HBRUSH oldBrush;
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	if(handle)
		hdcPaint = GetDC(handle);
	else if(pixmap)
		hdcPaint = pixmap->hdc;
	else
		return;

	oldPen = SelectObject(hdcPaint, _hPen[threadid]);
	oldBrush = SelectObject(hdcPaint, _hBrush[threadid]);
	Rectangle(hdcPaint, x, y, x + width, y + height);
	SelectObject(hdcPaint, oldPen);
	SelectObject(hdcPaint, oldBrush);
	if(!pixmap)
		ReleaseDC(handle, hdcPaint);
}

/* Draw text on a window (preferably a render window).
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       x: X coordinate.
 *       y: Y coordinate.
 *       text: Text to be displayed.
 */
void dw_draw_text(HWND handle, HPIXMAP pixmap, int x, int y, char *text)
{
	HDC hdc;
	int size = 9, z, mustdelete = 0;
	HFONT hFont, oldFont;
	int threadid = dw_thread_id();

	if(threadid < 0 || threadid >= THREAD_LIMIT)
		threadid = 0;

	if(handle)
		hdc = GetDC(handle);
	else if(pixmap)
		hdc = pixmap->hdc;
	else
		return;

	{
		ColorInfo *cinfo;

		if(handle)
			cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);
		else
			cinfo = (ColorInfo *)GetWindowLong(pixmap->handle, GWL_USERDATA);

		if(cinfo)
		{
			hFont = _acquire_font(handle, cinfo->fontname);
			mustdelete = 1;
		}
	}
	oldFont = SelectObject(hdc, hFont);
	SetTextColor(hdc, _foreground[threadid]);
	SetBkMode(hdc, TRANSPARENT);
	TextOut(hdc, x, y, text, strlen(text));
	SelectObject(hdc, oldFont);
	if(mustdelete)
		DeleteObject(hFont);
	if(!pixmap)
		ReleaseDC(handle, hdc);
}

/* Query the width and height of a text string.
 * Parameters:
 *       handle: Handle to the window.
 *       pixmap: Handle to the pixmap. (choose only one of these)
 *       text: Text to be queried.
 *       width: Pointer to a variable to be filled in with the width.
 *       height Pointer to a variable to be filled in with the height.
 */
void dw_font_text_extents(HWND handle, HPIXMAP pixmap, char *text, int *width, int *height)
{
	HDC hdc;
	int mustdelete = 0;
	HFONT hFont = NULL, oldFont;
	SIZE sz;

	if(handle)
		hdc = GetDC(handle);
	else if(pixmap)
		hdc = pixmap->hdc;
	else
		return;

	{
		ColorInfo *cinfo;

		if(handle)
			cinfo = (ColorInfo *)GetWindowLong(handle, GWL_USERDATA);
		else
			cinfo = (ColorInfo *)GetWindowLong(pixmap->handle, GWL_USERDATA);

		if(cinfo)
		{
			hFont = _acquire_font(handle, cinfo->fontname);
			mustdelete = 1;
		}
	}
	oldFont = SelectObject(hdc, hFont);

	GetTextExtentPoint32(hdc, text, strlen(text), &sz);

	if(width)
		*width = sz.cx;

	if(height)
		*height = sz.cy;

	SelectObject(hdc, oldFont);
	if(mustdelete)
		DeleteObject(hFont);
	if(!pixmap)
		ReleaseDC(handle, hdc);
}

/* Call this after drawing to the screen to make sure
 * anything you have drawn is visible.
 */
void dw_flush(void)
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
HPIXMAP dw_pixmap_new(HWND handle, unsigned long width, unsigned long height, int depth)
{
	HPIXMAP pixmap;
	BITMAP bm;
	HDC hdc;

	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;

	hdc = GetDC(handle);

	pixmap->width = width; pixmap->height = height;

	pixmap->handle = handle;
	pixmap->hbm = CreateCompatibleBitmap(hdc, width, height);
	pixmap->hdc = CreateCompatibleDC(hdc);

	SelectObject(pixmap->hdc, pixmap->hbm);

	ReleaseDC(handle, hdc);

	return pixmap;
}

/*
 * Creates a pixmap from internal resource graphic specified by id.
 * Parameters:
 *       handle: Window handle the pixmap is associated with.
 *       id: Resource ID associated with requested pixmap.
 * Returns:
 *       A handle to a pixmap or NULL on failure.
 */
HPIXMAP dw_pixmap_grab(HWND handle, ULONG id)
{
	HPIXMAP pixmap;
	BITMAP bm;
	HDC hdc;

	if (!(pixmap = calloc(1,sizeof(struct _hpixmap))))
		return NULL;

	hdc = GetDC(handle);


	pixmap->hbm = LoadBitmap(DWInstance, MAKEINTRESOURCE(id));
	pixmap->hdc = CreateCompatibleDC(hdc);

	GetObject(pixmap->hbm, sizeof(BITMAP), (void *)&bm);

	pixmap->width = bm.bmWidth; pixmap->height = bm.bmHeight;

	SelectObject(pixmap->hdc, pixmap->hbm);

	ReleaseDC(handle, hdc);

	return pixmap;
}

/*
 * Destroys an allocated pixmap.
 * Parameters:
 *       pixmap: Handle to a pixmap returned by
 *               dw_pixmap_new..
 */
void dw_pixmap_destroy(HPIXMAP pixmap)
{
	if(pixmap)
	{
		DeleteDC(pixmap->hdc);
		DeleteObject(pixmap->hbm);
		free(pixmap);
	}
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
void dw_pixmap_bitblt(HWND dest, HPIXMAP destp, int xdest, int ydest, int width, int height, HWND src, HPIXMAP srcp, int xsrc, int ysrc)
{
	HDC hdcdest;
	HDC hdcsrc;

	if(dest)
		hdcdest = GetDC(dest);
	else if(destp)
		hdcdest = destp->hdc;
	else
		return;

	if(src)
		hdcsrc = GetDC(src);
	else if(srcp)
		hdcsrc = srcp->hdc;
	else
		return;

	BitBlt(hdcdest, xdest, ydest, width, height, hdcsrc, xsrc, ysrc, SRCCOPY);

	if(!destp)
		ReleaseDC(dest, hdcdest);
	if(!srcp)
		ReleaseDC(src, hdcsrc);
}

/*
 * Emits a beep.
 * Parameters:
 *       freq: Frequency.
 *       dur: Duration.
 */
void dw_beep(int freq, int dur)
{
	Beep(freq, dur);
}

/*
 * Returns the handle to an unnamed mutex semaphore.
 */
HMTX dw_mutex_new(void)
{
	return (HMTX)CreateMutex(NULL, FALSE, NULL);
}

/*
 * Closes a semaphore created by dw_mutex_new().
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_close(HMTX mutex)
{
	CloseHandle((HANDLE)mutex);
}

/*
 * Tries to gain access to the semaphore, if it can't it blocks.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_lock(HMTX mutex)
{
	if(_dwtid == dw_thread_id())
	{
		int rc = WaitForSingleObject((HANDLE)mutex, 0);

		while(rc == WAIT_TIMEOUT)
		{
			dw_main_sleep(1);
			rc = WaitForSingleObject((HANDLE)mutex, 0);
		}
	}
    else
		WaitForSingleObject((HANDLE)mutex, INFINITE);
}

/*
 * Reliquishes the access to the semaphore.
 * Parameters:
 *       mutex: The handle to the mutex returned by dw_mutex_new().
 */
void dw_mutex_unlock(HMTX mutex)
{
	ReleaseMutex((HANDLE)mutex);
}

/*
 * Returns the handle to an unnamed event semaphore.
 */
HEV dw_event_new(void)
{
    return CreateEvent(NULL, TRUE, FALSE, NULL);
}

/*
 * Resets a semaphore created by dw_event_new().
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_reset(HEV eve)
{
	return ResetEvent(eve);
}

/*
 * Posts a semaphore created by dw_event_new(). Causing all threads
 * waiting on this event in dw_event_wait to continue.
 * Parameters:
 *       eve: The handle to the event returned by dw_event_new().
 */
int dw_event_post(HEV eve)
{
	return SetEvent(eve);
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

	rc = WaitForSingleObject(eve, timeout);
	if(rc == WAIT_OBJECT_0)
		return 1;
	if(rc == WAIT_ABANDONED)
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
	if(eve)
		return CloseHandle(*eve);
	return FALSE;
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
#if defined(__CYGWIN__)
	return 0;
#else
	return (DWTID)_beginthread((void(*)(void *))func, stack, data);
#endif
}

/*
 * Ends execution of current thread immediately.
 */
void dw_thread_end(void)
{
#if !defined(__CYGWIN__)
	_endthread();
#endif
}

/*
 * Returns the current thread's ID.
 */
DWTID dw_thread_id(void)
{
#if defined(__CYGWIN__)
	return 0;
#else
	return (DWTID)GetCurrentThreadId();
#endif
}

/*
 * Cleanly terminates a DW session, should be signal handler safe.
 * Parameters:
 *       exitcode: Exit code reported to the operating system.
 */
void dw_exit(int exitcode)
{
	exit(exitcode);
}

/*
 * Creates a splitbar window (widget) with given parameters.
 * Parameters:
 *       type: Value can be BOXVERT or BOXHORZ.
 *       topleft: Handle to the window to be top or left.
 *       bottomright:  Handle to the window to be bottom or right.
 * Returns:
 *       A handle to a splitbar window or NULL on failure.
 */
HWND dw_splitbar_new(int type, HWND topleft, HWND bottomright, unsigned long id)
{
	HWND tmp = CreateWindow(SplitbarClassName,
							"",
							WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN,
							0,0,2000,1000,
							DW_HWND_OBJECT,
							(HMENU)id,
							DWInstance,
							NULL);

	if(tmp)
	{
		HWND tmpbox = dw_box_new(BOXVERT, 0);
        float *percent = (float *)malloc(sizeof(float));

		dw_box_pack_start(tmpbox, topleft, 1, 1, TRUE, TRUE, 0);
		SetParent(tmpbox, tmp);
		dw_window_set_data(tmp, "_dw_topleft", (void *)tmpbox);

		tmpbox = dw_box_new(BOXVERT, 0);
		dw_box_pack_start(tmpbox, bottomright, 1, 1, TRUE, TRUE, 0);
		SetParent(tmpbox, tmp);
		dw_window_set_data(tmp, "_dw_bottomright", (void *)tmpbox);
		*percent = 50.0;
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
void dw_splitbar_set(HWND handle, float percent)
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
float dw_splitbar_get(HWND handle)
{
	float *percent = (float *)dw_window_get_data(handle, "_dw_percent");

	if(percent)
		return *percent;
	return 0.0;
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
void dw_box_pack_end(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad)
{
	Box *thisbox;

	thisbox = (Box *)GetWindowLong(box, GWL_USERDATA);
	if(thisbox)
	{
		int z;
		Item *tmpitem, *thisitem = thisbox->items;
		char tmpbuf[100];

		tmpitem = malloc(sizeof(Item)*(thisbox->count+1));

		for(z=0;z<thisbox->count;z++)
		{
			tmpitem[z+1] = thisitem[z];
		}

		GetClassName(item, tmpbuf, 99);

		if(strnicmp(tmpbuf, FRAMECLASSNAME, 2)==0)
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

		if(thisbox->count)
			free(thisitem);

		thisbox->count++;

		SetParent(item, box);
		if(strncmp(tmpbuf, UPDOWN_CLASS, strlen(UPDOWN_CLASS))==0)
		{
			ColorInfo *cinfo = (ColorInfo *)GetWindowLong(item, GWL_USERDATA);

			if(cinfo)
			{
				SetParent(cinfo->buddy, box);
				ShowWindow(cinfo->buddy, SW_SHOW);
				SendMessage(item, UDM_SETBUDDY, (WPARAM)cinfo->buddy, 0);
			}
		}
	}
}

/*
 * Sets the default focus item for a window/dialog.
 * Parameters:
 *         window: Toplevel window or dialog.
 *         defaultitem: Handle to the dialog item to be default.
 */
void dw_window_default(HWND window, HWND defaultitem)
{
	Box *thisbox = (Box *)GetWindowLong(window, GWL_USERDATA);

	if(thisbox)
		thisbox->defaultitem = defaultitem;
}

/*
 * Sets window to click the default dialog item when an ENTER is pressed.
 * Parameters:
 *         window: Window (widget) to look for the ENTER press.
 *         next: Window (widget) to move to next (or click)
 */
void dw_window_click_default(HWND window, HWND next)
{
	ColorInfo *cinfo = (ColorInfo *)GetWindowLong(window, GWL_USERDATA);

	if(cinfo)
		cinfo->clickdefault = next;
}

/*
 * Returns some information about the current operating environment.
 * Parameters:
 *       env: Pointer to a DWEnv struct.
 */
void dw_environment_query(DWEnv *env)
{
	if(!env)
		return;

	/* Get the Windows version. */

	env->MajorVersion =  (DWORD)(LOBYTE(LOWORD(dwVersion)));
	env->MinorVersion =  (DWORD)(HIBYTE(LOWORD(dwVersion)));

	/* Get the build number for Windows NT/Windows 2000. */

	env->MinorBuild =  0;

	if (dwVersion < 0x80000000)
	{
		if(env->MajorVersion == 5 && env->MinorVersion == 1)
			strcpy(env->osName, "Windows XP");
		else if(env->MajorVersion == 5 && env->MinorVersion == 0)
			strcpy(env->osName, "Windows 2000");
		else
			strcpy(env->osName, "Windows NT");

		env->MajorBuild = (DWORD)(HIWORD(dwVersion));
	}
	else
	{
		strcpy(env->osName, "Windows 95/98/ME");
		env->MajorBuild =  0;
	}

	strcpy(env->buildDate, __DATE__);
	strcpy(env->buildTime, __TIME__);
	env->DWMajorVersion = DW_MAJOR_VERSION;
	env->DWMinorVersion = DW_MINOR_VERSION;
	env->DWSubVersion = DW_SUB_VERSION;
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
char *dw_file_browse(char *title, char *defpath, char *ext, int flags)
{
	OPENFILENAME of;
	char filenamebuf[1001] = "";
	int rc;

	if(ext)
	{
		strcpy(filenamebuf, "*.");
		strcat(filenamebuf, ext);
	}

	memset(&of, 0, sizeof(OPENFILENAME));

	of.lStructSize = sizeof(OPENFILENAME);
	of.hwndOwner = HWND_DESKTOP;
	of.hInstance = DWInstance;
	of.lpstrInitialDir = defpath;
	of.lpstrTitle = title;
	of.lpstrFile = filenamebuf;
	of.nMaxFile = 1000;
	of.lpstrDefExt = ext;
	of.Flags = 0;

	if(flags & DW_FILE_SAVE)
		rc = GetSaveFileName(&of);
	else
		rc = GetOpenFileName(&of);

	if(rc)
		return strdup(of.lpstrFile);

	return NULL;
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
	char **newparams;
	int retcode, count = 0, z;

	while(params[count])
	{
		count++;
	}

	newparams = (char **)malloc(sizeof(char *) * (count+1));

	for(z=0;z<count;z++)
	{
		newparams[z] = malloc(strlen(params[z])+3);
		strcpy(newparams[z], "\"");
		strcat(newparams[z], params[z]);
		strcat(newparams[z], "\"");
	}
	newparams[count] = NULL;

	retcode = spawnvp(P_NOWAIT, program, newparams);

	for(z=0;z<count;z++)
	{
		free(newparams[z]);
	}
	free(newparams);

	return retcode;
}

/*
 * Loads a web browser pointed at the given URL.
 * Parameters:
 *       url: Uniform resource locator.
 */
int dw_browse(char *url)
{
	char *browseurl = url;
	int retcode;

	if(strlen(url) > 7 && strncmp(url, "file://", 7) == 0)
	{
		int len, z;

		browseurl = &url[7];
		len = strlen(browseurl);

		for(z=0;z<len;z++)
		{
			if(browseurl[z] == '|')
				browseurl[z] = ':';
			if(browseurl[z] == '/')
				browseurl[z] = '\\';
		}
	}

	retcode = (int)ShellExecute(NULL, "open", browseurl, NULL, NULL, SW_SHOWNORMAL);
	if(retcode<33 && retcode != 2)
		return -1;
	return 1;
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
		/* Figure out how to do this the "Windows way" */
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
void dw_window_function(HWND handle, void *function, void *data)
{
	SendMessage(handle, WM_USER, (WPARAM)function, (LPARAM)data);
}

/* Functions for managing the user data lists that are associated with
 * a given window handle.  Used in dw_window_set_data() and
 * dw_window_get_data().
 */
UserData *find_userdata(UserData **root, char *varname)
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

int new_userdata(UserData **root, char *varname, void *data)
{
	UserData *new = find_userdata(root, varname);

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

int remove_userdata(UserData **root, char *varname, int all)
{
	UserData *prev = NULL, *tmp = *root;

	while(tmp)
	{
		if(all || stricmp(tmp->varname, varname) == 0)
		{
			if(!prev)
			{
				free(tmp->varname);
				free(tmp);
				*root = tmp->next;
				return 0;
			}
			else
			{
				prev->next = tmp->next;
				free(tmp->varname);
				free(tmp);
				return 0;
			}
		}
		prev = tmp;
		tmp = tmp->next;
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
	ColorInfo *cinfo = (ColorInfo *)GetWindowLong(window, GWL_USERDATA);

	if(!cinfo)
	{
		cinfo = calloc(1, sizeof(ColorInfo));
		SetWindowLong(window, GWL_USERDATA, (LONG)cinfo);
	}

	if(cinfo)
	{
		if(data)
			new_userdata(&(cinfo->root), dataname, data);
		else
		{
			if(dataname)
				remove_userdata(&(cinfo->root), dataname, FALSE);
			else
				remove_userdata(&(cinfo->root), NULL, TRUE);
		}
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
	ColorInfo *cinfo = (ColorInfo *)GetWindowLong(window, GWL_USERDATA);

	if(cinfo && cinfo->root && dataname)
	{
		UserData *ud = find_userdata(&(cinfo->root), dataname);
		if(ud)
			return ud->data;
	}
	return NULL;
}

/*
 * Add a callback to a window event.
 * Parameters:
 *       window: Window handle of signal to be called back.
 *       signame: A string pointer identifying which signal to be hooked.
 *       sigfunc: The pointer to the function to be used as the callback.
 *       data: User data to be passed to the handler function.
 */
void dw_signal_connect(HWND window, char *signame, void *sigfunc, void *data)
{
	ULONG message = 0L;

	if(window && signame && sigfunc)
	{
		if(stricmp(signame, "set-focus") == 0)
			window = _normalize_handle(window);

		if((message = _findsigmessage(signame)) != 0)
			_new_signal(message, window, sigfunc, data);
	}
}

/*
 * Removes callbacks for a given window with given name.
 * Parameters:
 *       window: Window handle of callback to be removed.
 */
void dw_signal_disconnect_by_name(HWND window, char *signame)
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
void dw_signal_disconnect_by_window(HWND window)
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
void dw_signal_disconnect_by_data(HWND window, void *data)
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

#ifdef TEST
HWND mainwindow,
	 listbox,
	 okbutton,
	 cancelbutton,
	 lbbox,
	 stext,
	 buttonbox,
	 testwindow,
	 testbox,
	 testok,
	 testcancel,
	 testbox2,
	 testok2,
	 testcancel2,
	 notebook;
int count = 2;

#ifdef USE_FILTER
/* Do any handling you need in the filter function */
LONG testfilter(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
	switch(msg)
	{
	case WM_COMMAND:
		switch (COMMANDMSG(&msg)->cmd)
		{
		case 1001L:
		case 1002L:
			dw_window_destroy(mainwindow);;
			count--;
			break;
		case 1003L:
		case 1004L:
			dw_window_destroy(testwindow);;
            count--;
			break;
		}
		if(!count)
			exit(0);
		break;
	}
	/* Return -1 to allow the default handlers to return. */
	return TRUE;
}
#else
int test_callback(HWND window, void *data)
{
	dw_window_destroy((HWND)data);
	/* Return -1 to allow the default handlers to return. */
	count--;
	if(!count)
        exit(0);
	return -1;
}
#endif

/*
 * Let's demonstrate the functionality of this library. :)
 */
int main(int argc, char *argv[])
{
	ULONG flStyle = DW_FCF_SYSMENU | DW_FCF_TITLEBAR |
		DW_FCF_SHELLPOSITION | DW_FCF_TASKLIST | DW_FCF_DLGBORDER;
	int pageid;

	dw_init(TRUE, argc, argv);

	/* Try a little server dialog. :) */
	mainwindow = dw_window_new(HWND_DESKTOP, "Server", flStyle | DW_FCF_SIZEBORDER | DW_FCF_MINMAX);

	lbbox = dw_box_new(BOXVERT, 10);

	dw_box_pack_start(mainwindow, lbbox, 0, 0, TRUE, TRUE, 0);

	stext = dw_text_new("Choose a server:", 0);

	dw_window_set_style(stext, DW_DT_VCENTER, DW_DT_VCENTER);

	dw_box_pack_start(lbbox, stext, 130, 15, FALSE, FALSE, 10);

	listbox = dw_listbox_new(100L, FALSE);

	dw_box_pack_start(lbbox, listbox, 130, 200, TRUE, TRUE, 10);

	buttonbox = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(lbbox, buttonbox, 0, 0, TRUE, TRUE, 0);

	okbutton = dw_button_new("Ok", 1001L);

	dw_box_pack_start(buttonbox, okbutton, 50, 30, TRUE, TRUE, 5);

	cancelbutton = dw_button_new("Cancel", 1002L);

	dw_box_pack_start(buttonbox, cancelbutton, 50, 30, TRUE, TRUE, 5);

	/* Set some nice fonts and colors */
	dw_window_set_color(lbbox, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
	dw_window_set_color(buttonbox, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
	dw_window_set_font(stext, "9.WarpSans");
	dw_window_set_color(stext, DW_CLR_BLACK, DW_CLR_PALEGRAY);
	dw_window_set_font(listbox, "9.WarpSans");
	dw_window_set_font(okbutton, "9.WarpSans");
	dw_window_set_font(cancelbutton, "9.WarpSans");

	dw_window_show(mainwindow);

	dw_window_set_usize(mainwindow, 170, 340);

	/* Another small example */
	flStyle |= DW_FCF_MINMAX | DW_FCF_SIZEBORDER;

	testwindow = dw_window_new(HWND_DESKTOP, "Wow a test dialog! :) yay!", flStyle);

	testbox = dw_box_new(BOXVERT, 0);

	dw_box_pack_start(testwindow, testbox, 0, 0, TRUE, TRUE, 0);

	notebook = dw_notebook_new(1010L, TRUE);

	dw_box_pack_start(testbox, notebook, 100, 100, TRUE, TRUE, 0);

	testbox = dw_box_new(BOXVERT, 10);

	pageid = dw_notebook_page_new(notebook, 0L, FALSE);

	dw_notebook_page_set_text(notebook, pageid, "Test page");
	dw_notebook_page_set_status_text(notebook, pageid, "Test page");

    dw_notebook_pack(notebook, pageid, testbox);

	testok = dw_button_new("Ok", 1003L);

	dw_box_pack_start(testbox, testok, 60, 40, TRUE, TRUE, 10);

	testcancel = dw_button_new("Cancel", 1004L);

	dw_box_pack_start(testbox, testcancel, 60, 40, TRUE, TRUE, 10);

	testbox2 = dw_box_new(BOXHORZ, 0);

	dw_box_pack_start(testbox, testbox2, 0, 0, TRUE, TRUE, 0);

	testok2 = dw_button_new("Ok", 1003L);

	dw_box_pack_start(testbox2, testok2, 60, 40, TRUE, TRUE, 10);

	dw_box_pack_splitbar_start(testbox2);

	testcancel2 = dw_button_new("Cancel", 1004L);

	dw_box_pack_start(testbox2, testcancel2, 60, 40, TRUE, TRUE, 10);

	/* Set some nice fonts and colors */
	dw_window_set_color(testbox, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
	dw_window_set_color(testbox2, DW_CLR_PALEGRAY, DW_CLR_PALEGRAY);
	dw_window_set_font(testok, "9.WarpSans");
	dw_window_set_font(testcancel, "9.WarpSans");
	dw_window_set_font(testok2, "9.WarpSans");
	dw_window_set_font(testcancel2, "9.WarpSans");

	dw_window_show(testwindow);

#ifdef USE_FILTER

	dw_main(0L, (void *)testfilter);
#else
	/* Setup the function callbacks */
	dw_signal_connect(okbutton, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)mainwindow);
	dw_signal_connect(cancelbutton, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)mainwindow);
	dw_signal_connect(testok, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)testwindow);
	dw_signal_connect(testcancel, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)testwindow);
	dw_signal_connect(testok2, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)testwindow);
	dw_signal_connect(testcancel2, "clicked", DW_SIGNAL_FUNC(test_callback), (void *)testwindow);
	dw_signal_connect(mainwindow, "delete_event", DW_SIGNAL_FUNC(test_callback), (void *)mainwindow);
	dw_signal_connect(testwindow, "delete_event", DW_SIGNAL_FUNC(test_callback), (void *)testwindow);

	dw_main(0L, NULL);
#endif

	return 0;
}
#endif