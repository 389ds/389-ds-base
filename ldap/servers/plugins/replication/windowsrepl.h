/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5.h - 5.0 replication header */
#include "repl.h"
#include "repl5.h"
 
#ifndef _WINDOWSREPL5_H_
#define _WINDOWSREPL5_H_

#include <limits.h>
#include <time.h>
#include <stdio.h>

#undef REPL_PLUGIN_NAME
#define REPL_PLUGIN_NAME "WindowsSyncPlugin"
#define WINDOWS_REPL_PLUGIN_NAME REPL_PLUGIN_NAME

char* get_repl_session_id (Slapi_PBlock *pb, char *id, CSN **opcsn);
#include <string.h>
#ifndef _WIN32
#include <sys/param.h>
#endif /* _WIN32 */

#include "portable.h" /* GGOODREPL - is this cheating? */
#include "repl_shared.h"
#include "llist.h"
#include "repl5_ruv.h"
#include "cl4.h"


/* Attribute names for replication agreement attributes */
extern const char *type_nsdsWindowsReplicaCookie;
extern char *windows_repl_plugin_name;

/* windows plugin points */
int windows_preop_bind (Slapi_PBlock *pb); 
int windows_preop_add (Slapi_PBlock *pb);
int windows_preop_delete (Slapi_PBlock *pb);
int windows_preop_modify (Slapi_PBlock *pb);
int windows_preop_modrdn (Slapi_PBlock *pb);
int windows_preop_search (Slapi_PBlock *pb);
int windows_preop_compare (Slapi_PBlock *pb);
int windows_bepreop_add (Slapi_PBlock *pb);
int windows_bepreop_delete (Slapi_PBlock *pb);
int windows_bepreop_modify (Slapi_PBlock *pb);
int windows_bepreop_modrdn (Slapi_PBlock *pb);
int windows_bepostop_modrdn (Slapi_PBlock *pb);
int windows_bepostop_delete (Slapi_PBlock *pb);
int windows_postop_bind (Slapi_PBlock *pb);
int windows_postop_add (Slapi_PBlock *pb);
int windows_postop_delete (Slapi_PBlock *pb);
int windows_postop_modify (Slapi_PBlock *pb);
int windows_postop_modrdn (Slapi_PBlock *pb);

/* In repl5_init.c */
char* get_thread_private_agmtname ();
void  set_thread_private_agmtname (const char *agmtname);
void* get_thread_private_cache ();
void  set_thread_private_cache (void *buf);
char* get_repl_session_id (Slapi_PBlock *pb, char *id, CSN **opcsn);

/* In repl_extop.c */
int windows_extop_StartNSDS50ReplicationRequest(Slapi_PBlock *pb);
int windows_extop_EndNSDS50ReplicationRequest(Slapi_PBlock *pb);
int extop_noop(Slapi_PBlock *pb);
struct berval *NSDS50StartReplicationRequest_new(const char *protocol_oid,
	const char *repl_root, char **extra_referrals, CSN *csn);
struct berval *NSDS50EndReplicationRequest_new(char *repl_root);
int decode_repl_ext_response(struct berval *data, int *response_code,
	struct berval ***ruv_bervals);

/* extension construct/destructor */
void* windows_operation_extension_constructor (void *object, void *parent);
void windows_operation_extension_destructor (void* ext,void *object, void *parent);
void* windows_consumer_operation_extension_constructor (void *object, void *parent);
void windows_consumer_operation_extension_destructor (void* ext,void *object, void *parent);
void windows_mtnode_extension_destructor (void* ext, void *object, void *parent);
void *windows_mtnode_extension_constructor (void *object, void *parent);





typedef struct windows_operation_extension
{
  int prevent_recursive_call;
  struct slapi_operation_parameters *operation_parameters;
  char *repl_gen;
} windows_operation_extension;

/* windows_ext.c */
void windows_repl_sup_init_ext ();    
void windows_repl_con_init_ext ();
int windows_repl_sup_register_ext (ext_type type);  // never used?
int windows_repl_con_register_ext (ext_type type);  // never used
void* windows_repl_sup_get_ext (ext_type type, void *object); 
void* windows_repl_con_get_ext (ext_type type, void *object); 



/* In repl5_total.c */
int windows_extop_NSDS50ReplicationEntry(Slapi_PBlock *pb);

/* In repl_controls.c */
int create_NSDS50ReplUpdateInfoControl(const char *uuid,
	const char *superior_uuid, const CSN *csn,
	LDAPMod **modify_mods, LDAPControl **ctrlp);
void destroy_NSDS50ReplUpdateInfoControl(LDAPControl **ctrlp);
int decode_NSDS50ReplUpdateInfoControl(LDAPControl **controlsp,
    char **uuid, char **newsuperior_uuid, CSN **csn, LDAPMod ***modrdn_mods);

/* In repl5_replsupplier.c */
/* typedef struct repl_supplier Repl_Supplier; */
Repl_Supplier *replsupplier_init(Slapi_Entry *e);
void replsupplier_configure(Repl_Supplier *rs, Slapi_PBlock *pb);
void replsupplier_start(Repl_Supplier *rs);
void replsupplier_stop(Repl_Supplier *rs);
void replsupplier_destroy(Repl_Supplier **rs);
void replsupplier_notify(Repl_Supplier *rs, PRUint32 eventmask);
PRUint32 replsupplier_get_status(Repl_Supplier *rs);

/* In repl5_plugins.c */
int windows_set_local_purl();
const char *windows_get_local_purl();
PRBool windows_started();

/* In repl5_schedule.c */
/* typedef struct schedule Schedule; */
/* typedef void (*window_state_change_callback)(void *arg, PRBool opened); */
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
#define REPLICATION_SESSION_SUCCESS	0

/* In repl5_bos.c */
/* typedef struct repl_bos Repl_Bos; */

/* In repl5_agmt.c */
/* typedef struct repl5agmt Repl_Agmt; */
/* #define TRANSPORT_FLAG_SSL 1 */
/* #define TRANSPORT_FLAG_TLS 2 */
/* #define BINDMETHOD_SIMPLE_AUTH 1 */
/* #define BINDMETHOD_SSL_CLIENTAUTH 2 */
Repl_Agmt *windows_agmt_new_from_entry(Slapi_Entry *e);
/* Repl_Agmt *agmt_new_from_pblock(Slapi_PBlock *pb); */
/* void agmt_delete(void **ra); */
/* const Slapi_DN *agmt_get_dn_byref(const Repl_Agmt *ra); */
/* int agmt_get_auto_initialize(const Repl_Agmt *ra); */
/* long agmt_get_timeout(const Repl_Agmt *ra); */
/* long agmt_get_busywaittime(const Repl_Agmt *ra); */
/* long agmt_get_pausetime(const Repl_Agmt *ra); */
int windows_agmt_start(Repl_Agmt *ra); 
int windows_agmt_stop(Repl_Agmt *ra); 
/* int agmt_replicate_now(Repl_Agmt *ra); */
/* char *agmt_get_hostname(const Repl_Agmt *ra); */
/* int agmt_get_port(const Repl_Agmt *ra); */
/* PRUint32 agmt_get_transport_flags(const Repl_Agmt *ra); */
/* char *agmt_get_binddn(const Repl_Agmt *ra); */
/* struct berval *agmt_get_credentials(const Repl_Agmt *ra); */
/* int agmt_get_bindmethod(const Repl_Agmt *ra); */
Slapi_DN *windows_agmt_get_replarea(const Repl_Agmt *ra); 
/* int agmt_is_fractional(const Repl_Agmt *ra); */
/* int agmt_is_fractional_attr(const Repl_Agmt *ra, const char *attrname); */
/* int agmt_is_50_mm_protocol(const Repl_Agmt *ra); */
/* int agmt_matches_name(const Repl_Agmt *ra, const Slapi_DN *name); */
/* int agmt_replarea_matches(const Repl_Agmt *ra, const Slapi_DN *name); */
int windows_agmt_schedule_in_window_now(const Repl_Agmt *ra);
int windows_agmt_set_schedule_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); 
/* int agmt_set_timeout_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
/* int agmt_set_busywaittime_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
/* int agmt_set_pausetime_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
int windows_agmt_set_credentials_from_entry( Repl_Agmt *ra, const Slapi_Entry *e );
/* int agmt_set_binddn_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
/* int agmt_set_bind_method_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
/* int agmt_set_transportinfo_from_entry( Repl_Agmt *ra, const Slapi_Entry *e ); */
const char *windows_agmt_get_long_name(const Repl_Agmt *ra);
/* int agmt_initialize_replica(const Repl_Agmt *agmt); */
/* void agmt_replica_init_done (const Repl_Agmt *agmt); */
/* void agmt_notify_change(Repl_Agmt *ra, Slapi_PBlock *pb); */
/* Object* agmt_get_consumer_ruv (Repl_Agmt *ra); */
/* ReplicaId agmt_get_consumer_rid ( Repl_Agmt *ra, void *conn ); */
/* int agmt_set_consumer_ruv (Repl_Agmt *ra, RUV *ruv); */
void windows_agmt_update_consumer_ruv (Repl_Agmt *ra); 
/* CSN* agmt_get_consumer_schema_csn (Repl_Agmt *ra); */
/* void agmt_set_consumer_schema_csn (Repl_Agmt *ra, CSN *csn); */
/* void agmt_set_last_update_in_progress (Repl_Agmt *ra, PRBool in_progress); */
/* void agmt_set_last_update_start (Repl_Agmt *ra, time_t start_time); */
/* void agmt_set_last_update_end (Repl_Agmt *ra, time_t end_time); */
/* void agmt_set_last_update_status (Repl_Agmt *ra, int ldaprc, int replrc, const char *msg); */
/* void agmt_set_update_in_progress (Repl_Agmt *ra, PRBool in_progress); */
/* void agmt_set_last_init_start (Repl_Agmt *ra, time_t start_time); */
/* void agmt_set_last_init_end (Repl_Agmt *ra, time_t end_time); */
/* void agmt_set_last_init_status (Repl_Agmt *ra, int ldaprc, int replrc, const char *msg); */
/* void agmt_inc_last_update_changecount (Repl_Agmt *ra, ReplicaId rid, int skipped); */
/* void agmt_get_changecount_string (Repl_Agmt *ra, char *buf, int bufsize); */

/* typedef struct replica Replica; */

/* In repl5_agmtlist.c */
int windows_agmtlist_config_init();
void windows_agmtlist_shutdown();
void windows_agmtlist_notify_all(Slapi_PBlock *pb);
Object* windows_agmtlist_get_first_agreement_for_replica(Replica *r);
Object* windows_agmtlist_get_next_agreement_for_replica(Replica *r, Object *prev);


/* In repl5_backoff.c */
/* typedef struct backoff_timer Backoff_Timer; */
#define BACKOFF_FIXED 1
#define BACKOFF_EXPONENTIAL 2
#define BACKOFF_RANDOM 3
Backoff_Timer *backoff_new(int timer_type, int initial_interval, int max_interval);
time_t backoff_reset(Backoff_Timer *bt, slapi_eq_fn_t callback, void *callback_data);
time_t backoff_step(Backoff_Timer *bt);
int backoff_expired(Backoff_Timer *bt, int margin);
void backoff_delete(Backoff_Timer **btp);

/* In repl5_connection.c */
/* typedef struct repl_connection Repl_Connection; */
/* typedef enum */
/* {    */
/*     CONN_OPERATION_SUCCESS, */
/* 	CONN_OPERATION_FAILED, */
/* 	CONN_NOT_CONNECTED, */
/* 	CONN_SUPPORTS_DS5_REPL, */
/* 	CONN_DOES_NOT_SUPPORT_DS5_REPL, */
/* 	CONN_SCHEMA_UPDATED, */
/* 	CONN_SCHEMA_NO_UPDATE_NEEDED, */
/* 	CONN_LOCAL_ERROR, */
/* 	CONN_BUSY, */
/* 	CONN_SSL_NOT_ENABLED, */
/* 	CONN_TIMEOUT */
/* } ConnResult;   */

Repl_Connection *windows_conn_new(Repl_Agmt *agmt);
ConnResult windows_conn_connect(Repl_Connection *conn);
void windows_conn_disconnect(Repl_Connection *conn);
void windows_conn_delete(Repl_Connection *conn);
void windows_conn_get_error(Repl_Connection *conn, int *operation, int *error);
ConnResult windows_conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_delete(Repl_Connection *conn, const char *dn,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_rename(Repl_Connection *conn, const char *dn,
	const char *newrdn, const char *newparent, int deleteoldrdn,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid,
	struct berval *payload, char **retoidp, struct berval **retdatap,
	LDAPControl *update_control, LDAPControl ***returned_controls);
const char *windows_conn_get_status(Repl_Connection *conn);
void windows_conn_start_linger(Repl_Connection *conn);
void windows_conn_cancel_linger(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_ds5_repl(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_dirsync(Repl_Connection *conn);
ConnResult windows_conn_read_entry_attribute(Repl_Connection *conn, const char *dn, char *type,
	struct berval ***returned_bvals);
ConnResult windows_conn_push_schema(Repl_Connection *conn, CSN **remotecsn);
void windows_conn_set_timeout(Repl_Connection *conn, long timeout);
void windows_conn_set_agmt_changed(Repl_Connection *conn);



/* In repl5_protocol.c */
typedef struct windows_repl_protocol Windows_Repl_Protocol;
Repl_Protocol *protocol_new(Repl_Agmt *agmt, int protocol_state); 
void protocol_start(Repl_Protocol *rp); 

Repl_Agmt *protocol_get_agreement(Repl_Protocol *rp); 
/* initiate total protocol */
void protocol_initialize_replica(Repl_Protocol *rp); 
/* stop protocol session in progress */
void protocol_stop(Repl_Protocol *rp); 
void protocol_free(Repl_Protocol **rpp); 
void protocol_delete(Repl_Protocol **rpp);
PRBool protocol_set_active_protocol (Repl_Protocol *rp, PRBool total); 
void protocol_clear_active_protocol (Repl_Protocol *rp); 
Repl_Connection *protocol_get_connection(Repl_Protocol *rp); 
void protocol_resume(Repl_Protocol *rp, int wakeup_action);
void protocol_notify_update(Repl_Protocol *rp);
void protocol_notify_agmt_changed(Repl_Protocol *rp, char * agmt_name);
void protocol_notify_window_opened (Repl_Protocol *rp);
void protocol_notify_window_closed (Repl_Protocol *rp);
Object *protocol_get_replica_object(Repl_Protocol *rp);
void protocol_replicate_now(Repl_Protocol *rp);

/* In repl5_replica.c */
/* typedef enum */
/* {    */
/*     REPLICA_TYPE_UNKNOWN, */
/* 	REPLICA_TYPE_PRIMARY, */
/* 	REPLICA_TYPE_READONLY, */
/* 	REPLICA_TYPE_UPDATABLE, */
/*     REPLICA_TYPE_END	 */
/* } ReplicaType;   */

/* #define RUV_STORAGE_ENTRY_UNIQUEID "ffffffff-ffffffff-ffffffff-ffffffff" */
/* #define START_ITERATION_ENTRY_UNIQUEID "00000000-00000000-00000000-00000000" */
/* #define START_ITERATION_ENTRY_DN       "cn=start iteration" */

/* typedef int (*FNEnumReplica) (Replica *r, void *arg); */

/* this function should be called to construct the replica object
   from the data already in the DIT */
 Replica *windows_replica_new(const Slapi_DN *root); 

#define REPLICA_IN_USE	1 /* The replica is busy */
#define REPLICA_INCREMENTAL_IN_PROGRESS 2 /* Set only between start and stop inc */
#define REPLICA_TOTAL_IN_PROGRESS 4 /* Set only between start and stop total */
#define REPLICA_AGREEMENTS_DISABLED 8 /* Replica is offline */
PRBool replica_is_state_flag_set(Replica *r, PRInt32 flag);
void replica_set_state_flag (Replica *r, PRUint32 flag, PRBool clear); 
void replica_enable_replication (Replica *r);
void replica_disable_replication (Replica *r, Object *r_obj);
int windows_replica_start_agreement(Replica *r, Repl_Agmt *ra); 

CSN* replica_generate_next_csn ( Slapi_PBlock *pb, const CSN *basecsn );
int replica_get_attr ( Slapi_PBlock *pb, const char *type, void *value );

/* mapping tree extensions manipulation */
void windows_mtnode_extension_init ();
void windows_mtnode_extension_destroy ();
void windows_mtnode_construct_replicas ();

void windows_be_state_change (void *handle, char *be_name, int old_be_state, int new_be_state);

/* In repl5_replica_config.c */
int windows_replica_config_init();
void windows_replica_config_destroy ();

/* replutil.c */
LDAPControl* create_managedsait_control ();
LDAPControl* create_backend_control(Slapi_DN *sdn);
void repl_set_mtn_state_and_referrals(const Slapi_DN *sdn, const char *mtn_state,
									  const RUV *ruv, char **ruv_referrals,
									  char **other_referrals);
void repl_set_repl_plugin_path(const char *path);

/* repl5_updatedn_list.c */
/* typedef void *ReplicaUpdateDNList; */
/* typedef int (*FNEnumDN)(Slapi_DN *dn, void *arg); */
ReplicaUpdateDNList replica_updatedn_list_new(const Slapi_Entry *entry);
void replica_updatedn_list_free(ReplicaUpdateDNList list);
void replica_updatedn_list_replace(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_delete(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
void replica_updatedn_list_add(ReplicaUpdateDNList list, const Slapi_ValueSet *vs);
PRBool replica_updatedn_list_ismember(ReplicaUpdateDNList list, const Slapi_DN *dn);
char *replica_updatedn_list_to_string(ReplicaUpdateDNList list, const char *delimiter);
void replica_updatedn_list_enumerate(ReplicaUpdateDNList list, FNEnumDN fn, void *arg);


/* windows_private.c */
PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra);

/* windows_protocol.c */
int add_or_modify_user(Slapi_Entry *e);


void repl5_set_debug_timeout(const char *val);

typedef struct windows_mtnode_extension
{
  Object *replica;
} windows_mtnode_extension;





#endif /* _WINDOWSREPL5_H_ */

