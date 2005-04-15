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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ptconn.c - LDAP connection-related code for Pass Through Authentication
 *
 */

#include "passthru.h"

/*
 * function prototypes
 */
static int dn_is_underneath_suffix( PassThruSuffix *suffix, char *normdn,
    int dnlen );
static void close_and_dispose_connection( PassThruConnection *conn );
static void check_for_stale_connections( PassThruServer *srvr );


/*
 * Most of the complicated connection-related code lives in this file.  Some
 * general notes about how we manage our connections to "remote" LDAP servers:
 *
 * 1) Each server we have a relationship with is managed independently.
 *
 * 2) We may simultaneously issue multiple bind requests on a single LDAP
 *    connection.  Each server has a "maxconcurrency" configuration
 *    parameter associated with it that caps the number of outstanding
 *    binds per connection.  For each connection we maintain a "usecount"
 *    which is used to track the number of threads using the connection.
 *
 * 3) We may open more than one connection to a server.  This is only done
 *    when "maxconcurrency" is exceeded for all the connections we already
 *    have open.  Each server has a "maxconnections" configuration
 *    parameter associated with it that caps the number of connections.
 *    We also maintain a "connlist_count" for each server so we know when
 *    we have reached the maximum number of open connections allowed.
 *
 * 4) If no connection is available to service a request (and we have
 *    reached the limit of how many we are supposed to open), threads
 *    go to sleep on a condition variable and one is woken up each time
 *    a connection's "usecount" is decremented.
 *
 * 5) If we see an LDAP_CONNECT_ERROR or LDAP_SERVER_DOWN error on a
 *    session handle, we mark its status as PASSTHRU_CONNSTATUS_DOWN and
 *    close it as soon as all threads using it release it.  Connections
 *    marked as "down" are not counted against the "maxconnections" limit.
 *
 * 6) We close and reopen connections that have been open for more than
 *    the server's configured connection lifetime.  This is done to ensure
 *    that we reconnect to a primary server after failover occurs.  If no
 *    lifetime is configured or it is set to 0, we never close and reopen
 *    connections.
 */


/*
 * Given a normalized target dn, see if it we should "pass through"
 * authentication to another LDAP server.  The answer is "yes" if the
 * target dn resides under one of the suffixes we have that is associated
 * with an LDAP server we know about.
 *
 * This function assumes that normdn is normalized and the the suffixes in the
 * cfg structure have also been normalized.
 * 
 * Returns an LDAP error code, typically:
 *	LDAP_SUCCESS		should pass though; *srvrp set.
 *	LDAP_NO_SUCH_OBJECT	let this server handle the bind.
 */
int
passthru_dn2server( PassThruConfig *cfg, char *normdn, PassThruServer **srvrp )
{
    PassThruServer	*ptsrvr;
    PassThruSuffix	*ptsuffix;
    int			dnlen;

    PASSTHRU_ASSERT( cfg != NULL );
    PASSTHRU_ASSERT( normdn != NULL );
    PASSTHRU_ASSERT( srvrp != NULL );

    dnlen = strlen( normdn );

    for ( ptsrvr = cfg->ptconfig_serverlist; ptsrvr != NULL;
	    ptsrvr = ptsrvr->ptsrvr_next ) {
	for ( ptsuffix = ptsrvr->ptsrvr_suffixes; ptsuffix != NULL;
		ptsuffix = ptsuffix->ptsuffix_next ) {
	    if ( dn_is_underneath_suffix( ptsuffix, normdn, dnlen )) {
		*srvrp = ptsrvr;
		return( LDAP_SUCCESS );		/* got it */
	    }
	}
    }

    *srvrp = NULL;
    return( LDAP_NO_SUCH_OBJECT );		/* no match */
}


/*
 * Get an LDAP session handle for communicating with srvr.
 *
 * Returns an LDAP eror code, typically:
 *	LDAP_SUCCESS
 *	other
 */
int
passthru_get_connection( PassThruServer *srvr, LDAP **ldp )
{
    int			rc;
    PassThruConnection	*conn, *connprev;
    LDAP		*ld;

    PASSTHRU_ASSERT( srvr != NULL );
    PASSTHRU_ASSERT( ldp != NULL );

    check_for_stale_connections( srvr );

    slapi_lock_mutex( srvr->ptsrvr_connlist_mutex );
    rc = LDAP_SUCCESS;		/* optimistic */

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	"=> passthru_get_connection server %s:%d conns: %d maxconns: %d\n",
	srvr->ptsrvr_hostname, srvr->ptsrvr_port, srvr->ptsrvr_connlist_count,
	srvr->ptsrvr_maxconnections );

    for ( ;; ) {
	/*
	 * look for an available, already open connection
	 */
	connprev = NULL;
	for ( conn = srvr->ptsrvr_connlist; conn != NULL;
		conn = conn->ptconn_next ) {
	    if ( conn->ptconn_status == PASSTHRU_CONNSTATUS_OK
		    && conn->ptconn_usecount < srvr->ptsrvr_maxconcurrency ) {
#ifdef PASSTHRU_VERBOSE_LOGGING
		slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
			"<= passthru_get_connection server found "
			"conn 0x%x to use)\n", conn->ptconn_ld );
#endif
		goto unlock_and_return;		/* found one */
	    }
	    connprev = conn;
	}

	if ( srvr->ptsrvr_connlist_count < srvr->ptsrvr_maxconnections ) {
	    /*
	     * we have not exceeded the maximum number of connections allowed,
	     * so we initialize a new one and add it to the end of our list.
	     */
	    if (( ld = slapi_ldap_init( srvr->ptsrvr_hostname,
		    srvr->ptsrvr_port, srvr->ptsrvr_secure, 1 )) == NULL ) {
#ifdef PASSTHRU_VERBOSE_LOGGING
		slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
			"<= passthru_get_connection slapi_ldap_init failed\n" );
#endif
		rc = LDAP_LOCAL_ERROR;
		goto unlock_and_return;
	    }

	    /*
	     * set protocol version to correct value for this server
	     */
	    if ( ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION,
		    &srvr->ptsrvr_ldapversion ) != 0 ) {
		slapi_ldap_unbind( ld );
	    }

	    conn = (PassThruConnection *)slapi_ch_malloc(
		    sizeof( PassThruConnection ));
	    conn->ptconn_ld = ld;
	    conn->ptconn_status = PASSTHRU_CONNSTATUS_OK;
	    time( &conn->ptconn_opentime );
	    conn->ptconn_ldapversion = srvr->ptsrvr_ldapversion;
	    conn->ptconn_usecount = 0;
	    conn->ptconn_next = NULL;
	    conn->ptconn_prev = connprev;
	    if ( connprev == NULL ) {
		srvr->ptsrvr_connlist = conn;
		conn->ptconn_prev = NULL;
	    } else {
		connprev->ptconn_next = conn;
		conn->ptconn_prev = connprev;
	    }

	    ++srvr->ptsrvr_connlist_count;

#ifdef PASSTHRU_VERBOSE_LOGGING
	    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		    "<= passthru_get_connection added new conn 0x%x, "
		    "conn count now %d\n", ld, srvr->ptsrvr_connlist_count );
#endif
	    goto unlock_and_return;		/* got a new one */
	}

#ifdef PASSTHRU_VERBOSE_LOGGING
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"... passthru_get_connection waiting for conn to free up\n" );
#endif
	slapi_wait_condvar( srvr->ptsrvr_connlist_cv, NULL );

#ifdef PASSTHRU_VERBOSE_LOGGING
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"... passthru_get_connection awake again\n" );
#endif
    }

unlock_and_return:
    if ( conn != NULL ) {
	++conn->ptconn_usecount;
	*ldp = conn->ptconn_ld;
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"<= passthru_get_connection ld=0x%x (concurrency now %d)\n",
		*ldp, conn->ptconn_usecount );
    } else {
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"<= passthru_get_connection error %d\n", rc );
    }

    slapi_unlock_mutex( srvr->ptsrvr_connlist_mutex );
    return( rc );
}


/*
 * Mark the connection ld is associated with as free to be used again.
 * If dispose is non-zero, we mark the connection as "bad" and dispose
 *    of it and its ld once the use count becomes zero.
 */
void
passthru_release_connection( PassThruServer *srvr, LDAP *ld, int dispose )
{
    PassThruConnection	*conn, *connprev;

    PASSTHRU_ASSERT( srvr != NULL );
    PASSTHRU_ASSERT( ld != NULL );

#ifdef PASSTHRU_VERBOSE_LOGGING
    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> passthru_release_connection ld=0x%x%s\n", ld,
	    dispose ? " (disposing)" : "" );
#endif

    slapi_lock_mutex( srvr->ptsrvr_connlist_mutex );

    /*
     * find the connection structure this ld is part of
     */
    connprev = NULL;
    for ( conn = srvr->ptsrvr_connlist; conn != NULL;
	    conn = conn->ptconn_next ) {
	if ( ld == conn->ptconn_ld ) {
	    break;
	}
	connprev = conn;
    }

    if ( conn == NULL ) {		/* ld not found -- unexpected */
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"=> passthru_release_connection ld=0x%x not found\n", ld );
    } else {
	PASSTHRU_ASSERT( conn->ptconn_usecount > 0 );
	--conn->ptconn_usecount;
	if ( dispose ) {
	    conn->ptconn_status = PASSTHRU_CONNSTATUS_DOWN;
	}

	if ( conn->ptconn_status != PASSTHRU_CONNSTATUS_OK
		&& conn->ptconn_usecount == 0 ) {
	    /*
	     * remove from server's connection list
	     */
	    if ( connprev == NULL ) {
		srvr->ptsrvr_connlist = conn->ptconn_next;
	    } else {
		connprev->ptconn_next = conn->ptconn_next;
	    }
	    --srvr->ptsrvr_connlist_count;

	    /*
	     * close connection and free memory
	     */
	    close_and_dispose_connection( conn );
	}
    }

    /*
     * wake up a thread that is waiting for a connection (there may not be
     * any but the slapi_notify_condvar() call should be cheap in any event).
     */
    slapi_notify_condvar( srvr->ptsrvr_connlist_cv, 0 );

    /*
     * unlock and return
     */
    slapi_unlock_mutex( srvr->ptsrvr_connlist_mutex );
}


/*
 * close all open connections in preparation for server shutdown, etc.
 */
void
passthru_close_all_connections( PassThruConfig *cfg )
{
    PassThruServer	*srvr;
    PassThruConnection	*conn, *nextconn;

    PASSTHRU_ASSERT( cfg != NULL );

    for ( srvr = cfg->ptconfig_serverlist; srvr != NULL;
	    srvr = srvr->ptsrvr_next ) {
	for ( conn = srvr->ptsrvr_connlist; conn != NULL; conn = nextconn ) {
	    nextconn = conn->ptconn_next;
	    close_and_dispose_connection( conn );
	}
    }
}


/*
 * return non-zero value if normdn falls underneath a suffix
 */
static int
dn_is_underneath_suffix( PassThruSuffix *suffix, char *normdn, int dnlen )
{
    PASSTHRU_ASSERT( suffix != NULL );
    PASSTHRU_ASSERT( normdn != NULL );
    PASSTHRU_ASSERT( dnlen >= 0 );

    return ( suffix->ptsuffix_len <= dnlen &&
	    slapi_UTF8CASECMP( suffix->ptsuffix_normsuffix,
	    normdn + ( dnlen - suffix->ptsuffix_len )) == 0 );
}


/*
 * Unbind from server and dispose of a connection.
 */
static void
close_and_dispose_connection( PassThruConnection *conn )
{
    PASSTHRU_ASSERT( conn != NULL );
    PASSTHRU_ASSERT( conn->ptconn_ld != NULL );

    slapi_ldap_unbind( conn->ptconn_ld );
    conn->ptconn_ld = NULL;
    slapi_ch_free( (void **)&conn );
}


/*
 * Close (or mark to be closed) any connections for this srvr that have
 * exceeded the maximum connection lifetime.
 */
static void
check_for_stale_connections( PassThruServer *srvr )
{
    PassThruConnection	*conn, *prevconn, *nextconn;
    time_t		curtime;

    PASSTHRU_ASSERT( srvr != NULL );

#ifdef PASSTHRU_VERBOSE_LOGGING
    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "check_for_stale_connections: server %s (lifetime %d secs)\n",
	    srvr->ptsrvr_url, srvr->ptsrvr_connlifetime );
#endif


    if ( srvr->ptsrvr_connlifetime <= 0 ) {
	return;
    }

    time( &curtime );

    slapi_lock_mutex( srvr->ptsrvr_connlist_mutex );

    prevconn = NULL;
    for ( conn = srvr->ptsrvr_connlist; conn != NULL; conn = nextconn ) {
	nextconn = conn->ptconn_next;

	if ( curtime - conn->ptconn_opentime > srvr->ptsrvr_connlifetime ) {
	    if ( conn->ptconn_usecount == 0 ) {
		/*
		 * connection is idle and stale -- remove from server's list
		 */
#ifdef PASSTHRU_VERBOSE_LOGGING
		slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
			"check_for_stale_connections: discarding idle, "
			"stale connection 0x%x\n", conn->ptconn_ld );
#endif
		if ( prevconn == NULL ) {
		    srvr->ptsrvr_connlist = nextconn;
		} else {
		    prevconn->ptconn_next = nextconn;
		}
		--srvr->ptsrvr_connlist_count;
		close_and_dispose_connection( conn );
	    } else {
		/*
		 * connection is stale but in use -- mark to be disposed later
		 */
#ifdef PASSTHRU_VERBOSE_LOGGING
		slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
			"check_for_stale_connections: marking connection 0x%x "
			"stale (use count %d)\n", conn->ptconn_ld,
			conn->ptconn_usecount );
#endif
		conn->ptconn_status = PASSTHRU_CONNSTATUS_STALE;
		prevconn = conn;
	    }
	} else {
	    prevconn = conn;
	}
    }

    slapi_unlock_mutex( srvr->ptsrvr_connlist_mutex );
}
