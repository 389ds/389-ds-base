/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _VIEWS_H_
#define _VIEWS_H_

/* mechanics */

typedef int (*api_views_entry_exists)(char *view_dn, Slapi_Entry *e);
typedef int (*api_views_entry_dn_exists)(char *view_dn, char *e_dn);

/* API ID for slapi_apib_get_interface */

#define Views_v1_0_GUID "000e5b1e-9958-41da-a573-db8064a3894e"

/* API */

/* the api broker reserves api[0] for its use */

#define views_entry_exists(api, dn, entry) \
	((api_views_entry_exists*)(api))[1]( dn, entry )

#define views_entry_dn_exists(api, dn, entry_dn) \
	((api_views_entry_dn_exists*)(api))[2]( dn, entry_dn )

#endif /*_VIEWS_H_*/
