/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ereport.c:  Records transactions, reports errors to administrators, etc.
 * 
 * Rob McCool
 */


#include "private/pprio.h" /* for nspr20 binary release */
#include "netsite.h"
#include "file.h"      /* system_fopenWA, system_write_atomic */
#include "util.h"      /* util_vsprintf */
#include "ereport.h"
#include "slapi-plugin.h"

#include "base/dbtbase.h"

#include <stdarg.h>
#include <stdio.h>      /* vsprintf */
#include <string.h>     /* strcpy */
#include <time.h>       /* localtime */

/* taken from ACL plugin acl.h */
#define ACL_PLUGIN_NAME "NSACLPlugin"

NSAPI_PUBLIC int ereport_v(int degree, char *fmt, va_list args)
{
    char errstr[MAX_ERROR_LEN];

    util_vsnprintf(errstr, MAX_ERROR_LEN, fmt, args);
    switch (degree) 
    {
        case LOG_WARN:
        case LOG_FAILURE:
        case LOG_INFORM:
        case LOG_VERBOSE:
        case LOG_MISCONFIG:
//            slapi_log_error(SLAPI_LOG_PLUGIN, ACL_PLUGIN_NAME, errstr);
            break;
        case LOG_SECURITY:
//            slapi_log_error(SLAPI_LOG_ACL, ACL_PLUGIN_NAME, errstr);
            break;
        case LOG_CATASTROPHE:
//            slapi_log_error(SLAPI_LOG_FATAL, ACL_PLUGIN_NAME, errstr);
            break;
	default:
            break;
    }
    return IO_OKAY;
}

NSAPI_PUBLIC int ereport(int degree, char *fmt, ...)
{
    va_list args;
    int rv;
    va_start(args, fmt);
    rv = ereport_v(degree, fmt, args);
    va_end(args);
    return rv;
}
