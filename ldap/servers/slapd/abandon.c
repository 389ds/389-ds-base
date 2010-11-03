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
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

void
do_abandon( Slapi_PBlock *pb )
{
	int		err, suppressed_by_plugin = 0;
	ber_int_t	id;
	Operation	*o;
	BerElement	*ber = pb->pb_op->o_ber;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_abandon\n", 0, 0, 0 );

	/*
	 * Parse the abandon request.  It looks like this:
	 *
	 *	AbandonRequest := MessageID
	 */

	if ( ber_scanf( ber, "i", &id ) == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ber_scanf failed (op=Abandon; params=ID)\n",
		    0, 0 ,0 );
		return;
	}

	slapi_pblock_set( pb, SLAPI_ABANDON_MSGID, &id );

	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY,
				"get_ldapmessage_controls failed: %d (%s) (op=Abandon)\n", 
				err, ldap_err2string( err ), 0);
		/* LDAP does not allow any response to an abandon */
		return;
	}

	LDAPDebug( LDAP_DEBUG_ARGS, "do_abandon: id %d\n", id, 0 ,0 );

	/*
	 * find the operation being abandoned and set the o_abandon
	 * flag.  We don't allow the operation to abandon itself.
	 * It's up to the backend to periodically check this
	 * flag and abort the operation at a convenient time.
	 */

	PR_Lock( pb->pb_conn->c_mutex );
	for ( o = pb->pb_conn->c_ops; o != NULL; o = o->o_next ) {
		if ( o->o_msgid == id && o != pb->pb_op)
			break;
	}

	if ( o != NULL ) {
	  	const Slapi_DN *ts = NULL;
		/*
		 * call the pre-abandon plugins. if they succeed, call
		 * the backend abandon function. then call the post-abandon
		 * plugins.
		 */
		/* ONREPL - plugins should be passed some information about abandoned operation */
		/* target spec and abandoned operation type are used to decide which plugins 
		   are applicable for the operation */
		ts = operation_get_target_spec (o);
		if (ts) {
		  	operation_set_target_spec (pb->pb_op, ts);
		} else {
		     LDAPDebug( LDAP_DEBUG_TRACE, "do_abandon: no target spec of abandoned operation\n", 0,0,0);
		}

		operation_set_abandoned_op (pb->pb_op, o->o_abandoned_op);
		if ( plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_ABANDON_FN )
		    == 0 ) {
			int	rc = 0;

			if ( o->o_status != SLAPI_OP_STATUS_RESULT_SENT ) {
				o->o_status = SLAPI_OP_STATUS_ABANDONED;
			} else {
				o = NULL;	/* nothing was abandoned */
			}

			slapi_pblock_set( pb, SLAPI_PLUGIN_OPRETURN, &rc );
			plugin_call_plugins( pb, SLAPI_PLUGIN_POST_ABANDON_FN );
		} else {
			suppressed_by_plugin = 1;
		}
	} else {
		LDAPDebug( LDAP_DEBUG_TRACE, "do_abandon: op not found\n", 0, 0,
		    0 );
	}

	if (pagedresults_cleanup(pb->pb_conn)) {
		/* Cleaned up paged result connection */
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d ABANDON"
			" targetop=Simple Paged Results\n",
			pb->pb_conn->c_connid, pb->pb_op->o_opid );
	} else if ( NULL == o ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d ABANDON"
			" targetop=NOTFOUND msgid=%d\n",
			pb->pb_conn->c_connid, pb->pb_op->o_opid, id );
	} else if ( suppressed_by_plugin ) {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d ABANDON"
			" targetop=SUPPRESSED-BY-PLUGIN msgid=%d\n",
			pb->pb_conn->c_connid, pb->pb_op->o_opid, id );
	} else {
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d ABANDON"
			" targetop=%d msgid=%d nentries=%d etime=%ld\n",
			pb->pb_conn->c_connid, pb->pb_op->o_opid, o->o_opid, id,
			o->o_results.r.r_search.nentries, current_time() - o->o_time );

	}

	PR_Unlock( pb->pb_conn->c_mutex );
	/*
	 * Wake up the persistent searches, so they
	 * can notice if they've been abandoned.
	 */
	ps_wakeup_all();
}
