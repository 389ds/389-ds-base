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

#include <base/buffer.h>
#include <base/file.h>
#include <base/daemon.h>
#include <base/eventlog.h>
#include <base/util.h>
#include <base/shexp.h>
#include <base/session.h>
#include <base/sem.h>
#include <base/pblock.h>
#include <base/net.h>
#include <base/ereport.h>
#include <base/cinfo.h>
#include <base/systhr.h>
#include <base/shmem.h>
#include <base/crit.h>
#include <base/systhr.h>

#include <frame/objset.h>
#include <frame/conf.h>
#include <frame/func.h>
#include <frame/http.h>
#include <frame/log.h>
#include <frame/object.h>
#include <frame/protocol.h>
#include <frame/req.h>
#include <frame/httpact.h>
#include <frame/servact.h>

#include <libmsgdisp/orb.h>
#include <libmsgdisp/nsarray.h>
#include <libmsgdisp/msgchnel.h>
#include <libmsgdisp/mdbtree.h>
#include <libmsgdisp/mdutil.h>

#include <ssl.h>
#include <nt/magnus.h>

typedef void * (SafFunction)();
SafFunction **SafTable;
__declspec(dllexport) int InitSafTable(SafFunction *Table);

/* Functions from ntbuffer.c */
	
#define FILEBUF_OPEN 1 
#define NETBUF_OPEN 2 
#define FILEBUF_OPEN_NOSTAT 3 

#define PIPEBUF_OPEN 4 
#define PIPEBUF_CLOSE 5 
#define FILEBUF_NEXT 6 
#define NETBUF_NEXT 7 

#define PIPEBUF_NEXT 8 
#define FILEBUF_CLOSE 9 
#define NETBUF_CLOSE 10 
#define FILEBUF_GRAB 11 
#define NETBUF_GRAB 12 
#define PIPEBUF_GRAB 13 
#define NETBUF_BUF2SD 14 
#define FILEBUF_BUF2SD 15 

#define PIPEBUF_BUF2SD 16 
#define PIPEBUF_NETBUF2SD 17 

/* Functions from daemon.h */

#define NTDAEMON_RUN 18 
#define CHILD_STATUS 19 

/* Functions from file.h */
#define SYSTEM_FREAD 20
#define SYSTEM_PREAD 21
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
#define SYSTEM_INITLOCK 33 

#define FILE_UNIX2LOCAL 34 
#define DIR_OPEN 35
#define DIR_READ 36 
#define DIR_CLOSE 37 

/* Functions from sem.h */
#define SEM_INIT 40 
#define SEM_TERMINATE 41
#define SEM_GRAB 42
#define SEM_TGRAB 43 
#define SEM_RELEASE 44

/* Functions from session.h */
#define SESSION_CREATE 45 
#define SESSION_FREE 46 
#define SESSION_DNS_LOOKUP 47 

/* Functions from cinfo.h */
#define CINFO_INIT 70
#define CINFO_TERMINATE 71
#define CINFO_MERGE 72
#define CINFO_FIND 73
#define CINFO_LOOKUP 74 
#define CINFO_DUMP_DATABASE 75 

/* Functions from ereport.h */
#define EREPORT 80
#define EREPORT_INIT 81
#define EREPORT_TERMINATE 82
#define EREPORT_GETFD 83

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

/* Functions from net.h */
#define NET_BIND 110 
#define NET_READ 111 
#define NET_WRITE 112

#define NET_FIND_FQDN 113
#define NET_IP2HOST 114
#define NET_SENDMAIL 115

/* Functions from pblock.h */
#define PARAM_CREATE 120
#define PARAM_FREE 121
#define PBLOCK_CREATE 122 
#define PBLOCK_FREE 123 
#define PBLOCK_FINDVAL 124 
#define PBLOCK_NVINSERT 125 
#define PBLOCK_NNINSERT 126 
#define PBLOCK_PINSERT 127 
#define PBLOCK_STR2PBLOCK 128 
#define PBLOCK_PBLOCK2STR 129 
#define PBLOCK_COPY 130
#define PBLOCK_PB2ENV 131
#define PBLOCK_FR 132

/* Functions from systhr.h */
#define	SYSTHREAD_START 133
#define SYSTHREAD_ATTACH 134
#define SYSTHREAD_TERMINATE 135
#define SYSTHREAD_SLEEP 136
#define SYSTHREAD_INIT 137
#define SYSTHREAD_NEWKEY 138
#define SYSTHREAD_GETDATA 139
#define SYSTHREAD_SETDATA 140

/* Functions from shmem.h */
#define	SHMEM_ALLOC 141
#define SHMEM_FREE 142

/* Functions from eventlog.h */
#define INITIALIZE_ADMIN_LOGGING 143
#define INITIALIZE_HTTPD_LOGGING 144
#define INITIALIZE_HTTPS_LOGGING 145

#define TERMINATE_ADMIN_LOGGING 146
#define TERMINATE_HTTPD_LOGGING 147
#define TERMINATE_HTTPS_LOGGING 148

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
#define UTIL_GETLINE 180 
#define UTIL_ENV_CREATE 181 
#define UTIL_ENV_STR 182 
#define NTUTIL_ENV_STR 183 
#define UTIL_ENV_REPLACE 184 
#define UTIL_ENV_FREE 185
#define UTIL_ENV_FIND 186
#define UTIL_HOSTNAME 187
#define UTIL_CHDIR2PATH	188
#define UTIL_IS_MOZILLA 189
#define UTIL_IS_URL 190
#define UTIL_LATER_THAN 191
#define UTIL_URI_IS_EVIL 192
#define UTIL_URI_PARSE 193
#define UTIL_URI_UNESCAPE 194
#define UTIL_URI_ESCAPE 195
#define UTIL_URL_ESCAPE 196 
#define UTIL_SH_ESCAPE 197
#define UTIL_ITOA 198
#define UTIL_VSPRINTF 199
#define UTIL_SPRINTF 200
#define UTIL_VSNPRINTF 201
#define UTIL_SNPRINTF 202

/* Functions from magnus.h */
#define MAGNUS_ATRESTART 203

/* Functions from conf.h */
#define CONF_INIT 207
#define CONF_TERMINATE 208
#define CONF_GETGLOBALS 209
#define CONF_VARS2DAEMON 210

/* Functions from req.h */
#define REQUEST_CREATE 211
#define REQUEST_FREE 212 
#define REQUEST_RESTART_INTERNAL 213 
#define REQUEST_TRANSLATE_URI 214 
#define REQUEST_HEADER 215 
#define REQUEST_STAT_PATH 216
#define REQUEST_URI2PATH 217 
#define REQUEST_PATHCHECKS 218 
#define REQUEST_FILEINFO 219
#define REQUEST_HANDLE_PROCESSED 220
#define REQUEST_SERVICE 221
#define REQUEST_HANDLE 222

/* Functions from object.h */
#define DIRECTIVE_NAME2NUM 223
#define DIRECTIVE_NUM2NAME 224
#define OBJECT_CREATE 225 
#define OBJECT_FREE 226 
#define OBJECT_ADD_DIRECTIVE 227 
#define OBJECT_EXECUTE 228 

/* Functions from objset.h */
#define OBJSET_SCAN_BUFFER 230
#define OBJSET_CREATE 231 
#define OBJSET_FREE 232 
#define OBJSET_FREE_SETONLY 233 
#define OBJSET_NEW_OBJECT 234 
#define OBJSET_ADD_OBJECT 235 
#define OBJSET_FINDBYNAME 236 
#define OBJSET_FINDBYPPATH 237

/* Functions from http.h */
#define HTTP_PARSE_REQUEST 240
#define HTTP_SCAN_HEADERS 241
#define HTTP_START_RESPONSE 242
#define HTTP_HDRS2_ENV 243
#define HTTP_STATUS 244 
#define HTTP_SET_FINFO 245 
#define HTTP_DUMP822 246 
#define HTTP_FINISH_REQUEST 247 
#define HTTP_HANDLE_SESSION 248
#define HTTP_URI2URL 249 

/* Functions from func.h */
#define FUNC_INIT 251 
#define FUNC_FIND 252 
#define FUNC_EXEC 253 
#define FUNC_INSERT 254 

/* Functions from log.h */
#define LOG_ERROR 260 

/* robm Functions added in 2.0 */
#define SYSTEM_FOPENWT 261
#define SYSTEM_MALLOC 262
#define SYSTEM_FREE 263
#define SYSTEM_REALLOC 264
#define SYSTEM_STRDUP 265

#define UPLOAD_FILE 266

#define CRIT_INIT 267
#define CRIT_ENTER 268
#define CRIT_EXIT 269
#define CRIT_TERMINATE 270
#define SYSTHREAD_CURRENT 271

#define NET_ACCEPT 272
#define NET_CLOSE 273
#define NET_CONNECT 274
#define NET_IOCTL 275
#define NET_LISTEN 276
#define NET_SETSOCKOPT 277
#define NET_SOCKET 278

/* Daryoush Functions added in 3.0 */
#define NSORB_INIT 279
#define NSORB_INST_ID 280
#define NSORB_GET_INST 281
#define NSORB_REG_INT	282
#define NSORB_FIND_OBJ	283
#define NSORB_GET_INTERFACE 284

#define ARR_NEW	285
#define ARR_FREE 286
#define ARR_GET_OBJ	288
#define ARR_GET_LAST_OBJ 289
#define ARR_NEW_OBJ 290
#define ARR_GET_NUM_OBJ 291
#define ARR_RESET 292
#define ARR_REMOVEOBJ 293
#define ARR_GET_OBJ_NUM 294

#define CM_BT_NEW 295
#define CM_BT_ADD_NODE 296
#define CM_BT_FIND_NODE 297
#define CM_BT_DEL_NODE 298
#define CM_BT_DESTROY 299
#define CM_BT_GET_NUM 300
#define CM_BT_TRAVEL 301

#define CM_STR_NEW 302
#define CM_STR_ADD 303
#define CM_STR_REL 304
#define CM_STR_FREE 305
#define CM_STR_GET 306
#define CM_STR_SIZE 307
#define CM_COPY_STR 308
#define CM_MAKE_UID 309

#define MS_NEW 310
#define MS_CREATE 311
