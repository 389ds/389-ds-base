/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/**
 * LDAP content synchronization plug-in
 */

#include <stdio.h>
#include <string.h>
#include "slap.h"
#include "slapi-plugin.h"
#include "slapi-private.h"

#define PLUGIN_NAME "content-sync-plugin"

#define SYNC_PLUGIN_SUBSYSTEM "content-sync-plugin"
#define SYNC_PREOP_DESC       "content-sync-preop-subplugin"
#define SYNC_POSTOP_DESC      "content-sync-postop-subplugin"
#define SYNC_INT_POSTOP_DESC  "content-sync-int-postop-subplugin"
#define SYNC_BETXN_PREOP_DESC "content-sync-betxn-preop-subplugin"
#define SYNC_BE_POSTOP_DESC "content-sync-be-post-subplugin"

#define SYNC_ALLOW_OPENLDAP_COMPAT "syncrepl-allow-openldap"

#define OP_FLAG_SYNC_PERSIST 0x01

#define E_SYNC_REFRESH_REQUIRED 0x1000

#define CL_ATTR_CHANGENUMBER "changenumber"
#define CL_ATTR_ENTRYDN      "targetDn"
#define CL_ATTR_UNIQUEID     "targetUniqueId"
#define CL_ATTR_ENTRYUUID    "targetEntryUUID"
#define CL_ATTR_CHGTYPE      "changetype"
#define CL_ATTR_NEWSUPERIOR  "newsuperior"
#define CL_SRCH_BASE         "cn=changelog"

#define SYNC_INVALID_CHANGENUM ((unsigned long)-1)

typedef struct sync_cookie
{
    char *cookie_client_signature;
    char *cookie_server_signature;
    unsigned long cookie_change_info;
    PRBool openldap_compat;
} Sync_Cookie;

typedef struct sync_update
{
    char *upd_uuid;
    char *upd_euuid;
    int upd_chgtype;
    Slapi_Entry *upd_e;
} Sync_UpdateNode;

#define SYNC_CALLBACK_PREINIT (-1)

typedef struct sync_callback
{
    Slapi_PBlock *orig_pb;
    unsigned long changenr;
    unsigned long change_start;
    int cb_err;
    Sync_UpdateNode *cb_updates;
    PRBool openldap_compat;
} Sync_CallBackData;

/* Pending list flags 
 * OPERATION_PL_PENDING: operation not yet completed
 * OPERATION_PL_SUCCEEDED: operation completed successfully
 * OPERATION_PL_FAILED: operation completed and failed
 * OPERATION_PL_IGNORED: operation completed but with an undefine status
 */
typedef enum _pl_flags {
    OPERATION_PL_HEAD = 1,
    OPERATION_PL_PENDING = 2,
    OPERATION_PL_SUCCEEDED = 3,
    OPERATION_PL_FAILED = 4,
    OPERATION_PL_IGNORED = 5
} pl_flags_t;

/* Pending list operations.
 * it contains a list ('next') of nested operations. The
 * order the same order that the server applied the operation
 * see https://www.port389.org/docs/389ds/design/content-synchronization-plugin.html#queue-and-pending-list
 */
typedef struct OPERATION_PL_CTX
{
    Operation *op;      /* Pending operation, should not be freed as it belongs to the pblock */
    pl_flags_t flags;  /* operation is completed (set to TRUE in POST) */
    Slapi_Entry *entry; /* entry to be store in the enqueued node. 1st arg sync_queue_change */
    Slapi_Entry *eprev; /* pre-entry to be stored in the enqueued node. 2nd arg sync_queue_change */
    ber_int_t chgtype;  /* change type to be stored in the enqueued node. 3rd arg of sync_queue_change */
    struct OPERATION_PL_CTX *next; /* list of nested operation, the head of the list is the primary operation */
} OPERATION_PL_CTX_T;

OPERATION_PL_CTX_T * get_thread_primary_op(void);
void set_thread_primary_op(OPERATION_PL_CTX_T *op);

void sync_register_allow_openldap_compat(PRBool allow);
int sync_register_operation_extension(void);
int sync_unregister_operation_entension(void);

int sync_srch_refresh_pre_search(Slapi_PBlock *pb);
int sync_srch_refresh_post_search(Slapi_PBlock *pb);
int sync_srch_refresh_pre_entry(Slapi_PBlock *pb);
int sync_srch_refresh_pre_result(Slapi_PBlock *pb);
int sync_del_persist_post_op(Slapi_PBlock *pb);
int sync_mod_persist_post_op(Slapi_PBlock *pb);
int sync_modrdn_persist_post_op(Slapi_PBlock *pb);
int sync_add_persist_post_op(Slapi_PBlock *pb);
int sync_update_persist_betxn_pre_op(Slapi_PBlock *pb);

int sync_parse_control_value(struct berval *psbvp, ber_int_t *mode, int *reload, char **cookie);
int sync_create_state_control(Slapi_Entry *e, LDAPControl **ctrlp, int type, Sync_Cookie *cookie, PRBool openldap_compat);
int sync_create_sync_done_control(LDAPControl **ctrlp, int refresh, char *cookie);
int sync_intermediate_msg(Slapi_PBlock *pb, int tag, Sync_Cookie *cookie, char **uuids);
int sync_result_msg(Slapi_PBlock *pb, Sync_Cookie *cookie);
int sync_result_err(Slapi_PBlock *pb, int rc, char *msg);

Sync_Cookie *sync_cookie_create(Slapi_PBlock *pb, Sync_Cookie *client_cookie);
void sync_cookie_update(Sync_Cookie *cookie, Slapi_Entry *ec);
Sync_Cookie *sync_cookie_parse(char *cookie, PRBool *cookie_refresh, PRBool *allow_openldap_compat);
int sync_cookie_isvalid(Sync_Cookie *testcookie, Sync_Cookie *refcookie);
void sync_cookie_free(Sync_Cookie **freecookie);
char *sync_cookie2str(Sync_Cookie *cookie);
int sync_number2int(char *nrstr);
unsigned long sync_number2ulong(char *nrstr);
char *sync_nsuniqueid2uuid(const char *nsuniqueid);
char *sync_entryuuid2uuid(const char *nsuniqueid);

int sync_is_active(Slapi_Entry *e, Slapi_PBlock *pb);
int sync_is_active_scope(const Slapi_DN *dn, Slapi_PBlock *pb);

int sync_refresh_update_content(Slapi_PBlock *pb, Sync_Cookie *client_cookie, Sync_Cookie *session_cookie);
int sync_refresh_initial_content(Slapi_PBlock *pb, int persist, PRThread *tid, Sync_Cookie *session_cookie);
int sync_read_entry_from_changelog(Slapi_Entry *cl_entry, void *cb_data);
int sync_send_entry_from_changelog(Slapi_PBlock *pb, int chg_req, char *uniqueid, Sync_Cookie *session_cookie);
void sync_send_deleted_entries(Slapi_PBlock *pb, Sync_UpdateNode *upd, int chg_count, Sync_Cookie *session_cookie);
void sync_send_modified_entries(Slapi_PBlock *pb, Sync_UpdateNode *upd, int chg_count, Sync_Cookie *session_cookie);

int sync_persist_initialize(int argc, char **argv);
PRThread *sync_persist_add(Slapi_PBlock *pb);
int sync_persist_startup(PRThread *tid, Sync_Cookie *session_cookie);
int sync_persist_terminate_all(void);
int sync_persist_terminate(PRThread *tid);

Slapi_PBlock *sync_pblock_copy(Slapi_PBlock *src);

/* prototype for functions not in slapi-plugin.h */
Slapi_ComponentId *plugin_get_default_component_id(void);


/*
 * Structures to handle the persitent phase of
 * Content Synchronization Requests
 *
 * A queue of entries being to be sent by a particular persistent
 * sync thread
 *
 * will be created in post op plugins
 */
typedef struct sync_queue_node
{
    Slapi_Entry *sync_entry;
    LDAPControl *pe_ctrls[2]; /* XXX ?? XXX */
    struct sync_queue_node *sync_next;
    int sync_chgtype;
} SyncQueueNode;

/*
 * Information about a single sync search
 *
 * will be created when a content sync control with
 * mode == 3 is decoded
 */
typedef struct sync_request
{
    Slapi_PBlock *req_pblock;
    Slapi_Operation *req_orig_op;
    PRLock *req_lock;
    PRThread *req_tid;
    char *req_orig_base;
    Slapi_Filter *req_filter;
    PRInt32 req_complete;
    Sync_Cookie *req_cookie;
    SyncQueueNode *ps_eq_head;
    SyncQueueNode *ps_eq_tail;
    int req_active;
    struct sync_request *req_next;
} SyncRequest;

/*
 * A list of established persistent synchronization searches.
 *
 * will be initialized at plugin initialization
 */
#define SYNC_MAX_CONCURRENT 10
typedef struct sync_request_list
{
    Slapi_RWLock *sync_req_rwlock; /* R/W lock struct to serialize access */
    SyncRequest *sync_req_head;    /* Head of list */
    pthread_mutex_t sync_req_cvarlock;    /* Lock for cvar */
    pthread_cond_t sync_req_cvar;         /* ps threads sleep on this */
    int sync_req_max_persist;
    int sync_req_cur_persist;
} SyncRequestList;

#define SYNC_FLAG_ADD_STATE_CTRL    0x01
#define SYNC_FLAG_ADD_DONE_CTRL     0x02
#define SYNC_FLAG_NO_RESULT         0x04
#define SYNC_FLAG_SEND_INTERMEDIATE 0x08

typedef struct sync_op_info
{
    int send_flag;       /* hint for preop plugins what to send */
    Sync_Cookie *cookie; /* cookie to add in control */
    PRThread *tid;       /* thread for persistent phase */
} SyncOpInfo;

