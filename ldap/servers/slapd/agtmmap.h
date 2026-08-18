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
#include <semaphore.h>
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
    uint64_t dsAnonymousBinds;
    uint64_t dsUnAuthBinds;
    uint64_t dsSimpleAuthBinds;
    uint64_t dsStrongAuthBinds;
    uint64_t dsBindSecurityErrors;
    uint64_t dsInOps;
    uint64_t dsReadOps;
    uint64_t dsCompareOps;
    uint64_t dsAddEntryOps;
    uint64_t dsRemoveEntryOps;
    uint64_t dsModifyEntryOps;
    uint64_t dsModifyRDNOps;
    uint64_t dsListOps;
    uint64_t dsSearchOps;
    uint64_t dsOneLevelSearchOps;
    uint64_t dsWholeSubtreeSearchOps;
    uint64_t dsReferrals;
    uint64_t dsChainings;
    uint64_t dsSecurityErrors;
    uint64_t dsErrors;
    uint64_t dsConnections;             /* Number of currently connected clients */
    uint64_t dsConnectionSeq;           /* Monotonically increasing number bumped on each new conn est */
    uint64_t dsMaxThreadsHits;          /* Number of times a connection hit max threads */
    uint64_t dsConnectionsInMaxThreads; /* current number of connections that are in max threads */
    uint64_t dsBytesRecv;               /* Count of bytes read from clients */
    uint64_t dsBytesSent;               /* Count of bytes sent to clients */
    uint64_t dsEntriesReturned;         /* Number of entries returned by the server */
    uint64_t dsReferralsReturned;       /* Number of entries returned by the server */
};

struct entries_stats_t
{
    /*
     *  Entries Table Attributes
     */
    uint64_t dsSupplierEntries;
    uint64_t dsCopyEntries;
    uint64_t dsCacheEntries;
    uint64_t dsCacheHits;
    uint64_t dsConsumerHits;
};

struct int_stats_t
{
    /*
     *   Interaction Table Attributes
     */
    int32_t dsIntIndex;
    char dsName[SNMP_FIELD_LENGTH];
    time_t dsTimeOfCreation;
    time_t dsTimeOfLastAttempt;
    time_t dsTimeOfLastSuccess;
    uint64_t dsFailuresSinceLastSuccess;
    uint64_t dsFailures;
    uint64_t dsSuccesses;
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

/****************************************************************************
 *
 *  agt_sem_open () - Like sem_open but ignores umask
 *
 *
 * Inputs:            see sem_open man page.
 * Outputs:           see sem_open man page.
 * Return Values:     see sem_open man page.
 *
 ****************************************************************************/
sem_t *agt_sem_open(const char *name, int oflag, mode_t mode, unsigned int value);

#ifdef __cplusplus
}
#endif
