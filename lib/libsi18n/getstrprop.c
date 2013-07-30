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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "i18n.h"

#include "getstrmem.h"

static char*
XP_GetStringFromMemory(const char* strLibraryName,int iToken)
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
  int      found = 0;
  unsigned uToken = iToken;
  const char*    cPtr;
  DATABIN* pBucket;

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
  while (*(pBucket->pLibraryName)!='\0') {
    if (strcmp(pBucket->pLibraryName,strLibraryName)==0) {
	  found = 1;
	  break;
    }
    pBucket++;
  }

  if (!found) {
    return emptyString;
  }

  if (uToken<=pBucket->numberOfStringsInLibrary) {
      return pBucket->pArrayOfLibraryStrings[uToken];
    } else {
	  /* string token out of range */
      return emptyString;
    }

}

const char*
XP_GetStringFromDatabase(const char* strLibraryName,
                         const char* strLanguage,
                         int key)
{
    const char *result = NULL;

    /* we use memory strings only in ds. */
    if (result == NULL)
        result = XP_GetStringFromMemory(strLibraryName,key);
    return result;
}
