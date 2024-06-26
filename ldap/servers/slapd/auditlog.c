/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005-2024 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"

/*
 * JCM - The audit log might be better implemented as a post-op plugin.
 */

#define ATTR_CHANGETYPE "changetype"
#define ATTR_NEWRDN "newrdn"
#define ATTR_DELETEOLDRDN "deleteoldrdn"
#define ATTR_NEWSUPERIOR "newsuperior"
#define ATTR_MODIFIERSNAME "modifiersname"
#define JBUFSIZE 75
char *attr_changetype = ATTR_CHANGETYPE;
char *attr_newrdn = ATTR_NEWRDN;
char *attr_deleteoldrdn = ATTR_DELETEOLDRDN;
char *attr_newsuperior = ATTR_NEWSUPERIOR;
char *attr_modifiersname = ATTR_MODIFIERSNAME;

static int audit_hide_unhashed_pw = 1;
static int auditfail_hide_unhashed_pw = 1;

/* Forward Declarations */
static void write_audit_file(Slapi_PBlock *pb, Slapi_Entry *entry, int logtype,
                             int optype, const char *dn, void *change,
                             int flag, time_t curtime, int rc, int sourcelog);

static const char *modrdn_changes[4];

void
write_audit_log_entry(Slapi_PBlock *pb)
{
    time_t curtime;
    Slapi_DN *sdn;
    const char *dn;
    void *change;
    int flag = 0;
    Operation *op;
    Slapi_Entry *entry = NULL;

    /* if the audit log is not enabled, just skip all of
       this stuff */
    if (!config_get_auditlog_logging_enabled()) {
        return;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    switch (operation_get_type(op)) {
    case SLAPI_OPERATION_MODIFY:
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &change);
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
        break;
    case SLAPI_OPERATION_ADD:
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &change);
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
        break;
    case SLAPI_OPERATION_DELETE: {
        char *deleterDN = NULL;
        slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &deleterDN);
        change = deleterDN;
        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &entry);
    } break;

    case SLAPI_OPERATION_MODDN: {
        char *rdn = NULL;
        Slapi_DN *snewsuperior = NULL;
        char *requestor = NULL;
        /* newrdn: change is just for logging -- case does not matter. */
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &rdn);
        slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &flag);
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &snewsuperior);
        slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &requestor);
        modrdn_changes[0] = rdn;
        modrdn_changes[1] = requestor;
        if (snewsuperior && slapi_sdn_get_dn(snewsuperior)) {
            modrdn_changes[2] = slapi_sdn_get_dn(snewsuperior);
            modrdn_changes[3] = NULL;
        } else {
            modrdn_changes[2] = NULL;
        }
        change = (void *)modrdn_changes;
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
        break;
    }
    default:
        return; /* Unsupported operation type. */
    }
    curtime = slapi_current_utc_time();
    /* log the raw, unnormalized DN */
    dn = slapi_sdn_get_udn(sdn);
    write_audit_file(pb, entry, SLAPD_AUDIT_LOG, operation_get_type(op), dn,
                     change, flag, curtime, LDAP_SUCCESS, SLAPD_AUDIT_LOG);
}

void
write_auditfail_log_entry(Slapi_PBlock *pb)
{
    time_t curtime;
    Slapi_DN *sdn;
    const char *dn;
    void *change;
    int flag = 0;
    Operation *op;
    int pbrc = 0;
    char *auditfail_config = NULL;
    char *audit_config = NULL;

    /* if the audit log is not enabled, just skip all of
       this stuff */
    if (!config_get_auditfaillog_logging_enabled()) {
        return;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &pbrc);

    switch (operation_get_type(op)) {
    case SLAPI_OPERATION_MODIFY:
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &change);
        break;
    case SLAPI_OPERATION_ADD:
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &change);
        break;
    case SLAPI_OPERATION_DELETE: {
        char *deleterDN = NULL;
        slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &deleterDN);
        change = deleterDN;
    } break;
    case SLAPI_OPERATION_MODDN: {
        char *rdn = NULL;
        Slapi_DN *snewsuperior = NULL;
        char *requestor = NULL;
        /* newrdn: change is just for logging -- case does not matter. */
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &rdn);
        slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &flag);
        slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &snewsuperior);
        slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &requestor);
        modrdn_changes[0] = rdn;
        modrdn_changes[1] = requestor;
        if (snewsuperior && slapi_sdn_get_dn(snewsuperior)) {
            modrdn_changes[2] = slapi_sdn_get_dn(snewsuperior);
            modrdn_changes[3] = NULL;
        } else {
            modrdn_changes[2] = NULL;
        }
        change = (void *)modrdn_changes;
        break;
    }
    default:
        return; /* Unsupported operation type. */
    }

    curtime = slapi_current_utc_time();
    /* log the raw, unnormalized DN */
    dn = slapi_sdn_get_udn(sdn);
    auditfail_config = config_get_auditfaillog();
    audit_config = config_get_auditlog();
    if (auditfail_config == NULL || strlen(auditfail_config) == 0 || PL_strcasecmp(auditfail_config, audit_config) == 0) {
        /* If no auditfail log or "auditfaillog" == "auditlog", write to audit log */
        write_audit_file(pb, NULL, SLAPD_AUDIT_LOG, operation_get_type(op), dn,
                         change, flag, curtime, pbrc, SLAPD_AUDITFAIL_LOG);
    } else {
        /* If we have our own auditfail log path */
        write_audit_file(pb, NULL, SLAPD_AUDITFAIL_LOG, operation_get_type(op),
                         dn, change, flag, curtime, pbrc, SLAPD_AUDITFAIL_LOG);
    }
    slapi_ch_free_string(&auditfail_config);
    slapi_ch_free_string(&audit_config);
}

/*
 * Write the attribute values to the audit log as "comments"
 *
 *   Slapi_Attr *entry - the attribute begin logged.
 *   char *attrname - the attribute name.
 *   lenstr *l - the audit log buffer
 *
 *   Resulting output in the log:
 *
 *       #ATTR: VALUE
 *       #ATTR: VALUE
 */
static void
log_entry_attr(Slapi_Attr *entry_attr, char *attrname, lenstr *l)
{
    Slapi_Value **vals = attr_get_present_values(entry_attr);
    for(size_t i = 0; vals && vals[i]; i++) {
        char log_val[256] = "";
        const struct berval *bv = slapi_value_get_berval(vals[i]);
        if (bv->bv_len >= 256) {
            strncpy(log_val, bv->bv_val, 252);
            strcpy(log_val+252, "...");
        } else {
            strncpy(log_val, bv->bv_val, bv->bv_len);
            log_val[bv->bv_len] = 0;
        }
        addlenstr(l, "#");
        addlenstr(l, attrname);
        addlenstr(l, ": ");
        addlenstr(l, log_val);
        addlenstr(l, "\n");
    }
}

static void
log_entry_attr_json(Slapi_Attr *entry_attr, char *attrname, json_object *attr_list)
{
    Slapi_Value **vals = attr_get_present_values(entry_attr);
    for(size_t i = 0; vals && vals[i]; i++) {
        json_object *attr_obj = json_object_new_object();
        char log_val[256] = "";
        const struct berval *bv = slapi_value_get_berval(vals[i]);

        if (bv->bv_len >= 256) {
            strncpy(log_val, bv->bv_val, 252);
            strcpy(log_val+252, "...");
        } else {
            strncpy(log_val, bv->bv_val, bv->bv_len);
            log_val[bv->bv_len] = 0;
        }

        json_object_object_add(attr_obj, attrname,
                               json_object_new_string(log_val));
        json_object_array_add(attr_list, attr_obj);
    }
}

/*
 * Write "requested" attributes from the entry to the audit log as "comments"
 *
 *   Slapi_Entry *entry - the entry being updated
 *   lenstr *l - the audit log buffer
 *
 *   Resulting output in the log:
 *
 *       #ATTR: VALUE
 *       #ATTR: VALUE
 */
static void
add_entry_attrs_ext(Slapi_Entry *entry, lenstr *l, PRBool use_json, json_object *json_log)
{
    Slapi_Attr *entry_attr = NULL;
    json_object *id_list;
    char *display_attrs = NULL;
    char *req_attr = NULL;
    char *last = NULL;

    if (entry == NULL) {
        /* auditfail log does not have an entry to read */
        return;
    }

    display_attrs = config_get_auditlog_display_attrs();
    if (display_attrs == NULL) {
        return;
    }

    id_list = json_object_new_array();

    entry_attr = entry->e_attrs;
    if (strcmp(display_attrs, "*")) {
        /* Return specific attributes */
        for (req_attr = ldap_utf8strtok_r(display_attrs, ", ", &last); req_attr;
             req_attr = ldap_utf8strtok_r(NULL, ", ", &last))
        {
            slapi_entry_attr_find(entry, req_attr, &entry_attr);
            if (entry_attr) {
                if (use_json) {
                    log_entry_attr_json(entry_attr, req_attr, id_list);
                } else {
                    log_entry_attr(entry_attr, req_attr, l);
                }
            }
        }
    } else {
        /* Return all attributes */
        for (; entry_attr; entry_attr = entry_attr->a_next) {
            char *attr = NULL;

            slapi_attr_get_type(entry_attr, &attr);
            if (strcmp(attr, PSEUDO_ATTR_UNHASHEDUSERPASSWORD) == 0) {
                /* Do not write the unhashed clear-text password */
                continue;
            }

            if (strcasecmp(attr, SLAPI_USERPWD_ATTR) == 0 ||
                strcasecmp(attr, CONFIG_ROOTPW_ATTRIBUTE) == 0)
            {
                /* userpassword/rootdn password - mask the value */
                if (use_json) {
                    json_object *secret_obj = json_object_new_object();
                    json_object_object_add(secret_obj, attr,
                                           json_object_new_string("**********************"));
                    json_object_array_add(id_list, secret_obj);
                } else {
                    addlenstr(l, "#");
                    addlenstr(l, attr);
                    addlenstr(l, ": ****************************\n");
                }
                continue;
            }
            if (use_json) {
                log_entry_attr_json(entry_attr, attr, id_list);
            } else {
                log_entry_attr(entry_attr, attr, l);
            }
        }
    }
    if (json_object_array_length(id_list) > 0) {
        json_object_object_add(json_log, "id_list", id_list);
    } else {
        /* free empty list */
        json_object_put(id_list);
    }
    slapi_ch_free_string(&display_attrs);
}

static void
add_entry_attrs(Slapi_Entry *entry, lenstr *l)
{
    add_entry_attrs_ext(entry, l, PR_FALSE, NULL);
}

static void
add_entry_attrs_json(Slapi_Entry *entry,  json_object *json_log)
{
    add_entry_attrs_ext(entry, NULL, PR_TRUE, json_log);
}

static void
write_audit_file_json(Slapi_PBlock *pb, Slapi_Entry *entry, int logtype,
                      int optype, const char *dn, void *change, int flag,
                      time_t curtime, int result, int log_format)
{
    Connection *pb_conn = NULL;
    Slapi_Operation *operation = NULL;
    Slapi_DN *target_sdn = NULL;
    Slapi_Entry *e = NULL;
    LDAPMod **mods = NULL;
    json_object *log_json = NULL;
    json_object *del_obj = NULL;
    json_object *modrdn_obj = NULL;
    struct tm tms;
    struct tm gmtime;
    const char *target_dn = NULL;
    char binddn[JBUFSIZE] = "";
    char local_time[JBUFSIZE] = "";
    char gm_time[JBUFSIZE] = "";
    char *client_ip = NULL;
    char *server_ip = NULL;
    char *newrdn = NULL;
    char *mod_dn = NULL;
    char *msg = NULL;
    char *time_format = NULL;
    char *tmp = NULL;
    char *tmpsave = NULL;
    uint64_t conn_id = 0;
    int32_t rc = -1;
    int32_t op_id = 0;

    /* custom local time */
    time_format = config_get_auditlog_time_format();
    (void)localtime_r(&curtime, &tms);
    if (strftime(local_time, JBUFSIZE, time_format, &tms) == 0) {
        slapi_log_err(SLAPI_LOG_ERR, "write_audit_file_json",
                      "Unable to format time "
                      "(%ld) using format (%s), trying default format...\n",
                      curtime, time_format);
        /* Got an error, use default format and try again */
        if (strftime(local_time, JBUFSIZE, SLAPD_INIT_AUDITLOG_TIME_FORMAT, &tms) == 0) {
            slapi_log_err(SLAPI_LOG_ERR, "write_audit_file_json",
                      "Unable to format time (%ld)\n", curtime);
            return;
        }
    }

    /* gmtime */
    gmtime_r(&curtime, &gmtime);
    strftime(gm_time, 21, "%FT%TZ", &gmtime);

    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &target_dn);

    if (target_dn == NULL) {
        if (optype == SLAPI_OPERATION_ADD) {
            slapi_pblock_get(pb, SLAPI_TARGET_SDN, &target_sdn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            } else {
                target_dn = "unknown";
            }
        } else if (optype == SLAPI_OPERATION_DELETE) {
            slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &target_dn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            } else {
                target_dn = "unknown";
            }
        } else if (optype == SLAPI_OPERATION_MODRDN) {
            slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &target_dn);
            if (target_sdn) {
                target_dn = slapi_sdn_get_ndn(target_sdn);
            } else {
                target_dn = "unknown";
            }
        } else {
            target_dn = "unknown";
        }
    }

    if (pb_conn) {
        conn_id = pb_conn->c_connid,
        op_id = operation->o_opid,
        client_ip = pb_conn->c_ipaddr;
        server_ip = pb_conn->c_serveripaddr;
        slapi_pblock_get(pb, SLAPI_CONN_DN, &mod_dn);
        PR_snprintf(binddn, sizeof(binddn), "%s", mod_dn ? mod_dn : "");
        slapi_ch_free_string(&mod_dn);
    } else {
        conn_id = -1,
        op_id = -1,
        client_ip = "internal";
        server_ip = "internal";
        PR_snprintf(binddn, sizeof(binddn), "internal");
    }

    /* Start building the JSON obj */
    log_json = json_object_new_object();
    json_object_object_add(log_json, "gm_time",    json_object_new_string(gm_time));
    json_object_object_add(log_json, "local_time", json_object_new_string(local_time));
    json_object_object_add(log_json, "target_dn",  json_object_new_string(target_dn));
    json_object_object_add(log_json, "bind_dn",    json_object_new_string(binddn));
    json_object_object_add(log_json, "client_ip",  json_object_new_string(client_ip));
    json_object_object_add(log_json, "server_ip",  json_object_new_string(server_ip));
    json_object_object_add(log_json, "conn_id",    json_object_new_int64(conn_id));
    json_object_object_add(log_json, "op_id",      json_object_new_int(op_id));
    json_object_object_add(log_json, "result",     json_object_new_int(result));

    // Add the display attributes
    add_entry_attrs_json(entry, log_json);

    switch (optype) {
        case SLAPI_OPERATION_MODIFY:
            json_object *mod_list = json_object_new_array();
            mods = change;
            for (size_t j = 0; (mods != NULL) && (mods[j] != NULL); j++) {
                json_object *mod = NULL;
                int operationtype = mods[j]->mod_op & ~LDAP_MOD_BVALUES;

                if (strcmp(mods[j]->mod_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD) == 0) {
                    switch (logtype) {
                    case SLAPD_AUDIT_LOG:
                        if (audit_hide_unhashed_pw != 0) {
                            continue;
                        }
                        break;
                    case SLAPD_AUDITFAIL_LOG:
                        if (auditfail_hide_unhashed_pw != 0) {
                            continue;
                        }
                        break;
                    }
                }

                mod = json_object_new_object();
                switch (operationtype) {
                case LDAP_MOD_ADD:
                    json_object_object_add(mod, "op", json_object_new_string("add"));
                    break;

                case LDAP_MOD_DELETE:
                    json_object_object_add(mod, "op", json_object_new_string("delete"));
                    break;

                case LDAP_MOD_REPLACE:
                    json_object_object_add(mod, "op", json_object_new_string("replace"));
                    break;

                default:
                    operationtype = LDAP_MOD_IGNORE;
                    break;
                }
                json_object_object_add(mod, "attr", json_object_new_string(mods[j]->mod_type));

                if (operationtype != LDAP_MOD_IGNORE) {
                    json_object *val_list = NULL;
                    val_list = json_object_new_array();
                    for (size_t i = 0; mods[j]->mod_bvalues != NULL && mods[j]->mod_bvalues[i] != NULL; i++) {
                        json_object_array_add(val_list, json_object_new_string(mods[j]->mod_bvalues[i]->bv_val));
                    }
                    json_object_object_add(mod, "values", val_list);
                }
                json_object_array_add(mod_list, mod);
            }
            /* Add entire mod list to the main object */
            json_object_object_add(log_json, "modify", mod_list);
            break;

        case SLAPI_OPERATION_ADD:
            int len;
            e = change;
            tmp = slapi_entry2str(e, &len);
            tmpsave = tmp;
            while ((tmp = strchr(tmp, '\n')) != NULL) {
                tmp++;
                if (!ldap_utf8isspace(tmp)) {
                    break;
                }
            }
            json_object_object_add(log_json, "add", json_object_new_string(tmp));
            slapi_ch_free_string(&tmpsave);
            break;

        case SLAPI_OPERATION_DELETE:
            tmp = change;
            del_obj = json_object_new_object();
            if (tmp && tmp[0]) {
                json_object_object_add(del_obj, "dn", json_object_new_string(target_dn));
                json_object_object_add(log_json, "delete", del_obj);
            } else {
                json_object_object_add(del_obj, "dn", json_object_new_string(target_dn));
                json_object_object_add(log_json, "delete", del_obj);
            }
            break;

        case SLAPI_OPERATION_MODDN:
            newrdn = ((char **)change)[0];
            modrdn_obj = json_object_new_object();
            json_object_object_add(modrdn_obj, attr_newrdn, json_object_new_string(newrdn));
            json_object_object_add(modrdn_obj, attr_deleteoldrdn, json_object_new_boolean(flag));

            if (((char **)change)[2]) {
                char *newsuperior = ((char **)change)[2];
                json_object_object_add(modrdn_obj, attr_newsuperior, json_object_new_string(newsuperior));
            }
            json_object_object_add(log_json, "modrdn", modrdn_obj);
            break;
    }

    msg = (char *)json_object_to_json_string_ext(log_json, log_format);

    switch (logtype) {
    case SLAPD_AUDIT_LOG:
        rc = slapd_log_audit(msg, PR_TRUE);
        break;
    case SLAPD_AUDITFAIL_LOG:
        rc = slapd_log_auditfail(msg, PR_TRUE);
        break;
    default:
        /* Unsupported log type, we should make some noise */
        slapi_log_err(SLAPI_LOG_ERR, "write_audit_file_json",
                      "Invalid log type specified. logtype %d\n", logtype);
        break;
    }

    /* Done with JSON object, this will free it */
    json_object_put(log_json);
    slapi_ch_free_string(&time_format);

    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "write_audit_file_json",
                      "Failed to update audit log, error %d\n", rc);
    }
}

/*
 * Function: write_audit_file
 * Arguments:
 *            pb - pblock
 *            entry - Slapi Entry used for sdiplay attributes
 *            logtype - Destination where the message will go.
 *            optype - type of LDAP operation being logged
 *            dn     - distinguished name of entry being changed
 *            change - pointer to the actual change operation
 *                     For a delete operation, may contain the modifier's DN.
 *            flag   - only used by modrdn operations - value of deleteoldrdn flag
 *            curtime - the current time
 *            rc     - The ldap result code. Used in conjunction with auditfail
 *            sourcelog - The source of the message (audit or auditfail)
 * Returns: nothing
 */
static void
write_audit_file(
    Slapi_PBlock *pb,
    Slapi_Entry *entry,
    int logtype,
    int optype,
    const char *dn,
    void *change,
    int flag,
    time_t curtime,
    int rc,
    int sourcelog)
{
    LDAPMod **mods;
    Slapi_Entry *e;
    char *newrdn, *tmp, *tmpsave;
    int len, i, j;
    char *timestr;
    char *rcstr;
    lenstr *l;
    int log_format = config_get_auditlog_log_format();

    if (log_format != LOG_FORMAT_DEFAULT) {
        /* We are using the json format instead of the LDIF format */
        write_audit_file_json(pb, entry, logtype, optype, dn, change, flag,
                              curtime, rc, log_format);
        return;
    }

    l = lenstr_new();

    addlenstr(l, "time: ");
    timestr = format_localTime(curtime);
    addlenstr(l, timestr);
    slapi_ch_free_string(&timestr);
    addlenstr(l, "\n");
    addlenstr(l, "dn: ");
    addlenstr(l, dn);
    addlenstr(l, "\n");

    /* Display requested attributes from the entry */
    add_entry_attrs(entry, l);

    /* Display the operation result */
    addlenstr(l, "result: ");
    rcstr = slapi_ch_smprintf("%d", rc);
    addlenstr(l, rcstr);
    slapi_ch_free_string(&rcstr);
    addlenstr(l, "\n");

    switch (optype) {
    case SLAPI_OPERATION_MODIFY:
        addlenstr(l, attr_changetype);
        addlenstr(l, ": modify\n");
        mods = change;
        for (j = 0; (mods != NULL) && (mods[j] != NULL); j++) {
            int operationtype = mods[j]->mod_op & ~LDAP_MOD_BVALUES;

            if (strcmp(mods[j]->mod_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD) == 0) {
                switch (logtype) {
                case SLAPD_AUDIT_LOG:
                    if (audit_hide_unhashed_pw != 0) {
                        continue;
                    }
                    break;
                case SLAPD_AUDITFAIL_LOG:
                    if (auditfail_hide_unhashed_pw != 0) {
                        continue;
                    }
                    break;
                }
            }
            switch (operationtype) {
            case LDAP_MOD_ADD:
                addlenstr(l, "add: ");
                addlenstr(l, mods[j]->mod_type);
                addlenstr(l, "\n");
                break;

            case LDAP_MOD_DELETE:
                addlenstr(l, "delete: ");
                addlenstr(l, mods[j]->mod_type);
                addlenstr(l, "\n");
                break;

            case LDAP_MOD_REPLACE:
                addlenstr(l, "replace: ");
                addlenstr(l, mods[j]->mod_type);
                addlenstr(l, "\n");
                break;

            default:
                operationtype = LDAP_MOD_IGNORE;
                break;
            }
            if (operationtype != LDAP_MOD_IGNORE) {
                for (i = 0; mods[j]->mod_bvalues != NULL && mods[j]->mod_bvalues[i] != NULL; i++) {
                    char *buf, *bufp;
                    len = strlen(mods[j]->mod_type);
                    len = LDIF_SIZE_NEEDED(len, mods[j]->mod_bvalues[i]->bv_len) + 1;
                    buf = slapi_ch_malloc(len);
                    bufp = buf;
                    slapi_ldif_put_type_and_value_with_options(&bufp, mods[j]->mod_type,
                                                               mods[j]->mod_bvalues[i]->bv_val,
                                                               mods[j]->mod_bvalues[i]->bv_len, 0);
                    *bufp = '\0';
                    addlenstr(l, buf);
                    slapi_ch_free((void **)&buf);
                }
            }
            addlenstr(l, "-\n");
        }
        break;

    case SLAPI_OPERATION_ADD:
        e = change;
        addlenstr(l, attr_changetype);
        addlenstr(l, ": add\n");
        tmp = slapi_entry2str(e, &len);
        tmpsave = tmp;
        while ((tmp = strchr(tmp, '\n')) != NULL) {
            tmp++;
            if (!ldap_utf8isspace(tmp)) {
                break;
            }
        }
        addlenstr(l, tmp);
        slapi_ch_free((void **)&tmpsave);
        break;

    case SLAPI_OPERATION_DELETE:
        tmp = change;
        addlenstr(l, attr_changetype);
        addlenstr(l, ": delete\n");
        if (tmp && tmp[0]) {
            addlenstr(l, attr_modifiersname);
            addlenstr(l, ": ");
            addlenstr(l, tmp);
            addlenstr(l, "\n");
        }
        break;

    case SLAPI_OPERATION_MODDN:
        newrdn = ((char **)change)[0];
        addlenstr(l, attr_changetype);
        addlenstr(l, ": modrdn\n");
        addlenstr(l, attr_newrdn);
        addlenstr(l, ": ");
        addlenstr(l, newrdn);
        addlenstr(l, "\n");
        addlenstr(l, attr_deleteoldrdn);
        addlenstr(l, ": ");
        addlenstr(l, flag ? "1" : "0");
        addlenstr(l, "\n");
        if (((char **)change)[2]) {
            char *newsuperior = ((char **)change)[2];
            addlenstr(l, attr_newsuperior);
            addlenstr(l, ": ");
            addlenstr(l, newsuperior);
            addlenstr(l, "\n");
        }
        if (((char **)change)[1]) {
            char *modifier = ((char **)change)[1];
            addlenstr(l, attr_modifiersname);
            addlenstr(l, ": ");
            addlenstr(l, modifier);
            addlenstr(l, "\n");
        }
    }
    addlenstr(l, "\n");

    switch (logtype) {
    case SLAPD_AUDIT_LOG:
        slapd_log_audit(l->ls_buf, PR_FALSE);
        break;
    case SLAPD_AUDITFAIL_LOG:
        slapd_log_auditfail(l->ls_buf, PR_FALSE);
        break;
    default:
        /* Unsupported log type, we should make some noise */
        slapi_log_err(SLAPI_LOG_ERR, "write_audit_log", "Invalid log type specified. logtype %d\n", logtype);
        break;
    }

    lenstr_free(&l);
}

void
auditlog_hide_unhashed_pw()
{
    audit_hide_unhashed_pw = 1;
}

void
auditlog_expose_unhashed_pw()
{
    audit_hide_unhashed_pw = 0;
}

void
auditfaillog_hide_unhashed_pw()
{
    auditfail_hide_unhashed_pw = 1;
}

void
auditfaillog_expose_unhashed_pw()
{
    auditfail_hide_unhashed_pw = 0;
}
