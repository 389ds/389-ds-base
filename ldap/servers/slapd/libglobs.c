/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *  libglobs.c -- SLAPD library global variables
 *
 *  !!!!!!!!!!!!!!!!!!!!!!!!!!!! IMPORTANT !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 *  Process for adding new configuration items to libglobs.c
 *
 *  To understand the process of adding a new configuration item, you need to
 *  know how values here are used, and their lifecycle.
 *
 *  First, the *initial* values are set from main.c when it calls
 *  FrontendConfig_init(). This creates the global frontendConfig struct.
 *
 *  Logging is then initiated in g_log_init(), which sets a number of defaults.
 *
 *  During the startup, dse.ldif is read. Any value from dse.ldif now overrides
 *  the value in cfg. These call the appropriate config_set_<type> function
 *  so the checking and locking is performed.
 *
 *  The server is now running. Values from the cfg are read through the code
 *  with config_get_<type>. For cn=config, these are read from configdse.c
 *  and presented to the search.
 *
 *  When a value is modified, the appropriate config_set_<type> function is
 *  simply called.
 *
 *  When a value is deleted, two things can happen. First, is that the value
 *  does not define an initvalue, so the deletion is rejected will
 *  LDAP_UNWILLING_TO_PERFORM. Second is that the value does have an initvalue
 *  so the mod_delete actually acts as config_set_<type>(initvalue). Null is
 *  never seen by the cfg struct. This is important as it prevents races!
 *
 *  A key note is if the value is in dse.ldif, it *always* overrides the value
 *  that DS is providing. If the value is only in libglobs.c as a default, if
 *  the default changes, any instance that does NOT define the config in dse.ldif
 *  will automatically gain the new default.
 *
 *  ===== ADDING A NEW VALUE =====
 *
 *  With this in mind, you are here to add a new value.
 *
 *  First, add the appropriate type for the cfg struct in slap.h
 *  struct _slapdFrontendConfig { }
 *  Now, you *must* provide defaults for the type. In slap.h there is a section
 *  of SLAPD_DEFAULT_* options. You want to add your option here. If it's an int
 *  type you *must* provided
 *  #define SLAPD_DEFAULT_OPTION <int>
 *  #define SLAPD_DEFAULT_OPTION_STR "<int>"
 *
 *  Now the default is populated in libglobs.c. Add a line like:
 *  cfg->option = SLAPD_DEFAULT_OPTION
 *
 *  Next you need to add the config_get_and_set struct. It is defined below
 *  but important to note is:
 *  {CONFIG_ACCESSLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
 *      log_set_expirationtime, SLAPD_ACCESS_LOG,
 *      (void**)&global_slapdFrontendConfig.accesslog_exptime,
 *      CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR},
 *  {CONFIG_LOCALUSER_ATTRIBUTE, config_set_localuser,
 *      NULL, 0,
 *      (void**)&global_slapdFrontendConfig.localuser,
 *      CONFIG_STRING, NULL, NULL // deletion is not allowed
 *  },
 *
 *  The first struct takes an int. So here, you would use SLAPD_DEFAULT_OPTION_STR
 *  for your initvalue. This allows the config item to be reset.
 *  The second struct *does not* allow a reset, and it's initvalue is set to NULL.
 *
 *  You may now optionally add the config_get_<type> / config_set_<type>
 *  functions. If you do not define these, ldap will not be able to modify the
 *  value live or from dse.ldif. So you probably want these ;)
 *
 *  DO NOT add your new config type to template.dse.ldif.in. You will BREAK
 *  transparent upgrades of the value.
 *
 *  Key notes:
 *  - A value that does not allow reset, can still be modified. It just cannot
 *      have a mod_delete performed on it.
 *  - Logging defaults must go in libglobs.c, slap.h, and log.c (g_log_init())
 *  - To allow a reset to "blank", init value of "" for a char * type is used.
 *  - For int and onoff types, you must provide a int or a bool for reset to work.
 *  - Int types must have a matching _STR define for the initvalue to allow reset
 *  - define your values in pairs in slap.h. This way it's easy to spot mistakes.
 *  - DO NOT add your new values to dse.ldif. ONLY in slap.h/libglobs.c. This
 *      allows default upgrading!
 *
 *  Happy configuring
 *      -- wibrown, 2016.
 *
 */

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
#include <sys/time.h>
#include <sys/param.h> /* MAXPATHLEN */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h> /* pwdnam */
#include <assert.h>
#ifdef USE_SYSCONF
#include <unistd.h>
#endif /* USE_SYSCONF */
#include "slap.h"
#include "plhash.h"
#if defined(LINUX)
#include <malloc.h>
#endif
#include <sys/resource.h>
#include <rust-slapi-private.h>


#define REMOVE_CHANGELOG_CMD "remove"

int slapd_ldap_debug = SLAPD_DEFAULT_ERRORLOG_LEVEL;
char *ldap_srvtab = "";

/* Note that the 'attrname' arguments are used only for log messages */
typedef int (*ConfigSetFunc)(const char *attrname, char *value, char *errorbuf, int apply);
typedef int (*LogSetFunc)(const char *attrname, char *value, int whichlog, char *errorbuf, int apply);
typedef void * (*ConfigGenInitFunc)(void);

typedef enum {
    CONFIG_INT,                          /* maps to int */
    CONFIG_LONG,                         /* maps to long */
    CONFIG_LONG_LONG,                    /* maps to a long long (PRInt64) */
    CONFIG_STRING,                       /* maps to char* */
    CONFIG_CHARRAY,                      /* maps to char** */
    CONFIG_ON_OFF,                       /* maps 0/1 to "off"/"on" */
    CONFIG_STRING_OR_OFF,                /* use "off" instead of null or an empty string */
    CONFIG_STRING_OR_UNKNOWN,            /* use "unknown" instead of an empty string */
    CONFIG_CONSTANT_INT,                 /* for #define values, e.g. */
    CONFIG_CONSTANT_STRING,              /* for #define values, e.g. */
    CONFIG_SPECIAL_REFERRALLIST,         /* this is a berval list */
    CONFIG_SPECIAL_SSLCLIENTAUTH,        /* maps strings to an enumeration */
    CONFIG_SPECIAL_ERRORLOGLEVEL,        /* requires & with LDAP_DEBUG_ANY */
    CONFIG_STRING_OR_EMPTY,              /* use an empty string */
    CONFIG_SPECIAL_ANON_ACCESS_SWITCH,   /* maps strings to an enumeration */
    CONFIG_SPECIAL_VALIDATE_CERT_SWITCH, /* maps strings to an enumeration */
    CONFIG_SPECIAL_UNHASHED_PW_SWITCH,   /* unhashed pw: on/off/nolog */
    CONFIG_SPECIAL_TLS_CHECK_CRL,        /* maps enum tls_check_crl_t to char * */
    CONFIG_SPECIAL_FILTER_VERIFY,      /* maps to a config strict/warn-strict/warn/off enum */
    CONFIG_STRING_GENERATED,             /* A string that can be set, or is internally generated */
} ConfigVarType;

static int32_t config_set_onoff(const char *attrname, char *value, int32_t *configvalue, char *errorbuf, int apply);
static int config_set_schemareplace(const char *attrname, char *value, char *errorbuf, int apply);
static int invalid_sasl_mech(char *str);


/* CONFIG_ON_OFF */
slapi_onoff_t init_accesslog_rotationsync_enabled;
slapi_onoff_t init_securitylog_rotationsync_enabled;
slapi_onoff_t init_errorlog_rotationsync_enabled;
slapi_onoff_t init_auditlog_rotationsync_enabled;
slapi_onoff_t init_auditfaillog_rotationsync_enabled;
slapi_onoff_t init_accesslog_compress_enabled;
slapi_onoff_t init_securitylog_compress_enabled;
slapi_onoff_t init_auditlog_compress_enabled;
slapi_onoff_t init_auditfaillog_compress_enabled;
slapi_onoff_t init_errorlog_compress_enabled;
slapi_onoff_t init_accesslog_logging_enabled;
slapi_onoff_t init_accesslogbuffering;
slapi_onoff_t init_securitylog_logging_enabled;
slapi_onoff_t init_securitylogbuffering;
slapi_onoff_t init_external_libs_debug_enabled;
slapi_onoff_t init_errorlog_logging_enabled;
slapi_onoff_t init_auditlog_logging_enabled;
slapi_onoff_t init_auditlog_logging_hide_unhashed_pw;
slapi_onoff_t init_auditfaillog_logging_enabled;
slapi_onoff_t init_auditfaillog_logging_hide_unhashed_pw;
slapi_onoff_t init_logging_hr_timestamps;
slapi_onoff_t init_csnlogging;
slapi_onoff_t init_pw_unlock;
slapi_onoff_t init_pw_must_change;
slapi_onoff_t init_pwpolicy_local;
slapi_onoff_t init_pwpolicy_inherit_global;
slapi_onoff_t init_pw_lockout;
slapi_onoff_t init_pw_history;
slapi_onoff_t init_pw_is_global_policy;
slapi_onoff_t init_pw_is_legacy;
slapi_onoff_t init_pw_track_update_time;
slapi_onoff_t init_pw_change;
slapi_onoff_t init_pw_exp;
slapi_onoff_t init_pw_send_expiring;
slapi_onoff_t init_pw_palindrome;
slapi_onoff_t init_pw_dict_check;
slapi_onoff_t init_allow_hashed_pw;
slapi_onoff_t init_pw_syntax;
slapi_onoff_t init_schemacheck;
slapi_onoff_t init_schemamod;
slapi_onoff_t init_ds4_compatible_schema;
slapi_onoff_t init_schema_ignore_trailing_spaces;
slapi_onoff_t init_enquote_sup_oc;
slapi_onoff_t init_rewrite_rfc1274;
slapi_onoff_t init_syntaxcheck;
slapi_onoff_t init_syntaxlogging;
slapi_onoff_t init_dn_validate_strict;
slapi_onoff_t init_attrname_exceptions;
slapi_onoff_t init_return_exact_case;
slapi_onoff_t init_result_tweak;
slapi_onoff_t init_plugin_track;
slapi_onoff_t init_moddn_aci;
slapi_onoff_t init_targetfilter_cache;
slapi_onoff_t init_lastmod;
slapi_onoff_t init_readonly;
slapi_onoff_t init_accesscontrol;
slapi_onoff_t init_nagle;
slapi_onoff_t init_security;
slapi_onoff_t init_ssl_check_hostname;
slapi_onoff_t init_ldapi_switch;
slapi_onoff_t init_ldapi_bind_switch;
slapi_onoff_t init_ldapi_map_entries;
slapi_onoff_t init_allow_unauth_binds;
slapi_onoff_t init_require_secure_binds;
slapi_onoff_t init_close_on_failed_bind;
slapi_onoff_t init_minssf_exclude_rootdse;
slapi_onoff_t init_force_sasl_external;
slapi_onoff_t init_slapi_counters;
slapi_onoff_t init_entryusn_global;
slapi_onoff_t init_disk_monitoring;
slapi_onoff_t init_disk_threshold_readonly;
slapi_onoff_t init_disk_logging_critical;
slapi_onoff_t init_ndn_cache_enabled;
slapi_onoff_t init_sasl_mapping_fallback;
slapi_onoff_t init_return_orig_type;
slapi_onoff_t init_enable_turbo_mode;
slapi_onoff_t init_connection_nocanon;
slapi_onoff_t init_plugin_logging;
slapi_int_t init_connection_buffer;
slapi_onoff_t init_ignore_time_skew;
slapi_onoff_t init_dynamic_plugins;
slapi_onoff_t init_cn_uses_dn_syntax_in_dns;
slapi_onoff_t init_global_backend_local;
slapi_onoff_t init_enable_nunc_stans;
#if defined(LINUX)
#endif
slapi_onoff_t init_extract_pem;
slapi_onoff_t init_ignore_vattrs;
slapi_onoff_t init_enable_upgrade_hash;
slapi_special_filter_verify_t init_verify_filter_schema;
slapi_onoff_t init_enable_ldapssotoken;
slapi_onoff_t init_return_orig_dn;
slapi_onoff_t init_pw_admin_skip_info;

static int
isInt(ConfigVarType type)
{
    return type == CONFIG_INT || type == CONFIG_ON_OFF || type == CONFIG_SPECIAL_SSLCLIENTAUTH || type == CONFIG_SPECIAL_ERRORLOGLEVEL;
}

/* the caller will typically have to cast the result based on the ConfigVarType */
typedef void *(*ConfigGetFunc)(void);

/* static Ref_Array global_referrals; */
static slapdFrontendConfig_t global_slapdFrontendConfig;

static struct config_get_and_set
{
    const char *attr_name;         /* the name of the attribute */
    ConfigSetFunc setfunc;         /* the function to call to set the value */
    LogSetFunc logsetfunc;         /* log functions are special */
    int whichlog;                  /* ACCESS, ERROR, AUDIT, etc. */
    void **config_var_addr;        /* address of member of slapdFrontendConfig struct */
    ConfigVarType config_var_type; /* cast to this type when getting */
    ConfigGetFunc getfunc;         /* for special handling */
    void *initvalue;               /* init values */
    ConfigGenInitFunc geninitfunc; /* An init value generator */
} ConfigList[] = {
    {CONFIG_AUDITLOG_MODE_ATTRIBUTE, NULL,
     log_set_mode, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_mode,
     CONFIG_STRING, NULL, SLAPD_INIT_LOG_MODE, NULL},
    {CONFIG_AUDITLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
     log_set_rotationsync_enabled, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_rotationsync_enabled,
     CONFIG_ON_OFF, NULL, &init_auditlog_rotationsync_enabled, NULL},
    {CONFIG_AUDITLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
     log_set_rotationsynchour, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_rotationsynchour,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR, NULL},
    {CONFIG_AUDITLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
     log_set_rotationsyncmin, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_rotationsyncmin,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR, NULL},
    {CONFIG_AUDITLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
     log_set_rotationtime, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_rotationtime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONTIME_STR, NULL},
    {CONFIG_ACCESSLOG_MODE_ATTRIBUTE, NULL,
     log_set_mode, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_mode,
     CONFIG_STRING, NULL, SLAPD_INIT_LOG_MODE, NULL},
    {CONFIG_ACCESSLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
     log_set_numlogsperdir, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_maxnumlogs,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ACCESS_MAXNUMLOGS_STR, NULL},
    {CONFIG_LOGLEVEL_ATTRIBUTE, config_set_errorlog_level,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.errorloglevel,
     CONFIG_SPECIAL_ERRORLOGLEVEL, NULL, SLAPD_DEFAULT_FE_ERRORLOG_LEVEL_STR, NULL},
    {CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
     log_set_logging, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_logging_enabled,
     CONFIG_ON_OFF, NULL, &init_errorlog_logging_enabled, NULL},
    {CONFIG_ERRORLOG_MODE_ATTRIBUTE, NULL,
     log_set_mode, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_mode,
     CONFIG_STRING, NULL, SLAPD_INIT_LOG_MODE, NULL},
    {CONFIG_ERRORLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
     log_set_expirationtime, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_exptime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR, NULL},
    {CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
     log_set_logging, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_logging_enabled,
     CONFIG_ON_OFF, NULL, &init_accesslog_logging_enabled, NULL},
    {CONFIG_ACCESSLOG_COMPRESS_ENABLED_ATTRIBUTE, NULL,
     log_set_compression, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_compress,
     CONFIG_ON_OFF, NULL, &init_accesslog_compress_enabled, NULL},
    {CONFIG_SECURITYLOG_COMPRESS_ENABLED_ATTRIBUTE, NULL,
     log_set_compression, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_compress,
     CONFIG_ON_OFF, NULL, &init_securitylog_compress_enabled, NULL},
    {CONFIG_AUDITLOG_COMPRESS_ENABLED_ATTRIBUTE, NULL,
     log_set_compression, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_compress,
     CONFIG_ON_OFF, NULL, &init_auditlog_compress_enabled, NULL},
    {CONFIG_AUDITFAILLOG_COMPRESS_ENABLED_ATTRIBUTE, NULL,
     log_set_compression, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_compress,
     CONFIG_ON_OFF, NULL, &init_auditfaillog_compress_enabled, NULL},
    {CONFIG_ERRORLOG_COMPRESS_ENABLED_ATTRIBUTE, NULL,
     log_set_compression, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_compress,
     CONFIG_ON_OFF, NULL, &init_errorlog_compress_enabled, NULL},
    {CONFIG_PORT_ATTRIBUTE, config_set_port,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.port,
     CONFIG_INT, NULL, NULL, NULL},
    {CONFIG_WORKINGDIR_ATTRIBUTE, config_set_workingdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.workingdir,
     CONFIG_STRING_OR_EMPTY, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_MAXTHREADSPERCONN_ATTRIBUTE, config_set_maxthreadsperconn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.maxthreadsperconn,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAX_THREADS_PER_CONN_STR, NULL},
    {CONFIG_ACCESSLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
     log_set_expirationtime, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_exptime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR, NULL},
    {CONFIG_LOCALUSER_ATTRIBUTE, config_set_localuser,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.localuser,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_ERRORLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
     log_set_rotationsync_enabled, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_rotationsync_enabled,
     CONFIG_ON_OFF, NULL, &init_errorlog_rotationsync_enabled, NULL},
    {CONFIG_ERRORLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
     log_set_rotationsynchour, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_rotationsynchour,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR, NULL},
    {CONFIG_ERRORLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
     log_set_rotationsyncmin, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_rotationsyncmin,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR, NULL},
    {CONFIG_ERRORLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
     log_set_rotationtime, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_rotationtime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONTIME_STR, NULL},
    {CONFIG_PW_INHISTORY_ATTRIBUTE, config_set_pw_inhistory,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_inhistory,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_INHISTORY_STR, NULL},
    {CONFIG_PW_STORAGESCHEME_ATTRIBUTE, config_set_pw_storagescheme,
     NULL, 0, NULL,
     CONFIG_STRING, (ConfigGetFunc)config_get_pw_storagescheme,
     "", NULL},
    /*
     * Set this to empty string to allow reset to work, but
     * the value is actually derived in set_pw_storagescheme.
     */
    {CONFIG_PW_UNLOCK_ATTRIBUTE, config_set_pw_unlock,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_unlock,
     CONFIG_ON_OFF, NULL, &init_pw_unlock, NULL},
    {CONFIG_PW_GRACELIMIT_ATTRIBUTE, config_set_pw_gracelimit,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_gracelimit,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_GRACELIMIT_STR, NULL},
    {CONFIG_PW_ADMIN_DN_ATTRIBUTE, config_set_pw_admin_dn,
     NULL, 0,
     NULL,
     CONFIG_STRING, (ConfigGetFunc)config_get_pw_admin_dn, "", NULL},
    {CONFIG_PW_ADMIN_SKIP_INFO_ATTRIBUTE, config_set_pw_admin_skip_info,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_admin_skip_info,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_pw_admin_dn,
     &init_pw_admin_skip_info, NULL},
    {CONFIG_ACCESSLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
     log_set_rotationsync_enabled, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_rotationsync_enabled,
     CONFIG_ON_OFF, NULL, &init_accesslog_rotationsync_enabled, NULL},
    {CONFIG_ACCESSLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
     log_set_rotationsynchour, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_rotationsynchour,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR, NULL},
    {CONFIG_ACCESSLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
     log_set_rotationsyncmin, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_rotationsyncmin,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR, NULL},
    {CONFIG_ACCESSLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
     log_set_rotationtime, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_rotationtime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONTIME_STR, NULL},
    {CONFIG_PW_MUSTCHANGE_ATTRIBUTE, config_set_pw_must_change,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_must_change,
     CONFIG_ON_OFF, NULL, &init_pw_must_change, NULL},
    {CONFIG_PWPOLICY_LOCAL_ATTRIBUTE, config_set_pwpolicy_local,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pwpolicy_local,
     CONFIG_ON_OFF, NULL, &init_pwpolicy_local, NULL},
    {CONFIG_PWPOLICY_INHERIT_GLOBAL_ATTRIBUTE, config_set_pwpolicy_inherit_global,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pwpolicy_inherit_global,
     CONFIG_ON_OFF, NULL, &init_pwpolicy_inherit_global, NULL},
    {CONFIG_AUDITLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
     log_set_maxdiskspace, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_maxdiskspace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXDISKSPACE_STR, NULL},
    {CONFIG_SIZELIMIT_ATTRIBUTE, config_set_sizelimit,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.sizelimit,
     CONFIG_INT, NULL, SLAPD_DEFAULT_SIZELIMIT_STR, NULL},
    {CONFIG_AUDITLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
     log_set_logsize, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_maxlogsize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR, NULL},
    {CONFIG_PW_WARNING_ATTRIBUTE, config_set_pw_warning,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_warning,
     CONFIG_LONG, NULL, SLAPD_DEFAULT_PW_WARNING_STR, NULL},
    {CONFIG_READONLY_ATTRIBUTE, config_set_readonly,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.readonly,
     CONFIG_ON_OFF, NULL, &init_readonly, NULL},
    {CONFIG_SASL_MAPPING_FALLBACK, config_set_sasl_mapping_fallback,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.sasl_mapping_fallback,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_sasl_mapping_fallback,
     &init_sasl_mapping_fallback, NULL},
    {CONFIG_THREADNUMBER_ATTRIBUTE, config_set_threadnumber,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.threadnumber,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAX_THREADS_STR, NULL},
    {CONFIG_PW_LOCKOUT_ATTRIBUTE, config_set_pw_lockout,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_lockout,
     CONFIG_ON_OFF, NULL, &init_pw_lockout, NULL},
    {CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, config_set_enquote_sup_oc,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.enquote_sup_oc,
     CONFIG_ON_OFF, NULL, &init_enquote_sup_oc, NULL},
    {CONFIG_LOCALHOST_ATTRIBUTE, config_set_localhost,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.localhost,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_IOBLOCKTIMEOUT_ATTRIBUTE, config_set_ioblocktimeout,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ioblocktimeout,
     CONFIG_INT, NULL, SLAPD_DEFAULT_IOBLOCK_TIMEOUT_STR, NULL},
    {CONFIG_MAX_FILTER_NEST_LEVEL_ATTRIBUTE, config_set_max_filter_nest_level,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.max_filter_nest_level,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL_STR, NULL},
    {CONFIG_ERRORLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
     log_set_maxdiskspace, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_maxdiskspace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXDISKSPACE_STR, NULL},
    {CONFIG_PW_MINLENGTH_ATTRIBUTE, config_set_pw_minlength,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minlength,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINLENGTH_STR, NULL},
    {CONFIG_PW_MINDIGITS_ATTRIBUTE, config_set_pw_mindigits,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_mindigits,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINDIGITS_STR, NULL},
    {CONFIG_PW_MINALPHAS_ATTRIBUTE, config_set_pw_minalphas,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minalphas,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINALPHAS_STR, NULL},
    {CONFIG_PW_MINUPPERS_ATTRIBUTE, config_set_pw_minuppers,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minuppers,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINUPPERS_STR, NULL},
    {CONFIG_PW_MINLOWERS_ATTRIBUTE, config_set_pw_minlowers,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minlowers,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINLOWERS_STR, NULL},
    {CONFIG_PW_MINSPECIALS_ATTRIBUTE, config_set_pw_minspecials,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minspecials,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINSPECIALS_STR, NULL},
    {CONFIG_PW_MIN8BIT_ATTRIBUTE, config_set_pw_min8bit,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_min8bit,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MIN8BIT_STR, NULL},
    {CONFIG_PW_MAXREPEATS_ATTRIBUTE, config_set_pw_maxrepeats,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_maxrepeats,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MAXREPEATS_STR, NULL},
    {CONFIG_PW_MINCATEGORIES_ATTRIBUTE, config_set_pw_mincategories,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_mincategories,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINCATEGORIES_STR, NULL},
    {CONFIG_PW_MINTOKENLENGTH_ATTRIBUTE, config_set_pw_mintokenlength,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_mintokenlength,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MINTOKENLENGTH_STR, NULL},

    /* Password palindrome */
    {CONFIG_PW_PALINDROME_ATTRIBUTE, config_set_pw_palindrome,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_palindrome,
     CONFIG_ON_OFF, NULL, &init_pw_palindrome, NULL},
    /* password dictionary check */
    {CONFIG_PW_CHECK_DICT_ATTRIBUTE, config_set_pw_dict_check,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_check_dict,
     CONFIG_ON_OFF, NULL, &init_pw_dict_check, NULL},
    /* password dictionary path */
    {CONFIG_PW_DICT_PATH_ATTRIBUTE, config_set_pw_dict_path,
      NULL, 0,
      (void **)&global_slapdFrontendConfig.pw_policy.pw_dict_path,
      CONFIG_STRING, NULL, "", NULL},
    /* password user attr check list */
    {CONFIG_PW_USERATTRS_ATTRIBUTE, config_set_pw_user_attrs,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_cmp_attrs,
     CONFIG_STRING, NULL, "", NULL},
    /* password bad work list */
    {CONFIG_PW_BAD_WORDS_ATTRIBUTE, config_set_pw_bad_words,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_bad_words,
     CONFIG_STRING, NULL, "", NULL},
    /* password max sequence */
    {CONFIG_PW_MAX_SEQ_ATTRIBUTE, config_set_pw_max_seq,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_max_seq,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MAX_SEQ_ATTRIBUTE_STR, NULL},
    /* Max sequence sets */
    {CONFIG_PW_MAX_SEQ_SETS_ATTRIBUTE, config_set_pw_max_seq_sets,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_seq_char_sets,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MAX_SEQ_SETS_ATTRIBUTE_STR, NULL},
    /* password max repeated characters per class */
    {CONFIG_PW_MAX_CLASS_CHARS_ATTRIBUTE, config_set_pw_max_class_repeats,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_max_class_repeats,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MAX_CLASS_CHARS_ATTRIBUTE_STR, NULL},
    {CONFIG_ERRORLOG_ATTRIBUTE, config_set_errorlog,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.errorlog,
     CONFIG_STRING_OR_EMPTY, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_EXTERNAL_LIBS_DEBUG_ENABLED, config_set_external_libs_debug_enabled,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.external_libs_debug_enabled,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_external_libs_debug_enabled,
     &init_external_libs_debug_enabled, NULL},
    {CONFIG_AUDITLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
     log_set_expirationtime, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_exptime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR, NULL},
    {CONFIG_SCHEMACHECK_ATTRIBUTE, config_set_schemacheck,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.schemacheck,
     CONFIG_ON_OFF, NULL, &init_schemacheck, NULL},
    {CONFIG_SCHEMAMOD_ATTRIBUTE, config_set_schemamod,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.schemamod,
     CONFIG_ON_OFF, NULL, &init_schemamod, NULL},
    {CONFIG_SYNTAXCHECK_ATTRIBUTE, config_set_syntaxcheck,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.syntaxcheck,
     CONFIG_ON_OFF, NULL, &init_syntaxcheck, NULL},
    {CONFIG_SYNTAXLOGGING_ATTRIBUTE, config_set_syntaxlogging,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.syntaxlogging,
     CONFIG_ON_OFF, NULL, &init_syntaxlogging, NULL},
    {CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE, config_set_dn_validate_strict,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.dn_validate_strict,
     CONFIG_ON_OFF, NULL, &init_dn_validate_strict, NULL},
    {CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, config_set_ds4_compatible_schema,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ds4_compatible_schema,
     CONFIG_ON_OFF, NULL, &init_ds4_compatible_schema, NULL},
    {CONFIG_SCHEMA_IGNORE_TRAILING_SPACES,
     config_set_schema_ignore_trailing_spaces, NULL, 0,
     (void **)&global_slapdFrontendConfig.schema_ignore_trailing_spaces,
     CONFIG_ON_OFF, NULL, &init_schema_ignore_trailing_spaces, NULL},
    {CONFIG_SCHEMAREPLACE_ATTRIBUTE, config_set_schemareplace, NULL, 0,
     (void **)&global_slapdFrontendConfig.schemareplace,
     CONFIG_STRING_OR_OFF, NULL, CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY, NULL},
    {CONFIG_ACCESSLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
     log_set_maxdiskspace, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_maxdiskspace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ACCESS_MAXDISKSPACE_STR, NULL},
    {CONFIG_REFERRAL_ATTRIBUTE, (ConfigSetFunc)config_set_defaultreferral,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.defaultreferral,
     CONFIG_SPECIAL_REFERRALLIST, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_PW_MAXFAILURE_ATTRIBUTE, config_set_pw_maxfailure,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_maxfailure,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_MAXFAILURE_STR, NULL},
    {CONFIG_ACCESSLOG_ATTRIBUTE, config_set_accesslog,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.accesslog,
     CONFIG_STRING_OR_EMPTY, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_LASTMOD_ATTRIBUTE, config_set_lastmod,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.lastmod,
     CONFIG_ON_OFF, NULL, &init_lastmod, NULL},
    {CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE, config_set_rootpwstoragescheme,
     NULL, 0, NULL,
     CONFIG_STRING, (ConfigGetFunc)config_get_rootpwstoragescheme,
     "", NULL},
    /*
     * Set this to empty string to allow reset to work, but
     * the value is actually derived in set_rootpwstoragescheme.
     */
    {CONFIG_PW_HISTORY_ATTRIBUTE, config_set_pw_history,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_history,
     CONFIG_ON_OFF, NULL, &init_pw_history, NULL},
    {CONFIG_SECURITY_ATTRIBUTE, config_set_security,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.security,
     CONFIG_ON_OFF, NULL, &init_security, NULL},
    {CONFIG_PW_MAXAGE_ATTRIBUTE, config_set_pw_maxage,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_maxage,
     CONFIG_LONG, NULL, SLAPD_DEFAULT_PW_MAXAGE_STR, NULL},
    {CONFIG_AUDITLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_rotationtimeunit, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_rotationunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_AUDITLOG_ROTATIONUNIT, NULL},
    {CONFIG_PW_RESETFAILURECOUNT_ATTRIBUTE, config_set_pw_resetfailurecount,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_resetfailurecount,
     CONFIG_LONG, NULL, SLAPD_DEFAULT_PW_RESETFAILURECOUNT_STR, NULL},
    {CONFIG_PW_TPR_MAXUSE, config_set_pw_tpr_maxuse,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_tpr_maxuse,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_TPR_MAXUSE_STR, NULL},
    {CONFIG_PW_TPR_DELAY_EXPIRE_AT, config_set_pw_tpr_delay_expire_at,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_tpr_delay_expire_at,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_TPR_DELAY_EXPIRE_AT_STR, NULL},
     {CONFIG_PW_TPR_DELAY_VALID_FROM, config_set_pw_tpr_delay_valid_from,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_tpr_delay_valid_from,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PW_TPR_DELAY_VALID_FROM_STR, NULL},
    {CONFIG_PW_ISGLOBAL_ATTRIBUTE, config_set_pw_is_global_policy,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_is_global_policy,
     CONFIG_ON_OFF, NULL, &init_pw_is_global_policy, NULL},
    {CONFIG_PW_IS_LEGACY, config_set_pw_is_legacy_policy,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_is_legacy,
     CONFIG_ON_OFF, NULL, &init_pw_is_legacy, NULL},
    {CONFIG_PW_TRACK_LAST_UPDATE_TIME, config_set_pw_track_last_update_time,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_track_update_time,
     CONFIG_ON_OFF, NULL, &init_pw_track_update_time, NULL},
    {CONFIG_AUDITLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
     log_set_numlogsperdir, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_maxnumlogs,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXNUMLOGS_STR, NULL},
    {CONFIG_ERRORLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_expirationtimeunit, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_exptimeunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_LOG_EXPTIMEUNIT, NULL},
    /* errorlog list is read only, so no set func and no config var addr */
    {CONFIG_ERRORLOG_LIST_ATTRIBUTE, NULL,
     NULL, 0, NULL,
     CONFIG_CHARRAY, (ConfigGetFunc)config_get_errorlog_list, NULL, NULL},
    {CONFIG_GROUPEVALNESTLEVEL_ATTRIBUTE, config_set_groupevalnestlevel,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.groupevalnestlevel,
     CONFIG_INT, NULL, SLAPD_DEFAULT_GROUPEVALNESTLEVEL_STR, NULL},
    {CONFIG_ACCESSLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_expirationtimeunit, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_exptimeunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_LOG_EXPTIMEUNIT, NULL},
    {CONFIG_ROOTPW_ATTRIBUTE, config_set_rootpw,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.rootpw,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_PW_CHANGE_ATTRIBUTE, config_set_pw_change,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_change,
     CONFIG_ON_OFF, NULL, &init_pw_change, NULL},
    {CONFIG_ACCESSLOGLEVEL_ATTRIBUTE, config_set_accesslog_level,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.accessloglevel,
     CONFIG_INT, NULL, SLAPD_DEFAULT_ACCESSLOG_LEVEL_STR, NULL},
    {CONFIG_STATLOGLEVEL_ATTRIBUTE, config_set_statlog_level,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.statloglevel,
     CONFIG_INT, NULL, SLAPD_DEFAULT_STATLOG_LEVEL, NULL},
    {CONFIG_SECURITYLOGLEVEL_ATTRIBUTE, config_set_securitylog_level,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.securityloglevel,
     CONFIG_INT, NULL, SLAPD_DEFAULT_ACCESSLOG_LEVEL_STR, NULL},
    {CONFIG_ERRORLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_rotationtimeunit, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_rotationunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_ERRORLOG_ROTATIONUNIT, NULL},
    {CONFIG_SECUREPORT_ATTRIBUTE, config_set_secureport,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.secureport,
     CONFIG_INT, NULL, NULL, NULL},
    {CONFIG_BASEDN_ATTRIBUTE, config_set_basedn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.certmap_basedn,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_TIMELIMIT_ATTRIBUTE, config_set_timelimit,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.timelimit,
     CONFIG_INT, NULL, SLAPD_DEFAULT_TIMELIMIT_STR, NULL},
    {CONFIG_ERRORLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
     log_set_logsize, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_maxlogsize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR, NULL},
    {CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE, config_set_reservedescriptors,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.reservedescriptors,
     CONFIG_INT, NULL, SLAPD_DEFAULT_RESERVE_FDS_STR, NULL},
    /* access log list is read only, no set func, no config var addr */
    {CONFIG_ACCESSLOG_LIST_ATTRIBUTE, NULL,
     NULL, 0, NULL,
     CONFIG_CHARRAY, (ConfigGetFunc)config_get_accesslog_list, NULL, NULL},
    {CONFIG_SVRTAB_ATTRIBUTE, config_set_srvtab,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.srvtab,
     CONFIG_STRING, NULL, "", NULL},
    {CONFIG_PW_EXP_ATTRIBUTE, config_set_pw_exp,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_exp,
     CONFIG_ON_OFF, NULL, &init_pw_exp, NULL},
    {CONFIG_PW_SEND_EXPIRING, config_set_pw_send_expiring,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_send_expiring,
     CONFIG_ON_OFF, NULL, &init_pw_send_expiring, NULL},
    {CONFIG_ACCESSCONTROL_ATTRIBUTE, config_set_accesscontrol,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.accesscontrol,
     CONFIG_ON_OFF, NULL, &init_accesscontrol, NULL},
    {CONFIG_AUDITLOG_LIST_ATTRIBUTE, NULL,
     NULL, 0, NULL,
     CONFIG_CHARRAY, (ConfigGetFunc)config_get_auditlog_list, NULL, NULL},
    {CONFIG_ACCESSLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_rotationtimeunit, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_rotationunit,
     CONFIG_STRING, NULL, SLAPD_INIT_ACCESSLOG_ROTATIONUNIT, NULL},
    {CONFIG_PW_LOCKDURATION_ATTRIBUTE, config_set_pw_lockduration,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_lockduration,
     CONFIG_LONG, NULL, SLAPD_DEFAULT_PW_LOCKDURATION_STR, NULL},
    {CONFIG_ACCESSLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
     log_set_logsize, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_maxlogsize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR, NULL},
    {CONFIG_IDLETIMEOUT_ATTRIBUTE, config_set_idletimeout,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.idletimeout,
     CONFIG_INT, NULL, SLAPD_DEFAULT_IDLE_TIMEOUT_STR, NULL},
    {CONFIG_NAGLE_ATTRIBUTE, config_set_nagle,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.nagle,
     CONFIG_ON_OFF, NULL, &init_nagle, NULL},
    {CONFIG_ERRORLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
     log_set_mindiskspace, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_minfreespace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MINFREESPACE_STR, NULL},
    {CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
     log_set_logging, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_logging_enabled,
     CONFIG_ON_OFF, NULL, &init_auditlog_logging_enabled, NULL},
    {CONFIG_AUDITLOG_LOGGING_HIDE_UNHASHED_PW, config_set_auditlog_unhashed_pw,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.auditlog_logging_hide_unhashed_pw,
     CONFIG_ON_OFF, NULL, &init_auditlog_logging_hide_unhashed_pw, NULL},
    {CONFIG_AUDITLOG_DISPLAY_ATTRS, config_set_auditlog_display_attrs,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.auditlog_display_attrs,
     CONFIG_STRING, NULL, &config_get_auditlog_display_attrs, NULL},
    {CONFIG_ACCESSLOG_BUFFERING_ATTRIBUTE, config_set_accesslogbuffering,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.accesslogbuffering,
     CONFIG_ON_OFF, NULL, &init_accesslogbuffering, NULL},
    {CONFIG_SECURITYLOG_BUFFERING_ATTRIBUTE, config_set_securitylogbuffering,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.securitylogbuffering,
     CONFIG_ON_OFF, NULL, &init_securitylogbuffering, NULL},
    {CONFIG_CSNLOGGING_ATTRIBUTE, config_set_csnlogging,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.csnlogging,
     CONFIG_ON_OFF, NULL, &init_csnlogging, NULL},
    {CONFIG_AUDITLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_expirationtimeunit, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_exptimeunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_LOG_EXPTIMEUNIT, NULL},
    {CONFIG_ALLOW_HASHED_PW_ATTRIBUTE, config_set_allow_hashed_pw,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.allow_hashed_pw,
     CONFIG_ON_OFF, NULL, &init_allow_hashed_pw, NULL},
    {CONFIG_PW_SYNTAX_ATTRIBUTE, config_set_pw_syntax,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_syntax,
     CONFIG_ON_OFF, NULL, &init_pw_syntax, NULL},
    {CONFIG_LISTENHOST_ATTRIBUTE, config_set_listenhost,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.listenhost,
     CONFIG_STRING, NULL, "", NULL /* Empty value is allowed */},
    {CONFIG_SNMP_INDEX_ATTRIBUTE, config_set_snmp_index,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.snmp_index,
     CONFIG_INT, NULL, SLAPD_DEFAULT_SNMP_INDEX_STR, NULL},
    {CONFIG_LDAPI_FILENAME_ATTRIBUTE, config_set_ldapi_filename,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_filename,
     CONFIG_STRING, NULL, SLAPD_LDAPI_DEFAULT_FILENAME, NULL},
    {CONFIG_LDAPI_SWITCH_ATTRIBUTE, config_set_ldapi_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_switch,
     CONFIG_ON_OFF, NULL, &init_ldapi_switch, NULL},
    {CONFIG_LDAPI_BIND_SWITCH_ATTRIBUTE, config_set_ldapi_bind_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_bind_switch,
     CONFIG_ON_OFF, NULL, &init_ldapi_bind_switch, NULL},
    {CONFIG_LDAPI_ROOT_DN_ATTRIBUTE, config_set_ldapi_root_dn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_root_dn,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_DIRECTORY_MANAGER, NULL},
    {CONFIG_LDAPI_MAP_ENTRIES_ATTRIBUTE, config_set_ldapi_map_entries,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_map_entries,
     CONFIG_ON_OFF, NULL, &init_ldapi_map_entries, NULL},
    {CONFIG_LDAPI_UIDNUMBER_TYPE_ATTRIBUTE, config_set_ldapi_uidnumber_type,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_uidnumber_type,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_UIDNUM_TYPE, NULL},
    {CONFIG_LDAPI_GIDNUMBER_TYPE_ATTRIBUTE, config_set_ldapi_gidnumber_type,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_gidnumber_type,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_GIDNUM_TYPE, NULL},
    {CONFIG_LDAPI_SEARCH_BASE_DN_ATTRIBUTE, config_set_ldapi_search_base_dn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_search_base_dn,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_LDAPI_SEARCH_BASE, NULL},
    {CONFIG_LDAPI_AUTH_MAP_BASE_ATTRIBUTE, config_set_ldapi_mapping_base_dn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_auto_mapping_base,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_LDAPI_MAPPING_DN, NULL},
#if defined(ENABLE_AUTO_DN_SUFFIX)
    {CONFIG_LDAPI_AUTO_DN_SUFFIX_ATTRIBUTE, config_set_ldapi_auto_dn_suffix,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapi_auto_dn_suffix,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_LDAPI_AUTO_DN, NULL},
#endif
    {CONFIG_ANON_LIMITS_DN_ATTRIBUTE, config_set_anon_limits_dn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.anon_limits_dn,
     CONFIG_STRING, NULL, "", NULL},
    {CONFIG_SLAPI_COUNTER_ATTRIBUTE, config_set_slapi_counters,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.slapi_counters,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_slapi_counters,
     &init_slapi_counters, NULL},
    {CONFIG_ACCESSLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
     log_set_mindiskspace, SLAPD_ACCESS_LOG,
     (void **)&global_slapdFrontendConfig.accesslog_minfreespace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MINFREESPACE_STR, NULL},
    {CONFIG_ERRORLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
     log_set_numlogsperdir, SLAPD_ERROR_LOG,
     (void **)&global_slapdFrontendConfig.errorlog_maxnumlogs,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXNUMLOGS_STR, NULL},
    {CONFIG_SECURELISTENHOST_ATTRIBUTE, config_set_securelistenhost,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.securelistenhost,
     CONFIG_STRING, NULL, "", NULL /* Empty value is allowed */},
    {CONFIG_AUDITLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
     log_set_mindiskspace, SLAPD_AUDIT_LOG,
     (void **)&global_slapdFrontendConfig.auditlog_minfreespace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MINFREESPACE_STR, NULL},
    {CONFIG_ROOTDN_ATTRIBUTE, config_set_rootdn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.rootdn,
     CONFIG_STRING, NULL, SLAPD_DEFAULT_DIRECTORY_MANAGER, NULL},
    {CONFIG_PW_MINAGE_ATTRIBUTE, config_set_pw_minage,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pw_policy.pw_minage,
     CONFIG_LONG, NULL, SLAPD_DEFAULT_PW_MINAGE_STR, NULL},
    {CONFIG_AUDITFILE_ATTRIBUTE, config_set_auditlog,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.auditlog,
     CONFIG_STRING_OR_EMPTY, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, config_set_return_exact_case,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.return_exact_case,
     CONFIG_ON_OFF, NULL, &init_return_exact_case, NULL},
    {CONFIG_RESULT_TWEAK_ATTRIBUTE, config_set_result_tweak,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.result_tweak,
     CONFIG_ON_OFF, NULL, &init_result_tweak, NULL},
    {CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE, config_set_plugin_tracking,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.plugin_track,
     CONFIG_ON_OFF, NULL, &init_plugin_track, NULL},
    {CONFIG_MODDN_ACI_ATTRIBUTE, config_set_moddn_aci,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.moddn_aci,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_moddn_aci,
     &init_moddn_aci, NULL},
    {CONFIG_TARGETFILTER_CACHE_ATTRIBUTE, config_set_targetfilter_cache,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.targetfilter_cache,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_targetfilter_cache,
     &init_targetfilter_cache, NULL},
    {CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE, config_set_attrname_exceptions,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.attrname_exceptions,
     CONFIG_ON_OFF, NULL, &init_attrname_exceptions, NULL},
    {CONFIG_MAXBERSIZE_ATTRIBUTE, config_set_maxbersize,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.maxbersize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAXBERSIZE_STR, NULL},
    {CONFIG_MAXSASLIOSIZE_ATTRIBUTE, config_set_maxsasliosize,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.maxsasliosize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAX_SASLIO_SIZE_STR, NULL},
    {CONFIG_VERSIONSTRING_ATTRIBUTE, config_set_versionstring,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.versionstring,
     CONFIG_STRING, NULL, SLAPD_VERSION_STR, NULL},
    {CONFIG_REFERRAL_MODE_ATTRIBUTE, config_set_referral_mode,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.refer_url,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_NUM_LISTENERS_ATTRIBUTE, config_set_num_listeners,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.num_listeners,
     CONFIG_INT, NULL, SLAPD_DEFAULT_NUM_LISTENERS_STR, NULL},
    {CONFIG_MAXDESCRIPTORS_ATTRIBUTE, config_set_maxdescriptors,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.maxdescriptors,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MAXDESCRIPTORS_STR, NULL},
    {CONFIG_SSLCLIENTAUTH_ATTRIBUTE, config_set_SSLclientAuth,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.SSLclientAuth,
     CONFIG_SPECIAL_SSLCLIENTAUTH, NULL, SLAPD_DEFAULT_SSLCLIENTAUTH_STR, NULL},
    {CONFIG_SSL_CHECK_HOSTNAME_ATTRIBUTE, config_set_ssl_check_hostname,
     NULL, 0, NULL,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_ssl_check_hostname,
     &init_ssl_check_hostname, NULL},
    {CONFIG_CONFIG_ATTRIBUTE, 0,
     NULL, 0, (void **)SLAPD_CONFIG_DN,
     CONFIG_CONSTANT_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    {CONFIG_HASH_FILTERS_ATTRIBUTE, config_set_hash_filters,
     NULL, 0, NULL,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_hash_filters,
     NULL, NULL /* deletion is not allowed */},
    /* instance dir; used by admin tasks */
    {CONFIG_INSTDIR_ATTRIBUTE, config_set_instancedir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.instancedir,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    /* parameterizing schema dir */
    {CONFIG_SCHEMADIR_ATTRIBUTE, config_set_schemadir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.schemadir,
     CONFIG_STRING, NULL, NULL, NULL /* deletion is not allowed */},
    /* parameterizing lock dir */
    {CONFIG_LOCKDIR_ATTRIBUTE, config_set_lockdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.lockdir,
     CONFIG_STRING, (ConfigGetFunc)config_get_lockdir,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing tmp dir */
    {CONFIG_TMPDIR_ATTRIBUTE, config_set_tmpdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.tmpdir,
     CONFIG_STRING, (ConfigGetFunc)config_get_tmpdir,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing cert dir */
    {CONFIG_CERTDIR_ATTRIBUTE, config_set_certdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.certdir,
     CONFIG_STRING, (ConfigGetFunc)config_get_certdir,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing ldif dir */
    {CONFIG_LDIFDIR_ATTRIBUTE, config_set_ldifdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldifdir,
     CONFIG_STRING, (ConfigGetFunc)config_get_ldifdir,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing bak dir */
    {CONFIG_BAKDIR_ATTRIBUTE, config_set_bakdir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.bakdir,
     CONFIG_STRING, (ConfigGetFunc)config_get_bakdir,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing sasl plugin path */
    {CONFIG_SASLPATH_ATTRIBUTE, config_set_saslpath,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.saslpath,
     CONFIG_STRING, (ConfigGetFunc)config_get_saslpath,
     NULL, NULL /* deletion is not allowed */},
    /* parameterizing run dir */
    {CONFIG_RUNDIR_ATTRIBUTE, config_set_rundir,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.rundir,
     CONFIG_STRING, (ConfigGetFunc)config_get_rundir,
     NULL, NULL /* deletion is not allowed */},
    {CONFIG_REWRITE_RFC1274_ATTRIBUTE, config_set_rewrite_rfc1274,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.rewrite_rfc1274,
     CONFIG_ON_OFF, NULL, &init_rewrite_rfc1274, NULL},
    {CONFIG_OUTBOUND_LDAP_IO_TIMEOUT_ATTRIBUTE,
     config_set_outbound_ldap_io_timeout,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.outbound_ldap_io_timeout,
     CONFIG_INT, NULL, SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT_STR, NULL},
    {CONFIG_UNAUTH_BINDS_ATTRIBUTE, config_set_unauth_binds_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.allow_unauth_binds,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_unauth_binds_switch,
     &init_allow_unauth_binds, NULL},
    {CONFIG_REQUIRE_SECURE_BINDS_ATTRIBUTE, config_set_require_secure_binds,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.require_secure_binds,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_require_secure_binds,
     &init_require_secure_binds, NULL},
    {CONFIG_CLOSE_ON_FAILED_BIND, config_set_close_on_failed_bind,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.close_on_failed_bind,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_close_on_failed_bind,
     &init_close_on_failed_bind, NULL},
    {CONFIG_ANON_ACCESS_ATTRIBUTE, config_set_anon_access_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.allow_anon_access,
     CONFIG_SPECIAL_ANON_ACCESS_SWITCH,
     (ConfigGetFunc)config_get_anon_access_switch,
     SLAPD_DEFAULT_ALLOW_ANON_ACCESS_STR, NULL},
    {CONFIG_LOCALSSF_ATTRIBUTE, config_set_localssf,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.localssf,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOCAL_SSF_STR, NULL},
    {CONFIG_MINSSF_ATTRIBUTE, config_set_minssf,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.minssf,
     CONFIG_INT, NULL, SLAPD_DEFAULT_MIN_SSF_STR, NULL},
    {CONFIG_MINSSF_EXCLUDE_ROOTDSE, config_set_minssf_exclude_rootdse,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.minssf_exclude_rootdse,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_minssf_exclude_rootdse,
     &init_minssf_exclude_rootdse, NULL},
    {CONFIG_FORCE_SASL_EXTERNAL_ATTRIBUTE, config_set_force_sasl_external,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.force_sasl_external,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_force_sasl_external,
     &init_force_sasl_external, NULL},
    {CONFIG_ENTRYUSN_GLOBAL, config_set_entryusn_global,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.entryusn_global,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_entryusn_global,
     &init_entryusn_global, NULL},
    {CONFIG_ENTRYUSN_IMPORT_INITVAL, config_set_entryusn_import_init,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.entryusn_import_init,
     CONFIG_STRING, (ConfigGetFunc)config_get_entryusn_import_init,
     SLAPD_ENTRYUSN_IMPORT_INIT, NULL},
    {CONFIG_VALIDATE_CERT_ATTRIBUTE, config_set_validate_cert_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.validate_cert,
     CONFIG_SPECIAL_VALIDATE_CERT_SWITCH,
     (ConfigGetFunc)config_get_validate_cert_switch, SLAPD_DEFAULT_VALIDATE_CERT_STR, NULL},
    {CONFIG_PAGEDSIZELIMIT_ATTRIBUTE, config_set_pagedsizelimit,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.pagedsizelimit,
     CONFIG_INT, NULL, SLAPD_DEFAULT_PAGEDSIZELIMIT_STR, NULL},
    {CONFIG_DEFAULT_NAMING_CONTEXT, config_set_default_naming_context,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.default_naming_context,
     CONFIG_STRING, (ConfigGetFunc)config_get_default_naming_context, NULL, NULL},
    {CONFIG_DISK_MONITORING, config_set_disk_monitoring,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.disk_monitoring,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_disk_monitoring,
     &init_disk_monitoring, NULL},
    {CONFIG_DISK_THRESHOLD_READONLY, config_set_disk_threshold_readonly,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.disk_threshold_readonly,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_disk_threshold_readonly,
     &init_disk_threshold_readonly, NULL},
    {CONFIG_DISK_THRESHOLD, config_set_disk_threshold,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.disk_threshold,
     CONFIG_LONG_LONG, (ConfigGetFunc)config_get_disk_threshold,
     SLAPD_DEFAULT_DISK_THRESHOLD_STR, NULL},
    {CONFIG_DISK_GRACE_PERIOD, config_set_disk_grace_period,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.disk_grace_period,
     CONFIG_INT, (ConfigGetFunc)config_get_disk_grace_period,
     SLAPD_DEFAULT_DISK_GRACE_PERIOD_STR, NULL},
    {CONFIG_DISK_LOGGING_CRITICAL, config_set_disk_logging_critical,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.disk_logging_critical,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_disk_logging_critical,
     &init_disk_logging_critical, NULL},
    {CONFIG_NDN_CACHE, config_set_ndn_cache_enabled,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ndn_cache_enabled,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_ndn_cache_enabled,
     &init_ndn_cache_enabled, NULL},
    {CONFIG_NDN_CACHE_SIZE, config_set_ndn_cache_max_size,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ndn_cache_max_size,
     CONFIG_INT, (ConfigGetFunc)config_get_ndn_cache_size, SLAPD_DEFAULT_NDN_SIZE_STR, NULL},
    /* The issue here is that we probably need "empty string" to be valid, rather than NULL for reset purposes */
    {CONFIG_ALLOWED_SASL_MECHS, config_set_allowed_sasl_mechs,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.allowed_sasl_mechs,
     CONFIG_STRING, (ConfigGetFunc)config_get_allowed_sasl_mechs, "", NULL},
    {CONFIG_IGNORE_VATTRS, config_set_ignore_vattrs,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ignore_vattrs,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_ignore_vattrs, &init_ignore_vattrs, NULL},
    {CONFIG_UNHASHED_PW_SWITCH_ATTRIBUTE, config_set_unhashed_pw_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.unhashed_pw_switch,
     CONFIG_SPECIAL_UNHASHED_PW_SWITCH,
     (ConfigGetFunc)config_get_unhashed_pw_switch,
     SLAPD_DEFAULT_UNHASHED_PW_SWITCH_STR, NULL},
    {CONFIG_SASL_MAXBUFSIZE, config_set_sasl_maxbufsize,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.sasl_max_bufsize,
     CONFIG_INT, (ConfigGetFunc)config_get_sasl_maxbufsize,
     SLAPD_DEFAULT_SASL_MAXBUFSIZE_STR, NULL},
    {CONFIG_SEARCH_RETURN_ORIGINAL_TYPE, config_set_return_orig_type_switch,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.return_orig_type,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_return_orig_type_switch, &init_return_orig_type, NULL},
    {CONFIG_ENABLE_TURBO_MODE, config_set_enable_turbo_mode,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.enable_turbo_mode,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_enable_turbo_mode, &init_enable_turbo_mode, NULL},
    {CONFIG_CONNECTION_BUFFER, config_set_connection_buffer,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.connection_buffer,
     CONFIG_INT, (ConfigGetFunc)config_get_connection_buffer, &init_connection_buffer, NULL},
    {CONFIG_CONNECTION_NOCANON, config_set_connection_nocanon,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.connection_nocanon,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_connection_nocanon, &init_connection_nocanon, NULL},
    {CONFIG_PLUGIN_LOGGING, config_set_plugin_logging,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.plugin_logging,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_plugin_logging, &init_plugin_logging, NULL},
    {CONFIG_LISTEN_BACKLOG_SIZE, config_set_listen_backlog_size,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.listen_backlog_size, CONFIG_INT,
     (ConfigGetFunc)config_get_listen_backlog_size, DAEMON_LISTEN_SIZE_STR, NULL},
    {CONFIG_DYNAMIC_PLUGINS, config_set_dynamic_plugins,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.dynamic_plugins, CONFIG_ON_OFF,
     (ConfigGetFunc)config_get_dynamic_plugins, &init_dynamic_plugins, NULL},
    {CONFIG_CN_USES_DN_SYNTAX_IN_DNS, config_set_cn_uses_dn_syntax_in_dns,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.cn_uses_dn_syntax_in_dns, CONFIG_ON_OFF,
     (ConfigGetFunc)config_get_cn_uses_dn_syntax_in_dns, &init_cn_uses_dn_syntax_in_dns, NULL},
#if defined(LINUX)
#if defined(__GLIBC__)
    {CONFIG_MALLOC_MXFAST, config_set_malloc_mxfast,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.malloc_mxfast,
     CONFIG_INT, (ConfigGetFunc)config_get_malloc_mxfast,
     DEFAULT_MALLOC_UNSET_STR, NULL},
    {CONFIG_MALLOC_TRIM_THRESHOLD, config_set_malloc_trim_threshold,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.malloc_trim_threshold,
     CONFIG_INT, (ConfigGetFunc)config_get_malloc_trim_threshold,
     DEFAULT_MALLOC_UNSET_STR, NULL},
    {CONFIG_MALLOC_MMAP_THRESHOLD, config_set_malloc_mmap_threshold,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.malloc_mmap_threshold,
     CONFIG_INT, (ConfigGetFunc)config_get_malloc_mmap_threshold,
     DEFAULT_MALLOC_UNSET_STR, NULL},
#endif
#endif
    {CONFIG_IGNORE_TIME_SKEW, config_set_ignore_time_skew,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ignore_time_skew,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_ignore_time_skew, &init_ignore_time_skew, NULL},
    {CONFIG_GLOBAL_BACKEND_LOCK, config_set_global_backend_lock,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.global_backend_lock,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_global_backend_lock, &init_global_backend_local, NULL},
    {CONFIG_MAXSIMPLEPAGED_PER_CONN_ATTRIBUTE, config_set_maxsimplepaged_per_conn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.maxsimplepaged_per_conn,
     CONFIG_INT, (ConfigGetFunc)config_get_maxsimplepaged_per_conn, SLAPD_DEFAULT_MAXSIMPLEPAGED_PER_CONN_STR, NULL},
    {CONFIG_ENABLE_NUNC_STANS, config_set_enable_nunc_stans,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.enable_nunc_stans,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_enable_nunc_stans, &init_enable_nunc_stans, NULL},
    /* Audit fail log configuration */
    {CONFIG_AUDITFAILLOG_MODE_ATTRIBUTE, NULL,
     log_set_mode, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_mode,
     CONFIG_STRING, NULL, SLAPD_INIT_LOG_MODE, NULL},
    {CONFIG_AUDITFAILLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
     log_set_rotationsync_enabled, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_rotationsync_enabled,
     CONFIG_ON_OFF, NULL, &init_auditfaillog_rotationsync_enabled, NULL},
    {CONFIG_AUDITFAILLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
     log_set_rotationsynchour, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_rotationsynchour,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR, NULL},
    {CONFIG_AUDITFAILLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
     log_set_rotationsyncmin, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_rotationsyncmin,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR, NULL},
    {CONFIG_AUDITFAILLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
     log_set_rotationtime, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_rotationtime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONTIME_STR, NULL},
    {CONFIG_AUDITFAILLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
     log_set_maxdiskspace, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_maxdiskspace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXDISKSPACE_STR, NULL},
    {CONFIG_AUDITFAILLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
     log_set_logsize, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_maxlogsize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR, NULL},
    {CONFIG_AUDITFAILLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
     log_set_expirationtime, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_exptime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR, NULL},
    {CONFIG_AUDITFAILLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
     log_set_numlogsperdir, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_maxnumlogs,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXNUMLOGS_STR, NULL},
    {CONFIG_AUDITFAILLOG_LIST_ATTRIBUTE, NULL,
     NULL, 0, NULL,
     CONFIG_CHARRAY, (ConfigGetFunc)config_get_auditfaillog_list, NULL, NULL},
    {CONFIG_AUDITFAILLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
     log_set_logging, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_logging_enabled,
     CONFIG_ON_OFF, NULL, &init_auditfaillog_logging_enabled, NULL},
    {CONFIG_AUDITFAILLOG_LOGGING_HIDE_UNHASHED_PW, config_set_auditfaillog_unhashed_pw,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.auditfaillog_logging_hide_unhashed_pw,
     CONFIG_ON_OFF, NULL, &init_auditfaillog_logging_hide_unhashed_pw, NULL},
    {CONFIG_AUDITFAILLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_expirationtimeunit, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_exptimeunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_LOG_EXPTIMEUNIT, NULL},
    {CONFIG_AUDITFAILLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
     log_set_mindiskspace, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_minfreespace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MINFREESPACE_STR, NULL},
    {CONFIG_AUDITFAILLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_rotationtimeunit, SLAPD_AUDITFAIL_LOG,
     (void **)&global_slapdFrontendConfig.auditfaillog_rotationunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_AUDITFAILLOG_ROTATIONUNIT, NULL},
    {CONFIG_AUDITFAILFILE_ATTRIBUTE, config_set_auditfaillog,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.auditfaillog,
     CONFIG_STRING_OR_EMPTY, NULL, "", NULL /* prevents deletion when null */},
    /* Security Audit log configuration */
    {CONFIG_SECURITYLOG_MODE_ATTRIBUTE, NULL,
     log_set_mode, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_mode,
     CONFIG_STRING, NULL, SLAPD_INIT_LOG_MODE, NULL},
    {CONFIG_SECURITYLOG_LOGROTATIONSYNCENABLED_ATTRIBUTE, NULL,
     log_set_rotationsync_enabled, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_rotationsync_enabled,
     CONFIG_ON_OFF, NULL, &init_securitylog_rotationsync_enabled, NULL},
    {CONFIG_SECURITYLOG_LOGROTATIONSYNCHOUR_ATTRIBUTE, NULL,
     log_set_rotationsynchour, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_rotationsynchour,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR_STR, NULL},
    {CONFIG_SECURITYLOG_LOGROTATIONSYNCMIN_ATTRIBUTE, NULL,
     log_set_rotationsyncmin, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_rotationsyncmin,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN_STR, NULL},
    {CONFIG_SECURITYLOG_LOGROTATIONTIME_ATTRIBUTE, NULL,
     log_set_rotationtime, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_rotationtime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_ROTATIONTIME_STR, NULL},
    {CONFIG_SECURITYLOG_MAXLOGDISKSPACE_ATTRIBUTE, NULL,
     log_set_maxdiskspace, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_maxdiskspace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXDISKSPACE_STR, NULL},
    {CONFIG_SECURITYLOG_MAXLOGSIZE_ATTRIBUTE, NULL,
     log_set_logsize, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_maxlogsize,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXLOGSIZE_STR, NULL},
    {CONFIG_SECURITYLOG_LOGEXPIRATIONTIME_ATTRIBUTE, NULL,
     log_set_expirationtime, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_exptime,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_EXPTIME_STR, NULL},
    {CONFIG_SECURITYLOG_MAXNUMOFLOGSPERDIR_ATTRIBUTE, NULL,
     log_set_numlogsperdir, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_maxnumlogs,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MAXNUMLOGS_STR, NULL},
    {CONFIG_SECURITYLOG_LIST_ATTRIBUTE, NULL,
     NULL, 0, NULL,
     CONFIG_CHARRAY, (ConfigGetFunc)config_get_securitylog_list, NULL, NULL},
    {CONFIG_SECURITYLOG_LOGGING_ENABLED_ATTRIBUTE, NULL,
     log_set_logging, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_logging_enabled,
     CONFIG_ON_OFF, NULL, &init_securitylog_logging_enabled, NULL},
    {CONFIG_SECURITYLOG_LOGEXPIRATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_expirationtimeunit, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_exptimeunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_LOG_EXPTIMEUNIT, NULL},
    {CONFIG_SECURITYLOG_MINFREEDISKSPACE_ATTRIBUTE, NULL,
     log_set_mindiskspace, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_minfreespace,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LOG_MINFREESPACE_STR, NULL},
    {CONFIG_SECURITYLOG_LOGROTATIONTIMEUNIT_ATTRIBUTE, NULL,
     log_set_rotationtimeunit, SLAPD_SECURITY_LOG,
     (void **)&global_slapdFrontendConfig.securitylog_rotationunit,
     CONFIG_STRING_OR_UNKNOWN, NULL, SLAPD_INIT_SECURITYLOG_ROTATIONUNIT, NULL},
    {CONFIG_SECURITYFILE_ATTRIBUTE, config_set_securitylog,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.securitylog,
     CONFIG_STRING_OR_EMPTY, NULL, "", NULL /* prevents deletion when null */},
/* warning: initialization makes pointer from integer without a cast [enabled by default]. Why do we get this? */
#ifdef HAVE_CLOCK_GETTIME
    {CONFIG_LOGGING_HR_TIMESTAMPS, config_set_logging_hr_timestamps,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.logging_hr_timestamps,
     CONFIG_ON_OFF, NULL, &init_logging_hr_timestamps, NULL},
#endif
    {CONFIG_EXTRACT_PEM, config_set_extract_pem,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.extract_pem,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_extract_pem, &init_extract_pem, NULL},
    {CONFIG_LOGGING_BACKEND, NULL,
     log_set_backend, 0,
     (void **)&global_slapdFrontendConfig.logging_backend,
     CONFIG_STRING_OR_EMPTY, NULL, SLAPD_INIT_LOGGING_BACKEND_INTERNAL, NULL},
    {CONFIG_TLS_CHECK_CRL_ATTRIBUTE, config_set_tls_check_crl,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.tls_check_crl,
     CONFIG_SPECIAL_TLS_CHECK_CRL, (ConfigGetFunc)config_get_tls_check_crl,
     "none", NULL /* Allow reset to this value */},
    {CONFIG_ENABLE_UPGRADE_HASH, config_set_enable_upgrade_hash,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.enable_upgrade_hash,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_enable_upgrade_hash, &init_enable_upgrade_hash, NULL},
    {CONFIG_VERIFY_FILTER_SCHEMA, config_set_verify_filter_schema,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.verify_filter_schema,
     CONFIG_SPECIAL_FILTER_VERIFY, (ConfigGetFunc)config_get_verify_filter_schema,
     &init_verify_filter_schema},
    {CONFIG_ENABLE_LDAPSSOTOKEN, config_set_enable_ldapssotoken,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.enable_ldapssotoken,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_enable_ldapssotoken, &init_enable_ldapssotoken, NULL},
    {CONFIG_LDAPSSOTOKEN_SECRET, config_set_ldapssotoken_secret,
     NULL, 0,
     NULL,
     CONFIG_STRING_GENERATED, (ConfigGetFunc)config_get_ldapssotoken_secret, NULL,
     (ConfigGenInitFunc)fernet_generate_new_key
     },
    {CONFIG_LDAPSSOTOKEN_TTL, config_set_ldapssotoken_ttl,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.ldapssotoken_ttl,
     CONFIG_INT, NULL, SLAPD_DEFAULT_LDAPSSOTOKEN_TTL_STR, NULL},
    {CONFIG_TCP_FIN_TIMEOUT, config_set_tcp_fin_timeout,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.tcp_fin_timeout, CONFIG_INT,
     (ConfigGetFunc)config_get_tcp_fin_timeout, SLAPD_DEFAULT_TCP_FIN_TIMEOUT_STR, NULL},
    {CONFIG_TCP_KEEPALIVE_TIME, config_set_tcp_keepalive_time,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.tcp_keepalive_time, CONFIG_INT,
     (ConfigGetFunc)config_get_tcp_keepalive_time, SLAPD_DEFAULT_TCP_KEEPALIVE_TIME_STR, NULL},
    {CONFIG_REFERRAL_CHECK_PERIOD, config_set_referral_check_period,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.referral_check_period,
     CONFIG_INT, (ConfigGetFunc)config_set_referral_check_period,
     SLAPD_DEFAULT_REFERRAL_CHECK_PERIOD_STR, NULL},
    {CONFIG_RETURN_ENTRY_DN, config_set_return_orig_dn,
     NULL, 0,
     (void **)&global_slapdFrontendConfig.return_orig_dn,
     CONFIG_ON_OFF, (ConfigGetFunc)config_get_return_orig_dn, &init_return_orig_dn, NULL},
    /* End config */
    };

/*
 * hashNocaseString - used for case insensitive hash lookups
 */
PLHashNumber
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
PRIntn
hashNocaseCompare(const void *v1, const void *v2)
{
    return (strcasecmp((char *)v1, (char *)v2) == 0);
}

static PLHashTable *confighash = 0;

static void
init_config_get_and_set(void)
{
    if (!confighash) {
        int ii = 0;
        int tablesize = sizeof(ConfigList) / sizeof(ConfigList[0]);
        confighash = PL_NewHashTable(tablesize + 1, hashNocaseString,
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
#define GOLDEN_RATIO 0x9E3779B9U

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
    for (ii = 0; bvec && bvec[ii]; ++ii) {
        slapi_ch_free((void **)&bvec[ii]->bv_val);
        slapi_ch_free((void **)&bvec[ii]);
    }
    slapi_ch_free((void **)&bvec);
}

static struct berval **
strarray2bervalarray(const char **strarray)
{
    int ii = 0;
    struct berval **newlist = 0;

    /* first, count the number of items in the list */
    for (ii = 0; strarray && strarray[ii]; ++ii)
        ;

    /* if no items, return null */
    if (!ii)
        return newlist;

    /* allocate the list */
    newlist = (struct berval **)slapi_ch_malloc((ii + 1) * sizeof(struct berval *));
    newlist[ii] = 0;
    for (; ii; --ii) {
        newlist[ii - 1] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
        newlist[ii - 1]->bv_val = slapi_ch_strdup(strarray[ii - 1]);
        newlist[ii - 1]->bv_len = strlen(strarray[ii - 1]);
    }

    return newlist;
}

/*
 * counter for active threads
 */
static uint64_t active_threads = 0;

void
g_incr_active_threadcnt(void)
{
    slapi_atomic_incr_64(&active_threads, __ATOMIC_RELEASE);
}

void
g_decr_active_threadcnt(void)
{
    slapi_atomic_decr_64(&active_threads, __ATOMIC_RELEASE);
}

uint64_t
g_get_active_threadcnt(void)
{
    return slapi_atomic_load_64(&active_threads, __ATOMIC_RELEASE);
}

/*
** Setting this flag forces the server to shutdown.
*/
static int slapd_shutdown = 0;

void
g_set_shutdown(int reason)
{
    slapd_shutdown = reason;
    raise(SIGTERM);
}

int
g_get_shutdown(void)
{
    return slapd_shutdown;
}

int
slapi_is_shutting_down(void)
{
    return slapd_shutdown;
}


static int cmd_shutdown;

void
c_set_shutdown(void)
{
    cmd_shutdown = SLAPI_SHUTDOWN_SIGNAL;
}

int
c_get_shutdown(void)
{
    return cmd_shutdown;
}

slapdFrontendConfig_t *
getFrontendConfig(void)
{
    return &global_slapdFrontendConfig;
}

/*
 * FrontendConfig_init:
 * Put all default values for config stuff here.
 * If there's no default value, the value will be NULL if it's not set in dse.ldif
 */

void
pwpolicy_init_defaults (passwdPolicy *pw_policy)
{
    pw_policy->pw_change = LDAP_ON;
    pw_policy->pw_must_change = LDAP_OFF;
    pw_policy->pw_syntax = LDAP_OFF;
    pw_policy->pw_exp = LDAP_OFF;
    pw_policy->pw_send_expiring = LDAP_OFF;
    pw_policy->pw_minlength = SLAPD_DEFAULT_PW_MINLENGTH;
    pw_policy->pw_mindigits = SLAPD_DEFAULT_PW_MINDIGITS;
    pw_policy->pw_minalphas = SLAPD_DEFAULT_PW_MINALPHAS;
    pw_policy->pw_minuppers = SLAPD_DEFAULT_PW_MINUPPERS;
    pw_policy->pw_minlowers = SLAPD_DEFAULT_PW_MINLOWERS;
    pw_policy->pw_minspecials = SLAPD_DEFAULT_PW_MINSPECIALS;
    pw_policy->pw_min8bit = SLAPD_DEFAULT_PW_MIN8BIT;
    pw_policy->pw_maxrepeats = SLAPD_DEFAULT_PW_MAXREPEATS;
    pw_policy->pw_mincategories = SLAPD_DEFAULT_PW_MINCATEGORIES;
    pw_policy->pw_mintokenlength = SLAPD_DEFAULT_PW_MINTOKENLENGTH;
    pw_policy->pw_maxage = SLAPD_DEFAULT_PW_MAXAGE;
    pw_policy->pw_minage = SLAPD_DEFAULT_PW_MINAGE;
    pw_policy->pw_warning = SLAPD_DEFAULT_PW_WARNING;
    pw_policy->pw_history = LDAP_OFF;
    pw_policy->pw_inhistory = SLAPD_DEFAULT_PW_INHISTORY;
    pw_policy->pw_lockout = LDAP_OFF;
    pw_policy->pw_maxfailure = SLAPD_DEFAULT_PW_MAXFAILURE;
    pw_policy->pw_unlock = LDAP_ON;
    pw_policy->pw_lockduration = SLAPD_DEFAULT_PW_LOCKDURATION;
    pw_policy->pw_resetfailurecount = SLAPD_DEFAULT_PW_RESETFAILURECOUNT;
    pw_policy->pw_tpr_maxuse = SLAPD_DEFAULT_PW_TPR_MAXUSE;
    pw_policy->pw_tpr_delay_expire_at = SLAPD_DEFAULT_PW_TPR_DELAY_EXPIRE_AT;
    pw_policy->pw_tpr_delay_valid_from = SLAPD_DEFAULT_PW_TPR_DELAY_VALID_FROM;
    pw_policy->pw_gracelimit = SLAPD_DEFAULT_PW_GRACELIMIT;
    pw_policy->pw_admin = NULL;
    pw_policy->pw_admin_skip_info = LDAP_OFF;
    pw_policy->pw_admin_user = NULL;
    pw_policy->pw_is_legacy = LDAP_ON;
    pw_policy->pw_track_update_time = LDAP_OFF;
}

static void
pwpolicy_fe_init_onoff(passwdPolicy *pw_policy)
{
    init_pw_change = pw_policy->pw_change;
    init_pw_must_change = pw_policy->pw_must_change;
    init_pw_syntax = pw_policy->pw_syntax;
    init_pw_exp = pw_policy->pw_exp;
    init_pw_send_expiring = pw_policy->pw_send_expiring;
    init_pw_history = pw_policy->pw_history;
    init_pw_lockout = pw_policy->pw_lockout;
    init_pw_unlock = pw_policy->pw_unlock;
    init_pw_is_legacy = pw_policy->pw_is_legacy;
    init_pw_track_update_time = pw_policy->pw_track_update_time;
    init_pw_palindrome = pw_policy->pw_palindrome;
    init_pw_dict_check = pw_policy->pw_check_dict;
}

void
FrontendConfig_init(void)
{
    slapdFrontendConfig_t *cfg = getFrontendConfig();
    struct rlimit rlp;
    int64_t maxdescriptors = SLAPD_DEFAULT_MAXDESCRIPTORS;

    /* prove rust is working */
    PR_ASSERT(do_nothing_rust() == 0);


#if SLAPI_CFG_USE_RWLOCK == 1
    /* initialize the read/write configuration lock */
    if ((cfg->cfg_rwlock = slapi_new_rwlock()) == NULL) {
        slapi_log_err(SLAPI_LOG_EMERG, "FrontendConfig_init",
                      "Failed to initialize cfg_rwlock. Exiting now.");
        exit(-1);
    }
#else
    if ((cfg->cfg_lock = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_EMERG, "FrontendConfig_init",
                      "Failed to initialize cfg_lock. Exiting now.");
        exit(-1);
    }
#endif
    /* Default the maximum fd's to the maximum allowed */
    if (getrlimit(RLIMIT_NOFILE, &rlp) == 0) {
        if ((int64_t)rlp.rlim_max < SLAPD_DEFAULT_MAXDESCRIPTORS) {
            maxdescriptors = (int64_t)rlp.rlim_max;
        }
    }

    /* Take the lock to make sure we barrier correctly. */
    CFG_LOCK_WRITE(cfg);

    cfg->port = LDAP_PORT;
    cfg->secureport = LDAPS_PORT;
    cfg->ldapi_filename = slapi_ch_strdup(SLAPD_LDAPI_DEFAULT_FILENAME);
    init_ldapi_switch = cfg->ldapi_switch = LDAP_OFF;
    init_ldapi_bind_switch = cfg->ldapi_bind_switch = LDAP_OFF;
    cfg->ldapi_root_dn = slapi_ch_strdup(SLAPD_DEFAULT_DIRECTORY_MANAGER);
    init_ldapi_map_entries = cfg->ldapi_map_entries = LDAP_OFF;
    cfg->ldapi_uidnumber_type = slapi_ch_strdup(SLAPD_DEFAULT_UIDNUM_TYPE);
    cfg->ldapi_gidnumber_type = slapi_ch_strdup(SLAPD_DEFAULT_GIDNUM_TYPE);
    /* These DNs are no need to be normalized. */
    cfg->ldapi_search_base_dn = slapi_ch_strdup(SLAPD_DEFAULT_LDAPI_SEARCH_BASE);
    cfg->ldapi_auto_mapping_base = slapi_ch_strdup(SLAPD_DEFAULT_LDAPI_MAPPING_DN);
#if defined(ENABLE_AUTO_DN_SUFFIX)
    cfg->ldapi_auto_dn_suffix = slapi_ch_strdup(SLAPD_DEFAULT_LDAPI_AUTO_DN);
#endif
    init_allow_unauth_binds = cfg->allow_unauth_binds = LDAP_OFF;
    init_require_secure_binds = cfg->require_secure_binds = LDAP_OFF;
    init_close_on_failed_bind = cfg->close_on_failed_bind = LDAP_OFF;
    cfg->allow_anon_access = SLAPD_DEFAULT_ALLOW_ANON_ACCESS;
    init_slapi_counters = cfg->slapi_counters = LDAP_ON;
    cfg->threadnumber = util_get_hardware_threads();
    cfg->maxthreadsperconn = SLAPD_DEFAULT_MAX_THREADS_PER_CONN;
    cfg->reservedescriptors = SLAPD_DEFAULT_RESERVE_FDS;
    cfg->idletimeout = SLAPD_DEFAULT_IDLE_TIMEOUT;
    cfg->ioblocktimeout = SLAPD_DEFAULT_IOBLOCK_TIMEOUT;
    cfg->outbound_ldap_io_timeout = SLAPD_DEFAULT_OUTBOUND_LDAP_IO_TIMEOUT;
    cfg->max_filter_nest_level = SLAPD_DEFAULT_MAX_FILTER_NEST_LEVEL;
    cfg->maxsasliosize = SLAPD_DEFAULT_MAX_SASLIO_SIZE;
    cfg->localssf = SLAPD_DEFAULT_LOCAL_SSF;
    cfg->minssf = SLAPD_DEFAULT_MIN_SSF;
    /* minssf is applied to rootdse, by default */
    init_minssf_exclude_rootdse = cfg->minssf_exclude_rootdse = LDAP_OFF;
    cfg->validate_cert = SLAPD_DEFAULT_VALIDATE_CERT;
    cfg->maxdescriptors = maxdescriptors;
    cfg->groupevalnestlevel = SLAPD_DEFAULT_GROUPEVALNESTLEVEL;
    cfg->snmp_index = SLAPD_DEFAULT_SNMP_INDEX;
    cfg->SSLclientAuth = SLAPD_DEFAULT_SSLCLIENTAUTH;
    cfg->num_listeners = SLAPD_DEFAULT_NUM_LISTENERS;
    init_accesscontrol = cfg->accesscontrol = LDAP_ON;

    /* nagle triggers set/unset TCP_CORK setsockopt per operation
     * as DS only sends complete PDU there is no benefit of nagle/tcp_cork
     */
    init_nagle = cfg->nagle = LDAP_OFF;
    init_security = cfg->security = LDAP_OFF;
    init_ssl_check_hostname = cfg->ssl_check_hostname = LDAP_ON;
    cfg->tls_check_crl = TLS_CHECK_NONE;
    init_return_exact_case = cfg->return_exact_case = LDAP_ON;
    init_result_tweak = cfg->result_tweak = LDAP_OFF;
    init_attrname_exceptions = cfg->attrname_exceptions = LDAP_OFF;
    cfg->useroc = slapi_ch_strdup("");
    cfg->userat = slapi_ch_strdup("");
    /* kexcoff: should not be initialized by default here
     * wibrown: The reason is that at the time this is called, plugins are
     * not yet loaded, so there are no schemes avaliable. As a result
     * pw_name2scheme will always return NULL
     */
    /* cfg->rootpwstoragescheme = pw_name2scheme( DEFAULT_PASSWORD_SCHEME_NAME ); */
    /* cfg->pw_storagescheme = pw_name2scheme( DEFAULT_PASSWORD_SCHEME_NAME ); */
    cfg->slapd_type = 0;
    cfg->versionstring = SLAPD_VERSION_STR;
    cfg->sizelimit = SLAPD_DEFAULT_SIZELIMIT;
    cfg->pagedsizelimit = SLAPD_DEFAULT_PAGEDSIZELIMIT;
    cfg->timelimit = SLAPD_DEFAULT_TIMELIMIT;
    cfg->anon_limits_dn = slapi_ch_strdup("");
    init_schemacheck = cfg->schemacheck = LDAP_ON;
    init_schemamod = cfg->schemamod = LDAP_ON;
    init_syntaxcheck = cfg->syntaxcheck = LDAP_ON;
    init_plugin_track = cfg->plugin_track = LDAP_OFF;
    init_moddn_aci = cfg->moddn_aci = LDAP_ON;
    init_targetfilter_cache = cfg->targetfilter_cache = LDAP_ON;
    init_syntaxlogging = cfg->syntaxlogging = LDAP_OFF;
    init_dn_validate_strict = cfg->dn_validate_strict = LDAP_OFF;
    init_ds4_compatible_schema = cfg->ds4_compatible_schema = LDAP_OFF;
    init_enquote_sup_oc = cfg->enquote_sup_oc = LDAP_OFF;
    init_lastmod = cfg->lastmod = LDAP_ON;
    init_rewrite_rfc1274 = cfg->rewrite_rfc1274 = LDAP_OFF;
    cfg->schemareplace = slapi_ch_strdup(CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY);
    init_schema_ignore_trailing_spaces = cfg->schema_ignore_trailing_spaces =
        SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES;
    /* do not force sasl external by default -
    * let clients abide by the LDAP standards and send us a SASL/EXTERNAL bind
    * if that's what they want to do */
    init_force_sasl_external = cfg->force_sasl_external = LDAP_OFF;
    init_readonly = cfg->readonly = LDAP_OFF;

    pwpolicy_init_defaults(&cfg->pw_policy);
    pwpolicy_fe_init_onoff(&cfg->pw_policy);
    init_pwpolicy_local = cfg->pwpolicy_local = LDAP_OFF;
    init_pwpolicy_inherit_global = cfg->pwpolicy_inherit_global = LDAP_OFF;
    init_allow_hashed_pw = cfg->allow_hashed_pw = LDAP_OFF;
    init_pw_is_global_policy = cfg->pw_is_global_policy = LDAP_OFF;
    init_pw_admin_skip_info = cfg->pw_admin_skip_info = LDAP_OFF;

    init_accesslog_logging_enabled = cfg->accesslog_logging_enabled = LDAP_ON;
    cfg->accesslog_mode = slapi_ch_strdup(SLAPD_INIT_LOG_MODE);
    cfg->accesslog_maxnumlogs = SLAPD_DEFAULT_LOG_ACCESS_MAXNUMLOGS;
    cfg->accesslog_maxlogsize = SLAPD_DEFAULT_LOG_MAXLOGSIZE;
    cfg->accesslog_rotationtime = SLAPD_DEFAULT_LOG_ROTATIONTIME;
    cfg->accesslog_rotationunit = slapi_ch_strdup(SLAPD_INIT_ACCESSLOG_ROTATIONUNIT);
    init_accesslog_rotationsync_enabled =
        cfg->accesslog_rotationsync_enabled = LDAP_OFF;
    cfg->accesslog_rotationsynchour = SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR;
    cfg->accesslog_rotationsyncmin = SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN;
    cfg->accesslog_maxdiskspace = SLAPD_DEFAULT_LOG_ACCESS_MAXDISKSPACE;
    cfg->accesslog_minfreespace = SLAPD_DEFAULT_LOG_MINFREESPACE;
    cfg->accesslog_exptime = SLAPD_DEFAULT_LOG_EXPTIME;
    cfg->accesslog_exptimeunit = slapi_ch_strdup(SLAPD_INIT_LOG_EXPTIMEUNIT);
    cfg->accessloglevel = SLAPD_DEFAULT_ACCESSLOG_LEVEL;
    init_accesslogbuffering = cfg->accesslogbuffering = LDAP_ON;
    init_csnlogging = cfg->csnlogging = LDAP_ON;
    init_accesslog_compress_enabled = cfg->accesslog_compress = LDAP_OFF;
    cfg->statloglevel = SLAPD_DEFAULT_STATLOG_LEVEL;

    init_securitylog_logging_enabled = cfg->securitylog_logging_enabled = LDAP_ON;
    cfg->securitylog_mode = slapi_ch_strdup(SLAPD_INIT_LOG_MODE);
    cfg->securitylog_maxnumlogs = SLAPD_DEFAULT_LOG_SECURITY_MAXNUMLOGS;
    cfg->securitylog_maxlogsize = SLAPD_DEFAULT_LOG_MAXLOGSIZE;
    cfg->securitylog_rotationtime = SLAPD_DEFAULT_LOG_ROTATIONTIME;
    cfg->securitylog_rotationunit = slapi_ch_strdup(SLAPD_INIT_SECURITYLOG_ROTATIONUNIT);
    init_securitylog_rotationsync_enabled =
        cfg->securitylog_rotationsync_enabled = LDAP_OFF;
    cfg->securitylog_rotationsynchour = SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR;
    cfg->securitylog_rotationsyncmin = SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN;
    cfg->securitylog_maxdiskspace = SLAPD_DEFAULT_LOG_SECURITY_MAXDISKSPACE;
    cfg->securitylog_minfreespace = SLAPD_DEFAULT_LOG_MINFREESPACE;
    cfg->securitylog_exptime = SLAPD_DEFAULT_LOG_EXPTIME;
    cfg->securitylog_exptimeunit = slapi_ch_strdup(SLAPD_INIT_LOG_EXPTIMEUNIT);
    cfg->securityloglevel = SLAPD_DEFAULT_SECURITYLOG_LEVEL;
    init_securitylogbuffering = cfg->securitylogbuffering = LDAP_ON;
    init_securitylog_compress_enabled = cfg->securitylog_compress = LDAP_ON;

    init_errorlog_logging_enabled = cfg->errorlog_logging_enabled = LDAP_ON;
    init_external_libs_debug_enabled = cfg->external_libs_debug_enabled = LDAP_OFF;
    cfg->errorlog_mode = slapi_ch_strdup(SLAPD_INIT_LOG_MODE);
    cfg->errorlog_maxnumlogs = SLAPD_DEFAULT_LOG_MAXNUMLOGS;
    cfg->errorlog_maxlogsize = SLAPD_DEFAULT_LOG_MAXLOGSIZE;
    cfg->errorlog_rotationtime = SLAPD_DEFAULT_LOG_ROTATIONTIME;
    cfg->errorlog_rotationunit = slapi_ch_strdup(SLAPD_INIT_ERRORLOG_ROTATIONUNIT);
    init_errorlog_rotationsync_enabled =
        cfg->errorlog_rotationsync_enabled = LDAP_OFF;
    cfg->errorlog_rotationsynchour = SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR;
    cfg->errorlog_rotationsyncmin = SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN;
    cfg->errorlog_maxdiskspace = SLAPD_DEFAULT_LOG_MAXDISKSPACE;
    cfg->errorlog_minfreespace = SLAPD_DEFAULT_LOG_MINFREESPACE;
    cfg->errorlog_exptime = SLAPD_DEFAULT_LOG_EXPTIME;
    cfg->errorlog_exptimeunit = slapi_ch_strdup(SLAPD_INIT_LOG_EXPTIMEUNIT);
    cfg->errorloglevel = SLAPD_DEFAULT_FE_ERRORLOG_LEVEL;
    init_errorlog_compress_enabled = cfg->errorlog_compress = LDAP_OFF;

    init_auditlog_logging_enabled = cfg->auditlog_logging_enabled = LDAP_OFF;
    cfg->auditlog_mode = slapi_ch_strdup(SLAPD_INIT_LOG_MODE);
    cfg->auditlog_maxnumlogs = SLAPD_DEFAULT_LOG_MAXNUMLOGS;
    cfg->auditlog_maxlogsize = SLAPD_DEFAULT_LOG_MAXLOGSIZE;
    cfg->auditlog_rotationtime = SLAPD_DEFAULT_LOG_ROTATIONTIME;
    cfg->auditlog_rotationunit = slapi_ch_strdup(SLAPD_INIT_AUDITLOG_ROTATIONUNIT);
    init_auditlog_rotationsync_enabled =
        cfg->auditlog_rotationsync_enabled = LDAP_OFF;
    cfg->auditlog_rotationsynchour = SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR;
    cfg->auditlog_rotationsyncmin = SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN;
    cfg->auditlog_maxdiskspace = SLAPD_DEFAULT_LOG_MAXDISKSPACE;
    cfg->auditlog_minfreespace = SLAPD_DEFAULT_LOG_MINFREESPACE;
    cfg->auditlog_exptime = SLAPD_DEFAULT_LOG_EXPTIME;
    cfg->auditlog_exptimeunit = slapi_ch_strdup(SLAPD_INIT_LOG_EXPTIMEUNIT);
    init_auditlog_logging_hide_unhashed_pw =
        cfg->auditlog_logging_hide_unhashed_pw = LDAP_ON;
    init_auditlog_compress_enabled = cfg->auditlog_compress = LDAP_OFF;

    init_auditfaillog_logging_enabled = cfg->auditfaillog_logging_enabled = LDAP_OFF;
    cfg->auditfaillog_mode = slapi_ch_strdup(SLAPD_INIT_LOG_MODE);
    cfg->auditfaillog_maxnumlogs = SLAPD_DEFAULT_LOG_MAXNUMLOGS;
    cfg->auditfaillog_maxlogsize = SLAPD_DEFAULT_LOG_MAXLOGSIZE;
    cfg->auditfaillog_rotationtime = SLAPD_DEFAULT_LOG_ROTATIONTIME;
    cfg->auditfaillog_rotationunit = slapi_ch_strdup(SLAPD_INIT_AUDITFAILLOG_ROTATIONUNIT);
    init_auditfaillog_rotationsync_enabled =
        cfg->auditfaillog_rotationsync_enabled = LDAP_OFF;
    cfg->auditfaillog_rotationsynchour = SLAPD_DEFAULT_LOG_ROTATIONSYNCHOUR;
    cfg->auditfaillog_rotationsyncmin = SLAPD_DEFAULT_LOG_ROTATIONSYNCMIN;
    cfg->auditfaillog_maxdiskspace = SLAPD_DEFAULT_LOG_MAXDISKSPACE;
    cfg->auditfaillog_minfreespace = SLAPD_DEFAULT_LOG_MINFREESPACE;
    cfg->auditfaillog_exptime = SLAPD_DEFAULT_LOG_EXPTIME;
    cfg->auditfaillog_exptimeunit = slapi_ch_strdup(SLAPD_INIT_LOG_EXPTIMEUNIT);
    init_auditfaillog_logging_hide_unhashed_pw =
        cfg->auditfaillog_logging_hide_unhashed_pw = LDAP_ON;
    init_auditfaillog_compress_enabled = cfg->auditfaillog_compress = LDAP_OFF;

#ifdef HAVE_CLOCK_GETTIME
    init_logging_hr_timestamps =
        cfg->logging_hr_timestamps = LDAP_ON;
#endif
    init_entryusn_global = cfg->entryusn_global = LDAP_OFF;
    cfg->entryusn_import_init = slapi_ch_strdup(SLAPD_ENTRYUSN_IMPORT_INIT);
    cfg->default_naming_context = NULL; /* store normalized dn */
    cfg->allowed_sasl_mechs = NULL;

    init_disk_monitoring = cfg->disk_monitoring = LDAP_OFF;
    init_disk_threshold_readonly = cfg->disk_threshold_readonly = LDAP_OFF;
    cfg->disk_threshold = SLAPD_DEFAULT_DISK_THRESHOLD;
    cfg->disk_grace_period = SLAPD_DEFAULT_DISK_GRACE_PERIOD;
    init_disk_logging_critical = cfg->disk_logging_critical = LDAP_OFF;
    init_ndn_cache_enabled = cfg->ndn_cache_enabled = LDAP_ON;
    cfg->ndn_cache_max_size = SLAPD_DEFAULT_NDN_SIZE;
    init_sasl_mapping_fallback = cfg->sasl_mapping_fallback = LDAP_OFF;
    init_ignore_vattrs = cfg->ignore_vattrs = LDAP_ON;
    cfg->sasl_max_bufsize = SLAPD_DEFAULT_SASL_MAXBUFSIZE;
    cfg->unhashed_pw_switch = SLAPD_DEFAULT_UNHASHED_PW_SWITCH;
    init_return_orig_type = cfg->return_orig_type = LDAP_OFF;
    init_enable_turbo_mode = cfg->enable_turbo_mode = LDAP_ON;
    init_connection_buffer = cfg->connection_buffer = CONNECTION_BUFFER_ON;
    init_connection_nocanon = cfg->connection_nocanon = LDAP_ON;
    init_plugin_logging = cfg->plugin_logging = LDAP_OFF;
    cfg->listen_backlog_size = DAEMON_LISTEN_SIZE;
    init_ignore_time_skew = cfg->ignore_time_skew = LDAP_OFF;
    init_dynamic_plugins = cfg->dynamic_plugins = LDAP_OFF;
    init_cn_uses_dn_syntax_in_dns = cfg->cn_uses_dn_syntax_in_dns = LDAP_OFF;
    init_global_backend_local = LDAP_OFF;
    cfg->maxsimplepaged_per_conn = SLAPD_DEFAULT_MAXSIMPLEPAGED_PER_CONN;
    cfg->maxbersize = SLAPD_DEFAULT_MAXBERSIZE;
    cfg->logging_backend = slapi_ch_strdup(SLAPD_INIT_LOGGING_BACKEND_INTERNAL);
    cfg->rootdn = slapi_ch_strdup(SLAPD_DEFAULT_DIRECTORY_MANAGER);
    init_enable_nunc_stans = cfg->enable_nunc_stans = LDAP_OFF;
#if defined(LINUX)
#if defined(__GLIBC__)
    cfg->malloc_mxfast = DEFAULT_MALLOC_UNSET;
    cfg->malloc_trim_threshold = DEFAULT_MALLOC_UNSET;
    cfg->malloc_mmap_threshold = DEFAULT_MALLOC_UNSET;
#endif
#endif
    init_extract_pem = cfg->extract_pem = LDAP_ON;
    cfg->referral_check_period = SLAPD_DEFAULT_REFERRAL_CHECK_PERIOD;
    init_return_orig_dn = cfg->return_orig_dn = LDAP_ON;
    /*
     * Default upgrade hash to on - this is an important security step, meaning that old
     * or legacy hashes are upgraded on bind. It means we are proactive in securing accounts
     * that may have infrequent on no password changes (which is current best practice in
     * computer security).
     *
     * A risk is that some accounts may use clear/crypt for other application integrations
     * where the hash is "read" from the account. To avoid this, these two hashes are NEVER
     * upgraded - in other words, "ON" means only MD5, SHA*, are upgraded to the "current"
     * scheme set in cn=config
     */
    init_enable_upgrade_hash = cfg->enable_upgrade_hash = LDAP_ON;
    init_verify_filter_schema = cfg->verify_filter_schema = SLAPI_WARN_SAFE;
    /*
     * Default to enabled ldapssotoken, but if no secret is given we generate one
     * randomly each startup.
     */
    init_enable_ldapssotoken = cfg->enable_ldapssotoken = LDAP_ON;
    cfg->ldapssotoken_secret = fernet_generate_new_key();
    cfg->ldapssotoken_ttl = SLAPD_DEFAULT_LDAPSSOTOKEN_TTL;

    cfg->tcp_fin_timeout = SLAPD_DEFAULT_TCP_FIN_TIMEOUT;
    cfg->tcp_keepalive_time = SLAPD_DEFAULT_TCP_KEEPALIVE_TIME;

    /* Done, unlock!  */
    CFG_UNLOCK_WRITE(cfg);

    /* init the dse file backup lock */
    dse_init_backup_lock();

    init_config_get_and_set();
}


int
g_get_global_lastmod(void)
{
    return config_get_lastmod();
}

int
g_get_slapd_security_on(void)
{
    return config_get_security();
}

static struct snmp_vars_t global_snmp_vars;
static PRUintn thread_private_snmp_vars_idx;
/*
 * https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSPR/Reference/PR_NewThreadPrivateIndex
 * It is called each time:
 *  - PR_SetThreadPrivate is called with a not NULL private value
 *  - on thread exit
 */
static void
snmp_vars_idx_free(void *ptr)
{
    int *idx = ptr;
    if (idx) {
        slapi_ch_free((void **)&idx);
    }
}
/* Define a per thread private area that is used to store
 * in the (workers) thread the index in per_thread_snmp_vars
 * of the set of counters
 */
void
init_thread_private_snmp_vars()
{
    if (PR_NewThreadPrivateIndex(&thread_private_snmp_vars_idx, snmp_vars_idx_free) != PR_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ALERT,
              "init_thread_private_snmp_vars", "Failure to per thread snmp counters !\n");
        PR_ASSERT(0);
    }
}
int
thread_private_snmp_vars_get_idx(void)
{
    int *idx;
    idx = (int *) PR_GetThreadPrivate(thread_private_snmp_vars_idx);
    if (idx == NULL) {
        /* if it was not initialized set it to zero */
        return 0;
    }
    return *idx;
}
void
thread_private_snmp_vars_set_idx(int32_t idx)
{
    int *val;
    val = (int32_t *) PR_GetThreadPrivate(thread_private_snmp_vars_idx);
    if (val == NULL) {
        /* if it was not initialized set it to zero */
        val = (int *) slapi_ch_calloc(1, sizeof(int32_t));
        PR_SetThreadPrivate(thread_private_snmp_vars_idx, (void *) val);
    }
    *val = idx;
}

static struct snmp_vars_t *per_thread_snmp_vars = NULL; /* array of counters */
static int max_slots_snmp_vars = 0;                     /* no slots array of counters */
struct snmp_vars_t *
g_get_per_thread_snmp_vars(void)
{
    int thread_vars = thread_private_snmp_vars_get_idx();
    if (thread_vars < 0 || thread_vars >= max_slots_snmp_vars) {
        /* fallback to the global one */
        thread_vars = 0;
    }
    return &per_thread_snmp_vars[thread_vars];
}


struct snmp_vars_t *
g_get_first_thread_snmp_vars(int *cookie)
{
    *cookie = 0;
    if (max_slots_snmp_vars == 0) {
        /* not yet initialized */
        return NULL;
    }
    return &per_thread_snmp_vars[0];
}

struct snmp_vars_t *
g_get_next_thread_snmp_vars(int *cookie)
{
    int index = *cookie;
    if (index < 0 || index >= (max_slots_snmp_vars - 1)) {
        return NULL;
    }
    *cookie = index + 1;
    return &per_thread_snmp_vars[index + 1];
}

/* Allocated the first slot of arrays of counters
 * The first slot contains counters that are not specific to counters
 */
void
alloc_global_snmp_vars()
{
    PR_ASSERT(max_slots_snmp_vars == 0);
    if (max_slots_snmp_vars == 0) {
        max_slots_snmp_vars = 1;
        per_thread_snmp_vars = (struct snmp_vars_t *) slapi_ch_calloc(max_slots_snmp_vars, sizeof(struct snmp_vars_t));
    }

}

/* Allocated the next slots of the arrays of counters
 * with a slot per worker thread
 */
void
alloc_per_thread_snmp_vars(int32_t maxthread)
{
    PR_ASSERT(max_slots_snmp_vars == 1);
    if (max_slots_snmp_vars == 1) {
        max_slots_snmp_vars += maxthread; /* one extra slot for the global counters */
        per_thread_snmp_vars = (struct snmp_vars_t *) slapi_ch_realloc((char *) per_thread_snmp_vars,
                                                                       max_slots_snmp_vars * sizeof (struct snmp_vars_t));

        /* make sure to zeroed the new alloacted counters */
        memset(&per_thread_snmp_vars[1], 0, (max_slots_snmp_vars - 1) * sizeof (struct snmp_vars_t));
    }
}

struct snmp_vars_t *
g_get_global_snmp_vars(void)
{
    return &global_snmp_vars;
}

static slapdEntryPoints *sep = NULL;
void
set_dll_entry_points(slapdEntryPoints *p)
{
    if (NULL == sep) {
        sep = p;
    }
}


int
get_entry_point(int ep_name, caddr_t *ep_addr)
{
    int rc = 0;

    if (sep != NULL) {
        switch (ep_name) {
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
    } else {
        rc = -1;
    }
    return rc;
}

int32_t
config_set_auditlog_unhashed_pw(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->auditlog_logging_hide_unhashed_pw),
                              errorbuf, apply);
    if (strcasecmp(value, "on") == 0) {
        auditlog_hide_unhashed_pw();
    } else {
        auditlog_expose_unhashed_pw();
    }
    return retVal;
}

int32_t
config_set_auditfaillog_unhashed_pw(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->auditfaillog_logging_hide_unhashed_pw),
                              errorbuf, apply);
    if (strcasecmp(value, "on") == 0) {
        auditfaillog_hide_unhashed_pw();
    } else {
        auditfaillog_expose_unhashed_pw();
    }
    return retVal;
}

#ifdef HAVE_CLOCK_GETTIME
int32_t
config_set_logging_hr_timestamps(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->logging_hr_timestamps),
                              errorbuf, apply);
    if (apply && retVal == LDAP_SUCCESS) {
        if (strcasecmp(value, "on") == 0) {
            log_enable_hr_timestamps();
        } else {
            log_disable_hr_timestamps();
        }
    }
    return retVal;
}
#endif

/*
 * Utility function called by many of the config_set_XXX() functions.
 * Returns a non-zero value if 'value' is NULL and zero if not.
 * Also constructs an error message in 'errorbuf' if value is NULL.
 * If or_zero_length is non-zero, zero length values are treated as
 * equivalent to NULL (i.e., they will cause a non-zero value to be
 * returned by this function).
 */
static int
config_value_is_null(const char *attrname, const char *value, char *errorbuf, int or_zero_length)
{
    if (NULL == value || (or_zero_length && *value == '\0')) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: deleting the value is not allowed.", attrname);
        return 1;
    }

    return 0;
}

int32_t
config_set_auditlog_display_attrs(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&slapdFrontendConfig->auditlog_display_attrs);
        slapdFrontendConfig->auditlog_display_attrs = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

char *
config_get_auditlog_display_attrs()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->auditlog_display_attrs);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_ignore_vattrs(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->ignore_vattrs), errorbuf, apply);

    return retVal;
}

int32_t
config_set_sasl_mapping_fallback(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->sasl_mapping_fallback), errorbuf, apply);

    return retVal;
}

int32_t
config_set_disk_monitoring(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->disk_monitoring),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_set_disk_threshold_readonly(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->disk_threshold_readonly),
                              errorbuf, apply);
    return retVal;
}


int
config_set_disk_threshold(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    PRInt64 threshold = 0;
    char *endp = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    threshold = strtoll(value, &endp, 10);
    if (*endp != '\0' || threshold < 4096 || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid, threshold must be greater than or equal to 4096 and less then %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->disk_threshold = (uint64_t)threshold;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int32_t
config_set_disk_logging_critical(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->disk_logging_critical),
                              errorbuf, apply);
    return retVal;
}

int
config_set_disk_grace_period(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    int period = 0;
    char *endp = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    period = strtol(value, &endp, 10);
    if (*endp != '\0' || period < 1 || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid, grace period must be at least 1 minute", attrname, value);
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

int32_t
config_set_ndn_cache_enabled(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->ndn_cache_enabled), errorbuf, apply);

    return retVal;
}

int
config_set_ndn_cache_max_size(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    char *endp;
    long size;

    size = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return retVal;
    }

    if (size < 0) {
        size = 0; /* same as -1 */
    }
    if (size > 0 && size < 1024000) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "ndn_cache_max_size too low(%d), changing to %d bytes.\n", (int)size, NDN_DEFAULT_SIZE);
        size = NDN_DEFAULT_SIZE;
    }
    if (apply) {
        slapi_atomic_store_64(&(slapdFrontendConfig->ndn_cache_max_size), size, __ATOMIC_RELEASE);
    }

    return retVal;
}

int
config_set_sasl_maxbufsize(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    long default_size = SLAPD_DEFAULT_SASL_MAXBUFSIZE;
    long size;
    char *endp;

    size = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return retVal;
    }

    if (size < default_size) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "nsslapd-sasl-max-buffer-size is too low (%ld), setting to default value (%ld).\n",
                              size, default_size);
        size = default_size;
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->sasl_max_bufsize = size;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int32_t
config_set_return_orig_type_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->return_orig_type), errorbuf, apply);

    return retVal;
}

int
config_set_port(const char *attrname, char *port, char *errorbuf, int apply)
{
    long nPort;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;

    if (config_value_is_null(attrname, port, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nPort = strtol(port, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || nPort > LDAP_PORT_MAX || nPort < 0) {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid, ports must range from 0 to %d", attrname, port, LDAP_PORT_MAX);
        return retVal;
    }

    if (nPort == 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "config_set_port", "Non-Secure Port Disabled\n");
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->port = nPort;
        /*    n_port = nPort; */

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}


int
config_set_secureport(const char *attrname, char *port, char *errorbuf, int apply)
{
    long nPort;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;

    if (config_value_is_null(attrname, port, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nPort = strtol(port, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || nPort > LDAP_PORT_MAX || nPort <= 0) {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid, ports must range from 1 to %d", attrname, port, LDAP_PORT_MAX);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->secureport = nPort;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}


int32_t
config_set_tls_check_crl(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    /* Default */
    tls_check_crl_t state = TLS_CHECK_NONE;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (strcasecmp(value, "none") == 0) {
        state = TLS_CHECK_NONE;
    } else if (strcasecmp(value, "peer") == 0) {
        state = TLS_CHECK_PEER;
    } else if (strcasecmp(value, "all") == 0) {
        state = TLS_CHECK_ALL;
    } else {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: unsupported value: %s", attrname, value);
    }

    if (retVal == LDAP_SUCCESS && apply) {
        slapi_atomic_store_32((int32_t *)&(slapdFrontendConfig->tls_check_crl), state, __ATOMIC_RELEASE);
    }

    return retVal;
}


int
config_set_SSLclientAuth(const char *attrname, char *value, char *errorbuf, int apply)
{

    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }
    /* first check the value, return an error if it's invalid */
    else if (strcasecmp(value, "off") != 0 &&
             strcasecmp(value, "allowed") != 0 &&
             strcasecmp(value, "required") != 0) {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: unsupported value: %s", attrname, value);
        return retVal;
    } else if (!apply) {
        /* return success now, if we aren't supposed to apply the change */
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    if (!strcasecmp(value, "off")) {
        slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_OFF;
    } else if (!strcasecmp(value, "allowed")) {
        slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_ALLOWED;
    } else if (!strcasecmp(value, "required")) {
        slapdFrontendConfig->SSLclientAuth = SLAPD_SSLCLIENTAUTH_REQUIRED;
    } else {
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: unsupported value: %s", attrname, value);
    }

    CFG_UNLOCK_WRITE(slapdFrontendConfig);

    return retVal;
}


int32_t
config_set_ssl_check_hostname(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->ssl_check_hostname),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_localhost(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->localhost));
        slapdFrontendConfig->localhost = slapi_ch_strdup(value);

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_listenhost(const char *attrname __attribute__((unused)), char *value, char *errorbuf __attribute__((unused)), int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->listenhost));
        slapdFrontendConfig->listenhost = slapi_ch_strdup(value);

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_snmp_index(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long snmp_index;
    long snmp_index_disable;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    snmp_index_disable = SLAPD_DEFAULT_SNMP_INDEX; /* if snmp index is disabled, use the nsslapd-port instead */
    ;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        snmp_index = snmp_index_disable;
    } else {
        errno = 0;
        snmp_index = strtol(value, &endp, 10);

        if (*endp != '\0' || errno == ERANGE || snmp_index < snmp_index_disable) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: invalid value \"%s\", %s must be greater or equal to %lu (%lu means disabled)",
                                  attrname, value, CONFIG_SNMP_INDEX_ATTRIBUTE, snmp_index_disable, snmp_index_disable);
            retVal = LDAP_OPERATIONS_ERROR;
        }
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->snmp_index = snmp_index;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_ldapi_filename(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    /*
     * LDAPI file path length is limited by sizeof((*ports_info.i_listenaddr)->local.path))
     * which is set in main.c inside of "#if defined(ENABLE_LDAPI)" block
     * ports_info.i_listenaddr is sizeof(PRNetAddr) and our required sizes is 8 bytes less
     */
    size_t result_size = sizeof(PRNetAddr) - 8;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (strlen(value) >= result_size) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, its length must be less than %d",
                              attrname, value, result_size);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_filename));
        slapdFrontendConfig->ldapi_filename = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int32_t
config_set_ldapi_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->ldapi_switch),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_ldapi_bind_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->ldapi_bind_switch),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_ldapi_root_dn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_log_err(SLAPI_LOG_WARNING, "config_set_ldapi_root_dn",
                "The \"nsslapd-ldapimaprootdn\" setting is obsolete and kept for compatibility reasons. "
                "For LDAPI configuration, \"nsslapd-rootdn\" is used instead.\n");
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_root_dn));
        slapdFrontendConfig->ldapi_root_dn = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int32_t
config_set_ldapi_map_entries(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->ldapi_map_entries),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_ldapi_uidnumber_type(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_uidnumber_type));
        slapdFrontendConfig->ldapi_uidnumber_type = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_ldapi_gidnumber_type(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_gidnumber_type));
        slapdFrontendConfig->ldapi_gidnumber_type = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_ldapi_search_base_dn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_search_base_dn));
        slapdFrontendConfig->ldapi_search_base_dn = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_ldapi_mapping_base_dn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        /* Make sure value is NULL in this case */
        value = NULL;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&(slapdFrontendConfig->ldapi_auto_mapping_base));
        slapdFrontendConfig->ldapi_auto_mapping_base = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

char *
config_get_ldapi_mapping_base_dn(void)
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_auto_mapping_base);
    CFG_UNLOCK_READ(slapdFrontendConfig);
    return retVal;
}

#if defined(ENABLE_AUTO_DN_SUFFIX)
int
config_set_ldapi_auto_dn_suffix(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->ldapi_auto_dn_suffix));
        slapdFrontendConfig->ldapi_auto_dn_suffix = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}
#endif

int
config_set_anon_limits_dn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->anon_limits_dn));
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
int32_t
config_set_slapi_counters(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->slapi_counters), errorbuf, apply);

    return retVal;
}

int
config_set_securelistenhost(const char *attrname __attribute__((unused)), char *value, char *errorbuf __attribute__((unused)), int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->securelistenhost));
        slapdFrontendConfig->securelistenhost = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_srvtab(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&(slapdFrontendConfig->srvtab));
        ldap_srvtab = slapi_ch_strdup(value);
        slapdFrontendConfig->srvtab = slapi_ch_strdup(value);

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_sizelimit(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long sizelimit;
    char *endp = NULL;
    Slapi_Backend *be;
    char *cookie;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    sizelimit = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || sizelimit < -1) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: \"%s\" is invalid, sizelimit must range from -1 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {

        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->sizelimit = sizelimit;
        g_set_defsize(sizelimit);
        cookie = NULL;
        be = slapi_get_first_backend(&cookie);
        while (be) {
            be->be_sizelimit = slapdFrontendConfig->sizelimit;
            be = slapi_get_next_backend(cookie);
        }

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&cookie);
    }
    return retVal;
}

int
config_set_pagedsizelimit(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long pagedsizelimit;
    char *endp = NULL;
    Slapi_Backend *be;
    char *cookie;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    pagedsizelimit = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || pagedsizelimit < -1) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid, pagedsizelimit must range from -1 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {

        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pagedsizelimit = pagedsizelimit;
        cookie = NULL;
        be = slapi_get_first_backend(&cookie);
        while (be) {
            be->be_pagedsizelimit = slapdFrontendConfig->pagedsizelimit;
            be = slapi_get_next_backend(cookie);
        }

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&cookie);
    }
    return retVal;
}

int
config_set_pw_storagescheme(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct pw_scheme *new_scheme = NULL;
    char *scheme_list = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);

    new_scheme = pw_name2scheme(value);
    if (new_scheme == NULL) {
        if (scheme_list != NULL) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid scheme - %s. Valid schemes are: %s",
                                  attrname, value, scheme_list);
        } else {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "%s: invalid scheme - %s (no pwdstorage scheme plugin loaded)",
                                  attrname, value);
        }
        retVal = LDAP_OPERATIONS_ERROR;
        slapi_ch_free_string(&scheme_list);
        return retVal;
    } else if (new_scheme->pws_enc == NULL) {
        /* For example: the NS-MTA-MD5 password scheme is for comparision only and for backward
         * compatibility with an Old Messaging Server that was setting passwords in the
         * directory already encrypted. The scheme cannot and don't encrypt password if
         * they are in clear. We don't take it
         */

        if (scheme_list) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "pw_storagescheme: invalid encoding scheme - %s\nValid values are: %s\n", value, scheme_list);
        }
        retVal = LDAP_UNWILLING_TO_PERFORM;
        slapi_ch_free_string(&scheme_list);
        free_pw_scheme(new_scheme);
        return retVal;
    }

    if (apply) {
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


int32_t
config_set_pw_change(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_change),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_pw_history(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_history),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_pw_must_change(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_must_change),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_pwpolicy_local(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pwpolicy_local),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_pwpolicy_inherit_global(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pwpolicy_inherit_global),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_allow_hashed_pw(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->allow_hashed_pw),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_syntax(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_syntax),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_palindrome(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_palindrome),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_dict_check(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_check_dict),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_dict_path(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        value = NULL;
    } else {
        /* We have a value, do some basic checks */
        if (value[0] != '/') {
            /* Not a path - error */
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "password dictionary path \"%s\" is invalid.", value);
            retVal = LDAP_OPERATIONS_ERROR;
            return retVal;
        }
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&slapdFrontendConfig->pw_policy.pw_dict_path);
        slapdFrontendConfig->pw_policy.pw_dict_path = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

char **
config_get_pw_user_attrs_array(void)
{
    /*
     * array of password user attributes. If is null, returns NULL thanks to ch_array_dup.
     * Caller must free!
     */
    char **retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_array_dup(slapdFrontendConfig->pw_policy.pw_cmp_attrs_array);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_pw_user_attrs(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        value = NULL;
    }
    if (apply) {
        /* During a reset, the value is "", so we have to handle this case. */
        if (value && strcmp(value, "") != 0) {
            char **nval_array;
            char *nval = slapi_ch_strdup(value);
            /* A separate variable is used because slapi_str2charray_ext can change it and nval'd become corrupted */
            char *tmp_array_nval = slapi_ch_strdup(nval);

            /* We should accept comma-separated lists but slapi_str2charray_ext will process only space-separated */
            replace_char(tmp_array_nval, ',', ' ');
            /* Take list of attributes and break it up into a char array */
            nval_array = slapi_str2charray_ext(tmp_array_nval, " ", 0);
            slapi_ch_free_string(&tmp_array_nval);

            CFG_LOCK_WRITE(slapdFrontendConfig);
            slapi_ch_free_string(&slapdFrontendConfig->pw_policy.pw_cmp_attrs);
            slapi_ch_array_free(slapdFrontendConfig->pw_policy.pw_cmp_attrs_array);
            slapdFrontendConfig->pw_policy.pw_cmp_attrs = nval;
            slapdFrontendConfig->pw_policy.pw_cmp_attrs_array = nval_array;
            CFG_UNLOCK_WRITE(slapdFrontendConfig);
        } else {
            CFG_LOCK_WRITE(slapdFrontendConfig);
            slapi_ch_free_string(&slapdFrontendConfig->pw_policy.pw_cmp_attrs);
            slapi_ch_array_free(slapdFrontendConfig->pw_policy.pw_cmp_attrs_array);
            slapdFrontendConfig->pw_policy.pw_cmp_attrs = NULL;
            slapdFrontendConfig->pw_policy.pw_cmp_attrs_array = NULL;
            CFG_UNLOCK_WRITE(slapdFrontendConfig);
         }
    }
    return retVal;
}

char **
config_get_pw_bad_words_array(void)
{
    /*
     * array of words to reject. If is null, returns NULL thanks to ch_array_dup.
     * Caller must free!
     */
    char **retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_array_dup(slapdFrontendConfig->pw_policy.pw_bad_words_array);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_pw_bad_words(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        value = NULL;
    }
    if (apply) {
        /* During a reset, the value is "", so we have to handle this case. */
        if (value && strcmp(value, "") != 0) {
            char **nval_array;
            char *nval = slapi_ch_strdup(value);
            /* A separate variable is used because slapi_str2charray_ext can change it and nval'd become corrupted */
            char *tmp_array_nval = slapi_ch_strdup(nval);

            /* We should accept comma-separated lists but slapi_str2charray_ext will process only space-separated */
            replace_char(tmp_array_nval, ',', ' ');
            /* Take list of attributes and break it up into a char array */
            nval_array = slapi_str2charray_ext(tmp_array_nval, " ", 0);
            slapi_ch_free_string(&tmp_array_nval);

            CFG_LOCK_WRITE(slapdFrontendConfig);
            slapi_ch_free_string(&slapdFrontendConfig->pw_policy.pw_bad_words);
            slapi_ch_array_free(slapdFrontendConfig->pw_policy.pw_bad_words_array);
            slapdFrontendConfig->pw_policy.pw_bad_words = nval;
            slapdFrontendConfig->pw_policy.pw_bad_words_array = nval_array;
            CFG_UNLOCK_WRITE(slapdFrontendConfig);
        } else {
            CFG_LOCK_WRITE(slapdFrontendConfig);
            slapi_ch_free_string(&slapdFrontendConfig->pw_policy.pw_bad_words);
            slapi_ch_array_free(slapdFrontendConfig->pw_policy.pw_bad_words_array);
            slapdFrontendConfig->pw_policy.pw_bad_words = NULL;
            slapdFrontendConfig->pw_policy.pw_bad_words_array = NULL;
            CFG_UNLOCK_WRITE(slapdFrontendConfig);
         }
    }
    return retVal;
}

int32_t
config_set_pw_max_seq(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t max = 0;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        max = 0;
    } else {
        errno = 0;
        max = (int32_t)strtol(value, &endp, 10);

        if (*endp != '\0' || errno == ERANGE || max < 0 || max > 10) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "password maximum sequence \"%s\" is invalid. The range is from 0 to 10.", value);
            retVal = LDAP_OPERATIONS_ERROR;
            return retVal;
        }
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_max_seq = max;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}


int32_t
config_set_pw_max_seq_sets(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t max = 0;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        max = 0;
    } else {
        errno = 0;
        max = (int32_t)strtol(value, &endp, 10);

        if (*endp != '\0' || errno == ERANGE || max < 0 || max > 10) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                  "password maximum sequence sets \"%s\" is invalid. The range is from 0 to 10.", value);
            retVal = LDAP_OPERATIONS_ERROR;
            return retVal;
        }
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_seq_char_sets = max;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int32_t
config_set_pw_max_class_repeats(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t max = 0;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        max = 0;
    } else {
        errno = 0;
        max = (int32_t)strtol(value, &endp, 10);

        if (*endp != '\0' || errno == ERANGE || max < 0 || max > 1024) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                    "password maximum repated characters per characters class \"%s\" is invalid. "
                    "The range is from 0 to 1024.", value);
            retVal = LDAP_OPERATIONS_ERROR;
            return retVal;
        }
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_max_class_repeats = max;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_pw_minlength(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minLength = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minLength = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minLength < 2 || minLength > 512) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum length \"%s\" is invalid. The minimum length must range from 2 to 512.", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minlength = minLength;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_mindigits(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minDigits = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minDigits = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minDigits < 0 || minDigits > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of digits \"%s\" is invalid. "
                              "The minimum number of digits must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mindigits = minDigits;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_minalphas(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minAlphas = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minAlphas = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minAlphas < 0 || minAlphas > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of alphas \"%s\" is invalid. "
                              "The minimum number of alphas must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minalphas = minAlphas;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_minuppers(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minUppers = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minUppers = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minUppers < 0 || minUppers > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of uppercase characters \"%s\" is invalid. "
                              "The minimum number of uppercase characters must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minuppers = minUppers;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_minlowers(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minLowers = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minLowers = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minLowers < 0 || minLowers > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of lowercase characters \"%s\" is invalid. "
                              "The minimum number of lowercase characters must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minlowers = minLowers;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_minspecials(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minSpecials = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minSpecials = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minSpecials < 0 || minSpecials > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of special characters \"%s\" is invalid. "
                              "The minimum number of special characters must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_minspecials = minSpecials;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_min8bit(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long min8bit = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    min8bit = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || min8bit < 0 || min8bit > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of 8-bit characters \"%s\" is invalid. "
                              "The minimum number of 8-bit characters must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_min8bit = min8bit;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_maxrepeats(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long maxRepeats = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    maxRepeats = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || maxRepeats < 0 || maxRepeats > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password maximum number of repeated characters \"%s\" is invalid. "
                              "The maximum number of repeated characters must range from 0 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_maxrepeats = maxRepeats;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_mincategories(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minCategories = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minCategories = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minCategories < 1 || minCategories > 5) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum number of categories \"%s\" is invalid. "
                              "The minimum number of categories must range from 1 to 5.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mincategories = minCategories;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_mintokenlength(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long minTokenLength = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minTokenLength = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || minTokenLength < 1 || minTokenLength > 64) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password minimum token length \"%s\" is invalid. "
                              "The minimum token length must range from 1 to 64.",
                              value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_mintokenlength = minTokenLength;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_maxfailure(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long maxFailure = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    maxFailure = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || maxFailure <= 0 || maxFailure > 32767) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password maximum retry \"%s\" is invalid. Password maximum failure must range from 1 to 32767", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_maxfailure = maxFailure;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}


int
config_set_pw_inhistory(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    int64_t history = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    history = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || history < 0 || history > 24) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password history length \"%s\" is invalid. The password history must range from 0 to 24", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_inhistory = history;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}


int
config_set_pw_lockduration(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    time_t duration = 0; /* in seconds */

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    /* in seconds */
    duration = parse_duration_time_t(value);

    /*
    * If the duration set is larger than time_t max - current time, we probably have
    * made it to the heat death of the universe. Congratulations on finding this bug.
    */
    if (errno == ERANGE || duration <= 0 || duration > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "password lockout duration \"%s\" is invalid. ", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        slapdFrontendConfig->pw_policy.pw_lockduration = duration;
    }

    return retVal;
}


int
config_set_pw_resetfailurecount(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    time_t duration = 0; /* in seconds */

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    /* in seconds */
    duration = parse_duration_time_t(value);

    if (errno == ERANGE || duration <= 0 || duration > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "password reset count duration \"%s\" is invalid. ", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        slapdFrontendConfig->pw_policy.pw_resetfailurecount = duration;
    }

    return retVal;
}

int
config_set_pw_tpr_maxuse(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    char *endp = NULL;
    int maxUse = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    maxUse = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || maxUse < -1 || maxUse > 255) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password TPR maximum use \"%s\" is invalid. A One Time password maximum use must range from 0 to 255. -1 is disabled", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_tpr_maxuse = maxUse;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_pw_tpr_delay_expire_at(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    char *endp = NULL;
    int expire_at = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    expire_at = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || expire_at < -1 || expire_at > (7 * 24 * 3600)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password TPR delay of validity \"%s\" is invalid. Delay, after reset, TPR starts to be valid is 0 to 1 week (In seconds). -1 is disabled", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_tpr_delay_expire_at = expire_at;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}
int
config_set_pw_tpr_delay_valid_from(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    char *endp = NULL;
    int ValidDelay = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    ValidDelay = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || ValidDelay < -1 || ValidDelay > (7 * 24 * 3600)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password TPR delay of validity \"%s\" is invalid. Delay, after reset, TPR starts to be valid is 0 to 1 week (In seconds). -1 is disabled", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->pw_policy.pw_tpr_delay_valid_from = ValidDelay;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int32_t
config_set_pw_is_global_policy(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_is_global_policy),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_is_legacy_policy(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_is_legacy),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_pw_admin_dn(const char *attrname __attribute__((unused)), char *value, char *errorbuf __attribute__((unused)), int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_sdn_free(&slapdFrontendConfig->pw_policy.pw_admin);
        slapdFrontendConfig->pw_policy.pw_admin = slapi_sdn_new_dn_byval(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int32_t
config_set_pw_admin_skip_info(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_admin_skip_info),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_track_last_update_time(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_track_update_time),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_exp(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_exp),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_send_expiring(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_send_expiring),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_unlock(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_unlock),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_pw_lockout(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->pw_policy.pw_lockout),
                              errorbuf,
                              apply);

    return retVal;
}


int
config_set_pw_gracelimit(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long gracelimit = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    gracelimit = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || gracelimit < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "password grace limit \"%s\" is invalid, password grace limit must range from 0 to %lld",
                              value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        slapdFrontendConfig->pw_policy.pw_gracelimit = gracelimit;

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}


int32_t
config_set_lastmod(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    Slapi_Backend *be = NULL;
    char *cookie;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->lastmod),
                              errorbuf,
                              apply);

    if (retVal == LDAP_SUCCESS && apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);

        cookie = NULL;
        be = slapi_get_first_backend(&cookie);
        while (be) {
            be->be_lastmod = slapdFrontendConfig->lastmod;
            be = slapi_get_next_backend(cookie);
        }

        CFG_UNLOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&cookie);
    }

    return retVal;
}


int32_t
config_set_nagle(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->nagle),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_accesscontrol(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->accesscontrol),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_return_exact_case(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->return_exact_case),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_result_tweak(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->result_tweak),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_plugin_tracking(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->plugin_track),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_moddn_aci(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->moddn_aci),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_targetfilter_cache(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->targetfilter_cache),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_dynamic_plugins(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->dynamic_plugins),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_get_dynamic_plugins(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->dynamic_plugins), __ATOMIC_ACQUIRE);

}

int32_t
config_set_cn_uses_dn_syntax_in_dns(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->cn_uses_dn_syntax_in_dns),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_get_cn_uses_dn_syntax_in_dns()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->cn_uses_dn_syntax_in_dns), __ATOMIC_ACQUIRE);
}

int32_t
config_set_security(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->security),
                              errorbuf,
                              apply);

    return retVal;
}

static int32_t
config_set_onoff(const char *attrname, char *value, int32_t *configvalue, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapi_onoff_t newval = -1;

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (strcasecmp(value, "on") && strcasecmp(value, "off")) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"on\" or \"off\".", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        /* we can return now if we aren't applying the changes */
        return retVal;
    }

    if (strcasecmp(value, "on") == 0) {
        newval = LDAP_ON;
    } else if (strcasecmp(value, "off") == 0) {
        newval = LDAP_OFF;
    }

    slapi_atomic_store_32(configvalue, newval, __ATOMIC_RELEASE);

    return retVal;
}

int32_t
config_set_readonly(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->readonly),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_schemacheck(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->schemacheck),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_schemamod(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->schemamod),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_syntaxcheck(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->syntaxcheck),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_syntaxlogging(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->syntaxlogging),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_dn_validate_strict(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->dn_validate_strict),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_ds4_compatible_schema(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->ds4_compatible_schema),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_schema_ignore_trailing_spaces(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->schema_ignore_trailing_spaces),
                              errorbuf,
                              apply);

    return retVal;
}


int32_t
config_set_enquote_sup_oc(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->enquote_sup_oc),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_rootdn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->rootdn));
        slapdFrontendConfig->rootdn = slapi_dn_normalize(slapi_ch_strdup(value));
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_rootpw(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct pw_scheme *is_hashed = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    slapi_ch_free((void **)&(slapdFrontendConfig->rootpw));

    is_hashed = pw_val2scheme(value, NULL, 0);

    if (is_hashed) {
        slapdFrontendConfig->rootpw = slapi_ch_strdup(value);
        free_pw_scheme(is_hashed);
    } else if (slapd_nss_is_initialized() ||
               (strcasecmp(slapdFrontendConfig->rootpwstoragescheme->pws_name,
                           "clear") == 0)) {
        /* to hash, security library should have been initialized, by now */
        /* pwd enc func returns slapi_ch_malloc memory */
        slapdFrontendConfig->rootpw = (slapdFrontendConfig->rootpwstoragescheme->pws_enc)(value);
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: password scheme mismatch (passwd scheme is %s; password is clear text)",
                              attrname, slapdFrontendConfig->rootpwstoragescheme->pws_name);
        retVal = LDAP_PARAM_ERROR;
    }

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}


int
config_set_rootpwstoragescheme(const char *attrname, char *value, char *errorbuf, int apply __attribute__((unused)))
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct pw_scheme *new_scheme = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    new_scheme = pw_name2scheme(value);
    if (new_scheme == NULL) {
        if (errorbuf) {
            char *scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
            if (scheme_list) {
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid scheme - %s. Valid schemes are: %s",
                                      attrname, value, scheme_list);
            } else {
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                      "%s: invalid scheme - %s (no pwdstorage scheme plugin loaded)", attrname, value);
            }
            slapi_ch_free_string(&scheme_list);
        }
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
int
config_set_storagescheme(void)
{

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct pw_scheme *new_scheme = NULL;

    CFG_LOCK_WRITE(slapdFrontendConfig);

    new_scheme = pw_name2scheme(DEFAULT_PASSWORD_SCHEME_NAME);
    free_pw_scheme(slapdFrontendConfig->pw_storagescheme);
    slapdFrontendConfig->pw_storagescheme = new_scheme;

    new_scheme = pw_name2scheme(DEFAULT_PASSWORD_SCHEME_NAME);
    slapdFrontendConfig->rootpwstoragescheme = new_scheme;

    CFG_UNLOCK_WRITE(slapdFrontendConfig);

    return (new_scheme == NULL);
}


int
config_set_localuser(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        struct passwd *pw = NULL;
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&slapdFrontendConfig->localuser);
        slapdFrontendConfig->localuser = slapi_ch_strdup(value);
        if (slapdFrontendConfig->localuserinfo != NULL) {
            slapi_ch_free((void **)&(slapdFrontendConfig->localuserinfo));
        }
        pw = getpwnam(value);
        if (pw) {
            slapdFrontendConfig->localuserinfo =
                (struct passwd *)slapi_ch_malloc(sizeof(struct passwd));
            memcpy(slapdFrontendConfig->localuserinfo, pw, sizeof(struct passwd));
        }

        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_workingdir(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (PR_Access(value, PR_ACCESS_EXISTS) != 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Working directory \"%s\" does not exist.", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }
    if (PR_Access(value, PR_ACCESS_WRITE_OK) != 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Working directory \"%s\" is not writeable.", value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->workingdir = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

/* alias of encryption key and certificate files is now retrieved through */
/* calls to psetFullCreate() and psetGetAttrSingleValue(). See ssl.c, */
/* where this function is still used to set the global variable */
int
config_set_encryptionalias(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->encryptionalias));

        slapdFrontendConfig->encryptionalias = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_threadnumber(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t threadnum = 0;
    int32_t hw_threadnum = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    threadnum = strtol(value, &endp, 10);

    /* Means we want to re-run the hardware detection. */
    hw_threadnum = util_get_hardware_threads();
    if (threadnum == -1) {
        threadnum = hw_threadnum;
    } else {
        /*
         * Log a message if the user defined thread number is very different
         * from the hardware threads as this is probably not the optimal
         * value.
         */
        if (threadnum >= hw_threadnum) {
            if (threadnum > MIN_THREADS && threadnum / hw_threadnum >= 4) {
                /* We're over the default minimum and way higher than the hw
                 * threads. */
                slapi_log_err(SLAPI_LOG_NOTICE, "config_set_threadnumber",
                        "The configured thread number (%d) is significantly "
                        "higher than the number of hardware threads (%d).  "
                        "This can potentially hurt server performance.  If "
                        "you are unsure how to tune \"nsslapd-threadnumber\" "
                        "then set it to \"-1\" and the server will tune it "
                        "according to the system hardware\n",
                        threadnum, hw_threadnum);
            }
        } else if (threadnum < MIN_THREADS) {
            /* The thread number should never be less than the minimum and
             * hardware threads. */
            slapi_log_err(SLAPI_LOG_WARNING, "config_set_threadnumber",
                    "The configured thread number (%d) is lower than the number "
                    "of hardware threads (%d).  This will hurt server performance.  "
                    "If you are unsure how to tune \"nsslapd-threadnumber\" then "
                    "set it to \"-1\" and the server will tune it according to the "
                    "system hardware\n",
                    threadnum, hw_threadnum);
            }
    }

    if (*endp != '\0' || errno == ERANGE || threadnum < 1 || threadnum > 65535) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", maximum thread number must range from 1 to 65535", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }
    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->threadnumber), threadnum, __ATOMIC_RELAXED);
    }
    return retVal;
}

int
config_set_maxthreadsperconn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t maxthreadnum = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    maxthreadnum = (int32_t)strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || maxthreadnum < 1 || maxthreadnum > 65535) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", maximum thread number per connection must range from 1 to 65535",
                              attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->maxthreadsperconn), maxthreadnum, __ATOMIC_RELEASE);
    }
    return retVal;
}

int32_t
config_set_maxdescriptors(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    int64_t nValue = 0;
    int64_t maxVal = SLAPD_DEFAULT_MAXDESCRIPTORS;
    struct rlimit rlp;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (0 == getrlimit(RLIMIT_NOFILE, &rlp)) {
        if ((int64_t)rlp.rlim_max < maxVal) {
            maxVal = (int64_t)rlp.rlim_max;
        }
    }

    errno = 0;
    nValue = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nValue < 1 || nValue > maxVal) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", maximum file descriptors must range from 1 to %d (the current process limit). "
                              "Server will use a setting of %d.",
                              attrname, value, maxVal, maxVal);
        if (nValue > maxVal) {
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

int
config_set_reservedescriptors(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int maxVal = 65535;
    long nValue = 0;
    char *endp = NULL;
    struct rlimit rlp;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (0 == getrlimit(RLIMIT_NOFILE, &rlp)) {
        maxVal = (int)rlp.rlim_max;
    }

    errno = 0;
    nValue = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nValue < 1 || nValue > maxVal) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", reserved file descriptors must range from 1 to %d (the current process maxdescriptors limit). "
                              "Server will use a setting of %d.",
                              attrname, value, maxVal, maxVal);
        if (nValue > maxVal) {
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
config_set_num_listeners(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long nValue = 0;
    int minVal = 1;
    int maxVal = 4;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nValue = strtol(value, &endp, 0);
    if (*endp != '\0' || errno == ERANGE || nValue < minVal || nValue > maxVal) {
        nValue = (nValue < minVal) ? minVal : maxVal;
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                "%s: invalid value \"%s\", %s must range from %d to %d. "
                                "Server will use a setting of %d.",
                                CONFIG_NUM_LISTENERS_ATTRIBUTE, attrname, value, minVal, maxVal, nValue);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->num_listeners = nValue;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_ioblocktimeout(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int32_t nValue = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nValue = (int32_t)strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nValue < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", I/O block timeout must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->ioblocktimeout), nValue, __ATOMIC_RELEASE);
    }
    return retVal;
}


int
config_set_idletimeout(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long nValue = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nValue = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nValue < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: invalid value \"%s\", idle timeout must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->idletimeout = nValue;
        /*    g_idle_timeout= nValue; */
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}


int
config_set_groupevalnestlevel(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long nValue = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nValue = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nValue < 0 || nValue > 5) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", group eval nest level must range from 0 to 5", attrname, value);
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
config_set_defaultreferral(const char *attrname, struct berval **value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, (char *)value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        g_set_default_referral(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_userat(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->userat));
        slapdFrontendConfig->userat = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_timelimit(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long nVal = 0;
    char *endp = NULL;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    Slapi_Backend *be = NULL;
    char *cookie;

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    nVal = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || nVal < -1) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", time limit must range from -1 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        g_set_deftime(nVal);
        slapdFrontendConfig->timelimit = nVal;
        be = slapi_get_first_backend(&cookie);
        while (be) {
            be->be_timelimit = slapdFrontendConfig->timelimit;
            be = slapi_get_next_backend(cookie);
        }
        CFG_UNLOCK_WRITE(slapdFrontendConfig);

        slapi_ch_free((void **)&cookie);
    }
    return retVal;
}

int
config_set_useroc(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->useroc));
        slapdFrontendConfig->useroc = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_accesslog(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    retVal = log_update_accesslogdir(value, apply);

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Cannot open accesslog directory \"%s\", client accesses will not be logged.",
                value);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->accesslog));
        slapdFrontendConfig->accesslog = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_securitylog(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    retVal = log_update_securitylogdir(value, apply);

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                "Cannot open securitylog directory \"%s\", client accesses will not be logged.",
                value);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->securitylog));
        slapdFrontendConfig->securitylog = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_errorlog(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    retVal = log_update_errorlogdir(value, apply);

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "Cannot open errorlog file \"%s\", errors cannot be logged.  Exiting...", value);
        syslog(LOG_ERR,
               "Cannot open errorlog file \"%s\", errors cannot be logged.  Exiting...", value);
        g_set_shutdown(SLAPI_SHUTDOWN_EXIT);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->errorlog));
        slapdFrontendConfig->errorlog = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_auditlog(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    retVal = log_update_auditlogdir(value, apply);

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Cannot open auditlog directory \"%s\"", value);
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->auditlog));
        slapdFrontendConfig->auditlog = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_auditfaillog(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* Dont block the update to null */
    if (!config_value_is_null(attrname, value, errorbuf, 1)) {
        retVal = log_update_auditfaillogdir(value, apply);
        if (retVal != LDAP_SUCCESS) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Cannot open auditfaillog directory \"%s\"", value);
        }
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&(slapdFrontendConfig->auditfaillog));
        slapdFrontendConfig->auditfaillog = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_pw_maxage(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    time_t age = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    /* age in seconds */
    age = parse_duration_time_t(value);

    if (age <= 0 || age > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: password maximum age \"%s\" is invalid.", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        slapdFrontendConfig->pw_policy.pw_maxage = age;
    }
    return retVal;
}

int
config_set_pw_minage(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    time_t age = 0;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    /* age in seconds */
    age = parse_duration_time_t(value);
    if (age < 0 || age > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: password minimum age \"%s\" is invalid.", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        slapdFrontendConfig->pw_policy.pw_minage = age;
    }
    return retVal;
}

int
config_set_pw_warning(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    time_t sec;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    /* in seconds */
    sec = parse_duration_time_t(value);

    if (errno == ERANGE || sec < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: password warning age \"%s\" is invalid, password warning "
                              "age must range from 0 to %lld seconds",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }
    /* translate to seconds */
    if (apply) {
        slapdFrontendConfig->pw_policy.pw_warning = sec;
    }
    return retVal;
}


int
config_set_errorlog_level(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long level = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    level = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || level < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: error log level \"%s\" is invalid,"
                                                                   " error log level must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->errorloglevel = level;
        /* Set the internal value - apply the default error level */
        level |= SLAPD_DEFAULT_ERRORLOG_LEVEL;
        slapd_ldap_debug = level;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_accesslog_level(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long level = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    level = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || level < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: access log level \"%s\" is invalid,"
                                                                   " access log level must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        g_set_accesslog_level(level);
        slapdFrontendConfig->accessloglevel = level;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_statlog_level(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long level = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    level = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || level < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: stat log level \"%s\" is invalid,"
                                                                   " access log level must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        g_set_statlog_level(level);
        slapdFrontendConfig->statloglevel = level;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

int
config_set_securitylog_level(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long level = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    level = strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || level < 0) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s: security log level \"%s\" is invalid,"
                                                                   " security log level must range from 0 to %lld",
                              attrname, value, (long long int)LONG_MAX);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        g_set_securitylog_level(level);
        slapdFrontendConfig->securityloglevel = level;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return retVal;
}

/* set the referral-mode url (which puts us into referral mode) */
int
config_set_referral_mode(const char *attrname __attribute__((unused)), char *url, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    slapdFrontendConfig->refer_mode = REFER_MODE_OFF;

    if ((!url) || (!url[0])) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "referral url must have a value");
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
config_set_versionstring(const char *attrname __attribute__((unused)), char *version, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if ((!version) || (!version[0])) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "versionstring must have a value");
        return LDAP_OPERATIONS_ERROR;
    }
    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->versionstring = slapi_ch_strdup(version);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return LDAP_SUCCESS;
}


#define config_copy_strval(s) s ? slapi_ch_strdup(s) : NULL;

tls_check_crl_t
config_get_tls_check_crl() {
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return (tls_check_crl_t)slapi_atomic_load_32((int32_t *)&(slapdFrontendConfig->tls_check_crl), __ATOMIC_ACQUIRE);
}

int
config_get_port()
{
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
config_get_ignore_vattrs()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    return (int)slapdFrontendConfig->ignore_vattrs;
}

int32_t
config_get_sasl_mapping_fallback()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->sasl_mapping_fallback), __ATOMIC_ACQUIRE);

}

int32_t
config_get_disk_monitoring()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->disk_monitoring), __ATOMIC_ACQUIRE);
}

int32_t
config_get_disk_threshold_readonly()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->disk_threshold_readonly), __ATOMIC_ACQUIRE);
}

int32_t
config_get_disk_logging_critical()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->disk_logging_critical), __ATOMIC_ACQUIRE);
}

int
config_get_disk_grace_period()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_grace_period;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

uint64_t
config_get_disk_threshold()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    uint64_t retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->disk_threshold;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_ldapi_filename()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_filename);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int32_t
config_get_ldapi_switch()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ldapi_switch), __ATOMIC_ACQUIRE);
}

int32_t
config_get_ldapi_bind_switch()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ldapi_bind_switch), __ATOMIC_ACQUIRE);
}

char *
config_get_ldapi_root_dn()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_root_dn);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_ldapi_map_entries()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ldapi_map_entries), __ATOMIC_ACQUIRE);
}

char *
config_get_ldapi_uidnumber_type()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_uidnumber_type);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_ldapi_gidnumber_type()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_gidnumber_type);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_ldapi_search_base_dn()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_search_base_dn);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

#if defined(ENABLE_AUTO_DN_SUFFIX)
char *
config_get_ldapi_auto_dn_suffix()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->ldapi_auto_dn_suffix);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}
#endif


char *
config_get_anon_limits_dn()
{
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->anon_limits_dn);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_slapi_counters()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->slapi_counters), __ATOMIC_ACQUIRE);

}

char *
config_get_workingdir(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapdFrontendConfig->workingdir);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_versionstring(void)
{

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
config_get_secureport(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->secureport;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int
config_get_SSLclientAuth(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->SSLclientAuth;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int
config_get_ssl_check_hostname(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return (int)slapdFrontendConfig->ssl_check_hostname;
}


char *
config_get_localhost(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->localhost);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_listenhost(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->listenhost);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_securelistenhost(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->securelistenhost);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_srvtab(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->srvtab);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_sizelimit(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->sizelimit;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pagedsizelimit(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pagedsizelimit;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_pw_admin_dn(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_strdup(slapi_sdn_get_dn(slapdFrontendConfig->pw_policy.pw_admin));
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_admin_skip_update(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return (int)slapdFrontendConfig->pw_policy.pw_admin_skip_info;
}

char *
config_get_pw_storagescheme(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal = 0;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->pw_storagescheme->pws_name);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int32_t
config_get_pw_change(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_change), __ATOMIC_ACQUIRE);
}


int32_t
config_get_pw_history(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_history), __ATOMIC_ACQUIRE);
}


int32_t
config_get_pw_must_change(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_must_change), __ATOMIC_ACQUIRE);
}

int32_t
config_get_allow_hashed_pw(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->allow_hashed_pw), __ATOMIC_ACQUIRE);
}

int32_t
config_get_pw_syntax(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_syntax), __ATOMIC_ACQUIRE);
}


int
config_get_pw_minlength(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minlength;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_mindigits(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_mindigits;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_minalphas(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minalphas;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_minuppers(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minuppers;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_minlowers(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minlowers;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_minspecials(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minspecials;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_min8bit(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_min8bit;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_maxrepeats(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_maxrepeats;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_mincategories(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_mincategories;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_mintokenlength(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_mintokenlength;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pw_maxfailure(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_maxfailure;
    CFG_UNLOCK_READ(slapdFrontendConfig);
    return retVal;
}

int
config_get_pw_inhistory(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_inhistory;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

long
config_get_pw_lockduration(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_lockduration;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

long
config_get_pw_resetfailurecount(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_resetfailurecount;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_pw_is_global_policy(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_is_global_policy), __ATOMIC_ACQUIRE);
}

int32_t
config_get_pw_is_legacy_policy(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_is_legacy), __ATOMIC_ACQUIRE);
}

int32_t
config_get_pw_exp(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_exp), __ATOMIC_ACQUIRE);
}


int32_t
config_get_pw_unlock(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_unlock), __ATOMIC_ACQUIRE);
}

int32_t
config_get_pw_lockout()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->pw_policy.pw_lockout), __ATOMIC_ACQUIRE);
}

int
config_get_pw_gracelimit(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = 0;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_gracelimit;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_lastmod()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->lastmod), __ATOMIC_ACQUIRE);
}

int32_t
config_get_enquote_sup_oc()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->enquote_sup_oc), __ATOMIC_ACQUIRE);
}

int32_t
config_get_nagle(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->nagle), __ATOMIC_ACQUIRE);
}

int32_t
config_get_accesscontrol(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->accesscontrol), __ATOMIC_ACQUIRE);
}

int32_t
config_get_return_exact_case(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->return_exact_case), __ATOMIC_ACQUIRE);
}

int32_t
config_get_result_tweak(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->result_tweak), __ATOMIC_ACQUIRE);
}

int32_t
config_get_moddn_aci(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->moddn_aci), __ATOMIC_ACQUIRE);
}

int32_t
config_get_targetfilter_cache(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->targetfilter_cache), __ATOMIC_ACQUIRE);
}

int32_t
config_get_security(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->security), __ATOMIC_ACQUIRE);
}

int32_t
slapi_config_get_readonly(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->readonly), __ATOMIC_ACQUIRE);
}

int32_t
config_get_schemacheck(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->schemacheck), __ATOMIC_ACQUIRE);
}

int32_t
config_get_schemamod(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->schemamod), __ATOMIC_ACQUIRE);
}

int32_t
config_get_syntaxcheck(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->syntaxcheck), __ATOMIC_ACQUIRE);
}

int32_t
config_get_syntaxlogging(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->syntaxlogging), __ATOMIC_ACQUIRE);
}

int32_t
config_get_dn_validate_strict(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->dn_validate_strict), __ATOMIC_ACQUIRE);
}

int32_t
config_get_ds4_compatible_schema(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ds4_compatible_schema), __ATOMIC_ACQUIRE);
}

int32_t
config_get_schema_ignore_trailing_spaces(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->schema_ignore_trailing_spaces), __ATOMIC_ACQUIRE);
}

char *
config_get_rootdn(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->rootdn);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
slapi_get_rootdn(void)
{
    return config_get_rootdn();
}

char *
config_get_rootpw(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->rootpw);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


char *
config_get_rootpwstoragescheme(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->rootpwstoragescheme->pws_name);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_localuser(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->localuser);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

/* alias of encryption key and certificate files is now retrieved through */
/* calls to psetFullCreate() and psetGetAttrSingleValue(). See ssl.c, */
/* where this function is still used to set the global variable */
char *
config_get_encryptionalias(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->encryptionalias);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_threadnumber(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal;

    retVal = slapi_atomic_load_32(&(slapdFrontendConfig->threadnumber), __ATOMIC_RELAXED);

    if (retVal <= 0) {
        retVal = util_get_hardware_threads();
    }

    return retVal;
}

int32_t
config_get_maxthreadsperconn()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->maxthreadsperconn), __ATOMIC_ACQUIRE);
}

int64_t
config_get_maxdescriptors(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int64_t retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->maxdescriptors;
    CFG_UNLOCK_READ(slapdFrontendConfig);
    return retVal;
}

int
config_get_reservedescriptors()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->reservedescriptors;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_ioblocktimeout()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ioblocktimeout), __ATOMIC_ACQUIRE);
}

int
config_get_idletimeout()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->idletimeout;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}


int
config_get_groupevalnestlevel()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->groupevalnestlevel;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

struct berval **
config_get_defaultreferral(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    struct berval **refs;
    int nReferrals = 0;

    CFG_LOCK_READ(slapdFrontendConfig);
    /* count the number of referrals */
    for (nReferrals = 0;
         slapdFrontendConfig->defaultreferral &&
         slapdFrontendConfig->defaultreferral[nReferrals];
         nReferrals++)
        ;

    refs = (struct berval **)
        slapi_ch_malloc((nReferrals + 1) * sizeof(struct berval *));

    /*terminate the end, and add the referrals backwards */
    refs[nReferrals--] = NULL;

    while (nReferrals >= 0) {
        refs[nReferrals] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
        refs[nReferrals]->bv_val =
            config_copy_strval(slapdFrontendConfig->defaultreferral[nReferrals]->bv_val);
        refs[nReferrals]->bv_len = slapdFrontendConfig->defaultreferral[nReferrals]->bv_len;
        nReferrals--;
    }
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return refs;
}

char *
config_get_userat()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->userat);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_timelimit()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->timelimit;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_useroc()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_WRITE(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->useroc);
    CFG_UNLOCK_WRITE(slapdFrontendConfig);

    return retVal;
}

char *
config_get_accesslog()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->accesslog);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_securitylog()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->securitylog);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_errorlog()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->errorlog);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_external_libs_debug_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->external_libs_debug_enabled), __ATOMIC_ACQUIRE);
}

char *
config_get_auditlog()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->auditlog);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_auditfaillog()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->auditfaillog);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

long long
config_get_pw_maxage(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_maxage;
    CFG_UNLOCK_READ(slapdFrontendConfig);
    return retVal;
}

long long
config_get_pw_minage()
{

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_minage;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

long long
config_get_pw_warning(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->pw_policy.pw_warning;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_get_pwpolicy_inherit_global()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->pwpolicy_inherit_global;
    return retVal;
}

int
config_get_errorlog_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->errorloglevel;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal |= SLAPD_DEFAULT_ERRORLOG_LEVEL;
}

int
config_get_accesslog_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->accessloglevel;

    return retVal;
}

int
config_get_statlog_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->statloglevel;

    return retVal;
}

int
config_get_securitylog_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->securityloglevel;

    return retVal;
}

int
config_get_auditlog_logging_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = (int)slapdFrontendConfig->auditlog_logging_enabled;

    return retVal;
}

int
config_get_auditfaillog_logging_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = (int)slapdFrontendConfig->auditfaillog_logging_enabled;

    return retVal;
}

int
config_get_accesslog_logging_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = (int)slapdFrontendConfig->accesslog_logging_enabled;

    return retVal;
}

int
config_get_securitylog_logging_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = (int)slapdFrontendConfig->securitylog_logging_enabled;

    return retVal;
}

char *
config_get_referral_mode(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *ret;

    CFG_LOCK_READ(slapdFrontendConfig);
    ret = config_copy_strval(slapdFrontendConfig->refer_url);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return ret;
}

int
config_get_num_listeners(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->num_listeners;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

/* return yes/no without actually copying the referral url
   we don't worry about another thread changing this value
   since we now return an integer */
int
config_check_referral_mode(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return (slapdFrontendConfig->refer_mode & REFER_MODE_ON);
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

int32_t
config_get_unauth_binds_switch(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->allow_unauth_binds), __ATOMIC_ACQUIRE);
}

int32_t
config_get_require_secure_binds(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->require_secure_binds), __ATOMIC_ACQUIRE);
}

int32_t
config_get_close_on_failed_bind(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->close_on_failed_bind), __ATOMIC_ACQUIRE);
}

int32_t
config_get_anon_access_switch(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->allow_anon_access), __ATOMIC_ACQUIRE);
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
config_set_maxbersize(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long size;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    size = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (!apply) {
        return retVal;
    }

    if (size == 0) {
        size = SLAPD_DEFAULT_MAXBERSIZE;
    }
    CFG_LOCK_WRITE(slapdFrontendConfig);

    slapdFrontendConfig->maxbersize = size;

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}

ber_len_t
config_get_maxbersize()
{
    ber_len_t maxbersize;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    maxbersize = slapdFrontendConfig->maxbersize;
    if (maxbersize == 0) {
        maxbersize = SLAPD_DEFAULT_MAXBERSIZE;
    }

    return maxbersize;
}

int
config_set_maxsasliosize(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    long maxsasliosize;
    char *endptr;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    maxsasliosize = strtol(value, &endptr, 10);

    /* Check for non-numeric garbage in the value */
    if (*endptr != '\0') {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* Check for a value overflow */
    if (((maxsasliosize == LONG_MAX) || (maxsasliosize == LONG_MIN)) && (errno == ERANGE)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* A setting of -1 means unlimited.  Don't allow other negative values. */
    if ((maxsasliosize < 0) && (maxsasliosize != -1)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid. Value must range from -1 to %lld",
                              attrname, value, (long long int)LONG_MAX);
    } else if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->maxsasliosize = maxsasliosize;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int32_t
config_get_maxsasliosize()
{
    int32_t maxsasliosize;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    maxsasliosize = slapdFrontendConfig->maxsasliosize;

    return maxsasliosize;
}

int
config_set_localssf(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int localssf;
    char *endptr;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    localssf = (int)strtol(value, &endptr, 10);

    /* Check for non-numeric garbage in the value */
    if (*endptr != '\0') {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* Check for a value overflow */
    if (((localssf == INT_MAX) || (localssf == INT_MIN)) && (errno == ERANGE)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* Don't allow negative values. */
    if (localssf < 0) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid. Value must range from 0 to %d", attrname, value, INT_MAX);
    } else if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->localssf = localssf;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int
config_set_minssf(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    int minssf;
    char *endptr;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    minssf = (int)strtol(value, &endptr, 10);

    /* Check for non-numeric garbage in the value */
    if (*endptr != '\0') {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* Check for a value overflow */
    if (((minssf == INT_MAX) || (minssf == INT_MIN)) && (errno == ERANGE)) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    /* Don't allow negative values. */
    if (minssf < 0) {
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (retVal != LDAP_SUCCESS) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: \"%s\" is invalid. Value must range from 0 to %d", attrname, value, INT_MAX);
    } else if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->minssf = minssf;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}

int32_t
config_set_minssf_exclude_rootdse(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->minssf_exclude_rootdse),
                              errorbuf,
                              apply);

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

int32_t
config_get_minssf_exclude_rootdse()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->minssf_exclude_rootdse), __ATOMIC_ACQUIRE);

}

int
config_set_max_filter_nest_level(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *endp;
    int32_t level;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    level = (int32_t)strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "(%s) value (%s) is invalid\n", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
        return retVal;
    }

    if (!apply) {
        return retVal;
    }

    slapi_atomic_store_32(&(slapdFrontendConfig->max_filter_nest_level), level, __ATOMIC_RELEASE);
    return retVal;
}

int32_t
config_get_max_filter_nest_level()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->max_filter_nest_level), __ATOMIC_ACQUIRE);
}

uint64_t
config_get_ndn_cache_size()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_64(&(slapdFrontendConfig->ndn_cache_max_size), __ATOMIC_ACQUIRE);
}

int32_t
config_get_ndn_cache_enabled()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ndn_cache_enabled), __ATOMIC_ACQUIRE);
}

int32_t
config_get_return_orig_type_switch()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->return_orig_type), __ATOMIC_ACQUIRE);
}

char *
config_get_basedn(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->certmap_basedn);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int
config_set_basedn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapi_ch_free((void **)&slapdFrontendConfig->certmap_basedn);

    slapdFrontendConfig->certmap_basedn = slapi_dn_normalize(slapi_ch_strdup(value));

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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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
config_get_instancedir(void)
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
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
config_get_securitylog_list()
{
    return log_get_loglist(SLAPD_SECURITY_LOG);
}

char **
config_get_auditlog_list()
{
    return log_get_loglist(SLAPD_AUDIT_LOG);
}

char **
config_get_auditfaillog_list()
{
    return log_get_loglist(SLAPD_AUDITFAIL_LOG);
}

int32_t
config_set_accesslogbuffering(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->accesslogbuffering),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_securitylogbuffering(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->securitylogbuffering),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_csnlogging(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
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
    return (int)slapdFrontendConfig->csnlogging;
}

int32_t
config_set_attrname_exceptions(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
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
    return (int)slapdFrontendConfig->attrname_exceptions;
}

int32_t
config_set_hash_filters(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t val = 0;
    int32_t retVal = LDAP_SUCCESS;

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

int32_t
config_set_rewrite_rfc1274(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
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

    retVal = (int)slapdFrontendConfig->rewrite_rfc1274;
    return retVal;
}


static int
config_set_schemareplace(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        retVal = LDAP_OPERATIONS_ERROR;
    } else {
        /*
     * check that the value is one we allow.
     */
        if (0 != strcasecmp(value, CONFIG_SCHEMAREPLACE_STR_OFF) &&
            0 != strcasecmp(value, CONFIG_SCHEMAREPLACE_STR_ON) &&
            0 != strcasecmp(value, CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY)) {
            retVal = LDAP_OPERATIONS_ERROR;
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "unsupported value: %s", value);
        }
    }

    if (LDAP_SUCCESS == retVal && apply) {
        slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free((void **)&slapdFrontendConfig->schemareplace);
        slapdFrontendConfig->schemareplace = slapi_ch_strdup(value);
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return retVal;
}


int
config_set_outbound_ldap_io_timeout(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long timeout;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    timeout = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapdFrontendConfig->outbound_ldap_io_timeout = timeout;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }
    return LDAP_SUCCESS;
}


int32_t
config_set_unauth_binds_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->allow_unauth_binds),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_require_secure_binds(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->require_secure_binds),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_set_close_on_failed_bind(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->close_on_failed_bind),
                              errorbuf,
                              apply);

    return retVal;
}

int
config_set_anon_access_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(value, "on") != 0) && (strcasecmp(value, "off") != 0) &&
        (strcasecmp(value, "rootdse") != 0)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"on\", \"off\", or \"rootdse\".", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        /* we can return now if we aren't applying the changes */
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    if (strcasecmp(value, "on") == 0) {
        slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_ON;
    } else if (strcasecmp(value, "off") == 0) {
        slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_OFF;
    } else if (strcasecmp(value, "rootdse") == 0) {
        slapdFrontendConfig->allow_anon_access = SLAPD_ANON_ACCESS_ROOTDSE;
    }

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}

int
config_set_validate_cert_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(value, "on") != 0) && (strcasecmp(value, "off") != 0) &&
        (strcasecmp(value, "warn") != 0)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"on\", \"off\", or \"warn\".", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        /* we can return now if we aren't applying the changes */
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    if (strcasecmp(value, "on") == 0) {
        slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_ON;
    } else if (strcasecmp(value, "off") == 0) {
        slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_OFF;
    } else if (strcasecmp(value, "warn") == 0) {
        slapdFrontendConfig->validate_cert = SLAPD_VALIDATE_CERT_WARN;
    }

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}

int32_t
config_get_force_sasl_external(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->force_sasl_external), __ATOMIC_ACQUIRE);
}

int32_t
config_set_force_sasl_external(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname,
                              value,
                              &(slapdFrontendConfig->force_sasl_external),
                              errorbuf,
                              apply);

    return retVal;
}

int32_t
config_get_entryusn_global(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->entryusn_global), __ATOMIC_ACQUIRE);
}

int32_t
config_set_entryusn_global(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
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
config_set_entryusn_import_init(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
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

char **
config_get_allowed_sasl_mechs_array(void)
{
    /*
     * array of mechs. If is null, returns NULL thanks to ch_array_dup.
     * Caller must free!
     */
    char **retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapi_ch_array_dup(slapdFrontendConfig->allowed_sasl_mechs_array);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

char *
config_get_allowed_sasl_mechs(void)
{
    /*
     * Space seperated list of allowed mechs
     * if this is NULL, means *all* mechs are allowed!
     */
    char *retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->allowed_sasl_mechs;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

/* separated list of sasl mechs to allow */
int
config_set_allowed_sasl_mechs(const char *attrname, char *value, char *errorbuf __attribute__((unused)), int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (!apply) {
        return LDAP_SUCCESS;
    }

    /* During a reset, the value is "", so we have to handle this case. */
    if (strcmp(value, "") != 0) {
        char **nval_array;
        char *nval = slapi_ch_strdup(value);
        /* A separate variable is used because slapi_str2charray_ext can change it and nval'd become corrupted */
        char *tmp_array_nval;

        /* cyrus sasl doesn't like comma separated lists */
        replace_char(nval, ',', ' ');

        if (invalid_sasl_mech(nval)) {
            slapi_log_err(SLAPI_LOG_ERR, "config_set_allowed_sasl_mechs",
                          "Invalid value/character for sasl mechanism (%s).  Use ASCII "
                          "characters, upto 20 characters, that are upper-case letters, "
                          "digits, hyphens, or underscores\n",
                          nval);
            slapi_ch_free_string(&nval);
            return LDAP_UNWILLING_TO_PERFORM;
        }

        tmp_array_nval = slapi_ch_strdup(nval);
        nval_array = slapi_str2charray_ext(tmp_array_nval, " ", 0);
        slapi_ch_free_string(&tmp_array_nval);

        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&slapdFrontendConfig->allowed_sasl_mechs);
        slapi_ch_array_free(slapdFrontendConfig->allowed_sasl_mechs_array);
        slapdFrontendConfig->allowed_sasl_mechs = nval;
        slapdFrontendConfig->allowed_sasl_mechs_array = nval_array;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    } else {
        /* If this value is "", we need to set the list to *all* possible mechs */
        CFG_LOCK_WRITE(slapdFrontendConfig);
        slapi_ch_free_string(&slapdFrontendConfig->allowed_sasl_mechs);
        slapi_ch_array_free(slapdFrontendConfig->allowed_sasl_mechs_array);
        slapdFrontendConfig->allowed_sasl_mechs = NULL;
        slapdFrontendConfig->allowed_sasl_mechs_array = NULL;
        CFG_UNLOCK_WRITE(slapdFrontendConfig);
    }

    return LDAP_SUCCESS;
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
config_set_default_naming_context(const char *attrname __attribute__((unused)),
                                  char *value,
                                  char *errorbuf,
                                  int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    void *node;
    Slapi_DN *sdn;
    char *suffix = NULL;

    if (value && *value) {
        int in_init = 0;
        suffix = slapi_create_dn_string("%s", value);
        if (NULL == suffix) {
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s is not a valid suffix.", value);
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
            slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "%s is not an existing suffix.", value);
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
config_set_unhashed_pw_switch(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(value, "on") != 0) && (strcasecmp(value, "off") != 0) &&
        (strcasecmp(value, "nolog") != 0)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"on\", \"off\", or \"nolog\".", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        /* we can return now if we aren't applying the changes */
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    if (strcasecmp(value, "on") == 0) {
        slapdFrontendConfig->unhashed_pw_switch = SLAPD_UNHASHED_PW_ON;
    } else if (strcasecmp(value, "off") == 0) {
        slapdFrontendConfig->unhashed_pw_switch = SLAPD_UNHASHED_PW_OFF;
    } else if (strcasecmp(value, "nolog") == 0) {
        slapdFrontendConfig->unhashed_pw_switch = SLAPD_UNHASHED_PW_NOLOG;
    }

    CFG_UNLOCK_WRITE(slapdFrontendConfig);

    return retVal;
}

int32_t
config_get_enable_turbo_mode(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->enable_turbo_mode), __ATOMIC_ACQUIRE);
}

int32_t
config_get_connection_nocanon(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->connection_nocanon), __ATOMIC_ACQUIRE);
}

int32_t
config_get_plugin_logging(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->plugin_logging), __ATOMIC_ACQUIRE);
}

int32_t
slapi_config_get_unhashed_pw_switch()
{
    return config_get_unhashed_pw_switch();
}

int32_t
config_get_unhashed_pw_switch()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->unhashed_pw_switch), __ATOMIC_ACQUIRE);
}

int32_t
config_get_ignore_time_skew(void)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ignore_time_skew), __ATOMIC_ACQUIRE);
}

int32_t
config_get_global_backend_lock()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->global_backend_lock), __ATOMIC_ACQUIRE);
}

int32_t
config_set_enable_turbo_mode(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->enable_turbo_mode),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_set_connection_nocanon(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->connection_nocanon),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_set_ignore_time_skew(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->ignore_time_skew),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_set_global_backend_lock(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->global_backend_lock),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_set_plugin_logging(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->plugin_logging),
                              errorbuf, apply);
    return retVal;
}

int
config_get_connection_buffer(void)
{
    int retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    retVal = (int)slapdFrontendConfig->connection_buffer;

    return retVal;
}

int
config_set_connection_buffer(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal = LDAP_SUCCESS;
    int32_t val;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if ((strcasecmp(value, "0") != 0) && (strcasecmp(value, "1") != 0) &&
        (strcasecmp(value, "2") != 0)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"0\", \"1\", or \"2\".", attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return retVal;
    }

    val = atoi(value);
    slapi_atomic_store_32(&(slapdFrontendConfig->connection_buffer), val, __ATOMIC_RELEASE);

    return retVal;
}

int
config_set_listen_backlog_size(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long size;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    size = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->listen_backlog_size), size, __ATOMIC_RELEASE);
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

int
config_get_enable_nunc_stans()
{
    int retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->enable_nunc_stans;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_enable_nunc_stans(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->enable_nunc_stans),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_get_enable_upgrade_hash()
{
    int32_t retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->enable_upgrade_hash;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_enable_upgrade_hash(const char *attrname, char *value, char *errorbuf, int32_t apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->enable_upgrade_hash),
                              errorbuf, apply);
    return retVal;
}

static char *
config_initvalue_to_onoff(struct config_get_and_set *cgas, char *initvalbuf, size_t initvalbufsize)
{
    char *retval = NULL;
    if (cgas->config_var_type == CONFIG_ON_OFF) {
        slapi_onoff_t *ival = (slapi_onoff_t *)(intptr_t)cgas->initvalue;
        PR_snprintf(initvalbuf, initvalbufsize, "%s", (ival && *ival) ? "on" : "off");
        retval = initvalbuf;
    }
    return retval;
}

static char *
config_initvalue_to_special_filter_verify(struct config_get_and_set *cgas, char *initvalbuf, size_t initvalbufsize) {
    char *retval = NULL;
    if (cgas->config_var_type == CONFIG_SPECIAL_FILTER_VERIFY) {
        slapi_special_filter_verify_t *value = (slapi_special_filter_verify_t *)(intptr_t)cgas->initvalue;
        if (value != NULL) {
            if (*value == SLAPI_STRICT) {
                PR_snprintf(initvalbuf, initvalbufsize, "%s", "reject-invalid");
                retval = initvalbuf;
            } else if (*value == SLAPI_WARN_SAFE) {
                PR_snprintf(initvalbuf, initvalbufsize, "%s", "process-safe");
                retval = initvalbuf;
            } else if (*value == SLAPI_WARN_UNSAFE) {
                PR_snprintf(initvalbuf, initvalbufsize, "%s", "warn-invalid");
                retval = initvalbuf;
            } else if (*value == SLAPI_OFF_UNSAFE) {
                PR_snprintf(initvalbuf, initvalbufsize, "%s", "off");
                retval = initvalbuf;
            }
        }
    }
    return retval;
}

static int32_t
config_set_specialfilterverify(slapdFrontendConfig_t *slapdFrontendConfig, slapi_special_filter_verify_t *target, const char *attrname, char *value, char *errorbuf, int apply) {
    if (target == NULL) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (config_value_is_null(attrname, value, errorbuf, 1)) {
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_special_filter_verify_t p_val = SLAPI_WARN_SAFE;

    /* on/warn/off retained for legacy reasons due to wbrown making terrible mistakes :( :( */
    if (strcasecmp(value, "on") == 0) {
        p_val = SLAPI_STRICT;
    } else if (strcasecmp(value, "warn") == 0) {
        p_val = SLAPI_WARN_SAFE;
    /* The new fixed/descriptive names */
    } else if (strcasecmp(value, "reject-invalid") == 0) {
        p_val = SLAPI_STRICT;
    } else if (strcasecmp(value, "process-safe") == 0) {
        p_val = SLAPI_WARN_SAFE;
    } else if (strcasecmp(value, "warn-invalid") == 0) {
        p_val = SLAPI_WARN_UNSAFE;
    } else if (strcasecmp(value, "off") == 0) {
        p_val = SLAPI_OFF_UNSAFE;
    } else {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\". Valid values are \"reject-invalid\", \"process-safe\", \"warn-invalid\" or \"off\". If in doubt, choose \"process-safe\"", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return LDAP_SUCCESS;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);
    *target = p_val;
    CFG_UNLOCK_WRITE(slapdFrontendConfig);

    return LDAP_SUCCESS;
}


int32_t
config_set_verify_filter_schema(const char *attrname, char *value, char *errorbuf, int apply) {
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    slapi_special_filter_verify_t *target = &(slapdFrontendConfig->verify_filter_schema);
    return config_set_specialfilterverify(slapdFrontendConfig, target, attrname, value, errorbuf, apply);
}

Slapi_Filter_Policy
config_get_verify_filter_schema()
{
    slapi_special_filter_verify_t retVal;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->verify_filter_schema;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    /* Now map this to a policy that the fns understand. */
    switch (retVal) {
    case SLAPI_STRICT:
        return FILTER_POLICY_STRICT;
        break;
    case SLAPI_WARN_SAFE:
        return FILTER_POLICY_PROTECT;
        break;
    case SLAPI_WARN_UNSAFE:
        return FILTER_POLICY_WARNING;
        break;
    default:
        return FILTER_POLICY_OFF;
    }
    /* Should be unreachable ... */
    return FILTER_POLICY_OFF;
}

int32_t
config_get_referral_check_period()
{
    int32_t retVal;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->referral_check_period;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_referral_check_period(const char *attrname, char *value, char *errorbuf, int apply __attribute__((unused)))
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t min = 5;
    int32_t max = 3600;
    int32_t referral_check_period;
    char *endp = NULL;

    errno = 0;
    referral_check_period = strtol(value, &endp, 10);
    if ((*endp != '\0') || (errno == ERANGE) || (referral_check_period < min) || (referral_check_period > max)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "limit \"%s\" is invalid, %s must range from %d to %d",
                              value, CONFIG_REFERRAL_CHECK_PERIOD, min, max);
        return LDAP_OPERATIONS_ERROR;
    }
    slapi_atomic_store_32(&(slapdFrontendConfig->referral_check_period), referral_check_period, __ATOMIC_RELEASE);

    return LDAP_SUCCESS;
}

int32_t
config_get_return_orig_dn()
{
    int32_t retVal;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->return_orig_dn;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_return_orig_dn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->return_orig_dn),
                              errorbuf, apply);
    return retVal;
}

int32_t
config_get_enable_ldapssotoken()
{
    int32_t retVal;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = slapdFrontendConfig->enable_ldapssotoken;
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_enable_ldapssotoken(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value,
                              &(slapdFrontendConfig->enable_ldapssotoken),
                              errorbuf, apply);
    return retVal;
}

char *
config_get_ldapssotoken_secret()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char *retVal;

    CFG_LOCK_READ(slapdFrontendConfig);
    retVal = config_copy_strval(slapdFrontendConfig->ldapssotoken_secret);
    CFG_UNLOCK_READ(slapdFrontendConfig);

    return retVal;
}

int32_t
config_set_ldapssotoken_secret(const char *attrname, char *value, char *errorbuf, int apply)
{
    if (config_get_enable_ldapssotoken() == 0) {
        return LDAP_OPERATIONS_ERROR;
    }

    int32_t retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (fernet_validate_key(value) == 0) {
        return LDAP_UNWILLING_TO_PERFORM;
    }

    if (!apply) {
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);
    slapi_ch_free((void **)&slapdFrontendConfig->ldapssotoken_secret);

    slapdFrontendConfig->ldapssotoken_secret = slapi_ch_strdup(value);

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}

int32_t
config_set_ldapssotoken_ttl(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    int32_t ldapssotoken_ttl = 0;
    char *endp = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    ldapssotoken_ttl = (int32_t)strtol(value, &endp, 10);

    if (*endp != '\0' || errno == ERANGE || ldapssotoken_ttl < 1 || ldapssotoken_ttl > 86400) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                              "%s: invalid value \"%s\", maximum ldapssotoken ttl must range from 1 to 86400 (1 day)",
                              attrname, value);
        retVal = LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->ldapssotoken_ttl), ldapssotoken_ttl, __ATOMIC_RELEASE);
    }
    return retVal;
}

int32_t
config_get_ldapssotoken_ttl()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    return slapi_atomic_load_32(&(slapdFrontendConfig->ldapssotoken_ttl), __ATOMIC_ACQUIRE);
}

int
config_set_tcp_fin_timeout(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long tcp_fin_timeout;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    tcp_fin_timeout = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->tcp_fin_timeout), tcp_fin_timeout, __ATOMIC_RELEASE);
    }
    return LDAP_SUCCESS;
}

int
config_get_tcp_fin_timeout()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->tcp_fin_timeout;
    return retVal;
}

int
config_set_tcp_keepalive_time(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long tcp_keepalive_time;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    tcp_keepalive_time = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE || tcp_keepalive_time < 1) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply) {
        slapi_atomic_store_32(&(slapdFrontendConfig->tcp_keepalive_time), tcp_keepalive_time, __ATOMIC_RELEASE);
    }
    return LDAP_SUCCESS;
}

int
config_get_tcp_keepalive_time()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->tcp_keepalive_time;
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
    if (!cgas) {
#if 0
        debugHashTable(attr);
#endif
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown attribute %s will be ignored\n", attr);
        slapi_log_err(SLAPI_LOG_ERR, "config_set", "Unknown attribute %s will be ignored\n", attr);
        return LDAP_NO_SUCH_ATTRIBUTE;
    }

    switch (cgas->config_var_type) {
    case CONFIG_SPECIAL_REFERRALLIST:
        if (NULL == values) /* special token which means to remove referrals */
        {
            struct berval val;
            struct berval *vals[2] = {0, 0};
            vals[0] = &val;
            val.bv_val = REFERRAL_REMOVE_CMD;
            val.bv_len = strlen(REFERRAL_REMOVE_CMD);
            retval = config_set_defaultreferral(attr, vals, errorbuf, apply);
        } else {
            retval = config_set_defaultreferral(attr, values, errorbuf, apply);
        }
        break;

    default:
        if (values == NULL && (cgas->initvalue != NULL || cgas->geninitfunc != NULL)) {
            /* We are deleting all our values and reset to defaults */
            char initvalbuf[64];
            void *initval = cgas->initvalue;
            if (cgas->config_var_type == CONFIG_ON_OFF) {
                initval = (void *)config_initvalue_to_onoff(cgas, initvalbuf, sizeof(initvalbuf));
            } else if (cgas->config_var_type == CONFIG_SPECIAL_FILTER_VERIFY) {
                initval = (void *)config_initvalue_to_special_filter_verify(cgas, initvalbuf, sizeof(initvalbuf));
            } else if (cgas->geninitfunc) {
                initval = cgas->geninitfunc();
            }
            PR_ASSERT(initval);

            if (cgas->setfunc) {
                retval = (cgas->setfunc)(cgas->attr_name, initval, errorbuf, apply);
            } else if (cgas->logsetfunc) {
                retval = (cgas->logsetfunc)(cgas->attr_name, initval, cgas->whichlog, errorbuf, apply);
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "config_set",
                              "The attribute %s is read only; ignoring setting NULL value\n", attr);
            }
        } else if (values != NULL) {
            for (ii = 0; !retval && values && values[ii]; ++ii) {
                if (cgas->setfunc) {
                    retval = (cgas->setfunc)(cgas->attr_name,
                                             (char *)values[ii]->bv_val, errorbuf, apply);
                } else if (cgas->logsetfunc) {
                    retval = (cgas->logsetfunc)(cgas->attr_name,
                                                (char *)values[ii]->bv_val, cgas->whichlog,
                                                errorbuf, apply);
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "config_set",
                                  "The attribute %s is read only; ignoring new value %s\n",
                                  attr, values[ii]->bv_val);
                }
                values[ii]->bv_len = strlen((char *)values[ii]->bv_val);
            }
        } else {
            retval = LDAP_UNWILLING_TO_PERFORM;
        }
        break;
    }

    return retval;
}

static void
config_set_value(
    Slapi_Entry *e,
    struct config_get_and_set *cgas,
    void **value)
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
    case CONFIG_LONG_LONG:
        if (value)
            slapi_entry_attr_set_longlong(e, cgas->attr_name, *((long long *)value));
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;
    case CONFIG_STRING:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ? *((char **)value) : "");
        break;

    case CONFIG_STRING_GENERATED:
        assert(value);
        slapi_entry_attr_set_charptr(e, cgas->attr_name, *((char **)value));
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
            slapi_entry_attr_replace(e, cgas->attr_name, (struct berval **)*value);
        else
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "");
        break;

    case CONFIG_CONSTANT_STRING:
        assert(value); /* should be a constant value */
        slapi_entry_attr_set_charptr(e, cgas->attr_name, (char *)value);
        break;

    case CONFIG_CONSTANT_INT:
        assert(value); /* should be a constant value */
        pval = (uintptr_t)value;
        ival = (int)pval;
        slapi_entry_attr_set_int(e, cgas->attr_name, ival);
        break;

    case CONFIG_SPECIAL_TLS_CHECK_CRL:
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, (char *)cgas->initvalue);
            break;
        }
        tls_check_crl_t state = *(tls_check_crl_t *)value;

        if (state == TLS_CHECK_ALL) {
            sval = "all";
        } else if (state == TLS_CHECK_PEER) {
            sval = "peer";
        } else {
            sval = "none";
        }
        slapi_entry_attr_set_charptr(e, cgas->attr_name, sval);
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
                                     (value && *((char **)value)) ? *((char **)value) : "off");
        break;

    case CONFIG_STRING_OR_EMPTY:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ? *((char **)value) : "");
        break;

    case CONFIG_STRING_OR_UNKNOWN:
        slapi_entry_attr_set_charptr(e, cgas->attr_name,
                                     (value && *((char **)value)) ? *((char **)value) : "unknown");
        break;

    case CONFIG_SPECIAL_ERRORLOGLEVEL:
        if (value) {
            ival = *(int *)value;
            ival &= ~LDAP_DEBUG_ANY;
            if (ival == 0) {
                /*
                 * Don't store the default value as zero,
                 * but as its real value.
                 */
                ival = LDAP_DEBUG_ANY;
            } else {
                ival = *(int *)value;
            }
            slapi_entry_attr_set_int(e, cgas->attr_name, ival);
        } else
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

    case CONFIG_SPECIAL_UNHASHED_PW_SWITCH:
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "on");
            break;
        }

        if (*((int *)value) == SLAPD_UNHASHED_PW_OFF) {
            sval = "off";
        } else if (*((int *)value) == SLAPD_UNHASHED_PW_NOLOG) {
            sval = "nolog";
        } else {
            sval = "on";
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

    case CONFIG_SPECIAL_FILTER_VERIFY:
        /* Is this the right default here? */
        if (!value) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "process-safe");
            break;
        }

        if (*((slapi_special_filter_verify_t *)value) == SLAPI_STRICT) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "reject-invalid");
        } else if (*((slapi_special_filter_verify_t *)value) == SLAPI_WARN_SAFE) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "process-safe");
        } else if (*((slapi_special_filter_verify_t *)value) == SLAPI_WARN_UNSAFE) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "warn-invalid");
        } else if (*((slapi_special_filter_verify_t *)value) == SLAPI_OFF_UNSAFE) {
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "off");
        } else {
            /* Default to safe warn-proccess-safely */
            slapi_entry_attr_set_charptr(e, cgas->attr_name, "process-safe");
        }

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
    int tablesize = sizeof(ConfigList) / sizeof(ConfigList[0]);
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

        /* coverity[var_deref_model] */
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
            needs_free = 1;     /* get funcs must return alloc'd memory except for get
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


int
config_set_external_libs_debug_enabled(const char *attrname, char *value, char *errorbuf, int apply)
{
    int32_t retVal = LDAP_SUCCESS;
    int32_t dbglvl = 0; /* no debugging */
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->external_libs_debug_enabled),
                              errorbuf, apply);
    if (retVal == LDAP_SUCCESS && strcasecmp(value, "on") == 0) {
        dbglvl = -1; /* all debug levels */
    } else if (retVal == LDAP_SUCCESS && strcasecmp(value, "off") == 0) {
        dbglvl = 0;
    } else {
        return retVal;
    }
    ber_set_option(NULL, LBER_OPT_DEBUG_LEVEL, &dbglvl);
    ldap_set_option(NULL, LDAP_OPT_DEBUG_LEVEL, &dbglvl);
    return retVal;
}

void
config_set_accesslog_enabled(int value)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    errorbuf[0] = '\0';

    slapi_atomic_store_32(&(slapdFrontendConfig->accesslog_logging_enabled), value, __ATOMIC_RELEASE);
    if (value) {
        log_set_logging(CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_ACCESS_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_ACCESSLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_ACCESS_LOG, errorbuf, CONFIG_APPLY);
    }
    if (errorbuf[0] != '\0') {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_accesslog_enabled", "%s\n", errorbuf);
    }
}

void
config_set_securitylog_enabled(int value)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    errorbuf[0] = '\0';

    slapi_atomic_store_32(&(slapdFrontendConfig->securitylog_logging_enabled), value, __ATOMIC_RELEASE);
    if (value) {
        log_set_logging(CONFIG_SECURITYLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_SECURITY_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_SECURITYLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_SECURITY_LOG, errorbuf, CONFIG_APPLY);
    }
    if (errorbuf[0] != '\0') {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_accesslog_enabled", "%s\n", errorbuf);
    }
}

void
config_set_auditlog_enabled(int value)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    errorbuf[0] = '\0';

    slapi_atomic_store_32(&(slapdFrontendConfig->auditlog_logging_enabled), value, __ATOMIC_RELEASE);
    if (value) {
        log_set_logging(CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_AUDIT_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_AUDITLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_AUDIT_LOG, errorbuf, CONFIG_APPLY);
    }
    if (errorbuf[0] != '\0') {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_auditlog_enabled", "%s\n", errorbuf);
    }
}

void
config_set_auditfaillog_enabled(int value)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    errorbuf[0] = '\0';

    slapi_atomic_store_32(&(slapdFrontendConfig->auditfaillog_logging_enabled), value, __ATOMIC_RELEASE);
    if (value) {
        log_set_logging(CONFIG_AUDITFAILLOG_LOGGING_ENABLED_ATTRIBUTE, "on", SLAPD_AUDITFAIL_LOG, errorbuf, CONFIG_APPLY);
    } else {
        log_set_logging(CONFIG_AUDITFAILLOG_LOGGING_ENABLED_ATTRIBUTE, "off", SLAPD_AUDITFAIL_LOG, errorbuf, CONFIG_APPLY);
    }
    if (errorbuf[0] != '\0') {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_auditlog_enabled", "%s\n", errorbuf);
    }
}

int
config_set_maxsimplepaged_per_conn(const char *attrname, char *value, char *errorbuf, int apply)
{
    int retVal = LDAP_SUCCESS;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    long size;
    char *endp;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }

    errno = 0;
    size = strtol(value, &endp, 10);
    if (*endp != '\0' || errno == ERANGE) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "(%s) value (%s) is invalid\n", attrname, value);
        return LDAP_OPERATIONS_ERROR;
    }

    if (!apply) {
        return retVal;
    }

    CFG_LOCK_WRITE(slapdFrontendConfig);

    slapdFrontendConfig->maxsimplepaged_per_conn = size;

    CFG_UNLOCK_WRITE(slapdFrontendConfig);
    return retVal;
}

int
config_get_maxsimplepaged_per_conn()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->maxsimplepaged_per_conn;
    return retVal;
}

int32_t
config_set_extract_pem(const char *attrname, char *value, char *errorbuf, int apply)
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t retVal = LDAP_SUCCESS;

    retVal = config_set_onoff(attrname, value, &(slapdFrontendConfig->extract_pem), errorbuf, apply);
    return retVal;
}

int
config_get_extract_pem()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->extract_pem;
    return retVal;
}

#if defined(LINUX)
#if defined(__GLIBC__)
int
config_set_malloc_mxfast(const char *attrname, char *value, char *errorbuf, int apply __attribute__((unused)))
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int max = 80 * (sizeof(size_t) / 4);
    int32_t mxfast;
    char *endp = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }
    errno = 0;
    mxfast = strtol(value, &endp, 10);
    if ((*endp != '\0') || (errno == ERANGE)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "limit \"%s\" is invalid, %s must range from 0 to %d",
                              value, CONFIG_MALLOC_MXFAST, max);
        return LDAP_OPERATIONS_ERROR;
    }
    slapi_atomic_store_32(&(slapdFrontendConfig->malloc_mxfast), mxfast, __ATOMIC_RELEASE);

    if ((mxfast >= 0) && (mxfast <= max)) {
        mallopt(M_MXFAST, mxfast);
    } else if (DEFAULT_MALLOC_UNSET != mxfast) {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_malloc_mxfast",
                      "%s: Invalid value %d will be ignored\n",
                      CONFIG_MALLOC_MXFAST, mxfast);
    }
    return LDAP_SUCCESS;
}

int
config_get_malloc_mxfast()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->malloc_mxfast;
    return retVal;
}

int
config_set_malloc_trim_threshold(const char *attrname, char *value, char *errorbuf, int apply __attribute__((unused)))
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int32_t trim_threshold;
    char *endp = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }
    errno = 0;
    trim_threshold = strtol(value, &endp, 10);
    if ((*endp != '\0') || (errno == ERANGE)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "limit \"%s\" is invalid, %s must range from 0 to %lld",
                              value, CONFIG_MALLOC_TRIM_THRESHOLD, (long long int)LONG_MAX);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_atomic_store_32(&(slapdFrontendConfig->malloc_trim_threshold), trim_threshold, __ATOMIC_RELEASE);

    if (trim_threshold >= -1) {
        mallopt(M_TRIM_THRESHOLD, trim_threshold);
    } else if (DEFAULT_MALLOC_UNSET != trim_threshold) {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_malloc_trim_threshold",
                      "%s: Invalid value %d will be ignored\n",
                      CONFIG_MALLOC_TRIM_THRESHOLD, trim_threshold);
    }
    return LDAP_SUCCESS;
}

int
config_get_malloc_trim_threshold()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->malloc_trim_threshold;
    return retVal;
}

int
config_set_malloc_mmap_threshold(const char *attrname, char *value, char *errorbuf, int apply __attribute__((unused)))
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int max;
    int mmap_threshold;
    char *endp = NULL;

    if (config_value_is_null(attrname, value, errorbuf, 0)) {
        return LDAP_OPERATIONS_ERROR;
    }
    if (sizeof(char *) == 8) {
        max = 33554432; /* 4*1024*1024*sizeof(long) on 64-bit systems */
    } else {
        max = 524288; /* 512*1024 on 32-bit systems */
    }

    errno = 0;
    mmap_threshold = strtol(value, &endp, 10);
    if ((*endp != '\0') || (errno == ERANGE)) {
        slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "limit \"%s\"d is invalid, value must range from 0 to %d",
                              value, CONFIG_MALLOC_MMAP_THRESHOLD, max);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_atomic_store_32(&(slapdFrontendConfig->malloc_mmap_threshold), mmap_threshold, __ATOMIC_RELEASE);

    if ((mmap_threshold >= 0) && (mmap_threshold <= max)) {
        mallopt(M_MMAP_THRESHOLD, mmap_threshold);
    } else if (DEFAULT_MALLOC_UNSET != mmap_threshold) {
        slapi_log_err(SLAPI_LOG_ERR, "config_set_malloc_mmap_threshold",
                      "%s: Invalid value %d will be ignored\n",
                      CONFIG_MALLOC_MMAP_THRESHOLD, mmap_threshold);
    }
    return LDAP_SUCCESS;
}

int
config_get_malloc_mmap_threshold()
{
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    int retVal;

    retVal = slapdFrontendConfig->malloc_mmap_threshold;
    return retVal;
}
#endif
#endif

char *
slapi_err2string(int result)
{
    return ldap_err2string(result);
}

/*
 * Check the SASL mechanism values
 *
 * As per RFC 4422:
 * SASL mechanisms are named by character strings, from 1 to 20
 * characters in length, consisting of ASCII [ASCII] uppercase letters,
 * digits, hyphens, and/or underscores.
 */
static int
invalid_sasl_mech(char *str)
{
    char *mech = NULL;
    char *token = NULL;
    char *next = NULL;
    int i;

    if (str == NULL) {
        return 1;
    }
    if (strlen(str) < 1) {
        /* ignore empty values */
        return 0;
    }

    /*
     * Check the length for each mechanism
     */
    token = slapi_ch_strdup(str);
    for (mech = ldap_utf8strtok_r(token, " ", &next); mech;
         mech = ldap_utf8strtok_r(NULL, " ", &next)) {
        if (strlen(mech) == 0 || strlen(mech) > 20) {
            /* invalid length */
            slapi_ch_free_string(&token);
            return 1;
        }
    }
    slapi_ch_free_string(&token);

    /*
     * Check the individual characters
     */
    for (i = 0; str[i]; i++) {
        if (((int)str[i] < 48 || (int)str[i] > 57) && /* not a digit */
            ((int)str[i] < 65 || (int)str[i] > 90) && /* not upper case */
            (int)str[i] != 32 &&                      /* not a space (between mechanisms) */
            (int)str[i] != 45 &&                      /* not a hyphen */
            (int)str[i] != 95)                        /* not an underscore */
        {
            /* invalid character */
            return 1;
        }
    }

    /* Mechanism value is valid */
    return 0;
}

/*
 * Check if the number of reserve descriptors satisfy the servers needs.
 *
 * 1) Calculate the number of reserve descriptors the server requires
 * 2) Get the configured value for nsslapd-reservedescriptors
 * 3) If the configured value is less than the calculated value, increase it
 *
 * The formula used here is taken from the RH DS 11 docs:
 * nsslapd-reservedescriptor = 20 + (NldbmBackends * 4) + NglobalIndex +
 * 8 ReplicationDescriptors + Nreplicas +
 * NchainingBackends * nsOperationCOnnectionsLImit +
 * 3 PTADescriptors + 5 SSLDescriptors
 */
int
validate_num_config_reservedescriptors(void)
{
    #define RESRV_DESC_CONST 20
    #define BE_DESC_CONST 4
    #define REPL_DESC_CONST 8
    #define PTA_DESC_CONST 3
    #define SSL_DESC_CONST 5
    Slapi_Attr *attr = NULL;
    Slapi_Backend *be = NULL;
    Slapi_DN sdn;
    Slapi_Entry *entry = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *search_pb = NULL;
    char *cookie = NULL;
    char const *mt_str = NULL;
    char *entry_str = NULL;
    int rc = -1;
    int num_backends = 0;
    int num_repl_agmts = 0;
    int num_chaining_backends = 0;
    int chain_conn_limit = 0;
    int calc_reservedesc = RESRV_DESC_CONST;
    int config_reservedesc = config_get_reservedescriptors();

    /* Get number of backends, multiplied by the backend descriptor constant */
    for (be = slapi_get_first_backend(&cookie); be != NULL; be = slapi_get_next_backend(cookie)) {
        entry_str = slapi_create_dn_string("cn=%s,cn=ldbm database,cn=plugins,cn=config", be->be_name);
        if (NULL == entry_str) {
            slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to create backend dn string");
            return -1;
        }
        slapi_sdn_init_dn_byref(&sdn, entry_str);
        slapi_search_internal_get_entry(&sdn, NULL, &entry, plugin_get_default_component_id());
        if (entry) {
            if (slapi_entry_attr_hasvalue(entry, "objectclass", "nsBackendInstance")) {
                num_backends += 1;
            }
        }
        slapi_entry_free(entry);
        slapi_ch_free_string(&entry_str);
        slapi_sdn_done(&sdn);
    }
    slapi_ch_free((void **)&cookie);
    if (num_backends) {
        calc_reservedesc += (num_backends * BE_DESC_CONST);
    }

    /* Get number of indexes for each backend and add to total */
    for (be = slapi_get_first_backend(&cookie); be; be = slapi_get_next_backend(cookie)) {
        entry_str = slapi_create_dn_string("cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config", be->be_name);
        if (NULL == entry_str) {
            slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to create index dn string");
            return -1;
        }
        slapi_sdn_init_dn_byref(&sdn, entry_str);
        slapi_search_internal_get_entry(&sdn, NULL, &entry, plugin_get_default_component_id());
        if (entry) {
            rc = slapi_entry_attr_find(entry, "numsubordinates", &attr);
            if (LDAP_SUCCESS == rc) {
                Slapi_Value *sval;
                slapi_attr_first_value(attr, &sval);
                if (sval != NULL) {
                    const struct berval *bval = slapi_value_get_berval(sval);
                    if (NULL != bval)
                        calc_reservedesc += atol(bval->bv_val);
                }
            }
        }
        slapi_entry_free(entry);
        slapi_ch_free_string(&entry_str);
        slapi_sdn_done(&sdn);
    }
    slapi_ch_free((void **)&cookie);

    /* If replication is enabled add replication descriptor constant, plus the number of enabled repl agmts */
    mt_str = slapi_get_mapping_tree_config_root();
    if (NULL == mt_str) {
        slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to get mapping tree config string");
        return -1;
    }
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, mt_str, LDAP_SCOPE_SUBTREE, "(objectClass=nsds5replicationagreement) nsds5ReplicaEnabled", NULL, 0, NULL, NULL, plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (LDAP_SUCCESS == rc) {
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        for (; *entries; ++entries) {
            num_repl_agmts += 1;
        }
        if (num_repl_agmts) {
            calc_reservedesc += REPL_DESC_CONST;
        }
    }
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    calc_reservedesc += num_repl_agmts;

    /* Get the operation connection limit from the default instance config */
    entry_str = slapi_create_dn_string("cn=default instance config,cn=chaining database,cn=plugins,cn=config");
    if (NULL == entry_str) {
        slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to create default chaining config dn string");
        return -1;
    }
    slapi_sdn_init_dn_byref(&sdn, entry_str);
    slapi_search_internal_get_entry(&sdn, NULL, &entry, plugin_get_default_component_id());
    if (entry) {
        chain_conn_limit = slapi_entry_attr_get_int(entry, "nsoperationconnectionslimit");
    }
    slapi_entry_free(entry);
    slapi_ch_free_string(&entry_str);
    slapi_sdn_done(&sdn);

    /* Get the number of chaining backends, multiplied by the chaining operation connection limit */
    for (be = slapi_get_first_backend(&cookie); be; be = slapi_get_next_backend(cookie)) {
        entry_str = slapi_create_dn_string("cn=%s,cn=chaining database,cn=plugins,cn=config", be->be_name);
        if (NULL == entry_str) {
            slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to create chaining be dn string");
            return -1;
        }
        slapi_sdn_init_dn_byref(&sdn, entry_str);
        slapi_search_internal_get_entry(&sdn, NULL, &entry, plugin_get_default_component_id());
        if (entry) {
            if (slapi_entry_attr_hasvalue(entry, "objectclass", "nsBackendInstance")) {
                num_chaining_backends += 1;
            }
        }
        slapi_entry_free(entry);
        slapi_ch_free_string(&entry_str);
        slapi_sdn_done(&sdn);
    }
    slapi_ch_free((void **)&cookie);
    if (num_chaining_backends) {
        calc_reservedesc += (num_chaining_backends * chain_conn_limit);
    }

    /* If PTA is enabled add the pass through auth descriptor constant */
    entry_str = slapi_create_dn_string("cn=Pass Through Authentication,cn=plugins,cn=config");
    if (NULL == entry_str) {
        slapi_log_err(SLAPI_LOG_ERR, "validate_num_config_reservedescriptors", "Failed to create PTA dn string");
        return -1;
    }
    slapi_sdn_init_dn_byref(&sdn, entry_str);
    slapi_search_internal_get_entry(&sdn, NULL, &entry, plugin_get_default_component_id());
    if (entry) {
        if (slapi_entry_attr_hasvalue(entry, "nsslapd-PluginEnabled", "on")) {
            calc_reservedesc += PTA_DESC_CONST;
        }
    }
    slapi_entry_free(entry);
    slapi_ch_free_string(&entry_str);
    slapi_sdn_done(&sdn);

    /* If SSL is enabled add the SSL descriptor constant */;;
    if (config_get_security()) {
        calc_reservedesc += SSL_DESC_CONST;
    }

    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    char resrvdesc_str[SLAPI_DSE_RETURNTEXT_SIZE];
    /* Are the configured reserve descriptors enough to satisfy the servers needs */
    if (config_reservedesc < calc_reservedesc) {
        PR_snprintf(resrvdesc_str, sizeof(resrvdesc_str), "%d", calc_reservedesc);
        if (LDAP_SUCCESS == config_set_reservedescriptors(CONFIG_RESERVEDESCRIPTORS_ATTRIBUTE, resrvdesc_str, errorbuf, 1)) {
            slapi_log_err(SLAPI_LOG_INFO, "validate_num_config_reservedescriptors",
                  "reserve descriptors changed from %d to %d\n", config_reservedesc, calc_reservedesc);
        }
    }

    return (0);
}
