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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * repl_legacy_consumer.c - support for legacy replication (consumer-side)
 *
 * Support for legacy replication involves correctly dealing with
 * the addition and removal of attribute types "copiedFrom" and
 * "copyingFrom". The behavior is:
 * 1) If a copiedFrom appears in an entry, and that entry is the root
 *    of a replicated area, then put the backend into "refer on update"
 *    mode and install a referral corresponding to the URL contained
 *    in the copiedFrom attribute. This referral overrides the mode
 *    of the replica, e.g. if it was previously an updateable replica,
 *    it now becomes read-only except for the updatedn.
 * 2) If a copiedFrom disappears from an entry, or the entry containing
 *    the copiedFrom is removed, restore the backend to the state
 *    determined by the DS 5.0 replica configuration.
 * 3) If a "copyingFrom" referral appears in an entry, and that entry
 *    is the root of a replicated area, then put the backend into
 *    "refer all operations" mode and install a referral corresponding
 *    to the URL contained in the copyingFrom attribute. This referral
 *    overrides the mode of the replica, e.g if it was previously an
 *    updateable replica, it now becomes read-only and refers all
 *    operations except for the updatedn.
 * 4) If a copyingFrom disappears from an entry, or the entry containing
 *    the copyingFrom is removed, restore the backend to the state
 *    determined by the DS 5.0 replica configuration.
 */


#include "repl5.h"
#include "repl.h"

/* Forward Declarations */
static int legacy_consumer_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int legacy_consumer_config_modify (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);
static int legacy_consumer_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

static int legacy_consumer_extract_config(Slapi_Entry* entry, char *returntext);
static int legacy_consumer_read_config ();
static void legacy_consumer_encode_pw (Slapi_Entry *e);
static void set_legacy_purl (Slapi_PBlock *pb, const char *purl);
static int get_legacy_referral (Slapi_Entry *e, char **referral, char **state);

#define LEGACY_CONSUMER_CONFIG_DN "cn=legacy consumer," REPL_CONFIG_TOP
#define LEGACY_CONSUMER_FILTER "(objectclass=*)"

/* Configuration parameters local to this module */
static Slapi_DN *legacy_consumer_replicationdn = NULL;
static char *legacy_consumer_replicationpw = NULL;
/* Lock which protects the above config parameters */
PRRWLock *legacy_consumer_config_lock = NULL;

static PRBool
target_is_a_replica_root(Slapi_PBlock *pb, const Slapi_DN **root)
{
	char *dn;
	Slapi_DN *sdn;
	PRBool return_value;
	Object *repl_obj;

	slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn);
	sdn = slapi_sdn_new_dn_byref(dn);
	repl_obj = replica_get_replica_from_dn(sdn);
	if (NULL != repl_obj)
	{
		Replica *r = object_get_data(repl_obj);
		*root = replica_get_root(r);
		return_value = PR_TRUE;
		object_release(repl_obj);
	}
	else
	{
		*root = NULL;
		return_value = PR_FALSE;
	}
	slapi_sdn_free(&sdn);
	return return_value;
}



static int
parse_cfstring(const char *cfstring, char **referral, char **generation, char **lastreplayed)
{
	int return_value = -1;
    char *ref, *gen, *lastplayed;

	if (cfstring != NULL)
	{
		char *tmp;
		char *cfcopy = slapi_ch_strdup(cfstring);
		ref = cfcopy;
		tmp = strchr(cfcopy, ' ');
		if (NULL != tmp)
		{
			*tmp++ = '\0';
			while ('\0' != *tmp && ' ' == *tmp) tmp++;
			gen = tmp;
			tmp = strchr(gen, ' ');
			if (NULL != tmp)
			{
				*tmp++ = '\0';
				while ('\0' != *tmp && ' ' == *tmp) tmp++;
				lastplayed = tmp;
				return_value = 0;
			}
		}
		
        if (return_value == 0)
		{
            if (referral)
			    *referral = slapi_ch_strdup(ref);
            if (generation)
			    *generation = slapi_ch_strdup(gen);
            if (lastreplayed)
			    *lastreplayed = slapi_ch_strdup(lastplayed);
		}
		slapi_ch_free((void **)&cfcopy);
	}
	return return_value;
}



/*
 * This is called from the consumer post-op plugin point.
 * It's called if:
 * 1) The operation is an add or modify operation, and a
 *    copiedfrom/copyingfrom was found in the entry/mods, or
 * 2) the operation is a delete operation, or
 * 3) the operation is a moddn operation.
 */

void
process_legacy_cf(Slapi_PBlock *pb)
{
	consumer_operation_extension *opext;
	Slapi_Operation *op;
    char *referral_array[2] = {0};
    char *referral;
	char *state;
	int rc;
    const Slapi_DN *replica_root_sdn = NULL;
    Slapi_Entry *e;

	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	opext = (consumer_operation_extension*) repl_con_get_ext (REPL_CON_EXT_OP, op);
    
    if (opext->has_cf)
    {
        PR_ASSERT (operation_get_type (op) == SLAPI_OPERATION_ADD || 
                   operation_get_type (op) == SLAPI_OPERATION_MODIFY);
	    
        if ((PR_FALSE == target_is_a_replica_root(pb, &replica_root_sdn)) ||
			(NULL == replica_root_sdn)){
			return;
		}

        slapi_pblock_get (pb, SLAPI_ENTRY_POST_OP, &e);
        PR_ASSERT (e);

		if (NULL == e)
			return;
		
		rc = get_legacy_referral (e, &referral, &state);
        if (rc == 0)
        {
            referral_array[0] = referral;
            referral_array[1] = NULL;
			repl_set_mtn_state_and_referrals(replica_root_sdn, state, NULL, NULL,
											 referral_array);
            /* set partial url in the replica_object */
            set_legacy_purl (pb, referral);

            slapi_ch_free((void **)&referral);
	    }
			    
    }
}

void legacy_consumer_be_state_change (void *handle, char *be_name,
	                                  int old_be_state, int new_be_state)
{
    Object *r_obj;
    Replica *r;

    /* we only interested when a backend is coming online */
    if (new_be_state == SLAPI_BE_STATE_ON)
    {
        r_obj = replica_get_for_backend (be_name);
        if (r_obj)
        {
            r = (Replica*)object_get_data (r_obj);
            PR_ASSERT (r);

            if (replica_is_legacy_consumer (r))
                legacy_consumer_init_referrals (r);

            object_release (r_obj);
        }
    }    
}
	
	   
static int
dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
	*returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

int
legacy_consumer_config_init()
{
    /* The FE DSE *must* be initialised before we get here */
	int rc;

	if ((legacy_consumer_config_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "legacy_consumer_config_lock")) == NULL) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"Failed to create legacy_consumer config read-write lock\n");
		exit(1);
	}

	rc = legacy_consumer_read_config ();
	if (rc != LDAP_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			"Failed to initialize legacy replication configuration\n");
		return 1;
	}
    
	slapi_config_register_callback(SLAPI_OPERATION_ADD,DSE_FLAG_PREOP,LEGACY_CONSUMER_CONFIG_DN,LDAP_SCOPE_SUBTREE,LEGACY_CONSUMER_FILTER,legacy_consumer_config_add,NULL); 
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY,DSE_FLAG_PREOP,LEGACY_CONSUMER_CONFIG_DN,LDAP_SCOPE_SUBTREE,LEGACY_CONSUMER_FILTER,legacy_consumer_config_modify,NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODRDN,DSE_FLAG_PREOP,LEGACY_CONSUMER_CONFIG_DN,LDAP_SCOPE_SUBTREE,LEGACY_CONSUMER_FILTER,dont_allow_that,NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE,DSE_FLAG_PREOP,LEGACY_CONSUMER_CONFIG_DN,LDAP_SCOPE_SUBTREE,LEGACY_CONSUMER_FILTER,legacy_consumer_config_delete,NULL); 

    return 0;
}

static int 
legacy_consumer_config_add (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    int rc;	

	rc = legacy_consumer_extract_config(e, returntext);
	if (rc != LDAP_SUCCESS)
	{
		*returncode = rc;
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Failed to configure legacy replication\n");
		return SLAPI_DSE_CALLBACK_ERROR;
	}
    /* make sure that the password is encoded */
    legacy_consumer_encode_pw(e);

    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "legacy_consumer_config_add: "
                    "successfully configured legacy consumer credentials\n");

	return SLAPI_DSE_CALLBACK_OK;
}

#define config_copy_strval( s ) s ? slapi_ch_strdup (s) : NULL;

static int 
legacy_consumer_config_modify (Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
    int rc= 0;
   	LDAPMod **mods;
	int not_allowed = 0;
	int i;

	if (returntext)
	{
		returntext[0] = '\0';
	}
	*returncode = LDAP_SUCCESS;


	slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
	PR_RWLock_Wlock (legacy_consumer_config_lock);
		
	for (i = 0; (mods[i] && (!not_allowed)); i++)
	{
		if (mods[i]->mod_op & LDAP_MOD_DELETE)
		{
			/* We don't support deleting an attribute from cn=config */
		}
		else
		{
			int j;
			for (j = 0; ((mods[i]->mod_values[j]) && (LDAP_SUCCESS == rc)); j++)
			{
				char *config_attr, *config_attr_value;
				int mod_type;
				config_attr = (char *) mods[i]->mod_type; 
				config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
				/* replace existing value */
				mod_type = mods[i]->mod_op & ~LDAP_MOD_BVALUES;
				if ( strcasecmp (config_attr, CONFIG_LEGACY_REPLICATIONDN_ATTRIBUTE ) == 0 )
				{
                    if (legacy_consumer_replicationdn)
                            slapi_sdn_free (&legacy_consumer_replicationdn);

					if (mod_type == LDAP_MOD_REPLACE)
					{
                        if (config_attr_value)
						    legacy_consumer_replicationdn = slapi_sdn_new_dn_byval (config_attr_value);
					}
					else if (mod_type == LDAP_MOD_DELETE)
					{
						legacy_consumer_replicationdn = NULL;
					}
					else if (mod_type == LDAP_MOD_ADD)
					{
						if (legacy_consumer_replicationdn != NULL)
						{
							not_allowed = 1;
							slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
									"Multiple replicationdns not permitted." );
						}
						else
						{
                            if (config_attr_value)
							    legacy_consumer_replicationdn = slapi_sdn_new_dn_byval (config_attr_value);
						}
					}
				}
                else if ( strcasecmp ( config_attr, CONFIG_LEGACY_REPLICATIONPW_ATTRIBUTE ) == 0 )
                {
                    if (mod_type == LDAP_MOD_REPLACE)
                    {
                        legacy_consumer_replicationpw = config_copy_strval(config_attr_value);
                    }
                    else if (mod_type == LDAP_MOD_DELETE)
                    {
                        legacy_consumer_replicationpw = NULL;
                    }
                    else if (mod_type == LDAP_MOD_ADD)
                    {
                        if (legacy_consumer_replicationpw != NULL)
                        {
                            not_allowed = 1;
                            slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name,
								"Multiple replicationpws not permitted." );
                        }
                        else
                        {
                            legacy_consumer_replicationpw = config_copy_strval(config_attr_value);
                        }
                    }
                }
			}
		}
	}

	PR_RWLock_Unlock (legacy_consumer_config_lock);


	if (not_allowed)
	{
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Failed to modify legacy replication configuration\n" );
		*returncode= LDAP_CONSTRAINT_VIOLATION;
		return SLAPI_DSE_CALLBACK_ERROR;
	}

    /* make sure that the password is encoded */
    legacy_consumer_encode_pw (e);

	return SLAPI_DSE_CALLBACK_OK;
}

static int 
legacy_consumer_config_delete (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
	
	PR_RWLock_Wlock (legacy_consumer_config_lock);
    if (legacy_consumer_replicationdn)
        slapi_sdn_free (&legacy_consumer_replicationdn);
    if (legacy_consumer_replicationpw)
        slapi_ch_free ((void**)&legacy_consumer_replicationpw);

	legacy_consumer_replicationdn = NULL;
    legacy_consumer_replicationpw = NULL;
	PR_RWLock_Unlock (legacy_consumer_config_lock);

	*returncode = LDAP_SUCCESS;
	return SLAPI_DSE_CALLBACK_OK;
}

/*
 * Given the changelog configuration entry, extract the configuration directives.
 */
static int
legacy_consumer_extract_config(Slapi_Entry* entry, char *returntext)
{
	int rc = LDAP_SUCCESS; /* OK */
	char *arg;

  	PR_RWLock_Wlock (legacy_consumer_config_lock);

    arg= slapi_entry_attr_get_charptr(entry,CONFIG_LEGACY_REPLICATIONDN_ATTRIBUTE);
    if (arg)
	    legacy_consumer_replicationdn = slapi_sdn_new_dn_passin (arg);

    arg= slapi_entry_attr_get_charptr(entry,CONFIG_LEGACY_REPLICATIONPW_ATTRIBUTE);
    legacy_consumer_replicationpw = arg;

	PR_RWLock_Unlock (legacy_consumer_config_lock);

	return rc;
}




static int 
legacy_consumer_read_config ()
{
    int rc = LDAP_SUCCESS;
    int scope= LDAP_SCOPE_BASE;
    Slapi_PBlock *pb;
    
    pb = slapi_pblock_new ();
    slapi_search_internal_set_pb (pb, LEGACY_CONSUMER_CONFIG_DN, scope,
		"(objectclass=*)", NULL /*attrs*/, 0 /* attrs only */,
		NULL /* controls */, NULL /* uniqueid */,
		repl_get_plugin_identity(PLUGIN_LEGACY_REPLICATION), 0 /* actions */);
    slapi_search_internal_pb (pb);
    slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    if ( LDAP_SUCCESS == rc )
	{
        Slapi_Entry **entries = NULL;
        slapi_pblock_get( pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries );
        if ( NULL != entries && NULL != entries[0])
		{
        	/* Extract the config info from the changelog entry */
			rc = legacy_consumer_extract_config(entries[0], NULL);
        }
    }
    else
	{
        rc = LDAP_SUCCESS;
	}
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return rc;
}


int
legacy_consumer_is_replicationdn(char *dn)
{
	int return_value = 0; /* Assume not */

	if (NULL != dn && '\0' != dn[0])
	{
		if (NULL != legacy_consumer_replicationdn) 
        {
            Slapi_DN *sdn = slapi_sdn_new_dn_byref (dn);

			if (slapi_sdn_compare (legacy_consumer_replicationdn, sdn) == 0) {
				return_value = 1;
			}

            slapi_sdn_free (&sdn);
		}
	}
    return return_value;
}


int
legacy_consumer_is_replicationpw(struct berval *pwval)
{
	int return_value = 0; /* Assume not */

	if (NULL != pwval && NULL != pwval->bv_val)
	{
		if (NULL != legacy_consumer_replicationpw &&
			'\0' != legacy_consumer_replicationpw[0]) {
			struct berval *pwvals[2];
			struct berval config_pw;
			
			config_pw.bv_val = legacy_consumer_replicationpw;
			config_pw.bv_len = strlen(legacy_consumer_replicationpw);
			pwvals[0] = &config_pw;
			pwvals[1] = NULL;

			return_value = slapi_pw_find(pwvals, pwval) == 0;
		}
	}
    return return_value;
}

static void
legacy_consumer_encode_pw (Slapi_Entry *e)
{
	char *updatepw = slapi_entry_attr_get_charptr(e, 
		CONFIG_LEGACY_REPLICATIONPW_ATTRIBUTE);
	int is_encoded;
	char *encoded_value = NULL;

	if (updatepw != NULL)
	{
		is_encoded = slapi_is_encoded (updatepw);

		if (!is_encoded)
		{
			encoded_value =	slapi_encode (updatepw, "SHA");
		}

		if (encoded_value)
		{
			slapi_entry_attr_set_charptr(e,
				CONFIG_LEGACY_REPLICATIONPW_ATTRIBUTE, encoded_value);
		}
	}
}

static void
set_legacy_purl (Slapi_PBlock *pb, const char *purl)
{
    Object *r_obj;
    Replica *r;

    r_obj = replica_get_replica_for_op (pb);
    PR_ASSERT (r_obj);
    r = (Replica*)object_get_data (r_obj);
    PR_ASSERT (r && replica_is_legacy_consumer(r));

    replica_set_legacy_purl (r, purl);

    object_release (r_obj);
}

/* this function get referrals from an entry.
   Returns 0 if successful
           1 if no referrals are present
          -1 in case of error
 */
static int 
get_legacy_referral (Slapi_Entry *e, char **referral, char **state)
{
    char* pat = "ldap://%s";
    const char *val = NULL;
    char *hostport;
    int rc = 1;
    Slapi_Attr *attr;
    const Slapi_Value *sval;

    PR_ASSERT (e && referral && state);
    
    /* Find any copiedFrom/copyingFrom attributes -
       copyingFrom has priority */
	if (slapi_entry_attr_find(e, type_copyingFrom, &attr) == 0)
	{
		slapi_attr_first_value(attr, (Slapi_Value **)&sval);	
        val = slapi_value_get_string(sval);
        *state = STATE_REFERRAL;
	}
	else if (slapi_entry_attr_find(e, type_copiedFrom, &attr) == 0)
	{
		slapi_attr_first_value(attr, (Slapi_Value **)&sval);	
        val = slapi_value_get_string(sval);
        *state = STATE_UPDATE_REFERRAL;        
	}        

    if (val)
    {
        rc = parse_cfstring(val, &hostport, NULL, NULL);
        if (rc != 0)
        {
            const char *target_dn = slapi_entry_get_dn_const(e);
		    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Warning: a copiedFrom "
			"or copyingFrom attribute was added to or removed from an "
			"entry that is not the root of a replicated area. It is possible "
			"that a legacy replication supplier is incorrectly configured "
			"to supply updates to the subtree rooted at %s\n",
			target_dn == NULL ? "null" : target_dn);
        }
        else
        {
            *referral = slapi_ch_smprintf (pat, hostport);

            slapi_ch_free ((void**)&hostport);
        }
    }
    else
    {
        rc = 1; /* no copiedFrom or copyingFrom int the entry */
    }
  
    return rc;
}

/* this function is called during server startup or when replica's data
   is reloaded. It sets up referrals in the mapping tree based on the
   copiedFrom and copyingFrom attributes. It also sets up partial url in
   the replica object used to update RUV.
   Returns 0 if successful and -1 otherwise

 */
int
legacy_consumer_init_referrals (Replica *r)
{
    Slapi_PBlock *pb;
    const Slapi_DN *root_sdn; 
	const char *root_dn;
    char *attrs[] = {"copiedFrom", "copyingFrom"};
    int rc;
    Slapi_Entry **entries = NULL;
    char *referral = NULL;
    char *referral_array[2];
    char *state = NULL;

    PR_ASSERT (r);

    pb = slapi_pblock_new ();
    PR_ASSERT (pb);

    root_sdn = replica_get_root(r);
    PR_ASSERT (root_sdn);

    root_dn = slapi_sdn_get_ndn(root_sdn);
    PR_ASSERT (root_dn);

    slapi_search_internal_set_pb (pb, root_dn, LDAP_SCOPE_BASE, "objectclass=*",attrs, 
                                  0 /* attrsonly */, NULL /* controls */, 
                                  NULL /* uniqueid */, 
                                  repl_get_plugin_identity (PLUGIN_LEGACY_REPLICATION), 
                                  0 /* flags */);
								  
    slapi_search_internal_pb (pb);

    slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS)
    {
        if (rc == LDAP_REFERRAL)
        {
            /* We are in referral mode, probably because ORC failed */
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "legacy_consumer_init_referrals "
                            "data for replica %s is in referral mode due to failed "
                            "initialization. Replica need to be reinitialized\n",
                            root_dn);
            rc = 0;
        }
        else
        {
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "legacy_consumer_init_referrals "
                            "failed to obtain root entry for replica %s; LDAP error - %d\n",
                            root_dn, rc);
            rc = -1;
        }

        goto done;
    }

    slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    PR_ASSERT (entries && entries[0]);

    rc = get_legacy_referral (entries[0], &referral, &state);
    if (rc == 0)
    {         
        referral_array[0] = referral;
        referral_array[1] = NULL;
        repl_set_mtn_state_and_referrals(root_sdn, state, NULL, NULL, referral_array);

        /* set purtial url in the replica_object */
        replica_set_legacy_purl (r, referral);

        slapi_ch_free((void **)&referral);
	 }
     else if (rc == 1) /* no referrals - treat as success */
     {
        rc = 0;
     }
			       
    slapi_free_search_results_internal (pb);

done:
    
    slapi_pblock_destroy (pb);
    return rc;
}

