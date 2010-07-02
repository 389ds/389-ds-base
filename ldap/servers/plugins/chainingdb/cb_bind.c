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

#include "cb.h"

static void
cb_free_bervals( struct berval **bvs );


static int
cb_sasl_bind_once_s( cb_conn_pool *pool, char *dn, int method, char * mechanism,
        struct berval *creds, LDAPControl **reqctrls,
        char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
        LDAPControl ***resctrlsp , int * status);

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

static int
cb_sasl_bind_s(Slapi_PBlock * pb, cb_conn_pool *pool, int tries,
        char *dn, int method,char * mechanism, struct berval *creds, LDAPControl **reqctrls,
        char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
        LDAPControl ***resctrlsp ,int *status) {

    int         rc;
 
    do {
         /* check to see if operation has been abandoned...*/

	if (LDAP_AUTH_SIMPLE!=method)
		return LDAP_AUTH_METHOD_NOT_SUPPORTED;

        if ( slapi_op_abandoned( pb )) {
            rc = LDAP_USER_CANCELLED;
        } else {
            rc = cb_sasl_bind_once_s( pool, dn, method,mechanism, creds, reqctrls,
                     matcheddnp, errmsgp, refurlsp, resctrlsp ,status);
        }
    } while ( CB_LDAP_CONN_ERROR( rc ) && --tries > 0 );
       
    return( rc );
}

static int
cb_sasl_bind_once_s( cb_conn_pool *pool, char *dn, int method, char * mechanism,
        struct berval *creds, LDAPControl **reqctrls,
        char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
        LDAPControl ***resctrlsp , int * status) {

    int                 rc, msgid;
    char                **referrals;
    struct timeval      timeout_copy, *timeout;
    LDAPMessage         *result=NULL;
    LDAP                *ld=NULL;
    char 		*cnxerrbuf=NULL;
    cb_outgoing_conn	*cnx;
    int version=LDAP_VERSION3;
	
    /* Grab an LDAP connection to use for this bind. */

    PR_RWLock_Rlock(pool->rwl_config_lock);
    timeout_copy.tv_sec = pool->conn.bind_timeout.tv_sec;
    timeout_copy.tv_usec = pool->conn.bind_timeout.tv_usec;
    PR_RWLock_Unlock(pool->rwl_config_lock);

    if (( rc = cb_get_connection( pool, &ld ,&cnx, NULL, &cnxerrbuf)) != LDAP_SUCCESS ) {
	*errmsgp=cnxerrbuf;
        goto release_and_return;
    }
       
    /* Send the bind operation (need to retry on LDAP_SERVER_DOWN) */
    
    ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version );

    if (( rc = ldap_sasl_bind( ld, dn, LDAP_SASL_SIMPLE, creds, reqctrls,
                NULL, &msgid )) != LDAP_SUCCESS ) {
        goto release_and_return;
    }

	/* XXXSD what is the exact semantics of bind_to ? it is used to get a
	connection handle and later to bind ==> bind op may last 2*bind_to
	from the user point of view 
	confusion comes from teh fact that bind to is used 2for 3 differnt thinks,	
	*/

    /*
     * determine timeout value (how long we will wait for a response)
     * if timeout is zero'd, we wait indefinitely.
     */
    if ( timeout_copy.tv_sec == 0 && timeout_copy.tv_usec == 0 ) {
        timeout = NULL;
    } else {
	timeout = &timeout_copy;
    }
       
    /*
     * Wait for a result.
     */
    rc = ldap_result( ld, msgid, 1, timeout, &result );
 
    /*
     * Interpret the result.
     */

   if ( rc == 0 ) {            /* timeout */
        /*
         * Timed out waiting for a reply from the server.
         */
        rc = LDAP_TIMEOUT;
    } else if ( rc < 0 ) {

        /* Some other error occurred (no result received). */
	char * matcheddnp2, * errmsgp2;
	matcheddnp2=errmsgp2=NULL;

	rc = slapi_ldap_get_lderrno( ld, &matcheddnp2, &errmsgp2 );

	/* Need to allocate errmsgs */
	if (matcheddnp2)
		*matcheddnp=slapi_ch_strdup(matcheddnp2);
	if (errmsgp2)
		*errmsgp=slapi_ch_strdup(errmsgp2);
	
	if ( LDAP_SUCCESS != rc )  {
        	slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
			"cb_sasl_bind_once_s failed (%s)\n",ldap_err2string(rc));
	}
    } else {

        /* Got a result from remote server -- parse it.*/

	char * matcheddnp2, * errmsgp2;
	matcheddnp2=errmsgp2=NULL;
	*resctrlsp=NULL;
        rc = ldap_parse_result( ld, result, status, &matcheddnp2, &errmsgp2,
                &referrals, resctrlsp, 1 );
        if ( referrals != NULL ) {
            *refurlsp = referrals2berval( referrals );
            slapi_ldap_value_free( referrals );
        }
	/* realloc matcheddn & errmsg because the mem alloc model */
	/* may differ from malloc				  */
	if (matcheddnp2) {
		*matcheddnp=slapi_ch_strdup(matcheddnp2);
		ldap_memfree(matcheddnp2);
	}
	if (errmsgp2) {
		*errmsgp=slapi_ch_strdup(errmsgp2);
		ldap_memfree(errmsgp2);
	}

    }

release_and_return:
    if ( ld != NULL ) {
        cb_release_op_connection( pool, ld, CB_LDAP_CONN_ERROR( rc ));
    }
       
    return( rc );
}

int
chainingdb_bind( Slapi_PBlock *pb ) {

	int 			status=LDAP_SUCCESS;
	int 			allocated_errmsg;
	int 			rc=LDAP_SUCCESS;
	cb_backend_instance 	*cb;
	Slapi_Backend		*be;
	char                    *dn;
        int                     method;
        struct berval           *creds, **urls;
	char 			*matcheddn,*errmsg;
    	LDAPControl         	**reqctrls, **resctrls, **ctrls;
	char 			* mechanism;
	int 			freectrls=1;
	int 			bind_retry;
	
        if ( LDAP_SUCCESS != (rc = cb_forward_operation(pb) )) {
        	cb_send_ldap_result( pb, rc, NULL, "Chaining forbidden", 0, NULL );
                return SLAPI_BIND_FAIL;
        }

	ctrls=NULL;
	/* don't add proxy auth control. use this call to check for supported   */
	/* controls only.							*/
        if ( LDAP_SUCCESS != ( rc = cb_update_controls( pb, NULL, &ctrls, 0 )) ) {
                cb_send_ldap_result( pb, rc, NULL, NULL, 0, NULL );
		if (ctrls)
			ldap_controls_free(ctrls);
                return SLAPI_BIND_FAIL;
        }
	if (ctrls)
		ldap_controls_free(ctrls);

        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
        slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn );
        slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method );
	slapi_pblock_get( pb, SLAPI_BIND_SASLMECHANISM, &mechanism);
        slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &creds );
        slapi_pblock_get( pb, SLAPI_REQCONTROLS, &reqctrls );
        cb = cb_get_instance(be);

	if ( NULL == dn ) 
		dn="";

        /* always allow noauth simple binds */
        if (( method == LDAP_AUTH_SIMPLE) && creds->bv_len == 0 ) {
                return( SLAPI_BIND_ANONYMOUS );
        }

        cb_update_monitor_info(pb,cb,SLAPI_OPERATION_BIND);

	matcheddn=errmsg=NULL;
    	allocated_errmsg = 0;
	resctrls=NULL;
	urls=NULL;

	/* Check wether the chaining BE is available or not */
        if ( cb_check_availability( cb, pb ) == FARMSERVER_UNAVAILABLE ){
	  return -1;
        }

        PR_RWLock_Rlock(cb->rwl_config_lock);
	bind_retry=cb->bind_retry;
        PR_RWLock_Unlock(cb->rwl_config_lock);

	if ( LDAP_SUCCESS == (rc = cb_sasl_bind_s(pb, cb->bind_pool, bind_retry, dn,method,mechanism,
		creds,reqctrls,&matcheddn,&errmsg,&urls,&resctrls, &status))) {
        	rc = status;
            	allocated_errmsg = 1;
	} else
	if ( LDAP_USER_CANCELLED != rc ) {
   		errmsg = ldap_err2string( rc );
		if (rc == LDAP_TIMEOUT) {
		  cb_ping_farm(cb,NULL,0);
		}
            	rc = LDAP_OPERATIONS_ERROR;
	}

 	if ( rc != LDAP_USER_CANCELLED ) {  /* not abandoned */
        	if ( resctrls != NULL ) {
            		slapi_pblock_set( pb, SLAPI_RESCONTROLS, resctrls );
			freectrls=0;
        	}

		if ( rc != LDAP_SUCCESS ) {
        		cb_send_ldap_result( pb, rc, matcheddn, errmsg, 0, urls );
		}
    	}

    	if ( urls != NULL ) {
        	cb_free_bervals( urls );
    	}
    	if ( freectrls && ( resctrls != NULL )) {
        	ldap_controls_free( resctrls );
    	}
        slapi_ch_free((void **)& matcheddn );    	
    	if ( allocated_errmsg && errmsg != NULL ) {
        	slapi_ch_free((void **)& errmsg );
    	}

	return ((rc == LDAP_SUCCESS ) ? SLAPI_BIND_SUCCESS : SLAPI_BIND_FAIL );
}

static void
cb_free_bervals( struct berval **bvs )
{
    int         i;

    if ( bvs != NULL ) {
        for ( i = 0; bvs[ i ] != NULL; ++i ) {
            slapi_ch_free( (void **)&bvs[ i ] );
        }
    }    
    slapi_ch_free( (void **)&bvs );
}

