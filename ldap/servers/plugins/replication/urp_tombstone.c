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


/*
 * urp_tombstone.c - Update Resolution Procedures - Tombstones
 */

#include "slapi-plugin.h"
#include "repl5.h"
#include "urp.h"

extern int slapi_log_urp;

/*
 * Check if the entry is a tombstone.
 */
int
is_tombstone_entry(const Slapi_Entry *entry)
{
    int flag;

    /* LP: This doesn't work very well with entries that we tombstone ourself */
    flag = slapi_entry_flag_is_set(entry, SLAPI_ENTRY_FLAG_TOMBSTONE);
    if (flag == 0) {
        /* This is slow */
        flag = slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE);
    }
    return flag;
}

PRBool
get_tombstone_csn(const Slapi_Entry *entry, const CSN **delcsn)
{
    PRBool ists = PR_FALSE;
    if (is_tombstone_entry(entry)) {
        ists = PR_TRUE;
        *delcsn = entry_get_deletion_csn((Slapi_Entry *)entry); /* cast away const */
    }

    return ists;
}

static Slapi_DN *
get_valid_parent_for_conflict(Slapi_Entry *entry)
{
    const char *replconflict = slapi_entry_attr_get_ref(entry, ATTR_NSDS5_REPLCONFLICT);
    char *validdn = NULL;
    Slapi_DN *valid_DN = NULL;

    if (replconflict) {
        validdn = strstr(replconflict, " (ADD) ");
        if (validdn) {
            validdn += 7;
            valid_DN = slapi_sdn_new_dn_byval(validdn);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "get_valid_parent_for_conflict - valid entry dn: %s\n",
                          validdn);
        }
    }
    return valid_DN;
}

int
tombstone_to_conflict_check_parent(
    char *sessionid,
    char *parentdn,
    const char *uniqueid,
    const char *parentuniqueid,
    CSN *opcsn,
    Slapi_DN *conflictdn)
{
    int rc = 0;
    int op_result;
    Slapi_PBlock *newpb;

    if (parentdn == 0) {
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "tombstone_to_conflict_check_parent - no parent entry for: %s\n",
                      slapi_sdn_get_ndn(conflictdn));
        return rc;
    } else {
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "tombstone_to_conflict_check_parent - checking parent entry: %s\n",
                      parentdn);
    }
    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(
        newpb,
        parentdn,
        LDAP_SCOPE_BASE,
        "objectclass=*",
        NULL,           /*attrs*/
        0,              /*attrsonly*/
        NULL,           /*Controls*/
        parentuniqueid, /*uniqueid*/
        repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
        0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    switch (op_result) {
    case LDAP_SUCCESS: {
        Slapi_Entry **entries = NULL;
        slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries != NULL && entries[0] != NULL) {
            if (is_conflict_entry(entries[0])) {
                Slapi_DN *newsuperior = get_valid_parent_for_conflict(entries[0]);
                Slapi_RDN *rdn = slapi_rdn_new();
                slapi_sdn_get_rdn(conflictdn, rdn);
                const char *newrdn = slapi_rdn_get_rdn(rdn);
                op_result = urp_fixup_modrdn_entry(
                    conflictdn,
                    newrdn,
                    newsuperior,
                    uniqueid,
                    NULL,
                    NULL,
                    OP_FLAG_NOOP);
                slapi_rdn_free(&rdn);
                slapi_sdn_free(&newsuperior);
            }
        }
    } break;
    default:
        break;
    }

    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    return rc;
}

static int
tombstone_to_glue_resolve_parent(
    Slapi_PBlock *pb,
    char *sessionid,
    const Slapi_DN *parentdn,
    const char *parentuniqueid,
    CSN *opcsn,
    Slapi_DN **newparentdn)
{
    /* Let's have a look at the parent of this entry... */
    if (!slapi_sdn_isempty(parentdn) && parentuniqueid != NULL) {
        int op_result;
        Slapi_PBlock *newpb = slapi_pblock_new();
        slapi_search_internal_set_pb(
            newpb,
            slapi_sdn_get_dn(parentdn), /* JCM - This DN just identifies the backend to be searched. */
            LDAP_SCOPE_BASE,
            "objectclass=*",
            NULL,           /*attrs*/
            0,              /*attrsonly*/
            NULL,           /*Controls*/
            parentuniqueid, /*uniqueid*/
            repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
            0);
        slapi_search_internal_pb(newpb);
        slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
        switch (op_result) {
        case LDAP_SUCCESS: {
            Slapi_Entry **entries = NULL;
            /* OK, the tombstone entry parent exists. Is it also a tombstone? */
            slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if (entries != NULL && entries[0] != NULL) {
                if (is_tombstone_entry(entries[0])) {
                    tombstone_to_glue(pb, sessionid, entries[0], parentdn,
                                      REASON_RESURRECT_ENTRY, opcsn, newparentdn);
                }
            } else {
                /* JCM - Couldn't find the entry! */
            }
        } break;
        default:
            /* So, the tombstone entry had a parent... but it's gone. */
            /* That's probably a bad thing. */
            break;
        }
        slapi_free_search_results_internal(newpb);
        slapi_pblock_destroy(newpb);
    }
    return 0;
}

/*
 * transform a conflict entry to a tombstone of the original entry
 * 1. rename the conflict to the original entry
 * 2. remove conflict attrs
 * 3. delete it
 */

int
conflict_to_tombstone(char *sessionid, Slapi_Entry *entry, CSN *opcsn)
{
    int op_result = 0;
    Slapi_RDN *srdn = slapi_rdn_new();
    const char *uniqueid = slapi_entry_get_uniqueid(entry);
    const char *newrdn = NULL;
    char *conflictdn = NULL;
    const char *replconflict = slapi_entry_attr_get_ref(entry, ATTR_NSDS5_REPLCONFLICT);

    if (replconflict) {
        conflictdn = strstr(replconflict, " (ADD) ");
        if (conflictdn == NULL) {
            /* error, wrong type of conflict */
            op_result = 1;
        } else {
            conflictdn += 7;
            slapi_rdn_init_all_dn(srdn, conflictdn);
            newrdn = slapi_rdn_get_nrdn(srdn);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "conflict_to_tombstone - %s - valid entry dn: %s newrdn: %s\n",
                          sessionid, conflictdn, newrdn);
        }
    }

    if (op_result)
        goto done;

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "conflict_to_tombstone - %s - Renaming entry %s to %s\n",
                  sessionid, slapi_entry_get_dn_const(entry), newrdn);
    op_result = urp_fixup_rename_entry(entry, newrdn, NULL, OP_FLAG_NOOP);
    if (op_result)
        goto done;

    op_result = urp_fixup_delete_entry(uniqueid, slapi_entry_get_dn_const(entry), opcsn, 0);

done:
    slapi_rdn_free(&srdn);
    return op_result;
}

/*
 * Convert a tombstone into a conflict entry.
 */
int
tombstone_to_conflict(
    char *sessionid,
    Slapi_Entry *tombstoneentry,
    const Slapi_DN *conflictdn,
    const char *reason,
    CSN *opcsn,
    Slapi_DN **newparentdn)
{
    int op_result = 0;
    Slapi_Mods smods;
    char csnstr[CSN_STRSIZE + 1];

    char *uniqueid = slapi_entry_attr_get_charptr(tombstoneentry, "nsuiqueid");
    const char *entrydn = slapi_entry_attr_get_ref(tombstoneentry, "nscpentrydn");
    char *parentdn = slapi_dn_parent(slapi_sdn_get_ndn(conflictdn));
    const CSN *dncsn = entry_get_dncsn(tombstoneentry);
    char *buf = slapi_ch_smprintf("%s (%s) %s", REASON_ANNOTATE_DN, "ADD", entrydn);
    csn_as_string(dncsn, PR_FALSE, csnstr);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "tombstone_to_conflict - %s - trying to resurrect tombstone as '%s'.\n",
                  sessionid, slapi_sdn_get_ndn(conflictdn));
    slapi_sdn_set_normdn_byval(slapi_entry_get_sdn(tombstoneentry), slapi_sdn_get_ndn(conflictdn));
    /* not just e_sdn, e_rsdn needs to be updated. */
    slapi_rdn_set_all_dn(slapi_entry_get_srdn(tombstoneentry), slapi_sdn_get_ndn(conflictdn));
    /* Warning: urp_fixup_add_entry consumes tombstoneentry (including entrydn) */
    op_result = urp_fixup_add_entry(tombstoneentry, NULL, NULL, opcsn, OP_FLAG_RESURECT_ENTRY | OP_FLAG_NOOP);

    if (op_result) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "tombstone_to_conflict - %s - Can't resurrect tombstone, error=%d\n",
                      sessionid, op_result);
        goto done;
    }

    slapi_mods_init(&smods, 5);
    slapi_mods_add(&smods, LDAP_MOD_ADD, "objectclass", strlen("ldapsubentry"), "ldapsubentry");
    slapi_mods_add(&smods, LDAP_MOD_DELETE, "objectclass", strlen("glue"), "glue");
    slapi_mods_add(&smods, LDAP_MOD_REPLACE, "conflictcsn", strlen(csnstr), csnstr);
    slapi_mods_add(&smods, LDAP_MOD_ADD, ATTR_NSDS5_REPLCONFLICT, strlen(buf), buf);
    op_result = urp_fixup_modify_entry(uniqueid, conflictdn, opcsn, &smods, 0);
    slapi_mods_done(&smods);
    if (op_result == LDAP_TYPE_OR_VALUE_EXISTS) {
        /* the objectclass was already present */
        op_result = LDAP_SUCCESS;
    }

done:
    slapi_ch_free_string(&buf);
    slapi_ch_free_string(&uniqueid);
    slapi_ch_free_string(&parentdn);
    return op_result;
}

/*
 * Convert a tombstone into a glue entry.
 */
int
tombstone_to_glue(
    Slapi_PBlock *pb,
    char *sessionid,
    Slapi_Entry *tombstoneentry,
    const Slapi_DN *gluedn, /* does not start with uniqueid= */
    const char *reason,
    CSN *opcsn,
    Slapi_DN **newparentdn)
{
    Slapi_DN *parentdn;
    const char *parentuniqueid;
    const char *tombstoneuniqueid;
    Slapi_Entry *addingentry = NULL;
    Slapi_Entry *addingentry_bakup = NULL;
    const char *addingdn;
    int op_result;
    int rdn_is_conflict = 0;

    /* JCMREPL
     * Nothing logged to the 5.0 Change Log
     * Add is logged to the 4.0 Change Log - Core server Add code
     * must attach the entry to the Operation
     */

    /* Resurrect the parent entry first */

    /* JCM - This DN calculation is odd. It could resolve to NULL
     * which won't help us identify the correct backend to search.
     */
    is_suffix_dn_ext(pb, gluedn, &parentdn, 1 /* is_tombstone */);
    parentuniqueid = slapi_entry_attr_get_ref(tombstoneentry, SLAPI_ATTR_VALUE_PARENT_UNIQUEID);
    tombstone_to_glue_resolve_parent(pb, sessionid, parentdn, parentuniqueid, opcsn, newparentdn);

    /* Submit an Add operation to turn the tombstone entry into glue. */
    /*
     * The tombstone is stored with an invalid DN, we must fix this.
     */
    addingentry = slapi_entry_dup(tombstoneentry);

    if (newparentdn && *newparentdn && slapi_sdn_compare(parentdn, *newparentdn)) {
        /* If the parents are resolved, the tombstone's DN is going to be different... */
        /* Update DN in addingentry */
        Slapi_RDN *rdn = slapi_rdn_new();
        slapi_rdn_set_dn_ext(rdn, slapi_sdn_get_dn(gluedn), SLAPI_RDN_SET_DN_INCLUDE_UNIQUEID);
        addingdn = slapi_moddn_get_newdn(slapi_entry_get_sdn(addingentry), slapi_rdn_get_rdn(rdn),
                                         slapi_sdn_get_dn(*newparentdn));
        slapi_rdn_free(&rdn);
        slapi_sdn_init_normdn_passin(*newparentdn, addingdn); /* to return the new parentdn to the caller */
    } else {
        slapi_sdn_free(newparentdn); /* no change in parentdn; returning NULL */
        addingdn = slapi_sdn_get_dn(gluedn);
    }
    slapi_sdn_set_normdn_byval(slapi_entry_get_sdn(addingentry), addingdn);
    /* not just e_sdn, e_rsdn needs to be updated. */
    slapi_rdn_set_all_dn(slapi_entry_get_srdn(addingentry), slapi_entry_get_dn_const(addingentry));
    rdn_is_conflict = slapi_rdn_is_conflict(slapi_entry_get_srdn(addingentry));

    if (!slapi_entry_attr_hasvalue(addingentry, ATTR_NSDS5_REPLCONFLICT, reason)) {
        /* Add the reason of turning it to glue - The backend code will use it*/
        slapi_entry_add_string(addingentry, ATTR_NSDS5_REPLCONFLICT, reason);
    }
    tombstoneuniqueid = slapi_entry_get_uniqueid(tombstoneentry);
    /*
     * addingentry and parentuniqueid are consumed in urp_fixup_add_entry,
     * regardless of the result.
     * Note: addingentry is not really consumed in ldbm_back_add.
     * tombstoneentry from DB/entry cache is duplicated and turned to be a glue.
     * This addingentry is freed in op_shared_add.
     */
    addingentry_bakup = slapi_entry_dup(addingentry);
    op_result = urp_fixup_add_entry(addingentry, tombstoneuniqueid, slapi_ch_strdup(parentuniqueid), opcsn, OP_FLAG_RESURECT_ENTRY);
    if ((LDAP_ALREADY_EXISTS == op_result) && !rdn_is_conflict) {
        /* conflict -- there's already the same named entry added.
         * But this to-be-glued entry needs to be added since this is a parent of child entries...
         * So, rename this tombstone parententry a conflict, glue entry.
         * Instead of "fixup_add", we have to "fixup_rename"...
         * */
        char *conflictrdn = get_rdn_plus_uniqueid(sessionid, addingdn, tombstoneuniqueid);
        if (conflictrdn) {
            addingentry = addingentry_bakup;
            addingentry_bakup = NULL;
            if (!slapi_entry_attr_hasvalue(addingentry, ATTR_NSDS5_REPLCONFLICT, reason)) {
                /* Add the reason of turning it to glue - The backend code will use it*/
                slapi_entry_add_string(addingentry, ATTR_NSDS5_REPLCONFLICT, reason);
            }
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "tombstone_to_glue - %s - Can't resurrect tombstone to glue reason '%s'. "
                          "Try with conflict dn %s, error=%d\n",
                          sessionid, reason, addingdn, op_result);
            op_result = urp_fixup_rename_entry(addingentry, (const char *)conflictrdn, parentuniqueid,
                                               OP_FLAG_RESURECT_ENTRY | OP_FLAG_TOMBSTONE_ENTRY);
            slapi_ch_free_string(&conflictrdn);
            slapi_entry_free(addingentry);
            addingentry = NULL;
        }
    }
    slapi_entry_free(addingentry_bakup);
    if (op_result == LDAP_SUCCESS) {
        slapi_log_err(/*slapi_log_urp*/ SLAPI_LOG_ERR, repl_plugin_name,
                      "tombstone_to_glue - %s - Resurrected tombstone %s to glue reason '%s'\n", sessionid, addingdn, reason);
    } else if (LDAP_ALREADY_EXISTS == op_result) {
        slapi_log_err(/*slapi_log_urp*/ SLAPI_LOG_ERR, repl_plugin_name,
                      "tombstone_to_glue - %s - No need to turn tombstone %s to glue; it was already resurrected.\n",
                      sessionid, addingdn);
        op_result = LDAP_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "tombstone_to_glue - %s - Can't resurrect tombstone %s to glue reason '%s', error=%d\n",
                      sessionid, addingdn, reason, op_result);
    }
    slapi_sdn_free(&parentdn);
    return op_result;
}

int
entry_to_tombstone(Slapi_PBlock *pb, Slapi_Entry *entry)
{
    Slapi_Operation *op;
    Slapi_Mods smods;
    CSN *opcsn;
    const char *uniqueid;
    int op_result = LDAP_SUCCESS;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    opcsn = operation_get_csn(op);
    uniqueid = slapi_entry_get_uniqueid(entry);


    slapi_mods_init(&smods, 2);
    /* Remove objectclass=glue */
    slapi_mods_add(&smods, LDAP_MOD_DELETE, SLAPI_ATTR_OBJECTCLASS, strlen("glue"), "glue");
    /* Remove any URP conflict since a tombstone shouldn't
     * be retrieved later for conflict removal.
     */
    slapi_mods_add(&smods, LDAP_MOD_DELETE, ATTR_NSDS5_REPLCONFLICT, 0, NULL);

    op_result = urp_fixup_modify_entry(uniqueid,
                                       slapi_entry_get_sdn_const(entry),
                                       opcsn, &smods, 0);
    slapi_mods_done(&smods);

    /*
     * Delete the entry.
     */
    if (op_result == LDAP_SUCCESS) {
        /*
         * Using internal delete operation since it would go
         * through the urp operations and trigger the recursive
         * fixup if applicable.
         */
        op_result = urp_fixup_delete_entry(uniqueid, slapi_entry_get_dn_const(entry), opcsn, 0);
    }

    return op_result;
}
