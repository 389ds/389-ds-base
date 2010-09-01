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


#include <stdio.h>
#include <string.h>

#include <plhash.h>

#include <netsite.h>
#include <ldaputil/errors.h>
#include <ldaputil/certmap.h>
#include <ldaputil/dbconf.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/authdb.h>
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include <libaccess/acl.h>
#include <libaccess/aclglobal.h>
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>

#define BIG_LINE 1024

char *ACL_default_dbname = 0;
ACLDbType_t ACL_default_dbtype = ACL_DBTYPE_INVALID;
ACLMethod_t ACL_default_method = ACL_METHOD_INVALID;
int acl_registered_dbcnt = 0;

extern int acl_registered_names(PLHashTable *ht, int count, char ***names);

/************************** Database Types *************************/

#define databaseNamesHashTable ACLDbNameHash

int acl_num_databases ()
{
    return acl_registered_dbcnt;
}

static int reg_dbname_internal (NSErr_t *errp, ACLDbType_t dbtype,
				const char *dbname, const char *url,
				PList_t plist)
{
    DbParseFn_t parseFunc;
    void *db;
    int rv;
    AuthdbInfo_t *authdb_info;

    if (!ACL_DbTypeIsRegistered(errp, dbtype)) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4400, ACL_Program, 2, XP_GetAdminStr(DBT_DbtypeNotDefinedYet), dbname);
	return -1;
    }
    
    parseFunc = ACL_DbTypeParseFn(errp, dbtype);

    if (!parseFunc) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4400, ACL_Program, 2, XP_GetAdminStr(DBT_DbtypeNotDefinedYet), dbname);
	return -1;
    }

    rv = (*parseFunc)(errp, dbtype, dbname, url, plist, (void **)&db);

    if (rv < 0) {
	/* plist contains error message/code */
	return rv;
    }

    /* Store the db returned by the parse function in the hash table.
     */

    authdb_info = (AuthdbInfo_t *)pool_malloc(ACL_DATABASE_POOL, sizeof(AuthdbInfo_t));
    
    if (!authdb_info) {
	nserrGenerate(errp, ACLERRNOMEM, ACLERR4420, ACL_Program, 0);
	return -1;
    }

    authdb_info->dbname = pool_strdup(ACL_DATABASE_POOL, dbname);
    authdb_info->dbtype = dbtype;
    authdb_info->dbinfo = db;	/* value returned from parseFunc */

    PR_HashTableAdd(ACLDbNameHash, authdb_info->dbname, authdb_info);
    acl_registered_dbcnt++;

    return 0;
}

NSAPI_PUBLIC int ACL_DatabaseRegister (NSErr_t *errp, ACLDbType_t dbtype,
				     const char *dbname, const char *url,
				     PList_t plist)
{
    if (!dbname || !*dbname) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4500, ACL_Program, 1, XP_GetAdminStr(DBT_DatabaseRegisterDatabaseNameMissing));
	return -1;
    }

    return reg_dbname_internal(errp, dbtype, dbname, url, plist);
}

NSAPI_PUBLIC int
ACL_DatabaseNamesGet(NSErr_t *errp, char ***names, int *count)
{
    *count = acl_registered_dbcnt;
    return acl_registered_names (ACLDbNameHash, *count, names);
}

NSAPI_PUBLIC int
ACL_DatabaseNamesFree(NSErr_t *errp, char **names, int count)
{
    int i;

    for (i = count-1; i; i--) FREE(names[i]);

    FREE(names);
    return 0;
}

/* try to determine the dbtype from the database url */
static int acl_url_to_dbtype (const char *url, ACLDbType_t *dbtype_out)
{
    ACLDbType_t dbtype;
    NSErr_t *errp = 0;

    *dbtype_out = dbtype = ACL_DBTYPE_INVALID;
    if (!url || !*url) return -1;

    // urls with ldap:, ldaps: and ldapdb: are all of type ACL_DBTYPE_LDAP.
    if (!strncmp(url, URL_PREFIX_LDAP, URL_PREFIX_LDAP_LEN))
	dbtype = ACL_DbTypeLdap;
    else {
	/* treat prefix in the url as dbtype if it has been registered.
	 */
	size_t prefix_len = strcspn(url, ":");
	char dbtypestr[BIG_LINE];

	if (prefix_len && (prefix_len < sizeof(dbtypestr))) {
	    strncpy(dbtypestr, url, prefix_len);
	    dbtypestr[prefix_len] = 0;

	    if (!ACL_DbTypeFind(errp, dbtypestr, &dbtype)) {
		/* prefix is not a registered dbtype */
		dbtype = ACL_DBTYPE_INVALID;
	    }
	}
    }

    if (ACL_DbTypeIsEqual(errp, dbtype, ACL_DBTYPE_INVALID)) {
	/* try all the registered parse functions to determine the dbtype */
    }

    if (ACL_DbTypeIsEqual(errp, dbtype, ACL_DBTYPE_INVALID)) return -1;

    *dbtype_out = dbtype;
    return 0;
}

NSAPI_PUBLIC int ACL_RegisterDbFromACL (NSErr_t *errp, const char *url,
					ACLDbType_t *dbtype)
{
    /* If the database by name url is already registered, don't do anything.
     * If it is not registered, determine the dbtype from the url.
     * If the dbtype can be determined, register the database with dbname same
     * as the url.  Return the dbtype.
     */
    void *db;
    int rv;
    PList_t plist;

    if (ACL_DatabaseFind(errp, url, dbtype, &db) == LAS_EVAL_TRUE)
	return 0;

    /* The database is not registered yet.  Parse the url to find out its
     * type.  If parsing fails, return failure.
     */
    rv = acl_url_to_dbtype(url, dbtype);
	
    if (rv < 0) {
	return rv;
    }

    plist = PListNew(NULL);
    rv = ACL_DatabaseRegister(errp, *dbtype, url, url, plist);
    PListDestroy(plist);
    return rv;
}

NSAPI_PUBLIC int ACL_DatabaseFind(NSErr_t *errp, const char *name,
				  ACLDbType_t *dbtype, void **db)
{
    AuthdbInfo_t *info;

    *dbtype = ACL_DBTYPE_INVALID;
    *db = 0;

    if (ACLDbNameHash) {
	info = (AuthdbInfo_t *)PR_HashTableLookup(ACLDbNameHash, 
						  name
						  );

	if (info) {
	    *dbtype = info->dbtype;
	    *db = info->dbinfo;

	    return LAS_EVAL_TRUE;
	}
    }

    return LAS_EVAL_FAIL;
}

void 
ACL_DatabaseDestroy(void)
{
    pool_destroy(ACL_DATABASE_POOL);
    ACL_DATABASE_POOL = NULL;
    ACLDbNameHash = NULL;
    return;
}

