/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
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
