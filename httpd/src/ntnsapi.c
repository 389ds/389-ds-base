/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Aruna Victor	
 * NT NSAPI works differently from UNIX. The DLL doesn't know the addresses
 * of the functions in the server process and needs to be told them.
 */

#include <nt/nsapi.h>
extern int upload_file(pblock *pb, Session *sn, Request *rq);

#ifdef BUILD_DLL

#include <libadmin/libadmin.h>
#include <libadmin/dstats.h>
#include <libaccess/nsadb.h>
#include <libadminutil/admutil.h>
#include <libadminutil/distadm.h>
#if 0
#ifndef NO_MOCHA
#include <mocha/mo_atom.h>
#include <mocha/mo_scope.h>
#include <mocha/mochaapi.h>
#endif /* NO_MOCHA */
#endif
#include <base/fsmutex.h>

extern char *system_winerr(void);                                               
extern char *system_winsockerr(void);                                           

VOID NsapiDummy()
{	
	int i = 0;
	SafTable = (SafFunction **)MALLOC(400 * sizeof(VOID*));

    /* Force references to libadmin */

	/* Functions from libadmin:keyconf.c */
	SafTable[i++] = (SafFunction *)get_alias_dir;
	/* Functions from libadmin:objconf.c */
	SafTable[i++] = (SafFunction *)get_mag_init;
	/* Functions from libadmin:form_post.c */
	SafTable[i++] = (SafFunction *)post_begin;
	/* Functions from libadmin:form_get.c */
	SafTable[i++] = (SafFunction *)open_html_file;
	/* Functions from libadmin:userdb.c */
#if 0
	SafTable[i++] = (SafFunction *)detect_db_type;
#endif
	/* Functions from libadmin:authdb.c */
	SafTable[i++] = (SafFunction *)list_auth_dbs;
	/* Functions from libadmin:error.c */
	SafTable[i++] = (SafFunction *)report_error;
	/* Functions from libadmin:template.c */
	SafTable[i++] = (SafFunction *)directive_is;
	/* Functions from libaccess:pcontrol.c */
	SafTable[i++] = (SafFunction *)restart_http;
	/* Functions from libadmin:admserv.c */
	SafTable[i++] = (SafFunction *)read_server_lst;
	/* Functions from libadmin:multconf.c */
	SafTable[i++] = (SafFunction *)make_conflist;
	/* Functions from libadmin:password */
	SafTable[i++] = (SafFunction *)pw_enc;
	/* Functions from libadmin:cron_conf.c */
#if 0
	SafTable[i++] = (SafFunction *)cron_conf_create_obj;
#endif
	/* Functions from libadmin:dstats.c */
	SafTable[i++] = (SafFunction *)dstats_open;
	/* Functions from libadmin:distadm.c */
	SafTable[i++] = (SafFunction *)ADM_InitializePermissions;

#ifndef NO_MOCHA
    /* Force references to mocha */
	/* Functions from mocha:mo_atom.c */
	SafTable[i++] = (SafFunction *)mocha_Atomize;
	/* Functions from mocha:mo_fun.c */
	/* SafTable[i++] = (SafFunction *)mocha_NewFunctionObject; */
	/* Functions from mocha:mo_scope.c */
	SafTable[i++] = (SafFunction *)mocha_DefineSymbol;
	/* Functions from mocha:mochaapi.c */
	SafTable[i++] = (SafFunction *)MOCHA_CanConvertDatum;
#endif /* NO_MOCHA */
	
    /* Force references to base */
    SafTable[i++] = (SafFunction *)fsmutex_init;

	/* Force references to libmsgdisp */
	SafTable[i++] = (SafFunction *)NSORB_Init;
	SafTable[i++] = (SafFunction *)ConsumerCreatePush;
	SafTable[i++] = (SafFunction *)CMNewBTree;
	SafTable[i++] = (SafFunction *)NSObjArrayNew;
}
#endif /* BUILD_DLL */

VOID InitializeSafFunctions() 
{

	SafTable = (SafFunction **)MALLOC(400 * sizeof(VOID *));

/* Functions from buffer.c */
	
	SafTable[FILEBUF_OPEN] = (SafFunction *)filebuf_open;
	SafTable[NETBUF_OPEN] = (SafFunction *)netbuf_open;
	SafTable[FILEBUF_OPEN_NOSTAT] = (SafFunction *)filebuf_open_nostat;
	SafTable[PIPEBUF_OPEN] = (SafFunction *)pipebuf_open;
	SafTable[PIPEBUF_CLOSE] = (SafFunction *)pipebuf_close;
	SafTable[FILEBUF_OPEN_NOSTAT] = (SafFunction *)filebuf_open_nostat;
	SafTable[NETBUF_NEXT] = (SafFunction *)netbuf_next;
	SafTable[PIPEBUF_NEXT] = (SafFunction *)pipebuf_next;
	SafTable[FILEBUF_CLOSE] = (SafFunction *)filebuf_close;
	SafTable[NETBUF_CLOSE] = (SafFunction *)netbuf_close;
	SafTable[FILEBUF_GRAB] = (SafFunction *)filebuf_grab;
	SafTable[NETBUF_GRAB] = (SafFunction *)netbuf_grab;
	SafTable[PIPEBUF_GRAB] = (SafFunction *)pipebuf_grab;
	SafTable[NETBUF_BUF2SD] = (SafFunction *)netbuf_buf2sd;
	SafTable[FILEBUF_BUF2SD] = (SafFunction *)filebuf_buf2sd;
	SafTable[PIPEBUF_BUF2SD] = (SafFunction *)pipebuf_buf2sd;
	SafTable[PIPEBUF_NETBUF2SD] = (SafFunction *)pipebuf_netbuf2sd;

/* Functions from daemon.h */
	SafTable[NTDAEMON_RUN] = (SafFunction *)daemon_run;
	SafTable[CHILD_STATUS] = (SafFunction *)child_status;

/* Functions from file.h */
	SafTable[SYSTEM_STAT] = (SafFunction *)system_stat;
	SafTable[SYSTEM_FOPENRO] = (SafFunction *)system_fopenRO;
	SafTable[SYSTEM_FOPENWA] = (SafFunction *)system_fopenWA;
	SafTable[SYSTEM_FOPENRW] = (SafFunction *)system_fopenRW;
	SafTable[SYSTEM_NOCOREDUMPS] = (SafFunction *)system_nocoredumps;
	SafTable[SYSTEM_FWRITE] = (SafFunction *)system_fwrite;
	SafTable[SYSTEM_FWRITE_ATOMIC] = (SafFunction *)system_fwrite_atomic;
	SafTable[SYSTEM_WINERR] = (SafFunction *)system_winerr;
	SafTable[SYSTEM_WINSOCKERR] = (SafFunction *)system_winsockerr;

	SafTable[FILE_NOTFOUND] = (SafFunction *)file_notfound;
    /* Removed from main code- 9-2-96
     *
	SafTable[SYSTEM_INITLOCK] = (SafFunction *)system_initlock;*/
	SafTable[FILE_UNIX2LOCAL] = (SafFunction *)file_unix2local;
	SafTable[DIR_OPEN] = (SafFunction *)dir_open;
	SafTable[DIR_READ] = (SafFunction *)dir_read;
	SafTable[DIR_CLOSE] = (SafFunction *)dir_close;

/* Functions from cinfo.h */
	SafTable[CINFO_INIT] = (SafFunction *)cinfo_init;
	SafTable[CINFO_TERMINATE] = (SafFunction *)cinfo_terminate;
	SafTable[CINFO_MERGE] = (SafFunction *)cinfo_merge;
	SafTable[CINFO_FIND] = (SafFunction *)cinfo_find;
	SafTable[CINFO_LOOKUP] = (SafFunction *)cinfo_lookup;
	SafTable[CINFO_DUMP_DATABASE] = (SafFunction *)cinfo_dump_database;

/* Functions from ereport.h */
	SafTable[EREPORT] = (SafFunction *)ereport ;
	SafTable[EREPORT_INIT] = (SafFunction *)ereport_init;
	SafTable[EREPORT_TERMINATE] = (SafFunction *)ereport_terminate;
	SafTable[EREPORT_GETFD] = (SafFunction *)ereport_getfd;

#ifdef NET_SSL
/* Functions from minissl.h */
	SafTable[SSL_CLOSE] = (SafFunction *)PR_Close;
	SafTable[SSL_SOCKET] = (SafFunction *)PR_NewTCPSocket;
	SafTable[SSL_GET_SOCKOPT] = (SafFunction *)PR_GetSocketOption;
	SafTable[SSL_SET_SOCKOPT] = (SafFunction *)PR_SetSocketOption;
	SafTable[SSL_BIND] = (SafFunction *)PR_Bind;
	SafTable[SSL_LISTEN] = (SafFunction *)PR_Listen;
	SafTable[SSL_ACCEPT] = (SafFunction *)PR_Accept;
	SafTable[SSL_READ] = (SafFunction *)PR_Read;
	SafTable[SSL_WRITE] = (SafFunction *)PR_Write;
	SafTable[SSL_GETPEERNAME] = (SafFunction *)PR_GetPeerName;
#endif /* NET_SSL */


/* Functions from net.h */

	SafTable[NET_BIND] = (SafFunction *)net_bind;
	SafTable[NET_READ] = (SafFunction *)net_read;
	SafTable[NET_WRITE] = (SafFunction *)net_write;
	/* SafTable[NET_FIND_FQDN] = (SafFunction *)net_find_fqdn; */
	SafTable[NET_IP2HOST] = (SafFunction *)net_ip2host;

/* Functions from pblock.h */
	SafTable[PARAM_CREATE] = (SafFunction *)param_create;
	SafTable[PARAM_FREE] = (SafFunction *)param_free;
	SafTable[PBLOCK_CREATE] = (SafFunction *)pblock_create;
	SafTable[PBLOCK_FREE] = (SafFunction *)pblock_free;
	SafTable[PBLOCK_FINDVAL] = (SafFunction *)pblock_findval;
	SafTable[PBLOCK_NVINSERT] = (SafFunction *)pblock_nvinsert;
	SafTable[PBLOCK_NNINSERT] = (SafFunction *)pblock_nninsert;
	SafTable[PBLOCK_PINSERT] = (SafFunction *)pblock_pinsert;
	SafTable[PBLOCK_STR2PBLOCK] = (SafFunction *)pblock_str2pblock;
	SafTable[PBLOCK_PBLOCK2STR] = (SafFunction *)pblock_pblock2str;
	SafTable[PBLOCK_COPY] = (SafFunction *)pblock_copy;
	SafTable[PBLOCK_PB2ENV] = (SafFunction *)pblock_pb2env;
	SafTable[PBLOCK_FR] = (SafFunction *)pblock_fr;

/* Functions from sem.h */
	SafTable[SEM_INIT] = (SafFunction *)sem_init;
	SafTable[SEM_TERMINATE] = (SafFunction *)sem_terminate;
	SafTable[SEM_GRAB] = (SafFunction *)sem_grab;
	SafTable[SEM_TGRAB] = (SafFunction *)sem_grab;
	SafTable[SEM_RELEASE] = (SafFunction *)sem_release;

/* Functions from session.h */
	SafTable[SESSION_CREATE] = (SafFunction *)session_create;
	SafTable[SESSION_FREE] = (SafFunction *)session_free;
	SafTable[SESSION_DNS_LOOKUP] = (SafFunction *)session_dns_lookup;

/* Functions from shexp.h */
	SafTable[SHEXP_VALID] = (SafFunction *)shexp_valid;
	SafTable[SHEXP_MATCH] = (SafFunction *)shexp_match;
	SafTable[SHEXP_CMP] = (SafFunction *)shexp_cmp;
	SafTable[SHEXP_CASECMP] = (SafFunction *)shexp_casecmp;

/* Functions from systhr.h */
	SafTable[SYSTHREAD_START] = (SafFunction *)systhread_start;
	SafTable[SYSTHREAD_ATTACH] = (SafFunction *)systhread_attach;
	SafTable[SYSTHREAD_TERMINATE] = (SafFunction *)systhread_terminate;
	SafTable[SYSTHREAD_SLEEP] = (SafFunction *)systhread_sleep;
	SafTable[SYSTHREAD_INIT] = (SafFunction *)systhread_init;
	SafTable[SYSTHREAD_NEWKEY] = (SafFunction *)systhread_newkey;
	SafTable[SYSTHREAD_GETDATA] = (SafFunction *)systhread_getdata;
	SafTable[SYSTHREAD_SETDATA] = (SafFunction *)systhread_setdata;

/* Functions from shmem.h */
	SafTable[SHMEM_ALLOC] = (SafFunction *)shmem_alloc;
	SafTable[SHMEM_FREE] = (SafFunction *)shmem_free;

/* Functions from systems.h */
	SafTable[UTIL_STRCASECMP] = (SafFunction *)util_strcasecmp;
	SafTable[UTIL_STRNCASECMP] = (SafFunction *)util_strncasecmp;

/* Functions from util.h */
	SafTable[UTIL_GETLINE] = (SafFunction *)util_getline;
	SafTable[UTIL_ENV_CREATE] = (SafFunction *)util_env_create;
	SafTable[UTIL_ENV_STR] = (SafFunction *)util_env_str;
#if 0
	/* Removed from ntcgi.c --- obsolete; MB 1-26-96 */
	SafTable[NTUTIL_ENV_STR] = (SafFunction *)ntutil_env_str;
#endif
	SafTable[UTIL_ENV_REPLACE] = (SafFunction *)util_env_replace;
	SafTable[UTIL_ENV_FREE] = (SafFunction *)util_env_free;
	SafTable[UTIL_ENV_FIND] = (SafFunction *)util_env_find;
	SafTable[UTIL_HOSTNAME] = (SafFunction *)util_hostname;
	SafTable[UTIL_CHDIR2PATH] = (SafFunction *)util_chdir2path;
	SafTable[UTIL_IS_MOZILLA] = (SafFunction *)util_is_mozilla;
	SafTable[UTIL_IS_URL] = (SafFunction *)util_is_url;
	SafTable[UTIL_LATER_THAN] = (SafFunction *)util_later_than;
	SafTable[UTIL_URI_IS_EVIL] = (SafFunction *)util_uri_is_evil;
	SafTable[UTIL_URI_PARSE] = (SafFunction *)util_uri_parse;
	SafTable[UTIL_URI_UNESCAPE] = (SafFunction *)util_uri_unescape;
	SafTable[UTIL_URI_ESCAPE] = (SafFunction *)util_uri_escape;
	SafTable[UTIL_URL_ESCAPE] = (SafFunction *)util_url_escape;
	SafTable[UTIL_SH_ESCAPE] = (SafFunction *)util_sh_escape;
	SafTable[UTIL_ITOA] = (SafFunction *)util_itoa;
	SafTable[UTIL_VSPRINTF] = (SafFunction *)util_vsprintf;
	SafTable[UTIL_SPRINTF] = (SafFunction *)util_sprintf;
	SafTable[UTIL_VSNPRINTF] = (SafFunction *)util_vsnprintf;
	SafTable[UTIL_SNPRINTF] = (SafFunction *)util_snprintf;

//	SafTable[INITIALIZE_ADMIN_LOGGING] = (SafFunction *)InitializeAdminLogging;
//	SafTable[INITIALIZE_HTTPD_LOGGING] = (SafFunction *)InitializeHttpdLogging;
//	SafTable[TERMINATE_ADMIN_LOGGING] = (SafFunction *)TerminateAdminLogging;
//	SafTable[TERMINATE_HTTPD_LOGGING] = (SafFunction *)TerminateHttpdLogging;

	SafTable[LOG_ERROR_EVENT] = (SafFunction *)LogErrorEvent;

/* Functions from conf.h */
	SafTable[CONF_INIT] = (SafFunction *)conf_init;
	SafTable[CONF_TERMINATE] = (SafFunction *)conf_terminate;
	SafTable[CONF_GETGLOBALS] = (SafFunction *)conf_getglobals;
	SafTable[CONF_VARS2DAEMON] = (SafFunction *)conf_vars2daemon;

	SafTable[FUNC_INIT]	= (SafFunction *)func_init;
	SafTable[FUNC_FIND]	= (SafFunction *)func_find;
	SafTable[FUNC_EXEC]	= (SafFunction *)func_exec;
	SafTable[FUNC_INSERT] = (SafFunction *)func_insert;
	SafTable[HTTP_SCAN_HEADERS]	= (SafFunction *)http_scan_headers;
	SafTable[HTTP_START_RESPONSE] = (SafFunction *)http_start_response;
	SafTable[HTTP_HDRS2_ENV] = (SafFunction *)http_hdrs2env;
	SafTable[HTTP_STATUS] = (SafFunction *)http_status;
	SafTable[HTTP_SET_FINFO] = (SafFunction *)http_set_finfo;
	SafTable[HTTP_DUMP822] = (SafFunction *)http_dump822;
	SafTable[HTTP_FINISH_REQUEST] = (SafFunction *)http_finish_request;
	SafTable[HTTP_HANDLE_SESSION] = (SafFunction *)http_handle_session;
	SafTable[HTTP_URI2URL] = (SafFunction *)http_uri2url;

/* Functions from log.h */
	SafTable[LOG_ERROR] = (SafFunction *)log_error;
	SafTable[DIRECTIVE_NAME2NUM] = (SafFunction *)directive_name2num;
	SafTable[DIRECTIVE_NUM2NAME] = (SafFunction *)directive_num2name;
	SafTable[OBJECT_CREATE] = (SafFunction *)object_create;
	SafTable[OBJECT_FREE] = (SafFunction *)object_free;
	SafTable[OBJECT_ADD_DIRECTIVE] = (SafFunction *)object_add_directive;
	SafTable[OBJECT_EXECUTE] = (SafFunction *)object_execute;
	SafTable[OBJSET_SCAN_BUFFER] = (SafFunction *)objset_scan_buffer;
	SafTable[OBJSET_FREE] = (SafFunction *)objset_free;
	SafTable[OBJSET_FREE_SETONLY] = (SafFunction *)objset_free_setonly;
	SafTable[OBJSET_NEW_OBJECT] = (SafFunction *)objset_new_object;
	SafTable[OBJSET_ADD_OBJECT] = (SafFunction *)objset_add_object;
	SafTable[OBJSET_FINDBYNAME] = (SafFunction *)objset_findbyname;
	SafTable[OBJSET_FINDBYPPATH] = (SafFunction *)objset_findbyppath;

	SafTable[REQUEST_CREATE] = (SafFunction *)request_create;
	SafTable[REQUEST_FREE] = (SafFunction *)request_free;
	SafTable[REQUEST_RESTART_INTERNAL] = (SafFunction *)request_restart_internal;
	SafTable[REQUEST_TRANSLATE_URI] = (SafFunction *)request_translate_uri;
	SafTable[REQUEST_HEADER] = (SafFunction *)request_header;
	SafTable[REQUEST_STAT_PATH] = (SafFunction *)request_stat_path;

	/* temporarily remove definitions till they are added into the header file. */
	SafTable[REQUEST_HANDLE_PROCESSED] = (SafFunction *)request_handle_processed;
	
	SafTable[MAGNUS_ATRESTART] = (SafFunction *)magnus_atrestart;

        SafTable[SYSTEM_FOPENWT] = (SafFunction *)system_fopenWT;
        SafTable[SYSTEM_MALLOC] = (SafFunction *)system_malloc;
        SafTable[SYSTEM_FREE] = (SafFunction *)system_free;
        SafTable[SYSTEM_REALLOC] = (SafFunction *)system_realloc;
        SafTable[SYSTEM_STRDUP] = (SafFunction *)system_strdup;

    SafTable[UPLOAD_FILE] = (SafFunction *)upload_file;

        SafTable[CRIT_INIT] = (SafFunction *)crit_init;
        SafTable[CRIT_ENTER] = (SafFunction *)crit_enter;
        SafTable[CRIT_EXIT] = (SafFunction *)crit_exit;
        SafTable[CRIT_TERMINATE] = (SafFunction *)crit_terminate;
        SafTable[SYSTHREAD_CURRENT] = (SafFunction *)systhread_current;

        SafTable[NET_ACCEPT] = (SafFunction *)net_accept;
        SafTable[NET_CLOSE] = (SafFunction *)net_close;
        SafTable[NET_CONNECT] = (SafFunction *)net_connect;
        SafTable[NET_IOCTL] = (SafFunction *)net_ioctl;
        SafTable[NET_LISTEN] = (SafFunction *)net_listen;
        SafTable[NET_SETSOCKOPT] = (SafFunction *)net_setsockopt;
        SafTable[NET_SOCKET] = (SafFunction *)net_socket;


	/* msgdisp functions */
   SafTable[NSORB_INIT] = (SafFunction *)NSORB_Init;
   SafTable[NSORB_INST_ID] = (SafFunction *)NSORB_InstanceID;
   SafTable[NSORB_GET_INST] = (SafFunction *)NSORB_GetInstance;
   SafTable[NSORB_REG_INT] = (SafFunction *)NSORB_RegisterInterface;
   SafTable[NSORB_FIND_OBJ] = (SafFunction *)NSORB_FindObject;
   SafTable[NSORB_GET_INTERFACE] = (SafFunction *)NSORB_GetInterface;

   SafTable[ARR_NEW] = (SafFunction *)NSObjArrayNew;
   SafTable[ARR_FREE] = (SafFunction *)NSObjArrayFree;
   SafTable[ARR_GET_OBJ] = (SafFunction *)NSObjArrayGetObj;
   SafTable[ARR_GET_LAST_OBJ] = (SafFunction *)NSObjArrayGetLastObj;
   SafTable[ARR_NEW_OBJ] = (SafFunction *)NSObjArrayNewObj;
   SafTable[ARR_GET_NUM_OBJ] = (SafFunction *)NSObjArrayGetNumObj;
   SafTable[ARR_RESET] = (SafFunction *)NSObjArrayReset;
   SafTable[ARR_REMOVEOBJ] = (SafFunction *)NSObjArrayRemoveObj;
   SafTable[ARR_GET_OBJ_NUM] = (SafFunction *)NSObjArrayGetObjNum;

   SafTable[CM_BT_NEW] = (SafFunction *)CMNewBTree;
   SafTable[CM_BT_ADD_NODE] = (SafFunction *)CMBTreeAddNode;
   SafTable[CM_BT_FIND_NODE] = (SafFunction *)CMBTreeFindNode;
   SafTable[CM_BT_DEL_NODE] = (SafFunction *)CMBTreeDeleteNode;
   SafTable[CM_BT_DESTROY] = (SafFunction *)CMBTreeDestroy;
   SafTable[CM_BT_GET_NUM] = (SafFunction *)CMBTreeGetNumNode;
   SafTable[CM_BT_TRAVEL] = (SafFunction *)CMBTreeInorderTravel;

   SafTable[CM_STR_NEW] = (SafFunction *)NewCMStrObj;
   SafTable[CM_STR_ADD] = (SafFunction *)CMStrObjAdd;
   SafTable[CM_STR_REL] = (SafFunction *)CMStrObjRelease;
   SafTable[CM_STR_FREE] = (SafFunction *)CMStrObjFree;
   SafTable[CM_STR_GET] = (SafFunction *)CMStrObjGetString;
   SafTable[CM_STR_SIZE] = (SafFunction *)CMStrObjGetSize;
   SafTable[CM_COPY_STR] = (SafFunction *)CMCopyString;
   SafTable[CM_MAKE_UID] = (SafFunction *)MakeUID;

   SafTable[MS_NEW] = (SafFunction *)ConsumerNewPush;
   SafTable[MS_CREATE] = (SafFunction *)ConsumerCreatePush;



}

