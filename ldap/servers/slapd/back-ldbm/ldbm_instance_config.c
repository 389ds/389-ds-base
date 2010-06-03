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
 * to ldbm instances.
 */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations for the callbacks */
int ldbm_instance_search_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_modify_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);

static char *ldbm_instance_attrcrypt_filter = "(objectclass=nsAttributeEncryption)";

/* dse entries add for a new ldbm instance */
static char *ldbm_instance_skeleton_entries[] =
{
    "dn:cn=monitor, cn=%s, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:monitor\n",

    "dn:cn=index, cn=%s, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:index\n",
    
    "dn:cn=encrypted attributes, cn=%s, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:encrypted attributes\n",
    
    "dn:cn=encrypted attribute keys, cn=%s, cn=%s, cn=plugins, cn=config\n"
    "objectclass:top\n"
    "objectclass:extensibleObject\n"
    "cn:encrypted attribute keys\n",

    ""
};


/*------------------------------------------------------------------------
 * Get and set functions for ldbm instance variables
 *----------------------------------------------------------------------*/
static void *
ldbm_instance_config_cachesize_get(void *arg) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    
    return (void *) cache_get_max_entries(&(inst->inst_cache));
}

static int 
ldbm_instance_config_cachesize_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    int retval = LDAP_SUCCESS;
    long val = (long) value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        cache_set_max_entries(&(inst->inst_cache), val);
    }

    return retval;
}

static void * 
ldbm_instance_config_cachememsize_get(void *arg) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    
    return (void *) cache_get_max_size(&(inst->inst_cache));
}

static int 
ldbm_instance_config_cachememsize_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        cache_set_max_size(&(inst->inst_cache), val, CACHE_TYPE_ENTRY);
    }

    return retval;
}

static void * 
ldbm_instance_config_dncachememsize_get(void *arg) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    
    return (void *) cache_get_max_size(&(inst->inst_dncache));
}

static int 
ldbm_instance_config_dncachememsize_set(void *arg, void *value, char *errorbuf, int phase, int apply) 
{
    ldbm_instance *inst = (ldbm_instance *) arg;
    int retval = LDAP_SUCCESS;
    size_t val = (size_t) value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        cache_set_max_size(&(inst->inst_dncache), val, CACHE_TYPE_DN);
    }

    return retval;
}

static void *
ldbm_instance_config_readonly_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)((uintptr_t)inst->inst_be->be_readonly);
}

static void *
ldbm_instance_config_instance_dir_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (inst->inst_dir_name == NULL)
        return slapi_ch_strdup("");
    else if (inst->inst_parent_dir_name)
    {
        int len = strlen(inst->inst_parent_dir_name) +
                  strlen(inst->inst_dir_name) + 2;
        char *full_inst_dir = (char *)slapi_ch_malloc(len);
        PR_snprintf(full_inst_dir, len, "%s%c%s",
            inst->inst_parent_dir_name, get_sep(inst->inst_parent_dir_name),
            inst->inst_dir_name);
        return full_inst_dir;
    }
    else 
        return slapi_ch_strdup(inst->inst_dir_name);
}

static void *
ldbm_instance_config_require_index_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    
    return (void *)((uintptr_t)inst->require_index);
}

static int
ldbm_instance_config_instance_dir_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    if ((value == NULL) || (strlen(value) == 0))
    {
        inst->inst_dir_name = NULL;
        inst->inst_parent_dir_name = NULL;
    }
    else
    {
        char *dir = (char *)value;
        if (is_fullpath(dir))
        {
            char sep = get_sep(dir);
            char *p = strrchr(dir, sep);
            if (NULL == p)    /* should not happens, tho */
            {
                inst->inst_parent_dir_name = NULL;
                inst->inst_dir_name = rel2abspath(dir); /* normalize dir;
                                                           strdup'ed in 
                                                           rel2abspath */
            }
            else
            {
                *p = '\0';
                inst->inst_parent_dir_name = rel2abspath(dir); /* normalize dir;
                                                                  strdup'ed in
                                                                  rel2abspath */
                inst->inst_dir_name = slapi_ch_strdup(p+1);
                *p = sep;
            }
        }
        else
        {
            inst->inst_parent_dir_name = NULL;
            inst->inst_dir_name = slapi_ch_strdup(dir);
        }
    }
    return LDAP_SUCCESS;
}

static int
ldbm_instance_config_readonly_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    uintptr_t pval = (uintptr_t)value;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    if (CONFIG_PHASE_RUNNING == phase) {
        /* if the instance is busy, we'll save the user's readonly settings
         * but won't change them until the instance is un-busy again.
         */
        if (! (inst->inst_flags & INST_FLAG_BUSY)) {
            slapi_mtn_be_set_readonly(inst->inst_be, (int)pval);
        }
        if ((int)pval) {
            inst->inst_flags |= INST_FLAG_READONLY;
        } else {
            inst->inst_flags &= ~INST_FLAG_READONLY;
        }
    } else {
        slapi_be_set_readonly(inst->inst_be, (int)pval);
    }

    return LDAP_SUCCESS;
}

static int
ldbm_instance_config_require_index_set(void *arg, void *value, char *errorbuf, int phase, int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    inst->require_index = (int)((uintptr_t)value);

    return LDAP_SUCCESS;
}


/*------------------------------------------------------------------------
 * ldbm instance configuration array
 *----------------------------------------------------------------------*/
static config_info ldbm_instance_config[] = {
    {CONFIG_INSTANCE_CACHESIZE, CONFIG_TYPE_LONG, "-1", &ldbm_instance_config_cachesize_get, &ldbm_instance_config_cachesize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_CACHEMEMSIZE, CONFIG_TYPE_SIZE_T, "10485760", &ldbm_instance_config_cachememsize_get, &ldbm_instance_config_cachememsize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_READONLY, CONFIG_TYPE_ONOFF, "off", &ldbm_instance_config_readonly_get, &ldbm_instance_config_readonly_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_REQUIRE_INDEX, CONFIG_TYPE_ONOFF, "off", &ldbm_instance_config_require_index_get, &ldbm_instance_config_require_index_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_DIR, CONFIG_TYPE_STRING, NULL, &ldbm_instance_config_instance_dir_get, &ldbm_instance_config_instance_dir_set, CONFIG_FLAG_ALWAYS_SHOW},
    {CONFIG_INSTANCE_DNCACHEMEMSIZE, CONFIG_TYPE_SIZE_T, "10485760", &ldbm_instance_config_dncachememsize_get, &ldbm_instance_config_dncachememsize_set, CONFIG_FLAG_ALWAYS_SHOW|CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {NULL, 0, NULL, NULL, NULL, 0}
};

void 
ldbm_instance_config_setup_default(ldbm_instance *inst) 
{
    config_info *config;
    char err_buf[BUFSIZ];

    for (config = ldbm_instance_config; config->config_name != NULL; config++) {
        ldbm_config_set((void *)inst, config->config_name, ldbm_instance_config, NULL /* use default */, err_buf, CONFIG_PHASE_INITIALIZATION, 1 /* apply */);
    }
}

static int 
parse_ldbm_instance_entry(Slapi_Entry *e, char **instance_name)
{
    Slapi_Attr *attr = NULL;
    
    for (slapi_entry_first_attr(e, &attr); attr;
         slapi_entry_next_attr(e, attr, &attr)) {
        char *attr_name = NULL;

        slapi_attr_get_type(attr, &attr_name);
        if (strcasecmp(attr_name, "cn") == 0) {
            Slapi_Value *sval = NULL;
            struct berval *bval;
            slapi_attr_first_value(attr, &sval);
            bval = (struct berval *) slapi_value_get_berval(sval);
            *instance_name = slapi_ch_strdup((char *)bval->bv_val);
        }
    }
    return 0;
}

/* When a new instance is started, we need to read the dse to
 * find out what indexes should be maintained.  This function
 * does that.  Returns 0 on success. */
static int 
read_instance_index_entries(ldbm_instance *inst)
{
    Slapi_PBlock *tmp_pb;
    int scope = LDAP_SCOPE_SUBTREE;
    const char *searchfilter = "(objectclass=nsIndex)";
    char *basedn = NULL;

    /* Construct the base dn of the subtree that holds the index entries 
     * for this instance. */
    basedn = slapi_create_dn_string("cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                          inst->inst_name, inst->inst_li->li_plugin->plg_name); 
    if (NULL == basedn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "read_instance_index_entries: "
                       "failed create index dn for plugin %s, instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name); 
        return 1;
    }

    /* Set up a tmp callback that will handle the init for each index entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
        basedn, scope, searchfilter, ldbm_index_init_entry_callback,
        (void *) inst);

    /* Do a search of the subtree containing the index entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE, 
        searchfilter, NULL, 0, NULL, NULL, inst->inst_li->li_identity, 0);
    slapi_search_internal_pb (tmp_pb);

    /* Remove the tmp callback */
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
        basedn, scope, searchfilter, ldbm_index_init_entry_callback);
    slapi_free_search_results_internal(tmp_pb);
    slapi_pblock_destroy(tmp_pb);

    slapi_ch_free_string(&basedn);
    return 0;
}

/* When a new instance is started, we need to read the dse to
 * find out what attributes should be encrypted.  This function
 * does that.  Returns 0 on success. */
static int 
read_instance_attrcrypt_entries(ldbm_instance *inst)
{
    Slapi_PBlock *tmp_pb;
    int scope = LDAP_SCOPE_SUBTREE;
    const char *searchfilter = ldbm_instance_attrcrypt_filter;
    char *basedn = NULL;

    /* Construct the base dn of the subtree that holds the index entries 
     * for this instance. */
    basedn = slapi_create_dn_string("cn=encrypted attributes,cn=%s,cn=%s,cn=plugins,cn=config",
                          inst->inst_name, inst->inst_li->li_plugin->plg_name); 
    if (NULL == basedn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "read_instance_attrcrypt_entries: "
                       "failed create encrypted attributes dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return 1;
    }

    /* Set up a tmp callback that will handle the init for each index entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
        basedn, scope, searchfilter, ldbm_attrcrypt_init_entry_callback,
        (void *) inst);

    /* Do a search of the subtree containing the index entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE, 
        searchfilter, NULL, 0, NULL, NULL, inst->inst_li->li_identity, 0);
    slapi_search_internal_pb (tmp_pb);

    /* Remove the tmp callback */
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
        basedn, scope, searchfilter, ldbm_attrcrypt_init_entry_callback);
    slapi_free_search_results_internal(tmp_pb);
    slapi_pblock_destroy(tmp_pb);

    slapi_ch_free_string(&basedn);
    return 0;
}

/* Handles the parsing of the config entry for an ldbm instance.  Returns 0
 * on success. */    
static int 
parse_ldbm_instance_config_entry(ldbm_instance *inst, Slapi_Entry *e, config_info *config_array)
{
    Slapi_Attr *attr = NULL;
    
    for (slapi_entry_first_attr(e, &attr); attr; 
         slapi_entry_next_attr(e, attr, &attr)) { 
        char *attr_name = NULL;
        Slapi_Value *sval = NULL;
        struct berval *bval;
        char err_buf[BUFSIZ];
        
        slapi_attr_get_type(attr, &attr_name);

        /* There are some attributes that we don't care about, 
         * like objectclass. */
        if (ldbm_config_ignored_attr(attr_name)) {
            continue;
        }

        /* We have to handle suffix attributes a little differently */
        if (strcasecmp(attr_name, CONFIG_INSTANCE_SUFFIX) == 0) {
            Slapi_DN suffix;

            slapi_attr_first_value(attr, &sval);
            
            bval = (struct berval *) slapi_value_get_berval(sval);
            slapi_sdn_init_dn_byref(&suffix, bval->bv_val);
            if (!slapi_be_issuffix(inst->inst_be, &suffix)) {
                be_addsuffix(inst->inst_be, &suffix);
            }
            slapi_sdn_done(&suffix);
            continue;
        }

        /* We are assuming that each of these attributes are to have 
         * only one value.  If they have more than one value, like
         * the nsslapd-suffix attribute, then they need to be 
         * handled differently. */
        slapi_attr_first_value(attr, &sval);
        bval = (struct berval *) slapi_value_get_berval(sval);

        if (ldbm_config_set((void *) inst, attr_name, config_array, bval,
            err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */) != LDAP_SUCCESS) {
            LDAPDebug(LDAP_DEBUG_ANY, "Error with config attribute %s : %s\n",
                      attr_name, err_buf, 0);
            return 1;
        }
    }

    /* Read the index entries */
    read_instance_index_entries(inst);
    /* Read the attribute encryption entries */
    read_instance_attrcrypt_entries(inst);

    return 0;
}

/* general-purpose callback to deny an operation */
static int ldbm_instance_deny_config(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

/* Reads in any config information held in the dse for the given
 * entry.  Creates dse entries used to configure the given instance
 * if they don't already exist.  Registers dse callback functions to
 * maintain those dse entries.  Returns 0 on success. */
int 
ldbm_instance_config_load_dse_info(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    char *dn = NULL;
    int rval = 0;

    /* We try to read the entry 
     * cn=instance_name, cn=ldbm database, cn=plugins, cn=config.  If the
     * entry is there, then we process the config information it stores.
     */
    dn = slapi_create_dn_string("cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_config_load_dse_info: "
                       "failed create instance dn %s for plugin %s\n",
                       inst->inst_name, inst->inst_li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE, 
                                 "objectclass=*", NULL, 0, NULL, NULL,
                                 li->li_identity, 0);
    slapi_search_internal_pb (search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, "Error accessing the config DSE\n", 0, 0, 0);
        rval = 1;
        goto bail;
    } else {
        /* Need to parse the configuration information for the ldbm
         * plugin that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if ((!entries) || (!entries[0])) {
            LDAPDebug(LDAP_DEBUG_ANY, "Error accessing the config DSE\n",
                      0, 0, 0);
            rval = 1;
            goto bail;
        }
        if (0 != parse_ldbm_instance_config_entry(inst, entries[0],
                                         ldbm_instance_config)) {
            LDAPDebug(LDAP_DEBUG_ANY, "Error parsing the config DSE\n",
                      0, 0, 0);
            rval = 1;
            goto bail;
        }
    }

    if (search_pb)
    {
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
    }

    /* Add skeleton dse entries for this instance */
    /* IF they already exist, that's ok */
    ldbm_config_add_dse_entries(li, ldbm_instance_skeleton_entries,
                                inst->inst_name, li->li_plugin->plg_name,
                                inst->inst_name, 0);

    /* setup the dse callback functions for the ldbm instance config entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_search_config_entry_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_modify_config_entry_callback, (void *) inst);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_search_config_entry_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_deny_config, (void *)inst);
    /* delete is handled by a callback set in ldbm_config.c */

    slapi_ch_free_string(&dn);

    /* don't forget the monitor! */
    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_config_load_dse_info: "
                       "failed create monitor instance dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        rval = 1;
        goto bail;
    }
    /* make callback on search; deny add/modify/delete */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_back_monitor_instance_search,
        (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=*)", ldbm_instance_deny_config,
        (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_instance_deny_config,
        (void *)inst);
    /* delete is okay */
    slapi_ch_free_string(&dn);

    /* Callbacks to handle indexes */
    dn = slapi_create_dn_string("cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_config_load_dse_info: "
                       "failed create index instance dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_add_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_delete_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_modify_callback, (void *) inst);
    slapi_ch_free_string(&dn);

    /* Callbacks to handle attribute encryption */
    dn = slapi_create_dn_string("cn=encrypted attributes,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_config_load_dse_info: "
                       "failed create encrypted attribute instance dn "
                       "for plugin %s, instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_add_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_delete_callback, (void *) inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_modify_callback, (void *) inst);
    rval = 0;
bail:
    slapi_ch_free_string(&dn);
    return rval;
}

/*
 * Config. DSE callback for instance entry searches.
 */
int 
ldbm_instance_search_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    ldbm_instance *inst = (ldbm_instance *) arg;
    config_info *config;
    int x;
    const Slapi_DN *suffix;

    vals[0] = &val;
    vals[1] = NULL;  

    returntext[0] = '\0';

    /* show the suffixes */
    attrlist_delete(&e->e_attrs, CONFIG_INSTANCE_SUFFIX);
    x = 0;
    do {
        suffix = slapi_be_getsuffix(inst->inst_be, x);
        if (suffix != NULL) {
            val.bv_val = (char *) slapi_sdn_get_dn(suffix);
            val.bv_len = strlen (val.bv_val);
            attrlist_merge( &e->e_attrs, CONFIG_INSTANCE_SUFFIX, vals );
        }
        x++;
    } while(suffix!=NULL);

    PR_Lock(inst->inst_config_mutex);

    for(config = ldbm_instance_config; config->config_name != NULL; config++) {
        /* Go through the ldbm_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        ldbm_config_get((void *) inst, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }
    
    PR_Unlock(inst->inst_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

/* This function is used by the instance modify callback to add a new
 * suffix.  It return LDAP_SUCCESS on success.
 */
int 
add_suffix(ldbm_instance *inst, struct berval **bvals, int apply_mod, char *returntext)
{
    Slapi_DN suffix;
    int x;

    returntext[0] = '\0';
    for (x = 0; bvals[x]; x++) {
        slapi_sdn_init_dn_byref(&suffix, bvals[x]->bv_val);
        if (!slapi_be_issuffix(inst->inst_be, &suffix) && apply_mod) {
            be_addsuffix(inst->inst_be, &suffix);
        }
        slapi_sdn_done(&suffix);
    }

    return LDAP_SUCCESS;
}

/*
 * Config. DSE callback for instance entry modifies.
 */        
int 
ldbm_instance_modify_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg) 
{ 
    int i; 
    char *attr_name; 
    LDAPMod **mods; 
    int rc = LDAP_SUCCESS; 
    int apply_mod = 0; 
    ldbm_instance *inst = (ldbm_instance *) arg;

    /* This lock is probably way too conservative, but we don't expect much
     * contention for it. */
    PR_Lock(inst->inst_config_mutex);

    slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods ); 
    
    returntext[0] = '\0'; 

    /* 
     * First pass: set apply mods to 0 so only input validation will be done; 
     * 2nd pass: set apply mods to 1 to apply changes to internal storage 
     */ 
    for ( apply_mod = 0; apply_mod <= 1 && LDAP_SUCCESS == rc; apply_mod++ ) {
        for (i = 0; mods[i] && LDAP_SUCCESS == rc; i++) {
            attr_name = mods[i]->mod_type;

            if (strcasecmp(attr_name, CONFIG_INSTANCE_SUFFIX) == 0) {
                /* naughty naughty, we don't allow this */
                rc = LDAP_UNWILLING_TO_PERFORM;
                if (returntext) {
					PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
								"Can't change the root suffix of a backend");
                }
                LDAPDebug(LDAP_DEBUG_ANY,
                          "ldbm: modify attempted to change the root suffix "
                          "of a backend (which is not allowed)\n",
                          0, 0, 0);
                continue;
            }

            /* There are some attributes that we don't care about, like
             * modifiersname. */
            if (ldbm_config_ignored_attr(attr_name)) {
                continue;
            }

            if ((mods[i]->mod_op & LDAP_MOD_DELETE) || 
                (mods[i]->mod_op & LDAP_MOD_ADD)) { 
                rc= LDAP_UNWILLING_TO_PERFORM; 
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "%s attributes is not allowed", 
                        (mods[i]->mod_op & LDAP_MOD_DELETE) ?
                        "Deleting" : "Adding"); 
            } else if (mods[i]->mod_op & LDAP_MOD_REPLACE) {
        /* This assumes there is only one bval for this mod. */
                rc = ldbm_config_set((void *) inst, attr_name,
                    ldbm_instance_config, mods[i]->mod_bvalues[0], returntext,
                    CONFIG_PHASE_RUNNING, apply_mod);
            }
        }
    }

    PR_Unlock(inst->inst_config_mutex);

    *returncode = rc;
    if (LDAP_SUCCESS == rc) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

/* This function is used to set instance config attributes. It can be used as a 
 * shortcut to doing an internal modify operation on the config DSE.
 */
void 
ldbm_instance_config_internal_set(ldbm_instance *inst, char *attrname, char *value)
{
    char err_buf[BUFSIZ];
    struct berval bval;

    bval.bv_val = value;
    bval.bv_len = strlen(value);

    if (ldbm_config_set((void *) inst, attrname, ldbm_instance_config, &bval, 
        err_buf, CONFIG_PHASE_INTERNAL, 1 /* apply */) != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, 
            "Internal Error: Error setting instance config attr %s to %s: %s\n", 
            attrname, value, err_buf);
        exit(1);
    }
}



static int ldbm_instance_generate(struct ldbminfo *li, char *instance_name,
                                  Slapi_Backend **ret_be)
{
    Slapi_Backend *new_be = NULL;
    int rc = 0;

    /* Create a new instance, process config info for it,
     * and then call slapi_be_new and create a new backend here
     */
    new_be = slapi_be_new(LDBM_DATABASE_TYPE_NAME /* type */, instance_name, 
                          0 /* public */, 1 /* do log changes */);
    new_be->be_database = li->li_plugin;
    ldbm_instance_create(new_be, instance_name);

    ldbm_instance_config_load_dse_info(new_be->be_instance_info);
    rc = ldbm_instance_create_default_indexes(new_be);

    /* if USN plugin is enabled, set slapi_counter */
    if (plugin_enabled("USN", li->li_identity)) {
        /* slapi_counter_new sets the initial value to 0 */
        new_be->be_usn_counter = slapi_counter_new();
    }

    if (ret_be != NULL) {
        *ret_be = new_be;
    }

    return rc;
}

int
ldbm_instance_postadd_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    backend *be = NULL;
    struct ldbm_instance *inst;
    char *instance_name;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    int rval = 0;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    ldbm_instance_generate(li, instance_name, &be);

    inst = ldbm_instance_find_by_name(li, instance_name);

    /* Add default indexes */
    ldbm_instance_create_default_user_indexes(inst);

    /* Initialize and register callbacks for VLV indexes */
    vlv_init(inst);

    /* this is an ACTUAL ADD being done while the server is running!
     * start up the appropriate backend...
     */
    rval = ldbm_instance_start(be);
    if (0 != rval)
    {
        LDAPDebug(LDAP_DEBUG_ANY,
            "ldbm_instance_postadd_instance_entry_callback: "
            "ldbm_instnace_start (%s) failed (%d)\n",
            instance_name, rval, 0);
    }

    slapi_ch_free((void **)&instance_name);

    /* instance must be fully ready before we call this */
    slapi_mtn_be_started(be);

    return SLAPI_DSE_CALLBACK_OK;
}

int
ldbm_instance_add_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg) 
{
    char *instance_name;
    struct ldbm_instance *inst= NULL;
    struct ldbminfo *li= (struct ldbminfo *) arg;
    int rc = 0;

    parse_ldbm_instance_entry(entryBefore, &instance_name);

    /* Make sure we don't create two instances with the same name. */
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (inst != NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: ldbm instance %s already exists\n",
                  instance_name, 0, 0);
        if (returntext != NULL)
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "An ldbm instance with the name %s already exists\n",
                    instance_name);
        if (returncode != NULL)
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        slapi_ch_free((void **)&instance_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    if (pb == NULL) {
        /* called during startup -- do the rest now */
        rc = ldbm_instance_generate(li, instance_name, NULL);
        if (!rc) {
            inst = ldbm_instance_find_by_name(li, instance_name);
            rc = ldbm_instance_create_default_user_indexes(inst);
        }
    }
    /* if called during a normal ADD operation, the postadd callback
     * will do the rest.
     */

    slapi_ch_free((void **)&instance_name);
    return (rc == 0) ? SLAPI_DSE_CALLBACK_OK : SLAPI_DSE_CALLBACK_ERROR;
}




/* unregister the DSE callbacks on a backend -- this needs to be done when
 * deleting a backend, so that adding the same backend later won't cause
 * these expired callbacks to be called.
 */
static void ldbm_instance_unregister_callbacks(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    /* tear down callbacks for the instance config entry */
    dn = slapi_create_dn_string("cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_unregister_callbacks: "
                       "failed create instance dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        goto bail;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_search_config_entry_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_modify_config_entry_callback);
    slapi_config_remove_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_search_config_entry_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)",
        ldbm_instance_deny_config);
    slapi_ch_free_string(&dn);

    /* now the cn=monitor entry */
    dn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_unregister_callbacks: "
                       "failed create monitor instance dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        goto bail;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_back_monitor_instance_search);
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=*)", ldbm_instance_deny_config);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_BASE, "(objectclass=*)", ldbm_instance_deny_config);
    slapi_ch_free_string(&dn);

    /* now the cn=index entries */
    dn = slapi_create_dn_string("cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_unregister_callbacks: "
                       "failed create index dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        goto bail;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_add_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_delete_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
        ldbm_instance_index_config_modify_callback);
    slapi_ch_free_string(&dn);

    /* now the cn=encrypted attributes entries */
    dn = slapi_create_dn_string("cn=encrypted attributes,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        LDAPDebug2Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_unregister_callbacks: "
                       "failed create encrypted attributes dn for plugin %s, "
                       "instance %s\n",
                       inst->inst_li->li_plugin->plg_name, inst->inst_name);
        goto bail;
    }
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_add_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_delete_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
        LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
        ldbm_instance_attrcrypt_config_modify_callback);

    vlv_remove_callbacks(inst);
bail:
    slapi_ch_free_string(&dn);
}


int
ldbm_instance_post_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    char *instance_name;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct ldbm_instance *inst = NULL;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    inst = ldbm_instance_find_by_name(li, instance_name);

    if (inst == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: instance '%s' does not exist! (2)\n",
                  instance_name, 0, 0);
        if (returntext) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "No ldbm instance exists with the name '%s' (2)\n",
                    instance_name);
        }
        if (returncode) {
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        }
        slapi_ch_free((void **)&instance_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    LDAPDebug(LDAP_DEBUG_ANY, "ldbm: removing '%s'.\n", instance_name, 0, 0);

    {
        struct ldbminfo *li = (struct ldbminfo *) inst->inst_be->be_database->plg_private;
        dblayer_private *priv = (dblayer_private*) li->li_dblayer_private;
        struct dblayer_private_env *pEnv = priv->dblayer_env; 
        if(pEnv) {
            PRDir *dirhandle = NULL;
            char inst_dir[MAXPATHLEN*2];
            char *inst_dirp = NULL;

            if (inst->inst_dir_name == NULL){
                dblayer_get_instance_data_dir(inst->inst_be);
            }    
            inst_dirp = dblayer_get_full_inst_dir(li, inst, 
                                                  inst_dir, MAXPATHLEN*2);
            if (NULL != inst_dirp) {
                dirhandle = PR_OpenDir(inst_dirp);
                /* the db dir instance may have been removed already */
                if (dirhandle) {
                    PRDirEntry *direntry = NULL;
                    char *dbp = NULL;
                    char *p = NULL;
                    while (NULL != (direntry = PR_ReadDir(dirhandle,
                                               PR_SKIP_DOT|PR_SKIP_DOT_DOT))) {
                        int rc;
                        if (!direntry->name)
                            break;

                        dbp = PR_smprintf("%s/%s", inst_dirp, direntry->name);
                        if (NULL == dbp) {
                            LDAPDebug (LDAP_DEBUG_ANY,
                            "ldbm_instance_post_delete_instance_entry_callback:"
                            " failed to generate db path: %s/%s\n",
                            inst_dirp, direntry->name, 0);
                            break;
                        }

                        p = strstr(dbp, LDBM_FILENAME_SUFFIX);
                        if (NULL != p &&
                            strlen(p) == strlen(LDBM_FILENAME_SUFFIX)) {
                            rc = dblayer_db_remove(pEnv, dbp, 0);
                        } else {
                            rc = PR_Delete(dbp);
                        }
                        PR_ASSERT(rc == 0);
                        PR_smprintf_free(dbp);
                    }
                    PR_CloseDir(dirhandle);                
                }
                /* 
                 * When a backend was removed, the db instance directory 
                 * was removed as well (See also bz463774).
                 * In case DB_RECOVER_FATAL is set in the DB open after 
                 * the removal (e.g., in restore), the logs in the transaction
                 * logs are replayed and compared with the contents of the DB 
                 * files.  At that time, if the db instance directory does not 
                 * exist, libdb returns FATAL error.  To prevent the problem, 
                 * we have to leave the empty directory. (bz597375)
                 *
                 * PR_RmDir(inst_dirp);
                 */
            } /* non-null dirhandle */
            if (inst_dirp != inst_dir) {
                slapi_ch_free_string(&inst_dirp);
            }
        } /* non-null pEnv */
    }

    ldbm_instance_unregister_callbacks(inst);    
    slapi_be_free(&inst->inst_be);
    ldbm_instance_destroy(inst);
    slapi_ch_free((void **)&instance_name);

    return SLAPI_DSE_CALLBACK_OK;
}

int
ldbm_instance_delete_instance_entry_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    char *instance_name;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct ldbm_instance *inst = NULL;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (inst == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: instance '%s' does not exist!\n",
                  instance_name, 0, 0);
        if (returntext) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "No ldbm instance exists with the name '%s'\n",
                    instance_name);
        }
        if (returncode) {
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        }
        slapi_ch_free((void **)&instance_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* check if some online task is happening */
    if (instance_set_busy(inst) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm: '%s' is in the middle of a task. "
                  "Cancel the task or wait for it to finish, "
                  "then try again.\n", instance_name, 0, 0);
        if (returntext) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "ldbm instance '%s' is in the middle of a "
                    "task. Cancel the task or wait for it to finish, "
                    "then try again.\n", instance_name);
        }
        if (returncode) {
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        }
        slapi_ch_free((void **)&instance_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* okay, we're gonna delete this database instance.  take it offline. */
    LDAPDebug(LDAP_DEBUG_ANY, "ldbm: Bringing %s offline...\n",
              instance_name, 0, 0);
    slapi_mtn_be_stopping(inst->inst_be);
    dblayer_instance_close(inst->inst_be);
    cache_destroy_please(&inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        cache_destroy_please(&inst->inst_dncache, CACHE_TYPE_DN);
    }
    slapi_ch_free((void **)&instance_name);

    return SLAPI_DSE_CALLBACK_OK;
}

