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

/* #define DBG_PRINT */

#include <netsite.h>
#include <base/rwlock.h>
#include <base/ereport.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include "aclutil.h"
#include <ldaputil/errors.h>
#include <ldaputil/certmap.h>
#include <ldaputil/ldaputil.h>
#include <ldaputil/dbconf.h>
#include <ldaputil/ldapauth.h>
#include <libaccess/authdb.h>
#include <libaccess/ldapacl.h>
#include <libaccess/usrcache.h>
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclglobal.h>
#include <libaccess/aclerror.h>

#define BIG_LINE 1024

static int need_ldap_over_ssl = 0;
static RWLOCK ldb_rwlock = (RWLOCK)0;

void init_ldb_rwlock ()
{
    ldb_rwlock = rwlock_Init();
}

void ldb_write_rwlock (LDAPDatabase_t *ldb, RWLOCK lock)
{
    DBG_PRINT1("ldb_write_rwlock\n");
    /* Don't lock for local database -- let ldapsdk handle thread safety*/
    if (!ldapu_is_local_db(ldb))
	rwlock_WriteLock(lock);
}

void ldb_read_rwlock (LDAPDatabase_t *ldb, RWLOCK lock)
{
    DBG_PRINT1("ldb_read_rwlock\n");
    /* Don't lock for local database -- let ldapsdk handle thread safety*/
    if (!ldapu_is_local_db(ldb))
	rwlock_ReadLock(lock);
}

void ldb_unlock_rwlock (LDAPDatabase_t *ldb, RWLOCK lock)
{
    DBG_PRINT1("ldb_unlock_rwlock\n");
    /* we don't lock for local database */
    if (!ldapu_is_local_db(ldb))
	rwlock_Unlock(lock);
}

int ACL_NeedLDAPOverSSL ()
{
    return need_ldap_over_ssl;
}

NSAPI_PUBLIC int parse_ldap_url (NSErr_t *errp, ACLDbType_t dbtype,
				 const char *dbname, const char *url,
				 PList_t plist, void **db)
{
    LDAPDatabase_t *ldb;
    char *binddn = 0;
    char *bindpw = 0;
    int rv;

    *db = 0;

    if (!url || !*url) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5800, ACL_Program, 1, XP_GetAdminStr(DBT_ldapaclDatabaseUrlIsMissing));
	return -1;
    }

    if (!dbname || !*dbname) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5810, ACL_Program, 1, XP_GetAdminStr(DBT_ldapaclDatabaseNameIsMissing));
	return -1;
    }

    /* look for binddn and bindpw in the plist */
    if (plist) {
	PListFindValue(plist, LDAPU_ATTR_BINDDN, (void **)&binddn, NULL);
	PListFindValue(plist, LDAPU_ATTR_BINDPW, (void **)&bindpw, NULL);
    }

    rv = ldapu_url_parse(url, binddn, bindpw, &ldb);

    if (rv != LDAPU_SUCCESS) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5820, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclErrorParsingLdapUrl), ldapu_err2string(rv));
	return -1;
    }

    /* success */
    *db = ldb;
    
    /* Check if we need to do LDAP over SSL */
    if (!need_ldap_over_ssl) {
	need_ldap_over_ssl = ldb->use_ssl;
    }

    return 0;
}

int get_is_valid_password_basic_ldap (NSErr_t *errp, PList_t subject,
				      PList_t resource, PList_t auth_info,
				      PList_t global_auth, void *unused)
{
    /* If the raw-user-name and raw-user-password attributes are present then
     * verify the password against the LDAP database.
     * Otherwise call AttrGetter for raw-user-name.
     */
    char *raw_user;
    char *raw_pw;
    char *userdn = 0;
    int rv;
    char *dbname;
    ACLDbType_t dbtype;
    LDAPDatabase_t *ldb;
    time_t *req_time = 0;
    pool_handle_t *subj_pool = PListGetPool(subject)

    DBG_PRINT1("get_is_valid_password_basic_ldap\n");
    rv = ACL_GetAttribute(errp, ACL_ATTR_RAW_USER, (void **)&raw_user,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }

    rv = ACL_GetAttribute(errp, ACL_ATTR_RAW_PASSWORD, (void **)&raw_pw,
			  subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }

    if (!raw_pw || !*raw_pw) {
	/* Null password is not allowed in LDAP since most LDAP servers let
	 * the bind call succeed as anonymous login (with limited privileges).
	 */
	return LAS_EVAL_FALSE;
    }

    /* Authenticate the raw_user and raw_pw against LDAP database. */
    rv = ACL_AuthInfoGetDbname(auth_info, &dbname);

    if (rv < 0) {
	char rv_str[16];
	sprintf(rv_str, "%d", rv);
	nserrGenerate(errp, ACLERRFAIL, ACLERR5830, ACL_Program, 2,
	XP_GetAdminStr(DBT_ldapaclUnableToGetDatabaseName), rv_str);
        return LAS_EVAL_FAIL;
    }

    rv = ACL_DatabaseFind(errp, dbname, &dbtype, (void **)&ldb);

    if (rv != LAS_EVAL_TRUE) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR5840, ACL_Program, 2,
	XP_GetAdminStr(DBT_ldapaclUnableToGetParsedDatabaseName), dbname);
        return rv;
    }

    if (acl_usr_cache_enabled()) {
	/* avoid unnecessary system call to get time if cache is disabled */
	req_time = acl_get_req_time(resource);

	/* We have user name and password. */
	/* Check the cache to see if the password is valid */
	rv = acl_usr_cache_passwd_check(raw_user, dbname, raw_pw, *req_time,
					&userdn, subj_pool);
    }
    else {
	rv = LAS_EVAL_FALSE;
    }

    if (rv != LAS_EVAL_TRUE) {
	LDAPMessage *res = 0;
	const char *some_attrs[] = { "C", 0 };
	LDAP *ld;
	char *udn;
	/* Not found in the cache */

	/* Since we will bind with the user/password and other code relying on
	 * ldb being bound as ldb->binddn and ldb->bindpw may fail.  So block
	 * them until we are done.
	 */
	ldb_write_rwlock(ldb, ldb_rwlock);
	rv = ldapu_ldap_init_and_bind(ldb);

	if (rv != LDAPU_SUCCESS) {
	    ldb_unlock_rwlock(ldb, ldb_rwlock);
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5850, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclCoudlntInitializeConnectionToLdap), ldapu_err2string(rv));
	    return LAS_EVAL_FAIL;
	}

	/* LDAPU_REQ will reconnect & retry once if LDAP server went down */
	ld = ldb->ld;
	LDAPU_REQ(rv, ldb, ldapu_find_uid_attrs(ld, raw_user,
						ldb->basedn, some_attrs,
						1, &res));

	if (rv == LDAPU_SUCCESS) {
	    LDAPMessage *entry = ldap_first_entry(ld, res);

	    userdn = ldap_get_dn(ld, entry);
	    
	    /* LDAPU_REQ will reconnect & retry once if LDAP server went down */
	    LDAPU_REQ(rv, ldb, ldapu_auth_userdn_password(ld, userdn, raw_pw));

	    /* Make sure we rebind with the server's DN
	     * ignore errors from ldapu_ldap_rebind -- we will get the same
	     * errors in subsequent calls to LDAP.  Return status from the
	     * above call is our only interest now.
	     */
	    ldapu_ldap_rebind(ldb);
	}

	if (res) ldap_msgfree(res);
	ldb_unlock_rwlock(ldb, ldb_rwlock);

	if (rv == LDAPU_FAILED || rv == LDAP_INVALID_CREDENTIALS) {
	    /* user entry not found or incorrect password  */
	    if (userdn) ldap_memfree(userdn);
	    return LAS_EVAL_FALSE;
	}
	else if (rv != LDAPU_SUCCESS) {
	    /* some unexpected LDAP error */
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5860, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclPassworkCheckLdapError), ldapu_err2string(rv));
	    if (userdn) ldap_memfree(userdn);
	    return LAS_EVAL_FAIL;
	}

	/* Make an entry in the cache */
	if (acl_usr_cache_enabled()) {
	    acl_usr_cache_insert(raw_user, dbname, userdn, raw_pw, 0, 0,
				 *req_time);
	}
	udn = pool_strdup(subj_pool, userdn);
	ldap_memfree(userdn);
	userdn = udn;
    }

    PListInitProp(subject, ACL_ATTR_IS_VALID_PASSWORD_INDEX, ACL_ATTR_IS_VALID_PASSWORD, raw_user, 0);
    PListInitProp(subject, ACL_ATTR_USERDN_INDEX, ACL_ATTR_USERDN, userdn, 0);
    return LAS_EVAL_TRUE;
}

static int acl_grpcmpfn (const void *groupids, const char *group,
			 const int len)
{
    const char *token = (const char *)groupids;
    int tlen;
    char delim = ',';

    while((token = acl_next_token_len(token, delim, &tlen)) != NULL) {
	if (tlen > 0 && tlen == len && !strncmp(token, group, len))
	    return LDAPU_SUCCESS;
	else if (tlen == 0 || 0 != (token = strchr(token+tlen, delim)))
	    token++;
	else
	    break;
    }

    return LDAPU_FAILED;
}

int get_user_ismember_ldap (NSErr_t *errp, PList_t subject,
			    PList_t resource, PList_t auth_info,
			    PList_t global_auth, void *unused)
{
    int retval;
    int rv;
    char *userdn;
    char *groups;
    char *member_of = 0;
    LDAPDatabase_t *ldb;
    char *dbname;
    ACLDbType_t dbtype;

    DBG_PRINT1("get_user_ismember_ldap\n");

    rv = ACL_GetAttribute(errp, ACL_ATTR_USERDN, (void **)&userdn, subject,
			  resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return LAS_EVAL_FAIL;
    }

    rv = ACL_GetAttribute(errp, ACL_ATTR_GROUPS, (void **)&groups, subject,
			  resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return rv;
    }

    rv = ACL_AuthInfoGetDbname(auth_info, &dbname);

    if (rv < 0) {
	char rv_str[16];
	sprintf(rv_str, "%d", rv);
	nserrGenerate(errp, ACLERRINVAL, ACLERR5900, ACL_Program, 2, XP_GetAdminStr(DBT_GetUserIsMemberLdapUnabelToGetDatabaseName), rv_str);
        return rv;
    }

    rv = ACL_DatabaseFind(errp, dbname, &dbtype, (void **)&ldb);

    if (rv != LAS_EVAL_TRUE) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR5910, ACL_Program, 2, XP_GetAdminStr(DBT_GetUserIsMemberLdapUnableToGetParsedDatabaseName), dbname);
	return rv;
    }

    ldb_read_rwlock(ldb, ldb_rwlock);
    rv = ldapu_ldap_init_and_bind(ldb);

    if (rv != LDAPU_SUCCESS) {
	ldb_unlock_rwlock(ldb, ldb_rwlock);
	nserrGenerate(errp, ACLERRFAIL, ACLERR5930, ACL_Program, 2,
	XP_GetAdminStr(DBT_GetUserIsMemberLdapCouldntInitializeConnectionToLdap),   ldapu_err2string(rv));
	return LAS_EVAL_FAIL;
    }

    /* check if the user is member of any of the groups */
    /* LDAPU_REQ will reconnect & retry once if LDAP server went down */
    LDAPU_REQ(rv, ldb, ldapu_auth_userdn_groupids(ldb->ld,
						  userdn,
						  groups,
						  acl_grpcmpfn,
						  ldb->basedn,
						  &member_of));

    ldb_unlock_rwlock(ldb, ldb_rwlock);
	
    if (rv == LDAPU_SUCCESS) {
	/* User is a member of one of the groups */
	if (member_of) {
	    PListInitProp(subject, ACL_ATTR_USER_ISMEMBER_INDEX,
			  ACL_ATTR_USER_ISMEMBER,
			  pool_strdup(PListGetPool(subject), member_of), 0);
	    retval = LAS_EVAL_TRUE;
	}
	else {
	    /* This shouldn't happen */
	    retval = LAS_EVAL_FALSE;
	}
    }
    else if (rv == LDAPU_FAILED) {
	/* User is not a member of any of the groups */
	retval = LAS_EVAL_FALSE;
    }
    else {
	/* unexpected LDAP error */
	nserrGenerate(errp, ACLERRFAIL, ACLERR5950, ACL_Program, 2,
		      XP_GetAdminStr(DBT_GetUserIsMemberLdapError),
		      ldapu_err2string(rv));
	retval = LAS_EVAL_FAIL;
    }

    return retval;
}


/* This function returns LDAPU error codes so that the caller can call
 * ldapu_err2string to get the error string.
 */
int acl_map_cert_to_user (NSErr_t *errp, const char *dbname,
			  LDAPDatabase_t *ldb, void *cert,
			  PList_t resource, pool_handle_t *pool,
			  char **user, char **userdn)
{
    int rv;
    LDAPMessage *res;
    LDAPMessage *entry;
    char *uid;
    time_t *req_time = 0;

    if (acl_usr_cache_enabled()) {
	req_time = acl_get_req_time(resource);

	rv = acl_cert_cache_get_uid (cert, dbname, *req_time, user, userdn,
				     pool);
    }
    else {
	rv = LAS_EVAL_FALSE;
    }

    if (rv != LAS_EVAL_TRUE) {
	/* Not found in the cache */

	ldb_read_rwlock(ldb, ldb_rwlock);
	rv = ldapu_ldap_init_and_bind(ldb);

	/* LDAPU_REQ will reconnect & retry once if LDAP server went down */
	/* it sets the variable rv */
	if (rv == LDAPU_SUCCESS) {

	    LDAPU_REQ(rv, ldb, ldapu_cert_to_user(cert, ldb->ld, ldb->basedn,
						  &res, &uid));

	    if (rv == LDAPU_SUCCESS) {
		char *dn;

		*user = pool_strdup(pool, uid);
		if (!*user) rv = LDAPU_ERR_OUT_OF_MEMORY;
		free(uid);

		entry = ldap_first_entry(ldb->ld, res);
		dn = ldap_get_dn(ldb->ld, entry);
		if (acl_usr_cache_enabled()) {
		    acl_cert_cache_insert (cert, dbname, *user, dn, *req_time);
		}
		*userdn = dn ? pool_strdup(pool, dn) : 0;
		if (!*userdn) rv = LDAPU_ERR_OUT_OF_MEMORY;
		ldap_memfree(dn);
	    }
	    if (res) ldap_msgfree(res);
	}
	ldb_unlock_rwlock(ldb, ldb_rwlock);
    }
    else {
	rv = LDAPU_SUCCESS;
    }

    return rv;
}


/*
 * ACL_LDAPDatabaseHandle -
 *   Finds the internal structure representing the 'dbname'.  If it is an LDAP
 * database, returns the 'LDAP *ld' pointer.  Also, binds to the LDAP server.
 * The LDAP *ld handle can be used in calls to LDAP API.
 * Returns LAS_EVAL_TRUE if successful, otherwise logs an error in
 * LOG_SECURITY and returns LAS_EVAL_FAIL.
 */
int ACL_LDAPDatabaseHandle (NSErr_t *errp, const char *dbname, LDAP **ld,
			    char **basedn)
{
    int rv;
    ACLDbType_t dbtype;
    void *db;
    LDAPDatabase_t *ldb;

    *ld = 0;
    if (!dbname || !*dbname) dbname = DBCONF_DEFAULT_DBNAME;

    /* Check if the ldb is already in the ACLUserLdbHash */
    ldb = (LDAPDatabase_t *)PR_HashTableLookup(ACLUserLdbHash, dbname);

    if (!ldb) {

	rv = ACL_DatabaseFind(errp, dbname, &dbtype, &db);

	if (rv != LAS_EVAL_TRUE) {
	    nserrGenerate(errp, ACLERRINVAL, ACLERR6000, ACL_Program, 2, XP_GetAdminStr(DBT_LdapDatabaseHandleNotARegisteredDatabase), dbname);
	    return LAS_EVAL_FAIL;
	}

	if (!ACL_DbTypeIsEqual(errp, dbtype, ACL_DbTypeLdap)) {
	    /* Not an LDAP database -- error */
	    nserrGenerate(errp, ACLERRINVAL, ACLERR6010, ACL_Program, 2, XP_GetAdminStr(DBT_LdapDatabaseHandleNotAnLdapDatabase), dbname);
	    return LAS_EVAL_FAIL;
	}

	ldb = ldapu_copy_LDAPDatabase_t((LDAPDatabase_t *)db);

	if (!ldb) {
	    /* Not an LDAP database -- error */
	    nserrGenerate(errp, ACLERRNOMEM, ACLERR6020, ACL_Program, 1, XP_GetAdminStr(DBT_LdapDatabaseHandleOutOfMemory));
	    return LAS_EVAL_FAIL;
	}

	PR_HashTableAdd(ACLUserLdbHash, PERM_STRDUP(dbname), ldb);
    }

    if (!ldb->ld) {
	rv = ldapu_ldap_init_and_bind(ldb);

	if (rv != LDAPU_SUCCESS) {
	    nserrGenerate(errp, ACLERRFAIL, ACLERR6030, ACL_Program, 2, XP_GetAdminStr(DBT_LdapDatabaseHandleCouldntInitializeConnectionToLdap), ldapu_err2string(rv));
	    return LAS_EVAL_FAIL;
	}
    }

    /*
     * Force the rebind -- we don't know whether the customer has used this ld
     * to bind as somebody else.  It will also check if the LDAP server is up
     * and running, reestablish the connection if the LDAP server has rebooted
     * since it was last used.
     */
    rv = ldapu_ldap_rebind(ldb);

    if (rv != LDAPU_SUCCESS) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR6040, ACL_Program, 2, XP_GetAdminStr(DBT_LdapDatabaseHandleCouldntBindToLdapServer), ldapu_err2string(rv));
	return LAS_EVAL_FAIL;
    }

    *ld = ldb->ld;

    if (basedn) {
	/* They asked for the basedn too */
	*basedn = PERM_STRDUP(ldb->basedn);
    }

    return LAS_EVAL_TRUE;
}

int get_userdn_ldap (NSErr_t *errp, PList_t subject,
		     PList_t resource, PList_t auth_info,
		     PList_t global_auth, void *unused)
{
    char *uid;
    char *dbname;
    char *userdn;
    time_t *req_time = 0;
    pool_handle_t *subj_pool = PListGetPool(subject);
    int rv;
    
    rv = ACL_GetAttribute(errp, ACL_ATTR_USER, (void **)&uid, subject,
			  resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
	return LAS_EVAL_FAIL;
    }

    /* The getter for ACL_ATTR_USER may have put the USERDN on the PList */
    rv = PListGetValue(subject, ACL_ATTR_USERDN_INDEX, (void **)&userdn, NULL);

    if (rv >= 0) {
	/* Nothing to do */
	return LAS_EVAL_TRUE;
    }

    rv = ACL_AuthInfoGetDbname(auth_info, &dbname);

    if (rv < 0) {
	char rv_str[16];
	sprintf(rv_str, "%d", rv);
	nserrGenerate(errp, ACLERRFAIL, ACLERR5830, ACL_Program, 2,
		      XP_GetAdminStr(DBT_ldapaclUnableToGetDatabaseName), rv_str);
	return LAS_EVAL_FAIL;
    }

    /* Check if the userdn is available in the usr_cache */
    if (acl_usr_cache_enabled()) {
	/* avoid unnecessary system call to get time if cache is disabled */
	req_time = acl_get_req_time(resource);

	rv = acl_usr_cache_get_userdn(uid, dbname, *req_time, &userdn,
				      subj_pool);
    }
    else {
	rv = LAS_EVAL_FALSE;
    }

    if (rv == LAS_EVAL_TRUE) {
	/* Found in the cache */
	PListInitProp(subject, ACL_ATTR_USERDN_INDEX, ACL_ATTR_USERDN,
		      userdn, 0);
    }
    else {
	ACLDbType_t dbtype;
	LDAPDatabase_t *ldb = 0;

	/* Perform LDAP lookup */
	rv = ACL_DatabaseFind(errp, dbname, &dbtype, (void **)&ldb);

	if (rv != LAS_EVAL_TRUE) {
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5840, ACL_Program, 2,
			  XP_GetAdminStr(DBT_ldapaclUnableToGetParsedDatabaseName), dbname);
	    return rv;
	}

	ldb_read_rwlock(ldb, ldb_rwlock);
	rv = ldapu_ldap_init_and_bind(ldb);

	if (rv != LDAPU_SUCCESS) {
	    ldb_unlock_rwlock(ldb, ldb_rwlock);
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5850, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclCoudlntInitializeConnectionToLdap), ldapu_err2string(rv));
	    return LAS_EVAL_FAIL;
	}

	LDAPU_REQ(rv, ldb, ldapu_find_userdn(ldb->ld, uid, ldb->basedn,
					     &userdn));

	ldb_unlock_rwlock(ldb, ldb_rwlock);

	if (rv == LDAPU_SUCCESS) {
	    /* Found it.  Store it in the cache also. */
	    PListInitProp(subject, ACL_ATTR_USERDN_INDEX, ACL_ATTR_USERDN,
			  pool_strdup(subj_pool, userdn), 0);
	    if (acl_usr_cache_enabled()) {
		acl_usr_cache_set_userdn(uid, dbname, userdn, *req_time);
	    }
	    ldapu_free(userdn);
	    rv = LAS_EVAL_TRUE;
	}
	else if (rv == LDAPU_FAILED) {
	    /* Not found but not an error */
	    rv = LAS_EVAL_FALSE;
	}
	else {
	    /* some unexpected LDAP error */
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5860, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclPassworkCheckLdapError), ldapu_err2string(rv));
	    rv = LAS_EVAL_FAIL;
	}
    }

    return rv;
}

/* Attr getter for LDAP database to check if the user exists */
int get_user_exists_ldap (NSErr_t *errp, PList_t subject,
			  PList_t resource, PList_t auth_info,
			  PList_t global_auth, void *unused)
{
    int rv;
    char *user;
    char *userdn;

    /* See if the userdn is already available */
    rv = PListGetValue(subject, ACL_ATTR_USERDN_INDEX, (void **)&userdn, NULL);

    if (rv >= 0) {
	/* Check if the DN is still valid against the database */
	/* Get the database name */
	char *dbname;
	ACLDbType_t dbtype;
	LDAPDatabase_t *ldb = 0;
	LDAPMessage *res;
	const char *some_attrs[] = { "c", 0 };

	rv = ACL_AuthInfoGetDbname(auth_info, &dbname);

	if (rv < 0) {
	    char rv_str[16];
	    sprintf(rv_str, "%d", rv);
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5830, ACL_Program, 2,
			  XP_GetAdminStr(DBT_ldapaclUnableToGetDatabaseName), rv_str);
	    return LAS_EVAL_FAIL;
	}

	/* Perform LDAP lookup */
	rv = ACL_DatabaseFind(errp, dbname, &dbtype, (void **)&ldb);

	if (rv != LAS_EVAL_TRUE) {
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5840, ACL_Program, 2,
			  XP_GetAdminStr(DBT_ldapaclUnableToGetParsedDatabaseName), dbname);
	    return rv;
	}

	ldb_read_rwlock(ldb, ldb_rwlock);
	rv = ldapu_ldap_init_and_bind(ldb);

	if (rv != LDAPU_SUCCESS) {
	    ldb_unlock_rwlock(ldb, ldb_rwlock);
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5850, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclCoudlntInitializeConnectionToLdap), ldapu_err2string(rv));
	    return LAS_EVAL_FAIL;
	}

	LDAPU_REQ(rv, ldb, ldapu_find (ldb->ld, ldb->basedn, LDAP_SCOPE_BASE,
				       NULL, some_attrs, 1, &res));

	ldb_unlock_rwlock(ldb, ldb_rwlock);

	if (rv == LDAPU_SUCCESS) {
	    /* Found it. */
	    rv = LAS_EVAL_TRUE;
	}
	else if (rv == LDAPU_FAILED) {
	    /* Not found but not an error */
	    rv = LAS_EVAL_FALSE;
	}
	else {
	    /* some unexpected LDAP error */
	    nserrGenerate(errp, ACLERRFAIL, ACLERR5860, ACL_Program, 2, XP_GetAdminStr(DBT_ldapaclPassworkCheckLdapError), ldapu_err2string(rv));
	    rv = LAS_EVAL_FAIL;
	}
    }
    else {
	/* If the DN doesn't exist, should we just return an error ? */
	/* If yes, we don't need rest of the code */

	/* If we don't have a DN, we must have a user at least */
	rv = PListGetValue(subject, ACL_ATTR_USER_INDEX, (void **)&user, NULL);

	if (rv < 0) {
	    /* We don't even have a user name */
	    return LAS_EVAL_FAIL;
	}

	rv = ACL_GetAttribute(errp, ACL_ATTR_USERDN, (void **)&userdn, subject,
			      resource, auth_info, global_auth);
    }

    /* If we can get the userdn then the user exists */
    if (rv == LAS_EVAL_TRUE) {
	PListInitProp(subject, ACL_ATTR_USER_EXISTS_INDEX,
		      ACL_ATTR_USER_EXISTS, userdn, 0);
    }

    return rv;
}

/* acl_user_exists - */
/* Function to check if the user still exists */
/* This function works for all kinds of databases */
/* Returns 0 on success and -ve value on failure */
NSAPI_PUBLIC int acl_user_exists (const char *user, const char *userdn,
				  const char *dbname, const int logerr)
{
    NSErr_t     err = NSERRINIT;
    NSErr_t	*errp = &err;
    pool_handle_t *pool = 0;
    time_t *req_time = 0;
    PList_t subject = 0;
    PList_t resource = 0;
    PList_t auth_info = 0;
    PList_t global_auth = NULL;
    int rv;

    /* Check if the userdn is available in the usr_cache */
    if (acl_usr_cache_enabled() && userdn) {
	/* avoid unnecessary system call to get time if cache is disabled */
	req_time = (time_t *)MALLOC(sizeof(time_t));

	if (req_time) {
	    time(req_time);
	    rv = acl_usr_cache_userdn_check(user, dbname, userdn, *req_time);
	    FREE((void *)req_time);
	}

	if (rv == LAS_EVAL_TRUE)
	{
	    /* Found in the cache with the same DN */
	    return 0;
	}
    }

    pool = pool_create();
    subject = PListCreate(pool, ACL_ATTR_INDEX_MAX, 0, 0);
    resource = PListCreate(pool, ACL_ATTR_INDEX_MAX, 0, 0);
    auth_info = PListCreate(pool, ACL_ATTR_INDEX_MAX, 0, 0);
 
    if (!pool || !subject || !resource || !auth_info) {
	/* ran out of memory */
	goto no_mem;
    }

    /* store a pointer to the user rather than a copy */
    rv = PListInitProp(subject, ACL_ATTR_USER_INDEX, ACL_ATTR_USER,
		       user, 0);
    if (rv < 0) { /* Plist error */ goto plist_err; }

    if (userdn && *userdn) {
	/* store a pointer to the userdn rather than a copy */
	rv = PListInitProp(subject, ACL_ATTR_USERDN_INDEX, ACL_ATTR_USERDN,
			   userdn, 0);
	if (rv < 0) { /* Plist error */ goto plist_err; }
    }

    /* store the cached dbname on auth_info */
    rv = ACL_AuthInfoSetDbname(errp, auth_info, dbname);
    if (rv < 0) { /* auth_info error */ goto err; }

    rv = ACL_GetAttribute(errp, ACL_ATTR_USER_EXISTS, (void **)&user,
			  subject, resource, auth_info, global_auth);

    if (rv == LAS_EVAL_TRUE) {
	/* User still exists */
	rv = 0;
    }
    else if (rv == LAS_EVAL_FALSE) {
	/* User doesn't exist anymore */
	nserrGenerate(errp, ACLERRFAIL, 5880, ACL_Program, 2, XP_GetAdminStr(DBT_AclUserExistsNot), user);
	goto err;
    }
    else {
	/* Unexpected error while checking the existence of the user */
	goto err;
    }

    goto done;

plist_err:
    nserrGenerate(errp, ACLERRFAIL, 5890, ACL_Program, 1, XP_GetAdminStr(DBT_AclUserPlistError));
    goto err;

no_mem:
    nserrGenerate(errp, ACLERRNOMEM, 5870, ACL_Program, 1, XP_GetAdminStr(DBT_AclUserExistsOutOfMemory));
    goto err;

err:
    if (logerr) {
	/* Unexpected error while checking the existence of the user */
	char buf[BIG_LINE];
	/* generate error message (upto depth 6) into buf */
	aclErrorFmt(errp, buf, BIG_LINE, 6);
	ereport(LOG_SECURITY, "Error while checking the existence of user: %s", buf);
    }

    nserrDispose(errp);
    rv = -1;

done:
    /* Destroy the PLists & the pool */
    if (subject) PListDestroy(subject);
    if (resource) PListDestroy(resource);    
    if (auth_info) PListDestroy(auth_info);
    if (pool) pool_destroy(pool);
    return rv;
}
