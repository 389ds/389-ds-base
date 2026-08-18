/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef _REPL5_PROT_PRIVATE_H_
#define _REPL5_PROT_PRIVATE_H_

#define ACQUIRE_SUCCESS 101
#define ACQUIRE_REPLICA_BUSY 102
#define ACQUIRE_FATAL_ERROR 103
#define ACQUIRE_CONSUMER_WAS_UPTODATE 104
#define ACQUIRE_TRANSIENT_ERROR 105

typedef struct private_repl_protocol
{
    void (*delete)(struct private_repl_protocol **);
    void (*run)(struct private_repl_protocol *);
    int (*stop)(struct private_repl_protocol *);
    int (*status)(struct private_repl_protocol *);
    void (*notify_update)(struct private_repl_protocol *);
    void (*notify_agmt_changed)(struct private_repl_protocol *);
    void (*notify_window_opened)(struct private_repl_protocol *);
    void (*notify_window_closed)(struct private_repl_protocol *);
    void (*update_now)(struct private_repl_protocol *);
    int stopped;
    int terminate;
    PRUint32 eventbits;
    Repl_Connection *conn;
    int last_acquire_response_code;
    Repl_Agmt *agmt;
    Replica *replica;
    void *private;
    PRBool replica_acquired;
    int repl50consumer; /* Flag to tell us if this is a 5.0-style consumer we're talking to */
    int repl71consumer; /* Flag to tell us if this is a 7.1-style consumer we're talking to */
    int repl90consumer; /* Flag to tell us if this is a 9.0-style consumer we're talking to */
    pthread_mutex_t lock;
    pthread_cond_t cvar;
} Private_Repl_Protocol;

extern Private_Repl_Protocol *Repl_5_Inc_Protocol_new(Repl_Protocol *rp);
extern Private_Repl_Protocol *Repl_5_Tot_Protocol_new(Repl_Protocol *rp);
extern int repl5_tot_last_rcv_msgid(Repl_Connection *conn);
extern int repl5_tot_flowcontrol_detection(Repl_Connection *conn, int increment);
extern Private_Repl_Protocol *Windows_Inc_Protocol_new(Repl_Protocol *rp);
extern Private_Repl_Protocol *Windows_Tot_Protocol_new(Repl_Protocol *rp);

#define PROTOCOL_TERMINATION_NORMAL 301
#define PROTOCOL_TERMINATION_ABNORMAL 302
#define PROTOCOL_TERMINATION_NEEDS_TOTAL_UPDATE 303

#define RESUME_DO_TOTAL_UPDATE 401
#define RESUME_DO_INCREMENTAL_UPDATE 402
#define RESUME_TERMINATE 403
#define RESUME_SUSPEND 404

/* Backoff timer settings for reconnect */
#define PROTOCOL_BACKOFF_MINIMUM 3        /* 3 seconds */
#define PROTOCOL_BACKOFF_MAXIMUM (60 * 5) /* 5 minutes */
/* Backoff timer settings for replica busy reconnect */
#define PROTOCOL_BUSY_BACKOFF_MINIMUM PROTOCOL_BACKOFF_MINIMUM
#define PROTOCOL_BUSY_BACKOFF_MAXIMUM PROTOCOL_BUSY_BACKOFF_MINIMUM

/* protocol related functions */
void release_replica(Private_Repl_Protocol *prp);
int acquire_replica(Private_Repl_Protocol *prp, char *prot_oid, RUV **ruv);
BerElement *entry2bere(const Slapi_Entry *e, char **excluded_attrs);
CSN *get_current_csn(Slapi_DN *replarea_sdn);
char *protocol_response2string(int response);
int repl5_strip_fractional_mods(Repl_Agmt *agmt, LDAPMod **);
void windows_release_replica(Private_Repl_Protocol *prp);
int windows_acquire_replica(Private_Repl_Protocol *prp, RUV **ruv, int check_ruv);
void repl5_set_backoff_min(Private_Repl_Protocol *prp, int min);
void repl5_set_backoff_max(Private_Repl_Protocol *prp, int max);
int repl5_get_backoff_min(Private_Repl_Protocol *prp);
int repl5_get_backoff_max(Private_Repl_Protocol *prp);

#endif /* _REPL5_PROT_PRIVATE_H_ */
