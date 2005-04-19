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
#ifdef __ia64
#define DLL_SUFFIX ".so"
#else
#define DLL_SUFFIX ".sl"
#endif
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
		const char *libname = d->name;
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
