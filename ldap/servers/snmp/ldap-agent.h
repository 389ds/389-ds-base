/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef DSOPSTABLE_H
#define DSOPSTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* net-snmp-config.h defines
   all of these unconditionally - so we undefine
   them here to make the compiler warnings shut up
   hopefully we don't need the real versions
   of these, but then with no warnings the compiler
   will just silently redefine them to the wrong
   ones anyway
   Then undefine them after the include so that
   our own local defines will take effect
*/
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <net-snmp/net-snmp-config.h>
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/library/snmp_assert.h>
#include <net-snmp/library/container.h>
#include <net-snmp/agent/table_array.h>
#include "../slapd/agtmmap.h"
#include <semaphore.h>
#include <fcntl.h>

#ifdef HPUX
/* HP-UX doesn't define SEM_FAILED like other platforms, so
 * we define it ourselves.  We make this define HP-UX specific
 * since sem_open() doesn't seem to return the same value on
 * all platforms in a failure case (it's 1 on some platforms,
 * and 0 on others). */
#define SEM_FAILED ((sem_t *)(-1))
#endif

#define MAXLINE 4096
#define CACHE_REFRESH_INTERVAL 15
#define UPDATE_THRESHOLD 20
#define SNMP_NUM_SEM_WAITS 10
#define LDAP_AGENT_PIDFILE "ldap-agent.pid"
#define LDAP_AGENT_LOGFILE "ldap-agent.log"

/*************************************************************
 * Trap value defines
 */
#define SERVER_UP 6002
#define SERVER_DOWN 6001
#define STATE_UNKNOWN 0

/*************************************************************
 * Structures
 */
typedef struct server_instance_s
{
    uint32_t port;
    int server_state;
    char *stats_file;
    char *stats_sem_name;
    char *dse_ldif;
    struct server_instance_s *next;
} server_instance;

typedef struct stats_table_context_s
{
    netsnmp_index index;
    struct hdr_stats_t hdr_tbl;
    struct ops_stats_t ops_tbl;
    struct entries_stats_t entries_tbl;
    server_instance *entity_tbl;
} stats_table_context;

/*************************************************************
 * Function Declarations
 */
void exit_usage(void);
void load_config(char *);
void init_ldap_agent(void);
void initialize_stats_table(void);
int load_stats_table(netsnmp_cache *, void *);
void free_stats_table(netsnmp_cache *, void *);
stats_table_context *stats_table_create_row(unsigned long);
stats_table_context *stats_table_find_row(unsigned long);
int dsOpsTable_get_value(netsnmp_request_info *,
                         netsnmp_index *,
                         netsnmp_table_request_info *);
int dsEntriesTable_get_value(netsnmp_request_info *,
                             netsnmp_index *,
                             netsnmp_table_request_info *);
int dsEntityTable_get_value(netsnmp_request_info *,
                            netsnmp_index *,
                            netsnmp_table_request_info *);
int send_DirectoryServerDown_trap(server_instance *);
int send_DirectoryServerStart_trap(server_instance *);

/*************************************************************
 * Oid Declarations
 */
extern oid dsOpsTable_oid[];
extern size_t dsOpsTable_oid_len;
extern oid dsEntriesTable_oid[];
extern size_t dsEntriesTable_oid_len;
extern oid dsEntityTable_oid[];
extern size_t dsEntityTable_oid_len;
extern oid snmptrap_oid[];
extern size_t snmptrap_oid_len;

#define enterprise_OID 1, 3, 6, 1, 4, 1, 2312
#define dsOpsTable_TABLE_OID enterprise_OID, 6, 1
#define dsEntriesTable_TABLE_OID enterprise_OID, 6, 2
#define dsEntityTable_TABLE_OID enterprise_OID, 6, 5
#define snmptrap_OID 1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0
#define DirectoryServerDown_OID enterprise_OID, 0, 6001
#define DirectoryServerStart_OID enterprise_OID, 0, 6002

/*************************************************************
 * dsOpsTable column defines
 */
#define COLUMN_DSANONYMOUSBINDS 1
#define COLUMN_DSUNAUTHBINDS 2
#define COLUMN_DSSIMPLEAUTHBINDS 3
#define COLUMN_DSSTRONGAUTHBINDS 4
#define COLUMN_DSBINDSECURITYERRORS 5
#define COLUMN_DSINOPS 6
#define COLUMN_DSREADOPS 7
#define COLUMN_DSCOMPAREOPS 8
#define COLUMN_DSADDENTRYOPS 9
#define COLUMN_DSREMOVEENTRYOPS 10
#define COLUMN_DSMODIFYENTRYOPS 11
#define COLUMN_DSMODIFYRDNOPS 12
#define COLUMN_DSLISTOPS 13
#define COLUMN_DSSEARCHOPS 14
#define COLUMN_DSONELEVELSEARCHOPS 15
#define COLUMN_DSWHOLESUBTREESEARCHOPS 16
#define COLUMN_DSREFERRALS 17
#define COLUMN_DSCHAININGS 18
#define COLUMN_DSSECURITYERRORS 19
#define COLUMN_DSERRORS 20
#define COLUMN_DSCONNECTIONS 21
#define COLUMN_DSCONNECTIONSINMAXTHREADS 22
#define COLUMN_DSMAXTHREADSHITS 23
#define dsOpsTable_COL_MIN 1
#define dsOpsTable_COL_MAX 23

/*************************************************************
 * dsEntriesTable column defines
 */
#define COLUMN_DSSUPPLIERENTRIES 1
#define COLUMN_DSCOPYENTRIES 2
#define COLUMN_DSCACHEENTRIES 3
#define COLUMN_DSCACHEHITS 4
#define COLUMN_DSCONSUMERHITS 5
#define dsEntriesTable_COL_MIN 1
#define dsEntriesTable_COL_MAX 5

/*************************************************************
 * dsEntityTable column defines
 */
#define COLUMN_DSENTITYDESCR 1
#define COLUMN_DSENTITYVERS 2
#define COLUMN_DSENTITYORG 3
#define COLUMN_DSENTITYLOCATION 4
#define COLUMN_DSENTITYCONTACT 5
#define COLUMN_DSENTITYNAME 6
#define dsEntityTable_COL_MIN 1
#define dsEntityTable_COL_MAX 6

#ifdef __cplusplus
}
#endif
#endif /** DSOPSTABLE_H */
