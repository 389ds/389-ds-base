/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
