/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Aruna Victor	
 * NT NSAPI works differently from UNIX. The DLL doesn't know the addresses
 * of the functions in the server process and needs to be told them.
 */

#include <nt/nsapi.h>

#ifdef BUILD_DLL

#include <libadmin/libadmin.h>
#include <libaccess/aclproto.h>
#include <base/fsmutex.h>
#include <i18n.h>
#include <base/ereport.h>

VOID NsapiDummy()
{	
	int i = 0;
	SafTable = (SafFunction **)MALLOC(400 * sizeof(VOID*));

	/* Force references to libadmin */
	SafTable[i++] = (SafFunction *)get_userdb_dir;
	/* Functions from libadmin:error.c */
	SafTable[i++] = (SafFunction *)report_error;
	/* Functions from libadmin:template.c */
	SafTable[i++] = (SafFunction *)helpJavaScriptForTopic;

	/* Force references to base */
	SafTable[i++] = (SafFunction *)fsmutex_init;

}
#endif /* BUILD_DLL */

VOID InitializeSafFunctions() 
{

	SafTable = (SafFunction **)MALLOC(400 * sizeof(VOID *));

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
	SafTable[FILE_UNIX2LOCAL] = (SafFunction *)file_unix2local;
	SafTable[DIR_OPEN] = (SafFunction *)dir_open;
	SafTable[DIR_READ] = (SafFunction *)dir_read;
	SafTable[DIR_CLOSE] = (SafFunction *)dir_close;

/* Functions from ereport.h */
	SafTable[EREPORT] = (SafFunction *)ereport ;

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

/* Functions from systems.h */
	SafTable[UTIL_STRCASECMP] = (SafFunction *)util_strcasecmp;
	SafTable[UTIL_STRNCASECMP] = (SafFunction *)util_strncasecmp;

/* Functions from util.h */
	SafTable[UTIL_HOSTNAME] = (SafFunction *)util_hostname;
	SafTable[UTIL_ITOA] = (SafFunction *)util_itoa;
	SafTable[UTIL_VSPRINTF] = (SafFunction *)util_vsprintf;
	SafTable[UTIL_SPRINTF] = (SafFunction *)util_sprintf;
	SafTable[UTIL_VSNPRINTF] = (SafFunction *)util_vsnprintf;
	SafTable[UTIL_SNPRINTF] = (SafFunction *)util_snprintf;

	SafTable[LOG_ERROR_EVENT] = (SafFunction *)LogErrorEvent;

/* Functions from aclproto.h */
	SafTable[ACL_LISTCONCAT] = (SafFunction *)ACL_ListConcat;

/* Functions from i18n.h */
	SafTable[GETCLIENTLANG] = (SafFunction *)GetClientLanguage;

/* Functions from file.h */
        SafTable[SYSTEM_FOPENWT] = (SafFunction *)system_fopenWT;
        SafTable[SYSTEM_MALLOC] = (SafFunction *)system_malloc;
        SafTable[SYSTEM_FREE] = (SafFunction *)system_free;
        SafTable[SYSTEM_REALLOC] = (SafFunction *)system_realloc;
        SafTable[SYSTEM_STRDUP] = (SafFunction *)system_strdup;

/* Functions from crit.h */
        SafTable[CRIT_INIT] = (SafFunction *)crit_init;
        SafTable[CRIT_ENTER] = (SafFunction *)crit_enter;
        SafTable[CRIT_EXIT] = (SafFunction *)crit_exit;
        SafTable[CRIT_TERMINATE] = (SafFunction *)crit_terminate;
        SafTable[SYSTHREAD_CURRENT] = (SafFunction *)systhread_current;
}
