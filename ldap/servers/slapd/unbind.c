/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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
	int ignore_criticality = 1;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_unbind\n", 0, 0, 0 );

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	ber = operation->o_ber;
	/*
	 * Parse the unbind request.  It looks like this:
	 *
	 *	UnBindRequest ::= NULL
	 */
	if ( ber_get_null( ber ) == LBER_ERROR ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d UNBIND,"
				" decoding error: UnBindRequest not null\n",
				(long long unsigned int)pb->pb_conn->c_connid, operation->o_opid );
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
	if ( (err = get_ldapmessage_controls_ext( pb, ber, NULL, ignore_criticality )) != 0 ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d UNBIND,"
				" error processing controls - error %d (%s)\n",
				(long long unsigned int)pb->pb_conn->c_connid, operation->o_opid,
				err, ldap_err2string( err ));
		/* LDAPv3 does not allow a response to an unbind... so just return. */
		goto free_and_return;
	}

	/* target spec is used to decide which plugins are applicable for the operation */
	PR_Lock( pb->pb_conn->c_mutex );
	operation_set_target_spec_str (operation, pb->pb_conn->c_dn);
	PR_Unlock( pb->pb_conn->c_mutex );

	/* ONREPL - plugins should be called and passed bind dn and, possibly, other data */

	slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d UNBIND\n",
			(long long unsigned int)pb->pb_conn->c_connid, operation->o_opid );

	/* pass the unbind to all backends */
	be_unbindall( pb->pb_conn, operation );

	/* close the connection to the client */
	disconnect_server( pb->pb_conn, operation->o_connid, operation->o_opid, SLAPD_DISCONNECT_UNBIND, 0);

free_and_return:;
}
