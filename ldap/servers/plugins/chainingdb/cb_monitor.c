/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
	int 			deletecount,addcount,modifycount,modrdncount,searchbasecount,searchonelevelcount;
	int 			searchsubtreecount,abandoncount,bindcount,unbindcount,comparecount;
	int 			outgoingconn, outgoingbindconn;
	cb_backend_instance	*inst = (cb_backend_instance *)arg;

	/* First make sure the backend instance is configured */
	/* If not, don't return anything		      */

        PR_RWLock_Rlock(inst->rwl_config_lock);
	if (!inst->isconfigured) {
	        *returnCode= LDAP_NO_SUCH_OBJECT;
        	PR_RWLock_Unlock(inst->rwl_config_lock);
		return SLAPI_DSE_CALLBACK_ERROR;
	}
        PR_RWLock_Unlock(inst->rwl_config_lock);

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

  	sprintf( buf, "%d", outgoingconn );
  	val.bv_val = buf;
  	val.bv_len = strlen( buf );
  	slapi_entry_attr_replace( e, CB_MONITOR_OUTGOINGCONN, ( struct berval **)vals );

  	sprintf( buf, "%d", outgoingbindconn );
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
