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


/*
 * urp.c - Update Resolution Procedures
 */

#include "slapi-plugin.h"
#include "repl5.h"
#include "urp.h"

extern int slapi_log_urp;

static int urp_add_resolve_parententry(Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, Slapi_Entry *parententry, CSN *opcsn);
static int urp_add_check_tombstone(Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, CSN *opcsn);
static int urp_delete_check_conflict(char *sessionid, Slapi_Entry *tombstone_entry, CSN *opcsn);
static int urp_add_new_entry_to_conflict(Slapi_PBlock *pb, char *sessionid, Slapi_Entry *addentry, CSN *opcsn);
static int urp_annotate_dn(char *sessionid, const Slapi_Entry *entry, CSN *opcsn, const char *optype, char **conflict_dn);
static int urp_conflict_to_glue(char *sessionid, const Slapi_Entry *entry, Slapi_DN *parentdn, CSN *opcsn);
static char *urp_find_tombstone_for_glue(Slapi_PBlock *pb, char *sessionid, const Slapi_Entry *entry, Slapi_DN *parentdn, CSN *opcsn);
static char *urp_find_valid_entry_to_delete(Slapi_PBlock *pb, const Slapi_Entry *deleteentry, char *sessionid, CSN *opcsn);
static int urp_naming_conflict_removal(Slapi_PBlock *pb, char *sessionid, CSN *opcsn, const char *optype);
static int mod_namingconflict_attr(const char *uniqueid, const Slapi_DN *entrysdn, const Slapi_DN *conflictsdn, CSN *opcsn, const char *optype);
static int mod_objectclass_attr(const char *uniqueid, const Slapi_DN *entrysdn, const Slapi_DN *conflictsdn, CSN *opcsn, const char *optype);
static int del_replconflict_attr(const Slapi_Entry *entry, CSN *opcsn, int opflags);
static char *get_dn_plus_uniqueid(char *sessionid,const Slapi_DN *oldsdn,const char *uniqueid);
static int is_suffix_entry(Slapi_PBlock *pb, Slapi_Entry *entry, Slapi_DN **parenddn);
static int is_renamed_entry(Slapi_PBlock *pb, Slapi_Entry *entry, CSN *opcsn);
static int urp_fixup_add_cenotaph(Slapi_PBlock *pb, char *sessionid, CSN *opcsn);
static int is_deleted_at_csn(const Slapi_Entry *entry, CSN *opcsn);
static char * urp_get_valid_parent_nsuniqueid(Slapi_DN *parentdn);
static int urp_rename_conflict_children(const char *old_parent, const Slapi_DN *new_parent);
static Slapi_Entry *urp_get_min_naming_conflict_entry(Slapi_PBlock *pb, const char *collision_dn, char *sessionid, CSN *opcsn);

/*
 * Return 0 for OK, -1 for Error.
 */
int
urp_modify_operation(Slapi_PBlock *pb)
{
    Slapi_Entry *modifyentry = NULL;
    int op_result = 0;
    int rc = 0; /* OK */
    char sessionid[REPL_SESSION_ID_SIZE];
    CSN *opcsn;

    if (slapi_op_abandoned(pb)) {
        return rc;
    }

    get_repl_session_id(pb, sessionid, &opcsn);
    slapi_pblock_get(pb, SLAPI_MODIFY_EXISTING_ENTRY, &modifyentry);
    if (modifyentry != NULL) {
        /*
         * The entry to be modified exists.
         * - the entry could be a tombstone... but that's OK.
         * - the entry could be glue... that may not be OK. JCMREPL
         */
        rc = 0;        /* OK, Modify the entry */
        PROFILE_POINT; /* Modify Conflict; Entry Exists; Apply Modification */
    } else {
        /*
         * The entry to be modified could not be found.
         */
        op_result = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_NOOP; /* Must discard this Modification */
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_modify_operation - No such entry\n");
        PROFILE_POINT; /* Modify Conflict; Entry Does Not Exist; Discard Modification */
    }
    return rc;
}

/*
 * Return 0 for OK,
 *       -1 for Ignore or Error depending on SLAPI_RESULT_CODE,
 *       >0 for action code
 * Action Code Bit 0: Fetch existing entry.
 * Action Code Bit 1: Fetch parent entry.
 * The function is called as a be pre-op on consumers.
 */
int
urp_add_operation(Slapi_PBlock *pb)
{
    Slapi_Entry *existing_uniqueid_entry;
    Slapi_Entry *existing_dn_entry;
    Slapi_Entry *addentry;
    CSN *opcsn;
    const char *basedn;
    char sessionid[REPL_SESSION_ID_SIZE];
    int r;
    int op_result = 0;
    int rc = 0; /* OK */

    if (slapi_op_abandoned(pb)) {
        return rc;
    }

    get_repl_session_id(pb, sessionid, &opcsn);
    slapi_pblock_get(pb, SLAPI_ADD_EXISTING_UNIQUEID_ENTRY, &existing_uniqueid_entry);
    if (existing_uniqueid_entry != NULL) {
        /*
         * An entry with this uniqueid already exists.
         * - It could be a replay of the same Add, or
         * - It could be a UUID generation collision, or
         */
        /*
         * This operation won't be replayed.  That is, this CSN won't update
         * the max csn in RUV. The CSN is left uncommitted in RUV unless an
         * error is set to op_result.  Just to get rid of this CSN from RUV,
         * setting an error to op_result
         */
        /* op_result = LDAP_SUCCESS; */
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_add_operation - %s - An entry with this uniqueid already exists.\n",
                      slapi_entry_get_dn_const(existing_uniqueid_entry));
        op_result = LDAP_ALREADY_EXISTS;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_NOOP; /* Ignore this Operation */
        PROFILE_POINT;          /* Add Conflict; UniqueID Exists;  Ignore */
        goto bailout;
    }

    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &addentry);
    slapi_pblock_get(pb, SLAPI_ADD_EXISTING_DN_ENTRY, &existing_dn_entry);

    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                   "urp_add_operation - handling add of (%s).\n",
                   slapi_entry_get_dn_const(addentry));
    /* first check if there was an existing entry at the time of this add 
     * in that case the new entry will become a conflict entry independent 
     * of the case if the other entry still exists
     */
    rc = urp_add_check_tombstone(pb, sessionid, addentry, opcsn);
    if (rc == 1) {
        /* the new entry is conflicting with an already deleted entry
         * transform to conflict entry
          */
        rc = urp_add_new_entry_to_conflict(pb, sessionid, addentry, opcsn);
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                       "urp_add_operation - new entry to conflictentry (%s).\n",
                       slapi_entry_get_dn_const(addentry));
        goto bailout;
    } else if (rc == 2) {
        /* the tombstone is newer than the added entry
         * inform the caller that this needs to become a tombstone operation
         * and turn the tombstone to a conflict
         */
        rc = SLAPI_PLUGIN_NOOP_TOMBSTONE;
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                       "urp_add_operation - new entry to tombstone (%s).\n",
                       slapi_entry_get_dn_const(addentry));
        goto bailout;
    }

    if (existing_dn_entry == NULL) /* The target DN does not exist */
    {
        /* Check for parent entry... this could be an orphan. */
        Slapi_Entry *parententry;
        slapi_pblock_get(pb, SLAPI_ADD_PARENT_ENTRY, &parententry);
        rc = urp_add_resolve_parententry(pb, sessionid, addentry, parententry, opcsn);
        PROFILE_POINT; /* Add Entry */
        goto bailout;
    }

    /*
     * Naming conflict: an entry with the target DN already exists.
     * Compare the DistinguishedNameCSN of the existing entry
     * and the OperationCSN. The smaller CSN wins. The loser changes
     * its RDN to uniqueid+baserdn, and adds operational attribute
     * ATTR_NSDS5_REPLCONFLIC.
     */
    basedn = slapi_entry_get_ndn (addentry);
    r = csn_compare (entry_get_dncsn(existing_dn_entry), opcsn);
    if (r < 0) {
        /* Entry to be added is a loser */
        rc = urp_add_new_entry_to_conflict(pb, sessionid, addentry, opcsn);
    } else if (r > 0) {
        char *conflict_dn = NULL;
        /* Existing entry is a loser */
        if (!urp_annotate_dn(sessionid, existing_dn_entry, opcsn, "ADD", &conflict_dn)) {
            op_result= LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_add_operation - %s - Entry to be added is a loser; "
                           "urp_annotate_dn failed.\n", basedn);
            rc = SLAPI_PLUGIN_NOOP; /* Ignore this Operation */
        } else {
            /* The backend add code should now search for the existing entry again. */
            rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
            slapi_pblock_set(pb, SLAPI_URP_NAMING_COLLISION_DN, conflict_dn);
        }
        PROFILE_POINT; /* Add Conflict; Entry Exists; Rename Existing Entry */
    } else             /* r==0 */
    {
        /* The CSN of the Operation and the Entry DN are the same.
         * This could only happen if:
         * a) There are two replicas with the same ReplicaID.
         * b) We've seen the Operation before.
         * Let's go with (b) and ignore the little bastard.
         */
        /*
         * This operation won't be replayed.  That is, this CSN won't update
         * the max csn in RUV. The CSN is left uncommitted in RUV unless an
         * error is set to op_result.  Just to get rid of this CSN from RUV,
         * setting an error to op_result
         */
        /* op_result = LDAP_SUCCESS; */
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_add_operation - %s - The CSN of the Operation and the Entry DN are the same.",
                      slapi_entry_get_dn_const(existing_dn_entry));
        op_result = LDAP_UNWILLING_TO_PERFORM;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_NOOP; /* Ignore this Operation */
        PROFILE_POINT;          /* Add Conflict; Entry Exists; Same CSN */
    }

bailout:
    return rc;
}

/*
 * Return 0 for OK, -1 for Error, >0 for action code
 * Action Code Bit 0: Fetch existing entry.
 * Action Code Bit 1: Fetch parent entry.
 */
int
urp_modrdn_operation(Slapi_PBlock *pb)
{
    slapi_operation_parameters *op_params = NULL;
    Slapi_Entry *parent_entry;
    Slapi_Entry *new_parent_entry;
    Slapi_DN *newsuperior = NULL;
    Slapi_DN *parentdn = NULL;
    Slapi_Entry *target_entry;
    Slapi_Entry *existing_entry;
    const CSN *target_entry_dncsn;
    CSN *opcsn = NULL;
    char *op_uniqueid = NULL;
    const char *existing_uniqueid = NULL;
    const Slapi_DN *target_sdn;
    const Slapi_DN *existing_sdn;
    char *newrdn;
    char sessionid[REPL_SESSION_ID_SIZE];
    int r;
    int op_result = 0;
    int rc = 0; /* OK */
    int del_old_replconflict_attr = 0;

    if (slapi_op_abandoned(pb)) {
        return rc;
    }

    slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_ENTRY, &target_entry);
    if (target_entry == NULL) {
        /* An entry can't be found for the Unique Identifier */
        op_result = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = -1;       /* No entry to modrdn */
        PROFILE_POINT; /* ModRDN Conflict; Entry does not Exist; Discard ModRDN */
        goto bailout;
    }

    get_repl_session_id(pb, sessionid, &opcsn);
    target_entry_dncsn = entry_get_dncsn(target_entry);
    if (csn_compare(target_entry_dncsn, opcsn) >= 0) {
        /*
         * The Operation CSN is not newer than the DN CSN.
         * Either we're beaten by another ModRDN or we've applied the op.
         */
        /* op_result= LDAP_SUCCESS; */
        /*
         * This operation won't be replayed.  That is, this CSN won't update
         * the max csn in RUV. The CSN is left uncommitted in RUV unless an
         * error is set to op_result.  Just to get rid of this CSN from RUV,
         * setting an error to op_result
         */
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_modrdn_operation - %s - operation CSN is newer than the DN CSN.\n",
                      slapi_entry_get_dn_const(target_entry));
        op_result = LDAP_UNWILLING_TO_PERFORM;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_NOOP; /* Ignore the modrdn */
        PROFILE_POINT;          /* ModRDN Conflict; Entry with Target DN Exists; OPCSN is not newer. */
        goto bailout;
    }

    /* The DN CSN is older than the Operation CSN. Apply the operation */
    target_sdn = slapi_entry_get_sdn_const(target_entry);
    /* newrdn is no need to be case-ignored (get_rdn_plus_uniqueid) */
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &newrdn);
    slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &op_uniqueid);
    slapi_pblock_get(pb, SLAPI_MODRDN_PARENT_ENTRY, &parent_entry);
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWPARENT_ENTRY, &new_parent_entry);
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &newsuperior);

    if (is_conflict_entry (target_entry)) {
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "urp_modrdn_operation  - Target_entry %s is a conflict; what to do ?\n",
                      slapi_entry_get_dn((Slapi_Entry *)target_entry));
    }

    if (is_tombstone_entry(target_entry)) {
        /*
         * It is a non-trivial task to rename a tombstone.
         * This op has been ignored so far by
         * setting SLAPI_RESULT_CODE to LDAP_NO_SUCH_OBJECT
         * and rc to -1.
         */

        /* Turn the tombstone to glue before rename it */
        /* check if the delete was after the modrdn */
        char *del_str = (char*)slapi_entry_attr_get_ref(target_entry, "nstombstonecsn");
        CSN *del_csn = csn_new_by_string(del_str);
        if (csn_compare(del_csn,opcsn)>0) {
            char *glue_dn = (char*)slapi_entry_attr_get_ref(target_entry, "nscpentrydn");
            Slapi_DN *glue_sdn = slapi_sdn_new_dn_byval(glue_dn);
            op_result = tombstone_to_glue (pb, sessionid, target_entry,
                                           glue_sdn, "renameTombstone", opcsn, NULL);
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                          "urp_modrdn_operation  - Target_entry %s is a tombstone; Renaming since delete was after rename.\n",
                           slapi_entry_get_dn((Slapi_Entry *)target_entry));
            slapi_sdn_free(&glue_sdn);
        } else {
            op_result = LDAP_NO_SUCH_OBJECT;
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                          "urp_modrdn_operation  - Target_entry %s is a tombstone; returning LDAP_NO_SUCH_OBJECT.\n",
                          slapi_entry_get_dn((Slapi_Entry *)target_entry));
        }
        csn_free(&del_csn);

        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);

        if (op_result == 0) {
            /*
             * Remember to turn this entry back to tombstone in post op.
             * We'll just borrow an obsolete pblock type here. ???
             */
            /* no. slapi_pblock_set (pb, SLAPI_URP_TOMBSTONE_UNIQUEID, slapi_ch_strdup(op_uniqueid)); */
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_TARGET_ENTRY);
            rc = 0;
        } else {
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_modrdn_operation - %s - Target entry is a tombstone.\n",
                          slapi_entry_get_dn_const(target_entry));
            rc = SLAPI_PLUGIN_NOOP; /* Ignore the modrdn */
        }
        PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; OPCSN is not newer. */
        goto bailout;
    }

    slapi_pblock_get(pb, SLAPI_MODRDN_EXISTING_ENTRY, &existing_entry);
    if (existing_entry != NULL) {
        /*
         * An entry with the target DN already exists.
         * The smaller dncsn wins. The loser changes its RDN to
         * uniqueid+baserdn, and adds operational attribute
         * ATTR_NSDS5_REPLCONFLICT
         */

        if (is_conflict_entry (existing_entry)) {
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                          "urp_modrdn_operation  - Existing_entry %s is a conflict; what to do ?\n",
                          slapi_entry_get_dn((Slapi_Entry *)existing_entry));
        }

        existing_uniqueid = slapi_entry_get_uniqueid(existing_entry);
        existing_sdn = slapi_entry_get_sdn_const(existing_entry);

        /*
         * It used to dismiss the operation if the existing entry is
         * the same as the target one.
         * But renaming the RDN with the one which only cases are different,
         * cn=ABC --> cn=Abc, this case matches.  We should go forward the op.
         */
        if (strcmp(op_uniqueid, existing_uniqueid) == 0) {
            op_result = LDAP_SUCCESS;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = 0;        /* Don't ignore the op */
            PROFILE_POINT; /* ModRDN Replay */
            goto bailout;
        }

        r = csn_compare(entry_get_dncsn(existing_entry), opcsn);
        if (r == 0) {
            /*
             * The CSN of the Operation and the Entry DN are the same
             * but the uniqueids are not.
             * There might be two replicas with the same ReplicaID.
             */
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_modrdn_operation - Duplicated CSN for different uniqueids [%s][%s]",
                          existing_uniqueid, op_uniqueid);
            op_result = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = SLAPI_PLUGIN_NOOP; /* Ignore this Operation */
            PROFILE_POINT;          /* ModRDN Conflict; Duplicated CSN for Different Entries */
            goto bailout;
        }

        if (r < 0) {
            /* The target entry is a loser */

            char *newrdn_with_uniqueid;
            newrdn_with_uniqueid = get_rdn_plus_uniqueid(sessionid, newrdn, op_uniqueid);
            if (newrdn_with_uniqueid == NULL) {
                op_result = LDAP_OPERATIONS_ERROR;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
                rc = -1;       /* Ignore this Operation */
                PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists;
                                  Unique ID already in RDN - Change to Lost and Found entry */
                goto bailout;
            }
            mod_namingconflict_attr(op_uniqueid, target_sdn, existing_sdn, opcsn, "MODRDN");
            mod_objectclass_attr(op_uniqueid, target_sdn, target_sdn, opcsn, "MODRDN");
            slapi_pblock_set(pb, SLAPI_MODRDN_NEWRDN, newrdn_with_uniqueid);
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_modrdn_operation - Naming conflict MODRDN. Rename target entry from %s to %s\n",
                          newrdn, newrdn_with_uniqueid);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; Rename Operation Entry */
            goto bailout;
        }

        if (r > 0) {
            /* The existing entry is a loser */

            int resolve = urp_annotate_dn(sessionid, existing_entry, opcsn, "MODRDN", NULL);
            if (!resolve) {
                op_result = LDAP_OPERATIONS_ERROR;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
                rc = -1; /* Abort this Operation */
                goto bailout;
            }
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
            if (LDAP_NO_SUCH_OBJECT == resolve) {
                /* This means that existing_dn_entry did not really exist!!!
                 * This indicates that a get_copy_of_entry -> dn2entry returned
                 * an entry (existing_dn_entry) that was already removed from the ldbm.
                 * This is bad, because it indicates a dn cache or DB corruption.
                 * However, as far as the conflict is concerned, this error is harmless:
                 * if the existing_dn_entry did not exist in the first place, there was no
                 * conflict!! Return 0 for success to break the ldbm_back_modrdn loop
                 * and get out of this inexistent conflict resolution ASAP.
                 */
                rc = 0;
            }
            /* Set flag to remove possible old naming conflict */
            del_old_replconflict_attr = 1;
            PROFILE_POINT; /* ModRDN Conflict; Entry with Target DN Exists; Rename Entry with Target DN */
            goto bailout;
        }
    } else {
        /*
         * No entry with the target DN exists.
         */

        /* Set flag to remove possible old naming conflict */
        del_old_replconflict_attr = 1;

        if (new_parent_entry != NULL) {
            /* The new superior entry exists */
            rc = 0;        /* OK, Apply the ModRDN */
            PROFILE_POINT; /* ModRDN Conflict; OK */
            goto bailout;
        }

        /* The new superior entry doesn't exist */

        slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &newsuperior);
        if (newsuperior == NULL) {
            /* (new_parent_entry==NULL && newsuperiordn==NULL)
             * This is ok - SLAPI_MODRDN_NEWPARENT_ENTRY will
             * only be set if SLAPI_MODRDN_NEWSUPERIOR_SDN was
             * suplied by the client. If it wasn't, we're just
             * changing the RDN of the entry. In that case,
             * if the entry exists, its parent won't change
             * when it's renamed, and therefore we can assume
             * its parent exists.
             */
            rc = 0;
            PROFILE_POINT; /* ModRDN OK */
            goto bailout;
        }

        if ((0 == slapi_sdn_compare(slapi_entry_get_sdn(parent_entry),
                                    newsuperior)) ||
            is_suffix_dn(pb, newsuperior, &parentdn)) {
            /*
             * The new superior is the same as the current one, or
             * this entry is a suffix whose parent can be absent.
             */
            rc = 0;        /* OK, Move the entry */
            PROFILE_POINT; /* ModRDN Conflict; Absent Target Parent; Create Suffix Entry */
            goto bailout;
        }

        /*
         * This entry is not a suffix entry, so the parent entry should exist.
         * (This shouldn't happen in a ds5 server)
         */
        slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
        op_result = create_glue_entry(pb, sessionid, newsuperior,
                                      op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid, opcsn);
        if (LDAP_SUCCESS != op_result) {
            /*
             * FATAL ERROR
             * We should probably just abort the rename
             * this will cause replication divergence requiring
             * admin intercession
             */
            slapi_log_err(SLAPI_LOG_ERR, sessionid,
                          "urp_modrdn_operation - Parent %s couldn't be found, nor recreated as a glue entry\n",
                          slapi_sdn_get_dn(newsuperior));
            op_result = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = SLAPI_PLUGIN_FAILURE; /* Ignore this Operation */
            PROFILE_POINT;
            goto bailout;
        }

        /* The backend add code should now search for the parent again. */
        rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_NEWPARENT_ENTRY);
        PROFILE_POINT; /* ModRDN Conflict; Absent Target Parent - Change to Lost and Found entry */
        goto bailout;
    }

bailout:
    if (del_old_replconflict_attr && rc == 0) {
        del_replconflict_attr(target_entry, opcsn, 0);
    }
    if (parentdn)
        slapi_sdn_free(&parentdn);
    return rc;
}

/*
 * Return 0 for OK, -1 for Error
 */
int
urp_delete_operation(Slapi_PBlock *pb)
{
    Slapi_Entry *deleteentry;
    CSN *opcsn = NULL;
    char sessionid[REPL_SESSION_ID_SIZE];
    int op_result = 0;
    int rc = SLAPI_PLUGIN_SUCCESS; /* OK */

    if (slapi_op_abandoned(pb)) {
        return rc;
    }

    slapi_pblock_get(pb, SLAPI_DELETE_EXISTING_ENTRY, &deleteentry);

    get_repl_session_id(pb, sessionid, &opcsn);
    if (deleteentry == NULL) /* uniqueid can't be found */
    {
        op_result = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_FAILURE; /* Don't apply the Delete */
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_delete_operation - Entry %s does not exist; returning NO_SUCH_OBJECT.\n",
                      slapi_entry_get_dn((Slapi_Entry *)deleteentry));
        PROFILE_POINT; /* Delete Operation; Entry not exist. */
    } else if (is_tombstone_entry(deleteentry)) {
        /* The entry is already a Tombstone,
         * either because the operation was already applied,
         * then ignore this delete.
         * check if the tombstone csn matches the operationcsn
         */
        if (is_deleted_at_csn(deleteentry, opcsn)) {
            op_result= LDAP_ALREADY_EXISTS;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = SLAPI_PLUGIN_NOOP; /* Don't apply the Delete */
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_delete_operation - Entry \"%s\" is already a Tombstone.\n",
                           slapi_entry_get_dn_const(deleteentry));
        } else if (urp_delete_check_conflict (sessionid, deleteentry, opcsn)) {
            /* the other option is that an entry was turned into a conflict
             * when a conflicting  add was handled for an already delted entry
             * This means that now the conflict has also to be turned into a tombstone
             * Check if such a conflict exists - and turn to a tombstone
             */
            op_result= LDAP_SUCCESS;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = SLAPI_PLUGIN_NOOP_COMMIT; /* Don't apply the Delete */
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_delete_operation - Deleted conflict entry instead of tombstone \"%s\"\n",
                           slapi_entry_get_dn_const(deleteentry));
        } else {
            /* no already deleted tombstone and no alternative to delete
             * do nothing
             */
            op_result= LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
            rc = SLAPI_PLUGIN_NOOP; /* Don't apply the Delete */
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_delete_operation - Entry \"%s\" cannot be deleted.\n",
                           slapi_entry_get_dn_const(deleteentry));
        }

        PROFILE_POINT; /* Delete Operation; Already a Tombstone. */
    } else             /* The entry to be deleted exists and is not a tombstone */
    {
        get_repl_session_id(pb, sessionid, &opcsn);

        /* Check if the entry has children. */
        if (!slapi_entry_has_children(deleteentry)) {
            /* Remove possible conflict attributes */
            rc = SLAPI_PLUGIN_SUCCESS; /* OK, to delete the entry */
            if (is_conflict_entry(deleteentry)) {
                Slapi_DN *sdn = NULL;
                slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdn);
                if (0 == slapi_sdn_compare(sdn, slapi_entry_get_sdn(deleteentry))) {
                    /* the delete directly targetd the conflict entry, just continue */
                } else {
                    /* check if there is a valid entry added before the delete,
                     * then we need to delete this entry
                     */
                    char *valid_uniqueid = urp_find_valid_entry_to_delete(pb, deleteentry, sessionid, opcsn);
                    if (valid_uniqueid) {
                        /* do we need to free SLAPI_TARGET_UNIQUEID ? */
                        slapi_pblock_set( pb, SLAPI_TARGET_UNIQUEID, (void*)valid_uniqueid);
                        rc = slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_UNIQUEID_ENTRY);
                    } else { 
                        del_replconflict_attr (deleteentry, opcsn, 0);
                        rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
                    }
                }
            }  else if (is_renamed_entry(pb, deleteentry, opcsn)) {
                /* the entry was renamed before the delete, ignore the delete */
                op_result= LDAP_SUCCESS;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
                rc = SLAPI_PLUGIN_NOOP; /* Don't apply the Delete */
            }
            PROFILE_POINT; /* Delete Operation; OK. */
        } else {
            /* the entry has children, so it is either the orphan-glue
             * scenario or the more complex situatin when there is a conflict entry
             * for the entry to be deleted which would be restored in the post_delete
             * phase. We would just have to swap the entries and be done,
             * but unfortunately as a plugin this is not possible.
             * So the approach is:
             * - move the children to th conflict entry
             * - let the delete happen
             * - let the post_del rename the conflict entry
             */
            Slapi_Entry *conflict_entry = urp_get_min_naming_conflict_entry (pb, slapi_entry_get_dn_const(deleteentry), sessionid, opcsn);
            if (conflict_entry) {
                urp_rename_conflict_children(slapi_entry_get_dn_const(deleteentry),
                                             slapi_entry_get_sdn_const(conflict_entry));
                slapi_entry_free(conflict_entry);
                rc = SLAPI_PLUGIN_SUCCESS; /* OK, to delete the entry */
            } else {
                /* Turn this entry into a glue_absent_parent entry */
                rc = entry_to_glue(sessionid, deleteentry, REASON_RESURRECT_ENTRY, opcsn);

                /* Turn the Delete into a No-Op */
                op_result= LDAP_SUCCESS;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
                if (rc == 0) {
                    /* Don't apply the Delete, but commit the glue change */
                    rc = SLAPI_PLUGIN_NOOP_COMMIT;
                } else {
                    rc = SLAPI_PLUGIN_NOOP; /* Don't apply the Delete */
                }
                slapi_log_err(slapi_log_urp, sessionid,
                               "urp_delete_operation - Turn entry \"%s\" into a glue_absent_parent entry.\n",
                                 slapi_entry_get_dn_const(deleteentry));
                PROFILE_POINT; /* Delete Operation; Entry has children. */
            }
        }
    }
    return rc;
}


/*
 * Return 0 for OK, -1 for Error
 */
int
urp_post_add_operation(Slapi_PBlock *pb)
{
    char sessionid[REPL_SESSION_ID_SIZE];
    Slapi_Operation *op;
    CSN *opcsn;
    Slapi_Entry *addentry;
    char *conflict_dn = NULL;
    const char *valid_dn = NULL;
    int rc = 0;

    slapi_pblock_get( pb, SLAPI_URP_NAMING_COLLISION_DN, &conflict_dn );

    if (conflict_dn) {
        /* the existing entry was turned into a conflict,
         * move children to the valid entry
         */
        slapi_pblock_get( pb, SLAPI_OPERATION, &op);
        get_repl_session_id (pb, sessionid, &opcsn);
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "urp_post_add_operation - Entry %s is conflict entry, check for children\n",
                      conflict_dn);
        slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &addentry );
        valid_dn = slapi_entry_get_dn_const(addentry);
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "urp_post_add_operation - Entry %s is valid entry, check for children\n",
                      valid_dn);
        rc = urp_rename_conflict_children(conflict_dn, slapi_entry_get_sdn_const(addentry));
    }
    slapi_ch_free_string(&conflict_dn);
    slapi_pblock_set( pb, SLAPI_URP_NAMING_COLLISION_DN, NULL );
    slapi_pblock_get( pb, SLAPI_URP_TOMBSTONE_CONFLICT_DN, &conflict_dn );

    if (conflict_dn) {
    /* a tombstone was tirned into a conflict
     * move it to a valid parent if parent is also a conflict
     */
        Slapi_DN *conflict_sdn = slapi_sdn_new_dn_byval(conflict_dn);
        char *parent_dn = slapi_dn_parent(conflict_dn);
        slapi_pblock_get( pb, SLAPI_OPERATION, &op);
        get_repl_session_id (pb, sessionid, &opcsn);
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "urp_post_add_operation - Entry %s is conflict from tombstone, check parent\n",
                      conflict_dn);
        rc = tombstone_to_conflict_check_parent(sessionid, parent_dn, NULL, NULL, opcsn, conflict_sdn);
        slapi_sdn_free(&conflict_sdn);
        slapi_ch_free_string(&parent_dn);
    }
    return rc;
}

int
urp_post_modrdn_operation(Slapi_PBlock *pb)
{
    CSN *opcsn;
    char sessionid[REPL_SESSION_ID_SIZE];
    char *tombstone_uniqueid;
    Slapi_Entry *postentry;
    Slapi_Operation *op;
    int rc = SLAPI_PLUGIN_SUCCESS; /* OK */
    int oprc = 0;

    /* only execute if operation was successful */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc);
    if (oprc) return rc;

    /*
     * Do not abandon the post op - the processed CSN needs to be
     * committed to keep the consistency between the changelog
     * and the backend DB.
     * if ( slapi_op_abandoned(pb) ) return 0;
     */

    slapi_pblock_get(pb, SLAPI_URP_TOMBSTONE_UNIQUEID, &tombstone_uniqueid);
    if (tombstone_uniqueid == NULL) {
        /*
         * The entry is not resurrected from tombstone. Hence
         * we need to check if any naming conflict with its
         * old dn can be resolved.
         */
        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
            get_repl_session_id(pb, sessionid, &opcsn);
            urp_naming_conflict_removal(pb, sessionid, opcsn, "MODRDN");
            /* keep track of the entry's lifetime befor renaming
             * by creating a cenotaph
             */
            urp_fixup_add_cenotaph(pb, sessionid, opcsn);
        }
    } else {
        /*
         * The entry was a resurrected tombstone.
         * This could happen when we applied a rename
         * to a tombstone to avoid server divergence. Now
         * it's time to put the entry back to tombstone.
         */
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &postentry);
        if (postentry && strcmp(tombstone_uniqueid, slapi_entry_get_uniqueid(postentry)) == 0) {
            entry_to_tombstone(pb, postentry);
        }
        slapi_ch_free((void **)&tombstone_uniqueid);
        slapi_pblock_set(pb, SLAPI_URP_TOMBSTONE_UNIQUEID, NULL);
    }

    return rc;
}

/*
 * Conflict removal
 */
int
urp_post_delete_operation(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    Slapi_Entry *entry;
    CSN *opcsn;
    char sessionid[REPL_SESSION_ID_SIZE];
    int op_result;
    int oprc = 0;

    /* only execute if operation was successful */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc);
    if (oprc) return 0;

    /*
     * Do not abandon the post op - the processed CSN needs to be
     * committed to keep the consistency between the changelog
     * and the backend DB
     * if ( slapi_op_abandoned(pb) ) return 0;
     */

    get_repl_session_id(pb, sessionid, &opcsn);

    /*
     * Conflict removal from the parent entry:
     * If the parent is glue and has no more children,
     * turn the parent to tombstone
     */
    slapi_pblock_get(pb, SLAPI_DELETE_GLUE_PARENT_ENTRY, &entry);
    if (entry != NULL) {
        op_result = entry_to_tombstone(pb, entry);
        if (op_result == LDAP_SUCCESS) {
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_post_delete_operation - Tombstoned glue entry %s since it has no more children\n",
                          slapi_entry_get_dn_const(entry));
        }
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
        /*
         * Conflict removal from the peers of the old dn
         */
        urp_naming_conflict_removal(pb, sessionid, opcsn, "DEL");
    }

    return 0;
}

static int
urp_fixup_add_cenotaph(Slapi_PBlock *pb, char *sessionid, CSN *opcsn)
{
    Slapi_PBlock *add_pb;
    Slapi_Entry *cenotaph = NULL;
    Slapi_Entry *pre_entry = NULL;
    int ret = 0;
    Slapi_DN *pre_sdn = NULL;
    Slapi_RDN *rdn = NULL;
    char *parentdn = NULL;
    char *newdn;
    const char *entrydn;
    const char *uniqueid = NULL;
    CSN *dncsn = NULL;
    char csnstr[CSN_STRSIZE+1];

    slapi_pblock_get (pb, SLAPI_ENTRY_PRE_OP, &pre_entry);
    if (pre_entry == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, sessionid,
                       "urp_fixup_add_cenotaph - failed to get preop entry\n");
        return -1;
    }

    slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &pre_sdn);
    entrydn = slapi_sdn_get_ndn(pre_sdn);
    /* pre_sdn = slapi_entry_get_sdn(pre_entry); 
       entrydn = slapi_entry_get_ndn (pre_entry);*/
    uniqueid = slapi_entry_get_uniqueid (pre_entry);
    parentdn = slapi_dn_parent(entrydn);
    rdn = slapi_rdn_new();
    slapi_sdn_get_rdn(pre_sdn, rdn);
    slapi_rdn_remove_attr (rdn, SLAPI_ATTR_UNIQUEID );
    slapi_rdn_add(rdn, "cenotaphID", uniqueid);
    newdn = slapi_ch_smprintf("%s,%s", slapi_rdn_get_rdn(rdn), parentdn);
    slapi_rdn_free(&rdn);
    slapi_ch_free_string(&parentdn);
    /* slapi_sdn_free(&pre_sdn); */

    cenotaph = slapi_entry_alloc();
    slapi_entry_init(cenotaph, slapi_ch_strdup(newdn), NULL);

    dncsn = (CSN *)entry_get_dncsn (pre_entry);
    slapi_entry_add_string(cenotaph, SLAPI_ATTR_OBJECTCLASS, "extensibleobject");
    slapi_entry_add_string(cenotaph, SLAPI_ATTR_OBJECTCLASS, "nstombstone");
    slapi_entry_add_string(cenotaph,"cenotaphfrom", csn_as_string(dncsn, PR_FALSE, csnstr));
    slapi_entry_add_string(cenotaph,"cenotaphto", csn_as_string(opcsn, PR_FALSE, csnstr));
    slapi_entry_add_string(cenotaph,"nstombstonecsn", csn_as_string(opcsn, PR_FALSE, csnstr));
    slapi_entry_add_string(cenotaph, SLAPI_ATTR_NSCP_ENTRYDN, entrydn);

    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                   "urp_fixup_add_cenotaph - addinng cenotaph: %s \n", newdn);
    add_pb = slapi_pblock_new();
    slapi_pblock_init(add_pb);

    slapi_add_entry_internal_set_pb(add_pb,
                                    cenotaph,
                                    NULL,
                                    repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                    OP_FLAG_REPL_FIXUP|OP_FLAG_NOOP|OP_FLAG_CENOTAPH_ENTRY|SLAPI_OP_FLAG_BYPASS_REFERRALS);
    slapi_add_internal_pb(add_pb);
    slapi_pblock_get(add_pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    slapi_pblock_destroy(add_pb);

    if (ret == LDAP_ALREADY_EXISTS) {
        /* the cenotaph already exists, probably because of a loop
         * in renaming entries. Update it with new csns
         */
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                       "urp_fixup_add_cenotaph - cenotaph (%s) already exists, updating\n", newdn);
        Slapi_PBlock *mod_pb = slapi_pblock_new();
        Slapi_Mods smods;
        Slapi_DN *sdn = slapi_sdn_new_dn_byval(newdn);
        slapi_mods_init(&smods, 4);
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "cenotaphfrom", csn_as_string(dncsn, PR_FALSE, csnstr));
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "cenotaphto", csn_as_string(opcsn, PR_FALSE, csnstr));
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "nstombstonecsn", csn_as_string(opcsn, PR_FALSE, csnstr));

        slapi_modify_internal_set_pb_ext(
            mod_pb,
            sdn,
            slapi_mods_get_ldapmods_byref(&smods),
            NULL, /* Controls */
            NULL,
            repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
            OP_FLAG_REPL_FIXUP|OP_FLAG_NOOP|OP_FLAG_CENOTAPH_ENTRY|SLAPI_OP_FLAG_BYPASS_REFERRALS);

        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
        if (ret != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, sessionid,
                       "urp_fixup_add_cenotaph - failed to modify cenotaph, err= %d\n", ret);
        }
        slapi_mods_done(&smods);
        slapi_sdn_free(&sdn);
        slapi_pblock_destroy(mod_pb);

    } else if (ret != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, sessionid,
                       "urp_fixup_add_cenotaph - failed to add cenotaph, err= %d\n", ret);
    }
    slapi_ch_free_string(&newdn);

    return ret;
}

int
urp_fixup_add_entry(Slapi_Entry *e, const char *target_uniqueid, const char *parentuniqueid, CSN *opcsn, int opflags)
{
    Slapi_PBlock *newpb;
    Slapi_Operation *op;
    int op_result;

    newpb = slapi_pblock_new();

    /*
     * Mark this operation as replicated, so that the front end
     * doesn't add extra attributes.
     */
    slapi_add_entry_internal_set_pb(
        newpb,
        e,    /* entry will be consumed */
        NULL, /*Controls*/
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);
    if (target_uniqueid) {
        slapi_pblock_set(newpb, SLAPI_TARGET_UNIQUEID, (void *)target_uniqueid);
    }
    if (parentuniqueid) {
        struct slapi_operation_parameters *op_params;
        slapi_pblock_get(newpb, SLAPI_OPERATION_PARAMETERS, &op_params);
        op_params->p.p_add.parentuniqueid = (char *)parentuniqueid; /* Consumes parentuniqueid */
    }
    slapi_pblock_get(newpb, SLAPI_OPERATION, &op);
    operation_set_csn(op, opcsn);

    slapi_add_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_destroy(newpb);

    return op_result;
}

int
urp_fixup_modrdn_entry (const Slapi_DN *entrydn, const char *newrdn, const Slapi_DN *newsuperior, const char *entryuniqueid, const char *parentuniqueid, CSN *opcsn, int opflags)
{
    Slapi_PBlock *newpb;
    Slapi_Operation *op;
    int op_result;

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "urp_fixup_modrdn_entry: moving entry (%s) to (%s) as (%s)\n",
                   slapi_sdn_get_dn(entrydn), slapi_sdn_get_dn(newsuperior),newrdn);
    /* log only
    return 0; */
    newpb = slapi_pblock_new();

    slapi_rename_internal_set_pb_ext(newpb,
                                     entrydn,
                                     newrdn,
                                     newsuperior,
                                     0,
                                     NULL,
                                     entryuniqueid,
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                     OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

    /* set operation csn if provided */
    if (opcsn) {
        slapi_pblock_get (newpb, SLAPI_OPERATION, &op);
        operation_set_csn (op, opcsn);
    }
    if (parentuniqueid) {
        struct slapi_operation_parameters *op_params;
        slapi_pblock_get( newpb, SLAPI_OPERATION_PARAMETERS, &op_params );
        op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid = (char*)parentuniqueid;
    }
    slapi_modrdn_internal_pb(newpb); 
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                  "urp_fixup_modrdn_entry: moving entry (%s) result %d\n",
                  slapi_sdn_get_dn(entrydn), op_result);
    slapi_pblock_destroy(newpb);
    return op_result;
}

int
urp_fixup_rename_entry(const Slapi_Entry *entry, const char *newrdn, const char *parentuniqueid, int opflags)
{
    Slapi_PBlock *newpb;
    Slapi_Operation *op;
    CSN *opcsn;
    int op_result;

    newpb = slapi_pblock_new();

    /*
     * Must mark this operation as replicated,
     * so that the frontend doesn't add extra attributes.
     */
    slapi_rename_internal_set_pb_ext(
        newpb,
        slapi_entry_get_sdn_const(entry),
        newrdn,                          /*NewRDN*/
        NULL,                            /*NewSuperior*/
        0,                               /* !Delete Old RDNS */
        NULL,                            /*Controls*/
        slapi_entry_get_uniqueid(entry), /*uniqueid*/
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

    /* set operation csn to the entry's dncsn */
    opcsn = (CSN *)entry_get_dncsn(entry);
    slapi_pblock_get(newpb, SLAPI_OPERATION, &op);
    operation_set_csn(op, opcsn);
    if (parentuniqueid) {
        struct slapi_operation_parameters *op_params;
        slapi_pblock_get(newpb, SLAPI_OPERATION_PARAMETERS, &op_params);
        op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid = (char *)parentuniqueid;
    }
    slapi_modrdn_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);

    slapi_pblock_destroy(newpb);
    return op_result;
}

int
urp_fixup_move_entry (const Slapi_Entry *entry, const Slapi_DN *newsuperior, int opflags)
{
    Slapi_PBlock *newpb;
    int op_result;

    newpb = slapi_pblock_new();

    /* 
     * Must mark this operation as replicated,
     * so that the frontend doesn't add extra attributes.
     */
    slapi_rename_internal_set_pb_ext(newpb,
                                     slapi_entry_get_sdn_const (entry),
                                     slapi_entry_get_rdn_const(entry), /*NewRDN*/
                                     newsuperior, /*NewSuperior*/
                                     0, /* !Delete Old RDNS */
                                     NULL, /*Controls*/
                                     slapi_entry_get_uniqueid (entry), /*uniqueid*/
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                     OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

    /* set operation csn to the entry's dncsn */
	/* TBD check which csn to use 
    Slapi_Operation *op;
	CSN *opcsn;
	opcsn = (CSN *)entry_get_dncsn (entry);
    slapi_pblock_get (newpb, SLAPI_OPERATION, &op);
    operation_set_csn (op, opcsn);
TBD */
    slapi_modrdn_internal_pb(newpb); 
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);

    slapi_pblock_destroy(newpb);
    return op_result;
}

int
urp_fixup_delete_entry(const char *uniqueid, const char *dn, CSN *opcsn, int opflags)
{
    Slapi_PBlock *newpb;
    Slapi_Operation *op;
    int op_result;

    newpb = slapi_pblock_new();

    /*
     * Mark this operation as replicated, so that the front end
     * doesn't add extra attributes.
     */
    slapi_delete_internal_set_pb(
        newpb,
        dn,
        NULL,     /*Controls*/
        uniqueid, /*uniqueid*/
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);
    slapi_pblock_get(newpb, SLAPI_OPERATION, &op);
    operation_set_csn(op, opcsn);

    slapi_delete_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_destroy(newpb);

    return op_result;
}

int
urp_fixup_modify_entry(const char *uniqueid, const Slapi_DN *sdn, CSN *opcsn, Slapi_Mods *smods, int opflags)
{
    Slapi_PBlock *newpb;
    Slapi_Operation *op;
    int op_result;

    newpb = slapi_pblock_new();

    slapi_modify_internal_set_pb_ext(
        newpb,
        sdn,
        slapi_mods_get_ldapmods_byref(smods),
        NULL, /* Controls */
        uniqueid,
        repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
        OP_FLAG_REPLICATED | OP_FLAG_REPL_FIXUP | opflags);

    /* set operation csn */
    slapi_pblock_get(newpb, SLAPI_OPERATION, &op);
    operation_set_csn(op, opcsn);

    /* do modify */
    slapi_modify_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_destroy(newpb);

    return op_result;
}

static int
urp_add_resolve_parententry(Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, Slapi_Entry *parententry, CSN *opcsn)
{
    Slapi_DN *parentdn = NULL;
    Slapi_RDN *add_rdn = NULL;
    char *newdn = NULL;
    int ldap_rc;
    int rc = 0;
    Slapi_DN *sdn = NULL;

    if (is_suffix_entry(pb, entry, &parentdn)) {
        /* It's OK for the suffix entry's parent to be absent */
        rc = 0;
        PROFILE_POINT; /* Add Conflict; Suffix Entry */
        goto bailout;
    }

    /* The entry is not a suffix. */
    if (parententry == NULL) /* The parent entry was not found. */
    {
        /* Create a glue entry to stand in for the absent parent */
        slapi_operation_parameters *op_params;
        slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
        ldap_rc = create_glue_entry(pb, sessionid, parentdn, op_params->p.p_add.parentuniqueid, opcsn);
        if (LDAP_SUCCESS == ldap_rc) {
            /* The backend code should now search for the parent again. */
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
            PROFILE_POINT; /* Add Conflict; Orphaned Entry; Glue Parent */
        } else {
            /*
             * Error. The parent can't be created as a glue entry.
             * This will cause replication divergence and will
             * require admin intercession
             */
            ldap_rc = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_rc);
            rc = -1;       /* Abort this Operation */
            PROFILE_POINT; /* Add Conflict; Orphaned Entry; Impossible to create parent; Refuse Change. */
        }
        goto bailout;
    }

    if (is_tombstone_entry(parententry)) /* The parent is a tombstone */
    {
        /* The parent entry must be resurected from the dead. */
        /* parentdn retrieved from entry is not tombstone dn. */
        ldap_rc = tombstone_to_glue(pb, sessionid, parententry, parentdn, REASON_RESURRECT_ENTRY, opcsn, NULL);
        if (ldap_rc != LDAP_SUCCESS) {
            ldap_rc = LDAP_OPERATIONS_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ldap_rc);
            rc = -1; /* Abort the operation */
        } else {
            /* The backend add code should now search for the parent again. */
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
            rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_PARENT_ENTRY);
        }
        PROFILE_POINT; /* Add Conflict; Orphaned Entry; Parent Was Tombstone */
        goto bailout;
    }

    if(is_conflict_entry(parententry)) {
        /* The parent is a conflict entry
         * Check if a valid entry exists and make this the new parent
         *
         * or if no valid entry exists
         * - check if also a tombstone for the parentdn exists
         * - compare csns of tombstone and conflict generation
         * - turn the latest into a valid glue
         */
        char *valid_nsuniqueid = urp_get_valid_parent_nsuniqueid(parentdn);
        if (valid_nsuniqueid) {
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                          "urp_resolve parent entry: found valid parent %s\n", slapi_sdn_get_dn(parentdn));
            struct slapi_operation_parameters *op_params;
            slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
            /* free it ? */
            op_params->p.p_add.parentuniqueid = valid_nsuniqueid;
        } else {
            int op_result;
            char *tombstone_nsuniqueid = urp_find_tombstone_for_glue(pb, sessionid, parententry, parentdn, opcsn);
            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
            if (tombstone_nsuniqueid) {
                struct slapi_operation_parameters *op_params;
                slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
                op_params->p.p_add.parentuniqueid = tombstone_nsuniqueid;
                rc = LDAP_SUCCESS;
            } else if (0 == op_result) {
                rc = urp_conflict_to_glue(sessionid, parententry, parentdn, opcsn);
                if (rc == 0) {
                    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                                  "urp_resolve parent entry: created valid parent from conflict %s\n", slapi_sdn_get_dn(parentdn));
                } else {
                    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                                  "urp_resolve parent entry: cannot resolve parent %s\n", slapi_sdn_get_dn(parentdn));
                }
            }
        }
        goto bailout;
    }

    /* The parent is healthy */
    /* Now we need to check that the parent has the correct DN */
    if (slapi_sdn_isparent(slapi_entry_get_sdn(parententry), slapi_entry_get_sdn(entry))) {
        rc = 0;        /* OK, Add the entry */
        PROFILE_POINT; /* Add Conflict; Parent Exists */
        goto bailout;
    }

    /*
     * Parent entry doesn't have a DN parent to the entry.
     * This can happen if parententry was renamed due to
     * conflict and the child entry was created before
     * replication occured. See defect 530942.
     * We need to rename the entry to be child of its parent.
     */
    add_rdn = slapi_rdn_new_dn(slapi_entry_get_dn_const(entry));
    newdn = slapi_dn_plus_rdn(slapi_entry_get_dn_const(parententry), slapi_rdn_get_rdn(add_rdn));
    slapi_entry_set_normdn(entry, newdn);

    /* slapi_pblock_get(pb, SLAPI_ADD_TARGET, &dn); */
    slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
    slapi_sdn_free(&sdn);

    sdn = slapi_sdn_dup(slapi_entry_get_sdn_const(entry));
    slapi_pblock_set(pb, SLAPI_ADD_TARGET_SDN, sdn);

    slapi_log_err(slapi_log_urp, sessionid,
                  "Parent was renamed. Renamed the child to %s\n", newdn);
    rc = slapi_setbit_int(rc, SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);
    PROFILE_POINT; /* Add Conflict; Parent Renamed; Rename Operation Entry */

bailout:
    if (parentdn)
        slapi_sdn_free(&parentdn);
    slapi_rdn_free(&add_rdn);
    return rc;
}
static int
urp_add_new_entry_to_conflict (Slapi_PBlock *pb, char *sessionid, Slapi_Entry *addentry, CSN *opcsn)
{
    int rc = 0;
    int op_result;

    /* Entry to be added is a loser */
    char *basedn = slapi_entry_get_ndn(addentry);
    const char *adduniqueid = slapi_entry_get_uniqueid(addentry);
    char *newdn= get_dn_plus_uniqueid(sessionid, (const Slapi_DN *)addentry, adduniqueid);
    if(newdn == NULL) {
        op_result= LDAP_OPERATIONS_ERROR;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &op_result);
        rc = SLAPI_PLUGIN_NOOP; /* Abort this Operation */
        slapi_log_err(slapi_log_urp, sessionid,
                       "urp_add_operation - %s - Add conflict; Unique ID (%s) already in RDN\n",
                       basedn, adduniqueid);
    } else {
        /* Add the nsds5ReplConflict attribute in the mods */
        Slapi_Attr *attr = NULL;
        Slapi_Value **vals = NULL;
        Slapi_Value **csn_vals = NULL;
        Slapi_DN *sdn = NULL;
        Slapi_RDN *rdn;
        char buf[BUFSIZ];
        char csnstr[CSN_STRSIZE+1];

        PR_snprintf(buf, BUFSIZ, "%s (ADD) %s", REASON_ANNOTATE_DN, basedn);
        if (slapi_entry_attr_find(addentry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0) {
            /* ATTR_NSDS5_REPLCONFLICT exists */
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                          "urp_add_operation - New entry has nsds5ReplConflict already\n");
            vals = attr_get_present_values (attr); /* this returns a pointer to the contents */
        }
        if ( vals == NULL || *vals == NULL ) {
            /* Add new attribute */
            slapi_entry_add_string(addentry, ATTR_NSDS5_REPLCONFLICT, buf);
        } else {
            /*
             * Replace old attribute. We don't worry about the index
             * change here since the entry is yet to be added.
             */
            slapi_value_set_string(*vals, buf);
        }

        /* add the ldapsubentry objectclass if not present */
        slapi_entry_attr_find(addentry, "objectclass", &attr);
        if (attr != NULL) {
            struct berval bv;
            bv.bv_val = "ldapsubentry";
            bv.bv_len = strlen(bv.bv_val);
            if (slapi_attr_value_find(attr, &bv) != 0) {
                Slapi_Value *new_v = slapi_value_new();
                slapi_value_init_berval(new_v, &bv);
                slapi_attr_add_value(attr, new_v);
                slapi_value_free(&new_v);
                slapi_entry_set_flag(addentry, SLAPI_ENTRY_LDAPSUBENTRY);
            }
        }
        /* add or replace the conflict csn */
        if (slapi_entry_attr_find (addentry, "conflictcsn", &attr) == 0) {
            csn_vals = attr_get_present_values (attr);
        }
        if (csn_vals == NULL || *csn_vals == NULL ) {
            slapi_entry_add_string(addentry,"conflictcsn", csn_as_string(opcsn, PR_FALSE, csnstr));
        } else {

            slapi_value_set_string (*csn_vals, csn_as_string(opcsn, PR_FALSE, csnstr));
        }

        /* slapi_pblock_get(pb, SLAPI_ADD_TARGET, &dn); */
        slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
        slapi_sdn_free(&sdn);

        slapi_entry_set_normdn(addentry, newdn); /* dn: passin */

        sdn = slapi_sdn_dup(slapi_entry_get_sdn_const(addentry));
        slapi_pblock_set(pb, SLAPI_ADD_TARGET_SDN, sdn);

        rdn = slapi_rdn_new_sdn ( slapi_entry_get_sdn_const(addentry) );
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                         "urp_add_operation - Naming conflict ADD. Add %s instead\n",
                         slapi_rdn_get_rdn(rdn));
        slapi_rdn_free(&rdn);
        rc= slapi_setbit_int(rc,SLAPI_RTN_BIT_FETCH_EXISTING_DN_ENTRY);

    }
    return rc;
}

static int
urp_add_check_tombstone (Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, CSN *opcsn)
{
    int rc = 0;
    int op_result;
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *newpb;
    char *basedn = slapi_entry_get_ndn(entry);
    char *escaped_filter;
    const Slapi_DN *suffix = slapi_get_suffix_by_dn(slapi_entry_get_sdn (entry));
    escaped_filter = slapi_filter_escape_filter_value("nscpentrydn", basedn);

    char *filter = slapi_filter_sprintf("(&(objectclass=nstombstone)%s)", escaped_filter);
    slapi_ch_free((void **)&escaped_filter);
    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(newpb,
                                 slapi_sdn_get_dn(suffix), /* Base DN */
                                 LDAP_SCOPE_SUBTREE,
                                 filter,
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ( (op_result != LDAP_SUCCESS) || (entries == NULL) )
    {
        /* Log a message */
        goto done;
    }
    for (int i = 0; entries && (entries[i] != NULL); i++) {
        /* we need to distinguish between normal tombstones abd cenotaphs */
        int is_cenotaph;
        CSN *from_csn = NULL;
        CSN *to_csn = NULL;
        char *to_value = NULL;
        char *from_value = (char*)slapi_entry_attr_get_ref(entries[i], "cenotaphfrom");
        if (from_value) {
            is_cenotaph = 1;
            from_csn = csn_new_by_string(from_value);
            to_value = (char*)slapi_entry_attr_get_ref(entries[i], "cenotaphto");
            to_csn = csn_new_by_string(to_value);
        } else {
            is_cenotaph = 0;
            from_csn = csn_dup(entry_get_dncsn(entries[i]));
            to_value = (char*)slapi_entry_attr_get_ref(entries[i], "nstombstonecsn");
            to_csn = csn_new_by_string(to_value);
        }
        if (csn_compare(from_csn, opcsn) < 0 &&
            csn_compare(to_csn, opcsn) > 0 ) {
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                           "urp_add_check_tombstone - found conflicting tombstone (%s).\n",
                           slapi_entry_get_dn_const(entries[i]));
            rc = 1;
        } else if (!is_cenotaph && csn_compare(opcsn, from_csn) < 0) {
            /* the tombstone is from an entry added later than the received one */
            Slapi_Value *tomb_value;
            const char *adduniqueid = slapi_entry_get_uniqueid (entries[i]);
            const char *base_dn = slapi_entry_get_dn_const (entry);
            char *newrdn = get_rdn_plus_uniqueid ( sessionid, base_dn, adduniqueid );
            char *parentdn = slapi_dn_parent_ext(slapi_entry_get_dn_const(entries[i]),1);
            char *conflict_dn = slapi_ch_smprintf("%s,%s",newrdn, parentdn);
            slapi_log_err(SLAPI_LOG_REPL, sessionid,
                           "urp_add_check_tombstone - found tombstone for newer entry(%s) create conflict (%s).\n",
                           slapi_entry_get_dn_const(entries[i]),conflict_dn);
            Slapi_DN *conflict_sdn = slapi_sdn_new_dn_byval(conflict_dn);
            /* need to add the tombstone value here since we need the deletion csn */
            tomb_value = slapi_value_new_string(SLAPI_ATTR_VALUE_TOMBSTONE);
            value_update_csn(tomb_value, CSN_TYPE_VALUE_UPDATED, entry_get_deletion_csn(entries[i]));
            slapi_entry_add_value(entry, SLAPI_ATTR_OBJECTCLASS, tomb_value);
            slapi_value_free(&tomb_value);

            /* now turn the existing tombstone into a conflict */
            tombstone_to_conflict(sessionid, slapi_entry_dup(entries[i]), conflict_sdn, "tombstoneToConflict", opcsn, NULL);
            slapi_pblock_set(pb, SLAPI_URP_TOMBSTONE_CONFLICT_DN, conflict_dn);
            slapi_ch_free_string(&newrdn);
            slapi_ch_free_string(&parentdn);
            slapi_sdn_free(&conflict_sdn);
            rc = 2;
        }
        csn_free(&from_csn);
        csn_free(&to_csn);

        if (rc) break;
    }

done:
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    if (filter) {
        PR_smprintf_free(filter);
    }
    return rc;
}

static int
urp_delete_check_conflict (char *sessionid, Slapi_Entry *tombstone_entry, CSN *opcsn)
{
    int rc = 0;
    int op_result = 0;
    char *filter = NULL;
    Slapi_PBlock *newpb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry *conflict_e = NULL;
    char *validdn = (char*)slapi_entry_attr_get_ref(tombstone_entry, "nscpentrydn");
    char *parent_dn = slapi_dn_parent (validdn);

    filter = slapi_filter_sprintf("(&(objectclass=ldapsubentry)(%s=%s (ADD) %s%s))", ATTR_NSDS5_REPLCONFLICT, REASON_ANNOTATE_DN,
                                  ESC_NEXT_VAL, validdn);
    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(newpb,
                                 parent_dn,
                                 LDAP_SCOPE_SUBTREE,
                                 filter,
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if ((op_result != LDAP_SUCCESS) || (entries == NULL) || entries[0] == NULL) {
        /* Log a message */
        goto done;
    }

    conflict_e = slapi_entry_dup(entries[0]);
    conflict_to_tombstone(sessionid, conflict_e, opcsn);
    slapi_entry_free(conflict_e);
    rc = 1;
done:
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    newpb = NULL;
    if (filter) {
        PR_smprintf_free(filter);
    }

    slapi_ch_free_string(&parent_dn);
    return rc;
}

static char*
urp_find_valid_entry_to_delete(Slapi_PBlock *pb, const Slapi_Entry *deleteentry, char *sessionid, CSN *opcsn)
{
    Slapi_DN *sdnp = NULL;
    const char *dn;
    char *delete_nsuniqueid = NULL;
    Slapi_PBlock *newpb;
    const CSN *dncsn;
    Slapi_Entry **entries = NULL;
    int op_result;

    slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdnp);
    dn = slapi_sdn_get_dn(sdnp);
    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(newpb,
                                 dn, /* Base DN */
                                 LDAP_SCOPE_BASE,
                                 "objectclass=*",
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((op_result != LDAP_SUCCESS) || (entries == NULL) || entries[0] == NULL) {
        /* Log a message */
        goto done;
    }
    dncsn = entry_get_dncsn(entries[0]);
    if (dncsn && (csn_compare(dncsn, opcsn) < 0 )) {
        delete_nsuniqueid = slapi_entry_attr_get_charptr(entries[0], "nsuniqueid");
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                    "urp_find_valid_entry_to_delete - found (%s) (%s).\n",
            delete_nsuniqueid, slapi_entry_get_dn_const(entries[0]));
    }

done:
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    return delete_nsuniqueid;
}

static char*
urp_find_tombstone_for_glue (Slapi_PBlock *pb, char *sessionid, const Slapi_Entry *entry, Slapi_DN *parentdn, CSN *opcsn)
{
    char *tombstone_nsuniqueid = NULL;
    int op_result;
    int rc = LDAP_SUCCESS;;
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *newpb;
    const char *basedn = slapi_sdn_get_dn(parentdn);
    char *escaped_filter;
    escaped_filter = slapi_filter_escape_filter_value("nscpentrydn", (char *)basedn);

    char *conflict_csnstr = (char*)slapi_entry_attr_get_ref((Slapi_Entry *)entry, "conflictcsn");
    CSN *conflict_csn = csn_new_by_string(conflict_csnstr);
    CSN *tombstone_csn = NULL;

    char *filter = slapi_filter_sprintf("(&(objectclass=nstombstone)%s)", escaped_filter);
    slapi_ch_free((void **)&escaped_filter);
    newpb = slapi_pblock_new();
    char *parent_dn = slapi_dn_parent (basedn);
    slapi_search_internal_set_pb(newpb,
                                 parent_dn, /* Base DN */
                                 LDAP_SCOPE_SUBTREE,
                                 filter,
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ( (op_result != LDAP_SUCCESS) || (entries == NULL) )
    {
        /* Log a message */
        goto done;
    }
    for (int i = 0; entries && (entries[i] != NULL); i++) {
        char *tombstone_csn_value = (char*)slapi_entry_attr_get_ref(entries[i], "nstombstonecsn");
        if (tombstone_csn_value) {
            csn_free(&tombstone_csn);
            tombstone_csn = csn_new_by_string(tombstone_csn_value);
            if( csn_compare(tombstone_csn, conflict_csn) > 0 ) {
                slapi_log_err(SLAPI_LOG_REPL, sessionid,
                               "urp_find_tombstone_for_glue - found tombstone newer than conflict (%s).\n",
                               slapi_entry_get_dn_const(entries[i]));
                tombstone_nsuniqueid = slapi_entry_attr_get_charptr(entries[i], "nsuniqueid");
                rc = tombstone_to_glue (pb, sessionid, entries[i], parentdn, REASON_RESURRECT_ENTRY, opcsn, NULL);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR, sessionid,
                                  "urp_resolve parent entry: failed to create glue from tombstone %s\n", slapi_sdn_get_dn(parentdn));
                    slapi_ch_free_string(&tombstone_nsuniqueid);
                    tombstone_nsuniqueid = NULL;
                }
                break;
            }
        }
    }
    csn_free(&tombstone_csn);

done:
    slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    csn_free(&conflict_csn);
    slapi_ch_free_string(&parent_dn);
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    if (filter) {
        PR_smprintf_free(filter);
    }
    return tombstone_nsuniqueid;
}

static int
urp_conflict_to_glue (char *sessionid, const Slapi_Entry *entry, Slapi_DN *parentdn, CSN *opcsn)
{
    int rc = 0;
    int op_result;
    const char *newrdn;
    Slapi_RDN *parentrdn = slapi_rdn_new();
    const char *basedn;

    basedn = slapi_entry_get_dn_const (entry);
    slapi_sdn_get_rdn(parentdn, parentrdn);
    newrdn = slapi_rdn_get_rdn(parentrdn);
    if(newrdn) {
        del_replconflict_attr(entry, opcsn, 0);
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_conflict_to_glue - %s --> %s\n", basedn, newrdn);
        op_result = urp_fixup_rename_entry ( entry, newrdn, NULL, 0 );
        switch(op_result) {
        case LDAP_SUCCESS:
            break;
        case LDAP_NO_SUCH_OBJECT:
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_conflict_to_glue failed(%d) - %s --> %s\n", op_result, basedn, newrdn);
            rc = LDAP_NO_SUCH_OBJECT;
            break;
        default: 
            slapi_log_err(slapi_log_urp, sessionid,
                           "urp_conflict_to_glue failed(%d) - %s --> %s\n", op_result, basedn, newrdn);
            rc = 1;
        }
    }
    slapi_rdn_free(&parentrdn);
    return rc;
}
/* 
 * urp_annotate_dn:
 * Returns 0 on failure
 * Returns > 0 on success (1 on general conflict resolution success, LDAP_NO_SUCH_OBJECT on no-conflict success)
 *
 * Use this function to annotate an existing entry only. To annotate
 * a new entry (the operation entry) see urp_add_operation.
 */
static int
urp_annotate_dn(char *sessionid, const Slapi_Entry *entry, CSN *opcsn, const char *optype, char **conflict_dn)
{
    int rc = 0; /* Fail */
    int op_result;
    char *newrdn;
    const char *uniqueid;
    const Slapi_DN *basesdn;
    const char *basedn;

    uniqueid = slapi_entry_get_uniqueid(entry);
    basesdn = slapi_entry_get_sdn_const(entry);
    basedn = slapi_entry_get_dn_const(entry);
    newrdn = get_rdn_plus_uniqueid(sessionid, basedn, uniqueid);
    if (conflict_dn) {
        *conflict_dn = NULL;
    }
    if (newrdn) {
        mod_namingconflict_attr(uniqueid, basesdn, basesdn, opcsn, optype);
        mod_objectclass_attr(uniqueid, basesdn, basesdn, opcsn, optype);
        slapi_log_err(slapi_log_urp, sessionid,
                      "urp_annotate_dn - %s --> %s\n", basedn, newrdn);
        op_result = urp_fixup_rename_entry(entry, newrdn, NULL, 0);
        switch (op_result) {
        case LDAP_SUCCESS:
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_annotate_dn - Naming conflict %s. Renamed existing entry to %s\n",
                          optype, newrdn);
            if (conflict_dn) {
                *conflict_dn = slapi_ch_smprintf("%s,%s",newrdn,slapi_dn_find_parent(basedn));
            }
            rc = 1;
            break;
        case LDAP_NO_SUCH_OBJECT:
            /* This means that entry did not really exist!!!
             * This is clearly indicating that there is a
             * get_copy_of_entry -> dn2entry returned
             * an entry (entry) that was already removed
             * from the ldbm database...
             * This is bad, because it clearly indicates
             * some kind of db or cache corruption. We need to print
             * this fact clearly in the errors log to try
             * to solve this corruption one day.
             * However, as far as the conflict is concerned,
             * this error is completely harmless:
             * if thew entry did not exist in the first place,
             * there was never a room
             * for a conflict!! After fix for 558293, this
             * state can't be reproduced anymore (5-Oct-01)
             */
            slapi_log_err(SLAPI_LOG_ERR, sessionid,
                          "urp_annotate_dn - Entry %s exists in cache but not in DB\n",
                          basedn);
            rc = LDAP_NO_SUCH_OBJECT;
            break;
        default:
            slapi_log_err(slapi_log_urp, sessionid,
                          "urp_annotate_dn - Failed to annotate %s, err=%d\n",
                          newrdn, op_result);
        }
        slapi_ch_free((void **)&newrdn);
    } else {
        slapi_log_err(SLAPI_LOG_ERR, sessionid,
                      "urp_annotate_dn - Failed to create conflict DN from basedn: %s and uniqueid: %s\n",
                      basedn, uniqueid);
    }
    return rc;
}

static char *
urp_get_valid_parent_nsuniqueid (Slapi_DN *parentdn)
{
    Slapi_PBlock *newpb = NULL;
    Slapi_Entry **entries = NULL;
    int op_result = LDAP_SUCCESS;
    char *nsuid = NULL;

    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(newpb,
                                 slapi_sdn_get_dn(parentdn),
                                 LDAP_SCOPE_BASE,
                                 "objectclass=*",
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if ( (op_result != LDAP_SUCCESS) || (entries == NULL) ) {
        /* Log a message */
        goto done;
    }

    nsuid = slapi_entry_attr_get_charptr(entries[0], "nsuniqueid");
done:
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    newpb = NULL;

    return nsuid;
}

static int
urp_rename_conflict_children(const char *old_parent, const Slapi_DN *new_parent)
{
    Slapi_PBlock *newpb = NULL;
    Slapi_Entry **entries = NULL;
    int op_result = LDAP_SUCCESS;
    int i;

    newpb = slapi_pblock_new();
    slapi_search_internal_set_pb(newpb,
                                 old_parent,
                                 LDAP_SCOPE_ONELEVEL,
                                 "(|(objectclass=*)(objectclass=ldapsubentry))",
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 NULL, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if ((op_result != LDAP_SUCCESS) || (entries == NULL) || entries[0] == NULL) {
        /* Log a message */
        goto done;
    }
    for (i = 0; NULL != entries[i]; i++) {
        op_result = urp_fixup_move_entry(entries[i],new_parent,0);
        slapi_log_err(SLAPI_LOG_REPL, "session test",
                                     "urp_rename_conflict children - Renaming: %s, Result: %d\n",
                                      slapi_entry_get_dn_const(entries[i]), op_result);
    }

done:
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    newpb = NULL;

    return op_result;
}

/*
 * An URP Naming Collision helper function. Retrieves a list of entries
 * that have the given dn excluding the unique id of the entry. Any
 * entries returned will be entries that have been added with the same
 * dn, but caused a naming conflict when replicated. The URP to fix
 * this constraint violation is to append the unique id of the entry
 * to its RDN.
 */
static Slapi_Entry *
urp_get_min_naming_conflict_entry(Slapi_PBlock *pb, const char *collisiondn, char *sessionid, CSN *opcsn)
{
    Slapi_PBlock *newpb = NULL;
    LDAPControl **server_ctrls = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry *min_naming_conflict_entry = NULL;
    const CSN *min_csn = NULL;
    char *filter = NULL;
    char *parent_dn = NULL;
    const char *basedn;
    int i = 0;
    int min_i = -1;
    int op_result = LDAP_SUCCESS;

    if (collisiondn) {
        basedn = collisiondn;
    } else {
        slapi_pblock_get (pb, SLAPI_URP_NAMING_COLLISION_DN, &basedn);
    }
    if (NULL == basedn || strncmp (basedn, SLAPI_ATTR_UNIQUEID, strlen(SLAPI_ATTR_UNIQUEID)) == 0)
        return NULL;

    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                  "urp_get_min_naming_conflict_entry - %s\n", basedn);

    filter = slapi_filter_sprintf("(&(objectclass=ldapsubentry)(%s=%s (ADD) %s%s))", ATTR_NSDS5_REPLCONFLICT, REASON_ANNOTATE_DN,
                                  ESC_NEXT_VAL, basedn);

    /* server_ctrls will be freed when newpb is destroyed */
    server_ctrls = (LDAPControl **)slapi_ch_calloc (2, sizeof (LDAPControl *));
    server_ctrls[0] = create_managedsait_control();
    server_ctrls[1] = NULL;
    
    newpb = slapi_pblock_new();
    parent_dn = slapi_dn_parent (basedn);
    slapi_search_internal_set_pb(newpb,
                                 parent_dn, /* Base DN */
                                 LDAP_SCOPE_ONELEVEL,
                                 filter,
                                 NULL, /* Attrs */
                                 0, /* AttrOnly */
                                 server_ctrls, /* Controls */
                                 NULL, /* UniqueID */
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
                                 0);
    slapi_search_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ( (op_result != LDAP_SUCCESS) || (entries == NULL) ) {
        /* Log a message */
        goto done;
    }
    /* For all entries, get the one with the smallest dn csn */
    for (i = 0; NULL != entries[i]; i++) {
        const CSN *dncsn;
        dncsn = entry_get_dncsn(entries[i]);
        if ((dncsn != opcsn) && (csn_compare(dncsn, opcsn) > 0 ) &&
            ((min_csn == NULL) || (csn_compare(dncsn, min_csn) < 0)) &&
            !is_tombstone_entry (entries[i])) {
            min_csn = dncsn;
            min_i = i;
        }
        /*
         * If there are too many conflicts, the current urp code has no
         * guarantee for all servers to converge anyway, because the
         * urp and the backend can't be done in one transaction due
         * to either performance or the deadlock problem.
         * Don't sacrifice the performance too much for impossible.
         */
        if (min_csn && i > 5) {
            break;
        }
    }
    
    if (min_csn != NULL) {
        /* Found one entry */
        min_naming_conflict_entry = slapi_entry_dup(entries[min_i]);
    }

done:
    slapi_ch_free_string(&parent_dn);
    if (filter) {
        PR_smprintf_free(filter);
    }
    slapi_free_search_results_internal(newpb);
    slapi_pblock_destroy(newpb);
    newpb = NULL;

    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                  "urp_get_min_naming_conflict_entry - Found %d entries\n", min_csn?1:0);

    return min_naming_conflict_entry;
}

/*
 * If an entry is deleted or renamed, a new winner may be
 * chosen from its naming competitors.
 * The entry with the smallest dncsn restores its original DN.
 */
static int
urp_naming_conflict_removal(Slapi_PBlock *pb, char *sessionid, CSN *opcsn, const char *optype)
{
    Slapi_Entry *min_naming_conflict_entry;
    Slapi_RDN *oldrdn, *newrdn;
    const char *oldrdnstr, *newrdnstr;
    int op_result;

    /*
     * Backend op has set SLAPI_URP_NAMING_COLLISION_DN to the basedn.
     */
    min_naming_conflict_entry = urp_get_min_naming_conflict_entry(pb, NULL, sessionid, opcsn);
    if (min_naming_conflict_entry == NULL) {
        return 0;
    }

    /* EXPERIMENT - do step 2 before 1 */

    /* Step2: Remove ATTR_NSDS5_REPLCONFLICT from the winning entry */
    /*
     * A fixup op will not invoke urp_modrdn_operation(). Even it does,
     * urp_modrdn_operation() will do nothing because of the same CSN.
     */
    op_result = del_replconflict_attr(min_naming_conflict_entry, opcsn, OP_FLAG_ACTION_INVOKE_FOR_REPLOP);
    if (op_result != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_REPL, sessionid,
                      "urp_naming_conflict_removal - Failed to remove nsds5ReplConflict for %s, err=%d\n",
                      slapi_entry_get_ndn(min_naming_conflict_entry), op_result);
    }
    /* end Step 2 */
    /* Step 1: Restore the entry's original DN */

    oldrdn = slapi_rdn_new_sdn(slapi_entry_get_sdn_const(min_naming_conflict_entry));
    oldrdnstr = slapi_rdn_get_rdn(oldrdn);

    slapi_log_err(SLAPI_LOG_REPL, sessionid,
                   "urp_naming_conflict_removal - Found %s\n", slapi_entry_get_ndn(min_naming_conflict_entry));
    /* newrdnstr is the old rdn of the entry minus the nsuniqueid part */
    newrdn = slapi_rdn_new_rdn ( oldrdn );
    slapi_rdn_remove_attr (newrdn, SLAPI_ATTR_UNIQUEID );
    newrdnstr = slapi_rdn_get_rdn(newrdn);        

    /*
     * Set OP_FLAG_ACTION_INVOKE_FOR_REPLOP since this operation
     * is done after DB lock was released. The backend modrdn
     * will acquire the DB lock if it sees this flag.
     */
    op_result = urp_fixup_rename_entry((const Slapi_Entry *)min_naming_conflict_entry, newrdnstr, NULL, OP_FLAG_ACTION_INVOKE_FOR_REPLOP);
    if ( op_result != LDAP_SUCCESS ) {
        slapi_log_err(slapi_log_urp, sessionid,
            "urp_naming_conflict_removal - Failed to restore RDN of %s, err=%d\n", oldrdnstr, op_result);
        goto bailout;
    }
    slapi_log_err(slapi_log_urp, sessionid,
                  "urp_naming_conflict_removal - Naming conflict removed by %s. RDN of %s was restored\n", optype, oldrdnstr);
            
    /* end Step 1 */

bailout:
    slapi_entry_free(min_naming_conflict_entry);
    slapi_rdn_free(&oldrdn);
    slapi_rdn_free(&newrdn);
    return op_result;
}

/* The returned value is either null or "uniqueid=<uniqueid>+<basedn>" */
static char *
get_dn_plus_uniqueid(char *sessionid, const Slapi_DN *oldsdn, const char *uniqueid)
{
    Slapi_RDN *rdn = slapi_rdn_new();
    char *newdn = NULL;
    int rc = slapi_rdn_init_all_sdn_ext(rdn, oldsdn, 1);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, sessionid,
                      "Failed to convert %s to RDN\n", slapi_sdn_get_dn(oldsdn));
        goto bail;
    }

    PR_ASSERT(uniqueid != NULL);

    /* Check if the RDN already contains the Unique ID */
    if (slapi_rdn_is_conflict(rdn)) {
        /* The Unique ID is already in the RDN.
         * This is a highly improbable collision.
         * It suggests that a duplicate UUID was generated.
         * This will cause replication divergence and will
         * require admin intercession
         */
        slapi_log_err(SLAPI_LOG_WARNING, sessionid,
                      "get_dn_plus_uniqueid - Annotated DN %s has naming conflict\n", slapi_sdn_get_dn(oldsdn));
    } else {
        char *parentdn = slapi_dn_parent(slapi_sdn_get_dn(oldsdn));
        slapi_rdn_add(rdn, SLAPI_ATTR_UNIQUEID, uniqueid);
        /*
         * using slapi_ch_smprintf is okay since ...
         * uniqueid in rdn is normalized and
         * parentdn is normalized by slapi_sdn_get_dn.
         */
        newdn = slapi_ch_smprintf("%s,%s", slapi_rdn_get_rdn(rdn), parentdn);
        slapi_ch_free_string(&parentdn);
    }
bail:
    slapi_rdn_free(&rdn);
    return newdn;
}

char *
get_rdn_plus_uniqueid(char *sessionid, const char *olddn, const char *uniqueid)
{
    char *newrdn = NULL;
    Slapi_RDN *rdn = NULL;
    /* Check if the RDN already contains the Unique ID */
    rdn = slapi_rdn_new_dn(olddn);
    if (rdn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, sessionid,
                      "Failed to convert %s to RDN\n", olddn);
        goto bail;
    }

    PR_ASSERT(uniqueid!=NULL);
    if (slapi_rdn_is_conflict(rdn)) {
        /* The Unique ID is already in the RDN.
         * This is a highly improbable collision.
         * It suggests that a duplicate UUID was generated.
         * This will cause replication divergence and will
         * require admin intercession
         */
        slapi_log_err(SLAPI_LOG_WARNING, sessionid,
                      "get_rdn_plus_uniqueid - Annotated RDN %s has naming conflict\n", olddn);
    } else {
        slapi_rdn_add(rdn,SLAPI_ATTR_UNIQUEID,uniqueid);
        newrdn = slapi_ch_strdup(slapi_rdn_get_rdn(rdn));
    }
bail:
    slapi_rdn_free(&rdn);
    return newrdn;
}

static int
is_deleted_at_csn(const Slapi_Entry *entry, CSN *opcsn)
{
    int rc = 0;
    char *tombstone_csnstr = (char*)slapi_entry_attr_get_ref((Slapi_Entry *)entry, "nstombstonecsn");
    CSN *tombstone_csn = csn_new_by_string(tombstone_csnstr);

    if (csn_compare (tombstone_csn, opcsn) == 0) rc = 1;

    csn_free(&tombstone_csn);

    return rc;
}

int
is_conflict_entry(const Slapi_Entry *entry)
{
    int is_conflict = 0;
    if (slapi_entry_attr_get_ref((Slapi_Entry *)entry, ATTR_NSDS5_REPLCONFLICT)) {
        is_conflict = 1;
    }
    return is_conflict;
}

static int
is_renamed_entry(Slapi_PBlock *pb, Slapi_Entry *entry, CSN *opcsn)
{
    int rc = 0;
    Slapi_DN *del_dn = NULL;
    slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &del_dn);
    if (slapi_sdn_compare(del_dn, slapi_entry_get_sdn(entry))) {
        /* the existing entry was renamed, check if it was before the delete */
        if (csn_compare (entry_get_dncsn(entry), opcsn) < 0) rc = 1;
    }

    return rc;
}

static int
is_suffix_entry(Slapi_PBlock *pb, Slapi_Entry *entry, Slapi_DN **parentdn)
{
    return is_suffix_dn(pb, slapi_entry_get_sdn(entry), parentdn);
}

int
is_suffix_dn_ext(Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parentdn, int is_tombstone)
{
    Slapi_Backend *backend;
    int rc;

    *parentdn = slapi_sdn_new();
    slapi_pblock_get(pb, SLAPI_BACKEND, &backend);
    slapi_sdn_get_backend_parent_ext(dn, *parentdn, backend, is_tombstone);

    /* A suffix entry doesn't have parent dn */
    rc = slapi_sdn_isempty(*parentdn) ? 1 : 0;

    return rc;
}

int
is_suffix_dn(Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parentdn)
{
    return is_suffix_dn_ext(pb, dn, parentdn, 0);
}

static int
mod_namingconflict_attr(const char *uniqueid, const Slapi_DN *entrysdn, const Slapi_DN *conflictsdn, CSN *opcsn, const char *optype)
{
    Slapi_Mods smods;
    char buf[BUFSIZ];
    int op_result;

    PR_snprintf(buf, sizeof(buf), "%s (%s) %s",
                 REASON_ANNOTATE_DN, optype, slapi_sdn_get_dn(conflictsdn));
    slapi_mods_init(&smods, 2);
    if ( strncmp(slapi_sdn_get_dn(entrysdn), SLAPI_ATTR_UNIQUEID,
         strlen(SLAPI_ATTR_UNIQUEID)) != 0 ) {
        slapi_mods_add(&smods, LDAP_MOD_ADD, ATTR_NSDS5_REPLCONFLICT, strlen(buf), buf);
    } else {
        /*
         * If the existing entry is already a naming conflict loser,
         * the following replace operation should result in the
         * replace of the ATTR_NSDS5_REPLCONFLICT index as well.
         */
        slapi_mods_add(&smods, LDAP_MOD_REPLACE, ATTR_NSDS5_REPLCONFLICT, strlen(buf), buf);
    }
    op_result = urp_fixup_modify_entry(uniqueid, entrysdn, opcsn, &smods, 0);
    slapi_mods_done(&smods);
    return op_result;
}

static int
mod_objectclass_attr(const char *uniqueid, const Slapi_DN *entrysdn, const Slapi_DN *conflictsdn, CSN *opcsn, const char *optype)
{
    Slapi_Mods smods;
    int op_result;
    char csnstr[CSN_STRSIZE+1] = {0};

    slapi_mods_init(&smods, 3);
    slapi_mods_add_string(&smods, LDAP_MOD_ADD, "objectclass", "ldapsubentry");
    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "conflictcsn", csn_as_string(opcsn, PR_FALSE, csnstr));
    op_result = urp_fixup_modify_entry(uniqueid, entrysdn, opcsn, &smods, 0);
    slapi_mods_done(&smods);
    if (op_result == LDAP_TYPE_OR_VALUE_EXISTS) {
        /* the objectclass was already present */
        op_result = LDAP_SUCCESS;
    }
    return op_result;
}

static int
del_replconflict_attr(const Slapi_Entry *entry, CSN *opcsn, int opflags)
{
    Slapi_Attr *attr;
    int op_result = 0;

    if (slapi_entry_attr_find(entry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0) {
        Slapi_Mods smods;
        const char *uniqueid;
        const Slapi_DN *entrysdn;

        uniqueid = slapi_entry_get_uniqueid(entry);
        entrysdn = slapi_entry_get_sdn_const(entry);
        slapi_mods_init(&smods, 3);
        slapi_mods_add(&smods, LDAP_MOD_DELETE, ATTR_NSDS5_REPLCONFLICT, 0, NULL);
        slapi_mods_add(&smods, LDAP_MOD_DELETE, "objectclass", strlen("ldapsubentry"),"ldapsubentry");
        op_result = urp_fixup_modify_entry(uniqueid, entrysdn, opcsn, &smods, opflags);
        slapi_mods_done(&smods);
    }
    return op_result;
}
