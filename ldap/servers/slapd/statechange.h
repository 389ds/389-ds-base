/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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
