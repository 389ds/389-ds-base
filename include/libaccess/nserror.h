/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nserror_h
#define __nserror_h

#ifndef NOINTNSACL
#define INTNSACL
#endif /* !NOINTNSACL */

/*
 * Description (nserror.h)
 *
 *	This file describes the interface to an error handling mechanism
 *	that is intended for general use.  This mechanism uses a data
 *	structure known as an "error frame" to capture information about
 *	an error.  Multiple error frames are used in nested function calls
 *	to capture the interpretation of an error at the different levels
 *	of a nested call.
 */

#include <stdarg.h>
#include <prtypes.h>
#include "public/nsacl/nserrdef.h"

#ifdef INTNSACL

NSPR_BEGIN_EXTERN_C

/* Functions in nseframe.c */
extern void nserrDispose(NSErr_t * errp);
extern NSEFrame_t * nserrFAlloc(NSErr_t * errp);
extern void nserrFFree(NSErr_t * errp, NSEFrame_t * efp);
extern NSEFrame_t * nserrGenerate(NSErr_t * errp, long retcode, long errorid,
				  char * program, int errc, ...);

/* Functions in nserrmsg.c */
extern char * nserrMessage(NSEFrame_t * efp, int flags);
extern char * nserrRetrieve(NSEFrame_t * efp, int flags);

NSPR_END_EXTERN_C

#endif /* INTNSACL */

#endif /* __nserror_h */
