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
 * emitauth.c -- generate authentication form -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

static int isexp = 0; /* Why is this static? */

static void
emit_authinfo( int isEditing, int isPwForm, char *binddn )
{
    char *cookie, *dn, *rndstr, *pw;
    int rc;
    int isauth = 0;

    /* try to get the DN the user is bound as, and determine if
     * authentication credentials have expired.
     */
    if (( cookie = dsgw_get_auth_cookie()) != NULL ) {
	if ( dsgw_parse_cookie( cookie, &rndstr, &dn ) == 0 ) {
	    if ( dn == NULL ) {
		isauth = 0;
	    } else {
		if (( rc = dsgw_ckdn2passwd( rndstr, dn, &pw )) == 0 ) {
		    isauth = 1;
		} else {
		    isauth = 0;
		    if ( rc == DSGW_CKDB_EXPIRED ) {
			isexp = 1;
		    }
		}
	    }
	} else {
	    isauth = 0;
	}
    } else {
	isauth = 0;
    }

    dsgw_emitf( "<CENTER>\n"
	"<FONT SIZE=+2>%s</FONT>\n"
	"</CENTER>\n"
	"<p>", XP_GetClientStr(DBT_authenticateLogInToTheDirectory_) );

    if ( isPwForm ) {
#ifdef NOTFORNOW
	/* ldap_dn2ufn currently gobble up 'dc' so don't use it for */
	/* now */
	auto char *ufn = ldap_dn2ufn( binddn );
	dsgw_emitf( XP_GetClientStr(DBT_youAreAboutToAuthenticate_), ufn);
	free( ufn );
#else
	dsgw_emitf( XP_GetClientStr(DBT_youAreAboutToAuthenticate_), binddn);
#endif
    } else if ( isEditing ) {
	dsgw_emits( XP_GetClientStr(DBT_beforeYouCanEditOrAddEntriesYouM_) );
    } else {
	dsgw_emits( XP_GetClientStr(DBT_fromThisScreenYouMayAuthenticate_) );
    }
    if ( isEditing ) {
	return;
    }
    dsgw_emitf( "<HR>\n"
	"<CENTER>\n"
	"<FONT SIZE=+2>%s</FONT>\n"
	"</CENTER>\n"
	"<P>\n", XP_GetClientStr(DBT_authenticationStatus_) );

    if ( isauth ) {
	auto char *ufn;
	dsgw_emits( XP_GetClientStr(DBT_FormNyouAreCurrentlyAuthenticate_) );
	ufn = ldap_dn2ufn( dn );
	dsgw_emitf( "<b>%s</b>\n", ufn );
	free( ufn );
	dsgw_emitf( "%s<BR>"
	    "<CENTER>\n"
	    "<INPUT TYPE=BUTTON "
	    "VALUE=\"%s\""
	    "onClick=\"doUnauth();\">\n"
	    "</FORM>\n"
	    "</CENTER>\n"
	    "<HR>\n",
	    XP_GetClientStr(DBT_NifYouWishToDiscardYourAuthentic_),
	    XP_GetClientStr(DBT_discardAuthenticationCredentials_2) );
    } else if ( isexp ) {
	dsgw_emits( XP_GetClientStr(DBT_yourAuthenticationCredentialsFor_) );
	dsgw_emitf( "<b>%s</b> ", dn );
	dsgw_emits( XP_GetClientStr(DBT_haveExpiredN_) );
    } else {
	dsgw_emits( XP_GetClientStr(DBT_currentlyYouAreNotAuthenticatedT_) );
    }
}


static void
emit_file (char* filename, char* authdesturl, char *user )
{
    auto FILE* html = dsgw_open_html_file( filename, DSGW_ERROPT_EXIT );
    auto char line[ BIG_LINE ];
    auto int argc;
    auto char **argv, *escaped_dn;

    if ( user != NULL ) {
	escaped_dn = dsgw_strdup_escaped( user );
    } else {
	escaped_dn = "";
    }

    while ( dsgw_next_html_line( html, line )) {
	if ( dsgw_parse_line( line, &argc, &argv, 0, dsgw_simple_cond_is_true, NULL )) {
	    if ( dsgw_directive_is( line, DRCT_HEAD )) {
		dsgw_head_begin();
		dsgw_emits ("\n");

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_SCRIPT" )) {
		dsgw_emits ("<SCRIPT NAME=\"JavaScript\">\n"
			    "<!-- Hide from non-JavaScript browsers\n"
			    "function doUnauth()\n"
			    "{\n");
		dsgw_emits ("    if ( confirm( ");
		dsgw_quote_emits (QUOTATION_JAVASCRIPT,
				  XP_GetClientStr(DBT_discardAuthenticationCredentials_));
		dsgw_emits (" )) {\n"
			    "        window.location.href='unauth?context=");
		dsgw_emits(context);
		dsgw_emits("';\n"
			    "    }\n"
			    "}\n");
#if 0				/* This doesn't work with Navigator 2.x */
		dsgw_emits ("function checkSS(sform)\n"
			    "{\n"
			    "    if (sform.searchstring.value == null || sform.searchstring.value == \"\") {\n");
		dsgw_emit_alert (NULL, NULL, XP_GetClientStr(DBT_youDidNotSupplyASearchString_));
		dsgw_emits ("        return false;\n"
			    "    }\n"
			    "}\n");
#endif
		dsgw_emits ("function init()\n"
			    "{\n"
			    "    document.authSearchForm.searchstring.select();\n"
			    "    document.authSearchForm.searchstring.focus();\n"
			    "    if (top.history.length == 1 && top.opener != null && top.opener.location.href != "
			    "top.location.href) {\n"
			    "        if (top.closewin == true) {\n"
			    "            top.opener.document.clear();\n"
			    "            top.opener.document.open();\n"
			    "            top.opener.document.write('');\n"
			    "            top.opener.document.close();\n"
			    "        }\n"
			    "    }\n"
			    "    top.closewin = false;\n"
			    "}\n"
			    "// End hiding -->\n"
			    "</SCRIPT>\n");

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_BODY" )) {
		dsgw_emitf ("<BODY onLoad=\"setTimeout('init()', 10);\" %s>\n",
			    dsgw_html_body_colors);
		dsgw_emit_alertForm();

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_INFO" )) {
		emit_authinfo( authdesturl != NULL, 0, NULL );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_FORM" )) {
		dsgw_form_begin ("authSearchForm", "action=\"dosearch\""
#if 0				/* This doesn't work with Navigator 2.x */
				 " onSubmit=\"return checkSS(this)\""
#endif
				 );
		dsgw_emits ("\n<INPUT TYPE=hidden NAME=mode VALUE=\"auth\">\n");
		if ( authdesturl != NULL ) {
		    dsgw_emitf ("<INPUT TYPE=hidden NAME=authdesturl VALUE=\"%s\">\n",
				authdesturl);
		}

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_NAME" )) {
		dsgw_emitf ("<INPUT NAME=\"searchstring\" VALUE=\"%s\" SIZE=40>\n",
			    ( user == NULL ) ? "" : user );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_SEARCH_BUTTONS" )) {
		if ( authdesturl == NULL ) {
		    dsgw_emitf ("<TD ALIGN=CENTER WIDTH=50%%>\n"
				"<INPUT TYPE=\"submit\" VALUE=\"%s\">\n"
				"<TD ALIGN=CENTER WIDTH=50%%>\n",
				XP_GetClientStr(DBT_continue_) );
		} else {
		    dsgw_emitf ("<TD ALIGN=CENTER WIDTH=33%%>\n"
				"<INPUT TYPE=\"submit\" VALUE=\"%s\">\n"
				"<TD ALIGN=CENTER WIDTH=33%%>\n"
				"<INPUT TYPE=\"button\" VALUE=\"%s\" "
				"onClick=\"parent.close();\">\n"
				"<TD ALIGN=CENTER WIDTH=34%%>\n", 
				XP_GetClientStr(DBT_continue_1), XP_GetClientStr(DBT_cancel_) );
		}
		dsgw_emit_helpbutton ("AUTHHELP_ID" );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_AS_ROOT_FORM" )) {
		dsgw_form_begin ("AuthAsRootDNForm", "action=\"auth\"");
		dsgw_emits ("\n");
		dsgw_emits ("<INPUT TYPE=hidden NAME=authasrootdn VALUE=\"true\">\n");
		if ( authdesturl != NULL ) {
		    dsgw_emitf ("<INPUT TYPE=hidden NAME=authdesturl VALUE=\"%s\">\n",
				authdesturl );
		}

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_SCRIPT" )) {
		dsgw_emits ("<SCRIPT NAME=\"JavaScript\">\n"
			    "<!-- Hide from non-JavaScript browsers\n");
		/* doUnauth function - invoke CGI which tosses cookies. */
		dsgw_emitf ("function doUnauth()\n"
			    "{\n"
			    "    if ( confirm( '%s' )) {\n"
			    "        window.location.href='unauth?context=%s';\n"
			    "    }\n"
			    "}\n"
			    "// End hiding -->\n"
			    "</SCRIPT>\n\n",
			    XP_GetClientStr (DBT_discardAuthenticationCredentials_1), context);

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_BODY" )) {
		dsgw_emitf ("<BODY onLoad=\"document.authPwForm.password.select();document.authPwForm.password.focus();\" %s>\n",
			    dsgw_html_body_colors );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_INFO" )) {
		emit_authinfo( authdesturl != NULL, 1, user );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_FORM" )) {
		dsgw_form_begin( "authPwForm", "action=\"doauth\"" );
		dsgw_emits ("\n" );
		dsgw_emitf (
			"<INPUT type=hidden name=escapedbinddn value=\"%s\">\n",
			escaped_dn );
		if ( authdesturl != NULL ) {
		    dsgw_emitf ("<INPUT type=hidden name=authdesturl value=\"%s\">\n",
				authdesturl );
		}

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_NAME" )) {
		auto char** xdn = ldap_explode_dn( user, 1 );
		dsgw_emits( xdn[ 0 ] );
		ldap_value_free( xdn );

	    } else if ( dsgw_directive_is( line, "DS_AUTH_PASSWORD_BUTTONS" )) {
		if ( authdesturl == NULL ) {
		    dsgw_emitf ("<TD ALIGN=CENTER WIDTH=50%%>\n"
				"<INPUT TYPE=\"submit\" VALUE=\"%s\">\n"
				"<TD ALIGN=CENTER WIDTH=50%%>\n",
				XP_GetClientStr(DBT_continue_2) );
		} else {
		    dsgw_emitf ("<TD ALIGN=CENTER WIDTH=33%%>\n"
				"<INPUT TYPE=\"submit\" VALUE=\"%s\">\n"
				"<TD ALIGN=CENTER WIDTH=33%%>\n"
				"<INPUT TYPE=\"button\" VALUE=\"%s\" "
				"onClick=\"parent.close();\">\n"
				"<TD ALIGN=CENTER WIDTH=34%%>\n",
				XP_GetClientStr(DBT_continue_3), XP_GetClientStr(DBT_cancel_1) );
		}
		dsgw_emit_helpbutton ("AUTHHELP_PW" );

	    } else if ( dsgw_directive_is( line, "DS_HELP_BUTTON" ) && argc > 0) {
		dsgw_emit_helpbutton (argv[0]);
	    } else {
		dsgw_emits (line);
	    }
	    dsgw_argv_free( argv );
	}
    }
    fflush (stdout);
    fclose (html);
}


void
dsgw_emit_auth_form( char *binddn )
{
    dsgw_emit_auth_dest( binddn, dsgw_get_cgi_var( "authdesturl", DSGW_CGIVAR_OPTIONAL ));
}

void
dsgw_emit_auth_dest( char *binddn, char* authdesturl )
{
    /*
     * If dn is NULL, then we don't know who we want to bind as yet.
     * Generate a simplified search form.  This form needs to post:
     * mode=auth
     * searchstring
     * authdesturl
     *
     * If dn was given, then prompt for the password.  Needs to post:
     * password
     * authdesturl
     * binddn
     */
    if ( binddn == NULL ) {
	emit_file( "authSearch.html", authdesturl,
		  dsgw_get_cgi_var( "authhint", DSGW_CGIVAR_OPTIONAL ));
    } else {
	emit_file( "authPassword.html", authdesturl, binddn );
    }
}







