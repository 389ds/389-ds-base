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

#ifndef CBHFILE
#define CBHFILE

/*** #define CB_YIELD ***/

#include <stdio.h>
#include <string.h>
#include <prlock.h>
#include <prcvar.h>
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "portable.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"

/* Constants */

#define CB_DIRECTORY_MANAGER_DN		"cn=directory manager"
#define CB_CHAINING_BACKEND_TYPE  	"chaining database"
#define CB_PLUGIN_NAME			"chaining database"
#define CB_PLUGIN_SUBSYSTEM		"chaining database"
#define CB_PLUGIN_DESCRIPTION		"LDAP chaining backend database plugin"

#define CB_LDAP_SECURE_PORT		636
#define CB_BUFSIZE			2048


/* Macros */

#define CB_LDAP_CONN_ERROR( err ) ( (err) == LDAP_SERVER_DOWN || \
                                    (err) == LDAP_CONNECT_ERROR )
#define CB_ASSERT( expr )         PR_ASSERT( expr )

/* Innosoft chaining extension for loop detection */

#define CB_LDAP_CONTROL_CHAIN_SERVER	"1.3.6.1.4.1.1466.29539.12"

/* Chaining backend configuration attributes */

/* Monitor entry */
#define CB_MONITOR_EXTENSIBLEOCL		"extensibleObject"
#define CB_MONITOR_INSTNAME			"cn"
#define CB_MONITOR_ADDCOUNT			"nsAddCount"
#define CB_MONITOR_DELETECOUNT			"nsDeleteCount"
#define CB_MONITOR_MODIFYCOUNT			"nsModifyCount"
#define CB_MONITOR_MODRDNCOUNT			"nsRenameCount"
#define CB_MONITOR_SEARCHBASECOUNT		"nsSearchBaseCount"
#define CB_MONITOR_SEARCHONELEVELCOUNT		"nsSearchOneLevelCount"
#define CB_MONITOR_SEARCHSUBTREECOUNT		"nsSearchSubtreeCount"
#define CB_MONITOR_ABANDONCOUNT			"nsAbandonCount"
#define CB_MONITOR_BINDCOUNT			"nsBindCount"
#define CB_MONITOR_UNBINDCOUNT			"nsUnbindCount"
#define CB_MONITOR_COMPARECOUNT			"nsCompareCount"
#define CB_MONITOR_OUTGOINGCONN			"nsOpenOpConnectionCount"
#define CB_MONITOR_OUTGOINGBINDCOUNT		"nsOpenBindConnectionCount"

/* Global configuration */
#define CB_CONFIG_GLOBAL_FORWARD_CTRLS		"nsTransmittedControls"
#define CB_CONFIG_GLOBAL_CHAINING_COMPONENTS	"nsActiveChainingComponents"	
#define CB_CONFIG_GLOBAL_CHAINABLE_COMPONENTS	"nsPossibleChainingComponents"
/* not documented */
#define CB_CONFIG_GLOBAL_DEBUG			"nsDebug"


/* Instance-specific configuration */
#define CB_CONFIG_CHAINING_COMPONENTS		CB_CONFIG_GLOBAL_CHAINING_COMPONENTS
#define CB_CONFIG_EXTENSIBLEOCL			"extensibleObject"
/* XXXSD to be changed */
#define CB_CONFIG_INSTANCE_FILTER		"(objectclass=nsBackendInstance)"
#define CB_CONFIG_INSTNAME			"cn"
#define CB_CONFIG_SUFFIX			"nsslapd-suffix"
#define CB_CONFIG_SIZELIMIT			"nsslapd-sizelimit"
#define CB_CONFIG_TIMELIMIT			"nsslapd-timelimit"
#define CB_CONFIG_HOSTURL			"nsFarmServerURL"

#define CB_CONFIG_BINDUSER			"nsMultiplexorBindDn"	
#define CB_CONFIG_USERPASSWORD			"nsMultiplexorCredentials"	
#define CB_CONFIG_MAXBINDCONNECTIONS		"nsBindConnectionsLimit"
#define CB_CONFIG_MAXCONNECTIONS		"nsOperationConnectionsLimit"
#define CB_CONFIG_MAXCONCURRENCY		"nsConcurrentOperationsLimit"
#define CB_CONFIG_MAXBINDCONCURRENCY		"nsConcurrentBindLimit"

#define CB_CONFIG_IMPERSONATION			"nsProxiedAuthorization"

#define CB_CONFIG_BINDTIMEOUT			"nsBindTimeout"
#define CB_CONFIG_TIMEOUT			"nsOperationTimeout"
#define CB_CONFIG_MAX_IDLE_TIME			"nsMaxResponseDelay"
#define CB_CONFIG_MAX_TEST_TIME			"nsMaxTestResponseDelay"

#define CB_CONFIG_REFERRAL			"nsReferralOnScopedSearch"

#define CB_CONFIG_CONNLIFETIME			"nsConnectionLife"
#define CB_CONFIG_ABANDONTIMEOUT		"nsAbandonedSearchCheckInterval "
#define CB_CONFIG_BINDRETRY			"nsBindRetryLimit"
#define CB_CONFIG_LOCALACL			"nsCheckLocalACI"
#define CB_CONFIG_HOPLIMIT			"nsHopLimit"

/* not documented */
#define CB_CONFIG_ILLEGAL_ATTRS			"nsServerDefinedAttributes"

/* Default configuration values (as string) */

/*
 * CB_DEF_MAXCONNECTIONS and CB_DEF_MAXCONCURRENCY used to be 10.
 * Reduced CB_DEF_MAXCONCURRENCY to 2 to workaround bug 623793 -
 * err=1 in accesslogs and ber parsing errors in errors logs.
 */
#define CB_DEF_MAXCONNECTIONS			"20" 	/* CB_CONFIG_MAXCONNECTIONS */
#define CB_DEF_MAXCONCURRENCY			"2"	/* CB_CONFIG_MAXCONCURRENCY */
#define CB_DEF_BIND_MAXCONNECTIONS		"3"	/* CB_CONFIG_MAXBINDCONNECTIONS */
#define CB_DEF_BIND_MAXCONCURRENCY		"10"	/* CB_CONFIG_MAXBINDCONCURRENCY */
#define CB_DEF_BINDTIMEOUT			"15"	/* CB_CONFIG_BINDTIMEOUT */
#define CB_DEF_CONNLIFETIME			"0"	/* CB_CONFIG_CONNLIFETIME */
#define CB_DEF_IMPERSONATION			"on"	/* CB_CONFIG_IMPERSONATION */
#define CB_DEF_SEARCHREFERRAL			"off"	/* CB_CONFIG_REFERRAL */
#define CB_DEF_ABANDON_TIMEOUT			"1"	/* CB_CONFIG_ABANDONTIMEOUT */
#define CB_DEF_BINDRETRY			"3"	/* CB_CONFIG_BINDRETRY */
#define CB_DEF_LOCALACL				"off"	/* CB_CONFIG_LOCALACL */
#define CB_DEF_TIMELIMIT			"3600"
#define CB_DEF_SIZELIMIT			"2000"
#define CB_DEF_HOPLIMIT				"10"	/* CB_CONFIG_HOPLIMIT */
#define CB_DEF_MAX_IDLE_TIME			"60"	/* CB_CONFIG_MAX_IDLE_TIME */
#define CB_DEF_MAX_TEST_TIME			"15"	/* CB_CONFIG_MAX_TEST_TIME */

typedef void *cb_config_get_fn_t(void *arg);
typedef int cb_config_set_fn_t(void *arg, void *value, char *errorbuf, int phase, int apply);
typedef struct _cb_instance_config_info {
        char *config_name;
        int config_type;
        char *config_default_value;
        cb_config_get_fn_t *config_get_fn;
        cb_config_set_fn_t *config_set_fn;
        int config_flags;
} cb_instance_config_info;
 
#define CB_CONFIG_TYPE_ONOFF 1     /* val = (int) value */
#define CB_CONFIG_TYPE_STRING 2    /* val = (char *) value - The get functions
                                 * for this type must return alloced memory
                                 * that should be freed by the caller. */
#define CB_CONFIG_TYPE_INT 3       /* val = (int) value */
#define CB_CONFIG_TYPE_LONG 4      /* val = (long) value */
#define CB_CONFIG_TYPE_INT_OCTAL 5 /* Same as CONFIG_TYPE_INT, but shown in octal*/
#define CB_PREVIOUSLY_SET 1
#define CB_ALWAYS_SHOW 2
#define CB_CONFIG_PHASE_INITIALIZATION 1
#define CB_CONFIG_PHASE_STARTUP 2
#define CB_CONFIG_PHASE_RUNNING 3
#define CB_CONFIG_PHASE_INTERNAL 4

/*jarnou: default amount of time in seconds during wich the chaining backend will be unavailable */
#define CB_UNAVAILABLE_PERIOD			30 /* CB_CONFIG_UNAVAILABLE_PERIOD */
#define CB_INFINITE_TIME                        360000 /* must be enough ... */
/*jarnou: default number of connections failed from which the farm is declared unavailable  */
#define CB_NUM_CONN_BEFORE_UNAVAILABILITY	1 
#define FARMSERVER_UNAVAILABLE			1
#define FARMSERVER_AVAILABLE			0

/* Internal data structures */

/* cb_backend represents the chaining backend type. */
/* Only one instance is created when the plugin is  */
/* loaded. Contain global conf			    */
typedef struct _cb_backend {

	/*
	** keep track of plugin identity.
	** Used for internal operations
	*/

	void 		*identity;
	char *		pluginDN;
	char *		configDN;

 	/*
	** There are times when we need a pointer to the chaining database
        ** plugin, so we will store a pointer to it here.  Examples of
        ** when we need it are when we create a new instance and when
        ** we need the name of the plugin to do internal ops.
	*/
	
        struct slapdplugin      *plugin;

	/*
	** Global config. shared by all chaining db instances 
	*/

	struct {
		char ** forward_ctrls;		/* List of forwardable controls    */
		char ** chaining_components;	/* List of plugins that chains	   */
		char ** chainable_components;	/* List of plugins allowed to chain*/
						/* internal operations.            */
		PRRWLock *rwl_config_lock;	/* Protect the global config	   */
	} config;

	int started;				/* TRUE when started		   */
	
} cb_backend;


/* Connection management */

/* states */
#define CB_CONNSTATUS_OK		1	/* Open */
#define CB_CONNSTATUS_DOWN		2	/* Down */
#define CB_CONNSTATUS_STALE		3	

#define ENABLE_MULTITHREAD_PER_CONN 1	/* to allow multiple threads to perform LDAP operations on a connection */
#define DISABLE_MULTITHREAD_PER_CONN 0	/* to allow only one thread to perform LDAP operations on a connection */

/**************  WARNING: Be careful if you want to change this constant. It is used in hexadecimal in cb_conn_stateless.c in the function PR_ThreadSelf() ************/
#define MAX_CONN_ARRAY 2048 /* we suppose the number of threads in the server not to exceed this limit*/
/**********************************************************************************************************/
typedef struct _cb_outgoing_conn{
	LDAP 				*ld;
	unsigned long			refcount;
	struct _cb_outgoing_conn 	*next;
	time_t				opentime;
	int 				status;
        int                             ThreadId ; /* usefull to identify the thread when SSL is enabled */
} cb_outgoing_conn;

typedef struct  {
	char 		*hostname;	/* Farm server name */
	char 		*url;
	unsigned int 	port;
	int 		secure;	
	char 		*binddn;	/* normalized */
	char 		*binddn2;	/* not normalized, value returned to the client */
	char 		*password;
	int 		bindit;		/* If true, open AND bind */
	char 		** waste_basket; /* stale char *   */

	struct {
		unsigned int 		maxconnections;
		unsigned int 		maxconcurrency;
		unsigned int 		connlifetime;
		struct timeval 		op_timeout;
		struct timeval 		bind_timeout;

		Slapi_Mutex		*conn_list_mutex;
		Slapi_CondVar		*conn_list_cv;
		cb_outgoing_conn 	*conn_list;
		unsigned int 		conn_list_count;

	} conn;

	cb_outgoing_conn  *connarray[MAX_CONN_ARRAY]; /* array of secure connections */

	/* To protect the config set by LDAP */
	PRRWLock	* rwl_config_lock;
} cb_conn_pool;


/* _cb_backend_instance represents a instance of the chaining */
/* backend.						      */

typedef struct _cb_backend_instance {
	
	char 			*inst_name;		/* Unique name */
	Slapi_Backend		*inst_be;		/* Slapi_Bakedn associated with it */
	cb_backend		*backend_type;		/* pointer to the backend type */

	/* configuration */

	PRRWLock		*rwl_config_lock;	/* protect the config */
	char 			*configDn;		/* config entry dn */
	char 			*monitorDn;		/* monitor entry dn */
	int 		  	local_acl;		/* True if local acl evaluation */
	/* sometimes a chaining backend may be associated with a local backend
	   1) The chaining backend is the backend of a sub suffix, and the
	      parent suffix has a local backend
	   2) Entry distribution is being used to distribute write operations to
          a chaining backend and other operations to a local backend
		  (e.g. a replication hub or consumer)
	   If the associated local backend is being initialized (import), it will be
	   disabled, and it will be impossible to evaluate local acls.  In this case,
	   we still want to be able to chain operations to a farm server or another
	   database chain.  But the current code will not allow cascading without
	   local acl evaluation (cb_controls.c).  The following variable allows us to relax that
	   restriction while the associated backend is disabled
	*/
	int             associated_be_is_disabled; /* true if associated backend is disabled */
	int 		  	isconfigured;		/* True when valid config entry */
	int 			impersonate;		/* TRUE to impersonate users */
	int 			searchreferral;		/* TRUE to return referral for scoped searches */
	int 			bind_retry;
	struct timeval		abandon_timeout;	/* check for abandoned op periodically */
	struct timeval  	op_timeout;
	char 			**url_array;		/* list of urls to farm servers */
        char 			**chaining_components;  /* List of plugins using chaining  */
	char 			**illegal_attributes;	/* Attributes not forwarded */
	char 			**every_attribute;	/* attr list to get every attr, including op attrs */
	int			sizelimit;
	int			timelimit;
	int			hoplimit;
	int 			max_idle_time;		/* how long we wait before pinging the farm server */
	int 			max_test_time;		/* how long we wait during ping */

	cb_conn_pool 		*pool;			/* Operation cnx pool */
	cb_conn_pool 		*bind_pool;		/* Bind cnx pool */

	Slapi_Eq_Context	eq_ctx;			/* Use to identify the function put in the queue */

	/* Monitoring */

	struct {
		Slapi_Mutex		*mutex;
		unsigned 		long addcount;
		unsigned 		long deletecount;
		unsigned 		long modifycount;
		unsigned 		long modrdncount;
		unsigned 		long searchbasecount;
		unsigned 		long searchonelevelcount;
		unsigned 		long searchsubtreecount;
		unsigned 		long abandoncount;
		unsigned 		long bindcount;
		unsigned 		long unbindcount;
		unsigned 		long comparecount;
	} monitor;

	/* Monitoring the chaining BE availability */
	/* Principle: as soon as we detect an abnormal pb with an ldap operation, and we close the connection
	or if we can't open a connection, we increment a counter (cpt). This counter represents the number of
	continuously pbs we can notice. Before forwarding an LDAP operation, wether the farmserver is available or not,
	through the value of the counter. If the farmserver is not available, we just return an error msg to the client */

	struct {
		int		unavailable_period  ;		/* how long we wait as soon as the farm is declared unavailable */
		int		max_num_conn_failed ;	/* max number of consecutive failed/aborted connections before we declared the farm as unreachable */
		time_t          unavailableTimeLimit ;		/* time from which the chaining BE becomes available */
		int		farmserver_state ;			/* FARMSERVER_AVAILABLE if the chaining is available, FARMSERVER_UNAVAILABLE else */
		int		cpt ;						/* count the number of consecutive failed/aborted connexions */
		Slapi_Mutex	*cpt_lock ;					/* lock to protect the counter cpt */
	        Slapi_Mutex     *lock_timeLimit ;               /* lock to protect the unavailableTimeLimit variable*/
	} monitor_availability;

		
} cb_backend_instance;

/* Data structure for the search operation to carry candidates */

#define CB_SEARCHCONTEXT_ENTRY 	2

typedef struct _cb_searchContext {
        int 		type;
	void 		*data;
	int		msgid;
	LDAP 		*ld;
	cb_outgoing_conn	*cnx;
	Slapi_Entry	*tobefreed;
	LDAPMessage	*pending_result;
	int 		pending_result_type;
} cb_searchContext;

#define CB_REOPEN_CONN		-1968	/* Different from any LDAP_XXX errors */

/* Forward declarations */

/* for ctrl_flags on cb_update_controls */
#define CB_UPDATE_CONTROLS_ADDAUTH   1
#define CB_UPDATE_CONTROLS_ISABANDON 2


int cb_get_connection(cb_conn_pool * pool, LDAP ** ld, cb_outgoing_conn ** cnx, struct timeval * tmax, char **errmsg);
int cb_config(cb_backend_instance * cb, int argc, char ** argv );
int cb_update_controls( Slapi_PBlock *pb, LDAP * ld, LDAPControl *** controls, int ctrl_flags);
int cb_is_control_forwardable(cb_backend * cb, char *controloid);
int cb_access_allowed (Slapi_PBlock *pb,Slapi_Entry *e,char *type,struct berval * bval, int op, char ** buf);
int cb_forward_operation(Slapi_PBlock * op);
int cb_parse_instance_config_entry(cb_backend * cb, Slapi_Entry * e);
int cb_abandon_connection(cb_backend_instance * cb, Slapi_PBlock * pb, LDAP ** ld);
int cb_atoi(char *str);
int cb_check_forward_abandon(cb_backend_instance * cb,Slapi_PBlock * pb, LDAP * ld, int msgid );
int cb_search_monitor_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *e2, int *ret, char *t,void *a);
int cb_config_load_dse_info(Slapi_PBlock * pb);
int cb_config_add_dse_entries(cb_backend *cb, char **entries, char *string1, char *string2, char *string3);
int cb_add_suffix(cb_backend_instance *inst, struct berval **bvals, int apply_mod, char *returntext);
int cb_create_default_backend_instance_config(cb_backend * cb);
int cb_build_backend_instance_config(cb_backend_instance *inst, Slapi_Entry * conf,int apply);
int cb_instance_delete_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg);
int cb_instance_search_config_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg);
int cb_instance_add_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg);
int cb_instance_modify_config_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg);
int cb_dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg);
int cb_config_search_callback(Slapi_PBlock *pb, Slapi_Entry* e1, Slapi_Entry* e2, int *returncode,
        char *returntext, void *arg);
int cb_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int cb_config_delete_instance_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int cb_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int cb_config_add_instance_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
int cb_delete_monitor_callback(Slapi_PBlock * pb, Slapi_Entry * e, Slapi_Entry * entryAfter, int * returnCode, char * returnText, void * arg);
int cb_config_add_check_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2, int *returncode,
        char *returntext, void *arg);
int cb_instance_add_config_check_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg);
int cb_config_modify_check_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode,
        char *returntext, void *arg);

void cb_eliminate_illegal_attributes(cb_backend_instance * inst, Slapi_Entry * e);
void cb_release_op_connection(cb_conn_pool * pool, LDAP *ldd, int dispose);
void cb_register_supported_control( cb_backend * cb, char *controloid, unsigned long controlops );
void cb_unregister_all_supported_control( cb_backend * cb );
void cb_register_supported_control( cb_backend * cb, char *controloid, unsigned long controlops );
void cb_unregister_supported_control( cb_backend * cb, char *controloid, unsigned long controlops );
void cb_set_acl_policy(Slapi_PBlock *pb);
void cb_close_conn_pool(cb_conn_pool * pool);
void cb_update_monitor_info(Slapi_PBlock * pb, cb_backend_instance * inst,int op);
void cb_send_ldap_result(Slapi_PBlock *pb, int err, char *m,char *t, int ne, struct berval **urls );
void cb_stale_all_connections( cb_backend_instance * be);
int
cb_config_add_instance_check_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
 int *returncode, char *returntext, void *arg);


int chaining_back_add 	( Slapi_PBlock *pb );
int chaining_back_delete ( Slapi_PBlock *pb );
int chaining_back_compare ( Slapi_PBlock *pb );
int chaining_back_modify ( Slapi_PBlock *pb );
int chaining_back_modrdn ( Slapi_PBlock *pb );
int chaining_back_abandon ( Slapi_PBlock *pb );
int chaining_back_entry_release ( Slapi_PBlock *pb );
int chainingdb_next_search_entry( Slapi_PBlock *pb );
int chainingdb_build_candidate_list ( Slapi_PBlock *pb );
int chainingdb_start (Slapi_PBlock *pb );
int chainingdb_bind (Slapi_PBlock *pb );
int cb_db_size (Slapi_PBlock *pb );
int cb_back_close (Slapi_PBlock *pb );
int cb_back_cleanup (Slapi_PBlock *pb );

long cb_atol(char *str);

Slapi_Entry * cb_LDAPMessage2Entry(LDAP * ctx, LDAPMessage * msg, int attrsonly);
char * cb_urlparse_err2string( int err );
char * cb_get_rootdn();
struct berval ** referrals2berval(char ** referrals);
cb_backend_instance * cb_get_instance(Slapi_Backend * be);
cb_backend * cb_get_backend_type();
int cb_debug_on();
void cb_set_debug(int on);
int cb_ping_farm(cb_backend_instance *cb,cb_outgoing_conn * cnx,time_t end);
void cb_update_failed_conn_cpt ( cb_backend_instance *cb ) ;
void cb_reset_conn_cpt( cb_backend_instance *cb ) ;
int  cb_check_availability( cb_backend_instance *cb, Slapi_PBlock *pb ) ;

time_t current_time();
char* get_localhost_DNS();

/* this function is called when state of a backend changes */
void cb_be_state_change (void *handle, char *be_name, int old_be_state, int new_be_state);

#endif
