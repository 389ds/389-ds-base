/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *
 * psearch.c - persistent search
 * August 1997, ggood@netscape.com
 *
 * Open issues:
 *  - we increment and decrement active_threads in here.  Are there
 *    conditions under which this can prevent a server shutdown?
 */

#include <assert.h>
#include "slap.h"
#include "fe.h"

/*
 * A structure used to create a linked list
 * of entries being sent by a particular persistent
 * search result thread.
 * The ctrl is an "Entry Modify Notification" control
 * which we may send back with entries.
 */
typedef struct _ps_entry_queue_node
{
    Slapi_Entry *pe_entry;
    LDAPControl *pe_ctrls[2];
    struct _ps_entry_queue_node *pe_next;
} PSEQNode;

/*
 * Information about a single persistent search
 */
typedef struct _psearch
{
    Slapi_PBlock *ps_pblock;
    PRLock *ps_lock;
    uint64_t ps_complete;
    PSEQNode *ps_eq_head;
    PSEQNode *ps_eq_tail;
    time_t ps_lasttime;
    ber_int_t ps_changetypes;
    int ps_send_entchg_controls;
    struct _psearch *ps_next;
} PSearch;

/*
 * A list of outstanding persistent searches.
 */
typedef struct _psearch_list
{
    Slapi_RWLock *pl_rwlock;     /* R/W lock struct to serialize access */
    PSearch *pl_head;            /* Head of list */
    pthread_mutex_t pl_cvarlock; /* Lock for cvar */
    pthread_cond_t pl_cvar;      /* ps threads sleep on this */
} PSearch_List;

/*
 * Convenience macros for locking the list of persistent searches
 */
#define PSL_LOCK_READ() slapi_rwlock_rdlock(psearch_list->pl_rwlock)
#define PSL_UNLOCK_READ() slapi_rwlock_unlock(psearch_list->pl_rwlock)
#define PSL_LOCK_WRITE() slapi_rwlock_wrlock(psearch_list->pl_rwlock)
#define PSL_UNLOCK_WRITE() slapi_rwlock_unlock(psearch_list->pl_rwlock)


/*
 * Convenience macro for checking if the Persistent Search subsystem has
 * been initialized.
 */
#define PS_IS_INITIALIZED() (psearch_list != NULL)

/* Main list of outstanding persistent searches */
static PSearch_List *psearch_list = NULL;

/* Forward declarations */
static void ps_send_results(void *arg);
static PSearch *psearch_alloc(void);
static void ps_add_ps(PSearch *ps);
static void ps_remove(PSearch *dps);
static void pe_ch_free(PSEQNode **pe);
static int create_entrychange_control(ber_int_t chgtype, ber_int_t chgnum, const char *prevdn, LDAPControl **ctrlp);


/*
 * Initialize the list structure which contains the list
 * of outstanding persistent searches.  This must be
 * called early during server startup.
 */
void
ps_init_psearch_system()
{
    if (!PS_IS_INITIALIZED()) {
        int32_t rc = 0;

        psearch_list = (PSearch_List *)slapi_ch_calloc(1, sizeof(PSearch_List));
        if ((psearch_list->pl_rwlock = slapi_new_rwlock()) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "ps_init_psearch_system", "Cannot initialize lock structure.  "
                                                                   "The server is terminating.\n");
            exit(-1);
        }

        if ((rc = pthread_mutex_init(&(psearch_list->pl_cvarlock), NULL)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "ps_init_psearch_system",
                          "Cannot create new lock.  error %d (%s)\n",
                          rc, strerror(rc));
            exit(1);
        }
        if ((rc = pthread_cond_init(&(psearch_list->pl_cvar), NULL)) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "housekeeping_start",
                          "housekeeping cannot create new condition variable.  error %d (%s)\n",
                          rc, strerror(rc));
            exit(1);
        }
        psearch_list->pl_head = NULL;
    }
}


/*
 * Close all outstanding persistent searches.
 * To be used when the server is shutting down.
 */
void
ps_stop_psearch_system()
{
    PSearch *ps;

    if (PS_IS_INITIALIZED()) {
        PSL_LOCK_WRITE();
        for (ps = psearch_list->pl_head; NULL != ps; ps = ps->ps_next) {
            slapi_atomic_incr_64(&(ps->ps_complete), __ATOMIC_RELEASE);
        }
        PSL_UNLOCK_WRITE();
        ps_wakeup_all();
    }
}

/*
 * Add the given pblock to the list of outstanding persistent searches.
 * Then, start a thread to send the results to the client as they
 * are dispatched by add, modify, and modrdn operations.
 */
void
ps_add(Slapi_PBlock *pb, ber_int_t changetypes, int send_entchg_controls)
{
    PSearch *ps;
    PRThread *ps_tid;

    if (PS_IS_INITIALIZED() && NULL != pb) {
        /* Create the new node */
        ps = psearch_alloc();
        if (!ps) {
            return; /* Error is logged by psearch_alloc */
        }
        /*
         * The new thread use the operation so tell worker thread
         * not to reuse it.
         */
        g_pc_do_not_reuse_operation();

        ps->ps_pblock = slapi_pblock_clone(pb);
        ps->ps_changetypes = changetypes;
        ps->ps_send_entchg_controls = send_entchg_controls;

        /* Add it to the head of the list of persistent searches */
        ps_add_ps(ps);

        /* Start a thread to send the results */
        ps_tid = PR_CreateThread(PR_USER_THREAD, ps_send_results,
                                 (void *)ps, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                 PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);

        /* Checking if the thread is succesfully created and
         * if the thread is not created succesfully.... we send
         * error messages to the Log file
         */
        if (NULL == ps_tid) {
            int prerr;
            prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "ps_add", "PR_CreateThread()failed in the "
                                                   "ps_add function: " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));

            /* Now remove the ps from the list so call the function ps_remove */
            ps_remove(ps);
            PR_DestroyLock(ps->ps_lock);
            ps->ps_lock = NULL;
            slapi_ch_free((void **)&ps->ps_pblock);
            slapi_ch_free((void **)&ps);
        }
    }
}


/*
 * Remove the given PSearch from the list of outstanding persistent
 * searches and delete its resources.
 */
static void
ps_remove(PSearch *dps)
{
    PSearch *ps;

    if (PS_IS_INITIALIZED() && NULL != dps) {
        PSL_LOCK_WRITE();
        if (dps == psearch_list->pl_head) {
            /* Remove from head */
            psearch_list->pl_head = psearch_list->pl_head->ps_next;
        } else {
            /* Find and remove from list */
            ps = psearch_list->pl_head;
            while (NULL != ps->ps_next) {
                if (ps->ps_next == dps) {
                    ps->ps_next = ps->ps_next->ps_next;
                    break;
                } else {
                    ps = ps->ps_next;
                }
            }
        }
        PSL_UNLOCK_WRITE();
    }
}

/*
 * Free a persistent search node (and everything it holds).
 */
static void
pe_ch_free(PSEQNode **pe)
{
    if (pe != NULL && *pe != NULL) {
        if ((*pe)->pe_entry != NULL) {
            slapi_entry_free((*pe)->pe_entry);
            (*pe)->pe_entry = NULL;
        }

        if ((*pe)->pe_ctrls[0] != NULL) {
            ldap_control_free((*pe)->pe_ctrls[0]);
            (*pe)->pe_ctrls[0] = NULL;
        }

        slapi_ch_free((void **)pe);
    }
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
ps_send_results(void *arg)
{
    PSearch *ps = (PSearch *)arg;
    PSEQNode *peq, *peqnext;
    struct slapi_filter *filter = 0;
    char *base = NULL;
    Slapi_DN *sdn = NULL;
    char *fstr = NULL;
    char **pbattrs = NULL;
    int conn_acq_flag = 0;
    Slapi_Connection *conn = NULL;
    Connection *pb_conn = NULL;
    Operation *pb_op = NULL;

    g_incr_active_threadcnt();

    slapi_pblock_get(ps->ps_pblock, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(ps->ps_pblock, SLAPI_OPERATION, &pb_op);

    if (pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ps_send_results", "pb_conn is NULL\n");
        return;
    }

    /* need to acquire a reference to this connection so that it will not
       be released or cleaned up out from under us */
    pthread_mutex_lock(&(pb_conn->c_mutex));
    conn_acq_flag = connection_acquire_nolock(pb_conn);
    pthread_mutex_unlock(&(pb_conn->c_mutex));

    if (conn_acq_flag) {
        slapi_log_err(SLAPI_LOG_CONNS, "ps_send_results",
                      "conn=%" PRIu64 " op=%d Could not acquire the connection - psearch aborted\n",
                      pb_conn->c_connid, pb_op ? pb_op->o_opid : -1);
    }

    pthread_mutex_lock(&(psearch_list->pl_cvarlock));

    while ((conn_acq_flag == 0) && slapi_atomic_load_64(&(ps->ps_complete), __ATOMIC_ACQUIRE) == 0) {
        /* Check for an abandoned operation */
        if (pb_op == NULL || slapi_op_abandoned(ps->ps_pblock)) {
            slapi_log_err(SLAPI_LOG_CONNS, "ps_send_results",
                          "conn=%" PRIu64 " op=%d The operation has been abandoned\n",
                          pb_conn->c_connid, pb_op ? pb_op->o_opid : -1);
            break;
        }
        if (NULL == ps->ps_eq_head) {
            /* Nothing to do */
            pthread_cond_wait(&(psearch_list->pl_cvar), &(psearch_list->pl_cvarlock));
        } else {
            /* dequeue the item */
            int attrsonly;
            char **attrs;
            LDAPControl **ectrls;
            Slapi_Entry *ec;
            Slapi_Filter *f = NULL;

            PR_Lock(ps->ps_lock);

            peq = ps->ps_eq_head;
            ps->ps_eq_head = peq->pe_next;
            if (NULL == ps->ps_eq_head) {
                ps->ps_eq_tail = NULL;
            }

            PR_Unlock(ps->ps_lock);

            /* Get all the information we need to send the result */
            ec = peq->pe_entry;
            slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_ATTRS, &attrs);
            slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_ATTRSONLY, &attrsonly);
            if (!ps->ps_send_entchg_controls || peq->pe_ctrls[0] == NULL) {
                ectrls = NULL;
            } else {
                ectrls = peq->pe_ctrls;
            }

            /*
             * Send the result.  Since send_ldap_search_entry can block for
             * up to 30 minutes, we relinquish all locks before calling it.
             */
            pthread_mutex_unlock(&(psearch_list->pl_cvarlock));

            /*
             * The entry is in the right scope and matches the filter
             * but we need to redo the filter test here to check access
             * controls. See the comments at the slapi_filter_test()
             * call in ps_service_persistent_searches().
            */
            slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_FILTER, &f);

            /* See if the entry meets the filter and ACL criteria */
            if (slapi_vattr_filter_test(ps->ps_pblock, ec, f,
                                        1 /* verify_access */) == 0) {
                int rc = 0;
                slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_RESULT_ENTRY, ec);
                rc = send_ldap_search_entry(ps->ps_pblock, ec,
                                            ectrls, attrs, attrsonly);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_CONNS, "ps_send_results",
                                  "conn=%" PRIu64 " op=%d Error %d sending entry %s with op status %d\n",
                                  pb_conn->c_connid, pb_op ? pb_op->o_opid: -1,
                                  rc, slapi_entry_get_dn_const(ec), pb_op ? pb_op->o_status : -1);
                }
            }

            pthread_mutex_lock(&(psearch_list->pl_cvarlock));

            /* Deallocate our wrapper for this entry */
            pe_ch_free(&peq);
        }
    }
    pthread_mutex_unlock(&(psearch_list->pl_cvarlock));
    ps_remove(ps);

    /* indicate the end of search */
    plugin_call_plugins(ps->ps_pblock, SLAPI_PLUGIN_POST_SEARCH_FN);

    /* free things from the pblock that were not free'd in do_search() */
    /* we strdup'd this in search.c - need to free */
    slapi_pblock_get(ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, &base);
    slapi_pblock_set(ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, NULL);
    slapi_ch_free_string(&base);

    /* Free SLAPI_SEARCH_* before deleting op since those are held by op */
    slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, &sdn);
    slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, NULL);
    slapi_sdn_free(&sdn);

    slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_STRFILTER, &fstr);
    slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_STRFILTER, NULL);
    slapi_ch_free_string(&fstr);

    slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_ATTRS, &pbattrs);
    slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_ATTRS, NULL);
    if (pbattrs != NULL) {
        charray_free(pbattrs);
    }

    slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_FILTER, &filter);
    slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_FILTER, NULL);
    slapi_filter_free(filter, 1);

    conn = pb_conn; /* save to release later - connection_remove_operation_ext will NULL the pb_conn */
    /* Clean up the connection structure */
    pthread_mutex_lock(&(conn->c_mutex));

    slapi_log_err(SLAPI_LOG_CONNS, "ps_send_results",
                  "conn=%" PRIu64 " op=%d Releasing the connection and operation\n",
                  conn->c_connid, pb_op ? pb_op->o_opid : -1);
    /* Delete this op from the connection's list */
    connection_remove_operation_ext(ps->ps_pblock, conn, pb_op);
    /*
     * Then free the operation:  connection_remove_operation_ext
     *   calls operation_done and unlink op from pblock
     *   so operation should be explictly freed
     */
    operation_free(&pb_op, NULL);


    /* Decrement the connection refcnt */
    if (conn_acq_flag == 0) { /* we acquired it, so release it */
        connection_release_nolock(conn);
    }
    pthread_mutex_unlock(&(conn->c_mutex));
    conn = NULL;

    PR_DestroyLock(ps->ps_lock);
    ps->ps_lock = NULL;

    slapi_ch_free((void **)&ps->ps_pblock);
    for (peq = ps->ps_eq_head; peq; peq = peqnext) {
        peqnext = peq->pe_next;
        pe_ch_free(&peq);
    }
    slapi_ch_free((void **)&ps);
    g_decr_active_threadcnt();
}


/*
 * Allocate and initialize an empty PSearch node.
 */
static PSearch *
psearch_alloc(void)
{
    PSearch *ps;

    ps = (PSearch *)slapi_ch_calloc(1, sizeof(PSearch));

    ps->ps_pblock = NULL;
    if ((ps->ps_lock = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "psearch_alloc", "Cannot create new lock.  "
                                                      "Persistent search abandoned.\n");
        slapi_ch_free((void **)&ps);
        return (NULL);
    }
    slapi_atomic_store_64(&(ps->ps_complete), 0, __ATOMIC_RELEASE);
    ps->ps_eq_head = ps->ps_eq_tail = (PSEQNode *)NULL;
    ps->ps_lasttime = (time_t)0L;
    ps->ps_next = NULL;
    return ps;
}


/*
 * Add the given persistent search to the
 * head of the list of persistent searches.
 */
static void
ps_add_ps(PSearch *ps)
{
    if (PS_IS_INITIALIZED() && NULL != ps) {
        PSL_LOCK_WRITE();
        ps->ps_next = psearch_list->pl_head;
        psearch_list->pl_head = ps;
        PSL_UNLOCK_WRITE();
    }
}


/*
 * Wake up all threads sleeping on
 * the psearch_list condition variable.
 */
void
ps_wakeup_all()
{
    if (PS_IS_INITIALIZED()) {
        pthread_mutex_lock(&(psearch_list->pl_cvarlock));
        pthread_cond_broadcast(&(psearch_list->pl_cvar));
        pthread_mutex_unlock(&(psearch_list->pl_cvarlock));
    }
}


/*
 * Check if there are any persistent searches.  If so,
 * the check to see if the chgtype is one of those the
 * client is interested in.  If so, then check to see if
 * the entry matches any of the filters the searches.
 * If so, then enqueue the entry on that persistent search's
 * ps_entryqueue and signal it to wake up and send the entry.
 *
 * Note that if eprev is NULL we assume that the entry's DN
 * was not changed by the op. that called this function.  If
 * chgnum is 0 it is unknown so we won't ever send it to a
 * client in the EntryChangeNotification control.
 */
void
ps_service_persistent_searches(Slapi_Entry *e, Slapi_Entry *eprev, ber_int_t chgtype, ber_int_t chgnum)
{
    LDAPControl *ctrl = NULL;
    PSearch *ps = NULL;
    PSEQNode *pe = NULL;
    int matched = 0;
    const char *edn;

    if (!PS_IS_INITIALIZED()) {
        return;
    }

    if (NULL == e) {
        /* For now, some backends such as the chaining backend do not provide a post-op entry */
        return;
    }

    assert(psearch_list);
    assert(psearch_list->pl_rwlock);
    PSL_LOCK_READ();
    edn = slapi_entry_get_dn_const(e);

    for (ps = psearch_list ? psearch_list->pl_head : NULL; NULL != ps; ps = ps->ps_next) {
        char *origbase = NULL;
        Slapi_DN *base = NULL;
        Slapi_Filter *f;
        int scope;
        Connection *pb_conn = NULL;
        Operation *pb_op = NULL;

        slapi_pblock_get(ps->ps_pblock, SLAPI_OPERATION, &pb_op);
        slapi_pblock_get(ps->ps_pblock, SLAPI_CONNECTION, &pb_conn);

        /* Skip the node that doesn't meet the changetype,
         * or is unable to use the change in ps_send_results()
         */
        if ((ps->ps_changetypes & chgtype) == 0 || pb_op == NULL ||
            slapi_op_abandoned(ps->ps_pblock)) {
            continue;
        }

        slapi_log_err(SLAPI_LOG_CONNS, "ps_service_persistent_searches",
                      "conn=%" PRIu64 " op=%d entry %s with chgtype %d "
                      "matches the ps changetype %d\n",
                      pb_conn ? pb_conn->c_connid : -1,
                      pb_op->o_opid,
                      edn, chgtype, ps->ps_changetypes);

        slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_FILTER, &f);
        slapi_pblock_get(ps->ps_pblock, SLAPI_ORIGINAL_TARGET_DN, &origbase);
        slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, &base);
        slapi_pblock_get(ps->ps_pblock, SLAPI_SEARCH_SCOPE, &scope);
        if (NULL == base) {
            base = slapi_sdn_new_dn_byref(origbase);
            slapi_pblock_set(ps->ps_pblock, SLAPI_SEARCH_TARGET_SDN, base);
        }

        /*
         * See if the entry meets the scope and filter criteria.
         * We cannot do the acl check here as this thread
         * would then potentially clash with the ps_send_results()
         * thread on the aclpb in ps->ps_pblock.
         * By avoiding the acl check in this thread, and leaving all the acl
         * checking to the ps_send_results() thread we avoid
         * the ps_pblock contention problem.
         * The lesson here is "Do not give multiple threads arbitary access
         * to the same pblock" this kind of muti-threaded access
         * to the same pblock must be done carefully--there is currently no
         * generic satisfactory way to do this.
        */
        if (slapi_sdn_scope_test(slapi_entry_get_sdn_const(e), base, scope) &&
            slapi_vattr_filter_test(ps->ps_pblock, e, f, 0 /* verify_access */) == 0) {
            PSEQNode *pOldtail;

            /* The scope and the filter match - enqueue it */

            matched++;
            pe = (PSEQNode *)slapi_ch_calloc(1, sizeof(PSEQNode));
            pe->pe_entry = slapi_entry_dup(e);
            if (ps->ps_send_entchg_controls) {
                /* create_entrychange_control() is more
                 * expensive than slapi_dup_control()
                 */
                if (ctrl == NULL) {
                    int rc;
                    rc = create_entrychange_control(chgtype, chgnum,
                                                    eprev ? slapi_entry_get_dn_const(eprev) : NULL,
                                                    &ctrl);
                    if (rc != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "ps_service_persistent_searches",
                                      "Unable to create EntryChangeNotification control for"
                                      " entry \"%s\" -- control won't be sent.\n",
                                      slapi_entry_get_dn_const(e));
                    }
                }
                if (ctrl) {
                    pe->pe_ctrls[0] = slapi_dup_control(ctrl);
                }
            }

            /* Put it on the end of the list for this pers search */
            PR_Lock(ps->ps_lock);
            pOldtail = ps->ps_eq_tail;
            ps->ps_eq_tail = pe;
            if (NULL == ps->ps_eq_head) {
                ps->ps_eq_head = ps->ps_eq_tail;
            } else {
                pOldtail->pe_next = ps->ps_eq_tail;
            }
            PR_Unlock(ps->ps_lock);
        }
    }

    PSL_UNLOCK_READ();

    /* Were there any matches? */
    if (matched) {
        ldap_control_free(ctrl);
        /* Turn 'em loose */
        ps_wakeup_all();
        slapi_log_err(SLAPI_LOG_TRACE, "ps_service_persistent_searches", "Enqueued entry "
                      "\"%s\" on %d persistent search lists\n",
                      slapi_entry_get_dn_const(e), matched);
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "ps_service_persistent_searches",
                      "Entry \"%s\" not enqueued on any persistent search lists\n",
                      slapi_entry_get_dn_const(e));
    }
}

/*
 * Parse the value from an LDAPv3 "Persistent Search" control.  They look
 * like this:
 *
 *    PersistentSearch ::= SEQUENCE {
 *    changeTypes INTEGER,
 *    -- the changeTypes field is the logical OR of
 *    -- one or more of these values: add (1), delete (2),
 *    -- modify (4), modDN (8).  It specifies which types of
 *    -- changes will cause an entry to be returned.
 *    changesOnly BOOLEAN, -- skip initial search?
 *    returnECs BOOLEAN,   -- return "Entry Change" controls?
 *   }
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 *
 * This function is standalone; it does not require initialization of
 * the PS subsystem.
 */
int
ps_parse_control_value(struct berval *psbvp, ber_int_t *changetypesp, int *changesonlyp, int *returnecsp)
{
    int rc = LDAP_SUCCESS;

    if (psbvp->bv_len == 0 || psbvp->bv_val == NULL) {
        rc = LDAP_PROTOCOL_ERROR;
    } else {
        BerElement *ber = ber_init(psbvp);
        if (ber == NULL) {
            rc = LDAP_OPERATIONS_ERROR;
        } else {
            if (ber_scanf(ber, "{ibb}", changetypesp, changesonlyp, returnecsp) == LBER_ERROR) {
                rc = LDAP_PROTOCOL_ERROR;
            }
            /* the ber encoding is no longer needed */
            ber_free(ber, 1);
        }
    }

    return (rc);
}


/*
 * Create an LDAPv3 "Entry Change Notification" control.  They look like this:
 *
 *    EntryChangeNotification ::= SEQUENCE {
 *        changeType        ENUMERATED {
 *        add    (1),    -- LDAP_CHANGETYPE_ADD
 *        delete    (2),    -- LDAP_CHANGETYPE_DELETE
 *        modify    (4),    -- LDAP_CHANGETYPE_MODIFY
 *        moddn    (8),    -- LDAP_CHANGETYPE_MODDN
 *        },
 *        previousDN     LDAPDN OPTIONAL,   -- included for MODDN ops. only
 *        changeNumber INTEGER OPTIONAL,  -- included if supported by DSA
 *    }
 *
 * This function returns an LDAP error code (LDAP_SUCCESS if all goes well).
 * The value returned in *ctrlp should be free'd use ldap_control_free().
 * If chgnum is 0 we omit it from the control.
 */
static int
create_entrychange_control(ber_int_t chgtype, ber_int_t chgnum, const char *dn, LDAPControl **ctrlp)
{
    int rc;
    BerElement *ber;
    struct berval *bvp;
    const char *prevdn = dn;

    if (prevdn == NULL) {
        prevdn = "";
    }

    if (ctrlp == NULL || (ber = der_alloc()) == NULL) {
        return (LDAP_OPERATIONS_ERROR);
    }

    *ctrlp = NULL;

    if ((rc = ber_printf(ber, "{e", chgtype)) != -1) {
        if (chgtype == LDAP_CHANGETYPE_MODDN) {
            rc = ber_printf(ber, "s", prevdn);
        }
        if (rc != -1 && chgnum != 0) {
            rc = ber_printf(ber, "i", chgnum);
        }
        if (rc != -1) {
            rc = ber_printf(ber, "}");
        }
    }

    if (rc != -1) {
        rc = ber_flatten(ber, &bvp);
    }
    ber_free(ber, 1);

    if (rc == -1) {
        return (LDAP_OPERATIONS_ERROR);
    }

    *ctrlp = (LDAPControl *)slapi_ch_malloc(sizeof(LDAPControl));
    (*ctrlp)->ldctl_iscritical = 0;
    (*ctrlp)->ldctl_oid = slapi_ch_strdup(LDAP_CONTROL_ENTRYCHANGE);
    (*ctrlp)->ldctl_value = *bvp; /* struct copy */

    bvp->bv_val = NULL;
    ber_bvfree(bvp);

    return (LDAP_SUCCESS);
}
