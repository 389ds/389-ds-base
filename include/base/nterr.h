/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Added function prototypes for nterror stuff.
 *
 * Robin Maxwell
 */

#ifndef _NTERR_H
#define _NTERR_H
NSPR_BEGIN_EXTERN_C

char * FindError(int error);
NSAPI_PUBLIC void HashNtErrors();

NSPR_END_EXTERN_C

#endif /* _NTERR_H */
