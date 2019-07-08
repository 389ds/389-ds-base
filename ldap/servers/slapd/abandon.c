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

    /*
     * find the operation being abandoned and set the o_abandon
     * flag.  We don't allow the operation to abandon itself.
     * It's up to the backend to periodically check this
     * flag and abort the operation at a convenient time.
     */

    pthread_mutex_lock(&(pb_conn->c_mutex));
    for (o = pb_conn->c_ops; o != NULL; o = o->o_next) {
        if (o->o_msgid == id && o != pb_op)
            break;
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

    if (0 == pagedresults_free_one_msgid_nolock(pb_conn, id)) {
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64
                                           " op=%d ABANDON targetop=Simple Paged Results msgid=%d\n",
                         pb_conn->c_connid, pb_op->o_opid, id);
    } else if (NULL == o) {
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                                           " targetop=NOTFOUND msgid=%d\n",
                         pb_conn->c_connid, pb_op->o_opid, id);
    } else if (suppressed_by_plugin) {
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                                           " targetop=SUPPRESSED-BY-PLUGIN msgid=%d\n",
                         pb_conn->c_connid, pb_op->o_opid, id);
    } else {
        struct timespec o_hr_time_end;
        slapi_operation_time_elapsed(o, &o_hr_time_end);
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d ABANDON"
                                           " targetop=%d msgid=%d nentries=%d etime=%" PRId64 ".%010" PRId64 "\n",
                         pb_conn->c_connid, pb_op->o_opid, o->o_opid, id,
                         o->o_results.r.r_search.nentries, (int64_t)o_hr_time_end.tv_sec, (int64_t)o_hr_time_end.tv_nsec);
    }

    pthread_mutex_unlock(&(pb_conn->c_mutex));
    /*
     * Wake up the persistent searches, so they
     * can notice if they've been abandoned.
     */
    ps_wakeup_all();
}
