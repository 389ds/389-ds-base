/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
#define	FALSE			0
#endif
#ifndef TRUE
#define	TRUE			1
#endif

#ifndef PUBLIC_NSACL_ACLAPI_H
#include "public/nsacl/aclapi.h"
#endif /* !PUBLIC_NSACL_ACLAPI_H */

#ifdef INTNSACL

NSPR_BEGIN_EXTERN_C

extern	char	*generic_rights[];
extern	char	*http_generic[];

NSPR_END_EXTERN_C

#endif /* INTNSACL */

#endif
