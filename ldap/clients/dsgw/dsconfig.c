/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * dsconfig.c -- CGI configuration update handler -- directory gateway
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */

#include "dsgw.h"

static void handle_request( int reqmethod );
static void handle_post();


main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    int		reqmethod;

    context= dsgw_ch_strdup("pb");
    /*CHANGE THIS*/

    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_POST | DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    handle_request( reqmethod );

    exit( 0 );
}


#define DSGWCONFIG_EMPTY_IF_NULL( s )	( (s) == NULL ? "" : (s) )


static void
handle_request( int reqmethod )
{
    FILE	*fp;
    char	**argv, *buf, line[ BIG_LINE ];
    char	*checked = " CHECKED ", *qs = NULL;
    char	*str_valuefmt = " VALUE=\"%s\" ";
    char	*int_valuefmt = " VALUE=\"%d\" ";
    int		did_post, argc, switch_mode = 0, is_localdb = 0;

    buf = dsgw_ch_malloc( strlen( progname ) + 6 );	/* room for ".html\0" */
    sprintf( buf, "%s.html", progname );
    fp = dsgw_open_html_file( buf, DSGW_ERROPT_EXIT );
    free( buf );
    did_post = 0;
    qs = getenv( "QUERY_STRING" );
    if (( reqmethod == DSGW_METHOD_GET ) && ( qs != NULL ) &&
	    !strcasecmp( qs, "CHANGE" )) {
	switch_mode = 1;
    }

    is_localdb = gc->gc_localdbconf != NULL;

    while ( dsgw_next_html_line( fp, line ))  {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true,
		NULL )) {
	    if ( dsgw_directive_is( line, DRCT_DS_INLINE_POST_RESULTS )) {
		if ( !did_post && reqmethod == DSGW_METHOD_POST ) {
		    handle_post();
		    did_post = 1;
		    /* We re-read the config file, so re-calculate is_localdb */
		    is_localdb = ( gc->gc_localdbconf != NULL );
		}

	    } else if ( dsgw_directive_is( line, DRCT_DS_CHECKED_IF_LOCAL )) {
		if (( is_localdb && !switch_mode ) ||
			( !is_localdb && switch_mode )) {
		    dsgw_emits( checked );
		}

	    } else if ( dsgw_directive_is( line, DRCT_DS_CONFIG_INFO )) {
		dsgw_emits( "<FONT SIZE=\"+1\"><B>" );
		if (( is_localdb && !switch_mode ) ||
		     ( !is_localdb && switch_mode )) {
		    dsgw_emits( "Local Directory Configuration" );
		} else {
		    dsgw_emits( "LDAP Directory Server Configuration" );
		}
		dsgw_emits( "</FONT>\n" );
		
	    } else if ( dsgw_directive_is( line, DRCT_DS_CHECKED_IF_REMOTE )) {
		if (( !is_localdb && !switch_mode ) ||
			( is_localdb && switch_mode )) {
		    dsgw_emits( checked );
		}

	    } else if ( dsgw_directive_is( line, DRCT_DS_HOSTNAME_VALUE ) &&
		    (( !is_localdb && !switch_mode ) ||
		     ( is_localdb && switch_mode ))) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP><B>Host Name:</B></TD>"
		       "<TD><INPUT TYPE=\"text\" NAME=\"host\"" );
		dsgw_emitf( str_valuefmt,
			    DSGWCONFIG_EMPTY_IF_NULL( gc->gc_ldapserver ));
		dsgw_emits( "SIZE=40></TD>\n</TR>\n\n" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_PORT_VALUE ) &&
		    (( !is_localdb && !switch_mode ) ||
		     ( is_localdb && switch_mode ))) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP><B>Port:</B></TD>\n"
		       "<TD><INPUT TYPE=\"text\" NAME=\"port\" " );
		if ( !is_localdb ) {
		    dsgw_emitf( int_valuefmt, gc->gc_ldapport );
		}
		dsgw_emits( "SIZE=5></TD>\n</TR>\n\n" );


#ifndef DSGW_NO_SSL
	    } else if ( dsgw_directive_is( line, DRCT_DS_SSL_CONFIG_VALUE ) &&
		    (( !is_localdb && !switch_mode ) ||
		     ( is_localdb && switch_mode ))) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP>\n"
		       "<B>Use Secure<BR>Sockets Layer (SSL)<BR>for "
		       "connections?:</B></TD>\n"
		       "<TD><INPUT TYPE=\"radio\" NAME=\"ssl\" "
		       "VALUE=\"true\" onClick=\"selectedSSL(true)\"" );
		if ( gc->gc_ldapssl ) {
		    dsgw_emits( checked );
		}
		dsgw_HTML_emits( ">Yes" DSGW_UTF8_NBSP "\n<INPUT TYPE=\"radio\" NAME=\"ssl\" "
		       "VALUE=\"false\" onClick=\"selectedSSL(false)\"" );
		if ( !gc->gc_ldapssl ) {
		    dsgw_emits( checked );
		}
		dsgw_emits( ">No\n</TD>\n</TR>\n\n" );
#endif

	    } else if ( dsgw_directive_is( line, DRCT_DS_BASEDN_VALUE )) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP><B>Base DN" );
		if (( is_localdb && !switch_mode ) ||
		    ( !is_localdb && switch_mode )) {
		    dsgw_emits( " (optional)" );
		}
		dsgw_emits( ":</B></TD>\n<TD><INPUT TYPE=\"text\" "
		       "NAME=\"basedn\" " );
		dsgw_emitf( str_valuefmt,
			DSGWCONFIG_EMPTY_IF_NULL( gc->gc_ldapsearchbase ));
		dsgw_emits( "SIZE=50></TD>\n</TR>\n\n" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_BINDDN_VALUE ) &&
		    (( !is_localdb && !switch_mode ) ||
		     ( is_localdb && switch_mode ))) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP><B>"
		       "Bind DN (optional):</B></TD>\n"
		       "<TD><INPUT TYPE=\"text\" NAME=\"binddn\" " );
		if ( gc->gc_binddn == NULL || strlen( gc->gc_binddn ) == 0 ) {
		    dsgw_emits( "VALUE=\"\"" );
		} else {
		    dsgw_emitf( "VALUE=\"%s\" ", gc->gc_binddn );
		}
		dsgw_emits( " SIZE=50></TD>\n</TR>\n\n" );

	    } else if ( dsgw_directive_is( line, DRCT_DS_BINDPASSWD_VALUE ) &&
		    (( !is_localdb && !switch_mode ) ||
		     ( is_localdb && switch_mode ))) {
		dsgw_emits( "<TR>\n<TD ALIGN=\"right\" NOWRAP><B>"
		       "Bind Password (optional):</B></TD>\n"
		       "<TD><INPUT TYPE=\"password\" NAME=\"bindpw\" " );
		if ( gc->gc_bindpw != NULL && ( strlen( gc->gc_bindpw ) > 0 )) {
		    dsgw_emitf( str_valuefmt, gc->gc_bindpw );
		}
		dsgw_emits( "SIZE=20></TD>\n</TR>\n\n" );
	    } else if ( dsgw_directive_is( line, DRCT_DS_NOCERTFILE_WARNING )
		    && ( gc->gc_securitypath == NULL )
		    && !is_localdb && gc->gc_ldapssl && argc > 0 ) {
		/*
		 * using LDAP over SSL but no CertFile in ns-admin.conf:
		 * show a warning message
		 */
		dsgw_emits( argv[ 0 ] );
	    }
	}
    }

    fclose( fp );
}


static void
handle_post()
{
    char	*dirsvctype, *dbhandle;
    dsgwconfig	cfg;

    memset( &cfg, 0, sizeof( cfg ));

    dirsvctype = dsgw_get_cgi_var( "dirsvctype", DSGW_CGIVAR_REQUIRED );
    dbhandle = dsgw_get_cgi_var( "dbhandle", DSGW_CGIVAR_OPTIONAL );
    cfg.gc_ldapsearchbase = dsgw_get_cgi_var( "basedn", DSGW_CGIVAR_OPTIONAL );

    if ( strcasecmp( dirsvctype, "local" ) == 0 ) {
	char	*userdb_path;

	if (( userdb_path = get_userdb_dir()) == NULL ) {
	    dsgw_error( DSGW_ERR_USERDB_PATH, NULL, DSGW_ERROPT_INLINE, 0,
		    NULL );
	    return;
	}
	cfg.gc_localdbconf = dsgw_ch_malloc( strlen( userdb_path ) +
		strlen( DSGW_LCACHECONF_PPATH ) +
		strlen( DSGW_LCACHECONF_FILE ) + 2 );
	sprintf( cfg.gc_localdbconf, "%s/%s%s", userdb_path,
		DSGW_LCACHECONF_PPATH, DSGW_LCACHECONF_FILE );
    } else if ( strcasecmp( dirsvctype, "remote" ) == 0 ) {
	cfg.gc_ldapserver = dsgw_get_cgi_var( "host", DSGW_CGIVAR_REQUIRED );
	cfg.gc_ldapport = atoi( dsgw_get_cgi_var( "port",
		DSGW_CGIVAR_REQUIRED ));
#ifndef DSGW_NO_SSL
	cfg.gc_ldapssl =
		dsgw_get_boolean_var( "ssl", DSGW_CGIVAR_OPTIONAL, 0 );
#endif
	cfg.gc_binddn = dsgw_get_escaped_cgi_var( "escapedbinddn", "binddn",
		DSGW_CGIVAR_OPTIONAL );
	cfg.gc_bindpw = dsgw_get_cgi_var( "bindpw", DSGW_CGIVAR_OPTIONAL );
    } else {
	dsgw_error( DSGW_ERR_SERVICETYPE, dirsvctype, DSGW_ERROPT_INLINE, 0,
		NULL );
	return;
    }

    if ( cfg.gc_ldapsearchbase == NULL ) {
	cfg.gc_ldapsearchbase = "";
    }

    if ( dsgw_update_dbswitch( &cfg, dbhandle, DSGW_ERROPT_INLINE ) == 0 ) {
	/*
	 * success: display status message and then re-read config. file
	 */
	dsgw_emits( "<FONT SIZE=\"+1\">\n<P>The Directory Service configuration" );
	if ( dbhandle != NULL ) {
	    dsgw_emitf( " for <B>%s</B>", dbhandle );
	}
	dsgw_emits( " has been updated.\n</FONT>\n" );

	(void)dsgw_read_config(NULL);
    }

    dsgw_emits( "<HR>\n" );
}
