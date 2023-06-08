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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <dirent.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include "agtmmap.h"
#include "slap.h"
#include "prthread.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "plstr.h"
#include "wthreads.h"

#ifdef HPUX
/* HP-UX doesn't define SEM_FAILED like other platforms, so
 * we define it ourselves. */
#define SEM_FAILED ((sem_t *)(-1))
#endif

#define SNMP_NUM_SEM_WAITS 10

#include "snmp_collator.h"

/* stevross: safe to assume port should be at most 5 digits ? */
#define PORT_LEN 5
/* strlen of url portions ie "ldap://:/" */
#define URL_CHARS_LEN 9

static char *make_ds_url(char *host, int port);
#ifdef DEBUG_SNMP_INTERACTION
static void print_snmp_interaction_table();
#endif /* DEBUG_SNMP_INTERACTION */
static int search_interaction_table(char *dsURL, int *isnew);
static void loadConfigStats(void);
static Slapi_Entry *getConfigEntry(Slapi_Entry **e);
static void freeConfigEntry(Slapi_Entry **e);
static void snmp_update_ops_table(void);
static void snmp_update_entries_table(void);
static void snmp_update_interactions_table(void);
static void snmp_update_cache_stats(void);
static void snmp_collator_create_semaphore(void);
static void snmp_collator_sem_wait(void);

/* snmp stats stuff */
struct agt_stats_t *stats = NULL;

/* mmap stuff */
static int hdl;

/* collator stuff */
static char *tmpstatsfile = AGT_STATS_FILE;
static char szStatsFile[_MAX_PATH];
static char stats_sem_name[_MAX_PATH];
static Slapi_Eq_Context snmp_eq_ctx;
static int snmp_collator_stopped = 0;

/* synchronization stuff */
static Slapi_Mutex *interaction_table_mutex = NULL;
static sem_t *stats_sem = NULL;


/***********************************************************************************
*
* int snmp_collator_init()
*
*    initializes the global variables used by snmp
*
************************************************************************************/

void
snmp_thread_counters_cleanup(struct snmp_vars_t *snmp_vars)
{
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsAnonymousBinds);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsUnAuthBinds);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsSimpleAuthBinds);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsStrongAuthBinds);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsBindSecurityErrors);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsInOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsReadOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsCompareOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsAddEntryOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsRemoveEntryOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsModifyEntryOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsModifyRDNOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsListOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsSearchOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsOneLevelSearchOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsWholeSubtreeSearchOps);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsReferrals);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsChainings);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsSecurityErrors);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsErrors);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsConnections);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsConnectionSeq);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsBytesRecv);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsBytesSent);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsEntriesReturned);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsReferralsReturned);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsConnectionsInMaxThreads);
    slapi_counter_destroy(&snmp_vars->ops_tbl.dsMaxThreadsHits);
    slapi_counter_destroy(&snmp_vars->entries_tbl.dsSupplierEntries);
    slapi_counter_destroy(&snmp_vars->entries_tbl.dsCopyEntries);
    slapi_counter_destroy(&snmp_vars->entries_tbl.dsCacheEntries);
    slapi_counter_destroy(&snmp_vars->entries_tbl.dsCacheHits);
    slapi_counter_destroy(&snmp_vars->entries_tbl.dsConsumerHits);
    slapi_counter_destroy(&snmp_vars->server_tbl.dsOpInitiated);
    slapi_counter_destroy(&snmp_vars->server_tbl.dsOpCompleted);
    slapi_counter_destroy(&snmp_vars->server_tbl.dsEntriesSent);
    slapi_counter_destroy(&snmp_vars->server_tbl.dsBytesSent);
}

void
snmp_thread_counters_init(struct snmp_vars_t *snmp_vars)
{
    snmp_vars->ops_tbl.dsAnonymousBinds = slapi_counter_new();
    snmp_vars->ops_tbl.dsUnAuthBinds = slapi_counter_new();
    snmp_vars->ops_tbl.dsSimpleAuthBinds = slapi_counter_new();
    snmp_vars->ops_tbl.dsStrongAuthBinds = slapi_counter_new();
    snmp_vars->ops_tbl.dsBindSecurityErrors = slapi_counter_new();
    snmp_vars->ops_tbl.dsInOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsReadOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsCompareOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsAddEntryOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsRemoveEntryOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsModifyEntryOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsModifyRDNOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsListOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsSearchOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsOneLevelSearchOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsWholeSubtreeSearchOps = slapi_counter_new();
    snmp_vars->ops_tbl.dsReferrals = slapi_counter_new();
    snmp_vars->ops_tbl.dsChainings = slapi_counter_new();
    snmp_vars->ops_tbl.dsSecurityErrors = slapi_counter_new();
    snmp_vars->ops_tbl.dsErrors = slapi_counter_new();
    snmp_vars->ops_tbl.dsConnections = slapi_counter_new();
    snmp_vars->ops_tbl.dsConnectionSeq = slapi_counter_new();
    snmp_vars->ops_tbl.dsBytesRecv = slapi_counter_new();
    snmp_vars->ops_tbl.dsBytesSent = slapi_counter_new();
    snmp_vars->ops_tbl.dsEntriesReturned = slapi_counter_new();
    snmp_vars->ops_tbl.dsReferralsReturned = slapi_counter_new();
    snmp_vars->ops_tbl.dsConnectionsInMaxThreads = slapi_counter_new();
    snmp_vars->ops_tbl.dsMaxThreadsHits = slapi_counter_new();
    snmp_vars->entries_tbl.dsSupplierEntries = slapi_counter_new();
    snmp_vars->entries_tbl.dsCopyEntries = slapi_counter_new();
    snmp_vars->entries_tbl.dsCacheEntries = slapi_counter_new();
    snmp_vars->entries_tbl.dsCacheHits = slapi_counter_new();
    snmp_vars->entries_tbl.dsConsumerHits = slapi_counter_new();
    snmp_vars->server_tbl.dsOpInitiated = slapi_counter_new();
    snmp_vars->server_tbl.dsOpCompleted = slapi_counter_new();
    snmp_vars->server_tbl.dsEntriesSent = slapi_counter_new();
    snmp_vars->server_tbl.dsBytesSent = slapi_counter_new();

    /* Initialize the global interaction table */
    for (size_t i = 0; i < NUM_SNMP_INT_TBL_ROWS; i++) {
        snmp_vars->int_tbl[i].dsIntIndex = i + 1;
        PL_strncpyz(snmp_vars->int_tbl[i].dsName, "Not Available",
                sizeof (snmp_vars->int_tbl[i].dsName));
        snmp_vars->int_tbl[i].dsTimeOfCreation = 0;
        snmp_vars->int_tbl[i].dsTimeOfLastAttempt = 0;
        snmp_vars->int_tbl[i].dsTimeOfLastSuccess = 0;
        snmp_vars->int_tbl[i].dsFailuresSinceLastSuccess = 0;
        snmp_vars->int_tbl[i].dsFailures = 0;
        snmp_vars->int_tbl[i].dsSuccesses = 0;
        PL_strncpyz(snmp_vars->int_tbl[i].dsURL, "Not Available",
                sizeof (snmp_vars->int_tbl[i].dsURL));
    }
}



static int
snmp_collator_init(void)
{
    /* Get the semaphore */
    snmp_collator_sem_wait();

    /* Initialize the mmap structure */
    memset((void *)stats, 0, sizeof(*stats));

    /* Load header stats table */
    PL_strncpyz(stats->hdr_stats.dsVersion, SLAPD_VERSION_STR,
            (sizeof(stats->hdr_stats.dsVersion) / sizeof(char)) - 1);
    stats->hdr_stats.restarted = 0;
    stats->hdr_stats.startTime = time(0); /* This is a bit off, hope it's ok */
    loadConfigStats();

    /* update the mmap'd tables */
    snmp_update_ops_table();
    snmp_update_entries_table();
    snmp_update_interactions_table();

    /* Release the semaphore */
    sem_post(stats_sem);

    /* create lock for interaction table */
    if (!interaction_table_mutex) {
        interaction_table_mutex = slapi_new_mutex();
    }

    return 0;
}


/***********************************************************************************
 * given the name, whether or not it was successful and the URL updates snmp
 * interaction table appropriately
 *
 *
************************************************************************************/

void
set_snmp_interaction_row(char *host, int port, int error)
{
    int index;
    int isnew = 0;
    char *dsName;
    char *dsURL;
    int cookie;
    struct snmp_vars_t *snmp_vars;

    /* The interactions table is using the default (first) snmp_vars*/
    snmp_vars = g_get_first_thread_snmp_vars(&cookie);
    if (snmp_vars == NULL || interaction_table_mutex == NULL)
        return;

    /* stevross: our servers don't have a concept of dsName as a distinguished name
               as specified in the MIB. Make this "Not Available" for now waiting for
           sometime in the future when we do
   */


    dsName = "Not Available";

    dsURL = make_ds_url(host, port);

    /* lock around here to avoid race condition of two threads trying to update table at same time */
    slapi_lock_mutex(interaction_table_mutex);
    index = search_interaction_table(dsURL, &isnew);
    if (isnew) {
        /* fillin the new row from scratch*/
        snmp_vars->int_tbl[index].dsIntIndex = index;
        PL_strncpyz(snmp_vars->int_tbl[index].dsName, dsName,
                sizeof(snmp_vars->int_tbl[index].dsName));
        snmp_vars->int_tbl[index].dsTimeOfCreation = time(0);
        snmp_vars->int_tbl[index].dsTimeOfLastAttempt = time(0);
        if (error == 0) {
            snmp_vars->int_tbl[index].dsTimeOfLastSuccess = time(0);
            snmp_vars->int_tbl[index].dsFailuresSinceLastSuccess = 0;
            snmp_vars->int_tbl[index].dsFailures = 0;
            snmp_vars->int_tbl[index].dsSuccesses = 1;
        } else {
            snmp_vars->int_tbl[index].dsTimeOfLastSuccess = 0;
            snmp_vars->int_tbl[index].dsFailuresSinceLastSuccess = 1;
            snmp_vars->int_tbl[index].dsFailures = 1;
            snmp_vars->int_tbl[index].dsSuccesses = 0;
        }
        PL_strncpyz(snmp_vars->int_tbl[index].dsURL, dsURL,
                sizeof(snmp_vars->int_tbl[index].dsURL));
    } else {
        /* just update the appropriate fields */
        snmp_vars->int_tbl[index].dsTimeOfLastAttempt = time(0);
        if (error == 0) {
            snmp_vars->int_tbl[index].dsTimeOfLastSuccess = time(0);
            snmp_vars->int_tbl[index].dsFailuresSinceLastSuccess = 0;
            snmp_vars->int_tbl[index].dsSuccesses += 1;
        } else {
            snmp_vars->int_tbl[index].dsFailuresSinceLastSuccess += 1;
            snmp_vars->int_tbl[index].dsFailures += 1;
        }
    }
    slapi_unlock_mutex(interaction_table_mutex);
    /* free the memory allocated for dsURL in call to ds_make_url */
    if (dsURL != NULL) {
        slapi_ch_free((void **)&dsURL);
    }
}

/***********************************************************************************
 * Given: host and port
 * Returns: ldapUrl in form of
 *    ldap://host.mcom.com:port/
 *
 *    this should point to root DSE
************************************************************************************/
static char *
make_ds_url(char *host, int port)
{
    char *url;

    url = slapi_ch_smprintf("ldap://%s:%d/", host, port);

    return url;
}

/***********************************************************************************
 * search_interaction_table is not used.
 * searches the table for the url specified
 * If there, returns index to update stats
 * if, not there returns index of oldest interaction, and isnew flag is set
 * so caller can rewrite this row
************************************************************************************/

static int
search_interaction_table(char *dsURL, int *isnew)
{
    int i;
    int index = 0;
    time_t oldestattempt;
    time_t currentattempt;
    int cookie;
    struct snmp_vars_t *snmp_vars;

    /* The interactions table is using the default (first) snmp_vars*/
    snmp_vars = g_get_first_thread_snmp_vars(&cookie);
    if (snmp_vars == NULL)
        return index;

    oldestattempt = snmp_vars->int_tbl[0].dsTimeOfLastAttempt;
    *isnew = 1;

    for (i = 0; i < NUM_SNMP_INT_TBL_ROWS; i++) {
        if (!strcmp(snmp_vars->int_tbl[i].dsURL, "Not Available")) {
            /* found it -- this is new, first time for this row */
            index = i;
            break;
        } else if (!strcmp(snmp_vars->int_tbl[i].dsURL, dsURL)) {
            /* found it  -- it was already there*/
            *isnew = 0;
            index = i;
            break;
        } else {
            /* not found so figure out oldest row */
            currentattempt = snmp_vars->int_tbl[i].dsTimeOfLastAttempt;

            if (currentattempt <= oldestattempt) {
                index = i;
                oldestattempt = currentattempt;
            }
        }
    }

    return index;
}

#ifdef DEBUG_SNMP_INTERACTION
/* for debugging until subagent part working, print contents of interaction table */
static void
print_snmp_interaction_table()
{
    int i;
    for (i = 0; i < NUM_SNMP_INT_TBL_ROWS; i++) {
        fprintf(stderr, "                dsIntIndex: %"PRInt32" \n", g_get_global_snmp_vars()->int_tbl[i].dsIntIndex);
        fprintf(stderr, "                    dsName: %s \n", g_get_global_snmp_vars()->int_tbl[i].dsName);
        fprintf(stderr, "          dsTimeOfCreation: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation);
        fprintf(stderr, "       dsTimeOfLastAttempt: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt);
        fprintf(stderr, "       dsTimeOfLastSuccess: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess);
        fprintf(stderr, "dsFailuresSinceLastSuccess: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess);
        fprintf(stderr, "                dsFailures: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsFailures);
        fprintf(stderr, "               dsSuccesses: %"PRIu64" \n", g_get_global_snmp_vars()->int_tbl[i].dsSuccesses);
        fprintf(stderr, "                     dsURL: %s \n", g_get_global_snmp_vars()->int_tbl[i].dsURL);
        fprintf(stderr, "\n");
    }
}
#endif /* DEBUG_SNMP_INTERACTION */


/***********************************************************************************
*
* int snmp_collator_start()
*
*   open the memory map and initialize the variables
*    initializes the global variables used by snmp
*
*    starts the collator thread
************************************************************************************/

int
snmp_collator_start()
{

    int err;
    char *statspath = config_get_rundir();
    char *instdir = config_get_configdir();
    char *instname = NULL;

    /*
   * Get directory for our stats file
   */
    if (NULL == statspath) {
        statspath = slapi_ch_strdup("/tmp");
    }

    instname = PL_strrstr(instdir, "slapd-");
    if (!instname) {
        instname = PL_strrstr(instdir, "/");
        if (instname) {
            instname++;
        }
    }
    PR_snprintf(szStatsFile, sizeof(szStatsFile), "%s/%s%s",
                statspath, instname, AGT_STATS_EXTENSION);
    PR_snprintf(stats_sem_name, sizeof(stats_sem_name), "/%s%s",
                instname, AGT_STATS_EXTENSION);
    tmpstatsfile = szStatsFile;
    slapi_ch_free_string(&statspath);
    slapi_ch_free_string(&instdir);

    /* open the memory map */
    if ((err = agt_mopen_stats(tmpstatsfile, O_RDWR, &hdl) != 0)) {
        if (err != EEXIST) /* Ignore if file already exists */
        {
            slapi_log_err(SLAPI_LOG_EMERG, "snmp collator", "Failed to open stats file (%s) "
                                                            "(error %d): %s.\n",
                          szStatsFile, err, slapd_system_strerror(err));
            exit(1);
        }
    }

    /* Create semaphore for stats file access */
    snmp_collator_create_semaphore();

    /* point stats struct at mmap data */
    stats = (struct agt_stats_t *)mmap_tbl[hdl].fp;

    /* initialize stats data */
    snmp_collator_init();

    /* Arrange to be called back periodically to update the mmap'd stats file. */
    snmp_eq_ctx = slapi_eq_repeat_rel(snmp_collator_update, NULL,
                                      slapi_current_rel_time_t(),
                                      SLAPD_SNMP_UPDATE_INTERVAL);
    return 0;
}


/***********************************************************************************
*
* int snmp_collator_stop()
*
*    stops the collator thread
*   closes the memory map
*   cleans up any needed memory
*
************************************************************************************/

int
snmp_collator_stop()
{
    int err;

    if (snmp_collator_stopped) {
        return 0;
    }

    /* Abort any pending events */
    slapi_eq_cancel_rel(snmp_eq_ctx);
    snmp_collator_stopped = 1;

    /* acquire the semaphore */
    snmp_collator_sem_wait();

    /* close the memory map */
    if ((err = agt_mclose_stats(hdl)) != 0) {
        fprintf(stderr, "Failed to close stats file (%s) (error = %d).",
                AGT_STATS_FILE, err);
    }

    if (remove(tmpstatsfile) != 0) {
        fprintf(stderr, "Failed to remove (%s) (error =  %d).\n",
                tmpstatsfile, errno);
    }

    /* close and delete semaphore */
    sem_close(stats_sem);
    sem_unlink(stats_sem_name);

    /* delete lock */
    slapi_destroy_mutex(interaction_table_mutex);

    /* stevross: I probably need to free stats too... make sure to add that later */

    return 0;
}

/*
 * snmp_collator_create_semaphore()
 *
 * Create a semaphore to synchronize access to the stats file with
 * the SNMP sub-agent.  NSPR doesn't support a trywait function
 * for semaphores, so we just use POSIX semaphores directly.
 */
static void
snmp_collator_create_semaphore(void)
{
    /* First just try to create the semaphore.  This should usually just work. */
    if ((stats_sem = sem_open(stats_sem_name, O_CREAT | O_EXCL, SLAPD_DEFAULT_FILE_MODE, 1)) == SEM_FAILED) {
        if (errno == EEXIST) {
            /* It appears that we didn't exit cleanly last time and left the semaphore
             * around.  Recreate it since we don't know what state it is in. */
            if (sem_unlink(stats_sem_name) != 0) {
                slapi_log_err(SLAPI_LOG_EMERG, "snmp_collator_create_semaphore",
                              "Failed to delete old semaphore for stats file (/dev/shm/sem.%s). "
                              "Error %d (%s).\n",
                              stats_sem_name + 1, errno, slapd_system_strerror(errno));
                exit(1);
            }

            if ((stats_sem = sem_open(stats_sem_name, O_CREAT | O_EXCL, SLAPD_DEFAULT_FILE_MODE, 1)) == SEM_FAILED) {
                /* No dice */
                slapi_log_err(SLAPI_LOG_EMERG, "snmp_collator_create_semaphore",
                              "Failed to create semaphore for stats file (/dev/shm/sem.%s). Error %d (%s).\n",
                              stats_sem_name + 1, errno, slapd_system_strerror(errno));
                exit(1);
            }
        } else {
            /* Some other problem occurred creating the semaphore. */
            slapi_log_err(SLAPI_LOG_EMERG, "snmp_collator_create_semaphore",
                          "Failed to create semaphore for stats file (/dev/shm/sem.%s). Error %d.(%s)\n",
                          stats_sem_name + 1, errno, slapd_system_strerror(errno));
            exit(1);
        }
    }

    /* If we've reached this point, everything should be good. */
    return;
}

/*
 * snmp_collator_sem_wait()
 *
 * A wrapper used to get the semaphore.  We don't want to block,
 * but we want to retry a specified number of times in case the
 * semaphore is help by the sub-agent.
 */
static void
snmp_collator_sem_wait(void)
{
    int i = 0;
    int got_sem = 0;

    if (SEM_FAILED == stats_sem) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "snmp_collator_sem_wait", "semaphore for stats file (%s) is not available.\n", szStatsFile);
        return;
    }

    for (i = 0; i < SNMP_NUM_SEM_WAITS; i++) {
        if (sem_trywait(stats_sem) == 0) {
            got_sem = 1;
            break;
        }
        PR_Sleep(PR_SecondsToInterval(1));
    }

    if (!got_sem) {
        /* If we've been unable to get the semaphore, there's
         * something wrong (likely the sub-agent went out to
         * lunch).  We remove the old semaphore and recreate
         * a new one to avoid hanging up the server. */
        sem_close(stats_sem);
        sem_unlink(stats_sem_name);
        snmp_collator_create_semaphore();
    }
}


/***********************************************************************************
*
* int snmp_collator_update()
*
* Event callback function that updates the mmap'd stats file
* for the SNMP sub-agent.  This will use a semaphore while
* updating the stats file to prevent the SNMP sub-agent from
* reading it in the middle of an update.
*
************************************************************************************/

void
snmp_collator_update(time_t start_time __attribute__((unused)), void *arg __attribute__((unused)))
{
    if (snmp_collator_stopped) {
        return;
    }

    /* force an update of the backend cache stats. */
    snmp_update_cache_stats();

    /* get the semaphore */
    snmp_collator_sem_wait();

    /* just update the update time in the header */
    if (stats != NULL) {
        stats->hdr_stats.updateTime = time(0);
    }

    /* update the mmap'd tables */
    snmp_update_ops_table();
    snmp_update_entries_table();
    snmp_update_interactions_table();

    /* release the semaphore */
    sem_post(stats_sem);
}

/*
 * snmp_update_ops_table()
 *
 * Updates the mmap'd operations table.  The semaphore
 * should be acquired before you call this.
 */
static void
snmp_update_ops_table(void)
{
    int cookie;
    struct snmp_vars_t *snmp_vars;
    int32_t total;

    pthread_mutex_lock(&g_pc.snmp.mutex);
    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsAnonymousBinds);
    }
    stats->ops_stats.dsAnonymousBinds = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsUnAuthBinds);
    }
    stats->ops_stats.dsUnAuthBinds = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSimpleAuthBinds);
    }
    stats->ops_stats.dsSimpleAuthBinds = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsStrongAuthBinds);
    }
    stats->ops_stats.dsStrongAuthBinds = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBindSecurityErrors);
    }
    stats->ops_stats.dsBindSecurityErrors = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsInOps);
    }
    stats->ops_stats.dsInOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReadOps);
    }
    stats->ops_stats.dsReadOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsCompareOps);
    }
    stats->ops_stats.dsCompareOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsAddEntryOps);
    }
    stats->ops_stats.dsAddEntryOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsRemoveEntryOps);
    }
    stats->ops_stats.dsRemoveEntryOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsModifyEntryOps);
    }
    stats->ops_stats.dsModifyEntryOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsModifyRDNOps);
    }
    stats->ops_stats.dsModifyRDNOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsListOps);
    }
    stats->ops_stats.dsListOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSearchOps);
    }
    stats->ops_stats.dsSearchOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsOneLevelSearchOps);
    }
    stats->ops_stats.dsOneLevelSearchOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsWholeSubtreeSearchOps);
    }
    stats->ops_stats.dsWholeSubtreeSearchOps = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReferrals);
    }
    stats->ops_stats.dsReferrals = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsChainings);
    }
    stats->ops_stats.dsChainings = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSecurityErrors);
    }
    stats->ops_stats.dsSecurityErrors = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsErrors);
    }
    stats->ops_stats.dsErrors = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnections);
    }
    stats->ops_stats.dsConnections = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnectionSeq);
    }
    stats->ops_stats.dsConnectionSeq = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnectionsInMaxThreads);
    }
    stats->ops_stats.dsConnectionsInMaxThreads = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsMaxThreadsHits);
    }
    stats->ops_stats.dsMaxThreadsHits = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBytesRecv);
    }
    stats->ops_stats.dsBytesRecv = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBytesSent);
    }
    stats->ops_stats.dsBytesSent = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsEntriesReturned);
    }
    stats->ops_stats.dsEntriesReturned = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReferralsReturned);
    }
    stats->ops_stats.dsReferralsReturned = total;
    pthread_mutex_unlock(&g_pc.snmp.mutex);
}

/*
 * snmp_update_entries_table()
 *
 * Updated the mmap'd entries table.  The semaphore should
 * be acquired before you call this.
 */
static void
snmp_update_entries_table(void)
{
    int cookie;
    struct snmp_vars_t *snmp_vars;
    int32_t total;

    pthread_mutex_lock(&g_pc.snmp.mutex);
    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsSupplierEntries);
    }
    stats->entries_stats.dsSupplierEntries = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCopyEntries);
    }
    stats->entries_stats.dsCopyEntries = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCacheEntries);
    }
    stats->entries_stats.dsCacheEntries = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCacheHits);
    }
    stats->entries_stats.dsCacheHits = total;

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsConsumerHits);
    }
    stats->entries_stats.dsConsumerHits = total;
    pthread_mutex_unlock(&g_pc.snmp.mutex);
}

/*
 * snmp_update_interactions_table()
 *
 * Updates the mmap'd interactions table.  The semaphore should
 * be acquired before you call this.
 */
static void
snmp_update_interactions_table(void)
{
    int i;
    int cookie;
    struct snmp_vars_t *snmp_vars;
    
    /* The interactions table is using the default (first) snmp_vars*/
    snmp_vars = g_get_first_thread_snmp_vars(&cookie);
    if (snmp_vars == NULL)
        return;

    for (i = 0; i < NUM_SNMP_INT_TBL_ROWS; i++) {
        stats->int_stats[i].dsIntIndex = i;
        PL_strncpyz(stats->int_stats[i].dsName, snmp_vars->int_tbl[i].dsName,
                sizeof(stats->int_stats[i].dsName));
        stats->int_stats[i].dsTimeOfCreation = snmp_vars->int_tbl[i].dsTimeOfCreation;
        stats->int_stats[i].dsTimeOfLastAttempt = snmp_vars->int_tbl[i].dsTimeOfLastAttempt;
        stats->int_stats[i].dsTimeOfLastSuccess = snmp_vars->int_tbl[i].dsTimeOfLastSuccess;
        stats->int_stats[i].dsFailuresSinceLastSuccess = snmp_vars->int_tbl[i].dsFailuresSinceLastSuccess;
        stats->int_stats[i].dsFailures = snmp_vars->int_tbl[i].dsFailures;
        stats->int_stats[i].dsSuccesses = snmp_vars->int_tbl[i].dsSuccesses;
        PL_strncpyz(stats->int_stats[i].dsURL, snmp_vars->int_tbl[i].dsURL,
                sizeof(stats->int_stats[i].dsURL));
    }
}

/*
 * snmp_update_cache_stats()
 *
 * Reads the backend cache stats from the backend monitor entry and
 * updates the global counter used by the SNMP sub-agent as well as
 * the SNMP monitor entry.
 */
static void
snmp_update_cache_stats(void)
{
    Slapi_Backend *be, *be_next;
    char *cookie = NULL;
    Slapi_PBlock *search_result_pb = NULL;
    Slapi_Entry **search_entries;
    int search_result;

    /* set the cache hits/cache entries info */
    be = slapi_get_first_backend(&cookie);
    if (!be) {
        slapi_ch_free((void **)&cookie);
        return;
    }

    be_next = slapi_get_next_backend(cookie);

    slapi_ch_free((void **)&cookie);

    /* for now, only do it if there is only 1 backend, otherwise don't know
     * which backend to pick */
    if (be_next == NULL) {
        Slapi_DN monitordn;
        slapi_sdn_init(&monitordn);
        be_getmonitordn(be, &monitordn);

        /* do a search on the monitor dn to get info */
        search_result_pb = slapi_search_internal(slapi_sdn_get_dn(&monitordn),
                                                 LDAP_SCOPE_BASE,
                                                 "objectclass=*",
                                                 NULL,
                                                 NULL,
                                                 0);
        slapi_sdn_done(&monitordn);

        slapi_pblock_get(search_result_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);

        if (search_result == 0) {
            int cookie;
            struct snmp_vars_t *snmp_vars;
            slapi_pblock_get(search_result_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                             &search_entries);

            /* set the entrycachehits to the global counter*/
            snmp_vars = g_get_first_thread_snmp_vars(&cookie);
            slapi_counter_set_value(snmp_vars->entries_tbl.dsCacheHits,
                                    slapi_entry_attr_get_ulonglong(search_entries[0], "entrycachehits"));

            /* set the currententrycachesize  to the global counter */
            slapi_counter_set_value(snmp_vars->entries_tbl.dsCacheEntries,
                                    slapi_entry_attr_get_ulonglong(search_entries[0], "currententrycachesize"));
        }

        slapi_free_search_results_internal(search_result_pb);
        slapi_pblock_destroy(search_result_pb);
    }
}

static void
add_counter_to_value(Slapi_Entry *e, const char *type, PRUint64 countervalue)
{
    char value[40];
    snprintf(value, sizeof(value), "%" PRIu64, countervalue);
    slapi_entry_attr_set_charptr(e, type, value);
}

void
snmp_as_entry(Slapi_Entry *e)
{
    int cookie;
    uint64_t total;
    struct snmp_vars_t *snmp_vars;

    pthread_mutex_lock(&g_pc.snmp.mutex);
    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsAnonymousBinds);
    }
    add_counter_to_value(e, "AnonymousBinds", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsUnAuthBinds);
    }
    add_counter_to_value(e, "UnAuthBinds", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSimpleAuthBinds);
    }
    add_counter_to_value(e, "SimpleAuthBinds", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsStrongAuthBinds);
    }
    add_counter_to_value(e, "StrongAuthBinds", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBindSecurityErrors);
    }
    add_counter_to_value(e, "BindSecurityErrors", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsInOps);
    }
    add_counter_to_value(e, "InOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReadOps);
    }
    add_counter_to_value(e, "ReadOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsCompareOps);
    }
    add_counter_to_value(e, "CompareOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsAddEntryOps);
    }
    add_counter_to_value(e, "AddEntryOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsRemoveEntryOps);
    }
    add_counter_to_value(e, "RemoveEntryOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsModifyEntryOps);
    }
    add_counter_to_value(e, "ModifyEntryOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsModifyRDNOps);
    }
    add_counter_to_value(e, "ModifyRDNOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsListOps);
    }
    add_counter_to_value(e, "ListOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSearchOps);
    }
    add_counter_to_value(e, "SearchOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsOneLevelSearchOps);
    }
    add_counter_to_value(e, "OneLevelSearchOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsWholeSubtreeSearchOps);
    }
    add_counter_to_value(e, "WholeSubtreeSearchOps", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReferrals);
    }
    add_counter_to_value(e, "Referrals", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsChainings);
    }
    add_counter_to_value(e, "Chainings", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsSecurityErrors);
    }
    add_counter_to_value(e, "SecurityErrors", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsErrors);
    }
    add_counter_to_value(e, "Errors", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnections);
    }
    add_counter_to_value(e, "Connections", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnectionSeq);
    }
    add_counter_to_value(e, "ConnectionSeq", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsConnectionsInMaxThreads);
    }
    add_counter_to_value(e, "ConnectionsInMaxThreads", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsMaxThreadsHits);
    }
    add_counter_to_value(e, "ConnectionsMaxThreadsCount", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBytesRecv);
    }
    add_counter_to_value(e, "BytesRecv", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsBytesSent);
    }
    add_counter_to_value(e, "BytesSent", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsEntriesReturned);
    }
    add_counter_to_value(e, "EntriesReturned", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->ops_tbl.dsReferralsReturned);
    }
    add_counter_to_value(e, "ReferralsReturned", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsSupplierEntries);
    }
    add_counter_to_value(e, "SupplierEntries", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCopyEntries);
    }
    add_counter_to_value(e, "CopyEntries", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCacheEntries);
    }
    add_counter_to_value(e, "CacheEntries", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsCacheHits);
    }
    add_counter_to_value(e, "CacheHits", total);

    for (total = 0, snmp_vars = g_get_first_thread_snmp_vars(&cookie); snmp_vars; snmp_vars = g_get_next_thread_snmp_vars(&cookie)) {
        total += slapi_counter_get_value(snmp_vars->entries_tbl.dsConsumerHits);
    }
    add_counter_to_value(e, "ConsumerHits", total);
    pthread_mutex_unlock(&g_pc.snmp.mutex);
}

/*
 * loadConfigStats()
 *
 * Reads the header table SNMP settings and sets them in the mmap'd stats
 * file.  This should be done only when the semaphore is held.
 */
static void
loadConfigStats(void)
{
    Slapi_Entry *entry = NULL;
    char *name = NULL;
    char *desc = NULL;
    char *org = NULL;
    char *loc = NULL;
    char *contact = NULL;

    /* Read attributes from SNMP config entry */
    getConfigEntry(&entry);
    if (entry != NULL) {
        name = (char *)slapi_entry_attr_get_ref(entry, SNMP_NAME_ATTR);
        desc = (char *)slapi_entry_attr_get_ref(entry, SNMP_DESC_ATTR);
        org = (char *)slapi_entry_attr_get_ref(entry, SNMP_ORG_ATTR);
        loc = (char *)slapi_entry_attr_get_ref(entry, SNMP_LOC_ATTR);
        contact = (char *)slapi_entry_attr_get_ref(entry, SNMP_CONTACT_ATTR);
    }

    /* Load stats into table */
    if (name != NULL) {
        PL_strncpyz(stats->hdr_stats.dsName, name, SNMP_FIELD_LENGTH);
    }

    if (desc != NULL) {
        PL_strncpyz(stats->hdr_stats.dsDescription, desc, SNMP_FIELD_LENGTH);
    }

    if (org != NULL) {
        PL_strncpyz(stats->hdr_stats.dsOrganization, org, SNMP_FIELD_LENGTH);
    }

    if (loc != NULL) {
        PL_strncpyz(stats->hdr_stats.dsLocation, loc, SNMP_FIELD_LENGTH);
    }

    if (contact != NULL) {
        PL_strncpyz(stats->hdr_stats.dsContact, contact, SNMP_FIELD_LENGTH);
    }

    if (entry) {
        freeConfigEntry(&entry);
    }
}

static Slapi_Entry *
getConfigEntry(Slapi_Entry **e)
{
    Slapi_DN sdn;

    /* SNMP_CONFIG_DN: no need to be normalized */
    slapi_sdn_init_normdn_byref(&sdn, SNMP_CONFIG_DN);
    slapi_search_internal_get_entry(&sdn, NULL, e,
                                    plugin_get_default_component_id());
    slapi_sdn_done(&sdn);
    return *e;
}

static void
freeConfigEntry(Slapi_Entry **e)
{
    if ((e != NULL) && (*e != NULL)) {
        slapi_entry_free(*e);
        *e = NULL;
    }
}
