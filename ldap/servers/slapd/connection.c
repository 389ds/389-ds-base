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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <stdlib.h>
#endif
#include <signal.h>
#include "slap.h"
#include "prcvar.h"
#include "prlog.h" /* for PR_ASSERT */
#include "fe.h"
#include <sasl.h>
#if defined(LINUX)
#include <netinet/tcp.h> /* for TCP_CORK */
#endif


static void connection_threadmain( void );
static void add_pb( Slapi_PBlock * );
static Slapi_PBlock *get_pb( void );
static void connection_add_operation(Connection* conn, Operation *op);
static void connection_free_private_buffer(Connection *conn);
static void op_copy_identity(Connection *conn, Operation *op);
static void connection_set_ssl_ssf(Connection *conn);
static int is_ber_too_big(const Connection *conn, ber_len_t ber_len);
static void log_ber_too_big_error(const Connection *conn,
				ber_len_t ber_len, ber_len_t maxbersize);

/*
 * We maintain a global work queue of Slapi_PBlock's that have not yet
 * been handed off to an operation thread.
 */
struct Slapi_PBlock_q
{
	Slapi_PBlock *pb;
	struct Slapi_PBlock_q *next_pb;
	int pb_fd;
};

static struct Slapi_PBlock_q *first_pb= NULL;	/* global work queue head */
static struct Slapi_PBlock_q *last_pb= NULL;	/* global work queue tail */
static PRLock *pb_q_lock=NULL;			/* protects first_pb & last_pb */

static PRCondVar *op_thread_cv;	/* used by operation threads to wait for work */
static PRLock *op_thread_lock;	/* associated with op_thread_cv */
static int op_shutdown= 0;		/* if non-zero, server is shutting down */

#define LDAP_SOCKET_IO_BUFFER_SIZE 512 /* Size of the buffer we give to the I/O system for reads */


/*
 * We really are done with this connection. Get rid of everything.
 *
 * Note: this function should be called with conn->c_mutex already locked
 * or at a time when multiple threads are not in play that might touch the
 * connection structure.
 */
void
connection_done(Connection *conn)
{
	connection_cleanup(conn);
	/* free the private content, the buffer has been freed by above connection_cleanup */
	slapi_ch_free((void**)&conn->c_private);
	if (NULL != conn->c_sb)
	{
		ber_sockbuf_free(conn->c_sb);	   
	}
	if (NULL != conn->c_mutex)
	{
		PR_DestroyLock(conn->c_mutex);
	}
	if (NULL != conn->c_pdumutex)
	{
		PR_DestroyLock(conn->c_pdumutex);
	}
	/* PAGED_RESULTS */
	pagedresults_cleanup_all(conn, 0);
}

/*
 * We're going to be making use of this connection again.
 * So, get rid of everything we can't make use of.
 *
 * Note: this function should be called with conn->c_mutex already locked
 * or at a time when multiple threads are not in play that might touch the
 * connection structure.
 */
void
connection_cleanup(Connection *conn)
{
	bind_credentials_clear( conn, PR_FALSE /* do not lock conn */,
							PR_TRUE /* clear external creds. */ );
	slapi_ch_free((void**)&conn->c_authtype);

	/* Call the plugin extension destructors */
	factory_destroy_extension(connection_type,conn,NULL/*Parent*/,&(conn->c_extension));
	/* 
	 * We hang onto these, since we can reuse them.
	 * Sockbuf *c_sb;
	 * PRLock *c_mutex;
	 * PRLock *c_pdumutex;
	 * Conn_private *c_private;
	 */

#ifdef _WIN32
	if (conn->c_prfd && (conn->c_flags & CONN_FLAG_SSL))
	{
		LDAPDebug( LDAP_DEBUG_CONNS,
		  "conn=%" PRIu64 " fd=%d closed now\n",
		  conn->c_connid, conn->c_sd,0);
		PR_Close(conn->c_prfd);
	}
	else if (conn->c_sd)
	{
		LDAPDebug( LDAP_DEBUG_CONNS,
		  "conn=%" PRIu64 " fd=%d closed now\n",
		  conn->c_connid, conn->c_sd,0);
		closesocket(conn->c_sd);
	}
#else
	if (conn->c_prfd)
	{
		PR_Close(conn->c_prfd);
	}
#endif

	conn->c_sd= SLAPD_INVALID_SOCKET;
	conn->c_ldapversion= 0;
	
    conn->c_isreplication_session = 0;
	slapi_ch_free((void**)&conn->cin_addr );
	slapi_ch_free((void**)&conn->cin_destaddr );
    if ( conn->c_domain != NULL )
    {
		ber_bvecfree( conn->c_domain );
		conn->c_domain = NULL;
    }
	/* conn->c_ops= NULL; */
	conn->c_gettingber= 0;
	conn->c_currentber= NULL;
	conn->c_starttime= 0;
	conn->c_connid= 0;
	conn->c_opsinitiated= 0;
	conn->c_opscompleted= 0;
	conn->c_threadnumber= 0;
	conn->c_refcnt= 0;
	conn->c_idlesince= 0;
	conn->c_flags= 0;
	conn->c_needpw= 0;
	conn->c_prfd= NULL;
	/* c_ci stays as it is */
	conn->c_fdi= SLAPD_INVALID_SOCKET_INDEX;
	conn->c_next= NULL;
	conn->c_prev= NULL;
	conn->c_extension= NULL;
	conn->c_ssl_ssf = 0; 
	conn->c_local_ssf = 0;
	conn->c_unix_local = 0;
	/* destroy any sasl context */
	sasl_dispose((sasl_conn_t**)&conn->c_sasl_conn);
	/* PAGED_RESULTS */
	pagedresults_cleanup(conn, 0 /* do not need to lock inside */);

	/* free the connection socket buffer */
	connection_free_private_buffer(conn);
}

/*
 * Callers of connection_reset() must hold the conn->c_mutex lock.
 */
void
connection_reset(Connection* conn, int ns, PRNetAddr * from, int fromLen, int is_SSL)
{
    char *		pTmp = is_SSL ? "SSL " : "";
    char		*str_ip = NULL, *str_destip;
    char		buf_ip[ 256 ], buf_destip[ 256 ];
    char		*str_unknown = "unknown";
    int			in_referral_mode = config_check_referral_mode();

    LDAPDebug( LDAP_DEBUG_CONNS, "new %sconnection on %d\n", pTmp, conn->c_sd, 0 );

    /* bump our count of connections and update SNMP stats */
    conn->c_connid = slapi_counter_increment(num_conns);

    if (! in_referral_mode) {
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsConnectionSeq);
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsConnections);
    }

    /* 
     * get peer address (IP address of this client)
     */
    slapi_ch_free( (void**)&conn->cin_addr ); /* just to be conservative */
    if ( from->raw.family == PR_AF_LOCAL ) { /* ldapi */
	conn->cin_addr = (PRNetAddr *) slapi_ch_malloc( sizeof( PRNetAddr ) );
	PL_strncpyz(buf_ip, from->local.path, sizeof(from->local.path));
	memcpy( conn->cin_addr, from, sizeof( PRNetAddr ) );
	if (!buf_ip[0]) {
	    PR_GetPeerName( conn->c_prfd, from );
	    PL_strncpyz(buf_ip, from->local.path, sizeof(from->local.path));
	    memcpy( conn->cin_addr, from, sizeof( PRNetAddr ) );
	}
	if (!buf_ip[0]) {
	    /* cannot derive local address */
	    /* need something for logging */
	    PL_strncpyz(buf_ip, "local", sizeof(buf_ip));
	}
	str_ip = buf_ip;
    } else if ( ((from->ipv6.ip.pr_s6_addr32[0] != 0) || /* from contains non zeros */
	  (from->ipv6.ip.pr_s6_addr32[1] != 0) || 
	  (from->ipv6.ip.pr_s6_addr32[2] != 0) || 
	  (from->ipv6.ip.pr_s6_addr32[3] != 0)) || 
	 ((conn->c_prfd != NULL) && (PR_GetPeerName( conn->c_prfd, from ) == 0)) ) {
	conn->cin_addr = (PRNetAddr *) slapi_ch_malloc( sizeof( PRNetAddr ) );
	memcpy( conn->cin_addr, from, sizeof( PRNetAddr ) );
		
	if ( PR_IsNetAddrType( conn->cin_addr, PR_IpAddrV4Mapped ) ) {
	     PRNetAddr v4addr;
	     memset( &v4addr, 0, sizeof( v4addr ) );
	     v4addr.inet.family = PR_AF_INET;
	     v4addr.inet.ip = conn->cin_addr->ipv6.ip.pr_s6_addr32[3];
	     PR_NetAddrToString( &v4addr, buf_ip, sizeof( buf_ip ) );
	} else {
	     PR_NetAddrToString( conn->cin_addr, buf_ip, sizeof( buf_ip ) );
	}
	buf_ip[ sizeof( buf_ip ) - 1 ] = '\0';
	str_ip = buf_ip;		        
    } else {
	/* try syscall since "from" was not given and PR_GetPeerName failed */
	/* a corner case */
	struct sockaddr_in addr; /* assuming IPv4 */
#if ( defined( hpux ) )
	int                addrlen;
#else
	socklen_t          addrlen;
#endif

	addrlen = sizeof( addr );
	memset( &addr, 0, addrlen );

	if ( (conn->c_prfd == NULL) && 
	     (getpeername( conn->c_sd, (struct sockaddr *)&addr, &addrlen )
	      == 0) ) {
	    conn->cin_addr = (PRNetAddr *)slapi_ch_malloc( sizeof( PRNetAddr ));
	    memset( conn->cin_addr, 0, sizeof( PRNetAddr ) );
	    PR_NetAddrFamily( conn->cin_addr ) = AF_INET6;
	    /* note: IPv4-mapped IPv6 addr does not work on Windows */
	    PR_ConvertIPv4AddrToIPv6(addr.sin_addr.s_addr, &(conn->cin_addr->ipv6.ip));
	    PRLDAP_SET_PORT(conn->cin_addr, addr.sin_port);

	    /* copy string equivalent of address into a buffer to use for
	     * logging since each call to inet_ntoa() returns a pointer to a
	     * single thread-specific buffer (which prevents us from calling
	     * inet_ntoa() twice in one call to slapi_log_access()).
	     */
	    str_ip = inet_ntoa( addr.sin_addr );
	    strncpy( buf_ip, str_ip, sizeof( buf_ip ) - 1 );
	    buf_ip[ sizeof( buf_ip ) - 1 ] = '\0';
	    str_ip = buf_ip;
	} else {
	    str_ip = str_unknown;
	}
    }

    /*
     * get destination address (server IP address this client connected to)
     */
    slapi_ch_free( (void**)&conn->cin_destaddr ); /* just to be conservative */
    if ( conn->c_prfd != NULL ) {
	conn->cin_destaddr = (PRNetAddr *) slapi_ch_malloc( sizeof( PRNetAddr ) );
	memset( conn->cin_destaddr, 0, sizeof( PRNetAddr ));
	if (PR_GetSockName( conn->c_prfd, conn->cin_destaddr ) == 0) {
	    if ( conn->cin_destaddr->raw.family == PR_AF_LOCAL ) { /* ldapi */
		PL_strncpyz(buf_destip, conn->cin_destaddr->local.path,
			    sizeof(conn->cin_destaddr->local.path));
		if (!buf_destip[0]) {
		    PL_strncpyz(buf_destip, "unknown local file", sizeof(buf_destip));
		}
	    } else if ( PR_IsNetAddrType( conn->cin_destaddr, PR_IpAddrV4Mapped ) ) {
		PRNetAddr v4destaddr;
		memset( &v4destaddr, 0, sizeof( v4destaddr ) );
		v4destaddr.inet.family = PR_AF_INET;
		v4destaddr.inet.ip = conn->cin_destaddr->ipv6.ip.pr_s6_addr32[3];
		PR_NetAddrToString( &v4destaddr, buf_destip, sizeof( buf_destip ) );
	    } else {
		PR_NetAddrToString( conn->cin_destaddr, buf_destip, sizeof( buf_destip ) );
	    }
	    buf_destip[ sizeof( buf_destip ) - 1 ] = '\0';
	    str_destip = buf_destip;		        
	} else {
	    str_destip = str_unknown;
	}
    } else {
	/* try syscall since c_prfd == NULL */
	/* a corner case */
	struct sockaddr_in	destaddr; /* assuming IPv4 */
#if ( defined( hpux ) )
	int			destaddrlen;
#else
	socklen_t		destaddrlen;
#endif

	destaddrlen = sizeof( destaddr );
	memset( &destaddr, 0, destaddrlen );
	if ( (getsockname( conn->c_sd, (struct sockaddr *)&destaddr,
					&destaddrlen ) == 0) ) {
	    conn->cin_destaddr =
		    (PRNetAddr *)slapi_ch_malloc( sizeof( PRNetAddr ));
	    memset( conn->cin_destaddr, 0, sizeof( PRNetAddr ));
	    PR_NetAddrFamily( conn->cin_destaddr ) = AF_INET6;
	    PRLDAP_SET_PORT( conn->cin_destaddr, destaddr.sin_port );
	    /* note: IPv4-mapped IPv6 addr does not work on Windows */
	    PR_ConvertIPv4AddrToIPv6(destaddr.sin_addr.s_addr,
				     &(conn->cin_destaddr->ipv6.ip));

	    /* copy string equivalent of address into a buffer to use for
	     * logging since each call to inet_ntoa() returns a pointer to a
	     * single thread-specific buffer (which prevents us from calling
	     * inet_ntoa() twice in one call to slapi_log_access()).
	     */
	    str_destip = inet_ntoa( destaddr.sin_addr );
	    strncpy( buf_destip, str_destip, sizeof( buf_destip ) - 1 );
	    buf_destip[ sizeof( buf_destip ) - 1 ] = '\0';
	    str_destip = buf_destip;
	} else {
	    str_destip = str_unknown;
	}      
    }


    if ( !in_referral_mode ) {
	/* create a sasl connection */
	ids_sasl_server_new(conn);
    }

    /* log useful stuff to our access log */
    slapi_log_access( LDAP_DEBUG_STATS,
	    "conn=%" NSPRIu64 " fd=%d slot=%d %sconnection from %s to %s\n",
	    conn->c_connid, conn->c_sd, ns, pTmp, str_ip, str_destip );

    /* initialize the remaining connection fields */
    conn->c_ldapversion = LDAP_VERSION3;
    conn->c_starttime = current_time();
    conn->c_idlesince = conn->c_starttime;
    conn->c_flags = is_SSL ? CONN_FLAG_SSL : 0;
    conn->c_authtype = slapi_ch_strdup(SLAPD_AUTH_NONE);
    /* Just initialize the SSL SSF to 0 now since the handshake isn't complete
     * yet, which prevents us from getting the effective key length. */
    conn->c_ssl_ssf = 0;
    conn->c_local_ssf = 0;
}

/* Create a pool of threads for handling the operations */ 
void 
init_op_threads()
{
	int i;
	PRErrorCode errorCode;
	int max_threads = config_get_threadnumber();
	/* Initialize the locks and cv */

   if ((pb_q_lock = PR_NewLock()) == NULL ) {
        errorCode = PR_GetError();
        LDAPDebug( LDAP_DEBUG_ANY,
		   "init_op_threads: PR_NewLock failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
		   errorCode, slapd_pr_strerror(errorCode), 0 );
        exit(-1);
   }

   if ((op_thread_lock = PR_NewLock()) == NULL ) {
        errorCode = PR_GetError();
        LDAPDebug( LDAP_DEBUG_ANY,
		   "init_op_threads: PR_NewLock failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
		   errorCode, slapd_pr_strerror(errorCode), 0 );
        exit(-1);
   }

   if ((op_thread_cv = PR_NewCondVar( op_thread_lock )) == NULL) {
        errorCode = PR_GetError();
       	LDAPDebug( LDAP_DEBUG_ANY, "init_op_threads: PR_NewCondVar failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
		   errorCode, slapd_pr_strerror(errorCode), 0 );
       	exit(-1);
   }

	/* start the operation threads */
	for (i=0; i < max_threads; i++) { 
		PR_SetConcurrency(4);
		if (PR_CreateThread (PR_USER_THREAD,
                	(VFP) (void *) connection_threadmain, NULL,
                	PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, 
			PR_UNJOINABLE_THREAD, 
			SLAPD_DEFAULT_THREAD_STACKSIZE
			) == NULL ) {
			int prerr = PR_GetError();
			LDAPDebug( LDAP_DEBUG_ANY, "PR_CreateThread failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				prerr, slapd_pr_strerror( prerr ), 0 );
		} else {
			g_incr_active_threadcnt();
		}
	}
}

static void 
referral_mode_reply(Slapi_PBlock *pb)
{
    struct slapdplugin *plugin;
    plugin = (struct slapdplugin *) slapi_ch_calloc(1, sizeof(struct slapdplugin));
    if (plugin!=NULL)
	{
	    struct berval *urls[2], url;
	    char *refer;
	    refer = config_get_referral_mode();
	    pb->pb_plugin = plugin;
	    set_db_default_result_handlers(pb);
	    urls[0] = &url;
	    urls[1] = NULL;
	    url.bv_val = refer;
	    url.bv_len = refer ? strlen(refer) : 0;
	    slapi_send_ldap_result(pb, LDAP_REFERRAL, NULL, NULL, 0, urls);
    	slapi_ch_free((void **)&plugin);
	    slapi_ch_free((void **)&refer);
	}
}

static int
connection_need_new_password(const Connection *conn, const Operation *op, Slapi_PBlock *pb)
{
	int r= 0;
	/*
   	 * add tag != LDAP_REQ_SEARCH to allow admin server 3.5 to do 
	 * searches when the user needs to reset 
	 * the pw the first time logon. 
	 * LP: 22 Dec 2000: Removing LDAP_REQ_SEARCH. It's very unlikely that AS 3.5 will
	 * be used to manage DS5.0
	 */

	if ( conn->c_needpw && op->o_tag != LDAP_REQ_MODIFY &&
		op->o_tag != LDAP_REQ_BIND && op->o_tag != LDAP_REQ_UNBIND && 
		op->o_tag != LDAP_REQ_ABANDON && op->o_tag != LDAP_REQ_EXTENDED)
	{
		slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);	
		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d %s\n",
           	pb->pb_conn->c_connid, pb->pb_op->o_opid, 
			"UNPROCESSED OPERATION - need new password" );
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, 
			NULL, NULL, 0, NULL );
		r= 1;
	}
	return r;
}


static void
connection_dispatch_operation(Connection *conn, Operation *op, Slapi_PBlock *pb)
{
	int minssf = config_get_minssf();
	int minssf_exclude_rootdse = 0;

	/* Get the effective key length now since the first SSL handshake should be complete */
	connection_set_ssl_ssf( conn );

	/* Copy the Connection DN and SSF into the operation struct */
	op_copy_identity( conn, op );

	/* If the minimum SSF requirements are not met, only allow
	 * bind and extended operations through.  The bind and extop
	 * code will ensure that only SASL binds and startTLS are
	 * allowed, which gives the connection a chance to meet the
	 * SSF requirements.  We also allow UNBIND and ABANDON.*/
	/* 
	 * If nsslapd-minssf-exclude-rootdse is on, we have to go to the 
	 * next step and check if the operation is against rootdse or not.
	 * Once found it's not on rootdse, return LDAP_UNWILLING_TO_PERFORM there.
	 */
	minssf_exclude_rootdse = config_get_minssf_exclude_rootdse();
	if (!minssf_exclude_rootdse &&
	    (conn->c_sasl_ssf < minssf) && (conn->c_ssl_ssf < minssf) &&
	    (conn->c_local_ssf < minssf) &&(op->o_tag != LDAP_REQ_BIND) &&
	    (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) &&
	    (op->o_tag != LDAP_REQ_ABANDON)) {
		slapi_log_access( LDAP_DEBUG_STATS,
			"conn=%" NSPRIu64 " op=%d UNPROCESSED OPERATION"
			" - Insufficient SSF (local_ssf=%d sasl_ssf=%d ssl_ssf=%d)\n",
			conn->c_connid, op->o_opid, conn->c_local_ssf,
			conn->c_sasl_ssf, conn->c_ssl_ssf );
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
			"Minimum SSF not met.", 0, NULL );
		return;
	}

	/* If anonymous access is disabled and the connection is
	 * not authenticated, only allow bind and extended operations.
	 * We allow extended operations so one can do a startTLS prior
	 * to binding to protect their credentials in transit. 
	 * We also allow UNBIND and ABANDON.
	 *
	 * If anonymous access is only allowed for root DSE searches,
	 * we let SEARCH operations through as well.  The search code
	 * is responsible for checking if the operation is a root DSE
	 * search. */
        if ((slapi_sdn_get_dn(&(op->o_sdn)) == NULL ) &&
            /* anon access off and something other than BIND, EXTOP, UNBIND or ABANDON */
	    (((config_get_anon_access_switch() == SLAPD_ANON_ACCESS_OFF) && (op->o_tag != LDAP_REQ_BIND) &&
             (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) && (op->o_tag != LDAP_REQ_ABANDON)) ||
            /* root DSE access only and something other than BIND, EXTOP, UNBIND, ABANDON, or SEARCH */
	    ((config_get_anon_access_switch() == SLAPD_ANON_ACCESS_ROOTDSE) && (op->o_tag != LDAP_REQ_BIND) &&
	     (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) &&
	     (op->o_tag != LDAP_REQ_ABANDON) && (op->o_tag != LDAP_REQ_SEARCH)))) {
		slapi_log_access( LDAP_DEBUG_STATS,
			"conn=%" NSPRIu64 " op=%d UNPROCESSED OPERATION"
			" - Anonymous access not allowed\n",
            		conn->c_connid, op->o_opid );

		send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                                  "Anonymous access is not allowed.",
                                  0, NULL );
		return;
	}

	/* process the operation */
	switch ( op->o_tag ) {
	case LDAP_REQ_BIND:
		operation_set_type(op,SLAPI_OPERATION_BIND);
		do_bind( pb );
		break;

	case LDAP_REQ_UNBIND:
		operation_set_type(op,SLAPI_OPERATION_UNBIND);
		do_unbind( pb );
		break;

	case LDAP_REQ_ADD:
		operation_set_type(op,SLAPI_OPERATION_ADD);
		do_add( pb );
		break;

	case LDAP_REQ_DELETE:
		operation_set_type(op,SLAPI_OPERATION_DELETE);
		do_delete( pb );
		break;

	case LDAP_REQ_MODRDN:
		operation_set_type(op,SLAPI_OPERATION_MODRDN);
		do_modrdn( pb );
		break;

	case LDAP_REQ_MODIFY:
		operation_set_type(op,SLAPI_OPERATION_MODIFY);
		do_modify( pb );
		break;

	case LDAP_REQ_COMPARE:
		operation_set_type(op,SLAPI_OPERATION_COMPARE);
		do_compare( pb );
		break;

	case LDAP_REQ_SEARCH:
		operation_set_type(op,SLAPI_OPERATION_SEARCH);
		

	/* On Linux we can use TCP_CORK to get us 5-10% speed benefit when one entry is returned */
	/* Nagle needs to be turned _off_, the default is off on linux, in daemon.c */
#if defined(LINUX)
	{
		int i = 1;
		int ret = 0;
		/* Set TCP_CORK here but only if this is not LDAPI */
		if(!conn->c_unix_local)
		{
			ret = setsockopt(conn->c_sd,IPPROTO_TCP,TCP_CORK,&i,sizeof(i));
			if (ret < 0) {
				LDAPDebug(LDAP_DEBUG_ANY, "Failed to set TCP_CORK on connection %" NSPRIu64 "\n",conn->c_connid, 0, 0);
			}
		}
#endif

		do_search( pb );

#if defined(LINUX)
		/* Clear TCP_CORK to flush any unsent data but only if not LDAPI*/
		i = 0;
		if(!conn->c_unix_local)
		{
			ret = setsockopt(conn->c_sd,IPPROTO_TCP,TCP_CORK,&i,sizeof(i));
			if (ret < 0) {
				LDAPDebug(LDAP_DEBUG_ANY, "Failed to clear TCP_CORK on connection %" NSPRIu64 "\n",conn->c_connid, 0, 0);
			}
		}
	}
#endif
		break;

	/* for some strange reason, the console is using this old obsolete
	 * value for ABANDON so we have to support it until the console
	 * get fixed
	 * otherwise the console has VERY BAD performances when a fair amount
	 * of entries are created in the DIT
	 */
	case LDAP_REQ_ABANDON_30:
	case LDAP_REQ_ABANDON:
		operation_set_type(op,SLAPI_OPERATION_ABANDON);
		do_abandon( pb );
		break;

	case LDAP_REQ_EXTENDED:
		operation_set_type(op,SLAPI_OPERATION_EXTENDED);
		do_extended( pb );
		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ignoring unknown LDAP request (conn=%" NSPRIu64 ", tag=0x%lx)\n",
		    conn->c_connid, op->o_tag, 0 );
		break;
	}
}

/* this function should be called under c_mutex */
int connection_release_nolock (Connection *conn)
{
    if (conn->c_refcnt <= 0)
    {
        slapi_log_error(SLAPI_LOG_FATAL, "connection",
		                "conn=%" NSPRIu64 " fd=%d Attempt to release connection that is not acquired\n",
			            conn->c_connid, conn->c_sd);
        PR_ASSERT (PR_FALSE);
        return -1;
    }
    else
    {
        conn->c_refcnt--;

        return 0;
    }
}

/* this function should be called under c_mutex */
int connection_acquire_nolock (Connection *conn)
{
    /* connection in the closing state can't be acquired */
    if (conn->c_flags & CONN_FLAG_CLOSING)
    {
	/* This may happen while other threads are still working on this connection */
        slapi_log_error(SLAPI_LOG_FATAL, "connection",
		                "conn=%" NSPRIu64 " fd=%d Attempt to acquire connection in the closing state\n",
			            conn->c_connid, conn->c_sd);
        return -1;
    }
    else
    {
        conn->c_refcnt++;
        return 0;
    }
}

/* returns non-0 if connection can be reused and 0 otherwise */
int connection_is_free (Connection *conn)
{
    int rc;

    PR_Lock(conn->c_mutex);
    rc = conn->c_sd == SLAPD_INVALID_SOCKET && conn->c_refcnt == 0 &&
         !(conn->c_flags & CONN_FLAG_CLOSING);
    PR_Unlock(conn->c_mutex);

    return rc;
}

int connection_is_active_nolock (Connection *conn)
{
    return (conn->c_sd != SLAPD_INVALID_SOCKET) && 
           !(conn->c_flags & CONN_FLAG_CLOSING);
}

/* returns non-0 if this is an active connection meaning it is in use
   and not in the closing mode */

#if defined LDAP_IOCP
/*
 * IO Completion ports are currently only available on NT.
 */

typedef enum  {read_data, write_data, new_connection} work_type;
static int wait_on_new_work(Connection **ppConn, work_type *type);
static int issue_new_read(Connection *conn);
static int finished_chomping(Connection *conn);
static int read_the_data(Connection *op, int *process_op, int *defer_io, int *defer_pushback);
static int is_new_operation(Connection *conn);
static int process_operation(Connection *conn, Operation *op);
static int connection_operation_new(Connection *conn, Operation **ppOp);
Operation *get_current_op(Connection *conn);
static int handle_read_data(Connection *conn,Operation **op,
	 int * connection_referenced);
int queue_pushed_back_data(Connection *conn);
static int add_to_select_set(Connection *conn);

static void inc_op_count(Connection* conn)
{
	PR_AtomicIncrement(&conn->c_opscompleted);
	slapi_counter_increment(ops_completed);
}

static int connection_increment_reference(Connection *conn)
{
	int rc = 0;
	PR_Lock( conn->c_mutex );
	rc = connection_acquire_nolock (conn);
	PR_Unlock( conn->c_mutex );
	return rc;
}

static void connection_decrement_reference(Connection *conn)
{
	PR_Lock( conn->c_mutex );
	connection_release_nolock (conn);
	PR_Unlock( conn->c_mutex );
}

static void
connection_threadmain()
{
	/*
	 * OK, so this is the thread main routine for the thread pool.
	 * This is the general idea : wait on the i/o completion port.
	 * then get some data. There are three cases here:
	 * 1) This is the first piece of data read for a new LDAP op.
	 * 2) This is a subsequent, but not final, piece of data read in the current LDAP op on this connection
	 * 3) This is the last piece of the current LDAP op on the current connection.
	 * Note that these cases are NOT exclusive ! In particular, all three can occur for the same read.
	 * based on detecting these cases, we end up doing one or more of the following things:
	 * a) Create new structures for a new op.
	 * b) Read data into the BER buffer for the op.
	 * c) Press on to service the operation request (note that the results are currently written
	 * synchronously.
	 * We always queue a new read on the socket too.
	 * (Note, we need to make sure we don't issue the new read operation until we've copied
	 * the data from the existing one. Otherwise we'd open ourselves to getting OOO data.)
	 *
	 * The intention is that this code will be clean enough to be used for the UNIX build,
	 * once we fake up I/O completion ports with select and another thread.
	 */

	Connection *conn = NULL;
	Operation *op = NULL;
	int return_value = -1;
	int abandon_connection = 0;
	work_type command = 0;
	int connection_referenced = 0;

	/* Don't ask me, and I will tell you no lies */
#if defined( OSF1 ) || defined( hpux ) || defined( LINUX )
	/* Arrange to ignore SIGPIPE signals. */
	SIGNAL( SIGPIPE, SIG_IGN );
#endif

	while (1) {

		abandon_connection = 1; /* we start off assuming that we'll fail somewhere */
		conn = NULL; /* just make sure we don't step on an old connection by mistake */
		op = NULL; /* Same goes for the operation */

		return_value = wait_on_new_work(&conn,&command);
        if( op_shutdown ) 
            break;
		if (0 == return_value) {
			connection_referenced = 0; /* No outstanding ref count on connection if wait for work returned OK */
			switch (command) {
				case read_data:
					return_value = handle_read_data(conn,&op,&connection_referenced);
					if (0 == return_value)
					{
						abandon_connection = 0;
					}
					break;
				case write_data:
					/* NYI, but we need to go and find the state for the connection, find the operation
					 * which queued the write, and then get whatever data we need to write, then write it ! */
					break;
				case new_connection:
					/* NYI, but this would consist of the same stuff which is currently in daemon.c.
					 * On NT, we'd use AcceptEx() */
					break;
				default:
					break;
			}
			finished_chomping(conn);
		} else {
			PR_SetError(PR_IO_ERROR, return_value);
			connection_referenced = 1; /* There is an outstanding refcnt on the conn, so we get to close the right one ! */
		}

		/* If anything went wrong with the connection above, such that we need to
		 * disconnect it, we'll know here and shoot it in the foot.
		 */
		if ( (NULL != conn) && abandon_connection) {
			disconnect_server(conn, conn->c_connid, op ? op->o_opid : -1, SLAPD_DISCONNECT_ABORT, 0 );
			if (connection_referenced) {
				connection_decrement_reference(conn);
			}
		}
	}
	g_decr_active_threadcnt();
}

static int handle_read_data(Connection *conn,Operation **op,
			 int * connection_referenced)
{
	int return_value = 0;
	int return_value2 = 0;
	int process_op = 0; /* Do we or do we not process a complete operation now ? */
	int defer_io = 0;
	int defer_pushback = 0;

	if (is_new_operation(conn)) {
		return_value = connection_operation_new(conn,op);
	} else {
		*op = get_current_op(conn);
	}
	
	/* if connection is closing */
	if (return_value != 0) {
	    LDAPDebug(LDAP_DEBUG_CONNS,
		      "handle_read_data returns as conn %" NSPRIu64 " closing, fd=%d\n",
		      conn->c_connid,conn->c_sd,0);
	    return return_value;
	}

	return_value = read_the_data(conn,&process_op, &defer_io, &defer_pushback);

	if (0 == return_value) {
		int replication_session = conn->c_isreplication_session;
		if (0 != process_op)
			return_value = process_operation(conn,*op);
		/* Post any pending I/O operation _after_ processing any operation */
		if (replication_session) {
			/* Initiate any deferred I/O here */
			if (defer_io) {
				if (conn->c_flags & CONN_FLAG_SSL) {
					add_to_select_set(conn);
					return_value2 = 0;
				} else {
					return_value2 = issue_new_read(conn);
				}
			}
			if (defer_pushback) {
				return_value2 = queue_pushed_back_data(conn);
			}
		}
	}
	else
		*connection_referenced = 1;

	if (return_value) {
		return return_value;
	} else {
		return return_value2;
	}
}

/* Function which does the work involved in servicing an LDAP operation. */
static int process_operation(Connection *conn, Operation *op)
{
	Slapi_PBlock	*pb = NULL;
	ber_len_t	len;
	ber_tag_t	tag;
	ber_int_t	msgid;
	int return_value = 0;
	int destroy_content = 1;


	pb = (Slapi_PBlock *) slapi_ch_calloc( 1, sizeof(Slapi_PBlock) );
	pb->pb_conn = conn;
	pb->pb_op = op;
    /* destroy operation content when done */
    slapi_pblock_set (pb, SLAPI_DESTROY_CONTENT, &destroy_content);

	if (! config_check_referral_mode()) {
		slapi_counter_increment(ops_initiated);
		slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsInOps); 
	}

	if ( (tag = ber_get_int( op->o_ber, &msgid ))
		!= LDAP_TAG_MSGID ) {
		/* log, close and send error */
		LDAPDebug( LDAP_DEBUG_ANY,
			"conn=%" NSPRIu64 " unable to read tag for incoming request\n", conn->c_connid, 0, 0 );
		return_value = -1;
		goto done;
	}
	op->o_msgid = msgid;

	tag = ber_peek_tag( op->o_ber, &len );
	switch ( tag ) {
	  case LBER_ERROR:
	  case LDAP_TAG_LDAPDN: /* optional username, for CLDAP */
		/* log, close and send error */
		LDAPDebug( LDAP_DEBUG_ANY,
			"conn=%" NSPRIu64 " ber_peek_tag returns 0x%lx\n", conn->c_connid, tag, 0 );
		return_value = -1;
		goto done;
	  default:
		break;
	}
	op->o_tag = tag;

	/* are we in referral-only mode? */
	if (config_check_referral_mode() && tag != LDAP_REQ_UNBIND)
	{
	    referral_mode_reply(pb);
	    goto done;
	}

	/* check if new password is required */
	if(connection_need_new_password(conn, op, pb))
	{
		goto done;
	}

        /* if this is a bulk import, only "add" and "import done (extop)" are 
         * allowed */
        if (conn->c_flags & CONN_FLAG_IMPORT) {
            if ((tag != LDAP_REQ_ADD) && (tag != LDAP_REQ_EXTENDED)) {
                /* no cookie for you. */
                LDAPDebug(LDAP_DEBUG_ANY, "Attempted operation %d from "
                          "within bulk import\n", tag, 0, 0);
                slapi_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL,
                                       0, NULL);
                return_value = -1;
                goto done;
            }
        }

	/*
	 * Call the do_<operation> function to process this request.
	 */
	connection_dispatch_operation(conn, op, pb);

done:

	/* If we're here, it means that we successfully completed an operation , so bump the counts */
	inc_op_count(conn);

	if ( !( pb->pb_op->o_flags & OP_FLAG_PS )) {
	    /*
	     * If not a persistent search, remove the operation
	     * from this connection's list.
	     */
	    PR_Lock( conn->c_mutex );
	    connection_remove_operation( conn, op );
	    PR_Unlock( conn->c_mutex );

		/* destroying the pblock will cause destruction of the operation
		 * so this must happen before releasing the connection
		 */
	    slapi_pblock_destroy( pb );

	    PR_Lock( conn->c_mutex );
        if (connection_release_nolock (conn) != 0)
	    {
			return_value = -1;
		}
	    PR_Unlock( conn->c_mutex );

	} else { /* ps code acquires ref to conn - we need to release ours here */
	    PR_Lock( conn->c_mutex );
        if (connection_release_nolock (conn) != 0)
	    {
			return_value = -1;
		}
	    PR_Unlock( conn->c_mutex );
	}
	return return_value;
}

/* Helper functions for the code above: */

 
struct Conn_private {
	/* First the platform-dependent part */
#ifdef _WIN32
	OVERLAPPED c_overlapped;
	DWORD c_buffer_size;
	char *c_buffer;
	DWORD c_number_of_async_bytes_read;
	DWORD c_buffer_offset;
	DWORD c_deferred_length;
#else
#endif
	/* Now the platform independent part */
	Operation *c_current_op;
	int c_flags;
};

static void connection_free_private_buffer(Connection *conn)
{
#ifdef _WIN32
	if (NULL != conn->c_private) {
		slapi_ch_free( (void**)&conn->c_private->c_buffer);
	}
#else
#endif
}

#define FLAG_CONN_HAD_SOME 1 /* Set when we've read the first piece of data already, means we don't need to allocate a new op */
#define FLAG_CONN_COMPLETE 2 /* Set when we've read all of an LDAP operation request, means we can proceed to process it */


/* Little helper functions */

Operation *get_current_op(Connection *conn)
{
	Operation *return_op = conn->c_private->c_current_op;
	PR_ASSERT(NULL != return_op);
	return return_op;
}

static int is_new_operation(Connection *conn)
{
	if (0 == conn->c_private->c_flags) {
		return 1;
	} else {
		return 0;
	}
}

/* Called when a new operation comes in on a connection */
static int connection_operation_new(Connection *conn, Operation **ppOp)
{
	/* we need to make a new operation structure and chain it onto the connection */
	Operation *temp_op = NULL;
	int rc;

	PR_Lock( conn->c_mutex );
	if (connection_is_active_nolock(conn) == 0) {
	    LDAPDebug(LDAP_DEBUG_CONNS,
		      "not creating a new operation when conn %" NSPRIu64 " closing\n",
		      conn->c_connid,0,0);
	    PR_Unlock( conn->c_mutex );
	    return -1;
	}
	temp_op = operation_new( plugin_build_operation_action_bitmap( 0,
			plugin_get_server_plg() ));
	connection_add_operation( conn, temp_op);
	rc = connection_acquire_nolock (conn); 
	PR_Unlock( conn->c_mutex );
	/* Stash the op pointer in the connection structure for later use */
	PR_ASSERT(NULL == conn->c_private->c_current_op);
	conn->c_private->c_current_op = temp_op;
	*ppOp = temp_op;
	return rc;
}

/* Call this to tell the select thread to put us back into the read-ready signal set */
static int add_to_select_set(Connection *conn)
{
	conn->c_gettingber = 0;
	signal_listner();
	return 0;
}

static int remove_from_select_set(Connection *conn)
{
	conn->c_gettingber = 1;
	return 0;
}

/* Helper functions from here on are platform-dependent */
/* First the NT ones */

#ifdef _WIN32

static HANDLE completion_port = INVALID_HANDLE_VALUE;
#define COMPKEY_DIE ((DWORD) -1L) /* used to kill off workers */

static void push_back_data(Connection *conn, size_t offset, size_t length);
static int queue_pushed_back_data(Connection *conn);

/* Called when we've read from the completion queue, so there's data
 * waiting for us to pickup. We're told: the number of bytes read, the
 * address of the buffer, the state of this connection (new op, middle of op).
 */
static int read_the_data(Connection *conn, int *process_op, int *defer_io, int *defer_pushback)
{
	Conn_private *priv = conn->c_private;
	Operation *op = NULL;
	DWORD Bytes_Read = 0;
	char *Buffer = NULL;
	ber_tag_t tag = 0;
	int return_value = -1;
	ber_len_t ber_len = 0;
	ber_len_t Bytes_Scanned = 0;

	*defer_io = 0;
	*defer_pushback = 0;

	op = priv->c_current_op;
	Bytes_Read = priv->c_number_of_async_bytes_read;
	Buffer = priv->c_buffer + priv->c_buffer_offset;

	PR_ASSERT(NULL != op->o_ber);
	
	/* Is this an SSL connection ? */
	if (0 == (conn->c_flags & CONN_FLAG_SSL)) {
		/* Not SSL */

		if (! config_check_referral_mode()) {
		/* Update stats */
			PR_Lock( op_thread_lock );
			(*(g_get_global_snmp_vars()->ops_tbl.dsBytesRecv)) += Bytes_Read;	 
			PR_Unlock( op_thread_lock );
		}

		/* We need to read the data into the BER buffer */
		/* This can return a tag pr LBER_DEFAULT, indicating some error condition */
		tag = ber_get_next_buffer_ext( Buffer, Bytes_Read, &ber_len, op->o_ber, &Bytes_Scanned, conn->c_sb );
		if (LBER_DEFAULT == tag || LBER_OVERFLOW == tag)
		{
			if (0 == Bytes_Scanned)
			{
				/* Means we encountered an error---eg the client sent us pure crap---
				a bunch of bytes which we took to be a tag, length, then we ran off the
				end of the buffer. The next time we get here, we'll be returned LBER_DEFAULT
				This means that everything we've seen up till now is useless because it wasn't
				an LDAP message. 
				So, we toss it away ! */
				if (LBER_OVERFLOW == tag) {
					slapi_log_error( SLAPI_LOG_FATAL, "connection",
						"conn=%" NSPRIu64 " fd=%d The length of BER Element was too long.\n",
						conn->c_connid, conn->c_sd );
				}
				PR_Lock( conn->c_mutex );
				connection_remove_operation( conn, op );
				operation_free(&op, conn);
				priv->c_current_op = NULL;
				PR_Unlock( conn->c_mutex );
				return -1; /* Abandon Connection */
			}
		}
		if (is_ber_too_big(conn,ber_len))
		{
			PR_Lock( conn->c_mutex );
			connection_remove_operation( conn, op );
			operation_free(&op, conn);
			priv->c_current_op = NULL;
			PR_Unlock( conn->c_mutex );
			return -1; /* Abandon Connection */
		}

		/* We set the flag to indicate that we'er in the middle of an op */
		priv->c_flags |= FLAG_CONN_HAD_SOME;
		
		/* Then we decide whether this is the last read for the current op */
		/* and set the flag accordingly */
		if (LBER_DEFAULT != tag) {	/* we received a complete message */
			if (LDAP_TAG_MESSAGE == tag) {	/* looks like an LDAP message */
				/* It's time to process this operation */
				*process_op = 1;
				priv->c_current_op = NULL;
				priv->c_flags = 0;
			} else {
				/*
				 * We received a non-LDAP message.  Log and close connection.
				 */
				LDAPDebug( LDAP_DEBUG_ANY,
					"conn=%" NSPRIu64 " received a non-LDAP message"
					" (tag 0x%lx, expected 0x%lx)\n",
					conn->c_connid, tag, LDAP_TAG_MESSAGE );
				PR_Lock( conn->c_mutex );
				connection_remove_operation( conn, op );
	     	    operation_free(&op, conn);
				priv->c_current_op = NULL;
				PR_Unlock( conn->c_mutex );
				return -1; /* Abandon Connection */
			}
		}

		/* Finally, mark whether there's the beginning of another operation remaining in the buffer */
		/* If there is, queue up another I/O completion request on the port to get it handled OK */
		/* If not, issue a new read on the socket. */
		if (Bytes_Scanned != Bytes_Read) {
		        if (connection_increment_reference(conn) == -1) {
			    LDAPDebug(LDAP_DEBUG_CONNS,
				      "could not acquire lock in issue_new_read as conn %" NSPRIu64 " closing fd=%d\n",
				      conn->c_connid,conn->c_sd,0); 
			    /* XXX how to handle this error? */
			    /* MAB: 25 Jan 01: let's try like this and pray this won't leak... */
			    /* GB : this should be OK because an error here 
			     * means some other thread decided to close the
		             * connection, which mean a fatal error happened
			     * in that case just forget about the remaining 
		 	     * data and return
			     */
			    return (0);
			}
			push_back_data(conn,priv->c_overlapped.Offset + Bytes_Scanned,Bytes_Read-Bytes_Scanned);
			if (!conn->c_isreplication_session) {
				if ((return_value = queue_pushed_back_data(conn)) == -1) {
		 			/* MAB: 25 jan 01 we need to decrement the conn refcnt before leaving... Otherwise,
					 * this thread will unbalance the ref_cnt inc and dec for this connection
					 * and the result is that the connection is never closed and instead is kept 
					 * forever an never released -> this was causing a fd starvation on NT
					 */
					connection_decrement_reference(conn);
					LDAPDebug(LDAP_DEBUG_CONNS,
						  "push_back_data failed: closing conn %" NSPRIu64 " fd=%d\n",
						  conn->c_connid,conn->c_sd,0); 
				}
			} else {
				/* Queue the I/O later to serialize */
				*defer_pushback = 1;
				return_value = 0;
			}
		} else {
			priv->c_overlapped.Offset = 0;
			if (!conn->c_isreplication_session) {
				return_value = issue_new_read(conn);
			} else {
				/* Queue the I/O later to serialize */
				*defer_io = 1;
				return_value = 0;
			}
		}
	} else {
		/* SSL */
		if ( (tag = ber_get_next( conn->c_sb, &ber_len, op->o_ber ))
			   != LDAP_TAG_MESSAGE ) {
			return( -1 );
		}
		if(is_ber_too_big(conn,ber_len))
		{
		    return( -1 );
		}
		/* Put this connection back into the read-ready signal state */
		/* priv->c_flags |= FLAG_CONN_COMPLETE; Redundant now */
		/* It's time to process this operation */
		*process_op = 1;
		priv->c_current_op = NULL;
		priv->c_flags = 0;
		return_value = 0;
		if (!conn->c_isreplication_session) {
			add_to_select_set(conn);
		} else {
			*defer_io = 1;
		}
	}

	return return_value;
}
 
void push_back_data(Connection *conn, size_t offset, size_t length)
{
	conn->c_private->c_overlapped.Offset = offset;
	conn->c_private->c_deferred_length = length;
}

int queue_pushed_back_data(Connection *conn)
{
	/* Use PostQueuedCompletionStatus() to push the data back up the pipe */
	BOOL return_bool = FALSE;

	return_bool = PostQueuedCompletionStatus(completion_port,conn->c_private->c_deferred_length,(DWORD)conn,&conn->c_private->c_overlapped); 

	if (return_bool) {
		return 0;
	} else {
		return -1;
	}
}

/* This function issues a new read operation on the connection.
 * Called once we've finished reading everything from the buffer.
 * VMS crusties will notice the similarity to $QIO.
 */
int issue_new_read(Connection *conn)
{
	BOOL return_bool = FALSE;
	HANDLE socket = INVALID_HANDLE_VALUE;
	void **buffer = NULL;
	DWORD bytes_read = 0;
	DWORD buffer_size = 0;
	OVERLAPPED *overlapped = NULL;

	PR_ASSERT(NULL != conn);
	socket = (HANDLE)conn->c_sd;
	PR_ASSERT(NULL != socket);

	/* here we make sure that we have a buffer allocated */
	buffer = &conn->c_private->c_buffer;
	if (NULL == *buffer) {
		*buffer = (void*)slapi_ch_malloc(LDAP_SOCKET_IO_BUFFER_SIZE);
		if (NULL == *buffer) {
			/* memory allocation failure */
			return -1;
		}
		conn->c_private->c_buffer_size = LDAP_SOCKET_IO_BUFFER_SIZE;
	}

	buffer_size = conn->c_private->c_buffer_size;
	overlapped = &conn->c_private->c_overlapped;

	if (connection_increment_reference(conn) == -1) {
	    LDAPDebug(LDAP_DEBUG_CONNS,
		      "could not acquire lock in issue_new_read as conn %" NSPRIu64 " closing fd=%d\n",
		      conn->c_connid,conn->c_sd,0); 
	    /* This means that the connection is closing */
	    return -1;
	}
	return_bool = ReadFile(socket,*buffer,buffer_size,&bytes_read,overlapped);
	if ( !return_bool && ERROR_IO_PENDING != GetLastError( ) ) {
		/* This means that the connection is shot for some reason */
		connection_decrement_reference(conn);
		return -1;
    } else {
		/* Our work is done, i/o read now queued */
		return 0;
	}
}

static int wait_on_new_work(Connection **ppConn, work_type *type)
{
	/* Here, we wait on the I/O completion port for new data */
	/* because we're not sure whether the completion port has been created yet,
	 * we wait 'till it has been.
	 */
	Connection *temp_conn = NULL;
	DWORD Bytes_Received = 0;
	OVERLAPPED *pOverlapped = NULL;
	BOOL return_bool = FALSE;

	*type = read_data;

	while ( (INVALID_HANDLE_VALUE == completion_port) && (!op_shutdown) ) {
		Sleep(100);
	}
	while (1) {
		if (op_shutdown) {
			return EINTR;
		}
		return_bool = GetQueuedCompletionStatus(completion_port,&Bytes_Received,(DWORD*)&temp_conn,&pOverlapped,INFINITE);
        if ((unsigned long)temp_conn == COMPKEY_DIE ) {                       
			 continue;  /* kill this worker */
        }
		if (TRUE == return_bool) {
			/* we successfully completed the I/O operation */
			/* set the connection pointer the caller gave us to the one from the port */
			PR_ASSERT(NULL != pOverlapped);
			PR_ASSERT(NULL != temp_conn);
			*ppConn = temp_conn;
			/* store the # bytes read in the connection structure */
			(*ppConn)->c_private->c_number_of_async_bytes_read = Bytes_Received;
			(*ppConn)->c_private->c_buffer_offset = (*ppConn)->c_private->c_overlapped.Offset;
			if( Bytes_Received == 0 )
			{
				/* 0 bytes received from a completed overlapped I/O 
				 operation means the socket's been closed. */
				break;
			}
			(*ppConn)->c_idlesince = current_time();
			/* If we exit here, everything is OK */
			connection_decrement_reference(temp_conn);
			return 0;
		}
		if ( (FALSE == return_bool) && (NULL == pOverlapped) ) {
			/* we timed out */
            /* slapi_log_error( SLAPI_LOG_FATAL, "connection",
		                     "GetQueuedCompletionStatus call timed out\n");*/
			continue;
		}
		if ( (FALSE == return_bool) && (NULL != pOverlapped)) {
			/* signifies some sort of i/o error, most likely an abortive close */
            /* slapi_log_error( SLAPI_LOG_FATAL, "connection",
		            "GetQueuedCompletionStatus call failed; error - %ld\n", GetLastError());*/
			if (NULL != temp_conn) {
				/* If we were told the connection, return it--otherwise we can't tell which connection to close */
				*ppConn = temp_conn;
			}
			break;
		}
	}
	return EPIPE; /* we failed to read for some reason */
}

int connection_new_private(Connection *conn)
{
	/* first add to the completion port */
	DWORD threads = 10; /* DBDB hackhack */
	HANDLE socket = INVALID_HANDLE_VALUE;
	HANDLE return_port = NULL;
	Conn_private *priv = NULL;
	int return_value = -1;
	
	PR_ASSERT(NULL != conn);

	socket = (HANDLE) conn->c_sd;

	/* make the private data if it isn't already there */

	if (NULL == conn->c_private) {
		Conn_private *new_private = (Conn_private *)slapi_ch_malloc(sizeof(Conn_private));
		if (NULL == new_private) {
			/* memory allocation failed */
			return -1;
		}
		conn->c_private = new_private;
		ZeroMemory(conn->c_private,sizeof(Conn_private));
	}
	priv = conn->c_private;
	/* Make sure the private structure is cleared */
	/* Note: you must modify this code if the contents
	 * of the structure are changed---we can't simply 
	 * zero the structure because we want to preserve the
	 * buffer. IMPORTANT---here we reuse the I/O buffer
	 * from before. This is deliberate, to avoid mallocing again */
	ZeroMemory(&(priv->c_overlapped),sizeof(OVERLAPPED));
	priv->c_number_of_async_bytes_read = 0;
	priv->c_buffer_offset = 0;
	priv->c_flags = 0;
	priv->c_current_op = NULL;


	if (INVALID_HANDLE_VALUE == completion_port) {
		/* completion port not yet setup, we need to make it */
		completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE,NULL,0,0);
		if (NULL == completion_port) {
			LDAPDebug(LDAP_DEBUG_ANY,"Failed to create master I/O completion port\n",0,0,0);
			return -1;
		}
	}
	/* If the connection is SSL, don't do the right thing */
	if (0 == (conn->c_flags & CONN_FLAG_SSL)) {
		return_port = CreateIoCompletionPort(socket,completion_port,(DWORD)conn,0);
		if (NULL == return_port) {
			LDAPDebug(LDAP_DEBUG_ANY,"Failed to associate socket with I/O completion port, fd=%d,GetLastError = %d\n",socket,GetLastError(),0);
			return -1;
		}
		/* Now queue the initial read on this connection */
		return_value = issue_new_read(conn);
	} else {
		return_value = 0;
	}

	return return_value;
}

/* If all is well, this only gets called for SSL connections */
int connection_activity(Connection *conn)
{
	/* First check that this really is an SSL connection */
	if (0 == (conn->c_flags & CONN_FLAG_SSL)) {
		return -1;
	}
	/* Now, the plan here is to push something up the IOCP pipe */
	/* We need to fake something up so that the code which pulls 
	 * it off the queue does the right thing. Here's what we do:
	 * We just call PostQueuedCompletionStatus like normal.
	 * The connection is marked as SSL, and it is this that the
	 * reading code notices. Simple !
	 */
	/* Also, we need to participate in the signaling protocol to the select thread */
	remove_from_select_set(conn);
	/* We hold the lock already, increment the reference count, which will
	   be decremented in wait_for_new_work(). */
	if (connection_acquire_nolock (conn) == -1) {
	    LDAPDebug(LDAP_DEBUG_CONNS,
		      "could not acquire lock in connection_activity as conn %" NSPRIu64 " closing fd=%d\n",
		      conn->c_connid,conn->c_sd,0); 
	    /* XXX how to handle this error? */
	    /* MAB: 25 Jan 01: let's return on error and pray this won't leak */
	    return (-1);
	}
	push_back_data(conn, 0, 1);
	return queue_pushed_back_data(conn);
}

static int finished_chomping(Connection *conn)
{
	/* On NT we don't need to do anything here */
	return 0;
}

#else /* WIN32/UNIX */

/*
 * This is where the UNIX Helper functions would be if IO
 * Completion Ports were supported on UNIX.
 */

#endif	/* WIN32/UNIX */

#else /* LDAP_IOCP */

/*
 * IO Completion Ports are not available on this platform.
 */

static int counter= 0; /* JCM Dumb Name */

/* The connection private structure for UNIX turbo mode */
struct Conn_private
{
	int previous_op_count;	/* the operation counter value last time we sampled it, used to compute operation rate */
	int operation_rate;		/* rate (ops/sample period) at which this connection has been processing operations */
	time_t previous_count_check_time; /* The wall clock time we last sampled the operation count */
	size_t c_buffer_size;	/* size of the socket read buffer */
	char *c_buffer;			/* pointer to the socket read buffer */
	size_t c_buffer_bytes; /* number of bytes currently stored in the buffer */
	size_t c_buffer_offset; /* offset to the location of new data in the buffer */
};

#if defined(USE_OPENLDAP)
/* Copy up to bytes_to_read bytes from b into return_buffer.
 * Returns a count of bytes copied (always >= 0).
 */
ber_slen_t
openldap_read_function(Sockbuf_IO_Desc *sbiod, void *buf, ber_len_t len)
{
	Connection *conn = NULL;
	/* copy up to bytes_to_read bytes into the caller's buffer, return the number of bytes copied */
	ber_slen_t bytes_to_copy = 0;
	char *readbuf; /* buffer to "read" from */
	size_t max; /* number of bytes currently stored in the buffer */
	size_t offset; /* offset to the location of new data in the buffer */

	PR_ASSERT(sbiod);
	PR_ASSERT(sbiod->sbiod_pvt);

	conn = (Connection *)sbiod->sbiod_pvt;

	PR_ASSERT(conn->c_private->c_buffer);

	readbuf = conn->c_private->c_buffer;
	max = conn->c_private->c_buffer_bytes;
	offset = conn->c_private->c_buffer_offset;

	if (len <= (max - offset)) {
		bytes_to_copy = len; /* we have enough buffered data */
	} else {
		bytes_to_copy = max - offset; /* just return what we have */
	}

	if (bytes_to_copy <= 0) {
		bytes_to_copy = 0;	/* never return a negative result */
		/* in this case, we don't have enough data to satisfy the
		   caller, so we have to let it know we need more */
#if defined(EWOULDBLOCK)
		errno = EWOULDBLOCK;
#elif defined(EAGAIN)
		errno = EAGAIN;
#endif			
		PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
	} else {
		/* copy buffered data into output buf */
		SAFEMEMCPY(buf, readbuf + offset, bytes_to_copy);
		conn->c_private->c_buffer_offset += bytes_to_copy;
	}
	return bytes_to_copy;
}
#endif

int 
connection_new_private(Connection *conn)
{
	if (NULL == conn->c_private) {
		Conn_private *new_private = (Conn_private *)slapi_ch_calloc(1,sizeof(Conn_private));
		if (NULL == new_private) {
			/* memory allocation failed */
			return -1;
		}
		conn->c_private = new_private;
	}

	/* The c_buffer is supposed to be NULL here, cleaned by connection_cleanup, 
	   double check to avoid memory leak */
	if (NULL == conn->c_private->c_buffer) {
		conn->c_private->c_buffer = (char*)slapi_ch_malloc(LDAP_SOCKET_IO_BUFFER_SIZE);
		if (NULL == conn->c_private->c_buffer) {
			/* memory allocation failure */
			return -1;
		}
		conn->c_private->c_buffer_size = LDAP_SOCKET_IO_BUFFER_SIZE;
	}

	/*
	 * Clear the private structure, preserving the buffer and length in
	 * case we are reusing the buffer.
	 */
	{
		char	*c_buffer = conn->c_private->c_buffer;
		size_t	c_buffer_size = conn->c_private->c_buffer_size;;

		memset( conn->c_private, 0, sizeof(Conn_private));
		conn->c_private->c_buffer = c_buffer;
		conn->c_private->c_buffer_size = c_buffer_size;
	}

	return 0;
}

static void 
connection_free_private_buffer(Connection *conn)
{
	if (NULL != conn->c_private) {
		slapi_ch_free((void*)&(conn->c_private->c_buffer));
	}
}

/*
 * Turbo Mode:
 * Turbo Connection Mode is designed to more efficiently
 * serve a small number of highly active connections performing
 * mainly search operations. It is only used on UNIX---completion
 * ports on NT make it unnecessary.
 * A connection can be in turbo mode, or not in turbo mode.
 * For non-turbo mode, the code path is the same as was before:
 * worker threads wait on a condition variable for work.
 * When they awake they consult the operation queue for
 * something to do, read the operation from the connection's socket,
 * perform the operation and go back to waiting on the condition variable.
 * In Turbo Mode, a worker thread becomes associated with a connection.
 * It then waits not on the condition variable, but directly on read ready
 * state on the connection's socket. When new data arrives, it decodes
 * the operation and executes it, and then goes back to read another
 * operation from the same socket, or block waiting on new data.
 * The read is done non-blocking, wait in poll with a timeout.
 * 
 * There is a mechanism to ensure that only the most active
 * connections are in turbo mode at any time. If this were not
 * the case we could starve out some client operation requests
 * due to waiting on I/O in many turbo threads at the same time.
 *
 * Each worker thread periodically  (every 10 seconds) examines
 * the activity level for the connection it is processing.
 * This applies regardless of whether the connection is
 * currently in turbo mode or not. Activity is measured as 
 * the number of operations initiated since the last check was done.
 * The N connections with the highest activity level are allowed
 * to enter turbo mode. If the current connection is in the top N,
 * then we decide to enter turbo mode. If the current connection
 * is no longer in the top N, then we leave turbo mode.
 * The decision to enter or leave turbo mode is taken under
 * the connection mutex, preventing race conditions where 
 * more than one thread can change the turbo state of a connection
 * concurrently.
 */


/* Connection status values returned by
	connection_wait_for_new_pb(), connection_read_operation(), etc. */
	
#define CONN_FOUND_WORK_TO_DO 0
#define CONN_SHUTDOWN 1
#define CONN_NOWORK 2
#define CONN_DONE 3
#define CONN_TIMEDOUT 4

#define CONN_TURBO_TIMEOUT_INTERVAL 1000 /* milliseconds */
#define CONN_TURBO_CHECK_INTERVAL 5 /* seconds */
#define CONN_TURBO_PERCENTILE 50 /* proportion of threads allowed to be in turbo mode */
#define CONN_TURBO_HYSTERESIS 0 /* avoid flip flopping in and out of turbo mode */

int connection_wait_for_new_pb(Slapi_PBlock	**ppb, PRIntervalTime	interval)
{
	int ret = CONN_FOUND_WORK_TO_DO;
	
	PR_Lock( op_thread_lock );

	/* While there is no operation to do... */
	while( counter < 1) {
		/* Check if we should shutdown. */ 
		if (op_shutdown) {
			PR_Unlock( op_thread_lock );
			return CONN_SHUTDOWN;
		}
		PR_WaitCondVar( op_thread_cv, interval);
	}

	/* There is some work to do. */

	counter--;
	PR_Unlock( op_thread_lock );

	/* Get the next operation from the work queue. */

	*ppb = get_pb();
	if (*ppb == NULL) {
		LDAPDebug( LDAP_DEBUG_ANY, "pb is null \n", 0,  0, 0 );
		PR_Lock( op_thread_lock );
		counter++;
		PR_Unlock( op_thread_lock );
		ret = CONN_NOWORK;
	}
	return ret;
}

void connection_make_new_pb(Slapi_PBlock	**ppb, Connection	*conn)
{
	/* In the classic case, the pb is made in connection_activity() and then
	   queued. get_pb() dequeues it. So we can just make it ourselves here */

	/* *ppb = (Slapi_PBlock *) slapi_ch_calloc( 1, sizeof(Slapi_PBlock) ); */
	*ppb = slapi_pblock_new();
	(*ppb)->pb_conn = conn;
	(*ppb)->pb_op = operation_new( plugin_build_operation_action_bitmap( 0,
			plugin_get_server_plg() ));
	connection_add_operation( conn, (*ppb)->pb_op );
}


/*
 * Utility function called by  connection_read_operation(). This is a
 * small wrapper on top of libldap's ber_get_next_buffer_ext().
 *
 * Return value:
 *   0: Success
 *      case 1) If there was not enough data in the buffer to complete the 
 *      message, go to the next cycle. In this case, bytes_scanned is set 
 *      to a positive number and *tagp is set to LBER_DEFAULT.
 *      case 2) Complete.  *tagp == (tag of the message) and bytes_scanned is
 *      set to a positive number.
 *  -1: Failure
 *      case 1) *tagp == LBER_OVERFLOW: the length is either bigger than 
 *      ber_uint_t type or the value preset via 
 *      LBER_SOCKBUF_OPT_MAX_INCOMING_SIZE option
 *      case 2) *tagp == LBER_DEFAULT: memory error or tag mismatch
 */
static int
get_next_from_buffer( void *buffer, size_t buffer_size, ber_len_t *lenp,
    ber_tag_t *tagp, BerElement *ber, Connection *conn )
{
	PRErrorCode		err = 0;
	PRInt32			syserr = 0;
	ber_len_t		bytes_scanned = 0;

	*lenp = 0;
#if defined(USE_OPENLDAP)
	*tagp = ber_get_next( conn->c_sb, &bytes_scanned, ber );
#else
	*tagp = ber_get_next_buffer_ext( buffer, buffer_size, lenp, ber,
			&bytes_scanned, conn->c_sb );
#endif
    /* openldap ber_get_next doesn't return partial bytes_scanned if it hasn't
       read a whole pdu - so we have to check the errno for the
       "would block" condition meaning openldap needs more data to read */
	if ((LBER_OVERFLOW == *tagp || LBER_DEFAULT == *tagp) && 0 == bytes_scanned &&
		!SLAPD_SYSTEM_WOULD_BLOCK_ERROR(errno))
	{
		if (LBER_OVERFLOW == *tagp)
		{
			err = SLAPD_DISCONNECT_BER_TOO_BIG;
		}
		else if (errno == ERANGE)
		{
			/* openldap does not differentiate between length == 0
			   and length > max - all we know is that there was a
			   problem with the length - assume too big */
			err = SLAPD_DISCONNECT_BER_TOO_BIG;
		}
		else
		{
			err = SLAPD_DISCONNECT_BAD_BER_TAG;
		}
		syserr = errno;
		/* Bad stuff happened, like the client sent us some junk */
		LDAPDebug( LDAP_DEBUG_CONNS,
			"ber_get_next failed for connection %" NSPRIu64 "\n", conn->c_connid, 0, 0 );
		/* reset private buffer */
		conn->c_private->c_buffer_bytes = conn->c_private->c_buffer_offset = 0;

		/* drop connection */
		disconnect_server( conn, conn->c_connid, -1, err, syserr );
		return -1;
	}

	/* openldap_read_function will advance c_buffer_offset */
#if !defined(USE_OPENLDAP)
	/* success, or need to wait for more data */
	/* if openldap could not read a whole pdu, bytes_scanned will be zero -
	   it does not return partial results */
	conn->c_private->c_buffer_offset += bytes_scanned;
#endif
	return 0;
}

/* Either read read data into the connection buffer, or fail with err set */
static int
connection_read_ldap_data(Connection *conn, PRInt32 *err)
{
	int ret = 0;
    ret = PR_Recv(conn->c_prfd,conn->c_private->c_buffer,conn->c_private->c_buffer_size,0,PR_INTERVAL_NO_WAIT);
    if (ret < 0) {
        *err = PR_GetError();
    }
	return ret;
}

/* Upon returning from this function, we have either: 
   1. Read a PDU successfully.
   2. Detected some error condition with the connection which requires closing it.
   3. In Turbo mode, we Timed out without seeing any data.

   We also handle the case where we read ahead beyond the current PDU
   by buffering the data and setting the 'remaining_data' flag.

 */
int connection_read_operation(Connection *conn, Operation *op, ber_tag_t *tag, int *remaining_data)
{
	ber_len_t	len = 0;
	int		ret = 0;
	int		waits_done = 0;
	ber_int_t	msgid;
	int new_operation = 1; /* Are we doing the first I/O read for a new operation ? */
	char *buffer = conn->c_private->c_buffer;
	PRErrorCode err = 0;
	PRInt32 syserr = 0;
	
	/*
	 * if the socket is still valid, get the ber element
	 * waiting for us on this connection. timeout is handled
	 * in the low-level read_function.
	 */
	if ( (conn->c_sd == SLAPD_INVALID_SOCKET) ||
		 (conn->c_flags & CONN_FLAG_CLOSING) ) {
		return CONN_DONE;
	}
	
	*tag = LBER_DEFAULT;
	/* First check to see if we have buffered data from "before" */
	if (conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset) {
		/* If so, use that data first */
		if ( 0 != get_next_from_buffer( buffer
				+ conn->c_private->c_buffer_offset,
				conn->c_private->c_buffer_bytes
				- conn->c_private->c_buffer_offset,
				&len, tag, op->o_ber, conn )) {
			return CONN_DONE;
		}
		new_operation = 0;
	}
	/* If we still haven't seen a complete PDU, read from the network */
	while (*tag == LBER_DEFAULT) {
		int ioblocktimeout_waits = config_get_ioblocktimeout() / CONN_TURBO_TIMEOUT_INTERVAL;
		/* We should never get here with data remaining in the buffer */
		PR_ASSERT( !new_operation || 0 == (conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset) );
		/* We make a non-blocking read call */
		ret = connection_read_ldap_data(conn,&err);
		if (ret <= 0) {
			if (0 == ret) {
				/* Connection is closed */
				PR_Lock( conn->c_mutex );
				disconnect_server_nomutex( conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, 0 );
				conn->c_gettingber = 0;
				PR_Unlock( conn->c_mutex );
				signal_listner();
				return CONN_DONE;
			}
			/* err = PR_GetError(); */
			/* If we would block, we need to poll for a while */
			syserr = PR_GetOSError();
			if ( SLAPD_PR_WOULD_BLOCK_ERROR( err ) ||
			     SLAPD_SYSTEM_WOULD_BLOCK_ERROR( syserr ) ) {
				struct PRPollDesc	pr_pd;
				PRIntervalTime	timeout = PR_MillisecondsToInterval(CONN_TURBO_TIMEOUT_INTERVAL);
				pr_pd.fd = (PRFileDesc *)conn->c_prfd;
				pr_pd.in_flags = PR_POLL_READ;
				pr_pd.out_flags = 0;
				ret = PR_Poll(&pr_pd, 1, timeout);
				waits_done++;
				/* Did we time out ? */
				if (0 == ret) {
					/* We timed out, should the server shutdown ? */
					if (op_shutdown) {
						return CONN_SHUTDOWN;
					}
					/* We timed out, is this the first read in a PDU ? */
					if (new_operation) {
						/* If so, we return */
						return CONN_TIMEDOUT;
					} else {
						/* Otherwise we loop, unless we exceeded the ioblock timeout */
						if (waits_done > ioblocktimeout_waits) {
							LDAPDebug( LDAP_DEBUG_CONNS,"ioblock timeout expired on connection %" NSPRIu64 "\n", conn->c_connid, 0, 0 );
							disconnect_server( conn, conn->c_connid, -1,
									SLAPD_DISCONNECT_IO_TIMEOUT, 0 );
							return CONN_DONE;
						} else {

							/* The turbo mode may cause threads starvation.
			   				   Do a yield here to reduce the starving.
							*/
							PR_Sleep(PR_INTERVAL_NO_WAIT);

							continue;
						}
					}
				}
				if (-1 == ret) {
					/* PR_Poll call failed */
					err = PR_GetError();
					syserr = PR_GetOSError();
					LDAPDebug( LDAP_DEBUG_ANY,
						"PR_Poll for connection %" NSPRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror( err ) );
					/* If this happens we should close the connection */
					disconnect_server( conn, conn->c_connid, -1, err, syserr );
					return CONN_DONE;
				}
			} else {
				/* Some other error, typically meaning bad stuff */
					syserr = PR_GetOSError();
					LDAPDebug( LDAP_DEBUG_CONNS,
						"PR_Recv for connection %" NSPRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror( err ) );
					/* If this happens we should close the connection */
					disconnect_server( conn, conn->c_connid, -1, err, syserr );
					return CONN_DONE;
			}
		} else {
			/* We read some data off the network, do something with it */
			conn->c_private->c_buffer_bytes = ret;
			conn->c_private->c_buffer_offset = 0;

			if ( get_next_from_buffer( buffer,
						conn->c_private->c_buffer_bytes
						- conn->c_private->c_buffer_offset,
						&len, tag, op->o_ber, conn ) != 0 ) {
				return CONN_DONE;
			}

			new_operation = 0;
			ret = 0;
			waits_done = 0;	/* got some data: reset counter */
		}
	}
	/* If there is remaining buffered data, set the flag to tell the caller */
	if (conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset) {
		*remaining_data = 1;
	}

	if ( *tag != LDAP_TAG_MESSAGE ) {
		/*
		 * We received a non-LDAP message.  Log and close connection.
		 */
		LDAPDebug( LDAP_DEBUG_ANY,
			"conn=%" NSPRIu64 " received a non-LDAP message (tag 0x%lx, expected 0x%lx)\n",
			conn->c_connid, *tag, LDAP_TAG_MESSAGE );
		disconnect_server( conn, conn->c_connid, -1,
			SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO );
		return CONN_DONE;
	}

	if ( (*tag = ber_get_int( op->o_ber, &msgid ))
		!= LDAP_TAG_MSGID ) {
		/* log, close and send error */
		LDAPDebug( LDAP_DEBUG_ANY,
			"conn=%" NSPRIu64 " unable to read tag for incoming request\n", conn->c_connid, 0, 0 );
		disconnect_server( conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO );
		return CONN_DONE;
	}
	if(is_ber_too_big(conn,len))
	{
		disconnect_server( conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_TOO_BIG, 0 );
		return CONN_DONE;
	}
	op->o_msgid = msgid;

	*tag = ber_peek_tag( op->o_ber, &len );
	switch ( *tag ) {
	  case LBER_ERROR:
	  case LDAP_TAG_LDAPDN: /* optional username, for CLDAP */
		/* log, close and send error */
		LDAPDebug( LDAP_DEBUG_ANY,
			"conn=%" NSPRIu64 " ber_peek_tag returns 0x%lx\n", conn->c_connid, *tag, 0 );
		disconnect_server( conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_PEEK, EPROTO );
		return CONN_DONE;
	  default:
		break;
	}
	op->o_tag = *tag;
	return ret;
}

void connection_make_readable(Connection *conn)
{
	PR_Lock( conn->c_mutex );
	conn->c_gettingber = 0;
	PR_Unlock( conn->c_mutex );
	signal_listner();
}

/*
 * Figure out the operation completion rate for this connection
 */
void connection_check_activity_level(Connection *conn)
{
	int current_count = 0;
	int delta_count = 0;
	PR_Lock( conn->c_mutex );
	/* get the current op count */
	current_count = conn->c_opscompleted;
	/* compare to the previous op count */
	delta_count = current_count - conn->c_private->previous_op_count;
	/* delta is the rate, store that */
	conn->c_private->operation_rate = delta_count;
	/* store current count in the previous count slot */
	conn->c_private->previous_op_count = current_count;
	/* update the last checked time */
	conn->c_private->previous_count_check_time = current_time();
	PR_Unlock( conn->c_mutex );
	LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " activity level = %d\n",conn->c_connid,delta_count,0); 
}

typedef struct table_iterate_info_struct {
	int connection_count;
	int rank_count;
	int our_rate;
} table_iterate_info;

int table_iterate_function(Connection *conn, void *arg)
{
	int ret = 0;
	table_iterate_info *pinfo = (table_iterate_info*)arg;
	pinfo->connection_count++;
	if (conn->c_private->operation_rate > pinfo->our_rate) {
		pinfo->rank_count++;
	}
	return ret;
}

/* 
 * Scan the list of active connections, evaluate our relative rank
 * for connection activity.
 */
void connection_find_our_rank(Connection *conn,int *connection_count, int *our_rank)
{
	table_iterate_info info = {0};
	info.our_rate = conn->c_private->operation_rate;
	connection_table_iterate_active_connections(the_connection_table, &info, &table_iterate_function);
	*connection_count = info.connection_count;
	*our_rank = info.rank_count;
}

/* 
 * Evaluate the turbo policy for this connection
 */
void connection_enter_leave_turbo(Connection *conn, int current_turbo_flag, int *new_turbo_flag)
{
	int current_mode = 0;
	int new_mode = 0;
	int connection_count = 0;
	int our_rank = 0;
	int threshold_rank = 0;
	PR_Lock(conn->c_mutex);
	/* We can already be in turbo mode, or not */
	current_mode = current_turbo_flag;
	if (pagedresults_in_use(conn)) {
		/* PAGED_RESULTS does not need turbo mode */
		new_mode = 0;
	} else if (conn->c_private->operation_rate == 0) {
		/* The connection is ranked by the passed activities. If some other
		 * connection have more activity, increase rank by one. The highest 
		 * rank is least activity, good candidates to move out of turbo mode. 
		 * However, if no activity on all the connections, then every 
		 * connection gets 0 rank, so none move out. 
		 * No bother to do so much calcuation, short-cut to non-turbo mode 
		 * if no activities in passed interval */
		new_mode = 0;
	} else {
	  double activet = 0.0;
	  connection_find_our_rank(conn,&connection_count, &our_rank);
	  LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " turbo rank = %d out of %d conns\n",conn->c_connid,our_rank,connection_count); 
	  activet = (double)g_get_active_threadcnt();
	  threshold_rank = (int)(activet * ((double)CONN_TURBO_PERCENTILE / 100.0));

	  /* adjust threshold_rank according number of connections,
	     less turbo threads as more connections,
	     one measure to reduce thread startvation.
	   */
	  if (connection_count > threshold_rank) {
		threshold_rank -= (connection_count - threshold_rank) / 5;
	  }

	  if (current_mode) {
		/* We're currently in turbo mode */
		/* Policy says that we stay in turbo mode provided 
		   connection activity is still high.
		 */
		if (our_rank - CONN_TURBO_HYSTERESIS < threshold_rank) {
			/* Stay in turbo mode */
			new_mode = 1;
		} else {
			/* Exit turbo mode */
			new_mode = 0;
		}
	  } else {
		/* We're currently not in turbo mode */
		/* Policy says that we go into turbo mode if
		   recent connection activity is high. 
		 */
		if (our_rank + CONN_TURBO_HYSTERESIS < threshold_rank) {
			/* Enter turbo mode */
			new_mode = 1;
		} else {
			/* Stay out of turbo mode */
			new_mode = 0;
		}
	  }
	}
	PR_Unlock(conn->c_mutex);
	if (current_mode != new_mode) {
		if (current_mode) {
			LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " leaving turbo mode\n",conn->c_connid,0,0); 
		} else {
			LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " entering turbo mode\n",conn->c_connid,0,0); 
		}
	}
	*new_turbo_flag = new_mode;
}

static void
connection_threadmain()
{
	Slapi_PBlock	*pb = NULL;
	PRIntervalTime	interval = PR_SecondsToInterval(10);
	Connection	*conn = NULL;
	Operation	*op;
	ber_tag_t	tag = 0;
	int need_wakeup;
	int thread_turbo_flag = 0;
	int ret = 0;
	int more_data = 0;
	int replication_connection = 0; /* If this connection is from a replication supplier, we want to ensure that operation processing is serialized */
	int doshutdown = 0;

#if defined( OSF1 ) || defined( hpux )
	/* Arrange to ignore SIGPIPE signals. */
	SIGNAL( SIGPIPE, SIG_IGN );
#endif

	while (1) {
		int is_timedout = 0;
		time_t curtime = 0;
		
		if( op_shutdown ) {
			LDAPDebug( LDAP_DEBUG_TRACE, 
			"op_thread received shutdown signal\n",	0, 0, 0 );
			g_decr_active_threadcnt();
			return;
		}

		if (!thread_turbo_flag && (NULL == pb) && !more_data) {
			/* If more data is left from the previous connection_read_operation,
			   we should finish the op now.  Client might be thinking it's
			   done sending the request and wait for the response forever.
			   [blackflag 624234] */
			ret = connection_wait_for_new_pb(&pb,interval);
			switch (ret) {
				case CONN_NOWORK:
					continue;
				case CONN_SHUTDOWN:
					LDAPDebug( LDAP_DEBUG_TRACE, 
					"op_thread received shutdown signal\n", 					0,  0, 0 );
					g_decr_active_threadcnt();
					return;
				case CONN_FOUND_WORK_TO_DO:
					/* note - don't need to lock here - connection should only
					   be used by this thread - since c_gettingber is set to 1
					   in connection_activity when the conn is added to the
					   work queue, setup_pr_read_pds won't add the connection prfd
					   to the poll list */
					if (connection_call_io_layer_callbacks(pb->pb_conn)) {
						LDAPDebug0Args( LDAP_DEBUG_ANY, "Error: could not add/remove IO layers from connection\n" );
					}
				default:
					break;
			}
		} else if (NULL == pb) {

			/* The turbo mode may cause threads starvation.
			   Do a yield here to reduce the starving
			*/
			PR_Sleep(PR_INTERVAL_NO_WAIT);

			PR_Lock(conn->c_mutex);
			/* Make our own pb in turbo mode */
			connection_make_new_pb(&pb,conn);
			if (connection_call_io_layer_callbacks(conn)) {
				LDAPDebug0Args( LDAP_DEBUG_ANY, "Error: could not add/remove IO layers from connection\n" );
			}
			PR_Unlock(conn->c_mutex);
			if (! config_check_referral_mode()) {
			  slapi_counter_increment(ops_initiated);
			  slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsInOps); 
			}
		}
		/* Once we're here we have a pb */ 
		conn = pb->pb_conn;
		op = pb->pb_op;
		
		more_data = 0;
		ret = connection_read_operation(conn,op,&tag,&more_data);

		curtime = current_time();
#define DB_PERF_TURBO 1		
#if defined(DB_PERF_TURBO)
		/* If it's been a while since we last did it ... */
		if (curtime - conn->c_private->previous_count_check_time > CONN_TURBO_CHECK_INTERVAL) {
			int new_turbo_flag = 0;
			/* Check the connection's activity level */
			connection_check_activity_level(conn);
			/* And if appropriate, change into or out of turbo mode */
			connection_enter_leave_turbo(conn,thread_turbo_flag,&new_turbo_flag);
			thread_turbo_flag = new_turbo_flag;
		}

		/* turn off turbo mode immediately if any pb waiting in global queue */
		if (thread_turbo_flag && (counter > 0)) {
			thread_turbo_flag = 0;
			LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " leaving turbo mode\n",conn->c_connid,0,0); 
		}
#endif
		
		switch (ret) {
			case CONN_DONE:
				/* This means that the connection was closed, so clear turbo mode */
				/*FALLTHROUGH*/
			case CONN_TIMEDOUT:
				thread_turbo_flag = 0;
				is_timedout = 1;
				/* note: 
				 * should call connection_make_readable after the op is removed
				 * connection_make_readable(conn);
				 */
				LDAPDebug(LDAP_DEBUG_CONNS,"conn %" NSPRIu64 " leaving turbo mode due to %d\n",conn->c_connid,ret,0); 
				goto done;
			case CONN_SHUTDOWN:
				LDAPDebug( LDAP_DEBUG_TRACE, 
				"op_thread received shutdown signal\n", 0, 0, 0 );
				g_decr_active_threadcnt();
				doshutdown = 1;
				goto done; /* To destroy pb, jump to done once */
			default:
				break;
		}

		/* if we got here, then we had some read activity */
		if (thread_turbo_flag) {
			/* turbo mode avoids handle_pr_read_ready which avoids setting c_idlesince
			   update c_idlesince here since, if we got some read activity, we are
			   not idle */
			conn->c_idlesince = curtime;
		}

		/* 
		 * Do not put the connection back to the read ready poll list 
		 * if the operation is unbind.  Unbind will close the socket.
		 * Similarly, if we are in turbo mode, don't send the socket 
		 * back to the poll set.
		 * more_data: [blackflag 624234]
		 * If the connection is from a replication supplier, don't make it readable here.
		 * We want to ensure that replication operations are processed strictly in the order
		 * they are received off the wire.
		 */
		replication_connection = conn->c_isreplication_session;
		if (tag != LDAP_REQ_UNBIND && (!thread_turbo_flag) && !more_data && !replication_connection) {
			connection_make_readable(conn);
		}

		/* are we in referral-only mode? */
		if (config_check_referral_mode() && tag != LDAP_REQ_UNBIND) {
		    referral_mode_reply(pb);
		    goto done;
		}

		/* check if new password is required */
		if(connection_need_new_password(conn, op, pb)) {
			goto done;
		}

		/* if this is a bulk import, only "add" and "import done"
		 * are allowed */
		if (conn->c_flags & CONN_FLAG_IMPORT) {
			if ((tag != LDAP_REQ_ADD) && (tag != LDAP_REQ_EXTENDED)) {
				/* no cookie for you. */
				LDAPDebug(LDAP_DEBUG_ANY, "Attempted operation %d "
						  "from within bulk import\n",
						  tag, 0, 0);
				slapi_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
									   NULL, 0, NULL);
				goto done;
			}
		}

		/*
		 * Call the do_<operation> function to process this request.
		 */
		connection_dispatch_operation(conn, op, pb);

done:	
		/*
		 * done with this operation. delete it from the op
		 * queue for this connection, delete the number of
		 * threads devoted to this connection, and see if
		 * there's more work to do right now on this conn.
		 */

		/* number of ops on this connection */
		PR_AtomicIncrement(&conn->c_opscompleted);
		/* total number of ops for the server */
		slapi_counter_increment(ops_completed);
		/* If this op isn't a persistent search, remove it */
		if ( !( pb->pb_op->o_flags & OP_FLAG_PS )) {
		    /* delete from connection operation queue & decr refcnt */
		    PR_Lock( conn->c_mutex );
		    connection_remove_operation( conn, op );
			/* destroying the pblock will cause destruction of the operation
			 * so this must happend before releasing the connection
			 */
		    slapi_pblock_destroy( pb );

			/* If we're in turbo mode, we keep our reference to the connection 
			   alive */
		    if (!thread_turbo_flag && !more_data) {
				connection_release_nolock (conn);
			}
		    PR_Unlock( conn->c_mutex );
		} else { /* the ps code acquires a ref to the conn - we need to release ours here */
		    PR_Lock( conn->c_mutex );
			connection_release_nolock (conn);
		    PR_Unlock( conn->c_mutex );
		}
		/* Since we didn't do so earlier, we need to make a replication connection readable again here */
		if ( ((1 == is_timedout) || (replication_connection && !thread_turbo_flag)) && !more_data)
			connection_make_readable(conn);
		pb = NULL;
		if (doshutdown) {
			return;
		}

		if (!thread_turbo_flag && !more_data) { /* Don't do this in turbo mode */
			PR_Lock( conn->c_mutex );
			/* if the threadnumber of now below the maximum, wakeup
			 * the listener thread so that we start polling on this 
			 * connection again
			 */
			/* DBDB I think this code is bogus -- we already signaled the listener above here */
			if (conn->c_threadnumber == config_get_maxthreadsperconn())
				need_wakeup = 1;
			else
				need_wakeup = 0;
			conn->c_threadnumber--;
			PR_Unlock( conn->c_mutex );
	
			if (need_wakeup)
				signal_listner();
		}
		
		
	} /* while (1) */
}

/* thread need to hold conn->c_mutex before calling this function */
int
connection_activity(Connection *conn)
{
	Slapi_PBlock	*pb;

	connection_make_new_pb(&pb, conn);
	
	/* Add pb to the end of the work queue.  */
	add_pb( pb );

	/* Check if exceed the max thread per connection.  If so, increment 
	   c_pbwait.  Otherwise increment the counter and notify the cond. var. 
	   there is work to do.   */

	if (connection_acquire_nolock (conn) == -1) {
	    LDAPDebug(LDAP_DEBUG_CONNS,
		      "could not acquire lock in connection_activity as conn %" NSPRIu64 " closing fd=%d\n",
		      conn->c_connid,conn->c_sd,0); 
	    /* XXX how to handle this error? */
	    /* MAB: 25 Jan 01: let's return on error and pray this won't leak */
	    return (-1);
	}
	conn->c_gettingber = 1;
	conn->c_threadnumber++;
    PR_Lock( op_thread_lock );
    counter++;
    PR_NotifyCondVar( op_thread_cv );
    PR_Unlock( op_thread_lock );
	
	if (! config_check_referral_mode()) {
	    slapi_counter_increment(ops_initiated);
	    slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsInOps); 
	}
	return 0;
}

/* add_pb():  will add a pb to the end of the global work queue. The work queue
	is implemented as a singal link list. */

static void
add_pb( Slapi_PBlock *pb)
{

	struct Slapi_PBlock_q	*new_pb=NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "add_pb \n", 0,  0, 0 );

	new_pb = (struct Slapi_PBlock_q *) slapi_ch_malloc ( sizeof( struct Slapi_PBlock_q ));
	new_pb->pb = pb;
	new_pb->next_pb =NULL;

	PR_Lock( pb_q_lock );
	if (last_pb == NULL) {
		last_pb = new_pb;
		first_pb = new_pb;
	}
	else {
		last_pb->next_pb = new_pb;
		last_pb = new_pb;
	}
	PR_Unlock( pb_q_lock );
}

/* get_pb(): will get a pb from the begining of the work queue, return NULL if 
	the queue is empty.*/

static Slapi_PBlock *
get_pb()
{

	struct Slapi_PBlock_q  *tmp = NULL;
	Slapi_PBlock *pb;

	LDAPDebug( LDAP_DEBUG_TRACE, "get_pb \n", 0,  0, 0 );
	PR_Lock( pb_q_lock );
	if (first_pb == NULL) {
		PR_Unlock( pb_q_lock );
		LDAPDebug( LDAP_DEBUG_ANY, "get_pb: the work queue is empty.\n",
			 0,  0, 0 );
		return NULL;
	}

	tmp = first_pb;
	if ( first_pb == last_pb ) {
		last_pb = NULL;
	}
	first_pb = tmp->next_pb;
	PR_Unlock( pb_q_lock );

	pb = tmp->pb;
	/* Free the memory used by the pb found. */
	slapi_ch_free ((void **)&tmp);

	return (pb);
}
#endif /* LDAP_IOCP */


/* Helper functions common to both varieties of connection code: */

/* op_thread_cleanup() : This function is called by daemon thread when it gets 
	the slapd_shutdown signal.  It will set op_shutdown to 1 and notify 
	all thread waiting on op_thread_cv to terminate.  */

void
op_thread_cleanup()
{
#ifdef _WIN32
    int i;
    PRIntervalTime    interval;
    int max_threads = config_get_threadnumber();
    interval = PR_SecondsToInterval(3); 
#endif	
    LDAPDebug( LDAP_DEBUG_ANY, 
        "slapd shutting down - signaling operation threads\n", 0, 0, 0);
    
    PR_Lock( op_thread_lock );
    op_shutdown = 1;
    PR_NotifyAllCondVar ( op_thread_cv );
    PR_Unlock( op_thread_lock );
#ifdef _WIN32 
    LDAPDebug( LDAP_DEBUG_ANY,
              "slapd shutting down - waiting for %d threads to terminate\n",
              g_get_active_threadcnt(), 0, 0 );
    /* kill off each worker waiting on GetQueuedCompletionStatus */
    for ( i = 0; i < max_threads; ++ i )
    {
        PostQueuedCompletionStatus( completion_port, 0, COMPKEY_DIE ,0);
    }
    /* don't sleep: there's no reason to do so here DS_Sleep(interval); */ /* sleep 3 seconds */
#endif
}

static void
connection_add_operation(Connection* conn,Operation* op)
{
	Operation **olist= &conn->c_ops;
	int	id= conn->c_opsinitiated++;
	PRUint64 connid = conn->c_connid;
	Operation **tmp;

	/* slapi_ch_stop_recording(); */

	for ( tmp = olist; *tmp != NULL; tmp = &(*tmp)->o_next )
		;	/* NULL */

	*tmp= op;
	op->o_opid = id;
	op->o_connid = connid;
	/* Call the plugin extension constructors */
	op->o_extension = factory_create_extension(get_operation_object_type(),op,conn);
}

/*
 * Find an Operation on the Connection, and zap it in the butt.
 * Call this function with conn->c_mutex locked.
 */
void
connection_remove_operation( Connection *conn, Operation *op )
{
    Operation **olist= &conn->c_ops;
	Operation **tmp;

	for ( tmp = olist; *tmp != NULL && *tmp != op; tmp = &(*tmp)->o_next )
		;	/* NULL */

	if ( *tmp == NULL )
	{
		LDAPDebug( LDAP_DEBUG_ANY, "connection_remove_operation: can't find op %d for conn %" NSPRIu64 "\n",
		    (int)op->o_msgid, conn->c_connid, 0 );
	}
	else
	{
		*tmp = (*tmp)->o_next;
	}
}


/*
 * Return a non-zero value if any operations are pending on conn.
 * Operation op2ignore is ignored (okay to pass NULL). Typically, op2ignore
 *    is the caller's op (because the caller wants to check if all other
 *    ops are done).
 * If test_resultsent is non-zero, operations that have already sent
 *    a result to the client are ignored.
 * Call this function with conn->c_mutex locked.
 */
int
connection_operations_pending( Connection *conn, Operation *op2ignore,
		int test_resultsent )
{
	Operation	*op;

	PR_ASSERT( conn != NULL );

	for ( op = conn->c_ops; op != NULL; op = op->o_next ) {
		if ( op == op2ignore ) {
			continue;
		}
		if ( !test_resultsent || op->o_status != SLAPI_OP_STATUS_RESULT_SENT ) {
			break;
		}
	}

	return( op != NULL );
}


/* Copy the authorization identity from the connection struct into the 
 * operation struct.  We do this late, because an operation might start
 * before authentication is complete, at least on an SSL connection.
 * We want each operation to get its authorization identity after the
 * SSL software has had its chance to finish the SSL handshake;
 * that is, after the first few bytes of the request are received.
 * In particular, we want the first request from an LDAPS client
 * to have an authorization identity derived from the initial SSL
 * handshake.  We also copy the SSF at this time.
 */
static void 
op_copy_identity(Connection *conn, Operation *op)
{
    size_t dnlen;
    size_t typelen;

	PR_Lock( conn->c_mutex );
	dnlen= conn->c_dn ? strlen (conn->c_dn) : 0;
	typelen= conn->c_authtype ? strlen (conn->c_authtype) : 0;

	slapi_sdn_done(&op->o_sdn);
	slapi_ch_free_string(&(op->o_authtype));
    if (dnlen <= 0 && typelen <= 0) {
        op->o_authtype = NULL;
    } else {
	    slapi_sdn_set_dn_byval(&op->o_sdn,conn->c_dn);
        op->o_authtype = slapi_ch_strdup(conn->c_authtype);
        /* set the thread data bind dn index */
        slapi_td_set_dn(slapi_ch_strdup(conn->c_dn));
    }
    /* XXX We should also copy c_client_cert into *op here; it's
     * part of the authorization identity.  The operation's copy
     * (not c_client_cert) should be used for access control.
     */

    /* copy isroot flag as well so root DN privileges are preserved */
    op->o_isroot = conn->c_isroot;

    /* copy the highest SSF (between local, SASL, and SSL/TLS)
     * into the operation for use by access control. */
    if ((conn->c_sasl_ssf >= conn->c_ssl_ssf) && (conn->c_sasl_ssf >= conn->c_local_ssf)) {
        op->o_ssf = conn->c_sasl_ssf;
    } else if ((conn->c_ssl_ssf >= conn->c_sasl_ssf) && (conn->c_ssl_ssf >= conn->c_local_ssf)){
        op->o_ssf = conn->c_ssl_ssf;
    } else {
        op->o_ssf = conn->c_local_ssf;
    }

    PR_Unlock( conn->c_mutex );
}

/* Sets the SSL SSF in the connection struct. */
static void
connection_set_ssl_ssf(Connection *conn)
{
	PR_Lock( conn->c_mutex );

	if (conn->c_flags & CONN_FLAG_SSL) {
		SSL_SecurityStatus(conn->c_prfd, NULL, NULL, NULL, &(conn->c_ssl_ssf), NULL, NULL);
	} else {
		conn->c_ssl_ssf = 0;
	}

	PR_Unlock( conn->c_mutex );
}

static int
is_ber_too_big(const Connection *conn, ber_len_t ber_len)
{
    ber_len_t maxbersize= config_get_maxbersize();
    if(ber_len > maxbersize)
	{
		log_ber_too_big_error(conn, ber_len, maxbersize);
		return 1;
	}
	return 0;
}


/*
 * Pass 0 for maxbersize if you do not have it handy. It is also OK to pass
 * 0 for ber_len, in which case a slightly less informative message is
 * logged.
 */
static void
log_ber_too_big_error(const Connection *conn, ber_len_t ber_len,
		ber_len_t maxbersize)
{
	if (0 == maxbersize) {
		maxbersize= config_get_maxbersize();
	}
	if (0 == ber_len) {
		slapi_log_error( SLAPI_LOG_FATAL, "connection",
			"conn=%" NSPRIu64 " fd=%d Incoming BER Element was too long, max allowable"
			" is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
			" cn=config to increase.\n",
			conn->c_connid, conn->c_sd, maxbersize );
	} else {
		slapi_log_error( SLAPI_LOG_FATAL, "connection",
			"conn=%" NSPRIu64 " fd=%d Incoming BER Element was %" BERLEN_T " bytes, max allowable"
			" is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
			" cn=config to increase.\n",
			conn->c_connid, conn->c_sd, ber_len, maxbersize );
	}
}


void
disconnect_server( Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error )
{
	PR_Lock( conn->c_mutex );
	disconnect_server_nomutex( conn, opconnid, opid, reason, error );
	PR_Unlock( conn->c_mutex );
}

static ps_wakeup_all_fn_ptr ps_wakeup_all_fn = NULL;

/*
 * disconnect_server - close a connection. takes the connection to close,
 * the connid associated with the operation generating the close (so we
 * don't accidentally close a connection that's not ours), and the opid
 * of the operation generating the close (for logging purposes).
 */

void
disconnect_server_nomutex( Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error )
{
    if ( ( conn->c_sd != SLAPD_INVALID_SOCKET &&
	conn->c_connid == opconnid ) && !(conn->c_flags & CONN_FLAG_CLOSING) ) { 

	/*
	 * PR_Close must be called before anything else is done because
	 * of NSPR problem on NT which requires that the socket on which
	 * I/O timed out is closed before any other I/O operation is
	 * attempted by the thread.
	 * WARNING :  As of today the current code does not fulfill the
	 * requirements above.
	 */

	/* Mark that the socket should be closed on this connection.
	 * We don't want to actually close the socket here, because
	 * the listener thread could be PR_Polling over it right now.
	 * The last thread to stop using the connection will do the closing.
	 */
	conn->c_flags |= CONN_FLAG_CLOSING;
	g_decrement_current_conn_count();

	/*
	 * Print the error captured above.
	 */
	if (error && (EPIPE != error) ) {
	    slapi_log_access( LDAP_DEBUG_STATS,
		  "conn=%" NSPRIu64 " op=%d fd=%d closed error %d (%s) - %s\n",
		  conn->c_connid, opid, conn->c_sd, error,
		  slapd_system_strerror(error),
		  slapd_pr_strerror(reason));
	} else {
	    slapi_log_access( LDAP_DEBUG_STATS,
		  "conn=%" NSPRIu64 " op=%d fd=%d closed - %s\n",
		  conn->c_connid, opid, conn->c_sd,
		  slapd_pr_strerror(reason));
	}

	if (! config_check_referral_mode()) {
	    slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsConnections);
	}

	conn->c_gettingber = 0;
	connection_abandon_operations( conn );
	/* needed here to ensure simple paged results timeout properly and 
	 * don't impact subsequent ops */
	pagedresults_reset_timedout(conn);

	if (! config_check_referral_mode()) {
	    /*
	     * If any of the outstanding operations on this
	     * connection were persistent searches, then
	     * ding all the persistent searches to get them
	     * to notice that their operations have been abandoned.
	     */
	    int found_ps = 0;
	    Operation *o;

	    for ( o = conn->c_ops; !found_ps && o != NULL; o = o->o_next ) {
		if ( o->o_flags & OP_FLAG_PS ) {
		    found_ps = 1;
		}
	    }
	    if ( found_ps ) {
		if ( NULL == ps_wakeup_all_fn ) {
		    if ( get_entry_point( ENTRY_POINT_PS_WAKEUP_ALL,
			    (caddr_t *)(&ps_wakeup_all_fn )) == 0 ) {
			(ps_wakeup_all_fn)();
		    }
		} else {
		    (ps_wakeup_all_fn)();
		}
	    }
	}
    }
}

void
connection_abandon_operations( Connection *c )
{
	Operation *op;
	for ( op = c->c_ops; op != NULL; op = op->o_next )
	{
		/* abandon the operation only if it is not yet
		   completed (i.e., no result has been sent yet to 
		   the client */
		if ( op->o_status != SLAPI_OP_STATUS_RESULT_SENT ) {
			op->o_status = SLAPI_OP_STATUS_ABANDONED;
		}
	}
}

/* must be called within c->c_mutex */
void
connection_set_io_layer_cb( Connection *c, Conn_IO_Layer_cb push_cb, Conn_IO_Layer_cb pop_cb, void *cb_data )
{
	c->c_push_io_layer_cb = push_cb;
	c->c_pop_io_layer_cb = pop_cb;
	c->c_io_layer_cb_data = cb_data;
}

/* must be called within c->c_mutex */
int
connection_call_io_layer_callbacks( Connection *c )
{
	int rv = 0;
	if (c->c_pop_io_layer_cb) {
		rv = (c->c_pop_io_layer_cb)(c, c->c_io_layer_cb_data);
		c->c_pop_io_layer_cb = NULL;
	}
	if (!rv && c->c_push_io_layer_cb) {
		rv = (c->c_push_io_layer_cb)(c, c->c_io_layer_cb_data);
		c->c_push_io_layer_cb = NULL;
	}
	c->c_io_layer_cb_data = NULL;

	return rv;
}

