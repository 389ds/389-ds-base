/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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

#ifdef MCC_ADMSERV
#include "gsadmserv.h"
#define GSXXX_H_INCLUDED
#endif

#ifdef NS_ENTERPRISE
#include "gshttpd.h"
#define GSXXX_H_INCLUDED
#endif

#ifdef NS_DS
#include "gsslapd.h"
#define GSXXX_H_INCLUDED
#endif

#ifdef NS_PERSONAL
#include "gshttpd.h"
#define GSXXX_H_INCLUDED
#endif

#ifdef MCC_PROXY
#include "gsproxy.h"
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
  char DBTlibraryName[128];
  
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
    char* cptr;
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
    while (table=allxpstr[j++].restable) {
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
            while (*src) {
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
