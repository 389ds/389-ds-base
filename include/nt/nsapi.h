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
 * Aruna Victor
 */

#include <windows.h>
#include <stdio.h>

#include <base/file.h>
#include <base/eventlog.h>
#include <base/util.h>
#include <base/shexp.h>
#include <base/systhr.h>
#include <base/crit.h>
#include <base/systhr.h>

#include <ssl.h>
typedef void * (SafFunction)();
SafFunction **SafTable;
__declspec(dllexport) int InitSafTable(SafFunction *Table);

/* Functions from file.h */
#define SYSTEM_FOPENRO 22
#define SYSTEM_FOPENWA 23
#define SYSTEM_FOPENRW 24
#define SYSTEM_FCLOSE 25 
#define SYSTEM_NOCOREDUMPS 26
#define SYSTEM_FWRITE 27
#define SYSTEM_FWRITE_ATOMIC 28
#define SYSTEM_WINERR 29
#define SYSTEM_WINSOCKERR 30
#define FILE_NOTFOUND 31
#define SYSTEM_STAT 32

#define FILE_UNIX2LOCAL 34 
#define DIR_OPEN 35
#define DIR_READ 36 
#define DIR_CLOSE 37 

/* Functions from ereport.h */
#define EREPORT 80

/* Functions from minissl.h */
#define SSL_CLOSE 90
#define SSL_SOCKET 91 
#define SSL_GET_SOCKOPT 92 
#define SSL_SET_SOCKOPT 93 
#define SSL_BIND 94 
#define SSL_LISTEN 95 
#define SSL_ACCEPT 96
#define SSL_READ 97
#define SSL_WRITE 98
#define SSL_GETPEERNAME 99

/* Functions from systhr.h */
#define	SYSTHREAD_START 133
#define SYSTHREAD_ATTACH 134
#define SYSTHREAD_TERMINATE 135
#define SYSTHREAD_SLEEP 136
#define SYSTHREAD_INIT 137
#define SYSTHREAD_NEWKEY 138
#define SYSTHREAD_GETDATA 139
#define SYSTHREAD_SETDATA 140

#define LOG_ERROR_EVENT 149

/* Functions from shexp.h */
#define SHEXP_VALID 160 
#define SHEXP_MATCH 161
#define SHEXP_CMP 162 
#define SHEXP_CASECMP 163

/* Functions from systems.h */
#define UTIL_STRCASECMP 170
#define UTIL_STRNCASECMP 171

/* Functions from util.h */
#define UTIL_HOSTNAME 187
#define UTIL_ITOA 198
#define UTIL_VSPRINTF 199
#define UTIL_SPRINTF 200
#define UTIL_VSNPRINTF 201
#define UTIL_SNPRINTF 202

/* Functions from conf.h */
#define CONF_INIT 207

/* robm Functions added in 2.0 */
#define SYSTEM_FOPENWT 261
#define SYSTEM_MALLOC 262
#define SYSTEM_FREE 263
#define SYSTEM_REALLOC 264
#define SYSTEM_STRDUP 265

#define CRIT_INIT 267
#define CRIT_ENTER 268
#define CRIT_EXIT 269
#define CRIT_TERMINATE 270
#define SYSTHREAD_CURRENT 271

#define ACL_LISTCONCAT 312
#define GETCLIENTLANG 313
