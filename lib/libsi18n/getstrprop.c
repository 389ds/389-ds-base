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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "i18n.h"

#include "getstrmem.h"

#include "coreres.h"

Resource *hResource = NULL;
char empty_string[] = "";

char*
XP_GetStringFromMemory(char* strLibraryName,int iToken);



void
XP_InitStringDatabase(char* pathCWD, char* databaseName)
{
    hResource = core_res_init_resource (pathCWD, databaseName);
}

char *XP_GetPropertyString(char* strLibraryName,int iToken, ACCEPT_LANGUAGE_LIST lang)
{
    char *key_name;
    char *result = NULL;

    if (hResource == NULL)
        return NULL;

	/*creating the key*/
	key_name=(char*)malloc(strlen(strLibraryName) + 10);
	sprintf(key_name, "%s-%d", strLibraryName, iToken);
	if(key_name == NULL)
		return NULL;

    result = (char *) core_res_getstring(hResource, key_name, lang) ;

    if (key_name)
        free (key_name);

    if (result == NULL)
        return empty_string;
    else
        return result ;
}

char*
XP_GetStringFromDatabase(char* strLibraryName,
                         char* strLanguage,
                         int key)
{
    char *result = NULL;
    ACCEPT_LANGUAGE_LIST alanglist;
	int n;

    /*
     * display first choice language if available, otherwise 
     * use default which is english in most case
     */
    if (hResource) { 
        n = XP_AccLangList (strLanguage, alanglist);
		if (n >= MAX_ACCEPT_LANGUAGE)
			alanglist[MAX_ACCEPT_LANGUAGE-1][0] = '\0';
		else
			alanglist[n][0] = '\0';
		result = XP_GetPropertyString(strLibraryName, key, alanglist);
    }

    /* we should never come here. */
    if (result == NULL)
        result = XP_GetStringFromMemory(strLibraryName,key);
    return result;
}


char*
XP_GetStringFromMemory(char* strLibraryName,int iToken)
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
  char*    cPtr;
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
