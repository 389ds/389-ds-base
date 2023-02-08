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

/* modrdn.c - ldbm backend modrdn routine */

#include "back-ldbm.h"

static const char *moddn_get_newdn(Slapi_PBlock *pb, Slapi_DN *dn_olddn, Slapi_DN *dn_newrdn, Slapi_DN *dn_newsuperiordn, int is_tombstone);
static void moddn_unlock_and_return_entry(backend *be, struct backentry **targetentry);
static int moddn_newrdn_mods(Slapi_PBlock *pb, const char *olddn, struct backentry *ec, Slapi_Mods *smods_wsi, int is_repl_op);
static IDList *moddn_get_children(back_txn *ptxn, Slapi_PBlock *pb, backend *be, struct backentry *parententry, Slapi_DN *parentdn, struct backentry ***child_entries, struct backdn ***child_dns, int is_resurect_operation);
static int moddn_rename_children(back_txn *ptxn, Slapi_PBlock *pb, backend *be, IDList *children, Slapi_DN *dn_parentdn, Slapi_DN *dn_newsuperiordn, struct backentry *child_entries[]);
static int modrdn_rename_entry_update_indexes(back_txn *ptxn, Slapi_PBlock *pb, struct ldbminfo *li, struct backentry *e, struct backentry **ec, Slapi_Mods *smods1, Slapi_Mods *smods2, Slapi_Mods *smods3, Slapi_Mods *smods4);
static void mods_remove_nsuniqueid(Slapi_Mods *smods);
static int32_t dsentrydn_modrdn_update(backend *be, const char *newdn, struct backentry *e, struct backentry **ec, back_txn *txn);
static int dsentrydn_moddn_rename(back_txn *ptxn, backend *be, ID id, IDList *children, const Slapi_DN *dn_parentdn, const Slapi_DN *dn_newsuperiordn, struct backentry *child_entries[]);

#define MOD_SET_ERROR(rc, error, count)                                            \
    {                                                                              \
        (rc) = (error);                                                            \
        (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
    }

int
ldbm_back_modrdn(Slapi_PBlock *pb)
{
    backend *be;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li;
    struct backentry *e = NULL;
    struct backentry *ec = NULL;
    back_txn txn;
    back_txnid parent_txn;
    int retval = -1;
    const char *msg;
    Slapi_Entry *postentry = NULL;
    char *errbuf = NULL;
    int disk_full = 0;
    int retry_count = 0;
    int ldap_result_code = LDAP_SUCCESS;
    char *ldap_result_message = NULL;
    char *ldap_result_matcheddn = NULL;
    struct backentry *parententry = NULL;
    struct backentry *newparententry = NULL;
    struct backentry *original_entry = NULL;
    struct backentry *tmpentry = NULL;
    modify_context parent_modify_context = {0};
    modify_context newparent_modify_context = {0};
    modify_context ruv_c = {0};
    int ruv_c_init = 0;
    int is_ruv = 0;
    IDList *children = NULL;
    struct backentry **child_entries = NULL;
    struct backdn **child_dns = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_DN dn_newdn;
    Slapi_DN dn_newrdn;
    Slapi_DN *dn_newsuperiordn = NULL;
    Slapi_DN dn_parentdn;
    Slapi_DN *orig_dn_newsuperiordn = NULL;
    Slapi_DN *pb_dn_newsuperiordn = NULL; /* used to check what is currently in the pblock */
    Slapi_Entry *target_entry = NULL;
    Slapi_Entry *original_targetentry = NULL;
    int rc;
    int isroot;
    LDAPMod **mods;
    Slapi_Mods smods_operation_wsi = {0};
    Slapi_Mods smods_generated = {0};
    Slapi_Mods smods_generated_wsi = {0};
    Slapi_Operation *operation;
    int is_replicated_operation = 0;
    int is_fixup_operation = 0;
    int is_resurect_operation = 0;
    int is_tombstone = 0;
    entry_address new_addr;
    entry_address *old_addr;
    entry_address oldparent_addr;
    entry_address *newsuperior_addr;
    char *original_newrdn = NULL;
    CSN *opcsn = NULL;
    const char *newdn = NULL;
    char *newrdn = NULL;
    int opreturn = 0;
    int free_modrdn_existing_entry = 0;
    int not_an_error = 0;
    int support_moddn_aci;
    int myrc = 0;
    PRUint64 conn_id;
    int op_id;
    int result_sent = 0;
    Connection *pb_conn = NULL;
    int32_t parent_op = 0;
    struct timespec parent_time;
    Slapi_Mods *smods_add_rdn = NULL;

    if (slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id) < 0) {
        conn_id = 0; /* connection is NULL */
    }
    slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);

    /* sdn & parentsdn need to be initialized before "goto *_return" */
    slapi_sdn_init(&dn_newdn);
    slapi_sdn_init(&dn_newrdn);
    slapi_sdn_init(&dn_parentdn);

    slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&parent_txn);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
    is_resurect_operation = operation_is_flag_set(operation, OP_FLAG_RESURECT_ENTRY);
    is_tombstone = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY); /* tombstone_to_glue on parent entry*/
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    if (NULL == sdn) {
        slapi_send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                               "Null target DN", 0, NULL);
        return (-1);
    }

    /* In case of support of 'moddn' permission in aci, this permission will
     * be tested rather than the SLAPI_ACL_ADD
     */
    support_moddn_aci = config_get_moddn_aci();

    /* dblayer_txn_init needs to be called before "goto error_return" */
    dblayer_txn_init(li, &txn);

    if (txn.back_txn_txn == NULL) {
        /* This is the parent operation, get the time */
        parent_op = 1;
        parent_time = slapi_current_rel_time_hr();
    }

    /* the calls to perform searches require the parent txn if any
       so set txn to the parent_txn until we begin the child transaction */
    if (parent_txn) {
        txn.back_txn_txn = parent_txn;
    } else {
        parent_txn = txn.back_txn_txn;
        slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    }

    if (pb_conn) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "enter conn=%" PRIu64 " op=%d\n",
                      pb_conn->c_connid, operation->o_opid);
    }

    inst = (ldbm_instance *)be->be_instance_info;
    {
        char *newrdn /* , *newsuperiordn */;
        /* newrdn is normalized, bu tno need to be case-ignored as
         * it's passed to slapi_sdn_init_normdn_byref */
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &dn_newsuperiordn);
        slapi_sdn_init_dn_byref(&dn_newrdn, newrdn);
        /* slapi_sdn_init_normdn_byref(&dn_newsuperiordn, newsuperiordn); */
        if (is_resurect_operation) {
            /* no need to free this pdn. */
            const char *pdn = slapi_dn_find_parent_ext(slapi_sdn_get_dn(sdn), is_resurect_operation);
            slapi_sdn_set_dn_byval(&dn_parentdn, pdn);
        } else {
            slapi_sdn_get_parent(sdn, &dn_parentdn);
        }
    }

    /* if old and new superior are equals, newsuperior should not be set
     * Here we have to reset newsuperiordn in order to save processing and
     * avoid later deadlock when trying to fetch twice the same entry
     */
    if (slapi_sdn_compare(dn_newsuperiordn, &dn_parentdn) == 0) {
        slapi_sdn_done(dn_newsuperiordn);
    }

    /*
     * Even if subtree rename is off,
     * Replicated Operations are allowed to change the superior
     */
    if (!entryrdn_get_switch() &&
        (!is_replicated_operation && !slapi_sdn_isempty(dn_newsuperiordn))) {
        slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                               "server does not support moving of entries", 0, NULL);
        return (-1);
    }

    if (inst && inst->inst_ref_count) {
        slapi_counter_increment(inst->inst_ref_count);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                      "Instance \"%s\" does not exist.\n",
                      inst ? inst->inst_name : "null instance");
        return (-1);
    }

    /* The dblock serializes writes to the database,
     * which reduces deadlocking in the db code,
     * which means that we run faster.
     *
     * But, this lock is re-enterant for the fixup
     * operations that the URP code in the Replication
     * plugin generates.
     *
     * Also some URP post-op operations are called after
     * the backend has committed the change and released
     * the dblock. Acquire the dblock again for them
     * if OP_FLAG_ACTION_INVOKE_FOR_REPLOP is set.
     *
     * SERIALLOCK is moved to dblayer_txn_begin along with exposing be
     * transaction to plugins (see slapi_back_transaction_* APIs).
     *
    if(SERIALLOCK(li) &&
       (!operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP) ||
        operation_is_flag_set(operation,OP_FLAG_ACTION_INVOKE_FOR_REPLOP)))
    {
        dblayer_lock_backend(be);
        dblock_acquired= 1;
    }
     */

    /*
     * So, we believe that no code up till here actually added anything
     * to persistent store. From now on, we're transacted
     */
    txn.back_txn_txn = NULL; /* ready to create the child transaction */
    for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
        if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
            Slapi_Entry *ent = NULL;

            /* don't release SERIAL LOCK */
            dblayer_txn_abort_ext(li, &txn, PR_FALSE);
            /* txn is no longer valid - reset slapi_txn to the parent */
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);

            slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
            slapi_ch_free_string(&newrdn);
            slapi_pblock_set(pb, SLAPI_MODRDN_NEWRDN, original_newrdn);
            slapi_sdn_set_dn_byref(&dn_newrdn, original_newrdn);
            original_newrdn = slapi_ch_strdup(original_newrdn);

            /* we need to restart with the original newsuperiordn which could have
             * been modified. So check what is in the pblock, if it was changed
             * free it, reset orig dn in th epblock and recreate a working superior
             */
            slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &pb_dn_newsuperiordn);
            if (pb_dn_newsuperiordn != orig_dn_newsuperiordn) {
                slapi_sdn_free(&pb_dn_newsuperiordn);
                slapi_pblock_set(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, orig_dn_newsuperiordn);
                dn_newsuperiordn = slapi_sdn_dup(orig_dn_newsuperiordn);
            }
            /* must duplicate ec before returning it to cache,
             * which could free the entry. */
            if (!original_entry) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                    "retrying transaction, but no original entry found\n");
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
            if ((tmpentry = backentry_dup(original_entry)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
            slapi_pblock_get(pb, SLAPI_MODRDN_EXISTING_ENTRY, &ent);
            if (cache_is_in_cache(&inst->inst_cache, ec)) {
                CACHE_REMOVE(&inst->inst_cache, ec);
            }
            if (ent && (ent == ec->ep_entry)) {
                /*
                 * On a retry, it's possible that ec is now stored in the
                 * pblock as SLAPI_MODRDN_EXISTING_ENTRY.  "ec" will be freed
                 * by CACHE_RETURN below, so set ent to NULL so don't free
                 * it again.
                 * And it needs to be checked always.
                 */
                ent = NULL;
            }
            CACHE_RETURN(&inst->inst_cache, &ec);

            /* LK why do we need this ????? */
            if (!cache_is_in_cache(&inst->inst_cache, e)) {
                if (CACHE_ADD(&inst->inst_cache, e, NULL) < 0) {
                    slapi_log_err(SLAPI_LOG_CACHE,
                                  "ldbm_back_modrdn", "CACHE_ADD %s to cache failed\n",
                                  slapi_entry_get_dn_const(e->ep_entry));
                }
            }
            if (ent && (ent != original_entry->ep_entry)) {
                slapi_entry_free(ent);
                slapi_pblock_set(pb, SLAPI_MODRDN_EXISTING_ENTRY, NULL);
            }
            slapi_pblock_set(pb, SLAPI_MODRDN_EXISTING_ENTRY, original_entry->ep_entry);
            ec = original_entry;
            original_entry = tmpentry;
            tmpentry = NULL;
            free_modrdn_existing_entry = 0; /* owned by original_entry now */
            if (!cache_is_in_cache(&inst->inst_cache, ec)) {
                /* Put the resetted entry 'ec' into the cache again. */
                if (cache_add_tentative(&inst->inst_cache, ec, NULL) < 0) {
                    /* allow modrdn even if the src dn and dest dn are identical */
                    if (0 != slapi_sdn_compare((const Slapi_DN *)&dn_newdn,
                                               (const Slapi_DN *)sdn)) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                      "Adding %s to cache failed\n",
                                      slapi_entry_get_dn_const(ec->ep_entry));
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        goto error_return;
                    }
                    /* so if the old dn is the same as the new dn, the entry will not be cached
                       until it is replaced with cache_replace */
                }
            }

            slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_ENTRY, &target_entry);
            slapi_entry_free(target_entry);
            slapi_pblock_set(pb, SLAPI_MODRDN_TARGET_ENTRY, original_targetentry);
            if ((original_targetentry = slapi_entry_dup(original_targetentry)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }

            if (ruv_c_init) {
                /* reset the ruv txn stuff */
                modify_term(&ruv_c, be);
                ruv_c_init = 0;
            }
            /* We're re-trying */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                          "Modrdn Retrying Transaction\n");
#ifndef LDBM_NO_BACKOFF_DELAY
            {
                PRIntervalTime interval;
                interval = PR_MillisecondsToInterval(slapi_rand() % 100);
                DS_Sleep(interval);
            }
#endif
        }
        if (0 == retry_count) {
            /* First time, hold SERIAL LOCK */
            retval = dblayer_txn_begin(be, parent_txn, &txn);
        } else {
            /* Otherwise, no SERIAL LOCK */
            retval = dblayer_txn_begin_ext(li, parent_txn, &txn, PR_FALSE);
        }
        if (0 != retval) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            if (LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            goto error_return;
        }

        /* stash the transaction */
        slapi_pblock_set(pb, SLAPI_TXN, (void *)txn.back_txn_txn);

        if (0 == retry_count) { /* just once */
            rc = 0;
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_TARGET_ENTRY);
            while (rc != 0) {
                /* JCM - copying entries can be expensive... should optimize */
                /*
                 * Some present state information is passed through the PBlock to the
                 * backend pre-op plugin. To ensure a consistent snapshot of this state
                 * we wrap the reading of the entry with the dblock.
                 */
                /* <new rdn>,<new superior> */
                if (slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY)) {
                    /* see if an entry with the new name already exists */
                    done_with_pblock_entry(pb, SLAPI_MODRDN_EXISTING_ENTRY); /* Could be through this multiple times */
                    /* newrdn is normalized, bu tno need to be case-ignored as
                     * it's passed to slapi_sdn_init_normdn_byref */
                    slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
                    slapi_sdn_init_dn_byref(&dn_newrdn, newrdn);
                    newdn = moddn_get_newdn(pb, sdn, &dn_newrdn, dn_newsuperiordn, is_tombstone);
                    slapi_sdn_set_dn_passin(&dn_newdn, newdn);
                    new_addr.sdn = &dn_newdn;
                    new_addr.udn = NULL;

                    /* check dn syntax on newdn */
                    ldap_result_code = slapi_dn_syntax_check(pb,
                                                             (char *)slapi_sdn_get_ndn(&dn_newdn), 1);
                    if (ldap_result_code) {
                        ldap_result_code = LDAP_INVALID_DN_SYNTAX;
                        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                        goto error_return;
                    }
                    new_addr.uniqueid = NULL;
                    ldap_result_code = get_copy_of_entry(pb, &new_addr, &txn, SLAPI_MODRDN_EXISTING_ENTRY, 0);
                    if (ldap_result_code == LDAP_OPERATIONS_ERROR ||
                        ldap_result_code == LDAP_INVALID_DN_SYNTAX) {
                        goto error_return;
                    }
                    free_modrdn_existing_entry = 1; /* need to free it */
                }

                /* <old superior> */
                if (slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY)) {
                    /* find and lock the old parent entry */
                    done_with_pblock_entry(pb, SLAPI_MODRDN_PARENT_ENTRY); /* Could be through this multiple times */
                    oldparent_addr.sdn = &dn_parentdn;
                    oldparent_addr.uniqueid = NULL;
                    ldap_result_code = get_copy_of_entry(pb, &oldparent_addr, &txn, SLAPI_MODRDN_PARENT_ENTRY, !is_replicated_operation);
                }

                /* <new superior> */
                if (slapi_sdn_get_ndn(dn_newsuperiordn) != NULL &&
                    slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY)) {
                    /* find and lock the new parent entry */
                    done_with_pblock_entry(pb, SLAPI_MODRDN_NEWPARENT_ENTRY); /* Could be through this multiple times */
                    /* Check that this really is a new superior,
                     * and not the same old one. Compare parentdn & newsuperior */
                    if (slapi_sdn_compare(dn_newsuperiordn, &dn_parentdn) == 0) {
                        slapi_sdn_done(dn_newsuperiordn);
                    } else {
                        entry_address my_addr;
                        if (is_replicated_operation) {
                            /* If this is a replicated operation,
                             * then should fetch new superior with uniqueid */
                            slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_ADDRESS,
                                             &newsuperior_addr);
                        } else {
                            my_addr.sdn = dn_newsuperiordn;
                            my_addr.uniqueid = NULL;
                            newsuperior_addr = &my_addr;
                        }
                        ldap_result_code = get_copy_of_entry(pb, newsuperior_addr, &txn, SLAPI_MODRDN_NEWPARENT_ENTRY, !is_replicated_operation);
                    }
                }

                /* <old rdn>,<old superior> */
                if (slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_TARGET_ENTRY)) {
                    /* find and lock the entry we are about to modify */
                    done_with_pblock_entry(pb, SLAPI_MODRDN_TARGET_ENTRY); /* Could be through this multiple times */
                    slapi_pblock_get(pb, SLAPI_TARGET_ADDRESS, &old_addr);
                    ldap_result_code = get_copy_of_entry(pb, old_addr, &txn, SLAPI_MODRDN_TARGET_ENTRY, !is_replicated_operation);
                    if (ldap_result_code == LDAP_OPERATIONS_ERROR ||
                        ldap_result_code == LDAP_INVALID_DN_SYNTAX) {
                        /* JCM - Usually the call to find_entry2modify would generate the result code. */
                        /* JCM !!! */
                        goto error_return;
                    }
                }
                /* Call the Backend Pre ModRDN plugins */
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                rc = plugin_call_mmr_plugin_preop(pb, NULL,SLAPI_PLUGIN_BE_PRE_MODRDN_FN);
                if (rc == 0) {
                    rc= plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_MODRDN_FN);
                }
                if (rc < 0) {
                    if (SLAPI_PLUGIN_NOOP == rc) {
                        not_an_error = 1;
                        rc = LDAP_SUCCESS;
                    }
                    /*
                     * Plugin indicated some kind of failure,
                     * or that this Operation became a No-Op.
                     */
                    if (!ldap_result_code) {
                        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                    }
                    if (!opreturn) {
                        slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                    }
                    if (!opreturn) {
                        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &rc);
                    }
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
                /*
                 * (rc!=-1) means that the plugin changed things, so we go around
                 * the loop once again to get the new present state.
                 */
                /* JCMREPL - Warning: A Plugin could cause an infinite loop by always returning a result code that requires some action. */
            }

            /* find and lock the entry we are about to modify */
            /* JCMREPL - Argh, what happens about the stinking referrals? */
            slapi_pblock_get(pb, SLAPI_TARGET_ADDRESS, &old_addr);
            e = find_entry2modify(pb, be, old_addr, &txn, &result_sent);
            if (e == NULL) {
                ldap_result_code = -1;
                goto error_return; /* error result sent by find_entry2modify() */
            }
            if (slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE) &&
                !is_resurect_operation) {
                ldap_result_code = LDAP_UNWILLING_TO_PERFORM;
                ldap_result_message = "Operation not allowed on tombstone entry.";
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                              "Attempt to rename a tombstone entry %s\n",
                              slapi_sdn_get_dn(slapi_entry_get_sdn_const(e->ep_entry)));
                goto error_return;
            }
            /* Check that an entry with the same DN doesn't already exist. */
            {
                Slapi_Entry *entry;
                slapi_pblock_get(pb, SLAPI_MODRDN_EXISTING_ENTRY, &entry);
                if ((entry != NULL) &&
                    /* allow modrdn even if the src dn and dest dn are identical */
                    (0 != slapi_sdn_compare((const Slapi_DN *)&dn_newdn,
                                            (const Slapi_DN *)sdn))) {
                    ldap_result_code = LDAP_ALREADY_EXISTS;
                    goto error_return;
                }
            }

            /* Fetch and lock the parent of the entry that is moving */
            oldparent_addr.sdn = &dn_parentdn;
            if (is_resurect_operation) {
                oldparent_addr.uniqueid = operation->o_params.p.p_modrdn.modrdn_newsuperior_address.uniqueid;
            } else {
                oldparent_addr.uniqueid = NULL;
            }
            parententry = find_entry2modify_only(pb, be, &oldparent_addr, &txn, &result_sent);
            modify_init(&parent_modify_context, parententry);

            /* Fetch and lock the new parent of the entry that is moving */
            if (slapi_sdn_get_ndn(dn_newsuperiordn) != NULL) {
                slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_ADDRESS, &newsuperior_addr);
                if (is_resurect_operation) {
                    newsuperior_addr->uniqueid = slapi_entry_attr_get_charptr(e->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
                }
                newparententry = find_entry2modify_only(pb, be, newsuperior_addr, &txn, &result_sent);
                slapi_ch_free_string(&newsuperior_addr->uniqueid);
                modify_init(&newparent_modify_context, newparententry);
            }

            opcsn = operation_get_csn(operation);
            if (!is_fixup_operation) {
                if (opcsn == NULL && operation->o_csngen_handler) {
                    /*
                     * Current op is a user request. Opcsn will be assigned
                     * if the dn is in an updatable replica.
                     */
                    if (entry_assign_operation_csn(pb, e->ep_entry, parententry ? parententry->ep_entry : NULL, &opcsn) != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                "failed to generate modrdn CSN for entry (%s), aborting operation\n",
                                slapi_entry_get_dn(e->ep_entry));
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        goto error_return;
                    }
                }
                if (opcsn != NULL) {
                    entry_set_maxcsn(e->ep_entry, opcsn);
                }
            }

            if (newparententry != NULL) {
                /* don't forget we also want to preserve case of new superior */
                if (NULL == dn_newsuperiordn) {
                    dn_newsuperiordn = slapi_sdn_dup(
                        slapi_entry_get_sdn_const(newparententry->ep_entry));
                } else {
                    slapi_sdn_copy(slapi_entry_get_sdn_const(newparententry->ep_entry),
                                   dn_newsuperiordn);
                }
                slapi_pblock_set(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, dn_newsuperiordn);
            }
            slapi_sdn_set_dn_passin(&dn_newdn, moddn_get_newdn(pb, sdn, &dn_newrdn, dn_newsuperiordn, is_tombstone));

            /* Check that we're allowed to add an entry below the new superior */
            if (newparententry == NULL) {
                /* There may not be a new parent because we don't intend there to be one. */
                if (slapi_sdn_get_dn(dn_newsuperiordn)) {
                    /* If the new entry is not to be a suffix,
                     * return an error no matter who requested this modrdn */
                    if (!slapi_be_issuffix(be, &dn_newdn)) {
                        /* Here means that we didn't find the parent */
                        int err = 0;
                        Slapi_DN ancestorsdn;
                        struct backentry *ancestorentry;
                        slapi_sdn_init(&ancestorsdn);
                        ancestorentry = dn2ancestor(be, &dn_newdn, &ancestorsdn, &txn, &err, 0);
                        CACHE_RETURN(&inst->inst_cache, &ancestorentry);
                        ldap_result_matcheddn = slapi_ch_strdup((char *)slapi_sdn_get_dn(&ancestorsdn));
                        ldap_result_code = LDAP_NO_SUCH_OBJECT;
                        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "New superior "
                                                                           "does not exist matched %s, newsuperior = %s\n",
                                      ldap_result_matcheddn == NULL ? "NULL" : ldap_result_matcheddn,
                                      slapi_sdn_get_ndn(dn_newsuperiordn));
                        slapi_sdn_done(&ancestorsdn);
                        goto error_return;
                    }
                }
            } else {
                if (support_moddn_aci) {
                    /* aci permission requires 'moddn' right to allow a MODDN */
                    ldap_result_code = plugin_call_acl_plugin(pb, newparententry->ep_entry, NULL, NULL, SLAPI_ACL_MODDN, ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
                    if (ldap_result_code != LDAP_SUCCESS) {
                        ldap_result_message = errbuf;
                        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "No 'moddn' access to new superior.\n");
                        goto error_return;
                    }
                } else {
                    /* aci permission requires 'add' right to allow a MODDN (old style) */
                    ldap_result_code = plugin_call_acl_plugin(pb, newparententry->ep_entry, NULL, NULL, SLAPI_ACL_ADD, ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
                    if (ldap_result_code != LDAP_SUCCESS) {
                        ldap_result_message = errbuf;
                        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "No 'add' access to new superior.\n");
                        goto error_return;
                    }
                }
            }

            /* Check that the target entry has a parent */
            if (parententry == NULL) {
                /* If the entry a suffix, and we're root, then it's OK that the parent doesn't exist */
                if (!(slapi_be_issuffix(be, sdn)) && !isroot) {
                    /* Here means that we didn't find the parent */
                    ldap_result_matcheddn = "NULL";
                    ldap_result_code = LDAP_NO_SUCH_OBJECT;
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn",
                                  "Parent does not exist matched %s, parentdn = %s\n",
                                  ldap_result_matcheddn, slapi_sdn_get_ndn(&dn_parentdn));
                    goto error_return;
                }
            }

            /* If it is a replicated Operation or "subtree-rename" is on,
             * it's allowed to rename entries with children */
            if (!is_replicated_operation && !entryrdn_get_switch() &&
                slapi_entry_has_children(e->ep_entry)) {
                ldap_result_code = LDAP_NOT_ALLOWED_ON_NONLEAF;
                goto error_return;
            }

            /*
             * JCM - All the child entries must be locked in the cache, so the size of
             * subtree that can be renamed is limited by the cache size.
             */

            /* Save away a copy of the entry, before modifications */
            slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup(e->ep_entry));

            /* create a copy of the entry and apply the changes to it */
            if ((ec = backentry_dup(e)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }

            /* JCMACL - Should be performed before the child check. */
            /* JCMACL - Why is the check performed against the copy, rather than the existing entry? */
            /* This check must be performed even if the entry is renamed with its own name
             * No optimization here we need to check we have the write access to the target entry
             */
            ldap_result_code = plugin_call_acl_plugin(pb, ec->ep_entry,
                                                      NULL /*attr*/, NULL /*value*/, SLAPI_ACL_WRITE,
                                                      ACLPLUGIN_ACCESS_MODRDN, &errbuf);
            if (ldap_result_code != LDAP_SUCCESS) {
                goto error_return;
            }

            /* Set the new dn to the copy of the entry */
            slapi_entry_set_sdn(ec->ep_entry, &dn_newdn);
            if (entryrdn_get_switch()) { /* subtree-rename: on */
                Slapi_RDN srdn;
                /* Set the new rdn to the copy of the entry; store full dn in e_srdn */
                slapi_rdn_init_all_sdn(&srdn, &dn_newdn);
                slapi_entry_set_srdn(ec->ep_entry, &srdn);
                slapi_rdn_done(&srdn);
            }

            if (is_resurect_operation) {
                slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_modrdn",
                              "Resurrecting an entry %s\n", slapi_entry_get_dn(ec->ep_entry));
                slapi_entry_attr_delete(ec->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
                slapi_entry_attr_delete(ec->ep_entry, SLAPI_ATTR_TOMBSTONE_CSN);
                slapi_entry_delete_string(ec->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
                /* Now also remove the nscpEntryDN */
                if (slapi_entry_attr_delete(ec->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN) != 0) {
                    slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_modrdn", "Resurrection of %s - Couldn't remove %s\n",
                                  slapi_entry_get_dn(ec->ep_entry), SLAPI_ATTR_NSCP_ENTRYDN);
                }

                /* Set the reason (this is only a reason why modrdn is needed for resurrection) */
                slapi_entry_add_string(ec->ep_entry, "nsds5ReplConflict", "deletedEntryHasChildren");

                /* Clear the Tombstone Flag in the entry */
                slapi_entry_clear_flag(ec->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);

                /* make sure the objectclass
                   - does not contain any duplicate values
                   - has CSNs for the new values we added
                */
                {
                    Slapi_Attr *sa = NULL;
                    Slapi_Value sv;
                    const struct berval *svbv = NULL;

                    /* add the extensibleobject objectclass with csn if not present */
                    slapi_entry_attr_find(ec->ep_entry, SLAPI_ATTR_OBJECTCLASS, &sa);
                    slapi_value_init_string(&sv, "extensibleobject");
                    svbv = slapi_value_get_berval(&sv);
                    if (slapi_attr_value_find(sa, svbv)) { /* not found, so add it */
                        if (opcsn) {
                            value_update_csn(&sv, CSN_TYPE_VALUE_UPDATED, opcsn);
                        }
                        slapi_attr_add_value(sa, &sv);
                    }
                    value_done(&sv);

                    /* add the glue objectclass with csn if not present */
                    slapi_value_init_string(&sv, "glue");
                    svbv = slapi_value_get_berval(&sv);
                    if (slapi_attr_value_find(sa, svbv)) { /* not found, so add it */
                        if (opcsn) {
                            value_update_csn(&sv, CSN_TYPE_VALUE_UPDATED, opcsn);
                        }
                        slapi_attr_add_value(sa, &sv);
                    }
                    value_done(&sv);
                }
            }

            /* create it in the cache - prevents others from creating it */
            if ((cache_add_tentative(&inst->inst_cache, ec, NULL) < 0)) {
                /* allow modrdn even if the src dn and dest dn are identical */
                if (0 != slapi_sdn_compare((const Slapi_DN *)&dn_newdn,
                                           (const Slapi_DN *)sdn)) {
                    /* somebody must've created it between dn2entry() and here */
                    /* JCMREPL - Hmm... we can't permit this to happen...? */
                    ldap_result_code = LDAP_ALREADY_EXISTS;
                    if (is_resurect_operation) {
                        slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_modrdn",
                                      "conn=%" PRIu64 " op=%d cache_add_tentative failed: %s\n",
                                      conn_id, op_id, slapi_entry_get_dn(ec->ep_entry));
                    }
                    goto error_return;
                }
                /* so if the old dn is the same as the new dn, the entry will not be cached
                   until it is replaced with cache_replace */
            }
            /* Build the list of modifications required to the existing entry */
            slapi_mods_init(&smods_generated, 4);
            slapi_mods_init(&smods_generated_wsi, 4);
            ldap_result_code = moddn_newrdn_mods(pb, slapi_sdn_get_dn(sdn),
                                                 ec, &smods_generated_wsi, is_replicated_operation);
            if (ldap_result_code != LDAP_SUCCESS) {
                if (ldap_result_code == LDAP_UNWILLING_TO_PERFORM)
                    ldap_result_message = "Modification of old rdn attribute type not allowed.";
                goto error_return;
            }
            if (!entryrdn_get_switch()) /* subtree-rename: off */
            {
                /*
                * Remove the old entrydn index entry, and add the new one.
                */
                slapi_mods_add(&smods_generated, LDAP_MOD_DELETE, LDBM_ENTRYDN_STR,
                               strlen(backentry_get_ndn(e)), backentry_get_ndn(e));
                slapi_mods_add(&smods_generated, LDAP_MOD_REPLACE, LDBM_ENTRYDN_STR,
                               strlen(backentry_get_ndn(ec)), backentry_get_ndn(ec));
            }

            /*
             * Update parentid if we have a new superior.
             */
            if (slapi_sdn_get_dn(dn_newsuperiordn) != NULL) {
                char buf[40]; /* Enough for an ID */

                if (parententry != NULL) {
                    sprintf(buf, "%lu", (u_long)parententry->ep_id);
                    slapi_mods_add_string(&smods_generated, LDAP_MOD_DELETE, LDBM_PARENTID_STR, buf);
                }
                if (newparententry != NULL) {
                    sprintf(buf, "%lu", (u_long)newparententry->ep_id);
                    slapi_mods_add_string(&smods_generated, LDAP_MOD_REPLACE, LDBM_PARENTID_STR, buf);
                }
            }

            slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
            slapi_mods_init_byref(&smods_operation_wsi, mods);

            /*
             * We are about to pass the last abandon test, so from now on we are
             * committed to finish this operation. Set status to "will complete"
             * before we make our last abandon check to avoid race conditions in
             * the code that processes abandon operations.
             */
            if (operation) {
                operation->o_status = SLAPI_OP_STATUS_WILL_COMPLETE;
            }
            if (slapi_op_abandoned(pb)) {
                goto error_return;
            }

            /*
             * First, we apply the generated mods that do not involve any state information.
             */
            if (entry_apply_mods(ec->ep_entry, slapi_mods_get_ldapmods_byref(&smods_generated)) != 0) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "entry_apply_mods failed for entry %s\n",
                              slapi_entry_get_dn_const(ec->ep_entry));
                goto error_return;
            }

            /*
             * Now we apply the generated mods that do involve state information.
             */
            if (slapi_mods_get_num_mods(&smods_generated_wsi) > 0) {
                if (entry_apply_mods_wsi(ec->ep_entry, &smods_generated_wsi, operation_get_csn(operation), is_replicated_operation) != 0) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "entry_apply_mods_wsi failed for entry %s\n",
                                  slapi_entry_get_dn_const(ec->ep_entry));
                    goto error_return;
                }
            }

            /*
             * Now we apply the operation mods that do involve state information.
             * (Operational attributes).
             * The following block looks redundent to the one above. But it may
             * be necessary - check the comment for version 1.3.16.22.2.76 of
             * this file and compare that version with its previous one.
             */
            if (slapi_mods_get_num_mods(&smods_operation_wsi) > 0) {
                if (entry_apply_mods_wsi(ec->ep_entry, &smods_operation_wsi, operation_get_csn(operation), is_replicated_operation) != 0) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "entry_apply_mods_wsi (operational attributes) failed for entry %s\n",
                                  slapi_entry_get_dn_const(ec->ep_entry));
                    goto error_return;
                }
            }

            /* time to check if applying a replicated operation removed
             * the RDN value from the entry. Assuming that only replicated update
             * can lead to that bad result
             */
            if (entry_get_rdn_mods(pb, ec->ep_entry, opcsn, is_replicated_operation, &smods_add_rdn)) {
                goto error_return;
            }

            /* check that the entry still obeys the schema */
            if (slapi_entry_schema_check(pb, ec->ep_entry) != 0) {
                ldap_result_code = LDAP_OBJECT_CLASS_VIOLATION;
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                goto error_return;
            }

            /* Check attribute syntax if any new values are being added for the new RDN */
            if (slapi_mods_get_num_mods(&smods_operation_wsi) > 0) {
                if (slapi_mods_syntax_check(pb, smods_generated_wsi.mods, 0) != 0) {
                    ldap_result_code = LDAP_INVALID_SYNTAX;
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
            }

            /*
             * Update the DN CSN of the entry.
             */
            entry_add_dncsn(ec->ep_entry, operation_get_csn(operation));
            entry_add_rdn_csn(ec->ep_entry, operation_get_csn(operation));

            /*
             * If the entry has a new superior then the subordinate count
             * of the parents must be updated.
             */
            if (slapi_sdn_get_dn(dn_newsuperiordn) != NULL) {
                /*
                 * Update the subordinate count of the parents to reflect the moved child.
                 */
                if (parententry) {
                    retval = parent_update_on_childchange(&parent_modify_context,
                                                          PARENTUPDATE_DEL, NULL);
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                                  "conn=%" PRIu64 " op=%d parent_update_on_childchange: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                                  conn_id, op_id, parent_modify_context.old_entry, parent_modify_context.new_entry, retval);

                    /* The parent modify context now contains info needed later */
                    if (retval) {
                        goto error_return;
                    }
                }
                if (newparententry) {
                    retval = parent_update_on_childchange(&newparent_modify_context,
                                                          PARENTUPDATE_ADD, NULL);
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                                  "conn=%" PRIu64 " op=%d parent_update_on_childchange: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                                  conn_id, op_id, parent_modify_context.old_entry, parent_modify_context.new_entry, retval);
                    /* The newparent modify context now contains info needed later */
                    if (retval) {
                        goto error_return;
                    }
                }
            }
            /* is_resurect_operation case, there's no new superior.  Just rename. */
            if (is_resurect_operation && parententry) {
                retval = parent_update_on_childchange(&parent_modify_context, PARENTUPDATE_RESURECT, NULL);
                if (retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                                  "conn=%" PRIu64 " op=%d parent_update_on_childchange parent %s of %s failed, rc=%d\n",
                                  conn_id, op_id,
                                  slapi_entry_get_dn_const(parent_modify_context.old_entry->ep_entry),
                                  slapi_entry_get_dn_const(ec->ep_entry), retval);
                    goto error_return;
                }
            }

            /*
             * If the entry has children including tombstones,
             * then we're going to have to rename them all.
             */
            if (slapi_entry_has_children_ext(e->ep_entry, 1)) {
                /* JCM - This is where the subtree lock will appear */
                if (entryrdn_get_switch()) /* subtree-rename: on */
                {
                    if (is_resurect_operation) {
#if defined(DEBUG)
                        /* Get the present value of the subcount attr, or 0 if not present */
                        Slapi_Attr *read_attr = NULL;
                        int sub_count = -1;
                        if (0 == slapi_entry_attr_find(parent_modify_context.old_entry->ep_entry,
                                                       "numsubordinates", &read_attr)) {
                            /* decode the value */
                            Slapi_Value *sval;
                            slapi_attr_first_value(read_attr, &sval);
                            if (sval) {
                                const struct berval *bval = slapi_value_get_berval(sval);
                                if (bval) {
                                    sub_count = atol(bval->bv_val);
                                }
                            }
                        }
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                      "parent_update_on_childchange parent %s of %s numsub=%d\n",
                                      slapi_entry_get_dn_const(parent_modify_context.old_entry->ep_entry),
                                      slapi_entry_get_dn_const(e->ep_entry), sub_count);
#endif
                        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                                      "%s has children\n", slapi_entry_get_dn(e->ep_entry));
                    }
                    children = moddn_get_children(&txn, pb, be, e, sdn,
                                                  &child_entries, &child_dns, is_resurect_operation);
                } else {
                    children = moddn_get_children(&txn, pb, be, e, sdn,
                                                  &child_entries, NULL, 0);
                }

                /* JCM - Shouldn't we perform an access control check on all the children. */
                /* JCMREPL - But, the replication client has total rights over its subtree, so no access check needed. */
                /* JCM - A subtree move could break ACIs, static groups, and dynamic groups. */
            } else if (is_resurect_operation) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                              "%s has NO children\n", slapi_entry_get_dn(e->ep_entry));
            }

            /*
             * make copies of the originals, no need to copy the mods because
             * we have already copied them
             */
            if ((original_entry = backentry_dup(ec)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
            slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_ENTRY, &target_entry);
            if ((original_targetentry = slapi_entry_dup(target_entry)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }

            slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
            original_newrdn = slapi_ch_strdup(newrdn);
            slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &dn_newsuperiordn);
            orig_dn_newsuperiordn = slapi_sdn_dup(dn_newsuperiordn);
        } /* if (0 == retry_count) just once */

        /* call the transaction pre modrdn plugins just after creating the transaction */
        retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN);
        if (retval) {
            slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn", "SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN plugin "
                                                               "returned error code %d\n",
                          retval);
            if (SLAPI_PLUGIN_NOOP == retval) {
                not_an_error = 1;
                rc = retval = LDAP_SUCCESS;
            }
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

        /*
         * Update the indexes for the entry.
         */
        retval = modrdn_rename_entry_update_indexes(&txn, pb, li, e, &ec, &smods_generated, &smods_generated_wsi, &smods_operation_wsi, smods_add_rdn);
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            continue;
        }
        if (retval) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                          "modrdn_rename_entry_update_indexes %s --> %s failed, err=%d\n",
                          slapi_entry_get_dn(e->ep_entry), slapi_entry_get_dn(ec->ep_entry), retval);
            if (LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            goto error_return;
        }

        /*
         * add new name to index
         */
        {
            char **rdns;
            int i;
            if ((rdns = slapi_ldap_explode_rdn(slapi_sdn_get_dn(&dn_newrdn), 0)) != NULL) {
                for (i = 0; rdns[i] != NULL; i++) {
                    char *type;
                    Slapi_Value *svp[2] = {0};
                    /* Have to use long form init due to presence of internal struct */
                    Slapi_Value sv = {{0}, 0, 0};
                    if (slapi_rdn2typeval(rdns[i], &type, &sv.bv) != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                      "rdn2typeval (%s) failed\n", rdns[i]);
                        if (LDBM_OS_ERR_IS_DISKFULL(retval))
                            disk_full = 1;
                        MOD_SET_ERROR(ldap_result_code,
                                      LDAP_OPERATIONS_ERROR, retry_count);
                        goto error_return;
                    }
                    svp[0] = &sv;
                    svp[1] = NULL;
                    retval = index_addordel_values_sv(be, type, svp, NULL, ec->ep_id, BE_INDEX_ADD, &txn);
                    if (DBI_RC_RETRY == retval) {
                        /* To retry txn, once break "for loop" */
                        break;
                    } else if (retval != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                      "Could not add new value to index, err=%d %s\n",
                                      retval, (msg = dblayer_strerror(retval)) ? msg : "");
                        if (LDBM_OS_ERR_IS_DISKFULL(retval))
                            disk_full = 1;
                        MOD_SET_ERROR(ldap_result_code,
                                      LDAP_OPERATIONS_ERROR, retry_count);
                        goto error_return;
                    }
                }
                slapi_ldap_value_free(rdns);
                if (DBI_RC_RETRY == retval) {
                    /* Retry txn */
                    continue;
                }
            }
        }
        if (slapi_sdn_get_dn(dn_newsuperiordn) != NULL) {
            /* Push out the db modifications from the parent entry */
            retval = modify_update_all(be, pb, &parent_modify_context, &txn);
            if (DBI_RC_RETRY == retval) {
                /* Retry txn */
                continue;
            } else if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                              "Could not update parent, err=%d %s\n", retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                MOD_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
            /* Push out the db modifications from the new parent entry */
            else /* retval == 0 */
            {
                retval = modify_update_all(be, pb, &newparent_modify_context, &txn);
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                              "conn=%" PRIu64 " op=%d modify_update_all: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                              conn_id, op_id, parent_modify_context.old_entry, parent_modify_context.new_entry, retval);
                if (DBI_RC_RETRY == retval) {
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                                  "could not update parent, err=%d %s\n", retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    MOD_SET_ERROR(ldap_result_code,
                                  LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
        }

        /*
         * Update ancestorid index.
         */
        if (slapi_sdn_get_dn(dn_newsuperiordn) != NULL) {
            retval = ldbm_ancestorid_move_subtree(be, sdn, &dn_newdn, e->ep_id, children, &txn);
            if (retval != 0) {
                if (retval == DBI_RC_RETRY) {
                    continue;
                }
                if (retval == DBI_RC_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                MOD_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }
        /*
         * Update entryrdn index
         */
        if (entryrdn_get_switch()) /* subtree-rename: on */
        {
            const Slapi_DN *oldsdn = slapi_entry_get_sdn_const(e->ep_entry);
            Slapi_RDN newsrdn;
            slapi_rdn_init_sdn(&newsrdn, (const Slapi_DN *)&dn_newdn);
            retval = entryrdn_rename_subtree(be, oldsdn, &newsrdn,
                                             (const Slapi_DN *)dn_newsuperiordn,
                                             e->ep_id, &txn, is_tombstone);
            slapi_rdn_done(&newsrdn);
            if (retval != 0) {
                if (retval == DBI_RC_RETRY) {
                    continue;
                }
                if (retval == DBI_RC_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                              "entryrdn_rename_subtree failed (%d); dn: %s, newsrdn: %s, dn_newsuperiordn: %s\n",
                              retval, slapi_sdn_get_dn(sdn), slapi_rdn_get_rdn(&newsrdn),
                              slapi_sdn_get_dn(dn_newsuperiordn));
                goto error_return;
            }
        }

        /*
         * If the entry has children, then rename them all.
         */
        if (!entryrdn_get_switch() && children) /* subtree-rename: off */
        {
            retval = moddn_rename_children(&txn, pb, be, children, sdn,
                                           &dn_newdn, child_entries);
        }

        /* Update "dsEntryDN" */
        if (children) {
            retval = dsentrydn_moddn_rename(&txn, be, e->ep_id, children, sdn,
                                            &dn_newdn, child_entries);
        }
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            continue;
        }
        if (retval != 0) {
            if (retval == DBI_RC_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval))
                disk_full = 1;
            MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            goto error_return;
        }

        if (!is_ruv && !is_fixup_operation && !NO_RUV_UPDATE(li)) {
            ruv_c_init = ldbm_txn_ruv_modify_context(pb, &ruv_c);
            if (-1 == ruv_c_init) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
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
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                              "modify_update_all failed, err=%d %s\n", retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                }
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
        }

        break; /* retval==0, Done, Terminate the loop */
    }
    if (retry_count == RETRY_TIMES) {
        /* Failed */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                      "Retry count exceeded in modrdn\n");
        ldap_result_code = LDAP_BUSY;
        goto error_return;
    }

    postentry = slapi_entry_dup(ec->ep_entry);

    if (parententry != NULL) {
        modify_switch_entries(&parent_modify_context, be);
    }
    if (newparententry != NULL) {
        myrc = modify_switch_entries(&newparent_modify_context, be);
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                      "conn=%" PRIu64 " op=%d modify_switch_entries: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                      conn_id, op_id, parent_modify_context.old_entry, parent_modify_context.new_entry, myrc);
    }

    if (retval == 0 && opcsn != NULL && !is_fixup_operation) {
        slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN,
                         slapi_ch_strdup(slapi_sdn_get_dn(sdn)));
    }
    slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, postentry);
    /* call the transaction post modrdn plugins just before the commit */
    if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN))) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn",
                      "SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN plugin returned error code %d\n", retval);
        if (!ldap_result_code) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        if (!ldap_result_code) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                          "SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN plugin "
                          "returned error but did not set SLAPI_RESULT_CODE\n");
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
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
    retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN);
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
        MOD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
        goto error_return;
    }

    if (children) {
        int i = 0;
        if (child_entries && *child_entries) {
            if (entryrdn_get_switch()) /* subtree-rename: on */
            {
                /*
                 * If subtree-rename is on, delete subordinate entries from the
                 * entry cache.  Next time the entries are read from the db,
                 * "renamed" dn is generated based upon the moved subtree.
                 */
                for (i = 0; child_entries[i] != NULL; i++) {
                    if (is_resurect_operation) {
                        slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_modrdn",
                                      "Calling cache remove & return %s (refcnt: %d)\n",
                                      slapi_entry_get_dn(child_entries[i]->ep_entry),
                                      child_entries[i]->ep_refcnt);
                    }
                    CACHE_REMOVE(&inst->inst_cache, child_entries[i]);
                    cache_unlock_entry(&inst->inst_cache, child_entries[i]);
                    CACHE_RETURN(&inst->inst_cache, &child_entries[i]);
                }
            } else {
                for (; child_entries[i] != NULL; i++) {
                    cache_unlock_entry(&inst->inst_cache, child_entries[i]);
                    CACHE_RETURN(&inst->inst_cache, &(child_entries[i]));
                }
            }
        }
        if (entryrdn_get_switch() && child_dns && *child_dns) {
            for (i = 0; child_dns[i] != NULL; i++) {
                CACHE_REMOVE(&inst->inst_dncache, child_dns[i]);
                CACHE_RETURN(&inst->inst_dncache, &child_dns[i]);
            }
        }
    }

    if (ruv_c_init) {
        if (modify_switch_entries(&ruv_c, be) != 0) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_modrdn",
                          "modify_switch_entries failed\n");
            goto error_return;
        }
    }

    retval = 0;
    goto common_return;

error_return:
    /* Revert the caches if this is the parent operation */
    if (parent_op) {
        revert_cache(inst, &parent_time);
    }

    /* result already sent above - just free stuff */
    if (postentry) {
        slapi_entry_free(postentry);
        postentry = NULL;
        /* make sure caller doesn't attempt to free this */
        slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, postentry);
    }
    if (children) {
        int i = 0;
        if (child_entries && *child_entries && inst) {
            if (entryrdn_get_switch()) /* subtree-rename: on */
            {
                /*
                 * If subtree-rename is on, delete subordinate entries from the
                 * entry cache even if the procedure was not successful.
                 */
                for (i = 0; child_entries[i] != NULL; i++) {
                    CACHE_REMOVE(&inst->inst_cache, child_entries[i]);
                    cache_unlock_entry(&inst->inst_cache, child_entries[i]);
                    CACHE_RETURN(&inst->inst_cache, &child_entries[i]);
                }
            } else {
                for (; child_entries[i] != NULL; i++) {
                    cache_unlock_entry(&inst->inst_cache, child_entries[i]);
                    CACHE_RETURN(&inst->inst_cache, &(child_entries[i]));
                }
            }
        }
        if (entryrdn_get_switch() && child_dns && *child_dns && inst) {
            for (i = 0; child_dns[i] != NULL; i++) {
                CACHE_REMOVE(&inst->inst_dncache, child_dns[i]);
                CACHE_RETURN(&inst->inst_dncache, &child_dns[i]);
            }
        }
    }

    if (retval == DBI_RC_RUNRECOVERY) {
        dblayer_remember_disk_filled(li);
        ldbm_nasty("ldbm_back_modrdn", "ModifyDN", 82, retval);
        disk_full = 1;
    }

    if (disk_full) {
        retval = return_on_disk_full(li);
    } else {
        /* It is safer not to abort when the transaction is not started. */
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
            /* call the transaction post modrdn plugins just before the abort */
            if ((retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN))) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn",
                              "SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN plugin "
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

                /* Revert the caches if this is the parent operation */
                if (parent_op) {
                    revert_cache(inst, &parent_time);
                }
            }
            retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN);

            /* Release SERIAL LOCK */
            dblayer_txn_abort(be, &txn); /* abort crashes in case disk full */
            /* txn is no longer valid - reset the txn pointer to the parent */
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
        }
        if (!not_an_error) {
            retval = SLAPI_FAIL_GENERAL;
        }
    }

common_return:

    /* result code could be used in the bepost plugin functions. */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    /*
     * The bepostop is called even if the operation fails.
     */
    plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_MODRDN_FN);

    /* Free up the resource we don't need any more */
    if (ec) {
        if (is_resurect_operation) {
            slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_modrdn",
                          "Resurrecting an entry %s: result: %d, %d\n",
                          slapi_entry_get_dn(ec->ep_entry), ldap_result_code, retval);
        }
        if (inst && (0 == retval) && entryrdn_get_switch()) { /* subtree-rename: on */
            /* since the op was successful, add the addingentry's dn to the dn cache */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, ec->ep_id);
            if (bdn) { /* already in the dncache */
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            } else { /* not in the dncache yet */
                Slapi_DN *ecsdn = slapi_sdn_dup(slapi_entry_get_sdn(ec->ep_entry));
                if (ecsdn) {
                    bdn = backdn_init(ecsdn, ec->ep_id, 0);
                    if (bdn) {
                        CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                        slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_modrdn",
                                      "set %s to dn cache\n", slapi_sdn_get_dn(sdn));
                    }
                }
            }
        }

        if (ec && retval) {
            /* if the operation failed, the destination entry does not exist
             * but it has been added in dncache during cache_add_tentative
             * we need to remove it. Else a retrieval from ep_id can give the wrong DN
             */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, ec->ep_id);
            slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_modrdn",
                                      "operation failed, the target entry is cleared from dncache (%s)\n", slapi_entry_get_dn(ec->ep_entry));
            CACHE_REMOVE(&inst->inst_dncache, bdn);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
        }

        if (ec && inst) {
            CACHE_RETURN(&inst->inst_cache, &ec);
        }
        ec = NULL;
    }

    if (inst) {
        if (e && entryrdn_get_switch() && (0 == retval)) {
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, e->ep_id);
            CACHE_REMOVE(&inst->inst_dncache, bdn);
            CACHE_RETURN(&inst->inst_dncache, &bdn);
        }
        if (inst->inst_ref_count) {
            slapi_counter_decrement(inst->inst_ref_count);
        }
    }

    moddn_unlock_and_return_entry(be, &e);

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
            slapi_send_ldap_result(pb, ldap_result_code, ldap_result_matcheddn,
                                   ldap_result_message, 0, NULL);
        }
    }
    slapi_mods_done(&smods_operation_wsi);
    slapi_mods_done(&smods_generated);
    slapi_mods_done(&smods_generated_wsi);
    slapi_mods_free(&smods_add_rdn);
    slapi_ch_free((void **)&child_entries);
    slapi_ch_free((void **)&child_dns);
    if (ldap_result_matcheddn && 0 != strcmp(ldap_result_matcheddn, "NULL"))
        slapi_ch_free((void **)&ldap_result_matcheddn);
    idl_free(&children);
    slapi_sdn_done(&dn_newdn);
    slapi_sdn_done(&dn_newrdn);
    slapi_sdn_done(&dn_parentdn);
    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                  "conn=%" PRIu64 " op=%d modify_term: old_entry=0x%p, new_entry=0x%p\n",
                  conn_id, op_id, parent_modify_context.old_entry, parent_modify_context.new_entry);
    myrc = modify_term(&parent_modify_context, be);
    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                  "conn=%" PRIu64 " op=%d modify_term: rc=%d\n", conn_id, op_id, myrc);
    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_modrdn",
                  "conn=%" PRIu64 " op=%d modify_term: old_entry=0x%p, new_entry=0x%p\n",
                  conn_id, op_id, newparent_modify_context.old_entry, newparent_modify_context.new_entry);
    myrc = modify_term(&newparent_modify_context, be);
    if (free_modrdn_existing_entry) {
        done_with_pblock_entry(pb, SLAPI_MODRDN_EXISTING_ENTRY);
    } else { /* owned by original_entry */
        slapi_pblock_set(pb, SLAPI_MODRDN_EXISTING_ENTRY, NULL);
    }
    done_with_pblock_entry(pb, SLAPI_MODRDN_PARENT_ENTRY);
    done_with_pblock_entry(pb, SLAPI_MODRDN_NEWPARENT_ENTRY);
    done_with_pblock_entry(pb, SLAPI_MODRDN_TARGET_ENTRY);
    slapi_ch_free_string(&original_newrdn);
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &pb_dn_newsuperiordn);
    if (pb_dn_newsuperiordn != orig_dn_newsuperiordn) {
        slapi_sdn_free(&orig_dn_newsuperiordn);
    } else {
        slapi_sdn_free(&dn_newsuperiordn);
    }
    backentry_free(&original_entry);
    backentry_free(&tmpentry);
    slapi_entry_free(original_targetentry);
    slapi_ch_free((void**)&errbuf);
    if (pb_conn) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_modrdn",
                      "leave conn=%" PRIu64 " op=%d\n",
                      pb_conn->c_connid, operation->o_opid);
    }
    if (li->li_flags & LI_LMDB_IMPL) {
        cache_clear(&inst->inst_dncache, 1);
        cache_clear(&inst->inst_cache, 0);
    }
    return retval;
}


/* dsEntryDN functions */

static int
dsentrydn_moddn_rename_child(
    back_txn *ptxn,
    Slapi_Backend *be,
    struct ldbminfo *li,
    struct backentry *e,
    struct backentry **ec,
    size_t parentdncomps,
    char **newsuperiordns,
    size_t newsuperiordncomps)
{
    /*
     * Construct the new DN for the entry by taking the old DN
     * excluding the old parent entry DN, and adding the new
     * superior entry DN.
     */
    int retval = 0;
    char *olddn;
    char *newdn;
    char **olddns;
    size_t olddncomps = 0;
    int need = 1; /* For the '\0' */
    size_t i;

    olddn = slapi_entry_attr_get_charptr((*ec)->ep_entry, SLAPI_ATTR_DS_ENTRYDN);
    if (NULL == olddn) {
        return retval;
    }
    olddns = slapi_ldap_explode_dn(olddn, 0);
    if (NULL == olddns) {
        goto out;
    }
    for (; olddns[olddncomps] != NULL; olddncomps++);

    for (i = 0; i < olddncomps - parentdncomps; i++) {
        need += strlen(olddns[i]) + 1; /* For the "," */
    }
    for (i = 0; i < newsuperiordncomps; i++) {
        need += strlen(newsuperiordns[i]) + 1; /* For the " " */
    }
    need--; /* We don't have a comma on the end of the last component */
    newdn = slapi_ch_malloc(need);
    newdn[0] = '\0';
    for (i = 0; i < olddncomps - parentdncomps; i++) {
        strcat(newdn, olddns[i]);
        strcat(newdn, ",");
    }
    slapi_ldap_value_free(olddns);

    for (i = 0; i < newsuperiordncomps; i++) {
        strcat(newdn, newsuperiordns[i]);
        if (i < newsuperiordncomps - 1) {
            /* We don't have a comma on the end of the last component */
            strcat(newdn, ",");
        }
    }

    retval = dsentrydn_modrdn_update(be, newdn, e, ec, ptxn);

out:
    slapi_ch_free_string(&olddn);

    return retval;
}

static int
dsentrydn_moddn_rename(
    back_txn *ptxn,
    backend *be,
    ID id,
    IDList *children,
    const Slapi_DN *dn_parentdn,
    const Slapi_DN *dn_newsuperiordn,
    struct backentry *child_entries[])
{
    /* Iterate over the children list renaming every child */
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    struct backentry **child_entry_copies = NULL;
    int retval = 0;
    char **newsuperiordns = NULL;
    size_t newsuperiordncomps = 0;
    size_t parentdncomps = 0;
    NIDS nids = children ? children->b_nids : 0;

    /*
     * Break down the parent entry dn into its components.
     */
    {
        char **parentdns = slapi_ldap_explode_dn(slapi_sdn_get_dn(dn_parentdn), 0);
        if (parentdns) {
            for (; parentdns[parentdncomps] != NULL; parentdncomps++)
                ;
            slapi_ldap_value_free(parentdns);
        } else {
            return -1;
        }
    }

    /*
     * Break down the new superior entry dn into its components.
     */
    newsuperiordns = slapi_ldap_explode_dn(slapi_sdn_get_dn(dn_newsuperiordn), 0);
    if (newsuperiordns) {
        for (; newsuperiordns[newsuperiordncomps] != NULL; newsuperiordncomps++)
            ;
    } else {
        return -1;
    }

    /*
     * Iterate over the child entries renaming them.
     */
    child_entry_copies = (struct backentry **)slapi_ch_calloc(sizeof(struct backentry *), nids + 1);
    for (size_t i = 0; i <= nids; i++) {
        child_entry_copies[i] = backentry_dup(child_entries[i]);
    }
    for (size_t i = 0; retval == 0 && child_entries[i]; i++) {
        retval = dsentrydn_moddn_rename_child(ptxn, be, li, child_entries[i], &child_entry_copies[i],
                                              parentdncomps, newsuperiordns,
                                              newsuperiordncomps);
    }

    if (0 == retval) {
        for (size_t i = 0; child_entries[i]; i++) {
            CACHE_REMOVE(&inst->inst_cache, child_entry_copies[i]);
            CACHE_RETURN(&inst->inst_cache, &(child_entry_copies[i]));
        }
    } else {
        /* failure */
        for (size_t i = 0; child_entries[i]; i++) {
            backentry_free(&(child_entry_copies[i]));
        }
    }

    slapi_ch_free((void **)&child_entry_copies);
    slapi_ldap_value_free(newsuperiordns);

    return retval;
}

static int32_t
dsentrydn_modrdn_update(
    backend *be,
    const char *newdn,
    struct backentry *e,
    struct backentry **ec,
    back_txn *txn)
{
    int32_t ret = 0;
    int32_t cache_rc = 0;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    /* We have the entry, so update id2entry */
    slapi_entry_attr_set_charptr((*ec)->ep_entry, SLAPI_ATTR_DS_ENTRYDN, newdn);
    slapi_entry_set_dn((*ec)->ep_entry, (char *)newdn);
    ret = id2entry_add_ext(be, *ec, txn, 1, &cache_rc);
    if (cache_rc) {
        slapi_log_err(SLAPI_LOG_CACHE,
                      "dsentrydn_modrdn_update",
                      "Adding %s failed to add to the cache (rc: %d, cache_rc: %d)\n",
                      slapi_entry_get_dn(e->ep_entry), ret, cache_rc);
    }
    if (DBI_RC_RETRY == ret) {
        /* Retry txn */
        slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes",
                      "id2entry_add deadlock\n");
        goto out;
    }
    if (ret != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "modrdn_rename_entry_update_indexes",
                      "id2entry_add failed, err=%d\n",
                      ret);
        goto out;
    }

    if (cache_replace(&inst->inst_cache, e, *ec) != 0) {
        /* id2entry was updated, so update the cache */
        slapi_log_err(SLAPI_LOG_CACHE,
                      "dsentrydn_modrdn_update", "cache_replace %s -> %s failed\n",
                      slapi_entry_get_dn(e->ep_entry), slapi_entry_get_dn((*ec)->ep_entry));
        ret = -1;
    }

out:
    return ret;
}

/*
 * Work out what the new DN of the entry will be.
 */
static const char *
moddn_get_newdn(Slapi_PBlock *pb, Slapi_DN *dn_olddn, Slapi_DN *dn_newrdn, Slapi_DN *dn_newsuperiordn, int is_tombstone)
{
    char *newdn;
    const char *newrdn = slapi_sdn_get_dn(dn_newrdn);
    const char *newsuperiordn = slapi_sdn_get_dn(dn_newsuperiordn);

    if (newsuperiordn != NULL) {
        /* construct the new dn */
        if (slapi_dn_isroot(newsuperiordn)) {
            newdn = slapi_ch_strdup(newrdn);
        } else {
            newdn = slapi_dn_plus_rdn(newsuperiordn, newrdn); /* JCM - Use Slapi_RDN */
        }
    } else {
        /* construct the new dn */
        const char *dn = slapi_sdn_get_dn((const Slapi_DN *)dn_olddn);
        if (slapi_dn_isbesuffix(pb, dn)) {
            newdn = slapi_ch_strdup(newrdn);
        } else {
            /* no need to free this pdn. */
            const char *pdn = slapi_dn_find_parent_ext(dn, is_tombstone);
            if (pdn) {
                newdn = slapi_dn_plus_rdn(pdn, newrdn);
            } else {
                newdn = slapi_ch_strdup(newrdn);
            }
        }
    }
    return newdn;
}

/*
 * Return the entries to the cache.
 */
static void
moddn_unlock_and_return_entry(
    backend *be,
    struct backentry **targetentry)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    /* Something bad happened so we should give back all the entries */
    if (*targetentry != NULL) {
        cache_unlock_entry(&inst->inst_cache, *targetentry);
        if (cache_is_in_cache(&inst->inst_cache, *targetentry)) {
            CACHE_REMOVE(&inst->inst_cache, *targetentry);
        }
        CACHE_RETURN(&inst->inst_cache, targetentry);
        *targetentry = NULL;
    }
}


/*
 * JCM - There was a problem with multi-valued RDNs where
 * JCM - there was an intersection of the two sets RDN Components
 * JCM - and the deleteoldrdn flag was set. A value was deleted
 * JCM - but not re-added because the value is found to already
 * JCM - exist.
 *
 * This function returns 1 if it is necessary to add an RDN value
 * to the entry.  This is necessary if either:
 * 1 the attribute or the value is not present in the entry, or
 * 2 the attribute is present, deleteoldrdn is set, and the RDN value
 *   is in the deleted list.
 *
 * For example, suppose you rename cn=a to cn=a+sn=b.  The cn=a value
 * is removed from the entry and then readded.
 */

static int
moddn_rdn_add_needed(
    struct backentry *ec,
    char *type,
    struct berval *bvp,
    int deleteoldrdn,
    Slapi_Mods *smods_wsi)
{
    Slapi_Attr *attr;
    LDAPMod *mod;

    if (slapi_entry_attr_find(ec->ep_entry, type, &attr) != 0 ||
        slapi_attr_value_find(attr, bvp) != 0) {
        return 1;
    }

    if (deleteoldrdn == 0)
        return 0;

    /* in a multi-valued RDN, the RDN value might have been already
     * put on the smods_wsi list to be deleted, yet might still be
     * in the target RDN.
     */

    for (mod = slapi_mods_get_first_mod(smods_wsi);
         mod != NULL;
         mod = slapi_mods_get_next_mod(smods_wsi)) {
        if (SLAPI_IS_MOD_DELETE(mod->mod_op) &&
            (strcasecmp(mod->mod_type, type) == 0) &&
            (mod->mod_bvalues != NULL) &&
            (slapi_attr_value_cmp(attr, *mod->mod_bvalues, bvp) == 0)) {
            return 1;
        }
    }

    return 0;
}

/*
 * Build the list of modifications to apply to the Existing Entry
 * With State Information:
 * - delete old rdn values from the entry if deleteoldrdn is set
 * - add new rdn values to the entry
 * Without State Information
 * - No changes
 */
static int
moddn_newrdn_mods(Slapi_PBlock *pb, const char *olddn, struct backentry *ec, Slapi_Mods *smods_wsi, int is_repl_op)
{
    char **rdns = NULL;
    char **dns = NULL;
    int deleteoldrdn;
    char *type = NULL;
    char *dn = NULL;
    char *newrdn = NULL;
    int i;
    struct berval *bvps[2];
    struct berval bv;

    bvps[0] = &bv;
    bvps[1] = NULL;

    /* slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn ); */
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
    slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &deleteoldrdn);


    /*
     * This loop removes the old RDN of the existing entry.
     */
    if (deleteoldrdn) {
        int baddn = 0;  /* set to true if could not parse dn */
        int badrdn = 0; /* set to true if could not parse rdn */
        dn = slapi_ch_strdup(olddn);
        dns = slapi_ldap_explode_dn(dn, 0);
        if (dns != NULL) {
            rdns = slapi_ldap_explode_rdn(dns[0], 0);
            if (rdns != NULL) {
                for (i = 0; rdns[i] != NULL; i++) {
                    /* delete from entry attributes */
                    if (deleteoldrdn && slapi_rdn2typeval(rdns[i], &type, &bv) == 0) {
                        /* check if user is allowed to modify the specified attribute */
                        /*
                     * It would be better to do this check in the front end
                     * end inside op_shared_rename(), but unfortunately we
                     * don't have access to the target entry there.
                     */
                        if (!op_shared_is_allowed_attr(type, is_repl_op)) {
                            slapi_ldap_value_free(rdns);
                            slapi_ldap_value_free(dns);
                            slapi_ch_free_string(&dn);
                            return LDAP_UNWILLING_TO_PERFORM;
                        }
                        if (strcasecmp(type, SLAPI_ATTR_UNIQUEID) != 0)
                            slapi_mods_add_modbvps(smods_wsi, LDAP_MOD_DELETE, type, bvps);
                    }
                }
                slapi_ldap_value_free(rdns);
            } else {
                badrdn = 1;
            }
            slapi_ldap_value_free(dns);
        } else {
            baddn = 1;
        }
        slapi_ch_free_string(&dn);

        if (baddn || badrdn) {
            slapi_log_err(SLAPI_LOG_TRACE, "moddn_newrdn_mods", "Failed: olddn=%s baddn=%d badrdn=%d\n",
                          olddn, baddn, badrdn);
            return LDAP_OPERATIONS_ERROR;
        }
    }
    /*
     * add new RDN values to the entry (non-normalized)
     */
    rdns = slapi_ldap_explode_rdn(newrdn, 0);
    if (rdns != NULL) {
        for (i = 0; rdns[i] != NULL; i++) {
            if (slapi_rdn2typeval(rdns[i], &type, &bv) != 0) {
                continue;
            }

            /* add to entry if it's not already there or if was
             * already deleted
             */
            if (moddn_rdn_add_needed(ec, type, &bv,
                                     deleteoldrdn,
                                     smods_wsi) == 1) {
                slapi_mods_add_modbvps(smods_wsi, LDAP_MOD_ADD, type, bvps);
            }
        }
        slapi_ldap_value_free(rdns);
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "moddn_newrdn_mods", "Failed: could not parse new rdn %s\n",
                      newrdn);
        return LDAP_OPERATIONS_ERROR;
    }

    return LDAP_SUCCESS;
}

static void
mods_remove_nsuniqueid(Slapi_Mods *smods)
{
    int i;

    LDAPMod **mods = slapi_mods_get_ldapmods_byref(smods);
    for (i = 0; mods[i] != NULL; i++) {
        if (!strcasecmp(mods[i]->mod_type, SLAPI_ATTR_UNIQUEID)) {
            mods[i]->mod_op = LDAP_MOD_IGNORE;
        }
    }
}


/*
 * Update the indexes to reflect the DN change made.
 * e is the entry before, ec the entry after.
 * mods contains the list of attribute change made.
 */
static int
modrdn_rename_entry_update_indexes(back_txn *ptxn, Slapi_PBlock *pb, struct ldbminfo *li __attribute__((unused)), struct backentry *e, struct backentry **ec, Slapi_Mods *smods1, Slapi_Mods *smods2, Slapi_Mods *smods3, Slapi_Mods *smods4)
{
    backend *be;
    ldbm_instance *inst;
    int retval = 0;
    const char *msg;
    Slapi_Operation *operation;
    int is_ruv = 0; /* True if the current entry is RUV */
    int cache_rc = 0;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    inst = (ldbm_instance *)be->be_instance_info;

    /*
     * Update the ID to Entry index.
     * Note that id2entry_add replaces the entry, so the Entry ID stays the same.
     */
    retval = id2entry_add_ext(be, *ec, ptxn, 1, &cache_rc);
    if (cache_rc) {
        slapi_log_err(SLAPI_LOG_CACHE,
                      "modrdn_rename_entry_update_indexes",
                      "Adding %s failed to add to the cache (rc: %d, cache_rc: %d)\n",
                      slapi_entry_get_dn(e->ep_entry), retval, cache_rc);
    }
    if (DBI_RC_RETRY == retval) {
        /* Retry txn */
        slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes", "id2entry_add deadlock\n");
        goto error_return;
    }
    if (retval != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "modrdn_rename_entry_update_indexes",
                      "id2entry_add failed, err=%d %s\n",
                      retval, (msg = dblayer_strerror(retval)) ? msg : "");
        goto error_return;
    }
    if (smods1 != NULL && slapi_mods_get_num_mods(smods1) > 0) {
        /*
         * update the indexes: lastmod, rdn, etc.
         */
        retval = index_add_mods(be, slapi_mods_get_ldapmods_byref(smods1), e, *ec, ptxn);
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes", "index_add_mods1 deadlock\n");
            goto error_return;
        }
        if (retval != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "modrdn_rename_entry_update_indexes",
                          "index_add_mods 1 failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            goto error_return;
        }
    }
    if (smods2 != NULL && slapi_mods_get_num_mods(smods2) > 0) {
        /*
         * smods2 contains the state generated mods. One of them might be the removal of a "nsuniqueid" rdn component
         * previously gnerated through a conflict resolution. We need to make sure we don't remove the index for "nsuniqueid"
         * so let's get it out from the mods before calling index_add_mods...
         */
        mods_remove_nsuniqueid(smods2);
        /*
         * update the indexes: lastmod, rdn, etc.
         */
        retval = index_add_mods(be, slapi_mods_get_ldapmods_byref(smods2), e, *ec, ptxn);
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes",
                          "index_add_mods2 deadlock\n");
            goto error_return;
        }
        if (retval != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "modrdn_rename_entry_update_indexes",
                          "index_add_mods 2 failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            goto error_return;
        }
    }
    if (smods3 != NULL && slapi_mods_get_num_mods(smods3) > 0) {
        /*
         * update the indexes: lastmod, rdn, etc.
         */
        retval = index_add_mods(be, slapi_mods_get_ldapmods_byref(smods3), e, *ec, ptxn);
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes",
                          "index_add_mods3 deadlock\n");
            goto error_return;
        }
        if (retval != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "modrdn_rename_entry_update_indexes",
                          "index_add_mods 3 failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            goto error_return;
        }
    }
    if (smods4 != NULL && slapi_mods_get_num_mods(smods4) > 0) {
        /*
         * update the indexes: lastmod, rdn, etc.
         */
        retval = index_add_mods(be, slapi_mods_get_ldapmods_byref(smods4), e, *ec, ptxn);
        if (DBI_RC_RETRY == retval) {
            /* Retry txn */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes",
                          "index_add_mods4 deadlock\n");
            goto error_return;
        }
        if (retval != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "modrdn_rename_entry_update_indexes",
                          "index_add_mods 4 failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            goto error_return;
        }
    }
    /*
     * Remove the old entry from the Virtual List View indexes.
     * Add the new entry to the Virtual List View indexes.
     * If ruv, we don't have to update vlv.
     */
    if (!is_ruv) {
        retval = vlv_update_all_indexes(ptxn, be, pb, e, *ec);
        if (DBI_RC_RETRY == retval) {
            /* Abort and re-try */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "modrdn_rename_entry_update_indexes",
                          "vlv_update_all_indexes deadlock\n");
            goto error_return;
        }
        if (retval != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "modrdn_rename_entry_update_indexes",
                          "vlv_update_all_indexes failed, err=%d %s\n",
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            goto error_return;
        }
    }
    if (cache_replace(&inst->inst_cache, e, *ec) != 0) {
        slapi_log_err(SLAPI_LOG_CACHE,
                      "modrdn_rename_entry_update_indexes", "cache_replace %s -> %s failed\n",
                      slapi_entry_get_dn(e->ep_entry), slapi_entry_get_dn((*ec)->ep_entry));
        retval = -1;
        goto error_return;
    }
error_return:
    return retval;
}

static int
moddn_rename_child_entry(
    back_txn *ptxn,
    Slapi_PBlock *pb,
    struct ldbminfo *li,
    struct backentry *e,
    struct backentry **ec,
    int parentdncomps,
    char **newsuperiordns,
    int newsuperiordncomps,
    CSN *opcsn)
{
    /*
     * Construct the new DN for the entry by taking the old DN
     * excluding the old parent entry DN, and adding the new
     * superior entry DN.
     *
     * slapi_ldap_explode_dn is probably a bit slow, but it knows about
     * DN escaping which is pretty complicated, and we wouldn't
     * want to reimplement that here.
     *
     * JCM - This was written before Slapi_RDN... so this could be made much neater.
     */
    int retval = 0;
    char *olddn;
    char *newdn;
    char **olddns;
    int olddncomps = 0;
    int need = 1; /* For the '\0' */
    int i;
    olddn = slapi_entry_get_dn((*ec)->ep_entry);
    if (NULL == olddn) {
        return retval;
    }
    olddns = slapi_ldap_explode_dn(olddn, 0);
    if (NULL == olddns) {
        return retval;
    }
    for (; olddns[olddncomps] != NULL; olddncomps++)
        ;
    for (i = 0; i < olddncomps - parentdncomps; i++) {
        need += strlen(olddns[i]) + 2; /* For the ", " */
    }
    for (i = 0; i < newsuperiordncomps; i++) {
        need += strlen(newsuperiordns[i]) + 2; /* For the ", " */
    }
    need--; /* We don't have a comma on the end of the last component */
    newdn = slapi_ch_malloc(need);
    newdn[0] = '\0';
    for (i = 0; i < olddncomps - parentdncomps; i++) {
        strcat(newdn, olddns[i]);
        strcat(newdn, ", ");
    }
    for (i = 0; i < newsuperiordncomps; i++) {
        strcat(newdn, newsuperiordns[i]);
        if (i < newsuperiordncomps - 1) {
            /* We don't have a comma on the end of the last component */
            strcat(newdn, ", ");
        }
    }
    slapi_ldap_value_free(olddns);
    slapi_entry_set_dn((*ec)->ep_entry, newdn);
    /* add the entrydn operational attributes */
    add_update_entrydn_operational_attributes(*ec);

    /*
     * Update the DN CSN of the entry.
     */
    {
        entry_add_dncsn(e->ep_entry, opcsn);
        entry_add_rdn_csn(e->ep_entry, opcsn);
        entry_set_maxcsn(e->ep_entry, opcsn);
    }
    {
        Slapi_Mods smods = {0};
        Slapi_Mods *smodsp = NULL;
        slapi_mods_init(&smods, 2);
        slapi_mods_add(&smods, LDAP_MOD_DELETE, LDBM_ENTRYDN_STR,
                       strlen(backentry_get_ndn(e)), backentry_get_ndn(e));
        slapi_mods_add(&smods, LDAP_MOD_REPLACE, LDBM_ENTRYDN_STR,
                       strlen(backentry_get_ndn(*ec)), backentry_get_ndn(*ec));
        smodsp = &smods;
        /*
         * Update all the indexes.
         */
        retval = modrdn_rename_entry_update_indexes(ptxn, pb, li, e, ec,
                                                    smodsp, NULL, NULL, NULL);
        /* JCMREPL - Should the children get updated modifiersname and lastmodifiedtime? */
        slapi_mods_done(&smods);
    }
    return retval;
}

/*
 * Rename all the children of an entry who's name has changed.
 * Called if "subtree-rename: off"
 */
static int
moddn_rename_children(
    back_txn *ptxn,
    Slapi_PBlock *pb,
    backend *be,
    IDList *children,
    Slapi_DN *dn_parentdn,
    Slapi_DN *dn_newsuperiordn,
    struct backentry *child_entries[])
{
    /* Iterate over the children list renaming every child */
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    Slapi_Operation *operation;
    CSN *opcsn;
    int retval = -1;
    uint i = 0;
    char **newsuperiordns = NULL;
    int newsuperiordncomps = 0;
    int parentdncomps = 0;
    NIDS nids = children ? children->b_nids : 0;
    struct backentry **child_entry_copies = NULL;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    /*
     * Break down the parent entry dn into its components.
     */
    {
        char **parentdns = slapi_ldap_explode_dn(slapi_sdn_get_dn(dn_parentdn), 0);
        if (parentdns) {
            for (; parentdns[parentdncomps] != NULL; parentdncomps++)
                ;
            slapi_ldap_value_free(parentdns);
        } else {
            return retval;
        }
    }

    /*
     * Break down the new superior entry dn into its components.
     */
    newsuperiordns = slapi_ldap_explode_dn(slapi_sdn_get_dn(dn_newsuperiordn), 0);
    if (newsuperiordns) {
        for (; newsuperiordns[newsuperiordncomps] != NULL; newsuperiordncomps++)
            ;
    } else {
        return retval;
    }

    /* probably, only if "subtree-rename is off */
    child_entry_copies =
        (struct backentry **)slapi_ch_calloc(sizeof(struct backentry *), nids + 1);
    for (i = 0; i <= nids; i++) {
        child_entry_copies[i] = backentry_dup(child_entries[i]);
    }

    /*
     * Iterate over the child entries renaming them.
     */
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    opcsn = operation_get_csn(operation);
    for (i = 0, retval = 0; retval == 0 && child_entries[i] && child_entry_copies[i]; i++) {
        retval = moddn_rename_child_entry(ptxn, pb, li, child_entries[i],
                                          &child_entry_copies[i], parentdncomps,
                                          newsuperiordns, newsuperiordncomps,
                                          opcsn);
    }
    if (0 == retval) /* success */
    {
        CACHE_REMOVE(&inst->inst_cache, child_entry_copies[i]);
        CACHE_RETURN(&inst->inst_cache, &(child_entry_copies[i]));
    } else /* failure */
    {
        while (child_entries[i] != NULL) {
            backentry_free(&(child_entry_copies[i]));
            i++;
        }
    }
    slapi_ldap_value_free(newsuperiordns);
    slapi_ch_free((void **)&child_entry_copies);
    return retval;
}

/*
 * Get an IDList of all the children of an entry.
 */
static IDList *
moddn_get_children(back_txn *ptxn,
                   Slapi_PBlock *pb,
                   backend *be,
                   struct backentry *parententry,
                   Slapi_DN *dn_parentdn,
                   struct backentry ***child_entries,
                   struct backdn ***child_dns,
                   int is_resurect_operation)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    int err = 0;
    IDList *candidates;
    IDList *result_idl = NULL;
    char filterstr[20];
    Slapi_Filter *filter;
    NIDS nids;
    int entrynum = 0;
    int dnnum = 0;
    ID id;
    idl_iterator sr_current; /* the current position in the search results */
    struct backentry *e = NULL;
    struct backdn *dn = NULL;

    if (child_entries) {
        *child_entries = NULL;
    }
    if (child_dns) {
        *child_dns = NULL;
    }
    if (entryrdn_get_switch()) {
        err = entryrdn_get_subordinates(be,
                                        slapi_entry_get_sdn_const(parententry->ep_entry),
                                        parententry->ep_id, &candidates, ptxn, is_resurect_operation);
        if (err) {
            slapi_log_err(SLAPI_LOG_ERR, "moddn_get_children",
                          "entryrdn_get_subordinates returned %d\n", err);
            goto bail;
        }
    } else {
        /* Fetch a candidate list of all the entries below the entry
         * being moved */
        strcpy(filterstr, "objectclass=*");
        filter = slapi_str2filter(filterstr);
        /*
         * We used to set managedSAIT here, but because the subtree create
         * referral step is now in build_candidate_list, we can trust the filter
         * we provide here is exactly as we provide it IE no referrals involved.
         */
        candidates = subtree_candidates(pb, be, slapi_sdn_get_ndn(dn_parentdn),
                                        parententry, filter,
                                        NULL /* allids_before_scopingp */, &err);
        slapi_filter_free(filter, 1);
    }

    if (candidates) {
        Slapi_DN parentsdn = {0};
        if (is_resurect_operation) {
            slapi_sdn_get_parent(dn_parentdn, &parentsdn);
            dn_parentdn = &parentsdn;
        }

        sr_current = idl_iterator_init(candidates);
        result_idl = idl_alloc(candidates->b_nids);
        do {
            id = idl_iterator_dereference_increment(&sr_current, candidates);
            if (id != NOID) {
                int err = 0;
                e = id2entry(be, id, ptxn, &err);
                if (e != NULL) {
                    /* The subtree search will have included the parent
                     * entry in the result set */
                    if (e != parententry) {
                        /* Check that the candidate entry is really
                         * below the base. */
                        if (slapi_dn_issuffix(backentry_get_ndn(e),
                                              slapi_sdn_get_ndn(dn_parentdn))) {
                            /*
                             * The given ID list is not sorted.
                             * We have to call idl_insert instead of idl_append.
                             */
                            idl_insert(&result_idl, id);
                        }
                    }
                    CACHE_RETURN(&inst->inst_cache, &e);
                }
            }
        } while (id != NOID);
        idl_free(&candidates);
        slapi_sdn_done(&parentsdn);
    }

    nids = result_idl ? result_idl->b_nids : 0;

    if (child_entries) {
        *child_entries = (struct backentry **)slapi_ch_calloc(sizeof(struct backentry *), nids + 1);
    }
    if (child_dns) {
        *child_dns = (struct backdn **)slapi_ch_calloc(sizeof(struct backdn *), nids + 1);
    }

    sr_current = idl_iterator_init(result_idl);
    do {
        id = idl_iterator_dereference_increment(&sr_current, result_idl);
        if (id != NOID) {
            if (child_entries) {
                e = cache_find_id(&inst->inst_cache, id);
                if (e != NULL) {
                    cache_lock_entry(&inst->inst_cache, e);
                    (*child_entries)[entrynum] = e;
                    entrynum++;
                }
            }
            if (entryrdn_get_switch() && child_dns) {
                dn = dncache_find_id(&inst->inst_dncache, id);
                if (dn != NULL) {
                    (*child_dns)[dnnum] = dn;
                    dnnum++;
                }
            }
        }
    } while (id != NOID);

bail:
    return result_idl;
}
