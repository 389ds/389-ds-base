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

#define LIBRARY_NAME "dsgw"

/* avoid warnings for this extremely annoying variable */
#ifdef LINUX
#define __DSGW_UNUSED __attribute__((__unused__))
#else
#define __DSGW_UNUSED
#endif
__DSGW_UNUSED static char dbtdsgwid[] = "$DBT: dsgw referenced v1 $";

#include "i18n.h"

BEGIN_STR(dsgw)
	ResDef( DBT_LibraryID_, -1, dbtdsgwid )/* extracted from dbtdsgw.h*/
	ResDef( DBT_unknownHttpRequestMethod_, 1, "Unknown HTTP request method." )/*extracted from error.c*/
	ResDef( DBT_invalidOrIncompleteHtmlFormData_, 2, "Invalid or incomplete HTML form data." )/*extracted from error.c*/
	ResDef( DBT_outOfMemory_, 3, "Out of memory." )/*extracted from error.c*/
	ResDef( DBT_requiredQueryFormInputIsMissing_, 4, "Required query/form input is missing." )/*extracted from error.c*/
	ResDef( DBT_illegalCharacterInFilePath_, 5, "Illegal character in file path." )/*extracted from error.c*/
	ResDef( DBT_badOrMissingConfigurationFile_, 6, "Bad or missing configuration file." )/*extracted from error.c*/
	ResDef( DBT_unableToInitializeLdap_, 7, "Unable to initialize LDAP." )/*extracted from error.c*/
	ResDef( DBT_anErrorOccurredWhileContactingTh_, 8, "An error occurred while contacting the LDAP server." )/*extracted from error.c*/
	ResDef( DBT_unknownSearchObjectType_, 9, "Unknown search object type." )/*extracted from error.c*/
	ResDef( DBT_unknownAttributeLabel_, 10, "Unknown attribute label." )/*extracted from error.c*/
	ResDef( DBT_unknownMatchPrompt_, 11, "Unknown match prompt." )/*extracted from error.c*/
	ResDef( DBT_noSearchFiltersForObjectType_, 12, "No search filters for object type." )/*extracted from error.c*/
	ResDef( DBT_unableToOpenHtmlTemplateFile_, 13, "Unable to open HTML template file." )/*extracted from error.c*/
	ResDef( DBT_unknownSearchModeUseSmartComplex_, 14, "Unknown search mode - use \"smart\", \"complex\", \"pattern\", or \"auth\"." )/*extracted from error.c*/
	ResDef( DBT_distinguishedNameMissingInUrl_, 15, "Distinguished Name missing in URL." )/*extracted from error.c*/
	ResDef( DBT_unknownScopeInUrlShouldBeBaseSub_, 16, "Unknown scope in URL (should be base, sub, or one)." )/*extracted from error.c*/
	ResDef( DBT_unrecognizedUrlOrUnknownError_, 17, "Unrecognized URL or unknown error." )/*extracted from error.c*/
	ResDef( DBT_badUrlFormat_, 18, "Bad URL format." )/*extracted from error.c*/
	ResDef( DBT_internalError_, 19, "Internal error." )/*extracted from error.c*/
	ResDef( DBT_unableToWriteTemplateIndexFile_, 20, "Unable to write template index file." )/*extracted from error.c*/
	ResDef( DBT_unableToOpenTemplateIndexFile_, 21, "Unable to open template index file." )/*extracted from error.c*/
	ResDef( DBT_unableToReadDirectory_, 22, "Unable to read directory." )/*extracted from error.c*/
	ResDef( DBT_ldapSslInitializationFailedCheck_, 23, "LDAP SSL initialization failed (check the security path)." )/*extracted from error.c*/
	ResDef( DBT_forTheUsersAndGroupsFormsToWorkO_, 24, "For the Users and Groups forms to work over SSL, you or your server administrator needs to activate SSL for this Administration Server.  The Encryption|On/Off page can be used to do so. " )/*extracted from error.c*/
	ResDef( DBT_authenticationCredentialsNotFoun_, 25, "Authentication credentials not found in authentication database." )/*extracted from error.c*/
	ResDef( DBT_errorRetrievingDataFromTheAuthen_, 26, "Error retrieving data from the authentication database." )/*extracted from error.c*/
	ResDef( DBT_yourAuthenticationCredentialsHav_, 27, "Your authentication credentials have expired." )/*extracted from error.c*/
	ResDef( DBT_unableToCreateRandomString_, 28, "Unable to create a random string." )/*extracted from error.c*/
	ResDef( DBT_noDistinguishedNameWasProvidedWh_, 29, "No distinguished name was provided when retrieving credentials." )/*extracted from error.c*/
	ResDef( DBT_cannotOpenAuthenticationDatabase_, 30, "Cannot open authentication database." )/*extracted from error.c*/
	ResDef( DBT_couldNotAppendDataToTheAuthentic_, 31, "Could not append data to the authentication database." )/*extracted from error.c*/
	ResDef( DBT_noDirectoryManagerIsDefined_, 32, "No Directory Manager is defined." )/*extracted from error.c*/
	ResDef( DBT_noSearchStringWasProvidedPleaseT_, 33, "No search string was provided.  Please try again." )/*extracted from error.c*/
	ResDef( DBT_tooManyArgumentsOnOneLineInTheCo_, 34, "Too many arguments on one line in the config. file." )/*extracted from error.c*/
	ResDef( DBT_failedToInitializeWindowsSockets_, 35, "Failed to initialize Windows Sockets." )/*extracted from error.c*/
	ResDef( DBT_authenticationCredentialsCouldNo_, 36, "Authentication credentials could not be obtained from the Administration Server." )/*extracted from error.c*/
	ResDef( DBT_distinguishedNameMissingInLdapdb_, 37, "Distinguished Name missing in ldapdb:// URL." )/*extracted from error.c*/
	ResDef( DBT_unrecognizedUrlOrUnknownError_1, 38, "Unrecognized URL or unknown error." )/*extracted from error.c*/
	ResDef( DBT_badUrlFormat_1, 39, "Bad URL format." )/*extracted from error.c*/
	ResDef( DBT_anErrorOccurredWhileInitializing_, 40, "An error occurred while initializing the local ldap database." )/*extracted from error.c*/
	ResDef( DBT_unknownDirectoryServiceTypeUseLo_, 41, "Unknown directory service type - use \"local\" or \"remote\"." )/*extracted from error.c*/
	ResDef( DBT_anErrorOccurredWhileReadingTheDb_, 42, "An error occurred while reading the db configuration file." )/*extracted from error.c*/
	ResDef( DBT_nshomeUserdbPathWasNull_, 43, "NSHOME/userdb path was NULL." )/*extracted from error.c*/
	ResDef( DBT_theDirectoryServiceConfiguration_, 44, "The directory service configuration could not be updated." )/*extracted from error.c*/
	ResDef( DBT_theEntryCouldNotBeReadFromTheDir_, 45, "The entry could not be read from the directory." )/*extracted from error.c*/
	ResDef( DBT_theLdapDatabaseCouldNotBeErased_, 46, "The LDAP database could not be erased." )/*extracted from error.c*/
	ResDef( DBT_youMayNotChangeEntriesBesidesYou_, 47, "You may not change entries besides your own." )/*extracted from error.c*/
	ResDef( DBT_problem_, 48, "Problem" )/*extracted from error.c*/
	ResDef( DBT_authenticationProblem_, 49, "Authentication Problem" )/*extracted from error.c*/
	ResDef( DBT_NPYouMustReAuthenticateBeforeCon_, 50, ".\n<P>You must re-authenticate before continuing.\n" )/*extracted from error.c*/
	ResDef( DBT_NPYouMustReAuthenticateBeforeCon_1, 51, ".\n<P>You must re-authenticate before continuing.\n" )/*extracted from error.c*/
	ResDef( DBT_unknownError_, 52, "unknown error" )/*extracted from error.c*/
	ResDef( DBT_theOperationWasSuccessful_, 53, "The operation was successful." )/*extracted from error.c*/
	ResDef( DBT_anInternalErrorOccurredInTheServ_, 54, "An internal error occurred in the server.  This usually\nindicates a serious malfunction in the server and should be\nbrought to the attention of your server administrator." )/*extracted from error.c*/
	ResDef( DBT_theServerCouldNotUnderstandTheRe_, 55, "The server could not understand the request which was sent to\nit by the gateway." )/*extracted from error.c*/
	ResDef( DBT_aTimeLimitWasExceededInRespondin_, 56, "A time limit was exceeded in responding to your request.  If\nyou are searching for entries, you may achieve better results\nif you are more specific in your search." )/*extracted from error.c*/
	ResDef( DBT_aSizeLimitWasExceededInRespondin_, 57, "A size limit was exceeded in responding to your request.  If\nyou are searching for entries, you may achieve better results\nif you are more specific in your search, because too many entries\nmatched your search criteria." )/*extracted from error.c*/
	ResDef( DBT_theGatewayAttemptedToAuthenticat_, 58, "The gateway attempted to authenticate to the server using\na method the server doesn't understand." )/*extracted from error.c*/
	ResDef( DBT_theGatewayAttemptedToAuthenticat_1, 59, "The gateway attempted to authenticate to the server using an\nauthentication method which the server does not support. " )/*extracted from error.c*/
	ResDef( DBT_yourRequestCouldNotBeFulfilledPr_, 60, "Your request could not be fulfilled, probably because the server\nthat was contacted does not contain the data you are looking\nfor.  It is possible that a referral to another server was\nreturned but could not be followed.  If you were trying to make\nchanges to the directory, it may be that the server that holds\nthe master copy of the data is not available." )/*extracted from error.c*/
	ResDef( DBT_yourRequestExceededAnAdministrat_, 61, "Your request exceeded an administrative limit in the server." )/*extracted from error.c*/
	ResDef( DBT_aCriticalExtensionThatTheGateway_, 62, "A critical extension that the gateway requested is not available in this server." )/*extracted from error.c*/
	ResDef( DBT_theServerWasUnableToProcessTheRe_, 63, "The server was unable to process the request, becase the\nrequest referred to an attribute which does not exist in the\nentry." )/*extracted from error.c*/
	ResDef( DBT_theServerWasUnableToFulfillYourR_, 64, "The server was unable to fulfill your request, because the\nrequest violates a constraint." )/*extracted from error.c*/
	ResDef( DBT_theServerCouldNotAddAValueToTheE_, 65, "The server could not add a value to the entry, because that\nvalue is already contained in the entry." )/*extracted from error.c*/
	ResDef( DBT_theServerCouldNotLocateTheEntryI_, 66, "The server could not locate the entry.  If adding a new entry,\nbe sure that the parent of the entry you are trying to add exists.\nIf you received this error while searching, it indicates that the\nentry which was being searched for does not exist.\nIf you were attempting to authenticate as the directory manager and\nreceived this error, check the gateway configuration file." )/*extracted from error.c*/
	ResDef( DBT_aDistinguishedNameWasNotInThePro_, 67, "A distinguished name was not in the proper format. " )/*extracted from error.c*/
	ResDef( DBT_theEntryYouAttemptedToAuthentica_, 68, "The entry you attempted to authenticate as does not have a\npassword set, or is missing other required authentication\ncredentials.  You cannot authenticate as that entry until the\nappropriate attributes have been added by the directory manager. " )/*extracted from error.c*/
	ResDef( DBT_thePasswordOrOtherAuthentication_, 69, "The password (or other authentication credentials) you supplied\nis incorrect." )/*extracted from error.c*/
	ResDef( DBT_youDoNotHaveSufficientPrivileges_, 70, "You do not have sufficient privileges to perform the operation. " )/*extracted from error.c*/
	ResDef( DBT_theServerIsTooBusyToServiceYourR_, 71, "The server is too busy to service your request.  Try again\nin a few minutes." )/*extracted from error.c*/
	ResDef( DBT_theLdapServerCouldNotBeContacted_, 72, "The LDAP server could not be contacted." )/*extracted from error.c*/
	ResDef( DBT_theServerWasUnwilliingToProcessY_, 73, "The server was unwilling to process your request.  Usually,\nthis indicates that serving your request would put a heavy load\non the server.  It may also indicate that the server is not\nconfigured to process your request.  If searching, you may wish\nto limit the scope of your search." )/*extracted from error.c*/
	ResDef( DBT_theDirectoryServerCouldNotHonorY_, 74, "The directory server could not honor your request because it\nviolates the schema requirements.  Typically, this means that you\nhave not provided a value for a required field.  It could also mean\nthat the schema in the directory server needs to be updated." )/*extracted from error.c*/
	ResDef( DBT_theDirectoryServerWillNotAllowYo_, 75, "The directory server will not allow you to delete or rename\nan entry if that entry has children.  If you wish to do this, you\nmust first delete all the child entries." )/*extracted from error.c*/
	ResDef( DBT_theServerWasUnableToAddANewEntry_, 76, "The server was unable to add a new entry, or rename an existing\nentry, because an entry by that name already exists." )/*extracted from error.c*/
	ResDef( DBT_yourRequestWouldAffectSeveralDir_, 77, "Your request would affect several directory servers." )/*extracted from error.c*/
	ResDef( DBT_theDirectoryServerCouldNotBeCont_, 78, "The directory server could not be contacted.  Contact your\nserver administrator for assistance." )/*extracted from error.c*/
	ResDef( DBT_anErrorOccuredWhileSendingDataTo_, 79, "An error occured while sending data to the server." )/*extracted from error.c*/
	ResDef( DBT_anErrorOccuredWhileReadingDataFr_, 80, "An error occured while reading data from the server." )/*extracted from error.c*/
	ResDef( DBT_theServerDidNotRespondToTheReque_, 81, "The server did not respond to the request. \nThe request timed out." )/*extracted from error.c*/
	ResDef( DBT_theServerDoesNotSupportTheAuthen_, 82, "The server does not support the authentication method used\nby the gateway." )/*extracted from error.c*/
	ResDef( DBT_theSearchFilterConstructedByTheG_, 83, "The search filter constructed by the gateway was in error." )/*extracted from error.c*/
	ResDef( DBT_theOperationWasCancelledAtYourRe_, 84, "The operation was cancelled at your request." )/*extracted from error.c*/
	ResDef( DBT_anInternalErrorOccurredInTheLibr_, 85, "An internal error occurred in the library - a parameter was\nincorrect." )/*extracted from error.c*/
	ResDef( DBT_aConnectionToTheServerCouldNotBe_, 86, "A connection to the server could not be opened. Contact your\nserver administrator for assistance." )/*extracted from error.c*/
	ResDef( DBT_anUnknownErrorWasEncountered_, 87, "An unknown error was encountered." )/*extracted from error.c*/
	ResDef( DBT_entryAlreadyExists_, 88, "Entry Already Exists" )/*extracted from edit.c*/
	ResDef( DBT_anEntryNamed_, 89, "An entry named " )/*extracted from edit.c*/
	ResDef( DBT_onmouseoverWindowStatusClickHere_, 90, "onMouseOver=\"window.status='Click here to view this entry'; return true\"" )/*extracted from edit.c*/
	ResDef( DBT_alreadyExistsPPleaseChooseAnothe_, 91, " already exists.<P>Please choose another name and/or location.\n<P>\n" )/*extracted from edit.c*/
	ResDef( DBT_parentEntryDoesNotExist_, 92, "Parent entry does not exist" )/*extracted from edit.c*/
	ResDef( DBT_youCannotAddAnEntryByTheNamePBSB_, 93, "You cannot add an entry by the name:<P><B>%s</B>,<P>\nbecause the parent of that entry does not exist.<P>\nBefore you can add this entry, you must first add\n" )/*extracted from edit.c*/
	ResDef( DBT_itsParentN_, 94, "its parent.\n" )/*extracted from edit.c*/
	ResDef( DBT_anEntryNamedPBSBN_, 95, "an entry named:<P><B>%s</B>.\n" )/*extracted from edit.c*/
	ResDef( DBT_warningNoAuthenticationContinuin_, 96, "Warning:  no authentication (continuing)...\n" )/*extracted from domodify.c*/
	ResDef( DBT_SDirectoryEntry_, 97, "%s Directory Entry" )/*extracted from domodify.c*/
	ResDef( DBT_PreEntryDnSPrePN_, 98, "<PRE>Entry DN: %s</PRE><P>\n" )/*extracted from domodify.c*/
	ResDef( DBT_changesToBSBHaveBeenSaved_, 99, "Changes to <B>%s</B> have been saved." )/*extracted from domodify.c*/
	ResDef( DBT_BSBHasBeenAdded_, 100, "<B>%s</B> has been added." )/*extracted from domodify.c*/
	ResDef( DBT_BSBHasBeenDeleted_, 101, "<B>%s</B> has been deleted." )/*extracted from domodify.c*/
	ResDef( DBT_renamedBSBToBSB_, 102, "Renamed <B>%s</B> to <B>%s</B>." )/*extracted from domodify.c*/
	ResDef( DBT_PBNoteBBecauseYouSTheEntryYouWer_, 103, "<P><B>Note:</B>  because you %s the entry you were \nauthenticated as, it was necessary to discard your \nauthentication credentials. You will need to authenticate \nagain to make additional changes.\n" )/*extracted from domodify.c*/
	ResDef( DBT_deleted_, 104, "deleted" )/*extracted from domodify.c*/
	ResDef( DBT_renamed_, 105, "renamed" )/*extracted from domodify.c*/
	ResDef( DBT_changedThePasswordOf_, 106, "changed the password of" )/*extracted from domodify.c*/
	ResDef( DBT_attributeSWasChangedBrN_, 107, "Attribute %s was changed<BR>\n" )/*extracted from domodify.c*/
	ResDef( DBT_TnotAsciiLdBytesN_, 108, "\tNOT ASCII (%ld bytes)\n" )/*extracted from domodify.c*/
	ResDef( DBT_noValuesWereEnteredPleaseTryAgai_, 109, "No values were entered.  Please try again.\n" )/*extracted from domodify.c*/
	ResDef( DBT_noChangesWereMadeN_, 110, "No changes were made.\n" )/*extracted from domodify.c; XXXmcs: no longer used*/
	ResDef( DBT_PSendingSToTheDirectoryServerN_, 111, "<P>Sending %s to the directory server...\n" )/*extracted from domodify.c*/
	ResDef( DBT_information_, 112, "information" )/*extracted from domodify.c*/
	ResDef( DBT_changes_, 113, "changes" )/*extracted from domodify.c*/
	ResDef( DBT_PSuccessfullyAddedEntryN_, 114, "<P>Successfully added entry.\n" )/*extracted from domodify.c*/
	ResDef( DBT_PSuccessfullyEditedEntryYourChan_, 115, "<P>Successfully edited entry.  Your changes have been saved.\n" )/*extracted from domodify.c*/
	ResDef( DBT_PSuccessfullyDeletedEntryN_, 116, "<P>Successfully deleted entry.\n" )/*extracted from domodify.c*/
	ResDef( DBT_PreTheNewNameForTheEntryIsSNPreH_, 117, "<PRE>The new name for the entry is: %s\n</PRE><HR>\n" )/*extracted from domodify.c*/
	ResDef( DBT_PSuccessfullyRenamedEntryN_, 118, "<P>Successfully renamed entry.\n" )/*extracted from domodify.c*/
	ResDef( DBT_youMustProvideTheOldPassword_, 119, "You must provide the old password." )/*extracted from domodify.c*/
	ResDef( DBT_youMustProvideANewPasswordPlease_, 120, "You must provide a new password.  Please try again" )/*extracted from domodify.c*/
	ResDef( DBT_theNewAndConfirmingPasswordsDoNo_, 121, "The new and confirming passwords do not match.  Please try again" )/*extracted from domodify.c*/
	ResDef( DBT_BrTheSBSBIsAlreadyInUsePleaseCho_, 122, "<BR>The %s <B>%s</B> is already in use. Please choose a different one.<BR>\n" )/*extracted from domodify.c*/
	ResDef( DBT_missingFormDataElement100s_, 123, "missing form data element \"%.100s\"" )/*extracted from cgiutil.c*/
	ResDef( DBT_initializingConfigInfo_, 124, "Initializing config info" )/*extracted from config.c*/
	ResDef( DBT_cannotOpenFile_, 125, "Cannot open file." )/*extracted from config.c*/
	ResDef( DBT_malformedDbconfFile_, 126, "Malformed dbconf file." )/*extracted from config.c*/
	ResDef( DBT_missingPropertyNameInDbconfFile_, 127, "Missing property name in dbconf file." )/*extracted from config.c*/
	ResDef( DBT_outOfMemory_1, 128, "Out of memory." )/*extracted from config.c*/
	ResDef( DBT_missingDirectiveInDbconfFile_, 129, "Missing directive in dbconf file." )/*extracted from config.c*/
	ResDef( DBT_cannotOpenConfigFileSN_, 130, "Cannot open config file \"%s\"\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForAuthlifetimeDi_, 131, "Missing argument for \"authlifetime\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForDirmgrDirectiv_, 132, "Missing argument for \"dirmgr\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForBaseurlDirecti_, 133, "Missing argument for \"baseurl\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_badUrlProvidedForBaseurlDirectiv_, 134, "Bad URL provided for \"baseurl\" directive - the base DN is missing\n" )/*extracted from config.c*/
	ResDef( DBT_parsingBaseurlDirective_, 135, "parsing baseurl directive" )/*extracted from config.c*/
	ResDef( DBT_badUrlProvidedForBaseurlDirectiv_1, 136, "Bad URL provided for \"baseurl\" directive - not an \"ldap://\" URL\n" )/*extracted from config.c*/
	ResDef( DBT_LdapsUrlsAreNotYetSupportedN_, 137, "\"ldaps://\" URLs are not yet supported\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentsForTemplateDirec_, 138, "Missing arguments for \"template\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForSslrequiredDir_, 139, "Missing argument for \"sslrequired\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_unknownArgumentToSslrequiredDire_, 140, "Unknown argument to \"sslrequired\" directive (should be \"never\", \"whenauthenticated\", \"always\")\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForSecuritypathDi_, 141, "Missing argument for \"securitypath\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForLocationSuffix_, 142, "Missing argument for \"location-suffix\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_threeArgumentsAreRequiredForTheL_, 143, "Three arguments are required for the \"location\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_atLeastTwoArgumentsAreRequiredFo_, 144, "At least two arguments are required for the \"newtype\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_unknownLocationInNewtypeDirectiv_, 145, "Unknown location in \"newtype\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_threeOrFourArgumentsAreRequiredF_, 146, "Three or four arguments are required for the \"tmplset\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_fourArgumentsAreRequiredForTheAt_, 147, "Four arguments are required for the \"attrvset\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForCharsetDirecti_, 148, "Missing argument for \"charset\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForClientlanguage_, 149, "Missing argument for \"ClientLanguage\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForAdminlanguageD_, 150, "Missing argument for \"AdminLanguage\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForDefaultlanguag_, 151, "Missing argument for \"DefaultLanguage\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingFilenameForIncludeDirecti_, 152, "Missing filename for \"include\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_unknownDirectiveInConfigFileN_, 153, "Unknown directive in config file\n" )/*extracted from config.c*/
	ResDef( DBT_EraseDbCouldNotOpenLcacheConfFil_, 154, "<= erase_db could not open lcache.conf file \"%s\"\n" )/*extracted from config.c*/
	ResDef( DBT_FontSize1NPTheDatabaseHasBeenDel_, 155, "<FONT SIZE=\"+1\">\n<P>The database has been deleted. Creating new database... \n</FONT>\n " )/*extracted from config.c*/
	ResDef( DBT_FontSize1NPTheDatabaseCouldNotBe_, 156, "<FONT SIZE=\"+1\">\n<P>The database could not be deleted \n</FONT>\n " )/*extracted from config.c*/
	ResDef( DBT_AppSuffixCouldNotOpenLdifFileSN_, 157, "<= app_suffix could not open ldif file \"%s\"\n" )/*extracted from config.c*/
	ResDef( DBT_AppSuffixCouldNotOpenTmpFileSN_, 158, "<= app_suffix could not open tmp file \"%s\"\n" )/*extracted from config.c*/
	ResDef( DBT_unableToRenameSToS_, 159, "Unable to rename %s to %s" )/*extracted from config.c*/
	ResDef( DBT_nullPointerReturnedByDbconfReadD_, 160, "null pointer returned by dbconf_read_default_dbinfo()." )/*extracted from config.c*/
	ResDef( DBT_badLdapdbUrlTheBaseDnIsMissingN_, 161, "Bad \"ldapdb\" URL - the base DN is missing\n" )/*extracted from config.c*/
	ResDef( DBT_badLdapdbUrlN_, 162, "Bad \"ldapdb\" URL\n" )/*extracted from config.c*/
	ResDef( DBT_badUrlProvidedForBaseurlDirectiv_2, 163, "Bad URL provided for \"baseurl\" directive - the base DN is missing\n" )/*extracted from config.c*/
	ResDef( DBT_parsingBaseurlDirective_1, 164, "parsing baseurl directive" )/*extracted from config.c*/
	ResDef( DBT_badUrlProvidedForBaseurlDirectiv_3, 165, "Bad URL provided for \"baseurl\" directive - not an \"ldap:// or ldapdb://\" URL\n" )/*extracted from config.c*/
	ResDef( DBT_LdapsUrlsAreNotYetSupportedN_1, 166, "\"ldaps://\" URLs are not yet supported\n" )/*extracted from config.c*/
	ResDef( DBT_noValueGivenForBinddn_, 167, "No value given for binddn" )/*extracted from config.c*/
	ResDef( DBT_noValueGivenForBindpw_, 168, "No value given for bindpw" )/*extracted from config.c*/
	ResDef( DBT_thereIsNoDefaultDirectoryService_, 169, "There is no default directory service defined in the dbswitch.conf file" )/*extracted from config.c*/
	ResDef( DBT_cannotOpenConfigFileSForWritingN_, 170, "Cannot open config file \"%s\" for writing\n" )/*extracted from config.c*/
	ResDef( DBT_unableToRenameSToS_1, 171, "Unable to rename %s to %s" )/*extracted from config.c*/
	ResDef( DBT_configFileS_, 172, "config file %s: " )/*extracted from config.c*/
	ResDef( DBT_configFileSLineD_, 173, "config file %s: line %d: " )/*extracted from config.c*/
	ResDef( DBT_maxD_, 174, "max %d" )/*extracted from config.c*/
	ResDef( DBT_ok_, 175, " OK " )/*extracted from domodify.c*/
	ResDef( DBT_closeWindow_, 176, "Close Window" )/*extracted from domodify.c*/
	ResDef( DBT_goBack_, 177, "Go Back" )/*extracted from domodify.c*/
	ResDef( DBT_CryptLockedSGmt_, 178, "{crypt}LOCKED [%s GMT]" )/*extracted from domodify.c*/
	ResDef( DBT_returnToMain_, 179, "Return to Main" )/*extracted from dsgwutil.c*/
	ResDef( DBT_help_, 181, "  Help  " )/*extracted from dsgwutil.c*/
	ResDef( DBT_help_1, 182, "Help" )/*extracted from dsgwutil.c*/
	ResDef( DBT_helpIsNotYetAvailable_, 184, "Help is not yet available." )/*extracted from dsgwutil.c*/
	ResDef( DBT_closeWindow_1, 186, "Close Window" )/*extracted from edit.c*/
	ResDef( DBT_closeWindow_2, 187, "Close Window" )/*extracted from edit.c*/
	ResDef( DBT_missingTemplate_, 188, "The URL did not include a template name"
	        " (immediately following the '?')." )/*edit.c*/
	ResDef( DBT_authenticate_, 189, "Authenticate..." )/*extracted from emitauth.c*/
	ResDef( DBT_discardAuthenticationCredentials_, 190, "Discard authentication credentials (log out)?" )/*extracted from emitauth.c*/
	ResDef( DBT_youDidNotSupplyASearchString_, 191, "Please type a search string" )/*extracted from emitauth.c*/
	ResDef( DBT_theFirstStepInAuthenticatingToTh_, 192, "The first step in authenticating to the directory is identifying\nyourself.<br>Please type your name:" )/*extracted from emitauth.c*/
	ResDef( DBT_continue_, 193, "Continue" )/*extracted from emitauth.c*/
	ResDef( DBT_continue_1, 194, "Continue" )/*extracted from emitauth.c*/
	ResDef( DBT_cancel_, 195, "Cancel" )/*extracted from emitauth.c*/
	ResDef( DBT_authenticateAsDirectoryManagerNb_, 196, "Authenticate as directory manager\"> "
	       "\302\240" /* nbsp, in UTF-8 */
	       "(only available to Directory Administrators)\n" )/*extracted from emitauth.c*/
	ResDef( DBT_authenticate_1, 197, "Authenticate..." )/*extracted from emitauth.c*/
	ResDef( DBT_discardAuthenticationCredentials_1, 198, "Discard authentication credentials?" )/*extracted from emitauth.c*/
	ResDef( DBT_continue_2, 200, "Continue" )/*extracted from emitauth.c*/
	ResDef( DBT_continue_3, 201, "Continue" )/*extracted from emitauth.c*/
	ResDef( DBT_cancel_1, 202, "Cancel" )/*extracted from emitauth.c*/
	ResDef( DBT_authenticateLogInToTheDirectory_, 203, "Authenticate (log in) to the directory" )/*extracted from emitauth.c*/
	ResDef( DBT_youAreAboutToAuthenticate_, 204, "You are about to authenticate to the directory as"
	       " <B>%s</B>.  To complete the authentication process, type your password.\n" )
	ResDef( DBT_beforeYouCanEditOrAddEntriesYouM_, 206, "Before you can edit or add entries, you must authenticate\n(log in) to the directory.  This window will guide\nyou through the steps of the authentication\nprocess.\n" )/*extracted from emitauth.c*/
	ResDef( DBT_fromThisScreenYouMayAuthenticate_, 207, "From this screen you may authenticate, or log in, \nto the directory.  You will need to authenticate\nbefore you can modify directory entries.  If you\nattempt to modify an entry without authenticating,\nyou will be asked to log in.\n" )/*extracted from emitauth.c*/
	ResDef( DBT_authenticationStatus_, 208, "Authentication Status" )/*extracted from emitauth.c*/
	ResDef( DBT_FormNyouAreCurrentlyAuthenticate_, 209, "<form>\nYou are currently authenticated to the directory as " )/*extracted from emitauth.c*/
	ResDef( DBT_NifYouWishToDiscardYourAuthentic_, 210, ".\nIf you wish to discard your authentication credentials and log out of the directory, click on the button below." )/*extracted from emitauth.c*/
	ResDef( DBT_discardAuthenticationCredentials_2, 211, "Discard Authentication Credentials (log out)" )/*extracted from emitauth.c*/
	ResDef( DBT_yourAuthenticationCredentialsFor_, 212, "Your authentication credentials for " )/*extracted from emitauth.c*/
	ResDef( DBT_haveExpiredN_, 213, "have expired.\n<HR>\n" )/*extracted from emitauth.c*/
	ResDef( DBT_currentlyYouAreNotAuthenticatedT_, 214, "Currently, you are not authenticated to the directory.<HR>\n" )/*extracted from emitauth.c*/
	ResDef( DBT_missingS_, 215, "missing \"%s=\"" )/*extracted from entrydisplay.c*/
	ResDef( DBT_unknownSS_, 216, "unknown \"%s=%s\"" )/*extracted from entrydisplay.c*/
	ResDef( DBT_unknownOptionS_, 217, "unknown option %s" )/*extracted from entrydisplay.c*/
	ResDef( DBT_unknownSyntaxSN_, 218, "unknown syntax=%s\n" )/*extracted from entrydisplay.c*/
	ResDef( DBT_HtmlTypeSNotSupportedBrN_, 219, "** HTML type \"%s\" not supported **<BR>\n" )/*extracted from entrydisplay.c*/
	ResDef( DBT_edit_, 224, "Edit" )/*extracted from entrydisplay.c*/
	ResDef( DBT_saveChanges_, 225, "Save Changes" )/*extracted from entrydisplay.c*/
	ResDef( DBT_obsolete_226, 226, "modify" )/*extracted from entrydisplay.c*/
	ResDef( DBT_obsolete_227, 227, "add" )/*extracted from entrydisplay.c*/
	ResDef( DBT_delete_, 228, "Delete" )/*extracted from entrydisplay.c*/
	ResDef( DBT_deleteThisEntry_, 229, "Delete this entry?" )/*extracted from entrydisplay.c*/
	ResDef( DBT_rename_, 230, "Rename" )/*extracted from entrydisplay.c*/
	ResDef( DBT_enterANewNameForThisEntry_, 231, "Enter a new name for this entry:" )/*extracted from entrydisplay.c*/
	ResDef( DBT_editAs_, 232, "Edit As" )/*extracted from entrydisplay.c*/
	ResDef( DBT_missingS_1, 233, "missing %s=" )/*extracted from entrydisplay.c*/
	ResDef( DBT_closeWindow_3, 234, "Close Window" )/*extracted from entrydisplay.c*/
	ResDef( DBT_edit_1, 235, "Edit..." )/*extracted from entrydisplay.c*/
	ResDef( DBT_missingSN_, 236, "missing \"%s=\"\n" )/*extracted from entrydisplay.c*/
	ResDef( DBT_unknownSetSN_, 237, "unknown set \"%s\"\n" )/*extracted from entrydisplay.c*/
	ResDef( DBT_unknownSyntaxSN_1, 238, "unknown syntax \"%s\"\n" )/*extracted from entrydisplay.c*/
	ResDef( DBT_reAuthenticate_, 239, "Re-Authenticate" )/*extracted from error.c*/
	ResDef( DBT_closeWindow_4, 240, "Close Window" )/*extracted from error.c*/
	ResDef( DBT_obsolete_241, 241, "Do you really want to " )/*extracted from htmlparse.c*/
	ResDef( DBT_obsolete_242, 242, "?" )/*extracted from htmlparse.c*/
	ResDef( DBT_ok_1, 243, "   OK   " )/*extracted from htmlparse.c*/
	ResDef( DBT_ok_2, 244, "   OK   " )/*extracted from htmlparse.c*/
	ResDef( DBT_reset_, 245, " Reset " )/*extracted from htmlparse.c*/
	ResDef( DBT_done_, 246, "  Done  " )/*extracted from htmlparse.c*/
	ResDef( DBT_cancel_2, 247, " Cancel " )/*extracted from htmlparse.c*/
	ResDef( DBT_foundAnotherIfNestedIfsAreNotSup_, 248, "found another IF (nested IFs are not supported)" )/*extracted from htmlparse.c*/
	ResDef( DBT_foundElseButDidnTSeeAnIf_, 249, "found ELSE but didn't see an IF" )/*extracted from htmlparse.c*/
	ResDef( DBT_foundElseAfterElseExpectingEndif_, 250, "found ELSE after ELSE (expecting ENDIF)" )/*extracted from htmlparse.c*/
	ResDef( DBT_foundElifButDidnTSeeAnIf_, 251, "found ELIF but didn't see an IF" )/*extracted from htmlparse.c*/
	ResDef( DBT_foundElifAfterElseExpectingEndif_, 252, "found ELIF after ELSE (expecting ENDIF)" )/*extracted from htmlparse.c*/
	ResDef( DBT_foundEndifButDidnTSeeAnIf_, 253, "found ENDIF but didn't see an IF" )/*extracted from htmlparse.c*/
	ResDef( DBT_BrBTemplateErrorBSBrN_, 254, "<BR><B>template error:</B> %s<BR>\n" )/*extracted from htmlparse.c*/
	ResDef( DBT_ldapInitLcacheInitAttemptedBefor_, 255, "ldap_init/lcache_init attempted before config file read" )/*extracted from ldaputil.c*/
	ResDef( DBT_notRunningUnderTheAdministration_, 256, "not running under the administration server" )/*extracted from ldaputil.c*/
	ResDef( DBT_couldNotInitializePermissions_, 257, "Could not initialize permissions" )/*extracted from ldaputil.c*/
	ResDef( DBT_couldNotMapUsernameToADnErrorFro_, 258, "Could not map username to a DN (error from admin server)" )/*extracted from ldaputil.c*/
	ResDef( DBT_couldNotGetCurrentUsername_, 259, "Could not get current username" )/*extracted from ldaputil.c*/
	ResDef( DBT_couldNotGetCurrentUserPassword_, 260, "Could not get current user password" )/*extracted from ldaputil.c*/
	ResDef( DBT_obsolete_261, 261, "Error: %s" )/*extracted from ldaputil.c*/
	ResDef( DBT_noteThereIsNoDisplayTemplateForT_, 262, "Note: there is no display template for this type of entry available, so it is\ndisplayed below using a default method." )/*extracted from ldaputil.c*/
	ResDef( DBT_invalidUserIdOrNullLdapHandle_, 263, "Invalid user id or NULL LDAP handle" )/*extracted from ldaputil.c*/
	ResDef( DBT_noMatchForUserId_, 264, "no match for user id" )/*extracted from ldaputil.c*/
	ResDef( DBT_moreThanOneMatchForUserId_, 265, "more than one match for user id" )/*extracted from ldaputil.c*/
	ResDef( DBT_theEntireDirectory_, 266, "the entire directory" )/*extracted from ldaputil.c*/
	ResDef( DBT_twoArgumentsAreRequiredForTheInc_, 267, "Two arguments are required for the \"includeset\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_theAttributeValueRequestedWasNot_, 268, "The attribute value requested was not found in the entry." )/* extracted from error.c*/
	ResDef( DBT_missingArgumentForNLS_, 269, "Missing argument for \"NLS\" directive\n" )
	ResDef( DBT_aValueMustBeSpecifiedForNTUserId, 270, "A value must be specified for NT User Id.\n" )
	ResDef( DBT_theCombinationOfNTUserIdNTDomain_, 271, "The combination of NT User Id, NT Domain Id is not unique in the directory.\n" )
	ResDef( DBT_valuesMustBeSpecifiedForBothNTUser_ , 272, "Values must be specified for both NT User Id and NT Domain Id.\n" )
	ResDef( DBT_theNTUserIdValueMustNotExceed_, 273, "The NT User Id value must not exceed 20 characters in length.\n" )
	ResDef( DBT_enterNameForNewEntry_, 274, "Please provide a name for the new entry." )
	ResDef( DBT_enterLocationForNewEntry_, 275, "Please select a location for the new entry." )
	ResDef( DBT_titleNewEntry_, 276, "New Entry" )
	ResDef( DBT_noDirMgrIsDefined_, 277, "In order to use this feature, there must be a dirmgr defined in dsgw.conf")
	ResDef( DBT_threeOrFourArgumentsAreRequiredF_2, 278, "Three or four arguments are required for the \"vcard-property\" directive\n" )
	ResDef( DBT_vcardPropertySyntaxMustBeCisOrMl_, 279, "VCard property syntax must be \"cis\" or \"mls\"\n" )
	ResDef( DBT_Found0Entries_,            280, "Found no entries.\n%2$s" )
	ResDef( DBT_Found0EntriesWhere_,       281, "Found no entries where the %2$s %3$s '%4$s'.\n" )
	ResDef( DBT_SearchFound0Entries_,      282, "Searched and found no entries.\n%2$s" )
	ResDef( DBT_SearchFound0EntriesWhere_, 283, "Searched and found no entries where the %2$s %3$s %4$s'.\n" )
	ResDef( DBT_Found1Entry_,            284, "Found 1 entry.\n%2$s" )
	ResDef( DBT_Found1EntryWhere_,       285, "Found 1 entry where the %2$s %3$s '%4$s'.\n" )
	ResDef( DBT_SearchFound1Entry_,      286, "Searched and found 1 entry.\n%2$s" )
	ResDef( DBT_SearchFound1EntryWhere_, 287, "Searched and found 1 entry where the %2$s %3$s '%4$s'.\n" )
	ResDef( DBT_FoundEntries_,            288, "Found %1$li entries.\n%2$s" )
	ResDef( DBT_FoundEntriesWhere_,       289, "Found %1$li entries where the %2$s %3$s '%4$s'.\n" )
	ResDef( DBT_SearchFoundEntries_,      290, "Searched and found %1$li entries.\n%2$s" )
	ResDef( DBT_SearchFoundEntriesWhere_, 291, "Searched and found %1$li entries where the %2$s %3$s '%4$s'.\n" )
	ResDef( DBT_theLDAPFilterIs_, 292, "the LDAP filter is" )
	ResDef( DBT_theServerCouldNotLocateTheEntryY_, 293, "The server could not locate the entry you used when you authenticated.  It is possible that someone renamed the entry or that is was deleted.  Please try to authenticate again." )/*extracted from error.c*/
	ResDef( DBT_InvalidPasswordSyntax_, 294, "The new password syntax is invalid.\n" )
	ResDef( DBT_PasswordInHistory_, 295, "The new password occurs in the password history.\n" )
	ResDef( DBT_ExceedPasswordRetryContactSysAdmin_, 296, "You've exceeded the password retry limit.  Please contact your System Administrator.\n" )
	ResDef( DBT_ExceedPasswordRetryTryLater_, 297, "You've exceeded the password retry limit.  Please try again later.\n" )
	ResDef( DBT_PasswordExpired_, 298, "The password has expired.  Contact your System Administrator to reset the password.\n" )
	ResDef( DBT_Editing_, 299, "Editing" )/*extracted from domodify.c*/
	ResDef( DBT_Adding_, 300, "Adding" )/*extracted from domodify.c*/
	ResDef( DBT_Deleting_, 301, "Deleting" )/*extracted from domodify.c*/
	ResDef( DBT_Renaming_, 302, "Renaming" )/*extracted from domodify.c*/
	ResDef( DBT_noNameInTheList_, 303, "There are <B>no</B> names in the list." )/*extracted from dnedit.c*/
	ResDef( DBT_oneNameInTheList_, 304, "There is <B>1</B> name in the list." )/*extracted from dnedit.c*/
	ResDef( DBT_someNamesInTheList_, 305, "There are <B>%s</B> names in the list." )/*extracted from dnedit.c*/
	ResDef( DBT_RemoveFromList_, 306, "Remove<BR>from<BR>list?        Name" )/*extracted from dnedit.c -- should be JavaScript syntax*/
	ResDef( DBT_discardChanges_, 307, "Discard Changes?" ) /*extracted from dnedit.c */
	ResDef( DBT_discardChangesWindow_, 308, "width=300,height=130,resizable" ) /*extracted from dnedit.c */
	ResDef( DBT_continueWithoutSaving_, 309, "Continue without saving changes?<br>Unsaved changes will be lost." ) /*extracted from entrydisplay.c */
	ResDef( DBT_continueWithoutSavingWindow_, 310, "width=400,height=150,resizable" ) /*extracted from dnedit.c */
	ResDef( DBT_alertTitle_, 311, "Alert" ) /*extracted from htmlout.c */
	ResDef( DBT_confirmTitle_, 312, "Confirm" ) /*extracted from htmlout.c */
	ResDef( DBT_AuthenticationFailed_, 313, "Authentication Failed\n" )/*extracted from doauth.c*/
	ResDef( DBT_AuthenticationSuccessful_, 314, "Authentication Successful" )/*extracted from doauth.c*/
	ResDef( DBT_YouAreNowAuthenticated_, 315, "You are now authenticated to the directory as <B>%s</B>." )/*extracted from doauth.c*/
	ResDef( DBT_YourAuthenticationCredentialsWill_, 316, "Your authentication credentials will expire in %d minutes.\n" )/*extracted from doauth.c*/
	ResDef( DBT_AfterYourCredentialsExpire_, 317, "After your credentials expire, you will need to \nre-authenticate to the directory.\n" )/*extracted from doauth.c*/
	ResDef( DBT_ThePasswordForThisEntryWillExpire_, 318, "<P>The password for this entry will expire <B>%s</B>.\n" )/*extracted from doauth.c*/
	ResDef( DBT_AuthenticationFailedBecause_, 319, "Authentication failed because" )/*extracted from doauth.c*/
	ResDef( DBT_AuthEntryNotExist_, 320, "Authentication failed because the entry you attempted to authenticate as does\nnot exist in the directory.\nYou may only authenticate as an existing directory\nentry.\n")/*extracted from doauth.c*/
	ResDef( DBT_AuthEntryHasNoPassword_, 321, "Authentication failed because the entry you attempted to authenticate as does\nnot have a password.  Before you can authenticate\nas this entry, a password must be set by a\ndirectory administrator\n")/*extracted from doauth.c*/
	ResDef( DBT_thePasswordIsIncorrect_, 322, "Authentication failed because the password you supplied is incorrect.  Please\nclick the Retry button and try again.  If you have\nforgotten the password for this entry, a directory\nadministrator must reset the password for you.\n")/*extracted from doauth.c*/
	ResDef( DBT_AuthUnexpectedError_, 323, "Authentication failed because of an unexpected error: %s\n")/*extracted from doauth.c*/
	ResDef( DBT_Retry_, 324, "Retry" )/*extracted from doauth.c*/
	ResDef( DBT_ToContinue_, 325, "To continue, select a task from the list above.\n" )/*extracted from doauth.c*/
	ResDef( DBT_EditPassword_, 326, "Edit Password" )/*extracted from dsgwutil.c*/
	ResDef( DBT_PasswordExpiredFor_, 327, "<H3>Password Expired for %s</H3>\n" )/*extracted from dsgwutil.c*/
	ResDef( DBT_YourPasswordHasExpired_, 328, "Your Directory Server password has expired." )/*extracted from dsgwutil.c*/
	ResDef( DBT_YouMustChangeYourPasswd_, 329, "  You must change your password immediately.\n" )/*extracted from dsgwutil.c*/
	ResDef( DBT_youDidNotProvidePasswd_, 330, "you did not provide a password. Whenever you authenticate, you must provide a password so that the server can verify your identity." )/*extracted from doauth.c*/
	ResDef( DBT_authDBNotOpened_, 331, "the server was unable to generate authentication credentials.  The authentication database could not be opened." )/*extracted from doauth.c*/
	ResDef( DBT_DataCouldNotAppendToAuthDB_, 332, "the server was unable to generate authentication credentials. Data could not be appended to the authentication database.")/*extracted from doauth.c*/
	ResDef( DBT_continue_4, 333, "Continue" )/*extracted from doauth.c*/
	ResDef( DBT_closeWindow_5, 334, "Close Window" )/*extracted from doauth.c*/
	ResDef( DBT_Success_, 335, "Success" )/*extracted from unauth.c*/
	ResDef( DBT_YouAreNoLongerAuthenticated_, 336, "Your authentication credentials have been destroyed.  You are no longer authenticated to the \ndirectory.\n")/*extracted from unauth.c*/
	ResDef( DBT_GoBack_, 337, "Go Back")/*extracted from unauth.c*/

	ResDef( DBT_LDAP_SUCCESS, 338, "Success")
	ResDef( DBT_LDAP_OPERATIONS_ERROR, 339, "Operations error")
	ResDef( DBT_LDAP_PROTOCOL_ERROR, 340, "Protocol error")
	ResDef( DBT_LDAP_TIMELIMIT_EXCEEDED, 341, "Warning: a time limit was exceeded.  Not all matching entries are shown.")
	ResDef( DBT_LDAP_SIZELIMIT_EXCEEDED, 342, "Warning: a size limit was exceeded.  Not all matching entries are shown.")
	ResDef( DBT_LDAP_COMPARE_FALSE, 343, "Compare false")
	ResDef( DBT_LDAP_COMPARE_TRUE, 344, "Compare true")
	ResDef( DBT_LDAP_STRONG_AUTH_NOT_SUPPORTED, 345, "Strong authentication not supported")
	ResDef( DBT_LDAP_STRONG_AUTH_REQUIRED, 346, "Strong authentication required")
	ResDef( DBT_LDAP_PARTIAL_RESULTS, 347, "Warning: some directory servers could not be contacted.  Not all matching entries are shown.")
	ResDef( DBT_LDAP_REFERRAL, 348, "Referral received")
	ResDef( DBT_LDAP_ADMINLIMIT_EXCEEDED, 349, "Administrative limit exceeded")
	ResDef( DBT_LDAP_UNAVAILABLE_CRITICAL_EXTENSION, 350, "Unavailable critical extension")
	ResDef( DBT_LDAP_CONFIDENTIALITY_REQUIRED, 351, "Confidentiality required")
	ResDef( DBT_LDAP_SASL_BIND_IN_PROGRESS, 352, "SASL bind in progress")

	ResDef( DBT_LDAP_NO_SUCH_ATTRIBUTE, 353, "No such attribute")
	ResDef( DBT_LDAP_UNDEFINED_TYPE, 354, "Undefined attribute type")
	ResDef( DBT_LDAP_INAPPROPRIATE_MATCHING, 355, "Inappropriate matching")
	ResDef( DBT_LDAP_CONSTRAINT_VIOLATION, 356, "Constraint violation")
	ResDef( DBT_LDAP_TYPE_OR_VALUE_EXISTS, 357, "Type or value exists")
	ResDef( DBT_LDAP_INVALID_SYNTAX, 358, "Invalid syntax")

	ResDef( DBT_LDAP_NO_SUCH_OBJECT, 359, "No such object")
	ResDef( DBT_LDAP_ALIAS_PROBLEM, 360, "Alias problem")
	ResDef( DBT_LDAP_INVALID_DN_SYNTAX, 361, "Invalid DN syntax")
	ResDef( DBT_LDAP_IS_LEAF, 362, "Object is a leaf")
	ResDef( DBT_LDAP_ALIAS_DEREF_PROBLEM, 363, "Alias dereferencing problem")

	ResDef( DBT_LDAP_INAPPROPRIATE_AUTH, 364, "Inappropriate authentication")
	ResDef( DBT_LDAP_INVALID_CREDENTIALS, 365, "Invalid credentials")
	ResDef( DBT_LDAP_INSUFFICIENT_ACCESS, 366, "Insufficient access")
	ResDef( DBT_LDAP_BUSY, 367, "DSA is busy")
	ResDef( DBT_LDAP_UNAVAILABLE, 368, "DSA is unavailable")
	ResDef( DBT_LDAP_UNWILLING_TO_PERFORM, 369, "DSA is unwilling to perform")
	ResDef( DBT_LDAP_LOOP_DETECT, 370, "Loop detected")

	ResDef( DBT_LDAP_NAMING_VIOLATION, 371, "Naming violation")
	ResDef( DBT_LDAP_OBJECT_CLASS_VIOLATION, 372, "Object class violation")
	ResDef( DBT_LDAP_NOT_ALLOWED_ON_NONLEAF, 373, "Operation not allowed on nonleaf")
	ResDef( DBT_LDAP_NOT_ALLOWED_ON_RDN, 374, "Operation not allowed on RDN")
	ResDef( DBT_LDAP_ALREADY_EXISTS, 375, "Already exists")
	ResDef( DBT_LDAP_NO_OBJECT_CLASS_MODS, 376, "Cannot modify object class")
	ResDef( DBT_LDAP_RESULTS_TOO_LARGE, 377, "Results too large")
	ResDef( DBT_LDAP_AFFECTS_MULTIPLE_DSAS, 378, "Affects multiple servers")

	ResDef( DBT_LDAP_OTHER, 379, "Unknown error")
	ResDef( DBT_LDAP_SERVER_DOWN, 380, "Can't contact LDAP server")
	ResDef( DBT_LDAP_LOCAL_ERROR, 381, "Local error")
	ResDef( DBT_LDAP_ENCODING_ERROR, 382, "Encoding error")
	ResDef( DBT_LDAP_DECODING_ERROR, 383, "Decoding error")
	ResDef( DBT_LDAP_TIMEOUT, 384, "Timed out")
	ResDef( DBT_LDAP_AUTH_UNKNOWN, 385, "Unknown authentication method")
	ResDef( DBT_LDAP_FILTER_ERROR, 386, "Bad search filter")
	ResDef( DBT_LDAP_USER_CANCELLED, 387, "User cancelled operation")
	ResDef( DBT_LDAP_PARAM_ERROR, 388, "Bad parameter to an ldap routine")
	ResDef( DBT_LDAP_NO_MEMORY, 389, "Out of memory")
	ResDef( DBT_LDAP_CONNECT_ERROR, 390, "Can't connect to the LDAP server")
	ResDef( DBT_LDAP_NOT_SUPPORTED, 391, "Not supported by this version of the LDAP protocol")
	ResDef( DBT_LDAP_CONTROL_NOT_FOUND, 392, "Requested LDAP control not found")
	ResDef( DBT_LDAP_NO_RESULTS_RETURNED, 393, "No results returned")
	ResDef( DBT_LDAP_MORE_RESULTS_TO_RETURN, 394, "More results to return")
	ResDef( DBT_LDAP_CLIENT_LOOP, 395, "Client detected loop")
	ResDef( DBT_LDAP_REFERRAL_LIMIT_EXCEEDED, 396, "Referral hop limit exceeded")
	ResDef( DBT_missingArgumentForHtmlpathDi_, 399, "Missing argument for \"htmldir\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_errorS_, 400, "Error: %s (%i)" )/*extracted from error.c*/
	ResDef( DBT_doYouReallyWantTo_, 401, "Do you really want to %s?" )/*extracted from htmlparse.c*/
	ResDef( DBT_doYouReallyWantToWindow_, 402, "width=400,height=130,resizable" )/*extracted from htmlparse.c*/
	ResDef( DBT_missingArgumentForConfigpathDi_, 403, "Missing argument for \"configdir\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForNametransDi_,  404, "Missing argument for \"gwnametrans\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_MissingContext_,  405, "Missing context\n" )/*extracted from config.c*/
        ResDef( DBT_missingFilenameForBinddnfileDirecti_, 406, "Missing filename for \"binddnfile\" directive\n" ) /*extracted from config.c*/
        ResDef( DBT_missingArgumentForBinddnDirectiv_, 407, "Missing argument for \"binddn\" directive\n" )
        ResDef( DBT_missingArgumentForBindpwDirectiv_, 408, "Missing argument for \"bindpw\" directive\n" )
        ResDef( DBT_badFilenameForBinddnfileDirecti_, 409, "The binddn file must be specified with a full path and cannot exist under the dsgw directory\n" )
        ResDef (DBT_wrongPlaceForBinddnDirectiv_, 410, "The bind information should not be in the main configuration file. Please put it in a separate file outside of the dsgw directory\n")
	ResDef( DBT_NotWillingToExecute_,  411, "The directory server gateway is not available for the restricted installation. To use the gateway upgrade to the full version of the Netscape Directory Server.\n" )
	ResDef( DBT_missingArgumentForOrgChartURLDirectiv_, 412, "Missing argument for \"url-orgchart-base\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_missingArgumentForOrgChartSearchAttr_ , 413, "Missing argument for \"orgchart-attrib-farleft-rdn\" directive\n" )/*extracted from config.c*/
	ResDef( DBT_theCharsetIsNotSupported , 414, "The charset is not supported\n" )
	ResDef( DBT_invalidTemplateVarLen, 415, "The string length %d of template variable \"%s\" is too long\n" )
END_STR(dsgw)

