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

/* protect_db.c
 * Used to police access to the db.  Prevents different instances of
 * slapd from clobbering each other
 */



#ifndef _WIN32

#define LOCK_FILE   "lock"
#define IMPORT_DIR  "imports"
#define EXPORT_DIR  "exports"
#define SERVER_DIR  "server"
#define NUM_TRIES   20
#define WAIT_TIME   250 

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
#endif 

#include "protect_db.h"

#include "slap.h"


#ifndef _WIN32
/* This is the unix version of the code to protect the db. */

static int
grab_lockfile()
{
    pid_t pid, owning_pid;
    char lockfile[MAXPATHLEN];
    int fd, x;
    int removed_lockfile = 0;
    struct timeval t;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();


    /* Don't use anything that makes a NSPR call here (like LDAPDebug)! This function
       gets called by an atexit function, and NSPR is long gone by then. */

    /* Get the name of the lockfile */
    snprintf(lockfile, sizeof(lockfile), "%s/%s", slapdFrontendConfig->lockdir, LOCK_FILE);
    lockfile[sizeof(lockfile)-1] = (char)0;
    /* Get our pid */
    pid = getpid();

    /* Try to grab it */
    if ((fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0664)) != -1) {
        /* We got the lock, write our pid to the file */
        write(fd, (void *) &pid, sizeof(pid_t));
    close(fd);
        return 0;
    }
     
    /* We weren't able to get the lock.  Find out why. */
    if (errno != EEXIST) {
        /* Hmm, something happened that we weren't prepared to handle */
        fprintf(stderr, ERROR_ACCESSING_LOCKFILE, lockfile);
        return -1;
    } 

    while(1) {
        /* Try to grab the lockfile NUM_TRIES times waiting WAIT_TIME milliseconds after each try */
    t.tv_sec = 0;
    t.tv_usec = WAIT_TIME * 1000;
        for(x = 0; x < NUM_TRIES; x++) {
            if ((fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0664)) != -1) {
                /* Got the lock */
                write(fd, (void *) &pid, sizeof(pid_t));
        close(fd);
                return 0;
            }
            select(0, NULL, NULL, NULL, &t);
        }
        
        /* We still haven't got the lockfile.  Find out who owns it and see if they are still up */
        if ((fd = open(lockfile,  O_RDONLY)) != -1) {
            size_t nb_bytes=0;    

            nb_bytes = read(fd, (void *) &owning_pid, sizeof(pid_t));
            if ( (nb_bytes != (size_t)(sizeof(pid_t)) ) || (owning_pid == 0) || (kill(owning_pid, 0) != 0 && errno == ESRCH) ) {
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
release_lockfile()
{
    char lockfile[MAXPATHLEN];
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* This function assumes that the caller owns the lock, it doesn't check to make sure! */

    snprintf(lockfile, sizeof(lockfile), "%s/%s", slapdFrontendConfig->lockdir, LOCK_FILE);
    lockfile[sizeof(lockfile)-1] = (char)0;
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
    struct passwd* pw;
    struct stat stat_buffer;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* Make sure it exists */
    if (PR_MkDir(dir, 0755) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        if (prerr != PR_FILE_EXISTS_ERROR) {
            LDAPDebug(LDAP_DEBUG_ANY, FILE_CREATE_ERROR, dir, prerr, slapd_pr_strerror(prerr));
            return 1;
        }
    }

    /* Make sure it's owned by the correct user */
    if (slapdFrontendConfig->localuser != NULL) {
      if ( (pw = getpwnam(slapdFrontendConfig->localuser)) == NULL ) {
        LDAPDebug(LDAP_DEBUG_ANY, GETPWNAM_WARNING, slapdFrontendConfig->localuser, errno, strerror(errno));
      } else {
        if (chown(dir, pw->pw_uid, -1) == -1) {
            stat(dir, &stat_buffer);
            if (stat_buffer.st_uid != pw->pw_uid) {
                LDAPDebug(LDAP_DEBUG_ANY, CHOWN_WARNING, dir, 0, 0);
            }
        }
      } /* else */
    }

    return 0;
}
    
/* Creates a file in the directory 'dir_name' whose name is the 
 * pid for this process. 
 */
static void
add_this_process_to(char *dir_name)
{
    char file_name[MAXPATHLEN];
    struct passwd* pw;
    struct stat stat_buffer;
    PRFileDesc* prfd;
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    snprintf(file_name, sizeof(file_name), "%s/%d", dir_name, getpid());
    file_name[sizeof(file_name)-1] = (char)0;
    
    if ((prfd = PR_Open(file_name, PR_RDWR | PR_CREATE_FILE, 0666)) == NULL) {
    LDAPDebug(LDAP_DEBUG_ANY, FILE_CREATE_WARNING, file_name, 0, 0);
    return;
    }
    
    /* Make sure the owner is of the file is the user the server
     * runs as. */
    if (slapdFrontendConfig->localuser != NULL) {
      if ( (pw = getpwnam(slapdFrontendConfig->localuser)) == NULL ) {
    LDAPDebug(LDAP_DEBUG_ANY, GETPWNAM_WARNING, slapdFrontendConfig->localuser, errno, strerror(errno));
      } else {
        if (chown(file_name, pw->pw_uid, -1) == -1) {
            stat(file_name, &stat_buffer);
            if (stat_buffer.st_uid != pw->pw_uid) {
                LDAPDebug(LDAP_DEBUG_ANY, CHOWN_WARNING, file_name, 0, 0);
            }
        }
      } /* else */
    }
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

    while((entry = PR_ReadDir(dir, PR_SKIP_BOTH)) != NULL) {
        pid = (pid_t) strtol(entry->name, &endp, 0);
        if (*endp != '\0') {
            /* not quite sure what this file was, but we 
             * didn't put it there */
            continue;
        }
        if (is_process_up(pid)) {
            result = (long) pid;
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

    while((entry = readdir(dir)) != NULL) {
    
        /* skip dot and dot-dot */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        pid = (pid_t) strtol(entry->d_name, &endp, 0);
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
remove_slapd_process()
{
    char import_dir[MAXPATHLEN];
    char export_dir[MAXPATHLEN];
    char server_dir[MAXPATHLEN];
    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    /* Create the name of the directories that hold the pids of the currently running 
     * ns-slapd processes */
    snprintf(import_dir, sizeof(import_dir), "%s/%s", slapdFrontendConfig->lockdir, IMPORT_DIR);
    import_dir[sizeof(import_dir)-1] = (char)0;
    snprintf(export_dir, sizeof(export_dir), "%s/%s", slapdFrontendConfig->lockdir, EXPORT_DIR);
    export_dir[sizeof(export_dir)-1] = (char)0;
    snprintf(server_dir, sizeof(server_dir), "%s/%s", slapdFrontendConfig->lockdir, SERVER_DIR);
    server_dir[sizeof(server_dir)-1] = (char)0;

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
    import_dir[sizeof(import_dir)-1] = (char)0;
    snprintf(export_dir, sizeof(export_dir), "%s/%s", slapdFrontendConfig->lockdir, EXPORT_DIR);
    export_dir[sizeof(export_dir)-1] = (char)0;
    snprintf(server_dir, sizeof(server_dir), "%s/%s", slapdFrontendConfig->lockdir, SERVER_DIR);
    server_dir[sizeof(server_dir)-1] = (char)0;

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
        LDAPDebug(LDAP_DEBUG_ANY, NO_SERVER_DUE_TO_SERVER, running, 0, 0);
    } else if (importing) {
        result = -1;
        LDAPDebug(LDAP_DEBUG_ANY, NO_SERVER_DUE_TO_IMPORT, importing, 0, 0);
    } else {
        add_this_process_to(server_dir);
        result = 0;
    }
    break;
    case SLAPD_EXEMODE_DB2LDIF:
    if (r_flag)  {
        /* When the -r flag is used in db2ldif we need to make sure 
         * we get a consistent snapshot of the server.  As a result
         * it needs to run by itself, so no other slapd process can
         * change the database while it is running. */
        if (running || importing) {
            LDAPDebug(LDAP_DEBUG_ANY, NO_DB2LDIFR_DUE_TO_USE, 0, 0, 0);
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
            LDAPDebug(LDAP_DEBUG_ANY, NO_DB2LDIF_DUE_TO_IMPORT, importing, 0, 0);
            result = -1;        
        } else {
            add_this_process_to(export_dir);
            result = 0;
        }
    }
    break;
    case SLAPD_EXEMODE_DB2ARCHIVE:
    if (importing) {
        LDAPDebug(LDAP_DEBUG_ANY, NO_DB2BAK_DUE_TO_IMPORT, importing, 0, 0);        
        result = -1;
    } else {
        add_this_process_to(export_dir);
        result = 0;
    }
    break;
    case SLAPD_EXEMODE_ARCHIVE2DB:
    case SLAPD_EXEMODE_LDIF2DB:
    if (running || importing || exporting) {
        LDAPDebug(LDAP_DEBUG_ANY, NO_IMPORT_DUE_TO_USE, 0, 0, 0);
        result = -1;
    } else {
        add_this_process_to(import_dir);
        result = 0;
    }
    break;
    case SLAPD_EXEMODE_DB2INDEX:
        if (running || importing || exporting) {
            LDAPDebug(LDAP_DEBUG_ANY, NO_DB2INDEX_DUE_TO_USE, 0, 0, 0);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_UPGRADEDB:
        if (running || importing || exporting) {
            LDAPDebug(LDAP_DEBUG_ANY, NO_UPGRADEDB_DUE_TO_USE, 0, 0, 0);
            result = -1;
        } else {
            add_this_process_to(import_dir);
            result = 0;
        }
        break;
    case SLAPD_EXEMODE_DBTEST:
        if (running || importing || exporting) {
            LDAPDebug(LDAP_DEBUG_ANY, NO_DBTEST_DUE_TO_USE, 0, 0, 0);
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
is_slapd_running() {
  char server_dir[MAXPATHLEN];
  slapdFrontendConfig_t *cfg = getFrontendConfig();
  int running = 0;
  
  snprintf(server_dir, sizeof(server_dir), "%s/%s", cfg->lockdir, SERVER_DIR);
  server_dir[sizeof(server_dir)-1] = (char)0;
  
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

#else /* _WIN32 */

/* The NT version of this code */

/* Returns 1 if the mutex named 'mutexName' otherwise it
 * returns 0
 */
int
mutex_exists( char *mutexName )
{
    if ( OpenMutex( SYNCHRONIZE, FALSE, mutexName ) == NULL ) {
        return( 0 );
    } else {
        return( 1 );
    }        
}

/* is_slapd_running(): 
 * returns 1 if slapd is running, 0 if not 
 */

int
is_slapd_running() {
  char mutexName[ MAXPATHLEN + 1 ];
  char serverMutexName[ MAXPATHLEN + 1 ];
  int result = 0;
  slapdFrontendConfig_t *cfg = getFrontendConfig();

  strncpy( mutexName, cfg->lockdir, MAXPATHLEN );
  strncpy( serverMutexName, cfg->lockdir, MAXPATHLEN );
  mutexName[ MAXPATHLEN ] = '\0';

  serverMutexName[ MAXPATHLEN ] = '\0';
  strcat( serverMutexName, "/server" );
    
  return mutex_exists ( serverMutexName );
}

static void fix_mutex_name(char *name)
{
    /* On NT mutex names cannot contain the '\' character.
     * This functions replaces '\' with '/' in the supplied
     * name. */
    int x;

    for (x = 0; name[x] != '\0'; x++) {
        if ('\\' == name[x]) {
            name[x] = '/';
        }
    }
}

/*
 * We retain any opened handle to the mutex we create here,
 * to use when we shutdown, to delete the mutex prior to 
 * signaling that we've exited.
 */
static HANDLE open_mutex = NULL;

/* 
 * Call this to clean up the locks, before signaling
 * that the server is down.
 */
void
remove_slapd_process()
{
    if (open_mutex) {
        CloseHandle(open_mutex);
    }
}

/* This function makes sure different instances of slapd don't
 * run in conflicting modes at the same time. The WIN32 version
 * uses mutexes as names that the kernel handles.  Basically there
 * is a server mutex, and import mutex, and an export mutex.  If
 * the server mutex exists, then the server is running.  If the 
 * import mutex exists, then either ldif2db or bak2db is running.
 * If the export mutex exists, then one or more of db2ldif or db2bak
 * are running.  There is also a mutex that is actually locked when
 * checking the existence of the other mutexes. The OS will
 * automatically remove a mutex if no process has a handle on it.
 * returns a 0 if it is ok for the process to run 
 * returns a -1 if the process can't run do to a conflict with other
 * slapd processes
 */
int
add_new_slapd_process(int exec_mode, int r_flag, int skip_flag)
{
    char mutexName[ MAXPATHLEN + 1 ];
    char serverMutexName[ MAXPATHLEN + 1 ];
    char importMutexName[ MAXPATHLEN + 1 ];
    char exportMutexName[ MAXPATHLEN + 1 ];

    HANDLE mutex;
    SECURITY_ATTRIBUTES mutexAttributes;
    PSECURITY_DESCRIPTOR pSD;
    LPVOID lpMsgBuf;

    int result = 0;

    slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();

    if (skip_flag) {
        return 0;
    }

    /* Create the names for the mutexes */
    PL_strncpyz(mutexName, slapdFrontendConfig->lockdir, sizeof(mutexName));

    /* Make sure the name of the mutex is legal. */
    fix_mutex_name(mutexName);

    PR_snprintf(serverMutexName, sizeof(serverMutexName), "%s/server", mutexName);
    PR_snprintf(importMutexName, sizeof(importMutexName), "%s/import", mutexName);
    PR_snprintf(exportMutexName, sizeof(exportMutexName), "%s/export", mutexName);
    
    /* Fill in the security crap for the mutex */
    pSD = (PSECURITY_DESCRIPTOR)slapi_ch_malloc( sizeof( SECURITY_DESCRIPTOR ) );
    InitializeSecurityDescriptor( pSD, SECURITY_DESCRIPTOR_REVISION );
    SetSecurityDescriptorDacl( pSD, TRUE, NULL, FALSE );
    mutexAttributes.nLength = sizeof( mutexAttributes );
    mutexAttributes.lpSecurityDescriptor = pSD;
    mutexAttributes.bInheritHandle = FALSE;
    
    /* Get a handle to the main mutex */
    if ( ( mutex = CreateMutex( &mutexAttributes, FALSE, mutexName ) ) == NULL ) {
        FormatMessage( 
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
            (LPTSTR) &lpMsgBuf,
            0,
            NULL 
        );

        LDAPDebug( LDAP_DEBUG_ANY, CREATE_MUTEX_ERROR, lpMsgBuf, 0, 0 );
        LocalFree( lpMsgBuf );
        exit( 1 );
    }
    
    /* Lock the main mutex */
    if ( WaitForSingleObject( mutex, INFINITE ) == WAIT_FAILED ) {
        FormatMessage( 
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
            (LPTSTR) &lpMsgBuf,
            0,
            NULL 
        );

        LDAPDebug( LDAP_DEBUG_ANY, WAIT_ERROR, lpMsgBuf, 0, 0 );
        LocalFree( lpMsgBuf );
        exit( 1 );
    }


    switch (exec_mode) {
        case SLAPD_EXEMODE_SLAPD:
            if ( mutex_exists( serverMutexName ) ||
                 mutex_exists( importMutexName ) )  {
                LDAPDebug( LDAP_DEBUG_ANY, NO_SERVER_DUE_TO_USE, 0, 0, 0);
                result = -1;
            } else {
                open_mutex = CreateMutex( &mutexAttributes, FALSE, serverMutexName );
                result = 0;
            }
            break;
        case SLAPD_EXEMODE_DB2LDIF:
            if (r_flag)  {
                /* When the -r flag is used in db2ldif we need to make sure 
                 * we get a consistent snapshot of the server.  As a result
                 * it needs to run by itself, so no other slapd process can
                 * change the database while it is running. */
                if ( mutex_exists( serverMutexName ) ||
                     mutex_exists( importMutexName ) ||
                     mutex_exists( exportMutexName ) ) {
                    LDAPDebug(LDAP_DEBUG_ANY, NO_DB2LDIFR_DUE_TO_USE, 0, 0, 0);
                    result = -1;
                } else {
                    CreateMutex( &mutexAttributes, FALSE, exportMutexName );
                    result = 0;
                }
                break;
            }
        case SLAPD_EXEMODE_DB2ARCHIVE:
            if ( mutex_exists( importMutexName ) )  {
                LDAPDebug(LDAP_DEBUG_ANY, NO_EXPORT_DUE_TO_IMPORT, 0, 0, 0);
                result = -1;
            } else {
                CreateMutex( &mutexAttributes, FALSE, exportMutexName );
                result = 0;
            }
            break;
        case SLAPD_EXEMODE_ARCHIVE2DB:
        case SLAPD_EXEMODE_LDIF2DB:
            if ( mutex_exists( serverMutexName ) ||
                 mutex_exists( importMutexName ) ||
                 mutex_exists( exportMutexName ) ) {
                    LDAPDebug(LDAP_DEBUG_ANY, NO_IMPORT_DUE_TO_USE, 0, 0, 0);
                result = -1;
            } else {
                CreateMutex( &mutexAttributes, FALSE, importMutexName );
                result = 0;
            }
            break;
        case SLAPD_EXEMODE_UPGRADEDB:
            if ( mutex_exists( serverMutexName ) ||
                 mutex_exists( importMutexName ) ||
                 mutex_exists( exportMutexName ) ) {
                    LDAPDebug(LDAP_DEBUG_ANY, NO_UPGRADEDB_DUE_TO_USE, 0, 0, 0);
                result = -1;
            } else {
                CreateMutex( &mutexAttributes, FALSE, importMutexName );
                result = 0;
            }
            break;
        case SLAPD_EXEMODE_DBTEST:
            if ( mutex_exists( serverMutexName ) ||
                 mutex_exists( importMutexName ) ||
                 mutex_exists( exportMutexName ) ) {
                    LDAPDebug(LDAP_DEBUG_ANY, NO_DBTEST_DUE_TO_USE, 0, 0, 0);
                result = -1;
            } else {
                CreateMutex( &mutexAttributes, FALSE, importMutexName );
                result = 0;
            }
            break;
    }
    
    /* release the main mutex */
    ReleaseMutex( mutex );

    slapi_ch_free((void**)&pSD );

    return( result );
}
#endif /* _WIN32 */    
    
