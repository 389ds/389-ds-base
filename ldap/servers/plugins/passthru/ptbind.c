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
/*
 * ptbind.c - LDAP bind-related code for Pass Through Authentication
 *
 */

#include "passthru.h"

static int
passthru_simple_bind_once_s( PassThruServer *srvr, char *dn,
	struct berval *creds, LDAPControl **reqctrls, int *lderrnop,
	char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
	LDAPControl ***resctrlsp );


/*
 * Attempt to chain a bind request off to "srvr." We return an LDAP error
 * code that indicates whether we successfully got a response from the
 * other server or not.  If we succeed, we return LDAP_SUCCESS and *lderrnop
 * is set to the result code from the remote server.
 *
 * Note that in the face of "ldap server down" or "ldap connect failed" errors
 * we make up to "tries" attempts to bind to the remote server.  Since we
 * are only interested in recovering silently when the remote server is up
 * but decided to close our connection, we retry without pausing between
 * attempts.
 */
int
passthru_simple_bind_s( Slapi_PBlock *pb, PassThruServer *srvr, int tries,
	char *dn, struct berval *creds, LDAPControl **reqctrls, int *lderrnop,
	char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
	LDAPControl ***resctrlsp )
{
    int		rc;

    PASSTHRU_ASSERT( srvr != NULL );
    PASSTHRU_ASSERT( tries > 0 );
    PASSTHRU_ASSERT( creds != NULL );
    PASSTHRU_ASSERT( lderrnop != NULL );
    PASSTHRU_ASSERT( refurlsp != NULL );

    do {
	/*
	 * check to see if operation has been abandoned...
	 */
	if ( slapi_op_abandoned( pb )) {
	    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		    "operation abandoned\n" );
	    rc = LDAP_USER_CANCELLED;
	} else {
	    rc = passthru_simple_bind_once_s( srvr, dn, creds, reqctrls,
		    lderrnop, matcheddnp, errmsgp, refurlsp, resctrlsp );
	}
    } while ( PASSTHRU_LDAP_CONN_ERROR( rc ) && --tries > 0 );

    return( rc );
}


/*
 * like passthru_simple_bind_s() but only makes one attempt.
 */
static int
passthru_simple_bind_once_s( PassThruServer *srvr, char *dn,
	struct berval *creds, LDAPControl **reqctrls, int *lderrnop,
	char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
	LDAPControl ***resctrlsp )
{
    int			rc, msgid;
    char		**referrals;
    struct timeval	tv, *timeout;
    LDAPMessage		*result;
    LDAP		*ld;

    /*
     * Grab an LDAP connection to use for this bind.
     */
    ld = NULL;
    if (( rc = passthru_get_connection( srvr, &ld )) != LDAP_SUCCESS ) {
	goto release_and_return;
    }

    /*
     * Send the bind operation (need to retry on LDAP_SERVER_DOWN)
     */
    if (( rc = ldap_sasl_bind( ld, dn, LDAP_SASL_SIMPLE, creds, reqctrls,
		NULL, &msgid )) != LDAP_SUCCESS ) {
	goto release_and_return;
    }

    /*
     * determine timeout value (how long we will wait for a response)
     * if timeout is NULL or zero'd, we wait indefinitely.
     */
    if ( srvr->ptsrvr_timeout == NULL || ( srvr->ptsrvr_timeout->tv_sec == 0
	    && srvr->ptsrvr_timeout->tv_usec == 0 )) {
	timeout = NULL;
    } else {
	tv = *srvr->ptsrvr_timeout;	/* struct copy */
	timeout = &tv;
    }

    /*
     * Wait for a result.
     */
    rc = ldap_result( ld, msgid, 1, timeout, &result );

    /*
     * Interpret the result.
     */
    if ( rc == 0 ) {		/* timeout */
	/*
	 * Timed out waiting for a reply from the server.
	 */
	rc = LDAP_TIMEOUT;
    } else if ( rc < 0 ) {
	/*
	 * Some other error occurred (no result received).
	 */
	rc = ldap_get_lderrno( ld, matcheddnp, errmsgp );
    } else {
	/*
	 * Got a result from remote server -- parse it.
	 */
	rc = ldap_parse_result( ld, result, lderrnop, matcheddnp, errmsgp,
		&referrals, resctrlsp, 1 );
	if ( referrals != NULL ) {
	    *refurlsp = passthru_strs2bervals( referrals );
	    ldap_value_free( referrals );
	}
    }


release_and_return:
    if ( ld != NULL ) {
	passthru_release_connection( srvr, ld, PASSTHRU_LDAP_CONN_ERROR( rc ));
    }

    return( rc );
}
