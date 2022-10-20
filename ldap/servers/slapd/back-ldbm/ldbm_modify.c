/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* modify.c - ldbm backend modify routine */

#include <assert.h>
#include "back-ldbm.h"

extern char *numsubordinates;
extern char *hassubordinates;

static void remove_illegal_mods(LDAPMod **mods);
static int mods_have_effect(Slapi_Entry *entry, Slapi_Mods *smods);

#define MOD_SET_ERROR(rc, error, count)                                            \
    {                                                                              \
        (rc) = (error);                                                            \
        (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
    }

/* Modify context structure constructor, sans allocation */
void
modify_init(modify_context *mc, struct backentry *old_entry)
{
    /* Store the old entry */
    PR_ASSERT(NULL == mc->old_entry);
    PR_ASSERT(NULL == mc->new_entry);

    mc->old_entry = old_entry;
    mc->attr_encrypt = 1;
}

int
modify_apply_mods(modify_context *mc, Slapi_Mods *smods)
{
    return modify_apply_mods_ignore_error(mc, smods, -1);
}

int
modify_apply_mods_ignore_error(modify_context *mc, Slapi_Mods *smods, int error)
{
    int ret = 0;
    /* Make a copy of the entry */
    PR_ASSERT(mc->old_entry != NULL);
    PR_ASSERT(mc->new_entry == NULL);
    mc->new_entry = backentry_dup(mc->old_entry);
    PR_ASSERT(smods != NULL);
    if (mods_have_effect(mc->new_entry->ep_entry, smods)) {
        ret = entry_apply_mods_ignore_error(mc->new_entry->ep_entry, slapi_mods_get_ldapmods_byref(smods), error);
    }
    mc->smods = smods;
    if (error == ret) {
        ret = LDAP_SUCCESS;
    }
    return ret;
}

/* Modify context structure destructor */
int
modify_term(modify_context *mc, struct backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    slapi_mods_free(&mc->smods);
    /* Unlock and return entries */
    if (mc->old_entry) {
        cache_unlock_entry(&inst->inst_cache, mc->old_entry);
        CACHE_RETURN(&(inst->inst_cache), &(mc->old_entry));
        mc->old_entry = NULL;
    }

    CACHE_RETURN(&(inst->inst_cache), &(mc->new_entry));
    mc->new_entry = NULL;
    return 0;
}

/* Modify context structure member to switch entries in the cache */
int
modify_switch_entries(modify_context *mc, backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int ret = 0;
    if (mc->old_entry && mc->new_entry) {
        ret = cache_replace(&(inst->inst_cache), mc->old_entry, mc->new_entry);
        if (ret) {
            slapi_log_err(SLAPI_LOG_CACHE, "modify_switch_entries", "Replacing %s with %s failed (%d)\n",
                          slapi_entry_get_dn(mc->old_entry->ep_entry),
                          slapi_entry_get_dn(mc->new_entry->ep_entry), ret);
        }
    }
    return ret;
}

/*
 * Switch the new with the old(original) - undoing modify_switch_entries()
 * This expects modify_term() to be called next, as the old "new" entry
 * is now gone(replaced by the original entry).
 */
int
modify_unswitch_entries(modify_context *mc, backend *be)
{
    struct backentry *tmp_be;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int ret = 0;

    if (mc->old_entry && mc->new_entry &&
        cache_is_in_cache(&inst->inst_cache, mc->new_entry)) {
        /* switch the entries, and reset the new, new, entry */
        tmp_be = mc->new_entry;
        mc->new_entry = mc->old_entry;
        mc->new_entry->ep_state = 0;
        if (cache_has_otherref(&(inst->inst_cache), mc->new_entry)) {
            /* some other thread refers the entry */
            CACHE_RETURN(&(inst->inst_cache), &(mc->new_entry));
        } else {
            /* don't call CACHE_RETURN, that frees the entry!  */
            mc->new_entry->ep_refcnt = 0;
        }
        mc->old_entry = tmp_be;

        ret = cache_replace(&(inst->inst_cache), mc->old_entry, mc->new_entry);
        if (ret == 0) {
            /*
             * The new entry was originally locked, so since we did the
             * switch we need to unlock the "new" entry, and return the
             * "old" one.  modify_term() will then return the "new" entry.
             */
            cache_unlock_entry(&inst->inst_cache, mc->new_entry);
            cache_lock_entry(&inst->inst_cache, mc->old_entry);
        } else {
            slapi_log_err(SLAPI_LOG_CACHE, "modify_unswitch_entries", "Replacing %s with %s failed (%d)\n",
                          slapi_entry_get_dn(mc->old_entry->ep_entry),
                          slapi_entry_get_dn(mc->new_entry->ep_entry), ret);
        }
    }

    return ret;
}

/* This routine does that part of a modify operation which involves
   updating the on-disk data: updates idices, id2entry.
   Copes properly with DBI_RC_RETRY. The caller must be able to cope with
   DBI_RC_RETRY returned.
   The caller is presumed to proceed as follows:
    Find the entry you want to modify;
    Lock it for modify;
    Make a copy of it; (call backentry_dup() )
    Apply modifications to the copy in memory (call entry_apply_mods() )
    begin transaction;
    Do any other mods to on-disk data you want
    Call this routine;
    Commit transaction;
   You pass it environment data: struct ldbminfo, pb (not sure why, but the vlv code seems to need it)
   the copy of the entry before modfication, the entry after modification;
   an LDAPMods array containing the modifications performed
*/
int
modify_update_all(backend *be, Slapi_PBlock *pb, modify_context *mc, back_txn *txn)
{
    static char *function_name = "modify_update_all";
    Slapi_Operation *operation;
    int is_ruv = 0; /* True if the current entry is RUV */
    int retval = 0;

    if (pb) { /* pb could be NULL if it's called from import */
        slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
        is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    }
    /*
     * Update the ID to Entry index.
     * Note that id2entry_add replaces the entry, so the Entry ID stays the same.
     */
    retval = id2entry_add_ext(be, mc->new_entry, txn, mc->attr_encrypt, NULL);
    if (0 != retval) {
        if (DBI_RC_RETRY != retval) {
            ldbm_nasty(function_name, "", 66, retval);
        }
        goto error;
    }
    retval = index_add_mods(be, slapi_mods_get_ldapmods_byref(mc->smods), mc->old_entry, mc->new_entry, txn);
    if (0 != retval) {
        if (DBI_RC_RETRY != retval) {
            ldbm_nasty(function_name, "", 65, retval);
        }
        goto error;
    }
    /*
     * Remove the old entry from the Virtual List View indexes.
     * Add the new entry to the Virtual List View indexes.
     * Because the VLV code calls slapi_filter_test(), which requires a pb (why?),
     * we allow the caller sans pb to get everything except vlv indexing.
     */
    if (NULL != pb && !is_ruv) {
        retval = vlv_update_all_indexes(txn, be, pb, mc->old_entry, mc->new_entry);
        if (0 != retval) {
            if (DBI_RC_RETRY != retval) {
                ldbm_nasty(function_name, "", 64, retval);
            }
            goto error;
        }
    }
error:
    return retval;
}

int32_t
entry_get_rdn_mods(Slapi_PBlock *pb, Slapi_Entry *entry, CSN *csn, int repl_op, Slapi_Mods **smods_ret)
{
    unsigned long op_type = SLAPI_OPERATION_NONE;
    char *new_rdn = NULL;
    char **dns = NULL;
    char **rdns = NULL;
    Slapi_Mods *smods = NULL;
    char *type = NULL;
    struct berval *bvp[2] = {0};
    struct berval bv;
    Slapi_Attr *attr = NULL;
    const char *entry_dn = NULL;

    *smods_ret = NULL;
    entry_dn = slapi_entry_get_dn_const(entry);
    /* Do not bother to check that RDN is present, no one rename RUV or change its nsuniqueid */
    if (strcasestr(entry_dn, RUV_STORAGE_ENTRY_UNIQUEID)) {
        return 0;
    }

    /* First get the RDNs of the operation */
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);
    switch (op_type) {
        case SLAPI_OPERATION_MODIFY:
            dns = slapi_ldap_explode_dn(entry_dn, 0);
            if (dns == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "entry_get_rdn_mods",
                      "Fails to split DN \"%s\" into components\n", entry_dn);
                return -1;
            }
            rdns = slapi_ldap_explode_rdn(dns[0], 0);
            slapi_ldap_value_free(dns);

            break;
        case SLAPI_OPERATION_MODRDN:
            slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &new_rdn);
            rdns = slapi_ldap_explode_rdn(new_rdn, 0);
            break;
        default:
            break;
    }
    if (rdns == NULL || rdns[0] == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "entry_get_rdn_mods",
                      "Fails to split RDN \"%s\" into components\n", slapi_entry_get_dn_const(entry));
        return -1;
    }

    /* Update the entry to add RDNs values if they are missing */
    smods = slapi_mods_new();

    bvp[0] = &bv;
    bvp[1] = NULL;
    for (size_t rdns_count = 0; rdns[rdns_count]; rdns_count++) {
        Slapi_Value *value;
        attr = NULL;
        slapi_rdn2typeval(rdns[rdns_count], &type, &bv);

        /* Check if the RDN value exists */
        if ((slapi_entry_attr_find(entry, type, &attr) != 0) ||
            (slapi_attr_value_find(attr, &bv))) {
            const CSN *csn_rdn_add;
            const CSN *adcsn = attr_get_deletion_csn(attr);

            /* It is missing => adds it */
            if (slapi_attr_flag_is_set(attr, SLAPI_ATTR_FLAG_SINGLE)) {
                if (csn_compare(adcsn, csn) >= 0) {
                    /* this is a single valued attribute and the current value
                     * (that is different from RDN value) is more recent than
                     * the RDN value we want to apply.
                     * Keep the current value and add a conflict flag
                     */

                    type = ATTR_NSDS5_REPLCONFLICT;
                    bv.bv_val = "RDN value may be missing because it is single-valued";
                    bv.bv_len = strlen(bv.bv_val);
                    slapi_entry_add_string(entry, type, bv.bv_val);
                    slapi_mods_add_modbvps(smods, LDAP_MOD_ADD, type, bvp);
                    continue;
                }
            }
            /* if a RDN value needs to be forced, make sure it csn is ahead */
            slapi_mods_add_modbvps(smods, LDAP_MOD_ADD, type, bvp);
            csn_rdn_add = csn_max(adcsn, csn);

            if (entry_apply_mods_wsi(entry, smods, csn_rdn_add, repl_op)) {
                slapi_log_err(SLAPI_LOG_ERR, "entry_get_rdn_mods",
                              "Fails to set \"%s\" in  \"%s\"\n", type, slapi_entry_get_dn_const(entry));
                slapi_ldap_value_free(rdns);
                slapi_mods_free(&smods);
                return -1;
            }
            /* Make the RDN value a distinguished value */
            attr_value_find_wsi(attr, &bv, &value);
            value_update_csn(value, CSN_TYPE_VALUE_DISTINGUISHED, csn_rdn_add);
        }
    }
    slapi_ldap_value_free(rdns);
    if (smods->num_mods == 0) {
        /* smods_ret already NULL, just free the useless smods */
        slapi_mods_free(&smods);
    } else {
        *smods_ret = smods;
    }
    return 0;
}
/**
   Apply the mods to the ec entry.  Check for syntax, schema problems.
   Check for abandon.

   Return code:
   -1 - error - result code and message are set appropriately
   0 - successfully applied and checked
   1 - not an error - no mods to apply or op abandoned
 */
static int
modify_apply_check_expand(
    Slapi_PBlock *pb,
    Slapi_Operation *operation,
    LDAPMod **mods,       /* list of mods to apply */
    struct backentry *e,  /* original "before" entry */
    struct backentry *ec, /* "after" entry with mods applied */
    Slapi_Entry **postentry,
    int *ldap_result_code,
    char **ldap_result_message)
{
    int rc = 0;
    int i;
    int repl_op;
    int change_entry = 0;
    Slapi_Mods smods = {0};
    CSN *csn = operation_get_csn(operation);

    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
    slapi_mods_init_byref(&smods, mods);

    if ((change_entry = mods_have_effect(ec->ep_entry, &smods))) {
        *ldap_result_code = entry_apply_mods_wsi(ec->ep_entry, &smods, csn,
                                                 operation_is_flag_set(operation, OP_FLAG_REPLICATED));
        /*
         * XXXmcs: it would be nice to get back an error message from
         * the above call so we could pass it along to the client, e.g.,
         * "duplicate value for attribute givenName."
         */
    } else {
        Slapi_Entry *epostop = NULL;
        /* If the entry was not actually changed, we still need to
         * set the SLAPI_ENTRY_POST_OP field in the pblock (post-op
         * plugins expect that field to be present for all modify
         * operations that return LDAP_SUCCESS).
         */
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &epostop);
        slapi_entry_free(epostop); /* free existing one, if any */
        slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, slapi_entry_dup(e->ep_entry));
        *postentry = NULL; /* to avoid free in main error cleanup code */
    }
    if (!change_entry || *ldap_result_code != 0) {
        /* change_entry == 0 is not an error just a no-op */
        rc = change_entry ? -1 : 1;
        goto done;
    }



    /*
     * If the objectClass attribute type was modified in any way, expand
     * the objectClass values to reflect the inheritance hierarchy.
     */
    for (i = 0; mods && mods[i]; ++i) {
        if (0 == strcasecmp(SLAPI_ATTR_OBJECTCLASS, mods[i]->mod_type)) {
            slapi_schema_expand_objectclasses( ec->ep_entry );
            /* if the objectclass ldapsubentry is added or removed, the flag in the entry
             * has to be updated. Otherwise it would only be effective after a reload
             * of the entry.
             */
            for (size_t j = 0; mods[i]->mod_bvalues != NULL && mods[i]->mod_bvalues[j] != NULL; j++ ) {
                if  (strncasecmp((const char *)mods[i]->mod_bvalues[j]->bv_val,
                        "ldapsubentry", mods[i]->mod_bvalues[j]->bv_len) == 0) {
                    switch ( mods[i]->mod_op & ~LDAP_MOD_BVALUES ) {
                    case LDAP_MOD_ADD:
                    case LDAP_MOD_REPLACE:
                        ec->ep_entry->e_flags |= SLAPI_ENTRY_FLAG_LDAPSUBENTRY;
                        break;
                    case LDAP_MOD_DELETE:
                        ec->ep_entry->e_flags &= ~SLAPI_ENTRY_FLAG_LDAPSUBENTRY;
                        break;
                    }
                    break;
                }
            }
            break;
        }
    }

    /*
     * We are about to pass the last abandon test, so from now on we are
     * committed to finish this operation. Set status to "will complete"
     * before we make our last abandon check to avoid race conditions in
     * the code that processes abandon operations.
     */
    operation->o_status = SLAPI_OP_STATUS_WILL_COMPLETE;
    if (slapi_op_abandoned(pb)) {
        rc = 1;
        goto done;
    }

    /* multisupplier replication can result in a schema violation,
     * although the individual operations on each supplier were valid
     * It is too late to resolve this. But we can check schema and
     * add a replication conflict attribute.
     */
    /* check that the entry still obeys the schema */
    if ((operation_is_flag_set(operation, OP_FLAG_ACTION_SCHEMA_CHECK)) &&
        slapi_entry_schema_check_ext(pb, ec->ep_entry, 1) != 0) {
        if (repl_op) {
            Slapi_Attr *attr;
            Slapi_Mods smods;
            LDAPMod **lmods;
            if (slapi_entry_attr_find(ec->ep_entry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0) {
                /* add value */
                Slapi_Value *val = slapi_value_new_string("Schema violation");
                slapi_attr_add_value(attr, val);
                slapi_value_free(&val);
            } else {
                /* Add new attribute */
                slapi_entry_add_string(ec->ep_entry, ATTR_NSDS5_REPLCONFLICT, "Schema violation");
            }
            /* the replconflict attribute is indexed and the index is built from the mods,
             * so we need to extend the mods */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &lmods);
            slapi_mods_init_passin(&smods, lmods);
            slapi_mods_add_string(&smods, LDAP_MOD_ADD, ATTR_NSDS5_REPLCONFLICT, "Schema violation");
            lmods = slapi_mods_get_ldapmods_passout(&smods);
            slapi_pblock_set(pb, SLAPI_MODIFY_MODS, lmods);
            slapi_mods_done(&smods);

        } else {
            *ldap_result_code = LDAP_OBJECT_CLASS_VIOLATION;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, ldap_result_message);
            rc = -1;
            goto done;
        }
    }

    if (!repl_op) {
        /* check attribute syntax for the new values */
        if (slapi_mods_syntax_check(pb, mods, 0) != 0) {
            *ldap_result_code = LDAP_INVALID_SYNTAX;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, ldap_result_message);
            rc = -1;
            goto done;
        }

        /*
         * make sure the entry contains all values in the RDN.
         * if not, the modification must have removed them.
         */
        if (!slapi_entry_rdn_values_present(ec->ep_entry)) {
            *ldap_result_code = LDAP_NOT_ALLOWED_ON_RDN;
            rc = -1;
            goto done;
        }
    }

done:
    slapi_mods_done(&smods);

    return rc;
}

int
ldbm_back_modify(Slapi_PBlock *pb)
{
    backend *be;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li;
    struct backentry *e = NULL, *ec = NULL;
    struct backentry *original_entry = NULL, *tmpentry = NULL;
    Slapi_Entry *postentry = NULL;
    LDAPMod **mods = NULL;
    LDAPMod **mods_original = NULL;
    Slapi_Mods smods = {0};
    back_txn txn;
    back_txnid parent_txn;
    modify_context ruv_c = {0};
    int ruv_c_init = 0;
    int retval = -1;
    const char *msg;
    char *errbuf = NULL;
    int retry_count = 0;
    int disk_full = 0;
    int ldap_result_code = LDAP_SUCCESS;
    char *ldap_result_message = NULL;
    int rc = 0;
    Slapi_Operation *operation;
    entry_address *addr;
    int is_fixup_operation = 0;
    int is_ruv = 0; /* True if the current entry is RUV */
    CSN *opcsn = NULL;
    int repl_op;
    int opreturn = 0;
    int mod_count = 0;
    int not_an_error = 0;
    int is_noop = 0;
    int fixup_tombstone = 0;
    int ec_locked = 0;
    int result_sent = 0;
    int32_t parent_op = 0;
    struct timespec parent_time;
    Slapi_Mods *smods_add_rdn = NULL;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TARGET_ADDRESS, &addr);
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&parent_txn);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);

    fixup_tombstone = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_FIXUP);

    dblayer_txn_init(li, &txn); /* must do this before first goto error_return */
    /* the calls to perform searches require the parent txn if any
       so set txn to the parent_txn until we begin the child transaction */

    if (txn.back_txn_txn == NULL) {
        /* This is the parent operation, get the time */
        parent_op = 1;
        parent_time = slapi_current_rel_time_hr();
    }

    if (parent_txn) {
        txn.back_txn_txn = parent_txn;
    } else {
        parent_txn = txn.back_txn_txn;
        /* coverity[var_deref_model] */
        slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    }

    if (NULL == operation) {
        ldap_result_code = LDAP_OPERATIONS_ERROR;
        goto error_return;
    }

    is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
    is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    inst = (ldbm_instance *)be->be_instance_info;

    if (NULL == addr) {
        goto error_return;
    }
    if (inst && inst->inst_ref_count) {
        slapi_counter_increment(inst->inst_ref_count);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                      "Instance \"%s\" does not exist.\n",
                      inst ? inst->inst_name : "null instance");
        goto error_return;
    }

    /* no need to check the dn syntax as this is a replicated op */
    if (!repl_op) {
        ldap_result_code = slapi_dn_syntax_check(pb, slapi_sdn_get_dn(addr->sdn), 1);
        if (ldap_result_code) {
            ldap_result_code = LDAP_INVALID_DN_SYNTAX;
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
            goto error_return;
        }
    }

    /* The dblock serializes writes to the database,
     * which reduces deadlocking in the db code,
     * which means that we run faster.
     *
     * But, this lock is re-enterant for the fixup
     * operations that the URP code in the Replication
     * plugin generates.
     *
     * SERIALLOCK is moved to dblayer_txn_begin along with exposing be
     * transaction to plugins (see slapi_back_transaction_* APIs).
     *
    if(SERIALLOCK(li) && !operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP)) {
        dblayer_lock_backend(be);
        dblock_acquired= 1;
    }
     */
    if (MANAGE_ENTRY_BEFORE_DBLOCK(li)) {
        /* find and lock the entry we are about to modify */
        if (fixup_tombstone) {
            e = find_entry2modify_only_ext(pb, be, addr, TOMBSTONE_INCLUDED, &txn, &result_sent);
        } else {
            e = find_entry2modify(pb, be, addr, &txn, &result_sent);
        }
        if (e == NULL) {
            ldap_result_code = -1;
            goto error_return; /* error result sent by find_entry2modify() */
        }
    }

    txn.back_txn_txn = NULL; /* ready to create the child transaction */
    for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
        int cache_rc = 0;
        int new_mod_count = 0;
        if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
            /* don't release SERIAL LOCK */
            dblayer_txn_abort_ext(li, &txn, PR_FALSE);
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
            /*
             * Since be_txn_preop functions could have modified the entry/mods,
             * We need to grab the current mods, free them, and restore the
             * originals.  Same thing for the entry.
             */

            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            ldap_mods_free(mods, 1);
            slapi_pblock_set(pb, SLAPI_MODIFY_MODS, copy_mods(mods_original));

            /* reset ec set cache in id2entry_add_ext */
            if (ec) {
                /* must duplicate ec before returning it to cache,
                 * which could free the entry. */
                if ((tmpentry = backentry_dup(original_entry ? original_entry : ec)) == NULL) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    goto error_return;
                }
                if (cache_is_in_cache(&inst->inst_cache, ec)) {
                    CACHE_REMOVE(&inst->inst_cache, ec);
                }
                CACHE_RETURN(&inst->inst_cache, &ec);
                slapi_pblock_set(pb, SLAPI_MODIFY_EXISTING_ENTRY, original_entry->ep_entry);
                ec = original_entry;
                original_entry = tmpentry;
                tmpentry = NULL;
            }

            if (ruv_c_init) {
                /* reset the ruv txn stuff */
                modify_term(&ruv_c, be);
                ruv_c_init = 0;
            }

            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modify",
                          "Modify Retrying Transaction\n");
#ifndef LDBM_NO_BACKOFF_DELAY
            {
                PRIntervalTime interval;
                interval = PR_MillisecondsToInterval(slapi_rand() % 100);
                DS_Sleep(interval);
            }
#endif
        }

        /* Nothing above here modifies persistent store, everything after here is subject to the transaction */
        /* dblayer_txn_begin holds SERIAL lock,
         * which should be outside of locking the entry (find_entry2modify) */
        if (0 == retry_count) {
            /* First time, hold SERIAL LOCK */
            retval = dblayer_txn_begin(be, parent_txn, &txn);
        } else {
            /* Otherwise, no SERIAL LOCK */
            retval = dblayer_txn_begin_ext(li, parent_txn, &txn, PR_FALSE);
        }
        if (0 != retval) {
            if (LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            goto error_return;
        }
        /* stash the transaction for plugins */
        slapi_pblock_set(pb, SLAPI_TXN, txn.back_txn_txn);

        if (0 == retry_count) { /* just once */
            if (!MANAGE_ENTRY_BEFORE_DBLOCK(li)) {
                /* find and lock the entry we are about to modify */
                if (fixup_tombstone) {
                    e = find_entry2modify_only_ext(pb, be, addr, TOMBSTONE_INCLUDED, &txn, &result_sent);
                } else {
                    e = find_entry2modify(pb, be, addr, &txn, &result_sent);
                }
                if (e == NULL) {
                    ldap_result_code = -1;
                    goto error_return; /* error result sent by find_entry2modify() */
                }
            }
            assert(e);

            if (!is_fixup_operation && !fixup_tombstone) {
                if (!repl_op && slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE)) {
                    ldap_result_code = LDAP_UNWILLING_TO_PERFORM;
                    ldap_result_message = "Operation not allowed on tombstone entry.";
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                                  "Attempt to modify a tombstone entry %s\n",
                                  slapi_sdn_get_dn(slapi_entry_get_sdn_const(e->ep_entry)));
                    goto error_return;
                }
                opcsn = operation_get_csn(operation);
                if (opcsn == NULL && operation->o_csngen_handler) {
                    /*
                     * Current op is a user request. Opcsn will be assigned
                     * if the dn is in an updatable replica.
                     */
                    if (entry_assign_operation_csn(pb, e->ep_entry, NULL, &opcsn) != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                                "failed to generate modify CSN for entry (%s), aborting operation\n",
                                slapi_entry_get_dn(e->ep_entry));
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        goto error_return;
                    }
                }
                if (opcsn) {
                    entry_set_maxcsn(e->ep_entry, opcsn);
                }
            }

            /* Save away a copy of the entry, before modifications */
            slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup(e->ep_entry));

            if ((ldap_result_code = plugin_call_acl_mods_access(pb, e->ep_entry, mods, &errbuf)) != LDAP_SUCCESS) {
                ldap_result_message = errbuf;
                goto error_return;
            }

            /* create a copy of the entry and apply the changes to it */
            if ((ec = backentry_dup(e)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }

            if (!repl_op) {
                remove_illegal_mods(mods);
            }

            /* ec is the entry that our bepreop should get to mess with */
            slapi_pblock_set(pb, SLAPI_MODIFY_EXISTING_ENTRY, ec->ep_entry);
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);

            opreturn = plugin_call_mmr_plugin_preop(pb, NULL,SLAPI_PLUGIN_BE_PRE_MODIFY_FN);
            if (opreturn == 0) {
                opreturn = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_MODIFY_FN);
            }
            if (opreturn ||
                (slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code) && ldap_result_code) ||
                (slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn) && opreturn)) {
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                if (!ldap_result_code) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                                  "SLAPI_PLUGIN_BE_PRE_MODIFY_FN "
                                  "returned error but did not set SLAPI_RESULT_CODE\n");
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                }
                if (SLAPI_PLUGIN_NOOP == opreturn) {
                    not_an_error = 1;
                    is_noop = 1;
                    rc = opreturn = LDAP_SUCCESS;
                } else if (!opreturn) {
                    opreturn = SLAPI_PLUGIN_FAILURE;
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                }
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                goto error_return;
            }
            /* The Plugin may have messed about with some of the PBlock parameters... ie. mods */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

            /* apply the mods, check for syntax, schema problems, etc. */
            if (modify_apply_check_expand(pb, operation, mods, e, ec, &postentry,
                                          &ldap_result_code, &ldap_result_message)) {
                goto error_return;
            }
            /* the schema check could have added a repl conflict mod
             * get the mods again */
            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            slapi_mods_init_byref(&smods, mods);
            mod_count = slapi_mods_get_num_mods(&smods);
            /*
             * Grab a copy of the mods and the entry in case the be_txn_preop changes
             * the them.  If we have a failure, then we need to reset the mods to their
             * their original state;
             */
            mods_original = copy_mods(mods);
            if ((original_entry = backentry_dup(ec)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
        } /* if (0 == retry_count) just once */

        /* call the transaction pre modify plugins just after creating the transaction */
        retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN);
        if (retval) {
            slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modify", "SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN plugin "
                                                               "returned error code %d\n",
                          retval);
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
            slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
            if (SLAPI_PLUGIN_NOOP == retval) {
                not_an_error = 1;
                rc = retval = LDAP_SUCCESS;
            }
            if (!opreturn) {
                slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
            }
            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
            goto error_return;
        }

        /* the mods might have been changed, so get the latest */
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

        /* make sure the betxnpreop did not alter any of the mods that
           had already previously been applied */
        slapi_mods_done(&smods);
        slapi_mods_init_byref(&smods, mods);
        new_mod_count = slapi_mods_get_num_mods(&smods);
        if (new_mod_count < mod_count) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                          "Error: BE_TXN_PRE_MODIFY plugin has removed "
                          "mods from the original list - mod count was [%d] now [%d] "
                          "mods will not be applied - mods list changes must be done "
                          "in the BE_PRE_MODIFY plugin, not the BE_TXN_PRE_MODIFY\n",
                          mod_count, new_mod_count);
        } else if (new_mod_count > mod_count) { /* apply the new betxnpremod mods */
            /* apply the mods, check for syntax, schema problems, etc. */
            if (modify_apply_check_expand(pb, operation, &mods[mod_count], e, ec, &postentry,
                                          &ldap_result_code, &ldap_result_message)) {
                goto error_return;
            }
        } /* else if new_mod_count == mod_count then betxnpremod plugin did nothing */

        /* time to check if applying a replicated operation removed
         * the RDN value from the entry. Assuming that only replicated update
         * can lead to that bad result
         */
        if (entry_get_rdn_mods(pb, ec->ep_entry, opcsn, repl_op, &smods_add_rdn)) {
            goto error_return;
        }


        /*
         * Update the ID to Entry index.
         * Note that id2entry_add replaces the entry, so the Entry ID
         * stays the same.
         */
        retval = id2entry_add_ext(be, ec, &txn, 1, &cache_rc);
        if (DBI_RC_RETRY == retval) {
            /* Abort and re-try */
            continue;
        }
        if (0 != retval) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                          "id2entry_add failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            if (LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            goto error_return;
        }
        retval = index_add_mods(be, mods, e, ec, &txn);
        if (DBI_RC_RETRY == retval) {
            /* Abort and re-try */
            continue;
        }
        if (0 != retval) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                          "index_add_mods failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            if (LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            goto error_return;
        }

        if (smods_add_rdn && slapi_mods_get_num_mods(smods_add_rdn) > 0) {
            retval = index_add_mods(be, (LDAPMod **) slapi_mods_get_ldapmods_byref(smods_add_rdn), e, ec, &txn);
            if (DBI_RC_RETRY == retval) {
                /* Abort and re-try */
                slapi_mods_free(&smods_add_rdn);
                continue;
            }
            if (retval != 0) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                        "index_add_mods (rdn) failed, err=%d %s\n",
                        retval, (msg = dblayer_strerror(retval)) ? msg : "");
                MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                slapi_mods_free(&smods_add_rdn);
                goto error_return;
            }
        }
        /*
         * Remove the old entry from the Virtual List View indexes.
         * Add the new entry to the Virtual List View indexes.
         * If the entry is ruv, no need to update vlv.
         */
        if (!is_ruv) {
            retval = vlv_update_all_indexes(&txn, be, pb, e, ec);
            if (DBI_RC_RETRY == retval) {
                /* Abort and re-try */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                              "vlv_update_index failed, err=%d %s\n",
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                MOD_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }

        if (!is_ruv && !is_fixup_operation && !NO_RUV_UPDATE(li)) {
            ruv_c_init = ldbm_txn_ruv_modify_context(pb, &ruv_c);
            if (-1 == ruv_c_init) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                              "ldbm_txn_ruv_modify_context failed to construct RUV modify context\n");
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                retval = 0;
                goto error_return;
            }
        }

        if (ruv_c_init) {
            retval = modify_update_all(be, pb, &ruv_c, &txn);
            if (DBI_RC_RETRY == retval) {
                /* Abort and re-try */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                              "modify_update_all failed, err=%d %s\n", retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
        }

        if (0 == retval) {
            break;
        }
    }
    if (retry_count == RETRY_TIMES) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                      "Retry count exceeded in modify\n");
        ldap_result_code = LDAP_BUSY;
        goto error_return;
    }

    if (ruv_c_init) {
        if (modify_switch_entries(&ruv_c, be) != 0) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modify",
                          "modify_switch_entries failed\n");
            goto error_return;
        }
    }

    if (cache_replace(&inst->inst_cache, e, ec) != 0) {
        MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
        goto error_return;
    }
    /* e uncached */
    /* we must return both e (which has been deleted) and new entry ec to cache */
    /* cache_replace removes e from the cache hash tables */
    cache_unlock_entry(&inst->inst_cache, e);
    CACHE_RETURN(&inst->inst_cache, &e);
    /* lock new entry in cache to prevent usage until we are complete */
    cache_lock_entry(&inst->inst_cache, ec);
    ec_locked = 1;
    postentry = slapi_entry_dup(ec->ep_entry);
    slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, postentry);

    /* invalidate virtual cache */
    ec->ep_entry->e_virtual_watermark = 0;

    /*
     * LP Fix of crash when the commit will fail:
     * If the commit fail, the common error path will
     * try to unlock the entry again and crash (PR_ASSERT
     * in debug mode.
     * By just setting e to NULL, we avoid this. It's OK since
     * we don't use e after that in the normal case.
     */
    e = NULL;

    /* call the transaction post modify plugins just before the commit */
    if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN))) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modify",
                      "SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN plugin "
                      "returned error code %d\n",
                      retval);
        if (!ldap_result_code) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        if (!opreturn) {
            slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
        }
        if (!opreturn) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
        }
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
        goto error_return;
    }
    retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN);
    if (retval) {
        ldbm_set_error(pb, retval, &ldap_result_code, &ldap_result_message);
        goto error_return;
    }

    /* Release SERIAL LOCK */
    retval = dblayer_txn_commit(be, &txn);
    /* after commit - txn is no longer valid - replace SLAPI_TXN with parent */
    slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    if (0 != retval) {
        if (LDBM_OS_ERR_IS_DISKFULL(retval))
            disk_full = 1;
        ldap_result_code = LDAP_OPERATIONS_ERROR;
        goto error_return;
    }

    rc = 0;
    goto common_return;

error_return:
    /* Revert the caches if this is the parent operation */
    if (parent_op) {
        revert_cache(inst, &parent_time);
    }

    if (postentry != NULL) {
        slapi_entry_free(postentry);
        postentry = NULL;
        slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, NULL);
    }
    if (retval == DBI_RC_RUNRECOVERY) {
        dblayer_remember_disk_filled(li);
        ldbm_nasty("ldbm_back_modify", "Modify", 81, retval);
        disk_full = 1;
    }

    if (disk_full) {
        rc = return_on_disk_full(li);
    } else {
        if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
            /* make sure SLAPI_RESULT_CODE and SLAPI_PLUGIN_OPRETURN are set */
            int val = 0;
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &val);
            if (!val) {
                if (!ldap_result_code) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                }
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
            }
            slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &val);
            if (!val) {
                opreturn = -1;
                slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
            }
            /* call the transaction post modify plugins just before the abort */
            /* plugins called before abort should check for the OPRETURN or RESULT_CODE
               and skip processing if they don't want do anything - some plugins that
               keep track of a counter (usn, dna) may want to "rollback" the counter
               in this case */
            if (!is_noop && (retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN))) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modify",
                              "SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN plugin returned error code %d\n", retval);
                slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                if (!opreturn) {
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
                }
            }
            retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN);

            /* It is safer not to abort when the transaction is not started. */
            /* Release SERIAL LOCK */
            dblayer_txn_abort(be, &txn); /* abort crashes in case disk full */
            /* txn is no longer valid - reset the txn pointer to the parent */
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
        }
        if (!not_an_error) {
            rc = SLAPI_FAIL_GENERAL;
        }
    }

    /* if ec is in cache, remove it, then add back e if we still have it */
    if (inst && cache_is_in_cache(&inst->inst_cache, ec)) {
        CACHE_REMOVE(&inst->inst_cache, ec);
        /* if ec was in cache, e was not - add back e */
        if (e) {
            if (CACHE_ADD(&inst->inst_cache, e, NULL) < 0) {
                slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_modify", "CACHE_ADD %s failed\n",
                              slapi_entry_get_dn(e->ep_entry));
            }
        }
    }

common_return:
    slapi_mods_done(&smods);
    slapi_mods_free(&smods_add_rdn);

    if (inst) {
        if (ec_locked || cache_is_in_cache(&inst->inst_cache, ec)) {
            cache_unlock_entry(&inst->inst_cache, ec);
        } else if (e) {
            /* if ec was not in cache, cache_replace was not done.
             * i.e., e was not unlocked. */
            cache_unlock_entry(&inst->inst_cache, e);
            CACHE_RETURN(&inst->inst_cache, &e);
        }
        CACHE_RETURN(&inst->inst_cache, &ec);
        if (inst->inst_ref_count) {
            slapi_counter_decrement(inst->inst_ref_count);
        }
    }

    /* result code could be used in the bepost plugin functions. */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);

    /* The bepostop is called even if the operation fails. */
    if (!disk_full)
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN);

    if (ruv_c_init) {
        modify_term(&ruv_c, be);
    }

    if (ldap_result_code == -1) {
        /* Reset to LDAP_NO_SUCH_OBJECT*/
        ldap_result_code = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    } else {
        if (not_an_error) {
            /* This is mainly used by urp.  Solved conflict is not an error.
             * And we don't want the supplier to halt sending the updates. */
            ldap_result_code = LDAP_SUCCESS;
        }
        if (!result_sent) {
            /* result is already sent in find_entry. */
            slapi_send_ldap_result(pb, ldap_result_code, NULL, ldap_result_message, 0, NULL);
        }
    }

    /* free our backups */
    ldap_mods_free(mods_original, 1);
    backentry_free(&original_entry);
    backentry_free(&tmpentry);
    slapi_ch_free_string(&errbuf);

    return rc;
}

/* Function removes mods which are not allowed over-the-wire */
static void
remove_illegal_mods(LDAPMod **mods)
{
    int i, j;
    LDAPMod *tmp;

    /* remove any attempts by the user to modify these attrs */
    for (i = 0; (mods != NULL) && (mods[i] != NULL); i++) {
        if (strcasecmp(mods[i]->mod_type, numsubordinates) == 0 || strcasecmp(mods[i]->mod_type, hassubordinates) == 0) {
            tmp = mods[i];
            for (j = i; mods[j] != NULL; j++) {
                mods[j] = mods[j + 1];
            }
            slapi_ch_free((void **)&(tmp->mod_type));
            if (tmp->mod_bvalues != NULL) {
                ber_bvecfree(tmp->mod_bvalues);
            }
            slapi_ch_free((void **)&tmp);
            i--;
        }
    }
}

/* A mod has no effect if it is trying to replace a non-existing
 * attribute with null value
 */
static int
mods_have_effect(Slapi_Entry *entry, Slapi_Mods *smods)
{
    LDAPMod *mod;
    Slapi_Attr *attr;
    int have_effect = 1;
    int j;

    /* Mods have effect if there is at least a non-replace mod or
     * a non-null-value mod.
     */
    for (j = 0; j < smods->num_mods - 1; j++) {
        if ((mod = smods->mods[j]) != NULL) {
            if (((mod->mod_op & LDAP_MOD_REPLACE) == 0) ||
                (mod->mod_vals.modv_bvals &&
                 strcasecmp(mod->mod_type, "modifiersname") &&
                 strcasecmp(mod->mod_type, "modifytime"))) {
                goto done;
            }
        }
    }

    if (entry && entry->e_sdn.dn) {
        for (j = 0; j < smods->num_mods - 1; j++) {
            if ((mod = smods->mods[j]) != NULL) {
                for (attr = entry->e_attrs; attr; attr = attr->a_next) {
                    /* Mods have effect if at least a null-value-mod is
                     * to actually remove an existing attribute
                     */
                    if (strcasecmp(mod->mod_type, attr->a_type) == 0) {
                        have_effect = 1; /* found one - mod has effect */
                        goto done;
                    }
                    /* this mod type was not found in the entry - if we don't
                       find one of the other mod types, or if there are no more
                       mod types to look for, this mod does not apply */
                    have_effect = 0;
                }
            }
        }
    }

done:

    /* Return true would let the flow continue along the old path before
     * this function was added
     */
    return have_effect;
}
