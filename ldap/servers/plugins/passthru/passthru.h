/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * passthru.h - Pass Through Authentication shared definitions
 *
 */

#ifndef _PASSTHRU_H_
#define _PASSTHRU_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "portable.h"
#include "slapi-plugin.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"
#include <nspr.h>

/* Private API: to get slapd_pr_strerror() and SLAPI_COMPONENT_NAME_NSPR */
#include "slapi-private.h"

/*
 * macros
 */
#define PASSTHRU_PLUGIN_SUBSYSTEM	"passthru-plugin"   /* for logging */

#define PASSTHRU_ASSERT( expr )		PR_ASSERT( expr )

#define PASSTHRU_LDAP_CONN_ERROR( err )	( (err) == LDAP_SERVER_DOWN || \
					     (err) == LDAP_CONNECT_ERROR )

#define PASSTHRU_OP_NOT_HANDLED		0
#define PASSTHRU_OP_HANDLED		1

#define PASSTHRU_CONN_TRIES		2

/* #define	PASSTHRU_VERBOSE_LOGGING	*/

/* defaults */
#define PASSTHRU_DEF_SRVR_MAXCONNECTIONS	3
#define PASSTHRU_DEF_SRVR_MAXCONCURRENCY	5
#define PASSTHRU_DEF_SRVR_TIMEOUT		300	/* seconds */
#define PASSTHRU_DEF_SRVR_PROTOCOL_VERSION	LDAP_VERSION3
#define PASSTHRU_DEF_SRVR_CONNLIFETIME		0	/* seconds */
#define PASSTHRU_DEF_SRVR_FAILOVERCONNLIFETIME	300	/* seconds */

/*
 * structs
 */
typedef struct passthrusuffix {
    int				ptsuffix_len;
    char			*ptsuffix_normsuffix; /* not case normalized */
    struct passthrusuffix	*ptsuffix_next;
} PassThruSuffix;

typedef struct passthruconnection {
    LDAP			*ptconn_ld;
    int				ptconn_ldapversion;
    int				ptconn_usecount;
#define PASSTHRU_CONNSTATUS_OK			0
#define PASSTHRU_CONNSTATUS_DOWN		1
#define PASSTHRU_CONNSTATUS_STALE		2
    int				ptconn_status;
    time_t			ptconn_opentime;
    struct passthruconnection	*ptconn_prev;
    struct passthruconnection	*ptconn_next;
} PassThruConnection;

typedef struct passthruserver {
    char			*ptsrvr_url;		/* copy from argv[i] */
    char			*ptsrvr_hostname;
    int				ptsrvr_port;
    int				ptsrvr_secure;		/* use SSL? */
    int				ptsrvr_ldapversion;
    int				ptsrvr_maxconnections;
    int				ptsrvr_maxconcurrency;
    int				ptsrvr_connlifetime;	/* in seconds */
    struct timeval		*ptsrvr_timeout;	/* for ldap_result() */
    PassThruSuffix		*ptsrvr_suffixes;
    Slapi_CondVar		*ptsrvr_connlist_cv;
    Slapi_Mutex			*ptsrvr_connlist_mutex;	/* protects connlist */
    int				ptsrvr_connlist_count;
    PassThruConnection		*ptsrvr_connlist;
    struct passthruserver	*ptsrvr_next;
} PassThruServer;

typedef struct passthruconfig {
    PassThruServer	*ptconfig_serverlist;
} PassThruConfig;


/*
 * public functions
 */
/*
 * ptbind.c:
 */
int passthru_simple_bind_s( Slapi_PBlock *pb, PassThruServer *srvr, int tries,
	char *dn, struct berval *creds, LDAPControl **reqctrls, int *lderrnop,
	char **matcheddnp, char **errmsgp, struct berval ***refurlsp,
	LDAPControl ***resctrlsp );

/*
 * ptconfig.c:
 */
int passthru_config( int argc, char **argv );
PassThruConfig *passthru_get_config( void );

/*
 * ptconn.c:
 */
int passthru_dn2server( PassThruConfig *cfg, char *normdn,
	PassThruServer **srvrp );
int passthru_get_connection( PassThruServer *srvr, LDAP **ldp );
void passthru_release_connection( PassThruServer *srvr, LDAP *ld, int dispose );
void passthru_close_all_connections( PassThruConfig *cfg );

/*
 * ptutil.c:
 */
struct berval **passthru_strs2bervals( char **ss );
char ** passthru_bervals2strs( struct berval **bvs );
void passthru_free_bervals( struct berval **bvs );
char *passthru_urlparse_err2string( int err );

#endif	/* _PASSTHRU_H_ */
