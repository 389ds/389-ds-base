/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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

