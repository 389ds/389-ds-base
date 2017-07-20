/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _LDAPU_CERT_H
#define _LDAPU_CERT_H

#ifndef NSAPI_PUBLIC
#define NSAPI_PUBLIC
#endif

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC int ldapu_get_cert(void *SSLendpoint, void **cert);

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_CERT_H */
