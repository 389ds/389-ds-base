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
#include <string.h>
#include <stdlib.h>

#include "netsite.h"
#ifdef BERKELEY_DB_RESOURCE
#include "mcom_db.h"
#include "nsres.h"
#endif

#define RESOURCE_STR

/********************************************/
/* Begin: Application dependent information */
/********************************************/

#ifdef NS_DS
#include "gsslapd.h"
#define GSXXX_H_INCLUDED
#endif

#ifdef buildAnotherServer
#include "gsanother.h"
#define GSXXX_H_INCLUDED
#endif

/********************************************/
/*  End: Application dependent information  */
/********************************************/

/**********************************************/
/*  Begin: Check that BUILD_MODULE is handled */
/*         and a gs*.h file has been included */
/**********************************************/

#ifndef GSXXX_H_INCLUDED
#error Error in makstrdb.c: BUILD_MODULE not handled; gs*.h not included.
#endif

/********************************************/
/*  End: Check that BUILD_MODULE is handled */
/*       and a gs*.h file has been included */
/********************************************/

/*******************************************************************************/

#ifdef XP_DEBUG

void
XP_PrintStringDatabase(void)  /* debug routine */
{
  int i;
  int j;
  char* LibraryName;
  RESOURCE_TABLE* table;
  
  j = 0;
  while (table=allxpstr[j++].restable) {
    LibraryName = table->str;
    printf("Library %d: %s\n",j,LibraryName);
    i = 1;
    table++;
    while (table->str) {
      printf("%d: %s      %d      \"%s\"\n",i,LibraryName,table->id,table->str);
      i++;
      table++;
    }
  }
}

#endif /* XP_DEBUG */

#ifdef BERKELEY_DB_RESOURCE
/*******************************************************************************/

int
XP_MakeStringDatabase(void)
{
  int j;
  char* LibraryName;
  char* cptr;
  RESOURCE_TABLE* table;
  NSRESHANDLE hresdb;
  
  /* Creating database */
  hresdb = NSResCreateTable(DATABASE_NAME, NULL);
  if (hresdb==0) {
    printf("Error creating database %s\n",DATABASE_NAME);
    return 1;
  }
 
  j = 0;
  while (table=allxpstr[j++].restable) {
    LibraryName = table->str;
    printf("Add Library %d: %s\n",j,LibraryName);
    table++;
    while (table->str) {
      if (table->id==-1 && strstr(table->str,"$DBT: ")) {
        cptr = strstr(table->str,"referenced");
        if (cptr) {
          strncpy(cptr,"in DB file",10);
        }
      }
      NSResAddString(hresdb,LibraryName,table->id,table->str,0);
      table++;
    }
  }
  
  NSResCloseTable(hresdb);
  return 0;
}
#endif

/*******************************************************************************/

int
XP_MakeStringProperties(void)
{
    int j;
    char* LibraryName;
    RESOURCE_TABLE* table;
    FILE *hresfile;
    char buffer[2000];
    char *src, *dest;
    char *dbfile;
  
    /* Creating database */
    dbfile = (char *) malloc (strlen(DATABASE_NAME) + 20);
    strcpy(dbfile, DATABASE_NAME);
    strcat(dbfile, ".properties");

    hresfile = fopen(dbfile, "w");

    if (hresfile==NULL) {
        printf("Error creating properties file %s\n",DATABASE_NAME);
        return 1;
    }
 
    j = 0;
    while ((table=allxpstr[j++].restable)) {
        LibraryName = table->str;
        fprintf(hresfile, "\n");
        fprintf(hresfile, "#######################################\n");
        fprintf(hresfile, "############### %s ###############\n", LibraryName);
        printf("Add Library %d: %s\n",j,LibraryName);
        table++;
        while (table->str) {        
            /*
              Change special char to \uXXXX
             */
            src = table->str;
            dest = buffer;
            while (*src && (sizeof(buffer) > (dest-buffer))) {
                if (*src < 0x20) {
                    strcpy(dest,"\\u00");
                    dest += 4;
                    sprintf(dest, "%02x", *src);
                    dest += 1;
                }
                else {
                    *dest = *src;
                }      
                src ++;
                dest ++;
            }
            *dest = '\0';

            if (table->id > 0) {
                fprintf(hresfile, "%s-%d =%s\n", LibraryName, table->id, buffer);
            }
            table++;
        }
    }
  
    fclose(hresfile);
    return 0;
}




/*******************************************************************************/

int main()
{
#if 0
    return XP_MakeStringDatabase();
#else
    return XP_MakeStringProperties();
#endif
}

/*******************************************************************************/
