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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 *	LAS registration interface
 */

#include <netsite.h>
#include <plhash.h>
#include <base/systems.h>
#include <base/util.h>
#include <prlog.h>
#include "permhash.h"
#include <libaccess/nserror.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/aclglobal.h>
#include "aclcache.h"
#include <libaccess/aclerror.h>

/* This is to force aclspace.o into ns-httpd30.dll */
static ACLGlobal_p *link_ACLGlobal = &ACLGlobal;

/* This forces oneeval.o into ns-httpd30.dll */
static ACLDispatchVector_t **link_nsacl_table = &__nsacl_table;

ACLMethod_t ACLMethodDefault = ACL_METHOD_INVALID;
ACLDbType_t ACLDbTypeDefault = ACL_DBTYPE_INVALID;
static char *ACLDatabaseDefault = 0;

ACLDbType_t ACL_DbTypeLdap = ACL_DBTYPE_INVALID;

DbParseFn_t	ACLDbParseFnTable[ACL_MAX_DBTYPE];

void
ACL_LasHashInit()
{
    int	i;

    ACLLasEvalHash = PR_NewHashTable(0,
				     PR_HashString,
				     PR_CompareStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps,
				     NULL);
    PR_ASSERT(ACLLasEvalHash);

    ACLLasFlushHash = PR_NewHashTable(0,
				     PR_HashString,
				     PR_CompareStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps,
				     NULL);
    PR_ASSERT(ACLLasFlushHash);

    ACLMethodHash = PR_NewHashTable(ACL_MAX_METHOD,
				     PR_HashCaseString,
				     PR_CompareCaseStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps,
				     NULL);
    PR_ASSERT(ACLMethodHash);

    ACLDbTypeHash = PR_NewHashTable(ACL_MAX_DBTYPE,
				     PR_HashCaseString,
				     PR_CompareCaseStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps,
				     NULL);
    PR_ASSERT(ACLDbTypeHash);

    for (i = 0; i < ACL_MAX_DBTYPE; i++)
	ACLDbParseFnTable[i] = 0;

    ACLAttrGetterHash = PR_NewHashTable(256,
				     PR_HashCaseString,
				     PR_CompareCaseStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps,
				     NULL);
    PR_ASSERT(ACLDbTypeHash);

    ACLDbNameHash = PR_NewHashTable(0, 
				    PR_HashCaseString,
				    PR_CompareCaseStrings,
				    PR_CompareValues,
				    &ACLPermAllocOps, 
				    ACL_DATABASE_POOL);
    PR_ASSERT(ACLDbNameHash);

    ACLUserLdbHash = PR_NewHashTable(0, 
				     PR_HashCaseString,
				     PR_CompareCaseStrings,
				     PR_CompareValues,
				     &ACLPermAllocOps, 
				     NULL);
    PR_ASSERT(ACLUserLdbHash);

    return;
}

void
ACL_LasHashDestroy()
{
    if (ACLLasEvalHash) {
        PR_HashTableDestroy(ACLLasEvalHash);
        ACLLasEvalHash=NULL;
    }
    if (ACLLasFlushHash) {
        PR_HashTableDestroy(ACLLasFlushHash);
        ACLLasFlushHash=NULL;
    }
}

/*  ACL_LasRegister
 *  INPUT
 *	errp		NSError structure
 *	attr_name	E.g. "ip" or "dns" etc.
 *	eval_func	E.g. LASIpEval
 *	flush_func	Optional - E.g. LASIpFlush or NULL
 *  OUTPUT
 *	0 on success, non-zero on failure
 */
NSAPI_PUBLIC int
ACL_LasRegister(NSErr_t *errp, char *attr_name, LASEvalFunc_t eval_func,
LASFlushFunc_t flush_func)
{
    if ((!attr_name) || (!eval_func)) return -1;

    ACL_CritEnter();

    /*  See if the function is already registered.  If so, report and
     *  error, but go ahead and replace it.
     */
    if (PR_HashTableLookup(ACLLasEvalHash, attr_name) != NULL) {
        nserrGenerate(errp, ACLERRDUPSYM, ACLERR3900, ACL_Program, 1,
                      attr_name);
    }

    /*  Put it in the hash tables  */
    if (NULL == PR_HashTableAdd(ACLLasEvalHash, attr_name, (void *)eval_func)) {
        ACL_CritExit();
        return -1;
    }
    if (NULL == 
              PR_HashTableAdd(ACLLasFlushHash, attr_name, (void *)flush_func)) {
        ACL_CritExit();
        return -1;
    }

    ACL_CritExit();
    return 0;
}

/*  ACL_LasFindEval
 *  INPUT
 *      errp            NSError pointer
 *      attr_name       E.g. "ip" or "user" etc.
 *      eval_funcp      Where the function pointer is returned.  NULL if the
 *                      function isn't registered.
 *		Must be called in a critical section as ACLEvalHash is a global
 * 		variable. 
 *  OUTPUT
 *      0 on success, non-zero on failure
 */
NSAPI_PUBLIC int
ACL_LasFindEval(NSErr_t *errp, char *attr_name, LASEvalFunc_t *eval_funcp)
{
 
    PR_ASSERT(attr_name);
    if (!attr_name) return -1;
 
    *eval_funcp = (LASEvalFunc_t)PR_HashTableLookup(ACLLasEvalHash, attr_name);
    return 0;
}
 
 
/*  ACL_LasFindFlush
 *  INPUT
 *      errp            NSError pointer
 *      attr_name       E.g. "ip" or "user" etc.
 *      eval_funcp      Where the function pointer is returned.  NULL if the
 *                      function isn't registered.
 *  OUTPUT
 *      0 on success, non-zero on failure
 */
NSAPI_PUBLIC int 
ACL_LasFindFlush(NSErr_t *errp, char *attr_name, LASFlushFunc_t *flush_funcp)
{
 
    PR_ASSERT(attr_name);
    if (!attr_name) return -1;
 
    *flush_funcp = (LASFlushFunc_t)PR_HashTableLookup(ACLLasFlushHash, attr_name);
    return 0;
}


/*  ACL_MethodRegister
 *  INPUT
 *	name		Method name string.  Can be freed after return.
 *  OUTPUT
 *	&t		Place to return the Method_t (>0)
 *	retcode		0 on success, non-zero otherwise
 */

int cur_method = 0;	/* Use a static counter to generate the numbers */

NSAPI_PUBLIC int
ACL_MethodRegister(NSErr_t *errp, const char *name, ACLMethod_t *t)
{
    ACLMethod_t rv;

    ACL_CritEnter();

    /*  See if this is already registered  */
    rv = (ACLMethod_t) PR_HashTableLookup(ACLMethodHash, name);
    if (rv != NULL) {
	*t = rv;
        ACL_CritExit();
	return 0;
    }

    /*  To prevent the hash table from resizing, don't get to 32 entries  */
    if (cur_method >= (ACL_MAX_METHOD-1)) {
	ACL_CritExit();
	return -1;
    }
	
    /*  Put it in the hash table  */
    if (NULL == PR_HashTableAdd(ACLMethodHash, name, (void *)++cur_method)) {
        ACL_CritExit();
        return -1;
    }
    *t = (ACLMethod_t) cur_method;

    ACL_CritExit();
    return 0;
}

NSAPI_PUBLIC int
ACL_MethodFind(NSErr_t *errp, const char *name, ACLMethod_t *t)
{
    ACLMethod_t rv;

    /*  Don't have to get the Critical Section lock 'cause the only danger
     *  would be if the hash table had to be resized.  We created it with
     *  room for 32 entries before that happens.
     */
    rv = (ACLMethod_t) PR_HashTableLookup(ACLMethodHash, name);
    if (rv != NULL) {
	*t = rv;
	return 0;
    }

    return -1;
}

typedef struct HashEnumArg_s {
    char **names;
    int count;
} HashEnumArg_t;

typedef HashEnumArg_t *HashEnumArg_p;

static int acl_hash_enumerator (PLHashEntry *he, PRIntn i, void *arg)
{
    HashEnumArg_t *info = (HashEnumArg_t *)arg;
    char **names = info->names;

    names[info->count++] = STRDUP((const char *)he->key);

    return names[info->count-1] ? 0 : -1;
}

int acl_registered_names(PLHashTable *ht, int count, char ***names)
{
    HashEnumArg_t arg;
    int rv;

    if (count == 0) {
	*names = 0;
	return 0;
    }

    arg.names = (char **)MALLOC(count * sizeof(char *));
    arg.count = 0;

    if (!arg.names) return -1;

    rv = PR_HashTableEnumerateEntries(ht, acl_hash_enumerator, &arg);

    if (rv >= 0) {
	/* success */
	*names = arg.names;
    }
    else {
	*names = 0;
    }

    return rv;
}

NSAPI_PUBLIC int
ACL_MethodNamesGet(NSErr_t *errp, char ***names, int *count)
{
    *count = cur_method;
    return acl_registered_names (ACLMethodHash, *count, names);
}

NSAPI_PUBLIC int
ACL_MethodNamesFree(NSErr_t *errp, char **names, int count)
{
    int i;

    if (!names) return 0;

    for (i = count-1; i; i--) FREE(names[i]);

    FREE(names);
    return 0;
}

NSAPI_PUBLIC int
ACL_DbTypeFind(NSErr_t *errp, const char *name, ACLDbType_t *t)
{
    ACLDbType_t rv;

    /*  Don't have to get the Critical Section lock 'cause the only danger
     *  would be if the hash table had to be resized.  We created it with
     *  room for 32 entries before that happens.
     */
    rv = (ACLDbType_t) PR_HashTableLookup(ACLDbTypeHash, name);
    if (rv != NULL) {
	*t = rv;
	return 0;
    }

    return -1;
}

/*  ACL_DbTypeRegister
 *  INPUT
 *	name		DbType name string.  Can be freed after return.
 *  OUTPUT
 *	&t		Place to return the DbType (>0)
 *	retcode		0 on success, non-zero otherwise
 */

int cur_dbtype = 0;	/* Use a static counter to generate the numbers */

NSAPI_PUBLIC int
ACL_DbTypeRegister(NSErr_t *errp, const char *name, DbParseFn_t func, ACLDbType_t *t)
{
    ACLDbType_t rv;

    ACL_CritEnter();

    /*  See if this is already registered  */
    rv = (ACLDbType_t) PR_HashTableLookup(ACLDbTypeHash, name);
    if (rv != NULL) {
	*t = rv;
	ACLDbParseFnTable[(int)(PRSize)rv] = func;
        ACL_CritExit();
	return 0;
    }
	
    /*  To prevent the hash table from resizing, don't get to 32 entries  */
    if (cur_dbtype >= (ACL_MAX_DBTYPE-1)) {
	ACL_CritExit();
	return -1;
    }
	
    /*  Put it in the hash table  */
    if (NULL == PR_HashTableAdd(ACLDbTypeHash, name, (void *)++cur_dbtype)) {
        ACL_CritExit();
        return -1;
    }
    *t = (ACLDbType_t) cur_dbtype;
    ACLDbParseFnTable[cur_dbtype] = func;

    ACL_CritExit();
    return 0;
}


NSAPI_PUBLIC int
ACL_DbTypeIsRegistered (NSErr_t *errp, const ACLDbType_t t)
{
    return (0 < ((int)(PRSize)t) && ((int)(PRSize)t) <= cur_dbtype);
}


/*  ACL_MethodIsEqual
 *	RETURNS		non-zero if equal.
 */
NSAPI_PUBLIC int
ACL_MethodIsEqual(NSErr_t *errp, const ACLMethod_t t1, const ACLMethod_t t2)
{
    return (t1 == t2);
}


/*  ACL_DbTypeIsEqual
 *	RETURNS		non-zero if equal.
 */
NSAPI_PUBLIC int
ACL_DbTypeIsEqual(NSErr_t *errp, const ACLDbType_t t1, const ACLDbType_t t2)
{
    return (t1 == t2);
}


/*  ACL_MethodNameIsEqual
 *	Takes a method type and a method name and sees if they match.
 *	Returns non-zero on match.
 */
NSAPI_PUBLIC int
ACL_MethodNameIsEqual(NSErr_t *errp, const ACLMethod_t t1, const char *name)
{
    int		rv;
    ACLMethod_t	t2;

    rv = ACL_MethodFind(errp, name, &t2);
    if (rv) 
        return (rv);
    else
        return (t1 == t2);
}

/*  ACL_DbTypeNameIsEqual
 *	Takes a dbtype type and a dbtype name and sees if they match.
 *	Returns non-zero on match.
 */
NSAPI_PUBLIC int
ACL_DbTypeNameIsEqual(NSErr_t *errp, const ACLDbType_t t1, const char *name)
{
    int		rv;
    ACLDbType_t	t2;

    rv = ACL_DbTypeFind(errp, name, &t2);
    if (rv) 
        return (rv);
    else
        return (t1 == t2);
}

/*  ACL_MethodGetDefault
 */
NSAPI_PUBLIC ACLMethod_t
ACL_MethodGetDefault(NSErr_t *errp)
{
    return (ACLMethodDefault);
}

/*  ACL_MethodSetDefault
 */
NSAPI_PUBLIC int
ACL_MethodSetDefault(NSErr_t *errp, const ACLMethod_t t)
{
    ACLMethodDefault = t;
    return 0;
}


/*  ACL_DbTypeGetDefault
 */
NSAPI_PUBLIC ACLDbType_t
ACL_DbTypeGetDefault(NSErr_t *errp)
{
    return (ACLDbTypeDefault);
}

/*  ACL_DbTypeSetDefault
 */
NSAPI_PUBLIC int
ACL_DbTypeSetDefault(NSErr_t *errp, ACLDbType_t t)
{
    ACLDbTypeDefault = t;
    return 0;
}


/*  ACL_DatabaseGetDefault
 */
NSAPI_PUBLIC const char *
ACL_DatabaseGetDefault(NSErr_t *errp)
{
    return (ACLDatabaseDefault);
}

/*  ACL_DatabaseSetDefault
 */
NSAPI_PUBLIC int
ACL_DatabaseSetDefault(NSErr_t *errp, const char *dbname)
{
    ACLDbType_t dbtype;
    int rv;
    void *db;

    if (!dbname || !*dbname) return LAS_EVAL_FAIL;

    rv = ACL_DatabaseFind(errp, dbname, &dbtype, &db);

    if (rv != LAS_EVAL_TRUE) return -1;

    if (ACLDatabaseDefault) pool_free(ACL_DATABASE_POOL, ACLDatabaseDefault);
    
    ACL_DbTypeSetDefault(errp, dbtype);
    ACLDatabaseDefault = pool_strdup(ACL_DATABASE_POOL, dbname);

    return ACLDatabaseDefault ? 0 : -1;
}


/*  ACL_AuthInfoGetMethod
 *	INPUT
 *	auth_info	A PList of the authentication name/value pairs as
 *			provided by EvalTestRights to the LAS.
 *	OUTPUT
 *	*t		The Method number.  This can be the default method
 number if the auth_info PList doesn't explicitly have a Method entry.
 *	retcode		0 on success.
 */
NSAPI_PUBLIC int
ACL_AuthInfoGetMethod(NSErr_t *errp, PList_t auth_info, ACLMethod_t *t)
{
    ACLMethod_t *methodp;

    if (!auth_info ||
	PListGetValue(auth_info, ACL_ATTR_METHOD_INDEX, (void **)&methodp, NULL) < 0)
    {
	/* No entry for "method" */
	*t = ACLMethodDefault;
    } else {
	*t = *methodp;
    }

    return 0;
}
    

/*  ACL_AuthInfoSetMethod
 *    INPUT
 *	auth_info	A PList of the authentication name/value pairs as
 *			provided by EvalTestRights to the LAS.
 *	t		The Method number.
 *    OUTPUT
 *	retcode		0 on success.
 */
NSAPI_PUBLIC int
ACL_AuthInfoSetMethod(NSErr_t *errp, PList_t auth_info, ACLMethod_t t)
{
    ACLMethod_t *methodp;
    int rv;

    if (auth_info) {
	rv = PListGetValue(auth_info, ACL_ATTR_METHOD_INDEX, (void **)&methodp,
			    NULL);

	if (rv < 0) {
	    /* No entry for "method" */
	    methodp = (ACLMethod_t *)PERM_MALLOC(sizeof(ACLMethod_t));
	    if (!methodp) return -1;
	    *methodp = t;
	    PListInitProp(auth_info, ACL_ATTR_METHOD_INDEX, ACL_ATTR_METHOD, methodp, 0);
	}
	else {
	    /* replace the old entry */
	    if (!methodp) return -1;
	    *methodp = t;
	}
    }
    else {
	return -1;
    }

    return 0;
}
    

/*  ACL_AuthInfoSetDbname
 *    INPUT
 *	auth_info	A PList of the authentication name/value pairs as
 *			provided by EvalTestRights to the LAS.
 *	dbname		Name of the new auth_info database.
 *    OUTPUT
 *	retcode		0 on success.
 */
NSAPI_PUBLIC int
ACL_AuthInfoSetDbname(NSErr_t *errp, PList_t auth_info, const char *dbname)
{
    ACLDbType_t *dbtype = NULL;
    ACLDbType_t *t2;
    char *copy;
    char *n2;
    void *db;
    int old1;
    int old2;
    int rv;

    if (auth_info) {
    	dbtype = (ACLDbType_t *)PERM_MALLOC(sizeof(ACLDbType_t));
	if (!dbtype) {
	    /* out of memory */
	    return -1;
	}
	rv = ACL_DatabaseFind(errp, dbname, dbtype, (void **)&db);

	if (rv != LAS_EVAL_TRUE) {
	    PERM_FREE(dbtype);
	    return -1;
	}

	/* Check the existing entry */
	old1 = PListGetValue(auth_info, ACL_ATTR_DBTYPE_INDEX, (void **)&t2,
			     NULL);
	old2 = PListGetValue(auth_info, ACL_ATTR_DATABASE_INDEX, (void **)&n2,
			     NULL);

	if (old1 >= 0 && old2 >= 0) {
	    /* check if the old entry is same */
	    if (ACL_DbTypeIsEqual(errp, *dbtype, *t2)) {
		/* Nothing to do */
		PERM_FREE(dbtype);
		return 0;
	    }
	}
	/* free the old entries */
	if (old1 >= 0) {
	    PListDeleteProp(auth_info, ACL_ATTR_DBTYPE_INDEX, ACL_ATTR_DBTYPE);
	    PERM_FREE(t2);
	}
	if (old2 >= 0) {
	    PListDeleteProp(auth_info, ACL_ATTR_DATABASE_INDEX, ACL_ATTR_DATABASE);
	    PERM_FREE(n2);
	}

	/* Create new entries for "dbtype" & "dbname" */
	copy = (char *)PERM_STRDUP(dbname);
	if (!copy) {
	    PERM_FREE(dbtype);
	    return -1;
	}
	PListInitProp(auth_info, ACL_ATTR_DATABASE_INDEX,
		      ACL_ATTR_DATABASE, copy, 0);
	PListInitProp(auth_info, ACL_ATTR_DBTYPE_INDEX, ACL_ATTR_DBTYPE, 
		      dbtype, 0);
    }
    else {
	return -1;
    }

    return 0;
}


/*  ACL_AuthInfoGetDbType
 *	INPUT
 *	auth_info	A PList of the authentication name/value pairs as
 *			provided by EvalTestRights to the LAS.
 *	OUTPUT
 *	*t		The DbType number.  This can be the default dbtype
 *			number if the auth_info PList doesn't explicitly
 *			have a DbType entry.
 *	retcode		0 on success.
 */
NSAPI_PUBLIC int
ACL_AuthInfoGetDbType(NSErr_t *errp, PList_t auth_info, ACLDbType_t *t)
{
    ACLDbType_t *dbtypep;

    if (!auth_info ||
	PListGetValue(auth_info, ACL_ATTR_DBTYPE_INDEX, (void **)&dbtypep, NULL) < 0)
    {
	/* No entry for "dbtype" */
	*t = ACLDbTypeDefault;
    } else {
	*t = *dbtypep;
    }

    return 0;
}
    
/*  ACL_AuthInfoGetDbname
 *	INPUT
 *	auth_info	A PList of the authentication name/value pairs as
 *			provided by EvalTestRights to the LAS.
 *	OUTPUT
 *	dbname		The database name.  This can be the default database
 *			name if the auth_info PList doesn't explicitly
 *			have a database entry.
 *	retcode		0 on success.
 */
NSAPI_PUBLIC int
ACL_AuthInfoGetDbname(PList_t auth_info, char **dbname)
{
    char *dbstr;

    if (!auth_info ||
	PListGetValue(auth_info, ACL_ATTR_DATABASE_INDEX, (void **)&dbstr, NULL) < 0)
    {
	/* No entry for "database" */
        dbstr = ACLDatabaseDefault;
    } 

    /* else the value was already set by the PListGetValue call */
    *dbname = dbstr;
    return 0;
}
    
NSAPI_PUBLIC DbParseFn_t
ACL_DbTypeParseFn(NSErr_t *errp, const ACLDbType_t dbtype)
{
    if (ACL_DbTypeIsRegistered(errp, dbtype))
	return ACLDbParseFnTable[(int)(PRSize)dbtype];
    else
	return 0;
}

/*  The hash table is keyed by attribute name, and contains pointers to the
 *  PRCList headers.  These in turn, circularly link a set of AttrGetter_s
 *  structures.
 */
NSAPI_PUBLIC int
ACL_AttrGetterRegister(NSErr_t *errp, const char *attr, ACLAttrGetterFn_t fn,
                       ACLMethod_t m, ACLDbType_t d, int position, void *arg)
{
    ACLAttrGetter_t	*getter;
    PLHashEntry         **hep;

    if (position != ACL_AT_FRONT  &&  position != ACL_AT_END) {
	return -1;
    }

    ACL_CritEnter();
    
    hep = PR_HashTableRawLookup(ACLAttrGetterHash, PR_HashCaseString(attr), attr);

    /*  Now, allocate the current entry  */
    getter = (ACLAttrGetter_t *)CALLOC(sizeof(ACLAttrGetter_t));
    if (getter == NULL) {
        ACL_CritExit();
        return -1;
    }
    getter->method	= m;
    getter->dbtype	= d;
    getter->fn	= fn;
    getter->arg = arg;

    if (*hep == 0) {	/* New entry */
        PR_INIT_CLIST(&getter->list);
        if (NULL == PR_HashTableAdd(ACLAttrGetterHash, attr, (void *)getter)) {
            FREE(getter);
            ACL_CritExit();
            return -1;
        }
    }
    else {

        ACLAttrGetter_t *head = (ACLAttrGetter_t *)((*hep)->value);

        PR_INSERT_BEFORE(&getter->list, &head->list);

        if (position == ACL_AT_FRONT) {

            /* Set new head of list */
            (*hep)->value = (void *)getter;
        }
    }

    ACL_CritExit();
    return 0;
}

NSAPI_PUBLIC int
ACL_AttrGetterFind(NSErr_t *errp, const char *attr,
                   ACLAttrGetterList_t *getters)
{
    *getters = PR_HashTableLookup(ACLAttrGetterHash, attr);
    if (*getters)
	return 0;
    else
        return -1;
}

NSAPI_PUBLIC
ACLAttrGetter_t * ACL_AttrGetterFirst(ACLAttrGetterList_t *getters)
{
    ACLAttrGetter_t * first = 0;

    if (getters && *getters) {

        first = (ACLAttrGetter_t *)(*getters);
    }

    return first;
}

NSAPI_PUBLIC ACLAttrGetter_t *
ACL_AttrGetterNext(ACLAttrGetterList_t *getters, ACLAttrGetter_t *last)
{
    ACLAttrGetter_t *head;
    ACLAttrGetter_t *next = 0;

    if (getters && *getters && last) {

        head = (ACLAttrGetter_t *)(*getters);
        if (head) {

            /* End of list? */
            if (last != (ACLAttrGetter_t *)PR_LIST_TAIL(&head->list)) {

                /* No, get next entry */
                next = (ACLAttrGetter_t *)PR_NEXT_LINK(&last->list);
            }
        }
    }

    return next;
}
