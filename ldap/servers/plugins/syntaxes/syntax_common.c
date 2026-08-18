/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "syntax.h"

int
syntax_register_matching_rule_plugins(
    struct mr_plugin_def mr_plugin_table[],
    size_t mr_plugin_table_size,
    int32_t (*matching_rule_plugin_init)(Slapi_PBlock *))
{
    int rc = -1;
    size_t ii;

    for (ii = 0; ii < mr_plugin_table_size; ++ii) {
        char *argv[2];

        argv[0] = mr_plugin_table[ii].mr_def_entry.mr_name;
        argv[1] = NULL;
        rc = slapi_register_plugin_ext("matchingrule", 1 /* enabled */,
                                       "matching_rule_plugin_init",
                                       matching_rule_plugin_init,
                                       mr_plugin_table[ii].mr_def_entry.mr_name,
                                       argv, NULL, PLUGIN_DEFAULT_PRECEDENCE);
    }

    return rc;
}

int
syntax_matching_rule_plugin_init(
    Slapi_PBlock *pb,
    struct mr_plugin_def mr_plugin_table[],
    size_t mr_plugin_table_size)
{
    size_t ii;
    char **argv = NULL;
    int rc = -1;
    struct mr_plugin_def *mrpd = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (!argv || !argv[0]) {
        slapi_log_err(SLAPI_LOG_ERR, SYNTAX_PLUGIN_SUBSYSTEM,
                      "syntax_matching_rule_plugin_init - "
                      "Error: matching rule plugin name not specified\n");
        return rc;
    }
    for (ii = 0; ii < mr_plugin_table_size; ++ii) {
        /* get the arguments - argv[0] is our plugin name */
        /* find the plugin name in the table */
        if (!strcmp(mr_plugin_table[ii].mr_def_entry.mr_name, argv[0])) {
            mrpd = &mr_plugin_table[ii];
            rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, &mrpd->mr_plg_desc);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, mrpd->mr_filter_create);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_INDEXER_CREATE_FN, mrpd->mr_indexer_create);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_AVA, mrpd->mr_filter_ava);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_SUB, mrpd->mr_filter_sub);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES2KEYS, mrpd->mr_values2keys);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA, mrpd->mr_assertion2keys_ava);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB, mrpd->mr_assertion2keys_sub);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_NAMES, mrpd->mr_names);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_COMPARE, mrpd->mr_compare);
            rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_MR_NORMALIZE, mrpd->mr_normalize);
            break;
        }
    }

    if (!mrpd) {
        slapi_log_err(SLAPI_LOG_ERR, SYNTAX_PLUGIN_SUBSYSTEM,
                      "syntax_matching_rule_plugin_init - "
                      "Error: matching rule plugin name [%s] not found\n",
                      argv[0]);
    } else {
        rc = slapi_matchingrule_register(&mrpd->mr_def_entry);
    }

    return rc;
}
