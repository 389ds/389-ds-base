/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _IDL_API_H_
#define _IDL_API_H_

/* mechanics */

typedef IDList *(*api_idl_alloc)( NIDS nids );
typedef void (*api_idl_insert)(IDList **idl, ID id);

/* API ID for slapi_apib_get_interface */

#define IDL_v1_0_GUID "ec228d97-971d-4b9e-91b5-4f90e1841f24"

/* API */

/* the api broker reserves api[0] for its use */

#define IDList_alloc(api, nids) \
	((api_idl_alloc*)(api))[1](nids)

#define IDList_insert(api, idl, id) \
	((api_idl_insert*)(api))[2](idl, id)


#endif /*_IDL_API_H_*/
