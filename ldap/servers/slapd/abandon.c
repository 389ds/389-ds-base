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

/* abandon.c - decode and handle an ldap abandon operation */

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

void
do_abandon(Slapi_PBlock *pb)
{
    int err, suppressed_by_plugin = 0;
    ber_int_t id;
    Connection *pb_conn = NULL;
    Operation *pb_op = NULL;
    Operation *o;
    int32_t log_format = config_get_accesslog_log_format();
    slapd_log_pblock logpb = {0};
    char *sessionTrackingId;
    /* Should fit
     *  - ~10chars for ' sid=\"..\"'
     *  - 15+3 for the truncated sessionID
     * Need to sync with SESSION_ID_STR_SZ
     */
    char session_str[30] = {0};
    /* Keep a copy of some data because o may vanish once conn is unlocked */
    struct {
        struct timespec hr_time_end;
        int nentries;
        int opid;
    } o_copy;

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);

    slapi_log_err(SLAPI_LOG_TRACE, "do_abandon", "->\n");

    if (pb_op == NULL || pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "do_abandon", "NULL param: pb_conn (0x%p) pb_op (0x%p)\n",
                      pb_conn, pb_op);
        return;
    }

    BerElement *ber = pb_op->o_ber;

    /*
     * Parse the abandon request.  It looks like this:
     *
     *    AbandonRequest := MessageID
     */

    if (ber_scanf(ber, "i", &id) == LBER_ERROR) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "do_abandon", "ber_scanf failed (op=Abandon; params=ID)\n");
        return;
    }

    slapi_pblock_set(pb, SLAPI_ABANDON_MSGID, &id);

    /*
     * in LDAPv3 there can be optional control extensions on
     * the end of an LDAPMessage. we need to read them in and
     * pass them to the backend.
     */
    if ((err = get_ldapmessage_controls(pb, ber, NULL)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "do_abandon", "get_ldapmessage_controls failed: %d (%s) (op=Abandon)\n",
                      err, ldap_err2string(err));
        /* LDAP does not allow any response to an abandon */
        return;
    }

    slapi_log_err(SLAPI_LOG_ARGS, "do_abandon", "id %d\n", id);

    slapi_pblock_get(pb, SLAPI_SESSION_TRACKING, &sessionTrackingId);

    /* prepare session_str to be logged */
    if (sessionTrackingId) {
        if (sizeof(session_str) < (strlen(sessionTrackingId) + 10 + 1)) {
            /* The session tracking string is too large to fit in 'session_str'
             * Likely SESSION_ID_STR_SZ was changed without increasing the size of session_str.
             * Just ignore the session string.
             */
            session_str[0] = '\0';
            slapi_log_err(SLAPI_LOG_ERR, "do_abandon", "Too large session tracking string (%ld) - It is ignored\n",
                          strlen(sessionTrackingId));
        } else {
            snprintf(session_str, sizeof(session_str), " sid=\"%s\"", sessionTrackingId);
        }
    } else {
        session_str[0] = '\0';
    }

    /*
     * find the operation being abandoned and set the o_abandon
     * flag.  We don't allow the operation to abandon itself.
     * It's up to the backend to periodically check this
     * flag and abort the operation at a convenient time.
     */

    pthread_mutex_lock(&(pb_conn->c_mutex));
    for (o = pb_conn->c_ops; o != NULL; o = o->o_next) {
        if (o->o_msgid == id && o != pb_op) {
            slapi_operation_time_elapsed(o, &o_copy.hr_time_end);
            o_copy.nentries = o->o_results.r.r_search.nentries;
            o_copy.opid = o->o_opid;
            break;
        }
    }

    if (o != NULL) {
        const Slapi_DN *ts = NULL;
        /*
         * call the pre-abandon plugins. if they succeed, call
         * the backend abandon function. then call the post-abandon
         * plugins.
         */
        /* ONREPL - plugins should be passed some information about abandoned operation */
        /* target spec and abandoned operation type are used to decide which plugins
           are applicable for the operation */
        ts = operation_get_target_spec(o);
        if (ts) {
            operation_set_target_spec(pb_op, ts);
        } else {
            slapi_log_err(SLAPI_LOG_TRACE, "do_abandon", "no target spec of abandoned operation\n");
        }

        operation_set_abandoned_op(pb_op, o->o_abandoned_op);
        if (plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_ABANDON_FN) == 0) {
            int rc = 0;

            if (o->o_status != SLAPI_OP_STATUS_RESULT_SENT) {
                o->o_status = SLAPI_OP_STATUS_ABANDONED;
            } else {
                o = NULL; /* nothing was abandoned */
            }

            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
            plugin_call_plugins(pb, SLAPI_PLUGIN_POST_ABANDON_FN);
        } else {
            suppressed_by_plugin = 1;
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "do_abandon", "op not found\n");
    }

    pthread_mutex_unlock(&(pb_conn->c_mutex));

    /* Prep the log block */
    slapd_log_pblock_init(&logpb, log_format, pb);
    logpb.msgid = id;
    logpb.nentries = -1;
    logpb.tv_sec = -1;
    logpb.tv_nsec = -1;

    if (0 == pagedresults_free_one_msgid(pb_conn, id, PR_NOT_LOCKED)) {
        if (log_format != LOG_FORMAT_DEFAULT) {
            /* JSON logging */
            logpb.target_op = "Simple Paged Results";
            slapd_log_access_abandon(&logpb);
        } else {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64
                             " op=%d ABANDON targetop=Simple Paged Results msgid=%d%s\n",
                             pb_conn->c_connid, pb_op->o_opid, id, session_str);
        }
    } else if (NULL == o) {
        if (log_format != LOG_FORMAT_DEFAULT) {
            /* JSON logging */
            logpb.target_op = "NOTFOUND";
            slapd_log_access_abandon(&logpb);
        } else {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                             " targetop=NOTFOUND msgid=%d%s\n",
                             pb_conn->c_connid, pb_op->o_opid, id, session_str);
        }
    } else if (suppressed_by_plugin) {
        if (log_format != LOG_FORMAT_DEFAULT) {
            /* JSON logging */
            logpb.target_op = "SUPPRESSED-BY-PLUGIN";
            slapd_log_access_abandon(&logpb);
        } else {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                             " targetop=SUPPRESSED-BY-PLUGIN msgid=%d%s\n",
                             pb_conn->c_connid, pb_op->o_opid, id, session_str);
        }
    } else {
        if (log_format != LOG_FORMAT_DEFAULT) {
            /* JSON logging */
            char targetop[11] = {0};

            PR_snprintf(targetop, sizeof(targetop), "%d", o_copy.opid);
            logpb.target_op = targetop;
            logpb.nentries = o_copy.nentries;
            logpb.tv_sec = (int64_t)o_copy.hr_time_end.tv_sec;
            logpb.tv_nsec = (int64_t)o_copy.hr_time_end.tv_nsec;
            slapd_log_access_abandon(&logpb);
        } else {
            slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                             " targetop=%d msgid=%d nentries=%d etime=%" PRId64 ".%010" PRId64 "%s\n",
                             pb_conn->c_connid, pb_op->o_opid, o_copy.opid, id,
                             o_copy.nentries, (int64_t)o_copy.hr_time_end.tv_sec,
                             (int64_t)o_copy.hr_time_end.tv_nsec, session_str);
        }
    }
    /*
     * Wake up the persistent searches, so they
     * can notice if they've been abandoned.
     */
    ps_wakeup_all();
}
