/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Dynamic Lists plug-in
 */
#include "dynamic_lists.h"

/*
 * Plug-in globals
 */
static void *_PluginID = NULL;
static DynamicListsConfig listConfig = {0};
static Slapi_PluginDesc pdesc = { DYNAMIC_LISTS_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  DYNAMIC_LISTS_PLUGIN_DESC };

/*
 * Plug-in management functions
 */
int dynamic_lists_init(Slapi_PBlock *pb);
static int dynamic_lists_start(Slapi_PBlock *pb);
static int dynamic_lists_close(Slapi_PBlock *pb);
static int dynamic_lists_pre_entry(Slapi_PBlock *pb);

/*
 * Plugin identity functions
 */
void
dynamic_lists_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
dynamic_lists_get_plugin_id(void)
{
    return _PluginID;
}

/*
 * Plug-in initialization functions
 */
int
dynamic_lists_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "--> dynamic_lists_init\n");

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    dynamic_lists_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)dynamic_lists_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)dynamic_lists_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ENTRY_FN,
                         (void *)dynamic_lists_pre_entry) != 0)
    {
        slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                      "dynamic_lists_init - Failed to register plugin\n");
        status = -1;
    }

    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "<-- dynamic_lists_init\n");
    return status;
}


/*
 * dynamic_lists_start()
 *
 * Creates config lock and loads config cache.
 */
static int
dynamic_lists_start(Slapi_PBlock *pb)
{
    int rc = 0;

    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "--> dynamic_lists_start\n");

    /* Load the config - config only reloaded at startup */
    rc = dynamic_lists_load_config(pb, &listConfig);

    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "<-- dynamic_lists_start (rc %d)\n", rc);

    return rc;
}

/*
 * dynamic_lists_close()
 *
 */
static int
dynamic_lists_close(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "--> dynamic_lists_close\n");

    /* Free the config values */
    slapi_ch_free_string(&listConfig.oc);
    slapi_ch_free_string(&listConfig.URLAttr);
    slapi_ch_free_string(&listConfig.attr);

    slapi_log_err(SLAPI_LOG_TRACE, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                  "<-- dynamic_lists_close\n");

    return 0;
}

/*
  See if the client has the requested rights over the entry and the specified
  attributes.  Each attribute in attrs will be tested.  The retattrs array will
  hold the attributes that could be read.  If NULL, this means the entry is
  not allowed, or none of the requested attributes are allowed.  If non-NULL, this
  array can be passed to a subsequent search operation.
*/
static bool
dynamic_lists_check_access(Slapi_PBlock *pb, Slapi_Entry *entry, const char *attr)
{
    if (slapi_access_allowed(pb, entry, (char *)attr, NULL, SLAPI_ACL_READ) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_PLUGIN, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                      "dynamic_lists_check_access - "
                      "The client does not have permission to read attribute %s in entry %s\n",
                      attr, slapi_entry_get_dn_const(entry));
        return false;
    }

    return true;
}


/*
 * dynamic_lists_pre_entry()
 *
 */
static int
dynamic_lists_pre_entry(Slapi_PBlock *pb)
{
    Slapi_PBlock *search_pb = NULL;
    LDAPURLDesc *ludp = NULL;
    Slapi_Value *oc_value = NULL;
    const Slapi_Value **urls = NULL;
    Slapi_Entry *list_entry = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry *dup_entry = NULL;
    const char *url = NULL;
    char *list_attr = NULL;
    int secure = 0;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_ORIG, &list_entry);
    slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_COPY, &dup_entry);

    if (slapi_op_internal(pb)) {
        /* We do not build dynamic content for internal operations*/
        return rc;
    }

    /*
     * Check if the entry has the configured dynamic list objectclass
     */
    oc_value = slapi_value_new_string(listConfig.oc);
    if (slapi_entry_attr_has_syntax_value(list_entry, "objectclass", oc_value) == 0) {
        /* Entry does not have the groupOfUrls objectclass */
        slapi_value_free(&oc_value);
        return rc;
    }
    slapi_value_free(&oc_value);


    /* The entry could have multiple urls, so we need to loop here */
    urls = slapi_entry_attr_get_valuearray(list_entry, listConfig.URLAttr);
    for (size_t i = 0; urls && urls[i]; i++) {
        Slapi_ValueSet *list_set = NULL;
        url = slapi_value_get_string(urls[i]);
        /*
        * Parse the LDAP URL and validate it
        */
        if ((rc = slapi_ldap_url_parse(url, &ludp, 0, &secure)) != 0) {
            /* Failed to parse the memberUrl attribute */
            slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                          "dynamic_lists_pre_entry - %s - failed to parse the LDAP url: %s\n",
                          slapi_entry_get_dn_const(list_entry), url);
            goto cont;
        }

        if (ludp->lud_filter == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                          "dynamic_lists_pre_entry - %s - Missing filter in LDAP URL %s\n",
                          slapi_entry_get_dn_const(list_entry), url);
            goto cont;
        }

        if (ludp->lud_dn == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                          "dynamic_lists_pre_entry - %s - Missing base dn in LDAP URL %s\n",
                          slapi_entry_get_dn_const(list_entry), url);
            goto cont;
        }

        if (ludp->lud_attrs != NULL && ludp->lud_attrs[0] != NULL) {
            /* If an attribute is specified use it */
            list_attr = ludp->lud_attrs[0];
        }

        /*
        * Check if the client has permission to read the list attribute
        */
        if (dynamic_lists_check_access(pb, list_entry,
                                       list_attr ? list_attr : listConfig.attr) == false)
        {
            /* Access denied, so we can't proceed */
            goto cont;
        }

        slapi_log_err(SLAPI_LOG_PLUGIN, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                      "dynamic_lists_pre_entry - %s (url: %s) - Performing internal search\n",
                      slapi_entry_get_dn_const(list_entry), url);

        /*
        * Get our DN's groupOfUrls attribute
        */
        search_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(search_pb, ludp->lud_dn, ludp->lud_scope, ludp->lud_filter,
                                     NULL, 0, NULL, NULL, dynamic_lists_get_plugin_id(), 0);
        slapi_search_internal_pb(search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (LDAP_SUCCESS != rc) {
            /* log an error and use the plugin entry for the config */
            slapi_log_err(SLAPI_LOG_ERR, DYNAMIC_LISTS_PLUGIN_SUBSYSTEM,
                          "dynamic_lists_pre_entry - "
                          "%s - Internal search based on LDAP url (%s) failed: err=%d\n",
                          slapi_entry_get_dn_const(list_entry), url, rc);
            goto cont;
        } else {
            list_set = slapi_valueset_new();
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            for (size_t i = 0; entries && entries[i]; i++) {
                if (list_attr == NULL) {
                    /* No list attribute, so use the DN of the entry */
                    const char *dn = slapi_entry_get_dn_const(entries[i]);
                    slapi_valueset_add_value_ext(list_set, slapi_value_new_string(dn),
                                                 SLAPI_VALUE_FLAG_PASSIN);
                } else {
                    /* Use an attribute/value from within the entry */
                    const char *entry_list_attr = slapi_entry_attr_get_ref(entries[i],
                                                                           list_attr);
                    if (entry_list_attr) {
                        slapi_valueset_add_value_ext(list_set,
                                                     slapi_value_new_string(entry_list_attr),
                                                     SLAPI_VALUE_FLAG_PASSIN);
                    }
                }
            }
        }
        if (slapi_valueset_isempty(list_set) == 0) {
            if (dup_entry == NULL) {
                dup_entry = slapi_entry_dup(list_entry);
            }
            slapi_entry_add_valueset(dup_entry,
                                     list_attr ? list_attr : listConfig.attr,
                                     list_set);
        }

cont:
        slapi_valueset_free(list_set);
        slapi_free_search_results_internal(search_pb);
        slapi_pblock_destroy(search_pb);
        if (ludp != NULL) {
            ldap_free_urldesc(ludp);
        }
    } /* end of URL loop */

    slapi_pblock_set(pb, SLAPI_SEARCH_ENTRY_COPY, dup_entry);

    return 0;
}
