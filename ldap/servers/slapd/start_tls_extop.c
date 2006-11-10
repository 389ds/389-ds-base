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

/*
 * Start TLS - LDAP Extended Operation.
 *
 *
 * This plugin implements the "Start TLS (Transport Layer Security)" 
 * extended operation for LDAP. The plugin function is called by
 * the server if an LDAP client request contains the OID:
 * "1.3.6.1.4.1.1466.20037".
 *
 */

#include <stdio.h>
#include <string.h>
#include <private/pprio.h>


#include <prio.h>
#include <ssl.h>
#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"




/* OID of the extended operation handled by this plug-in */
/* #define START_TLS_OID	"1.3.6.1.4.1.1466.20037" */


Slapi_PluginDesc exopdesc = { "start_tls_plugin", "Fedora", "0.1",
	"Start TLS extended operation plugin" };




/* Start TLS Extended operation plugin function */
int
start_tls( Slapi_PBlock *pb )
{

	char		*oid;
	Connection      *conn;
	PRFileDesc      *oldsocket, *newsocket;
	int             secure;
	int             ns;
#ifdef _WIN32
	int				oldnativesocket;
#endif
	int             rv;

	/* Get the pb ready for sending Start TLS Extended Responses back to the client. 
	 * The only requirement is to set the LDAP OID of the extended response to the START_TLS_OID. */

	if ( slapi_pblock_set( pb, SLAPI_EXT_OP_RET_OID, START_TLS_OID ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Could not set extended response oid.\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"Could not set extended response oid.", 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	}


	/* Before going any further, we'll make sure that the right extended operation plugin
	 * has been called: i.e., the OID shipped whithin the extended operation request must 
	 * match this very plugin's OID: START_TLS_OID. */

	if ( slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_OID, &oid ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Could not get OID value from request.\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"Could not get OID value from request.", 0, NULL );
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	} else {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Received extended operation request with OID %s\n", oid );
	}
	
	if ( strcasecmp( oid, START_TLS_OID ) != 0) {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Request OID does not match Start TLS OID.\n" );
	        slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"Request OID does not match Start TLS OID.", 0, NULL ); 
		return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	} else {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Start TLS extended operation request confirmed.\n" );
	}      


	/* At least we know that the request was indeed an Start TLS one. */

	conn = pb->pb_conn;
	PR_Lock( conn->c_mutex );
#ifndef _WIN32
	oldsocket = conn->c_prfd;
	if ( oldsocket == (PRFileDesc *) NULL ) {
                slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Connection socket not available.\n" );
	        slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					"Connection socket not available.", 0, NULL ); 
		goto unlock_and_return;
	}
#else
	oldnativesocket = conn->c_sd;
	oldsocket = PR_ImportTCPSocket(oldnativesocket);
	if ( oldsocket == (PRFileDesc *) NULL ) {
                slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Failed to import NT native socket into NSPR.\n" );
	        slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					"Failed to import NT native socket into NSPR.", 0, NULL ); 
		goto unlock_and_return;
	}
#endif

	/* Check whether the Start TLS request can be accepted. */

	if ( connection_operations_pending( conn, pb->pb_op,
				1 /* check for ops where result not yet sent */ )) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Other operations are still pending on the connection.\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"Other operations are still pending on the connection.", 0, NULL );
		goto unlock_and_return;
	}


	if ( !config_get_security() ) {
	        /* if any, here is where the referral to another SSL supporting server should be done: */
	        /* slapi_send_ldap_result( pb, LDAP_REFERRAL, NULL, msg, 0, url ); */
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "SSL not supported by this server.\n" );
		slapi_send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, 
					"SSL not supported by this server.", 0, NULL );
		goto unlock_and_return;
	}


	if ( conn->c_flags & CONN_FLAG_SSL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "SSL connection already established.\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"SSL connection already established.", 0, NULL );
		goto unlock_and_return;
	}

	if ( conn->c_flags & CONN_FLAG_SASL_CONTINUE ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "SASL multi-stage bind in progress.\n" );
		slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, 
					"SASL multi-stage bind in progress.", 0, NULL );
		goto unlock_and_return;
	}


	if ( conn->c_flags & CONN_FLAG_CLOSING ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				 "Connection being closed at this moment.\n" );
		slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					"Connection being closed at this moment.", 0, NULL );
		goto unlock_and_return;
	}	



	/* At first sight, there doesn't seem to be any major impediment to start TLS.
	 * So, we may as well try initialising SSL. */

	if ( slapd_security_library_is_initialized() == 0 ) {	  
               slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				"NSS libraries not initialised.\n" );
	       slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
				       "NSS libraries not initialised.", 0, NULL );
	       goto unlock_and_return;
	}	



	/* Since no specific argument for denying the Start TLS request has been found, 
	 * we send a success response back to the client. */
	
	slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
			 "Start TLS request accepted.Server willing to negotiate SSL.\n" );
	slapi_send_ldap_result( pb, LDAP_SUCCESS, NULL, 
				"Start TLS request accepted.Server willing to negotiate SSL.", 0, NULL );	
	

	/* So far we have set up the environment for deploying SSL. It's now time to import the socket
	 * into SSL and to configure it consequently. */
  
	if ( slapd_ssl_listener_is_initialized() != 0 ) {
	       PRFileDesc * ssl_listensocket;

	       ssl_listensocket = get_ssl_listener_fd(); 
	       if ( ssl_listensocket == (PRFileDesc *) NULL ) {
		       slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
					"SSL listener socket not found.\n" );
		       slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					       "SSL listener socket not found.", 0, NULL );
		       goto unlock_and_return;
	       }
	       newsocket = slapd_ssl_importFD( ssl_listensocket, oldsocket );
	       if ( newsocket == (PRFileDesc *) NULL ) {
		       slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
					"SSL socket import failed.\n" );
		       slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					       "SSL socket import failed.", 0, NULL );
		       goto unlock_and_return;
	       }
	} else {
	       if ( slapd_ssl_init2( &oldsocket, 1 ) != 0 ) {
		       slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
					"SSL socket import or configuration failed.\n" );
		       slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
					       "SSL socket import or configuration failed.", 0, NULL );
		       goto unlock_and_return;
	       }
	       newsocket = oldsocket; 
	}


	rv = slapd_ssl_resetHandshake( newsocket, 1 );
	if ( rv != SECSuccess ) {
	       slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
				"Unable to set socket ready for SSL handshake.\n" );
	       slapi_send_ldap_result( pb, LDAP_UNAVAILABLE, NULL, 
				       "Unable to set socket ready for SSL handshake.", 0, NULL );
	       goto unlock_and_return;
	}
	


	/* From here on, messages will be sent through the SSL layer, so we need to get our 
	 * connection ready. */

	secure = 1;
	ns = configure_pr_socket( &newsocket, secure );

	/*
	ber_sockbuf_set_option( conn->c_sb, LBER_SOCKBUF_OPT_DESC, &newsocket );
	ber_sockbuf_set_option( conn->c_sb, LBER_SOCKBUF_OPT_READ_FN, (void *)secure_read_function );
	ber_sockbuf_set_option( conn->c_sb, LBER_SOCKBUF_OPT_WRITE_FN, (void *)secure_write_function );
	*/

	/*changed to */
	{
	struct lber_x_ext_io_fns *func_pointers = malloc(LBER_X_EXTIO_FNS_SIZE);
		func_pointers->lbextiofn_size = LBER_X_EXTIO_FNS_SIZE;
		func_pointers->lbextiofn_read = secure_read_function;
		func_pointers->lbextiofn_write = secure_write_function;
		func_pointers->lbextiofn_writev = NULL;
		func_pointers->lbextiofn_socket_arg = (struct lextiof_socket_private *) newsocket;
		ber_sockbuf_set_option( conn->c_sb,
			LBER_SOCKBUF_OPT_EXT_IO_FNS, func_pointers);
		free(func_pointers);
	}	
	conn->c_flags |= CONN_FLAG_SSL;
	conn->c_flags |= CONN_FLAG_START_TLS;
	conn->c_sd = ns;
	conn->c_prfd = newsocket;

	
	rv = slapd_ssl_handshakeCallback (conn->c_prfd, (void *)handle_handshake_done, conn);

	if ( rv < 0 ) {
	       PRErrorCode prerr = PR_GetError();
	       slapi_log_error( SLAPI_LOG_FATAL, "start_tls", 
			  "SSL_HandshakeCallback() %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
			  rv, prerr, slapd_pr_strerror( prerr ) );
	}
	
	if ( config_get_SSLclientAuth() != SLAPD_SSLCLIENTAUTH_OFF ) {
	       rv = slapd_ssl_badCertHook (conn->c_prfd, (void *)handle_bad_certificate, conn);
	       if ( rv < 0 ) {
		        PRErrorCode prerr = PR_GetError();
			slapi_log_error( SLAPI_LOG_FATAL, "start_tls", 
					 "SSL_BadCertHook(%i) %i " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
					 conn->c_sd, rv, prerr, slapd_pr_strerror( prerr ) );
	       }
	}
	
	
	/* Once agreed in starting TLS, the handshake must be carried out. */
	
	slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
			 "Starting SSL Handshake.\n" );

 unlock_and_return:
	PR_Unlock( conn->c_mutex );

	return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );	

}/* start_tls */


/* TLS Graceful Closure function.
 * The function below kind of "resets" the connection to its state previous
 * to receiving the Start TLS operation request. But it also sets the
 * authorization and authentication credentials to "anonymous". Finally,
 * it sends a closure alert message to the client.
 *
 * Note: start_tls_graceful_closure() must be called with c->c_mutex locked.
 */
int
start_tls_graceful_closure( Connection *c, Slapi_PBlock * pb, int is_initiator )
{
	int ns;
	Slapi_PBlock *pblock = pb;
	struct slapdplugin  *plugin;
	int secure = 0;
	PRFileDesc *ssl_fd;

	if ( pblock == NULL ) {
	       pblock = slapi_pblock_new();
	       plugin = (struct slapdplugin *) slapi_ch_calloc( 1, sizeof( struct slapdplugin ) );
	       pblock->pb_plugin = plugin; 	       
	       pblock->pb_conn = c;
	       pblock->pb_op = c->c_ops;
	       set_db_default_result_handlers( pblock );
	       if ( slapi_pblock_set( pblock, SLAPI_EXT_OP_RET_OID, START_TLS_OID ) != 0 ) {
		       slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls", 
					"Could not set extended response oid.\n" );
		       slapi_send_ldap_result( pblock, LDAP_OPERATIONS_ERROR, NULL, 
					"Could not set extended response oid.", 0, NULL );
		       return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
	       }
	       slapi_ch_free( (void **) &plugin );
	}

	/* First thing to do is to finish with whatever operation may be hanging on the
	 * encrypted session.
	 */

	while ( connection_operations_pending( c, pblock->pb_op,
				0 /* wait for all other ops to full complete */ )) {
	  slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls",
			   "Still %d operations to be completed before closing the SSL connection.\n",
			   c->c_refcnt - 1 );
	}

	slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls_graceful_closure", "SSL_CLOSE_NOTIFY_ALERT\n" );

	/* An SSL close_notify alert should be sent to the client. However, the NSS API
	 * doesn't provide us with anything alike.
	 */
	slapi_send_ldap_result( pblock, LDAP_OPERATIONS_ERROR, NULL, 
				"SSL_CLOSE_NOTIFY_ALERT", 0, NULL );
	

	if ( is_initiator ) {
	  /* if this call belongs to the initiator of the SSL connection closure, it must first
	   * wait for the peer to send another close_notify alert back.
	   */
	}


	PR_Lock( c->c_mutex );

	/* "Unimport" the socket from SSL, i.e. get rid of the upper layer of the 
	 * file descriptor stack, which represents SSL. 
	 * The ssl socket assigned to c->c_prfd should also be closed and destroyed. 
	 * Should find a way of getting that ssl socket.
	 */ 
	/*	
	rc = strcasecmp( "SSL", PR_GetNameForIdentity( c->c_prfd->identity ) );
	if ( rc == 0 ) {
	  sslSocket * ssl_socket;

	  ssl_socket = (sslSocket *) c->c_prfd->secret;
	  ssl_socket->fd = NULL;
	}
	*/
	ssl_fd = PR_PopIOLayer( c->c_prfd, PR_TOP_IO_LAYER );
	ssl_fd->dtor( ssl_fd );


#ifndef _WIN32
	secure = 0;
	ns = configure_pr_socket( &(c->c_prfd), secure );

	ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_DESC, &(c->c_prfd) );

#else
	ns = PR_FileDesc2NativeHandle( c->c_prfd );
	c->c_prfd = NULL;

	configure_ns_socket( &ns );

	ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_DESC, &ns );	

#endif

	c->c_sd = ns;
        c->c_flags &= ~CONN_FLAG_SSL;
        c->c_flags &= ~CONN_FLAG_START_TLS;

	ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_READ_FN, (void *)read_function );
	ber_sockbuf_set_option( c->c_sb, LBER_SOCKBUF_OPT_WRITE_FN, (void *)write_function );


	/*  authentication & authorization credentials must be set to "anonymous". */

	bind_credentials_clear( c, PR_FALSE, PR_TRUE );

	PR_Unlock( c->c_mutex );



	return ( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );
}    


static char *start_tls_oid_list[] = {
	START_TLS_OID,
	NULL
};
static char *start_tls_name_list[] = {
	"startTLS",
	NULL
};

int start_tls_register_plugin()
{
	slapi_register_plugin( "extendedop", 1 /* Enabled */, "start_tls_init", 
			start_tls_init, "Start TLS extended operation",
			start_tls_oid_list, NULL );

	return 0;
}


/* Initialization function */

int start_tls_init( Slapi_PBlock *pb )
{
	char	**argv;
	char	*oid;

	/* Get the arguments appended to the plugin extendedop directive. The first argument 
	 * (after the standard arguments for the directive) should contain the OID of the
	 * extended operation.
	 */ 

	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls_init", "Could not get argv\n" );
		return( -1 );
	}

	/* Compare the OID specified in the configuration file against the Start TLS OID. */

	if ( argv == NULL || strcmp( argv[0], START_TLS_OID ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls_init", 
				 "OID is missing or is not %s\n", START_TLS_OID );
		return( -1 );
	} else {
		oid = slapi_ch_strdup( argv[0] );
		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls_init", 
				 "Registering plug-in for Start TLS extended op %s.\n", oid );
	}

	/* Register the plug-in function as an extended operation
	 * plug-in function that handles the operation identified by
	 * OID 1.3.6.1.4.1.1466.20037.  Also specify the version of the server 
	 * plug-in */ 
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	     slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&exopdesc ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *) start_tls ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, start_tls_oid_list ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, start_tls_name_list ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "start_tls_init",
				 "Failed to set plug-in version, function, and OID.\n" );
		return( -1 );
	}
	
	return( 0 );
}

