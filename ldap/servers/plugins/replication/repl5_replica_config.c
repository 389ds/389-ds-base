/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5_replica_config.c - replica configuration over ldap */
#include <ctype.h>	/* for isdigit() */
#include "repl.h"   /* ONREPL - this is bad */
#include "repl5.h"
#include "cl5_api.h"

#define CONFIG_BASE		    "cn=mapping tree,cn=config"
#define CONFIG_FILTER	    "(objectclass=nsDS5Replica)"
#define TASK_ATTR           "nsds5Task"
#define CL2LDIF_TASK        "CL2LDIF"
#define CLEANRUV            "CLEANRUV"
#define CLEANRUVLEN         8

int slapi_log_urp = SLAPI_LOG_REPL;

/* Forward Declartions */
static int replica_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

static int replica_config_change_type_and_id (Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods);
static int replica_config_change_updatedn (Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_flags (Replica *r, const char *new_flags,  char *returntext, int apply_mods);
static int replica_execute_task (Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_execute_cl2ldif_task (Object *r, char *returntext);
static int replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext);
												
static multimaster_mtnode_extension * _replica_config_get_mtnode_ext (const Slapi_Entry *e);

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
}

static int 
replica_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, 
					   int *returncode, char *errorbuf, void *arg)
{
	Replica *r = NULL;
    multimaster_mtnode_extension *mtnode_ext;	
    char *replica_root = (char*)slapi_entry_attr_get_charptr (e, attr_replicaRoot);
	char buf [BUFSIZ];
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
        sprintf (errortext, "replica already configured for %s", replica_root);
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
	char buf [BUFSIZ];
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
        sprintf (errortext, "replica does not exist for %s", replica_root);
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
                sprintf (errortext, "modification of %s attribute is not allowed", 
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
                    sprintf (errortext, "deletion of %s attribute is not allowed", config_attr);                         
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
                    sprintf (errortext, "modification of attribute %s is not allowed in replica entry", config_attr);
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
        /* remove object from the hash */
        r = (Replica*)object_get_data (mtnode_ext->replica);
        PR_ASSERT (r);
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
    char val [64];

	/* add attribute that contains number of entries in the changelog for this replica */
	
    PR_Lock (s_configLock);
    
	/* if we have no changelog - we have no changes */
	if (cl5GetState () == CL5_STATE_OPEN)
	{    
		mtnode_ext = _replica_config_get_mtnode_ext (e);
		PR_ASSERT (mtnode_ext);
    
		if (mtnode_ext->replica)
		{
            object_acquire (mtnode_ext->replica);
			changeCount = cl5GetOperationCount (mtnode_ext->replica);        
			object_release (mtnode_ext->replica);
		}
    }

    sprintf (val, "%d", changeCount);
    slapi_entry_add_string (e, type_replicaChangeCount, val);

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
            sprintf (returntext, "invalid replica type %d", type);
            return LDAP_OPERATIONS_ERROR;
        }
    }

	/* disallow changing type to itself just to permit a replica ID change */
	if (oldtype == type)
	{
            sprintf (returntext, "replica type is already %d - not changing", type);
            return LDAP_OPERATIONS_ERROR;
	}

	if (type == REPLICA_TYPE_READONLY)
	{
		rid = READ_ONLY_REPLICA_ID; /* default rid for read only */
	}
	else if (!new_id)
	{
		sprintf(returntext, "a replica ID is required when changing replica type to read-write");
		return LDAP_UNWILLING_TO_PERFORM;
	}
	else
	{
		int temprid = atoi (new_id);
		if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID)
		{
			sprintf(returntext,
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
            sprintf (returntext, "replica ID is already %d - not changing", rid);
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
	else if (strncasecmp (task_name, CLEANRUV, CLEANRUVLEN) == 0)
	{
		int temprid = atoi(&(task_name[CLEANRUVLEN]));
		if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID){
			sprintf(returntext, "Invalid replica id for task - %s", task_name);
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"replica_execute_task: %s\n", returntext);
			return LDAP_OPERATIONS_ERROR;
		}
		if (apply_mods)
		{
			return replica_execute_cleanruv_task (r, (ReplicaId)temprid, returntext);
		}
		else
			return LDAP_SUCCESS;
	}
	else
	{
        sprintf (returntext, "unsupported replica task - %s", task_name);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
                        "replica_execute_task: %s\n", returntext);        
        return LDAP_OPERATIONS_ERROR;
    }
    
}

static int replica_execute_cl2ldif_task (Object *r, char *returntext)
{
    int rc;
    Object *rlist [2];
    Replica *replica;
    char fName [MAXPATHLEN];
    char *clDir;

    if (cl5GetState () != CL5_STATE_OPEN)
    {
        sprintf (returntext, "changelog is not open");
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
                        "replica_execute_cl2ldif_task: %s\n", returntext);        
        return LDAP_OPERATIONS_ERROR;
    }

    rlist[0] = r;
    rlist[1] = NULL;

    /* file is stored in the changelog directory and is named
       <replica name>.ldif */
    clDir = cl5GetDir ();
    PR_ASSERT (clDir);

    replica = (Replica*)object_get_data (r);
    PR_ASSERT (replica);

    sprintf (fName, "%s/%s.ldif", clDir, replica_get_name (replica));
    slapi_ch_free ((void**)&clDir);

    rc = cl5ExportLDIF (fName, rlist);
    if (rc != CL5_SUCCESS)
    {
        sprintf (returntext, "failed to export changelog data to file %s; "
                 "changelog error - %d", fName, rc);
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
                        "replica_execute_cl2ldif_task: %s\n", returntext);        
        return LDAP_OPERATIONS_ERROR;    
    }

    return LDAP_SUCCESS;
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

static int
replica_execute_cleanruv_task (Object *r, ReplicaId rid, char *returntext)
{
	int rc = 0;
	Object *RUVObj;
	RUV *local_ruv = NULL;
    Replica *replica = (Replica*)object_get_data (r);

    PR_ASSERT (replica);

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
	
	if (rc != RUV_SUCCESS){
		return LDAP_OPERATIONS_ERROR;
	}
	return LDAP_SUCCESS;
}


