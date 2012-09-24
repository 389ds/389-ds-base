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
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* repl5_replica_config.c - replica configuration over ldap */
#include <ctype.h>	/* for isdigit() */
#include "repl.h"   /* ONREPL - this is bad */
#include "repl5.h"
#include "cl5_api.h"
#include "cl5.h"

/* CONFIG_BASE: no need to optimize */
#define CONFIG_BASE		    "cn=mapping tree,cn=config"
#define CONFIG_FILTER	    "(objectclass=nsDS5Replica)"
#define TASK_ATTR           "nsds5Task"
#define CL2LDIF_TASK        "CL2LDIF"
#define LDIF2CL_TASK        "LDIF2CL"
#define CLEANRUV            "CLEANRUV"
#define CLEANRUVLEN         8
#define CLEANALLRUV         "CLEANALLRUV"
#define CLEANALLRUVLEN      11
#define REPLICA_RDN         "cn=replica"
#define CLEANALLRUV_ID      "CleanAllRUV Task"
#define ABORT_CLEANALLRUV_ID    "Abort CleanAllRUV Task"

int slapi_log_urp = SLAPI_LOG_REPL;
static ReplicaId cleaned_rids[CLEANRIDSIZ + 1] = {0};
static ReplicaId aborted_rids[CLEANRIDSIZ + 1] = {0};
static Slapi_RWLock *rid_lock = NULL;
static Slapi_RWLock *abort_rid_lock = NULL;
static PRLock *notify_lock = NULL;
static PRCondVar *notify_cvar = NULL;

/* Forward Declartions */
static int replica_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_post_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_cleanall_ruv_task(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,  int *returncode, char *returntext, void *arg);
static int replica_config_change_type_and_id (Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods);
static int replica_config_change_updatedn (Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_flags (Replica *r, const char *new_flags,  char *returntext, int apply_mods);
static int replica_execute_task (Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_execute_cl2ldif_task (Object *r, char *returntext);
static int replica_execute_ldif2cl_task (Object *r, char *returntext);
static int replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext);
static int replica_execute_cleanall_ruv_task (Object *r, ReplicaId rid, Slapi_Task *task, char *returntext);
static void replica_cleanallruv_thread(void *arg);
static void replica_send_cleanruv_task(Repl_Agmt *agmt, ReplicaId rid, Slapi_Task *task);
static int check_agmts_are_alive(Replica *replica, ReplicaId rid, Slapi_Task *task);
static int check_agmts_are_caught_up(Replica *replica, ReplicaId rid, char *maxcsn, Slapi_Task *task);
static int replica_cleanallruv_send_extop(Repl_Agmt *ra, ReplicaId rid, Slapi_Task *task, struct berval *payload, int check_result);
static int replica_cleanallruv_send_abort_extop(Repl_Agmt *ra, Slapi_Task *task, struct berval *payload);
static int replica_cleanallruv_check_maxcsn(Repl_Agmt *agmt, char *rid_text, char *maxcsn, Slapi_Task *task);
static int replica_cleanallruv_replica_alive(Repl_Agmt *agmt);
static int replica_cleanallruv_check_ruv(Repl_Agmt *ra, char *rid_text, Slapi_Task *task);
static int get_cleanruv_task_count();
static int get_abort_cleanruv_task_count();
static int replica_cleanup_task (Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_task_done(Replica *replica);
static multimaster_mtnode_extension * _replica_config_get_mtnode_ext (const Slapi_Entry *e);

/*
 * Note: internal add/modify/delete operations should not be run while
 * s_configLock is held.  E.g., slapi_modify_internal_pb via replica_task_done
 * in replica_config_post_modify.
 */
static PRLock *s_configLock;

static int
dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
	*returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

int
replica_config_init()
{
	s_configLock = PR_NewLock ();

	if (s_configLock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_init: "
                        "failed to create configuration lock; NSPR error - %d\n",
                        PR_GetError ());
		return -1;
	}
	rid_lock = slapi_new_rwlock();
	if(rid_lock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_init: "
				"failed to create rid_lock; NSPR error - %d\n", PR_GetError ());
		return -1;
	}
	abort_rid_lock = slapi_new_rwlock();
	if(abort_rid_lock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_init: "
				"failed to create abort_rid_lock; NSPR error - %d\n", PR_GetError ());
		return -1;
	}
	if ( ( notify_lock = PR_NewLock()) == NULL ) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_init: "
				"failed to create notify lock; NSPR error - %d\n", PR_GetError ());
		return -1;
	}
	if ( ( notify_cvar = PR_NewCondVar( notify_lock )) == NULL ) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_init: "
				"failed to create notify cond var; NSPR error - %d\n", PR_GetError ());
		return -1;
	}

	/* config DSE must be initialized before we get here */
	slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								   CONFIG_FILTER, replica_config_add, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								   CONFIG_FILTER, replica_config_modify,NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								   CONFIG_FILTER, dont_allow_that, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								   CONFIG_FILTER, replica_config_delete,NULL);
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								   CONFIG_FILTER, replica_config_search,NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP,
                                   CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_post_modify,
                                   NULL);

    /* register the CLEANALLRUV & ABORT task */
    slapi_task_register_handler("cleanallruv", replica_cleanall_ruv_task);
    slapi_task_register_handler("abort cleanallruv", replica_cleanall_ruv_abort);

    return 0;
}

void
replica_config_destroy ()
{
	if (s_configLock)
	{
		PR_DestroyLock (s_configLock);
        s_configLock = NULL;
	}

	/* config DSE must be initialized before we get here */
	slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								 CONFIG_FILTER, replica_config_add);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								 CONFIG_FILTER, replica_config_modify);
    slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								 CONFIG_FILTER, dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								 CONFIG_FILTER, replica_config_delete);
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
								 CONFIG_FILTER, replica_config_search);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP,
                                 CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_post_modify);
}

static int
replica_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter,
					   int *returncode, char *errorbuf, void *arg)
{
	Replica *r = NULL;
    multimaster_mtnode_extension *mtnode_ext;
    char *replica_root = (char*)slapi_entry_attr_get_charptr (e, attr_replicaRoot);
	char buf [SLAPI_DSE_RETURNTEXT_SIZE];
	char *errortext = errorbuf ? errorbuf : buf;

	if (errorbuf)
	{
		errorbuf[0] = '\0';
	}

	*returncode = LDAP_SUCCESS;

	PR_Lock (s_configLock);

	/* add the dn to the dn hash so we can tell this replica is being configured */
	replica_add_by_dn(replica_root);

    mtnode_ext = _replica_config_get_mtnode_ext (e);
    PR_ASSERT (mtnode_ext);

    if (mtnode_ext->replica)
    {
        PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE, "replica already configured for %s", replica_root);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_add: %s\n", errortext);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }

    /* create replica object */
    r = replica_new_from_entry (e, errortext, PR_TRUE /* is a newly added entry */);
    if (r == NULL)
    {
        *returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

	/* Set the mapping tree node state, and the referrals from the RUV */
    /* if this server is a 4.0 consumer the referrals are set by legacy plugin */
    if (!replica_is_legacy_consumer (r))
	    consumer5_set_mapping_tree_state_for_replica(r, NULL);

    /* ONREPL if replica is added as writable we need to execute protocol that
       introduces new writable replica to the topology */

    mtnode_ext->replica = object_new (r, replica_destroy); /* Refcnt is 1 */

    /* add replica object to the hash */
    *returncode = replica_add_by_name (replica_get_name (r), mtnode_ext->replica); /* Increments object refcnt */
	/* delete the dn from the dn hash - done with configuration */
	replica_delete_by_dn(replica_root);

done:

	PR_Unlock (s_configLock);
	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&replica_root);

	if (*returncode != LDAP_SUCCESS)
	{
        if (mtnode_ext->replica)
            object_release (mtnode_ext->replica);
		return SLAPI_DSE_CALLBACK_ERROR;
	}
	else
		return SLAPI_DSE_CALLBACK_OK;
}

static int
replica_config_modify (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
					   int *returncode, char *returntext, void *arg)
{
    int rc= 0;
   	LDAPMod **mods;
	int i, apply_mods;
    multimaster_mtnode_extension *mtnode_ext;
	Replica *r = NULL;
    char *replica_root = NULL;
	char buf [SLAPI_DSE_RETURNTEXT_SIZE];
	char *errortext = returntext ? returntext : buf;
    char *config_attr, *config_attr_value;
    Slapi_Operation *op;
    void *identity;

	if (returntext)
	{
		returntext[0] = '\0';
	}
	*returncode = LDAP_SUCCESS;

    /* just let internal operations originated from replication plugin to go through */
    slapi_pblock_get (pb, SLAPI_OPERATION, &op);
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);

    if (operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
        (identity == repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION)))
    {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    replica_root = (char*)slapi_entry_attr_get_charptr (e, attr_replicaRoot);

	PR_Lock (s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext (e);
    PR_ASSERT (mtnode_ext);

    if (mtnode_ext->replica)
        object_acquire (mtnode_ext->replica);

    if (mtnode_ext->replica == NULL)
    {
        PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE, "replica does not exist for %s", replica_root);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n",
                        errortext);
        *returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    r = object_get_data (mtnode_ext->replica);
    PR_ASSERT (r);

	slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (apply_mods = 0; apply_mods <= 1; apply_mods++)
	{
		/* we only allow the replica ID and type to be modified together e.g.
		   if converting a read only replica to a master or vice versa -
		   we will need to change both the replica ID and the type at the same
		   time - we must disallow changing the replica ID if the type is not
		   being changed and vice versa
		*/
		char *new_repl_id = NULL;
		char *new_repl_type = NULL;

        if (*returncode != LDAP_SUCCESS)
            break;

        for (i = 0; (mods[i] && (LDAP_SUCCESS == rc)); i++)
		{
            if (*returncode != LDAP_SUCCESS)
                break;

            config_attr = (char *) mods[i]->mod_type;
            PR_ASSERT (config_attr);

            /* disallow modifications or removal of replica root,
               replica name and replica state attributes */
            if (strcasecmp (config_attr, attr_replicaRoot) == 0 ||
                strcasecmp (config_attr, attr_replicaName) == 0 ||
                strcasecmp (config_attr, attr_state) == 0)
            {
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE, "modification of %s attribute is not allowed",
                         config_attr);
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n",
                                errortext);
            }
            /* this is a request to delete an attribute */
            else if ((mods[i]->mod_op & LDAP_MOD_DELETE) || mods[i]->mod_bvalues == NULL
                     || mods[i]->mod_bvalues[0]->bv_val == NULL)
            {
                /* currently, you can only remove referral,
				   legacy consumer or bind dn attribute */
                if (strcasecmp (config_attr, attr_replicaBindDn) == 0)
                {
				    *returncode = replica_config_change_updatedn (r, mods[i], errortext, apply_mods);
                }
                else if (strcasecmp (config_attr, attr_replicaReferral) == 0)
                {
                    if (apply_mods) {
				        replica_set_referrals(r, NULL);
						if (!replica_is_legacy_consumer (r)) {
							consumer5_set_mapping_tree_state_for_replica(r, NULL);
						}
					}
                }
                else if (strcasecmp (config_attr, type_replicaLegacyConsumer) == 0)
                {
                    if (apply_mods)
                        replica_set_legacy_consumer (r, PR_FALSE);
                }
                else
                {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE, "deletion of %s attribute is not allowed", config_attr);
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n", errortext);
                }
            }
            else    /* modify an attribute */
            {
                config_attr_value = (char *) mods[i]->mod_bvalues[0]->bv_val;

                if (strcasecmp (config_attr, attr_replicaBindDn) == 0)
			    {
				    *returncode = replica_config_change_updatedn (r, mods[i], errortext, apply_mods);
                }
                else if (strcasecmp (config_attr, attr_replicaType) == 0)
			    {
					new_repl_type = slapi_ch_strdup(config_attr_value);
                }
                else if (strcasecmp (config_attr, attr_replicaId) == 0)
			    {
					new_repl_id = slapi_ch_strdup(config_attr_value);
                }
                else if (strcasecmp (config_attr, attr_flags) == 0)
                {
                    *returncode = replica_config_change_flags (r, config_attr_value, errortext, apply_mods);
                }
                else if (strcasecmp (config_attr, TASK_ATTR) == 0)
                {
                    *returncode = replica_execute_task (mtnode_ext->replica, config_attr_value, errortext, apply_mods);
                }
                else if (strcasecmp (config_attr, attr_replicaReferral) == 0)
                {
                    if (apply_mods)
                    {
				        Slapi_Mod smod;
                        Slapi_ValueSet *vs= slapi_valueset_new();
                        slapi_mod_init_byref(&smod,mods[i]);
                        slapi_valueset_set_from_smod(vs, &smod);
                        replica_set_referrals (r, vs);
                        slapi_mod_done(&smod);
                        slapi_valueset_free(vs);
						if (!replica_is_legacy_consumer (r)) {
							consumer5_set_mapping_tree_state_for_replica(r, NULL);
						}
                    }
                }
				else if (strcasecmp (config_attr, type_replicaPurgeDelay) == 0)
				{
					if (apply_mods && config_attr_value && config_attr_value[0])
					{
						PRUint32 delay;
						if (isdigit (config_attr_value[0]))
						{
							delay = (unsigned int)atoi(config_attr_value);
							replica_set_purge_delay(r, delay);
						}
						else
							*returncode = LDAP_OPERATIONS_ERROR;
					}
				}
				else if (strcasecmp (config_attr, type_replicaTombstonePurgeInterval) == 0)
				{
					if (apply_mods && config_attr_value && config_attr_value[0])
					{
						long interval;
						interval = atol (config_attr_value);
						replica_set_tombstone_reap_interval (r, interval);
					}
				}
                else if (strcasecmp (config_attr, type_replicaLegacyConsumer) == 0)
                {
                    if (apply_mods)
                    {
                        PRBool legacy = (strcasecmp (config_attr_value, "on") == 0) ||
                                        (strcasecmp (config_attr_value, "true") == 0) ||
                                        (strcasecmp (config_attr_value, "yes") == 0) ||
                                        (strcasecmp (config_attr_value, "1") == 0);

                        replica_set_legacy_consumer (r, legacy);
                    }
                }
                /* ignore modifiers attributes added by the server */
                else if (strcasecmp (config_attr, "modifytimestamp") == 0 ||
                         strcasecmp (config_attr, "modifiersname") == 0)
                {
                    *returncode = LDAP_SUCCESS;
                }
                else
                {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE,
								 "modification of attribute %s is not allowed in replica entry", config_attr);
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n", errortext);
                }
            }
		}

		if (new_repl_id || new_repl_type)
		{
			*returncode = replica_config_change_type_and_id(r, new_repl_type, new_repl_id, errortext, apply_mods);
			slapi_ch_free_string(&new_repl_id);
			slapi_ch_free_string(&new_repl_type);
		}
	}

done:
    if (mtnode_ext->replica)
        object_release (mtnode_ext->replica);

	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free_string(&replica_root);

	PR_Unlock (s_configLock);

	if (*returncode != LDAP_SUCCESS)
	{
		return SLAPI_DSE_CALLBACK_ERROR;
	}
	else
    {
	    return SLAPI_DSE_CALLBACK_OK;
    }
}

static int
replica_config_post_modify(Slapi_PBlock *pb,
                           Slapi_Entry* entryBefore,
                           Slapi_Entry* e,
                           int *returncode,
                           char *returntext,
                           void *arg)
{
    int rc= 0;
    LDAPMod **mods;
    int i, apply_mods;
    multimaster_mtnode_extension *mtnode_ext;
    Replica *r = NULL;
    char *replica_root = NULL;
    char buf [SLAPI_DSE_RETURNTEXT_SIZE];
    char *errortext = returntext ? returntext : buf;
    char *config_attr, *config_attr_value;
    Slapi_Operation *op;
    void *identity;
    int flag_need_cleanup = 0;

    if (returntext)
    {
        returntext[0] = '\0';
    }
    *returncode = LDAP_SUCCESS;

    /* just let internal operations originated from replication plugin to go through */
    slapi_pblock_get (pb, SLAPI_OPERATION, &op);
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);

    if (operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
        (identity == repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION)))
    {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    replica_root = (char*)slapi_entry_attr_get_charptr (e, attr_replicaRoot);

    PR_Lock (s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext (e);
    PR_ASSERT (mtnode_ext);

    if (mtnode_ext->replica)
        object_acquire (mtnode_ext->replica);

    if (mtnode_ext->replica == NULL)
    {
        PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                     "replica does not exist for %s", replica_root);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_config_post_modify: %s\n",
                        errortext);
        *returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    r = object_get_data (mtnode_ext->replica);
    PR_ASSERT (r);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (apply_mods = 0; apply_mods <= 1; apply_mods++)
    {
        /* we only allow the replica ID and type to be modified together e.g.
           if converting a read only replica to a master or vice versa -
           we will need to change both the replica ID and the type at the same
           time - we must disallow changing the replica ID if the type is not
           being changed and vice versa
        */
        if (*returncode != LDAP_SUCCESS)
            break;

        for (i = 0; (mods[i] && (LDAP_SUCCESS == rc)); i++)
        {
            if (*returncode != LDAP_SUCCESS)
                break;

            config_attr = (char *) mods[i]->mod_type;
            PR_ASSERT (config_attr);

            /* disallow modifications or removal of replica root,
               replica name and replica state attributes */
            if (strcasecmp (config_attr, attr_replicaRoot) == 0 ||
                strcasecmp (config_attr, attr_replicaName) == 0 ||
                strcasecmp (config_attr, attr_state) == 0)
            {
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf (errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                             "modification of %s attribute is not allowed",
                             config_attr);
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                                "replica_config_post_modify: %s\n",
                                errortext);
            }
            /* this is a request to delete an attribute */
            else if ((mods[i]->mod_op & LDAP_MOD_DELETE) ||
                     mods[i]->mod_bvalues == NULL ||
                     mods[i]->mod_bvalues[0]->bv_val == NULL)
            {
                ;
            }
            else    /* modify an attribute */
            {
                config_attr_value = (char *) mods[i]->mod_bvalues[0]->bv_val;

                if (strcasecmp (config_attr, TASK_ATTR) == 0)
                {
                    flag_need_cleanup = 1;
                }
            }
        }
    }

done:
    PR_Unlock (s_configLock);

    /* slapi_ch_free accepts NULL pointer */
    slapi_ch_free_string (&replica_root);

    /* Call replica_cleanup_task after s_configLock is reliesed */
    if (flag_need_cleanup)
    {
        *returncode = replica_cleanup_task(mtnode_ext->replica,
                                           config_attr_value,
                                           errortext, apply_mods);
    }

    if (mtnode_ext->replica)
        object_release (mtnode_ext->replica);

    if (*returncode != LDAP_SUCCESS)
    {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
    else
    {
        return SLAPI_DSE_CALLBACK_OK;
    }
}

static int
replica_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter,
					   int *returncode, char *returntext, void *arg)
{
    multimaster_mtnode_extension *mtnode_ext;
    Replica *r;

	PR_Lock (s_configLock);

	mtnode_ext = _replica_config_get_mtnode_ext (e);
    PR_ASSERT (mtnode_ext);

    if (mtnode_ext->replica)
    {
        char ebuf[BUFSIZ];

        /* remove object from the hash */
        r = (Replica*)object_get_data (mtnode_ext->replica);
        PR_ASSERT (r);
        /* The changelog for this replica is no longer valid, so we should remove it. */
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_delete: "
                        "Warning: The changelog for replica %s is no longer valid since "
                        "the replica config is being deleted.  Removing the changelog.\n",
                        escape_string(slapi_sdn_get_dn(replica_get_root(r)),ebuf));
        cl5DeleteDBSync(mtnode_ext->replica);
        replica_delete_by_name (replica_get_name (r));
        object_release (mtnode_ext->replica);
        mtnode_ext->replica = NULL;
    }

	PR_Unlock (s_configLock);

	*returncode = LDAP_SUCCESS;
	return SLAPI_DSE_CALLBACK_OK;
}

static int
replica_config_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode,
                       char *returntext, void *arg)
{
    multimaster_mtnode_extension *mtnode_ext;
    int changeCount = 0;
	PRBool reapActive = PR_FALSE;
    char val [64];

	/* add attribute that contains number of entries in the changelog for this replica */

    PR_Lock (s_configLock);

	mtnode_ext = _replica_config_get_mtnode_ext (e);
	PR_ASSERT (mtnode_ext);

	if (mtnode_ext->replica) {
		Replica *replica;
		object_acquire (mtnode_ext->replica);
		if (cl5GetState () == CL5_STATE_OPEN) {
			changeCount = cl5GetOperationCount (mtnode_ext->replica);
		}
		replica = (Replica*)object_get_data (mtnode_ext->replica);
		if (replica) {
			reapActive = replica_get_tombstone_reap_active(replica);
		}
		object_release (mtnode_ext->replica);
	}

    sprintf (val, "%d", changeCount);
    slapi_entry_add_string (e, type_replicaChangeCount, val);
	slapi_entry_attr_set_int(e, "nsds5replicaReapActive", (int)reapActive);

    PR_Unlock (s_configLock);

    return SLAPI_DSE_CALLBACK_OK;
}

static int
replica_config_change_type_and_id (Replica *r, const char *new_type,
								   const char *new_id, char *returntext,
								   int apply_mods)
{
    int type;
	ReplicaType oldtype;
	ReplicaId rid;
	ReplicaId oldrid;

    PR_ASSERT (r);

	oldtype = replica_get_type(r);
	oldrid = replica_get_rid(r);
    if (new_type == NULL)   /* by default - replica is read-only */
	{
        type = REPLICA_TYPE_READONLY;
	}
    else
    {
        type = atoi (new_type);
        if (type <= REPLICA_TYPE_UNKNOWN || type >= REPLICA_TYPE_END)
        {
            PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "invalid replica type %d", type);
            return LDAP_OPERATIONS_ERROR;
        }
    }

	/* disallow changing type to itself just to permit a replica ID change */
	if (oldtype == type)
	{
            PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "replica type is already %d - not changing", type);
            return LDAP_OPERATIONS_ERROR;
	}

	if (type == REPLICA_TYPE_READONLY)
	{
		rid = READ_ONLY_REPLICA_ID; /* default rid for read only */
	}
	else if (!new_id)
	{
		PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "a replica ID is required when changing replica type to read-write");
		return LDAP_UNWILLING_TO_PERFORM;
	}
	else
	{
		int temprid = atoi (new_id);
		if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID)
		{
			PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					"attribute %s must have a value greater than 0 "
					"and less than %d",
					attr_replicaId, READ_ONLY_REPLICA_ID);
			return LDAP_UNWILLING_TO_PERFORM;
		}
		else
		{
			rid = (ReplicaId)temprid;
		}
	}

	/* error if old rid == new rid */
	if (oldrid == rid)
	{
            PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "replica ID is already %d - not changing", rid);
            return LDAP_OPERATIONS_ERROR;
	}

    if (apply_mods)
    {
        replica_set_type (r, type);
		replica_set_rid(r, rid);

		/* Set the mapping tree node, and the list of referrals */
        /* if this server is a 4.0 consumer the referrals are set by legacy plugin */
        if (!replica_is_legacy_consumer(r))
		    consumer5_set_mapping_tree_state_for_replica(r, NULL);
    }

    return LDAP_SUCCESS;
}

static int
replica_config_change_updatedn (Replica *r, const LDAPMod *mod, char *returntext,
                                int apply_mods)
{
    PR_ASSERT (r);

    if (apply_mods)
    {
		Slapi_Mod smod;
		Slapi_ValueSet *vs= slapi_valueset_new();
		slapi_mod_init_byref(&smod, (LDAPMod *)mod); /* cast away const */
		slapi_valueset_set_from_smod(vs, &smod);
		replica_set_updatedn(r, vs, mod->mod_op);
		slapi_mod_done(&smod);
		slapi_valueset_free(vs);
	}

    return LDAP_SUCCESS;
}

static int replica_config_change_flags (Replica *r, const char *new_flags,
                                        char *returntext, int apply_mods)
{
    PR_ASSERT (r);

    if (apply_mods)
    {
        PRUint32 flags;

        flags = atol (new_flags);

        replica_replace_flags (r, flags);
    }

    return LDAP_SUCCESS;
}

static int replica_execute_task (Object *r, const char *task_name, char *returntext,
                                 int apply_mods)
{

    if (strcasecmp (task_name, CL2LDIF_TASK) == 0)
    {
		if (apply_mods)
		{
			return replica_execute_cl2ldif_task (r, returntext);
		}
		else
			return LDAP_SUCCESS;
	}
	else if (strcasecmp (task_name, LDIF2CL_TASK) == 0)
    {
		if (apply_mods)
		{
			return replica_execute_ldif2cl_task (r, returntext);
		}
		else
			return LDAP_SUCCESS;
	}
	else if (strncasecmp (task_name, CLEANRUV, CLEANRUVLEN) == 0)
	{
		int temprid = atoi(&(task_name[CLEANRUVLEN]));
		if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID){
			PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - %s", temprid, task_name);
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_execute_task: %s\n", returntext);
			return LDAP_OPERATIONS_ERROR;
		}
		if (apply_mods)
		{
			return replica_execute_cleanruv_task (r, (ReplicaId)temprid, returntext);
		}
		else
			return LDAP_SUCCESS;
	}
	else if (strncasecmp (task_name, CLEANALLRUV, CLEANALLRUVLEN) == 0)
	{
		int temprid = atoi(&(task_name[CLEANALLRUVLEN]));
		if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID){
				PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)", temprid, task_name);
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_execute_task: %s\n", returntext);
				return LDAP_OPERATIONS_ERROR;
		}
		if (apply_mods)
		{
			Slapi_Task *empty_task = NULL;
			return replica_execute_cleanall_ruv_task(r, (ReplicaId)temprid, empty_task, returntext);
		}
		else
			return LDAP_SUCCESS;
	}
	else
	{
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "unsupported replica task - %s", task_name);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_task: %s\n", returntext);
        return LDAP_OPERATIONS_ERROR;
    }

}

static int
replica_cleanup_task (Object *r, const char *task_name, char *returntext,
                      int apply_mods)
{
    int rc = LDAP_SUCCESS;
    if (apply_mods) {
        Replica *replica = (Replica*)object_get_data (r);
        if (NULL == replica) {
            rc = LDAP_OPERATIONS_ERROR;
        } else {
            rc = replica_task_done(replica);
        }
    }
    return rc;
}

static int
replica_task_done(Replica *replica)
{
    int rc = LDAP_OPERATIONS_ERROR;
    char *replica_dn = NULL;
    Slapi_DN *replica_sdn = NULL;
    Slapi_PBlock *pb = NULL;
    LDAPMod *mods [2];
    LDAPMod mod;
    if (NULL == replica) {
        return rc;
    }
    /* dn: cn=replica,cn=dc\3Dexample\2Cdc\3Dcom,cn=mapping tree,cn=config */
    replica_dn = slapi_ch_smprintf("%s,cn=\"%s\",%s",
                                   REPLICA_RDN,
                                   slapi_sdn_get_dn(replica_get_root(replica)),
                                   CONFIG_BASE);
    if (NULL == replica_dn) {
        return rc;
    }
	replica_sdn = slapi_sdn_new_dn_passin(replica_dn);
    pb = slapi_pblock_new();
    mods[0] = &mod;
    mods[1] = NULL;
    mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
    mod.mod_type = (char *)TASK_ATTR;
    mod.mod_bvalues = NULL;

    slapi_modify_internal_set_pb_ext(pb, replica_sdn, mods, NULL/* controls */,
                      NULL/* uniqueid */,
                      repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION),
                      0/* flags */);
    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if ((rc != LDAP_SUCCESS) && (rc != LDAP_NO_SUCH_ATTRIBUTE)) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_task_done: "
                        "failed to remove (%s) attribute from (%s) entry; "
                        "LDAP error - %d\n",
                        TASK_ATTR, replica_dn, rc);
    }

    slapi_pblock_destroy (pb);
    slapi_sdn_free(&replica_sdn);

    return rc;
}

static int replica_execute_cl2ldif_task (Object *r, char *returntext)
{
    int rc;
    Object *rlist [2];
    Replica *replica;
    char fName [MAXPATHLEN];
    char *clDir = NULL;

    if (cl5GetState () != CL5_STATE_OPEN)
    {
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "changelog is not open");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_cl2ldif_task: %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    rlist[0] = r;
    rlist[1] = NULL;

    /* file is stored in the changelog directory and is named
       <replica name>.ldif */
    clDir = cl5GetDir ();
    if (NULL == clDir) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    replica = (Replica*)object_get_data (r);
    if (NULL == replica) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    PR_snprintf (fName, MAXPATHLEN, "%s/%s.ldif", clDir, replica_get_name (replica));
    slapi_ch_free_string (&clDir);

    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                    "Beginning changelog export of replica \"%s\"\n",
                    replica_get_name(replica));
    rc = cl5ExportLDIF (fName, rlist);
    if (rc == CL5_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "Finished changelog export of replica \"%s\"\n",
                        replica_get_name(replica));
        rc = LDAP_SUCCESS;
    } else {
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                     "Failed changelog export replica %s; "
                     "changelog error - %d", replica_get_name(replica), rc);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_cl2ldif_task: %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
    }
bail:
    return rc;
}

static int replica_execute_ldif2cl_task (Object *r, char *returntext)
{
    int rc, imprc = 0;
    Object *rlist [2];
    Replica *replica;
    char fName [MAXPATHLEN];
    char *clDir = NULL;
    changelog5Config config;

    if (cl5GetState () != CL5_STATE_OPEN)
    {
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE, "changelog is not open");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_ldif2cl_task: %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    rlist[0] = r;
    rlist[1] = NULL;

    /* file is stored in the changelog directory and is named
       <replica name>.ldif */
    clDir = cl5GetDir ();
    if (NULL == clDir) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    replica = (Replica*)object_get_data (r);
    if (NULL == replica) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    PR_snprintf (fName, MAXPATHLEN, "%s/%s.ldif", clDir, replica_get_name (replica));

    rc = cl5Close();
    if (rc != CL5_SUCCESS)
    {
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                     "failed to close changelog to import changelog data; "
                     "changelog error - %d", rc);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_ldif2cl_task: %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }
    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                    "Beginning changelog import of replica \"%s\"\n",
                    replica_get_name(replica));
    imprc = cl5ImportLDIF (clDir, fName, rlist);
    slapi_ch_free_string (&clDir);
    if (CL5_SUCCESS == imprc)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "Finished changelog import of replica \"%s\"\n",
                        replica_get_name(replica));
    }
    else
    {
        PR_snprintf (returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                     "Failed changelog import replica %s; "
                     "changelog error - %d", replica_get_name(replica), rc);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
                        "replica_execute_ldif2cl_task: %s\n", returntext);
        imprc = LDAP_OPERATIONS_ERROR;
    }
    changelog5_read_config (&config);
    /* restart changelog */
    rc = cl5Open (config.dir, &config.dbconfig);
    if (CL5_SUCCESS == rc)
    {
        rc = LDAP_SUCCESS;
    }
    else
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
            "replica_execute_ldif2cl_task: failed to start changelog at %s\n",
            config.dir?config.dir:"null config dir");
        rc = LDAP_OPERATIONS_ERROR;
    }
bail:
    changelog5_config_done(&config);
    /* if cl5ImportLDIF returned an error, report it first. */
    return imprc?imprc:rc;
}

static multimaster_mtnode_extension *
_replica_config_get_mtnode_ext (const Slapi_Entry *e)
{
    const char *replica_root;
    Slapi_DN *sdn = NULL;
    mapping_tree_node *mtnode;
    multimaster_mtnode_extension *ext = NULL;
    char ebuf[BUFSIZ];

    /* retirve root of the tree for which replica is configured */
    replica_root = slapi_entry_attr_get_charptr (e, attr_replicaRoot);
    if (replica_root == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_add: "
                        "configuration entry %s missing %s attribute\n",
                        escape_string(slapi_entry_get_dn((Slapi_Entry *)e), ebuf),
                        attr_replicaRoot);   
        return NULL;
    }

    sdn = slapi_sdn_new_dn_passin (replica_root);

    /* locate mapping tree node for the specified subtree */
    mtnode = slapi_get_mapping_tree_node_by_dn (sdn);
    if (mtnode == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_add: "
                        "failed to locate mapping tree node for dn %s\n",
                        escape_string(slapi_sdn_get_dn(sdn), ebuf));        
    }
    else
    {
        /* check if replica object already exists for the specified subtree */
        ext = (multimaster_mtnode_extension *)repl_con_get_ext (REPL_CON_EXT_MTNODE, mtnode);
    }

    slapi_sdn_free (&sdn);

    return ext;
}

int
replica_execute_cleanruv_task_ext(Object *r, ReplicaId rid)
{
    return replica_execute_cleanruv_task(r, rid, NULL);
}

static int
replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext /* not used */)
{
	Object *RUVObj;
	RUV *local_ruv = NULL;
	Replica *replica = (Replica*)object_get_data (r);
	int rc = 0;
	PR_ASSERT (replica);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "cleanruv_task: cleaning rid (%d)...\n",(int)rid);
	RUVObj = replica_get_ruv(replica);
	PR_ASSERT(RUVObj);
	local_ruv =  (RUV*)object_get_data (RUVObj);
	/* Need to check that :
	 *  - rid is not the local one
	 *  - rid is not the last one
	 */
	if ((replica_get_rid(replica) == rid) ||
		(ruv_replica_count(local_ruv) <= 1)) {
		return LDAP_UNWILLING_TO_PERFORM;
	}
	rc = ruv_delete_replica(local_ruv, rid);
	replica_set_ruv_dirty(replica);
	replica_write_ruv(replica);
	object_release(RUVObj);

	/* Update Mapping Tree to reflect RUV changes */
	consumer5_set_mapping_tree_state_for_replica(replica, NULL);

	/*
	 *  Clean the changelog RUV's
	 */
	cl5CleanRUV(rid);

	if (rc != RUV_SUCCESS){
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanruv_task: task failed(%d)\n",rc);
		return LDAP_OPERATIONS_ERROR;
	}
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "cleanruv_task: finished successfully\n");
	return LDAP_SUCCESS;
}

const char *
fetch_attr(Slapi_Entry *e, const char *attrname, const char *default_val)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;

    if (slapi_entry_attr_find(e, attrname, &attr) != 0)
        return default_val;

    slapi_attr_first_value(attr, &val);
    return slapi_value_get_string(val);
}

static int
replica_cleanall_ruv_task(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,
		                      int *returncode, char *returntext, void *arg)
{
    Slapi_Task *task = NULL;
    const Slapi_DN *task_dn;
    Slapi_DN *dn = NULL;
    Object *r;
    const char *base_dn;
    const char *rid_str;
    ReplicaId rid;
    int rc = SLAPI_DSE_CALLBACK_OK;

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    if(task == NULL){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Failed to create new task\n");
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /*
     *  Get our task settings
     */
    if ((base_dn = fetch_attr(e, "replica-base-dn", 0)) == NULL){
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((rid_str = fetch_attr(e, "replica-id", 0)) == NULL){
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    task_dn = slapi_entry_get_sdn(e);
    /*
     *  Check the rid
     */
    rid = atoi(rid_str);
    if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)",
            rid, slapi_sdn_get_dn(task_dn));
        cleanruv_log(task, CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Get the replica object
     */
    dn = slapi_sdn_new_dn_byval(base_dn);
    if((r = replica_get_replica_from_dn(dn)) == NULL){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Could not find replica from dn(%s)",slapi_sdn_get_dn(dn));
        cleanruv_log(task, CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* clean the RUV's */
    rc = replica_execute_cleanall_ruv_task (r, rid, task, returntext);

out:
    if(rc){
        cleanruv_log(task, CLEANALLRUV_ID, "Task failed...(%d)", rc);
        slapi_task_finish(task, *returncode);
    } else {
        rc = SLAPI_DSE_CALLBACK_OK;
    }
    slapi_sdn_free(&dn);

    return rc;
}

/*
 *  CLEANALLRUV task
 *
 *  [1]  Get the maxcsn from the RUV of the rid we want to clean
 *  [2]  Create the payload for the "cleanallruv" extended ops
 *  [3]  Create "monitor" thread to do the real work.
 *
 */
static int
replica_execute_cleanall_ruv_task (Object *r, ReplicaId rid, Slapi_Task *task, char *returntext)
{
    PRThread *thread = NULL;
    Slapi_Task *pre_task = NULL; /* this is supposed to be null for logging */
    Replica *replica;
    Object *ruv_obj;
    cleanruv_data *data = NULL;
    CSN *maxcsn = NULL;
    const RUV *ruv;
    struct berval *payload = NULL;
    char *ridstr = NULL;
    char csnstr[CSN_STRSIZE];
    int rc = 0;

    if(get_cleanruv_task_count() >= CLEANRIDSIZ){
        /* we are already running the maximum number of tasks */
        cleanruv_log(pre_task, CLEANALLRUV_ID,
    	    "Exceeded maximum number of active CLEANALLRUV tasks(%d)",CLEANRIDSIZ);
        return LDAP_UNWILLING_TO_PERFORM;
    }

    /*
     *  Grab the replica
     */
    replica = (Replica*)object_get_data (r);
    /*
     *  Check if this is a consumer
     */
    if(replica_get_type(replica) == REPLICA_TYPE_READONLY){
        /* this is a consumer, send error */
        cleanruv_log(pre_task, CLEANALLRUV_ID, "Failed to clean rid (%d), task can not be run on a consumer",rid);
        if(task){
            rc = -1;
            slapi_task_finish(task, rc);
        }
        return -1;
    }
    /*
     *  Grab the max csn of the deleted replica
     */
    ruv_obj = replica_get_ruv(replica);
    ruv = object_get_data (ruv_obj);
    if(ruv_get_rid_max_csn(ruv, &maxcsn, rid) == RUV_BAD_DATA){
        /* no maxcsn, can not proceed */
        cleanruv_log(pre_task, CLEANALLRUV_ID, "Could not find maxcsn for rid (%d)", rid);
        rc = -1;
        object_release(ruv_obj);
        goto fail;
    } else {
        object_release(ruv_obj);
        if(maxcsn == NULL || csn_get_replicaid(maxcsn) == 0){
            /*
             *  This is for consistency with extop csn creation, where
             *  we want the csn string to be "0000000000000000000" not ""
             */
            csn_free(&maxcsn);
            maxcsn = csn_new();
            csn_init_by_string(maxcsn, "");
        }
        csn_as_string(maxcsn, PR_FALSE, csnstr);
    }
    /*
     *  Create payload
     */
    ridstr = slapi_ch_smprintf("%d:%s:%s", rid, slapi_sdn_get_dn(replica_get_root(replica)), csnstr);
    payload = create_ruv_payload(ridstr);
    slapi_ch_free_string(&ridstr);

    if(payload == NULL){
        cleanruv_log(pre_task, CLEANALLRUV_ID, "Failed to create extended op payload, aborting task");
        rc = -1;
        goto fail;
    }

    /*
     *  Launch the cleanallruv thread.  Once all the replicas are cleaned it will release the rid
     */
    data = (cleanruv_data*)slapi_ch_calloc(1, sizeof(cleanruv_data));
    if (data == NULL) {
        cleanruv_log(pre_task, CLEANALLRUV_ID, "Failed to allocate cleanruv_data.  Aborting task.");
        rc = -1;
        goto fail;
    }
    data->repl_obj = r;
    data->replica = replica;
    data->rid = rid;
    data->task = task;
    data->maxcsn = maxcsn;
    data->payload = payload;
    data->sdn = NULL;

    thread = PR_CreateThread(PR_USER_THREAD, replica_cleanallruv_thread,
        (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
        PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        rc = -1;
        goto fail;
    } else {
        goto done;
    }

fail:
    cleanruv_log(pre_task, CLEANALLRUV_ID, "Failed to clean rid (%d)",rid);
    if(task){
        slapi_task_finish(task, rc);
    }
    if(payload){
        ber_bvfree(payload);
    }
    csn_free(&maxcsn);
    if(task) /* only the task acquires the r obj */
         object_release (r);

done:

    return rc;
}

void
replica_cleanallruv_thread_ext(void *arg)
{
    replica_cleanallruv_thread(arg);
}

/*
 *  CLEANALLRUV Thread
 *
 *  [1]  Wait for the maxcsn to be covered
 *  [2]  Make sure all the replicas are alive
 *  [3]  Set the cleaned rid
 *  [4]  Send the cleanAllRUV extop to all the replicas
 *  [5]  Manually send the CLEANRUV task to replicas that do not support CLEANALLRUV
 *  [6]  Wait for all the replicas to be cleaned.
 *  [7]  Trigger cl trimming, release the rid, and remove all the "cleanallruv" attributes
 *       from the config.
 */
static void
replica_cleanallruv_thread(void *arg)
{
    Object *ruv_obj = NULL;
    Object *agmt_obj = NULL;
    Repl_Agmt *agmt = NULL;
    RUV *ruv = NULL;
    cleanruv_data *data = arg;
    char csnstr[CSN_STRSIZE];
    char *returntext = NULL;
    char *rid_text = NULL;
    int found_dirty_rid = 1;
    int agmt_not_notified = 1;
    int interval = 10;
    int aborted = 0;
    int free_obj = 0;
    int rc = 0;

    /*
     *  Initialize our settings
     */
    if(data->replica == NULL && data->repl_obj == NULL){
        /*
         * This thread was initiated at startup because the process did not finish.  Due
         * to startup timing issues, we need to wait before grabbing the replica obj, as
         * the backends might not be online yet.
         */
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(5) );
        PR_Unlock( notify_lock );
        data->repl_obj = replica_get_replica_from_dn(data->sdn);
        if(data->repl_obj == NULL){
        	cleanruv_log(data->task, CLEANALLRUV_ID, "Unable to retrieve repl object from dn(%s).", data->sdn);
        	aborted = 1;
        	goto done;
        }
        data->replica = (Replica*)object_get_data(data->repl_obj);
        free_obj = 1;
    } else if(data->replica == NULL && data->repl_obj){
        data->replica = (Replica*)object_get_data(data->repl_obj);
    } else if( data->repl_obj == NULL && data->replica){
        data->repl_obj = object_new(data->replica, NULL);
        free_obj = 1;
    }
    /* verify we have set our repl objects */
    if(data->repl_obj == NULL || data->replica == NULL){
    	cleanruv_log(data->task, CLEANALLRUV_ID, "Unable to set the replica objects.");
    	aborted = 1;
    	goto done;
    }
    if(data->task){
        slapi_task_begin(data->task, 1);
    }
    rid_text = slapi_ch_smprintf("{replica %d ldap", data->rid);
    csn_as_string(data->maxcsn, PR_FALSE, csnstr);

    /*
     *  Add the cleanallruv task to the repl config - so we can handle restarts
     */
    cleanruv_log(data->task, CLEANALLRUV_ID, "Cleaning rid (%d)...", data->rid);
    add_cleaned_rid(data->rid, data->replica, csnstr); /* marks config that we started cleaning a rid */
    /*
     *  First, wait for the maxcsn to be covered
     */
    cleanruv_log(data->task, CLEANALLRUV_ID, "Waiting to process all the updates from the deleted replica...");
    ruv_obj = replica_get_ruv(data->replica);
    ruv = object_get_data (ruv_obj);
    while(data->maxcsn && !is_task_aborted(data->rid) && !is_cleaned_rid(data->rid) && !slapi_is_shutting_down()){
        if(csn_get_replicaid(data->maxcsn) == 0 || ruv_covers_csn_cleanallruv(ruv,data->maxcsn)){
            /* We are caught up, now we can clean the ruv's */
            break;
        }
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(5) );
        PR_Unlock( notify_lock );
    }
    object_release(ruv_obj);
    /*
     *  Next, make sure all the replicas are up and running before sending off the clean ruv tasks
     */
    cleanruv_log(data->task, CLEANALLRUV_ID,"Waiting for all the replicas to be online...");
    if(check_agmts_are_alive(data->replica, data->rid, data->task)){
        /* error, aborted or shutdown */
        aborted = 1;
        goto done;
    }
    /*
     *  Make sure all the replicas have seen the max csn
     */
    cleanruv_log(data->task, CLEANALLRUV_ID,"Waiting for all the replicas to receive all the deleted replica updates...");
    if(check_agmts_are_caught_up(data->replica, data->rid, csnstr, data->task)){
        /* error, aborted or shutdown */
        aborted = 1;
        goto done;
    }
    /*
     *  Set the rid as notified - this blocks the changelog from sending out updates
     *  during this process, as well as prevents the db ruv from getting polluted.
     */
    set_cleaned_rid(data->rid);
    /*
     *  Now send the cleanruv extended op to all the agreements
     */
    cleanruv_log(data->task, CLEANALLRUV_ID, "Sending cleanAllRUV task to all the replicas...");
    while(agmt_not_notified && !is_task_aborted(data->rid) && !slapi_is_shutting_down()){
        agmt_obj = agmtlist_get_first_agreement_for_replica (data->replica);
        if(agmt_obj == NULL){
            /* no agmts, just clean this replica */
            break;
        }
        while (agmt_obj){
            agmt = (Repl_Agmt*)object_get_data (agmt_obj);
            if(!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
                agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
                agmt_not_notified = 0;
                continue;
            }
            if(replica_cleanallruv_send_extop(agmt, data->rid, data->task, data->payload, 1) == 0){
                agmt_not_notified = 0;
            } else {
                agmt_not_notified = 1;
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
        }

        if(is_task_aborted(data->rid)){
            aborted = 1;
            goto done;
        }
        if(agmt_not_notified == 0){
           break;
        }
        /*
         *  need to sleep between passes
         */
        cleanruv_log(data->task, CLEANALLRUV_ID, "Not all replicas have received the "
            "cleanallruv extended op, retrying in %d seconds",interval);
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(interval) );
        PR_Unlock( notify_lock );

        if(interval < 14400){ /* 4 hour max */
            interval = interval * 2;
        } else {
            interval = 14400;
        }
    }
    /*
     *  Run the CLEANRUV task
     */
    cleanruv_log(data->task, CLEANALLRUV_ID,"Cleaning local ruv's...");
    replica_execute_cleanruv_task (data->repl_obj, data->rid, returntext);
    /*
     *  Wait for all the replicas to be cleaned
     */
    cleanruv_log(data->task, CLEANALLRUV_ID,"Waiting for all the replicas to be cleaned...");

    interval = 10;
    while(found_dirty_rid && !is_task_aborted(data->rid) && !slapi_is_shutting_down()){
        agmt_obj = agmtlist_get_first_agreement_for_replica (data->replica);
        if(agmt_obj == NULL){
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()){
            agmt = (Repl_Agmt*)object_get_data (agmt_obj);
            if(!agmt_is_enabled(agmt)  || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
                agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
                found_dirty_rid = 0;
                continue;
            }
            if(replica_cleanallruv_check_ruv(agmt, rid_text, data->task) == 0){
                found_dirty_rid = 0;
            } else {
                found_dirty_rid = 1;
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
        }
        /* If the task is abort or everyone is cleaned, break out */
        if(is_task_aborted(data->rid)){
            aborted = 1;
            goto done;
        }
        if(found_dirty_rid == 0){
           break;
        }
        /*
         *  need to sleep between passes
         */
        cleanruv_log(data->task, CLEANALLRUV_ID, "Replicas have not been cleaned yet, "
            "retrying in %d seconds", interval);
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(interval) );
        PR_Unlock( notify_lock );

        if(interval < 14400){ /* 4 hour max */
            interval = interval * 2;
        } else {
            interval = 14400;
        }
    } /* while */

done:
    /*
     *  If the replicas are cleaned, release the rid, and trim the changelog
     */
    if(!aborted){
        trigger_cl_trimming(data->rid);
        delete_cleaned_rid(data->replica, data->rid, data->maxcsn);
        cleanruv_log(data->task, CLEANALLRUV_ID, "Successfully cleaned rid(%d).", data->rid);
        slapi_task_finish(data->task, rc);
    } else {
        /*
         *  Shutdown or abort
         */
        if(!is_task_aborted(data->rid)){
            cleanruv_log(data->task, CLEANALLRUV_ID,"Server shutting down.  Process will resume at server startup");
        } else {
            cleanruv_log(data->task, CLEANALLRUV_ID,"Task aborted for rid(%d).",data->rid);
        }
        if(data->task){
            slapi_task_finish(data->task, rc);
        }
    }

    if(data->payload){
        ber_bvfree(data->payload);
    }
    if(data->repl_obj && free_obj){
        object_release(data->repl_obj);
    }
    slapi_sdn_free(&data->sdn);
    slapi_ch_free_string(&rid_text);
    csn_free(&data->maxcsn);
    slapi_ch_free((void **)&data);
}

/*
 *  Waits for all the repl agmts to be have have the maxcsn.  Returns error only on abort or shutdown
 */
static int
check_agmts_are_caught_up(Replica *replica, ReplicaId rid, char *maxcsn, Slapi_Task *task)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    char *rid_text;
    int not_all_caughtup = 1;
    int interval = 10;

    rid_text = slapi_ch_smprintf("{replica %d ldap", rid);

    while(not_all_caughtup && !is_task_aborted(rid) && !slapi_is_shutting_down()){
        agmt_obj = agmtlist_get_first_agreement_for_replica (replica);
        if(agmt_obj == NULL){
            not_all_caughtup = 0;
            break;
        }
        while (agmt_obj){
            agmt = (Repl_Agmt*)object_get_data (agmt_obj);
            if(!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
                agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
                not_all_caughtup = 0;
                continue;
            }
            if(replica_cleanallruv_check_maxcsn(agmt, rid_text, maxcsn, task) == 0){
                not_all_caughtup = 0;
            } else {
                not_all_caughtup = 1;
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
        } /* agmt while */

        if(not_all_caughtup == 0 || is_task_aborted(rid) ){
            break;
        }
        cleanruv_log(task, CLEANALLRUV_ID, "Not all replicas caught up, retrying in %d seconds",interval);
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(interval) );
        PR_Unlock( notify_lock );

        if(interval < 14400){ /* 4 hour max */
            interval = interval * 2;
        } else {
            interval = 14400;
        }
    }
    slapi_ch_free_string(&rid_text);

    if(is_task_aborted(rid)){
        return -1;
    }

    return not_all_caughtup;
}

/*
 *  Waits for all the repl agmts to be online.  Returns error only on abort or shutdown
 */
static int
check_agmts_are_alive(Replica *replica, ReplicaId rid, Slapi_Task *task)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    int not_all_alive = 1;
    int interval = 10;

    while(not_all_alive && is_task_aborted(rid) == 0 && !slapi_is_shutting_down()){
        agmt_obj = agmtlist_get_first_agreement_for_replica (replica);
        if(agmt_obj == NULL){
            not_all_alive = 0;
            break;
        }
        while (agmt_obj){
            agmt = (Repl_Agmt*)object_get_data (agmt_obj);
            if(!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
                agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
                not_all_alive = 0;
                continue;
            }
            if(replica_cleanallruv_replica_alive(agmt) == 0){
                not_all_alive = 0;
            } else {
                not_all_alive = 1;
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
        }

        if(not_all_alive == 0 || is_task_aborted(rid)){
            break;
        }
        cleanruv_log(task, CLEANALLRUV_ID, "Not all replicas online, retrying in %d seconds...",interval);
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(interval) );
        PR_Unlock( notify_lock );

        if(interval < 14400){ /* 4 hour max */
            interval = interval * 2;
        } else {
            interval = 14400;
        }
    }
    if(is_task_aborted(rid)){
        return -1;
    }

    return not_all_alive;
}

/*
 *  Create the CLEANALLRUV extended op payload
 */
struct berval *
create_ruv_payload(char *value)
{
    struct berval *req_data = NULL;
    BerElement *tmp_bere = NULL;

    if ((tmp_bere = der_alloc()) == NULL){
        goto error;
    }
    if (ber_printf(tmp_bere, "{s", value) == -1){
        goto error;
    }
    if (ber_printf(tmp_bere, "}") == -1){
        goto error;
    }
    if (ber_flatten(tmp_bere, &req_data) == -1){
        goto error;
    }
    goto done;

error:
    if (NULL != req_data){
        ber_bvfree(req_data);
        req_data = NULL;
    }

done:
    if (NULL != tmp_bere){
        ber_free(tmp_bere, 1);
        tmp_bere = NULL;
    }

    return req_data;
}

/*
 *  Manually add the CLEANRUV task to replicas that do not support
 *  the CLEANALLRUV task.
 */
static void
replica_send_cleanruv_task(Repl_Agmt *agmt, ReplicaId rid, Slapi_Task *task)
{
    Repl_Connection *conn;
    ConnResult crc = 0;
    LDAP *ld;
    Slapi_DN *sdn;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *repl_dn = NULL;
    char data[15];
    int rc;

    if((conn = conn_new(agmt)) == NULL){
        return;
    }
    crc = conn_connect(conn);
    if (CONN_OPERATION_SUCCESS != crc){
        conn_delete_internal_ext(conn);
        return;
    }
    ld = conn_get_ldap(conn);
    if(ld == NULL){
        conn_delete_internal_ext(conn);
        return;
    }
    val.bv_len = PR_snprintf(data, sizeof(data), "CLEANRUV%d", rid);
    sdn = agmt_get_replarea(agmt);
    mod.mod_op  = LDAP_MOD_ADD|LDAP_MOD_BVALUES;
    mod.mod_type = "nsds5task";
    mod.mod_bvalues = vals;
    vals [0] = &val;
    vals [1] = NULL;
    val.bv_val = data;
    mods[0] = &mod;
    mods[1] = NULL;
    repl_dn = slapi_create_dn_string("cn=replica,cn=%s,cn=mapping tree,cn=config", slapi_sdn_get_dn(sdn));
    /*
     *  Add task to remote replica
     */
    rc = ldap_modify_ext_s( ld, repl_dn, mods, NULL, NULL);

    if(rc != LDAP_SUCCESS){
    	cleanruv_log(task, CLEANALLRUV_ID, "Failed to add CLEANRUV task replica "
            "(%s).  You will need to manually run the CLEANRUV task on this replica (%s) error (%d)",
            agmt_get_long_name(agmt), agmt_get_hostname(agmt), rc);
    }
    slapi_ch_free_string(&repl_dn);
    slapi_sdn_free(&sdn);
    conn_delete_internal_ext(conn);
}

/*
 *  Check if the rid is in our list of "cleaned" rids
 */
int
is_cleaned_rid(ReplicaId rid)
{
    int i;

    slapi_rwlock_rdlock(rid_lock);
    for(i = 0; i < CLEANRIDSIZ && cleaned_rids[i] != 0; i++){
        if(rid == cleaned_rids[i]){
            slapi_rwlock_unlock(rid_lock);
            return 1;
        }
    }
    slapi_rwlock_unlock(rid_lock);

    return 0;
}

int
is_task_aborted(ReplicaId rid)
{
	int i;

    if(rid == 0){
        return 0;
    }
    slapi_rwlock_rdlock(abort_rid_lock);
    for(i = 0; i < CLEANRIDSIZ && aborted_rids[i] != 0; i++){
        if(rid == aborted_rids[i]){
            slapi_rwlock_unlock(abort_rid_lock);
            return 1;
        }
    }
    slapi_rwlock_unlock(abort_rid_lock);
    return 0;
}

/*
 *  Just add the rid to the in memory, as we don't want it to survive after a restart,
 *  This prevent the changelog from sending updates from this rid, and the local ruv
 *  will not be updated either.
 */
void
set_cleaned_rid(ReplicaId rid)
{
    int i;

    slapi_rwlock_wrlock(rid_lock);
    for(i = 0; i < CLEANRIDSIZ; i++){
        if(cleaned_rids[i] == 0){
            cleaned_rids[i] = rid;
            cleaned_rids[i + 1] = 0;
        }
    }
    slapi_rwlock_unlock(rid_lock);
}

/*
 *  Add the rid and maxcsn to the repl config (so we can resume after a server restart)
 */
void
add_cleaned_rid(ReplicaId rid, Replica *r, char *maxcsn)
{
    Slapi_PBlock *pb;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char data[CSN_STRSIZE + 10];
    char *dn;
    int rc;

    if(r == NULL || maxcsn == NULL){
        return;
    }
    /*
     *  Write the rid & maxcsn to the config entry
     */
    val.bv_len = PR_snprintf(data, sizeof(data),"%d:%s", rid, maxcsn);
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    mod.mod_op  = LDAP_MOD_ADD|LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaCleanRUV;
    mod.mod_bvalues = vals;
    vals [0] = &val;
    vals [1] = NULL;
    val.bv_val = data;
    mods[0] = &mod;
    mods[1] = NULL;

    replica_add_cleanruv_data(r, val.bv_val);

    slapi_modify_internal_set_pb (pb, dn, mods, NULL, NULL,
        repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS && rc != LDAP_TYPE_OR_VALUE_EXISTS){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: failed to update replica "
            "config (%d), rid (%d)\n", rc, rid);
    }
    slapi_ch_free_string(&dn);
    slapi_pblock_destroy(pb);
}

/*
 *  Add aborted rid and repl root to config in case of a server restart
 */
void
add_aborted_rid(ReplicaId rid, Replica *r, char *repl_root)
{
    Slapi_PBlock *pb;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *data;
    char *dn;
    int rc;
    int i;

    slapi_rwlock_wrlock(abort_rid_lock);
    for(i = 0; i < CLEANRIDSIZ; i++){
        if(aborted_rids[i] == 0){
            aborted_rids[i] = rid;
            aborted_rids[i + 1] = 0;
            break;
        }
    }
    slapi_rwlock_unlock(abort_rid_lock);
    /*
     *  Write the rid to the config entry
     */
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    data = PR_smprintf("%d:%s", rid, repl_root);
    mod.mod_op  = LDAP_MOD_ADD|LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaAbortCleanRUV;
    mod.mod_bvalues = vals;
    vals [0] = &val;
    vals [1] = NULL;
    val.bv_val = data;
    val.bv_len = strlen (data);
    mods[0] = &mod;
    mods[1] = NULL;

    slapi_modify_internal_set_pb (pb, dn, mods, NULL, NULL,
        repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: failed to update "
        "replica config (%d), rid (%d)\n", rc, rid);
    }

    slapi_ch_free_string(&dn);
    slapi_ch_free_string(&data);
    slapi_pblock_destroy(pb);
}

void
delete_aborted_rid(Replica *r, ReplicaId rid, char *repl_root){
    Slapi_PBlock *pb;
    LDAPMod *mods[2];
    LDAPMod mod;
    struct berval *vals[2];
    struct berval val;
    char *data;
    char *dn;
    int rc;
    int i;

    if(r == NULL)
        return;

    /*
     *  Remove this rid, and optimize the array
     */
    slapi_rwlock_wrlock(abort_rid_lock);
    for(i = 0; i < CLEANRIDSIZ && aborted_rids[i] != rid; i++); /* found rid, stop */
    for(; i < CLEANRIDSIZ; i++){
        /* rewrite entire array */
        aborted_rids[i] = aborted_rids[i + 1];
    }
    slapi_rwlock_unlock(abort_rid_lock);
    /*
     *  Prepare the mods for the config entry
     */
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    data = PR_smprintf("%d:%s", (int)rid, repl_root);

    mod.mod_op  = LDAP_MOD_DELETE|LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaAbortCleanRUV;
    mod.mod_bvalues = vals;
    vals [0] = &val;
    vals [1] = NULL;
    val.bv_val = data;
    val.bv_len = strlen (data);
    mods[0] = &mod;
    mods[1] = NULL;

    slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Abort CleanAllRUV Task: failed to remove replica "
            "config (%d), rid (%d)\n", rc, rid);
    }
    slapi_pblock_destroy (pb);
    slapi_ch_free_string(&dn);
    slapi_ch_free_string(&data);
}

/*
 *  Remove the rid from our list, and the config
 */
void
delete_cleaned_rid(Replica *r, ReplicaId rid, CSN *maxcsn)
{
    Slapi_PBlock *pb;
    Object *agmt_obj;
    Repl_Agmt *agmt;
    LDAPMod *mods[2];
    LDAPMod mod;
    struct berval *vals[2];
    struct berval val;
    char *dn;
    char data[CSN_STRSIZE + 10];
    char csnstr[CSN_STRSIZE];
    int rc;
    int i;

    if(r == NULL || maxcsn == NULL)
        return;

    /*
     *  Remove this rid, and optimize the array
     */
    slapi_rwlock_wrlock(rid_lock);
    for(i = 0; i < CLEANRIDSIZ && cleaned_rids[i] != rid; i++); /* found rid, stop */
    for(; i < CLEANRIDSIZ; i++){
        /* rewrite entire array */
        cleaned_rids[i] = cleaned_rids[i + 1];
    }
    slapi_rwlock_unlock(rid_lock);
    /*
     *  Prepare the mods for the config entry
     */
    csn_as_string(maxcsn, PR_FALSE, csnstr);
    val.bv_len = PR_snprintf(data, sizeof(data), "%d:%s", (int)rid, csnstr);
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    mod.mod_op  = LDAP_MOD_DELETE|LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaCleanRUV;
    mod.mod_bvalues = vals;
    vals [0] = &val;
    vals [1] = NULL;
    val.bv_val = data;
    mods[0] = &mod;
    mods[1] = NULL;

    replica_remove_cleanruv_data(r, data);

    slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS){
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "CleanAllRUV Task: failed to remove replica config "
            "(%d), rid (%d)\n", rc, rid);
    }
    slapi_pblock_destroy (pb);
    slapi_ch_free_string(&dn);
    /*
     *  Now release the cleaned rid from the repl agmts
     */
    agmt_obj = agmtlist_get_first_agreement_for_replica (r);
    while (agmt_obj){
        agmt = (Repl_Agmt*)object_get_data (agmt_obj);
        if(!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
            agmt_obj = agmtlist_get_next_agreement_for_replica (r, agmt_obj);
            continue;
        }
        agmt_set_cleanruv_data(agmt, rid, CLEANRUV_RELEASED);
        agmt_obj = agmtlist_get_next_agreement_for_replica (r, agmt_obj);
    }
}

/*
 *  Abort the CLEANALLRUV task
 */
int
replica_cleanall_ruv_abort(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter,
        int *returncode, char *returntext, void *arg)
{
    PRThread *thread = NULL;
    struct berval *payload = NULL;
    Slapi_Task *task = NULL;
    Replica *replica;
    ReplicaId rid;
    cleanruv_data *data = NULL;
    Slapi_DN *sdn = NULL;
    Object *r;
    CSN *maxcsn = NULL;
    const char *base_dn;
    const char *rid_str;
    const char *certify_all;
    char *ridstr = NULL;
    int rc = SLAPI_DSE_CALLBACK_OK;

    if(get_abort_cleanruv_task_count() >= CLEANRIDSIZ){
        /* we are already running the maximum number of tasks */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Exceeded maximum number of active ABORT CLEANALLRUV tasks(%d)",CLEANRIDSIZ);
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    /*
     *  Get our task settings
     */
    if ((rid_str = fetch_attr(e, "replica-id", 0)) == NULL){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing required attr \"replica-id\"");
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((base_dn = fetch_attr(e, "replica-base-dn", 0)) == NULL){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing required attr \"replica-base-dn\"");
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    certify_all = fetch_attr(e, "replica-certify-all", 0);
    /*
     *  Check the rid
     */
    rid = atoi(rid_str);
    if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)",
            rid, slapi_sdn_get_dn(slapi_entry_get_sdn(e)));
        cleanruv_log(task, ABORT_CLEANALLRUV_ID,"%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Get the replica object
     */
    sdn = slapi_sdn_new_dn_byval(base_dn);
    if((r = replica_get_replica_from_dn(sdn)) == NULL){
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to find replica from dn(%s)", base_dn);
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Check verify value
     */
    if(certify_all){
        if(strcasecmp(certify_all,"yes") && strcasecmp(certify_all,"no")){
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid value for \"replica-certify-all\", the value "
                "must be \"yes\" or \"no\".");
            cleanruv_log(task, ABORT_CLEANALLRUV_ID, "%s", returntext);
            *returncode = LDAP_OPERATIONS_ERROR;
            rc = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
    } else {
        certify_all = "no";
    }
    /*
     *  Create payload
     */
    ridstr = slapi_ch_smprintf("%d:%s:%s", rid, base_dn, certify_all);
    payload = create_ruv_payload(ridstr);

    if(payload == NULL){
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "Failed to create extended op payload, aborting task");
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Stop the cleaning, and delete the rid
     */
    replica = (Replica*)object_get_data (r);
    maxcsn = replica_get_cleanruv_maxcsn(replica, rid);
    delete_cleaned_rid(replica, rid, maxcsn);
    add_aborted_rid(rid, replica, (char *)base_dn);
    stop_ruv_cleaning();
    /*
     *  Prepare the abort struct and fire off the thread
     */
    data = (cleanruv_data*)slapi_ch_calloc(1, sizeof(cleanruv_data));
    if (data == NULL) {
        cleanruv_log(task, ABORT_CLEANALLRUV_ID,"Failed to allocate abort_cleanruv_data.  Aborting task.");
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    data->repl_obj = r; /* released in replica_abort_task_thread() */
    data->replica = replica;
    data->task = task;
    data->payload = payload;
    data->rid = rid;
    data->repl_root = slapi_ch_strdup(base_dn);
    data->sdn = NULL;
    data->certify = slapi_ch_strdup(certify_all);

    thread = PR_CreateThread(PR_USER_THREAD, replica_abort_task_thread,
                (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        object_release(r);
        cleanruv_log(task, ABORT_CLEANALLRUV_ID,"Unable to create abort thread.  Aborting task.");
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
    }

out:
    csn_free(&maxcsn);
    slapi_ch_free_string(&ridstr);
    slapi_sdn_free(&sdn);

    if(rc != SLAPI_DSE_CALLBACK_OK){
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "Abort Task failed (%d)", rc);
        slapi_task_finish(task, rc);
    }

    return rc;
}

/*
 *  Abort CLEANALLRUV task thread
 */
void
replica_abort_task_thread(void *arg)
{
    cleanruv_data *data = (cleanruv_data *)arg;
    Repl_Agmt *agmt;
    Object *agmt_obj;
    int agmt_not_notified = 1;
    int interval = 10;
    int release_it = 0;

    /*
     *   Need to build the replica from the dn
     */
    if(data->replica == NULL && data->repl_obj == NULL){
        /*
         * This thread was initiated at startup because the process did not finish.  Due
         * to timing issues, we need to wait to grab the replica obj until we get here.
         */
        if((data->repl_obj = replica_get_replica_from_dn(data->sdn)) == NULL){
            cleanruv_log(data->task, ABORT_CLEANALLRUV_ID, "Failed to get replica object from dn (%s).", slapi_sdn_get_dn(data->sdn));
            goto done;
        }
        if(data->replica == NULL && data->repl_obj){
            data->replica = (Replica*)object_get_data(data->repl_obj);
        }
        release_it = 1;
    }

    /*
     *  Now send the cleanruv extended op to all the agreements
     */
    while(agmt_not_notified && !slapi_is_shutting_down()){
        agmt_obj = agmtlist_get_first_agreement_for_replica (data->replica);
        if(agmt_obj == NULL){
        	agmt_not_notified = 0;
        	break;
        }
        while (agmt_obj){
            agmt = (Repl_Agmt*)object_get_data (agmt_obj);
            if(!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS){
                agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
                agmt_not_notified = 0;
                continue;
            }
            if(replica_cleanallruv_send_abort_extop(agmt, data->task, data->payload)){
                if(strcasecmp(data->certify,"yes") == 0){
                    /* we are verifying all the replicas receive the abort task */
                    agmt_not_notified = 1;
                    break;
                } else {
                    /* we do not care if we could not reach a replica, just continue as if we did */
                    agmt_not_notified = 0;
                }
            } else {
                /* success */
                agmt_not_notified = 0;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica (data->replica, agmt_obj);
        } /* while loop for agmts */

        if(agmt_not_notified == 0){
            /* everybody has been contacted */
            break;
        }
        /*
         *  need to sleep between passes
         */
        cleanruv_log(data->task, ABORT_CLEANALLRUV_ID,"Retrying in %d seconds",interval);
        PR_Lock( notify_lock );
        PR_WaitCondVar( notify_cvar, PR_SecondsToInterval(interval) );
        PR_Unlock( notify_lock );

        if(interval < 14400){ /* 4 hour max */
            interval = interval * 2;
        } else {
            interval = 14400;
        }
    } /* while */

done:
    if(agmt_not_notified){
        /* failure */
        cleanruv_log(data->task, ABORT_CLEANALLRUV_ID,"Abort task failed, will resume the task at the next server startup.");
    } else {
        /*
         *  Clean up the config
         */
        delete_aborted_rid(data->replica, data->rid, data->repl_root);
        cleanruv_log(data->task, ABORT_CLEANALLRUV_ID, "Successfully aborted cleanAllRUV task for rid(%d)", data->rid);
    }
    if(data->task){
        slapi_task_finish(data->task, agmt_not_notified);
    }
    if(data->repl_obj && release_it)
        object_release(data->repl_obj);
    if(data->payload){
        ber_bvfree(data->payload);
    }
    slapi_ch_free_string(&data->repl_root);
    slapi_ch_free_string(&data->certify);
    slapi_sdn_free(&data->sdn);
    slapi_ch_free((void **)&data);
}

static int
replica_cleanallruv_send_abort_extop(Repl_Agmt *ra, Slapi_Task *task, struct berval *payload)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    int msgid = 0;
    int rc = 0;

    if((conn = conn_new(ra)) == NULL){
        return -1;
    }
    if(conn_connect(conn) == CONN_OPERATION_SUCCESS){
        crc = conn_send_extended_operation(conn, REPL_ABORT_CLEANRUV_OID, payload, NULL, &msgid);
        /*
         * success or failure, just return the error code
         */
        rc = crc;
        if(rc){
        	cleanruv_log(task, ABORT_CLEANALLRUV_ID, "Failed to send extop to replica(%s).", agmt_get_long_name(ra));
        }
    } else {
        cleanruv_log(task, ABORT_CLEANALLRUV_ID, "Failed to connect to replica(%s).", agmt_get_long_name(ra));
        rc = -1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}


static int
replica_cleanallruv_send_extop(Repl_Agmt *ra, ReplicaId rid, Slapi_Task *task, struct berval *payload, int check_result)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    int msgid = 0;
    int rc = 0;

    if((conn = conn_new(ra)) == NULL){
        return -1;
    }
    if(conn_connect(conn) == CONN_OPERATION_SUCCESS){
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_OID, payload, NULL, &msgid);
        if(crc == CONN_OPERATION_SUCCESS && check_result){
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc ){
                struct berval **ruv_bervals = NULL;
                struct berval *data = NULL;
                char *data_guid = NULL;

                decode_repl_ext_response(retsdata, &rc, &ruv_bervals, &data_guid, &data);
                /* just free everything, we only wanted "rc" */
                slapi_ch_free_string(&data_guid);
                if(data)
                    ber_bvfree(data);
                if (ruv_bervals)
                    ber_bvecfree(ruv_bervals);

                if(rc == 0 ){ /* rc == 1 is success */
                    cleanruv_log(task, CLEANALLRUV_ID,"Replica %s does not support the CLEANALLRUV task.  Sending replica CLEANRUV task...",
                        slapi_sdn_get_dn(agmt_get_dn_byref(ra)));
                    /*
                     *  Ok, this replica doesn't know about CLEANALLRUV, so just manually
                     *  add the CLEANRUV task to the replica.
                     */
                    replica_send_cleanruv_task(ra, rid, task);
                } else {
                    /* extop was accepted */
                    rc = 0;
                }
                if (NULL != retoid)
                    ldap_memfree(retoid);
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
            }
            agmt_set_cleanruv_data(ra, rid, CLEANRUV_NOTIFIED);
        } else {
            /*
             * success or failure, just return the error code
             */
            rc = crc;
        }
    } else {
        rc =-1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static int
replica_cleanallruv_check_maxcsn(Repl_Agmt *agmt, char *rid_text, char *maxcsn, Slapi_Task *task)
{
    Repl_Connection *conn = NULL;
    LDAP *ld;
    Slapi_DN *sdn;
    struct berval **vals;
    LDAPMessage *result = NULL, *entry = NULL;
    BerElement *ber;
    char *attrs[2];
    char *attr = NULL;
    char *iter = NULL;
    char *ruv_part = NULL;
    int found_rid = 0;
    int part_count = 0;
    int rc = 0, i;

    if((conn = conn_new(agmt)) == NULL){
        return -1;
    }

    if(conn_connect(conn) == CONN_OPERATION_SUCCESS){
        attrs[0] = "nsds50ruv";
        attrs[1] = NULL;
        ld = conn_get_ldap(conn);
        if(ld == NULL){
            conn_delete_internal_ext(conn);
            return -1;
        }
        sdn = agmt_get_replarea(agmt);
        rc = ldap_search_ext_s(ld, slapi_sdn_get_dn(sdn), LDAP_SCOPE_SUBTREE,
            "(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))",
            attrs, 0, NULL, NULL, NULL, 0, &result);
        slapi_sdn_free(&sdn);
        if(rc != LDAP_SUCCESS){
        	cleanruv_log(task, CLEANALLRUV_ID,"Failed to contact "
                "agmt (%s) error (%d), will retry later.", agmt_get_long_name(agmt), rc);
            conn_delete_internal_ext(conn);
            return -1;
        }
        entry = ldap_first_entry( ld, result );
        if ( entry != NULL ) {
            for ( attr = ldap_first_attribute( ld, entry, &ber ); attr != NULL; attr = ldap_next_attribute( ld, entry, ber ) ){
                /* make sure the attribute is nsds50ruv */
                if(strcasecmp(attr,"nsds50ruv") != 0){
                    ldap_memfree( attr );
                    continue;
                }
                found_rid = 0;
                if ((vals = ldap_get_values_len( ld, entry, attr)) != NULL ) {
                    for ( i = 0; vals[i] && vals[i]->bv_val; i++ ) {
                        /* look for this replica */
                        if(strstr(vals[i]->bv_val, rid_text)){
                            found_rid = 1;
                            /* get the max csn compare it to our known max csn */
                            ruv_part = ldap_utf8strtok_r(vals[i]->bv_val, " ", &iter);
                            for(part_count = 1; ruv_part && part_count < 5; part_count++){
                                ruv_part = ldap_utf8strtok_r(iter, " ", &iter);
                            }
                            if(part_count == 5 && ruv_part){
                                /* we have the maxcsn */
                                if(strcmp(ruv_part, maxcsn)){
                                    /* we are not caught up yet, free, and return */
                                    ldap_value_free_len(vals);
                                    ldap_memfree( attr );
                                    ldap_msgfree( result );
                                    if(ber){
                                        ber_free( ber, 0 );
                                    }
                                    conn_delete_internal_ext(conn);
                                    return -1;
                                } else {
                                    /* ok this replica has all the updates from the deleted replica */
                                    rc = 0;
                                }
                            } else {
                                /* there is no maxcsn for this rid - treat it as caught up */
                                rc = 0;
                            }
                        }
                    }
                    if(!found_rid){
                        /* must have been cleaned already */
                        rc = 0;
                    }
                    ldap_value_free_len(vals);
                }
                ldap_memfree( attr );
            }
            if ( ber != NULL ) {
                ber_free( ber, 0 );
            }
        }
        if(result)
            ldap_msgfree( result );
    } else {
        rc = -1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static int
replica_cleanallruv_replica_alive(Repl_Agmt *agmt)
{
    Repl_Connection *conn = NULL;
    LDAP *ld = NULL;
    LDAPMessage *result = NULL;
    int rc = 0;

    if((conn = conn_new(agmt)) == NULL){
        return -1;
    }
    if(conn_connect(conn) == CONN_OPERATION_SUCCESS){
        ld = conn_get_ldap(conn);
        if(ld == NULL){
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "cleanAllRUV_task: failed to get LDAP "
                "handle from the replication agmt (%s).  Moving on to the next agmt.\n",agmt_get_long_name(agmt));
            conn_delete_internal_ext(conn);
            return -1;
        }
        if(ldap_search_ext_s(ld, "", LDAP_SCOPE_BASE, "objectclass=top",
            NULL, 0, NULL, NULL, NULL, 0, &result) == LDAP_SUCCESS)
        {
            rc = 0;
        } else {
            rc = -1;
        }
        if(result)
            ldap_msgfree( result );
    } else {
        rc = -1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static int
replica_cleanallruv_check_ruv(Repl_Agmt *ra, char *rid_text, Slapi_Task *task)
{
    Repl_Connection *conn = NULL;
    BerElement *ber = NULL;
    struct berval **vals = NULL;
    LDAPMessage *result = NULL, *entry = NULL;
    LDAP *ld = NULL;
    Slapi_DN *sdn;
    char *attrs[2];
    char *attr = NULL;
    int rc = 0, i;

    if((conn = conn_new(ra)) == NULL){
        return -1;
    }
    if(conn_connect(conn) == CONN_OPERATION_SUCCESS){
        attrs[0] = "nsds50ruv";
        attrs[1] = NULL;
        ld = conn_get_ldap(conn);
        if(ld == NULL){
        	cleanruv_log(task, CLEANALLRUV_ID,"Failed to get LDAP handle from "
                "the replication agmt (%s).  Moving on to the next agmt.",agmt_get_long_name(ra));
            rc = -1;
            goto done;
        }

        sdn = agmt_get_replarea(ra);
        rc = ldap_search_ext_s(ld, slapi_sdn_get_dn(sdn), LDAP_SCOPE_SUBTREE,
            "(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))",
            attrs, 0, NULL, NULL, NULL, 0, &result);
        slapi_sdn_free(&sdn);
        if(rc != LDAP_SUCCESS){
            cleanruv_log(task, CLEANALLRUV_ID,"Failed to contact "
                "agmt (%s) error (%d), will retry later.", agmt_get_long_name(ra), rc);
            rc = -1;
            goto done;
        }
        entry = ldap_first_entry( ld, result );
        if ( entry != NULL ) {
            for ( attr = ldap_first_attribute( ld, entry, &ber ); attr != NULL; attr = ldap_next_attribute( ld, entry, ber ) ){
                /* make sure the attribute is nsds50ruv */
                if(strcasecmp(attr,"nsds50ruv") != 0){
                    ldap_memfree( attr );
                    continue;
                }
                if ((vals = ldap_get_values_len( ld, entry, attr)) != NULL ) {
                    for ( i = 0; vals[i] && vals[i]->bv_val; i++ ) {
                        /* look for this replica */
                        if(strstr(vals[i]->bv_val, rid_text)){
                            /* rid has not been cleaned yet, free and return */
                            rc = -1;
                            ldap_value_free_len(vals);
                            ldap_memfree( attr );
                            if ( ber != NULL ) {
                                ber_free( ber, 0 );
                                ber = NULL;
                            }
                            goto done;
                        } else {
                            rc = 0;
                        }
                    }
                    ldap_value_free_len(vals);
                }
                ldap_memfree( attr );
            } /* for loop */
            if ( ber != NULL ) {
                ber_free( ber, 0 );
            }
        }
done:
        if(result)
            ldap_msgfree( result );
    } else {
        return -1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static int
get_cleanruv_task_count()
{
   int i, count = 0;

   slapi_rwlock_wrlock(rid_lock);
   for(i = 0; i < CLEANRIDSIZ; i++){
       if(cleaned_rids[i] != 0){
           count++;
       }
   }
   slapi_rwlock_unlock(rid_lock);

   return count;
}

static int
get_abort_cleanruv_task_count()
{
   int i, count = 0;

   slapi_rwlock_wrlock(rid_lock);
   for(i = 0; i < CLEANRIDSIZ; i++){
       if(aborted_rids[i] != 0){
           count++;
       }
   }
   slapi_rwlock_unlock(rid_lock);

   return count;
}

/*
 *  Notify sleeping CLEANALLRUV threads to stop
 */
void
stop_ruv_cleaning()
{
    if(notify_lock){
        PR_Lock( notify_lock );
        PR_NotifyCondVar( notify_cvar );
        PR_Unlock( notify_lock );
    }
}

/*
 *  Write our logging to the task and error log
 */
void
cleanruv_log(Slapi_Task *task, char *task_type, char *fmt, ...)
{
    va_list ap1;
    va_list ap2;
    va_list ap3;
    va_list ap4;
    char *errlog_fmt;

    va_start(ap1, fmt);
    va_start(ap2, fmt);
    va_start(ap3, fmt);
    va_start(ap4, fmt);

    if(task){
        slapi_task_log_notice_ext(task, fmt, ap1);
        slapi_task_log_status_ext(task, fmt, ap2);
        slapi_task_inc_progress(task);
    }
    errlog_fmt = PR_smprintf("%s: %s\n",task_type, fmt);
    slapi_log_error_ext(SLAPI_LOG_FATAL, repl_plugin_name, errlog_fmt, ap3, ap4);
    slapi_ch_free_string(&errlog_fmt);

    va_end(ap1);
    va_end(ap2);
    va_end(ap3);
    va_end(ap4);
}

