/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _LDAPU_CERT_H
#define _LDAPU_CERT_H

#ifndef NSAPI_PUBLIC
#ifdef XP_WIN32
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC 
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC int ldapu_get_cert (void *SSLendpoint, void **cert);

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_CERT_H */
