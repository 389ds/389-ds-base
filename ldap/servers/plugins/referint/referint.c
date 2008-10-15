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
#include <string.h>
#include "portable.h"
#include "slapi-plugin.h"
#include "slap.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"

/* include NSPR header files */
#include "prthread.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "prio.h"

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define REFERINT_PLUGIN_SUBSYSTEM   "referint-plugin"   /* used for logging */

#ifdef _WIN32
#define REFERINT_DEFAULT_FILE_MODE	0
#else
#define REFERINT_DEFAULT_FILE_MODE S_IRUSR | S_IWUSR
#endif


#define MAX_LINE 2048
#define READ_BUFSIZE  4096
#define MY_EOF   0 

/* function prototypes */
int referint_postop_init( Slapi_PBlock *pb ); 
int referint_postop_del( Slapi_PBlock *pb ); 
int referint_postop_modrdn( Slapi_PBlock *pb ); 
int referint_postop_start( Slapi_PBlock *pb);
int referint_postop_close( Slapi_PBlock *pb);
int update_integrity(char **argv, char *origDN, char *newrDN, int logChanges);
void referint_thread_func(void *arg);
int  GetNextLine(char *dest, int size_dest, PRFileDesc *stream);
void writeintegritylog(char *logfilename, char *dn, char *newrdn);
int my_fgetc(PRFileDesc *stream);

/* global thread control stuff */

static PRLock 		*referint_mutex = NULL;       
static PRThread		*referint_tid = NULL;
int keeprunning = 0;

static PRLock 		*keeprunning_mutex = NULL; 
static PRCondVar        *keeprunning_cv = NULL; 

static Slapi_PluginDesc pdesc = { "referint", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"referential integrity plugin" };

static void* referint_plugin_identity = NULL;

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

int
referint_postop_init( Slapi_PBlock *pb )
{

	/*
	 * Get plugin identity and stored it for later use
	 * Used for internal operations
	 */

    	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &referint_plugin_identity);
    	PR_ASSERT (referint_plugin_identity);

	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	  slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                     (void *)&pdesc ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_POST_DELETE_FN,
                     (void *) referint_postop_del ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                     (void *) referint_postop_modrdn ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	         (void *) referint_postop_start ) != 0 ||
	     slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
			         (void *) referint_postop_close ) != 0)
        {
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                             "referint_postop_init failed\n" );
            return( -1 );
        }

        return( 0 );
}


int
referint_postop_del( Slapi_PBlock *pb )
{
	char	*dn;
	int rc;
	int oprc;
	char **argv;
	int argc;
	int delay;
	int logChanges=0;
	int isrepop = 0;

	if ( slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &isrepop ) != 0  ||
		 slapi_pblock_get( pb, SLAPI_DELETE_TARGET, &dn ) != 0  ||
	     slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0) 
        {
            slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                             "referint_postop_del: could not get parameters\n" );
            return( -1 );
        }

        /* this plugin should only execute if the delete was successful
		   and this is not a replicated op
	    */
        if(oprc != 0 || isrepop)
        {
            return( 0 );
        }
	/* get args */ 
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argc\n" );
	  return( -1 ); 
	} 
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argv\n" );
	  return( -1 ); 
	} 
	
	if(argv == NULL){
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop_modrdn, args are NULL\n" );
	  return( -1 ); 
	}
	
	if (argc >= 3) {
		/* argv[0] will be the delay */
		delay = atoi(argv[0]);

		/* argv[2] will be wether or not to log changes */
		logChanges = atoi(argv[2]);

		if(delay == -1){
		  /* integrity updating is off */
		  rc = 0;
		}else if(delay == 0){
		  /* no delay */
 		  /* call function to update references to entry */
		  rc = update_integrity(argv, dn, NULL, logChanges);
		}else{
		  /* write the entry to integrity log */
		  writeintegritylog(argv[1],dn, NULL);
		  rc = 0;
		}
	} else {
		slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop insufficient arguments supplied\n" );
		return( -1 ); 
	}

	return( rc );

}

int
referint_postop_modrdn( Slapi_PBlock *pb )
{
	char	*dn;
	char	*newrdn;
	int oprc;
	int rc;
	char **argv;
	int argc = 0;
	int delay;
	int logChanges=0;
	int isrepop = 0;

	if ( slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &isrepop ) != 0  ||
		 slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn ) != 0 ||
	     slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn ) != 0 ||
     	     slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0 ){
	   
		slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		    "referint_postop_modrdn: could not get parameters\n" );
		return( -1 );
	}

	/* this plugin should only execute if the delete was successful 
	   and this is not a replicated op
	*/
	if(oprc != 0 || isrepop){
	  return( 0 );
	}
	/* get args */ 
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argv\n" ); 
	  return( -1 ); 
	} 
	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argv\n" );
	  return( -1 ); 
	} 
	
	if(argv == NULL){
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop_modrdn, args are NULL\n" ); 
	  return( -1 ); 
	}

	if (argc >= 3) {
		/* argv[0] will always be the delay */
		delay = atoi(argv[0]);

		/* argv[2] will be wether or not to log changes */
		logChanges = atoi(argv[2]);
	} else {
		slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop_modrdn insufficient arguments supplied\n" );
		return( -1 ); 
	}

	if(delay == -1){
	  /* integrity updating is off */
	  rc = 0;
	}else if(delay == 0){
	  /* no delay */
 	  /* call function to update references to entry */
	  rc = update_integrity(argv, dn, newrdn, logChanges);
	}else{
	  /* write the entry to integrity log */
	  writeintegritylog(argv[1],dn, newrdn);
	  rc = 0;
	}

	return( rc );
}

int isFatalSearchError(int search_result)
{

    /*   Make sure search result is fatal 
     *   Some conditions that happen quite often are not fatal 
     *   for example if you have two suffixes and one is null, you will always
     *   get no such object, howerever this is not a fatal error. 
     *   Add other conditions to the if statement as they are found
     */

	/* NPCTE fix for bugid 531225, esc 0. <P.R> <30-May-2001> */
	switch(search_result) {
		case LDAP_REFERRAL: 
		case LDAP_NO_SUCH_OBJECT: return 0 ;
	}
	return 1;
	 /* end of NPCTE fix for bugid 531225 */

}

int update_integrity(char **argv, char *origDN, char *newrDN, int logChanges){

  Slapi_PBlock *search_result_pb = NULL;
  Slapi_PBlock *mod_result_pb = NULL;
  Slapi_Entry		**search_entries = NULL;
  int search_result;
  Slapi_DN *sdn = NULL;
  void *node = NULL;
  LDAPMod attribute1, attribute2;
  const LDAPMod *list_of_mods[3];
  char *values_del[2];
  char *values_add[2];
  char *filter = NULL;
  int i, j;
  const char *search_base = NULL;
  char *newDN=NULL;
  char **dnParts=NULL;
  int dnsize;
  int x;
  int rc;
 
  if ( argv == NULL ) { 
      slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
          "referint_postop required config file arguments missing\n" );
      rc = -1;
      goto free_and_return;
  } 

  /* for now, just putting attributes to keep integrity on in conf file,
     until resolve the other timing mode issue */
  
  /* Search each namingContext in turn */
    for ( sdn = slapi_get_first_suffix( &node, 0 ); sdn != NULL;
		sdn = slapi_get_next_suffix( &node, 0 ))
    {
	  search_base = slapi_sdn_get_dn( sdn );
  
			
      for(i=3; argv[i] != NULL; i++)
      {
          unsigned long filtlen = strlen(argv[i]) + (strlen(origDN) * 3 ) + 4;
          filter = (char *)slapi_ch_calloc( filtlen, sizeof(char ));  
	  if (( search_result = ldap_create_filter( filter, filtlen, "(%a=%e)",
                  NULL, NULL, argv[i], origDN, NULL )) == LDAP_SUCCESS ) {

		/* Don't need any attribute */
		char * attrs[2];
		attrs[0]="1.1";
		attrs[1]=NULL;

		/* Use new search API */
      		search_result_pb = slapi_pblock_new();
      		slapi_search_internal_set_pb(search_result_pb, search_base, LDAP_SCOPE_SUBTREE,
        		filter, attrs, 0 /* attrs only */, NULL,NULL,referint_plugin_identity,0);
      		slapi_search_internal_pb(search_result_pb);

                slapi_pblock_get( search_result_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
	  }


          /* if search successfull then do integrity update */
          if(search_result == 0)
	  {
	      slapi_pblock_get( search_result_pb,SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
			 &search_entries);

	       for(j=0; search_entries[j] != NULL; j++)
	       {
	           /* no matter what mode in always going to delete old dn so set that up */
	           values_del[0]= origDN;
	           values_del[1]= NULL;
	           attribute1.mod_type = argv[i];
	           attribute1.mod_op = LDAP_MOD_DELETE;
	           attribute1.mod_values = values_del;
	           list_of_mods[0] = &attribute1;

	           if(newrDN == NULL){
	               /* in delete mode so terminate list of mods cause this is the only one */
	               list_of_mods[1] = NULL;
         	   }else if(newrDN != NULL){
	               /* in modrdn mode */

		       /* need to put together rdn into a dn */
		       dnParts = ldap_explode_dn( origDN, 0 );
	     
		       /* skip original rdn so start at 1*/  
		       dnsize = 0;
		       for(x=1; dnParts[x] != NULL; x++)
		       { 
			   /* +2 for space and comma adding later */
			   dnsize += strlen(dnParts[x]) + 2;    
		       }
		       /* add the newrDN length */
		       dnsize += strlen(newrDN) + 1;

		       newDN = slapi_ch_calloc(dnsize, sizeof(char));
		       strcat(newDN, newrDN);
		       for(x=1; dnParts[x] != NULL; x++)
		       { 
			   strcat(newDN, ", ");
			   strcat(newDN, dnParts[x]);
		       }
	     
		       values_add[0]=newDN;
		       values_add[1]=NULL;
		       attribute2.mod_type = argv[i];
		       attribute2.mod_op = LDAP_MOD_ADD;
		       attribute2.mod_values = values_add;

		       /* add the new dn to list of mods and terminate list of mods */
		       list_of_mods[1] = &attribute2;
		       list_of_mods[2] = NULL;

		   }

		   /* try to cleanup entry */

		   /* Use new internal operation API */
		   mod_result_pb=slapi_pblock_new();
		   slapi_modify_internal_set_pb(mod_result_pb,slapi_entry_get_dn(search_entries[j]),
			(LDAPMod **)list_of_mods,NULL,NULL,referint_plugin_identity,0);
		   slapi_modify_internal_pb(mod_result_pb);

		   /* could check the result code here if want to log it or something later 
		      for now, continue no matter what result is */

	       slapi_pblock_destroy(mod_result_pb);

		   /* cleanup memory allocated for dnParts and newDN */
		   if(dnParts != NULL){
		       for(x=0; dnParts[x] != NULL; x++)
		       {
		           slapi_ch_free_string(&dnParts[x]);
		       }
	           slapi_ch_free((void **)&dnParts);
		   }
		   slapi_ch_free_string(&newDN);

	       }



          }else{
              if(isFatalSearchError(search_result))
              {
		      /* NPCTE fix for bugid 531225, esc 0. <P.R> <30-May-2001> */
                  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                      "referint_postop search (base=%s filter=%s) returned error %d\n", search_base,filter,search_result );
		       /* end of NPCTE fix for bugid 531225 */
                  rc = -1;
                  goto free_and_return;
              }
   	      
          }
      
	      slapi_ch_free((void**)&filter);

		  if(search_result_pb != NULL){
		      slapi_free_search_results_internal(search_result_pb);
		      slapi_pblock_destroy(search_result_pb);
			  search_result_pb= NULL;
		  }

      }
  }
  /* if got here, then everything good rc = 0 */
  rc = 0;

free_and_return:

  /* free filter and search_results_pb */
  slapi_ch_free_string(&filter);

  if(search_result_pb != NULL)
  {
      slapi_free_search_results_internal(search_result_pb);
	  slapi_pblock_destroy(search_result_pb);
  }

  return(rc);
}

int referint_postop_start( Slapi_PBlock *pb)
{

    char **argv;
	int argc = 0;
 
    /* get args */ 
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0 ) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argv\n" );
	  return( -1 ); 
    } 
    if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) { 
	  slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop failed to get argv\n" );
	  return( -1 ); 
    } 

    if(argv == NULL){
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		  "args were null in referint_postop_start\n" );
	return( -1  );
    }

    /* only bother to start the thread if you are in delay mode. 
       0  = no delay,
       -1 = integrity off */

	if (argc >= 1) {
		if(atoi(argv[0]) > 0){

		  /* initialize  cv and lock */
			 
		  referint_mutex = PR_NewLock();
		  keeprunning_mutex = PR_NewLock();
		  keeprunning_cv = PR_NewCondVar(keeprunning_mutex);
		  keeprunning =1;
			
		  referint_tid = PR_CreateThread (PR_USER_THREAD, 
							referint_thread_func, 
							(void *)argv,
							PR_PRIORITY_NORMAL, 
							PR_GLOBAL_THREAD, 
							PR_UNJOINABLE_THREAD, 
							SLAPD_DEFAULT_THREAD_STACKSIZE);
		  if ( referint_tid == NULL ) {
			slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
				   "referint_postop_start PR_CreateThread failed\n" );
			exit( 1 );
		  }
		}
	} else {
		slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		     "referint_postop_start insufficient arguments supplied\n" );
		return( -1 ); 
	}

    return(0);

}

int referint_postop_close( Slapi_PBlock *pb)
{

	/* signal the thread to exit */
	if (NULL != keeprunning_mutex) {
		PR_Lock(keeprunning_mutex);  
		keeprunning=0;
		if (NULL != keeprunning_cv) {
			PR_NotifyCondVar(keeprunning_cv);
		}
		PR_Unlock(keeprunning_mutex);  
	}

	return(0);
}

void referint_thread_func(void *arg){

    char **plugin_argv = (char **)arg;
    PRFileDesc *prfd;
    char *logfilename;
    char thisline[MAX_LINE];
    int delay;
    int no_changes;
    char delimiter[]="\t\n";
    char *ptoken;
    char *tmpdn, *tmprdn;
    int logChanges=0;
	char * iter = NULL;

    if(plugin_argv == NULL){
      slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
		 "referint_thread_func not get args \n" );
      return;
    }


    delay = atoi(plugin_argv[0]);
    logfilename = plugin_argv[1]; 
	logChanges = atoi(plugin_argv[2]);

    /* keep running this thread until plugin is signalled to close */

    while(1){ 

        no_changes=1;

        while(no_changes){

	    PR_Lock(keeprunning_mutex);
	    if(keeprunning == 0){
	        PR_Unlock(keeprunning_mutex);  
	       break;
	    }
	    PR_Unlock(keeprunning_mutex);  


	    PR_Lock(referint_mutex);
        if (( prfd = PR_Open( logfilename, PR_RDONLY,
	                          REFERINT_DEFAULT_FILE_MODE )) == NULL ) 
        {
            PR_Unlock(referint_mutex);	
            /* go back to sleep and wait for this file */
  	        PR_Lock(keeprunning_mutex);
            PR_WaitCondVar(keeprunning_cv, PR_SecondsToInterval(delay));
            PR_Unlock(keeprunning_mutex);
	    }else{
	        no_changes = 0;
	    }
	}

	/* check keep running here, because after break out of no
	 * changes loop on shutdown, also need to break out of this
	 * loop before trying to do the changes. The server
	 * will pick them up on next startup as file still exists 
	 */
        PR_Lock(keeprunning_mutex);
        if(keeprunning == 0){
	    PR_Unlock(keeprunning_mutex);  
	    break;
        }
        PR_Unlock(keeprunning_mutex);  
    
  
	while( GetNextLine(thisline, MAX_LINE, prfd) ){
	    ptoken = ldap_utf8strtok_r(thisline, delimiter, &iter);
	    tmpdn = slapi_ch_calloc(strlen(ptoken) + 1, sizeof(char));
	    strcpy(tmpdn, ptoken);

	    ptoken = ldap_utf8strtok_r (NULL, delimiter, &iter);
	    if(!strcasecmp(ptoken, "NULL")){
	        tmprdn = NULL;
	    }else{
	        tmprdn = slapi_ch_calloc(strlen(ptoken) + 1, sizeof(char));
		strcpy(tmprdn, ptoken);
	    }

      
	    update_integrity(plugin_argv, tmpdn, tmprdn, logChanges);
      
	    slapi_ch_free((void **) &tmpdn);
	    slapi_ch_free((void **) &tmprdn);	
	}

	PR_Close(prfd);
      
    /* remove the original file */
    if( PR_SUCCESS != PR_Delete(logfilename) )
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                         "referint_postop_close could not delete \"%s\"\n",
                          logfilename );
    }

	/* unlock and let other writers back at the file */
	PR_Unlock(referint_mutex);
	
	/* wait on condition here */
	PR_Lock(keeprunning_mutex);
	PR_WaitCondVar(keeprunning_cv, PR_SecondsToInterval(delay));
	PR_Unlock(keeprunning_mutex);
    }

	/* cleanup resources allocated in start  */
	if (NULL != keeprunning_mutex) {
		PR_DestroyLock(keeprunning_mutex);
	}
	if (NULL != referint_mutex) {
		PR_DestroyLock(referint_mutex);
	}
	if (NULL != keeprunning_cv) {
		PR_DestroyCondVar(keeprunning_cv);
	}


}

int my_fgetc(PRFileDesc *stream)
{
    static char buf[READ_BUFSIZE]  =  "\0";
    static int  position           =  READ_BUFSIZE;
    int         retval;
    int         err;

    /* check if we need to load the buffer */

    if( READ_BUFSIZE == position )
    {
        memset(buf, '\0', READ_BUFSIZE);
        if( ( err = PR_Read(stream, buf, READ_BUFSIZE) ) >= 0)
        {
            /* it read some data */;
            position = 0;
        }else{
            /* an error occurred */
            return err;
        }
    }
	
    /* try to read some data */
    if( '\0' == buf[position])
    {
        /* out of data, return eof */
        retval = MY_EOF;
        position = READ_BUFSIZE;
    }else{
        retval = buf[position];
        position++;
    }
        
    return retval;

}	

int
GetNextLine(char *dest, int size_dest, PRFileDesc *stream) {

    char nextchar ='\0';
    int  done     = 0;
    int  i        = 0;
	
    while(!done)
    {
        if( ( nextchar = my_fgetc(stream) ) != 0)
        {
            if( i < (size_dest - 1) ) 
            {
                dest[i] = nextchar;
                i++;

                if(nextchar == '\n')
                {
                    /* end of line reached */
                    done = 1;
                }
		             
            }else{
                /* no more room in buffer */
                done = 1;
            }
		
        }else{
            /* error or end of file */
            done = 1;
        }
    }

    dest[i] =  '\0';
    
    /* return size of string read */
    return i;
}

void writeintegritylog(char *logfilename, char *dn, char *newrdn){
    PRFileDesc *prfd;
    char buffer[MAX_LINE];
    int len_to_write = 0;
    int rc;
    /* write this record to the file */

    /* use this lock to protect file data when update integrity is occuring */
    /* should hopefully not be a big issue on concurrency */
    
    PR_Lock(referint_mutex);
    if (( prfd = PR_Open( logfilename, PR_WRONLY | PR_CREATE_FILE | PR_APPEND,
	      REFERINT_DEFAULT_FILE_MODE )) == NULL ) 
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
	       "referint_postop could not write integrity log \"%s\" "
		SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
	       logfilename, PR_GetError(), slapd_pr_strerror(PR_GetError()) );
        
        PR_Unlock(referint_mutex);
	    return;
    }

    /* make sure we have enough room in our buffer
       before trying to write it 
     */

	/* add length of dn +  4(two tabs, a newline, and terminating \0) */
    len_to_write = strlen(dn) + 4;

    if(newrdn == NULL)
    {
        /* add the length of "NULL" */
        len_to_write += 4;
    }else{
        /* add the length of the newrdn */
        len_to_write += strlen(newrdn);
    }

    if(len_to_write > MAX_LINE )
    {
        slapi_log_error( SLAPI_LOG_FATAL, REFERINT_PLUGIN_SUBSYSTEM,
                         "referint_postop could not write integrity log:"
                         " line length exceeded. It will not be able"
                         " to update references to this entry.\n");
    }else{
       PR_snprintf(buffer, MAX_LINE, "%s\t%s\t\n", 
				   dn,
				   (newrdn != NULL) ? newrdn : "NULL");
        if (PR_Write(prfd,buffer,strlen(buffer)) < 0){
           slapi_log_error(SLAPI_LOG_FATAL,REFERINT_PLUGIN_SUBSYSTEM,
	       " writeintegritylog: PR_Write failed : The disk"
	       " may be full or the file is unwritable :: NSPR error - %d\n",
	       PR_GetError());
	    }
   }

    /* If file descriptor is closed successfully, PR_SUCCESS */
    
    rc = PR_Close(prfd);
	if (rc != PR_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL,REFERINT_PLUGIN_SUBSYSTEM, 
				" writeintegritylog: failed to close the file"
				" descriptor prfd; NSPR error - %d\n",
				PR_GetError());
	}
    PR_Unlock(referint_mutex);    
  }
