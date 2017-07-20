/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * error.c - Handle error recovery
 *
 * All blame to Mike McCool
 */

#include "libadmin/libadmin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define get_error() errno
#define verbose_error() system_errmsg()


void
_report_error(int type, char *info, char *details, int shouldexit)
{
    /* Be sure headers are terminated. */
    fputs("\n", stdout);

    fprintf(stdout, "<SCRIPT LANGUAGE=\"JavaScript\">");
    output_alert(type, info, details, 0);
    if (shouldexit) {
        fprintf(stdout, "if(history.length>1) history.back();");
    }
    fprintf(stdout, "</SCRIPT>\n");

    if (shouldexit) {
        exit(0);
    }
}

/*
 * Format and output a call to the JavaScript alert() function.
 * The caller must ensure a JavaScript context.
 */
NSAPI_PUBLIC void
output_alert(int type, char *info, char *details, int wait)
{
    char *wrapped = NULL;
    int err;

    if (type >= MAX_ERROR)
        type = DEFAULT_ERROR;

    wrapped = alert_word_wrap(details, WORD_WRAP_WIDTH, "\\n");

    if (!info)
        info = "";
    fprintf(stdout, (wait) ? "confirm(\"" : "alert(\"");
    fprintf(stdout, "%s:%s\\n%s", error_headers[type], info, wrapped);
    if (type == FILE_ERROR || type == SYSTEM_ERROR) {
        err = get_error();
        if (err != 0) {
            const char *err_str = verbose_error();
            fprintf(stdout,
                    "\\n\\nThe system returned error number %d, "
                    "which is %s.",
                    err, err_str);
            FREE(err_str);
        }
    }
    fprintf(stdout, "\");");

    FREE(wrapped);
}

NSAPI_PUBLIC void
report_error(int type, char *info, char *details)
{
    _report_error(type, info, details, 1);
}

NSAPI_PUBLIC void
report_warning(int type, char *info, char *details)
{
    _report_error(type, info, details, 0);
}
