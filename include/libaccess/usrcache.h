/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACL_USER_CACHE_H
#define ACL_USER_CACHE_H

#ifdef NSPR20
#include <plhash.h>
#else
#include <nspr/prhash.h>
#endif

#include <sys/types.h>
#include <time.h>
/* Removed for new ns security integration
#include <sec.h>
*/
#include <key.h>
#include <cert.h>
#include <prclist.h>

typedef struct {
    PRCList list;		/* pointer to next & prev obj */
    char *uid;			/* unique within a database */
    char *userdn;		/* LDAP DN if using LDAP db */
    char *passwd;		/* password */
    SECItem *derCert;		/* raw certificate data */
    char *group;		/* group recently checked for membership */
    time_t time;		/* last time when the cache was validated */
    PRHashTable *hashtable;	/* hash table where this obj is being used */
} UserCacheObj;

NSPR_BEGIN_EXTERN_C

/* Set the number of seconds the cache is valid */
extern int acl_usr_cache_set_timeout (const int nsec);

/* Is the cache enabled? */
extern int acl_usr_cache_enabled();

/* initialize user cache */
extern int acl_usr_cache_init ();

/* Creates a new user obj entry */
extern int acl_usr_cache_insert (const char *uid, const char *dbname,
				 const char *dn, const char *passwd,
				 const char *group, const SECItem *derCert,
				 const time_t time);

/* Add group to the user's cache obj. */
extern int acl_usr_cache_set_group (const char *uid, const char *dbname,
				    const char *group, const time_t time);

/* Add userdn to the user's cache obj. */
extern int acl_usr_cache_set_userdn (const char *uid, const char *dbname,
				     const char *userdn, const time_t time);

/* Returns LAS_EVAL_TRUE if the user's password matches -- also returns the dn */
extern int acl_usr_cache_passwd_check (const char *uid, const char *dbname,
				       const char *passwd,
				       const time_t time, char **dn,
				       pool_handle_t *pool);

/* Returns LAS_EVAL_TRUE if the user is a member of the group */
extern int acl_usr_cache_group_check (const char *uid, const char *dbname,
				      const char *group, const time_t time);

/* Returns LAS_EVAL_TRUE if the user is a member of the group */
extern int acl_usr_cache_group_len_check (const char *uid, const char *dbname,
					  const char *group,
					  const int len,
					  const time_t time);

/* Returns LAS_EVAL_TRUE if the user's cache is valid and has a group */
extern int acl_usr_cache_get_group (const char *uid, const char *dbname,
				    const time_t time, char **group,
				    pool_handle_t *pool);

/* Returns LAS_EVAL_TRUE if the user is a member of the group */
extern int acl_usr_cache_userdn_check (const char *uid, const char *dbname,
				       const char *userdn, const time_t time);

/* Returns LAS_EVAL_TRUE if the user's cache is valid and has userdn */
extern int acl_usr_cache_get_userdn (const char *uid, const char *dbname,
				     const time_t time, char **userdn,
				     pool_handle_t *pool);

/* Creates a new user obj entry for cert to user mapping */
extern int acl_cert_cache_insert (void *cert, const char *dbname,
				  const char *uid, const char *dn,
				  const time_t time);

/* Returns LAS_EVAL_TRUE if the user's cache is valid and returns uid */
extern int acl_cert_cache_get_uid (void *cert, const char *dbname,
				   const time_t time, char **uid,
				   char **dn, pool_handle_t *pool);

NSPR_END_EXTERN_C


#endif /* ACL_USER_CACHE_H */
