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

#ifndef ACL_HEADER
#define ACL_HEADER

#ifndef NOINTNSACL
#define INTNSACL
#endif /* NOINTNSACL */

#include <netsite.h>
#include <base/pool.h>
#include <base/plist.h>
#include <libaccess/nserror.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#ifndef PUBLIC_NSACL_ACLAPI_H
#include "public/nsacl/aclapi.h"
#endif /* !PUBLIC_NSACL_ACLAPI_H */

#ifdef INTNSACL

NSPR_BEGIN_EXTERN_C

extern const char *generic_rights[];
extern const char *http_generic[];

NSPR_END_EXTERN_C

#endif /* INTNSACL */

#endif
