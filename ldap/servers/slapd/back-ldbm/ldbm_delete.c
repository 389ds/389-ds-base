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


/* delete.c - ldbm backend delete routine */

#include "back-ldbm.h"

#define DEL_SET_ERROR(rc, error, count)                                            \
    {                                                                              \
        (rc) = (error);                                                            \
        (count) = RETRY_TIMES; /* otherwise, the transaction may not be aborted */ \
    }

int
ldbm_back_delete(Slapi_PBlock *pb)
{
    backend *be;
    ldbm_instance *inst = NULL;
    struct ldbminfo *li = NULL;
    struct backentry *e = NULL;
    struct backentry *tombstone = NULL;
    struct backentry *original_tombstone = NULL;
    struct backentry *tmptombstone = NULL;
    const char *dn = NULL;
    back_txn txn;
    back_txnid parent_txn;
    int retval = -1;
    const char *msg;
    char *errbuf = NULL;
    int retry_count = 0;
    int disk_full = 0;
    int parent_found = 0;
    int ruv_c_init = 0;
    modify_context parent_modify_c = {0};
    modify_context ruv_c = {0};
    int rc = 0;
    int ldap_result_code = LDAP_SUCCESS;
    char *ldap_result_message = NULL;
    Slapi_DN *sdnp = NULL;
    char *e_uniqueid = NULL;
    Slapi_DN nscpEntrySDN;
    Slapi_Operation *operation;
    CSN *opcsn = NULL;
    int is_fixup_operation = 0;
    int is_ruv = 0; /* True if the current entry is RUV */
    int is_replicated_operation = 0;
    int is_tombstone_entry = 0;     /* True if the current entry is alreday a tombstone        */
    int delete_tombstone_entry = 0; /* We must remove the given tombstone entry from the dbi_db_t    */
    int create_tombstone_entry = 0; /* We perform a "regular" LDAP delete but since we use    */
                                    /* replication, we must create a new tombstone entry    */
    int remove_e_from_cache = 0;
    entry_address *addr;
    int addordel_flags = 0; /* passed to index_addordel */
    char *entryusn_str = NULL;
    Slapi_Entry *orig_entry = NULL;
    Slapi_DN parentsdn;
    int opreturn = 0;
    int free_delete_existing_entry = 0;
    int not_an_error = 0;
    int parent_switched = 0;
    int myrc = 0;
    PRUint64 conn_id;
    const CSN *tombstone_csn = NULL;
    char deletion_csn_str[CSN_STRSIZE];
    int op_id;
    ID ep_id = 0;
    ID tomb_ep_id = 0;
    int result_sent = 0;
    Connection *pb_conn;
    int32_t parent_op = 0;
    struct timespec parent_time;

    if (slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id) < 0) {
        conn_id = 0; /* connection is NULL */
    }
    slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdnp);
    slapi_pblock_get(pb, SLAPI_TARGET_ADDRESS, &addr);
    slapi_pblock_get(pb, SLAPI_TXN, (void **)&parent_txn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    slapi_sdn_init(&nscpEntrySDN);
    slapi_sdn_init(&parentsdn);

    inst = (ldbm_instance *)be->be_instance_info;
    if (inst && inst->inst_ref_count) {
        slapi_counter_increment(inst->inst_ref_count);
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_back_delete", "Instance \"%s\" does not exist.\n",
                      inst ? inst->inst_name : "null instance");
        ldap_result_code = LDAP_UNWILLING_TO_PERFORM;
        ldap_result_message = "Backend instance is not available.";
        /* error_return code dereferences "inst" but diskfull_return one does not. */
        goto diskfull_return;
    }

    /* dblayer_txn_init needs to be called before "goto error_return" */
    dblayer_txn_init(li, &txn);
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
        if (parent_txn)
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    }

    if (pb_conn) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_delete", "Enter conn=%" PRIu64 " op=%d\n",
                      pb_conn->c_connid, operation->o_opid);
    }

    if ((NULL == addr) || (NULL == sdnp)) {
        /* retval is -1 */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                      "Either of DELETE_TARGET_SDN or TARGET_ADDRESS is NULL.\n");
        goto error_return;
    }
    dn = slapi_sdn_get_dn(sdnp);
    ldap_result_code = slapi_dn_syntax_check(pb, dn, 1);
    if (ldap_result_code) {
        ldap_result_code = LDAP_INVALID_DN_SYNTAX;
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
        /* retval is -1 */
        goto error_return;
    }

    is_fixup_operation = operation_is_flag_set(operation, OP_FLAG_REPL_FIXUP);
    is_ruv = operation_is_flag_set(operation, OP_FLAG_REPL_RUV);
    delete_tombstone_entry = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY);

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
    if(SERIALLOCK(li) && !operation_is_flag_set(operation,OP_FLAG_REPL_FIXUP))
    {
        dblayer_lock_backend(be);
        dblock_acquired= 1;
    }
     */

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
        /* retval is -1 */
        goto error_return;
    }

    /*
     * So, we believe that no code up till here actually added anything
     * to the persistent store. From now on, we're transacted
     */
    txn.back_txn_txn = NULL; /* ready to create the child transaction */
    for (retry_count = 0; retry_count < RETRY_TIMES; retry_count++) {
        if (txn.back_txn_txn && (txn.back_txn_txn != parent_txn)) { /* retry_count > 0 */
            Slapi_Entry *ent = NULL;

            /* Don't release SERIAL LOCK */
            dblayer_txn_abort_ext(li, &txn, PR_FALSE);
            slapi_pblock_set(pb, SLAPI_TXN, parent_txn);

            /* reset original entry */
            slapi_pblock_get(pb, SLAPI_DELETE_EXISTING_ENTRY, &ent);
            if (ent && free_delete_existing_entry) {
                slapi_entry_free(ent);
                slapi_pblock_set(pb, SLAPI_DELETE_EXISTING_ENTRY, NULL);
            }
            slapi_pblock_set(pb, SLAPI_DELETE_EXISTING_ENTRY, slapi_entry_dup(e->ep_entry));
            free_delete_existing_entry = 1; /* must free the dup */
            if (create_tombstone_entry) {
                slapi_sdn_set_ndn_byval(&nscpEntrySDN, slapi_sdn_get_ndn(slapi_entry_get_sdn(e->ep_entry)));

                /* reset tombstone entry */
                if (original_tombstone) {
                    /* must duplicate tombstone before returning it to cache,
                     * which could free the entry. */
                    if ((tmptombstone = backentry_dup(original_tombstone)) == NULL) {
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        goto error_return;
                    }
                    if (cache_is_in_cache(&inst->inst_cache, tombstone)) {
                        CACHE_REMOVE(&inst->inst_cache, tombstone);
                    }
                    CACHE_RETURN(&inst->inst_cache, &tombstone);
                    if (tombstone) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                      "conn=%" PRIu64 " op=%d [retry: %d] tombstone %s is not freed!!! refcnt %d, state %d\n",
                                      conn_id, op_id, retry_count, slapi_entry_get_dn(tombstone->ep_entry),
                                      tombstone->ep_refcnt, tombstone->ep_state);
                    }
                    tombstone = original_tombstone;
                    original_tombstone = tmptombstone;
                    tmptombstone = NULL;
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                  "conn=%" PRIu64 " op=%d [retry: %d] No original_tombstone for %s!!\n",
                                  conn_id, op_id, retry_count, slapi_entry_get_dn(e->ep_entry));
                }
            }
            if (ruv_c_init) {
                /* reset the ruv txn stuff */
                modify_term(&ruv_c, be);
                ruv_c_init = 0;
            }

            /* We're re-trying */
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                          "Delete Retrying Transaction\n");
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
            if (LDBM_OS_ERR_IS_DISKFULL(retval)) disk_full = 1;
            ldap_result_code= LDAP_OPERATIONS_ERROR;
            goto error_return;
        }

        /* stash the transaction */
        slapi_pblock_set(pb, SLAPI_TXN, txn.back_txn_txn);

        if (0 == retry_count) { /* just once */
            /* find and lock the entry we are about to modify */
            /*
             * A corner case:
             * If a conflict occurred in a MMR topology, a replicated delete
             * op from another supplier could target a conflict entry; while the
             * corresponding entry on this server could have been already
             * deleted.  That is, the entry 'e' found with "addr" is a tomb-
             * stone.  If it is the case, we need to back off.
             */
replace_entry:
            if ((e = find_entry2modify(pb, be, addr, &txn, &result_sent)) == NULL)
            {
                ldap_result_code= LDAP_NO_SUCH_OBJECT;
                retval = -1;
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "Deleting entry is already deleted.\n");
                goto error_return; /* error result sent by find_entry2modify() */
            }
            ep_id = e->ep_id;

            /* JCMACL - Shouldn't the access check be before the has children check...
             * otherwise we're revealing the fact that an entry exists and has children */
            /* Before has children to mask the presence of children disclosure. */
            ldap_result_code = plugin_call_acl_plugin (pb, e->ep_entry, NULL, NULL, SLAPI_ACL_DELETE,
                                                       ACLPLUGIN_ACCESS_DEFAULT, &errbuf );
            if ( ldap_result_code != LDAP_SUCCESS ) {
                ldap_result_message= errbuf;
                retval = -1;
                goto error_return;
            }
            /* this has to be handled by urp for replicated operations */
            retval = slapi_entry_has_children(e->ep_entry);
            if (retval && !is_replicated_operation) {
                ldap_result_code= LDAP_NOT_ALLOWED_ON_NONLEAF;
                if (slapi_entry_has_conflict_children(e->ep_entry, (void *)li->li_identity) > 0) {
                    ldap_result_message = "Entry has replication conflicts as children";
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                  "conn=%" PRIu64 " op=%d Deleting entry %s has replication conflicts as children.\n",
                                   conn_id, op_id, slapi_entry_get_dn(e->ep_entry));
                } else {
                    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                                  "conn=%" PRIu64 " op=%d Deleting entry %s has %d children.\n",
                                   conn_id, op_id, slapi_entry_get_dn(e->ep_entry), retval);
                }
                retval = -1;
                goto error_return;
            }

            /* Don't call pre-op for Tombstone entries */
            if (!delete_tombstone_entry) {
                /*
                 * Some present state information is passed through the PBlock to the
                 * backend pre-op plugin. To ensure a consistent snapshot of this state
                 * we wrap the reading of the entry with the dblock.
                 */
                ldap_result_code= get_copy_of_entry(pb, addr, &txn,
                                                    SLAPI_DELETE_EXISTING_ENTRY, !is_replicated_operation);
                free_delete_existing_entry = 1;
                if(ldap_result_code==LDAP_OPERATIONS_ERROR ||
                   ldap_result_code==LDAP_INVALID_DN_SYNTAX) {
                    /* restore original entry so the front-end delete code can free it */
                    retval = -1;
                    goto error_return;
                }
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);

                retval = plugin_call_mmr_plugin_preop(pb, NULL,SLAPI_PLUGIN_BE_PRE_DELETE_FN);
                if (SLAPI_PLUGIN_NOOP == retval) {
                    not_an_error = 1;
                    rc = LDAP_SUCCESS;
                } else if (SLAPI_PLUGIN_NOOP_COMMIT == retval) {
                    not_an_error = 1;
                    rc = LDAP_SUCCESS;
                    retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN);
                    goto commit_return;
                } else if(slapi_isbitset_int(retval,SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY)) {
                    /* we need to delete an other entry, determined by urp */
                    done_with_pblock_entry(pb,SLAPI_DELETE_EXISTING_ENTRY); /* Could be through this multiple times */
                    if (cache_is_in_cache(&inst->inst_cache, e)) {
                        ep_id = e->ep_id; /* Otherwise, e might have been freed. */
                        CACHE_REMOVE(&inst->inst_cache, e);
                    }
                    cache_unlock_entry(&inst->inst_cache, e);
                    CACHE_RETURN(&inst->inst_cache, &e);
                    /*
                     * e is unlocked and no longer in cache.
                     * It could be freed at any moment.
                     */
                    e = NULL;
                    goto replace_entry;
                } else if(slapi_isbitset_int(retval,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY)) {
                    Slapi_Entry *e_to_delete = NULL;
                    done_with_pblock_entry(pb,SLAPI_DELETE_EXISTING_ENTRY); /* Could be through this multiple times */
                    slapi_log_err(SLAPI_LOG_REPL,
                                   "ldbm_back_delete", "reloading existing entry "
                                   "(%s)\n", addr->uniqueid );
                    ldap_result_code= get_copy_of_entry(pb, addr, &txn,
                                SLAPI_DELETE_EXISTING_ENTRY, !is_replicated_operation);
                    slapi_pblock_get(pb,SLAPI_DELETE_EXISTING_ENTRY, &e_to_delete);
                    slapi_entry_free( e->ep_entry );
                    e->ep_entry = slapi_entry_dup( e_to_delete );
                    retval = 0;
                }
                if (retval == 0) {
                    retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN);
                }
                if (retval)
                {
                    /*
                     * Plugin indicated some kind of failure,
                     * or that this Operation became a No-Op.
                     */
                    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                    if (!ldap_result_code) {
                        if (LDAP_ALREADY_EXISTS == ldap_result_code) {
                            /*
                             * The target entry is already a tombstone.
                             * We need to treat this as a success,
                             * but we need to remove the entry e from the entry cache.
                             */
                            remove_e_from_cache = 1;
                            ldap_result_code = LDAP_SUCCESS;
                        }
                        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                    }
                    /* restore original entry so the front-end delete code can free it */
                    slapi_pblock_get( pb, SLAPI_PLUGIN_OPRETURN, &opreturn );
                    if (!opreturn) {
                        slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN, ldap_result_code ? &ldap_result_code : &rc );
                    }
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
                /* the flag could be set in a preop plugin (e.g., USN) */
                delete_tombstone_entry = operation_is_flag_set(operation, OP_FLAG_TOMBSTONE_ENTRY);
            }

            /* Save away a copy of the entry, before modifications */
            slapi_pblock_set(pb, SLAPI_ENTRY_PRE_OP, slapi_entry_dup(e->ep_entry));

            /* call the transaction pre delete plugins just after the
             * to-be-deleted entry is prepared. */
            /* these should not need to modify the entry to be deleted -
               if for some reason they ever do, do not use e->ep_entry since
               it could be in the cache and referenced by other search threads -
               instead, have them modify a copy of the entry */
            if (!delete_tombstone_entry) {
                retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN);
                if (retval) {
                    slapi_log_err(SLAPI_LOG_TRACE,
                                   "ldbm_back_delete", "SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN plugin "
                                   "returned error code %d\n", retval );
                    if (!ldap_result_code) {
                        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                    }
                    if (!opreturn) {
                        slapi_pblock_get( pb, SLAPI_PLUGIN_OPRETURN, &opreturn );
                    }
                    if (!opreturn) {
                        slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN,
                                          ldap_result_code ?
                                          &ldap_result_code : &retval );
                    }
                    slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                    goto error_return;
                }
            }

            /*
             * Sanity check to avoid to delete a non-tombstone or to tombstone again
             * a tombstone entry. This should not happen (see bug 561003).
             */
            is_tombstone_entry = slapi_entry_flag_is_set(e->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
            if (delete_tombstone_entry) {
                if (!is_tombstone_entry) {
                    slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_delete",
                            "Attempt to delete a non-tombstone entry %s\n", dn);
                    delete_tombstone_entry = 0;
                }
            } else {
                if (is_tombstone_entry) {
                    slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_delete",
                            "Attempt to Tombstone again a tombstone entry %s\n", dn);
                    delete_tombstone_entry = 1;
                    operation_set_flag(operation, OP_FLAG_TOMBSTONE_ENTRY);
                }
            }

            /*
             * If a CSN is set, we need to tombstone the entry,
             * rather than deleting it outright.
             */
            opcsn = operation_get_csn (operation);
            if (!delete_tombstone_entry) {
                if ((opcsn == NULL) && !is_fixup_operation && operation->o_csngen_handler) {
                    /*
                     * Current op is a user request. Opcsn will be assigned
                     * by entry_assign_operation_csn() if the dn is in an
                     * updatable replica.
                     */
                    if (entry_assign_operation_csn(pb, e->ep_entry, NULL, &opcsn) != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                "failed to generate delete CSN for entry (%s), aborting operation\n",
                                slapi_entry_get_dn(e->ep_entry));
                        retval = -1;
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        goto error_return;
                    }
                }
                if (opcsn != NULL) {
                    if (!is_fixup_operation) {
                        entry_set_maxcsn (e->ep_entry, opcsn);
                    }
                }
                /*
                 * We are dealing with replication and if we haven't been called to
                 * remove a tombstone, then it's because  we want to create a new one.
                 */
                if ( slapi_operation_get_replica_attr (pb, operation, "nsds5ReplicaTombstonePurgeInterval",
                                                       &create_tombstone_entry) == 0 ) {
                    create_tombstone_entry = (create_tombstone_entry < 0) ? 0 : 1;
                }
            }
            if (create_tombstone_entry && is_tombstone_entry) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                    "Attempt to convert a tombstone entry %s to tombstone\n", dn);
                retval = -1;
                ldap_result_code = LDAP_UNWILLING_TO_PERFORM;
                goto error_return;
            }

#ifdef DEBUG
            slapi_log_err(SLAPI_LOG_REPL, "ldbm_back_delete",
                    "entry: %s  - flags: delete %d is_tombstone_entry %d create %d \n",
                    dn, delete_tombstone_entry, is_tombstone_entry, create_tombstone_entry);
#endif

            /*
             * Get the entry's parent. We do this here because index_read
             * seems to deadlock the database when dblayer_txn_begin is
             * called.
             */
            slapi_sdn_get_backend_parent_ext(sdnp, &parentsdn, be, is_tombstone_entry);
            if (!slapi_sdn_isempty(&parentsdn)) {
                struct backentry *parent = NULL;
                char *pid_str = (char *)slapi_entry_attr_get_ref(e->ep_entry, LDBM_PARENTID_STR);
                if (pid_str) {
                    /* First, try to get the direct parent. */
                    /*
                     * Although a rare case, multiple parents from repl conflict could exist.
                     * In such case, if a parent entry is found just by parentsdn
                     * (find_entry2modify_only_ext), a wrong parent could be found,
                     * and numsubordinate count could get confused.
                     */
                    ID pid;
                    int cache_retry_count = 0;
                    int cache_retry = 0;

                    pid = (ID)strtol(pid_str, (char **)NULL, 10);

                    /*
                     * Its possible that the parent entry retrieved from the cache in id2entry
                     * could be removed before we lock it, because tombstone purging updated/replaced
                     * the parent.  If we fail to lock the entry, just try again.
                     */
                    while (1) {
                        parent = id2entry(be, pid, &txn, &retval);
                        if (parent && (cache_retry = cache_lock_entry(&inst->inst_cache, parent))) {
                            /* Failed to obtain parent entry's entry lock */
                            if (cache_retry == RETRY_CACHE_LOCK &&
                                cache_retry_count < LDBM_CACHE_RETRY_COUNT) {
                                /* try again */
                                DS_Sleep(PR_MillisecondsToInterval(100));
                                cache_retry_count++;
                                continue;
                            }
                            retval = -1;
                            CACHE_RETURN(&(inst->inst_cache), &parent);
                            goto error_return;
                        } else {
                            /* entry locked, move on */
                            break;
                        }
                    }
                }
                if (NULL == parent) {
                    entry_address parent_addr;
                    if (is_tombstone_entry) {
                        parent_addr.uniqueid = slapi_entry_attr_get_charptr(e->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
                    } else {
                        parent_addr.uniqueid = NULL;
                    }
                    parent_addr.sdn = &parentsdn;
                    parent = find_entry2modify_only_ext(pb, be, &parent_addr, TOMBSTONE_INCLUDED, &txn, &result_sent);
                }
                if (parent) {
                    int isglue;
                    size_t haschildren = 0;
                    int op = PARENTUPDATE_DEL;

                    /* Unfortunately findentry doesn't tell us whether it just
                     * didn't find the entry, or if there was an error, so we
                     * have to assume that the parent wasn't found */
                    parent_found = 1;

                    /* Modify the parent in memory */
                    modify_init(&parent_modify_c, parent);
                    if (create_tombstone_entry) {
                        op |= PARENTUPDATE_CREATE_TOMBSTONE;
                    } else if (delete_tombstone_entry) {
                        op |= PARENTUPDATE_DELETE_TOMBSTONE;
                    }
                    retval = parent_update_on_childchange(&parent_modify_c, op, &haschildren);
                    /* The modify context now contains info needed later */
                    if (0 != retval) {
                        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                      "conn=%" PRIu64 " op=%d parent_update_on_childchange: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                                      conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, retval);
                        ldap_result_code = LDAP_OPERATIONS_ERROR;
                        slapi_sdn_done(&parentsdn);
                        retval = -1;
                        goto error_return;
                    }

                    /*
                     * Replication urp_post_delete will delete the parent entry
                     * if it is a glue entry without any more children.
                     * Those urp condition checkings are done here to
                     * save unnecessary entry dup.
                     */
                    isglue = slapi_entry_attr_hasvalue(parent_modify_c.new_entry->ep_entry,
                                                       SLAPI_ATTR_OBJECTCLASS, "glue");
                    if (opcsn && parent_modify_c.new_entry && !haschildren && isglue) {
                        slapi_pblock_set(pb, SLAPI_DELETE_GLUE_PARENT_ENTRY,
                                         slapi_entry_dup(parent_modify_c.new_entry->ep_entry));
                    }
                }
            }

            if (create_tombstone_entry) {
                /*
                 * The entry is not removed from the disk when we tombstone an
                 * entry. We change the DN, add objectclass=tombstone, and record
                 * the UniqueID of the parent entry.
                 */
                const char *childuniqueid = slapi_entry_get_uniqueid(e->ep_entry);
                const char *parentuniqueid = NULL;
                char *edn = slapi_entry_get_dn(e->ep_entry);
                char *tombstone_dn;
                Slapi_Value *tomb_value;

                if (slapi_entry_attr_hasvalue(e->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE) &&
                    slapi_is_special_rdn(edn, RDN_IS_TOMBSTONE)) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                  "conn=%" PRIu64 " op=%d Turning a tombstone into a tombstone! \"%s\"; e: 0x%p, cache_state: 0x%x, refcnt: %d\n",
                                  conn_id, op_id, edn, e, e->ep_state, e->ep_refcnt);
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    retval = -1;
                    goto error_return;
                }
                if (!childuniqueid) {
                    slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete",
                                  "conn=%" PRIu64 " op=%d No nsUniqueId in the entry \"%s\"; e: 0x%p, cache_state: 0x%x, refcnt: %d\n",
                                  conn_id, op_id, edn, e, e->ep_state, e->ep_refcnt);
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    retval = -1;
                    goto error_return;
                }
                /* always create the special tombstone dn, even if it already starts with nsuniqueid */
                tombstone_dn = compute_entry_tombstone_dn(edn, childuniqueid);
                slapi_sdn_set_ndn_byval(&nscpEntrySDN, slapi_sdn_get_ndn(slapi_entry_get_sdn(e->ep_entry)));

                /* Copy the entry unique_id for URP conflict checking */
                e_uniqueid = slapi_ch_strdup(childuniqueid);

                if (parent_modify_c.old_entry != NULL) {
                    /* The suffix entry has no parent */
                    parentuniqueid = slapi_entry_get_uniqueid(parent_modify_c.old_entry->ep_entry);
                }
                tombstone = backentry_dup(e);
                tomb_ep_id = tombstone->ep_id;
                slapi_entry_set_dn(tombstone->ep_entry, tombstone_dn); /* Consumes DN */
                if (entryrdn_get_switch())                             /* subtree-rename: on */
                {
                    Slapi_RDN *srdn = slapi_entry_get_srdn(tombstone->ep_entry);
                    char *tombstone_rdn =
                        compute_entry_tombstone_rdn(slapi_entry_get_rdn_const(e->ep_entry),
                                                    childuniqueid);
                    /* e_srdn has "uniaqueid=..., <ORIG RDN>" */
                    slapi_rdn_replace_rdn(srdn, tombstone_rdn);
                    slapi_ch_free_string(&tombstone_rdn);
                }
                /* Set tombstone flag on ep_entry */
                slapi_entry_set_flag(tombstone->ep_entry, SLAPI_ENTRY_FLAG_TOMBSTONE);

                if (parentuniqueid != NULL) {
                    /* The suffix entry has no parent */
                    slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID, parentuniqueid);
                }
                if (opcsn) {
                    csn_as_string(opcsn, PR_FALSE, deletion_csn_str);
                    slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_TOMBSTONE_CSN, deletion_csn_str);
                }
                slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(&nscpEntrySDN));
                tomb_value = slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
                value_update_csn(tomb_value, CSN_TYPE_VALUE_UPDATED, operation_get_csn(operation));
                slapi_entry_add_value(tombstone->ep_entry, SLAPI_ATTR_OBJECTCLASS, tomb_value);
                slapi_value_free(&tomb_value);
                /* XXXggood above used to be: slapi_entry_add_string(tombstone->ep_entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE); */
                /* JCMREPL - Add a description of what's going on? */

                if ((original_tombstone = backentry_dup(tombstone)) == NULL) {
                    ldap_result_code = LDAP_OPERATIONS_ERROR;
                    retval = -1;
                    goto error_return;
                }
            }
        } /* if (0 == retry_count) just once */
        else if (!delete_tombstone_entry) {
            /* call the transaction pre delete plugins not just once
             * but every time transaction is restarted. */
            /* these should not need to modify the entry to be deleted -
               if for some reason they ever do, do not use e->ep_entry since
               it could be in the cache and referenced by other search threads -
               instead, have them modify a copy of the entry */
            retval = plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN);
            if (retval) {
                if (SLAPI_PLUGIN_NOOP == retval) {
                    not_an_error = 1;
                    rc = LDAP_SUCCESS;
                }
                slapi_log_err(SLAPI_LOG_TRACE,
                              "ldbm_back_delete", "SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN plugin "
                                                  "returned error code %d\n",
                              retval);
                if (!ldap_result_code) {
                    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                }
                if (!opreturn) {
                    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                }
                if (!opreturn) {
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN,
                                     ldap_result_code ? &ldap_result_code : &retval);
                }
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                goto error_return;
            }
        }

        if (create_tombstone_entry) {
            slapi_pblock_get(pb, SLAPI_DELETE_BEPREOP_ENTRY, &orig_entry);
            /* this is ok because no other threads should be accessing
               the tombstone entry */
            slapi_pblock_set(pb, SLAPI_DELETE_BEPREOP_ENTRY,
                             tombstone->ep_entry);
            rc = plugin_call_plugins(pb,
                                     SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN);
            if (rc == -1) {
                /*
                * Plugin indicated some kind of failure,
                 * or that this Operation became a No-Op.
                 */
                if (!ldap_result_code) {
                    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
                }
                /* restore original entry so the front-end delete code
                 * can free it */
                slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
                if (!opreturn) {
                    slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN,
                                     ldap_result_code ? &ldap_result_code : &rc);
                }
                /* retval is -1 */
                slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
                goto error_return;
            }
            slapi_pblock_set(pb, SLAPI_DELETE_BEPREOP_ENTRY, orig_entry);
            orig_entry = NULL;

            /*
             * The entry is not removed from the disk when we tombstone an
             * entry. We change the DN, add objectclass=tombstone, and record
             * the UniqueID of the parent entry.
             */
            /* Note: cache_add (tombstone) fails since the original entry having
             * the same ID is already in the cache.  Thus, we have to add it
             * tentatively for now, then cache_add again when the original
             * entry is removed from the cache.
             */
            retval = cache_add_tentative(&inst->inst_cache, tombstone, NULL);
            if (0 > retval) {
                slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_delete",
                              "conn=%" PRIu64 " op=%d tombstone entry %s failed to add to the cache: %d\n",
                              conn_id, op_id, slapi_entry_get_dn(tombstone->ep_entry), retval);
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
            retval = id2entry_add(be, tombstone, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "delete 1 DBI_RC_RETRY\n");
                /* Abort and re-try */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete", "id2entry_add failed, err=%d %s\n",
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        } else {
            /* delete the entry from disk */
            retval = id2entry_delete(be, e, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "delete 2 DEADLOCK\n");
                /* Retry txn */
                continue;
            }
            if (retval) {
                if (retval == DBI_RC_RUNRECOVERY || LDBM_OS_ERR_IS_DISKFULL(retval)) {
                    disk_full = 1;
                }
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }
        /* delete from attribute indexes */
        addordel_flags = BE_INDEX_DEL | BE_INDEX_PRESENCE | BE_INDEX_EQUALITY;
        if (delete_tombstone_entry) {
            addordel_flags |= BE_INDEX_TOMBSTONE; /* tell index code we are deleting a tombstone */
        }
        retval = index_addordel_entry(be, e, addordel_flags, &txn);
        if (DBI_RC_RETRY == retval) {
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "delete 1 DEADLOCK\n");
            /* Retry txn */
            continue;
        }
        if (retval) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete", "index_addordel_entry(%s, 0x%x) failed (%d)\n",
                          slapi_entry_get_dn(e->ep_entry), addordel_flags, retval);
            DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
            goto error_return;
        }
        if (create_tombstone_entry) {
            /*
             * The tombstone entry is removed from all attribute indexes
             * above, but we want it to remain in the nsUniqueID and nscpEntryDN indexes
             * and for objectclass=tombstone.
             */


            retval = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS,
                                           SLAPI_ATTR_VALUE_TOMBSTONE,
                                           tombstone->ep_id, BE_INDEX_ADD, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "ldbm_back_delete", "(adding %s) DBI_RC_RETRY\n",
                              SLAPI_ATTR_VALUE_TOMBSTONE);
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "(adding %s) failed, err=%d %s\n",
                              SLAPI_ATTR_VALUE_TOMBSTONE, retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }

            /* Need to update the nsTombstoneCSN index */
            if ((tombstone_csn = entry_get_deletion_csn(tombstone->ep_entry))) {
                csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
                retval = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN,
                                               deletion_csn_str, tombstone->ep_id,
                                               BE_INDEX_ADD, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "Delete tombstone csn(adding %s) DBI_RC_RETRY\n",
                                  deletion_csn_str);
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "delete tombstone csn(adding %s) failed, err=%d %s\n",
                                  deletion_csn_str,
                                  retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                        disk_full = 1;
                    }
                    DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }

            retval = index_addordel_string(be, SLAPI_ATTR_UNIQUEID,
                                           slapi_entry_get_uniqueid(tombstone->ep_entry),
                                           tombstone->ep_id, BE_INDEX_ADD, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "ldbm_back_delete", "(adding %s) DBI_RC_RETRY\n",
                              SLAPI_ATTR_UNIQUEID);
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "adding %s) failed, err=%d %s\n",
                              SLAPI_ATTR_UNIQUEID, retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
            retval = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN,
                                           slapi_sdn_get_ndn(&nscpEntrySDN),
                                           tombstone->ep_id, BE_INDEX_ADD, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "ldbm_back_delete", "(adding %s) DBI_RC_RETRY\n",
                              SLAPI_ATTR_NSCP_ENTRYDN);
                /* Retry txn */
                continue;
            }
            if (retval) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "adding %s) failed, err=%d %s\n",
                              SLAPI_ATTR_NSCP_ENTRYDN, retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
            /* add a new usn to the entryusn index */
            entryusn_str = (char *)slapi_entry_attr_get_ref(tombstone->ep_entry, SLAPI_ATTR_ENTRYUSN);
            if (entryusn_str) {
                retval = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN,
                                               entryusn_str, tombstone->ep_id, BE_INDEX_ADD, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "(adding %s) DBI_RC_RETRY\n",
                                  SLAPI_ATTR_ENTRYUSN);
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "(adding %s) failed, err=%d %s\n",
                                  SLAPI_ATTR_ENTRYUSN, retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    DEL_SET_ERROR(ldap_result_code,
                                  LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
            if (entryrdn_get_switch()) /* subtree-rename: on */
            {
                Slapi_Attr *attr;
                Slapi_Value **svals;
                /* To maintain tombstonenumsubordinates,
                 * parentid is needed for tombstone, as well. */
                slapi_entry_attr_find(tombstone->ep_entry, LDBM_PARENTID_STR,
                                      &attr);
                if (attr) {
                    svals = attr_get_present_values(attr);
                    retval = index_addordel_values_sv(be, LDBM_PARENTID_STR,
                                                      svals, NULL, e->ep_id,
                                                      BE_INDEX_ADD, &txn);
                    if (DBI_RC_RETRY == retval) {
                        slapi_log_err(SLAPI_LOG_BACKLDBM,
                                      "ldbm_back_delete", "delete (updating " LDBM_PARENTID_STR ") DBI_RC_RETRY\n");
                        /* Retry txn */
                        continue;
                    }
                    if (retval) {
                        slapi_log_err(SLAPI_LOG_ERR,
                                      "ldbm_back_delete", "(deleting %s) failed, err=%d %s\n",
                                      LDBM_PARENTID_STR, retval,
                                      (msg = dblayer_strerror(retval)) ? msg : "");
                        if (LDBM_OS_ERR_IS_DISKFULL(retval))
                            disk_full = 1;
                        DEL_SET_ERROR(ldap_result_code,
                                      LDAP_OPERATIONS_ERROR, retry_count);
                        goto error_return;
                    }
                }

                retval = entryrdn_index_entry(be, tombstone, BE_INDEX_ADD, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "(adding tombstone entryrdn) DBI_RC_RETRY\n");
                    /* Retry txn */
                    continue;
                }
                if (retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "(adding tombstone entryrdn %s) failed, err=%d %s\n",
                                  slapi_entry_get_dn(tombstone->ep_entry),
                                  retval, (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
        } /* create_tombstone_entry */
        else if (delete_tombstone_entry) {
            /*
             * We need to remove the Tombstone entry from the remaining indexes:
             * objectclass=nsTombstone, nsUniqueID, nscpEntryDN, nsTombstoneCSN
             */
            char *nscpedn = NULL;

            retval = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS,
                                           SLAPI_ATTR_VALUE_TOMBSTONE, e->ep_id,
                                           BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "ldbm_back_delete", "(deleting %s) 0 DBI_RC_RETRY\n",
                              SLAPI_ATTR_VALUE_TOMBSTONE);
                /* Retry txn */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "(deleting %s) failed, err=%d %s\n",
                              SLAPI_ATTR_VALUE_TOMBSTONE, retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }

            /* Need to update the nsTombstoneCSN index */
            if ((tombstone_csn = entry_get_deletion_csn(e->ep_entry))) {
                csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
                retval = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN,
                                               deletion_csn_str, e->ep_id,
                                               BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "delete tombstone csn(deleting %s) DBI_RC_RETRY\n",
                                  deletion_csn_str);
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "delete tombsone csn(deleting %s) failed, err=%d %s\n",
                                  deletion_csn_str,
                                  retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval)) {
                        disk_full = 1;
                    }
                    DEL_SET_ERROR(ldap_result_code, LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }

            retval = index_addordel_string(be, SLAPI_ATTR_UNIQUEID,
                                           slapi_entry_get_uniqueid(e->ep_entry),
                                           e->ep_id, BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM,
                              "ldbm_back_delete", "(deleting %s) 1 DBI_RC_RETRY\n",
                              SLAPI_ATTR_UNIQUEID);
                /* Retry txn */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "(deleting %s) failed 1, err=%d %s\n",
                              SLAPI_ATTR_UNIQUEID, retval,
                              (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
            nscpedn = (char *)slapi_entry_attr_get_ref(e->ep_entry, SLAPI_ATTR_NSCP_ENTRYDN);
            if (nscpedn) {
                retval = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN,
                                               nscpedn, e->ep_id, BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "(deleting %s) 2 DBI_RC_RETRY\n",
                                  SLAPI_ATTR_NSCP_ENTRYDN);
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "(deleting %s) failed 2, err=%d %s\n",
                                  SLAPI_ATTR_NSCP_ENTRYDN, retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    DEL_SET_ERROR(ldap_result_code,
                                  LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
            /* delete usn from the entryusn index */
            entryusn_str = (char *)slapi_entry_attr_get_ref(e->ep_entry, SLAPI_ATTR_ENTRYUSN);
            if (entryusn_str) {
                retval = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN,
                                               entryusn_str, e->ep_id,
                                               BE_INDEX_DEL | BE_INDEX_EQUALITY, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "(deleting %s) 3 DBI_RC_RETRY\n",
                                  SLAPI_ATTR_ENTRYUSN);
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "(deleting %s) failed 3, err=%d %s\n",
                                  SLAPI_ATTR_ENTRYUSN, retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    DEL_SET_ERROR(ldap_result_code,
                                  LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
            if (entryrdn_get_switch()) /* subtree-rename: on */
            {
                retval = entryrdn_index_entry(be, e, BE_INDEX_DEL, &txn);
                if (DBI_RC_RETRY == retval) {
                    slapi_log_err(SLAPI_LOG_BACKLDBM,
                                  "ldbm_back_delete", "(deleting entryrdn) DBI_RC_RETRY\n");
                    /* Retry txn */
                    continue;
                }
                if (0 != retval) {
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "ldbm_back_delete", "(deleting entryrdn) failed, err=%d %s\n",
                                  retval,
                                  (msg = dblayer_strerror(retval)) ? msg : "");
                    if (LDBM_OS_ERR_IS_DISKFULL(retval))
                        disk_full = 1;
                    DEL_SET_ERROR(ldap_result_code,
                                  LDAP_OPERATIONS_ERROR, retry_count);
                    goto error_return;
                }
            }
        } /* delete_tombstone_entry */

        if (parent_found) {
            /* Push out the db modifications from the parent entry */
            retval = modify_update_all(be, pb, &parent_modify_c, &txn);
            slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                          "conn=%" PRIu64 " op=%d modify_update_all: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                          conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, retval);
            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "4 DEADLOCK\n");
                /* Retry txn */
                continue;
            }
            if (0 != retval) {
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete", "3 BAD, err=%d %s\n",
                              retval, (msg = dblayer_strerror(retval)) ? msg : "");
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }
        /*
         * first check if searchentry needs to be removed
         * Remove the entry from the Virtual List View indexes.
         */
        if (!delete_tombstone_entry && !is_ruv &&
            !vlv_delete_search_entry(pb, e->ep_entry, inst)) {
            retval = vlv_update_all_indexes(&txn, be, pb, e, NULL);

            if (DBI_RC_RETRY == retval) {
                slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete", "DEADLOCK vlv_update_all_indexes\n");
                /* Retry txn */
                continue;
            }
            if (retval != 0) {
                if (LDBM_OS_ERR_IS_DISKFULL(retval))
                    disk_full = 1;
                DEL_SET_ERROR(ldap_result_code,
                              LDAP_OPERATIONS_ERROR, retry_count);
                goto error_return;
            }
        }

        if (!is_ruv && !is_fixup_operation && !delete_tombstone_entry && !NO_RUV_UPDATE(li)) {
            ruv_c_init = ldbm_txn_ruv_modify_context(pb, &ruv_c);
            if (-1 == ruv_c_init) {
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "ldbm_txn_ruv_modify_context "
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
                slapi_log_err(SLAPI_LOG_ERR,
                              "ldbm_back_delete", "modify_update_all failed, err=%d %s\n", retval,
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
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete", "Retry count exceeded in delete\n");
        ldap_result_code = LDAP_BUSY;
        retval = -1;
        goto error_return;
    }
    if (create_tombstone_entry) {
        retval = cache_replace(&inst->inst_cache, e, tombstone);
        if (retval) {
            slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_delete", "cache_replace failed (%d): %s --> %s\n",
                          retval, slapi_entry_get_dn(e->ep_entry), slapi_entry_get_dn(tombstone->ep_entry));
            retval = -1;
            goto error_return;
        }
    }

    if (rc == 0 && opcsn && !is_fixup_operation && !delete_tombstone_entry) {
        /* URP Naming Collision
         * When an entry is deleted by a replicated delete operation
         * we must check for entries that have had a naming collision
         * with this entry. Now that this name has been given up, one
         * of those entries can take over the name.
         */
        slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN, slapi_ch_strdup(dn));
    }
    /* call the transaction post delete plugins just before the commit */
    if (!delete_tombstone_entry &&
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN)) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_delete", "SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN plugin "
                                                           "returned error code\n");
        if (!ldap_result_code) {
            slapi_pblock_get(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        if (!ldap_result_code) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_delete", "SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN plugin "
                                                             "returned error code but did not set SLAPI_RESULT_CODE\n");
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
        }
        if (!opreturn) {
            slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &opreturn);
        }
        if (!retval) {
            retval = -1;
        }
        if (!opreturn) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &retval);
        }
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &ldap_result_message);
        goto error_return;
    }
    if (parent_found) {
        /* Replace the old parent entry with the newly modified one */
        myrc = modify_switch_entries(&parent_modify_c, be);
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                      "conn=%" PRIu64 " op=%d modify_switch_entries: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                      conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, myrc);
        if (myrc == 0) {
            parent_switched = 1;
        }
    }

    retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN);
    if (retval) {
        ldbm_set_error(pb, retval, &ldap_result_code, &ldap_result_message);
        goto error_return;
    }

commit_return:
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

    /* delete from cache and clean up */
    if (e) {
        if (!create_tombstone_entry) {
            struct backentry *old_e = e;
            e = cache_find_id(&inst->inst_cache, e->ep_id);
            if (e != old_e) {
                cache_unlock_entry(&inst->inst_cache, old_e);
                CACHE_RETURN(&inst->inst_cache, &old_e);
            } else {
                CACHE_RETURN(&inst->inst_cache, &e);
            }
        }

        /*
         * e could have been replaced by cache_find_id(), recheck if it's NULL
         * before trying to unlock it, etc.
         */
        if (e) {
            if (cache_is_in_cache(&inst->inst_cache, e)) {
                ep_id = e->ep_id; /* Otherwise, e might have been freed. */
                CACHE_REMOVE(&inst->inst_cache, e);
            }
            cache_unlock_entry(&inst->inst_cache, e);
            CACHE_RETURN(&inst->inst_cache, &e);
            /*
             * e is unlocked and no longer in cache.
             * It could be freed at any moment.
             */
            e = NULL;
        }

        if (entryrdn_get_switch() && ep_id) { /* subtree-rename: on */
            /* since the op was successful, delete the tombstone dn from the dn cache */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, ep_id);
            if (bdn) { /* in the dncache, remove it. */
                CACHE_REMOVE(&inst->inst_dncache, bdn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            }
        }
    }

    if (ruv_c_init) {
        if (modify_switch_entries(&ruv_c, be) != 0) {
            ldap_result_code = LDAP_OPERATIONS_ERROR;
            slapi_log_err(SLAPI_LOG_ERR,
                          "ldbm_back_delete", "modify_switch_entries failed\n");
            retval = -1;
            goto error_return;
        }
    }

    rc = 0;
    goto common_return;

error_return:
    /* Revert the caches if this is the parent operation */
    if (parent_op) {
        revert_cache(inst, &parent_time);
    }

    if (tombstone) {
        if (cache_is_in_cache(&inst->inst_cache, tombstone)) {
            tomb_ep_id = tombstone->ep_id; /* Otherwise, tombstone might have been freed. */
        }
        if (entryrdn_get_switch() && tomb_ep_id) { /* subtree-rename: on */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache, tombstone->ep_id);
            if (bdn) { /* already in the dncache. Delete it. */
                CACHE_REMOVE(&inst->inst_dncache, bdn);
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            }
        }

        if (cache_is_in_cache(&inst->inst_cache, tombstone)) {
            CACHE_REMOVE(&inst->inst_cache, tombstone);
        }
        CACHE_RETURN(&inst->inst_cache, &tombstone);
        tombstone = NULL;
    }

    if (retval == DBI_RC_RUNRECOVERY) {
        dblayer_remember_disk_filled(li);
        ldbm_nasty("ldbm_back_delete", "Delete", 79, retval);
        disk_full = 1;
    }

    if (disk_full) {
        rc = return_on_disk_full(li);
        goto diskfull_return;
    } else
        rc = SLAPI_FAIL_GENERAL;

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
            opreturn = retval;
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &retval);
        }

        /* call the transaction post delete plugins just before the abort */
        if (!delete_tombstone_entry &&
            plugin_call_plugins(pb, SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN)) {
            slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_delete", "SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN plugin "
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
        }
        retval = plugin_call_mmr_plugin_postop(pb, NULL,SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN);

        /* Release SERIAL LOCK */
        dblayer_txn_abort(be, &txn); /* abort crashes in case disk full */
        /* txn is no longer valid - reset the txn pointer to the parent */
        slapi_pblock_set(pb, SLAPI_TXN, parent_txn);
    }
    if (parent_switched) {
        /*
         * Restore the old parent entry, switch the new with the original.
         * Otherwise the numsubordinate count will be off, and could later
         * be written to disk.
         */
        myrc = modify_unswitch_entries(&parent_modify_c, be);
        slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                      "conn=%" PRIu64 " op=%d modify_unswitch_entries: old_entry=0x%p, new_entry=0x%p, rc=%d\n",
                      conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry, myrc);
    }

common_return:
    if (orig_entry) {
        /* NOTE: #define SLAPI_DELETE_BEPREOP_ENTRY SLAPI_ENTRY_PRE_OP */
        /* so if orig_entry is NULL, we will wipe out SLAPI_ENTRY_PRE_OP
           for the post op plugins */
        slapi_pblock_set(pb, SLAPI_DELETE_BEPREOP_ENTRY, orig_entry);
    }
    if (inst && tombstone) {
        if ((0 == retval) && entryrdn_get_switch()) { /* subtree-rename: on */
            /* since the op was successful, add the addingentry's dn to the dn cache */
            struct backdn *bdn = dncache_find_id(&inst->inst_dncache,
                                                 tombstone->ep_id);
            if (bdn) { /* already in the dncache */
                CACHE_RETURN(&inst->inst_dncache, &bdn);
            } else { /* not in the dncache yet */
                Slapi_DN *tombstonesdn = slapi_sdn_dup(slapi_entry_get_sdn(tombstone->ep_entry));
                if (tombstonesdn) {
                    bdn = backdn_init(tombstonesdn, tombstone->ep_id, 0);
                    if (bdn) {
                        CACHE_ADD(&inst->inst_dncache, bdn, NULL);
                        slapi_log_err(SLAPI_LOG_CACHE, "ldbm_back_delete",
                                      "set %s to dn cache\n", slapi_sdn_get_dn(tombstonesdn));
                        CACHE_RETURN(&inst->inst_dncache, &bdn);
                    }
                }
            }
        }
        CACHE_RETURN(&inst->inst_cache, &tombstone);
        tombstone = NULL;
    }

    /* result code could be used in the bepost plugin functions. */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_result_code);
    /*
     * The bepostop is called even if the operation fails,
     * but not if the operation is purging tombstones.
     */
    if (!delete_tombstone_entry) {
        plugin_call_plugins(pb, SLAPI_PLUGIN_BE_POST_DELETE_FN);
    }
    /* Need to return to cache after post op plugins are called */
    if (e) {
        if (cache_is_in_cache(&inst->inst_cache, e)) {
            if (remove_e_from_cache) {
                /* The entry is already transformed to a tombstone. */
                CACHE_REMOVE(&inst->inst_cache, e);
            }
        }
        cache_unlock_entry(&inst->inst_cache, e);
        CACHE_RETURN(&inst->inst_cache, &e);
        /*
         * e is unlocked and no longer in cache.
         * It could be freed at any moment.
         */
        e = NULL;
    }
    if (inst->inst_ref_count) {
        slapi_counter_decrement(inst->inst_ref_count);
    }
    if (ruv_c_init) {
        modify_term(&ruv_c, be);
    }

diskfull_return:
    if (ldap_result_code != -1) {
        if (not_an_error) {
            /* This is mainly used by urp.  Solved conflict is not an error.
             * And we don't want the supplier to halt sending the updates. */
            ldap_result_code = LDAP_SUCCESS;
        }
        if (!result_sent) {
            slapi_send_ldap_result(pb, ldap_result_code, NULL, ldap_result_message, 0, NULL);
        }
    }
    slapi_log_err(SLAPI_LOG_BACKLDBM, "ldbm_back_delete",
                  "conn=%" PRIu64 " op=%d modify_term: old_entry=0x%p, new_entry=0x%p, in_cache=%d\n",
                  conn_id, op_id, parent_modify_c.old_entry, parent_modify_c.new_entry,
                  inst ? cache_is_in_cache(&inst->inst_cache, parent_modify_c.new_entry):-1);
    myrc = modify_term(&parent_modify_c, be);
    if (free_delete_existing_entry) {
        done_with_pblock_entry(pb, SLAPI_DELETE_EXISTING_ENTRY);
    } else { /* owned by someone else */
        slapi_pblock_set(pb, SLAPI_DELETE_EXISTING_ENTRY, NULL);
    }
    backentry_free(&original_tombstone);
    backentry_free(&tmptombstone);
    slapi_ch_free((void **)&errbuf);
    slapi_sdn_done(&nscpEntrySDN);
    slapi_ch_free_string(&e_uniqueid);
    slapi_sdn_done(&parentsdn);
    if (pb_conn) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_delete", "leave conn=%" PRIu64 " op=%d\n",
                      pb_conn->c_connid, operation->o_opid);
    }

    return rc;
}
