/** BEGIN COPYRIGHT BLOCK
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
extern int sema_destroy();
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

sema_destroy()
{
	return 0;
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

acl_usr_cache_init()
{
	return 0;
}

acl_usr_cache_group_check()
{
	return 0;
}

acl_usr_cache_set_group()
{
	return 0;
}

XP_SetError()
{
	return 0;
}

XP_GetError()
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

symTableFindSym()
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

LASIpGetIp()
{
	return(0x11223344);
}

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

aclCheckHosts()
{
return(0);
}

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
