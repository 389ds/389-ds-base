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

#include "cb.h"
#include "plstr.h"

/*
** 1 set/get function for each parameter of a backend instance
** NOTE: Some parameters won't be taken into account until the server has restarted
** In such cases, the internal conf is not updated but the new value is stored in the 
** dse.ldif file.
**/

/* Get functions */

static void *cb_instance_hosturl_get(void *arg);
static void *cb_instance_starttls_get(void *arg);
static void *cb_instance_binduser_get(void *arg);
static void *cb_instance_bindmech_get(void *arg);
static void *cb_instance_userpassword_get(void *arg);
static void *cb_instance_maxbconn_get(void *arg);
static void *cb_instance_maxconn_get(void *arg);
static void *cb_instance_abandonto_get(void *arg);
static void *cb_instance_maxbconc_get(void *arg);
static void *cb_instance_maxconc_get(void *arg);
static void *cb_instance_imperson_get(void *arg);
static void *cb_instance_connlife_get(void *arg);
static void *cb_instance_bindto_get(void *arg);
static void *cb_instance_opto_get(void *arg);
static void *cb_instance_ref_get(void *arg);
static void *cb_instance_acl_get(void *arg);
static void *cb_instance_bindretry_get(void *arg);
static void *cb_instance_sizelimit_get(void *arg);
static void *cb_instance_timelimit_get(void *arg);
static void *cb_instance_hoplimit_get(void *arg);
static void *cb_instance_max_idle_get(void *arg);
static void *cb_instance_max_test_get(void *arg);


/* Set functions */

static int cb_instance_hosturl_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_starttls_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_binduser_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_bindmech_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_userpassword_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_maxbconn_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_maxconn_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_abandonto_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_maxbconc_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_maxconc_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_imperson_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_connlife_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_bindto_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_opto_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_ref_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_acl_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_bindretry_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_sizelimit_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_timelimit_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_hoplimit_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_max_idle_set(void *arg, void *value, char *errorbuf, int phase, int apply);
static int cb_instance_max_test_set(void *arg, void *value, char *errorbuf, int phase, int apply);

/* Default hardwired values */

cb_instance_config_info cb_the_instance_config[] = {
{CB_CONFIG_HOSTURL,CB_CONFIG_TYPE_STRING,"",&cb_instance_hosturl_get,&cb_instance_hosturl_set,CB_ALWAYS_SHOW},
{CB_CONFIG_BINDUSER, CB_CONFIG_TYPE_STRING, "", &cb_instance_binduser_get, &cb_instance_binduser_set,CB_ALWAYS_SHOW},
{CB_CONFIG_USERPASSWORD,CB_CONFIG_TYPE_STRING,"",&cb_instance_userpassword_get,&cb_instance_userpassword_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAXBINDCONNECTIONS,CB_CONFIG_TYPE_INT,CB_DEF_BIND_MAXCONNECTIONS,&cb_instance_maxbconn_get, &cb_instance_maxbconn_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAXCONNECTIONS,CB_CONFIG_TYPE_INT,CB_DEF_MAXCONNECTIONS,&cb_instance_maxconn_get, &cb_instance_maxconn_set,CB_ALWAYS_SHOW},
{CB_CONFIG_ABANDONTIMEOUT,CB_CONFIG_TYPE_INT,CB_DEF_ABANDON_TIMEOUT,&cb_instance_abandonto_get, &cb_instance_abandonto_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAXBINDCONCURRENCY,CB_CONFIG_TYPE_INT,CB_DEF_BIND_MAXCONCURRENCY,&cb_instance_maxbconc_get, &cb_instance_maxbconc_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAXCONCURRENCY,CB_CONFIG_TYPE_INT,CB_DEF_MAXCONCURRENCY,&cb_instance_maxconc_get, &cb_instance_maxconc_set,CB_ALWAYS_SHOW},
{CB_CONFIG_IMPERSONATION,CB_CONFIG_TYPE_ONOFF,CB_DEF_IMPERSONATION,&cb_instance_imperson_get, &cb_instance_imperson_set,CB_ALWAYS_SHOW},
{CB_CONFIG_CONNLIFETIME,CB_CONFIG_TYPE_INT,CB_DEF_CONNLIFETIME,&cb_instance_connlife_get, &cb_instance_connlife_set,CB_ALWAYS_SHOW},
{CB_CONFIG_BINDTIMEOUT,CB_CONFIG_TYPE_INT,CB_DEF_BINDTIMEOUT,&cb_instance_bindto_get, &cb_instance_bindto_set,CB_ALWAYS_SHOW},
{CB_CONFIG_TIMEOUT,CB_CONFIG_TYPE_INT,"0",&cb_instance_opto_get, &cb_instance_opto_set,0},
{CB_CONFIG_REFERRAL,CB_CONFIG_TYPE_ONOFF,CB_DEF_SEARCHREFERRAL,&cb_instance_ref_get, &cb_instance_ref_set,CB_ALWAYS_SHOW},
{CB_CONFIG_LOCALACL,CB_CONFIG_TYPE_ONOFF,CB_DEF_LOCALACL,&cb_instance_acl_get, &cb_instance_acl_set,CB_ALWAYS_SHOW},
{CB_CONFIG_BINDRETRY,CB_CONFIG_TYPE_INT,CB_DEF_BINDRETRY,&cb_instance_bindretry_get, &cb_instance_bindretry_set,CB_ALWAYS_SHOW},
{CB_CONFIG_SIZELIMIT,CB_CONFIG_TYPE_INT,CB_DEF_SIZELIMIT,&cb_instance_sizelimit_get, &cb_instance_sizelimit_set,CB_ALWAYS_SHOW},
{CB_CONFIG_TIMELIMIT,CB_CONFIG_TYPE_INT,CB_DEF_TIMELIMIT,&cb_instance_timelimit_get, &cb_instance_timelimit_set,CB_ALWAYS_SHOW},
{CB_CONFIG_HOPLIMIT,CB_CONFIG_TYPE_INT,CB_DEF_HOPLIMIT,&cb_instance_hoplimit_get, &cb_instance_hoplimit_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAX_IDLE_TIME,CB_CONFIG_TYPE_INT,CB_DEF_MAX_IDLE_TIME,&cb_instance_max_idle_get, &cb_instance_max_idle_set,CB_ALWAYS_SHOW},
{CB_CONFIG_MAX_TEST_TIME,CB_CONFIG_TYPE_INT,CB_DEF_MAX_TEST_TIME,&cb_instance_max_test_get, &cb_instance_max_test_set,CB_ALWAYS_SHOW},
{CB_CONFIG_STARTTLS,CB_CONFIG_TYPE_ONOFF,CB_DEF_STARTTLS,&cb_instance_starttls_get, &cb_instance_starttls_set,CB_ALWAYS_SHOW},
{CB_CONFIG_BINDMECH,CB_CONFIG_TYPE_STRING,CB_DEF_BINDMECH,&cb_instance_bindmech_get, &cb_instance_bindmech_set,CB_ALWAYS_SHOW},
{NULL, 0, NULL, NULL, NULL, 0}
};

/* Others forward declarations */

int cb_instance_delete_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg);
int cb_instance_search_config_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg);
int cb_instance_add_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg);
static int
cb_instance_config_set(void *arg, char *attr_name, cb_instance_config_info *config_array,
struct berval *bval, char *err_buf, int phase, int apply_mod);


int
cb_dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg)
{
    *returncode=LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

static char *cb_skeleton_entries[] =
{

    "dn:cn=monitor, cn=%s, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:monitor\n",

        ""
};

/*
** Initialize a backend instance with a default configuration
*/

static void cb_instance_config_set_default(cb_backend_instance *inst)
{
        cb_instance_config_info *config;
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

        for (config = cb_the_instance_config; config->config_name != NULL; config++) {
                cb_instance_config_set((void *)inst, 
		config->config_name, cb_the_instance_config, NULL /* use default */, err_buf, 
		CB_CONFIG_PHASE_INITIALIZATION, 1 /* apply */);
        }

	/* Set backend instance flags */
	if (inst->inst_be) 
		slapi_be_set_flag(inst->inst_be,SLAPI_BE_FLAG_REMOTE_DATA);
}

/*
** Allocate a new chaining backend instance. Internal structure
*/

static cb_backend_instance * cb_instance_alloc(cb_backend * cb, char * name, char * basedn) {

	int i;

	cb_backend_instance * inst =
		(cb_backend_instance *)slapi_ch_calloc(1, sizeof(cb_backend_instance));

	/* associated_be_is_disabled defaults to 0 - this may be a problem if the associated
	   be is disabled at instance creation time
	 */
        inst->inst_name = slapi_ch_strdup(name);
        inst->monitor.mutex = slapi_new_mutex();
	inst->monitor_availability.cpt_lock = slapi_new_mutex();
	inst->monitor_availability.lock_timeLimit = slapi_new_mutex();
        inst->pool= (cb_conn_pool *) slapi_ch_calloc(1,sizeof(cb_conn_pool));
        inst->pool->conn.conn_list_mutex = slapi_new_mutex();
        inst->pool->conn.conn_list_cv = slapi_new_condvar(inst->pool->conn.conn_list_mutex);
        inst->pool->bindit=1;

        inst->bind_pool= (cb_conn_pool *) slapi_ch_calloc(1,sizeof(cb_conn_pool));
        inst->bind_pool->conn.conn_list_mutex = slapi_new_mutex();
        inst->bind_pool->conn.conn_list_cv = slapi_new_condvar(inst->bind_pool->conn.conn_list_mutex);
	
	inst->backend_type=cb;
	/* initialize monitor_availability */
	inst->monitor_availability.farmserver_state = FARMSERVER_AVAILABLE ; /* we expect the farm to be available */
	inst->monitor_availability.cpt              = 0 ;					 /* set up the failed conn counter to 0 */

	/* create RW lock to protect the config */
	inst->rwl_config_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, slapi_ch_strdup(name));

	/* quick hack 				    */
	/* put a ref to the config lock in the pool */
	/* so that the connection mgmt module can   */
	/* access the config safely		    */

	inst->pool->rwl_config_lock = inst->rwl_config_lock;
	inst->bind_pool->rwl_config_lock = inst->rwl_config_lock;

	for (i=0; i < MAX_CONN_ARRAY; i++) {
		inst->pool->connarray[i] = NULL;
		inst->bind_pool->connarray[i] = NULL;
	}

	/* Config is now merged with the backend entry */
	inst->configDn=slapi_ch_strdup(basedn);

	inst->monitorDn=slapi_ch_smprintf("cn=monitor,%s",basedn); 

	inst->eq_ctx = NULL;

	return inst;
}

void cb_instance_free(cb_backend_instance * inst) {

	if (inst) {
		PR_RWLock_Wlock(inst->rwl_config_lock);

		if ( inst->eq_ctx != NULL )
		{
			slapi_eq_cancel(inst->eq_ctx);
			inst->eq_ctx = NULL;
		}

		if (inst->bind_pool) 
        		cb_close_conn_pool(inst->bind_pool);
		if (inst->pool)
        		cb_close_conn_pool(inst->pool);

		slapi_destroy_condvar(inst->bind_pool->conn.conn_list_cv);
		slapi_destroy_condvar(inst->pool->conn.conn_list_cv);
		slapi_destroy_mutex(inst->monitor.mutex);
		slapi_destroy_mutex(inst->bind_pool->conn.conn_list_mutex);
		slapi_destroy_mutex(inst->pool->conn.conn_list_mutex);
		slapi_destroy_mutex(inst->monitor_availability.cpt_lock);
		slapi_destroy_mutex(inst->monitor_availability.lock_timeLimit);
		slapi_ch_free_string(&inst->configDn);
		slapi_ch_free_string(&inst->monitorDn);
		slapi_ch_free_string(&inst->inst_name);
		charray_free(inst->every_attribute);

		slapi_ch_free((void **) &inst->bind_pool);
		slapi_ch_free((void **) &inst->pool);

		PR_RWLock_Unlock(inst->rwl_config_lock);

		PR_DestroyRWLock(inst->rwl_config_lock);

		slapi_ch_free((void **) &inst);
	}
/* XXXSD */
}

/*
** Check the change the configuration of an existing chaining
** backend instance.
*/

int cb_instance_modify_config_check_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg) {

	cb_backend_instance * inst = (cb_backend_instance *) arg;
        LDAPMod **mods;
        int rc = LDAP_SUCCESS;
	int i;
	char * attr_name;
        returntext[0] = '\0';

	CB_ASSERT(inst!=NULL);
        slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );

	/* First pass to validate input */
        for (i = 0; mods[i] && LDAP_SUCCESS == rc; i++) {
                attr_name = mods[i]->mod_type;

		/* specific processing for multi-valued attributes */
		if ( !strcasecmp ( attr_name, CB_CONFIG_SUFFIX )) {
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "suffix modification not allowed\n");
                        rc = LDAP_UNWILLING_TO_PERFORM;
			continue;
		} else
                if ( !strcasecmp ( attr_name, CB_CONFIG_ILLEGAL_ATTRS )) {
			continue;
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_CHAINING_COMPONENTS )) {
			continue;
		} else

		/* CB_CONFIG_BINDUSER & CB_CONFIG_USERPASSWORD may be added */
		/* or deleted						    */

		if ( !strcasecmp ( attr_name, CB_CONFIG_USERPASSWORD ))  {
			continue;
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_BINDUSER ))  {

			/* Make sure value is not forbidden */
                	if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op) ||
                        	SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {

                        	rc = cb_instance_config_set((void *) inst, attr_name,
                  	      	cb_the_instance_config, mods[i]->mod_bvalues[0], returntext,
                              	CB_CONFIG_PHASE_RUNNING, 0);
				continue;
			}
		} 
		
                if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op) ||
                        SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                        rc= LDAP_UNWILLING_TO_PERFORM;
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "%s attributes is not allowed",
                                (mods[i]->mod_op & LDAP_MOD_DELETE) ? "Deleting" : "Adding");
                } else if (mods[i]->mod_op & LDAP_MOD_REPLACE) {
                        /* This assumes there is only one bval for this mod. */
                        rc = cb_instance_config_set((void *) inst, attr_name,
                  	      cb_the_instance_config, mods[i]->mod_bvalues[0], returntext,
                              CB_CONFIG_PHASE_RUNNING, 0);
                }
	}
        *returncode= rc;
		return ((LDAP_SUCCESS == rc) ? 1:-1);
}


/*
** Change the configuration of an existing chaining
** backend instance.
*/

int cb_instance_modify_config_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e,
        int *returncode, char *returntext, void *arg) {

	cb_backend_instance * inst = (cb_backend_instance *) arg;
        LDAPMod **mods;
        int rc = LDAP_SUCCESS;
	int i;
	int reopen_conn=0;
	char * attr_name;
        returntext[0] = '\0';

	CB_ASSERT(inst!=NULL);
        slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );

	/* input checked in the preop modify callback */

        for (i = 0; mods[i] && LDAP_SUCCESS == rc; i++) {
                attr_name = mods[i]->mod_type;

		/* specific processing for multi-valued attributes */
		if ( !strcasecmp ( attr_name, CB_CONFIG_ILLEGAL_ATTRS )) {
               		char * config_attr_value;
                       	int done=0;
			int j;

       			PR_RWLock_Wlock(inst->rwl_config_lock);
                        for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
                               	config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
                               	if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) {
                                       	if (!done) {
                                               	charray_free(inst->illegal_attributes);
                                               	inst->illegal_attributes=NULL;
                                               	done=1;
                                       	}
                                       	charray_add(&inst->illegal_attributes,
						slapi_ch_strdup(config_attr_value));
                               	} else
                               	if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                                       	charray_add(&inst->illegal_attributes,
                                               	slapi_ch_strdup(config_attr_value));
                               	} else
                               	if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                                       	charray_remove(inst->illegal_attributes,
                                               	slapi_ch_strdup(config_attr_value),
												0 /* freeit */);
                               	}
                        }
                        if (NULL == mods[i]->mod_bvalues) {
                               	charray_free(inst->illegal_attributes);
                               	inst->illegal_attributes=NULL;
                        }
        		PR_RWLock_Unlock(inst->rwl_config_lock);
                        continue;
		} 
		if ( !strcasecmp ( attr_name, CB_CONFIG_CHAINING_COMPONENTS )) {
               		char * config_attr_value;
                       	int done=0;
			int j;

        		PR_RWLock_Wlock(inst->rwl_config_lock);
                       	for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
                               	config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
                               	if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) {
                                       	if (!done) {
                                               	charray_free(inst->chaining_components);
                                               	inst->chaining_components=NULL;
                                               	done=1;
                                       	}
					/* XXXSD assume dns */
                                       	charray_add(&inst->chaining_components,
                                               	slapi_dn_normalize(slapi_ch_strdup(config_attr_value)));
                               	} else
                               	if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                                       	charray_add(&inst->chaining_components,
                                               	slapi_dn_normalize(slapi_ch_strdup(config_attr_value)));
                               	} else
                               	if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                                       	charray_remove(inst->chaining_components,
                                               	slapi_dn_normalize(slapi_ch_strdup(config_attr_value)),
												0 /* freeit */);
                               	}
                       	}
                       	if (NULL == mods[i]->mod_bvalues) {
                               	charray_free(inst->chaining_components);
                               	inst->chaining_components=NULL;
                       	}
        		PR_RWLock_Unlock(inst->rwl_config_lock);
                        continue;
		} 



		if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op) || 
                        SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {

			/* Special processing for binddn & password */
			/* because they are optional		    */

			if (( !strcasecmp ( attr_name, CB_CONFIG_BINDUSER )) || 
			    ( !strcasecmp ( attr_name, CB_CONFIG_USERPASSWORD ))) {

				if (mods[i]->mod_op & LDAP_MOD_DELETE) {
                        		rc = cb_instance_config_set((void *) inst, attr_name, 
					cb_the_instance_config, NULL, returntext,
					CB_CONFIG_PHASE_RUNNING, 1); }
				else {
                        		rc = cb_instance_config_set((void *) inst, attr_name, 
					cb_the_instance_config, mods[i]->mod_bvalues[0], returntext,
					CB_CONFIG_PHASE_RUNNING, 1);
				}
				if (rc==CB_REOPEN_CONN) {
					reopen_conn=1;
					rc=LDAP_SUCCESS;
				}
				continue;
			}
			
                        rc= LDAP_UNWILLING_TO_PERFORM;
                        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "%s attributes is not allowed",
                              (mods[i]->mod_op & LDAP_MOD_DELETE) ? "Deleting" : "Adding");
                } else if (mods[i]->mod_op & LDAP_MOD_REPLACE) {
                        /* This assumes there is only one bval for this mod. */
                        rc = cb_instance_config_set((void *) inst, attr_name, 
			cb_the_instance_config, mods[i]->mod_bvalues[0], returntext,
				CB_CONFIG_PHASE_RUNNING, 1);
		
			/* Update requires to reopen connections  */
			/* Expensive operation so do it only once */
			if (rc==CB_REOPEN_CONN) {
				reopen_conn=1;
				rc=LDAP_SUCCESS;
			}
                }
	}

        *returncode= rc; 

	if (reopen_conn) {
		cb_stale_all_connections(inst);
	}

        return ((LDAP_SUCCESS == rc) ? SLAPI_DSE_CALLBACK_OK:SLAPI_DSE_CALLBACK_ERROR);
}

/*
** Parse and instantiate backend instances 
*/

int
cb_parse_instance_config_entry(cb_backend * cb, Slapi_Entry * e) {

	int rc 			=LDAP_SUCCESS;
        Slapi_Attr 		*attr = NULL;
        Slapi_Value 		*sval;
    	const struct berval 	*attrValue;
	cb_backend_instance 	*inst=NULL;
	char			*instname;
	char 			retmsg[CB_BUFSIZE];

	CB_ASSERT(e!=NULL);
	
	/*
	** Retrieve the instance name and make sure it is not 
	** already declared.
	*/
	
	if ( 0 == slapi_entry_attr_find( e, CB_CONFIG_INSTNAME, &attr )) {
    		slapi_attr_first_value(attr, &sval);
    		attrValue = slapi_value_get_berval(sval);
		instname=attrValue->bv_val;
	} else {
		slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, 
			"Malformed backend instance (<%s> missing)>\n", CB_CONFIG_INSTNAME); 
		return LDAP_LOCAL_ERROR;
	}

        /* Allocate a new backend internal data structure */
        inst = cb_instance_alloc(cb,instname,slapi_entry_get_dn(e));

	/* Emulate a add config entry to configure */
	/* this backend instance.		   */

	cb_instance_add_config_callback(NULL,e,NULL,&rc,retmsg,inst);
	if ( rc != LDAP_SUCCESS ) {
		cb_instance_free(inst);
	}
	return rc;
}

/*
** Update the instance configuration
*/

static int 
cb_instance_config_initialize(cb_backend_instance * inst, Slapi_Entry * e , int phase, int apply) {

        int rc                  =LDAP_SUCCESS;
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
	struct berval * 	 bval;
	int 			using_def_connlifetime,i;
        char 			err_buf[SLAPI_DSE_RETURNTEXT_SIZE];
	int 			urlfound=0;
	char 			*rootdn;

	using_def_connlifetime=1;

        for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
		char * attr_name=NULL;	
                slapi_attr_get_type(attr, &attr_name);

		if ( !strcasecmp ( attr_name, CB_CONFIG_SUFFIX )) {
			if (apply && ( inst->inst_be != NULL )) {
  				Slapi_DN *suffix;
				suffix = slapi_sdn_new();
                        	i = slapi_attr_first_value(attr, &sval);
                        	while (i != -1 ) {
                                	bval = (struct berval *) slapi_value_get_berval(sval);
                                	slapi_sdn_init_dn_byref(suffix, bval->bv_val);

                                	if (!slapi_be_issuffix(inst->inst_be, suffix)) {
                                        	slapi_be_addsuffix(inst->inst_be, suffix);
                                	}
                                	slapi_sdn_done(suffix);
					slapi_sdn_free(&suffix);
                                	i = slapi_attr_next_value(attr, i, &sval);
                        	}
			}
                        continue;
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_CHAINING_COMPONENTS )) {

       			if (apply) {
	                	PR_RWLock_Wlock(inst->rwl_config_lock);
                                i = slapi_attr_first_value(attr, &sval);
				charray_free(inst->chaining_components);
				inst->chaining_components=NULL;
                                while (i != -1 ) {
                                        bval = (struct berval *) slapi_value_get_berval(sval);
					charray_add(&inst->chaining_components,
						slapi_dn_normalize(slapi_ch_strdup(bval->bv_val)));
                                        i = slapi_attr_next_value(attr, i, &sval);
                                }
	                	PR_RWLock_Unlock(inst->rwl_config_lock);
                        }
                        continue;
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_ILLEGAL_ATTRS )) {

       			if (apply) {
	                	PR_RWLock_Wlock(inst->rwl_config_lock);
                                i = slapi_attr_first_value(attr, &sval);
				charray_free(inst->illegal_attributes);
				inst->illegal_attributes=NULL;
                                while (i != -1 ) {
                                        bval = (struct berval *) slapi_value_get_berval(sval);
					charray_add(&inst->illegal_attributes,
						slapi_ch_strdup(bval->bv_val));
                                        i = slapi_attr_next_value(attr, i, &sval);
                                }
	                	PR_RWLock_Unlock(inst->rwl_config_lock);
                        }
                        continue;
		}


		if ( !strcasecmp ( attr_name, CB_CONFIG_HOSTURL )) {
			urlfound=1;
		}
			

      		/* We are assuming that each of these attributes are to have
                 * only one value.  If they have more than one value, like
                 * the nsslapd-suffix attribute, then they need to be
                 * handled differently. */

                slapi_attr_first_value(attr, &sval);
                bval = (struct berval *) slapi_value_get_berval(sval);
 
                if (cb_instance_config_set((void *) inst, attr_name, 
			cb_the_instance_config, bval, err_buf, phase, apply ) != LDAP_SUCCESS) {
                        slapi_log_error( SLAPI_LOG_FATAL, 
				CB_PLUGIN_SUBSYSTEM,"Error with config attribute %s : %s\n",
				attr_name, err_buf);
			rc=LDAP_LOCAL_ERROR;
                        break;
                }
                if ( !strcasecmp ( attr_name, CB_CONFIG_CONNLIFETIME )) {
			using_def_connlifetime=0;
		}
	}


	/*
	** Check for mandatory attributes
	** Post-Processing
	*/

	if (LDAP_SUCCESS == rc) {
		if (!urlfound) {
       	 		slapi_log_error( SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
			"Malformed backend instance entry. Mandatory attr <%s> missing\n",
			CB_CONFIG_HOSTURL);
			rc= LDAP_LOCAL_ERROR;
		}

		if (apply ) {
    			if ( using_def_connlifetime &&
                		strchr( inst->pool->hostname, ' ' ) != NULL ) {

                		cb_instance_config_set((void *)inst, CB_CONFIG_CONNLIFETIME, 
				cb_the_instance_config, NULL /* use default */, err_buf, 
					CB_CONFIG_PHASE_INITIALIZATION, 1 );
        		}
		}
	}

	/* 
	** Additional checks
	** It is forbidden to use directory manager as proxy user
	** due to a bug in the acl check
	*/

	rootdn=cb_get_rootdn();

	if (inst->impersonate && inst->pool && inst->pool->binddn && 
		!strcmp(inst->pool->binddn,rootdn)) {	/* UTF8 aware */
                slapi_log_error( SLAPI_LOG_FATAL,
                	CB_PLUGIN_SUBSYSTEM,"Error with config attribute %s (%s: forbidden value)\n",
                                CB_CONFIG_BINDUSER, rootdn);
                        rc=LDAP_LOCAL_ERROR;
	}
	slapi_ch_free((void **)&rootdn);

	return rc;
}

/********************************************************
** Get and set functions for chaining backend instances *
*********************************************************
*/

static void *cb_instance_hosturl_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	char * data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = slapi_ch_strdup(inst->pool->url);
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return data;
}

static int cb_instance_hosturl_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance 	*inst=(cb_backend_instance *) arg;
	char 			*url = (char *) value;
        LDAPURLDesc         	*ludp=NULL;
	int 			rc=LDAP_SUCCESS;

 	if (( rc = ldap_url_parse( url, &ludp )) != 0 ) {
		PL_strncpyz(errorbuf,cb_urlparse_err2string( rc ), SLAPI_DSE_RETURNTEXT_SIZE);
		if (CB_CONFIG_PHASE_INITIALIZATION == phase)
			inst->pool->url=slapi_ch_strdup("");
		return(LDAP_INVALID_SYNTAX);
	}
 
	if (apply) {

               	PR_RWLock_Wlock(inst->rwl_config_lock);

	        if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
       			( phase != CB_CONFIG_PHASE_STARTUP )) {

			/* Dynamic modification				*/
			/* Don't free char * pointer now		*/
			/* STore them in a waste basket 		*/
			/* Will be relesase when the backend stops 	*/
			
			if (inst->pool->hostname)
				charray_add(&inst->pool->waste_basket,inst->pool->hostname);
			if (inst->pool->url)
				charray_add(&inst->pool->waste_basket,inst->pool->url);
			
			if (inst->bind_pool->hostname)
				charray_add(&inst->bind_pool->waste_basket,inst->bind_pool->hostname);
			if (inst->bind_pool->url)
				charray_add(&inst->bind_pool->waste_basket,inst->bind_pool->url);

			/* Require connection cleanup */
			rc=CB_REOPEN_CONN;
		}

		/* Normal case. Extract useful data from */
		/* the url and update the configuration  */

		if ((ludp->lud_host==NULL) || (strlen(ludp->lud_host)==0)) {
                        inst->pool->hostname=(char *)slapi_ch_strdup((char *)get_localhost_DNS());
		} else {
               		inst->pool->hostname = slapi_ch_strdup( ludp->lud_host );
		}
               	inst->pool->url = slapi_ch_strdup( url);
               	inst->pool->secure = (( ludp->lud_options & LDAP_URL_OPT_SECURE ) != 0 );

		if ((ludp->lud_port==0) && inst->pool->secure)
			inst->pool->port=CB_LDAP_SECURE_PORT;
		else
               		inst->pool->port = ludp->lud_port;

		/* Build a charray of <host>:<port> */
		/* hostname is of the form <host>[:port] <host>[:port] */

		{ char * aBufCopy, * aHostName;
			char * iter = NULL;
			aBufCopy=slapi_ch_strdup(inst->pool->hostname);

			aHostName=ldap_utf8strtok_r(aBufCopy," ", &iter);
			charray_free(inst->url_array);
			inst->url_array=NULL;
			while (aHostName) {
			
				char * aHostPort;
				if ( NULL == strstr(aHostName,":")) {
					aHostPort = slapi_ch_smprintf("%s://%s:%d/",
												  inst->pool->secure ? "ldaps" : "ldap", 
												  aHostName,inst->pool->port);
				} else {
					aHostPort = slapi_ch_smprintf("%s://%s/", 
												  inst->pool->secure ? "ldaps" : "ldap",
												  aHostName);
				}

				charray_add(&inst->url_array,aHostPort);
				aHostName=ldap_utf8strtok_r(NULL," ", &iter);
			}

			slapi_ch_free((void **) &aBufCopy);
		}

		inst->bind_pool->port=inst->pool->port;
		inst->bind_pool->secure=inst->pool->secure;
		inst->bind_pool->hostname=slapi_ch_strdup(inst->pool->hostname);

	        PR_RWLock_Unlock(inst->rwl_config_lock);
	}

    	if ( ludp != NULL ) {
       		ldap_free_urldesc( ludp );
	}
        return rc;
}

static void *cb_instance_binduser_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	char * data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = slapi_ch_strdup(inst->pool->binddn2);	/* not normalized */	
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return data;
}

static int cb_instance_binduser_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	int rc=LDAP_SUCCESS;

	if (apply) {

	        PR_RWLock_Wlock(inst->rwl_config_lock);
		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
	    		( phase != CB_CONFIG_PHASE_STARTUP )) {

			/* Dynamic modif   */
			/* Free user later */

			charray_add(&inst->pool->waste_basket,inst->pool->binddn);
			charray_add(&inst->pool->waste_basket,inst->pool->binddn2);
			rc=CB_REOPEN_CONN;
		}

		inst->pool->binddn=slapi_ch_strdup((char *) value);
		inst->pool->binddn2=slapi_ch_strdup((char *) value);
		slapi_dn_normalize_case(inst->pool->binddn);
	        PR_RWLock_Unlock(inst->rwl_config_lock);
	} else {

		/* Security check */
		/* directory manager of the farm server should not be used as */
		/* proxing user. This is hard to check, so assume same directory */
		/* manager across servers.					   */
		
		char * rootdn = cb_get_rootdn();
		char * theValueCopy = NULL;

		if (value) {
			theValueCopy=slapi_ch_strdup((char *) value);
			slapi_dn_normalize_case(theValueCopy);
		}

		PR_RWLock_Rlock(inst->rwl_config_lock);
                if (inst->impersonate && theValueCopy && 
                        !strcmp(theValueCopy,rootdn)) {	/* UTF8-aware. See cb_get_dn() */
                        rc=LDAP_UNWILLING_TO_PERFORM; 
			if (errorbuf) {
				PR_snprintf(errorbuf,SLAPI_DSE_RETURNTEXT_SIZE, "value %s not allowed",rootdn);
			}
                }
                PR_RWLock_Unlock(inst->rwl_config_lock);

		slapi_ch_free((void **)&theValueCopy);
		slapi_ch_free((void **)&rootdn);
	}

	return rc;
}


static void *cb_instance_userpassword_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	char * data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = slapi_ch_strdup(inst->pool->password);
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return data;
}

static int cb_instance_userpassword_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	int rc=LDAP_SUCCESS;

	if (apply) {
               	PR_RWLock_Wlock(inst->rwl_config_lock);
		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
    			( phase != CB_CONFIG_PHASE_STARTUP )) {

			/* Dynamic modif */
			charray_add(&inst->pool->waste_basket,inst->pool->password);
			rc=CB_REOPEN_CONN;
		}

		inst->pool->password=slapi_ch_strdup((char *) value);
               	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return rc;
}

static void *cb_instance_sizelimit_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->sizelimit;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_sizelimit_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
            inst->sizelimit=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
		if (inst->inst_be) 
			be_set_sizelimit(inst->inst_be, (int) ((uintptr_t)value));
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_timelimit_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->timelimit;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_timelimit_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->timelimit=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
		if (inst->inst_be) 
			be_set_timelimit(inst->inst_be, (int) ((uintptr_t)value));
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_max_test_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->max_test_time;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_max_test_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->max_test_time=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_max_idle_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->max_idle_time;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_max_idle_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->max_idle_time=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}


static void *cb_instance_hoplimit_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->hoplimit;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_hoplimit_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->hoplimit=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_maxbconn_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->bind_pool->conn.maxconnections;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_maxbconn_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->bind_pool->conn.maxconnections=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_maxconn_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->pool->conn.maxconnections;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_maxconn_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->pool->conn.maxconnections=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_abandonto_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->abandon_timeout.tv_sec;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_abandonto_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;

	if (apply) {
		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
    			( phase != CB_CONFIG_PHASE_STARTUP )) {

			/* Dynamic modif not supported     */
			/* Stored in ldif only		   */
			return LDAP_SUCCESS;
		}

               	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->abandon_timeout.tv_sec=(int) ((uintptr_t)value);
		inst->abandon_timeout.tv_usec=0;
               	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_maxbconc_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->bind_pool->conn.maxconcurrency;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_maxbconc_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
        	PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->bind_pool->conn.maxconcurrency=(int) ((uintptr_t)value);
        	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_maxconc_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = inst->pool->conn.maxconcurrency;
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return (void *) data;
}

static int cb_instance_maxconc_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->pool->conn.maxconcurrency=(int) ((uintptr_t)value);
                PR_RWLock_Unlock(inst->rwl_config_lock);
	}
        return LDAP_SUCCESS;   
}

static void *cb_instance_imperson_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
        data = inst->impersonate;
        PR_RWLock_Unlock(inst->rwl_config_lock);
        return (void *) data;
}

static int cb_instance_imperson_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	int rc=LDAP_SUCCESS;

	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock); 
		inst->impersonate=(int) ((uintptr_t)value);
                PR_RWLock_Unlock(inst->rwl_config_lock); 
	} else {
		/* Security check: Make sure the proxing user is */
		/* not the directory manager.			 */

		char * rootdn=cb_get_rootdn();

                PR_RWLock_Rlock(inst->rwl_config_lock); 
		if (((int) ((uintptr_t)value)) && inst->pool && inst->pool->binddn &&
	                !strcmp(inst->pool->binddn,rootdn)) {	/* UTF-8 aware */
		  	rc=LDAP_UNWILLING_TO_PERFORM;
			if (errorbuf)
				PR_snprintf(errorbuf,SLAPI_DSE_RETURNTEXT_SIZE, "Proxy mode incompatible with %s value (%s not allowed)",
					CB_CONFIG_BINDUSER,rootdn);
		}
                PR_RWLock_Unlock(inst->rwl_config_lock); 
		slapi_ch_free((void **)&rootdn);
	}

        return rc;   
}

static void *cb_instance_connlife_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data; 
 
        PR_RWLock_Rlock(inst->rwl_config_lock); 
        data=inst->pool->conn.connlifetime;
        PR_RWLock_Unlock(inst->rwl_config_lock); 
        return (void *) data; 
}

static int cb_instance_connlife_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);  
		inst->pool->conn.connlifetime=(int) ((uintptr_t)value);
                PR_RWLock_Unlock(inst->rwl_config_lock);  
	}
        return LDAP_SUCCESS;     
}

static void *cb_instance_bindto_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;  
 
        PR_RWLock_Rlock(inst->rwl_config_lock);  
        data=inst->bind_pool->conn.op_timeout.tv_sec;
        PR_RWLock_Unlock(inst->rwl_config_lock);  
        return (void *) data;   
}

static int cb_instance_bindto_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);   
		inst->bind_pool->conn.op_timeout.tv_sec=(int) ((uintptr_t)value);
		inst->bind_pool->conn.op_timeout.tv_usec=0;
		inst->bind_pool->conn.bind_timeout.tv_sec=(int) ((uintptr_t)value);
		inst->bind_pool->conn.bind_timeout.tv_usec=0;
		/* Used to bind to the farm server */
		inst->pool->conn.bind_timeout.tv_sec=(int) ((uintptr_t)value);
		inst->pool->conn.bind_timeout.tv_usec=0;
                PR_RWLock_Unlock(inst->rwl_config_lock);   
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_opto_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;  
 
        PR_RWLock_Rlock(inst->rwl_config_lock);  
        data=inst->pool->conn.op_timeout.tv_sec;
        PR_RWLock_Unlock(inst->rwl_config_lock);  
        return (void *) data;   
}

static int cb_instance_opto_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
        cb_backend_instance * inst=(cb_backend_instance *) arg;
        if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);
                inst->pool->conn.op_timeout.tv_sec=(int) ((uintptr_t)value);
                inst->pool->conn.op_timeout.tv_usec=0;
                PR_RWLock_Unlock(inst->rwl_config_lock);
        }
        return LDAP_SUCCESS;
}

static void *cb_instance_ref_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;   
  
        PR_RWLock_Rlock(inst->rwl_config_lock);   
        data=inst->searchreferral;
        PR_RWLock_Unlock(inst->rwl_config_lock);   
        return (void *) data;    
}

static int cb_instance_ref_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);    
		inst->searchreferral=(int) ((uintptr_t)value);
                PR_RWLock_Unlock(inst->rwl_config_lock);    
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_acl_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
        data=inst->local_acl;
        PR_RWLock_Unlock(inst->rwl_config_lock);
        return (void *) data;
}

static int cb_instance_acl_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;

	if (apply) {
    		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
                        ( phase != CB_CONFIG_PHASE_STARTUP )) {

                        /* Dynamic modif not supported     */
                        /* Stored in ldif only             */
                        return LDAP_SUCCESS;
                }
	        PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->local_acl=(int) ((uintptr_t)value);
	        PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}

static void *cb_instance_bindretry_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock); 
        data=inst->bind_retry;
        PR_RWLock_Unlock(inst->rwl_config_lock); 
        return (void *) data; 
}

static int cb_instance_bindretry_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	if (apply) {
                PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->bind_retry=(int) ((uintptr_t)value);
                PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return LDAP_SUCCESS;
}


static void *cb_instance_starttls_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
        uintptr_t data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
        data=inst->pool->starttls;
        PR_RWLock_Unlock(inst->rwl_config_lock);
        return (void *) data;
}

static int cb_instance_starttls_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	int rc = LDAP_SUCCESS;

	if (apply) {
	        PR_RWLock_Wlock(inst->rwl_config_lock);
		inst->pool->starttls=(int) ((uintptr_t)value);
	        PR_RWLock_Unlock(inst->rwl_config_lock);
		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
    			( phase != CB_CONFIG_PHASE_STARTUP )) {
		    rc=CB_REOPEN_CONN; /* reconnect with the new starttls setting */
		}
	}
	return rc;
}

static void *cb_instance_bindmech_get(void *arg)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	char * data;

        PR_RWLock_Rlock(inst->rwl_config_lock);
	data = slapi_ch_strdup(inst->pool->mech);
        PR_RWLock_Unlock(inst->rwl_config_lock);
	return data;
}

static int cb_instance_bindmech_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
	cb_backend_instance * inst=(cb_backend_instance *) arg;
	int rc=LDAP_SUCCESS;

	if (apply) {
               	PR_RWLock_Wlock(inst->rwl_config_lock);
		if (( phase != CB_CONFIG_PHASE_INITIALIZATION ) &&
    			( phase != CB_CONFIG_PHASE_STARTUP )) {

			/* Dynamic modif */
			charray_add(&inst->pool->waste_basket,inst->pool->mech);
			rc=CB_REOPEN_CONN;
		}

		inst->pool->mech=slapi_ch_strdup((char *) value);
               	PR_RWLock_Unlock(inst->rwl_config_lock);
	}
	return rc;
}



/* Finds an entry in a config_info array with the given name.  Returns
 * the entry on success and NULL when not found.
 */
static cb_instance_config_info *cb_get_config_info(cb_instance_config_info *config_array, char *attr_name)
{
        int x;

        for(x = 0; config_array[x].config_name != NULL; x++) {
                if (!strcasecmp(config_array[x].config_name, attr_name)) {
                        return &(config_array[x]);
                }
        }
        return NULL;
}

/*
** Update an attribute value
** For now, unknown attributes are ignored
** Return a LDAP error code OR CB_REOPEN_CONN when the
** update requires to close open connections.
** err_buf is size SLAPI_DSE_RETURNTEXT_SIZE
*/
static int 
cb_instance_config_set(void *arg, char *attr_name, cb_instance_config_info *config_array, 
struct berval *bval, char *err_buf, int phase, int apply_mod)
{
        cb_instance_config_info *config;
        int use_default;
        int int_val;
        long long_val;
        int retval=LDAP_LOCAL_ERROR;

        config = cb_get_config_info(config_array, attr_name);
        if (NULL == config) {
		/* Ignore unknown attributes */
                return LDAP_SUCCESS;
        }

        /* If the config phase is initialization or if bval is NULL, we will use
         * the default value for the attribute. */
        if (CB_CONFIG_PHASE_INITIALIZATION == phase || NULL == bval) {
                use_default = 1;
        } else { 
                use_default = 0;
                /* Since we are setting the value for the config attribute, we
                 * need to turn on the CB_PREVIOUSLY_SET flag to make
                 * sure this attribute is shown. */
                config->config_flags |= CB_PREVIOUSLY_SET;
        }

        switch(config->config_type) {
        case CB_CONFIG_TYPE_INT:
                if (use_default) {
                        int_val = cb_atoi(config->config_default_value);
                } else {
                        int_val = cb_atoi((char *)bval->bv_val);
                }
                retval = config->config_set_fn(arg, (void *) ((uintptr_t)int_val), err_buf, phase, apply_mod);
                break;
        case CB_CONFIG_TYPE_INT_OCTAL:
                if (use_default) {
                        int_val = (int) strtol(config->config_default_value, NULL, 8);
                } else {
                        int_val = (int) strtol((char *)bval->bv_val, NULL, 8);
                }
                retval = config->config_set_fn(arg, (void *) ((uintptr_t)int_val), err_buf, phase, apply_mod);
                break;
        case CB_CONFIG_TYPE_LONG:
                if (use_default) {
                        long_val = cb_atol(config->config_default_value);
                } else {
                        long_val = cb_atol((char *)bval->bv_val);
                }
                retval = config->config_set_fn(arg, (void *) long_val, err_buf, phase, apply_mod);
                break;
        case CB_CONFIG_TYPE_STRING:
                if (use_default) {
                        retval = config->config_set_fn(arg, config->config_default_value, err_buf, phase, apply_mod);
                } else {
                        retval = config->config_set_fn(arg, bval->bv_val, err_buf, phase, apply_mod);
                }
                break;
        case CB_CONFIG_TYPE_ONOFF:
                if (use_default) {
                        int_val = !strcasecmp(config->config_default_value, "on");
                } else {
                        int_val = !strcasecmp((char *) bval->bv_val, "on");
                }
                retval = config->config_set_fn(arg, (void *) ((uintptr_t)int_val), err_buf, phase, apply_mod);
                break;
        }        
        return retval;
}

/* Utility function used in creating config entries.  Using the
 * config_info, this function gets info and formats in the correct
 * way.
 * buf is CB_BUFSIZE size
 */
void cb_instance_config_get(void *arg, cb_instance_config_info *config, char *buf)
{
        char *tmp_string;

        if (config == NULL) {
                buf[0] = '\0';
        }
 
        switch(config->config_type) {
        case CB_CONFIG_TYPE_INT:
                sprintf(buf, "%d", (int) ((uintptr_t)config->config_get_fn(arg)));
                break;
        case CB_CONFIG_TYPE_INT_OCTAL:
                sprintf(buf, "%o", (int) ((uintptr_t)config->config_get_fn(arg)));
                break;
        case CB_CONFIG_TYPE_LONG:
                sprintf(buf, "%ld", (long) config->config_get_fn(arg));
                break;
        case CB_CONFIG_TYPE_STRING:
                /* Remember the get function for strings returns memory
                 * that must be freed. */
                tmp_string = (char *) config->config_get_fn(arg);
                PR_snprintf(buf, CB_BUFSIZE, "%s", (char *) tmp_string);
                slapi_ch_free((void **)&tmp_string);
                break;
        case CB_CONFIG_TYPE_ONOFF:
                if ((int) ((uintptr_t)config->config_get_fn(arg))) {
                        sprintf(buf,"%s","on");
                } else {
                        sprintf(buf,"%s","off");
                }
                break;
	default:
                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        "Invalid attribute syntax.\n");
	
        }
}

/*
** Search for instance config entry
** Always return 'active' values because some configuration changes
** won't be taken into account until the server restarts
*/

int cb_instance_search_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter,
        int *returncode, char *returntext, void *arg) {

        char                    buf[CB_BUFSIZE];
        struct berval           val;
        struct berval           *vals[2];
        int                     i = 0;
        cb_backend_instance     *inst = (cb_backend_instance *)arg;
        cb_instance_config_info * config;

        vals[0] = &val;
        vals[1] = NULL;

        /* suffixes */

        PR_RWLock_Rlock(inst->rwl_config_lock);

        {
                const Slapi_DN *aSuffix;
                i=0;
		if (inst->inst_be) {
                while ((aSuffix=slapi_be_getsuffix(inst->inst_be,i))) {
                        val.bv_val = (char *)slapi_sdn_get_dn(aSuffix);
                        val.bv_len = strlen( val.bv_val );
			if (val.bv_len) {
                        	if (i==0)
                                	slapi_entry_attr_replace(e,CB_CONFIG_SUFFIX,(struct berval **)vals );
                        	else
                                	slapi_entry_attr_merge(e,CB_CONFIG_SUFFIX,(struct berval **)vals );
			}
                        i++;
                }
		}
        }

	for (i=0; inst->chaining_components && inst->chaining_components[i]; i++) {
                val.bv_val = inst->chaining_components[i];
                val.bv_len = strlen( val.bv_val );
		if (val.bv_len) {
        		if (i==0)
                		slapi_entry_attr_replace(e,CB_CONFIG_CHAINING_COMPONENTS,(struct berval **)vals );
                	else
                        	slapi_entry_attr_merge(e,CB_CONFIG_CHAINING_COMPONENTS,(struct berval **)vals );
		}
	}
	
	for (i=0; inst->illegal_attributes && inst->illegal_attributes[i]; i++) {
                val.bv_val = inst->illegal_attributes[i];
                val.bv_len = strlen( val.bv_val );
		if (val.bv_len) {
        		if (i==0)
                		slapi_entry_attr_replace(e,CB_CONFIG_ILLEGAL_ATTRS,(struct berval **)vals );
                	else
                        	slapi_entry_attr_merge(e,CB_CONFIG_ILLEGAL_ATTRS,(struct berval **)vals );
		}
	}

        PR_RWLock_Unlock(inst->rwl_config_lock);

	/* standard attributes */
        for(config = cb_the_instance_config; config->config_name != NULL; config++) {
                if (!(config->config_flags & (CB_ALWAYS_SHOW | CB_PREVIOUSLY_SET))) {
                        /* This config option shouldn't be shown */
                        continue;
                }

                cb_instance_config_get((void *) inst, config, buf);

                val.bv_val = buf;
                val.bv_len = strlen(buf);
		if (val.bv_len) 
                	slapi_entry_attr_replace(e, config->config_name, vals);
        }

        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
}

/*
** Ooops!!! The backend instance is beeing deleted
*/

int cb_instance_delete_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg) {

        cb_backend_instance * inst = (cb_backend_instance *) arg;
	int rc;
	Slapi_Entry * anEntry=NULL;
	Slapi_DN * aDn;

	CB_ASSERT( inst!=NULL );

	/* notify the front-end */
	slapi_mtn_be_stopping(inst->inst_be);

	/* Now it is safe to stop */
	/* No pending op          */


	/* unregister callbacks */
        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, inst->configDn, 
		LDAP_SCOPE_BASE, "(objectclass=*)", cb_instance_search_config_callback);

        slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_POSTOP, inst->configDn,
		 LDAP_SCOPE_BASE, "(objectclass=*)", cb_instance_delete_config_callback);

        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, inst->configDn, 
		LDAP_SCOPE_BASE, "(objectclass=*)", cb_instance_modify_config_check_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, inst->configDn, 
		LDAP_SCOPE_BASE, "(objectclass=*)", cb_instance_modify_config_callback);

	/* At this point, the monitor entry should have been removed */
	/* If not, manually call delete callback 		     */

        aDn = slapi_sdn_new_dn_byref(inst->monitorDn);
       	if ( LDAP_SUCCESS==(slapi_search_internal_get_entry(aDn,NULL, &anEntry,inst->backend_type->identity))) {
		cb_delete_monitor_callback( NULL, anEntry, NULL, &rc , NULL, inst );
		if (anEntry) 
	                slapi_entry_free(anEntry);
	}
        slapi_sdn_done(aDn);
        slapi_sdn_free(&aDn);

	/* free resources */
        cb_close_conn_pool(inst->bind_pool);
        cb_close_conn_pool(inst->pool);
	slapi_be_free(&(inst->inst_be));
        cb_instance_free(inst);

	return SLAPI_DSE_CALLBACK_OK;
}

static void cb_instance_add_monitor_later(time_t when, void *arg) {

	cb_backend_instance * inst = (cb_backend_instance *) arg;

	if ( inst != NULL )
	{
		PR_RWLock_Rlock(inst->rwl_config_lock);

		/* create the monitor entry if it is not there yet */
		if (LDAP_SUCCESS == cb_config_add_dse_entries(inst->backend_type, cb_skeleton_entries,
			inst->inst_name,CB_PLUGIN_NAME, NULL)) 
		{

			/* add monitor callbacks */
			slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, inst->monitorDn, LDAP_SCOPE_BASE,
					"(objectclass=*)", cb_search_monitor_callback, (void *) inst);

			slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, inst->monitorDn, LDAP_SCOPE_BASE,
					"(objectclass=*)", cb_dont_allow_that, (void *) NULL);

			slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP , inst->monitorDn, LDAP_SCOPE_BASE,
					"(objectclass=*)", cb_delete_monitor_callback, (void *) inst);
		}
		PR_RWLock_Unlock(inst->rwl_config_lock);
	}
}


int cb_instance_add_config_check_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg) {

       	int 			rc=LDAP_SUCCESS;
	cb_backend_instance 	*inst;
	cb_backend 		*cb=(cb_backend *) arg;
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
        const struct berval     *attrValue;
	char 			*instname=NULL;

	if (returntext)
		returntext[0]='\0';

	/* Basic entry check */
        if ( 0 == slapi_entry_attr_find( e, CB_CONFIG_INSTNAME, &attr )) {
                slapi_attr_first_value(attr, &sval);
                attrValue = slapi_value_get_berval(sval);
                instname=attrValue->bv_val;
        }
	if ( instname == NULL ) {
                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        "Malformed backend instance (<%s> missing)>\n", CB_CONFIG_INSTNAME);
				*returncode = LDAP_LOCAL_ERROR;
                return SLAPI_DSE_CALLBACK_ERROR;
        }

        /* Allocate a new backend internal data structure */
        inst = cb_instance_alloc(cb,instname,slapi_entry_get_dn(e));

	/* build the backend instance from the default hardcoded conf,  */
	/* the default instance config and the specific entry specified */
	if ((rc=cb_build_backend_instance_config(inst,e,0))
		!= LDAP_SUCCESS) {
                slapi_log_error( SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
                	"Can't instantiate chaining backend instance %s.\n",inst->inst_name);
		*returncode=rc;
                cb_instance_free(inst);
                return SLAPI_DSE_CALLBACK_ERROR;
        }

	/* Free the dummy instance */
	*returncode=rc;
        cb_instance_free(inst);

        return SLAPI_DSE_CALLBACK_OK;
}
 

/* Create the default instance config from hard-coded values */
/*
** Initialize the backend instance with the config entry 
** passed in arguments.
** <arg> : (cb_backend *)
*/

int cb_instance_add_config_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2,
       int *returncode, char *returntext, void *arg) {

       	int 			rc=LDAP_SUCCESS;
	cb_backend_instance 	*inst;
	cb_backend 		*cb=(cb_backend *) arg;
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
        const struct berval     *attrValue;
	char 			*instname=NULL;

	if (returntext)
		returntext[0]='\0';

	/* Basic entry check */
        if ( 0 == slapi_entry_attr_find( e, CB_CONFIG_INSTNAME, &attr )) {
                slapi_attr_first_value(attr, &sval);
                attrValue = slapi_value_get_berval(sval);
                instname=attrValue->bv_val;
        }
	if ( instname == NULL ) {
                slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
                        "Malformed backend instance (<%s> missing)>\n", CB_CONFIG_INSTNAME);
				*returncode = LDAP_LOCAL_ERROR;
                return SLAPI_DSE_CALLBACK_ERROR;
        }

        /* Allocate a new backend internal data structure */
        inst = cb_instance_alloc(cb,instname,slapi_entry_get_dn(e));

	/* build the backend instance from the default hardcoded conf,  */
	/* the default instance config and the specific entry specified */
	if ((rc=cb_build_backend_instance_config(inst,e,0))
		!= LDAP_SUCCESS) {
                slapi_log_error( SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
                	"Can't instantiate chaining backend instance %s.\n",inst->inst_name);
		*returncode=rc;
                cb_instance_free(inst);
                return SLAPI_DSE_CALLBACK_ERROR;
        }

	/* Instantiate a Slapi_Backend if necessary */
	if (!inst->isconfigured) {

		Slapi_PBlock 		*aPb=NULL;

        	inst->inst_be = slapi_be_new(CB_CHAINING_BACKEND_TYPE,slapi_ch_strdup(inst->inst_name),0,0);
        	aPb=slapi_pblock_new();
        	slapi_pblock_set(aPb, SLAPI_PLUGIN, inst->backend_type->plugin);
        	slapi_be_setentrypoint(inst->inst_be,0,(void *)NULL,aPb);
        	slapi_be_set_instance_info(inst->inst_be,inst);
        	slapi_pblock_set(aPb, SLAPI_PLUGIN, NULL);
        	slapi_pblock_destroy(aPb);
	}

	cb_build_backend_instance_config(inst,e,1);
	
	/* kexcoff: the order of the following calls is very important to prevent the deletion of the
		instance to happen before the creation of the monitor part of the config.
		However, not sure it solves all the situations, but at least it is worth to maintain
		this order. */

	if (!inst->isconfigured) 
	{ 
		/* Add monitor entry and callback on it 
		 * called from an add...
		 * we can't call recursively into the DSE to do more adds, they'll
		 * silently fail.  instead, schedule the adds to happen in 1 second.
		 */
		inst->eq_ctx = slapi_eq_once(cb_instance_add_monitor_later, (void *)inst, time(NULL)+1);
	}

	/* Get the list of operational attrs defined in the schema */
	/* see cb_search file for a reason for that		   */

	inst->every_attribute=slapi_schema_list_attribute_names(SLAPI_ATTR_FLAG_OPATTR);
	charray_add(&inst->every_attribute,slapi_ch_strdup(LDAP_ALL_USER_ATTRS));

	if (!inst->isconfigured) 
	{ 
		slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, inst->configDn,
		 LDAP_SCOPE_BASE,"(objectclass=*)",cb_instance_modify_config_check_callback, (void *) inst);
		slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, inst->configDn,
		 LDAP_SCOPE_BASE,"(objectclass=*)",cb_instance_modify_config_callback, (void *) inst);

		slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, inst->configDn, 
		LDAP_SCOPE_BASE,"(objectclass=*)", cb_instance_search_config_callback, (void *) inst);

		/* allow deletion otherwise impossible to remote a backend instance */
		/* dynamically...							  */
		slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_POSTOP, inst->configDn, 
		LDAP_SCOPE_BASE,"(objectclass=*)", cb_instance_delete_config_callback, (void *) inst);
	}

	/* Notify the front-end */
	/* After the call below, we can receive ops */
	slapi_mtn_be_started(inst->inst_be);

	inst->isconfigured=1;
	return SLAPI_DSE_CALLBACK_OK;
}
 

/* Create the default instance config from hard-coded values */

int cb_create_default_backend_instance_config(cb_backend * cb) {


	int 			rc;
	cb_backend_instance 	*dummy;
	Slapi_Entry 		*e=slapi_entry_alloc();
	char            	*defaultDn;
	char 			*olddn;
        struct berval           val;
        struct berval           *vals[2];
	Slapi_PBlock 		*pb;

	dummy = cb_instance_alloc(cb, "dummy", "o=dummy");
        cb_instance_config_set_default(dummy);
	cb_instance_search_config_callback(NULL,e,NULL, &rc, NULL,(void *) dummy);


	/* set right dn	 and objectclass */

	defaultDn = PR_smprintf("cn=default instance config,%s",cb->pluginDN);
	olddn = slapi_entry_get_dn(e);
	slapi_ch_free((void **) &olddn);
	
	slapi_entry_set_dn(e,slapi_ch_strdup(defaultDn));

 	vals[0] = &val;
        vals[1] = NULL;

 	val.bv_val = "top";
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_replace( e, "objectclass", (struct berval **)vals );
        val.bv_val = CB_CONFIG_EXTENSIBLEOCL;
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_merge( e, "objectclass", (struct berval **)vals );
 	val.bv_val = "default instance config";
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_replace( e, "cn", (struct berval **)vals );

	/* create entry */
 	pb = slapi_pblock_new();
        slapi_add_entry_internal_set_pb(pb, e, NULL, cb->identity, 0);
	slapi_add_internal_pb(pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if ( LDAP_SUCCESS != rc ) {
	  slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
			  "Add %s failed (%s)\n",defaultDn,ldap_err2string(rc));
	}

        slapi_pblock_destroy(pb); 
	/* cleanup */
        cb_instance_free(dummy);
	/* BEWARE: entry is consummed */
		PR_smprintf_free(defaultDn);
	return rc;
}

/* Extract backend instance configuration from the LDAP entry */

int cb_build_backend_instance_config(cb_backend_instance *inst, Slapi_Entry * conf, int apply) {

	cb_backend 	*cb = inst->backend_type;
        Slapi_PBlock    *default_pb;
	Slapi_Entry     **default_entries = NULL;
	Slapi_Entry 	*default_conf=NULL;
	int 		default_res, rc;
	char 		*defaultDn;
	cb_backend_instance * current_inst;

	rc=LDAP_SUCCESS;

	if (apply)
		current_inst=inst;
	else
		current_inst=cb_instance_alloc(cb,inst->inst_name,"cn=dummy");

	/* set default configuration */
	cb_instance_config_set_default(current_inst);

        /* 2: Overwrite values present in the default instance config */
 
        defaultDn = PR_smprintf("cn=default instance config,%s",cb->pluginDN);
 
        default_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(default_pb, defaultDn, LDAP_SCOPE_BASE,
                "objectclass=*", NULL, 0, NULL, NULL, cb->identity, 0);
        slapi_search_internal_pb (default_pb);
		PR_smprintf_free(defaultDn);
        slapi_pblock_get(default_pb, SLAPI_PLUGIN_INTOP_RESULT, &default_res);
        if ( LDAP_SUCCESS == default_res ) {
                slapi_pblock_get(default_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &default_entries);
                if (default_entries && default_entries[0] ) {
                               
                        struct berval           val;
                        struct berval           *vals[2];
                        vals[0] = &val;
                        vals[1] = NULL;
                        default_conf=default_entries[0];
 
                        /* hack: add a dummy url (mandatory) to avoid error */
                        /* will be overwritten by the one in conf entry     */
                        val.bv_val = "ldap://localhost/";
                        val.bv_len = strlen( val.bv_val );
                        slapi_entry_attr_replace( default_conf, CB_CONFIG_HOSTURL, (struct berval **)vals );
                                
                        rc=cb_instance_config_initialize(current_inst,default_conf,CB_CONFIG_PHASE_STARTUP,1);
                }
        }
        slapi_free_search_results_internal(default_pb);
        slapi_pblock_destroy(default_pb);  

	if (rc == LDAP_SUCCESS)
		rc=cb_instance_config_initialize(current_inst,conf,CB_CONFIG_PHASE_STARTUP,1);
	
	if (!apply)
		cb_instance_free(current_inst);

	return rc;
}
