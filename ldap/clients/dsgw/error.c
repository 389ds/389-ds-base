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
 * error.c -- error handling functions -- HTTP gateway
 */

#include "dsgw.h"
#include "dbtdsgw.h"

static char *dsgw_ldaperr2longstring( int err, int options );

struct dsgwerr {
    int		dsgwerr_code;
    int		dsgwerr_msg;
};


/* all of the DSGW_ERR_... #defines are in dsgw.h */
static struct dsgwerr dsgw_errs[] = {
    { DSGW_ERR_BADMETHOD,
        DBT_unknownHttpRequestMethod_ },
    { DSGW_ERR_BADFORMDATA,
        DBT_invalidOrIncompleteHtmlFormData_ },
    { DSGW_ERR_NOMEMORY,
        DBT_outOfMemory_ },
    { DSGW_ERR_MISSINGINPUT,
        DBT_requiredQueryFormInputIsMissing_ },
    { DSGW_ERR_BADFILEPATH,
        DBT_illegalCharacterInFilePath_ },
    { DSGW_ERR_BADCONFIG,
        DBT_badOrMissingConfigurationFile_ },
    { DSGW_ERR_LDAPINIT,
        DBT_unableToInitializeLdap_ },
    { DSGW_ERR_LDAPGENERAL,
        DBT_anErrorOccurredWhileContactingTh_ },
    { DSGW_ERR_UNKSRCHTYPE,
        DBT_unknownSearchObjectType_ },
    { DSGW_ERR_UNKATTRLABEL,
        DBT_unknownAttributeLabel_ },
    { DSGW_ERR_UNKMATCHPROMPT,
        DBT_unknownMatchPrompt_ },
    { DSGW_ERR_NOFILTERS,
        DBT_noSearchFiltersForObjectType_ },
    { DSGW_ERR_OPENHTMLFILE,
        DBT_unableToOpenHtmlTemplateFile_ },
    { DSGW_ERR_SEARCHMODE,
        DBT_unknownSearchModeUseSmartComplex_ },
    { DSGW_ERR_LDAPURL_NODN,
        DBT_distinguishedNameMissingInUrl_ },
    { DSGW_ERR_LDAPURL_BADSCOPE,
        DBT_unknownScopeInUrlShouldBeBaseSub_ },
    { DSGW_ERR_LDAPURL_NOTLDAP,
        DBT_unrecognizedUrlOrUnknownError_ },
    { DSGW_ERR_LDAPURL_BAD,
        DBT_badUrlFormat_ },
    { DSGW_ERR_INTERNAL,
        DBT_internalError_ },
    { DSGW_ERR_WRITEINDEXFILE,
        DBT_unableToWriteTemplateIndexFile_ },
    { DSGW_ERR_OPENINDEXFILE,
        DBT_unableToOpenTemplateIndexFile_ },
    { DSGW_ERR_OPENDIR,
        DBT_unableToReadDirectory_ },
    { DSGW_ERR_SSLINIT,
        DBT_ldapSslInitializationFailedCheck_ },
    { DSGW_ERR_NOSECPATH,
        DBT_forTheUsersAndGroupsFormsToWorkO_ },
    { DSGW_CKDB_KEY_NOT_PRESENT,
        DBT_authenticationCredentialsNotFoun_ },
    { DSGW_CKDB_DBERROR,
        DBT_errorRetrievingDataFromTheAuthen_ },
    { DSGW_CKDB_EXPIRED,
        DBT_yourAuthenticationCredentialsHav_ },
    { DSGW_CKDB_RNDSTRFAIL,
        DBT_unableToCreateRandomString_ },
    { DSGW_CKDB_NODN,
        DBT_noDistinguishedNameWasProvidedWh_ },
    { DSGW_CKDB_CANTOPEN,
        DBT_cannotOpenAuthenticationDatabase_ },
    { DSGW_CKDB_CANTAPPEND,
        DBT_couldNotAppendDataToTheAuthentic_ },
    { DSGW_ERR_NO_MGRDN,
        DBT_noDirectoryManagerIsDefined_ },
    { DSGW_ERR_NOSEARCHSTRING,
        DBT_noSearchStringWasProvidedPleaseT_ },
    { DSGW_ERR_CONFIGTOOMANYARGS,
        DBT_tooManyArgumentsOnOneLineInTheCo_ },
    { DSGW_ERR_WSAINIT,
        DBT_failedToInitializeWindowsSockets_ },
    { DSGW_ERR_ADMSERV_CREDFAIL,
        DBT_authenticationCredentialsCouldNo_ },
    { DSGW_ERR_LDAPDBURL_NODN,
        DBT_distinguishedNameMissingInLdapdb_ },
    { DSGW_ERR_LDAPDBURL_NOTLDAPDB,
        DBT_unrecognizedUrlOrUnknownError_1 },
    { DSGW_ERR_LDAPDBURL_BAD,
        DBT_badUrlFormat_1 },
    { DSGW_ERR_LCACHEINIT,
        DBT_anErrorOccurredWhileInitializing_ },
    { DSGW_ERR_SERVICETYPE,
        DBT_unknownDirectoryServiceTypeUseLo_ },
    { DSGW_ERR_DBCONF,
        DBT_anErrorOccurredWhileReadingTheDb_ },
    { DSGW_ERR_USERDB_PATH,
        DBT_nshomeUserdbPathWasNull_ },
    { DSGW_ERR_UPDATE_DBSWITCH,
        DBT_theDirectoryServiceConfiguration_ },
    { DSGW_ERR_ENTRY_NOT_FOUND,
        DBT_theEntryCouldNotBeReadFromTheDir_ },
    { DSGW_ERR_DB_ERASE,
        DBT_theLdapDatabaseCouldNotBeErased_ },
    { DSGW_ERR_LOCALDB_PERMISSION_DENIED,
        DBT_youMayNotChangeEntriesBesidesYou_ },
    { DSGW_ERR_NOATTRVALUE,
		DBT_theAttributeValueRequestedWasNot_ },
    { DSGW_ERR_USERID_REQUIRED,
	    /* "A value must be specified for NT User Id" */
		DBT_aValueMustBeSpecifiedForNTUserId },
    { DSGW_ERR_DOMAINID_NOTUNIQUE,
	    /* "The combination of NT User Id, NT Domain Id */
		/* is not unique in the directory" */
		DBT_theCombinationOfNTUserIdNTDomain_ },
    { DSGW_ERR_USERID_DOMAINID_REQUIRED,
		/* "Values must be specified for both NT */
		/* User Id and NT Domain Id" */
		DBT_valuesMustBeSpecifiedForBothNTUser_ },
    { DSGW_ERR_USERID_MAXLEN_EXCEEDED,
		/* "The NT User Id value must not exceed 20 characters in length." */
		DBT_theNTUserIdValueMustNotExceed_ },
    { DSGW_ERR_CHARSET_NOT_SUPPORTED,
		/* "The charset %s is not supported" */
		DBT_theCharsetIsNotSupported },
};
#define DSGW_ERROR_CNT	( sizeof( dsgw_errs ) / sizeof( struct dsgwerr ))



/*
 * dsgw_error -- report error as HTML text
 */
void
dsgw_error( int err, char *extra, int options, int ldaperr, char *lderrtxt )
{
    char	*msg, *prelude = XP_GetClientStr(DBT_problem_);

    if (( options & DSGW_ERROPT_IGNORE ) != 0 ) {
	return;
    }

    if (( options & DSGW_ERROPT_INLINE ) == 0 ) {
	dsgw_send_header();
	dsgw_html_begin( prelude, 1 );
    }

    msg = dsgw_err2string( err );

    dsgw_emitf( "<FONT SIZE=\"+1\">\n%s\n</FONT>\n", msg );
    if ( extra != NULL ) {
	if ( lderrtxt == NULL ) {
	    dsgw_emitf( "<BR>(%s)", extra );
	} else {
	    dsgw_emitf( "<BR>(%s - %s)", extra, lderrtxt );
	}
    } else if ( lderrtxt != NULL ) {
	dsgw_emitf( "<BR>(%s)", lderrtxt );
    }

#ifdef DSGW_DEBUG
    if ( extra == NULL ) {
	dsgw_log( "%s: %s\n", prelude, msg );
     } else {
	dsgw_log( "%s: %s (%s)\n", prelude, msg, extra );
    }
#endif
    if ( ldaperr  != 0 ) {
	msg = dsgw_ldaperr2longstring( ldaperr, options );
	dsgw_emitf("<P>%s", msg );
    }

    if (( options & DSGW_ERROPT_INLINE ) == 0 ) {
	dsgw_html_end();
    }

    if (( options & DSGW_ERROPT_EXIT ) != 0 ) {
	exit( 0 );
    }
}


/*
 * special handling for cookie expired or cookie database problems
 *	delete cookie on both server and client
 *	send helpful error with appropriate buttons:
 * * if searching, display an error message, and a re-auth button, along
 *   with a help button.
 * * if authenticating,  (does this ever happen?)
 * * if generating an editable view, display an error messge, and tell
 *   user to bring main window to front and requthenticate.
 * * if submitting a modify operation, include an "Authenticate" button
 *   which brings up a new auth window, which only offers you a
 *   "close" button when finished.
 *
 * returns 1 if the CGI should exit.
 * 0 if it should continue. - RJP
 */
int
dsgw_dn2passwd_error( int ckrc, int skipauthwarning )
{
    char	*authck;

    /*
     * cookie is expired or bad -- delete it on both server and client sides
     * 
     */
    if (( authck = dsgw_get_auth_cookie()) != NULL ) {
	dsgw_delcookie( authck );
    }

    /* pop up a javascript alert */
    if (gc->gc_mode == DSGW_MODE_DOSEARCH) {
	/* Just display a helpful error message */
	if (ckrc != DSGW_CKDB_KEY_NOT_PRESENT && !skipauthwarning) {
	    dsgw_send_header();
	    dsgw_emit_alertForm();
	    dsgw_emits( "<SCRIPT LANGUAGE=JavaScript><!--\n" );
	    dsgw_emit_alert (NULL, NULL, dsgw_err2string( ckrc ),
			     0L, "", "", "");
	    dsgw_emits( "// -->\n</SCRIPT>\n");
	}
	return(0);
    }
    dsgw_send_header();

    dsgw_html_begin( XP_GetClientStr(DBT_authenticationProblem_), 1 );
    
    dsgw_emits( "<SCRIPT LANGUAGE=\"JavaScript\">\n<!-- hide\n\n" );
    dsgw_emitf( "document.cookie = '%s=%s; path=/';\n\n", DSGW_AUTHCKNAME,
	       DSGW_UNAUTHSTR );
    
    dsgw_emits( "function reAuth()\n{\n" );
    dsgw_emitf( "    a = open( '%s?context=%s', 'AuthWin');\n",
	       dsgw_getvp( DSGW_CGINUM_AUTH ), context);
    dsgw_emits( "    a.opener = self;\n" );
    dsgw_emits( "    a.closewin = false;\n" );
    dsgw_emits( "}\n// end hiding -->\n</SCRIPT>\n" );
    
    dsgw_emits( dsgw_err2string( ckrc ) );

    if (gc->gc_mode == DSGW_MODE_EDIT || gc->gc_mode == DSGW_MODE_DOMODIFY) {

	dsgw_emits( XP_GetClientStr(DBT_NPYouMustReAuthenticateBeforeCon_1) );
	dsgw_emits( "<P>\n" );
	dsgw_form_begin( NULL, NULL );
	dsgw_emits("\n<CENTER><TABLE border=2 width=\"100%\"><TR>\n" );
	dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	dsgw_emitf( "<INPUT TYPE=\"button\" VALUE=\"%s\" "
		"onClick=\"top.close();\">\n",
                XP_GetClientStr(DBT_closeWindow_4) );
	dsgw_emits( "<TD WIDTH=\"50%\" ALIGN=\"center\">\n" );
	dsgw_emit_helpbutton( "AUTHEXPIRED" );
	dsgw_emits( "\n</TABLE></CENTER></FORM>\n" );

    }
    
    dsgw_html_end();
    return(1);
}


char *
dsgw_err2string( int err )
{
    int		i;

    for ( i = 0; i < DSGW_ERROR_CNT; ++i ) {
        if ( dsgw_errs[ i ].dsgwerr_code == err ) {
            return( XP_GetClientStr(dsgw_errs[ i ].dsgwerr_msg) );
        }
    }

    return( XP_GetClientStr(DBT_unknownError_) );
}


static char *
dsgw_ldaperr2longstring( int err, int options )
{
    char 	*s = "";

    switch ( err ) {
    case LDAP_SUCCESS:
	s = XP_GetClientStr(DBT_theOperationWasSuccessful_);
	break;
    case LDAP_OPERATIONS_ERROR:
	s = XP_GetClientStr(DBT_anInternalErrorOccurredInTheServ_);
	break;
    case LDAP_PROTOCOL_ERROR:
	s = XP_GetClientStr(DBT_theServerCouldNotUnderstandTheRe_);
	break;
    case LDAP_TIMELIMIT_EXCEEDED:
	s = XP_GetClientStr(DBT_aTimeLimitWasExceededInRespondin_);
	break;
    case LDAP_SIZELIMIT_EXCEEDED:
	s = XP_GetClientStr(DBT_aSizeLimitWasExceededInRespondin_);
	break;
    case LDAP_COMPARE_FALSE:
	break;
    case LDAP_COMPARE_TRUE:
	break;
    case LDAP_STRONG_AUTH_NOT_SUPPORTED:
	s = XP_GetClientStr(DBT_theGatewayAttemptedToAuthenticat_);
	break;
    case LDAP_STRONG_AUTH_REQUIRED:
	s = XP_GetClientStr(DBT_theGatewayAttemptedToAuthenticat_1);
	break;
#ifdef LDAP_REFERRAL				/* new in LDAPv3 */
    case LDAP_REFERRAL:
#endif
    case LDAP_PARTIAL_RESULTS:
	s = XP_GetClientStr(DBT_yourRequestCouldNotBeFulfilledPr_);
	break;
#ifdef LDAP_ADMIN_LIMIT_EXCEEDED		/* new in LDAPv3 */
    case LDAP_ADMIN_LIMIT_EXCEEDED:
	s = XP_GetClientStr(DBT_yourRequestExceededAnAdministrat_);
	break;
#endif
#ifdef LDAP_UNAVAILABLE_CRITICAL_EXTENSION	/* new in LDAPv3 */
    case LDAP_UNAVAILABLE_CRITICAL_EXTENSION:
	s = XP_GetClientStr(DBT_aCriticalExtensionThatTheGateway_);
	break;
#endif
    case LDAP_NO_SUCH_ATTRIBUTE:
	s = XP_GetClientStr(DBT_theServerWasUnableToProcessTheRe_);
	break;
    case LDAP_UNDEFINED_TYPE:
	break;
    case LDAP_INAPPROPRIATE_MATCHING:
	break;
    case LDAP_CONSTRAINT_VIOLATION:
	s = XP_GetClientStr(DBT_theServerWasUnableToFulfillYourR_);
	break;
    case LDAP_TYPE_OR_VALUE_EXISTS:
	s = XP_GetClientStr(DBT_theServerCouldNotAddAValueToTheE_);
	break;
    case LDAP_INVALID_SYNTAX:
	break;
    case LDAP_NO_SUCH_OBJECT:
	if (( options & DSGW_ERROPT_DURINGBIND ) == 0 ) {
	    s = XP_GetClientStr(DBT_theServerCouldNotLocateTheEntryI_);
	} else {
	    s = XP_GetClientStr(DBT_theServerCouldNotLocateTheEntryY_);
	}
	break;
    case LDAP_ALIAS_PROBLEM:
	break;
    case LDAP_INVALID_DN_SYNTAX:
	s = XP_GetClientStr(DBT_aDistinguishedNameWasNotInThePro_);
	break;
    case LDAP_IS_LEAF:
	break;
    case LDAP_ALIAS_DEREF_PROBLEM:
	break;
    case LDAP_INAPPROPRIATE_AUTH:
	s = XP_GetClientStr(DBT_theEntryYouAttemptedToAuthentica_);
	break;
    case LDAP_INVALID_CREDENTIALS:
	s = XP_GetClientStr(DBT_thePasswordOrOtherAuthentication_);
	break;
    case LDAP_INSUFFICIENT_ACCESS:
	s = XP_GetClientStr(DBT_youDoNotHaveSufficientPrivileges_);
	break;
    case LDAP_BUSY:
	s = XP_GetClientStr(DBT_theServerIsTooBusyToServiceYourR_);
	break;
    case LDAP_UNAVAILABLE:
	s = XP_GetClientStr(DBT_theLdapServerCouldNotBeContacted_);
	break;
    case LDAP_UNWILLING_TO_PERFORM:
	s = XP_GetClientStr(DBT_theServerWasUnwilliingToProcessY_);
	break;
    case LDAP_LOOP_DETECT:
	break;
    case LDAP_NAMING_VIOLATION:
	break;
    case LDAP_OBJECT_CLASS_VIOLATION:
	s = XP_GetClientStr(DBT_theDirectoryServerCouldNotHonorY_);
	break;
    case LDAP_NOT_ALLOWED_ON_NONLEAF:
	s = XP_GetClientStr(DBT_theDirectoryServerWillNotAllowYo_);
	break;
    case LDAP_NOT_ALLOWED_ON_RDN:
	break;
    case LDAP_ALREADY_EXISTS:
	s = XP_GetClientStr(DBT_theServerWasUnableToAddANewEntry_);
	break;
    case LDAP_NO_OBJECT_CLASS_MODS:
	break;
    case LDAP_RESULTS_TOO_LARGE:
	break;
#ifdef LDAP_AFFECTS_MULTIPLE_DSAS		/* new in LDAPv3 */
    case LDAP_AFFECTS_MULTIPLE_DSAS:
	s = XP_GetClientStr(DBT_yourRequestWouldAffectSeveralDir_);
	break;
#endif
    case LDAP_OTHER:
	break;
    case LDAP_SERVER_DOWN:
	s = XP_GetClientStr(DBT_theDirectoryServerCouldNotBeCont_);
	break;
    case LDAP_LOCAL_ERROR:
	break;
    case LDAP_ENCODING_ERROR:
	s = XP_GetClientStr(DBT_anErrorOccuredWhileSendingDataTo_);
	break;
    case LDAP_DECODING_ERROR:
	s = XP_GetClientStr(DBT_anErrorOccuredWhileReadingDataFr_);
	break;
    case LDAP_TIMEOUT:
	s = XP_GetClientStr(DBT_theServerDidNotRespondToTheReque_);
	break;
    case LDAP_AUTH_UNKNOWN:
	s = XP_GetClientStr(DBT_theServerDoesNotSupportTheAuthen_);
	break;
    case LDAP_FILTER_ERROR:
	s = XP_GetClientStr(DBT_theSearchFilterConstructedByTheG_);
	break;
    case LDAP_USER_CANCELLED:
	s = XP_GetClientStr(DBT_theOperationWasCancelledAtYourRe_);
	break;
    case LDAP_PARAM_ERROR:
	break;
    case LDAP_NO_MEMORY:
	s = XP_GetClientStr(DBT_anInternalErrorOccurredInTheLibr_);
	break;
    case LDAP_CONNECT_ERROR:
	s = XP_GetClientStr(DBT_aConnectionToTheServerCouldNotBe_);
	break;
    default:
	s = XP_GetClientStr(DBT_anUnknownErrorWasEncountered_);
    }
    return s;
}

static struct dsgwerr LDAP_errs[] = {
	{ LDAP_SUCCESS, 		DBT_LDAP_SUCCESS},
	{ LDAP_OPERATIONS_ERROR,	DBT_LDAP_OPERATIONS_ERROR},
	{ LDAP_PROTOCOL_ERROR,		DBT_LDAP_PROTOCOL_ERROR},
	{ LDAP_TIMELIMIT_EXCEEDED,	DBT_LDAP_TIMELIMIT_EXCEEDED},
	{ LDAP_SIZELIMIT_EXCEEDED,	DBT_LDAP_SIZELIMIT_EXCEEDED},
	{ LDAP_COMPARE_FALSE,		DBT_LDAP_COMPARE_FALSE},
	{ LDAP_COMPARE_TRUE,		DBT_LDAP_COMPARE_TRUE},
	{ LDAP_STRONG_AUTH_NOT_SUPPORTED, DBT_LDAP_STRONG_AUTH_NOT_SUPPORTED},
	{ LDAP_STRONG_AUTH_REQUIRED,	DBT_LDAP_STRONG_AUTH_REQUIRED},
	{ LDAP_PARTIAL_RESULTS,		DBT_LDAP_PARTIAL_RESULTS},
	{ LDAP_REFERRAL,		DBT_LDAP_REFERRAL},
	{ LDAP_ADMINLIMIT_EXCEEDED,	DBT_LDAP_ADMINLIMIT_EXCEEDED},
	{ LDAP_UNAVAILABLE_CRITICAL_EXTENSION, DBT_LDAP_UNAVAILABLE_CRITICAL_EXTENSION},
	{ LDAP_CONFIDENTIALITY_REQUIRED,DBT_LDAP_CONFIDENTIALITY_REQUIRED},
	{ LDAP_SASL_BIND_IN_PROGRESS,	DBT_LDAP_SASL_BIND_IN_PROGRESS},

	{ LDAP_NO_SUCH_ATTRIBUTE,	DBT_LDAP_NO_SUCH_ATTRIBUTE},
	{ LDAP_UNDEFINED_TYPE,		DBT_LDAP_UNDEFINED_TYPE},
	{ LDAP_INAPPROPRIATE_MATCHING,	DBT_LDAP_INAPPROPRIATE_MATCHING},
	{ LDAP_CONSTRAINT_VIOLATION,	DBT_LDAP_CONSTRAINT_VIOLATION},
	{ LDAP_TYPE_OR_VALUE_EXISTS,	DBT_LDAP_TYPE_OR_VALUE_EXISTS},
	{ LDAP_INVALID_SYNTAX,		DBT_LDAP_INVALID_SYNTAX},

	{ LDAP_NO_SUCH_OBJECT,		DBT_LDAP_NO_SUCH_OBJECT},
	{ LDAP_ALIAS_PROBLEM,		DBT_LDAP_ALIAS_PROBLEM},
	{ LDAP_INVALID_DN_SYNTAX,	DBT_LDAP_INVALID_DN_SYNTAX},
	{ LDAP_IS_LEAF,			DBT_LDAP_IS_LEAF},
	{ LDAP_ALIAS_DEREF_PROBLEM,	DBT_LDAP_ALIAS_DEREF_PROBLEM},

	{ LDAP_INAPPROPRIATE_AUTH,	DBT_LDAP_INAPPROPRIATE_AUTH},
	{ LDAP_INVALID_CREDENTIALS,	DBT_LDAP_INVALID_CREDENTIALS},
	{ LDAP_INSUFFICIENT_ACCESS,	DBT_LDAP_INSUFFICIENT_ACCESS},
	{ LDAP_BUSY,			DBT_LDAP_BUSY},
	{ LDAP_UNAVAILABLE,		DBT_LDAP_UNAVAILABLE},
	{ LDAP_UNWILLING_TO_PERFORM,	DBT_LDAP_UNWILLING_TO_PERFORM},
	{ LDAP_LOOP_DETECT,		DBT_LDAP_LOOP_DETECT},

	{ LDAP_NAMING_VIOLATION,	DBT_LDAP_NAMING_VIOLATION},
	{ LDAP_OBJECT_CLASS_VIOLATION,	DBT_LDAP_OBJECT_CLASS_VIOLATION},
	{ LDAP_NOT_ALLOWED_ON_NONLEAF,	DBT_LDAP_NOT_ALLOWED_ON_NONLEAF},
	{ LDAP_NOT_ALLOWED_ON_RDN,	DBT_LDAP_NOT_ALLOWED_ON_RDN},
	{ LDAP_ALREADY_EXISTS,		DBT_LDAP_ALREADY_EXISTS},
	{ LDAP_NO_OBJECT_CLASS_MODS,	DBT_LDAP_NO_OBJECT_CLASS_MODS},
	{ LDAP_RESULTS_TOO_LARGE,	DBT_LDAP_RESULTS_TOO_LARGE},
	{ LDAP_AFFECTS_MULTIPLE_DSAS,	DBT_LDAP_AFFECTS_MULTIPLE_DSAS},

	{ LDAP_OTHER,			DBT_LDAP_OTHER},
	{ LDAP_SERVER_DOWN,		DBT_LDAP_SERVER_DOWN},
	{ LDAP_LOCAL_ERROR,		DBT_LDAP_LOCAL_ERROR},
	{ LDAP_ENCODING_ERROR,		DBT_LDAP_ENCODING_ERROR},
	{ LDAP_DECODING_ERROR,		DBT_LDAP_DECODING_ERROR},
	{ LDAP_TIMEOUT,			DBT_LDAP_TIMEOUT},
	{ LDAP_AUTH_UNKNOWN,		DBT_LDAP_AUTH_UNKNOWN},
	{ LDAP_FILTER_ERROR,		DBT_LDAP_FILTER_ERROR},
	{ LDAP_USER_CANCELLED,		DBT_LDAP_USER_CANCELLED},
	{ LDAP_PARAM_ERROR,		DBT_LDAP_PARAM_ERROR},
	{ LDAP_NO_MEMORY,		DBT_LDAP_NO_MEMORY},
	{ LDAP_CONNECT_ERROR,		DBT_LDAP_CONNECT_ERROR},
	{ LDAP_NOT_SUPPORTED,		DBT_LDAP_NOT_SUPPORTED},
	{ LDAP_CONTROL_NOT_FOUND,	DBT_LDAP_CONTROL_NOT_FOUND},
	{ LDAP_NO_RESULTS_RETURNED,	DBT_LDAP_NO_RESULTS_RETURNED},
	{ LDAP_MORE_RESULTS_TO_RETURN,	DBT_LDAP_MORE_RESULTS_TO_RETURN},
	{ LDAP_CLIENT_LOOP,		DBT_LDAP_CLIENT_LOOP},
	{ LDAP_REFERRAL_LIMIT_EXCEEDED,	DBT_LDAP_REFERRAL_LIMIT_EXCEEDED}};

#define LDAP_ERROR_CNT	( sizeof( LDAP_errs ) / sizeof( struct dsgwerr ))

char *
dsgw_ldaperr2string( int lderr )
{
    auto int msgno = 0;
    auto int i;

    for ( i = 0; i < LDAP_ERROR_CNT; ++i ) {
        if ( LDAP_errs[ i ].dsgwerr_code == lderr ) {
            msgno = LDAP_errs[ i ].dsgwerr_msg;
	    break;
        }
    }
    if (msgno != 0) {
	auto char* msg = XP_GetClientStr(msgno);
	if (msg && *msg) return dsgw_ch_strdup( msg );
    }
    { /* get the message string from the LDAP SDK: */
	auto char* fmt = XP_GetClientStr(DBT_errorS_);
	auto char* s = ldap_err2string( lderr );
	auto char* msg = dsgw_ch_malloc( strlen( fmt ) + strlen( s ) + 20);
	PR_snprintf( msg, strlen(fmt) + strlen(s) + 20, fmt, s, lderr );
	return msg;
    }
}
