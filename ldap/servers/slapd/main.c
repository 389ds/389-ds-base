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

/* This was malloc.h - but it's moved to stdlib.h on most platforms, and FBSD is strict */
/* Make it stdlib.h, and revert to malloc.h with ifdefs if we have issues here. WB 2016 */
#include <stdlib.h>
#include <ldap.h>
#undef OFF
#undef LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <grp.h>
#include <pwd.h> /* getpwnam */
#if !defined(LINUX) && !defined(__FreeBSD__)
union semun
{
    int val;
    struct semid_ds *buf;
    ushort *array;
};
#endif
#include <unistd.h> /* dup2 */
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/param.h> /* MAXPATHLEN */
#if defined(__sun)
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#endif
#include "slap.h"
#include "slapi-plugin.h"
#include "prinit.h"
#include "snmp_collator.h"
#include "fe.h" /* client_auth_init() */
#include "protect_db.h"
#include "getopt_ext.h"
#include "fe.h"
#include <nss.h>

#include <slapi-private.h>

#ifdef LINUX
/* For mallopt. Should be removed soon. */
#include <malloc.h>
#endif

/* Forward Declarations */

struct main_config
{
    char *extraname;
    int slapd_exemode;
    int n_port;
    int i_port;
    int s_port;
    char *myname;
    char **ldif_file;
    int ldif_files;
    char *cmd_line_instance_name;
    char **cmd_line_instance_names;
    int skip_db_protect_check;
    char **db2ldif_include;
    char **db2ldif_exclude;
    int ldif2db_removedupvals;
    int ldif2db_noattrindexes;
    char **db2index_attrs;
    int ldif_printkey;
    char *archive_name;
    int db2ldif_dump_replica;
    int db2ldif_dump_uniqueid;
    int ldif_include_changelog;
    int ldif2db_generate_uniqueid;
    char *ldif2db_namespaceid;
    int importexport_encrypt;
    int upgradedb_flags;
    int upgradednformat_dryrun;
    int is_quiet;
    int backuptools_verbose;
    int dbverify_verbose;
    char *dbverify_dbdir;
};
/* dbverify options */

static void register_objects(void);
static void process_command_line(int argc, char **argv, struct main_config *mcfg);
static int slapd_exemode_ldif2db(struct main_config *mcfg);
static int slapd_exemode_db2ldif(int argc, char **argv, struct main_config *mcfg);
static int slapd_exemode_db2index(struct main_config *mcfg);
static int slapd_exemode_archive2db(struct main_config *mcfg);
static int slapd_exemode_db2archive(struct main_config *mcfg);
static int slapd_exemode_upgradedb(struct main_config *mcfg);
static int slapd_exemode_upgradednformat(struct main_config *mcfg);
static int slapd_exemode_dbverify(struct main_config *mcfg);
static int slapd_exemode_suffix2instance(struct main_config *mcfg);
static int slapd_debug_level_string2level(const char *s);
static void slapd_debug_level_log(int level);
static void slapd_debug_level_usage(void);

/*
   Four cases:
    - change ownership of all files in directory (strip_fn=PR_FALSE)
    - change ownership of all files in directory; but trailing fn needs to be stripped (strip_fn=PR_TRUE)
    - fn is relative to root directory (/access); we print error message and let user shoot his foot
    - fn is relative to current directory (access); we print error message and let user shoot his other foot

    The docs say any valid filename.
*/

static void
chown_dir_files(char *name, struct passwd *pw, PRBool strip_fn, PRBool both)
{
    PRDir *dir;
    PRDirEntry *entry;
    char file[MAXPATHLEN + 1];
    char *log = NULL, *ptr = NULL;
    int rc = 0;
    gid_t gid = -1;

    log = slapi_ch_strdup(name);
    if (strip_fn) {
        if ((ptr = strrchr(log, '/')) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "chown_dir_files", "Caution changing ownership of ./%s \n", name);
            if (slapd_chown_if_not_owner(log, pw->pw_uid, -1)) {
                slapi_log_err(SLAPI_LOG_ERR, "chown_dir_files", "file (%s) chown failed (%d) %s.\n",
                              log, errno, slapd_system_strerror(errno));
            }
            rc = 1;
        } else if (log == ptr) {
            slapi_log_err(SLAPI_LOG_ERR, "chown_dir_files",
                          "Caution changing ownership of / directory and its contents to %s\n", pw->pw_name);
            *(++ptr) = '\0';
        } else {
            *ptr = '\0';
        }
    }
    if ((!rc) && ((dir = PR_OpenDir(log)) != NULL)) {
        /* change the owner for each of the files in the dir */
        while ((entry = PR_ReadDir(dir, PR_SKIP_BOTH)) != NULL) {
            PR_snprintf(file, MAXPATHLEN + 1, "%s/%s", log, entry->name);
            if (both) {
                gid = pw->pw_gid;
            } else {
                gid = -1;
            }
            if (slapd_chown_if_not_owner(file, pw->pw_uid, gid)) {
                slapi_log_err(SLAPI_LOG_ERR, "chown_dir_files", "file (%s) chown failed (%d) %s.\n",
                              file, errno, slapd_system_strerror(errno));
            }
        }
        PR_CloseDir(dir);
    }
    slapi_ch_free_string(&log);
}

/* Changes the owner of the files in the logs and
 * config directory to the user that the server runs as.
*/

static void
fix_ownership(void)
{
    struct passwd *pw = NULL;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (slapdFrontendConfig->localuser == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "fix_ownership",
                      "Local user missing from frontend configuration\n");
        return;
    }

    /* Provided the dse.ldif was read, this should never happen .... */
    if (slapdFrontendConfig->localuserinfo == NULL) {
        pw = getpwnam(slapdFrontendConfig->localuser);
        if (NULL == pw) {
            slapi_log_err(SLAPI_LOG_ERR, "fix_ownership",
                          "Unable to find user %s in system account database, "
                          "errno %d (%s)\n",
                          slapdFrontendConfig->localuser, errno, strerror(errno));
            return;
        }
        slapdFrontendConfig->localuserinfo =
            (struct passwd *)slapi_ch_malloc(sizeof(struct passwd));
        memcpy(slapdFrontendConfig->localuserinfo, pw, sizeof(struct passwd));
    }

    pw = slapdFrontendConfig->localuserinfo;

    /* config directory needs to be owned by the local user */
    if (slapdFrontendConfig->configdir) {
        chown_dir_files(slapdFrontendConfig->configdir, pw, PR_FALSE, PR_FALSE);
    }
    /* do access log file, if any */
    if (slapdFrontendConfig->accesslog) {
        chown_dir_files(slapdFrontendConfig->accesslog, pw, PR_TRUE, PR_TRUE);
    }
    /* do audit log file, if any */
    if (slapdFrontendConfig->auditlog) {
        chown_dir_files(slapdFrontendConfig->auditlog, pw, PR_TRUE, PR_TRUE);
    }
    /* do error log file, if any */
    if (slapdFrontendConfig->errorlog) {
        chown_dir_files(slapdFrontendConfig->errorlog, pw, PR_TRUE, PR_TRUE);
    }
}

/* Changes identity to the named user
 * If username == NULL, does nothing.
 * Does nothing on NT regardless.
 */
static int
main_setuid(char *username)
{
    if (username != NULL) {
        struct passwd *pw;
        /* Make sure everything in the log and config directory
         * is owned by the correct user */
        fix_ownership();
        pw = getpwnam(username);
        if (pw == NULL) {
            int oserr = errno;

            slapi_log_err(SLAPI_LOG_ERR, "main_setuid", "getpwnam(%s) == NULL, error %d (%s)\n",
                          username, oserr, slapd_system_strerror(oserr));
        } else {
            /*
             * According to https://github.com/389ds/389-ds-base/issues/3034 and
             * POS36-C. Observe correct revocation order while relinquishing privileges
             * setgroups must be called to ensure our supplemental group list is
             * correctly limited. Because we only care about our single group
             * dirsrv for the target user, we set that.
             *
             * In the future this may change however.
             */
            if ((getuid()==0) && setgroups(0, NULL) != 0) {
                int oserr = errno;

                slapi_log_err(SLAPI_LOG_ERR, "main_setuid", "setgroups(0, NULL) != 0, error %d (%s)\n",
                              oserr, slapd_system_strerror(oserr));
                return -1;
            }
            if (setgid(pw->pw_gid) != 0) {
                int oserr = errno;

                slapi_log_err(SLAPI_LOG_ERR, "main_setuid", "setgid(%li) != 0, error %d (%s)\n",
                              (long)pw->pw_gid, oserr, slapd_system_strerror(oserr));
                return -1;
            }
            if (setuid(pw->pw_uid) != 0) {
                int oserr = errno;

                slapi_log_err(SLAPI_LOG_ERR, "main_setuid", "setuid(%li) != 0, error %d (%s)\n",
                              (long)pw->pw_uid, oserr, slapd_system_strerror(oserr));
                return -1;
            }
        }
    }
    return 0;
}

/* set good defaults for front-end config in referral mode */
static void
referral_set_defaults(void)
{
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    config_set_maxdescriptors(CONFIG_MAXDESCRIPTORS_ATTRIBUTE, "1024", errorbuf, 1);
}

static int
name2exemode(char *progname, char *s, int exit_if_unknown)
{
    int exemode;

    if (strcmp(s, "db2ldif") == 0) {
        exemode = SLAPD_EXEMODE_DB2LDIF;
    } else if (strcmp(s, "ldif2db") == 0) {
        exemode = SLAPD_EXEMODE_LDIF2DB;
    } else if (strcmp(s, "archive2db") == 0) {
        exemode = SLAPD_EXEMODE_ARCHIVE2DB;
    } else if (strcmp(s, "db2archive") == 0) {
        exemode = SLAPD_EXEMODE_DB2ARCHIVE;
    } else if (strcmp(s, "server") == 0) {
        exemode = SLAPD_EXEMODE_SLAPD;
    } else if (strcmp(s, "db2index") == 0) {
        exemode = SLAPD_EXEMODE_DB2INDEX;
    } else if (strcmp(s, "refer") == 0) {
        exemode = SLAPD_EXEMODE_REFERRAL;
    } else if (strcmp(s, "suffix2instance") == 0) {
        exemode = SLAPD_EXEMODE_SUFFIX2INSTANCE;
    } else if (strcmp(s, "upgradedb") == 0) {
        exemode = SLAPD_EXEMODE_UPGRADEDB;
    } else if (strcmp(s, "upgradednformat") == 0) {
        exemode = SLAPD_EXEMODE_UPGRADEDNFORMAT;
    } else if (strcmp(s, "dbverify") == 0) {
        exemode = SLAPD_EXEMODE_DBVERIFY;
    } else if (exit_if_unknown) {
        fprintf(stderr, "usage: %s -D configdir "
                        "[ldif2db | db2ldif | archive2db "
                        "| db2archive | db2index | refer | suffix2instance "
                        "| upgradedb | upgradednformat | dbverify] "
                        "[options]\n",
                progname);
        exit(1);
    } else {
        exemode = SLAPD_EXEMODE_UNKNOWN;
    }

    return (exemode);
}


static void
usage(char *name, char *extraname, int slapd_exemode)
{
    char *usagestr = NULL;
    char *extraspace;

    if (extraname == NULL) {
        extraspace = extraname = "";
    } else {
        extraspace = " ";
    }

    switch (slapd_exemode) {
    case SLAPD_EXEMODE_DB2LDIF:
        usagestr = "usage: %s %s%s-D configdir [-n backend-instance-name] [-d debuglevel] "
                   "[-N] [-a outputfile] [-r] [-C] [{-s includesuffix}*] "
                   "[{-x excludesuffix}*] [-u] [-U] [-m] [-M] [-E] [-q]\n"
                   "Note: either \"-n backend_instance_name\" or \"-s includesuffix\" is required.\n";
        break;
    case SLAPD_EXEMODE_LDIF2DB:
        usagestr = "usage: %s %s%s-D configdir [-d debuglevel] "
                   "[-n backend_instance_name] [-O] [-g uniqueid_type] [--namespaceid uniqueID]"
                   "[{-s includesuffix}*] [{-x excludesuffix}*]  [-E] [-q] {-i ldif-file}*\n"
                   "Note: either \"-n backend_instance_name\" or \"-s includesuffix\" is required.\n";
        break;
    case SLAPD_EXEMODE_DB2ARCHIVE:
        usagestr = "usage: %s %s%s-D configdir [-q] [-d debuglevel] -a archivedir\n";
        break;
    case SLAPD_EXEMODE_ARCHIVE2DB:
        usagestr = "usage: %s %s%s-D configdir [-q] [-d debuglevel] -a archivedir\n";
        break;
    case SLAPD_EXEMODE_DB2INDEX:
        usagestr = "usage: %s %s%s-D configdir -n backend-instance-name "
                   "[-d debuglevel] {-t attributetype}* {-T VLV Search Name}*\n";
        /* JCM should say 'Address Book' or something instead of VLV */
        break;
    case SLAPD_EXEMODE_REFERRAL:
        usagestr = "usage: %s %s%s-D configdir -r referral-url [-p port]\n";
        break;
    case SLAPD_EXEMODE_SUFFIX2INSTANCE:
        usagestr = "usage: %s %s%s -D configdir {-s suffix}*\n";
        break;
    case SLAPD_EXEMODE_UPGRADEDB:
        usagestr = "usage: %s %s%s-D configdir [-d debuglevel] [-f] [-r] -a archivedir\n";
        break;
    case SLAPD_EXEMODE_UPGRADEDNFORMAT:
        usagestr = "usage: %s %s%s-D configdir [-d debuglevel] [-N] -n backend-instance-name -a fullpath-backend-instance-dir-full\n";
        break;
    case SLAPD_EXEMODE_DBVERIFY:
        usagestr = "usage: %s %s%s-D configdir [-d debuglevel] [-n backend-instance-name] [-a db-directory]\n";
        break;

    default: /* SLAPD_EXEMODE_SLAPD */
        usagestr = "usage: %s %s%s-D configdir [-d debuglevel] "
                   "[-i pidlogfile] [-v] [-V]\n";
    }

    fprintf(stderr, usagestr, name, extraname, extraspace);
}


/* taken from idsktune */
#if defined(__sun)
static void
ids_get_platform_solaris(char *buf)
{
    struct utsname u;
    char sbuf[128];
    FILE *fp;

#if defined(sparc) || defined(__sparc)
    int is_u = 0;

    sbuf[0] = '\0';
    sysinfo(SI_MACHINE, sbuf, 128);

    if (strcmp(sbuf, "sun4u") == 0) {
        is_u = 1;
    }

    sbuf[0] = '\0';
    sysinfo(SI_PLATFORM, sbuf, 128);

    PR_snprintf(buf, sizeof(buf), "%ssparc%s-%s-solaris",
                is_u ? "u" : "",
                sizeof(long) == 4 ? "" : "v9",
                sbuf);
#else
#if defined(i386) || defined(__i386)
    sprintf(buf, "i386-unknown-solaris");
#else
    sprintf(buf, "unknown-unknown-solaris");
#endif /* not i386 */
#endif /* not sparc */

    uname(&u);
    if (isascii(u.release[0]) && isdigit(u.release[0]))
        strcat(buf, u.release);

    fp = fopen("/etc/release", "r");

    if (fp != NULL) {
        char *rp;

        sbuf[0] = '\0';
        fgets(sbuf, 128, fp);
        fclose(fp);
        rp = strstr(sbuf, "Solaris");
        if (rp) {
            rp += 8;
            while (*rp != 's' && *rp != '\0')
                rp++;
            if (*rp == 's') {
                char *rp2;
                rp2 = strchr(rp, ' ');
                if (rp2)
                    *rp2 = '\0';
                strcat(buf, "_");
                strcat(buf, rp);
            }
        }
    }
}
#endif

static void
slapd_print_version(int verbose)
{
#if defined(__sun)
    char buf[8192];
#endif
    char *versionstring = config_get_versionstring();
    char *buildnum = config_get_buildnum();

    printf(SLAPD_VENDOR_NAME "\n%s B%s\n", versionstring, buildnum);

    if (strcmp(buildnum, BUILD_NUM) != 0) {
        printf("ns-slapd: B%s\n", BUILD_NUM);
    }

    slapi_ch_free((void **)&versionstring);
    slapi_ch_free((void **)&buildnum);

    if (verbose == 0)
        return;

#if defined(__sun)
    ids_get_platform_solaris(buf);
    printf("System: %s\n", buf);
#endif

    /* this won't print much with the -v flag as the dse.ldif file
     * hasn't be read yet.
     */
    plugin_print_versions();
}

/* On UNIX, we create a file with our PID in it */
static int
write_start_pid_file(void)
{
    FILE *fp = NULL;
    /*
     * The following section of code is closely coupled with the
     * admin programs. Please do not make changes here without
     * consulting the start/stop code for the admin code.
     */
    if (start_pid_file == NULL) {
        return 0;
    } else if ((fp = fopen(start_pid_file, "w")) != NULL) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
        if (chmod(start_pid_file, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0) {
            int rerrno = errno;
            slapi_log_err(SLAPI_LOG_ERR, "write_start_pid_file", "file (%s) chmod failed (%d)\n", start_pid_file, rerrno);
            unlink(start_pid_file);
            return -2;
        } else {
            return 0;
        }
    }
    int rerrno = errno;
    slapi_log_err(SLAPI_LOG_ERR, "write_start_pid_file", "file (%s) fopen failed (%d)\n", start_pid_file, rerrno);
    return -1;
}

int
main(int argc, char **argv)
{
    int return_value = 0;
    struct main_config mcfg = {0};

    /* Set a number of defaults */
    mcfg.slapd_exemode = SLAPD_EXEMODE_UNKNOWN;
    mcfg.ldif_printkey = EXPORT_PRINTKEY | EXPORT_APPENDMODE;
    mcfg.ldif_include_changelog = 0;
    mcfg.db2ldif_dump_uniqueid = 1;
    mcfg.ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
    mcfg.ldif2db_removedupvals = 1;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    daemon_ports_t ports_info = {0};

#ifdef LINUX
#if defined(__GLIBC__)
    char *m = getenv("SLAPD_MXFAST");
    if (m) {
        int val = atoi(m);
        int max = 80 * (sizeof(size_t) / 4);

        if (val >= 0 && val <= max) {
            mallopt(M_MXFAST, val);
        }
    }
#endif
#endif

    /* Set third party libs function before we init config so we see the logs earlier */
    log_external_libs_debug_set_log_fn();

    /*
     * Initialize NSPR very early. NSPR supports implicit initialization,
     * but it is not bulletproof -- so it is better to be explicit.
     */
    PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);
    FrontendConfig_init();

    /* Pause for the debugger if DEBUG_SLEEP is set in the environment */
    {
        char *s = getenv("DEBUG_SLEEP");
        if ((s != NULL) && isdigit(*s)) {
            char *endp = NULL;
            int64_t secs;
            errno = 0;

            secs = strtol(s, &endp, 10);
            if ( endp == s ||
                 *endp != '\0' ||
                 ((secs == LONG_MIN || secs == LONG_MAX) && errno == ERANGE) ||
                 secs < 1 )
            {
                /* Invalid value, default to 30 seconds */
                secs = 30;
            } else if (secs > 3600) {
                secs = 3600;
            }
            printf("slapd pid is %d - sleeping for %" PRId64 "\n", getpid(), secs);
            sleep(secs);
        }
    }

    /* used to set configfile to the default config file name here */
    if ((mcfg.myname = strrchr(argv[0], '/')) == NULL) {
        mcfg.myname = slapi_ch_strdup(argv[0]);
    } else {
        mcfg.myname = slapi_ch_strdup(mcfg.myname + 1);
    }

    process_command_line(argc, argv, &mcfg);

#ifdef WITH_SYSTEMD
    /* Note: Must come *after* processing CLI, to override the should_detach flag */
    /*
     * HUGE WARNING: Systemd has some undocumented magic. with Type=notify, this
     * acts as type=simple, but waits for ns-slapd to tell systemd it's good to go.
     * If ns-slapd daemonises, systemd will KILL IT because simple==no forking.
     *
     * So instead, we need to work out if we have the NOTIFY_SOCKET env variable
     * and if we do, we need to prevent forking so systemd doesn't nail us to
     * the wall.
     *
     * Of course, systemd makes NO GUARANTEE that it will be called notify_socket
     * in the next version, nor that it won't give the variable to a service type
     * which isn't of the type notify ..... This could all go very wrong :)
     */
    char *notify = getenv("NOTIFY_SOCKET");
    if (notify) {
        should_detach = 0;
    }
#endif

    if (NULL == slapdFrontendConfig->configdir) {
        usage(mcfg.myname, mcfg.extraname, mcfg.slapd_exemode);
        exit(1);
    }

    /* display debugging level if it is anything other than the default */
    if (0 != (slapd_ldap_debug & ~LDAP_DEBUG_ANY)) {
        slapd_debug_level_log(slapd_ldap_debug);
    }

    slapd_init();
    g_log_init();
    vattr_init();

    /*
     * init the thread data indexes. Nothing should be creating their
     * own thread data, and should be using this function instead
     * as we may swap to context based storage in the future rather
     * than direct thread-local accesses (especially important with
     * consideration of rust etc)
     */
    slapi_td_init();
    
    /* Init the global counters */
    alloc_global_snmp_vars();

    if (mcfg.slapd_exemode == SLAPD_EXEMODE_REFERRAL) {
        slapdFrontendConfig = getFrontendConfig();
        /* make up the config stuff */
        referral_set_defaults();
        /*
         * Process the config files.
         */
        if (0 == slapd_bootstrap_config(slapdFrontendConfig->configdir)) {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
                          slapdFrontendConfig->configdir);
            exit(1);
        }

        mcfg.n_port = config_get_port();
        mcfg.s_port = config_get_secureport();
        register_objects();

    } else {
        slapdFrontendConfig = getFrontendConfig();
        /* The 2 calls below have been moved to this place to make sure that
         * they are called before setup_internal_backends to avoid bug 524439 */
        /*
         * The 2 calls below where being sometimes called AFTER
         * ldapi_register_extended_op (such fact was being stated and
         * reproducible for some optimized installations at startup (bug
         * 524439)... Such bad call was happening in the context of
         * setup_internal_backends -> dse_read_file -> load_plugin_entry ->
         * plugin_setup -> replication_multisupplier_plugin_init ->
         * slapi_register_plugin -> plugin_setup ->
         * multisupplier_start_extop_init -> * slapi_pblock_set ->
         * ldapi_register_extended_op... Unfortunately, the server
         * design is such that it is assumed that ldapi_init_extended_ops is
         * always called first.
         * THE FIX: Move the two calls below before a call to
         * setup_internal_backends (down in this same function)
         */
        ldapi_init_extended_ops();


        /*
         * Initialize the default backend.  This should be done before we
         * process the config. files
         */
        defbackend_init();

        /*
         * Register the extensible objects with the factory.
         */
        register_objects();
        /*
         * Register the controls that we support.
         */
        init_controls();

        /*
         * Register the server features that we support.
         */
        init_features();

        /*
         * Initialize the global plugin list lock
         */
        global_plugin_init();

        /*
         * Process the config files.
         */
        if (0 == slapd_bootstrap_config(slapdFrontendConfig->configdir)) {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
                          slapdFrontendConfig->configdir);
            exit(1);
        }

        /* We need to init sasl after we load the bootstrap config since
         * the config may be setting the sasl plugin path.
         */
        init_saslmechanisms();

        /* -sduloutre: must be done before any internal search */
        /* do it before splitting off to other modes too -robey */
        /* -richm: must be done before reading config files */
        return_value = compute_init();
        if (return_value != 0) {
            slapi_log_err(SLAPI_LOG_EMERG, "main", "Initialization Failed 0 %d\n", return_value);
            exit(1);
        }
        entry_computed_attr_init();

        /* This will setup the mapping tree too */
        if (0 == setup_internal_backends(slapdFrontendConfig->configdir)) {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
                          slapdFrontendConfig->configdir);
            exit(1);
        }

        mcfg.n_port = config_get_port();
        mcfg.s_port = config_get_secureport();
    }

    raise_process_limits(); /* should be done ASAP once config file read */

    /* Set entry points in libslapd */
    set_entry_points();

    /*
     * After we read the config file we should make
     * sure that everything we needed to read in has
     * been read in and we'll start whatever threads,
     * etc the backends need to start
     */

    /* Important: up 'till here we could be running as root (on unix).
     * we believe that we've not created any files before here, otherwise
     * they'd be owned by root, which is bad. We're about to change identity
     * to some non-root user, but before we do, we call the daemon code
     * to let it open the listen sockets. If these sockets are low-numbered,
     * we need to be root in order to open them.
     */

    if ((mcfg.slapd_exemode == SLAPD_EXEMODE_SLAPD) ||
        (mcfg.slapd_exemode == SLAPD_EXEMODE_REFERRAL)) {
        char *listenhost = config_get_listenhost();
        char *securelistenhost = config_get_securelistenhost();
        ports_info.n_port = (unsigned short)mcfg.n_port;
        if (slapd_listenhost2addr(listenhost,
                                  &ports_info.n_listenaddr) != 0 ||
            ports_info.n_listenaddr == NULL) {
            slapi_ch_free_string(&listenhost);
            slapi_ch_free_string(&securelistenhost);
            return (1);
        }
        slapi_ch_free_string(&listenhost);

        ports_info.s_port = (unsigned short)mcfg.s_port;
        if (slapd_listenhost2addr(securelistenhost,
                                  &ports_info.s_listenaddr) != 0 ||
            ports_info.s_listenaddr == NULL) {
            slapi_ch_free_string(&securelistenhost);
            return (1);
        }
        slapi_ch_free_string(&securelistenhost);

#if defined(ENABLE_LDAPI)
        if (config_get_ldapi_switch() && slapdFrontendConfig->ldapi_filename != 0) {
            mcfg.i_port = ports_info.i_port = 1; /* flag ldapi as on */
            ports_info.i_listenaddr = (PRNetAddr **)slapi_ch_calloc(2, sizeof(PRNetAddr *));
            *ports_info.i_listenaddr = (PRNetAddr *)slapi_ch_calloc(1, sizeof(PRNetAddr));
            (*ports_info.i_listenaddr)->local.family = PR_AF_LOCAL;
            PL_strncpyz((*ports_info.i_listenaddr)->local.path,
                        slapdFrontendConfig->ldapi_filename,
                        sizeof((*ports_info.i_listenaddr)->local.path));
            unlink((*ports_info.i_listenaddr)->local.path);
        }
#endif /* ENABLE_LDAPI */

        return_value = daemon_pre_setuid_init(&ports_info);
        if (0 != return_value) {
            slapi_log_err(SLAPI_LOG_ERR, "main", "Failed to init daemon\n");
            exit(1);
        }
    }

    /* Now, sockets are open, so we can safely change identity now */
    return_value = main_setuid(slapdFrontendConfig->localuser);
    if (0 != return_value) {
        slapi_log_err(SLAPI_LOG_ERR, "main", "Failed to change user and group identity to that of %s\n",
                      slapdFrontendConfig->localuser);
        exit(1);
    }

    /*
     * Detach ourselves from the terminal (unless running in debug mode).
     * We must detach before we start any threads since detach forks() on
     * UNIX.
     * Have to detach after ssl_init - the user may be prompted for the PIN
     * on the terminal, so it must be open.
     */
    if (detach(mcfg.slapd_exemode, mcfg.importexport_encrypt, mcfg.s_port, &ports_info)) {
        return_value = 1;
        goto cleanup;
    }

    /*
     * if we were called upon to do special database stuff, do it and be
     * done.
     */
    switch (mcfg.slapd_exemode) {
    case SLAPD_EXEMODE_LDIF2DB:
        return_value = slapd_exemode_ldif2db(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_DB2LDIF:
        return_value = slapd_exemode_db2ldif(argc, argv, &mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_DB2INDEX:
        return_value = slapd_exemode_db2index(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_ARCHIVE2DB:
        return_value = slapd_exemode_archive2db(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_DB2ARCHIVE:
        return_value = slapd_exemode_db2archive(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_REFERRAL:
        /* check that all the necessary info was given, then go on */
        if (!config_check_referral_mode()) {
            slapi_log_err(SLAPI_LOG_ALERT, "main",
                          "ERROR: No referral URL supplied\n");
            usage(mcfg.myname, mcfg.extraname, mcfg.slapd_exemode);
            exit(1);
        }
        break;

    case SLAPD_EXEMODE_SUFFIX2INSTANCE:
        return_value = slapd_exemode_suffix2instance(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_UPGRADEDB:
        return_value = slapd_exemode_upgradedb(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_UPGRADEDNFORMAT:
        return_value = slapd_exemode_upgradednformat(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_DBVERIFY:
        return_value = slapd_exemode_dbverify(&mcfg);
        goto cleanup;
        break;

    case SLAPD_EXEMODE_PRINTVERSION:
        slapd_print_version(1);
        return_value = 1;
        goto cleanup;
        break;
    default: {
        char *rundir = config_get_rundir();

        /* Ensure that we can read from and write to our rundir */
        if (access(rundir, R_OK | W_OK)) {
            slapi_log_err(SLAPI_LOG_EMERG, "main", "Unable to access " CONFIG_RUNDIR_ATTRIBUTE ": %s\n",
                          slapd_system_strerror(errno));
            slapi_log_err(SLAPI_LOG_EMERG, "main", "Ensure that user \"%s\" has read and write "
                                                   "permissions on %s\n",
                          slapdFrontendConfig->localuser, rundir);
            slapi_log_err(SLAPI_LOG_EMERG, "main", "Shutting down.\n");
            slapi_ch_free_string(&rundir);
            return_value = 1;
            goto cleanup;
        }
        slapi_ch_free_string(&rundir);
        break;
    }
    }

    if (config_get_disk_monitoring()) {
        char **dirs = NULL;
        char *dirstr = NULL;
        uint64_t disk_space = 0;
        uint64_t threshold = 0;
        uint64_t halfway = 0;
        threshold = config_get_disk_threshold();
        halfway = threshold / 2;
        disk_mon_get_dirs(&dirs);
        dirstr = disk_mon_check_diskspace(dirs, threshold, &disk_space);
        if (dirstr != NULL && disk_space < halfway) {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "Disk Monitoring is enabled and disk space on (%s) is too far below the threshold(%" PRIu64 " bytes). Exiting now.\n",
                          dirstr, threshold);
            slapi_ch_array_free(dirs);
            /*
             * We should free the structs we allocated for sockets and addresses
             * as they would be freed at the slapd_daemon but it was not initiated
             * at that point of start-up.
             */
            slapd_sockets_ports_free(&ports_info);
            return_value = 1;
            goto cleanup;
        }
        slapi_ch_array_free(dirs);
        dirs = NULL;
    }

    /* initialize the normalized DN cache */
    if (ndn_cache_init() != 0) {
        slapi_log_err(SLAPI_LOG_EMERG, "main", "Unable to create ndn cache\n");
        return_value = 1;
        goto cleanup;
    }

    global_backend_lock_init();

    /*
    * Now write our PID to the startup PID file.
    * This is used by the start up script to determine our PID quickly
    * after we fork, without needing to wait for the 'real' pid file to be
    * written. That could take minutes. And the start script will wait
    * that long looking for it. With this new 'early pid' file, it can avoid
    * doing that, by detecting the pid and watching for the process exiting.
    * This removes the blank stares all round from start-slapd when the server
    * fails to start for some reason
    */
    if (write_start_pid_file() != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "main",
                      "Shutting down as we are unable to write to the pid file location\n");
        return_value = 1;
        goto cleanup;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if ((mcfg.slapd_exemode != SLAPD_EXEMODE_REFERRAL) &&
        (add_new_slapd_process(mcfg.slapd_exemode, mcfg.db2ldif_dump_replica,
                               mcfg.skip_db_protect_check) == -1)) {
        slapi_log_err(SLAPI_LOG_CRIT, "main",
                      "Shutting down due to possible conflicts with other slapd processes\n");
        return_value = 1;
        goto cleanup;
    }

    /*
     * Now it is safe to log our first startup message.  If we were to
     * log anything earlier than now it would appear on the admin startup
     * screen twice because before we detach everything is sent to both
     * stderr and our error log.  Yuck.
     */
    if (1) {
        char *versionstring = config_get_versionstring();
        char *buildnum = config_get_buildnum();
        slapi_log_err(SLAPI_LOG_INFO, "main", "%s B%s starting up\n",
                      versionstring, buildnum);
        slapi_ch_free((void **)&buildnum);
        slapi_ch_free((void **)&versionstring);
    }

    /* log the max fd limit as it is typically set in env/systemd */
    slapi_log_err(SLAPI_LOG_INFO, "main",
            "Setting the maximum file descriptor limit to: %" PRId64 "\n",
            config_get_maxdescriptors());

    if (mcfg.slapd_exemode != SLAPD_EXEMODE_REFERRAL) {
        int rc;
        Slapi_DN *sdn;

        fedse_create_startOK(DSE_FILENAME, DSE_STARTOKFILE,
                             slapdFrontendConfig->configdir);

        eq_init(); /* DEPRECATED */
        eq_init_rel(); /* must be done before plugins started */

        ps_init_psearch_system(); /* must come before plugin_startall() */


        /* initialize UniqueID generator - must be done once backends are started
           and event queue is initialized but before plugins are started */
        /* Note: This DN is no need to be normalized. */
        sdn = slapi_sdn_new_ndn_byval("cn=uniqueid generator,cn=config");
        rc = uniqueIDGenInit(NULL, sdn, mcfg.slapd_exemode == SLAPD_EXEMODE_SLAPD);
        slapi_sdn_free(&sdn);
        if (rc != UID_SUCCESS) {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "Fatal Error---Failed to initialize uniqueid generator; error = %d. "
                          "Exiting now.\n",
                          rc);
            return_value = 1;
            goto cleanup;
        }

        /* --ugaston: register the start-tls plugin */
        if (slapd_security_library_is_initialized() != 0) {
            start_tls_register_plugin();
            slapi_log_err(SLAPI_LOG_PLUGIN, "main", "Start TLS plugin registered.\n");
        }
        passwd_modify_register_plugin();
        slapi_log_err(SLAPI_LOG_PLUGIN, "main", "Password Modify plugin registered.\n");

        /* Cleanup old tasks that may still be in the DSE from a previous
           session.  Call before plugin_startall since cleanup needs to be
           done before plugin_startall where user defined task plugins could
           be started.
         */
        task_cleanup();

        /*
         * This step checks for any updates and changes on upgrade
         * specifically, it manages assumptions about what plugins should exist, and their
         * configurations, and potentially even the state of configurations on the server
         * and their removal and deprecation.
         *
         * Has to be after uuid + dse to change config, but before password and plugins
         * so we can adjust these configurations.
         */
        if (upgrade_server() != UPGRADE_SUCCESS) {
            return_value = 1;
            goto cleanup;
        }

        /*
         * Initialize password storage in entry extension.
         * Need to be initialized before plugin_startall in case stucked
         * changes are replicated as soon as the replication plugin is started.
         */
        pw_exp_init();
        op_stat_init();

        plugin_print_lists();
        plugin_startall(argc, argv, NULL /* specific plugin list */);
        compute_plugins_started();
        (void) rewriters_init();
        if (housekeeping_start((time_t)0, NULL) == NULL) {
            return_value = 1;
            goto cleanup;
        }

        eq_start(); /* must be done after plugins started - DEPRECATED */
        eq_start_rel(); /* must be done after plugins started */

        vattr_check(); /* Check if it exists virtual attribute definitions */

#ifdef HPUX10
        /* HPUX linker voodoo */
        if (collation_init == NULL) {
            return_value = 1;
            goto cleanup;
        }

#endif /* HPUX */

        normalize_oc();

        if (mcfg.n_port) {
        } else if (mcfg.i_port) {
        } else if (config_get_security()) {
        } else {
            slapi_log_err(SLAPI_LOG_EMERG, "main",
                          "Fatal Error---No ports specified. "
                          "Exiting now.\n");

            return_value = 1;
            goto cleanup;
        }
    }

    if (mcfg.slapd_exemode != SLAPD_EXEMODE_REFERRAL) {
        /* else do this after seteuid() */
        /* setup cn=tasks tree */
        task_init();

        /* pw_init() needs to be here since it uses aci function calls.  */
        pw_init();
        /* Initialize the sasl mapping code */
        if (sasl_map_init()) {
            slapi_log_err(SLAPI_LOG_CRIT, "main", "Failed to initialize sasl mapping code\n");
        }
    }

    /*
     * search_register_reslimits() and daemon_register_reslimits() can
     * be called any time before we start accepting client connections.
     * We call these even when running in referral mode because they
     * do little harm and registering at least one resource limit forces
     * the reslimit subsystem to initialize itself... which prevents
     * strange error messages from being logged to the error log for
     * the first LDAP connection.
     */
    if (search_register_reslimits() != SLAPI_RESLIMIT_STATUS_SUCCESS ||
        daemon_register_reslimits() != SLAPI_RESLIMIT_STATUS_SUCCESS) {
        return_value = 1;
        goto cleanup;
    }

    {
        starttime = slapi_current_utc_time();
        slapd_daemon(&ports_info);
    }
    slapi_log_err(SLAPI_LOG_INFO, "main", "slapd stopped.\n");
    reslimit_cleanup();
    vattr_cleanup();
    sasl_map_done();
cleanup:
    slapi_ch_free_string(&(mcfg.myname));
    compute_terminate();
    SSL_ShutdownServerSessionIDCache();
    SSL_ClearSessionCache();
    ndn_cache_destroy();
    NSS_Shutdown();
    dse_destroy_backup_lock();

    /*
     * Server has stopped, lets force everything to disk: logs
     * db, dse.ldif, all of it.
     */
    sync();
    return return_value;
}


#if defined(hpux)
void
signal2sigaction(int s, void *a)
{
    struct sigaction act = {0};

    act.sa_handler = (VFP)a;
    act.sa_flags = 0;
    (void)sigemptyset(&act.sa_mask);
    (void)sigaddset(&act.sa_mask, s);
    (void)sigaction(s, &act, NULL);
}
#endif /* hpux */

static void
register_objects(void)
{
    get_operation_object_type();
    daemon_register_connection();
    get_entry_object_type();
    mapping_tree_get_extension_type();
}

static void
process_command_line(int argc, char **argv, struct main_config *mcfg)
{
    int i;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    char *opts;
    struct opt_ext *long_opts;
    int longopt_index = 0;

    /*
     * Refer to the file getopt_ext.h for an overview of how to use the
     * long option names
     *
     */


    /*
     * when a new option letter is used, please move it from the "available"
     * list to the "used" list.
     *
     */
    /*
     * single-letter options already in use:
     *
     * a C c D E d f G g i
     * L l N m n O o P p r S s T t
     * u v V w x Z z
     *
     * 1
     *
     */

    /*
     * single-letter options still available:
     *
     * A B b e F H h I J j
     * K k M Q q R
     * W  X Y y
     *
     * 2 3 4 5 6 7 8 9 0
     *
     */

    char *opts_db2ldif = "vd:D:ENa:rs:x:CSut:n:UmMo1qRV";
    struct opt_ext long_options_db2ldif[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"dontPrintKey", ArgNone, 'n'},
        {"archive", ArgRequired, 'a'},
        {"replica", ArgNone, 'r'},
        {"include", ArgRequired, 's'},
        {"exclude", ArgRequired, 'x'},
        /*{"whatshouldwecallthis",ArgNone,'C'},*/
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"noUniqueIds", ArgNone, 'u'},
        {"configDir", ArgRequired, 'D'},
        {"encrypt", ArgOptional, 'E'},
        {"includechangelog", ArgNone, 'R'},
        {"nowrap", ArgNone, 'U'},
        {"minimalEncode", ArgNone, 'm'},
        {"oneOutputFile", ArgNone, 'o'},
        {"multipleOutputFile", ArgNone, 'M'},
        {"noVersionNum", ArgNone, '1'},
        {"quiet", ArgNone, 'q'},
        {"verbose", ArgNone, 'V'},
        {0, 0, 0}};

    char *opts_ldif2db = "vd:i:g:G:n:s:x:NOCc:St:D:EqV";
    struct opt_ext long_options_ldif2db[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"ldiffile", ArgRequired, 'i'},
        {"generateUniqueId", ArgOptional, 'g'},
        {"backend", ArgRequired, 'n'},
        {"include", ArgRequired, 's'},
        {"exclude", ArgRequired, 'x'},
        {"noindex", ArgNone, 'O'},
        /*{"whatshouldwecallthis",ArgNone,'C'},*/
        /*{"whatshouldwecallthis",ArgRequired,'c'},*/
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"namespaceid", ArgRequired, 'G'},
        {"nostate", ArgNone, 'Z'},
        {"configDir", ArgRequired, 'D'},
        {"encrypt", ArgOptional, 'E'},
        {"quiet", ArgNone, 'q'},
        {"verbose", ArgNone, 'V'},
        {0, 0, 0}};

    char *opts_archive2db = "vd:i:a:SD:qV";
    struct opt_ext long_options_archive2db[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"pidfile", ArgRequired, 'i'},
        {"archive", ArgRequired, 'a'},
        {"backEndInstName", ArgRequired, 'n'},
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"configDir", ArgRequired, 'D'},
        {"quiet", ArgNone, 'q'},
        {"verbose", ArgNone, 'V'},
        {0, 0, 0}};


    char *opts_db2archive = "vd:i:a:SD:qV";
    struct opt_ext long_options_db2archive[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"pidfile", ArgRequired, 'i'},
        {"archive", ArgRequired, 'a'},
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"configDir", ArgRequired, 'D'},
        {"quiet", ArgNone, 'q'},
        {"verbose", ArgNone, 'V'},
        {0, 0, 0}};

    char *opts_db2index = "vd:a:t:T:SD:n:s:x:";
    struct opt_ext long_options_db2index[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"backend", ArgRequired, 'n'},
        {"archive", ArgRequired, 'a'},
        {"indexAttribute", ArgRequired, 't'},
        {"vlvIndex", ArgRequired, 'T'},
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"configDir", ArgRequired, 'D'},
        {"include", ArgRequired, 's'},
        {"exclude", ArgRequired, 'x'},
        {0, 0, 0}};

    char *opts_upgradedb = "vfrd:a:D:";
    struct opt_ext long_options_upgradedb[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"force", ArgNone, 'f'},
        {"dn2rdn", ArgNone, 'r'},
        {"archive", ArgRequired, 'a'},
        {"configDir", ArgRequired, 'D'},
        {0, 0, 0}};

    char *opts_upgradednformat = "vd:a:n:D:N";
    struct opt_ext long_options_upgradednformat[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"backend", ArgRequired, 'n'},
        {"archive", ArgRequired, 'a'}, /* Path to the work db instance dir */
        {"configDir", ArgRequired, 'D'},
        {"dryrun", ArgNone, 'N'},
        {0, 0, 0}};

    char *opts_dbverify = "vVfd:n:D:a:";
    struct opt_ext long_options_dbverify[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"backend", ArgRequired, 'n'},
        {"configDir", ArgRequired, 'D'},
        {"verbose", ArgNone, 'V'},
        {"dbdir", ArgRequired, 'a'},
        {0, 0, 0}};

    char *opts_referral = "vd:p:r:SD:";
    struct opt_ext long_options_referral[] = {
        {"version", ArgNone, 'v'},
        {"debug", ArgRequired, 'd'},
        {"port", ArgRequired, 'p'},
        {"referralMode", ArgRequired, 'r'},
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"configDir", ArgRequired, 'D'},
        {0, 0, 0}};

    char *opts_suffix2instance = "s:D:";
    struct opt_ext long_options_suffix2instance[] = {
        {"suffix", ArgRequired, 's'},
        {"instanceDir", ArgRequired, 'D'},
        {0, 0, 0}};

    char *opts_slapd = "vVd:i:SD:w:";
    struct opt_ext long_options_slapd[] = {
        {"version", ArgNone, 'v'},
        {"versionFull", ArgNone, 'V'},
        {"debug", ArgRequired, 'd'},
        {"pidfile", ArgRequired, 'i'},
        {"allowMultipleProcesses", ArgNone, 'S'},
        {"configDir", ArgRequired, 'D'},
        {"startpidfile", ArgRequired, 'w'},
        {0, 0, 0}};

    /*
     * determine which of serveral modes we are executing in.
     */
    mcfg->extraname = NULL;
    if ((mcfg->slapd_exemode = name2exemode(mcfg->myname, mcfg->myname, 0)) == SLAPD_EXEMODE_UNKNOWN) {


        if (argv[1] != NULL && argv[1][0] != '-') {
            mcfg->slapd_exemode = name2exemode(mcfg->myname, argv[1], 1);
            mcfg->extraname = argv[1];
            optind_ext = 2; /* make getopt() skip argv[1] */
            optind = 2;
        }
    }
    if (mcfg->slapd_exemode == SLAPD_EXEMODE_UNKNOWN) {
        mcfg->slapd_exemode = SLAPD_EXEMODE_SLAPD; /* default */
    }
    /*
     * richm: If running in regular slapd server mode, allow the front
     * end dse files (dse.ldif and ldbm.ldif) to be written in case of
     * additions or modifications.  In all other modes, these files
     * should only be read and never written.
     */

    if (mcfg->slapd_exemode == SLAPD_EXEMODE_SLAPD ||
        mcfg->slapd_exemode == SLAPD_EXEMODE_ARCHIVE2DB || /* bak2db adjusts config */
        mcfg->slapd_exemode == SLAPD_EXEMODE_UPGRADEDB)    /* update idl-switch */
        dse_unset_dont_ever_write_dse_files();

    /* maintain compatibility with pre-5.x options */
    switch (mcfg->slapd_exemode) {
    case SLAPD_EXEMODE_DB2LDIF:
        opts = opts_db2ldif;
        long_opts = long_options_db2ldif;
        break;
    case SLAPD_EXEMODE_LDIF2DB:
        opts = opts_ldif2db;
        long_opts = long_options_ldif2db;
        break;
    case SLAPD_EXEMODE_ARCHIVE2DB:
        opts = opts_archive2db;
        long_opts = long_options_archive2db;
        break;
    case SLAPD_EXEMODE_DB2ARCHIVE:
        opts = opts_db2archive;
        long_opts = long_options_db2archive;
        break;
    case SLAPD_EXEMODE_DB2INDEX:
        opts = opts_db2index;
        long_opts = long_options_db2index;
        break;
    case SLAPD_EXEMODE_REFERRAL:
        /* Default to not detaching, but if REFERRAL, turn it on. */
        should_detach = 1;
        opts = opts_referral;
        long_opts = long_options_referral;
        break;
    case SLAPD_EXEMODE_SUFFIX2INSTANCE:
        opts = opts_suffix2instance;
        long_opts = long_options_suffix2instance;
        break;
    case SLAPD_EXEMODE_UPGRADEDB:
        opts = opts_upgradedb;
        long_opts = long_options_upgradedb;
        break;
    case SLAPD_EXEMODE_UPGRADEDNFORMAT:
        opts = opts_upgradednformat;
        long_opts = long_options_upgradednformat;
        break;
    case SLAPD_EXEMODE_DBVERIFY:
        opts = opts_dbverify;
        long_opts = long_options_dbverify;
        break;
    default: /* SLAPD_EXEMODE_SLAPD */
             /* Default to not detaching, but if SLAPD, turn it on. */
        should_detach = 1;
        opts = opts_slapd;
        long_opts = long_options_slapd;
    }

    while ((i = getopt_ext(argc, argv, opts,
                           long_opts, &longopt_index)) != EOF) {
        char *configdir = 0;
        switch (i) {
#ifdef LDAP_ERROR_LOGGING
        case 'd': /* turn on debugging */
            if (optarg_ext[0] == '?' || 0 == strcasecmp(optarg_ext, "help")) {
                slapd_debug_level_usage();
                exit(1);
            } else {
                should_detach = 0;
                slapd_ldap_debug = slapd_debug_level_string2level(optarg_ext);
                if (slapd_ldap_debug < 0) {
                    slapd_debug_level_usage();
                    exit(1);
                }
                slapd_ldap_debug |= LDAP_DEBUG_ANY;
            }
            break;
#else
        case 'd': /* turn on debugging */
            fprintf(stderr,
                    "must compile with LDAP_ERROR_LOGGING for debugging\n");
            break;
#endif

        case 'D': /* config dir */
            configdir = rel2abspath(optarg_ext);

            if (config_set_configdir("configdir (-D)",
                                     configdir, errorbuf, 1) != LDAP_SUCCESS) {
                fprintf(stderr, "%s: aborting now\n", errorbuf);
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            slapi_ch_free((void **)&configdir);

            break;

        case 'p': /* port on which to listen (referral mode only) */
            if (config_set_port("portnumber (-p)", optarg_ext,
                                errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                fprintf(stderr, "%s: aborting now\n", errorbuf);
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            break;

        case 'i': /* set pid log file or ldif2db LDIF file */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_LDIF2DB) {
                char *p;
                /* if LDIF comes through standard input, skip path checking */
                if (optarg_ext[0] != '-' || strlen(optarg_ext) != 1) {
                    if (optarg_ext[0] != '/') {
                        fprintf(stderr, "%s file could not be opened: absolute path "
                                        " required.\n",
                                optarg_ext);
                        break;
                    }
                }
                p = (char *)slapi_ch_malloc(strlen(optarg_ext) + 1);

                strcpy(p, optarg_ext);
                charray_add(&(mcfg->ldif_file), p);
                mcfg->ldif_files++;
            } else {
                pid_file = rel2abspath(optarg_ext);
            }
            break;
        case 'w': /* set startup pid file */
            start_pid_file = rel2abspath(optarg_ext);
            break;
        case 'n': /* which backend to do ldif2db/bak2db for */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_LDIF2DB ||
                mcfg->slapd_exemode == SLAPD_EXEMODE_UPGRADEDNFORMAT ||
                mcfg->slapd_exemode == SLAPD_EXEMODE_DB2INDEX) {
                /* The -n argument will give the name of a backend instance. */
                mcfg->cmd_line_instance_name = optarg_ext;
            } else if (mcfg->slapd_exemode == SLAPD_EXEMODE_DB2LDIF ||
                       mcfg->slapd_exemode == SLAPD_EXEMODE_DBVERIFY) {
                char *s = slapi_ch_strdup(optarg_ext);
                charray_add(&(mcfg->cmd_line_instance_names), s);
            }
            break;
        case 's': /* which suffix to include in import/export */
        {
            int rc = charray_normdn_add(&(mcfg->db2ldif_include), optarg_ext, NULL);
            if (rc < 0) {
                fprintf(stderr, "Invalid dn: -s %s\n", optarg_ext);
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
        } break;
        case 'x': /* which suffix to exclude in import/export */
        {
            int rc = charray_normdn_add(&(mcfg->db2ldif_exclude), optarg_ext, NULL);
            if (rc < 0) {
                fprintf(stderr, "Invalid dn: -x %s\n", optarg_ext);
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
        } break;
        case 'r': /* db2ldif for replication */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_REFERRAL) {
                if (config_set_referral_mode("referral (-r)", optarg_ext,
                                             errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                    fprintf(stderr, "%s: aborting now\n", errorbuf);
                    usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                    exit(1);
                }
                break;
            } else if (mcfg->slapd_exemode == SLAPD_EXEMODE_UPGRADEDB) {
                mcfg->upgradedb_flags |= SLAPI_UPGRADEDB_DN2RDN;
                break;
            } else if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->db2ldif_dump_replica = 1;
            break;
        case 'N': /* do not do ldif2db duplicate value check */
                  /* Or dryrun mode for upgradednformat */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB &&
                mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF &&
                mcfg->slapd_exemode != SLAPD_EXEMODE_UPGRADEDNFORMAT) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            /*
             * -N flag is obsolete, but we silently accept it
             * so we don't break customer's scripts.
             */

            /* The -N flag now does what the -n flag used to do for db2ldif.
             * This is so -n cane be used for the instance name just like
             * with ldif2db. */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_DB2LDIF) {
                mcfg->ldif_printkey &= ~EXPORT_PRINTKEY;
            }
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_UPGRADEDNFORMAT) {
                mcfg->upgradednformat_dryrun = 1;
            }

            break;

        case 'U': /* db2ldif only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /*
             * don't fold (wrap) long lines (default is to fold),
             * as of ldapsearch -T
             */
            mcfg->ldif_printkey |= EXPORT_NOWRAP;

            break;

        case 'm': /* db2ldif only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /* minimal base64 encoding */
            mcfg->ldif_printkey |= EXPORT_MINIMAL_ENCODING;

            break;

        case 'M': /* db2ldif only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /*
             * output ldif is stored in several file called intance_filename.
             * by default, all instances are stored in the single filename.
             */
            mcfg->ldif_printkey &= ~EXPORT_APPENDMODE;

            break;

        case 'o': /* db2ldif only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /*
             * output ldif is stored in one file.
             * by default, each instance is stored in instance_filename.
             */
            mcfg->ldif_printkey |= EXPORT_APPENDMODE;

            break;

        case 'R': /* db2ldif  and ldif2db only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF &&
                mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /*
             * import/export should handle changelog.
             */
            mcfg->ldif_include_changelog = 1;

            break;

        case 'C':
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_LDIF2DB) {
                /* used to mean "Cool new import" (which is now
                 * the default) -- ignore
                 */
                break;
            }
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_DB2LDIF) {
                /* possibly corrupted db -- don't look at any
                 * file except id2entry.  yet another overloaded
                 * flag.
                 */
                mcfg->ldif_printkey |= EXPORT_ID2ENTRY_ONLY;
                break;
            }
            usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
            exit(1);

        case 'c': /* merge chunk size for Cool new import */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->ldif2db_removedupvals = atoi(optarg_ext); /* We overload this flag---ok since we always check for dupes in the new code */
            break;

        case 'O': /* only create core db, no attr indexes */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->ldif2db_noattrindexes = 1;
            break;

        case 't': /* attribute type to index - may be repeated */
        case 'T': /* VLV Search to index - may be repeated */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_DB2INDEX) {
                char *p = slapi_ch_smprintf("%c%s", i, optarg_ext);
                charray_add(&(mcfg->db2index_attrs), p);
                break;
            }
            usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
            exit(1);

        case 'v': /* print version and exit */
            slapd_print_version(0);
            exit(1);
            break;

        case 'V': /* verbose option for dbverify, db2ldif, ldif2db, db2bak, bak2db */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_DBVERIFY) {
                mcfg->dbverify_verbose = 1;
            } else if (mcfg->slapd_exemode == SLAPD_EXEMODE_LDIF2DB ||
                mcfg->slapd_exemode == SLAPD_EXEMODE_DB2LDIF ||
                mcfg->slapd_exemode == SLAPD_EXEMODE_ARCHIVE2DB ||
                mcfg->slapd_exemode == SLAPD_EXEMODE_DB2ARCHIVE) {
                mcfg->backuptools_verbose = 1;
            } else {
                mcfg->slapd_exemode = SLAPD_EXEMODE_PRINTVERSION;
            }
            break;

        case 'a': /* archive pathname for db */
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_DBVERIFY) {
                mcfg->dbverify_dbdir = optarg_ext;
            } else {
                mcfg->archive_name = optarg_ext;
            }
            break;

        case 'Z':
            if (mcfg->slapd_exemode == SLAPD_EXEMODE_LDIF2DB) {
                break;
            }
            usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
            exit(1);
        case 'S': /* skip the check for slad running in conflicting modes */
            mcfg->skip_db_protect_check = 1;
            break;
        case 'u': /* do not dump uniqueid for db2ldif */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->db2ldif_dump_uniqueid = 0;
            break;
        case 'g': /* generate uniqueid for ldif2db */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            if (optarg_ext == NULL) {
                printf("ldif2db: generation type is not specified for -g; "
                       "random generation is used\n");
                mcfg->ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
            } else if (strcasecmp(optarg_ext, "none") == 0)
                mcfg->ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_NONE;
            else if (strcasecmp(optarg_ext, "deterministic") == 0) /* name based */
                mcfg->ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_NAME_BASED;
            else /* default - time based */
                mcfg->ldif2db_generate_uniqueid = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
            break;
        case 'G': /* namespace id for name based uniqueid generation for ldif2db */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            mcfg->ldif2db_namespaceid = optarg_ext;
            break;
        case 'E': /* encrypt data if importing, decrypt if exporting */
            if ((mcfg->slapd_exemode != SLAPD_EXEMODE_LDIF2DB) && (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF)) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->importexport_encrypt = 1;
            break;
        case 'f': /* upgradedb only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_UPGRADEDB) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }
            mcfg->upgradedb_flags |= SLAPI_UPGRADEDB_FORCE;
            break;
        case '1': /* db2ldif only */
            if (mcfg->slapd_exemode != SLAPD_EXEMODE_DB2LDIF) {
                usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
                exit(1);
            }

            /*
             * do not output "version: 1" to the ldif file
             */
            mcfg->ldif_printkey |= EXPORT_NOVERSION;

            break;
        case 'q': /* quiet option for db2ldif, ldif2db, db2bak, bak2db */
            mcfg->is_quiet = 1;
            break;
        default:
            usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
            exit(1);
        }
    }

    if ((NULL != mcfg->cmd_line_instance_names) && (NULL != mcfg->cmd_line_instance_names[1]) && (mcfg->ldif_printkey & EXPORT_APPENDMODE)) {
        fprintf(stderr, "WARNING: several backends are being"
                        " exported to a single ldif file\n");
        fprintf(stderr, "         use option -M to export to"
                        " multiple ldif files\n");
    }
    /* Any leftover arguments? */
    if (optind_last > optind) {
        usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
        exit(1);
    }

    return;
}

static int
lookup_instance_name_by_suffix(char *suffix,
                               char ***suffixes,
                               char ***instances,
                               int isexact)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL, **ep;
    char *query;
    char *backend_name;
    char *fullsuffix;
    int rval = -1;

    if (pb == NULL)
        goto done;

    if (isexact) {
        query = slapi_filter_sprintf("(&(objectclass=nsmappingtree)(|(cn=\"%s%s\")(cn=%s%s)))",
                                     ESC_NEXT_VAL, suffix, ESC_NEXT_VAL, suffix);
        if (query == NULL)
            goto done;

        /* Note: This DN is no need to be normalized. */
        slapi_search_internal_set_pb(pb, "cn=mapping tree,cn=config",
                                     LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
                                     (void *)plugin_get_default_component_id(), 0);
        slapi_search_internal_pb(pb);
        slapi_ch_free((void **)&query);

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
        if (rval != LDAP_SUCCESS)
            goto done;

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if ((entries == NULL) || (entries[0] == NULL))
            goto done;

    } else {
        char *suffixp = suffix;
        while (NULL != suffixp && strlen(suffixp) > 0) {
            query = slapi_filter_sprintf("(&(objectclass=nsmappingtree)(|(cn=*%s%s\")(cn=*%s%s)))",
                                         ESC_NEXT_VAL, suffixp, ESC_NEXT_VAL, suffixp);
            if (query == NULL)
                goto done;
            /* Note: This DN is no need to be normalized. */
            slapi_search_internal_set_pb(pb, "cn=mapping tree,cn=config",
                                         LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
                                         (void *)plugin_get_default_component_id(), 0);
            slapi_search_internal_pb(pb);
            slapi_ch_free((void **)&query);

            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
            if (rval != LDAP_SUCCESS)
                goto done;

            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if ((entries == NULL) || (entries[0] == NULL)) {
                suffixp = strchr(suffixp, ','); /* get a parent dn */
                if (NULL != suffixp) {
                    suffixp++;
                }
            } else {
                break; /* found backend entries */
            }
        }
    }

    rval = 0;
    for (ep = entries; ep && *ep; ep++) {
        backend_name = slapi_entry_attr_get_charptr(*ep, "nsslapd-backend");
        if (backend_name) {
            charray_add(instances, backend_name);
            if (suffixes) {
                fullsuffix = slapi_entry_attr_get_charptr(*ep, "cn");
                charray_add(suffixes, fullsuffix); /* NULL is ok */
            }
        }
    }

done:
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    return rval;
}

int
lookup_instance_name_by_suffixes(char **included, char **excluded, char ***instances)
{
    char **incl_instances, **excl_instances;
    char **p;
    int rval = -1;

    incl_instances = NULL;
    for (p = included; p && *p; p++) {
        if (lookup_instance_name_by_suffix(*p, NULL, &incl_instances, 0) < 0)
            return rval;
    }

    excl_instances = NULL;
    for (p = excluded; p && *p; p++) {
        if (lookup_instance_name_by_suffix(*p, NULL, &excl_instances, 0) < 0)
            return rval;
    }

    rval = 0;
    charray_subtract(incl_instances, excl_instances, NULL);
    charray_free(excl_instances);
    *instances = incl_instances;
    return rval;
}

/* helper function for ldif2db & friends -- given an instance name, lookup
 * the plugin name in the DSE.  this assumes the DSE has already been loaded.
 */
static struct slapdplugin *
lookup_plugin_by_instance_name(const char *name)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *pb = slapi_pblock_new();
    struct slapdplugin *plugin = NULL;
    char *query, *dn, *cn;
    int ret = 0;

    if (pb == NULL)
        return NULL;

    query = slapi_filter_sprintf("(&(cn=%s%s)(objectclass=nsBackendInstance))", ESC_AND_NORM_NEXT_VAL, name);
    if (query == NULL) {
        slapi_pblock_destroy(pb);
        return NULL;
    }

    /* Note: This DN is no need to be normalized. */
    slapi_search_internal_set_pb(pb, "cn=plugins,cn=config",
                                 LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
                                 (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&query);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    /* okay -- have the entry for this instance, now let's chop up the dn */
    /* parent dn is the plugin */
    dn = slapi_dn_parent(slapi_entry_get_dn(entries[0]));

    /* clean up */
    slapi_free_search_results_internal(pb);
    entries = NULL;
    slapi_pblock_destroy(pb);
    pb = NULL; /* this seems redundant . . . until we add code after this line */

    /* now... look up the parent */
    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_BASE,
                                 "(objectclass=nsSlapdPlugin)", NULL, 0, NULL, NULL,
                                 (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&dn);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL)) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        return NULL;
    }

    if((cn = (char *)slapi_entry_attr_get_ref(entries[0], "cn"))) {
        plugin = plugin_get_by_name(cn);
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return plugin;
}

static int
slapd_exemode_ldif2db(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *plugin;
    char **instances = NULL;

    if (mcfg->ldif_file == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                      "Required argument -i <ldiffile> missing\n");
        usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
        return 1;
    }

    /* this should be the first time to be called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    /*
     * if instance is given, just use it to get the backend.
     * otherwise, we use included/excluded suffix list to specify a backend.
     */
    if (NULL == mcfg->cmd_line_instance_name) {
        char **ip;
        int counter;

        if (lookup_instance_name_by_suffixes(mcfg->db2ldif_include, mcfg->db2ldif_exclude, &instances) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                          "Backend instances name [-n <name>] or "
                          "included suffix [-s <suffix>] need to be specified.\n");
            return 1;
        }

        if (instances) {
            for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
                ;

            if (counter == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                              "There is no backend instance to import to.\n");
                return 1;
            } else if (counter > 1) {
                int i;
                slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                              "There are multiple backend instances specified:\n");
                for (i = 0; i < counter; i++)
                    slapi_log_err(SLAPI_LOG_ERR, "     : %s\n",
                                  instances[i], 0, 0);
                return 1;
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_exemode_ldif2db", "Backend Instance: %s\n",
                              *instances);
                mcfg->cmd_line_instance_name = *instances;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                          "There is no backend instances to import to.\n");
            return 1;
        }
    }

    plugin = lookup_plugin_by_instance_name(mcfg->cmd_line_instance_name);
    if (plugin == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                      "Could not find backend '%s'.\n",
                      mcfg->cmd_line_instance_name);
        return 1;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if (add_new_slapd_process(mcfg->slapd_exemode, mcfg->db2ldif_dump_replica,
                              mcfg->skip_db_protect_check) == -1) {

        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                      "Shutting down due to possible conflicts with other slapd processes\n");
        return 1;
    }
    /* check for slapi v2 support */
    if (!SLAPI_PLUGIN_IS_V2(plugin)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db", "%s is too old to reindex all.\n",
                      plugin->plg_name);
        return 1;
    }
    if (mcfg->backuptools_verbose) {
        slapd_ldap_debug |= LDAP_DEBUG_BACKLDBM;
    }
    if (!(slapd_ldap_debug & LDAP_DEBUG_BACKLDBM)) {
        g_set_detached(1);
    }
    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_REMOVEDUPVALS, &(mcfg->ldif2db_removedupvals));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_NOATTRINDEXES, &(mcfg->ldif2db_noattrindexes));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &(mcfg->ldif2db_generate_uniqueid));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_NAMESPACEID, &(mcfg->ldif2db_namespaceid));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_ENCRYPT, &(mcfg->importexport_encrypt));
    slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, mcfg->cmd_line_instance_name);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_FILE, mcfg->ldif_file);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_INCLUDE, mcfg->db2ldif_include);
    slapi_pblock_set(pb, SLAPI_LDIF2DB_EXCLUDE, mcfg->db2ldif_exclude);
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    if (plugin->plg_ldif2db != NULL) {
        return_value = (*plugin->plg_ldif2db)(pb);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_ldif2db",
                      "No ldif2db function defined for "
                      "%s\n",
                      plugin->plg_name);
        return_value = -1;
    }

    /* check for task warnings */
    if(!return_value) {
        if((return_value = slapi_pblock_get_task_warning(pb))) {
            slapi_log_err(SLAPI_LOG_INFO, "slapd_exemode_ldif2db","returning task warning: %d\n", return_value);
        }
    }

    slapi_pblock_destroy(pb);
    charray_free(instances);
    charray_free(mcfg->cmd_line_instance_names);
    charray_free(mcfg->db2ldif_include);
    charray_free(mcfg->db2index_attrs);
    charray_free(mcfg->ldif_file);
    return (return_value);
}

static int
slapd_exemode_db2ldif(int argc, char **argv, struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *plugin;
    char *my_ldiffile;
    char **instp;

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    /*
     * if instance is given, just pass it to the backend.
     * otherwise, we use included/excluded suffix list to specify a backend.
     */
    if (NULL == mcfg->cmd_line_instance_names) {
        char **instances, **ip;
        int counter;

        if (lookup_instance_name_by_suffixes(mcfg->db2ldif_include, mcfg->db2ldif_exclude, &instances) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "Backend instances name [-n <name>] or "
                          "included suffix [-s <suffix>] need to be specified.\n");
            return 1;
        }

        if (instances) {
            for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
                ;

            if (counter == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                              "There is no backend instance to export from.\n");
                return 1;
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_exemode_db2ldif", "db2ldif - Backend Instance(s): \n");
                for (ip = instances, counter = 0; ip && *ip; ip++, counter++) {
                    slapi_log_err(SLAPI_LOG_INFO, "slapd_exemode_db2ldif", "db2ldif - %s\n", *ip);
                }
                mcfg->cmd_line_instance_names = instances;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "There is no backend instances to export from.\n");
            return 1;
        }
    }

    for (instp = mcfg->cmd_line_instance_names; instp && *instp; instp++) {
        int release_me = 0;

        plugin = lookup_plugin_by_instance_name(*instp);
        if (plugin == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "Could not find backend '%s'.\n", *instp);
            return 1;
        }

        if (plugin->plg_db2ldif == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "No db2ldif function defined for backend %s - cannot export\n", *instp);
            return 1;
        }

        /* Make sure we aren't going to run slapd in
         * a mode that is going to conflict with other
         * slapd processes that are currently running
         */
        if (add_new_slapd_process(mcfg->slapd_exemode, mcfg->db2ldif_dump_replica,
                                  mcfg->skip_db_protect_check) == -1) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "Shutting down due to possible conflicts "
                          "with other slapd processes\n");
            return 1;
        }

        if (!(SLAPI_PLUGIN_IS_V2(plugin))) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2ldif",
                          "%s is too old to do exports.\n", plugin->plg_name);
            return 1;
        }

        if (mcfg->backuptools_verbose) {
            slapd_ldap_debug |= LDAP_DEBUG_BACKLDBM;
        }
        if (!(slapd_ldap_debug & LDAP_DEBUG_BACKLDBM)) {
            g_set_detached(1);
        }
        Slapi_PBlock *pb = slapi_pblock_new();
        slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
        slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
        slapi_pblock_set(pb, SLAPI_LDIF2DB_INCLUDE, mcfg->db2ldif_include);
        slapi_pblock_set(pb, SLAPI_LDIF2DB_EXCLUDE, mcfg->db2ldif_exclude);
        slapi_pblock_set(pb, SLAPI_LDIF2DB_ENCRYPT, &(mcfg->importexport_encrypt));
        slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, *instp);
        slapi_pblock_set_ldif_dump_replica(pb, mcfg->db2ldif_dump_replica);
        slapi_pblock_set(pb, SLAPI_DB2LDIF_DUMP_UNIQUEID, &(mcfg->db2ldif_dump_uniqueid));
        slapi_pblock_set(pb, SLAPI_LDIF_CHANGELOG, &(mcfg->ldif_include_changelog));
        int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
        slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
        int32_t is_running = 0;
        if (is_slapd_running()) {
            is_running = 1;
        }
        slapi_pblock_set(pb, SLAPI_DB2LDIF_SERVER_RUNNING, &is_running);

        if (mcfg->db2ldif_dump_replica) {
            char **plugin_list = NULL;
            char *repl_plg_name = "Multisupplier Replication Plugin";

            /*
             * Only start the necessary plugins for "db2ldif -r"
             *
             * We need replication, but replication has its own
             * dependencies
             */
            plugin_get_plugin_dependencies(repl_plg_name, &plugin_list);

            eq_init(); /* must be done before plugins started - DEPRECATED */
            eq_init_rel(); /* must be done before plugins started */

            ps_init_psearch_system(); /* must come before plugin_startall() */
            plugin_startall(argc, argv, plugin_list);
            eq_start(); /* must be done after plugins started - DEPRECATED*/
            eq_start_rel(); /* must be done after plugins started */
            charray_free(plugin_list);
        }

        if (mcfg->archive_name) { /* redirect stdout to this file: */
            char *p, *q;
            char sep = '/';

            my_ldiffile = mcfg->archive_name;
            if (mcfg->ldif_printkey & EXPORT_APPENDMODE) {
                if (instp == mcfg->cmd_line_instance_names) { /* first export */
                    mcfg->ldif_printkey |= EXPORT_APPENDMODE_1;
                } else {
                    mcfg->ldif_printkey &= ~EXPORT_APPENDMODE_1;
                }
            } else {                                   /* not APPENDMODE */
                if (strcmp(mcfg->archive_name, "-")) { /* not '-' */
                    my_ldiffile =
                        (char *)slapi_ch_malloc((unsigned long)(strlen(mcfg->archive_name) + strlen(*instp) + 2));
                    p = strrchr(mcfg->archive_name, sep);
                    if (NULL == p) {
                        sprintf(my_ldiffile, "%s_%s", *instp, mcfg->archive_name);
                    } else {
                        q = p + 1;
                        *p = '\0';
                        sprintf(my_ldiffile, "%s%c%s_%s",
                                mcfg->archive_name, sep, *instp, q);
                        *p = sep;
                    }
                    release_me = 1;
                }
            }

            if (!mcfg->is_quiet) {
                fprintf(stderr, "ldiffile: %s\n", my_ldiffile);
            }
            /* just send the filename to the backend and let
             * the backend open it (so they can do special
             * stuff for 64-bit fs)
             */
            slapi_pblock_set(pb, SLAPI_DB2LDIF_FILE, my_ldiffile);
            slapi_pblock_set(pb, SLAPI_DB2LDIF_PRINTKEY, &(mcfg->ldif_printkey));
        }

        return_value = (plugin->plg_db2ldif)(pb);

        slapi_pblock_destroy(pb);

        if (release_me) {
            slapi_ch_free((void **)&my_ldiffile);
        }
    }
    charray_free(mcfg->cmd_line_instance_names);
    charray_free(mcfg->db2ldif_include);
    if (mcfg->db2ldif_dump_replica) {
        eq_stop(); /* DEPRECATED*/
        eq_stop_rel(); /* event queue should be shutdown before closing
                          all plugins (especially, replication plugin) */
        plugin_closeall(1 /* Close Backends */, 1 /* Close Globals */);
    }
    return (return_value);
}

static int
slapd_exemode_suffix2instance(struct main_config *mcfg)
{
    int return_value = 0;
    char **instances = NULL;
    char **suffixes = NULL;
    char **p, **q, **r;

    /* this should be the first time this are called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    for (p = mcfg->db2ldif_include; p && *p; p++) {
        if (lookup_instance_name_by_suffix(*p, &suffixes, &instances, 0) < 0)
            continue;
        fprintf(stderr, "Suffix, Instance name pair(s) under \"%s\":\n", *p);
        if (instances) {
            for (q = suffixes, r = instances; *r; q++, r++) {
                fprintf(stderr, "\tsuffix %s; instance name \"%s\"\n",
                        *q ? *q : "-", *r);
            }
        } else {
            fprintf(stderr, "\tNo instance\n");
        }
        charray_free(suffixes);
        suffixes = NULL;
        charray_free(instances);
        instances = NULL;
    }
    return (return_value);
}

static int
slapd_exemode_db2index(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *plugin;
    char **instances = NULL;

    mapping_tree_init();

    /*
     * if instance is given, just use it to get the backend.
     * otherwise, we use included/excluded suffix list to specify a backend.
     */
    if (NULL == mcfg->cmd_line_instance_name) {
        char **ip;
        int counter;

        if (lookup_instance_name_by_suffixes(mcfg->db2ldif_include, mcfg->db2ldif_exclude, &instances) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                          "Backend instances name [-n <name>] or "
                          "included suffix [-s <suffix>] need to be specified.\n");
            return 1;
        }

        if (instances) {
            for (ip = instances, counter = 0; ip && *ip; ip++, counter++)
                ;

            if (counter == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                              "There is no backend instance to import to.\n");
                return 1;
            } else if (counter > 1) {
                int i;
                slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                              "There are multiple backend instances specified:\n");
                for (i = 0; i < counter; i++)
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                                  "-- %s\n", instances[i]);
                return 1;
            } else {
                slapi_log_err(SLAPI_LOG_INFO, "slapd_exemode_db2index",
                              "Backend Instance: %s\n", *instances);
                mcfg->cmd_line_instance_name = *instances;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                          "There is no backend instances to import to.\n");
            return 1;
        }
    }

    plugin = lookup_plugin_by_instance_name(mcfg->cmd_line_instance_name);
    if (plugin == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                      "Could not find backend '%s'.\n",
                      mcfg->cmd_line_instance_name);
        return 1;
    }

    /* make sure nothing else is running */
    if (add_new_slapd_process(mcfg->slapd_exemode, mcfg->db2ldif_dump_replica,
                              mcfg->skip_db_protect_check) == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2index",
                      "Shutting down due to possible conflicts with other "
                      "slapd processes.\n");
        return 1;
    }

    if (mcfg->db2index_attrs == NULL) {
        usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
        return 1;
    }
    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
    slapi_pblock_set(pb, SLAPI_DB2INDEX_ATTRS, mcfg->db2index_attrs);
    slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, mcfg->cmd_line_instance_name);
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    return_value = (*plugin->plg_db2index)(pb);

    slapi_pblock_destroy(pb);
    charray_free(mcfg->db2index_attrs);
    charray_free(mcfg->db2ldif_include);
    /* This frees  mcfg->cmd_line_instance_name */
    charray_free(instances);

    return (return_value);
}


static int
slapd_exemode_db2archive(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *backend_plugin;

    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapd_exemode_db2archive",
                      "Could not find the ldbm backend plugin.\n");
        return 1;
    }
    if (NULL == mcfg->archive_name) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2archive",
                      "No archive directory supplied\n");
        return 1;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if (add_new_slapd_process(mcfg->slapd_exemode, mcfg->db2ldif_dump_replica,
                              mcfg->skip_db_protect_check) == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_db2archive",
                      "Shutting down due to possible conflicts with other slapd processes\n");
        return 1;
    }

    if (mcfg->backuptools_verbose) {
        slapd_ldap_debug |= LDAP_DEBUG_BACKLDBM;
    }
    if (!(slapd_ldap_debug & LDAP_DEBUG_BACKLDBM)) {
        g_set_detached(1);
    }

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, backend_plugin);
    slapi_pblock_set(pb, SLAPI_SEQ_VAL, mcfg->archive_name);
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    return_value = (backend_plugin->plg_db2archive)(pb);
    slapi_pblock_destroy(pb);
    return return_value;
}

static int
slapd_exemode_archive2db(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *backend_plugin;

    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapd_exemode_archive2db",
                      "Could not find the ldbm backend plugin.\n");
        return 1;
    }
    if (NULL == mcfg->archive_name) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_archive2db",
                      "No archive directory supplied\n");
        return 1;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if (add_new_slapd_process(mcfg->slapd_exemode, mcfg->db2ldif_dump_replica,
                              mcfg->skip_db_protect_check) == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_archive2db",
                      "Shutting down due to possible conflicts with other slapd processes\n");
        return 1;
    }

    if (mcfg->backuptools_verbose) {
        slapd_ldap_debug |= LDAP_DEBUG_BACKLDBM;
    }
    if (!(slapd_ldap_debug & LDAP_DEBUG_BACKLDBM)) {
        g_set_detached(1);
    }

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, backend_plugin);
    slapi_pblock_set(pb, SLAPI_SEQ_VAL, mcfg->archive_name);
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    return_value = (backend_plugin->plg_archive2db)(pb);
    slapi_pblock_destroy(pb);
    return return_value;
}

/*
 * functions to convert idl from the old format to the new one
 * (604921) Support a database uprev process any time post-install
 */
static int
slapd_exemode_upgradedb(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *backend_plugin;

    if (mcfg->archive_name == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradedb",
                      "Required argument -a <backup_dir> missing\n");
        usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
        return 1;
    }

    /* this should be the first time to be called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradedb",
                      "Could not find the ldbm backend plugin.\n");
        return 1;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     */
    if (add_new_slapd_process(mcfg->slapd_exemode, 0, mcfg->skip_db_protect_check) == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradedb",
                      "Shutting down due to possible conflicts with other slapd processes\n");
        return 1;
    }
    /* check for slapi v2 support */
    if (!SLAPI_PLUGIN_IS_V2(backend_plugin)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradedb",
                      "%s is too old to do convert idl.\n", backend_plugin->plg_name);
        return 1;
    }

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, backend_plugin);
    slapi_pblock_set(pb, SLAPI_SEQ_VAL, mcfg->archive_name);
    slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &(mcfg->upgradedb_flags));
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    /* borrowing import code, so need to set up the import variables */
    slapi_pblock_set(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &(mcfg->ldif2db_generate_uniqueid));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_NAMESPACEID, &(mcfg->ldif2db_namespaceid));
    if (backend_plugin->plg_upgradedb != NULL) {
        return_value = (*backend_plugin->plg_upgradedb)(pb);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradedb",
                      "No upgradedb function defined for "
                      "%s\n",
                      backend_plugin->plg_name);
        return_value = -1;
    }
    slapi_pblock_destroy(pb);
    return (return_value);
}

/* Command to upgrade the old dn format to the new style */
static int
slapd_exemode_upgradednformat(struct main_config *mcfg)
{
    int rc = -1; /* error, by default */
    struct slapdplugin *backend_plugin;

    if (mcfg->archive_name == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradednformat",
                      "Required argument \"-a <path to work db instance dir>\" is missing\n");
        usage(mcfg->myname, mcfg->extraname, mcfg->slapd_exemode);
        goto bail;
    }

    /* this should be the first time to be called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();

    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradednformat",
                      "Could not find the ldbm backend plugin.\n");
        goto bail;
    }

    /* Make sure we aren't going to run slapd in
     * a mode that is going to conflict with other
     * slapd processes that are currently running
     * Pretending to execute import.
     */
    if (add_new_slapd_process(mcfg->slapd_exemode, 0, mcfg->skip_db_protect_check) == -1) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradednformat",
                      "Shutting down due to possible "
                      "conflicts with other slapd processes\n");
        goto bail;
    }
    /* check for slapi v2 support */
    if (!SLAPI_PLUGIN_IS_V2(backend_plugin)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradednformat",
                      "%s is too old to upgrade dn format.\n",
                      backend_plugin->plg_name);
        goto bail;
    }

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, backend_plugin);
    slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, mcfg->cmd_line_instance_name);
    int32_t seq_type = SLAPI_UPGRADEDNFORMAT;
    if (mcfg->upgradednformat_dryrun) {
        seq_type = SLAPI_UPGRADEDNFORMAT | SLAPI_DRYRUN;
    }
    slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &seq_type);
    slapi_pblock_set(pb, SLAPI_SEQ_VAL, mcfg->archive_name); /* Path to the work db instance dir */
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    /* borrowing import code, so need to set up the import variables */
    slapi_pblock_set(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &(mcfg->ldif2db_generate_uniqueid));
    slapi_pblock_set(pb, SLAPI_LDIF2DB_NAMESPACEID, &(mcfg->ldif2db_namespaceid));
    if (backend_plugin->plg_upgradednformat != NULL) {
        rc = (*backend_plugin->plg_upgradednformat)(pb);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_upgradednformat",
                      "No upgradednformat function defined for "
                      "%s\n",
                      backend_plugin->plg_name);
    }
    slapi_pblock_destroy(pb);
bail:
    return (rc);
}

/*
 * function to perform DB verify
 */
static int
slapd_exemode_dbverify(struct main_config *mcfg)
{
    int return_value = 0;
    struct slapdplugin *backend_plugin;

    /* this should be the first time to be called!  if the init order
     * is ever changed, these lines should be changed (or erased)!
     */
    mapping_tree_init();
    if ((backend_plugin = plugin_get_by_name("ldbm database")) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "slapd_exemode_dbverify",
                      "Could not find the ldbm backend plugin.\n");
        return 1;
    }

    /* check for slapi v2 support */
    if (!SLAPI_PLUGIN_IS_V2(backend_plugin)) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_dbverify",
                      "%s is too old to do dbverify.\n", backend_plugin->plg_name);
        return 1;
    }

    Slapi_PBlock *pb = slapi_pblock_new();
    slapi_pblock_set(pb, SLAPI_BACKEND, NULL);
    slapi_pblock_set(pb, SLAPI_PLUGIN, backend_plugin);
    slapi_pblock_set(pb, SLAPI_SEQ_TYPE, &(mcfg->dbverify_verbose));
    slapi_pblock_set(pb, SLAPI_BACKEND_INSTANCE_NAME, mcfg->cmd_line_instance_names);
    int32_t task_flags = SLAPI_TASK_RUNNING_FROM_COMMANDLINE;
    slapi_pblock_set(pb, SLAPI_TASK_FLAGS, &task_flags);
    slapi_pblock_set(pb, SLAPI_DBVERIFY_DBDIR, mcfg->dbverify_dbdir);

    if (backend_plugin->plg_dbverify != NULL) {
        return_value = (*backend_plugin->plg_dbverify)(pb);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_exemode_dbverify",
                      "No db verify function defined for "
                      "%s\n",
                      backend_plugin->plg_name);
        return_value = -1;
    }
    charray_free(mcfg->cmd_line_instance_names);
    slapi_pblock_destroy(pb);

    return (return_value);
}


#ifdef LDAP_ERROR_LOGGING
/*
 * Table to associate a string with a debug level.
 */
static struct slapd_debug_level_entry
{
    int dle_level;          /* LDAP_DEBUG_XXX value */
    const char *dle_string; /* string equivalent; NULL marks end of list */
    char dle_hide;
} slapd_debug_level_map[] = {
    {LDAP_DEBUG_TRACE, "trace", 0},
    {LDAP_DEBUG_PACKETS, "packets", 0},
    {LDAP_DEBUG_ARGS, "arguments", 0},
    {LDAP_DEBUG_ARGS, "args", 1},
    {LDAP_DEBUG_CONNS, "connections", 0},
    {LDAP_DEBUG_CONNS, "conn", 1},
    {LDAP_DEBUG_CONNS, "conns", 1},
    {LDAP_DEBUG_BER, "ber", 0},
    {LDAP_DEBUG_FILTER, "filters", 0},
    {LDAP_DEBUG_CONFIG, "config", 0},
    {LDAP_DEBUG_ACL, "accesscontrol", 0},
    {LDAP_DEBUG_ACL, "acl", 1},
    {LDAP_DEBUG_ACL, "acls", 1},
    {LDAP_DEBUG_STATS, "stats", 0},
    {LDAP_DEBUG_STATS2, "stats2", 0},
    {LDAP_DEBUG_SHELL, "shell", 1},
    {LDAP_DEBUG_PARSE, "parsing", 0},
    {LDAP_DEBUG_HOUSE, "housekeeping", 0},
    {LDAP_DEBUG_REPL, "replication", 0},
    {LDAP_DEBUG_REPL, "repl", 1},
    {LDAP_DEBUG_ANY, "errors", 0},
    {LDAP_DEBUG_ANY, "ANY", 1},
    {LDAP_DEBUG_ANY, "error", 1},
    {LDAP_DEBUG_CACHE, "caches", 0},
    {LDAP_DEBUG_CACHE, "cache", 1},
    {LDAP_DEBUG_PLUGIN, "plugins", 0},
    {LDAP_DEBUG_PLUGIN, "plugin", 1},
    {LDAP_DEBUG_TIMING, "timing", 0},
    {LDAP_DEBUG_ACLSUMMARY, "accesscontrolsummary", 0},
    {LDAP_DEBUG_BACKLDBM, "backend", 0},
    {LDAP_DEBUG_ALL_LEVELS, "ALL", 0},
    {0, NULL, 0}};


/*
 * Given a string representation of a debug level, map it to a integer value
 * and return that value.  -1 is returned upon error, with a message
 * printed to stderr.
 */
static int
slapd_debug_level_string2level(const char *s)
{
    int level, i;
    char *cur, *next, *scopy;

    level = 0;
    scopy = slapi_ch_strdup(s);

    for (cur = scopy; cur != NULL; cur = next) {
        if ((next = strchr(cur, '+')) != NULL) {
            *next++ = '\0';
        }

        if (isdigit(*cur)) {
            level |= atoi(cur);
        } else {
            for (i = 0; NULL != slapd_debug_level_map[i].dle_string; ++i) {
                if (strcasecmp(cur, slapd_debug_level_map[i].dle_string) == 0) {
                    level |= slapd_debug_level_map[i].dle_level;
                    break;
                }
            }

            if (NULL == slapd_debug_level_map[i].dle_string) {
                fprintf(stderr, "Unrecognized debug level \"%s\"\n", cur);
                slapi_ch_free_string(&scopy);
                return -1;
            }
        }
    }

    slapi_ch_free_string(&scopy);

    return level;
}


/*
 * Print to stderr the string equivalent of level.
 * The ANY level is omitted because it is always present.
 */
static void
slapd_debug_level_log(int level)
{
    int i, count, len;
    char *msg, *p;

    level &= ~LDAP_DEBUG_ANY;

    /* first pass: determine space needed for the debug level string */
    len = 1; /* room for '\0' terminator */
    count = 0;
    for (i = 0; NULL != slapd_debug_level_map[i].dle_string; ++i) {
        if (!slapd_debug_level_map[i].dle_hide &&
            slapd_debug_level_map[i].dle_level != LDAP_DEBUG_ALL_LEVELS && 0 != (level & slapd_debug_level_map[i].dle_level)) {
            if (count > 0) {
                ++len; /* room for '+' character */
            }
            len += strlen(slapd_debug_level_map[i].dle_string);
            ++count;
        }
    }

    /* second pass: construct the debug level string */
    p = msg = slapi_ch_calloc(1, len);
    count = 0;
    for (i = 0; NULL != slapd_debug_level_map[i].dle_string; ++i) {
        if (!slapd_debug_level_map[i].dle_hide &&
            slapd_debug_level_map[i].dle_level != LDAP_DEBUG_ALL_LEVELS && 0 != (level & slapd_debug_level_map[i].dle_level)) {
            if (count > 0) {
                *p++ = '+';
            }
            strcpy(p, slapd_debug_level_map[i].dle_string);
            p += strlen(p);
            ++count;
        }
    }

    slapi_log_err(SLAPI_LOG_INFO, SLAPD_VERSION_STR,
                  "%s: %s (%d)\n", "debug level", msg, level);
    slapi_ch_free((void **)&msg);
}


/*
 * Display usage/help for the debug level flag (-d)
 */
static void
slapd_debug_level_usage(void)
{
    int i;

    fprintf(stderr, "Debug levels:\n");
    for (i = 0; NULL != slapd_debug_level_map[i].dle_string; ++i) {
        if (!slapd_debug_level_map[i].dle_hide && slapd_debug_level_map[i].dle_level != LDAP_DEBUG_ALL_LEVELS) {
            fprintf(stderr, "    %6d - %s%s\n",
                    slapd_debug_level_map[i].dle_level,
                    slapd_debug_level_map[i].dle_string,
                    (0 == (slapd_debug_level_map[i].dle_level &
                           LDAP_DEBUG_ANY))
                        ? ""
                        : " (always logged)");
        }
    }
    fprintf(stderr, "To activate multiple levels, add the numeric"
                    " values together or separate the\n"
                    "values with a + character, e.g., all of the following"
                    " have the same effect:\n"
                    "    -d connections+filters\n"
                    "    -d 8+32\n"
                    "    -d 40\n");
}
#endif /* LDAP_ERROR_LOGGING */

static int
force_to_disable_security(const char *what, int *init_ssl, daemon_ports_t *ports_info)
{
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    errorbuf[0] = '\0';

    slapi_log_err(SLAPI_LOG_ERR, "force_to_disable_security",
                  "ERROR: %s Initialization Failed.  Disabling %s.\n", what, what);
    ports_info->s_socket = SLAPD_INVALID_SOCKET;
    ports_info->s_port = 0;
    *init_ssl = 0;
    if (config_set_security(CONFIG_SECURITY_ATTRIBUTE, "off", errorbuf, 1)) {
        slapi_log_err(SLAPI_LOG_ERR, "force_to_disable_security",
                      "ERROR: Failed to disable %s: \"%s\".\n",
                      CONFIG_SECURITY_ATTRIBUTE, errorbuf[0] ? errorbuf : "no error message");
        return 1;
    }
    return 0;
}

/*
  This function does all NSS and SSL related initialization
  required during startup.  We use this function rather
  than just call this code from main because we must perform
  all of this initialization after the fork() but before
  we detach from the controlling terminal.  This is because
  the NSS softokn requires that NSS_Init is called after the
  fork - this was always the case, but it is a hard error in
  NSS 3.11.99 and later.  We also have to call NSS_Init before
  doing the detach because NSS may prompt the user for the
  token (h/w or softokn) password on stdin.  So we use this
  function that we can call from detach() if running in
  regular slapd exemode or from main() if running in other
  modes (or just not detaching).
*/
int
slapd_do_all_nss_ssl_init(int slapd_exemode, int importexport_encrypt, int s_port, daemon_ports_t *ports_info)
{
    /*
     * Initialise NSS once for the whole slapd process, whether SSL
     * is enabled or not. We use NSS for random number generation and
     * other things even if we are not going to accept SSL connections.
     * We also need NSS for attribute encryption/decryption on import and export.
     *
     * It's important to remember that while in FIPS mode the administrator should always enable
     * the security, otherwise we don't call slapd_pk11_authenticate which is a requirement for FIPS mode
     */
    PRBool isFIPS = slapd_pk11_isFIPS();
    int init_ssl = config_get_security();

    if (isFIPS && !init_ssl) {
        slapi_log_err(SLAPI_LOG_WARNING, "slapd_do_all_nss_ssl_init",
                      "ERROR: TLS is not enabled, and the machine is in FIPS mode. "
                      "Some functionality won't work correctly (for example, "
                      "users with PBKDF2-SHA256 password scheme won't be able to log in). "
                      "It's highly advisable to enable TLS on this instance.\n");
    }

    if (slapd_exemode == SLAPD_EXEMODE_SLAPD) {
        init_ssl = init_ssl && (0 != s_port) && (s_port <= LDAP_PORT_MAX);
    } else {
        init_ssl = init_ssl && importexport_encrypt;
    }
    /* As of DS 6.1, always do a full initialization so that other
     * modules can assume NSS is available
     */
    if (slapd_nss_init((slapd_exemode == SLAPD_EXEMODE_SLAPD),
                       (slapd_exemode != SLAPD_EXEMODE_REFERRAL) /* have config? */)) {
        if (force_to_disable_security("NSS", &init_ssl, ports_info)) {
            return 1;
        }
    }

    if (slapd_exemode == SLAPD_EXEMODE_SLAPD) {
        client_auth_init();
    }

    if (init_ssl && slapd_ssl_init()) {
        if (force_to_disable_security("SSL", &init_ssl, ports_info)) {
            return 1;
        }
    }

    if ((slapd_exemode == SLAPD_EXEMODE_SLAPD) ||
        (slapd_exemode == SLAPD_EXEMODE_REFERRAL)) {
        if (init_ssl) {
            PRFileDesc **sock;
            for (sock = ports_info->s_socket; sock && *sock; sock++) {
                if (slapd_ssl_init2(sock, 0)) {
                    if (force_to_disable_security("SSL2", &init_ssl, ports_info)) {
                        return 1;
                    }
                }
            }
        }
    }

    return 0;
}
