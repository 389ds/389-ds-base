/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include "sync.h"

/* Main list of established persistent synchronizaton searches */
static SyncRequestList *sync_request_list = NULL;
/*
 * Convenience macros for locking the list of persistent searches
 */
#define SYNC_LOCK_READ() slapi_rwlock_rdlock(sync_request_list->sync_req_rwlock)
#define SYNC_UNLOCK_READ() slapi_rwlock_unlock(sync_request_list->sync_req_rwlock)
#define SYNC_LOCK_WRITE() slapi_rwlock_wrlock(sync_request_list->sync_req_rwlock)
#define SYNC_UNLOCK_WRITE() slapi_rwlock_unlock(sync_request_list->sync_req_rwlock)

/*
 * Convenience macro for checking if the Content Synchronization subsystem has
 * been initialized.
 */
#define SYNC_IS_INITIALIZED() (sync_request_list != NULL)

static int plugin_closing = 0;
static PRUint64 thread_count = 0;
static int sync_add_request(SyncRequest *req);
static void sync_remove_request(SyncRequest *req);
static SyncRequest *sync_request_alloc(void);
void sync_queue_change(OPERATION_PL_CTX_T *operation);
static void sync_send_results(void *arg);
static void sync_request_wakeup_all(void);
static void sync_node_free(SyncQueueNode **node);

static int sync_acquire_connection(Slapi_Connection *conn);
static int sync_release_connection(Slapi_PBlock *pb, Slapi_Connection *conn, Slapi_Operation *op, int release);

/* This routine appends the operation at the end of the
 * per thread pending list of nested operation..
 * being a betxn_preop the pending list has the same order
 * that the server received the operation
 *
 * In case of DB_RETRY, this callback can be called several times
 * The detection of the DB_RETRY is done via the operation extension
 */
int
sync_update_persist_betxn_pre_op(Slapi_PBlock *pb)
{
    OPERATION_PL_CTX_T *prim_op;
    OPERATION_PL_CTX_T *new_op;
    Slapi_DN *sdn;
    uint32_t idx_pl = 0;
    op_ext_ident_t *op_ident;
    Operation *op;

    if (!SYNC_IS_INITIALIZED()) {
        /* not initialized if sync plugin is not started */
        return 0;
    }

    prim_op = get_thread_primary_op();
    op_ident = sync_persist_get_operation_extension(pb);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);

    /* Check if we are in a DB retry case */
    if (op_ident && prim_op) {
        OPERATION_PL_CTX_T *current_op;

        /* This callback is called (with the same operation) because of a DB_RETRY */

        /* It already existed (in the operation extension) an index of the operation in the pending list */
        for (idx_pl = 0, current_op = prim_op; current_op->next; idx_pl++, current_op = current_op->next) {
            if (op_ident->idx_pl == idx_pl) {
                break;
            }
        }

        /* The retrieved operation in the pending list is at the right
         * index and state. Just return making this callback a noop
         */
        PR_ASSERT(current_op);
        PR_ASSERT(current_op->op == op);
        PR_ASSERT(current_op->flags == OPERATION_PL_PENDING);
        slapi_log_err(SLAPI_LOG_WARNING, SYNC_PLUGIN_SUBSYSTEM, "sync_update_persist_betxn_pre_op - DB retried operation targets "
                      "\"%s\" (op=0x%lx idx_pl=%d) => op not changed in PL\n",
                      slapi_sdn_get_dn(sdn), (ulong) op, idx_pl);
        return 0;
    }

    /* Create a new pending operation node */
    new_op = (OPERATION_PL_CTX_T *)slapi_ch_calloc(1, sizeof(OPERATION_PL_CTX_T));
    new_op->flags = OPERATION_PL_PENDING;
    new_op->op = op;

    if (prim_op) {
        /* It already exists a primary operation, so the current
         * operation is a nested one that we need to register at the end
         * of the pending nested operations
         * Also computes the idx_pl that will be the identifier (index) of the operation
         * in the pending list
         */
        OPERATION_PL_CTX_T *current_op;
        for (idx_pl = 0, current_op = prim_op; current_op->next; idx_pl++, current_op = current_op->next);
        current_op->next = new_op;
        idx_pl++; /* idx_pl is currently the index of the last op
                   * as we are adding a new op we need to increase that index
                   */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_update_persist_betxn_pre_op - nested operation targets "
                      "\"%s\" (op=0x%lx idx_pl=%d)\n",
                      slapi_sdn_get_dn(sdn), (ulong) new_op->op, idx_pl);
    } else {
        /* The current operation is the first/primary one in the txn
         * registers it directly in the thread private data (head)
         */
        set_thread_primary_op(new_op);
        idx_pl = 0; /* as primary operation, its index in the pending list is 0 */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_update_persist_betxn_pre_op - primary operation targets "
                      "\"%s\" (0x%lx)\n",
                      slapi_sdn_get_dn(sdn), (ulong) new_op->op);
    }

    /* records, in the operation extension AND in the pending list, the identifier (index) of
     * this operation into the pending list
     */
    op_ident = (op_ext_ident_t *) slapi_ch_calloc(1, sizeof (op_ext_ident_t));
    op_ident->idx_pl = idx_pl;
    new_op->idx_pl   = idx_pl;
    sync_persist_set_operation_extension(pb, op_ident);
    return 0;
}

/* This operation failed or skipped (e.g. no MODs).
 * In such case POST entry does not exist
 */
static void
ignore_op_pl(Slapi_PBlock *pb)
{
    OPERATION_PL_CTX_T *prim_op, *curr_op;
    op_ext_ident_t *ident;
    Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /* prim_op is set if betxn was called
     * In case of invalid update (schema violation) the
     * operation skip betxn and prim_op is not set.
     * This is the same for ident
     */
    prim_op = get_thread_primary_op();
    if (prim_op == NULL) {
        /* This can happen if the PRE_OP (sync_update_persist_betxn_pre_op) was not called.
         * The only known case it happens is with dynamic plugin enabled and an
         * update that enable the sync_repl plugin. In such case sync_repl registers
         * the postop (sync_update_persist_op) that is called while the preop was not called
         */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM,
              "ignore_op_pl - Operation without primary op set (0x%lx)\n",
              (ulong) op);
        return;
    }
    ident = sync_persist_get_operation_extension(pb);

    if (ident) {
        /* The TXN_BEPROP was called, so the operation is
         * registered in the pending list
         */
        for (curr_op = prim_op; curr_op; curr_op = curr_op->next) {
            if (curr_op->idx_pl == ident->idx_pl) {
                /* The operation extension (ident) refers this operation (currop in the pending list).
                 * This is called during sync_repl postop. At this moment
                 * the operation in the pending list (identified by idx_pl in the operation extension)
                 * should be pending
                 */
                PR_ASSERT(curr_op->flags == OPERATION_PL_PENDING);
                slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "ignore_op_pl operation (op=0x%lx, idx_pl=%d) from the pending list\n",
                        (ulong) op, ident->idx_pl);
                curr_op->flags = OPERATION_PL_IGNORED;
                return;
            }
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "ignore_op_pl failing operation (op=0x%lx, idx_pl=%d) was not in the pending list\n",
                    (ulong) op, ident ? ident->idx_pl : -1);
}

/* This is a generic function that is called by betxn_post of this plugin.
 * For the given operation (pb->pb_op) it sets in the pending list the state
 * of the completed operation.
 * When all operations are completed, if the primary operation is successful it
 * flushes (enqueue) the operations to the sync repl queue(s), else it just free
 * the pending list (skipping enqueue). 
 */
static void
sync_update_persist_op(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t op_tag, char *label)
{
    OPERATION_PL_CTX_T *prim_op = NULL, *curr_op;
    Operation *pb_op;
    op_ext_ident_t *ident;
    Slapi_DN *sdn;
    uint32_t count; /* use for diagnostic of the lenght of the pending list */
    int32_t rc;

    if (!SYNC_IS_INITIALIZED()) {
        /* not initialized if sync plugin is not started */
        return;
    }
    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);

    if (NULL == e) {
        /* Ignore this operation (for example case of failure of the operation
         * or operation resulting in an empty Mods))
         */
        ignore_op_pl(pb);
        return;
    }
    
    /* Retrieve the result of the operation */
    if (slapi_op_internal(pb)) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (0 != rc) {
            /* The internal operation did not succeed */
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "internal operation Failed (0x%lx) rc=%d\n",
                       (ulong) pb_op, rc);
        }
    } else {
        slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        if (0 != rc) {
            /* The operation did not succeed */
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "direct operation Failed (0x%lx) rc=%d\n",
                       (ulong) pb_op, rc);
        }
    }


    prim_op = get_thread_primary_op();
    if (prim_op == NULL) {
        /* This can happen if the PRE_OP (sync_update_persist_betxn_pre_op) was not called.
         * The only known case it happens is with dynamic plugin enabled and an
         * update that enable the sync_repl plugin. In such case sync_repl registers
         * the postop (sync_update_persist_op) that is called while the preop was not called
         */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_update_persist_op - Operation without primary op set (0x%lx)\n",
                      (ulong) pb_op);
        return;
    }
    ident = sync_persist_get_operation_extension(pb);

    if ((ident == NULL) && operation_is_flag_set(pb_op, OP_FLAG_NOOP)) {
        /* This happens for URP (add cenotaph, fixup rename, tombstone resurrect)
         * As a NOOP betxn plugins are not called and operation ext is not created
         */
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "Skip noop operation (0x%lx)\n",
                       (ulong) pb_op);
        return;
    }
    assert(ident);
    /* First mark the operation as completed/failed
     * the param to be used once the operation will be pushed
     * on the listeners queue
     */
    for (curr_op = prim_op; curr_op; curr_op = curr_op->next) {
        if (curr_op->idx_pl == ident->idx_pl) {
            /* The operation extension (ident) refers this operation (currop in the pending list)
             * This is called during sync_repl postop. At this moment
             * the operation in the pending list (identified by idx_pl in the operation extension)
             * should be pending
             */
            PR_ASSERT(curr_op->flags == OPERATION_PL_PENDING);
            if (rc == LDAP_SUCCESS) {
                curr_op->flags = OPERATION_PL_SUCCEEDED;
                curr_op->entry = slapi_entry_dup(e);
                curr_op->eprev = eprev ? slapi_entry_dup(eprev) : NULL;
                curr_op->chgtype = op_tag;
            } else {
                curr_op->flags = OPERATION_PL_FAILED;
            }
            break;
        }
    }
    if (!curr_op) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM, "%s - operation (op=0x%lx, idx_pl=%d) not found on the pendling list\n", 
                      label, (ulong) pb_op, ident->idx_pl);
        PR_ASSERT(curr_op);
    }
    
    /* for diagnostic of the pending list, dump its content if it is too long */
    for (count = 0, curr_op = prim_op; curr_op; count++, curr_op = curr_op->next);
    if (loglevel_is_set(SLAPI_LOG_PLUGIN) && (count > 10)) {

        /* if pending list looks abnormally too long, dump the pending list */
        for (curr_op = prim_op; curr_op; curr_op = curr_op->next) {
            char *flags_str;
            char * entry_str;

            if (curr_op->entry) {
                entry_str = slapi_entry_get_dn(curr_op->entry);
            } else if (curr_op->eprev) {
                entry_str = slapi_entry_get_dn(curr_op->eprev);
            } else {
                entry_str = "unknown";
            }
            switch (curr_op->flags) {
                case OPERATION_PL_SUCCEEDED:
                    flags_str = "succeeded";
                    break;
                case OPERATION_PL_FAILED:
                    flags_str = "failed";
                    break;
                case OPERATION_PL_IGNORED:
                    flags_str = "ignored";
                    break;
                case OPERATION_PL_PENDING:
                    flags_str = "pending";
                    break;
                default:
                    flags_str = "unknown";
                    break;


            }
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "dump pending list(0x%lx) %s %s\n",
                    (ulong) curr_op->op, entry_str, flags_str);
        }
    }

    /* Second check if it remains a pending operation in the pending list */
    for (curr_op = prim_op; curr_op; curr_op = curr_op->next) {
        if (curr_op->flags == OPERATION_PL_PENDING) {
            break;
        }
    }
    if (curr_op) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "%s - It remains a pending operation (0x%lx)\n", label, (ulong) curr_op->op);
    } else {
        OPERATION_PL_CTX_T *next = NULL;
        PRBool enqueue_it = PR_TRUE;
        /* all operations on the pending list are completed moved them
         * to the listeners queue in the same order as pending list.
         * If the primary operation failed, operation are not moved to
         * the queue
         */
        if (prim_op->flags == OPERATION_PL_FAILED) {
            /* if primary update failed, the txn is aborted and none of
             * the operations were applied. Just forget this pending list
             */
            enqueue_it = PR_FALSE;
        }
        for (curr_op = prim_op; curr_op; curr_op = next) {
            char *entry;
            if (curr_op->entry) {
                entry = slapi_entry_get_dn(curr_op->entry);
            } else if (curr_op->eprev){
                entry = slapi_entry_get_dn(curr_op->eprev);
            } else {
                entry = "unknown";
            }
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "Do %s enqueue (0x%lx) %s\n",
                    enqueue_it ? "" : "not", (ulong) curr_op->op, entry);
            if (enqueue_it) {
                sync_queue_change(curr_op);
            }
            
            /* now free this pending operation */
            next = curr_op->next;
            slapi_entry_free(curr_op->entry);
            slapi_entry_free(curr_op->eprev);
            slapi_ch_free((void **)&curr_op);
        }
        /* we consumed all the pending operation, free the pending list*/
        set_thread_primary_op(NULL);
    }
}
int
sync_add_persist_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e;
    if (!SYNC_IS_INITIALIZED()) {
        return (0);
    }

    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
    sync_update_persist_op(pb, e, NULL, LDAP_REQ_ADD, "sync_add_persist_post_op");

    return (0);
}

int
sync_del_persist_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e;

    if (!SYNC_IS_INITIALIZED()) {
        return (0);
    }

    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
    sync_update_persist_op(pb, e, NULL, LDAP_REQ_DELETE, "sync_del_persist_post_op");

    return (0);
}

int
sync_mod_persist_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e, *e_prev;

    if (!SYNC_IS_INITIALIZED()) {
        return (0);
    }

    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e_prev);
    sync_update_persist_op(pb, e, e_prev, LDAP_REQ_MODIFY, "sync_mod_persist_post_op");

    return (0);
}

int
sync_modrdn_persist_post_op(Slapi_PBlock *pb)
{
    Slapi_Entry *e, *e_prev;

    if (!SYNC_IS_INITIALIZED()) {
        return (0);
    }

    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e_prev);
    sync_update_persist_op(pb, e, e_prev, LDAP_REQ_MODRDN, "sync_mod_persist_post_op");

    return (0);
}

void
sync_queue_change(OPERATION_PL_CTX_T *operation)
{
    SyncRequest *req = NULL;
    SyncQueueNode *node = NULL;
    int matched = 0;
    int prev_match = 0;
    int cur_match = 0;
    Slapi_Entry *e = operation->entry;
    Slapi_Entry *eprev = operation->eprev;
    ber_int_t chgtype = operation->chgtype;

    if (!SYNC_IS_INITIALIZED()) {
        return;
    }

    if (NULL == e) {
        /* For now, some backends such as the chaining backend do not provide a post-op entry */
        return;
    }

    SYNC_LOCK_READ();

    for (req = sync_request_list->sync_req_head; NULL != req; req = req->req_next) {
        Slapi_DN *base = NULL;
        int scope;
        Slapi_Operation *op;

        /* Skip the nodes that have no more active operation
         */
        slapi_pblock_get(req->req_pblock, SLAPI_OPERATION, &op);
        if (op == NULL || slapi_op_abandoned(req->req_pblock)) {
            continue;
        }

        slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_TARGET_SDN, &base);
        slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_SCOPE, &scope);
        if (NULL == base) {
            base = slapi_sdn_new_dn_byref(req->req_orig_base);
            slapi_pblock_set(req->req_pblock, SLAPI_SEARCH_TARGET_SDN, base);
        }

        /*
         * See if the entry meets the scope and filter criteria.
         * We cannot do the acl check here as this thread
         * would then potentially clash with the ps_send_results()
         * thread on the aclpb in ps->req_pblock.
         * By avoiding the acl check in this thread, and leaving all the acl
         * checking to the ps_send_results() thread we avoid
         * the req_pblock contention problem.
         * The lesson here is "Do not give multiple threads arbitary access
         * to the same pblock" this kind of muti-threaded access
         * to the same pblock must be done carefully--there is currently no
         * generic satisfactory way to do this.
        */

        /* if the change is a modrdn then we need to check if the entry was
         * moved into scope, out of scope, or stays in scope
         */
        if (chgtype == LDAP_REQ_MODRDN || chgtype == LDAP_REQ_MODIFY)
            prev_match = slapi_sdn_scope_test(slapi_entry_get_sdn_const(eprev), base, scope) &&
                         (0 == slapi_vattr_filter_test(req->req_pblock, eprev, req->req_filter, 0 /* verify_access */));

        cur_match = slapi_sdn_scope_test(slapi_entry_get_sdn_const(e), base, scope) &&
                    (0 == slapi_vattr_filter_test(req->req_pblock, e, req->req_filter, 0 /* verify_access */));

        if (prev_match || cur_match) {
            SyncQueueNode *pOldtail;

            /* The scope and the filter match - enqueue it */

            matched++;
            node = (SyncQueueNode *)slapi_ch_calloc(1, sizeof(SyncQueueNode));

            if (chgtype == LDAP_REQ_MODRDN || chgtype == LDAP_REQ_MODIFY) {
                if (prev_match && cur_match)
                    node->sync_chgtype = LDAP_REQ_MODIFY;
                else if (prev_match)
                    node->sync_chgtype = LDAP_REQ_DELETE;
                else
                    node->sync_chgtype = LDAP_REQ_ADD;
            } else {
                node->sync_chgtype = chgtype;
            }
            if (node->sync_chgtype == LDAP_REQ_DELETE && chgtype == LDAP_REQ_MODIFY) {
                /* use previous entry to pass the filter test in sync_send_results */
                node->sync_entry = slapi_entry_dup(eprev);
            } else {
                node->sync_entry = slapi_entry_dup(e);
            }
            /* Put it on the end of the list for this sync search */
            PR_Lock(req->req_lock);
            pOldtail = req->ps_eq_tail;
            req->ps_eq_tail = node;
            if (NULL == req->ps_eq_head) {
                req->ps_eq_head = req->ps_eq_tail;
            } else {
                pOldtail->sync_next = req->ps_eq_tail;
            }
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_queue_change - entry "
                                                              "\"%s\" \n",
                      slapi_entry_get_dn_const(node->sync_entry));
            PR_Unlock(req->req_lock);
        }
    }
    /* Were there any matches? */
    if (matched) {
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_queue_change - enqueued entry "
                                                              "\"%s\" on %d request listeners\n",
                      slapi_entry_get_dn_const(e), matched);
    } else {
        slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_queue_change - entry "
                                                              "\"%s\" not enqueued on any request search listeners\n",
                      slapi_entry_get_dn_const(e));
    }
    SYNC_UNLOCK_READ();

    /* Were there any matches? */
    if (matched) {
        /* Notify update threads */
        sync_request_wakeup_all();
    }
}
/*
 * Initialize the list structure which contains the list
 * of established content sync persistent requests
 */
int
sync_persist_initialize(int argc, char **argv)
{
    if (!SYNC_IS_INITIALIZED()) {
        pthread_condattr_t sync_req_condAttr; /* cond var attribute */
        int rc = 0;

        sync_request_list = (SyncRequestList *)slapi_ch_calloc(1, sizeof(SyncRequestList));
        if ((sync_request_list->sync_req_rwlock = slapi_new_rwlock()) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM, "sync_persist_initialize - Cannot initialize lock structure(1).\n");
            return (-1);
        }
        if (pthread_mutex_init(&(sync_request_list->sync_req_cvarlock), NULL) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "sync_persist_initialize",
                          "Failed to create lock: error %d (%s)\n",
                          rc, strerror(rc));
            return (-1);
        }
        if ((rc = pthread_condattr_init(&sync_req_condAttr)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "sync_persist_initialize",
                          "Failed to create new condition attribute variable. error %d (%s)\n",
                          rc, strerror(rc));
            return (-1);
        }
        if ((rc = pthread_condattr_setclock(&sync_req_condAttr, CLOCK_MONOTONIC)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "sync_persist_initialize",
                          "Cannot set condition attr clock. error %d (%s)\n",
                          rc, strerror(rc));
            return (-1);
        }
        if ((rc = pthread_cond_init(&(sync_request_list->sync_req_cvar), &sync_req_condAttr)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "sync_persist_initialize",
                          "Failed to create new condition variable. error %d (%s)\n",
                          rc, strerror(rc));
            return (-1);
        }
        pthread_condattr_destroy(&sync_req_condAttr); /* no longer needed */

        sync_request_list->sync_req_head = NULL;
        sync_request_list->sync_req_cur_persist = 0;
        sync_request_list->sync_req_max_persist = SYNC_MAX_CONCURRENT;
        if (argc > 0) {
            /* for now the only plugin arg is the max concurrent
             * persistent sync searches
             */
            sync_request_list->sync_req_max_persist = sync_number2int(argv[0]);
            if (sync_request_list->sync_req_max_persist == -1) {
                sync_request_list->sync_req_max_persist = SYNC_MAX_CONCURRENT;
            }
        }
        plugin_closing = 0;
    }
    return (0);
}
/*
 * Add the given pblock to the list of established sync searches.
 * Then, start a thread to send the results to the client as they
 * are dispatched by add, modify, and modrdn operations.
 */
PRThread *
sync_persist_add(Slapi_PBlock *pb)
{
    SyncRequest *req = NULL;
    char *base;
    Slapi_Filter *filter;

    if (SYNC_IS_INITIALIZED() && NULL != pb) {
        /* Create the new node */
        req = sync_request_alloc();
        assert(req); /* avoid gcc_analyzer warning */
        assert(pb); /* avoid gcc_analyzer warning */
        slapi_pblock_get(pb, SLAPI_OPERATION, &req->req_orig_op); /* neede to access original op */
        req->req_pblock = sync_pblock_copy(pb);
        slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &base);
        req->req_orig_base = slapi_ch_strdup(base);
        slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &filter);
        req->req_filter = slapi_filter_dup(filter);

        /* Add it to the head of the list of persistent searches */
        if (0 == sync_add_request(req)) {

            /* Start a thread to send the results */
            req->req_tid = PR_CreateThread(PR_USER_THREAD, sync_send_results,
                                           (void *)req, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                           PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);

            /* Checking if the thread is succesfully created and
                 * if the thread is not created succesfully.... we send
             * error messages to the Log file
             */
            if (NULL == (req->req_tid)) {
                int prerr;
                prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                              "sync_persist_add - Failed to create persitent thread, error %d (%s)\n",
                              prerr, slapi_pr_strerror(prerr));
                /* Now remove the ps from the list so call the function ps_remove */
                sync_remove_request(req);
                PR_DestroyLock(req->req_lock);
                req->req_lock = NULL;
                slapi_ch_free((void **)&req->req_pblock);
                slapi_ch_free((void **)&req);
            } else {
                thread_count++;
                return (req->req_tid);
            }
        }
    }
    return (NULL);
}

int
sync_persist_startup(PRThread *tid, Sync_Cookie *cookie)
{
    SyncRequest *cur;
    int rc = 1;

    if (SYNC_IS_INITIALIZED() && NULL != tid) {
        SYNC_LOCK_READ();
        /* Find and change */
        cur = sync_request_list->sync_req_head;
        while (NULL != cur) {
            if (cur->req_tid == tid) {
                cur->req_active = PR_TRUE;
                cur->req_cookie = cookie;
                rc = 0;
                break;
            }
            cur = cur->req_next;
        }
        SYNC_UNLOCK_READ();
    }
    return (rc);
}


int
sync_persist_terminate(PRThread *tid)
{
    SyncRequest *cur;
    int rc = 1;

    if (SYNC_IS_INITIALIZED() && NULL != tid) {
        SYNC_LOCK_READ();
        /* Find and change */
        cur = sync_request_list->sync_req_head;
        while (NULL != cur) {
            if (cur->req_tid == tid) {
                cur->req_active = PR_FALSE;
                cur->req_complete = PR_TRUE;
                rc = 0;
                break;
            }
            cur = cur->req_next;
        }
        SYNC_UNLOCK_READ();
    }
    if (rc == 0) {
        sync_remove_request(cur);
    }
    return (rc);
}

/*
 * Called when stopping/disabling the plugin (like shutdown)
 */
int
sync_persist_terminate_all()
{
    SyncRequest *req = NULL, *next;
    if (SYNC_IS_INITIALIZED()) {
        /* signal the threads to stop */
        plugin_closing = 1;
        sync_request_wakeup_all();

        /* wait for all the threads to finish */
        while (thread_count > 0) {
            PR_Sleep(PR_SecondsToInterval(1));
        }

        slapi_destroy_rwlock(sync_request_list->sync_req_rwlock);
        pthread_mutex_destroy(&(sync_request_list->sync_req_cvarlock));
		pthread_cond_destroy(&(sync_request_list->sync_req_cvar));

        /* it frees the structures, just in case it remained connected sync_repl client */
        for (req = sync_request_list->sync_req_head; NULL != req; req = next) {
            next = req->req_next;
            slapi_pblock_destroy(req->req_pblock);
            req->req_pblock = NULL;
            PR_DestroyLock(req->req_lock);
            req->req_lock = NULL;
            slapi_ch_free((void **)&req);
        }
        slapi_ch_free((void **)&sync_request_list);
    }

    return (0);
}

/*
 * Allocate and initialize an empty Sync node.
 */
static SyncRequest *
sync_request_alloc(void)
{
    SyncRequest *req;

    req = (SyncRequest *)slapi_ch_calloc(1, sizeof(SyncRequest));

    req->req_pblock = NULL;
    if ((req->req_lock = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM, "sync_request_alloc - Cannot initialize lock structure.\n");
        slapi_ch_free((void **)&req);
        return (NULL);
    }
    req->req_tid = (PRThread *)NULL;
    req->req_complete = 0;
    req->req_cookie = NULL;
    req->ps_eq_head = req->ps_eq_tail = (SyncQueueNode *)NULL;
    req->req_next = NULL;
    req->req_active = PR_FALSE;
    return req;
}


/*
 * Add the given persistent search to the
 * head of the list of persistent searches.
 */
static int
sync_add_request(SyncRequest *req)
{
    int rc = 0;
    if (SYNC_IS_INITIALIZED() && NULL != req) {
        SYNC_LOCK_WRITE();
        if (sync_request_list->sync_req_cur_persist < sync_request_list->sync_req_max_persist) {
            sync_request_list->sync_req_cur_persist++;
            req->req_next = sync_request_list->sync_req_head;
            sync_request_list->sync_req_head = req;
        } else {
            rc = 1;
        }
        SYNC_UNLOCK_WRITE();
    }
    return (rc);
}

static void
sync_remove_request(SyncRequest *req)
{
    SyncRequest *cur;
    int removed = 0;

    if (SYNC_IS_INITIALIZED() && NULL != req) {
        SYNC_LOCK_WRITE();
        if (NULL == sync_request_list->sync_req_head) {
            /* should not happen, attempt to remove a request never added */
        } else if (req == sync_request_list->sync_req_head) {
            /* Remove from head */
            sync_request_list->sync_req_head = sync_request_list->sync_req_head->req_next;
            removed = 1;
        } else {
            /* Find and remove from list */
            cur = sync_request_list->sync_req_head;
            while (NULL != cur->req_next) {
                if (cur->req_next == req) {
                    cur->req_next = cur->req_next->req_next;
                    removed = 1;
                    break;
                } else {
                    cur = cur->req_next;
                }
            }
        }
        if (removed) {
            sync_request_list->sync_req_cur_persist--;
        }
        SYNC_UNLOCK_WRITE();
        if (!removed) {
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_remove_request - "
                                                                   "Attempt to remove nonexistent req\n");
        }
    }
}

static void
sync_request_wakeup_all(void)
{
    if (SYNC_IS_INITIALIZED()) {
        pthread_mutex_lock(&(sync_request_list->sync_req_cvarlock));
        pthread_cond_broadcast(&(sync_request_list->sync_req_cvar));
        pthread_mutex_unlock(&(sync_request_list->sync_req_cvarlock));
    }
}

static int
sync_acquire_connection(Slapi_Connection *conn)
{
    int rc;
    /* need to acquire a reference to this connection so that it will not
       be released or cleaned up out from under us

       in psearch.c it is implemented as:
        PR_Lock( ps->req_pblock->pb_conn->c_mutex );
        conn_acq_flag = connection_acquire_nolock(ps->req_pblock->pb_conn);
            PR_Unlock( ps->req_pblock->pb_conn->c_mutex );


    HOW TO DO FROM A PLUGIN
    - either expose the functions from the connection code in the private api
      and allow to link them in
    - or fake a connection structure
        struct fake_conn {
            void    *needed1
            void    *needed2
            void    *pad1
            void    *pad2
            void    *needed3;
        }
        struct fake_conn *c = (struct fake_conn *) conn;
        c->needed3 ++;
        this would require knowledge or analysis of the connection structure,
        could probably be done for servers with a common history
    */
    /* use exposed slapi_connection functions */
    rc = slapi_connection_acquire(conn);
    return (rc);
}

static int
sync_release_connection(Slapi_PBlock *pb, Slapi_Connection *conn, Slapi_Operation *op, int release)
{
    /* see comments in sync_acquire_connection */

    /* using exposed connection handling functions */

    slapi_connection_remove_operation(pb, conn, op, release);

    return (0);
}
/*
 * Thread routine for sending search results to a client
 * which is persistently waiting for them.
 *
 * This routine will terminate when either (a) the ps_complete
 * flag is set, or (b) the associated operation is abandoned.
 * In any case, the thread won't notice until it wakes from
 * sleeping on the ps_list condition variable, so it needs
 * to be awakened.
 */
static void
sync_send_results(void *arg)
{
    SyncRequest *req = (SyncRequest *)arg;
    SyncQueueNode *qnode, *qnodenext;
    int conn_acq_flag = 0;
    Slapi_Connection *conn = NULL;
    Slapi_Operation *op = req->req_orig_op;
    int rc;
    PRUint64 connid;
    int opid;
    char **attrs_dup;
    char *strFilter;

    slapi_pblock_get(req->req_pblock, SLAPI_CONN_ID, &connid);
    slapi_pblock_get(req->req_pblock, SLAPI_OPERATION_ID, &opid);
    slapi_pblock_get(req->req_pblock, SLAPI_CONNECTION, &conn);
    if (NULL == conn) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_send_results - conn=%" PRIu64 " op=%d Null connection - aborted\n",
                      connid, opid);
        goto done;
    }
    conn_acq_flag = sync_acquire_connection(conn);
    if (conn_acq_flag) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_send_results - conn=%" PRIu64 " op=%d Could not acquire the connection - aborted\n",
                      connid, opid);
        goto done;
    }

    pthread_mutex_lock(&(sync_request_list->sync_req_cvarlock));

    while ((conn_acq_flag == 0) && !req->req_complete && !plugin_closing) {
        /* Check for an abandoned operation */
        if (op == NULL || slapi_is_operation_abandoned(op)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM,
                          "sync_send_results - conn=%" PRIu64 " op=%d Operation no longer active - terminating\n",
                          connid, opid);
            break;
        }
        if (NULL == req->ps_eq_head || !req->req_active) {
            /* Nothing to do yet, or the refresh phase is not yet completed */
            /* If an operation is abandoned, we do not get notified by the
             * connection code. Wake up every second to check if thread
             * should terminate.
             */
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += 1;
            pthread_cond_timedwait(&(sync_request_list->sync_req_cvar),
                                   &(sync_request_list->sync_req_cvarlock),
                                   &current_time);
        } else {
            /* dequeue the item */
            int attrsonly;
            char **attrs;
            char **noattrs = NULL;
            LDAPControl **ectrls = NULL;
            Slapi_Entry *ec;
            int chg_type = LDAP_SYNC_NONE;

            /* dequeue one element */
            PR_Lock(req->req_lock);
            qnode = req->ps_eq_head;
            slapi_log_err(SLAPI_LOG_PLUGIN, SYNC_PLUGIN_SUBSYSTEM, "sync_queue_change - dequeue  "
                          "\"%s\" \n",
                          slapi_entry_get_dn_const(qnode->sync_entry));
            req->ps_eq_head = qnode->sync_next;
            if (NULL == req->ps_eq_head) {
                req->ps_eq_tail = NULL;
            }
            PR_Unlock(req->req_lock);

            /* Get all the information we need to send the result */
            ec = qnode->sync_entry;
            slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_ATTRS, &attrs);
            slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_ATTRSONLY, &attrsonly);

            /*
             * Send the result.  Since send_ldap_search_entry can block for
             * up to 30 minutes, we relinquish all locks before calling it.
             */
            pthread_mutex_unlock(&(sync_request_list->sync_req_cvarlock));

            /*
             * The entry is in the right scope and matches the filter
             * but we need to redo the filter test here to check access
             * controls. See the comments at the slapi_filter_test()
             * call in sync_persist_add().
            */

            if (slapi_vattr_filter_test(req->req_pblock, ec, req->req_filter,
                                        1 /* verify_access */) == 0) {
                slapi_pblock_set(req->req_pblock, SLAPI_SEARCH_RESULT_ENTRY, ec);

                /* NEED TO BUILD THE CONTROL */
                switch (qnode->sync_chgtype) {
                case LDAP_REQ_ADD:
                    chg_type = LDAP_SYNC_ADD;
                    break;
                case LDAP_REQ_MODIFY:
                    chg_type = LDAP_SYNC_MODIFY;
                    break;
                case LDAP_REQ_MODRDN:
                    chg_type = LDAP_SYNC_MODIFY;
                    break;
                case LDAP_REQ_DELETE:
                    chg_type = LDAP_SYNC_DELETE;
                    noattrs = (char **)slapi_ch_calloc(2, sizeof(char *));
                    noattrs[0] = slapi_ch_strdup("1.1");
                    noattrs[1] = NULL;
                    break;
                }
                ectrls = (LDAPControl **)slapi_ch_calloc(2, sizeof(LDAPControl *));
                if (req->req_cookie) {
                    sync_cookie_update(req->req_cookie, ec);
                }
                sync_create_state_control(ec, &ectrls[0], chg_type, req->req_cookie, PR_FALSE);
                rc = slapi_send_ldap_search_entry(req->req_pblock,
                                                  ec, ectrls,
                                                  noattrs ? noattrs : attrs, attrsonly);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_CONNS, SYNC_PLUGIN_SUBSYSTEM,
                                  "sync_send_results - Error %d sending entry %s\n",
                                  rc, slapi_entry_get_dn_const(ec));
                }
                ldap_controls_free(ectrls);
                slapi_ch_array_free(noattrs);
            }
            pthread_mutex_lock(&(sync_request_list->sync_req_cvarlock));

            /* Deallocate our wrapper for this entry */
            sync_node_free(&qnode);
        }
    }
    pthread_mutex_unlock(&(sync_request_list->sync_req_cvarlock));

    /* indicate the end of search */
    sync_release_connection(req->req_pblock, conn, op, conn_acq_flag == 0);

done:
    /* This client closed the connection or shutdown, free the req */
    sync_remove_request(req);
    PR_DestroyLock(req->req_lock);
    req->req_lock = NULL;

    slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_ATTRS, &attrs_dup);
    slapi_ch_array_free(attrs_dup);
    slapi_pblock_set(req->req_pblock, SLAPI_SEARCH_ATTRS, NULL);

    slapi_pblock_get(req->req_pblock, SLAPI_SEARCH_STRFILTER, &strFilter);
    slapi_ch_free((void **)&strFilter);
    slapi_pblock_set(req->req_pblock, SLAPI_SEARCH_STRFILTER, NULL);

    slapi_pblock_destroy(req->req_pblock);
    req->req_pblock = NULL;

    slapi_ch_free((void **)&req->req_orig_base);
    slapi_filter_free(req->req_filter, 1);
    sync_cookie_free(&req->req_cookie);
    for (qnode = req->ps_eq_head; qnode; qnode = qnodenext) {
        qnodenext = qnode->sync_next;
        sync_node_free(&qnode);
    }
    slapi_ch_free((void **)&req);
    thread_count--;
}


/*
 * Free a sync update node (and everything it holds).
 */
static void
sync_node_free(SyncQueueNode **node)
{
    if (node != NULL && *node != NULL) {
        if ((*node)->sync_entry != NULL) {
            slapi_entry_free((*node)->sync_entry);
            (*node)->sync_entry = NULL;
        }
        slapi_ch_free((void **)node);
    }
}
