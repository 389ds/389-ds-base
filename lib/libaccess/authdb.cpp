/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>

#include <plhash.h>

#include <netsite.h>
#include "permhash.h"
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
	int prefix_len = strcspn(url, ":");
	char dbtypestr[BIG_LINE];

	if (prefix_len) {
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
#ifdef NSPR20
						  name
#else
						  (char *)name
#endif
						  );

	if (info) {
	    *dbtype = info->dbtype;
	    *db = info->dbinfo;

	    return LAS_EVAL_TRUE;
	}
    }

    return LAS_EVAL_FAIL;
}


NSAPI_PUBLIC int ACL_ReadDbMapFile (NSErr_t *errp, const char *map_file,
				    int default_only)
{
    DBConfInfo_t *info;
    DBConfDBInfo_t *db_info;
    DBPropVal_t *propval;
    PList_t plist;
    int rv;
    int seen_default = 0;

    if (default_only)
	rv = dbconf_read_default_dbinfo(map_file, &db_info);
    else
	rv = dbconf_read_config_file(map_file, &info);

    if (rv != LDAPU_SUCCESS) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4600, ACL_Program, 3, XP_GetAdminStr(DBT_ReadDbMapFileErrorReadingFile), map_file, ldapu_err2string(rv));
	return -1;
    }

    rv = 0;

    if (!default_only)
	db_info = info->firstdb;

    while(db_info) {
	char *url = db_info->url;
	char *dbname = db_info->dbname;
	ACLDbType_t dbtype;

	/* process db_info */
	if (url) {
	    rv = acl_url_to_dbtype(url, &dbtype);
	    
	    if (rv < 0) {
		nserrGenerate(errp, ACLERRFAIL, ACLERR4610, ACL_Program, 2,
		              XP_GetAdminStr(DBT_ReadDbMapFileCouldntDetermineDbtype), url);
		break;
	    }
	}
	else {
            nserrGenerate(errp, ACLERRFAIL, ACLERR4620, ACL_Program, 2,
	                  XP_GetAdminStr(DBT_ReadDbMapFileMissingUrl), dbname);
	    rv = -1;
	    break;
	}

	/* convert any property-value pairs in db_info into plist */
	plist = PListNew(NULL);
	propval = db_info->firstprop;

	while(propval) {
	    if (propval->prop) {
		PListInitProp(plist, 0, propval->prop, propval->val, 0);
	    }
	    else {
		nserrGenerate(errp, ACLERRINVAL, ACLERR4630, ACL_Program, 2,
		              XP_GetAdminStr(DBT_ReadDbMapFileInvalidPropertyPair), dbname);
		rv = -1;
		break;
	    }
	    propval = propval->next;
	}

	if (rv < 0) break;

	/* register the database */
	rv = ACL_DatabaseRegister(errp, dbtype, dbname, url, plist);
	PListDestroy(plist);

	if (rv < 0) {
	    /* Failed to register database */
	    nserrGenerate(errp, ACLERRFAIL, ACLERR4640, ACL_Program, 2,
	                  XP_GetAdminStr(DBT_ReadDbMapFileRegisterDatabaseFailed), dbname);
	    break;
	}

	/* If the dbname is "default", set the default_dbtype */
	if (!strcmp(dbname, DBCONF_DEFAULT_DBNAME)) {
	    if (!ACL_DbTypeIsEqual(errp, dbtype, ACL_DbTypeLdap)) {
		nserrGenerate(errp, ACLERRINVAL, ACLERR4350, ACL_Program, 1,
		              XP_GetAdminStr(DBT_ReadDbMapFileDefaultDatabaseNotLdap));
		rv = -1;
		break;
	    }
	    if (seen_default) {
		nserrGenerate(errp, ACLERRINVAL, ACLERR4360, ACL_Program, 1, XP_GetAdminStr(DBT_ReadDbMapFileMultipleDefaultDatabases));
		rv = -1;
		break;
	    }
	    seen_default = 1;
	    ACL_DatabaseSetDefault(errp, dbname);
	}

	db_info = db_info->next;
    }

    if (!seen_default) {
	nserrGenerate(errp, ACLERRINVAL, ACLERR4370, ACL_Program, 1, XP_GetAdminStr(DBT_ReadDbMapFileMissingDefaultDatabase));
	rv = -1;
    }

    if (default_only)
	dbconf_free_dbinfo(db_info);
    else
	dbconf_free_confinfo(info);

    return rv;
}

void 
ACL_DatabaseDestroy(void)
{
    pool_destroy(ACL_DATABASE_POOL);
    ACL_DATABASE_POOL = NULL;
    ACLDbNameHash = NULL;
    return;
}

