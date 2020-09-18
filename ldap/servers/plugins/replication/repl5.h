/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* repl5.h - 5.0 replication header */

#ifndef _REPL5_H_
#define _REPL5_H_

#include <limits.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include "portable.h" /* GGOODREPL - is this cheating? */
#include "repl_shared.h"
#include "llist.h"
#include "repl5_ruv.h"
#include "plstr.h"

#define START_UPDATE_DELAY                   2 /* 2 second */
#define REPLICA_TYPE_WINDOWS                 1
#define REPLICA_TYPE_MULTIMASTER             0
#define REPL_DIRSYNC_CONTROL_OID             "1.2.840.113556.1.4.841"
#define REPL_RETURN_DELETED_OBJS_CONTROL_OID "1.2.840.113556.1.4.417"
#define REPL_WIN2K3_AD_OID                   "1.2.840.113556.1.4.1670"
#define LDAP_CONTROL_REPL_MODRDN_EXTRAMODS   "2.16.840.1.113730.3.4.999"
#define REPLICATION_SUBSYSTEM                "replication"

/* DS 5.0 replication protocol OIDs */
#define REPL_START_NSDS50_REPLICATION_REQUEST_OID "2.16.840.1.113730.3.5.3"
#define REPL_END_NSDS50_REPLICATION_REQUEST_OID   "2.16.840.1.113730.3.5.5"
#define REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID "2.16.840.1.113730.3.5.6"
#define REPL_NSDS50_REPLICATION_RESPONSE_OID      "2.16.840.1.113730.3.5.4"
#define REPL_NSDS50_UPDATE_INFO_CONTROL_OID       "2.16.840.1.113730.3.4.13"
#define REPL_NSDS50_INCREMENTAL_PROTOCOL_OID      "2.16.840.1.113730.3.6.1"
#define REPL_NSDS50_TOTAL_PROTOCOL_OID            "2.16.840.1.113730.3.6.2"
/* DS7.1 introduces pipelineing in the protocol : really not much different to the 5.0
 * protocol, but enough change to make it unsafe to interoperate the two. So we define
 * new OIDs for 7.1 here. The supplier server looks for these on the consumer and
 * if they're not there it falls back to the older 5.0 non-pipelined protocol */
#define REPL_NSDS71_INCREMENTAL_PROTOCOL_OID "2.16.840.1.113730.3.6.4"
#define REPL_NSDS71_TOTAL_PROTOCOL_OID       "2.16.840.1.113730.3.6.3"
/* The new protocol OIDs above do not help us with determining if a consumer
 * Supports them or not. That's because they're burried inside the start replication
 * extended operation, and are not visible in the support controls and operations list
 * So, we add a new extended operation for the 7.1 total protocol. This is partly because
 * the total protocol is slightly different (no LDAP_BUSY allowed in 7.1) and partly
 * because we need a handy way to spot the difference between a pre-7.1 and post-7.0
 * consumer at the supplier */
#define REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID "2.16.840.1.113730.3.5.9"
/* DS9.0 introduces replication session callbacks that can send/receive
 * arbitrary data when starting a replication session.  This requires a
 * new set of start and response extops. */
#define REPL_START_NSDS90_REPLICATION_REQUEST_OID "2.16.840.1.113730.3.5.12"
#define REPL_NSDS90_REPLICATION_RESPONSE_OID      "2.16.840.1.113730.3.5.13"
/* cleanallruv extended ops */
#define REPL_CLEANRUV_OID              "2.16.840.1.113730.3.6.5"
#define REPL_ABORT_CLEANRUV_OID        "2.16.840.1.113730.3.6.6"
#define REPL_CLEANRUV_GET_MAXCSN_OID   "2.16.840.1.113730.3.6.7"
#define REPL_CLEANRUV_CHECK_STATUS_OID "2.16.840.1.113730.3.6.8"
#define REPL_ABORT_SESSION_OID         "2.16.840.1.113730.3.6.9"
#define SESSION_ACQUIRED 0
#define ABORT_SESSION    1
#define SESSION_ABORTED  2

#define CLEANRUV_ACCEPTED  "accepted"
#define CLEANRUV_REJECTED  "rejected"
#define CLEANRUV_FINISHED  "finished"
#define CLEANRUV_CLEANING  "cleaning"
#define CLEANRUV_NO_MAXCSN "no maxcsn"
#define CLEANALLRUV_ID "CleanAllRUV Task"
#define ABORT_CLEANALLRUV_ID "Abort CleanAllRUV Task"

/* DS 5.0 replication protocol error codes */
#define NSDS50_REPL_REPLICA_READY             0x00  /* Replica ready, go ahead */
#define NSDS50_REPL_REPLICA_BUSY              0x01  /* Replica busy, try later */
#define NSDS50_REPL_EXCESSIVE_CLOCK_SKEW      0x02  /* Supplier clock too far ahead */
#define NSDS50_REPL_PERMISSION_DENIED         0x03  /* Bind DN not allowed to send updates */
#define NSDS50_REPL_DECODING_ERROR            0x04  /* Consumer couldn't decode extended operation */
#define NSDS50_REPL_UNKNOWN_UPDATE_PROTOCOL   0x05  /* Consumer doesn't understand suplier's update protocol */
#define NSDS50_REPL_NO_SUCH_REPLICA           0x06  /* Consumer holds no such replica */
#define NSDS50_REPL_BELOW_PURGEPOINT          0x07  /* Supplier provided a CSN below the consumer's purge point */
#define NSDS50_REPL_INTERNAL_ERROR            0x08  /* Something bad happened on consumer */
#define NSDS50_REPL_REPLICA_RELEASE_SUCCEEDED 0x09  /* Replica released successfully */
#define NSDS50_REPL_REPLICAID_ERROR           0x0B  /* replicaID doesn't seem to be unique */
#define NSDS50_REPL_DISABLED                  0x0C  /* replica suffix is disabled */
#define NSDS50_REPL_UPTODATE                  0x0D  /* replica is uptodate */
#define NSDS50_REPL_BACKOFF                   0x0E  /* replica wants master to go into backoff mode */
#define NSDS50_REPL_CL_ERROR                  0x0F  /* Problem reading changelog */
#define NSDS50_REPL_CONN_ERROR                0x10  /* Problem with replication connection*/
#define NSDS50_REPL_CONN_TIMEOUT              0x11  /* Connection timeout */
#define NSDS50_REPL_TRANSIENT_ERROR           0x12  /* Transient error */
#define NSDS50_REPL_RUV_ERROR                 0x13  /* Problem with the RUV */
#define NSDS50_REPL_REPLICA_NO_RESPONSE       0xff  /* No response received */

/* Protocol status */
#define PROTOCOL_STATUS_UNKNOWN                        701
#define PROTOCOL_STATUS_INCREMENTAL_AWAITING_CHANGES   702
#define PROTOCOL_STATUS_INCREMENTAL_ACQUIRING_REPLICA  703
#define PROTOCOL_STATUS_INCREMENTAL_RELEASING_REPLICA  704
#define PROTOCOL_STATUS_INCREMENTAL_SENDING_UPDATES    705
#define PROTOCOL_STATUS_INCREMENTAL_BACKING_OFF        706
#define PROTOCOL_STATUS_INCREMENTAL_NEEDS_TOTAL_UPDATE 707
#define PROTOCOL_STATUS_INCREMENTAL_FATAL_ERROR        708
#define PROTOCOL_STATUS_TOTAL_ACQUIRING_REPLICA        709
#define PROTOCOL_STATUS_TOTAL_RELEASING_REPLICA        710
#define PROTOCOL_STATUS_TOTAL_SENDING_DATA             711

#define DEFAULT_PROTOCOL_TIMEOUT 120

/* To Allow Consumer Initialization when adding an agreement - */
#define STATE_PERFORMING_TOTAL_UPDATE       501
#define STATE_PERFORMING_INCREMENTAL_UPDATE 502

#define MAX_NUM_OF_MASTERS   256
#define REPL_SESSION_ID_SIZE 64

#define REPL_GET_DN(addrp) slapi_sdn_get_dn((addrp)->sdn)
#define REPL_GET_DN_LEN(addrp) slapi_sdn_get_ndn_len((addrp)->sdn)

/* Attribute names for replication agreement attributes */
extern const char *type_nsds5ReplicaHost;
extern const char *type_nsds5ReplicaPort;
extern const char *type_nsds5TransportInfo;
extern const char *type_nsds5ReplicaBindDN;
extern const char *type_nsds5ReplicaBindDNGroup;
extern const char *type_nsds5ReplicaBindDNGroupCheckInterval;
extern const char *type_nsds5ReplicaCredentials;
extern const char *type_nsds5ReplicaBindMethod;
extern const char *type_nsds5ReplicaRoot;
extern const char *type_nsds5ReplicatedAttributeList;
extern const char *type_nsds5ReplicatedAttributeListTotal;
extern const char *type_nsds5ReplicaUpdateSchedule;
extern const char *type_nsds5ReplicaInitialize;
extern const char *type_nsds5ReplicaTimeout;
extern const char *type_nsds5ReplicaBusyWaitTime;
extern const char *type_nsds5ReplicaSessionPauseTime;
extern const char *type_nsds5ReplicaEnabled;
extern const char *type_nsds5ReplicaStripAttrs;
extern const char *type_nsds5ReplicaFlowControlWindow;
extern const char *type_nsds5ReplicaFlowControlPause;
extern const char *type_replicaProtocolTimeout;
extern const char *type_replicaReleaseTimeout;
extern const char *type_replicaBackoffMin;
extern const char *type_replicaBackoffMax;
extern const char *type_replicaPrecisePurge;
extern const char *type_replicaIgnoreMissingChange;
extern const char *type_nsds5ReplicaBootstrapBindDN;
extern const char *type_nsds5ReplicaBootstrapCredentials;
extern const char *type_nsds5ReplicaBootstrapBindMethod;
extern const char *type_nsds5ReplicaBootstrapTransportInfo;

/* Attribute names for windows replication agreements */
extern const char *type_nsds7WindowsReplicaArea;
extern const char *type_nsds7DirectoryReplicaArea;
extern const char *type_nsds7CreateNewUsers;
extern const char *type_nsds7CreateNewGroups;
extern const char *type_nsds7DirsyncCookie;
extern const char *type_nsds7WindowsDomain;
extern const char *type_winSyncInterval;
extern const char *type_oneWaySync;
extern const char *type_winsyncMoveAction;
extern const char *type_winSyncWindowsFilter;
extern const char *type_winSyncDirectoryFilter;
extern const char *type_winSyncSubtreePair;

/* To Allow Consumer Initialization when adding an agreement - */
extern const char *type_nsds5BeginReplicaRefresh;

/* For tuning replica release */
extern const char *type_nsds5WaitForAsyncResults;

/* replica related attributes */
extern const char *attr_replicaId;
extern const char *attr_replicaRoot;
extern const char *attr_replicaType;
extern const char *attr_replicaBindDn;
extern const char *attr_replicaBindDnGroup;
extern const char *attr_replicaBindDnGroupCheckInterval;
extern const char *attr_state;
extern const char *attr_flags;
extern const char *attr_replicaName;
extern const char *attr_replicaReferral;
extern const char *type_ruvElement;
extern const char *type_agmtMaxCSN;
extern const char *type_replicaPurgeDelay;
extern const char *type_replicaChangeCount;
extern const char *type_replicaTombstonePurgeInterval;
extern const char *type_replicaCleanRUV;
extern const char *type_replicaAbortCleanRUV;
extern const char *type_ruvElementUpdatetime;

/* multimaster plugin points */
int multimaster_preop_bind(Slapi_PBlock *pb);
int multimaster_preop_add(Slapi_PBlock *pb);
int multimaster_preop_delete(Slapi_PBlock *pb);
int multimaster_preop_modify(Slapi_PBlock *pb);
int multimaster_preop_modrdn(Slapi_PBlock *pb);
int multimaster_preop_search(Slapi_PBlock *pb);
int multimaster_preop_compare(Slapi_PBlock *pb);
int multimaster_ruv_search(Slapi_PBlock *pb);
int multimaster_mmr_preop (Slapi_PBlock *pb, int flags);
int multimaster_mmr_postop (Slapi_PBlock *pb, int flags);
int multimaster_bepreop_add(Slapi_PBlock *pb);
int multimaster_bepreop_delete(Slapi_PBlock *pb);
int multimaster_bepreop_modify(Slapi_PBlock *pb);
int multimaster_bepreop_modrdn(Slapi_PBlock *pb);
int replica_ruv_smods_for_op(Slapi_PBlock *pb, char **uniqueid, Slapi_Mods **smods);
int multimaster_bepostop_modrdn(Slapi_PBlock *pb);
int multimaster_bepostop_delete(Slapi_PBlock *pb);
int multimaster_postop_bind(Slapi_PBlock *pb);
int multimaster_postop_add(Slapi_PBlock *pb);
int multimaster_postop_delete(Slapi_PBlock *pb);
int multimaster_postop_modify(Slapi_PBlock *pb);
int multimaster_postop_modrdn(Slapi_PBlock *pb);
int multimaster_betxnpostop_modrdn(Slapi_PBlock *pb);
int multimaster_betxnpostop_delete(Slapi_PBlock *pb);
int multimaster_betxnpostop_add(Slapi_PBlock *pb);
int multimaster_betxnpostop_modify(Slapi_PBlock *pb);
int multimaster_be_betxnpostop_modrdn(Slapi_PBlock *pb);
int multimaster_be_betxnpostop_delete(Slapi_PBlock *pb);
int multimaster_be_betxnpostop_add(Slapi_PBlock *pb);
int multimaster_be_betxnpostop_modify(Slapi_PBlock *pb);

/* In repl5_replica.c */
typedef struct replica Replica;

/* csn pending lists */
#define CSNPL_CTX_REPLCNT 4
typedef struct CSNPL_CTX
{
    CSN *prim_csn;
    size_t repl_alloc;  /* max number of replicas  */
    size_t repl_cnt;    /* number of replicas affected by operation */
    Replica *prim_repl; /* pirmary replica */
    Replica **sec_repl; /* additional replicas affected */
} CSNPL_CTX;

/* In repl5_init.c */
extern int repl5_is_betxn;
char *get_thread_private_agmtname(void);
void set_thread_private_agmtname(const char *agmtname);
void set_thread_primary_csn(const CSN *prim_csn, Replica *repl);
void add_replica_to_primcsn(CSNPL_CTX *prim_csn, Replica *repl);
CSNPL_CTX *get_thread_primary_csn(void);
void *get_thread_private_cache(void);
void set_thread_private_cache(void *buf);
char *get_repl_session_id(Slapi_PBlock *pb, char *id, CSN **opcsn);

/* In repl_extop.c */
int multimaster_extop_StartNSDS50ReplicationRequest(Slapi_PBlock *pb);
int multimaster_extop_EndNSDS50ReplicationRequest(Slapi_PBlock *pb);
int multimaster_extop_cleanruv(Slapi_PBlock *pb);
int multimaster_extop_abort_cleanruv(Slapi_PBlock *pb);
int multimaster_extop_cleanruv_get_maxcsn(Slapi_PBlock *pb);
int multimaster_extop_cleanruv_check_status(Slapi_PBlock *pb);
int extop_noop(Slapi_PBlock *pb);
struct berval *NSDS50StartReplicationRequest_new(const char *protocol_oid,
                                                 const char *repl_root,
                                                 char **extra_referrals,
                                                 CSN *csn);
struct berval *NSDS50EndReplicationRequest_new(char *repl_root);
int decode_repl_ext_response(struct berval *bvdata, int *response_code, struct berval ***ruv_bervals, char **data_guid, struct berval **data);
struct berval *NSDS90StartReplicationRequest_new(const char *protocol_oid,
                                                 const char *repl_root,
                                                 char **extra_referrals,
                                                 CSN *csn,
                                                 const char *data_guid,
                                                 const struct berval *data);

/* In repl5_total.c */
int multimaster_extop_NSDS50ReplicationEntry(Slapi_PBlock *pb);

/* From repl_globals.c */
extern char *repl_changenumber;
extern char *repl_targetdn;
extern char *repl_changetype;
extern char *repl_newrdn;
extern char *repl_deleteoldrdn;
extern char *repl_changes;
extern char *repl_newsuperior;
extern char *repl_changetime;
extern char *attr_csn;
extern char *changetype_add;
extern char *changetype_delete;
extern char *changetype_modify;
extern char *changetype_modrdn;
extern char *changetype_moddn;
extern char *type_copyingFrom;
extern char *type_copiedFrom;
extern char *filter_copyingFrom;
extern char *filter_copiedFrom;
extern char *filter_objectclass;
extern char *type_cn;
extern char *type_objectclass;

/* In profile.c */
#ifdef PROFILE
#define PROFILE_POINT \
    if (CFG_profile)  \
    profile_log(__FILE__, __LINE__) /* JCMREPL - Where is the profiling flag stored? */
#else
#define PROFILE_POINT ((void)0)
#endif

/* In repl_controls.c */
int create_NSDS50ReplUpdateInfoControl(const char *uuid,
                                       const char *superior_uuid,
                                       const CSN *csn,
                                       LDAPMod **modify_mods,
                                       LDAPControl **ctrlp);
void destroy_NSDS50ReplUpdateInfoControl(LDAPControl **ctrlp);
int decode_NSDS50ReplUpdateInfoControl(LDAPControl **controlsp,
                                       char **uuid,
                                       char **newsuperior_uuid,
                                       CSN **csn,
                                       LDAPMod ***modrdn_mods);

/* In repl5_replsupplier.c */
typedef struct repl_supplier Repl_Supplier;
Repl_Supplier *replsupplier_init(Slapi_Entry *e);
void replsupplier_configure(Repl_Supplier *rs, Slapi_PBlock *pb);
void replsupplier_start(Repl_Supplier *rs);
void replsupplier_stop(Repl_Supplier *rs);
void replsupplier_destroy(Repl_Supplier **rs);
void replsupplier_notify(Repl_Supplier *rs, uint32_t eventmask);
uint32_t replsupplier_get_status(Repl_Supplier *rs);

/* In repl5_plugins.c */
int multimaster_set_local_purl(void);
const char *multimaster_get_local_purl(void);
PRBool multimaster_started(void);

/* In repl5_schedule.c */
typedef struct schedule Schedule;
typedef void (*window_state_change_callback)(void *arg, PRBool opened);
Schedule *schedule_new(window_state_change_callback callback_fn, void *callback_arg, const char *session_id);
void schedule_destroy(Schedule *s);
int schedule_set(Schedule *sch, Slapi_Attr *attr);
char **schedule_get(Schedule *sch);
int schedule_in_window_now(Schedule *sch);
PRTime schedule_next(Schedule *sch);
int schedule_notify(Schedule *sch, Slapi_PBlock *pb);
void schedule_set_priority_attributes(Schedule *sch, char **prio_attrs, int override_schedule);
void schedule_set_startup_delay(Schedule *sch, size_t startup_delay);
void schedule_set_maximum_backlog(Schedule *sch, size_t max_backlog);
void schedule_notify_session(Schedule *sch, PRTime session_end_time, unsigned int flags);
#define REPLICATION_SESSION_SUCCESS 0

/* In repl5_bos.c */
typedef struct repl_bos Repl_Bos;

/* In repl5_agmt.c */
typedef struct repl5agmt Repl_Agmt;

#define TRANSPORT_FLAG_LDAPS 1
#define TRANSPORT_FLAG_STARTTLS 2
#define BINDMETHOD_SIMPLE_AUTH 1
#define BINDMETHOD_SSL_CLIENTAUTH 2
#define BINDMETHOD_SASL_GSSAPI 3
#define BINDMETHOD_SASL_DIGEST_MD5 4
Repl_Agmt *agmt_new_from_entry(Slapi_Entry *e);
Repl_Agmt *agmt_new_from_pblock(Slapi_PBlock *pb);
void agmt_delete(void **ra);
const Slapi_DN *agmt_get_dn_byref(const Repl_Agmt *ra);
int agmt_get_auto_initialize(const Repl_Agmt *ra);
long agmt_get_timeout(const Repl_Agmt *ra);
long agmt_get_busywaittime(const Repl_Agmt *ra);
long agmt_get_pausetime(const Repl_Agmt *ra);
long agmt_get_flowcontrolwindow(const Repl_Agmt *ra);
long agmt_get_flowcontrolpause(const Repl_Agmt *ra);
long agmt_get_ignoremissing(const Repl_Agmt *ra);
int agmt_start(Repl_Agmt *ra);
int windows_agmt_start(Repl_Agmt *ra);
int agmt_stop(Repl_Agmt *ra);
int agmt_replicate_now(Repl_Agmt *ra);
char *agmt_get_hostname(const Repl_Agmt *ra);
int agmt_get_port(const Repl_Agmt *ra);
uint32_t agmt_get_transport_flags(const Repl_Agmt *ra);
uint32_t agmt_get_bootstrap_transport_flags(const Repl_Agmt *ra);
char *agmt_get_binddn(const Repl_Agmt *ra);
char *agmt_get_bootstrap_binddn(const Repl_Agmt *ra);
struct berval *agmt_get_credentials(const Repl_Agmt *ra);
struct berval *agmt_get_bootstrap_credentials(const Repl_Agmt *ra);
int agmt_get_bindmethod(const Repl_Agmt *ra);
int64_t agmt_get_bootstrap_bindmethod(const Repl_Agmt *ra);
Slapi_DN *agmt_get_replarea(const Repl_Agmt *ra);
int agmt_is_fractional(const Repl_Agmt *ra);
int agmt_is_fractional_attr(const Repl_Agmt *ra, const char *attrname);
int agmt_is_fractional_attr_total(const Repl_Agmt *ra, const char *attrname);
int agmt_is_50_mm_protocol(const Repl_Agmt *ra);
int agmt_matches_name(const Repl_Agmt *ra, const Slapi_DN *name);
int agmt_replarea_matches(const Repl_Agmt *ra, const Slapi_DN *name);
int agmt_schedule_in_window_now(const Repl_Agmt *ra);
int agmt_set_schedule_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_timeout_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_flowcontrolwindow_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_flowcontrolpause_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_ignoremissing_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_busywaittime_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_pausetime_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_credentials_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int32_t agmt_set_bootstrap_credentials_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_binddn_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int32_t agmt_set_bootstrap_binddn_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_bind_method_from_entry(Repl_Agmt *ra, const Slapi_Entry *e, PRBool bootstrap);
int agmt_set_transportinfo_from_entry(Repl_Agmt *ra, const Slapi_Entry *e, PRBool bootstrap);
int agmt_set_port_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_host_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
const char *agmt_get_long_name(const Repl_Agmt *ra);
int agmt_initialize_replica(const Repl_Agmt *agmt);
void agmt_replica_init_done(const Repl_Agmt *agmt);
void agmt_notify_change(Repl_Agmt *ra, Slapi_PBlock *pb);
Object *agmt_get_consumer_ruv(Repl_Agmt *ra);
ReplicaId agmt_get_consumer_rid(Repl_Agmt *ra, void *conn);
int agmt_set_consumer_ruv(Repl_Agmt *ra, RUV *ruv);
void agmt_update_consumer_ruv(Repl_Agmt *ra);
CSN *agmt_get_consumer_schema_csn(Repl_Agmt *ra);
void agmt_set_consumer_schema_csn(Repl_Agmt *ra, CSN *csn);
void agmt_set_last_update_in_progress(Repl_Agmt *ra, PRBool in_progress);
void agmt_set_last_update_start(Repl_Agmt *ra, time_t start_time);
void agmt_set_last_update_end(Repl_Agmt *ra, time_t end_time);
void agmt_set_last_update_status(Repl_Agmt *ra, int ldaprc, int replrc, const char *msg);
void agmt_set_update_in_progress(Repl_Agmt *ra, PRBool in_progress);
PRBool agmt_get_update_in_progress(const Repl_Agmt *ra);
void agmt_set_last_init_start(Repl_Agmt *ra, time_t start_time);
void agmt_set_last_init_end(Repl_Agmt *ra, time_t end_time);
void agmt_set_last_init_status(Repl_Agmt *ra, int ldaprc, int replrc, int connrc, const char *msg);
void agmt_inc_last_update_changecount(Repl_Agmt *ra, ReplicaId rid, int skipped);
void agmt_get_changecount_string(Repl_Agmt *ra, char *buf, int bufsize);
int agmt_set_replicated_attributes_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_replicated_attributes_total_from_entry(Repl_Agmt *ra, const Slapi_Entry *e);
int agmt_set_replicated_attributes_from_attr(Repl_Agmt *ra, Slapi_Attr *sattr);
int agmt_set_replicated_attributes_total_from_attr(Repl_Agmt *ra, Slapi_Attr *sattr);
char **agmt_get_fractional_attrs(const Repl_Agmt *ra);
char **agmt_get_fractional_attrs_total(const Repl_Agmt *ra);
char **agmt_validate_replicated_attributes(Repl_Agmt *ra, int total);
void *agmt_get_priv(const Repl_Agmt *agmt);
void agmt_set_priv(Repl_Agmt *agmt, void *priv);
int get_agmt_agreement_type(Repl_Agmt *agmt);
void *agmt_get_connection(Repl_Agmt *ra);
int agmt_has_protocol(Repl_Agmt *agmt);
PRBool agmt_is_enabled(Repl_Agmt *ra);
int agmt_set_enabled_from_entry(Repl_Agmt *ra, Slapi_Entry *e, char *returntext);
char **agmt_get_attrs_to_strip(Repl_Agmt *ra);
int agmt_set_attrs_to_strip(Repl_Agmt *ra, Slapi_Entry *e);
int agmt_set_timeout(Repl_Agmt *ra, long timeout);
int agmt_set_ignoremissing(Repl_Agmt *ra, long ignoremissing);
void agmt_update_done(Repl_Agmt *ra, int is_total);
uint64_t agmt_get_protocol_timeout(Repl_Agmt *agmt);
void agmt_set_protocol_timeout(Repl_Agmt *agmt, uint64_t timeout);
void agmt_update_maxcsn(Replica *r, Slapi_DN *sdn, int op, LDAPMod **mods, CSN *csn);
void add_agmt_maxcsns(Slapi_Entry *e, Replica *r);
void agmt_remove_maxcsn(Repl_Agmt *ra);
int agmt_maxcsn_to_smod(Replica *r, Slapi_Mod *smod);
int agmt_set_WaitForAsyncResults(Repl_Agmt *ra, const Slapi_Entry *e);

/* In repl5_agmtlist.c */
int agmtlist_config_init(void);
void agmtlist_shutdown(void);
void agmtlist_notify_all(Slapi_PBlock *pb);
Object *agmtlist_get_first_agreement_for_replica(Replica *r);
Object *agmtlist_get_next_agreement_for_replica(Replica *r, Object *prev);
int agmtlist_agmt_exists(const Repl_Agmt *ra);

/* In repl5_backoff.c */
typedef struct backoff_timer Backoff_Timer;
#define BACKOFF_FIXED 1
#define BACKOFF_EXPONENTIAL 2
#define BACKOFF_RANDOM 3
Backoff_Timer *backoff_new(int timer_type, int initial_interval, int max_interval);
time_t backoff_reset(Backoff_Timer *bt, slapi_eq_fn_t callback, void *callback_data);
time_t backoff_step(Backoff_Timer *bt);
int backoff_expired(Backoff_Timer *bt, int margin);
void backoff_delete(Backoff_Timer **btp);

#define REPL_PROTOCOL_UNKNOWN 0
#define REPL_PROTOCOL_50_INCREMENTAL 2
#define REPL_PROTOCOL_50_TOTALUPDATE 3

/* Type of extensions that can be registered */
typedef enum {
    REPL_SUP_EXT_OP,     /* extension for Operation object, replication supplier */
    REPL_SUP_EXT_CONN,   /* extension for Connection object, replication supplier */
    REPL_CON_EXT_OP,     /* extension for Operation object, replication consumer */
    REPL_CON_EXT_CONN,   /* extension for Connection object, replication consumer */
    REPL_CON_EXT_MTNODE, /* extension for mapping_tree_node object, replication consumer */
    REPL_EXT_ALL
} ext_type;

/* Operation extension functions - supplier_operation_extension.c */

/* --- supplier operation extension --- */
typedef struct supplier_operation_extension
{
    int prevent_recursive_call;
    struct slapi_operation_parameters *operation_parameters;
    char *repl_gen;
} supplier_operation_extension;

/* extension construct/destructor */
void *supplier_operation_extension_constructor(void *object, void *parent);
void supplier_operation_extension_destructor(void *ext, void *object, void *parent);

/* --- consumer operation extension --- */
typedef struct consumer_operation_extension
{
    int has_cf; /* non-zero if the operation contains a copiedFrom/copyingFrom attr */
    void *search_referrals;
} consumer_operation_extension;

/* extension construct/destructor */
void *consumer_operation_extension_constructor(void *object, void *parent);
void consumer_operation_extension_destructor(void *ext, void *object, void *parent);


/* Connection extension functions - repl_connext.c */
typedef struct consumer_connection_extension
{
    int repl_protocol_version; /* the replication protocol version number the supplier is talking. */
    Replica *replica_acquired;    /* replica */
    void *supplier_ruv;        /* RUV* */
    int isreplicationsession;
    Slapi_Connection *connection;
    PRLock *lock;    /* protects entire structure */
    int in_use_opid; /* the id of the operation actively using this, else -1 */
} consumer_connection_extension;

/* extension construct/destructor */
void *consumer_connection_extension_constructor(void *object, void *parent);
void consumer_connection_extension_destructor(void *ext, void *object, void *parent);

/* extension helpers for managing exclusive access */
consumer_connection_extension *consumer_connection_extension_acquire_exclusive_access(void *conn, uint64_t connid, int opid);
int consumer_connection_extension_relinquish_exclusive_access(void *conn, uint64_t connid, int opid, PRBool force);

/* mapping tree extension - stores replica object */
typedef struct multimaster_mtnode_extension
{
    Object *replica;
    Objset *removed_replicas;
} multimaster_mtnode_extension;
void *multimaster_mtnode_extension_constructor(void *object, void *parent);
void multimaster_mtnode_extension_destructor(void *ext, void *object, void *parent);

/* general extension functions - repl_ext.c */
void repl_sup_init_ext();                            /* initializes registrations - must be called first */
void repl_con_init_ext();                            /* initializes registrations - must be called first */
int repl_sup_register_ext(ext_type type);            /* registers an extension of the specified type */
int repl_con_register_ext(ext_type type);            /* registers an extension of the specified type */
void *repl_sup_get_ext(ext_type type, void *object); /* retireves the extension from the object */
void *repl_con_get_ext(ext_type type, void *object); /* retireves the extension from the object */


/* In repl5_connection.c
 * keep in sync with conn_result2string
 */
typedef struct repl_connection Repl_Connection;
typedef enum {
    CONN_OPERATION_SUCCESS,
    CONN_OPERATION_FAILED,
    CONN_NOT_CONNECTED,
    CONN_SUPPORTS_DS5_REPL,
    CONN_DOES_NOT_SUPPORT_DS5_REPL,
    CONN_SCHEMA_UPDATED,
    CONN_SCHEMA_NO_UPDATE_NEEDED,
    CONN_LOCAL_ERROR,
    CONN_BUSY,
    CONN_SSL_NOT_ENABLED,
    CONN_TIMEOUT,
    CONN_SUPPORTS_DS71_REPL,
    CONN_DOES_NOT_SUPPORT_DS71_REPL,
    CONN_IS_READONLY,
    CONN_IS_NOT_READONLY,
    CONN_SUPPORTS_DIRSYNC,
    CONN_DOES_NOT_SUPPORT_DIRSYNC,
    CONN_IS_WIN2K3,
    CONN_NOT_WIN2K3,
    CONN_SUPPORTS_DS90_REPL,
    CONN_DOES_NOT_SUPPORT_DS90_REPL
} ConnResult;

char *conn_result2string(int result);
Repl_Connection *conn_new(Repl_Agmt *agmt);
ConnResult conn_connect(Repl_Connection *conn);
void conn_disconnect(Repl_Connection *conn);
void conn_delete(Repl_Connection *conn);
void conn_get_error(Repl_Connection *conn, int *operation, int *error);
void conn_get_error_ex(Repl_Connection *conn, int *operation, int *error, char **error_string);
ConnResult conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs, LDAPControl *update_control, int *message_id);
ConnResult conn_send_delete(Repl_Connection *conn, const char *dn, LDAPControl *update_control, int *message_id);
ConnResult conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods, LDAPControl *update_control, int *message_id);
ConnResult conn_send_rename(Repl_Connection *conn, const char *dn, const char *newrdn, const char *newparent, int deleteoldrdn, LDAPControl *update_control, int *message_id);
ConnResult conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid, struct berval *payload, LDAPControl *update_control, int *message_id);
const char *conn_get_status(Repl_Connection *conn);
void conn_start_linger(Repl_Connection *conn);
void conn_cancel_linger(Repl_Connection *conn);
ConnResult conn_replica_supports_ds5_repl(Repl_Connection *conn);
ConnResult conn_replica_supports_ds71_repl(Repl_Connection *conn);
ConnResult conn_replica_supports_ds90_repl(Repl_Connection *conn);
ConnResult conn_replica_is_readonly(Repl_Connection *conn);

ConnResult conn_read_entry_attribute(Repl_Connection *conn, const char *dn, char *type, struct berval ***returned_bvals);
ConnResult conn_push_schema(Repl_Connection *conn, CSN **remotecsn);
void conn_set_timeout(Repl_Connection *conn, long timeout);
long conn_get_timeout(Repl_Connection *conn);
void conn_set_agmt_changed(Repl_Connection *conn);
ConnResult conn_read_result(Repl_Connection *conn, int *message_id);
ConnResult conn_read_result_ex(Repl_Connection *conn, char **retoidp, struct berval **retdatap, LDAPControl ***returned_controls, int send_msgid, int *resp_msgid, int noblock);
LDAP *conn_get_ldap(Repl_Connection *conn);
void conn_lock(Repl_Connection *conn);
void conn_unlock(Repl_Connection *conn);
void conn_delete_internal_ext(Repl_Connection *conn);
const char *conn_get_bindmethod(Repl_Connection *conn);
void conn_set_tot_update_cb(Repl_Connection *conn, void *cb_data);
void conn_set_tot_update_cb_nolock(Repl_Connection *conn, void *cb_data);
void conn_get_tot_update_cb(Repl_Connection *conn, void **cb_data);
void conn_get_tot_update_cb_nolock(Repl_Connection *conn, void **cb_data);

/* In repl5_protocol.c */
typedef struct repl_protocol Repl_Protocol;
Repl_Protocol *prot_new(Repl_Agmt *agmt, int protocol_state);
void prot_start(Repl_Protocol *rp);
Repl_Agmt *prot_get_agreement(Repl_Protocol *rp);
/* initiate total protocol */
void prot_initialize_replica(Repl_Protocol *rp);
/* stop protocol session in progress */
void prot_stop(Repl_Protocol *rp);
void prot_delete(Repl_Protocol **rpp);
void prot_free(Repl_Protocol **rpp);
PRBool prot_set_active_protocol(Repl_Protocol *rp, PRBool total);
void prot_clear_active_protocol(Repl_Protocol *rp);
Repl_Connection *prot_get_connection(Repl_Protocol *rp);
void prot_resume(Repl_Protocol *rp, int wakeup_action);
void prot_notify_update(Repl_Protocol *rp);
void prot_notify_agmt_changed(Repl_Protocol *rp, char *agmt_name);
void prot_notify_window_opened(Repl_Protocol *rp);
void prot_notify_window_closed(Repl_Protocol *rp);
Replica *prot_get_replica(Repl_Protocol *rp);
void prot_replicate_now(Repl_Protocol *rp);

Repl_Protocol *agmt_get_protocol(Repl_Agmt *ra);

/* In repl5_replica.c */
typedef enum {
    REPLICA_TYPE_UNKNOWN,
    REPLICA_TYPE_PRIMARY,
    REPLICA_TYPE_READONLY,
    REPLICA_TYPE_UPDATABLE,
    REPLICA_TYPE_END
} ReplicaType;

#define RUV_STORAGE_ENTRY_UNIQUEID "ffffffff-ffffffff-ffffffff-ffffffff"
#define START_ITERATION_ENTRY_UNIQUEID "00000000-00000000-00000000-00000000"
#define START_ITERATION_ENTRY_DN "cn=start iteration"

typedef int (*FNEnumReplica)(Replica *r, void *arg);

/* this function should be called to construct the replica object
   from the data already in the DIT */
Replica *replica_new(const Slapi_DN *root);
Replica *windows_replica_new(const Slapi_DN *root);
/* this function should be called to construct the replica object
   during addition of the replica over LDAP */
int replica_new_from_entry(Slapi_Entry *e, char *errortext, PRBool is_add_operation, Replica **r);
void replica_destroy(void **arg);
int replica_subentry_update(Slapi_DN *repl_root, ReplicaId rid);
int replica_subentry_check(Slapi_DN *repl_root, ReplicaId rid);
PRBool replica_get_exclusive_access(Replica *r, PRBool *isInc, uint64_t connid, int opid, const char *locking_purl, char **current_purl);
void replica_relinquish_exclusive_access(Replica *r, uint64_t connid, int opid);
PRBool replica_get_tombstone_reap_active(const Replica *r);
const Slapi_DN *replica_get_root(const Replica *r);
const char *replica_get_name(const Replica *r);
uint64_t replica_get_locking_conn(const Replica *r);
ReplicaId replica_get_rid(const Replica *r);
void replica_set_rid(Replica *r, ReplicaId rid);
PRBool replica_is_initialized(const Replica *r);
Object *replica_get_ruv(const Replica *r);
/* replica now owns the RUV */
void replica_set_ruv(Replica *r, RUV *ruv);
Object *replica_get_csngen(const Replica *r);
ReplicaType replica_get_type(const Replica *r);
void replica_set_type(Replica *r, ReplicaType type);
PRBool replica_is_updatedn(Replica *r, const Slapi_DN *sdn);
void replica_set_updatedn(Replica *r, const Slapi_ValueSet *vs, int mod_op);
void replica_set_groupdn(Replica *r, const Slapi_ValueSet *vs, int mod_op);
char *replica_get_generation(const Replica *r);

/* currently supported flags */
#define REPLICA_LOG_CHANGES 0x1 /* enable change logging */

PRBool replica_is_flag_set(const Replica *r, uint32_t flag);
void replica_set_flag(Replica *r, uint32_t flag, PRBool clear);
void replica_replace_flags(Replica *r, uint32_t flags);
void replica_dump(Replica *r);
void replica_set_enabled(Replica *r, PRBool enable);
Replica *replica_get_replica_from_dn(const Slapi_DN *dn);
Replica *replica_get_replica_from_root(const char *repl_root);
int replica_update_ruv(Replica *replica, const CSN *csn, const char *replica_purl);
Replica *replica_get_replica_for_op(Slapi_PBlock *pb);
/* the functions below manipulate replica hash */
int replica_init_name_hash(void);
void replica_destroy_name_hash(void);
int replica_add_by_name(const char *name, Replica *replica);
int replica_delete_by_name(const char *name);
Replica *replica_get_by_name(const char *name);
void replica_flush(Replica *r);
void replica_set_csn_assigned(Replica *r);
void replica_get_referrals(const Replica *r, char ***referrals);
void replica_set_referrals(Replica *r, const Slapi_ValueSet *vs);
int replica_update_csngen_state(Replica *r, const RUV *ruv);
int replica_update_csngen_state_ext(Replica *r, const RUV *ruv, const CSN *extracsn);
CSN *replica_get_purge_csn(const Replica *r);
int replica_log_ruv_elements(const Replica *r);
void replica_enumerate_replicas(FNEnumReplica fn, void *arg);
int replica_reload_ruv(Replica *r);
int replica_check_for_data_reload(Replica *r, void *arg);
/* the functions below manipulate replica dn hash */
int replica_init_dn_hash(void);
void replica_destroy_dn_hash(void);
int replica_add_by_dn(const char *dn);
int replica_delete_by_dn(const char *dn);
int replica_is_being_configured(const char *dn);
void consumer5_set_mapping_tree_state_for_replica(const Replica *r, RUV *supplierRuv);
Replica *replica_get_for_backend(const char *be_name);
void replica_set_purge_delay(Replica *r, uint32_t purge_delay);
void replica_set_tombstone_reap_interval(Replica *r, long interval);
void replica_update_ruv_consumer(Replica *r, RUV *supplier_ruv);
Slapi_Entry *get_in_memory_ruv(Slapi_DN *suffix_sdn);
int replica_write_ruv(Replica *r);
char *replica_get_dn(Replica *r);
void replica_check_for_tasks(time_t when, void *arg);
void replica_update_state(time_t when, void *arg);
void replica_reset_csn_pl(Replica *r);
uint64_t replica_get_protocol_timeout(Replica *r);
void replica_set_protocol_timeout(Replica *r, uint64_t timeout);
uint64_t replica_get_release_timeout(Replica *r);
void replica_set_release_timeout(Replica *r, uint64_t timeout);
void replica_set_groupdn_checkinterval(Replica *r, int timeout);
uint64_t replica_get_backoff_min(Replica *r);
uint64_t replica_get_backoff_max(Replica *r);
void replica_set_backoff_min(Replica *r, uint64_t min);
void replica_set_backoff_max(Replica *r, uint64_t max);
int replica_get_agmt_count(Replica *r);
void replica_incr_agmt_count(Replica *r);
void replica_decr_agmt_count(Replica *r);
uint64_t replica_get_precise_purging(Replica *r);
void replica_set_precise_purging(Replica *r, uint64_t on_off);
PRBool ignore_error_and_keep_going(int error);
void replica_check_release_timeout(Replica *r, Slapi_PBlock *pb);
void replica_lock_replica(Replica *r);
void replica_unlock_replica(Replica *r);
void *replica_get_cl_info(Replica *r);
int replica_set_cl_info(Replica *r, void *cl);

/* The functions below handles the state flag */
/* Current internal state flags */
/* The replica can be busy and not other flag,
 * it means that the protocol has ended, but the work is not done yet.
 * It happens on total protocol, the end protocol has been received,
 * and the thread waits for import to finish
 */
#define REPLICA_IN_USE                   1 /* The replica is busy */
#define REPLICA_INCREMENTAL_IN_PROGRESS  2 /* Set only between start and stop inc */
#define REPLICA_TOTAL_IN_PROGRESS        4 /* Set only between start and stop total */
#define REPLICA_AGREEMENTS_DISABLED      8 /* Replica is offline */
#define REPLICA_TOTAL_EXCL_SEND         16 /* The server is either sending or receiving the total update.  Introducing it if SEND
                                              is active, RECV should back off. And vice versa.  But SEND can coexist. */
#define REPLICA_TOTAL_EXCL_RECV         32 /* ditto */

PRBool replica_is_state_flag_set(Replica *r, int32_t flag);
void replica_set_state_flag(Replica *r, uint32_t flag, PRBool clear);
void replica_set_tombstone_reap_stop(Replica *r, PRBool val);
void replica_enable_replication(Replica *r);
void replica_disable_replication(Replica *r);
int replica_start_agreement(Replica *r, Repl_Agmt *ra);
int windows_replica_start_agreement(Replica *r, Repl_Agmt *ra);

int32_t replica_generate_next_csn(Slapi_PBlock *pb, const CSN *basecsn, CSN **opcsn);
int replica_get_attr(Slapi_PBlock *pb, const char *type, void *value);

/* mapping tree extensions manipulation */
void multimaster_mtnode_extension_init(void);
void multimaster_mtnode_extension_destroy(void);
void multimaster_mtnode_construct_replicas(void);

void multimaster_be_state_change(void *handle, char *be_name, int old_be_state, int new_be_state);

#define CLEANRIDSIZ 64 /* maximum number for concurrent CLEANALLRUV tasks */
#define CLEANRID_BUFSIZ 128

typedef struct _cleanruv_data
{
    Replica *replica;
    ReplicaId rid;
    Slapi_Task *task;
    struct berval *payload;
    CSN *maxcsn;
    char *repl_root;
    Slapi_DN *sdn;
    char *certify;
    char *force;
    PRBool original_task;
} cleanruv_data;

typedef struct _cleanruv_purge_data
{
    int cleaned_rid;
    const Slapi_DN *suffix_sdn;
    Replica *replica;
} cleanruv_purge_data;

typedef struct _csngen_test_data
{
    Slapi_Task *task;
} csngen_test_data;

/* In repl5_replica_config.c */
int replica_config_init(void);
void replica_config_destroy(void);
int get_replica_type(Replica *r);
int replica_execute_cleanruv_task_ext(Replica *r, ReplicaId rid);
void add_cleaned_rid(cleanruv_data *data);
int is_cleaned_rid(ReplicaId rid);
int32_t check_and_set_cleanruv_task_count(ReplicaId rid);
int32_t check_and_set_abort_cleanruv_task_count(void);
int replica_cleanall_ruv_abort(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
void replica_cleanallruv_thread_ext(void *arg);
void stop_ruv_cleaning(void);
int task_aborted(void);
void replica_abort_task_thread(void *arg);
void remove_cleaned_rid(ReplicaId rid);
int process_repl_agmts(Replica *replica, int *agmt_info, char *oid, Slapi_Task *task, struct berval *payload, int op);
int decode_cleanruv_payload(struct berval *extop_value, char **payload);
struct berval *create_cleanruv_payload(char *value);
void ruv_get_cleaned_rids(RUV *ruv, ReplicaId *rids);
void add_aborted_rid(ReplicaId rid, Replica *r, char *repl_root, char *certify_all, PRBool original_task);
int is_task_aborted(ReplicaId rid);
void delete_aborted_rid(Replica *replica, ReplicaId rid, char *repl_root, char *certify_all, PRBool original_task, int skip);
int is_pre_cleaned_rid(ReplicaId rid);
void set_cleaned_rid(ReplicaId rid);
void cleanruv_log(Slapi_Task *task, int rid, char *task_type, int sev_level, char *fmt, ...);
char *replica_cleanallruv_get_local_maxcsn(ReplicaId rid, char *base_dn);

/* replutil.c */
LDAPControl *create_managedsait_control(void);
LDAPControl *create_backend_control(Slapi_DN *sdn);
void repl_set_mtn_state_and_referrals(const Slapi_DN *sdn, const char *mtn_state, const RUV *ruv, char **ruv_referrals, char **other_referrals);
void repl_set_repl_plugin_path(const char *path);
int repl_config_valid_num(const char *config_attr, char *config_attr_value, int64_t min, int64_t max,
                          int *returncode, char *errortext, int64_t *retval);

/* repl5_updatedn_list.c */
typedef void *ReplicaUpdateDNList;
typedef int (*FNEnumDN)(Slapi_DN *dn, void *arg);
ReplicaUpdateDNList replica_updatedn_list_new(const Slapi_Entry *entry);
Slapi_ValueSet *replica_updatedn_group_new(const Slapi_Entry *entry);
ReplicaUpdateDNList replica_groupdn_list_new(const Slapi_ValueSet *vs);
void replica_updatedn_list_free(ReplicaUpdateDNList list);
void replica_updatedn_list_replace(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_group_replace(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_delete(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_add(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_add_ext(ReplicaUpdateDNList list, const Slapi_ValueSet *vs, int group_update);
PRBool replica_updatedn_list_ismember(ReplicaUpdateDNList list, const Slapi_DN *dn);
char *replica_updatedn_list_to_string(ReplicaUpdateDNList list, const char *delimiter);
void replica_updatedn_list_enumerate(ReplicaUpdateDNList list, FNEnumDN fn, void *arg);

/* enabling developper traces for MMR to understand the total/inc protocol state machines */
#ifdef DEV_DEBUG
#define SLAPI_LOG_DEV_DEBUG SLAPI_LOG_DEBUG
#define dev_debug(a) slapi_log_err(SLAPI_LOG_DEV_DEBUG, "DEV_DEBUG", "%s\n", a)
#else
#define dev_debug(a)
#endif

void repl5_set_debug_timeout(const char *val);
/* temp hack XXX */
ReplicaId agmt_get_consumerRID(Repl_Agmt *ra);

/* For replica release tuning */
int agmt_get_WaitForAsyncResults(Repl_Agmt *ra);

PRBool ldif_dump_is_running(void);

void windows_init_agreement_from_entry(Repl_Agmt *ra, Slapi_Entry *e);
int windows_handle_modify_agreement(Repl_Agmt *ra, const char *type, Slapi_Entry *e);
void windows_agreement_delete(Repl_Agmt *ra);
Repl_Connection *windows_conn_new(Repl_Agmt *agmt);
void windows_conn_delete(Repl_Connection *conn);
void windows_update_done(Repl_Agmt *ra, int is_total);

/* repl_session_plugin.c */
void repl_session_plugin_init(void);
void repl_session_plugin_call_agmt_init_cb(Repl_Agmt *ra);
int repl_session_plugin_call_pre_acquire_cb(const Repl_Agmt *ra, int is_total, char **data_guid, struct berval **data);
int repl_session_plugin_call_post_acquire_cb(const Repl_Agmt *ra, int is_total, const char *data_guid, const struct berval *data);
int repl_session_plugin_call_recv_acquire_cb(const char *repl_area, int is_total, const char *data_guid, const struct berval *data);
int repl_session_plugin_call_reply_acquire_cb(const char *repl_area, int is_total, char **data_guid, struct berval **data);
void repl_session_plugin_call_destroy_agmt_cb(const Repl_Agmt *ra);

#endif /* _REPL5_H_ */
