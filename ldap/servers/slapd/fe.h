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

#ifndef _SLAPD_FE_H_
#define _SLAPD_FE_H_

#include <prio.h>
#include "slap.h"

/*
 * Global Variables...
 */
#ifdef LDAP_DEBUG
#if defined( _WIN32 )
#ifndef DONT_DECLARE_SLAPD_LDAP_DEBUG
extern __declspec(dllimport) int slapd_ldap_debug;
#endif /* DONT_DECLARE_SLAPD_LDAP_DEBUG */
#endif
#endif
extern Slapi_Counter *ops_initiated;
extern Slapi_Counter *ops_completed;
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
extern char	*configfile;
#if defined( _WIN32 )
extern LPTSTR pszServerName;
#endif
#if defined( _WIN32 )
/* String constants (no change for international) */
extern HANDLE hSlapdEventSource;
extern SERVICE_STATUS LDAPServerStatus;
extern SERVICE_STATUS_HANDLE hLDAPServerServiceStatus;
#endif

/*
 * auth.c
 *
 */
void client_auth_init();
void handle_handshake_done (PRFileDesc *prfd, void* clientData);
int handle_bad_certificate (void* clientData, PRFileDesc *prfd);

/*
 * connection.c
 */
void op_thread_cleanup();

/*
 * ntuserpin.c - Prompts for the key database passphrase.
 */
#include "svrcore.h"
typedef struct SVRCORENTUserPinObj SVRCORENTUserPinObj;
SVRCOREError SVRCORE_CreateNTUserPinObj(SVRCORENTUserPinObj **out);
void SVRCORE_SetNTUserPinInteractive(SVRCORENTUserPinObj *obj, PRBool interactive);
void SVRCORE_DestroyNTUserPinObj(SVRCORENTUserPinObj *obj);

/*
 * connection.c
 */
void connection_abandon_operations( Connection *conn );
int connection_activity( Connection *conn );
void init_op_threads();
int connection_new_private(Connection *conn);
void connection_remove_operation( Connection *conn, Operation *op );
int connection_operations_pending( Connection *conn, Operation *op2ignore,
		int test_resultsent );
void connection_done(Connection *conn);
void connection_cleanup(Connection *conn);
void connection_reset(Connection* conn, int ns, PRNetAddr * from, int fromLen, int is_SSL);

/*
 * conntable.c
 */

/*
 * Note: the correct order to use when acquiring multiple locks is
 * c[i]->c_mutex followed by table_mutex.
 */
struct connection_table
{
	int size;
	/* An array of connections, file descriptors, and a mapping between them. */
	Connection *c;
	struct POLL_STRUCT *fd;
	int n_tcps;     /* standard socket start index in fd */
	int n_tcpe;     /* standard socket last ( +1 ) index in fd */
	int s_tcps;     /* ssl socket start index in fd */
	int s_tcpe;     /* ssl socket last ( +1 ) in fd */
#ifndef XP_WIN32
#if defined(ENABLE_LDAPI)
	int i_unixs;    /* unix socket start index in fd */
	int i_unixe;    /* unix socket last ( +1 ) in fd */
#endif /* ENABLE_LDAPI */
#endif
	PRLock *table_mutex;
};
typedef struct connection_table Connection_Table;

extern Connection_Table *the_connection_table; /* JCM - Exported from globals.c for daemon.c, monitor.c, puke, gag, etc */

Connection_Table *connection_table_new(int table_size);
void connection_table_free(Connection_Table *ct);
void connection_table_abandon_all_operations(Connection_Table *ct);
Connection *connection_table_get_connection(Connection_Table *ct, int sd);
void connection_table_move_connection_out_of_active_list(Connection_Table *ct, Connection *c);
void connection_table_move_connection_on_to_active_list(Connection_Table *ct, Connection *c);
void connection_table_as_entry(Connection_Table *ct, Slapi_Entry *e);
void connection_table_dump_activity_to_errors_log(Connection_Table *ct);
Connection* connection_table_get_first_active_connection (Connection_Table *ct);
Connection* connection_table_get_next_active_connection (Connection_Table *ct, Connection *c);
typedef int (*Connection_Table_Iterate_Function)(Connection *c, void *arg);
int connection_table_iterate_active_connections(Connection_Table *ct, void* arg, Connection_Table_Iterate_Function f);
#if defined( _WIN32 )
Connection* connection_table_get_connection_from_fd(Connection_Table *ct,PRFileDesc *prfd);
#endif
#if 0
void connection_table_dump(Connection_Table *ct);
#endif

/*
 * daemon.c
 */
int signal_listner();
int daemon_pre_setuid_init(daemon_ports_t *ports);
void slapd_daemon( daemon_ports_t *ports );
void daemon_register_connection();
int slapd_listenhost2addr( const char *listenhost, PRNetAddr ***addr );
int daemon_register_reslimits( void );
int secure_read_function( int ignore , void *buffer, int count, struct lextiof_socket_private *handle );
int secure_write_function( int ignore, const void *buffer, int count, struct lextiof_socket_private *handle );
int read_function(int ignore, void *buffer,  int count, struct lextiof_socket_private *handle );
int write_function(int ignore, const void *buffer,  int count, struct lextiof_socket_private *handle );
PRFileDesc * get_ssl_listener_fd();
int configure_pr_socket( PRFileDesc **pr_socket, int secure, int local );
void configure_ns_socket( int * ns );

/*
 * sasl_io.c
 */
int sasl_io_enable(Connection *c);

/*
 * sasl_map.c
 */
int sasl_map_config_add(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int sasl_map_config_delete(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int sasl_map_domap(char *sasl_user, char *sasl_realm, char **ldap_search_base, char **ldap_search_filter);
int sasl_map_init();
int sasl_map_done();

#endif
