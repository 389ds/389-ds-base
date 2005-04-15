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

#include <prlog.h>
#include <base/crit.h>
#include <base/ereport.h>
#include <plhash.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/aclglobal.h>
#include <libaccess/usrcache.h>
#include <libaccess/las.h>
#include <libaccess/ldapacl.h>
#include "aclutil.h"
#include "permhash.h"
#include "aclcache.h"


static CRITICAL acl_hash_crit = NULL;	/* Controls Global Hash */

enum {
    ACL_URI_HASH,
    ACL_URI_GET_HASH
};

/*	ACL_ListHashKeyHash
 *	Given an ACL List address, computes a randomized hash value of the
 *	ACL structure pointer addresses by simply adding them up.  Returns
 *	the resultant hash value.
 */
static PLHashNumber
ACL_ListHashKeyHash(const void *Iacllist)
{
    PLHashNumber hash=0;
    ACLWrapper_t *wrap;
    ACLListHandle_t *acllist=(ACLListHandle_t *)Iacllist;

    for (wrap=acllist->acl_list_head; wrap; wrap=wrap->wrap_next) {
	hash += (PLHashNumber)(PRSize)wrap->acl;
    }

    return (hash);
}

/*	ACL_ListHashKeyCompare
 *	Given two acl lists, compares the addresses of the acl pointers within
 *	them to see if theyre identical.  Returns 1 if equal, 0 otherwise.
 */
static int
ACL_ListHashKeyCompare(const void *Iacllist1, const void *Iacllist2)
{
    ACLWrapper_t *wrap1, *wrap2;
    ACLListHandle_t *acllist1=(ACLListHandle_t *)Iacllist1;
    ACLListHandle_t *acllist2=(ACLListHandle_t *)Iacllist2;

    if (acllist1->acl_count != acllist2->acl_count) 
        return 0;

    wrap1 = acllist1->acl_list_head;
    wrap2 = acllist2->acl_list_head;

    while ((wrap1 != NULL) && (wrap2 != NULL)) {
	if (wrap1->acl != wrap2->acl) 
            return 0;
	wrap1 = wrap1->wrap_next;
	wrap2 = wrap2->wrap_next;
    }

    if ((wrap1 != NULL) || (wrap2 != NULL)) 
        return 0;
    else 
        return 1;
}

/*  ACL_ListHashValueCompare
 *  Returns 1 if equal, 0 otherwise
 */
static int
ACL_ListHashValueCompare(const void *acllist1, const void *acllist2)
{

    return (acllist1 == acllist2);
}

void 
ACL_ListHashInit()
{
    ACLListHash = PR_NewHashTable(200, 
				  ACL_ListHashKeyHash,
				  ACL_ListHashKeyCompare,
				  ACL_ListHashValueCompare,
				  &ACLPermAllocOps, 
				  NULL);
    if (ACLListHash == NULL) {
	ereport(LOG_SECURITY, "Unable to allocate ACL List Hash\n");
	return;
    }

    return;
}

static void 
ACL_ListHashDestroy()
{
    if (ACLListHash) {
        PR_HashTableDestroy(ACLListHash);
        ACLListHash = NULL;
    }

    return;
}

/*  ACL_ListHashUpdate
 *  Typically called with the &rq->acllist.  Checks if the newly generated
 *  acllist matches one that's already been created.  If so, toss the new
 *  list and set the pointer to the old list in its place.
 */
void 
ACL_ListHashUpdate(ACLListHandle_t **acllistp)
{
    NSErr_t *errp = 0;
    ACLListHandle_t *tmp_acllist;

    PR_ASSERT(ACL_AssertAcllist(*acllistp));

    tmp_acllist = (ACLListHandle_t *)PR_HashTableLookup(ACLListHash, *acllistp);
    if (tmp_acllist  &&  tmp_acllist != *acllistp) {
	PR_ASSERT(*acllistp  &&  *acllistp != ACL_LIST_NO_ACLS);
	ACL_ListDestroy(errp, *acllistp);
	*acllistp = tmp_acllist;
	PR_ASSERT(ACL_CritHeld());
        tmp_acllist->ref_count++;	/* we're gonna use it */
    } else { 			/* Wasn't in the list */
	PR_HashTableAdd(ACLListHash, *acllistp, *acllistp);
    }

    PR_ASSERT(ACL_AssertAcllist(*acllistp));
    return;
}

/*  ACL_ListCacheEnter
 *  In some cases, the URI cache is useless.  E.g. when virtual servers are used.
 *  When that happens, the List Cache is still useful, because the cached ACL
 *  List has the Eval cache in it, plus any LAS caches.
 */
NSAPI_PUBLIC void
ACL_ListHashEnter(ACLListHandle_t **acllistp)
{
    ACL_CritEnter();

    /*  Look for a matching ACL List and use it if we find one.  */
    if (*acllistp)  {
	PR_ASSERT(*acllistp != ACL_LIST_NO_ACLS);
        PR_ASSERT(ACL_AssertAcllist(*acllistp));
	ACL_ListHashUpdate(acllistp);
    } else {
	*acllistp = ACL_LIST_NO_ACLS;
    }

    ACL_CritExit();
    PR_ASSERT(ACL_AssertAcllist(*acllistp));
    return;
}

/*  ACL_ListHashCheck
 *  When Virtual Servers are active, and the ACL URI cache is inactive, someone
 *  with an old ACL List pointer can check to see if it's still valid.  This will
 *  also increment the reference count on it.
 */
NSAPI_PUBLIC int
ACL_ListHashCheck(ACLListHandle_t **acllistp)
{
    ACLListHandle_t *tmp_acllist;

    if (*acllistp == ACL_LIST_NO_ACLS) return 1;

    ACL_CritEnter();

    tmp_acllist = (ACLListHandle_t *)PR_HashTableLookup(ACLListHash, *acllistp);
    if (tmp_acllist) {
	PR_ASSERT(*acllistp  &&  *acllistp != ACL_LIST_NO_ACLS);
	*acllistp = tmp_acllist;
	PR_ASSERT(ACL_CritHeld());
        tmp_acllist->ref_count++;	/* we're gonna use it */
        ACL_CritExit();
	PR_ASSERT(ACL_AssertAcllist(*acllistp));
        return 1;		/* Normal path */
    } else { 			/* Wasn't in the list */
        ACL_CritExit();
        return 0;
    }

}
   

void
ACL_UriHashDestroy(void)
{
    if (acl_uri_hash) {
	PR_HashTableDestroy(acl_uri_hash);
        acl_uri_hash = NULL;
    }
    if (acl_uri_get_hash) {
	PR_HashTableDestroy(acl_uri_get_hash);
        acl_uri_get_hash = NULL;
    }
    pool_destroy((void **)acl_uri_hash_pool);
    acl_uri_hash_pool = NULL;

}

void
ACL_Destroy(void)
{
    ACL_ListHashDestroy();
    ACL_UriHashDestroy();
    ACL_LasHashDestroy();
}

/* 	Only used in ASSERT statements to verify that we have the lock 	 */
int
ACL_CritHeld(void)
{
    return (crit_owner_is_me(acl_hash_crit));
}

NSAPI_PUBLIC void
ACL_CritEnter(void)
{
    crit_enter(acl_hash_crit);
}

NSAPI_PUBLIC void 
ACL_CritExit(void)
{
    crit_exit(acl_hash_crit);
}

void
ACL_CritInit(void)
{
    acl_hash_crit = crit_init();
}

void
ACL_UriHashInit(void)
{
    acl_uri_hash = PR_NewHashTable(200,
                                     PR_HashString,
                                     PR_CompareStrings,
                                     PR_CompareValues,
                                     &ACLPermAllocOps,
                                     NULL);
    acl_uri_get_hash = PR_NewHashTable(200,
                                     PR_HashString,
                                     PR_CompareStrings,
                                     PR_CompareValues,
                                     &ACLPermAllocOps,
                                     NULL);
    acl_uri_hash_pool = pool_create();
}

/*  ACL_CacheCheck
 *  INPUT
 *	uri	A URI string pointer
 *	acllistp A pointer to an acllist placeholder.  E.g. &rq->acllist
 *  OUTPUT
 *	return	1 if cached. 0 if not.   The reference count on the ACL List
 *	is INCREMENTED, and will be decremented when ACL_EvalDestroy or 
 *	ACL_ListDecrement is
 *	called. 
 */
int
ACL_INTCacheCheck(int which, char *uri, ACLListHandle_t **acllistp)
{
    PLHashTable *hash;
    PR_ASSERT(uri && acl_uri_hash && acl_uri_get_hash);

    /*  ACL cache:  If the ACL List is already in the cache, there's no need
     *  to go through the pathcheck directives.
     *  NULL	means that the URI hasn't been accessed before.
     *  ACL_LIST_NO_ACLS	
     *		means that the URI has no ACLs.
     *  Anything else is a pointer to the acllist.
     */
    ACL_CritEnter();

    /* Get the pointer to the hash table after acquiring the lock */
    if (which == ACL_URI_HASH)
	hash = acl_uri_hash;
    else
	hash = acl_uri_get_hash;

    *acllistp = (ACLListHandle_t *)PR_HashTableLookup(hash, uri);
    if (*acllistp != NULL) {
	if (*acllistp != ACL_LIST_NO_ACLS) {
            PR_ASSERT((*acllistp)->ref_count >= 0);
	    PR_ASSERT(ACL_CritHeld());
	    (*acllistp)->ref_count++;
	}
        ACL_CritExit();
	PR_ASSERT(ACL_AssertAcllist(*acllistp));
        return 1;		/* Normal path */
    }

    ACL_CritExit();
    return 0;
}

int
ACL_CacheCheckGet(char *uri, ACLListHandle_t **acllistp)
{
    return (ACL_INTCacheCheck(ACL_URI_GET_HASH, uri, acllistp));
}

int
ACL_CacheCheck(char *uri, ACLListHandle_t **acllistp)
{
    return (ACL_INTCacheCheck(ACL_URI_HASH, uri, acllistp));
}

 
/*  ACL_CacheEnter
 *  INPUT
 *	acllist or 0 if there were no ACLs that applied.
 *  OUTPUT
 *      The acllist address may be changed if it matches an existing one.
 */
static void
ACL_INTCacheEnter(int which, char *uri, ACLListHandle_t **acllistp)
{
    ACLListHandle_t *tmpacllist;
    NSErr_t *errp = 0;
    PLHashTable *hash;

    PR_ASSERT(uri);

    ACL_CritEnter();

    /* Get the pointer to the hash table after acquiring the lock */
    if (which == ACL_URI_HASH)
	hash = acl_uri_hash;
    else
	hash = acl_uri_get_hash;

    /*  Check again (now that we're in the critical section) to see if
     *  someone else created an ACL List for this URI.  If so, discard the
     *  list that we made and replace it with the one just found.
     */
    tmpacllist = (ACLListHandle_t *)PR_HashTableLookup(hash, uri);
    if (tmpacllist != NULL) {
        if (tmpacllist != ACL_LIST_NO_ACLS) {
	    PR_ASSERT(ACL_CritHeld());
            tmpacllist->ref_count++;	/* we're going to use it */
        }
	ACL_CritExit();
	if (*acllistp  &&  *acllistp != ACL_LIST_NO_ACLS) {
	    ACL_ListDestroy(errp, *acllistp);
	}
	*acllistp = tmpacllist;
	PR_ASSERT(ACL_AssertAcllist(*acllistp));
	return;
    }

    /*  Didn't find another list, so put ours in.  */
    /*  Look for a matching ACL List and use it if we find one.  */
    if (*acllistp)  {
	PR_ASSERT(*acllistp != ACL_LIST_NO_ACLS);
        PR_ASSERT(ACL_AssertAcllist(*acllistp));
	ACL_ListHashUpdate(acllistp);
    } else {
	*acllistp = ACL_LIST_NO_ACLS;
    }
    PR_HashTableAdd(hash, pool_strdup((void **)acl_uri_hash_pool, uri), (void *)*acllistp);

    ACL_CritExit();
    PR_ASSERT(ACL_AssertAcllist(*acllistp));
    return;
}

void
ACL_CacheEnter(char *uri, ACLListHandle_t **acllistp)
{
    ACL_INTCacheEnter(ACL_URI_HASH, uri, acllistp);
    return;
}

void
ACL_CacheEnterGet(char *uri, ACLListHandle_t **acllistp)
{
    ACL_INTCacheEnter(ACL_URI_GET_HASH, uri, acllistp);
    return;
}

/*  ACL_AddAclName
 *  Adds the ACLs for just the terminal object specified in a pathname.
 *  INPUT
 *	path	The filesystem pathname of the terminal object.
 *	acllistp The address of the list of ACLs found thus far.  
 *		Could be NULL.  If so, a new acllist will be allocated (if any
 *		acls are found).  Otherwise the existing list will be added to.
 *      masterlist	Usually acl_root_30.
 */
void
ACL_AddAclName(char *path, ACLListHandle_t **acllistp, ACLListHandle_t
*masterlist)
{
    ACLHandle_t *acl;
    NSErr_t *errp = 0;

#ifdef XP_WIN32
    acl = ACL_ListFind(errp, masterlist, path, ACL_CASE_INSENSITIVE);
#else
    acl = ACL_ListFind(errp, masterlist, path, ACL_CASE_SENSITIVE);
#endif
    if (!acl)
	return;

    PR_ASSERT(ACL_AssertAcl(acl));

    if (!*acllistp)
	*acllistp = ACL_ListNew(errp);
    ACL_ListAppend(NULL, *acllistp, acl, 0);

    PR_ASSERT(ACL_AssertAcllist(*acllistp));
    return;
}


/*  ACL_GetPathAcls
 *  Adds the ACLs for all directories plus the terminal object along a given
 *  filesystem pathname. For each pathname component, look for the name, the
 *  name + "/", and the name + "/*".  The last one is because the resource
 *  picker likes to postpend "/*" for directories.
 *  INPUT
 *	path	The filesystem pathname of the terminal object.
 *	acllistp The address of the list of ACLs found thus far.  
 *		Could be NULL.  If so, a new acllist will be allocated (if any
 *		acls are found).  Otherwise the existing list will be added to.
 *	prefix  A string to be prepended to the path component when looking
 *		for a matching ACL tag.
 */
void
ACL_GetPathAcls(char *path, ACLListHandle_t **acllistp, char *prefix,
ACLListHandle_t *masterlist)
{
    char *slashp=path;
    int  slashidx;
    char ppath[ACL_PATH_MAX];
    int  prefixlen;
    char *dst;

    PR_ASSERT(path);
    PR_ASSERT(prefix);

    dst = strncpy(ppath, prefix, ACL_PATH_MAX);
    if (dst >= (ppath+ACL_PATH_MAX-1)) {
	ereport(LOG_SECURITY, "Abort - the path is too long for ACL_GetPathAcls to handle\n");
	abort();
    }
    prefixlen = strlen(ppath);

    /* Handle the first "/". i.e. the root directory */
    if (*path == '/') {
	ppath[prefixlen]='/';
	ppath[prefixlen+1]='\0';
    	ACL_AddAclName(ppath, acllistp, masterlist);
        strcat(ppath, "*");
	ACL_AddAclName(ppath, acllistp, masterlist);
    	slashp = path;		
    }

    do {
	slashp = strchr(++slashp, '/');
	if (slashp) {
            slashidx = slashp - path;
            strncpy(&ppath[prefixlen], path, slashidx);
            ppath[slashidx+prefixlen] = '\0';
	    ACL_AddAclName(ppath, acllistp, masterlist);
	    /*  Must also handle "/a/b/" in addition to "/a/b"  */
	    strcat(ppath, "/");
	    ACL_AddAclName(ppath, acllistp, masterlist);
	    strcat(ppath, "*");
	    ACL_AddAclName(ppath, acllistp, masterlist);
	    continue;
	}
	strcpy(&ppath[prefixlen], path);
	ACL_AddAclName(ppath, acllistp, masterlist);
	strcat(ppath, "/");
	ACL_AddAclName(ppath, acllistp, masterlist);
	strcat(ppath, "*");
	ACL_AddAclName(ppath, acllistp, masterlist);
	break;
    } while (slashp);

}


static int get_is_owner_default (NSErr_t *errp, PList_t subject,
				 PList_t resource, PList_t auth_info,
				 PList_t global_auth, void *unused)
{
    /* Make sure we don't generate error "all getters declined" message from
     * ACL_GetAttribute.
     */
	PListInitProp(subject, ACL_ATTR_IS_OWNER_INDEX, ACL_ATTR_IS_OWNER,
		"true", 0);

    return LAS_EVAL_TRUE;
}


NSAPI_PUBLIC int
ACL_Init(void)
{
    ACL_InitAttr2Index();
    ACLGlobal = (ACLGlobal_p)PERM_CALLOC(sizeof(ACLGlobal_s));
    oldACLGlobal = (ACLGlobal_p)PERM_CALLOC(sizeof(ACLGlobal_s));
    PR_ASSERT(ACLGlobal && oldACLGlobal);
    ACL_DATABASE_POOL = pool_create();
    ACL_METHOD_POOL = pool_create();
    ACL_CritInit();
    ACL_UriHashInit();
    ACL_ListHashInit();
    ACL_LasHashInit();
    ACL_Init2();
    init_ldb_rwlock();
    ACL_RegisterInit();

    return 0;
}

/* This one gets called at startup AND at cache flush time. */
void
ACL_Init2(void)
{

    /* Register the ACL functions */
    ACL_LasRegister(NULL, "timeofday", LASTimeOfDayEval, LASTimeOfDayFlush);
    ACL_LasRegister(NULL, "dayofweek", LASDayOfWeekEval, LASDayOfWeekFlush);
    ACL_LasRegister(NULL, "ip", LASIpEval, LASIpFlush);
    ACL_LasRegister(NULL, "dns", LASDnsEval, LASDnsFlush);
    ACL_LasRegister(NULL, "dnsalias", LASDnsEval, LASDnsFlush);
    ACL_LasRegister(NULL, "group", LASGroupEval, (LASFlushFunc_t)NULL);
    ACL_LasRegister(NULL, "user", LASUserEval, (LASFlushFunc_t)NULL);
#ifdef MCC_ADMSERV
    ACL_LasRegister(NULL, "program", LASProgramEval, (LASFlushFunc_t)NULL);
#endif

    ACL_AttrGetterRegister(NULL, ACL_ATTR_USERDN,
			   get_userdn_ldap,
			   ACL_METHOD_ANY, ACL_DBTYPE_ANY,
			   ACL_AT_END, NULL);
    return;
}

NSAPI_PUBLIC int
ACL_InitPostMagnus(void)
{
    int rv;

    rv = ACL_AttrGetterRegister(NULL, ACL_ATTR_IS_OWNER,
				get_is_owner_default,
				ACL_METHOD_ANY, ACL_DBTYPE_ANY,
				ACL_AT_END, NULL);
    return rv;
}

NSAPI_PUBLIC int
ACL_LateInitPostMagnus(void)
{
    return acl_usr_cache_init();
}
