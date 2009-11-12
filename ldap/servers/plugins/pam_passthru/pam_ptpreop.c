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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * pamptpreop.c - bind pre-operation plugin for Pass Through Authentication to PAM
 *
 */

#include "pam_passthru.h"

static Slapi_PluginDesc pdesc = { "pam_passthruauth",  VENDOR, DS_PACKAGE_VERSION,
	"PAM pass through authentication plugin" };

static void * pam_passthruauth_plugin_identity = NULL;

/*
 * function prototypes
 */
static int pam_passthru_bindpreop( Slapi_PBlock *pb );
static int pam_passthru_bindpreop_start( Slapi_PBlock *pb );
static int pam_passthru_bindpreop_close( Slapi_PBlock *pb );


/*
** Plugin identity mgmt
*/

void pam_passthruauth_set_plugin_identity(void * identity) 
{
	pam_passthruauth_plugin_identity=identity;
}

void * pam_passthruauth_get_plugin_identity()
{
	return pam_passthruauth_plugin_identity;
}

/*
 * Plugin initialization function (which must be listed in the appropriate
 * slapd config file).
 */
int
pam_passthruauth_init( Slapi_PBlock *pb )
{
    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> pam_passthruauth_init\n" );

    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &pam_passthruauth_plugin_identity);
    PR_ASSERT (pam_passthruauth_plugin_identity);

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
		    (void *)SLAPI_PLUGIN_VERSION_01 ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
		    (void *)&pdesc ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
		    (void *)pam_passthru_bindpreop_start ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN,
		    (void *)pam_passthru_bindpreop ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN,
		    (void *)pam_passthru_bindpreop_close ) != 0  ) {
	slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
		"pam_passthruauth_init failed\n" );
	return( -1 );
    }

    slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	"<= pam_passthruauth_init succeeded\n" );

    return( 0 );
}

/*
 * pam_passthru_bindpreop_start() is called before the directory server
 * is fully up.  We parse our configuration and initialize any mutexes, etc.
 */
static int
pam_passthru_bindpreop_start( Slapi_PBlock *pb )
{
	int rc;
	Slapi_Entry *config_e = NULL; /* entry containing plugin config */

    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> pam_passthru_bindpreop_start\n" );

    if ( slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &config_e ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "missing config entry\n" );
		return( -1 );
    }

    if (( rc = pam_passthru_config( config_e )) != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "configuration failed (%s)\n", ldap_err2string( rc ));
		return( -1 );
    }

    if (( rc = pam_passthru_pam_init()) != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "could not initialize PAM subsystem (%d)\n", rc);
		return( -1 );
    }

    return( 0 );
}


/*
 * Called right before the Directory Server shuts down.
 */
static int
pam_passthru_bindpreop_close( Slapi_PBlock *pb )
{
    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> pam_passthru_bindpreop_close\n" );

    return( 0 );
}


static int
pam_passthru_bindpreop( Slapi_PBlock *pb )
{
    int rc, method;
    char *normbinddn, *errmsg = NULL;
    Pam_PassthruConfig	*cfg;
    struct berval	*creds;
	int retcode = PAM_PASSTHRU_OP_NOT_HANDLED;

    PAM_PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> pam_passthru_bindpreop\n" );

    /*
     * retrieve parameters for bind operation
     */
    if ( slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ||
		 slapi_pblock_get( pb, SLAPI_BIND_TARGET, &normbinddn ) != 0 ||
		 slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &creds ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "<= not handled (unable to retrieve bind parameters)\n" );
		return retcode;
    }

    /*
     * We only handle simple bind requests that include non-NULL binddn and
     * credentials.  Let the Directory Server itself handle everything else.
     */
    if ( method != LDAP_AUTH_SIMPLE || *normbinddn == '\0' ||
		 creds->bv_len == 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "<= not handled (not simple bind or NULL dn/credentials)\n" );
		return retcode;
    }

	/* get the config */
	cfg = pam_passthru_get_config();

	/* don't lock mutex here - simple integer access - assume atomic */
	if (cfg->pamptconfig_secure) { /* is a secure connection required? */
		int is_ssl = 0;
		slapi_pblock_get(pb, SLAPI_CONN_IS_SSL_SESSION, &is_ssl);
		if (!is_ssl) {
			slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							 "<= connection not secure (secure connection required; check config)");
			return retcode;
		}
	}

    /*
     * Check to see if the target DN is one we should "pass through" to
     * PAM
     */
    if ( pam_passthru_check_suffix( cfg, normbinddn ) != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						 "<= not handled (not one of our suffixes)\n" );
		return retcode;
    }

    /*
     * We are now committed to handling this bind request.
     * Chain it off to PAM
     */
	rc = pam_passthru_do_pam_auth(pb, cfg);

    /*
     * If bind succeeded, change authentication information associated
     * with this connection.
     */
    if (rc == LDAP_SUCCESS) {
        char *ndn = slapi_ch_strdup(normbinddn);
        if ((slapi_pblock_set(pb, SLAPI_CONN_DN, ndn) != 0) ||
			(slapi_pblock_set(pb, SLAPI_CONN_AUTHMETHOD,
							  SLAPD_AUTH_SIMPLE) != 0)) {
            slapi_ch_free_string(&ndn);
            rc = LDAP_OPERATIONS_ERROR;
            errmsg = "unable to set connection DN or AUTHTYPE";
            slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"%s\n", errmsg);
        } else {
			LDAPControl **reqctrls = NULL;
			slapi_pblock_get(pb, SLAPI_REQCONTROLS, &reqctrls);
			if (slapi_control_present(reqctrls, LDAP_CONTROL_AUTH_REQUEST, NULL, NULL)) {
				slapi_add_auth_response_control(pb, ndn);
			}
		}
    }

	if (rc == LDAP_SUCCESS) {
		/* we are handling the result */
		slapi_send_ldap_result(pb, rc, NULL, errmsg, 0, NULL);
		/* tell bind code we handled the result */
		retcode = PAM_PASSTHRU_OP_HANDLED;
	} else if (!cfg->pamptconfig_fallback) {
		/* tell bind code we already sent back the error result in pam_ptimpl.c */
		retcode = PAM_PASSTHRU_OP_HANDLED;
	}

    slapi_log_error(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
					"<= handled (error %d - %s)\n", rc, ldap_err2string(rc));

    return retcode;
}
