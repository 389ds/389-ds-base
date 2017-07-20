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
 * libadmin.h - All functions contained in libadmin.a
 *
 * All blame goes to Mike McCool
 */

#ifndef libadmin_h
#define libadmin_h

#include <stdio.h>
#include <limits.h>
#include "base/systems.h"
#include "base/systhr.h"
#include "base/util.h"
#include <unistd.h>
#include "prinit.h"
#include "prthread.h"
#include "prlong.h"

#define NSPR_INIT(Program) (PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 8))

NSPR_BEGIN_EXTERN_C

/* error types */
#define FILE_ERROR 0
#define MEMORY_ERROR 1
#define SYSTEM_ERROR 2
#define INCORRECT_USAGE 3
#define ELEM_MISSING 4
#define REGISTRY_DATABASE_ERROR 5
#define NETWORK_ERROR 6
#define GENERAL_FAILURE 7
#define WARNING 8

/* The upper bound on error types */
#define MAX_ERROR 9

/* The default error type (in case something goes wrong */
#define DEFAULT_ERROR 3

#define INFO_IDX_NAME "infowin"
#define INFO_TOPIC_NAME "infotopic"
#define HELP_WIN_OPTIONS "'resizable=1,width=500,height=500'"

/* Initialize libadmin.  Should be called by EVERY CGI. */
/* util.c */
NSAPI_PUBLIC int ADM_Init(void);

/* Since everyone seems to be doing this independently, at least centralize
   the code.  Useful for onClicks and automatic help */
NSAPI_PUBLIC char *helpJavaScript(void);
NSAPI_PUBLIC char *helpJavaScriptForTopic(char *topic);

/* Report an error.  Takes 3 args: 1. Category of error
 *                                 2. Some more specific category info (opt)
 *                                 3. A short explanation of the error.
 *
 * report_warning: same thing except doesn't exit when done whining
 */
/* error.c */
NSAPI_PUBLIC void output_alert(int type, char *info, char *details, int wait);
NSAPI_PUBLIC void report_error(int type, char *info, char *details);
NSAPI_PUBLIC void report_warning(int type, char *info, char *details);

/* Word wrap a string to fit into a JavaScript alert box. */
/* str is the string, width is the width to wrap to, linefeed is the string
 * to use as a linefeed. */
/* util.c */
#define WORD_WRAP_WIDTH 80
NSAPI_PUBLIC char *alert_word_wrap(char *str, int width, char *linefeed);

/* Get the admin/userdb directory. */
/* util.c */
NSAPI_PUBLIC char *get_userdb_dir(void);

NSAPI_PUBLIC char *cookieValue(char *, char *);

NSPR_END_EXTERN_C

#endif /* libadmin_h */
