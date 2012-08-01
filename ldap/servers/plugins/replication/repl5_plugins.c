/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#include "repl.h"
#include "cl5_api.h"
#include "urp.h"

static char *local_purl = NULL;
static char *purl_attrs[] = {"nsslapd-localhost", "nsslapd-port", "nsslapd-secureport", NULL};

/* Forward declarations */
static void copy_operation_parameters(Slapi_PBlock *pb);
static int write_changelog_and_ruv(Slapi_PBlock *pb);
static int process_postop (Slapi_PBlock *pb);
static int cancel_opcsn (Slapi_PBlock *pb); 
static int ruv_tombstone_op (Slapi_PBlock *pb);
static PRBool process_operation (Slapi_PBlock *pb, const CSN *csn);
static PRBool is_mmr_replica (Slapi_PBlock *pb);
static const char *replica_get_purl_for_op (const Replica *r, Slapi_PBlock *pb, const CSN *opcsn);

/*
 * XXXggood - what to do if both ssl and non-ssl ports available? How
 * do you know which to use? Offer a choice in replication config?
 */
int
multimaster_set_local_purl()
{
    int rc = 0;
    Slapi_Entry **entries;
    Slapi_PBlock *pb = NULL;

    pb = slapi_pblock_new ();

	slapi_search_internal_set_pb (pb, "cn=config", LDAP_SCOPE_BASE,
		"objectclass=*", purl_attrs, 0, NULL, NULL,
		repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_search_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "multimaster_set_local_purl: "
			"unable to read server configuration: error %d\n", rc);
	}
	else
	{
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
		if (NULL == entries || NULL == entries[0])
		{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "multimaster_set_local_purl: "
			"server configuration missing\n");
			rc = -1;
		}
		else
		{
			char *host = slapi_entry_attr_get_charptr(entries[0], "nsslapd-localhost");
			char *port = slapi_entry_attr_get_charptr(entries[0], "nsslapd-port");
			char *sslport = slapi_entry_attr_get_charptr(entries[0], "nsslapd-secureport");
			if (host == NULL || ((port == NULL && sslport == NULL)))
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"multimaster_set_local_purl: invalid server "
					"configuration\n");
			}
			else
			{
				local_purl = slapi_ch_smprintf("ldap://%s:%s", host, port);
			}

			/* slapi_ch_free acceptS NULL pointer */
			slapi_ch_free ((void**)&host);
			slapi_ch_free ((void**)&port);
			slapi_ch_free ((void**)&sslport);
		}
	}
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy (pb);

	return rc;
}


const char *
multimaster_get_local_purl()
{
	return local_purl;
}
	

/* ================= Multimaster Pre-Op Plugin Points ================== */


int 
multimaster_preop_bind (Slapi_PBlock *pb) 
{
	return 0;
}

int 
multimaster_preop_add (Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;
    int is_legacy_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /* If there is no replica or it is a legacy consumer - we don't need to continue.
       Legacy plugin is handling 4.0 consumer code */
    /* but if it is legacy, csngen_handler needs to be assigned here */
    is_legacy_operation =
        operation_is_flag_set(op,OP_FLAG_LEGACY_REPLICATION_DN);
    if (is_legacy_operation)
    {
        copy_operation_parameters(pb);
        slapi_operation_set_csngen_handler(op,
                                           (void*)replica_generate_next_csn);
        return 0;
    }

    if (!is_mmr_replica (pb))
    {
        copy_operation_parameters(pb);
        return 0;
    }

    is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
    is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

    if(is_replicated_operation)
    {
        if(!is_fixup_operation)
        {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
               get_repl_session_id (pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp)
            {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                char *superior_uuid= NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, &superior_uuid, &csn, NULL /* modrdn_mods */);
                if (-1 == drc)
                {
                    slapi_log_error(SLAPI_LOG_FATAL, REPLICATION_SUBSYSTEM,
                            "%s An error occurred while decoding the replication update "
                            "control - Add\n", sessionid);
                }
                else if (1 == drc)
                {
                    /*
                     * For add operations, we just set the operation csn. The entry's
                     * uniqueid should already be an attribute of the replicated entry.
                     */
                    struct slapi_operation_parameters *op_params;

                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation (pb, csn))
                    {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0, 
                            "replication operation not processed, replica unavailable "
                            "or csn ignored", 0, 0); 
                        csn_free (&csn);
                        slapi_ch_free ((void**)&target_uuid);
                        slapi_ch_free ((void**)&superior_uuid);

                        return -1;
                    }

                    operation_set_csn(op, csn);                    
                    slapi_pblock_set( pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                    slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
                    /* JCMREPL - Complain if there's no superior uuid */
                    op_params->p.p_add.parentuniqueid= superior_uuid; /* JCMREPL - Not very elegant */
                    /* JCMREPL - When do these things get free'd? */
                    if(target_uuid!=NULL)
                    {
                        /*
                         * Make sure that the Unique Identifier in the Control is the
                         * same as the one in the entry.
                         */
                        Slapi_Entry    *addentry;
                        char *entry_uuid;
                        slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &addentry );
                        entry_uuid= slapi_entry_attr_get_charptr( addentry, SLAPI_ATTR_UNIQUEID);
                        if(entry_uuid==NULL)
                        {
                            /* Odd that the entry doesn't have a Unique Identifier. But, we can fix it. */
                            slapi_entry_set_uniqueid(addentry, slapi_ch_strdup(target_uuid)); /* JCMREPL - strdup EVIL! There should be a uuid dup function. */
                        }
                        else
                        {
                            if(strcasecmp(entry_uuid,target_uuid)!=0)
                            {
                                slapi_log_error(SLAPI_LOG_FATAL, REPLICATION_SUBSYSTEM,
                                    "%s Replicated Add received with Control_UUID=%s and Entry_UUID=%s.\n",
                                    sessionid, target_uuid,entry_uuid);
                            }

                            slapi_ch_free ((void**)&entry_uuid);
                        }
                    }
                }
            }
            else
            {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        }
        else
        {
            /* Replicated & Fixup Operation */
        }
    }
    else
    {
        slapi_operation_set_csngen_handler ( op, (void*)replica_generate_next_csn );
    }

    copy_operation_parameters(pb);

    return 0;
}

int 
multimaster_preop_delete (Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;
    int is_legacy_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /* If there is no replica or it is a legacy consumer - we don't need to continue.
       Legacy plugin is handling 4.0 consumer code */
    /* but if it is legacy, csngen_handler needs to be assigned here */
    is_legacy_operation =
        operation_is_flag_set(op,OP_FLAG_LEGACY_REPLICATION_DN);
    if (is_legacy_operation)
    {
        copy_operation_parameters(pb);
    	slapi_operation_set_replica_attr_handler ( op, (void*)replica_get_attr );
        slapi_operation_set_csngen_handler(op,
                                           (void*)replica_generate_next_csn);
        return 0;
    }

    if (!is_mmr_replica (pb))
    {
        copy_operation_parameters(pb);
        return 0;
    }

    is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
    is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

    if(is_replicated_operation)
    {
        if(!is_fixup_operation)
        {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id (pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp)
            {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, NULL, &csn, NULL /* modrdn_mods */);
                if (-1 == drc)
                {
                    slapi_log_error(SLAPI_LOG_FATAL, REPLICATION_SUBSYSTEM,
                            "%s An error occurred while decoding the replication update "
                            "control - Delete\n", sessionid);
                }
                else if (1 == drc)
                {
                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation (pb, csn))
                    {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0, 
                            "replication operation not processed, replica unavailable "
                            "or csn ignored", 0, 0); 
                        slapi_log_error(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                            "%s replication operation not processed, replica unavailable "
                            "or csn ignored\n", sessionid); 
                        csn_free (&csn);
                        slapi_ch_free ((void**)&target_uuid);

                        return -1;
                    }

                    /*
                     * For delete operations, we pass the uniqueid of the deleted entry
                     * to the backend and let it sort out which entry to really delete.
                     * We also set the operation csn.
                     */
                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                }
            }
            else
            {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        }
        else
        {
            /* Replicated & Fixup Operation */
        }
    }
    else
    {
        slapi_operation_set_csngen_handler ( op, (void*)replica_generate_next_csn );
    }

    copy_operation_parameters(pb);
    slapi_operation_set_replica_attr_handler ( op, (void*)replica_get_attr );

    return 0;
}

int 
multimaster_preop_modify (Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;
    int is_legacy_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /* If there is no replica or it is a legacy consumer - we don't need to continue.
       Legacy plugin is handling 4.0 consumer code */
    /* but if it is legacy, csngen_handler needs to be assigned here */
    is_legacy_operation =
        operation_is_flag_set(op,OP_FLAG_LEGACY_REPLICATION_DN);
    if (is_legacy_operation)
    {
        copy_operation_parameters(pb);
        slapi_operation_set_csngen_handler(op,
                                           (void*)replica_generate_next_csn);
        return 0;
    }

    if (!is_mmr_replica (pb))
    {
        copy_operation_parameters(pb);
        return 0;
    }

    is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
    is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

    if(is_replicated_operation)
    {
        if(!is_fixup_operation)
        {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id (pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp)
            {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, NULL, &csn, NULL /* modrdn_mods */);
                if (-1 == drc)
                {
                    slapi_log_error(SLAPI_LOG_FATAL, REPLICATION_SUBSYSTEM,
                            "%s An error occurred while decoding the replication update "
                            "control- Modify\n", sessionid);
                }
                else if (1 == drc)
                {
                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation (pb, csn))
                    {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0, 
                            "replication operation not processed, replica unavailable "
                            "or csn ignored", 0, 0); 
                        slapi_log_error(SLAPI_LOG_REPL, REPLICATION_SUBSYSTEM,
                            "%s replication operation not processed, replica unavailable "
                            "or csn ignored\n", sessionid); 
                        csn_free (&csn);
                        slapi_ch_free ((void**)&target_uuid);

                        return -1;
                    }

                    /*
                     * For modify operations, we pass the uniqueid of the modified entry
                     * to the backend and let it sort out which entry to really modify.
                     * We also set the operation csn.
                     */
                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                }
            }
            else
            {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        }
        else
        {
            /* Replicated & Fixup Operation */
        }
    }
    else
    {
        slapi_operation_set_csngen_handler ( op, (void*)replica_generate_next_csn );
    }

    copy_operation_parameters(pb);
    return 0;
}

int 
multimaster_preop_modrdn (Slapi_PBlock *pb)
{
    Slapi_Operation *op;
    int is_replicated_operation;
    int is_fixup_operation;
    int is_legacy_operation;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /* If there is no replica or it is a legacy consumer - we don't need to continue.
       Legacy plugin is handling 4.0 consumer code */
    /* but if it is legacy, csngen_handler needs to be assigned here */
    is_legacy_operation =
        operation_is_flag_set(op,OP_FLAG_LEGACY_REPLICATION_DN);
    if (is_legacy_operation)
    {
        copy_operation_parameters(pb);
        slapi_operation_set_csngen_handler(op,
                                           (void*)replica_generate_next_csn);
        return 0;
    }

    if (!is_mmr_replica (pb))
    {
        copy_operation_parameters(pb);
        return 0;
    }

    is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
    is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

    if(is_replicated_operation)
    {
        if(!is_fixup_operation)
        {
            LDAPControl **ctrlp;
            char sessionid[REPL_SESSION_ID_SIZE];
            get_repl_session_id (pb, sessionid, NULL);
            slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
            if (NULL != ctrlp)
            {
                CSN *csn = NULL;
                char *target_uuid = NULL;
                char *newsuperior_uuid = NULL;
                LDAPMod **modrdn_mods = NULL;
                int drc = decode_NSDS50ReplUpdateInfoControl(ctrlp, &target_uuid, &newsuperior_uuid,
                        &csn, &modrdn_mods);
                if (-1 == drc)
                {
                    slapi_log_error(SLAPI_LOG_FATAL, REPLICATION_SUBSYSTEM,
                            "%s An error occurred while decoding the replication update "
                            "control - ModRDN\n", sessionid);
                }
                else if (1 == drc)
                {
                    /*
                     * For modrdn operations, we pass the uniqueid of the entry being
                     * renamed to the backend and let it sort out which entry to really
                     * rename. We also set the operation csn, and if the newsuperior_uuid
                     * was sent, we decode that as well. 
                     */
                    struct slapi_operation_parameters *op_params;

                    /* we don't want to process replicated operations with csn smaller
                    than the corresponding csn in the consumer's ruv */
                    if (!process_operation (pb, csn))
                    {
                        slapi_send_ldap_result(pb, LDAP_SUCCESS, 0, 
                            "replication operation not processed, replica unavailable "
                            "or csn ignored", 0, 0); 
                        csn_free (&csn);
                        slapi_ch_free ((void**)&target_uuid);
                        slapi_ch_free ((void**)&newsuperior_uuid);
                        ldap_mods_free (modrdn_mods, 1);

                        return -1;
                    }

                    operation_set_csn(op, csn);
                    slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, target_uuid);
                    slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
                    op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid= newsuperior_uuid; /* JCMREPL - Not very elegant */
                }

                /*
                 * The replicated modrdn operation may also contain a sequence
                 * that contains side effects of the modrdn operation, e.g. the
                 * modifiersname and modifytimestamp are updated.
                 */
                if (NULL != modrdn_mods)
                {
                    LDAPMod **mods;
                    Slapi_Mods smods;
                    int i;
                    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
                    slapi_mods_init_passin(&smods, mods);
                    for (i = 0; NULL != modrdn_mods[i]; i++)
                    {
                        slapi_mods_add_ldapmod(&smods, modrdn_mods[i]); /* Does not copy mod */
                    }
                    mods = slapi_mods_get_ldapmods_passout(&smods);
                    slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
                    slapi_mods_done(&smods);
                    slapi_ch_free((void **)&modrdn_mods); /* Free container only - contents are referred to by array "mods" */
                }
            }
            else
            {
                PR_ASSERT(0); /* JCMREPL - A Replicated Operation with no Repl Baggage control... What does that mean? */
            }
        }
        else
        {
            /* Replicated & Fixup Operation */
        }
    }
    else
    {
        slapi_operation_set_csngen_handler ( op, (void*)replica_generate_next_csn );
    }

    copy_operation_parameters(pb);

    return 0;
}

int 
multimaster_preop_search (Slapi_PBlock *pb)
{
    return 0;
}

int 
multimaster_preop_compare (Slapi_PBlock *pb)
{
    return 0;
}

static void
purge_entry_state_information (Slapi_PBlock *pb)
{
	CSN *purge_csn;
	Object *repl_obj;
	Replica *replica;

	/* we don't want to  trim RUV tombstone because we will
	   deadlock with ourself */
	if (ruv_tombstone_op (pb))
		return;

	repl_obj = replica_get_replica_for_op(pb);
	if (NULL != repl_obj)
	{
		replica = object_get_data(repl_obj);
		if (NULL != replica)
		{
			purge_csn = replica_get_purge_csn(replica);
		}
		if (NULL != purge_csn)
		{
			Slapi_Entry *e;
			int optype;

			slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
			switch (optype)
			{
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
			if (NULL != e)
			{
				entry_purge_state_information(e, purge_csn);
				/* conntion is always null */
                if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                    char csn_str[CSN_STRSIZE];
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
                                    "Purged state information from entry %s up to "
                                    "CSN %s\n", slapi_entry_get_dn(e),
                                    csn_as_string(purge_csn, PR_FALSE, csn_str));
                }
			}
			csn_free(&purge_csn);
		}
		object_release(repl_obj);
	}
}

int 
multimaster_bepreop_add (Slapi_PBlock *pb)
{
	int rc= 0;
	Slapi_Operation *op;
	int is_replicated_operation;
	int is_fixup_operation;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

	/* For replicated operations, apply URP algorithm */
	if (!is_fixup_operation)
	{
		slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
			(void *)replica_ruv_smods_for_op);
		if (is_replicated_operation) { 
			rc = urp_add_operation(pb);
		}
	}

	return rc;
}

int 
multimaster_bepreop_delete (Slapi_PBlock *pb)
{
	int rc= 0;
	Slapi_Operation *op;
	int is_replicated_operation;
	int is_fixup_operation;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

	/* For replicated operations, apply URP algorithm */
	if(!is_fixup_operation)
	{
		slapi_pblock_set(pb, SLAPI_TXN_RUV_MODS_FN,
			(void *)replica_ruv_smods_for_op);
		if (is_replicated_operation) {
			rc = urp_delete_operation(pb);
		}
	}

	return rc;
}

int 
multimaster_bepreop_modify (Slapi_PBlock *pb)
{
	int rc= 0;
	Slapi_Operation *op;
	int is_replicated_operation;
	int is_fixup_operation;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

	/* For replicated operations, apply URP algorithm */
	if(!is_fixup_operation)
	{
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
multimaster_bepreop_modrdn (Slapi_PBlock *pb)
{
	int rc= 0;
	Slapi_Operation *op;
	int is_replicated_operation;
	int is_fixup_operation;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	is_fixup_operation= operation_is_flag_set(op,OP_FLAG_REPL_FIXUP);

	/* For replicated operations, apply URP algorithm */
	if(!is_fixup_operation)
	{
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
multimaster_bepostop_modrdn (Slapi_PBlock *pb)
{
	Slapi_Operation *op;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	if ( ! operation_is_flag_set (op, OP_FLAG_REPL_FIXUP) )
	{
		urp_post_modrdn_operation (pb);
	}
	return 0;
}

int 
multimaster_bepostop_delete (Slapi_PBlock *pb)
{
	Slapi_Operation *op;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	if ( ! operation_is_flag_set (op, OP_FLAG_REPL_FIXUP) )
	{
		urp_post_delete_operation (pb);
	}
	return 0;
}

/* postop - write to changelog */
int 
multimaster_postop_bind (Slapi_PBlock *pb)
{
	return 0;
}

int 
multimaster_postop_add (Slapi_PBlock *pb)
{
	return process_postop(pb);
}

int 
multimaster_postop_delete (Slapi_PBlock *pb)
{
	return process_postop(pb);
}

int 
multimaster_postop_modify (Slapi_PBlock *pb)
{
	return process_postop(pb);
}

int 
multimaster_postop_modrdn (Slapi_PBlock *pb)
{
	return process_postop(pb);
}

int
multimaster_betxnpostop_delete (Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multimaster_betxnpostop_modrdn (Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multimaster_betxnpostop_add (Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
}

int
multimaster_betxnpostop_modify (Slapi_PBlock *pb)
{
    return write_changelog_and_ruv(pb);
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
    Object *repl_obj;
    Replica *replica;

    repl_obj = replica_get_replica_for_op (pb);

    /* we are only interested in the updates to replicas */
    if (repl_obj)
    {
        /* we only save the original operation parameters for replicated operations
           since client operations don't go through urp engine and pblock data can be logged */
        slapi_pblock_get( pb, SLAPI_OPERATION, &op );
        PR_ASSERT (op);

        replica = (Replica*)object_get_data (repl_obj);
        PR_ASSERT (replica);

        opext = (supplier_operation_extension*) repl_sup_get_ext (REPL_SUP_EXT_OP, op);
        if (operation_is_flag_set(op,OP_FLAG_REPLICATED) &&
            !operation_is_flag_set(op, OP_FLAG_REPL_FIXUP))
        {
            slapi_pblock_get (pb, SLAPI_OPERATION_PARAMETERS, &op_params);                          
            opext->operation_parameters= operation_parameters_dup(op_params);             
        }
           
        /* this condition is needed to avoid re-entering backend serial lock
           when ruv state is updated */
        if (!operation_is_flag_set(op, OP_FLAG_REPL_FIXUP))
        {
            /* save replica generation in case it changes */
            opext->repl_gen = replica_get_generation (replica);
        }

        object_release (repl_obj);                
    }
}

/*
 * Helper function: update the RUV so that it reflects the
 * locally-processed update. This is called for both replicated
 * and non-replicated operations.
 */
static void
update_ruv_component(Replica *replica, CSN *opcsn, Slapi_PBlock *pb)
{
    PRBool legacy;
    char *purl;

	if (!replica || !opcsn)
		return;

	/* Replica configured, so update its ruv */
	legacy = replica_is_legacy_consumer (replica);
	if (legacy)
		purl = replica_get_legacy_purl (replica);
	else
		purl = (char*)replica_get_purl_for_op (replica, pb, opcsn);

	replica_update_ruv(replica, opcsn, purl);

	if (legacy)
	{
		slapi_ch_free ((void**)&purl);
	}
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
write_changelog_and_ruv (Slapi_PBlock *pb)
{	
	Slapi_Operation *op = NULL;
	int rc;
	slapi_operation_parameters *op_params = NULL;
	Object *repl_obj;
	int return_value = 0;
	Replica *r;
	Slapi_Backend *be;
	int is_replicated_operation = 0;

	/* we just let fixup operations through */
	slapi_pblock_get( pb, SLAPI_OPERATION, &op );
	if ((operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) ||
		(operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY)))
	{
		return 0;
	}

	/* ignore operations intended for chaining backends - they will be
	   replicated back to us or should be ignored anyway
	   replicated operations should be processed normally, as they should
	   be going to a local backend */
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	slapi_pblock_get(pb, SLAPI_BACKEND, &be);
	if (!is_replicated_operation &&
		slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA))
	{
		return 0;
	}

	slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
	if (rc) { /* op failed - just return */
		return 0;
	}

	/* we only log changes for operations applied to a replica */
	repl_obj = replica_get_replica_for_op (pb);
	if (repl_obj == NULL)
		return 0;
 
	r = (Replica*)object_get_data (repl_obj);
	PR_ASSERT (r);

	if (replica_is_flag_set (r, REPLICA_LOG_CHANGES) &&
		(cl5GetState () == CL5_STATE_OPEN))
	{
		supplier_operation_extension *opext = NULL;
		const char *repl_name;
		char *repl_gen;

		opext = (supplier_operation_extension*) repl_sup_get_ext (REPL_SUP_EXT_OP, op);
		PR_ASSERT (opext);

		/* get replica generation and replica name to pass to the write function */
		repl_name = replica_get_name (r);
		repl_gen = opext->repl_gen;
		PR_ASSERT (repl_name && repl_gen);

		/* for replicated operations, we log the original, non-urp data which is
		   saved in the operation extension */
		if (operation_is_flag_set(op,OP_FLAG_REPLICATED))
		{		
			PR_ASSERT (opext->operation_parameters);
			op_params = opext->operation_parameters;
		}
		else /* since client operations don't go through urp, we log the operation data in pblock */
		{
			Slapi_Entry *e = NULL;
			const char *uniqueid = NULL;

			slapi_pblock_get (pb, SLAPI_OPERATION_PARAMETERS, &op_params);
			PR_ASSERT (op_params);

			/* need to set uniqueid operation parameter */
			/* we try to use the appropriate entry (pre op or post op) 
			   depending on the type of operation (add, delete, modify)
			   However, in some cases, the backend operation may fail in
			   a non fatal way (e.g. attempt to remove an attribute value
			   that does not exist) but we still need to log the change.
			   So, the POST_OP entry may not have been set in the FE modify
			   code.  In that case, we use the PRE_OP entry.
			*/
			slapi_pblock_get (pb, SLAPI_ENTRY_POST_OP, &e);
			if ((e == NULL) ||
				(op_params->operation_type == SLAPI_OPERATION_DELETE))
			{
				slapi_pblock_get (pb, SLAPI_ENTRY_PRE_OP, &e);
			}

			PR_ASSERT (e);

			uniqueid = slapi_entry_get_uniqueid (e);
			PR_ASSERT (uniqueid);

			op_params->target_address.uniqueid = slapi_ch_strdup (uniqueid);
		} 

		if( is_cleaned_rid(csn_get_replicaid(op_params->csn))){
			/* this RID has been cleaned */
			object_release (repl_obj);
			return 0;
		}

		/* we might have stripped all the mods - in that case we do not
		   log the operation */
		if (op_params->operation_type != SLAPI_OPERATION_MODIFY ||
			op_params->p.p_modify.modify_mods != NULL)
		{
			void *txn = NULL;
			if (cl5_is_diskfull() && !cl5_diskspace_is_available()) 
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
								"write_changelog_and_ruv: Skipped due to DISKFULL\n");
				return 0;
			}
			slapi_pblock_get(pb, SLAPI_TXN, &txn);
			rc = cl5WriteOperationTxn(repl_name, repl_gen, op_params, 
									  !operation_is_flag_set(op, OP_FLAG_REPLICATED), txn);
			if (rc != CL5_SUCCESS)
			{
    			char csn_str[CSN_STRSIZE];
			    /* ONREPL - log error */
        		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"write_changelog_and_ruv: can't add a change for "
					"%s (uniqid: %s, optype: %lu) to changelog csn %s\n",
					REPL_GET_DN(&op_params->target_address),
					op_params->target_address.uniqueid,
					op_params->operation_type,
					csn_as_string(op_params->csn, PR_FALSE, csn_str));
				return_value = 1;
			}
		}

		if (!operation_is_flag_set(op,OP_FLAG_REPLICATED))
		{
			slapi_ch_free((void**)&op_params->target_address.uniqueid);
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
		CSN *opcsn;

		slapi_pblock_get( pb, SLAPI_OPERATION, &op );
		opcsn = operation_get_csn(op);
		update_ruv_component(r, opcsn, pb);
	}

	object_release (repl_obj);
	return return_value;
}

/*
 * Postop processing - write the changelog if the operation resulted in
 * an LDAP_SUCCESS result code, update the RUV, and notify the replication
 * agreements about the change.
 * If the result code is not LDAP_SUCCESS, then cancel the operation CSN.
 */
static int
process_postop (Slapi_PBlock *pb)
{
    int rc = LDAP_SUCCESS;
    Slapi_Operation *op;
	Slapi_Backend *be;
    int is_replicated_operation = 0;
	CSN *opcsn = NULL;
	char sessionid[REPL_SESSION_ID_SIZE];

    /* we just let fixup operations through */
    slapi_pblock_get( pb, SLAPI_OPERATION, &op );
    if ((operation_is_flag_set(op, OP_FLAG_REPL_FIXUP)) ||
		(operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY)))
	{
        return 0;
	}

	/* ignore operations intended for chaining backends - they will be
	   replicated back to us or should be ignored anyway
	   replicated operations should be processed normally, as they should
	   be going to a local backend */
	is_replicated_operation= operation_is_flag_set(op,OP_FLAG_REPLICATED);
	slapi_pblock_get(pb, SLAPI_BACKEND, &be);
	if (!is_replicated_operation &&
		slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA))
	{
		return 0;
	}

	get_repl_session_id (pb, sessionid, &opcsn);

    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
	if (rc == LDAP_SUCCESS)
	{
        agmtlist_notify_all(pb);
	}
    else if (opcsn)
	{
        rc = cancel_opcsn (pb); 

		/* Don't try to get session id since conn is always null */
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s process postop: canceling operation csn\n", sessionid);
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
	if (is_replicated_operation)
	{
		slapi_operation_parameters *op_params = NULL;
		int optype = 0;
		/* target uid and csn are set for all repl operations. Free them */
		char *target_uuid = NULL;
		slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
		slapi_pblock_get(pb, SLAPI_TARGET_UNIQUEID, &target_uuid);
		slapi_pblock_set(pb, SLAPI_TARGET_UNIQUEID, NULL);
		slapi_ch_free((void**)&target_uuid);
		if (optype == SLAPI_OPERATION_ADD) {
			slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
			slapi_ch_free((void **) &op_params->p.p_add.parentuniqueid);
		}
		if (optype == SLAPI_OPERATION_MODRDN) {
			slapi_pblock_get( pb, SLAPI_OPERATION_PARAMETERS, &op_params );
			slapi_ch_free((void **) &op_params->p.p_modrdn.modrdn_newsuperior_address.uniqueid);
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
cancel_opcsn (Slapi_PBlock *pb)
{
    Object *repl_obj;
    Slapi_Operation *op = NULL;

    PR_ASSERT (pb);

    repl_obj = replica_get_replica_for_op (pb);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    PR_ASSERT (op);
	if (repl_obj)
    {
        Replica *r;
        Object *gen_obj;
        CSNGen *gen;
		CSN *opcsn;

        r = (Replica*)object_get_data (repl_obj);
        PR_ASSERT (r);
        opcsn = operation_get_csn(op);

        if (!operation_is_flag_set(op,OP_FLAG_REPLICATED))
        {
            /* get csn generator for the replica */
            gen_obj = replica_get_csngen (r);        
            PR_ASSERT (gen_obj);
            gen = (CSNGen*)object_get_data (gen_obj);
		    
		    if (NULL != opcsn)
		    {
			    csngen_abort_csn (gen, operation_get_csn(op));
		    }
        
            object_release (gen_obj);
        }
        else if (!operation_is_flag_set(op,OP_FLAG_REPL_FIXUP))
        {
            Object *ruv_obj;

            ruv_obj = replica_get_ruv (r);
            PR_ASSERT (ruv_obj);
            ruv_cancel_csn_inprogress ((RUV*)object_get_data (ruv_obj), opcsn);
            object_release (ruv_obj);
        }

        object_release (repl_obj);
    }
    
    return 0;
}



/*
 * Return non-zero if the target entry DN is the DN of the RUV tombstone
 * entry.
 * The entry has rdn of nsuniqueid = ffffffff-ffffffff-ffffffff-ffffffff
 */
static int
ruv_tombstone_op (Slapi_PBlock *pb)
{
	char *uniqueid;
	int rc;

	slapi_pblock_get (pb, SLAPI_TARGET_UNIQUEID, &uniqueid);

	rc = uniqueid && strcasecmp (uniqueid, RUV_STORAGE_ENTRY_UNIQUEID) == 0;

	return rc;
}

/* we don't want to process replicated operations with csn smaller
   than the corresponding csn in the consumer's ruv */
static PRBool 
process_operation (Slapi_PBlock *pb, const CSN *csn)
{
    Object *r_obj;
    Replica *r;
    Object *ruv_obj;
    RUV *ruv;
    int rc;

    r_obj = replica_get_replica_for_op(pb);
    if (r_obj == NULL)
    {
		char sessionid[REPL_SESSION_ID_SIZE];
		get_repl_session_id (pb, sessionid, NULL);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "%s process_operation: "
			"can't locate replica for the replicated operation\n",
			sessionid );
        return PR_FALSE;
    }
    
    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);

    ruv_obj = replica_get_ruv (r);
    PR_ASSERT (ruv_obj);

    ruv = (RUV*)object_get_data (ruv_obj);
    PR_ASSERT (ruv);
 
    rc = ruv_add_csn_inprogress (ruv, csn);

    object_release (ruv_obj);
    object_release (r_obj);

    return (rc == RUV_SUCCESS);
}

static PRBool 
is_mmr_replica (Slapi_PBlock *pb)
{
    Object *r_obj;
    Replica *r;
    PRBool mmr;

    r_obj = replica_get_replica_for_op(pb);
    if (r_obj == NULL)
    {
        return PR_FALSE;
    }   

    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);

    mmr = !replica_is_legacy_consumer (r);

    object_release (r_obj);

    return mmr;
}

static const char *replica_get_purl_for_op (const Replica *r, Slapi_PBlock *pb, const CSN *opcsn)
{
    int is_replicated_op;
    const char *purl = NULL;

    slapi_pblock_get(pb, SLAPI_IS_MMR_REPLICATED_OPERATION, &is_replicated_op);

	if (!is_replicated_op)
	{
		purl = multimaster_get_local_purl();
	}
	else
	{
		/* Get the appropriate partial URL from the supplier RUV */
		Slapi_Connection *conn;
		consumer_connection_extension *connext;
		slapi_pblock_get(pb, SLAPI_CONNECTION, &conn);
		connext = (consumer_connection_extension *)repl_con_get_ext(
			REPL_CON_EXT_CONN, conn);
		if (NULL == connext || NULL == connext->supplier_ruv)
		{
			char sessionid[REPL_SESSION_ID_SIZE];
			get_repl_session_id (pb, sessionid, NULL);
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "%s replica_get_purl_for_op: "
                            "cannot obtain consumer connection extension or supplier_ruv.\n",
							sessionid);
		}
		else
		{            
			purl = ruv_get_purl_for_replica(connext->supplier_ruv,
				                            csn_get_replicaid(opcsn));
		}
	}

    return purl;   
}

#ifdef NOTUSED
/* ONREPL at the moment, I decided not to trim copiedFrom and copyingFrom
   attributes when sending operation to replicas. This is because, each
   operation results in a state information stored in the database and
   if we don't replay all operations we will endup with state inconsistency.

   Keeping the function just in case
 */
static void strip_legacy_info (slapi_operation_parameters *op_params)
{
    switch (op_params->operation_type)
    {
        case SLAPI_OPERATION_ADD:       
                slapi_entry_delete_values_sv(op_params->p.p_add.target_entry, 
                                             type_copiedFrom, NULL);
                slapi_entry_delete_values_sv(op_params->p.p_add.target_entry, 
                                             type_copyingFrom, NULL);
                break;
        case SLAPI_OPERATION_MODIFY:
        {        
                Slapi_Mods smods;
                LDAPMod *mod;

                slapi_mods_init_byref(&smods, op_params->p.p_modify.modify_mods);
                mod = slapi_mods_get_first_mod(&smods);
                while (mod)
                {
                    /* modify just to update copiedFrom or copyingFrom attribute 
                       does not contain modifiersname or modifytime - so we don't
                       have to strip them */
                    if (strcasecmp (mod->mod_type, type_copiedFrom) == 0 ||
                        strcasecmp (mod->mod_type, type_copyingFrom) == 0)
                        slapi_mods_remove(&smods);
                    mod = slapi_mods_get_next_mod(&smods);
                }

                op_params->p.p_modify.modify_mods = slapi_mods_get_ldapmods_passout (&smods);
                slapi_mods_done (&smods);

                break;
        }

        default: break;
    }
}
#endif

/* this function is called when state of a backend changes */
void 
multimaster_be_state_change (void *handle, char *be_name, int old_be_state, int new_be_state)
{
    Object *r_obj;
    Replica *r;

    /* check if we have replica associated with the backend */
    r_obj = replica_get_for_backend (be_name);
    if (r_obj == NULL)
        return;

    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r);

    if (new_be_state == SLAPI_BE_STATE_ON)
    {
        /* backend came back online - restart replication */     
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "multimaster_be_state_change: "
			"replica %s is coming online; enabling replication\n",
            slapi_sdn_get_ndn (replica_get_root (r)));
        replica_enable_replication (r);
    }
    else if (new_be_state == SLAPI_BE_STATE_OFFLINE)
    {
        /* backend is about to be taken down - disable replication */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "multimaster_be_state_change: "
			"replica %s is going offline; disabling replication\n",
            slapi_sdn_get_ndn (replica_get_root (r)));
        replica_disable_replication (r, r_obj);
    }
    else if (new_be_state == SLAPI_BE_STATE_DELETE)
    {
        /* backend is about to be removed - disable replication */
        if (old_be_state == SLAPI_BE_STATE_ON)
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "multimaster_be_state_change: "
			    "replica %s is about to be deleted; disabling replication\n",
                slapi_sdn_get_ndn (replica_get_root (r)));            
            replica_disable_replication (r, r_obj);
        }
    }

    object_release (r_obj);
}
