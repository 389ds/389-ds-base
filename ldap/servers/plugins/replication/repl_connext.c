/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
