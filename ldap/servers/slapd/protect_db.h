/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* Header file for protect_db.c */

int add_new_slapd_process(int exec_mode, int r_flag, int skip_flag);
int is_slapd_running();
void remove_slapd_process();

/*
 * These are the format strings used in the error messages in protect_db.c.  
 * After each string is a short description of what gets inserted into the
 * string.
 */

#define ERROR_ACCESSING_LOCKFILE "Error - Problem accessing the lockfile %s\n" 
  /* name of lockfile */

#define LOCKFILE_DEAD_OWNER "Error - The lockfile, %s, is held by process %d,\nwhich no longer seems to be running.  If this is\nthe case, please remove the lockfile\n"
  /* name of lockfile, pid of owning process */

#define UNABLE_TO_GET_LOCKFILE "Error - Unable to acquire the lockfile, %s.\nPlease make sure no instances of the ns-slapd process are running, remove the lockfile and try again\n"
  /* name of lockfile */

#define LOCKFILE_ALREADY_OWNED "Error - Unable to start because process %d holds a lock.\nPlease make sure that the process is running correctly\nIf it is not, kill it and try again.\n"
  /* pid of owning process */

#define FILE_CREATE_ERROR "Error - Unable to create %s, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n"
  /* name of file */

#define FILE_CREATE_WARNING "Warning - Unable to create %s\n"
  /* name of file */

#define GETPWNAM_WARNING "Warning - Unable to find user %s in system account database, errno %d (%s)\n"
  /* user name, error code, error message */

#define CHOWN_WARNING "Warning - couldn't set the ownership for %s\n"
  /* file name */

#define NO_SERVER_DUE_TO_SERVER "Unable to start slapd because it is already running as process %d\n"
  /* pid of running slapd process */

#define NO_SERVER_DUE_TO_IMPORT "Unable to start slapd because a database is being imported by process %d\n"
  /* pid of importing process */

#define NO_SERVER_DUE_TO_USE "Unable to start slapd because the database is in use\nby another slapd process\n"

#define NO_DB2LDIFR_DUE_TO_USE "Unable to run db2ldif with the -r flag because the database is being used by another slapd process.\n"

#define NO_DB2LDIF_DUE_TO_IMPORT "Unable to run db2ldif because the process %d is importing the database.\n"
  /* pid of importing process */

#define NO_DB2BAK_DUE_TO_IMPORT "Unable to run db2bak because the process %d is importing the database.\n"
  /* pid of importing process */

#define NO_EXPORT_DUE_TO_IMPORT "Unable to export the database because it is being imported.\n"

#define NO_IMPORT_DUE_TO_USE "Unable to import the database because it is being used by another slapd process.\n"

#define NO_DBTEST_DUE_TO_USE "Unable to test the database because it is being used by another slapd process.\n"

#define NO_DB2INDEX_DUE_TO_USE "Unable to create an index because the database is being used by another slapd process.\n"

#if defined(UPGRADEDB)
#define NO_UPGRADEDB_DUE_TO_USE "Unable to recreate index files because the database is being used by another slapd process.\n"
#endif

#define CREATE_MUTEX_ERROR "Error - CreateMutex failed: %s\n"
  /* reason for failure */

#define WAIT_ERROR "Error - wait failed: %s\n"
  /* reason for failure */

