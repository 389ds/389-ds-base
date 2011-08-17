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

#include "cb.h"

extern cb_instance_config_info cb_the_instance_config[];
/*
** Chaining backend instance monitor function
** This function wraps up backend specific monitoring information
** and return it to the client as an LDAP entry.
** This function is usually called upon receipt of the monitor
** dn for this backend
**
** Monitor information:
**
**	Database misc
**		database
**
** 	Number of hits for each operation
**		addopcount
**		modifyopcount
**		deleteopcount
**		modrdnopcount
**		compareopcount
**		searchsubtreeopcount
**		searchonelevelopcount
**		searchbaseopcount
**		bindopcount
**		unbindopcount
**		abandonopcount
**
**	Outgoing connections
**		outgoingopconnections
**		outgoingbindconnections
**
*/

int 
cb_search_monitor_callback(Slapi_PBlock * pb, Slapi_Entry * e, Slapi_Entry * entryAfter, int * returnCode, char * returnText, void * arg)
{

	char          		buf[CB_BUFSIZE];
	struct berval 		val;
  	struct berval 		*vals[2];
	unsigned long		deletecount,addcount,modifycount,modrdncount,searchbasecount,searchonelevelcount;
	unsigned long		searchsubtreecount,abandoncount,bindcount,unbindcount,comparecount;
	unsigned int 		outgoingconn, outgoingbindconn;
	cb_backend_instance	*inst = (cb_backend_instance *)arg;

	/* First make sure the backend instance is configured */
	/* If not, don't return anything		      */

        slapi_rwlock_rdlock(inst->rwl_config_lock);
	if (!inst->isconfigured) {
	        *returnCode= LDAP_NO_SUCH_OBJECT;
        	slapi_rwlock_unlock(inst->rwl_config_lock);
		return SLAPI_DSE_CALLBACK_ERROR;
	}
        slapi_rwlock_unlock(inst->rwl_config_lock);

  	vals[0] = &val;
  	vals[1] = NULL;

	slapi_lock_mutex(inst->monitor.mutex);

	addcount		=inst->monitor.addcount;
	deletecount		=inst->monitor.deletecount;
	modifycount		=inst->monitor.modifycount;
	modrdncount		=inst->monitor.modrdncount;
	searchbasecount		=inst->monitor.searchbasecount;
	searchonelevelcount	=inst->monitor.searchonelevelcount;
	searchsubtreecount	=inst->monitor.searchsubtreecount;
	abandoncount		=inst->monitor.abandoncount;
	bindcount		=inst->monitor.bindcount;
	unbindcount		=inst->monitor.unbindcount;
	comparecount		=inst->monitor.comparecount;
	
	slapi_unlock_mutex(inst->monitor.mutex);

	/*
	** Get connection information
	*/

    	slapi_lock_mutex(inst->pool->conn.conn_list_mutex);
	outgoingconn= inst->pool->conn.conn_list_count;
    	slapi_unlock_mutex(inst->pool->conn.conn_list_mutex);

    	slapi_lock_mutex(inst->bind_pool->conn.conn_list_mutex);
	outgoingbindconn= inst->bind_pool->conn.conn_list_count;
    	slapi_unlock_mutex(inst->bind_pool->conn.conn_list_mutex);
	
  	sprintf( buf, "%lu", addcount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_ADDCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", deletecount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_DELETECOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", modifycount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_MODIFYCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", modrdncount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_MODRDNCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", searchbasecount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_SEARCHBASECOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", searchonelevelcount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_SEARCHONELEVELCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", searchsubtreecount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_SEARCHSUBTREECOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", abandoncount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_ABANDONCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", bindcount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_BINDCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", unbindcount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_UNBINDCOUNT, ( struct berval **)vals );

  	sprintf( buf, "%lu", comparecount );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_COMPARECOUNT, ( struct berval **)vals );

  	sprintf( buf, "%u", outgoingconn );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_OUTGOINGCONN, ( struct berval **)vals );

  	sprintf( buf, "%u", outgoingbindconn );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_OUTGOINGBINDCOUNT, ( struct berval **)vals );

  	*returnCode= LDAP_SUCCESS;
  	return(SLAPI_DSE_CALLBACK_OK);
}

void 
cb_update_monitor_info(Slapi_PBlock * pb, cb_backend_instance * inst,int op)
{

	int scope;

	slapi_lock_mutex(inst->monitor.mutex);
	switch (op) {
	case SLAPI_OPERATION_ADD:
		inst->monitor.addcount++;
		break;
	case SLAPI_OPERATION_MODIFY:
		inst->monitor.modifycount++;
		break;
	case SLAPI_OPERATION_DELETE:
		inst->monitor.deletecount++;
		break;
	case SLAPI_OPERATION_MODRDN:
/**	case SLAPI_OPERATION_MODDN: **/
		inst->monitor.modrdncount++;
		break;
	case SLAPI_OPERATION_COMPARE:
		inst->monitor.comparecount++;
		break;
	case SLAPI_OPERATION_ABANDON:
		inst->monitor.abandoncount++;
		break;
	case SLAPI_OPERATION_BIND:
		inst->monitor.bindcount++;
		break;
	case SLAPI_OPERATION_UNBIND:
		inst->monitor.unbindcount++;
		break;
	case SLAPI_OPERATION_SEARCH:
        	slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
		if ( LDAP_SCOPE_BASE == scope )
			inst->monitor.searchbasecount++;
		else
		if ( LDAP_SCOPE_ONELEVEL == scope )
			inst->monitor.searchonelevelcount++;
		else
			inst->monitor.searchsubtreecount++;
		break;
	default:
	        slapi_log_error( SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,"cb_update_monitor_info: invalid op type <%d>\n",op);
	}
	slapi_unlock_mutex(inst->monitor.mutex);
}
			

int
cb_delete_monitor_callback(Slapi_PBlock * pb, Slapi_Entry * e, Slapi_Entry * entryAfter, int * returnCode, char * returnText, void * arg)
{

	cb_backend_instance	*inst = (cb_backend_instance *)arg;

        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, inst->monitorDn, LDAP_SCOPE_BASE,
                "(objectclass=*)", cb_search_monitor_callback);
        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, inst->monitorDn, LDAP_SCOPE_BASE,
                "(objectclass=*)", cb_dont_allow_that);
        slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, inst->monitorDn, LDAP_SCOPE_BASE,
                "(objectclass=*)", cb_delete_monitor_callback);
 
        *returnCode= LDAP_SUCCESS;
        return(SLAPI_DSE_CALLBACK_OK);
}
