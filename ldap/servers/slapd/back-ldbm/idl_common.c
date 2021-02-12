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

/* Common IDL code, used in both old and new indexing schemes */

#include "back-ldbm.h"

size_t
idl_sizeof(IDList *idl)
{
    if (NULL == idl) {
        return 0;
    }
    return sizeof(IDList) + (idl->b_nmax * sizeof(ID));
}

NIDS
idl_length(IDList *idl)
{
    if (NULL == idl) {
        return 0;
    }
    return (idl->b_nmax == ALLIDSBLOCK) ? UINT_MAX : idl->b_nids;
}

int
idl_is_allids(IDList *idl)
{
    if (NULL == idl) {
        return 0;
    }
    return (idl->b_nmax == ALLIDSBLOCK);
}

IDList *
idl_alloc(NIDS nids)
{
    IDList *new;

    if (nids == 0) {
        /*
         * Because we use allids in b_nmax as 0, when we request an
         * empty set, this *looks* like an empty set. Instead, we make
         * a minimum b_nmax to trick it.
         */
        nids = 1;
    }

    /* nmax + nids + space for the ids */
    new = (IDList *)slapi_ch_calloc(1, sizeof(IDList) + (sizeof(ID) * nids));
    new->b_nmax = nids;
    /* new->b_nids = 0; */

    return (new);
}

IDList *
idl_allids(backend *be)
{
    IDList *idl;

    idl = idl_alloc(0);
    idl->b_nmax = ALLIDSBLOCK;
    idl->b_nids = next_id_get(be);

    return (idl);
}

void
idl_free(IDList **idl)
{
    if ((NULL == idl) || (NULL == *idl)) {
        return;
    }

    slapi_ch_free((void **)idl);
}


/*
 * idl_append - append an id to an id list.
 *
 * Warning: The ID List must be maintained in order.
 * Use idl_insert if the id may not
 *
 * returns
 *    0 - appended
 *    1 - already in there
 *    2 - not enough room
 */

int
idl_append(IDList *idl, ID id)
{
    if (NULL == idl) {
        return 2;
    }
    if (ALLIDS(idl) || ((idl->b_nids) && (idl->b_ids[idl->b_nids - 1] == id))) {
        return (1); /* already there */
    }

    if (idl->b_nids == idl->b_nmax) {
        return (2); /* not enough room */
    }

    idl->b_ids[idl->b_nids] = id;
    idl->b_nids++;

    return (0);
}

/* Append an ID to an IDL, realloc-ing the space if needs be */
/* ID presented is not to be already in the IDL. */
/* moved from idl_new.c */
int
idl_append_extend(IDList **orig_idl, ID id)
{
    IDList *idl = *orig_idl;

    if (idl == NULL) {
        idl = idl_alloc(IDLIST_MIN_BLOCK_SIZE); /* used to be 0 */
        idl_append(idl, id);

        *orig_idl = idl;
        return 0;
    }

    if (idl->b_nids == idl->b_nmax) {
        /* No more room, need to extend */
        idl->b_nmax = idl->b_nmax * 2;
        idl = (IDList *)slapi_ch_realloc((char *)idl, sizeof(IDList) + (sizeof(ID) * idl->b_nmax));
        if (idl == NULL) {
            return ENOMEM;
        }
    }

    idl->b_ids[idl->b_nids] = id;
    idl->b_nids++;
    *orig_idl = idl;

    return 0;
}

static IDList *
idl_dup(IDList *idl)
{
    IDList *new;

    if (idl == NULL) {
        return (NULL);
    }

    new = idl_alloc(idl->b_nmax);
    memcpy(new, idl, idl_sizeof(idl));

    return (new);
}

static IDList *
idl_min(IDList *a, IDList *b)
{
    return (a->b_nids > b->b_nids ? b : a);
}

int
idl_id_is_in_idlist(IDList *idl, ID id)
{
    if (NULL == idl || NOID == id) {
        return 0; /* not in the list */
    }
    if (ALLIDS(idl)) {
        return 1; /* in the list */
    }

    for (NIDS i = 0; i < idl->b_nids; i++) {
        if (id == idl->b_ids[i]) {
            return 1; /* in the list */
        }
    }
    return 0; /* not in the list */
}

/*
 * idl_compare - compare idl a and b for value equality and ordering.
 */
int64_t
idl_compare(IDList *a, IDList *b)
{
    /* Assert they are not none. */
    if (a == NULL || b == NULL) {
        return 1;
    }
    /* Are they the same pointer? */
    if (a == b) {
        return 0;
    }
    /* Do they have the same number of IDs? */
    if (a->b_nids != b->b_nids) {
        return 1;
    }

    /* Are they both allid blocks? */
    if (ALLIDS(a) && ALLIDS(b)) {
        return 0;
    }

    /* Same size, and not the same array. Lets check! */
    for (size_t i = 0; i < a->b_nids; i++) {
        if (a->b_ids[i] != b->b_ids[i]) {
            return 1;
        }
    }
    /* Must be the same! */
    return 0;
}

/*
 * idl_intersection - return a intersection b
 */
IDList *
idl_intersection(
    backend *be,
    IDList *a,
    IDList *b)
{
    NIDS ai, bi, ni;
    IDList *n;

    if (a == NULL || a->b_nids == 0) {
        return idl_dup(a);
    }

    if (b == NULL || b->b_nids == 0) {
        return idl_dup(b);
    }

    if (ALLIDS(a)) {
        slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
        return (idl_dup(b));
    }
    if (ALLIDS(b)) {
        slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
        return (idl_dup(a));
    }

    n = idl_dup(idl_min(a, b));

    for (ni = 0, ai = 0, bi = 0; ai < a->b_nids; ai++) {
        for (; bi < b->b_nids && b->b_ids[bi] < a->b_ids[ai]; bi++)
            ; /* NULL */

        if (bi == b->b_nids) {
            break;
        }

        if (b->b_ids[bi] == a->b_ids[ai]) {
            n->b_ids[ni++] = a->b_ids[ai];
        }
    }

    n->b_nids = ni;

    return (n);
}

/*
 * idl_union - return a union b
 */

IDList *
idl_union(
    backend *be,
    IDList *a,
    IDList *b)
{
    NIDS ai, bi, ni;
    IDList *n;

    if (a == NULL || a->b_nids == 0) {
        return (idl_dup(b));
    }
    if (b == NULL || b->b_nids == 0) {
        return (idl_dup(a));
    }
    if (ALLIDS(a) || ALLIDS(b)) {
        return (idl_allids(be));
    }

    if (b->b_nids < a->b_nids) {
        n = a;
        a = b;
        b = n;
    }

    n = idl_alloc(a->b_nids + b->b_nids);

    for (ni = 0, ai = 0, bi = 0; ai < a->b_nids && bi < b->b_nids;) {
        if (a->b_ids[ai] < b->b_ids[bi]) {
            n->b_ids[ni++] = a->b_ids[ai++];
        } else if (b->b_ids[bi] < a->b_ids[ai]) {
            n->b_ids[ni++] = b->b_ids[bi++];
        } else {
            n->b_ids[ni++] = a->b_ids[ai];
            ai++, bi++;
        }
    }

    for (; ai < a->b_nids; ai++) {
        n->b_ids[ni++] = a->b_ids[ai];
    }
    for (; bi < b->b_nids; bi++) {
        n->b_ids[ni++] = b->b_ids[bi];
    }
    n->b_nids = ni;

    return (n);
}

/*
 * idl_notin - return a intersection ~b (or a minus b)
 * dbi_db_t --- changed the interface of this function (no code called it),
 * such that it can modify IDL a in place (it'll always be the same
 * or smaller than the a passed in if not allids).
 * If a new list is generated, it's returned in new_result and the function
 * returns 1. Otherwise the result remains in a, and the function returns 0.
 * The intention is to optimize for the interesting case in filterindex.c
 * where we are computing foo AND NOT bar, and both foo and bar are not allids.
 */

int
idl_notin(
    backend *be,
    IDList *a,
    IDList *b,
    IDList **new_result)
{
    NIDS ni, ai, bi;
    IDList *n;
    *new_result = NULL;

    /* Nothing in a, so nothing to subtract from. */
    if (a == NULL || a->b_nids == 0) {
        *new_result = idl_alloc(0);
        return 1;
    }
    /* b is empty, so nothing to remove from a. */
    if (b == NULL || b->b_nids == 0) {
        return 0;
    }

    /* b is allIDS, so a - b, should be the empty set.
     * but if the type in unindexed, we don't know. Instead we have to
     * return a, and mark that we can't skip the filter test.
     */
    if (ALLIDS(b)) {
        slapi_be_set_flag(be, SLAPI_BE_FLAG_DONT_BYPASS_FILTERTEST);
        return 0;
    }

    if (ALLIDS(a)) { /* Not convinced that this code is really worth it */
        /* It's trying to do allids notin b, where maxid is smaller than some size */
        n = idl_alloc(SLAPD_LDBM_MIN_MAXIDS);
        ni = 0;

        for (ai = 1, bi = 0; ai < a->b_nids && ni < n->b_nmax &&
                             bi < b->b_nmax;
             ai++) {
            if (b->b_ids[bi] == ai) {
                bi++;
            } else {
                n->b_ids[ni++] = ai;
            }
        }

        for (; ai < a->b_nids && ni < n->b_nmax; ai++) {
            n->b_ids[ni++] = ai;
        }

        if (ni == n->b_nmax) {
            idl_free(&n);
            *new_result = idl_allids(be);
        } else {
            n->b_nids = ni;
            *new_result = n;
        }
        return (1);
    }

    /* This is the case we're interested in, we want to detect where a and b don't overlap */
    {
        size_t ahii, aloi, bhii, bloi;
        size_t ahi, alo, bhi, blo;
        int aloblo, ahiblo, alobhi, ahibhi;

        aloi = bloi = 0;
        ahii = a->b_nids - 1;
        bhii = b->b_nids - 1;

        ahi = a->b_ids[ahii];
        alo = a->b_ids[aloi];
        bhi = b->b_ids[bhii];
        blo = b->b_ids[bloi];
        /* if the ranges don't overlap, we're done, current a is the result */
        aloblo = alo < blo;
        ahiblo = ahi < blo;
        alobhi = ahi > bhi;
        ahibhi = alo > bhi;
        if ((aloblo & ahiblo) || (alobhi & ahibhi)) {
            return 0;
        } else {
            /* Do what we did before */
            n = idl_dup(a);

            ni = 0;
            for (ai = 0, bi = 0; ai < a->b_nids; ai++) {
                for (; bi < b->b_nids && b->b_ids[bi] < a->b_ids[ai];
                     bi++) {
                    ; /* NULL */
                }

                if (bi == b->b_nids) {
                    break;
                }

                if (b->b_ids[bi] != a->b_ids[ai]) {
                    n->b_ids[ni++] = a->b_ids[ai];
                }
            }

            for (; ai < a->b_nids; ai++) {
                n->b_ids[ni++] = a->b_ids[ai];
            }
            n->b_nids = ni;

            *new_result = n;
            return (1);
        }
    }
}

ID
idl_firstid(IDList *idl)
{
    if (idl == NULL || idl->b_nids == 0) {
        return (NOID);
    }

    if (ALLIDS(idl)) {
        return (idl->b_nids == 1 ? NOID : 1);
    }

    return (idl->b_ids[0]);
}

ID
idl_nextid(IDList *idl, ID id)
{
    NIDS i;

    if (NULL == idl || idl->b_nids == 0) {
        return NOID;
    }
    if (ALLIDS(idl)) {
        return (++id < idl->b_nids ? id : NOID);
    }

    for (i = 0; i < idl->b_nids && idl->b_ids[i] < id; i++) {
        ; /* NULL */
    }
    i++;

    if (i >= idl->b_nids) {
        return (NOID);
    } else {
        return (idl->b_ids[i]);
    }
}

/* Make an ID list iterator */
idl_iterator
idl_iterator_init(const IDList *idl __attribute__((unused)))
{
    return (idl_iterator)0;
}

idl_iterator
idl_iterator_increment(idl_iterator *i)
{
    size_t t = (size_t)*i;
    t += 1;
    *i = (idl_iterator)t;
    return *i;
}

idl_iterator
idl_iterator_decrement(idl_iterator *i)
{
    size_t t = (size_t)*i;
    if (t > 0) {
        t -= 1;
    }
    *i = (idl_iterator)t;
    return *i;
}

ID
idl_iterator_dereference(idl_iterator i, const IDList *idl)
{
    /*
     * NOID is used to terminate iteration. When we get an allIDS
     * idl->b_nids == number of entries in id2entry. That's how we
     * know to stop returning ids.
     */
    if ((NULL == idl) || (i >= idl->b_nids)) {
        return NOID;
    }
    if (ALLIDS(idl)) {
        /*
         * entries in id2entry start at 1, not 0, so we have off by one here.
         */
        return (ID)i + 1;
    } else {
        return idl->b_ids[i];
    }
}

ID
idl_iterator_dereference_increment(idl_iterator *i, const IDList *idl)
{
    ID t = idl_iterator_dereference(*i, idl);
    idl_iterator_increment(i);
    return t;
}

ID
idl_iterator_dereference_decrement(idl_iterator *i, const IDList *idl)
{
    idl_iterator_decrement(i);
    return idl_iterator_dereference(*i, idl);
}
