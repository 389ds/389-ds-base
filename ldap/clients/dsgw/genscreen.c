/** --- BEGIN COPYRIGHT BLOCK ---
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
 * do so, delete this exception statement from your version. 
 * 
 * 
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
