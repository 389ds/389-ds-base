/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACL_LAS_HEADER
#define ACL_LAS_HEADER

#ifndef NOINTNSACL
#define INTNSACL
#endif /* !NOINTNSACL */

/* #include <prhash.h>  */
#include <plhash.h> 
#include <prclist.h>

#include <base/plist.h>
#include <libaccess/nserror.h>
#include <libaccess/acl.h>

#ifndef PUBLIC_NSACL_ACLDEF_H
#include "public/nsacl/acldef.h"
#endif /* !PUBLIC_NSACL_ACLDEF_H */

#define	ACL_MAX_METHOD		32
#define	ACL_MAX_DBTYPE		32

struct ACLAttrGetter {
	PRCList			list;	/* must be first */
	ACLMethod_t		method;
	ACLDbType_t		dbtype;
	ACLAttrGetterFn_t	fn;
	void			*arg;
};

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC extern int
	ACL_LasRegister(NSErr_t *errp, char *attr_name, LASEvalFunc_t
	eval_func, LASFlushFunc_t flush_func);
NSAPI_PUBLIC extern int
	ACL_LasFindEval(NSErr_t *errp, char *attr_name, LASEvalFunc_t
	*eval_funcp);
NSAPI_PUBLIC extern int
	ACL_LasFindFlush(NSErr_t *errp, char *attr_name, LASFlushFunc_t
	*flush_funcp);
extern void
	ACL_LasHashInit(void);
extern void
	ACL_LasHashDestroy(void);

/*
 *	Revised, normalized method/dbtype registration routines
 */
NSAPI_PUBLIC extern int
	ACL_MethodRegister(NSErr_t *errp, const char *name, ACLMethod_t *t);
NSAPI_PUBLIC extern int
	ACL_MethodIsEqual(NSErr_t *errp, const ACLMethod_t t1, const ACLMethod_t t2);
NSAPI_PUBLIC extern int
	ACL_MethodNameIsEqual(NSErr_t *errp, const ACLMethod_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_MethodFind(NSErr_t *errp, const char *name, ACLMethod_t *t);
NSAPI_PUBLIC extern ACLMethod_t
	ACL_MethodGetDefault(NSErr_t *errp);
NSAPI_PUBLIC extern int
	ACL_MethodSetDefault(NSErr_t *errp, const ACLMethod_t t);
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetMethod(NSErr_t *errp, PList_t auth_info, ACLMethod_t *t);
NSAPI_PUBLIC extern int
	ACL_AuthInfoSetMethod(NSErr_t *errp, PList_t auth_info, ACLMethod_t t);
NSAPI_PUBLIC extern int
	ACL_DbTypeRegister(NSErr_t *errp, const char *name, DbParseFn_t func, ACLDbType_t *t);
NSAPI_PUBLIC extern int
	ACL_DbTypeIsEqual(NSErr_t *errp, const ACLDbType_t t1, const ACLDbType_t t2);
NSAPI_PUBLIC extern int
	ACL_DbTypeNameIsEqual(NSErr_t *errp, const ACLDbType_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_DbTypeFind(NSErr_t *errp, const char *name, ACLDbType_t *t);
NSAPI_PUBLIC extern ACLDbType_t
	ACL_DbTypeGetDefault(NSErr_t *errp);
NSAPI_PUBLIC extern const char *
	ACL_DatabaseGetDefault(NSErr_t *errp);
NSAPI_PUBLIC extern int
	ACL_DatabaseSetDefault(NSErr_t *errp, const char *dbname);
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetDbType(NSErr_t *errp, PList_t auth_info, ACLDbType_t *t);
NSAPI_PUBLIC extern int
	ACL_DbTypeIsRegistered(NSErr_t *errp, const ACLDbType_t dbtype);
NSAPI_PUBLIC extern int
	ACL_AttrGetterRegister(NSErr_t *errp, const char *attr,
                               ACLAttrGetterFn_t fn, ACLMethod_t m,
                               ACLDbType_t d, int position, void *arg);

extern ACLDbType_t ACL_DbTypeLdap;

NSAPI_PUBLIC extern int
	ACL_DbTypeSetDefault(NSErr_t *errp, ACLDbType_t t);
NSAPI_PUBLIC extern DbParseFn_t
	ACL_DbTypeParseFn(NSErr_t *errp, const ACLDbType_t dbtype);
NSAPI_PUBLIC extern int
	ACL_AttrGetterFind(NSErr_t *errp, const char *attr,
			   ACLAttrGetterList_t *getters);
NSAPI_PUBLIC extern ACLAttrGetter_t *
	ACL_AttrGetterFirst(ACLAttrGetterList_t *getters);
NSAPI_PUBLIC extern ACLAttrGetter_t *
	ACL_AttrGetterNext(ACLAttrGetterList_t *getters,
			   ACLAttrGetter_t *last);

/* typedef PRHashTable AttrGetterTable_t; */
typedef PLHashTable AttrGetterTable_t;

typedef struct {
    char *method;
    char *authtype;
    char *dbtype;
    AttrGetterTable_t *attrGetters;
} MethodInfo_t;

NSAPI_PUBLIC int ACL_ModuleRegister (NSErr_t *errp, const char *moduleName, AclModuleInitFunc func);

NSAPI_PUBLIC int ACL_GetAttribute(NSErr_t *errp, const char *attr, void **val, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);

NSAPI_PUBLIC int ACL_DatabaseRegister(NSErr_t *errp, ACLDbType_t dbtype, const char *dbname, const char *url, PList_t plist);

NSAPI_PUBLIC int ACL_RegisterDbFromACL(NSErr_t *errp, const char *url, ACLDbType_t *dbtype);
NSAPI_PUBLIC int ACL_DatabaseFind(NSErr_t *errp, const char *dbname,
				  ACLDbType_t *dbtype, void **db);
NSAPI_PUBLIC int ACL_LDAPDatabaseHandle (NSErr_t *errp,
                                         const char *dbname, LDAP **ld,
					 char **basedn);
NSAPI_PUBLIC int ACL_AuthInfoGetDbname (PList_t auth_info, char **dbname);
NSAPI_PUBLIC int ACL_AuthInfoSetDbname (NSErr_t *errp, PList_t auth_info,
					const char *dbname);
NSAPI_PUBLIC int ACL_CacheFlushRegister(AclCacheFlushFunc_t func);
NSAPI_PUBLIC int ACL_SetDefaultResult (NSErr_t *errp,
				       ACLEvalHandle_t *acleval,
				       int result);
NSAPI_PUBLIC int ACL_GetDefaultResult (ACLEvalHandle_t *acleval);

struct program_groups {
	char *type;
	char **groups;
	char **programs;
};

extern int LASTimeOfDayEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASDayOfWeekEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASIpEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASDnsEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASGroupEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASUserEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);
extern int LASProgramEval(NSErr_t *errp, char *attribute, CmpOp_t comparator,
			char *pattern, ACLCachable_t *cachable, void **las_cookie,
			PList_t subject, PList_t resource, PList_t auth_info,
			PList_t global_auth);

extern void LASTimeOfDayFlush(void **cookie);
extern void LASDayOfWeekFlush(void **cookie);
extern void LASIpFlush(void **cookie);
extern void LASDnsFlush(void **cookie);

NSPR_END_EXTERN_C

#endif	/* ACL_LAS_HEADER */
