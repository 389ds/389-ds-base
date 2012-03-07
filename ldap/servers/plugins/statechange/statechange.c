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

/* plugin which provides a callback mechanism for state changes in the DS */

#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "slapi-plugin.h"
#include "statechange.h"

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* the circular list of systems to notify */
typedef struct _statechange_notify
{
	char *caller_id;
	char *dn;
	char *filter;
	Slapi_Filter *realfilter;
	notify_callback func;
	void *caller_data;
	struct _statechange_notify *next;
	struct _statechange_notify *prev;
} SCNotify;

static SCNotify *head; /* a place to start in the list */

#define SCN_PLUGIN_SUBSYSTEM   "statechange-plugin"   /* used for logging */

static void *api[5];
static Slapi_Mutex *buffer_lock = 0;

/* other function prototypes */
int statechange_init( Slapi_PBlock *pb ); 
static int statechange_start( Slapi_PBlock *pb );
static int statechange_close( Slapi_PBlock *pb );
static int statechange_post_op( Slapi_PBlock *pb, int modtype );
static int statechange_mod_post_op( Slapi_PBlock *pb );
static int statechange_modrdn_post_op( Slapi_PBlock *pb );
static int statechange_add_post_op( Slapi_PBlock *pb );
static int statechange_delete_post_op( Slapi_PBlock *pb );
static int _statechange_register(char *caller_id, char *dn, char *filter, void *caller_data, notify_callback func);
static void *_statechange_unregister(char *dn, char *filter, notify_callback func);
static void _statechange_unregister_all(char *caller_id, caller_data_free_callback);
static void _statechange_vattr_cache_invalidator_callback(Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data);
static SCNotify *statechange_find_notify(char *dn, char *filter, notify_callback func);


static Slapi_PluginDesc pdesc = { "statechange", VENDOR, DS_PACKAGE_VERSION,
	"state change notification service plugin" };


#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif


/* 
	statechange_init
	--------
	adds our callbacks to the list
*/
int statechange_init( Slapi_PBlock *pb )
{
	int ret = 0;
	Slapi_Entry *plugin_entry = NULL;
	char *plugin_type = NULL;
	int postadd = SLAPI_PLUGIN_POST_ADD_FN;
	int postmod = SLAPI_PLUGIN_POST_MODIFY_FN;
	int postmdn = SLAPI_PLUGIN_POST_MODRDN_FN;
	int postdel = SLAPI_PLUGIN_POST_DELETE_FN;

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "--> statechange_init\n");

	if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
		plugin_entry &&
		(plugin_type = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype")) &&
		plugin_type && strstr(plugin_type, "betxn")) {
		postadd = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
		postmod = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
		postmdn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
		postdel = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
	}
	slapi_ch_free_string(&plugin_type);

	head = 0;

	if (	slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	         (void *) statechange_start ) != 0 ||
	        slapi_pblock_set(pb, postmod, (void *) statechange_mod_post_op ) != 0 ||
	        slapi_pblock_set(pb, postmdn, (void *) statechange_modrdn_post_op ) != 0 ||
	        slapi_pblock_set(pb, postadd, (void *) statechange_add_post_op ) != 0 ||
	        slapi_pblock_set(pb, postdel, (void *) statechange_delete_post_op ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	         (void *) statechange_close ) != 0 ||
			slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                     (void *)&pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, SCN_PLUGIN_SUBSYSTEM,
                         "statechange_init: failed to register plugin\n" );
		ret = -1;
    }

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "<-- statechange_init\n");
    return ret;
}


/*
	statechange_start
	---------
	This function publishes the interface for this plugin
*/
static int statechange_start( Slapi_PBlock *pb )
{
	int ret = 0;

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "--> statechange_start\n");

	api[0] = 0; /* reserved for api broker use, must be zero */
	api[1] = (void *)_statechange_register;
	api[2] = (void *)_statechange_unregister;
	api[3] = (void *)_statechange_unregister_all;
	api[4] = (void *)_statechange_vattr_cache_invalidator_callback;

	if(0 == (buffer_lock = slapi_new_mutex())) /* we never free this mutex */
	{
		/* badness */
		slapi_log_error( SLAPI_LOG_FATAL, SCN_PLUGIN_SUBSYSTEM, "statechange: failed to create lock\n");
		ret = -1;
	}
	else
	{
		if( slapi_apib_register(StateChange_v1_0_GUID, api) )
		{
			slapi_log_error( SLAPI_LOG_FATAL, SCN_PLUGIN_SUBSYSTEM, "statechange: failed to publish state change interface\n");
			ret = -1;
		}
	}

	head = 0;

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "<-- statechange_start\n");
	return ret;
}

/*
	statechange_close
	---------
	unregisters the interface for this plugin
*/
static int statechange_close( Slapi_PBlock *pb )
{
	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "--> statechange_close\n");

	slapi_apib_unregister(StateChange_v1_0_GUID);

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "<-- statechange_close\n");

	return 0;
}


static int statechange_mod_post_op( Slapi_PBlock *pb )
{
	return statechange_post_op(pb, LDAP_CHANGETYPE_MODIFY);
}

static int statechange_modrdn_post_op( Slapi_PBlock *pb )
{
	return statechange_post_op(pb, LDAP_CHANGETYPE_MODDN);
}

static int statechange_add_post_op( Slapi_PBlock *pb )
{
	return statechange_post_op(pb, LDAP_CHANGETYPE_ADD);
}

static int statechange_delete_post_op( Slapi_PBlock *pb )
{
	return statechange_post_op(pb, LDAP_CHANGETYPE_DELETE);
}


/*
	statechange_post_op
	-----------
	Catch all for all post operations that change entries
	in some way - evaluate the change against the notification
	entries and fire off the relevant callbacks - it is called
	from the real postop functions which supply it with the 
	postop type
*/
static int statechange_post_op( Slapi_PBlock *pb, int modtype )
{
	SCNotify *notify = head; 
	int execute;
	Slapi_DN *sdn = NULL;
	char *ndn = NULL;
	struct slapi_entry *e_before = NULL;
	struct slapi_entry *e_after = NULL;

	if(head == 0)
		return 0;

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "--> statechange_post_op\n");

	/* evaluate this operation against the notification entries */
	
	slapi_lock_mutex(buffer_lock);
	if(head)
	{
		slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
		if (NULL == sdn) {
			slapi_log_error( SLAPI_LOG_FATAL, SCN_PLUGIN_SUBSYSTEM, 
			         "statechange_post_op: failed to get dn of changed entry" );
			goto bail;
		}
		ndn = (char *)slapi_sdn_get_ndn(sdn);

		slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &e_before );
		slapi_pblock_get( pb, SLAPI_ENTRY_POST_OP, &e_after );

		do 
		{
			execute = 0;

			/* first dn */
			if(notify->dn)
			{
				if(0 != slapi_dn_issuffix(ndn, notify->dn))
					execute = 1;
			}
			else
			/* note, if supplied null for everything in the entry *all* ops match */
				execute = 1;

			if(execute && notify->filter)
			{
				/* next the filter */
				int filter_test = 0;

				/* need to test entry both before and after op */
				if(e_before && !slapi_filter_test_simple( e_before, notify->realfilter))
					filter_test = 1;

				if(!filter_test && e_after && !slapi_filter_test_simple( e_after, notify->realfilter))
					filter_test = 1;

				if(!filter_test)
					execute = 0;
			}

			if(execute)
			{
				if(e_after)
					(notify->func)(e_after, ndn, modtype, pb, notify->caller_data);
				else
					(notify->func)(e_before, ndn, modtype, pb, notify->caller_data);
			}

			notify = notify->next;
		}
		while(notify && notify != head);
	}
bail:
	slapi_unlock_mutex(buffer_lock);

	slapi_log_error( SLAPI_LOG_TRACE, SCN_PLUGIN_SUBSYSTEM, "<-- statechange_post_op\n");
	return 0; /* always succeed */
}


static int _statechange_register(char *caller_id, char *dn, char *filter, void *caller_data, notify_callback func)
{
	int ret = -1;
	SCNotify *item;

	/* simple - we don't check for duplicates */
	
	item = (SCNotify*)slapi_ch_malloc(sizeof(SCNotify));
	if(item)
	{
		char *writable_filter = slapi_ch_strdup(filter);
		item->caller_id = slapi_ch_strdup(caller_id);
		if(dn)
		{
			item->dn = slapi_ch_strdup(dn);
			slapi_dn_normalize( item->dn );
		}
		else
			item->dn = 0;
		item->filter = slapi_ch_strdup(filter);
		item->caller_data = caller_data;
		if (writable_filter &&
			(NULL == (item->realfilter = slapi_str2filter(writable_filter)))) {
			slapi_log_error(SLAPI_LOG_FATAL, SCN_PLUGIN_SUBSYSTEM,
							"Error: invalid filter in statechange entry [%s]: [%s]\n",
							dn, filter);
			slapi_ch_free_string(&item->caller_id);
			slapi_ch_free_string(&item->dn);
			slapi_ch_free_string(&item->filter);
			slapi_ch_free_string(&writable_filter);
			slapi_ch_free((void **)&item);
			return -1;
		} else if (!writable_filter) {
			item->realfilter = NULL;
		}
		item->func = func;

		slapi_lock_mutex(buffer_lock);
		if(head == NULL)
		{
			head = item;
			head->next = head;
			head->prev = head;
		}
		else
		{
			item->next = head;
			item->prev = head->prev;
			head->prev = item;
			item->prev->next = item;
		}
		slapi_unlock_mutex(buffer_lock);
		slapi_ch_free_string(&writable_filter);

		ret = 0;
	}

	return ret;
}

static void *_statechange_unregister(char *dn, char *filter, notify_callback thefunc)
{
	void *ret = NULL;
	SCNotify *func = NULL;

	if(buffer_lock == 0)
		return ret;

	slapi_lock_mutex(buffer_lock);

	if((func = statechange_find_notify(dn, filter, thefunc)))
	{
		func->prev->next = func->next;
		func->next->prev = func->prev;

		if(func == head)
		{
			head = func->next;
		}

		if(func == head) /* must be the last item, turn off the lights */
			head = 0;

		slapi_ch_free_string(&func->caller_id);
		slapi_ch_free_string(&func->dn);
		slapi_ch_free_string(&func->filter);
		slapi_filter_free( func->realfilter, 1 );
		ret = func->caller_data;
		slapi_ch_free((void **)&func);
	}

	slapi_unlock_mutex(buffer_lock);

	return ret;
}

static void _statechange_unregister_all(char *caller_id, caller_data_free_callback callback)
{
	SCNotify *notify = head;
	SCNotify *start_notify = head;

	if(buffer_lock == 0)
		return;

	slapi_lock_mutex(buffer_lock);


	if(notify)
	{
		do 
		{
			SCNotify *notify_next = notify->next;

			if( slapi_utf8casecmp((unsigned char *)caller_id, (unsigned char *)notify->caller_id) )
			{
				notify->prev->next = notify->next;
				notify->next->prev = notify->prev;

				if(notify == head)
				{
					head = notify->next;
					start_notify = notify->prev;
				}

				if(notify == head) /* must be the last item, turn off the lights */
					head = 0;

				if(callback)
					callback(notify->caller_data);
				slapi_ch_free_string(&notify->caller_id);
				slapi_ch_free_string(&notify->dn);
				slapi_ch_free_string(&notify->filter);
				slapi_filter_free( notify->realfilter, 1 );
				slapi_ch_free((void **)&notify);
			}

			notify = notify_next;
		}
		while(notify != start_notify && notify != NULL);
	}

	slapi_unlock_mutex(buffer_lock);
}

/* this func needs looking at to make work */
static SCNotify *statechange_find_notify(char *dn, char *filter, notify_callback func)
{
	SCNotify *notify = head;
	SCNotify *start_notify = head;

	if(notify)
	{
		do 
		{
			if(	!slapi_utf8casecmp((unsigned char *)dn, (unsigned char *)notify->dn) && 
				!slapi_utf8casecmp((unsigned char *)filter, (unsigned char *)notify->filter) && func == notify->func) 
			{
				return notify;
			}

			notify = notify->next;
		}
		while(notify != start_notify);
	}

	return 0;
}

/* intended for use by vattr service providers
 * to deal with significant vattr state changes
 */
static void _statechange_vattr_cache_invalidator_callback(Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data)
{
	/* simply get the significance data and act */
	switch(*(int*)caller_data)
	{
	case STATECHANGE_VATTR_ENTRY_INVALIDATE:
		if(e)
			slapi_entry_vattrcache_watermark_invalidate(e);
		break;

	case STATECHANGE_VATTR_GLOBAL_INVALIDATE:
	default:
		slapi_entrycache_vattrcache_watermark_invalidate();
		break;
	}
}

