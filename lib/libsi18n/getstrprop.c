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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "i18n.h"

#include "getstrmem.h"

static char *
XP_GetStringFromMemory(const char *strLibraryName, int iToken)
{
    /*
   * In memory model called by XP_GetStringFromDatabase
   * does not use database (nsres, et al.).
   *
   * This function uses hash table for library lookup
   * and direct lookup for string.
   *
   * This function is thread safe.
   */


    unsigned hashKey;
    int found = 0;
    unsigned uToken = iToken;
    const char *cPtr;
    DATABIN *pBucket;

    /* calculate hash key */
    hashKey = 0;
    cPtr = strLibraryName;
    while (*cPtr) {
        hashKey += *(cPtr++);
    }
    hashKey &= BUCKET_MASK;

    /* get bucket for this hash key */
    pBucket = buckets[hashKey];

    /* search overflow buckets */
    while (*(pBucket->pLibraryName) != '\0') {
        if (strcmp(pBucket->pLibraryName, strLibraryName) == 0) {
            found = 1;
            break;
        }
        pBucket++;
    }

    if (!found) {
        return emptyString;
    }

    if (uToken <= pBucket->numberOfStringsInLibrary) {
        return pBucket->pArrayOfLibraryStrings[uToken];
    } else {
        /* string token out of range */
        return emptyString;
    }
}

const char *
XP_GetStringFromDatabase(const char *strLibraryName,
                         const char *strLanguage __attribute__((unused)),
                         int key)
{
    const char *result = NULL;

    /* we use memory strings only in ds. */
    if (result == NULL)
        result = XP_GetStringFromMemory(strLibraryName, key);
    return result;
}
