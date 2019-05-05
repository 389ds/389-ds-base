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


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <cert.h>
#include "certmap.h" /* Public Certmap API */
#include "plugin.h"  /* must define extern "C" functions */


NSAPI_PUBLIC int
plugin_init_fn(void *certmap_info, const char *issuerName, const CERTName *issuerDN, const char *libname)
{
    static int initialized = 0;
    int rv;

    /* Make sure CertmapDLLInit is initialized only once */
    if (!initialized) {

        initialized = 1;
    }

    fprintf(stderr, "plugin_init_fn called.\n");
    ldapu_set_cert_mapfn(issuerDN, plugin_mapping_fn);
    ldapu_set_cert_verifyfn(issuerDN, plugin_verify_fn);

    if (!default_searchfn)
        default_searchfn = ldapu_get_cert_searchfn(issuerDN);

    ldapu_set_cert_searchfn(issuerDN, plugin_search_fn);
    return LDAPU_SUCCESS;
}
