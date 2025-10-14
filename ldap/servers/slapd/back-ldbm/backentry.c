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

/* backentry.c - wrapper routines to deal with entries */

#include "back-ldbm.h"

void
backentry_free(struct backentry **bep)
{
    struct backentry *ep;
    if (NULL == bep || NULL == *bep) {
        return;
    }
    ep = *bep;

    PR_ASSERT(ep->ep_state & (ENTRY_STATE_DELETED | ENTRY_STATE_NOTINCACHE | ENTRY_STATE_INVALID));
    if (ep->ep_entry != NULL) {
        slapi_entry_free(ep->ep_entry);
    }
    if (ep->ep_mutexp != NULL) {
        PR_DestroyMonitor(ep->ep_mutexp);
    }
    slapi_ch_free((void **)&ep);
    *bep = NULL;
}

struct backentry *
backentry_alloc()
{
    struct backentry *ec;
    ec = (struct backentry *)slapi_ch_calloc(1, sizeof(struct backentry));
    ec->ep_state = ENTRY_STATE_NOTINCACHE;
    ec->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
    ec->debug_sig = 0x45454545;
#endif
    return ec;
}

void
backentry_clear_entry(struct backentry *ep)
{
    if (ep) {
        ep->ep_entry = NULL;
    }
}

struct backentry *
backentry_init(Slapi_Entry *e)
{
    struct backentry *ep;

    ep = (struct backentry *)slapi_ch_calloc(1, sizeof(struct backentry));
    ep->ep_entry = e;
    ep->ep_state = ENTRY_STATE_NOTINCACHE;
    ep->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
    ep->debug_sig = 0x23232323;
#endif

    return (ep);
}

struct backentry *
backentry_dup(struct backentry *e)
{
    struct backentry *ec;

    if (NULL == e) {
        return NULL;
    }

    ec = (struct backentry *)slapi_ch_calloc(1, sizeof(struct backentry));
    ec->ep_id = e->ep_id;
    ec->ep_entry = slapi_entry_dup(e->ep_entry);
    ec->ep_state = ENTRY_STATE_NOTINCACHE;
    ec->ep_type = CACHE_TYPE_ENTRY;
#ifdef LDAP_CACHE_DEBUG
    ec->debug_sig = 0x12121212;
#endif

    return (ec);
}

char *
backentry_get_ndn(const struct backentry *e)
{
    return (char *)slapi_sdn_get_ndn(slapi_entry_get_sdn_const(e->ep_entry));
}

const Slapi_DN *
backentry_get_sdn(const struct backentry *e)
{
    return slapi_entry_get_sdn_const(e->ep_entry);
}

void
backdn_free(struct backdn **bdn)
{
    if (NULL == bdn || NULL == *bdn) {
        return;
    }
    slapi_sdn_free(&((*bdn)->dn_sdn));
    slapi_ch_free((void **)bdn);
    *bdn = NULL;
}

struct backdn *
backdn_init(Slapi_DN *sdn, ID id, int to_remove_from_hash)
{
    struct backdn *bdn;

    bdn = (struct backdn *)slapi_ch_calloc(1, sizeof(struct backdn));
    bdn->dn_sdn = sdn;
    bdn->ep_id = id;
    bdn->ep_size = slapi_sdn_get_size(sdn);
    bdn->ep_type = CACHE_TYPE_DN;
    if (!to_remove_from_hash) {
        bdn->ep_state = ENTRY_STATE_NOTINCACHE;
    }

    return (bdn);
}

void
backentry_init_weight(BackEntryWeightData *starttime)
{
    clock_gettime(CLOCK_MONOTONIC, starttime);
}

void
backentry_compute_weight(struct backentry *e, const BackEntryWeightData *starttime)
{
    struct timespec now = {0};
    struct timespec delta = {0};
    unsigned int nbmembers = 0;
    int n = 0;
    Slapi_Attr *a = NULL;

    clock_gettime(CLOCK_MONOTONIC, &now);
    slapi_timespec_diff(&now, (BackEntryWeightData*)starttime, &delta);
    e->ep_weight = delta.tv_sec * 1000000UL + delta.tv_nsec / 1000UL;
    if (e->ep_weight == 0) {
        /* Ensure that entries with very small weight can be distinguished
         * from those without any weight.
         */
        e->ep_weight = 1L;
    }
    /* Let count the number of members */
    if (e->ep_entry) {
        slapi_entry_attr_find(e->ep_entry, "uniquemember", &a);
        slapi_attr_get_numvalues(a, &n);
        nbmembers += (unsigned int)n;
        slapi_entry_attr_find(e->ep_entry, "member", &a);
        slapi_attr_get_numvalues(a, &n);
        nbmembers += (unsigned int)n;
    }
    /* Compute log²(nbmembers) */
    for (n=0; nbmembers>0; nbmembers>>=1) n++;
    /* And increase significantly the loading time for large groups
     * so that large groups weight get higer than standard entries one
     * (even if they got a CPU context switch while being loaded)
     */
    if ( n > 8 ) {
        e->ep_weight *= n;
    }
}
