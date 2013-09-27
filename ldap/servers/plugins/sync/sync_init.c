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
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "sync.h"

static Slapi_PluginDesc pdesc = { PLUGIN_NAME, VENDOR, DS_PACKAGE_VERSION, "Context Synchronization (RFC4533) plugin" };

static int sync_start(Slapi_PBlock * pb);
static int sync_close(Slapi_PBlock * pb);
static int sync_preop_init( Slapi_PBlock *pb );
static int sync_postop_init( Slapi_PBlock *pb );

int sync_init( Slapi_PBlock *pb )
{
	char *plugin_identity = NULL;
	int rc = 0;

	slapi_log_error(SLAPI_LOG_TRACE, SYNC_PLUGIN_SUBSYSTEM,
                    "--> sync_init\n");

	/**
	 * Store the plugin identity for later use.
	 * Used for internal operations
	 */

	slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
	PR_ASSERT(plugin_identity);

	if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
    		slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *) sync_start) != 0 ||
        	slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) sync_close) != 0 ||
        	slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ) {
        		slapi_log_error(SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM,
                	        "sync_init: failed to register plugin\n");
			rc = 1;
	}

	if (rc == 0) {
        	char *plugin_type = "preoperation";
       	 /* the config change checking post op */
        	if (slapi_register_plugin(
				plugin_type, 
                		1,        	/* Enabled */
				"sync_init",	/* this function desc */
				sync_preop_init,/* init func for post op */
				SYNC_PREOP_DESC,/* plugin desc */
				NULL, 
				plugin_identity)) {
           				slapi_log_error(SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM,
                            			"sync_init: failed to register preop plugin\n");
					rc = 1;
		}
	}

	if (rc == 0) {
        	char *plugin_type = "postoperation";
        	/* the config change checking post op */
        	if (slapi_register_plugin(plugin_type, 
                                  1,        /* Enabled */
                                  "sync_init",   /* this function desc */
                                  sync_postop_init,  /* init func for post op */
                                  SYNC_POSTOP_DESC,      /* plugin desc */
                                  NULL,
                                  plugin_identity )) {
            				slapi_log_error(SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM,
                            			"sync_init: failed to register postop plugin\n");
					rc = 1;
		}
	}

	return( rc );
}

static int
sync_preop_init( Slapi_PBlock *pb )
{
	int rc;
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void *) sync_srch_refresh_pre_search);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ENTRY_FN, (void *) sync_srch_refresh_pre_entry);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_RESULT_FN, (void *) sync_srch_refresh_pre_result);
        rc |= sync_register_operation_extension();
	return(rc);
	
}

static int
sync_postop_init( Slapi_PBlock *pb )
{
	int rc;
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN, (void *) sync_add_persist_post_op);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *) sync_del_persist_post_op);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *) sync_mod_persist_post_op);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *) sync_modrdn_persist_post_op);
        rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_POST_SEARCH_FN, (void *) sync_srch_refresh_post_search);
	return(rc);
}

/*
	sync_start
	--------------
	Register the Content Synchronization Control.
	Initialize locks and queues for the persitent phase.
*/
static int
sync_start(Slapi_PBlock * pb)
{
	int	argc;
	char	**argv;

	slapi_register_supported_control( LDAP_CONTROL_SYNC,
        	SLAPI_OPERATION_SEARCH );
	slapi_log_error(SLAPI_LOG_TRACE, SYNC_PLUGIN_SUBSYSTEM,
		"--> sync_start\n");

	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGC, &argc ) != 0 ||
	     slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM,
			"unable to get arguments\n" );
		return( -1 );
	}
	sync_persist_initialize(argc, argv);

	return (0);
}

/*
	sync_close
	--------------
	Free locks and queues allocated.
*/
static int
sync_close(Slapi_PBlock * pb)
{
	sync_persist_terminate_all();
    return (0);
}
