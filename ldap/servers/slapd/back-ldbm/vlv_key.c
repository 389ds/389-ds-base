/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
    struct vlv_key *p= (struct vlv_key*)slapi_ch_malloc(sizeof(struct vlv_key));
    p->keymem= 64;
    memset(&p->key,0,sizeof(DBT));
    p->key.data= slapi_ch_malloc(p->keymem);
    p->key.size= 0;
    return p;
}

void
vlv_key_delete(struct vlv_key **p)
{
    slapi_ch_free(&((*p)->key.data));
    slapi_ch_free((void **)p);
}

#if 0
static void
vlv_key_copy(const struct vlv_key *p1,struct vlv_key *p2)
{
    p2->keymem= p1->keymem;
    p2->key.data= slapi_ch_realloc(p2->key.data,p2->keymem);
    strcpy(p2->key.data, p1->key.data);
    p2->key.size= p1->key.size;
}
#endif

/*
 * Add an attribute value to the end of a composite key.
 */
void
vlv_key_addattr(struct vlv_key *p,struct berval *val)
{
    /* If there isn't room then allocate some more memory */
    unsigned int need = p->key.size + val->bv_len;
    if(need > p->keymem)
    {
        p->keymem*= 2;
        if(need > p->keymem)
        {
            p->keymem= need;
        }
        p->key.data= slapi_ch_realloc(p->key.data,p->keymem);
    }
    memcpy(((char*)p->key.data)+p->key.size, val->bv_val, val->bv_len);
    p->key.size+= val->bv_len;
}



