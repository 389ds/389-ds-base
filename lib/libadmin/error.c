/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
#ifdef XP_WIN32
        WSACleanup();
#endif
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

