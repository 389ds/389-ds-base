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

/* #define DBG_PRINT */


#include <netsite.h>
extern "C" {
#include <secitem.h>
}
#include <base/crit.h>
#include <ldaputil/errors.h>
#include <libaccess/usrcache.h>
#include <libaccess/las.h>
#include <libaccess/authdb.h>
#include "permhash.h"

/* uid is unique within a database.  The user cache tables are stored per
 * database.  The following table maps a database name to the corresponding
 * user cache table.  The user cache table is another hash table which stores
 * the UserCacheObj instances.
 */
static PRHashTable *databaseUserCacheTable = 0;
static time_t acl_usr_cache_lifetime = (time_t)120;
static PRCList *usrobj_list = 0;
static const int num_usrobj = 200;
static CRITICAL usr_hash_crit = NULL;	/* Controls user cache hash tables & */
					/* usrobj link list */
static pool_handle_t *usrcache_pool = NULL;
static PRHashTable *singleDbTable = 0;

#define USEROBJ_PTR(l) \
    ((UserCacheObj*) ((char*) (l) - offsetof(UserCacheObj, list)))

static void user_hash_crit_enter (void)
{
    /* Caching may be disabled (usr_hash_crit will be NULL) */
    if (usr_hash_crit) crit_enter(usr_hash_crit);
}

static void user_hash_crit_exit (void)
{
    /* Caching may be disabled (usr_hash_crit will be NULL) */
    if (usr_hash_crit) crit_exit(usr_hash_crit);
}

static void user_hash_crit_init (void)
{
    usr_hash_crit = crit_init();
}

static PRHashNumber
usr_cache_hash_cert(const void *key)
{
    PRHashNumber h;
    const unsigned char *s;
    unsigned int i = 0;
    SECItem *derCert = (SECItem *)key;
    unsigned int len = derCert->len;
 
    h = 0;
    for (s = (const unsigned char *)derCert->data; i < len; s++, i++)
        h = (h >> 28) ^ (h << 4) ^ *s;
    return h;
}
 
static PRHashNumber
usr_cache_hash_fn (const void *key)
{
    UserCacheObj *usrObj = (UserCacheObj *)key;

    if (usrObj->derCert)
	return usr_cache_hash_cert(usrObj->derCert);
    else
	return PR_HashCaseString(usrObj->uid);
}

static int
usr_cache_compare_certs(const void *v1, const void *v2)
{
    const SECItem *c1 = (const SECItem *)v1;
    const SECItem *c2 = (const SECItem *)v2;

    return (c1->len == c2 ->len && !memcmp(c1->data, c2->data, c1->len));
}
 
static int
usr_cache_compare_fn(const void *v1, const void *v2)
{
    UserCacheObj *usrObj1 = (UserCacheObj *)v1;
    UserCacheObj *usrObj2 = (UserCacheObj *)v2;

    if (usrObj1->derCert && usrObj2->derCert)
	return usr_cache_compare_certs(usrObj1->derCert, usrObj2->derCert);
    else if (!usrObj1->derCert && !usrObj2->derCert)
	return PR_CompareCaseStrings(usrObj1->uid, usrObj1->uid);
    else
	return 0;
}

static PRHashTable *alloc_db2uid_table ()
{
    return PR_NewHashTable(0, 
			   usr_cache_hash_fn,
			   usr_cache_compare_fn,
			   PR_CompareValues,
			   &ACLPermAllocOps, 
			   usrcache_pool);
}


int acl_usr_cache_set_timeout (const int nsec)
{
    acl_usr_cache_lifetime = (time_t)nsec;
    return 0;
}


int acl_usr_cache_enabled ()
{
    return (acl_usr_cache_lifetime > 0);
}


int acl_usr_cache_init ()
{
    UserCacheObj *usrobj;
    int i;

    if (acl_usr_cache_lifetime <= 0) {
	/* Caching is disabled */
	DBG_PRINT1("usrcache is disabled");
	return 0;
    }

    usrcache_pool = pool_create();
    user_hash_crit_init();

    if (acl_num_databases() == 0) {
	/* Something wrong -- No databases registered yet! */
	return -1;
    }
    else if (acl_num_databases() == 1) {
	/* Optimize for single database */
	DBG_PRINT1("Optimizing usrcache for single db");
	singleDbTable = alloc_db2uid_table();
    }
    else {
	singleDbTable = 0;
	databaseUserCacheTable = PR_NewHashTable(0, 
						 PR_HashCaseString,
						 PR_CompareCaseStrings,
						 PR_CompareValues,
						 &ACLPermAllocOps, 
						 usrcache_pool);
    }

    /* Allocate first UserCacheObj and initialize the circular link list */
    usrobj = (UserCacheObj *)pool_malloc(usrcache_pool, sizeof(UserCacheObj));
    if (!usrobj) return -1;
    memset((void *)usrobj, 0, sizeof(UserCacheObj));
    usrobj_list = &usrobj->list;
    PR_INIT_CLIST(usrobj_list);

    /* Allocate rest of the UserCacheObj and put them in the link list */
    for(i = 0; i < num_usrobj; i++){
	usrobj = (UserCacheObj *)pool_malloc(usrcache_pool,
					     sizeof(UserCacheObj));
					     
	if (!usrobj) return -1;
	memset((void *)usrobj, 0, sizeof(UserCacheObj));
	PR_INSERT_AFTER(&usrobj->list, usrobj_list);
    }

    return (singleDbTable || databaseUserCacheTable) ? 0 : -1;
}

/* If the user hash table exists in the databaseUserCacheTable then return it.
 * Otherwise, create a new hash table, insert it in the databaseUserCacheTable
 * and then return it.
 */
static int usr_cache_table_get (const char *dbname, PRHashTable **usrTable)
{
    PRHashTable *table;

    if (singleDbTable) {
	*usrTable = singleDbTable;
	return LAS_EVAL_TRUE;
    }

    user_hash_crit_enter();

    table = (PRHashTable *)PR_HashTableLookup(databaseUserCacheTable,
					      dbname);

    if (!table) {
	/* create a new table and insert it in the databaseUserCacheTable */
	table = alloc_db2uid_table();

	if (table) {
	    PR_HashTableAdd(databaseUserCacheTable,
			    pool_strdup(usrcache_pool, dbname),
			    table);
	}
    }

    *usrTable = table;

    user_hash_crit_exit();

    return table ? LAS_EVAL_TRUE : LAS_EVAL_FAIL;
}

int acl_usr_cache_insert (const char *uid, const char *dbname,
			  const char *userdn, const char *passwd,
			  const char *group,
			  const SECItem *derCert, const time_t time)
{
    PRHashTable *usrTable;
    UserCacheObj *usrobj;
    UserCacheObj key;
    int rv;

    if (acl_usr_cache_lifetime <= 0) {
	/* Caching is disabled */
	return LAS_EVAL_TRUE;
    }

    rv = usr_cache_table_get (dbname, &usrTable);

    if (rv != LAS_EVAL_TRUE) return rv;

    user_hash_crit_enter();

    key.uid = (char *)uid;
    key.derCert = (SECItem *)derCert;

    usrobj = (UserCacheObj *)PR_HashTableLookup(usrTable, &key);

    if (usrobj) {
	time_t elapsed = time - usrobj->time;
	int expired = (elapsed >= acl_usr_cache_lifetime);

	/* Free & reset the old values in usrobj if -- there is an old value
	 * and if the new value is given then it is different or the usrobj
	 * has expired */
	/* Set the field if the new value is given and the field is not set */
	/* If the usrobj has not expired then we only want to update the field
	 * whose new value is non-NULL and different */

	/* Work on the 'uid' field */
	if (usrobj->uid &&
	    (uid ? strcmp(usrobj->uid, uid) : expired))
	{
	    pool_free(usrcache_pool, usrobj->uid);
	    usrobj->uid = 0;
	}
	if (uid && !usrobj->uid) {
	    usrobj->uid = pool_strdup(usrcache_pool, uid);
	}

	/* Work on the 'userdn' field */
	if (usrobj->userdn &&
	    (userdn ? strcmp(usrobj->userdn, userdn) : expired))
	{
	    pool_free(usrcache_pool, usrobj->userdn);
	    usrobj->userdn = 0;
	}
	if (userdn && !usrobj->userdn) {
	    usrobj->userdn = pool_strdup(usrcache_pool, userdn);
	}

	/* Work on the 'passwd' field */
	if (usrobj->passwd &&
	    (passwd ? strcmp(usrobj->passwd, passwd) : expired))
	{
	    pool_free(usrcache_pool, usrobj->passwd);
	    usrobj->passwd = 0;
	}
	if (passwd && !usrobj->passwd) {
	    usrobj->passwd = pool_strdup(usrcache_pool, passwd);
	}

	/* Work on the 'group' field -- not replace a valid group */
	if (!expired && usrobj->group &&
	    (group ? strcmp(usrobj->group, group) : expired))
	{
	    pool_free(usrcache_pool, usrobj->group);
	    usrobj->group = 0;
	}
	if (group && !usrobj->group) {
	    usrobj->group = pool_strdup(usrcache_pool, group);
	}

	/* Work on the 'derCert' field */
	if (usrobj->derCert &&
	    (derCert ? (derCert->len != usrobj->derCert->len ||
			memcmp(usrobj->derCert->data, derCert->data,
			       derCert->len))
	     : expired))
	{
	    SECITEM_FreeItem(usrobj->derCert, PR_TRUE);
	    usrobj->derCert = 0;
	}
	if (derCert && !usrobj->derCert) {
	    usrobj->derCert = SECITEM_DupItem((SECItem *)derCert);
	}

	/* Reset the time only if the usrobj has expired */
	if (expired) {
	    DBG_PRINT1("Replace ");
	    usrobj->time = time;
	}
	else {
	    DBG_PRINT1("Update ");
	}
    }
    else {
	/* Get the last usrobj from the link list, erase it and use it */
	/* Maybe the last usrobj is not invalid yet but we don't want to grow
	 * the list of usrobjs.  The last obj is the best candidate for being
	 * not valid.  We don't want to compare the time -- just use it.
	 */
	PRCList *tail = PR_LIST_TAIL(usrobj_list);
	usrobj = USEROBJ_PTR(tail);

	/* If the removed usrobj is in the hashtable, remove it from there */
	if (usrobj->hashtable) {
	    PR_HashTableRemove(usrobj->hashtable, usrobj);
	}

	/* Free the memory associated with the usrobj */
	if (usrobj->userdn) pool_free(usrcache_pool, usrobj->userdn);
	if (usrobj->passwd) pool_free(usrcache_pool, usrobj->passwd);
	if (usrobj->group) pool_free(usrcache_pool, usrobj->group);
	if (usrobj->derCert) SECITEM_FreeItem(usrobj->derCert, PR_TRUE);
	if (usrobj->uid) pool_free(usrcache_pool, usrobj->uid);

	/* Fill in the usrobj with the current data */
	usrobj->uid = pool_strdup(usrcache_pool, uid);
	usrobj->userdn = userdn ? pool_strdup(usrcache_pool, userdn) : 0;
	usrobj->passwd = passwd ? pool_strdup(usrcache_pool, passwd) : 0;
	usrobj->derCert = derCert ? SECITEM_DupItem((SECItem *)derCert) : 0;
	usrobj->group = group ? pool_strdup(usrcache_pool, group) : 0;
	usrobj->time = time;

	/* Add the usrobj to the user hash table */
	PR_HashTableAdd(usrTable, usrobj, usrobj);
	usrobj->hashtable = usrTable;
	DBG_PRINT1("Insert ");
    }

    /* Move the usrobj to the head of the list */
    PR_REMOVE_LINK(&usrobj->list);
    PR_INSERT_AFTER(&usrobj->list, usrobj_list);

    /* Set the time in the UserCacheObj */
    if (usrobj) {
	rv = LAS_EVAL_TRUE;
    }
    else {
	rv = LAS_EVAL_FAIL;
    }

    DBG_PRINT4("acl_usr_cache_insert: derCert = \"%s\" uid = \"%s\" at time = %ld\n",
	       usrobj->derCert ? (char *)usrobj->derCert->data : "<NONE>",
	       uid, time);

    user_hash_crit_exit();
    return rv;
}

static int acl_usr_cache_get_usrobj (const char *uid, const SECItem *derCert,
				     const char *dbname, const time_t time,
				     UserCacheObj **usrobj_out)
{
    PRHashTable *usrtable;
    UserCacheObj *usrobj;
    UserCacheObj key;
    time_t elapsed;
    int rv;

    *usrobj_out = 0;

    if (acl_usr_cache_lifetime <= 0) {
	/* Caching is disabled */
	return LAS_EVAL_FALSE;
    }

    rv = usr_cache_table_get(dbname, &usrtable);
    if (!usrtable) return LAS_EVAL_FALSE;

    key.uid = (char *)uid;
    key.derCert = (SECItem *)derCert;

    usrobj = (UserCacheObj *)PR_HashTableLookup(usrtable, &key);

    if (!usrobj) return LAS_EVAL_FALSE;

    rv = LAS_EVAL_FALSE;

    elapsed = time - usrobj->time;

    /* If the cache is valid, return the usrobj */
    if (elapsed < acl_usr_cache_lifetime) {
	rv = LAS_EVAL_TRUE;
	*usrobj_out = usrobj;
	DBG_PRINT4("usr_cache found: derCert = \"%s\" uid = \"%s\" at time = %ld\n",
		   usrobj->derCert ? (char *)usrobj->derCert->data : "<NONE>",
		   usrobj->uid, time);
    }
    else {
	DBG_PRINT4("usr_cache expired: derCert = \"%s\" uid = \"%s\" at time = %ld\n",
		   usrobj->derCert ? (char *)usrobj->derCert->data : "<NONE>",
		   usrobj->uid, time);
    }

    return rv;
}

int acl_usr_cache_passwd_check (const char *uid, const char *dbname,
				const char *passwd,
				const time_t time, char **dn,
				pool_handle_t *pool)
{
    UserCacheObj *usrobj;
    int rv;

    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE && usrobj->passwd && passwd &&
	!strcmp(usrobj->passwd, passwd))
    {
	/* extract dn from the usrobj */
	*dn = usrobj->userdn ? pool_strdup(pool, usrobj->userdn) : 0;
	rv = LAS_EVAL_TRUE;
	DBG_PRINT1("Success ");
    }
    else {
	rv = LAS_EVAL_FALSE;
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_passwd_check: uid = \"%s\" at time = %ld\n",
	       uid, time);
    user_hash_crit_exit();

    return rv;
}

int acl_usr_cache_group_check (const char *uid, const char *dbname,
			       const char *group, const time_t time)
{
    UserCacheObj *usrobj;
    int rv;

    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE && usrobj->group && group &&
	!strcmp(usrobj->group, group))
    {
	DBG_PRINT1("Success ");
    }
    else {
	rv = LAS_EVAL_FALSE;
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_group_check: uid = \"%s\" group = \"%s\"\n",
	       uid, group ? group : "<NONE>");
    user_hash_crit_exit();

    return rv;
}

int acl_usr_cache_group_len_check (const char *uid, const char *dbname,
				   const char *group, const int len,
				   const time_t time)
{
    UserCacheObj *usrobj;
    int rv;

    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE && usrobj->group && group &&
	!strncmp(usrobj->group, group, len))
    {
	rv = LAS_EVAL_TRUE;
	DBG_PRINT1("Success ");
    }
    else {
	rv = LAS_EVAL_FALSE;
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_group_check: uid = \"%s\" group = \"%s\"\n",
	       uid, group);
    user_hash_crit_exit();

    return rv;
}


int acl_usr_cache_get_userdn (const char *uid, const char *dbname,
			      const time_t time, char **userdn,
			      pool_handle_t *pool)
{
    UserCacheObj *usrobj;
    int rv;

    *userdn = 0;
    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE) {
	*userdn = usrobj->userdn ? pool_strdup(pool, usrobj->userdn) : 0;
	DBG_PRINT1("Success ");
    }
    else {
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_get_userdn: uid = \"%s\" userdn = \"%s\"\n",
	       uid, *userdn ? *userdn : "<NONE>");
    user_hash_crit_exit();

    return *userdn ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
}

int acl_usr_cache_userdn_check (const char *uid, const char *dbname,
				const char *userdn, const time_t time)
{
    UserCacheObj *usrobj;
    int rv;

    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE && usrobj->userdn && userdn &&
	!strcmp(usrobj->userdn, userdn))
    {
	DBG_PRINT1("Success ");
    }
    else {
	rv = LAS_EVAL_FALSE;
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_userdn_check: uid = \"%s\" userdn = \"%s\"\n",
	       uid, userdn ? userdn : "<NONE>");
    user_hash_crit_exit();

    return rv;
}

int acl_usr_cache_set_userdn (const char *uid, const char *dbname,
			      const char *userdn, const time_t time)
{
    int rv;

    /* acl_usr_cache_insert updates the existing un-expired entry or creates a
     * new one */
    rv = acl_usr_cache_insert(uid, dbname, userdn, 0, 0, 0, time);

    return rv;
}

int acl_usr_cache_get_group (const char *uid, const char *dbname,
			     const time_t time, char **group,
			     pool_handle_t *pool)
{
    UserCacheObj *usrobj;
    int rv;

    *group = 0;
    user_hash_crit_enter();
    rv = acl_usr_cache_get_usrobj(uid, 0, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE) {
	*group = usrobj->group ? pool_strdup(pool, usrobj->group) : 0;
	DBG_PRINT1("Success ");
    }
    else {
	DBG_PRINT1("Failed ");
    }

    DBG_PRINT3("acl_usr_cache_get_group: uid = \"%s\" group = \"%s\"\n",
	       uid, *group ? *group : "<NONE>");
    user_hash_crit_exit();

    return *group ? LAS_EVAL_TRUE : LAS_EVAL_FALSE;
}

int acl_usr_cache_set_group (const char *uid, const char *dbname,
			     const char *group, const time_t time)
{
    int rv;

    /* acl_usr_cache_insert updates the existing un-expired entry or creates a
     * new one */
    rv = acl_usr_cache_insert(uid, dbname, 0, 0, group, 0, time);

    return rv;
}

int acl_cert_cache_insert (void *cert_in, const char *dbname,
			   const char *uid, const char *dn,
			   const time_t time)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    SECItem derCert = cert->derCert;
    int rv;

    rv = acl_usr_cache_insert(uid, dbname, dn, 0, 0, &derCert, time);

    return rv;
}

/* Returns LAS_EVAL_TRUE if the user's cache is valid and returns uid */
int acl_cert_cache_get_uid (void *cert_in, const char *dbname,
			    const time_t time, char **uid, char **dn,
			    pool_handle_t *pool)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    SECItem derCert = cert->derCert;
    UserCacheObj *usrobj = 0;
    int rv;

    rv = acl_usr_cache_get_usrobj(0, &derCert, dbname, time, &usrobj);

    if (rv == LAS_EVAL_TRUE && usrobj && usrobj->uid) {
	*uid = pool_strdup(pool, usrobj->uid);
	*dn = usrobj->userdn ? pool_strdup(pool, usrobj->userdn) : 0;
    }
    else {
	*uid = 0;
	*dn = 0;
	rv = LAS_EVAL_FALSE;
    }

    return rv;
}

