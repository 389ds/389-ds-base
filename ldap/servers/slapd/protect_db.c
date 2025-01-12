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

/* protect_db.c
 * Used to police access to the db.  Prevents different instances of
 * slapd from clobbering each other
 */

#define LOCK_FILE "lock"
#define IMPORT_DIR "imports"
#define EXPORT_DIR "exports"
#define SERVER_DIR "server"
#define NUM_TRIES 20
#define WAIT_TIME 250

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h> /* open */
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/param.h> /* MAXPATHLEN */
#include <dirent.h>
#include <pwd.h>

#include "protect_db.h"
#include "slap.h"

static int
grab_lockfile(void)
{
    pid_t pid, owning_pid;
    char lockfile[MAXPATHLEN];
    int fd, x, rc;
    int removed_lockfile = 0;
    struct timeval t;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();


    /* Don't use anything that makes a NSPR call here (like LDAPDebug)! This function
       gets called by an atexit function, and NSPR is long gone by then. */

    /* Get the name of the lockfile */
    snprintf(lockfile, sizeof(lockfile), "%s/%s", slapdFrontendConfig->lockdir, LOCK_FILE);
    lockfile[sizeof(lockfile) - 1] = (char)0;
    /* Get our pid */
    pid = getpid();

    /* Try to grab it */
    if ((fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0644)) != -1) {
        /* We got the lock, write our pid to the file */
        rc = write(fd, (void *)&pid, sizeof(pid_t));
        close(fd);
        if (rc < 0) {
            fprintf(stderr, ERROR_WRITING_LOCKFILE, lockfile);
            return rc;
        }
        return 0;
    }

    /* We weren't able to get the lock.  Find out why. */
    if (errno != EEXIST) {
        /* Hmm, something happened that we weren't prepared to handle */
        fprintf(stderr, ERROR_ACCESSING_LOCKFILE, lockfile);
        return -1;
    }

    while (1) {
        /* Try to grab the lockfile NUM_TRIES times waiting WAIT_TIME milliseconds after each try */
        t.tv_sec = 0;
        t.tv_usec = WAIT_TIME * 1000;
        for (x = 0; x < NUM_TRIES; x++) {
            if ((fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0644)) != -1) {
                /* Got the lock */
                rc = write(fd, (void *)&pid, sizeof(pid_t));
                close(fd);
                if (rc < 0) {
                    fprintf(stderr, ERROR_WRITING_LOCKFILE, lockfile);
                    return rc;
                }
                return 0;
            }
            select(0, NULL, NULL, NULL, &t);
        }

        /* We still haven't got the lockfile.  Find out who owns it and see if they are still up */
        if ((fd = open(lockfile, O_RDONLY)) != -1) {
            size_t nb_bytes = 0;

            nb_bytes = read(fd, (void *)&owning_pid, sizeof(pid_t));
            close(fd);
            if ((nb_bytes != (size_t)(sizeof(pid_t))) || (owning_pid == 0) || (kill(owning_pid, 0) != 0 && errno == ESRCH)) {
                /* The process that owns the lock is dead. Try to remove the old lockfile. */
                if (unlink(lockfile) != 0) {
                    /* Unable to remove the stale lockfile. */
                    fprintf(stderr, LOCKFILE_DEAD_OWNER, lockfile, owning_pid);
                    return -1;
                }
                if (removed_lockfile) {
                    /* We already removed the lockfile once. Best thing to do is give up. */
                    /* I'm not sure what should be given as an error here */
                    fprintf(stderr, UNABLE_TO_GET_LOCKFILE, lockfile);
                    return -1;
                } else {
                    removed_lockfile = 1;
                }
            } else {
                /* It looks like the owner of the lockfile is still up and running. */
                fprintf(stderr, LOCKFILE_ALREADY_OWNED, owning_pid);
                return -1;
            }
        }
    }
}

static void
release_lockfile(void)
{
    char lockfile[MAXPATHLEN];
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* This function assumes that the caller owns the lock, it doesn't check to make sure! */

    snprintf(lockfile, sizeof(lockfile), "%s/%s", slapdFrontendConfig->lockdir, LOCK_FILE);
    lockfile[sizeof(lockfile) - 1] = (char)0;
    unlink(lockfile);
}


/* Takes the pid of a process.  Returns 1 if the process seems
 * to be up, otherwise it returns 0.
 */
static int
is_process_up(pid_t pid)
{
    if (kill(pid, 0) == -1 && errno == ESRCH) {
        return 0;
    } else {
        return 1;
    }
}

/* Make sure the directory 'dir' exists and is owned bye the user
 * the server runs as. Returns 0 if everything is ok.
 */
static int
make_sure_dir_exists(char *dir)
{
    struct passwd *pw;
    struct stat stat_buffer;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* Make sure it exists */
    if (PR_MkDir(dir, 0755) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (prerr != PR_FILE_EXISTS_ERROR) {
            slapi_log_err(SLAPI_LOG_ERR, "make_sure_dir_exists",
                          FILE_CREATE_ERROR, dir, prerr, slapd_pr_strerror(prerr));
            return 1;
        }
    }

    /* Make sure it's owned by the correct user */
    if (slapdFrontendConfig->localuser != NULL &&
        slapdFrontendConfig->localuserinfo != NULL) {
        pw = slapdFrontendConfig->localuserinfo;
        if (chown(dir, pw->pw_uid, -1) == -1) {
            if ((stat(dir, &stat_buffer) == 0) && (stat_buffer.st_uid != pw->pw_uid)) {
                slapi_log_err(SLAPI_LOG_WARNING, "make_sure_dir_exists", CHOWN_WARNING, dir);
                return 1;
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "make_sure_dir_exists", STAT_ERROR, dir, errno);
                return 1;
            }
        }
    }

    return 0;
}

/* Creates a file in the directory 'dir_name' whose name is the
 * pid for this process.
 */
static void
add_this_process_to(char *dir_name)
{
    char *file_name;
    struct passwd *pw;
    struct stat stat_buffer;
    PRFileDesc *prfd;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    file_name = PR_smprintf("%s/%d", dir_name, getpid());
    if ((prfd = PR_Open(file_name, PR_RDWR | PR_CREATE_FILE, 0644)) == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING, "add_this_process_to", FILE_CREATE_WARNING, file_name);
        slapi_ch_free_string(&file_name);
        return;
    }

    /* Make sure the owner is of the file is the user the server
     * runs as. */
    if (slapdFrontendConfig->localuser != NULL &&
        slapdFrontendConfig->localuserinfo != NULL) {
        pw = slapdFrontendConfig->localuserinfo;
        if (chown(file_name, pw->pw_uid, -1) == -1) {
            if ((stat(file_name, &stat_buffer) == 0) && (stat_buffer.st_uid != pw->pw_uid)) {
                slapi_log_err(SLAPI_LOG_WARNING, "add_this_process_to", CHOWN_WARNING, file_name);
            }
        }
    }
    slapi_ch_free_string(&file_name);
    PR_Close(prfd);
}


/* The directory 'dir_name' is expected to contain files whose
 * names are the pid of running slapd processes.  This
 * function will check each entry in the directory and remove
 * any files that represent processes that don't exist.
 * Returns 0 if there are no processes represented, or
 * the pid of one of the processes.
 */
static long
sample_and_update(char *dir_name)
{
    PRDir *dir;
    PRDirEntry *entry;
    pid_t pid;
    long result = 0;
    char *endp;
    char file_name[MAXPATHLEN];

    if ((dir = PR_OpenDir(dir_name)) == NULL) {
        return 0;
    }

    while ((entry = PR_ReadDir(dir, PR_SKIP_BOTH)) != NULL) {
        pid = (pid_t)strtol(entry->name, &endp, 0);
        if (*endp != '\0') {
            /* not quite sure what this file was, but we
             * didn't put it there */
            continue;
        }
        if (pid == getpid()) {
            /*
             * We have re-used our pid number, and we are now checking for ourself!
             *
             * GitHub: https://github.com/389ds/389-ds-base/issues/4042
             *
             * This situation is common in containers, where the process name space means we
             * may be checking ourself, and have low pids that get re-used. Worse, we cant
             * actually check the pid of any other instance in a different container.
             * So at the very least in THIS case, we ignore it, since we are the pid
             * that has the lock, and it's probably a left over from a bad startup.
             */
        } else if (is_process_up(pid)) {
            result = (long)pid;
        } else {
            PR_snprintf(file_name, MAXPATHLEN, "%s/%s", dir_name, entry->name);
            PR_Delete(file_name);
        }
    }
    PR_CloseDir(dir);
    return result;
}


/* Removes any stale pid entries and the pid entry for this
 * process from the directory 'dir_name'.
 */
static void
remove_and_update(char *dir_name)
{
    /* since this is called from an atexit function, we can't use
     * NSPR. */
    DIR *dir;
    struct dirent *entry;
    pid_t pid;
    pid_t our_pid;
    char *endp;
    char file_name[MAXPATHLEN];

    /* get our pid */
    our_pid = getpid();

    if ((dir = opendir(dir_name)) == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {

        /* skip dot and dot-dot */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        pid = (pid_t)strtol(entry->d_name, &endp, 0);
        if (*endp != '\0') {
            /* not quite sure what this file was, but we
             * didn't put it there */
            continue;
        }
        if (!is_process_up(pid) || pid == our_pid) {
            PR_snprintf(file_name, sizeof(file_name), "%s/%s", dir_name, entry->d_name);
            unlink(file_name);
        }
    }
    closedir(dir);
}


/* Walks through all the pid directories and clears any stale
 * pids.  It also removes the files for this process.
 */
void
remove_slapd_process(void)
{
    char import_dir[MAXPATHLEN];
    char export_dir[MAXPATHLEN];
    char server_dir[MAXPATHLEN];
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* Create the name of the directories that hold the pids of the currently running
     * ns-slapd processes */
    snprintf(import_dir, sizeof(import_dir), "%s/%s", slapdFrontendConfig->lockdir, IMPORT_DIR);
    import_dir[sizeof(import_dir) - 1] = (char)0;
    snprintf(export_dir, sizeof(export_dir), "%s/%s", slapdFrontendConfig->lockdir, EXPORT_DIR);
    export_dir[sizeof(export_dir) - 1] = (char)0;
    snprintf(server_dir, sizeof(server_dir), "%s/%s", slapdFrontendConfig->lockdir, SERVER_DIR);
    server_dir[sizeof(server_dir) - 1] = (char)0;

    /* Grab the lockfile */
    if (grab_lockfile() != 0) {
        /* Unable to grab the lockfile */
        return;
    }

    remove_and_update(import_dir);
    remove_and_update(export_dir);
    remove_and_update(server_dir);

    release_lockfile();
}

int
add_new_slapd_process(int exec_mode, int r_flag, int skip_flag)
{
    char import_dir[MAXPATHLEN];
    char export_dir[MAXPATHLEN];
    char server_dir[MAXPATHLEN];
    int running, importing, exporting;
    int result = 0;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (skip_flag) {
        return 0;
    }

    /* Create the name of the directories that hold the pids of the currently running
     * ns-slapd processes */
    snprintf(import_dir, sizeof(import_dir), "%s/%s", slapdFrontendConfig->lockdir, IMPORT_DIR);
    import_dir[sizeof(import_dir) - 1] = (char)0;
    snprintf(export_dir, sizeof(export_dir), "%s/%s", slapdFrontendConfig->lockdir, EXPORT_DIR);
    export_dir[sizeof(export_dir) - 1] = (char)0;
    snprintf(server_dir, sizeof(server_dir), "%s/%s", slapdFrontendConfig->lockdir, SERVER_DIR);
    server_dir[sizeof(server_dir) - 1] = (char)0;

    /* Grab the lockfile */
    if (grab_lockfile() != 0) {
        /* Unable to grab the lockfile */
        return -1;
    }

    /* Make sure the directories exist */
    if (make_sure_dir_exists(slapdFrontendConfig->lockdir) != 0 ||
        make_sure_dir_exists(import_dir) != 0 ||
        make_sure_dir_exists(export_dir) != 0 ||
        make_sure_dir_exists(server_dir) != 0) {
        release_lockfile();
        return -1;
    }

    /* Go through the directories and find out what's going on.
     * Clear any stale pids encountered. */
    importing = sample_and_update(import_dir);
    exporting = sample_and_update(export_dir);
    running = sample_and_update(server_dir);

    switch (exec_mode) {
    case SLAPD_EXEMODE_SLAPD:
        if (running) {
            result = -1;
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_SERVER_DUE_TO_SERVER, running);
        } else if (importing) {
            result = -1;
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_SERVER_DUE_TO_IMPORT, importing);
        } else {
            add_this_process_to(server_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_DB2LDIF:
        if (r_flag) {
            /* When the -r flag is used in db2ldif we need to make sure
         * we get a consistent snapshot of the server.  As a result
         * it needs to run by itself, so no other slapd process can
         * change the database while it is running. */
            if (running || importing) {
                slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_DB2LDIFR_DUE_TO_USE);
                result = -1;
            } else {
                /* Even though this is really going to export code, we will
             * but it in the importing dir so no other process can change
             * things while we are doing ldif2db with the -r flag. */
                add_this_process_to(import_dir);
                result = 0;
            }
        } else {
            if (importing) {
                slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_DB2LDIF_DUE_TO_IMPORT, importing);
                result = -1;
            } else {
                add_this_process_to(export_dir);
                result = 0;
            }
        }
        break;
    case SLAPD_EXEMODE_DB2ARCHIVE:
        if (importing) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_DB2BAK_DUE_TO_IMPORT, importing);
            result = -1;
        } else {
            add_this_process_to(export_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_ARCHIVE2DB:
    case SLAPD_EXEMODE_LDIF2DB:
        if (running || importing || exporting) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_IMPORT_DUE_TO_USE);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_DB2INDEX:
        if (running || importing || exporting) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_DB2INDEX_DUE_TO_USE);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_UPGRADEDB:
        if (running || importing || exporting) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_UPGRADEDB_DUE_TO_USE);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_UPGRADEDNFORMAT:
        if (running || importing || exporting) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_UPGRADEDNFORMAT_DUE_TO_USE);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_DBTEST:
        if (running || importing || exporting) {
            slapi_log_err(SLAPI_LOG_ERR, "add_new_slapd_process", NO_DBTEST_DUE_TO_USE);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    }

    release_lockfile();

    if (result == 0) {
        atexit(remove_slapd_process);
    }

    return result;
}


/* is_slapd_running()
 * returns 1 if slapd is running, 0 if not, -1 on error
 */
int
is_slapd_running(void)
{
    char server_dir[MAXPATHLEN];
    slapdFrontendConfig_t *cfg = getFrontendConfig();
    int running = 0;

    snprintf(server_dir, sizeof(server_dir), "%s/%s", cfg->lockdir, SERVER_DIR);
    server_dir[sizeof(server_dir) - 1] = (char)0;

    /* Grab the lockfile */
    if (grab_lockfile() != 0) {
        /* Unable to grab the lockfile */
        return -1;
    }

    /* Make sure the directories exist */
    if (make_sure_dir_exists(cfg->lockdir) != 0 ||
        make_sure_dir_exists(server_dir) != 0) {
        release_lockfile();
        return -1;
    }

    running = sample_and_update(server_dir);
    release_lockfile();
    return running;
}
