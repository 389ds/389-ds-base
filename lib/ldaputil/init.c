/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <string.h>
#include <prlink.h>
#include <prio.h>
#include <prprf.h>

/*#include "base/file.h"*/
#include "ldaputil/certmap.h"
/*#include "ldaputil/ldapdb.h"*/
#include "ldaputil/ldaputil.h"
#include "ldaputil/cert.h"
#include "ldaputil/errors.h"
#include "ldaputil/init.h"

#include "slapi-plugin.h"

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

static int
load_server_libs(const char *dir)
{
    int rv = LDAPU_SUCCESS;
    PRDir *ds;
    int suffix_len = strlen(DLL_SUFFIX);

    if ((ds = PR_OpenDir(dir)) != NULL) {
        PRDirEntry *d;

        /* Dir exists */
        while ((d = PR_ReadDir(ds, PR_SKIP_BOTH))) {
            PRLibrary *lib = 0;
            const char *libname = d->name;
            int len = strlen(libname);
            int is_lib;

            is_lib = (len > suffix_len && !strcmp(libname + len - suffix_len, DLL_SUFFIX));

            if (is_lib) {
                char path[1024];

                PR_snprintf(path, sizeof(path), "%s%c%s", dir, FILE_PATHSEP, libname);
                lib = PR_LoadLibrary(path);
                if (!lib)
                    rv = LDAPU_ERR_UNABLE_TO_LOAD_PLUGIN;
            }
        }
    } else {
        /* It's ok if dir doesn't exists */
    }

    return rv;
}

NSAPI_PUBLIC int
ldaputil_init(const char *config_file,
              const char *dllname,
              const char *serv_root,
              const char *serv_type,
              const char *serv_id)
{
    int rv = LDAPU_SUCCESS;
    static int initialized = 0;

    /* If already initialized, cleanup the old structures */
    if (initialized)
        ldaputil_exit();

    if (config_file && *config_file) {
        char dir[1024];

        LDAPUCertMapListInfo_t *certmap_list;
        LDAPUCertMapInfo_t *certmap_default;

        if (serv_root && *serv_root) {
            /* Load common libraries */
            PR_snprintf(dir, sizeof(dir), "%s%clib%c%s", serv_root, FILE_PATHSEP,
                        FILE_PATHSEP, "common");
            rv = load_server_libs(dir);

            if (rv != LDAPU_SUCCESS)
                return rv;

            if (serv_type && *serv_type) {
                /* Load server type specific libraries */
                sprintf(dir, "%s%clib%c%s", serv_root, FILE_PATHSEP,
                        FILE_PATHSEP, serv_type);
                rv = load_server_libs(dir);

                if (rv != LDAPU_SUCCESS)
                    return rv;

                if (serv_id && *serv_id) {
                    /* Load server instance specific libraries */
                    sprintf(dir, "%s%clib%c%s", serv_root, FILE_PATHSEP,
                            FILE_PATHSEP, serv_id);
                    rv = load_server_libs(dir);

                    if (rv != LDAPU_SUCCESS)
                        return rv;
                }
            }
        }

        rv = ldapu_certmap_init(config_file, dllname, &certmap_list,
                                &certmap_default);
    }

    initialized = 1;

    if (rv != LDAPU_SUCCESS)
        return rv;

    return rv;
}
