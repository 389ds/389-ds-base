/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "sync.h"

static SyncOpInfo *new_SyncOpInfo(int flag, PRThread *tid, Sync_Cookie *cookie);

static int sync_extension_type;
static int sync_extension_handle;

static SyncOpInfo *sync_get_operation_extension(Slapi_PBlock *pb);
static void sync_set_operation_extension(Slapi_PBlock *pb, SyncOpInfo *spec);
static int sync_find_ref_by_uuid(Sync_UpdateNode *updates, int stop, char *uniqueid);
static void sync_free_update_nodes(Sync_UpdateNode **updates, int count);
Slapi_Entry *sync_deleted_entry_from_changelog(Slapi_Entry *cl_entry);
static int sync_feature_allowed(Slapi_PBlock *pb);

static int
sync_feature_allowed(Slapi_PBlock *pb)
{
    int isroot = 0;
    int ldapcode = LDAP_SUCCESS;

    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    if (!isroot) {
        char *dn;
        Slapi_Entry *feature = NULL;

        /* Fetch the feature entry and see if the requestor is allowed access. */
        dn = slapi_ch_smprintf("dn: oid=%s,cn=features,cn=config", LDAP_CONTROL_SYNC);
        if ((feature = slapi_str2entry(dn, 0)) != NULL) {
            char *dummy_attr = "1.1";

            ldapcode = slapi_access_allowed(pb, feature, dummy_attr, NULL, SLAPI_ACL_READ);
        }

        /* If the feature entry does not exist, deny use of the control.  Only
         * the root DN will be allowed to use the control in this case. */
        if ((feature == NULL) || (ldapcode != LDAP_SUCCESS)) {
            ldapcode = LDAP_INSUFFICIENT_ACCESS;
        }
        slapi_ch_free((void **)&dn);
        slapi_entry_free(feature);
    }
    return (ldapcode);
}

int
sync_srch_refresh_pre_search(Slapi_PBlock *pb)
{

    LDAPControl **requestcontrols;
    struct berval *psbvp;
    Sync_Cookie *client_cookie = NULL;
    Sync_Cookie *session_cookie = NULL;
    int rc = 0;
    int sync_persist = 0;
    PRThread *tid = NULL;
    int entries_sent = 0;

    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &requestcontrols);
    if (slapi_control_present(requestcontrols, LDAP_CONTROL_SYNC, &psbvp, NULL)) {
        char *cookie = NULL;
        int32_t mode = 1;
        int32_t refresh = 0;
        bool cookie_refresh = 0;

        if (sync_parse_control_value(psbvp, &mode,
                                     &refresh, &cookie) != LDAP_SUCCESS) {
            rc = 1;
            goto error_return;
        } else {
            /* control is valid, check if usere is allowed to perform sync searches */
            rc = sync_feature_allowed(pb);
            if (rc) {
                sync_result_err(pb, rc, NULL);
                goto error_return;
            }
        }

        if (mode == 1 || mode == 3) {
            /*
             * OpenLDAP violates rfc4533 by sending a "rid=" in it's initial cookie sync, even
             * when using their changelog mode. As a result, we parse the cookie to handle this
             * shenangians to determine if this is valid.
             */
            client_cookie = sync_cookie_parse(cookie, &cookie_refresh);
            /*
             * we need to return a cookie in the result message
             * indicating a state to be used in future sessions
             * as starting point - create it now. We need to provide
             * the client_cookie so we understand if we are in
             * openldap mode or not, and to get the 'rid' of the
             * consumer.
             */
            session_cookie = sync_cookie_create(pb, client_cookie);
            PR_ASSERT(session_cookie);
            /*
             *  if mode is persist we need to setup the persit handler
             * to catch the mods while the refresh is done
             */
            if (mode == 3) {
                tid = sync_persist_add(pb);
                if (tid)
                    sync_persist = 1;
                else {
                    rc = LDAP_UNWILLING_TO_PERFORM;
                    sync_result_err(pb, rc, "Too many active synchronization sessions");
                    goto error_return;
                }
            }
            /*
             * now handle the refresh request
             * there are two scenarios
             * 1. no cookie is provided this means send all entries matching the search request
             * 2. a cookie is provided: send all entries changed since the cookie was issued
             *     -- return an error if the cookie is invalid
             *     -- return e-syncRefreshRequired if the data referenced in the cookie are no
             *         longer in the history
            */
            if (!cookie_refresh) {
                if (sync_cookie_isvalid(client_cookie, session_cookie)) {
                    rc = sync_refresh_update_content(pb, client_cookie, session_cookie);
                    if (rc == 0) {
                        entries_sent = 1;
                    }
                    if (sync_persist) {
                        rc = sync_intermediate_msg(pb, LDAP_TAG_SYNC_REFRESH_DELETE, session_cookie, NULL);
                    } else {
                        rc = sync_result_msg(pb, session_cookie);
                    }
                } else {
                    rc = E_SYNC_REFRESH_REQUIRED;
                    sync_result_err(pb, rc, "Invalid session cookie");
                }
            } else {
                rc = sync_refresh_initial_content(pb, sync_persist, tid, session_cookie);
                if (rc == 0 && !sync_persist) {
                    /* maintained in postop code */
                    session_cookie = NULL;
                }
                /* if persis it will be handed over to persist code */
            }

            if (rc) {
                if (sync_persist) {
                    sync_persist_terminate(tid);
                }
                goto error_return;
            } else if (sync_persist) {
                Slapi_Operation *operation;

                slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
                if (client_cookie) {
                    rc = sync_persist_startup(tid, session_cookie);
                }
                if (rc == 0) {
                    session_cookie = NULL; /* maintained in persist code */
                    slapi_operation_set_flag(operation, OP_FLAG_SYNC_PERSIST);
                }
            }


        } else {
            /* unknown mode, return an error */
            rc = 1;
        }
    error_return:
        sync_cookie_free(&client_cookie);
        sync_cookie_free(&session_cookie);
        slapi_ch_free((void **)&cookie);
    }

    /* if we sent the entries
     * return "error" to abort normal search
     */
    if (entries_sent > 0) {
        return (1);
    } else {
        return (rc);
    }
}

int
sync_srch_refresh_post_search(Slapi_PBlock *pb)
{
    int rc = 0;
    SyncOpInfo *info = sync_get_operation_extension(pb);

    if (!info) {
        return (0); /* nothing to do */
    }
    if (info->send_flag & SYNC_FLAG_SEND_INTERMEDIATE) {
        rc = sync_intermediate_msg(pb, LDAP_TAG_SYNC_REFRESH_DELETE, info->cookie, NULL);
        /* the refresh phase is over, now the post op
         * plugins will create the state control
         * depending on the operation type, reset flag
         */
        info->send_flag &= ~SYNC_FLAG_ADD_STATE_CTRL;
        /* activate the persistent phase thread*/
        sync_persist_startup(info->tid, info->cookie);
    }
    if (info->send_flag & SYNC_FLAG_ADD_DONE_CTRL) {
        LDAPControl **ctrl = (LDAPControl **)slapi_ch_calloc(2, sizeof(LDAPControl *));
        char *cookiestr = sync_cookie2str(info->cookie);
        /*
         * RFC4533
         *   If refreshDeletes of syncDoneValue is FALSE, the new copy includes
         *   all changed entries returned by the reissued Sync Operation, as well
         *   as all unchanged entries identified as being present by the reissued
         *   Sync Operation, but whose content is provided by the previous Sync
         *   Operation.  The unchanged entries not identified as being present are
         *   deleted from the client content.  They had been either deleted,
         *   moved, or otherwise scoped-out from the content.
         *
         *   If refreshDeletes of syncDoneValue is TRUE, the new copy includes all
         *   changed entries returned by the reissued Sync Operation, as well as
         *   all other entries of the previous copy except for those that are
         *   identified as having been deleted from the content.
         *
         * Confused yet? Don't worry so am I. I have no idea what this means or
         * what it will do. The best I can see from wireshark is that if refDel is
         * false, then anything *not* present will be purged from the change that
         * was supplied. Which probably says a lot about how confusing syncrepl is
         * that we've hardcoded this to false for literally years and no one has
         * complained, probably because every client is broken in their own ways
         * as no one can actually interpret that dense statement above.
         *
         * Point is, if we set refresh to true for openldap mode, it works, and if
         * it's false, the moment we send a single intermediate delete message, we
         * delete literally everything 🔥.
         */
        if (info->cookie->openldap_compat) {
            sync_create_sync_done_control(&ctrl[0], 1, cookiestr);
        } else {
            sync_create_sync_done_control(&ctrl[0], 0, cookiestr);
        }
        slapi_pblock_set(pb, SLAPI_RESCONTROLS, ctrl);
        slapi_ch_free((void **)&cookiestr);
    }
    return (rc);
}

int
sync_srch_refresh_pre_entry(Slapi_PBlock *pb)
{
    int rc = 0;
    SyncOpInfo *info = sync_get_operation_extension(pb);

    if (!info) {
        rc = 0; /* nothing to do */
    } else if (info->send_flag & SYNC_FLAG_ADD_STATE_CTRL) {
        Slapi_Entry *e;
        slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &e);
        LDAPControl **ctrl = (LDAPControl **)slapi_ch_calloc(2, sizeof(LDAPControl *));
        sync_create_state_control(e, &ctrl[0], LDAP_SYNC_ADD, NULL);
        slapi_pblock_set(pb, SLAPI_SEARCH_CTRLS, ctrl);
    }
    return (rc);
}

int
sync_srch_refresh_pre_result(Slapi_PBlock *pb)
{
    SyncOpInfo *info = sync_get_operation_extension(pb);

    if (!info) {
        return 0; /* nothing to do */
    }
    if (info->send_flag & SYNC_FLAG_NO_RESULT) {
        return (1);
    } else {
        return (0);
    }
}

static void
sync_free_update_nodes(Sync_UpdateNode **updates, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if ((*updates)[i].upd_uuid)
            slapi_ch_free((void **)&((*updates)[i].upd_uuid));
        if ((*updates)[i].upd_e)
            slapi_entry_free((*updates)[i].upd_e);
    }
    slapi_ch_free((void **)updates);
}

int
sync_refresh_update_content(Slapi_PBlock *pb, Sync_Cookie *client_cookie, Sync_Cookie *server_cookie)
{
    Slapi_PBlock *seq_pb;
    char *filter;
    Sync_CallBackData cb_data;
    int rc = LDAP_SUCCESS;
    PR_ASSERT(client_cookie);

    /*
     * We have nothing to send, move along.
     * Should be caught by cookie is valid though if the server < client, but if
     * they are equal, we return.
     */
    PR_ASSERT(server_cookie->cookie_change_info >= client_cookie->cookie_change_info);
    if (server_cookie->cookie_change_info == client_cookie->cookie_change_info) {
        return rc;
    }

    int chg_count = (server_cookie->cookie_change_info - client_cookie->cookie_change_info) + 1;
    PR_ASSERT(chg_count > 0);

    cb_data.cb_updates = (Sync_UpdateNode *)slapi_ch_calloc(chg_count, sizeof(Sync_UpdateNode));

    seq_pb = slapi_pblock_new();
    slapi_pblock_init(seq_pb);

    cb_data.orig_pb = pb;
    cb_data.change_start = client_cookie->cookie_change_info;

    /*
     * The client has already seen up to AND including change_info, so this should
     * should reflect that. originally was:
     *
     *  filter = slapi_ch_smprintf("(&(changenumber>=%lu)(changenumber<=%lu))",
     *                             client_cookie->cookie_change_info,
     *                             server_cookie->cookie_change_info);
     *
     * which would create a situation where if the previous cn was say 5, and the next
     * is 6, we'd get both 5 and 6, even though the client has already seen 5. But worse
     * if 5 was an "add" of the entry, and 6 was a "delete" of the same entry then sync
     * would over-optimise and remove the sync value because it things the add/delete was
     * in the same operation so we'd never send it. But the client HAD seen the add, and
     * now we'd never send the delete so this would be a bug. This created some confusion
     * for me in the tests, but the sync repl tests now correctly work and reflect the behaviour
     * expected.
     */
    filter = slapi_ch_smprintf("(&(changenumber>=%lu)(changenumber<=%lu))",
                               client_cookie->cookie_change_info + 1,
                               server_cookie->cookie_change_info);
    slapi_search_internal_set_pb(
        seq_pb,
        CL_SRCH_BASE,
        LDAP_SCOPE_ONE,
        filter,
        NULL,
        0,
        NULL, NULL,
        plugin_get_default_component_id(),
        0);

    rc = slapi_search_internal_callback_pb(
        seq_pb, &cb_data, NULL, sync_read_entry_from_changelog, NULL);
    slapi_pblock_destroy(seq_pb);

    /* Now send the deleted entries in a sync info message
     * and the modified entries as single entries
     */
    sync_send_deleted_entries(pb, cb_data.cb_updates, chg_count, server_cookie);
    sync_send_modified_entries(pb, cb_data.cb_updates, chg_count);

    sync_free_update_nodes(&cb_data.cb_updates, chg_count);
    slapi_ch_free((void **)&filter);
    return (rc);
}

int
sync_refresh_initial_content(Slapi_PBlock *pb, int sync_persist, PRThread *tid, Sync_Cookie *sc)
{
    /* the entries will be sent in the normal search process, but
     * - a control has to be sent with each entry
     *   if sync persist:
     *   - an intermediate response has to be sent
     *   - no result message must be sent
     *
     *   else
     *   - a result message with a sync done control has to be sent
     *
     *   setup on operation extension to take care of in
     *   pre_entry, pre_result and post_search plugins
     */
    SyncOpInfo *info;

    if (sync_persist) {
        info = new_SyncOpInfo(SYNC_FLAG_ADD_STATE_CTRL |
                                  SYNC_FLAG_SEND_INTERMEDIATE |
                                  SYNC_FLAG_NO_RESULT,
                              tid,
                              sc);
    } else {
        info = new_SyncOpInfo(SYNC_FLAG_ADD_STATE_CTRL |
                                  SYNC_FLAG_ADD_DONE_CTRL,
                              tid,
                              sc);
    }
    sync_set_operation_extension(pb, info);

    return (0);
}

static int
sync_str2chgreq(char *chgtype)
{
    if (chgtype == NULL) {
        return (-1);
    }
    if (strcasecmp(chgtype, "add") == 0) {
        return (LDAP_REQ_ADD);
    } else if (strcasecmp(chgtype, "modify") == 0) {
        return (LDAP_REQ_MODIFY);
    } else if (strcasecmp(chgtype, "modrdn") == 0) {
        return (LDAP_REQ_MODRDN);
    } else if (strcasecmp(chgtype, "delete") == 0) {
        return (LDAP_REQ_DELETE);
    } else {
        return (-1);
    }
}

static char *
sync_get_attr_value_from_entry(Slapi_Entry *cl_entry, char *attrtype)
{
    Slapi_Value *sval = NULL;
    const struct berval *value;
    char *strvalue = NULL;
    if (NULL != cl_entry) {
        Slapi_Attr *chattr = NULL;
        sval = NULL;
        value = NULL;
        if (slapi_entry_attr_find(cl_entry, attrtype, &chattr) == 0) {
            slapi_attr_first_value(chattr, &sval);
            if (NULL != sval) {
                value = slapi_value_get_berval(sval);
                if (NULL != value && NULL != value->bv_val &&
                    '\0' != value->bv_val[0]) {
                    strvalue = slapi_ch_strdup(value->bv_val);
                }
            }
        }
    }
    return (strvalue);
}

static int
sync_find_ref_by_uuid(Sync_UpdateNode *updates, int stop, char *uniqueid)
{
    int rc = -1;
    int i;
    for (i = 0; i < stop; i++) {
        if (updates[i].upd_uuid && (0 == strcmp(uniqueid, updates[i].upd_uuid))) {
            rc = i;
            break;
        }
    }
    return (rc);
}

static int
sync_is_entry_in_scope(Slapi_PBlock *pb, Slapi_Entry *db_entry)
{
    Slapi_Filter *origfilter;
    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &origfilter);
    if (db_entry &&
        sync_is_active(db_entry, pb) &&
        (slapi_vattr_filter_test(pb, db_entry, origfilter, 1) == 0)) {
        return (1);
    } else {
        return (0);
    }
}

Slapi_Entry *
sync_deleted_entry_from_changelog(Slapi_Entry *cl_entry)
{
    Slapi_Entry *db_entry = NULL;
    char *entrydn = NULL;
    char *uniqueid = NULL;

    entrydn = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_ENTRYDN);
    uniqueid = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_UNIQUEID);

    /* when the Retro CL can provide the deleted entry
     * the entry will be taken from th RCL.
     * For now. just create an entry to holde the nsuniqueid
     */
    db_entry = slapi_entry_alloc();
    slapi_entry_init(db_entry, entrydn, NULL);
    slapi_entry_add_string(db_entry, "nsuniqueid", uniqueid);
    slapi_ch_free((void **)&uniqueid);

    return (db_entry);
}

int
sync_read_entry_from_changelog(Slapi_Entry *cl_entry, void *cb_data)
{
    char *uniqueid = NULL;
    char *chgtype = NULL;
    char *chgnr = NULL;
    int chg_req;
    int prev = 0;
    int index = 0;
    unsigned long chgnum = 0;
    Sync_CallBackData *cb = (Sync_CallBackData *)cb_data;

    if (cb == NULL) {
        return (1);
    }

    uniqueid = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_UNIQUEID);
    if (uniqueid == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_read_entry_from_changelog - Retro Changelog does not provide nsuniquedid."
                      "Check RCL plugin configuration.\n");
        return (1);
    }
    chgnr = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_CHANGENUMBER);
    chgnum = sync_number2ulong(chgnr);
    if (SYNC_INVALID_CHANGENUM == chgnum) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_read_entry_from_changelog - Change number provided by Retro Changelog is invalid: %s\n", chgnr);
        slapi_ch_free_string(&chgnr);
        slapi_ch_free_string(&uniqueid);
        return (1);
    }
    if (chgnum < cb->change_start) {
        slapi_log_err(SLAPI_LOG_ERR, SYNC_PLUGIN_SUBSYSTEM,
                      "sync_read_entry_from_changelog - "
                      "Change number provided by Retro Changelog %s is less than the initial number %lu\n",
                      chgnr, cb->change_start);
        slapi_ch_free_string(&chgnr);
        slapi_ch_free_string(&uniqueid);
        return (1);
    }
    index = chgnum - cb->change_start;
    chgtype = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_CHGTYPE);
    chg_req = sync_str2chgreq(chgtype);
    switch (chg_req) {
    case LDAP_REQ_ADD:
        /* nsuniqueid cannot exist, just add reference */
        cb->cb_updates[index].upd_chgtype = LDAP_REQ_ADD;
        cb->cb_updates[index].upd_uuid = uniqueid;
        break;
    case LDAP_REQ_MODIFY:
        /* check if we have seen this uuid already */
        prev = sync_find_ref_by_uuid(cb->cb_updates, index, uniqueid);
        if (prev == -1) {
            cb->cb_updates[index].upd_chgtype = LDAP_REQ_MODIFY;
            cb->cb_updates[index].upd_uuid = uniqueid;
        } else {
            /* was add or mod, keep it */
            cb->cb_updates[index].upd_uuid = 0;
            cb->cb_updates[index].upd_chgtype = 0;
            slapi_ch_free_string(&uniqueid);
        }
        break;
    case LDAP_REQ_MODRDN: {
        /* if it is a modrdn, we finally need to decide if this will
                 * trigger a present or delete state, keep the info that
                 * the entry was subject to a modrdn
                 */
        int new_scope = 0;
        int old_scope = 0;
        Slapi_DN *original_dn;
        char *newsuperior = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_NEWSUPERIOR);
        char *entrydn = sync_get_attr_value_from_entry(cl_entry, CL_ATTR_ENTRYDN);
        /* if newsuperior is set we need to checkif the entry has been moved into
             * or moved out of the scope of the synchronization request
             */
        original_dn = slapi_sdn_new_dn_byref(entrydn);
        old_scope = sync_is_active_scope(original_dn, cb->orig_pb);
        slapi_sdn_free(&original_dn);
        slapi_ch_free_string(&entrydn);
        if (newsuperior) {
            Slapi_DN *newbase;
            newbase = slapi_sdn_new_dn_byref(newsuperior);
            new_scope = sync_is_active_scope(newbase, cb->orig_pb);
            slapi_ch_free_string(&newsuperior);
            slapi_sdn_free(&newbase);
        } else {
            /* scope didn't change */
            new_scope = old_scope;
        }
        prev = sync_find_ref_by_uuid(cb->cb_updates, index, uniqueid);
        if (old_scope && new_scope) {
            /* nothing changed, it's just a MOD */
            if (prev == -1) {
                cb->cb_updates[index].upd_chgtype = LDAP_REQ_MODIFY;
                cb->cb_updates[index].upd_uuid = uniqueid;
            } else {
                cb->cb_updates[index].upd_uuid = 0;
                cb->cb_updates[index].upd_chgtype = 0;
                slapi_ch_free_string(&uniqueid);
            }
        } else if (old_scope) {
            /* it was moved out of scope, handle as DEL */
            if (prev == -1) {
                cb->cb_updates[index].upd_chgtype = LDAP_REQ_DELETE;
                cb->cb_updates[index].upd_uuid = uniqueid;
                cb->cb_updates[index].upd_e = sync_deleted_entry_from_changelog(cl_entry);
            } else {
                cb->cb_updates[prev].upd_chgtype = LDAP_REQ_DELETE;
                cb->cb_updates[prev].upd_e = sync_deleted_entry_from_changelog(cl_entry);
                slapi_ch_free_string(&uniqueid);
            }
        } else if (new_scope) {
            /* moved into scope, handle as ADD */
            cb->cb_updates[index].upd_chgtype = LDAP_REQ_ADD;
            cb->cb_updates[index].upd_uuid = uniqueid;
        } else {
            /* nothing to do */
            slapi_ch_free_string(&uniqueid);
        }
        slapi_sdn_free(&original_dn);
        break;
    }
    case LDAP_REQ_DELETE:
        /* check if we have seen this uuid already */
        prev = sync_find_ref_by_uuid(cb->cb_updates, index, uniqueid);
        if (prev == -1) {
            cb->cb_updates[index].upd_chgtype = LDAP_REQ_DELETE;
            cb->cb_updates[index].upd_uuid = uniqueid;
            cb->cb_updates[index].upd_e = sync_deleted_entry_from_changelog(cl_entry);
        } else {
            /* if it was added since last cookie state, we
                 * can ignoere it */
            if (cb->cb_updates[prev].upd_chgtype == LDAP_REQ_ADD) {
                slapi_ch_free_string(&(cb->cb_updates[prev].upd_uuid));
                cb->cb_updates[prev].upd_uuid = NULL;
                cb->cb_updates[index].upd_uuid = NULL;
            } else {
                /* ignore previous mod */
                cb->cb_updates[index].upd_uuid = NULL;
                cb->cb_updates[prev].upd_chgtype = LDAP_REQ_DELETE;
                cb->cb_updates[prev].upd_e = sync_deleted_entry_from_changelog(cl_entry);
            }
            slapi_ch_free_string(&uniqueid);
        }
        break;
    default:
        slapi_ch_free_string(&uniqueid);
    }
    slapi_ch_free_string(&chgtype);
    slapi_ch_free_string(&chgnr);

    return (0);
}
#define SYNC_MAX_DELETED_UUID_BATCH 50

void
sync_send_deleted_entries(Slapi_PBlock *pb, Sync_UpdateNode *upd, int chg_count, Sync_Cookie *cookie)
{
    char *syncUUIDs[SYNC_MAX_DELETED_UUID_BATCH + 1] = {0};
    size_t uuid_index = 0;

    syncUUIDs[0] = NULL;
    for (size_t index = 0; index < chg_count; index++) {
        if (upd[index].upd_chgtype == LDAP_REQ_DELETE &&
            upd[index].upd_uuid) {
            if (uuid_index < SYNC_MAX_DELETED_UUID_BATCH) {
                syncUUIDs[uuid_index] = sync_nsuniqueid2uuid(upd[index].upd_uuid);
                uuid_index++;
            } else {
                /* max number of uuids to be sent in one sync info message */
                syncUUIDs[uuid_index] = NULL;
                sync_intermediate_msg(pb, LDAP_TAG_SYNC_ID_SET, cookie, (char **)&syncUUIDs);
                for (size_t i = 0; i < uuid_index; i++) {
                    slapi_ch_free((void **)&syncUUIDs[i]);
                    syncUUIDs[i] = NULL;
                }
                uuid_index = 0;
            }
        }
    }

    if (uuid_index > 0 && syncUUIDs[uuid_index - 1]) {
        /* more entries to send */
        syncUUIDs[uuid_index] = NULL;
        sync_intermediate_msg(pb, LDAP_TAG_SYNC_ID_SET, cookie, (char **)&syncUUIDs);
        for (size_t i = 0; i < uuid_index; i++) {
            slapi_ch_free((void **)&syncUUIDs[i]);
            syncUUIDs[i] = NULL;
        }
    }
}

void
sync_send_modified_entries(Slapi_PBlock *pb, Sync_UpdateNode *upd, int chg_count)
{
    int index;

    for (index = 0; index < chg_count; index++) {
        if (upd[index].upd_chgtype != LDAP_REQ_DELETE &&
            upd[index].upd_uuid)

            sync_send_entry_from_changelog(pb, upd[index].upd_chgtype, upd[index].upd_uuid);
    }
}

int
sync_send_entry_from_changelog(Slapi_PBlock *pb, int chg_req __attribute__((unused)), char *uniqueid)
{
    Slapi_Entry *db_entry = NULL;
    int chg_type = LDAP_SYNC_ADD;
    int rv;
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    char *origbase;
    char *filter = slapi_ch_smprintf("(nsuniqueid=%s)", uniqueid);

    slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &origbase);
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, origbase,
                                 LDAP_SCOPE_SUBTREE, filter,
                                 NULL, 0, NULL, NULL, plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rv);
    if (rv == LDAP_SUCCESS) {
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries)
            db_entry = *entries; /* there can only be one */
    }

    if (db_entry && sync_is_entry_in_scope(pb, db_entry)) {
        LDAPControl **ctrl = (LDAPControl **)slapi_ch_calloc(2, sizeof(LDAPControl *));
        sync_create_state_control(db_entry, &ctrl[0], chg_type, NULL);
        slapi_send_ldap_search_entry(pb, db_entry, ctrl, NULL, 0);
        ldap_controls_free(ctrl);
    }
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_ch_free((void **)&filter);
    return (0);
}

static SyncOpInfo *
new_SyncOpInfo(int flag, PRThread *tid, Sync_Cookie *cookie)
{
    SyncOpInfo *spec = (SyncOpInfo *)slapi_ch_calloc(1, sizeof(SyncOpInfo));
    spec->send_flag = flag;
    spec->cookie = cookie;
    spec->tid = tid;

    return spec;
}
/* consumer operation extension constructor */
static void *
sync_operation_extension_ctor(void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    /* we only set the extension value explicitly if the
       client requested the control - see deref_pre_search */
    return NULL; /* we don't set anything in the ctor */
}

/* consumer operation extension destructor */
static void
sync_delete_SyncOpInfo(SyncOpInfo **info)
{
    if (info && *info) {
        sync_cookie_free(&((*info)->cookie));
        slapi_ch_free((void **)info);
    }
}

static void
sync_operation_extension_dtor(void *ext, void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    SyncOpInfo *spec = (SyncOpInfo *)ext;
    sync_delete_SyncOpInfo(&spec);
}

static SyncOpInfo *
sync_get_operation_extension(Slapi_PBlock *pb)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    return (SyncOpInfo *)slapi_get_object_extension(sync_extension_type,
                                                    op, sync_extension_handle);
}

static void
sync_set_operation_extension(Slapi_PBlock *pb, SyncOpInfo *spec)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_set_object_extension(sync_extension_type, op,
                               sync_extension_handle, (void *)spec);
}

int
sync_register_operation_extension(void)
{
    return slapi_register_object_extension(SYNC_PLUGIN_SUBSYSTEM,
                                           SLAPI_EXT_OPERATION,
                                           sync_operation_extension_ctor,
                                           sync_operation_extension_dtor,
                                           &sync_extension_type,
                                           &sync_extension_handle);
}

int
sync_unregister_operation_entension(void)
{
    int rc = slapi_unregister_object_extension(SYNC_PLUGIN_SUBSYSTEM,
                                               SLAPI_EXT_OPERATION,
                                               &sync_extension_type,
                                               &sync_extension_handle);

    return rc;
}
