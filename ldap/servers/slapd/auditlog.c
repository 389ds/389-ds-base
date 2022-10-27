/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
char *attr_changetype = ATTR_CHANGETYPE;
char *attr_newrdn = ATTR_NEWRDN;
char *attr_deleteoldrdn = ATTR_DELETEOLDRDN;
char *attr_newsuperior = ATTR_NEWSUPERIOR;
char *attr_modifiersname = ATTR_MODIFIERSNAME;

static int audit_hide_unhashed_pw = 1;
static int auditfail_hide_unhashed_pw = 1;

/* Forward Declarations */
static void write_audit_file(Slapi_Entry *entry, int logtype, int optype, const char *dn, void *change, int flag, time_t curtime, int rc, int sourcelog);

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
    write_audit_file(entry, SLAPD_AUDIT_LOG, operation_get_type(op), dn, change, flag, curtime, LDAP_SUCCESS, SLAPD_AUDIT_LOG);
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
        write_audit_file(NULL, SLAPD_AUDIT_LOG, operation_get_type(op), dn, change, flag, curtime, pbrc, SLAPD_AUDITFAIL_LOG);
    } else {
        /* If we have our own auditfail log path */
        write_audit_file(NULL, SLAPD_AUDITFAIL_LOG, operation_get_type(op), dn, change, flag, curtime, pbrc, SLAPD_AUDITFAIL_LOG);
    }
    slapi_ch_free_string(&auditfail_config);
    slapi_ch_free_string(&audit_config);
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
add_entry_attrs(Slapi_Entry *entry, lenstr *l)
{
    Slapi_Attr *entry_attr = NULL;
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

    entry_attr = entry->e_attrs;
    if (strcmp(display_attrs, "*")) {
        /* Return specific attributes */
        for (req_attr = ldap_utf8strtok_r(display_attrs, ", ", &last); req_attr;
             req_attr = ldap_utf8strtok_r(NULL, ", ", &last))
        {
            char **vals = slapi_entry_attr_get_charray(entry, req_attr);
            for(size_t i = 0; vals && vals[i]; i++) {
                char log_val[256] = {0};

                if (strlen(vals[i]) > 256) {
                    strncpy(log_val, vals[i], 252);
                    strcat(log_val, "...");
                } else {
                    strcpy(log_val, vals[i]);
                }
                addlenstr(l, "#");
                addlenstr(l, req_attr);
                addlenstr(l, ": ");
                addlenstr(l, log_val);
                addlenstr(l, "\n");
            }
        }
    } else {
        /* Return all attributes */
        for (; entry_attr; entry_attr = entry_attr->a_next) {
            Slapi_Value **vals = attr_get_present_values(entry_attr);
            char *attr = NULL;
            const char *val = NULL;

            slapi_attr_get_type(entry_attr, &attr);
            if (strcmp(attr, PSEUDO_ATTR_UNHASHEDUSERPASSWORD) == 0) {
                /* Do not write the unhashed clear-text password */
                continue;
            }

            if (strcasecmp(attr, SLAPI_USERPWD_ATTR) == 0 ||
                strcasecmp(attr, CONFIG_ROOTPW_ATTRIBUTE) == 0)
            {
                /* userpassword/rootdn password - mask the value */
                addlenstr(l, "#");
                addlenstr(l, attr);
                addlenstr(l, ": ****************************\n");
                continue;
            }

            for(size_t i = 0; vals && vals[i]; i++) {
                char log_val[256] = {0};

                val = slapi_value_get_string(vals[i]);
                if (strlen(val) > 256) {
                    strncpy(log_val, val, 252);
                    strcat(log_val, "...");
                } else {
                    strcpy(log_val, val);
                }
                addlenstr(l, "#");
                addlenstr(l, attr);
                addlenstr(l, ": ");
                addlenstr(l, log_val);
                addlenstr(l, "\n");
            }
        }
    }
    slapi_ch_free_string(&display_attrs);
}

/*
 * Function: write_audit_file
 * Arguments:
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
        slapd_log_audit(l->ls_buf, l->ls_len, sourcelog);
        break;
    case SLAPD_AUDITFAIL_LOG:
        slapd_log_auditfail(l->ls_buf, l->ls_len);
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
