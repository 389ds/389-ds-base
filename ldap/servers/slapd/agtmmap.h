/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/********************************************************************
 *
 *      agtmmap.h: Memory Map interface for SNMP sub-agent for 
 *                 Brandx Directory Server stats (for UNIX environment).
 *
 *      Revision History:
 *      07/22/97        Created                 Steve Ross
 *
 *
 *
 **********************************************************************/
 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef  _WIN32
#include <windows.h> 
#define caddr_t PCHAR
#endif

#ifdef __cplusplus
extern "C" {
#endif


#define NUM_SNMP_INT_TBL_ROWS 5
#ifndef  _WIN32
extern int			errno;
#endif

#if !defined(_MAX_PATH)
#define _MAX_PATH 256
#endif
#define AGT_STATS_FILE "slapd.stats"
#define AGT_MJR_VERSION 1
#define AGT_MNR_VERSION 0


typedef enum { AGT_MAP_UNINIT = 0, AGT_MAP_READ, AGT_MAP_RDWR } agt_mmap_type;

#ifndef  _WIN32
typedef struct
{
        agt_mmap_type   maptype;
        int             fd;
        caddr_t         fp;
}  agt_mmap_context_t;
#else
typedef struct
{
        agt_mmap_type   maptype;
        HANDLE          fd;
        caddr_t         fp;
		HANDLE			mfh;
}  agt_mmap_context_t;
#endif /* ! _WIN32 */

struct hdr_stats_t{
    /*
     *  Header
     */
    int		hdrVersionMjr;
    int		hdrVersionMnr;
    int		restarted; /* 1/0 = Yes/No */
    time_t	startTime;
    time_t	updateTime;

};

struct ops_stats_t{
    /*
     *      Ops Table attributes
     */
	int dsAnonymousBinds;
	int dsUnAuthBinds;
	int dsSimpleAuthBinds;
	int dsStrongAuthBinds;
	int dsBindSecurityErrors;
	int dsInOps;
    int dsReadOps;
    int dsCompareOps;
	int dsAddEntryOps;
	int dsRemoveEntryOps;
	int dsModifyEntryOps;
	int dsModifyRDNOps;
	int dsListOps;
	int dsSearchOps;
	int dsOneLevelSearchOps;
	int dsWholeSubtreeSearchOps;
	int dsReferrals;
	int dsChainings;
	int dsSecurityErrors;
	int dsErrors;
	int dsConnections;	 /* Number of currently connected clients */
	int dsConnectionSeq; /* Monotonically increasing number bumped on each new conn est */
	int dsBytesRecv;	/* Count of bytes read from clients */
	int dsBytesSent;	/* Count of bytes sent to clients */
	int dsEntriesReturned; /* Number of entries returned by the server */
	int dsReferralsReturned; /* Number of entries returned by the server */
};

struct entries_stats_t
{
       /*
	*  Entries Table Attributes
	*/
        
        int dsMasterEntries;
	int dsCopyEntries;
	int dsCacheEntries;
	int dsCacheHits;
	int dsSlaveHits;

};
struct int_stats_t
{
       /*
        *   Interaction Table Attributes
        */

        int     dsIntIndex;
        char    dsName[100];
	time_t  dsTimeOfCreation;         
	time_t  dsTimeOfLastAttempt;      
	time_t  dsTimeOfLastSuccess;      
	int     dsFailuresSinceLastSuccess;
	int     dsFailures;
	int     dsSuccesses;
	char    dsURL[100];

};
struct agt_stats_t
{
     struct hdr_stats_t hdr_stats;
     struct ops_stats_t ops_stats;
     struct entries_stats_t entries_stats;
     struct int_stats_t int_stats[NUM_SNMP_INT_TBL_ROWS];
        
} ;

extern agt_mmap_context_t      	mmap_tbl[];

/****************************************************************************
 *
 *  agt_mopen_stats () - open and Memory Map the stats file.  agt_mclose_stats() 
 * 			 must be called prior to invoking agt_mopen_stats() again.
 * Inputs: 	
 * 	statsfile ->  Name of stats file including full path or NULL. 
 * 	       	      If NULL, default ($NETSITE_ROOT/daemonstats.ldap) is assumed.
 *	mode      ->  Must be one of O_RDONLY / O_RDWR.
 *		      O_RDWR creates the file if it does not exist.
 * Outputs:
 *	hdl	  ->  Opaque handle to the mapped file. Should be 
 *		      passed to a subsequent agt_mupdate_stats() or 
 *		      agt_mread_stats() or agt_mclose_stats() call.
 * Return Values:
 *		      Returns 0 on successfully doing the memmap or error 
 * 		      codes as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/

int agt_mopen_stats 	(char * statsfile, int mode, int *hdl);

/****************************************************************************
 *
 *  agt_mclose_stats () - Close the Memory Map'ed stats file.
 *
 *
 * Inputs: 	
 *	hdl	  ->  Opaque handle to the mapped file. Should have been 
 *		      returned by an earlier call to agt_mopen_stats().
 *		      
 * Outputs:	      <NONE>
 *		      
 * Return Values:     Returns 0 on normal completion or error codes 
 * 		      as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/
int agt_mclose_stats (int hdl);

#ifdef __cplusplus
}
#endif
