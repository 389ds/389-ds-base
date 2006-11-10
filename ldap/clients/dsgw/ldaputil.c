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
 * ldaputil.c -- LDAP utility functions -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"
#include "../../include/disptmpl.h"
#ifndef NO_LIBLCACHE
#include <lcache.h>
#endif
#if XP_WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
#include "libadminutil/distadm.h"

static dsgwtmplinfo *init_listdisplay( char *tmplname, unsigned long options );
static int do_search( dsgwtmplinfo *tip, LDAP *ld, char *base, int scope,
	char *filter, LDAPMessage **msgpp );
static void handle_search_results( dsgwtmplinfo *tip, LDAP *ld, int rc,
	LDAPMessage *msgp, unsigned long options );
static int LDAP_CALL LDAP_CALLBACK
	get_rebind_credentials( LDAP *ld, char **whop, char **credp,
	int *methodp, int freeit, void *arg );
static void strcpy_special_undo( char *d, char *s );
static int entry2htmlwrite( void *fp, char *buf, int len );
static void emit_one_loc_dn( char *dn, char *friendlyname, char *rootname,
	int only_one );
static char *uid2dn( LDAP *ld, char *uid, char *base, int *ldaprc,
	char **lderrtxtp, char **errsp );
static void return_one_attr( LDAP *ld, LDAPMessage *entry, char *attrtype,
	char *mimetype, int valindex );
static void break_up_one_attr( char *attr, char **attrtypep, char **mimetypep,
	int *valindexp );

/* binddn and bindpasswd are used in get_rebind_credentials() */
static char *binddn = NULL, *bindpasswd = NULL;

#ifndef DSGW_NO_SSL
/*static CERTCertDBHandle certdbh;*/
static char * certdbh;

#endif

/*
 * initialize various LDAP library things -- any non-NULL parameters are
 *	initialized and set.  If an error occurs, this function will not
 *	return at all.
 * If an LDAP connection was opened, this function will return either
 * DSGW_BOUND_ASUSER if a valid cookie was found in the environment
 * and we were able to bind to the directory as that user.  If no
 * cookie was found, or the cookie would not be used to bind, then
 * an anonymous bind is performed and DSGW_BOUND_ANONYMOUS is returned.
 * If skipac (skip authentication check) is non-zero, then this
 * function will always authenticate as NULL.
 *
 * If we are configured to use a local LDAP database instead of a real
 * directory server, we always do an unauthenticated bind but we return
 * DSGW_BOUND_ASUSER.  This is done to keep our CGIs that check for a
 * return code of DSGW_BOUND_ASUSER happy.
 *
 * If skipauthwarning is set, then we don't display the javascript
 * auth warning for searches. - RJP
 */
int
dsgw_init_ldap( LDAP **ldp, LDAPFiltDesc **lfdpp, int skipac, int skipauthwarning )
{
    char	*path;
    char	*userid, *dn, *rndstr, *passwd, *cookie, *p;
    int		ret = 0, optval, limit;
#ifdef XP_WIN32
    WSADATA	wsadata;
#endif

    /* LDAP search filters */
    if ( lfdpp != NULL ) {
	path = dsgw_file2path( gc->gc_configdir, DSGW_FILTERFILE );
	if (( *lfdpp = ldap_init_getfilter( path )) == NULL ) {
	    dsgw_error( DSGW_ERR_BADCONFIG, path, DSGW_ERROPT_EXIT, 0, NULL );
	}
	free( path );
	ret =  0;
    }

#ifdef XP_WIN32

    if( ret = WSAStartup(0x0101, &wsadata ) != 0 )
	dsgw_error( DSGW_ERR_WSAINIT, NULL, DSGW_ERROPT_EXIT, 0, NULL );

#endif /* XP_WIN32 */

    /* LDAP connection */
    if ( ldp != NULL ) {
	if ( gc == NULL ) {
	    dsgw_error( DSGW_ERR_INTERNAL, 
		    XP_GetClientStr(DBT_ldapInitLcacheInitAttemptedBefor_),
		    DSGW_ERROPT_EXIT, 0, NULL );
	}
	if ( gc->gc_localdbconf == NULL ) {
	    /* "Real LDAP server" case */
#ifdef DSGW_NO_SSL
	    *ldp = ldap_init( gc->gc_ldapserver, gc->gc_ldapport );
#else /* DSGW_NO_SSL */
	    if ( gc->gc_ldapssl ) {
		if ( gc->gc_securitypath == NULL ) {
		    dsgw_error( DSGW_ERR_NOSECPATH, NULL, DSGW_ERROPT_EXIT,
			    0, NULL );
		}
		if ( ldapssl_client_init( gc->gc_securitypath,
			&certdbh ) < 0 ) {
		    dsgw_error( DSGW_ERR_SSLINIT, gc->gc_securitypath,
			    DSGW_ERROPT_EXIT, 0, NULL );
		}
		*ldp = ldapssl_init( gc->gc_ldapserver, gc->gc_ldapport, 1 );
		dsgw_NSSInitializedAlready = 1;
	    } else {
		*ldp = ldap_init( gc->gc_ldapserver, gc->gc_ldapport );
	    }
#endif /* !DSGW_NO_SSL */
	    if ( *ldp == NULL ) {
		dsgw_error( DSGW_ERR_LDAPINIT, NULL, DSGW_ERROPT_EXIT, 0,
			NULL );
	    }

	} 
#ifndef NO_LIBLCACHE
else {
	    /* Local DB case */
	    if (( *ldp = ldap_init( NULL, 0 )) == NULL ) {
		dsgw_error( DSGW_ERR_LDAPINIT, NULL, DSGW_ERROPT_EXIT, 0,
			NULL );
	    }
	    if ( lcache_init( *ldp, gc->gc_localdbconf ) != 0 ) {
		dsgw_error( DSGW_ERR_LCACHEINIT, strerror(errno),
			DSGW_ERROPT_EXIT, 0, NULL );
	    }
	    optval = 1;
	    (void) ldap_set_option( *ldp, LDAP_OPT_CACHE_ENABLE, &optval );
	    optval = LDAP_CACHE_LOCALDB;
	    (void) ldap_set_option( *ldp, LDAP_OPT_CACHE_STRATEGY, &optval );
	}
#endif
	rndstr = dn = NULL;
	passwd = dsgw_get_cgi_var( "passwd", DSGW_CGIVAR_OPTIONAL );

	if (( p = dsgw_get_cgi_var( "ldapsizelimit", DSGW_CGIVAR_OPTIONAL ))
		!= NULL ) {
	    limit = atoi( p );
	    (void) ldap_set_option( *ldp, LDAP_OPT_SIZELIMIT, &limit );
	}

	if (( p = dsgw_get_cgi_var( "ldaptimelimit", DSGW_CGIVAR_OPTIONAL ))
		!= NULL ) {
	    limit = atoi( p );
	    (void) ldap_set_option( *ldp, LDAP_OPT_TIMELIMIT, &limit );
	}

	/*
	 * we don't bother with authentication if:
	 *  the "skipac" flag is non-zero OR
	 *  no "passwd" form element was passed in and we are using local db
	 */
	if ( !skipac && ( passwd != NULL || gc->gc_localdbconf == NULL )) {
	    /*
	     * There are several ways in which authentication might
	     * happen.
	     */
	    if ( gc->gc_admserv ) {
		/*
		 * We're running under the admin server, so ask libadmin
		 * for the user's credentials.  If a password comes as a form
		 * field, it overrides value we get from admin server
		 */
		(void)dsgw_get_adm_identity( *ldp, &userid, &dn,
			( passwd == NULL ) ? &passwd : NULL, DSGW_ERROPT_EXIT );

#ifdef DSGW_DEBUG
		dsgw_log( "dsgw_init_ldap: run under admserv, user id = %s, "
			"dn = %s, passwd = %s, skipac = %d, dn = 0x%x\n",
			userid == NULL ? "NULL" : userid,
			dn == NULL ? "NULL" : dn,
			passwd == NULL ? "NULL" : passwd,
			skipac, dn );
#endif
	    } else {
		/*
		 * Not running under admin server.  The DN and password
		 * might come in as form fields, or the authentication
		 * might be accomplished via a client-side cookie which
		 * gets looked up in the gateway's cookie database.
		 */

		/* check for dn/binddn in request */
		if ( passwd != NULL ) {
		    if (( dn = dsgw_get_escaped_cgi_var( "escapedbinddn",
			    "binddn", DSGW_CGIVAR_OPTIONAL )) == NULL &&
			    ( dn = dsgw_get_cgi_var( "dn",
			    DSGW_CGIVAR_OPTIONAL )) == NULL ) {
			free( passwd );
			passwd = NULL;
		    } else {
			/* got DN:  undo extra level of escaping */
			dsgw_form_unescape( dn );
		    }
		}

		if ( passwd == NULL ) {
		    /* Check for a valid authentication cookie */
		    cookie = dsgw_get_auth_cookie();
		    if ( cookie != NULL ) {
			if ( dsgw_parse_cookie( cookie, &rndstr, &dn ) == 0 ) {
			    int ckrc;
			    if (( ckrc = dsgw_ckdn2passwd( rndstr, dn,
				    &passwd )) != 0 ) {

				passwd = NULL;
				dn = NULL;
				/* 
				 * Delete the cookie and print out the error message.
				 * dn2passwd_error() returns 1 if the CGI should exit,
				 * 0 if it should continue.
				 */
				if (dsgw_dn2passwd_error( ckrc, skipauthwarning )) {
				    exit( 0 );
				}

			    }
			}
		    }

		    if ( rndstr != NULL ) {
			free( rndstr );
		    }
		    if ( cookie != NULL ) {
			free( cookie );
		    }
		}
	    }
	}

	/*
	 * try to use LDAP version 3 but fall back to v2 if bind fails
	 */
	optval = LDAP_VERSION3;
	(void)ldap_set_option( *ldp, LDAP_OPT_PROTOCOL_VERSION, &optval );

	/*
	 * If everything above failed to set the dn/password, then use
	 * the binddn and bindpw, if any.
	 */
	if (dn == NULL && passwd == NULL && 
	    strlen(gc->gc_binddn) > 0 && strlen(gc->gc_bindpw) > 0) {
	  dn = dsgw_ch_strdup(gc->gc_binddn);
	  passwd = dsgw_ch_strdup(gc->gc_bindpw);
	}

	if (( ret = ldap_simple_bind_s( *ldp, dn, passwd ))
	    == LDAP_PROTOCOL_ERROR ) {
		optval = LDAP_VERSION2;
		(void)ldap_set_option( *ldp, LDAP_OPT_PROTOCOL_VERSION,
			&optval );
		ret = ldap_simple_bind_s( *ldp, dn, passwd );
	}

	if ( ret != LDAP_SUCCESS ){
	    dsgw_ldap_error( *ldp, DSGW_ERROPT_DURINGBIND );

	    /* Display back button */
	    dsgw_form_begin( NULL, NULL );
	    dsgw_emits( "\n<CENTER><TABLE border=2 width=\"100%\"><TR>\n" );
	    dsgw_emits( "<TD WIDTH=\"100%\" ALIGN=\"center\">\n" );
	    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		       "onClick=\"history.back()\">\n",
		       XP_GetClientStr(DBT_goBack_) );
	    dsgw_emits( "\n</TABLE></CENTER></FORM>\n" );
	    exit(0);
	}

	if (( dn != NULL ) && ( passwd != NULL )) {
	    ret = DSGW_BOUND_ASUSER;
	    binddn = dn;
	    bindpasswd = passwd;
	    ldap_set_rebind_proc( *ldp, get_rebind_credentials, NULL );
	} else if ( gc->gc_localdbconf != NULL ) {
	    ret = DSGW_BOUND_ASUSER;	/* a small, harmless lie */
	} else {
	    ret = DSGW_BOUND_ANONYMOUS;
	}

    }
    return ret;
}


/* 
 * get user identity from the admin. server (if running under it)
 *	if uidp is non-NULL, it is set to point to user's login id.
 *	if dnp is non-NULL, it is set to point to user's DN.
 *	if pwdp is non-NULL, it is set to point to user's password.
 * Returns: 0 if all goes well, -1 if an error occurs.
 *
 * Note that ld is used only if dnp != NULL, and then only if the admin server
 * returns NULL when asked for the DN.
 */
int
dsgw_get_adm_identity( LDAP *ld, char **uidp, char **dnp, char **pwdp,
	int erropts  )
{
    int		rc, need_to_get_dn;
    char	*uid;
    static int	adm_inited = 0;

    if ( !gc->gc_admserv ) {
	dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL,
		XP_GetClientStr(DBT_notRunningUnderTheAdministration_),
		erropts, 0, NULL );
	return( -1 );
    }

    if ( !adm_inited ) {
	if ( ADM_InitializePermissions( &rc ) < 0 ) {
	    dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL,
		    XP_GetClientStr(DBT_couldNotInitializePermissions_),
		    erropts, 0, NULL );
	    return( -1 );
	}
	adm_inited = 1;
    }

    need_to_get_dn = ( dnp != NULL );

    if ( need_to_get_dn && ADM_GetUserDNString( &rc, dnp ) < 0 ) {
	dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL,
		XP_GetClientStr(DBT_couldNotMapUsernameToADnErrorFro_),
		erropts, 0, NULL );
	return( -1 );
    }

    /*
     * get userid if:
     *    1. requested by caller (uidp != NULL)
     * or 2. DN was requested but Admin Server didn't return the DN
     */
    if (( uidp != NULL || ( need_to_get_dn && *dnp == NULL )) &&
	    ( ADM_GetCurrentUsername( &rc, &uid ) < 0 || uid == NULL )) {
	dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL,
		XP_GetClientStr(DBT_couldNotGetCurrentUsername_), erropts,
		0, NULL );
	return( -1 );
    }

    if ( uidp != NULL ) {
	*uidp = uid;
    }

    if ( need_to_get_dn && *dnp == NULL ) {
	/*
	 * try to map userid to DN using LDAP search
	 */
	int	lderr;
	char	*errstr, *lderrtxt;

	if (( *dnp = uid2dn( ld, uid, gc->gc_ldapsearchbase, &lderr,
		&lderrtxt, &errstr )) == NULL ) {
	    dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL, errstr, erropts, lderr,
		    lderrtxt );
	    return( -1 );
	}
    }

    if ( pwdp != NULL && ADM_GetCurrentPassword( &rc, pwdp ) < 0 ) {
	dsgw_error( DSGW_ERR_ADMSERV_CREDFAIL,
		XP_GetClientStr(DBT_couldNotGetCurrentUserPassword_), erropts,
		0, NULL );
	return( -1 );
    }

    return( 0 );
}


void
dsgw_ldap_error( LDAP *ld, int erropts )
{
    int		lderr;
    char	*lderrtxt = NULL;

    lderr = ldap_get_lderrno( ld, NULL, &lderrtxt );
    dsgw_error( DSGW_ERR_LDAPGENERAL, dsgw_ldaperr2string( lderr ),
		erropts, lderr, lderrtxt );
}


struct ldap_searchobj *
dsgw_type2searchobj( struct ldap_searchobj *solistp, char *type )
{
    struct ldap_searchobj	*sop;

    for ( sop = ldap_first_searchobj( solistp ); sop != NULL;
	    sop = ldap_next_searchobj( solistp, sop )) {
	if ( strcasecmp( type, sop->so_objtypeprompt ) == 0 ) {
	    return( sop );
	}
    }

    return( NULL );
}


struct ldap_searchattr *
dsgw_label2searchattr( struct ldap_searchobj *sop, char *label )
{
    struct ldap_searchattr *sap;

    for ( sap = sop->so_salist; sap != NULL; sap = sap->sa_next ) {
	if ( strcasecmp( label, sap->sa_attrlabel ) == 0 ) {
	    return( sap );
	}
    }

    return( NULL );
}


struct ldap_searchmatch *
dsgw_prompt2searchmatch( struct ldap_searchobj *sop, char *prompt )
{
    struct ldap_searchmatch *smp;

    for ( smp = sop->so_smlist; smp != NULL; smp = smp->sm_next ) {
	if ( strcasecmp( prompt, smp->sm_matchprompt ) == 0 ) {
	    return( smp );
	}
    }

    return( NULL );
}


static dsgwtmplinfo *
init_listdisplay( char *tmplname, unsigned long options )
{
    char	*s;

    if (( s = dsgw_get_cgi_var( "listtemplate", DSGW_CGIVAR_OPTIONAL ))
	    != NULL ) {
	tmplname = s;
    }

    return( dsgw_display_init( DSGW_TMPLTYPE_LIST, tmplname, options ));
}


void
dsgw_smart_search( LDAP *ld, struct ldap_searchobj *sop, LDAPFiltDesc *lfdp,
	char *base, char *value, unsigned long options )
{
    int			rc;
    LDAPFiltInfo	*lfip;
    dsgwtmplinfo	*tip;
    LDAPMessage		*msgp;

    ldap_setfilteraffixes( lfdp, sop->so_filterprefix, NULL );
    tip = init_listdisplay( sop->so_objtypeprompt, options );

    if (( lfip = ldap_getfirstfilter( lfdp, sop->so_filtertag, value ))
	    == NULL ) {
	dsgw_error( DSGW_ERR_NOFILTERS, sop->so_objtypeprompt,
		DSGW_ERROPT_EXIT, 0, NULL );
    }

    for ( ; lfip != NULL; lfip = ldap_getnextfilter( lfdp )) {
	dsgw_set_searchdesc( tip, NULL, lfip->lfi_desc, value );

	rc = do_search( tip, ld, base, sop->so_defaultscope, lfip->lfi_filter,
		&msgp );

	if ( rc != LDAP_SUCCESS ||
		( msgp != NULL && ldap_count_entries( ld, msgp ) > 0 )) {
	    if ( strstr( lfip->lfi_filter, "~=" ) != NULL ) {
		/* always list if approximate filter used to find entry */
		options |= DSGW_DISPLAY_OPT_LIST_IF_ONE;
	    }
	    break;	/* error or got some entries:  stop searching */
	}
    }

    handle_search_results( tip, ld, rc, msgp, options );
}


void
dsgw_pattern_search( LDAP *ld, char *listtmpl,
	char *searchdesc2, char *searchdesc3, char *searchdesc4,
	char *filtpattern, char *filtprefix, char *filtsuffix, char *attr,
	char *base, int scope, char *value, unsigned long options )
{
    char		buf[ 4096 ];
    int			rc;
    dsgwtmplinfo	*tip;
    LDAPMessage		*msgp;

    tip = init_listdisplay( listtmpl, options );

    ldap_build_filter( buf, sizeof( buf ), filtpattern,
	    filtprefix, filtsuffix, attr, value, NULL );

    dsgw_set_searchdesc( tip, searchdesc2, searchdesc3, searchdesc4 );

    rc = do_search( tip, ld, base, scope, buf, &msgp );
    handle_search_results( tip, ld, rc, msgp, options );
}


/*
 * Perform URL-based search.
 * Note that if "ld" is NULL, this routine sets gc->gc_ldapserver and
 * gc->gc_ldapport globals itself, calls dsgw_init_ldap(), and then does
 * the URL-based search.  If "ld" is not NULL, no initialization is done
 * here.
 */
void
dsgw_ldapurl_search( LDAP *ld, char *ldapurl )
{
    int			rc, ec, saveport, did_init_ldap;
    LDAPMessage		*msgp;
    LDAPURLDesc		*ludp;
    char		*saveserver;
    unsigned long	no_options = 0;
    int                 one_attr = 0;

    if (( rc = ldap_url_parse( ldapurl, &ludp )) != 0 ) {
	switch ( rc ) {
	case LDAP_URL_ERR_NODN:
	    ec = DSGW_ERR_LDAPURL_NODN;
	    break;
	case LDAP_URL_ERR_BADSCOPE:
	    ec = DSGW_ERR_LDAPURL_BADSCOPE;
	    break;
	case LDAP_URL_ERR_MEM:
	    ec = DSGW_ERR_NOMEMORY;
	    break;
	case LDAP_URL_ERR_NOTLDAP:
	default:
	    ec = DSGW_ERR_LDAPURL_NOTLDAP;
	    break;
	}
	dsgw_error( ec, ldapurl, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( ld == NULL ) {
	saveserver = gc->gc_ldapserver;
	gc->gc_ldapserver = ludp->lud_host;
	saveport = gc->gc_ldapport;
	gc->gc_ldapport = ludp->lud_port;
	one_attr = ( ludp->lud_attrs != NULL && ludp->lud_attrs[ 0 ] != NULL && ludp->lud_attrs[ 1 ] == NULL );
	(void)dsgw_init_ldap( &ld, NULL, 0, one_attr );
	did_init_ldap = 1;
    } else {
	did_init_ldap = 0;
    }

    /* XXX a bit of a hack:  if it looks like only a DN was included, we
     * assume that a read of the entry is desired.
     */
    if ( ludp->lud_scope == LDAP_SCOPE_BASE && strcasecmp( ludp->lud_filter,
	    "(objectClass=*)" ) == 0 ) {
	dsgw_read_entry( ld, ludp->lud_dn, NULL, NULL, ludp->lud_attrs,
		no_options );
    } else {
	dsgwtmplinfo	*tip;

	dsgw_send_header();
	tip = init_listdisplay( "urlsearch", no_options );
	dsgw_set_searchdesc( tip, NULL, XP_GetClientStr(DBT_theLDAPFilterIs_), ldapurl );
	rc = do_search( tip, ld, ludp->lud_dn, ludp->lud_scope,
		ludp->lud_filter, &msgp );
	handle_search_results( tip, ld, rc, msgp, no_options );
    }

    if ( did_init_ldap ) {
	ldap_unbind( ld );
	gc->gc_ldapserver = saveserver;
	gc->gc_ldapport = saveport;
    }
}


/*
 * do the actual search over LDAP.  Return an LDAP error code.
 */
static int
do_search( dsgwtmplinfo *tip, LDAP *ld, char *base, int scope, char *filter,
	LDAPMessage **msgpp )
{
    char		**attrlist, *attrs[ 3 ];

    *msgpp = NULL;

    if ( tip == NULL || tip->dsti_attrs == NULL ) {
	attrs[ 0 ] = DSGW_ATTRTYPE_OBJECTCLASS;
	if ( tip != NULL && tip->dsti_sortbyattr != NULL ) {
	    attrs[ 1 ] = tip->dsti_sortbyattr;
	    attrs[ 2 ] = NULL;
	} else {
	    attrs[ 1 ] = NULL;
	}
	attrlist = attrs;
    } else {
	attrlist = tip->dsti_attrs;
    }
#ifdef DSGW_DEBUG
    dsgw_log ("ldap_search_s(ld,\"%s\",%i,\"%s\")\n", base, scope, filter);
#endif
    return( ldap_search_s( ld, base, scope, filter, attrlist, 0, msgpp ));
}


static int
is_subtype( const char *sub, const char *sup )
{
    auto const size_t subLen = strlen( sub );
    auto const size_t supLen = strlen( sup );
    if ( subLen < supLen ) return 0;
    if ( subLen == supLen ) return !strcasecmp( sub, sup );
    if ( sub[supLen] != ';' ) return 0;
    return !strncasecmp( sub, sup, strlen( sup ));
}

static const struct berval* LDAP_C LDAP_CALLBACK
dsgw_keygen( void *arg, LDAP *ld, LDAPMessage *entry )
{
    auto const char* sortbyattr = (char*)arg;
    auto struct berval* result = NULL;

    if (sortbyattr == NULL) {	/* sort by DN */
	auto char* DN = ldap_get_dn( ld, entry );
	if (DN) {
	    result = dsgw_strkeygen( CASE_INSENSITIVE, DN );
	    ldap_memfree( DN );
	}
    } else {
	auto char* attr;
	auto BerElement *ber;
	for (attr = ldap_first_attribute( ld, entry, &ber ); attr != NULL; 
	     attr = ldap_next_attribute ( ld, entry, ber ) ) {
	    auto char **vals;
	    if ( is_subtype( attr, sortbyattr ) &&
		NULL != ( vals = ldap_get_values( ld, entry, attr ))) {
		auto size_t i;
		for ( i = 0; vals[i] != NULL; ++i ) {
		    auto struct berval* key = dsgw_strkeygen( CASE_INSENSITIVE, vals[i] );
		    if ( result == NULL || dsgw_keycmp( NULL, key, result ) < 0 ) {
			auto struct berval* tmp = result;
			result = key;
			key = tmp;
#ifdef DSGW_DEBUG
			{
			    auto char* ev = dsgw_strdup_escaped( vals[i] );
			    auto char* DN = ldap_get_dn( ld, entry );
			    dsgw_log( "dsgw_keygen(%s,%s) %p %s\n", sortbyattr, DN, (void*)result, ev );
			    ldap_memfree( DN );
			    free( ev );
			}
#endif
		    }
		    if ( key != NULL ) {
			dsgw_keyfree( arg, key );
		    }
		}
		ldap_value_free( vals );
	    }
	    ldap_memfree( attr );
	}
	if ( ber != NULL ) {
	    ldap_ber_free( ber, 0 );
	}
    }
    return result ? result : /* no such attribute */ dsgw_key_last;
}

static void
handle_search_results( dsgwtmplinfo *tip, LDAP *ld, int rc, LDAPMessage *msgp,
	unsigned long options )
{
    int		count;
    LDAPMessage	*entry;
    char	*dn, *errortext, *lderrtxt, **ocvals;

    count = ( msgp == NULL ) ? 0 : ldap_count_entries( ld, msgp );
    if ( rc == LDAP_SUCCESS ) {
	errortext = NULL;
	lderrtxt = NULL;
    } else {
	errortext = dsgw_ldaperr2string( rc );
	(void)ldap_get_lderrno( ld, NULL, &lderrtxt );
    }
    dsgw_set_search_result( tip, count, errortext, lderrtxt );

    if ( count > 0 ) {
	entry = ldap_first_entry( ld, msgp );

	if ( count == 1 && ( options & DSGW_DISPLAY_OPT_LIST_IF_ONE ) == 0 ) {
	    /* found exactly one entry:  read and display it */
	    dn = ldap_get_dn( ld, entry );
	    ocvals = ldap_get_values( ld, entry, DSGW_ATTRTYPE_OBJECTCLASS );
	    ldap_msgfree( msgp );

	    dsgw_read_entry( ld, dn, ocvals, NULL, NULL, options );

	    if ( ocvals != NULL ) {
		ldap_value_free( ocvals );
	    }
	    return;
	}

	/* list entries */
#ifdef DSGW_DEBUG
	dsgw_log( "handle_search_results: sort entries by %s\n",
		  tip->dsti_sortbyattr ? tip->dsti_sortbyattr : "DN" );
#endif
	ldap_keysort_entries( ld, &msgp, tip->dsti_sortbyattr,
		dsgw_keygen, dsgw_keycmp, dsgw_keyfree );
	for ( entry = ldap_first_entry( ld, msgp ); entry != NULL;
		entry = ldap_next_entry( ld, entry )) {
	    dsgw_display_entry( tip, ld, entry, NULL, NULL );
	}
	if ( options & DSGW_DISPLAY_OPT_DNLIST_JS ) {
	    int i;
	    char *edn, *js0, *js1;
	    char **xdn;
	    char **sn;

	    dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	    dsgw_emits( "var dnlist = new Array;\n" );
	    for ( i = 0, entry = ldap_first_entry( ld, msgp ); entry != NULL;
		    i++, entry = ldap_next_entry( ld, entry )) {
		dn = ldap_get_dn( ld, entry );
		edn = dsgw_strdup_escaped( dn );
		xdn = ldap_explode_dn( dn, 1 );
		dsgw_emitf( "dnlist[%d] = new Object\n", i );
		dsgw_emitf( "dnlist[%d].edn = '%s';\n", i, edn );
		js0 = dsgw_escape_quotes( xdn[ 0 ] );
		if ( xdn[1] != NULL ) {
		    js1 = dsgw_escape_quotes( xdn[ 1 ] );
		    dsgw_emitf( "dnlist[%d].rdn = '%s, %s';\n", i, js0, js1 );
		    free( js1 );
		} else {
		    dsgw_emitf( "dnlist[%d].rdn = '%s';\n", i, js0 );
		}
		free( js0 );
		if (( sn = ldap_get_values( ld, entry, "sn" )) == NULL ) {
		    js0 = NULL;
		} else {
		    js0 = dsgw_escape_quotes( sn[ 0 ] );
		    ldap_value_free( sn );
		}
		dsgw_emitf( "dnlist[%d].sn = '%s';\n", i, ( js0 == NULL ) ?
			" " : js0 );
		if ( js0 != NULL ) {
		    free( js0 );
		}

		dsgw_emitf( "dnlist[%d].selected = false;\n", i );
		free( edn );
		ldap_value_free( xdn );
		ldap_memfree( dn );
	    }
	    dsgw_emitf( "dnlist.count = %d;\n", i );
	    dsgw_emitf( "</SCRIPT>\n" );
	}
	ldap_msgfree( msgp );
    } else {
	/* Count <= 0 */
	if ( options & DSGW_DISPLAY_OPT_DNLIST_JS ) {
	    dsgw_emitf( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	    dsgw_emitf( "var dnlist = new Array;\n" );
	    dsgw_emitf( "dnlist.count = 0;\n" );
	    dsgw_emitf( "</SCRIPT>\n" );
	}
    }

    dsgw_display_done( tip );
}


/*
 * read and display a single entry.  If ocvals is non-NULL, it should
 * contain the list of objectClass values for this entry.
 */ 
void
dsgw_read_entry( LDAP *ld, char *dn, char **ocvals, char *tmplname,
	char **attrs, unsigned long options )
{
    int			rc, one_attr, freeocvals, valindex;
    char		*tmpattr, *attr0, *mimetype;
    LDAPMessage		*msgp, *entry, *aomsgp, *aoentry;
    dsgwtmpl		*tmpl;
    dsgwtmplinfo	*tip;

    if (( options & DSGW_DISPLAY_OPT_AUTH ) != 0 ) {
	/*
	 * XXX hack -- if we are trying to authenticate, we don't generate an
	 * entry display at all.  Instead, we generate an authenticate form.
	 */
	dsgw_send_header();
	dsgw_emit_auth_form( dn );
	return;
    }

    one_attr = ( attrs != NULL && attrs[ 0 ] != NULL && attrs[ 1 ] == NULL );
    if ( one_attr ) {
	break_up_one_attr( attrs[ 0 ], &tmpattr, &mimetype, &valindex );
	if ( strcasecmp( tmpattr, "_vcard" ) == 0 ) {	/* VCards are special */
	    dsgw_vcard_from_entry( ld, dn, mimetype );
	    return;
	}
	attr0 = attrs[ 0 ];     /* replace first & only attr. */
	attrs[ 0 ] = tmpattr;
    } else {
	attr0 = NULL;
    }

    if ( tmplname == NULL && ( tmplname = dsgw_get_cgi_var( "displaytemplate",
	    DSGW_CGIVAR_OPTIONAL )) == NULL && attrs == NULL ) {
	/* determine what display template to use based on objectClass values */
	freeocvals = 0;
	if ( ocvals == NULL ) {	/* read entry to get objectClasses */
	    char	*attrs[ 2 ];

	    attrs[ 0 ] = DSGW_ATTRTYPE_OBJECTCLASS;
	    attrs[ 1 ] = NULL;

	    if (( rc = ldap_search_s( ld, dn, LDAP_SCOPE_BASE, "objectClass=*",
		    attrs, 0, &msgp )) != LDAP_SUCCESS ||
		    ( entry = ldap_first_entry( ld, msgp )) == NULL ) {
		dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
	    }
	    ocvals = ldap_get_values( ld, msgp, DSGW_ATTRTYPE_OBJECTCLASS );
	    freeocvals = 1;
	    ldap_msgfree( msgp );
	}


	if ( ocvals == NULL || ( tmpl = dsgw_oc2template( ocvals )) == NULL ) {
	    tmplname = NULL;
	} else {
	    tmplname = tmpl->dstmpl_name;
	}

	if ( freeocvals ) {
	    ldap_value_free( ocvals );
	}
    }

    if ( tmplname == NULL ) {
	tip = NULL;

	if ( !one_attr ) {
	    char	*title;

	    if (( title = ldap_dn2ufn( dn )) == NULL ) {
		title = dn;
	    }
	    dsgw_send_header();
	    dsgw_html_begin( title, 1 );
	    dsgw_emitf( "<FONT SIZE=\"+1\">\n%s\n</FONT>\n",
	    XP_GetClientStr(DBT_noteThereIsNoDisplayTemplateForT_) );
	} 

    } else if (( tip = dsgw_display_init( DSGW_TMPLTYPE_DISPLAY, tmplname,
	    options )) != NULL ) {
	dsgw_send_header();
	attrs = tip->dsti_attrs;
    }

    /* now read the attributes needed for the template */
    if (( rc = ldap_search_s( ld, dn, LDAP_SCOPE_BASE, "objectClass=*",
	    attrs, 0, &msgp )) != LDAP_SUCCESS ) {
	dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
    }

    if (( entry = ldap_first_entry( ld, msgp )) == NULL ) {
	ldap_msgfree( msgp );
	dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
    }

    /* and retrieve attribute types only if we need any of them */
    if ( one_attr || tip == NULL || tip->dsti_attrsonly_attrs == NULL ) {
	aomsgp = NULL;
    } else {
	if (( rc = ldap_search_s( ld, dn, LDAP_SCOPE_BASE, "objectClass=*",
		tip->dsti_attrsonly_attrs, 1, &aomsgp )) != LDAP_SUCCESS ) {
	    dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
	}

	/*
	 * if no entries were returned, "aoentry" will be set to NULL by the
	 * next statement.  We don't treat that as an error since we know the
	 * entry exists.  It probably just means none of the "attrsonly" types
	 * were present in the entry.
	 */
	aoentry = ldap_first_entry( ld, aomsgp );
    }

    /* display it (finally!) */
    if ( one_attr ) {
	return_one_attr( ld, entry, attrs[ 0 ], mimetype, valindex );
    } else if ( tip == NULL ) {
	/* no template available -- display in an ugly but complete manner */
	if (( rc = ldap_entry2html( ld, NULL, entry, NULL, NULL, NULL,
		entry2htmlwrite, stdout, "\n", 0, LDAP_DISP_OPT_HTMLBODYONLY,
		NULL, NULL )) != LDAP_SUCCESS ) {
	    dsgw_ldap_error( ld, DSGW_ERROPT_EXIT );
	}
	dsgw_html_end();
    } else {
	/* use template to create a nicely formatted display */
	dsgw_display_entry( tip, ld, entry, aoentry, NULL );
	dsgw_display_done( tip );
    }

    if ( attr0 != NULL ) {
	attrs[ 0 ] = attr0;     /* if we replaced this, put original back */
    }

    if ( msgp != NULL ) {
	ldap_msgfree( msgp );
    }
    if ( aomsgp != NULL ) {
	ldap_msgfree( aomsgp );
    }
}


/*
 * return 1 if the entry already exists, 0 if not, -1 if some error occurs
 */
int
dsgw_ldap_entry_exists( LDAP *ld, char *dn, char **matchedp,
	unsigned long erropts )
{
    LDAPMessage *msgp;
    int		rc;

    msgp = NULL;
    if ( matchedp != NULL ) {
	*matchedp = NULL;
    }

    if (( rc = do_search( NULL, ld, dn, LDAP_SCOPE_BASE, "(objectClass=*)",
	    &msgp )) != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT ) {
	dsgw_ldap_error( ld, erropts );
    }

    if ( msgp == NULL || rc == LDAP_NO_SUCH_OBJECT ) {
	rc = 0;
	if ( matchedp != NULL ) {
	    (void)ldap_get_lderrno( ld, matchedp, NULL );
	}
    } else {
	rc = ( ldap_count_entries( ld, msgp ) > 0 ? 1 : 0 );
	ldap_msgfree( msgp );
    }

    return( rc );
}


static int
entry2htmlwrite( void *fp, char *buf, int len )
{
        return( fwrite( buf, len, 1, (FILE *)fp ) == 0 ? -1 : len );
}


/*
 * return 1 if the entry's parent exists, 0 if not, -1 if some error occurs.
 * If the entry is the same as gc->gc_ldapsearchbase, then we return 1,
 * so we don't prevent people from adding their organizational entry.
 */
int
dsgw_ldap_parent_exists( LDAP *ld, char *dn, unsigned long erropts )
{
    LDAPMessage *msgp;
    int		rc;

    /* Is "dn" == gc->gc_ldapsearchbase? */
    msgp = NULL;
    if (( rc = do_search( NULL, ld, dn, LDAP_SCOPE_BASE, "(objectClass=*)",
	    &msgp )) != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT ) {
	dsgw_ldap_error( ld, erropts );
    }

    if ( msgp == NULL ) {
	rc = 0;
    } else {
	rc = ( ldap_count_entries( ld, msgp ) > 0 ? 1 : 0 );
	ldap_msgfree( msgp );
    }

    return( rc );
}



/*
 * this function is called back by LIBLDAP when chasing referrals
 */
static int LDAP_CALL LDAP_CALLBACK
get_rebind_credentials( LDAP *ld, char **whop, char **credp,
	int *methodp, int freeit, void *arg )
{
    if ( !freeit ) {
	*whop = binddn;
	*credp = bindpasswd;
	*methodp = LDAP_AUTH_SIMPLE;
    }

    return( LDAP_SUCCESS );
}


char *
dsgw_get_binddn()
{
    return( binddn );
}

/*
 * return 1 if bound using "dn"
 * return 0 if definitely bound as someone else
 * return "def_answer" is we can't tell for sure
 */
int
dsgw_bound_as_dn( char *dn, int def_answer )
{
    int		i, rc;
    char	**rdns1, **rdns2;

    if ( binddn == NULL ) {
	/*
	 * not authenticated: if not using local db or using it as an
	 * end-user, return the default
	 */
	if ( gc->gc_localdbconf == NULL || gc->gc_enduser ) {
	    return( def_answer );
	}

	/*
         * if using local db as an admin, return "bound as someone else"
	 * since there is no access control enforced anyways.
	 */
	return( 0 );
    }

    /* first try a simple case-insensitive comparison */
    if ( strcasecmp( binddn, dn ) == 0 ) {
	return( 1 );	/* DNs are the same */
    }

    /*
     * These DNs may not have the same spacing or punctuation.  Compare RDN
     * components to eliminate any differences.
     */
    if (( rdns1 = ldap_explode_dn( binddn, 0 )) == NULL ) {
	return( def_answer );	/* we don't know: return the default */
    }

    if (( rdns2 = ldap_explode_dn( dn, 0 )) == NULL ) {
	ldap_value_free( rdns1 );
	return( def_answer );	/* we don't know: return the default */
    }

    for ( i = 0; rdns1[ i ] != NULL && rdns2[ i ] != NULL; ++i ) {
	if ( strcasecmp( rdns1[ i ], rdns2[ i ] ) != 0 ) {
	    break;	/* DNs are not the same */
	}
    }

    rc = ( rdns1[ i ] == NULL && rdns2[ i ] == NULL );

    ldap_value_free( rdns1 );
    ldap_value_free( rdns2 );

    return( rc );
}



/*
 * Compare 2 DNs.  Return 1 if they are equivalent, 0 if not.
 */
int
dsgw_dn_cmp( char *dn1, char *dn2 )
{
    int		i, rc;
    char	**rdns1, **rdns2;

    /* first try a simple case-insensitive comparison */
    if ( dsgw_utf8casecmp( (unsigned char *)dn1, (unsigned char *)dn2 ) == 0 ) {
	return( 1 );	/* DNs are the same */
    }

    /*
     * These DNs may not have the same spacing or punctuation.  Compare RDN
     * components to eliminate any differences.
     */
    if (( rdns1 = ldap_explode_dn( dn1, 0 )) == NULL ) {
	return( 0 );	/* we don't know: return 0 */
    }

    if (( rdns2 = ldap_explode_dn( dn2, 0 )) == NULL ) {
	ldap_value_free( rdns1 );
	return( 0 );	/* we don't know: return 0 */
    }

    for ( i = 0; rdns1[ i ] != NULL && rdns2[ i ] != NULL; ++i ) {
	if ( dsgw_utf8casecmp( (unsigned char *)rdns1[ i ], (unsigned char *)rdns2[ i ] ) != 0 ) {
	    break;	/* DNs are not the same */
	}
    }

    rc = ( rdns1[ i ] == NULL && rdns2[ i ] == NULL );

    ldap_value_free( rdns1 );
    ldap_value_free( rdns2 );

    return( rc );
}


/*
 * Return the parent of dn.  The caller is responsible for freeing the
 * returned value.  Returns NULL on error.
 */
char *
dsgw_dn_parent( char *dn )
{
    char *dnp;
    int i;
    char **rdns;

    if ( dn == NULL ) {
	return( NULL );
    }

    dnp = dsgw_ch_malloc( strlen( dn ));
    dnp[ 0 ] = '\0';
    if (( rdns = ldap_explode_dn( dn, 0 )) == NULL ) {
	return NULL;
    }
    for ( i = 1; rdns[ i ] != NULL; i++ ) {
	strcat( dnp, rdns[ i ] );
	strcat( dnp, "," );
    }
    /* Get rid of the trailing "," we just appended */
    dnp[ strlen( dnp ) - 1 ] = '\0';
    ldap_value_free( rdns );
    return( dnp );
}
    

/*
 * Return 1 if dn1 is the immediate ancestor of dn2, 0 otherwise.
 */
int
dsgw_is_dnparent( char *dn1, char *dn2 )
{
    char *dnp;
    int rc;

    /* A null or zero-length DN cannot have a parent */
    if ( dn2 == NULL || strlen( dn2 ) == 0 ) {
	return 0;
    }

    dnp = dsgw_dn_parent( dn2 );
    rc = dsgw_dn_cmp( dn1, dnp );
    free( dnp );

    return rc;
}


/*
 * return malloc'd array of RDN attribute value pairs
 * each element of the array is a string that looks like:  TAG=VALUE
 * this is used to extract values from the RDN when a new entry is added
 */
char **
dsgw_rdn_values( char *dn )
{
    char	**rdns, **rdncomps, *val;
    int		i;

    if (( rdns = ldap_explode_dn( dn, 0 )) == NULL ) {
	return( NULL );
    }

    rdncomps = ldap_explode_rdn( rdns[0], 0 );
    ldap_value_free( rdns );
    if ( rdncomps == NULL ) {
	return( NULL );
    }

    for ( i = 0; rdncomps[ i ] != NULL; ++i ) {
	if (( val = strchr( rdncomps[ i ], '=' )) == NULL ) {
	    ldap_value_free( rdncomps );
	    return( NULL );
	}
	++val;
	strcpy_special_undo( val, val );	/* undo in place */
    }

    return( rdncomps );
}


/*
 * the following routine was lifted from servers/slapd/ava.c
 * it removes special quoting, etc. from values that appear in an LDAP DN
 */
static void
strcpy_special_undo( char *d, char *s )
{
    int     quote;
 
    quote = 0;
    if ( *s == '"' ) {
	    s++;
	    quote = 1;
    }
    for ( ; *s; LDAP_UTF8INC(s)) {
	switch ( *s ) {
	case '"':
	    break;
	case '\\':
	    s++;
	    /* FALL */
	default:
	    d += LDAP_UTF8COPY (d, s);
	    break;
	}
    }
    *d = '\0'; LDAP_UTF8DEC(d);
    if ( quote && *d == '"' ) {
	*d = '\0';
    }
}


static char *
uid2dn( LDAP *ld, char *uid, char *base, int *ldaprc, char **lderrtxtp,
	char **errsp )
{
    char *attrs[] = { "objectclass", NULL };
    char filtbuf[ 85 ];	/* max of 80 char. uid + "uid=" + zero terminator */
    int rc, count;
    LDAPMessage *result;
    LDAPMessage *e;
    char *dn;
    
    *ldaprc = LDAP_SUCCESS;	/* optimistic */
    *errsp = *lderrtxtp = NULL;

    if ( ld == NULL || uid == NULL || strlen( uid ) > 80 ) {
	*errsp = XP_GetClientStr(DBT_invalidUserIdOrNullLdapHandle_);
	return NULL;
    }
    PR_snprintf( filtbuf, sizeof(filtbuf), "uid=%s", uid );

    if (( rc = ldap_search_s( ld, base, LDAP_SCOPE_SUBTREE, filtbuf,
	    attrs, 1, &result )) != LDAP_SUCCESS ) {
	*ldaprc = rc;
	(void)ldap_get_lderrno( ld, NULL, lderrtxtp );
	return NULL;
    }
    if (( count = ldap_count_entries( ld, result )) != 1 ) {
	/* Search either returned no entries, or more than one entry */
	ldap_msgfree( result );
	if ( count == 0 ) {
	    *errsp = XP_GetClientStr(DBT_noMatchForUserId_);
	} else {
	    *errsp = XP_GetClientStr(DBT_moreThanOneMatchForUserId_);
	}
	return NULL;
    }

    dn = NULL;
    if (( e = ldap_first_entry( ld, result )) == NULL ||
	    ( dn = ldap_get_dn( ld, e )) == NULL ) {
	*ldaprc = ldap_get_lderrno( ld, NULL, NULL );
    }
    ldap_msgfree( result );
    return( dn );
}


/*
 * Emit an HTML "SELECT" object that contains all the o's and ou's that
 * are underneath our default searchbase.  If there are none other than
 * the searchbase, we emit a hidden HTML TEXT object that contains the
 * searchbase and the "prefix" and "suffix" are not used.  The values for
 * the SELECT options and for the TEXT object are all escaped DNs.
 *  
 * Location popup directives look like this:
 *	<-- DS_LOCATIONPOPUP "name=VARNAME" "prefix=PREFIX" "suffix=SUFFIX" -->
 *
 * If "prefix" and/or "suffix" are omitted, they default to "".
 * If "name" is omitted it defaults to "base".
 *
 * If there are "location" directives in the dsgw.conf file, we use those
 * instead of actually searching the directory.
 */
void
dsgw_emit_location_popup( LDAP *ld, int argc, char **argv, int erropts )
{
    char	line[BIG_LINE];
    char	*varname, *prefix, *suffix, *rootname, *dn;
    int		i, count, did_init_ldap;
    LDAPMessage	*res, *e;

    if (( varname = get_arg_by_name( "name", argc, argv )) == NULL ) {
	varname = "base";
    }
    if (( prefix = get_arg_by_name( "prefix", argc, argv )) == NULL ) {
	prefix = "";
    }
    if (( suffix = get_arg_by_name( "suffix", argc, argv )) == NULL ) {
	suffix = "";
    }
    rootname = get_arg_by_name( "rootname", argc, argv );

    did_init_ldap = 0;
    res = NULL;

    if ( gc->gc_newentryloccount > 0 ) {
	count = gc->gc_newentryloccount;
    } else {
	char		*attrs[ 3 ];
	int		rc;

	if ( ld == NULL ) {
	    (void)dsgw_init_ldap( &ld, NULL, 0, 0 );
	    did_init_ldap = 1;
	}
	attrs[ 0 ] = "o";
	attrs[ 1 ] = "ou";
	attrs[ 2 ] = NULL;

	rc = ldap_search_s( ld, gc->gc_ldapsearchbase, LDAP_SCOPE_SUBTREE,
		"(|(objectclass=organization)(objectclass=organizationalunit))",
		attrs, 1, &res );
	if ( rc != LDAP_SUCCESS || res == NULL ) {
	    dsgw_ldap_error( ld, erropts );
	    return;
	}

	count = ldap_count_entries( ld, res );
	if ( gc->gc_ldapsearchbase == NULL || *gc->gc_ldapsearchbase == '\0' ) {
	    ++count;	/* include base DN even if it is "" */
	} else {
	    /*
	     * check to see if search base was one of the entries returned
	     * we want to always list the base entry, so we need to check
	     */
	    for ( e = ldap_first_entry( ld, res ); e != NULL;
		    e = ldap_next_entry( ld, e )) {
		if (( dn = ldap_get_dn( ld, e )) == NULL ) {
		    dsgw_ldap_error( ld, erropts );
		    ldap_msgfree( res );
		    return;
		}

		rc = dsgw_dn_cmp( dn, gc->gc_ldapsearchbase );
		free( dn );
		if ( rc ) {	/* base DN was returned */
		    break;
		}
	    }
	    if ( e == NULL ) {
		++count;	/* include base DN even if was not returned */
	    }
	}
    }

    if ( count > 1 ) {
	util_snprintf( line, sizeof(line), "%s\n<SELECT NAME=\"%s\">\n",
		prefix, varname );
    } else {
	util_snprintf( line, sizeof(line), "<INPUT TYPE=\"hidden\" NAME=\"%s\" ",
		varname );
    }
    dsgw_emits( line );

    if ( gc->gc_newentryloccount > 0 ) {
	for ( i = 0; i < gc->gc_newentryloccount; ++i ) {
	    emit_one_loc_dn( gc->gc_newentrylocs[ i ].dsloc_dnsuffix,
		    gc->gc_newentrylocs[i].dsloc_fullname, rootname,
		    ( count < 2 ));
	}
    } else {
	/* always include the base dn first */
	emit_one_loc_dn( gc->gc_ldapsearchbase, NULL, rootname, ( count < 2 ));

	/* XXXmcs it would be nice to do a more intelligent sort here */
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_emit_location_popup: ldap_sort_entries(NULL)\n" );
#endif
	ldap_sort_entries( ld, &res, NULL, dsgw_strcmp (CASE_INSENSITIVE));

	for ( e = ldap_first_entry( ld, res ); e != NULL;
		e = ldap_next_entry( ld, e )) {
	    if (( dn = ldap_get_dn( ld, e )) == NULL ) {
		dsgw_ldap_error( ld, erropts );
		ldap_msgfree( res );
		return;
	    }

	    if ( !dsgw_dn_cmp( dn, gc->gc_ldapsearchbase )) {
		emit_one_loc_dn( dn, NULL, rootname, ( count < 2 ));
	    }
	    free( dn );
	}
    }

    if ( count > 1 ) {
	util_snprintf( line, sizeof(line), "</SELECT>\n%s\n", suffix );
	dsgw_emits( line );
    }

    if ( res != NULL ) {
	ldap_msgfree( res );
    }
    if ( did_init_ldap ) {
	ldap_unbind( ld );
    }
}


static void
emit_one_loc_dn( char *dn, char *friendlyname, char *rootname, int only_one )
{
    char	*escapeddn, **rdns, line[ BIG_LINE ];

    rdns = NULL;
    escapeddn = dsgw_strdup_escaped( dn );

    if ( !only_one ) {
	dsgw_emits( "<OPTION" );
    }

    if ( friendlyname == NULL ) {	/* use first component of DN */
	if ( *dn == '\0' ) {
	    friendlyname = ( rootname == NULL ? XP_GetClientStr(DBT_theEntireDirectory_)
		    : rootname );
	} else if (( rdns = ldap_explode_dn( dn, 1 )) == NULL
		|| rdns[ 0 ] == NULL ) {
	    friendlyname = dn;
	} else {
	    friendlyname = rdns[ 0 ];
	}
    }

    util_snprintf( line, sizeof(line), " VALUE=\"%s\">%s\n", escapeddn,
	    only_one ? "" : friendlyname );
    free( escapeddn );
    if ( rdns != NULL ) {
	ldap_value_free( rdns );
    }
    dsgw_emits( line );
}


/*
 * Return a MIME document that contains a single value.
 * XXX:  does this really belong in ldaputil.c?
 */
static void
return_one_attr( LDAP *ld, LDAPMessage *entry, char *attrtype, char *mimetype,
	int valindex )
{
    char		*val;
    struct berval	**bvals;
    unsigned long	vlen;

    if (( bvals = ldap_get_values_len( ld, entry, attrtype )) == NULL ) {
	dsgw_error( DSGW_ERR_NOATTRVALUE, attrtype, DSGW_ERROPT_EXIT, 0, NULL );
    }

    if ( valindex > ldap_count_values_len( bvals )) {
	dsgw_error( DSGW_ERR_NOATTRVALUE, attrtype, DSGW_ERROPT_EXIT, 0, NULL );
    }

    val = bvals[ valindex ]->bv_val;
    vlen = bvals[ valindex ]->bv_len;

    fprintf( stdout, "Content-Type: %s\n", mimetype );
    fprintf( stdout, "Content-Length: %ld\n\n", vlen );

#ifdef XP_WIN32
    /* flush any data on stdout before changing the mode */
    fflush( stdout );

    /* set the mode to binary 
       so windows doesn't replace with carriage
       return line feed and mess everything up
    */
    _setmode( _fileno( stdout ), _O_BINARY );
#endif

    fwrite( val, vlen, 1, stdout );

#ifdef XP_WIN32
    /* flush any remaining binary data */
    fflush( stdout );

    /* set the mode back to text */
    _setmode( _fileno( stdout ), _O_TEXT );
#endif

    ldap_value_free_len( bvals );
    free( attrtype );
}


/*
 * The general format of attrtype is:
 *	<attrtype> [ &<mimetype> ] [ &<valindex> ]
 * This routine breaks it up.  Callers should free( *attrtypep ) after they
 * are done using attrtypep and mimetypep.
 */
static void
break_up_one_attr( char *attr, char **attrtypep, char **mimetypep,
	int *valindexp )
{
    char	*p;

    *attrtypep = dsgw_ch_strdup( attr );

    *mimetypep = "text/plain";	/* default */
    *valindexp = 0;		/* default: retrieve first value */

    if (( p = strchr( *attrtypep, '&' )) != NULL ) {
	*p++ = '\0';
	if ( *p != '\0' ) {
	    *mimetypep = p;
	    if (( p = strchr( *mimetypep, '&' )) != NULL ) {
		*p++ = '\0';
		*valindexp = atoi( p );
	    }
	}
    }
}
