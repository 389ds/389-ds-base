/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#define LIBRARY_NAME "libaccess"

static char dbtlibaccessid[] = "$DBT: libaccess referenced v1 $";

#include "i18n.h"

BEGIN_STR(libaccess)
	ResDef( DBT_LibraryID_, -1, dbtlibaccessid )/* extracted from dbtlibaccess.h*/
	ResDef( DBT_basicNcsa_, 1, "basic-ncsa" )/*extracted from userauth.cpp*/
	ResDef( DBT_cannotOpenDatabaseS_, 2, "cannot open database %s" )/*extracted from userauth.cpp*/
	ResDef( DBT_basicNcsa_1, 3, "basic-ncsa" )/*extracted from userauth.cpp*/
	ResDef( DBT_userSPasswordDidNotMatchDatabase_, 4, "user %s password did not match database %s" )/*extracted from userauth.cpp*/
	ResDef( DBT_basicNcsa_2, 5, "basic-ncsa" )/*extracted from userauth.cpp*/
	ResDef( DBT_cannotOpenConnectionToLdapServer_, 6, "cannot open connection to LDAP server on %s:%d" )/*NOT USED - extracted from userauth.cpp*/
	ResDef( DBT_basicNcsa_3, 7, "basic-ncsa" )/*extracted from userauth.cpp*/
	ResDef( DBT_userSPasswordDidNotMatchLdapOnSD_, 8, "user %s password did not match LDAP on %s:%d" )/*NOT USED - extracted from userauth.cpp*/
	ResDef( DBT_aclState_, 9, "acl-state" )/*extracted from userauth.cpp*/
	ResDef( DBT_missingRealm_, 10, "missing realm" )/*extracted from userauth.cpp*/
	ResDef( DBT_unableToAllocateAclListHashN_, 11, "Unable to allocate ACL List Hash\n" )/*extracted from cache.cpp*/
	ResDef( DBT_aclevalbuildcontextUnableToPermM_, 12, "ACLEvalBuildContext unable to PERM_MALLOC cache structure\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclevalbuildcontextUnableToCreat_, 13, "ACLEvalBuildContext unable to create hash table\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclevalbuildcontextUnableToAlloc_, 14, "ACLEvalBuildContext unable to allocate ACE Entry\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclevalbuildcontextUnableToAlloc_1, 15, "ACLEvalBuildContext unable to allocate ACE entry\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclevalbuildcontextUnableToAlloc_2, 16, "ACLEvalBuildContext unable to allocate Boundary Entry\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclevalbuildcontextFailedN_, 17, "ACLEvalBuildContext failed.\n" )/*extracted from eval.cpp*/
	ResDef( DBT_aclEvaltestrightsAnInterimAbsolu_, 18, "ACL_EvalTestRights: an interim, absolute non-allow value was encountered. right=%s, value=%d\n" )/*NOT USED - extracted from eval.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAllocateHashT_, 19, "LASDnsBuild unable to allocate hash table header\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAddKeySN_, 20, "LASDnsBuild unable to add key %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAddKeySN_1, 21, "LASDnsBuild unable to add key %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAddKeySN_2, 22, "LASDnsBuild unable to add key %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAddKeySN_3, 23, "LASDnsBuild unable to add key %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsbuildUnableToAddKeySN_4, 24, "LASDnsBuild unable to add key %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasDnsBuildReceivedRequestForAtt_, 25, "LAS DNS build received request for attribute %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsevalIllegalComparatorDN_, 26, "LASDnsEval - illegal comparator %s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsevalUnableToAllocateContex_, 27, "LASDnsEval unable to allocate Context struct\n\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsevalUnableToGetSessionAddr_, 28, "LASDnsEval unable to get session address %d\n" )/*NOT USED - extracted from lasdns.cpp*/
	ResDef( DBT_lasdnsevalUnableToGetDnsErrorDN_, 29, "LASDnsEval unable to get DNS - error=%s\n" )/*extracted from lasdns.cpp*/
	ResDef( DBT_lasGroupEvalReceivedRequestForAt_, 30, "LAS Group Eval received request for attribute %s\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalIllegalComparatorDN_, 31, "LASGroupEval - illegal comparator %s\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalRanOutOfMemoryN_, 32, "LASGroupEval - ran out of memory\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalUnableToGetSessionAd_, 33, "LASGroupEval unable to get session address %d\n" )/*NOT USED - extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalUnableToGetSessionAd_1, 34, "LASGroupEval unable to get session address %d\n" )/*NOT USED - extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalCouldnTLocateGetterF_, 35, "LASGroupEval - couldn't locate getter for auth-user\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalAttributeGetterForAu_, 36, "LASGroupEval - Attribute getter for auth-user failed\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasgroupevalAttributeGetterDidnT_, 37, "LASGroupEval - Attribute getter didn't set auth-user\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_checkGroupMembershipOfUserSForGr_, 38, "Check group membership of user \"%s\" for group \"%s\"\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_ldapuSuccessForGroupSN_, 39, "LDAPU_SUCCESS for group \"%s\"\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_ldapuFailedForGroupSN_, 40, "LDAPU_FAILED for group \"%s\"\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasEvalFalseN_, 41, "LAS_EVAL_FALSE\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasEvalTrueN_, 42, "LAS_EVAL_TRUE\n" )/*extracted from lasgroup.cpp*/
	ResDef( DBT_lasiptreeallocNoMemoryN_, 43, "LASIpTreeAlloc - no memory\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_ipLasUnableToAllocateTreeNodeN_, 44, "IP LAS unable to allocate tree node\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_ipLasUnableToAllocateTreeNodeN_1, 45, "IP LAS unable to allocate tree node\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasIpBuildReceivedRequestForAttr_, 46, "LAS IP build received request for attribute %s\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasipevalIllegalComparatorDN_, 47, "LASIpEval - illegal comparator %s\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasipevalUnableToGetSessionAddre_, 48, "LASIpEval unable to get session address - error=%s\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasipevalUnableToAllocateContext_, 49, "LASIpEval unable to allocate Context struct\n\n" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasipevalReach32BitsWithoutConcl_, 50, "LASIpEval - reach 32 bits without conclusion value=%s" )/*extracted from lasip.cpp*/
	ResDef( DBT_lasProgramEvalReceivedRequestFor_, 51, "LAS Program Eval received request for attribute %s\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasprogramevalIllegalComparatorD_, 52, "LASProgramEval - illegal comparator %s\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasprogramUnableToGetSessionAddr_, 53, "LASProgram unable to get session address %d\n" )/*NOT USED - extracted from lasprogram.cpp*/
	ResDef( DBT_bin_, 54, "bin" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasprogramevalRequestNotOfTypeAd_, 55, "LASProgramEval: request not of type admin or bin, passing.\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasprogramevalCheckIfProgramSMat_, 56, "LASProgramEval: check if program %s matches pattern %s.\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasprogramevalInvalidWildcardExp_, 57, "LASProgramEval: Invalid wildcard expression %s.\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasEvalFalseN_1, 58, "LAS_EVAL_FALSE\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_lasEvalTrueN_1, 59, "LAS_EVAL_TRUE\n" )/*extracted from lasprogram.cpp*/
	ResDef( DBT_unexpectedAttributeInDayofweekSN_, 60, "Unexpected attribute in dayOfWeek - %s\n" )/*extracted from lastod.cpp*/
	ResDef( DBT_illegalComparatorForDayofweekDN_, 61, "Illegal comparator for dayOfWeek - %s\n" )/*extracted from lastod.cpp*/
	ResDef( DBT_unexpectedAttributeInTimeofdaySN_, 62, "Unexpected attribute in timeOfDay - %s\n" )/*extracted from lastod.cpp*/
	ResDef( DBT_lasUserEvalReceivedRequestForAtt_, 63, "LAS User Eval received request for attribute %s\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasuserevalIllegalComparatorDN_, 64, "LASUserEval - illegal comparator %s\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasuserevalRanOutOfMemoryN_, 65, "LASUserEval - ran out of memory\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasuserevalUnableToGetSessionAdd_, 66, "LASUserEval unable to get session address %d\n" )/*NOT USED - extracted from lasuser.cpp*/
	ResDef( DBT_lasuserevalUnableToGetSessionAdd_1, 67, "LASUserEval unable to get session address %d\n" )/*NOT USED - extracted from lasuser.cpp*/
	ResDef( DBT_lasgroupevalCouldnTLocateGetterF_1, 68, "LASGroupEval - couldn't locate getter for auth-user\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasgroupevalAttributeGetterForAu_1, 69, "LASGroupEval - Attribute getter for auth-user failed\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasgroupevalAttributeGetterDidnT_1, 70, "LASGroupEval - Attribute getter didn't set auth-user\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_checkIfUidUserIECheckSSN_, 71, "Check if uid == user (i.e. check \"%s\" == \"%s)\"\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_successForUserSN_, 72, "SUCCESS for user \"%s\"\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_failedForUserSN_, 73, "FAILED for user \"%s\"\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasEvalFalseN_2, 74, "LAS_EVAL_FALSE\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_lasEvalTrueN_2, 75, "LAS_EVAL_TRUE\n" )/*extracted from lasuser.cpp*/
	ResDef( DBT_Unused76, 76, "")
	ResDef( DBT_lasProgramUnableToGetRequest_, 77, "LASProgram unable to get request address - error=%s" ) /*extracted from lasprogram.cpp*/
	ResDef( DBT_lasProgramRejectingRequestForProgram_, 78, "LASProgram rejecting request for program %s from pattern %s" ) /*extracted from lasprogram.cpp*/
	ResDef( DBT_aclcacheflushCannotParseFile, 79, "ACL_CacheFlush: unable to parse file \"%s\"\n" )
	ResDef( DBT_aclcacheflushCannotConcatList, 80, "ACL_CacheFlush: unable to concatenate ACL list \"%s\"\n" )
	ResDef( DBT_aclcacheflushCannotOpenMagnus, 81, "ACL_CacheFlush: unable to open and process the magnus file \"%s\"\n" )
	ResDef( DBT_illegalComparatorForTimeOfDayDN_, 82, "Illegal comparator for timeOfDay - %s\n" )/*extracted from lastod.cpp*/
	ResDef( DBT_EvalBuildContextUnableToCreateHash, 83, "ACL_EvalBuildContext unable to create hash table\n")
	ResDef( DBT_EvalBuildContextUnableToAllocCache, 84, "ACL_EvalBuildContext unable to PERM_CALLOC cache structure\n")
	ResDef( DBT_EvalBuildContextUnableToAllocAceEntry, 85, "ACL_EvalBuildContext unable to allocate ACE entry\n")
	ResDef( DBT_EvalBuildContextUnableToAllocAuthPointerArray, 86, "ACL_EvalBuildContext unable to allocate auth pointer array\n")
	ResDef( DBT_EvalBuildContextUnableToAllocAuthPlist, 87, "ACL_EvalBuildContext unable to allocate auth plist\n")
	ResDef( DBT_EvalTestRightsInterimAbsoluteNonAllowValue, 88, "ACL_EvalTestRights: an interim, absolute non-allow value was encountered. right=%s, value=%s\n")
	ResDef( DBT_EvalTestRightsEvalBuildContextFailed, 89, "ACL_INTEvalTestRights: call to ACL_EvalBuildContext returned failure status\n")
	ResDef( DBT_ModuleRegisterModuleNameMissing, 90, "ACL_ModuleRegister: module name is missing\n")
	ResDef( DBT_ModuleRegisterFailed, 91, "ACL_ModuleRegister: call to module init function returned a failed status\n")
	ResDef( DBT_GetAttributeCouldntDetermineMethod, 92, "ACL_GetAttribute: couldn't determine method for %s\n")
	ResDef( DBT_GetAttributeCouldntLocateGetter, 93,  "ACL_GetAttribute: couldn't locate getter for %s")
	ResDef( DBT_GetAttributeDidntGetAttr, 94, "ACL_GetAttribute: attr getter failed to get %s")
	ResDef( DBT_GetAttributeDidntSetAttr, 95, "ACL_GetAttribute: attr getter failed to get %s")
	ResDef( DBT_GetAttributeAllGettersDeclined, 96, "ACL_GetAttribute: All attribute getters declined for attr %s")
	ResDef( DBT_DbtypeNoteDefinedYet, 97, "ACL_DatabaseRegister: dbtype for database \"%s\" is not defined yet!")
	ResDef( DBT_DatabaseRegisterDatabaseNameMissing, 98, "ACL_DatabaseRegister: database name is missing")
	ResDef( DBT_ReadDbMapFileErrorReadingFile, 99,  "Error reading the DB Map File: %s. Reason: %s")
	ResDef( DBT_ReadDbMapFileMissingUrl, 100, "URL is missing for database %s")
	ResDef( DBT_ReadDbMapFileInvalidPropertyPair, 101,  "Invalid property value pair for database %s")
	ResDef( DBT_ReadDbMapFileDefaultDatabaseNotLdap, 102,  "\"default\" database must be an LDAP database")
	ResDef( DBT_ReadDbMapFileMultipleDefaultDatabases, 103, "Multiple \"default\" databases are being registered")
	ResDef( DBT_ReadDbMapFileMissingDefaultDatabase, 104, "\"default\" LDAP database must be registered")
	ResDef( DBT_lasGroupEvalUnableToGetDatabaseName, 105, "LASGroupEval unable to get database name - error= %s")
	ResDef( DBT_lasProgramReceivedInvalidProgramExpression, 106, "received invalid program expression %s")
	ResDef( DBT_ldapaclDatabaseUrlIsMissing, 107, "parse_ldap_url: database url is missing")
	ResDef( DBT_ldapaclDatabaseNameIsMissing, 108, "parse_ldap_url: database name is missing")
 	ResDef( DBT_ldapaclErrorParsingLdapUrl, 109, "parse_ldap_url: error in parsing ldap url. Reason: %s")
	ResDef( DBT_ldapaclUnableToGetDatabaseName, 110,  "ldap password check: unable to get database name - error=%s")
	ResDef( DBT_ldapaclUnableToGetParsedDatabaseName, 111, "ldap password check: unable to get parsed database %s")
	ResDef( DBT_ldapaclCoudlntInitializeConnectionToLdap, 112, "ldap password check: couldn't initialize connection to LDAP. Reason: %s")
	ResDef( DBT_ldapaclPassworkCheckLdapError, 113, "ldap password check: LDAP error: \"%s\"")
	ResDef( DBT_GetUserIsMemberLdapUnabelToGetDatabaseName, 114, "get_user_ismember_ldap unable to get database name - error=%s")
	ResDef( DBT_GetUserIsMemberLdapUnableToGetParsedDatabaseName, 115, "get_user_ismember_ldap unable to get parsed database %s")
	ResDef( DBT_GetUserIsMemberLdapCouldntInitializeConnectionToLdap, 116, "ldap password check: couldn't initialize connection to LDAP. Reason: %s")
	ResDef( DBT_GetUserIsMemberLdapGroupDoesntExist, 117, "get_user_ismember_ldap: group %s does not exist")
	ResDef( DBT_GetUserIsMemberLdapError, 118, "get_user_ismember_ldap: LDAP error: \"%s\"")
	ResDef( DBT_LdapDatabaseHandleNotARegisteredDatabase, 119, "ACL_LDAPDatabaseHandle: %s is not a registered database")
	ResDef( DBT_LdapDatabaseHandleNotAnLdapDatabase, 120, "ACL_LDAPDatabaseHandle: %s is not an LDAP database")
	ResDef( DBT_LdapDatabaseHandleOutOfMemory, 121, "ACL_LDAPDatabaseHandle: out of memory")
	ResDef( DBT_LdapDatabaseHandleCouldntInitializeConnectionToLdap, 122, "ACL_LDAPDatabaseHandle: couldn't initialize connection to LDAP. Reason: %s")
	ResDef(  DBT_LdapDatabaseHandleCouldntBindToLdapServer, 123,  "ACL_LDAPDatabaseHandle: couldn't bind to LDAP server. Reason: %s")
	ResDef( DBT_AclerrfmtAclerrnomem, 124, "insufficient dynamic memory")
	ResDef( DBT_AclerrfmtAclerropen, 125, "error opening file, %s: %s")
	ResDef( DBT_AclerrfmtAclerrdupsym1, 126, "duplicate definition of %s")
	ResDef( DBT_AclerrfmtAclerrdupsym3, 127,  "file %s, line %s: duplicate definition of %s")
	ResDef( DBT_AclerrfmtAclerrsyntax, 128, "file %s, line %s: syntax error")
	ResDef( DBT_AclerrfmtAclerrundef, 129, "file %s, line %s: %s is undefined")
	ResDef( DBT_AclerrfmtAclaclundef, 130, "in acl %s, %s %s is undefined")
	ResDef( DBT_AclerrfmtAclerradb, 131, "database %s: error accessing %s")
	ResDef( DBT_AclerrfmtAclerrparse1, 132, "%s")
	ResDef( DBT_AclerrfmtAclerrparse2, 133, "file %s, line %s: invalid syntax")
	ResDef( DBT_AclerrfmtAclerrparse3, 134, "file %s, line %s: syntax error at \"%s\"")
	ResDef( DBT_AclerrfmtAclerrnorlm, 135, "realm %s is not defined")
	ResDef( DBT_AclerrfmtUnknownerr, 136, "error code = %d")
	ResDef( DBT_AclerrfmtAclerrinternal, 137, "internal ACL error")
	ResDef( DBT_AclerrfmtAclerrinval, 138, "invalid argument")
	ResDef( DBT_DbtypeNotDefinedYet, 139, "ACL_DatabaseRegister: dbtype for database \"%s\" is not defined yet!")
	ResDef( DBT_ReadDbMapFileCouldntDetermineDbtype, 140, "couldn't determine dbtype from: %s")
	ResDef( DBT_ReadDbMapFileRegisterDatabaseFailed, 141,  "Failed to register database %s")
	ResDef( DBT_AclerrfmtAclerrfail, 142, "ACL call returned failed status")
	ResDef( DBT_AclerrfmtAclerrio, 143, "file %s: ACL IO error - %s")
	ResDef( DBT_AclUserExistsOutOfMemory, 144, "acl_user_exists: out of memory")
	ResDef( DBT_AclUserExistsNot, 145, "acl_user_exists: user doesn't exist anymore")
	ResDef( DBT_AclUserPlistError, 146, "acl_user_exists: plist error")
END_STR(libaccess)
