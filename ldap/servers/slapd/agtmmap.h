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


/********************************************************************
 *
 *      agtmmap.h: Memory Map interface for SNMP sub-agent for 
 *                 Fedora Directory Server stats (for UNIX environment).
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
#include "nspr.h"
#ifdef  _WIN32
#include <windows.h> 
#define caddr_t PCHAR
#endif

#ifdef __cplusplus
extern "C" {
#endif


#define NUM_SNMP_INT_TBL_ROWS 5
#define SNMP_FIELD_LENGTH 100

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
    int		restarted; /* 1/0 = Yes/No */
    time_t	startTime;
    time_t	updateTime;
    char        dsVersion[SNMP_FIELD_LENGTH];
    char	dsName[SNMP_FIELD_LENGTH];
    char	dsDescription[SNMP_FIELD_LENGTH];
    char	dsOrganization[SNMP_FIELD_LENGTH];
    char	dsLocation[SNMP_FIELD_LENGTH];
    char	dsContact[SNMP_FIELD_LENGTH];
};

struct ops_stats_t{
    /*
     *      Ops Table attributes
     */
    PRUint32 dsAnonymousBinds;
    PRUint32 dsUnAuthBinds;
    PRUint32 dsSimpleAuthBinds;
    PRUint32 dsStrongAuthBinds;
    PRUint32 dsBindSecurityErrors;
    PRUint32 dsInOps;
    PRUint32 dsReadOps;
    PRUint32 dsCompareOps;
    PRUint32 dsAddEntryOps;
    PRUint32 dsRemoveEntryOps;
    PRUint32 dsModifyEntryOps;
    PRUint32 dsModifyRDNOps;
    PRUint32 dsListOps;
    PRUint32 dsSearchOps;
    PRUint32 dsOneLevelSearchOps;
    PRUint32 dsWholeSubtreeSearchOps;
    PRUint32 dsReferrals;
    PRUint32 dsChainings;
    PRUint32 dsSecurityErrors;
    PRUint32 dsErrors;
    PRUint32 dsConnections;	 /* Number of currently connected clients */
    PRUint32 dsConnectionSeq; /* Monotonically increasing number bumped on each new conn est */
    PRUint32 dsBytesRecv;	/* Count of bytes read from clients */
    PRUint32 dsBytesSent;	/* Count of bytes sent to clients */
    PRUint32 dsEntriesReturned; /* Number of entries returned by the server */
    PRUint32 dsReferralsReturned; /* Number of entries returned by the server */
};

struct entries_stats_t
{
    /*
     *  Entries Table Attributes
     */
    PRUint32 dsMasterEntries;
    PRUint32 dsCopyEntries;
    PRUint32 dsCacheEntries;
    PRUint32 dsCacheHits;
    PRUint32 dsSlaveHits;
};

struct int_stats_t
{
    /*
     *   Interaction Table Attributes
     */
    PRUint32 dsIntIndex;
    char     dsName[SNMP_FIELD_LENGTH];
    time_t   dsTimeOfCreation;         
    time_t   dsTimeOfLastAttempt;      
    time_t   dsTimeOfLastSuccess;      
    PRUint32 dsFailuresSinceLastSuccess;
    PRUint32 dsFailures;
    PRUint32 dsSuccesses;
    char     dsURL[SNMP_FIELD_LENGTH];
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

int agt_mread_stats(int hdl, struct hdr_stats_t *, struct ops_stats_t *,
                    struct entries_stats_t *);

#ifdef __cplusplus
}
#endif
