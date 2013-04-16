/** BEGIN COPYRIGHT BLOCK
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
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 *
 *  libglobs.c -- SLAPD library global variables
 */
/* for windows only
   we define slapd_ldap_debug here, so we don't want to declare
   it in any header file which might conflict with our definition
*/
#define DONT_DECLARE_SLAPD_LDAP_DEBUG /* see ldaplog.h */

#include "ldap.h"
#include <sslproto.h>

#undef OFF
#undef LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#if defined( _WIN32 )
#define R_OK 04
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
#else
#include <sys/time.h>
#include <sys/param.h> /* MAXPATHLEN */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pwd.h> /* pwdnam */
#endif
#ifdef USE_SYSCONF
#include <unistd.h>
#endif /* USE_SYSCONF */
#include "slap.h"
#include "plhash.h"

#define REMOVE_CHANGELOG_CMD "remove"
#define DEFAULT_SASL_MAXBUFSIZE "65536"
#define SLAPD_DEFAULT_SASL_MAXBUFSIZE 65536

/* On UNIX, there's only one copy of slapd_ldap_debug */
/* On NT, each module keeps its own module_ldap_debug, which */
/* points to the process' slapd_ldap_debug */
#ifdef _WIN32
int		*module_ldap_debug;
int __declspec(dllexport)    slapd_ldap_debug = LDAP_DEBUG_ANY;
#else
int     slapd_ldap_debug = LDAP_DEBUG_ANY;
#endif

char		*ldap_srvtab = "";

/* Note that the 'attrname' arguments are used only for log messages */
typedef int (*ConfigSetFunc)(const char *attrname, char *value,
		char *errorbuf, int apply);
typedef int (*LogSetFunc)(const char *attrname, char *value, int whichlog,
		char *errorbuf, int apply);

typedef enum {
	CONFIG_INT, /* maps to int */
	CONFIG_LONG, /* maps to long */
	CONFIG_STRING, /* maps to char* */
	CONFIG_CHARRAY, /* maps to char** */
	CONFIG_ON_OFF, /* maps 0/1 to "off"/"on" */
	CONFIG_STRING_OR_OFF, /* use "off" instead of null or an empty string */
	CONFIG_STRING_OR_UNKNOWN, /* use "unknown" instead of an empty string */
	CONFIG_CONSTANT_INT, /* for #define values, e.g. */
	CONFIG_CONSTANT_STRING, /* for #define values, e.g. */
	CONFIG_SPECIAL_REFERRALLIST, /* this is a berval list */
	CONFIG_SPECIAL_SSLCLIENTAUTH, /* maps strings to an enumeration */
	CONFIG_SPECIAL_ERRORLOGLEVEL, /* requires & with LDAP_DEBUG_ANY */
	CONFIG_STRING_OR_EMPTY, /* use an empty string */
	CONFIG_SPECIAL_ANON_ACCESS_SWITCH, /* maps strings to an enumeration */
	CONFIG_SPECIAL_VALIDATE_CERT_SWITCH /* maps strings to an enumeration */
} ConfigVarType;

static int config_set_onoff( const char *attrname, char *value,
		int *configvalue, char *errorbuf, int apply );
static int config_set_schemareplace ( const char *attrname, char *value,
		char *errorbuf, int apply );

static int
isInt(ConfigVarType type)
{
    return type == CONFIG_INT || type == CONFIG_ON_OFF || type == CONFIG_SPECIAL_SSLCLIENTAUTH || type == CONFIG_SPECIAL_ERRORLOGLEVEL;
}

/* the caller will typically have to cast the result based on the ConfigVarType */
typedef void *(*ConfigGetFunc)(void);

/* static Ref_Array global_referrals; */
static slapdFrontendConfig_t global_slapdFrontendConfig;

static struct config_get_and_set {
	const char *attr_name; /* the name of the attribute */
	ConfigSetFunc setfunc; /* the function to call to set the value */
	LogSetFunc logsetfunc; /* log functions are special */
	int whichlog; /* ACCESS, ERROR, AUDIT, etc. */
	void** config_var_addr; /* address of member of slapdFrontendConfig struct */
	ConfigVarType config_var_type; /* cast to this type when getting */
	ConfigGetFunc getfunc; /* for special handling */
} ConfigList[] = {
	{CONFIG_AUDITLOG_MODE_ATTRIBUTE, NULL,
		log_set_mode, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_mode, CONFIG_STRING, NULL},
	{CONFIG_AUDITLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
		log_set_rotationsync_enabled, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_rotationsync_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
		log_set_rotationsynchour, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_rotationsynchour, CONFIG_INT, NULL},
	{CONFIG_AUDITLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
		log_set_rotationsyncmin, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_rotationsyncmin, CONFIG_INT, NULL},
	{CONFIG_AUDITLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
		log_set_rotationtime, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_rotationtime, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_MODE_ATTRIBUTE, NULL,
		log_set_mode, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_mode, CONFIG_STRING, NULL},
	{CONFIG_ACCESSLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
		log_set_numlogsperdir, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_maxnumlogs, CONFIG_INT, NULL},
	{CONFIG_LOGLEVEL_ATTRIBUTE, config_set_errorlog_level,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.errorloglevel,
		CONFIG_SPECIAL_ERRORLOGLEVEL, NULL},
	{CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
		log_set_logging, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_logging_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_ERRORLOG_MODE_ATTRIBUTE, NULL,
		log_set_mode, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_mode, CONFIG_STRING, NULL},
	{CONFIG_ERRORLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
		log_set_expirationtime, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_exptime, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
		log_set_logging, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_logging_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_PORT_ATTRIBUTE, config_set_port,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.port, CONFIG_INT, NULL},
	{CONFIG_WORKINGDIR_ATTRIBUTE, config_set_workingdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.workingdir, CONFIG_STRING_OR_EMPTY, NULL},
	{CONFIG_MAXTHREADSPERCONN_ATTRIBUTE, config_set_maxthreadsperconn,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.maxthreadsperconn, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
		log_set_expirationtime, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_exptime, CONFIG_INT, NULL},
#ifndef _WIN32
	{CONFIG_LOCALUSER_ATTRIBUTE, config_set_localuser,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.localuser, CONFIG_STRING, NULL},
#endif
	{CONFIG_ERRORLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
		log_set_rotationsync_enabled, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_rotationsync_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_ERRORLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
		log_set_rotationsynchour, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_rotationsynchour, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
		log_set_rotationsyncmin, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_rotationsyncmin, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
		log_set_rotationtime, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_rotationtime, CONFIG_INT, NULL},
	{CONFIG_PW_INHISTORY_ATTRIBUTE, config_set_pw_inhistory,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_inhistory, CONFIG_INT, NULL},
	{CONFIG_PW_STORAGESCHEME_ATTRIBUTE, config_set_pw_storagescheme,
		NULL, 0, NULL, CONFIG_STRING, (ConfigGetFunc)config_get_pw_storagescheme},
	{CONFIG_PW_UNLOCK_ATTRIBUTE, config_set_pw_unlock,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_unlock, CONFIG_ON_OFF, NULL},
	{CONFIG_PW_GRACELIMIT_ATTRIBUTE, config_set_pw_gracelimit,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_gracelimit, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
		log_set_rotationsync_enabled, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_rotationsync_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_ACCESSLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
		log_set_rotationsynchour, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_rotationsynchour, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
		log_set_rotationsyncmin, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_rotationsyncmin, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
		log_set_rotationtime, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_rotationtime, CONFIG_INT, NULL},
	{CONFIG_PW_MUSTCHANGE_ATTRIBUTE, config_set_pw_must_change,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_must_change, CONFIG_ON_OFF, NULL},
	{CONFIG_PWPOLICY_LOCAL_ATTRIBUTE, config_set_pwpolicy_local,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pwpolicy_local, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
		log_set_maxdiskspace, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_maxdiskspace, CONFIG_INT, NULL},
	{CONFIG_SIZELIMIT_ATTRIBUTE, config_set_sizelimit,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.sizelimit, CONFIG_INT, NULL},
	{CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
		log_set_logsize, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_maxlogsize, CONFIG_INT, NULL},
	{CONFIG_PW_WARNING_ATTRIBUTE, config_set_pw_warning,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_warning, CONFIG_LONG, NULL},
	{CONFIG_READONLY_ATTRIBUTE, config_set_readonly,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.readonly, CONFIG_ON_OFF, NULL},
	{CONFIG_THREADNUMBER_ATTRIBUTE, config_set_threadnumber,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.threadnumber, CONFIG_INT, NULL},
	{CONFIG_PW_LOCKOUT_ATTRIBUTE, config_set_pw_lockout,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_lockout, CONFIG_ON_OFF, NULL},
	{CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, config_set_enquote_sup_oc,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.enquote_sup_oc, CONFIG_ON_OFF, NULL},
	{CONFIG_LOCALHOST_ATTRIBUTE, config_set_localhost,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.localhost, CONFIG_STRING, NULL},
	{CONFIG_IOBLOCKTIMEOUT_ATTRIBUTE, config_set_ioblocktimeout,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.ioblocktimeout, CONFIG_INT, NULL},
	{CONFIG_MAX_FILTER_NEST_LEVEL_ATTRIBUTE, config_set_max_filter_nest_level,
		NULL, 0, (void**)&global_slapdFrontendConfig.max_filter_nest_level,
		CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
		log_set_maxdiskspace, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_maxdiskspace, CONFIG_INT, NULL},
	{CONFIG_PW_MINLENGTH_ATTRIBUTE, config_set_pw_minlength,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_minlength, CONFIG_INT, NULL},
	{CONFIG_PW_MINDIGITS_ATTRIBUTE, config_set_pw_mindigits,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_mindigits, CONFIG_INT, NULL},
	{CONFIG_PW_MINALPHAS_ATTRIBUTE, config_set_pw_minalphas,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_minalphas, CONFIG_INT, NULL},
	{CONFIG_PW_MINUPPERS_ATTRIBUTE, config_set_pw_minuppers,
		NULL, 0,
                (void**)&global_slapdFrontendConfig.pw_policy.pw_minuppers, CONFIG_INT, NULL},
	{CONFIG_PW_MINLOWERS_ATTRIBUTE, config_set_pw_minlowers,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_minlowers, CONFIG_INT, NULL},
        {CONFIG_PW_MINSPECIALS_ATTRIBUTE, config_set_pw_minspecials,
                NULL, 0,
                (void**)&global_slapdFrontendConfig.pw_policy.pw_minspecials, CONFIG_INT, NULL},
	{CONFIG_PW_MIN8BIT_ATTRIBUTE, config_set_pw_min8bit,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_min8bit, CONFIG_INT, NULL},
	{CONFIG_PW_MAXREPEATS_ATTRIBUTE, config_set_pw_maxrepeats,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_maxrepeats, CONFIG_INT, NULL},
        {CONFIG_PW_MINCATEGORIES_ATTRIBUTE, config_set_pw_mincategories,
                NULL, 0,
                (void**)&global_slapdFrontendConfig.pw_policy.pw_mincategories, CONFIG_INT, NULL},
	{CONFIG_PW_MINTOKENLENGTH_ATTRIBUTE, config_set_pw_mintokenlength,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_mintokenlength, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_ATTRIBUTE, config_set_errorlog,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.errorlog, CONFIG_STRING_OR_EMPTY, NULL},
	{CONFIG_AUDITLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
		log_set_expirationtime, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_exptime, CONFIG_INT, NULL},
	{CONFIG_SCHEMACHECK_ATTRIBUTE, config_set_schemacheck,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.schemacheck, CONFIG_ON_OFF, NULL},
	{CONFIG_SYNTAXCHECK_ATTRIBUTE, config_set_syntaxcheck,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.syntaxcheck, CONFIG_ON_OFF, NULL},
	{CONFIG_SYNTAXLOGGING_ATTRIBUTE, config_set_syntaxlogging,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.syntaxlogging, CONFIG_ON_OFF, NULL},
	{CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE, config_set_dn_validate_strict,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.dn_validate_strict, CONFIG_ON_OFF, NULL},
	{CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, config_set_ds4_compatible_schema,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.ds4_compatible_schema,
		CONFIG_ON_OFF, NULL},
	{CONFIG_SCHEMA_IGNORE_TRAILING_SPACES,
		config_set_schema_ignore_trailing_spaces, NULL, 0,
		(void**)&global_slapdFrontendConfig.schema_ignore_trailing_spaces,
		CONFIG_ON_OFF, NULL},
	{CONFIG_SCHEMAREPLACE_ATTRIBUTE, config_set_schemareplace, NULL, 0,
		(void**)&global_slapdFrontendConfig.schemareplace,
		CONFIG_STRING_OR_OFF, NULL},
	{CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
		log_set_maxdiskspace, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_maxdiskspace, CONFIG_INT, NULL},
	{CONFIG_REFERRAL_ATTRIBUTE, (ConfigSetFunc)config_set_defaultreferral,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.defaultreferral,
		CONFIG_SPECIAL_REFERRALLIST, NULL},
	{CONFIG_PW_MAXFAILURE_ATTRIBUTE, config_set_pw_maxfailure,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_maxfailure, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_ATTRIBUTE, config_set_accesslog,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.accesslog, CONFIG_STRING_OR_EMPTY, NULL},
	{CONFIG_LASTMOD_ATTRIBUTE, config_set_lastmod,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.lastmod, CONFIG_ON_OFF, NULL},
	{CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE, config_set_rootpwstoragescheme,
		NULL, 0, NULL, CONFIG_STRING, (ConfigGetFunc)config_get_rootpwstoragescheme},
	{CONFIG_PW_HISTORY_ATTRIBUTE, config_set_pw_history,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_history, CONFIG_ON_OFF, NULL},
	{CONFIG_SECURITY_ATTRIBUTE, config_set_security,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.security, CONFIG_ON_OFF, NULL},
	{CONFIG_PW_MAXAGE_ATTRIBUTE, config_set_pw_maxage,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_maxage, CONFIG_LONG, NULL},
	{CONFIG_AUDITLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_rotationtimeunit, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_rotationunit,
		CONFIG_STRING_OR_UNKNOWN, NULL},
	{CONFIG_PW_RESETFAILURECOUNT_ATTRIBUTE, config_set_pw_resetfailurecount,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_resetfailurecount, CONFIG_LONG, NULL},
	{CONFIG_PW_ISGLOBAL_ATTRIBUTE, config_set_pw_is_global_policy,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_is_global_policy, CONFIG_ON_OFF, NULL},
	{CONFIG_PW_IS_LEGACY, config_set_pw_is_legacy_policy,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_is_legacy, CONFIG_ON_OFF, NULL},
	{CONFIG_PW_TRACK_LAST_UPDATE_TIME, config_set_pw_track_last_update_time,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_track_update_time, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
		log_set_numlogsperdir, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_maxnumlogs, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_expirationtimeunit, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_exptimeunit,
		CONFIG_STRING_OR_UNKNOWN, NULL},
	/* errorlog list is read only, so no set func and no config var addr */
	{CONFIG_ERRORLOG_LIST_ATTRIBUTE, NULL, NULL, 0, NULL,
		CONFIG_CHARRAY, (ConfigGetFunc)config_get_errorlog_list},
	{CONFIG_GROUPEVALNESTLEVEL_ATTRIBUTE, config_set_groupevalnestlevel,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.groupevalnestlevel, CONFIG_INT, NULL},
	{CONFIG_ACCESSLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_expirationtimeunit, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_exptimeunit,
		CONFIG_STRING_OR_UNKNOWN, NULL},
	{CONFIG_ROOTPW_ATTRIBUTE, config_set_rootpw,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.rootpw, CONFIG_STRING, NULL},
	{CONFIG_PW_CHANGE_ATTRIBUTE, config_set_pw_change,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_change, CONFIG_ON_OFF, NULL},
	{CONFIG_ACCESSLOGLEVEL_ATTRIBUTE, config_set_accesslog_level,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.accessloglevel, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_rotationtimeunit, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_rotationunit,
		CONFIG_STRING_OR_UNKNOWN, NULL},
	{CONFIG_SECUREPORT_ATTRIBUTE, config_set_secureport,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.secureport, CONFIG_INT, NULL},
	{CONFIG_BASEDN_ATTRIBUTE, config_set_basedn,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.certmap_basedn, CONFIG_STRING, NULL},
	{CONFIG_TIMELIMIT_ATTRIBUTE, config_set_timelimit,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.timelimit, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
		log_set_logsize, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_maxlogsize, CONFIG_INT, NULL},
	{CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE, config_set_reservedescriptors,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.reservedescriptors, CONFIG_INT, NULL},
	/* access log list is read only, no set func, no config var addr */
	{CONFIG_ACCESSLOG_LIST_ATTRIBUTE, NULL, NULL, 0,
		NULL, CONFIG_CHARRAY, (ConfigGetFunc)config_get_accesslog_list},
	{CONFIG_SVRTAB_ATTRIBUTE, config_set_srvtab,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.srvtab, CONFIG_STRING, NULL},
	{CONFIG_PW_EXP_ATTRIBUTE, config_set_pw_exp,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_exp, CONFIG_ON_OFF, NULL},
	{CONFIG_ACCESSCONTROL_ATTRIBUTE, config_set_accesscontrol,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.accesscontrol, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_LIST_ATTRIBUTE, NULL, NULL, 0,
		NULL, CONFIG_CHARRAY, (ConfigGetFunc)config_get_auditlog_list},
	{CONFIG_ACCESSLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_rotationtimeunit, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_rotationunit, CONFIG_STRING, NULL},
	{CONFIG_PW_LOCKDURATION_ATTRIBUTE, config_set_pw_lockduration,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_lockduration, CONFIG_LONG, NULL},
	{CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
		log_set_logsize, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_maxlogsize, CONFIG_INT, NULL},
	{CONFIG_IDLETIMEOUT_ATTRIBUTE, config_set_idletimeout,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.idletimeout, CONFIG_INT, NULL},
	{CONFIG_NAGLE_ATTRIBUTE, config_set_nagle,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.nagle, CONFIG_ON_OFF, NULL},
	{CONFIG_ERRORLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
		log_set_mindiskspace, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_minfreespace, CONFIG_INT, NULL},
	{CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
		log_set_logging, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_logging_enabled, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_LOGGING_HIDE_UNHASHED_PW, config_set_auditlog_unhashed_pw,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.auditlog_logging_hide_unhashed_pw, CONFIG_ON_OFF, NULL},
	{CONFIG_ACCESSLOG_BUFFERING_ATTRIBUTE, config_set_accesslogbuffering,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.accesslogbuffering, CONFIG_ON_OFF, NULL},
	{CONFIG_CSNLOGGING_ATTRIBUTE, config_set_csnlogging,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.csnlogging, CONFIG_ON_OFF, NULL},
	{CONFIG_AUDITLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
		log_set_expirationtimeunit, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_exptimeunit,
		CONFIG_STRING_OR_UNKNOWN, NULL},
	{CONFIG_PW_SYNTAX_ATTRIBUTE, config_set_pw_syntax,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_syntax, CONFIG_ON_OFF, NULL},
	{CONFIG_LISTENHOST_ATTRIBUTE, config_set_listenhost,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.listenhost, CONFIG_STRING, NULL},
	{CONFIG_LDAPI_FILENAME_ATTRIBUTE, config_set_ldapi_filename,
                NULL, 0,
                (void**)&global_slapdFrontendConfig.ldapi_filename, CONFIG_STRING, NULL},
        {CONFIG_LDAPI_SWITCH_ATTRIBUTE, config_set_ldapi_switch,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_switch, CONFIG_ON_OFF, NULL},
        {CONFIG_LDAPI_BIND_SWITCH_ATTRIBUTE, config_set_ldapi_bind_switch,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_bind_switch, CONFIG_ON_OFF, NULL},
        {CONFIG_LDAPI_ROOT_DN_ATTRIBUTE, config_set_ldapi_root_dn,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_root_dn, CONFIG_STRING, NULL},
        {CONFIG_LDAPI_MAP_ENTRIES_ATTRIBUTE, config_set_ldapi_map_entries,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_map_entries, CONFIG_ON_OFF, NULL},
        {CONFIG_LDAPI_UIDNUMBER_TYPE_ATTRIBUTE, config_set_ldapi_uidnumber_type,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_uidnumber_type, CONFIG_STRING, NULL},
        {CONFIG_LDAPI_GIDNUMBER_TYPE_ATTRIBUTE, config_set_ldapi_gidnumber_type,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_gidnumber_type, CONFIG_STRING, NULL},
        {CONFIG_LDAPI_SEARCH_BASE_DN_ATTRIBUTE, config_set_ldapi_search_base_dn,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_search_base_dn, CONFIG_STRING, NULL},
#if defined(ENABLE_AUTO_DN_SUFFIX)
        {CONFIG_LDAPI_AUTO_DN_SUFFIX_ATTRIBUTE, config_set_ldapi_auto_dn_suffix,
                NULL, 0,
		(void**)&global_slapdFrontendConfig.ldapi_auto_dn_suffix, CONFIG_STRING, NULL},
#endif
	{CONFIG_ANON_LIMITS_DN_ATTRIBUTE, config_set_anon_limits_dn,
                NULL, 0,
                (void**)&global_slapdFrontendConfig.anon_limits_dn, CONFIG_STRING, NULL},
	{CONFIG_SLAPI_COUNTER_ATTRIBUTE, config_set_slapi_counters,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.slapi_counters, CONFIG_ON_OFF, 
		(ConfigGetFunc)config_get_slapi_counters},
	{CONFIG_ACCESSLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
		log_set_mindiskspace, SLAPD_ACCESS_LOG,
		(void**)&global_slapdFrontendConfig.accesslog_minfreespace, CONFIG_INT, NULL},
	{CONFIG_ERRORLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
		log_set_numlogsperdir, SLAPD_ERROR_LOG,
		(void**)&global_slapdFrontendConfig.errorlog_maxnumlogs, CONFIG_INT, NULL},
	{CONFIG_SECURELISTENHOST_ATTRIBUTE, config_set_securelistenhost,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.securelistenhost, CONFIG_STRING, NULL},
	{CONFIG_AUDITLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
		log_set_mindiskspace, SLAPD_AUDIT_LOG,
		(void**)&global_slapdFrontendConfig.auditlog_minfreespace, CONFIG_INT, NULL},
	{CONFIG_ROOTDN_ATTRIBUTE, config_set_rootdn,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.rootdn, CONFIG_STRING, NULL},
	{CONFIG_PW_MINAGE_ATTRIBUTE, config_set_pw_minage,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pw_policy.pw_minage, CONFIG_LONG, NULL},
	{CONFIG_AUDITFILE_ATTRIBUTE, config_set_auditlog,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.auditlog, CONFIG_STRING_OR_EMPTY, NULL},
	{CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, config_set_return_exact_case,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.return_exact_case, CONFIG_ON_OFF, NULL},
	{CONFIG_RESULT_TWEAK_ATTRIBUTE, config_set_result_tweak,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.result_tweak, CONFIG_ON_OFF, NULL},
	{CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE, config_set_plugin_tracking,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.plugin_track, CONFIG_ON_OFF, NULL},
	{CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE, config_set_attrname_exceptions,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.attrname_exceptions, CONFIG_ON_OFF, NULL},
	{CONFIG_MAXBERSIZE_ATTRIBUTE, config_set_maxbersize,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.maxbersize, CONFIG_INT, NULL},
	{CONFIG_MAXSASLIOSIZE_ATTRIBUTE, config_set_maxsasliosize,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.maxsasliosize, CONFIG_INT, NULL},
	{CONFIG_VERSIONSTRING_ATTRIBUTE, config_set_versionstring,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.versionstring, CONFIG_STRING, NULL},
	{CONFIG_REFERRAL_MODE_ATTRIBUTE, config_set_referral_mode,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.refer_url, CONFIG_STRING, NULL},
#if !defined(_WIN32) && !defined(AIX)
	{CONFIG_MAXDESCRIPTORS_ATTRIBUTE, config_set_maxdescriptors,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.maxdescriptors, CONFIG_INT, NULL},
#endif
	{CONFIG_CONNTABLESIZE_ATTRIBUTE, config_set_conntablesize,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.conntablesize, CONFIG_INT, NULL},
	{CONFIG_SSLCLIENTAUTH_ATTRIBUTE, config_set_SSLclientAuth,
		NULL, 0,
		(void **)&global_slapdFrontendConfig.SSLclientAuth, CONFIG_SPECIAL_SSLCLIENTAUTH, NULL},
	{CONFIG_SSL_CHECK_HOSTNAME_ATTRIBUTE, config_set_ssl_check_hostname,
		NULL, 0, NULL, CONFIG_ON_OFF, (ConfigGetFunc)config_get_ssl_check_hostname},
	{CONFIG_CONFIG_ATTRIBUTE, 0, NULL, 0, (void**)SLAPD_CONFIG_DN,
		CONFIG_CONSTANT_STRING, NULL},
	{CONFIG_HASH_FILTERS_ATTRIBUTE, config_set_hash_filters,
		NULL, 0, NULL, CONFIG_ON_OFF, (ConfigGetFunc)config_get_hash_filters},
	/* instance dir; used by admin tasks */
	{CONFIG_INSTDIR_ATTRIBUTE, config_set_instancedir,
		NULL, 0, 
		(void**)&global_slapdFrontendConfig.instancedir, CONFIG_STRING, NULL},
	/* parameterizing schema dir */
	{CONFIG_SCHEMADIR_ATTRIBUTE, config_set_schemadir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.schemadir, CONFIG_STRING, NULL},
	/* parameterizing lock dir */
	{CONFIG_LOCKDIR_ATTRIBUTE, config_set_lockdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.lockdir, CONFIG_STRING, (ConfigGetFunc)config_get_lockdir},
	/* parameterizing tmp dir */
	{CONFIG_TMPDIR_ATTRIBUTE, config_set_tmpdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.tmpdir, CONFIG_STRING, (ConfigGetFunc)config_get_tmpdir},
	/* parameterizing cert dir */
	{CONFIG_CERTDIR_ATTRIBUTE, config_set_certdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.certdir, CONFIG_STRING, (ConfigGetFunc)config_get_certdir},
	/* parameterizing ldif dir */
	{CONFIG_LDIFDIR_ATTRIBUTE, config_set_ldifdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.ldifdir, CONFIG_STRING, (ConfigGetFunc)config_get_ldifdir},
	/* parameterizing bak dir */
	{CONFIG_BAKDIR_ATTRIBUTE, config_set_bakdir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.bakdir, CONFIG_STRING, (ConfigGetFunc)config_get_bakdir},
	/* parameterizing sasl plugin path */
	{CONFIG_SASLPATH_ATTRIBUTE, config_set_saslpath,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.saslpath, CONFIG_STRING, (ConfigGetFunc)config_get_saslpath},
	/* parameterizing run dir */
	{CONFIG_RUNDIR_ATTRIBUTE, config_set_rundir,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.rundir, CONFIG_STRING, (ConfigGetFunc)config_get_rundir},
	{CONFIG_REWRITE_RFC1274_ATTRIBUTE, config_set_rewrite_rfc1274,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.rewrite_rfc1274, CONFIG_ON_OFF, NULL},
	{CONFIG_OUTBOUND_LDAP_IO_TIMEOUT_ATTRIBUTE,
		config_set_outbound_ldap_io_timeout,
		NULL, 0,
		(void **)&global_slapdFrontendConfig.outbound_ldap_io_timeout,
		CONFIG_INT, NULL},
	{CONFIG_UNAUTH_BINDS_ATTRIBUTE, config_set_unauth_binds_switch,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.allow_unauth_binds, CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_unauth_binds_switch},
	{CONFIG_REQUIRE_SECURE_BINDS_ATTRIBUTE, config_set_require_secure_binds,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.require_secure_binds, CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_require_secure_binds},
	{CONFIG_ANON_ACCESS_ATTRIBUTE, config_set_anon_access_switch,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.allow_anon_access, CONFIG_SPECIAL_ANON_ACCESS_SWITCH,
		(ConfigGetFunc)config_get_anon_access_switch},
	{CONFIG_LOCALSSF_ATTRIBUTE, config_set_localssf,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.localssf, CONFIG_INT, NULL},
	{CONFIG_MINSSF_ATTRIBUTE, config_set_minssf,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.minssf, CONFIG_INT, NULL},
	{CONFIG_MINSSF_EXCLUDE_ROOTDSE, config_set_minssf_exclude_rootdse,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.minssf_exclude_rootdse,
		CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_minssf_exclude_rootdse},
	{CONFIG_FORCE_SASL_EXTERNAL_ATTRIBUTE, config_set_force_sasl_external,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.force_sasl_external, CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_force_sasl_external},
	{CONFIG_ENTRYUSN_GLOBAL, config_set_entryusn_global,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.entryusn_global, CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_entryusn_global},
	{CONFIG_ENTRYUSN_IMPORT_INITVAL, config_set_entryusn_import_init,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.entryusn_import_init,
		CONFIG_STRING, (ConfigGetFunc)config_get_entryusn_import_init},
	{CONFIG_ALLOWED_TO_DELETE_ATTRIBUTE, config_set_allowed_to_delete_attrs,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.allowed_to_delete_attrs,
		CONFIG_STRING, (ConfigGetFunc)config_get_allowed_to_delete_attrs},
	{CONFIG_VALIDATE_CERT_ATTRIBUTE, config_set_validate_cert_switch,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.validate_cert,
		CONFIG_SPECIAL_VALIDATE_CERT_SWITCH,
		(ConfigGetFunc)config_get_validate_cert_switch},
	{CONFIG_PAGEDSIZELIMIT_ATTRIBUTE, config_set_pagedsizelimit,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.pagedsizelimit, CONFIG_INT, NULL},
	{CONFIG_DEFAULT_NAMING_CONTEXT, config_set_default_naming_context,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.default_naming_context,
		CONFIG_STRING, (ConfigGetFunc)config_get_default_naming_context},
	{CONFIG_DISK_MONITORING, config_set_disk_monitoring,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.disk_monitoring, CONFIG_ON_OFF,
		(ConfigGetFunc)config_get_disk_monitoring},
	{CONFIG_DISK_THRESHOLD, config_set_disk_threshold,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.disk_threshold, CONFIG_LONG,
		(ConfigGetFunc)config_get_disk_threshold},
	{CONFIG_DISK_GRACE_PERIOD, config_set_disk_grace_period,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.disk_grace_period,
		CONFIG_INT, (ConfigGetFunc)config_get_disk_grace_period},
	{CONFIG_DISK_LOGGING_CRITICAL, config_set_disk_logging_critical,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.disk_logging_critical,
		CONFIG_ON_OFF, (ConfigGetFunc)config_get_disk_logging_critical},
	{CONFIG_SASL_MAXBUFSIZE, config_set_sasl_maxbufsize,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.sasl_max_bufsize,
		CONFIG_INT, (ConfigGetFunc)config_get_sasl_maxbufsize},
	{CONFIG_LISTEN_BACKLOG_SIZE, config_set_listen_backlog_size,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.listen_backlog_size, CONFIG_INT,
		(ConfigGetFunc)config_get_listen_backlog_size}
#ifdef MEMPOOL_EXPERIMENTAL
	,{CONFIG_MEMPOOL_SWITCH_ATTRIBUTE, config_set_mempool_switch,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.mempool_switch, CONFIG_ON_OFF, (ConfigGetFunc)config_get_mempool_switch},
	{CONFIG_MEMPOOL_MAXFREELIST_ATTRIBUTE, config_set_mempool_maxfreelist,
		NULL, 0,
		(void**)&global_slapdFrontendConfig.mempool_maxfreelist, CONFIG_INT, (ConfigGetFunc)config_get_mempool_maxfreelist}
#endif /* MEMPOOL_EXPERIMENTAL */
};

/*
 * hashNocaseString - used for case insensitive hash lookups
 */
static PLHashNumber
hashNocaseString(const void *key)
{
    PLHashNumber h = 0;
    const unsigned char *s;
 
    for (s = key; *s; s++)
        h = (h >> 28) ^ (h << 4) ^ (tolower(*s));
    return h;
}

/*
 * hashNocaseCompare - used for case insensitive hash key comparisons
 */
static PRIntn
hashNocaseCompare(const void *v1, const void *v2)
{
	return (strcasecmp((char *)v1, (char *)v2) == 0);
}

static PLHashTable *confighash = 0;

static void
init_config_get_and_set()
{
	if (!confighash) {
		int ii = 0;
		int tablesize = sizeof(ConfigList)/sizeof(ConfigList[0]);
		confighash = PL_NewHashTable(tablesize+1, hashNocaseString,
									 hashNocaseCompare,
									 PL_CompareValues, 0, 0);
		for (ii = 0; ii < tablesize; ++ii) {
			if (PL_HashTableLookup(confighash, ConfigList[ii].attr_name))
				printf("error: %s is already in the list\n",
					   ConfigList[ii].attr_name);
			if (!PL_HashTableAdd(confighash, ConfigList[ii].attr_name, &ConfigList[ii]))
				printf("error: could not add %s to the list\n",
					   ConfigList[ii].attr_name);
		}
	}
}

#if 0
#define GOLDEN_RATIO    0x9E3779B9U

PR_IMPLEMENT(PLHashEntry **)
PL_HashTableRawLookup(PLHashTable *ht, PLHashNumber keyHash, const void *key)
{
    PLHashEntry *he, **hep, **hep0;
    PLHashNumber h;

#ifdef HASHMETER
    ht->nlookups++;
#endif
    h = keyHash * GOLDEN_RATIO;
    h >>= ht->shift;
    hep = hep0 = &ht->buckets[h];
    while ((he = *hep) != 0) {
        if (he->keyHash == keyHash && (*ht->keyCompare)(key, he->key)) {
            /* Move to front of chain if not already there */
            if (hep != hep0) {
                *hep = he->next;
                he->next = *hep0;
                *hep0 = he;
            }
            return hep0;
        }
        hep = &he->next;
#ifdef HASHMETER
        ht->nsteps++;
#endif
    }
    return hep;
}

static void
debugHashTable(const char *key)
{
	int ii = 0;
	PLHashEntry **hep = PL_HashTableRawLookup(confighash, hashNocaseString(key),
											  key);
	if (!hep || !*hep)
		printf("raw lookup failed for %s\n", key);
	else if (hep && *hep)
		printf("raw lookup found %s -> %ul %s\n", key, (*hep)->keyHash, (*hep)->key);

	printf("hash table has %d entries\n", confighash->nentries);
	for (ii = 0; ii < confighash->nentries; ++ii)
	{
		PLHashEntry *he = confighash->buckets[ii];
		if (!he)
			printf("hash table entry %d is null\n", ii);
		else {
			printf("hash bucket %d:\n", ii);
			while (he) {
				int keys = !hashNocaseCompare(key, he->key);
				int hash = (hashNocaseString(key) == he->keyHash);
				printf("\thashval = %ul key = %s\n", he->keyHash, he->key);
				if (keys && hash) {
					printf("\t\tFOUND\n");
				} else if (keys) {
					printf("\t\tkeys match but hash vals do not\n");
				} else if (hash) {
					printf("\t\thash match but keys do not\n");
				}
				he = he->next;
			}
		}
	}
}
#endif

static void
bervalarray_free(struct berval **bvec)
{
	int ii = 0;
	for(ii = 0; bvec && bvec[ii]; ++ii) {
		slapi_ch_free((void **)&bvec[ii]->bv_val);
		slapi_ch_free((void **)&bvec[ii]);
	}
	slapi_ch_free((void**)&bvec);
}

static struct berval **
strarray2bervalarray(const char **strarray)
{
	int ii = 0;
	struct berval **newlist = 0;

	/* first, count the number of items in the list */
	for (ii = 0; strarray && strarray[ii]; ++ii);

	/* if no items, return null */
	if (!ii)
		return newlist;

	/* allocate the list */
	newlist = (struct berval **)slapi_ch_malloc((ii+1) * sizeof(struct berval *));
	newlist[ii] = 0;
	for (; ii; --ii) {
		newlist[ii-1] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
		newlist[ii-1]->bv_val = slapi_ch_strdup(strarray[ii-1]);
		newlist[ii-1]->bv_len = strlen(strarray[ii-1]);
	}

	return newlist;
}

/*
 * counter for active threads
 */
static PRInt32 active_threads = 0;

void
g_incr_active_threadcnt()
{
    PR_AtomicIncrement(&active_threads);
}

void
g_decr_active_threadcnt()
{
    PR_AtomicDecrement(&active_threads);
}

int
g_get_active_threadcnt()
{
    return (int)active_threads;
}

/*
** Setting this flag forces the server to shutdown.
*/
static int slapd_shutdown;

void g_set_shutdown( int reason )
{
    slapd_shutdown = reason;
}

int g_get_shutdown()
{
    return slapd_shutdown;
}

int slapi_is_shutting_down()
{
	return slapd_shutdown;
}


static int cmd_shutdown;

void c_set_shutdown()
{
   cmd_shutdown = SLAPI_SHUTDOWN_SIGNAL;
}

int c_get_shutdown()
{
   return cmd_shutdown;
}

slapdFrontendConfig_t *
getFrontendConfig()
{
	return &global_slapdFrontendConfig;
}

/*
 * FrontendConfig_init: 
 * Put all default values for config stuff here.
 * If there's no default value, the value will be NULL if it's not set in dse.ldif
 */

void 
FrontendConfig_init () {
  slapdFrontendConfig_t *cfg = getFrontendConfig();

  /* initialize the read/write configuration lock */
  if ( (cfg->cfg_rwlock = slapi_new_rwlock()) == NULL ) {
	LDAPDebug ( LDAP_DEBUG_ANY, 
				"FrontendConfig_init: failed to initialize cfg_rwlock. Exiting now.",
				0,0,0 );
	exit(-1);
  }
				
  cfg->port = LDAP_PORT;
  cfg->secureport = LDAPS_PORT;
  cfg->ldapi_filename = slapi_ch_strdup(SLAPD_LDAPI_DEFAULT_FILENAME);
  cfg->ldapi_switch = LDAP_OFF;
  cfg->ldapi_bind_switch = LDAP_OFF;
  cfg->ldapi_root_dn = slapi_ch_strdup("cn=Directory Manager");
  cfg->ldapi_map_entries = LDAP_OFF;
  cfg->ldapi_uidnumber_type = slapi_ch_strdup("uidNumber");
  cfg->ldapi_gidnumber_type = slapi_ch_strdup("gidNumber");
  /* These DNs are no need to be normalized. */
  cfg->ldapi_search_base_dn = slapi_ch_strdup("dc=example,dc=com");
#if defined(ENABLE_AUTO_DN_SUFFIX)
  cfg->ldapi_auto_dn_suffix = slapi_ch_strdup("cn=peercred,cn=external,cn=auth");
#endif
  cfg->allow_unauth_binds = LDAP_OFF;
  cfg->require_secure_binds = LDAP_OFF;
  cfg->allow_anon_access = SLAPD_ANON_ACCESS_ON;
  cfg->slapi_counters = LDAP_ON;
  cfg->threadnumber = SLAPD_DEFAULT_MAX_THREADS;
  cfg->maxthreadsperconn = SLAPD_DEFAULT_MAX_THREADS_PER_CONN;
  cfg->reservedescriptors = SLAPD_DEFAULT_RESERVE_FDS;
  cfg->idletimeout = SLAPD_DEFAULT_IDLE_TIMEOUT;
  cfg->ioblocktimeout = SLAPD_DEFAULT_IOBLOCK_TIMEOUT;
  cfg->outbound_ldap_io_timeout = SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT;
  cfg->max_filter_nest_level = SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL;
  cfg->maxsasliosize = SLAPD_DEFAULT_MAX_SASLIO_SIZE;
  cfg->localssf = SLAPD_DEFAULT_LOCAL_SSF;
  cfg->minssf = SLAPD_DEFAULT_MIN_SSF;
  cfg->minssf_exclude_rootdse = LDAP_OFF; /* minssf is applied to rootdse,
                                             by default */
  cfg->validate_cert = SLAPD_VALIDATE_CERT_WARN;

#ifdef _WIN32
  cfg->conntablesize = SLAPD_DEFAULT_CONNTABLESIZE;
#else
#ifdef USE_SYSCONF
   cfg->conntablesize  = sysconf( _SC_OPEN_MAX );
#else /* USE_SYSCONF */
   cfg->conntablesize = getdtablesize();
#endif /* USE_SYSCONF */
#endif /* _WIN32 */

  cfg->accesscontrol = LDAP_ON;
  cfg->security = LDAP_OFF;
  cfg->ssl_check_hostname = LDAP_ON;
  cfg->return_exact_case = LDAP_ON;
  cfg->result_tweak = LDAP_OFF;
  cfg->reservedescriptors = SLAPD_DEFAULT_RESERVE_FDS;
  cfg->useroc = slapi_ch_strdup ( "" );
  cfg->userat = slapi_ch_strdup ( "" );
/* kexcoff: should not be initialized by default here
  cfg->rootpwstoragescheme = pw_name2scheme( SHA1_SCHEME_NAME );
  cfg->pw_storagescheme = pw_name2scheme( SHA1_SCHEME_NAME );
*/
  cfg->slapd_type = 0;
  cfg->versionstring = SLAPD_VERSION_STR;
  cfg->sizelimit = SLAPD_DEFAULT_SIZELIMIT;
  cfg->pagedsizelimit = 0;
  cfg->timelimit = SLAPD_DEFAULT_TIMELIMIT;
  cfg->anon_limits_dn = slapi_ch_strdup("");
  cfg->schemacheck = LDAP_ON;
  cfg->syntaxcheck = LDAP_OFF;
  cfg->plugin_track = LDAP_OFF;
  cfg->syntaxlogging = LDAP_OFF;
  cfg->dn_validate_strict = LDAP_OFF;
  cfg->ds4_compatible_schema = LDAP_OFF;
  cfg->enquote_sup_oc = LDAP_OFF;
  cfg->lastmod = LDAP_ON;
  cfg->rewrite_rfc1274 = LDAP_OFF;
  cfg->schemareplace = slapi_ch_strdup( CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY );
  cfg->schema_ignore_trailing_spaces = SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES;
  cfg->force_sasl_external = LDAP_OFF; /* do not force sasl external by default - let clients abide by the LDAP standards and send us a SASL/EXTERNAL bind if that's what they want to do */

  cfg->pwpolicy_local = LDAP_OFF;
  cfg->pw_policy.pw_change = LDAP_ON;
  cfg->pw_policy.pw_must_change = LDAP_OFF;
  cfg->pw_policy.pw_syntax = LDAP_OFF;
  cfg->pw_policy.pw_exp = LDAP_OFF;
  cfg->pw_policy.pw_minlength = 8;
  cfg->pw_policy.pw_mindigits = 0;
  cfg->pw_policy.pw_minalphas = 0;
  cfg->pw_policy.pw_minuppers = 0;
  cfg->pw_policy.pw_minlowers = 0;
  cfg->pw_policy.pw_minspecials = 0;
  cfg->pw_policy.pw_min8bit = 0;
  cfg->pw_policy.pw_maxrepeats = 0;
  cfg->pw_policy.pw_mincategories = 3;
  cfg->pw_policy.pw_mintokenlength = 3;
  cfg->pw_policy.pw_maxage = 8640000; /* 100 days     */
  cfg->pw_policy.pw_minage = 0;
  cfg->pw_policy.pw_warning = 86400; /* 1 day        */
  cfg->pw_policy.pw_history = LDAP_OFF;
  cfg->pw_policy.pw_inhistory = 6;
  cfg->pw_policy.pw_lockout = LDAP_OFF;
  cfg->pw_policy.pw_maxfailure = 3;
  cfg->pw_policy.pw_unlock = LDAP_ON;
  cfg->pw_policy.pw_lockduration = 3600;     /* 60 minutes   */
  cfg->pw_policy.pw_resetfailurecount = 600; /* 10 minutes   */ 
  cfg->pw_policy.pw_gracelimit = 0;
  cfg->pw_policy.pw_is_legacy = LDAP_ON;
  cfg->pw_policy.pw_track_update_time = LDAP_OFF;
  cfg->pw_is_global_policy = LDAP_OFF;

  cfg->accesslog_logging_enabled = LDAP_ON;
  cfg->accesslog_mode = slapi_ch_strdup("600");
  cfg->accesslog_maxnumlogs = 10;
  cfg->accesslog_maxlogsize = 100;
  cfg->accesslog_rotationtime = 1;
  cfg->accesslog_rotationunit = slapi_ch_strdup("day");
  cfg->accesslog_rotationsync_enabled = LDAP_OFF;
  cfg->accesslog_rotationsynchour = 0;
  cfg->accesslog_rotationsyncmin = 0;
  cfg->accesslog_maxdiskspace = 500;
  cfg->accesslog_minfreespace = 5;
  cfg->accesslog_exptime = 1;
  cfg->accesslog_exptimeunit = slapi_ch_strdup("month");
  cfg->accessloglevel = 256;
  cfg->accesslogbuffering = LDAP_ON;
  cfg->csnlogging = LDAP_ON;

  cfg->errorlog_logging_enabled = LDAP_ON;
  cfg->errorlog_mode = slapi_ch_strdup("600");
  cfg->errorlog_maxnumlogs = 1;
  cfg->errorlog_maxlogsize = 100;
  cfg->errorlog_rotationtime = 1;
  cfg->errorlog_rotationunit = slapi_ch_strdup ("week");
  cfg->errorlog_rotationsync_enabled = LDAP_OFF;
  cfg->errorlog_rotationsynchour = 0;
  cfg->errorlog_rotationsyncmin = 0;
  cfg->errorlog_maxdiskspace = 100;
  cfg->errorlog_minfreespace = 5;
  cfg->errorlog_exptime = 1;
  cfg->errorlog_exptimeunit = slapi_ch_strdup("month");
  cfg->errorloglevel = 0;

  cfg->auditlog_logging_enabled = LDAP_OFF;
  cfg->auditlog_mode = slapi_ch_strdup("600");
  cfg->auditlog_maxnumlogs = 1;
  cfg->auditlog_maxlogsize = 100;
  cfg->auditlog_rotationtime = 1;
  cfg->auditlog_rotationunit = slapi_ch_strdup ("week");
  cfg->auditlog_rotationsync_enabled = LDAP_OFF;
  cfg->auditlog_rotationsynchour = 0;
  cfg->auditlog_rotationsyncmin = 0;
  cfg->auditlog_maxdiskspace = 100;
  cfg->auditlog_minfreespace = 5;
  cfg->auditlog_exptime = 1;
  cfg->auditlog_exptimeunit = slapi_ch_strdup("month");
  cfg->auditlog_logging_hide_unhashed_pw = LDAP_ON;

  cfg->entryusn_global = LDAP_OFF; 
  cfg->entryusn_import_init = slapi_ch_strdup("0"); 
  cfg->allowed_to_delete_attrs = slapi_ch_strdup("nsslapd-listenhost nsslapd-securelistenhost nsslapd-defaultnamingcontext");
  cfg->default_naming_context = NULL; /* store normalized dn */

  cfg->disk_monitoring = LDAP_OFF;
  cfg->disk_threshold = 2097152;  /* 2 mb */
  cfg->disk_grace_period = 60; /* 1 hour */
  cfg->disk_logging_critical = LDAP_OFF;
  cfg->sasl_max_bufsize = SLAPD_DEFAULT_SASL_MAXBUFSIZE;

  cfg->listen_backlog_size = DAEMON_LISTEN_SIZE;
#ifdef MEMPOOL_EXPERIMENTAL
  cfg->mempool_switch = LDAP_ON;
  cfg->mempool_maxfreelist = 1024;
  cfg->system_page_size = sysconf(_SC_PAGE_SIZE);	/* not to get every time; no set, get only */
  {
    long sc_size = cfg->system_page_size;
    cfg->system_page_bits = 0;
    while ((sc_size >>= 1) > 0) {
      cfg->system_page_bits++;	/* to calculate once; no set, get only */
    }
  }
#endif /* MEMPOOL_EXPERIMENTAL */

  init_config_get_and_set();
}

int 
g_get_global_lastmod()
{
  return  config_get_lastmod();
}


int g_get_slapd_security_on(){
  return config_get_security();
}



#ifdef _WIN32
void libldap_init_debug_level(int *val_ptr)
{
    module_ldap_debug = val_ptr;
}
#endif

struct snmp_vars_t global_snmp_vars;

struct snmp_vars_t * g_get_global_snmp_vars(){
    return &global_snmp_vars;
}

static slapdEntryPoints *sep = NULL;
void
set_dll_entry_points( slapdEntryPoints *p )
{
    if ( NULL == sep )
    {
    	sep = p;
    }
}


int
get_entry_point( int ep_name, caddr_t *ep_addr )
{
    int rc = 0;

    if(sep!=NULL)
    {
        switch ( ep_name ) {
        case ENTRY_POINT_PS_WAKEUP_ALL:
        	*ep_addr = sep->sep_ps_wakeup_all;
        	break;
        case ENTRY_POINT_PS_SERVICE:
        	*ep_addr = sep->sep_ps_service;
        	break;
        case ENTRY_POINT_DISCONNECT_SERVER:
        	*ep_addr = sep->sep_disconnect_server;
        	break;
        case ENTRY_POINT_SLAPD_SSL_INIT:
        	*ep_addr = sep->sep_slapd_ssl_init;
        	break;
        case ENTRY_POINT_SLAPD_SSL_INIT2:
        	*ep_addr = sep->sep_slapd_ssl_init2;
        	break;
        default:
        	rc = -1;
        }
    }
    else
    {
        rc= -1;
    }
    return rc;
}

int
config_set_auditlog_unhashed_pw(const char *attrname, char *value, char *errorbuf, int apply)
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int retVal = LDAP_SUCCESS;

	retVal = config_set_onoff ( attrname, value, &(slapdFrontendConfig->auditlog_logging_hide_unhashed_pw),
								errorbuf, apply);
	if(strcasecmp(value,"on") == 0){
		auditlog_hide_unhashed_pw();
	} else {
		auditlog_expose_unhashed_pw();
	}
	return retVal;
}

/*
 * Utility function called by many of the config_set_XXX() functions.
 * Returns a non-zero value if 'value' is NULL and zero if not.
 * Also constructs an error message in 'errorbuf' if value is NULL.
 * If or_zero_length is non-zero, zero length values are treated as
 * equivalent to NULL (i.e., they will cause a non-zero value to be
 * returned by this function).
 */
static int
config_value_is_null( const char *attrname, const char *value, char *errorbuf,
		int or_zero_length )
{
	if ( NULL == value || ( or_zero_length && *value == '\0' )) {
		PR_snprintf( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: NULL value",
				attrname );
		return 1;
	}

	return 0;
}

int
config_set_disk_monitoring( const char *attrname, char *value, char *errorbuf, int apply )
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;

    retVal = config_set_onoff ( attrname, value, &(slapdFrontendConfig->disk_monitoring),
                                errorbuf, apply);
    return retVal;
}

int
config_set_disk_threshold( const char *attrname, char *value, char *errorbuf, int apply )
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    PRInt64 threshold = 0;
    char *endp = NULL;

    if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    threshold = strtoll(value, &endp, 10);

    if ( *endp != '\0' || threshold < 4096 || errno == ERANGE ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, threshold must be greater than 4096 and less then %lld",
            attrname, value, (long long int)LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->disk_threshold = threshold;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_disk_logging_critical( const char *attrname, char *value, char *errorbuf, int apply )
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;

    retVal = config_set_onoff ( attrname, value, &(slapdFrontendConfig->disk_logging_critical),
                                errorbuf, apply);
    return retVal;
}

int
config_set_disk_grace_period( const char *attrname, char *value, char *errorbuf, int apply )
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    int period = 0;
    char *endp = NULL;

    if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
    }

    period = strtol(value, &endp, 10);

    if ( *endp != '\0' || period < 1 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, grace period must be at least 1 minute",
                      attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->disk_grace_period = period;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_sasl_maxbufsize(const char *attrname, char *value, char *errorbuf, int apply )
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    int default_size = atoi(DEFAULT_SASL_MAXBUFSIZE);
    int size;

    size = atoi(value);
    if(size < default_size){
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "nsslapd-sasl-max-buffer-size is too low (%d), "
            "setting to default value (%d).\n",size, default_size);
        size = default_size;
    }
    if(apply){
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->sasl_max_bufsize = size;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int 
config_set_port( const char *attrname, char *port, char *errorbuf, int apply ) {
  long nPort;
  char *endp = NULL;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal = LDAP_SUCCESS;
  
  if ( config_value_is_null( attrname, port, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  nPort = strtol(port, &endp, 10);
  
  if ( *endp != '\0' || errno == ERANGE || nPort > LDAP_PORT_MAX || nPort < 0 ) {
	retVal = LDAP_OPERATIONS_ERROR;
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "%s: \"%s\" is invalid, ports must range from 0 to %d",
			  attrname, port, LDAP_PORT_MAX );
        return retVal;
  }

  if ( nPort == 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                           "Information: Non-Secure Port Disabled\n", 0, 0, 0 );
  }
  
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	slapdFrontendConfig->port = nPort;
	/*	n_port = nPort; */

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_secureport( const char *attrname, char *port, char *errorbuf, int apply ) {
  long nPort;
  char *endp = NULL;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal = LDAP_SUCCESS;

  if ( config_value_is_null( attrname, port, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  nPort = strtol(port, &endp, 10);
  
  if (*endp != '\0' || errno == ERANGE || nPort > LDAP_PORT_MAX || nPort <= 0 ) {
	retVal = LDAP_OPERATIONS_ERROR;
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "%s: \"%s\" is invalid, ports must range from 1 to %d",
			  attrname, port, LDAP_PORT_MAX );
  }
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapdFrontendConfig->secureport = nPort;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}
						  

int 
config_set_SSLclientAuth( const char *attrname, char *value, char *errorbuf, int apply ) {
  
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	retVal = LDAP_OPERATIONS_ERROR;
  }
  /* first check the value, return an error if it's invalid */
  else if ( strcasecmp (value, "off") != 0 &&
			strcasecmp (value, "allowed") != 0 &&
			strcasecmp (value, "required")!= 0 ) {
	retVal = LDAP_OPERATIONS_ERROR;
	if( errorbuf )
	  PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: unsupported value: %s", attrname, value );
	return retVal;
  }
  else if ( !apply ) {
	/* return success now, if we aren't supposed to apply the change */
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);

  if ( !strcasecmp( value, "off" )) {
	slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_OFF; 
  } 
  else if ( !strcasecmp( value, "allowed" )) {
	slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_ALLOWED;
  } 
  else if ( !strcasecmp( value, "required" )) {
	slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_REQUIRED;
  }
  else {
	retVal = LDAP_OPERATIONS_ERROR;
	if( errorbuf )
		PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"%s: unsupported value: %s", attrname, value );
  }

  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  
  return retVal;
}


int
config_set_ssl_check_hostname(const char *attrname, char *value,
		char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	retVal = config_set_onoff(attrname,
							  value, 
							  &(slapdFrontendConfig->ssl_check_hostname),
							  errorbuf,
							  apply);
  
	return retVal;
}

int
config_set_localhost( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	slapi_ch_free (  (void **) &(slapdFrontendConfig->localhost) );
	slapdFrontendConfig->localhost = slapi_ch_strdup ( value );

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
} 

int
config_set_listenhost( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapi_ch_free ( (void **) &(slapdFrontendConfig->listenhost) );
	slapdFrontendConfig->listenhost = slapi_ch_strdup ( value );

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_ldapi_filename( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_filename) );
        slapdFrontendConfig->ldapi_filename = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_ldapi_switch( const char *attrname, char *value, char *errorbuf, int apply ) {
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->ldapi_switch),
		errorbuf,
		apply);

	return retVal;
}

int config_set_ldapi_bind_switch( const char *attrname, char *value, char *errorbuf, int apply )
{
        int retVal = LDAP_SUCCESS;
        slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

        retVal = config_set_onoff(attrname,
                value,
                &(slapdFrontendConfig->ldapi_bind_switch),
                errorbuf,
                apply);

        return retVal;
}

int config_set_ldapi_root_dn( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_root_dn) );
        slapdFrontendConfig->ldapi_root_dn = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int config_set_ldapi_map_entries( const char *attrname, char *value, char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->ldapi_map_entries),
		errorbuf,
		apply);

	return retVal;
} 

int config_set_ldapi_uidnumber_type( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_uidnumber_type) );
        slapdFrontendConfig->ldapi_uidnumber_type = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
} 

int config_set_ldapi_gidnumber_type( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_gidnumber_type) );
        slapdFrontendConfig->ldapi_gidnumber_type = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int config_set_ldapi_search_base_dn( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_search_base_dn) );
        slapdFrontendConfig->ldapi_search_base_dn = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

#if defined(ENABLE_AUTO_DN_SUFFIX)
int config_set_ldapi_auto_dn_suffix( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->ldapi_auto_dn_suffix) );
        slapdFrontendConfig->ldapi_auto_dn_suffix = slapi_ch_strdup ( value );
         CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}
#endif

int config_set_anon_limits_dn( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  if ( apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free ( (void **) &(slapdFrontendConfig->anon_limits_dn) );
        slapdFrontendConfig->anon_limits_dn = 
                                       slapi_create_dn_string("%s", value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

/*
 * Set nsslapd-counters: on | off to the internal config variable slapi_counters.
 * If set to off, slapi_counters is not initialized and the counters are not
 * incremented.  Note: counters which are necessary for the server's running
 * are not disabled.
 */
int config_set_slapi_counters( const char *attrname, char *value, char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname, value,
				&(slapdFrontendConfig->slapi_counters), errorbuf, apply);

	return retVal;
}

int
config_set_securelistenhost( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapi_ch_free (  (void **) &(slapdFrontendConfig->securelistenhost) );
	slapdFrontendConfig->securelistenhost = slapi_ch_strdup ( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_srvtab( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapi_ch_free ( (void **) &(slapdFrontendConfig->srvtab) );
	ldap_srvtab = slapi_ch_strdup ( value );
	slapdFrontendConfig->srvtab = slapi_ch_strdup ( value );
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_sizelimit( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long sizelimit;
  char *endp = NULL;
  Slapi_Backend *be;
  char *cookie;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  sizelimit = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || sizelimit < -1 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, sizelimit must range from -1 to %ld",
			attrname, value, LONG_MAX );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if (apply) {
	
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapdFrontendConfig->sizelimit= sizelimit;
	g_set_defsize (sizelimit);
	cookie = NULL;
	be = slapi_get_first_backend(&cookie);
	while (be) {
	  be->be_sizelimit = slapdFrontendConfig->sizelimit;
	  be = slapi_get_next_backend(cookie);
	}
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ((void **)&cookie);

  }
  return retVal;  
}

int 
config_set_pagedsizelimit( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long pagedsizelimit;
  char *endp = NULL;
  Slapi_Backend *be;
  char *cookie;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  pagedsizelimit = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || pagedsizelimit < -1 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, pagedsizelimit must range from -1 to %ld",
			attrname, value, LONG_MAX );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if (apply) {
	
	CFG_LOCK_WRITE(slapdFrontendConfig);
	
	slapdFrontendConfig->pagedsizelimit= pagedsizelimit;
	cookie = NULL;
	be = slapi_get_first_backend(&cookie);
	while (be) {
	  be->be_pagedsizelimit = slapdFrontendConfig->pagedsizelimit;
	  be = slapi_get_next_backend(cookie);
	}
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ((void **)&cookie);

  }
  return retVal;  
}

int
config_set_pw_storagescheme( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  struct pw_scheme *new_scheme = NULL;
  char * scheme_list = NULL;
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
  
  new_scheme = pw_name2scheme(value);
  if ( new_scheme == NULL) {
		if ( scheme_list != NULL ) {
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: invalid scheme - %s. Valid schemes are: %s",
					attrname, value, scheme_list );
		} else {
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: invalid scheme - %s (no pwdstorage scheme"
					" plugin loaded)",
					attrname, value);
		}
	retVal = LDAP_OPERATIONS_ERROR;
	slapi_ch_free_string(&scheme_list);
	return retVal;
  }
  else if ( new_scheme->pws_enc == NULL )
  {
	/* For example: the NS-MTA-MD5 password scheme is for comparision only and for backward 
	compatibility with an Old Messaging Server that was setting passwords in the 
	directory already encrypted. The scheme cannot and don't encrypt password if 
	they are in clear. We don't take it */ 

	if ( scheme_list != NULL ) {
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
				"pw_storagescheme: invalid encoding scheme - %s\nValid values are: %s\n", value, scheme_list );
	}
	retVal = LDAP_UNWILLING_TO_PERFORM;
	slapi_ch_free_string(&scheme_list);
	free_pw_scheme(new_scheme);
	return retVal;
  }
    
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	free_pw_scheme(slapdFrontendConfig->pw_storagescheme);
	slapdFrontendConfig->pw_storagescheme = new_scheme;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  } else {
	free_pw_scheme(new_scheme);
  }
  slapi_ch_free_string(&scheme_list);

  return retVal;
}
	

int
config_set_pw_change( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_change),
							  errorbuf,
							  apply);
  
  if (retVal == LDAP_SUCCESS) {
	  /* LP: Update ACI to reflect the value ! */
	  if (apply)
		  pw_mod_allowchange_aci(!slapdFrontendConfig->pw_policy.pw_change &&
								 !slapdFrontendConfig->pw_policy.pw_must_change);
  }
  
  return retVal;
}


int
config_set_pw_history( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_history),
							  errorbuf,
							  apply);
  
  return retVal;
}



int
config_set_pw_must_change( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_must_change),
							  errorbuf,
							  apply);
  
  if (retVal == LDAP_SUCCESS) {
	  /* LP: Update ACI to reflect the value ! */
	  if (apply)
		  pw_mod_allowchange_aci(!slapdFrontendConfig->pw_policy.pw_change &&
								 !slapdFrontendConfig->pw_policy.pw_must_change);
  }
  
  return retVal;
}


int
config_set_pwpolicy_local( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pwpolicy_local),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_pw_syntax( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_syntax),
							  errorbuf,
							  apply);
  
  return retVal;
}



int
config_set_pw_minlength( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minLength = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  minLength = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minLength < 2 || minLength > 512 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password minimum length \"%s\" is invalid. "
			  "The minimum length must range from 2 to 512.",
			  value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);	

	slapdFrontendConfig->pw_policy.pw_minlength = minLength;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  
  return retVal;
}

int
config_set_pw_mindigits( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minDigits = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minDigits = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minDigits < 0 || minDigits > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of digits \"%s\" is invalid. "
                          "The minimum number of digits must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mindigits = minDigits;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_minalphas( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minAlphas = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minAlphas = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minAlphas < 0 || minAlphas > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of alphas \"%s\" is invalid. "
                          "The minimum number of alphas must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minalphas = minAlphas;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_minuppers( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minUppers = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minUppers = strtol(value, &endp, 10);

  if (  *endp != '\0' || errno == ERANGE || minUppers < 0 || minUppers > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of uppercase characters \"%s\" is invalid. "
                          "The minimum number of uppercase characters must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minuppers = minUppers;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_minlowers( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minLowers = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minLowers = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minLowers < 0 || minLowers > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of lowercase characters \"%s\" is invalid. "
                          "The minimum number of lowercase characters must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minlowers = minLowers;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_minspecials( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minSpecials = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minSpecials = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minSpecials < 0 || minSpecials > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of special characters \"%s\" is invalid. "
                          "The minimum number of special characters must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minspecials = minSpecials;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_min8bit( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long min8bit = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  min8bit = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || min8bit < 0 || min8bit > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of 8-bit characters \"%s\" is invalid. "
                          "The minimum number of 8-bit characters must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_min8bit = min8bit;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_maxrepeats( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long maxRepeats = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  maxRepeats = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || maxRepeats < 0 || maxRepeats > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password maximum number of repeated characters \"%s\" is invalid. "
                          "The maximum number of repeated characters must range from 0 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_maxrepeats = maxRepeats;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_mincategories( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minCategories = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minCategories = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minCategories < 1 || minCategories > 5 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum number of categories \"%s\" is invalid. "
                          "The minimum number of categories must range from 1 to 5.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mincategories = minCategories;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_mintokenlength( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long minTokenLength = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  minTokenLength = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || minTokenLength < 1 || minTokenLength > 64 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                          "password minimum token length \"%s\" is invalid. "
                          "The minimum token length must range from 1 to 64.",
                          value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mintokenlength = minTokenLength;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_pw_maxfailure( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long maxFailure = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  maxFailure = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || maxFailure <= 0 || maxFailure > 32767 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password maximum retry \"%s\" is invalid. "
			  "Password maximum failure must range from 1 to 32767",
			  value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	slapdFrontendConfig->pw_policy.pw_maxfailure = maxFailure;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  
  return retVal;
}



int
config_set_pw_inhistory( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long history = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  history = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || history < 2 || history > 24 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password history length \"%s\" is invalid. "
			  "The password history must range from 2 to 24",
			  value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	slapdFrontendConfig->pw_policy.pw_inhistory = history;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  
  return retVal;
}


int
config_set_pw_lockduration( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long duration = 0; /* in minutes */

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  /* in seconds */
  duration = parse_duration(value);

  if ( errno == ERANGE || duration <= 0 || duration > (MAX_ALLOWED_TIME_IN_SECS - current_time()) ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password lockout duration \"%s\" is invalid. ",
			  value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
    slapdFrontendConfig->pw_policy.pw_lockduration = duration;
  }
  
  return retVal;
}


int
config_set_pw_resetfailurecount( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long duration = 0; /* in minutes */

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  /* in seconds */  
  duration = parse_duration(value);

  if ( errno == ERANGE || duration < 0 || duration > (MAX_ALLOWED_TIME_IN_SECS - current_time()) ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password reset count duration \"%s\" is invalid. ",
			  value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	slapdFrontendConfig->pw_policy.pw_resetfailurecount = duration;
  }
  
  return retVal;
}

int
config_set_pw_is_global_policy( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_is_global_policy),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_pw_is_legacy_policy( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value,
							  &(slapdFrontendConfig->pw_policy.pw_is_legacy),
							  errorbuf,
							  apply);

  return retVal;
}

int
config_set_pw_track_last_update_time( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value,
							  &(slapdFrontendConfig->pw_policy.pw_track_update_time),
							  errorbuf,
							  apply);

  return retVal;
}

int
config_set_pw_exp( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_exp),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_pw_unlock( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_unlock),
							  errorbuf,
							  apply);
  
  return retVal;
}


int
config_set_pw_lockout( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->pw_policy.pw_lockout),
							  errorbuf,
							  apply);
  
  return retVal;
}


int
config_set_pw_gracelimit( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long gracelimit = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  errno = 0;
  gracelimit = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || gracelimit < 0 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "password grace limit \"%s\" is invalid, password grace limit must range from 0 to %ld",
			  value , LONG_MAX );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	slapdFrontendConfig->pw_policy.pw_gracelimit = gracelimit;
	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  
  return retVal;
}


int
config_set_lastmod( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  Slapi_Backend *be = NULL;
  char *cookie;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->lastmod),
							  errorbuf,
							  apply);
  
  if ( retVal == LDAP_SUCCESS && apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);

	cookie = NULL;
	be = slapi_get_first_backend (&cookie);
	while (be) {
	  be->be_lastmod = slapdFrontendConfig->lastmod;
	  be = slapi_get_next_backend (cookie);
	}

	CFG_UNLOCK_WRITE(slapdFrontendConfig);

	slapi_ch_free ((void **)&cookie);

  }
  
  return retVal;
}


int
config_set_nagle( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->nagle),
							  errorbuf,
							  apply);

  return retVal;
}


int
config_set_accesscontrol( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->accesscontrol),
							  errorbuf,
							  apply);
  
  return retVal;
}



int
config_set_return_exact_case( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->return_exact_case),
							  errorbuf,
							  apply);
  
  return retVal;
}


int
config_set_result_tweak( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->result_tweak),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_plugin_tracking( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value,
							  &(slapdFrontendConfig->plugin_track),
							  errorbuf,
							  apply);

  return retVal;
}

int
config_set_security( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->security),
							  errorbuf,
							  apply);
  
  return retVal;
}


static int 
config_set_onoff ( const char *attrname, char *value, int *configvalue,
		char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  if ( strcasecmp ( value, "on" ) != 0 &&
	   strcasecmp ( value, "off") != 0 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid value \"%s\". Valid values are \"on\" or \"off\".",
			attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
  }
 
  if ( !apply ) {
	/* we can return now if we aren't applying the changes */
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);
  
  if ( strcasecmp ( value, "on" ) == 0 ) {
	*configvalue = LDAP_ON;
  }
  else if ( strcasecmp ( value, "off" ) == 0 ) {
	*configvalue = LDAP_OFF;
  }
  
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  return retVal;
}
			 
int
config_set_readonly( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->readonly),
							  errorbuf,
							  apply );
  
  return retVal;
}


int
config_set_schemacheck( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->schemacheck),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_syntaxcheck( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
                              value,
                              &(slapdFrontendConfig->syntaxcheck),
                              errorbuf,
                              apply);

  return retVal;
} 

int
config_set_syntaxlogging( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
                              value,
                              &(slapdFrontendConfig->syntaxlogging),
                              errorbuf,
                              apply);

  return retVal;
}

int
config_set_dn_validate_strict( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
                              value,
                              &(slapdFrontendConfig->dn_validate_strict),
                              errorbuf,
                              apply);

  return retVal;
}

int
config_set_ds4_compatible_schema( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->ds4_compatible_schema),
							  errorbuf,
							  apply);
  
  return retVal;
}


int
config_set_schema_ignore_trailing_spaces( const char *attrname, char *value,
		char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->schema_ignore_trailing_spaces),
							  errorbuf,
							  apply);
  
  return retVal;
}


int
config_set_enquote_sup_oc( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
							  value, 
							  &(slapdFrontendConfig->enquote_sup_oc),
							  errorbuf,
							  apply);
  
  return retVal;
}

int
config_set_rootdn( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &(slapdFrontendConfig->rootdn) );
	slapdFrontendConfig->rootdn = slapi_dn_normalize (slapi_ch_strdup ( value ) );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_rootpw( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  struct pw_scheme *is_hashed = NULL;

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  if (!apply) {
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);

  slapi_ch_free ( (void **) &(slapdFrontendConfig->rootpw) );
  
  is_hashed  = pw_val2scheme ( value, NULL, 0 );
  
  if ( is_hashed ) {
    slapdFrontendConfig->rootpw = slapi_ch_strdup ( value );
    free_pw_scheme(is_hashed);
  } else if (slapd_nss_is_initialized() ||
            (strcasecmp(slapdFrontendConfig->rootpwstoragescheme->pws_name,
                       "clear") == 0)) {
    /* to hash, security library should have been initialized, by now */
    /* pwd enc func returns slapi_ch_malloc memory */
    slapdFrontendConfig->rootpw = (slapdFrontendConfig->rootpwstoragescheme->pws_enc)(value); 
  } else {
    PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                  "%s: password scheme mismatch (passwd scheme is %s; "
                  "password is clear text)", attrname,
                  slapdFrontendConfig->rootpwstoragescheme->pws_name);
    retVal = LDAP_PARAM_ERROR;
  }
  
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  return retVal;
}


int
config_set_rootpwstoragescheme( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	struct pw_scheme *new_scheme = NULL;
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

	new_scheme = pw_name2scheme ( value );
  if (new_scheme == NULL ) {
		char * scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME); 
		if ( scheme_list != NULL ) { 
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: invalid scheme - %s. Valid schemes are: %s",
					attrname, value, scheme_list );
		} else {
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
					"%s: invalid scheme - %s (no pwdstorage scheme"
					" plugin loaded)", attrname, value);
		}
		slapi_ch_free_string(&scheme_list);
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }
  
  CFG_LOCK_WRITE(slapdFrontendConfig);
	free_pw_scheme(slapdFrontendConfig->rootpwstoragescheme);
  slapdFrontendConfig->rootpwstoragescheme = new_scheme;
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  
  return retVal;
}
	
/*
 * kexcoff: to replace default initialization in FrontendConfig_init()
 */
int config_set_storagescheme() {

	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	struct pw_scheme *new_scheme = NULL;

    CFG_LOCK_WRITE(slapdFrontendConfig);

	new_scheme = pw_name2scheme("SSHA");
	free_pw_scheme(slapdFrontendConfig->pw_storagescheme);
	slapdFrontendConfig->pw_storagescheme = new_scheme;

	new_scheme = pw_name2scheme("SSHA");
	slapdFrontendConfig->rootpwstoragescheme = new_scheme;
       
    CFG_UNLOCK_WRITE(slapdFrontendConfig);

	return ( new_scheme == NULL );

}

#ifndef _WIN32
int 
config_set_localuser( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if (apply) {
    struct passwd *pw = NULL;
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &slapdFrontendConfig->localuser );
	slapdFrontendConfig->localuser = slapi_ch_strdup ( value );
	if (slapdFrontendConfig->localuserinfo != NULL) {
	  slapi_ch_free ( (void **) &(slapdFrontendConfig->localuserinfo) );
	}
	pw = getpwnam( value );
	if ( pw ) {
	  slapdFrontendConfig->localuserinfo =
			  (struct passwd *)slapi_ch_malloc(sizeof(struct passwd));
	  memcpy(slapdFrontendConfig->localuserinfo, pw, sizeof(struct passwd));
	}

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}
#endif /* _WIN32 */

int
config_set_workingdir( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if ( PR_Access ( value, PR_ACCESS_EXISTS ) != 0 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Working directory \"%s\" does not exist.", value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }
  if ( PR_Access ( value, PR_ACCESS_WRITE_OK ) != 0 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Working directory \"%s\" is not writeable.", value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->workingdir = slapi_ch_strdup ( value );
#ifdef _WIN32
	dostounixpath(slapdFrontendConfig->workingdir);
#endif /* _WIN32 */
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

/* alias of encryption key and certificate files is now retrieved through */
/* calls to psetFullCreate() and psetGetAttrSingleValue(). See ssl.c, */
/* where this function is still used to set the global variable */
int 
config_set_encryptionalias( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &(slapdFrontendConfig->encryptionalias) );
	
	slapdFrontendConfig->encryptionalias = slapi_ch_strdup ( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_threadnumber( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long threadnum = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  threadnum = strtol(value, &endp, 10);
  
  if ( *endp != '\0' || errno == ERANGE || threadnum < 1 || threadnum > 65535 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", maximum thread number must range from 1 to 65535", attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
  }
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	/*	max_threads = threadnum; */
	slapdFrontendConfig->threadnumber = threadnum;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_maxthreadsperconn( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long maxthreadnum = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  maxthreadnum = strtol(value, &endp, 10);
  
  if ( *endp != '\0' || errno == ERANGE || maxthreadnum < 1 || maxthreadnum > 65535 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", maximum thread number per connection must range from 1 to 65535", attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
  }
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	/*	max_threads_per_conn = maxthreadnum; */
	slapdFrontendConfig->maxthreadsperconn = maxthreadnum;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

#if !defined(_WIN32) && !defined(AIX)
#include <sys/resource.h>
int
config_set_maxdescriptors( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nValue = 0;
  int maxVal = 65535;
  struct rlimit rlp;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if ( 0 == getrlimit( RLIMIT_NOFILE, &rlp ) ) {
          maxVal = (int)rlp.rlim_max;
  }

  errno = 0;
  nValue = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || nValue < 1 || nValue > maxVal ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", maximum "
			"file descriptors must range from 1 to %d (the current process limit).  "
			"Server will use a setting of %d.", attrname, value, maxVal, maxVal);
        if ( nValue > maxVal ) {
            nValue = maxVal;
            retVal = LDAP_UNWILLING_TO_PERFORM;
        } else {
	    retVal = LDAP_OPERATIONS_ERROR;
        }
  }
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->maxdescriptors = nValue;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}
#endif /* !_WIN32 && !AIX */

int
config_set_conntablesize( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nValue = 0;
  int maxVal = 65535;
  char *endp = NULL;
#ifndef _WIN32
  struct rlimit rlp;
#endif
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

#ifndef _WIN32
  if ( 0 == getrlimit( RLIMIT_NOFILE, &rlp ) ) {
          maxVal = (int)rlp.rlim_max;
  }
#endif

  errno = 0;
  nValue = strtol(value, &endp, 0);
  
#ifdef _WIN32
  if ( *endp != '\0' || errno == ERANGE || nValue < 1 || nValue > 0xfffffe ) {
	PR_snprintf ( errorbuf,  SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", connection table size must range from 1 to 0xfffffe", attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
  }
#elif !defined(AIX)

  if ( *endp != '\0' || errno == ERANGE || nValue < 1 || nValue > maxVal ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", connection table "
			"size must range from 1 to %d (the current process maxdescriptors limit).  "
			"Server will use a setting of %d.", attrname, value, maxVal, maxVal );
        if ( nValue > maxVal) {
            nValue = maxVal;
            retVal = LDAP_UNWILLING_TO_PERFORM;
        } else {
            retVal = LDAP_OPERATIONS_ERROR;
        }
  }
#endif
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->conntablesize = nValue;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}

int
config_set_reservedescriptors( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  int maxVal = 65535;
  long nValue = 0;
  char *endp = NULL;
#ifndef _WIN32
  struct rlimit rlp;
#endif

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

#ifndef _WIN32
  if ( 0 == getrlimit( RLIMIT_NOFILE, &rlp ) ) {
          maxVal = (int)rlp.rlim_max;
  }
#endif

  errno = 0;
  nValue = strtol(value, &endp, 10);
  
  if ( *endp != '\0' || errno == ERANGE || nValue < 1 || nValue > maxVal ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", reserved file "
			"descriptors must range from 1 to %d (the current process maxdescriptors limit).  "
			"Server will use a setting of %d.", attrname, value, maxVal, maxVal );
        if ( nValue > maxVal) {
            nValue = maxVal;
            retVal = LDAP_UNWILLING_TO_PERFORM;
        } else {
	    retVal = LDAP_OPERATIONS_ERROR;
        }
  }
  
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->reservedescriptors = nValue;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}



int
config_set_ioblocktimeout( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nValue = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  nValue = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || nValue < 0 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", I/O block timeout must range from 0 to %ld",
                      attrname, value, LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

#if defined(IRIX)
  /* on IRIX poll can only handle timeouts up to
	 2147483 without failing, cap it at 30 minutes */

  if ( nValue >  SLAPD_DEFAULT_IOBLOCK_TIMEOUT ) {
	nValue = SLAPD_DEFAULT_IOBLOCK_TIMEOUT;
  }
#endif /* IRIX */

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->ioblocktimeout = nValue;
	/*	g_ioblock_timeout= nValue; */
	CFG_UNLOCK_WRITE(slapdFrontendConfig);	
  }
  return retVal;

}


int
config_set_idletimeout( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nValue = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  nValue = strtol(value, &endp, 10);

  if (*endp != '\0' || errno == ERANGE || nValue < 0 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", idle timeout must range from 0 to %ld",
                      attrname, value, LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->idletimeout = nValue;
	/*	g_idle_timeout= nValue; */
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}


int 
config_set_groupevalnestlevel( const char *attrname, char * value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nValue = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  nValue = strtol(value, &endp, 10);
  
  if ( *endp != '\0' || errno == ERANGE || nValue < 0 || nValue > 5 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "%s: invalid value \"%s\", group eval nest level must range from 0 to 5",
			  attrname, value );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->groupevalnestlevel = nValue;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;

}

int
config_set_defaultreferral( const char *attrname, struct berval **value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, (char *)value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	g_set_default_referral( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_userat( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free( (void **) &(slapdFrontendConfig->userat) );
	slapdFrontendConfig->userat = slapi_ch_strdup(value);
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int 
config_set_timelimit( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long nVal = 0;
  char *endp = NULL;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  Slapi_Backend *be = NULL;
  char *cookie;
  
  *errorbuf = 0;

  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  nVal = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || nVal < -1 ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid value \"%s\", time limit must range from -1 to %ld",
                         attrname, value, LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	g_set_deftime ( nVal );
	slapdFrontendConfig->timelimit = nVal;
	be = slapi_get_first_backend (&cookie);
	while (be) {
	  be->be_timelimit = slapdFrontendConfig->timelimit;
	  be = slapi_get_next_backend (cookie);
	}	
	CFG_UNLOCK_WRITE(slapdFrontendConfig);

	slapi_ch_free ((void **)&cookie);
  }
  return retVal;
}

int 
config_set_useroc( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &(slapdFrontendConfig->useroc) );
	slapdFrontendConfig->useroc = slapi_ch_strdup( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}


int
config_set_accesslog( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  retVal = log_update_accesslogdir ( value, apply );
  
  if ( retVal != LDAP_SUCCESS ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			"Cannot open accesslog directory \"%s\", client accesses will "
			"not be logged.", value );
  }
  
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &(slapdFrontendConfig->accesslog) );
	slapdFrontendConfig->accesslog = slapi_ch_strdup ( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_errorlog( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
    return LDAP_OPERATIONS_ERROR;
  }
  
  retVal = log_update_errorlogdir ( value, apply );
  
  if ( retVal != LDAP_SUCCESS ) {
    PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
      "Cannot open errorlog file \"%s\", errors cannot be logged.  Exiting...",
      value );
    syslog(LOG_ERR, 
      "Cannot open errorlog file \"%s\", errors cannot be logged.  Exiting...",
      value );
    g_set_shutdown( SLAPI_SHUTDOWN_EXIT );
  }
  
  if ( apply ) {
    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapi_ch_free ( (void **) &(slapdFrontendConfig->errorlog) );
    slapdFrontendConfig->errorlog = slapi_ch_strdup ( value );
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_auditlog( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  retVal = log_update_auditlogdir ( value, apply );
  
  if ( retVal != LDAP_SUCCESS ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			"Cannot open auditlog directory \"%s\"", value );
  }
  
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free ( (void **) &(slapdFrontendConfig->auditlog) );
	slapdFrontendConfig->auditlog = slapi_ch_strdup ( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

int
config_set_pw_maxage( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long age;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  /* age in seconds */
  age = parse_duration(value);

  if ( age <= 0 || age > (MAX_ALLOWED_TIME_IN_SECS - current_time()) ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "%s: password maximum age \"%s\" is invalid. ",
			  attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }
  
  if ( apply ) {
	slapdFrontendConfig->pw_policy.pw_maxage = age;
  }
  return retVal;
}

int
config_set_pw_minage( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long age;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  /* age in seconds */
  age = parse_duration(value);
  if ( age < 0 || age > (MAX_ALLOWED_TIME_IN_SECS - current_time()) ) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			  "%s: password minimum age \"%s\" is invalid. ",
			  attrname, value );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }

  if ( apply ) {
	slapdFrontendConfig->pw_policy.pw_minage = age;
  }
  return retVal;
}

int
config_set_pw_warning( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long sec;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  errno = 0;
  /* in seconds */
  sec = parse_duration(value);

  if (errno == ERANGE || sec < 0) {
	PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, 
			   "%s: password warning age \"%s\" is invalid, password warning "
			   "age must range from 0 to %ld seconds", 
			   attrname, value, LONG_MAX );
	retVal = LDAP_OPERATIONS_ERROR;
	return retVal;
  }
  /* translate to seconds */
  if ( apply ) {
	slapdFrontendConfig->pw_policy.pw_warning = sec;
  }
  return retVal;
}



int
config_set_errorlog_level( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long level = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  level = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || level < 0 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: error log level \"%s\" is invalid,"
                      " error log level must range from 0 to %ld", attrname, value, LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }
  
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	level |= LDAP_DEBUG_ANY;

#ifdef _WIN32
	*module_ldap_debug = level;
#else
	slapd_ldap_debug = level;
#endif
	slapdFrontendConfig->errorloglevel = level;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}


int
config_set_accesslog_level( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal = LDAP_SUCCESS;
  long level = 0;
  char *endp = NULL;

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
	return LDAP_OPERATIONS_ERROR;
  }

  errno = 0;
  level = strtol(value, &endp, 10);

  if ( *endp != '\0' || errno == ERANGE || level < 0 ) {
        PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: access log level \"%s\" is invalid,"
                      " access log level must range from 0 to %ld", attrname, value, LONG_MAX );
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
  }
  
  if ( apply ) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	g_set_accesslog_level ( level );
	slapdFrontendConfig->accessloglevel = level;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return retVal;
}

/* set the referral-mode url (which puts us into referral mode) */
int config_set_referral_mode(const char *attrname, char *url, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	slapdFrontendConfig->refer_mode=REFER_MODE_OFF;

    if ((!url) || (!url[0])) {
	strcpy(errorbuf, "referral url must have a value");
	return LDAP_OPERATIONS_ERROR;
    }
    if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->refer_url = slapi_ch_strdup(url);
	slapdFrontendConfig->refer_mode = REFER_MODE_ON;
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return LDAP_SUCCESS;
}

int
config_set_versionstring( const char *attrname, char *version, char *errorbuf, int apply ) {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ((!version) || (!version[0])) {
	PL_strncpyz(errorbuf, "versionstring must have a value", SLAPI_DSE_RETURNTEXT_SIZE);
	return LDAP_OPERATIONS_ERROR;
  }
  if (apply) {
	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapdFrontendConfig->versionstring = slapi_ch_strdup(version);
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  return LDAP_SUCCESS;
}




#define config_copy_strval( s ) s ? slapi_ch_strdup (s) : NULL;

int
config_get_port(){
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->port;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;

}

int
config_get_sasl_maxbufsize()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->sasl_max_bufsize;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_disk_monitoring(){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_monitoring;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_disk_logging_critical(){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_logging_critical;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_disk_grace_period(){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_grace_period;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

PRUint64
config_get_disk_threshold(){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_threshold;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_ldapi_filename(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_filename);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}


int config_get_ldapi_switch(){   
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig(); 
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->ldapi_switch;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int config_get_ldapi_bind_switch(){
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->ldapi_bind_switch;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *config_get_ldapi_root_dn(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_root_dn);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int config_get_ldapi_map_entries(){
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->ldapi_map_entries;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *config_get_ldapi_uidnumber_type(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_uidnumber_type);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *config_get_ldapi_gidnumber_type(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_gidnumber_type);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *config_get_ldapi_search_base_dn(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_search_base_dn);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

#if defined(ENABLE_AUTO_DN_SUFFIX)
char *config_get_ldapi_auto_dn_suffix(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_auto_dn_suffix);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}
#endif


char *config_get_anon_limits_dn(){
  char *retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->anon_limits_dn);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int config_get_slapi_counters()
{   
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig(); 
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->slapi_counters;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *
config_get_workingdir() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->workingdir);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char *
config_get_versionstring() {

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapi_ch_strdup(slapdFrontendConfig->versionstring);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
  
}
  

char *
config_get_buildnum(void)
{
    return slapi_ch_strdup(BUILD_NUM);
}

int
config_get_secureport() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->secureport;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}
						  

int 
config_get_SSLclientAuth() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->SSLclientAuth;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}


int
config_get_ssl_check_hostname()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->ssl_check_hostname;
}


char *
config_get_localhost() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval ( slapdFrontendConfig->localhost );
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;

} 

char *
config_get_listenhost() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
 
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval ( slapdFrontendConfig->listenhost );
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

char *
config_get_securelistenhost() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval( slapdFrontendConfig->securelistenhost );
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

char * 
config_get_srvtab() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->srvtab);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_sizelimit() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
	
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->sizelimit;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int
config_get_pagedsizelimit() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
	
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pagedsizelimit;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

char *
config_get_pw_storagescheme() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal = 0;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->pw_storagescheme->pws_name);
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}
	

int
config_get_pw_change() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_change;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}


int
config_get_pw_history() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_history;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}



int
config_get_pw_must_change() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_must_change;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}


int
config_get_pw_syntax() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_syntax;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}



int
config_get_pw_minlength() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minlength;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_mindigits() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_mindigits;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_minalphas() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minalphas;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_minuppers() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minuppers;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_minlowers() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minlowers;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_minspecials() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minspecials;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_min8bit() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_min8bit;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_maxrepeats() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_maxrepeats;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_mincategories() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_mincategories;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_mintokenlength() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_mintokenlength;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int 
config_get_pw_maxfailure() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_maxfailure;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  return retVal;

}

int
config_get_pw_inhistory() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_inhistory;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 
}

long
config_get_pw_lockduration() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  long retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_lockduration;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 

}

long
config_get_pw_resetfailurecount() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  long retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_resetfailurecount;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_is_global_policy() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_is_global_policy;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int
config_get_pw_is_legacy_policy() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_is_legacy;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_exp() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_exp;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}


int
config_get_pw_unlock() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_unlock;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 
}

int
config_get_pw_lockout(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_lockout;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_pw_gracelimit() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal=0;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_gracelimit;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 

}

int
config_get_lastmod(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->lastmod;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int
config_get_enquote_sup_oc(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->enquote_sup_oc;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int
config_get_nagle() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->nagle;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  return retVal;
}

int
config_get_accesscontrol() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->accesscontrol;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

int
config_get_return_exact_case() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  retVal = slapdFrontendConfig->return_exact_case;

  return retVal; 
}

int
config_get_result_tweak() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->result_tweak;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int
config_get_security() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->security;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}
			 
int
slapi_config_get_readonly() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->readonly;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_schemacheck() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->schemacheck;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

int
config_get_syntaxcheck() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->syntaxcheck;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_syntaxlogging() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->syntaxlogging;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_dn_validate_strict() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->dn_validate_strict;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_ds4_compatible_schema() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->ds4_compatible_schema;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

int
config_get_schema_ignore_trailing_spaces() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->schema_ignore_trailing_spaces;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal;
}

char *
config_get_rootdn() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval (slapdFrontendConfig->rootdn);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

char * slapi_get_rootdn() {
	return config_get_rootdn();
}

char *
config_get_rootpw() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval (slapdFrontendConfig->rootpw);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}


char *
config_get_rootpwstoragescheme() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
	
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->rootpwstoragescheme->pws_name);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

#ifndef _WIN32

char *
config_get_localuser() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->localuser);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

#endif /* _WIN32 */
/* alias of encryption key and certificate files is now retrieved through */
/* calls to psetFullCreate() and psetGetAttrSingleValue(). See ssl.c, */
/* where this function is still used to set the global variable */
char *
config_get_encryptionalias() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->encryptionalias);
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 
}

int
config_get_threadnumber() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->threadnumber;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_maxthreadsperconn(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->maxthreadsperconn;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

#if !defined(_WIN32) && !defined(AIX)
int
config_get_maxdescriptors() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->maxdescriptors;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  return retVal; 
}

#endif /* !_WIN32 && !AIX */

int
config_get_reservedescriptors(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->reservedescriptors;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 
}

int
config_get_ioblocktimeout(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->ioblocktimeout;
  CFG_UNLOCK_READ(slapdFrontendConfig);	

  return retVal;
}

int
config_get_idletimeout(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->idletimeout;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}


int
config_get_groupevalnestlevel(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->groupevalnestlevel;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

struct berval **
config_get_defaultreferral() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  struct berval **refs;
  int nReferrals = 0;

  CFG_LOCK_READ(slapdFrontendConfig);
  /* count the number of referrals */
  for ( nReferrals = 0; 
		slapdFrontendConfig->defaultreferral && 
		  slapdFrontendConfig->defaultreferral[nReferrals];
		nReferrals++)
	;
  
  refs = (struct berval **) 
	slapi_ch_malloc( (nReferrals + 1) * sizeof(struct berval *) );
  
  /*terminate the end, and add the referrals backwards */
  refs [nReferrals--] = NULL;

  while ( nReferrals >= 0 ) {
	refs[nReferrals] = (struct berval *) slapi_ch_malloc( sizeof(struct berval) );
	refs[nReferrals]->bv_val = 
	  config_copy_strval( slapdFrontendConfig->defaultreferral[nReferrals]->bv_val );
	refs[nReferrals]->bv_len =  slapdFrontendConfig->defaultreferral[nReferrals]->bv_len;
	nReferrals--;
  }
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return refs;
}

char *
config_get_userat (  ){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval( slapdFrontendConfig->userat );
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int 
config_get_timelimit(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal= slapdFrontendConfig->timelimit;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

char* 
config_get_useroc(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_WRITE(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->useroc );
  CFG_UNLOCK_WRITE(slapdFrontendConfig);

  return retVal; 
}

char *
config_get_accesslog(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->accesslog);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

char *
config_get_errorlog(  ){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->errorlog);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;

}

char *
config_get_auditlog(  ){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval(slapdFrontendConfig->auditlog);
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

long
config_get_pw_maxage() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  long retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_maxage;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  return retVal; 
}

long
config_get_pw_minage(){

  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  long retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_minage;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

long
config_get_pw_warning() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  long retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->pw_policy.pw_warning;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_get_errorlog_level(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->errorloglevel;
  CFG_UNLOCK_READ(slapdFrontendConfig);
  
  return retVal; 
}

/*  return integer -- don't worry about locking similar to config_check_referral_mode 
    below */

int
config_get_accesslog_level(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  retVal = slapdFrontendConfig->accessloglevel;

  return retVal;
}

/*  return integer -- don't worry about locking similar to config_check_referral_mode 
    below */

int
config_get_auditlog_logging_enabled(){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  retVal = slapdFrontendConfig->auditlog_logging_enabled;

  return retVal;
}

int
config_get_accesslog_logging_enabled(){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->accesslog_logging_enabled;

    return retVal;
}

char *config_get_referral_mode(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *ret;

    CFG_LOCK_READ(slapdFrontendConfig);
    ret = config_copy_strval(slapdFrontendConfig->refer_url);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return ret;
}

int
config_get_conntablesize(void){
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;
  
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->conntablesize;
  CFG_UNLOCK_READ(slapdFrontendConfig);	

  return retVal;
}

/* return yes/no without actually copying the referral url
   we don't worry about another thread changing this value
   since we now return an integer */
int config_check_referral_mode(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return(slapdFrontendConfig->refer_mode & REFER_MODE_ON);
}

int
config_get_outbound_ldap_io_timeout(void)
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = slapdFrontendConfig->outbound_ldap_io_timeout;
	CFG_UNLOCK_READ(slapdFrontendConfig);
	return retVal; 
}

int
config_get_unauth_binds_switch(void)
{
	int retVal;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = slapdFrontendConfig->allow_unauth_binds;
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal;
}

int
config_get_require_secure_binds(void)
{
	int retVal;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = slapdFrontendConfig->require_secure_binds;
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal;
}

int
config_get_anon_access_switch(void)
{
	int retVal = 0;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	CFG_LOCK_READ(slapdFrontendConfig);
        retVal = slapdFrontendConfig->allow_anon_access;
	CFG_UNLOCK_READ(slapdFrontendConfig);
	return retVal;
}

int
config_get_validate_cert_switch(void)
{
	int retVal = 0;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = slapdFrontendConfig->validate_cert;
	CFG_UNLOCK_READ(slapdFrontendConfig);
	return retVal;
}

int
config_set_maxbersize( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  if ( !apply ) {
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);

  slapdFrontendConfig->maxbersize = atoi(value);
  
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  return retVal;
}

ber_len_t
config_get_maxbersize()
{
    ber_len_t maxbersize;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    maxbersize = slapdFrontendConfig->maxbersize;
    if(maxbersize==0)
        maxbersize= 2 * 1024 * 1024; /* Default: 2Mb */
    return maxbersize;

}

int
config_set_maxsasliosize( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal =  LDAP_SUCCESS;
  long maxsasliosize;
  char *endptr;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  maxsasliosize = strtol(value, &endptr, 10);

  /* Check for non-numeric garbage in the value */
  if (*endptr != '\0') {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* Check for a value overflow */
  if (((maxsasliosize == LONG_MAX) || (maxsasliosize == LONG_MIN)) && (errno == ERANGE)){
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* A setting of -1 means unlimited.  Don't allow other negative values. */
  if ((maxsasliosize < 0) && (maxsasliosize != -1)) {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  if (retVal != LDAP_SUCCESS) {
    PR_snprintf(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                 "%s: \"%s\" is invalid. Value must range from -1 to %ld",
                 attrname, value, LONG_MAX );
  } else if (apply) {
    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapdFrontendConfig->maxsasliosize = maxsasliosize;
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

size_t
config_get_maxsasliosize()
{
  size_t maxsasliosize;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  maxsasliosize = slapdFrontendConfig->maxsasliosize;

  return maxsasliosize;
}

int
config_set_localssf( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal =  LDAP_SUCCESS;
  int localssf;
  char *endptr;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  localssf = (int) strtol(value, &endptr, 10);

  /* Check for non-numeric garbage in the value */
  if (*endptr != '\0') {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* Check for a value overflow */
  if (((localssf == INT_MAX) || (localssf == INT_MIN)) && (errno == ERANGE)){
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* Don't allow negative values. */
  if (localssf < 0) {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  if (retVal != LDAP_SUCCESS) {
    PR_snprintf(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                 "%s: \"%s\" is invalid. Value must range from 0 to %d",
                 attrname, value, INT_MAX );
  } else if (apply) {
    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapdFrontendConfig->localssf = localssf;
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_minssf( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal =  LDAP_SUCCESS;
  int minssf;
  char *endptr;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
        return LDAP_OPERATIONS_ERROR;
  }

  minssf = (int) strtol(value, &endptr, 10);

  /* Check for non-numeric garbage in the value */
  if (*endptr != '\0') {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* Check for a value overflow */
  if (((minssf == INT_MAX) || (minssf == INT_MIN)) && (errno == ERANGE)){
    retVal = LDAP_OPERATIONS_ERROR;
  }

  /* Don't allow negative values. */
  if (minssf < 0) {
    retVal = LDAP_OPERATIONS_ERROR;
  }

  if (retVal != LDAP_SUCCESS) {
    PR_snprintf(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                 "%s: \"%s\" is invalid. Value must range from 0 to %d",
                 attrname, value, INT_MAX );
  } else if (apply) {
    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapdFrontendConfig->minssf = minssf;
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }

  return retVal;
}

int
config_set_minssf_exclude_rootdse( const char *attrname, char *value,
                                   char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff ( attrname,
                              value, 
                              &(slapdFrontendConfig->minssf_exclude_rootdse),
                              errorbuf,
                              apply );
  
  return retVal;
}

int
config_get_localssf()
{
  int localssf;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  localssf = slapdFrontendConfig->localssf;

  return localssf;
}

int
config_get_minssf()
{
  int minssf;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  minssf = slapdFrontendConfig->minssf;

  return minssf;
}

int
config_get_minssf_exclude_rootdse()
{
  int retVal;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = slapdFrontendConfig->minssf_exclude_rootdse;
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal;
}

int
config_set_max_filter_nest_level( const char *attrname, char *value,
		char *errorbuf, int apply )
{
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  if ( !apply ) {
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);
  slapdFrontendConfig->max_filter_nest_level = atoi(value);
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  return retVal;
}

int
config_get_max_filter_nest_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	int	retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->max_filter_nest_level;
	CFG_UNLOCK_READ(slapdFrontendConfig);
	return retVal;
}


char *
config_get_basedn() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  char *retVal;

  CFG_LOCK_READ(slapdFrontendConfig);
  retVal = config_copy_strval ( slapdFrontendConfig->certmap_basedn );
  CFG_UNLOCK_READ(slapdFrontendConfig);

  return retVal; 
}

int 
config_set_basedn ( const char *attrname, char *value, char *errorbuf, int apply ) {
  int retVal =  LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	return LDAP_OPERATIONS_ERROR;
  }
  
  if ( !apply ) {
	return retVal;
  }

  CFG_LOCK_WRITE(slapdFrontendConfig);
  slapi_ch_free ( (void **) &slapdFrontendConfig->certmap_basedn );

  slapdFrontendConfig->certmap_basedn = slapi_dn_normalize( slapi_ch_strdup(value) );
  
  CFG_UNLOCK_WRITE(slapdFrontendConfig);
  return retVal;
}

char *
config_get_configdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->configdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_configdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->configdir);

	slapdFrontendConfig->configdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_instancedir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->instancedir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_instancedir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
   
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
   
	if (!apply) {
		return retVal;
	}
 
	CFG_LOCK_WRITE(slapdFrontendConfig);
	/* We don't want to allow users to modify instance dir.
	 * Set it once when the server starts. */
	if (NULL == slapdFrontendConfig->instancedir) {
		slapdFrontendConfig->instancedir = slapi_ch_strdup(value);
	}
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_schemadir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->schemadir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_schemadir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->schemadir);

	slapdFrontendConfig->schemadir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_lockdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->lockdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_lockdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->lockdir);

	slapdFrontendConfig->lockdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_tmpdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->tmpdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_tmpdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->tmpdir);

	slapdFrontendConfig->tmpdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_certdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->certdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_certdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->certdir);

	slapdFrontendConfig->certdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_ldifdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->ldifdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_ldifdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->ldifdir);

	slapdFrontendConfig->ldifdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_bakdir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->bakdir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_bakdir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->bakdir);

	slapdFrontendConfig->bakdir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_rundir()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->rundir);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal; 
}

int
config_set_rundir(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
  
	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->rundir);

	slapdFrontendConfig->rundir = slapi_ch_strdup(value);
  
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char *
config_get_saslpath()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	char *retVal;

	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = config_copy_strval(slapdFrontendConfig->saslpath);
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal;
}

int
config_set_saslpath(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}

	if (!apply) {
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free((void **)&slapdFrontendConfig->saslpath);

	slapdFrontendConfig->saslpath = slapi_ch_strdup(value);

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

char **
config_get_errorlog_list()
{
	return log_get_loglist(SLAPD_ERROR_LOG);
}

char **
config_get_accesslog_list()
{
	return log_get_loglist(SLAPD_ACCESS_LOG);
}

char **
config_get_auditlog_list()
{
	return log_get_loglist(SLAPD_AUDIT_LOG);
}

int
config_set_accesslogbuffering(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	retVal = config_set_onoff(attrname,
							  value, 
							  &(slapdFrontendConfig->accesslogbuffering),
							  errorbuf,
							  apply);
  
	return retVal;
}

#ifdef MEMPOOL_EXPERIMENTAL
int
config_set_mempool_switch( const char *attrname, char *value, char *errorbuf, int apply ) {
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->mempool_switch),
		errorbuf,
		apply);

	return retVal;
}

int
config_get_mempool_switch()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->mempool_switch;
}

int
config_set_mempool_maxfreelist( const char *attrname, char *value, char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	char *endp = NULL;
	int maxfreelist;

	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}
	errno = 0;
	maxfreelist = strtol(value, &endp, 10);
	if (0 != errno ) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ( apply ) {
		CFG_LOCK_WRITE(slapdFrontendConfig);

		slapdFrontendConfig->mempool_maxfreelist = maxfreelist;
	
		CFG_UNLOCK_WRITE(slapdFrontendConfig);
	}
	
	return retVal;
}

int
config_get_mempool_maxfreelist()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->mempool_maxfreelist;
}

long
config_get_system_page_size()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->system_page_size;
}

int
config_get_system_page_bits()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->system_page_bits;
}
#endif /* MEMPOOL_EXPERIMENTAL */

int
config_set_csnlogging(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	retVal = config_set_onoff(attrname,
							  value, 
							  &(slapdFrontendConfig->csnlogging),
							  errorbuf,
							  apply);
  
	return retVal;
}

int
config_get_csnlogging()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->csnlogging;
}

int
config_set_attrname_exceptions(const char *attrname, char *value, char *errorbuf, int apply)
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
	retVal = config_set_onoff(attrname,
							  value, 
							  &(slapdFrontendConfig->attrname_exceptions),
							  errorbuf,
							  apply);
  
	return retVal;
}

int
config_get_attrname_exceptions()
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	return slapdFrontendConfig->attrname_exceptions;
}

int
config_set_hash_filters(const char *attrname, char *value, char *errorbuf, int apply)
{
	int val = 0;
	int retVal = LDAP_SUCCESS;
  
	retVal = config_set_onoff(attrname,
							  value, 
							  &val,
							  errorbuf,
							  apply);
  
	if (retVal == LDAP_SUCCESS) {
		set_hash_filters(val);
	}

	return retVal;
}

int
config_get_hash_filters()
{
	return 0; /* for now */
}

int
config_set_rewrite_rfc1274(const char *attrname, char *value, char *errorbuf, int apply)
{
  int retVal = LDAP_SUCCESS;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

  retVal = config_set_onoff(attrname,
							value, 
						    &(slapdFrontendConfig->rewrite_rfc1274),
						    errorbuf,
						    apply);
  
  return retVal;
}


/* we don't worry about another thread changing this flag since it is an
   integer */
int
config_get_rewrite_rfc1274()
{
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  retVal = slapdFrontendConfig->rewrite_rfc1274;
  return retVal; 
}


static int 
config_set_schemareplace( const char *attrname, char *value, char *errorbuf, int apply )
{
  int retVal = LDAP_SUCCESS;
	
  if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
	retVal = LDAP_OPERATIONS_ERROR;
  } else {
	/*
	 * check that the value is one we allow.
	 */
	if ( 0 != strcasecmp( value, CONFIG_SCHEMAREPLACE_STR_OFF ) && 
			0 != strcasecmp( value, CONFIG_SCHEMAREPLACE_STR_ON ) && 
			0 != strcasecmp( value, CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY )) {
		retVal = LDAP_OPERATIONS_ERROR;
		if( errorbuf ) {
			PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "unsupported value: %s", value );
		}
	}
  }

  if ( LDAP_SUCCESS == retVal && apply ) {
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	CFG_LOCK_WRITE(slapdFrontendConfig);
	slapi_ch_free( (void **)&slapdFrontendConfig->schemareplace );
	slapdFrontendConfig->schemareplace = slapi_ch_strdup( value );
	CFG_UNLOCK_WRITE(slapdFrontendConfig);
  }
  
  return retVal;
}


int
config_set_outbound_ldap_io_timeout( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ( apply ) {
		CFG_LOCK_WRITE(slapdFrontendConfig);
		slapdFrontendConfig->outbound_ldap_io_timeout = atoi( value );
		CFG_UNLOCK_WRITE(slapdFrontendConfig);	
	}
	return LDAP_SUCCESS;
}


int
config_set_unauth_binds_switch( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->allow_unauth_binds),
		errorbuf,
		apply);

	return retVal;
}

int
config_set_require_secure_binds( const char *attrname, char *value,
                char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->require_secure_binds),
		errorbuf,
		apply);

	return retVal;
}

int
config_set_anon_access_switch( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	if (config_value_is_null(attrname, value, errorbuf, 0)) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ((strcasecmp(value, "on") != 0) && (strcasecmp(value, "off") != 0) &&
	    (strcasecmp(value, "rootdse") != 0)) {
		PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid value \"%s\". Valid values are \"on\", "
			"\"off\", or \"rootdse\".", attrname, value);
		retVal = LDAP_OPERATIONS_ERROR;
	}

	if (!apply) {
		/* we can return now if we aren't applying the changes */
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);

	if (strcasecmp(value, "on") == 0 ) {
		slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_ON;
	} else if (strcasecmp(value, "off") == 0 ) {
		slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_OFF;
	} else if (strcasecmp(value, "rootdse") == 0) {
		slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_ROOTDSE;
	}

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

int
config_set_validate_cert_switch( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	if (config_value_is_null(attrname, value, errorbuf, 0)) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ((strcasecmp(value, "on") != 0) && (strcasecmp(value, "off") != 0) &&
	    (strcasecmp(value, "warn") != 0)) {
		PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
			"%s: invalid value \"%s\". Valid values are \"on\", "
			"\"off\", or \"warn\".", attrname, value);
		retVal = LDAP_OPERATIONS_ERROR;
	}

	if (!apply) {
		/* we can return now if we aren't applying the changes */
		return retVal;
	}

	CFG_LOCK_WRITE(slapdFrontendConfig);

	if (strcasecmp(value, "on") == 0 ) {
		slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_ON;
	} else if (strcasecmp(value, "off") == 0 ) {
		slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_OFF;
	} else if (strcasecmp(value, "warn") == 0) {
		slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_WARN;
	}

	CFG_UNLOCK_WRITE(slapdFrontendConfig);
	return retVal;
}

int
config_get_force_sasl_external(void)
{
	int retVal;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	CFG_LOCK_READ(slapdFrontendConfig);
	retVal = slapdFrontendConfig->force_sasl_external;
	CFG_UNLOCK_READ(slapdFrontendConfig);

	return retVal;
}

int
config_set_force_sasl_external( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	int retVal = LDAP_SUCCESS;
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

	retVal = config_set_onoff(attrname,
		value,
		&(slapdFrontendConfig->force_sasl_external),
		errorbuf,
		apply);

	return retVal;
}

int
config_get_entryusn_global(void)
{
    int retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->entryusn_global;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_set_entryusn_global( const char *attrname, char *value,
                            char *errorbuf, int apply )
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->entryusn_global),
                              errorbuf, apply);
    return retVal;
}

char *
config_get_entryusn_import_init(void)
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->entryusn_import_init);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int
config_set_entryusn_import_init( const char *attrname, char *value,
                                 char *errorbuf, int apply )
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&(slapdFrontendConfig->entryusn_import_init));
        slapdFrontendConfig->entryusn_import_init = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

char *
config_get_allowed_to_delete_attrs(void)
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->allowed_to_delete_attrs);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int
config_set_allowed_to_delete_attrs( const char *attrname, char *value,
                                    char *errorbuf, int apply )
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if ( config_value_is_null( attrname, value, errorbuf, 1 )) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&(slapdFrontendConfig->allowed_to_delete_attrs));
        slapdFrontendConfig->allowed_to_delete_attrs = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

char *
config_get_default_naming_context(void)
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->default_naming_context;
    CFG_UNLOCK_READ(slapdFrontendConfig);
  
    return retVal;
}

int
config_set_default_naming_context(const char *attrname, 
                                  char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    void     *node;
    Slapi_DN *sdn;
    char     *suffix = NULL;

    if (value && *value) {
        int in_init = 0;
        suffix = slapi_create_dn_string("%s", value);
        if (NULL == suffix) {
            if (errorbuf) {
                PR_snprintf (errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                             "%s is not a valid suffix.", value);
            }
            return LDAP_INVALID_DN_SYNTAX;
        }
        sdn = slapi_get_first_suffix(&node, 0);
        if (NULL == sdn) {
            in_init = 1; /* at the startup time, no suffix is set yet */
        }
        while (sdn) {
            if (0 == strcasecmp(suffix, slapi_sdn_get_dn(sdn))) {
                /* matched */
                break;
            }
            sdn = slapi_get_next_suffix(&node, 0);
        }
        if (!in_init && (NULL == sdn)) { /* not in startup && no match */
            if (errorbuf) {
                PR_snprintf (errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                             "%s is not an existing suffix.", value);
            }
            slapi_ch_free_string(&suffix);
            return LDAP_NO_SUCH_OBJECT;
        }
    } else {
        /* reset */
        suffix = NULL;
    }

    if (!apply) {
        slapi_ch_free_string(&suffix);
        return LDAP_SUCCESS;
    }

    if (errorbuf) {
        *errorbuf = '\0';
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&slapdFrontendConfig->default_naming_context);
        /* normalized suffix*/
        slapdFrontendConfig->default_naming_context = suffix;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return LDAP_SUCCESS;
}

int
config_set_listen_backlog_size( const char *attrname, char *value,
		char *errorbuf, int apply )
{
	slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
	
	if ( config_value_is_null( attrname, value, errorbuf, 0 )) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ( apply ) {
    		slapdFrontendConfig->listen_backlog_size = atoi(value);
	}
	return LDAP_SUCCESS;
}

int
config_get_listen_backlog_size()
{
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int retVal;

  retVal = slapdFrontendConfig->listen_backlog_size;
  return retVal; 
}

/*
 * This function is intended to be used from the dse code modify callback.  It
 * is "optimized" for that case because it takes a berval** of values, which is
 * currently what is used by ldapmod to hold the values.  We could easily switch
 * this to take a Slapi_Value array or even a Slapi_Attr.  Most config params
 * have simple config_set_XXX functions which take a char* argument holding the
 * value.  The log_set_XXX functions have an additional parameter which
 * discriminates the log to use.  The config parameters with types CONFIG_SPECIAL_XXX
 * require special handling to set their values.
 */
int
config_set(const char *attr, struct berval **values, char *errorbuf, int apply)
{
	int ii = 0;
	int retval = LDAP_SUCCESS;
	struct config_get_and_set *cgas = 0;
	cgas = (struct config_get_and_set *)PL_HashTableLookup(confighash, attr);
	if (!cgas)
	{
#if 0
		debugHashTable(attr);
#endif
		PR_snprintf ( errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown attribute %s will be ignored", attr);
		slapi_log_error(SLAPI_LOG_FATAL, "config", "%s\n", errorbuf);
		return LDAP_NO_SUCH_ATTRIBUTE;
	}

	switch (cgas->config_var_type)
	{
	case CONFIG_SPECIAL_REFERRALLIST:
		if (NULL == values) /* special token which means to remove referrals */
		{
			struct berval val;
			struct berval *vals[2] = {0, 0};
			vals[0] = &val;
			val.bv_val = REFERRAL_REMOVE_CMD;
			val.bv_len = strlen(REFERRAL_REMOVE_CMD);
			retval = config_set_defaultreferral(attr, vals, errorbuf, apply);
		}
		else
		{
			retval = config_set_defaultreferral(attr, values, errorbuf, apply);
		}
		break;

	default:
		if ((NULL == values) &&
			config_allowed_to_delete_attrs(cgas->attr_name)) {
			if (cgas->setfunc) {
				retval = (cgas->setfunc)(cgas->attr_name, NULL,
				                         errorbuf, apply);
			} else if (cgas->logsetfunc) {
				retval = (cgas->logsetfunc)(cgas->attr_name, NULL,
				                            cgas->whichlog, errorbuf, apply);
			} else {
				LDAPDebug1Arg(LDAP_DEBUG_ANY, 
				              "config_set: the attribute %s is read only; "
				              "ignoring setting NULL value\n", attr);
			}
		}
		for (ii = 0; !retval && values && values[ii]; ++ii)
		{
			if (cgas->setfunc)
				retval = (cgas->setfunc)(cgas->attr_name,
							(char *)values[ii]->bv_val, errorbuf, apply);
			else if (cgas->logsetfunc)
				retval = (cgas->logsetfunc)(cgas->attr_name,
							(char *)values[ii]->bv_val, cgas->whichlog,
							errorbuf, apply);
			else
				LDAPDebug(LDAP_DEBUG_ANY, 
						  "config_set: the attribute %s is read only; ignoring new value %s\n",
						  attr, values[ii]->bv_val, 0);
		}
		break;
	} 

	return retval;
}

static void
config_set_value(
    Slapi_Entry *e, 
    struct config_get_and_set *cgas,
    void **value
)
{
    struct berval **values = 0;
    char *sval = 0;
    int ival = 0;
    uintptr_t pval;

    switch (cgas->config_var_type) {
    case CONFIG_ON_OFF: /* convert 0,1 to "off","on" */
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((int *)value)) ? "on" : "off");
        break;

    case CONFIG_INT:
        if (value)
            slapi_entry_attr_set_int(e, cgas->attr_name, *((int *)value));
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;

    case CONFIG_LONG:
        if (value)
            slapi_entry_attr_set_long(e, cgas->attr_name, *((long *)value));
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;

    case CONFIG_STRING:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ?
                                     *((char **)value) : "");
        break;

    case CONFIG_CHARRAY:
        if (value) {
            values = strarray2bervalarray((const char **)*((char ***)value));
            if (!values) {
                slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
            } else {
                slapi_entry_attr_replace(e, cgas->attr_name, values);
                bervalarray_free(values);
            }
        } else {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        }
        break;

    case CONFIG_SPECIAL_REFERRALLIST:
        /* referral list is already an array of berval* */
        if (value)
            slapi_entry_attr_replace(e, cgas->attr_name, (struct berval**)*value);
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;

    case CONFIG_CONSTANT_STRING:
        PR_ASSERT(value); /* should be a constant value */
        slapi_entry_attr_set_charptr(e, cgas->attr_name, (char*)value);
        break;

    case CONFIG_CONSTANT_INT:
        PR_ASSERT(value); /* should be a constant value */
        pval = (uintptr_t)value;
        ival = (int)pval;
        slapi_entry_attr_set_int(e, cgas->attr_name, ival);
        break;

    case CONFIG_SPECIAL_SSLCLIENTAUTH:
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "off");
            break;
        }

        if (*((int *)value) == SLAPD_SSLCLIENTAUTH_ALLOWED) {
            sval = "allowed";
        } else if (*((int *)value) == SLAPD_SSLCLIENTAUTH_REQUIRED) {
            sval = "required";
        } else {
            sval = "off";
        }
        slapi_entry_attr_set_charptr(e, cgas->attr_name, sval);
        break;

    case CONFIG_STRING_OR_OFF:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ?
                                     *((char **)value) : "off");
        break;

    case CONFIG_STRING_OR_EMPTY:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ?
                                     *((char **)value) : "");
        break;

    case CONFIG_STRING_OR_UNKNOWN:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ?
                                     *((char **)value) : "unknown");
        break;

    case CONFIG_SPECIAL_ERRORLOGLEVEL:
        if (value) {
            int ival = *(int *)value;
            ival &= ~LDAP_DEBUG_ANY;
            slapi_entry_attr_set_int(e, cgas->attr_name, ival);
        }
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;

    case CONFIG_SPECIAL_ANON_ACCESS_SWITCH:
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "off");
            break;
        }

        if (*((int *)value) == SLAPD_ANON_ACCESS_ON) {
            sval = "on";
        } else if (*((int *)value) == SLAPD_ANON_ACCESS_ROOTDSE) {
            sval = "rootdse";
        } else {
            sval = "off";
        }
        slapi_entry_attr_set_charptr(e, cgas->attr_name, sval);
        break;

    case CONFIG_SPECIAL_VALIDATE_CERT_SWITCH:
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "off");
            break;
        }

        if (*((int *)value) == SLAPD_VALIDATE_CERT_ON) {
            sval = "on";
        } else if (*((int *)value) == SLAPD_VALIDATE_CERT_WARN) {
            sval = "warn";
        } else {
            sval = "off";
        }
        slapi_entry_attr_set_charptr(e, cgas->attr_name, sval);

        break;

    default:
        PR_ASSERT(0); /* something went horribly wrong . . . */
        break;
    }

    return;
}

/*
 * Fill in the given slapi_entry with the config attributes and values
 */
int
config_set_entry(Slapi_Entry *e)
{
    int ii = 0;
    int tablesize = sizeof(ConfigList)/sizeof(ConfigList[0]);
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /*
     * Avoid recursive calls to the readers/writer
     * lock as it causes deadlock under stress. Each
     * individual config get function acquires a read
     * lock where necessary.
     */

    /*
     * Pass 1: Values which do not have a get function.
     */

    CFG_LOCK_READ(slapdFrontendConfig);
    for (ii = 0; ii < tablesize; ++ii) {
        struct config_get_and_set *cgas = &ConfigList[ii];
        void **value = 0;

        PR_ASSERT(cgas);
        value = cgas->config_var_addr;
        PR_ASSERT(cgas->attr_name);

        /* Skip values handled in pass 2 */
        if (NULL == value && cgas->getfunc) {
            continue;
        }

        config_set_value(e, cgas, value);
    }
    CFG_UNLOCK_READ(slapdFrontendConfig);

    /*
     * Pass 2: Values which do have a get function.
     */
    for (ii = 0; ii < tablesize; ++ii) {
        struct config_get_and_set *cgas = &ConfigList[ii];
        int ival = 0;
        long lval = 0;
        void **value = NULL;
        void *alloc_val = NULL;
        int needs_free = 0;

        PR_ASSERT(cgas);
        value = cgas->config_var_addr;
        PR_ASSERT(cgas->attr_name);

        /* Skip values handled in pass 1 */
        if (NULL != value || cgas->getfunc == NULL) {
            continue;
        }

        /* must cast return of getfunc and store in variable of correct sized type */
        /* otherwise endianness problems will ensue */
        if (isInt(cgas->config_var_type)) {
            ival = (int)(intptr_t)(cgas->getfunc)();
            value = (void **)&ival; /* value must be address of int */
        } else if (cgas->config_var_type == CONFIG_LONG) {
            lval = (long)(intptr_t)(cgas->getfunc)();
            value = (void **)&lval; /* value must be address of long */
        } else {
            alloc_val = (cgas->getfunc)();
            value = &alloc_val; /* value must be address of pointer */
            needs_free = 1; /* get funcs must return alloc'd memory except for get
                               funcs which return a simple integral type e.g. int */
        }

        config_set_value(e, cgas, value);

        if (needs_free && value) { /* assumes memory allocated by slapi_ch_Xalloc */
            if (CONFIG_CHARRAY == cgas->config_var_type) {
                charray_free((char **)*value);
            } else if (CONFIG_SPECIAL_REFERRALLIST == cgas->config_var_type) {
                ber_bvecfree((struct berval **)*value);
            } else if ((CONFIG_CONSTANT_INT != cgas->config_var_type) && /* do not free constants */
                       (CONFIG_CONSTANT_STRING != cgas->config_var_type)) {
                slapi_ch_free(value);
            }
        }
    }

    return 1;
}

/* these attr types are allowed to delete */
int
config_allowed_to_delete_attrs(const char *attr_type)
{
	int rc = 0;
	if (attr_type) {
		char *delattrs = config_get_allowed_to_delete_attrs();
		char **allowed = slapi_str2charray_ext(delattrs, " ", 0);
		char **ap;
		for (ap = allowed; ap && *ap; ap++) {
			if (strcasecmp (attr_type, *ap) == 0) {
				rc = 1;
				break;
			}
		}
		slapi_ch_array_free(allowed);
		slapi_ch_free_string(&delattrs);
	}
	return rc;
}

void
config_set_accesslog_enabled(int value){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[BUFSIZ];

    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapdFrontendConfig->accesslog_logging_enabled = value;
    if(value){
        log_set_logging(CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_ACCESS_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_ACCESS_LOG, errorbuf, CONFIG_APPLY);
    }
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
}

void
config_set_auditlog_enabled(int value){
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[BUFSIZ];

    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapdFrontendConfig->auditlog_logging_enabled = value;
    if(value){
        log_set_logging(CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_AUDIT_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_AUDIT_LOG, errorbuf, CONFIG_APPLY);
    }
    CFG_UNLOCK_WRITE(slapdFrontendConfig);
}

char *
slapi_err2string(int result)
{
    /*
     *  If we are using openldap, then we can safely use ldap_err2string with
     *  positive and negative result codes.  MozLDAP's ldap_err2string can
     *  only handle positive result codes.
     */
#if defined (USE_OPENLDAP)
    return ldap_err2string(result);
#else
    if(result >= 0){
        return ldap_err2string(result);
    }
    switch (result)
    {
        case -1:
            return ("Can't contact LDAP server");
        case -2:
            return ("Local error");
        case -3:
            return ("Encoding error");
        case -4:
            return ("Decoding error");
        case -5:
            return ("Timed out");
        case -6:
            return ("Unknown authentication method");
        case -7:
            return ("Bad search filter");
        case -8:
            return ("User canceled operation");
        case -9:
            return ("Bad parameter to an ldap routine");
        case -10:
            return ("Out of memory");
        case -11:
            return ("Connect error");
        case -12:
            return ("Not Supported");
        case -13:
            return ("Control not found");
        case -14:
            return ("No results returned");
        case -15:
            return ("More results to return");
        case -16:
            return ("Client Loop");
        case -17:
            return ("Referral Limit Exceeded");

        default:
            return ("Unknown system error");
    }
#endif
}

