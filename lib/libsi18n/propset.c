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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include "i18n.h"

#include "txtfile.h"
#include "reshash.h"
#include "propset.h"

int PropertiesLoadFileToHash(PropertiesSet *propset, char *language);
char *GetProertiesFilename(char *directory, char *file, char *language);
int PropertiesLanguageStatus(PropertiesSet *propset, char *language);
int PropertiesSetLangStatus(LanguageStatus *langstatus, char *language, int status);
int unicode_to_UTF8(unsigned int wch, char *utf8);
char *decode_ascii(char *src);


PropertiesSet * PropertiesInit(char *directory, char *file)
{
    struct stat buf;
    char * file_path;
    PropertiesSet *propset = NULL;
    PropertiesSet *result = NULL;
    ResHash *reshash;

    file_path = (char *) malloc (strlen(directory) + strlen(file) + 20);

    strcpy(file_path, directory);
    strcat(file_path, "/");
    strcat(file_path, file);
    strcat(file_path, ".properties");

    if (stat(file_path, &buf) == 0) {
        propset = (PropertiesSet *) malloc(sizeof(PropertiesSet));
        memset(propset, 0, sizeof(PropertiesSet));
        reshash = (ResHash *) ResHashCreate(file);

        if (reshash) {
            propset->langlist = (LanguageStatus *) malloc(sizeof(LanguageStatus));
            memset(propset->langlist, 0, sizeof(LanguageStatus));

            propset->res = reshash;
            propset->directory = strdup(directory);
            propset->filename = strdup(file);
            PropertiesLoadFileToHash(propset, NULL);
            result = propset;
        }
    }

    if (file_path)
        free (file_path);

    return result;
}


char *GetProertiesFilename(char *directory, char *file, char *language)
{
    char *filepath;

    if (language && *language == '\0')
        filepath = (char *) malloc(strlen(directory) + strlen(file) + strlen(language) + 20);
    else
        filepath = (char *) malloc(strlen(directory) + strlen(file) + 20);

    strcpy(filepath, directory);
    if (filepath[strlen(filepath) - 1] != '/')
        strcat(filepath, "/");
    strcat(filepath, file);
    if (language && *language != '\0') {
        strcat(filepath, "_");
        strcat(filepath, language);
    }
    strcat(filepath, ".properties");

    return filepath;
}

/*
  PropertiesLoadToHash

    Opens property file and save data to hash table

  Input
    propfile: handle
    file:  full path with file extension   

  return:
    0: SUCCESS
    1: FAIL
*/

int PropertiesLoadFileToHash(PropertiesSet *propset, char *language)
{
    TEXTFILE *hfile;
    char *filepath;
    char *p, *q;
    int n;
    char linebuf[1000];
    int st;

    st = PropertiesLanguageStatus(propset, language);
    if (st == LANGUAGE_INVALID)
        return 1;
    else if (st == LANGUAGE_LOAD)
        return 0;

    filepath = GetProertiesFilename(propset->directory, propset->filename, language);

    if ((hfile = OpenTextFile (filepath, TEXT_OPEN_FOR_READ)) == NULL) {
        PropertiesSetLangStatus(propset->langlist, language, LANGUAGE_INVALID); 
        return 1;
    }

    while ((n = ReadTextLine(hfile, linebuf)) >= 0) {
        if (n == 0)
            continue;

        p = linebuf;
        /* strip leading spaces */
        while (*p == ' ' || *p == '\t')
            p ++;
        /* skip comment line */
        if (*p == '\0' || *p == '#' || *p == '=')
            continue;

        q = strchr (linebuf, '=');
        if (q) {
            char *key, *value, *newvalue;

            *q = '\0';
            key = p;
            value = q + 1;
            /* strip trailing space for key */
            p = key + strlen(key) - 1;
            while (*p == ' ' || *p == '\t') {
                *p = '\0';
                p --;
            }

            /* decode Unicode escape value */
            newvalue = decode_ascii(value);

            if (newvalue) {
                ResHashAdd(propset->res, key, newvalue, language);
                free(newvalue);
            }
            else
                ResHashAdd(propset->res, key, value, language);
        }
    }
    PropertiesSetLangStatus(propset->langlist, language, LANGUAGE_LOAD); 
    return 0;
}

/*
  PropertiesIsLoaded

   Test if current properties associated with language 
   is loaded or not. 
   
   return:
        1:  SUCCESS
        0:  FAIL
  */

int PropertiesLanguageStatus(PropertiesSet *propset, char *language)
{
    LanguageStatus *plang;

    plang = propset->langlist;
    if (language == NULL || *language == '\0') {
        return plang->status;
    }

    plang = plang->next;

    while (plang) {
        if (strcmp(plang->language, language) == 0) {
            return plang->status;
        }
        plang = plang->next;
    }
    return LANGUAGE_NONE;
}

int PropertiesSetLangStatus(LanguageStatus *langlist, char *language, int status)
{
    LanguageStatus *plang, *prev;
    LanguageStatus *langstatus;

    if (language == NULL || *language == '\0') {
        langlist->status = status;
        return 0;
    }

    prev = plang = langlist;
    plang = plang->next;

    while (plang) {
        if (strcmp(plang->language, language) == 0) {
            plang->status = status;
            return 0;
        }
        prev = plang;
        plang = plang->next;
    }
    
    langstatus = (LanguageStatus *) malloc(sizeof(LanguageStatus));
    memset (langstatus, 0, sizeof(LanguageStatus));
    langstatus->language = strdup(language);
    langstatus->status = status;
    prev->next = langstatus;
    
    return 0;
}


/***
    PropertiesOpenFile

    return 0:  loaded
           1:  fail to load file associated with the language


 */
int PropertiesOpenFile(PropertiesSet *propset, char *language)
{
    int status;
    status = PropertiesLanguageStatus(propset, language);
    
    if (status == LANGUAGE_NONE)
        return PropertiesLoadFileToHash (propset, language);
    else if (status == LANGUAGE_INVALID)
        return 1;
    else
        return 0;
}

const char *PropertiesGetString(PropertiesSet *propset, char *key, ACCEPT_LANGUAGE_LIST acceptlangauge)
{
    int i;
    char *language = NULL;

    i = 0;
    while (acceptlangauge[i][0]) {
        if (PropertiesOpenFile(propset, acceptlangauge[i]) == 0) {
            language = acceptlangauge[i];
            break;
        }
        i ++;
    }

    return ResHashSearch(propset->res, key, language);
}
void PropertiesDestroy(PropertiesSet *propset)
{
    LanguageStatus *langattrib, *next;

    if (propset) {
        if (propset->path)
            free(propset->path);
        if (propset->directory)
            free(propset->directory);
        if (propset->filename)
            free(propset->filename);

        ResHashDestroy(propset->res);

        langattrib = propset->langlist;
        while (langattrib) {
            next = langattrib->next;
            if (langattrib->language)
                free(langattrib->language);
            free(langattrib);
            langattrib = next;
        }
    }
}


char *decode_ascii(char *src)
{
    int i;
    char utf8[10];
	int state = 0;
	int digit = 0;
	int digit_count = 0;
    char *result, *p, *q;

    if (src == NULL || *src == '\0')
        return NULL;

    if (strchr(src, '\\') == NULL)
        return NULL;

    result = (char *) malloc(strlen(src) + 1);

    p = src;
    q = result;

    for (;*p; p++) {
        char ch;
        int n;
    	if (state == BACKSLASH_U) {
    		ch = toupper(*p);
    		if (ch >= '0' && ch <= '9') {
    			digit = digit * 16 + (ch - '0');
    			digit_count ++;
    		}
    		else if (ch >= 'A' && ch <= 'F') {
    			digit = digit * 16 + (ch - 'A' + 10);
    			digit_count ++;
    		}
    		else {
    			n = unicode_to_UTF8(digit, utf8);                
                for (i = 0; i < n; i++)
                    *q ++ = utf8[i];
                *q ++ = *p;
    			state = 0;
    			digit_count = 0;
    		}    			   
    		
    		if (digit_count == 4) {
    			n = unicode_to_UTF8(digit, utf8);
                for (i = 0; i < n; i++)
                    *q ++ = utf8[i];
    			state = 0;
    		}    			
    	}
    	else if (state == BACKSLASH) {
    		if (*p == 'u') {
    			state = BACKSLASH_U;
    			digit = 0;
    			digit_count = 0;
    			continue;    				
    		}
			else if (*p == 'n') {
    			*q++ = '\n';
    			state = 0;
			}
			else if (*p == 'r') {
    			*q++ = '\r';
    			state = 0;
			}
    		else {
    			*q++ = '\\';
    			*q++ = *p;
    			state = 0;
    		}    			    			
    	}
    	else if (*p == '\\') {
    		state = BACKSLASH;
    		continue;
    	}
    	else {
    		*q++ = *p;
    		state = 0;
    	}
    }
    *q = '\0';
    return result;    	
}


int unicode_to_UTF8(unsigned int wch, char *utf8)
{
    unsigned char hibyte, lobyte, mibyte;

    if (wch <= 0x7F) {
        /* 0000 007F ==>   0xxxxxxx  */
        utf8[0] = (unsigned char) wch ;
        utf8[1] = '\0';
        return 1;        
    }
    else if (wch <= 0x7FF) {
        /* 0000 07FF ==>  110xxxxx 10xxxxxx */
        lobyte = wch & 0x3F;
        hibyte = (wch >> 6) & 0x1F;

        utf8[0] = 0xC0 | hibyte;
        utf8[1] = 0x80 | lobyte;
        utf8[2] = '\0';
        return 2;
    }
    else {
        /* FFFF   ==> 1110xxxx 10xxxxxx 10xxxxxx */
        lobyte = wch & 0x3F;
        mibyte = (wch >> 6) & 0x3F;
        hibyte = (wch >> 12) & 0xF;

        utf8[0] = 0xE0 | hibyte;
        utf8[1] = 0x80 | mibyte;
        utf8[2] = 0x80 | lobyte;
        utf8[3] = '\0';
        return 3;
    }
}
