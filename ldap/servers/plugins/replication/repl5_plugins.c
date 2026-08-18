/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
 * repl5_consumer.c - consumer plugin functions
 */

/*
 * LDAP Data Model Constraints...
 *
 * 1) Parent entry must exist.
 * 2) RDN must be unique.
 * 3) RDN components must be attribute values.
 * 4) Must conform to the schema constraints - Single Valued Attributes.
 * 5) Must conform to the schema constraints - Required Attributes.
 * 6) Must conform to the schema constraints - No Extra Attributes.
 * 7) Duplicate attribute values are not permited.
 * 8) Cycle in the ancestry graph not permitted.
 *
 * Update Resolution Procedures...
 * 1) ...
 * 2) Add the UniqueID to the RDN.
 * 3) Remove the not present RDN component.
 *    Use the UniqueID if the RDN becomes empty.
 * 4) Keep the most recent value.
 * 5) Ignore.
 * 6) Ignore.
 * 7) Keep the largest CSN for the duplicate value.
 * 8) Don't check for this.
 */

#include "repl5.h"
#include "cl5_api.h"
#include "urp.h"
#include "csnpl.h"

static char *local_purl = NULL;
static char *purl_attrs[] = {"nsslapd-localhost", "nsslapd-port", "nsslapd-secureport", NULL};

/* Forward declarations */
static void copy_operation_parameters(Slapi_PBlock *pb);
static int write_changelog_and_ruv(Slapi_PBlock *pb);
static int process_postop(Slapi_PBlock *pb);
static int cancel_opcsn(Slapi_PBlock *pb);
static int ruv_tombstone_op(Slapi_PBlock *pb);
static PRBool process_operation(Slapi_PBlock *pb, const CSN *csn);
static PRBool is_mmr_replica(Slapi_PBlock *pb);
static const char *replica_get_purl_for_op(const Replica *r, Slapi_PBlock *pb, const CSN *opcsn);

/*
 * XXXggood - what to do if both ssl and non-ssl ports available? How
 * do you know which to use? Offer a choice in replication config?
 */
int
multisupplier_set_local_purl()
{
    int rc = 0;
    Slapi_Entry **entries;
    Slapi_PBlock *pb = NULL;

    pb = slapi_pblock_new();

    slapi_search_internal_set_pb(pb, "cn=config", LDAP_SCOPE_BASE,
                                 "objectclass=*", purl_attrs, 0, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "multisupplier_set_local_purl - "
                                                       "unable to read server configuration: error %d\n",
                      rc);
    } else {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (NULL == entries || NULL == entries[0]) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "multisupplier_set_local_purl - "
                                                           "Server configuration missing\n");
            rc = -1;
        } else {
            char *host = (char *)slapi_entry_attr_get_ref(entries[0], "nsslapd-localhost");
            char *port = (char *)slapi_entry_attr_get_ref(entries[0], "nsslapd-port");
            char *sslport = (char *)slapi_entry_attr_get_ref(entries[0], "nsslapd-secureport");
            if (host == NULL || ((port == NULL && sslport == NULL))) {
                slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name,
                              "multisupplier_set_local_purl - Invalid server "
                              "configuration\n");
            } else {
                if (slapi_is_ipv6_addr(host)) {
                    /* need to put brackets around the ipv6 address */
                    local_purl = slapi_ch_smprintf("ldap://[%s]:%s", host, port);
                } else {
                    local_purl = slapi_ch_smprintf("ldap://%s:%s", host, port);
                }
            }
        }
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return rc;
}


const char *
multisupplier_get_local_purl()
{
    return local_purl;
}


/* ================= Multisupplier Pre-Op Plugin Points ================== */


int
multisupplier_preop_bind(Slapi_PBlock *pb __attribute__((unused)))
{
    return 0;
}

int
multisupplier_preop_add(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (!is_mmr_replica(pb)) {
        copy_operation_parameters(pb);
        return SLAPI_PLUGIN_SUCCESS;
    }

    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    if (is_replicated_operation) {
        if (!is_fixup_operation) {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id(pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp) {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                char *superior_uuid = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, &superior_uuid, &csn, NULL /* modrdn_mods */);
                if (-1 == drc) {
                    slapi_log_err(SLAPI_LOG_ERR, REPLICATION_SUBSYSTEM,
                                  "multisupplier_preop_add - %s An error occurred while decoding the replication update "
                                  "control - Add\n",
                                  sessionid);
                } else if (1 == drc) {
                    /*
                     * For add operations, we just set the operation csn. The entry's
                     * uniqueid should already be an attribute of the replicated entry.
                     */
                    struct slapi_operation_parameters *op_params;

                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation(pb, csn)) {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0,
                                               "replication operation not processed, replica unavailable "
                                               "or csn ignored",
                                               0, 0);
                        csn_free(&csn);
                        slapi_ch_free((void **)&target_uuid);
                        slapi_ch_free((void **)&superior_uuid);

                        return SLAPI_PLUGIN_FAILURE;
                    }

                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                    slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
                    /* JCMREPL - Complain if there's no superior uuid */
                    op_params->p.p_add.parentuniqueid = superior_uuid; /* JCMREPL - Not very elegant */
                    /* JCMREPL - When do these things get free'd? */
                    if (target_uuid != NULL) {
                        /*
                         * Make sure that the Unique Identifier in the Control is the
                         * same as the one in the entry.
                         */
                        Slapi_Entry *addentry;
                        const char *entry_uuid;
                        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &addentry);
                        entry_uuid = slapi_entry_attr_get_ref(addentry, SLAPI_ATTR_UNIQUEID);
                        if (entry_uuid == NULL) {
                            /* Odd that the entry doesn't have a Unique Identifier. But, we can fix it. */
                            slapi_entry_set_uniqueid(addentry, slapi_ch_strdup(target_uuid)); /* JCMREPL - strdup EVIL! There should be a uuid dup function. */
                        } else {
                            if (strcasecmp(entry_uuid, target_uuid) != 0) {
                                slapi_log_err(SLAPI_LOG_WARNING, REPLICATION_SUBSYSTEM,
                                              "multisupplier_preop_add - %s Replicated Add received with Control_UUID=%s and Entry_UUID=%s.\n",
                                              sessionid, target_uuid, entry_uuid);
                            }
                        }
                    }
                }
            } else {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        } else {
            /* Replicated & Fixup Operation */
        }
    } else {
        slapi_operation_set_csngen_handler(op, (void *)replica_generate_next_csn);
    }

    copy_operation_parameters(pb);

    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_preop_delete(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (!is_mmr_replica(pb)) {
        copy_operation_parameters(pb);
        return SLAPI_PLUGIN_SUCCESS;
    }

    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    if (is_replicated_operation) {
        if (!is_fixup_operation) {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id(pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp) {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, NULL, &csn, NULL /* modrdn_mods */);
                if (-1 == drc) {
                    slapi_log_err(SLAPI_LOG_ERR, REPLICATION_SUBSYSTEM,
                                  "multisupplier_preop_delete - %s An error occurred while decoding the replication update "
                                  "control - Delete\n",
                                  sessionid);
                } else if (1 == drc) {
                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation(pb, csn)) {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0,
                                               "replication operation not processed, replica unavailable "
                                               "or csn ignored",
                                               0, 0);
                        slapi_log_err(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                                      "multisupplier_preop_delete - %s replication operation not processed, replica unavailable "
                                      "or csn ignored\n",
                                      sessionid);
                        csn_free(&csn);
                        slapi_ch_free((void **)&target_uuid);

                        return SLAPI_PLUGIN_FAILURE;
                    }

                    /*
                     * For delete operations, we pass the uniqueid of the deleted entry
                     * to the backend and let it sort out which entry to really delete.
                     * We also set the operation csn.
                     */
                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                }
            } else {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        } else {
            /* Replicated & Fixup Operation */
        }
    } else {
        slapi_operation_set_csngen_handler(op, (void *)replica_generate_next_csn);
    }

    copy_operation_parameters(pb);
    slapi_operation_set_replica_attr_handler(op, (void *)replica_get_attr);

    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_preop_modify(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (!is_mmr_replica(pb)) {
        copy_operation_parameters(pb);
        return SLAPI_PLUGIN_SUCCESS;
    }

    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    if (is_replicated_operation) {
        if (!is_fixup_operation) {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id(pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp) {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, NULL, &csn, NULL /* modrdn_mods */);
                if (-1 == drc) {
                    slapi_log_err(SLAPI_LOG_ERR, REPLICATION_SUBSYSTEM,
                                  "multisupplier_preop_modify - %s An error occurred while decoding the replication update "
                                  "control- Modify\n",
                                  sessionid);
                } else if (1 == drc) {
                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation(pb, csn)) {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0,
                                               "replication operation not processed, replica unavailable "
                                               "or csn ignored",
                                               0, 0);
                        slapi_log_err(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                                      "multisupplier_preop_modify - %s replication operation not processed, replica unavailable "
                                      "or csn ignored\n",
                                      sessionid);
                        csn_free(&csn);
                        slapi_ch_free((void **)&target_uuid);

                        return SLAPI_PLUGIN_FAILURE;
                    }

                    /*
                     * For modify operations, we pass the uniqueid of the modified entry
                     * to the backend and let it sort out which entry to really modify.
                     * We also set the operation csn.
                     */
                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                }
            } else {
                /*  PR_ASSERT(0); JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
                /*
                 *  This could be RI plugin responding to a replicated update from AD or some other supplier that is not
                 *  using the RI plugin, so don't PR_ASSERT here.  This only happens if we configure the RI plugin with
                 *  "nsslapd-pluginAllowReplUpdates: on", also the RI plugin only issues "modifies".
                 */
            }
        } else {
            /* Replicated & Fixup Operation */
        }
    } else {
        slapi_operation_set_csngen_handler(op, (void *)replica_generate_next_csn);
    }

    copy_operation_parameters(pb);
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_preop_modrdn(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    if (!is_mmr_replica(pb)) {
        copy_operation_parameters(pb);
        return SLAPI_PLUGIN_SUCCESS;
    }

    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    if (is_replicated_operation) {
        if (!is_fixup_operation) {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id(pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp) {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                char *newsuperior_uuid = NULL;
                LDAPMod **modrdn_mods = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, &newsuperior_uuid,
                                                             &csn, &modrdn_mods);
                if (-1 == drc) {
                    slapi_log_err(SLAPI_LOG_ERR, REPLICATION_SUBSYSTEM,
                                  "multisupplier_preop_modrdn - %s An error occurred while decoding the replication update "
                                  "control - ModRDN\n",
                                  sessionid);
                } else if (1 == drc) {
                    /*
                     * For modrdn operations, we pass the uniqueid of the entry being
                     * renamed to the backend and let it sort out which entry to really
                     * rename. We also set the operation csn, and if the newsuperior_uuid
                     * was sent, we decode that as well.
                     */
                    struct slapi_operation_parameters *op_params;

                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation(pb, csn)) {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0,
                                               "replication operation not processed, replica unavailable "
                                               "or csn ignored",
                                               0, 0);
                        csn_free(&csn);
                        slapi_ch_free((void **)&target_uuid);
                        slapi_ch_free((void **)&newsuperior_uuid);
                        ldap_mods_free(modrdn_mods, 1);

                        return SLAPI_PLUGIN_FAILURE;
                    }

                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                    slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
                    op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid = newsuperior_uuid; /* JCMREPL - Not very elegant */
                }

                /*
                 * The replicated modrdn operation may also contain a sequence
                 * that contains side effects of the modrdn operation, e.g. the
                 * modifiersname and modifytimestamp are updated.
                 */
                if (NULL != modrdn_mods) {
                    LDAPMod **mods;
                    Slapi_Mods smods;
                    int i;
                    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
                    slapi_mods_init_passin(&smods, mods);
                    for (i = 0; NULL != modrdn_mods[i]; i++) {
                        slapi_mods_add_ldapmod(&smods, modrdn_mods[i]); /* Does not copy mod */
                    }
                    mods = slapi_mods_get_ldapmods_passout(&smods);
                    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
                    slapi_mods_done(&smods);
                    slapi_ch_free((void **)&modrdn_mods); /* Free container only - contents are referred to by array "mods" */
                }
            } else {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        } else {
            /* Replicated & Fixup Operation */
        }
    } else {
        slapi_operation_set_csngen_handler(op, (void *)replica_generate_next_csn);
    }

    copy_operation_parameters(pb);

    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_preop_search(Slapi_PBlock *pb __attribute__((unused)))
{
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_preop_compare(Slapi_PBlock *pb __attribute__((unused)))
{
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_ruv_search(Slapi_PBlock *pb)
{
    Slapi_Entry *e, *e_alt;
    Slapi_DN *suffix_sdn;
    Slapi_Operation *operation;

    slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_ORIG, &e);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);

    if ((e == NULL) || (operation == NULL))
        return SLAPI_PLUGIN_SUCCESS;

    if (!operation_is_flag_set(operation, OP_FLAG_INTERNAL) && is_ruv_tombstone_entry(e)) {
        /* We are about to send back the database RUV, we need to return
                 * in memory RUV instead
                 */

        /* Retrieve the suffix DN from the RUV entry */
        suffix_sdn = slapi_sdn_new();
        slapi_sdn_get_parent(slapi_entry_get_sdn(e), suffix_sdn);

        /* Now set the in memory RUV into the pblock */
        if ((e_alt = get_in_memory_ruv(suffix_sdn)) != NULL) {
            slapi_pblock_set(pb, SLAPI_SEARCH_ENTRY_COPY, e_alt);
        }

        slapi_sdn_free(&suffix_sdn);
    }

    return SLAPI_PLUGIN_SUCCESS;
}

static void
purge_entry_state_information(Slapi_PBlock *pb)
{
    CSN *purge_csn = NULL;
    Replica *replica;

    /* we don't want to  trim RUV tombstone because we will
       deadlock with ourself */
    if (ruv_tombstone_op(pb))
        return;

    replica = replica_get_replica_for_op(pb);
    if (NULL != replica) {
        purge_csn = replica_get_purge_csn(replica);
    }
    if (NULL != purge_csn) {
        Slapi_Entry *e;
        int optype;

        slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
        switch (optype) {
        case SLAPI_OPERATION_MODIFY:
            slapi_pblock_get(pb, SLAPI_MODIFY_EXISTING_ENTRY, &e);
            break;
        case SLAPI_OPERATION_MODRDN:
            /* XXXggood - the following always gives a NULL entry - why? */
            slapi_pblock_get(pb, SLAPI_MODRDN_EXISTING_ENTRY, &e);
            break;
        case SLAPI_OPERATION_DELETE:
            slapi_pblock_get(pb, SLAPI_DELETE_EXISTING_ENTRY, &e);
            break;
        default:
            e = NULL; /* Don't purge on ADD - not needed */
            break;
        }
        if (NULL != e) {
            entry_purge_state_information(e, purge_csn);
            /* conntion is always null */
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                char csn_str[CSN_STRSIZE];
                slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                              "purge_entry_state_information -  From entry %s up to "
                              "CSN %s\n",
                              slapi_entry_get_dn(e),
                              csn_as_string(purge_csn, PR_FALSE, csn_str));
            }
        }
        csn_free(&purge_csn);
    }
}
int

multisupplier_mmr_preop (Slapi_PBlock *pb, int flags)
{
	int rc= SLAPI_PLUGIN_SUCCESS;

    if (!is_mmr_replica(pb)) {
        return rc;
    }

	switch (flags)
	{
	case SLAPI_PLUGIN_BE_PRE_ADD_FN:
		rc = multisupplier_bepreop_add(pb);
		break;
	case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
		rc = multisupplier_bepreop_modify(pb);
		break;
	case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
		rc = multisupplier_bepreop_modrdn(pb);
		break;
	case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
		rc = multisupplier_bepreop_delete(pb);
		break;
	}
	return rc;
}

int
multisupplier_mmr_postop (Slapi_PBlock *pb, int flags)
{
	int rc= SLAPI_PLUGIN_SUCCESS;

    if (!is_mmr_replica(pb)) {
        return rc;
    }

	switch (flags)
	{
	case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
		rc = multisupplier_be_betxnpostop_add(pb);
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
		rc = multisupplier_be_betxnpostop_delete(pb);
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
		rc = multisupplier_be_betxnpostop_modify(pb);
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
		rc = multisupplier_be_betxnpostop_modrdn(pb);
		break;
	}
    if (rc) {
	    slapi_log_err(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                     "multisupplier_mmr_postop - error %d for operation %d.\n", rc, flags);
	}
	return rc;
}

/* pure bepreop's -- should be done before transaction starts */
int
multisupplier_bepreop_add(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    /* For replicated operations, apply URP algorithm */
    if (!is_fixup_operation) {
        slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
                         (void *)replica_ruv_smods_for_op);
        if (is_replicated_operation) {
            rc = urp_add_operation(pb);
        }
    }

    return rc;
}

int
multisupplier_bepreop_delete(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    /* For replicated operations, apply URP algorithm */
    if (!is_fixup_operation) {
        slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
                         (void *)replica_ruv_smods_for_op);
        if (is_replicated_operation) {
            rc = urp_delete_operation(pb);
        }
    }

    return rc;
}

int
multisupplier_bepreop_modify(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    /* For replicated operations, apply URP algorithm */
    if (!is_fixup_operation) {
        slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
                         (void *)replica_ruv_smods_for_op);
        if (is_replicated_operation) {
            rc = urp_modify_operation(pb);
        }
    }

    /* Clean up old state information */
    purge_entry_state_information(pb);

    return rc;
}

int
multisupplier_bepreop_modrdn(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    is_fixup_operation = operation_is_flag_set(op, OP_FLAG_REPL_FIXUP);

    /* For replicated operations, apply URP algorithm */
    if (!is_fixup_operation) {
        slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
                         (void *)replica_ruv_smods_for_op);
        if (is_replicated_operation) {
            rc = urp_modrdn_operation(pb);
        }
    }

    /* Clean up old state information */
    purge_entry_state_information(pb);

    return rc;
}

int
multisupplier_bepostop_add(Slapi_PBlock *pb)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if ( ! operation_is_flag_set (op, OP_FLAG_REPL_FIXUP) ) {
        urp_post_add_operation (pb);
    }
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_bepostop_modrdn(Slapi_PBlock *pb)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
        urp_post_modrdn_operation(pb);
    }
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_bepostop_delete(Slapi_PBlock *pb)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
        urp_post_delete_operation(pb);
    }
    return SLAPI_PLUGIN_SUCCESS;
}

/* postop - write to changelog */
int
multisupplier_postop_bind(Slapi_PBlock *pb __attribute__((unused)))
{
    return SLAPI_PLUGIN_SUCCESS;
}

int
multisupplier_postop_add(Slapi_PBlock *pb)
{
    return process_postop(pb);
}

int
multisupplier_postop_delete(Slapi_PBlock *pb)
{
    return process_postop(pb);
}

int
multisupplier_postop_modify(Slapi_PBlock *pb)
{
    return process_postop(pb);
}

int
multisupplier_postop_modrdn(Slapi_PBlock *pb)
{
    return process_postop(pb);
}

int
multisupplier_betxnpostop_delete(Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multisupplier_betxnpostop_modrdn(Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multisupplier_betxnpostop_add(Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multisupplier_betxnpostop_modify(Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

/* If nsslapd-pluginbetxn is on */
int
multisupplier_be_betxnpostop_delete(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    /* original betxnpost */
    rc = write_changelog_and_ruv(pb);
    /* original bepost */
    rc |= multisupplier_bepostop_delete(pb);
    return rc;
}

int
multisupplier_be_betxnpostop_modrdn(Slapi_PBlock *pb)
{
    int rc = 0;
    /* original betxnpost */
    rc = write_changelog_and_ruv(pb);
    /* original bepost */
    rc |= multisupplier_bepostop_modrdn(pb);
    return rc;
}

int
multisupplier_be_betxnpostop_add(Slapi_PBlock *pb)
{
    int rc = 0;
    /* original betxnpost */
    rc = write_changelog_and_ruv(pb);
    rc |= multisupplier_bepostop_add(pb);
    return rc;
}

int
multisupplier_be_betxnpostop_modify(Slapi_PBlock *pb)
{
    int rc = 0;
    /* original betxnpost */
    rc = write_changelog_and_ruv(pb);
    return rc;
}

/* Helper functions */

/*
 * This function makes a copy of the operation parameters
 * and stashes them in the consumer operation extension.
 * This is where the 5.0 Change Log will get the operation
 * details from.
 */
static void
copy_operation_parameters(Slapi_PBlock *pb)
{
    Slapi_Operation *op = NULL;
    struct slapi_operation_parameters *op_params;
    supplier_operation_extension *opext;
    Replica *replica;

    replica = replica_get_replica_for_op(pb);

    /* we are only interested in the updates to replicas */
    if (NULL == replica) {
        return;
    }
    /* we only save the original operation parameters for replicated operations
       since client operations don't go through urp engine and pblock data can be logged */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (NULL == op) {
        slapi_log_err(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                      "copy_operation_parameters - operation is null.\n");
        return;
    }
    opext = (supplier_operation_extension *)repl_sup_get_ext(REPL_SUP_EXT_OP, op);
    if (operation_is_flag_set(op, OP_FLAG_REPLICATED) &&
        !operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
        slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
        opext->operation_parameters = operation_parameters_dup(op_params);
    }

    /* this condition is needed to avoid re-entering backend serial lock
       when ruv state is updated */
    if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
        /* save replica generation in case it changes */
        opext->repl_gen = replica_get_generation(replica);
    }
}

/*
 * Helper function: update the RUV so that it reflects the
 * locally-processed update. This is called for both replicated
 * and non-replicated operations.
 */
static int
update_ruv_component(Replica *replica, CSN *opcsn, Slapi_PBlock *pb)
{
    char *purl;
    int rc = RUV_NOTFOUND;

    if (!replica || !opcsn)
        return rc;

    /* Replica configured, so update its ruv */
    purl = (char *)replica_get_purl_for_op(replica, pb, opcsn);

    rc = replica_update_ruv(replica, opcsn, purl);

    return rc;
}

/*
 * Write the changelog. Note: it is an error to call write_changelog_and_ruv() for fixup
 * operations. The caller should avoid calling this function if the operation is
 * a fixup op.
 * Also update the ruv - we need to do this while we have the replica lock acquired
 * so that the csn is written to the changelog and the ruv is updated with the csn
 * in one atomic operation - if there is no changelog, we still need to update
 * the ruv (e.g. for consumers)
 */
static int
write_changelog_and_ruv(Slapi_PBlock *pb)
{
    Slapi_Operation *op = NULL;
    CSN *opcsn;
    CSNPL_CTX *prim_csn;
    int rc;
    slapi_operation_parameters *op_params = NULL;
    int return_value = SLAPI_PLUGIN_SUCCESS;
    Replica *r;
    Slapi_Backend *be;
    int is_replicated_operation = 0;

    /* we just let fixup operations through */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (NULL == op) {
        return return_value;
    }
    if ((operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) ||
        (operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY))) {
        return return_value;
    }

    /* ignore operations intended for chaining backends - they will be
       replicated back to us or should be ignored anyway
       replicated operations should be processed normally, as they should
       be going to a local backend */
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (!is_replicated_operation &&
        slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
        return return_value;
    }
    /* we only log changes for operations applied to a replica */
    r = replica_get_replica_for_op(pb);
    if (r == NULL)
        return return_value;

    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    if (rc) { /* op failed - just return */
        cancel_opcsn(pb);
        goto common_return;
    }


    replica_check_release_timeout(r, pb);

    if (replica_is_flag_set(r, REPLICA_LOG_CHANGES)) {
        supplier_operation_extension *opext = NULL;
        cldb_Handle *cldb  = NULL;

        opext = (supplier_operation_extension *)repl_sup_get_ext(REPL_SUP_EXT_OP, op);
        PR_ASSERT(opext);

        /* changelog database information to pass to the write function */
        cldb = replica_get_cl_info(r);
        if (cldb == NULL) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name_cl,
                          "write_changelog_and_ruv - changelog is not initialized\n");
            return return_value;
        }

        /* for replicated operations, we log the original, non-urp data which is
           saved in the operation extension */
        if (operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
            PR_ASSERT(opext->operation_parameters);
            op_params = opext->operation_parameters;
        } else /* since client operations don't go through urp, we log the operation data in pblock */
        {
            Slapi_Entry *e = NULL;
            const char *uniqueid = NULL;

            slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
            if (NULL == op_params) {
                goto common_return;
            }

            /* need to set uniqueid operation parameter */
            /* we try to use the appropriate entry (pre op or post op)
               depending on the type of operation (add, delete, modify)
               However, in some cases, the backend operation may fail in
               a non fatal way (e.g. attempt to remove an attribute value
               that does not exist) but we still need to log the change.
               So, the POST_OP entry may not have been set in the FE modify
               code.  In that case, we use the PRE_OP entry.
            */
            slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
            if ((e == NULL) ||
                (op_params->operation_type == SLAPI_OPERATION_DELETE)) {
                slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
            }
            if (NULL == e) {
                goto common_return;
            }
            uniqueid = slapi_entry_get_uniqueid(e);
            if (NULL == uniqueid) {
                goto common_return;
            }
            op_params->target_address.uniqueid = slapi_ch_strdup(uniqueid);
        }

        if (op_params->csn && is_cleaned_rid(csn_get_replicaid(op_params->csn))) {
            /* this RID has been cleaned */
            if (!operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
                slapi_ch_free((void **)&op_params->target_address.uniqueid);
            }
            goto common_return;
        }

        /* Skip internal operations with no op csn if this is a read-only replica */
        if (op_params->csn == NULL &&
            operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
            replica_get_type(r) == REPLICA_TYPE_READONLY)
        {
            slapi_log_err(SLAPI_LOG_REPL, "write_changelog_and_ruv",
                          "Skipping internal operation on read-only replica\n");
            if (!operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
                slapi_ch_free((void **)&op_params->target_address.uniqueid);
            }
            goto common_return;
        }

        /* we might have stripped all the mods - in that case we do not
           log the operation */
        if (op_params->operation_type != SLAPI_OPERATION_MODIFY ||
            op_params->p.p_modify.modify_mods != NULL) {
            void *txn = NULL;
            char csn_str[CSN_STRSIZE];
            slapi_pblock_get(pb, SLAPI_TXN, &txn);
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "write_changelog_and_ruv - Writing change for "
                          "%s (uniqid: %s, optype: %lu) to changelog csn %s\n",
                          REPL_GET_DN(&op_params->target_address),
                          op_params->target_address.uniqueid,
                          op_params->operation_type,
                          csn_as_string(op_params->csn, PR_FALSE, csn_str));
            rc = cl5WriteOperationTxn(cldb, op_params, txn);
            if (rc != CL5_SUCCESS) {
                /* ONREPL - log error */
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "write_changelog_and_ruv - Can't add a change for "
                              "%s (uniqid: %s, optype: %lu) to changelog csn %s\n",
                              REPL_GET_DN(&op_params->target_address),
                              op_params->target_address.uniqueid,
                              op_params->operation_type,
                              csn_as_string(op_params->csn, PR_FALSE, csn_str));
                return_value = SLAPI_PLUGIN_FAILURE;
            }
        }

        if (!operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
            slapi_ch_free((void **)&op_params->target_address.uniqueid);
        }
    }

    /*
      This was moved because we need to write the changelog and update
      the ruv in one atomic operation - I have seen cases where the inc
      protocol thread interrupts this thread between the time the changelog
      is written and the ruv is updated - this causes confusion in several
      places, especially in _cl5SkipReplayEntry since it cannot find the csn it
      just read from the changelog in either the supplier or consumer ruv
    */
    if (0 == return_value) {
        char csn_str[CSN_STRSIZE] = {'\0'};
        const char *dn = op_params ? REPL_GET_DN(&op_params->target_address) : "unknown";
        Slapi_DN *sdn = op_params ? (&op_params->target_address)->sdn : NULL;
        char *uniqueid = op_params ? op_params->target_address.uniqueid : "unknown";
        unsigned long optype = op_params ? op_params->operation_type : 0;
        CSN *oppcsn = op_params ? op_params->csn : NULL;
        LDAPMod **mods = op_params ? op_params->p.p_modify.modify_mods : NULL;

        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        opcsn = operation_get_csn(op);

        /* Update each agmt's maxcsn */
        if (op_params && sdn) {
            agmt_update_maxcsn(r, sdn, op_params->operation_type, mods, opcsn);
        }
        rc = update_ruv_component(r, opcsn, pb);
        if (RUV_COVERS_CSN == rc) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                          "write_changelog_and_ruv - RUV already covers csn for "
                          "%s (uniqid: %s, optype: %lu) csn %s\n",
                          dn, uniqueid, optype,
                          csn_as_string(oppcsn, PR_FALSE, csn_str));
        } else if ((rc != RUV_SUCCESS) && (rc != RUV_NOTFOUND)) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                          "write_changelog_and_ruv - Failed to update RUV for "
                          "%s (uniqid: %s, optype: %lu) to changelog csn %s - rc %d\n",
                          dn, uniqueid, optype,
                          csn_as_string(oppcsn, PR_FALSE, csn_str), rc);
        }
    }

common_return:
    opcsn = operation_get_csn(op);
    prim_csn = get_thread_primary_csn();
    if (csn_primary(r, opcsn, prim_csn)) {
        if (return_value == 0) {
            /* the primary csn was succesfully committed
             * unset it in the thread local data
             */
            set_thread_primary_csn(NULL, NULL);
        }
    }
    return return_value;
}

/*
 * Postop processing - write the changelog if the operation resulted in
 * an LDAP_SUCCESS result code, update the RUV, and notify the replication
 * agreements about the change.
 * If the result code is not LDAP_SUCCESS, then cancel the operation CSN.
 */
static int
process_postop(Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    Slapi_Backend *be;
    int is_replicated_operation = 0;
    CSN *opcsn = NULL;
    char sessionid[REPL_SESSION_ID_SIZE];
    int retval = LDAP_SUCCESS;
    int rc = 0;

    /* we just let fixup operations through */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if ((operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) ||
        (operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY))) {
        return SLAPI_PLUGIN_SUCCESS;
    }

    /* ignore operations intended for chaining backends - they will be
       replicated back to us or should be ignored anyway
       replicated operations should be processed normally, as they should
       be going to a local backend */
    is_replicated_operation = operation_is_flag_set(op, OP_FLAG_REPLICATED);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (!is_replicated_operation &&
        slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
        return SLAPI_PLUGIN_SUCCESS;
    }

    get_repl_session_id(pb, sessionid, &opcsn);

    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &retval);
    if (retval == LDAP_SUCCESS) {
        agmtlist_notify_all(pb);
        rc = SLAPI_PLUGIN_SUCCESS;
    } else if (opcsn) {
        rc = cancel_opcsn(pb);

        /* Don't try to get session id since conn is always null */
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name,
                      "process postop - %s canceling operation csn\n", sessionid);
    } else {
        rc = SLAPI_PLUGIN_FAILURE;
    }

    /* the target unique id is set in the modify_preop above, so
       we need to free it */
    /* The following bunch of frees code does not belong to this place
     * but rather to operation_free who should be responsible for calling
     * operation_parameters_free and it doesn't. I guess this is like this
     * because several crashes happened in the past regarding some opparams
     * that were getting individually freed before they should. Whatever
     * the case, this is not the place and we should make sure that this
     * code gets removed for 5.next and substituted by the strategy (operation_free).
     * For 5.0, such change is too risky, so this will be done here */
    if (is_replicated_operation) {
        slapi_operation_parameters *op_params = NULL;
        int optype = 0;
        /* target uid and csn are set for all repl operations. Free them */
        char *target_uuid = NULL;
        slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
        slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &target_uuid);
        slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, NULL);
        slapi_ch_free((void **)&target_uuid);
        if (optype == SLAPI_OPERATION_ADD) {
            slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
            slapi_ch_free((void **)&op_params->p.p_add.parentuniqueid);
        }
        if (optype == SLAPI_OPERATION_MODRDN) {
            slapi_pblock_get(pb, SLAPI_OPERATION_PARAMETERS, &op_params);
            slapi_ch_free((void **)&op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid);
        }

        if (!ignore_error_and_keep_going(retval)) {
            /*
             * We have an error we can't ignore.  Release the replica and close
             * the connection to stop the replication session.
             */
            consumer_connection_extension *connext = NULL;
            Slapi_Connection *conn = NULL;
            char csn_str[CSN_STRSIZE] = {'\0'};
            PRUint64 connid = 0;
            int opid = 0;

            slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
            slapi_pblock_get(pb, SLAPI_OPERATION_ID, &opid);
            slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
            if (conn) {
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "process_postop - Failed to apply update (%s) error (%d).  "
                              "Aborting replication session(conn=%" PRIu64 " op=%d)\n",
                              csn_as_string(opcsn, PR_FALSE, csn_str), retval,
                              connid, opid);
                /*
                 * Release this replica so new sessions can begin
                 */
                connext = consumer_connection_extension_acquire_exclusive_access(conn, connid, opid);
                if (connext && connext->replica_acquired) {
                    int zero = 0;
                    replica_relinquish_exclusive_access(connext->replica_acquired, connid, opid);
                    connext->replica_acquired = NULL;
                    connext->isreplicationsession = 0;
                    slapi_pblock_set(pb, SLAPI_CONN_IS_REPLICATION_SESSION, &zero);
                }
                if (connext) {
                    consumer_connection_extension_relinquish_exclusive_access(conn, connid, opid, PR_FALSE);
                }

                /*
                 * Close the connection to end the current session with the
                 * supplier.  This prevents new updates from coming in and
                 * updating the consumer RUV - which would cause this failed
                 * update to be never be replayed.
                 */
                slapi_disconnect_server(conn);
            }
        }
    }
    if (NULL == opcsn)
        opcsn = operation_get_csn(op);
    if (opcsn)
        csn_free(&opcsn);

    return rc;
}

/*
 * Cancel an operation CSN. This removes it from any CSN pending lists.
 * This function is called when a previously-generated CSN will not
 * be needed, e.g. if the update operation produced an error.
 */
static int
cancel_opcsn(Slapi_PBlock *pb)
{
    Replica *replica = NULL;
    Slapi_Operation *op = NULL;

    if (NULL == pb) {
        return SLAPI_PLUGIN_SUCCESS;
    }
    replica = replica_get_replica_for_op(pb);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (NULL == op) {
        return SLAPI_PLUGIN_SUCCESS;
    }
    if (replica) {
        Object *gen_obj;
        CSNGen *gen;
        CSN *opcsn;

        opcsn = operation_get_csn(op);

        if (!operation_is_flag_set(op, OP_FLAG_REPLICATED)) {
            /* get csn generator for the replica */
            gen_obj = replica_get_csngen(replica);
            PR_ASSERT(gen_obj);
            gen = (CSNGen *)object_get_data(gen_obj);

            if (NULL != opcsn) {
                csngen_abort_csn(gen, operation_get_csn(op));
            }

            object_release(gen_obj);
        } else if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) {
            Object *ruv_obj;

            ruv_obj = replica_get_ruv(replica);
            PR_ASSERT(ruv_obj);
            ruv_cancel_csn_inprogress(replica, (RUV *)object_get_data(ruv_obj), opcsn, replica_get_rid(replica));
            object_release(ruv_obj);
        }
    }

    return SLAPI_PLUGIN_SUCCESS;
}


/*
 * Return non-zero if the target entry DN is the DN of the RUV tombstone
 * entry.
 * The entry has rdn of nsuniqueid = ffffffff-ffffffff-ffffffff-ffffffff
 */
static int
ruv_tombstone_op(Slapi_PBlock *pb)
{
    char *uniqueid;
    int rc;

    slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &uniqueid);

    rc = uniqueid && strcasecmp(uniqueid, RUV_STORAGE_ENTRY_UNIQUEID) == 0;

    return rc;
}

/* we don't want to process replicated operations with csn smaller
   than the corresponding csn in the consumer's ruv */
static PRBool
process_operation(Slapi_PBlock *pb, const CSN *csn)
{
    Replica *r;
    Object *ruv_obj;
    RUV *ruv;
    int rc;

    r = replica_get_replica_for_op(pb);
    if (r == NULL) {
        char sessionid[REPL_SESSION_ID_SIZE];
        get_repl_session_id(pb, sessionid, NULL);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "process_operation - "
                                                       "%s - Can't locate replica for the replicated operation\n",
                      sessionid);
        return PR_FALSE;
    }

    ruv_obj = replica_get_ruv(r);
    PR_ASSERT(ruv_obj);

    ruv = (RUV *)object_get_data(ruv_obj);
    PR_ASSERT(ruv);

    rc = ruv_add_csn_inprogress(r, ruv, csn);

    object_release(ruv_obj);

    return (rc == RUV_SUCCESS);
}

static PRBool
is_mmr_replica(Slapi_PBlock *pb)
{
    Replica *replica;

    replica = replica_get_replica_for_op(pb);
    if (replica == NULL) {
        return PR_FALSE;
    }

    return PR_TRUE;
}

static const char *
replica_get_purl_for_op(const Replica *r __attribute__((unused)), Slapi_PBlock *pb, const CSN *opcsn)
{
    int is_replicated_op;
    const char *purl = NULL;

    slapi_pblock_get(pb, SLAPI_IS_MMR_REPLICATED_OPERATION, &is_replicated_op);

    if (!is_replicated_op) {
        purl = multisupplier_get_local_purl();
    } else {
        /* Get the appropriate partial URL from the supplier RUV */
        Slapi_Connection *conn;
        consumer_connection_extension *connext;
        slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
        /* TEL 20120531: There is a slim chance we want to take exclusive access
         * to this instead.  However, it isn't clear to me that it is worth the
         * risk of changing this working code. */
        connext = (consumer_connection_extension *)repl_con_get_ext(
            REPL_CON_EXT_CONN, conn);
        if (NULL == connext || NULL == connext->supplier_ruv) {
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id(pb, sessionid, NULL);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_get_purl_for_op - "
                                                           "%s - Cannot obtain consumer connection extension or supplier_ruv.\n",
                          sessionid);
        } else {
            purl = ruv_get_purl_for_replica(connext->supplier_ruv,
                                            csn_get_replicaid(opcsn));
        }
    }

    return purl;
}

/* this function is called when state of a backend changes */
void
multisupplier_be_state_change(void *handle __attribute__((unused)), char *be_name, int old_be_state, int new_be_state)
{
    Replica *r;

    /* check if we have replica associated with the backend */
    r = replica_get_for_backend(be_name);
    if (r == NULL) {
        return;
    }

    if (new_be_state == SLAPI_BE_STATE_ON) {
        /* backend came back online - restart replication */
        slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "multisupplier_be_state_change - "
                                                          "Replica %s is coming online; enabling replication\n",
                      slapi_sdn_get_ndn(replica_get_root(r)));
        replica_enable_replication(r);
    } else if (new_be_state == SLAPI_BE_STATE_OFFLINE) {
        /* backend is about to be taken down - disable replication */
        slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "multisupplier_be_state_change - "
                                                          "Replica %s is going offline; disabling replication\n",
                      slapi_sdn_get_ndn(replica_get_root(r)));
        replica_disable_replication(r);
    } else if (new_be_state == SLAPI_BE_STATE_DELETE) {
        /* backend is about to be removed - disable replication */
        if (old_be_state == SLAPI_BE_STATE_ON) {
            slapi_log_err(SLAPI_LOG_NOTICE, repl_plugin_name, "multisupplier_be_state_change - "
                                                              "Replica %s is about to be deleted; disabling replication\n",
                          slapi_sdn_get_ndn(replica_get_root(r)));
            replica_disable_replication(r);
        }
    }

}
