/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"

/*
 * Perform an add operation
 *
 * Returns:
 *   0  - success
 *   <0 - fail
 *
 */

int
chaining_back_add ( Slapi_PBlock *pb )
{

	Slapi_Backend 		*be;
	Slapi_Entry		*e;
	cb_backend_instance 	*cb;
	LDAPControl 		**serverctrls=NULL;
	LDAPControl 		**ctrls=NULL;
	int 			rc,parse_rc,msgid,i;
	LDAP 			*ld=NULL;
	char         		**referrals=NULL;
	LDAPMod			** mods;
	LDAPMessage		* res;
	char 			*dn,* matched_msg, *error_msg;
	char			*cnxerrbuf=NULL;
   	time_t 			endtime;
	cb_outgoing_conn	*cnx;
	
	if ( (rc=cb_forward_operation(pb)) != LDAP_SUCCESS ) {
               	cb_send_ldap_result( pb, rc, NULL, "Remote data access disabled", 0, NULL );
		return -1;
	}

        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	cb = cb_get_instance(be);

	/* Update monitor info */
	cb_update_monitor_info(pb,cb,SLAPI_OPERATION_ADD);

	/* Check wether the chaining BE is available or not */
        if ( cb_check_availability( cb, pb ) == FARMSERVER_UNAVAILABLE ){
	  return -1;
        }


 	slapi_pblock_get( pb, SLAPI_ADD_TARGET, &dn );
        slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e );

	/* Check local access controls */
	if (cb->local_acl && !cb->associated_be_is_disabled) {
		char * errbuf=NULL;
        	rc = cb_access_allowed (pb, e, NULL, NULL, SLAPI_ACL_ADD, &errbuf);
   		if ( rc != LDAP_SUCCESS ) {
                	cb_send_ldap_result( pb, rc, NULL, errbuf, 0, NULL );
                	slapi_ch_free((void **)&errbuf);
			return -1;
		}
        }

	/* Build LDAPMod from the SLapi_Entry */
	cb_eliminate_illegal_attributes(cb,e);

	if ((rc = slapi_entry2mods ((const Slapi_Entry *)e, NULL, &mods)) != LDAP_SUCCESS) {
                cb_send_ldap_result( pb, rc,NULL,NULL, 0, NULL);
                return -1;
	}

	/* Grab a connection handle */
	if ((rc = cb_get_connection(cb->pool,&ld,&cnx,NULL,&cnxerrbuf)) != LDAP_SUCCESS) {
                cb_send_ldap_result( pb, LDAP_OPERATIONS_ERROR,NULL,cnxerrbuf, 0, NULL);
		ldap_mods_free(mods,1);
		if (cnxerrbuf) {
		  PR_smprintf_free(cnxerrbuf);
		}
                /* ping the farm. If the farm is unreachable, we increment the counter */
                cb_ping_farm(cb,NULL,0);

                return -1;
	}
	
	/* Control management */
	if ( (rc = cb_update_controls( pb,ld,&ctrls,CB_UPDATE_CONTROLS_ADDAUTH)) != LDAP_SUCCESS ) {
                cb_send_ldap_result( pb, rc, NULL,NULL, 0, NULL);
		cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(rc));
		ldap_mods_free(mods,1);
                return -1;
	}

        if ( slapi_op_abandoned( pb )) {
                cb_release_op_connection(cb->pool,ld,0);
		ldap_mods_free(mods,1);
		if ( NULL != ctrls)
			ldap_controls_free(ctrls);
                return -1;
        }

	/* heart-beat management */
	if (cb->max_idle_time>0)
        	endtime=current_time() + cb->max_idle_time;

	/* Send LDAP operation to the remote host */
	rc = ldap_add_ext( ld, dn, mods, ctrls, NULL, &msgid );
	
	if ( NULL != ctrls)
		ldap_controls_free(ctrls);

	if ( rc != LDAP_SUCCESS ) {

                cb_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
                        ldap_err2string(rc), 0, NULL);
		cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(rc));
		ldap_mods_free(mods,1);
                return -1;
	}

	/* 
	 * Poll the server for the results of the add operation.
	 * Check for abandoned operation regularly.
	 */

	while ( 1 ) {

		if (cb_check_forward_abandon(cb,pb,ld,msgid)) {
			/* connection handle released in cb_check_forward_abandon() */
			ldap_mods_free(mods,1);
			return -1;
		}

   		rc = ldap_result( ld, msgid, 0, &cb->abandon_timeout, &res );
   		switch ( rc ) {
   		case -1:
                	cb_send_ldap_result(pb,LDAP_OPERATIONS_ERROR, NULL,
				ldap_err2string(rc), 0, NULL);
			cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(rc));
			ldap_mods_free(mods,1);
			if (res)
				ldap_msgfree(res);
                	return -1;
		case 0:

			if ((rc=cb_ping_farm(cb,cnx,endtime)) != LDAP_SUCCESS) {

				/* does not respond. give up and return a*/
				/* error to the client.			 */

               			/*cb_send_ldap_result(pb,LDAP_OPERATIONS_ERROR, NULL,
					ldap_err2string(rc), 0, NULL);*/
				cb_send_ldap_result(pb,LDAP_OPERATIONS_ERROR, NULL,     "FARM SERVER TEMPORARY UNAVAILABLE", 0, NULL);
				cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(rc));
				ldap_mods_free(mods,1);
				if (res)
					ldap_msgfree(res);
               			return -1;
			}
#ifdef CB_YIELD
			DS_Sleep(PR_INTERVAL_NO_WAIT);
#endif
			break;
		default:
			serverctrls=NULL;
			matched_msg=error_msg=NULL;
			referrals=NULL;

			parse_rc = ldap_parse_result( ld, res, &rc, &matched_msg, 
         			&error_msg, &referrals, &serverctrls, 1 );

      			if ( parse_rc != LDAP_SUCCESS ) {
                		cb_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
                        		ldap_err2string(parse_rc), 0, NULL);
				cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(parse_rc));
				ldap_mods_free(mods,1);
			       	slapi_ch_free((void **)&matched_msg);
				slapi_ch_free((void **)&error_msg);
				if (serverctrls)
	                                ldap_controls_free(serverctrls);
				/* jarnou: free referrals */
                                if (referrals)
                                        charray_free(referrals);
                		return -1;
			}

      			if ( rc != LDAP_SUCCESS ) {
				struct berval ** refs =  referrals2berval(referrals); 
                        	cb_send_ldap_result( pb, rc, matched_msg, error_msg, 0, refs);
				cb_release_op_connection(cb->pool,ld,CB_LDAP_CONN_ERROR(rc));
				ldap_mods_free(mods,1);
				slapi_ch_free((void **)&matched_msg);
			       	slapi_ch_free((void **)&error_msg);
				if (refs) 
					ber_bvecfree(refs);
				if (referrals) 
					charray_free(referrals);
				if (serverctrls)
	                                ldap_controls_free(serverctrls);
                		return -1;
			}

			ldap_mods_free(mods,1 );
			cb_release_op_connection(cb->pool,ld,0);

			/* Add control response sent by the farm server */

			for (i=0; serverctrls && serverctrls[i];i++)
        			slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, serverctrls[i]);
			if (serverctrls)
	                	ldap_controls_free(serverctrls);
			/* jarnou: free matched_msg, error_msg, and referrals if necessary */
		 	slapi_ch_free((void **)&matched_msg);
		     	slapi_ch_free((void **)&error_msg);
		       	if (referrals) 
		       		charray_free(referrals);
        		cb_send_ldap_result( pb, LDAP_SUCCESS, NULL, NULL, 0, NULL );

			slapi_entry_free(e);
        		slapi_pblock_set( pb, SLAPI_ADD_ENTRY, NULL );

			return 0;
		}
	}

	/* Never reached */
}
