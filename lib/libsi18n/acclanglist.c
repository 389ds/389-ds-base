/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "i18n.h"


/*
 *      Accept-Language = "Accept-Language" ":"
 *                        1#( language-range [ ";" "q" "=" qvalue ] )
 *      language-range  = ( ( 1*8ALPHA *( "-" 1*8ALPHA ) ) | "*" )
 *
 *      NLS_AccLangList() assumes that "Accept-Language:" has already
 *      been stripped off. It takes as input
 * 
 *      1#( ( ( 1*8ALPHA *( "-" 1*8ALPHA ) ) | "*" ) [ ";" "q" "=" qvalue ] )
 *
 *      and returns a list of languages, ordered by qvalues, in
 *      the array NLS_ACCEPT_LANGUAGE_LIST. 
 *      
 *      If there are to many languages (>NLS_MAX_ACCEPT_LANGUAGE) the excess
 *      is ignored. If the language-range is too long (>NLS_MAX_ACCEPT_LENGTH),
 *      the language-range is ignored. In these cases, NLS_AccLangList()
 *      will quietly return, perhaps with numLang = 0. numLang is
 *      returned by the function.  
 */


static size_t
AcceptLangList(const char* AcceptLanguage,
               ACCEPT_LANGUAGE_LIST AcceptLanguageList)
{
  char* input;
  char* cPtr;
  char* cPtr1;
  char* cPtr2;
  int i;
  int j;
  int countLang = 0;
  
  input = strdup(AcceptLanguage);
  if (input == (char*)NULL){
	  return 0;
  }

  cPtr1 = input-1;
  cPtr2 = input;

  /* put in standard form */
  while (*(++cPtr1)) {
    if      (isalpha(*cPtr1))  *cPtr2++ = tolower(*cPtr1); /* force lower case */
    else if (isspace(*cPtr1));                             /* ignore any space */
    else if (*cPtr1=='-')      *cPtr2++ = '_';             /* "-" -> "_"       */
    else if (*cPtr1=='*');                                 /* ignore "*"       */
    else                       *cPtr2++ = *cPtr1;          /* else unchanged   */
  }
  *cPtr2 = '\0';

  countLang = 0;

  if (strchr(input,';')) {
    /* deal with the quality values */

    float qvalue[MAX_ACCEPT_LANGUAGE];
    float qSwap;
    float bias = 0.0f;
    char* ptrLanguage[MAX_ACCEPT_LANGUAGE];
    char* ptrSwap;

    cPtr = strtok(input,",");
    while (cPtr) {
      qvalue[countLang] = 1.0f;
      if (cPtr1 = strchr(cPtr,';')) {
        sscanf(cPtr1,";q=%f",&qvalue[countLang]);
        *cPtr1 = '\0';
      }
      if (strlen(cPtr)<MAX_ACCEPT_LENGTH) {     /* ignore if too long */
        qvalue[countLang] -= (bias += 0.0001f); /* to insure original order */
        ptrLanguage[countLang++] = cPtr;
        if (countLang>=MAX_ACCEPT_LANGUAGE) break; /* quit if too many */
      }
      cPtr = strtok(NULL,",");
    }

    /* sort according to decending qvalue */
    /* not a very good algorithm, but count is not likely large */
    for ( i=0 ; i<countLang-1 ; i++ ) {
      for ( j=i+1 ; j<countLang ; j++ ) {
        if (qvalue[i]<qvalue[j]) {
          qSwap     = qvalue[i];
          qvalue[i] = qvalue[j];
          qvalue[j] = qSwap;
          ptrSwap        = ptrLanguage[i];
          ptrLanguage[i] = ptrLanguage[j];
          ptrLanguage[j] = ptrSwap;
        }
      }
    }
    for ( i=0 ; i<countLang ; i++ ) {
      strcpy(AcceptLanguageList[i],ptrLanguage[i]);
    }

  } else {
    /* simple case: no quality values */

    cPtr = strtok(input,",");
    while (cPtr) {
      if (strlen(cPtr)<MAX_ACCEPT_LENGTH) {        /* ignore if too long */
        strcpy(AcceptLanguageList[countLang++],cPtr);
        if (countLang>=MAX_ACCEPT_LANGUAGE) break; /* quit if too many */
      }
      cPtr = strtok(NULL,",");
    }
  }

  free(input);

  return countLang;
}

/*
 *   Get prioritized locale list from NLS_AcceptLangList 
 *      
 *   Add additonal language to the list for fallback if locale 
 *   name is language_region
 *
 */


int
XP_AccLangList(char* AcceptLanguage,
               ACCEPT_LANGUAGE_LIST AcceptLanguageList)
{
	int i;
	int n;
	char *defaultLanguage = "en";
	ACCEPT_LANGUAGE_LIST curLanguageList;
	int index = 0;
	char lang[3];
	int k;

	n = AcceptLangList(AcceptLanguage, curLanguageList);

	if (n == 0)
		return 0;

	memset(lang, 0, 3);
	for (i = 0; i < n; i++) {
		if (*lang && (strncmp(lang, curLanguageList[i], 2) != 0)) {
			/* add lang if current language is the last occurence in the list */
			for (k = i+1; (k < n) && strncmp(curLanguageList[k],lang,2); k++);

			if (k == n) {
				strcpy(AcceptLanguageList[index++], lang);
				*lang = '\0';
			}
		}

		strcpy(AcceptLanguageList[index++], curLanguageList[i]);

        /* Add current language for future appending.,make sure it's not on list */
        if ((strlen(curLanguageList[i]) > 2) && (curLanguageList[i][2] == '_')) {
		    strncpy(lang, curLanguageList[i], 2);
	        for (k = 0; (k < index) && strcmp(AcceptLanguageList[k], lang); k++);

	        if (k != index)   lang[0] = '\0';
        }
	}

	if (lang[0] != '\0')
		strcpy(AcceptLanguageList[index++], lang);	/* add new lang */

	/* Append defaultLanguage if it's not in the list */
	for (i = 0; (i < index) && strcmp(AcceptLanguageList[i], defaultLanguage); i++);

	if (i == index)
		strcpy(AcceptLanguageList[index++], defaultLanguage);

	return index;
}
