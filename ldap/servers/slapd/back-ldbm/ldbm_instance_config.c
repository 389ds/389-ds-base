/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* This file handles configuration information that is specific
 * to ldbm instances.
 */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations for the callbacks */
int ldbm_instance_search_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
int ldbm_instance_modify_config_entry_callback(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);

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

        ""};


/*------------------------------------------------------------------------
 * Get and set functions for ldbm instance variables
 *----------------------------------------------------------------------*/
static void *
ldbm_instance_config_cachesize_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)cache_get_max_entries(&(inst->inst_cache));
}

static int
ldbm_instance_config_cachesize_set(void *arg,
                                   void *value,
                                   char *errorbuf __attribute__((unused)),
                                   int phase,
                                   int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    int retval = LDAP_SUCCESS;
    long val = (long)value;

    /* Do whatever we can to make sure the data is ok. */

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            if (val > 0 && inst->inst_li->li_cache_autosize) {
                /* We are auto-tuning the cache, so this change would be overwritten - return an error */
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                      "Error: \"nsslapd-cachesize\" can not be updated while \"nsslapd-cache-autosize\" is set "
                                      "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".");
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_cachesize_set",
                              "\"nsslapd-cachesize\" can not be set while \"nsslapd-cache-autosize\" is set "
                              "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".\n");
                return LDAP_UNWILLING_TO_PERFORM;
            }
        }
        cache_set_max_entries(&(inst->inst_cache), val);
    }

    return retval;
}

static void *
ldbm_instance_config_cachememsize_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)cache_get_max_size(&(inst->inst_cache));
}

static int
ldbm_instance_config_cachememsize_set(void *arg,
                                      void *value,
                                      char *errorbuf,
                                      int phase,
                                      int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);
    uint64_t delta = 0;
    uint64_t delta_original = 0;

    /* Do whatever we can to make sure the data is ok. */
    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */

    if (apply) {
        if (CONFIG_PHASE_RUNNING == phase) {
            if (val > 0 && inst->inst_li->li_cache_autosize) {
                /* We are auto-tuning the cache, so this change would be overwritten - return an error */
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                      "Error: \"nsslapd-cachememsize\" can not be updated while \"nsslapd-cache-autosize\" is set "
                                      "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".");
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_cachememsize_set",
                              "\"nsslapd-cachememsize\" can not be set while \"nsslapd-cache-autosize\" is set "
                              "in \"cn=config,cn=ldbm database,cn=plugins,cn=config\".\n");
                return LDAP_UNWILLING_TO_PERFORM;
            }
        }
        if (val > inst->inst_cache.c_maxsize) {
            delta = val - inst->inst_cache.c_maxsize;
            delta_original = delta;

            util_cachesize_result sane;
            slapi_pal_meminfo *mi = spal_meminfo_get();
            sane = util_is_cachesize_sane(mi, &delta);
            spal_meminfo_destroy(mi);

            if (sane == UTIL_CACHESIZE_ERROR) {
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE, "Error: unable to determine system memory limits.");
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_cachememsize_set",
                              "Enable to determine system memory limits.\n");
                return LDAP_UNWILLING_TO_PERFORM;
            } else if (sane == UTIL_CACHESIZE_REDUCED) {
                slapi_log_err(SLAPI_LOG_WARNING, "ldbm_instance_config_cachememsize_set",
                              "delta +%" PRIu64 " of request %" PRIu64 " reduced to %" PRIu64 "\n", delta_original, val, delta);
                /*
                 * This works as: value = 100
                 * delta_original to inst, 20;
                 * delta reduced to 5:
                 * 100 - (20 - 5) == 85;
                 * so if you recalculated delta now (val - inst), it would be 5.
                 */
                val = val - (delta_original - delta);
            }
        }
        if (inst->inst_cache.c_maxsize < MINCACHESIZE || val < MINCACHESIZE) {
            slapi_log_err(SLAPI_LOG_INFO, "ldbm_instance_config_cachememsize_set",
                          "force a minimal value %" PRIu64 "\n", MINCACHESIZE);
            /* This value will trigger an autotune next start up, but it should increase only */
            val = MINCACHESIZE;
        }
        cache_set_max_size(&(inst->inst_cache), val, CACHE_TYPE_ENTRY);
    }

    return retval;
}

static void *
ldbm_instance_config_dncachememsize_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)cache_get_max_size(&(inst->inst_dncache));
}

static int
ldbm_instance_config_dncachememsize_set(void *arg,
                                        void *value,
                                        char *errorbuf,
                                        int phase __attribute__((unused)),
                                        int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    int retval = LDAP_SUCCESS;
    uint64_t val = (uint64_t)((uintptr_t)value);
    uint64_t delta = 0;

    /* Do whatever we can to make sure the data is ok. */
    /* There is an error here. We check the new val against our current mem-alloc
     * Issue is that we already are using system pages, so while our value *might*
     * be valid, we may reject it here due to the current procs page usage.
     *
     * So how do we solve this? If we are setting a SMALLER value than we
     * currently have ALLOW it, because we already passed the cache sanity.
     * If we are setting a LARGER value, we check the delta of the two, and make
     * sure that it is sane.
     */

    if (apply) {
        if (val > inst->inst_dncache.c_maxsize) {
            delta = val - inst->inst_dncache.c_maxsize;

            util_cachesize_result sane;
            slapi_pal_meminfo *mi = spal_meminfo_get();
            sane = util_is_cachesize_sane(mi, &delta);
            spal_meminfo_destroy(mi);

            if (sane != UTIL_CACHESIZE_VALID) {
                slapi_create_errormsg(errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
                                      "Error: dncachememsize value is too large.");
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_dncachememsize_set",
                              "dncachememsize value is too large.\n");
                return LDAP_UNWILLING_TO_PERFORM;
            }
        }
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
ldbm_instance_config_require_index_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)((uintptr_t)inst->require_index);
}

static void *
ldbm_instance_config_require_internalop_index_get(void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    return (void *)((uintptr_t)inst->require_internalop_index);
}

static int
ldbm_instance_config_readonly_set(void *arg,
                                  void *value,
                                  char *errorbuf __attribute__((unused)),
                                  int phase,
                                  int apply)
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
        if (!(inst->inst_flags & INST_FLAG_BUSY)) {
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
ldbm_instance_config_require_index_set(void *arg,
                                       void *value,
                                       char *errorbuf __attribute__((unused)),
                                       int phase __attribute__((unused)),
                                       int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    inst->require_index = (int)((uintptr_t)value);

    return LDAP_SUCCESS;
}


static int
ldbm_instance_config_require_internalop_index_set(void *arg,
                                                  void *value,
                                                  char *errorbuf __attribute__((unused)),
                                                  int phase __attribute__((unused)),
                                                  int apply)
{
    ldbm_instance *inst = (ldbm_instance *)arg;

    if (!apply) {
        return LDAP_SUCCESS;
    }

    inst->require_internalop_index = (int)((uintptr_t)value);

    return LDAP_SUCCESS;
}

/*------------------------------------------------------------------------
 * ldbm instance configuration array
 *----------------------------------------------------------------------*/
static config_info ldbm_instance_config[] = {
    {CONFIG_INSTANCE_CACHESIZE, CONFIG_TYPE_LONG, "-1", &ldbm_instance_config_cachesize_get, &ldbm_instance_config_cachesize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_CACHEMEMSIZE, CONFIG_TYPE_UINT64, DEFAULT_CACHE_SIZE_STR, &ldbm_instance_config_cachememsize_get, &ldbm_instance_config_cachememsize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_READONLY, CONFIG_TYPE_ONOFF, "off", &ldbm_instance_config_readonly_get, &ldbm_instance_config_readonly_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_REQUIRE_INDEX, CONFIG_TYPE_ONOFF, "off", &ldbm_instance_config_require_index_get, &ldbm_instance_config_require_index_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
	{CONFIG_INSTANCE_REQUIRE_INTERNALOP_INDEX, CONFIG_TYPE_ONOFF, "off", &ldbm_instance_config_require_internalop_index_get, &ldbm_instance_config_require_internalop_index_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {CONFIG_INSTANCE_DNCACHEMEMSIZE, CONFIG_TYPE_UINT64, DEFAULT_DNCACHE_SIZE_STR, &ldbm_instance_config_dncachememsize_get, &ldbm_instance_config_dncachememsize_set, CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_ALLOW_RUNNING_CHANGE},
    {NULL, 0, NULL, NULL, NULL, 0}};

void
ldbm_instance_config_setup_default(ldbm_instance *inst)
{
    config_info *config;

    for (config = ldbm_instance_config; config->config_name != NULL; config++) {
        ldbm_config_set((void *)inst, config->config_name, ldbm_instance_config, NULL /* use default */, NULL, CONFIG_PHASE_INITIALIZATION, 1 /* apply */, LDAP_MOD_REPLACE);
    }
}


/* Returns LDAP_SUCCESS on success */
int
ldbm_instance_config_set(ldbm_instance *inst, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op)
{
    config_info *config;
    int rc = LDAP_SUCCESS;

    config = config_info_get(config_array, attr_name);
    if (NULL == config) {
        struct ldbminfo *li = inst->inst_li;
        dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
        slapi_log_err(SLAPI_LOG_CONFIG, "ldbm_instance_config_set", "Unknown config attribute %s check db specific layer\n", attr_name);
        slapi_create_errormsg(err_buf, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown config attribute %s check db specific layer\n", attr_name);
        rc = priv->instance_config_set_fn(inst, attr_name, apply_mod, mod_op, phase, bval);
    } else {
        rc = ldbm_config_set(inst, attr_name, config_array, bval, err_buf,phase, apply_mod, mod_op);
    }

    return rc;
}

void
ldbm_instance_config_get(ldbm_instance *inst, config_info *config, char *buf)
{
    void *val = NULL;

    if (config == NULL) {
        buf[0] = '\0';
        return;
    }

    val = config->config_get_fn((void *)inst);
    config_info_print_val(val, config->config_type, buf);

    if (config->config_type == CONFIG_TYPE_STRING) {
        slapi_ch_free((void **)&val);
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
            bval = (struct berval *)slapi_value_get_berval(sval);
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
        slapi_log_err(SLAPI_LOG_ERR,
                      "read_instance_index_entries",
                      "Failed create index dn for plugin %s, instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return 1;
    }

    /* Set up a tmp callback that will handle the init for each index entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
                                   basedn, scope, searchfilter, ldbm_index_init_entry_callback,
                                   (void *)inst);

    /* Do a search of the subtree containing the index entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE,
                                 searchfilter, NULL, 0, NULL, NULL, inst->inst_li->li_identity, 0);
    slapi_search_internal_pb(tmp_pb);

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
        slapi_log_err(SLAPI_LOG_ERR,
                      "read_instance_attrcrypt_entries",
                      "Failed create encrypted attributes dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        return 1;
    }

    /* Set up a tmp callback that will handle the init for each index entry */
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
                                   basedn, scope, searchfilter, ldbm_attrcrypt_init_entry_callback,
                                   (void *)inst);

    /* Do a search of the subtree containing the index entries */
    tmp_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(tmp_pb, basedn, LDAP_SCOPE_SUBTREE,
                                 searchfilter, NULL, 0, NULL, NULL, inst->inst_li->li_identity, 0);
    slapi_search_internal_pb(tmp_pb);

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
        char err_buf[SLAPI_DSE_RETURNTEXT_SIZE];

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

            bval = (struct berval *)slapi_value_get_berval(sval);
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
        bval = (struct berval *)slapi_value_get_berval(sval);

        if (ldbm_instance_config_set((void *)inst, attr_name, config_array, bval,
                            err_buf, CONFIG_PHASE_STARTUP, 1 /* apply */, LDAP_MOD_REPLACE) != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_ERR, "parse_ldbm_instance_config_entry",
                          "Error with config attribute %s : %s\n",
                          attr_name, err_buf);
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
static int
ldbm_instance_deny_config(Slapi_PBlock *pb __attribute__((unused)),
                          Slapi_Entry *e __attribute__((unused)),
                          Slapi_Entry *entryAfter __attribute__((unused)),
                          int *returncode,
                          char *returntext __attribute__((unused)),
                          void *arg __attribute__((unused)))
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

void
ldbm_instance_register_modify_callback(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    dn = slapi_create_dn_string("cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)",
                                   ldbm_instance_modify_config_entry_callback, (void *)inst);
    slapi_ch_free_string(&dn);
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
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_config_load_dse_info",
                      "Failed create instance dn %s for plugin %s\n",
                      inst->inst_name, inst->inst_li->li_plugin->plg_name);
        rval = 1;
        goto bail;
    }

    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_load_dse_info", "Out of memory\n");
        rval = 1;
        goto bail;
    }

    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_BASE,
                                 "objectclass=*", NULL, 0, NULL, NULL,
                                 li->li_identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);

    if (rval != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_load_dse_info",
                      "Error accessing the config DSE entry (%s), error %d\n", dn, rval);
        rval = 1;
        goto bail;
    } else {
        /* Need to parse the configuration information for the ldbm
         * plugin that is held in the DSE. */
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);
        if ((!entries) || (!entries[0])) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_load_dse_info",
                          "No entries found in config DSE entry (%s)\n", dn);
            rval = 1;
            goto bail;
        }
        if (0 != parse_ldbm_instance_config_entry(inst, entries[0],
                                                  ldbm_instance_config)) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_config_load_dse_info",
                          "Error parsing the config DSE\n");
            rval = 1;
            goto bail;
        }
    }

    if (search_pb) {
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
                                   ldbm_instance_search_config_entry_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)",
                                   ldbm_instance_modify_config_entry_callback, (void *)inst);
    slapi_config_register_callback(DSE_OPERATION_WRITE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)",
                                   ldbm_instance_search_config_entry_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_BASE, "(objectclass=*)",
                                   ldbm_instance_deny_config, (void *)inst);
    /* delete is handled by a callback set in ldbm_config.c */
    slapi_ch_free_string(&dn);

    /* Callbacks to handle indexes */
    dn = slapi_create_dn_string("cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_config_load_dse_info",
                      "failed create index instance dn for plugin %s, "
                      "instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
                                   ldbm_instance_index_config_add_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
                                   ldbm_instance_index_config_delete_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, "(objectclass=nsIndex)",
                                   ldbm_instance_index_config_modify_callback, (void *)inst);
    slapi_ch_free_string(&dn);

    /* Callbacks to handle attribute encryption */
    dn = slapi_create_dn_string("cn=encrypted attributes,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_config_load_dse_info",
                      "failed create encrypted attribute instance dn "
                      "for plugin %s, instance %s\n",
                      inst->inst_li->li_plugin->plg_name, inst->inst_name);
        rval = 1;
        goto bail;
    }
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
                                   ldbm_instance_attrcrypt_config_add_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
                                   ldbm_instance_attrcrypt_config_delete_callback, (void *)inst);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, dn,
                                   LDAP_SCOPE_SUBTREE, ldbm_instance_attrcrypt_filter,
                                   ldbm_instance_attrcrypt_config_modify_callback, (void *)inst);
    rval = 0;
bail:
    slapi_ch_free_string(&dn);
    return rval;
}

/*
 * Config. DSE callback for instance entry searches.
 */
int
ldbm_instance_search_config_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                           Slapi_Entry *e,
                                           Slapi_Entry *entryAfter __attribute__((unused)),
                                           int *returncode,
                                           char *returntext,
                                           void *arg)
{
    char buf[BUFSIZ];
    struct berval *vals[2];
    struct berval val;
    ldbm_instance *inst = (ldbm_instance *)arg;
    struct ldbminfo *li = inst->inst_li;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    config_info *config;
    const Slapi_DN *suffix;

    vals[0] = &val;
    vals[1] = NULL;
    returntext[0] = '\0';

    /* show the suffixes */
    attrlist_delete(&e->e_attrs, CONFIG_INSTANCE_SUFFIX);
    suffix = slapi_be_getsuffix(inst->inst_be);
    if (suffix != NULL) {
        val.bv_val = (char *)slapi_sdn_get_dn(suffix);
        val.bv_len = strlen(val.bv_val);
        attrlist_merge(&e->e_attrs, CONFIG_INSTANCE_SUFFIX, vals);
    }

    PR_Lock(inst->inst_config_mutex);

    for (config = ldbm_instance_config; config->config_name != NULL; config++) {
        /* Go through the ldbm_config table and fill in the entry. */

        if (!(config->config_flags & (CONFIG_FLAG_ALWAYS_SHOW | CONFIG_FLAG_PREVIOUSLY_SET))) {
            /* This config option shouldn't be shown */
            continue;
        }

        ldbm_config_get((void *)inst, config, buf);

        val.bv_val = buf;
        val.bv_len = strlen(buf);
        slapi_entry_attr_replace(e, config->config_name, vals);
    }

    /* NOTE (LK): need to extend with db specific attrs */
    priv->instance_search_callback_fn(e, returncode, returntext, inst);

    PR_Unlock(inst->inst_config_mutex);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

/*
 * Config. DSE callback for instance entry modifies.
 */
int
ldbm_instance_modify_config_entry_callback(Slapi_PBlock *pb,
                                           Slapi_Entry *entryBefore __attribute__((unused)),
                                           Slapi_Entry *e __attribute__((unused)),
                                           int *returncode,
                                           char *returntext,
                                           void *arg)
{
    int i;
    char *attr_name;
    LDAPMod **mods;
    int rc = LDAP_SUCCESS;
    int apply_mod = 0;
    ldbm_instance *inst = (ldbm_instance *)arg;

    /* This lock is probably way too conservative, but we don't expect much
     * contention for it. */
    PR_Lock(inst->inst_config_mutex);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    if (!returntext) {
        rc = LDAP_OPERATIONS_ERROR;
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_modify_config_entry_callback",
                      "NULL return text\n");
        goto out;
    }

    returntext[0] = '\0';

    /*
     * First pass: set apply mods to 0 so only input validation will be done;
     * 2nd pass: set apply mods to 1 to apply changes to internal storage
     */
    for (apply_mod = 0; apply_mod <= 1 && LDAP_SUCCESS == rc; apply_mod++) {
        for (i = 0; mods && mods[i] && LDAP_SUCCESS == rc; i++) {
            attr_name = mods[i]->mod_type;

            if (strcasecmp(attr_name, CONFIG_INSTANCE_SUFFIX) == 0) {
                /* naughty naughty, we don't allow this */
                rc = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "Can't change the root suffix of a backend");
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_modify_config_entry_callback",
                              "Modify attempted to change the root suffix of a backend (which is not allowed)\n");
                continue;
            }

            /* There are some attributes that we don't care about, like
             * modifiersname. */
            if (ldbm_config_ignored_attr(attr_name)) {
                continue;
            }

            /* This assumes there is only one bval for this mod. */
            if (mods[i]->mod_bvalues == NULL) {
                /* This avoids the null pointer deref.
                 * In ldbm_config.c ldbm_config_set, it checks for the NULL.
                 * If it's a NULL, we get NO_SUCH_ATTRIBUTE error.
                 */
                rc = ldbm_config_set((void *)inst, attr_name,
                                     ldbm_instance_config, NULL, returntext,
                                     CONFIG_PHASE_RUNNING, apply_mod, mods[i]->mod_op);
            } else {
                rc = ldbm_config_set((void *)inst, attr_name,
                                     ldbm_instance_config, mods[i]->mod_bvalues[0], returntext,
                                     CONFIG_PHASE_RUNNING, apply_mod, mods[i]->mod_op);
            }
        }
    }

out:
    PR_Unlock(inst->inst_config_mutex);

    *returncode = rc;
    if (LDAP_SUCCESS == rc) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

static int
ldbm_instance_generate(struct ldbminfo *li, char *instance_name, Slapi_Backend **ret_be)
{
    Slapi_Backend *new_be = NULL;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    int rc = 0;

    /* Create a new instance, process config info for it,
     * and then call slapi_be_new and create a new backend here
     */
    new_be = slapi_be_new(LDBM_DATABASE_TYPE_NAME /* type */, instance_name,
                          0 /* public */, 1 /* do log changes */);
    new_be->be_database = li->li_plugin;
    rc = ldbm_instance_create(new_be, instance_name);
    if (rc) {
        goto bail;
    }

    ldbm_instance_config_load_dse_info(new_be->be_instance_info);
    priv->instance_register_monitor_fn(new_be->be_instance_info);

    ldbm_instance_create_default_indexes(new_be);

    /* if USN plugin is enabled, set slapi_counter */
    if (plugin_enabled("USN", li->li_identity) && ldbm_back_isinitialized()) {
        /*
         * ldbm_back is already initialized.
         * I.e., a new instance is being added.
         * If not initialized, ldbm_usn_init is called later and
         * be usn counter is initialized there.
         */
        if (config_get_entryusn_global()) {
            /* global usn counter is already created.
             * set it to be_usn_counter. */
            new_be->be_usn_counter = li->li_global_usn_counter;
        } else {
            new_be->be_usn_counter = slapi_counter_new();
            slapi_counter_set_value(new_be->be_usn_counter, INITIALUSN);
        }
    }

    if (ret_be != NULL) {
        *ret_be = new_be;
    }
bail:
    return rc;
}

/* unregister the DSE callbacks on a backend -- this needs to be done when
 * deleting a backend, so that adding the same backend later won't cause
 * these expired callbacks to be called.
 */
static void
ldbm_instance_unregister_callbacks(ldbm_instance *inst)
{
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;

    /* tear down callbacks for the instance config entry */
    dn = slapi_create_dn_string("cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_unregister_callbacks",
                      "Failed create instance dn for plugin %s, "
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


    /* now the cn=index entries */
    dn = slapi_create_dn_string("cn=index,cn=%s,cn=%s,cn=plugins,cn=config",
                                inst->inst_name, li->li_plugin->plg_name);
    if (NULL == dn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_unregister_callbacks",
                      "Failed create index dn for plugin %s, "
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
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_unregister_callbacks",
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
ldbm_instance_postadd_instance_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                              Slapi_Entry *entryBefore,
                                              Slapi_Entry *entryAfter __attribute__((unused)),
                                              int *returncode __attribute__((unused)),
                                              char *returntext __attribute__((unused)),
                                              void *arg)
{
    backend *be = NULL;
    struct ldbm_instance *inst;
    char *instance_name;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dblayer_private *priv = NULL;
    int rval = 0;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    rval = ldbm_instance_generate(li, instance_name, &be);

    inst = ldbm_instance_find_by_name(li, instance_name);

    /* Add default indexes */
    ldbm_instance_create_default_user_indexes(inst);

    /* Initialize and register callbacks for VLV indexes */
    vlv_init(inst);

    /* this is an ACTUAL ADD being done while the server is running!
     * start up the appropriate backend...
     */
    rval = ldbm_instance_start(be);
    if (0 != rval) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_postadd_instance_entry_callback",
                      "ldbm_instnace_start (%s) failed (%d)\n",
                      instance_name, rval);
    }


    /* call the backend implementation specific callbacks */
    priv = (dblayer_private *)li->li_dblayer_private;
    priv->instance_postadd_config_fn(li, inst);

    slapi_ch_free((void **)&instance_name);

    /* instance must be fully ready before we call this */
    slapi_mtn_be_started(be);

    return SLAPI_DSE_CALLBACK_OK;
}

int
ldbm_instance_add_instance_entry_callback(Slapi_PBlock *pb,
                                          Slapi_Entry *entryBefore,
                                          Slapi_Entry *entryAfter __attribute__((unused)),
                                          int *returncode,
                                          char *returntext,
                                          void *arg)
{
    char *instance_name;
    struct ldbm_instance *inst = NULL;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    dblayer_private *priv = NULL;
    int rc = 0;

    parse_ldbm_instance_entry(entryBefore, &instance_name);

    /* Make sure we don't create two instances with the same name. */
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (inst != NULL) {
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_instance_add_instance_entry_callback",
                      "ldbm instance %s already exists\n", instance_name);
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


    /* call the backend implementation specific callbacks */
    priv = (dblayer_private *)li->li_dblayer_private;
    priv->instance_add_config_fn(li, inst);

    slapi_ch_free((void **)&instance_name);
    return (rc == 0) ? SLAPI_DSE_CALLBACK_OK : SLAPI_DSE_CALLBACK_ERROR;
}



int
ldbm_instance_post_delete_instance_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                                  Slapi_Entry *entryBefore,
                                                  Slapi_Entry *entryAfter __attribute__((unused)),
                                                  int *returncode,
                                                  char *returntext,
                                                  void *arg)
{
    char *instance_name;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct ldbm_instance *inst = NULL;
    dblayer_private *priv = NULL;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    inst = ldbm_instance_find_by_name(li, instance_name);

    if (inst == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_post_delete_instance_entry_callback",
                      "Instance '%s' does not exist!\n", instance_name);
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

    slapi_log_err(SLAPI_LOG_INFO, "ldbm_instance_post_delete_instance_entry_callback",
                  "Removing '%s'.\n", instance_name);

    cache_destroy_please(&inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        cache_destroy_please(&inst->inst_dncache, CACHE_TYPE_DN);
    }
    /* call the backend implementation specific callbacks */
    priv = (dblayer_private *)li->li_dblayer_private;
    priv->instance_postdel_config_fn(li, inst);

    ldbm_instance_unregister_callbacks(inst);
    vlv_close(inst);
    slapi_be_free(&inst->inst_be);
    ldbm_instance_destroy(inst);
    slapi_ch_free((void **)&instance_name);

    return SLAPI_DSE_CALLBACK_OK;
}

int
ldbm_instance_delete_instance_entry_callback(Slapi_PBlock *pb __attribute__((unused)),
                                             Slapi_Entry *entryBefore,
                                             Slapi_Entry *entryAfter __attribute__((unused)),
                                             int *returncode,
                                             char *returntext,
                                             void *arg)
{
    char *instance_name = NULL;
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct ldbm_instance *inst = NULL;
    dblayer_private *priv = NULL;

    parse_ldbm_instance_entry(entryBefore, &instance_name);
    inst = ldbm_instance_find_by_name(li, instance_name);
    if (inst == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_delete_instance_entry_callback",
                      "Instance '%s' does not exist!\n", instance_name);
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
    if ((instance_set_busy(inst) != 0) ||
        (slapi_counter_get_value(inst->inst_ref_count) > 0)) {
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_instance_delete_instance_entry_callback",
                      "'%s' is in the middle of a task. Cancel the task or wait for it to finish, "
                      "then try again.\n",
                      instance_name);
        if (returntext) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "ldbm instance '%s' is in the middle of a "
                                                               "task. Cancel the task or wait for it to finish, "
                                                               "then try again.\n",
                        instance_name);
        }
        if (returncode) {
            *returncode = LDAP_UNWILLING_TO_PERFORM;
        }
        slapi_ch_free((void **)&instance_name);
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* okay, we're gonna delete this database instance.  take it offline. */
    slapi_log_err(SLAPI_LOG_INFO, "ldbm_instance_delete_instance_entry_callback",
                  "Bringing %s offline...\n", instance_name);
    slapi_mtn_be_stopping(inst->inst_be);

    /* call the backend implementation specific callbacks */
    priv = (dblayer_private *)li->li_dblayer_private;
    priv->instance_del_config_fn(li, inst);

    dblayer_instance_close(inst->inst_be);
    slapi_ch_free((void **)&instance_name);

    return SLAPI_DSE_CALLBACK_OK;
}
