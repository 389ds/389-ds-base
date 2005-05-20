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
/*
 * domodify.c -- LDAP modify CGI handler -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

#define DSGW_CHANGETYPE_UNKNOWN	0
#define DSGW_CHANGETYPE_MODIFY	1
#define DSGW_CHANGETYPE_ADD	2
#define DSGW_CHANGETYPE_DELETE	3
#define DSGW_CHANGETYPE_MODRDN	4

static void post_request();
static int entry_modify_or_add( LDAP *ld, char *dn, int add, int *pwdchangedp );
static int entry_delete( LDAP *ld, char *dn );
static int entry_modrdn( LDAP *ld, char *dn, char *newrdn, int deleteoldrdn );
static int gather_passwd_changes( char *dn, LDAPMod ***pmodsp,
	int adding_entry, int *pwdchangedp );
static void modify_error( int lderr, char *lderrtxt );
static void addmodifyop( LDAPMod ***pmodsp, int modop, char *attr,
	char *value, int vlen );
static void remove_modifyops( LDAPMod **pmods, char *attr );
static int starts_with( char *s, char *startswith );
static char **post2multilinevals( char *postedval );
static char **post2vals( char *postedval );
static int require_oldpasswd( char *modifydn );
static int value_is_unique( LDAP *ld, char *dn, char *attr, char *value );
static int	verbose = 0;
static int	quiet = 0;
static int	display_results_inline = 0;


int main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{

    (void)dsgw_init( argc, argv,  DSGW_METHOD_POST );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    post_request();

    exit( 0 );
}


static void
post_request()
{
    LDAP	*ld;
    int		rc, changetype, dnlen, i, passwd_changed, discard_authcreds;
    char	*s, *encodeddn, *dn, *newrdn, *changedesc, **rdns, **oldrdns,
		*jscomp, *entry_name, *new_name, *success_msg;
    char	*old_dn;
    char	buf[ 256 ];
#if 0
    FILE	*genfp;
#endif

    passwd_changed = discard_authcreds = 0;
    s = dsgw_get_cgi_var( "changetype", DSGW_CGIVAR_REQUIRED );
    changedesc = XP_GetClientStr(DBT_Editing_);

    if ( strcasecmp( s, "modify" ) == 0 ) {
	changetype = DSGW_CHANGETYPE_MODIFY;
    } else if ( strcasecmp( s, "add" ) == 0 ) {
	changetype = DSGW_CHANGETYPE_ADD;
	changedesc = XP_GetClientStr(DBT_Adding_);
    } else if ( strcasecmp( s, "delete" ) == 0 ) {
	changetype = DSGW_CHANGETYPE_DELETE;
	changedesc = XP_GetClientStr(DBT_Deleting_);
    } else if ( strcasecmp( s, "modrdn" ) == 0 ) {
	changetype = DSGW_CHANGETYPE_MODRDN;
	changedesc = XP_GetClientStr(DBT_Renaming_);
    } else {
	changetype = DSGW_CHANGETYPE_UNKNOWN;
    }

    encodeddn = dsgw_get_cgi_var( "dn", DSGW_CGIVAR_REQUIRED );

    /* undo extra level of escaping on DN */
    dn = dsgw_ch_strdup( encodeddn );
    dsgw_form_unescape( dn );
    old_dn = dn;

    quiet = dsgw_get_boolean_var( "quiet", DSGW_CGIVAR_OPTIONAL, 0 );

#if 0
    /*
     * If the "genscreen" form variable is set, it is the name of a
     * genscreen-compatible HTML template to display the domodify results
     * within.  We replace the "DS_LAST_OP_INFO" directive with our own
     * "domodify" output.  Presence of "genscreen" also turns on quiet mode.
     */
    if (( s = dsgw_get_cgi_var( "genscreen", DSGW_CGIVAR_OPTIONAL )) != NULL &&
	    dsgw_genscreen_begin( s, &genfp, DRCT_DS_LAST_OP_INFO, 0 ) == 0 ) {
	quiet = display_results_inline = 1;
    }
#endif

    verbose = dsgw_get_boolean_var( "verbose", DSGW_CGIVAR_OPTIONAL, 0 );
    if ( verbose ) {
	quiet = 0;	/* verbose overrides quiet */
    }

    if ( dsgw_init_ldap( &ld, NULL, 0, 0) != DSGW_BOUND_ASUSER ) {
	dsgw_emitf( XP_GetClientStr(DBT_warningNoAuthenticationContinuin_) );
    }

    if ( !quiet ) {
	PR_snprintf( buf, 256,
		XP_GetClientStr(DBT_SDirectoryEntry_), changedesc );
	dsgw_html_begin( buf, 1 );
    } else {
	dsgw_html_begin( NULL, 0 );
    }

    dsgw_emits( "\n<FONT SIZE=+1>\n" );

    rdns = ldap_explode_dn( dn, 1 );
    if ( rdns == NULL || rdns[ 0 ] == NULL ) {
	entry_name = dn;
    } else {
	entry_name = dsgw_ch_strdup( rdns[ 0 ] );
    }
    new_name = success_msg = "";
    dsgw_emitf( "%s <B>%s</B>...\n</FONT>\n\n", changedesc, entry_name );
    if ( rdns != NULL ) {
	ldap_value_free( rdns );
    }

    if ( verbose ) {
	dsgw_emitf( XP_GetClientStr(DBT_PreEntryDnSPrePN_), dn );
    }

    /*
     * For end-user CGIs under admin server, if we're talking to a local DB,
     * then there's no access control, and therefore we need to disallow
     * people from changing entries other than their own.  Do that check right
     * here.
     */
    if ( gc->gc_enduser && gc->gc_localdbconf != NULL ) {
	char *bdn;
	(void)dsgw_get_adm_identity( ld, NULL, &bdn, NULL, DSGW_ERROPT_EXIT );
	/* Make sure DN we're bound as matches the DN being modified */
	if ( dsgw_dn_cmp( dn, bdn ) == 0 ) {
	    /* Not the same - generate an error and bail out */
	    dsgw_error( DSGW_ERR_LOCALDB_PERMISSION_DENIED, NULL,
		    DSGW_ERROPT_EXIT, 0, NULL );
	}
    }

    rc = LDAP_SUCCESS;
    switch( changetype ) {
    case DSGW_CHANGETYPE_MODIFY:
	if ( dsgw_get_boolean_var( "changed_DN", DSGW_CGIVAR_OPTIONAL, 0 )) {
	    /* Collect all the inputs named "replace_DN_attr", where
	       attr is an LDAP attribute type.  Construct an AVA from
	       each such input, and combine the AVAs to form newrdn.
	    */
	    auto int i = 0;
	    auto char *varname, *val;
	    auto size_t newrdn_len;
	    newrdn = NULL;
	    while ( (varname = dsgw_next_cgi_var( &i, &val )) != NULL) {
		if ( starts_with( varname, "replace_" )) {
		    auto char* attr = varname;
		    auto int is_rdn = 0;
		    {
			auto char* p;
			while (( p = strchr( attr, '_' )) != NULL ) {
			    attr = p + 1;
			    if ( starts_with( attr, "DN_" )) {
				is_rdn = 1;
			    } /* ignore any other prefixes */
			}
		    }
		    if (is_rdn && strlen(val) > 0) {
			auto const size_t attrlen = strlen (attr);
			auto const size_t val_len = strlen (val);
			auto const size_t ava_len = attrlen + 1 + val_len;
			auto char* ava;
			if (newrdn == NULL) {
			    ava = newrdn = dsgw_ch_malloc (ava_len + 1);
			    newrdn_len = ava_len;
			} else {
			    newrdn = dsgw_ch_realloc (newrdn, newrdn_len + ava_len + 2);
			    memcpy (newrdn + newrdn_len, "+", 1);
			    ava = newrdn + newrdn_len + 1;
			    newrdn_len += (ava_len + 1);
			}
			memcpy (ava, attr, attrlen);
			memcpy (ava + attrlen, "=", 1);
			memcpy (ava + attrlen + 1, val, val_len + 1);
		    }
		}
		free (varname);
	    }
	    if (newrdn) goto continue_modrdn;
	    /* else failed to compute newrdn */
	}
	break;
    case DSGW_CHANGETYPE_MODRDN:
	newrdn = dsgw_get_cgi_var( "newrdn", DSGW_CGIVAR_REQUIRED );
    continue_modrdn:
	dsgw_remove_leading_and_trailing_spaces( &newrdn );
	rc = entry_modrdn( ld, dn, newrdn, dsgw_get_boolean_var( "deleteoldrdn",
		DSGW_CGIVAR_OPTIONAL, 0 ));

	if ( rc == LDAP_SUCCESS ) {

	    /* construct the new DN so we can insert correct "edit" link */
	    if (( oldrdns = ldap_explode_dn( dn, 0 )) == NULL ) {
		dsgw_error( DSGW_ERR_NOMEMORY, NULL, DSGW_ERROPT_EXIT,
			0, NULL );
	    }

	    dnlen = strlen( newrdn ) + 1;	/* room for "," */
	    for ( i = 1; oldrdns[ i ] != NULL; ++i ) {
		dnlen += ( 1 + strlen( oldrdns[ i ] ));
	    }
	    dn = dsgw_ch_malloc( dnlen + 1 );
	    *dn = '\0';
	    strcat( dn, newrdn );
	    for ( i = 1; oldrdns[ i ] != NULL; ++i ) {
		strcat( dn, "," );
		strcat( dn, oldrdns[ i ] );
	    }
	    ldap_value_free( oldrdns );
	    free( encodeddn );
	    encodeddn = dsgw_strdup_escaped( dn );

	    success_msg = XP_GetClientStr(DBT_renamedBSBToBSB_);
	    if (( rdns = ldap_explode_rdn( newrdn, 1 )) == NULL
		    || rdns[ 0 ] == NULL ) {
		new_name = newrdn;
	    } else {
		new_name = dsgw_ch_strdup (rdns[ 0 ]);
		ldap_value_free( rdns );
	    }
	}
	break;
    default:
	break;
    }

    switch( changetype ) {
    case DSGW_CHANGETYPE_MODIFY:
	if (rc != LDAP_SUCCESS) break;
    case DSGW_CHANGETYPE_ADD:
	rc = entry_modify_or_add( ld, dn, changetype == DSGW_CHANGETYPE_ADD,
		&passwd_changed );
	if ( changetype == DSGW_CHANGETYPE_MODIFY ) {
	    success_msg = XP_GetClientStr(DBT_changesToBSBHaveBeenSaved_);
	} else {
	    success_msg = XP_GetClientStr(DBT_BSBHasBeenAdded_);
	}
	break;
    case DSGW_CHANGETYPE_DELETE:
	rc = entry_delete( ld, dn );
	success_msg = XP_GetClientStr(DBT_BSBHasBeenDeleted_);
	break;
    case DSGW_CHANGETYPE_MODRDN:
	break;
    default:
	rc = LDAP_PARAM_ERROR;
    }

    /*
     * If we are not running under the admin. server AND the operation
     * succeeded and the user is bound as the entry they just changed,
     * AND one of these conditions is true:
     *   1. we changed the password
     *   2. we did a modrdn
     *   3. we deleted the entry
     * then the auth. credentials should be discarded.  If we do discard, we
     * print an informative message for the user.
     */
    if ( !gc->gc_admserv && rc == LDAP_SUCCESS &&
	    ( changetype == DSGW_CHANGETYPE_DELETE || dn != old_dn ||
	    ( changetype == DSGW_CHANGETYPE_MODIFY && passwd_changed )) &&
	    dsgw_bound_as_dn( old_dn, 0 )) {
	char	*authck;

	/* first, remove the cookie from the cookie database (ignore errors) */
	if (( authck = dsgw_get_auth_cookie()) != NULL ) {
	    (void)dsgw_delcookie( authck );
	}

	/* output JavaScript to clear the cookie in the user's browser */
	dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	dsgw_emits( "<!-- Hide from non-JavaScript browsers\n" );
	dsgw_emitf( "document.cookie = '%s=%s; path=/'\n",
		DSGW_AUTHCKNAME, DSGW_UNAUTHSTR );
	dsgw_emits( "// End Hiding -->\n</SCRIPT>\n" );
	dsgw_emitf( XP_GetClientStr(DBT_PBNoteBBecauseYouSTheEntryYouWer_),
		( changetype == DSGW_CHANGETYPE_DELETE ) ? XP_GetClientStr(DBT_deleted_) :
		( dn != old_dn ) ? XP_GetClientStr(DBT_renamed_) :
		XP_GetClientStr(DBT_changedThePasswordOf_) );
    }

    if ( rc == LDAP_SUCCESS ) {
	/*
	 * check for "completion_javascript" form var and
	 * execute it if present.
	 */ 
	jscomp = dsgw_get_cgi_var( "completion_javascript",
		DSGW_CGIVAR_OPTIONAL );
	if ( jscomp != NULL ) {
	    char	*entry_name_js;
	    char	*new_name_js;

	    entry_name_js = dsgw_escape_quotes( entry_name );
	    new_name_js = dsgw_escape_quotes( new_name );
	    dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n" );
	    dsgw_emits( "dsmodify_info = '" );
	    dsgw_emitf( success_msg, entry_name_js, new_name_js );
	    dsgw_emits( "';\n" );
	    dsgw_emitf( "dsmodify_dn = '%s';\n",
		    ( changetype == DSGW_CHANGETYPE_DELETE ) ? "":
		    encodeddn );
	    dsgw_emitf( "eval('%s');\n", jscomp );
	    dsgw_emits( "</SCRIPT>\n" );
	}
    } else {
	jscomp = NULL;
    }

    if (( jscomp == NULL || changetype == DSGW_CHANGETYPE_DELETE )
	    && !gc->gc_admserv ) {
	dsgw_form_begin( NULL, NULL );
	dsgw_emits( "\n<CENTER><TABLE border=2 width=\"100%\"><TR>\n" );
	/*
	 * Show framed button.  If the modify succeeded, it is "Close".
	 * If the modify failed, it is "Go Back."
	 */
	dsgw_emits( "<TD WIDTH=\"100%\" ALIGN=\"center\">\n" );
	if ( rc == LDAP_SUCCESS ) {
	    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		    "onClick=\"parent.close()\">\n",
		    XP_GetClientStr(DBT_closeWindow_) );
	} else {
	    dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		    "onClick=\"history.back()\">\n",
		    XP_GetClientStr(DBT_goBack_) );
	}
	dsgw_emits( "\n</TABLE></CENTER></FORM>\n" );
    }

#if 0
    if ( display_results_inline && genfp != NULL ) {
	dsgw_emits( "<HR>\n" );
	dsgw_genscreen_continue( &genfp, NULL, 0 );
    } else if ( !quiet ) {
	dsgw_html_end();
    }
#else
    if ( !quiet ) {
	dsgw_html_end();
    }
#endif
    ldap_unbind( ld );
    if (old_dn != dn) free ( old_dn );
    free( dn );
}

static int
entry_modify_or_add( LDAP *ld, char *dn, int add, int *pwdchangedp )
{
    int		lderr, i, j, opoffset, modop, mls, unique, unchanged_count;
    char	*varname, *varvalue, *retval, *attr, *p, **vals, **unchanged_attrs;
    char	*ntuserid = NULL;

    LDAPMod	**pmods;

	int		msgid;
	LDAPMessage	*res = NULL;
	char	*errmsg = NULL;
	int     isNtUser = 0;

    pmods = NULL;
    unchanged_attrs = NULL;
    unchanged_count = 0;

    /*
     * Gather up password changes (if present in CGI POST)
     */
    if (( lderr = gather_passwd_changes( dn, &pmods, add, pwdchangedp ))
	    != LDAP_SUCCESS ) {
	return( lderr );
    }

    if ( verbose ) {
	dsgw_emitf( "<PRE>\n" );
    }

    /*
     * Gather up other changes:  each attribute value is POSTed in a variable
     * named:
     *      add_[unique_]ATTR
     *      replace_[unique_][DN_]ATTR
     * or   delete_[unique_]ATTR
     *
     * where ATTR is the LDAP attribute name and "unique_" is optional (if
     * present, we check to make sure the value is not in use before accepting
     * a replace or add).
     *
     * Additionally, if a variable name changed_ATTR is POSTed and its value
     * is not "true", it is assumed that no values have changed for that
     * ATTRibute.  If no "changed_ATTR" variable is POSTed, we assume that
     * ATTR has in fact changed.
     */
    i = 0;
    while (( varname = dsgw_next_cgi_var( &i, &varvalue )) != NULL ) {
	if ( varvalue != NULL && *varvalue == '\0' ) {
	    varvalue = NULL;
	} else {
	    dsgw_remove_leading_and_trailing_spaces( &varvalue );
	}

	opoffset = -1;
	if ( starts_with( varname, "add_" )) {
	    modop = LDAP_MOD_ADD;
	    opoffset = 4;
	    attr = varname + opoffset;
	    if (!isNtUser && (strcasecmp(DSGW_OC_NTUSER, attr) == 0)) {
		isNtUser = 1;
	    }
	} else if ( starts_with( varname, "replace_" )) {
	    modop = LDAP_MOD_REPLACE;
	    opoffset = 8;
		attr = varname + opoffset;
	} else if ( starts_with( varname, "delete_" )) {
	    modop = LDAP_MOD_DELETE;
	    opoffset = 7;
	} else if ( !strcmp( varname, "changed_DN" )) {
	    /* ignore it */
	} else if ( starts_with( varname, "changed_" )) {
	    attr = varname + 8;
	    if ( verbose && strcasecmp( varvalue, "true" ) == 0 ) {
		dsgw_emitf( XP_GetClientStr(DBT_attributeSWasChangedBrN_), attr );
	    }
	    if ( varvalue != NULL && strcasecmp( varvalue, "true" ) != 0 ) {
		unchanged_attrs = (char **)dsgw_ch_realloc( unchanged_attrs,
			( 2 + unchanged_count ) * sizeof( char * ));
		unchanged_attrs[ unchanged_count++ ] = dsgw_ch_strdup( attr );
		unchanged_attrs[ unchanged_count ] = NULL;

		if ( pmods != NULL ) {
		    remove_modifyops( pmods, attr );
		}
	    }
	}

	if ( opoffset >= 0 ) {
	    attr = varname + opoffset;
	    mls = 0;
	    unique = 0;
	    while (( p = strchr( attr, '_' )) != NULL ) {
		if ( starts_with( attr, "mls_" )) {
		    mls = 1;
		} else if ( starts_with( attr, "unique_" )) {
		    unique = 1;
		} /* ignore any other prefixes */
		attr = p + 1;
	    }

	    for ( j = 0; j < unchanged_count; ++j ) {
			if ( strcasecmp( unchanged_attrs[ j ], attr ) == 0 ) {
		    	break;
			}
	    }

	    if ( j >= unchanged_count ) {
		if ( varvalue == NULL || *varvalue == '\0' ) {
		    vals = NULL;
		    varvalue = NULL;
		} else {
		    varvalue = dsgw_ch_strdup( varvalue );
		    if ( mls ) {
			vals = post2multilinevals( varvalue );
		    } else {
			vals = post2vals( varvalue );
		    }
		}
		if ( vals == NULL ) {
		    if ( modop != LDAP_MOD_ADD ) {
			addmodifyop( &pmods, modop, attr, NULL, 0 );
		    }
		} else {
		    for ( j = 0; vals[ j ] != NULL; ++j ) {
			    if ( unique && modop != LDAP_MOD_DELETE && ( lderr =
				   value_is_unique( ld, dn, attr, vals[ j ] )) !=
				   LDAP_SUCCESS ) {
			       return( lderr );
			    }
				
			    if( isNtUser && (strcasecmp( DSGW_ATTRTYPE_NTUSERDOMAINID, attr) == 0)) {
				if( !ntuserid  ) {
				    ntuserid = strdup( vals[ j ] );
				}
			    }
				addmodifyop( &pmods, modop, attr, vals[ j ],
					strlen( vals[ j ] ));
		    }
		    free( vals );
		}
		if ( varvalue != NULL ) {
		    free( varvalue );
		}
	    }
	}

	free( varname );
    }

    /* if the admin is adding an NT person, there must be an ntuserid */
    if( (isNtUser) && (ntuserid == NULL) ) {
	dsgw_error( DSGW_ERR_USERID_REQUIRED, NULL, 0, 0, NULL );
	return(LDAP_PARAM_ERROR);
    }

    /* if an ntuserid is being added, it must be the correct length */
    if( (isNtUser) && ntuserid && (strlen( ntuserid ) > MAX_NTUSERID_LEN)) {
	dsgw_error( DSGW_ERR_USERID_MAXLEN_EXCEEDED, NULL, 0, 0, NULL );
	return(LDAP_PARAM_ERROR);
    }

    if ( verbose && pmods != NULL ) {
	int		j, notascii;
	unsigned long	k;
	struct berval	*bvp;

	for ( i = 0; pmods[ i ] != NULL; ++i ) {
	    modop = pmods[ i ]->mod_op & ~LDAP_MOD_BVALUES;
	    dsgw_emitf( "%s %s:\n", modop == LDAP_MOD_REPLACE ?
		    "replace" : modop == LDAP_MOD_ADD ?
		    "add" : "delete", pmods[ i ]->mod_type );
	    if ( pmods[ i ]->mod_bvalues != NULL ) {
		for ( j = 0; pmods[ i ]->mod_bvalues[ j ] != NULL; ++j ) {
		    bvp = pmods[ i ]->mod_bvalues[ j ];
		    notascii = 0;
		    for ( k = 0; k < bvp->bv_len; ++k ) {
			if ( !isascii( bvp->bv_val[ k ] )) {
			    notascii = 1;
			    break;
			}
		    }
		    if ( notascii ) {
			dsgw_emitf( XP_GetClientStr(DBT_TnotAsciiLdBytesN_), bvp->bv_len );
		    } else {
			dsgw_emitf( "\t\"%s\"\n", bvp->bv_val );
		    }
		}
	    }
	}
    }

    if ( verbose ) {
	dsgw_emitf( "</PRE>\n" );
	fflush( stdout );
    }

    dsgw_emitf( "<FONT SIZE=+1>\n" );

    /*
     * apply the changes using LDAP
     */
    if ( pmods == NULL ) {
	if ( add ) {
	    dsgw_emits( XP_GetClientStr(DBT_noValuesWereEnteredPleaseTryAgai_) );
	    lderr = LDAP_PARAM_ERROR;
	} else {	/* no changes -- just report success */
	    lderr = LDAP_SUCCESS;
	    if ( !quiet ) {
		dsgw_emitf( XP_GetClientStr(DBT_PSuccessfullyEditedEntryYourChan_) );
	    }
	}
    } else { 
	if ( !quiet ) {
	    dsgw_emitf( XP_GetClientStr(DBT_PSendingSToTheDirectoryServerN_),
		    add ? XP_GetClientStr(DBT_information_) : XP_GetClientStr(DBT_changes_));
	    fflush( stdout );
	}

	if ( add ) {
		lderr = ldap_add_ext( ld, dn, pmods, NULL, NULL, &msgid );
	} else {
		lderr = ldap_modify_ext( ld, dn, pmods, NULL, NULL, &msgid );
	}

	if( lderr == LDAP_SUCCESS ) {
		if(( lderr = ldap_result( ld, msgid, 1, (struct timeval *)NULL, &res )) == -1 ) {
			lderr = ldap_get_lderrno( ld, NULL, &errmsg );
			modify_error( lderr, errmsg );
		} else {
			lderr = ldap_result2error( ld, res, 1 ); 
			if ( lderr == LDAP_SUCCESS ) {
				if ( !quiet ) {
				if ( add ) {
					dsgw_emitf( XP_GetClientStr(DBT_PSuccessfullyAddedEntryN_) );
				} else {
					dsgw_emitf( XP_GetClientStr(DBT_PSuccessfullyEditedEntryYourChan_) );
				}
				}
			} else {
				(void)ldap_get_lderrno( ld, NULL, &errmsg );
				modify_error( lderr, errmsg );

				/* Do some checks for password policy infractions. */
				if( lderr == LDAP_CONSTRAINT_VIOLATION ) {
					if( errmsg && strstr( errmsg, "invalid password syntax" ) ) 
						dsgw_emitf( "<BR>(%s)", XP_GetClientStr(DBT_InvalidPasswordSyntax_) );
					else if( errmsg && strstr( errmsg, "password in history" ) )
						dsgw_emitf( "<BR>(%s)", XP_GetClientStr(DBT_PasswordInHistory_) );
				}
			}
		}
	} else {
		(void)ldap_get_lderrno( ld, NULL, &errmsg );
		modify_error( lderr, errmsg );
	}

	ldap_mods_free( pmods, 1 );
    }

    dsgw_emitf( "</FONT>\n" );
    return( lderr );
}


static int
entry_delete( LDAP *ld, char *dn )
{
    int		lderr;
    char	*errmsg = NULL;

    dsgw_emitf( "<FONT SIZE=+1>\n" );
    if (( lderr = ldap_delete_s( ld, dn )) == LDAP_SUCCESS ) {
	if ( !quiet ) {
	    dsgw_emitf( XP_GetClientStr(DBT_PSuccessfullyDeletedEntryN_) );
	}
    } else {
	(void)ldap_get_lderrno( ld, NULL, &errmsg );
	modify_error( lderr, errmsg );
    }

    dsgw_emitf( "</FONT>\n" );
    return( lderr );
}


static int
entry_modrdn( LDAP *ld, char *dn, char *newrdn, int deleteoldrdn )
{
    int		lderr;
    char	*errmsg = NULL;

    if ( verbose ) {
	dsgw_emitf( XP_GetClientStr(DBT_PreTheNewNameForTheEntryIsSNPreH_),
		newrdn );
    }

    dsgw_emitf( "<FONT SIZE=+1>\n" );
    if (( lderr = ldap_modrdn2_s( ld, dn, newrdn, deleteoldrdn ))
	    == LDAP_SUCCESS ) {
	if ( !quiet ) {
	    dsgw_emitf( XP_GetClientStr(DBT_PSuccessfullyRenamedEntryN_) );
	}
    } else {
	(void)ldap_get_lderrno( ld, NULL, &errmsg );
	modify_error( lderr, errmsg );
    }

    dsgw_emitf( "</FONT>\n" );
    return( lderr );
}


static int
gather_passwd_changes( char *dn, LDAPMod ***pmodsp, int adding_entry,
	int *pwdchangedp )
{
    int		lderr, lockpasswd;
    char	*bindpasswd, *newpasswd, *newpasswdconfirm, *errstring;

    lockpasswd = dsgw_get_boolean_var( "lockpasswd", 0, 0 );
    if ( lockpasswd ) {
	/*
	 * the userPassword attribute to a special value that no password
	 * submitted by a user can ever match.
	 */
	time_t		curtime;
	struct tm	*gmtp;
	char		*tstr;

	/* get string representation of current GMT time */
	curtime = time( NULL );
	gmtp = gmtime( &curtime );
	tstr = asctime( gmtp );

	/* remove trailing newline */
	tstr[ strlen( tstr ) - 1 ] = '\0';

	/* allocate room for "{crypt}LOCKED [" + tstr + " GMT]" + zero byte */
	newpasswd = dsgw_ch_malloc( 15 + strlen( tstr ) + 5 + 1 );
	sprintf( newpasswd, XP_GetClientStr(DBT_CryptLockedSGmt_), tstr );

    } else if (( newpasswd = dsgw_get_cgi_var( "newpasswd",
	    DSGW_CGIVAR_OPTIONAL )) == NULL ) {
	return( LDAP_SUCCESS );	/* not setting password -- nothing to do */
    }

    lderr = LDAP_PARAM_ERROR;	/* pessimistic */

    if ( !adding_entry && ( bindpasswd = dsgw_get_cgi_var( "passwd",
	    DSGW_CGIVAR_OPTIONAL )) == NULL && require_oldpasswd( dn )) {
	errstring = XP_GetClientStr(DBT_youMustProvideTheOldPassword_);
    } else if ( !lockpasswd &&
	    (( newpasswdconfirm = dsgw_get_cgi_var( "newpasswdconfirm",
	    DSGW_CGIVAR_OPTIONAL )) == NULL || strcmp( newpasswd,
	    newpasswdconfirm ) != 0 )) {
	errstring = XP_GetClientStr(DBT_theNewAndConfirmingPasswordsDoNo_);
    } else {
	addmodifyop( pmodsp, adding_entry ? LDAP_MOD_ADD : LDAP_MOD_REPLACE,
		DSGW_ATTRTYPE_USERPASSWORD, newpasswd, strlen( newpasswd ));
	*pwdchangedp = 1;
	lderr = LDAP_SUCCESS;
    }

    if ( lderr != LDAP_SUCCESS ) {
	dsgw_emitf( "<FONT SIZE=+1>\n%s\n</FONT>\n", errstring );
    }

    return( lderr );
}


static void
modify_error( int lderr, char *lderrtxt )
{
    dsgw_error( DSGW_ERR_LDAPGENERAL, dsgw_ldaperr2string( lderr ),
	    ( display_results_inline ? DSGW_ERROPT_INLINE : 0 ),
	    lderr, lderrtxt );
}


/*
 * this "addmodifyop" routine is lifted with minor changes from
 * ldap/tools/ldapmodify.c
 */
static void
addmodifyop( LDAPMod ***pmodsp, int modop, char *attr, char *value, int vlen )
{
    LDAPMod		**pmods;
    int			i, j;
    struct berval	*bvp;

    if ( attr == NULL || *attr == '\0' ) {
	return;
    }

    pmods = *pmodsp;
    modop |= LDAP_MOD_BVALUES;

    i = 0;
    if ( pmods != NULL ) {
	for ( ; pmods[ i ] != NULL && pmods[ i ]->mod_type != NULL; ++i ) {
	    if ( strcasecmp( pmods[ i ]->mod_type, attr ) == 0 &&
		    pmods[ i ]->mod_op == modop ) {
		break;
	    }
	}
    }

    if ( pmods == NULL || pmods[ i ] == NULL ) {
	pmods = (LDAPMod **)dsgw_ch_realloc( pmods, (i + 2) *
		sizeof( LDAPMod * ));
	*pmodsp = pmods;
	pmods[ i + 1 ] = NULL;
	pmods[ i ] = (LDAPMod *)dsgw_ch_malloc( sizeof( LDAPMod ));
	memset( pmods[ i ], 0, sizeof( LDAPMod ));
	pmods[ i ]->mod_op = modop;
	pmods[ i ]->mod_type = dsgw_ch_strdup( attr );
    }

    if ( value != NULL ) {
	j = 0;
	if ( pmods[ i ]->mod_bvalues != NULL ) {
	    for ( ; pmods[ i ]->mod_bvalues[ j ] != NULL; ++j ) {
		;
	    }
	}
	pmods[ i ]->mod_bvalues =
		(struct berval **)dsgw_ch_realloc( pmods[ i ]->mod_bvalues,
		(j + 2) * sizeof( struct berval * ));
	pmods[ i ]->mod_bvalues[ j + 1 ] = NULL;
	bvp = (struct berval *)dsgw_ch_malloc( sizeof( struct berval ));
	pmods[ i ]->mod_bvalues[ j ] = bvp;

	bvp->bv_len = vlen;
	bvp->bv_val = (char *)dsgw_ch_malloc( vlen + 1 );
	memcpy( bvp->bv_val, value, vlen );
	bvp->bv_val[ vlen ] = '\0';
    }
}


/* remove all modify ops that refer to "attr" */
static void
remove_modifyops( LDAPMod **pmods, char *attr )
{
    int		i, found_attr;

    if ( pmods == NULL ) {
	return;
    }

    do {
	found_attr = 0;
	for ( i = 0 ; pmods[ i ] != NULL; ++i ) {
	    if ( strcasecmp( pmods[ i ]->mod_type, attr ) == 0 ) {
		found_attr = 1;
		break;
	    }
	}

	if ( found_attr ) {
	    if ( pmods[ i ]->mod_bvalues != NULL ) {
		ber_bvecfree( pmods[ i ]->mod_bvalues );
	    }
	    free( pmods[ i ] );

	    for ( ; pmods[ i + 1 ] != NULL; ++i ) {
		pmods[ i ] = pmods[ i + 1 ];
	    }
	    pmods[ i ] = NULL;
	}

    } while ( found_attr );
}


static int
starts_with( char *s, char *startswith )
{
    int	len;

    len = strlen( startswith );
    return ( strlen( s ) > len && strncmp( s, startswith, len ) == 0 );
}


/* 
 * there is one value in "postedval" but newlines must be changed to "$",
 * '$' characters must be changed to \24, and '\' chars. changed to \5C
 */
static char **
post2multilinevals( char *postedval )
{
    int		specials;
    char	*p, *r, **vals;

    vals = dsgw_ch_malloc( 2 * sizeof( char * ));
    vals[ 1 ] = NULL;

    specials = 0;
    for ( p = postedval; *p != '\0'; ++p ) {
	if ( *p == '$' || *p == '\\' || *p == '\n' || *p == '\r') {
	    ++specials;
	}
    }

    /* allocate enough room to handle any necessary escaping */
    r = vals[ 0 ] = dsgw_ch_malloc( 2 * specials + strlen( postedval ) + 1 );

    /* copy and escape as appropriate */
    for ( p = postedval; *p != '\0'; ++p ) {
	if ( *p == '\n' || *p == '\r' ) {	/* change to "$" */
	    *r++ = '$';
	    if ( *(p+1) != '\0' && *(p+1) != *p &&
		    ( *(p+1) == '\n' || *(p+1) == '\r' )) {
		++p;	/* skip next char. if sequence is "\r\n" or "\n\r" */
	    }
	} else if ( *p == '$' ) {		/* change to "\24" */
	    *r++ = '\\';
	    *r++ = '2';
	    *r++ = '4';
	} else {
	    *r++ = *p;
	    if ( *p == '\\' ) {			/* change to "\5C" */
		*r++ = '5';
		*r++ = 'C';
	    }
	}
    }

    *r = '\0';

    return( vals );
}


/* values are delimited by newlines, preceded by optional carriage returns */
static char **
post2vals( char *postedval )
{
    int		count, len;
    char	*p, *q, **vals;

    vals = NULL;

    count = 0;
    for ( p = postedval; p != NULL && *p != '\0'; p = q ) {
	/* skip any leading CRs or NLs */
	while (( *p == '\n' || *p == '\r' ) && *p != '\0' ) {
	    ++p;
	}
	if ( *p == '\0' ) {
	    break;
	}

	/* find end of this line */
	if (( q = strchr( p, '\n' )) != NULL ) {
	    *q++ = '\0';
	}

	/* remove CR, if any */
	len = strlen( p ) - 1;
	if ( p[ len ] == '\r' ) {
	    p[ len ] = '\0';
	}

	/* add to values array */
	vals = dsgw_ch_realloc( vals, ( count + 2 ) * sizeof( char * ));
	vals[ count++ ] = p;
    }
    vals[ count ] = NULL;

    return( vals );
}


/*
 * Determine if we should insist that the old password for the entry
 * we are modifying (modifydn) be POSTed.  The rule we use is simply
 * this:  if the binddn and modifydn are the same, require the old
 * password.  This allows directory admins. to reset passwords while
 * preventing normal users from having their password changed if they
 * just happen to walk away from their computer for a while when they
 * are authenticated to the gateway.
 */
static int
require_oldpasswd( char *modifydn )
{
    return( dsgw_bound_as_dn( modifydn, 1 ));
}


/*
 * search directory to find out if an attribute value is unique.  If the
 * value doesn't already exist or if it exists only in the same entry we
 * are changing, we return LDAP_SUCCESS.  If it does exist, we return
 * LDAP_TYPE_OR_VALUE_EXISTS.  If some other error occurs, we return another
 * LDAP error code.
 */
static int
value_is_unique( LDAP *ld, char *dn, char *attr, char *value )
{
    int		rc, count;
    char	*attrs[2], *buf, *tmpdn, *attrdesc, *errmsg = NULL;
    LDAPMessage	*res, *e;

    /* allocate room for "(attr=value)" filter */
    buf = dsgw_ch_malloc( strlen( attr ) + strlen( value ) + 4 );
    sprintf( buf, "(%s=%s)", attr, value );

    attrs[ 0 ] = attr;
    attrs[ 1 ] = NULL;

    rc = ldap_search_s( ld, gc->gc_ldapsearchbase, LDAP_SCOPE_SUBTREE,
	    buf, attrs, 1, &res );
    free( buf );

    if ( rc != LDAP_SUCCESS || res == NULL ) {
	(void)ldap_get_lderrno( ld, NULL, &errmsg );
	modify_error( rc, errmsg );
	return( rc );
    }

    if (( count = ldap_count_entries( ld, res )) == 0 ) {
	rc = LDAP_SUCCESS;
    } else if ( count > 1 ) {
	rc = LDAP_TYPE_OR_VALUE_EXISTS;
    } else {	/* found one entry: see if it is the entry we are modifying */
	if (( e = ldap_first_entry( ld, res )) == NULL ||
	    ( tmpdn = ldap_get_dn( ld, e )) == NULL ) {
	    rc = ldap_get_lderrno( ld, NULL, NULL );
	} else if ( dsgw_dn_cmp( dn, tmpdn ) != 0 ) {
	    rc = LDAP_SUCCESS;	/* same entry */
	} else {
	    rc = LDAP_TYPE_OR_VALUE_EXISTS;
	}
    }

    ldap_msgfree( res );

    if ( rc == LDAP_TYPE_OR_VALUE_EXISTS ) {
	buf = dsgw_ch_malloc( strlen( attr ) + 6 );	/* room for "desc_" */
	sprintf( buf, "desc_%s", attr );
	if (( attrdesc = dsgw_get_cgi_var( buf, DSGW_CGIVAR_OPTIONAL ))
		== NULL ) {
	    attrdesc = attr;
	}
	free( buf );

	dsgw_emits( "\n<FONT SIZE=+1>\n" );
	dsgw_emitf( XP_GetClientStr(DBT_BrTheSBSBIsAlreadyInUsePleaseCho_), attrdesc, value );
	dsgw_emits( "\n</FONT>\n" );
    }

    return( rc );
}
