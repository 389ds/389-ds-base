/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _CERTMAP_PLUGIN_H
#define _CERTMAP_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

extern int plugin_init_fn (void *certmap_info, const char *issuerName,
			   const char *issuerDN);

#ifdef __cplusplus
}
#endif

#endif /* _CERTMAP_PLUGIN_H */
