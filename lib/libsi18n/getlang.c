/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libadmin/libadmin.h"


#include "i18n.h"

/*********************************************************************
  strReplace replaces the first instance of from in target with to.
  from can be "": to is inserted at start of target.
  to can be "": from is removed from target.
  if from is not found, 0 is returned; else 1 is returned.
 *********************************************************************/

static int
strReplace(char* target,char* from,char* to)
{
  /* replace /from/to/ in target */
  
  char* pFrom;
  char* pOldTail;
  int   lenTo;
  
  pFrom = strstr(target,from);
  if (pFrom) {
    pOldTail = pFrom+strlen(from);
    lenTo = strlen(to);
    memmove(pFrom+lenTo,pOldTail,strlen(pOldTail)+1);
    memcpy(pFrom,to,lenTo);
    return 1;
  }
  
  return 0;
}

/*********************************************************************
  statFileDir is a wrapper to stat() that strips trailing slashes
  because stat() on Windows seems to return -1 otherwise.
*********************************************************************/

int
statFileDir(const char *path,  struct stat *info) {
	int ret, pathlen;
	char *newpath = strdup(path);

	if(newpath == NULL)
		return -1;

	for (pathlen = (strlen(newpath) - 1); pathlen >= 0; pathlen--) {
		if (newpath[pathlen] == '/' || newpath[pathlen] == '\\') {
			newpath[pathlen] = '\0';
		} else {
			break;
		}
	}

	ret = stat(newpath, info);

	if (newpath)
		free(newpath);

	return ret;
}

/*********************************************************************
  GetLanguage is reserved for future use. These APIs are not belong
  to this file. It needs to be moved to somewhere which knows what's
  the current language setting.
 *********************************************************************/

static char emptyString[] = "";
 
static char client_language[128] = "en";
static char admin_language[128] = "en";
static char default_language[128] = "en";

void
SetLanguage(int type, char *language)
{
	switch(type) {
	case CLIENT_LANGUAGE:
		if (language)
			strcpy(client_language, language);
		break;
	case ADMIN_LANGUAGE:
		if (language)
			strcpy(admin_language, language);
		break;
	case DEFAULT_LANGUAGE:
		if (language)
			strcpy(default_language, language);
		break;
	}
	return ;
}



char*
GetClientLanguage(void)
{
  if (client_language)
	return client_language;
  else
	return emptyString;
}
 
char*
GetAdminLanguage(void)
{
  if (admin_language)
	return admin_language;
  else
	return emptyString;
}

char*
GetDefaultLanguage(void)
{
  if (default_language)
	return default_language;
  else
	return "en";
}

/*********************************************************************
  GetFileForLanguage looks for a file in the appropriate language.
 *********************************************************************/

NSAPI_PUBLIC
int
GetFileForLanguage(char* filePath,char* language,char* existingFilePath)
{
  /* Input: filePath,language
   * filePath is of the form "/xxx/xxx/$$LANGDIR/xxx/xxx/filename"
   *          or of the form "/xxx/xxx/xxx/xxx/filename".
   * filename may or may not have an extension.
   * language is an Accept-Language list; each language-range will be
   *   tried as a subdirectory name and possibly as a filename modifier.
   *   "*" is ignored - default always provided if needed.
   *   "-" is replaced by "_".
   * $$LANGDIR is a special string replaced by language. It is optional.
   *   For the default case, $$LANGDIR/ is replaced by nothing
   *   (so // is not created).
   *
   * Returned: existingPath
   * existingFilePath is the path of a satisfactory, existing file.
   * if no file is found, an empty string "" is returned.
   *
   * int returned: -1 if no file found (existingFilePath = "")
   *                0 if default file is returned
   *                1 if language file is returned (any in list)
   *
   * Example:
   *    filePath               = "/path/$$LANGDIR/filename.ext"
   *    language               = "language"
   *    GetDefaultLanguage() --> "default"
   *    LANG_DELIMIT           = "_"
   *  
   * 1. Try: "/path/language/filename.ext"
   * 2. Try: "/path/filename_language.ext"
   * 3. Try: "/path/default/filename.ext"
   * 4. Try: "/path/filename_default.ext"
   * 5. Try: "/path/filename.ext"
   *   else: ""
   *
   * Example:
   *    language               = "en-us;q=0.6,ja;q=0.8,en-ca"
   *  
   * 1. Try: "/path/en-ca/filename.ext"
   * 2. Try: "/path/filename_en_ca.ext"
   * 3. Try: "/path/ja/filename.ext"
   * 4. Try: "/path/filename_ja.ext"
   * 5. Try: "/path/en_us/filename.ext"
   * 6. Try: "/path/filename_en_us.ext"
   * 7. Try: "/path/default/filename.ext"
   * 8. Try: "/path/filename_default.ext"
   * 9. Try: "/path/filename.ext"
   *   else: ""
   *
   */

#define LANG_DELIMIT '_'

  int pattern;
  char* pDot;
  char* pSlash;

  /* PRFileInfo info; */
  struct stat info;

  char lang_modifier[MAX_ACCEPT_LENGTH+1];

  ACCEPT_LANGUAGE_LIST acceptLanguageList;
  int numLang;
  int iLang;
  int iCase;


  /* escape in case XP_InitStringDatabase has not been called */
  if (filePath==NULL) {
    *existingFilePath = '\0';
    return -1;
  }

  pattern = (strstr(filePath,"$$LANGDIR/")!=NULL);

  for ( iCase=1 ; iCase>=0 ; iCase-- ) {
    if (iCase==1) {             /* iCase=1 tries requested language */
      numLang = XP_AccLangList(language,acceptLanguageList);
    } else {                    /* iCase=0 tries default language */
      numLang = XP_AccLangList(GetDefaultLanguage(),acceptLanguageList);
    }
    
    for ( iLang=0 ; iLang<numLang ; iLang++ ) {
      
      /* Try: /path/language/filename.ext */
      if (pattern) {
        strcpy(existingFilePath,filePath);
        strReplace(existingFilePath,"$$LANGDIR",acceptLanguageList[iLang]);

        if (statFileDir(existingFilePath,&info)==0) {
          return iCase;
        }

        /*
          if (PR_GetFileInfo(existingFilePath,&info)==PR_SUCCESS) {
          return iCase;
          }
          */
      }
      
      /* Try: /path/filename_language.ext */
      {
        strcpy(existingFilePath,filePath);
        strReplace(existingFilePath,"$$LANGDIR/",emptyString);
        pDot = strrchr(existingFilePath,'.');
        pSlash = strrchr(existingFilePath,'/');
        if (pSlash>=pDot) {
          pDot = strchr(existingFilePath,'\0');
        }
        sprintf(lang_modifier,"%c%s",LANG_DELIMIT,acceptLanguageList[iLang]);
        strReplace(pDot,emptyString,lang_modifier);

        if (statFileDir(existingFilePath,&info)==0) {
          return iCase;
        }

        /*
          if (PR_GetFileInfo(existingFilePath,&info)==PR_SUCCESS) {
          return iCase;
          }
          */
      }
    }
  }
  
  /* Try: /path/filename.ext */
  {
    strcpy(existingFilePath,filePath);
    strReplace(existingFilePath,"$$LANGDIR/",emptyString);

    if (statFileDir(existingFilePath,&info)==0) {
      return 0;
    }

    /*
      if (PR_GetFileInfo(existingFilePath,&info)==PR_SUCCESS) {
      return 0;
      }
      */
  }

  /* Else: */
  *existingFilePath = '\0';
  return -1;
}






