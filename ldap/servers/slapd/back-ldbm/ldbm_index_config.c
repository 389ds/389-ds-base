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

/* This file handles configuration information that is specific
 * to ldbm instance indexes.
 */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations for the callbacks */
int ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 
int ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 

#define INDEXTYPE_NONE 1

static int ldbm_index_parse_entry(ldbm_instance *inst, Slapi_Entry *e,
                                  const char *trace_string, char **index_name)
{
    Slapi_Attr *attr;
    const struct berval *attrValue;
    Slapi_Value *sval;

    /* Get the name of the attribute to index which will be the value
     * of the cn attribute. */
    if (slapi_entry_attr_find(e, "cn", &attr) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: malformed index entry %s\n",
                  slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    if (NULL == attrValue->bv_val || 0 == attrValue->bv_len) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Warning: malformed index entry %s -- empty index name\n",
                   slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    if(index_name != NULL){
    	*index_name = slapi_ch_strdup(attrValue->bv_val);
    }

    /* check and see if we have the required indexType */
    if (0 == slapi_entry_attr_find(e, "nsIndexType", &attr)) {
    	slapi_attr_first_value(attr, &sval);
    	attrValue = slapi_value_get_berval(sval);
    	if (NULL == attrValue->bv_val || attrValue->bv_len == 0) {
    		/* missing the index type, error out */
            LDAPDebug(LDAP_DEBUG_ANY,
                     "Warning: malformed index entry %s -- empty nsIndexType\n",
                     slapi_entry_get_dn(e), 0, 0);
            slapi_ch_free_string(index_name);
            return LDAP_OPERATIONS_ERROR;
        }
    }

    /* ok the entry is good to process, pass it to attr_index_config */
    attr_index_config(inst->inst_be, (char *)trace_string, 0, e, 0, 0);

    return LDAP_SUCCESS;
}

                                  
/*
 * Temp callback that gets called for each index entry when a new
 * instance is starting up.
 */
int 
ldbm_index_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *) arg;

    returntext[0] = '\0';
    *returncode = ldbm_index_parse_entry(inst, e, "from ldbm instance init", NULL);
    if (*returncode == LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Problem initializing index entry %s\n",
                slapi_entry_get_dn(e));
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

/* 
 * Config DSE callback for index additions.
 */	
int 
ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* eAfter, int *returncode, char *returntext, void *arg) 
{ 
    ldbm_instance *inst = (ldbm_instance *) arg;
    char *index_name;

    returntext[0] = '\0';
    *returncode = ldbm_index_parse_entry(inst, e, "from DSE add", &index_name);
    if (*returncode == LDAP_SUCCESS) {
        struct attrinfo *ai = NULL;
        /* if the index is a "system" index, we assume it's being added by
         * by the server, and it's okay for the index to go online immediately.
         * if not, we set the index "offline" so it won't actually be used
         * until someone runs db2index on it.
         */
        if (! ldbm_attribute_always_indexed(index_name)) {
        		ainfo_get(inst->inst_be, index_name, &ai);
        		PR_ASSERT(ai != NULL);
        		ai->ai_indexmask |= INDEX_OFFLINE;
        }
        slapi_ch_free((void **)&index_name);
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

/*
 * Config DSE callback for index deletes.
 */	
int 
ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg) 
{ 
  ldbm_instance *inst = (ldbm_instance *) arg;
  Slapi_Attr *attr;
  Slapi_Value *sval;
  const struct berval *attrValue;
  int rc = SLAPI_DSE_CALLBACK_OK;
  struct attrinfo *ainfo = NULL;
  
  returntext[0] = '\0';
  *returncode = LDAP_SUCCESS;
  
  slapi_entry_attr_find(e, "cn", &attr);
  slapi_attr_first_value(attr, &sval);
  attrValue = slapi_value_get_berval(sval);
  
  attr_index_config(inst->inst_be, "From DSE delete", 0, e, 0, INDEXTYPE_NONE);
  
  ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
  if (NULL == ainfo) {
    *returncode = LDAP_UNAVAILABLE;
    rc = SLAPI_DSE_CALLBACK_ERROR;
  } else {
    if (dblayer_erase_index_file(inst->inst_be, ainfo, 0 /* do chkpt */)) {
      *returncode = LDAP_UNWILLING_TO_PERFORM;
      rc = SLAPI_DSE_CALLBACK_ERROR;
    }
  }
  
  return rc;
}

/*
 * Config DSE callback for index entry changes.
 *
 * this function is huge!
 */
int
ldbm_instance_index_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e,
        Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    Slapi_Attr *attr;
    Slapi_Value *sval;
    const struct berval *attrValue;
    struct attrinfo *ainfo = NULL;

    returntext[0] = '\0';
    *returncode = LDAP_SUCCESS;

    if(slapi_entry_attr_find(entryAfter, "cn", &attr) != 0){
    	 LDAPDebug(LDAP_DEBUG_ANY,
    			 "Warning: malformed index entry %s - missing cn attribute\n",
    			 slapi_entry_get_dn(entryAfter), 0, 0);
    	 *returncode = LDAP_OBJECT_CLASS_VIOLATION;
    	 return SLAPI_DSE_CALLBACK_ERROR;
    }
    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);

    if (NULL == attrValue->bv_val || 0 == attrValue->bv_len) {
        LDAPDebug(LDAP_DEBUG_ANY,
        		"Warning: malformed index entry %s, missing index name\n",
        		slapi_entry_get_dn(e), 0, 0);
        *returncode =  LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
    if (NULL == ainfo) {
    	LDAPDebug(LDAP_DEBUG_ANY,
    			"Warning: malformed index entry %s - missing cn attribute info\n",
    			slapi_entry_get_dn(e), 0, 0);
    	*returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    if(slapi_entry_attr_find(entryAfter, "nsIndexType", &attr) != 0){
    	 LDAPDebug(LDAP_DEBUG_ANY,
    			 "Warning: malformed index entry %s - missing nsIndexType attribute\n",
    			 slapi_entry_get_dn(entryAfter), 0, 0);
    	 *returncode = LDAP_OBJECT_CLASS_VIOLATION;
    	 return SLAPI_DSE_CALLBACK_ERROR;
    }

    attr_index_config(inst->inst_be, "from DSE modify", 0, entryAfter, 0, 0);

    return SLAPI_DSE_CALLBACK_OK;
}

/* add index entries to the per-instance DSE (used only from instance.c) */
int ldbm_instance_config_add_index_entry(
    ldbm_instance *inst, 
    Slapi_Entry *e,
    int flags
)
{
    char *eBuf;
    int j = 0;
    char *basetype = NULL;
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;
    Slapi_Attr *attr;
    const struct berval *attrValue;
    Slapi_Value *sval;
    int rc = 0;

    /* get the cn value */
    if (slapi_entry_attr_find(e, "cn", &attr) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: malformed index entry %s, missing cn attrbiute\n",
                  slapi_entry_get_dn(e), 0, 0);
        return -1;
    }

    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    if (NULL == attrValue->bv_val || 0 == attrValue->bv_len) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Warning: malformed index entry %s, missing index name\n",
                  slapi_entry_get_dn(e), 0, 0);
        return -1;
    }

	basetype = slapi_attr_basetype(attrValue->bv_val, NULL, 0);
	dn = slapi_create_dn_string("cn=%s,cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                            basetype, inst->inst_name, li->li_plugin->plg_name);
	if (NULL == dn) {
		LDAPDebug(LDAP_DEBUG_ANY,
				"ldbm_instance_config_add_index_entry: "
				"failed create index dn with type %s for plugin %s, "
				"instance %s\n",
				basetype, inst->inst_li->li_plugin->plg_name,
				inst->inst_name);
		slapi_ch_free((void**)&basetype);
		return -1;
	}

	eBuf = PR_smprintf(
			"dn: %s\n"
			"objectclass: top\n"
			"objectclass: nsIndex\n"
			"cn: %s\n"
			"nsSystemIndex: %s\n",
                dn, basetype,
                (ldbm_attribute_always_indexed(basetype)?"true":"false"));
	slapi_ch_free_string(&dn);

	/* get nsIndexType and its values, and add them */
	if( 0 == slapi_entry_attr_find(e, "nsIndexType", &attr)){
		for (j = slapi_attr_first_value(attr, &sval); j != -1;j = slapi_attr_next_value(attr, j, &sval)) {
			attrValue = slapi_value_get_berval(sval);
			eBuf = PR_sprintf_append(eBuf, "nsIndexType: %s\n", attrValue->bv_val);
		}
	} else {
		LDAPDebug(LDAP_DEBUG_ANY,
				"ldbm_instance_config_add_index_entry: "
				"failed create index dn with type %s for plugin %s, "
				"instance %s.  Missing nsIndexType\n",
				basetype, inst->inst_li->li_plugin->plg_name,
				inst->inst_name);
		slapi_ch_free((void**)&basetype);
		return -1;
	}

	/* get nsMatchingRule and its values, and add them */
	if(0 == slapi_entry_attr_find(e, "nsMatchingRule", &attr)) {
		for (j = slapi_attr_first_value(attr, &sval); j != -1;j = slapi_attr_next_value(attr, j, &sval)) {
			attrValue = slapi_value_get_berval(sval);
			eBuf = PR_sprintf_append(eBuf, "nsMatchingRule: %s\n", attrValue->bv_val);
		}
 	}

	ldbm_config_add_dse_entry(li, eBuf, flags);
	if (eBuf) {
		PR_smprintf_free(eBuf);
	}

	slapi_ch_free((void**)&basetype);

    return rc;
}

int
ldbm_instance_index_config_enable_index(ldbm_instance *inst, Slapi_Entry* e)
{
    char *index_name;
    int rc;

    rc=ldbm_index_parse_entry(inst, e, "from DSE add", &index_name);
    if (rc == LDAP_SUCCESS) {
    	/* Assume the caller knows if it is OK to go online immediately */
        struct attrinfo *ai = NULL;

        ainfo_get(inst->inst_be, index_name, &ai);
        PR_ASSERT(ai != NULL);
        ai->ai_indexmask &= ~INDEX_OFFLINE;
        slapi_ch_free((void **)&index_name);
    } 
    return rc;
}

/*
** Create the default user-defined indexes
**
** Search for user-defined default indexes and add them
** to the backend instance being created.
*/

int ldbm_instance_create_default_user_indexes(ldbm_instance *inst)
{
    Slapi_PBlock *aPb;
    Slapi_Entry **entries = NULL;
    Slapi_Attr *attr;
    char *basedn = NULL;
    struct ldbminfo *li;

    /* write the dse file only on the final index */
    int flags = LDBM_INSTANCE_CONFIG_DONT_WRITE;

    if (NULL == inst) {
        LDAPDebug(LDAP_DEBUG_ANY, 
		"Warning: can't initialize default user indexes (invalid instance).\n", 0,0,0);
        return -1;
    }

    li = inst->inst_li;

    /* Construct the base dn of the subtree that holds the default user indexes. */
	basedn = slapi_create_dn_string("cn=default indexes,cn=config,cn=%s,cn=plugins,cn=config", 
										li->li_plugin->plg_name);
	if (NULL == basedn) {
		LDAPDebug1Arg(LDAP_DEBUG_ANY,
				      "ldbm_instance_create_default_user_indexes: "
				      "failed create default index dn for plugin %s\n",
				      inst->inst_li->li_plugin->plg_name);
        return -1;
	}

    /* Do a search of the subtree containing the index entries */
    aPb = slapi_pblock_new();
    slapi_search_internal_set_pb(aPb, basedn, LDAP_SCOPE_SUBTREE, 
	"(objectclass=nsIndex)", NULL, 0 , NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb (aPb);
    slapi_pblock_get(aPb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries!=NULL) {
        int i;
        for (i=0; entries[i]!=NULL; i++) {
                /*
                 * Get the name of the attribute to index which will be the value
                 * of the cn attribute.
                 */
                if (slapi_entry_attr_find(entries[i], "cn", &attr) != 0) {
                        LDAPDebug(LDAP_DEBUG_ANY,"Warning: malformed index entry %s. Index ignored.\n",
                                slapi_entry_get_dn(entries[i]), 0, 0);
                        continue;
                }

                /* Create the index entry in the backend */
                if (entries[i+1] == NULL) {
                    /* write the dse file only on the final index */
                    flags = 0;
                }

                ldbm_instance_config_add_index_entry(inst, entries[i], flags);

                /* put the index online */
                ldbm_instance_index_config_enable_index(inst, entries[i]);
        }
    }

    slapi_free_search_results_internal(aPb);
    slapi_pblock_destroy(aPb);
    slapi_ch_free_string(&basedn);
    return 0;
}
