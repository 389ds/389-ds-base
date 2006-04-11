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
/*
 * create_instance.c: Routines for creating an instance of a Directory Server
 *
 * These routines are not thread safe.
 * 
 * Rob McCool
 */

#define GW_CONF 1
#define PB_CONF 2

#include "create_instance.h"
#include "cfg_sspt.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <sys/stat.h>
#include <ctype.h>

#define PATH_SIZE 1024
#define ERR_SIZE 8192

/* delay time in seconds between referential integrity updates
    0 means continues */
#define REFERINT_DELAY 0

/* 1=log changes for replaction, 0=don't replicate changes */
#define REFERINT_LOG_CHANGES 0

#include "dsalib.h"
#include "dirver.h"

#include "nspr.h"
#include "plstr.h"

#ifdef XP_WIN32
#define NOT_ABSOLUTE_PATH(str) \
  ((str[0] != '/') && (str[0] != '\\') && (str[2] != '/') && (str[2] != '\\'))
#define EADDRINUSE WSAEADDRINUSE
#define EACCES WSAEACCES
#include <winsock.h>
#include <io.h>
#include <regparms.h>
#include <nt/ntos.h>
#define SHLIB_EXT "dll"
#else
#define NOT_ABSOLUTE_PATH(str) (str[0] != '/')
#include <errno.h>
#include <sys/types.h>

#if !defined(HPUX) && !defined(LINUX2_0)
#include <sys/select.h>  /* FD_SETSIZE */
#else
#include <sys/types.h>   /* FD_SETSIZE is in types.h on HPUX */
#endif

#if !defined(_WIN32) && !defined(AIX)
#include <sys/resource.h> /* get/setrlimit stuff */
#endif

#include <sys/socket.h>  /* socket flags */
#include <netinet/in.h>  /* sockaddr_in */
#include <arpa/inet.h>   /* inet_addr */
#ifdef HPUX
#ifdef __ia64
#define SHLIB_EXT "so"
#else
#define SHLIB_EXT "sl"
#endif
#else
#define SHLIB_EXT "so"
#endif

#endif

/* 
   NT doesn't strictly need these, but the libadmin API which is emulated
   below uses them.
 */
#define NEWSCRIPT_MODE 0755
#define NEWFILE_MODE 0644
#define NEWDIR_MODE 0755
#define NEWSECDIR_MODE 0700

#include <stdarg.h>

#ifdef XP_WIN32

OS_TYPE NS_WINAPI INFO_GetOperatingSystem ();
DWORD NS_WINAPI SERVICE_ReinstallNTService( LPCTSTR szServiceName, 
                                            LPCTSTR szServiceDisplayName,
                                            LPCTSTR szServiceExe );


#endif
static void ds_gen_index(FILE* f, char* belowdn);
static char *ds_gen_orgchart_conf(char *sroot, char *cs_path, server_config_s *cf);
static char *ds_gen_gw_conf(char *sroot, char *cs_path, server_config_s *cf, int conf_type);
static char *install_ds(char *sroot, server_config_s *cf, char *param_name);

static int write_ldap_info(char *slapd_server_root, server_config_s *cf);
#if defined (BUILD_PRESENCE)
static char *gen_presence_init_script(char *sroot, server_config_s *cf, 
                                         char *cs_path);
static int init_presence(char *sroot, server_config_s *cf, char *cs_path);
#endif

static char *make_error(char *fmt, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 1, 2)));
#else
        ;
#endif

static char *make_error(char *fmt, ...)
{
    static char errbuf[ERR_SIZE];
    va_list args;

    va_start(args, fmt);
    PR_vsnprintf(errbuf, sizeof(errbuf), fmt, args);
    va_end(args);
    return errbuf;
}


/* This is to determine if we can skip the port number checks.  During
migration or server cloning, we may want to copy over an old configuration,
including the old port number, which may not currently have permission to
use; if we don't need to start the server right away, we can skip
certain checks
*/
static int needToStartServer(server_config_s *cf)
{
    if (cf && (
       (cf->cfg_sspt && !strcmp(cf->cfg_sspt, "1")) ||
       (cf->start_server && !strcmp(cf->start_server, "1"))
    ))
    {
        return 1;
    }

    return 0;
}

static char *
myStrdup(const char *s)
{
    if (s == NULL)
    return (char *)s;

    return strdup(s);
}

static int getSuiteSpotUserGroup(server_config_s* cf)
{
#ifdef XP_UNIX
    static const char *ssUsersFile = "shared/config/ssusers.conf";
    char realFile[PATH_SIZE];
    char buf[1024];
    FILE *fp = NULL;
    int status = 1;

    if (cf->servuser)
        return 0;

    PR_snprintf(realFile, sizeof(realFile), "%s/%s", cf->sroot, ssUsersFile);
    if (!(fp = fopen(realFile, "r")))
        return 1;

    while (fgets(buf, sizeof(buf), fp))
    {
        char *p = NULL;

        if (buf[0] == '#' || buf[0] == '\n')
            continue;

        buf[strlen(buf) - 1] = 0;
        if (NULL != (p = strstr(buf, "SuiteSpotUser")))
        {
            p += strlen("SuiteSpotUser");
            while (ldap_utf8isspace(p))
            LDAP_UTF8INC(p);
            cf->servuser = strdup(p);
            status = 0;
            break;
        }
    }

    if (fp)
        fclose(fp);

    return status;
#else
    return 0;
#endif
}

/* ----------------------- Create default settings ------------------------ */


void set_defaults(char *sroot, char *hn, server_config_s *conf)
{
    char *id = 0, *t = 0;

    conf->sroot = sroot;

    if (hn)
    {
        if( (t = strchr(hn, '.')) )
            *t = '\0';
		id = PR_smprintf("%s", hn);
        if(t)
            *t = '.';
    }

    conf->servname = hn;
    conf->bindaddr = "";
    conf->servport = "80";
    conf->cfg_sspt = NULL;
    conf->suitespot3x_uid = NULL;
    conf->cfg_sspt_uid = NULL;
    conf->cfg_sspt_uidpw = NULL;
    conf->servport = "389";
    conf->secserv = "off";
    conf->secservport = "636";
    conf->ntsynch = "off";
    conf->ntsynchssl = "on";
    conf->ntsynchport = "5009";
    conf->rootpw = "";
    conf->roothashedpw = "";
    conf->loglevel = NULL;
    if (getenv("DEBUG_DS_LOG_LEVEL"))
        conf->loglevel = getenv("DEBUG_DS_LOG_LEVEL");
    conf->suffix = "dc=example, dc=com";
#ifndef DONT_ALWAYS_CREATE_NETSCAPEROOT
    conf->netscaperoot = name_netscaperootDN;
#endif /* DONT_ALWAYS_CREATE_NETSCAPEROOT */
#define CREATE_SAMPLE_SUFFIX
#ifdef CREATE_SAMPLE_SUFFIX
    conf->samplesuffix = "dc=example, dc=com";
#endif /* CREATE_SAMPLE_SUFFIX */
#ifdef TEST_CONFIG
    conf->netscaperoot = "cn=config40";
#endif /* TEST_CONFIG */

#define ROOT_RDN    "cn=Directory Manager"
    conf->rootdn = ROOT_RDN;
/*    conf->rootdn = malloc(strlen(ROOT_RDN) + 2 + strlen(conf->suffix) + 1);
    sprintf(conf->rootdn, "%s, %s", ROOT_RDN, conf->suffix);*/
    conf->servid = id;

#ifdef XP_UNIX
    conf->servuser = NULL;
#ifdef THREAD_NSPR_KERNEL
    conf->numprocs = "1";
    conf->maxthreads = "128";
#else
    conf->numprocs = "4";
    conf->maxthreads = "32";
#endif
#else /* XP_WIN32 */
    conf->maxthreads = "32";
#endif
    conf->minthreads = "4";

    conf->upgradingServer = 0;
    
    conf->start_server = "1";
    conf->admin_domain = NULL;
    conf->config_ldap_url = NULL;
    conf->user_ldap_url = NULL;
    conf->use_existing_config_ds = 0;
    conf->use_existing_user_ds = 0;
    conf->consumerdn = NULL;
    conf->disable_schema_checking = NULL;
    conf->install_ldif_file = NULL;
}


/* ----------------- Sanity check a server configuration ------------------ */


char *create_instance_checkport(char *, char *);
char *create_instance_checkuser(char *);
int create_instance_numbers(char *);
int create_instance_exists(char *fn);
char *create_instance_copy(char *, char *, int);
char *create_instance_concatenate(char *, char *, int);
int create_instance_mkdir(char *, int);
char *create_instance_mkdir_p(char *, int);

#if defined( SOLARIS )
/*
 * Solaris 9+ specific installation
 */
int create_instance_symlink(char *, char *);
#endif /* SOLARIS */


/*
  returns NULL if the given dn is a valid dn, or an error string
*/
static char *
isAValidDN(const char *dn_to_test)
{
    char *t = 0;

    if (!dn_to_test || !*dn_to_test)
    {
        t = "No value specified for the parameter.";
    }
    else
    {
        char **rdnList = ldap_explode_dn(dn_to_test, 0);
        char **rdnNoTypes = ldap_explode_dn(dn_to_test, 1);
        if (!rdnList || !rdnList[0] || !rdnNoTypes || !rdnNoTypes[0] ||
            !*rdnNoTypes[0] || !PL_strcasecmp(rdnList[0], rdnNoTypes[0]))
        {
            t = make_error("The given value [%s] is not a valid DN.",
                   dn_to_test);
        }
        if (rdnList)
            ldap_value_free(rdnList);
        if (rdnNoTypes)
            ldap_value_free(rdnNoTypes);
    }

    if (t)
        return t;

    return NULL;
}

/*
  prints a message if the given dn uses LDAPv2 style quoting
*/
void
checkForLDAPv2Quoting(const char *dn_to_test)
{
    if (ds_dn_uses_LDAPv2_quoting(dn_to_test))
    {
        char *newdn = strdup(dn_to_test);
        char *t;
        dn_normalize_convert(newdn);
        t = make_error(
            "The given value [%s] is quoted in the deprecated LDAPv2 style\n"
            "quoting format.  It will be automatically converted to use the\n"
            "LDAPv3 style escaped format [%s].", dn_to_test, newdn);
        free(newdn);
        ds_show_message(t);
    }

    return;
}

/*
  returns NULL if the given string contains no 8 bit chars, otherwise an
  error message
*/
static char *
contains8BitChars(const char *s)
{
    char *t = 0;

    if (s && *s)
    {
        for (; !t && *s; ++s)
        {
            if (*s & 0x80)
            {
                t = make_error("The given value [%s] contains invalid 8 bit characters.",
                   s);
            }
        }
    }

    return t;
}

static char *sanity_check(server_config_s *cf, char *param_name)
{
    char *t, fn[PATH_SIZE];
    register int x;

    if (!param_name)
        return "Parameter param_name is null";

    /* if we don't need to start the server right away, we can skip the
    port number checks
    */
    if (!needToStartServer(cf))
    {
        if( (t = create_instance_checkport(cf->bindaddr, cf->servport)) )
        {
            PL_strncpyz(param_name, "servport", BIG_LINE);
            return t;
        }

        if ( cf->secserv && (strcmp(cf->secserv, "on") == 0) && (cf->secservport != NULL) &&
         (*(cf->secservport) != '\0') ) {
            if ( (t = create_instance_checkport(cf->bindaddr, cf->secservport)) ) {
                PL_strncpyz(param_name, "secservport", BIG_LINE);
                return t;
            }
        }
        if ( cf->ntsynch && (strcmp(cf->ntsynch, "on") == 0) && (cf->ntsynchport != NULL) &&
         (*(cf->ntsynchport) != '\0') ) {
            if ( (t = create_instance_checkport(cf->bindaddr, cf->ntsynchport)) ) {
                PL_strncpyz(param_name, "ntsynchport", BIG_LINE);
                return t;
            }
        }
    }

    /* is the server identifier good? */
    for(x=0; cf->servid[x]; x++)  {
        if(strchr("/ &;`'\"|*!?~<>^()[]{}$\\", cf->servid[x]))  {
        PL_strncpyz(param_name, "servid", BIG_LINE);
            return make_error("You used a shell-specific character in "
                              "your server id (the character was %c).", 
                              cf->servid[x]);
        }
    }
    /* has that identifier already been used? */
    PR_snprintf(fn, sizeof(fn), "%s%c%s-%s", cf->sroot, FILE_PATHSEP, 
            PRODUCT_NAME, cf->servid);

/* Not an error to upgrade! ???
   if ( !cf->upgradingServer ) {
       if(create_instance_exists(fn)) {
           PL_strncpyz(param_name, "servid", BIG_LINE);
           return make_error ("A server named '%s' already exists."
           "\nPlease choose another server identifier.",
           cf->servid);
       }
   }
   */

#ifdef XP_UNIX
    if( (t = create_instance_checkuser(cf->servuser)) )
    {
        PL_strncpyz(param_name, "servuser", BIG_LINE);
        return t;
    }
#endif

    /* make sure some drooling imbecile doesn't put in bogus numbers */
#ifdef XP_UNIX
    if((!create_instance_numbers(cf->numprocs)) || (atoi(cf->numprocs) <= 0))
    {
        PL_strncpyz(param_name, "numprocs", BIG_LINE);
        return ("The number of processes must be not be zero or "
                "negative.");
    }
#endif
    if((!create_instance_numbers(cf->maxthreads)) || (atoi(cf->maxthreads) <= 0))
    {
        PL_strncpyz(param_name, "maxthreads", BIG_LINE);
        return ("The maximum threads must be not be zero or negative.");
    }
    if((!create_instance_numbers(cf->minthreads)) || (atoi(cf->minthreads) <= 0))
    {
        PL_strncpyz(param_name, "minthreads", BIG_LINE);
        return ("The minumum threads must be not be zero or negative.");
    }

    if((atoi(cf->minthreads)) > (atoi(cf->maxthreads)))
    {
        PL_strncpyz(param_name, "minthreads", BIG_LINE);
        return ("Minimum threads must be less than maximum threads.");
    }

    /* see if the DN parameters are valid DNs */
    if (!cf->use_existing_user_ds && (t = isAValidDN(cf->suffix)))
    {
        PL_strncpyz(param_name, "suffix", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->suffix);

    if (NULL != (t = isAValidDN(cf->rootdn)))
    {
        PL_strncpyz(param_name, "rootdn", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->rootdn);

    if (cf->replicationdn && *cf->replicationdn && (t = isAValidDN(cf->replicationdn)))
    {
        PL_strncpyz(param_name, "replicationdn", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->replicationdn);

    if (cf->consumerdn && *cf->consumerdn && (t = isAValidDN(cf->consumerdn)))
    {
        PL_strncpyz(param_name, "consumerdn", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->consumerdn);

    if (cf->changelogsuffix && *cf->changelogsuffix &&
        (t = isAValidDN(cf->changelogsuffix)))
    {
        PL_strncpyz(param_name, "changelogsuffix", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->changelogsuffix);

    if (cf->netscaperoot && *cf->netscaperoot &&
        (t = isAValidDN(cf->netscaperoot)))
    {
        PL_strncpyz(param_name, "netscaperoot", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->netscaperoot);

    if (cf->samplesuffix && *cf->samplesuffix &&
        (t = isAValidDN(cf->samplesuffix)))
    {
        PL_strncpyz(param_name, "samplesuffix", BIG_LINE);
        return t;
    }
    checkForLDAPv2Quoting(cf->samplesuffix);

    if (NULL != (t = contains8BitChars(cf->rootpw)))
    {
        PL_strncpyz(param_name, "rootpw", BIG_LINE);
        return t;
    }

    if (NULL != (t = contains8BitChars(cf->cfg_sspt_uidpw)))
    {
        PL_strncpyz(param_name, "cfg_sspt_uidpw", BIG_LINE);
        return t;
    }

    if (NULL != (t = contains8BitChars(cf->replicationpw)))
    {
        PL_strncpyz(param_name, "replicationpw", BIG_LINE);
        return t;
    }

    if (NULL != (t = contains8BitChars(cf->consumerpw)))
    {
        PL_strncpyz(param_name, "consumerpw", BIG_LINE);
        return t;
    }

    if (cf->cfg_sspt_uid && *cf->cfg_sspt_uid)
    {
    /*
      If it is a valid DN, ok.  Otherwise, it should be a uid, and should
      be checked for 8 bit chars
    */
        if (NULL != (t = isAValidDN(cf->cfg_sspt_uid)))
        {
            if (NULL != (t = contains8BitChars(cf->cfg_sspt_uid)))
            {
                PL_strncpyz(param_name, "cfg_sspt_uid", BIG_LINE);
                return t;
            }
        }
        else
            checkForLDAPv2Quoting(cf->cfg_sspt_uid);
    }

    return NULL;
}

/* ----- From a configuration, set up a new server in the server root ----- */

/* ------------------ UNIX utilities for server creation ------------------ */

#ifdef XP_UNIX

#include <unistd.h>
#include <pwd.h>

char*
chownfile (struct passwd* pw, char* fn)
{
    if (pw != NULL && chown (fn, pw->pw_uid, pw->pw_gid) == -1) {
        if (pw->pw_name != NULL) {
            return make_error ("Could not change owner of %s to %s.",
                   fn, pw->pw_name);
        } else {
            return make_error ("Could not change owner of %s to (UID %li, GID %li).",
                   fn, (long)(pw->pw_uid), (long)(pw->pw_gid));
        }
    }
    return NULL;
}

char *chownlogs(char *sroot, char *user)
{
    struct passwd *pw;
    char fn[PATH_SIZE];
    if(user && *user && !geteuid())  {
        if(!(pw = getpwnam(user)))
            return make_error("Could not find UID and GID of user '%s'.", 
                              user);
        PR_snprintf(fn, sizeof(fn), "%s%clogs", sroot, FILE_PATHSEP);
        return chownfile (pw, fn);
    }
    return NULL;
}

char *chownconfig(char *sroot, char *user)
{
    struct passwd *pw;
    char fn[PATH_SIZE];
    if(user && *user && !geteuid())  {
        if(!(pw = getpwnam(user)))
            return make_error("Could not find UID and GID of user '%s'.", 
                              user);
        PR_snprintf(fn, sizeof(fn), "%s%cconfig", sroot, FILE_PATHSEP);
        return chownfile (pw, fn);
    }
    return NULL;
}

#else

#define chownfile(a, b) 
#define chownlogs(a, b) 
#define chownconfig(a, b) 
#define chownsearch(a, b) 

#endif
char *gen_script(char *s_root, char *name, char *fmt, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 3, 4)));
#else
        ;
#endif

char *gen_script(char *s_root, char *name, char *fmt, ...)
{
    char fn[PATH_SIZE];
    FILE *f;
    char *shell = "/bin/sh";
    va_list args;

    PR_snprintf(fn, sizeof(fn), "%s%c%s", s_root, FILE_PATHSEP, name);
    if(!(f = fopen(fn, "w")))  
        return make_error("Could not write to %s (%s).", fn, ds_system_errmsg());
    va_start(args, fmt);
#if !defined( XP_WIN32 )
#if defined( OSF1 )
    /*
    The standard /bin/sh has some rather strange behavior with "$@",
    so use the posix version wherever possible.  OSF1 4.0D should
    always have this one available.
    */
    if (!access("/usr/bin/posix/sh", 0))
        shell = "/usr/bin/posix/sh";
#endif /* OSF1 */
    fprintf(f, "#!%s\n\n", shell);
    /*
    Neutralize shared library access.
    
    On HP-UX, SHLIB_PATH is the historical variable.
    However on HP-UX 64 bit, LD_LIBRARY_PATH is also used.
    We unset both too.
    */
#if defined( SOLARIS ) || defined( OSF1 ) || defined( LINUX2_0 )
    fprintf(f, "unset LD_LIBRARY_PATH\n");
#endif
#if defined( HPUX )
    fprintf(f, "unset SHLIB_PATH\n");
    fprintf(f, "unset LD_LIBRARY_PATH\n");
#endif
#if defined( AIX )
    fprintf(f, "unset LIBPATH\n");
#endif
#endif
    vfprintf(f, fmt, args);

#if defined( XP_UNIX )
    fchmod(fileno(f), NEWSCRIPT_MODE);
#endif
    fclose(f);
#if defined( XP_WIN32 )
    chmod( fn, NEWSCRIPT_MODE);
#endif
    return NULL;
}

char *gen_perl_script(char *s_root, char *cs_path, char *name, char *fmt, ...)
#ifdef __GNUC__ 
        __attribute__ ((format (printf, 4, 5)));
#else
        ;
#endif
char *gen_perl_script(char *s_root, char *cs_path, char *name, char *fmt, ...)
{
    char myperl[PATH_SIZE];
    char fn[PATH_SIZE];
    FILE *f;
    va_list args;

	if (PR_FAILURE == PR_Access(cs_path, PR_ACCESS_EXISTS)) {
		printf("Notice: %s does not exist, skipping %s . . .\n", cs_path, name);
		return NULL;
	}

    PR_snprintf(fn, sizeof(fn), "%s%c%s", cs_path, FILE_PATHSEP, name);
    PR_snprintf(myperl, sizeof(myperl), "%s%cbin%cslapd%cadmin%cbin%cperl",
                    s_root, FILE_PATHSEP, FILE_PATHSEP,
                    FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
    if(!(f = fopen(fn, "w")))  
        return make_error("Could not write to %s (%s).", fn, ds_system_errmsg());
    va_start(args, fmt);
#if !defined( XP_WIN32 )
    fprintf(f, "#!%s\n\n", myperl);
#endif
    vfprintf(f, fmt, args);

#if defined( XP_UNIX )
    fchmod(fileno(f), NEWSCRIPT_MODE);
#endif
    fclose(f);
#if defined( XP_WIN32 )
    chmod( fn, NEWSCRIPT_MODE);
#endif

    return NULL;
}

char *gen_perl_script_auto(char *s_root, char *cs_path, char *name,
                           server_config_s *cf)
{
    char myperl[PATH_SIZE];
    char fn[PATH_SIZE], ofn[PATH_SIZE];
    const char *table[10][2];

	if (PR_FAILURE == PR_Access(cs_path, PR_ACCESS_EXISTS)) {
		printf("Notice: %s does not exist, skipping %s . . .\n", cs_path, name);
		return NULL;
	}

    PR_snprintf(ofn, sizeof(ofn), "%s%cbin%cslapd%cadmin%cscripts%ctemplate-%s", s_root,
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP,
            FILE_PATHSEP, name);
    PR_snprintf(fn, sizeof(fn), "%s%c%s", cs_path, FILE_PATHSEP, name);
#ifdef USE_NSPERL
    PR_snprintf(myperl, sizeof(myperl), "!%s%cbin%cslapd%cadmin%cbin%cperl",
            s_root, FILE_PATHSEP, FILE_PATHSEP,
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
#else
	strcpy(myperl, "!/usr/bin/env perl");
#endif

    table[0][0] = "DS-ROOT";
    table[0][1] = s_root;
    table[1][0] = "MY-DS-ROOT";
    table[1][1] = cs_path;
    table[2][0] = "SEP";
    table[2][1] = FILE_PATHSEPP;
    table[3][0] = "SERVER-NAME";
    table[3][1] = cf->servname;
    table[4][0] = "SERVER-PORT";
    table[4][1] = cf->servport;
    table[5][0] = "PERL-EXEC";
    table[6][0] = "DEV-NULL";
#if !defined( XP_WIN32 )
    table[5][1] = myperl;
    table[6][1] = " /dev/null ";
#else
    table[5][1] = " perl script";
    table[6][1] = " NUL ";
#endif
    table[7][0] = "ROOT-DN";
    table[7][1] = cf->rootdn;
    table[8][0] = table[8][1] = NULL;

    if (generate_script(ofn, fn, NEWSCRIPT_MODE, table) != 0) {
        return make_error("Could not write %s to %s (%s).", ofn, fn,
                          ds_system_errmsg());
    }

    return NULL;
}

char *gen_perl_script_auto_for_migration(char *s_root, char *cs_path, char *name,
                           server_config_s *cf)
{
    char myperl[PATH_SIZE];
    char fn[PATH_SIZE], ofn[PATH_SIZE];
    const char *table[10][2];

    PR_snprintf(ofn, sizeof(ofn), "%s%cbin%cslapd%cadmin%cscripts%ctemplate-%s", s_root,
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP,
            FILE_PATHSEP, name);
    PR_snprintf(fn, sizeof(fn), "%s%cbin%cslapd%cadmin%cbin%c%s", s_root, FILE_PATHSEP, 
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, name);
#ifdef USE_NSPERL
    PR_snprintf(myperl, sizeof(myperl), "!%s%cbin%cslapd%cadmin%cbin%cperl",
            s_root, FILE_PATHSEP, FILE_PATHSEP,
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
#else
	strcpy(myperl, "!/usr/bin/env perl");
#endif

    table[0][0] = "DS-ROOT";
    table[0][1] = s_root;
    table[1][0] = "MY-DS-ROOT";
    table[1][1] = cs_path;
    table[2][0] = "SEP";
    table[2][1] = FILE_PATHSEPP;
    table[3][0] = "SERVER-NAME";
    table[3][1] = cf->servname;
    table[4][0] = "SERVER-PORT";
    table[4][1] = cf->servport;
    table[5][0] = "PERL-EXEC";
    table[6][0] = "DEV-NULL";
#if !defined( XP_WIN32 )
    table[5][1] = myperl;
    table[6][1] = " /dev/null ";
#else
    table[5][1] = " perl script";
    table[6][1] = " NUL ";
#endif
    table[7][0] = "ROOT-DN";
    table[7][1] = cf->rootdn;
    table[8][0] = table[8][1] = NULL;

    if (generate_script(ofn, fn, NEWSCRIPT_MODE, table) != 0) {
        return make_error("Could not write %s to %s (%s).", ofn, fn,
                          ds_system_errmsg());
    }

    return NULL;
}

/* ------------------ NT utilities for server creation ------------------ */

#ifdef XP_WIN32

char *
service_exists(char *servid)
{
    DWORD status, lasterror = 0;
    char szServiceName[MAX_PATH] = {0};
    PR_snprintf(szServiceName, sizeof(szServiceName),"%s-%s", SVR_ID_SERVICE, servid);
    /* if the service already exists, error */
    status = SERVICE_GetNTServiceStatus(szServiceName, &lasterror );
    if ( (lasterror == ERROR_SERVICE_DOES_NOT_EXIST) || 
        (status == SERVRET_ERROR) || (status == SERVRET_REMOVED) ) {
         return 0;
    } else {    return
        make_error("Server %s already exists: cannot create another.  "
                   "Please choose a different name or delete the "
                   "existing server.",
                   szServiceName);
    }

    return 0;
}

void setup_nteventlogging(char *szServiceId, char *szMessageFile)
{
    HKEY hKey;
    char szKey[MAX_PATH];
    DWORD dwData;

    PR_snprintf(szKey, sizeof(szKey), "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", szServiceId);

    if(RegCreateKey(HKEY_LOCAL_MACHINE, szKey, &hKey) == ERROR_SUCCESS)
    {
        if(RegSetValueEx(hKey, "EventMessageFile", 0, REG_SZ, (LPBYTE)szMessageFile, strlen(szMessageFile) + 1) == ERROR_SUCCESS)
        {
            dwData = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
            RegSetValueEx(hKey, "TypesSupported", 0, REG_DWORD, (LPBYTE) &dwData, sizeof(DWORD));
        }
        RegCloseKey(hKey);
    }
}

    
char *add_ntservice(server_config_s *cf)
{
    char szMessageFile[MAX_PATH];
    char szServiceExe[MAX_PATH], szServiceDisplayName[MAX_PATH], szServiceName[MAX_PATH];
    DWORD dwLastError;
    
    PR_snprintf(szServiceExe, sizeof(szServiceExe), "%s/bin/%s/server/%s", cf->sroot, 
              SVR_DIR_ROOT, SVR_EXE);
    PR_snprintf(szServiceName, sizeof(szServiceName),"%s-%s", SVR_ID_SERVICE, cf->servid);
    PR_snprintf(szServiceDisplayName, sizeof(szServiceDisplayName), "%s (%s)", SVR_NAME_FULL_VERSION, 
              cf->servid);

    /* install new service - if already installed, try and remove and 
        then reinstall */
    dwLastError = SERVICE_ReinstallNTService( szServiceName, 
        szServiceDisplayName, szServiceExe );
    if ( dwLastError != NO_ERROR ) {
         return make_error ( "While installing %s Service, the "
                "NT Service Manager reported error %d (%s)", 
                szServiceDisplayName, dwLastError, ds_system_errmsg() );
    }

    // setup event logging registry keys, do this after service creation
    PR_snprintf(szMessageFile, sizeof(szMessageFile), "%s\\bin\\%s\\server\\%s", cf->sroot, 
        SVR_DIR_ROOT, "slapdmessages30.dll");
    setup_nteventlogging(szServiceName, szMessageFile);

    // TODO: add perfmon setup code -ahakim 11/22/96
    return NULL;
}

char *setup_ntserver(server_config_s *cf)
{
    char line[MAX_PATH], *sroot = cf->sroot;
    char subdir[MAX_PATH];
    char NumValuesBuf[3];
    DWORD Result;
    HKEY hServerKey;
    DWORD NumValues;
    DWORD iterator;
    int value_already_exists = 0;
    DWORD type_buffer;
    char  value_data_buffer[MAX_PATH];
    DWORD sizeof_value_data_buffer;

    /* MLM - Adding ACL directories authdb and authdb/default */
    PR_snprintf(subdir, sizeof(subdir), "%s%cauthdb", sroot, FILE_PATHSEP); 
    if( (create_instance_mkdir(subdir, NEWDIR_MODE)) )
        return make_error("mkdir %s failed (%s)", subdir, ds_system_errmsg());

    PR_snprintf(subdir, sizeof(subdir), "%s%cauthdb%cdefault", sroot, FILE_PATHSEP, FILE_PATHSEP); 
    if( (create_instance_mkdir(subdir, NEWDIR_MODE)) )
        return make_error("mkdir %s failed (%s)", subdir, ds_system_errmsg());

    /* Create DS-nickname (corresponding to ServiceID) key in registry */
    PR_snprintf(line, sizeof(line), "%s\\%s\\%s-%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT, 
        SVR_ID_SERVICE, cf->servid);

    Result = RegCreateKey(HKEY_LOCAL_MACHINE, line, &hServerKey);
    if (Result != ERROR_SUCCESS) {
        return make_error("Could not create registry server key %s - error %d (%s)",
            line, GetLastError(), ds_system_errmsg());
    }
    
    // note that SVR_ID_PRODUCT is being used here, which is of the form dsX
    // as opposed to SVR_ID_SERVICE, which is of the form dsX30
    PR_snprintf(line, sizeof(line), "%s\\%s-%s\\config", sroot, SVR_ID_PRODUCT, cf->servid);
    Result = RegSetValueEx(hServerKey, VALUE_CONFIG_PATH, 0, REG_SZ, 
        line, strlen(line) + 1);

    RegCloseKey(hServerKey);

     /* Create SNMP key in registry */
    PR_snprintf(line, sizeof(line), "%s\\%s\\%s", KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT, 
        KEY_SNMP_CURRENTVERSION);

    Result = RegCreateKey(HKEY_LOCAL_MACHINE, line, &hServerKey);
    if (Result != ERROR_SUCCESS) {
        return make_error("Could not create registry server key %s - error %d (%s)",
            line, GetLastError(), ds_system_errmsg());
    }
    
    
    /* Create the SNMP Pathname value */
    PR_snprintf(line, sizeof(line), "%s\\%s", sroot, SNMP_PATH);
    Result = RegSetValueEx(hServerKey, VALUE_APP_PATH, 0, REG_SZ, 
        line, strlen(line) + 1);    
    RegCloseKey(hServerKey);

    /* write SNMP extension agent value to Microsoft SNMP Part of Registry)  */
    PR_snprintf(line, sizeof(line), "%s\\%s", KEY_SERVICES, KEY_SNMP_SERVICE); 
    Result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
                          line,
                          0,
                          KEY_ALL_ACCESS,
                          &hServerKey);
    /* if its there set the value, otherwise go on to the next thing  */
    if (Result == ERROR_SUCCESS) 
    {
        /* extension agents should have linearly increasing value, 
        make sure it doesn't already exist, find last one and increment 
        value for new key */

        PR_snprintf(line, sizeof(line), "%s\\%s\\%s",    KEY_SOFTWARE_NETSCAPE, SVR_KEY_ROOT, KEY_SNMP_CURRENTVERSION);

        Result = RegQueryInfoKey(hServerKey, NULL, NULL, NULL, NULL, NULL,
                                 NULL, &NumValues, NULL, NULL, NULL, NULL);
      
        if (Result == ERROR_SUCCESS){
            for(iterator = 0; iterator <= NumValues; iterator++)
            {
                /* initialize to max size to avoid
                   ERROR_MORE_DATA because size gets set
                   to actual size of key after call 
                   to RegQueryValueEx, previously there
                   was a bug if last key was smaller
                   than this one it would return ERROR_MORE_DATA
                   and it would not find the key if it was already there
                */
                sizeof_value_data_buffer=MAX_PATH;
                PR_snprintf(NumValuesBuf, sizeof(NumValuesBuf), "%d", iterator);
                Result = RegQueryValueEx(hServerKey,
                                      NumValuesBuf,
                                      NULL,
                                      &type_buffer,
                                      value_data_buffer,
                                      &sizeof_value_data_buffer
                                     );

                if(!strcmp(value_data_buffer, line))
                {
                    value_already_exists = 1;
                }
            }                
        }
                
        if(!value_already_exists)
        {
              PR_snprintf(NumValuesBuf, sizeof(NumValuesBuf), "%d", NumValues + 1); 
            Result = RegSetValueEx(hServerKey, NumValuesBuf, 0, REG_SZ,
                                 line, strlen(line) + 1);

            /* couldn't set this value, so there is a real problem */
            if (Result != ERROR_SUCCESS)
            {
                return make_error("Could not set value %s (%d)",
                line, Result);
            }
        }

    }
    RegCloseKey(hServerKey);
        
    return NULL;
}
#endif

static char *
create_scripts(server_config_s *cf, char *param_name)
{
    char *t = NULL, *sroot = cf->sroot;
    char subdir[PATH_SIZE];

    /* Create slapd-nickname directory */
    PR_snprintf(subdir, sizeof(subdir), "%s%c"PRODUCT_NAME"-%s", sroot, FILE_PATHSEP, 
                 cf->servid);
#ifdef XP_UNIX
    /* Start/stop/rotate/restart scripts */
    if (getenv("USE_DEBUGGER"))
    {
        char *debugger = getenv("DSINST_DEBUGGER");
        char *debugger_command = getenv("DSINST_DEBUGGER_CMD");
        if (! debugger) {
            debugger = "/tools/ns/workshop/bin/dbx";
        }
        if (! debugger_command) {
            debugger_command = "echo"; /* e.g. do nothing */
        }
#ifdef OSF1
        printf("-D %s -i %s/logs/pid -d %s -z\n", subdir, subdir,
               cf->loglevel ? cf->loglevel : "0");
        t = gen_script(subdir, START_SCRIPT,
               "\n"
               "# Script that starts the ns-slapd server.\n"
               "# Exit status can be:\n"
               "#       0: Server started successfully\n"
               "#       1: Server could not be started\n"
               "#       2: Server already running\n"
               "\n"
               "NETSITE_ROOT=%s\n"
               "export NETSITE_ROOT\n"
               "PIDFILE=%s/logs/pid\n"
               "if test -f $PIDFILE ; then\n"
               "    PID=`cat $PIDFILE`\n"
               "    if kill -0 $PID > /dev/null 2>&1 ; then\n"
               "        echo There is an ns-slapd process already running: $PID\n"
               "        exit 2;\n"
               "    else\n"
               "        rm -f $PIDFILE\n"
               "    fi\n"
               "fi\n"
               "cd %s/bin/%s/server; ./%s -D %s -i %s/logs/pid -d %s -z \"$@\" &\n"
               "loop_counter=1\n"
               "max_count=120\n"
               "while test $loop_counter -le $max_count; do\n"
               "    loop_counter=`expr $loop_counter + 1`\n"
               "    if test ! -f $PIDFILE ; then\n"
               "        sleep 1;\n"
               "    else\n"
               "        PID=`cat $PIDFILE`\n"
            /* rbyrne: setuputil takes any message here as an error:
                "        echo Server has been started. ns-slapd process started: $PID\n"*/
               "        exit 0;\n"
               "    fi\n"
               "done\n"
               "echo Server not running!! Failed to start ns-slapd process.\n"
               "exit 1\n",
               sroot, subdir, sroot, PRODUCT_NAME, PRODUCT_BIN, subdir,
               subdir,
           cf->loglevel ? cf->loglevel : "0"
        );
#else
        t = gen_script(subdir, START_SCRIPT,
               "\n"
               "# Script that starts the ns-slapd server.\n"
               "# Exit status can be:\n"
               "#       0: Server started successfully\n"
               "#       1: Server could not be started\n"
               "#       2: Server already running\n"
               "\n"
               "NETSITE_ROOT=%s\n"
               "export NETSITE_ROOT\n"
               "PIDFILE=%s/logs/pid\n"
               "if test -f $PIDFILE ; then\n"
               "    PID=`cat $PIDFILE`\n"
               "    if kill -0 $PID > /dev/null 2>&1 ; then\n"
               "        echo There is an ns-slapd process already running: $PID\n"
               "        exit 2;\n"
               "    else\n"
               "        rm -f $PIDFILE\n"
               "    fi\n"
               "fi\n"
               "if [ -x /usr/local/bin/xterm ]; then\n"
               "  xterm=/usr/local/bin/xterm\n"
               "else\n"
               "  xterm=/usr/openwin/bin/xterm\n"
               "fi\n"
               "cd %s/bin/%s/server; $xterm -title debugger -e %s -c \"dbxenv follow_fork_mode child ; stop in main ; %s ; run -D %s -i %s/logs/pid -d %s -z $*\" %s &\n"
               "loop_counter=1\n"
               "max_count=120\n"
               "while test $loop_counter -le $max_count; do\n"
               "    loop_counter=`expr $loop_counter + 1`\n"
               "    if test ! -f $PIDFILE ; then\n"
               "        sleep 1;\n"
               "    else\n"
               "        PID=`cat $PIDFILE`\n"
                /* rbyrne: setuputil takes any message here as an error:
               "        echo Server has been started. ns-slapd process started: $PID\n"*/
               "        exit 0;\n"
               "    fi\n"
               "done\n"
               "echo Server not running!! Failed to start ns-slapd process.\n"
               "exit 1\n",
               sroot, subdir, sroot, PRODUCT_NAME, debugger, debugger_command,
               subdir,
               subdir, cf->loglevel ? cf->loglevel : "0", PRODUCT_BIN
        );
#endif
    }
    else
    {
        t = gen_script(subdir, START_SCRIPT,
            "\n"
            "# Script that starts the ns-slapd server.\n"
            "# Exit status can be:\n"
            "#       0: Server started successfully\n"
            "#       1: Server could not be started\n"
            "#       2: Server already running\n"
            "\n"
            "NETSITE_ROOT=%s\n"
            "export NETSITE_ROOT\n"
            "PIDFILE=%s/logs/pid\n"
            "STARTPIDFILE=%s/logs/startpid\n"
            "if test -f $STARTPIDFILE ; then\n"
            "    PID=`cat $STARTPIDFILE`\n"
            "    if kill -0 $PID > /dev/null 2>&1 ; then\n"
            "        echo There is an ns-slapd process already running: $PID\n"
            "        exit 2;\n"
            "    else\n"
            "        rm -f $STARTPIDFILE\n"
            "    fi\n"
            "fi\n"
            "if test -f $PIDFILE ; then\n"
            "    PID=`cat $PIDFILE`\n"
            "    if kill -0 $PID > /dev/null 2>&1 ; then\n"
            "        echo There is an ns-slapd process already running: $PID\n"
            "        exit 2;\n"
            "    else\n"
            "        rm -f $PIDFILE\n"
            "    fi\n"
            "fi\n"
            "cd %s/bin/%s/server; ./%s -D %s -i %s/logs/pid -w $STARTPIDFILE \"$@\"\n"
            "if [ $? -ne 0 ]; then\n"
            "    exit 1\n"
            "fi\n"
            "\n"
            "loop_counter=1\n"
            "# wait for 10 seconds for the start pid file to appear\n"
            "max_count=10\n"
            "while test $loop_counter -le $max_count; do\n"
            "    loop_counter=`expr $loop_counter + 1`\n"
            "    if test ! -f $STARTPIDFILE ; then\n"
            "        sleep 1;\n"
            "    else\n"
            "        PID=`cat $STARTPIDFILE`\n"
            "    fi\n"
            "done\n"
            "if test ! -f $STARTPIDFILE ; then\n"
            "    echo Server failed to start !!! Please check errors log for problems\n"
            "    exit 1\n"
            "fi\n"
            "loop_counter=1\n"
            "# wait for 10 minutes (600 times 1 seconds)\n"
            "max_count=600\n" /* 10 minutes */
            "while test $loop_counter -le $max_count; do\n"
            "    loop_counter=`expr $loop_counter + 1`\n"
            "    if test ! -f $PIDFILE ; then\n"
            "        if kill -0 $PID > /dev/null 2>&1 ; then\n"
            "            sleep 1\n"
            "    else\n"
            "        echo Server failed to start !!! Please check errors log for problems\n"
            "        exit 1\n"
            "    fi\n"
            "    else\n"
            "        PID=`cat $PIDFILE`\n"
            /* rbyrne: setuputil takes any message here as an error:
            "        echo Server has been started. ns-slapd process started: $PID\n"*/
            "        exit 0;\n"
            "    fi\n"
            "done\n"
            "echo Server not running!! Failed to start ns-slapd process.  Please check the errors log for problems.\n"
            "exit 1\n",
            sroot, subdir, subdir, sroot, PRODUCT_NAME, PRODUCT_BIN, subdir,
            subdir
        );
    }
    if(t) return t;
    
    t = gen_script(subdir, STOP_SCRIPT,
           "\n"
           "# Script that stops the ns-slapd server.\n"
           "# Exit status can be:\n"
           "#       0: Server stopped successfully\n"
           "#       1: Server could not be stopped\n"
           "#       2: Server was not running\n"
           "\n"
           "PIDFILE=%s/logs/pid\n"
           "if test ! -f $PIDFILE ; then\n"
           "    echo No ns-slapd PID file found. Server is probably not running\n"
           "    exit 2\n"
           "fi\n"
           "PID=`cat $PIDFILE`\n"
           "# see if the server is already stopped\n"
           "kill -0 $PID > /dev/null 2>&1 || {\n"
           "    echo Server not running\n"
           "    if test -f $PIDFILE ; then\n"
           "        rm -f $PIDFILE\n"
           "    fi\n"
           "    exit 2\n"
           "}\n"
           "# server is running - kill it\n"
           "kill $PID\n"
           "loop_counter=1\n"
           "# wait for 10 minutes (600 times 1 second)\n"
           "max_count=600\n" /* 10 minutes */
           "while test $loop_counter -le $max_count; do\n"
           "    loop_counter=`expr $loop_counter + 1`\n"
           "    if kill -0 $PID > /dev/null 2>&1 ; then\n"
           "        sleep 1;\n"
           "    else\n"
           "        if test -f $PIDFILE ; then\n"
           "            rm -f $PIDFILE\n"
           "        fi\n"
            /* rbyrne: setuputil takes any message here as an error:
               "        echo Server has been stopped. ns-slapd process stopped: $PID\n"*/
           "        exit 0\n"
           "    fi\n"
           "done\n"
           "if test -f $PIDFILE ; then\n"
           "    echo Server still running!! Failed to stop the ns-slapd process: $PID.  Please check the errors log for problems.\n"
           "fi\n"
           "exit 1\n",
            subdir);
    if(t) return t;

    t = gen_script(subdir, RESTART_SCRIPT,
           "\n"
           "# Script that restarts the ns-slapd server.\n"
           "# Exit status can be:\n"
           "#       0: Server restarted successfully\n"
           "#       1: Server could not be started\n"
           "#       2: Server started successfully (was not running)\n"
           "#       3: Server could not be stopped\n"
           "\n"
           "server_already_stopped=0\n"
           "%s/stop-slapd\n"
           "status=$?\n"
           "if [ $status -eq 1 ] ; then\n"
           "    exit 3;\n"
           "else\n"
           "   if [ $status -eq 2 ] ; then\n"
           "        server_already_stopped=1\n"
           "   fi\n"
           "fi\n"
           "%s/start-slapd\n"
           "status=$?\n"
           "if [ $server_already_stopped -eq 1 ] && [ $status -eq 0 ] ; then\n"
           "    exit 2;\n"
           "fi\n"
           "exit $status\n",
           subdir, subdir );
    if(t) return t;

    /* logs subdir owned by server user */
    if( (t = chownlogs(subdir, cf->servuser)) )
        return t;

    /* config subdir owned by server user */
    if( (t = chownconfig(subdir, cf->servuser)) )
        return t;

#else  /* XP_WIN32 */
    /* Windows platforms have some extra setup */
    if( (t = setup_ntserver(cf)) )
        return t;

    /* generate start script */
    t = gen_script(subdir, START_SCRIPT".bat", "net start slapd-%s\n", cf->servid);
    if(t) return t;

    /* generate stop script */
    t = gen_script(subdir, STOP_SCRIPT".bat", "net stop slapd-%s\n", cf->servid);
    if(t) return t;

    /* generate restart script */
    t = gen_script(subdir, RESTART_SCRIPT".bat", "net stop slapd-%s\n"
                   "net start slapd-%s\n", cf->servid, cf->servid);
    if(t) return t;
#endif  /* XP_WIN32 */

    return t; /* should be NULL */
}

/* ---------------------- Update server script files ---------------------- */
int update_server(server_config_s *cf)
{
    char *t;
	char error_param[BIG_LINE] = {0};

#if defined( SOLARIS )
    /*
     * Solaris 9+ specific installation 
     */
    char otherline[PATH_SIZE];
    char subdirvar[PATH_SIZE];
    char subdiretc[PATH_SIZE];
    char *sub;
#endif /* SOLARIS */

    error_param[0] = 0; /* init to empty string */

#ifdef XP_UNIX
    if (!cf->servuser)
        getSuiteSpotUserGroup(cf);
#else
    /* Abort if the service exists on NT */
    if (t = service_exists(cf->servid)) {
        PL_strncpyz(error_param, "servid", BIG_LINE);
        goto out;
    }
#endif

    if( (t = sanity_check(cf, error_param)) )
        goto out;

    t = create_scripts(cf, error_param);
    if(t) goto out;

out:
    if(t)
	{
		char *msg;
		if (error_param[0])
		{
			msg = PR_smprintf("%s.error:could not update server %s - %s",
							  error_param, cf->servid, t);
		}
		else
		{
			msg = PR_smprintf("error:could not update server %s - %s",
							  cf->servid, t);
		}
		ds_show_message(msg);
		PR_smprintf_free(msg);
		return 1;
	}
	else
		return 0;
}

/* ---------------------- Create configuration files ---------------------- */
char *create_server(server_config_s *cf, char *param_name)
{
    char line[PATH_SIZE], *t, *sroot = cf->sroot;
    char subdir[PATH_SIZE];

#if defined( SOLARIS )
    /*
     * Solaris 9+ specific installation 
     */
    char otherline[PATH_SIZE];
    char subdirvar[PATH_SIZE];
    char subdiretc[PATH_SIZE];
    char *sub;
#endif /* SOLARIS */

    if (param_name)
        param_name[0] = 0; /* init to empty string */

#ifdef XP_UNIX
    if (!cf->servuser)
        getSuiteSpotUserGroup(cf);
#else
    /* Abort if the service exists on NT */
    if (t = service_exists(cf->servid)) {
        PL_strncpyz(param_name, "servid", BIG_LINE);
        return t;
    }
#endif

    if( (t = sanity_check(cf, param_name)) )
        return t;

    /* Create slapd-nickname directory */
    PR_snprintf(subdir, sizeof(subdir), "%s%c"PRODUCT_NAME"-%s", sroot, FILE_PATHSEP, 
                 cf->servid);
    if( (create_instance_mkdir(subdir, NEWDIR_MODE)) )
        return make_error("mkdir %s failed (%s)", subdir, ds_system_errmsg());
    
    /* Create slapd-nickname/config directory */
    PR_snprintf(line, sizeof(line), "%s%cconfig", subdir, FILE_PATHSEP);
    if( (create_instance_mkdir(line, NEWDIR_MODE)) ) 
        return make_error("mkdir %s failed (%s)", line, ds_system_errmsg());

    /* Create slapd-nickname/config/schema directory */
    PR_snprintf(line, sizeof(line), "%s%cconfig%cschema", subdir, FILE_PATHSEP, FILE_PATHSEP);
    if( (create_instance_mkdir(line, NEWDIR_MODE)) ) 
        return make_error("mkdir %s failed (%s)", line, ds_system_errmsg());

#if defined (BUILD_PRESENCE)
    /* Create slapd-nickname/config/presence directory */
    PR_snprintf(line, sizeof(line), "%s%cconfig%cpresence", subdir, FILE_PATHSEP, FILE_PATHSEP);
    if( (create_instance_mkdir(line, NEWDIR_MODE)) ) 
        return make_error("mkdir %s failed (%s)", line, ds_system_errmsg());
#endif

    /* Create slapd-nickname/logs directory */
    PR_snprintf(line, sizeof(line), "%s%clogs", subdir, FILE_PATHSEP);
    if( (create_instance_mkdir(line, NEWSECDIR_MODE)) )
        return make_error("mkdir %s failed (%s)", line, ds_system_errmsg());

    /* Create httpacl directory */
    PR_snprintf(line, sizeof(line), "%s%chttpacl", cf->sroot, FILE_PATHSEP); 
    if( (create_instance_mkdir(line, NEWDIR_MODE)) )
        return make_error("mkdir %s failed (%s)", line, ds_system_errmsg());

    t = create_scripts(cf, param_name);
    if(t) return t;

#ifdef XP_WIN32
    if ( INFO_GetOperatingSystem () == OS_WINNT ) {

        if( (t =  add_ntservice(cf)) )
            return t;
    }
#endif

    /* Create subdirectories and config files for directory server */
    if( (t = install_ds(sroot, cf, param_name)) )
        return t;

    /* XXXrobm using link to start script instead of automatically doing it */
    return NULL;
}




/* ------------------------- Copied from libadmin ------------------------- */


/*
   These replace the versions in libadmin to allow error returns. 

   XXXrobm because libadmin calls itself a lot, I'm replacing ALL the
   functions this file requires
 */


int create_instance_exists(char *fn)
{
    struct stat finfo;

    if(stat(fn, &finfo) < 0)
        return 0;
    else
        return 1;
}


int create_instance_mkdir(char *dir, int mode)
{
    if(!create_instance_exists(dir)) {
#ifdef XP_UNIX
        if(mkdir(dir, mode) == -1)
#else  /* XP_WIN32 */
        if(!CreateDirectory(dir, NULL))
#endif /* XP_WIN32 */
            return -1;
    }
    return 0;
}


char *create_instance_mkdir_p(char *dir, int mode)
{
    static char errmsg[ERR_SIZE];
    struct stat fi;
    char *t;

#ifdef XP_UNIX
    t = dir + 1;
#else /* XP_WIN32 */
    t = dir + 3;
#endif /* XP_WIN32 */

    while(1) {
        t = strchr(t, FILE_PATHSEP);

        if(t) *t = '\0';
        if(stat(dir, &fi) == -1) {
            if(create_instance_mkdir(dir, mode) == -1) {
                PR_snprintf(errmsg, sizeof(errmsg), "mkdir %s failed (%s)", dir, ds_system_errmsg());
                return errmsg;
            }
        }
        if(t)
        {
            *t = FILE_PATHSEP;
            LDAP_UTF8INC(t);
        }
        else break;
    }
    return NULL;
}


int create_instance_numbers(char *target)
{
       char *p;
    for(p=target; *p; LDAP_UTF8INC(p) )
    {
        if(!ldap_utf8isdigit(p))
            return 0;
    }
    return 1;
}

#if defined( SOLARIS )
/*
 * Solaris 9+ specific installation
 */
int create_instance_symlink(char *actualpath, char *sympath)
{
    if(symlink(actualpath, sympath) == -1)
            return -1;
    return 0;
}
#endif /* SOLARIS */


/* --------------------------------- try* --------------------------------- */


/* robm This doesn't use net_ abstractions because they drag in SSL */
int trybind(char *addr, int port)
{
    int sd;
    struct sockaddr_in sa_server;
    int ret;

#ifdef XP_WIN32
    WSADATA wsd;

    if(WSAStartup(MAKEWORD(1, 1), &wsd) != 0)
        return -1;
#endif

    if ((sd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == -1)
        goto you_lose;

    if (addr == NULL)
        addr = "127.0.0.1"; /* use the local loopback address */

    memset((char *) &sa_server, 0, sizeof(sa_server));
    sa_server.sin_family=AF_INET;
    sa_server.sin_addr.s_addr = inet_addr(addr);
    sa_server.sin_port=htons((short)port);
    ret = connect(sd, (struct sockaddr *) &sa_server,sizeof(sa_server));
    if (ret == -1)
        ret = 0; /* could not connect, so port is not in use; that's good */
    else
    {
        ret = -1; /* connection succeeded, port in use, bad */
        errno = EADDRINUSE;
    }
#ifdef XP_UNIX
    close(sd);
#else
    closesocket(sd);
    WSACleanup();
#endif
    return ret;

you_lose:
#ifdef XP_WIN32
    WSACleanup();
#endif
    return -1;
}


#ifdef XP_UNIX
#include <pwd.h>
#include <fcntl.h>

int tryuser(char *user)
{
    struct passwd *pw;
    char fn[128];
    int fd, ret;

    setpwent();
    if(!(pw = getpwnam(user)))
        return -1;

    endpwent();

    if(geteuid())
        return 0;

    PR_snprintf(fn, sizeof(fn), "/tmp/trychown.%ld", (long)getpid());
    if( (fd = creat(fn, 0777)) == -1)
        return 0;         /* Hmm. */
    ret = chown(fn, pw->pw_uid, pw->pw_gid);
    close(fd);
    unlink(fn);
    return (ret == -1 ? -2 : 0);
}
#endif /* XP_UNIX */


/* --------------------------- create_instance_check* ---------------------------- */


char *create_instance_checkport(char *addr, char *sport)
{
    int port;

    port = atoi(sport);
    if((port < 1) || (port > 65535)) {
        return ("Valid port numbers are between 1 and 65535");
    }
    if(trybind(addr, port) == -1) {
        if(errno == EADDRINUSE)    {
            return make_error("Port %d is already in use", port);
        } 
        /* XXXrobm if admin server not running as root, you lose. */
        else if(errno == EACCES) {
            return ("Ports below 1024 require super user access.  "
                    "You must run the installation as root to install "
                    "on that port.");
        } else {
            ds_report_warning(DS_WARNING, "port", "That port is not available");
        }
    }
    return NULL;
}

#ifdef XP_UNIX
char *create_instance_checkuser(char *user)
{
    if (user && *user) switch(tryuser(user)) {
      case -1:
        return make_error ("Can't find a user named '%s'."
               "\nPlease select or create another user.",
               user);
      case -2:
        return make_error ("Can't change a file to be owned by %s."
               "\nPlease select or create another user.",
               user);
    }
    return NULL;
}
#endif


/* --------------------------------- copy --------------------------------- */

#define COPY_BUFFER_SIZE        4096

#ifdef XP_UNIX

 
char *create_instance_copy(char *sfile, char *dfile, int mode)
{
    int sfd, dfd, len;
    struct stat fi;
 
    char copy_buffer[COPY_BUFFER_SIZE];
    unsigned long read_len;
 
/* Make sure we're in the right umask */
    umask(022);

    if( (sfd = open(sfile, O_RDONLY)) == -1)
        return make_error("Cannot open %s for reading (%s)", sfile, 
                          ds_system_errmsg());
 
    fstat(sfd, &fi);
    if(!(S_ISREG(fi.st_mode))) {
        close(sfd);
        return make_error("%s is not a regular file", sfile);
    }
    len = fi.st_size;
 
    if( (dfd = open(dfile, O_RDWR | O_CREAT | O_TRUNC, mode)) == -1)
        return make_error("Cannot open file %s for writing (%s)", dfile, 
                          ds_system_errmsg());
 
    while(len) {
        read_len = len>COPY_BUFFER_SIZE?COPY_BUFFER_SIZE:len;
 
        if ( (read_len = read(sfd, copy_buffer, read_len)) == -1) {
            make_error("Cannot read from file %s (%s)", 
                       sfile, ds_system_errmsg());
        }
 
        if ( write(dfd, copy_buffer, read_len) != read_len) {
            make_error("Error writing to file %s from copy of %s (%s)",
                       dfile, sfile, ds_system_errmsg());
        }
 
        len -= read_len;
    }
    close(sfd);
    close(dfd);
    /* BERT! */
    return NULL;
}

#else /* XP_WIN32 */
char *create_instance_copy(char *sfile, char *dfile, int mode)
{
    HANDLE sfd, dfd, MapHandle;
    PCHAR fp;
    PCHAR fpBase;
    DWORD BytesWritten = 0;
    DWORD len;

    if( (sfd = CreateFile(sfile, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL))
                        == INVALID_HANDLE_VALUE) {
        return make_error("Cannot open file %s for reading (%s)", sfile,
                          ds_system_errmsg());
    }
    len = GetFileSize(sfd, NULL);
    if( (MapHandle = CreateFileMapping(sfd, NULL, PAGE_READONLY,
                0, 0, NULL)) == NULL) {
        return make_error("Cannot create file mapping of %s (%s)", sfile,
                          ds_system_errmsg());
    }
    if (!(fpBase = fp = MapViewOfFile(MapHandle, FILE_MAP_READ, 0, 0, 0))) {
        return make_error("Cannot map file %s (%s)", sfile, ds_system_errmsg());
    }
    if( (dfd = CreateFile(dfile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        return make_error("Cannot open destination file %s for writing (%s)",
                          dfile, ds_system_errmsg());
    }
    while ( len) {
        if(!WriteFile(dfd, fp, len, &BytesWritten, NULL)) {
            return ("Cannot write new file %s (%s)", dfile, ds_system_errmsg());
        }
        len -= BytesWritten;
        fp += BytesWritten;
    }
    CloseHandle(sfd);
    UnmapViewOfFile(fpBase);
    CloseHandle(MapHandle);
    FlushFileBuffers(dfd);
    CloseHandle(dfd);
    /* BERT! */
    return NULL;
}
#endif

static int
file_is_type_x(const char *dirname, const char *filename, PRFileType x)
{
    struct PRFileInfo inf;
    int status = 0;
    char *fullpath = PR_smprintf("%s/%s", dirname, filename);
    if (PR_SUCCESS == PR_GetFileInfo(fullpath, &inf) &&
        inf.type == x)
        status = 1;

    PR_smprintf_free(fullpath);

    return status;
}

/* return true if the given path and file corresponds to a directory */
static int
is_a_dir(const char *dirname, const char *filename)
{
    return file_is_type_x(dirname, filename, PR_FILE_DIRECTORY);
}

#if 0 /* not being used */
/* return true if the given path and file corresponds to a regular file */
static int
is_a_file(const char *dirname, const char *filename)
{
    return file_is_type_x(dirname, filename, PR_FILE_FILE);
}
#endif

static char *
ds_copy_group_files_using_mode(char *src_dir, char *dest_dir, 
                   char *filter, int use_mode)
{
    char *t = 0;
    PRDir *ds = 0;
    PRDirEntry *d = 0;
    char src_file[PATH_SIZE], dest_file[PATH_SIZE], fullname[PATH_SIZE];

    if(!(ds = PR_OpenDir(src_dir))) {
        return make_error("Can't read directory %s (%s)", src_dir, ds_system_errmsg());
    }
    while( (d = PR_ReadDir(ds, 0)) ) {
        if(d->name[0] != '.') {
            if(!filter || strstr(d->name, filter)) {
                PR_snprintf(fullname, sizeof(fullname), "%s/%s", src_dir, d->name);
                if(PR_SUCCESS != PR_Access(fullname, PR_ACCESS_EXISTS))
                    continue;
                PR_snprintf(src_file, sizeof(src_file),  "%s%c%s", src_dir,  FILE_PATHSEP, d->name);
                PR_snprintf(dest_file, sizeof(dest_file), "%s%c%s", dest_dir, FILE_PATHSEP, d->name);
                if(is_a_dir(src_dir, d->name)) {
                    char *sub_src_dir = strdup(src_file);
                    char *sub_dest_dir = strdup(dest_file);
                    if( (t = create_instance_mkdir_p(sub_dest_dir, NEWDIR_MODE)) )
                        return(t);
                    if( (t = ds_copy_group_files_using_mode(sub_src_dir, sub_dest_dir, filter, use_mode)) )
                        return t;
                    free(sub_src_dir);
                    free(sub_dest_dir);
                }
                else {
                    if( (t = create_instance_copy(src_file, dest_file, use_mode)) )
                        return t;
                }
            }
        }
    }
    PR_CloseDir(ds);
    return(NULL);
}

static char *
ds_copy_group_files(char *src_dir, char *dest_dir, char *filter)
{
    return ds_copy_group_files_using_mode(src_dir, dest_dir, filter, 
                                          NEWFILE_MODE);
}

#if 0 /* not being used */
static char *
ds_copy_group_bins(char *src_dir, char *dest_dir, char *filter, 
                   int use_mode)
{
    return ds_copy_group_files_using_mode(src_dir, dest_dir, filter, 
                                          NEWSCRIPT_MODE);
}
#endif

/* this macro was copied from libldap/tmplout.c */
#define HREF_CHAR_ACCEPTABLE( c )    (( c >= '-' && c <= '9' ) ||    \
                     ( c >= '@' && c <= 'Z' ) ||    \
                     ( c == '_' ) ||        \
                     ( c >= 'a' && c <= 'z' ))

/* this function is based on libldap/tmplout.c:strcat_escaped */
void fputs_escaped(char *s, FILE *fp)
{
    char    *hexdig = "0123456789ABCDEF";
    register unsigned char c;
    for ( ; (c = *(unsigned char*)s); ++s ) {
        if ( HREF_CHAR_ACCEPTABLE( c )) {
            putc( c, fp );
        } else {
            fprintf( fp, "%%%c%c", hexdig[ (c >> 4) & 0x0F ], hexdig[ c & 0x0F ] );
        }
    }
}

/* ------------- Create config files for Directory Server -------------- */

char *ds_cre_subdirs(char *sroot, server_config_s *cf, char *cs_path,
             struct passwd* pw)
{
    char subdir[PATH_SIZE], *t = NULL;

    /* create subdir <a_server>/db */
    PR_snprintf(subdir, sizeof(subdir), "%s%cdb", cs_path, FILE_PATHSEP);
    if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    /* create subdir <a_server>/ldif */
    PR_snprintf(subdir, sizeof(subdir), "%s%cldif", cs_path, FILE_PATHSEP);
    if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    /* create subdir <a_server>/dsml */
    PR_snprintf(subdir, sizeof(subdir), "%s%cdsml", cs_path, FILE_PATHSEP);
    if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    /* create subdir <a_server>/bak */
    PR_snprintf(subdir, sizeof(subdir), "%s%cbak", cs_path, FILE_PATHSEP);
    if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    /* Create slapd-nickname/confbak directory */
    PR_snprintf(subdir, sizeof(subdir), "%s%cconfbak", cs_path, FILE_PATHSEP);
    if( (t=create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    /* create subdir <server_root>/dsgw/context */
    PR_snprintf(subdir, sizeof(subdir), "%s%cclients", sroot, FILE_PATHSEP);
    if (is_a_dir(subdir, "dsgw")) { /* only create dsgw stuff if we are installing it */
        PR_snprintf(subdir, sizeof(subdir), "%s%cclients%cdsgw%ccontext", sroot, FILE_PATHSEP,FILE_PATHSEP,FILE_PATHSEP);
        if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
            return(t);
    }

  /* create subdir <server_root>/bin/slapd/authck */
    PR_snprintf(subdir, sizeof(subdir), "%s%cbin%cslapd%cauthck", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
    if( (t = create_instance_mkdir_p(subdir, NEWDIR_MODE)) )
        return(t);
    chownfile (pw, subdir);

    return (t);
}

#define CREATE_LDIF2DB() \
    gen_perl_script_auto(mysroot, mycs_path, "ldif2db.pl", cf)

#define CREATE_DB2INDEX() \
    gen_perl_script_auto(mysroot, mycs_path, "db2index.pl", cf)

#define CREATE_DB2LDIF() \
    gen_perl_script_auto(mysroot, mycs_path, "db2ldif.pl", cf)

#define CREATE_DB2BAK() \
    gen_perl_script_auto(mysroot, mycs_path, "db2bak.pl", cf)

#define CREATE_BAK2DB() \
    gen_perl_script_auto(mysroot, mycs_path, "bak2db.pl", cf)

#define CREATE_VERIFYDB() \
    gen_perl_script_auto(mysroot, mycs_path, "verify-db.pl", cf)

#define CREATE_REPL_MONITOR_CGI() \
    gen_perl_script_auto(mysroot, cgics_path, "repl-monitor-cgi.pl", cf)

#define CREATE_ACCOUNT_INACT(_commandName) \
    gen_perl_script_auto(mysroot, cs_path, _commandName, cf)

#define CREATE_DSML() \
    gen_perl_script_auto(mysroot, mycs_path, "dsml-activate.pl", cf)

#define CREATE_MIGRATETO5() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateTo5", cf)

#define CREATE_MIGRATE50TO51() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrate50to51", cf)

#define CREATE_MIGRATEINSTANCE5() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateInstance5", cf)

#define CREATE_MIGRATE5TO6() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrate5to6", cf)

#define CREATE_MIGRATEINSTANCE6() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateInstance6", cf)

#define CREATE_MIGRATETO6() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateTo6", cf)

#define CREATE_MIGRATE5TO7() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrate5to7", cf)

#define CREATE_MIGRATE6TO7() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrate6to7", cf)

#define CREATE_MIGRATEINSTANCE7() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateInstance7", cf)

#define CREATE_MIGRATETO7() \
    gen_perl_script_auto_for_migration(mysroot, mycs_path, "migrateTo7", cf)

#define CREATE_NEWPWPOLICY() \
    gen_perl_script_auto(mysroot, mycs_path, "ns-newpwpolicy.pl", cf)

#ifdef XP_UNIX
char *ds_gen_scripts(char *sroot, server_config_s *cf, char *cs_path)
{
    char *t = NULL;
    char server[PATH_SIZE], admin[PATH_SIZE], tools[PATH_SIZE];
    char cgics_path[PATH_SIZE];
    char *cl_scripts[7] = {"dsstop", "dsstart", "dsrestart", "dsrestore", "dsbackup", "dsimport", "dsexport"};
    char *cl_javafiles[7] = {"DSStop", "DSStart", "DSRestart", "DSRestore", "DSBackup", "DSImport", "DSExport"};
    int  cls = 0; /*Index into commandline script names and java names - RJP*/
    char *mysroot, *mycs_path;

#if defined( SOLARIS )
    /*
     * Solaris 9+ specific installation
     */
    char fn[PATH_SIZE];
#endif /* SOLARIS */

    mysroot = sroot;
    mycs_path = cs_path;

    PR_snprintf(server, sizeof(server), "%s/bin/"PRODUCT_NAME"/server", sroot);    
    PR_snprintf(admin, sizeof(admin), "%s/bin/"PRODUCT_NAME"/admin/bin", sroot);    
    PR_snprintf(tools, sizeof(tools), "%s/shared/bin", sroot);    
    PR_snprintf(cgics_path, sizeof(cgics_path), "%s%cbin%cadmin%cadmin%cbin", sroot,
            FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);

    t = gen_script(cs_path, "monitor", 
           "if [ \"x$1\" != \"x\" ];\nthen MDN=\"$1\";\nelse MDN=\"cn=monitor\";\n fi\n"

           "cd %s\nPATH=%s:$PATH;export PATH\n"
           "ldapsearch -p %s -b \"$MDN\" -s base \"objectClass=*\"\n",
                   tools, tools, cf->servport);
    if(t) return t;
    
    t = gen_script(cs_path, "saveconfig", 
        "cd %s\n"
        "echo saving configuration ...\n"
        "conf_ldif=%s/confbak/`date +%%Y_%%m_%%d_%%H%%M%%S`.ldif\n"
        "./ns-slapd db2ldif -N -D %s "
        "-s \"%s\" -a $conf_ldif -n NetscapeRoot 2>&1\n"
        "if [ \"$?\" -ge 1 ] \nthen\n"
        "    echo Error occurred while saving configuration\n"
        "    exit 1\n"
        "fi\n"
        "exit 0\n",
        server, cs_path, cs_path, cf->netscaperoot);
    if(t) return t;
    
    t = gen_script(cs_path, "restoreconfig", 
        "cd %s\n"
        "conf_ldif=`ls -1t %s/confbak/*.ldif | head -1`\n"
        "if [ -z \"$conf_ldif\" ]\n"
        "then\n"
        "    echo No configuration to restore in %s/confbak ; exit 1\n"
        "fi\n"
        "echo Restoring $conf_ldif\n"
        "./ns-slapd ldif2db -D %s"
        " -i $conf_ldif -n NetscapeRoot 2>&1\n"
        "exit $?\n",
        server, cs_path, cs_path, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "ldif2db", 
        "cd %s\n"
        "if [ $# -lt 4 ]\nthen\n"
        "\techo \"Usage: ldif2db -n backend_instance | {-s includesuffix}* [{-x excludesuffix}*]\"\n"
        "\techo \"               {-i ldiffile}* [-O]\"\n"
        "\techo \"Note: either \\\"-n backend_instance\\\" or \\\"-s includesuffix\\\" and \\\"-i ldiffile\\\" are required.\"\n"
        "\texit 1\n"
        "fi\n\n"
        "echo importing data ...\n"
        "./ns-slapd ldif2db -D %s \"$@\" 2>&1\n"
        "exit $?\n",
        server, cs_path);
    if(t) return t;

#if defined(UPGRADEDB)
    t = gen_script(cs_path, "upgradedb", 
        "cd %s\n"
        "if [ \"$#\" -eq 1 ]\nthen\n"
        "\tbak_dir=$1\nelse\n"
        "\tbak_dir=%s/bak/upgradedb_`date +%%Y_%%m_%%d_%%H_%%M_%%S`\nfi\n\n"
        "echo upgrade index files ...\n"
        "./ns-slapd upgradedb -D %s -a $bak_dir\n",
        server, cs_path, cs_path);
    if(t) return t;
#endif

    /* new code for dsml import */
    t = gen_script(cs_path, "dsml2db", 
        "cd %s\n"
        "if [ $# -lt 4 ]\nthen\n"
        "\techo \"Usage: dsml2db -n backend_instance | {-s includesuffix}* [{-x excludesuffix}*]\"\n"
        "\techo \"               {-i dsmlfile}\"\n"
        "\techo \"Note: either \\\"-n backend_instance\\\" or \\\"-s includesuffix\\\" and \\\"-i dsmlfile\\\" are required.\"\n"
        "\texit 1\n"
        "fi\n\n"
        "set_dsml=0\n"
        "dsml_file=\"mydummy\"\n"                
        "space=\" \"\n"
        "i=0\n"
        "for arg in \"$@\"\ndo\n"
        "\tif [ \"$arg\" = '-i' ];\n\tthen\n"
        "\t\tset_dsml=1\n"
        "\telif [ $set_dsml -eq 1 ];\n\tthen\n"
        "\t\tdsml_file=$arg\n"
        "\t\tset_dsml=2\n"
        "\telse\n"                
        "\t\teval a$i=\\\"$arg\\\"\n"
        "\t\ti=`expr $i + 1`\n"
        "\tfi\n"
        "done\n"
        "max=$i; i=0;\n"
        "shift $#\n"
        "while [ $i -lt $max ]; do\n"
        "\teval arg=\\$a$i\n"
        "\tset -- \"$@\" \"$arg\"\n"
        "\ti=`expr $i + 1`\n"
        "done\n"
        "\tif [ $dsml_file = \"mydummy\" ]\n\tthen\n\t"
        "echo \"Need a DSML file as input\""
        "\n\t\t exit 1"
        "\n\tfi\n"
        "\tif [ -f $dsml_file ] && [ -r $dsml_file ]\n\tthen\n"
        "\t\t%s/bin/base/jre/bin/java -Dverify=true -classpath %s/java/jars/crimson.jar:%s/java/ldapjdk.jar:%s/java/jars/xmltools.jar com.netscape.xmltools.DSML2LDIF $dsml_file\n"
        "\t\tif [ $? = 0 ]; then\n"
        "\t\techo importing data ...\n"
        "\t\t%s/bin/base/jre/bin/java -classpath %s/java/jars/crimson.jar:%s/java/ldapjdk.jar:%s/java/jars/xmltools.jar com.netscape.xmltools.DSML2LDIF $dsml_file | ./ns-slapd ldif2db -D %s \"$@\" -i -\n"
        "\t\texit $?\n"
        "\t\tfi\n"
        "\telse\n"
        "\t\techo \"File $dsml_file invalid. Absolute path is required.\"\n\t\texit 1\n"        
        "\tfi\n",
        server,sroot,sroot,sroot,sroot,sroot,sroot,sroot,sroot,cs_path);
    if(t) return t;
        
    t = gen_script(cs_path, "ldif2ldap", 
           "cd %s\n"
           "./ldapmodify -a -p %s -D \"$1\" -w \"$2\" -f $3\n",
           tools, cf->servport);
    if(t) return t;
    
    t = CREATE_LDIF2DB();
    if(t) return t;

    t = CREATE_DB2INDEX();
    if(t) return t;
/*
    t = CREATE_MIGRATETO5();
    if(t) return t;
    
    t = CREATE_MIGRATE50TO51();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE5();
    if(t) return t;

    t = CREATE_MIGRATE5TO6();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE6();
    if(t) return t;

    t = CREATE_MIGRATETO6();
    if(t) return t;
*/    

    t = CREATE_MIGRATE5TO7();
    if(t) return t;

    t = CREATE_MIGRATE6TO7();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE7();
    if(t) return t;

    t = CREATE_MIGRATETO7();
    if(t) return t;

    t = gen_script(cs_path, "getpwenc", 
           "cd %s\n"
           "PATH=%s:$PATH;export PATH\n"
           "if [ $# -lt 2 ]\n"
           "then\n"
           "\techo \"Usage: getpwenc scheme passwd\"\n"
           "\texit 1\n"
           "fi\n\n"
           "pwdhash -D %s -H -s \"$@\"\n",
           server, server, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "db2ldif", 
           "cd %s\n"
           "if [ \"$#\" -lt 2 ];\nthen\n"
           "\techo \"Usage: db2ldif {-n backend_instance}* | {-s includesuffix}*\"\n"
           "\techo \"               [{-x excludesuffix}*] [-a outputfile]\"\n"
           "\techo \"               [-N] [-r] [-C] [-u] [-U] [-m] [-M] [-1]\"\n"
           "\techo \"Note: either \\\"-n backend_instance\\\" or \\\"-s includesuffix\\\" is required.\"\n"
           "\texit 1\n"
           "fi\n\n"
           "set_ldif=0\n"
           "ldif_file=\"mydummy\"\n"
           "for arg in \"$@\"\ndo\n"
           "\tif [ \"$arg\" = '-a' ];\n\tthen\n"
           "\t\tset_ldif=1\n"
           "\telif [ $set_ldif -eq 1 ];\n\tthen\n"
           "\t\tldif_file=$arg\n"
           "\t\tset_ldif=2\n"
           "\tfi\n"
           "done\n"
           "if [ $ldif_file = \"mydummy\" ]\nthen\n"
           "\tldif_file=%s/ldif/`date +%%Y_%%m_%%d_%%H%%M%%S`.ldif\nfi\n"
           "if [ $set_ldif -eq 2 ]\nthen\n"
           "./ns-slapd db2ldif -D %s \"$@\"\nelse\n"
           "./ns-slapd db2ldif -D %s -a $ldif_file \"$@\"\nfi\n",
           server, cs_path, cs_path, cs_path);
    if(t) return t;

    /* new code for dsml export */
    t = gen_script(cs_path, "db2dsml", 
           "cd %s\n"
           "if [ \"$#\" -lt 2 ];\nthen\n"
           "\techo \"Usage: db2dsml {-n backend_instance} | {-s includesuffix}*\"\n"
           "\techo \"               [{-x excludesuffix}*] [-a outputfile]\"\n"
           "\techo \"               [-u]\"\n"
           "\techo \"Note: either \\\"-n backend_instance\\\" or \\\"-s includesuffix\\\" is required.\"\n"
           "\texit 1\n"
           "fi\n\n"
           "set_dsml=0\n"
           "dsml_file=\"mydummy\"\n"
           "arg_list=\"\"\n"
           "space=\" \"\n"
           "for arg in \"$@\"\ndo\n"
           "\tif [ \"$arg\" = '-a' ];\n\tthen\n"
           "\t\tset_dsml=1\n"
           "\telif [ $set_dsml -eq 1 ];\n\tthen\n"
           "\t\tdsml_file=$arg\n"
           "\t\tset_dsml=2\n"
           "\telse\n"           
           "\t\targ_list=$arg_list$space$arg\n"
           "\tfi\n"
           "done\n"           
           "if [ $dsml_file = \"mydummy\" ]\nthen\n"
           "\tdsml_file=%s/dsml/`date +%%Y_%%m_%%d_%%H%%M%%S`.dsml\n"
           "\techo dsmlfile: $dsml_file\n"
           "fi\n"
           "%s/bin/base/jre/bin/java -Dverify=true -classpath %s/java/ldapjdk.jar:%s/java/jars/xmltools.jar com.netscape.xmltools.LDIF2DSML -s -o $dsml_file \n"
           "if [ $? = 0 ]; then\n"
           "\t./ns-slapd db2ldif -D %s \"$@\" -a - | %s/bin/base/jre/bin/java -classpath %s/java/ldapjdk.jar:%s/java/jars/xmltools.jar com.netscape.xmltools.LDIF2DSML -s -o $dsml_file \n"
           "fi\n",
            server, cs_path, sroot, sroot, sroot, cs_path, sroot, sroot, sroot);
    if(t) return t;

    t = CREATE_DB2LDIF();
    if(t) return t;

#if defined(UPGRADEDB)
    t = gen_script(cs_path, "db2index", 
           "cd %s\n"
           "if [ $# -eq 0 ]\n"
           "then\n"
           "\tbak_dir=%s/bak/reindex_`date +%%Y_%%m_%%d_%%H_%%M_%%S`\n"
           "\t./ns-slapd upgradedb -D %s -f -a \"$bak_dir\"\n"
           "elif [ $# -lt 4 ]\n"
           "then\n"
           "\techo \"Usage: db2index [-n backend_instance | {-s includesuffix}* -t attribute[:indextypes[:matchingrules]] -T vlvattribute]\"\n"
           "\texit 1\n"
           "else\n"
           "\t./ns-slapd db2index -D %s \"$@\"\n"
           "fi\n\n",
           server, cs_path, cs_path, cs_path);
    if(t) return t;
#endif

    t = gen_script(cs_path, "vlvindex", 
           "cd %s\n"
           "if [ $# -lt 4 ]\n"
           "then\n"
           "\techo \"Usage: vlvindex -n backend_instance | {-s includesuffix}* -T attribute\"\n"
           "\techo Note: either \\\"-n backend_instance\\\" or \\\"-s includesuffix\\\" are required.\n"
           "\texit 1\n"
           "fi\n\n"
           "./ns-slapd db2index -D %s \"$@\"\n",
           server, cs_path);
    if(t) return t;

    t = gen_script(cs_path, "db2bak", 
           "cd %s\n"
           "if [ \"$#\" -eq 1 ]\nthen\n"
           "\tbak_dir=$1\nelse\n"
           "\tbak_dir=%s/bak/`date +%%Y_%%m_%%d_%%H_%%M_%%S`\nfi\n\n"
           "./ns-slapd db2archive -D %s -a $bak_dir\n",
           server, cs_path, cs_path);
    if(t) return t;

    t = CREATE_DB2BAK();
    if(t) return t;
    
    t = gen_script(cs_path, "bak2db", 
           "if [ $# -lt 1 ] || [ $# -gt 3 ]\nthen\n"
           "    echo \"Usage: bak2db archivedir [-n backendname]\"\n"
           "    exit 1\n"
           "else\n"
           "	archivedir=$1\n"
           "	shift\n"
           "fi\n"
           "while getopts \"n:\" flag\ndo\n"
           "	case $flag in\n"
           "		n) bename=$OPTARG;;\n"
           "		*) echo \"Usage: bak2db archivedir [-n backendname]\"; exit 2;;\n"
           "	esac\n"
           "done\n\n"
           "if [ 1 = `expr $archivedir : \"\\/\"` ]\nthen\n"
           "    archivedir=$archivedir\n"
           "else\n"
           "    # relative\n"
           "    archivedir=`pwd`/$archivedir\nfi\n\n"
           "cd %s\n"
           "if [ \"$#\" -eq 2 ]\nthen\n"
           "    ./ns-slapd archive2db -D %s -a $archivedir -n $bename\n"
           "else\n"
           "    ./ns-slapd archive2db -D %s -a $archivedir\n"		   
           "fi\n",
           server, cs_path, cs_path);
    if(t) return t;

    t = CREATE_BAK2DB();
    if(t) return t;

    t = CREATE_VERIFYDB();
    if(t) return t;

    t = CREATE_REPL_MONITOR_CGI();
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-inactivate.pl");
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-activate.pl");
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-accountstatus.pl");
    if(t) return t;

    t = CREATE_DSML();
    if(t) return t;

    t = CREATE_NEWPWPOLICY();
    if(t) return t;

    t = gen_script(cs_path, "suffix2instance",
           "cd %s\n"
           "if [ $# -lt 2 ]\n"
           "then\n"
           "\techo Usage: suffix2instance {-s includesuffix}*\n"
           "\texit 1\n"
           "fi\n\n"
           "./ns-slapd suffix2instance -D %s \"$@\" 2>&1\n",
           server, cs_path);
    if(t) return t;

    /*Generate the java commandline tools in bin/slapd/server*/
    for (cls = 0; cls < 7; cls++) {
        t = gen_script(server, cl_scripts[cls], 
             "cd %s\n\n"
             "lang=${LANG:=en}\n"
             "while [ $# -ge 1 ]\n"
             "do\n"
             "    if [ $1 = '-l' ]\n"
             "    then\n"
             "        shift\n"
             "        lang=$1\n"
             "    else\n"
             "        arg=\"$arg $1\"\n"
             "    fi\n"
             "    shift\n"
             "done\n"
             "./bin/base/jre/bin/jre -classpath ./bin/base/jre/lib:"
             "./bin/base/jre/lib/rt.jar:./bin/base/jre/lib/i18n.jar:"
             "./java/base.jar:./java/jars/ds40.jar:./java/jars/ds40_${lang}.jar:"
             "./java/swingall.jar:./java/ssl.zip:"
             "./java/ldapjdk.jar:./java/mcc40.jar:./java/mcc40_${lang}.jar:"
             "./java/nmclf40.jar:./java/nmclf40_${lang}.jar"
             " com.netscape.admin.dirserv.cmdln.%s $arg\n", 
             sroot, cl_javafiles[cls]);
        if(t) return t;
    }



    return (t);
}
#else
char *ds_gen_scripts(char *sroot, server_config_s *cf, char *cs_path)
{
    char *t = NULL;
    char server[PATH_SIZE], admin[PATH_SIZE], tools[PATH_SIZE];
    char cgics_path[PATH_SIZE];
    char *cl_scripts[7] = {"dsstop.bat", "dsstart.bat", "dsrestart.bat", "dsrestore.bat", "dsbackup.bat", "dsimport.bat", "dsexport.bat"};
    char *cl_javafiles[7] = {"DSStop", "DSStart", "DSRestart", "DSRestore", "DSBackup", "DSImport", "DSExport"};
    int  cls = 0; /*Index into commandline script names and java names - RJP*/
    char *mysroot, *mycs_path;

    {
        char *p, *q;
        int n;

        for (n = 0, p = sroot; p = strchr(p, '/'); n++, p++) ;
        for (p = sroot; p = strchr(p, '\\'); n++, p++) ;
        mysroot = (char *)malloc(strlen(sroot) + n + 1);
        for (p = sroot, q = mysroot; *p; p++, q++) {
            if ('/' == *p || '\\' == *p) {
                *q++ = '\\';
                *q = '\\';
            } else
                *q = *p;
        }
        *q = '\0';

        for (n = 0, p = cs_path; p = strchr(p, '/'); n++, p++) ;
        for (p = cs_path; p = strchr(p, '\\'); n++, p++) ;
        mycs_path = (char *)malloc(strlen(cs_path) + n + 1);
        for (p = cs_path, q = mycs_path; *p; p++, q++) {
            if ('/' == *p || '\\' == *p) {
                *q++ = '\\';
                *q = '\\';
            } else
                *q = *p;
        }
        *q = '\0';
    }

    PR_snprintf(server, sizeof(server), "%s/bin/"PRODUCT_NAME"/server", sroot);    
    PR_snprintf(admin, sizeof(admin), "%s/bin/"PRODUCT_NAME"/admin/bin", sroot);    
    PR_snprintf(tools, sizeof(tools), "%s/shared/bin", sroot);    
    PR_snprintf(cgics_path, sizeof(cgics_path), "%s/bin/admin/admin/bin", sroot);

    ds_unixtodospath( cs_path );
    ds_unixtodospath( server );
    ds_unixtodospath( admin );
    ds_unixtodospath( sroot );
    ds_unixtodospath( tools );
    ds_unixtodospath( cgics_path );

    t = gen_script(cs_path, "monitor.bat", 
           "@echo off\n"
           "setlocal\n"
           "set rc=0\n"
           "if %%1.==. goto noparam\n"
           "\"%s\\ldapsearch\" -p %s -b %%1 "
           "-s base \"objectClass=*\"\n"
           "set rc=%%errorlevel%%\n"
           "goto proceed\n"
           ":noparam\n"
           "\"%s\\ldapsearch\" -p %s -b \"cn=monitor\" "
           "-s base \"objectClass=*\"\n"
           "set rc=%%errorlevel%%\n"
           ":proceed\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           tools, cf->servport, tools, cf->servport);
    if(t) return t;
    
    t = gen_script(cs_path, "saveconfig.bat", 
           "@echo off\n"
           "setlocal\n"
           "set rc=0\n"
           "PATH=\"%s\";%%PATH%%\n"
           "namegen\n"
           "call bstart\n"
           "set config_ldif=%s\\confbak\\%%DATESTR%%.ldif\n"
           "call bend\n"
           "del bend.bat\n"
           "slapd db2ldif -s \"%s\" -a \"%%config_ldif%%\" -N"
           " -D \"%s\" -n NetscapeRoot 2>&1\n"
           "set rc=%%errorlevel%%\n"
           "if %%rc%%==0 goto done\n"
           "echo Error occurred while saving configuration\n"
        ":done\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           server, cs_path, cf->netscaperoot, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "restoreconfig.bat", 
           "@echo off\n"
           "setlocal\n"
           "set rc=0\n"
           "PATH=\"%s\";%%PATH%%\n"
           "set latestscript=%s\\latest_config.bat\n"
           "if EXIST \"%%latestscript%%\" del \"%%latestscript%%\"\n"
           "latest_file \"%s\\confbak\\*.ldif\" \"%%latestscript%%\"\n"
           "if not EXIST \"%%latestscript%%\" goto noconfig\n"
           "call \"%%latestscript%%\"\n"
           "del \"%%latestscript%%\"\n"
           "slapd ldif2db -D \"%s\" -i \"%%LATEST_FILE%%\""
           " -n NetscapeRoot 2>&1\n"
           "set rc=%%errorlevel%%\n"
           "if %%rc%%==0 goto done\n"
           "echo Error occurred while saving configuration\n"
           "goto done\n"
        ":noconfig\n"
           "set rc=0\n" /* no error */
           "echo No configuration to restore in %s\\confbak\n"
        ":done\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           server, cs_path, cs_path, cs_path, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "ldif2db.bat", 
                "@if not \"%%echo%%\" == \"on\" echo off\n"
                "setlocal\n"
                "set rc=0\n"
                   "PATH=\"%s\";%%PATH%%\n\n"
                "set noconfig=0\n"
                "if [%%2] == [] goto incorrect\n"
                "if [%%3] == [] goto incorrect\n"
                "if [%%4] == [] goto incorrect\n\n"
                "set args=\n"
                ":getargs\n"
                "if [%%1] == [] goto import\n"
                "set args=%%args%% %%1\n"
                "shift\n"
                "goto getargs\n\n"
                ":incorrect\n"
                ":usage\n"
                "echo \"Usage: ldif2db -n backend_instance | {-s \"includesuffix\"}* "
                "{-i ldif-file}* [-O] [{-x \"excludesuffix\"}*]\"\n"
                "set rc=1\n"
                "goto done\n\n"
                ":import\n"
                "echo importing data ...\n"
                   "slapd ldif2db -D \"%s\" %%args%% 2>&1\n\n"
                "set rc=%%errorlevel%%\n"
                ":done\n"
                "if defined MKSARGS exit %%rc%%\n"
                "exit /b %%rc%%\n",
                   server, cs_path);
    if(t) return t;

    /* new code for dsml import */
    t = gen_script(cs_path, "dsml2db.bat", 
                "@if not \"%%echo%%\" == \"on\" echo off\n"
                "setlocal\n"
                "set rc=0\n"
                   "PATH=\"%s\";%%PATH%%\n\n"
                "set noconfig=0\n"
                "if [%%2] == [] goto incorrect\n"
                "if [%%3] == [] goto incorrect\n"
                "if [%%4] == [] goto incorrect\n\n"
                "set args=\n"
                "goto getargs\n"
                ":setdsml\n"
                "set dsmlfile=\n"
                "set dsmlfile=%%2\n"
                "shift\n"
                "shift\n"
                "goto getargs\n"
                ":getargs\n"
                "if [%%1] == [] goto import\n"
                "if [%%1] == [-i] goto setdsml\n"
                "set args=%%args%% %%1\n"
                "shift\n"
                "goto getargs\n\n"
                ":incorrect\n"
                ":usage\n"
                "echo \"Usage: dsml2db -n backend_instance | {-s \"includesuffix\"}* "
                "{-i dsml-file} [{-x \"excludesuffix\"}*]\"\n"
                "set rc=1\n"
                "goto done\n\n"
                ":import\n"
                "%s\\bin\\base\\jre\\bin\\java -Dverify=true -classpath \".;%s\\java\\ldapjdk.jar;%s\\java\\jars\\crimson.jar;%s\\java\\jars\\xmltools.jar\" com.netscape.xmltools.DSML2LDIF %%dsmlfile%%\n"
                "set rc=%%errorlevel%%\n"
                "if %%rc%%==0 goto realimport else goto done\n"
                ":realimport\n"
                "echo importing data ...\n"
                   "%s\\bin\\base\\jre\\bin\\java -classpath \".;%s\\java\\ldapjdk.jar;%s\\java\\jars\\crimson.jar;%s\\java\\jars\\xmltools.jar\" com.netscape.xmltools.DSML2LDIF %%dsmlfile%% | slapd ldif2db -D \"%s\" -i - %%args%% 2>&1\n\n"
                "set rc=%%errorlevel%%\n"
                ":done\n"
                "if defined MKSARGS exit %%rc%%\n"
                "exit /b %%rc%%\n",
                   server, sroot, sroot, sroot, sroot, sroot, sroot, sroot, sroot, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "ldif2ldap.bat", 
           "@echo off\n"
           "\"%s\\ldapmodify\" -a -p %s -D %%1 -w %%2 -f %%3\n",
           tools, cf->servport);
    if(t) return t;

    t = CREATE_LDIF2DB();
    if(t) return t;
    
    t = CREATE_DB2INDEX();
    if(t) return t;

/*    
    t = CREATE_MIGRATETO5();
    if(t) return t;
    
    t = CREATE_MIGRATE50TO51();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE5();
    if(t) return t;

    t = CREATE_MIGRATETO6();
    if(t) return t;
    
    t = CREATE_MIGRATE5TO6();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE6();
    if(t) return t;
*/
    t = CREATE_MIGRATE5TO7();
    if(t) return t;

    t = CREATE_MIGRATE6TO7();
    if(t) return t;

    t = CREATE_MIGRATEINSTANCE7();
    if(t) return t;

    t = CREATE_MIGRATETO7();
    if(t) return t;

    t = gen_script(cs_path, "getpwenc.bat", 
           "@echo off\n"
           "\"%s\\pwdhash\" -D \"%s\" -H -s %%1 %%2\n",
           server, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "db2ldif.bat", 
            "@if not \"%%echo%%\" == \"on\" echo off\n\n"
            "setlocal\n"
            "set rc=0\n"
            "PATH=\"%s\";%%PATH%%\n\n"
            "if [%%2] == [] goto err\n\n"
            "set arg=\n"
            "set ldif_file=\n\n"
            ":again\n"
            "if \"%%1\" == \"\" goto next\n"
            "if \"%%1\" == \"-n\" goto doubletag\n"
            "if \"%%1\" == \"-s\" goto doubletag\n"
            "if \"%%1\" == \"-x\" goto doubletag\n"
            "if \"%%1\" == \"-a\" goto setldif\n"
            "if \"%%1\" == \"-N\" goto singletag\n"
            "if \"%%1\" == \"-r\" goto singletag\n"
            "if \"%%1\" == \"-C\" goto singletag\n"
            "if \"%%1\" == \"-u\" goto singletag\n"
            "if \"%%1\" == \"-m\" goto singletag\n"
            "if \"%%1\" == \"-o\" goto singletag\n"
            "if \"%%1\" == \"-U\" goto singletag\n"
            "if \"%%1\" == \"-M\" goto singletag\n"
            "if \"%%1\" == \"-E\" goto singletag\n"
            "goto next\n\n"
            ":doubletag\n"
            "set arg=%%1 %%2 %%arg%%\n"
            "shift\n"
            "shift\n"
            "goto again\n\n"
            ":singletag\n"
            "set arg=%%1 %%arg%%\n"
            "shift\n"
            "goto again\n\n"
            ":setldif\n"
            "set ldif_file=%%2\n"
            "shift\n"
            "shift\n"
            "goto again\n\n"
            ":next\n"
            "if not \"%%ldif_file%%\" == \"\" goto givenldif\n\n"
            "namegen\n"
            "call bstart\n"
            "set ldif_file=\"%s\\ldif\\%%DATESTR%%.ldif\"\n"
            "call bend\n"
            "del bend.bat\n\n"
            ":givenldif\n"
            "\"%s\\slapd\" db2ldif -D \"%s\" -a %%ldif_file%% %%arg%%\n"
            "set rc=%%errorlevel%%\n"
            "goto done\n\n"
            ":err\n"
            "echo \"Usage: db2ldif -n backend_instance | "
            "{-s \"includesuffix\"}* [{-x \"excludesuffix\"}*] [-N] [-r] [-C] "
            "[-u] [-U] [-m] [-M] [-1] [-a outputfile]\"\n\n"
            "set rc=1\n"
            ":done\n"
            "if defined MKSARGS exit %%rc%%\n"
            "exit /b %%rc%%\n",
                server, cs_path, server, cs_path);
    if(t) return t;

    t = CREATE_DB2LDIF();
    if(t) return t;

    /* new code for dsml export */
    t = gen_script(cs_path, "db2dsml.bat", 
            "@if not \"%%echo%%\" == \"on\" echo off\n\n"
            "setlocal\n"
            "set rc=0\n"
            "PATH=\"%s\";%%PATH%%\n\n"
            "if [%%2] == [] goto err\n\n"
            "set arg=\n"
            "set dsml_file=\n\n"
            ":again\n"
            "if \"%%1\" == \"\" goto next\n"
            "if \"%%1\" == \"-n\" goto doubletag\n"
            "if \"%%1\" == \"-s\" goto doubletag\n"
            "if \"%%1\" == \"-x\" goto doubletag\n"
            "if \"%%1\" == \"-a\" goto setdsml\n"
            "if \"%%1\" == \"-N\" goto singletag\n"
            "if \"%%1\" == \"-r\" goto singletag\n"
            "if \"%%1\" == \"-C\" goto singletag\n"
            "if \"%%1\" == \"-u\" goto singletag\n"
            "if \"%%1\" == \"-m\" goto singletag\n"
            "if \"%%1\" == \"-o\" goto singletag\n"
            "if \"%%1\" == \"-U\" goto singletag\n"
            "if \"%%1\" == \"-M\" goto singletag\n"
            "goto next\n\n"
            ":doubletag\n"
            "set arg=%%1 %%2 %%arg%%\n"
            "shift\n"
            "shift\n"
            "goto again\n\n"
            ":singletag\n"
            "set arg=%%1 %%arg%%\n"
            "shift\n"
            "goto again\n\n"
            ":setdsml\n"
            "set dsml_file=%%2\n"
            "shift\n"
            "shift\n"
            "goto again\n\n"
            ":next\n"
            "if not \"%%dsml_file%%\" == \"\" goto givendsml\n\n"
            "namegen\n"
            "call bstart\n"
            "set dsml_file=\"%s\\dsml\\%%DATESTR%%.dsml\"\n"
            "echo dsmlfile: %%dsml_file%%\n"
            "call bend\n"
            "del bend.bat\n\n"
            ":givendsml\n"
            "%s\\bin\\base\\jre\\bin\\java -Dverify=true -classpath \".;%s\\java\\ldapjdk.jar;%s\\java\\jars\\xmltools.jar\" com.netscape.xmltools.LDIF2DSML -s -o %%dsml_file%%\n"
            "set rc=%%errorlevel%%\n"
            "if %%rc%%==0 goto realimport else goto done\n\n"
            ":realimport\n"
            "\"%s\\slapd\" db2ldif -D \"%s\" -a - -1 %%arg%% | %s\\bin\\base\\jre\\bin\\java -classpath \".;%s\\java\\ldapjdk.jar;%s\\java\\jars\\xmltools.jar\" com.netscape.xmltools.LDIF2DSML -s -o %%dsml_file%%\n"
            "set rc=%%errorlevel%%\n"
            "goto done\n\n"
            ":err\n"
            "echo \"Usage: db2dsml -n backend_instance | "
            "{-s \"includesuffix\"}* [{-x \"excludesuffix\"}*]"
            "[-u] [-a outputfile]\"\n\n"
            "set rc=1\n"
            ":done\n"
            "if defined MKSARGS exit %%rc%%\n"
            "exit /b %%rc%%\n",
                server, cs_path, sroot, sroot, sroot, server, cs_path, sroot, sroot, sroot);
    if(t) return t;  
        
    t = gen_script(cs_path, "db2bak.bat", 
           "@echo off\n"
           "setlocal\n"
           "set rc=0\n"
           "PATH=\"%s\";%%PATH%%\n"
           "if %%1.==. goto nobak\n"
           "set bakdir=%%1\n"
           "goto backup\n"
           ":nobak\n"
           "namegen\n"
           "call bstart\n"
           "set bakdir=\"%s\\bak\\%%DATESTR%%\"\n"
           "call bend\n"
           "del bend.bat\n"
           ":backup\n"
           "\"%s\\slapd\" db2archive -D \"%s\" -a %%bakdir%% "
           "%%2 %%3 %%4 %%5 %%6 %%7 %%8\n"
           "set rc=%%errorlevel%%\n"
           ":done\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           server, cs_path, server, cs_path);
    if(t) return t;

    t = CREATE_DB2BAK();
    if(t) return t;
    
#if defined(UPGRADEDB)
    t = gen_script(cs_path, "db2index.bat", 
           "@echo off\n"
       "setlocal\n"
       "set rc=0\n"
           "PATH=\"%s\";%%PATH%%\n"
           "if %%1.==. goto indexall\n\n"
           "if %%2.==. goto err\n"
           "if %%3.==. goto err\n\n"
           "set bakdir=%%1\n"
           "goto backup\n\n"
           ":indexall\n"
           "namegen\n"
           "call bstart\n"
           "set bakdir=\"%s\\bak\\%%DATESTR%%\"\n"
           "call bend\n"
           "del bend.bat\n"
           "\"%s\\slapd\" upgradedb -D \"%s\" -f -a %%bakdir%%\n"
           "set rc=%%errorlevel%%\n"
           "goto done\n\n"
           ":backup\n"
           "\"%s\\slapd\" db2index -D \"%s\" " 
           "%%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8\n"
           "set rc=%%errorlevel%%\n"
           "goto done\n\n"
           ":err\n"
           "echo \"Usage: db2index [-n backend_instance | {-s instancesuffix}* -t attribute[:indextypes[:matchingrules]] -T vlvattribute]\"\n\n"
           "set rc=1\n"
           ":done\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           server, cs_path, server, cs_path, server, cs_path);
    if(t) return t;
#endif

    t = gen_script(cs_path, "vlvindex.bat", 
           "@echo off\n"
            "setlocal\n"
            "set rc=0\n"
            "if [%%2] == [] goto usage\n"
            "if [%%3] == [] goto usage\n"
            "if [%%4] == [] goto usage\n\n"
           "\"%s\\slapd\" db2index -D \"%s\" \"%%@\"\n"
            "set rc=%%errorlevel%%\n"
            "goto done\n\n"
            ":usage\n"
            "echo \"Usage: vlvindex -n backend_instance | {-s includesuffix}* {-T attribute}\"\n\n"
            "set rc=1\n"
            ":done\n"
            "if defined MKSARGS exit %%rc%%\n"
            "exit /b %%rc%%\n",
           server, cs_path);
    if(t) return t;
    
    t = gen_script(cs_path, "bak2db.bat", 
            "@echo off\n"
            "pushd & setlocal\n\n"
            "if [%%1] == [] (goto :usage)\n"
            "if not [%%4] == [] (goto :usage)\n\n"
            "set archivedir=%%1\n"
            "set rc=0\n\n"
            ":getopts\n"
            "shift\n"
            "if [%%1]==[] (goto :main)\n"
            "if [%%1]==[-n] (if not [%%2]==[] (set bename=%%2) else (goto :usage)) else (goto :getopts)\n\n"
            ":main\n"
            "call :relative %%archivedir%%\n"
            "if defined bename (\n"
            "\"%s\\slapd\" archive2db -D \"%s\" -a %%archivedir%% -n %%bename%%\n"
            ") else (\n"
            "\"%s\\slapd\" archive2db -D \"%s\" -a %%archivedir%%\n"
            ")\n"
            "set rc=%%ERRORLEVEL%%\n"
            "popd\n"
            "goto :done\n\n"
            "goto :EOF\n"
            ":usage\n"
            "echo %%0 archivedir [-n backendname]\n"
            "goto :done\n\n"
            "goto :EOF\n"
            ":relative\n"
            "set archivedir=%%~f1\n\n"
            "goto :EOF\n"
            ":done\n"
            "if defined MKSARGS exit %%rc%%\n"
            "exit /b %%rc%%\n",
            server, cs_path, server, cs_path);
    if(t) return t;

#if defined(UPGRADEDB)
    t = gen_script(cs_path, "upgradedb.bat", 
            "@echo off\n"
        "setlocal\n"
        "set rc=0\n"
            "PATH=\"%s\";%%PATH%%\n"
            "if %%1.==. goto nobak\n"
            "set bakdir=%%1\n"
            "goto backup\n"
            ":nobak\n"
            "namegen\n"
            "call bstart\n"
            "set bakdir=\"%s\\bak\\upgradedb_%%DATESTR%%\"\n"
            "call bend\n"
            "del bend.bat\n"
            ":backup\n"
            "\"%s\\slapd\" upgradedb -D \"%s\" -a %%bakdir%% "
            "%%2 %%3 %%4 %%5 %%6 %%7 %%8\n"
            "set rc=%%errorlevel%%\n"
            ":done\n"
            "if defined MKSARGS exit %%rc%%\n"
            "exit /b %%rc%%\n",
            server, cs_path, server, cs_path);
    if(t) return t;
#endif

    t = CREATE_BAK2DB();
    if(t) return t;

    t = CREATE_VERIFYDB();
    if(t) return t;

    t = CREATE_REPL_MONITOR_CGI();
    if(t) return t;

    t = gen_script(cs_path, "suffix2instance.bat",
           "@if not \"%%echo%%\" == \"on\" echo off\n\n"
           "setlocal\n"
           "set rc=0\n"
           "PATH=\"%s\";%%PATH%%\n\n"
           "if [%%2] == [] goto err\n\n"
           "set arg=\n\n"
           ":again\n"
           "if \"%%1\" == \"\" goto next\n"
           "if \"%%1\" == \"-s\" goto doubletag\n"
           "shift\n"
           "goto again\n\n"
           ":doubletag\n"
           "set arg=%%1 %%2 %%arg%%\n"
           "shift\n"
           "shift\n"
           "goto again\n\n"
           ":next\n"
           "\"%s\\slapd\" suffix2instance -D \"%s\" %%arg%%\n"
           "set rc=%%errorlevel%%\n"
           "goto done\n\n"
           ":err\n"
           "echo Usage: suffix2instance {-s \"suffix\"}*\n\n"
           "set rc=1\n"
           ":done\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           server, server, cs_path);
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-inactivate.pl");
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-activate.pl");
    if(t) return t;

    t = CREATE_ACCOUNT_INACT("ns-accountstatus.pl");
    if(t) return t;

    t = CREATE_DSML();
    if(t) return t;

    t = gen_script(cs_path, "dsml-activate.bat",
           "@echo off\n"
           "setlocal\n"
           "PATH=%s\\bin\\slapd\\admin\\bin;%%PATH%%\n"
           "perl \"%s\\dsml-activate.pl\" %%*\n"
           "set rc=%%errorlevel%%\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           sroot, cs_path);
    if(t) return t;



    t = CREATE_NEWPWPOLICY();
    if(t) return t;

    t = gen_script(cs_path, "ns-newpwpolicy.cmd",
           "@echo off\n"
           "setlocal\n"
           "PATH=%s\\bin\\slapd\\admin\\bin;%%PATH%%\n"
           "perl \"%s\\ns-newpwpolicy.pl\" %%*\n"
           "set rc=%%errorlevel%%\n"
           "if defined MKSARGS exit %%rc%%\n"
           "exit /b %%rc%%\n",
           sroot, cs_path);
    if(t) return t;

    free(mysroot);
    free(mycs_path);

    /*Generate the java commandline tools in bin/slapd/server*/
    for (cls = 0; cls < 7; cls++) {
        t = gen_script(server, cl_scripts[cls], 
             "@echo off\npushd \"%s\"\n\n"
             "setlocal\n"
             "set LANG=en\n"
             "set arg=\n"
             "set rc=0\n"
             ":getarg\n"
             "if %%1.==. goto start\n"
             "if %%1==-l goto getlang\n"
             "set arg=%%arg%% %%1\n"
             "shift\n"
             "goto getarg\n"
             ":getlang\n"
             "shift\n"
             "set LANG=%%1\n"
             "shift\n"
             "goto getarg\n"
             ":start\n"
             ".\\bin\\base\\jre\\bin\\jre  -classpath "
             ".;.\\java;.\\bin\\base\\jre\\lib;"
             ".\\bin\\base\\jre\\lib\\rt.jar;.\\bin\\base\\jre\\lib\\i18n.jar;"
             ".\\java\\base.jar;.\\java\\jars\\ds40.jar;.\\java\\jars\\ds40_%%LANG%%.jar;"
             ".\\java\\swingall.jar;.\\java\\ssl.zip;"
             ".\\java\\ldapjdk.jar;.\\java\\mcc40.jar;.\\java\\mcc40_%%LANG%%.jar;"
             ".\\java\\nmclf40.jar;.\\java\\nmclf40_%%LANG%%.jar "
             "com.netscape.admin.dirserv.cmdln.%s  %%arg%%\n"
             "set rc=%%errorlevel%%\n"
             "popd\n"
             "if defined MKSARGS exit %%rc%%\n"
             "exit /b %%rc%%\n", 
             sroot, cl_javafiles[cls]);             
        if(t) return t;
    }



    return (t);
}
#endif


void
suffix_gen_conf(FILE* f, char * suffix, char *be_name)
{
    char* belowdn;
    
    fprintf(f, "dn: cn=%s,cn=ldbm database,cn=plugins,cn=config\n", be_name);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "objectclass: nsBackendInstance\n");
    fprintf(f, "nsslapd-cachesize: -1\n");
    fprintf(f, "nsslapd-cachememsize: 10485760\n");
    fprintf(f, "nsslapd-suffix: %s\n", suffix);
    fprintf(f, "cn: %s\n", be_name);
    fprintf(f, "\n");

    fprintf(f, "dn: cn=monitor,cn=%s,cn=ldbm database,cn=plugins,cn=config\n", be_name);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: monitor\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=\"%s\",cn=mapping tree,cn=config\n", suffix);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "objectclass: nsMappingTree\n");
    fprintf(f, "cn: \"%s\"\n", suffix);
    fprintf(f, "nsslapd-state: backend\n");
    fprintf(f, "nsslapd-backend: %s\n", be_name);
    fprintf(f, "\n");

    /* Parent entry for attribute encryption config entries */

    fprintf(f, "dn: cn=encrypted attributes,cn=%s,cn=ldbm database,cn=plugins,cn=config\n", be_name);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: encrypted attributes\n");
    fprintf(f, "\n");

    /* Parent entry for attribute encryption keys */

    fprintf(f, "dn: cn=encrypted attribute keys,cn=%s,cn=ldbm database,cn=plugins,cn=config\n", be_name);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: encrypted attributes keys\n");
    fprintf(f, "\n");

    /* Indexes for the ldbm instance */

    fprintf(f, "dn: cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config\n", be_name);
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: index\n");
    fprintf(f, "\n");

    belowdn = PR_smprintf("cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config", be_name);
    ds_gen_index(f, belowdn);
	PR_smprintf_free(belowdn);
    
    /* done with ldbm entries */
}

#define MKSYNTAX(_name,_fn) do { \
    fprintf(f, "dn: cn=%s,cn=plugins,cn=config\n", (_name)); \
    fprintf(f, "objectclass: top\n"); \
    fprintf(f, "objectclass: nsSlapdPlugin\n"); \
    fprintf(f, "objectclass: extensibleObject\n"); \
    fprintf(f, "cn: %s\n",(_name)); \
    fprintf(f, "nsslapd-pluginpath: %s/lib/syntax-plugin%s\n", sroot, shared_lib); \
    fprintf(f, "nsslapd-plugininitfunc: %s\n", (_fn)); \
    fprintf(f, "nsslapd-plugintype: syntax\n"); \
    fprintf(f, "nsslapd-pluginenabled: on\n"); \
    fprintf(f, "\n"); \
  } while (0)

char *ds_gen_confs(char *sroot, server_config_s *cf, 
    char *cs_path)
{
    char* t = NULL;
    char src[PATH_SIZE], dest[PATH_SIZE];
    char fn[PATH_SIZE], line[1024];
    FILE *f = 0, *srcf = 0;
    int  rootdse = 0;
    char *shared_lib;

    PR_snprintf(fn, sizeof(fn), "%s%cconfig%cdse.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    if(!(f = fopen(fn, "w")))
        return make_error("Can't write to %s (%s)", fn, ds_system_errmsg());

#if defined( XP_WIN32 )
    shared_lib = ".dll";
#else
#ifdef HPUX
#ifdef __ia64
    shared_lib = ".so";
#else
    shared_lib = ".sl";
#endif
#else
#ifdef AIX
#if OSVERSION >= 4200
    shared_lib = ".so";
#else
    shared_lib = "_shr.a";
#endif
#else
    shared_lib = ".so";
#endif
#endif
#endif

    fprintf(f, "dn: cn=config\n");
    fprintf(f, "cn: config\n");
    fprintf(f, "objectclass:top\n");
    fprintf(f, "objectclass:extensibleObject\n");
    fprintf(f, "objectclass:nsslapdConfig\n");
    fprintf(f, "nsslapd-accesslog-logging-enabled: on\n");
    fprintf(f, "nsslapd-accesslog-maxlogsperdir: 10\n");
    fprintf(f, "nsslapd-accesslog-mode: 600\n");
    fprintf(f, "nsslapd-accesslog-maxlogsize: 100\n");
    fprintf(f, "nsslapd-accesslog-logrotationtime: 1\n");
    fprintf(f, "nsslapd-accesslog-logrotationtimeunit: day\n");
    fprintf(f, "nsslapd-accesslog-logrotationsync-enabled: off\n");
    fprintf(f, "nsslapd-accesslog-logrotationsynchour: 0\n");
    fprintf(f, "nsslapd-accesslog-logrotationsyncmin: 0\n");
    fprintf(f, "nsslapd-accesslog: %s/logs/access\n", cs_path);
    fprintf(f, "nsslapd-enquote-sup-oc: off\n");
    fprintf(f, "nsslapd-localhost: %s\n", cf->servname);
    fprintf(f, "nsslapd-schemacheck: %s\n",
            (cf->disable_schema_checking && !strcmp(cf->disable_schema_checking, "1")) ? "off" : "on");
    fprintf(f, "nsslapd-rewrite-rfc1274: off\n");
    fprintf(f, "nsslapd-return-exact-case: on\n");
    fprintf(f, "nsslapd-ssl-check-hostname: on\n");
    fprintf(f, "nsslapd-port: %s\n", cf->servport);
#if !defined( XP_WIN32 )
    if (cf->servuser && *(cf->servuser)) {
        fprintf(f, "nsslapd-localuser: %s\n", cf->servuser);
    }
#endif
    fprintf(f, "nsslapd-errorlog-logging-enabled: on\n");
    fprintf(f, "nsslapd-errorlog-mode: 600\n");
    fprintf(f, "nsslapd-errorlog-maxlogsperdir: 2\n");
    fprintf(f, "nsslapd-errorlog-maxlogsize: 100\n");
    fprintf(f, "nsslapd-errorlog-logrotationtime: 1\n");
    fprintf(f, "nsslapd-errorlog-logrotationtimeunit: week\n");
    fprintf(f, "nsslapd-errorlog-logrotationsync-enabled: off\n");
    fprintf(f, "nsslapd-errorlog-logrotationsynchour: 0\n");
    fprintf(f, "nsslapd-errorlog-logrotationsyncmin: 0\n");
    fprintf(f, "nsslapd-errorlog: %s/logs/errors\n", cs_path);
    if (cf->loglevel)
        fprintf(f, "nsslapd-errorlog-level: %s\n", cf->loglevel);
    fprintf(f, "nsslapd-auditlog: %s/logs/audit\n", cs_path);
    fprintf(f, "nsslapd-auditlog-mode: 600\n");
    fprintf(f, "nsslapd-auditlog-maxlogsize: 100\n");
    fprintf(f, "nsslapd-auditlog-logrotationtime: 1\n");
    fprintf(f, "nsslapd-auditlog-logrotationtimeunit: day\n");
    fprintf(f, "nsslapd-rootdn: %s\n", cf->rootdn);
#if !defined(_WIN32) && !defined(AIX)
    {
        unsigned int maxdescriptors = FD_SETSIZE;
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
            maxdescriptors = (unsigned int)rl.rlim_max;
        fprintf(f, "nsslapd-maxdescriptors: %d\n", maxdescriptors);
    }
#endif
    fprintf(f, "nsslapd-max-filter-nest-level: 40\n" );
    fprintf(f, "nsslapd-rootpw: %s\n", cf->roothashedpw);
    if (getenv("DEBUG_SINGLE_THREADED"))
        fprintf(f, "nsslapd-threadnumber: 1\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=plugins, cn=config\nobjectclass: top\nobjectclass: nsContainer\ncn: plugins\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Password Storage Schemes,cn=plugins, cn=config\n");
    fprintf(f, "objectclass: top\nobjectclass: nsContainer\ncn: Password Storage Schemes\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SSHA,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SSHA\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ssha_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SSHA256,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SSHA256\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ssha256_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SSHA384,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SSHA384\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ssha384_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SSHA512,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SSHA512\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ssha512_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SHA,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SHA\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: sha_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SHA256,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SHA256\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: sha256_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SHA384,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SHA384\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: sha384_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=SHA512,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: SHA512\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: sha512_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

#if !defined(_WIN32)
    fprintf(f, "dn: cn=CRYPT,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: CRYPT\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: crypt_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");
#endif

	fprintf(f, "dn: cn=MD5,cn=Password Storage Schemes,cn=plugins,cn=config\n");
	fprintf(f, "objectclass: top\n");
	fprintf(f, "objectclass: nsSlapdPlugin\n");
	fprintf(f, "cn: MD5\n");
	fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
	fprintf(f, "nsslapd-plugininitfunc: md5_pwd_storage_scheme_init\n");
	fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
	fprintf(f, "nsslapd-pluginenabled: on\n");
	fprintf(f, "\n");
 
    fprintf(f, "dn: cn=CLEAR,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: CLEAR\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: clear_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=NS-MTA-MD5,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "cn: NS-MTA-MD5\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/pwdstorage-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ns_mta_md5_pwd_storage_scheme_init\n");
    fprintf(f, "nsslapd-plugintype: pwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=DES,cn=Password Storage Schemes,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: DES\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/des-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: des_init\n");
    fprintf(f, "nsslapd-plugintype: reverpwdstoragescheme\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-pluginarg0: nsmultiplexorcredentials\n");
    fprintf(f, "nsslapd-pluginarg1: nsds5ReplicaCredentials\n");
    fprintf(f, "nsslapd-pluginid: des-storage-scheme\n");
    fprintf(f, "\n");
    
    MKSYNTAX("Case Ignore String Syntax","cis_init");
    MKSYNTAX("Case Exact String Syntax","ces_init");
    MKSYNTAX("Space Insensitive String Syntax","sicis_init");
    MKSYNTAX("Binary Syntax","bin_init");
    MKSYNTAX("Octet String Syntax","octetstring_init");
    MKSYNTAX("Boolean Syntax","boolean_init");
    MKSYNTAX("Generalized Time Syntax","time_init");
    MKSYNTAX("Telephone Syntax","tel_init");
    MKSYNTAX("Integer Syntax","int_init");
    MKSYNTAX("Distinguished Name Syntax","dn_init");
    MKSYNTAX("OID Syntax","oid_init");
    MKSYNTAX("URI Syntax","uri_init");
    MKSYNTAX("JPEG Syntax","jpeg_init");
    MKSYNTAX("Country String Syntax","country_init");
    MKSYNTAX("Postal Address Syntax","postal_init");

    fprintf(f, "dn: cn=State Change Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: State Change Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/statechange-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: statechange_init\n");
    fprintf(f, "nsslapd-plugintype: postoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Roles Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Roles Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/roles-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: roles_init\n");
     fprintf(f, "nsslapd-plugintype: postoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: State Change Plugin\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: Views\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=ACL Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ACL Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/acl-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: acl_init\n");
    fprintf(f, "nsslapd-plugintype: accesscontrol\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=ACL preoperation,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ACL preoperation\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/acl-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: acl_preopInit\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Legacy Replication Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Legacy Replication Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/replication-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: replication_legacy_plugin_init\n");
    fprintf(f, "nsslapd-plugintype: object\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: Multimaster Replication Plugin\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Multimaster Replication Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Multimaster Replication Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/replication-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: replication_multimaster_plugin_init\n");
    fprintf(f, "nsslapd-plugintype: object\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: ldbm database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: DES\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Retro Changelog Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Retro Changelog Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/retrocl-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: retrocl_plugin_init\n");
    fprintf(f, "nsslapd-plugintype: object\n");
    fprintf(f, "nsslapd-pluginenabled: off\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");


    /* cos needs to be placed before other same type'ed plugins (postoperation) */
    fprintf(f, "dn: cn=Class of Service,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Class of Service\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/cos-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: cos_init\n");
    fprintf(f, "nsslapd-plugintype: postoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: State Change Plugin\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: Views\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=Views,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Views\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/views-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: views_init\n");
    fprintf(f, "nsslapd-plugintype: object\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: State Change Plugin\n");
    fprintf(f, "\n");

    /* 
     * LP: Turn referential integrity plugin OFF by default 
     * defect 518862 
     */
    fprintf(f, "dn: cn=referential integrity postoperation,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: referential integrity postoperation\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/referint-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: referint_postop_init\n");
    fprintf(f, "nsslapd-plugintype: postoperation\n");
    fprintf(f, "nsslapd-pluginenabled: off\n");
    fprintf(f, "nsslapd-pluginArg0: %d\n", REFERINT_DELAY);
    fprintf(f, "nsslapd-pluginArg1: %s/logs/referint\n", cs_path);
    fprintf(f, "nsslapd-pluginArg2: %d\n", REFERINT_LOG_CHANGES);
    fprintf(f, "nsslapd-pluginArg3: member\n");
    fprintf(f, "nsslapd-pluginArg4: uniquemember\n");
     fprintf(f, "nsslapd-pluginArg5: owner\n");
     fprintf(f, "nsslapd-pluginArg6: seeAlso\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");
/*
    NT synch is dead as of 5.0

    fprintf(f, "dn: cn=ntSynchService preoperation,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ntSynchService preoperation\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/ntsynch-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: libntsynch_plugin_preop_init\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=ntSynchService postoperation,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ntSynchService postoperation\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/ntsynch-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: libntsynch_plugin_postop_init\n");
    fprintf(f, "nsslapd-plugintype: postoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");
*/
    if (!cf->use_existing_user_ds) {
        t = cf->suffix;
    } else {
        t = cf->netscaperoot;
    }

    /* 
     * LP: Turn attribute uniqueness plugin OFF by default 
     * defect 518862 
     */
    fprintf(f, "dn: cn=attribute uniqueness,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: attribute uniqueness\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/attr-unique-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: NSUniqueAttr_Init\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: off\n");
    fprintf(f, "nsslapd-pluginarg0: uid\n");
    fprintf(f, "nsslapd-pluginarg1: %s\n", t);
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=7-bit check,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: 7-bit check\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/attr-unique-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: NS7bitAttr_Init\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-pluginarg0: uid\n");
    fprintf(f, "nsslapd-pluginarg1: mail\n");
    fprintf(f, "nsslapd-pluginarg2: userpassword\n");
    fprintf(f, "nsslapd-pluginarg3: ,\n");
    fprintf(f, "nsslapd-pluginarg4: %s\n", t);
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");

    t = 0;

    fprintf(f, "dn: cn=Internationalization Plugin,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Internationalization Plugin\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/liblcoll%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: orderingRule_init\n");
    fprintf(f, "nsslapd-plugintype: matchingRule\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-pluginarg0: %s/config/slapd-collations.conf\n", cs_path);
    fprintf(f, "\n");

    /* The HTTP client plugin */
    fprintf(f, "dn: cn=HTTP Client,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: HTTP Client\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/http-client-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: http_client_init\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "\n");

#if defined (BUILD_PRESENCE)
    /* The IM presence plugin root */
    fprintf(f, "dn: cn=Presence,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Presence\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/presence-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: presence_init\n");
    fprintf(f, "nsslapd-plugintype: preoperation\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
    fprintf(f, "nsslapd-plugin-depends-on-named: HTTP Client\n");
    fprintf(f, "\n");

    /* The AIM presence plugin */
    fprintf(f, "dn: cn=AIM Presence,cn=Presence,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: AIM Presence\n");
    fprintf(f, "nsim-id: nsAIMid\n");
    fprintf(f, "nsim-urltext: http://big.oscar.aol.com/$nsaimid?on_url=http://online&off_url=http://offline\n");
    fprintf(f, "nsim-urlgraphic: http://big.oscar.aol.com/$nsaimid?on_url=http://online&off_url=http://offline\n");
    fprintf(f, "nsim-onvaluemaptext: http://online\n");
    fprintf(f, "nsim-offvaluemaptext: http://offline\n");
    fprintf(f, "nsim-urltextreturntype: TEXT\n");
    fprintf(f, "nsim-urlgraphicreturntype: TEXT\n");
    fprintf(f, "nsim-requestmethod: REDIRECT\n");
    fprintf(f, "nsim-statustext: nsAIMStatusText\n");
    fprintf(f, "nsim-statusgraphic: nsAIMStatusGraphic\n");
    fprintf(f, "\n");

    /* The ICQ presence plugin */
    fprintf(f, "dn: cn=ICQ Presence,cn=Presence,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ICQ Presence\n");
    fprintf(f, "nsim-id: nsICQid\n");
    fprintf(f, "nsim-urltext: http://online.mirabilis.com/scripts/online.dll?icq=$nsicqid&img=5\n");
    fprintf(f, "nsim-urlgraphic: http://online.mirabilis.com/scripts/online.dll?icq=$nsicqid&img=5\n");
    fprintf(f, "nsim-onvaluemaptext: /lib/image/0,,4367,00.gif\n");
    fprintf(f, "nsim-offvaluemaptext: /lib/image/0,,4349,00.gif\n");
    fprintf(f, "nsim-urltextreturntype: TEXT\n");
    fprintf(f, "nsim-urlgraphicreturntype: TEXT\n");
    fprintf(f, "nsim-requestmethod: REDIRECT\n");
    fprintf(f, "nsim-statustext: nsICQStatusText\n");
    fprintf(f, "nsim-statusgraphic: nsICQStatusGraphic\n");
    fprintf(f, "\n");

    /* The Yahoo presence plugin */
    fprintf(f, "dn: cn=Yahoo Presence,cn=Presence,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: Yahoo Presence\n");
    fprintf(f, "nsim-id: nsYIMid\n");
    fprintf(f, "nsim-urltext: http://opi.yahoo.com/online?u=$nsyimid&m=t\n");
    fprintf(f, "nsim-urlgraphic: http://opi.yahoo.com/online?u=$nsyimid&m=g&t=0\n");
    fprintf(f, "nsim-onvaluemaptext: $nsyimid is ONLINE\n");
    fprintf(f, "nsim-offvaluemaptext: $nsyimid is NOT ONLINE\n");
    fprintf(f, "nsim-urltextreturntype: TEXT\n");
    fprintf(f, "nsim-urlgraphicreturntype: BINARY\n");
    fprintf(f, "nsim-requestmethod: GET\n");
    fprintf(f, "nsim-statustext: nsYIMStatusText\n");
    fprintf(f, "nsim-statusgraphic: nsYIMStatusGraphic\n");
    fprintf(f, "\n");
#endif

        /* enable pass thru authentication */
    if (cf->use_existing_config_ds || cf->use_existing_user_ds)
    {
        LDAPURLDesc *desc = 0;
        char *url = cf->use_existing_config_ds ? cf->config_ldap_url :
            cf->user_ldap_url;
        if (url && !ldap_url_parse(url, &desc) && desc)
        {
            char *suffix = desc->lud_dn;
            char *service = !strncmp(url, "ldaps:", strlen("ldaps:")) ?
                "ldaps" : "ldap";
            if (cf->use_existing_config_ds)
            {
                suffix = cf->netscaperoot;
            }

            suffix = ds_URL_encode(suffix);
            fprintf(f, "dn: cn=Pass Through Authentication,cn=plugins,cn=config\n");
            fprintf(f, "objectclass: top\n");
            fprintf(f, "objectclass: nsSlapdPlugin\n");
            fprintf(f, "objectclass: extensibleObject\n");
            fprintf(f, "cn: Pass Through Authentication\n");
            fprintf(f, "nsslapd-pluginpath: %s/lib/passthru-plugin%s\n", sroot, shared_lib);
            fprintf(f, "nsslapd-plugininitfunc: passthruauth_init\n");
            fprintf(f, "nsslapd-plugintype: preoperation\n");
            fprintf(f, "nsslapd-pluginenabled: on\n");
            fprintf(f, "nsslapd-pluginarg0: %s://%s:%d/%s\n", service, desc->lud_host, desc->lud_port,
                    suffix);
            fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
            fprintf(f, "\n");
            free(suffix);
            ldap_free_urldesc(desc);
        }
    }

#ifdef BUILD_PAM_PASSTHRU
#if !defined( XP_WIN32 )
	/* PAM Pass Through Auth plugin - off by default */
	fprintf(f, "dn: cn=PAM Pass Through Auth,cn=plugins,cn=config\n");
	fprintf(f, "objectclass: top\n");
	fprintf(f, "objectclass: nsSlapdPlugin\n");
	fprintf(f, "objectclass: extensibleObject\n");
	fprintf(f, "objectclass: pamConfig\n");
	fprintf(f, "cn: PAM Pass Through Auth\n");
	fprintf(f, "nsslapd-pluginpath: %s/lib/pam-passthru-plugin%s\n", sroot, shared_lib);
	fprintf(f, "nsslapd-plugininitfunc: pam_passthruauth_init\n");
	fprintf(f, "nsslapd-plugintype: preoperation\n");
	fprintf(f, "nsslapd-pluginenabled: off\n");
	fprintf(f, "nsslapd-pluginLoadGlobal: true\n");
	fprintf(f, "nsslapd-plugin-depends-on-type: database\n");
	fprintf(f, "pamMissingSuffix: ALLOW\n");
	if (cf->netscaperoot) {
		fprintf(f, "pamExcludeSuffix: %s\n", cf->netscaperoot);
	}
	fprintf(f, "pamExcludeSuffix: cn=config\n");
	fprintf(f, "pamMapMethod: RDN\n");
	fprintf(f, "pamFallback: FALSE\n");
	fprintf(f, "pamSecure: TRUE\n");
	fprintf(f, "pamService: ldapserver\n");
	fprintf(f, "\n");
#endif /* NO PAM FOR WINDOWS */
#endif /* BUILD_PAM_PASSTHRU */

    fprintf(f, "dn: cn=ldbm database,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: ldbm database\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/libback-ldbm%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: ldbm_back_init\n");
    fprintf(f, "nsslapd-plugintype: database\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: Syntax\n");
    fprintf(f, "nsslapd-plugin-depends-on-type: matchingRule\n");
    fprintf(f, "\n");

    if (strlen(cf->suffix) == 0){
        rootdse = 1;
    }

    /* Entries for the ldbm plugin */
    fprintf(f, "dn: cn=config,cn=ldbm database,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: config\n");
    fprintf(f, "nsslapd-lookthroughlimit: 5000\n");
    fprintf(f, "nsslapd-mode: 600\n");
    fprintf(f, "nsslapd-directory: %s/db\n", cs_path);
    fprintf(f, "nsslapd-dbcachesize: 10485760\n");
    /* will be default from 6.2 or 6.11... */
    if (getenv("USE_OLD_IDL_SWITCH")) {
        fprintf(f, "nsslapd-idl-switch: old\n");
    }
    fprintf(f, "\n");

    /* Placeholder for the default user-defined ldbm indexes */
    fprintf(f, "dn: cn=default indexes, cn=config,cn=ldbm database,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: default indexes\n");
    fprintf(f, "\n");

    /* default user-defined ldbm indexes */
    ds_gen_index(f, "cn=default indexes, cn=config,cn=ldbm database,cn=plugins,cn=config");




    fprintf(f, "dn: cn=monitor, cn=ldbm database, cn=plugins, cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: monitor\n");
    fprintf(f, "\n");

        fprintf(f, "dn: cn=database, cn=monitor, cn=ldbm database, cn=plugins, cn=config\n");
        fprintf(f, "objectclass: top\n");
        fprintf(f, "objectclass: extensibleObject\n");
        fprintf(f, "cn: database\n");
        fprintf(f, "\n");

    /* Entries for the chaining backend plugin */
    fprintf(f, "dn: cn=chaining database,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: nsSlapdPlugin\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: chaining database\n");
    fprintf(f, "nsslapd-pluginpath: %s/lib/chainingdb-plugin%s\n", sroot, shared_lib);
    fprintf(f, "nsslapd-plugininitfunc: chaining_back_init\n");
    fprintf(f, "nsslapd-plugintype: database\n");
    fprintf(f, "nsslapd-pluginenabled: on\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=config,cn=chaining database,cn=plugins,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: config\n");
        fprintf(f, "nsTransmittedControls: 2.16.840.1.113730.3.4.2\n");
        fprintf(f, "nsTransmittedControls: 2.16.840.1.113730.3.4.9\n");
        fprintf(f, "nsTransmittedControls: 1.2.840.113556.1.4.473\n");
        fprintf(f, "nsTransmittedControls: 1.3.6.1.4.1.1466.29539.12\n");
    fprintf(f, "nsPossibleChainingComponents: cn=resource limits,cn=components,cn=config\n");
    fprintf(f, "nsPossibleChainingComponents: cn=certificate-based authentication,cn=components,cn=config\n");
    fprintf(f, "nsPossibleChainingComponents: cn=ACL Plugin,cn=plugins,cn=config\n");
    fprintf(f, "nsPossibleChainingComponents: cn=old plugin,cn=plugins,cn=config\n");
    fprintf(f, "nsPossibleChainingComponents: cn=referential integrity postoperation,cn=plugins,cn=config\n");
    fprintf(f, "nsPossibleChainingComponents: cn=attribute uniqueness,cn=plugins,cn=config\n");
    fprintf(f, "\n");

    free(t);
    t = NULL;

    /* suffix for the mapping tree */
    fprintf(f, "dn: cn=mapping tree,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: mapping tree\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: tasks\n");
    fprintf(f, "\n");

    /* Entries for the ldbm instances and mapping tree */
    if ( cf->netscaperoot && !cf->use_existing_config_ds)
    {
        suffix_gen_conf(f, cf->netscaperoot, "NetscapeRoot");
    }

    if (!cf->use_existing_user_ds)
    {
        suffix_gen_conf(f, cf->suffix, "userRoot");
    }
    
    if ( cf->samplesuffix && cf->suffix && PL_strcasecmp(cf->samplesuffix, cf->suffix))
    {
        suffix_gen_conf(f, cf->samplesuffix, "sampleRoot");
    }

    if ( cf->testconfig && cf->suffix && PL_strcasecmp(cf->testconfig, cf->suffix))
    {
        suffix_gen_conf(f, cf->testconfig, "testRoot");
    }


    /* tasks */
    fprintf(f, "dn: cn=import,cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: import\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=export,cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: export\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=backup,cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: backup\n");
    fprintf(f, "\n");

    fprintf(f, "dn: cn=restore,cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: restore\n");
    fprintf(f, "\n");

#if defined(UPGRADEDB)
    fprintf(f, "dn: cn=upgradedb,cn=tasks,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: upgradedb\n");
    fprintf(f, "\n");
#endif
    /* END of tasks */


    fprintf(f, "dn: cn=replication,cn=config\n");
    fprintf(f, "objectclass: top\n");
    fprintf(f, "objectclass: extensibleObject\n");
    fprintf(f, "cn: replication\n");
    fprintf(f, "\n");

    if( cf->replicationdn && *(cf->replicationdn) ) 
    {
        fprintf(f, "dn: cn=replication4,cn=replication,cn=config\n");
        fprintf(f, "cn: replication4\n");
        fprintf(f, "objectclass: top\n");
        fprintf(f, "objectclass: nsConsumer4Config\n");
        fprintf(f, "nsslapd-updatedn: %s\n", cf->replicationdn);
        fprintf(f, "nsslapd-updatepw: %s\n", cf->replicationhashedpw);
        fprintf(f, "\n");
    }

    if(cf->changelogdir && *(cf->changelogdir) )
    {
        fprintf(f, "dn: cn=changelog4,cn=config\n");
        fprintf(f, "cn: changelog4\n");
        fprintf(f, "objectclass: top\n");
        fprintf(f, "objectclass: nsChangelog4Config\n");
        fprintf(f, "nsslapd-changelogdir: %s\n", cf->changelogdir);
        fprintf(f, "nsslapd-changelogsuffix: %s\n", cf->changelogsuffix);
        fprintf(f, "nsslapd-changelogmaxage: 2d\n");
        fprintf(f, "\n");

        /* create the changelog directory */
        if( (t = create_instance_mkdir_p(cf->changelogdir, NEWDIR_MODE)) )
            return(t);
    }

    fclose (f);
    
    PR_snprintf(src, sizeof(src), "%s%cconfig%cdse.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    PR_snprintf(fn, sizeof(fn), "%s%cconfig%cdse_original.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, fn, 0600);

    /*
     * generate slapd-collations.conf
     */
    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cconfig%c%s-collations.conf",
            sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
            FILE_PATHSEP, PRODUCT_NAME);
    PR_snprintf(dest, sizeof(dest), "%s%cconfig%c%s-collations.conf", cs_path, FILE_PATHSEP,
            FILE_PATHSEP, PRODUCT_NAME);
    if (!(srcf = fopen(src, "r"))) {
        return make_error("Can't read from %s (%s)", src, ds_system_errmsg());
    }
    if (!(f = fopen(dest, "w"))) {
        return make_error("Can't write to %s (%s)", dest, ds_system_errmsg());
    }
    while (fgets(line, sizeof(line), srcf)) {
        if ((line[0] != '\0') && (fputs(line, f) == EOF)) {
            make_error("Error writing to file %s from copy of %s (%s)",
                       dest, src, ds_system_errmsg());
        }
    }
    if (!feof(srcf)) {
        make_error("Error reading from file %s (%s)", src, ds_system_errmsg());
    }
    fclose(srcf);
    fclose(f);

    PR_snprintf(src, sizeof(src), "%s/bin/slapd/install/schema", sroot);
    PR_snprintf(dest, sizeof(dest), "%s/config/schema", cs_path);
    if (NULL != (t = ds_copy_group_files(src, dest, 0)))
        return t;
        
#if defined (BUILD_PRESENCE)
    PR_snprintf(src, sizeof(src), "%s/bin/slapd/install/presence", sroot);
    PR_snprintf(dest, sizeof(dest), "%s/config/presence", cs_path);
    if (t = ds_copy_group_files(src, dest, 0))
        return t;
#endif

    /* Generate the orgchart configuration */
    PR_snprintf(src, sizeof(src), "%s/clients", sroot);
    if (is_a_dir(src, "orgchart")) {
        if (NULL != (t = ds_gen_orgchart_conf(sroot, cs_path, cf))) {
            return t;
        }
    }
        
    /* Generate dsgw.conf */
    PR_snprintf(src, sizeof(src), "%s/clients", sroot);
    if (is_a_dir(src, "dsgw")) {
        if (NULL != (t = ds_gen_gw_conf(sroot, cs_path, cf, GW_CONF))) {
            return t;
        }

        /* Generate pb.conf */
        if (NULL != (t = ds_gen_gw_conf(sroot, cs_path, cf, PB_CONF))) {
            return t;
        }
    }

    return NULL; /* Everything worked fine */
}

/*
 * Function: ds_gen_gw_conf
 *
 * Returns: pointer to error message, or NULL if all went well
 *
 * Description: This generates the gateway configuration files 
 *              for the regular gateway stuff and for the phonebook.
 *
 * Author: RJP
 *
 */
static char *
ds_gen_gw_conf(char *sroot, char *cs_path, server_config_s *cf, int conf_type)
{
    char dest[PATH_SIZE];
    char src[PATH_SIZE];
    char line[1024];
    FILE *f = NULL;
    FILE *srcf = NULL;
    char *t = NULL;
    const char *ctxt;
   
    if (conf_type == GW_CONF) {
        ctxt = "dsgw";
    } else {
        ctxt = "pb";
    }
    /*
     * generate .../dsgw/context/[dsgw|pb].conf by creating the file, placing
     * install-specific config. file lines at the start of file, and then
     * copying the rest from NS-HOME/dsgw/config/dsgw.tmpl
     */

    PR_snprintf(dest, sizeof(dest), "%s%cclients%cdsgw%ccontext%c%s.conf", sroot, FILE_PATHSEP,FILE_PATHSEP,
        FILE_PATHSEP, FILE_PATHSEP, ctxt);


    /* If the config file already exists, just return success */
    if (create_instance_exists(dest)) {
        return(NULL);
    }
    
    /* Attempt to open that bad boy */
    if(!(f = fopen(dest, "w"))) {
        return make_error("Can't write to %s (%s)", dest, ds_system_errmsg());
    }

    /* Write out the appropriate values */
    fprintf(f, "# Used by Directory Server Gateway\n");
    fprintf(f, "baseurl\t\"ldap://%s:%s/", cf->servname, cf->servport);
    fputs_escaped(cf->suffix, f);
    fputs("\"\n\n",f);
    if (cf->rootdn && *(cf->rootdn)) {
        t = ds_enquote_config_value(DS_ROOTDN, cf->rootdn);
        fprintf(f, "dirmgr\t%s\n\n", t );
        if (t != cf->rootdn) free(t);
    }

    t = ds_enquote_config_value(DS_SUFFIX, cf->suffix);
    fprintf(f, "location-suffix\t%s\n\n", t);
    if (t != cf->suffix) free(t);


    fprintf(f, "securitypath\t\"%s%calias%c%s-cert.db\"\n\n", sroot, FILE_PATHSEP, FILE_PATHSEP, ctxt );

    fprintf(f, "# The url base to the orgchart application.\n#No link from the DSGW to the orgchart will appear in the UI if this configuration line is commented out.\n");
    fprintf(f, "url-orgchart-base\thttp://%s:%s/clients/orgchart/bin/org?context=%s&data=\n\n", cf->servname, cf->adminport ? cf->adminport : "80", ctxt);

    /* copy in template */
    if (conf_type == GW_CONF) {
        PR_snprintf(src, sizeof(src), "%s%cclients%cdsgw%cconfig%cdsgw.tmpl",
            sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
    } else if (conf_type == PB_CONF) {
        PR_snprintf(src, sizeof(src), "%s%cclients%cdsgw%cpbconfig%cpb.tmpl",
            sroot, FILE_PATHSEP,FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
    } else {
        /*This should never, ever happen if this function is called correctly*/
        fclose(f);
        return make_error("Unknown gateway config file requested");
    }


    /* Try to open the dsgw.conf template file (dsgw.tmpl) */
    if(!(srcf = fopen(src, "r"))) {
        fclose(f);
        return make_error("Can't read %s (%s)", src, ds_system_errmsg());
    }
    
    while(fgets(line, sizeof(line), srcf)) {
        fputs(line, f);
    }

    fclose(srcf);
    fclose(f);

    /* Generate default.conf */
    if (conf_type == GW_CONF) {
        struct passwd* pw = NULL;
        char defaultconf[PATH_SIZE];

#if !defined( XP_WIN32 )
        /* find the server's UID and GID */
        if (cf->servuser && *(cf->servuser)) {
            if ((pw = getpwnam (cf->servuser)) == NULL) {
                return make_error("Could not find UID and GID of user '%s'.", cf->servuser);
            } else if (pw->pw_name == NULL) {
                pw->pw_name = cf->servuser;
            }
        }
#endif

        PR_snprintf(defaultconf, sizeof(defaultconf), "%s%cclients%cdsgw%ccontext%cdefault.conf", sroot, 
          FILE_PATHSEP,FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);

        create_instance_copy(dest, defaultconf, NEWFILE_MODE);
        chownfile (pw, defaultconf);
    }
    unlink(src);

    return NULL;
}


/*
 * Function: ds_gen_orgchart_conf
 *
 * Returns: pointer to error message, or NULL if all went well
 *
 * Description: This generates the orgchart configuration file
 *
 * Author: RJP
 *
 */
static char *
ds_gen_orgchart_conf(char *sroot, char *cs_path, server_config_s *cf)
{
    char dest[PATH_SIZE];
    char src[PATH_SIZE];
    char line[1024];
    FILE *f = NULL;
    FILE *srcf = NULL;
   
    /*
     * generate .../clients/orgchart/config.txt by creating the file, placing
     * install-specific config. file lines at the start of file, and then
     * copying the rest from NS-HOME/clients/orgchart/config.tmpl
     */
    PR_snprintf(dest, sizeof(dest), "%s%cclients%corgchart%cconfig.txt", sroot, FILE_PATHSEP,
        FILE_PATHSEP, FILE_PATHSEP );
    PR_snprintf(src, sizeof(src), "%s%cclients%corgchart%cconfig.tmpl", sroot, FILE_PATHSEP, 
        FILE_PATHSEP, FILE_PATHSEP);

    /* If the config file already exists, just return success */
    if (create_instance_exists(dest)) {
        return(NULL);
    }
    
    /* Attempt to open that bad boy */
    if(!(f = fopen(dest, "w"))) {
        return make_error("Cannot write to %s (%s)", dest, ds_system_errmsg());
    }

    /* Write out the appropriate values */
    fprintf(f, "#############\n#\n#\n");
    fprintf(f, "#  Configuration file for Directory Server Org Chart\n");
    fprintf(f, "#  ----------------------------------------------------------\n#\n#\n");
    fprintf(f, "#############\n\n\n#\n");
    fprintf(f, "#   Blank lines in this file, as well as lines that\n");
    fprintf(f, "#   start with at least one \"#\" character, are both ignored.\n");
    fprintf(f, "#\n#\n");
    fprintf(f, "#   Name/Value pairs below are (and need to be) separated with\n");
    fprintf(f, "#   one or more tabs  (or spaces)\n");
    fprintf(f, "#\n");

    fprintf(f, "ldap-host\t%s\n", cf->servname);
    fprintf(f, "ldap-port\t%s\n", cf->servport);
    fprintf(f, "ldap-search-base\t%s\n\n", cf->suffix); 

    fprintf(f, "#\n#  If you would like to have the phonebook icon visible, you must\n");
    fprintf(f, "#  supply the partial phonebook URL below, which will have each\n");
    fprintf(f, "#  given user's DN attribute value concatenated to the end.\n");
    fprintf(f, "#\n#  For example, you could specify below something close to:\n");
    fprintf(f, "#\n#      url-phonebook-base      http://hostname.domain.com/dsgw/bin/dosearch?context=default&hp=localhost&dn=\n#\n\n");
    fprintf(f, "url-phonebook-base\thttp://%s:%s/clients/dsgw/bin/dosearch?context=pb&hp=%s:%s&dn=\n\n",cf->servname, cf->adminport ? cf->adminport : "80", cf->servname, cf->servport);
    
    /* Try to open the config.txt template file (config.tmpl) */
    if(!(srcf = fopen(src, "r"))) {
        fclose(f);
        return make_error("Can't read %s (%s)", src, ds_system_errmsg());
    }
    
    while(fgets(line, sizeof(line), srcf)) {
        fputs(line, f);
    }

    fclose(srcf);
    fclose(f);

    unlink(src);    
    return NULL;
}

#if defined (BUILD_PRESENCE)
/*
 * Function: gen_presence_init
 *
 * Description: Creates a script to initialize images for use in the IM
 * Presence plugin.
 */
#define    PRESENCE_LDIF "init_presence_images.ldif"
static char *gen_presence_init_script(char *sroot, server_config_s *cf, 
    char *cs_path)
{
    char fn[PATH_SIZE];
    char dir[PATH_SIZE];
    FILE *f;

    PR_snprintf(dir, sizeof(dir), "%s%cconfig%cpresence",
            cs_path, FILE_PATHSEP, FILE_PATHSEP);
    PR_snprintf(fn, sizeof(fn), "%s%c%s",
            dir, FILE_PATHSEP, PRESENCE_LDIF);

    if(!(f = fopen(fn, "w")))  
        return make_error("Could not write to %s (%s).", fn, ds_system_errmsg());

    fprintf( f,
             "dn:cn=ICQ Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-onvaluemapgraphic\n"
             "nsim-onvaluemapgraphic: %s%cicq-online.gif\n"
             "\n"
             "dn:cn=ICQ Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-offvaluemapgraphic\n"
             "nsim-offvaluemapgraphic: %s%cicq-offline.gif\n"
             "\n"
             "dn:cn=ICQ Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-disabledvaluemapgraphic\n"
             "nsim-disabledvaluemapgraphic: %s%cicq-disabled.gif\n"
             "\n"
             "dn:cn=AIM Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-onvaluemapgraphic\n"
             "nsim-onvaluemapgraphic: %s%caim-online.gif\n"
             "\n"
             "dn:cn=AIM Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-offvaluemapgraphic\n"
             "nsim-offvaluemapgraphic: %s%caim-offline.gif\n"
             "\n"
             "dn:cn=AIM Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-disabledvaluemapgraphic\n"
             "nsim-disabledvaluemapgraphic: %s%caim-offline.gif\n"
             "\n"
             "dn:cn=Yahoo Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-offvaluemapgraphic\n"
             "nsim-offvaluemapgraphic: %s%cyahoo-offline.gif\n"
             "\n"
             "dn:cn=Yahoo Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-onvaluemapgraphic\n"
             "nsim-onvaluemapgraphic: %s%cyahoo-online.gif\n"
             "\n"
             "dn:cn=Yahoo Presence,cn=Presence,cn=plugins,cn=config\n"
             "changeType:modify\n"
             "replace:nsim-disabledvaluemapgraphic\n"
             "nsim-disabledvaluemapgraphic: %s%cyahoo-offline.gif\n",
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP,
             dir, FILE_PATHSEP
        );
    fclose(f);
    return NULL;
}

/*
 * Function init_presence
 *
 * Description: Runs ldapmodify to initialize the images used by the
 * IM presence plugin
 */
static int init_presence(char *sroot, server_config_s *cf, char *cs_path)
{
    char cmd[PATH_SIZE];
    char tools[PATH_SIZE];
    char precmd[PATH_SIZE];

    precmd[0] = 0;
    PR_snprintf(tools, sizeof(tools), "%s%cshared%cbin", sroot, FILE_PATHSEP, FILE_PATHSEP);    

#ifdef XP_UNIX
    PR_snprintf(precmd, sizeof(precmd), "cd %s;", tools);
#endif

    PR_snprintf(cmd, sizeof(cmd), "%s%s%cldapmodify -q -p %d -b -D \"%s\" -w \"%s\" "
            "-f %s%s%cconfig%cpresence%c%s%s",
            precmd,
            tools, FILE_PATHSEP,
            atoi(cf->servport),
            cf->rootdn,
            cf->rootpw,
            ENQUOTE, cs_path, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP,
            PRESENCE_LDIF, ENQUOTE);
    return ds_exec_and_report( cmd );
}
#endif

/*
 * Function: ds_gen_index
 *
 * Description: This generates the default index list.
 * This function is passed the parent entry below which the nsIndex
 * entries must be created. This allows to use it when creating:
 *    - the default index list (ie belowdn = cn=default indexes,cn=config...)
 *    - the userRoot backend (ie belowdn = cn=index,cn=userRoot...)
 *
 */
static void
ds_gen_index(FILE* f, char* belowdn)
{
#define MKINDEX(_name, _inst, _sys, _type1, _type2, _type3) do { \
	char *_type2str = (_type2), *_type3str = (_type3); \
    fprintf(f, "dn: cn=%s,%s\n", (_name), (_inst)); \
    fprintf(f, "objectclass: top\n"); \
    fprintf(f, "objectclass: nsIndex\n"); \
    fprintf(f, "cn: %s\n", (_name)); \
    fprintf(f, "nssystemindex: %s\n", (_sys) ? "true" : "false"); \
	if (_type1) \
        fprintf(f, "nsindextype: %s\n", (_type1)); \
    if (_type2str) \
        fprintf(f, "nsindextype: %s\n", _type2str); \
    if (_type3str) \
        fprintf(f, "nsindextype: %s\n", _type3str); \
    fprintf(f, "\n"); \
} while (0)

    MKINDEX("aci", belowdn, 1, "pres", NULL, NULL);
    MKINDEX("cn", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("entrydn", belowdn, 1, "eq", NULL, NULL);
    MKINDEX("givenName", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("mail", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("mailAlternateAddress", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("mailHost", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("member", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("nsCalXItemId", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("nsLIProfileName", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("nsUniqueId", belowdn, 1, "eq", NULL, NULL);
    MKINDEX("nswcalCALID", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("numsubordinates", belowdn, 1, "pres", NULL, NULL);
    MKINDEX("objectclass", belowdn, 1, "eq", NULL, NULL);
    MKINDEX("owner", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("parentid", belowdn, 1, "eq", NULL, NULL);
    MKINDEX("pipstatus", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("pipuid", belowdn, 0, "pres", NULL, NULL);
    MKINDEX("seeAlso", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("sn", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("telephoneNumber", belowdn, 0, "pres", "eq", "sub");
    MKINDEX("uid", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("ntUniqueId", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("ntUserDomainId", belowdn, 0, "eq", NULL, NULL);
    MKINDEX("uniquemember", belowdn, 0, "eq", NULL, NULL);
}



static char *install_ds(char *sroot, server_config_s *cf, char *param_name)
{
    SLAPD_CONFIG slapd_conf;
    QUERY_VARS query_vars;
    char *t, src[PATH_SIZE], dest[PATH_SIZE], big_line[PATH_SIZE];
    char cs_path[PATH_SIZE];
    struct passwd* pw = NULL;
    int isrunning;
    int status = 0;
#ifdef XP_WIN32
    WSADATA    wsadata;
#endif

#if !defined( XP_WIN32 )
    /* find the server's UID and GID */
    if (cf->servuser && *(cf->servuser)) {
        if ((pw = getpwnam (cf->servuser)) == NULL) {
            PL_strncpyz(param_name, "servuser", BIG_LINE);
            return make_error("Could not find UID and GID of user '%s'.", 
                      cf->servuser);
        } else if (pw->pw_name == NULL) {
            pw->pw_name = cf->servuser;
        }
    }
#endif
    
    PR_snprintf(cs_path, sizeof(cs_path), "%s%c"PRODUCT_NAME"-%s", sroot, FILE_PATHSEP, cf->servid);

    /* create all <a_server>/<subdirs> */
    if ( (t = ds_cre_subdirs(sroot, cf, cs_path, pw)) )
        return(t);

    /* Generate all scripts */
    if ( (t = ds_gen_scripts(sroot, cf, cs_path)) )
        return(t);

#if defined( XP_WIN32 )
    ds_dostounixpath( sroot );
    ds_dostounixpath( cs_path );
#endif

    /* Generate all conf files */
    if ( (t = ds_gen_confs(sroot, cf, cs_path)) )
        return(t);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cldif%cExample.ldif", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cldif%cExample.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cldif%cExample-roles.ldif", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cldif%cExample-roles.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cldif%cExample-views.ldif", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cldif%cExample-views.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cldif%cEuropean.ldif", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
    FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cldif%cEuropean.ldif", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    /* new code for dsml sample files */
    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cdsml%cExample.dsml", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cdsml%cExample.dsml", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cdsml%cExample-roles.dsml", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cdsml%cExample-roles.dsml", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    PR_snprintf(src, sizeof(src), "%s%cbin%c"PRODUCT_NAME"%cinstall%cdsml%cEuropean.dsml", sroot, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP, 
        FILE_PATHSEP);
    PR_snprintf(dest, sizeof(dest), "%s%cdsml%cEuropean.dsml", cs_path, FILE_PATHSEP, FILE_PATHSEP);
    create_instance_copy(src, dest, NEWFILE_MODE);
    chownfile (pw, dest);

    /*
      If the user has specified an LDIF file to use to initialize the database,
      load it now
    */
    if (cf->install_ldif_file && !access(cf->install_ldif_file, 0))
    {
    char msg[2*PATH_SIZE] = {0};
    int status = ds_ldif2db_backend_subtree(cf->install_ldif_file, NULL, cf->suffix);
    if (status)
        PR_snprintf(msg, sizeof(msg), "The file %s could not be loaded",
            cf->install_ldif_file);
    else
        PR_snprintf(msg, sizeof(msg), "The file %s was successfully loaded",
            cf->install_ldif_file);
    ds_show_message(msg);
    free(cf->install_ldif_file);
    cf->install_ldif_file = NULL;
    }

    /*
      All of the config files have been written, and the server should
      be ready to go.  Start the server if the user specified to start
      it or if we are configuring the server to serve as the repository
      for SuiteSpot (Mission Control) information
      Only attempt to start the server if the port is not in use
      */
    if(needToStartServer(cf) &&
       !(t = create_instance_checkport(cf->bindaddr, cf->servport)))
    {
    PR_snprintf(big_line, sizeof(big_line),"SERVER_NAMES=slapd-%s",cf->servid);
    putenv(big_line);

    isrunning = ds_get_updown_status();

    if (isrunning != DS_SERVER_UP)
    {
        int start_status = 0;
        int verbose = 1;
        char instance_dir[PATH_SIZE], errorlog[PATH_SIZE];

        if (getenv("USE_DEBUGGER"))
        verbose = 0;
        /* slapd-nickname directory */
        PR_snprintf(instance_dir, sizeof(instance_dir), "%s%c"PRODUCT_NAME"-%s", sroot, FILE_PATHSEP, 
            cf->servid);
        /* error log file */
        PR_snprintf(errorlog, sizeof(errorlog), "%s%clogs%cerrors", instance_dir, FILE_PATHSEP,
            FILE_PATHSEP);
        start_status = ds_bring_up_server_install(verbose, instance_dir, errorlog);

        if (start_status != DS_SERVER_UP)
        {
        /*
          If we were going to configure the server for SuiteSpot (Mission
          Control), the server must be running.  Therefore, it is a very
          bad thing, and we want to exit with a non zero exit code so the
          caller will know something went wrong.
          Otherwise, if the user just wanted to start the server for some
          reason, just exit with a zero and the messages printed will
          let the user know the server wasn't started.
          */
        char *msg;
        if (start_status == DS_SERVER_PORT_IN_USE)
            msg = "The server could not be started because the port is in use.";
        else if (start_status == DS_SERVER_MAX_SEMAPHORES)
            msg = "No more servers may be installed on this system.\nPlease refer to documentation for information about how to\nincrease the number of installed servers per system.";
        else if (start_status == DS_SERVER_CORRUPTED_DB)
            msg = "The server could not be started because the database is corrupted.";
        else if (start_status == DS_SERVER_NO_RESOURCES)
            msg = "The server could not be started because the operating system is out of resources (e.g. CPU memory).";
        else if (start_status == DS_SERVER_COULD_NOT_START)
            msg = "The server could not be started due to invalid command syntax or operating system resource limits.";
        else
            msg = "The server could not be started.";

        if( cf->cfg_sspt && !strcmp(cf->cfg_sspt, "1") )
        {
            ds_report_error(DS_SYSTEM_ERROR, "server", msg);
            return msg;
        }
        else
        {
            ds_show_message(msg);
            return 0;
        }
        }
        else
        {
        ds_show_message("Your new directory server has been started.");
        }
    }

    /* write ldap.conf */
    write_ldap_info( sroot, cf );

#ifdef XP_UNIX
    ds_become_localuser_name (cf->servuser);
#endif
#ifdef XP_WIN32
    if( errno = WSAStartup(0x0101, &wsadata ) != 0 )
    {
        char szTmp[512];
    /*replaced errno > -1 && errno < sys_nerr ? sys_errlist[errno] :
                    "unknown" with strerror(errno)*/
        PR_snprintf(szTmp, sizeof(szTmp), "Error: Windows Sockets initialization failed errno %d (%s)<br>\n", errno,
            strerror(errno), 0 );
            
        fprintf (stdout, szTmp);
        return 0;
    }
#endif /* XP_WIN32 */

    memset( &query_vars, 0, sizeof(query_vars) );
    if (!cf->use_existing_user_ds)
        query_vars.suffix = myStrdup( cf->suffix );
    query_vars.ssAdmID = myStrdup( cf->cfg_sspt_uid );
    query_vars.ssAdmPW1 = myStrdup( cf->cfg_sspt_uidpw );
    query_vars.ssAdmPW2 = myStrdup( cf->cfg_sspt_uidpw );
    query_vars.rootDN = myStrdup( cf->rootdn ); 
    query_vars.rootPW = myStrdup( cf->rootpw );
    query_vars.admin_domain =
        myStrdup( cf->admin_domain );
    query_vars.netscaperoot = myStrdup( cf->netscaperoot );
    query_vars.testconfig = myStrdup( cf->testconfig );
    query_vars.consumerDN = myStrdup(cf->consumerdn);
    query_vars.consumerPW = myStrdup(cf->consumerhashedpw);
    if (cf->cfg_sspt && !strcmp(cf->cfg_sspt, "1"))
        query_vars.cfg_sspt = 1;
    else
        query_vars.cfg_sspt = 0;

    if (cf->suitespot3x_uid)
        query_vars.config_admin_uid = myStrdup(cf->suitespot3x_uid);
    else
        query_vars.config_admin_uid = myStrdup(cf->cfg_sspt_uid);

    memset(&slapd_conf, 0, sizeof(SLAPD_CONFIG));
    if (sroot)
        PL_strncpyz(slapd_conf.slapd_server_root, sroot, sizeof(slapd_conf.slapd_server_root));
    if (cf->servport)
        slapd_conf.port = atoi(cf->servport);
    if (cf->servname)
        PL_strncpyz(slapd_conf.host, cf->servname, sizeof(slapd_conf.host));

    status = config_suitespot(&slapd_conf, &query_vars);
    if (status == -1) /* invalid or null arguments or configuration */
        return "Invalid arguments for server configuration.";
    }
    else if (t) /* just notify the user about the port conflict */
    {
    ds_show_message(t);
    }

#if defined (BUILD_PRESENCE)
    /* Create script for initializing IM Presence images */
    if ((NULL == t) && (0 == status))
    {
        if ( (t = gen_presence_init_script(sroot, cf, cs_path)) )
            return(t);
        /* Initialize IM Presence images */
        status = init_presence(sroot, cf, cs_path);
        if (status)
            return make_error ("ds_exec_and_report() failed (%d).", status);
    }
#endif

    if (status)
    return make_error ("Could not configure server (%d).", status);

    return(NULL);
}

/* write_ldap_info() : writes ldap.conf */

static int
write_ldap_info( char *slapd_server_root, server_config_s *cf)
{
    FILE* fp;
    int ret = 0;

    char* fmt = "%s/shared/config/ldap.conf";
    char* infoFileName;

    if (!slapd_server_root) {
      return -1;
    }
    
    infoFileName = PR_smprintf(fmt, slapd_server_root);

    if ((fp = fopen(infoFileName, "w")) == NULL)
    {
        ret = -1;
    }
    else
    {
        fprintf(fp, "url\tldap://%s:%d/",
                cf->servname, atoi(cf->servport));
    
        if (cf->suffix)
            fprintf(fp, "%s", cf->suffix);

        fprintf(fp, "\n");
        
        if (cf->cfg_sspt_uid) {
            fprintf(fp, "admnm\t%s\n", cf->cfg_sspt_uid);
        }

        fclose(fp);
    }
    PR_smprintf_free(infoFileName);

    return ret;
}

/* ----------- Create a new server from configuration variables ----------- */


int create_config(server_config_s *cf)
{
    char *t = NULL;
	char error_param[BIG_LINE] = {0};

    t = create_server(cf, error_param);
    if(t)
	{
		char *msg;
		if (error_param[0])
		{
			msg = PR_smprintf("%s.error:could not create server %s - %s",
							  error_param, cf->servid, t);
		}
		else
		{
			msg = PR_smprintf("error:could not create server %s - %s",
							  cf->servid, t);
		}
		ds_show_message(msg);
		PR_smprintf_free(msg);
	}
	else
	{
		ds_show_message("Created new Directory Server");
		return 0;
	}

    return 1;
}


/* ------ check passwords are same and satisfy minimum length policy------- */
static int check_passwords(char *pw1, char *pw2)
{
    if (strcmp (pw1, pw2) != 0) {
	    ds_report_error (DS_INCORRECT_USAGE, " different passwords",
			  "Enter the password again."
			  "  The two passwords you entered are different.");
		return 1;
	}
	
    if ( ((int) strlen(pw1)) < 8 ) {
	    ds_report_error (DS_INCORRECT_USAGE, " password too short",
			  "The password must be at least 8 characters long.");
		return 1;
	}

	return 0;
}

/* ------ Parse the results of a form and create a server from them ------- */


int parse_form(server_config_s *cf)
{
    char *rm = getenv("REQUEST_METHOD");
    char *qs = getenv("QUERY_STRING");
    char* cfg_sspt_uid_pw1;
    char* cfg_sspt_uid_pw2;
    LDAPURLDesc *desc = 0;
    char *temp = 0;

    if (!(cf->sroot = getenv("NETSITE_ROOT"))) {
		ds_report_error (DS_INCORRECT_USAGE, " NETSITE_ROOT environment variable not set.",
						 "The environment variable NETSITE_ROOT must be set to the server root directory.");
		return 1;
	}

    if (rm && qs && !strcmp(rm, "GET"))
	{
		ds_get_begin(qs);
	}
    else if (ds_post_begin(stdin))
	{
		return 1;
	}

    if (rm)
	{
		printf("Content-type: text/plain\n\n");
	}
    /* else we are being called from server installation; no output */

    if (!(cf->servname = ds_a_get_cgi_var("servname", "Server Name",
										  "Please give a hostname for your server.")))
	{
		return 1;
	}

	cf->bindaddr = ds_a_get_cgi_var("bindaddr", NULL, NULL);
    if (!(cf->servport = ds_a_get_cgi_var("servport", "Server Port",
										  "Please specify the TCP port number for this server.")))
	{
		return 1;
	}

    cf->suitespot3x_uid = ds_a_get_cgi_var("suitespot3x_uid", NULL, NULL);
    cf->cfg_sspt = ds_a_get_cgi_var("cfg_sspt", NULL, NULL);
    cf->cfg_sspt_uid = ds_a_get_cgi_var("cfg_sspt_uid", NULL, NULL);
    if (cf->cfg_sspt_uid && *(cf->cfg_sspt_uid) &&
		!(cf->cfg_sspt_uidpw = ds_a_get_cgi_var("cfg_sspt_uid_pw", NULL, NULL)))
	{

		if (!(cfg_sspt_uid_pw1 = ds_a_get_cgi_var("cfg_sspt_uid_pw1", "Password",
												  "Enter the password for the Mission Control Administrator's account.")))
		{
			return 1;
		}

		if (!(cfg_sspt_uid_pw2 = ds_a_get_cgi_var("cfg_sspt_uid_pw2", "Password",
												  "Enter the password for the Mission Control Administrator account, "
												  "twice.")))
		{
			return 1;
		}

		if (strcmp (cfg_sspt_uid_pw1, cfg_sspt_uid_pw2) != 0)
		{
			ds_report_error (DS_INCORRECT_USAGE, " different passwords",
							 "Enter the Mission Control Administrator account password again."
							 "  The two Mission Control Administrator account passwords "
							 "you entered are different.");
			return 1;
		}
		if ( ((int) strlen(cfg_sspt_uid_pw1)) < 1 ) {
			ds_report_error (DS_INCORRECT_USAGE, " password too short",
							 "The password must be at least 1 character long.");
			return 1;
		}
		cf->cfg_sspt_uidpw = cfg_sspt_uid_pw1;
	}

    if (cf->cfg_sspt && *cf->cfg_sspt && !strcmp(cf->cfg_sspt, "1") &&
		!cf->cfg_sspt_uid)
	{
		ds_report_error (DS_INCORRECT_USAGE,
						 " Userid not specified",
						 "A Userid for Mission Control Administrator must be specified.");
		return 1;
	}
    cf->start_server = ds_a_get_cgi_var("start_server", NULL, NULL);
    cf->secserv = ds_a_get_cgi_var("secserv", NULL, NULL);
    if (cf->secserv && strcmp(cf->secserv, "off"))
		cf->secservport = ds_a_get_cgi_var("secservport", NULL, NULL);
    if (!(cf->servid = ds_a_get_cgi_var("servid", "Server Identifier",
										"Please give your server a short identifier.")))
	{
		return 1;
	}

#ifdef XP_UNIX
    cf->servuser = ds_a_get_cgi_var("servuser", NULL, NULL);
#endif

    cf->suffix = dn_normalize_convert(ds_a_get_cgi_var("suffix", NULL, NULL));
    
    if (cf->suffix == NULL) {
		cf->suffix = "";
    }

    cf->rootdn = dn_normalize_convert(ds_a_get_cgi_var("rootdn", NULL, NULL));
    if (cf->rootdn && *(cf->rootdn)) {
		if (!(cf->rootpw = ds_a_get_cgi_var("rootpw", NULL, NULL)))
		{
			char* pw1 = ds_a_get_cgi_var("rootpw1", "Password",
										 "Enter the password for the unrestricted user.");
			char* pw2 = ds_a_get_cgi_var("rootpw2", "Password",
										 "Enter the password for the unrestricted user, twice.");

			if (!pw1 || !pw2 || check_passwords(pw1, pw2))
			{
				return 1;
			}

			cf->rootpw = pw1;
		}
		/* Encode the password in SSHA by default */
		cf->roothashedpw = (char *)ds_salted_sha1_pw_enc (cf->rootpw);
    }

    cf->admin_domain = ds_a_get_cgi_var("admin_domain", NULL, NULL);

	if ((temp = ds_a_get_cgi_var("use_existing_config_ds", NULL, NULL))) {
		cf->use_existing_config_ds = atoi(temp);
	} else {
		cf->use_existing_config_ds = 1; /* there must already be one */
	}

	if ((temp = ds_a_get_cgi_var("use_existing_user_ds", NULL, NULL))) {
		cf->use_existing_config_ds = atoi(temp);
	} else {
		cf->use_existing_user_ds = 0; /* we are creating it */
	}

    temp = ds_a_get_cgi_var("ldap_url", NULL, NULL);
    if (temp && !ldap_url_parse(temp, &desc) && desc)
	{
		char *suffix;
                int isSSL;

		if (desc->lud_dn && *desc->lud_dn) { /* use given DN for netscaperoot suffix */
			cf->netscaperoot = strdup(desc->lud_dn);
			suffix = cf->netscaperoot;
		} else { /* use the default */
			suffix = dn_normalize_convert(strdup(cf->netscaperoot));
		}
		/* the config ds connection may require SSL */
		isSSL = !strncmp(temp, "ldaps:", strlen("ldaps:"));
		cf->config_ldap_url = PR_smprintf("ldap%s://%s:%d/%s",
										  (isSSL ? "s" : ""), desc->lud_host,
										  desc->lud_port, suffix);
		ldap_free_urldesc(desc);
	}

    /* if being called as a CGI, the user_ldap_url will be the directory
       we're creating */
    /* this is the directory we're creating, and we cannot create an ssl
       directory, so we don't have to worry about ldap vs ldaps here */
	if ((temp = ds_a_get_cgi_var("user_ldap_url", NULL, NULL))) {
		cf->user_ldap_url = strdup(temp);
	} else {
		cf->user_ldap_url = PR_smprintf("ldap://%s:%s/%s", cf->servname,
										cf->servport, cf->suffix);
	}

    cf->samplesuffix = NULL;

    cf->disable_schema_checking = ds_a_get_cgi_var("disable_schema_checking",
												   NULL, NULL);

    cf->adminport = ds_a_get_cgi_var("adminport", NULL, NULL);

    cf->install_ldif_file = ds_a_get_cgi_var("install_ldif_file", NULL, NULL);

    return 0;
}
