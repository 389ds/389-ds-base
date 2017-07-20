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

#ifndef BASE_FILE_H
#define BASE_FILE_H

#ifndef NOINTNSAPI
#define INTNSAPI
#endif /* !NOINTNSAPI */

/* GLOBAL FUNCTIONS:
 * DESCRIPTION:
 * system-specific functions for reading/writing files
 */

#ifndef NETSITE_H
#include "../netsite.h"
#endif /* !NETSITE_H */

/* --- Begin function prototypes --- */

#ifdef INTNSAPI

NSPR_BEGIN_EXTERN_C

void INTsystem_errmsg_init(void);

NSAPI_PUBLIC SYS_FILE INTsystem_fopenRO(char *path);
NSAPI_PUBLIC SYS_FILE INTsystem_fopenWA(char *path);
NSAPI_PUBLIC SYS_FILE INTsystem_fopenRW(char *path);
NSAPI_PUBLIC SYS_FILE INTsystem_fopenWT(char *path);
NSAPI_PUBLIC int INTsystem_fread(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC int INTsystem_fwrite(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC int INTsystem_fwrite_atomic(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC int INTsystem_lseek(SYS_FILE fd, int off, int wh);
NSAPI_PUBLIC int INTsystem_fclose(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_stat(char *name, struct stat *finfo);
NSAPI_PUBLIC int INTsystem_rename(char *oldpath, char *newpath);
NSAPI_PUBLIC int INTsystem_unlink(char *path);
NSAPI_PUBLIC int INTsystem_tlock(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_flock(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_ulock(SYS_FILE fd);


/* --- OBSOLETE ----------------------------------------------------------
 * The following macros/functions are obsolete and are only maintained for
 * compatibility.  Do not use them. 11-19-96
 * -----------------------------------------------------------------------
 */

NSAPI_PUBLIC int INTsystem_nocoredumps(void);
NSAPI_PUBLIC int INTfile_setinherit(SYS_FILE fd, int value);
NSAPI_PUBLIC int INTfile_notfound(void);
NSAPI_PUBLIC const char *INTsystem_errmsg(void);
NSAPI_PUBLIC int INTsystem_errmsg_fn(char **buff, size_t maxlen);

NSPR_END_EXTERN_C

#define system_errmsg_init INTsystem_errmsg_init
#define system_fopenRO INTsystem_fopenRO
#define system_fopenWA INTsystem_fopenWA
#define system_fopenRW INTsystem_fopenRW
#define system_fopenWT INTsystem_fopenWT
#define system_fread INTsystem_fread
#define system_fwrite INTsystem_fwrite
#define system_fwrite_atomic INTsystem_fwrite_atomic
#define system_lseek INTsystem_lseek
#define system_fclose INTsystem_fclose
#define system_stat INTsystem_stat
#define system_rename INTsystem_rename
#define system_unlink INTsystem_unlink
#define system_tlock INTsystem_tlock
#define system_flock INTsystem_flock
#define system_ulock INTsystem_ulock

#define system_nocoredumps INTsystem_nocoredumps
#define file_setinherit INTfile_setinherit
#define file_notfound INTfile_notfound
#define rtfile_notfound INTfile_notfound
#define system_errmsg INTsystem_errmsg
#define system_errmsg_fn INTsystem_errmsg_fn

#endif /* INTNSAPI */

#endif /* BASE_FILE_H */
