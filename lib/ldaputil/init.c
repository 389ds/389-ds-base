/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <string.h>
#include <prlink.h>
#include <prio.h>

/*#include "base/file.h"*/
#include "ldaputil/certmap.h"
/*#include "ldaputil/ldapdb.h"*/
#include "ldaputil/ldaputil.h"
#include "ldaputil/cert.h"
#include "ldaputil/errors.h"
#include "ldaputil/init.h"

#ifdef XP_WIN32
#define DLL_SUFFIX ".dll"
#ifndef FILE_PATHSEP
#define FILE_PATHSEP '\\'
#endif
#else
#ifndef FILE_PATHSEP
#define FILE_PATHSEP '/'
#endif
#ifdef HPUX
#define DLL_SUFFIX ".sl"
#else
#define DLL_SUFFIX ".so"
#endif
#endif

static int load_server_libs (const char *dir)
{
    int rv = LDAPU_SUCCESS;
    PRDir* ds;
    int suffix_len = strlen(DLL_SUFFIX);

    if ((ds = PR_OpenDir(dir)) != NULL) {
	PRDirEntry *d;

	/* Dir exists */
        while( (d = PR_ReadDir(ds, PR_SKIP_BOTH)) )  {
	    PRLibrary *lib = 0;
            char *libname = d->name;
	    int len = strlen(libname);
	    int is_lib;

	    is_lib = (len > suffix_len && !strcmp(libname+len-suffix_len,
						  DLL_SUFFIX));

            if(is_lib) {
		char path[1024];

		sprintf(path, "%s%c%s", dir, FILE_PATHSEP, libname);
		lib = PR_LoadLibrary(path);
		if (!lib) rv = LDAPU_ERR_UNABLE_TO_LOAD_PLUGIN;
	    }
	}
    }
    else {
	/* It's ok if dir doesn't exists */
    }
    
    return rv;
}

NSAPI_PUBLIC int ldaputil_init (const char *config_file,
				const char *dllname,
				const char *serv_root,
				const char *serv_type,
				const char *serv_id)
{
    int rv = LDAPU_SUCCESS;
    static int initialized = 0;

    /* If already initialized, cleanup the old structures */
    if (initialized) ldaputil_exit();

    if (config_file && *config_file) {
	char dir[1024];

	LDAPUCertMapListInfo_t *certmap_list;
	LDAPUCertMapInfo_t *certmap_default;

	if (serv_root && *serv_root) {
	    /* Load common libraries */
	    sprintf(dir, "%s%clib%c%s", serv_root, FILE_PATHSEP,
		    FILE_PATHSEP, "common");
	    rv = load_server_libs(dir);

	    if (rv != LDAPU_SUCCESS) return rv;

	    if (serv_type && *serv_type) {
		/* Load server type specific libraries */
		sprintf(dir, "%s%clib%c%s", serv_root, FILE_PATHSEP,
			FILE_PATHSEP, serv_type);
		rv = load_server_libs(dir);

		if (rv != LDAPU_SUCCESS) return rv;

		if (serv_id && *serv_id) {
		    /* Load server instance specific libraries */
		    sprintf(dir, "%s%clib%c%s", serv_root, FILE_PATHSEP,
			    FILE_PATHSEP, serv_id);
		    rv = load_server_libs(dir);

		    if (rv != LDAPU_SUCCESS) return rv;
		}
	    }
	}

	rv = ldapu_certmap_init (config_file, dllname, &certmap_list,
				 &certmap_default);
    }

    initialized = 1;

    if (rv != LDAPU_SUCCESS) return rv;

    return rv;
}

static LDAPUDispatchVector_t __ldapu_vector = {
    ldapu_cert_to_ldap_entry,
    ldapu_set_cert_mapfn,
    ldapu_get_cert_mapfn,
    ldapu_set_cert_searchfn,
    ldapu_get_cert_searchfn,
    ldapu_set_cert_verifyfn,
    ldapu_get_cert_verifyfn,
    ldapu_get_cert_subject_dn,
    ldapu_get_cert_issuer_dn,
    ldapu_get_cert_ava_val,
    ldapu_free_cert_ava_val,
    ldapu_get_cert_der,
    ldapu_issuer_certinfo,
    ldapu_certmap_info_attrval,
    ldapu_err2string,
    ldapu_free_old,
    ldapu_malloc,
    ldapu_strdup,
    ldapu_free
};

#ifdef XP_UNIX
LDAPUDispatchVector_t *__ldapu_table = &__ldapu_vector;
#endif

#if 0
NSAPI_PUBLIC int CertMapDLLInitFn(LDAPUDispatchVector_t **table)
{
    *table = &__ldapu_vector;
}
#endif

NSAPI_PUBLIC int CertMapDLLInitFn(LDAPUDispatchVector_t **table)
{
    *table = (LDAPUDispatchVector_t *)malloc(sizeof(LDAPUDispatchVector_t));

    if (!*table) return LDAPU_ERR_OUT_OF_MEMORY;

    (*table)->f_ldapu_cert_to_ldap_entry = ldapu_cert_to_ldap_entry;
    (*table)->f_ldapu_set_cert_mapfn = ldapu_set_cert_mapfn;
    (*table)->f_ldapu_get_cert_mapfn = ldapu_get_cert_mapfn;
    (*table)->f_ldapu_set_cert_searchfn = ldapu_set_cert_searchfn;
    (*table)->f_ldapu_get_cert_searchfn = ldapu_get_cert_searchfn;
    (*table)->f_ldapu_set_cert_verifyfn = ldapu_set_cert_verifyfn;
    (*table)->f_ldapu_get_cert_verifyfn = ldapu_get_cert_verifyfn;
    (*table)->f_ldapu_get_cert_subject_dn = ldapu_get_cert_subject_dn;
    (*table)->f_ldapu_get_cert_issuer_dn = ldapu_get_cert_issuer_dn;
    (*table)->f_ldapu_get_cert_ava_val = ldapu_get_cert_ava_val;
    (*table)->f_ldapu_free_cert_ava_val = ldapu_free_cert_ava_val;
    (*table)->f_ldapu_get_cert_der = ldapu_get_cert_der;
    (*table)->f_ldapu_issuer_certinfo = ldapu_issuer_certinfo;
    (*table)->f_ldapu_certmap_info_attrval = ldapu_certmap_info_attrval;
    (*table)->f_ldapu_err2string = ldapu_err2string;
    (*table)->f_ldapu_free_old = ldapu_free_old;
    (*table)->f_ldapu_malloc = ldapu_malloc;
    (*table)->f_ldapu_strdup = ldapu_strdup;
    (*table)->f_ldapu_free = ldapu_free;
    return LDAPU_SUCCESS;
}
