/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * doauth.c -- CGI authentication handler -- HTTP gateway
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */
#include "dsgw.h"
#include "dbtdsgw.h"

static void post_request();
static void do_autherror( int rc, char *msg, char *lderrtxt,
	int ommitclosebutton );


int main( argc, argv, env )
    int		argc;
    char	*argv[];
{
    int		reqmethod;

    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_POST );

    post_request();

    exit( 0 );
}

static void
post_request()
{
    char	*binddn, *password, *authdesturl, *ufn, *encodeddn, *lderrtxt;
    LDAP	*ld;
    int		rc;
	int		password_expiring = -1;
	int		msgid = 0;

    binddn = dsgw_get_escaped_cgi_var( "escapedbinddn", "binddn",
	    DSGW_CGIVAR_REQUIRED );
    encodeddn = dsgw_strdup_escaped( binddn );
    authdesturl = dsgw_get_cgi_var( "authdesturl", DSGW_CGIVAR_OPTIONAL );
    password = dsgw_get_cgi_var( "password", DSGW_CGIVAR_OPTIONAL );

    (void) dsgw_init_ldap( &ld, NULL, 1, 0);

    if ( password == NULL || strlen( password ) == 0 ) {
	do_autherror( 0, XP_GetClientStr( DBT_youDidNotProvidePasswd_ ), 
		NULL, authdesturl == NULL );
	exit( 0 );
    }

    if( ( msgid = ldap_simple_bind( ld, binddn, password ) ) == -1 ) {
		rc = ldap_get_lderrno( ld, NULL, &lderrtxt );
		do_autherror( rc, NULL, lderrtxt, authdesturl == NULL );
		exit( 0 );
    } else {

		char *ckbuf;
		LDAPControl **ctrls = NULL;
		LDAPMessage	*res;
		char	*errmsg = NULL;

		/* Conduct password policy checks */
		if(( rc = ldap_result( ld, msgid, 1, NULL, &res )) == -1 ) {
			rc = ldap_get_lderrno( ld, NULL, &errmsg );
			do_autherror( rc, NULL, errmsg, authdesturl == NULL );
			exit( 0 );
		}

		if( ldap_parse_result( ld, res, NULL, NULL, NULL, NULL, &ctrls, 0 ) 
			!= LDAP_SUCCESS ) {
			rc = ldap_get_lderrno( ld, NULL, &errmsg );
			do_autherror( rc, NULL, errmsg, authdesturl == NULL );
			exit( 0 );
		}

		rc = ldap_result2error( ld, res, 1 ); 
		if( rc == LDAP_SUCCESS ) {
			if( ctrls ) {
				int i;
				for( i = 0; ctrls[ i ] != NULL; ++i ) {
					if( !( strcmp( ctrls[ i ]->ldctl_oid, 
							LDAP_CONTROL_PWEXPIRED) ) ) {
						/* The password has expired. Convey this information, 
						   and give the user the option to change their 
						   password immediately. */
						dsgw_password_expired_alert( binddn );
						exit( 0 );
					}					
					else if( !( strcmp( ctrls[ i ]->ldctl_oid, 
							LDAP_CONTROL_PWEXPIRING) ) ) {
						/* "The password is expiring in n seconds" */
						if( ( ctrls[ i ]->ldctl_value.bv_val != NULL ) &&
							( ctrls[ i ]->ldctl_value.bv_len > 0 ) ) {
							password_expiring = atoi( ctrls[ i ]->ldctl_value.bv_val );
						}
					}
				}	
				ldap_controls_free( ctrls );
			}
		} else if( rc == LDAP_CONSTRAINT_VIOLATION ) {
			rc = ldap_get_lderrno( ld, NULL, &errmsg );
			if( errmsg && strstr( errmsg, 
					"Exceed password retry limit. Contact system administrator to reset" ) ) {
				do_autherror( rc, XP_GetClientStr(DBT_ExceedPasswordRetryContactSysAdmin_), 
					NULL, authdesturl == NULL );
			} else if( errmsg && strstr( errmsg, 
					"Exceed password retry limit. Please try later" ) ) {
				do_autherror( rc, XP_GetClientStr(DBT_ExceedPasswordRetryTryLater_), 
					NULL, authdesturl == NULL );
			} else {
				do_autherror( rc, NULL, errmsg,
				    authdesturl == NULL );
			}
			exit( 0 );
		} else if( rc == LDAP_INVALID_CREDENTIALS ) {
			if( errmsg && strstr( errmsg, "password expired" ) )  {
				do_autherror( rc, XP_GetClientStr(DBT_PasswordExpired_), 
					NULL, authdesturl == NULL );
			} else {
				do_autherror( rc, NULL, errmsg,
				    authdesturl == NULL );
			}
			exit( 0 );
		} else {
			rc = ldap_get_lderrno( ld, NULL, &errmsg );
			do_autherror( rc, NULL, errmsg, authdesturl == NULL );
			exit( 0 );
		}

	/* Construct cookie */
	if (( ckbuf = dsgw_mkcookie( binddn, password, gc->gc_authlifetime,
		&rc )) == NULL ) {
	    switch ( rc ) {
	    case DSGW_CKDB_CANTOPEN:
		do_autherror( 0, XP_GetClientStr( DBT_authDBNotOpened_ ),
				 NULL, authdesturl == NULL );
		break;
	    case DSGW_CKDB_CANTAPPEND:
		do_autherror( 0,
		    XP_GetClientStr( DBT_DataCouldNotAppendToAuthDB_ ),
		    NULL, authdesturl == NULL );
		break;
	    default:
		do_autherror( rc, NULL, NULL, authdesturl == NULL );
		break;
	    }
	    exit( 1 );
	}
	dsgw_add_header( ckbuf );

	/* Construct a success message */
	dsgw_send_header();
	dsgw_emits( "<HTML>" );
	dsgw_head_begin();
	dsgw_emits( "\n" 
	    "<TITLE>Authentication Successful</TITLE>\n"
	    "<SCRIPT LANGUAGE=\"JavaScript\">\n"
	    "<!-- Hide from non-JavaScript browsers\n" );

	if ( authdesturl != NULL && strlen( authdesturl ) > 0 ) {
	    dsgw_emitf( "var authdesturl=\"%s\";\n", authdesturl );
	} else {
	    dsgw_emitf( "var authdesturl=null;\n" );
	}

	if( password_expiring != -1 ) {
		if ( encodeddn != NULL && strlen( encodeddn ) > 0 ) {
			dsgw_emitf( "var editdesturl = '%s?passwd&dn=%s&context=%s';\n",
				dsgw_getvp( DSGW_CGINUM_EDIT ), encodeddn, context );
		} else {
			dsgw_emitf( "var editdesturl=null;\n" );
		}

		dsgw_emits( "function editPassword()\n"
			"{\n"
			"    if ( editdesturl != null ) {\n"
			"        top.location.href = editdesturl;\n"
			"    } else {\n"
			"        top.close();\n"
			"    }\n"
			"}\n" );
	}

	dsgw_emits( "function finishAuth()\n"
	    "{\n"
	    "    if ( authdesturl != null ) {\n"
	    "        top.location.href = authdesturl;\n"
	    "    } else {\n"
	    "        top.close();\n"
	    "    }\n"
	    "}\n"
	    "var contButtons = ");
	dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
	dsgw_form_begin ("bForm", NULL);
	if( password_expiring != -1 ) {
		/* Create a table with 1 row and 3 columns, 
		   one column for each button... */
		dsgw_emitf(
			"\n<TABLE BORDER COLS=3 WIDTH=100%%>\n"
			"<TD ALIGN=CENTER>\n"
			"<INPUT TYPE=BUTTON NAME=\"contButton\""
			"VALUE=\"%s\" onClick=\"finishAuth();\">\n"
			"<TD ALIGN=CENTER>\n"
			"<INPUT TYPE=BUTTON NAME=\"editButton\""
			"VALUE=\"%s\" onClick=\"editPassword();\">\n"
			"<TD ALIGN=CENTER>",
			XP_GetClientStr( DBT_continue_4 ),
			XP_GetClientStr( DBT_EditPassword_ ));
	} else {	
		dsgw_emitf(
			"\n<TABLE BORDER=2 WIDTH=100%%>\n"
			"<TD ALIGN=CENTER WIDTH=50%%>\n"
			"<INPUT TYPE=BUTTON NAME=\"contButton\""
			"VALUE=\"%s\" onClick=\"finishAuth();\">\n"
			"<TD ALIGN=CENTER WIDTH=50%%>",
			XP_GetClientStr( DBT_continue_4 ));
	}
	dsgw_emit_helpbutton( "AUTHSUCCESS" );
	dsgw_emits(
	    "\n</TABLE></FORM>");
	dsgw_quotation_end(); dsgw_emits(";\n");

	dsgw_emits(
	    "var noContButtons = ");
	dsgw_quotation_begin (QUOTATION_JAVASCRIPT_MULTILINE);
	dsgw_emits( XP_GetClientStr( DBT_ToContinue_ ));
	dsgw_form_begin( "bForm", NULL );
	dsgw_emits(
	    "\n<TABLE BORDER=2 WIDTH=100%>"
	    "\n<TD ALIGN=CENTER WIDTH=50%>" );
	dsgw_emit_homebutton();
	dsgw_emits( "\n<TD ALIGN=CENTER WIDTH=50%%>" );
	dsgw_emit_helpbutton( "AUTHSUCCESS" );
	dsgw_emits(
	    "\n</TABLE></FORM>\n");
	dsgw_quotation_end(); dsgw_emits(";\n");

	dsgw_emitf(
	    "// End hiding -->\n"
	    "</SCRIPT>\n"
	    "</HEAD>\n<BODY %s>\n"
	    "<CENTER>\n"
	    "<H3>%s</H3>\n"
	    "</CENTER>\n",
	    dsgw_html_body_colors,
	    XP_GetClientStr( DBT_AuthenticationSuccessful_ )
	    );

#ifdef NOTFORNOW
	/* ldap_dn2ufn currectly gobble up 'dc' so don't use it for */
	/* now */
	ufn = ldap_dn2ufn( binddn );
	dsgw_emitf( XP_GetClientStr( DBT_YouAreNowAuthenticated_ ), ufn );
#else
	dsgw_emitf( XP_GetClientStr( DBT_YouAreNowAuthenticated_ ), binddn );
#endif
	dsgw_emits( "<P>\n" );
#ifdef NOTFORNOW
	free( ufn );
#endif
	dsgw_emitf( XP_GetClientStr( DBT_YourAuthenticationCredentialsWill_ ),
		gc->gc_authlifetime / 60 );
	dsgw_emits(  XP_GetClientStr( DBT_AfterYourCredentialsExpire_ ));

	if( password_expiring != -1 ) {
		time_t	cur_time, pw_exp_time_t;
		struct	tm *pw_exp_time_tm;

		cur_time = dsgw_current_time();
		pw_exp_time_t = dsgw_time_plus_sec( cur_time, password_expiring );
		pw_exp_time_tm = localtime( &pw_exp_time_t );

		dsgw_emitf(
		    XP_GetClientStr( DBT_ThePasswordForThisEntryWillExpire_ ),
		    asctime( pw_exp_time_tm ));
		dsgw_emits( "<P>\n" );
	}

	dsgw_emits(
	    "<P>\n"
	    "<TR>\n"
	    "<SCRIPT LANGUAGE=\"JavaScript\">\n"
	    "<!-- Hide from non-JavaScript browsers\n"
	    "if ( authdesturl != null ) {\n"
	    "    document.write( contButtons );\n"
	    "} else {\n"
	    "    document.write( noContButtons );\n"
	    "}\n"
	    "// End hiding -->\n"
	    "</SCRIPT>\n"
	    "</BODY>\n</HTML>\n" );

	free( ckbuf );
	exit( 0 );
    }
}




static void
do_autherror( int rc, char *msg, char *lderrtxt, int omitclosebutton )
{
    dsgw_send_header();
    dsgw_emits( "<HTML>" );
    dsgw_head_begin();
    dsgw_emitf( "\n"
	"<TITLE>Authentication Error</TITLE></HEAD>\n"
	"<BODY %s>\n"
	"<CENTER>\n"
	"<FONT SIZE=+2>\n", dsgw_html_body_colors );

    dsgw_emits( XP_GetClientStr( DBT_AuthenticationFailed_ ));
    dsgw_emits( 
	"</FONT>\n"
	"</CENTER>\n"
	"<P>\n");
    if ( msg != NULL ) {
	dsgw_emitf( "%s %s\n", 
	    XP_GetClientStr( DBT_AuthenticationFailedBecause_ ),
	    msg );
    } else {
	switch ( rc ) {
	case LDAP_NO_SUCH_OBJECT:
	    dsgw_emits( XP_GetClientStr( DBT_AuthEntryNotExist_ ));
	    break;
	case LDAP_INAPPROPRIATE_AUTH:
	    dsgw_emits( XP_GetClientStr( DBT_AuthEntryHasNoPassword_ ));
	    break;
	case LDAP_INVALID_CREDENTIALS:
	    dsgw_emits( XP_GetClientStr( DBT_thePasswordIsIncorrect_ ));
	    break;
	case DSGW_CKDB_KEY_NOT_PRESENT:
	case DSGW_CKDB_DBERROR:
	case DSGW_CKDB_EXPIRED:
	case DSGW_CKDB_RNDSTRFAIL:
	case DSGW_CKDB_NODN:
	case DSGW_CKDB_CANTOPEN:
	case DSGW_CKDB_CANTAPPEND:
	    dsgw_emitf( XP_GetClientStr( DBT_AuthUnexpectedError_ ), dsgw_err2string( rc ));
	    break;
	default:
	    dsgw_emitf( XP_GetClientStr( DBT_AuthUnexpectedError_ ), dsgw_ldaperr2string( rc ));
	    break;
	}
    }
    if ( lderrtxt != NULL ) {
	dsgw_emitf( "<BR>(%s)", lderrtxt );
    }
    dsgw_emits( "<P>\n" );
    dsgw_form_begin( NULL, NULL );
    dsgw_emits(
	"\n"
	"<TABLE BORDER=2 WIDTH=100%%>\n"
	"<TR>\n" );
    if ( omitclosebutton ) {
	dsgw_emitf( "<TD ALIGN=CENTER WIDTH=33%%>\n"
	    "<INPUT TYPE=BUTTON VALUE=\"%s\" onClick=\"history.back()\">\n"
	    "<TD ALIGN=CENTER WIDTH=33%%>\n",
	    XP_GetClientStr( DBT_Retry_ ));
	dsgw_emit_homebutton();
	dsgw_emits ( "<TD ALIGN=CENTER WIDTH=34%%>\n" );
	dsgw_emit_helpbutton( "AUTHPROBLEM" );
    } else {
	dsgw_emitf( "<TD ALIGN=CENTER WIDTH=33%%>\n"
	    "<INPUT TYPE=BUTTON VALUE=\"%s\" onClick=\"history.back()\">\n"
	    "<TD ALIGN=CENTER WIDTH=33%%>\n"
	    "<INPUT TYPE=BUTTON VALUE=\"%s\" "
	    "onClick=\"parent.close();\">\n"
	    "<TD ALIGN=CENTER WIDTH=34%%>\n",
	    XP_GetClientStr( DBT_Retry_ ),
	    XP_GetClientStr( DBT_closeWindow_5 ));
	dsgw_emit_helpbutton( "AUTHPROBLEM" );
    }
    dsgw_emits( "</TABLE>\n"
	"</FORM>\n"
	"</BODY></HTML>\n" );
    fflush( stdout );
    return;
}
