/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef __dsalib_h
#define __dsalib_h

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#ifdef HPUX
#include <limits.h> /* for PATH_MAX */
#endif

/* error types */
#define DS_FILE_ERROR 0
#define DS_MEMORY_ERROR 1
#define DS_SYSTEM_ERROR 2
#define DS_INCORRECT_USAGE 3
#define DS_ELEM_MISSING 4
#define DS_REGISTRY_DATABASE_ERROR 5
#define DS_NETWORK_ERROR 6
#define DS_GENERAL_FAILURE 7
#define DS_WARNING 8

/* The upper bound on error types */
#define DS_MAX_ERROR 9

/* The default error type (in case something goes wrong */
#define DS_DEFAULT_ERROR 3

#ifndef BIG_LINE
#define	BIG_LINE	1024
#endif
#ifndef PATH_MAX
#if defined( _WIN32 )
#define PATH_MAX _MAX_PATH
#else
#define PATH_MAX 256
#endif /* _WIN32 */
#endif /* PATH_MAX */
#ifndef HTML_ERRCOLOR
#define HTML_ERRCOLOR "#AA0000"
#endif
#ifndef CONTENT_NAME
#define CONTENT_NAME "content"
#endif

#ifdef XP_UNIX

#define FILE_PATHSEP '/'
#define FILE_PATHSEPP "/"
#define FILE_PARENT "../"
#define WSACleanup()

#elif defined(XP_WIN32)

#define FILE_PATHSEP '/'
#define FILE_PATHSEPP "\\\\"
#define FILE_PARENT "..\\"

#endif /* XP_WIN32 */

#define PATH_SIZE 1024
#define ERR_SIZE 8192

/* 
   NT doesn't strictly need these, but the libadmin API which is emulated
   below uses them.
 */
#define NEWSCRIPT_MODE 0755
#define NEWFILE_MODE 0644
#define NEWDIR_MODE 0755

#if defined( XP_WIN32 )
#define DS_EXPORT_SYMBOL   __declspec( dllexport ) 
#else
#define DS_EXPORT_SYMBOL  
#endif

#if defined( XP_WIN32 )
#define ENQUOTE "\""
#else
#define ENQUOTE ""
#endif

#ifndef FILE_SEP
#ifdef XP_WIN32
  #define FILE_SEP '\\'
#else
  #define FILE_SEP '/'
#endif
#endif

#if defined( XP_WIN32 )
  #define PATH_FOR_PLATFORM(_path) ds_unixtodospath(_path)
#else
  #define PATH_FOR_PLATFORM(_path)
#endif

#define START_SCRIPT "start-slapd"
#define RESTART_SCRIPT "restart-slapd"
#define STOP_SCRIPT "stop-slapd"

#if defined( XP_WIN32 )
#define SLAPD_NAME "slapd"
#else
#define SLAPD_NAME "ns-slapd"
#endif

#define MOCHA_NAME "JavaScript"

/*
 * Return values from ds_get_updown_status()
 */
#define	DS_SERVER_UP		1
#define	DS_SERVER_DOWN		0
#define	DS_SERVER_UNKNOWN	-1
/*
 * Return values from ds_bring_up_server()
 */
#define	DS_SERVER_ALREADY_UP	-2
#define	DS_SERVER_ALREADY_DOWN	-3
#define	DS_SERVER_PORT_IN_USE		-4
#define	DS_SERVER_MAX_SEMAPHORES	-5
#define	DS_SERVER_CORRUPTED_DB		-6
#define	DS_SERVER_NO_RESOURCES		-7
#define	DS_SERVER_COULD_NOT_START	-8

/*
 * Other return values
 */
#define DS_UNKNOWN_ERROR            -1
#define DS_NO_SERVER_ROOT          -10
#define DS_CANNOT_EXEC             -11
#define DS_CANNOT_OPEN_STAT_FILE   -12
#define DS_NULL_PARAMETER          -13
#define DS_SERVER_MUST_BE_DOWN     -14
#define DS_CANNOT_OPEN_BACKUP_FILE -15
#define DS_NOT_A_DIRECTORY         -16
#define DS_CANNOT_CREATE_DIRECTORY -17
#define DS_CANNOT_OPEN_LDIF_FILE   -18
#define DS_IS_A_DIRECTORY          -19
#define DS_CANNOT_CREATE_FILE      -20
#define DS_UNDEFINED_VARIABLE      -21
#define DS_NO_SUCH_FILE            -22
#define DS_CANNOT_DELETE_FILE      -23
#define DS_UNKNOWN_SNMP_COMMAND    -24
#define DS_NON_NUMERIC_VALUE       -25
#define DS_NO_LOGFILE_NAME         -26
#define DS_CANNOT_OPEN_LOG_FILE    -27
#define DS_HAS_TOBE_READONLY_MODE  -28
#define DS_INVALID_LDIF_FILE       -29

/*
 * Types of config files.
 */
#define	DS_REAL_CONFIG	1
#define	DS_TMP_CONFIG	2

/*
 * Maximum numeric value we will accept in admin interface
 * We may at some point need per-option bounds, but for now,
 * there's just one global maximum.
 */
#define	DS_MAX_NUMERIC_VALUE	4294967295	/* 2^32 - 1 */

/* Use our own macro for rpt_err, so we can put our own error code in
   NMC_STATUS */
#undef rpt_err
#define rpt_err(CODE, STR1, STR2, STR3) \
                     fprintf( stdout, "NMC_ErrInfo: %s\n", (STR1) ); \
                     fprintf( stdout, "NMC_STATUS: %d\n", CODE )

/*
 * Flags for ds_display_config()
 */
#define DS_DISP_HRB     1       /* horizontal line to begin with */
#define DS_DISP_HRE     2       /* horizontal line to end with */
#define DS_DISP_TB      4       /* table begin */
#define DS_DISP_TE      8       /* table end */
#define DS_DISP_EOL     16      /* End Of Line */
#define DS_DISP_NOMT    32      /* display only non empty */
#define DS_DISP_NOIN    64      /* display with no input field */
#define DS_DISP_HELP    128     /* display with a help button */
#define DS_DISP_PLAIN   256     /* No table, no nothin */
#define DS_SIMPLE       (DS_DISP_EOL | DS_DISP_NOIN | DS_DISP_HELP)
 
/*
 * dci_type for ds_cfg_info
 */
#define DS_ATTR_STRING  1
#define DS_ATTR_NUMBER  2
#define DS_ATTR_ONOFF   3
#define DS_ATTR_LIMIT   4       /* a number where -1 is displayed as blank */

struct ds_cfg_info {
        char    *dci_varname;
        char    *dci_display;
        int     dci_type;
        char	*dci_help;
};
 
extern struct ds_cfg_info ds_cfg_info[];

#define LDBM_DATA_SIZE 5

/*ldbm specific backend information*/
struct ldbm_data {
  char  *tv[LDBM_DATA_SIZE][2]; /*type and value*/
};


/*
 * varname for ds_showparam()
 * NOTE: these must be kept in synch with the ds_cfg_info array defined
 * in ../lib/dsalib_conf.c
 */
#define DS_LOGLEVEL     	0
#define DS_REFERRAL     	1
#define DS_AUDITFILE	   	2
#define DS_LOCALHOST    	3
#define DS_PORT			4
#define DS_SECURITY		5
#define DS_SECURE_PORT		6
#define DS_SSL3CIPHERS		7
#define DS_PASSWDHASH		8
#define DS_ACCESSLOG		9
#define DS_ERRORLOG		10
#define DS_ROOTDN		11
#define DS_ROOTPW		12
#define DS_SUFFIX       	13
#define DS_LOCALUSER	14
#define DS_CFG_MAX		15 /* MUST be one greater than the last option */

/* These control how long we wait for the server to start up or shutdown */
#define SERVER_START_TIMEOUT 600 /* seconds */
#define SERVER_STOP_TIMEOUT SERVER_START_TIMEOUT /* same as start timeout */

typedef int (*DS_RM_RF_ERR_FUNC)(const char *path, const char *op, void *arg);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
extern DS_EXPORT_SYMBOL char *ds_get_server_root();
extern DS_EXPORT_SYMBOL char *ds_get_install_root();
extern DS_EXPORT_SYMBOL char *ds_get_admserv_based_root();
extern DS_EXPORT_SYMBOL void ds_log_debug_message(char *msg);
extern DS_EXPORT_SYMBOL void ds_log_env(char **envp);
extern DS_EXPORT_SYMBOL int ds_get_updown_status();
extern DS_EXPORT_SYMBOL void ds_print_startstop(int stop);
extern DS_EXPORT_SYMBOL int ds_bring_up_server_install(int verbose,
	char *root, char *errorlog);
extern DS_EXPORT_SYMBOL int ds_bring_up_server(int verbose);
extern DS_EXPORT_SYMBOL char *ds_get_server_name();
extern DS_EXPORT_SYMBOL void ds_send_error(char *errstr, int print_errno);
extern DS_EXPORT_SYMBOL void ds_send_status(char *str);
extern DS_EXPORT_SYMBOL char *ds_get_cgi_var(char *cgi_var_name);
extern DS_EXPORT_SYMBOL	char *ds_get_cgi_var_simple(int index);
extern DS_EXPORT_SYMBOL char *ds_get_cgi_multiple(char *cgi_var_name);
extern DS_EXPORT_SYMBOL char *ds_get_errors_name();
extern DS_EXPORT_SYMBOL char *ds_get_access_name();
extern DS_EXPORT_SYMBOL char *ds_get_audit_name();
extern DS_EXPORT_SYMBOL char *ds_get_logfile_name(int config_type);


extern DS_EXPORT_SYMBOL int ds_bring_down_server();
extern DS_EXPORT_SYMBOL void ds_print_server_status(int isrunning);
extern DS_EXPORT_SYMBOL int ds_get_file_size(char *fileName);
extern DS_EXPORT_SYMBOL void ds_display_tail(char *fileName, int timeOut, 
    int startSeek, char *doneMsg, char *lastLine);
extern DS_EXPORT_SYMBOL char **ds_get_ldif_files();
extern DS_EXPORT_SYMBOL int ds_ldif2db_preserve(char *file);
extern DS_EXPORT_SYMBOL int ds_ldif2db(char *file);
extern DS_EXPORT_SYMBOL int ds_ldif2db_backend_subtree(char *file, char *backend, char *subtree);
extern DS_EXPORT_SYMBOL int ds_db2ldif(char *file);
extern DS_EXPORT_SYMBOL int ds_vlvindex(char **backendList, char **attrList);
extern DS_EXPORT_SYMBOL int ds_addindex(char **attrList, char *backendName);
extern DS_EXPORT_SYMBOL int ds_db2ldif_subtree(char *file, char *subtree);
extern DS_EXPORT_SYMBOL char **ds_get_bak_dirs();
extern DS_EXPORT_SYMBOL int ds_db2bak(char *file);
extern DS_EXPORT_SYMBOL int ds_bak2db(char *file);
extern DS_EXPORT_SYMBOL int ds_get_monitor(int frontend, char *port);
extern DS_EXPORT_SYMBOL int ds_get_bemonitor(char *bemdn, char *port);
extern DS_EXPORT_SYMBOL int ds_client_access(char *port, char *dn);
extern DS_EXPORT_SYMBOL char **ds_get_config(int type);
extern DS_EXPORT_SYMBOL char *ds_get_pwenc(char *passwd_hash, char *password);
extern DS_EXPORT_SYMBOL int ds_check_config(int type);
extern DS_EXPORT_SYMBOL int ds_check_pw(char *pwhash, char *pwclear);
extern DS_EXPORT_SYMBOL int ds_set_config(char *change_file_name);
extern DS_EXPORT_SYMBOL char **ds_get_conf_from_file(FILE *conf);
extern DS_EXPORT_SYMBOL void ds_display_config(char **ds_config);
extern DS_EXPORT_SYMBOL char *ds_get_var_name(int varnum);
extern DS_EXPORT_SYMBOL int ds_showparam(char **ds_config, int varname, int phase, 
    int occurance, char *dispname, int size, int maxlength, unsigned flags,
    char *url);
extern DS_EXPORT_SYMBOL void ds_show_pwmaxage(char *value);
extern DS_EXPORT_SYMBOL void ds_show_pwhash(char *value);
extern DS_EXPORT_SYMBOL char *ds_get_value(char **ds_config, char *parm, int phase, int occurance);
extern DS_EXPORT_SYMBOL void ds_apply_cfg_changes(int param_list[], int changed);
extern DS_EXPORT_SYMBOL int ds_commit_cfg_changes();
extern DS_EXPORT_SYMBOL int ds_config_updated();
extern DS_EXPORT_SYMBOL void ds_display_header(char *font_size, char *header);
extern DS_EXPORT_SYMBOL void ds_display_message(char *font_size, char *header);
extern DS_EXPORT_SYMBOL void ds_print_file_form(char *action, char *fileptr, char *full_fileptr);
extern DS_EXPORT_SYMBOL char *ds_get_file_meaning(char *file);
extern DS_EXPORT_SYMBOL void ds_print_file_name(char *fileptr);
extern DS_EXPORT_SYMBOL int ds_file_exists(char *filename);
extern DS_EXPORT_SYMBOL int ds_cp_file(char *sfile, char *dfile, int mode);
extern DS_EXPORT_SYMBOL time_t ds_get_mtime(char *filename);
extern DS_EXPORT_SYMBOL char *ds_get_config_value( int option );
extern DS_EXPORT_SYMBOL char **ds_get_file_list( char *dir );
extern DS_EXPORT_SYMBOL char *ds_get_tmp_dir();
extern DS_EXPORT_SYMBOL void ds_unixtodospath(char *szText);
extern DS_EXPORT_SYMBOL void ds_timetofname(char *szText);
extern DS_EXPORT_SYMBOL void ds_dostounixpath(char *szText);
extern DS_EXPORT_SYMBOL int ds_saferename(char *szSrc, char *szTarget);
extern DS_EXPORT_SYMBOL char *get_specific_help_button(char *help_link, 
    char *dispname, char *helpinfo);

/* Change the DN to a canonical format (in place); return DN. */
extern DS_EXPORT_SYMBOL char* dn_normalize (char* DN);

/* Change the DN to a canonical format (in place) and convert to v3; return DN. */
extern DS_EXPORT_SYMBOL char* dn_normalize_convert (char* DN);

/* if dn contains an unescaped quote return true */
extern DS_EXPORT_SYMBOL int ds_dn_uses_LDAPv2_quoting(const char *dn);

/* Return != 0 iff the DN equals or ends with the given suffix.
   Both DN and suffix must be normalized, by dn_normalize(). */
extern DS_EXPORT_SYMBOL int dn_issuffix (char* DN, char* suffix);

/* Return a copy of the DN, but with optional whitespace inserted. */
extern DS_EXPORT_SYMBOL char* ds_dn_expand (char* DN);

/* Return the value if it can be stored 'as is' in a config file.
   If it requires enquoting, allocate and return its enquoted form.
   The caller should free() the returned pointer iff it's != value. 
   On Windows, we don't want to double up on "\" characters in filespecs,
   so we need to pass in the value type */
extern DS_EXPORT_SYMBOL char* ds_enquote_config_value (int paramnum, char* value);

/*
 * Bring up a javascript alert.
 */
extern DS_EXPORT_SYMBOL void ds_alert_user(char *header, char *message);

/* Construct and return the DN that corresponds to the give DNS name.
   The caller should free() the returned pointer. */
extern DS_EXPORT_SYMBOL char* ds_DNS_to_DN (char* DNS);

/* Construct and return the DN of the LDAP server's own entry.
   The caller must NOT free() the returned pointer. */
extern DS_EXPORT_SYMBOL char* ds_get_config_DN (char** ds_config);

/* Encode characters, as described in RFC 1738 section 2.2,
   if they're 'unsafe' (as defined in RFC 1738), or '?' or
   <special> (as defined in RFC 1779).
   The caller should free() the returned pointer. */
extern DS_EXPORT_SYMBOL char* ds_URL_encode (const char*);

/* Decode characters, as described in RFC 1738 section 2.2.
   The caller should free() the returned pointer. */
extern DS_EXPORT_SYMBOL char* ds_URL_decode (const char*);

/* Encode all characters, even if 'safe' */
extern DS_EXPORT_SYMBOL char* ds_encode_all (const char*);

/* Change the effective UID and GID of this process to
   those associated with the given localuser (if any). */
extern DS_EXPORT_SYMBOL char* ds_become_localuser_name (char* localuser);

/* Change the effective UID and GID of this process to
   those associated with ds_config's localuser (if any). */
extern DS_EXPORT_SYMBOL char* ds_become_localuser (char** ds_config);

/* Change the effective UID and GID of this process back to
   what they were before calling ds_become_localuser(). */
extern DS_EXPORT_SYMBOL char* ds_become_original();

extern DS_EXPORT_SYMBOL char* ds_makeshort(char *filepath);

extern DS_EXPORT_SYMBOL int ds_search_file(char *filename, char *searchstring);

/* Begin parsing a POST in a CGI context */
extern DS_EXPORT_SYMBOL int ds_post_begin(FILE *input);

/* Begin parsing a GET in a CGI context */
extern DS_EXPORT_SYMBOL void ds_get_begin(char *query_string);

/* Display an error to the user and exit from a CGI */
extern DS_EXPORT_SYMBOL void ds_report_error(int type, char *errmsg, char *details);

/* Display a warning to the user */
extern DS_EXPORT_SYMBOL void ds_report_warning(int type, char *errmsg, char *details);

/* These functions are used by the program to alter the output behaviour
if not executing in a CGI context */
extern DS_EXPORT_SYMBOL int ds_get_formatted_output(void);
extern DS_EXPORT_SYMBOL void ds_set_formatted_output(int val);

/* return the value of a CGI variable */
extern DS_EXPORT_SYMBOL char *ds_a_get_cgi_var(char *varname, char *elem_id, char *bongmsg);

/* return a multi-valued CGI variable */
extern DS_EXPORT_SYMBOL char **ds_a_get_cgi_multiple(char *varname, char *elem_id, char *bongmsg);

/* open an html file */
extern DS_EXPORT_SYMBOL FILE *ds_open_html_file(char *filename);

/* show a message to be parsed by the non-HTML front end */
extern DS_EXPORT_SYMBOL void ds_show_message(const char *message);

/* show a key/value pair to be parsed by the non-HTML front end */
extern DS_EXPORT_SYMBOL void ds_show_key_value(char *key, char *value);

extern DS_EXPORT_SYMBOL void ds_submit(char *helptarget) ;
extern DS_EXPORT_SYMBOL char *ds_get_helpbutton(char *topic);

extern DS_EXPORT_SYMBOL void alter_startup_line(char *startup_line);

extern DS_EXPORT_SYMBOL int ds_dir_exists(char *fn);
extern DS_EXPORT_SYMBOL int ds_mkdir(char *dir, int mode);
extern DS_EXPORT_SYMBOL char *ds_mkdir_p(char *dir, int mode);
extern DS_EXPORT_SYMBOL char *ds_salted_sha1_pw_enc (char* pwd);
extern DS_EXPORT_SYMBOL char * ds_escape_for_shell( char *s );

extern DS_EXPORT_SYMBOL char **ds_string_to_vec(char *s);

extern DS_EXPORT_SYMBOL char *ds_system_errmsg(void);

extern DS_EXPORT_SYMBOL int ds_exec_and_report(char *cmd);

/* remove a directory hierarchy - if the error function is given, it will be called upon
   error (e.g. directory not readable, cannot remove file, etc.) - if the callback function
   returns 0, this means to abort the removal, otherwise, continue
*/
extern DS_EXPORT_SYMBOL int ds_rm_rf(const char *dir, DS_RM_RF_ERR_FUNC ds_rm_rf_err_func, void *arg);
/*
  remove a registry key and report an error message if unsuccessful
*/
extern DS_EXPORT_SYMBOL int ds_remove_reg_key(void *base, const char *format, ...);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __dsalib_h */
