/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACL_REGISTER_HEADER
#define ACL_REGISTER_HEADER

#include <prhash.h>

#include <ldap.h>
#include <base/pblock.h>
#include <base/plist.h>
#include <libaccess/nserror.h>
#include <libaccess/acl.h>

typedef	void * ACLMethod_t;
#define	ACL_METHOD_ANY		(ACLMethod_t)-1
#define	ACL_METHOD_INVALID	(ACLMethod_t)-2
extern ACLMethod_t ACL_METHOD_BASIC;

typedef	void * ACLDbType_t;
#define	ACL_DBTYPE_ANY		(ACLDbType_t)-1
#define	ACL_DBTYPE_INVALID	(ACLDbType_t)-2
extern ACLDbType_t ACL_ACL_DBTYPE_LDAP;

typedef int (*AttrGetterFn)(NSErr_t *errp, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth, void *arg);
typedef int (*AclModuleInitFunc)(pblock *pb, Session *sn, Request *rq);
typedef int (*DbParseFn_t)(NSErr_t *errp, ACLDbType_t dbtype,
			   const char *name, const char *url,
			   PList_t plist, void **db);
typedef int (*AclCacheFlushFunc_t)(void);

#ifdef __cplusplus
typedef int (*LASEvalFunc_t)(NSErr_t*, char*, CmpOp_t, char*, int*, void**, PList_t, PList_t, PList_t, PList_t);
typedef void (*LASFlushFunc_t)(void **);
#else
typedef int (*LASEvalFunc_t)();
typedef void (*LASFlushFunc_t)();
#endif

/* We need to hide ACLGetter_t */
typedef struct ACLGetter_s {
	ACLMethod_t	method;
	ACLDbType_t	db;
	AttrGetterFn	fn;
	void 		*arg;
} ACLGetter_t;
typedef ACLGetter_t *ACLGetter_p;

/*
 *	Command values for the "position" argument to ACL_RegisterGetter
 *	Any positive >0 value is the specific position in the list to insert
 *	the new function.
 */
#define	ACL_AT_FRONT		0
#define	ACL_AT_END		-1
#define	ACL_REPLACE_ALL 	-2
#define	ACL_REPLACE_MATCHING	-3

#ifdef	ACL_LIB_INTERNAL
#define	ACL_MAX_METHOD		32
#define	ACL_MAX_DBTYPE		32
#endif

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC extern int
	ACL_LasRegister( NSErr_t *errp, char *attr_name, LASEvalFunc_t
	eval_func, LASFlushFunc_t flush_func );
NSAPI_PUBLIC extern int
	ACL_LasFindEval( NSErr_t *errp, char *attr_name, LASEvalFunc_t
	*eval_funcp );
NSAPI_PUBLIC extern int
	ACL_LasFindFlush( NSErr_t *errp, char *attr_name, LASFlushFunc_t
	*flush_funcp );
extern void
	ACL_LasHashInit( void );
extern void
	ACL_LasHashDestroy( void );

/*
 *	Revised, normalized method/dbtype registration routines
 */
NSAPI_PUBLIC extern int
	ACL_MethodRegister(const char *name, ACLMethod_t *t);
NSAPI_PUBLIC extern int
	ACL_MethodIsEqual(const ACLMethod_t t1, const ACLMethod_t t2);
NSAPI_PUBLIC extern int
	ACL_MethodNameIsEqual(const ACLMethod_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_MethodFind(const char *name, ACLMethod_t *t);
NSAPI_PUBLIC extern ACLMethod_t
	ACL_MethodGetDefault();
NSAPI_PUBLIC extern void
	ACL_MethodSetDefault(const ACLMethod_t t);
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetMethod(PList_t auth_info, ACLMethod_t *t);

NSAPI_PUBLIC extern int
	ACL_DbTypeRegister(const char *name, DbParseFn_t func, ACLDbType_t *t);
NSAPI_PUBLIC extern int
	ACL_DbTypeIsEqual(const ACLDbType_t t1, const ACLDbType_t t2);
NSAPI_PUBLIC extern int
	ACL_DbTypeNameIsEqual(const ACLDbType_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_DbTypeFind(const char *name, ACLDbType_t *t);
NSAPI_PUBLIC extern const ACLDbType_t
	ACL_DbTypeGetDefault();
NSAPI_PUBLIC extern void
	ACL_DbTypeSetDefault(ACLDbType_t t);
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetDbType(PList_t auth_info, ACLDbType_t *t);
NSAPI_PUBLIC extern int
	ACL_DbTypeIsRegistered(const ACLDbType_t dbtype);
NSAPI_PUBLIC extern DbParseFn_t
	ACL_DbTypeParseFn(const ACLDbType_t dbtype);

NSAPI_PUBLIC extern int
	ACL_AttrGetterRegister(const char *attr, AttrGetterFn fn, ACLMethod_t m,
	ACLDbType_t d, int position, void *arg);
typedef ACLGetter_t *AttrGetterList; /* TEMPORARY */
NSAPI_PUBLIC extern int
	ACL_AttrGetterFind(PList_t auth_info, const char *attr,
	AttrGetterList *getters);

NSPR_END_EXTERN_C


/* LAS return codes - Must all be negative numbers */
#define	LAS_EVAL_TRUE		-1
#define	LAS_EVAL_FALSE		-2
#define	LAS_EVAL_DECLINE	-3
#define	LAS_EVAL_FAIL		-4
#define	LAS_EVAL_INVALID	-5
#define	LAS_EVAL_NEED_MORE_INFO	-6

#define ACL_ATTR_GROUP	    "group"
#define ACL_ATTR_RAW_USER_LOGIN "user-login"
#define ACL_ATTR_AUTH_USER	    "auth-user"
#define ACL_ATTR_AUTH_TYPE	    "auth-type"
#define ACL_ATTR_AUTH_DB	    "auth-db"
#define ACL_ATTR_AUTH_PASSWORD  "auth-password"
#define ACL_ATTR_USER	    "user"
#define ACL_ATTR_PASSWORD	    "pw"
#define ACL_ATTR_USERDN	    "userdn"
#define ACL_ATTR_RAW_USER	    "raw-user"
#define ACL_ATTR_RAW_PASSWORD   "raw-pw"
#define ACL_ATTR_USER_ISMEMBER  "user-ismember"
#define ACL_ATTR_DATABASE	    "database"
#define ACL_ATTR_DBTYPE	    "dbtype"
#define ACL_ATTR_DBNAME	    "dbname"
#define ACL_ATTR_DATABASE_URL   "url"
#define ACL_ATTR_METHOD	    "method"
#define ACL_ATTR_AUTHTYPE	    "authtype"
#define ACL_ATTR_AUTHORIZATION  "authorization"
#define ACL_ATTR_PARSEFN	    "parsefn"
#define ACL_ATTR_ATTRIBUTE	    "attr"
#define ACL_ATTR_GETTERFN	    "getterfunc"
#define ACL_ATTR_IP		    "ip"
#define ACL_ATTR_DNS	    "dns"
#define ACL_ATTR_MODULE	    "module"
#define ACL_ATTR_MODULEFUNC	    "func"
#define ACL_ATTR_GROUPS	    "groups"
#define ACL_ATTR_IS_VALID_PASSWORD "isvalid-password"
#define ACL_ATTR_CERT2USER	    "cert2user"
#define ACL_ATTR_USER_CERT	    "cert"
#define ACL_ATTR_PROMPT	    "prompt"
#define ACL_ATTR_TIME	    "time"
#define ACL_ATTR_USERS_GROUP    "users-group"

#define ACL_DBTYPE_LDAP	    "ldap"

#define METHOD_DEFAULT	    "default"

typedef PRHashTable AttrGetterTable_t;

typedef struct {
    char *method;
    char *authtype;
    char *dbtype;
    AttrGetterTable_t *attrGetters;
} MethodInfo_t;

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC int ACL_FindMethod (NSErr_t *errp, const char *method, MethodInfo_t **method_info_handle);
NSAPI_PUBLIC int ACL_RegisterModule (NSErr_t *errp, const char *moduleName, AclModuleInitFunc func);
NSAPI_PUBLIC int ACL_RegisterMethod (NSErr_t *errp, const char *method, const char *authtype, const char *dbtype, MethodInfo_t **method_info_handle);
NSAPI_PUBLIC int ACL_RegisterAttrGetter (NSErr_t *errp, MethodInfo_t *method_info_handle, const char *attr, AttrGetterFn func);
NSAPI_PUBLIC int ACL_UseAttrGettersFromMethod (NSErr_t *errp, const char *method, const char *usefrom);
NSAPI_PUBLIC int ACL_GetAttribute(NSErr_t *errp, const char *attr, void **val, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
NSAPI_PUBLIC int ACL_FindAttrGetter (NSErr_t *errp, const char *method, const char *attr, AttrGetterFn *func);
NSAPI_PUBLIC int ACL_CallAttrGetter (NSErr_t *errp, const char *method, const char *attr, PList_t subject, PList_t resource, PList_t auth_info, PList_t global_auth);
NSAPI_PUBLIC int ACL_RegisterDbType(NSErr_t *errp, const char *dbtype, DbParseFn_t func);
NSAPI_PUBLIC int ACL_RegisterDbName(NSErr_t *errp, ACLDbType_t dbtype, const char *dbname, const char *url, PList_t plist);
NSAPI_PUBLIC int ACL_RegisterDbFromACL(NSErr_t *errp, const char *url, ACLDbType_t *dbtype);
NSAPI_PUBLIC int ACL_DatabaseFind(NSErr_t *errp, const char *dbname,
				  ACLDbType_t *dbtype, void **db);
NSAPI_PUBLIC int ACL_SetDefaultDatabase (NSErr_t *errp, const char *dbname);
NSAPI_PUBLIC int ACL_SetDefaultMethod (NSErr_t *errp, const char *method);
NSAPI_PUBLIC const char *ACL_DbnameGetDefault (NSErr_t *errp);
NSAPI_PUBLIC int ACL_LDAPDatabaseHandle (NSErr_t *errp, const char *dbname, LDAP **ld);
NSAPI_PUBLIC int ACL_AuthInfoGetDbname (NSErr_t *errp, PList_t auth_info, char **dbname);
NSAPI_PUBLIC int ACL_CacheFlushRegister(AclCacheFlushFunc_t func);

NSPR_END_EXTERN_C

struct program_groups {
	char **groups;
	char **programs;
};
  
#endif
