/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * error.c - Handle error recovery
 *
 * All blame to Mike McCool
 */

#include "libadmin/libadmin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XP_WIN32
#include <windows.h>
#include "base/nterr.h"
#endif
#include <base/file.h>

#define ERROR_HTML "error.html"

/* Be sure to edit libadmin.h and add new #define types for these headers. */
char *error_headers[MAX_ERROR] =
  {"File System Error",
   "Memory Error",
   "System Error",
   "Incorrect Usage",
   "Form Element Missing",
   "Registry Database Error",
   "Network Error",
   "Unexpected Failure",
   "Warning"};

#ifdef XP_UNIX
#define get_error() errno
#define verbose_error() system_errmsg()
#else /* XP_WIN32 */
int get_error()
{
    int error = GetLastError();
    return(error ? error: WSAGetLastError());
}
char *verbose_error()
{
    /* Initialize error hash tables */
    HashNtErrors();
    return alert_word_wrap(system_errmsg(), WORD_WRAP_WIDTH, "\\n");
}
#endif /* XP_WIN32 */

void _report_error(int type, char *info, char *details, int shouldexit)
{
    /* Be sure headers are terminated. */
    fputs("\n", stdout);

    fprintf(stdout, "<SCRIPT LANGUAGE=\"JavaScript\">");
    output_alert(type, info, details, 0);
    if(shouldexit)  {
        fprintf(stdout, "if(history.length>1) history.back();"); 
    }
    fprintf(stdout, "</SCRIPT>\n");

    if(shouldexit)  {
        WSACleanup();
        exit(0);
    }
}

/*
 * Format and output a call to the JavaScript alert() function.
 * The caller must ensure a JavaScript context.
 */
NSAPI_PUBLIC void output_alert(int type, char *info, char *details, int wait)
{
    char *wrapped=NULL;
    int err;

    if(type >= MAX_ERROR)
        type=DEFAULT_ERROR;

    wrapped=alert_word_wrap(details, WORD_WRAP_WIDTH, "\\n");

    if(!info) info="";
    fprintf(stdout, (wait) ? "confirm(\"" : "alert(\"");
    fprintf(stdout, "%s:%s\\n%s", error_headers[type], info, wrapped);
    if(type==FILE_ERROR || type==SYSTEM_ERROR)  {
        err = get_error();
        if(err != 0)
            fprintf(stdout,
                        "\\n\\nThe system returned error number %d, "
                        "which is %s.", err, verbose_error());
    }
    fprintf(stdout, "\");");
}

NSAPI_PUBLIC void report_error(int type, char *info, char *details)
{
    _report_error(type, info, details, 1);
}

NSAPI_PUBLIC void report_warning(int type, char *info, char *details)
{
    _report_error(type, info, details, 0);
}

