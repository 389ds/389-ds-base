/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * dsimpldif.c -- CGI import ldif file handler -- directory gateway
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */

#include "dsgw.h"
static void handle_request( int reqmethod );
static void handle_post();

static char *ldiffile;
static int erase = 0, stop = 1;

main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    int		reqmethod;

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
    char	*checked = " CHECKED ";
    char	*str_valuefmt = " VALUE=\"%s\" ";
    int		did_post, argc;

    buf = dsgw_ch_malloc( strlen( progname ) + 6 );	/* room for ".html\0" */
    sprintf( buf, "%s.html", progname );
    fp = dsgw_open_html_file( buf, DSGW_ERROPT_EXIT );
    free( buf );
    did_post = 0;

    while ( dsgw_next_html_line( fp, line ))  {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true,
		NULL )) {
	    if ( dsgw_directive_is( line, DRCT_DS_INLINE_POST_RESULTS )) {
		if ( !did_post && reqmethod == DSGW_METHOD_POST ) {
		    handle_post();
		    did_post = 1;
		}
	    } else if ( dsgw_directive_is( line, DS_LDIF_FILE )) {
		dsgw_emitf( str_valuefmt,
			    DSGWCONFIG_EMPTY_IF_NULL( ldiffile ));
	    } else if ( dsgw_directive_is( line, DS_CHECKED_IF_ERASE )) {
		if ( erase ) {
		    dsgw_emits( checked );
		}
	    } else if ( dsgw_directive_is( line, DS_CHECKED_IF_NOTERASE )) {
		if ( !erase ) {
		    dsgw_emits( checked );
		}
	    } else if ( dsgw_directive_is( line, DS_CHECKED_IF_STOP )) {
		if ( stop ) {
		    dsgw_emits( checked );
		}
	    } else if ( dsgw_directive_is( line, DS_CHECKED_IF_NOTSTOP )) {
		if ( !stop ) {
		    dsgw_emits( checked );
		}
	    }
	}
    }

    fclose( fp );
}


static void
handle_post()
{
    char cmd[ BIG_LINE ], path[BIG_LINE ];
    char *userdb_path;

    ldiffile = dsgw_get_cgi_var( "ldif", DSGW_CGIVAR_REQUIRED );
    erase = dsgw_get_boolean_var( "erase", DSGW_CGIVAR_REQUIRED, 0 );
    stop = dsgw_get_boolean_var( "stop", DSGW_CGIVAR_REQUIRED, 0 );

    if (erase) { 
	if ( gc->gc_localdbconf == NULL) {
	    /* don't erase the real ldap database */
	    dsgw_error( DSGW_ERR_DB_ERASE, NULL, DSGW_ERROPT_EXIT, 0, NULL );
	}
	/* erase the local database */
	if ( erase_db() != 0 ) {
	    return;
	}
    }

    if (( userdb_path = get_userdb_dir()) == NULL ) {
        dsgw_error( DSGW_ERR_USERDB_PATH, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if (gc->gc_localdbconf == NULL) {
	/* remote */
	PR_snprintf (cmd, BIG_LINE, "./%s -a %s -h %s -p %d -f %s > %s 2>&1", 
		DSGW_LDAPMODIFY, stop?"":"-c",gc->gc_ldapserver, 
		gc->gc_ldapport, ldiffile, DSGW_NULL_DEVICE);
    }
    else {
	/* local database */
    	PR_snprintf (cmd, BIG_LINE, "./%s -a %s -C %s -f %s > %s 2>&1",
		DSGW_LDAPMODIFY, stop?"":"-c", gc->gc_localdbconf, ldiffile,
		DSGW_NULL_DEVICE);
    }
    PR_snprintf (path, BIG_LINE, "%s%s", userdb_path, DSGW_TOOLSDIR);
    chdir ( path );
    fflush (stdout);
    if (system (cmd) == 0) { 
	/*
	 * success: display status message 
	 */
	dsgw_emits( 
	"<FONT SIZE=\"+1\">\n<P>The ldif file has been added.\n</FONT>\n " );
    }
    else {
	dsgw_emits( 
	"<FONT SIZE=\"+1\">\n<P>The ldif file could not be added.\n</FONT>\n " );
    }

    dsgw_emits( "<HR>\n" );
}

