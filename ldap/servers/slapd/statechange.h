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


#ifndef _STATE_NOTIFY_H_
#define _STATE_NOTIFY_H_

/* mechanics */

typedef void (*notify_callback)(Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data);
typedef void (*caller_data_free_callback)(void *caller_data);

typedef int (*api_statechange_register)(char *caller_id, char *dn, char *filter, void *caller_data, notify_callback func);
/* returns pointer to caller data passed to api_statechange_register */
typedef void *(*api_statechange_unregister)(char *dn, char *filter, notify_callback func);
typedef void (*api_statechange_unregister_all)(char *caller_id, caller_data_free_callback callback);

/* API ID for slapi_apib_get_interface */

#define StateChange_v1_0_GUID "0A340151-6FB3-11d3-80D2-006008A6EFF3"

/* API */

/* the api broker reserves api[0] for its use */

#define statechange_register(api, caller_id, dn, filter, caller, func) \
	((api_statechange_register*)(api))[1]( caller_id, dn, filter, caller, func)

#define statechange_unregister(api, dn, filter, func) \
	((api_statechange_unregister*)(api))[2]( dn, filter, func)

#define statechange_unregister_all(api, caller_id, callback) \
	((api_statechange_unregister*)(api))[3](caller_id, callback)

/* Vattr state change handler to be passed to statechange_register() by va sps*/
#define statechange_vattr_cache_invalidator_callback(api) api[4]

#define STATECHANGE_VATTR_GLOBAL_INVALIDATE 1
#define STATECHANGE_VATTR_ENTRY_INVALIDATE 2

/* Vattr api caller data to be passed to statechange_register() */
static int vattr_global_invalidate = STATECHANGE_VATTR_GLOBAL_INVALIDATE;
static int vattr_entry_invalidate = STATECHANGE_VATTR_ENTRY_INVALIDATE;

#endif /*_STATE_NOTIFY_H_*/
