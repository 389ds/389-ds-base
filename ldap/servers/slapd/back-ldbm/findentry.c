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

/* findentry.c - find a database entry, obeying referrals (& aliases?) */

#include "back-ldbm.h"


static struct backentry *find_entry_internal_dn(Slapi_PBlock *pb, backend *be, const Slapi_DN *sdn, int lock, back_txn *txn, int flags, int *rc);
static struct backentry *find_entry_internal(Slapi_PBlock *pb, Slapi_Backend *be, const entry_address *addr, int lock, back_txn *txn, int flags, int *rc);
/* The flags take these values */
#define FE_TOMBSTONE_INCLUDED TOMBSTONE_INCLUDED /* :1 defined in back-ldbm.h */
#define FE_REALLY_INTERNAL 0x2

int
check_entry_for_referral(Slapi_PBlock *pb, Slapi_Entry *entry, char *matched, const char *callingfn) /* JCM - Move somewhere more appropriate */
{
    int rc = 0, i = 0, numValues = 0;
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;
    struct berval **refscopy = NULL;
    struct berval **url = NULL;

    /* if the entry is a referral send the referral */
    if (!slapi_entry_flag_is_set(entry, SLAPI_ENTRY_FLAG_REFERRAL)) {
        return 0;
    }

    if (slapi_entry_attr_find(entry, "ref", &attr)) {
        // ref attribute not found (should not happen)
        PR_ASSERT(0);
        goto out;
    }

    slapi_attr_get_numvalues(attr, &numValues);
    if (numValues == 0) {
        // ref attribute is empty
        goto out;
    }

    url = (struct berval **)slapi_ch_malloc((numValues + 1) * sizeof(struct berval *));
    if (!url) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "check_entry_for_referral", "Out of memory\n");
        goto out;
    }

    for (i = slapi_attr_first_value(attr, &val); i != -1;
         i = slapi_attr_next_value(attr, i, &val)) {
        url[i] = (struct berval *)slapi_value_get_berval(val);
    }
    url[numValues] = NULL;

    refscopy = ref_adjust(pb, url, slapi_entry_get_sdn(entry), 0); /* JCM - What's this PBlock* for? */
    slapi_send_ldap_result(pb, LDAP_REFERRAL, matched, NULL, 0, refscopy);
    rc = 1;

    slapi_log_err(SLAPI_LOG_TRACE, "check_entry_for_referral",
                  "<= %s sent referral to (%s) for (%s)\n",
                  callingfn,
                  refscopy ? refscopy[0]->bv_val : "",
                  slapi_entry_get_dn(entry));
out:
    if (refscopy != NULL) {
        ber_bvecfree(refscopy);
    }
    if (url != NULL) {
        slapi_ch_free((void **)&url);
    }
    return rc;
}

static struct backentry *
find_entry_internal_dn(
    Slapi_PBlock *pb,
    backend *be,
    const Slapi_DN *sdn,
    int lock,
    back_txn *txn,
    int flags,
    int *rc /* return code */
    )
{
    struct backentry *e;
    int managedsait = 0;
    int err;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    size_t tries = 0;
    int isroot = 0;
    int op_type;

    /* get the managedsait ldap message control */
    slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);

    while ((tries < LDBM_CACHE_RETRY_COUNT) &&
           (e = dn2entry_ext(be, sdn, txn, flags & TOMBSTONE_INCLUDED, &err)) != NULL) {
        /*
         * we found the entry. if the managedsait control is set,
         * we return the entry. if managedsait is not set, we check
         * for the presence of a ref attribute, returning to the
         * client a referral to the ref'ed entry if a ref is present,
         * returning the entry to the caller if not.
         */
        if (!managedsait && !(flags & FE_REALLY_INTERNAL)) {
            /* see if the entry is a referral */
            if (check_entry_for_referral(pb, e->ep_entry, NULL, "find_entry_internal_dn")) {
                CACHE_RETURN(&inst->inst_cache, &e);
                if (rc) { /* if check_entry_for_referral returns non-zero, result is sent. */
                    *rc = FE_RC_SENT_RESULT;
                }
                return (NULL);
            }
        }

        /*
         * we'd like to return the entry. lock it if requested,
         * retrying if necessary.
         */

        /* wait for entry modify lock */
        if (!lock || cache_lock_entry(&inst->inst_cache, e) == 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "find_entry_internal_dn",
                          "<= found (%s)\n", slapi_sdn_get_dn(sdn));
            return (e);
        }
        /*
         * this entry has been deleted - see if it was actually
         * replaced with a new copy, and try the whole thing again.
         */
        slapi_log_err(SLAPI_LOG_ARGS, "find_entry_internal_dn",
                      "   Retrying (%s)\n", slapi_sdn_get_dn(sdn));
        CACHE_RETURN(&inst->inst_cache, &e);
        tries++;
    }
    if (tries >= LDBM_CACHE_RETRY_COUNT) {
        slapi_log_err(SLAPI_LOG_ERR, "find_entry_internal_dn", "Retry count exceeded (%s)\n", slapi_sdn_get_dn(sdn));
    }
    /*
     * there is no such entry in this server. see how far we
     * can match, and check if that entry contains a referral.
     * if it does and managedsait is not set, we return the
     * referral to the client. if it doesn't, or managedsait
     * is set, we return no such object.
     */
    if (!(flags & FE_REALLY_INTERNAL)) {
        struct backentry *me;
        Slapi_DN ancestorsdn;
        slapi_sdn_init(&ancestorsdn);
        Slapi_Backend *pb_backend;
        slapi_pblock_get(pb, SLAPI_BACKEND, &pb_backend);

        me = dn2ancestor(pb_backend, sdn, &ancestorsdn, txn, &err, 1 /* allow_suffix */);
        if (!managedsait && me != NULL) {
            /* if the entry is a referral send the referral */
            if (check_entry_for_referral(pb, me->ep_entry, (char *)slapi_sdn_get_dn(&ancestorsdn), "find_entry_internal_dn")) {
                CACHE_RETURN(&inst->inst_cache, &me);
                slapi_sdn_done(&ancestorsdn);
                if (rc) { /* if check_entry_for_referral returns non-zero, result is sent. */
                    *rc = FE_RC_SENT_RESULT;
                }
                return (NULL);
            }
            /* else fall through to no such object */
        }

        /* entry not found */
        if ((0 == err) || (DBI_RC_NOTFOUND == err)) {
            if (me && !isroot) {
                /* If not root, you may not want to reveal it. */
                int acl_type = -1;
                int return_err = LDAP_NO_SUCH_OBJECT;
                err = LDAP_SUCCESS;
                switch (op_type) {
                case SLAPI_OPERATION_ADD:
                    acl_type = SLAPI_ACL_ADD;
                    return_err = LDAP_INSUFFICIENT_ACCESS;
                    break;
                case SLAPI_OPERATION_DELETE:
                    acl_type = SLAPI_ACL_DELETE;
                    return_err = LDAP_INSUFFICIENT_ACCESS;
                    break;
                case SLAPI_OPERATION_MODDN:
                    acl_type = SLAPI_ACL_MODDN;
                    return_err = LDAP_INSUFFICIENT_ACCESS;
                    break;
                case SLAPI_OPERATION_MODIFY:
                    acl_type = SLAPI_ACL_WRITE;
                    return_err = LDAP_INSUFFICIENT_ACCESS;
                    break;
                case SLAPI_OPERATION_SEARCH:
                case SLAPI_OPERATION_COMPARE:
                    return_err = LDAP_SUCCESS;
                    acl_type = SLAPI_ACL_READ;
                    break;
                case SLAPI_OPERATION_BIND:
                    acl_type = -1; /* skip acl check. acl is not set up for bind. */
                    return_err = LDAP_INVALID_CREDENTIALS;
                    slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, "No such entry");
                    break;
                }
                if (acl_type > 0) {
                    char *dummy_attr = "1.1";
                    err = slapi_access_allowed(pb, me->ep_entry, dummy_attr, NULL, acl_type);
                }
                if (((acl_type > 0) && err) || (op_type == SLAPI_OPERATION_BIND)) {
                    /*
                     * Operations to be checked && ACL returns disallow.
                     * Not to disclose the info about the entry's existence,
                     * do not return the "matched" DN.
                     * Plus, the bind case returns LDAP_INAPPROPRIATE_AUTH.
                     */
                    slapi_send_ldap_result(pb, return_err, NULL, NULL, 0, NULL);
                } else {
                    slapi_send_ldap_result(pb, LDAP_NO_SUCH_OBJECT,
                                           (char *)slapi_sdn_get_dn(&ancestorsdn), NULL, 0, NULL);
                }
            } else {
                slapi_send_ldap_result(pb, LDAP_NO_SUCH_OBJECT,
                                       (char *)slapi_sdn_get_dn(&ancestorsdn), NULL, 0, NULL);
            }
        } else {
            slapi_send_ldap_result(pb, (LDAP_INVALID_DN_SYNTAX == err) ? LDAP_INVALID_DN_SYNTAX : LDAP_OPERATIONS_ERROR,
                                   (char *)slapi_sdn_get_dn(&ancestorsdn), NULL, 0, NULL);
        }
        if (rc) {
            *rc = FE_RC_SENT_RESULT;
        }
        slapi_sdn_done(&ancestorsdn);
        CACHE_RETURN(&inst->inst_cache, &me);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "find_entry_internal_dn", "<= Not found (%s)\n",
                  slapi_sdn_get_dn(sdn));
    return (NULL);
}

/* Note that this function does not issue any referals.
   It should only be called in case of 5.0 replicated operation
   which should not be referred.
 */
static struct backentry *
find_entry_internal_uniqueid(
    Slapi_PBlock *pb,
    backend *be,
    const char *uniqueid,
    int lock,
    back_txn *txn)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    struct backentry *e;
    int err;
    size_t tries = 0;

    while ((tries < LDBM_CACHE_RETRY_COUNT) &&
           (e = uniqueid2entry(be, uniqueid, txn, &err)) != NULL) {

        /*
         * we'd like to return the entry. lock it if requested,
         * retrying if necessary.
         */

        /* wait for entry modify lock */
        if (!lock || cache_lock_entry(&inst->inst_cache, e) == 0) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "find_entry_internal_uniqueid", "<= Found uniqueid = (%s)\n", uniqueid);
            return (e);
        }
        /*
         * this entry has been deleted - see if it was actually
         * replaced with a new copy, and try the whole thing again.
         */
        slapi_log_err(SLAPI_LOG_ARGS,
                      "   find_entry_internal_uniqueid", "Retrying; uniqueid = (%s)\n", uniqueid);
        CACHE_RETURN(&inst->inst_cache, &e);
        tries++;
    }
    if (tries >= LDBM_CACHE_RETRY_COUNT) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "find_entry_internal_uniqueid", "Retry count exceeded; uniqueid = (%s)\n",
                      uniqueid);
    }

    /* entry not found */
    slapi_send_ldap_result(pb, (0 == err || DBI_RC_NOTFOUND == err) ? LDAP_NO_SUCH_OBJECT : LDAP_OPERATIONS_ERROR, NULL /* matched */, NULL,
                           0, NULL);
    slapi_log_err(SLAPI_LOG_TRACE,
                  "find_entry_internal_uniqueid", "<= not found; uniqueid = (%s)\n",
                  uniqueid);
    return (NULL);
}

static struct backentry *
find_entry_internal(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    int lock,
    back_txn *txn,
    int flags,
    int *rc)
{
    /* check if we should search based on uniqueid or dn */
    if (addr->uniqueid != NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "find_entry_internal", "=> (uniqueid=%s) lock %d\n",
                      addr->uniqueid, lock);
        return (find_entry_internal_uniqueid(pb, be, addr->uniqueid, lock, txn));
    } else {
        struct backentry *entry = NULL;

        slapi_log_err(SLAPI_LOG_TRACE, "find_entry_internal", "(dn=%s) lock %d\n",
                      slapi_sdn_get_dn(addr->sdn), lock);
        if (addr->sdn) {
            entry = find_entry_internal_dn(pb, be, addr->sdn, lock, txn, flags, rc);
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "find_entry_internal", "NULL target dn\n");
        }

        slapi_log_err(SLAPI_LOG_TRACE, "find_entry_internal", "<=\n");
        return entry;
    }
}

struct backentry *
find_entry(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    back_txn *txn,
    int *rc)
{
    return (find_entry_internal(pb, be, addr, 0 /*!lock*/, txn, 0 /*flags*/, rc));
}

struct backentry *
find_entry2modify(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    back_txn *txn,
    int *rc)
{
    return (find_entry_internal(pb, be, addr, 1 /*lock*/, txn, 0 /*flags*/, rc));
}

/* New routines which do not do any referral stuff.
   Call these if all you want to do is get pointer to an entry
   and certainly do not want any side-effects relating to client ! */

struct backentry *
find_entry_only(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    back_txn *txn,
    int *rc)
{
    return (find_entry_internal(pb, be, addr, 0 /*!lock*/, txn, FE_REALLY_INTERNAL, rc));
}

struct backentry *
find_entry2modify_only(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    back_txn *txn,
    int *rc)
{
    return (find_entry_internal(pb, be, addr, 1 /*lock*/, txn, 0 /* to check aci, disable INTERNAL */, rc));
}

struct backentry *
find_entry2modify_only_ext(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    const entry_address *addr,
    int flags,
    back_txn *txn,
    int *rc)
{
    return (find_entry_internal(pb, be, addr, 1 /*lock*/, txn, FE_REALLY_INTERNAL | flags, rc));
}
