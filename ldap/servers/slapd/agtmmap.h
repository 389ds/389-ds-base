/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/********************************************************************
 *
 *      agtmmap.h: Memory Map interface for SNMP sub-agent for
 *                 Directory Server stats (for UNIX environment).
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

#ifdef __cplusplus
extern "C" {
#endif


#define NUM_SNMP_INT_TBL_ROWS 5
#define SNMP_FIELD_LENGTH 100

#if !defined(_MAX_PATH)
#define _MAX_PATH 256
#endif
#define AGT_STATS_EXTENSION ".stats"
#define AGT_STATS_FILE "slapd" AGT_STATS_EXTENSION
#define AGT_MJR_VERSION 1
#define AGT_MNR_VERSION 0


typedef enum { AGT_MAP_UNINIT = 0,
               AGT_MAP_READ,
               AGT_MAP_RDWR } agt_mmap_type;

typedef struct
{
    agt_mmap_type maptype;
    int fd;
    caddr_t fp;
} agt_mmap_context_t;

struct hdr_stats_t
{
    /*
     *  Header
     */
    int restarted; /* 1/0 = Yes/No */
    time_t startTime;
    time_t updateTime;
    char dsVersion[SNMP_FIELD_LENGTH];
    char dsName[SNMP_FIELD_LENGTH];
    char dsDescription[SNMP_FIELD_LENGTH];
    char dsOrganization[SNMP_FIELD_LENGTH];
    char dsLocation[SNMP_FIELD_LENGTH];
    char dsContact[SNMP_FIELD_LENGTH];
};

struct ops_stats_t
{
    /*
     *      Ops Table attributes
     */
    PRUint64 dsAnonymousBinds;
    PRUint64 dsUnAuthBinds;
    PRUint64 dsSimpleAuthBinds;
    PRUint64 dsStrongAuthBinds;
    PRUint64 dsBindSecurityErrors;
    PRUint64 dsInOps;
    PRUint64 dsReadOps;
    PRUint64 dsCompareOps;
    PRUint64 dsAddEntryOps;
    PRUint64 dsRemoveEntryOps;
    PRUint64 dsModifyEntryOps;
    PRUint64 dsModifyRDNOps;
    PRUint64 dsListOps;
    PRUint64 dsSearchOps;
    PRUint64 dsOneLevelSearchOps;
    PRUint64 dsWholeSubtreeSearchOps;
    PRUint64 dsReferrals;
    PRUint64 dsChainings;
    PRUint64 dsSecurityErrors;
    PRUint64 dsErrors;
    PRUint64 dsConnections;             /* Number of currently connected clients */
    PRUint64 dsConnectionSeq;           /* Monotonically increasing number bumped on each new conn est */
    PRUint64 dsMaxThreadsHit;           /* Number of times a connection hit max threads */
    PRUint64 dsConnectionsInMaxThreads; /* current number of connections that are in max threads */
    PRUint64 dsBytesRecv;               /* Count of bytes read from clients */
    PRUint64 dsBytesSent;               /* Count of bytes sent to clients */
    PRUint64 dsEntriesReturned;         /* Number of entries returned by the server */
    PRUint64 dsReferralsReturned;       /* Number of entries returned by the server */
};

struct entries_stats_t
{
    /*
     *  Entries Table Attributes
     */
    PRUint64 dsMasterEntries;
    PRUint64 dsCopyEntries;
    PRUint64 dsCacheEntries;
    PRUint64 dsCacheHits;
    PRUint64 dsSlaveHits;
};

struct int_stats_t
{
    /*
     *   Interaction Table Attributes
     */
    PRUint32 dsIntIndex;
    char dsName[SNMP_FIELD_LENGTH];
    time_t dsTimeOfCreation;
    time_t dsTimeOfLastAttempt;
    time_t dsTimeOfLastSuccess;
    PRUint32 dsFailuresSinceLastSuccess;
    PRUint32 dsFailures;
    PRUint32 dsSuccesses;
    char dsURL[SNMP_FIELD_LENGTH];
};

struct agt_stats_t
{
    struct hdr_stats_t hdr_stats;
    struct ops_stats_t ops_stats;
    struct entries_stats_t entries_stats;
    struct int_stats_t int_stats[NUM_SNMP_INT_TBL_ROWS];
};

extern agt_mmap_context_t mmap_tbl[];

/****************************************************************************
 *
 *  agt_mopen_stats () - open and Memory Map the stats file.  agt_mclose_stats()
 *              must be called prior to invoking agt_mopen_stats() again.
 * Inputs:
 *     statsfile ->  Name of stats file including full path or NULL.
 *                      If NULL, default ($NETSITE_ROOT/daemonstats.ldap) is assumed.
 *    mode      ->  Must be one of O_RDONLY / O_RDWR.
 *              O_RDWR creates the file if it does not exist.
 * Outputs:
 *    hdl      ->  Opaque handle to the mapped file. Should be
 *              passed to a subsequent agt_mupdate_stats() or
 *              agt_mread_stats() or agt_mclose_stats() call.
 * Return Values:
 *              Returns 0 on successfully doing the memmap or error
 *               codes as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/

int agt_mopen_stats(char *statsfile, int mode, int *hdl);

/****************************************************************************
 *
 *  agt_mclose_stats () - Close the Memory Map'ed stats file.
 *
 *
 * Inputs:
 *    hdl      ->  Opaque handle to the mapped file. Should have been
 *              returned by an earlier call to agt_mopen_stats().
 *
 * Outputs:          <NONE>
 *
 * Return Values:     Returns 0 on normal completion or error codes
 *               as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/
int agt_mclose_stats(int hdl);

int agt_mread_stats(int hdl, struct hdr_stats_t *, struct ops_stats_t *, struct entries_stats_t *);

#ifdef __cplusplus
}
#endif
