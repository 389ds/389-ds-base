/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

void
do_unbind( Slapi_PBlock *pb )
{
	Slapi_Operation *operation;
	BerElement	*ber;
	int		err;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_unbind\n", 0, 0, 0 );

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	ber = operation->o_ber;
	/*
	 * Parse the unbind request.  It looks like this:
	 *
	 *	UnBindRequest ::= NULL
	 */
	if ( ber_get_null( ber ) == LBER_ERROR ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%d op=%d UNBIND,"
				" decoding error: UnBindRequest not null\n",
				pb->pb_conn->c_connid, operation->o_opid );
		/* LDAPv3 does not allow a response to an unbind... so just return. */
		goto free_and_return;
	}

	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%d op=%d UNBIND,"
				" error processing controls - error %d (%s)\n",
				pb->pb_conn->c_connid, operation->o_opid,
				err, ldap_err2string( err ));
		/* LDAPv3 does not allow a response to an unbind... so just return. */
		goto free_and_return;
	}

	/* target spec is used to decide which plugins are applicable for the operation */
	PR_Lock( pb->pb_conn->c_mutex );
	operation_set_target_spec_str (operation, pb->pb_conn->c_dn);
	PR_Unlock( pb->pb_conn->c_mutex );

	/* ONREPL - plugins should be called and passed bind dn and, possibly, other data */

	slapi_log_access( LDAP_DEBUG_STATS, "conn=%d op=%d UNBIND\n",
	    pb->pb_conn->c_connid, operation->o_opid );

	/* pass the unbind to all backends */
	be_unbindall( pb->pb_conn, operation );

	/* close the connection to the client */
	disconnect_server( pb->pb_conn, operation->o_connid, operation->o_opid, SLAPD_DISCONNECT_UNBIND, 0);

free_and_return:;
}
