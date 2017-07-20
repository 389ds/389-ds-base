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


#ifndef _FILTER_H_
#define _FILTER_H_

#include "slapi-plugin.h" /* struct berval, Slapi_PBlock, mrFilterMatchFn */

typedef Slapi_Attr *attr_ptr;

typedef int (*mrf_plugin_fn)(Slapi_PBlock *);

#define MRF_ANY_TYPE  1
#define MRF_ANY_VALUE 2

/*
 * To adjust the other structures in struct slapi_filter,
 * the first field must be type and the second must be value.
 */
typedef struct mr_filter_t
{
    char *mrf_type;
    struct berval mrf_value;
    char *mrf_oid;
    char mrf_dnAttrs;
    mrFilterMatchFn mrf_match;
    mrf_plugin_fn mrf_index;
    unsigned int mrf_reusable; /* MRF_ANY_xxx */
    mrf_plugin_fn mrf_reset;
    void *mrf_object; /* whatever the implementation needs */
    mrf_plugin_fn mrf_destroy;
} mr_filter_t;

#endif
