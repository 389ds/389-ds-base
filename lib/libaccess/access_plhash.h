/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

NSPR_BEGIN_EXTERN_C
/*
 * access_plhash.cpp - supplement to NSPR plhash
 */
extern void *
ACL_HashTableLookup_const(
    void *ht, /* really a PLHashTable */
    const void *key);

NSPR_END_EXTERN_C

