/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * authdb.c:  Functions to aid in user/group database admin
 *            
 * These things leak memory like a sieve.  
 *            
 * Ben Polk
 *    (blame Mike McCool for functions with an MLM)
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "netsite.h"

/* Get the userdb directory. (V1.x) */
NSAPI_PUBLIC char *get_userdb_dir(void)
{
    char *userdb;
    char line[BIG_LINE];

#ifdef USE_ADMSERV
    char *tmp = getenv("NETSITE_ROOT");
    
    sprintf(line, "%s%cuserdb", tmp, FILE_PATHSEP);
#else
    char *tmp = get_mag_var("#ServerRoot");
    
    sprintf(line, "%s%cadmin%cuserdb", tmp, FILE_PATHSEP, FILE_PATHSEP);
#endif
    userdb = STRDUP(line);
    return userdb;
}
