/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
  This file contains a function which augments the standard nspr PL_HashTable
  api.  The problem is that the hash table lookup function in the standard NSPR
  actually modifies the hash table being searched, which means that it cannot be
  used with read locks in a multi threaded environment.  This function is a
  lookup function which is guaranteed not to modify the hash table passed in,
  so that it can be used with read locks.
*/

#include "plhash.h"

/* prototypes */
NSPR_BEGIN_EXTERN_C
PR_IMPLEMENT(void *)
ACL_HashTableLookup_const(PLHashTable *ht, const void *key);
NSPR_END_EXTERN_C

/*
** Multiplicative hash, from Knuth 6.4.
*/
#define GOLDEN_RATIO    0x9E3779B9U

PR_IMPLEMENT(PLHashEntry **)
ACL_HashTableRawLookup_const(PLHashTable *ht, PLHashNumber keyHash, const void *key)
{
    PLHashEntry *he, **hep;
    PLHashNumber h;

#ifdef HASHMETER
    ht->nlookups++;
#endif
    h = keyHash * GOLDEN_RATIO;
    h >>= ht->shift;
    hep = &ht->buckets[h];
    while ((he = *hep) != 0) {
        if (he->keyHash == keyHash && (*ht->keyCompare)(key, he->key)) {
            return hep;
        }
        hep = &he->next;
#ifdef HASHMETER
        ht->nsteps++;
#endif
    }
    return hep;
}

PR_IMPLEMENT(void *)
ACL_HashTableLookup_const(PLHashTable *ht, const void *key)
{
    PLHashNumber keyHash;
    PLHashEntry *he, **hep;

    keyHash = (*ht->keyHash)(key);
    hep = ACL_HashTableRawLookup_const(ht, keyHash, key);
    if ((he = *hep) != 0) {
        return he->value;
    }
    return 0;
}
