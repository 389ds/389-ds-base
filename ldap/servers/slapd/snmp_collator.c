/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef  _WIN32
#include <sys/ipc.h>
#include <sys/msg.h>
#include <dirent.h>
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

#include "snmp_collator.h" 
#include "../snmp/ntagt/nslagtcom_nt.h"

/* stevross: safe to assume port should be at most 5 digits ? */
#define PORT_LEN 5
/* strlen of url portions ie "ldap://:/" */
#define URL_CHARS_LEN 9 

char *make_ds_url(char *host, int port);
void print_snmp_interaction_table();
int search_interaction_table(char *dsURL, int *isnew);

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
#endif /* _WIN32*/
static Slapi_Eq_Context snmp_eq_ctx;
static int snmp_collator_stopped = 0;

/* lock stuff */
static PRLock 		*interaction_table_mutex;


/***********************************************************************************
*
* int snmp_collator_init()
*
*	initializes the global variables used by snmp
*
************************************************************************************/

int snmp_collator_init(){
 int i;

	/* 
	* Initialize the mmap structure 
	*/
	memset((void *) stats, 0, sizeof(*stats));
	stats->hdr_stats.hdrVersionMjr = AGT_MJR_VERSION;
	stats->hdr_stats.hdrVersionMnr = AGT_MNR_VERSION;
	stats->hdr_stats.restarted = 0;			 
	stats->hdr_stats.startTime = time(0);		/* This is a bit off, hope it's ok */

	/* point these at the mmaped data */
	g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds		= &(stats->ops_stats.dsAnonymousBinds);
    g_get_global_snmp_vars()->ops_tbl.dsUnAuthBinds			= &(stats->ops_stats.dsUnAuthBinds);
	g_get_global_snmp_vars()->ops_tbl.dsSimpleAuthBinds		= &(stats->ops_stats.dsSimpleAuthBinds);
	g_get_global_snmp_vars()->ops_tbl.dsStrongAuthBinds		= &(stats->ops_stats.dsStrongAuthBinds);
	g_get_global_snmp_vars()->ops_tbl.dsBindSecurityErrors	        = &(stats->ops_stats.dsBindSecurityErrors);
	g_get_global_snmp_vars()->ops_tbl.dsInOps			= &(stats->ops_stats.dsInOps);
    g_get_global_snmp_vars()->ops_tbl.dsReadOps			= &(stats->ops_stats.dsReadOps);
    g_get_global_snmp_vars()->ops_tbl.dsCompareOps			= &(stats->ops_stats.dsCompareOps);
	g_get_global_snmp_vars()->ops_tbl.dsAddEntryOps			= &(stats->ops_stats.dsAddEntryOps);
	g_get_global_snmp_vars()->ops_tbl.dsRemoveEntryOps		= &(stats->ops_stats.dsRemoveEntryOps); 
	g_get_global_snmp_vars()->ops_tbl.dsModifyEntryOps		= &(stats->ops_stats.dsModifyEntryOps);
	g_get_global_snmp_vars()->ops_tbl.dsModifyRDNOps		= &(stats->ops_stats.dsModifyRDNOps);
	g_get_global_snmp_vars()->ops_tbl.dsListOps			= &(stats->ops_stats.dsListOps);
	g_get_global_snmp_vars()->ops_tbl.dsSearchOps			= &(stats->ops_stats.dsSearchOps);
	g_get_global_snmp_vars()->ops_tbl.dsOneLevelSearchOps		= &(stats->ops_stats.dsOneLevelSearchOps);
	g_get_global_snmp_vars()->ops_tbl.dsWholeSubtreeSearchOps       = &(stats->ops_stats.dsWholeSubtreeSearchOps);
	g_get_global_snmp_vars()->ops_tbl.dsReferrals			= &(stats->ops_stats.dsReferrals);
	g_get_global_snmp_vars()->ops_tbl.dsChainings			= &(stats->ops_stats.dsChainings);
	g_get_global_snmp_vars()->ops_tbl.dsSecurityErrors	        = &(stats->ops_stats.dsSecurityErrors);
	g_get_global_snmp_vars()->ops_tbl.dsErrors			= &(stats->ops_stats.dsErrors);
	g_get_global_snmp_vars()->ops_tbl.dsConnections			= &(stats->ops_stats.dsConnections);
	g_get_global_snmp_vars()->ops_tbl.dsConnectionSeq			= &(stats->ops_stats.dsConnectionSeq);
	g_get_global_snmp_vars()->ops_tbl.dsBytesRecv			= &(stats->ops_stats.dsBytesRecv);
	g_get_global_snmp_vars()->ops_tbl.dsBytesSent			= &(stats->ops_stats.dsBytesSent);
	g_get_global_snmp_vars()->ops_tbl.dsEntriesReturned			= &(stats->ops_stats.dsEntriesReturned);
	g_get_global_snmp_vars()->ops_tbl.dsReferralsReturned			= &(stats->ops_stats.dsReferralsReturned);

	/* entries table */

	g_get_global_snmp_vars()->entries_tbl.dsMasterEntries		= &(stats->entries_stats.dsMasterEntries);
	g_get_global_snmp_vars()->entries_tbl.dsCopyEntries		= &(stats->entries_stats.dsCopyEntries);
	g_get_global_snmp_vars()->entries_tbl.dsCacheEntries		= &(stats->entries_stats.dsCacheEntries);
	g_get_global_snmp_vars()->entries_tbl.dsCacheHits		= &(stats->entries_stats.dsCacheHits);
	g_get_global_snmp_vars()->entries_tbl.dsSlaveHits		= &(stats->entries_stats.dsSlaveHits);

	/* interaction table */

	/* set pointers to table */
	for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	{
	    stats->int_stats[i].dsIntIndex=i;
	    g_get_global_snmp_vars()->int_tbl[i].dsIntIndex		 = &(stats->int_stats[i].dsIntIndex);
	    g_get_global_snmp_vars()->int_tbl[i].dsName			 = stats->int_stats[i].dsName;
	    g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation	 = &(stats->int_stats[i].dsTimeOfCreation);     
	    g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt	 = &(stats->int_stats[i].dsTimeOfLastAttempt);
	    g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess	 = &(stats->int_stats[i].dsTimeOfLastSuccess);
	    g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess
	      = &(stats->int_stats[i].dsFailuresSinceLastSuccess);
	    g_get_global_snmp_vars()->int_tbl[i].dsFailures		 = &(stats->int_stats[i].dsFailures);
	    g_get_global_snmp_vars()->int_tbl[i].dsSuccesses		 = &(stats->int_stats[i].dsSuccesses);
	    g_get_global_snmp_vars()->int_tbl[i].dsURL			 = stats->int_stats[i].dsURL;
	}

	/* initialize table contents */
	for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
	{
            *(g_get_global_snmp_vars()->int_tbl[i].dsIntIndex)                    = i + 1;
            strcpy(g_get_global_snmp_vars()->int_tbl[i].dsName, "Not Available");
            *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation)              = 0;
            *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt)           = 0;
            *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess)           = 0;
            *(g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess)    = 0;
            *(g_get_global_snmp_vars()->int_tbl[i].dsFailures)                    = 0;
            *(g_get_global_snmp_vars()->int_tbl[i].dsSuccesses)                   = 0;
	    strcpy(g_get_global_snmp_vars()->int_tbl[i].dsURL, "Not Available");
	}

	/* create lock for interaction table */
        interaction_table_mutex = PR_NewLock();

	return 0;
}



/***********************************************************************************
 * given the name, wether or not it was successfull and the URL updates snmp 
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
          *(g_get_global_snmp_vars()->int_tbl[index].dsIntIndex)	                  = index;
          strcpy(g_get_global_snmp_vars()->int_tbl[index].dsName, dsName);
          *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfCreation)	          = time(0);
          *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastAttempt)	          = time(0);
          if(error == 0){
              *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess)	  = time(0);
              *(g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess)  = 0;
              *(g_get_global_snmp_vars()->int_tbl[index].dsFailures)	          = 0;
              *(g_get_global_snmp_vars()->int_tbl[index].dsSuccesses)		  = 1;
          }else{
              *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess)	  = 0;
              *(g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess)  = 1;
              *(g_get_global_snmp_vars()->int_tbl[index].dsFailures)		  = 1;
              *(g_get_global_snmp_vars()->int_tbl[index].dsSuccesses)		  = 0;
          }
          strcpy(g_get_global_snmp_vars()->int_tbl[index].dsURL, dsURL);		         
      }else{
        /* just update the appropriate fields */
           *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastAttempt)	          = time(0);
          if(error == 0){
             *(g_get_global_snmp_vars()->int_tbl[index].dsTimeOfLastSuccess)	  = time(0);
             *(g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess)   = 0;
             *(g_get_global_snmp_vars()->int_tbl[index].dsSuccesses)                  += 1;
          }else{
             *(g_get_global_snmp_vars()->int_tbl[index].dsFailuresSinceLastSuccess)   +=1;
             *(g_get_global_snmp_vars()->int_tbl[index].dsFailures)                   +=1;
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
char *make_ds_url(char *host, int port){

  char *url;
  
   url = (char *)slapi_ch_malloc( (strlen(host) + PORT_LEN + URL_CHARS_LEN + 1) * sizeof(char));
 
   sprintf(url,"ldap://%s:%d/",host, port);
 
   return url;
}


/***********************************************************************************
 * searches the table for the url specified 
 * If there, returns index to update stats
 * if, not there returns index of oldest interaction, and isnew flag is set
 * so caller can rewrite this row
************************************************************************************/

int search_interaction_table(char *dsURL, int *isnew)
{
  int i;
  int index = 0;
  time_t oldestattempt;
  time_t currentattempt;
   
   oldestattempt = *(g_get_global_snmp_vars()->int_tbl[0].dsTimeOfLastAttempt);
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
          currentattempt = *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt);
	 
          if(currentattempt <= oldestattempt){
              index=i;
	      oldestattempt = currentattempt;
          }
       }

   }
   
   return index;

}
/* for debuging until subagent part working, print contents of interaction table */
void print_snmp_interaction_table()
{ 
  int i;
  for(i=0; i < NUM_SNMP_INT_TBL_ROWS; i++)
  {
    fprintf(stderr, "                dsIntIndex: %d \n", *(g_get_global_snmp_vars()->int_tbl[i].dsIntIndex));
    fprintf(stderr, "                    dsName: %s \n",   g_get_global_snmp_vars()->int_tbl[i].dsName);
    fprintf(stderr, "          dsTimeOfCreation: %ld \n", *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfCreation));
    fprintf(stderr, "       dsTimeOfLastAttempt: %ld \n", *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastAttempt));
    fprintf(stderr, "       dsTimeOfLastSuccess: %ld \n", *(g_get_global_snmp_vars()->int_tbl[i].dsTimeOfLastSuccess));
    fprintf(stderr, "dsFailuresSinceLastSuccess: %d \n", *(g_get_global_snmp_vars()->int_tbl[i].dsFailuresSinceLastSuccess));
    fprintf(stderr, "                dsFailures: %d \n", *(g_get_global_snmp_vars()->int_tbl[i].dsFailures));
    fprintf(stderr, "               dsSuccesses: %d \n", *(g_get_global_snmp_vars()->int_tbl[i].dsSuccesses));
    fprintf(stderr, "                     dsURL: %s \n", g_get_global_snmp_vars()->int_tbl[i].dsURL);
    fprintf(stderr, "\n");
  }
}

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
  char *instancedir = config_get_instancedir();

	/*
	 * Get directory for our stats file
	 */

  sprintf(szStatsFile, "%s/logs/%s", instancedir, 
		  AGT_STATS_FILE);
  tmpstatsfile = szStatsFile;

  slapi_ch_free((void **) &instancedir);


	  /* open the memory map */
	  
	  if ((err = agt_mopen_stats(tmpstatsfile, O_RDWR,  &hdl) != 0))
	  {
	      if (err != EEXIST)			/* Ignore if file already exists */
	      {
	          printf("Failed to open stats file (%s) (error %d).\n", 
	                 AGT_STATS_FILE, err);
           
	          exit(1);
	      }
	  }



/* point stats struct at mmap data */
	 stats = (struct agt_stats_t *) mmap_tbl [hdl].fp;

/* initialize stats data */

	snmp_collator_init();
/*
*	now that memmap is open and things point the right way
*    an atomic set or increment anywhere in slapd should set
*	the snmp memmap vars correctly and be able to be polled by snmp
*/

	/* Arrange to be called back periodically */
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

	/* Abort any pending events */
	slapi_eq_cancel(snmp_eq_ctx);
	snmp_collator_stopped = 1;

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

   /* delete lock */
   PR_DestroyLock(interaction_table_mutex);

#ifdef _WIN32
   /* send the event so server down trap gets set on NT */
   sc_setevent(MAGT_NSEV_SNMPTRAP);

#endif
   /* stevross: I probably need to free stats too... make sure to add that later */
  
return 0;
}



/***********************************************************************************
*
* int snmp_collator_update()
*
*    our architecture changed from mail server and we right to mmapped 
*    area as soon as operation completed, rather than maintining the same data twice  
*    and doing a polled update. However, to keep traps working correctly (as they depend)
*    on the time in the header, it is more efficient to write the header info
*    in a polled fashion (ever 1 sec)
*
************************************************************************************/

void
snmp_collator_update(time_t start_time, void *arg)
{
    Slapi_Backend	*be, *be_next;
    char			*cookie = NULL;
    Slapi_PBlock	*search_result_pb = NULL;
    Slapi_Entry     **search_entries;
    Slapi_Attr      *attr  = NULL;
    Slapi_Value     *sval = NULL;
    int             search_result;
    
	if (snmp_collator_stopped) {
		return;
	}

    /* just update the update time in the header */
    if( stats != NULL){
        stats->hdr_stats.updateTime = time(0);		
    }

    /* set the cache hits/cache entries info */
	be = slapi_get_first_backend(&cookie);
	if (!be)
		return;

	be_next = slapi_get_next_backend(cookie);

	slapi_ch_free ((void **) &cookie);

	/* for now, only do it if there is only 1 backend, otherwise don't know 
       which backend to pick */
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
		    const struct berval *val = NULL;
            /* get the entrycachehits */
		    slapi_pblock_get( search_result_pb,SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
				    &search_entries);
		    if(slapi_entry_attr_find( search_entries[0], "entrycachehits", &attr)  == 0 )
		    {
		        /* get the values out of the attribute */
				val = NULL;
				slapi_attr_first_value( attr, &sval );
				if(NULL != sval)
				{
				    val= slapi_value_get_berval( sval );
				}
		    }		       

		    /* if we got a value for entrycachehits, then set it */
		    if(val != NULL)
		    {
		        PR_AtomicSet(g_get_global_snmp_vars()->entries_tbl.dsCacheHits, atoi(val->bv_val));

		    }
		    
		    /* get the currententrycachesize */
		    attr = NULL;
		    val = NULL;
		    sval = NULL;
		    if(slapi_entry_attr_find( search_entries[0], "currententrycachesize", &attr)  == 0 )
		    {
		        /* get the values out of the attribute */
		        slapi_attr_first_value( attr,&sval );
				if(NULL != sval) {
				    val= slapi_value_get_berval( sval );
				}
		    }		       

		    /* if we got a value for currententrycachesize, then set it */
		    if(val != NULL)
		    {
		        PR_AtomicSet(g_get_global_snmp_vars()->entries_tbl.dsCacheEntries, atoi(val->bv_val));

		    }

		}
		
		slapi_free_search_results_internal(search_result_pb);
	    slapi_pblock_destroy(search_result_pb);
    }
}

static void
add_counter_to_value(Slapi_Entry *e, const char *type, int countervalue)
{
	char value[40];
	sprintf(value,"%d",countervalue);
	slapi_entry_attr_set_charptr( e, type, value);
}

void
snmp_as_entry(Slapi_Entry *e)
{
	add_counter_to_value(e,"AnonymousBinds",stats->ops_stats.dsAnonymousBinds);
	add_counter_to_value(e,"UnAuthBinds",stats->ops_stats.dsUnAuthBinds);
	add_counter_to_value(e,"SimpleAuthBinds",stats->ops_stats.dsSimpleAuthBinds);
	add_counter_to_value(e,"StrongAuthBinds",stats->ops_stats.dsStrongAuthBinds);
	add_counter_to_value(e,"BindSecurityErrors",stats->ops_stats.dsBindSecurityErrors);
	add_counter_to_value(e,"InOps",stats->ops_stats.dsInOps);
	add_counter_to_value(e,"ReadOps",stats->ops_stats.dsReadOps);
	add_counter_to_value(e,"CompareOps",stats->ops_stats.dsCompareOps);
	add_counter_to_value(e,"AddEntryOps",stats->ops_stats.dsAddEntryOps);
	add_counter_to_value(e,"RemoveEntryOps",stats->ops_stats.dsRemoveEntryOps);
	add_counter_to_value(e,"ModifyEntryOps",stats->ops_stats.dsModifyEntryOps);
	add_counter_to_value(e,"ModifyRDNOps",stats->ops_stats.dsModifyRDNOps);
	add_counter_to_value(e,"ListOps",stats->ops_stats.dsListOps);
	add_counter_to_value(e,"SearchOps",stats->ops_stats.dsSearchOps);
	add_counter_to_value(e,"OneLevelSearchOps",stats->ops_stats.dsOneLevelSearchOps);
	add_counter_to_value(e,"WholeSubtreeSearchOps",stats->ops_stats.dsWholeSubtreeSearchOps);
	add_counter_to_value(e,"Referrals",stats->ops_stats.dsReferrals);
	add_counter_to_value(e,"Chainings",stats->ops_stats.dsChainings);
	add_counter_to_value(e,"SecurityErrors",stats->ops_stats.dsSecurityErrors);
	add_counter_to_value(e,"Errors",stats->ops_stats.dsErrors);
	add_counter_to_value(e,"Connections",stats->ops_stats.dsConnections);
	add_counter_to_value(e,"ConnectionSeq",stats->ops_stats.dsConnectionSeq);
	add_counter_to_value(e,"BytesRecv",stats->ops_stats.dsBytesRecv);
	add_counter_to_value(e,"BytesSent",stats->ops_stats.dsBytesSent);
	add_counter_to_value(e,"EntriesReturned",stats->ops_stats.dsEntriesReturned);
	add_counter_to_value(e,"ReferralsReturned",stats->ops_stats.dsReferralsReturned);
	add_counter_to_value(e,"MasterEntries",stats->entries_stats.dsMasterEntries);
	add_counter_to_value(e,"CopyEntries",stats->entries_stats.dsCopyEntries);
	add_counter_to_value(e,"CacheEntries",stats->entries_stats.dsCacheEntries);
	add_counter_to_value(e,"CacheHits",stats->entries_stats.dsCacheHits);
	add_counter_to_value(e,"SlaveHits",stats->entries_stats.dsSlaveHits);
}









