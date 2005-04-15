/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
