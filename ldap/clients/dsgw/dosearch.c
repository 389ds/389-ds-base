/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/*
 * dosearch.c -- CGI search handler -- HTTP gateway
 */

#include "dsgw.h"

static void get_request(char* hostport, char *dn, char *ldapquery);
static void post_request();


int main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    int		reqmethod;
    char       *qs = NULL;
    char       *dn = NULL;
    char       *hostport = NULL;
    char       *ldapquery = NULL;
#ifndef __LP64__	
#ifdef HPUX
	/* call the static constructors in libnls */
	_main();
#endif
#endif
    /* 
     * Parse out the GET args, if any. See the comments under
     * get_request for an explanation of what's going on here
     */
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

	    if ( !strncasecmp( p, "hp=", 3 )) {
		hostport = dsgw_ch_strdup( p + 3 );
		dsgw_form_unescape( hostport );
		continue;
	    }

	    if ( !strncasecmp( p, "ldq=", 4 )) {
		ldapquery = dsgw_ch_strdup( p + 4 );
		dsgw_form_unescape( ldapquery );
		continue;
	    }

	    if ( !strncasecmp( p, "dn=", 3 )) {
		dn = dsgw_ch_strdup( p + 3 );
		dsgw_form_unescape( dn );
		continue;
	    }
	    
	    /* 
	     * If it doesn't match any of the above, then
	     * tack it onto the end of ldapquery.
	     */
	    if (ldapquery != NULL) {
	      ldapquery = dsgw_ch_realloc(ldapquery, sizeof(char *) * (strlen(ldapquery) + strlen(p) + 2));
	      sprintf( ldapquery, "%s&%s", ldapquery, p );
	    }	    
	}
	
	free( qs ); qs = NULL;
    }


    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_POST | DSGW_METHOD_GET );

    /*
     * Note: we don't call dsgw_send_header() here like we usually do because
     * on a GET we may be asked to return a MIME type other than the default
     * of text/html.  For GET requests, we send the headers inside
     * ldaputil.c:dsgw_ldapurl_search().  For POST requests, we send them
     * below in post_request().
     */

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    if ( reqmethod == DSGW_METHOD_GET ) {
	get_request(hostport, dn, ldapquery);
    } else {
	post_request();
    }

    exit( 0 );
}


static void
get_request(char* hostport, char *dn, char *ldapquery)
{
    int    urllen  = 0;
    int    argslen = 0;
    char  *p       = NULL;
    char  *ldapurl = NULL;

    /*
     * The following comment is kept here only as a reminder of the past.
     * It is no longer relevant. See the next comment. - RJP
     *
     * On a GET request, we do an LDAP URL search (which will just display
     * a single entry if all that is included is "host:port/DN").
     * The HTTP URL should be:
     *    .../dosearch[/host[:port]][?[dn=baseDN&][LDAPquery]]
     * This will be converted to the LDAP URL:
     *    ldap://[host[:port]]/[baseDN][?LDAPquery]
     *
     * For compatibility with prior versions, the HTTP URL may be:
     *    .../dosearch/host[:port]/[baseDN][?LDAPquery]
     * In this case, the host:port is required, since PATH_INFO can't
     * start with a '/' (web server sees that as a different program).
     * This older HTTP URL format is deprecated, because PATH_INFO is
     * not 8-bit clean on Japanese Windows NT.
     */
    
    /*
     * The only form supported now is:
     * .../dosearch?context=BLAH[&hp=host[:port]][&dn=baseDN][&ldq=LDAPquery]]  
     *   -RJP
     */
    argslen = 0;

    /* get the length of all the args (dn, hostport, ldapquery)*/
    if (hostport != NULL) {
      argslen += strlen(hostport);
    }

    if (dn != NULL) {
      argslen += strlen(dn);
    }

    if (ldapquery != NULL) {
      argslen += strlen(ldapquery);
    }

    /* If nothing was supplied, exit*/
    if ( argslen == 0 ) {
	dsgw_error( DSGW_ERR_MISSINGINPUT, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    /* Malloc the ldapurl*/
    urllen = LDAP_URL_PREFIX_LEN + argslen + 3;
    p = ldapurl = (char *)dsgw_ch_malloc( urllen );
    
    /*Slap on ldap:// */
    strcpy( p, LDAP_URL_PREFIX );
    p += LDAP_URL_PREFIX_LEN;

    /*Slap on host:port if there is one*/
    if ( hostport != NULL ) {
	strcpy( p, hostport );
    }

    strcat( ldapurl, "/" );

    /*Slap on /dn, if there is a dn */
    if ( dn != NULL ) {
	strcat( ldapurl, dn );
    }
    
    /*Slap on ?ldapquery */
    if ( ldapquery != NULL ) {
	sprintf( ldapurl + strlen( ldapurl ), "?%s", ldapquery );
    }

#ifdef DSGW_DEBUG
    dsgw_log( "get_request: processing LDAP URL \"%s\"\n", ldapurl );
#endif
    dsgw_ldapurl_search( NULL, ldapurl);
}


static void
post_request()
{
    char			*modestr, *searchstring, *type, *base;
    LDAP			*ld;
    LDAPFiltDesc		*lfdp;
    struct ldap_searchobj	*solistp, *sop;
    int				authmode, mode, options;

    dsgw_send_header();

    options = 0;
    modestr = dsgw_get_cgi_var( "mode", DSGW_CGIVAR_REQUIRED );
    searchstring = dsgw_get_cgi_var( "searchstring", DSGW_CGIVAR_OPTIONAL );
    dsgw_remove_leading_and_trailing_spaces( &searchstring );
#ifdef DSGW_DEBUG
    if (searchstring) {
	dsgw_log ("searchstring=\"%s\"\n", searchstring);
    } else {
	dsgw_log ("searchstring=NULL");
    }
#endif

    authmode = 0;
    if ( strcasecmp( modestr, DSGW_SRCHMODE_AUTH ) == 0 ) {
	/*
	 * treat authenticate as a variant of the smart search mode
	 */
	authmode = 1;
	mode = DSGW_SRCHMODE_SMART_ID;
	options |= DSGW_DISPLAY_OPT_AUTH;
    } else if ( strcasecmp( modestr, DSGW_SRCHMODE_SMART ) == 0 ) {
	mode = DSGW_SRCHMODE_SMART_ID;
    } else if ( strcasecmp( modestr, DSGW_SRCHMODE_COMPLEX ) == 0 ) {
	mode = DSGW_SRCHMODE_COMPLEX_ID;
    } else if (  strcasecmp( modestr, DSGW_SRCHMODE_PATTERN ) == 0 ) {
	mode = DSGW_SRCHMODE_PATTERN_ID;
    } else {
	dsgw_error( DSGW_ERR_SEARCHMODE, modestr, 0, 0, NULL );
    }

    if ( mode != DSGW_SRCHMODE_PATTERN_ID
	    && ( searchstring == NULL || *searchstring == '\0' )) {
	dsgw_error( DSGW_ERR_NOSEARCHSTRING, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if (( type = dsgw_get_cgi_var( "type", authmode ? DSGW_CGIVAR_OPTIONAL :
	    DSGW_CGIVAR_REQUIRED )) == NULL ) {
	type = DSGW_SRCHTYPE_AUTH;
    }

    if (( base = dsgw_get_cgi_var( "base", DSGW_CGIVAR_OPTIONAL )) == NULL ) {
	base = gc->gc_ldapsearchbase;
    }

    /* check for options (carried in boolean CGI variables) */
    if ( dsgw_get_boolean_var( "listifone", DSGW_CGIVAR_OPTIONAL, 0 )) {
	options |= DSGW_DISPLAY_OPT_LIST_IF_ONE;
    }

    if ( dsgw_get_boolean_var( "editable", DSGW_CGIVAR_OPTIONAL, 0 )) {
	options |= DSGW_DISPLAY_OPT_EDITABLE;
    }

    if ( dsgw_get_boolean_var( "link2edit", DSGW_CGIVAR_OPTIONAL, 0 )) {
	options |= DSGW_DISPLAY_OPT_LINK2EDIT;
    }

    if ( dsgw_get_boolean_var( "dnlist_js", DSGW_CGIVAR_OPTIONAL, 0 )) {
	options |= DSGW_DISPLAY_OPT_DNLIST_JS;
    }

    (void) dsgw_init_ldap( &ld, &lfdp, ( authmode == 1 ) ? 1 : 0, 0);

    if ( mode != DSGW_SRCHMODE_PATTERN_ID ) {
	dsgw_init_searchprefs( &solistp );

	if (( sop = dsgw_type2searchobj( solistp, type )) == NULL ) {
	    ldap_unbind( ld );
	    dsgw_error( DSGW_ERR_UNKSRCHTYPE, type, DSGW_ERROPT_EXIT, 0, NULL );
	}
    }

    switch( mode ) {
    case DSGW_SRCHMODE_SMART_ID:
	/*
	 * smart search mode -- try to do the right kind of search for the
	 * client based on what the user entered in the search box
	 */
	dsgw_smart_search( ld, sop, lfdp, base, searchstring, options );
	break;

    case DSGW_SRCHMODE_COMPLEX_ID: {
	/*
	 * complex search mode -- construct a specific filter based on
	 * user's form selections
	 */
	int			scope;
	char			*attrlabel, *matchprompt;
	struct ldap_searchattr	*sap;
	struct ldap_searchmatch	*smp;

	attrlabel = dsgw_get_cgi_var( "attr", DSGW_CGIVAR_REQUIRED );
	if (( sap = dsgw_label2searchattr( sop, attrlabel )) == NULL ) {
	    ldap_unbind( ld );
	    dsgw_error( DSGW_ERR_UNKATTRLABEL, attrlabel, DSGW_ERROPT_EXIT,
		    0, NULL );
	}
	
	matchprompt = dsgw_get_cgi_var( "match", DSGW_CGIVAR_REQUIRED );
	if (( smp = dsgw_prompt2searchmatch( sop, matchprompt )) == NULL ) {
	    ldap_unbind( ld );
	    dsgw_error( DSGW_ERR_UNKMATCHPROMPT, matchprompt,
		    DSGW_ERROPT_EXIT, 0, NULL );
	}

	scope = dsgw_get_int_var( "scope", DSGW_CGIVAR_OPTIONAL,
		sop->so_defaultscope );
	dsgw_pattern_search( ld, sop->so_objtypeprompt,
			     sap->sa_attrlabel, smp->sm_matchprompt, searchstring,
			     smp->sm_filter, sop->so_filterprefix, NULL, sap->sa_attr,
			     base, scope, searchstring, options );
    }
	break;

    case DSGW_SRCHMODE_PATTERN_ID: {
	/*
	 * pattern-based search mode (no searchprefs or filter file used)
	 */
	char	*attr, *pattern, *prefix, *suffix, *searchdesc;
	int	scope;

	attr = dsgw_get_cgi_var( "attr", DSGW_CGIVAR_REQUIRED );
	pattern = dsgw_get_cgi_var( "filterpattern", DSGW_CGIVAR_REQUIRED );
	prefix = dsgw_get_cgi_var( "filterprefix", DSGW_CGIVAR_OPTIONAL );
	suffix = dsgw_get_cgi_var( "filtersuffix", DSGW_CGIVAR_OPTIONAL );
	scope = dsgw_get_int_var( "scope", DSGW_CGIVAR_OPTIONAL,
		LDAP_SCOPE_SUBTREE );
	options |= DSGW_DISPLAY_OPT_CUSTOM_SEARCHDESC;
	searchdesc = dsgw_get_cgi_var( "searchdesc", DSGW_CGIVAR_OPTIONAL );
	dsgw_pattern_search( ld, type, searchdesc, NULL, NULL,
		pattern, prefix, suffix, attr,
		base, scope, searchstring, options );
	}
	break;
    }

    ldap_unbind( ld );
}
