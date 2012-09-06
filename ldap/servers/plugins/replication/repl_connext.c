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
		ext->in_use_opid = -1;
		ext->lock = PR_NewLock();
		if (NULL == ext->lock)
		{
			slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "unable to create replication consumer connection extension lock - out of memory\n" );
			/* no need to go through the full destructor, but still need to free up this memory */
			slapi_ch_free((void **)&ext);
			ext = NULL;
		}
	}

	return ext;
}

/* consumer connection extension destructor */
void consumer_connection_extension_destructor (void *ext, void *object, void *parent)
{
	PRUint64 connid = 0;
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
					slapi_pblock_set(pb, SLAPI_TARGET_SDN, (void*)repl_root_sdn);
					slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"Aborting total update in progress for replicated "
									"area %s connid=%" NSPRIu64 "\n", slapi_sdn_get_dn(repl_root_sdn),
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
        
        if (connext->lock)
        {
            PR_DestroyLock(connext->lock);
            connext->lock = NULL;
        }
        
        connext->in_use_opid = -1;
        
		connext->connection = NULL;
		slapi_ch_free((void **)&ext);	
	}
}

/* Obtain exclusive access to this connection extension.
 * Returns the consumer_connection_extension* if successful, else NULL.
 * 
 * This is similar to obtaining exclusive access to the replica, but not identical.
 * For the connection extension, you only want to hold on to exclusive access as
 * long as it is being actively used to process an operation.  Mainly that means
 * while processing either a 'start' or an 'end' extended operation.  This makes
 * certain that if another 'start' or 'end' operation is received on the connection,
 * the ops will not trample on each other's state.  As soon as you are done with
 * that single operation, it is time to relinquish the connection extension.
 * That differs from acquiring exclusive access to the replica, which is held over
 * after the 'start' operation and relinquished during the 'end' operation.
 */
consumer_connection_extension* 
consumer_connection_extension_acquire_exclusive_access(void* conn, PRUint64 connid, int opid)
{
    consumer_connection_extension* ret = NULL;
    
    /* step 1, grab the connext */
    consumer_connection_extension* connext = (consumer_connection_extension*)
            repl_con_get_ext(REPL_CON_EXT_CONN, conn);
    
    if (NULL != connext)
    {
        /* step 2, acquire its lock */
        PR_Lock(connext->lock);

        /* step 3, see if it is not in use, or in use by us */
        if (0 > connext->in_use_opid)
        {
            /* step 4, take it! */
            connext->in_use_opid = opid;
            ret = connext;
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Acquired consumer connection extension\n",
                    connid, opid);
        }
        else if (opid == connext->in_use_opid)
        {
            ret = connext;
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Reacquired consumer connection extension\n",
                    connid, opid);
        }
        else
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Could not acquire consumer connection extension; it is in use by op=%d\n",
                    connid, opid, connext->in_use_opid);
        }

        /* step 5, drop the lock */
        PR_Unlock(connext->lock);
    }
    else
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                "conn=%" NSPRIu64 " op=%d Could not acquire consumer extension, it is NULL!\n",
                connid, opid);
    }
    
    return ret;
}

/* Relinquish exclusive access to this connection extension.
 * Returns 0 if exclusive access could NOT be relinquished, and non-zero if it was.
 * Specifically:
 *      1 if the extension was in use and was relinquished.
 *      2 if the extension was not in use to begin with.
 * 
 * The extension will only be relinquished if it was first acquired by this op,
 * or if 'force' is TRUE.  Do not use 'force' without a legitimate reason, such
 * as when destroying the parent connection.
 * 
 * cf. consumer_connection_extension_acquire_exclusive_access() for details on how,
 * when, and why you would want to acquire and relinquish exclusive access.
 */
int 
consumer_connection_extension_relinquish_exclusive_access(void* conn, PRUint64 connid, int opid, PRBool force)
{
    int ret = 0;
    
    /* step 1, grab the connext */
    consumer_connection_extension* connext = (consumer_connection_extension*)
            repl_con_get_ext(REPL_CON_EXT_CONN, conn);
    
    if (NULL != connext)
    {
        /* step 2, acquire its lock */
        PR_Lock(connext->lock);

        /* step 3, see if it is in use */
        if (0 > connext->in_use_opid)
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Consumer connection extension is not in use\n",
                    connid, opid);
            ret = 2;
        }
        else if (opid == connext->in_use_opid)
        {
            /* step 4, relinquish it (normal) */
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Relinquishing consumer connection extension\n",
                    connid, opid);            
            connext->in_use_opid = -1;
            ret = 1;
        }
        else if (force)
        {
            /* step 4, relinquish it (forced) */
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Forced to relinquish consumer connection extension held by op=%d\n",
                    connid, opid, connext->in_use_opid);
            connext->in_use_opid = -1;
            ret = 1;
        }
        else
        {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                    "conn=%" NSPRIu64 " op=%d Not relinquishing consumer connection extension, it is held by op=%d!\n",
                    connid, opid, connext->in_use_opid);         
        }
        
        /* step 5, drop the lock */
        PR_Unlock(connext->lock);
    }
    else
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                "conn=%" NSPRIu64 " op=%d Could not relinquish consumer extension, it is NULL!\n",
                connid, opid);
    }
    
    return ret;
}

