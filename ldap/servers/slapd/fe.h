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

#ifndef _SLAPD_FE_H_
#define _SLAPD_FE_H_

#include <prio.h>
#include "slap.h"

/*
 * Global Variables...
 */
extern Slapi_Counter *ops_initiated;
extern Slapi_Counter *ops_completed;
extern Slapi_Counter *max_threads_count;
extern Slapi_Counter *conns_in_maxthreads;
extern PRThread *listener_tid;
extern PRThread *listener_tid;
extern Slapi_Counter *num_conns;
extern char *pid_file;
extern char *start_pid_file;
extern int should_detach;
extern int connection_type; /* JCM - Evil. Solve by creating a real connection constructor & destructor */
#ifndef HAVE_TIME_R
extern PRLock *time_func_mutex;
#endif /* HAVE_TIME_R */
extern PRLock *currenttime_mutex;
extern time_t starttime;
extern char *configfile;

/*
 * auth.c
 *
 */
void client_auth_init(void);
void handle_handshake_done(PRFileDesc *prfd, void *clientData);
int handle_bad_certificate(void *clientData, PRFileDesc *prfd);

/*
 * connection.c
 */
void op_thread_cleanup(void);
/* do this after all worker threads have terminated */
void connection_post_shutdown_cleanup(void);

/*
 * connection.c
 */
void connection_abandon_operations(Connection *conn);
int connection_activity(Connection *conn, int maxthreads);
void init_op_threads(int32_t threadnumber);
int connection_new_private(Connection *conn);
void connection_remove_operation(Connection *conn, Operation *op);
void connection_remove_operation_ext(Slapi_PBlock *pb, Connection *conn, Operation *op);
int connection_operations_pending(Connection *conn, Operation *op2ignore, int test_resultsent);
void connection_done(Connection *conn);
void connection_cleanup(Connection *conn);
void connection_reset(Connection *conn, int ns, PRNetAddr *from, int fromLen, int is_SSL);
void connection_set_io_layer_cb(Connection *c, Conn_IO_Layer_cb push_cb, Conn_IO_Layer_cb pop_cb, void *cb_data);
int connection_call_io_layer_callbacks(Connection *c);
size_t connection_read_buffer_data_len(Connection *c);
void connection_reset_read_buffer(Connection *c);

/*
 * conntable.c
 */

#ifdef ENABLE_EPOLL
/*
 * Connections of a connection table list that were marked closing and can
 * be moved out of its active list, waiting for the list thread.  Chained
 * through c_reap_next, a connection is on at most one queue (c_reap_queued).
 * The queue lock is taken under c_mutex, never with table_mutex.
 */
typedef struct epoll_reap_queue
{
    pthread_mutex_t lock;
    Connection *head;
    int32_t fd; /* eventfd in the list's epoll set, written when head is set */
} epoll_reap_queue;
#endif /* ENABLE_EPOLL */

/*
 * Note: the correct order to use when acquiring multiple locks is
 * c[i]->c_mutex followed by table_mutex.
 */
struct connection_table
{
    int size;
    int list_size;   /* Size of each connection table list. */
    int list_num;    /* Number of connection table lists. */
    int *num_active; /* list_num size buffer to track the number of connections on each connection table list. */
    /* An array of connections, file descriptors, and a mapping between them. */
    Connection **c;
    /* An array of free connections awaiting allocation. */;
    Connection **c_freelist;
    size_t conn_next_offset;
    struct POLL_STRUCT **fd;
#ifdef ENABLE_EPOLL
    int *epoll_fd;  /* epoll file descriptor for each connection table list */
    int32_t epoll_active; /* active connections on all lists, atomic, for the turbo mode cut-off */
    epoll_reap_queue *epoll_reap; /* closing queue for each connection table list */
#endif /* ENABLE_EPOLL */
    PRLock *table_mutex;
};
typedef struct connection_table Connection_Table;

typedef struct signal_pipe
{
    PRFileDesc *signalpipe[2];
    int readsignalpipe;
    int writesignalpipe;
} signal_pipe;

extern Connection_Table *the_connection_table; /* JCM - Exported from globals.c for daemon.c, monitor.c, puke, gag, etc */

Connection_Table *connection_table_new(int table_size);
void connection_table_free(Connection_Table *ct);
void connection_table_abandon_all_operations(Connection_Table *ct);
void connection_table_disconnect_all(Connection_Table *ct);
Connection *connection_table_get_connection(Connection_Table *ct, int sd);
int connection_table_move_connection_out_of_active_list(Connection_Table *ct, Connection *c);
#ifdef ENABLE_EPOLL
size_t connection_table_move_connections_out_of_active_list(Connection_Table *ct, Connection **conns, size_t count);
#endif
void connection_table_move_connection_on_to_active_list(Connection_Table *ct, Connection *c);
void connection_table_as_entry(Connection_Table *ct, Slapi_Entry *e);
void connection_table_dump_activity_to_errors_log(Connection_Table *ct);
Connection *connection_table_get_first_active_connection(Connection_Table *ct, size_t listnum);
Connection *connection_table_get_next_active_connection(Connection_Table *ct, Connection *c);
typedef int (*Connection_Table_Iterate_Function)(Connection *c, void *arg);
int connection_table_iterate_active_connections(Connection_Table *ct, void *arg, Connection_Table_Iterate_Function f);
int connection_table_get_list(Connection_Table *ct);
#ifdef ENABLE_EPOLL
int connection_table_active_count(Connection_Table *ct);
#endif /* ENABLE_EPOLL */

/*
 * daemon.c
 */
int signal_listner(int listnum);
#ifdef ENABLE_EPOLL
void connection_epoll_rearm_nolock(Connection *c);
int connection_epoll_rearm_prepare_nolock(Connection *c, int *epfd, int *fd);
void connection_epoll_rearm_unlocked(Connection *c, int epfd, int fd);
void connection_epoll_reap_nolock(Connection *c);
#endif /* ENABLE_EPOLL */
int daemon_pre_setuid_init(daemon_ports_t *ports);
void slapd_sockets_ports_free(daemon_ports_t *ports_info);
void slapd_daemon(daemon_ports_t *ports);
void daemon_register_connection(void);
int slapd_listenhost2addr(const char *listenhost, PRNetAddr ***addr);
int daemon_register_reslimits(void);
PRFileDesc *get_ssl_listener_fd(void);
int configure_pr_socket(PRFileDesc **pr_socket, int secure, int local);

/*
 * sasl_io.c
 */
int sasl_io_enable(Connection *c, void *data);
int sasl_io_cleanup(Connection *c, void *data);

/*
 * sasl_map.c
 */
typedef struct sasl_map_data_ sasl_map_data;
struct sasl_map_data_
{
    char *name;
    char *regular_expression;
    char *template_base_dn;
    char *template_search_filter;
    int priority;
    sasl_map_data *next; /* For linked list */
    sasl_map_data *prev;
};

typedef struct _sasl_map_private
{
    Slapi_RWLock *lock;
    sasl_map_data *map_data_list;
} sasl_map_private;

int sasl_map_config_add(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int sasl_map_config_delete(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int sasl_map_config_modify(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
int sasl_map_domap(sasl_map_data **map, char *sasl_user, char *sasl_realm, char **ldap_search_base, char **ldap_search_filter);
int sasl_map_init(void);
int sasl_map_done(void);
void sasl_map_read_lock(void);
void sasl_map_read_unlock(void);

/*
 * operation.c
 */
void fgot_set(struct op *op, fgot_id_t fgot_id, struct timespec *t);
void fgot_compute(struct op *op, fgot_id_t fgot_id, struct timespec *t1, struct timespec *t2);
void fgot_start(struct op *op, fgot_id_t fgot_id);
void fgot_end(struct op *op, fgot_id_t fgot_id);

#endif
