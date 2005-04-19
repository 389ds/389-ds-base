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
NSAPI_PUBLIC int INTsystem_fwrite(SYS_FILE fd,char *buf,int sz);
NSAPI_PUBLIC int INTsystem_fwrite_atomic(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC int INTsystem_lseek(SYS_FILE fd, int off, int wh);
NSAPI_PUBLIC int INTsystem_fclose(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_stat(char *name, struct stat *finfo);
NSAPI_PUBLIC int INTsystem_rename(char *oldpath, char *newpath);
NSAPI_PUBLIC int INTsystem_unlink(char *path);
NSAPI_PUBLIC int INTsystem_tlock(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_flock(SYS_FILE fd);
NSAPI_PUBLIC int INTsystem_ulock(SYS_FILE fd);

#ifdef XP_WIN32
NSAPI_PUBLIC SYS_DIR INTdir_open(char *path);
NSAPI_PUBLIC SYS_DIRENT *INTdir_read(SYS_DIR ds);
NSAPI_PUBLIC void INTdir_close(SYS_DIR ds);
#endif /* XP_WIN32 */

NSAPI_PUBLIC int INTdir_create_all(char *dir);

/* --- OBSOLETE ----------------------------------------------------------
 * The following macros/functions are obsolete and are only maintained for
 * compatibility.  Do not use them. 11-19-96
 * -----------------------------------------------------------------------
 */

#ifdef XP_WIN32
NSAPI_PUBLIC char *INTsystem_winsockerr(void);
NSAPI_PUBLIC char *INTsystem_winerr(void);
NSAPI_PUBLIC int INTsystem_pread(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC int INTsystem_pwrite(SYS_FILE fd, char *buf, int sz);
NSAPI_PUBLIC void INTfile_unix2local(char *path, char *p2);
#endif /* XP_WIN32 */

NSAPI_PUBLIC int INTsystem_nocoredumps(void);
NSAPI_PUBLIC int INTfile_setinherit(SYS_FILE fd, int value);
NSAPI_PUBLIC int INTfile_notfound(void);
NSAPI_PUBLIC char *INTsystem_errmsg(void);
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
#ifdef XP_WIN32
#define dir_open INTdir_open
#define dir_read INTdir_read
#define dir_close INTdir_close
#endif /* XP_WIN32 */
#define dir_create_all INTdir_create_all

/* Obsolete */
#ifdef XP_WIN32
#define system_winsockerr INTsystem_winsockerr
#define system_winerr INTsystem_winerr
#define system_pread INTsystem_pread
#define system_pwrite INTsystem_pwrite
#define file_unix2local INTfile_unix2local
#endif /* XP_WIN32 */

#define system_nocoredumps INTsystem_nocoredumps
#define file_setinherit INTfile_setinherit
#define file_notfound INTfile_notfound
#define rtfile_notfound INTfile_notfound
#define system_errmsg INTsystem_errmsg
#define system_errmsg_fn INTsystem_errmsg_fn

#endif /* INTNSAPI */

#endif /* BASE_FILE_H */
