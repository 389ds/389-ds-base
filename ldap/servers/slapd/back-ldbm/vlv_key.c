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

/* vlv_key.c */


#include "back-ldbm.h"
#include "vlv_key.h"

/*
 * These functions manipulate keys for the virtual list view indexes.
 * A key consists of a string of attribute values concatinated together,
 * plus an entry DN to ensure uniqueness.
 */

struct vlv_key *
vlv_key_new()
{
    struct vlv_key *p = (struct vlv_key *)slapi_ch_malloc(sizeof(struct vlv_key));
    p->keymem = 64;
    memset(&p->key, 0, sizeof(DBT));
    p->key.data = slapi_ch_malloc(p->keymem);
    p->key.size = 0;
    return p;
}

void
vlv_key_delete(struct vlv_key **p)
{
    slapi_ch_free(&((*p)->key.data));
    slapi_ch_free((void **)p);
}

/*
 * Add an attribute value to the end of a composite key.
 */
void
vlv_key_addattr(struct vlv_key *p, struct berval *val)
{
    /* If there isn't room then allocate some more memory */
    unsigned int need = p->key.size + val->bv_len;
    if (need > p->keymem) {
        p->keymem *= 2;
        if (need > p->keymem) {
            p->keymem = need;
        }
        p->key.data = slapi_ch_realloc(p->key.data, p->keymem);
    }
    memcpy(((char *)p->key.data) + p->key.size, val->bv_val, val->bv_len);
    p->key.size += val->bv_len;
}
