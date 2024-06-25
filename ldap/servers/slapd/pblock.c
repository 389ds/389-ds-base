/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cert.h"
#include "pblock_v3.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef PBLOCK_ANALYTICS

#define NUMBER_SLAPI_ATTRS 320
#define ANALYTICS_MAGIC 0x7645

#define SLAPI_OP_STACK_ELEM 2001
#define SLAPI_VATTR_CONTEXT 2002
#define SLAPI_LDIF_DUMP_REPLICA 2003
#define SLAPI_PWDPOLICY 2004
#define SLAPI_PW_ENTRY 2005
#define SLAPI_TASK_WARNING 2006

/* Used for checking assertions about pblocks in some cases. */
#define SLAPI_HINT 9999

static PRLock *pblock_analytics_lock = NULL;


static PLHashNumber
hash_int_func(const void *key)
{
    uint64_t ik = (uint64_t)key;
    return ik % NUMBER_SLAPI_ATTRS;
}

static PRIntn
hash_key_compare(const void *a, const void *b)
{
    uint64_t ia = (uint64_t)a;
    uint64_t ib = (uint64_t)b;
    return ia == ib;
}

static PRIntn
hash_value_compare(const void *a, const void *b)
{
    uint64_t ia = (uint64_t)a;
    uint64_t ib = (uint64_t)b;
    return ia == ib;
}

static void
pblock_analytics_init(Slapi_PBlock *pb)
{
    if (pblock_analytics_lock == NULL) {
        pblock_analytics_lock = PR_NewLock();
    }
    /* Create an array of values for us to use. */
    if (pb->analytics == NULL) {
        pb->analytics = PL_NewHashTable(NUMBER_SLAPI_ATTRS, hash_int_func, hash_key_compare, hash_value_compare, NULL, NULL);
    }
    pb->analytics_init = ANALYTICS_MAGIC;
}

static void
pblock_analytics_destroy(Slapi_PBlock *pb)
{
    /* Some parts of DS re-use or double free pblocks >.< */
    if (pb->analytics_init != ANALYTICS_MAGIC) {
        return;
    }
    /* Free the array of values */
    PL_HashTableDestroy(pb->analytics);
    pb->analytics_init = 0;
}

static void
pblock_analytics_record(Slapi_PBlock *pb, int access_type)
{
    if (pb->analytics_init != ANALYTICS_MAGIC) {
        pblock_analytics_init(pb);
    }
    /* record an access of type to the values */
    /* Is the value there? */
    uint64_t uact = (uint64_t)access_type;
    uint64_t value = (uint64_t)PL_HashTableLookup(pb->analytics, (void *)uact);
    if (value == 0) {
        PL_HashTableAdd(pb->analytics, (void *)uact, (void *)1);
    } else {
        /* If not, increment it. */
        PL_HashTableAdd(pb->analytics, (void *)uact, (void *)(value + 1));
    }
}

static PRIntn
pblock_analytics_report_entry(PLHashEntry *he, PRIntn index, void *arg)
{
    FILE *fp = (FILE *)arg;
    /* Print a pair of values */
    fprintf(fp, "%" PRIu64 ":%" PRIu64 ",", (uint64_t)he->key, (uint64_t)he->value);

    return HT_ENUMERATE_NEXT;
}

static void
pblock_analytics_report(Slapi_PBlock *pb)
{
    /* Some parts of DS re-use or double free pblocks >.< */
    if (pb->analytics_init != ANALYTICS_MAGIC) {
        return;
    }
    /* Write the report to disk. */
    /* Take the write lock. */
    PR_Lock(pblock_analytics_lock);

    FILE *fp = NULL;
    fp = fopen("/tmp/pblock_stats.csv", "a");
    if (fp == NULL) {
        int errsv = errno;
        printf("%d\n", errsv);
        abort();
    }
    /* Map over the hashmap */
    PL_HashTableEnumerateEntries(pb->analytics, pblock_analytics_report_entry, fp);
    /* Printf the new line */
    fprintf(fp, "\n");
    fclose(fp);
    /* Unlock */
    PR_Unlock(pblock_analytics_lock);
}

uint64_t
pblock_analytics_query(Slapi_PBlock *pb, int access_type)
{
    uint64_t uact = (uint64_t)access_type;
    /* For testing, allow querying of the stats we have taken. */
    return (uint64_t)PL_HashTableLookup(pb->analytics, (void *)uact);
}

#endif

void
pblock_init(Slapi_PBlock *pb)
{
    memset(pb, '\0', sizeof(Slapi_PBlock));
}

void
pblock_init_common(
    Slapi_PBlock *pb,
    Slapi_Backend *be,
    Connection *conn,
    Operation *op)
{
    PR_ASSERT(NULL != pb);
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_init(pb);
#endif
    /* No need to memset, this is only called in backend_manager, and it uses {0} */
    pb->pb_backend = be;
    pb->pb_conn = conn;
    pb->pb_op = op;
}

Slapi_PBlock *
slapi_pblock_new()
{
    Slapi_PBlock *pb;

    pb = (Slapi_PBlock *)slapi_ch_calloc(1, sizeof(Slapi_PBlock));
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_init(pb);
#endif
    return pb;
}

void
slapi_pblock_init(Slapi_PBlock *pb)
{
    if (pb != NULL) {
        pblock_done(pb);
        pblock_init(pb);
#ifdef PBLOCK_ANALYTICS
        pblock_analytics_init(pb);
#endif
    }
}

/*
 * THIS FUNCTION IS AWFUL, WE SHOULD NOT REUSE PBLOCKS
 */
void
pblock_done(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_report(pb);
    pblock_analytics_destroy(pb);
#endif
    if (pb->pb_op != NULL) {
        operation_free(&pb->pb_op, pb->pb_conn);
        pb->pb_op = NULL;
    }
    slapi_ch_free((void **)&(pb->pb_dse));
    slapi_ch_free((void **)&(pb->pb_task));
    slapi_ch_free((void **)&(pb->pb_mr));
    slapi_ch_free((void **)&(pb->pb_deprecated));
    slapi_ch_free((void **)&(pb->pb_misc));
    if (pb->pb_intop != NULL) {
        delete_passwdPolicy(&pb->pb_intop->pwdpolicy);
        slapi_ch_free((void **)&(pb->pb_intop->pb_result_text));
    }
    slapi_ch_free((void **)&(pb->pb_intop));
    if (pb->pb_intplugin != NULL) {
        slapi_ch_free((void **)&(pb->pb_intplugin->pb_vattr_context));
    }
    slapi_ch_free((void **)&(pb->pb_intplugin));
}

void
slapi_pblock_destroy(Slapi_PBlock *pb)
{
    if (pb != NULL) {
        pblock_done(pb);
        slapi_ch_free((void **)&pb);
    }
}

/* functions to alloc internals if needed */
static inline void __attribute__((always_inline))
_pblock_assert_pb_dse(Slapi_PBlock *pblock)
{
    if (pblock->pb_dse == NULL) {
        pblock->pb_dse = (slapi_pblock_dse *)slapi_ch_calloc(1, sizeof(slapi_pblock_dse));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_task(Slapi_PBlock *pblock)
{
    if (pblock->pb_task == NULL) {
        pblock->pb_task = (slapi_pblock_task *)slapi_ch_calloc(1, sizeof(slapi_pblock_task));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_mr(Slapi_PBlock *pblock)
{
    if (pblock->pb_mr == NULL) {
        pblock->pb_mr = (slapi_pblock_matching_rule *)slapi_ch_calloc(1, sizeof(slapi_pblock_matching_rule));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_misc(Slapi_PBlock *pblock)
{
    if (pblock->pb_misc == NULL) {
        pblock->pb_misc = (slapi_pblock_misc *)slapi_ch_calloc(1, sizeof(slapi_pblock_misc));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_intop(Slapi_PBlock *pblock)
{
    if (pblock->pb_intop == NULL) {
        pblock->pb_intop = (slapi_pblock_intop *)slapi_ch_calloc(1, sizeof(slapi_pblock_intop));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_intplugin(Slapi_PBlock *pblock)
{
    if (pblock->pb_intplugin == NULL) {
        pblock->pb_intplugin = (slapi_pblock_intplugin *)slapi_ch_calloc(1, sizeof(slapi_pblock_intplugin));
    }
}

static inline void __attribute__((always_inline))
_pblock_assert_pb_deprecated(Slapi_PBlock *pblock)
{
    if (pblock->pb_deprecated == NULL) {
        pblock->pb_deprecated = (slapi_pblock_deprecated *)slapi_ch_calloc(1, sizeof(slapi_pblock_deprecated));
    }
}

/* It clones the pblock
 * the content of the source pblock is transfered
 * to the target pblock (returned)
 * The source pblock should not be used for any operation
 * it needs to be reinit (slapi_pblock_init)
 */
Slapi_PBlock *
slapi_pblock_clone(Slapi_PBlock *pb)
{
    /*
     * This is used only in psearch, with an access pattern of
     * ['13:3', '191:28', '47:18', '2001:1', '115:3', '503:3', '196:10', '52:18', '1930:3', '133:189', '112:13', '57:1', '214:91', '70:93', '193:14', '49:10', '403:6', '117:2', '1001:1', '130:161', '109:2', '198:36', '510:27', '1945:3', '114:16', '4:11', '216:91', '712:11', '195:19', '51:26', '140:10', '2005:18', '9:2', '132:334', '111:1', '1160:1', '410:54', '48:1', '2002:156', '116:3', '1000:22', '53:27', '9999:1', '113:13', '3:132', '590:3', '215:91', '194:20', '118:1', '131:30', '860:2', '110:14', ]
     */
    Slapi_PBlock *new_pb = slapi_pblock_new();
    new_pb->pb_backend = pb->pb_backend;
    new_pb->pb_conn = pb->pb_conn;
    new_pb->pb_op = pb->pb_op;
    new_pb->pb_plugin = pb->pb_plugin;
    /* Perform a shallow copy. */
    if (pb->pb_dse != NULL) {
        _pblock_assert_pb_dse(new_pb);
        *(new_pb->pb_dse) = *(pb->pb_dse);
    }
    if (pb->pb_task != NULL) {
        _pblock_assert_pb_task(new_pb);
        *(new_pb->pb_task) = *(pb->pb_task);
        memset(pb->pb_task, 0, sizeof(slapi_pblock_task));
    }
    if (pb->pb_mr != NULL) {
        _pblock_assert_pb_mr(new_pb);
        *(new_pb->pb_mr) = *(pb->pb_mr);
        memset(pb->pb_mr, 0, sizeof(slapi_pblock_matching_rule));
    }
    if (pb->pb_misc != NULL) {
        _pblock_assert_pb_misc(new_pb);
        *(new_pb->pb_misc) = *(pb->pb_misc);
        memset(pb->pb_misc, 0, sizeof(slapi_pblock_misc));
    }
    if (pb->pb_intop != NULL) {
        _pblock_assert_pb_intop(new_pb);
        *(new_pb->pb_intop) = *(pb->pb_intop);
        memset(pb->pb_intop, 0, sizeof(slapi_pblock_intop));
    }
    if (pb->pb_intplugin != NULL) {
        _pblock_assert_pb_intplugin(new_pb);
        *(new_pb->pb_intplugin) = *(pb->pb_intplugin);
        memset(pb->pb_intplugin, 0,sizeof(slapi_pblock_intplugin));
    }
    if (pb->pb_deprecated != NULL) {
        _pblock_assert_pb_deprecated(new_pb);
        *(new_pb->pb_deprecated) = *(pb->pb_deprecated);
        memset(pb->pb_deprecated, 0, sizeof(slapi_pblock_deprecated));
    }
#ifdef PBLOCK_ANALYTICS
    new_pb->analytics = NULL;
    pblock_analytics_init(new_pb);
#endif
    return new_pb;
}

/* JCM - when pb_o_params is used, check the operation type. */
/* JCM - when pb_o_results is used, check the operation type. */

#define SLAPI_PLUGIN_TYPE_CHECK(PBLOCK, TYPE) \
    if (PBLOCK->pb_plugin->plg_type != TYPE)  \
    return (-1)


/*
 * Macro used to safely retrieve a plugin related pblock value (if the
 * pb_plugin element is NULL, NULL is returned).
 */
#define SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pb, element) \
    ((pb)->pb_plugin == NULL ? NULL : (pb)->pb_plugin->element)

int
slapi_pblock_get(Slapi_PBlock *pblock, int arg, void *value)
{

#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pblock, arg);
#endif

    char *authtype;
    Slapi_Backend *be;

    PR_ASSERT(NULL != pblock);
    PR_ASSERT(NULL != value);
    be = pblock->pb_backend;

    switch (arg) {
#ifndef __COVERITY__
    /*
     * Lots of false positive generated by coverity scan 
     * are due to slapi_pblock_get calls.
     * It looks like the scanner taints all branches of the switch
     * independantly of the switch value.
     *
     * For example:
     * CID 1548916:  Memory - corruptions  (USE_AFTER_FREE)
     * >>> Calling "slapi_pblock_init" frees pointer "mep_pb->pb_op" which has
     * already been freed.
     * ...
     * 14. identity_transfer: Passing field pb->pb_op (indirectly, via argument 1)
     *  to function slapi_pblock_get, which assigns it to coldn.[show details]
     * 382        slapi_pblock_get(pb, SLAPI_URP_NAMING_COLLISION_DN, &coldn);
     *               case SLAPI_OPERATION:
     *            2. var_assign_parm: Assigning: *((Operation **)value) = pblock->pb_op.
     *            622        (*(Operation **)value) = pblock->pb_op;
     *            3. break: Breaking from switch.
     *            623        break;
     * 15. freed_arg: slapi_ch_free_string frees parameter coldn.[show details]
     * 383        slapi_ch_free_string(&coldn);
     * 
     * But the 'identity transfer' inference is wrong as the switch value
     *   SLAPI_URP_NAMING_COLLISION_DN does not match SLAPI_OPERATION
     *
     * ==> Better ignore the switch values
     * 
     */
#ifdef PBLOCK_ANALYTICS
    case SLAPI_HINT:
        break;
#endif
    case SLAPI_BACKEND:
        (*(Slapi_Backend **)value) = be;
        break;
    case SLAPI_BACKEND_COUNT:
        if (pblock->pb_misc != NULL) {
            (*(int *)value) = pblock->pb_misc->pb_backend_count;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_BE_TYPE:
        if (NULL == be) {
            return (-1);
        }
        (*(char **)value) = be->be_type;
        break;
    case SLAPI_BE_READONLY:
        if (NULL == be) {
            (*(int *)value) = 0; /* default value */
        } else {
            (*(int *)value) = be->be_readonly;
        }
        break;
    case SLAPI_BE_LASTMOD:
        if (NULL == be) {
            (*(int *)value) = (g_get_global_lastmod() == LDAP_ON);
        } else {
            (*(int *)value) = (be->be_lastmod == LDAP_ON || (be->be_lastmod == LDAP_UNDEFINED && g_get_global_lastmod() == LDAP_ON));
        }
        break;
    case SLAPI_CONNECTION:
        (*(Connection **)value) = pblock->pb_conn;
        break;
    case SLAPI_CONN_ID:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_TRACE,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_ID \n");
            return (-1);
        }
        (*(uint64_t *)value) = pblock->pb_conn->c_connid;
        break;
    case SLAPI_CONN_DN:
        /*
         * NOTE: we have to make a copy of this that the caller
         * is responsible for freeing. otherwise, they would get
         * a pointer that could be freed out from under them.
         */
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_DN \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(char **)value) = (NULL == pblock->pb_conn->c_dn ? NULL : slapi_ch_strdup(pblock->pb_conn->c_dn));
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_AUTHTYPE: /* deprecated */
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_AUTHTYPE \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        authtype = pblock->pb_conn->c_authtype;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        if (authtype == NULL) {
            (*(char **)value) = NULL;
        } else if (strcasecmp(authtype, SLAPD_AUTH_NONE) == 0) {
            (*(char **)value) = SLAPD_AUTH_NONE;
        } else if (strcasecmp(authtype, SLAPD_AUTH_SIMPLE) == 0) {
            (*(char **)value) = SLAPD_AUTH_SIMPLE;
        } else if (strcasecmp(authtype, SLAPD_AUTH_SSL) == 0) {
            (*(char **)value) = SLAPD_AUTH_SSL;
        } else if (strcasecmp(authtype, SLAPD_AUTH_OS) == 0) {
            (*(char **)value) = SLAPD_AUTH_OS;
        } else if (strncasecmp(authtype, SLAPD_AUTH_SASL,
                               strlen(SLAPD_AUTH_SASL)) == 0) {
            (*(char **)value) = SLAPD_AUTH_SASL;
        } else {
            (*(char **)value) = "unknown";
        }
        break;
    case SLAPI_CONN_AUTHMETHOD:
        /* returns a copy */
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_AUTHMETHOD \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(char **)value) = pblock->pb_conn->c_authtype ? slapi_ch_strdup(pblock->pb_conn->c_authtype) : NULL;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_CLIENTNETADDR:
        if (pblock->pb_conn == NULL) {
            memset(value, 0, sizeof(PRNetAddr));
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        if (pblock->pb_conn->cin_addr == NULL) {
            memset(value, 0, sizeof(PRNetAddr));
        } else {
            (*(PRNetAddr *)value) =
                *(pblock->pb_conn->cin_addr);
        }
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
	case SLAPI_CONN_CLIENTNETADDR_ACLIP:
        if (pblock->pb_conn == NULL) {
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(PRNetAddr **) value) = pblock->pb_conn->cin_addr_aclip;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_SERVERNETADDR:
        if (pblock->pb_conn == NULL) {
            memset(value, 0, sizeof(PRNetAddr));
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        if (pblock->pb_conn->cin_destaddr == NULL) {
            memset(value, 0, sizeof(PRNetAddr));
        } else {
            (*(PRNetAddr *)value) =
                *(pblock->pb_conn->cin_destaddr);
        }
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_CLIENTIP:
        if (pblock->pb_conn == NULL) {
            memset(value, 0, sizeof(struct in_addr));
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        if (pblock->pb_conn->cin_addr == NULL) {
            memset(value, 0, sizeof(struct in_addr));
        } else {

            if (PR_IsNetAddrType(pblock->pb_conn->cin_addr,
                                 PR_IpAddrV4Mapped)) {

                (*(struct in_addr *)value).s_addr =
                    (*(pblock->pb_conn->cin_addr)).ipv6.ip.pr_s6_addr32[3];

            } else {
                memset(value, 0, sizeof(struct in_addr));
            }
        }
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_SERVERIP:
        if (pblock->pb_conn == NULL) {
            memset(value, 0, sizeof(struct in_addr));
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        if (pblock->pb_conn->cin_destaddr == NULL) {
            memset(value, 0, sizeof(PRNetAddr));
        } else {

            if (PR_IsNetAddrType(pblock->pb_conn->cin_destaddr,
                                 PR_IpAddrV4Mapped)) {

                (*(struct in_addr *)value).s_addr =
                    (*(pblock->pb_conn->cin_destaddr)).ipv6.ip.pr_s6_addr32[3];

            } else {
                memset(value, 0, sizeof(struct in_addr));
            }
        }
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_IS_REPLICATION_SESSION:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(int *)value) = pblock->pb_conn->c_isreplication_session;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_IS_SSL_SESSION:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_IS_SSL_SESSION \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(int *)value) = pblock->pb_conn->c_flags & CONN_FLAG_SSL;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_SASL_SSF:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_SASL_SSF \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(int *)value) = pblock->pb_conn->c_sasl_ssf;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_SSL_SSF:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_SSL_SSF \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(int *)value) = pblock->pb_conn->c_ssl_ssf;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_LOCAL_SSF:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_LOCAL_SSF \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        (*(int *)value) = pblock->pb_conn->c_local_ssf;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_CERT:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_CERT \n");
            return (-1);
        }
        (*(CERTCertificate **)value) = pblock->pb_conn->c_client_cert;
        break;
    case SLAPI_OPERATION:
        (*(Operation **)value) = pblock->pb_op;
        break;
    case SLAPI_OPERATION_TYPE:
        if (pblock->pb_op == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Operation is NULL and hence cannot access SLAPI_OPERATION_TYPE \n");
            return (-1);
        }
        (*(int *)value) = pblock->pb_op->o_params.operation_type;
        break;
    case SLAPI_OPINITIATED_TIME:
        if (pblock->pb_op == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Operation is NULL and hence cannot access SLAPI_OPINITIATED_TIME \n");
            return (-1);
        }
        (*(time_t *)value) = pblock->pb_op->o_hr_time_utc.tv_sec;
        break;
    case SLAPI_REQUESTOR_ISROOT:
        if (pblock->pb_intop != NULL) {
            (*(int *)value) = pblock->pb_intop->pb_requestor_isroot;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_SKIP_MODIFIED_ATTRS:
        if (pblock->pb_op == NULL) {
            (*(int *)value) = 0; /* No Operation -> No skip */
        } else {
            (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_SKIP_MODIFIED_ATTRS);
        }
        break;
    case SLAPI_IS_REPLICATED_OPERATION:
        if (pblock->pb_op == NULL) {
            (*(int *)value) = 0; /* No Operation -> Not Replicated */
        } else {
            (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_REPLICATED);
        }
        break;
    case SLAPI_IS_MMR_REPLICATED_OPERATION:
        if (pblock->pb_op == NULL) {
            (*(int *)value) = 0; /* No Operation -> Not Replicated */
        } else {
            (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_REPLICATED);
        }
        break;

    case SLAPI_OPERATION_PARAMETERS:
        if (pblock->pb_op != NULL) {
            (*(struct slapi_operation_parameters **)value) = &pblock->pb_op->o_params;
        }
        break;

    /* stuff related to config file processing */
    case SLAPI_CONFIG_FILENAME:
    case SLAPI_CONFIG_LINENO:
    case SLAPI_CONFIG_ARGC:
    case SLAPI_CONFIG_ARGV:
        return (-1); /* deprecated since DS 5.0 (no longer useful) */

    /* pblock memory management */
    case SLAPI_DESTROY_CONTENT:
        if (pblock->pb_deprecated != NULL) {
            (*(int *)value) = pblock->pb_deprecated->pb_destroy_content;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* stuff related to the current plugin */
    case SLAPI_PLUGIN:
        (*(struct slapdplugin **)value) = pblock->pb_plugin;
        break;
    case SLAPI_PLUGIN_PRIVATE:
        (*(void **)value) = pblock->pb_plugin->plg_private;
        break;
    case SLAPI_PLUGIN_TYPE:
        (*(int *)value) = pblock->pb_plugin->plg_type;
        break;
    case SLAPI_PLUGIN_ARGV:
        (*(char ***)value) = pblock->pb_plugin->plg_argv;
        break;
    case SLAPI_PLUGIN_ARGC:
        (*(int *)value) = pblock->pb_plugin->plg_argc;
        break;
    case SLAPI_PLUGIN_VERSION:
        (*(char **)value) = pblock->pb_plugin->plg_version;
        break;
    case SLAPI_PLUGIN_PRECEDENCE:
        (*(int *)value) = pblock->pb_plugin->plg_precedence;
        break;
    case SLAPI_PLUGIN_OPRETURN:
        if (pblock->pb_intop != NULL) {
            (*(int *)value) = pblock->pb_intop->pb_opreturn;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_PLUGIN_OBJECT:
        if (pblock->pb_intplugin != NULL) {
            (*(void **)value) = pblock->pb_intplugin->pb_object;
        } else {
            (*(void **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_DESTROY_FN:
        if (pblock->pb_intplugin != NULL) {
            (*(IFP *)value) = pblock->pb_intplugin->pb_destroy_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_DESCRIPTION:
        (*(Slapi_PluginDesc *)value) = pblock->pb_plugin->plg_desc;
        break;
    case SLAPI_PLUGIN_IDENTITY:
        if (pblock->pb_intplugin != NULL) {
            (*(void **)value) = pblock->pb_intplugin->pb_plugin_identity;
        } else {
            (*(void **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_CONFIG_AREA:
        if (pblock->pb_intplugin != NULL) {
            (*(char **)value) = pblock->pb_intplugin->pb_plugin_config_area;
        } else {
            (*(char **)value) = 0;
        }
        break;
    case SLAPI_PLUGIN_CONFIG_DN:
        if (pblock->pb_plugin != NULL) {
            (*(char **)value) = pblock->pb_plugin->plg_dn;
        }
        break;
    case SLAPI_PLUGIN_INTOP_RESULT:
        if (pblock->pb_intop != NULL) {
            (*(int *)value) = pblock->pb_intop->pb_internal_op_result;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry ***)value) = pblock->pb_intop->pb_plugin_internal_search_op_entries;
        } else {
            (*(Slapi_Entry ***)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS:
        if (pblock->pb_intop != NULL) {
            (*(char ***)value) = pblock->pb_intop->pb_plugin_internal_search_op_referrals;
        } else {
            (*(char ***)value) = NULL;
        }
        break;

    /* database plugin functions */
    case SLAPI_PLUGIN_DB_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bind;
        break;
    case SLAPI_PLUGIN_DB_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_unbind;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_search;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_next_search_entry;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_next_search_entry_ext;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(VFPP *)value) = pblock->pb_plugin->plg_search_results_release;
        break;
    case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(VFP *)value) = pblock->pb_plugin->plg_prev_search_results;
        break;
    case SLAPI_PLUGIN_DB_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_compare;
        break;
    case SLAPI_PLUGIN_DB_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_modify;
        break;
    case SLAPI_PLUGIN_DB_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_modrdn;
        break;
    case SLAPI_PLUGIN_DB_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_add;
        break;
    case SLAPI_PLUGIN_DB_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_delete;
        break;
    case SLAPI_PLUGIN_DB_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_abandon;
        break;
    case SLAPI_PLUGIN_DB_CONFIG_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_config;
        break;
    case SLAPI_PLUGIN_CLOSE_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_close;
        break;
    case SLAPI_PLUGIN_CLEANUP_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_cleanup;
        break;
    case SLAPI_PLUGIN_START_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_start;
        break;
    case SLAPI_PLUGIN_POSTSTART_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_poststart;
        break;
    case SLAPI_PLUGIN_DB_WIRE_IMPORT_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_wire_import;
        break;
    case SLAPI_PLUGIN_DB_GET_INFO_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_get_info;
        break;
    case SLAPI_PLUGIN_DB_SET_INFO_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_set_info;
        break;
    case SLAPI_PLUGIN_DB_CTRL_INFO_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_ctrl_info;
        break;
    case SLAPI_PLUGIN_DB_SEQ_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_seq;
        break;
    case SLAPI_PLUGIN_DB_ENTRY_FN:
        (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                                  plg_entry);
        break;
    case SLAPI_PLUGIN_DB_REFERRAL_FN:
        (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                                  plg_referral);
        break;
    case SLAPI_PLUGIN_DB_RESULT_FN:
        (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                                  plg_result);
        break;
    case SLAPI_PLUGIN_DB_RMDB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_rmdb;
        break;
    case SLAPI_PLUGIN_DB_LDIF2DB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_ldif2db;
        break;
    case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2ldif;
        break;
    case SLAPI_PLUGIN_DB_COMPACT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_dbcompact;
        break;
    case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2index;
        break;
    case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_archive2db;
        break;
    case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2archive;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_upgradedb;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_upgradednformat;
        break;
    case SLAPI_PLUGIN_DB_DBVERIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_dbverify;
        break;
    case SLAPI_PLUGIN_DB_BEGIN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin;
        break;
    case SLAPI_PLUGIN_DB_COMMIT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit;
        break;
    case SLAPI_PLUGIN_DB_ABORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort;
        break;
    case SLAPI_PLUGIN_DB_TEST_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest;
        break;
    /* database plugin-specific parameters */
    case SLAPI_PLUGIN_DB_NO_ACL:
        if (pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        if (NULL == be) {
            (*(int *)value) = 0; /* default value */
        } else {
            (*(int *)value) = be->be_noacl;
        }
        break;

    /* extendedop plugin functions */
    case SLAPI_PLUGIN_EXT_OP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_exhandler;
        break;
    case SLAPI_PLUGIN_EXT_OP_OIDLIST:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        (*(char ***)value) = pblock->pb_plugin->plg_exoids;
        break;
    case SLAPI_PLUGIN_EXT_OP_NAMELIST:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        (*(char ***)value) = pblock->pb_plugin->plg_exnames;
        break;
    case SLAPI_PLUGIN_EXT_OP_BACKEND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_be_exhandler;
        break;

    /* preoperation plugin functions */
    case SLAPI_PLUGIN_PRE_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_prebind;
        break;
    case SLAPI_PLUGIN_PRE_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preunbind;
        break;
    case SLAPI_PLUGIN_PRE_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_presearch;
        break;
    case SLAPI_PLUGIN_PRE_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_precompare;
        break;
    case SLAPI_PLUGIN_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_premodify;
        break;
    case SLAPI_PLUGIN_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_premodrdn;
        break;
    case SLAPI_PLUGIN_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preadd;
        break;
    case SLAPI_PLUGIN_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_predelete;
        break;
    case SLAPI_PLUGIN_PRE_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preabandon;
        break;
    case SLAPI_PLUGIN_PRE_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preentry;
        break;
    case SLAPI_PLUGIN_PRE_REFERRAL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_prereferral;
        break;
    case SLAPI_PLUGIN_PRE_RESULT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preresult;
        break;
    case SLAPI_PLUGIN_PRE_EXTOP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREEXTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_preextop;
        break;

    /* postoperation plugin functions */
    case SLAPI_PLUGIN_POST_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postbind;
        break;
    case SLAPI_PLUGIN_POST_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postunbind;
        break;
    case SLAPI_PLUGIN_POST_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postsearch;
        break;
    case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postsearchfail;
        break;
    case SLAPI_PLUGIN_POST_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postcompare;
        break;
    case SLAPI_PLUGIN_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postmodify;
        break;
    case SLAPI_PLUGIN_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postmodrdn;
        break;
    case SLAPI_PLUGIN_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postadd;
        break;
    case SLAPI_PLUGIN_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postdelete;
        break;
    case SLAPI_PLUGIN_POST_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postabandon;
        break;
    case SLAPI_PLUGIN_POST_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postentry;
        break;
    case SLAPI_PLUGIN_POST_REFERRAL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postreferral;
        break;
    case SLAPI_PLUGIN_POST_RESULT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postresult;
        break;
    case SLAPI_PLUGIN_POST_EXTOP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTEXTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_postextop;
        break;
    case SLAPI_ENTRY_PRE_OP:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_pre_op_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_ENTRY_POST_OP:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_post_op_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;

    /* backend preoperation plugin */
    case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepremodify;
        break;
    case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepremodrdn;
        break;
    case SLAPI_PLUGIN_BE_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepreadd;
        break;
    case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepredelete;
        break;
    case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepreclose;
        break;

    /* backend postoperation plugin */
    case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostmodify;
        break;
    case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostmodrdn;
        break;
    case SLAPI_PLUGIN_BE_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostadd;
        break;
    case SLAPI_PLUGIN_BE_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostdelete;
        break;
    case SLAPI_PLUGIN_BE_POST_OPEN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostopen;
        break;

    case SLAPI_PLUGIN_BE_POST_EXPORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostexport;
        break;
    case SLAPI_PLUGIN_BE_POST_IMPORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_bepostimport;
        break;

    /* internal preoperation plugin */
    case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modify;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modrdn;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_add;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_delete;
        break;

    /* internal postoperation plugin */
    case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modify;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modrdn;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_add;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_delete;
        break;

    /* rootDN pre bind operation plugin */
    case SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_bind;
        break;

    /* backend pre txn operation plugin */
    case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodify;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodrdn;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpreadd;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpredelete;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpredeletetombstone;
        break;

    /* backend post txn operation plugin */
    case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodify;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodrdn;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostadd;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostdelete;
        break;

    /* target address & controls for all operations should be normalized  */
    case SLAPI_TARGET_ADDRESS:
        if (pblock->pb_op != NULL) {
            (*(entry_address **)value) = &(pblock->pb_op->o_params.target_address);
        }
        break;
    case SLAPI_TARGET_DN: /* DEPRECATED */
        /* The returned value refers SLAPI_TARGET_SDN.
         * It should not be freed.*/
        if (pblock->pb_op != NULL) {
            Slapi_DN *sdn = pblock->pb_op->o_params.target_address.sdn;
            if (sdn) {
                (*(char **)value) = (char *)slapi_sdn_get_dn(sdn);
            } else {
                (*(char **)value) = NULL;
            }
        } else {
            return (-1);
        }
        break;
    case SLAPI_TARGET_SDN: /* Alias from SLAPI_ADD_TARGET_SDN */
        if (pblock->pb_op != NULL) {
            (*(Slapi_DN **)value) = pblock->pb_op->o_params.target_address.sdn;
        } else {
            return (-1);
        }
        break;
    case SLAPI_ORIGINAL_TARGET_DN:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.target_address.udn;
        }
        break;
    case SLAPI_TARGET_UNIQUEID:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.target_address.uniqueid;
        }
        break;
    case SLAPI_REQCONTROLS:
        if (pblock->pb_op != NULL) {
            (*(LDAPControl ***)value) = pblock->pb_op->o_params.request_controls;
        }
        break;
    case SLAPI_RESCONTROLS:
        if (pblock->pb_op != NULL) {
            (*(LDAPControl ***)value) = pblock->pb_op->o_results.result_controls;
        }
        break;
    case SLAPI_CONTROLS_ARG: /* used to pass control argument before operation is created */
        if (pblock->pb_intop != NULL) {
            (*(LDAPControl ***)value) = pblock->pb_intop->pb_ctrls_arg;
        } else {
            (*(LDAPControl ***)value) = NULL;
        }
        break;
    /* notes to be added to the access log RESULT line for this op. */
    case SLAPI_OPERATION_NOTES:
        if (pblock->pb_intop != NULL) {
            (*(unsigned int *)value) = pblock->pb_intop->pb_operation_notes;
        } else {
            (*(unsigned int *)value) = 0;
        }
        break;

    /* syntax plugin functions */
    case SLAPI_PLUGIN_SYNTAX_FILTER_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_filter_ava;
        break;
    case SLAPI_PLUGIN_SYNTAX_FILTER_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_filter_sub;
        break;
    case SLAPI_PLUGIN_SYNTAX_VALUES2KEYS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_values2keys;
        break;
    case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_assertion2keys_ava;
        break;
    case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_assertion2keys_sub;
        break;
    case SLAPI_PLUGIN_SYNTAX_NAMES:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(char ***)value) = pblock->pb_plugin->plg_syntax_names;
        break;
    case SLAPI_PLUGIN_SYNTAX_OID:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(char **)value) = pblock->pb_plugin->plg_syntax_oid;
        break;
    case SLAPI_PLUGIN_SYNTAX_FLAGS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(int *)value) = pblock->pb_plugin->plg_syntax_flags;
        break;
    case SLAPI_PLUGIN_SYNTAX_COMPARE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_compare;
        break;
    case SLAPI_SYNTAX_SUBSTRLENS: /* aka SLAPI_MR_SUBSTRLENS */
        if (pblock->pb_intplugin != NULL) {
            (*(int **)value) = pblock->pb_intplugin->pb_substrlens;
        } else {
            (*(int **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_SYNTAX_VALIDATE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_syntax_validate;
        break;
    case SLAPI_PLUGIN_SYNTAX_NORMALIZE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        (*(VFPV *)value) = pblock->pb_plugin->plg_syntax_normalize;
        break;

    /* controls we know about */
    case SLAPI_MANAGEDSAIT:
        if (pblock->pb_intop != NULL) {
            (*(int *)value) = pblock->pb_intop->pb_managedsait;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_PWPOLICY:
        if (pblock->pb_intop != NULL) {
            (*(int *)value) = pblock->pb_intop->pb_pwpolicy_ctrl;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* add arguments */
    case SLAPI_ADD_ENTRY:
        if (pblock->pb_op != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_op->o_params.p.p_add.target_entry;
        }
        break;
    case SLAPI_ADD_EXISTING_DN_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_existing_dn_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_ADD_EXISTING_UNIQUEID_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_existing_uniqueid_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_ADD_PARENT_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_parent_entry;
        }
        break;
    case SLAPI_ADD_PARENT_UNIQUEID:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_add.parentuniqueid;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    /* bind arguments */
    case SLAPI_BIND_METHOD:
        if (pblock->pb_op != NULL) {
            (*(ber_tag_t *)value) = pblock->pb_op->o_params.p.p_bind.bind_method;
        }
        break;
    case SLAPI_BIND_CREDENTIALS:
        if (pblock->pb_op != NULL) {
            (*(struct berval **)value) = pblock->pb_op->o_params.p.p_bind.bind_creds;
        }
        break;
    case SLAPI_BIND_SASLMECHANISM:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_bind.bind_saslmechanism;
        }
        break;
    /* bind return values */
    case SLAPI_BIND_RET_SASLCREDS:
        if (pblock->pb_op != NULL) {
            (*(struct berval **)value) = pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds;
        }
        break;

    /* compare arguments */
    case SLAPI_COMPARE_TYPE:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type;
        }
        break;
    case SLAPI_COMPARE_VALUE:
        if (pblock->pb_op != NULL) {
            (*(struct berval **)value) = &pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value;
        }
        break;

    /* modify arguments */
    case SLAPI_MODIFY_MODS:
        PR_ASSERT(pblock->pb_op);
        if (pblock->pb_op != NULL) {
            if (pblock->pb_op->o_params.operation_type == SLAPI_OPERATION_MODIFY) {
                (*(LDAPMod ***)value) = pblock->pb_op->o_params.p.p_modify.modify_mods;
            } else if (pblock->pb_op->o_params.operation_type == SLAPI_OPERATION_MODRDN) {
                (*(LDAPMod ***)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_mods;
            } else {
                PR_ASSERT(0); /* JCM */
            }
        }
        break;

    /* modrdn arguments */
    case SLAPI_MODRDN_NEWRDN:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn;
        }
        break;
    case SLAPI_MODRDN_DELOLDRDN:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn;
        }
        break;
    case SLAPI_MODRDN_NEWSUPERIOR: /* DEPRECATED */
        if (pblock->pb_op != NULL) {
            Slapi_DN *sdn =
                pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
            if (sdn) {
                (*(char **)value) = (char *)slapi_sdn_get_dn(sdn);
            } else {
                (*(char **)value) = NULL;
            }
        } else {
            return -1;
        }
        break;
    case SLAPI_MODRDN_NEWSUPERIOR_SDN:
        if (pblock->pb_op != NULL) {
            (*(Slapi_DN **)value) =
                pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
        } else {
            return -1;
        }
        break;
    case SLAPI_MODRDN_PARENT_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_parent_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_MODRDN_NEWPARENT_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_newparent_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_MODRDN_TARGET_ENTRY:
        if (pblock->pb_intop != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_intop->pb_target_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_MODRDN_NEWSUPERIOR_ADDRESS:
        if (pblock->pb_op != NULL) {
            (*(entry_address **)value) = &(pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address);
        }
        break;

    /* search arguments */
    case SLAPI_SEARCH_SCOPE:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_scope;
        }
        break;
    case SLAPI_SEARCH_DEREF:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_deref;
        }
        break;
    case SLAPI_SEARCH_SIZELIMIT:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_sizelimit;
        }
        break;
    case SLAPI_SEARCH_TIMELIMIT:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_timelimit;
        }
        break;
    case SLAPI_SEARCH_FILTER:
        if (pblock->pb_op != NULL) {
            (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter;
        }
        break;
    case SLAPI_SEARCH_FILTER_INTENDED:
        if (pblock->pb_op != NULL) {
            if (pblock->pb_op->o_params.p.p_search.search_filter_intended) {
                (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter_intended;
            } else {
                (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter;
            }
        }
        break;
    case SLAPI_SEARCH_STRFILTER:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_search.search_strfilter;
        }
        break;
    case SLAPI_SEARCH_ATTRS:
        if (pblock->pb_op != NULL) {
            (*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_attrs;
        }
        break;
    case SLAPI_SEARCH_GERATTRS:
        if (pblock->pb_op != NULL) {
            (*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_gerattrs;
        }
        break;
    case SLAPI_SEARCH_REQATTRS:
        if (pblock->pb_op != NULL) {
            (*(char ***)value) = pblock->pb_op->o_searchattrs;
        }
        break;
    case SLAPI_SEARCH_ATTRSONLY:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_attrsonly;
        }
        break;
    case SLAPI_SEARCH_IS_AND:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_is_and;
        }
        break;

    case SLAPI_ABANDON_MSGID:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid;
        }
        break;

    /* extended operation arguments */
    case SLAPI_EXT_OP_REQ_OID:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_params.p.p_extended.exop_oid;
        }
        break;
    case SLAPI_EXT_OP_REQ_VALUE:
        if (pblock->pb_op != NULL) {
            (*(struct berval **)value) = pblock->pb_op->o_params.p.p_extended.exop_value;
        }
        break;
    /* extended operation return values */
    case SLAPI_EXT_OP_RET_OID:
        if (pblock->pb_op != NULL) {
            (*(char **)value) = pblock->pb_op->o_results.r.r_extended.exop_ret_oid;
        }
        break;
    case SLAPI_EXT_OP_RET_VALUE:
        if (pblock->pb_op != NULL) {
            (*(struct berval **)value) = pblock->pb_op->o_results.r.r_extended.exop_ret_value;
        }
        break;

    /* matching rule plugin functions */
    case SLAPI_PLUGIN_MR_FILTER_CREATE_FN:
        SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_create;
        break;
    case SLAPI_PLUGIN_MR_INDEXER_CREATE_FN:
        SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_indexer_create;
        break;
    case SLAPI_PLUGIN_MR_FILTER_MATCH_FN:
        if (pblock->pb_mr != NULL) {
            (*(mrFilterMatchFn *)value) = pblock->pb_mr->filter_match_fn;
        } else {
            (*(mrFilterMatchFn *)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_FILTER_INDEX_FN:
        if (pblock->pb_mr != NULL) {
            (*(IFP *)value) = pblock->pb_mr->filter_index_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_FILTER_RESET_FN:
        if (pblock->pb_mr != NULL) {
            (*(IFP *)value) = pblock->pb_mr->filter_reset_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_INDEX_FN:
        if (pblock->pb_mr != NULL) {
            (*(IFP *)value) = pblock->pb_mr->index_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_INDEX_SV_FN:
        if (pblock->pb_mr != NULL) {
            (*(IFP *)value) = pblock->pb_mr->index_sv_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;

    /* matching rule plugin arguments */
    case SLAPI_PLUGIN_MR_OID:
        if (pblock->pb_mr != NULL) {
            (*(char **)value) = pblock->pb_mr->oid;
        } else {
            (*(char **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_TYPE:
        if (pblock->pb_mr != NULL) {
            (*(char **)value) = pblock->pb_mr->type;
        } else {
            (*(char **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_VALUE:
        if (pblock->pb_mr != NULL) {
            (*(struct berval **)value) = pblock->pb_mr->value;
        } else {
            (*(struct berval **)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_VALUES:
        if (pblock->pb_mr != NULL) {
            (*(struct berval ***)value) = pblock->pb_mr->values;
        } else {
            (*(struct berval ***)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_KEYS:
        if (pblock->pb_mr != NULL) {
            (*(struct berval ***)value) = pblock->pb_mr->keys;
        } else {
            (*(struct berval ***)value) = NULL;
        }
        break;
    case SLAPI_PLUGIN_MR_FILTER_REUSABLE:
        if (pblock->pb_mr != NULL) {
            (*(unsigned int *)value) = pblock->pb_mr->filter_reusable;
        } else {
            (*(unsigned int *)value) = 0;
        }
        break;
    case SLAPI_PLUGIN_MR_QUERY_OPERATOR:
        if (pblock->pb_mr != NULL) {
            (*(int *)value) = pblock->pb_mr->query_operator;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_PLUGIN_MR_USAGE:
        if (pblock->pb_mr != NULL) {
            (*(unsigned int *)value) = pblock->pb_mr->usage;
        } else {
            (*(unsigned int *)value) = 0;
        }
        break;

    /* new style matching rule syntax plugin functions */
    case SLAPI_PLUGIN_MR_FILTER_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_ava;
        break;
    case SLAPI_PLUGIN_MR_FILTER_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_sub;
        break;
    case SLAPI_PLUGIN_MR_VALUES2KEYS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_values2keys;
        break;
    case SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_assertion2keys_ava;
        break;
    case SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_assertion2keys_sub;
        break;
    case SLAPI_PLUGIN_MR_FLAGS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(int *)value) = pblock->pb_plugin->plg_mr_flags;
        break;
    case SLAPI_PLUGIN_MR_NAMES:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(char ***)value) = pblock->pb_plugin->plg_mr_names;
        break;
    case SLAPI_PLUGIN_MR_COMPARE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_mr_compare;
        break;
    case SLAPI_PLUGIN_MR_NORMALIZE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        (*(VFPV *)value) = pblock->pb_plugin->plg_mr_normalize;
        break;

    /* seq arguments */
    case SLAPI_SEQ_TYPE:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->seq_type;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_SEQ_ATTRNAME:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->seq_attrname;
        } else {
            (*(char **)value) = NULL;
        }
        break;
    case SLAPI_SEQ_VAL:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->seq_val;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    /* ldif2db arguments */
    case SLAPI_LDIF2DB_FILE:
        if (pblock->pb_task != NULL) {
            (*(char ***)value) = pblock->pb_task->ldif_files;
        } else {
            (*(char ***)value) = NULL;
        }
        break;
    case SLAPI_LDIF2DB_REMOVEDUPVALS:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->removedupvals;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_DB2INDEX_ATTRS:
        if (pblock->pb_task != NULL) {
            (*(char ***)value) = pblock->pb_task->db2index_attrs;
        } else {
            (*(char ***)value) = NULL;
        }
        break;
    case SLAPI_LDIF2DB_NOATTRINDEXES:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif2db_noattrindexes;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_LDIF2DB_INCLUDE:
        if (pblock->pb_task != NULL) {
            (*(char ***)value) = pblock->pb_task->ldif_include;
        } else {
            (*(char ***)value) = NULL;
        }
        break;
    case SLAPI_LDIF2DB_EXCLUDE:
        if (pblock->pb_task != NULL) {
            (*(char ***)value) = pblock->pb_task->ldif_exclude;
        } else {
            (*(char ***)value) = NULL;
        }
        break;
    case SLAPI_LDIF2DB_GENERATE_UNIQUEID:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif_generate_uniqueid;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_LDIF2DB_ENCRYPT:
    case SLAPI_DB2LDIF_DECRYPT:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif_encrypt;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_LDIF2DB_NAMESPACEID:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->ldif_namespaceid;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    /* db2ldif arguments */
    case SLAPI_DB2LDIF_PRINTKEY:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif_printkey;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_DB2LDIF_DUMP_UNIQUEID:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif_dump_uniqueid;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_DB2LDIF_FILE:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->ldif_file;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    /* db2ldif/ldif2db/db2bak/bak2db arguments */
    case SLAPI_BACKEND_INSTANCE_NAME:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->instance_name;
        } else {
            (*(char **)value) = NULL;
        }
        break;
    case SLAPI_BACKEND_TASK:
        if (pblock->pb_task != NULL) {
            (*(Slapi_Task **)value) = pblock->pb_task->task;
        } else {
            (*(Slapi_Task **)value) = NULL;
        }
        break;
    case SLAPI_TASK_FLAGS:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->task_flags;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_DB2LDIF_SERVER_RUNNING:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->server_running;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_BULK_IMPORT_ENTRY:
        if (pblock->pb_task != NULL) {
            (*(Slapi_Entry **)value) = pblock->pb_task->import_entry;
        } else {
            (*(Slapi_Entry **)value) = NULL;
        }
        break;
    case SLAPI_BULK_IMPORT_STATE:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->import_state;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_LDIF_CHANGELOG:
        if (pblock->pb_task != NULL) {
            (*(int *)value) = pblock->pb_task->ldif_include_changelog;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* dbverify */
    case SLAPI_DBVERIFY_DBDIR:
        if (pblock->pb_task != NULL) {
            (*(char **)value) = pblock->pb_task->dbverify_dbdir;
        } else {
            (*(char **)value) = NULL;
        }
        break;


    /* transaction arguments */
    case SLAPI_PARENT_TXN:
        if (pblock->pb_deprecated != NULL) {
            (*(void **)value) = pblock->pb_deprecated->pb_parent_txn;
        } else {
            (*(void **)value) = NULL;
        }
        break;
    case SLAPI_TXN:
        if (pblock->pb_intop != NULL) {
            (*(void **)value) = pblock->pb_intop->pb_txn;
        } else {
            (*(void **)value) = NULL;
        }
        break;
    case SLAPI_TXN_RUV_MODS_FN:
        if (pblock->pb_intop != NULL) {
            (*(IFP *)value) = pblock->pb_intop->pb_txn_ruv_mods_fn;
        } else {
            (*(IFP *)value) = NULL;
        }
        break;

    /* Search results set */
    case SLAPI_SEARCH_RESULT_SET:
        if (pblock->pb_op != NULL) {
            (*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_set;
        }
        break;
    /* estimated search result set size */
    case SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_results.r.r_search.estimate;
        }
        break;
    /* Entry returned from iterating over results set */
    case SLAPI_SEARCH_RESULT_ENTRY:
        if (pblock->pb_op != NULL) {
            (*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_entry;
        }
        break;
    case SLAPI_SEARCH_RESULT_ENTRY_EXT:
        if (pblock->pb_op != NULL) {
            (*(void **)value) = pblock->pb_op->o_results.r.r_search.opaque_backend_ptr;
        }
        break;
    /* Number of entries returned from search */
    case SLAPI_NENTRIES:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_results.r.r_search.nentries;
        }
        break;
    /* Referrals encountered while iterating over result set */
    case SLAPI_SEARCH_REFERRALS:
        if (pblock->pb_op != NULL) {
            (*(struct berval ***)value) = pblock->pb_op->o_results.r.r_search.search_referrals;
        }
        break;

    case SLAPI_RESULT_CODE:
        if (pblock->pb_op != NULL)
            *((int *)value) = pblock->pb_op->o_results.result_code;
        break;
    case SLAPI_RESULT_MATCHED:
        if (pblock->pb_op != NULL)
            *((char **)value) = pblock->pb_op->o_results.result_matched;
        break;
    case SLAPI_RESULT_TEXT:
        if (pblock->pb_op != NULL)
            *((char **)value) = pblock->pb_op->o_results.result_text;
        break;
    case SLAPI_PB_RESULT_TEXT:
        if (pblock->pb_intop != NULL) {
            *((char **)value) = pblock->pb_intop->pb_result_text;
        } else {
            *((char **)value) = NULL;
        }
        break;

    /* Size of the database, in kb */
    case SLAPI_DBSIZE:
        if (pblock->pb_misc != NULL) {
            (*(unsigned int *)value) = pblock->pb_misc->pb_dbsize;
        } else {
            (*(unsigned int *)value) = 0;
        }
        break;

    /* ACL Plugin */
    case SLAPI_PLUGIN_ACL_INIT:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_init;
        break;
    case SLAPI_PLUGIN_ACL_SYNTAX_CHECK:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_syntax_check;
        break;
    case SLAPI_PLUGIN_ACL_ALLOW_ACCESS:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_access_allowed;
        break;
    case SLAPI_PLUGIN_ACL_MODS_ALLOWED:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_mods_allowed;
        break;
    case SLAPI_PLUGIN_ACL_MODS_UPDATE:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_mods_update;
        break;
    /* MMR Plugin */
    case SLAPI_PLUGIN_MMR_BETXN_PREOP:
        (*(IFP *)value) = pblock->pb_plugin->plg_mmr_betxn_preop;
	break;
    case SLAPI_PLUGIN_MMR_BETXN_POSTOP:
        (*(IFP *)value) = pblock->pb_plugin->plg_mmr_betxn_postop;
	break;

    case SLAPI_REQUESTOR_DN:
        /* NOTE: It's not a copy of the DN */
        if (pblock->pb_op != NULL) {
            char *dn = (char *)slapi_sdn_get_dn(&pblock->pb_op->o_sdn);
            if (dn == NULL)
                (*(char **)value) = "";
            else
                (*(char **)value) = dn;
        }
        break;

    case SLAPI_REQUESTOR_SDN:
        if (pblock->pb_op != NULL) {
            (*(Slapi_DN **)value) = &pblock->pb_op->o_sdn;
        }
        break;

    case SLAPI_REQUESTOR_NDN:
        /* NOTE: It's not a copy of the DN */
        if (pblock->pb_op != NULL) {
            char *ndn = (char *)slapi_sdn_get_ndn(&pblock->pb_op->o_sdn);
            if (ndn == NULL)
                (*(char **)value) = "";
            else
                (*(char **)value) = ndn;
        }
        break;

    case SLAPI_OPERATION_AUTHTYPE:
        if (pblock->pb_op != NULL) {
            if (pblock->pb_op->o_authtype == NULL)
                (*(char **)value) = "";
            else
                (*(char **)value) = pblock->pb_op->o_authtype;
        }
        break;

    case SLAPI_OPERATION_SSF:
        if (pblock->pb_op != NULL) {
            *((int *)value) = pblock->pb_op->o_ssf;
        }
        break;

    case SLAPI_CLIENT_DNS:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n");
            return (-1);
        }
        (*(struct berval ***)value) = pblock->pb_conn->c_domain;
        break;

    case SLAPI_BE_MAXNESTLEVEL:
        if (NULL == be) {
            return (-1);
        }
        (*(int *)value) = be->be_maxnestlevel;
        break;
    case SLAPI_OPERATION_ID:
        if (pblock->pb_op != NULL) {
            (*(int *)value) = pblock->pb_op->o_opid;
        }
        break;
    /* Command line arguments */
    case SLAPI_ARGC:
        if (pblock->pb_misc != NULL) {
            (*(int *)value) = pblock->pb_misc->pb_slapd_argc;
        } else {
            (*(int *)value) = 0;
        }
        break;
    case SLAPI_ARGV:
        if (pblock->pb_misc != NULL) {
            (*(char ***)value) = pblock->pb_misc->pb_slapd_argv;
        } else {
            (*(char ***)value) = NULL;
        }
        break;

    /* Config file directory */
    case SLAPI_CONFIG_DIRECTORY:
        if (pblock->pb_intplugin != NULL) {
            (*(char **)value) = pblock->pb_intplugin->pb_slapd_configdir;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    /* password storage scheme (kexcoff */
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME:
        (*(char **)value) = pblock->pb_plugin->plg_pwdstorageschemename;
        break;
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD:
        if (pblock->pb_deprecated != NULL) {
            (*(char **)value) = pblock->pb_deprecated->pb_pwd_storage_scheme_user_passwd;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD:
        if (pblock->pb_deprecated != NULL) {
            (*(char **)value) = pblock->pb_deprecated->pb_pwd_storage_scheme_db_passwd;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN:
        (*(CFP *)value) = pblock->pb_plugin->plg_pwdstorageschemeenc;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemedec;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN:
        (*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemecmp;
        break;

    /* entry fetch/store plugin */
    case SLAPI_PLUGIN_ENTRY_FETCH_FUNC:
        (*(IFP *)value) = pblock->pb_plugin->plg_entryfetchfunc;
        break;

    case SLAPI_PLUGIN_ENTRY_STORE_FUNC:
        (*(IFP *)value) = pblock->pb_plugin->plg_entrystorefunc;
        break;

    case SLAPI_PLUGIN_ENABLED:
        if (pblock->pb_intplugin != NULL) {
            *((int *)value) = pblock->pb_intplugin->pb_plugin_enabled;
        } else {
            *((int *)value) = 0;
        }
        break;

    /* DSE add parameters */
    case SLAPI_DSE_DONT_WRITE_WHEN_ADDING:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->dont_add_write;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* DSE add parameters */
    case SLAPI_DSE_MERGE_WHEN_ADDING:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->add_merge;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* DSE add parameters */
    case SLAPI_DSE_DONT_CHECK_DUPS:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->dont_check_dups;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* DSE modify parameters */
    case SLAPI_DSE_REAPPLY_MODS:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->reapply_mods;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* DSE read parameters */
    case SLAPI_DSE_IS_PRIMARY_FILE:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->is_primary_file;
        } else {
            (*(int *)value) = 0;
        }
        break;

    /* used internally by schema code (schema.c) */
    case SLAPI_SCHEMA_FLAGS:
        if (pblock->pb_dse != NULL) {
            (*(int *)value) = pblock->pb_dse->schema_flags;
        } else {
            (*(int *)value) = 0;
        }
        break;

    case SLAPI_URP_NAMING_COLLISION_DN:
        if (pblock->pb_intop != NULL) {
            (*(char **)value) = pblock->pb_intop->pb_urp_naming_collision_dn;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    case SLAPI_URP_TOMBSTONE_UNIQUEID:
        if (pblock->pb_intop != NULL) {
            (*(char **)value) = pblock->pb_intop->pb_urp_tombstone_uniqueid;
        } else {
            (*(char **)value) = NULL;
        }
        break;

    case SLAPI_URP_TOMBSTONE_CONFLICT_DN:
        if (pblock->pb_intop != NULL) {
            (*(char **)value) = pblock->pb_intop->pb_urp_tombstone_conflict_dn;
        } else {
            (*(char **)value) = NULL;
        }
	break;

    case SLAPI_SEARCH_CTRLS:
        if (pblock->pb_intop != NULL) {
            (*(LDAPControl ***)value) = pblock->pb_intop->pb_search_ctrls;
        } else {
            (*(LDAPControl ***)value) = NULL;
        }
        break;

    case SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED:
        if (pblock->pb_intplugin != NULL) {
            (*(int *)value) = pblock->pb_intplugin->pb_syntax_filter_normalized;
        } else {
            (*(int *)value) =  0;
        }
        break;

    case SLAPI_PLUGIN_SYNTAX_FILTER_DATA:
        if (pblock->pb_intplugin != NULL) {
            (*(void **)value) = pblock->pb_intplugin->pb_syntax_filter_data;
        } else {
            (*(void **)value) = NULL;
        }
        break;

    case SLAPI_PAGED_RESULTS_INDEX:
        if (op_is_pagedresults(pblock->pb_op) && pblock->pb_intop != NULL) {
            /* search req is simple paged results */
            (*(int *)value) = pblock->pb_intop->pb_paged_results_index;
        } else {
            (*(int *)value) = -1;
        }
        break;

    case SLAPI_PAGED_RESULTS_COOKIE:
        if (op_is_pagedresults(pblock->pb_op) && pblock->pb_intop != NULL) {
            /* search req is simple paged results */
            (*(int *)value) = pblock->pb_intop->pb_paged_results_cookie;
        } else {
            (*(int *)value) = 0;
        }
        break;

    case SLAPI_USN_INCREMENT_FOR_TOMBSTONE:
        if (pblock->pb_intop != NULL) {
            (*(int32_t *)value) = pblock->pb_intop->pb_usn_tombstone_incremented;
        } else {
            (*(int32_t *)value) = 0;
        }
        break;

    /* ACI Target Check */
    case SLAPI_ACI_TARGET_CHECK:
        if (pblock->pb_misc != NULL) {
            (*(int *)value) = pblock->pb_misc->pb_aci_target_check;
        } else {
            (*(int *)value) = 0;
        }
        break;
#endif /* __COVERITY__ */
    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pblock_get", "Unknown parameter block argument %d\n", arg);
        PR_ASSERT(0);
        return (-1);
    }

    return (0);
}

int
slapi_pblock_set(Slapi_PBlock *pblock, int arg, void *value)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pblock, arg);
#endif
    char *authtype;

    PR_ASSERT(NULL != pblock);

    switch (arg) {
#ifdef PBLOCK_ANALYTICS
    case SLAPI_HINT:
        break;
#endif
    case SLAPI_BACKEND:
        pblock->pb_backend = (Slapi_Backend *)value;
        if (pblock->pb_backend && (NULL == pblock->pb_plugin)) {
            /* newly allocated pblock may not have backend plugin set. */
            pblock->pb_plugin =
                (struct slapdplugin *)pblock->pb_backend->be_database;
        }
        break;
    case SLAPI_BACKEND_COUNT:
        _pblock_assert_pb_misc(pblock);
        pblock->pb_misc->pb_backend_count = *((int *)value);
        break;
    case SLAPI_CONNECTION:
        pblock->pb_conn = (Connection *)value;
        break;
    case SLAPI_OPERATION:
        pblock->pb_op = (Operation *)value;
        break;
    case SLAPI_OPINITIATED_TIME:
        break;
    case SLAPI_REQUESTOR_ISROOT:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_requestor_isroot = *((int *)value);
        break;
    case SLAPI_IS_REPLICATED_OPERATION:
        PR_ASSERT(0);
        break;
    case SLAPI_OPERATION_PARAMETERS:
        PR_ASSERT(0);
        break;
    case SLAPI_CONN_ID:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CONN_ID \n");
            return (-1);
        }
        pblock->pb_conn->c_connid = *((uint64_t *)value);
        break;
    case SLAPI_CONN_DN:
        /*
             * Slightly crazy but we must pass a copy of the current
             * authtype into bind_credentials_set() since it will
             * free the current authtype.
             */
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CONN_DN \n");
            return (-1);
        }
        slapi_pblock_get(pblock, SLAPI_CONN_AUTHMETHOD, &authtype);
        bind_credentials_set(pblock->pb_conn, authtype,
                             (char *)value, NULL, NULL, NULL, NULL);
        slapi_ch_free((void **)&authtype);
        break;
    case SLAPI_CONN_AUTHTYPE: /* deprecated */
    case SLAPI_CONN_AUTHMETHOD:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_set",
                          "Connection is NULL and hence cannot access SLAPI_CONN_AUTHMETHOD \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        slapi_ch_free((void **)&pblock->pb_conn->c_authtype);
        pblock->pb_conn->c_authtype = slapi_ch_strdup((char *)value);
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_CLIENTNETADDR_ACLIP:
        if (pblock->pb_conn == NULL) {
            break;
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        slapi_ch_free((void **)&pblock->pb_conn->cin_addr_aclip);
        pblock->pb_conn->cin_addr_aclip = (PRNetAddr *)value;
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;
    case SLAPI_CONN_IS_REPLICATION_SESSION:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_set",
                          "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n");
            return (-1);
        }
        pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
        pblock->pb_conn->c_isreplication_session = *((int *)value);
        pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
        break;

    /* stuff related to config file processing */
    case SLAPI_CONFIG_FILENAME:
    case SLAPI_CONFIG_LINENO:
    case SLAPI_CONFIG_ARGC:
    case SLAPI_CONFIG_ARGV:
        return (-1); /* deprecated since DS 5.0 (no longer useful) */

    /* pblock memory management */
    case SLAPI_DESTROY_CONTENT:
        _pblock_assert_pb_deprecated(pblock);
        pblock->pb_deprecated->pb_destroy_content = *((int *)value);
        break;

    /* stuff related to the current plugin */
    case SLAPI_PLUGIN:
        pblock->pb_plugin = (struct slapdplugin *)value;
        break;
    case SLAPI_PLUGIN_PRIVATE:
        pblock->pb_plugin->plg_private = (void *)value;
        break;
    case SLAPI_PLUGIN_TYPE:
        pblock->pb_plugin->plg_type = *((int *)value);
        break;
    case SLAPI_PLUGIN_ARGV:
        pblock->pb_plugin->plg_argv = (char **)value;
        break;
    case SLAPI_PLUGIN_ARGC:
        pblock->pb_plugin->plg_argc = *((int *)value);
        break;
    case SLAPI_PLUGIN_VERSION:
        pblock->pb_plugin->plg_version = (char *)value;
        break;
    case SLAPI_PLUGIN_PRECEDENCE:
        pblock->pb_plugin->plg_precedence = *((int *)value);
        break;
    case SLAPI_PLUGIN_OPRETURN:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_opreturn = *((int *)value);
        break;
    case SLAPI_PLUGIN_OBJECT:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_object = (void *)value;
        break;
    case SLAPI_PLUGIN_IDENTITY:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_plugin_identity = (void *)value;
        break;
    case SLAPI_PLUGIN_CONFIG_AREA:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_plugin_config_area = (char *)value;
        break;
    case SLAPI_PLUGIN_DESTROY_FN:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_destroy_fn = (IFP)value;
        break;
    case SLAPI_PLUGIN_DESCRIPTION:
        pblock->pb_plugin->plg_desc = *((Slapi_PluginDesc *)value);
        break;
    case SLAPI_PLUGIN_INTOP_RESULT:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_internal_op_result = *((int *)value);
        break;
    case SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_plugin_internal_search_op_entries = (Slapi_Entry **)value;
        break;
    case SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_plugin_internal_search_op_referrals = (char **)value;
        break;
    case SLAPI_REQUESTOR_DN:
        if (pblock->pb_op == NULL) {
            return (-1);
        }
        slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn), (char *)value);
        break;
    case SLAPI_REQUESTOR_SDN:
        if (pblock->pb_op == NULL) {
            return (-1);
        }
        slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn), slapi_sdn_get_dn((Slapi_DN *)value));
        break;
    /* database plugin functions */
    case SLAPI_PLUGIN_DB_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_bind = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_unbind = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_search = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_next_search_entry = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_next_search_entry_ext = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_search_results_release = (VFPP)value;
        break;
    case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_prev_search_results = (VFP)value;
        break;
    case SLAPI_PLUGIN_DB_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_compare = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_modify = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_modrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_add = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_delete = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_abandon = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_CONFIG_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_config = (IFP)value;
        break;
    case SLAPI_PLUGIN_CLOSE_FN:
        pblock->pb_plugin->plg_close = (IFP)value;
        break;
    case SLAPI_PLUGIN_CLEANUP_FN:
        pblock->pb_plugin->plg_cleanup = (IFP)value;
        break;
    case SLAPI_PLUGIN_START_FN:
        pblock->pb_plugin->plg_start = (IFP)value;
        break;
    case SLAPI_PLUGIN_POSTSTART_FN:
        pblock->pb_plugin->plg_poststart = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_WIRE_IMPORT_FN:
        pblock->pb_plugin->plg_wire_import = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_GET_INFO_FN:
        pblock->pb_plugin->plg_get_info = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_SET_INFO_FN:
        pblock->pb_plugin->plg_set_info = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_CTRL_INFO_FN:
        pblock->pb_plugin->plg_ctrl_info = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_SEQ_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_seq = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_ENTRY_FN:
        pblock->pb_plugin->plg_entry = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_REFERRAL_FN:
        pblock->pb_plugin->plg_referral = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_RESULT_FN:
        pblock->pb_plugin->plg_result = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_RMDB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_rmdb = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_LDIF2DB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_ldif2db = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_db2ldif = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_db2index = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_archive2db = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_db2archive = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDB_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_upgradedb = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_upgradednformat = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_DBVERIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_dbverify = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_BEGIN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_COMMIT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_ABORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort = (IFP)value;
        break;
    case SLAPI_PLUGIN_DB_TEST_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest = (IFP)value;
        break;
    /* database plugin-specific parameters */
    case SLAPI_PLUGIN_DB_NO_ACL:
        if (pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        if (NULL == pblock->pb_backend) {
            return (-1);
        }
        pblock->pb_backend->be_noacl = *((int *)value);
        break;
    case SLAPI_PLUGIN_DB_COMPACT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
            return (-1);
        }
        pblock->pb_plugin->plg_dbcompact = (IFP)value;
        break;

    /* extendedop plugin functions */
    case SLAPI_PLUGIN_EXT_OP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        pblock->pb_plugin->plg_exhandler = (IFP)value;
        break;
    case SLAPI_PLUGIN_EXT_OP_OIDLIST:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        pblock->pb_plugin->plg_exoids = (char **)value;
        ldapi_register_extended_op((char **)value);
        break;
    case SLAPI_PLUGIN_EXT_OP_NAMELIST:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        pblock->pb_plugin->plg_exnames = (char **)value;
        break;
    case SLAPI_PLUGIN_EXT_OP_BACKEND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
            pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
            return (-1);
        }
        pblock->pb_plugin->plg_be_exhandler = (IFP)value;
        break;

    /* preoperation plugin functions */
    case SLAPI_PLUGIN_PRE_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_prebind = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preunbind = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_presearch = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_precompare = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_premodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_premodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_predelete = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preabandon = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preentry = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_REFERRAL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_prereferral = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_RESULT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preresult = (IFP)value;
        break;
    case SLAPI_PLUGIN_PRE_EXTOP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREEXTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_preextop = (IFP)value;
        break;

    /* postoperation plugin functions */
    case SLAPI_PLUGIN_POST_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postbind = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_UNBIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postunbind = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_SEARCH_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postsearch = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postsearchfail = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_COMPARE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postcompare = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postmodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postmodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postdelete = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_ABANDON_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postabandon = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_ENTRY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postentry = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_REFERRAL_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postreferral = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_RESULT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postresult = (IFP)value;
        break;
    case SLAPI_PLUGIN_POST_EXTOP_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTEXTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_postextop = (IFP)value;
        break;

    /* backend preoperation plugin */
    case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepremodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepremodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepreadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepredelete = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepreclose = (IFP)value;
        break;

    /* backend postoperation plugin */
    case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostmodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostmodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostdelete = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_OPEN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostopen = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_EXPORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostexport = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_POST_IMPORT_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_bepostimport = (IFP)value;
        break;

    /* internal preoperation plugin */
    case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_pre_modify = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_pre_modrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_pre_add = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_pre_delete = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_pre_bind = (IFP)value;
        break;

    /* internal postoperation plugin */
    case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_post_modify = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_post_modrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_post_add = (IFP)value;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_internal_post_delete = (IFP)value;
        break;

    /* backend preoperation plugin - called just after creating transaction */
    case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpremodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpremodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpreadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpredelete = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpredeletetombstone = (IFP)value;
        break;

    /* backend postoperation plugin - called just before committing transaction */
    case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpostmodify = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpostmodrdn = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpostadd = (IFP)value;
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
            return (-1);
        }
        pblock->pb_plugin->plg_betxnpostdelete = (IFP)value;
        break;

    /* syntax plugin functions */
    case SLAPI_PLUGIN_SYNTAX_FILTER_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_filter_ava = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_FILTER_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_filter_sub = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_VALUES2KEYS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_values2keys = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_assertion2keys_ava = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_assertion2keys_sub = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_NAMES:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        PR_ASSERT(pblock->pb_plugin->plg_syntax_names == NULL);
        pblock->pb_plugin->plg_syntax_names = slapi_ch_array_dup((char **)value);
        break;
    case SLAPI_PLUGIN_SYNTAX_OID:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        PR_ASSERT(pblock->pb_plugin->plg_syntax_oid == NULL);
        pblock->pb_plugin->plg_syntax_oid = slapi_ch_strdup((char *)value);
        break;
    case SLAPI_PLUGIN_SYNTAX_FLAGS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_flags = *((int *)value);
        break;
    case SLAPI_PLUGIN_SYNTAX_COMPARE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_compare = (IFP)value;
        break;
    case SLAPI_SYNTAX_SUBSTRLENS: /* aka SLAPI_MR_SUBSTRLENS */
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_substrlens = (int *)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_VALIDATE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_validate = (IFP)value;
        break;
    case SLAPI_PLUGIN_SYNTAX_NORMALIZE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
            return (-1);
        }
        pblock->pb_plugin->plg_syntax_normalize = (VFPV)value;
        break;
    case SLAPI_ENTRY_PRE_OP:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_pre_op_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_ENTRY_POST_OP:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_post_op_entry = (Slapi_Entry *)value;
        break;

    /* target address for all operations */
    case SLAPI_TARGET_ADDRESS:
        PR_ASSERT(PR_FALSE); /* can't do this */
        break;
    case SLAPI_TARGET_DN: /* DEPRECATED */
        /* slapi_pblock_set(pb, SLAPI_TARGET_DN, val) automatically
         * replaces SLAPI_TARGET_SDN.  Caller should not free the
         * original SLAPI_TARGET_SDN, but the reset one here by getting
         * the address using slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn). */
        if (pblock->pb_op != NULL) {
            Slapi_DN *sdn = pblock->pb_op->o_params.target_address.sdn;
            slapi_sdn_free(&sdn);
            pblock->pb_op->o_params.target_address.sdn = slapi_sdn_new_dn_byval((char *)value);
        } else {
            return (-1);
        }
        break;
    case SLAPI_TARGET_SDN: /* alias from SLAPI_ADD_TARGET_SDN */
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.target_address.sdn = (Slapi_DN *)value;
        } else {
            return (-1);
        }
        break;
    case SLAPI_ORIGINAL_TARGET_DN:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.target_address.udn = (char *)value;
        }
        break;
    case SLAPI_TARGET_UNIQUEID:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.target_address.uniqueid = (char *)value;
        }
        break;
    case SLAPI_REQCONTROLS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.request_controls = (LDAPControl **)value;
        }
        break;
    case SLAPI_RESCONTROLS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.result_controls = (LDAPControl **)value;
        }
        break;
    case SLAPI_CONTROLS_ARG: /* used to pass control argument before operation is created */
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_ctrls_arg = (LDAPControl **)value;
        break;
    case SLAPI_ADD_RESCONTROL:
        if (pblock->pb_op != NULL) {
            add_control(&pblock->pb_op->o_results.result_controls, (LDAPControl *)value);
        }
        break;

    /* notes to be added to the access log RESULT line for this op. */
    case SLAPI_OPERATION_NOTES:
        _pblock_assert_pb_intop(pblock);
        if (value == NULL) {
            pblock->pb_intop->pb_operation_notes = 0; /* cleared */
        } else {
            pblock->pb_intop->pb_operation_notes |= *((unsigned int *)value);
        }
        break;
    case SLAPI_SKIP_MODIFIED_ATTRS:
        if (pblock->pb_op == NULL)
            break;
        if (value == 0) {
            pblock->pb_op->o_flags &= ~OP_FLAG_SKIP_MODIFIED_ATTRS;
        } else {
            pblock->pb_op->o_flags |= OP_FLAG_SKIP_MODIFIED_ATTRS;
        }
        break;
    /* controls we know about */
    case SLAPI_MANAGEDSAIT:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_managedsait = *((int *)value);
        break;
    case SLAPI_PWPOLICY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_pwpolicy_ctrl = *((int *)value);
        break;

    /* add arguments */
    case SLAPI_ADD_ENTRY:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_add.target_entry = (Slapi_Entry *)value;
        }
        break;
    case SLAPI_ADD_EXISTING_DN_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_existing_dn_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_ADD_EXISTING_UNIQUEID_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_existing_uniqueid_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_ADD_PARENT_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_parent_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_ADD_PARENT_UNIQUEID:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_add.parentuniqueid = (char *)value;
        }
        break;

    /* bind arguments */
    case SLAPI_BIND_METHOD:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_bind.bind_method = *((ber_tag_t *)value);
        }
        break;
    case SLAPI_BIND_CREDENTIALS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_bind.bind_creds = (struct berval *)value;
        }
        break;
    case SLAPI_BIND_SASLMECHANISM:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_bind.bind_saslmechanism = (char *)value;
        }
        break;
    /* bind return values */
    case SLAPI_BIND_RET_SASLCREDS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds = (struct berval *)value;
        }
        break;

    /* compare arguments */
    case SLAPI_COMPARE_TYPE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type = (char *)value;
        }
        break;
    case SLAPI_COMPARE_VALUE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value = *((struct berval *)value);
        }
        break;

    /* modify arguments */
    case SLAPI_MODIFY_MODS:
        PR_ASSERT(pblock->pb_op);
        if (pblock->pb_op != NULL) {
            if (pblock->pb_op->o_params.operation_type == SLAPI_OPERATION_MODIFY) {
                pblock->pb_op->o_params.p.p_modify.modify_mods = (LDAPMod **)value;
            } else if (pblock->pb_op->o_params.operation_type == SLAPI_OPERATION_MODRDN) {
                pblock->pb_op->o_params.p.p_modrdn.modrdn_mods = (LDAPMod **)value;
            } else {
                PR_ASSERT(0);
            }
        }
        break;

    /* modrdn arguments */
    case SLAPI_MODRDN_NEWRDN:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn = (char *)value;
        }
        break;
    case SLAPI_MODRDN_DELOLDRDN:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn = *((int *)value);
        }
        break;
    case SLAPI_MODRDN_NEWSUPERIOR: /* DEPRECATED */
        if (pblock->pb_op != NULL) {
            Slapi_DN *sdn =
                pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
            slapi_sdn_free(&sdn);
            pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn =
                slapi_sdn_new_dn_byval((char *)value);
        } else {
            return -1;
        }
        break;
    case SLAPI_MODRDN_NEWSUPERIOR_SDN:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn =
                (Slapi_DN *)value;
        } else {
            return -1;
        }
        break;
    case SLAPI_MODRDN_PARENT_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_parent_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_MODRDN_NEWPARENT_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_newparent_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_MODRDN_TARGET_ENTRY:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_target_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_MODRDN_NEWSUPERIOR_ADDRESS:
        PR_ASSERT(PR_FALSE); /* can't do this */
        break;
    /* search arguments */
    case SLAPI_SEARCH_SCOPE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_scope = *((int *)value);
        }
        break;
    case SLAPI_SEARCH_DEREF:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_deref = *((int *)value);
        }
        break;
    case SLAPI_SEARCH_SIZELIMIT:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_sizelimit = *((int *)value);
        }
        break;
    case SLAPI_SEARCH_TIMELIMIT:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_timelimit = *((int *)value);
        }
        break;
    case SLAPI_SEARCH_FILTER:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_filter = (struct slapi_filter *)value;
            /* Prevent UAF by reseting this on set. */
            pblock->pb_op->o_params.p.p_search.search_filter_intended = NULL;
        }
        break;
    case SLAPI_SEARCH_FILTER_INTENDED:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_filter_intended = (struct slapi_filter *)value;
        }
        break;
    case SLAPI_SEARCH_STRFILTER:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_strfilter = (char *)value;
        }
        break;
    case SLAPI_SEARCH_ATTRS:
        if (pblock->pb_op != NULL) {
#if defined(USE_OLD_UNHASHED)
            char **attrs;
            for (attrs = (char **)value; attrs && *attrs; attrs++) {
                /* Get rid of forbidden attr, e.g.,
                 * PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                 * which never be returned. */
                if (is_type_forbidden(*attrs)) {
                    char **ptr;
                    for (ptr = attrs; ptr && *ptr; ptr++) {
                        if (ptr == attrs) {
                            slapi_ch_free_string(ptr); /* free unhashed type */
                        }
                        *ptr = *(ptr + 1); /* attrs is NULL terminated;
                                              the NULL is copied here. */
                    }
                }
            }
#endif
            pblock->pb_op->o_params.p.p_search.search_attrs = (char **)value;
        }
        break;
    case SLAPI_SEARCH_GERATTRS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_gerattrs = (char **)value;
        }
        break;
    case SLAPI_SEARCH_REQATTRS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_searchattrs = (char **)value;
        }
        break;
    case SLAPI_SEARCH_ATTRSONLY:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_attrsonly = *((int *)value);
        }
        break;
    case SLAPI_SEARCH_IS_AND:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_search.search_is_and = *((int *)value);
        }
        break;

    /* abandon operation arguments */
    case SLAPI_ABANDON_MSGID:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid = *((int *)value);
        }
        break;

    /* extended operation arguments */
    case SLAPI_EXT_OP_REQ_OID:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_extended.exop_oid = (char *)value;
        }
        break;
    case SLAPI_EXT_OP_REQ_VALUE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_params.p.p_extended.exop_value = (struct berval *)value;
        }
        break;
    /* extended operation return values */
    case SLAPI_EXT_OP_RET_OID:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_extended.exop_ret_oid = (char *)value;
        }
        break;
    case SLAPI_EXT_OP_RET_VALUE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_extended.exop_ret_value = (struct berval *)value;
        }
        break;

    /* matching rule plugin functions */
    case SLAPI_PLUGIN_MR_FILTER_CREATE_FN:
        SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
        pblock->pb_plugin->plg_mr_filter_create = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_INDEXER_CREATE_FN:
        SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
        pblock->pb_plugin->plg_mr_indexer_create = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_FILTER_MATCH_FN:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->filter_match_fn = (mrFilterMatchFn)value;
        break;
    case SLAPI_PLUGIN_MR_FILTER_INDEX_FN:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->filter_index_fn = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_FILTER_RESET_FN:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->filter_reset_fn = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_INDEX_FN:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->index_fn = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_INDEX_SV_FN:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->index_sv_fn = (IFP)value;
        break;

    /* matching rule plugin arguments */
    case SLAPI_PLUGIN_MR_OID:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->oid = (char *)value;
        break;
    case SLAPI_PLUGIN_MR_TYPE:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->type = (char *)value;
        break;
    case SLAPI_PLUGIN_MR_VALUE:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->value = (struct berval *)value;
        break;
    case SLAPI_PLUGIN_MR_VALUES:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->values = (struct berval **)value;
        break;
    case SLAPI_PLUGIN_MR_KEYS:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->keys = (struct berval **)value;
        break;
    case SLAPI_PLUGIN_MR_FILTER_REUSABLE:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->filter_reusable = *(unsigned int *)value;
        break;
    case SLAPI_PLUGIN_MR_QUERY_OPERATOR:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->query_operator = *(int *)value;
        break;
    case SLAPI_PLUGIN_MR_USAGE:
        _pblock_assert_pb_mr(pblock);
        pblock->pb_mr->usage = *(unsigned int *)value;
        break;

    /* new style matching rule syntax plugin functions */
    case SLAPI_PLUGIN_MR_FILTER_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_filter_ava = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_FILTER_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_filter_sub = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_VALUES2KEYS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_values2keys = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_assertion2keys_ava = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_assertion2keys_sub = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_FLAGS:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_flags = *((int *)value);
        break;
    case SLAPI_PLUGIN_MR_NAMES:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        PR_ASSERT(pblock->pb_plugin->plg_mr_names == NULL);
        pblock->pb_plugin->plg_mr_names = slapi_ch_array_dup((char **)value);
        break;
    case SLAPI_PLUGIN_MR_COMPARE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_compare = (IFP)value;
        break;
    case SLAPI_PLUGIN_MR_NORMALIZE:
        if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
            return (-1);
        }
        pblock->pb_plugin->plg_mr_normalize = (VFPV)value;
        break;

    /* seq arguments */
    case SLAPI_SEQ_TYPE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->seq_type = *((int *)value);
        break;
    case SLAPI_SEQ_ATTRNAME:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->seq_attrname = (char *)value;
        break;
    case SLAPI_SEQ_VAL:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->seq_val = (char *)value;
        break;

    /* ldif2db arguments */
    case SLAPI_LDIF2DB_FILE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_files = (char **)value;
        break;
    case SLAPI_LDIF2DB_REMOVEDUPVALS:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->removedupvals = *((int *)value);
        break;
    case SLAPI_DB2INDEX_ATTRS:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->db2index_attrs = (char **)value;
        break;
    case SLAPI_LDIF2DB_NOATTRINDEXES:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif2db_noattrindexes = *((int *)value);
        break;
    case SLAPI_LDIF2DB_INCLUDE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_include = (char **)value;
        break;
    case SLAPI_LDIF2DB_EXCLUDE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_exclude = (char **)value;
        break;
    case SLAPI_LDIF2DB_GENERATE_UNIQUEID:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_generate_uniqueid = *((int *)value);
        break;
    case SLAPI_LDIF2DB_NAMESPACEID:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_namespaceid = (char *)value;
        break;

    /* db2ldif arguments */
    case SLAPI_DB2LDIF_PRINTKEY:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_printkey = *((int *)value);
        break;
    case SLAPI_DB2LDIF_DUMP_UNIQUEID:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_dump_uniqueid = *((int *)value);
        break;
    case SLAPI_DB2LDIF_FILE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_file = (char *)value;
        break;

    /* db2ldif/ldif2db/db2bak/bak2db arguments */
    case SLAPI_BACKEND_INSTANCE_NAME:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->instance_name = (char *)value;
        break;
    case SLAPI_BACKEND_TASK:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->task = (Slapi_Task *)value;
        break;
    case SLAPI_TASK_FLAGS:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->task_flags = *((int *)value);
        break;
    case SLAPI_DB2LDIF_SERVER_RUNNING:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->server_running = *((int *)value);
        break;
    case SLAPI_BULK_IMPORT_ENTRY:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->import_entry = (Slapi_Entry *)value;
        break;
    case SLAPI_BULK_IMPORT_STATE:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->import_state = *((int *)value);
        break;

    case SLAPI_LDIF_CHANGELOG:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_include_changelog = *((int *)value);
        break;

    case SLAPI_LDIF2DB_ENCRYPT:
    case SLAPI_DB2LDIF_DECRYPT:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->ldif_encrypt = *((int *)value);
        break;
    /* dbverify */
    case SLAPI_DBVERIFY_DBDIR:
        _pblock_assert_pb_task(pblock);
        pblock->pb_task->dbverify_dbdir = (char *)value;
        break;


    /* transaction arguments */
    case SLAPI_PARENT_TXN:
        _pblock_assert_pb_deprecated(pblock);
        pblock->pb_deprecated->pb_parent_txn = (void *)value;
        break;
    case SLAPI_TXN:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_txn = (void *)value;
        break;
    case SLAPI_TXN_RUV_MODS_FN:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_txn_ruv_mods_fn = (IFP)value;
        break;

    /* Search results set */
    case SLAPI_SEARCH_RESULT_SET:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.search_result_set = (void *)value;
        }
        break;
    /* estimated search result set size */
    case SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.estimate = *(int *)value;
        }
        break;
    /* Search result - entry returned from iterating over result set */
    case SLAPI_SEARCH_RESULT_ENTRY:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.search_result_entry = (void *)value;
        }
        break;
    case SLAPI_SEARCH_RESULT_ENTRY_EXT:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.opaque_backend_ptr = (void *)value;
        }
        break;
    /* Number of entries returned from search */
    case SLAPI_NENTRIES:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.nentries = *((int *)value);
        }
        break;
    /* Referrals encountered while iterating over the result set */
    case SLAPI_SEARCH_REFERRALS:
        if (pblock->pb_op != NULL) {
            pblock->pb_op->o_results.r.r_search.search_referrals = (struct berval **)value;
        }
        break;

    case SLAPI_RESULT_CODE:
        if (pblock->pb_op != NULL)
            pblock->pb_op->o_results.result_code = (*(int *)value);
        break;
    case SLAPI_RESULT_MATCHED:
        if (pblock->pb_op != NULL)
            pblock->pb_op->o_results.result_matched = (char *)value;
        break;
    case SLAPI_RESULT_TEXT:
        if (pblock->pb_op != NULL)
            pblock->pb_op->o_results.result_text = (char *)value;
        break;
    case SLAPI_PB_RESULT_TEXT:
        _pblock_assert_pb_intop(pblock);
        slapi_ch_free((void **)&(pblock->pb_intop->pb_result_text));
        pblock->pb_intop->pb_result_text = slapi_ch_strdup((char *)value);
        break;

    /* Size of the database, in kb */
    case SLAPI_DBSIZE:
        _pblock_assert_pb_misc(pblock);
        pblock->pb_misc->pb_dbsize = *((unsigned int *)value);
        break;

    /* ACL Plugin */
    case SLAPI_PLUGIN_ACL_INIT:
        pblock->pb_plugin->plg_acl_init = (IFP)value;
        break;

    case SLAPI_PLUGIN_ACL_SYNTAX_CHECK:
        pblock->pb_plugin->plg_acl_syntax_check = (IFP)value;
        break;
    case SLAPI_PLUGIN_ACL_ALLOW_ACCESS:
        pblock->pb_plugin->plg_acl_access_allowed = (IFP)value;
        break;
    case SLAPI_PLUGIN_ACL_MODS_ALLOWED:
        pblock->pb_plugin->plg_acl_mods_allowed = (IFP)value;
        break;
    case SLAPI_PLUGIN_ACL_MODS_UPDATE:
        pblock->pb_plugin->plg_acl_mods_update = (IFP)value;
        break;
    /* MMR Plugin */
    case SLAPI_PLUGIN_MMR_BETXN_PREOP:
	pblock->pb_plugin->plg_mmr_betxn_preop = (IFP) value;
	break;
    case SLAPI_PLUGIN_MMR_BETXN_POSTOP:
	pblock->pb_plugin->plg_mmr_betxn_postop = (IFP) value;
	break;

    case SLAPI_CLIENT_DNS:
        if (pblock->pb_conn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n");
            return (-1);
        }
        pblock->pb_conn->c_domain = *((struct berval ***)value);
        break;
    /* Command line arguments */
    case SLAPI_ARGC:
        _pblock_assert_pb_misc(pblock);
        pblock->pb_misc->pb_slapd_argc = *((int *)value);
        break;
    case SLAPI_ARGV:
        _pblock_assert_pb_misc(pblock);
        pblock->pb_misc->pb_slapd_argv = *((char ***)value);
        break;

    /* Config file directory */
    case SLAPI_CONFIG_DIRECTORY:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_slapd_configdir = (char *)value;
        break;

    /* password storage scheme (kexcoff) */
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME:
        if (pblock->pb_plugin->plg_pwdstorageschemename != NULL) {
            /* Free the old name. */
            slapi_ch_free_string(&pblock->pb_plugin->plg_pwdstorageschemename);
        }
        pblock->pb_plugin->plg_pwdstorageschemename = slapi_ch_strdup((char *)value);
        break;
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD:
        _pblock_assert_pb_deprecated(pblock);
        pblock->pb_deprecated->pb_pwd_storage_scheme_user_passwd = (char *)value;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD:
        _pblock_assert_pb_deprecated(pblock);
        pblock->pb_deprecated->pb_pwd_storage_scheme_db_passwd = (char *)value;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN:
        pblock->pb_plugin->plg_pwdstorageschemeenc = (CFP)value;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN:
        pblock->pb_plugin->plg_pwdstorageschemedec = (IFP)value;
        break;

    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN:
        /* must provide a comparison function */
        if (value == NULL) {
            return (-1);
        }
        pblock->pb_plugin->plg_pwdstorageschemecmp = (IFP)value;
        break;

    /* entry fetch store */
    case SLAPI_PLUGIN_ENTRY_FETCH_FUNC:
        pblock->pb_plugin->plg_entryfetchfunc = (IFP)value;
        break;

    case SLAPI_PLUGIN_ENTRY_STORE_FUNC:
        pblock->pb_plugin->plg_entrystorefunc = (IFP)value;
        break;

    case SLAPI_PLUGIN_ENABLED:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_plugin_enabled = *((int *)value);
        break;

    /* DSE add parameters */
    case SLAPI_DSE_DONT_WRITE_WHEN_ADDING:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->dont_add_write = *((int *)value);
        break;

    /* DSE add parameters */
    case SLAPI_DSE_MERGE_WHEN_ADDING:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->add_merge = *((int *)value);
        break;

    /* DSE add parameters */
    case SLAPI_DSE_DONT_CHECK_DUPS:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->dont_check_dups = *((int *)value);
        break;

    /* DSE modify parameters */
    case SLAPI_DSE_REAPPLY_MODS:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->reapply_mods = *((int *)value);
        break;

    /* DSE read parameters */
    case SLAPI_DSE_IS_PRIMARY_FILE:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->is_primary_file = *((int *)value);
        break;

    /* used internally by schema code (schema.c) */
    case SLAPI_SCHEMA_FLAGS:
        _pblock_assert_pb_dse(pblock);
        pblock->pb_dse->schema_flags = *((int *)value);
        break;

    case SLAPI_URP_NAMING_COLLISION_DN:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_urp_naming_collision_dn = (char *)value;
        break;

    case SLAPI_URP_TOMBSTONE_CONFLICT_DN:
        pblock->pb_intop->pb_urp_tombstone_conflict_dn = (char *)value;
        break;

    case SLAPI_URP_TOMBSTONE_UNIQUEID:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_urp_tombstone_uniqueid = (char *)value;
        break;

    case SLAPI_SEARCH_CTRLS:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_search_ctrls = (LDAPControl **)value;
        break;

    case SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_syntax_filter_normalized = *((int *)value);
        break;

    case SLAPI_PLUGIN_SYNTAX_FILTER_DATA:
        _pblock_assert_pb_intplugin(pblock);
        pblock->pb_intplugin->pb_syntax_filter_data = (void *)value;
        break;

    case SLAPI_PAGED_RESULTS_INDEX:
        _pblock_assert_pb_intop(pblock);
        pblock->pb_intop->pb_paged_results_index = *(int *)value;
        break;

    case SLAPI_PAGED_RESULTS_COOKIE:
        pblock->pb_intop->pb_paged_results_cookie = *(int *)value;
        break;

    case SLAPI_USN_INCREMENT_FOR_TOMBSTONE:
        pblock->pb_intop->pb_usn_tombstone_incremented = *((int32_t *)value);
        break;

    /* ACI Target Check */
    case SLAPI_ACI_TARGET_CHECK:
        _pblock_assert_pb_misc(pblock);
        pblock->pb_misc->pb_aci_target_check = *((int *)value);
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pblock_set",
                      "Unknown parameter block argument %d\n", arg);
        PR_ASSERT(0);
        return (-1);
    }

    return (0);
}

int
slapi_is_ldapi_conn(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    // MAKE THIS BETTER.
    pblock_analytics_record(pb, SLAPI_CONNECTION);
#endif
    if (pb && pb->pb_conn) {
        return pb->pb_conn->c_unix_local;
    } else {
        return 0;
    }
}

/*
 * Clears (and free's as appropriate) the bind DN and related credentials
 * for the connection `conn'.
 *
 * If `lock_conn' is true, 'conn' is locked before touching it; otherwise
 * this function assumes that conn->c_mutex is ALREADY locked.
 *
 * If `clear_externalcreds' is true, the external DN, external authtype,
 * and client certificate are also cleared and free'd.
 *
 * Connection structure members that are potentially changed by this function:
 *        c_dn, c_isroot, c_authtype
 *        c_external_dn, c_external_authtype, c_client_cert
 *
 * This function might better belong on bind.c or perhaps connection.c but
 * it needs to be in libslapd because FE code and libslapd code calls it.
 */
void
bind_credentials_clear(Connection *conn, PRBool lock_conn, PRBool clear_externalcreds)
{
    if (lock_conn) {
        pthread_mutex_lock(&(conn->c_mutex));
    }

    if (conn->c_dn != NULL) {                   /* a non-anonymous bind has occurred */
        reslimit_update_from_entry(conn, NULL); /* clear resource limits */

        if (conn->c_dn != conn->c_external_dn) {
            slapi_ch_free((void **)&conn->c_dn);
        }
        conn->c_dn = NULL;
    }
    slapi_ch_free((void **)&conn->c_authtype);
    conn->c_isroot = 0;
    conn->c_authtype = slapi_ch_strdup(SLAPD_AUTH_NONE);

    if (clear_externalcreds) {
        slapi_ch_free((void **)&conn->c_external_dn);
        conn->c_external_dn = NULL;
        conn->c_external_authtype = SLAPD_AUTH_NONE;
        if (conn->c_client_cert) {
            CERT_DestroyCertificate(conn->c_client_cert);
            conn->c_client_cert = NULL;
        }
    }

    if (lock_conn) {
        pthread_mutex_unlock(&(conn->c_mutex));
    }
}

struct slapi_entry *
slapi_pblock_get_pw_entry(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_PW_ENTRY);
#endif
    if (pb->pb_intop != NULL) {
        return pb->pb_intop->pb_pw_entry;
    }
    return NULL;
}

void
slapi_pblock_set_pw_entry(Slapi_PBlock *pb, struct slapi_entry *entry)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_PW_ENTRY);
#endif
    _pblock_assert_pb_intop(pb);
    pb->pb_intop->pb_pw_entry = entry;
}

passwdPolicy *
slapi_pblock_get_pwdpolicy(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_PWDPOLICY);
#endif
    if (pb->pb_intop != NULL) {
        return pb->pb_intop->pwdpolicy;
    }
    return NULL;
}

void
slapi_pblock_set_pwdpolicy(Slapi_PBlock *pb, passwdPolicy *pwdpolicy)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_PWDPOLICY);
#endif
    _pblock_assert_pb_intop(pb);
    pb->pb_intop->pwdpolicy = pwdpolicy;
}

int32_t
slapi_pblock_get_ldif_dump_replica(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_LDIF_DUMP_REPLICA);
#endif
    if (pb->pb_task != NULL) {
        return pb->pb_task->ldif_dump_replica;
    }
    return 0;
}

void
slapi_pblock_set_ldif_dump_replica(Slapi_PBlock *pb, int32_t dump_replica)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_LDIF_DUMP_REPLICA);
#endif
    _pblock_assert_pb_task(pb);
    pb->pb_task->ldif_dump_replica = dump_replica;
}

int32_t
slapi_pblock_get_task_warning(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_TASK_WARNING);
#endif
    if (pb->pb_task != NULL) {
        return pb->pb_task->task_warning;
    }
    return 0;
}

void
slapi_pblock_set_task_warning(Slapi_PBlock *pb, task_warning warning)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_TASK_WARNING);
#endif
    _pblock_assert_pb_task(pb);
    pb->pb_task->task_warning = warning;
}

void *
slapi_pblock_get_vattr_context(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_VATTR_CONTEXT);
#endif
    if (pb->pb_intplugin != NULL) {
        return pb->pb_intplugin->pb_vattr_context;
    }
    return NULL;
}

void
slapi_pblock_set_vattr_context(Slapi_PBlock *pb, void *vattr_ctx)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_VATTR_CONTEXT);
#endif
    _pblock_assert_pb_intplugin(pb);
    pb->pb_intplugin->pb_vattr_context = vattr_ctx;
}

void *
slapi_pblock_get_op_stack_elem(Slapi_PBlock *pb)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_OP_STACK_ELEM);
#endif
    if (pb->pb_intop != NULL) {
        return pb->pb_intop->op_stack_elem;
    }
    return NULL;
}

void
slapi_pblock_set_op_stack_elem(Slapi_PBlock *pb, void *stack_elem)
{
#ifdef PBLOCK_ANALYTICS
    pblock_analytics_record(pb, SLAPI_OP_STACK_ELEM);
#endif
    _pblock_assert_pb_intop(pb);
    pb->pb_intop->op_stack_elem = stack_elem;
}

uint32_t
slapi_pblock_get_operation_notes(Slapi_PBlock *pb) {
    if (pb->pb_intop != NULL) {
        return pb->pb_intop->pb_operation_notes;
    }
    return 0;
}

/* overwrite the flag */
void
slapi_pblock_set_operation_notes(Slapi_PBlock *pb, uint32_t opnotes) {
    _pblock_assert_pb_intop(pb);
    pb->pb_intop->pb_operation_notes = opnotes;
}

/* ensure flag is set */
void
slapi_pblock_set_flag_operation_notes(Slapi_PBlock *pb, uint32_t opflag) {
    _pblock_assert_pb_intop(pb);
    pb->pb_intop->pb_operation_notes |= opflag;
}

/* Set result text if it's NULL */
void
slapi_pblock_set_result_text_if_empty(Slapi_PBlock *pb, char *text) {
    _pblock_assert_pb_intop(pb);
    if (pb->pb_intop->pb_result_text == NULL) {
        pb->pb_intop->pb_result_text = slapi_ch_strdup(text);
    }
}

/*
 * Clear and then set the bind DN and related credentials for the
 * connection `conn'.
 *
 * `authtype' should be one of the SLAPD_AUTH_... constants defined in
 * slapu-plugin.h or NULL.
 *
 * `normdn' must be a normalized DN and it must be malloc'd memory (it
 * is consumed by this function).  If there is an existing DN value
 * associated with the connection, it is free'd.  Pass NULL for `normdn'
 * to clear the DN.
 *
 * If `extauthtype' is non-NULL we also clear and then set the
 * external (e.g., SSL) credentials from the `externaldn' and `clientcert'.
 * Note that it is okay for `externaldn' and 'normdn' to have the same
 * (pointer) value.  This code and that in bind_credentials_clear()
 * is smart enough to know to only free the memory once.  Like `normdn',
 * `externaldn' and `clientcert' should be NULL or point to malloc'd memory
 * as they are both consumed by this function.
 *
 * We also:
 *
 *   1) Test to see if the DN is the root DN and set the c_isroot flag
 *        appropriately.
 * And
 *
 *   2) Call the binder-based resource limits subsystem so it can
 *        update the per-connection resource limit data it maintains.
 *
 * Note that this function should ALWAYS be used instead of manipulating
 * conn->c_dn directly; otherwise, subsystems like the binder-based resource
 * limits (see resourcelimit.c) won't be called.
 *
 * It is also acceptable to set the DN via a call slapi_pblock_set(), e.g.,
 *
 *            slapi_pblock_set( pb, SLAPI_CONN_DN, ndn );
 *
 * because it calls this function.
 *
 * Connection structure members that are potentially changed by this function:
 *        c_dn, c_isroot, c_authtype
 *        c_external_dn, c_external_authtype, c_client_cert
 *
 * This function might better belong on bind.c or perhaps connection.c but
 * it needs to be in libslapd because FE code and libslapd code calls it.
 */
void
bind_credentials_set(Connection *conn, char *authtype, char *normdn, char *extauthtype, char *externaldn, CERTCertificate *clientcert, Slapi_Entry *bind_target_entry)
{
    pthread_mutex_lock(&(conn->c_mutex));
    bind_credentials_set_nolock(conn, authtype, normdn,
                                extauthtype, externaldn, clientcert, bind_target_entry);
    pthread_mutex_unlock(&(conn->c_mutex));
}

void
bind_credentials_set_nolock(Connection *conn, char *authtype, char *normdn, char *extauthtype, char *externaldn, CERTCertificate *clientcert, Slapi_Entry *bind_target_entry)
{
    slapdFrontendConfig_t *fecfg = getFrontendConfig();
    int idletimeout = 0;

    /* clear credentials */
    bind_credentials_clear(conn, PR_FALSE /* conn is already locked */,
                           (extauthtype != NULL) /* clear external creds. if requested */);

    /* set primary credentials */
    slapi_ch_free((void **)&conn->c_authtype);
    conn->c_authtype = slapi_ch_strdup(authtype);
    conn->c_dn = normdn;
    conn->c_isroot = slapi_dn_isroot(normdn);

    /* Set the thread data with the normalized dn */
    slapi_td_set_dn(slapi_ch_strdup(normdn));

    /* set external credentials if requested */
    if (extauthtype != NULL) {
        conn->c_external_authtype = extauthtype;
        conn->c_external_dn = externaldn;
        conn->c_client_cert = clientcert;
    }

    /* notify binder-based resource limit subsystem about the change in DN */
    if (!conn->c_isroot) {
        if (conn->c_dn != NULL) {
            if (bind_target_entry == NULL) {
                Slapi_DN *sdn = slapi_sdn_new_normdn_byref(conn->c_dn);
                reslimit_update_from_dn(conn, sdn);
                slapi_sdn_free(&sdn);
            } else {
                reslimit_update_from_entry(conn, bind_target_entry);
            }
        } else {
            char *anon_dn = config_get_anon_limits_dn();
            /* If an anonymous limits dn is set, use it to set the limits. */
            if (anon_dn && (strlen(anon_dn) > 0)) {
                Slapi_DN *anon_sdn = slapi_sdn_new_normdn_byref(anon_dn);
                reslimit_update_from_dn(conn, anon_sdn);
                slapi_sdn_free(&anon_sdn);
            }

            slapi_ch_free_string(&anon_dn);
        }
        if (slapi_reslimit_get_integer_limit(conn, conn->c_idletimeout_handle,
                                             &idletimeout) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
            conn->c_idletimeout = fecfg->idletimeout;
        } else {
            conn->c_idletimeout = idletimeout;
        }
    } else {
        /* For root dn clear about the resource limits */
        reslimit_update_from_entry(conn, NULL);
        conn->c_idletimeout = 0;
    }
}
