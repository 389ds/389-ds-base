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

/*
** Controls that can't be forwarded due to the current implementation 
*/

static char * unsupported_ctrls[] = {LDAP_CONTROL_PERSISTENTSEARCH,NULL};

int cb_is_control_forwardable(cb_backend * cb, char *controloid) {
    return (!(charray_inlist(unsupported_ctrls,controloid)));
}

void
cb_register_supported_control( cb_backend * cb, char *controloid, unsigned long controlops )
{
    /* For now, ignore controlops */
    if ( controloid != NULL ) {
        slapi_rwlock_wrlock(cb->config.rwl_config_lock);
                   charray_add( &cb->config.forward_ctrls,slapi_ch_strdup( controloid ));
        slapi_rwlock_unlock(cb->config.rwl_config_lock);
        }
}


void
cb_unregister_all_supported_control( cb_backend * cb ) {

    slapi_rwlock_wrlock(cb->config.rwl_config_lock);
    charray_free(cb->config.forward_ctrls);
    cb->config.forward_ctrls=NULL;
    slapi_rwlock_unlock(cb->config.rwl_config_lock);
}

void
cb_unregister_supported_control( cb_backend * cb, char *controloid, unsigned long controlops )
{

    /* For now, ignore controlops */
    if ( controloid != NULL ) {
        int i;
        slapi_rwlock_wrlock(cb->config.rwl_config_lock);
        for ( i = 0; cb->config.forward_ctrls != NULL && cb->config.forward_ctrls[i] != NULL; ++i ) {
            if ( strcmp( cb->config.forward_ctrls[i], controloid ) == 0 ) {
                break;
            }
        }
        if ( cb->config.forward_ctrls == NULL || cb->config.forward_ctrls[i] == NULL) {
            slapi_rwlock_unlock(cb->config.rwl_config_lock);
            return;
        }
        if ( controlops == 0 ) {
            charray_remove(cb->config.forward_ctrls,controloid,0/* free it */);
        }
        slapi_rwlock_unlock(cb->config.rwl_config_lock);
    }
}

int cb_create_loop_control (
     const ber_int_t hops,
     LDAPControl **ctrlp)

{
    BerElement      *ber;
    int             rc;

    if ((ber = ber_alloc()) == NULL)
        return -1;

    if ( ber_printf( ber, "i", hops ) < 0) {
        ber_free(ber,1);
        return -1;
    }

    rc = slapi_build_control( CB_LDAP_CONTROL_CHAIN_SERVER, ber, 0, ctrlp);

    ber_free(ber,1);

    return rc;
}

/*
** Return the controls to be passed to the remote 
** farm server and the LDAP error to return.
**
** Add the Proxied Authorization control when impersonation
** is enabled. Other controls present in the request are added 
** to the control list
**
** #622885 .abandon should not inherit the to-be-abandoned-operation's controls
**         .controls attached to abandon should not be critical
*/

int cb_update_controls( Slapi_PBlock * pb, 
                        LDAP         * ld,
                        LDAPControl  *** controls,
                        int          ctrl_flags
                      )
{

    int cCount=0;
    int dCount=0;
    int i;
    char * proxyDN=NULL;
    LDAPControl ** reqControls = NULL;
    LDAPControl ** ctrls = NULL;
    cb_backend_instance  * cb;
    cb_backend           * cbb;
    Slapi_Backend        * be;
    int rc=LDAP_SUCCESS;
    ber_int_t hops=0;
    int useloop=0;
    int addauth = (ctrl_flags & CB_UPDATE_CONTROLS_ADDAUTH);
    int isabandon = (ctrl_flags & CB_UPDATE_CONTROLS_ISABANDON);
    int op_type = 0;

    *controls = NULL;
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);
    if (!isabandon || op_type == SLAPI_OPERATION_ABANDON) {
        /* if not abandon or abandon sent by client */
        slapi_pblock_get( pb, SLAPI_REQCONTROLS, &reqControls );
    }
    slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &cbb );
    cb = cb_get_instance(be);

    /*****************************************/
    /* First, check for unsupported controls */
    /* Return an error if critical control   */
    /* else remove it from the control list  */
    /*****************************************/

    for ( cCount=0; reqControls && reqControls[cCount]; cCount++ );
    ctrls = (LDAPControl **)slapi_ch_calloc(1,sizeof(LDAPControl *) * (cCount +3));

    slapi_rwlock_rdlock(cbb->config.rwl_config_lock);

    for ( cCount=0; reqControls && reqControls[cCount]; cCount++ ) {

        /* XXXSD CASCADING */
        /* For now, allow PROXY_AUTH control forwarding only when       */
        /* local acl evaluation to prevent unauthorized access          */

        if (!strcmp(reqControls[cCount]->ldctl_oid,LDAP_CONTROL_PROXYAUTH)) {

            /* we have to force remote acl checking if the associated backend to this
            chaining backend is disabled - disabled == no acl check possible */
            if (!cb->local_acl && !cb->associated_be_is_disabled) {
                slapi_log_error( SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                    "local aci check required to handle proxied auth control. Deny access.\n");
                    rc= LDAP_INSUFFICIENT_ACCESS;
                break;
            }

            /* XXXSD Not safe to use proxied authorization with Directory Manager */
            /* checked earlier when impersonation is on                           */

            if (!cb->impersonate) {
                char * requestor,*rootdn;
                char * requestorCopy=NULL;

                rootdn=cb_get_rootdn();
                slapi_pblock_get( pb, SLAPI_REQUESTOR_DN, &requestor );
                requestorCopy=slapi_ch_strdup(requestor);
                slapi_dn_normalize_case(requestorCopy);

                if (!strcmp( requestorCopy, rootdn )) {    /* UTF8- aware */
                    slapi_log_error( SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                    "Use of user <%s> incompatible with proxied auth. control\n",rootdn);
                    rc=LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
                    slapi_ch_free((void **)&requestorCopy);
                    break;
                }
                slapi_ch_free((void **)&rootdn);
                slapi_ch_free((void **)&requestorCopy);
            }

            addauth=0;
            ctrls[dCount]=slapi_dup_control(reqControls[cCount]);
            dCount++;

        } else
            if (!strcmp(reqControls[cCount]->ldctl_oid,CB_LDAP_CONTROL_CHAIN_SERVER) &&
                reqControls[cCount]->ldctl_value.bv_val) {

            /* Max hop count reached ?                 */
            /* Checked earlier by a call to cb_forward_operation()  */

            BerElement      *ber = NULL;

            ber = ber_init(&(reqControls[cCount]->ldctl_value));
            if (LBER_ERROR == ber_scanf(ber,"i",&hops)) {
                slapi_log_error( SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                                 "Unable to get number of hops from the chaining control\n");
            }
            ber_free(ber,1);
            useloop=1;

            /* Add to the control list later */

        } else {

            int i;
            for ( i = 0; cbb->config.forward_ctrls != NULL
                && cbb->config.forward_ctrls[i] != NULL; ++i ) {
                if ( strcmp( cbb->config.forward_ctrls[i], reqControls[cCount]->ldctl_oid ) == 0 ) {
                    break;
                }
            }
            /* For now, ignore optype */
            if ( cbb->config.forward_ctrls == NULL || cbb->config.forward_ctrls[i] == NULL) {
                if (reqControls[cCount]->ldctl_iscritical) {
                    rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
                    break;
                }
                /* Skip it */
            } else {
                ctrls[dCount]=slapi_dup_control(reqControls[cCount]);
                dCount++;
            }
        }
    }

    slapi_rwlock_unlock(cbb->config.rwl_config_lock);

    if (LDAP_SUCCESS != rc) {
        ldap_controls_free(ctrls);
        return rc;
    }

    /***************************************/
    /* add impersonation control if needed */
    /***************************************/

    if ( !(cb->impersonate) ) {

        /* don't add proxy control */
        addauth=0;
    }
        
    if (addauth) {
        slapi_pblock_get( pb, SLAPI_REQUESTOR_DN, &proxyDN );

        if ( slapi_ldap_create_proxyauth_control(ld, proxyDN, isabandon?0:1, 0, &ctrls[dCount] )) {
            ldap_controls_free(ctrls);
                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                "LDAP_CONTROL_PROXYAUTH control encoding failed.\n");
            return LDAP_OPERATIONS_ERROR;
        }
        dCount++;
    }

    /***********************************************************/
    /* add loop control if needed                              */
    /* Don't add it if not in the list of forwardable controls */
    /***********************************************************/

    if (!useloop) {
        for ( i = 0; cbb->config.forward_ctrls != NULL
                && cbb->config.forward_ctrls[i] != NULL; ++i ) {
            if ( strcmp( cbb->config.forward_ctrls[i], 
                CB_LDAP_CONTROL_CHAIN_SERVER) == 0 ) {
                break;
            }
        }
    }
    if ( useloop || (cbb->config.forward_ctrls !=NULL && cbb->config.forward_ctrls[i] !=NULL)){
        
        if (hops > 0) {
            hops--;
        } else {
            hops = cb->hoplimit;
        }

        /* loop control's critical flag is 0; 
         * no special treatment is needed for abandon */
        cb_create_loop_control(hops,&ctrls[dCount]); 
        dCount++;
    }

    if (dCount==0) {
        ldap_controls_free(ctrls);
    } else {
        *controls = ctrls;
    }

    return LDAP_SUCCESS;

}
