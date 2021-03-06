This is a stable release of Dynamic Windows version 3.0.

The current Dynamic Windows source base is considered stable on:
OS/2, Mac, Windows, Linux, FreeBSD and Solaris.

Known problems:

Boxes with no expandable items will have their contents centered on 
    GTK2 instead of top or left justified on the other platforms.
GTK3 due to changes in the core architecture does not support
    widgets that are smaller than what is contained within them
    unless they use scrolled windows. GTK2 and other platforms do.
In Unicode mode on OS/2 there are some bugs in the input controls,
    minor bugs in entryfield based controls and major bugs in the MLE.
    The text displays properly but the cursor and selection jumps
    around oddly when passing over multibyte characters.
Watcom builds on OS/2 currently leak handles when threads end.
    If you require Watcom on OS/2 stay with version 2.5 or
    do not use threads until fixed in a future version.

Known limitations:

It is not safe on all platforms to operate on widgets before they
are packed.  For portability pack widgets before operating on them.

Future features:

OS/2 is currently missing the HTML widget because the system does 
not support it by default. Looking into importing functionality 
from available libraries (Firefox, Webkit, Qt, etc).

Changes from version 2.5:
Added package configuration (pkg-config) support on Unix.
Changed DW_CLR_DEFAULT behavior to improve consistency.
Added dw_signal_connect_data() which features a callback on signal disconnection.
Improvements for building on 64bit Windows with MinGW.
   Window styles, HTML and Toolbar widgets are now supported.
Added dw_shutdown() for use when shutting down Dynamic Windows but not quite ready
   to exit immediately. (Functions like dw_exit() without the exit())
Separated the container "title" (string) and "data" (pointer) into separate spaces.
   The "classic" functions which take (char *) parameters now maintain their
   own string memory backing so you no longer need to keep the data available.
Removed dw_container_set_row_data() and dw_container_change_row_data() macros.
Added dw_container_set_row_data() and dw_container_change_row_data() functions.
Removed the "_dw_textcomp" container data flag, dw_container_cursor() and
   dw_container_delete_row() which take (char *) now function in text compare mode.
Added dw_container_cursor_by_data() and dw_container_delete_row_by_data() 
   functions which do the same things except in pointer comparison mode.
Added DW_CR_RETDATA flag to dw_container_query_*() functions to return the
   data pointer instead of the string pointer, this may change in the future.
Added exported internal functions _dw_init_thread() and _dw_deinit_thread()
    for language bindings to setup/cleanup threads created for DW use.
Fixed some memory leaks.
WARNING: Changed how tree and container titles are returned, they are now duplicated 
   and need to be freed with dw_free().  This affects the following functions:
   dw_tree_get_title(), dw_container_query_start() and dw_container_query_next()
   You should audit any code using these functions for leaks if using verison 3.

Dynamic Windows Documentation is available at:

http://dbsoft.org/dw_help.php

If you have any questions or suggestions feel free to email me at:

brian@dbsoft.org

Thanks!

Brian Smith
