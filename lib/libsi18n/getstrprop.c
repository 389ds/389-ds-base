/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "i18n.h"

#include "getstrmem.h"

#include "libadminutil/resource.h"
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
