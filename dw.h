/* $Id$ */

#ifndef _H_DW
#define _H_DW

/* Dynamic Windows version numbers */
#define DW_MAJOR_VERSION 1
#define DW_MINOR_VERSION 0
#define DW_SUB_VERSION 0

/* These corespond to the entries in the color
 * arrays in the Win32 dw.c, they are also the
 * same as DOS ANSI colors.
 */
#define DW_CLR_BLACK             0
#define DW_CLR_DARKRED           1
#define DW_CLR_DARKGREEN         2
#define DW_CLR_BROWN             3
#define DW_CLR_DARKBLUE          4
#define DW_CLR_DARKPINK          5
#define DW_CLR_DARKCYAN          6
#define DW_CLR_PALEGRAY          7
#define DW_CLR_DARKGRAY          8
#define DW_CLR_RED               9
#define DW_CLR_GREEN             10
#define DW_CLR_YELLOW            11
#define DW_CLR_BLUE              12
#define DW_CLR_PINK              13
#define DW_CLR_CYAN              14
#define DW_CLR_WHITE             15
#define DW_CLR_DEFAULT           16

/* Signal handler defines */
#define DW_SIGNAL_CONFIGURE      "configure_event"
#define DW_SIGNAL_KEY_PRESS      "key_press_event"
#define DW_SIGNAL_BUTTON_PRESS   "button_press_event"
#define DW_SIGNAL_BUTTON_RELEASE "button_release_event"
#define DW_SIGNAL_MOTION_NOTIFY  "motion_notify_event"
#define DW_SIGNAL_DELETE         "delete_event"
#define DW_SIGNAL_EXPOSE         "expose_event"
#define DW_SIGNAL_CLICKED        "clicked"
#define DW_SIGNAL_ITEM_ENTER     "container-select"
#define DW_SIGNAL_ITEM_CONTEXT   "container-context"
#define DW_SIGNAL_ITEM_SELECT    "tree-select"
#define DW_SIGNAL_LIST_SELECT    "item-select"
#define DW_SIGNAL_SET_FOCUS      "set-focus"
#define DW_SIGNAL_VALUE_CHANGED  "value_changed"
#define DW_SIGNAL_SWITCH_PAGE    "switch-page"

#if defined(__OS2__) || defined(__WIN32__) || defined(__MAC__) || defined(WINNT) || defined(__EMX__)
/* OS/2, Windows or MacOS */

#if defined(__IBMC__) && !defined(API)
#define API _System
#endif

/* Used internally */
#define TYPEBOX  0
#define TYPEITEM 1

#define SIZESTATIC 0
#define SIZEEXPAND 1

#define SPLITBAR_WIDTH 4
#define BUBBLE_HELP_MAX 256

typedef struct _user_data
{
	struct _user_data *next;
	void              *data;
	char              *varname;
} UserData;

/* OS/2 Specific section */
#if defined(__OS2__) || defined(__EMX__)
#define INCL_DOS
#define INCL_WIN
#define INCL_GPI

#include <os2.h>

#define DW_DT_LEFT               DT_LEFT
#define DW_DT_QUERYEXTENT        DT_QUERYEXTENT
#define DW_DT_UNDERSCORE         DT_UNDERSCORE
#define DW_DT_STRIKEOUT          DT_STRIKEOUT
#define DW_DT_TEXTATTRS          DT_TEXTATTRS
#define DW_DT_EXTERNALLEADING    DT_EXTERNALLEADING
#define DW_DT_CENTER             DT_CENTER
#define DW_DT_RIGHT              DT_RIGHT
#define DW_DT_TOP                DT_TOP
#define DW_DT_VCENTER            DT_VCENTER
#define DW_DT_BOTTOM             DT_BOTTOM
#define DW_DT_HALFTONE           DT_HALFTONE
#define DW_DT_MNEMONIC           DT_MNEMONIC
#define DW_DT_WORDBREAK          DT_WORDBREAK
#define DW_DT_ERASERECT          DT_ERASERECT

#ifndef FCF_CLOSEBUTTON
#define FCF_CLOSEBUTTON            0x04000000L
#endif

#define DW_FCF_TITLEBAR          FCF_TITLEBAR
#define DW_FCF_SYSMENU           (FCF_SYSMENU | FCF_CLOSEBUTTON)
#define DW_FCF_MENU              FCF_MENU
#define DW_FCF_SIZEBORDER        FCF_SIZEBORDER
#define DW_FCF_MINBUTTON         FCF_MINBUTTON
#define DW_FCF_MAXBUTTON         FCF_MAXBUTTON
#define DW_FCF_MINMAX            FCF_MINMAX
#define DW_FCF_VERTSCROLL        FCF_VERTSCROLL
#define DW_FCF_HORZSCROLL        FCF_HORZSCROLL
#define DW_FCF_DLGBORDER         FCF_DLGBORDER
#define DW_FCF_BORDER            FCF_BORDER
#define DW_FCF_SHELLPOSITION     FCF_SHELLPOSITION
#define DW_FCF_TASKLIST          FCF_TASKLIST
#define DW_FCF_NOBYTEALIGN       FCF_NOBYTEALIGN
#define DW_FCF_NOMOVEWITHOWNER   FCF_NOMOVEWITHOWNER
#define DW_FCF_SYSMODAL          FCF_SYSMODAL
#define DW_FCF_HIDEBUTTON        FCF_HIDEBUTTON
#define DW_FCF_HIDEMAX           FCF_HIDEMAX
#define DW_FCF_AUTOICON          FCF_AUTOICON

#define DW_CFA_BITMAPORICON      CFA_BITMAPORICON
#define DW_CFA_STRING            CFA_STRING
#define DW_CFA_ULONG             CFA_ULONG
#define DW_CFA_TIME              CFA_TIME
#define DW_CFA_DATE              CFA_DATE
#define DW_CFA_CENTER            CFA_CENTER
#define DW_CFA_LEFT              CFA_LEFT
#define DW_CFA_RIGHT             CFA_RIGHT
#define DW_CFA_HORZSEPARATOR     CFA_HORZSEPARATOR
#define DW_CFA_SEPARATOR         CFA_SEPARATOR

#define DW_CRA_SELECTED          CRA_SELECTED
#define DW_CRA_CURSORED          CRA_CURSORED

#define DW_LS_MULTIPLESEL        LS_MULTIPLESEL

#define DW_LIT_NONE              -1

#define DW_MLE_CASESENSITIVE     MLFSEARCH_CASESENSITIVE

#define DW_POINTER_ARROW         SPTR_ARROW
#define DW_POINTER_CLOCK         SPTR_WAIT

#define DW_OS2_NEW_WINDOW        1

/* flag values for dw_messagebox() */
#define DW_MB_OK                 MB_OK
#define DW_MB_OKCANCEL           MB_OKCANCEL
#define DW_MB_YESNO              MB_YESNO
#define DW_MB_YESNOCANCEL        MB_YESNOCANCEL

#define DW_MB_WARNING            MB_WARNING
#define DW_MB_ERROR              MB_ERROR
#define DW_MB_INFORMATION        MB_INFORMATION
#define DW_MB_QUESTION           MB_QUERY

/* Virtual Key Codes */
#define VK_LBUTTON           VK_BUTTON1
#define VK_RBUTTON           VK_BUTTON2
#define VK_MBUTTON           VK_BUTTON3
#define VK_RETURN            VK_NEWLINE
#define VK_SNAPSHOT          VK_PRINTSCRN
#define VK_CANCEL            VK_BREAK
#define VK_CAPITAL           VK_CAPSLOCK
#define VK_ESCAPE            VK_ESC
#define VK_PRIOR             VK_PAGEUP
#define VK_NEXT              VK_PAGEDOWN
#define VK_SELECT            133
#define VK_EXECUTE           134
#define VK_PRINT             135
#define VK_HELP              136
#define VK_LWIN              137
#define VK_RWIN              138
#define VK_MULTIPLY          ('*' + 128)
#define VK_ADD               ('+' + 128)
#define VK_SEPARATOR         141
#define VK_SUBTRACT          ('-' + 128)
#define VK_DECIMAL           ('.' + 128)
#define VK_DIVIDE            ('/' + 128)
#define VK_SCROLL            VK_SCRLLOCK
#define VK_LSHIFT            VK_SHIFT
#define VK_RSHIFT            147
#define VK_LCONTROL          VK_CTRL
#define VK_RCONTROL          149
#define VK_NUMPAD0           ('0' + 128)
#define VK_NUMPAD1           ('1' + 128)
#define VK_NUMPAD2           ('2' + 128)
#define VK_NUMPAD3           ('3' + 128)
#define VK_NUMPAD4           ('4' + 128)
#define VK_NUMPAD5           ('5' + 128)
#define VK_NUMPAD6           ('6' + 128)
#define VK_NUMPAD7           ('7' + 128)
#define VK_NUMPAD8           ('8' + 128)
#define VK_NUMPAD9           ('9' + 128)


typedef struct _window_data {
	PFNWP oldproc;
	UserData *root;
	HWND clickdefault;
	ULONG flags;
	void *data;
} WindowData;

typedef struct _hpixmap {
	unsigned long width, height;
	HDC hdc;
	HPS hps;
	HBITMAP hbm;
	HWND handle;
} *HPIXMAP;

typedef void *HTREEITEM;
typedef HWND HMENUI;
typedef HMODULE HMOD;
typedef unsigned short UWORD;

#define DW_NOMENU NULLHANDLE

extern HAB dwhab;
extern HMQ dwhmq;
#endif

#if defined(__MAC__)
/* MacOS specific section */
#include <Carbon/Carbon.h>

typedef ControlRef HWND;
typedef ThreadID DWTID;
typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short USHORT;
typedef short SHORT;
typedef unsigned short UWORD;
typedef short WORD ;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef unsigned UINT;
typedef int INT;
typedef void *HMTX;
typedef void *HEV;
typedef void *HMOD;
typedef void *HPIXMAP;
typedef void *HTREEITEM;
typedef void *HMENUI;

typedef struct _window_data {
	UserData *root;
	HWND clickdefault;
	ULONG flags;
	void *data;
} WindowData;

#define DW_DT_LEFT               0
#define DW_DT_QUERYEXTENT        0
#define DW_DT_UNDERSCORE         0
#define DW_DT_STRIKEOUT          0
#define DW_DT_TEXTATTRS          0
#define DW_DT_EXTERNALLEADING    0
#define DW_DT_CENTER             0
#define DW_DT_RIGHT              0
#define DW_DT_TOP                0
#define DW_DT_VCENTER            0
#define DW_DT_BOTTOM             0
#define DW_DT_HALFTONE           0
#define DW_DT_MNEMONIC           0
#define DW_DT_WORDBREAK          0
#define DW_DT_ERASERECT          0

#define DW_FCF_TITLEBAR          0
#define DW_FCF_SYSMENU           0
#define DW_FCF_MENU              0
#define DW_FCF_SIZEBORDER        0
#define DW_FCF_MINBUTTON         0
#define DW_FCF_MAXBUTTON         0
#define DW_FCF_MINMAX            0
#define DW_FCF_VERTSCROLL        0
#define DW_FCF_HORZSCROLL        0
#define DW_FCF_DLGBORDER         0
#define DW_FCF_BORDER            0
#define DW_FCF_SHELLPOSITION     0
#define DW_FCF_TASKLIST          0
#define DW_FCF_NOBYTEALIGN       0
#define DW_FCF_NOMOVEWITHOWNER   0
#define DW_FCF_SYSMODAL          0
#define DW_FCF_HIDEBUTTON        0
#define DW_FCF_HIDEMAX           0
#define DW_FCF_AUTOICON          0

#define DW_CFA_BITMAPORICON      1
#define DW_CFA_STRING            (1 << 1)
#define DW_CFA_ULONG             (1 << 2)
#define DW_CFA_TIME              (1 << 3)
#define DW_CFA_DATE              (1 << 4)
#define DW_CFA_CENTER            (1 << 5)
#define DW_CFA_LEFT              (1 << 6)
#define DW_CFA_RIGHT             (1 << 7)
#define DW_CFA_HORZSEPARATOR     0
#define DW_CFA_SEPARATOR         0

#define DW_CRA_SELECTED          1
#define DW_CRA_CURSORED          (1 << 1)

#define DW_LS_MULTIPLESEL        1

#define DW_LIT_NONE              -1

#define DW_MLE_CASESENSITIVE     MLFSEARCH_CASESENSITIVE

#define DW_POINTER_ARROW         SPTR_ARROW
#define DW_POINTER_CLOCK         SPTR_WAIT

/* flag values for dw_messagebox() */
#define DW_MB_OK                 MB_OK
#define DW_MB_OKCANCEL           MB_OKCANCEL
#define DW_MB_YESNO              MB_YESNO
#define DW_MB_YESNOCANCEL        MB_YESNOCANCEL

#define DW_MB_WARNING            MB_WARNING
#define DW_MB_ERROR              MB_ERROR
#define DW_MB_INFORMATION        MB_INFORMATION
#define DW_MB_QUESTION           MB_QUERY
#endif

/* Windows specific section */
#if defined(__WIN32__) || defined(WINNT)
#include <windows.h>
#include <commctrl.h>

#if defined(MSVC) && !defined(API)
#define API _cdecl
#endif

#define DW_DT_LEFT               SS_LEFT
#define DW_DT_QUERYEXTENT        0
#define DW_DT_UNDERSCORE         0
#define DW_DT_STRIKEOUT          0
#define DW_DT_TEXTATTRS          0
#define DW_DT_EXTERNALLEADING    0
#define DW_DT_CENTER             SS_CENTER
#define DW_DT_RIGHT              SS_RIGHT
#define DW_DT_TOP                0
#define DW_DT_VCENTER            SS_NOPREFIX
#define DW_DT_BOTTOM             0
#define DW_DT_HALFTONE           0
#define DW_DT_MNEMONIC           0
#define DW_DT_WORDBREAK          0
#define DW_DT_ERASERECT          0

#define DW_FCF_TITLEBAR          WS_CAPTION
#define DW_FCF_SYSMENU           WS_SYSMENU
#define DW_FCF_MENU              0
#define DW_FCF_SIZEBORDER        WS_THICKFRAME
#define DW_FCF_MINBUTTON         WS_MINIMIZEBOX
#define DW_FCF_MAXBUTTON         WS_MAXIMIZEBOX
#define DW_FCF_MINMAX            (WS_MINIMIZEBOX|WS_MAXIMIZEBOX)
#define DW_FCF_VERTSCROLL        WS_VSCROLL
#define DW_FCF_HORZSCROLL        WS_HSCROLL
#define DW_FCF_DLGBORDER         WS_DLGFRAME
#define DW_FCF_BORDER            WS_BORDER
#define DW_FCF_SHELLPOSITION     0
#define DW_FCF_TASKLIST          WS_VSCROLL
#define DW_FCF_NOBYTEALIGN       0
#define DW_FCF_NOMOVEWITHOWNER   0
#define DW_FCF_SYSMODAL          0
#define DW_FCF_HIDEBUTTON        WS_MINIMIZEBOX
#define DW_FCF_HIDEMAX           0
#define DW_FCF_AUTOICON          0

#define DW_CFA_BITMAPORICON      1
#define DW_CFA_STRING            (1 << 1)
#define DW_CFA_ULONG             (1 << 2)
#define DW_CFA_TIME              (1 << 3)
#define DW_CFA_DATE              (1 << 4)
#define DW_CFA_CENTER            (1 << 5)
#define DW_CFA_LEFT              (1 << 6)
#define DW_CFA_RIGHT             (1 << 7)
#define DW_CFA_HORZSEPARATOR     0
#define DW_CFA_SEPARATOR         0

#define DW_CRA_SELECTED          LVNI_SELECTED
#define DW_CRA_CURSORED          LVNI_FOCUSED

#define DW_LS_MULTIPLESEL        LBS_MULTIPLESEL

#define DW_LIT_NONE              -1

#define DW_MLE_CASESENSITIVE     1

#define DW_POINTER_ARROW         32512
#define DW_POINTER_CLOCK         32514

/* flag values for dw_messagebox() */
#define DW_MB_OK                 MB_OK
#define DW_MB_OKCANCEL           MB_OKCANCEL
#define DW_MB_YESNO              MB_YESNO
#define DW_MB_YESNOCANCEL        MB_YESNOCANCEL

#define DW_MB_WARNING            MB_ICONWARNING
#define DW_MB_ERROR              MB_ICONERROR
#define DW_MB_INFORMATION        MB_ICONINFORMATION
#define DW_MB_QUESTION           MB_ICONQUESTION

/* Key Modifiers */
#define KC_CTRL                  (1)
#define KC_SHIFT                 (1 << 1)
#define KC_ALT                   (1 << 2)

#define STATICCLASSNAME "STATIC"
#define COMBOBOXCLASSNAME "COMBOBOX"
#define LISTBOXCLASSNAME "LISTBOX"
#define BUTTONCLASSNAME "BUTTON"
#define POPUPMENUCLASSNAME "POPUPMENU"
#define EDITCLASSNAME "EDIT"
#define FRAMECLASSNAME "FRAME"
#define SCROLLBARCLASSNAME "SCROLLBAR"

#define ClassName "dynamicwindows"
#define SplitbarClassName "dwsplitbar"
#define ObjectClassName "dwobjectclass"
#define DefaultFont NULL

typedef struct _color {
	int fore;
	int back;
	HWND combo, buddy;
	int user;
	int vcenter;
	HWND clickdefault;
	HBRUSH hbrush;
	char fontname[128];
	WNDPROC pOldProc;
	UserData *root;
} ColorInfo;

typedef struct _notebookpage {
	ColorInfo cinfo;
	TC_ITEM item;
	HWND hwnd;
	int realid;
} NotebookPage;

typedef HANDLE HMTX;
typedef HANDLE HEV;
typedef HANDLE HMOD;

typedef struct _container {
	ColorInfo cinfo;
	ULONG *flags;
	WNDPROC pOldProc;
	ULONG columns;
} ContainerInfo;

typedef struct _hpixmap {
	unsigned long width, height;
	HBITMAP hbm;
	HDC hdc;
	HWND handle;
	void *bits;
} *HPIXMAP;

typedef HWND HMENUI;

#define DW_NOMENU NULL
#endif

typedef struct _item {
	/* Item type - Box or Item */
	int type;
	/* Handle to Frame or Window */
	HWND hwnd;
	/* Width and Height of static size */
	int width, height, origwidth, origheight;
	/* Size Type - Static or Expand */
	int hsize, vsize;
	/* Padding */
	int pad;
	/* Ratio of current item */
	float xratio, yratio;
} Item;

typedef struct _box {
#if defined(__WIN32__) || defined(WINNT)
	ColorInfo cinfo;
#elif defined(__OS2__) || defined(__EMX__)
	PFNWP oldproc;
	UserData *root;
	HWND hwndtitle;
	int titlebar;
#endif
	/* Number of items in the box */
	int count;
	/* Box type - horizontal or vertical */
	int type;
	/* Padding */
	int pad, parentpad;
	/* Groupbox */
	HWND grouphwnd;
	/* Default item */
	HWND defaultitem;
	/* Used as temporary storage in the calculation stage */
	int upx, upy, minheight, minwidth;
	/* Ratio in this box */
	float xratio, yratio, parentxratio, parentyratio;
	/* Used for calculating individual item ratios */
	int width, height;
	/* Any combinations of flags describing the box */
	unsigned long flags;
	/* Array of item structures */
	struct _item *items;
} Box;

typedef struct _bubblebutton {
#if defined(__WIN32__) || defined(WINNT)
	ColorInfo cinfo;
	int checkbox;
	WNDPROC pOldProc;
#endif
#if defined(__OS2__) || defined(__EMX__)
	PFNWP pOldProc;
	UserData *root;
#endif
	unsigned long id;
	char bubbletext[BUBBLE_HELP_MAX];
} BubbleButton;

void dw_box_pack_start_stub(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad);
void dw_box_pack_end_stub(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad);
#else
/* GTK Specific section */
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkprivate.h>
#include <gdk/gdkkeysyms.h>
#include <pthread.h>
#include <dlfcn.h>

#define DW_DT_LEFT               1
#define DW_DT_UNDERSCORE         (1 << 1)
#define DW_DT_STRIKEOUT          (1 << 2)
#define DW_DT_CENTER             (1 << 3)
#define DW_DT_RIGHT              (1 << 4)
#define DW_DT_TOP                (1 << 5)
#define DW_DT_VCENTER            (1 << 6)
#define DW_DT_BOTTOM             (1 << 7)
#define DW_DT_HALFTONE           (1 << 8)
#define DW_DT_MNEMONIC           (1 << 9)
#define DW_DT_WORDBREAK          (1 << 10)
#define DW_DT_ERASERECT          (1 << 11)

/* these don't exist under gtk, so make them dummy entries */
#define DW_DT_QUERYEXTENT        0
#define DW_DT_TEXTATTRS          0
#define DW_DT_EXTERNALLEADING    0

#define DW_FCF_TITLEBAR          1
#define DW_FCF_SYSMENU           (1 << 1)
#define DW_FCF_MENU              (1 << 2)
#define DW_FCF_SIZEBORDER        (1 << 3)
#define DW_FCF_MINBUTTON         (1 << 4)
#define DW_FCF_MAXBUTTON         (1 << 5)
#define DW_FCF_MINMAX            (1 << 6)
#define DW_FCF_VERTSCROLL        (1 << 7)
#define DW_FCF_HORZSCROLL        (1 << 8)
#define DW_FCF_DLGBORDER         (1 << 9)
#define DW_FCF_BORDER            (1 << 10)
#define DW_FCF_SHELLPOSITION     (1 << 11)
#define DW_FCF_TASKLIST          (1 << 12)
#define DW_FCF_NOBYTEALIGN       (1 << 13)
#define DW_FCF_NOMOVEWITHOWNER   (1 << 14)
#define DW_FCF_SYSMODAL          (1 << 15)
#define DW_FCF_HIDEBUTTON        (1 << 16)
#define DW_FCF_HIDEMAX           (1 << 17)
#define DW_FCF_AUTOICON          (1 << 18)

#define DW_CFA_BITMAPORICON      1
#define DW_CFA_STRING            (1 << 1)
#define DW_CFA_ULONG             (1 << 2)
#define DW_CFA_TIME              (1 << 3)
#define DW_CFA_DATE              (1 << 4)
#define DW_CFA_CENTER            (1 << 5)
#define DW_CFA_LEFT              (1 << 6)
#define DW_CFA_RIGHT             (1 << 7)
#define DW_CFA_HORZSEPARATOR     (1 << 8)
#define DW_CFA_SEPARATOR         (1 << 9)

#define DW_CRA_SELECTED          1
#define DW_CRA_CURSORED          (1 << 1)

#define DW_LS_MULTIPLESEL        1

#define DW_LIT_NONE              -1

#define DW_MLE_CASESENSITIVE     1

#define DW_POINTER_ARROW         GDK_TOP_LEFT_ARROW
#define DW_POINTER_CLOCK         GDK_WATCH

#define HWND_DESKTOP             ((HWND)0)

/* flag values for dw_messagebox() */
#define DW_MB_OK                 (1 << 1)
#define DW_MB_OKCANCEL           (1 << 2)
#define DW_MB_YESNO              (1 << 3)
#define DW_MB_YESNOCANCEL        (1 << 4)

#define DW_MB_WARNING            (1 << 10)
#define DW_MB_ERROR              (1 << 11)
#define DW_MB_INFORMATION        (1 << 12)
#define DW_MB_QUESTION           (1 << 13)

/* Virtual Key Codes */
#define VK_LBUTTON           GDK_Pointer_Button1
#define VK_RBUTTON           GDK_Pointer_Button3
#define VK_CANCEL            GDK_Cancel
#define VK_MBUTTON           GDK_Pointer_Button2
#define VK_BACK              GDK_BackSpace
#define VK_TAB               GDK_Tab
#define VK_CLEAR             GDK_Clear
#define VK_RETURN            GDK_Return
#define VK_MENU              GDK_Menu
#define VK_PAUSE             GDK_Pause
#define VK_CAPITAL           GDK_Caps_Lock
#define VK_ESCAPE            GDK_Escape
#define VK_SPACE             GDK_space
#define VK_PRIOR             GDK_Page_Up
#define VK_NEXT              GDK_Page_Down
#define VK_END               GDK_End
#define VK_HOME              GDK_Home
#define VK_LEFT              GDK_Left
#define VK_UP                GDK_Up
#define VK_RIGHT             GDK_Right
#define VK_DOWN              GDK_Down
#define VK_SELECT            GDK_Select
#define VK_PRINT             GDK_Sys_Req
#define VK_EXECUTE           GDK_Execute
#define VK_SNAPSHOT          GDK_Print
#define VK_INSERT            GDK_Insert
#define VK_DELETE            GDK_Delete
#define VK_HELP              GDK_Help
#define VK_LWIN              GDK_Super_L
#define VK_RWIN              GDK_Super_R
#define VK_NUMPAD0           GDK_KP_0
#define VK_NUMPAD1           GDK_KP_1
#define VK_NUMPAD2           GDK_KP_2
#define VK_NUMPAD3           GDK_KP_3
#define VK_NUMPAD4           GDK_KP_4
#define VK_NUMPAD5           GDK_KP_5
#define VK_NUMPAD6           GDK_KP_6
#define VK_NUMPAD7           GDK_KP_7
#define VK_NUMPAD8           GDK_KP_8
#define VK_NUMPAD9           GDK_KP_9
#define VK_MULTIPLY          GDK_KP_Multiply
#define VK_ADD               GDK_KP_Add
#define VK_SEPARATOR         GDK_KP_Separator
#define VK_SUBTRACT          GDK_KP_Subtract
#define VK_DECIMAL           GDK_KP_Decimal
#define VK_DIVIDE            GDK_KP_Divide
#define VK_F1                GDK_F1
#define VK_F2                GDK_F2
#define VK_F3                GDK_F3
#define VK_F4                GDK_F4
#define VK_F5                GDK_F5
#define VK_F6                GDK_F6
#define VK_F7                GDK_F7
#define VK_F8                GDK_F8
#define VK_F9                GDK_F9
#define VK_F10               GDK_F10
#define VK_F11               GDK_F11
#define VK_F12               GDK_F12
#define VK_F13               GDK_F13
#define VK_F14               GDK_F14
#define VK_F15               GDK_F15
#define VK_F16               GDK_F16
#define VK_F17               GDK_F17
#define VK_F18               GDK_F18
#define VK_F19               GDK_F19
#define VK_F20               GDK_F20
#define VK_F21               GDK_F21
#define VK_F22               GDK_F22
#define VK_F23               GDK_F23
#define VK_F24               GDK_F24
#define VK_NUMLOCK           GDK_Num_Lock
#define VK_SCROLL            GDK_Scroll_Lock
#define VK_LSHIFT            GDK_Shift_L
#define VK_RSHIFT            GDK_Shift_R
#define VK_LCONTROL          GDK_Control_L
#define VK_RCONTROL          GDK_Control_R
#define VK_LMENU             GDK_Menu
#define VK_RMENU             GDK_Menu

/* Key Modifiers */
#define KC_CTRL              GDK_CONTROL_MASK
#define KC_SHIFT             GDK_SHIFT_MASK
#define KC_ALT               GDK_MOD1_MASK

typedef GtkWidget *HWND;
#ifndef _ENVRNMNT_H
typedef unsigned long ULONG;
#endif
typedef long LONG;
typedef unsigned short USHORT;
typedef short SHORT;
typedef unsigned short UWORD;
typedef short WORD ;
typedef unsigned char UCHAR;
typedef char CHAR;
typedef unsigned UINT;
typedef int INT;
typedef pthread_mutex_t *HMTX;
typedef struct _dw_unix_event {
	pthread_mutex_t mutex;
	pthread_cond_t event;
	pthread_t thread;
	int alive;
	int posted;
} *HEV;
typedef pthread_t DWTID;
typedef void * HMOD;

typedef struct _hpixmap {
	unsigned long width, height;
	GdkPixmap *pixmap;
	HWND handle;
} *HPIXMAP;

typedef GtkWidget *HMENUI;
typedef void *HTREEITEM;

#define DW_NOMENU NULL

typedef struct _resource_struct {
	long resource_max, *resource_id;
	char **resource_data;
} DWResources;

#if !defined(DW_RESOURCES) || defined(BUILD_DLL)
static DWResources _resources = { 0, 0, 0 };
#else
extern DWResources _resources;
#endif

#endif

#if !defined(__OS2__) && !defined(__EMX__)
typedef struct _CDATE
{
	UCHAR  day;
	UCHAR  month;
	USHORT year;
} CDATE;
typedef CDATE *PCDATE;

typedef struct _CTIME
{
	UCHAR hours;
	UCHAR minutes;
	UCHAR seconds;
	UCHAR ucReserved;
} CTIME;
typedef CTIME *PCTIME;
#endif

#if defined(__OS2__) || defined(__WIN32__) || defined(WINNT) || defined(__EMX__)
typedef unsigned long DWTID;
#endif

typedef struct _dwenv {
	/* Operating System Name and DW Build Date/Time */
	char osName[30], buildDate[30], buildTime[30];
	/* Versions and builds */
	short MajorVersion, MinorVersion, MajorBuild, MinorBuild;
	/* Dynamic Window version */
	short DWMajorVersion, DWMinorVersion, DWSubVersion;
} DWEnv;


typedef struct _dwexpose {
	int x, y;
	int width, height;
} DWExpose;

typedef struct _dwdialog {
	HEV eve;
	int done;
	void *data, *result;
} DWDialog;

#define DW_SIGNAL_FUNC(a) ((void *)a)

#define DW_DESKTOP               HWND_DESKTOP
#define DW_MINIMIZED 1

#define DW_BUTTON1_MASK 1
#define DW_BUTTON2_MASK (1 << 1)
#define DW_BUTTON3_MASK (1 << 2)

#define DW_EXEC_CON 0
#define DW_EXEC_GUI 1

#define DW_FILE_OPEN 0
#define DW_FILE_SAVE 1

#define DW_HORZ 0
#define DW_VERT 1

/* Obsolete, should disappear sometime */
#define BOXHORZ DW_HORZ
#define BOXVERT DW_VERT

#define DW_SCROLL_UP 0
#define DW_SCROLL_DOWN 1
#define DW_SCROLL_TOP 2
#define DW_SCROLL_BOTTOM 3

/* return values for dw_messagebox() */
#define DW_MB_RETURN_OK           0
#define DW_MB_RETURN_YES          1
#define DW_MB_RETURN_NO           0
#define DW_MB_RETURN_CANCEL       2

#define DW_PIXMAP_WIDTH(x) (x ? x->width : 0)
#define DW_PIXMAP_HEIGHT(x) (x ? x->height : 0)

#define DW_RGB_COLOR (0xF0000000)
#define DW_RGB_TRANSPARENT (0x0F000000)
#define DW_RGB_MASK (0x00FFFFFF)
#define DW_RED_MASK (0x000000FF)
#define DW_GREEN_MASK (0x0000FF00)
#define DW_BLUE_MASK (0x00FF0000)
#define DW_RED_VALUE(a) (a & DW_RED_MASK)
#define DW_GREEN_VALUE(a) ((a & DW_GREEN_MASK) >> 8)
#define DW_BLUE_VALUE(a) ((a & DW_BLUE_MASK) >> 16)
#define DW_RGB(a, b, c) (0xF0000000 | a | b << 8 | c << 16)

#define DW_MENU_SEPARATOR ""

#if defined(__OS2__) || defined(__EMX__)
#define DW_OS2_RGB(a) ((DW_RED_VALUE(a) << 16) | (DW_GREEN_VALUE(a) << 8) | DW_BLUE_VALUE(a))
#endif

#ifndef API
#define API
#endif

#define DWSIGNAL API

/* Public function prototypes */
void API dw_box_pack_start(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad);
void API dw_box_pack_end(HWND box, HWND item, int width, int height, int hsize, int vsize, int pad);
#if !defined(__OS2__) && !defined(__WIN32__) && !defined(__EMX__) && !defined(__MAC__)
int API dw_int_init(DWResources *res, int newthread, int *argc, char **argv[]);
#define dw_init(a, b, c) dw_int_init(&_resources, a, &b, &c)
#else
int API dw_init(int newthread, int argc, char *argv[]);
#endif
void API dw_main(void);
void API dw_main_sleep(int seconds);
void API dw_main_iteration(void);
void API dw_free(void *ptr);
int API dw_window_show(HWND handle);
int API dw_window_hide(HWND handle);
int API dw_window_minimize(HWND handle);
int API dw_window_raise(HWND handle);
int API dw_window_lower(HWND handle);
int API dw_window_destroy(HWND handle);
void API dw_window_redraw(HWND handle);
int API dw_window_set_font(HWND handle, char *fontname);
int API dw_window_set_color(HWND handle, unsigned long fore, unsigned long back);
HWND API dw_window_new(HWND hwndOwner, char *title, unsigned long flStyle);
HWND API dw_box_new(int type, int pad);
HWND API dw_groupbox_new(int type, int pad, char *title);
HWND API dw_mdi_new(unsigned long id);
HWND API dw_bitmap_new(unsigned long id);
HWND API dw_bitmapbutton_new(char *text, unsigned long id);
HWND API dw_bitmapbutton_new_from_file(char *text, unsigned long id, char *filename);
HWND API dw_container_new(unsigned long id, int multi);
HWND API dw_tree_new(unsigned long id);
HWND API dw_text_new(char *text, unsigned long id);
HWND API dw_status_text_new(char *text, unsigned long id);
HWND API dw_mle_new(unsigned long id);
HWND API dw_entryfield_new(char *text, unsigned long id);
HWND API dw_entryfield_password_new(char *text, ULONG id);
HWND API dw_combobox_new(char *text, unsigned long id);
HWND API dw_button_new(char *text, unsigned long id);
HWND API dw_spinbutton_new(char *text, unsigned long id);
HWND API dw_radiobutton_new(char *text, ULONG id);
HWND API dw_percent_new(unsigned long id);
HWND API dw_slider_new(int vertical, int increments, ULONG id);
HWND API dw_scrollbar_new(int vertical, int increments, ULONG id);
HWND API dw_checkbox_new(char *text, unsigned long id);
HWND API dw_listbox_new(unsigned long id, int multi);
void API dw_listbox_append(HWND handle, char *text);
void API dw_listbox_clear(HWND handle);
int API dw_listbox_count(HWND handle);
void API dw_listbox_set_top(HWND handle, int top);
void API dw_listbox_select(HWND handle, int index, int state);
void API dw_listbox_delete(HWND handle, int index);
void API dw_listbox_query_text(HWND handle, unsigned int index, char *buffer, unsigned int length);
void API dw_listbox_set_text(HWND handle, unsigned int index, char *buffer);
unsigned int API dw_listbox_selected(HWND handle);
int API dw_listbox_selected_multi(HWND handle, int where);
unsigned int API dw_percent_query_range(HWND handle);
void API dw_percent_set_pos(HWND handle, unsigned int position);
unsigned int API dw_slider_query_pos(HWND handle);
void API dw_slider_set_pos(HWND handle, unsigned int position);
unsigned int API dw_scrollbar_query_pos(HWND handle);
void API dw_scrollbar_set_pos(HWND handle, unsigned int position);
void API dw_scrollbar_set_range(HWND handle, unsigned int range, unsigned int visible);
void API dw_window_set_pos(HWND handle, unsigned long x, unsigned long y);
void API dw_window_set_usize(HWND handle, unsigned long width, unsigned long height);
void API dw_window_set_pos_size(HWND handle, unsigned long x, unsigned long y, unsigned long width, unsigned long height);
void API dw_window_get_pos_size(HWND handle, unsigned long *x, unsigned long *y, unsigned long *width, unsigned long *height);
void API dw_window_set_style(HWND handle, unsigned long style, unsigned long mask);
void API dw_window_set_icon(HWND handle, unsigned long id);
void API dw_window_set_bitmap(HWND handle, unsigned long id, char *filename);
char * API dw_window_get_text(HWND handle);
void API dw_window_set_text(HWND handle, char *text);
int API dw_window_set_border(HWND handle, int border);
void API dw_window_disable(HWND handle);
void API dw_window_enable(HWND handle);
void API dw_window_capture(HWND handle);
void API dw_window_release(void);
void API dw_window_reparent(HWND handle, HWND newparent);
void API dw_window_pointer(HWND handle, int pointertype);
void API dw_window_default(HWND window, HWND defaultitem);
void API dw_window_click_default(HWND window, HWND next);
unsigned int API dw_mle_import(HWND handle, char *buffer, int startpoint);
void API dw_mle_export(HWND handle, char *buffer, int startpoint, int length);
void API dw_mle_query(HWND handle, unsigned long *bytes, unsigned long *lines);
void API dw_mle_delete(HWND handle, int startpoint, int length);
void API dw_mle_clear(HWND handle);
void API dw_mle_freeze(HWND handle);
void API dw_mle_thaw(HWND handle);
void API dw_mle_set(HWND handle, int point);
void API dw_mle_set_visible(HWND handle, int line);
void API dw_mle_set_editable(HWND handle, int state);
void API dw_mle_set_word_wrap(HWND handle, int state);
int API dw_mle_search(HWND handle, char *text, int point, unsigned long flags);
void API dw_spinbutton_set_pos(HWND handle, long position);
void API dw_spinbutton_set_limits(HWND handle, long upper, long lower);
void API dw_entryfield_set_limit(HWND handle, ULONG limit);
long API dw_spinbutton_query(HWND handle);
int API dw_checkbox_query(HWND handle);
void API dw_checkbox_set(HWND handle, int value);
HTREEITEM API dw_tree_insert(HWND handle, char *title, unsigned long icon, HTREEITEM parent, void *itemdata);
HTREEITEM API dw_tree_insert_after(HWND handle, HTREEITEM item, char *title, unsigned long icon, HTREEITEM parent, void *itemdata);
void API dw_tree_clear(HWND handle);
void API dw_tree_delete(HWND handle, HTREEITEM item);
void API dw_tree_set(HWND handle, HTREEITEM item, char *title, unsigned long icon);
void API dw_tree_expand(HWND handle, HTREEITEM item);
void API dw_tree_collapse(HWND handle, HTREEITEM item);
void API dw_tree_item_select(HWND handle, HTREEITEM item);
void API dw_tree_set_data(HWND handle, HTREEITEM item, void *itemdata);
void * API dw_tree_get_data(HWND handle, HTREEITEM item);
int API dw_container_setup(HWND handle, unsigned long *flags, char **titles, int count, int separator);
unsigned long API dw_icon_load(unsigned long module, unsigned long id);
unsigned long API dw_icon_load_from_file(char *filename);
void API dw_icon_free(unsigned long handle);
void * API dw_container_alloc(HWND handle, int rowcount);
void API dw_container_set_item(HWND handle, void *pointer, int column, int row, void *data);
void API dw_container_change_item(HWND handle, int column, int row, void *data);
void API dw_container_set_column_width(HWND handle, int column, int width);
void API dw_container_set_row_title(void *pointer, int row, char *title);
void API dw_container_insert(HWND handle, void *pointer, int rowcount);
void API dw_container_clear(HWND handle, int redraw);
void API dw_container_delete(HWND handle, int rowcount);
char * API dw_container_query_start(HWND handle, unsigned long flags);
char * API dw_container_query_next(HWND handle, unsigned long flags);
void API dw_container_scroll(HWND handle, int direction, long rows);
void API dw_container_cursor(HWND handle, char *text);
void API dw_container_delete_row(HWND handle, char *text);
void API dw_container_optimize(HWND handle);
int API dw_filesystem_setup(HWND handle, unsigned long *flags, char **titles, int count);
void API dw_filesystem_set_item(HWND handle, void *pointer, int column, int row, void *data);
void API dw_filesystem_set_file(HWND handle, void *pointer, int row, char *filename, unsigned long icon);
int API dw_screen_width(void);
int API dw_screen_height(void);
unsigned long API dw_color_depth(void);
HWND API dw_notebook_new(unsigned long id, int top);
unsigned long API dw_notebook_page_new(HWND handle, unsigned long flags, int front);
void API dw_notebook_page_destroy(HWND handle, unsigned int pageid);
void API dw_notebook_page_set_text(HWND handle, unsigned long pageid, char *text);
void API dw_notebook_page_set_status_text(HWND handle, unsigned long pageid, char *text);
void API dw_notebook_page_set(HWND handle, unsigned int pageid);
unsigned long API dw_notebook_page_query(HWND handle);
void API dw_notebook_pack(HWND handle, unsigned long pageid, HWND page);
HWND API dw_splitbar_new(int type, HWND topleft, HWND bottomright, unsigned long id);
void API dw_splitbar_set(HWND handle, float percent);
float API dw_splitbar_get(HWND handle);
HMENUI API dw_menu_new(unsigned long id);
HMENUI API dw_menubar_new(HWND location);
HWND API dw_menu_append_item(HMENUI menu, char *title, unsigned long id, unsigned long flags, int end, int check, HMENUI submenu);
void API dw_menu_item_set_check(HMENUI menu, unsigned long id, int check);
void API dw_menu_popup(HMENUI *menu, HWND parent, int x, int y);
void API dw_menu_destroy(HMENUI *menu);
void API dw_pointer_query_pos(long *x, long *y);
void API dw_pointer_set_pos(long x, long y);
void API dw_window_function(HWND handle, void *function, void *data);
HWND API dw_window_from_id(HWND handle, int id);
HMTX API dw_mutex_new(void);
void API dw_mutex_close(HMTX mutex);
void API dw_mutex_lock(HMTX mutex);
void API dw_mutex_unlock(HMTX mutex);
HEV API dw_event_new(void);
int API dw_event_reset(HEV eve);
int API dw_event_post(HEV eve);
int API dw_event_wait(HEV eve, unsigned long timeout);
int API dw_event_close (HEV *eve);
DWTID API dw_thread_new(void *func, void *data, int stack);
void API dw_thread_end(void);
DWTID API dw_thread_id(void);
void API dw_exit(int exitcode);
HWND API dw_render_new(unsigned long id);
void API dw_color_foreground_set(unsigned long value);
void API dw_color_background_set(unsigned long value);
void API dw_draw_point(HWND handle, HPIXMAP pixmap, int x, int y);
void API dw_draw_line(HWND handle, HPIXMAP pixmap, int x1, int y1, int x2, int y2);
void API dw_draw_rect(HWND handle, HPIXMAP pixmap, int fill, int x, int y, int width, int height);
void API dw_draw_text(HWND handle, HPIXMAP pixmap, int x, int y, char *text);
void API dw_font_text_extents(HWND handle, HPIXMAP pixmap, char *text, int *width, int *height);
void API dw_flush(void);
void API dw_pixmap_bitblt(HWND dest, HPIXMAP destp, int xdest, int ydest, int width, int height, HWND src, HPIXMAP srcp, int xsrc, int ysrc);
HPIXMAP API dw_pixmap_new(HWND handle, unsigned long width, unsigned long height, int depth);
HPIXMAP API dw_pixmap_new_from_file(HWND handle, char *filename);
HPIXMAP API dw_pixmap_grab(HWND handle, ULONG id);
void API dw_pixmap_destroy(HPIXMAP pixmap);
void API dw_beep(int freq, int dur);
int API dw_messagebox(char *title, int flags, char *format, ...);
void API dw_environment_query(DWEnv *env);
int API dw_exec(char *program, int type, char **params);
int API dw_browse(char *url);
char * API dw_file_browse(char *title, char *defpath, char *ext, int flags);
char * API dw_user_dir(void);
DWDialog * API dw_dialog_new(void *data);
int API dw_dialog_dismiss(DWDialog *dialog, void *result);
void * API dw_dialog_wait(DWDialog *dialog);
void API dw_window_set_data(HWND window, char *dataname, void *data);
void * API dw_window_get_data(HWND window, char *dataname);
int API dw_module_load(char *name, HMOD *handle);
int API dw_module_symbol(HMOD handle, char *name, void**func);
int API dw_module_close(HMOD handle);
int API dw_timer_connect(int interval, void *sigfunc, void *data);
void API dw_timer_disconnect(int id);
void API dw_signal_connect(HWND window, char *signame, void *sigfunc, void *data);
void API dw_signal_disconnect_by_window(HWND window);
void API dw_signal_disconnect_by_data(HWND window, void *data);
void API dw_signal_disconnect_by_name(HWND window, char *signame);

#endif
