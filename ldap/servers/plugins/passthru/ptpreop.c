/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ptpreop.c - bind pre-operation plugin for Pass Through Authentication
 *
 */

#include "passthru.h"

static Slapi_PluginDesc pdesc = { "passthruauth",  PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"pass through authentication plugin" };

/*
 * function prototypes
 */
static int passthru_bindpreop( Slapi_PBlock *pb );
static int passthru_bindpreop_start( Slapi_PBlock *pb );
static int passthru_bindpreop_close( Slapi_PBlock *pb );


/*
 * Plugin initialization function (which must be listed in the appropriate
 * slapd config file).
 */
int
passthruauth_init( Slapi_PBlock *pb )
{
    PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> passthruauth_init\n" );

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
		    (void *)SLAPI_PLUGIN_VERSION_01 ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
		    (void *)&pdesc ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN,
		    (void *)passthru_bindpreop_start ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN,
		    (void *)passthru_bindpreop ) != 0
	    || slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN,
		    (void *)passthru_bindpreop_close ) != 0  ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"passthruauth_init failed\n" );
	return( -1 );
    }

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	"<= passthruauth_init succeeded\n" );

    return( 0 );
}


/*
 * passthru_bindpreop_start() is called before the directory server
 * is fully up.  We parse our configuration and initialize any mutexes, etc.
 */
static int
passthru_bindpreop_start( Slapi_PBlock *pb )
{
    int		argc, rc;
    char	**argv;

    PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> passthru_bindpreop_start\n" );

    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"unable to get arguments\n" );
	return( -1 );
    }

    if (( rc = passthru_config( argc, argv )) != LDAP_SUCCESS ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"configuration failed (%s)\n", ldap_err2string( rc ));
	return( -1 );
    }

    return( 0 );
}


/*
 * Called right before the Directory Server shuts down.
 */
static int
passthru_bindpreop_close( Slapi_PBlock *pb )
{
    PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> passthru_bindpreop_close\n" );

    /*
     * close all our open connections.
     * XXXmcs: free any memory, mutexes, etc.
     */
    passthru_close_all_connections( passthru_get_config() );

    return( 0 );
}


static int
passthru_bindpreop( Slapi_PBlock *pb )
{
    int			rc, method;
    char		*normbinddn, *matcheddn;
    char		*libldap_errmsg, *pr_errmsg, *errmsg;
    PassThruConfig	*cfg;
    PassThruServer	*srvr;
    struct berval	*creds, **urls;
    LDAPControl		**reqctrls, **resctrls;

    PASSTHRU_ASSERT( pb != NULL );

    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "=> passthru_bindpreop\n" );

    /*
     * retrieve parameters for bind operation
     */
    if ( slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_TARGET, &normbinddn ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &creds ) != 0 ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"<= not handled (unable to retrieve bind parameters)\n" );
	return( PASSTHRU_OP_NOT_HANDLED );
    }
    if ( normbinddn == NULL ) {
	normbinddn = "";
    }

    /*
     * We only handle simple bind requests that include non-NULL binddn and
     * credentials.  Let the Directory Server itself handle everything else.
     */
    if ( method != LDAP_AUTH_SIMPLE || *normbinddn == '\0'
	    || creds->bv_len == 0 ) {
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"<= not handled (not simple bind or NULL dn/credentials)\n" );
	return( PASSTHRU_OP_NOT_HANDLED );
    }

    /*
     * Get pass through authentication configuration.
     */
    cfg = passthru_get_config();

    /*
     * Check to see if the target DN is one we should "pass through" to
     * another server.
     */
    if ( passthru_dn2server( cfg, normbinddn, &srvr ) != LDAP_SUCCESS ) {
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"<= not handled (not one of our suffixes)\n" );
	return( PASSTHRU_OP_NOT_HANDLED );
    }

    /*
     * We are now committed to handling this bind request.
     * Chain it off to another server.
     */
    matcheddn = errmsg = libldap_errmsg = pr_errmsg = NULL;
    urls = NULL;
    resctrls = NULL;
    if ( slapi_pblock_get( pb, SLAPI_REQCONTROLS, &reqctrls ) != 0 ) {
	rc = LDAP_OPERATIONS_ERROR;
	errmsg = "unable to retrieve bind controls";
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM, "%s\n",
		errmsg );
    } else {
	int	lderrno;

	if (( rc = passthru_simple_bind_s( pb, srvr, PASSTHRU_CONN_TRIES,
		normbinddn, creds, reqctrls, &lderrno, &matcheddn,
		&libldap_errmsg, &urls, &resctrls )) == LDAP_SUCCESS ) {
	    rc = lderrno;
	    errmsg = libldap_errmsg;
	} else if ( rc != LDAP_USER_CANCELLED ) {	/* not abandoned */
	    PRErrorCode	prerr = PR_GetError();
	    pr_errmsg = PR_smprintf( "error %d - %s %s ("
		    SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
		    rc, ldap_err2string( rc ), srvr->ptsrvr_url,
		    prerr, slapd_pr_strerror(prerr));
	    if ( NULL != pr_errmsg ) {
		errmsg = pr_errmsg;
	    } else {
		errmsg = ldap_err2string( rc );
	    }
	    rc = LDAP_OPERATIONS_ERROR;
	}
    }

    /*
     * If bind succeeded, change authentication information associated
     * with this connection.
     */
    if ( rc == LDAP_SUCCESS ) {
        char *ndn = slapi_ch_strdup( normbinddn );
        if (slapi_pblock_set(pb, SLAPI_CONN_DN, ndn) != 0 ||
	    slapi_pblock_set(pb, SLAPI_CONN_AUTHMETHOD,
                             SLAPD_AUTH_SIMPLE) != 0) {
            slapi_ch_free((void **)&ndn);
            rc = LDAP_OPERATIONS_ERROR;
            errmsg = "unable to set connection DN or AUTHTYPE";
            slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
                             "%s\n", errmsg );
        }
    }

    if ( rc != LDAP_USER_CANCELLED ) {	/* not abandoned */
	/*
	 * Send a result to our client.
	 */
	if ( resctrls != NULL ) {
	    (void)slapi_pblock_set( pb, SLAPI_RESCONTROLS, &resctrls );
	}
	slapi_send_ldap_result( pb, rc, matcheddn, errmsg, 0, urls );
    }

    /*
     * Clean up -- free allocated memory, etc.
     */
    if ( urls != NULL ) {
	passthru_free_bervals( urls );
    }
    if ( libldap_errmsg != NULL ) {
	ldap_memfree( errmsg );
    }
    if ( pr_errmsg != NULL ) {
	PR_smprintf_free( pr_errmsg );
    }
    if ( resctrls != NULL ) {
	ldap_controls_free( resctrls );
    }
    if ( matcheddn != NULL ) {
	ldap_memfree( matcheddn );
    }
 
    slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
	    "<= handled (error %d - %s)\n", rc, ldap_err2string( rc ));

    return( PASSTHRU_OP_HANDLED );
}
