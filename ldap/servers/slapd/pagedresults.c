/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"

/* helper function to clean up one prp slot */
static void
_pr_cleanup_one_slot(PagedResults *prp)
{
    PRLock *prmutex = NULL;
    if (!prp) {
        return;
    }
    if (prp->pr_current_be && prp->pr_current_be->be_search_results_release) {
        /* sr is left; release it. */
        prp->pr_current_be->be_search_results_release(&(prp->pr_search_result_set));
    }
    /* clean up the slot */
    if (prp->pr_mutex) {
        /* pr_mutex is reused; back it up and reset it. */
        prmutex = prp->pr_mutex;
    }
    memset(prp, '\0', sizeof(PagedResults));
    prp->pr_mutex = prmutex;
}

/*
 * Parse the value from an LDAPv3 "Simple Paged Results" control.  They look
 * like this:
 *
 *   realSearchControlValue ::= SEQUENCE {
 *   size INTEGER (0..maxInt),
 *   -- requested page size from client
 *   -- result set size estimate from server
 *   cookie OCTET STRING
 *   -- index for the pagedresults array in the connection
 *   }
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int
pagedresults_parse_control_value(Slapi_PBlock *pb,
                                 struct berval *psbvp,
                                 ber_int_t *pagesize,
                                 int *index,
                                 Slapi_Backend *be)
{
    int rc = LDAP_SUCCESS;
    struct berval cookie = {0};
    Connection *conn = NULL;
    Operation *op = NULL;
    BerElement *ber = NULL;
    PagedResults *prp = NULL;
    int i;
    int maxreqs = config_get_maxsimplepaged_per_conn();

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_parse_control_value", "=>\n");
    if (NULL == conn || NULL == op || NULL == pagesize || NULL == index) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "pagedresults_parse_control_value", "<= Error %d\n",
                      LDAP_OPERATIONS_ERROR);
        return LDAP_OPERATIONS_ERROR;
    }
    *index = -1;

    if (psbvp->bv_len == 0 || psbvp->bv_val == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                      "<= no control value\n");
        return LDAP_PROTOCOL_ERROR;
    }
    ber = ber_init(psbvp);
    if (ber == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                      "<= no control value\n");
        return LDAP_PROTOCOL_ERROR;
    }
    if (ber_scanf(ber, "{io}", pagesize, &cookie) == LBER_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                      "<= corrupted control value\n");
        return LDAP_PROTOCOL_ERROR;
    }
    if (!maxreqs) {
        slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                      "Simple paged results requests per conn exceeded the limit: %d\n",
                      maxreqs);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    pthread_mutex_lock(&(conn->c_mutex));
    /* the ber encoding is no longer needed */
    ber_free(ber, 1);
    if (cookie.bv_len <= 0) {
        /* first time? */
        int maxlen = conn->c_pagedresults.prl_maxlen;
        if (conn->c_pagedresults.prl_count == maxlen) {
            if (0 == maxlen) { /* first time */
                conn->c_pagedresults.prl_maxlen = 1;
                conn->c_pagedresults.prl_list = (PagedResults *)slapi_ch_calloc(1, sizeof(PagedResults));
            } else {
                /* new max length */
                conn->c_pagedresults.prl_maxlen *= 2;
                conn->c_pagedresults.prl_list = (PagedResults *)slapi_ch_realloc(
                    (char *)conn->c_pagedresults.prl_list,
                    sizeof(PagedResults) * conn->c_pagedresults.prl_maxlen);
                /* initialze newly allocated area */
                memset(conn->c_pagedresults.prl_list + maxlen, '\0', sizeof(PagedResults) * maxlen);
            }
            *index = maxlen; /* the first position in the new area */
            prp = conn->c_pagedresults.prl_list + *index;
            prp->pr_current_be = be;
        } else {
            prp = conn->c_pagedresults.prl_list;
            for (i = 0; i < conn->c_pagedresults.prl_maxlen; i++, prp++) {
                if (!prp->pr_current_be) { /* unused slot; take it */
                    _pr_cleanup_one_slot(prp);
                    prp->pr_current_be = be;
                    *index = i;
                    break;
                } else if (slapi_timespec_expire_check(&(prp->pr_timelimit_hr)) == TIMER_EXPIRED || /* timelimit exceeded */
                           (prp->pr_flags & CONN_FLAG_PAGEDRESULTS_ABANDONED) /* abandoned */) {
                    _pr_cleanup_one_slot(prp);
                    conn->c_pagedresults.prl_count--;
                    prp->pr_current_be = be;
                    *index = i;
                    break;
                }
            }
        }
        if ((maxreqs > 0) && (*index >= maxreqs)) {
            rc = LDAP_UNWILLING_TO_PERFORM;
            slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_parse_control_value",
                          "Simple paged results requests per conn exeeded the limit: %d\n",
                          maxreqs);
            goto bail;
        }

        if ((*index > -1) && (*index < conn->c_pagedresults.prl_maxlen) &&
            !conn->c_pagedresults.prl_list[*index].pr_mutex) {
            conn->c_pagedresults.prl_list[*index].pr_mutex = PR_NewLock();
        }
        conn->c_pagedresults.prl_count++;
    } else {
        /* Repeated paged results request.
         * PagedResults is already allocated. */
        char *ptr = slapi_ch_malloc(cookie.bv_len + 1);
        memcpy(ptr, cookie.bv_val, cookie.bv_len);
        *(ptr + cookie.bv_len) = '\0';
        *index = strtol(ptr, NULL, 10);
        slapi_ch_free_string(&ptr);
        if ((conn->c_pagedresults.prl_maxlen <= *index) || (*index < 0)) {
            rc = LDAP_PROTOCOL_ERROR;
            slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                          "Invalid cookie: %d\n", *index);
            *index = -1; /* index is invalid. reinitializing it. */
            goto bail;
        }
        prp = conn->c_pagedresults.prl_list + *index;
        if (!(prp->pr_search_result_set)) { /* freed and reused for the next backend. */
            conn->c_pagedresults.prl_count++;
        }
    }
    /* reset sizelimit */
    op->o_pagedresults_sizelimit = -1;

    if ((*index > -1) && (*index < conn->c_pagedresults.prl_maxlen)) {
        if (conn->c_pagedresults.prl_list[*index].pr_flags & CONN_FLAG_PAGEDRESULTS_ABANDONED) {
            /* repeated case? */
            prp = conn->c_pagedresults.prl_list + *index;
            _pr_cleanup_one_slot(prp);
            rc = LDAP_CANCELLED;
        } else {
            /* Need to keep the latest msgid to prepare for the abandon. */
            conn->c_pagedresults.prl_list[*index].pr_msgid = op->o_msgid;
        }
    } else {
        rc = LDAP_PROTOCOL_ERROR;
        slapi_log_err(SLAPI_LOG_ERR, "pagedresults_parse_control_value",
                      "Invalid cookie: %d\n", *index);
    }
bail:
    slapi_ch_free((void **)&cookie.bv_val);
    /* cleaning up the rest of the timedout or abandoned if any */
    prp = conn->c_pagedresults.prl_list;
    for (i = 0; i < conn->c_pagedresults.prl_maxlen; i++, prp++) {
        if (prp->pr_current_be &&
            (slapi_timespec_expire_check(&(prp->pr_timelimit_hr)) == TIMER_EXPIRED || /* timelimit exceeded */
             (prp->pr_flags & CONN_FLAG_PAGEDRESULTS_ABANDONED)) /* abandoned */) {
            _pr_cleanup_one_slot(prp);
            conn->c_pagedresults.prl_count--;
            if (i == *index) {
                /* registered slot is expired and cleaned up. return cancelled. */
                *index = -1;
                rc = LDAP_CANCELLED;
            }
        }
    }
    pthread_mutex_unlock(&(conn->c_mutex));

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_parse_control_value",
                  "<= idx %d\n", *index);
    return rc;
}

/*
 * controlType = LDAP_CONTROL_PAGEDRESULTS;
 * criticality = n/a;
 * controlValue:
 *   realSearchControlValue ::= SEQUENCE {
 *   size INTEGER (0..maxInt),
 *   -- requested page size from client
 *   -- result set size estimate from server
 *   cookie OCTET STRING
 *   }
 */
void
pagedresults_set_response_control(Slapi_PBlock *pb, int iscritical, ber_int_t estimate, int current_search_count, int index)
{
    LDAPControl **resultctrls = NULL;
    LDAPControl pr_respctrl;
    BerElement *ber = NULL;
    struct berval *berval = NULL;
    char *cookie_str = NULL;
    int found = 0;
    int i;
    int cookie = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_response_control",
                  "=> idx=%d\n", index);

    if ((ber = der_alloc()) == NULL) {
        goto bailout;
    }

    /* begin sequence, payload, end sequence */
    if (current_search_count < 0) {
        cookie = -1;
        cookie_str = slapi_ch_strdup("");
    } else {
        cookie = index;
        cookie_str = slapi_ch_smprintf("%d", index);
    }
    slapi_pblock_set(pb, SLAPI_PAGED_RESULTS_COOKIE, &cookie);
    ber_printf(ber, "{io}", estimate, cookie_str, strlen(cookie_str));
    if (ber_flatten(ber, &berval) != LDAP_SUCCESS) {
        goto bailout;
    }
    pr_respctrl.ldctl_oid = LDAP_CONTROL_PAGEDRESULTS;
    pr_respctrl.ldctl_iscritical = iscritical;
    pr_respctrl.ldctl_value.bv_val = berval->bv_val;
    pr_respctrl.ldctl_value.bv_len = berval->bv_len;

    slapi_pblock_get(pb, SLAPI_RESCONTROLS, &resultctrls);
    for (i = 0; resultctrls && resultctrls[i]; i++) {
        if (strcmp(resultctrls[i]->ldctl_oid, LDAP_CONTROL_PAGEDRESULTS) == 0) {
            /*
             * We get here if search returns more than one entry
             * and this is not the first entry.
             */
            ldap_control_free(resultctrls[i]);
            resultctrls[i] = slapi_dup_control(&pr_respctrl);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* slapi_pblock_set() will dup the control */
        slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, &pr_respctrl);
    }

bailout:
    slapi_ch_free_string(&cookie_str);
    ber_free(ber, 1);   /* ber_free() checks for NULL param */
    ber_bvfree(berval); /* ber_bvfree() checks for NULL param */

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_response_control",
                  "<= idx=%d\n", index);
}

int
pagedresults_free_one(Connection *conn, Operation *op, int index)
{
    int rc = -1;

    if (!op_is_pagedresults(op)) {
        return 0; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_free_one",
                  "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (conn->c_pagedresults.prl_count <= 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_free_one",
                          "conn=%" PRIu64 " paged requests list count is %d\n",
                          conn->c_connid, conn->c_pagedresults.prl_count);
        } else if (index < conn->c_pagedresults.prl_maxlen) {
            PagedResults *prp = conn->c_pagedresults.prl_list + index;
            _pr_cleanup_one_slot(prp);
            conn->c_pagedresults.prl_count--;
            rc = 0;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_free_one", "<= %d\n", rc);
    return rc;
}

/*
 * Used for abandoning - conn->c_mutex is already locked in do_abandone.
 */
int
pagedresults_free_one_msgid_nolock(Connection *conn, ber_int_t msgid)
{
    int rc = -1;
    int i;

    if (conn && (msgid > -1)) {
        if (conn->c_pagedresults.prl_maxlen <= 0) {
            ; /* Not a paged result. */
        } else {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "pagedresults_free_one_msgid_nolock", "=> msgid=%d\n", msgid);
            for (i = 0; i < conn->c_pagedresults.prl_maxlen; i++) {
                if (conn->c_pagedresults.prl_list[i].pr_msgid == msgid) {
                    PagedResults *prp = conn->c_pagedresults.prl_list + i;
                    if (prp->pr_current_be &&
                        prp->pr_current_be->be_search_results_release &&
                        prp->pr_search_result_set) {
                        prp->pr_current_be->be_search_results_release(&(prp->pr_search_result_set));
                    }
                    prp->pr_flags |= CONN_FLAG_PAGEDRESULTS_ABANDONED;
                    prp->pr_flags &= ~CONN_FLAG_PAGEDRESULTS_PROCESSING;
                    rc = 0;
                    break;
                }
            }
            slapi_log_err(SLAPI_LOG_TRACE,
                          "pagedresults_free_one_msgid_nolock", "<= %d\n", rc);
        }
    }

    return rc;
}

/* setters and getters for the connection */
Slapi_Backend *
pagedresults_get_current_be(Connection *conn, int index)
{
    Slapi_Backend *be = NULL;
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_current_be", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            be = conn->c_pagedresults.prl_list[index].pr_current_be;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_current_be", "<= %p\n", be);
    return be;
}

int
pagedresults_set_current_be(Connection *conn, Slapi_Backend *be, int index, int nolock)
{
    int rc = -1;
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_current_be", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        if (!nolock)
            pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_current_be = be;
        }
        rc = 0;
        if (!nolock)
            pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_current_be", "<= %d\n", rc);
    return rc;
}

void *
pagedresults_get_search_result(Connection *conn, Operation *op, int locked, int index)
{
    void *sr = NULL;
    if (!op_is_pagedresults(op)) {
        return sr; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result", "=> (%s) idx=%d\n",
                  locked ? "locked" : "not locked", index);
    if (conn && (index > -1)) {
        if (!locked) {
            pthread_mutex_lock(&(conn->c_mutex));
        }
        if (index < conn->c_pagedresults.prl_maxlen) {
            sr = conn->c_pagedresults.prl_list[index].pr_search_result_set;
        }
        if (!locked) {
            pthread_mutex_unlock(&(conn->c_mutex));
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result", "<=%p\n", sr);
    return sr;
}

int
pagedresults_set_search_result(Connection *conn, Operation *op, void *sr, int locked, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return 0; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result", "=> idx=%d, sr=%p\n",
                  index, sr);
    if (conn && (index > -1)) {
        if (!locked)
            pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            PagedResults *prp = conn->c_pagedresults.prl_list + index;
            if (!(prp->pr_flags & CONN_FLAG_PAGEDRESULTS_ABANDONED) || !sr) {
                /* If abandoned, don't set the search result unless it is NULL */
                prp->pr_search_result_set = sr;
            }
            rc = 0;
        }
        if (!locked)
            pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result", "=> %d\n", rc);
    return rc;
}

int
pagedresults_get_search_result_count(Connection *conn, Operation *op, int index)
{
    int count = 0;
    if (!op_is_pagedresults(op)) {
        return count; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result_count", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            count = conn->c_pagedresults.prl_list[index].pr_search_result_count;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result_count", "<= %d\n", count);
    return count;
}

int
pagedresults_set_search_result_count(Connection *conn, Operation *op, int count, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result_count", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_search_result_count = count;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result_count", "<= %d\n", rc);
    return rc;
}

int
pagedresults_get_search_result_set_size_estimate(Connection *conn,
                                                 Operation *op,
                                                 int index)
{
    int count = 0;
    if (!op_is_pagedresults(op)) {
        return count; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result_set_size_estimate",
                  "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            count = conn->c_pagedresults.prl_list[index].pr_search_result_set_size_estimate;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_search_result_set_size_estimate", "<= %d\n",
                  count);
    return count;
}

int
pagedresults_set_search_result_set_size_estimate(Connection *conn,
                                                 Operation *op,
                                                 int count,
                                                 int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result_set_size_estimate",
                  "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_search_result_set_size_estimate = count;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "<-- pagedresults_set_search_result_set_size_estimate", "<= %d\n",
                  rc);
    return rc;
}

int
pagedresults_get_with_sort(Connection *conn, Operation *op, int index)
{
    int flags = 0;
    if (!op_is_pagedresults(op)) {
        return flags; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_with_sort", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            flags = conn->c_pagedresults.prl_list[index].pr_flags & CONN_FLAG_PAGEDRESULTS_WITH_SORT;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_with_sort", "<= %d\n", flags);
    return flags;
}

int
pagedresults_set_with_sort(Connection *conn, Operation *op, int flags, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_with_sort", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            if (flags & OP_FLAG_SERVER_SIDE_SORTING) {
                conn->c_pagedresults.prl_list[index].pr_flags |=
                    CONN_FLAG_PAGEDRESULTS_WITH_SORT;
            }
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_with_sort", "<= %d\n", rc);
    return rc;
}

int
pagedresults_get_unindexed(Connection *conn, Operation *op, int index)
{
    int flags = 0;
    if (!op_is_pagedresults(op)) {
        return flags; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_unindexed", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            flags = conn->c_pagedresults.prl_list[index].pr_flags & CONN_FLAG_PAGEDRESULTS_UNINDEXED;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_unindexed", "<= %d\n", flags);
    return flags;
}

int
pagedresults_set_unindexed(Connection *conn, Operation *op, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_unindexed", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_flags |=
                CONN_FLAG_PAGEDRESULTS_UNINDEXED;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_unindexed", "<= %d\n", rc);
    return rc;
}

int
pagedresults_get_sort_result_code(Connection *conn, Operation *op, int index)
{
    int code = LDAP_OPERATIONS_ERROR;
    if (!op_is_pagedresults(op)) {
        return code; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_sort_result_code", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            code = conn->c_pagedresults.prl_list[index].pr_sort_result_code;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_get_sort_result_code", "<= %d\n", code);
    return code;
}

int
pagedresults_set_sort_result_code(Connection *conn, Operation *op, int code, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_sort_result_code", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_sort_result_code = code;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_sort_result_code", "<= %d\n", rc);
    return rc;
}

int
pagedresults_set_timelimit(Connection *conn, Operation *op, time_t timelimit, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_timelimit", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            slapi_timespec_expire_at(timelimit, &(conn->c_pagedresults.prl_list[index].pr_timelimit_hr));
        }
        pthread_mutex_unlock(&(conn->c_mutex));
        rc = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_timelimit", "<= %d\n", rc);
    return rc;
}

int
pagedresults_set_sizelimit(Connection *conn __attribute__((unused)), Operation *op, int sizelimit, int index)
{
    int rc = -1;
    if (!op_is_pagedresults(op)) {
        return rc; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_sizelimit", "=> idx=%d\n", index);
    op->o_pagedresults_sizelimit = sizelimit;
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_set_sizelimit", "<= %d\n", rc);
    return rc;
}

int
pagedresults_get_sizelimit(Connection *conn __attribute__((unused)), Operation *op, int index)
{
    int sizelimit = -1;
    if (!op_is_pagedresults(op)) {
        return sizelimit; /* noop */
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_get_sizelimit", "=> idx=%d\n", index);
    sizelimit = op->o_pagedresults_sizelimit;
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_get_sizelimit", "<=\n");
    return sizelimit;
}

/*
 * pagedresults_cleanup cleans up the pagedresults list;
 * it does not free the list.
 * return values
 * 0: not a simple paged result connection
 * 1: simple paged result and successfully abandoned
 */
int
pagedresults_cleanup(Connection *conn, int needlock)
{
    int rc = 0;
    int i;
    PagedResults *prp = NULL;

    /* slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_cleanup", "=>\n"); */

    if (NULL == conn) {
        /* slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_cleanup", "<= Connection is NULL\n"); */
        return 0;
    }

    if (needlock) {
        pthread_mutex_lock(&(conn->c_mutex));
    }
    for (i = 0; conn->c_pagedresults.prl_list &&
                i < conn->c_pagedresults.prl_maxlen;
         i++) {
        prp = conn->c_pagedresults.prl_list + i;
        if (prp->pr_current_be && prp->pr_search_result_set &&
            prp->pr_current_be->be_search_results_release) {
            prp->pr_current_be->be_search_results_release(&(prp->pr_search_result_set));
            rc = 1;
        }
        prp->pr_current_be = NULL;
        if (prp->pr_mutex) {
            PR_DestroyLock(prp->pr_mutex);
        }
        memset(prp, '\0', sizeof(PagedResults));
    }
    conn->c_pagedresults.prl_count = 0;
    if (needlock) {
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    /* slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_cleanup", "<= %d\n", rc); */
    return rc;
}

/*
 * pagedresults_cleanup_all frees the list.
 * return values
 * 0: not a simple paged result connection
 * 1: simple paged result and successfully abandoned
 */
int
pagedresults_cleanup_all(Connection *conn, int needlock)
{
    int rc = 0;
    int i;
    PagedResults *prp = NULL;

    if (NULL == conn) {
        return 0;
    }

    if (needlock) {
        pthread_mutex_lock(&(conn->c_mutex));
    }
    for (i = 0; conn->c_pagedresults.prl_list &&
                i < conn->c_pagedresults.prl_maxlen;
         i++) {
        prp = conn->c_pagedresults.prl_list + i;
        if (prp->pr_mutex) {
            PR_DestroyLock(prp->pr_mutex);
        }
        if (prp->pr_current_be && prp->pr_search_result_set &&
            prp->pr_current_be->be_search_results_release) {
            prp->pr_current_be->be_search_results_release(&(prp->pr_search_result_set));
            rc = 1;
        }
        prp->pr_current_be = NULL;
    }
    slapi_ch_free((void **)&conn->c_pagedresults.prl_list);
    conn->c_pagedresults.prl_maxlen = 0;
    conn->c_pagedresults.prl_count = 0;
    if (needlock) {
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    return rc;
}

#if 0 /* Stopped using it (#47347) */
/*
 * check to see if this connection is currently processing
 * a pagedresults search - if it is, return True - if not,
 * mark that it is processing, and return False
 */
int
pagedresults_check_or_set_processing(Connection *conn, int index)
{
    int ret = 0;
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_check_or_set_processing", "=>\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            ret = (conn->c_pagedresults.prl_list[index].pr_flags &
                   CONN_FLAG_PAGEDRESULTS_PROCESSING);
            /* if ret is true, the following doesn't do anything */
            conn->c_pagedresults.prl_list[index].pr_flags |=
                                              CONN_FLAG_PAGEDRESULTS_PROCESSING;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_check_or_set_processing", "<= %d\n", ret);
    return ret;
}

/*
 * mark the connection as being done with pagedresults
 * processing - returns True if it was processing,
 * False otherwise
 */
int
pagedresults_reset_processing(Connection *conn, int index)
{
    int ret = 0;
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_reset_processing", "=> idx=%d\n", index);
    if (conn && (index > -1)) {
        pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            ret = (conn->c_pagedresults.prl_list[index].pr_flags &
                   CONN_FLAG_PAGEDRESULTS_PROCESSING);
            /* if ret is false, the following doesn't do anything */
            conn->c_pagedresults.prl_list[index].pr_flags &=
                                             ~CONN_FLAG_PAGEDRESULTS_PROCESSING;
        }
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_reset_processing", "<= %d\n", ret);
    return ret;
}
#endif

/*
 * This timedout is mainly for an end user leaves a commandline untouched
 * for a long time.  This should not affect a permanent connection which
 * manages multiple simple paged results requests over the connection.
 *
 * [rule]
 * If there is just one slot and it's timed out, we return it is timedout.
 * If there are multiple slots, the connection may be a permanent one.
 * Do not return timed out here.  But let the next request take care the
 * timedout slot(s).
 *
 * must be called within conn->c_mutex
 */
int
pagedresults_is_timedout_nolock(Connection *conn)
{
    PagedResults *prp = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_is_timedout", "=>\n");

    if (!conn || (0 == conn->c_pagedresults.prl_maxlen)) {
        slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_is_timedout", "<= false 1\n");
        return 0;
    }

    prp = conn->c_pagedresults.prl_list;
    if (prp && (1 == conn->c_pagedresults.prl_maxlen)) {
        if (slapi_timespec_expire_check(&(prp->pr_timelimit_hr)) == TIMER_EXPIRED) {
            slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_is_timedout", "<= true\n");
            return 1;
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE, "<-- pagedresults_is_timedout", "<= false 2\n");
    return 0;
}

/*
 * reset all timeout
 * must be called within conn->c_mutex
 */
int
pagedresults_reset_timedout_nolock(Connection *conn)
{
    int i;
    PagedResults *prp = NULL;

    if (NULL == conn) {
        return 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_reset_timedout", "=>\n");

    for (i = 0; i < conn->c_pagedresults.prl_maxlen; i++) {
        prp = conn->c_pagedresults.prl_list + i;
        prp->pr_timelimit_hr.tv_sec = 0;
        prp->pr_timelimit_hr.tv_nsec = 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_reset_timedout", "<=\n");
    return 0;
}

/* paged results requests are in progress. */
int
pagedresults_in_use_nolock(Connection *conn)
{
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_in_use_nolock", "=>\n");
    if (NULL == conn) {
        slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_in_use_nolock", "<= Connection is NULL\n");
        return 0;
    }
    slapi_log_err(SLAPI_LOG_TRACE, "pagedresults_in_use_nolock", "<= %d\n",
                  conn->c_pagedresults.prl_count);
    return conn->c_pagedresults.prl_count;
}

int
op_is_pagedresults(Operation *op)
{
    if (NULL == op) {
        return 0;
    }
    return op->o_flags & OP_FLAG_PAGED_RESULTS;
}

void
op_set_pagedresults(Operation *op)
{
    if (NULL == op) {
        return;
    }
    op->o_flags |= OP_FLAG_PAGED_RESULTS;
}

/*
 * pagedresults_lock/unlock -- introduced to protect search results for the
 * asynchronous searches.
 */
void
pagedresults_lock(Connection *conn, int index)
{
    PagedResults *prp;
    if (!conn || (index < 0) || (index >= conn->c_pagedresults.prl_maxlen)) {
        return;
    }
    pthread_mutex_lock(&(conn->c_mutex));
    prp = conn->c_pagedresults.prl_list + index;
    pthread_mutex_unlock(&(conn->c_mutex));
    if (prp->pr_mutex) {
        PR_Lock(prp->pr_mutex);
    }
    return;
}

void
pagedresults_unlock(Connection *conn, int index)
{
    PagedResults *prp;
    if (!conn || (index < 0) || (index >= conn->c_pagedresults.prl_maxlen)) {
        return;
    }
    pthread_mutex_lock(&(conn->c_mutex));
    prp = conn->c_pagedresults.prl_list + index;
    pthread_mutex_unlock(&(conn->c_mutex));
    if (prp->pr_mutex) {
        PR_Unlock(prp->pr_mutex);
    }
    return;
}

int
pagedresults_is_abandoned_or_notavailable(Connection *conn, int locked, int index)
{
    PagedResults *prp;
    if (!conn || (index < 0) || (index >= conn->c_pagedresults.prl_maxlen)) {
        return 1; /* not abandoned, but do not want to proceed paged results op. */
    }
    if (!locked) {
        pthread_mutex_lock(&(conn->c_mutex));
    }
    prp = conn->c_pagedresults.prl_list + index;
    if (!locked) {
        pthread_mutex_unlock(&(conn->c_mutex));
    }
    return prp->pr_flags & CONN_FLAG_PAGEDRESULTS_ABANDONED;
}

int
pagedresults_set_search_result_pb(Slapi_PBlock *pb, void *sr, int locked)
{
    int rc = -1;
    Connection *conn = NULL;
    Operation *op = NULL;
    int index = -1;
    if (!pb) {
        return 0;
    }
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (!op_is_pagedresults(op)) {
        return 0; /* noop */
    }
    slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &index);
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result_pb", "=> idx=%d, sr=%p\n", index, sr);
    if (conn && (index > -1)) {
        if (!locked)
            pthread_mutex_lock(&(conn->c_mutex));
        if (index < conn->c_pagedresults.prl_maxlen) {
            conn->c_pagedresults.prl_list[index].pr_search_result_set = sr;
            rc = 0;
        }
        if (!locked) {
            pthread_mutex_unlock(&(conn->c_mutex));
        }
    }
    slapi_log_err(SLAPI_LOG_TRACE,
                  "pagedresults_set_search_result_pb", "<= %d\n", rc);
    return rc;
}
