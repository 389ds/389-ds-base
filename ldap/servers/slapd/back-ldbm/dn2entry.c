/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2022 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* dn2entry.c - given a dn return an entry */

#include "back-ldbm.h"

/*
 * Fetch the entry for this DN.
 *
 * Retuns NULL of the entry doesn't exist
 */
struct backentry *
dn2entry(
    Slapi_Backend *be,
    const Slapi_DN *sdn,
    back_txn *txn,
    int *err)
{
    return dn2entry_ext(be, sdn, txn, 0 /* flags */, err);
}

struct backentry *
dn2entry_ext(
    Slapi_Backend *be,
    const Slapi_DN *sdn,
    back_txn *txn,
    int flags,
    int *err)
{
    IDList *idl = NULL;
    ldbm_instance *inst;
    struct berval ndnv;
    struct backentry *e = NULL;
    char *indexname = "";

    slapi_log_err(SLAPI_LOG_TRACE, "dn2entry_ext", "=> \"%s\"\n", slapi_sdn_get_dn(sdn));

    inst = (ldbm_instance *)be->be_instance_info;

    *err = 0;
    ndnv.bv_val = (void *)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
    ndnv.bv_len = slapi_sdn_get_ndn_len(sdn);

    e = cache_find_dn(&inst->inst_cache, ndnv.bv_val, ndnv.bv_len);
    if (e == NULL) {
        ID id = ALLID;
        /* convert dn to entry id */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            *err = entryrdn_index_read_ext(be, sdn, &id,
                                           flags & TOMBSTONE_INCLUDED, txn);
            if (*err) {
                if (DB_NOTFOUND != *err) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "dn2entry_ext", "Failed to get id for %s "
                                                  "from entryrdn index (%d)\n",
                                  slapi_sdn_get_dn(sdn), *err);
                }
                /* There's no entry with this DN. */
                goto bail;
            }
            if (0 == id) {
                /*
                 * Note: A special entry such as RUV could be added
                 * to entryrdn even if the suffix does not exist in
                 * the index.  At that time, fake ID 0 is used as the
                 * parent id.
                 */
                /* There's no entry with this suffix. */
                goto bail;
            }
            indexname = LDBM_ENTRYRDN_STR;
        } else {
            if ((idl = index_read(be, LDBM_ENTRYDN_STR, indextype_EQUALITY,
                                  &ndnv, txn, err)) == NULL || idl->b_nids == 0) {
                /* There's no entry with this DN. */
                goto bail;
            }
            id = idl_firstid(idl);
            indexname = LDBM_ENTRYDN_STR;
        }
        /* convert entry id to entry */
        if ((e = id2entry(be, id, txn, err)) != NULL) {
            /* Means that we found the entry OK */
        } else {
            /* Hmm. The DN mapped onto an EntryID, but that didn't map onto an Entry. */
            if (*err != 0 && *err != DB_NOTFOUND) {
                /* JCM - Not sure if this is ever OK or not. */
            } else {
                /*
                 * this is pretty bad anyway. the dn was in the
                 * entrydn index, but we could not read the entry
                 * from the id2entry index. what should we do?
                 */
                slapi_log_err(SLAPI_LOG_ERR,
                              "dn2entry_ext", "The dn \"%s\" was in the %s index, "
                                              "but it did not exist in id2entry of instance %s.\n",
                              slapi_sdn_get_dn(sdn), indexname, inst->inst_name);
            }
        }
    }
bail:
    idl_free(&idl);
    slapi_log_err(SLAPI_LOG_TRACE, "dn2entry_ext", "<= %p\n", e);
    return (e);
}

/*
 * Use the DN to fetch the parent of the entry.
 * If the parent entry doesn't exist, keep working
 * up the DN until we hit "" or an backend suffix.
 *
 * ancestordn should be initialized before calling this function, and
 * should be empty
 *
 * Returns NULL for no entry found.
 *
 * When the caller is finished with the entry returned, it should return it
 * to the cache:
 *  e = dn2ancestor( ... );
 *  if ( NULL != e ) {
 *        cache_return( &inst->inst_cache, &e );
 *    }
 */
struct backentry *
dn2ancestor(
    Slapi_Backend *be,
    const Slapi_DN *sdn,
    Slapi_DN *ancestordn,
    back_txn *txn,
    int *err,
    int allow_suffix)
{
    struct backentry *e = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "dn2ancestor", "=> \"%s\"\n", slapi_sdn_get_dn(sdn));

    /* first, check to see if the given sdn is empty or a root suffix of the
       given backend - if so, it has no parent */
    if (!slapi_sdn_isempty(sdn) && !slapi_be_issuffix(be, sdn)) {
        Slapi_DN ancestorndn;
        const char *ptr;

        /* assign ancestordn to the parent of the given dn */
        ptr = slapi_dn_find_parent(slapi_sdn_get_dn(sdn));
        /* assign the ancestordn dn pointer to the parent of dn from sdn - sdn "owns"
           the memory, but ancestordn points to it */
        slapi_sdn_set_normdn_byref(ancestordn, ptr); /* free any previous contents */
        /* now, do the same for the normalized version */
        /* ancestorndn holds the normalized version for iteration purposes and
           because dn2entry needs the normalized dn */
        ptr = slapi_dn_find_parent(slapi_sdn_get_ndn(sdn));
        slapi_sdn_init_ndn_byref(&ancestorndn, ptr);

        /*
          At this point you may be wondering why I need both ancestorndn and
          ancestordn.  Because, with the slapi_sdn interface, you cannot set both
          the dn and ndn byref at the same time.  Whenever you call set_dn or set_ndn,
          it calls slapi_sdn_done which wipes out the previous contents.  I suppose I
          could have added another API to allow you to pass them both in.  Also, using
          slapi_sdn_get_ndn(ancestordn) every time would result in making a copy then
          normalizing the copy every time - not efficient.
          So, why not just use a char* for the ancestorndn?  Because dn2entry requires
          a Slapi_DN with the normalized dn.
        */

        /* stop when we get to "", or a backend suffix point */
        while (!e && !slapi_sdn_isempty(&ancestorndn)) {
            if (!allow_suffix) {
                /* Original behavior. */
                if (slapi_be_issuffix(be, &ancestorndn)) {
                    break;
                }
            }
            /* find the entry - it uses the ndn, so no further conversion is necessary */
            e = dn2entry(be, &ancestorndn, txn, err);
            if (!e) {
                /* not found, so set ancestordn to its parent and try again */
                ptr = slapi_dn_find_parent(slapi_sdn_get_ndn(&ancestorndn));
                /* keep in mind that ptr points to the raw ndn pointer inside
                   ancestorndn which is still the ndn string "owned" by sdn, the
                   original dn we started with - we are careful not to touch
                   or change it */
                slapi_sdn_set_ndn_byref(&ancestorndn, ptr); /* wipe out the previous contents */
                /* now do the same for the unnormalized one */
                ptr = slapi_dn_find_parent(slapi_sdn_get_dn(ancestordn));
                slapi_sdn_set_normdn_byref(ancestordn, ptr); /* wipe out the previous contents */
            }
        }

        slapi_sdn_done(&ancestorndn);
    }

    /* post conditions:
       e is the entry of the ancestor of sdn OR e is the suffix entry
       OR e is NULL
       ancestordn contains the unnormalized DN of e or is empty */
    slapi_log_err(SLAPI_LOG_TRACE, "dn2ancestor", "=> %p\n", e);
    return (e);
}

/*
 * Use uniqueid2entry or dn2entry to fetch an entry from the cache,
 * make a copy of it, and stash it in the pblock.
 */
int
get_copy_of_entry(Slapi_PBlock *pb, const entry_address *addr, back_txn *txn, int plock_parameter, int must_exist) /* JCM - Move somewhere more appropriate */
{
    int err = 0;
    int rc = LDAP_SUCCESS;
    backend *be;
    struct backentry *entry;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    if (addr->uniqueid != NULL) {
        entry = uniqueid2entry(be, addr->uniqueid, txn, &err);
    } else {
        if (addr->sdn) {
            entry = dn2entry(be, addr->sdn, txn, &err);
        } else {
            err = 1;
        }
    }
    if (0 != err && DB_NOTFOUND != err) {
        if (must_exist) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "get_copy_of_entry", "Operation error fetching %s (%s), error %d.\n",
                          addr->sdn ? slapi_sdn_get_dn(addr->sdn) : "Null DN",
                          (addr->uniqueid == NULL ? "null" : addr->uniqueid), err);
        }
        if (LDAP_INVALID_DN_SYNTAX == err) {
            rc = LDAP_INVALID_DN_SYNTAX; /* respect the error */
        } else {
            rc = LDAP_OPERATIONS_ERROR;
        }
    } else {
        /* If an entry is found, copy it into the PBlock. */
        if (entry != NULL) {
            ldbm_instance *inst;
            slapi_pblock_set(pb, plock_parameter, slapi_entry_dup(entry->ep_entry));
            inst = (ldbm_instance *)be->be_instance_info;
            CACHE_RETURN(&inst->inst_cache, &entry);
        }
    }
    return rc;
}

void
done_with_pblock_entry(Slapi_PBlock *pb, int plock_parameter) /* JCM - Move somewhere more appropriate */
{
    Slapi_Entry *entry;
    slapi_pblock_get(pb, plock_parameter, &entry);
    if (entry != NULL) {
        slapi_entry_free(entry);
        entry = NULL;
        slapi_pblock_set(pb, plock_parameter, entry);
    }
}
