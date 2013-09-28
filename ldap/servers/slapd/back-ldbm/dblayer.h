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

/* Structures and #defines used in the dblayer. */

#ifndef _DBLAYER_H_
#define _DBLAYER_H_

#ifdef DB_USE_64LFS
#ifdef OS_solaris
#include <dlfcn.h>	/* needed for dlopen and dlsym */
#endif /* solaris: dlopen */
#ifdef OS_solaris
#include <sys/mman.h>	/* needed for mmap/mmap64 */
#ifndef MAP_FAILED
#define MAP_FAILED (-1)
#endif
#endif  /* solaris: mmap */
#endif  /* DB_USE_64LFS */

#define DBLAYER_PAGESIZE (size_t)8*1024
#define DBLAYER_INDEX_PAGESIZE (size_t)8*1024 /* With the new idl design,
   the large 8Kbyte pages we use are not optimal. The page pool churns very
   quickly as we add new IDs under a sustained add load. Smaller pages stop
   this happening so much and consequently make us spend less time flushing
   dirty pages on checkpoints.  But 8K is still a good page size for id2entry. 
   So we now allow different page sizes for the primary and secondary indices.
   */

/* Interval, in ms, that threads sleep when they are wanting to
 * wait for a while withouth spinning. If this time is too long,
 * the server takes too long to shut down. If this interval is too
 * short, then CPU time gets burned by threads doing nothing.
 * As CPU speed increases over time, we reduce this interval
 * to allow the server to be more responsive to shutdown.
 * (Why is this important ? : A: because the TET tests start up
 * and shut down the server a gazillion times, so the server
 * shut down delay has a significant impact on the overall test
 * run time (which is very very very looooonnnnnggggg....).)
*/
#define DBLAYER_SLEEP_INTERVAL 250

#define DB_EXTN_PAGE_HEADER_SIZE 64 /* DBDB this is a guess */

#define DBLAYER_CACHE_FORCE_FILE 1

#define DBLAYER_LIB_VERSION_PRE_24 1
#define DBLAYER_LIB_VERSION_POST_24 2

/* Define constants from DB2.4 when using DB2.3 header file */
#ifndef DB_TSL_SPINS
#define	DB_TSL_SPINS	21		/* DB: initialize spin count. */
#endif
#ifndef DB_REGION_INIT
#define	DB_REGION_INIT	24		/* DB: page-fault regions in create. */
#endif
#ifndef DB_REGION_NAME
#define	DB_REGION_NAME	25		/* DB: named regions, no backing file. */
#endif

/* Used in check_and_set_import_cache */
/* After allocating the import cache, free memory must be left more than
 * the hard limit to run import. */
/* If the free memory size left is greater than hard limit and less than
 * soft limit, the import utility issues a warning, but it runs */
#define DBLAYER_IMPORTCACHESIZE_HL 100  /* import cache hard limit 100MB */
#define DBLAYER_IMPORTCACHESIZE_SL 1024 /* import cache soft limit 1GB */

struct dblayer_private_env {
	DB_ENV	*dblayer_DB_ENV;
	Slapi_RWLock * dblayer_env_lock;
	int dblayer_openflags;
	int dblayer_priv_flags;
};

#define DBLAYER_PRIV_SET_DATA_DIR 0x1

/* structure which holds our stuff */
struct dblayer_private
{
    struct dblayer_private_env * dblayer_env;
    char *dblayer_home_directory;
    char *dblayer_log_directory;
    char *dblayer_dbhome_directory;  /* default path for relative inst paths */
    char **dblayer_data_directories; /* passed to set_data_dir
                                      * including dblayer_dbhome_directory */
    char **dblayer_db_config;
    int dblayer_ncache;
    int dblayer_previous_ncache;
    int dblayer_tx_max;
    size_t dblayer_cachesize;
    size_t dblayer_previous_cachesize; /* Cache size when we last shut down--
                                        * used to determine if we delete 
                                        * the mpool */
    int dblayer_recovery_required;
    int dblayer_enable_transactions;
    int dblayer_durable_transactions;
    int dblayer_checkpoint_interval;
    int dblayer_circular_logging;
    size_t dblayer_page_size;       /* db page size if configured,
                                     * otherwise default to DBLAYER_PAGESIZE */
    size_t dblayer_index_page_size; /* db index page size if configured,
                                     * otherwise default to 
                                     * DBLAYER_INDEX_PAGESIZE */
    int dblayer_idl_divisor;        /* divide page size by this to get IDL 
                                     * size */
    size_t dblayer_logfile_size;    /* How large can one logfile be ? */
    size_t dblayer_logbuf_size;     /* how large log buffer can be */
    int dblayer_file_mode;          /* pmode for files we create */
    int dblayer_verbose;            /* Get libdb to exhale debugging info */
    int dblayer_debug;              /* Will libdb emit debugging info into 
                                     * our log ? */
    int dblayer_trickle_percentage;
    int dblayer_cache_config;       /* Special cache configurations
                                     * e.g. force file-based mpool */
    int dblayer_lib_version; 
    int dblayer_spin_count;         /* DB Mutex spin count, 0 == use default */
    int dblayer_named_regions;      /* Should the regions be named sections,
                                     * or backed by files ? */
    int dblayer_private_mem;        /* private memory will be used for 
                                     * allocation of regions and mutexes */
    int dblayer_private_import_mem; /* private memory will be used for 
                                     * allocation of regions and mutexes for
                                     * import */
    long dblayer_shm_key;           /* base segment ID for named regions */
    int db_debug_checkpointing;     /* Enable debugging messages from 
                                     * checkpointing */
    int dblayer_bad_stuff_happened; /* Means that something happened (e.g. out
                                     * of disk space) such that the guardian
                                     * file must not be written on shutdown */
    perfctrs_private *perf_private; /* Private data for performance counters
                                     * code */
    int dblayer_stop_threads;       /* Used to signal to threads that they 
                                     * should stop ASAP */
    PRInt32 dblayer_thread_count;   /* Tells us how many threads are running,
                                     * used to figure out when they're all
                                     * stopped */
    PRLock *thread_count_lock;      /* lock for thread_count_cv */
    PRCondVar *thread_count_cv;     /* condition variable for housekeeping thread shutdown */
    int dblayer_lockdown;           /* use DB_LOCKDOWN */
    int dblayer_lock_config;
    u_int32_t dblayer_deadlock_policy;    /* i.e. the atype to DB_ENV->lock_detect in deadlock_threadmain */
    int dblayer_compactdb_interval; /* interval to execute compact id2entry dbs */
};

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4300
void dblayer_log_print(const DB_ENV *dbenv, const char* prefix,
                       const char *buffer);
#else
void dblayer_log_print(const char* prefix, char *buffer);
#endif

int dblayer_db_remove(dblayer_private_env * env, char const path[], char const dbName[]);

int dblayer_delete_indices(ldbm_instance *inst);

/* Helper functions in dbhelp.c */

/* Make an environment to be used for isolated recovery (e.g. during a partial restore operation) */
int dblayer_make_private_recovery_env(char *db_home_dir, dblayer_private *priv, DB_ENV **env);
/* Make an environment to be used for simple non-transacted database operations, e.g. fixup during upgrade */
int dblayer_make_private_simple_env(char *db_home_dir, DB_ENV **env);
/* Copy a database file, preserving all its contents (used to reset the LSNs in the file in order to move 
 * it from one transacted environment to another.
 */
int dblayer_copy_file_resetlsns(char *home_dir, char *source_file_name, char *destination_file_name, int overwrite, dblayer_private *priv, ldbm_instance *inst);
/* Turn on the various logging and debug options for DB */
void dblayer_set_env_debugging(DB_ENV *pEnv, dblayer_private *priv);

/* Return the last four characters of a string; used for comparing extensions. */
char* last_four_chars(const char* s);

/* To support backingup/restoring changelog dir */
#define CHANGELOGENTRY   "cn=changelog5,cn=config"
#define CHANGELOGDIRATTR "nsslapd-changelogdir"

#endif /* _DBLAYER_H_ */
