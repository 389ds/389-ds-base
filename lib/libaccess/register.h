/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACL_REGISTER_HEADER
#define ACL_REGISTER_HEADER

#include <libaccess/nserror.h>
#include <libaccess/acl.h>
#include <libaccess/las.h>

typedef	void * ACLMethod_t;
#define	ACL_METHOD_ANY		(ACLMethod_t)-1
#define	ACL_METHOD_INVALID	(ACLMethod_t)-2
typedef	void * ACLDbType_t;
#define	ACL_DBTYPE_ANY		(ACLDbType_t)-1
#define	ACL_DBTYPE_INVALID	(ACLDbType_t)-2

typedef struct ACLGetter_s {
	ACLMethod_t	method;
	ACLDbType_t	db;
	AttrGetterFn	fn;
} ACLGetter_t;
typedef ACLGetter_s * ACLGetter_p;

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
	ACL_MethodIsEqual(ACLMethod_t t1, ACLMethod_t t2);
NSAPI_PUBLIC extern int
	ACL_MethodNameIsEqual(ACLMethod_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_MethodFind(const char *name, ACLMethod_t *t);
NSAPI_PUBLIC extern ACLMethod_t
	ACL_MethodGetDefault();
NSAPI_PUBLIC extern void
	ACL_MethodSetDefault();
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetMethod(PList_t auth_info, ACLMethod_t *t);

NSAPI_PUBLIC extern int
	ACL_DbTypeRegister(const char *name, DbParseFn_t func, ACLDbType_t *t);
NSAPI_PUBLIC extern int
	ACL_DbTypeIsEqual(ACLDbType_t t1, ACLDbType_t t2);
NSAPI_PUBLIC extern int
	ACL_DbTypeNameIsEqual(ACLDbType_t t, const char *name);
NSAPI_PUBLIC extern int
	ACL_DbTypeFind(const char *name, ACLDbType_t *t);
NSAPI_PUBLIC extern ACLDbType_t
	ACL_DbTypeGetDefault();
NSAPI_PUBLIC extern void
	ACL_DbTypeSetDefault();
NSAPI_PUBLIC extern int
	ACL_AuthInfoGetDbType(PList_t auth_info, ACLDbType_t *t);

NSAPI_PUBLIC extern int
	ACL_RegisterGetter(AttrGetterFn fn, ACLMethod_t m, ACLDbType_t d, int
	position, void *arg);

NSPR_END_EXTERN_C

#endif
