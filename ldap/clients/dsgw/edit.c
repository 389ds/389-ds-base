/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * edit.c -- CGI editable entry display -- HTTP gateway
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */

#include "dsgw.h"
#include "dbtdsgw.h"

static void get_request(char *dn, char *tmplname, 
			char *parent, unsigned long options);


int main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{


    char		*dn, *tmplname, *p, *parent;
    unsigned long	options;

    /*
     * If the QUERY_STRING is non-NULL, it looks like this:
     *
     *     template [&CONTEXT=context] [ &INFO=infostring ] [ &ADD ] [ &DN=dn ] \
     *              [&DNATTR=attrname&DNDESC=description]
     *
     * where:
     *   "template" is the name of the edit template to use for display,
     *   "dn" is escaped dn,
     *   "infostring" is a message used to replace DS_LAST_OP_INFO directives
     *   "attrname" is the name of a DN-valued attribute
     *   "dndesc" is the destriptive name of the above DN-valued attribute
     *
     * If "&ADD" is present, we check to make sure the entry
     * does not exist, then we check that the parent entry exists, and then
     * we present an "add entry" form.
     *
     * Note: original form http://host/edit/dn[/...]?template[&...] is
     *       supported for keeping backward compatibility.
     *       But passing DN as PATH_INFO is NOT recommended.
     *       Since PATH_INFO is passed to CGI as is (non-escaped),
     *       the content has a risk to get broken especially when
     *       it contains 8-bit UTF-8 data.  (This is a known problem
     *       on localized Windows machines.)
     */

    options = DSGW_DISPLAY_OPT_EDITABLE;
    dn = NULL;
#ifndef  __LP64__
#ifdef HPUX
	/* call the static constructors in libnls */
	_main();
#endif
#endif

    if (( tmplname = getenv( "QUERY_STRING" )) != NULL && *tmplname != '\0' ) {
	tmplname = dsgw_ch_strdup( tmplname );
	while ( tmplname != NULL && ((( p = strrchr( tmplname, '&' )) != NULL ) || (p=tmplname) != NULL )) {
	    if (p == tmplname) {
		tmplname = NULL;
	    } else {
		*p++ = '\0'; 
	    }

	    if ( strcasecmp( p, "add" ) == 0 ) {
		options |= DSGW_DISPLAY_OPT_ADDING;
		if (( p = strrchr( tmplname, '&' )) != NULL ) {
		    *p++ = '\0';
		}
	    }

	    if ( p != NULL && strncasecmp( p, "info=", 5 ) == 0 ) {
		dsgw_last_op_info = dsgw_ch_strdup( p + 5 );
		dsgw_form_unescape( dsgw_last_op_info );
		continue;
	    } 
	    if ( p != NULL && strncasecmp( p, "dn=", 3 ) == 0 ) {
		dn = dsgw_ch_strdup( p + 3 );
		dsgw_form_unescape( dn );
		continue;
	    } 
	    if ( p != NULL && strncasecmp( p, "dnattr=", 7 ) == 0 ) {
		dsgw_dnattr = dsgw_ch_strdup( p + 7 );
		dsgw_form_unescape( dsgw_dnattr );
		continue;
	    } 
	    if ( p != NULL && strncasecmp( p, "dndesc=", 7 ) == 0 ) {
		dsgw_dndesc = dsgw_ch_strdup( p + 7 );
		dsgw_form_unescape( dsgw_dndesc );
		continue;
	    } 
	    if ( p != NULL && strncasecmp( p, "context=", 8 ) == 0) {
		context = dsgw_ch_strdup( p + 8 );
		dsgw_form_unescape( context );
		continue;
	    }
	    
	    /* 
	     * If none of the if-statements above matched,
	     * then it's the template name
	     */
	    tmplname = p;
	    break;
	}
	
    } else {
	tmplname = NULL;
    }

    (void)dsgw_init( argc, argv,  DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    get_request(dn, tmplname, parent, options);

    exit( 0 );
}


static void
get_request(char *dn, char *tmplname, char *parent, unsigned long options)
{
    LDAP		*ld;

    if ( dn == NULL ) { /* not found in QUERY_STRING */
	dsgw_error( DSGW_ERR_MISSINGINPUT, NULL, DSGW_ERROPT_EXIT, 0, NULL );
    }

#ifdef DSGW_DEBUG
    dsgw_log( "get_request: dn: \"%s\", tmplname: \"%s\" "
	      "dnattr: \"%s\", dndesc: \"%s\"\n", dn,
	    ( tmplname == NULL ) ? "(null)" : tmplname, 
	    ( dsgw_dnattr == NULL ) ? "(null)" : dsgw_dnattr, 
	    ( dsgw_dndesc == NULL ) ? "(null)" : dsgw_dndesc );
#endif

    (void)dsgw_init_ldap( &ld, NULL, 0, 0);

    if (( options & DSGW_DISPLAY_OPT_ADDING ) == 0 ) {
	/*
	 * editing an existing entry -- if no DN is provided and we are running
	 * under the admin server, try to get DN from admin. server
	 */
	if ( *dn == '\0' ) {
	    (void)dsgw_get_adm_identity( ld, NULL, &dn, NULL,
		    DSGW_ERROPT_EXIT );
	}

	dsgw_read_entry( ld, dn, NULL, tmplname, NULL, options );

    } else {
	dsgwtmplinfo        *tip;
    	char		    *matched;

	/*
	 * new entry -- check to make sure it doesn't exist
	 */
	if ( dsgw_ldap_entry_exists( ld, dn, &matched, DSGW_ERROPT_EXIT )) {
	    char	**rdns;

	    dsgw_html_begin( XP_GetClientStr(DBT_entryAlreadyExists_), 1 );
	    dsgw_emits( XP_GetClientStr(DBT_anEntryNamed_) );
	    rdns = ldap_explode_dn( dn, 1 );
	    dsgw_html_href(
		    dsgw_build_urlprefix(),
		    dn, ( rdns == NULL || rdns[ 0 ] == NULL ) ? dn : rdns[ 0 ],
		    NULL, XP_GetClientStr(DBT_onmouseoverWindowStatusClickHere_) );
	    if ( rdns != NULL ) {
		ldap_value_free( rdns );
	    }
	    dsgw_emits( XP_GetClientStr(DBT_alreadyExistsPPleaseChooseAnothe_) );

	    dsgw_form_begin( NULL, NULL );
	    dsgw_emits( "\n<CENTER><TABLE border=2 width=\"100%\"><TR>\n" );
	    dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		"onClick=\"parent.close()\">", XP_GetClientStr(DBT_closeWindow_1) );
	    dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	    dsgw_emit_helpbutton( "ENTRYEXISTS" );
	    dsgw_emits( "\n</TABLE></CENTER></FORM>\n" );
	    dsgw_html_end();
	} else if ( !dsgw_is_dnparent( matched, dn ) &&
		!dsgw_dn_cmp( dn, gc->gc_ldapsearchbase )) {
	    /*
	     * The parent entry does not exist, and the dn being added is not
	     * the same as the suffix for which the gateway is configured.
	     */
	    dsgw_html_begin( XP_GetClientStr(DBT_parentEntryDoesNotExist_), 1 );
	    dsgw_emitf( XP_GetClientStr(DBT_youCannotAddAnEntryByTheNamePBSB_),
		    dn );
	    parent = dsgw_dn_parent( dn );
	    if ( parent == NULL || strlen( parent ) == 0 ) {
		dsgw_emits( XP_GetClientStr(DBT_itsParentN_) );
	    } else {
		dsgw_emitf( XP_GetClientStr(DBT_anEntryNamedPBSBN_), parent );
		free( parent );
	    }
	    dsgw_form_begin( NULL, NULL );
	    dsgw_emits( "\n<CENTER><TABLE border=2 width=\"100%\"><TR>\n" );
	    dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		"onClick=\"parent.close()\">", XP_GetClientStr(DBT_closeWindow_2) );
	    dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	    dsgw_emit_helpbutton( "ADD_NOPARENT" );
	    dsgw_emits( "\n</TABLE></CENTER></FORM>\n" );
	    dsgw_html_end();
	} else {
	    /*
	     * The parent exists, or the user is adding the entry whose DN
	     * is the same as the suffix for which the gateway is configured.
	     * Display the "add entry" form.
	     */

	    if ( tmplname == NULL ) {
#ifdef DSGW_DEBUG
                dsgw_log( "NULL tmplname\n" );
#endif
		dsgw_error( DSGW_ERR_MISSINGINPUT,
			XP_GetClientStr(DBT_missingTemplate_),
			DSGW_ERROPT_EXIT, 0, NULL );
	    }

	    tip = dsgw_display_init( DSGW_TMPLTYPE_DISPLAY, tmplname, options );
     
	    dsgw_display_entry( tip, ld, NULL, NULL, dn );
	    dsgw_display_done( tip );
	}
    }

    ldap_unbind( ld );
}










