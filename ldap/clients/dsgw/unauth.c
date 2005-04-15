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
 * unauth.c -- CGI to discard cookies -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

char *get_auth_cookie( char *cookie );
void generate_message( int type );

#define	CKEXP_SUCCESS	1
#define	CKEXP_FAILURE	2

int main( int argc, char **argv )
{
    int		reqmethod;
    char	*expck;
    char	*authck;
    int		rc;
    char        *qs = NULL;

  /* Parse out the context=blah.html */
    if (( qs = getenv( "QUERY_STRING" )) != NULL && *qs != '\0' ) {
	/* parse the query string: */
	auto char *p, *iter = NULL;
	qs = dsgw_ch_strdup( qs );
	
	for ( p = ldap_utf8strtok_r( qs,   "&", &iter ); p != NULL;
	      p = ldap_utf8strtok_r( NULL, "&", &iter )) {
	    
	    /*
	     * Get the conf file name. It'll be translated
	     * into /dsgw/context/CONTEXT.conf if
	     * CONTEXT is all alphanumeric (no slahes,
	     * or dots). CONTEXT is passed into the cgi.
	     * if context=CONTEXT is not there, or PATH_INFO
	     * was used, then use dsgw.conf
	     */
	    if ( !strncasecmp( p, "context=", 8 )) {
		context = dsgw_ch_strdup( p + 8 );
		dsgw_form_unescape( context );
		continue;
	    }
	    
	}
	
	free( qs ); qs = NULL;
    }
    
    
    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_GET );

    authck = dsgw_get_auth_cookie();
    if ( authck == NULL ) {
	/* No cookie.  Generate an informational message. */
	generate_message( CKEXP_SUCCESS );
	free( authck );
	exit( 0 );
    }
    
    /* Remove the cookie from the cookie database */
    rc = dsgw_delcookie( authck );

    /* Generate a cookie header with the cookie set to [unauthenticated] */
    expck = dsgw_ch_malloc( strlen( DSGW_CKHDR ) + strlen( DSGW_AUTHCKNAME ) +
	    strlen( DSGW_UNAUTHSTR ) + strlen( "=; path=/" ) + 2 );
    sprintf( expck, "%s%s=%s; path=/", DSGW_CKHDR, DSGW_AUTHCKNAME, DSGW_UNAUTHSTR );
    dsgw_add_header( expck );
    generate_message( CKEXP_SUCCESS );
    free( authck );
    free( expck );
    exit( 0 );
}



/*
 * It's quite likely that there will be more than one cookie in the
 * Cookie: header.  See if we've got an authentication cookie, and if
 * so, parse it out and return a pointer to it.  If no auth cookie
 * is present, return NULL.
 */
char *
get_auth_cookie( char *cookie )
{
    char *p, *e;

    if ( cookie == NULL ) {
	return NULL;
    }

    if (( p = strstr( cookie, DSGW_AUTHCKNAME )) == NULL ) {
	return NULL;
    }
    
    if (( e = strchr( p, ';' )) != NULL ) {
	*e = '\0';
    }

    return p;
}
	


void
generate_message( int type )
{
    dsgw_send_header();
    dsgw_emits( "<HTML>" );
    dsgw_head_begin();
    dsgw_emits( "\n<TITLE>" );
    if ( type == CKEXP_SUCCESS ) {
	dsgw_emits( "Success" );
    } else if ( type == CKEXP_FAILURE ) {
	dsgw_emits( "Error" );
    }
    dsgw_emits( "</TITLE>\n</HEAD>\n" );
    dsgw_emitf( "<BODY %s>\n", dsgw_html_body_colors );

    dsgw_emitf( "<CENTER>\n"
	"<FONT SIZE=+2>\n"
	"%s"
	"</FONT>\n"
	"</CENTER>\n"
	"<P>\n"
	"%s",
	XP_GetClientStr( DBT_Success_ ),
	XP_GetClientStr( DBT_YouAreNoLongerAuthenticated_ ));

    if ( type != CKEXP_SUCCESS ) {
	/*
	 * Something went wrong, so generate some JavaScript to
	 * discard the cookie.
	 */
	dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	dsgw_emitf( "document.cookie = '%s=%s; path=/';\n", DSGW_AUTHCKNAME,
		DSGW_UNAUTHSTR );
	dsgw_emits( "</SCRIPT>\n" );
    }
    dsgw_form_begin (NULL, NULL);
    dsgw_emits( "\n"
	"<TABLE BORDER=2 WIDTH=100%>\n"
	"<TR>\n"
	"<TD ALIGN=CENTER WIDTH=50%>\n");
    dsgw_emitf(
	"<INPUT TYPE=BUTTON VALUE=\"%s\"", XP_GetClientStr( DBT_GoBack_ ));
    dsgw_emits(
	" onClick=\"window.location.href=");
    dsgw_quote_emitf(QUOTATION_JAVASCRIPT, "auth?context=%s", context);
    dsgw_emits(";\"></TD>\n"
	"<TD ALIGN=CENTER WIDTH=50%>\n" );
    dsgw_emit_helpbutton( "UNAUTH" );
    dsgw_emits( "</TABLE></FORM>\n"
	"</BODY></HTML>\n" );
}

