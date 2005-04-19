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
/* repl_connext.c - replication extension to the Connection object
 */


#include "repl.h"
#include "repl5.h"


/* ***** Supplier side ***** */

/* NOT NEEDED YET */

/* ***** Consumer side ***** */

/* consumer connection extension constructor */
void* consumer_connection_extension_constructor (void *object, void *parent)
{
	consumer_connection_extension *ext = (consumer_connection_extension*) slapi_ch_malloc (sizeof (consumer_connection_extension));
	if (ext == NULL)
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "unable to create replication consumer connection extension - out of memory\n" );
	}
	else
	{
	    ext->is_legacy_replication_dn= 0;
		ext->repl_protocol_version = REPL_PROTOCOL_UNKNOWN;
		ext->replica_acquired = NULL;
		ext->isreplicationsession= 0;
        ext->supplier_ruv = NULL;
		ext->connection = NULL;
	}

	return ext;
}

/* consumer connection extension destructor */
void consumer_connection_extension_destructor (void *ext, void *object, void *parent)
{
	int connid = 0;
	if (ext)
	{
		/* Check to see if this replication session has acquired
		 * a replica. If so, release it here.
		 */
		consumer_connection_extension *connext = (consumer_connection_extension *)ext;
		if (NULL != connext->replica_acquired)
		{
            Replica *r = object_get_data ((Object*)connext->replica_acquired);
			/* If a total update was in progress, abort it */
			if (REPL_PROTOCOL_50_TOTALUPDATE == connext->repl_protocol_version)
			{
				Slapi_PBlock *pb = slapi_pblock_new();
				const Slapi_DN *repl_root_sdn = replica_get_root(r);
				PR_ASSERT(NULL != repl_root_sdn);
				if (NULL != repl_root_sdn)
				{
					slapi_pblock_set(pb, SLAPI_CONNECTION, connext->connection);
					slapi_pblock_set(pb, SLAPI_TARGET_DN, (void*)slapi_sdn_get_dn(repl_root_sdn));
					slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"Aborting total update in progress for replicated "
									"area %s connid=%d\n", slapi_sdn_get_dn(repl_root_sdn),
									connid);
					slapi_stop_bulk_import(pb);
				}
				else
				{
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"consumer_connection_extension_destructor: can't determine root "
						"of replicated area.\n");
				}
				slapi_pblock_destroy(pb);

				/* allow reaping again */
				replica_set_tombstone_reap_stop(r, PR_FALSE);
			}
			replica_relinquish_exclusive_access(r, connid, -1);
            object_release ((Object*)connext->replica_acquired);
			connext->replica_acquired = NULL;
		}

        if (connext->supplier_ruv)
        {
            ruv_destroy ((RUV **)&connext->supplier_ruv);
        }
		connext->connection = NULL;
		slapi_ch_free((void **)&ext);	
	}
}
