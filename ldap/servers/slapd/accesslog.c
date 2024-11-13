/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"

#define JBUFSIZE 75
#define KEY_SIZE 50

/*
 * ABANDON
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * char* target_op
 * char *sid
 * int32_t msgid
 * int32_t nentries
 * int64_t tv_sec  - for etime
 * int64_t tv_nsec - for etime
 */

int32_t
slapd_log_access_abandon(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char etime[BUFSIZ] = {0};
    char conn_key[KEY_SIZE] = {0};

    /* Build the connection key */
    PR_snprintf(conn_key, sizeof(conn_key), "%d-%" PRIu64,
                (int32_t)logpb->conn_time, logpb->conn_id);

    /* Construct the abandon etime */
    if (logpb->tv_sec != -1) {
        PR_snprintf(etime, sizeof(etime), "%" PRId64 ".%010" PRId64,
                    logpb->tv_sec, logpb->tv_nsec);
    }
    if (logpb->nentries != -1) {

    }

    return rc;
}

/*
 * ADD
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *dn
 * char *authzid
 * char *msg
 * LDAPControl **controls
 */
int32_t
slapd_log_access_add(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * AUTOBIND
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * char *bind_dn
 */
int32_t
slapd_log_access_autobind(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * BIND
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * const char *dn
 * int32_t version
 * const char *method
 * const char *mech
 * const char *msg
 * LDAPControl **controls
 */
int32_t
slapd_log_access_bind(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * UNBIND
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t err
 * char *close_err
 * char *msg
 */
int32_t
slapd_log_access_unbind(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    /*
     "conn=%" PRIu64 " op=%d UNBIND, error processing controls - error %d (%s)\n",
     "conn=%" PRIu64 " op=%d UNBIND, decoding error: UnBindRequest not null\n"
    */

    return rc;
}

/*
 * Disconnect
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t fdnum
 * const char *error
 * const char *reason
 */
int32_t
slapd_log_access_close(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * CMP
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *dn
 * char *attr
 * LDAPControl **controls
 */
int32_t
slapd_log_access_cmp(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * connection from
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t fd
 * int32_t slot
 * PRBool isTLS
 * char *client_ip
 * char *server_ip
 */
int32_t
slapd_log_access_conn(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * DEL
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *dn
 * char *authzid
 * LDAPControl **controls
 */
int32_t
slapd_log_access_delete(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * MOD
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *dn
 * char *authzid
 * char *msg
 * LDAPControl **controls
 */
int32_t
slapd_log_access_mod(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * MODRDN
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *dn
 * const char *newrdn
 * const char *newsup
 * PRBool deleteoldrdn
 * char *authzid
 * LDAPControl **controls
 */
int32_t
slapd_log_access_modrdn(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}


/*
 * RESULT
 *
 * int32_t log_format
 * Slapi_PBlock *pb
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * int32_t err
 * int32_t nentries
 * char *wtime
 * char *optime
 * char *etime
 * char *sid
 * char *notes
 * char *csn
 * char *dn
 * int32_t pr_idx    - psearch
 * int32_t pr_cookie - psearch
 * char *msg
 * LDAPControl **result_controls
 */
int32_t
slapd_log_access_result(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *bind_dn;

    /* get the bind dn */
    slapi_pblock_get(logpb->pb, SLAPI_CONN_DN, &bind_dn);

    /* build JSON object */

    /* free and return */
    slapi_ch_free_string(&bind_dn);

    return rc;
}

/*
 * SRCH
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *base_dn
 * int32_t scope
 * char *filter
 * char **attrs
 * char *authzid
 * PRBool psearch
 * const char *msg
 * LDAPControl **controls
 */
int32_t
slapd_log_access_search(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * STAT
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *attr
 * const char *key
 * const char *value
 * int32_t count
 */
int32_t
slapd_log_access_stat(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    /* STAT attribute=%s key(%s)=%s --> count %d */

    return rc;
}

/*
 * Generic access log error event
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * const char *op - SRCH, ADD, MOD, etc
 * const char *dn
 * const char *authzid
 * const char *msg
 * LDAPControl **req_controls
 * LDAPControl **result_controls
 */
int32_t
slapd_log_access_error(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    // slapi_log_access(LDAP_DEBUG_STATS, "conn=%" PRIu64 " op=%d %s dn=\"%s\"%s, %s\n",

    return rc;
}

/*
 * Generic access log error event
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * const char *local_ssf
 * const char *ssl_ssf
 * const char *sasl_ssf
 * const char *msg
 */
int32_t
slapd_log_access_ssf_error(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * Generic access log error event
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t fd
 * const char *haproxy_ip
 * const char *haproxy_destip
 */
int32_t
slapd_log_access_haproxy(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    /* "conn=%" PRIu64 " fd=%d HAProxy new_address_from=%s to new_address_dest=%s\n" */

    return rc;
}

/*
 * VLV
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * -- Request
 * int32_t vlv_req_before_count;
 * int32_t vlv_req_after_count;
 * int32_t vlv_req_index;
 * int32_t vlv_req_content_count;
 * char *vlv_req_value
 * int64_t vlv_req_value_len;
 * const char *vlv_sort_str
 * -- Response
 * int32_t vlv_res_target_position;
 * int32_t vlv_res_content_count;
 * int32_t vlv_res_result;
 */
int32_t
slapd_log_access_vlv(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    /*
     *   - VLV %d:%d:%d:%d (response status)
     *   - VLV %d:%d:%s (response status)
     */
    return rc;
}

/*
 * ENTRY - LDAP_DEBUG_STATS2
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * const char *target_dn
 */
int32_t
slapd_log_access_entry(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    return rc;
}

/*
 * REFERRAL - LDAP_DEBUG_STATS2
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 */
int32_t
slapd_log_access_referral(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    /*
        "conn=%" PRIu64 " op=%d REFERRAL");

    */
    return rc;
}

/*
 * Extended operation
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * const char* oid
 * const char* msg <optional>
 */
int32_t
slapd_log_access_extop(slapd_log_pblock *logpb)
{
    int32_t rc = 0;

    /*
        "conn=%" PRIu64 " op=%d oid=%s name=%s");

    */
    return rc;
}
