/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _FILTER_H_
#define _FILTER_H_

#include "slapi-plugin.h" /* struct berval, Slapi_PBlock, mrFilterMatchFn */

typedef Slapi_Attr* attr_ptr;

typedef int (*mrf_plugin_fn) (Slapi_PBlock*);

#define MRF_ANY_TYPE   1
#define MRF_ANY_VALUE  2

typedef struct mr_filter_t {
    char*         mrf_oid;
    char*         mrf_type;
    struct berval mrf_value;
    char          mrf_dnAttrs;
    struct slapdplugin* mrf_plugin;
    mrFilterMatchFn mrf_match;
    mrf_plugin_fn mrf_index;
    unsigned int  mrf_reusable; /* MRF_ANY_xxx */
    mrf_plugin_fn mrf_reset;
    void*         mrf_object; /* whatever the implementation needs */
    mrf_plugin_fn mrf_destroy;
} mr_filter_t;

#endif
