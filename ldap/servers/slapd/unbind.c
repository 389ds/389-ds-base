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

/* unbind.c - decode an ldap unbind operation and pass it to a backend db */

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
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "slap.h"

void
do_unbind(Slapi_PBlock *pb)
{
    Slapi_Operation *operation;
    Connection *pb_conn;
    BerElement *ber;
    int err;
    int ignore_criticality = 1;

    slapi_log_err(SLAPI_LOG_TRACE, "do_unbind", "=>\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    ber = operation->o_ber;
    /*
     * Parse the unbind request.  It looks like this:
     *
     *    UnBindRequest ::= NULL
     */
    if (ber_get_null(ber) == LBER_ERROR) {
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d UNBIND,"
                                           " decoding error: UnBindRequest not null\n",
                         pb_conn->c_connid, operation->o_opid);
        /* LDAPv3 does not allow a response to an unbind... so just return. */
        goto free_and_return;
    }

    /*
     * in LDAPv3 there can be optional control extensions on
     * the end of an LDAPMessage. we need to read them in and
     * pass them to the backend.
     * RFC 4511 section 4.1.11.  Controls says that the UnbindRequest
     * MUST ignore the criticality field of controls
     */
    if ((err = get_ldapmessage_controls_ext(pb, ber, NULL, ignore_criticality)) != 0) {
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d UNBIND,"
                                           " error processing controls - error %d (%s)\n",
                         pb_conn->c_connid, operation->o_opid,
                         err, ldap_err2string(err));
        /* LDAPv3 does not allow a response to an unbind... so just return. */
        goto free_and_return;
    }

    /* target spec is used to decide which plugins are applicable for the operation */
    pthread_mutex_lock(&(pb_conn->c_mutex));
    operation_set_target_spec_str(operation, pb_conn->c_dn);
    pthread_mutex_unlock(&(pb_conn->c_mutex));

    /* ONREPL - plugins should be called and passed bind dn and, possibly, other data */

    slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d UNBIND\n",
                     pb_conn->c_connid, operation->o_opid);

    /* pass the unbind to all backends */
    be_unbindall(pb_conn, operation);

free_and_return:;

    /* close the connection to the client */
    disconnect_server(pb_conn, operation->o_connid, operation->o_opid, SLAPD_DISCONNECT_UNBIND, 0);
}
