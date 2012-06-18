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
#define RELEASERUV          "RELEASERUV"
#define RELEASERUVLEN       10
#define REPLICA_RDN         "cn=replica"

int slapi_log_urp = SLAPI_LOG_REPL;
static ReplicaId cleaned_rid = 0;
static int released_rid = 0;

/* Forward Declartions */
static int replica_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_post_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

static int replica_config_change_type_and_id (Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods);
static int replica_config_change_updatedn (Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_flags (Replica *r, const char *new_flags,  char *returntext, int apply_mods);
static int replica_execute_task (Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_execute_cl2ldif_task (Object *r, char *returntext);
static int replica_execute_ldif2cl_task (Object *r, char *returntext);
static int replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext);
static int replica_execute_cleanall_ruv_task (Object *r, ReplicaId rid, char *returntext);
static int replica_execute_release_ruv_task(Object *r, ReplicaId rid);
static struct berval *create_ruv_payload(char *value);
static int replica_cleanup_task (Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_task_done(Replica *replica);
static multimaster_mtnode_extension * _replica_config_get_mtnode_ext (const Slapi_Entry *e);
int g_get_shutdown();

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
                        "failed to cretate configuration lock; NSPR error - %d\n",
                        PR_GetError ());
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
            else if (mods[i]->mod_op & LDAP_MOD_DELETE || mods[i]->mod_bvalues == NULL
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
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n", 
                                    errortext);
                }                
            }
            else    /* modify an attribute */
            {
                config_attr_value = (char *) mods[i]->mod_bvalues[0]->bv_val;

                if (strcasecmp (config_attr, attr_replicaBindDn) == 0)
			    {
				    *returncode = replica_config_change_updatedn (r, mods[i],
												       errortext, apply_mods);
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
                    *returncode = replica_config_change_flags (r, config_attr_value,
													           errortext, apply_mods);	
                }
                else if (strcasecmp (config_attr, TASK_ATTR) == 0) 
                {
                    *returncode = replica_execute_task (mtnode_ext->replica, config_attr_value,
													    errortext, apply_mods);
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
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "replica_config_modify: %s\n", 
                                    errortext);
                } 
            }
		}

		if (new_repl_id || new_repl_type)
		{
			*returncode = replica_config_change_type_and_id(r, new_repl_type,
															new_repl_id, errortext,
															apply_mods);
			slapi_ch_free_string(&new_repl_id);
			slapi_ch_free_string(&new_repl_type);
		}
	}

done:
    if (mtnode_ext->replica)
        object_release (mtnode_ext->replica);
	
	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free ((void**)&replica_root);

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
            else if (mods[i]->mod_op & LDAP_MOD_DELETE || 
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
			return replica_execute_cleanall_ruv_task(r, (ReplicaId)temprid, returntext);
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
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "cleanruv_extop: calling clean_ruv_ext\n");
	return replica_execute_cleanruv_task(r, rid, NULL);
}

static int
replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext /* not used */)
{
	int rc = 0;
	Object *RUVObj;
	RUV *local_ruv = NULL;
	Replica *replica = (Replica*)object_get_data (r);

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
	 *  Clean the changelog RUV's, and set the rids
	 */
	cl5CleanRUV(rid);
	delete_released_rid();

	if (rc != RUV_SUCCESS){
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanruv_task: task failed(%d)\n",rc);
		return LDAP_OPERATIONS_ERROR;
	}
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "cleanruv_task: finished successfully\n");
	return LDAP_SUCCESS;
}

static int
replica_execute_cleanall_ruv_task (Object *r, ReplicaId rid, char *returntext)
{
	PRThread *thread = NULL;
	Repl_Connection *conn;
	Replica *replica = (Replica*)object_get_data (r);
	Object *agmt_obj;
	Repl_Agmt *agmt;
	ConnResult crc;
	cleanruv_data *data = NULL;
	const Slapi_DN *dn = NULL;
	struct berval *payload = NULL;
	char *ridstr = NULL;
	int send_msgid = 0;
	int agmt_count = 0;
	int rc = 0;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: cleaning rid (%d)...\n",(int)rid);
	set_cleaned_rid(rid);
	/*
	 *  Create payload
	 */
	ridstr = slapi_ch_smprintf("%d:%s", rid, slapi_sdn_get_dn(replica_get_root(replica)));
	payload = create_ruv_payload(ridstr);
	if(payload == NULL){
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to create ext op payload, aborting task\n");
		goto done;
	}

	agmt_obj = agmtlist_get_first_agreement_for_replica (replica);
	while (agmt_obj)
	{
		agmt = (Repl_Agmt*)object_get_data (agmt_obj);
		dn = agmt_get_dn_byref(agmt);
		conn = (Repl_Connection *)agmt_get_connection(agmt);
		if(conn == NULL){
			/* no connection for this agreement, and move on */
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: the replica (%s), is "
				"missing the connection.  This replica will not be cleaned.\n", slapi_sdn_get_dn(dn));
			agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
			continue;
		}
		crc = conn_connect(conn);
		if (CONN_OPERATION_FAILED == crc ){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to connect "
				"to repl agreement connection (%s), error %d\n",slapi_sdn_get_dn(dn), ACQUIRE_TRANSIENT_ERROR);
		} else if (CONN_SSL_NOT_ENABLED == crc){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to acquire "
				"repl agmt connection (%s), errror %d\n",slapi_sdn_get_dn(dn), ACQUIRE_FATAL_ERROR);
		} else {
			conn_cancel_linger(conn);
			crc = conn_send_extended_operation(conn, REPL_CLEANRUV_OID, payload, NULL, &send_msgid);
			if (CONN_OPERATION_SUCCESS != crc){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to send "
					"cleanruv extended op to repl agmt (%s), error %d\n", slapi_sdn_get_dn(dn), crc);
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: successfully sent "
					"cleanruv extended op to (%s)\n",slapi_sdn_get_dn(dn));
				agmt_count++;
			}
			conn_start_linger(conn);
		}
		if(crc){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: replica (%s) has not "
				"been cleaned.  You will need to rerun the CLEANALLRUV task on this replica.\n", slapi_sdn_get_dn(dn));
			rc = crc;
		}
		agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
	}

done:

	if(payload)
		ber_bvfree(payload);

	slapi_ch_free_string(&ridstr);

	/*
	 *  Now run the cleanruv task
	 */
	replica_execute_cleanruv_task (r, rid, returntext);

	if(rc == 0 && agmt_count > 0){  /* success, but we need to check our replicas */
		/*
		 *  Launch the cleanruv monitoring thread.  Once all the replicas are cleaned it will release the rid
		 */
		data = (cleanruv_data*)slapi_ch_calloc(1, sizeof(cleanruv_data));
		if (data == NULL) {
			slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to allocate "
				"cleanruv_data.  Aborting task.\n");
			return -1;
		}
		data->repl_obj = r;
		data->rid = rid;

		thread = PR_CreateThread(PR_USER_THREAD, replica_cleanallruv_monitor_thread,
				(void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
				PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
		if (thread == NULL) {
			slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: unable to create cleanAllRUV "
				"monitoring thread.  Aborting task.\n");
		}
	} else if(rc == 0){ /* success */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Successfully Finished.\n");
	} else {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Task failed (%d)\n",rc);
	}

	return rc;
}

/*
 *  After all the cleanAllRUV extended ops have been sent, we need to monitoring those replicas'
 *  RUVs.  Once the rid is cleaned, then we need to release it, and push this "release"
 *  to the other replicas
 */
void
replica_cleanallruv_monitor_thread(void *arg)
{
	Object *agmt_obj;
	LDAP *ld;
	Repl_Connection *conn;
	Repl_Agmt *agmt;
	Replica *replica;;
	cleanruv_data *data = arg;
	LDAPMessage *result, *entry;
	BerElement   *ber;
	time_t start_time;
	struct berval **vals;
	char *rid_text;
	char *attrs[2];
	char *attr;
	int replicas_cleaned = 0;
	int found = 0;
	int crc;
	int rc = 0;
	int i;

	/*
	 *  Initialize our settings
	 */
	attrs[0] = "nsds50ruv";
	attrs[1] = NULL;
	rid_text = slapi_ch_smprintf("{replica %d ldap", data->rid);
	replica = (Replica*)object_get_data (data->repl_obj);
	start_time = current_time();

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Waiting for all the replicas to get cleaned...\n");

	while(!g_get_shutdown())
	{
		DS_Sleep(PR_SecondsToInterval(10));
		found = 0;
		agmt_obj = agmtlist_get_first_agreement_for_replica (replica);
		while (agmt_obj){
			agmt = (Repl_Agmt*)object_get_data (agmt_obj);
			if(!agmt_is_enabled(agmt)){
				agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
				continue;
			}
			/*
			 *  Get the replication connection
			 */
			conn = (Repl_Connection *)agmt_get_connection(agmt);
			crc = conn_connect(conn);
			if (CONN_OPERATION_FAILED == crc ){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: monitor thread failed to connect "
					"to repl agreement connection (%s), error %d\n","", ACQUIRE_TRANSIENT_ERROR);
				continue;
			} else if (CONN_SSL_NOT_ENABLED == crc){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: monitor thread failed to acquire "
					"repl agmt connection (%s), error %d\n","", ACQUIRE_FATAL_ERROR);
				continue;
			}
			/*
			 *  Get the LDAP connection handle from the conn
			 */
			ld = conn_get_ldap(conn);
			if(ld == NULL){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: monitor thread failed to get LDAP "
					"handle from the replication agmt (%s).  Moving on to the next agmt.\n",agmt_get_long_name(agmt));
				continue;
			}
			/*
			 *  Search this replica for its tombstone/ruv entry
			 */
			conn_cancel_linger(conn);
			conn_lock(conn);
			rc = ldap_search_ext_s(ld, slapi_sdn_get_dn(agmt_get_replarea(agmt)), LDAP_SCOPE_SUBTREE,
				"(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))",
				attrs, 0, NULL, NULL, NULL, 0, &result);
			if(rc != LDAP_SUCCESS){
				/*
				 *  Couldn't contact ldap server, what should we do?
				 *
				 *  Skip it and move on!  It's the admin's job to make sure all the replicas are up and running
				 */
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: monitor thread failed to contact "
					"agmt (%s), moving on to the next agmt.\n", agmt_get_long_name(agmt));
				conn_unlock(conn);
				conn_start_linger(conn);
				continue;
			}
			/*
			 *  There is only one entry.  Check its "nsds50ruv" for our cleaned rid
			 */
			entry = ldap_first_entry( ld, result );
			if ( entry != NULL ) {
				for ( attr = ldap_first_attribute( ld, entry, &ber ); attr != NULL; attr = ldap_next_attribute( ld, entry, ber ) ){
					/* make sure the attribute is nsds50ruv */
					if(strcasecmp(attr,"nsds50ruv") != 0){
						continue;
					}
					if ((vals = ldap_get_values_len( ld, entry, attr)) != NULL ) {
						for ( i = 0; vals[i] && vals[i]->bv_val; i++ ) {
							/* look for this replica */
							if(strstr(rid_text, vals[i]->bv_val)){
								/* rid has not been cleaned yet, start over */
								found = 1;
								break;
							}
						}
						ldap_value_free_len(vals);
					}
					ldap_memfree( attr );
				}
				if ( ber != NULL ) {
					ber_free( ber, 0 );
				}
			}
			ldap_msgfree( result );
			/*
			 *  Unlock the connection, and start the linger timer
			 */
			conn_unlock(conn);
			conn_start_linger(conn);

			if(found){
				/*
				 *  If we've been trying to clean these replicas for over an hour, just quit
				 */
				if( (current_time() - start_time) > 3600){
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: timed out checking if replicas have "
						"been cleaned.  The rid has not been released, you need to rerun the task.\n");
					goto done;
				}
				/*
				 *  a rid has not been cleaned yet, go back to sleep and check them again
				 */
				break;
			}

			agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
		}
		if(!found){
			/*
			 *  The replicas have been cleaned!  Next, release the rid
			 */
			replicas_cleaned = 1;
			break;
		}
	} /* while */

	/*
	 *  If the replicas are cleaned, release the rid
	 */
	if(replicas_cleaned){
		rc = replica_execute_release_ruv_task(data->repl_obj, data->rid);
		if(rc == 0){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Successfully Finished.  All active "
				"replicas have been cleaned.\n");
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Failed: Replica ID was not released (%d)  "
			"You will need to rerun the task.\n", rc);
		}
	} else {
		/*
		 *  Shutdown
		 */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: slapd shutting down, you will need to rerun the task.\n");
	}

done:
	slapi_ch_free((void **)&rid_text);
	slapi_ch_free((void **)&data);
}

/*
 *  This function releases the cleaned rid so that it can be reused.
 *  We send this operation to all the known active replicas.
 */
static int
replica_execute_release_ruv_task(Object *r, ReplicaId rid)
{
	Repl_Connection *conn;
	Replica *replica = (Replica*)object_get_data (r);
	Object *agmt_obj;
	Repl_Agmt *agmt;
	ConnResult crc;
	const Slapi_DN *dn = NULL;
	struct berval *payload = NULL;
	char *ridstr = NULL;
	int send_msgid = 0;
	int rc = 0;

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: releasing rid (%d)...\n", rid);

	/*
	 * Set the released rid, and trigger cl trimmming
	 */
	set_released_rid((int)rid);
	trigger_cl_trimming();
	/*
	 *  Create payload
	 */
	ridstr = slapi_ch_smprintf("%d:%s", rid, slapi_sdn_get_dn(replica_get_root(replica)));
	payload = create_ruv_payload(ridstr);
	if(payload == NULL){
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "releaseRUV_task: failed to create ext op payload, aborting op\n");
		rc = -1;
		goto done;
	}

	agmt_obj = agmtlist_get_first_agreement_for_replica (replica);
	while (agmt_obj)
	{
		agmt = (Repl_Agmt*)object_get_data (agmt_obj);
		dn = agmt_get_dn_byref(agmt);
		conn = (Repl_Connection *)agmt_get_connection(agmt);
		if(conn == NULL){
			/* no connection for this agreement, log error, and move on */
			agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
			continue;
		}
		crc = conn_connect(conn);
		if (CONN_OPERATION_FAILED == crc ){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "releaseRUV_task: failed to connect "
				"to repl agmt (%s), error %d\n",slapi_sdn_get_dn(dn), ACQUIRE_TRANSIENT_ERROR);
			rc = LDAP_OPERATIONS_ERROR;
		} else if (CONN_SSL_NOT_ENABLED == crc){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "releaseRUV_task: failed to acquire "
				"repl agmt (%s), error %d\n",slapi_sdn_get_dn(dn), ACQUIRE_FATAL_ERROR);
			rc = LDAP_OPERATIONS_ERROR;
		} else {
			conn_cancel_linger(conn);
			crc = conn_send_extended_operation(conn, REPL_RELEASERUV_OID, payload, NULL, &send_msgid);
			if (CONN_OPERATION_SUCCESS != crc){
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: failed to send "
					"releaseRUV extended op to repl agmt (%s), error %d\n", slapi_sdn_get_dn(dn), crc);
				rc = LDAP_OPERATIONS_ERROR;
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: successfully sent "
					"releaseRUV extended op to (%s)\n",slapi_sdn_get_dn(dn));
			}
			conn_start_linger(conn);
		}
		if(crc){
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: replica (%s) has not "
					"been released.  You will need to rerun the task\n",
					slapi_sdn_get_dn(dn));
			rc = crc;
		}
		agmt_obj = agmtlist_get_next_agreement_for_replica (replica, agmt_obj);
	}

done:
	/*
	 *  reset the released/clean rid
	 */
	if(rc == 0){
		set_released_rid(ALREADY_RELEASED);
		delete_cleaned_rid();
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Successfully released rid (%d)\n", rid);
	} else {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "cleanAllRUV_task: Failed to release rid (%d), error (%d)\n", rid, rc);
	}

	if(payload)
		ber_bvfree(payload);

	slapi_ch_free_string(&ridstr);

	return rc;
}

static struct berval *
create_ruv_payload(char *value){
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

int
is_cleaned_rid(ReplicaId rid)
{
	if(rid == cleaned_rid){
		return 1;
	} else {
		return 0;
	}
}

void
set_cleaned_rid( ReplicaId rid )
{
	cleaned_rid = rid;
}

void
delete_cleaned_rid()
{
	cleaned_rid = 0;
}

int
get_released_rid()
{
	return released_rid;
}

int
is_released_rid(int rid)
{
	if(rid == released_rid){
		return 1;
	} else {
		return 0;
	}
}

int
is_already_released_rid()
{
	if(released_rid == ALREADY_RELEASED){
		return 1;
	} else {
		return 0;
	}
}

void
set_released_rid( int rid )
{
	released_rid = rid;
}

void
delete_released_rid()
{
	released_rid = 0;
}
