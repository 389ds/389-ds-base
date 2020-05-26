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


#include "retrocl.h"

static int
entry2reple(Slapi_Entry *e, Slapi_Entry *oe, int optype);

static int
mods2reple(Slapi_Entry *e, LDAPMod **ldm);

static int
modrdn2reple(Slapi_Entry *e, const char *newrdn, int deloldrdn, LDAPMod **ldm, const char *newsup);

/******************************/

const char *retrocl_changenumber = "changenumber";
const char *retrocl_targetdn = "targetdn";
const char *retrocl_changetype = "changetype";
const char *retrocl_newrdn = "newrdn";
const char *retrocl_deleteoldrdn = "deleteoldrdn";
const char *retrocl_changes = "changes";
const char *retrocl_newsuperior = "newsuperior";
const char *retrocl_changetime = "changetime";
const char *retrocl_objectclass = "objectclass";
const char *retrocl_nsuniqueid = "nsuniqueid";
const char *retrocl_isreplicated = "isreplicated";

/*
 * Function: make_changes_string
 *
 * Returns:
 *
 * Arguments:
 *
 * Description:
 * loop through the LDAPMod struct and construct the changes attribute/
 *
 */

static lenstr *
make_changes_string(LDAPMod **ldm, const char **includeattrs)
{
    lenstr *l;
    int i, j, len;
    int skip;

    l = lenstr_new();

    for (i = 0; ldm[i] != NULL; i++) {
        /* If a list of explicit attributes was given, only add those */
        if (NULL != includeattrs) {
            skip = 1;
            for (j = 0; includeattrs[j] != NULL; j++) {
                if (strcasecmp(includeattrs[j], ldm[i]->mod_type) == 0) {
                    skip = 0;
                    break;
                }
            }
            if (skip) {
                continue;
            }
        }
        if (SLAPD_UNHASHED_PW_NOLOG == slapi_config_get_unhashed_pw_switch()) {
            if (0 == strcasecmp(ldm[i]->mod_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD)) {
                /* If nsslapd-unhashed-pw-switch == nolog, skip writing it to cl. */
                continue;
            }
        }
        switch (ldm[i]->mod_op & ~LDAP_MOD_BVALUES) {
        case LDAP_MOD_ADD:
            addlenstr(l, "add: ");
            addlenstr(l, ldm[i]->mod_type);
            addlenstr(l, "\n");
            break;
        case LDAP_MOD_DELETE:
            addlenstr(l, "delete: ");
            addlenstr(l, ldm[i]->mod_type);
            addlenstr(l, "\n");
            break;
        case LDAP_MOD_REPLACE:
            addlenstr(l, "replace: ");
            addlenstr(l, ldm[i]->mod_type);
            addlenstr(l, "\n");
            break;
        }
        for (j = 0; ldm[i]->mod_bvalues != NULL &&
                    ldm[i]->mod_bvalues[j] != NULL;
             j++) {
            char *buf = NULL;
            char *bufp = NULL;

            len = strlen(ldm[i]->mod_type);
            len = LDIF_SIZE_NEEDED(len,
                                   ldm[i]->mod_bvalues[j]->bv_len) +
                  1;
            buf = slapi_ch_malloc(len);
            bufp = buf;
            slapi_ldif_put_type_and_value_with_options(&bufp, ldm[i]->mod_type,
                                                       ldm[i]->mod_bvalues[j]->bv_val,
                                                       ldm[i]->mod_bvalues[j]->bv_len, 0);
            *bufp = '\0';

            addlenstr(l, buf);

            slapi_ch_free_string(&buf);
        }
        addlenstr(l, "-\n");
    }
    return l;
}

/*
 * Function: write_replog_db
 * Arguments: be     - backend to which this change is being applied
 *            optype - type of LDAP operation being logged
 *            dn     - distinguished name of entry being changed
 *            log_m - pointer to the actual change operation on a modify
 *            flag   - only used by modrdn operations - value of deleteoldrdn
 *            curtime - the current time
 * Returns: error code
 * Description: Given a change, construct an entry which is to be added to the
 *              changelog database.
 */
static int
write_replog_db(
    Slapi_PBlock *pb,
    int optype,
    char *dn,
    LDAPMod **log_m,
    int flag,
    time_t curtime,
    Slapi_Entry *log_e,
    Slapi_Entry *post_entry,
    const char *newrdn,
    LDAPMod **modrdn_mods,
    const char *newsuperior)
{
    Slapi_PBlock *newPb = NULL;
    changeNumber changenum;
    struct berval *vals[2];
    struct berval val;
    Slapi_Entry *e;
    char chnobuf[22] = {0};
    char *edn;
    int extensibleObject = 0;
    int err = 0;
    int ret = LDAP_SUCCESS;
    int i;

    if (!dn) {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "write_replog_db: NULL dn\n");
        return ret;
    }

    if (post_entry) {
        if (!retrocl_entry_in_scope(log_e) && !retrocl_entry_in_scope(post_entry)) {
            /* modrdn: entry not in scope, just return... */
            return ret;
        }
    } else {
        if (!retrocl_entry_in_scope(log_e)) {
            /* entry not in scope, just return... */
            return ret;
        }
    }

    PR_Lock(retrocl_internal_lock);
    changenum = retrocl_assign_changenumber();

    PR_ASSERT(changenum > 0UL);
    slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                  "write_replog_db: write change record %lu for dn: \"%s\"\n",
                  changenum, dn);

    /* Construct the dn of this change record */
    edn = slapi_ch_smprintf("%s=%lu,%s", retrocl_changenumber, changenum, RETROCL_CHANGELOG_DN);

    /*
     * Create the entry struct, and fill in fields common to all types
     * of change records.
     */
    vals[0] = &val;
    vals[1] = NULL;

    e = slapi_entry_alloc();
    slapi_entry_set_dn(e, slapi_ch_strdup(edn));

    /* Set the objectclass attribute */
    val.bv_val = "top";
    val.bv_len = 3;
    slapi_entry_add_values(e, "objectclass", vals);

    val.bv_val = "changelogentry";
    val.bv_len = 14;
    slapi_entry_add_values(e, "objectclass", vals);

    for (i = 0; i < retrocl_nattributes; i++) {
        char *attributeName = retrocl_attributes[i];
        char *attributeAlias = retrocl_aliases[i];

        if (attributeAlias == NULL) {
            attributeAlias = attributeName;
        }

        if (strcasecmp(attributeName, retrocl_nsuniqueid) == 0) {
            Slapi_Entry *entry = NULL;
            const char *uniqueId = NULL;

            slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
            if (entry == NULL) {
                slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &entry);
            }
            if (entry == NULL)
                continue;
            uniqueId = slapi_entry_get_uniqueid(entry);

            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "write_replog_db: add %s: \"%s\"\n", attributeAlias, uniqueId);

            val.bv_val = (char *)uniqueId;
            val.bv_len = strlen(uniqueId);

            slapi_entry_add_values(e, attributeAlias, vals);

            extensibleObject = 1;

        } else if (strcasecmp(attributeName, retrocl_isreplicated) == 0) {
            int isReplicated = 0;
            char *attributeValue = NULL;

            slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isReplicated);
            attributeValue = isReplicated ? "TRUE" : "FALSE";

            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "write_replog_db: add %s: \"%s\"\n", attributeAlias, attributeValue);

            val.bv_val = attributeValue;
            val.bv_len = strlen(attributeValue);

            slapi_entry_add_values(e, attributeAlias, vals);

            extensibleObject = 1;

        } else {
            Slapi_Entry *entry = NULL;
            Slapi_ValueSet *valueSet = NULL;
            int type_name_disposition = 0;
            char *actual_type_name = NULL;
            int flags = 0;
            int buffer_flags = 0;

            slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
            if (entry != NULL) {
                slapi_vattr_values_get(entry, attributeName, &valueSet,
                                       &type_name_disposition, &actual_type_name, flags, &buffer_flags);
            }

            if (valueSet == NULL) {
                slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &entry);
                if (entry != NULL) {
                    slapi_vattr_values_get(entry, attributeName, &valueSet,
                                           &type_name_disposition, &actual_type_name, flags, &buffer_flags);
                }
            }

            if (valueSet == NULL)
                continue;

            slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME,
                          "write_replog_db: add %s\n", attributeAlias);

            slapi_entry_add_valueset(e, attributeAlias, valueSet);
            slapi_vattr_values_free(&valueSet, &actual_type_name, buffer_flags);

            extensibleObject = 1;
        }
    }

    if (extensibleObject) {
        val.bv_val = "extensibleObject";
        val.bv_len = 16;
        slapi_entry_add_values(e, "objectclass", vals);
    }

    /* Set the changeNumber attribute */
    sprintf(chnobuf, "%lu", changenum);
    val.bv_val = chnobuf;
    val.bv_len = strlen(chnobuf);
    slapi_entry_add_values(e, retrocl_changenumber, vals);

    /* Set the targetentrydn attribute */
    val.bv_val = dn;
    val.bv_len = strlen(dn);
    slapi_entry_add_values(e, retrocl_targetdn, vals);

    /* Set the changeTime attribute */
    val.bv_val = format_genTime(curtime);
    val.bv_len = strlen(val.bv_val);
    slapi_entry_add_values(e, retrocl_changetime, vals);
    slapi_ch_free((void **)&val.bv_val);

    /*
     * Finish constructing the entry.  How to do it depends on the type
     * of modification being logged.
     */
    switch (optype) {
    case OP_ADD:
        if (entry2reple(e, log_e, OP_ADD) != 0) {
            err = SLAPI_PLUGIN_FAILURE;
        }
        break;

    case OP_MODIFY:
        if (mods2reple(e, log_m) != 0) {
            err = SLAPI_PLUGIN_FAILURE;
        }
        break;

    case OP_MODRDN:
        if (modrdn2reple(e, newrdn, flag, modrdn_mods, newsuperior) != 0) {
            err = SLAPI_PLUGIN_FAILURE;
        }
        break;

    case OP_DELETE:
        if (retrocl_log_deleted) {
            /* we have to log the full entry */
            if (entry2reple(e, log_e, OP_DELETE) != 0) {
                err = SLAPI_PLUGIN_FAILURE;
            }
        } else {
            /* Set the changetype attribute */
            val.bv_val = "delete";
            val.bv_len = 6;
            slapi_entry_add_values(e, retrocl_changetype, vals);
        }
        break;

    default:
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "write_replog_db - Unknown LDAP operation type %d.\n", optype);
        err = SLAPI_PLUGIN_FAILURE;
    }

    /* Call the repl backend to add this entry */
    if (0 == err) {
        newPb = slapi_pblock_new();
        slapi_add_entry_internal_set_pb(newPb, e, NULL /* controls */,
                                        g_plg_identity[PLUGIN_RETROCL],
                                        /* dont leave entry in cache if main oparation is aborted */
                                        SLAPI_OP_FLAG_NEVER_CACHE);
        slapi_add_internal_pb(newPb);
        slapi_pblock_get(newPb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
        slapi_pblock_destroy(newPb);
        if (0 != ret) {
            slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                          "write_replog_db - An error occured while adding change "
                          "number %lu, dn = %s: %s. \n",
                          changenum, edn, ldap_err2string(ret));
            retrocl_release_changenumber();

        } else {
            /* Tell the change numbering system this one's committed to disk  */
            retrocl_commit_changenumber();
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "write_replog_db - An error occurred while constructing "
                      "change record number %ld.\n",
                      changenum);
        retrocl_release_changenumber();
        ret = err;
    }
    PR_Unlock(retrocl_internal_lock);
    if (NULL != edn) {
        slapi_ch_free((void **)&edn);
    }

    return ret;
}


/*
 * Function: entry2reple
 * Arguments: e  - a partially-constructed Slapi_Entry structure
 *            oe - the original entry (an entry being added by a client).
 * Returns: 0 on success.
 * Description: Given an Slapi_Entry struct, construct a changelog entry which will be
 *              added to the replication database.  It is assumed that e points
 *              to an entry obtained from slapi_entry_alloc().
 */
static int
entry2reple(Slapi_Entry *e, Slapi_Entry *oe, int optype)
{
    char *p, *estr;
    struct berval *vals[2];
    struct berval val;
    int len;

    vals[0] = &val;
    vals[1] = NULL;

    /* Set the changetype attribute */
    if (optype == OP_ADD) {
        val.bv_val = "add";
        val.bv_len = 3;
    } else if (optype == OP_DELETE) {
        val.bv_val = "delete";
        val.bv_len = 6;
    } else {
        return (1);
    }
    slapi_entry_add_values(e, retrocl_changetype, vals);

    estr = slapi_entry2str(oe, &len);
    p = estr;
    /* Skip over the dn: line */
    while ((p = strchr(p, '\n')) != NULL) {
        p++;
        if (!ldap_utf8isspace(p)) {
            break;
        }
    }
    val.bv_val = p;
    val.bv_len = len - (p - estr); /* length + terminating \0 */
    slapi_entry_add_values(e, retrocl_changes, vals);
    slapi_ch_free_string(&estr);
    return 0;
}

/*
 * Function: mods2reple
 * Arguments: e  - a partially-constructed Slapi_Entry structure
 *            ldm - an array of pointers to LDAPMod structures describing the
 *                  change applied.
 * Returns: 0 on success.
 * Description: Given a pointer to an LDAPMod struct and a dn, construct
 *              a new entry which will be added to the replication database.
 *              It is assumed that e points to an entry obtained from
 *              slapi_entry_alloc().
 */
static int
mods2reple(Slapi_Entry *e, LDAPMod **ldm)
{
    struct berval val;
    struct berval *vals[2];
    lenstr *l;

    vals[0] = &val;
    vals[1] = NULL;

    /* Set the changetype attribute */
    val.bv_val = "modify";
    val.bv_len = 6;
    slapi_entry_add_values(e, "changetype", vals);

    if (NULL != ldm) {
        l = make_changes_string(ldm, NULL);
        if (NULL != l) {
            val.bv_val = l->ls_buf;
            val.bv_len = l->ls_len + 1; /* string + terminating \0 */
            slapi_entry_add_values(e, retrocl_changes, vals);
            lenstr_free(&l);
        }
    }
    return 0;
}


/*
 * Function: modrdn2reple
 * Arguments: e  - a partially-constructed Slapi_Entry structure
 *            newrdn - the new relative distinguished name for the entry
 *            deloldrdn - the "deleteoldrdn" flag provided by the client
 *            ldm - any modifications applied as a side-effect of the modrdn
 * Returns: 0 on success
 * Description: Given a dn, a new rdn,  and a deleteoldrdn flag, construct
 * a new entry which will be added to the replication database reflecting a
 * completed modrdn operation.  The entry has the same form as above.
 * It is assumed that e points to an entry obtained from slapi_entry_alloc().
 */
static int
modrdn2reple(
    Slapi_Entry *e,
    const char *newrdn,
    int deloldrdn,
    LDAPMod **ldm,
    const char *newsuperior)
{
    struct berval val;
    struct berval *vals[2];
    lenstr *l;
    static const char *lastmodattrs[] = {"modifiersname", "modifytimestamp",
                                         "creatorsname", "createtimestamp",
                                         NULL};

    vals[0] = &val;
    vals[1] = NULL;

    val.bv_val = "modrdn";
    val.bv_len = 6;
    slapi_entry_add_values(e, retrocl_changetype, vals);

    if (newrdn) {
        val.bv_val = (char *)newrdn; /* cast away const */
        val.bv_len = strlen(newrdn);
        slapi_entry_add_values(e, retrocl_newrdn, vals);
    }

    if (deloldrdn == 0) {
        val.bv_val = "FALSE";
        val.bv_len = 5;
    } else {
        val.bv_val = "TRUE";
        val.bv_len = 4;
    }
    slapi_entry_add_values(e, retrocl_deleteoldrdn, vals);

    if (newsuperior) {
        val.bv_val = (char *)newsuperior; /* cast away const */
        val.bv_len = strlen(newsuperior);
        slapi_entry_add_values(e, retrocl_newsuperior, vals);
    }

    if (NULL != ldm) {
        l = make_changes_string(ldm, lastmodattrs);
        if (NULL != l) {
            if (l->ls_len) {
                val.bv_val = l->ls_buf;
                val.bv_len = l->ls_len;
                slapi_entry_add_values(e, retrocl_changes, vals);
            }
            lenstr_free(&l);
        }
    }

    return 0;
}

/*
 * Function: retrocl_postob
 *
 * Returns: 0 on success
 *
 * Arguments: pblock, optype (add, del, modify etc)
 *
 * Description: called from retrocl.c op-specific plugins.
 *
 * Please be aware that operation threads may be scheduled out between their
 * being performed inside of the LDBM database and the changelog plugin
 * running.  For example, suppose MA and MB are two modify operations on the
 * same entry.  MA may be performed on the LDBM database, and then block
 * before the changelog runs.  MB then runs against the LDBM database and then
 * is written to the changelog.  MA starts running.  In the changelog, MB will
 * appear to have been performed before MA, but in the LDBM database the
 * opposite will have occured.
 *
 *
 */

int
retrocl_postob(Slapi_PBlock *pb, int optype)
{
    char *dn;
    LDAPMod **log_m = NULL;
    int flag = 0;
    Slapi_Entry *entry = NULL;
    Slapi_Entry *post_entry = NULL;
    Slapi_Operation *op = NULL;
    LDAPMod **modrdn_mods = NULL;
    char *newrdn = NULL;
    Slapi_DN *newsuperior = NULL;
    Slapi_Backend *be = NULL;
    time_t curtime;
    int rc = SLAPI_PLUGIN_SUCCESS;

    /*
     * Check to see if the change was made to the replication backend db.
     * If so, don't try to log it to the db (otherwise, we'd get in a loop).
     */

    (void)slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (be == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME, "retrocl_postob - Backend is not set\n");
        return SLAPI_PLUGIN_FAILURE;
    }

    if (slapi_be_logchanges(be) == 0) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change if not logging\n");
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (retrocl_be_changelog == NULL || be == retrocl_be_changelog) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change if no/cl be\n");
        return SLAPI_PLUGIN_SUCCESS;
    }

    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);

    if (rc != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change if op failed %d\n", rc);
        /* this could also mean that the changenumber is no longer correct
         * set a flag to check at next assignment
         */
        retrocl_set_check_changenumber();
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (slapi_op_abandoned(pb)) {
        slapi_log_err(SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change if op abandoned\n");
        return SLAPI_PLUGIN_SUCCESS;
    }

    curtime = slapi_current_utc_time();

    (void)slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &dn);

    /* change number could be retrieved from Operation extension stored in
     * the pblock, or else it needs to be made dynamically. */

    /* get the operation extension and retrieve the change number */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (op == NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change if no op\n");
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY) ||
        operation_is_flag_set(op, OP_FLAG_NOOP)) {
        slapi_log_err(SLAPI_LOG_TRACE, RETROCL_PLUGIN_NAME, "retrocl_postob - Not applying change for nsTombstone entries\n");
        return SLAPI_PLUGIN_SUCCESS;
    }
    /*
     * Start by grabbing the preop entry, ADD will replace it as needed.  Getting the entry
     * allows up to perform scoping in write_replog_db() for all op types.
     */
    (void)slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &entry);

    switch (optype) {
    case OP_MODIFY:
        (void)slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &log_m);
        break;
    case OP_ADD:
        /*
         * For adds, we want the unnormalized dn, so we can preserve
         * spacing, case, when replicating it.
         */
        (void)slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &entry);
        if (NULL != entry) {
            dn = slapi_entry_get_dn(entry);
        }
        break;
    case OP_DELETE:
        if (retrocl_log_deleted)
            (void)slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &entry);
        break;
    case OP_MODRDN:
        /* newrdn is used just for logging; no need to be normalized */
        (void)slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
        (void)slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &flag);
        (void)slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &modrdn_mods);
        (void)slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &newsuperior);
        (void)slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_entry);
        break;
    }

    /* check if we should log change to retro changelog, and
     * if so, do it here */
    if ((rc = write_replog_db(pb, optype, dn, log_m, flag, curtime, entry,
                              post_entry, newrdn, modrdn_mods, slapi_sdn_get_dn(newsuperior)))) {
        slapi_log_err(SLAPI_LOG_ERR, RETROCL_PLUGIN_NAME,
                      "retrocl_postob - Operation failure [%d]\n", rc);
        if (rc < 0) {
            rc = LDAP_OPERATIONS_ERROR;
        }
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
    }

    return rc;
}
