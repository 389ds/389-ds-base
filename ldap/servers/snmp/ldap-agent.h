#ifndef DSOPSTABLE_H
#define DSOPSTABLE_H

#ifdef __cplusplus
extern          "C" {
#endif


#include <net-snmp/net-snmp-config.h>
#include <net-snmp/library/container.h>
#include <net-snmp/agent/table_array.h>
#include "agtmmap.h"

#define MAXLINE 4096
#define CACHE_REFRESH_INTERVAL 15
#define UPDATE_THRESHOLD 20
#define LDAP_AGENT_PIDFILE ".ldap-agent.pid"
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
typedef struct server_instance_s {
    PRUint32 port;
    int server_state;
    char *stats_file;
    char *dse_ldif;
    char *description;
    char *org;
    char *location;
    char *contact;
    char *name;
    struct server_instance_s *next;
} server_instance;

typedef struct stats_table_context_s {
    netsnmp_index index;
    struct hdr_stats_t hdr_tbl;
    struct ops_stats_t ops_tbl;
    struct entries_stats_t entries_tbl;
    server_instance *entity_tbl;
} stats_table_context;

/*************************************************************
 * Function Declarations
 */
    void	exit_usage();
    void	load_config(char *);
    void        init_ldap_agent(void);
    void        initialize_stats_table(void);
    int         load_stats_table(netsnmp_cache *, void *);
    stats_table_context *stats_table_create_row(unsigned long);
    stats_table_context *stats_table_find_row(unsigned long);
    int         dsOpsTable_get_value(netsnmp_request_info *,
                                         netsnmp_index *,
                                         netsnmp_table_request_info *);
    int         dsEntriesTable_get_value(netsnmp_request_info *,
                                         netsnmp_index *,
                                         netsnmp_table_request_info *);
    int         dsEntityTable_get_value(netsnmp_request_info *,
                                         netsnmp_index *,
                                         netsnmp_table_request_info *);
    int		send_DirectoryServerDown_trap(server_instance *);
    int		send_DirectoryServerStart_trap(server_instance *);

/*************************************************************
 * Oid Declarations
 */
    extern oid      dsOpsTable_oid[];
    extern size_t   dsOpsTable_oid_len;
    extern oid      dsEntriesTable_oid[];
    extern size_t   dsEntriesTable_oid_len;
    extern oid      dsEntityTable_oid[];
    extern size_t   dsEntityTable_oid_len;
    extern oid      snmptrap_oid[];
    extern size_t   snmptrap_oid_len;

#define enterprise_OID 1,3,6,1,4,1,2312
#define dsOpsTable_TABLE_OID enterprise_OID,6,1
#define dsEntriesTable_TABLE_OID enterprise_OID,6,2
#define dsEntityTable_TABLE_OID enterprise_OID,6,5
#define snmptrap_OID 1,3,6,1,6,3,1,1,4,1,0
#define DirectoryServerDown_OID enterprise_OID,0,6001
#define DirectoryServerStart_OID enterprise_OID,0,6002

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
#define dsOpsTable_COL_MIN 1
#define dsOpsTable_COL_MAX 20

/*************************************************************
 * dsEntriesTable column defines
 */
#define COLUMN_DSMASTERENTRIES 1
#define COLUMN_DSCOPYENTRIES 2
#define COLUMN_DSCACHEENTRIES 3
#define COLUMN_DSCACHEHITS 4
#define COLUMN_DSSLAVEHITS 5
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
