/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/* 
 * Generate a screen.
 */

#include "dsgw.h"

static int dsgw_genscreen_begin( char *fname, FILE **fpp,
	char *stop_at_directive, int erropts );
static int dsgw_genscreen_continue( FILE **fpp, char *stop_at_directive,
	int erropts );

static LDAP *ld = NULL;

main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    char	*p, *tmplname, *buf;

    context=dsgw_ch_strdup("pb");
    /*CHANGE THIS*/

    (void)dsgw_init( argc, argv, DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    /*
     * If the QUERY_STRING is non-NULL, it looks like this:
     *
     *    template &CONTEXT=context [ &INFO=infostring ]
     *
     * where:
     *   "template" is the name of the HTML template to render
     *   "infostring" is a message used to replace DS_LAST_OP_INFO directives
     *
     * If the QUERY_STRING is NULL, the name of this program is used as the
     * template.
     */

    if (( tmplname = getenv( "QUERY_STRING" )) == NULL ) {
	tmplname = progname;
    } else {
	tmplname = dsgw_ch_strdup( tmplname );
	if (( p = strrchr( tmplname, '&' )) != NULL ) {
	    *p++ = '\0';
	    if ( strncasecmp( p, "info=", 5 ) == 0 ) {
		dsgw_last_op_info = dsgw_ch_strdup( p + 5 );
		dsgw_form_unescape( dsgw_last_op_info );
	    }
	}
    }
	

    buf = dsgw_ch_malloc( strlen( tmplname ) + 6 );	/* room for ".html\0" */
    sprintf( buf, "%s.html", tmplname );

    dsgw_genscreen_begin( buf, NULL, NULL, DSGW_ERROPT_EXIT );

    exit( 0 );
}


static int
dsgw_genscreen_begin( char *fname, FILE **fpp, char *stop_at_directive,
	int erropts )
{
    FILE	*html;

    if ( fpp == NULL ) {
	fpp = &html;
    }

    if (( *fpp = dsgw_open_html_file( fname, erropts )) == NULL ) {
	*fpp = NULL;
	return( -1 );
    }

    return( dsgw_genscreen_continue( fpp, stop_at_directive, erropts ));
}


static int
dsgw_genscreen_continue( FILE **fpp, char *stop_at_directive, int erropts )
{
    char	**argv, line[ BIG_LINE ];
    int		argc;
 
    while ( dsgw_next_html_line( *fpp, line ))  {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true,
		NULL )) {
	    if ( stop_at_directive != NULL &&
		    dsgw_directive_is( line, stop_at_directive )) {
		return( 0 );
	    }
	    if ( dsgw_directive_is( line, DRCT_DS_LOCATIONPOPUP )) {
		dsgw_emit_location_popup( ld, argc, argv, erropts );
	    }	
	}
    }

    fclose( *fpp );
    *fpp = NULL;

    return( 0 );
}
