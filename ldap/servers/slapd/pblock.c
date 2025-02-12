/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005-2025 Red Hat, Inc.
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
        slapi_ch_free_string(&pb->pb_intop->pb_session_tracking_id);
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

static int32_t
slapi_pblock_get_backend(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    (*(Slapi_Backend **)value) = be;
    return 0;
}

static int32_t
slapi_pblock_get_backend_count(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_misc != NULL) {
        (*(int *)value) = pblock->pb_misc->pb_backend_count;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_be_type(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    if (NULL == be) {
        return (-1);
    }
    (*(char **)value) = be->be_type;
    return 0;
}

static int32_t
slapi_pblock_get_be_readonly(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    if (NULL == be) {
        (*(int *)value) = 0; /* default value */
    } else {
        (*(int *)value) = be->be_readonly;
    }
    return 0;
}

static int32_t
slapi_pblock_get_be_lastmod(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    if (NULL == be) {
        (*(int *)value) = (g_get_global_lastmod() == LDAP_ON);
    } else {
        (*(int *)value) = (be->be_lastmod == LDAP_ON || (be->be_lastmod == LDAP_UNDEFINED && g_get_global_lastmod() == LDAP_ON));
    }
    return 0;
}

static int32_t
slapi_pblock_get_connection(Slapi_PBlock *pblock, void *value)
{
    (*(Connection **)value) = pblock->pb_conn;
    return 0;
}

static int32_t
slapi_pblock_get_conn_id(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_ID \n");
        return (-1);
    }
    (*(uint64_t *)value) = pblock->pb_conn->c_connid;
    return 0;
}

static int32_t
slapi_pblock_get_conn_dn(Slapi_PBlock *pblock, void *value)
{
    /*
     * NOTE: we have to make a copy of this that the caller
     * is responsible for freeing. otherwise, they would get
     * a pointer that could be freed out from under them.
     */
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_DN \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(char **)value) = (NULL == pblock->pb_conn->c_dn ? NULL : slapi_ch_strdup(pblock->pb_conn->c_dn));
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_deferred_memberof(Slapi_PBlock *pblock, void *value)
{
    (*(int *)value) = pblock->pb_deferred_memberof;
    return 0;
}

static int32_t
slapi_pblock_get_conn_authmethod(Slapi_PBlock *pblock, void *value)
{
    /* returns a copy */
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get",
                      "Connection is NULL and hence cannot access SLAPI_CONN_AUTHMETHOD \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(char **)value) = pblock->pb_conn->c_authtype ? slapi_ch_strdup(pblock->pb_conn->c_authtype) : NULL;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_clientnetaddr_aclip(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(PRNetAddr **) value) = pblock->pb_conn->cin_addr_aclip;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_clientnetaddr(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        memset(value, 0, sizeof(PRNetAddr));
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    if (pblock->pb_conn->cin_addr == NULL) {
        memset(value, 0, sizeof(PRNetAddr));
    } else {
        (*(PRNetAddr *)value) = *(pblock->pb_conn->cin_addr);
    }
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_servernetaddr(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        memset(value, 0, sizeof(PRNetAddr));
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    if (pblock->pb_conn->cin_destaddr == NULL) {
        memset(value, 0, sizeof(PRNetAddr));
    } else {
        (*(PRNetAddr *)value) = *(pblock->pb_conn->cin_destaddr);
    }
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_is_replication_session(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get",
                      "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(int *)value) = pblock->pb_conn->c_isreplication_session;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_is_ssl_session(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_IS_SSL_SESSION \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(int *)value) = pblock->pb_conn->c_flags & CONN_FLAG_SSL;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_sasl_ssf(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_SASL_SSF \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(int *)value) = pblock->pb_conn->c_sasl_ssf;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_ssl_ssf(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_SSL_SSF \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(int *)value) = pblock->pb_conn->c_ssl_ssf;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_local_ssf(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_LOCAL_SSF \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    (*(int *)value) = pblock->pb_conn->c_local_ssf;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_cert(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_CERT \n");
        return (-1);
    }
    (*(CERTCertificate **)value) = pblock->pb_conn->c_client_cert;
    return 0;
}

static int32_t
slapi_pblock_get_operation(Slapi_PBlock *pblock, void *value)
{
    (*(Operation **)value) = pblock->pb_op;
    return 0;
}

static int32_t
slapi_pblock_get_operation_type(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Operation is NULL and hence cannot access SLAPI_OPERATION_TYPE \n");
        return (-1);
    }
    (*(int *)value) = pblock->pb_op->o_params.operation_type;
    return 0;
}

static int32_t
slapi_pblock_get_opinitiated_time(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Operation is NULL and hence cannot access SLAPI_OPINITIATED_TIME \n");
        return (-1);
    }
    (*(time_t *)value) = pblock->pb_op->o_hr_time_utc.tv_sec;
    return 0;
}

static int32_t
slapi_pblock_get_requestor_isroot(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int *)value) = pblock->pb_intop->pb_requestor_isroot;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_skip_modified_attrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        (*(int *)value) = 0; /* No Operation -> No skip */
    } else {
        (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_SKIP_MODIFIED_ATTRS);
    }
    return 0;
}

static int32_t
slapi_pblock_get_is_replicated_operation(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        (*(int *)value) = 0; /* No Operation -> Not Replicated */
    } else {
        (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_REPLICATED);
    }
    return 0;
}

static int32_t
slapi_pblock_get_is_mmr_replicated_operation(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        (*(int *)value) = 0; /* No Operation -> Not Replicated */
    } else {
        (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_REPLICATED);
    }
    return 0;
}

static int32_t
slapi_pblock_get_operation_parameters(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct slapi_operation_parameters **)value) = &pblock->pb_op->o_params;
    }
    return 0;
}

static int32_t
slapi_pblock_get_destroy_content(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_deprecated != NULL) {
        (*(int *)value) = pblock->pb_deprecated->pb_destroy_content;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin(Slapi_PBlock *pblock, void *value)
{
    (*(struct slapdplugin **)value) = pblock->pb_plugin;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_private(Slapi_PBlock *pblock, void *value)
{
    (*(void **)value) = pblock->pb_plugin->plg_private;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_type(Slapi_PBlock *pblock, void *value)
{
    (*(int *)value) = pblock->pb_plugin->plg_type;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_argv(Slapi_PBlock *pblock, void *value)
{
    (*(char ***)value) = pblock->pb_plugin->plg_argv;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_argc(Slapi_PBlock *pblock, void *value)
{
    (*(int *)value) = pblock->pb_plugin->plg_argc;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_version(Slapi_PBlock *pblock, void *value)
{
    (*(char **)value) = pblock->pb_plugin->plg_version;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_precedence(Slapi_PBlock *pblock, void *value)
{
    (*(int *)value) = pblock->pb_plugin->plg_precedence;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_opreturn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int *)value) = pblock->pb_intop->pb_opreturn;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_object(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(void **)value) = pblock->pb_intplugin->pb_object;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_destroy_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(IFP *)value) = pblock->pb_intplugin->pb_destroy_fn;
    } else {
        (*(IFP *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_description(Slapi_PBlock *pblock, void *value)
{
    (*(Slapi_PluginDesc *)value) = pblock->pb_plugin->plg_desc;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_identity(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(void **)value) = pblock->pb_intplugin->pb_plugin_identity;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_config_area(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(char **)value) = pblock->pb_intplugin->pb_plugin_config_area;
    } else {
        (*(char **)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_config_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin != NULL) {
        (*(char **)value) = pblock->pb_plugin->plg_dn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_intop_result(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int *)value) = pblock->pb_intop->pb_internal_op_result;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_intop_search_entries(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry ***)value) = pblock->pb_intop->pb_plugin_internal_search_op_entries;
    } else {
        (*(Slapi_Entry ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_intop_search_referrals(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(char ***)value) = pblock->pb_intop->pb_plugin_internal_search_op_referrals;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_bind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_unbind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_search;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_next_search_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_next_search_entry;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_next_search_entry_ext_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_next_search_entry_ext;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_search_results_release_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(VFPP *)value) = pblock->pb_plugin->plg_search_results_release;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_prev_search_results_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(VFP *)value) = pblock->pb_plugin->plg_prev_search_results;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_compare;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_modify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_modrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_add;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_delete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_abandon;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_config_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_config;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_close_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_close;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_cleanup_fn(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_cleanup;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_start_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_start;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_poststart_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_poststart;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_wire_import_fn(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_wire_import;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_get_info_fn(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(struct backend *, int32_t,  void **))value) = pblock->pb_plugin->plg_get_info;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_set_info_fn(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(struct backend *, int32_t,  void **))value) = pblock->pb_plugin->plg_set_info;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_ctrl_info_fn(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(struct backend *, int32_t,  void **))value) = pblock->pb_plugin->plg_ctrl_info;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_seq_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_seq;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_entry_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                              plg_entry);
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_referral_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                              plg_referral);
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_result_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER(pblock,
                                                              plg_result);
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_rmdb_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_rmdb;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_ldif2db_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_ldif2db;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_db2ldif_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_db2ldif;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_compact_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(struct backend *, bool))value) = pblock->pb_plugin->plg_dbcompact;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_db2index_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_db2index;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_archive2db_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_archive2db;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_db2archive_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_db2archive;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_upgradedb_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_upgradedb;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_upgradednformat_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_upgradednformat;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_dbverify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_dbverify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_begin_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_commit_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_abort_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_test_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_db_no_acl(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    if (pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    if (NULL == be) {
        (*(int *)value) = 0; /* default value */
    } else {
        (*(int *)value) = be->be_noacl;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_ext_op_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_exhandler;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_ext_op_oidlist(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    (*(char ***)value) = pblock->pb_plugin->plg_exoids;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_ext_op_namelist(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    (*(char ***)value) = pblock->pb_plugin->plg_exnames;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_ext_op_backend_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Backend **))value) = pblock->pb_plugin->plg_be_exhandler;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_prebind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preunbind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_presearch;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_precompare;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_premodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_premodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_predelete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preabandon;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preentry;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_referral_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_prereferral;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_result_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preresult;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pre_extop_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREEXTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_preextop;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postbind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postunbind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postsearch;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_search_fail_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postsearchfail;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postcompare;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postmodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postmodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postdelete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postabandon;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postentry;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_referral_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postreferral;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_result_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postresult;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_post_extop_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTEXTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_postextop;
    return 0;
}

static int32_t
slapi_pblock_get_entry_pre_op(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_pre_op_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_entry_post_op(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_post_op_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepremodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepremodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepreadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepredelete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_pre_close_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepreclose;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostmodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostmodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostdelete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_open_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostopen;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_export_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostexport;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_post_import_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_bepostimport;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_add;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_delete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_add;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_post_delete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_internal_pre_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_bind;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpreadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpredelete;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_pre_delete_tombstone_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpredeletetombstone;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodify;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodrdn;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostadd;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_be_txn_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_betxnpostdelete;
    return 0;
}

static int32_t
slapi_pblock_get_target_address(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(entry_address **)value) = &(pblock->pb_op->o_params.target_address);
    }
    return 0;
}

static int32_t
slapi_pblock_get_target_dn(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_get_target_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(Slapi_DN **)value) = pblock->pb_op->o_params.target_address.sdn;
    } else {
        return (-1);
    }
    return 0;
}

static int32_t
slapi_pblock_get_original_target_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.target_address.udn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_target_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.target_address.uniqueid;
    }
    return 0;
}

static int32_t
slapi_pblock_get_reqcontrols(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(LDAPControl ***)value) = pblock->pb_op->o_params.request_controls;
    }
    return 0;
}

static int32_t
slapi_pblock_get_rescontrols(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(LDAPControl ***)value) = pblock->pb_op->o_results.result_controls;
    }
    return 0;
}

static int32_t
slapi_pblock_get_controls_arg(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(LDAPControl ***)value) = pblock->pb_intop->pb_ctrls_arg;
    } else {
        (*(LDAPControl ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_op_notes(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(unsigned int *)value) = pblock->pb_intop->pb_operation_notes;
    } else {
        (*(unsigned int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_filter_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, const struct berval *, Slapi_Value **,
                    int32_t,  Slapi_Value **))value) = pblock->pb_plugin->plg_syntax_filter_ava;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_filter_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock*, char *, char **, char *, Slapi_Value**))value) = pblock->pb_plugin->plg_syntax_filter_sub;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_values2keys(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Value **, Slapi_Value ***, int32_t))value) = pblock->pb_plugin->plg_syntax_values2keys;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_assertion2keys_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Value *, Slapi_Value ***, int32_t))value) = pblock->pb_plugin->plg_syntax_assertion2keys_ava;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_assertion2keys_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, char *, char **, char *, Slapi_Value ***))value) = pblock->pb_plugin->plg_syntax_assertion2keys_sub;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_names(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(char ***)value) = pblock->pb_plugin->plg_syntax_names;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(char **)value) = pblock->pb_plugin->plg_syntax_oid;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int *)value) = pblock->pb_plugin->plg_syntax_flags;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_compare(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(IFP *)value) = pblock->pb_plugin->plg_syntax_compare;
    return 0;
}

static int32_t
slapi_pblock_get_syntax_substrlens(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(int **)value) = pblock->pb_intplugin->pb_substrlens;
    } else {
        (*(int **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_validate(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(int32_t (**)(struct berval *))value) = pblock->pb_plugin->plg_syntax_validate;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_normalize(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    (*(void (**)(Slapi_PBlock *, char *, int32_t,  char **))value) = pblock->pb_plugin->plg_syntax_normalize;
    return 0;
}

static int32_t
slapi_pblock_get_managedsait(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int *)value) = pblock->pb_intop->pb_managedsait;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_session_tracking(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(char **)value) = pblock->pb_intop->pb_session_tracking_id;
    } else {
        (*(char **)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_pwpolicy(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int *)value) = pblock->pb_intop->pb_pwpolicy_ctrl;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_add_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_op->o_params.p.p_add.target_entry;
    }
    return 0;
}

static int32_t
slapi_pblock_get_add_existing_dn_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_existing_dn_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_add_existing_uniqueid_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_existing_uniqueid_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_add_parent_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_parent_entry;
    }
    return 0;
}

static int32_t
slapi_pblock_get_add_parent_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_add.parentuniqueid;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bind_method(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(ber_tag_t *)value) = pblock->pb_op->o_params.p.p_bind.bind_method;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bind_credentials(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval **)value) = pblock->pb_op->o_params.p.p_bind.bind_creds;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bind_saslmechanism(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_bind.bind_saslmechanism;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bind_ret_saslcreds(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval **)value) = pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds;
    }
    return 0;
}

static int32_t
slapi_pblock_get_compare_type(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type;
    }
    return 0;
}

static int32_t
slapi_pblock_get_compare_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval **)value) = &pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modify_mods(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_newrdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_deloldrdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_newsuperior(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_newsuperior_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(Slapi_DN **)value) =
            pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
    } else {
        return -1;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_parent_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_parent_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_newparent_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_newparent_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_target_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_intop->pb_target_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_modrdn_newsuperior_address(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(entry_address **)value) = &(pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address);
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_scope(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_scope;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_deref(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_deref;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_sizelimit(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_sizelimit;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_timelimit(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_timelimit;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_filter(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_filter_intended(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        if (pblock->pb_op->o_params.p.p_search.search_filter_intended) {
            (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter_intended;
        } else {
            (*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter;
        }
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_strfilter(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_search.search_strfilter;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_attrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_attrs;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_gerattrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_gerattrs;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_reqattrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char ***)value) = pblock->pb_op->o_searchattrs;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_attrsonly(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_attrsonly;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_is_and(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_search.search_is_and;
    }
    return 0;
}

static int32_t
slapi_pblock_get_abandon_msgid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ext_op_req_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_params.p.p_extended.exop_oid;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ext_op_req_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval **)value) = pblock->pb_op->o_params.p.p_extended.exop_value;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ext_op_ret_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(char **)value) = pblock->pb_op->o_results.r.r_extended.exop_ret_oid;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ext_op_ret_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval **)value) = pblock->pb_op->o_results.r.r_extended.exop_ret_value;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_create_fn(Slapi_PBlock *pblock, void *value)
{
    SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);

    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_mr_filter_create;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_indexer_create_fn(Slapi_PBlock *pblock, void *value)
{
    SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
    (*(int32_t (**)(Slapi_PBlock *))value) = pblock->pb_plugin->plg_mr_indexer_create;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_match_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(mrFilterMatchFn *)value) = pblock->pb_mr->filter_match_fn;
    } else {
        (*(mrFilterMatchFn *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_index_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(IFP *)value) = pblock->pb_mr->filter_index_fn;
    } else {
        (*(IFP *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_reset_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(IFP *)value) = pblock->pb_mr->filter_reset_fn;
    } else {
        (*(IFP *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_index_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(IFP *)value) = pblock->pb_mr->index_fn;
    } else {
        (*(IFP *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_index_sv_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(IFP *)value) = pblock->pb_mr->index_sv_fn;
    } else {
        (*(IFP *)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(char **)value) = pblock->pb_mr->oid;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_type(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(char **)value) = pblock->pb_mr->type;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(struct berval **)value) = pblock->pb_mr->value;
    } else {
        (*(struct berval **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_values(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(struct berval ***)value) = pblock->pb_mr->values;
    } else {
        (*(struct berval ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_keys(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(struct berval ***)value) = pblock->pb_mr->keys;
    } else {
        (*(struct berval ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_reusable(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(unsigned int *)value) = pblock->pb_mr->filter_reusable;
    } else {
        (*(unsigned int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_query_operator(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(int *)value) = pblock->pb_mr->query_operator;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_usage(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_mr != NULL) {
        (*(unsigned int *)value) = pblock->pb_mr->usage;
    } else {
        (*(unsigned int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, const struct berval *, Slapi_Value **, int32_t,  Slapi_Value **))value) = pblock->pb_plugin->plg_mr_filter_ava;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_filter_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, char *, char **, char*, Slapi_Value **))value) = pblock->pb_plugin->plg_mr_filter_sub;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_values2keys(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Value **, Slapi_Value ***, int32_t))value) = pblock->pb_plugin->plg_mr_values2keys;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_assertion2keys_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Value *, Slapi_Value ***, int32_t))value) = pblock->pb_plugin->plg_mr_assertion2keys_ava;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_assertion2keys_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(Slapi_PBlock *, char *, char **, char *, Slapi_Value ***))value) = pblock->pb_plugin->plg_mr_assertion2keys_sub;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int *)value) = pblock->pb_plugin->plg_mr_flags;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_names(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(char ***)value) = pblock->pb_plugin->plg_mr_names;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_compare(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(int32_t (**)(struct berval *, struct berval *))value) = pblock->pb_plugin->plg_mr_compare;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mr_normalize(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    (*(void (**)(Slapi_PBlock *, char *, int32_t,  char **))value) = pblock->pb_plugin->plg_mr_normalize;
    return 0;
}

static int32_t
slapi_pblock_get_seq_type(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->seq_type;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_seq_attrname(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->seq_attrname;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_seq_val(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->seq_val;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_file(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char ***)value) = pblock->pb_task->ldif_files;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_removedupvals(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->removedupvals;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_db2index_attrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char ***)value) = pblock->pb_task->db2index_attrs;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_noattrindexes(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif2db_noattrindexes;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_include(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char ***)value) = pblock->pb_task->ldif_include;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_exclude(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char ***)value) = pblock->pb_task->ldif_exclude;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_generate_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif_generate_uniqueid;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif_encrypted(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif_encrypt;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif2db_namespaceid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->ldif_namespaceid;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_db2ldif_printkey(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif_printkey;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_db2ldif_dump_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif_dump_uniqueid;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_db2ldif_file(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->ldif_file;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_backend_instance_name(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->instance_name;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_backend_task(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(Slapi_Task **)value) = pblock->pb_task->task;
    } else {
        (*(Slapi_Task **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_task_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->task_flags;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_db2ldif_server_running(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->server_running;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bulk_import_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(Slapi_Entry **)value) = pblock->pb_task->import_entry;
    } else {
        (*(Slapi_Entry **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_bulk_import_state(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->import_state;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_ldif_changelog(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(int *)value) = pblock->pb_task->ldif_include_changelog;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dbverify_dbdir(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_task != NULL) {
        (*(char **)value) = pblock->pb_task->dbverify_dbdir;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_parent_txn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_deprecated != NULL) {
        (*(void **)value) = pblock->pb_deprecated->pb_parent_txn;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_txn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(void **)value) = pblock->pb_intop->pb_txn;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_txn_ruv_mods_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int32_t(**)(Slapi_PBlock *, char **, Slapi_Mods **))value) = pblock->pb_intop->pb_txn_ruv_mods_fn;
    } else {
        (*(int32_t(**)(Slapi_PBlock *, char **, Slapi_Mods **))value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_result_set(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_set;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_result_set_size_estimate(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_results.r.r_search.estimate;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_result_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_entry;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_result_entry_ext(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(void **)value) = pblock->pb_op->o_results.r.r_search.opaque_backend_ptr;
    }
    return 0;
}

static int32_t
slapi_pblock_get_nentries(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_results.r.r_search.nentries;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_referrals(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(struct berval ***)value) = pblock->pb_op->o_results.r.r_search.search_referrals;
    }
    return 0;
}

static int32_t
slapi_pblock_get_result_code(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        *((int *)value) = pblock->pb_op->o_results.result_code;
    return 0;
}

static int32_t
slapi_pblock_get_result_matched(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        *((char **)value) = pblock->pb_op->o_results.result_matched;
    return 0;
}

static int32_t
slapi_pblock_get_result_text(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        *((char **)value) = pblock->pb_op->o_results.result_text;
    return 0;
}

static int32_t
slapi_pblock_get_pb_result_text(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        *((char **)value) = pblock->pb_intop->pb_result_text;
    } else {
        *((char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dbsize(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_misc != NULL) {
        (*(unsigned int *)value) = pblock->pb_misc->pb_dbsize;
    } else {
        (*(unsigned int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_acl_init(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_acl_init;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_acl_syntax_check(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Entry *, char **))value) = pblock->pb_plugin->plg_acl_syntax_check;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_acl_allow_access(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Entry *, char **, struct berval *,
                    int32_t,  int32_t,  char **))value) = pblock->pb_plugin->plg_acl_access_allowed;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_acl_mods_allowed(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, Slapi_Entry *, LDAPMod **, void *))value) = pblock->pb_plugin->plg_acl_mods_allowed;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_acl_mods_update(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, int32_t,  Slapi_DN *, void *))value) = pblock->pb_plugin->plg_acl_mods_update;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mmr_betxn_preop(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, int32_t))value) = pblock->pb_plugin->plg_mmr_betxn_preop;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_mmr_betxn_postop(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(Slapi_PBlock *, int32_t))value) = pblock->pb_plugin->plg_mmr_betxn_postop;
    return 0;
}

static int32_t
slapi_pblock_get_requestor_dn(Slapi_PBlock *pblock, void *value)
{
    /* NOTE: It's not a copy of the DN */
    if (pblock->pb_op != NULL) {
        char *dn = (char *)slapi_sdn_get_dn(&pblock->pb_op->o_sdn);
        if (dn == NULL)
            (*(char **)value) = "";
        else
            (*(char **)value) = dn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_requestor_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(Slapi_DN **)value) = &pblock->pb_op->o_sdn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_requestor_ndn(Slapi_PBlock *pblock, void *value)
{
    /* NOTE: It's not a copy of the DN */
    if (pblock->pb_op != NULL) {
        char *ndn = (char *)slapi_sdn_get_ndn(&pblock->pb_op->o_sdn);
        if (ndn == NULL)
            (*(char **)value) = "";
        else
            (*(char **)value) = ndn;
    }
    return 0;
}

static int32_t
slapi_pblock_get_operation_authtype(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        if (pblock->pb_op->o_authtype == NULL)
            (*(char **)value) = "";
        else
            (*(char **)value) = pblock->pb_op->o_authtype;
    }
    return 0;
}

static int32_t
slapi_pblock_get_operation_ssf(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        *((int *)value) = pblock->pb_op->o_ssf;
    }
    return 0;
}

static int32_t
slapi_pblock_get_client_dns(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n");
        return (-1);
    }
    (*(struct berval ***)value) = pblock->pb_conn->c_domain;
    return 0;
}

static int32_t
slapi_pblock_get_be_maxnestlevel(Slapi_PBlock *pblock, void *value)
{
    Slapi_Backend *be = pblock->pb_backend;

    if (NULL == be) {
        return (-1);
    }
    (*(int *)value) = be->be_maxnestlevel;
    return 0;
}

static int32_t
slapi_pblock_get_operation_id(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        (*(int *)value) = pblock->pb_op->o_opid;
    }
    return 0;
}

static int32_t
slapi_pblock_get_argc(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_misc != NULL) {
        (*(int *)value) = pblock->pb_misc->pb_slapd_argc;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_argv(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_misc != NULL) {
        (*(char ***)value) = pblock->pb_misc->pb_slapd_argv;
    } else {
        (*(char ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_config_directory(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(char **)value) = pblock->pb_intplugin->pb_slapd_configdir;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_name(Slapi_PBlock *pblock, void *value)
{
    (*(char **)value) = pblock->pb_plugin->plg_pwdstorageschemename;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_user_pwd(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_deprecated != NULL) {
        (*(char **)value) = pblock->pb_deprecated->pb_pwd_storage_scheme_user_passwd;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_db_pwd(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_deprecated != NULL) {
        (*(char **)value) = pblock->pb_deprecated->pb_pwd_storage_scheme_db_passwd;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_enc_fn(Slapi_PBlock *pblock, void *value)
{
    (*(CFP *)value) = pblock->pb_plugin->plg_pwdstorageschemeenc;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_dec_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemedec;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_pwd_storage_scheme_cmp_fn(Slapi_PBlock *pblock, void *value)
{
    (*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemecmp;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_entry_fetch_func(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(char **, uint32_t *))value) = pblock->pb_plugin->plg_entryfetchfunc;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_entry_store_func(Slapi_PBlock *pblock, void *value)
{
    (*(int32_t (**)(char **, uint32_t *))value) = pblock->pb_plugin->plg_entrystorefunc;
    return 0;
}

static int32_t
slapi_pblock_get_plugin_enabled(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        *((int *)value) = pblock->pb_intplugin->pb_plugin_enabled;
    } else {
        *((int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dse_dont_write_when_adding(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->dont_add_write;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dse_merge_when_adding(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->add_merge;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dse_dont_check_dups(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->dont_check_dups;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dse_reapply_mods(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->reapply_mods;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_dse_is_primary_file(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->is_primary_file;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_schema_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_dse != NULL) {
        (*(int *)value) = pblock->pb_dse->schema_flags;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_urp_naming_collision_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(char **)value) = pblock->pb_intop->pb_urp_naming_collision_dn;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_urp_tombstone_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(char **)value) = pblock->pb_intop->pb_urp_tombstone_uniqueid;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_urp_tombstone_conflict_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(char **)value) = pblock->pb_intop->pb_urp_tombstone_conflict_dn;
    } else {
        (*(char **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_search_ctrls(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(LDAPControl ***)value) = pblock->pb_intop->pb_search_ctrls;
    } else {
        (*(LDAPControl ***)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_filter_normalized(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(int *)value) = pblock->pb_intplugin->pb_syntax_filter_normalized;
    } else {
        (*(int *)value) =  0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_plugin_syntax_filter_data(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intplugin != NULL) {
        (*(void **)value) = pblock->pb_intplugin->pb_syntax_filter_data;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_paged_results_index(Slapi_PBlock *pblock, void *value)
{
    if (op_is_pagedresults(pblock->pb_op) && pblock->pb_intop != NULL) {
        /* search req is simple paged results */
        (*(int *)value) = pblock->pb_intop->pb_paged_results_index;
    } else {
        (*(int *)value) = -1;
    }
    return 0;
}

static int32_t
slapi_pblock_get_paged_results_cookie(Slapi_PBlock *pblock, void *value)
{
    if (op_is_pagedresults(pblock->pb_op) && pblock->pb_intop != NULL) {
        /* search req is simple paged results */
        (*(int *)value) = pblock->pb_intop->pb_paged_results_cookie;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_memberof_deferred_task(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(void **)value) = pblock->pb_intop->memberof_deferred_task;
    } else {
        (*(void **)value) = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_get_usn_increment_for_tombstone(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_intop != NULL) {
        (*(int32_t *)value) = pblock->pb_intop->pb_usn_tombstone_incremented;
    } else {
        (*(int32_t *)value) = 0;
    }
    return 0;
}

static int32_t
slapi_pblock_get_aci_target_check(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_misc != NULL) {
        (*(int *)value) = pblock->pb_misc->pb_aci_target_check;
    } else {
        (*(int *)value) = 0;
    }
    return 0;
}

/* DEPRECATED */
static int32_t
slapi_pblock_get_conn_authtype(Slapi_PBlock *pblock, void *value)
{
    char *authtype = NULL;

    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                        "slapi_pblock_get", "Connection is NULL and hence cannot access SLAPI_CONN_AUTHTYPE \n");
        return -1;
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
    return 0;
}

static int32_t
slapi_pblock_get_conn_clientip(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        memset(value, 0, sizeof(struct in_addr));
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    if (pblock->pb_conn->cin_addr == NULL) {
        memset(value, 0, sizeof(struct in_addr));
    } else {
        if (PR_IsNetAddrType(pblock->pb_conn->cin_addr, PR_IpAddrV4Mapped)) {
            (*(struct in_addr *)value).s_addr =
                (*(pblock->pb_conn->cin_addr)).ipv6.ip.pr_s6_addr32[3];
        } else {
            memset(value, 0, sizeof(struct in_addr));
        }
    }
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_get_conn_serverip(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        memset(value, 0, sizeof(struct in_addr));
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    if (pblock->pb_conn->cin_destaddr == NULL) {
        memset(value, 0, sizeof(PRNetAddr));
    } else {
        if (PR_IsNetAddrType(pblock->pb_conn->cin_destaddr, PR_IpAddrV4Mapped)) {
            (*(struct in_addr *)value).s_addr =
                (*(pblock->pb_conn->cin_destaddr)).ipv6.ip.pr_s6_addr32[3];
        } else {
            memset(value, 0, sizeof(struct in_addr));
        }
    }
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

/*
 *
 * Start of the "set" functions
 *
 */
static int32_t
slapi_pblock_set_backend(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_backend = (Slapi_Backend *)value;
    if (pblock->pb_backend && (NULL == pblock->pb_plugin)) {
        /* newly allocated pblock may not have backend plugin set. */
        pblock->pb_plugin =
            (struct slapdplugin *)pblock->pb_backend->be_database;
    }
    return 0;
}

static int32_t
slapi_pblock_set_backend_count(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_misc(pblock);
    pblock->pb_misc->pb_backend_count = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_connection(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_conn = (Connection *)value;
    return 0;
}

static int32_t
slapi_pblock_set_operation(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_op = (Operation *)value;
    return 0;
}

static int32_t
slapi_pblock_set_opinitiated_time(Slapi_PBlock *pblock, void *value)
{
    return 0;
}

static int32_t
slapi_pblock_set_requestor_isroot(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_requestor_isroot = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_is_replicated_operation(Slapi_PBlock *pblock, void *value)
{
    PR_ASSERT(0);
    return 0;
}

static int32_t
slapi_pblock_set_operation_parameters(Slapi_PBlock *pblock, void *value)
{
    PR_ASSERT(0);
    return 0;
}

static int32_t
slapi_pblock_set_conn_id(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CONN_ID \n");
        return (-1);
    }
    pblock->pb_conn->c_connid = *((uint64_t *)value);
    return 0;
}

static int32_t
slapi_pblock_set_conn_dn(Slapi_PBlock *pblock, void *value)
{
    /*
     * Slightly crazy but we must pass a copy of the current
     * authtype into bind_credentials_set() since it will
     * free the current authtype.
     */
    char *authtype = NULL;

    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CONN_DN \n");
        return (-1);
    }
    slapi_pblock_get(pblock, SLAPI_CONN_AUTHMETHOD, &authtype);
    bind_credentials_set(pblock->pb_conn, authtype,
                         (char *)value, NULL, NULL, NULL, NULL);
    slapi_ch_free((void **)&authtype);
    return 0;
}

static int32_t
slapi_pblock_set_deferred_memberof(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_deferred_memberof = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_conn_authmethod(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_set_conn_clientnetaddr_aclip(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        return 0;
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    slapi_ch_free((void **)&pblock->pb_conn->cin_addr_aclip);
    pblock->pb_conn->cin_addr_aclip = (PRNetAddr *)value;
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));

    return 0;
}

static int32_t
slapi_pblock_set_conn_is_replication_session(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_set",
                      "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n");
        return (-1);
    }
    pthread_mutex_lock(&(pblock->pb_conn->c_mutex));
    pblock->pb_conn->c_isreplication_session = *((int *)value);
    pthread_mutex_unlock(&(pblock->pb_conn->c_mutex));
    return 0;
}

static int32_t
slapi_pblock_set_destroy_content(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_deprecated(pblock);
    pblock->pb_deprecated->pb_destroy_content = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin = (struct slapdplugin *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_private(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_private = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_type(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_type = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_argv(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_argv = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_argc(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_argc = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_version(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_version = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_precedence(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_precedence = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_opreturn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_opreturn = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_object(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_object = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_identity(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_plugin_identity = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_config_area(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_plugin_config_area = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_destroy_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_destroy_fn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_description(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_desc = *((Slapi_PluginDesc *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_intop_result(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_internal_op_result = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_intop_search_entries(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_plugin_internal_search_op_entries = (Slapi_Entry **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_intop_search_referrals(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_plugin_internal_search_op_referrals = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_requestor_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        return (-1);
    }
    slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn), (char *)value);
    return 0;
}

static int32_t
slapi_pblock_set_requestor_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        return (-1);
    }
    slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn), slapi_sdn_get_dn((Slapi_DN *)value));
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_bind = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_unbind = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_search = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_next_search_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_next_search_entry = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_next_search_entry_ext_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_next_search_entry_ext = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_search_results_release_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_search_results_release = (VFPP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_prev_search_results_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_prev_search_results = (VFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_compare = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_modify = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_modrdn = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_add = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_delete = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_abandon = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_config_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_config = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_close_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_close = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_cleanup_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_cleanup = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_start_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_start = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_poststart_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_poststart = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_wire_import_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_wire_import = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_get_info_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_get_info = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_set_info_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_set_info = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_ctrl_info_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_ctrl_info = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_seq_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_seq = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_entry_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_entry = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_referral_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_referral = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_result_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_result = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_rmdb_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_rmdb = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_ldif2db_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_ldif2db = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_db2ldif_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_db2ldif = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_db2index_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_db2index = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_archive2db_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_archive2db = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_db2archive_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_db2archive = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_upgradedb_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_upgradedb = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_upgradednformat_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_upgradednformat = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_dbverify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_dbverify = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_begin_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_commit_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_abort_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_test_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_no_acl(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    if (NULL == pblock->pb_backend) {
        return (-1);
    }
    pblock->pb_backend->be_noacl = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_db_compact_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE) {
        return (-1);
    }
    pblock->pb_plugin->plg_dbcompact = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_ext_op_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    pblock->pb_plugin->plg_exhandler = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_ext_op_oidlist(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    pblock->pb_plugin->plg_exoids = (char **)value;
    ldapi_register_extended_op((char **)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_ext_op_namelist(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    pblock->pb_plugin->plg_exnames = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_ext_op_backend_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP &&
        pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNEXTENDEDOP) {
        return (-1);
    }
    pblock->pb_plugin->plg_be_exhandler = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_prebind = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preunbind = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_presearch = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_precompare = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_premodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_premodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_predelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preabandon = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preentry = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_referral_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_prereferral = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_result_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preresult = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pre_extop_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREEXTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_preextop = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postbind = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_unbind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postunbind = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_search_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postsearch = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_search_fail_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postsearchfail = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_compare_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postcompare = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postmodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postmodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postdelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_abandon_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postabandon = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_entry_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postentry = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_referral_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postreferral = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_result_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postresult = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_post_extop_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTEXTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_postextop = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepremodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepremodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepreadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepredelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_pre_close_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepreclose = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostmodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostmodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostdelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_open_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostopen = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_export_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostexport = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_post_import_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_bepostimport = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_pre_modify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_pre_modrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_pre_add = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_pre_delete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_pre_bind_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_pre_bind = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_post_modify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_post_modrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_post_add = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_internal_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_internal_post_delete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_pre_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpremodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_pre_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpremodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_pre_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpreadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_pre_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpredelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_pre_delete_tombstone_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpredeletetombstone = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_post_modify_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpostmodify = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_post_modrdn_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpostmodrdn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_post_add_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpostadd = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_be_txn_post_delete_fn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return (-1);
    }
    pblock->pb_plugin->plg_betxnpostdelete = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_filter_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_filter_ava = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_filter_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_filter_sub = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_values2keys(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_values2keys = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_assertion2keys_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_assertion2keys_ava = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_assertion2keys_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_assertion2keys_sub = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_names(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    PR_ASSERT(pblock->pb_plugin->plg_syntax_names == NULL);
    pblock->pb_plugin->plg_syntax_names = slapi_ch_array_dup((char **)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    PR_ASSERT(pblock->pb_plugin->plg_syntax_oid == NULL);
    pblock->pb_plugin->plg_syntax_oid = slapi_ch_strdup((char *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_flags = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_compare(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_compare = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_syntax_substrlens(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_substrlens = (int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_validate(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_validate = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_normalize(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX) {
        return (-1);
    }
    pblock->pb_plugin->plg_syntax_normalize = value;
    return 0;
}

static int32_t
slapi_pblock_set_entry_pre_op(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_pre_op_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_entry_post_op(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_post_op_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_target_address(Slapi_PBlock *pblock, void *value)
{
    PR_ASSERT(PR_FALSE); /* can't do this */
    return 0;
}

static int32_t
slapi_pblock_set_target_dn(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_set_target_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.target_address.sdn = (Slapi_DN *)value;
    } else {
        return (-1);
    }
    return 0;
}

static int32_t
slapi_pblock_set_original_target_dn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.target_address.udn = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_target_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.target_address.uniqueid = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_reqcontrols(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.request_controls = (LDAPControl **)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_rescontrols(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.result_controls = (LDAPControl **)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_controls_arg(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_ctrls_arg = (LDAPControl **)value;
    return 0;
}

static int32_t
slapi_pblock_set_add_rescontrol(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        add_control(&pblock->pb_op->o_results.result_controls, (LDAPControl *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_op_notes(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    if (value == NULL) {
        pblock->pb_intop->pb_operation_notes = 0; /* cleared */
    } else {
        pblock->pb_intop->pb_operation_notes |= *((unsigned int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_skip_modified_attrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op == NULL) {
        return 0;
    }
    if (value == 0) {
        pblock->pb_op->o_flags &= ~OP_FLAG_SKIP_MODIFIED_ATTRS;
    } else {
        pblock->pb_op->o_flags |= OP_FLAG_SKIP_MODIFIED_ATTRS;
    }
    return 0;
}

static int32_t
slapi_pblock_set_managedsait(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_managedsait = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_session_tracking(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_session_tracking_id = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_pwpolicy(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_pwpolicy_ctrl = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_add_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_add.target_entry = (Slapi_Entry *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_add_existing_dn_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_existing_dn_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_add_existing_uniqueid_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_existing_uniqueid_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_add_parent_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_parent_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_add_parent_uniqueid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_add.parentuniqueid = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_bind_method(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_bind.bind_method = *((ber_tag_t *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_bind_credentials(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_bind.bind_creds = (struct berval *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_bind_saslmechanism(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_bind.bind_saslmechanism = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_bind_ret_saslcreds(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds = (struct berval *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_compare_type(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_compare_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value = *((struct berval *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_modify_mods(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_newrdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_deloldrdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_newsuperior(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        Slapi_DN *sdn =
            pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
        slapi_sdn_free(&sdn);
        pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn =
            slapi_sdn_new_dn_byval((char *)value);
    } else {
        return -1;
    }
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_newsuperior_sdn(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn =
            (Slapi_DN *)value;
    } else {
        return -1;
    }
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_parent_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_parent_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_newparent_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_newparent_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_target_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_target_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_modrdn_newsuperior_address(Slapi_PBlock *pblock, void *value)
{
    PR_ASSERT(PR_FALSE); /* can't do this */
    return 0;
}

static int32_t
slapi_pblock_set_search_scope(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_scope = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_deref(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_deref = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_sizelimit(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_sizelimit = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_timelimit(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_timelimit = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_filter(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_filter = (struct slapi_filter *)value;
        /* Prevent UAF by reseting this on set. */
        pblock->pb_op->o_params.p.p_search.search_filter_intended = NULL;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_filter_intended(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_filter_intended = (struct slapi_filter *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_strfilter(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_strfilter = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_attrs(Slapi_PBlock *pblock, void *value)
{
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
    return 0;
}

static int32_t
slapi_pblock_set_search_gerattrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_gerattrs = (char **)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_reqattrs(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_searchattrs = (char **)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_attrsonly(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_attrsonly = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_is_and(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_search.search_is_and = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_abandon_msgid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_ext_op_req_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_extended.exop_oid = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_ext_op_req_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_params.p.p_extended.exop_value = (struct berval *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_ext_op_ret_oid(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_extended.exop_ret_oid = (char *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_ext_op_ret_value(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_extended.exop_ret_value = (struct berval *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_create_fn(Slapi_PBlock *pblock, void *value)
{
    SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
    pblock->pb_plugin->plg_mr_filter_create = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_indexer_create_fn(Slapi_PBlock *pblock, void *value)
{
    SLAPI_PLUGIN_TYPE_CHECK(pblock, SLAPI_PLUGIN_MATCHINGRULE);
    pblock->pb_plugin->plg_mr_indexer_create = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_match_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->filter_match_fn = (mrFilterMatchFn)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_index_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->filter_index_fn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_reset_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->filter_reset_fn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_index_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->index_fn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_index_sv_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->index_sv_fn = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_oid(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->oid = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_type(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->type = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_value(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->value = (struct berval *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_values(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->values = (struct berval **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_keys(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->keys = (struct berval **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_reusable(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->filter_reusable = *(unsigned int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_query_operator(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->query_operator = *(int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_usage(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_mr(pblock);
    pblock->pb_mr->usage = *(unsigned int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_filter_ava = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_filter_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_filter_sub = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_values2keys(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_values2keys = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_assertion2keys_ava(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_assertion2keys_ava = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_assertion2keys_sub(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_assertion2keys_sub = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_flags(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_flags = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_names(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    PR_ASSERT(pblock->pb_plugin->plg_mr_names == NULL);
    pblock->pb_plugin->plg_mr_names = slapi_ch_array_dup((char **)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_compare(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_compare = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mr_normalize(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE) {
        return (-1);
    }
    pblock->pb_plugin->plg_mr_normalize = value;
    return 0;
}

static int32_t
slapi_pblock_set_seq_type(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->seq_type = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_seq_attrname(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->seq_attrname = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_seq_val(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->seq_val = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_file(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_files = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_removedupvals(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->removedupvals = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_db2index_attrs(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->db2index_attrs = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_noattrindexes(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif2db_noattrindexes = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_include(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_include = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_exclude(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_exclude = (char **)value;
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_generate_uniqueid(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_generate_uniqueid = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_ldif2db_namespaceid(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_namespaceid = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_db2ldif_printkey(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_printkey = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_db2ldif_dump_uniqueid(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_dump_uniqueid = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_db2ldif_file(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_file = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_backend_instance_name(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->instance_name = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_backend_task(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->task = (Slapi_Task *)value;
    return 0;
}

static int32_t
slapi_pblock_set_task_flags(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->task_flags = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_db2ldif_server_running(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->server_running = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_bulk_import_entry(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->import_entry = (Slapi_Entry *)value;
    return 0;
}

static int32_t
slapi_pblock_set_bulk_import_state(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->import_state = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_ldif_changelog(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_include_changelog = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_ldif_encrypted(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->ldif_encrypt = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dbverify_dbdir(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_task(pblock);
    pblock->pb_task->dbverify_dbdir = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_parent_txn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_deprecated(pblock);
    pblock->pb_deprecated->pb_parent_txn = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_txn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_txn = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_txn_ruv_mods_fn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_txn_ruv_mods_fn = value;
    return 0;
}

static int32_t
slapi_pblock_set_search_result_set(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.search_result_set = (void *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_result_set_size_estimate(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.estimate = *(int *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_result_entry(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.search_result_entry = (void *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_result_entry_ext(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.opaque_backend_ptr = (void *)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_nentries(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.nentries = *((int *)value);
    }
    return 0;
}

static int32_t
slapi_pblock_set_search_referrals(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL) {
        pblock->pb_op->o_results.r.r_search.search_referrals = (struct berval **)value;
    }
    return 0;
}

static int32_t
slapi_pblock_set_result_code(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        pblock->pb_op->o_results.result_code = (*(int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_result_matched(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        pblock->pb_op->o_results.result_matched = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_result_text(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_op != NULL)
        pblock->pb_op->o_results.result_text = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_pb_result_text(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    slapi_ch_free((void **)&(pblock->pb_intop->pb_result_text));
    pblock->pb_intop->pb_result_text = slapi_ch_strdup((char *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dbsize(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_misc(pblock);
    pblock->pb_misc->pb_dbsize = *((unsigned int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_acl_init(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_acl_init = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_acl_syntax_check(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_acl_syntax_check = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_acl_allow_access(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_acl_access_allowed = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_acl_mods_allowed(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_acl_mods_allowed = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_acl_mods_update(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_acl_mods_update = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mmr_betxn_preop(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_mmr_betxn_preop = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_mmr_betxn_postop(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_mmr_betxn_postop = value;
    return 0;
}

static int32_t
slapi_pblock_set_client_dns(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_conn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_pblock_set", "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n");
        return (-1);
    }
    pblock->pb_conn->c_domain = *((struct berval ***)value);
    return 0;
}

static int32_t
slapi_pblock_set_argc(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_misc(pblock);
    pblock->pb_misc->pb_slapd_argc = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_argv(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_misc(pblock);
    pblock->pb_misc->pb_slapd_argv = *((char ***)value);
    return 0;
}

static int32_t
slapi_pblock_set_config_directory(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_slapd_configdir = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_name(Slapi_PBlock *pblock, void *value)
{
    if (pblock->pb_plugin->plg_pwdstorageschemename != NULL) {
        /* Free the old name. */
        slapi_ch_free_string(&pblock->pb_plugin->plg_pwdstorageschemename);
    }
    pblock->pb_plugin->plg_pwdstorageschemename = slapi_ch_strdup((char *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_user_pwd(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_deprecated(pblock);
    pblock->pb_deprecated->pb_pwd_storage_scheme_user_passwd = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_db_pwd(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_deprecated(pblock);
    pblock->pb_deprecated->pb_pwd_storage_scheme_db_passwd = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_enc_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_pwdstorageschemeenc = (CFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_dec_fn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_pwdstorageschemedec = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_pwd_storage_scheme_cmp_fn(Slapi_PBlock *pblock, void *value)
{
    /* must provide a comparison function */
    if (value == NULL) {
        return (-1);
    }
    pblock->pb_plugin->plg_pwdstorageschemecmp = (IFP)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_entry_fetch_func(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_entryfetchfunc = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_entry_store_func(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_plugin->plg_entrystorefunc = value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_enabled(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_plugin_enabled = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dse_dont_write_when_adding(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->dont_add_write = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dse_merge_when_adding(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->add_merge = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dse_dont_check_dups(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->dont_check_dups = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dse_reapply_mods(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->reapply_mods = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_dse_is_primary_file(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->is_primary_file = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_schema_flags(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_dse(pblock);
    pblock->pb_dse->schema_flags = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_urp_naming_collision_dn(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_urp_naming_collision_dn = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_urp_tombstone_conflict_dn(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_intop->pb_urp_tombstone_conflict_dn = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_urp_tombstone_uniqueid(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_urp_tombstone_uniqueid = (char *)value;
    return 0;
}

static int32_t
slapi_pblock_set_search_ctrls(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_search_ctrls = (LDAPControl **)value;
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_filter_normalized(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_syntax_filter_normalized = *((int *)value);
    return 0;
}

static int32_t
slapi_pblock_set_plugin_syntax_filter_data(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intplugin(pblock);
    pblock->pb_intplugin->pb_syntax_filter_data = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_paged_results_index(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->pb_paged_results_index = *(int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_paged_results_cookie(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_intop->pb_paged_results_cookie = *(int *)value;
    return 0;
}

static int32_t
slapi_pblock_set_memberof_deferred_task(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_intop(pblock);
    pblock->pb_intop->memberof_deferred_task = (void *)value;
    return 0;
}

static int32_t
slapi_pblock_set_usn_increment_for_tombstone(Slapi_PBlock *pblock, void *value)
{
    pblock->pb_intop->pb_usn_tombstone_incremented = *((int32_t *)value);
    return 0;
}

static int32_t
slapi_pblock_set_aci_target_check(Slapi_PBlock *pblock, void *value)
{
    _pblock_assert_pb_misc(pblock);
    pblock->pb_misc->pb_aci_target_check = *((int *)value);
    return 0;
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

/*
 * Callback table for getting slapi pblock parameters
 */
static int32_t (*get_cbtable[])(Slapi_PBlock *, void *) = {
    NULL,
    NULL, /* slot 1 available */
    NULL, /* slot 2 available */
    slapi_pblock_get_plugin,
    slapi_pblock_get_plugin_private,
    slapi_pblock_get_plugin_type,
    slapi_pblock_get_plugin_argv,
    slapi_pblock_get_plugin_argc,
    slapi_pblock_get_plugin_version,
    slapi_pblock_get_plugin_opreturn,
    slapi_pblock_get_plugin_object,
    slapi_pblock_get_plugin_destroy_fn,
    slapi_pblock_get_plugin_description,
    slapi_pblock_get_plugin_identity,
    slapi_pblock_get_plugin_precedence,
    slapi_pblock_get_plugin_intop_result,
    slapi_pblock_get_plugin_intop_search_entries,
    slapi_pblock_get_plugin_intop_search_referrals,
    NULL, /* slot 18 available */
    NULL, /* slot 19 available */
    NULL, /* slot 20 available */
    NULL, /* slot 21 available */
    NULL, /* slot 22 available */
    NULL, /* slot 23 available */
    NULL, /* slot 24 available */
    NULL, /* slot 25 available */
    NULL, /* slot 26 available */
    NULL, /* slot 27 available */
    NULL, /* slot 28 available */
    NULL, /* slot 29 available */
    NULL, /* slot 30 available */
    NULL, /* slot 31 available */
    NULL, /* slot 32 available */
    NULL, /* slot 33 available */
    NULL, /* slot 34 available */
    NULL, /* slot 35 available */
    NULL, /* slot 36 available */
    NULL, /* slot 37 available */
    NULL, /* slot 38 available */
    NULL, /* slot 39 available */
    NULL, /* slapi_pblock_get_config_filename - deprecated since DS 5.0 */
    NULL, /* slapi_pblock_get_config_lineno -  deprecated since DS 5.0 */
    NULL, /* slapi_pblock_get_config_argc - deprecated since DS 5.0 */
    NULL, /* slapi_pblock_get_config_argv -  deprecated since DS 5.0 */
    NULL, /* slot 44 available */
    NULL, /* slot 45 available */
    NULL, /* slot 46 available */
    slapi_pblock_get_target_sdn,
    slapi_pblock_get_target_address,
    slapi_pblock_get_target_uniqueid,
    slapi_pblock_get_target_dn,
    slapi_pblock_get_reqcontrols,
    slapi_pblock_get_entry_pre_op,
    slapi_pblock_get_entry_post_op,
    NULL, /* slot 54 available */
    slapi_pblock_get_rescontrols,
    NULL, /* "get" function not implemented for SLAPI_ADD_RESCONTROL (56) */
    slapi_pblock_get_op_notes,
    slapi_pblock_get_controls_arg,
    slapi_pblock_get_destroy_content,
    slapi_pblock_get_add_entry,
    slapi_pblock_get_add_existing_dn_entry,
    slapi_pblock_get_add_parent_entry,
    slapi_pblock_get_add_parent_uniqueid,
    slapi_pblock_get_add_existing_uniqueid_entry,
    NULL, /* slot 65 available */
    NULL, /* slot 66 available */
    NULL, /* slot 67 available */
    NULL, /* slot 68 available */
    NULL, /* slot 69 available */
    slapi_pblock_get_bind_method,
    slapi_pblock_get_bind_credentials,
    slapi_pblock_get_bind_saslmechanism,
    slapi_pblock_get_bind_ret_saslcreds,
    NULL, /* slot 74 available */
    NULL, /* slot 75 available */
    NULL, /* slot 76 available */
    NULL, /* slot 77 available */
    NULL, /* slot 78 available */
    NULL, /* slot 79 available */
    slapi_pblock_get_compare_type,
    slapi_pblock_get_compare_value,
    NULL, /* slot 82 available */
    NULL, /* slot 83 available */
    NULL, /* slot 84 available */
    NULL, /* slot 85 available */
    NULL, /* slot 86 available */
    NULL, /* slot 87 available */
    NULL, /* slot 88 available */
    NULL, /* slot 89 available */
    slapi_pblock_get_modify_mods,
    NULL, /* slot 91 available */
    NULL, /* slot 92 available */
    NULL, /* slot 93 available */
    NULL, /* slot 94 available */
    NULL, /* slot 95 available */
    NULL, /* slot 96 available */
    NULL, /* slot 97 available */
    NULL, /* slot 98 available */
    NULL, /* slot 99 available */
    slapi_pblock_get_modrdn_newrdn,
    slapi_pblock_get_modrdn_deloldrdn,
    slapi_pblock_get_modrdn_newsuperior,
    slapi_pblock_get_modrdn_newsuperior_sdn,
    slapi_pblock_get_modrdn_parent_entry,
    slapi_pblock_get_modrdn_newparent_entry,
    slapi_pblock_get_modrdn_target_entry,
    slapi_pblock_get_modrdn_newsuperior_address,
    NULL, /* slot 108 available */
    slapi_pblock_get_original_target_dn,
    slapi_pblock_get_search_scope,
    slapi_pblock_get_search_deref,
    slapi_pblock_get_search_sizelimit,
    slapi_pblock_get_search_timelimit,
    slapi_pblock_get_search_filter,
    slapi_pblock_get_search_strfilter,
    slapi_pblock_get_search_attrs,
    slapi_pblock_get_search_attrsonly,
    slapi_pblock_get_search_is_and,
    slapi_pblock_get_search_filter_intended,
    slapi_pblock_get_abandon_msgid,
    NULL, /* slot 121 available */
    NULL, /* slot 122 available */
    NULL, /* slot 123 available */
    NULL, /* slot 124 available */
    NULL, /* slot 125 available */
    NULL, /* slot 126 available */
    NULL, /* slot 127 available */
    NULL, /* slot 128 available */
    NULL, /* slot 129 available */
    slapi_pblock_get_backend,
    slapi_pblock_get_connection,
    slapi_pblock_get_operation,
    slapi_pblock_get_requestor_isroot,
    NULL, /* slot 134 available */
    slapi_pblock_get_be_type,
    slapi_pblock_get_be_readonly,
    slapi_pblock_get_be_lastmod,
    slapi_pblock_get_operation_parameters,
    slapi_pblock_get_conn_id,
    slapi_pblock_get_opinitiated_time,
    slapi_pblock_get_requestor_dn,
    slapi_pblock_get_is_replicated_operation,
    slapi_pblock_get_conn_dn,
    slapi_pblock_get_conn_authtype,
    slapi_pblock_get_conn_clientip,
    slapi_pblock_get_conn_serverip,
    slapi_pblock_get_argc,
    slapi_pblock_get_argv,
    slapi_pblock_get_conn_is_replication_session,
    slapi_pblock_get_seq_type,
    slapi_pblock_get_seq_attrname,
    slapi_pblock_get_seq_val,
    slapi_pblock_get_is_mmr_replicated_operation,
    NULL, /* slot 154 available */
    slapi_pblock_get_skip_modified_attrs,
    slapi_pblock_get_requestor_ndn,
    NULL, /* slot 157 available */
    NULL, /* slot 158 available */
    NULL, /* slot 159 available */
    slapi_pblock_get_ext_op_req_oid,
    slapi_pblock_get_ext_op_req_value,
    slapi_pblock_get_ext_op_ret_oid,
    slapi_pblock_get_ext_op_ret_value,
    NULL, /* slot 164 available */
    NULL, /* slot 165 available */
    NULL, /* slot 166 available */
    NULL, /* slot 167 available */
    NULL, /* slot 168 available */
    NULL, /* slot 169 available */
    NULL, /* slot 170 available */
    NULL, /* slot 171 available */
    NULL, /* slot 172 available */
    NULL, /* slot 173 available */
    NULL, /* slot 174 available */
    slapi_pblock_get_ldif2db_generate_uniqueid,
    slapi_pblock_get_db2ldif_dump_uniqueid,
    slapi_pblock_get_ldif2db_namespaceid,
    slapi_pblock_get_backend_instance_name,
    slapi_pblock_get_backend_task,
    slapi_pblock_get_ldif2db_file,
    slapi_pblock_get_task_flags,
    slapi_pblock_get_bulk_import_entry,
    slapi_pblock_get_db2ldif_printkey,
    slapi_pblock_get_db2ldif_file,
    slapi_pblock_get_ldif2db_removedupvals,
    slapi_pblock_get_db2index_attrs,
    slapi_pblock_get_ldif2db_noattrindexes,
    slapi_pblock_get_ldif2db_include,
    slapi_pblock_get_ldif2db_exclude,
    slapi_pblock_get_parent_txn,
    slapi_pblock_get_txn,
    slapi_pblock_get_bulk_import_state,
    slapi_pblock_get_search_result_set,
    slapi_pblock_get_search_result_entry,
    slapi_pblock_get_nentries,
    slapi_pblock_get_search_referrals,
    slapi_pblock_get_db2ldif_server_running,
    slapi_pblock_get_search_ctrls,
    slapi_pblock_get_dbsize,
    slapi_pblock_get_plugin_db_bind_fn,
    slapi_pblock_get_plugin_db_unbind_fn,
    slapi_pblock_get_plugin_db_search_fn,
    slapi_pblock_get_plugin_db_compare_fn,
    slapi_pblock_get_plugin_db_modify_fn,
    slapi_pblock_get_plugin_db_modrdn_fn,
    slapi_pblock_get_plugin_db_add_fn,
    slapi_pblock_get_plugin_db_delete_fn,
    slapi_pblock_get_plugin_db_abandon_fn,
    slapi_pblock_get_plugin_db_config_fn,
    slapi_pblock_get_plugin_close_fn,
    NULL, /* slot 211 available */
    slapi_pblock_get_plugin_start_fn,
    slapi_pblock_get_plugin_db_seq_fn,
    slapi_pblock_get_plugin_db_entry_fn,
    slapi_pblock_get_plugin_db_referral_fn,
    slapi_pblock_get_plugin_db_result_fn,
    slapi_pblock_get_plugin_db_ldif2db_fn,
    slapi_pblock_get_plugin_db_db2ldif_fn,
    slapi_pblock_get_plugin_db_begin_fn,
    slapi_pblock_get_plugin_db_commit_fn,
    slapi_pblock_get_plugin_db_abort_fn,
    slapi_pblock_get_plugin_db_archive2db_fn,
    slapi_pblock_get_plugin_db_db2archive_fn,
    slapi_pblock_get_plugin_db_next_search_entry_fn,
    NULL, /* slot 225 available */
    NULL, /* slot 226 available */
    slapi_pblock_get_plugin_db_test_fn,
    slapi_pblock_get_plugin_db_db2index_fn,
    slapi_pblock_get_plugin_db_next_search_entry_ext_fn,
    NULL, /* slot 230 available */
    NULL, /* slot 231 available */
    slapi_pblock_get_plugin_cleanup_fn,
    slapi_pblock_get_plugin_poststart_fn,
    slapi_pblock_get_plugin_db_wire_import_fn,
    slapi_pblock_get_plugin_db_upgradedb_fn,
    slapi_pblock_get_plugin_db_dbverify_fn,
    NULL, /* slot 237 available */
    slapi_pblock_get_plugin_db_search_results_release_fn,
    slapi_pblock_get_plugin_db_prev_search_results_fn,
    slapi_pblock_get_plugin_db_upgradednformat_fn,
    NULL, /* slot 241 available */
    NULL, /* slot 242 available */
    NULL, /* slot 243 available */
    NULL, /* slot 244 available */
    NULL, /* slot 245 available */
    NULL, /* slot 246 available */
    NULL, /* slot 247 available */
    NULL, /* slot 248 available */
    NULL, /* slot 249 available */
    slapi_pblock_get_plugin_db_no_acl,
    NULL, /* slot 251 available */
    NULL, /* slot 252 available */
    NULL, /* slot 253 available */
    NULL, /* slot 254 available */
    NULL, /* slot 255 available */
    NULL, /* slot 256 available */
    NULL, /* slot 257 available */
    NULL, /* slot 258 available */
    NULL, /* slot 259 available */
    NULL, /* slot 260 available */
    NULL, /* slot 261 available */
    NULL, /* slot 262 available */
    NULL, /* slot 263 available */
    NULL, /* slot 264 available */
    NULL, /* slot 265 available */
    NULL, /* slot 266 available */
    NULL, /* slot 267 available */
    NULL, /* slot 268 available */
    NULL, /* slot 269 available */
    NULL, /* slot 270 available */
    NULL, /* slot 271 available */
    NULL, /* slot 272 available */
    NULL, /* slot 273 available */
    NULL, /* slot 274 available */
    NULL, /* slot 275 available */
    NULL, /* slot 276 available */
    NULL, /* slot 277 available */
    NULL, /* slot 278 available */
    NULL, /* slot 279 available */
    slapi_pblock_get_plugin_db_rmdb_fn,
    slapi_pblock_get_config_directory,
    slapi_pblock_get_dse_dont_write_when_adding,
    slapi_pblock_get_dse_merge_when_adding,
    slapi_pblock_get_dse_dont_check_dups,
    slapi_pblock_get_schema_flags,
    slapi_pblock_get_urp_naming_collision_dn,
    slapi_pblock_get_dse_reapply_mods,
    slapi_pblock_get_urp_tombstone_uniqueid,
    slapi_pblock_get_dse_is_primary_file,
    slapi_pblock_get_plugin_db_get_info_fn,
    slapi_pblock_get_plugin_db_set_info_fn,
    slapi_pblock_get_plugin_db_ctrl_info_fn,
    slapi_pblock_get_urp_tombstone_conflict_dn,
    slapi_pblock_get_plugin_db_compact_fn,
    NULL, /* slot 295 available */
    NULL, /* slot 296 available */
    NULL, /* slot 297 available */
    NULL, /* slot 298 available */
    NULL, /* slot 299 available */
    slapi_pblock_get_plugin_ext_op_fn,
    slapi_pblock_get_plugin_ext_op_oidlist,
    slapi_pblock_get_plugin_ext_op_namelist,
    slapi_pblock_get_ldif_encrypted,
    slapi_pblock_get_ldif_encrypted, /* intentional duplicate */
    NULL, /* slot 305 available */
    NULL, /* slot 306 available */
    NULL, /* slot 307 available */
    NULL, /* slot 308 available */
    NULL, /* slot 309 available */
    NULL, /* slot 310 available */
    NULL, /* slot 311 available */
    NULL, /* slot 312 available */
    NULL, /* slot 313 available */
    NULL, /* slot 314 available */
    NULL, /* slot 315 available */
    NULL, /* slot 316 available */
    NULL, /* slot 317 available */
    NULL, /* slot 318 available */
    NULL, /* slot 319 available */
    NULL, /* slot 320 available */
    NULL, /* slot 321 available */
    NULL, /* slot 322 available */
    NULL, /* slot 323 available */
    NULL, /* slot 324 available */
    NULL, /* slot 325 available */
    NULL, /* slot 326 available */
    NULL, /* slot 327 available */
    NULL, /* slot 328 available */
    NULL, /* slot 329 available */
    NULL, /* slot 330 available */
    NULL, /* slot 331 available */
    NULL, /* slot 332 available */
    NULL, /* slot 333 available */
    NULL, /* slot 334 available */
    NULL, /* slot 335 available */
    NULL, /* slot 336 available */
    NULL, /* slot 337 available */
    NULL, /* slot 338 available */
    NULL, /* slot 339 available */
    NULL, /* slot 340 available */
    NULL, /* slot 341 available */
    NULL, /* slot 342 available */
    NULL, /* slot 343 available */
    NULL, /* slot 344 available */
    NULL, /* slot 345 available */
    NULL, /* slot 346 available */
    NULL, /* slot 347 available */
    NULL, /* slot 348 available */
    NULL, /* slot 349 available */
    NULL, /* slot 350 available */
    NULL, /* slot 351 available */
    NULL, /* slot 352 available */
    NULL, /* slot 353 available */
    NULL, /* slot 354 available */
    NULL, /* slot 355 available */
    NULL, /* slot 356 available */
    NULL, /* slot 357 available */
    NULL, /* slot 358 available */
    NULL, /* slot 359 available */
    NULL, /* slot 360 available */
    NULL, /* slot 361 available */
    NULL, /* slot 362 available */
    NULL, /* slot 363 available */
    NULL, /* slot 364 available */
    NULL, /* slot 365 available */
    NULL, /* slot 366 available */
    NULL, /* slot 367 available */
    NULL, /* slot 368 available */
    NULL, /* slot 369 available */
    NULL, /* slot 370 available */
    NULL, /* slot 371 available */
    NULL, /* slot 372 available */
    NULL, /* slot 373 available */
    NULL, /* slot 374 available */
    NULL, /* slot 375 available */
    NULL, /* slot 376 available */
    NULL, /* slot 377 available */
    NULL, /* slot 378 available */
    NULL, /* slot 379 available */
    NULL, /* slot 380 available */
    NULL, /* slot 381 available */
    NULL, /* slot 382 available */
    NULL, /* slot 383 available */
    NULL, /* slot 384 available */
    NULL, /* slot 385 available */
    NULL, /* slot 386 available */
    NULL, /* slot 387 available */
    NULL, /* slot 388 available */
    NULL, /* slot 389 available */
    NULL, /* slot 390 available */
    NULL, /* slot 391 available */
    NULL, /* slot 392 available */
    NULL, /* slot 393 available */
    NULL, /* slot 394 available */
    NULL, /* slot 395 available */
    NULL, /* slot 396 available */
    NULL, /* slot 397 available */
    NULL, /* slot 398 available */
    NULL, /* slot 399 available */
    NULL, /* slot 400 available */
    slapi_pblock_get_plugin_pre_bind_fn,
    slapi_pblock_get_plugin_pre_unbind_fn,
    slapi_pblock_get_plugin_pre_search_fn,
    slapi_pblock_get_plugin_pre_compare_fn,
    slapi_pblock_get_plugin_pre_modify_fn,
    slapi_pblock_get_plugin_pre_modrdn_fn,
    slapi_pblock_get_plugin_pre_add_fn,
    slapi_pblock_get_plugin_pre_delete_fn,
    slapi_pblock_get_plugin_pre_abandon_fn,
    slapi_pblock_get_plugin_pre_entry_fn,
    slapi_pblock_get_plugin_pre_referral_fn,
    slapi_pblock_get_plugin_pre_result_fn,
    slapi_pblock_get_plugin_pre_extop_fn,
    NULL, /* slot 414 available */
    NULL, /* slot 415 available */
    NULL, /* slot 416 available */
    NULL, /* slot 417 available */
    NULL, /* slot 418 available */
    NULL, /* slot 419 available */
    slapi_pblock_get_plugin_internal_pre_add_fn,
    slapi_pblock_get_plugin_internal_pre_modify_fn,
    slapi_pblock_get_plugin_internal_pre_modrdn_fn,
    slapi_pblock_get_plugin_internal_pre_delete_fn,
    slapi_pblock_get_plugin_internal_pre_bind_fn,
    NULL, /* slot 425 available */
    NULL, /* slot 426 available */
    NULL, /* slot 427 available */
    NULL, /* slot 428 available */
    NULL, /* slot 429 available */
    NULL, /* slot 430 available */
    NULL, /* slot 431 available */
    NULL, /* slot 432 available */
    NULL, /* slot 433 available */
    NULL, /* slot 434 available */
    NULL, /* slot 435 available */
    NULL, /* slot 436 available */
    NULL, /* slot 437 available */
    NULL, /* slot 438 available */
    NULL, /* slot 439 available */
    NULL, /* slot 440 available */
    NULL, /* slot 441 available */
    NULL, /* slot 442 available */
    NULL, /* slot 443 available */
    NULL, /* slot 444 available */
    NULL, /* slot 445 available */
    NULL, /* slot 446 available */
    NULL, /* slot 447 available */
    NULL, /* slot 448 available */
    NULL, /* slot 449 available */
    slapi_pblock_get_plugin_be_pre_add_fn,
    slapi_pblock_get_plugin_be_pre_modify_fn,
    slapi_pblock_get_plugin_be_pre_modrdn_fn,
    slapi_pblock_get_plugin_be_pre_delete_fn,
    slapi_pblock_get_plugin_be_pre_close_fn,
    NULL, /* slot 455 available */
    NULL, /* slot 456 available */
    NULL, /* slot 457 available */
    NULL, /* slot 458 available */
    NULL, /* slot 459 available */
    slapi_pblock_get_plugin_be_txn_pre_add_fn,
    slapi_pblock_get_plugin_be_txn_pre_modify_fn,
    slapi_pblock_get_plugin_be_txn_pre_modrdn_fn,
    slapi_pblock_get_plugin_be_txn_pre_delete_fn,
    slapi_pblock_get_plugin_be_txn_pre_delete_tombstone_fn,
    NULL, /* slot 465 available */
    NULL, /* slot 466 available */
    NULL, /* slot 467 available */
    NULL, /* slot 468 available */
    NULL, /* slot 469 available */
    NULL, /* slot 470 available */
    NULL, /* slot 471 available */
    NULL, /* slot 472 available */
    NULL, /* slot 473 available */
    NULL, /* slot 474 available */
    NULL, /* slot 475 available */
    NULL, /* slot 476 available */
    NULL, /* slot 477 available */
    NULL, /* slot 478 available */
    NULL, /* slot 479 available */
    NULL, /* slot 480 available */
    NULL, /* slot 481 available */
    NULL, /* slot 482 available */
    NULL, /* slot 483 available */
    NULL, /* slot 484 available */
    NULL, /* slot 485 available */
    NULL, /* slot 486 available */
    NULL, /* slot 487 available */
    NULL, /* slot 488 available */
    NULL, /* slot 489 available */
    NULL, /* slot 490 available */
    NULL, /* slot 491 available */
    NULL, /* slot 492 available */
    NULL, /* slot 493 available */
    NULL, /* slot 494 available */
    NULL, /* slot 495 available */
    NULL, /* slot 496 available */
    NULL, /* slot 497 available */
    NULL, /* slot 498 available */
    NULL, /* slot 499 available */
    NULL, /* slot 500 available */
    slapi_pblock_get_plugin_post_bind_fn,
    slapi_pblock_get_plugin_post_unbind_fn,
    slapi_pblock_get_plugin_post_search_fn,
    slapi_pblock_get_plugin_post_compare_fn,
    slapi_pblock_get_plugin_post_modify_fn,
    slapi_pblock_get_plugin_post_modrdn_fn,
    slapi_pblock_get_plugin_post_add_fn,
    slapi_pblock_get_plugin_post_delete_fn,
    slapi_pblock_get_plugin_post_abandon_fn,
    slapi_pblock_get_plugin_post_entry_fn,
    slapi_pblock_get_plugin_post_referral_fn,
    slapi_pblock_get_plugin_post_result_fn,
    slapi_pblock_get_plugin_post_search_fail_fn,
    slapi_pblock_get_plugin_post_extop_fn,
    NULL, /* slot 515 available */
    NULL, /* slot 516 available */
    NULL, /* slot 517 available */
    NULL, /* slot 518 available */
    NULL, /* slot 519 available */
    slapi_pblock_get_plugin_internal_post_add_fn,
    slapi_pblock_get_plugin_internal_post_modify_fn,
    slapi_pblock_get_plugin_internal_post_modrdn_fn,
    slapi_pblock_get_plugin_internal_post_delete_fn,
    NULL, /* slot 524 available */
    NULL, /* slot 525 available */
    NULL, /* slot 526 available */
    NULL, /* slot 527 available */
    NULL, /* slot 528 available */
    NULL, /* slot 529 available */
    NULL, /* slot 530 available */
    NULL, /* slot 531 available */
    NULL, /* slot 532 available */
    NULL, /* slot 533 available */
    NULL, /* slot 534 available */
    NULL, /* slot 535 available */
    NULL, /* slot 536 available */
    NULL, /* slot 537 available */
    NULL, /* slot 538 available */
    NULL, /* slot 539 available */
    NULL, /* slot 540 available */
    NULL, /* slot 541 available */
    NULL, /* slot 542 available */
    NULL, /* slot 543 available */
    NULL, /* slot 544 available */
    NULL, /* slot 545 available */
    NULL, /* slot 546 available */
    NULL, /* slot 547 available */
    NULL, /* slot 548 available */
    NULL, /* slot 549 available */
    slapi_pblock_get_plugin_be_post_add_fn,
    slapi_pblock_get_plugin_be_post_modify_fn,
    slapi_pblock_get_plugin_be_post_modrdn_fn,
    slapi_pblock_get_plugin_be_post_delete_fn,
    slapi_pblock_get_plugin_be_post_open_fn,
    NULL, /* slot 555 available */
    slapi_pblock_get_plugin_be_post_export_fn,
    slapi_pblock_get_plugin_be_post_import_fn,
    NULL, /* slot 558 available */
    NULL, /* slot 559 available */
    slapi_pblock_get_plugin_be_txn_post_add_fn,
    slapi_pblock_get_plugin_be_txn_post_modify_fn,
    slapi_pblock_get_plugin_be_txn_post_modrdn_fn,
    slapi_pblock_get_plugin_be_txn_post_delete_fn,
    NULL, /* slot 564 available */
    NULL, /* slot 565 available */
    NULL, /* slot 566 available */
    NULL, /* slot 567 available */
    NULL, /* slot 568 available */
    NULL, /* slot 569 available */
    NULL, /* slot 570 available */
    NULL, /* slot 571 available */
    NULL, /* slot 572 available */
    NULL, /* slot 573 available */
    NULL, /* slot 574 available */
    NULL, /* slot 575 available */
    NULL, /* slot 576 available */
    NULL, /* slot 577 available */
    NULL, /* slot 578 available */
    NULL, /* slot 579 available */
    NULL, /* slot 580 available */
    NULL, /* slot 581 available */
    NULL, /* slot 582 available */
    NULL, /* slot 583 available */
    NULL, /* slot 584 available */
    NULL, /* slot 585 available */
    NULL, /* slot 586 available */
    NULL, /* slot 587 available */
    NULL, /* slot 588 available */
    NULL, /* slot 589 available */
    slapi_pblock_get_operation_type,
    NULL, /* slot 591 available */
    NULL, /* slot 592 available */
    NULL, /* slot 593 available */
    NULL, /* slot 594 available */
    NULL, /* slot 595 available */
    NULL, /* slot 596 available */
    NULL, /* slot 597 available */
    NULL, /* slot 598 available */
    NULL, /* slot 599 available */
    slapi_pblock_get_plugin_mr_filter_create_fn,
    slapi_pblock_get_plugin_mr_indexer_create_fn,
    slapi_pblock_get_plugin_mr_filter_match_fn,
    slapi_pblock_get_plugin_mr_filter_index_fn,
    slapi_pblock_get_plugin_mr_filter_reset_fn,
    slapi_pblock_get_plugin_mr_index_fn,
    slapi_pblock_get_plugin_mr_index_sv_fn,
    NULL, /* slot 607 available */
    NULL, /* slot 608 available */
    NULL, /* slot 609 available */
    slapi_pblock_get_plugin_mr_oid,
    slapi_pblock_get_plugin_mr_type,
    slapi_pblock_get_plugin_mr_value,
    slapi_pblock_get_plugin_mr_values,
    slapi_pblock_get_plugin_mr_keys,
    slapi_pblock_get_plugin_mr_filter_reusable,
    slapi_pblock_get_plugin_mr_query_operator,
    slapi_pblock_get_plugin_mr_usage,
    slapi_pblock_get_plugin_mr_filter_ava,
    slapi_pblock_get_plugin_mr_filter_sub,
    slapi_pblock_get_plugin_mr_values2keys,
    slapi_pblock_get_plugin_mr_assertion2keys_ava,
    slapi_pblock_get_plugin_mr_assertion2keys_sub,
    slapi_pblock_get_plugin_mr_flags,
    slapi_pblock_get_plugin_mr_names,
    slapi_pblock_get_plugin_mr_compare,
    slapi_pblock_get_plugin_mr_normalize,
    NULL, /* slot 627 available */
    NULL, /* slot 628 available */
    NULL, /* slot 629 available */
    NULL, /* slot 630 available */
    NULL, /* slot 631 available */
    NULL, /* slot 632 available */
    NULL, /* slot 633 available */
    NULL, /* slot 634 available */
    NULL, /* slot 635 available */
    NULL, /* slot 636 available */
    NULL, /* slot 637 available */
    NULL, /* slot 638 available */
    NULL, /* slot 639 available */
    NULL, /* slot 640 available */
    NULL, /* slot 641 available */
    NULL, /* slot 642 available */
    NULL, /* slot 643 available */
    NULL, /* slot 644 available */
    NULL, /* slot 645 available */
    NULL, /* slot 646 available */
    NULL, /* slot 647 available */
    NULL, /* slot 648 available */
    NULL, /* slot 649 available */
    NULL, /* slot 650 available */
    NULL, /* slot 651 available */
    NULL, /* slot 652 available */
    NULL, /* slot 653 available */
    NULL, /* slot 654 available */
    NULL, /* slot 655 available */
    NULL, /* slot 656 available */
    NULL, /* slot 657 available */
    NULL, /* slot 658 available */
    NULL, /* slot 659 available */
    NULL, /* slot 660 available */
    NULL, /* slot 661 available */
    NULL, /* slot 662 available */
    NULL, /* slot 663 available */
    NULL, /* slot 664 available */
    NULL, /* slot 665 available */
    NULL, /* slot 666 available */
    NULL, /* slot 667 available */
    NULL, /* slot 668 available */
    NULL, /* slot 669 available */
    NULL, /* slot 670 available */
    NULL, /* slot 671 available */
    NULL, /* slot 672 available */
    NULL, /* slot 673 available */
    NULL, /* slot 674 available */
    NULL, /* slot 675 available */
    NULL, /* slot 676 available */
    NULL, /* slot 677 available */
    NULL, /* slot 678 available */
    NULL, /* slot 679 available */
    NULL, /* slot 680 available */
    NULL, /* slot 681 available */
    NULL, /* slot 682 available */
    NULL, /* slot 683 available */
    NULL, /* slot 684 available */
    NULL, /* slot 685 available */
    NULL, /* slot 686 available */
    NULL, /* slot 687 available */
    NULL, /* slot 688 available */
    NULL, /* slot 689 available */
    NULL, /* slot 690 available */
    NULL, /* slot 691 available */
    NULL, /* slot 692 available */
    NULL, /* slot 693 available */
    NULL, /* slot 694 available */
    NULL, /* slot 695 available */
    NULL, /* slot 696 available */
    NULL, /* slot 697 available */
    NULL, /* slot 698 available */
    NULL, /* slot 699 available */
    slapi_pblock_get_plugin_syntax_filter_ava,
    slapi_pblock_get_plugin_syntax_filter_sub,
    slapi_pblock_get_plugin_syntax_values2keys,
    slapi_pblock_get_plugin_syntax_assertion2keys_ava,
    slapi_pblock_get_plugin_syntax_assertion2keys_sub,
    slapi_pblock_get_plugin_syntax_names,
    slapi_pblock_get_plugin_syntax_oid,
    slapi_pblock_get_plugin_syntax_flags,
    slapi_pblock_get_plugin_syntax_compare,
    slapi_pblock_get_syntax_substrlens,
    slapi_pblock_get_plugin_syntax_validate,
    slapi_pblock_get_plugin_syntax_normalize,
    slapi_pblock_get_plugin_syntax_filter_normalized,
    slapi_pblock_get_plugin_syntax_filter_data,
    NULL, /* slot 714 available */
    NULL, /* slot 715 available */
    NULL, /* slot 716 available */
    NULL, /* slot 717 available */
    NULL, /* slot 718 available */
    NULL, /* slot 719 available */
    NULL, /* slot 720 available */
    NULL, /* slot 721 available */
    NULL, /* slot 722 available */
    NULL, /* slot 723 available */
    NULL, /* slot 724 available */
    NULL, /* slot 725 available */
    NULL, /* slot 726 available */
    NULL, /* slot 727 available */
    NULL, /* slot 728 available */
    NULL, /* slot 729 available */
    slapi_pblock_get_plugin_acl_init,
    slapi_pblock_get_plugin_acl_syntax_check,
    slapi_pblock_get_plugin_acl_allow_access,
    slapi_pblock_get_plugin_acl_mods_allowed,
    slapi_pblock_get_plugin_acl_mods_update,
    NULL, /* slot 735 available */
    NULL, /* slot 736 available */
    NULL, /* slot 737 available */
    NULL, /* slot 738 available */
    NULL, /* slot 739 available */
    NULL, /* slot 740 available */
    slapi_pblock_get_operation_authtype,
    slapi_pblock_get_be_maxnestlevel,
    slapi_pblock_get_conn_cert,
    slapi_pblock_get_operation_id,
    slapi_pblock_get_client_dns,
    slapi_pblock_get_conn_authmethod,
    slapi_pblock_get_conn_is_ssl_session,
    slapi_pblock_get_conn_sasl_ssf,
    slapi_pblock_get_conn_ssl_ssf,
    slapi_pblock_get_operation_ssf,
    slapi_pblock_get_conn_local_ssf,
    NULL, /* slot 752 available */
    NULL, /* slot 753 available */
    NULL, /* slot 754 available */
    NULL, /* slot 755 available */
    NULL, /* slot 756 available */
    NULL, /* slot 757 available */
    NULL, /* slot 758 available */
    NULL, /* slot 759 available */
    NULL, /* slot 760 available */
    slapi_pblock_get_plugin_mmr_betxn_preop,
    slapi_pblock_get_plugin_mmr_betxn_postop,
    NULL, /* slot 763 available */
    NULL, /* slot 764 available */
    NULL, /* slot 765 available */
    NULL, /* slot 766 available */
    NULL, /* slot 767 available */
    NULL, /* slot 768 available */
    NULL, /* slot 769 available */
    NULL, /* slot 770 available */
    NULL, /* slot 771 available */
    NULL, /* slot 772 available */
    NULL, /* slot 773 available */
    NULL, /* slot 774 available */
    NULL, /* slot 775 available */
    NULL, /* slot 776 available */
    NULL, /* slot 777 available */
    NULL, /* slot 778 available */
    NULL, /* slot 779 available */
    NULL, /* slot 780 available */
    NULL, /* slot 781 available */
    NULL, /* slot 782 available */
    NULL, /* slot 783 available */
    NULL, /* slot 784 available */
    NULL, /* slot 785 available */
    NULL, /* slot 786 available */
    NULL, /* slot 787 available */
    NULL, /* slot 788 available */
    NULL, /* slot 789 available */
    NULL, /* slot 790 available */
    NULL, /* slot 791 available */
    NULL, /* slot 792 available */
    NULL, /* slot 793 available */
    NULL, /* slot 794 available */
    NULL, /* slot 795 available */
    NULL, /* slot 796 available */
    NULL, /* slot 797 available */
    NULL, /* slot 798 available */
    NULL, /* slot 799 available */
    slapi_pblock_get_plugin_pwd_storage_scheme_enc_fn,
    slapi_pblock_get_plugin_pwd_storage_scheme_dec_fn,
    slapi_pblock_get_plugin_pwd_storage_scheme_cmp_fn,
    NULL, /* slot 803 available */
    NULL, /* slot 804 available */
    NULL, /* slot 805 available */
    NULL, /* slot 806 available */
    NULL, /* slot 807 available */
    NULL, /* slot 808 available */
    NULL, /* slot 809 available */
    slapi_pblock_get_plugin_pwd_storage_scheme_name,
    slapi_pblock_get_plugin_pwd_storage_scheme_user_pwd,
    slapi_pblock_get_plugin_pwd_storage_scheme_db_pwd,
    slapi_pblock_get_plugin_entry_fetch_func,
    slapi_pblock_get_plugin_entry_store_func,
    slapi_pblock_get_plugin_enabled,
    slapi_pblock_get_plugin_config_area,
    slapi_pblock_get_plugin_config_dn,
    NULL, /* slot 818 available */
    NULL, /* slot 819 available */
    NULL, /* slot 820 available */
    NULL, /* slot 821 available */
    NULL, /* slot 822 available */
    NULL, /* slot 823 available */
    NULL, /* slot 824 available */
    NULL, /* slot 825 available */
    NULL, /* slot 826 available */
    NULL, /* slot 827 available */
    NULL, /* slot 828 available */
    NULL, /* slot 829 available */
    NULL, /* slot 830 available */
    NULL, /* slot 831 available */
    NULL, /* slot 832 available */
    NULL, /* slot 833 available */
    NULL, /* slot 834 available */
    NULL, /* slot 835 available */
    NULL, /* slot 836 available */
    NULL, /* slot 837 available */
    NULL, /* slot 838 available */
    NULL, /* slot 839 available */
    NULL, /* slot 840 available */
    NULL, /* slot 841 available */
    NULL, /* slot 842 available */
    NULL, /* slot 843 available */
    NULL, /* slot 844 available */
    NULL, /* slot 845 available */
    NULL, /* slot 846 available */
    NULL, /* slot 847 available */
    NULL, /* slot 848 available */
    NULL, /* slot 849 available */
    slapi_pblock_get_conn_clientnetaddr,
    slapi_pblock_get_conn_servernetaddr,
    slapi_pblock_get_requestor_sdn,
    slapi_pblock_get_conn_clientnetaddr_aclip,
    NULL, /* slot 854 available */
    NULL, /* slot 855 available */
    NULL, /* slot 856 available */
    NULL, /* slot 857 available */
    NULL, /* slot 858 available */
    NULL, /* slot 859 available */
    slapi_pblock_get_backend_count,
    slapi_pblock_get_deferred_memberof,
    NULL, /* slot 862 available */
    NULL, /* slot 863 available */
    NULL, /* slot 864 available */
    NULL, /* slot 865 available */
    NULL, /* slot 866 available */
    NULL, /* slot 867 available */
    NULL, /* slot 868 available */
    NULL, /* slot 869 available */
    NULL, /* slot 870 available */
    NULL, /* slot 871 available */
    NULL, /* slot 872 available */
    NULL, /* slot 873 available */
    NULL, /* slot 874 available */
    NULL, /* slot 875 available */
    NULL, /* slot 876 available */
    NULL, /* slot 877 available */
    NULL, /* slot 878 available */
    NULL, /* slot 879 available */
    NULL, /* slot 880 available */
    slapi_pblock_get_result_code,
    slapi_pblock_get_result_text,
    slapi_pblock_get_result_matched,
    NULL, /* slot 884 available */
    slapi_pblock_get_pb_result_text,
    NULL, /* slot 886 available */
    NULL, /* slot 887 available */
    NULL, /* slot 888 available */
    NULL, /* slot 889 available */
    NULL, /* slot 890 available */
    NULL, /* slot 891 available */
    NULL, /* slot 892 available */
    NULL, /* slot 893 available */
    NULL, /* slot 894 available */
    NULL, /* slot 895 available */
    NULL, /* slot 896 available */
    NULL, /* slot 897 available */
    NULL, /* slot 898 available */
    NULL, /* slot 899 available */
    NULL, /* slot 900 available */
    NULL, /* slot 901 available */
    NULL, /* slot 902 available */
    NULL, /* slot 903 available */
    NULL, /* slot 904 available */
    NULL, /* slot 905 available */
    NULL, /* slot 906 available */
    NULL, /* slot 907 available */
    NULL, /* slot 908 available */
    NULL, /* slot 909 available */
    NULL, /* slot 910 available */
    NULL, /* slot 911 available */
    NULL, /* slot 912 available */
    NULL, /* slot 913 available */
    NULL, /* slot 914 available */
    NULL, /* slot 915 available */
    NULL, /* slot 916 available */
    NULL, /* slot 917 available */
    NULL, /* slot 918 available */
    NULL, /* slot 919 available */
    NULL, /* slot 920 available */
    NULL, /* slot 921 available */
    NULL, /* slot 922 available */
    NULL, /* slot 923 available */
    NULL, /* slot 924 available */
    NULL, /* slot 925 available */
    NULL, /* slot 926 available */
    NULL, /* slot 927 available */
    NULL, /* slot 928 available */
    NULL, /* slot 929 available */
    NULL, /* slot 930 available */
    NULL, /* slot 931 available */
    NULL, /* slot 932 available */
    NULL, /* slot 933 available */
    NULL, /* slot 934 available */
    NULL, /* slot 935 available */
    NULL, /* slot 936 available */
    NULL, /* slot 937 available */
    NULL, /* slot 938 available */
    NULL, /* slot 939 available */
    NULL, /* slot 940 available */
    NULL, /* slot 941 available */
    NULL, /* slot 942 available */
    NULL, /* slot 943 available */
    NULL, /* slot 944 available */
    NULL, /* slot 945 available */
    NULL, /* slot 946 available */
    NULL, /* slot 947 available */
    NULL, /* slot 948 available */
    NULL, /* slot 949 available */
    NULL, /* slot 950 available */
    NULL, /* slot 951 available */
    NULL, /* slot 952 available */
    NULL, /* slot 953 available */
    NULL, /* slot 954 available */
    NULL, /* slot 955 available */
    NULL, /* slot 956 available */
    NULL, /* slot 957 available */
    NULL, /* slot 958 available */
    NULL, /* slot 959 available */
    NULL, /* slot 960 available */
    NULL, /* slot 961 available */
    NULL, /* slot 962 available */
    NULL, /* slot 963 available */
    NULL, /* slot 964 available */
    NULL, /* slot 965 available */
    NULL, /* slot 966 available */
    NULL, /* slot 967 available */
    NULL, /* slot 968 available */
    NULL, /* slot 969 available */
    NULL, /* slot 970 available */
    NULL, /* slot 971 available */
    NULL, /* slot 972 available */
    NULL, /* slot 973 available */
    NULL, /* slot 974 available */
    NULL, /* slot 975 available */
    NULL, /* slot 976 available */
    NULL, /* slot 977 available */
    NULL, /* slot 978 available */
    NULL, /* slot 979 available */
    NULL, /* slot 980 available */
    NULL, /* slot 981 available */
    NULL, /* slot 982 available */
    NULL, /* slot 983 available */
    NULL, /* slot 984 available */
    NULL, /* slot 985 available */
    NULL, /* slot 986 available */
    NULL, /* slot 987 available */
    NULL, /* slot 988 available */
    NULL, /* slot 989 available */
    NULL, /* slot 990 available */
    NULL, /* slot 991 available */
    NULL, /* slot 992 available */
    NULL, /* slot 993 available */
    NULL, /* slot 994 available */
    NULL, /* slot 995 available */
    NULL, /* slot 996 available */
    NULL, /* slot 997 available */
    NULL, /* slot 998 available */
    NULL, /* slot 999 available */
    slapi_pblock_get_managedsait,
    slapi_pblock_get_pwpolicy,
    slapi_pblock_get_session_tracking,
    NULL, /* slot 1003 available */
    NULL, /* slot 1004 available */
    NULL, /* slot 1005 available */
    NULL, /* slot 1006 available */
    NULL, /* slot 1007 available */
    NULL, /* slot 1008 available */
    NULL, /* slot 1009 available */
    NULL, /* slot 1010 available */
    NULL, /* slot 1011 available */
    NULL, /* slot 1012 available */
    NULL, /* slot 1013 available */
    NULL, /* slot 1014 available */
    NULL, /* slot 1015 available */
    NULL, /* slot 1016 available */
    NULL, /* slot 1017 available */
    NULL, /* slot 1018 available */
    NULL, /* slot 1019 available */
    NULL, /* slot 1020 available */
    NULL, /* slot 1021 available */
    NULL, /* slot 1022 available */
    NULL, /* slot 1023 available */
    NULL, /* slot 1024 available */
    NULL, /* slot 1025 available */
    NULL, /* slot 1026 available */
    NULL, /* slot 1027 available */
    NULL, /* slot 1028 available */
    NULL, /* slot 1029 available */
    NULL, /* slot 1030 available */
    NULL, /* slot 1031 available */
    NULL, /* slot 1032 available */
    NULL, /* slot 1033 available */
    NULL, /* slot 1034 available */
    NULL, /* slot 1035 available */
    NULL, /* slot 1036 available */
    NULL, /* slot 1037 available */
    NULL, /* slot 1038 available */
    NULL, /* slot 1039 available */
    NULL, /* slot 1040 available */
    NULL, /* slot 1041 available */
    NULL, /* slot 1042 available */
    NULL, /* slot 1043 available */
    NULL, /* slot 1044 available */
    NULL, /* slot 1045 available */
    NULL, /* slot 1046 available */
    NULL, /* slot 1047 available */
    NULL, /* slot 1048 available */
    NULL, /* slot 1049 available */
    NULL, /* slot 1050 available */
    NULL, /* slot 1051 available */
    NULL, /* slot 1052 available */
    NULL, /* slot 1053 available */
    NULL, /* slot 1054 available */
    NULL, /* slot 1055 available */
    NULL, /* slot 1056 available */
    NULL, /* slot 1057 available */
    NULL, /* slot 1058 available */
    NULL, /* slot 1059 available */
    NULL, /* slot 1060 available */
    NULL, /* slot 1061 available */
    NULL, /* slot 1062 available */
    NULL, /* slot 1063 available */
    NULL, /* slot 1064 available */
    NULL, /* slot 1065 available */
    NULL, /* slot 1066 available */
    NULL, /* slot 1067 available */
    NULL, /* slot 1068 available */
    NULL, /* slot 1069 available */
    NULL, /* slot 1070 available */
    NULL, /* slot 1071 available */
    NULL, /* slot 1072 available */
    NULL, /* slot 1073 available */
    NULL, /* slot 1074 available */
    NULL, /* slot 1075 available */
    NULL, /* slot 1076 available */
    NULL, /* slot 1077 available */
    NULL, /* slot 1078 available */
    NULL, /* slot 1079 available */
    NULL, /* slot 1080 available */
    NULL, /* slot 1081 available */
    NULL, /* slot 1082 available */
    NULL, /* slot 1083 available */
    NULL, /* slot 1084 available */
    NULL, /* slot 1085 available */
    NULL, /* slot 1086 available */
    NULL, /* slot 1087 available */
    NULL, /* slot 1088 available */
    NULL, /* slot 1089 available */
    NULL, /* slot 1090 available */
    NULL, /* slot 1091 available */
    NULL, /* slot 1092 available */
    NULL, /* slot 1093 available */
    NULL, /* slot 1094 available */
    NULL, /* slot 1095 available */
    NULL, /* slot 1096 available */
    NULL, /* slot 1097 available */
    NULL, /* slot 1098 available */
    NULL, /* slot 1099 available */
    NULL, /* slot 1100 available */
    NULL, /* slot 1101 available */
    NULL, /* slot 1102 available */
    NULL, /* slot 1103 available */
    NULL, /* slot 1104 available */
    NULL, /* slot 1105 available */
    NULL, /* slot 1106 available */
    NULL, /* slot 1107 available */
    NULL, /* slot 1108 available */
    NULL, /* slot 1109 available */
    NULL, /* slot 1110 available */
    NULL, /* slot 1111 available */
    NULL, /* slot 1112 available */
    NULL, /* slot 1113 available */
    NULL, /* slot 1114 available */
    NULL, /* slot 1115 available */
    NULL, /* slot 1116 available */
    NULL, /* slot 1117 available */
    NULL, /* slot 1118 available */
    NULL, /* slot 1119 available */
    NULL, /* slot 1120 available */
    NULL, /* slot 1121 available */
    NULL, /* slot 1122 available */
    NULL, /* slot 1123 available */
    NULL, /* slot 1124 available */
    NULL, /* slot 1125 available */
    NULL, /* slot 1126 available */
    NULL, /* slot 1127 available */
    NULL, /* slot 1128 available */
    NULL, /* slot 1129 available */
    NULL, /* slot 1130 available */
    NULL, /* slot 1131 available */
    NULL, /* slot 1132 available */
    NULL, /* slot 1133 available */
    NULL, /* slot 1134 available */
    NULL, /* slot 1135 available */
    NULL, /* slot 1136 available */
    NULL, /* slot 1137 available */
    NULL, /* slot 1138 available */
    NULL, /* slot 1139 available */
    NULL, /* slot 1140 available */
    NULL, /* slot 1141 available */
    NULL, /* slot 1142 available */
    NULL, /* slot 1143 available */
    NULL, /* slot 1144 available */
    NULL, /* slot 1145 available */
    NULL, /* slot 1146 available */
    NULL, /* slot 1147 available */
    NULL, /* slot 1148 available */
    NULL, /* slot 1149 available */
    NULL, /* slot 1150 available */
    NULL, /* slot 1151 available */
    NULL, /* slot 1152 available */
    NULL, /* slot 1153 available */
    NULL, /* slot 1154 available */
    NULL, /* slot 1155 available */
    NULL, /* slot 1156 available */
    NULL, /* slot 1157 available */
    NULL, /* slot 1158 available */
    NULL, /* slot 1159 available */
    slapi_pblock_get_search_gerattrs,
    slapi_pblock_get_search_reqattrs,
    NULL, /* slot 1162 available */
    NULL, /* slot 1163 available */
    NULL, /* slot 1164 available */
    NULL, /* slot 1165 available */
    NULL, /* slot 1166 available */
    NULL, /* slot 1167 available */
    NULL, /* slot 1168 available */
    NULL, /* slot 1169 available */
    NULL, /* slot 1170 available */
    NULL, /* slot 1171 available */
    NULL, /* slot 1172 available */
    NULL, /* slot 1173 available */
    NULL, /* slot 1174 available */
    NULL, /* slot 1175 available */
    NULL, /* slot 1176 available */
    NULL, /* slot 1177 available */
    NULL, /* slot 1178 available */
    NULL, /* slot 1179 available */
    NULL, /* slot 1180 available */
    NULL, /* slot 1181 available */
    NULL, /* slot 1182 available */
    NULL, /* slot 1183 available */
    NULL, /* slot 1184 available */
    NULL, /* slot 1185 available */
    NULL, /* slot 1186 available */
    NULL, /* slot 1187 available */
    NULL, /* slot 1188 available */
    NULL, /* slot 1189 available */
    NULL, /* slot 1190 available */
    NULL, /* slot 1191 available */
    NULL, /* slot 1192 available */
    NULL, /* slot 1193 available */
    NULL, /* slot 1194 available */
    NULL, /* slot 1195 available */
    NULL, /* slot 1196 available */
    NULL, /* slot 1197 available */
    NULL, /* slot 1198 available */
    NULL, /* slot 1199 available */
    NULL, /* slot 1200 available */
    NULL, /* slot 1201 available */
    NULL, /* slot 1202 available */
    NULL, /* slot 1203 available */
    NULL, /* slot 1204 available */
    NULL, /* slot 1205 available */
    NULL, /* slot 1206 available */
    NULL, /* slot 1207 available */
    NULL, /* slot 1208 available */
    NULL, /* slot 1209 available */
    NULL, /* slot 1210 available */
    NULL, /* slot 1211 available */
    NULL, /* slot 1212 available */
    NULL, /* slot 1213 available */
    NULL, /* slot 1214 available */
    NULL, /* slot 1215 available */
    NULL, /* slot 1216 available */
    NULL, /* slot 1217 available */
    NULL, /* slot 1218 available */
    NULL, /* slot 1219 available */
    NULL, /* slot 1220 available */
    NULL, /* slot 1221 available */
    NULL, /* slot 1222 available */
    NULL, /* slot 1223 available */
    NULL, /* slot 1224 available */
    NULL, /* slot 1225 available */
    NULL, /* slot 1226 available */
    NULL, /* slot 1227 available */
    NULL, /* slot 1228 available */
    NULL, /* slot 1229 available */
    NULL, /* slot 1230 available */
    NULL, /* slot 1231 available */
    NULL, /* slot 1232 available */
    NULL, /* slot 1233 available */
    NULL, /* slot 1234 available */
    NULL, /* slot 1235 available */
    NULL, /* slot 1236 available */
    NULL, /* slot 1237 available */
    NULL, /* slot 1238 available */
    NULL, /* slot 1239 available */
    NULL, /* slot 1240 available */
    NULL, /* slot 1241 available */
    NULL, /* slot 1242 available */
    NULL, /* slot 1243 available */
    NULL, /* slot 1244 available */
    NULL, /* slot 1245 available */
    NULL, /* slot 1246 available */
    NULL, /* slot 1247 available */
    NULL, /* slot 1248 available */
    NULL, /* slot 1249 available */
    NULL, /* slot 1250 available */
    NULL, /* slot 1251 available */
    NULL, /* slot 1252 available */
    NULL, /* slot 1253 available */
    NULL, /* slot 1254 available */
    NULL, /* slot 1255 available */
    NULL, /* slot 1256 available */
    NULL, /* slot 1257 available */
    NULL, /* slot 1258 available */
    NULL, /* slot 1259 available */
    NULL, /* slot 1260 available */
    NULL, /* slot 1261 available */
    NULL, /* slot 1262 available */
    NULL, /* slot 1263 available */
    NULL, /* slot 1264 available */
    NULL, /* slot 1265 available */
    NULL, /* slot 1266 available */
    NULL, /* slot 1267 available */
    NULL, /* slot 1268 available */
    NULL, /* slot 1269 available */
    NULL, /* slot 1270 available */
    NULL, /* slot 1271 available */
    NULL, /* slot 1272 available */
    NULL, /* slot 1273 available */
    NULL, /* slot 1274 available */
    NULL, /* slot 1275 available */
    NULL, /* slot 1276 available */
    NULL, /* slot 1277 available */
    NULL, /* slot 1278 available */
    NULL, /* slot 1279 available */
    NULL, /* slot 1280 available */
    NULL, /* slot 1281 available */
    NULL, /* slot 1282 available */
    NULL, /* slot 1283 available */
    NULL, /* slot 1284 available */
    NULL, /* slot 1285 available */
    NULL, /* slot 1286 available */
    NULL, /* slot 1287 available */
    NULL, /* slot 1288 available */
    NULL, /* slot 1289 available */
    NULL, /* slot 1290 available */
    NULL, /* slot 1291 available */
    NULL, /* slot 1292 available */
    NULL, /* slot 1293 available */
    NULL, /* slot 1294 available */
    NULL, /* slot 1295 available */
    NULL, /* slot 1296 available */
    NULL, /* slot 1297 available */
    NULL, /* slot 1298 available */
    NULL, /* slot 1299 available */
    NULL, /* slot 1300 available */
    NULL, /* slot 1301 available */
    NULL, /* slot 1302 available */
    NULL, /* slot 1303 available */
    NULL, /* slot 1304 available */
    NULL, /* slot 1305 available */
    NULL, /* slot 1306 available */
    NULL, /* slot 1307 available */
    NULL, /* slot 1308 available */
    NULL, /* slot 1309 available */
    NULL, /* slot 1310 available */
    NULL, /* slot 1311 available */
    NULL, /* slot 1312 available */
    NULL, /* slot 1313 available */
    NULL, /* slot 1314 available */
    NULL, /* slot 1315 available */
    NULL, /* slot 1316 available */
    NULL, /* slot 1317 available */
    NULL, /* slot 1318 available */
    NULL, /* slot 1319 available */
    NULL, /* slot 1320 available */
    NULL, /* slot 1321 available */
    NULL, /* slot 1322 available */
    NULL, /* slot 1323 available */
    NULL, /* slot 1324 available */
    NULL, /* slot 1325 available */
    NULL, /* slot 1326 available */
    NULL, /* slot 1327 available */
    NULL, /* slot 1328 available */
    NULL, /* slot 1329 available */
    NULL, /* slot 1330 available */
    NULL, /* slot 1331 available */
    NULL, /* slot 1332 available */
    NULL, /* slot 1333 available */
    NULL, /* slot 1334 available */
    NULL, /* slot 1335 available */
    NULL, /* slot 1336 available */
    NULL, /* slot 1337 available */
    NULL, /* slot 1338 available */
    NULL, /* slot 1339 available */
    NULL, /* slot 1340 available */
    NULL, /* slot 1341 available */
    NULL, /* slot 1342 available */
    NULL, /* slot 1343 available */
    NULL, /* slot 1344 available */
    NULL, /* slot 1345 available */
    NULL, /* slot 1346 available */
    NULL, /* slot 1347 available */
    NULL, /* slot 1348 available */
    NULL, /* slot 1349 available */
    NULL, /* slot 1350 available */
    NULL, /* slot 1351 available */
    NULL, /* slot 1352 available */
    NULL, /* slot 1353 available */
    NULL, /* slot 1354 available */
    NULL, /* slot 1355 available */
    NULL, /* slot 1356 available */
    NULL, /* slot 1357 available */
    NULL, /* slot 1358 available */
    NULL, /* slot 1359 available */
    NULL, /* slot 1360 available */
    NULL, /* slot 1361 available */
    NULL, /* slot 1362 available */
    NULL, /* slot 1363 available */
    NULL, /* slot 1364 available */
    NULL, /* slot 1365 available */
    NULL, /* slot 1366 available */
    NULL, /* slot 1367 available */
    NULL, /* slot 1368 available */
    NULL, /* slot 1369 available */
    NULL, /* slot 1370 available */
    NULL, /* slot 1371 available */
    NULL, /* slot 1372 available */
    NULL, /* slot 1373 available */
    NULL, /* slot 1374 available */
    NULL, /* slot 1375 available */
    NULL, /* slot 1376 available */
    NULL, /* slot 1377 available */
    NULL, /* slot 1378 available */
    NULL, /* slot 1379 available */
    NULL, /* slot 1380 available */
    NULL, /* slot 1381 available */
    NULL, /* slot 1382 available */
    NULL, /* slot 1383 available */
    NULL, /* slot 1384 available */
    NULL, /* slot 1385 available */
    NULL, /* slot 1386 available */
    NULL, /* slot 1387 available */
    NULL, /* slot 1388 available */
    NULL, /* slot 1389 available */
    NULL, /* slot 1390 available */
    NULL, /* slot 1391 available */
    NULL, /* slot 1392 available */
    NULL, /* slot 1393 available */
    NULL, /* slot 1394 available */
    NULL, /* slot 1395 available */
    NULL, /* slot 1396 available */
    NULL, /* slot 1397 available */
    NULL, /* slot 1398 available */
    NULL, /* slot 1399 available */
    NULL, /* slot 1400 available */
    NULL, /* slot 1401 available */
    NULL, /* slot 1402 available */
    NULL, /* slot 1403 available */
    NULL, /* slot 1404 available */
    NULL, /* slot 1405 available */
    NULL, /* slot 1406 available */
    NULL, /* slot 1407 available */
    NULL, /* slot 1408 available */
    NULL, /* slot 1409 available */
    NULL, /* slot 1410 available */
    NULL, /* slot 1411 available */
    NULL, /* slot 1412 available */
    NULL, /* slot 1413 available */
    NULL, /* slot 1414 available */
    NULL, /* slot 1415 available */
    NULL, /* slot 1416 available */
    NULL, /* slot 1417 available */
    NULL, /* slot 1418 available */
    NULL, /* slot 1419 available */
    NULL, /* slot 1420 available */
    NULL, /* slot 1421 available */
    NULL, /* slot 1422 available */
    NULL, /* slot 1423 available */
    NULL, /* slot 1424 available */
    NULL, /* slot 1425 available */
    NULL, /* slot 1426 available */
    NULL, /* slot 1427 available */
    NULL, /* slot 1428 available */
    NULL, /* slot 1429 available */
    NULL, /* slot 1430 available */
    NULL, /* slot 1431 available */
    NULL, /* slot 1432 available */
    NULL, /* slot 1433 available */
    NULL, /* slot 1434 available */
    NULL, /* slot 1435 available */
    NULL, /* slot 1436 available */
    NULL, /* slot 1437 available */
    NULL, /* slot 1438 available */
    NULL, /* slot 1439 available */
    NULL, /* slot 1440 available */
    NULL, /* slot 1441 available */
    NULL, /* slot 1442 available */
    NULL, /* slot 1443 available */
    NULL, /* slot 1444 available */
    NULL, /* slot 1445 available */
    NULL, /* slot 1446 available */
    NULL, /* slot 1447 available */
    NULL, /* slot 1448 available */
    NULL, /* slot 1449 available */
    NULL, /* slot 1450 available */
    NULL, /* slot 1451 available */
    NULL, /* slot 1452 available */
    NULL, /* slot 1453 available */
    NULL, /* slot 1454 available */
    NULL, /* slot 1455 available */
    NULL, /* slot 1456 available */
    NULL, /* slot 1457 available */
    NULL, /* slot 1458 available */
    NULL, /* slot 1459 available */
    NULL, /* slot 1460 available */
    NULL, /* slot 1461 available */
    NULL, /* slot 1462 available */
    NULL, /* slot 1463 available */
    NULL, /* slot 1464 available */
    NULL, /* slot 1465 available */
    NULL, /* slot 1466 available */
    NULL, /* slot 1467 available */
    NULL, /* slot 1468 available */
    NULL, /* slot 1469 available */
    NULL, /* slot 1470 available */
    NULL, /* slot 1471 available */
    NULL, /* slot 1472 available */
    NULL, /* slot 1473 available */
    NULL, /* slot 1474 available */
    NULL, /* slot 1475 available */
    NULL, /* slot 1476 available */
    NULL, /* slot 1477 available */
    NULL, /* slot 1478 available */
    NULL, /* slot 1479 available */
    NULL, /* slot 1480 available */
    NULL, /* slot 1481 available */
    NULL, /* slot 1482 available */
    NULL, /* slot 1483 available */
    NULL, /* slot 1484 available */
    NULL, /* slot 1485 available */
    NULL, /* slot 1486 available */
    NULL, /* slot 1487 available */
    NULL, /* slot 1488 available */
    NULL, /* slot 1489 available */
    NULL, /* slot 1490 available */
    NULL, /* slot 1491 available */
    NULL, /* slot 1492 available */
    NULL, /* slot 1493 available */
    NULL, /* slot 1494 available */
    NULL, /* slot 1495 available */
    NULL, /* slot 1496 available */
    NULL, /* slot 1497 available */
    NULL, /* slot 1498 available */
    NULL, /* slot 1499 available */
    NULL, /* slot 1500 available */
    NULL, /* slot 1501 available */
    NULL, /* slot 1502 available */
    NULL, /* slot 1503 available */
    NULL, /* slot 1504 available */
    NULL, /* slot 1505 available */
    NULL, /* slot 1506 available */
    NULL, /* slot 1507 available */
    NULL, /* slot 1508 available */
    NULL, /* slot 1509 available */
    NULL, /* slot 1510 available */
    NULL, /* slot 1511 available */
    NULL, /* slot 1512 available */
    NULL, /* slot 1513 available */
    NULL, /* slot 1514 available */
    NULL, /* slot 1515 available */
    NULL, /* slot 1516 available */
    NULL, /* slot 1517 available */
    NULL, /* slot 1518 available */
    NULL, /* slot 1519 available */
    NULL, /* slot 1520 available */
    NULL, /* slot 1521 available */
    NULL, /* slot 1522 available */
    NULL, /* slot 1523 available */
    NULL, /* slot 1524 available */
    NULL, /* slot 1525 available */
    NULL, /* slot 1526 available */
    NULL, /* slot 1527 available */
    NULL, /* slot 1528 available */
    NULL, /* slot 1529 available */
    NULL, /* slot 1530 available */
    NULL, /* slot 1531 available */
    NULL, /* slot 1532 available */
    NULL, /* slot 1533 available */
    NULL, /* slot 1534 available */
    NULL, /* slot 1535 available */
    NULL, /* slot 1536 available */
    NULL, /* slot 1537 available */
    NULL, /* slot 1538 available */
    NULL, /* slot 1539 available */
    NULL, /* slot 1540 available */
    NULL, /* slot 1541 available */
    NULL, /* slot 1542 available */
    NULL, /* slot 1543 available */
    NULL, /* slot 1544 available */
    NULL, /* slot 1545 available */
    NULL, /* slot 1546 available */
    NULL, /* slot 1547 available */
    NULL, /* slot 1548 available */
    NULL, /* slot 1549 available */
    NULL, /* slot 1550 available */
    NULL, /* slot 1551 available */
    NULL, /* slot 1552 available */
    NULL, /* slot 1553 available */
    NULL, /* slot 1554 available */
    NULL, /* slot 1555 available */
    NULL, /* slot 1556 available */
    NULL, /* slot 1557 available */
    NULL, /* slot 1558 available */
    NULL, /* slot 1559 available */
    NULL, /* slot 1560 available */
    NULL, /* slot 1561 available */
    NULL, /* slot 1562 available */
    NULL, /* slot 1563 available */
    NULL, /* slot 1564 available */
    NULL, /* slot 1565 available */
    NULL, /* slot 1566 available */
    NULL, /* slot 1567 available */
    NULL, /* slot 1568 available */
    NULL, /* slot 1569 available */
    NULL, /* slot 1570 available */
    NULL, /* slot 1571 available */
    NULL, /* slot 1572 available */
    NULL, /* slot 1573 available */
    NULL, /* slot 1574 available */
    NULL, /* slot 1575 available */
    NULL, /* slot 1576 available */
    NULL, /* slot 1577 available */
    NULL, /* slot 1578 available */
    NULL, /* slot 1579 available */
    NULL, /* slot 1580 available */
    NULL, /* slot 1581 available */
    NULL, /* slot 1582 available */
    NULL, /* slot 1583 available */
    NULL, /* slot 1584 available */
    NULL, /* slot 1585 available */
    NULL, /* slot 1586 available */
    NULL, /* slot 1587 available */
    NULL, /* slot 1588 available */
    NULL, /* slot 1589 available */
    NULL, /* slot 1590 available */
    NULL, /* slot 1591 available */
    NULL, /* slot 1592 available */
    NULL, /* slot 1593 available */
    NULL, /* slot 1594 available */
    NULL, /* slot 1595 available */
    NULL, /* slot 1596 available */
    NULL, /* slot 1597 available */
    NULL, /* slot 1598 available */
    NULL, /* slot 1599 available */
    NULL, /* slot 1600 available */
    NULL, /* slot 1601 available */
    NULL, /* slot 1602 available */
    NULL, /* slot 1603 available */
    NULL, /* slot 1604 available */
    NULL, /* slot 1605 available */
    NULL, /* slot 1606 available */
    NULL, /* slot 1607 available */
    NULL, /* slot 1608 available */
    NULL, /* slot 1609 available */
    NULL, /* slot 1610 available */
    NULL, /* slot 1611 available */
    NULL, /* slot 1612 available */
    NULL, /* slot 1613 available */
    NULL, /* slot 1614 available */
    NULL, /* slot 1615 available */
    NULL, /* slot 1616 available */
    NULL, /* slot 1617 available */
    NULL, /* slot 1618 available */
    NULL, /* slot 1619 available */
    NULL, /* slot 1620 available */
    NULL, /* slot 1621 available */
    NULL, /* slot 1622 available */
    NULL, /* slot 1623 available */
    NULL, /* slot 1624 available */
    NULL, /* slot 1625 available */
    NULL, /* slot 1626 available */
    NULL, /* slot 1627 available */
    NULL, /* slot 1628 available */
    NULL, /* slot 1629 available */
    NULL, /* slot 1630 available */
    NULL, /* slot 1631 available */
    NULL, /* slot 1632 available */
    NULL, /* slot 1633 available */
    NULL, /* slot 1634 available */
    NULL, /* slot 1635 available */
    NULL, /* slot 1636 available */
    NULL, /* slot 1637 available */
    NULL, /* slot 1638 available */
    NULL, /* slot 1639 available */
    NULL, /* slot 1640 available */
    NULL, /* slot 1641 available */
    NULL, /* slot 1642 available */
    NULL, /* slot 1643 available */
    NULL, /* slot 1644 available */
    NULL, /* slot 1645 available */
    NULL, /* slot 1646 available */
    NULL, /* slot 1647 available */
    NULL, /* slot 1648 available */
    NULL, /* slot 1649 available */
    NULL, /* slot 1650 available */
    NULL, /* slot 1651 available */
    NULL, /* slot 1652 available */
    NULL, /* slot 1653 available */
    NULL, /* slot 1654 available */
    NULL, /* slot 1655 available */
    NULL, /* slot 1656 available */
    NULL, /* slot 1657 available */
    NULL, /* slot 1658 available */
    NULL, /* slot 1659 available */
    NULL, /* slot 1660 available */
    NULL, /* slot 1661 available */
    NULL, /* slot 1662 available */
    NULL, /* slot 1663 available */
    NULL, /* slot 1664 available */
    NULL, /* slot 1665 available */
    NULL, /* slot 1666 available */
    NULL, /* slot 1667 available */
    NULL, /* slot 1668 available */
    NULL, /* slot 1669 available */
    NULL, /* slot 1670 available */
    NULL, /* slot 1671 available */
    NULL, /* slot 1672 available */
    NULL, /* slot 1673 available */
    NULL, /* slot 1674 available */
    NULL, /* slot 1675 available */
    NULL, /* slot 1676 available */
    NULL, /* slot 1677 available */
    NULL, /* slot 1678 available */
    NULL, /* slot 1679 available */
    NULL, /* slot 1680 available */
    NULL, /* slot 1681 available */
    NULL, /* slot 1682 available */
    NULL, /* slot 1683 available */
    NULL, /* slot 1684 available */
    NULL, /* slot 1685 available */
    NULL, /* slot 1686 available */
    NULL, /* slot 1687 available */
    NULL, /* slot 1688 available */
    NULL, /* slot 1689 available */
    NULL, /* slot 1690 available */
    NULL, /* slot 1691 available */
    NULL, /* slot 1692 available */
    NULL, /* slot 1693 available */
    NULL, /* slot 1694 available */
    NULL, /* slot 1695 available */
    NULL, /* slot 1696 available */
    NULL, /* slot 1697 available */
    NULL, /* slot 1698 available */
    NULL, /* slot 1699 available */
    NULL, /* slot 1700 available */
    NULL, /* slot 1701 available */
    NULL, /* slot 1702 available */
    NULL, /* slot 1703 available */
    NULL, /* slot 1704 available */
    NULL, /* slot 1705 available */
    NULL, /* slot 1706 available */
    NULL, /* slot 1707 available */
    NULL, /* slot 1708 available */
    NULL, /* slot 1709 available */
    NULL, /* slot 1710 available */
    NULL, /* slot 1711 available */
    NULL, /* slot 1712 available */
    NULL, /* slot 1713 available */
    NULL, /* slot 1714 available */
    NULL, /* slot 1715 available */
    NULL, /* slot 1716 available */
    NULL, /* slot 1717 available */
    NULL, /* slot 1718 available */
    NULL, /* slot 1719 available */
    NULL, /* slot 1720 available */
    NULL, /* slot 1721 available */
    NULL, /* slot 1722 available */
    NULL, /* slot 1723 available */
    NULL, /* slot 1724 available */
    NULL, /* slot 1725 available */
    NULL, /* slot 1726 available */
    NULL, /* slot 1727 available */
    NULL, /* slot 1728 available */
    NULL, /* slot 1729 available */
    NULL, /* slot 1730 available */
    NULL, /* slot 1731 available */
    NULL, /* slot 1732 available */
    NULL, /* slot 1733 available */
    NULL, /* slot 1734 available */
    NULL, /* slot 1735 available */
    NULL, /* slot 1736 available */
    NULL, /* slot 1737 available */
    NULL, /* slot 1738 available */
    NULL, /* slot 1739 available */
    NULL, /* slot 1740 available */
    NULL, /* slot 1741 available */
    NULL, /* slot 1742 available */
    NULL, /* slot 1743 available */
    NULL, /* slot 1744 available */
    NULL, /* slot 1745 available */
    NULL, /* slot 1746 available */
    NULL, /* slot 1747 available */
    NULL, /* slot 1748 available */
    NULL, /* slot 1749 available */
    NULL, /* slot 1750 available */
    NULL, /* slot 1751 available */
    NULL, /* slot 1752 available */
    NULL, /* slot 1753 available */
    NULL, /* slot 1754 available */
    NULL, /* slot 1755 available */
    NULL, /* slot 1756 available */
    NULL, /* slot 1757 available */
    NULL, /* slot 1758 available */
    NULL, /* slot 1759 available */
    NULL, /* slot 1760 available */
    slapi_pblock_get_ldif_changelog,
    NULL, /* slot 1762 available */
    NULL, /* slot 1763 available */
    NULL, /* slot 1764 available */
    NULL, /* slot 1765 available */
    NULL, /* slot 1766 available */
    NULL, /* slot 1767 available */
    NULL, /* slot 1768 available */
    NULL, /* slot 1769 available */
    NULL, /* slot 1770 available */
    NULL, /* slot 1771 available */
    NULL, /* slot 1772 available */
    NULL, /* slot 1773 available */
    NULL, /* slot 1774 available */
    NULL, /* slot 1775 available */
    NULL, /* slot 1776 available */
    NULL, /* slot 1777 available */
    NULL, /* slot 1778 available */
    NULL, /* slot 1779 available */
    NULL, /* slot 1780 available */
    NULL, /* slot 1781 available */
    NULL, /* slot 1782 available */
    NULL, /* slot 1783 available */
    NULL, /* slot 1784 available */
    NULL, /* slot 1785 available */
    NULL, /* slot 1786 available */
    NULL, /* slot 1787 available */
    NULL, /* slot 1788 available */
    NULL, /* slot 1789 available */
    NULL, /* slot 1790 available */
    NULL, /* slot 1791 available */
    NULL, /* slot 1792 available */
    NULL, /* slot 1793 available */
    NULL, /* slot 1794 available */
    NULL, /* slot 1795 available */
    NULL, /* slot 1796 available */
    NULL, /* slot 1797 available */
    NULL, /* slot 1798 available */
    NULL, /* slot 1799 available */
    NULL, /* slot 1800 available */
    NULL, /* slot 1801 available */
    NULL, /* slot 1802 available */
    NULL, /* slot 1803 available */
    NULL, /* slot 1804 available */
    NULL, /* slot 1805 available */
    NULL, /* slot 1806 available */
    NULL, /* slot 1807 available */
    NULL, /* slot 1808 available */
    NULL, /* slot 1809 available */
    NULL, /* slot 1810 available */
    NULL, /* slot 1811 available */
    NULL, /* slot 1812 available */
    NULL, /* slot 1813 available */
    NULL, /* slot 1814 available */
    NULL, /* slot 1815 available */
    NULL, /* slot 1816 available */
    NULL, /* slot 1817 available */
    NULL, /* slot 1818 available */
    NULL, /* slot 1819 available */
    NULL, /* slot 1820 available */
    NULL, /* slot 1821 available */
    NULL, /* slot 1822 available */
    NULL, /* slot 1823 available */
    NULL, /* slot 1824 available */
    NULL, /* slot 1825 available */
    NULL, /* slot 1826 available */
    NULL, /* slot 1827 available */
    NULL, /* slot 1828 available */
    NULL, /* slot 1829 available */
    NULL, /* slot 1830 available */
    NULL, /* slot 1831 available */
    NULL, /* slot 1832 available */
    NULL, /* slot 1833 available */
    NULL, /* slot 1834 available */
    NULL, /* slot 1835 available */
    NULL, /* slot 1836 available */
    NULL, /* slot 1837 available */
    NULL, /* slot 1838 available */
    NULL, /* slot 1839 available */
    NULL, /* slot 1840 available */
    NULL, /* slot 1841 available */
    NULL, /* slot 1842 available */
    NULL, /* slot 1843 available */
    NULL, /* slot 1844 available */
    NULL, /* slot 1845 available */
    NULL, /* slot 1846 available */
    NULL, /* slot 1847 available */
    NULL, /* slot 1848 available */
    NULL, /* slot 1849 available */
    NULL, /* slot 1850 available */
    NULL, /* slot 1851 available */
    NULL, /* slot 1852 available */
    NULL, /* slot 1853 available */
    NULL, /* slot 1854 available */
    NULL, /* slot 1855 available */
    NULL, /* slot 1856 available */
    NULL, /* slot 1857 available */
    NULL, /* slot 1858 available */
    NULL, /* slot 1859 available */
    NULL, /* slot 1860 available */
    NULL, /* slot 1861 available */
    NULL, /* slot 1862 available */
    NULL, /* slot 1863 available */
    NULL, /* slot 1864 available */
    NULL, /* slot 1865 available */
    NULL, /* slot 1866 available */
    NULL, /* slot 1867 available */
    NULL, /* slot 1868 available */
    NULL, /* slot 1869 available */
    NULL, /* slot 1870 available */
    NULL, /* slot 1871 available */
    NULL, /* slot 1872 available */
    NULL, /* slot 1873 available */
    NULL, /* slot 1874 available */
    NULL, /* slot 1875 available */
    NULL, /* slot 1876 available */
    NULL, /* slot 1877 available */
    NULL, /* slot 1878 available */
    NULL, /* slot 1879 available */
    NULL, /* slot 1880 available */
    NULL, /* slot 1881 available */
    NULL, /* slot 1882 available */
    NULL, /* slot 1883 available */
    NULL, /* slot 1884 available */
    NULL, /* slot 1885 available */
    NULL, /* slot 1886 available */
    NULL, /* slot 1887 available */
    NULL, /* slot 1888 available */
    NULL, /* slot 1889 available */
    NULL, /* slot 1890 available */
    NULL, /* slot 1891 available */
    NULL, /* slot 1892 available */
    NULL, /* slot 1893 available */
    NULL, /* slot 1894 available */
    NULL, /* slot 1895 available */
    NULL, /* slot 1896 available */
    NULL, /* slot 1897 available */
    NULL, /* slot 1898 available */
    NULL, /* slot 1899 available */
    NULL, /* slot 1900 available */
    slapi_pblock_get_txn_ruv_mods_fn,
    NULL, /* slot 1902 available */
    NULL, /* slot 1903 available */
    NULL, /* slot 1904 available */
    NULL, /* slot 1905 available */
    NULL, /* slot 1906 available */
    NULL, /* slot 1907 available */
    NULL, /* slot 1908 available */
    NULL, /* slot 1909 available */
    NULL, /* slot 1910 available */
    NULL, /* slot 1911 available */
    NULL, /* slot 1912 available */
    NULL, /* slot 1913 available */
    NULL, /* slot 1914 available */
    NULL, /* slot 1915 available */
    NULL, /* slot 1916 available */
    NULL, /* slot 1917 available */
    NULL, /* slot 1918 available */
    NULL, /* slot 1919 available */
    NULL, /* slot 1920 available */
    NULL, /* slot 1921 available */
    NULL, /* slot 1922 available */
    NULL, /* slot 1923 available */
    NULL, /* slot 1924 available */
    NULL, /* slot 1925 available */
    NULL, /* slot 1926 available */
    NULL, /* slot 1927 available */
    NULL, /* slot 1928 available */
    NULL, /* slot 1929 available */
    slapi_pblock_get_search_result_set_size_estimate,
    NULL, /* slot 1931 available */
    NULL, /* slot 1932 available */
    NULL, /* slot 1933 available */
    NULL, /* slot 1934 available */
    NULL, /* slot 1935 available */
    NULL, /* slot 1936 available */
    NULL, /* slot 1937 available */
    NULL, /* slot 1938 available */
    NULL, /* slot 1939 available */
    NULL, /* slot 1940 available */
    NULL, /* slot 1941 available */
    NULL, /* slot 1942 available */
    NULL, /* slot 1943 available */
    slapi_pblock_get_search_result_entry_ext,
    slapi_pblock_get_paged_results_index,
    slapi_pblock_get_aci_target_check,
    slapi_pblock_get_dbverify_dbdir,
    slapi_pblock_get_plugin_ext_op_backend_fn,
    slapi_pblock_get_paged_results_cookie,
    slapi_pblock_get_usn_increment_for_tombstone,
    slapi_pblock_get_memberof_deferred_task,
    NULL, /* slot 1952 available */
};

int32_t
slapi_pblock_get(Slapi_PBlock *pblock, int arg, void *value)
{
    PR_ASSERT(NULL != pblock);
    PR_ASSERT(NULL != value);

    if (arg > 0 && arg < PR_ARRAY_SIZE(get_cbtable) && get_cbtable[arg] != NULL) {
        return get_cbtable[arg](pblock, value);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pblock_get",
                      "Unknown parameter block argument %d\n", arg);
        PR_ASSERT(0);
        return (-1);
    }
}

/*
 * Callback table for setting slapi pblock parameters
 */
static int32_t (*set_cbtable[])(Slapi_PBlock *, void *) = {
    NULL,
    NULL, /* slot 1 available */
    NULL, /* slot 2 available */
    slapi_pblock_set_plugin,
    slapi_pblock_set_plugin_private,
    slapi_pblock_set_plugin_type,
    slapi_pblock_set_plugin_argv,
    slapi_pblock_set_plugin_argc,
    slapi_pblock_set_plugin_version,
    slapi_pblock_set_plugin_opreturn,
    slapi_pblock_set_plugin_object,
    slapi_pblock_set_plugin_destroy_fn,
    slapi_pblock_set_plugin_description,
    slapi_pblock_set_plugin_identity,
    slapi_pblock_set_plugin_precedence,
    slapi_pblock_set_plugin_intop_result,
    slapi_pblock_set_plugin_intop_search_entries,
    slapi_pblock_set_plugin_intop_search_referrals,
    NULL, /* slot 18 available */
    NULL, /* slot 19 available */
    NULL, /* slot 20 available */
    NULL, /* slot 21 available */
    NULL, /* slot 22 available */
    NULL, /* slot 23 available */
    NULL, /* slot 24 available */
    NULL, /* slot 25 available */
    NULL, /* slot 26 available */
    NULL, /* slot 27 available */
    NULL, /* slot 28 available */
    NULL, /* slot 29 available */
    NULL, /* slot 30 available */
    NULL, /* slot 31 available */
    NULL, /* slot 32 available */
    NULL, /* slot 33 available */
    NULL, /* slot 34 available */
    NULL, /* slot 35 available */
    NULL, /* slot 36 available */
    NULL, /* slot 37 available */
    NULL, /* slot 38 available */
    NULL, /* slot 39 available */
    NULL, /* slapi_pblock_set_config_filename - deprecated since DS 5.0 */
    NULL, /* slapi_pblock_set_config_lineno -  deprecated since DS 5.0 */
    NULL, /* slapi_pblock_set_config_argc - deprecated since DS 5.0 */
    NULL, /* slapi_pblock_set_config_argv -  deprecated since DS 5.0 */
    NULL, /* slot 44 available */
    NULL, /* slot 45 available */
    NULL, /* slot 46 available */
    slapi_pblock_set_target_sdn,
    slapi_pblock_set_target_address,
    slapi_pblock_set_target_uniqueid,
    slapi_pblock_set_target_dn,
    slapi_pblock_set_reqcontrols,
    slapi_pblock_set_entry_pre_op,
    slapi_pblock_set_entry_post_op,
    NULL, /* slot 54 available */
    slapi_pblock_set_rescontrols,
    slapi_pblock_set_add_rescontrol,
    slapi_pblock_set_op_notes,
    slapi_pblock_set_controls_arg,
    slapi_pblock_set_destroy_content,
    slapi_pblock_set_add_entry,
    slapi_pblock_set_add_existing_dn_entry,
    slapi_pblock_set_add_parent_entry,
    slapi_pblock_set_add_parent_uniqueid,
    slapi_pblock_set_add_existing_uniqueid_entry,
    NULL, /* slot 65 available */
    NULL, /* slot 66 available */
    NULL, /* slot 67 available */
    NULL, /* slot 68 available */
    NULL, /* slot 69 available */
    slapi_pblock_set_bind_method,
    slapi_pblock_set_bind_credentials,
    slapi_pblock_set_bind_saslmechanism,
    slapi_pblock_set_bind_ret_saslcreds,
    NULL, /* slot 74 available */
    NULL, /* slot 75 available */
    NULL, /* slot 76 available */
    NULL, /* slot 77 available */
    NULL, /* slot 78 available */
    NULL, /* slot 79 available */
    slapi_pblock_set_compare_type,
    slapi_pblock_set_compare_value,
    NULL, /* slot 82 available */
    NULL, /* slot 83 available */
    NULL, /* slot 84 available */
    NULL, /* slot 85 available */
    NULL, /* slot 86 available */
    NULL, /* slot 87 available */
    NULL, /* slot 88 available */
    NULL, /* slot 89 available */
    slapi_pblock_set_modify_mods,
    NULL, /* slot 91 available */
    NULL, /* slot 92 available */
    NULL, /* slot 93 available */
    NULL, /* slot 94 available */
    NULL, /* slot 95 available */
    NULL, /* slot 96 available */
    NULL, /* slot 97 available */
    NULL, /* slot 98 available */
    NULL, /* slot 99 available */
    slapi_pblock_set_modrdn_newrdn,
    slapi_pblock_set_modrdn_deloldrdn,
    slapi_pblock_set_modrdn_newsuperior,
    slapi_pblock_set_modrdn_newsuperior_sdn,
    slapi_pblock_set_modrdn_parent_entry,
    slapi_pblock_set_modrdn_newparent_entry,
    slapi_pblock_set_modrdn_target_entry,
    slapi_pblock_set_modrdn_newsuperior_address,
    NULL, /* slot 108 available */
    slapi_pblock_set_original_target_dn,
    slapi_pblock_set_search_scope,
    slapi_pblock_set_search_deref,
    slapi_pblock_set_search_sizelimit,
    slapi_pblock_set_search_timelimit,
    slapi_pblock_set_search_filter,
    slapi_pblock_set_search_strfilter,
    slapi_pblock_set_search_attrs,
    slapi_pblock_set_search_attrsonly,
    slapi_pblock_set_search_is_and,
    slapi_pblock_set_search_filter_intended,
    slapi_pblock_set_abandon_msgid,
    NULL, /* slot 121 available */
    NULL, /* slot 122 available */
    NULL, /* slot 123 available */
    NULL, /* slot 124 available */
    NULL, /* slot 125 available */
    NULL, /* slot 126 available */
    NULL, /* slot 127 available */
    NULL, /* slot 128 available */
    NULL, /* slot 129 available */
    slapi_pblock_set_backend,
    slapi_pblock_set_connection,
    slapi_pblock_set_operation,
    slapi_pblock_set_requestor_isroot,
    NULL, /* slot 134 available */
    NULL, /* "set" function not implemented for SLAPI_BE_TYPE (135) */
    NULL, /* "set" function not implemented for SLAPI_BE_READONLY (136) */
    NULL, /* "set" function not implemented for SLAPI_BE_LASTMOD (137) */
    slapi_pblock_set_operation_parameters,
    slapi_pblock_set_conn_id,
    slapi_pblock_set_opinitiated_time,
    slapi_pblock_set_requestor_dn,
    slapi_pblock_set_is_replicated_operation,
    slapi_pblock_set_conn_dn,
    slapi_pblock_set_conn_authmethod, /* intentional duplicate - deprecated*/
    NULL, /* "set" function not implemented for SLAPI_CONN_CLIENTIP (145) */
    NULL, /* "set" function not implemented for SLAPI_CONN_SERVERIP (146) */
    slapi_pblock_set_argc,
    slapi_pblock_set_argv,
    slapi_pblock_set_conn_is_replication_session,
    slapi_pblock_set_seq_type,
    slapi_pblock_set_seq_attrname,
    slapi_pblock_set_seq_val,
    NULL, /* "set" function not implemented for SLAPI_IS_MMR_REPLICATED_OPERATION (153) */
    NULL, /* slot 154 available */
    slapi_pblock_set_skip_modified_attrs,
    NULL, /* "set" function not implemented for SLAPI_REQUESTOR_NDN (156) */
    NULL, /* slot 157 available */
    NULL, /* slot 158 available */
    NULL, /* slot 159 available */
    slapi_pblock_set_ext_op_req_oid,
    slapi_pblock_set_ext_op_req_value,
    slapi_pblock_set_ext_op_ret_oid,
    slapi_pblock_set_ext_op_ret_value,
    NULL, /* slot 164 available */
    NULL, /* slot 165 available */
    NULL, /* slot 166 available */
    NULL, /* slot 167 available */
    NULL, /* slot 168 available */
    NULL, /* slot 169 available */
    NULL, /* slot 170 available */
    NULL, /* slot 171 available */
    NULL, /* slot 172 available */
    NULL, /* slot 173 available */
    NULL, /* slot 174 available */
    slapi_pblock_set_ldif2db_generate_uniqueid,
    slapi_pblock_set_db2ldif_dump_uniqueid,
    slapi_pblock_set_ldif2db_namespaceid,
    slapi_pblock_set_backend_instance_name,
    slapi_pblock_set_backend_task,
    slapi_pblock_set_ldif2db_file,
    slapi_pblock_set_task_flags,
    slapi_pblock_set_bulk_import_entry,
    slapi_pblock_set_db2ldif_printkey,
    slapi_pblock_set_db2ldif_file,
    slapi_pblock_set_ldif2db_removedupvals,
    slapi_pblock_set_db2index_attrs,
    slapi_pblock_set_ldif2db_noattrindexes,
    slapi_pblock_set_ldif2db_include,
    slapi_pblock_set_ldif2db_exclude,
    slapi_pblock_set_parent_txn,
    slapi_pblock_set_txn,
    slapi_pblock_set_bulk_import_state,
    slapi_pblock_set_search_result_set,
    slapi_pblock_set_search_result_entry,
    slapi_pblock_set_nentries,
    slapi_pblock_set_search_referrals,
    slapi_pblock_set_db2ldif_server_running,
    slapi_pblock_set_search_ctrls,
    slapi_pblock_set_dbsize,
    slapi_pblock_set_plugin_db_bind_fn,
    slapi_pblock_set_plugin_db_unbind_fn,
    slapi_pblock_set_plugin_db_search_fn,
    slapi_pblock_set_plugin_db_compare_fn,
    slapi_pblock_set_plugin_db_modify_fn,
    slapi_pblock_set_plugin_db_modrdn_fn,
    slapi_pblock_set_plugin_db_add_fn,
    slapi_pblock_set_plugin_db_delete_fn,
    slapi_pblock_set_plugin_db_abandon_fn,
    slapi_pblock_set_plugin_db_config_fn,
    slapi_pblock_set_plugin_close_fn,
    NULL, /* slot 211 available */
    slapi_pblock_set_plugin_start_fn,
    slapi_pblock_set_plugin_db_seq_fn,
    slapi_pblock_set_plugin_db_entry_fn,
    slapi_pblock_set_plugin_db_referral_fn,
    slapi_pblock_set_plugin_db_result_fn,
    slapi_pblock_set_plugin_db_ldif2db_fn,
    slapi_pblock_set_plugin_db_db2ldif_fn,
    slapi_pblock_set_plugin_db_begin_fn,
    slapi_pblock_set_plugin_db_commit_fn,
    slapi_pblock_set_plugin_db_abort_fn,
    slapi_pblock_set_plugin_db_archive2db_fn,
    slapi_pblock_set_plugin_db_db2archive_fn,
    slapi_pblock_set_plugin_db_next_search_entry_fn,
    NULL, /* slot 225 available */
    NULL, /* slot 226 available */
    slapi_pblock_set_plugin_db_test_fn,
    slapi_pblock_set_plugin_db_db2index_fn,
    slapi_pblock_set_plugin_db_next_search_entry_ext_fn,
    NULL, /* slot 230 available */
    NULL, /* slot 231 available */
    slapi_pblock_set_plugin_cleanup_fn,
    slapi_pblock_set_plugin_poststart_fn,
    slapi_pblock_set_plugin_db_wire_import_fn,
    slapi_pblock_set_plugin_db_upgradedb_fn,
    slapi_pblock_set_plugin_db_dbverify_fn,
    NULL, /* slot 237 available */
    slapi_pblock_set_plugin_db_search_results_release_fn,
    slapi_pblock_set_plugin_db_prev_search_results_fn,
    slapi_pblock_set_plugin_db_upgradednformat_fn,
    NULL, /* slot 241 available */
    NULL, /* slot 242 available */
    NULL, /* slot 243 available */
    NULL, /* slot 244 available */
    NULL, /* slot 245 available */
    NULL, /* slot 246 available */
    NULL, /* slot 247 available */
    NULL, /* slot 248 available */
    NULL, /* slot 249 available */
    slapi_pblock_set_plugin_db_no_acl,
    NULL, /* slot 251 available */
    NULL, /* slot 252 available */
    NULL, /* slot 253 available */
    NULL, /* slot 254 available */
    NULL, /* slot 255 available */
    NULL, /* slot 256 available */
    NULL, /* slot 257 available */
    NULL, /* slot 258 available */
    NULL, /* slot 259 available */
    NULL, /* slot 260 available */
    NULL, /* slot 261 available */
    NULL, /* slot 262 available */
    NULL, /* slot 263 available */
    NULL, /* slot 264 available */
    NULL, /* slot 265 available */
    NULL, /* slot 266 available */
    NULL, /* slot 267 available */
    NULL, /* slot 268 available */
    NULL, /* slot 269 available */
    NULL, /* slot 270 available */
    NULL, /* slot 271 available */
    NULL, /* slot 272 available */
    NULL, /* slot 273 available */
    NULL, /* slot 274 available */
    NULL, /* slot 275 available */
    NULL, /* slot 276 available */
    NULL, /* slot 277 available */
    NULL, /* slot 278 available */
    NULL, /* slot 279 available */
    slapi_pblock_set_plugin_db_rmdb_fn,
    slapi_pblock_set_config_directory,
    slapi_pblock_set_dse_dont_write_when_adding,
    slapi_pblock_set_dse_merge_when_adding,
    slapi_pblock_set_dse_dont_check_dups,
    slapi_pblock_set_schema_flags,
    slapi_pblock_set_urp_naming_collision_dn,
    slapi_pblock_set_dse_reapply_mods,
    slapi_pblock_set_urp_tombstone_uniqueid,
    slapi_pblock_set_dse_is_primary_file,
    slapi_pblock_set_plugin_db_get_info_fn,
    slapi_pblock_set_plugin_db_set_info_fn,
    slapi_pblock_set_plugin_db_ctrl_info_fn,
    slapi_pblock_set_urp_tombstone_conflict_dn,
    slapi_pblock_set_plugin_db_compact_fn,
    NULL, /* slot 295 available */
    NULL, /* slot 296 available */
    NULL, /* slot 297 available */
    NULL, /* slot 298 available */
    NULL, /* slot 299 available */
    slapi_pblock_set_plugin_ext_op_fn,
    slapi_pblock_set_plugin_ext_op_oidlist,
    slapi_pblock_set_plugin_ext_op_namelist,
    slapi_pblock_set_ldif_encrypted,
    slapi_pblock_set_ldif_encrypted, /* intentional duplicate */
    NULL, /* slot 305 available */
    NULL, /* slot 306 available */
    NULL, /* slot 307 available */
    NULL, /* slot 308 available */
    NULL, /* slot 309 available */
    NULL, /* slot 310 available */
    NULL, /* slot 311 available */
    NULL, /* slot 312 available */
    NULL, /* slot 313 available */
    NULL, /* slot 314 available */
    NULL, /* slot 315 available */
    NULL, /* slot 316 available */
    NULL, /* slot 317 available */
    NULL, /* slot 318 available */
    NULL, /* slot 319 available */
    NULL, /* slot 320 available */
    NULL, /* slot 321 available */
    NULL, /* slot 322 available */
    NULL, /* slot 323 available */
    NULL, /* slot 324 available */
    NULL, /* slot 325 available */
    NULL, /* slot 326 available */
    NULL, /* slot 327 available */
    NULL, /* slot 328 available */
    NULL, /* slot 329 available */
    NULL, /* slot 330 available */
    NULL, /* slot 331 available */
    NULL, /* slot 332 available */
    NULL, /* slot 333 available */
    NULL, /* slot 334 available */
    NULL, /* slot 335 available */
    NULL, /* slot 336 available */
    NULL, /* slot 337 available */
    NULL, /* slot 338 available */
    NULL, /* slot 339 available */
    NULL, /* slot 340 available */
    NULL, /* slot 341 available */
    NULL, /* slot 342 available */
    NULL, /* slot 343 available */
    NULL, /* slot 344 available */
    NULL, /* slot 345 available */
    NULL, /* slot 346 available */
    NULL, /* slot 347 available */
    NULL, /* slot 348 available */
    NULL, /* slot 349 available */
    NULL, /* slot 350 available */
    NULL, /* slot 351 available */
    NULL, /* slot 352 available */
    NULL, /* slot 353 available */
    NULL, /* slot 354 available */
    NULL, /* slot 355 available */
    NULL, /* slot 356 available */
    NULL, /* slot 357 available */
    NULL, /* slot 358 available */
    NULL, /* slot 359 available */
    NULL, /* slot 360 available */
    NULL, /* slot 361 available */
    NULL, /* slot 362 available */
    NULL, /* slot 363 available */
    NULL, /* slot 364 available */
    NULL, /* slot 365 available */
    NULL, /* slot 366 available */
    NULL, /* slot 367 available */
    NULL, /* slot 368 available */
    NULL, /* slot 369 available */
    NULL, /* slot 370 available */
    NULL, /* slot 371 available */
    NULL, /* slot 372 available */
    NULL, /* slot 373 available */
    NULL, /* slot 374 available */
    NULL, /* slot 375 available */
    NULL, /* slot 376 available */
    NULL, /* slot 377 available */
    NULL, /* slot 378 available */
    NULL, /* slot 379 available */
    NULL, /* slot 380 available */
    NULL, /* slot 381 available */
    NULL, /* slot 382 available */
    NULL, /* slot 383 available */
    NULL, /* slot 384 available */
    NULL, /* slot 385 available */
    NULL, /* slot 386 available */
    NULL, /* slot 387 available */
    NULL, /* slot 388 available */
    NULL, /* slot 389 available */
    NULL, /* slot 390 available */
    NULL, /* slot 391 available */
    NULL, /* slot 392 available */
    NULL, /* slot 393 available */
    NULL, /* slot 394 available */
    NULL, /* slot 395 available */
    NULL, /* slot 396 available */
    NULL, /* slot 397 available */
    NULL, /* slot 398 available */
    NULL, /* slot 399 available */
    NULL, /* slot 400 available */
    slapi_pblock_set_plugin_pre_bind_fn,
    slapi_pblock_set_plugin_pre_unbind_fn,
    slapi_pblock_set_plugin_pre_search_fn,
    slapi_pblock_set_plugin_pre_compare_fn,
    slapi_pblock_set_plugin_pre_modify_fn,
    slapi_pblock_set_plugin_pre_modrdn_fn,
    slapi_pblock_set_plugin_pre_add_fn,
    slapi_pblock_set_plugin_pre_delete_fn,
    slapi_pblock_set_plugin_pre_abandon_fn,
    slapi_pblock_set_plugin_pre_entry_fn,
    slapi_pblock_set_plugin_pre_referral_fn,
    slapi_pblock_set_plugin_pre_result_fn,
    slapi_pblock_set_plugin_pre_extop_fn,
    NULL, /* slot 414 available */
    NULL, /* slot 415 available */
    NULL, /* slot 416 available */
    NULL, /* slot 417 available */
    NULL, /* slot 418 available */
    NULL, /* slot 419 available */
    slapi_pblock_set_plugin_internal_pre_add_fn,
    slapi_pblock_set_plugin_internal_pre_modify_fn,
    slapi_pblock_set_plugin_internal_pre_modrdn_fn,
    slapi_pblock_set_plugin_internal_pre_delete_fn,
    slapi_pblock_set_plugin_internal_pre_bind_fn,
    NULL, /* slot 425 available */
    NULL, /* slot 426 available */
    NULL, /* slot 427 available */
    NULL, /* slot 428 available */
    NULL, /* slot 429 available */
    NULL, /* slot 430 available */
    NULL, /* slot 431 available */
    NULL, /* slot 432 available */
    NULL, /* slot 433 available */
    NULL, /* slot 434 available */
    NULL, /* slot 435 available */
    NULL, /* slot 436 available */
    NULL, /* slot 437 available */
    NULL, /* slot 438 available */
    NULL, /* slot 439 available */
    NULL, /* slot 440 available */
    NULL, /* slot 441 available */
    NULL, /* slot 442 available */
    NULL, /* slot 443 available */
    NULL, /* slot 444 available */
    NULL, /* slot 445 available */
    NULL, /* slot 446 available */
    NULL, /* slot 447 available */
    NULL, /* slot 448 available */
    NULL, /* slot 449 available */
    slapi_pblock_set_plugin_be_pre_add_fn,
    slapi_pblock_set_plugin_be_pre_modify_fn,
    slapi_pblock_set_plugin_be_pre_modrdn_fn,
    slapi_pblock_set_plugin_be_pre_delete_fn,
    slapi_pblock_set_plugin_be_pre_close_fn,
    NULL, /* slot 455 available */
    NULL, /* slot 456 available */
    NULL, /* slot 457 available */
    NULL, /* slot 458 available */
    NULL, /* slot 459 available */
    slapi_pblock_set_plugin_be_txn_pre_add_fn,
    slapi_pblock_set_plugin_be_txn_pre_modify_fn,
    slapi_pblock_set_plugin_be_txn_pre_modrdn_fn,
    slapi_pblock_set_plugin_be_txn_pre_delete_fn,
    slapi_pblock_set_plugin_be_txn_pre_delete_tombstone_fn,
    NULL, /* slot 465 available */
    NULL, /* slot 466 available */
    NULL, /* slot 467 available */
    NULL, /* slot 468 available */
    NULL, /* slot 469 available */
    NULL, /* slot 470 available */
    NULL, /* slot 471 available */
    NULL, /* slot 472 available */
    NULL, /* slot 473 available */
    NULL, /* slot 474 available */
    NULL, /* slot 475 available */
    NULL, /* slot 476 available */
    NULL, /* slot 477 available */
    NULL, /* slot 478 available */
    NULL, /* slot 479 available */
    NULL, /* slot 480 available */
    NULL, /* slot 481 available */
    NULL, /* slot 482 available */
    NULL, /* slot 483 available */
    NULL, /* slot 484 available */
    NULL, /* slot 485 available */
    NULL, /* slot 486 available */
    NULL, /* slot 487 available */
    NULL, /* slot 488 available */
    NULL, /* slot 489 available */
    NULL, /* slot 490 available */
    NULL, /* slot 491 available */
    NULL, /* slot 492 available */
    NULL, /* slot 493 available */
    NULL, /* slot 494 available */
    NULL, /* slot 495 available */
    NULL, /* slot 496 available */
    NULL, /* slot 497 available */
    NULL, /* slot 498 available */
    NULL, /* slot 499 available */
    NULL, /* slot 500 available */
    slapi_pblock_set_plugin_post_bind_fn,
    slapi_pblock_set_plugin_post_unbind_fn,
    slapi_pblock_set_plugin_post_search_fn,
    slapi_pblock_set_plugin_post_compare_fn,
    slapi_pblock_set_plugin_post_modify_fn,
    slapi_pblock_set_plugin_post_modrdn_fn,
    slapi_pblock_set_plugin_post_add_fn,
    slapi_pblock_set_plugin_post_delete_fn,
    slapi_pblock_set_plugin_post_abandon_fn,
    slapi_pblock_set_plugin_post_entry_fn,
    slapi_pblock_set_plugin_post_referral_fn,
    slapi_pblock_set_plugin_post_result_fn,
    slapi_pblock_set_plugin_post_search_fail_fn,
    slapi_pblock_set_plugin_post_extop_fn,
    NULL, /* slot 515 available */
    NULL, /* slot 516 available */
    NULL, /* slot 517 available */
    NULL, /* slot 518 available */
    NULL, /* slot 519 available */
    slapi_pblock_set_plugin_internal_post_add_fn,
    slapi_pblock_set_plugin_internal_post_modify_fn,
    slapi_pblock_set_plugin_internal_post_modrdn_fn,
    slapi_pblock_set_plugin_internal_post_delete_fn,
    NULL, /* slot 524 available */
    NULL, /* slot 525 available */
    NULL, /* slot 526 available */
    NULL, /* slot 527 available */
    NULL, /* slot 528 available */
    NULL, /* slot 529 available */
    NULL, /* slot 530 available */
    NULL, /* slot 531 available */
    NULL, /* slot 532 available */
    NULL, /* slot 533 available */
    NULL, /* slot 534 available */
    NULL, /* slot 535 available */
    NULL, /* slot 536 available */
    NULL, /* slot 537 available */
    NULL, /* slot 538 available */
    NULL, /* slot 539 available */
    NULL, /* slot 540 available */
    NULL, /* slot 541 available */
    NULL, /* slot 542 available */
    NULL, /* slot 543 available */
    NULL, /* slot 544 available */
    NULL, /* slot 545 available */
    NULL, /* slot 546 available */
    NULL, /* slot 547 available */
    NULL, /* slot 548 available */
    NULL, /* slot 549 available */
    slapi_pblock_set_plugin_be_post_add_fn,
    slapi_pblock_set_plugin_be_post_modify_fn,
    slapi_pblock_set_plugin_be_post_modrdn_fn,
    slapi_pblock_set_plugin_be_post_delete_fn,
    slapi_pblock_set_plugin_be_post_open_fn,
    NULL, /* slot 555 available */
    slapi_pblock_set_plugin_be_post_export_fn,
    slapi_pblock_set_plugin_be_post_import_fn,
    NULL, /* slot 558 available */
    NULL, /* slot 559 available */
    slapi_pblock_set_plugin_be_txn_post_add_fn,
    slapi_pblock_set_plugin_be_txn_post_modify_fn,
    slapi_pblock_set_plugin_be_txn_post_modrdn_fn,
    slapi_pblock_set_plugin_be_txn_post_delete_fn,
    NULL, /* slot 564 available */
    NULL, /* slot 565 available */
    NULL, /* slot 566 available */
    NULL, /* slot 567 available */
    NULL, /* slot 568 available */
    NULL, /* slot 569 available */
    NULL, /* slot 570 available */
    NULL, /* slot 571 available */
    NULL, /* slot 572 available */
    NULL, /* slot 573 available */
    NULL, /* slot 574 available */
    NULL, /* slot 575 available */
    NULL, /* slot 576 available */
    NULL, /* slot 577 available */
    NULL, /* slot 578 available */
    NULL, /* slot 579 available */
    NULL, /* slot 580 available */
    NULL, /* slot 581 available */
    NULL, /* slot 582 available */
    NULL, /* slot 583 available */
    NULL, /* slot 584 available */
    NULL, /* slot 585 available */
    NULL, /* slot 586 available */
    NULL, /* slot 587 available */
    NULL, /* slot 588 available */
    NULL, /* slot 589 available */
    NULL, /* "set" function not implemented for SLAPI_OPERATION_TYPE (590) */
    NULL, /* slot 591 available */
    NULL, /* slot 592 available */
    NULL, /* slot 593 available */
    NULL, /* slot 594 available */
    NULL, /* slot 595 available */
    NULL, /* slot 596 available */
    NULL, /* slot 597 available */
    NULL, /* slot 598 available */
    NULL, /* slot 599 available */
    slapi_pblock_set_plugin_mr_filter_create_fn,
    slapi_pblock_set_plugin_mr_indexer_create_fn,
    slapi_pblock_set_plugin_mr_filter_match_fn,
    slapi_pblock_set_plugin_mr_filter_index_fn,
    slapi_pblock_set_plugin_mr_filter_reset_fn,
    slapi_pblock_set_plugin_mr_index_fn,
    slapi_pblock_set_plugin_mr_index_sv_fn,
    NULL, /* slot 607 available */
    NULL, /* slot 608 available */
    NULL, /* slot 609 available */
    slapi_pblock_set_plugin_mr_oid,
    slapi_pblock_set_plugin_mr_type,
    slapi_pblock_set_plugin_mr_value,
    slapi_pblock_set_plugin_mr_values,
    slapi_pblock_set_plugin_mr_keys,
    slapi_pblock_set_plugin_mr_filter_reusable,
    slapi_pblock_set_plugin_mr_query_operator,
    slapi_pblock_set_plugin_mr_usage,
    slapi_pblock_set_plugin_mr_filter_ava,
    slapi_pblock_set_plugin_mr_filter_sub,
    slapi_pblock_set_plugin_mr_values2keys,
    slapi_pblock_set_plugin_mr_assertion2keys_ava,
    slapi_pblock_set_plugin_mr_assertion2keys_sub,
    slapi_pblock_set_plugin_mr_flags,
    slapi_pblock_set_plugin_mr_names,
    slapi_pblock_set_plugin_mr_compare,
    slapi_pblock_set_plugin_mr_normalize,
    NULL, /* slot 627 available */
    NULL, /* slot 628 available */
    NULL, /* slot 629 available */
    NULL, /* slot 630 available */
    NULL, /* slot 631 available */
    NULL, /* slot 632 available */
    NULL, /* slot 633 available */
    NULL, /* slot 634 available */
    NULL, /* slot 635 available */
    NULL, /* slot 636 available */
    NULL, /* slot 637 available */
    NULL, /* slot 638 available */
    NULL, /* slot 639 available */
    NULL, /* slot 640 available */
    NULL, /* slot 641 available */
    NULL, /* slot 642 available */
    NULL, /* slot 643 available */
    NULL, /* slot 644 available */
    NULL, /* slot 645 available */
    NULL, /* slot 646 available */
    NULL, /* slot 647 available */
    NULL, /* slot 648 available */
    NULL, /* slot 649 available */
    NULL, /* slot 650 available */
    NULL, /* slot 651 available */
    NULL, /* slot 652 available */
    NULL, /* slot 653 available */
    NULL, /* slot 654 available */
    NULL, /* slot 655 available */
    NULL, /* slot 656 available */
    NULL, /* slot 657 available */
    NULL, /* slot 658 available */
    NULL, /* slot 659 available */
    NULL, /* slot 660 available */
    NULL, /* slot 661 available */
    NULL, /* slot 662 available */
    NULL, /* slot 663 available */
    NULL, /* slot 664 available */
    NULL, /* slot 665 available */
    NULL, /* slot 666 available */
    NULL, /* slot 667 available */
    NULL, /* slot 668 available */
    NULL, /* slot 669 available */
    NULL, /* slot 670 available */
    NULL, /* slot 671 available */
    NULL, /* slot 672 available */
    NULL, /* slot 673 available */
    NULL, /* slot 674 available */
    NULL, /* slot 675 available */
    NULL, /* slot 676 available */
    NULL, /* slot 677 available */
    NULL, /* slot 678 available */
    NULL, /* slot 679 available */
    NULL, /* slot 680 available */
    NULL, /* slot 681 available */
    NULL, /* slot 682 available */
    NULL, /* slot 683 available */
    NULL, /* slot 684 available */
    NULL, /* slot 685 available */
    NULL, /* slot 686 available */
    NULL, /* slot 687 available */
    NULL, /* slot 688 available */
    NULL, /* slot 689 available */
    NULL, /* slot 690 available */
    NULL, /* slot 691 available */
    NULL, /* slot 692 available */
    NULL, /* slot 693 available */
    NULL, /* slot 694 available */
    NULL, /* slot 695 available */
    NULL, /* slot 696 available */
    NULL, /* slot 697 available */
    NULL, /* slot 698 available */
    NULL, /* slot 699 available */
    slapi_pblock_set_plugin_syntax_filter_ava,
    slapi_pblock_set_plugin_syntax_filter_sub,
    slapi_pblock_set_plugin_syntax_values2keys,
    slapi_pblock_set_plugin_syntax_assertion2keys_ava,
    slapi_pblock_set_plugin_syntax_assertion2keys_sub,
    slapi_pblock_set_plugin_syntax_names,
    slapi_pblock_set_plugin_syntax_oid,
    slapi_pblock_set_plugin_syntax_flags,
    slapi_pblock_set_plugin_syntax_compare,
    slapi_pblock_set_syntax_substrlens,
    slapi_pblock_set_plugin_syntax_validate,
    slapi_pblock_set_plugin_syntax_normalize,
    slapi_pblock_set_plugin_syntax_filter_normalized,
    slapi_pblock_set_plugin_syntax_filter_data,
    NULL, /* slot 714 available */
    NULL, /* slot 715 available */
    NULL, /* slot 716 available */
    NULL, /* slot 717 available */
    NULL, /* slot 718 available */
    NULL, /* slot 719 available */
    NULL, /* slot 720 available */
    NULL, /* slot 721 available */
    NULL, /* slot 722 available */
    NULL, /* slot 723 available */
    NULL, /* slot 724 available */
    NULL, /* slot 725 available */
    NULL, /* slot 726 available */
    NULL, /* slot 727 available */
    NULL, /* slot 728 available */
    NULL, /* slot 729 available */
    slapi_pblock_set_plugin_acl_init,
    slapi_pblock_set_plugin_acl_syntax_check,
    slapi_pblock_set_plugin_acl_allow_access,
    slapi_pblock_set_plugin_acl_mods_allowed,
    slapi_pblock_set_plugin_acl_mods_update,
    NULL, /* slot 735 available */
    NULL, /* slot 736 available */
    NULL, /* slot 737 available */
    NULL, /* slot 738 available */
    NULL, /* slot 739 available */
    NULL, /* slot 740 available */
    NULL, /* "set" function not implemented for SLAPI_OPERATION_AUTHTYPE (741) */
    NULL, /* "set" function not implemented for SLAPI_BE_MAXNESTLEVEL (742) */
    NULL, /* "set" function not implemented for SLAPI_CONN_CERT (743) */
    NULL, /* "set" function not implemented for SLAPI_OPERATION_ID (744) */
    slapi_pblock_set_client_dns,
    slapi_pblock_set_conn_authmethod,
    NULL, /* "set" function not implemented for SLAPI_CONN_IS_SSL_SESSION (747) */
    NULL, /* "set" function not implemented for SLAPI_CONN_SASL_SSF (748) */
    NULL, /* "set" function not implemented for SLAPI_CONN_SSL_SSF (749) */
    NULL, /* "set" function not implemented for SLAPI_OPERATION_SSF (750) */
    NULL, /* "set" function not implemented for SLAPI_CONN_LOCAL_SSF (751) */
    NULL, /* slot 752 available */
    NULL, /* slot 753 available */
    NULL, /* slot 754 available */
    NULL, /* slot 755 available */
    NULL, /* slot 756 available */
    NULL, /* slot 757 available */
    NULL, /* slot 758 available */
    NULL, /* slot 759 available */
    NULL, /* slot 760 available */
    slapi_pblock_set_plugin_mmr_betxn_preop,
    slapi_pblock_set_plugin_mmr_betxn_postop,
    NULL, /* slot 763 available */
    NULL, /* slot 764 available */
    NULL, /* slot 765 available */
    NULL, /* slot 766 available */
    NULL, /* slot 767 available */
    NULL, /* slot 768 available */
    NULL, /* slot 769 available */
    NULL, /* slot 770 available */
    NULL, /* slot 771 available */
    NULL, /* slot 772 available */
    NULL, /* slot 773 available */
    NULL, /* slot 774 available */
    NULL, /* slot 775 available */
    NULL, /* slot 776 available */
    NULL, /* slot 777 available */
    NULL, /* slot 778 available */
    NULL, /* slot 779 available */
    NULL, /* slot 780 available */
    NULL, /* slot 781 available */
    NULL, /* slot 782 available */
    NULL, /* slot 783 available */
    NULL, /* slot 784 available */
    NULL, /* slot 785 available */
    NULL, /* slot 786 available */
    NULL, /* slot 787 available */
    NULL, /* slot 788 available */
    NULL, /* slot 789 available */
    NULL, /* slot 790 available */
    NULL, /* slot 791 available */
    NULL, /* slot 792 available */
    NULL, /* slot 793 available */
    NULL, /* slot 794 available */
    NULL, /* slot 795 available */
    NULL, /* slot 796 available */
    NULL, /* slot 797 available */
    NULL, /* slot 798 available */
    NULL, /* slot 799 available */
    slapi_pblock_set_plugin_pwd_storage_scheme_enc_fn,
    slapi_pblock_set_plugin_pwd_storage_scheme_dec_fn,
    slapi_pblock_set_plugin_pwd_storage_scheme_cmp_fn,
    NULL, /* slot 803 available */
    NULL, /* slot 804 available */
    NULL, /* slot 805 available */
    NULL, /* slot 806 available */
    NULL, /* slot 807 available */
    NULL, /* slot 808 available */
    NULL, /* slot 809 available */
    slapi_pblock_set_plugin_pwd_storage_scheme_name,
    slapi_pblock_set_plugin_pwd_storage_scheme_user_pwd,
    slapi_pblock_set_plugin_pwd_storage_scheme_db_pwd,
    slapi_pblock_set_plugin_entry_fetch_func,
    slapi_pblock_set_plugin_entry_store_func,
    slapi_pblock_set_plugin_enabled,
    slapi_pblock_set_plugin_config_area,
    NULL, /* "set" function not implemented for SLAPI_PLUGIN_CONFIG_DN (817) */
    NULL, /* slot 818 available */
    NULL, /* slot 819 available */
    NULL, /* slot 820 available */
    NULL, /* slot 821 available */
    NULL, /* slot 822 available */
    NULL, /* slot 823 available */
    NULL, /* slot 824 available */
    NULL, /* slot 825 available */
    NULL, /* slot 826 available */
    NULL, /* slot 827 available */
    NULL, /* slot 828 available */
    NULL, /* slot 829 available */
    NULL, /* slot 830 available */
    NULL, /* slot 831 available */
    NULL, /* slot 832 available */
    NULL, /* slot 833 available */
    NULL, /* slot 834 available */
    NULL, /* slot 835 available */
    NULL, /* slot 836 available */
    NULL, /* slot 837 available */
    NULL, /* slot 838 available */
    NULL, /* slot 839 available */
    NULL, /* slot 840 available */
    NULL, /* slot 841 available */
    NULL, /* slot 842 available */
    NULL, /* slot 843 available */
    NULL, /* slot 844 available */
    NULL, /* slot 845 available */
    NULL, /* slot 846 available */
    NULL, /* slot 847 available */
    NULL, /* slot 848 available */
    NULL, /* slot 849 available */
    NULL, /* "set" function not implemented for SLAPI_CONN_CLIENTNETADDR (850) */
    NULL, /* "set" function not implemented for SLAPI_CONN_SERVERNETADDR (851) */
    slapi_pblock_set_requestor_sdn,
    slapi_pblock_set_conn_clientnetaddr_aclip,
    NULL, /* slot 854 available */
    NULL, /* slot 855 available */
    NULL, /* slot 856 available */
    NULL, /* slot 857 available */
    NULL, /* slot 858 available */
    NULL, /* slot 859 available */
    slapi_pblock_set_backend_count,
    slapi_pblock_set_deferred_memberof,
    NULL, /* slot 862 available */
    NULL, /* slot 863 available */
    NULL, /* slot 864 available */
    NULL, /* slot 865 available */
    NULL, /* slot 866 available */
    NULL, /* slot 867 available */
    NULL, /* slot 868 available */
    NULL, /* slot 869 available */
    NULL, /* slot 870 available */
    NULL, /* slot 871 available */
    NULL, /* slot 872 available */
    NULL, /* slot 873 available */
    NULL, /* slot 874 available */
    NULL, /* slot 875 available */
    NULL, /* slot 876 available */
    NULL, /* slot 877 available */
    NULL, /* slot 878 available */
    NULL, /* slot 879 available */
    NULL, /* slot 880 available */
    slapi_pblock_set_result_code,
    slapi_pblock_set_result_text,
    slapi_pblock_set_result_matched,
    NULL, /* slot 884 available */
    slapi_pblock_set_pb_result_text,
    NULL, /* slot 886 available */
    NULL, /* slot 887 available */
    NULL, /* slot 888 available */
    NULL, /* slot 889 available */
    NULL, /* slot 890 available */
    NULL, /* slot 891 available */
    NULL, /* slot 892 available */
    NULL, /* slot 893 available */
    NULL, /* slot 894 available */
    NULL, /* slot 895 available */
    NULL, /* slot 896 available */
    NULL, /* slot 897 available */
    NULL, /* slot 898 available */
    NULL, /* slot 899 available */
    NULL, /* slot 900 available */
    NULL, /* slot 901 available */
    NULL, /* slot 902 available */
    NULL, /* slot 903 available */
    NULL, /* slot 904 available */
    NULL, /* slot 905 available */
    NULL, /* slot 906 available */
    NULL, /* slot 907 available */
    NULL, /* slot 908 available */
    NULL, /* slot 909 available */
    NULL, /* slot 910 available */
    NULL, /* slot 911 available */
    NULL, /* slot 912 available */
    NULL, /* slot 913 available */
    NULL, /* slot 914 available */
    NULL, /* slot 915 available */
    NULL, /* slot 916 available */
    NULL, /* slot 917 available */
    NULL, /* slot 918 available */
    NULL, /* slot 919 available */
    NULL, /* slot 920 available */
    NULL, /* slot 921 available */
    NULL, /* slot 922 available */
    NULL, /* slot 923 available */
    NULL, /* slot 924 available */
    NULL, /* slot 925 available */
    NULL, /* slot 926 available */
    NULL, /* slot 927 available */
    NULL, /* slot 928 available */
    NULL, /* slot 929 available */
    NULL, /* slot 930 available */
    NULL, /* slot 931 available */
    NULL, /* slot 932 available */
    NULL, /* slot 933 available */
    NULL, /* slot 934 available */
    NULL, /* slot 935 available */
    NULL, /* slot 936 available */
    NULL, /* slot 937 available */
    NULL, /* slot 938 available */
    NULL, /* slot 939 available */
    NULL, /* slot 940 available */
    NULL, /* slot 941 available */
    NULL, /* slot 942 available */
    NULL, /* slot 943 available */
    NULL, /* slot 944 available */
    NULL, /* slot 945 available */
    NULL, /* slot 946 available */
    NULL, /* slot 947 available */
    NULL, /* slot 948 available */
    NULL, /* slot 949 available */
    NULL, /* slot 950 available */
    NULL, /* slot 951 available */
    NULL, /* slot 952 available */
    NULL, /* slot 953 available */
    NULL, /* slot 954 available */
    NULL, /* slot 955 available */
    NULL, /* slot 956 available */
    NULL, /* slot 957 available */
    NULL, /* slot 958 available */
    NULL, /* slot 959 available */
    NULL, /* slot 960 available */
    NULL, /* slot 961 available */
    NULL, /* slot 962 available */
    NULL, /* slot 963 available */
    NULL, /* slot 964 available */
    NULL, /* slot 965 available */
    NULL, /* slot 966 available */
    NULL, /* slot 967 available */
    NULL, /* slot 968 available */
    NULL, /* slot 969 available */
    NULL, /* slot 970 available */
    NULL, /* slot 971 available */
    NULL, /* slot 972 available */
    NULL, /* slot 973 available */
    NULL, /* slot 974 available */
    NULL, /* slot 975 available */
    NULL, /* slot 976 available */
    NULL, /* slot 977 available */
    NULL, /* slot 978 available */
    NULL, /* slot 979 available */
    NULL, /* slot 980 available */
    NULL, /* slot 981 available */
    NULL, /* slot 982 available */
    NULL, /* slot 983 available */
    NULL, /* slot 984 available */
    NULL, /* slot 985 available */
    NULL, /* slot 986 available */
    NULL, /* slot 987 available */
    NULL, /* slot 988 available */
    NULL, /* slot 989 available */
    NULL, /* slot 990 available */
    NULL, /* slot 991 available */
    NULL, /* slot 992 available */
    NULL, /* slot 993 available */
    NULL, /* slot 994 available */
    NULL, /* slot 995 available */
    NULL, /* slot 996 available */
    NULL, /* slot 997 available */
    NULL, /* slot 998 available */
    NULL, /* slot 999 available */
    slapi_pblock_set_managedsait,
    slapi_pblock_set_pwpolicy,
    slapi_pblock_set_session_tracking,
    NULL, /* slot 1003 available */
    NULL, /* slot 1004 available */
    NULL, /* slot 1005 available */
    NULL, /* slot 1006 available */
    NULL, /* slot 1007 available */
    NULL, /* slot 1008 available */
    NULL, /* slot 1009 available */
    NULL, /* slot 1010 available */
    NULL, /* slot 1011 available */
    NULL, /* slot 1012 available */
    NULL, /* slot 1013 available */
    NULL, /* slot 1014 available */
    NULL, /* slot 1015 available */
    NULL, /* slot 1016 available */
    NULL, /* slot 1017 available */
    NULL, /* slot 1018 available */
    NULL, /* slot 1019 available */
    NULL, /* slot 1020 available */
    NULL, /* slot 1021 available */
    NULL, /* slot 1022 available */
    NULL, /* slot 1023 available */
    NULL, /* slot 1024 available */
    NULL, /* slot 1025 available */
    NULL, /* slot 1026 available */
    NULL, /* slot 1027 available */
    NULL, /* slot 1028 available */
    NULL, /* slot 1029 available */
    NULL, /* slot 1030 available */
    NULL, /* slot 1031 available */
    NULL, /* slot 1032 available */
    NULL, /* slot 1033 available */
    NULL, /* slot 1034 available */
    NULL, /* slot 1035 available */
    NULL, /* slot 1036 available */
    NULL, /* slot 1037 available */
    NULL, /* slot 1038 available */
    NULL, /* slot 1039 available */
    NULL, /* slot 1040 available */
    NULL, /* slot 1041 available */
    NULL, /* slot 1042 available */
    NULL, /* slot 1043 available */
    NULL, /* slot 1044 available */
    NULL, /* slot 1045 available */
    NULL, /* slot 1046 available */
    NULL, /* slot 1047 available */
    NULL, /* slot 1048 available */
    NULL, /* slot 1049 available */
    NULL, /* slot 1050 available */
    NULL, /* slot 1051 available */
    NULL, /* slot 1052 available */
    NULL, /* slot 1053 available */
    NULL, /* slot 1054 available */
    NULL, /* slot 1055 available */
    NULL, /* slot 1056 available */
    NULL, /* slot 1057 available */
    NULL, /* slot 1058 available */
    NULL, /* slot 1059 available */
    NULL, /* slot 1060 available */
    NULL, /* slot 1061 available */
    NULL, /* slot 1062 available */
    NULL, /* slot 1063 available */
    NULL, /* slot 1064 available */
    NULL, /* slot 1065 available */
    NULL, /* slot 1066 available */
    NULL, /* slot 1067 available */
    NULL, /* slot 1068 available */
    NULL, /* slot 1069 available */
    NULL, /* slot 1070 available */
    NULL, /* slot 1071 available */
    NULL, /* slot 1072 available */
    NULL, /* slot 1073 available */
    NULL, /* slot 1074 available */
    NULL, /* slot 1075 available */
    NULL, /* slot 1076 available */
    NULL, /* slot 1077 available */
    NULL, /* slot 1078 available */
    NULL, /* slot 1079 available */
    NULL, /* slot 1080 available */
    NULL, /* slot 1081 available */
    NULL, /* slot 1082 available */
    NULL, /* slot 1083 available */
    NULL, /* slot 1084 available */
    NULL, /* slot 1085 available */
    NULL, /* slot 1086 available */
    NULL, /* slot 1087 available */
    NULL, /* slot 1088 available */
    NULL, /* slot 1089 available */
    NULL, /* slot 1090 available */
    NULL, /* slot 1091 available */
    NULL, /* slot 1092 available */
    NULL, /* slot 1093 available */
    NULL, /* slot 1094 available */
    NULL, /* slot 1095 available */
    NULL, /* slot 1096 available */
    NULL, /* slot 1097 available */
    NULL, /* slot 1098 available */
    NULL, /* slot 1099 available */
    NULL, /* slot 1100 available */
    NULL, /* slot 1101 available */
    NULL, /* slot 1102 available */
    NULL, /* slot 1103 available */
    NULL, /* slot 1104 available */
    NULL, /* slot 1105 available */
    NULL, /* slot 1106 available */
    NULL, /* slot 1107 available */
    NULL, /* slot 1108 available */
    NULL, /* slot 1109 available */
    NULL, /* slot 1110 available */
    NULL, /* slot 1111 available */
    NULL, /* slot 1112 available */
    NULL, /* slot 1113 available */
    NULL, /* slot 1114 available */
    NULL, /* slot 1115 available */
    NULL, /* slot 1116 available */
    NULL, /* slot 1117 available */
    NULL, /* slot 1118 available */
    NULL, /* slot 1119 available */
    NULL, /* slot 1120 available */
    NULL, /* slot 1121 available */
    NULL, /* slot 1122 available */
    NULL, /* slot 1123 available */
    NULL, /* slot 1124 available */
    NULL, /* slot 1125 available */
    NULL, /* slot 1126 available */
    NULL, /* slot 1127 available */
    NULL, /* slot 1128 available */
    NULL, /* slot 1129 available */
    NULL, /* slot 1130 available */
    NULL, /* slot 1131 available */
    NULL, /* slot 1132 available */
    NULL, /* slot 1133 available */
    NULL, /* slot 1134 available */
    NULL, /* slot 1135 available */
    NULL, /* slot 1136 available */
    NULL, /* slot 1137 available */
    NULL, /* slot 1138 available */
    NULL, /* slot 1139 available */
    NULL, /* slot 1140 available */
    NULL, /* slot 1141 available */
    NULL, /* slot 1142 available */
    NULL, /* slot 1143 available */
    NULL, /* slot 1144 available */
    NULL, /* slot 1145 available */
    NULL, /* slot 1146 available */
    NULL, /* slot 1147 available */
    NULL, /* slot 1148 available */
    NULL, /* slot 1149 available */
    NULL, /* slot 1150 available */
    NULL, /* slot 1151 available */
    NULL, /* slot 1152 available */
    NULL, /* slot 1153 available */
    NULL, /* slot 1154 available */
    NULL, /* slot 1155 available */
    NULL, /* slot 1156 available */
    NULL, /* slot 1157 available */
    NULL, /* slot 1158 available */
    NULL, /* slot 1159 available */
    slapi_pblock_set_search_gerattrs,
    slapi_pblock_set_search_reqattrs,
    NULL, /* slot 1162 available */
    NULL, /* slot 1163 available */
    NULL, /* slot 1164 available */
    NULL, /* slot 1165 available */
    NULL, /* slot 1166 available */
    NULL, /* slot 1167 available */
    NULL, /* slot 1168 available */
    NULL, /* slot 1169 available */
    NULL, /* slot 1170 available */
    NULL, /* slot 1171 available */
    NULL, /* slot 1172 available */
    NULL, /* slot 1173 available */
    NULL, /* slot 1174 available */
    NULL, /* slot 1175 available */
    NULL, /* slot 1176 available */
    NULL, /* slot 1177 available */
    NULL, /* slot 1178 available */
    NULL, /* slot 1179 available */
    NULL, /* slot 1180 available */
    NULL, /* slot 1181 available */
    NULL, /* slot 1182 available */
    NULL, /* slot 1183 available */
    NULL, /* slot 1184 available */
    NULL, /* slot 1185 available */
    NULL, /* slot 1186 available */
    NULL, /* slot 1187 available */
    NULL, /* slot 1188 available */
    NULL, /* slot 1189 available */
    NULL, /* slot 1190 available */
    NULL, /* slot 1191 available */
    NULL, /* slot 1192 available */
    NULL, /* slot 1193 available */
    NULL, /* slot 1194 available */
    NULL, /* slot 1195 available */
    NULL, /* slot 1196 available */
    NULL, /* slot 1197 available */
    NULL, /* slot 1198 available */
    NULL, /* slot 1199 available */
    NULL, /* slot 1200 available */
    NULL, /* slot 1201 available */
    NULL, /* slot 1202 available */
    NULL, /* slot 1203 available */
    NULL, /* slot 1204 available */
    NULL, /* slot 1205 available */
    NULL, /* slot 1206 available */
    NULL, /* slot 1207 available */
    NULL, /* slot 1208 available */
    NULL, /* slot 1209 available */
    NULL, /* slot 1210 available */
    NULL, /* slot 1211 available */
    NULL, /* slot 1212 available */
    NULL, /* slot 1213 available */
    NULL, /* slot 1214 available */
    NULL, /* slot 1215 available */
    NULL, /* slot 1216 available */
    NULL, /* slot 1217 available */
    NULL, /* slot 1218 available */
    NULL, /* slot 1219 available */
    NULL, /* slot 1220 available */
    NULL, /* slot 1221 available */
    NULL, /* slot 1222 available */
    NULL, /* slot 1223 available */
    NULL, /* slot 1224 available */
    NULL, /* slot 1225 available */
    NULL, /* slot 1226 available */
    NULL, /* slot 1227 available */
    NULL, /* slot 1228 available */
    NULL, /* slot 1229 available */
    NULL, /* slot 1230 available */
    NULL, /* slot 1231 available */
    NULL, /* slot 1232 available */
    NULL, /* slot 1233 available */
    NULL, /* slot 1234 available */
    NULL, /* slot 1235 available */
    NULL, /* slot 1236 available */
    NULL, /* slot 1237 available */
    NULL, /* slot 1238 available */
    NULL, /* slot 1239 available */
    NULL, /* slot 1240 available */
    NULL, /* slot 1241 available */
    NULL, /* slot 1242 available */
    NULL, /* slot 1243 available */
    NULL, /* slot 1244 available */
    NULL, /* slot 1245 available */
    NULL, /* slot 1246 available */
    NULL, /* slot 1247 available */
    NULL, /* slot 1248 available */
    NULL, /* slot 1249 available */
    NULL, /* slot 1250 available */
    NULL, /* slot 1251 available */
    NULL, /* slot 1252 available */
    NULL, /* slot 1253 available */
    NULL, /* slot 1254 available */
    NULL, /* slot 1255 available */
    NULL, /* slot 1256 available */
    NULL, /* slot 1257 available */
    NULL, /* slot 1258 available */
    NULL, /* slot 1259 available */
    NULL, /* slot 1260 available */
    NULL, /* slot 1261 available */
    NULL, /* slot 1262 available */
    NULL, /* slot 1263 available */
    NULL, /* slot 1264 available */
    NULL, /* slot 1265 available */
    NULL, /* slot 1266 available */
    NULL, /* slot 1267 available */
    NULL, /* slot 1268 available */
    NULL, /* slot 1269 available */
    NULL, /* slot 1270 available */
    NULL, /* slot 1271 available */
    NULL, /* slot 1272 available */
    NULL, /* slot 1273 available */
    NULL, /* slot 1274 available */
    NULL, /* slot 1275 available */
    NULL, /* slot 1276 available */
    NULL, /* slot 1277 available */
    NULL, /* slot 1278 available */
    NULL, /* slot 1279 available */
    NULL, /* slot 1280 available */
    NULL, /* slot 1281 available */
    NULL, /* slot 1282 available */
    NULL, /* slot 1283 available */
    NULL, /* slot 1284 available */
    NULL, /* slot 1285 available */
    NULL, /* slot 1286 available */
    NULL, /* slot 1287 available */
    NULL, /* slot 1288 available */
    NULL, /* slot 1289 available */
    NULL, /* slot 1290 available */
    NULL, /* slot 1291 available */
    NULL, /* slot 1292 available */
    NULL, /* slot 1293 available */
    NULL, /* slot 1294 available */
    NULL, /* slot 1295 available */
    NULL, /* slot 1296 available */
    NULL, /* slot 1297 available */
    NULL, /* slot 1298 available */
    NULL, /* slot 1299 available */
    NULL, /* slot 1300 available */
    NULL, /* slot 1301 available */
    NULL, /* slot 1302 available */
    NULL, /* slot 1303 available */
    NULL, /* slot 1304 available */
    NULL, /* slot 1305 available */
    NULL, /* slot 1306 available */
    NULL, /* slot 1307 available */
    NULL, /* slot 1308 available */
    NULL, /* slot 1309 available */
    NULL, /* slot 1310 available */
    NULL, /* slot 1311 available */
    NULL, /* slot 1312 available */
    NULL, /* slot 1313 available */
    NULL, /* slot 1314 available */
    NULL, /* slot 1315 available */
    NULL, /* slot 1316 available */
    NULL, /* slot 1317 available */
    NULL, /* slot 1318 available */
    NULL, /* slot 1319 available */
    NULL, /* slot 1320 available */
    NULL, /* slot 1321 available */
    NULL, /* slot 1322 available */
    NULL, /* slot 1323 available */
    NULL, /* slot 1324 available */
    NULL, /* slot 1325 available */
    NULL, /* slot 1326 available */
    NULL, /* slot 1327 available */
    NULL, /* slot 1328 available */
    NULL, /* slot 1329 available */
    NULL, /* slot 1330 available */
    NULL, /* slot 1331 available */
    NULL, /* slot 1332 available */
    NULL, /* slot 1333 available */
    NULL, /* slot 1334 available */
    NULL, /* slot 1335 available */
    NULL, /* slot 1336 available */
    NULL, /* slot 1337 available */
    NULL, /* slot 1338 available */
    NULL, /* slot 1339 available */
    NULL, /* slot 1340 available */
    NULL, /* slot 1341 available */
    NULL, /* slot 1342 available */
    NULL, /* slot 1343 available */
    NULL, /* slot 1344 available */
    NULL, /* slot 1345 available */
    NULL, /* slot 1346 available */
    NULL, /* slot 1347 available */
    NULL, /* slot 1348 available */
    NULL, /* slot 1349 available */
    NULL, /* slot 1350 available */
    NULL, /* slot 1351 available */
    NULL, /* slot 1352 available */
    NULL, /* slot 1353 available */
    NULL, /* slot 1354 available */
    NULL, /* slot 1355 available */
    NULL, /* slot 1356 available */
    NULL, /* slot 1357 available */
    NULL, /* slot 1358 available */
    NULL, /* slot 1359 available */
    NULL, /* slot 1360 available */
    NULL, /* slot 1361 available */
    NULL, /* slot 1362 available */
    NULL, /* slot 1363 available */
    NULL, /* slot 1364 available */
    NULL, /* slot 1365 available */
    NULL, /* slot 1366 available */
    NULL, /* slot 1367 available */
    NULL, /* slot 1368 available */
    NULL, /* slot 1369 available */
    NULL, /* slot 1370 available */
    NULL, /* slot 1371 available */
    NULL, /* slot 1372 available */
    NULL, /* slot 1373 available */
    NULL, /* slot 1374 available */
    NULL, /* slot 1375 available */
    NULL, /* slot 1376 available */
    NULL, /* slot 1377 available */
    NULL, /* slot 1378 available */
    NULL, /* slot 1379 available */
    NULL, /* slot 1380 available */
    NULL, /* slot 1381 available */
    NULL, /* slot 1382 available */
    NULL, /* slot 1383 available */
    NULL, /* slot 1384 available */
    NULL, /* slot 1385 available */
    NULL, /* slot 1386 available */
    NULL, /* slot 1387 available */
    NULL, /* slot 1388 available */
    NULL, /* slot 1389 available */
    NULL, /* slot 1390 available */
    NULL, /* slot 1391 available */
    NULL, /* slot 1392 available */
    NULL, /* slot 1393 available */
    NULL, /* slot 1394 available */
    NULL, /* slot 1395 available */
    NULL, /* slot 1396 available */
    NULL, /* slot 1397 available */
    NULL, /* slot 1398 available */
    NULL, /* slot 1399 available */
    NULL, /* slot 1400 available */
    NULL, /* slot 1401 available */
    NULL, /* slot 1402 available */
    NULL, /* slot 1403 available */
    NULL, /* slot 1404 available */
    NULL, /* slot 1405 available */
    NULL, /* slot 1406 available */
    NULL, /* slot 1407 available */
    NULL, /* slot 1408 available */
    NULL, /* slot 1409 available */
    NULL, /* slot 1410 available */
    NULL, /* slot 1411 available */
    NULL, /* slot 1412 available */
    NULL, /* slot 1413 available */
    NULL, /* slot 1414 available */
    NULL, /* slot 1415 available */
    NULL, /* slot 1416 available */
    NULL, /* slot 1417 available */
    NULL, /* slot 1418 available */
    NULL, /* slot 1419 available */
    NULL, /* slot 1420 available */
    NULL, /* slot 1421 available */
    NULL, /* slot 1422 available */
    NULL, /* slot 1423 available */
    NULL, /* slot 1424 available */
    NULL, /* slot 1425 available */
    NULL, /* slot 1426 available */
    NULL, /* slot 1427 available */
    NULL, /* slot 1428 available */
    NULL, /* slot 1429 available */
    NULL, /* slot 1430 available */
    NULL, /* slot 1431 available */
    NULL, /* slot 1432 available */
    NULL, /* slot 1433 available */
    NULL, /* slot 1434 available */
    NULL, /* slot 1435 available */
    NULL, /* slot 1436 available */
    NULL, /* slot 1437 available */
    NULL, /* slot 1438 available */
    NULL, /* slot 1439 available */
    NULL, /* slot 1440 available */
    NULL, /* slot 1441 available */
    NULL, /* slot 1442 available */
    NULL, /* slot 1443 available */
    NULL, /* slot 1444 available */
    NULL, /* slot 1445 available */
    NULL, /* slot 1446 available */
    NULL, /* slot 1447 available */
    NULL, /* slot 1448 available */
    NULL, /* slot 1449 available */
    NULL, /* slot 1450 available */
    NULL, /* slot 1451 available */
    NULL, /* slot 1452 available */
    NULL, /* slot 1453 available */
    NULL, /* slot 1454 available */
    NULL, /* slot 1455 available */
    NULL, /* slot 1456 available */
    NULL, /* slot 1457 available */
    NULL, /* slot 1458 available */
    NULL, /* slot 1459 available */
    NULL, /* slot 1460 available */
    NULL, /* slot 1461 available */
    NULL, /* slot 1462 available */
    NULL, /* slot 1463 available */
    NULL, /* slot 1464 available */
    NULL, /* slot 1465 available */
    NULL, /* slot 1466 available */
    NULL, /* slot 1467 available */
    NULL, /* slot 1468 available */
    NULL, /* slot 1469 available */
    NULL, /* slot 1470 available */
    NULL, /* slot 1471 available */
    NULL, /* slot 1472 available */
    NULL, /* slot 1473 available */
    NULL, /* slot 1474 available */
    NULL, /* slot 1475 available */
    NULL, /* slot 1476 available */
    NULL, /* slot 1477 available */
    NULL, /* slot 1478 available */
    NULL, /* slot 1479 available */
    NULL, /* slot 1480 available */
    NULL, /* slot 1481 available */
    NULL, /* slot 1482 available */
    NULL, /* slot 1483 available */
    NULL, /* slot 1484 available */
    NULL, /* slot 1485 available */
    NULL, /* slot 1486 available */
    NULL, /* slot 1487 available */
    NULL, /* slot 1488 available */
    NULL, /* slot 1489 available */
    NULL, /* slot 1490 available */
    NULL, /* slot 1491 available */
    NULL, /* slot 1492 available */
    NULL, /* slot 1493 available */
    NULL, /* slot 1494 available */
    NULL, /* slot 1495 available */
    NULL, /* slot 1496 available */
    NULL, /* slot 1497 available */
    NULL, /* slot 1498 available */
    NULL, /* slot 1499 available */
    NULL, /* slot 1500 available */
    NULL, /* slot 1501 available */
    NULL, /* slot 1502 available */
    NULL, /* slot 1503 available */
    NULL, /* slot 1504 available */
    NULL, /* slot 1505 available */
    NULL, /* slot 1506 available */
    NULL, /* slot 1507 available */
    NULL, /* slot 1508 available */
    NULL, /* slot 1509 available */
    NULL, /* slot 1510 available */
    NULL, /* slot 1511 available */
    NULL, /* slot 1512 available */
    NULL, /* slot 1513 available */
    NULL, /* slot 1514 available */
    NULL, /* slot 1515 available */
    NULL, /* slot 1516 available */
    NULL, /* slot 1517 available */
    NULL, /* slot 1518 available */
    NULL, /* slot 1519 available */
    NULL, /* slot 1520 available */
    NULL, /* slot 1521 available */
    NULL, /* slot 1522 available */
    NULL, /* slot 1523 available */
    NULL, /* slot 1524 available */
    NULL, /* slot 1525 available */
    NULL, /* slot 1526 available */
    NULL, /* slot 1527 available */
    NULL, /* slot 1528 available */
    NULL, /* slot 1529 available */
    NULL, /* slot 1530 available */
    NULL, /* slot 1531 available */
    NULL, /* slot 1532 available */
    NULL, /* slot 1533 available */
    NULL, /* slot 1534 available */
    NULL, /* slot 1535 available */
    NULL, /* slot 1536 available */
    NULL, /* slot 1537 available */
    NULL, /* slot 1538 available */
    NULL, /* slot 1539 available */
    NULL, /* slot 1540 available */
    NULL, /* slot 1541 available */
    NULL, /* slot 1542 available */
    NULL, /* slot 1543 available */
    NULL, /* slot 1544 available */
    NULL, /* slot 1545 available */
    NULL, /* slot 1546 available */
    NULL, /* slot 1547 available */
    NULL, /* slot 1548 available */
    NULL, /* slot 1549 available */
    NULL, /* slot 1550 available */
    NULL, /* slot 1551 available */
    NULL, /* slot 1552 available */
    NULL, /* slot 1553 available */
    NULL, /* slot 1554 available */
    NULL, /* slot 1555 available */
    NULL, /* slot 1556 available */
    NULL, /* slot 1557 available */
    NULL, /* slot 1558 available */
    NULL, /* slot 1559 available */
    NULL, /* slot 1560 available */
    NULL, /* slot 1561 available */
    NULL, /* slot 1562 available */
    NULL, /* slot 1563 available */
    NULL, /* slot 1564 available */
    NULL, /* slot 1565 available */
    NULL, /* slot 1566 available */
    NULL, /* slot 1567 available */
    NULL, /* slot 1568 available */
    NULL, /* slot 1569 available */
    NULL, /* slot 1570 available */
    NULL, /* slot 1571 available */
    NULL, /* slot 1572 available */
    NULL, /* slot 1573 available */
    NULL, /* slot 1574 available */
    NULL, /* slot 1575 available */
    NULL, /* slot 1576 available */
    NULL, /* slot 1577 available */
    NULL, /* slot 1578 available */
    NULL, /* slot 1579 available */
    NULL, /* slot 1580 available */
    NULL, /* slot 1581 available */
    NULL, /* slot 1582 available */
    NULL, /* slot 1583 available */
    NULL, /* slot 1584 available */
    NULL, /* slot 1585 available */
    NULL, /* slot 1586 available */
    NULL, /* slot 1587 available */
    NULL, /* slot 1588 available */
    NULL, /* slot 1589 available */
    NULL, /* slot 1590 available */
    NULL, /* slot 1591 available */
    NULL, /* slot 1592 available */
    NULL, /* slot 1593 available */
    NULL, /* slot 1594 available */
    NULL, /* slot 1595 available */
    NULL, /* slot 1596 available */
    NULL, /* slot 1597 available */
    NULL, /* slot 1598 available */
    NULL, /* slot 1599 available */
    NULL, /* slot 1600 available */
    NULL, /* slot 1601 available */
    NULL, /* slot 1602 available */
    NULL, /* slot 1603 available */
    NULL, /* slot 1604 available */
    NULL, /* slot 1605 available */
    NULL, /* slot 1606 available */
    NULL, /* slot 1607 available */
    NULL, /* slot 1608 available */
    NULL, /* slot 1609 available */
    NULL, /* slot 1610 available */
    NULL, /* slot 1611 available */
    NULL, /* slot 1612 available */
    NULL, /* slot 1613 available */
    NULL, /* slot 1614 available */
    NULL, /* slot 1615 available */
    NULL, /* slot 1616 available */
    NULL, /* slot 1617 available */
    NULL, /* slot 1618 available */
    NULL, /* slot 1619 available */
    NULL, /* slot 1620 available */
    NULL, /* slot 1621 available */
    NULL, /* slot 1622 available */
    NULL, /* slot 1623 available */
    NULL, /* slot 1624 available */
    NULL, /* slot 1625 available */
    NULL, /* slot 1626 available */
    NULL, /* slot 1627 available */
    NULL, /* slot 1628 available */
    NULL, /* slot 1629 available */
    NULL, /* slot 1630 available */
    NULL, /* slot 1631 available */
    NULL, /* slot 1632 available */
    NULL, /* slot 1633 available */
    NULL, /* slot 1634 available */
    NULL, /* slot 1635 available */
    NULL, /* slot 1636 available */
    NULL, /* slot 1637 available */
    NULL, /* slot 1638 available */
    NULL, /* slot 1639 available */
    NULL, /* slot 1640 available */
    NULL, /* slot 1641 available */
    NULL, /* slot 1642 available */
    NULL, /* slot 1643 available */
    NULL, /* slot 1644 available */
    NULL, /* slot 1645 available */
    NULL, /* slot 1646 available */
    NULL, /* slot 1647 available */
    NULL, /* slot 1648 available */
    NULL, /* slot 1649 available */
    NULL, /* slot 1650 available */
    NULL, /* slot 1651 available */
    NULL, /* slot 1652 available */
    NULL, /* slot 1653 available */
    NULL, /* slot 1654 available */
    NULL, /* slot 1655 available */
    NULL, /* slot 1656 available */
    NULL, /* slot 1657 available */
    NULL, /* slot 1658 available */
    NULL, /* slot 1659 available */
    NULL, /* slot 1660 available */
    NULL, /* slot 1661 available */
    NULL, /* slot 1662 available */
    NULL, /* slot 1663 available */
    NULL, /* slot 1664 available */
    NULL, /* slot 1665 available */
    NULL, /* slot 1666 available */
    NULL, /* slot 1667 available */
    NULL, /* slot 1668 available */
    NULL, /* slot 1669 available */
    NULL, /* slot 1670 available */
    NULL, /* slot 1671 available */
    NULL, /* slot 1672 available */
    NULL, /* slot 1673 available */
    NULL, /* slot 1674 available */
    NULL, /* slot 1675 available */
    NULL, /* slot 1676 available */
    NULL, /* slot 1677 available */
    NULL, /* slot 1678 available */
    NULL, /* slot 1679 available */
    NULL, /* slot 1680 available */
    NULL, /* slot 1681 available */
    NULL, /* slot 1682 available */
    NULL, /* slot 1683 available */
    NULL, /* slot 1684 available */
    NULL, /* slot 1685 available */
    NULL, /* slot 1686 available */
    NULL, /* slot 1687 available */
    NULL, /* slot 1688 available */
    NULL, /* slot 1689 available */
    NULL, /* slot 1690 available */
    NULL, /* slot 1691 available */
    NULL, /* slot 1692 available */
    NULL, /* slot 1693 available */
    NULL, /* slot 1694 available */
    NULL, /* slot 1695 available */
    NULL, /* slot 1696 available */
    NULL, /* slot 1697 available */
    NULL, /* slot 1698 available */
    NULL, /* slot 1699 available */
    NULL, /* slot 1700 available */
    NULL, /* slot 1701 available */
    NULL, /* slot 1702 available */
    NULL, /* slot 1703 available */
    NULL, /* slot 1704 available */
    NULL, /* slot 1705 available */
    NULL, /* slot 1706 available */
    NULL, /* slot 1707 available */
    NULL, /* slot 1708 available */
    NULL, /* slot 1709 available */
    NULL, /* slot 1710 available */
    NULL, /* slot 1711 available */
    NULL, /* slot 1712 available */
    NULL, /* slot 1713 available */
    NULL, /* slot 1714 available */
    NULL, /* slot 1715 available */
    NULL, /* slot 1716 available */
    NULL, /* slot 1717 available */
    NULL, /* slot 1718 available */
    NULL, /* slot 1719 available */
    NULL, /* slot 1720 available */
    NULL, /* slot 1721 available */
    NULL, /* slot 1722 available */
    NULL, /* slot 1723 available */
    NULL, /* slot 1724 available */
    NULL, /* slot 1725 available */
    NULL, /* slot 1726 available */
    NULL, /* slot 1727 available */
    NULL, /* slot 1728 available */
    NULL, /* slot 1729 available */
    NULL, /* slot 1730 available */
    NULL, /* slot 1731 available */
    NULL, /* slot 1732 available */
    NULL, /* slot 1733 available */
    NULL, /* slot 1734 available */
    NULL, /* slot 1735 available */
    NULL, /* slot 1736 available */
    NULL, /* slot 1737 available */
    NULL, /* slot 1738 available */
    NULL, /* slot 1739 available */
    NULL, /* slot 1740 available */
    NULL, /* slot 1741 available */
    NULL, /* slot 1742 available */
    NULL, /* slot 1743 available */
    NULL, /* slot 1744 available */
    NULL, /* slot 1745 available */
    NULL, /* slot 1746 available */
    NULL, /* slot 1747 available */
    NULL, /* slot 1748 available */
    NULL, /* slot 1749 available */
    NULL, /* slot 1750 available */
    NULL, /* slot 1751 available */
    NULL, /* slot 1752 available */
    NULL, /* slot 1753 available */
    NULL, /* slot 1754 available */
    NULL, /* slot 1755 available */
    NULL, /* slot 1756 available */
    NULL, /* slot 1757 available */
    NULL, /* slot 1758 available */
    NULL, /* slot 1759 available */
    NULL, /* slot 1760 available */
    slapi_pblock_set_ldif_changelog,
    NULL, /* slot 1762 available */
    NULL, /* slot 1763 available */
    NULL, /* slot 1764 available */
    NULL, /* slot 1765 available */
    NULL, /* slot 1766 available */
    NULL, /* slot 1767 available */
    NULL, /* slot 1768 available */
    NULL, /* slot 1769 available */
    NULL, /* slot 1770 available */
    NULL, /* slot 1771 available */
    NULL, /* slot 1772 available */
    NULL, /* slot 1773 available */
    NULL, /* slot 1774 available */
    NULL, /* slot 1775 available */
    NULL, /* slot 1776 available */
    NULL, /* slot 1777 available */
    NULL, /* slot 1778 available */
    NULL, /* slot 1779 available */
    NULL, /* slot 1780 available */
    NULL, /* slot 1781 available */
    NULL, /* slot 1782 available */
    NULL, /* slot 1783 available */
    NULL, /* slot 1784 available */
    NULL, /* slot 1785 available */
    NULL, /* slot 1786 available */
    NULL, /* slot 1787 available */
    NULL, /* slot 1788 available */
    NULL, /* slot 1789 available */
    NULL, /* slot 1790 available */
    NULL, /* slot 1791 available */
    NULL, /* slot 1792 available */
    NULL, /* slot 1793 available */
    NULL, /* slot 1794 available */
    NULL, /* slot 1795 available */
    NULL, /* slot 1796 available */
    NULL, /* slot 1797 available */
    NULL, /* slot 1798 available */
    NULL, /* slot 1799 available */
    NULL, /* slot 1800 available */
    NULL, /* slot 1801 available */
    NULL, /* slot 1802 available */
    NULL, /* slot 1803 available */
    NULL, /* slot 1804 available */
    NULL, /* slot 1805 available */
    NULL, /* slot 1806 available */
    NULL, /* slot 1807 available */
    NULL, /* slot 1808 available */
    NULL, /* slot 1809 available */
    NULL, /* slot 1810 available */
    NULL, /* slot 1811 available */
    NULL, /* slot 1812 available */
    NULL, /* slot 1813 available */
    NULL, /* slot 1814 available */
    NULL, /* slot 1815 available */
    NULL, /* slot 1816 available */
    NULL, /* slot 1817 available */
    NULL, /* slot 1818 available */
    NULL, /* slot 1819 available */
    NULL, /* slot 1820 available */
    NULL, /* slot 1821 available */
    NULL, /* slot 1822 available */
    NULL, /* slot 1823 available */
    NULL, /* slot 1824 available */
    NULL, /* slot 1825 available */
    NULL, /* slot 1826 available */
    NULL, /* slot 1827 available */
    NULL, /* slot 1828 available */
    NULL, /* slot 1829 available */
    NULL, /* slot 1830 available */
    NULL, /* slot 1831 available */
    NULL, /* slot 1832 available */
    NULL, /* slot 1833 available */
    NULL, /* slot 1834 available */
    NULL, /* slot 1835 available */
    NULL, /* slot 1836 available */
    NULL, /* slot 1837 available */
    NULL, /* slot 1838 available */
    NULL, /* slot 1839 available */
    NULL, /* slot 1840 available */
    NULL, /* slot 1841 available */
    NULL, /* slot 1842 available */
    NULL, /* slot 1843 available */
    NULL, /* slot 1844 available */
    NULL, /* slot 1845 available */
    NULL, /* slot 1846 available */
    NULL, /* slot 1847 available */
    NULL, /* slot 1848 available */
    NULL, /* slot 1849 available */
    NULL, /* slot 1850 available */
    NULL, /* slot 1851 available */
    NULL, /* slot 1852 available */
    NULL, /* slot 1853 available */
    NULL, /* slot 1854 available */
    NULL, /* slot 1855 available */
    NULL, /* slot 1856 available */
    NULL, /* slot 1857 available */
    NULL, /* slot 1858 available */
    NULL, /* slot 1859 available */
    NULL, /* slot 1860 available */
    NULL, /* slot 1861 available */
    NULL, /* slot 1862 available */
    NULL, /* slot 1863 available */
    NULL, /* slot 1864 available */
    NULL, /* slot 1865 available */
    NULL, /* slot 1866 available */
    NULL, /* slot 1867 available */
    NULL, /* slot 1868 available */
    NULL, /* slot 1869 available */
    NULL, /* slot 1870 available */
    NULL, /* slot 1871 available */
    NULL, /* slot 1872 available */
    NULL, /* slot 1873 available */
    NULL, /* slot 1874 available */
    NULL, /* slot 1875 available */
    NULL, /* slot 1876 available */
    NULL, /* slot 1877 available */
    NULL, /* slot 1878 available */
    NULL, /* slot 1879 available */
    NULL, /* slot 1880 available */
    NULL, /* slot 1881 available */
    NULL, /* slot 1882 available */
    NULL, /* slot 1883 available */
    NULL, /* slot 1884 available */
    NULL, /* slot 1885 available */
    NULL, /* slot 1886 available */
    NULL, /* slot 1887 available */
    NULL, /* slot 1888 available */
    NULL, /* slot 1889 available */
    NULL, /* slot 1890 available */
    NULL, /* slot 1891 available */
    NULL, /* slot 1892 available */
    NULL, /* slot 1893 available */
    NULL, /* slot 1894 available */
    NULL, /* slot 1895 available */
    NULL, /* slot 1896 available */
    NULL, /* slot 1897 available */
    NULL, /* slot 1898 available */
    NULL, /* slot 1899 available */
    NULL, /* slot 1900 available */
    slapi_pblock_set_txn_ruv_mods_fn,
    NULL, /* slot 1902 available */
    NULL, /* slot 1903 available */
    NULL, /* slot 1904 available */
    NULL, /* slot 1905 available */
    NULL, /* slot 1906 available */
    NULL, /* slot 1907 available */
    NULL, /* slot 1908 available */
    NULL, /* slot 1909 available */
    NULL, /* slot 1910 available */
    NULL, /* slot 1911 available */
    NULL, /* slot 1912 available */
    NULL, /* slot 1913 available */
    NULL, /* slot 1914 available */
    NULL, /* slot 1915 available */
    NULL, /* slot 1916 available */
    NULL, /* slot 1917 available */
    NULL, /* slot 1918 available */
    NULL, /* slot 1919 available */
    NULL, /* slot 1920 available */
    NULL, /* slot 1921 available */
    NULL, /* slot 1922 available */
    NULL, /* slot 1923 available */
    NULL, /* slot 1924 available */
    NULL, /* slot 1925 available */
    NULL, /* slot 1926 available */
    NULL, /* slot 1927 available */
    NULL, /* slot 1928 available */
    NULL, /* slot 1929 available */
    slapi_pblock_set_search_result_set_size_estimate,
    NULL, /* slot 1931 available */
    NULL, /* slot 1932 available */
    NULL, /* slot 1933 available */
    NULL, /* slot 1934 available */
    NULL, /* slot 1935 available */
    NULL, /* slot 1936 available */
    NULL, /* slot 1937 available */
    NULL, /* slot 1938 available */
    NULL, /* slot 1939 available */
    NULL, /* slot 1940 available */
    NULL, /* slot 1941 available */
    NULL, /* slot 1942 available */
    NULL, /* slot 1943 available */
    slapi_pblock_set_search_result_entry_ext,
    slapi_pblock_set_paged_results_index,
    slapi_pblock_set_aci_target_check,
    slapi_pblock_set_dbverify_dbdir,
    slapi_pblock_set_plugin_ext_op_backend_fn,
    slapi_pblock_set_paged_results_cookie,
    slapi_pblock_set_usn_increment_for_tombstone,
    slapi_pblock_set_memberof_deferred_task,
    NULL, /* slot 1952 available */
};

int32_t
slapi_pblock_set(Slapi_PBlock *pblock, int arg, void *value)
{
    PR_ASSERT(NULL != pblock);

    if (arg > 0 && arg < PR_ARRAY_SIZE(set_cbtable) && set_cbtable[arg] != NULL) {
        return set_cbtable[arg](pblock, value);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pblock_set",
                      "Unknown parameter block argument %d\n", arg);
        PR_ASSERT(0);
        return (-1);
    }
}
