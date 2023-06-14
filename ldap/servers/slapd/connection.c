/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <signal.h>
#include "slap.h"
#include "prcvar.h"
#include "prlog.h" /* for PR_ASSERT */
#include "fe.h"
#include <sasl/sasl.h>
#if defined(LINUX)
#include <netinet/tcp.h> /* for TCP_CORK */
#endif

typedef Connection work_q_item;
static void connection_threadmain(void *arg);
static void connection_add_operation(Connection *conn, Operation *op);
static void connection_free_private_buffer(Connection *conn);
static void op_copy_identity(Connection *conn, Operation *op);
static void connection_set_ssl_ssf(Connection *conn);
static int is_ber_too_big(const Connection *conn, ber_len_t ber_len);
static void log_ber_too_big_error(const Connection *conn,
                                  ber_len_t ber_len,
                                  ber_len_t maxbersize);

static PRStack *op_stack;     /* stack of Slapi_Operation * objects so we don't have to malloc/free every time */
static PRInt32 op_stack_size; /* size of op_stack */

struct Slapi_op_stack
{
    PRStackElem stackelem; /* must be first in struct for PRStack to work */
    Slapi_Operation *op;
};

static void add_work_q(work_q_item *, struct Slapi_op_stack *);
static work_q_item *get_work_q(struct Slapi_op_stack **);

/*
 * We maintain a global work queue of items that have not yet
 * been handed off to an operation thread.
 */
struct Slapi_work_q
{
    PRStackElem stackelem; /* must be first in struct for PRStack to work */
    work_q_item *work_item;
    struct Slapi_op_stack *op_stack_obj;
    struct Slapi_work_q *next_work_item;
};

static struct Slapi_work_q *head_work_q = NULL; /* global work queue head */
static struct Slapi_work_q *tail_work_q = NULL; /* global work queue tail */
static pthread_mutex_t work_q_lock;             /* protects head_conn_q and tail_conn_q */
static pthread_cond_t work_q_cv;                /* used by operation threads to wait for work -
                                                 * when there is a conn in the queue waiting
                                                 * to be processed */
static PRInt32 work_q_size;                     /* size of conn_q */
static PRInt32 work_q_size_max;                 /* high water mark of work_q_size */
#define WORK_Q_EMPTY (work_q_size == 0)
static PRStack *work_q_stack;         /* stack of work_q structs so we don't have to malloc/free every time */
static PRInt32 work_q_stack_size;     /* size of work_q_stack */
static PRInt32 work_q_stack_size_max; /* max size of work_q_stack */
static PRInt32 op_shutdown = 0;       /* if non-zero, server is shutting down */

#define LDAP_SOCKET_IO_BUFFER_SIZE 512 /* Size of the buffer we give to the I/O system for reads */

static struct Slapi_work_q *
create_work_q(void)
{
    struct Slapi_work_q *work_q = (struct Slapi_work_q *)PR_StackPop(work_q_stack);
    if (!work_q) {
        work_q = (struct Slapi_work_q *)slapi_ch_malloc(sizeof(struct Slapi_work_q));
    } else {
        PR_AtomicDecrement(&work_q_stack_size);
    }
    return work_q;
}

static void
destroy_work_q(struct Slapi_work_q **work_q)
{
    if (work_q && *work_q) {
        (*work_q)->op_stack_obj = NULL;
        (*work_q)->work_item = NULL;
        PR_StackPush(work_q_stack, (PRStackElem *)*work_q);
        PR_AtomicIncrement(&work_q_stack_size);
        if (work_q_stack_size > work_q_stack_size_max) {
            work_q_stack_size_max = work_q_stack_size;
        }
    }
}

static struct Slapi_op_stack *
connection_get_operation(void)
{
    struct Slapi_op_stack *stack_obj = (struct Slapi_op_stack *)PR_StackPop(op_stack);
    if (!stack_obj) {
        stack_obj = (struct Slapi_op_stack *)slapi_ch_calloc(1, sizeof(struct Slapi_op_stack));
        stack_obj->op = operation_new(plugin_build_operation_action_bitmap(0,
                                                                           plugin_get_server_plg()));
    } else {
        PR_AtomicDecrement(&op_stack_size);
        if (!stack_obj->op) {
            stack_obj->op = operation_new(plugin_build_operation_action_bitmap(0,
                                                                               plugin_get_server_plg()));
        } else {
            operation_init(stack_obj->op,
                           plugin_build_operation_action_bitmap(0, plugin_get_server_plg()));
        }
    }
    return stack_obj;
}

static void
connection_done_operation(Connection *conn, struct Slapi_op_stack *stack_obj)
{
    operation_done(&(stack_obj->op), conn);
    PR_StackPush(op_stack, (PRStackElem *)stack_obj);
    PR_AtomicIncrement(&op_stack_size);
}

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
    slapi_ch_free((void **)&conn->c_private);
    pthread_mutex_destroy(&(conn->c_mutex));
    if (NULL != conn->c_sb) {
        ber_sockbuf_free(conn->c_sb);
    }
    if (NULL != conn->c_pdumutex) {
        PR_DestroyLock(conn->c_pdumutex);
    }
    /* PAGED_RESULTS */
    pagedresults_cleanup_all(conn, 0);

    /* Finally, flag that we are clean - basically write a 0 ...*/
    conn->c_state = CONN_STATE_FREE;
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
    bind_credentials_clear(conn, PR_FALSE /* do not lock conn */,
                           PR_TRUE /* clear external creds. */);
    slapi_ch_free((void **)&conn->c_authtype);

    /* Call the plugin extension destructors */
    factory_destroy_extension(connection_type, conn, NULL /*Parent*/, &(conn->c_extension));
    /*
     * We hang onto these, since we can reuse them.
     * Sockbuf *c_sb;
     * PRLock *c_mutex;
     * PRLock *c_pdumutex;
     * Conn_private *c_private;
     */
    if (conn->c_prfd) {
        PR_Close(conn->c_prfd);
    }

    conn->c_sd = SLAPD_INVALID_SOCKET;
    conn->c_ldapversion = 0;

    conn->c_isreplication_session = 0;
    slapi_ch_free((void **)&conn->cin_addr);
    slapi_ch_free((void **)&conn->cin_destaddr);
    slapi_ch_free((void **)&conn->cin_addr_aclip);
    slapi_ch_free_string(&conn->c_ipaddr);
    slapi_ch_free_string(&conn->c_serveripaddr);
    if (conn->c_domain != NULL) {
        ber_bvecfree(conn->c_domain);
        conn->c_domain = NULL;
    }
    /* conn->c_ops= NULL; */
    conn->c_gettingber = 0;
    conn->c_currentber = NULL;
    conn->c_starttime = 0;
    conn->c_connid = 0;
    conn->c_opsinitiated = 0;
    conn->c_opscompleted = 0;
    conn->c_anonlimits_set = 0;
    conn->c_threadnumber = 0;
    conn->c_refcnt = 0;
    conn->c_idlesince = 0;
    conn->c_flags = 0;
    conn->c_needpw = 0;
    conn->c_prfd = NULL;
    /* c_ci stays as it is */
    conn->c_fdi = SLAPD_INVALID_SOCKET_INDEX;
    conn->c_next = NULL;
    conn->c_prev = NULL;
    conn->c_extension = NULL;
    conn->c_ssl_ssf = 0;
    conn->c_local_ssf = 0;
    conn->c_unix_local = 0;
    /* destroy any sasl context */
    sasl_dispose((sasl_conn_t **)&conn->c_sasl_conn);
    /* PAGED_RESULTS */
    handle_closed_connection(conn); /* Clean up sockbufs */
    pagedresults_cleanup(conn, 0 /* do not need to lock inside */);

    /* free the connection socket buffer */
    connection_free_private_buffer(conn);
    /* even if !config_get_enable_nunc_stans, it is ok to set to 0 here */
    conn->c_ns_close_jobs = 0;
}

static char *
get_ip_str(struct sockaddr *addr, char *str)
{
    switch(addr->sa_family) {
        case AF_INET:
            if (sizeof(str) < INET_ADDRSTRLEN) {
                break;
            }
            inet_ntop(AF_INET, &(((struct sockaddr_in *)addr)->sin_addr), str, INET_ADDRSTRLEN);
            break;

        case AF_INET6:
            if (sizeof(str) < INET6_ADDRSTRLEN) {
                break;
            }
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)addr)->sin6_addr), str, INET6_ADDRSTRLEN);
            break;
    }

    return str;
}

/*
 * Callers of connection_reset() must hold the conn->c_mutex lock.
 */
void
connection_reset(Connection *conn, int ns, PRNetAddr *from, int fromLen __attribute__((unused)), int is_SSL)
{
    char *pTmp = is_SSL ? "SSL " : "";
    char *str_ip = NULL, *str_destip;
    char buf_ldapi[sizeof(from->local.path) + 1] = {0};
    char buf_destldapi[sizeof(from->local.path) + 1] = {0};
    char buf_ip[INET6_ADDRSTRLEN + 1] = {0};
    char buf_destip[INET6_ADDRSTRLEN + 1] = {0};
    char *str_unknown = "unknown";
    int in_referral_mode = config_check_referral_mode();

    slapi_log_err(SLAPI_LOG_CONNS, "connection_reset", "new %sconnection on %d\n", pTmp, conn->c_sd);

    /* bump our count of connections and update SNMP stats */
    conn->c_connid = slapi_counter_increment(num_conns);

    if (!in_referral_mode) {
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionSeq);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsConnections);
    }

    /*
     * get peer address (IP address of this client)
     */
    slapi_ch_free((void **)&conn->cin_addr); /* just to be conservative */
    if (from->raw.family == PR_AF_LOCAL) {   /* ldapi */
        conn->cin_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
        PL_strncpyz(buf_ldapi, from->local.path, sizeof(from->local.path));
        memcpy(conn->cin_addr, from, sizeof(PRNetAddr));
        if (!buf_ldapi[0]) {
            PR_GetPeerName(conn->c_prfd, from);
            PL_strncpyz(buf_ldapi, from->local.path, sizeof(from->local.path));
            memcpy(conn->cin_addr, from, sizeof(PRNetAddr));
            if (!buf_ldapi[0]) {
                /* Cannot derive local address, need something for logging */
                PL_strncpyz(buf_ldapi, "local", sizeof(buf_ldapi));
            }
        }
        str_ip = buf_ldapi;
    } else if (((from->ipv6.ip.pr_s6_addr32[0] != 0) || /* from contains non zeros */
                (from->ipv6.ip.pr_s6_addr32[1] != 0) ||
                (from->ipv6.ip.pr_s6_addr32[2] != 0) ||
                (from->ipv6.ip.pr_s6_addr32[3] != 0)) ||
               ((conn->c_prfd != NULL) && (PR_GetPeerName(conn->c_prfd, from) == 0)))
    {
        conn->cin_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
        memcpy(conn->cin_addr, from, sizeof(PRNetAddr));
        if (PR_IsNetAddrType(conn->cin_addr, PR_IpAddrV4Mapped)) {
            PRNetAddr v4addr = {{0}};
            v4addr.inet.family = PR_AF_INET;
            v4addr.inet.ip = conn->cin_addr->ipv6.ip.pr_s6_addr32[3];
            PR_NetAddrToString(&v4addr, buf_ip, sizeof(buf_ip));
        } else {
            PR_NetAddrToString(conn->cin_addr, buf_ip, sizeof(buf_ip));
        }
        buf_ip[sizeof(buf_ip) - 1] = '\0';
        str_ip = buf_ip;
    } else {
        /* try syscall since "from" was not given and PR_GetPeerName failed */
        /* a corner case */
        struct sockaddr addr = {0};
#if (defined(hpux))
        int addrlen;
#else
        socklen_t addrlen;
#endif

        addrlen = sizeof(addr);

        if ((conn->c_prfd == NULL) &&
            (getpeername(conn->c_sd, (struct sockaddr *)&addr, &addrlen) == 0))
        {
            conn->cin_addr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
            memset(conn->cin_addr, 0, sizeof(PRNetAddr));
            PR_NetAddrFamily(conn->cin_addr) = AF_INET6;
            /* note: IPv4-mapped IPv6 addr does not work on Windows */
            PR_ConvertIPv4AddrToIPv6(((struct sockaddr_in *)&addr)->sin_addr.s_addr, &(conn->cin_addr->ipv6.ip));
            PRLDAP_SET_PORT(conn->cin_addr, ((struct sockaddr_in *)&addr)->sin_port);
            str_ip = get_ip_str(&addr, buf_ip);
        } else {
            str_ip = str_unknown;
        }
    }

    /*
     * get destination address (server IP address this client connected to)
     */
    slapi_ch_free((void **)&conn->cin_destaddr); /* just to be conservative */
    if (conn->c_prfd != NULL) {
        conn->cin_destaddr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
        memset(conn->cin_destaddr, 0, sizeof(PRNetAddr));
        if (PR_GetSockName(conn->c_prfd, conn->cin_destaddr) == 0) {
            if (conn->cin_destaddr->raw.family == PR_AF_LOCAL) { /* ldapi */
                PL_strncpyz(buf_destldapi, conn->cin_destaddr->local.path,
                            sizeof(conn->cin_destaddr->local.path));
                if (!buf_destldapi[0]) {
                    PL_strncpyz(buf_destldapi, "unknown local file", sizeof(buf_destldapi));
                }
                str_destip = buf_destldapi;
            } else {
                if (PR_IsNetAddrType(conn->cin_destaddr, PR_IpAddrV4Mapped)) {
                    PRNetAddr v4destaddr = {{0}};
                    v4destaddr.inet.family = PR_AF_INET;
                    v4destaddr.inet.ip = conn->cin_destaddr->ipv6.ip.pr_s6_addr32[3];
                    PR_NetAddrToString(&v4destaddr, buf_destip, sizeof (buf_destip));
                } else {
                    PR_NetAddrToString(conn->cin_destaddr, buf_destip, sizeof (buf_destip));
                }
                buf_destip[sizeof (buf_destip) - 1] = '\0';
                str_destip = buf_destip;
            }
        } else {
            str_destip = str_unknown;
        }
    } else {
        /* try syscall since c_prfd == NULL */
        /* a corner case */
        struct sockaddr destaddr = {0}; /* assuming IPv4 */
#if (defined(hpux))
        int destaddrlen;
#else
        socklen_t destaddrlen;
#endif
        destaddrlen = sizeof(destaddr);

        if ((getsockname(conn->c_sd, &destaddr, &destaddrlen) == 0)) {
            conn->cin_destaddr = (PRNetAddr *)slapi_ch_malloc(sizeof(PRNetAddr));
            memset(conn->cin_destaddr, 0, sizeof(PRNetAddr));
            PR_NetAddrFamily(conn->cin_destaddr) = AF_INET6;
            PRLDAP_SET_PORT(conn->cin_destaddr, ((struct sockaddr_in *)&destaddr)->sin_port);
            /* note: IPv4-mapped IPv6 addr does not work on Windows */
            PR_ConvertIPv4AddrToIPv6(((struct sockaddr_in *)&destaddr)->sin_addr.s_addr, &(conn->cin_destaddr->ipv6.ip));
            str_destip = get_ip_str(&destaddr, buf_destip);
        } else {
            str_destip = str_unknown;
        }
    }
    slapi_ch_free((void **)&conn->cin_addr_aclip);

    if (!in_referral_mode) {
        /* create a sasl connection */
        ids_sasl_server_new(conn);
    }

    /* log useful stuff to our access log */
    slapi_log_access(LDAP_DEBUG_STATS,
                     "conn=%" PRIu64 " fd=%d slot=%d %sconnection from %s to %s\n",
                     conn->c_connid, conn->c_sd, ns, pTmp, str_ip, str_destip);

    /* initialize the remaining connection fields */
    conn->c_ldapversion = LDAP_VERSION3;
    conn->c_starttime = slapi_current_utc_time();  /* only used by the monitor */
    conn->c_idlesince = slapi_current_rel_time_t();
    conn->c_flags = is_SSL ? CONN_FLAG_SSL : 0;
    conn->c_authtype = slapi_ch_strdup(SLAPD_AUTH_NONE);
    /* Just initialize the SSL SSF to 0 now since the handshake isn't complete
     * yet, which prevents us from getting the effective key length. */
    conn->c_ssl_ssf = 0;
    conn->c_local_ssf = 0;
    conn->c_ipaddr = slapi_ch_strdup(str_ip);
    conn->c_serveripaddr = slapi_ch_strdup(str_destip);
}

/* Create a pool of threads for handling the operations */
void
init_op_threads()
{
    pthread_condattr_t condAttr;
    int32_t max_threads = config_get_threadnumber();
    int32_t rc;
    int32_t *threads_indexes;

    /* Initialize the locks and cv */
    if ((rc = pthread_mutex_init(&work_q_lock, NULL)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "init_op_threads",
                      "Cannot create new lock.  error %d (%s)\n",
                      rc, strerror(rc));
        exit(-1);
    }
    if ((rc = pthread_condattr_init(&condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "init_op_threads",
                      "Cannot create new condition attribute variable.  error %d (%s)\n",
                      rc, strerror(rc));
        exit(-1);
    } else if ((rc = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "init_op_threads",
                      "Cannot set condition attr clock.  error %d (%s)\n",
                      rc, strerror(rc));
        exit(-1);
    } else if ((rc = pthread_cond_init(&work_q_cv, &condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "init_op_threads",
                      "Cannot create new condition variable.  error %d (%s)\n",
                      rc, strerror(rc));
        exit(-1);
    }
    pthread_condattr_destroy(&condAttr); /* no longer needed */

    work_q_stack = PR_CreateStack("connection_work_q");
    op_stack = PR_CreateStack("connection_operation");
    alloc_per_thread_snmp_vars(max_threads);
    init_thread_private_snmp_vars();
    

    threads_indexes = (int32_t *) slapi_ch_calloc(max_threads, sizeof(int32_t));
    for (size_t i = 0; i < max_threads; i++) {
        threads_indexes[i] = i + 1; /* idx 0 is reserved for global snmp_vars */
    }
    
    /* start the operation threads */
    for (size_t i = 0; i < max_threads; i++) {
        PR_SetConcurrency(4);
        if (PR_CreateThread(PR_USER_THREAD,
                            (VFP)(void *)connection_threadmain, (void *) &threads_indexes[i],
                            PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                            PR_UNJOINABLE_THREAD,
                            SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL) {
            int prerr = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "init_op_threads",
                          "PR_CreateThread failed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                          prerr, slapd_pr_strerror(prerr));
        } else {
            g_incr_active_threadcnt();
        }
    }
    /* Here we should free thread_indexes, but because of the dynamic of the new
     * threads (connection_threadmain) we are not sure when it can be freed.
     * Let's accept that unique initialization leak (typically 32 to 64 bytes)
     */
}

static void
referral_mode_reply(Slapi_PBlock *pb)
{
    struct slapdplugin *plugin;
    plugin = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));
    if (plugin != NULL) {
        struct berval *urls[2], url;
        char *refer;
        refer = config_get_referral_mode();
        slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
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
    int r = 0;
    /*
        * add tag != LDAP_REQ_SEARCH to allow admin server 3.5 to do
     * searches when the user needs to reset
     * the pw the first time logon.
     * LP: 22 Dec 2000: Removing LDAP_REQ_SEARCH. It's very unlikely that AS 3.5 will
     * be used to manage DS5.0
     */

    if (conn->c_needpw && op->o_tag != LDAP_REQ_MODIFY &&
        op->o_tag != LDAP_REQ_BIND && op->o_tag != LDAP_REQ_UNBIND &&
        op->o_tag != LDAP_REQ_ABANDON && op->o_tag != LDAP_REQ_EXTENDED) {
        slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
        slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d %s\n",
                         conn->c_connid, op->o_opid,
                         "UNPROCESSED OPERATION - need new password");
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM,
                         NULL, NULL, 0, NULL);
        r = 1;
    }
    return r;
}


static void
connection_dispatch_operation(Connection *conn, Operation *op, Slapi_PBlock *pb)
{
    int32_t minssf = conn->c_minssf;
    int32_t minssf_exclude_rootdse = conn->c_minssf_exclude_rootdse;
#ifdef TCP_CORK
    int32_t enable_nagle = conn->c_enable_nagle;
    int32_t pop_cork = 0;
#endif

    /* Set the connid and op_id to be used by internal op logging */
    slapi_td_reset_internal_logging(conn->c_connid, op->o_opid);

    /* Get the effective key length now since the first SSL handshake should be complete */
    connection_set_ssl_ssf(conn);

    /* Copy the Connection DN and SSF into the operation struct */
    op_copy_identity(conn, op);

    if (slapi_operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
        /* If it is replicated op, ignore the maxbersize. */
        ber_len_t maxbersize = 0;
        ber_sockbuf_ctrl(conn->c_sb, LBER_SB_OPT_SET_MAX_INCOMING, &maxbersize);
    }

    /* Set the start time */
    slapi_operation_set_time_started(op);

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
    if (!minssf_exclude_rootdse &&
        (conn->c_sasl_ssf < minssf) && (conn->c_ssl_ssf < minssf) &&
        (conn->c_local_ssf < minssf) && (op->o_tag != LDAP_REQ_BIND) &&
        (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) &&
        (op->o_tag != LDAP_REQ_ABANDON)) {
        slapi_log_access(LDAP_DEBUG_STATS,
                         "conn=%" PRIu64 " op=%d UNPROCESSED OPERATION"
                         " - Insufficient SSF (local_ssf=%d sasl_ssf=%d ssl_ssf=%d)\n",
                         conn->c_connid, op->o_opid, conn->c_local_ssf,
                         conn->c_sasl_ssf, conn->c_ssl_ssf);
        send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                         "Minimum SSF not met.", 0, NULL);
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
    if ((slapi_sdn_get_dn(&(op->o_sdn)) == NULL) &&
        /* anon access off and something other than BIND, EXTOP, UNBIND or ABANDON */
        (((conn->c_anon_access == SLAPD_ANON_ACCESS_OFF) && (op->o_tag != LDAP_REQ_BIND) &&
          (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) && (op->o_tag != LDAP_REQ_ABANDON)) ||
         /* root DSE access only and something other than BIND, EXTOP, UNBIND, ABANDON, or SEARCH */
         ((conn->c_anon_access == SLAPD_ANON_ACCESS_ROOTDSE) && (op->o_tag != LDAP_REQ_BIND) &&
          (op->o_tag != LDAP_REQ_EXTENDED) && (op->o_tag != LDAP_REQ_UNBIND) &&
          (op->o_tag != LDAP_REQ_ABANDON) && (op->o_tag != LDAP_REQ_SEARCH)))) {
        slapi_log_access(LDAP_DEBUG_STATS,
                         "conn=%" PRIu64 " op=%d UNPROCESSED OPERATION"
                         " - Anonymous access not allowed\n",
                         conn->c_connid, op->o_opid);

        send_ldap_result(pb, LDAP_INAPPROPRIATE_AUTH, NULL,
                         "Anonymous access is not allowed.",
                         0, NULL);
        return;
    }

    /* process the operation */
    switch (op->o_tag) {
    case LDAP_REQ_BIND:
        operation_set_type(op, SLAPI_OPERATION_BIND);
        do_bind(pb);
        break;

    case LDAP_REQ_UNBIND:
        operation_set_type(op, SLAPI_OPERATION_UNBIND);
        do_unbind(pb);
        break;

    case LDAP_REQ_ADD:
        operation_set_type(op, SLAPI_OPERATION_ADD);
        do_add(pb);
        break;

    case LDAP_REQ_DELETE:
        operation_set_type(op, SLAPI_OPERATION_DELETE);
        do_delete(pb);
        break;

    case LDAP_REQ_MODRDN:
        operation_set_type(op, SLAPI_OPERATION_MODRDN);
        do_modrdn(pb);
        break;

    case LDAP_REQ_MODIFY:
        operation_set_type(op, SLAPI_OPERATION_MODIFY);
        do_modify(pb);
        break;

    case LDAP_REQ_COMPARE:
        operation_set_type(op, SLAPI_OPERATION_COMPARE);
        do_compare(pb);
        break;

    case LDAP_REQ_SEARCH:
        operation_set_type(op, SLAPI_OPERATION_SEARCH);

/* On Linux we can use TCP_CORK to get us 5-10% speed benefit when one entry is returned */
/* Nagle needs to be turned _on_, the default is _on_ on linux, in daemon.c */
#ifdef TCP_CORK
        if (enable_nagle && !conn->c_unix_local) {
            int i = 1;
            int ret = setsockopt(conn->c_sd, IPPROTO_TCP, TCP_CORK, &i, sizeof(i));
            if (ret < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "connection_dispatch_operation",
                              "Failed to set TCP_CORK on connection %" PRIu64 "\n", conn->c_connid);
            }
            pop_cork = 1;
        }
#endif
        do_search(pb);

#ifdef TCP_CORK
        if (pop_cork) {
            /* Clear TCP_CORK to flush any unsent data but only if not LDAPI*/
            int i = 0;
            int ret = setsockopt(conn->c_sd, IPPROTO_TCP, TCP_CORK, &i, sizeof(i));
            if (ret < 0) {
                slapi_log_err(SLAPI_LOG_ERR, "connection_dispatch_operation",
                              "Failed to clear TCP_CORK on connection %" PRIu64 "\n", conn->c_connid);
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
        operation_set_type(op, SLAPI_OPERATION_ABANDON);
        do_abandon(pb);
        break;

    case LDAP_REQ_EXTENDED:
        operation_set_type(op, SLAPI_OPERATION_EXTENDED);
        do_extended(pb);
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_dispatch_operation", "Ignoring unknown LDAP request (conn=%" PRIu64 ", tag=0x%lx)\n",
                      conn->c_connid, op->o_tag);
        break;
    }
}

/* this function should be called under c_mutex */
int
connection_release_nolock_ext(Connection *conn, int release_only)
{
    if (conn->c_refcnt <= 0) {
        slapi_log_err(SLAPI_LOG_ERR, "connection_release_nolock_ext",
                      "conn=%" PRIu64 " fd=%d Attempt to release connection that is not acquired\n",
                      conn->c_connid, conn->c_sd);
        PR_ASSERT(PR_FALSE);
        return -1;
    } else {
        conn->c_refcnt--;

        return 0;
    }
}

int
connection_release_nolock(Connection *conn)
{
    return connection_release_nolock_ext(conn, 0);
}

/* this function should be called under c_mutex */
int
connection_acquire_nolock_ext(Connection *conn, int allow_when_closing)
{
    /* connection in the closing state can't be acquired */
    if (!allow_when_closing && (conn->c_flags & CONN_FLAG_CLOSING)) {
        /* This may happen while other threads are still working on this connection */
        slapi_log_err(SLAPI_LOG_ERR, "connection_acquire_nolock_ext",
                      "conn=%" PRIu64 " fd=%d Attempt to acquire connection in the closing state\n",
                      conn->c_connid, conn->c_sd);
        return -1;
    } else {
        conn->c_refcnt++;
        return 0;
    }
}

int
connection_acquire_nolock(Connection *conn)
{
    return connection_acquire_nolock_ext(conn, 0);
}

/* returns non-0 if connection can be reused and 0 otherwise */
int
connection_is_free(Connection *conn, int use_lock)
{
    int rc = 0;

    if (use_lock) {
        /* If the lock is held, someone owns this! */
        if (pthread_mutex_trylock(&(conn->c_mutex)) != 0) {
            return 0;
        }
    }
    rc = conn->c_sd == SLAPD_INVALID_SOCKET && conn->c_refcnt == 0 &&
         !(conn->c_flags & CONN_FLAG_CLOSING);
    if (use_lock) {
        pthread_mutex_unlock(&(conn->c_mutex));
    }

    return rc;
}

int
connection_is_active_nolock(Connection *conn)
{
    return (conn->c_sd != SLAPD_INVALID_SOCKET) &&
           !(conn->c_flags & CONN_FLAG_CLOSING);
}

/* The connection private structure for UNIX turbo mode */
struct Conn_private
{
    int previous_op_count;            /* the operation counter value last time we sampled it, used to compute operation rate */
    int operation_rate;               /* rate (ops/sample period) at which this connection has been processing operations */
    time_t previous_count_check_time; /* The wall clock time we last sampled the operation count */
    size_t c_buffer_size;             /* size of the socket read buffer */
    char *c_buffer;                   /* pointer to the socket read buffer */
    size_t c_buffer_bytes;            /* number of bytes currently stored in the buffer */
    size_t c_buffer_offset;           /* offset to the location of new data in the buffer */
    int use_buffer;                   /* if true, use the buffer - if false, ber_get_next reads directly from socket */
};

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
    size_t max;    /* number of bytes currently stored in the buffer */
    size_t offset; /* offset to the location of new data in the buffer */

    PR_ASSERT(sbiod);
    PR_ASSERT(sbiod->sbiod_pvt);

    conn = (Connection *)sbiod->sbiod_pvt;

    if (CONNECTION_BUFFER_OFF == conn->c_private->use_buffer) {
        bytes_to_copy = PR_Recv(conn->c_prfd, buf, len, 0, PR_INTERVAL_NO_WAIT);
        goto done;
    }

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
        bytes_to_copy = 0; /* never return a negative result */
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
done:
    return bytes_to_copy;
}

int
connection_new_private(Connection *conn)
{
    if (NULL == conn->c_private) {
        Conn_private *new_private = (Conn_private *)slapi_ch_calloc(1, sizeof(Conn_private));
        if (NULL == new_private) {
            /* memory allocation failed */
            return -1;
        }
        conn->c_private = new_private;
        conn->c_private->use_buffer = config_get_connection_buffer();
    }

    /* The c_buffer is supposed to be NULL here, cleaned by connection_cleanup,
       double check to avoid memory leak */
    if ((CONNECTION_BUFFER_OFF != conn->c_private->use_buffer) && (NULL == conn->c_private->c_buffer)) {
        conn->c_private->c_buffer = (char *)slapi_ch_malloc(LDAP_SOCKET_IO_BUFFER_SIZE);
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
        char *c_buffer = conn->c_private->c_buffer;
        size_t c_buffer_size = conn->c_private->c_buffer_size;
        int use_buffer = conn->c_private->use_buffer;

        memset(conn->c_private, 0, sizeof(Conn_private));
        conn->c_private->c_buffer = c_buffer;
        conn->c_private->c_buffer_size = c_buffer_size;
        conn->c_private->use_buffer = use_buffer;
    }

    return 0;
}

static void
connection_free_private_buffer(Connection *conn)
{
    if (NULL != conn->c_private) {
        slapi_ch_free((void *)&(conn->c_private->c_buffer));
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
    connection_wait_for_new_work(), connection_read_operation(), etc. */

#define CONN_FOUND_WORK_TO_DO 0
#define CONN_SHUTDOWN 1
#define CONN_NOWORK 2
#define CONN_DONE 3
#define CONN_TIMEDOUT 4

#define CONN_TURBO_TIMEOUT_INTERVAL 100 /* milliseconds */
#define CONN_TURBO_TIMEOUT_MAXIMUM 5 /* attempts * interval IE 2000ms with 400 * 5 */
#define CONN_TURBO_CHECK_INTERVAL 5      /* seconds */
#define CONN_TURBO_PERCENTILE 50         /* proportion of threads allowed to be in turbo mode */
#define CONN_TURBO_HYSTERESIS 0          /* avoid flip flopping in and out of turbo mode */

void
connection_make_new_pb(Slapi_PBlock *pb, Connection *conn)
{
    struct Slapi_op_stack *stack_obj = NULL;
    /* we used to malloc/free the pb for each operation - now, just use a local stack pb
     * in connection_threadmain, and just clear it out
     */
    /* *ppb = (Slapi_PBlock *) slapi_ch_calloc( 1, sizeof(Slapi_PBlock) ); */
    /* *ppb = slapi_pblock_new(); */
    slapi_pblock_set(pb, SLAPI_CONNECTION, conn);
    stack_obj = connection_get_operation();
    slapi_pblock_set(pb, SLAPI_OPERATION, stack_obj->op);
    slapi_pblock_set_op_stack_elem(pb, stack_obj);
    connection_add_operation(conn, stack_obj->op);
}

int
connection_wait_for_new_work(Slapi_PBlock *pb, int32_t interval)
{
    int ret = CONN_FOUND_WORK_TO_DO;
    work_q_item *wqitem = NULL;
    struct Slapi_op_stack *op_stack_obj = NULL;

    pthread_mutex_lock(&work_q_lock);

    while (!op_shutdown && WORK_Q_EMPTY) {
        if (interval == 0 ) {
            pthread_cond_wait(&work_q_cv, &work_q_lock);
        } else {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_cond_timedwait(&work_q_cv, &work_q_lock, &current_time);
        }
    }

    if (op_shutdown) {
        slapi_log_err(SLAPI_LOG_TRACE, "connection_wait_for_new_work", "shutdown\n");
        ret = CONN_SHUTDOWN;
    } else if (NULL == (wqitem = get_work_q(&op_stack_obj))) {
        /* not sure how this can happen */
        slapi_log_err(SLAPI_LOG_TRACE, "connection_wait_for_new_work", "no work to do\n");
        ret = CONN_NOWORK;
    } else {
        /* make new pb */
        slapi_pblock_set(pb, SLAPI_CONNECTION, wqitem);
        slapi_pblock_set_op_stack_elem(pb, op_stack_obj);
        slapi_pblock_set(pb, SLAPI_OPERATION, op_stack_obj->op);
    }

    pthread_mutex_unlock(&work_q_lock);
    return ret;
}

#include "openldapber.h"

static ber_tag_t
_ber_get_len(BerElement *ber, ber_len_t *lenp)
{
    OLBerElement *lber = (OLBerElement *)ber;

    if (NULL == lenp) {
        return LBER_DEFAULT;
    }
    *lenp = 0;
    if (NULL == lber) {
        return LBER_DEFAULT;
    }
    *lenp = lber->ber_len;
    return lber->ber_tag;
}

/*
 * Utility function called by  connection_read_operation(). This is a
 * small wrapper on top of libldap's ber_get_next_buffer_ext().
 *
 * Caller must hold conn->c_mutex
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
get_next_from_buffer(void *buffer __attribute__((unused)), size_t buffer_size __attribute__((unused)), ber_len_t *lenp, ber_tag_t *tagp, BerElement *ber, Connection *conn)
{
    PRErrorCode err = 0;
    PRInt32 syserr = 0;
    ber_len_t bytes_scanned = 0;

    *lenp = 0;
    *tagp = ber_get_next(conn->c_sb, &bytes_scanned, ber);
    /* openldap ber_get_next doesn't return partial bytes_scanned if it hasn't
       read a whole pdu - so we have to check the errno for the
       "would block" condition meaning openldap needs more data to read */
    if ((LBER_OVERFLOW == *tagp || LBER_DEFAULT == *tagp) && 0 == bytes_scanned &&
        !SLAPD_SYSTEM_WOULD_BLOCK_ERROR(errno)) {
        if ((LBER_OVERFLOW == *tagp) || (errno == ERANGE)) {
            ber_len_t maxbersize = conn->c_maxbersize;
            ber_len_t tmplen = 0;
            (void)_ber_get_len(ber, &tmplen);
            /* openldap does not differentiate between length == 0
               and length > max - all we know is that there was a
               problem with the length - assume too big */
            err = SLAPD_DISCONNECT_BER_TOO_BIG;
            log_ber_too_big_error(conn, tmplen, maxbersize);
        } else {
            err = SLAPD_DISCONNECT_BAD_BER_TAG;
        }
        syserr = errno;
        /* Bad stuff happened, like the client sent us some junk */
        slapi_log_err(SLAPI_LOG_CONNS, "get_next_from_buffer",
                      "ber_get_next failed for connection %" PRIu64 "\n", conn->c_connid);
        /* reset private buffer */
        conn->c_private->c_buffer_bytes = conn->c_private->c_buffer_offset = 0;

        /* drop connection */
        disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
        return -1;
    } else if (CONNECTION_BUFFER_OFF == conn->c_private->use_buffer) {
        *lenp = bytes_scanned;
        if ((LBER_OVERFLOW == *tagp || LBER_DEFAULT == *tagp) && 0 == bytes_scanned &&
            SLAPD_SYSTEM_WOULD_BLOCK_ERROR(errno)) {
            return -2; /* tells connection_read_operation we need to try again */
        }
    } /* else, openldap_read_function will advance c_buffer_offset,
         nothing to do (we had to previously with mozldap) */
    return 0;
}

/* Either read read data into the connection buffer, or fail with err set */
static int
connection_read_ldap_data(Connection *conn, PRInt32 *err)
{
    int ret = 0;
    ret = PR_Recv(conn->c_prfd, conn->c_private->c_buffer, conn->c_private->c_buffer_size, 0, PR_INTERVAL_NO_WAIT);
    if (ret < 0) {
        *err = PR_GetError();
    } else if (CONNECTION_BUFFER_ADAPT == conn->c_private->use_buffer) {
        if ((ret == conn->c_private->c_buffer_size) && (conn->c_private->c_buffer_size < BUFSIZ)) {
            /* we read exactly what we requested - there could be more that we could have read */
            /* so increase the buffer size */
            conn->c_private->c_buffer_size *= 2;
            if (conn->c_private->c_buffer_size > BUFSIZ) {
                conn->c_private->c_buffer_size = BUFSIZ;
            }
            conn->c_private->c_buffer = slapi_ch_realloc(conn->c_private->c_buffer, conn->c_private->c_buffer_size);
        }
    }
    return ret;
}

static size_t
conn_buffered_data_avail_nolock(Connection *conn, int *conn_closed)
{
    if ((conn->c_sd == SLAPD_INVALID_SOCKET) || (conn->c_flags & CONN_FLAG_CLOSING)) {
        /* connection is closed - ignore the buffer */
        *conn_closed = 1;
        return 0;
    } else {
        *conn_closed = 0;
        return conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset;
    }
}

/* Upon returning from this function, we have either:
   1. Read a PDU successfully.
   2. Detected some error condition with the connection which requires closing it.
   3. In Turbo mode, we Timed out without seeing any data.

   We also handle the case where we read ahead beyond the current PDU
   by buffering the data and setting the 'remaining_data' flag.

 */
int
connection_read_operation(Connection *conn, Operation *op, ber_tag_t *tag, int *remaining_data)
{
    ber_len_t len = 0;
    int ret = 0;
    int32_t waits_done = 0;
    ber_int_t msgid;
    int new_operation = 1; /* Are we doing the first I/O read for a new operation ? */
    char *buffer = conn->c_private->c_buffer;
    PRErrorCode err = 0;
    PRInt32 syserr = 0;
    size_t buffer_data_avail;
    int conn_closed = 0;

    pthread_mutex_lock(&(conn->c_mutex));
    /*
     * if the socket is still valid, get the ber element
     * waiting for us on this connection. timeout is handled
     * in the low-level read_function.
     */
    if ((conn->c_sd == SLAPD_INVALID_SOCKET) ||
        (conn->c_flags & CONN_FLAG_CLOSING)) {
        ret = CONN_DONE;
        goto done;
    }

    *tag = LBER_DEFAULT;
    /* First check to see if we have buffered data from "before" */
    if ((buffer_data_avail = conn_buffered_data_avail_nolock(conn, &conn_closed))) {
        /* If so, use that data first */
        if (0 != get_next_from_buffer(buffer + conn->c_private->c_buffer_offset,
                                      buffer_data_avail,
                                      &len, tag, op->o_ber, conn)) {
            ret = CONN_DONE;
            goto done;
        }
        new_operation = 0;
    }
    /* If we still haven't seen a complete PDU, read from the network */
    while (*tag == LBER_DEFAULT) {
        int32_t ioblocktimeout_waits = conn->c_ioblocktimeout / CONN_TURBO_TIMEOUT_INTERVAL;
        /* We should never get here with data remaining in the buffer */
        PR_ASSERT(!new_operation || !conn_buffered_data_avail_nolock(conn, &conn_closed));
        /* We make a non-blocking read call */
        if (CONNECTION_BUFFER_OFF != conn->c_private->use_buffer) {
            ret = connection_read_ldap_data(conn, &err);
        } else {
            ret = get_next_from_buffer(NULL, 0, &len, tag, op->o_ber, conn);
            if (ret == -1) {
                ret = CONN_DONE;
                goto done; /* get_next_from_buffer does the disconnect stuff */
            } else if (ret == 0) {
                ret = len;
            }
            *remaining_data = 0;
        }
        if (ret <= 0) {
            if (0 == ret) {
                /* Connection is closed */
                disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, 0);
                conn->c_gettingber = 0;
                signal_listner(conn->c_ct_list);
                ret = CONN_DONE;
                goto done;
            }
            /* err = PR_GetError(); */
            /* If we would block, we need to poll for a while */
            syserr = PR_GetOSError();
            if (SLAPD_PR_WOULD_BLOCK_ERROR(err) ||
                SLAPD_SYSTEM_WOULD_BLOCK_ERROR(syserr)) {
                struct PRPollDesc pr_pd;
                PRIntervalTime timeout = PR_MillisecondsToInterval(CONN_TURBO_TIMEOUT_INTERVAL);
                pr_pd.fd = (PRFileDesc *)conn->c_prfd;
                pr_pd.in_flags = PR_POLL_READ;
                pr_pd.out_flags = 0;
                PR_Lock(conn->c_pdumutex);
                ret = PR_Poll(&pr_pd, 1, timeout);
                PR_Unlock(conn->c_pdumutex);
                waits_done++;
                /* Did we time out ? */
                if (0 == ret) {
                    /* We timed out, should the server shutdown ? */
                    if (op_shutdown) {
                        ret = CONN_SHUTDOWN;
                        goto done;
                    }
                    /* We timed out, is this the first read in a PDU ? */
                    if (new_operation) {
                        /* If so, we return */
                        ret = CONN_TIMEDOUT;
                        goto done;
                    } else {
                        /* Otherwise we loop, unless we exceeded the ioblock timeout */
                        if (waits_done > ioblocktimeout_waits) {
                            slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation",
                                          "ioblocktimeout expired on connection %" PRIu64 "\n", conn->c_connid);
                            disconnect_server_nomutex(conn, conn->c_connid, -1,
                                                      SLAPD_DISCONNECT_IO_TIMEOUT, 0);
                            ret = CONN_DONE;
                            goto done;
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
                    slapi_log_err(SLAPI_LOG_ERR, "connection_read_operation",
                                  "PR_Poll for connection %" PRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror(err));
                    /* If this happens we should close the connection */
                    disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
                    ret = CONN_DONE;
                    goto done;
                }
                slapi_log_err(SLAPI_LOG_CONNS,
                              "connection_read_operation", "connection %" PRIu64 " waited %d times for read to be ready\n", conn->c_connid, waits_done);
            } else {
                /* Some other error, typically meaning bad stuff */
                syserr = PR_GetOSError();
                slapi_log_err(SLAPI_LOG_CONNS, "connection_read_operation",
                              "PR_Recv for connection %" PRIu64 " returns %d (%s)\n", conn->c_connid, err, slapd_pr_strerror(err));
                /* If this happens we should close the connection */
                disconnect_server_nomutex(conn, conn->c_connid, -1, err, syserr);
                ret = CONN_DONE;
                goto done;
            }
        } else {
            /* We read some data off the network, do something with it */
            if (CONNECTION_BUFFER_OFF != conn->c_private->use_buffer) {
                conn->c_private->c_buffer_bytes = ret;
                conn->c_private->c_buffer_offset = 0;

                if (get_next_from_buffer(buffer,
                                         conn->c_private->c_buffer_bytes - conn->c_private->c_buffer_offset,
                                         &len, tag, op->o_ber, conn) != 0) {
                    ret = CONN_DONE;
                    goto done;
                }
            }
            slapi_log_err(SLAPI_LOG_CONNS,
                          "connection_read_operation", "connection %" PRIu64 " read %d bytes\n", conn->c_connid, ret);

            new_operation = 0;
            ret = CONN_FOUND_WORK_TO_DO;
            waits_done = 0; /* got some data: reset counter */
        }
    }
    /* If there is remaining buffered data, set the flag to tell the caller */
    if (conn_buffered_data_avail_nolock(conn, &conn_closed)) {
        *remaining_data = 1;
    } else if (conn_closed) {
        /* connection closed */
        ret = CONN_DONE;
        goto done;
    }

    if (*tag != LDAP_TAG_MESSAGE) {
        /*
         * We received a non-LDAP message.  Log and close connection.
         */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " received a non-LDAP message (tag 0x%lx, expected 0x%lx)\n",
                      conn->c_connid, *tag, LDAP_TAG_MESSAGE);
        disconnect_server_nomutex(conn, conn->c_connid, -1,
                                  SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO);
        ret = CONN_DONE;
        goto done;
    }

    if ((*tag = ber_get_int(op->o_ber, &msgid)) != LDAP_TAG_MSGID) {
        /* log, close and send error */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " unable to read tag for incoming request\n", conn->c_connid);
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BAD_BER_TAG, EPROTO);
        ret = CONN_DONE;
        goto done;
    }
    if (is_ber_too_big(conn, len)) {
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_TOO_BIG, 0);
        ret = CONN_DONE;
        goto done;
    }
    op->o_msgid = msgid;

    *tag = ber_peek_tag(op->o_ber, &len);
    switch (*tag) {
    case LBER_ERROR:
    case LDAP_TAG_LDAPDN: /* optional username, for CLDAP */
        /* log, close and send error */
        slapi_log_err(SLAPI_LOG_ERR,
                      "connection_read_operation", "conn=%" PRIu64 " ber_peek_tag returns 0x%lx\n", conn->c_connid, *tag);
        disconnect_server_nomutex(conn, conn->c_connid, -1, SLAPD_DISCONNECT_BER_PEEK, EPROTO);
        ret = CONN_DONE;
        goto done;
    default:
        break;
    }
    op->o_tag = *tag;
done:
    pthread_mutex_unlock(&(conn->c_mutex));
    return ret;
}

void
connection_make_readable(Connection *conn)
{
    pthread_mutex_lock(&(conn->c_mutex));
    conn->c_gettingber = 0;
    pthread_mutex_unlock(&(conn->c_mutex));
    signal_listner(conn->c_ct_list);
}

void
connection_make_readable_nolock(Connection *conn)
{
    conn->c_gettingber = 0;
    slapi_log_err(SLAPI_LOG_CONNS, "connection_make_readable_nolock", "making readable conn %" PRIu64 " fd=%d\n",
                  conn->c_connid, conn->c_sd);
}

/*
 * Figure out the operation completion rate for this connection
 */
void
connection_check_activity_level(Connection *conn)
{
    int current_count = 0;
    int delta_count = 0;
    pthread_mutex_lock(&(conn->c_mutex));
    /* get the current op count */
    current_count = conn->c_opscompleted;
    /* compare to the previous op count */
    delta_count = current_count - conn->c_private->previous_op_count;
    /* delta is the rate, store that */
    conn->c_private->operation_rate = delta_count;
    /* store current count in the previous count slot */
    conn->c_private->previous_op_count = current_count;
    /* update the last checked time */
    conn->c_private->previous_count_check_time = slapi_current_rel_time_t();
    pthread_mutex_unlock(&(conn->c_mutex));
    slapi_log_err(SLAPI_LOG_CONNS, "connection_check_activity_level", "conn %" PRIu64 " activity level = %d\n", conn->c_connid, delta_count);
}

typedef struct table_iterate_info_struct
{
    int connection_count;
    int rank_count;
    int our_rate;
} table_iterate_info;

int
table_iterate_function(Connection *conn, void *arg)
{
    int ret = 0;
    table_iterate_info *pinfo = (table_iterate_info *)arg;
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
void
connection_find_our_rank(Connection *conn, int *connection_count, int *our_rank)
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
void
connection_enter_leave_turbo(Connection *conn, int current_turbo_flag, int *new_turbo_flag)
{
    int current_mode = 0;
    int new_mode = 0;
    int connection_count = 0;
    int our_rank = 0;
    int threshold_rank = 0;
    pthread_mutex_lock(&(conn->c_mutex));
    /* We can already be in turbo mode, or not */
    current_mode = current_turbo_flag;
    if (pagedresults_in_use_nolock(conn)) {
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
        connection_find_our_rank(conn, &connection_count, &our_rank);
        slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo",
                      "conn %" PRIu64 " turbo rank = %d out of %d conns\n", conn->c_connid, our_rank, connection_count);
        activet = (double)g_get_active_threadcnt();
        threshold_rank = (int)(activet * ((double)CONN_TURBO_PERCENTILE / 100.0));

        /* adjust threshold_rank according number of connections,
         less turbo threads as more connections,
         one measure to reduce thread startvation.
       */
        if (connection_count > threshold_rank) {
            threshold_rank -= (connection_count - threshold_rank) / 5;
        }

        if (current_mode && (our_rank - CONN_TURBO_HYSTERESIS) < threshold_rank) {
            /* We're currently in turbo mode */
            /* Policy says that we stay in turbo mode provided
               connection activity is still high.
             */
            new_mode = 1;
        } else if (!current_mode && (our_rank + CONN_TURBO_HYSTERESIS) < threshold_rank) {
            /* We're currently not in turbo mode */
            /* Policy says that we go into turbo mode if
               recent connection activity is high.
             */
            new_mode = 1;
        }
    }
    pthread_mutex_unlock(&(conn->c_mutex));
    if (current_mode != new_mode) {
        if (current_mode) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo", "conn %" PRIu64 " leaving turbo mode\n", conn->c_connid);
        } else {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_enter_leave_turbo", "conn %" PRIu64 " entering turbo mode\n", conn->c_connid);
        }
    }
    *new_turbo_flag = new_mode;
}

static void
connection_threadmain(void *arg)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    int32_t *snmp_vars_idx = (int32_t *) arg;
    /* wait forever for new pb until one is available or shutdown */
    int32_t interval = 0; /* used be  10 seconds */
    Connection *conn = NULL;
    Operation *op;
    ber_tag_t tag = 0;
    int thread_turbo_flag = 0;
    int ret = 0;
    int more_data = 0;
    int replication_connection = 0; /* If this connection is from a replication supplier, we want to ensure that operation processing is serialized */
    int doshutdown = 0;
    int maxthreads = 0;
    long bypasspollcnt = 0;

#if defined(hpux)
    /* Arrange to ignore SIGPIPE signals. */
    SIGNAL(SIGPIPE, SIG_IGN);
#endif
    thread_private_snmp_vars_set_idx(*snmp_vars_idx);

    while (1) {
        int is_timedout = 0;
        time_t curtime = 0;

        if (op_shutdown) {
            slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                          "op_thread received shutdown signal\n");
            slapi_pblock_destroy(pb);
            g_decr_active_threadcnt();
            return;
        }

        if (!thread_turbo_flag && !more_data) {
	        Connection *pb_conn = NULL;

            /* If more data is left from the previous connection_read_operation,
               we should finish the op now.  Client might be thinking it's
               done sending the request and wait for the response forever.
               [blackflag 624234] */
            ret = connection_wait_for_new_work(pb, interval);

            switch (ret) {
            case CONN_NOWORK:
                PR_ASSERT(interval != 0); /* this should never happen */
                continue;
            case CONN_SHUTDOWN:
                slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                              "op_thread received shutdown signal\n");
                slapi_pblock_destroy(pb);
                g_decr_active_threadcnt();
                return;
            case CONN_FOUND_WORK_TO_DO:
                /* note - don't need to lock here - connection should only
                   be used by this thread - since c_gettingber is set to 1
                   in connection_activity when the conn is added to the
                   work queue, setup_pr_read_pds won't add the connection prfd
                   to the poll list */
                slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
                if (pb_conn == NULL) {
                    slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "pb_conn is NULL\n");
                    slapi_pblock_destroy(pb);
                    g_decr_active_threadcnt();
                    return;
                }

                pthread_mutex_lock(&(pb_conn->c_mutex));
                if (pb_conn->c_anonlimits_set == 0) {
                    /*
                     * We have a new connection, set the anonymous reslimit idletimeout
                     * if applicable.
                     */
                    char *anon_dn = config_get_anon_limits_dn();
                    int idletimeout;
                    /* If an anonymous limits dn is set, use it to set the limits. */
                    if (anon_dn && (strlen(anon_dn) > 0)) {
                        Slapi_DN *anon_sdn = slapi_sdn_new_normdn_byref(anon_dn);
                        reslimit_update_from_dn(pb_conn, anon_sdn);
                        slapi_sdn_free(&anon_sdn);
                        if (slapi_reslimit_get_integer_limit(pb_conn,
                                                             pb_conn->c_idletimeout_handle,
                                                             &idletimeout) == SLAPI_RESLIMIT_STATUS_SUCCESS) {
                            pb_conn->c_idletimeout = idletimeout;
                        }
                    }
                    slapi_ch_free_string(&anon_dn);
                    /*
                     * Set connection as initialized to avoid setting anonymous limits
                     * multiple times on the same connection
                     */
                    pb_conn->c_anonlimits_set = 1;
                }

                /* must hold c_mutex so that it synchronizes the IO layer push
                 * with a potential pending sasl bind that is registering the IO layer
                 */
                if (connection_call_io_layer_callbacks(pb_conn)) {
                    slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain",
                                  "Could not add/remove IO layers from connection\n");
                }
                pthread_mutex_unlock(&(pb_conn->c_mutex));
                break;
            default:
                break;
            }
        } else {

            /* The turbo mode may cause threads starvation.
               Do a yield here to reduce the starving
            */
            PR_Sleep(PR_INTERVAL_NO_WAIT);

            pthread_mutex_lock(&(conn->c_mutex));
            /* Make our own pb in turbo mode */
            connection_make_new_pb(pb, conn);
            if (connection_call_io_layer_callbacks(conn)) {
                slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain",
                              "Could not add/remove IO layers from connection\n");
            }
            pthread_mutex_unlock(&(conn->c_mutex));
            if (!config_check_referral_mode()) {
                slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpInitiated);
                slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsInOps);
            }
        }
        /* Once we're here we have a pb */
        slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        if (conn == NULL || op == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "NULL param: conn (0x%p) op (0x%p)\n", conn, op);
            slapi_pblock_destroy(pb);
            g_decr_active_threadcnt();
            return;
        }
        maxthreads = conn->c_max_threads_per_conn;
        more_data = 0;
        ret = connection_read_operation(conn, op, &tag, &more_data);
        if ((ret == CONN_DONE) || (ret == CONN_TIMEDOUT)) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " read not ready due to %d - thread_turbo_flag %d more_data %d "
                          "ops_initiated %d refcnt %d flags %d\n",
                          conn->c_connid, ret, thread_turbo_flag, more_data,
                          conn->c_opsinitiated, conn->c_refcnt, conn->c_flags);
        } else if (ret == CONN_FOUND_WORK_TO_DO) {
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " read operation successfully - thread_turbo_flag %d more_data %d "
                          "ops_initiated %d refcnt %d flags %d\n",
                          conn->c_connid, thread_turbo_flag, more_data,
                          conn->c_opsinitiated, conn->c_refcnt, conn->c_flags);
        }

        curtime = slapi_current_rel_time_t();
#define DB_PERF_TURBO 1
#if defined(DB_PERF_TURBO)
        /* If it's been a while since we last did it ... */
        if (curtime - conn->c_private->previous_count_check_time > CONN_TURBO_CHECK_INTERVAL) {
            if (config_get_enable_turbo_mode()) {
                int new_turbo_flag = 0;
                /* Check the connection's activity level */
                connection_check_activity_level(conn);
                /* And if appropriate, change into or out of turbo mode */
                connection_enter_leave_turbo(conn, thread_turbo_flag, &new_turbo_flag);
                thread_turbo_flag = new_turbo_flag;
            } else {
                thread_turbo_flag = 0;
            }
        }

        /* turn off turbo mode immediately if any pb waiting in global queue */
        if (thread_turbo_flag && !WORK_Q_EMPTY) {
            thread_turbo_flag = 0;
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " leaving turbo mode - pb_q is not empty %d\n",
                          conn->c_connid, work_q_size);
        }
#endif

        switch (ret) {
        case CONN_DONE:
        /* This means that the connection was closed, so clear turbo mode */
        /*FALLTHROUGH*/
        case CONN_TIMEDOUT:
            thread_turbo_flag = 0;
            is_timedout = 1;
            /* In the case of CONN_DONE, more_data could have been set to 1
                 * in connection_read_operation before an error was encountered.
                 * In that case, we need to set more_data to 0 - even if there is
                 * more data available, we're not going to use it anyway.
                 * In the case of CONN_TIMEDOUT, it is only used in one place, and
                 * more_data will never be set to 1, so it is safe to set it to 0 here.
                 * We need more_data to be 0 so the connection will be processed
                 * correctly at the end of this function.
                 */
            more_data = 0;
            /* note:
                 * should call connection_make_readable after the op is removed
                 * connection_make_readable(conn);
                 */
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain",
                          "conn %" PRIu64 " leaving turbo mode due to %d\n",
                          conn->c_connid, ret);
            goto done;
        case CONN_SHUTDOWN:
            slapi_log_err(SLAPI_LOG_TRACE, "connection_threadmain",
                          "op_thread received shutdown signal\n");
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
        if ((tag != LDAP_REQ_UNBIND) && !thread_turbo_flag && !replication_connection) {
            if (!more_data) {
                conn->c_flags &= ~CONN_FLAG_MAX_THREADS;
                pthread_mutex_lock(&(conn->c_mutex));
                connection_make_readable_nolock(conn);
                pthread_mutex_unlock(&(conn->c_mutex));
                /* once the connection is readable, another thread may access conn,
                 * so need locking from here on */
                signal_listner(conn->c_ct_list);
            } else { /* more data in conn - just put back on work_q - bypass poll */
                bypasspollcnt++;
                pthread_mutex_lock(&(conn->c_mutex));
                /* don't do this if it would put us over the max threads per conn */
                if (conn->c_threadnumber < maxthreads) {
                    /* for turbo, c_idlesince is set above - for !turbo and
                     * !more_data, we put the conn back in the poll loop and
                     * c_idlesince is set in handle_pr_read_ready - since we
                     * are bypassing both of those, we set idlesince here
                     */
                    conn->c_idlesince = curtime;
                    connection_activity(conn, maxthreads);
                    slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain", "conn %" PRIu64 " queued because more_data\n",
                                  conn->c_connid);
                } else {
                    /* keep count of how many times maxthreads has blocked an operation */
                    conn->c_maxthreadsblocked++;
                    if (conn->c_maxthreadsblocked == 1 && connection_has_psearch(conn)) {
                        slapi_log_err(SLAPI_LOG_NOTICE, "connection_threadmain",
                                "Connection (conn=%" PRIu64 ") has a running persistent search "
                                "that has exceeded the maximum allowed threads per connection. "
                                "New operations will be blocked.\n",
                                conn->c_connid);
                    }
                }
                pthread_mutex_unlock(&(conn->c_mutex));
            }
        }

        /* are we in referral-only mode? */
        if (config_check_referral_mode() && tag != LDAP_REQ_UNBIND) {
            referral_mode_reply(pb);
            goto done;
        }

        /* check if new password is required */
        if (connection_need_new_password(conn, op, pb)) {
            goto done;
        }

        /* if this is a bulk import, only "add" and "import done"
         * are allowed */
        if (conn->c_flags & CONN_FLAG_IMPORT) {
            if ((tag != LDAP_REQ_ADD) && (tag != LDAP_REQ_EXTENDED)) {
                /* no cookie for you. */
                slapi_log_err(SLAPI_LOG_ERR, "connection_threadmain", "Attempted operation %lu "
                                                                      "from within bulk import\n",
                              tag);
                slapi_send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL,
                                       NULL, 0, NULL);
                goto done;
            }
        }

        /*
         * Fix bz 1931820 issue (the check to set OP_FLAG_REPLICATED may be done
         * before replication session is properly set).
         */
        if (replication_connection) {
            operation_set_flag(op, OP_FLAG_REPLICATED);
        }

        /*
         * Call the do_<operation> function to process this request.
         */
        connection_dispatch_operation(conn, op, pb);

    done:
        if (doshutdown) {
            pthread_mutex_lock(&(conn->c_mutex));
            connection_remove_operation_ext(pb, conn, op);
            connection_make_readable_nolock(conn);
            conn->c_threadnumber--;
            slapi_counter_decrement(conns_in_maxthreads);
            slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
            connection_release_nolock(conn);
            pthread_mutex_unlock(&(conn->c_mutex));
            signal_listner(conn->c_ct_list);
            slapi_pblock_destroy(pb);
            return;
        }
        /*
         * done with this operation. delete it from the op
         * queue for this connection, delete the number of
         * threads devoted to this connection, and see if
         * there's more work to do right now on this conn.
         */

        /* number of ops on this connection */
        PR_AtomicIncrement(&conn->c_opscompleted);
        /* total number of ops for the server */
        slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpCompleted);
        /* If this op isn't a persistent search, remove it */
        if (op->o_flags & OP_FLAG_PS) {
            /* Release the connection (i.e. decrease refcnt) at the condition
             * this thread will not loop on it.
             * If we are in turbo mode (dedicated to that connection) or
             * more_data (continue reading buffered req) this thread
             * continues to hold the connection
             */
            if (!thread_turbo_flag && !more_data) {
                pthread_mutex_lock(&(conn->c_mutex));
                connection_release_nolock(conn); /* psearch acquires ref to conn - release this one now */
                pthread_mutex_unlock(&(conn->c_mutex));
            }
            /* ps_add makes a shallow copy of the pb - so we
             * can't free it or init it here - just set operation to NULL.
             * ps_send_results will call connection_remove_operation_ext to free it
             * The connection_thread private pblock ('pb') has be cloned and should only
             * be reinit (slapi_pblock_init)
             */
            slapi_pblock_set(pb, SLAPI_OPERATION, NULL);
            slapi_pblock_init(pb);
        } else {
            /* delete from connection operation queue & decr refcnt */
            int conn_closed = 0;
            pthread_mutex_lock(&(conn->c_mutex));
            connection_remove_operation_ext(pb, conn, op);

            /* If we're in turbo mode, we keep our reference to the connection alive */
            /* can't use the more_data var because connection could have changed in another thread */
            slapi_log_err(SLAPI_LOG_CONNS, "connection_threadmain", "conn %" PRIu64 " check more_data %d thread_turbo_flag %d"
                          "repl_conn_bef %d, repl_conn_now %d\n",
                          conn->c_connid, more_data, thread_turbo_flag,
                          replication_connection, conn->c_isreplication_session);
            if (!replication_connection &&  conn->c_isreplication_session) {
                /* it a connection that was just flagged as replication connection */
                more_data = 0;
            } else {
                /* normal connection or already established replication connection */
                more_data = conn_buffered_data_avail_nolock(conn, &conn_closed) ? 1 : 0;
            }
            if (!more_data) {
                if (!thread_turbo_flag) {
                    int32_t need_wakeup = 0;

                    /*
                     * Don't release the connection now.
                     * But note down what to do.
                     */
                    if (replication_connection || (1 == is_timedout)) {
                        connection_make_readable_nolock(conn);
                        need_wakeup = 1;
                    }
                    if (!need_wakeup) {
                        if (conn->c_threadnumber == maxthreads) {
                            need_wakeup = 1;
                        } else {
                            need_wakeup = 0;
                        }
                    }

                    if (conn->c_threadnumber == maxthreads) {
                        conn->c_flags &= ~CONN_FLAG_MAX_THREADS;
                        slapi_counter_decrement(conns_in_maxthreads);
                        slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
                    }
                    conn->c_threadnumber--;
                    connection_release_nolock(conn);
                    /* If need_wakeup, call signal_listner once.
                     * Need to release the connection (refcnt--)
                     * before that call.
                     */
                    if (need_wakeup) {
                        signal_listner(conn->c_ct_list);
                        need_wakeup = 0;
                    }
                }
            }
            pthread_mutex_unlock(&(conn->c_mutex));
        }
    } /* while (1) */
}

/* thread need to hold conn->c_mutex before calling this function */
int
connection_activity(Connection *conn, int maxthreads)
{
    struct Slapi_op_stack *op_stack_obj;

    if (connection_acquire_nolock(conn) == -1) {
        slapi_log_err(SLAPI_LOG_CONNS,
                      "connection_activity", "Could not acquire lock in connection_activity as conn %" PRIu64 " closing fd=%d\n",
                      conn->c_connid, conn->c_sd);
        /* XXX how to handle this error? */
        /* MAB: 25 Jan 01: let's return on error and pray this won't leak */
        return (-1);
    }

    /* set these here so setup_pr_read_pds will not add this conn back to the poll array */
    conn->c_gettingber = 1;
    conn->c_threadnumber++;
    if (conn->c_threadnumber == maxthreads) {
        conn->c_flags |= CONN_FLAG_MAX_THREADS;
        conn->c_maxthreadscount++;
        slapi_counter_increment(max_threads_count);
        slapi_counter_increment(conns_in_maxthreads);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsConnectionsInMaxThreads);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsMaxThreadsHits);
    }
    op_stack_obj = connection_get_operation();
    connection_add_operation(conn, op_stack_obj->op);
    /* Add conn to the end of the work queue.  */
    /* have to do this last - add_work_q will signal waiters in connection_wait_for_new_work */
    add_work_q((work_q_item *)conn, op_stack_obj);

    if (!config_check_referral_mode()) {
        slapi_counter_increment(g_get_per_thread_snmp_vars()->server_tbl.dsOpInitiated);
        slapi_counter_increment(g_get_per_thread_snmp_vars()->ops_tbl.dsInOps);
    }
    return 0;
}

/* add_work_q():  will add a work_q_item to the end of the global work queue. The work queue
    is implemented as a single link list. */

static void
add_work_q(work_q_item *wqitem, struct Slapi_op_stack *op_stack_obj)
{
    struct Slapi_work_q *new_work_q = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "add_work_q", "=>\n");

    new_work_q = create_work_q();
    new_work_q->work_item = wqitem;
    new_work_q->op_stack_obj = op_stack_obj;
    new_work_q->next_work_item = NULL;

    pthread_mutex_lock(&work_q_lock);
    if (tail_work_q == NULL) {
        tail_work_q = new_work_q;
        head_work_q = new_work_q;
    } else {
        tail_work_q->next_work_item = new_work_q;
        tail_work_q = new_work_q;
    }
    PR_AtomicIncrement(&work_q_size); /* increment q size */
    if (work_q_size > work_q_size_max) {
        work_q_size_max = work_q_size;
    }
    pthread_cond_signal(&work_q_cv); /* notify waiters in connection_wait_for_new_work */
    pthread_mutex_unlock(&work_q_lock);
}

/* get_work_q(): will get a work_q_item from the beginning of the work queue, return NULL if
    the queue is empty.  This should only be called from connection_wait_for_new_work
    with the work_q_lock held */

static work_q_item *
get_work_q(struct Slapi_op_stack **op_stack_obj)
{
    struct Slapi_work_q *tmp = NULL;
    work_q_item *wqitem;

    slapi_log_err(SLAPI_LOG_TRACE, "get_work_q", "=>\n");
    if (head_work_q == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "get_work_q", "The work queue is empty.\n");
        return NULL;
    }

    tmp = head_work_q;
    if (head_work_q == tail_work_q) {
        tail_work_q = NULL;
    }
    head_work_q = tmp->next_work_item;

    wqitem = tmp->work_item;
    *op_stack_obj = tmp->op_stack_obj;
    PR_AtomicDecrement(&work_q_size); /* decrement q size */
    /* Free the memory used by the item found. */
    destroy_work_q(&tmp);

    return (wqitem);
}

/* Helper functions common to both varieties of connection code: */

/* op_thread_cleanup() : This function is called by daemon thread when it gets
    the slapd_shutdown signal.  It will set op_shutdown to 1 and notify
    all thread waiting on op_thread_cv to terminate.  */

void
op_thread_cleanup()
{
    slapi_log_err(SLAPI_LOG_INFO, "op_thread_cleanup",
                  "slapd shutting down - signaling operation threads - op stack size %d max work q size %d max work q stack size %d\n",
                  op_stack_size, work_q_size_max, work_q_stack_size_max);

    PR_AtomicIncrement(&op_shutdown);
    pthread_mutex_lock(&work_q_lock);
    pthread_cond_broadcast(&work_q_cv); /* tell any thread waiting in connection_wait_for_new_work to shutdown */
    pthread_mutex_unlock(&work_q_lock);
}

/* do this after all worker threads have terminated */
void
connection_post_shutdown_cleanup()
{
    struct Slapi_op_stack *stack_obj;
    int stack_cnt = 0;
    struct Slapi_work_q *work_q;
    int work_cnt = 0;

    while ((work_q = (struct Slapi_work_q *)PR_StackPop(work_q_stack))) {
        Connection *conn = (Connection *)work_q->work_item;
        stack_obj = work_q->op_stack_obj;
        if (stack_obj) {
            if (conn) {
                connection_remove_operation(conn, stack_obj->op);
            }
            connection_done_operation(conn, stack_obj);
        }
        slapi_ch_free((void **)&work_q);
        work_cnt++;
    }
    PR_DestroyStack(work_q_stack);
    work_q_stack = NULL;
    while ((stack_obj = (struct Slapi_op_stack *)PR_StackPop(op_stack))) {
        operation_free(&stack_obj->op, NULL);
        slapi_ch_free((void **)&stack_obj);
        stack_cnt++;
    }
    PR_DestroyStack(op_stack);
    op_stack = NULL;
    slapi_log_err(SLAPI_LOG_INFO, "connection_post_shutdown_cleanup",
                  "slapd shutting down - freed %d work q stack objects - freed %d op stack objects\n",
                  work_cnt, stack_cnt);
}

static void
connection_add_operation(Connection *conn, Operation *op)
{
    Operation **olist = &conn->c_ops;
    int id = conn->c_opsinitiated++;
    PRUint64 connid = conn->c_connid;
    Operation **tmp;

    /* slapi_ch_stop_recording(); */

    for (tmp = olist; *tmp != NULL; tmp = &(*tmp)->o_next)
        ; /* NULL */

    *tmp = op;
    op->o_opid = id;
    op->o_connid = connid;
    /* Call the plugin extension constructors */
    op->o_extension = factory_create_extension(get_operation_object_type(), op, conn);
}

/*
 * Find an Operation on the Connection, and zap it in the butt.
 * Call this function with conn->c_mutex locked.
 */
void
connection_remove_operation(Connection *conn, Operation *op)
{
    Operation **olist = &conn->c_ops;
    Operation **tmp;

    for (tmp = olist; *tmp != NULL && *tmp != op; tmp = &(*tmp)->o_next)
        ; /* NULL */

    if (*tmp == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "connection_remove_operation", "Can't find op %d for conn %" PRIu64 "\n",
                      (int)op->o_msgid, conn->c_connid);
    } else {
        *tmp = (*tmp)->o_next;
    }
}

void
connection_remove_operation_ext(Slapi_PBlock *pb, Connection *conn, Operation *op)
{
    connection_remove_operation(conn, op);
    void *op_stack_elem = slapi_pblock_get_op_stack_elem(pb);
    connection_done_operation(conn, op_stack_elem);
    slapi_pblock_set(pb, SLAPI_OPERATION, NULL);
    slapi_pblock_init(pb);
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
connection_operations_pending(Connection *conn, Operation *op2ignore, int test_resultsent)
{
    Operation *op;

    PR_ASSERT(conn != NULL);

    for (op = conn->c_ops; op != NULL; op = op->o_next) {
        if (op == op2ignore) {
            continue;
        }
        if (!test_resultsent || op->o_status != SLAPI_OP_STATUS_RESULT_SENT) {
            break;
        }
    }

    return (op != NULL);
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

    pthread_mutex_lock(&(conn->c_mutex));
    dnlen = conn->c_dn ? strlen(conn->c_dn) : 0;
    typelen = conn->c_authtype ? strlen(conn->c_authtype) : 0;

    slapi_sdn_done(&op->o_sdn);
    slapi_ch_free_string(&(op->o_authtype));
    if (dnlen <= 0 && typelen <= 0) {
        op->o_authtype = NULL;
    } else {
        slapi_sdn_set_dn_byval(&op->o_sdn, conn->c_dn);
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
    } else if ((conn->c_ssl_ssf >= conn->c_sasl_ssf) && (conn->c_ssl_ssf >= conn->c_local_ssf)) {
        op->o_ssf = conn->c_ssl_ssf;
    } else {
        op->o_ssf = conn->c_local_ssf;
    }

    pthread_mutex_unlock(&(conn->c_mutex));
}

/* Sets the SSL SSF in the connection struct. */
static void
connection_set_ssl_ssf(Connection *conn)
{
    pthread_mutex_lock(&(conn->c_mutex));

    if (conn->c_flags & CONN_FLAG_SSL) {
        SSL_SecurityStatus(conn->c_prfd, NULL, NULL, NULL, &(conn->c_ssl_ssf), NULL, NULL);
    } else {
        conn->c_ssl_ssf = 0;
    }

    pthread_mutex_unlock(&(conn->c_mutex));
}

static int
is_ber_too_big(const Connection *conn, ber_len_t ber_len)
{
    if (ber_len > conn->c_maxbersize) {
        log_ber_too_big_error(conn, ber_len, conn->c_maxbersize);
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
log_ber_too_big_error(const Connection *conn, ber_len_t ber_len, ber_len_t maxbersize)
{
    if (0 == maxbersize) {
        maxbersize = conn->c_maxbersize;
    }
    if (0 == ber_len) {
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element was too long, max allowable"
                      " is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
                      " cn=config to increase.\n",
                      conn->c_connid, conn->c_sd, maxbersize);
    } else if (ber_len < maxbersize) {
        /* This means the request was misformed, not too large. */
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element may be misformed. "
                      "This may indicate an attempt to use TLS on a plaintext port, "
                      "IE ldaps://localhost:389. Check your client LDAP_URI settings.\n",
                      conn->c_connid, conn->c_sd);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "log_ber_too_big_error",
                      "conn=%" PRIu64 " fd=%d Incoming BER Element was %" BERLEN_T " bytes, max allowable"
                      " is %" BERLEN_T " bytes. Change the nsslapd-maxbersize attribute in"
                      " cn=config to increase.\n",
                      conn->c_connid, conn->c_sd, ber_len, maxbersize);
    }
}


void
disconnect_server(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error)
{
    pthread_mutex_lock(&(conn->c_mutex));
    disconnect_server_nomutex(conn, opconnid, opid, reason, error);
    pthread_mutex_unlock(&(conn->c_mutex));
}

static ps_wakeup_all_fn_ptr ps_wakeup_all_fn = NULL;

/*
 * disconnect_server - close a connection. takes the connection to close,
 * the connid associated with the operation generating the close (so we
 * don't accidentally close a connection that's not ours), and the opid
 * of the operation generating the close (for logging purposes).
 */

void
disconnect_server_nomutex_ext(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error, int schedule_closure_job)
{
    if ((conn->c_sd != SLAPD_INVALID_SOCKET &&
         conn->c_connid == opconnid) &&
        !(conn->c_flags & CONN_FLAG_CLOSING))
    {
        slapi_log_err(SLAPI_LOG_CONNS, "disconnect_server_nomutex_ext",
                "Setting conn %" PRIu64 " fd=%d to be disconnected: reason %d\n",
                conn->c_connid, conn->c_sd, reason);
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

        if(error) {
            slapi_log_access(LDAP_DEBUG_STATS,
                             "conn=%" PRIu64 " op=%d fd=%d Disconnect - %s - %s\n",
                             conn->c_connid, opid, conn->c_sd,
                             slapd_system_strerror(error),
                             slapd_pr_strerror(reason));
        } else {
            slapi_log_access(LDAP_DEBUG_STATS,
                             "conn=%" PRIu64 " op=%d fd=%d Disconnect - %s\n",
                             conn->c_connid, opid, conn->c_sd,
                             slapd_pr_strerror(reason));
        }
        slapi_log_security_tcp(conn, reason, slapd_pr_strerror(reason));

        if (!config_check_referral_mode()) {
            slapi_counter_decrement(g_get_per_thread_snmp_vars()->ops_tbl.dsConnections);
        }

        conn->c_gettingber = 0;
        connection_abandon_operations(conn);
        /* needed here to ensure simple paged results timeout properly and
         * don't impact subsequent ops */
        pagedresults_reset_timedout_nolock(conn);

        if (!config_check_referral_mode()) {
            /*
             * If any of the outstanding operations on this
             * connection were persistent searches, then
             * ding all the persistent searches to get them
             * to notice that their operations have been abandoned.
             */
            if (connection_has_psearch(conn)) {
                if (NULL == ps_wakeup_all_fn) {
                    if (get_entry_point(ENTRY_POINT_PS_WAKEUP_ALL,
                                        (caddr_t *)(&ps_wakeup_all_fn)) == 0) {
                        (ps_wakeup_all_fn)();
                    }
                } else {
                    (ps_wakeup_all_fn)();
                }
            }
        }

    } else {
        slapi_log_err(SLAPI_LOG_CONNS, "disconnect_server_nomutex_ext",
                "Not setting conn %d to be disconnected: %s\n",
                conn->c_sd,
                (conn->c_sd == SLAPD_INVALID_SOCKET) ? "socket is invalid" :
                        ((conn->c_connid != opconnid) ? "conn id does not match op conn id" :
                                    ((conn->c_flags & CONN_FLAG_CLOSING) ? "conn is closing" : "unknown")));
    }
}

void
disconnect_server_nomutex(Connection *conn, PRUint64 opconnid, int opid, PRErrorCode reason, PRInt32 error)
{
    disconnect_server_nomutex_ext(conn, opconnid, opid, reason, error, 1);
}

void
connection_abandon_operations(Connection *c)
{
    Operation *op;
    for (op = c->c_ops; op != NULL; op = op->o_next) {
        /* abandon the operation only if it is not yet
           completed (i.e., no result has been sent yet to
           the client */
        /* sync repl uses the persist mode, and it cannot prevent
         * setting o_status, but has to be abandonned
         * handle it here until a better solution is found
         */
        if (op->o_status != SLAPI_OP_STATUS_RESULT_SENT ||
            (op->o_flags & OP_FLAG_PS)) {
            op->o_status = SLAPI_OP_STATUS_ABANDONED;
        }
    }
}

/* must be called within c->c_mutex */
void
connection_set_io_layer_cb(Connection *c, Conn_IO_Layer_cb push_cb, Conn_IO_Layer_cb pop_cb, void *cb_data)
{
    c->c_push_io_layer_cb = push_cb;
    c->c_pop_io_layer_cb = pop_cb;
    c->c_io_layer_cb_data = cb_data;
}

/* must be called within c->c_mutex */
int
connection_call_io_layer_callbacks(Connection *c)
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

int32_t
connection_has_psearch(Connection *c)
{
    Operation *o;

    for (o = c->c_ops; o != NULL; o = o->o_next) {
        if (o->o_flags & OP_FLAG_PS) {
            return 1;
        }
    }

    return 0;
}
