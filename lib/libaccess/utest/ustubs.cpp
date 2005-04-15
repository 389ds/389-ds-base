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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include	<sys/types.h>
#include	<malloc.h>
#include	<string.h>
#include 	<base/crit.h>
#include	<base/plist.h>

#include	<libaccess/nserror.h>
#include 	<libaccess/acl.h>
#include 	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/ldapacl.h>
#include 	<ldaputil/dbconf.h>
#ifdef	NSPR20
#include	<prprf.h>
#else
#include	<nspr/prprf.h>
#endif

NSPR_BEGIN_EXTERN_C
extern char * ACL_Program;
extern int conf_getglobals();
extern int SPconf_getglobals();
extern int ereport(int, char*, ...);
extern int SPereport(int, char*, ...);
extern char * GetAdminLanguage(void);
extern char * XP_GetStringFromDatabase(char *strLibraryName, char *strLanguage, int iToken);
extern void ACL_Restart(void *cntlData);
extern int XP_SetError();
extern int XP_GetError();
extern int acl_usr_cache_init();
extern int acl_usr_cache_set_group();
extern int acl_usr_cache_group_check();
extern int acl_usr_cache_group_len_check();
extern int acl_usr_cache_enabled();
extern int get_userdn_ldap (NSErr_t *errp, PList_t subject,
                     PList_t resource, PList_t auth_info,
                     PList_t global_auth, void *unused);
extern char *ldapu_err2string(int err);
extern int ACL_CacheFlush(void);
NSPR_END_EXTERN_C

static char errbuf[10];

char *
ldapu_err2string(int err)
{
    sprintf(errbuf, "%d", err);
    return errbuf;
}


void init_ldb_rwlock ()
{
}

#ifdef notdef
char *system_errmsg()
{
    static char errmsg[1024];

    sprintf(errmsg, "Stubbed system_errmsg");
    return errmsg;
}
#endif

int
ACL_CacheFlushRegister(AclCacheFlushFunc_t flush_func)
{
    return 0;
}

int acl_usr_cache_init()
{
	return 0;
}

int acl_usr_cache_group_check()
{
	return 0;
}

int acl_usr_cache_set_group()
{
	return 0;
}

int acl_usr_cache_group_len_check()
{
	return 0;
}

int acl_usr_cache_enabled()
{
	return 0;
}

int get_userdn_ldap (NSErr_t *errp, PList_t subject,
                     PList_t resource, PList_t auth_info,
                     PList_t global_auth, void *unused)
{
	return LAS_EVAL_TRUE;
}

int XP_SetError()
{
	return 0;
}

int XP_GetError()
{
	return 0;
}

CRITICAL 
crit_init()
{
	return (CRITICAL)1;
}

void
crit_enter(CRITICAL c)
{
	return;
}

void
crit_exit(CRITICAL c)
{
	return;
}

void
crit_terminate(CRITICAL c)
{
	return;
}

int crit_owner_is_me(CRITICAL id)
{
    return 1;
}

int symTableFindSym()
{
	return 0;
}

int
ldap_auth_uid_groupid(LDAP *ld, char *uid, char *groupid,
		      char *base)
{
	return 0;
}

LDAP *
init_ldap (char *host, int port, int use_ssl)
{
	return (LDAP *)"init_ldap_stub";
}

int ACL_LDAPDatabaseHandle (NSErr_t *errp, const char *dbname, LDAP **ld,
			    char **basedn)
{
    *ld = (LDAP *)"ACL_LDAPDatabaseHandle_stub";
    if (basedn) *basedn = strdup("unknown basedn");
    return LAS_EVAL_TRUE;
}

#ifdef notdef
NSEFrame_t * nserrGenerate(NSErr_t * errp, long retcode, long errorid,
			   char * program, int errc, ...)
{
	return 0;
}
#endif

char * ACL_Program;

char *
LASUserGetUser()
{
	return "hmiller";
}

int 
LASIpGetIp()
{
	return(0x11223344);
}

int 
LASDnsGetDns(char **dnsv)
{
	*dnsv = "aruba.mcom.com";
	return 0;
}

int
ACL_DestroyList()
{
return(0);
}

int
aclCheckHosts()
{
return(0);
}

int
aclCheckUsers()
{
return(0);
}

char *LASGroupGetUser()
{
    return("hmiller");
}

int
SPconf_getglobals()
{
    return 0;
}

int
conf_getglobals()
{
    return 0;
}

int 
SPereport(int degree, char *fmt, ...)
{
    va_list args;
    char errstr[1024];

    va_start(args, fmt);
    PR_vsnprintf(&errstr[0], sizeof(errstr), fmt, args);
    printf("%s", errstr);
    va_end(args);
    return 0;
}

int 
ereport(int degree, char *fmt, ...)
{
    va_list args;
    char errstr[1024];

    va_start(args, fmt);
    PR_vsnprintf(&errstr[0], sizeof(errstr), fmt, args);
    printf("%s", errstr);
    va_end(args);
    return 0;
}

#ifdef notdef
int dbconf_read_config_file (const char *file, DBConfInfo_t **conf_info_out)
{
 return 0;
}
#endif

char *
GetAdminLanguage(void)
{
  return "";
}

static char errstr[1024];

char *
XP_GetStringFromDatabase(char *strLibraryName, char *strLanguage, int iToken)
{
    sprintf(errstr, "XP_GetAdminStr called for error %d\n", iToken);
    return errstr;
}

void
ACL_Restart(void * cntlData)
{
	return;
}

NSAPI_PUBLIC int
parse_ldap_url(NSErr_t *errp, ACLDbType_t dbtype, const char *name, const char
*url, PList_t plist, void **db)
{
	return 0;
}

int
ACL_CacheFlush(void)
{
	return 0;
}
