/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ptconfig.c - configuration-related code for Pass Through Authentication
 *
 */

#include "passthru.h"

/*
 * Configuration is a bit complicated to fit into a single slapd config file
 * line, but for now that's how it works.  The format is:
 *
 *   plugin preoperation on PTA NSHOME/passthru-plugin.so passthruauth_init ARGS
 *
 * where each ARGS provides configuration for one host.  Each ARG should
 * be of the form:
 *
 *  "ldap://hosts/suffixes maxconns,maxconcurrency,timeout,ldver,connlifetime"
 * OR
 *  "ldaps://hosts/suffixes maxconns,maxconcurrency,timeout,ldver,connlifetime"
 *
 * where:
 *    hosts is a space-separated list of remote servers (with optional port
 *      numbers) to be used.  Each one is tried in order when opening an
 *      LDAP connection.
 *    suffixes is a semicolon separated list of DNs (if a DN contains a
 *	semicolon it must be represented \3B),
 *    maxconns is a limit on how many connections will be made,
 *    maxconcurrency is a limit on how many operations can share a connection, 
 *    timeout is a time limit in seconds for bind operations to complete (use
 *	0 to specify an infinite limit).
 *    ldver is the LDAP protocol version to use to talk to the server (2 or 3)
 *    connlifetime is a time limit time in seconds for a connection to be
 *      used before it is closed and reopened (use 0 to specify an infinite
 *      limit).  connlifetime can be omitted in which case a default value
 *	is used; this is for compatibility with DS 4.0 which did not support
 *      connlifetime.
 */


/*
 * function prototypes
 */ 

/*
 * static variables
 */
/* for now, there is only one configuration and it is global to the plugin  */
static PassThruConfig	theConfig;
static int		inited = 0;


/*
 * Read configuration and create a configuration data structure.
 * This is called after the server has configured itself so we can check
 *   for things like collisions between our suffixes and backend's suffixes.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well).
 * XXXmcs: this function leaks memory if any errors occur.
 */
int
passthru_config( int argc, char **argv )
{
    int			i, j, rc, tosecs, using_def_connlifetime;
    char		*p, **suffixarray;
    PassThruServer	*prevsrvr, *srvr;
    PassThruSuffix	*suffix, *prevsuffix;
    LDAPURLDesc		*ludp;

    if ( inited ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"only one pass through plugin instance can be used\n" );
	return( LDAP_PARAM_ERROR );
    }

    inited = 1;

    /*
     * It doesn't make sense to configure a pass through plugin without
     * providing at least one remote server.  Return an error if attempted.
     */
    if ( argc < 1 ) {
	slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"no pass through servers found in configuration"
		" (at least one must be listed)\n" );
	return( LDAP_PARAM_ERROR );
    }

    /*
     * Parse argv[] values.
     */
    prevsrvr = NULL;
    for ( i = 0; i < argc; ++i ) {
	srvr = (PassThruServer *)slapi_ch_calloc( 1, sizeof( PassThruServer ));
	srvr->ptsrvr_url = slapi_ch_strdup( argv[i] );

	if (( p = strchr( srvr->ptsrvr_url, ' ' )) == NULL ) {
	    /*
	     * use defaults for maxconnections, maxconcurrency, timeout,
	     * LDAP version, and connlifetime.
	     */
	    srvr->ptsrvr_maxconnections = PASSTHRU_DEF_SRVR_MAXCONNECTIONS;
	    srvr->ptsrvr_maxconcurrency = PASSTHRU_DEF_SRVR_MAXCONCURRENCY;
	    srvr->ptsrvr_timeout = (struct timeval *)slapi_ch_calloc( 1,
		    sizeof( struct timeval ));
	    srvr->ptsrvr_timeout->tv_sec = PASSTHRU_DEF_SRVR_TIMEOUT;
	    srvr->ptsrvr_ldapversion = PASSTHRU_DEF_SRVR_PROTOCOL_VERSION;
	    using_def_connlifetime = 1;
	} else {
	    /*
	     * parse parameters.  format is:
	     *     maxconnections,maxconcurrency,timeout,ldapversion
	     * OR  maxconnections,maxconcurrency,timeout,ldapversion,lifetime
	     */
	    *p++ = '\0';
	    rc = sscanf( p, "%d,%d,%d,%d,%d", &srvr->ptsrvr_maxconnections,
		    &srvr->ptsrvr_maxconcurrency, &tosecs,
		    &srvr->ptsrvr_ldapversion, &srvr->ptsrvr_connlifetime );
	    if ( rc < 4 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
			"server parameters should be in the form "
			"\"maxconnections,maxconcurrency,timeout,ldapversion,"
			"connlifetime\" (got \"%s\")\n", p );
		return( LDAP_PARAM_ERROR );
	    } else if ( rc < 5 ) {
		using_def_connlifetime = 1;
		srvr->ptsrvr_connlifetime = PASSTHRU_DEF_SRVR_CONNLIFETIME;
	    } else {
		using_def_connlifetime = 0;
	    }

	    if ( srvr->ptsrvr_ldapversion != LDAP_VERSION2
		    && srvr->ptsrvr_ldapversion != LDAP_VERSION3 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
			"LDAP protocol version should be %d or %d (got %d)\n",
			LDAP_VERSION2, LDAP_VERSION3,
			srvr->ptsrvr_ldapversion );
		return( LDAP_PARAM_ERROR );
	    }

	    if ( srvr->ptsrvr_maxconnections <= 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
			"maximum connections must be greater than "
			"zero (got %d)\n", srvr->ptsrvr_maxconnections );
		return( LDAP_PARAM_ERROR );
	    }

	    if ( srvr->ptsrvr_maxconcurrency <= 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
			"maximum concurrency must be greater than "
			"zero (got %d)\n", srvr->ptsrvr_maxconcurrency );
		return( LDAP_PARAM_ERROR );
	    }

	    if ( tosecs <= 0 ) {
		srvr->ptsrvr_timeout = NULL;
	    } else {
		srvr->ptsrvr_timeout = (struct timeval *)slapi_ch_calloc( 1,
			sizeof( struct timeval ));
		srvr->ptsrvr_timeout->tv_sec = tosecs;
	    }
	}

	/*
	 * parse the LDAP URL
	 */
	if (( rc = ldap_url_parse( srvr->ptsrvr_url, &ludp )) != 0 ) {
	    slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		    "unable to parse LDAP URL \"%s\" (%s)\n",
		    srvr->ptsrvr_url, passthru_urlparse_err2string( rc ));
	    return( LDAP_PARAM_ERROR );
	}

	if ( ludp->lud_dn == NULL || *ludp->lud_dn == '\0' ) {
	    slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		    "missing suffix in LDAP URL \"%s\"\n",
		    srvr->ptsrvr_url );
	    return( LDAP_PARAM_ERROR );
	}
	
	srvr->ptsrvr_hostname = slapi_ch_strdup( ludp->lud_host );
	srvr->ptsrvr_port = ludp->lud_port;
	srvr->ptsrvr_secure =
		(( ludp->lud_options & LDAP_URL_OPT_SECURE ) != 0 );

	/*
	 * If a space-separated list of hosts is configured for failover,
	 * use a different (non infinite) default for connection lifetime.
	 */
	if ( using_def_connlifetime &&
	    strchr( srvr->ptsrvr_hostname, ' ' ) != NULL ) {
		srvr->ptsrvr_connlifetime =
			PASSTHRU_DEF_SRVR_FAILOVERCONNLIFETIME;
	}

	/*
	 * split the DN into multiple suffixes (separated by ';')
	 */
	if (( suffixarray = ldap_str2charray( ludp->lud_dn, ";" )) == NULL ) {
	    slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		"unable to parse suffix string \"%s\" within \"%s\"\n",
		ludp->lud_dn, srvr->ptsrvr_url );
	    return( LDAP_PARAM_ERROR );
	}

	/*
	 * free our LDAP URL descriptor
	 */
	ldap_free_urldesc( ludp );
	ludp = NULL;

	/*
	 * reorganize the suffixes into a linked list and normalize them
	 */
	prevsuffix = NULL;
	for ( j = 0; suffixarray[ j ] != NULL; ++j ) {

	    /*
	     * allocate a new PassThruSuffix structure and fill it in.
	     */
	    suffix = (PassThruSuffix *)slapi_ch_malloc(
		    sizeof( PassThruSuffix ));
	    suffix->ptsuffix_normsuffix =
		    slapi_dn_normalize( suffixarray[ j ] );
	    suffixarray[ j ] = NULL;
	    suffix->ptsuffix_len = strlen( suffix->ptsuffix_normsuffix );
	    suffix->ptsuffix_next = NULL;

	    /*
	     * add to end of list
	     */
	    if ( prevsuffix == NULL ) {
		srvr->ptsrvr_suffixes = suffix;
	    } else {
		prevsuffix->ptsuffix_next = suffix;
	    }
	    prevsuffix = suffix;
	}
	ldap_memfree( suffixarray );

	/*
	 * create mutexes and condition variables for this server
	 */
	if (( srvr->ptsrvr_connlist_mutex = slapi_new_mutex()) == NULL ||
		( srvr->ptsrvr_connlist_cv = slapi_new_condvar(
		srvr->ptsrvr_connlist_mutex )) == NULL ) {
	    return( LDAP_LOCAL_ERROR );
	}

	/*
	 * add this server to the end of our list
	 */
	if ( prevsrvr == NULL ) {
	    theConfig.ptconfig_serverlist = srvr;
	} else {
	    prevsrvr->ptsrvr_next = srvr;
	}
	prevsrvr = srvr;

#ifdef PASSTHRU_VERBOSE_LOGGING
	/*
	 * log configuration for debugging purposes
	 */
	slapi_log_error( SLAPI_LOG_PLUGIN, PASSTHRU_PLUGIN_SUBSYSTEM,
		"PTA server host: \"%s\", port: %d, secure: %d,"
		" maxconnections: %d, maxconcurrency: %d, timeout: %d,"
		" ldversion: %d, connlifetime: %d\n",
		srvr->ptsrvr_hostname, srvr->ptsrvr_port,
		srvr->ptsrvr_secure, srvr->ptsrvr_maxconnections,
		srvr->ptsrvr_maxconcurrency,
		srvr->ptsrvr_timeout == NULL ? -1
		: srvr->ptsrvr_timeout->tv_sec, srvr->ptsrvr_ldapversion,
		srvr->ptsrvr_connlifetime );
	for ( prevsuffix = srvr->ptsrvr_suffixes; prevsuffix != NULL;
		prevsuffix = prevsuffix->ptsuffix_next ) {
	    slapi_log_error( SLAPI_LOG_FATAL, PASSTHRU_PLUGIN_SUBSYSTEM,
		    "   normalized suffix: \"%s\"\n",
		    prevsuffix->ptsuffix_normsuffix );
	}
#endif

    }

    return( LDAP_SUCCESS );
}


/*
 * Get the pass though configuration data.  For now, there is only one
 * configuration and it is global to the plugin.
 */
PassThruConfig *
passthru_get_config( void )
{
    return( &theConfig );
}
