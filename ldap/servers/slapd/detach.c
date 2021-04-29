/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Copyright (c) 1990, 1994 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <sys/types.h>
#ifdef SVR4
#include <sys/stat.h>
#endif /* svr4 */
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <signal.h>
#ifdef LINUX
#undef CTIME
#endif
#include "slap.h"
#include "fe.h"

#if defined(USE_SYSCONF) || defined(LINUX)
#include <unistd.h>
#endif /* USE_SYSCONF */

static int
set_workingdir(void)
{
    int rc = 0;
    char *workingdir = config_get_workingdir();
    char *errorlog = 0;
    char *ptr = 0;
    extern char *config_get_errorlog(void);
    extern int config_set_workingdir(const char *attrname, char *value, char *errorbuf, int apply);
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];

    if (NULL == workingdir) {
        errorlog = config_get_errorlog();
        if (NULL == errorlog) {
            rc = chdir("/");
            if (0 == rc) {
                if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, "/", errorbuf, 1) == LDAP_OPERATIONS_ERROR) {
                    slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", "/");
            }
        } else {
            ptr = strrchr(errorlog, '/');
            if (ptr) {
                *ptr = '\0';
            }
            rc = chdir(errorlog);
            if (0 == rc) {
                if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, errorlog, errorbuf, 1) == LDAP_OPERATIONS_ERROR) {
                    slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
                    rc = chdir("/");
                    if (0 == rc) {
                        if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, "/", errorbuf, 1) == LDAP_OPERATIONS_ERROR) {
                            slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
                        }
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", "/");
                    }
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", errorlog);
                rc = chdir("/");
                if (0 == rc) {
                    if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, "/", errorbuf, 1) == LDAP_OPERATIONS_ERROR) {
                        slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
                    }
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", "/");
                }
            }
            slapi_ch_free_string(&errorlog);
        }
    } else {
        /* calling config_set_workingdir to check for validity of directory, don't apply */
        if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, workingdir, errorbuf, 0) == LDAP_OPERATIONS_ERROR) {
            slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
            rc = chdir("/");
            if (0 == rc) {
                if (config_set_workingdir(CONFIG_WORKINGDIR_ATTRIBUTE, "/", errorbuf, 1) == LDAP_OPERATIONS_ERROR) {
                    slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: set workingdir failed with \"%s\"\n", errorbuf);
                }
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", "/");
            }
        } else {
            rc = chdir(workingdir);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "set_workingdir", "detach: failed to chdir to %s\n", workingdir);
            }
        }
        slapi_ch_free_string(&workingdir);
    }
    return rc;
}

int
detach(int slapd_exemode, int importexport_encrypt, int s_port, daemon_ports_t *ports_info)
{
    int i, sd;
    char buf[50];

    if (should_detach) {
        for (i = 0; i < 5; i++) {
#if defined(sunos5)
            switch (fork1()) {
#else
            switch (fork()) {
#endif
            case -1:
                sleep(5);
                continue;

            case 0:
                break;

            default:
                _exit(0);
            }
            break;
        }

        /* call this right after the fork, but before closing stdin */
        if (slapd_do_all_nss_ssl_init(slapd_exemode, importexport_encrypt, s_port, ports_info)) {
            return 1;
        }

        if (set_workingdir()) {
            slapi_log_err(SLAPI_LOG_ERR, "detach", "set_workingdir failed.\n");
        }

        if ((sd = open("/dev/null", O_RDWR)) == -1) {
            perror("/dev/null");
            return 1;
        }
        (void)dup2(sd, 0);
        /* Lets ignore libaccess printf */
        (void)dup2(sd, 1);
        (void)dup2(sd, 2);
        close(sd);
#ifdef DEBUG
        /* But lets try to preserve other errors like loader undefined symbols */
		sprintf(buf, "/var/dirsrv/ns-slapd-%d-XXXXXX.stderr", getpid());
        if ((sd = mkstemps(buf, 7)) < 0) {
            /* Lets try /tmp (so that non root users may keep the stderr */
		    sprintf(buf, "/tmp/ns-slapd-%d-XXXXXX.stderr", getpid());
            sd = mkstemps(buf, 7);
        }
        if (sd >= 0) {
            (void)dup2(sd, 2);
            close(sd);
        }
#endif

#ifdef USE_SETSID
        setsid();
#else  /* USE_SETSID */
        if ((sd = open("/dev/tty", O_RDWR)) != -1) {
            (void)ioctl(sd, TIOCNOTTY, NULL);
            (void)close(sd);
        }
#endif /* USE_SETSID */

        g_set_detached(1);
    } else { /* not detaching - call nss/ssl init */

        if (slapd_do_all_nss_ssl_init(slapd_exemode, importexport_encrypt, s_port, ports_info)) {
            return 1;
        }
        if (set_workingdir()) {
            slapi_log_err(SLAPI_LOG_ERR, "detach", "set_workingdir failed 2.\n");
        }
    }

    (void)SIGNAL(SIGPIPE, SIG_IGN);
    return 0;
}

/*
 * close all open files except stdin/out/err
 */
void
close_all_files()
{
    int i, nbits;

#ifdef USE_SYSCONF
    nbits = sysconf(_SC_OPEN_MAX);
#else  /* USE_SYSCONF */
    nbits = getdtablesize();
#endif /* USE_SYSCONF */

    for (i = 3; i < nbits; i++) {
        close(i);
    }
}

static void
raise_process_fd_limits(void)
{
    struct rlimit rl, setrl;
    RLIM_TYPE curlim;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
    if (slapdFrontendConfig->maxdescriptors < 0) {
        return;
    }

    /*
     * Try to set our file descriptor limit.  Our basic strategy is:
     *    1) Try to set the soft limit and the hard limit if
         *         necessary to match our maxdescriptors value.
     *    2) If that fails and our soft limit is less than our hard
     *       limit, we try to raise it to match the hard.
     */
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        int oserr = errno;

        slapi_log_err(SLAPI_LOG_ERR, "raise_process_fd_limits",
                      "getrlimit of descriptor limit failed - error %d (%s)\n",
                      oserr, slapd_system_strerror(oserr));
        return;
    }

    if (rl.rlim_cur == slapdFrontendConfig->maxdescriptors) { /* already correct */
        return;
    }
    curlim = rl.rlim_cur;
    setrl = rl; /* struct copy */
    setrl.rlim_cur = slapdFrontendConfig->maxdescriptors;
    /* don't lower the hard limit as it's irreversible */
    if (setrl.rlim_cur > setrl.rlim_max) {
        setrl.rlim_max = setrl.rlim_cur;
    }
    if (setrlimit(RLIMIT_NOFILE, &setrl) != 0 && curlim < rl.rlim_max) {
        setrl = rl; /* struct copy */
        setrl.rlim_cur = setrl.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &setrl) != 0) {
            int oserr = errno;

            slapi_log_err(SLAPI_LOG_ERR, "raise_process_fd_limits",
                          "setrlimit of descriptor limit to %lu failed - error %d (%s)\n",
                          setrl.rlim_cur, oserr, slapd_system_strerror(oserr));
            return;
        }
    }

    (void)getrlimit(RLIMIT_NOFILE, &rl);
    slapi_log_err(SLAPI_LOG_TRACE, "raise_process_fd_limits",
                  "descriptor limit changed from %d to %lu\n", curlim, rl.rlim_cur);
}

/*
 * Try to raise relevant per-process limits
 */
void
raise_process_limits()
{
    struct rlimit rl;

    raise_process_fd_limits();

#ifdef RLIMIT_DATA
    if (getrlimit(RLIMIT_DATA, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_DATA, &rl) != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "raise_process_limits", "setrlimit(RLIMIT_DATA) failed %d\n",
                          errno);
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "raise_process_limits", "getrlimit(RLIMIT_DATA) failed %d\n",
                      errno);
    }
#endif

#ifdef RLIMIT_VMEM
    if (getrlimit(RLIMIT_VMEM, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_VMEM, &rl) != 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "raise_process_limits", "setrlimit(RLIMIT_VMEM) failed %d\n",
                          errno);
        }
    } else {
        slapi_log_err(SLAPI_LOG_TRACE, "raise_process_limits", "getrlimit(RLIMIT_VMEM) failed %d\n",
                      errno);
    }
#endif /* RLIMIT_VMEM */
}
