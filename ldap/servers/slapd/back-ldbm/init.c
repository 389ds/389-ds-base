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

/* init.c - initialize ldbm backend */

#include "back-ldbm.h"
#include "../slapi-plugin.h"

static Slapi_PluginDesc pdesc = {"ldbm-backend", VENDOR,
                                 DS_PACKAGE_VERSION, "high-performance LDAP backend database plugin"};

/* pb: not used */
int
ldbm_back_add_schema(Slapi_PBlock *pb __attribute__((unused)))
{
    int rc = 0;
    rc = slapi_add_internal_attr_syntax(LDBM_ENTRYDN_STR,
                                        LDBM_ENTRYDN_OID, DN_SYNTAX_OID, DNMATCH_NAME,
                                        SLAPI_ATTR_FLAG_SINGLE | SLAPI_ATTR_FLAG_NOUSERMOD);

    rc |= slapi_add_internal_attr_syntax("dncomp",
                                         LDBM_DNCOMP_OID, DN_SYNTAX_OID, DNMATCH_NAME,
                                         SLAPI_ATTR_FLAG_NOUSERMOD);

    rc |= slapi_add_internal_attr_syntax(LDBM_PARENTID_STR,
                                         LDBM_PARENTID_OID, INTEGER_SYNTAX_OID, INTEGERMATCH_NAME,
                                         SLAPI_ATTR_FLAG_SINGLE | SLAPI_ATTR_FLAG_NOUSERMOD);

    rc |= slapi_add_internal_attr_syntax("entryid",
                                         LDBM_ENTRYID_OID, DIRSTRING_SYNTAX_OID, CASEIGNOREMATCH_NAME,
                                         SLAPI_ATTR_FLAG_SINGLE | SLAPI_ATTR_FLAG_NOUSERMOD);

    return rc;
}

int
ldbm_back_init(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    int rc;
    struct slapdplugin *p;

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_init", "=>\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN, &p);

    /* allocate backend-specific stuff */
    li = (struct ldbminfo *)slapi_ch_calloc(1, sizeof(struct ldbminfo));

    /* Record the identity of the ldbm plugin.  The plugin
     * identity is used during internal ops. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &(li->li_identity));

    /* keep a pointer back to the plugin */
    li->li_plugin = p;

    /* set shutdown flag to zero.*/
    li->li_shutdown = 0;

    /* Initialize the set of instances. */
    li->li_instance_set = objset_new(&ldbm_back_instance_set_destructor);

    /* Init lock threshold value */
    li->li_dblock_threshold_reached = 0;

    /* ask the factory to give us space in the Connection object
         * (only bulk import uses this)
         */
    if (slapi_register_object_extension(p->plg_name, SLAPI_EXT_CONNECTION,
                                        factory_constructor, factory_destructor,
                                        &li->li_bulk_import_object, &li->li_bulk_import_handle) != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init",
                      "slapi_register_object_extension failed.\n");
        goto fail;
    }

    /* add some private attributes */
    rc = ldbm_back_add_schema(pb);

    /* set plugin private pointer and initialize locks, etc. */
    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, (void *)li);

    if ((li->li_shutdown_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init", "PR_NewLock failed\n");
        goto fail;
    }

    if ((li->li_config_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init", "PR_NewLock failed\n");
        goto fail;
    }

    /* set all of the necessary database plugin callback functions */
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                           (void *)SLAPI_PLUGIN_VERSION_03);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_BIND_FN,
                           (void *)ldbm_back_bind);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_UNBIND_FN,
                           (void *)ldbm_back_unbind);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SEARCH_FN,
                           (void *)ldbm_back_search);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN,
                           (void *)ldbm_back_next_search_entry);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN,
                           (void *)ldbm_back_next_search_entry_ext);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN,
                           (void *)ldbm_back_prev_search_results);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN,
                           (void *)ldbm_back_entry_release);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN,
                           (void *)ldbm_back_search_results_release);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_COMPARE_FN,
                           (void *)ldbm_back_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_MODIFY_FN,
                           (void *)ldbm_back_modify);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_MODRDN_FN,
                           (void *)ldbm_back_modrdn);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ADD_FN,
                           (void *)ldbm_back_add);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DELETE_FN,
                           (void *)ldbm_back_delete);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ABANDON_FN,
                           (void *)ldbm_back_abandon);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                           (void *)ldbm_back_close);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_CLEANUP_FN,
                           (void *)ldbm_back_cleanup);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                           (void *)ldbm_back_start);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SEQ_FN,
                           (void *)ldbm_back_seq);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_RMDB_FN,
                           (void *)ldbm_back_rmdb);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_LDIF2DB_FN,
                           (void *)ldbm_back_ldif2ldbm);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DB2LDIF_FN,
                           (void *)ldbm_back_ldbm2ldif);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DB2INDEX_FN,
                           (void *)ldbm_back_ldbm2index);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ARCHIVE2DB_FN,
                           (void *)ldbm_back_archive2ldbm);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DB2ARCHIVE_FN,
                           (void *)ldbm_back_ldbm2archive);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_UPGRADEDB_FN,
                           (void *)ldbm_back_upgradedb);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN,
                           (void *)ldbm_back_upgradednformat);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_DBVERIFY_FN,
                           (void *)ldbm_back_dbverify);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_BEGIN_FN,
                           (void *)dblayer_plugin_begin);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_COMMIT_FN,
                           (void *)dblayer_plugin_commit);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ABORT_FN,
                           (void *)dblayer_plugin_abort);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SIZE_FN,
                           (void *)ldbm_db_size);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_INIT_INSTANCE_FN,
                           (void *)ldbm_back_init); /* register itself so that the secon instance
                                          can be initialized */
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_WIRE_IMPORT_FN,
                           (void *)ldbm_back_wire_import);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_ADD_SCHEMA_FN,
                           (void *)ldbm_back_add_schema);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_GET_INFO_FN,
                           (void *)ldbm_back_get_info);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_SET_INFO_FN,
                           (void *)ldbm_back_set_info);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DB_CTRL_INFO_FN,
                           (void *)ldbm_back_ctrl_info);

    if (rc != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_init", "Failed %d\n", rc);
        goto fail;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_init", "<=\n");

    return (0);

fail:
    ldbm_config_destroy(li);
    slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, NULL);
    return (-1);
}
