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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * auth.c -- CGI authentication form generator -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

static void post_request();
static void get_request(char *binddn);

int main(
    int argc,
    char **argv
#ifdef DSGW_DEBUG
    ,char	*env[]
#endif
) {
    int	reqmethod;
    char *binddn  = NULL;
    char *qs      = NULL;

    if (( qs = getenv( "QUERY_STRING" )) != NULL && *qs != '\0' ) {
	/* parse the query string: */
	auto char *p, *iter = NULL;
	qs = dsgw_ch_strdup( qs );
	for ( p = ldap_utf8strtok_r( qs,   "&", &iter ); p != NULL;
	      p = ldap_utf8strtok_r( NULL, "&", &iter )) {

	    /*Get the context.*/
	    if ( !strncasecmp( p, "context=", 8 )) {
		context = dsgw_ch_strdup( p + 8 );
		dsgw_form_unescape( context );
		continue;
	    }

	    /*Get the dn*/
	    if ( !strncasecmp( p, "dn=", 3 )) {
		binddn = dsgw_ch_strdup( p + 3 );
		dsgw_form_unescape( binddn );
		continue;
	    }
	}
	free( qs ); qs = NULL;
    }

    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_POST | DSGW_METHOD_GET );

#ifdef DSGW_DEBUG
    dsgw_logstringarray( "env", env ); 
#endif

    if ( reqmethod == DSGW_METHOD_POST ) {
	post_request();
    } else {
	get_request(binddn);
    }

    exit( 0 );
}

static void
get_request(char *binddn)
{
    dsgw_send_header();

    if ( binddn != NULL ) {
	if ( !strcmp( binddn, MGRDNSTR )) {
	    if ( gc->gc_rootdn == NULL ) {
		dsgw_error( DSGW_ERR_NO_MGRDN,
			    XP_GetClientStr (DBT_noDirMgrIsDefined_),
			    DSGW_ERROPT_EXIT, 0, NULL );
	    }
	    binddn = dsgw_ch_strdup( gc->gc_rootdn );
	} else if ( *binddn == '\0' ) {
	    binddn = NULL;
	} else {
	    binddn = dsgw_ch_strdup( binddn );
	    dsgw_form_unescape( binddn );
	}
    }
    dsgw_emit_auth_form( binddn );
    if ( binddn != NULL ) {
	free( binddn );
    }
}



static void
post_request()
{
    char *binddn = NULL;
    char *dorootbind = NULL;

    dsgw_send_header();
    /*
     * If the "authasrootdn" CGI variable is present and has the value
     * "true" then the user clicked on the "authenticate as directory
     * manager" button.  In that case, try to bind as the root dn given
     * in the dsgw config file.
     */
    dorootbind = dsgw_get_cgi_var( "authasrootdn", DSGW_CGIVAR_OPTIONAL );
    if ( dorootbind != NULL && !strcasecmp( dorootbind, "true" )) {
	binddn = dsgw_ch_strdup( gc->gc_rootdn );
    } else {
	binddn = dsgw_get_escaped_cgi_var( "escapedbinddn", "binddn",
	    DSGW_CGIVAR_OPTIONAL );
    }
	
    dsgw_emit_auth_form( binddn );
}
