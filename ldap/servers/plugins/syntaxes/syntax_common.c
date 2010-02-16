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
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "syntax.h"

int
syntax_register_matching_rule_plugins(
	struct mr_plugin_def mr_plugin_table[],
	size_t mr_plugin_table_size,
	IFP matching_rule_plugin_init
)
{
	int rc = -1;
	int ii;

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
	size_t mr_plugin_table_size
)
{
	int ii;
	char **argv = NULL;
	int rc = -1;
	struct mr_plugin_def *mrpd = NULL;

	slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
	if (!argv || !argv[0]) {
		slapi_log_error(SLAPI_LOG_FATAL, "syntax_matching_rule_plugin_init",
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
			break;
		}
	}

	if (!mrpd) {
		slapi_log_error(SLAPI_LOG_FATAL, "syntax_matching_rule_plugin_init",
						"Error: matching rule plugin name [%s] not found\n",
						argv[0]);
	} else {
		rc = slapi_matchingrule_register(&mrpd->mr_def_entry);
	}

	return rc;
}

