/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "cb.h"
#include <errno.h>

/* Forward declarations */

static int cb_parse_config_entry(cb_backend * cb, Slapi_Entry *e);

/* body starts here */

/* Used to add an array of entries, like the one above and
** cb_instance_skeleton_entries to the dse.
** Returns 0 on success.
*/



int cb_config_add_dse_entries(cb_backend *cb, char **entries, char *string1, char *string2, char *string3)
{
        int x;
        Slapi_Entry *e;
        Slapi_PBlock *util_pb = NULL;
        int res, rc = 0;
        char entry_string[CB_BUFSIZE];

        for(x = 0; strlen(entries[x]) > 0; x++) {
                util_pb = slapi_pblock_new();
                sprintf(entry_string, entries[x], string1, string2, string3);
                e = slapi_str2entry(entry_string, 0);
                slapi_add_entry_internal_set_pb(util_pb, e, NULL, cb->identity, 0);
		slapi_add_internal_pb(util_pb);
		slapi_pblock_get(util_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
		if ( LDAP_SUCCESS != res && LDAP_ALREADY_EXISTS != res ) {
		  char ebuf[ BUFSIZ ];
		  slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, 
				  "Unable to add config entry (%s) to the DSE: %s\n",
				  escape_string(slapi_entry_get_dn(e), ebuf),
				  ldap_err2string(res));
		  rc = res;
		  slapi_pblock_destroy(util_pb);
		  break;
		}
		slapi_pblock_destroy(util_pb);
        }
        return rc;
}

/*
** Try to read the entry cn=config,cn=chaining database,cn=plugins,cn=config
** If the entry is there, then process the configuration information it stores.
** If it is missing, create it with default configuration.
** The default configuration is taken from the default entry if it exists
*/

int cb_config_load_dse_info(Slapi_PBlock * pb) {

	Slapi_PBlock 	*search_pb,*default_pb;
        Slapi_Entry 	**entries = NULL;
	Slapi_Entry 	*configEntry=NULL;
        int 		res,default_res,i;
	char		defaultDn[CB_BUFSIZE];
	cb_backend 	*cb;

        slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &cb );

	/* Get global configuration entry */
        search_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(search_pb, cb->configDN, LDAP_SCOPE_BASE,
                "objectclass=*", NULL, 0, NULL, NULL, cb->identity, 0);
        slapi_search_internal_pb (search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

	if ( LDAP_SUCCESS == res ) {
                slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
                if (NULL == entries || entries[0] == NULL) {
                        slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, 
				"Error accessing entry <%s>\n",cb->configDN);
                	slapi_free_search_results_internal(search_pb);
                	slapi_pblock_destroy(search_pb);
                        return 1;
		}
		configEntry=entries[0];
	} else
  	if ( LDAP_NO_SUCH_OBJECT == res ) {
		/* Don't do anything. The default conf is used */
		configEntry=NULL;
        } else {
                slapi_free_search_results_internal(search_pb);
                slapi_pblock_destroy(search_pb);
		slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
			"Error accessing entry <%s> (%s)\n",cb->configDN,ldap_err2string(res));
                return 1;
        } 

        /* Parse the configuration entry        */
        /* Default config if configEntry is NULL*/

        cb_parse_config_entry(cb, configEntry);
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);

	/*
	** Parse the chaining backend instances
	** Immediate subordinates of cn=<plugin name>,cn=plugins,cn=config
	*/

        search_pb = slapi_pblock_new();

        slapi_search_internal_set_pb(search_pb, cb->pluginDN, LDAP_SCOPE_ONELEVEL,
                CB_CONFIG_INSTANCE_FILTER,NULL,0,NULL,NULL,cb->identity, 0);
        slapi_search_internal_pb (search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
        if (res != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM, 
			"Error accessing the config DSE (%s)\n",ldap_err2string(res));
                slapi_free_search_results_internal(search_pb);
                slapi_pblock_destroy(search_pb);
                return 1;
	}

	/* Get the default instance value entry if it exists */
	/* else create it 				     */

	sprintf(defaultDn,"cn=default instance config,%s",cb->pluginDN);

        default_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(default_pb, defaultDn, LDAP_SCOPE_BASE,
                "objectclass=*", NULL, 0, NULL, NULL, cb->identity, 0);
        slapi_search_internal_pb (default_pb);
        slapi_pblock_get(default_pb, SLAPI_PLUGIN_INTOP_RESULT, &default_res);
	if (LDAP_SUCCESS != default_res) {
		cb_create_default_backend_instance_config(cb);
	}

        slapi_free_search_results_internal(default_pb);
        slapi_pblock_destroy(default_pb);

        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	for (i=0; entries && entries[i]; i++) {
		int retcode;
		char * aDn=slapi_entry_get_dn(entries[i]);
		slapi_dn_normalize(aDn);

		cb_instance_add_config_callback(pb,entries[i],NULL,&retcode,NULL,cb);
	}

        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);


	/* Add callbacks */
        slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, cb->configDN, 
		LDAP_SCOPE_BASE, "(objectclass=*)",cb_config_modify_check_callback, (void *) cb);
        slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP, cb->configDN, 
		LDAP_SCOPE_BASE, "(objectclass=*)",cb_config_modify_callback, (void *) cb);

        slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->configDN, 
		LDAP_SCOPE_BASE, "(objectclass=*)",cb_config_add_check_callback, (void *) cb);
        slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->configDN, 
		LDAP_SCOPE_BASE, "(objectclass=*)",cb_config_add_callback, (void *) cb);

        slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, cb->configDN,
		 LDAP_SCOPE_BASE, "(objectclass=*)",cb_config_search_callback, (void *) cb);

	/* instance creation */
        slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, cb->pluginDN, 
		LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER, cb_config_add_instance_check_callback, (void *) cb);

        slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_POSTOP, cb->pluginDN, 
		LDAP_SCOPE_SUBTREE, CB_CONFIG_INSTANCE_FILTER, cb_config_add_instance_callback, (void *) cb);

	return 0;
}

/* Check validity of the modification */

int cb_config_add_check_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2, int *returncode,
        char *returntext, void *arg)
{
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
        struct berval *          bval;
        int                     i;
        cb_backend      *cb = (cb_backend *) arg;
 
        CB_ASSERT (cb!=NULL);

        for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
                char * attr_name=NULL;
                slapi_attr_get_type(attr, &attr_name);
 
                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_FORWARD_CTRLS )) {
                        /* First, parse the values to make sure they are valid */
                        i = slapi_attr_first_value(attr, &sval);
                        while (i != -1 ) {
                                bval = (struct berval *) slapi_value_get_berval(sval);
                                if (!cb_is_control_forwardable(cb,bval->bv_val)) {
                                        slapi_log_error(SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                                                "control %s can't be forwarded.\n",bval->bv_val);
                                        *returncode=LDAP_CONSTRAINT_VIOLATION;
                                        return SLAPI_DSE_CALLBACK_ERROR;
                                }
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
                }
        }
        *returncode=LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
}

/*
** Global config is beeing added
** Take the new values into account
*/

int 
cb_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2, int *returncode, 
	char *returntext, void *arg) 
{
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
        struct berval *          bval;
        int                     i;
        cb_backend      *cb = (cb_backend *) arg;

        CB_ASSERT (cb!=NULL);

        for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
                char * attr_name=NULL;
                slapi_attr_get_type(attr, &attr_name);

                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_FORWARD_CTRLS )) {
			/* First, parse the values to make sure they are valid */
                	i = slapi_attr_first_value(attr, &sval);
                        while (i != -1 ) {
                        	bval = (struct berval *) slapi_value_get_berval(sval);
                                if (!cb_is_control_forwardable(cb,bval->bv_val)) {
                                        slapi_log_error(SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                                                "control %s can't be forwarded.\n",bval->bv_val);
                                        *returncode=LDAP_CONSTRAINT_VIOLATION;
                                        return SLAPI_DSE_CALLBACK_ERROR;
				}
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
			/* second pass. apply changes */
			cb_unregister_all_supported_control(cb);
                	i = slapi_attr_first_value(attr, &sval);
                        while (i != -1 ) {
                        	bval = (struct berval *) slapi_value_get_berval(sval);
				cb_register_supported_control(cb,bval->bv_val,0);
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
		}
	}
	*returncode=LDAP_SUCCESS;
	return SLAPI_DSE_CALLBACK_OK;
}

int 
cb_config_search_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* e2, int *returncode, 
	char *returntext, void *arg)  {

        cb_backend 	*cb = (cb_backend *) arg;
        struct berval           val;
        struct berval           *vals[2];
        int                     i = 0;

	CB_ASSERT (cb!=NULL);

        vals[0] = &val;
        vals[1] = NULL;

        /* naming attribute */
        val.bv_val = "config";
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_replace( e, "cn", (struct berval **)vals );

        /* objectclass attribute */
        val.bv_val = "top";
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_replace( e, "objectclass", (struct berval **)vals );
        val.bv_val = CB_CONFIG_EXTENSIBLEOCL;
        val.bv_len = strlen( val.bv_val );
        slapi_entry_attr_merge( e, "objectclass", (struct berval **)vals );
 
	/* other attributes */

        PR_RWLock_Rlock(cb->config.rwl_config_lock);

	for (i=0; cb->config.forward_ctrls && cb->config.forward_ctrls[i] ; i++) {
		val.bv_val=cb->config.forward_ctrls[i];
        	val.bv_len = strlen( val.bv_val );
		if (i==0)
        		slapi_entry_attr_replace( e, CB_CONFIG_GLOBAL_FORWARD_CTRLS, (struct berval **)vals ); 
		else
        		slapi_entry_attr_merge( e, CB_CONFIG_GLOBAL_FORWARD_CTRLS, (struct berval **)vals ); 
	}

	for (i=0;cb->config.chaining_components && cb->config.chaining_components[i];i++) {
		val.bv_val=cb->config.chaining_components[i];
        	val.bv_len = strlen( val.bv_val );
		if (i==0)
        		slapi_entry_attr_replace( e, CB_CONFIG_GLOBAL_CHAINING_COMPONENTS, 
				(struct berval **)vals ); 
		else
        		slapi_entry_attr_merge( e, CB_CONFIG_GLOBAL_CHAINING_COMPONENTS, 
				(struct berval **)vals ); 
	}

     	for (i=0; cb->config.chainable_components && cb->config.chainable_components[i]; i++) {
                val.bv_val=cb->config.chainable_components[i];
                val.bv_len = strlen( val.bv_val );
                if (i==0)
                        slapi_entry_attr_replace( e, CB_CONFIG_GLOBAL_CHAINABLE_COMPONENTS,
                                (struct berval **)vals );
                else
                        slapi_entry_attr_merge( e, CB_CONFIG_GLOBAL_CHAINABLE_COMPONENTS,
                                (struct berval **)vals );
        }


        PR_RWLock_Unlock(cb->config.rwl_config_lock);

        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
}

/* Check validity of the modification */

int 
cb_config_modify_check_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode,
        char *returntext, void *arg)
{
        LDAPMod         **mods;
        char            *attr_name;
        int             i,j;
        cb_backend      *cb = (cb_backend *) arg;
 
        CB_ASSERT (cb!=NULL);
 
        slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
 
  	for (i = 0; mods[i] ; i++) {
                attr_name = mods[i]->mod_type;
 
                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_FORWARD_CTRLS )) {
                        char * config_attr_value;
                        for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
                                config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
                                if (!cb_is_control_forwardable(cb,config_attr_value)) {
                                        slapi_log_error(SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
                                                "control %s can't be forwarded.\n",config_attr_value);
                                        *returncode=LDAP_CONSTRAINT_VIOLATION;
                                        return SLAPI_DSE_CALLBACK_ERROR;
                                }
			}
		}
	}
	*returncode=LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
}

int 
cb_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, 
	char *returntext, void *arg) 
{
        LDAPMod 	**mods;
	char 		*attr_name;
	int 		i,j;
        cb_backend 	*cb = (cb_backend *) arg;

	CB_ASSERT (cb!=NULL);

        slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );

        for (i = 0; mods[i] ; i++) {
		attr_name = mods[i]->mod_type;

               	if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_FORWARD_CTRLS )) {
			char * config_attr_value;
			int done=0;
        		for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
		                config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
				if (!cb_is_control_forwardable(cb,config_attr_value)) {
				        slapi_log_error(SLAPI_LOG_PLUGIN,CB_PLUGIN_SUBSYSTEM,
						"control %s can't be forwarded.\n",config_attr_value);
					*returncode=LDAP_CONSTRAINT_VIOLATION;
					return SLAPI_DSE_CALLBACK_ERROR;
				}

			        if ( mods[i]->mod_op & LDAP_MOD_REPLACE) {
					if (!done) {
						cb_unregister_all_supported_control(cb);
						done=1;
					}
					cb_register_supported_control(cb,config_attr_value,0);
				} else
			        if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD) {
					cb_register_supported_control(cb,config_attr_value,0);
				} else 
			        if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_DELETE) {
					cb_unregister_supported_control(cb,config_attr_value,0);
				}
			}
			if (NULL == mods[i]->mod_bvalues)
				cb_unregister_all_supported_control(cb);
		} else
                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_DEBUG )) {
			/* assume single-valued */
                        if (mods[i]->mod_op & LDAP_MOD_DELETE)
				cb_set_debug(0);
			else if ((mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD)
				cb_set_debug(1);
		} else
 		if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_CHAINING_COMPONENTS )) {
                        char * config_attr_value;
                        int done=0;

                        PR_RWLock_Wlock(cb->config.rwl_config_lock);

                        for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
                                config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
                                if ( mods[i]->mod_op & LDAP_MOD_REPLACE) {
                                        if (!done) {
					        charray_free(cb->config.chaining_components);
					        cb->config.chaining_components=NULL;
                                                done=1;
                                        }
					/* XXXSD assume dn. Normalize it */
					charray_add(&cb->config.chaining_components,
						slapi_dn_normalize(slapi_ch_strdup(config_attr_value)));
                                } else
                                if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD) {
					charray_add(&cb->config.chaining_components,
						slapi_dn_normalize(slapi_ch_strdup(config_attr_value)));
                                } else
                                if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_DELETE) {
					charray_remove(cb->config.chaining_components,
						slapi_dn_normalize(slapi_ch_strdup(config_attr_value)));
                                }
                        }
                        if (NULL == mods[i]->mod_bvalues) {
				charray_free(cb->config.chaining_components);
				cb->config.chaining_components=NULL;
			}

                        PR_RWLock_Unlock(cb->config.rwl_config_lock);
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_CHAINABLE_COMPONENTS )) {
                        char * config_attr_value;
                        int done=0;

                        PR_RWLock_Wlock(cb->config.rwl_config_lock);

                        for (j = 0; mods[i]->mod_bvalues && mods[i]->mod_bvalues[j]; j++) {
                                config_attr_value = (char *) mods[i]->mod_bvalues[j]->bv_val;
                                if ( mods[i]->mod_op & LDAP_MOD_REPLACE) {
                                        if (!done) {
                                                charray_free(cb->config.chainable_components);
                                                cb->config.chainable_components=NULL;
                                                done=1;
                                        }
                                        charray_add(&cb->config.chainable_components,
                                                slapi_dn_normalize(slapi_ch_strdup(config_attr_value)
));
                                } else
                                if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD) {
                                        charray_add(&cb->config.chainable_components,
                                                slapi_dn_normalize(slapi_ch_strdup(config_attr_value)
));
                                } else
                                if ( (mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_DELETE) {
                                        charray_remove(cb->config.chainable_components,
                                                slapi_dn_normalize(slapi_ch_strdup(config_attr_value)
));
                                }
                        }
                        if (NULL == mods[i]->mod_bvalues) {
                                charray_free(cb->config.chainable_components);
                                cb->config.chainable_components=NULL;
                        }

                        PR_RWLock_Unlock(cb->config.rwl_config_lock);
                }


	}
	*returncode=LDAP_SUCCESS;
	return SLAPI_DSE_CALLBACK_OK;
}

/*
** Creation of a new backend instance
*/

int 
cb_config_add_instance_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, 
	char *returntext, void *arg) 
{
        cb_backend      *cb=(cb_backend *)arg;
	CB_ASSERT(cb!=NULL);
        cb_instance_add_config_callback(pb,entryBefore,NULL,returncode,returntext,cb);
	return SLAPI_DSE_CALLBACK_OK;
}

int 
cb_config_add_instance_check_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, 
	char *returntext, void *arg) 
{
        cb_backend      *cb=(cb_backend *)arg;
	CB_ASSERT(cb!=NULL);
        return cb_instance_add_config_check_callback(pb,entryBefore,NULL,returncode,returntext,cb);
}

/*
** Parse the global chaining backend configuration
*/

static int cb_parse_config_entry(cb_backend * cb, Slapi_Entry *e)
{
        Slapi_Attr              *attr = NULL;
        Slapi_Value             *sval;
        struct berval 		*bval;
	int 			i;

	if (e == NULL)
		return LDAP_SUCCESS;

	cb_set_debug(0);

        for (slapi_entry_first_attr(e, &attr); attr; slapi_entry_next_attr(e, attr, &attr)) {
                char * attr_name=NULL;
                slapi_attr_get_type(attr, &attr_name);

                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_FORWARD_CTRLS )) {
                	i = slapi_attr_first_value(attr, &sval);

		        PR_RWLock_Wlock(cb->config.rwl_config_lock);
			if (cb->config.forward_ctrls) {
				charray_free(cb->config.forward_ctrls);
				cb->config.forward_ctrls=NULL;
			}
		        PR_RWLock_Unlock(cb->config.rwl_config_lock);

                        while (i != -1 ) {
                        	bval = (struct berval *) slapi_value_get_berval(sval);
				/* For now, don't support operation type */
				cb_register_supported_control(cb,bval->bv_val,
					SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE | 
					SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE |
					SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN);
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
		} else 
                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_CHAINING_COMPONENTS )) {
                	i = slapi_attr_first_value(attr, &sval);
		        PR_RWLock_Wlock(cb->config.rwl_config_lock);
			if (cb->config.chaining_components) {
				charray_free(cb->config.chaining_components);
				cb->config.chaining_components=NULL;
			}
                        while (i != -1 ) {
                        	bval = (struct berval *) slapi_value_get_berval(sval);
				/* XXXSD assume dn. Normalize it */
				charray_add( &cb->config.chaining_components,
					slapi_dn_normalize(slapi_ch_strdup(bval->bv_val)));
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
		        PR_RWLock_Unlock(cb->config.rwl_config_lock);
		} else
		if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_CHAINABLE_COMPONENTS )) {
                        i = slapi_attr_first_value(attr, &sval);
                        PR_RWLock_Wlock(cb->config.rwl_config_lock);
                        if (cb->config.chainable_components) {
                                charray_free(cb->config.chainable_components);
                                cb->config.chainable_components=NULL;
                        }
                        while (i != -1 ) {
                                bval = (struct berval *) slapi_value_get_berval(sval);
                                charray_add( &cb->config.chainable_components,
                                        slapi_dn_normalize(slapi_ch_strdup(bval->bv_val)));
                                i = slapi_attr_next_value(attr, i, &sval);
                        }
                        PR_RWLock_Unlock(cb->config.rwl_config_lock);
                } else
                if ( !strcasecmp ( attr_name, CB_CONFIG_GLOBAL_DEBUG )) {
                        i = slapi_attr_first_value(attr, &sval);
                        if (i != -1 ) {
                        	bval = (struct berval *) slapi_value_get_berval(sval);
				/* any value */
				cb_set_debug(1);
                        }
		}
	}
	return LDAP_SUCCESS;
}
