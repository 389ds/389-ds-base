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


/* add.c - ldap ldbm back-end add routine */

#include "back-ldbm.h"

extern char *numsubordinates;
extern char *hassubordinates;

static void delete_update_entrydn_operational_attributes(struct backentry *ep);

#define ADD_SET_ERROR(rc, error, count)                                            \
    {                                                                              \
        (rc) = (error);                                                            \
        (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
    }

/* in order to find the parent, we must have either the parent dn or uniqueid
   This function will return true if either are set, or false otherwise */
static int
have_parent_address(const Slapi_DN *parentsdn, const char *parentuniqueid)
{
    if (parentuniqueid && parentuniqueid[0]) {
        return 1; /* have parent uniqueid */
    }

    if (parentsdn && !slapi_sdn_isempty(parentsdn)) {
        return 1; /* have parent dn */
    }

    return 0; /* have no address */
}

int
ldbm_back_add(Slapi_PBlock *pb)
{
    backend *be;
    struct ldbminfo *li;
    ldbm_instance *inst = NULL;
    const char *dn = NULL;
    Slapi_Entry *e = NULL;
    struct backentry *tombstoneentry = NULL;
    struct backentry *addingentry = NULL;
    struct backentry *parententry = NULL;
    struct backentry *originalentry = NULL;
    struct backentry *tmpentry = NULL;
    ID pid;
    int isroot;
    char *errbuf = NULL;
    back_txn txn = {0};
    back_txnid parent_txn;
    int retval = -1;
    const char *msg;
    int managedsait;
    int ldap_result_code = LDAP_SUCCESS;
    char *ldap_result_message = NULL;
    char *ldap_result_matcheddn = NULL;
    int retry_count = 0;
    int disk_full = 0;
    modify_context parent_modify_c = {0};
    modify_context ruv_c = {0};
    int parent_found = 0;
    int ruv_c_init = 0;
    int rc = 0;
    int addingentry_id_assigned = 0;
    Slapi_DN *sdn = NULL;
    Slapi_DN parentsdn;
    Slapi_Operation *operation;
    int is_replicated_operation = 0;
    int is_resurect_operation = 0;
    int is_cenotaph_operation = 0;
    int is_tombstone_operation = 0;
    int is_fixup_operation = 0;
    int is_remove_from_cache = 0;
    int op_plugin_call = 1;
    int is_ruv = 0; /* True if the current entry is RUV */
    CSN *opcsn = NULL;
    entry_address addr = {0};
    int not_an_error = 0;
    int is_noop = 0;
    int parent_switched = 0;
    int noabort = 1;
    int myrc = 0;
    PRUint64 conn_id;
    int op_id;
    int result_sent = 0;
    int32_t parent_op = 0;
    int32_t betxn_callback_fails = 0; /* if a BETXN fails we need to revert entry cache */
    struct timespec parent_time;

    if (slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id) < 0) {
        conn_id = 0; /* connection is NULL */
    }
    slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    slapi_pblock_get(pb, SLAPI_MANAGEDSAIT, &managedsait);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&parent_txn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    if (operation == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add", "NULL operation\n");
        return LDAP_OPERATIONS_ERROR;
    }

    is_resurect_operation = operation_is_flag_set(operation, OP_FLAG_RESURECT_ENTRY);
    is_cenotaph_operation = operation_is_flag_set(operation, OP_FLAG_CENOTAPH_ENTRY);
    is_tombstone_operation = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY);
    is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
    is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    is_remove_from_cache = operation_is_flag_set(operation, OP_FLAG_NEVER_CACHE);
    if (operation_is_flag_set(operation,OP_FLAG_NOOP)) op_plugin_call = 0;

    inst = (ldbm_instance *)be->be_instance_info;
    if (inst && inst->inst_ref_count) {
        slapi_counter_increment(inst->inst_ref_count);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                      "Instance \"%s\" does not exist.\n",
                      inst ? inst->inst_name : "null instance");
        goto error_return;
    }

    if (e == NULL){
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add", "entry is NULL.\n");
        goto error_return;
    }

    /* sdn & parentsdn need to be initialized before "goto *_return" */
    slapi_sdn_init(&parentsdn);

    /* Get rid of ldbm backend attributes that you are not allowed to specify yourself */
    slapi_entry_delete_values(e, hassubordinates, NULL);
    slapi_entry_delete_values(e, numsubordinates, NULL);

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
        if (parent_txn)
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
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
    if(SERIALLOCK(li) && !is_fixup_operation)
    {
        dblayer_lock_backend(be);
        dblock_acquired= 1;
    }
     */

    rc = 0;

    /*
     * We are about to pass the last abandon test, so from now on we are
     * committed to finish this operation. Set status to "will complete"
     * before we make our last abandon check to avoid race conditions in
     * the code that processes abandon operations.
     */
    operation->o_status = SLAPI_OP_STATUS_WILL_COMPLETE;

    if (slapi_op_abandoned(pb)) {
        ldap_result_code = -1; /* needs to distinguish from "success" */
        goto error_return;
    }

    /*
     * Originally (in the U-M LDAP 3.3 code), there was a comment near this
     * code about a race condition.  The race was that a 2nd entry could be
     * added between the time when we check for an already existing entry
     * and the cache_add_entry_lock() call below.  A race condition no
     * longer exists, because now we keep the parent entry locked for
     * the duration of the old race condition's window of opportunity.
     */

    /*
     * Use transaction as a backend lock, which should be called
     * outside of entry lock -- find_entry* / cache_lock_entry
     * to avoid deadlock.
     */
    txn.back_txn_txn = NULL; /* ready to create the child transaction */
    for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
        if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) {
            /* Don't release SERIAL LOCK */
            dblayer_txn_abort_ext(li, &txn, PR_FALSE);
            noabort = 1;
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
            /* must duplicate addingentry before returning it to cache,
             * which could free the entry. */
            if ((tmpentry = backentry_dup(originalentry ? originalentry : addingentry)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
            if (cache_is_in_cache(&inst->inst_cache, addingentry)) {
                /* addingentry is in cache.  Remove it once. */
                retval = CACHE_REMOVE(&inst->inst_cache, addingentry);
                if (retval) {
                    slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_add", "cache_remove %s failed.\n",
                                  slapi_entry_get_dn_const(addingentry->ep_entry));
                }
            }
            CACHE_RETURN(&inst->inst_cache, &addingentry);
            slapi_pblock_set(pb, SLAPI_ADD_ENTRY, originalentry->ep_entry);
            addingentry = originalentry;
            originalentry = tmpentry;
            tmpentry = NULL;
            /* Adding the resetted addingentry to the cache. */
            if (cache_add_tentative(&inst->inst_cache, addingentry, NULL) < 0) {
                slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_add", "cache_add_tentative concurrency detected: %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry));
                ldap_result_code = LDAP_ALREADY_EXISTS;
                goto error_return;
            }
            if (ruv_c_init) {
                /* reset the ruv txn stuff */
                modify_term(&ruv_c, be);
                ruv_c_init = 0;
            }

            /* We're re-trying */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add", "Add Retrying Transaction\n");
#ifndef LDBM_NO_BACKOFF_DELAY
            {
                PRIntervalTime interval;
                interval = PR_MillisecondsToInterval(slapi_rand() % 100);
                DS_Sleep(interval);
            }
#endif
        }
        /* dblayer_txn_begin holds SERIAL lock,
         * which should be outside of locking the entry (find_entry2modify) */
        if (0 == retry_count) {
            /* First time, hold SERIAL LOCK */
            retval = dblayer_txn_begin(be, parent_txn, &txn);
            noabort = 0;

            if (!is_tombstone_operation) {
                rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            }

            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
            while (rc != 0) {
                /* JCM - copying entries can be expensive... should optimize */
                /*
                 * Some present state information is passed through the PBlock to the
                 * backend pre-op plugin. To ensure a consistent snapshot of this state
                 * we wrap the reading of the entry with the dblock.
                 */
                if (slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY)) {
                    /* Check if an entry with the intended uniqueid already exists. */
                    done_with_pblock_entry(pb, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY); /* Could be through this multiple times */
                    addr.udn = NULL;
                    addr.sdn = NULL;
                    addr.uniqueid = (char *)slapi_entry_get_uniqueid(e); /* jcm -  cast away const */
                    ldap_result_code = get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY, !is_replicated_operation);
                }
                if (slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY)) {
                    slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
                    if (NULL == sdn) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                                      "Null target dn\n");
                        goto error_return;
                    }

                    /* not need to check the dn syntax as this is a replicated op */
                    if (!is_replicated_operation) {
                        dn = slapi_sdn_get_dn(sdn);
                        ldap_result_code = slapi_dn_syntax_check(pb, dn, 1);
                        if (ldap_result_code) {
                            ldap_result_code = LDAP_INVALID_DN_SYNTAX;
                            slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                            goto error_return;
                        }
                    }

                    /*
                     * If the parent is conflict, slapi_sdn_get_backend_parent does not support it.
                     * That is, adding children to a conflict entry is not allowed.
                     */
                    slapi_sdn_get_backend_parent(sdn, &parentsdn, be);
                    /* Check if an entry with the intended DN already exists. */
                    done_with_pblock_entry(pb, SLAPI_ADD_EXISTING_DN_ENTRY); /* Could be through this multiple times */
                    addr.sdn = sdn;
                    addr.udn = NULL;
                    addr.uniqueid = NULL;
                    ldap_result_code = get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_EXISTING_DN_ENTRY, !is_replicated_operation);
                    if (ldap_result_code == LDAP_OPERATIONS_ERROR || ldap_result_code == LDAP_INVALID_DN_SYNTAX) {
                        goto error_return;
                    }
                }
                /* if we can find the parent by dn or uniqueid, and the operation has requested the parent
                   then get it */
                if (have_parent_address(&parentsdn, operation->o_params.p.p_add.parentuniqueid) &&
                    slapi_isbitset_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY)) {
                    done_with_pblock_entry(pb, SLAPI_ADD_PARENT_ENTRY); /* Could be through this multiple times */
                    addr.sdn = &parentsdn;
                    addr.udn = NULL;
                    addr.uniqueid = operation->o_params.p.p_add.parentuniqueid;
                    ldap_result_code = get_copy_of_entry(pb, &addr, &txn, SLAPI_ADD_PARENT_ENTRY, !is_replicated_operation);
                }

                ldap_result_code = plugin_call_acl_plugin(pb, e, NULL, NULL, SLAPI_ACL_ADD,
                                                          ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
                if (ldap_result_code != LDAP_SUCCESS) {
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "no access to parent, pdn = %s\n",
                                  slapi_sdn_get_dn(&parentsdn));
                    ldap_result_message = errbuf;
                    goto error_return;
                }
                /* Call the Backend Pre Add plugins */
                ldap_result_code = LDAP_SUCCESS;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                rc = plugin_call_mmr_plugin_preop(pb, NULL,SLAPI_PLUGIN_BE_PRE_ADD_FN);
                if (rc == 0  && op_plugin_call) {
                    rc = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_ADD_FN);
                }
                if (rc == SLAPI_PLUGIN_NOOP_TOMBSTONE) {
                    is_tombstone_operation = 1;
                    is_noop = 1;
                    op_plugin_call = 0;
                    rc = LDAP_SUCCESS;
                } else if (rc < 0) {
                    int opreturn = 0;
                    if (SLAPI_PLUGIN_NOOP == rc) {
                        not_an_error = 1;
                        is_noop = 1;
                        rc = LDAP_SUCCESS;
                    }
                    /*
                     * Plugin indicated some kind of failure,
                     * or that this Operation became a No-Op.
                     */
                    if (!ldap_result_code) {
                        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                    }
                    if (!ldap_result_code) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                                      "SLAPI_PLUGIN_BE_PRE_ADD_FN returned error but did not set SLAPI_RESULT_CODE\n");
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                    }
                    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                    if (!opreturn) {
                        /* make sure opreturn is set for the postop plugins */
                        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &rc);
                    }
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
                /*
                 * (rc!=-1 && rc!= 0) means that the plugin changed things, so we go around
                 * the loop once again to get the new present state.
                 */
                /* JCMREPL - Warning: A Plugin could cause an infinite loop by always returning a result code that requires some action. */
            }
        } else {
            /* Otherwise, no SERIAL LOCK */
            retval = dblayer_txn_begin_ext(li, parent_txn, &txn, PR_FALSE);
        }
        if (0 != retval) {
            if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                disk_full = 1;
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto diskfull_return;
            }
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            goto error_return;
        }
        noabort = 0;

        /* stash the transaction for plugins */
        slapi_pblock_set(pb, SLAPI_TXN, txn.back_txn_txn);

        if (0 == retry_count) { /* execute just once */
            /* Nothing in this if crause modifies persistent store.
             * it's called just once. */
            /*
             * Fetch the parent entry and acquire the cache lock.
             */
            if (have_parent_address(&parentsdn, operation->o_params.p.p_add.parentuniqueid)) {
                addr.sdn = &parentsdn;
                addr.udn = NULL;
                addr.uniqueid = operation->o_params.p.p_add.parentuniqueid;
                parententry = find_entry2modify_only(pb, be, &addr, &txn, &result_sent);
                if (parententry && parententry->ep_entry) {
                    if (!operation->o_params.p.p_add.parentuniqueid) {
                        /* Set the parentuniqueid now */
                        operation->o_params.p.p_add.parentuniqueid =
                            slapi_ch_strdup(slapi_entry_get_uniqueid(parententry->ep_entry));
                    }
                    if (slapi_sdn_isempty(&parentsdn) ||
                        slapi_sdn_compare(&parentsdn, slapi_entry_get_sdn(parententry->ep_entry))) {
                        /* Set the parentsdn now */
                        slapi_sdn_set_dn_byval(&parentsdn, slapi_entry_get_dn_const(parententry->ep_entry));
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                                  "find_entry2modify_only returned NULL parententry pdn: %s, uniqueid: %s\n",
                                  slapi_sdn_get_dn(&parentsdn), addr.uniqueid ? addr.uniqueid : "none");
                }
                modify_init(&parent_modify_c, parententry);
            }

            /* Check if the entry we have been asked to add already exists */
            {
                Slapi_Entry *entry;
                slapi_pblock_get(pb, SLAPI_ADD_EXISTING_DN_ENTRY, &entry);
                if (entry) {
                    if (is_resurect_operation) {
                        Slapi_Entry *uniqentry;
                        slapi_pblock_get(pb, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY, &uniqentry);
                        if (uniqentry == entry) {
                            /*
                             * adding entry having the uniqueid exists.
                             * No need to resurrect.
                             */
                            ldap_result_code = LDAP_SUCCESS;
                        } else {
                            /* The entry having the DN already exists */
                            if (uniqentry) {
                                if (PL_strcmp(slapi_entry_get_uniqueid(entry),
                                              slapi_entry_get_uniqueid(uniqentry))) {
                                    /* Not match; conflict. */
                                    ldap_result_code = LDAP_ALREADY_EXISTS;
                                } else {
                                    /* Same entry; no need to resurrect. */
                                    ldap_result_code = LDAP_SUCCESS;
                                }
                            } else {
                                ldap_result_code = LDAP_ALREADY_EXISTS;
                            }
                        }
                    } else {
                        /* The entry already exists */
                        ldap_result_code = LDAP_ALREADY_EXISTS;
                    }
                    if ((LDAP_ALREADY_EXISTS == ldap_result_code) && !isroot && !is_replicated_operation) {
                        myrc = plugin_call_acl_plugin(pb, e, NULL, NULL, SLAPI_ACL_ADD,
                                                      ACLPLUGIN_ACCESS_DEFAULT, &errbuf);
                        if (myrc) {
                            ldap_result_code = myrc;
                            ldap_result_message = errbuf;
                        }
                    }
                    goto error_return;
                } else {
                    /*
                     * did not find the entry - this is good, since we're
                     * trying to add it, but we have to check whether the
                     * entry we did match has a referral we should return
                     * instead. we do this only if managedsait is not on.
                     */
                    if (!managedsait && !is_tombstone_operation && !is_resurect_operation) {
                        int err = 0;
                        Slapi_DN ancestorsdn;
                        struct backentry *ancestorentry;
                        slapi_sdn_init(&ancestorsdn);
                        ancestorentry = dn2ancestor(be, sdn, &ancestorsdn, &txn, &err, 0);
                        slapi_sdn_done(&ancestorsdn);
                        if (ancestorentry != NULL) {
                            int sentreferral = check_entry_for_referral(pb, ancestorentry->ep_entry, backentry_get_ndn(ancestorentry), "ldbm_back_add");
                            CACHE_RETURN(&inst->inst_cache, &ancestorentry);
                            if (sentreferral) {
                                ldap_result_code = -1; /* The result was sent by check_entry_for_referral */
                                goto error_return;
                            }
                        }
                    }
                }
            }

            /* no need to check the schema as this is a replication add */
            if (!is_replicated_operation) {
                if ((operation_is_flag_set(operation, OP_FLAG_ACTION_SCHEMA_CHECK)) && (slapi_entry_schema_check(pb, e) != 0)) {
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "Entry failed schema check\n");
                    ldap_result_code = LDAP_OBJECT_CLASS_VIOLATION;
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }

                /* Check attribute syntax */
                if (slapi_entry_syntax_check(pb, e, 0) != 0) {
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "Entry failed syntax check\n");
                    ldap_result_code = LDAP_INVALID_SYNTAX;
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
            }

            opcsn = operation_get_csn(operation);
            if (is_resurect_operation) {
                char *reason = NULL;
                /*
                 * When we resurect a tombstone we must use its UniqueID
                 * to find the tombstone entry and lock it down in the cache.
                 */
                addr.udn = NULL;
                addr.sdn = NULL;
                addr.uniqueid = (char *)slapi_entry_get_uniqueid(e); /* jcm - cast away const */
                tombstoneentry = find_entry2modify(pb, be, &addr, &txn, &result_sent);
                if (tombstoneentry == NULL) {
                    ldap_result_code = -1;
                    goto error_return; /* error result sent by find_entry2modify() */
                }

                addingentry = backentry_dup(tombstoneentry);
                if (addingentry == NULL) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    goto error_return;
                }
                /*
                 * To resurect a tombstone we must fix its DN and remove the
                 * parent UniqueID that we stashed in there.
                 *
                 * The entry comes back to life as a Glue entry, so we add the
                 * magic objectclass.
                 */
                if (NULL == sdn) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                                  "Null target dn\n");
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    goto error_return;
                }
                dn = slapi_sdn_get_dn(sdn);
                slapi_entry_set_sdn(addingentry->ep_entry, sdn); /* The DN is passed into the entry. */
                /* not just e_sdn, e_rsdn needs to be updated. */
                slapi_rdn_set_all_dn(slapi_entry_get_srdn(addingentry->ep_entry),
                                     slapi_entry_get_dn_const(addingentry->ep_entry));
                /* LPREPL: the DN is normalized...Somehow who should get a not normalized one */
                addingentry->ep_id = slapi_entry_attr_get_ulong(addingentry->ep_entry, "entryid");
                slapi_entry_attr_delete(addingentry->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
                slapi_entry_delete_string(addingentry->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
                slapi_entry_attr_delete(addingentry->ep_entry, SLAPI_ATTR_TOMBSTONE_CSN);
                /* Now also remove the nscpEntryDN */
                if (slapi_entry_attr_delete(addingentry->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN) != 0) {
                    slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_add", "Resurrection of %s - Couldn't remove %s\n",
                                  dn, SLAPI_ATTR_NSCP_ENTRYDN);
                }

                /* And copy the reason from e */
                reason = slapi_entry_attr_get_charptr(e, "nsds5ReplConflict");
                if (reason) {
                    if (!slapi_entry_attr_hasvalue(addingentry->ep_entry, "nsds5ReplConflict", reason)) {
                        slapi_entry_add_string(addingentry->ep_entry, "nsds5ReplConflict", reason);
                        slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_add", "Resurrection of %s - Added Conflict reason %s\n",
                                      dn, reason);
                    }
                    slapi_ch_free_string(&reason);
                }
                /* Clear the Tombstone Flag in the entry */
                slapi_entry_clear_flag(addingentry->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);

                /* make sure the objectclass
                   - does not contain any duplicate values
                   - has CSNs for the new values we added
                */
                {
                    Slapi_Attr *sa = NULL;
                    Slapi_Value sv;
                    const struct berval *svbv = NULL;

                    /* add the extensibleobject objectclass with csn if not present */
                    slapi_entry_attr_find(addingentry->ep_entry, SLAPI_ATTR_OBJECTCLASS, &sa);
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
            } else {
                /*
                 * Try to add the entry to the cache, assign it a new entryid
                 * and mark it locked.  This should only fail if the entry
                 * already exists.
                 */
                /*
                 * next_id will add this id to the list of ids that are pending
                 * id2entry indexing.
                 */
                Slapi_DN nscpEntrySDN;
                addingentry = backentry_init(e);
                if ((addingentry->ep_id = next_id(be)) >= MAXID) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add ",
                                  "Maximum ID reached, cannot add entry to "
                                  "backend '%s'",
                                  be->be_name);
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    goto error_return;
                }
                addingentry_id_assigned = 1;

                if (!is_fixup_operation) {
                    if (opcsn == NULL && operation->o_csngen_handler) {
                        /*
                         * Current op is a user request. Opcsn will be assigned
                         * if the dn is in an updatable replica.
                         */
                        if (entry_assign_operation_csn(pb, e, parententry ? parententry->ep_entry : NULL, &opcsn) != 0) {
                            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                                    "failed to generate add CSN for entry (%s), aborting operation\n",
                                    slapi_entry_get_dn(e));
                            ldap_result_code = LDAP_OPERATIONS_ERROR;
                            goto error_return;
                        }
                    }
                    if (opcsn != NULL) {
                        entry_set_csn(e, opcsn);
                        entry_add_dncsn(e, opcsn);
                        entry_add_rdn_csn(e, opcsn);
                        entry_set_maxcsn(e, opcsn);
                    }
                }

                if (is_tombstone_operation) {
                    /* Directly add the entry as a tombstone */
                    /*
                     * 1) If the entry has an existing DN, change it to be
                     *    "nsuniqueid=<uniqueid>, <old dn>"
                     * 2) Add the objectclass value "tombstone" and arrange for only
                     *    that value to be indexed.
                     * 3) If the parent entry was found, set the nsparentuniqueid
                     *    attribute to be the unique id of that parent.
                     */
                    const char *entryuniqueid= slapi_entry_get_uniqueid(addingentry->ep_entry);
                    char *untombstoned_dn = slapi_entry_get_dn(e);
                    char *tombstoned_dn = NULL;
                    if (NULL == untombstoned_dn) {
                        untombstoned_dn = "";
                    }
                    tombstoned_dn = compute_entry_tombstone_dn(untombstoned_dn, entryuniqueid);
                    slapi_log_err(SLAPI_LOG_DEBUG,
                                  "ldbm_back_add", "(tombstone_operation for %s): calculated tombstone_dn "
                                  "is (%s) \n", entryuniqueid, tombstoned_dn);
                    /*
                     * This needs to be done before slapi_entry_set_dn call,
                     * because untombstoned_dn is released in slapi_entry_set_dn.
                     */
                    slapi_sdn_init(&nscpEntrySDN);
                    slapi_sdn_set_ndn_byval(&nscpEntrySDN, slapi_sdn_get_ndn(slapi_entry_get_sdn(addingentry->ep_entry)));

                    if (entryrdn_get_switch()) {
                        if (is_ruv) {
                            Slapi_RDN srdn = {0};
                            rc = slapi_rdn_init_all_dn(&srdn, tombstoned_dn);
                            if (rc) {
                                slapi_log_err(SLAPI_LOG_TRACE,
                                              "ldbm_back_add", "(tombstone_operation): failed to "
                                              "decompose %s to Slapi_RDN\n", tombstoned_dn);
                            } else {
                                slapi_entry_set_srdn(e, &srdn);
                                slapi_rdn_done(&srdn);
                            }
                        } else {
                            /* immediate entry to tombstone */
                            Slapi_RDN *srdn = slapi_entry_get_srdn(addingentry->ep_entry);
                            slapi_rdn_init_all_sdn(srdn, slapi_entry_get_sdn_const(addingentry->ep_entry));
                            char *tombstone_rdn = compute_entry_tombstone_rdn(slapi_entry_get_rdn_const(addingentry->ep_entry),
                                                                              entryuniqueid);
                            slapi_log_err(SLAPI_LOG_DEBUG,
                                          "ldbm_back_add", "(tombstone_operation for %s): calculated tombstone_rdn "
                                          "is (%s) \n", entryuniqueid, tombstone_rdn);
                            /* e_srdn has "uniaqueid=..., <ORIG RDN>" */
                            slapi_rdn_replace_rdn(srdn, tombstone_rdn);
                            slapi_ch_free_string(&tombstone_rdn);
                        }
                    }
                    slapi_entry_set_dn(addingentry->ep_entry, tombstoned_dn);
                    /* Work around pb with slapi_entry_add_string (defect 522327)
                     * doesn't check duplicate values */
                    if (!slapi_entry_attr_hasvalue(addingentry->ep_entry,
                                                   SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE)) {
                        slapi_entry_add_string(addingentry->ep_entry,
                                               SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
                        slapi_entry_set_flag(addingentry->ep_entry,
                                             SLAPI_ENTRY_FLAG_TOMBSTONE);
                    }
                    if (!slapi_entry_attr_hasvalue(addingentry->ep_entry,
                                                   SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE)) {
                        slapi_entry_add_string(addingentry->ep_entry,
                                               SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
                        slapi_entry_set_flag(addingentry->ep_entry,
                                             SLAPI_ENTRY_FLAG_TOMBSTONE);
                    }
                    if (attrlist_find(addingentry->ep_entry->e_attrs, SLAPI_ATTR_TOMBSTONE_CSN) == NULL) {
                        const CSN *tombstone_csn = NULL;
                        char tombstone_csnstr[CSN_STRSIZE];

                        /* Add the missing nsTombstoneCSN attribute to the tombstone */
                        if ((tombstone_csn = entry_get_deletion_csn(addingentry->ep_entry))) {
                            csn_as_string(tombstone_csn, PR_FALSE, tombstone_csnstr);
                            slapi_entry_add_string(addingentry->ep_entry, SLAPI_ATTR_TOMBSTONE_CSN,
                                                   tombstone_csnstr);
                        }
                    }

                    if (attrlist_find( addingentry->ep_entry->e_attrs, SLAPI_ATTR_NSCP_ENTRYDN ) == NULL){
                        slapi_entry_add_string(addingentry->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(&nscpEntrySDN));
                    }

                    if (NULL != operation->o_params.p.p_add.parentuniqueid) {
                        slapi_entry_add_string(addingentry->ep_entry,
                                               SLAPI_ATTR_VALUE_PARENT_UNIQUEID,
                                               operation->o_params.p.p_add.parentuniqueid);
                    }
                    slapi_sdn_done(&nscpEntrySDN);
                } else {
                        /* if an entry is explicitely added as tombstone the entry flag has to be set */
                        if (slapi_entry_attr_hasvalue(addingentry->ep_entry,
                                                      SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE)) {
                            slapi_entry_set_flag(addingentry->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
                        }
                }
            }

            /*
             * Get the parent dn and see if the corresponding entry exists.
             * If the parent does not exist, only allow the "root" user to
             * add the entry.
             */
            if (!slapi_sdn_isempty(&parentsdn)) {
                /* This is getting the parent */
                if (NULL == parententry) {
                    /* Here means that we didn't find the parent */
                    int err = 0;
                    Slapi_DN ancestorsdn;
                    struct backentry *ancestorentry;

                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add", "Parent \"%s\" does not exist. "
                                                                       "It might be a conflict entry.\n",
                                  slapi_sdn_get_dn(&parentsdn));
                    slapi_sdn_init(&ancestorsdn);
                    ancestorentry = dn2ancestor(be, &parentsdn, &ancestorsdn, &txn, &err, 1);
                    CACHE_RETURN(&inst->inst_cache, &ancestorentry);

                    ldap_result_code = LDAP_NO_SUCH_OBJECT;
                    ldap_result_matcheddn =
                        slapi_ch_strdup((char *)slapi_sdn_get_dn(&ancestorsdn)); /* jcm - cast away const. */
                    slapi_sdn_done(&ancestorsdn);
                    goto error_return;
                }
                pid = parententry->ep_id;

                /* We may need to adjust the DN since parent could be a resurrected conflict entry... */
                /* TBD better handle tombstone parents,
                 * we have the entry dn as nsuniqueid=nnnn,<rdn>,parentdn
                 * so is parent will return false
                 */
                 if (!is_tombstone_operation && !slapi_sdn_isparent(slapi_entry_get_sdn_const(parententry->ep_entry),
                                                                    slapi_entry_get_sdn_const(addingentry->ep_entry))) {
                    Slapi_DN adjustedsdn = {0};
                    char *adjusteddn = slapi_ch_smprintf("%s,%s",
                                                         slapi_entry_get_rdn_const(addingentry->ep_entry),
                                                         slapi_entry_get_dn_const(parententry->ep_entry));
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add", "Adjusting dn: %s --> %s\n",
                                  slapi_entry_get_dn(addingentry->ep_entry), adjusteddn);
                    slapi_sdn_set_normdn_passin(&adjustedsdn, adjusteddn);
                    slapi_entry_set_sdn(addingentry->ep_entry, &adjustedsdn);
                    /* not just e_sdn, e_rsdn needs to be updated. */
                    slapi_rdn_set_all_dn(slapi_entry_get_srdn(addingentry->ep_entry), adjusteddn);
                    slapi_sdn_done(&adjustedsdn);
                }
            } else { /* no parent */
                if (!isroot && !is_replicated_operation) {
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "No parent & not root\n");
                    ldap_result_code = LDAP_INSUFFICIENT_ACCESS;
                    goto error_return;
                }
                parententry = NULL;
                pid = 0;
            }

            if (is_resurect_operation) {
                add_update_entrydn_operational_attributes(addingentry);
            } else if (is_tombstone_operation) {
                /* Remove the entrydn operational attributes from the addingentry */
                delete_update_entrydn_operational_attributes(addingentry);
                if (!is_ruv) {
                    add_update_entry_operational_attributes(addingentry, pid);
                }
            } else {
                /*
                 * add the parentid, entryid and entrydn operational attributes
                 */
                add_update_entry_operational_attributes(addingentry, pid);
            }
            if (is_resurect_operation && tombstoneentry && cache_is_in_cache(&inst->inst_cache, tombstoneentry)) {
                /* we need to remove the tombstone from the cacehr otherwise we have two dns with the same id */
                cache_unlock_entry(&inst->inst_cache, tombstoneentry);
                CACHE_RETURN(&inst->inst_cache, &tombstoneentry);
            }

            /* Tentatively add the entry to the cache.  We do this after adding any
             * operational attributes to ensure that the cache is sized correctly. */
            if (cache_add_tentative(&inst->inst_cache, addingentry, NULL) < 0) {
                slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_add", "cache_add_tentative concurrency detected: %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry));
                ldap_result_code = LDAP_ALREADY_EXISTS;
                goto error_return;
            }

            /*
             * Before we add the entry, find out if the syntax of the aci
             * aci attribute values are correct or not. We don't want to
             * the entry if the syntax is incorrect.
             */
            if (plugin_call_acl_verify_syntax(pb, addingentry->ep_entry, &errbuf) != 0) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "ACL syntax error: %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry));
                ldap_result_code = LDAP_INVALID_SYNTAX;
                ldap_result_message = errbuf;
                goto error_return;
            }

            /* Having decided that we're really going to do the operation, let's modify
               the in-memory state of the parent to reflect the new child (update
               subordinate count specifically */
            if (parententry) {
                int op = is_resurect_operation ? PARENTUPDATE_RESURECT : PARENTUPDATE_ADD;
                if (is_cenotaph_operation || (is_tombstone_operation && !is_ruv)) {
                    /* if we directly add a tombstone the tombstone numsubordinates have to be increased
                     * (does not apply to adding the RUV)
                     */
                    op |= PARENTUPDATE_CREATE_TOMBSTONE;
                }
                retval = parent_update_on_childchange(&parent_modify_c, op, NULL);
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                              "conn=%" PRIu64 " op=%d parent_update_on_childchange: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                              conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, retval);
                /* The modify context now contains info needed later */
                if (retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add", "parent_update_on_childchange: %s, rc=%d\n",
                                  slapi_entry_get_dn_const(addingentry->ep_entry), retval);
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    goto error_return;
                }
                parent_found = 1;
                parententry = NULL;
            }
            if ((originalentry = backentry_dup(addingentry)) == NULL) {
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
        } /* if (0 == retry_count) just once */

        /* call the transaction pre add plugins just after the to-be-added entry
         * is prepared. */
        if (op_plugin_call) {
            retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN);
            if (retval) {
                int opreturn = 0;
                if (SLAPI_PLUGIN_NOOP == retval) {
                    not_an_error = 1;
                    rc = retval = LDAP_SUCCESS;
                }
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN plugin "
                                                                "returned error code %d\n",
                              retval);
                if (!ldap_result_code) {
                    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                }
                if (!ldap_result_code) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                }
                slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                if (!opreturn) {
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
                }
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_back_add", "SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN plugin failed: %d\n",
                              ldap_result_code ? ldap_result_code : retval);
                if (retval) {
                    betxn_callback_fails = 1;
                }
                goto error_return;
            }
        }
        if (is_tombstone_operation && slapi_is_loglevel_set(SLAPI_LOG_DEBUG)) {
            int len = 0;
            const char *rs = slapi_entry_get_rdn_const(addingentry->ep_entry);
            char *es= slapi_entry2str_with_options(addingentry->ep_entry, &len, SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID);
            slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_back_add", "now adding entry: %s\n %s\n", rs?rs:"no rdn", es);
            slapi_ch_free_string(&es);
        }

        retval = id2entry_add_ext(be, addingentry, &txn, 1, &myrc);
        if (DBI_RC_RETRY == retval) {
            slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 1 DEADLOCK\n");
            /* Retry txn */
            continue;
        }
        if (retval) {
            slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "id2entry_add(%s) failed, err=%d %s\n",
                          slapi_entry_get_dn_const(addingentry->ep_entry),
                          retval, (msg = dblayer_strerror(retval)) ? msg : "");
            ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                disk_full = 1;
                goto diskfull_return;
            }
            goto error_return;
        }
        if (is_resurect_operation) {
            const CSN *tombstone_csn = NULL;
            char deletion_csn_str[CSN_STRSIZE];

            retval = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE,
                                           addingentry->ep_id, BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 2 DBI_RC_RETRY\n");
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "index_addordel_string TOMBSTONE (%s), err=%d %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry),
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                    goto diskfull_return;
                }
                goto error_return;
            }

            /* Need to update the nsTombstoneCSN index */
            if ((tombstone_csn = entry_get_deletion_csn(tombstoneentry->ep_entry))) {
                csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
                retval = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN, deletion_csn_str,
                                               tombstoneentry->ep_id, BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 3 DBI_RC_RETRY\n");
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "index_addordel_string TOMBSTONE csn(%s), err=%d %s\n",
                                  slapi_entry_get_dn_const(tombstoneentry->ep_entry),
                                  retval, (msg = dblayer_strerror(retval)) ? msg : "");
                    ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                    if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                        disk_full = 1;
                        goto diskfull_return;
                    }
                    goto error_return;
                }
            }

            retval = index_addordel_string(be, SLAPI_ATTR_UNIQUEID, slapi_entry_get_uniqueid(addingentry->ep_entry), addingentry->ep_id, BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 4 DBI_RC_RETRY\n");
                /* Retry txn */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "index_addordel_string UNIQUEID (%s), err=%d %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry),
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                    goto diskfull_return;
                }
                goto error_return;
            }
            retval = index_addordel_string(be,
                                           SLAPI_ATTR_NSCP_ENTRYDN,
                                           slapi_sdn_get_ndn(sdn),
                                           addingentry->ep_id,
                                           BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 5 DBI_RC_RETRY\n");
                /* Retry txn */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add", "index_addordel_string ENTRYDN (%s), err=%d %s\n",
                              slapi_entry_get_dn_const(addingentry->ep_entry),
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                    goto diskfull_return;
                }
                goto error_return;
            }
            /* Need to delete the entryrdn index of the resurrected tombstone... */
            if (entryrdn_get_switch()) { /* subtree-rename: on */
                if (tombstoneentry) {
                    retval = entryrdn_index_entry(be, tombstoneentry, BE_INDEX_DEL, &txn);
                    if (retval) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                                      "Resurrecting %s: failed to remove entryrdn index, err=%d %s\n",
                                      slapi_entry_get_dn_const(tombstoneentry->ep_entry),
                                      retval, (msg = dblayer_strerror(retval)) ? msg : "");
                        goto error_return;
                    }
                }
            }
        }
        if (is_tombstone_operation) {
            retval = index_addordel_entry(be, addingentry, BE_INDEX_ADD | BE_INDEX_TOMBSTONE, &txn);
        } else {
            retval = index_addordel_entry(be, addingentry, BE_INDEX_ADD, &txn);
        }
        if (DBI_RC_RETRY == retval) {
            slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 5 DEADLOCK\n");
            /* retry txn */
            continue;
        }
        if (retval) {
            slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_back_add",
                          "Attempt to index %lu failed; rc=%d\n",
                          (u_long)addingentry->ep_id, retval);
            ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                disk_full = 1;
                goto diskfull_return;
            }
            goto error_return;
        }
        if (parent_found) {
            /* Push out the db modifications from the parent entry */
            retval = modify_update_all(be, pb, &parent_modify_c, &txn);
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                          "conn=%" PRIu64 " op=%d modify_update_all: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                          conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, retval);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add", "add 6 DEADLOCK\n");
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                              "modify_update_all: %s (%lu) failed; rc=%d\n",
                              slapi_entry_get_dn(addingentry->ep_entry), (u_long)addingentry->ep_id, retval);
                ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                    goto diskfull_return;
                }
                goto error_return;
            }
        }
        /*
         * Update the Virtual List View indexes
         */
        if (!is_ruv) {
            retval = vlv_update_all_indexes(&txn, be, pb, NULL, addingentry);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_ARGS, "ldbm_back_add",
                              "add DEADLOCK vlv_update_index\n");
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add",
                              "vlv_update_index failed, err=%d %s\n",
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                    goto diskfull_return;
                }
                goto error_return;
            }
        }

        if (!is_ruv && !is_fixup_operation && !NO_RUV_UPDATE(li)) {
            ruv_c_init = ldbm_txn_ruv_modify_context(pb, &ruv_c);
            if (-1 == ruv_c_init) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                              "ldbm_txn_ruv_modify_context - "
                              "failed to construct RUV modify context\n");
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
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                              "modify_update_all failed, err=%d %s\n", retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                ldap_result_code = LDAP_OPERATIONS_ERROR;
                goto error_return;
            }
        }

        if (retval == 0) {
            break;
        }
    }
    if (retry_count == RETRY_TIMES) {
        /* Failed */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                      "Retry count exceeded in add\n");
        ldap_result_code = LDAP_BUSY;
        goto error_return;
    }

    /*
     * At this point, everything's cool, and the only thing which
     * can go wrong is a transaction commit failure.
     */
    slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, NULL);
    slapi_pblock_set(pb, SLAPI_ENTRY_POST_OP, slapi_entry_dup(addingentry->ep_entry));

    if (is_resurect_operation) {
        /*
         * We can now switch the tombstone entry with the real entry.
         */
        retval = cache_replace(&inst->inst_cache, tombstoneentry, addingentry);
        if (retval) {
            /* This happens if the dn of addingentry already exists */
            ADD_SET_ERROR(ldap_result_code, LDAP_ALREADY_EXISTS, retry_count);
            slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_add",
                          "cache_replace concurrency detected: %s (rc: %d)\n",
                          slapi_entry_get_dn_const(addingentry->ep_entry), retval);
            retval = -1;
            goto error_return;
        }
        /*
         * The tombstone was locked down in the cache... we can
         * get rid of the entry in the cache now.
         * We cannot expect tombstoneentry exists from now on.
         */
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            /* since the op was successful, delete the tombstone dn from the dn cache */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache,
                                                 tombstoneentry->ep_id);
            if (bdn) { /* in the dncache, remove it. */
                CACHE_REMOVE(&inst->inst_dncache, bdn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            }
        }
    }
    if (parent_found) {
        /* switch the parent entry copy into play */
        myrc = modify_switch_entries(&parent_modify_c, be);
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                      "conn=%" PRIu64 " op=%d modify_switch_entries: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                      conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, myrc);
        if (0 == myrc) {
            parent_switched = 1;
        }
    }

    if (ruv_c_init) {
        if (modify_switch_entries(&ruv_c, be) != 0) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_add",
                          "modify_switch_entries failed\n");
            goto error_return;
        }
    }

    /* call the transaction post add plugins just before the commit */
    if (op_plugin_call && (retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN))) {
        int opreturn = 0;
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add",
                      "SLAPI_PLUGIN_BE_TXN_POST_ADD_FN plugin "
                      "returned error code %d\n",
                      retval);
        if (!ldap_result_code) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        if (!ldap_result_code) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
        if (!opreturn) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &retval);
        }
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
        if (retval) {
            betxn_callback_fails = 1;
        }
        goto error_return;
    }

    retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_ADD_FN);
    if (retval) {
        betxn_callback_fails = 1;
        ldbm_set_error(pb, retval, &ldap_result_code, &ldap_result_message);
        goto error_return;
    }

    /* Release SERIAL LOCK */
    retval = dblayer_txn_commit(be, &txn);
    /* after commit - txn is no longer valid - replace SLAPI_TXN with parent */
    slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    if (0 != retval) {
        ADD_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
        if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
            disk_full = 1;
            goto diskfull_return;
        }
        goto error_return;
    }
    noabort = 1;

    rc = 0;
    goto common_return;

error_return:
    /* Revert the caches if this is the parent operation */
    if (parent_op && betxn_callback_fails) {
        revert_cache(inst, &parent_time);
    }
    if (addingentry_id_assigned) {
        next_id_return(be, addingentry->ep_id);
    }
    if (rc == DBI_RC_RUNRECOVERY) {
        dblayer_remember_disk_filled(li);
        ldbm_nasty("ldbm_back_add", "Add", 80, rc);
        disk_full = 1;
    } else if (0 == rc) {
        rc = SLAPI_FAIL_GENERAL;
    }
    if (parent_switched) {
        /*
         * Restore the old parent entry, switch the new with the original.
         * Otherwise the numsubordinate count will be off, and could later
         * be written to disk.
         */
        myrc = modify_unswitch_entries(&parent_modify_c, be);
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                      "conn=%" PRIu64 " op=%d modify_unswitch_entries: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                      conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, myrc);
    }
diskfull_return:
    if (disk_full) {
        if (addingentry) {
            if (inst && cache_is_in_cache(&inst->inst_cache, addingentry)) {
                CACHE_REMOVE(&inst->inst_cache, addingentry);
                /* tell frontend not to free this entry */
                slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
            } else if (!cache_has_otherref(&inst->inst_cache, addingentry)) {
                if (!is_resurect_operation) {           /* if resurect, tombstoneentry is dupped. */
                    backentry_clear_entry(addingentry); /* e is released in the frontend */
                }
            }
            CACHE_RETURN(&inst->inst_cache, &addingentry);
        }
        rc = return_on_disk_full(li);
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
                val = -1;
                slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &val);
            }
            /* call the transaction post add plugins just before the abort */
            /* but only if it is not a NOOP */
            if (!is_noop && op_plugin_call && (retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN))) {
                int opreturn = 0;
                slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_add",
                              "SLAPI_PLUGIN_BE_TXN_POST_ADD_FN plugin "
                              "returned error code %d\n",
                              retval);
                if (!ldap_result_code) {
                    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                }
                slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                if (!opreturn) {
                    opreturn = -1;
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                }
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
            }
            /* the repl postop needs to be called for aborted operations */
            retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_ADD_FN);
            if (addingentry) {
                if (inst && cache_is_in_cache(&inst->inst_cache, addingentry)) {
                    CACHE_REMOVE(&inst->inst_cache, addingentry);
                    /* tell frontend not to free this entry */
                    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
                } else if (!cache_has_otherref(&inst->inst_cache, addingentry)) {
                    if (!is_resurect_operation) {           /* if resurect, tombstoneentry is dupped. */
                        backentry_clear_entry(addingentry); /* e is released in the frontend */
                    }
                }
                CACHE_RETURN(&inst->inst_cache, &addingentry);
            }
            /* Release SERIAL LOCK */
            if (!noabort) {
                dblayer_txn_abort(be, &txn); /* abort crashes in case disk full */
            }
            /* txn is no longer valid - reset the txn pointer to the parent */
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
        } else {
            if (addingentry) {
                if (inst && cache_is_in_cache(&inst->inst_cache, addingentry)) {
                    CACHE_REMOVE(&inst->inst_cache, addingentry);
                    /* tell frontend not to free this entry */
                    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
                } else if (!cache_has_otherref(&inst->inst_cache, addingentry)) {
                    if (!is_resurect_operation) {           /* if resurect, tombstoneentry is dupped. */
                        backentry_clear_entry(addingentry); /* e is released in the frontend */
                    }
                }
                CACHE_RETURN(&inst->inst_cache, &addingentry);
            }
        }
        if (!not_an_error) {
            rc = SLAPI_FAIL_GENERAL;
        }
    }

common_return:
    if (inst) {
        if (tombstoneentry && cache_is_in_cache(&inst->inst_cache, tombstoneentry)) {
            cache_unlock_entry(&inst->inst_cache, tombstoneentry);
            CACHE_RETURN(&inst->inst_cache, &tombstoneentry);
        }
        if (addingentry) {
            if ((0 == retval) && entryrdn_get_switch()) { /* subtree-rename: on */
                /* since adding the entry to the entry cache was successful,
                 * let's add the dn to dncache, if not yet done. */
                struct backdn *bdn = dncache_find_id(&inst->inst_dncache,
                                                     addingentry->ep_id);
                if (bdn) { /* already in the dncache */
                    CACHE_RETURN(&inst->inst_dncache, &bdn);
                } else { /* not in the dncache yet */
                    Slapi_DN *addingsdn =
                        slapi_sdn_dup(slapi_entry_get_sdn(addingentry->ep_entry));
                    if (addingsdn) {
                        bdn = backdn_init(addingsdn, addingentry->ep_id, 0);
                        if (bdn) {
                            CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                            CACHE_RETURN(&inst->inst_dncache, &bdn);
                            slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_add",
                                          "set %s to dn cache\n", dn);
                        }
                    }
                }
            }
            if (is_remove_from_cache) {
                CACHE_REMOVE(&inst->inst_cache, addingentry);
            }
            CACHE_RETURN(&inst->inst_cache, &addingentry);
        }
        if (inst->inst_ref_count) {
            slapi_counter_decrement(inst->inst_ref_count);
        }
    }
    /* bepost op needs to know this result */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    /* JCMREPL - The bepostop is called even if the operation fails. */
    plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_ADD_FN);
    if (ruv_c_init) {
        modify_term(&ruv_c, be);
    }
    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_add",
                  "conn=%" PRIu64 " op=%d modify_term: old_entry=0x%p, new_entry=0x%p\n",
                  conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry);
    myrc = modify_term(&parent_modify_c, be);
    done_with_pblock_entry(pb, SLAPI_ADD_EXISTING_DN_ENTRY);
    done_with_pblock_entry(pb, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY);
    done_with_pblock_entry(pb, SLAPI_ADD_PARENT_ENTRY);
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
            slapi_send_ldap_result(pb, ldap_result_code, ldap_result_matcheddn, ldap_result_message, 0, NULL);
        }
    }
    backentry_free(&originalentry);
    backentry_free(&tmpentry);
    slapi_sdn_done(&parentsdn);
    slapi_ch_free((void **)&ldap_result_matcheddn);
    slapi_ch_free((void **)&errbuf);
    return rc;
}

/*
 * add the parentid, entryid and entrydn, operational attributes.
 *
 * Note: This is called from the ldif2ldbm code.
 */
void
add_update_entry_operational_attributes(struct backentry *ep, ID pid)
{
    struct berval bv;
    struct berval *bvp[2];
    char buf[40]; /* Enough for an EntryID */

    bvp[0] = &bv;
    bvp[1] = NULL;

    /* parentid */
    /* If the pid is 0, then the entry does not have a parent.  It
     * may be the case that the entry is a suffix.  In any case,
     * the parentid attribute should only be added if the entry
     * has a parent. */
    if (pid != 0) {
        sprintf(buf, "%lu", (u_long)pid);
        bv.bv_val = buf;
        bv.bv_len = strlen(buf);
        entry_replace_values(ep->ep_entry, LDBM_PARENTID_STR, bvp);
    }

    /* entryid */
    sprintf(buf, "%lu", (u_long)ep->ep_id);
    bv.bv_val = buf;
    bv.bv_len = strlen(buf);
    entry_replace_values(ep->ep_entry, "entryid", bvp);

    /* add the entrydn operational attribute to the entry. */
    add_update_entrydn_operational_attributes(ep);
}

/*
 * add the entrydn operational attribute to the entry.
 */
void
add_update_entrydn_operational_attributes(struct backentry *ep)
{
    struct berval bv;
    struct berval *bvp[2];

    /* entrydn */
    bvp[0] = &bv;
    bvp[1] = NULL;
    bv.bv_val = (void *)backentry_get_ndn(ep);
    bv.bv_len = strlen(bv.bv_val);
    entry_replace_values_with_flags(ep->ep_entry, LDBM_ENTRYDN_STR, bvp,
                                    SLAPI_ATTR_FLAG_NORMALIZED_CIS);
}

/*
 * delete the entrydn operational attributes from the entry.
 */
static void
delete_update_entrydn_operational_attributes(struct backentry *ep)
{
    /* entrydn */
    slapi_entry_attr_delete(ep->ep_entry, LDBM_ENTRYDN_STR);
}

