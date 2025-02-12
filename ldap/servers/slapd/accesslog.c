/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "log.h"
#include "slap.h"

#define MAX_ELEMENT_SIZE 512
#define JBUFSIZE 75
#define KEY_SIZE 50

struct json_object*
json_obj_add_str(const char *value)
{
    char val_buffer[MAX_ELEMENT_SIZE] = {0};

    if (strlen(value) > MAX_ELEMENT_SIZE) {
        /* we use -4 to leave room for the NULL terminator */
        memcpy(&val_buffer, value, MAX_ELEMENT_SIZE-4);
        memcpy(&val_buffer[MAX_ELEMENT_SIZE-4], "...", 3);
    } else {
        PR_snprintf(val_buffer, sizeof(val_buffer), "%s", value);
    }

    return json_object_new_string(val_buffer);

}

static json_object *
build_base_obj(slapd_log_pblock *logpb, char *op_type)
{
    json_object *json_obj = NULL;
    Connection *conn = NULL;
    char local_time[TBUFSIZE] = {0};
    char conn_key[KEY_SIZE] = {0};
    char *time_format = NULL;
    int32_t ltlen = TBUFSIZE;
    bool haproxied = false;

    if (logpb->loginfo && (!(logpb->level & logpb->loginfo->log_access_level))) {
        return NULL;
    }

    /* custom local time */
    time_format = config_get_accesslog_time_format();
    if (format_localTime_hr_json_log(&logpb->curr_time, local_time, &ltlen,
                                     time_format) != 0)
    {
        /* MSG may be truncated */
        PR_snprintf(local_time, sizeof(local_time),
                    "build_base_obj, Unable to format system time");
        log__error_emergency(local_time, 1, 0);
        slapi_ch_free_string(&time_format);
        return NULL;
    }
    slapi_ch_free_string(&time_format);

    if (logpb->pb) {
        slapi_pblock_get(logpb->pb, SLAPI_CONNECTION, &conn);
        if (conn && conn->c_hapoxied) {
            haproxied = true;
        }
    }

    /* Build connection key */
    PR_snprintf(conn_key, sizeof(conn_key), "%ld-%" PRIu64,
                logpb->conn_time, logpb->conn_id);

    json_obj = json_object_new_object();
    json_object_object_add(json_obj, "local_time", json_obj_add_str(local_time));
    json_object_object_add(json_obj, "operation",  json_obj_add_str(op_type));
    json_object_object_add(json_obj, "key",        json_obj_add_str(conn_key));
    json_object_object_add(json_obj, "conn_id",    json_object_new_int64(logpb->conn_id));
    if (logpb->op_id != -1) {
        json_object_object_add(json_obj, "op_id", json_object_new_int(logpb->op_id));
    }
    if (logpb->op_internal_id != -1) {
        json_object_object_add(json_obj, "internal_op", json_object_new_boolean(true));
        json_object_object_add(json_obj, "op_internal_id", json_object_new_int(logpb->op_internal_id));
    }
    if (logpb->op_nested_count != -1) {
        json_object_object_add(json_obj, "op_internal_nested_count", json_object_new_int(logpb->op_nested_count));
    }
    if (logpb->oid) {
        const char *oid_name = NULL;
        json_object_object_add(json_obj, "oid", json_obj_add_str(logpb->oid));
        oid_name = get_oid_name(logpb->oid);
        if (oid_name) {
            json_object_object_add(json_obj, "oid_name", json_obj_add_str(oid_name));
        }
    }
    if (logpb->msg) {
        json_object_object_add(json_obj, "msg", json_obj_add_str(logpb->msg));
    }
    if (logpb->authzid) {
        json_object_object_add(json_obj, "authzid", json_obj_add_str(logpb->authzid));
    }
    if (haproxied) {
        json_object_object_add(json_obj, "haproxied", json_object_new_boolean(haproxied));
    }

    if (logpb->request_controls) {
        json_object *jarray = json_object_new_array();
        LDAPControl **ctrl = logpb->request_controls;

        for (size_t i = 0; ctrl[i] != NULL; i++) {
            char *oid = NULL;
            char *value = NULL;
            bool isCritical = false;
            json_object *ctrl_obj = NULL;

            if (i == 10) {
                /* We only log a max of 10 controls because anything over 10
                 * is very suspicious */
                json_object_array_add(jarray, json_object_new_string("..."));
                break;
            }

            ctrl_obj = json_object_new_object();
            slapi_parse_control(ctrl[i], &oid, &value, &isCritical);
            json_object_object_add(ctrl_obj, "oid", json_obj_add_str(oid));
            json_object_object_add(ctrl_obj, "oid_name", json_obj_add_str(get_oid_name(oid)));
            if (value) {
                json_object_object_add(ctrl_obj, "value", json_obj_add_str(value));
            } else {
                json_object_object_add(ctrl_obj, "value", json_obj_add_str(""));
            }
            json_object_object_add(ctrl_obj, "isCritical", json_object_new_boolean(isCritical));
            json_object_array_add(jarray, ctrl_obj);
            slapi_ch_free_string(&value);
        }
        json_object_object_add(json_obj, "request_controls", jarray);
    }
    if (logpb->response_controls) {
        json_object *jarray = json_object_new_array();
        LDAPControl **ctrl = logpb->response_controls;

        for (size_t i = 0; ctrl[i] != NULL; i++) {
            char *oid = NULL;
            char *value = NULL;
            bool isCritical = false;
            json_object *ctrl_obj = NULL;

            if (i == 10) {
                /* We only log a max of 10 controls because anything over 10
                 * is very suspicious */
                json_object_array_add(jarray, json_object_new_string("..."));
                break;
            }

            ctrl_obj = json_object_new_object();
            slapi_parse_control(ctrl[i], &oid, &value, &isCritical);
            json_object_object_add(ctrl_obj, "oid", json_obj_add_str(oid));
            json_object_object_add(ctrl_obj, "oid_name", json_obj_add_str(get_oid_name(oid)));
            if (value) {
                json_object_object_add(ctrl_obj, "value", json_obj_add_str(value));
            } else {
                json_object_object_add(ctrl_obj, "value", json_obj_add_str(""));
            }
            json_object_object_add(ctrl_obj, "isCritical", json_object_new_boolean(isCritical));
            json_object_array_add(jarray, ctrl_obj);
            slapi_ch_free_string(&value);
        }
        json_object_object_add(json_obj, "response_controls", jarray);
    }

    return json_obj;
}

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "ABANDON")) == NULL) {
        return rc;
    }

    /* Construct the abandon etime */
    if (logpb->tv_sec != -1) {
        PR_snprintf(etime, sizeof(etime), "%" PRId64 ".%010" PRId64,
                    logpb->tv_sec, logpb->tv_nsec);
        json_object_object_add(json_obj, "etime", json_obj_add_str(etime));
    }

    if (logpb->nentries != -1) {
        json_object_object_add(json_obj, "nentries", json_object_new_int(logpb->nentries));
    }
    if (logpb->sid) {
        json_object_object_add(json_obj, "sid", json_obj_add_str(logpb->sid));
    }
    json_object_object_add(json_obj, "msgid", json_object_new_int(logpb->msgid));
    json_object_object_add(json_obj, "target_op", json_obj_add_str(logpb->target_op));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "ADD")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "AUTOBIND")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "bind_dn", json_obj_add_str(logpb->bind_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "BIND")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "bind_dn", json_obj_add_str(logpb->bind_dn));
    json_object_object_add(json_obj, "version", json_object_new_int(logpb->version));
    json_object_object_add(json_obj, "method", json_obj_add_str(logpb->method));
    if (logpb->mech) {
        json_object_object_add(json_obj, "mech", json_obj_add_str(logpb->mech));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "UNBIND")) == NULL) {
        return rc;
    }

    if (logpb->err != 0) {
       json_object_object_add(json_obj, "err", json_object_new_int(logpb->err));
    }
    if (logpb->close_error) {
        json_object_object_add(json_obj, "close_error",
                               json_obj_add_str(logpb->close_error));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
 * const char *close_error
 * const char *close_reason
 */
int32_t
slapd_log_access_close(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "DISCONNECT")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "fd", json_object_new_int(logpb->fd));
    if (logpb->close_error) {
        json_object_object_add(json_obj, "close_error",
                               json_obj_add_str(logpb->close_error));
    }
    if (logpb->close_reason) {
        json_object_object_add(json_obj, "close_reason",
                               json_obj_add_str(logpb->close_reason));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
 * const char *target_dn
 * char *cmp_attr
 * LDAPControl **controls
 */
int32_t
slapd_log_access_cmp(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "COMPARE")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));
    json_object_object_add(json_obj, "cmp_attr", json_obj_add_str(logpb->cmp_attr));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "CONNECTION")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "fd", json_object_new_int(logpb->fd));
    json_object_object_add(json_obj, "slot", json_object_new_int(logpb->slot));
    json_object_object_add(json_obj, "tls", json_object_new_boolean(logpb->using_tls));
    json_object_object_add(json_obj, "client_ip",
                           json_obj_add_str(logpb->client_ip));
    json_object_object_add(json_obj, "server_ip",
                           json_obj_add_str(logpb->server_ip));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "DELETE")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "MODIFY")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "MODRDN")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));
    json_object_object_add(json_obj, "newrdn", json_obj_add_str(logpb->newrdn));
    if (logpb->newsup) {
        json_object_object_add(json_obj, "newsup", json_obj_add_str(logpb->newsup));
    }
    json_object_object_add(json_obj, "deleteoldrdn", json_object_new_boolean(logpb->deleteoldrdn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

    return rc;
}


/*
 * RESULT
 *
 * int32_t log_format
 * Slapi_PBlock *pb
 * time_t conn_time
 * uint64_t conn_id
 * uint32_t tag
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
 * CSN *csn
 * char *bind_dn
 * int32_t pr_idx    - psearch
 * int32_t pr_cookie - psearch
 * char *msg
 * LDAPControl **result_controls
 */
int32_t
slapd_log_access_result(slapd_log_pblock *logpb)
{
    Connection *conn = NULL;
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "RESULT")) == NULL) {
        return rc;
    }

    slapi_pblock_get(logpb->pb, SLAPI_CONNECTION, &conn); /* For IP addr */

    json_object_object_add(json_obj, "tag", json_object_new_int(logpb->tag));
    json_object_object_add(json_obj, "err", json_object_new_int(logpb->err));
    json_object_object_add(json_obj, "nentries", json_object_new_int(logpb->nentries));
    json_object_object_add(json_obj, "wtime", json_obj_add_str(logpb->wtime));
    json_object_object_add(json_obj, "optime", json_obj_add_str(logpb->optime));
    json_object_object_add(json_obj, "etime", json_obj_add_str(logpb->etime));
    if (conn) {
        json_object_object_add(json_obj, "client_ip", json_obj_add_str(conn->c_ipaddr));
    } else {
        json_object_object_add(json_obj, "client_ip", json_obj_add_str("Internal"));
    }
    if (logpb->csn) {
        char csn_str[CSN_STRSIZE] = {0};
        csn_as_string(logpb->csn, PR_FALSE, csn_str);
        json_object_object_add(json_obj, "csn",
                               json_obj_add_str(csn_str));
    }
    if (logpb->pr_idx >= 0) {
        json_object_object_add(json_obj, "pr_idx",
                               json_object_new_int(logpb->pr_idx));
        json_object_object_add(json_obj, "pr_cookie",
                               json_object_new_int(logpb->pr_cookie));
    }
    if (logpb->notes != 0) {
        char *notes[10] = {NULL};
        char *details[10] = {NULL};
        json_object *jarray = json_object_new_array();

        get_notes_info(logpb->notes, notes, details);
        for (size_t i = 0; notes[i]; i++) {
            char *filter_str = NULL;
            json_object *note = json_object_new_object();
            json_object_object_add(note, "note", json_obj_add_str(notes[i]));
            json_object_object_add(note, "description", json_obj_add_str(details[i]));
            if ((strcmp("A", notes[i]) == 0 || strcmp("U", notes[i]) == 0) && logpb->pb) {
                /* Full/partial unindexed search - log more info */
                char *base_dn;
                int32_t scope = 0;

                slapi_pblock_get(logpb->pb, SLAPI_TARGET_DN, &base_dn);
                slapi_pblock_get(logpb->pb, SLAPI_SEARCH_STRFILTER, &filter_str);
                slapi_pblock_get(logpb->pb, SLAPI_SEARCH_SCOPE, &scope);

                json_object_object_add(note, "base_dn", json_obj_add_str(base_dn));
                json_object_object_add(note, "filter", json_obj_add_str(filter_str));
                json_object_object_add(note, "scope", json_object_new_int(scope));
            } else if (strcmp("F", notes[i]) == 0 && logpb->pb) {
                slapi_pblock_get(logpb->pb, SLAPI_SEARCH_STRFILTER, &filter_str);
                json_object_object_add(note, "filter", json_obj_add_str(filter_str));
            }
            json_object_array_add(jarray, note);
        }
        json_object_object_add(json_obj, "notes", jarray);
    }
    if (logpb->sid) {
        json_object_object_add(json_obj, "sid",
                               json_obj_add_str(logpb->sid));
    }
    if (logpb->bind_dn) {
        json_object_object_add(json_obj, "bind_dn",
                               json_obj_add_str(logpb->bind_dn));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "SEARCH")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "base_dn", json_obj_add_str(logpb->base_dn));
    json_object_object_add(json_obj, "scope", json_object_new_int(logpb->scope));
    json_object_object_add(json_obj, "filter", json_obj_add_str(logpb->filter));
    if (logpb->psearch) {
        json_object_object_add(json_obj, "psearch", json_object_new_boolean(logpb->psearch));
    }
    if (logpb->attrs) {
        size_t attrs_len = 0;
        json_object *jarray = json_object_new_array();
        for (size_t i = 0; logpb->attrs[i]; i++) {
            attrs_len += strlen(logpb->attrs[i]);
            if (attrs_len + 3 > 128) {
                /* About to exceed to size limit, truncate the results */
                json_object_array_add(jarray, json_object_new_string("..."));
                break;
            }
            json_object_array_add(jarray, json_obj_add_str(logpb->attrs[i]));
        }
        json_object_object_add(json_obj, "attrs", jarray);
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "STAT")) == NULL) {
        return rc;
    }

    if (logpb->stat_etime) {
        /* this is the summary stat */
        json_object_object_add(json_obj, "stat_etime", json_obj_add_str(logpb->stat_etime));
    } else {
        if (logpb->stat_attr) {
            json_object_object_add(json_obj, "stat_attr", json_obj_add_str(logpb->stat_attr));
        }
        if (logpb->stat_key) {
            json_object_object_add(json_obj, "stat_key", json_obj_add_str(logpb->stat_key));
        }
        if (logpb->stat_value) {
            json_object_object_add(json_obj, "stat_key_value", json_obj_add_str(logpb->stat_value));
        }
        json_object_object_add(json_obj, "stat_count", json_object_new_int(logpb->stat_count));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "ERROR")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "operation", json_obj_add_str(logpb->op_type));
    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

    return rc;
}

/*
 * Generic access log error event
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t *local_ssf
 * int32_t *ssl_ssf
 * const char *sasl_ssf
 * const char *msg
 */
int32_t
slapd_log_access_ssf_error(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "ERROR")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "local_ssf", json_object_new_int(logpb->local_ssf));
    json_object_object_add(json_obj, "sasl_ssf", json_object_new_int(logpb->sasl_ssf));
    json_object_object_add(json_obj, "ssl_ssf", json_object_new_int(logpb->ssl_ssf));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "HAPROXY")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "fd", json_object_new_int(logpb->fd));
    json_object_object_add(json_obj, "haproxy_ip", json_obj_add_str(logpb->haproxy_ip));
    json_object_object_add(json_obj, "haproxy_destip", json_obj_add_str(logpb->haproxy_destip));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;
    json_object *req_obj = NULL;
    json_object *resp_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "VLV")) == NULL) {
        return rc;
    }

    req_obj = json_object_new_object();
    json_object_object_add(req_obj, "request_before_count", json_object_new_int(logpb->vlv_req_before_count));
    json_object_object_add(req_obj, "request_after_count", json_object_new_int(logpb->vlv_req_after_count));
    json_object_object_add(req_obj, "request_index", json_object_new_int(logpb->vlv_req_index));
    json_object_object_add(req_obj, "request_content_count", json_object_new_int(logpb->vlv_req_content_count));
    if (logpb->vlv_req_value) {
        json_object_object_add(req_obj, "request_value", json_obj_add_str(logpb->vlv_req_value));
    }
    json_object_object_add(req_obj, "request_value_len", json_object_new_int64(logpb->vlv_req_value_len));
    if (logpb->vlv_sort_str) {
        json_object_object_add(req_obj, "request_sort", json_obj_add_str(logpb->vlv_sort_str));
    }
    json_object_object_add(json_obj, "vlv_request", req_obj);

    resp_obj = json_object_new_object();
    json_object_object_add(resp_obj, "response_target_position", json_object_new_int(logpb->vlv_res_target_position));
    json_object_object_add(resp_obj, "response_content_count", json_object_new_int(logpb->vlv_res_content_count));
    json_object_object_add(resp_obj, "response_result", json_object_new_int(logpb->vlv_res_result));
    json_object_object_add(json_obj, "vlv_response", resp_obj);

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "ENTRY")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "target_dn", json_obj_add_str(logpb->target_dn));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "REFERRAL")) == NULL) {
        return rc;
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

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
 * const char* name
 */
int32_t
slapd_log_access_extop(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "EXTENDED_OP")) == NULL) {
        return rc;
    }

    if (logpb->name) {
        json_object_object_add(json_obj, "name", json_obj_add_str(logpb->name));
    }

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

    return rc;
}

/*
 * Sort
 *
 * int32_t log_format
 * time_t conn_time
 * uint64_t conn_id
 * int32_t op_id
 * int32_t op_internal_id
 * int32_t op_nested_count
 * char attr_str
 */
int32_t
slapd_log_access_sort(slapd_log_pblock *logpb)
{
    int32_t rc = 0;
    char *msg = NULL;
    json_object *json_obj = NULL;

    if ((json_obj = build_base_obj(logpb, "SORT")) == NULL) {
        return rc;
    }

    json_object_object_add(json_obj, "sort_attrs",
                           json_obj_add_str(logpb->sort_str));

    /* Convert json object to string and log it */
    msg = (char *)json_object_to_json_string_ext(json_obj, logpb->log_format);
    rc = slapd_log_access_json(msg);

    /* Done with JSON object - free it */
    json_object_put(json_obj);

    return rc;
}
