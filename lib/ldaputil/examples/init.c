/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "certmap.h"		/* Public Certmap API */
#include "plugin.h"		/* must define extern "C" functions */


NSAPI_PUBLIC int plugin_init_fn (void *certmap_info, const char *issuerName,
				 const char *issuerDN, const char *libname)
{
    static int initialized = 0;
    int rv;

    /* Make sure CertmapDLLInit is initialized only once */
    if (!initialized) {
#ifdef WIN32
	CertmapDLLInit(rv, libname);
	
	if (rv != LDAPU_SUCCESS) return rv;
#endif
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
