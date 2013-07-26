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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef  _WIN32
#include <sys/ipc.h>
#include <sys/msg.h>
#include <dirent.h>
#include <semaphore.h>
#endif
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

#ifdef HPUX
/* HP-UX doesn't define SEM_FAILED like other platforms, so
 * we define it ourselves. */
#define SEM_FAILED ((sem_t *)(-1))
#endif

#define SNMP_NUM_SEM_WAITS 10

#include "snmp_collator.h" 
#include "../snmp/ntagt/nslagtcom_nt.h"

/* stevross: safe to assume port should be at most 5 digits ? */
#define PORT_LEN 5
/* strlen of url portions ie "ldap://:/" */
#define URL_CHARS_LEN 9 

static char *make_ds_url(char *host, int port);
#ifdef DEBUG_SNMP_INTERACTION
static void print_snmp_interaction_table();
#endif /* DEBUG_SNMP_INTERACTION */
static int search_interaction_table(char *dsURL, int *isnew);
static void loadConfigStats();
static Slapi_Entry *getConfigEntry( Slapi_Entry **e );
static void freeConfigEntry( Slapi_Entry **e );
static void snmp_update_ops_table();
static void snmp_update_entries_table();
static void snmp_update_interactions_table();
static void snmp_update_cache_stats();
static void snmp_collator_create_semaphore();
static void snmp_collator_sem_wait();

/* snmp stats stuff */
struct agt_stats_t *stats=NULL;

/* mmap stuff */
static int hdl;

/* collator stuff */
static char *tmpstatsfile = AGT_STATS_FILE;
#ifdef _WIN32
static TCHAR szStatsFile[_MAX_PATH];
static TCHAR szTempDir[_MAX_PATH];
static HANDLE hParentProcess = NULL;
static HANDLE hStatSlot = NULL;
static HANDLE hLogFile = INVALID_HANDLE_VALUE;
static TCHAR szSpoolRootDir[_MAX_PATH];
#else
static char szStatsFile[_MAX_PATH];
static char stats_sem_name[_MAX_PATH];
#endif /* _WIN32*/
static Slapi_Eq_Context snmp_eq_ctx;
static int snmp_collator_stopped = 0;

/* synchronization stuff */
static PRLock 		*interaction_table_mutex;
static sem_t		*stats_sem;


/***********************************************************************************
*
* int snmp_collator_init()
*
*	initializes the global variables used by snmp
*
************************************************************************************/

static int snmp_collator_init(){
	int i;

	/*
	 * Create the global SNMP counters
	 */
	g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsSimpleAuthBinds		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsStrongAuthBinds		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsInOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsReadOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsCompareOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsAddEntryOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsRemoveEntryOps		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsModifyEntryOps		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsModifyRDNOps		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsListOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsSearchOps			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsOneLevelSearchOps		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsWholeSubtreeSearchOps	= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsReferrals			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsChainings			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsSecurityErrors		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsErrors			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsConnections			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsConnectionSeq		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsBytesRecv			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsBytesSent			= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsEntriesReturned		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsReferralsReturned		= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads	= slapi_counter_new();
	g_get_global_snmp_vars()->ops_tbl.dsMaxThreadsHit		= slapi_counter_new();
	g_get_global_snmp_vars()->entries_tbl.dsMasterEntries		= slapi_counter_new();
	g_get_global_snmp_vars()->entries_tbl.dsCopyEntries		= slapi_counter_new();
	g_get_global_snmp_vars()->entries_tbl.dsCacheEntries		= slapi_counter_new();
	g_get_global_snmp_vars()->entries_tbl.dsCacheHits		= slapi_counter_new();
	g_get_global_snmp_vars()->entries_tbl.dsSlaveHits		= slapi_counter_new();

	/* Initialize the global interaction table */
	for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	{
		g_get_global_snmp_vars()->int_tbl[i].dsIntIndex                    = i + 1;
		strncpy(g_get_global_snmp_vars()->int_tbl[i].dsName, "Not Available",
			sizeof(g_get_global_snmp_vars()->int_tbl[i].dsName));
		g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation              = 0;
		g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt           = 0;
		g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess           = 0;
		g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess    = 0;
		g_get_global_snmp_vars()->int_tbl[i].dsFailures                    = 0;
		g_get_global_snmp_vars()->int_tbl[i].dsSuccesses                   = 0;
		strncpy(g_get_global_snmp_vars()->int_tbl[i].dsURL, "Not Available",
			sizeof(g_get_global_snmp_vars()->int_tbl[i].dsURL));
	}

	/* Get the semaphore */
	snmp_collator_sem_wait();

	/* Initialize the mmap structure */
	memset((void *) stats, 0, sizeof(*stats));

	/* Load header stats table */
	strncpy(stats->hdr_stats.dsVersion, SLAPD_VERSION_STR,
                (sizeof(stats->hdr_stats.dsVersion)/sizeof(char)) - 1);
	stats->hdr_stats.restarted = 0;			 
	stats->hdr_stats.startTime = time(0);		/* This is a bit off, hope it's ok */
	loadConfigStats();

	/* update the mmap'd tables */
	snmp_update_ops_table();
	snmp_update_entries_table();
	snmp_update_interactions_table();

	/* Release the semaphore */
	sem_post(stats_sem);

	/* create lock for interaction table */
        interaction_table_mutex = PR_NewLock();

	return 0;
}



/***********************************************************************************
 * given the name, whether or not it was successful and the URL updates snmp 
 * interaction table appropriately
 *
 *
************************************************************************************/

void set_snmp_interaction_row(char *host, int port, int error)
{
  int index;
  int isnew;
  char *dsName;
  char *dsURL;

  /* stevross: our servers don't have a concept of dsName as a distinguished name
               as specified in the MIB. Make this "Not Available" for now waiting for
	       sometime in the future when we do
   */
               
	       
  dsName = "Not Available";

  dsURL= make_ds_url(host, port);

  /* lock around here to avoid race condition of two threads trying to update table at same time */
  PR_Lock(interaction_table_mutex);     
      index = search_interaction_table(dsURL, &isnew);
  
      if(isnew){
          /* fillin the new row from scratch*/
          g_get_global_snmp_vars()->int_tbl[index].dsIntIndex	                  = index;
          strncpy(g_get_global_snmp_vars()->int_tbl[index].dsName, dsName,
                  sizeof(g_get_global_snmp_vars()->int_tbl[index].dsName));
          g_get_global_snmp_vars()->int_tbl[index].dsTimeOfCreation	          = time(0);
          g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastAttempt	          = time(0);
          if(error == 0){
              g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess	  = time(0);
              g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess = 0;
              g_get_global_snmp_vars()->int_tbl[index].dsFailures	          = 0;
              g_get_global_snmp_vars()->int_tbl[index].dsSuccesses		  = 1;
          }else{
              g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess	  = 0;
              g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess = 1;
              g_get_global_snmp_vars()->int_tbl[index].dsFailures		  = 1;
              g_get_global_snmp_vars()->int_tbl[index].dsSuccesses		  = 0;
          }
          strncpy(g_get_global_snmp_vars()->int_tbl[index].dsURL, dsURL,
                  sizeof(g_get_global_snmp_vars()->int_tbl[index].dsURL));		         
      }else{
        /* just update the appropriate fields */
           g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastAttempt	         = time(0);
          if(error == 0){
             g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess        = time(0);
             g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess = 0;
             g_get_global_snmp_vars()->int_tbl[index].dsSuccesses                += 1;
          }else{
             g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess +=1;
             g_get_global_snmp_vars()->int_tbl[index].dsFailures                 +=1;
          }

      }
  PR_Unlock(interaction_table_mutex);     
  /* free the memory allocated for dsURL in call to ds_make_url */
  if(dsURL != NULL){
    slapi_ch_free( (void**)&dsURL );
  }
}

/***********************************************************************************
 * Given: host and port
 * Returns: ldapUrl in form of
 *    ldap://host.mcom.com:port/
 * 
 *    this should point to root DSE 
************************************************************************************/
static char *make_ds_url(char *host, int port){

  char *url;
  
   url = slapi_ch_smprintf("ldap://%s:%d/",host, port);
 
   return url;
}


/***********************************************************************************
 * searches the table for the url specified 
 * If there, returns index to update stats
 * if, not there returns index of oldest interaction, and isnew flag is set
 * so caller can rewrite this row
************************************************************************************/

static int search_interaction_table(char *dsURL, int *isnew)
{
  int i;
  int index = 0;
  time_t oldestattempt;
  time_t currentattempt;
   
   oldestattempt = g_get_global_snmp_vars()->int_tbl[0].dsTimeOfLastAttempt;
   *isnew = 1;
   
   for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++){
       if(!strcmp(g_get_global_snmp_vars()->int_tbl[i].dsURL, "Not Available"))
       {
           /* found it -- this is new, first time for this row */
	   index = i;
	   break;
       }else  if(!strcmp(g_get_global_snmp_vars()->int_tbl[i].dsURL, dsURL)){
           /* found it  -- it was already there*/
	   *isnew = 0;
	   index = i;
	   break;
       }else{
          /* not found so figure out oldest row */ 
          currentattempt = g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt;
	 
          if(currentattempt <= oldestattempt){
              index=i;
	      oldestattempt = currentattempt;
          }
       }

   }
   
   return index;

}

#ifdef DEBUG_SNMP_INTERACTION
/* for debuging until subagent part working, print contents of interaction table */
static void print_snmp_interaction_table()
{ 
  int i;
  for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
  {
    fprintf(stderr, "                dsIntIndex: %d \n", g_get_global_snmp_vars()->int_tbl[i].dsIntIndex);
    fprintf(stderr, "                    dsName: %s \n",   g_get_global_snmp_vars()->int_tbl[i].dsName);
    fprintf(stderr, "          dsTimeOfCreation: %ld \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation);
    fprintf(stderr, "       dsTimeOfLastAttempt: %ld \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt);
    fprintf(stderr, "       dsTimeOfLastSuccess: %ld \n", g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess);
    fprintf(stderr, "dsFailuresSinceLastSuccess: %d \n", g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess);
    fprintf(stderr, "                dsFailures: %d \n", g_get_global_snmp_vars()->int_tbl[i].dsFailures);
    fprintf(stderr, "               dsSuccesses: %d \n", g_get_global_snmp_vars()->int_tbl[i].dsSuccesses);
    fprintf(stderr, "                     dsURL: %s \n", g_get_global_snmp_vars()->int_tbl[i].dsURL);
    fprintf(stderr, "\n");
  }
}
#endif /* DEBUG_SNMP_INTERACTION */

/*-------------------------------------------------------------------------
 *
 * sc_setevent:  Sets the specified event (NT only).  The input event has
 *               to be created by the subagent during its initialization.
 *
 * Returns:  None
 *
 *-----------------------------------------------------------------------*/

#ifdef _WIN32
void sc_setevent(char *ev)
{
  HANDLE hTrapEvent;
  DWORD err = NO_ERROR;
  
  /*
   * Set the event handle to force NT SNMP service to call the subagent
   * DLL to generate a trap.  Any error will be ignored as the subagent
   * may not have been loaded.
   */
  if ((hTrapEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, 
                              (LPCTSTR) ev)) != NULL)
  {
    if (SetEvent(hTrapEvent) == FALSE)
      err = GetLastError();
  }
  else
    err = GetLastError();

  if (err != NO_ERROR)
  {
	  fprintf(stderr, "Failed to set trap (error = %d).\n", err);
  }
}
#endif



/***********************************************************************************
*
* int snmp_collator_start()
*
*   open the memory map and initialize the variables 
*	initializes the global variables used by snmp
*   
*	starts the collator thread
************************************************************************************/

int snmp_collator_start()
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
  if ((err = agt_mopen_stats(tmpstatsfile, O_RDWR,  &hdl) != 0))
  {
    if (err != EEXIST)      /* Ignore if file already exists */
    {
      slapi_log_error(SLAPI_LOG_FATAL, "snmp collator", "Failed to open stats file (%s) "
                      "(error %d): %s.\n", szStatsFile, err, slapd_system_strerror(err));
      exit(1);
    }
  }

  /* Create semaphore for stats file access */
  snmp_collator_create_semaphore();

  /* point stats struct at mmap data */
  stats = (struct agt_stats_t *) mmap_tbl [hdl].fp;

  /* initialize stats data */
  snmp_collator_init();

  /* Arrange to be called back periodically to update the mmap'd stats file. */
  snmp_eq_ctx = slapi_eq_repeat(snmp_collator_update, NULL, (time_t)0,
                                SLAPD_SNMP_UPDATE_INTERVAL);
  return 0;
}


/***********************************************************************************
*
* int snmp_collator_stop()
*
*	stops the collator thread
*   closes the memory map
*   cleans up any needed memory
*	
************************************************************************************/

int snmp_collator_stop()
{
	int err;

	if (snmp_collator_stopped) {
		return 0;
	}

	/* Abort any pending events */
	slapi_eq_cancel(snmp_eq_ctx);
	snmp_collator_stopped = 1;

   /* acquire the semaphore */
   snmp_collator_sem_wait();

   /* close the memory map */
   if ((err = agt_mclose_stats(hdl)) != 0)
   {
        fprintf(stderr, "Failed to close stats file (%s) (error = %d).", 
            AGT_STATS_FILE, err);
   }

   if (remove(tmpstatsfile) != 0)
   {
       fprintf(stderr, "Failed to remove (%s) (error =  %d).\n", 
            tmpstatsfile, errno);
   }

   /* close and delete semaphore */
   sem_close(stats_sem);
   sem_unlink(stats_sem_name);

   /* delete lock */
   if (interaction_table_mutex) {
       PR_DestroyLock(interaction_table_mutex);
   }

#ifdef _WIN32
   /* send the event so server down trap gets set on NT */
   sc_setevent(MAGT_NSEV_SNMPTRAP);

#endif
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
snmp_collator_create_semaphore()
{
    /* First just try to create the semaphore.  This should usually just work. */
    if ((stats_sem = sem_open(stats_sem_name, O_CREAT | O_EXCL, SLAPD_DEFAULT_FILE_MODE, 1)) == SEM_FAILED) {
        if (errno == EEXIST) {
            /* It appears that we didn't exit cleanly last time and left the semaphore
             * around.  Recreate it since we don't know what state it is in. */
            if (sem_unlink(stats_sem_name) != 0) {
                LDAPDebug( LDAP_DEBUG_ANY, "Failed to delete old semaphore for stats file (%s). "
                          "Error %d (%s).\n", szStatsFile, errno, slapd_system_strerror(errno) );
                exit(1);
            }

            if ((stats_sem = sem_open(stats_sem_name, O_CREAT | O_EXCL, SLAPD_DEFAULT_FILE_MODE, 1)) == SEM_FAILED) {
                /* No dice */
                LDAPDebug( LDAP_DEBUG_ANY, "Failed to create semaphore for stats file (%s). Error %d (%s).\n",
                         szStatsFile, errno, slapd_system_strerror(errno) );
                exit(1);
            }
        } else {
            /* Some other problem occurred creating the semaphore. */
            LDAPDebug( LDAP_DEBUG_ANY, "Failed to create semaphore for stats file (%s). Error %d.(%s)\n",
                     szStatsFile, errno, slapd_system_strerror(errno) );
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
snmp_collator_sem_wait()
{
    int i = 0;
    int got_sem = 0;

    if (SEM_FAILED == stats_sem) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY, 
           "semaphore for stats file (%s) is not available.\n", szStatsFile);
        return;
    }

    for (i=0; i < SNMP_NUM_SEM_WAITS; i++) {
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
snmp_collator_update(time_t start_time, void *arg)
{
    if (snmp_collator_stopped) {
        return;
    }

    /* force an update of the backend cache stats. */
    snmp_update_cache_stats();

    /* get the semaphore */
    snmp_collator_sem_wait();

    /* just update the update time in the header */
    if( stats != NULL){
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
snmp_update_ops_table()
{
    stats->ops_stats.dsAnonymousBinds = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds);
    stats->ops_stats.dsUnAuthBinds = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds);
    stats->ops_stats.dsSimpleAuthBinds = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSimpleAuthBinds);
    stats->ops_stats.dsStrongAuthBinds = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsStrongAuthBinds);
    stats->ops_stats.dsBindSecurityErrors = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors);
    stats->ops_stats.dsInOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsInOps);
    stats->ops_stats.dsReadOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReadOps);
    stats->ops_stats.dsCompareOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsCompareOps);
    stats->ops_stats.dsAddEntryOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsAddEntryOps);
    stats->ops_stats.dsRemoveEntryOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsRemoveEntryOps);
    stats->ops_stats.dsModifyEntryOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsModifyEntryOps);
    stats->ops_stats.dsModifyRDNOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsModifyRDNOps);
    stats->ops_stats.dsListOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsListOps);
    stats->ops_stats.dsSearchOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSearchOps);
    stats->ops_stats.dsOneLevelSearchOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsOneLevelSearchOps);
    stats->ops_stats.dsWholeSubtreeSearchOps = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsWholeSubtreeSearchOps);
    stats->ops_stats.dsReferrals = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReferrals);
    stats->ops_stats.dsChainings = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsChainings);
    stats->ops_stats.dsSecurityErrors = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSecurityErrors);
    stats->ops_stats.dsErrors = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsErrors);
    stats->ops_stats.dsConnections = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnections);
    stats->ops_stats.dsConnectionSeq = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnectionSeq);
    stats->ops_stats.dsConnectionsInMaxThreads = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
    stats->ops_stats.dsMaxThreadsHit = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsMaxThreadsHit);
    stats->ops_stats.dsBytesRecv = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBytesRecv);
    stats->ops_stats.dsBytesSent = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBytesSent);
    stats->ops_stats.dsEntriesReturned = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsEntriesReturned);
    stats->ops_stats.dsReferralsReturned = slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReferralsReturned);
}

/*
 * snmp_update_entries_table()
 *
 * Updated the mmap'd entries table.  The semaphore should
 * be acquired before you call this.
 */
static void
snmp_update_entries_table()
{
    stats->entries_stats.dsMasterEntries = slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsMasterEntries);
    stats->entries_stats.dsCopyEntries = slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCopyEntries);
    stats->entries_stats.dsCacheEntries = slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCacheEntries);
    stats->entries_stats.dsCacheHits = slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCacheHits);
    stats->entries_stats.dsSlaveHits = slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsSlaveHits);
}

/*
 * snmp_update_interactions_table()
 *
 * Updates the mmap'd interactions table.  The semaphore should
 * be acquired before you call this.
 */
static void
snmp_update_interactions_table()
{
    int i;

    for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++) {
        stats->int_stats[i].dsIntIndex = i;
        strncpy(stats->int_stats[i].dsName, g_get_global_snmp_vars()->int_tbl[i].dsName,
                sizeof(stats->int_stats[i].dsName));
        stats->int_stats[i].dsTimeOfCreation = g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation;
        stats->int_stats[i].dsTimeOfLastAttempt = g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt;
        stats->int_stats[i].dsTimeOfLastSuccess = g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess;
        stats->int_stats[i].dsFailuresSinceLastSuccess = g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess;
        stats->int_stats[i].dsFailures = g_get_global_snmp_vars()->int_tbl[i].dsFailures;
        stats->int_stats[i].dsSuccesses = g_get_global_snmp_vars()->int_tbl[i].dsSuccesses;
        strncpy(stats->int_stats[i].dsURL, g_get_global_snmp_vars()->int_tbl[i].dsURL,
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
snmp_update_cache_stats()
{
    Slapi_Backend       *be, *be_next;
    char                *cookie = NULL;
    Slapi_PBlock        *search_result_pb = NULL;
    Slapi_Entry         **search_entries;
    int                 search_result;

    /* set the cache hits/cache entries info */
    be = slapi_get_first_backend(&cookie);
    if (!be){
    	slapi_ch_free ((void **) &cookie);
        return;
    }

    be_next = slapi_get_next_backend(cookie);

    slapi_ch_free ((void **) &cookie);

    /* for now, only do it if there is only 1 backend, otherwise don't know 
     * which backend to pick */
    if(be_next == NULL)
    {
        Slapi_DN monitordn;
        slapi_sdn_init(&monitordn);
        be_getmonitordn(be,&monitordn);
   
        /* do a search on the monitor dn to get info */
        search_result_pb = slapi_search_internal( slapi_sdn_get_dn(&monitordn),
                LDAP_SCOPE_BASE,
                "objectclass=*", 
                NULL,
                NULL,
                0);
        slapi_sdn_done(&monitordn);

        slapi_pblock_get( search_result_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);

        if(search_result == 0)
        {
            slapi_pblock_get( search_result_pb,SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                    &search_entries);

            /* set the entrycachehits */
            slapi_counter_set_value(g_get_global_snmp_vars()->entries_tbl.dsCacheHits,
                    slapi_entry_attr_get_ulonglong(search_entries[0], "entrycachehits"));
		    
            /* set the currententrycachesize */
            slapi_counter_set_value(g_get_global_snmp_vars()->entries_tbl.dsCacheEntries,
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
	PR_snprintf(value,sizeof(value),"%" NSPRIu64, (long long unsigned int)countervalue);
	slapi_entry_attr_set_charptr( e, type, value);
}

void
snmp_as_entry(Slapi_Entry *e)
{
	add_counter_to_value(e,"AnonymousBinds", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds));
	add_counter_to_value(e,"UnAuthBinds", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds));
	add_counter_to_value(e,"SimpleAuthBinds", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSimpleAuthBinds));
	add_counter_to_value(e,"StrongAuthBinds", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsStrongAuthBinds));
	add_counter_to_value(e,"BindSecurityErrors", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors));
	add_counter_to_value(e,"InOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsInOps));
	add_counter_to_value(e,"ReadOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReadOps));
	add_counter_to_value(e,"CompareOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsCompareOps));
	add_counter_to_value(e,"AddEntryOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsAddEntryOps));
	add_counter_to_value(e,"RemoveEntryOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsRemoveEntryOps));
	add_counter_to_value(e,"ModifyEntryOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsModifyEntryOps));
	add_counter_to_value(e,"ModifyRDNOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsModifyRDNOps));
	add_counter_to_value(e,"ListOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsListOps));
	add_counter_to_value(e,"SearchOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSearchOps));
	add_counter_to_value(e,"OneLevelSearchOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsOneLevelSearchOps));
	add_counter_to_value(e,"WholeSubtreeSearchOps", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsWholeSubtreeSearchOps));
	add_counter_to_value(e,"Referrals", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReferrals));
	add_counter_to_value(e,"Chainings", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsChainings));
	add_counter_to_value(e,"SecurityErrors", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsSecurityErrors));
	add_counter_to_value(e,"Errors", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsErrors));
	add_counter_to_value(e,"Connections", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnections));
	add_counter_to_value(e,"ConnectionSeq", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnectionSeq));
	add_counter_to_value(e,"ConnectionsInMaxThreads", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads));
	add_counter_to_value(e,"ConnectionsMaxThreadsCount", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsMaxThreadsHit));
	add_counter_to_value(e,"BytesRecv", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBytesRecv));
	add_counter_to_value(e,"BytesSent", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsBytesSent));
	add_counter_to_value(e,"EntriesReturned", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsEntriesReturned));
	add_counter_to_value(e,"ReferralsReturned", slapi_counter_get_value(g_get_global_snmp_vars()->ops_tbl.dsReferralsReturned));
	add_counter_to_value(e,"MasterEntries", slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsMasterEntries));
	add_counter_to_value(e,"CopyEntries", slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCopyEntries));
	add_counter_to_value(e,"CacheEntries", slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCacheEntries));
	add_counter_to_value(e,"CacheHits", slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsCacheHits));
	add_counter_to_value(e,"SlaveHits", slapi_counter_get_value(g_get_global_snmp_vars()->entries_tbl.dsSlaveHits));
}

/*
 * loadConfigStats()
 *
 * Reads the header table SNMP settings and sets them in the mmap'd stats
 * file.  This should be done only when the semaphore is held.
 */
static void
loadConfigStats() {
	Slapi_Entry *entry = NULL;
	char *name = NULL;
	char *desc = NULL;
	char *org = NULL;
	char *loc = NULL;
	char *contact = NULL;

	/* Read attributes from SNMP config entry */
        getConfigEntry( &entry );
        if ( entry != NULL ) {
		name = slapi_entry_attr_get_charptr( entry, SNMP_NAME_ATTR );
		desc = slapi_entry_attr_get_charptr( entry, SNMP_DESC_ATTR );
		org = slapi_entry_attr_get_charptr( entry, SNMP_ORG_ATTR );
		loc = slapi_entry_attr_get_charptr( entry, SNMP_LOC_ATTR );
		contact = slapi_entry_attr_get_charptr( entry, SNMP_CONTACT_ATTR );
		freeConfigEntry( &entry );
        }

	/* Load stats into table */
        if ( name != NULL) {
		PL_strncpyz(stats->hdr_stats.dsName, name, SNMP_FIELD_LENGTH);
        }

	if ( desc != NULL) {
		PL_strncpyz(stats->hdr_stats.dsDescription, desc, SNMP_FIELD_LENGTH);
	}

	if ( org != NULL) {
		PL_strncpyz(stats->hdr_stats.dsOrganization, org, SNMP_FIELD_LENGTH);
	}

	if ( loc != NULL) {
		PL_strncpyz(stats->hdr_stats.dsLocation, loc, SNMP_FIELD_LENGTH);
	}

	if ( contact != NULL) {
		PL_strncpyz(stats->hdr_stats.dsContact, contact, SNMP_FIELD_LENGTH);
	}

	/* Free strings */
	slapi_ch_free((void **) &name);
	slapi_ch_free((void **) &desc);
	slapi_ch_free((void **) &org);
	slapi_ch_free((void **) &loc);
	slapi_ch_free((void **) &contact);
}

static Slapi_Entry *
getConfigEntry( Slapi_Entry **e ) {
        Slapi_DN        sdn;

        /* SNMP_CONFIG_DN: no need to be normalized */
        slapi_sdn_init_normdn_byref( &sdn, SNMP_CONFIG_DN );
        slapi_search_internal_get_entry( &sdn, NULL, e,
                        plugin_get_default_component_id());
        slapi_sdn_done( &sdn );
        return *e;
}

static void
freeConfigEntry( Slapi_Entry **e ) {
        if ( (e != NULL) && (*e != NULL) ) {
                slapi_entry_free( *e );
                *e = NULL;
        }
}

