/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef I18N_H
#define I18N_H

/* Make NSAPI_PUBLIC available */
#include "base/systems.h"
#include "libadminutil/resource.h"

typedef res_RESOURCE_TABLE RESOURCE_TABLE;
typedef res_RESOURCE_GLOBAL RESOURCE_GLOBAL;

/*******************************************************************************/

/*
 * In accordance with the recommendations in the 
 * "Netscape Coding Standard for Server Internationalization",
 * the following aliases are defined for fprintf, et al., and
 * these aliases should be used to clearly indicate the intended
 * destination for output.
 */

#define AdminFprintf  fprintf
#define DebugFprintf  fprintf

#define ClientSprintf sprintf
#define AdminSprintf  sprintf
#define DebugSprintf  sprintf

#define ClientFputs   fputs
#define AdminFputs    fputs
#define DebugFputs    fputs

/* more #define, as needed */

/*******************************************************************************/

/*
 * Function prototypes for application and libraries
 */


#ifdef __cplusplus
extern "C" 
{
#endif

/***************************/
/* XP_InitStringDatabase() */
/***************************/

NSAPI_PUBLIC
void
XP_InitStringDatabase(char* pathCWD, char* databaseName);

/* Initialize the resource string database */

/******************************/
/* XP_GetStringFromDatabase() */
/******************************/

NSAPI_PUBLIC
extern char*
XP_GetStringFromDatabase(char* strLibraryName,
                         char* strLanguage,
                         int iToken);

/* Given the LibraryName, Language and Token, extracts the string corresponding
   to that library and token from the database in the language requested and
   returns a pointer to the string.  Note: Use the macros XP_GetClientStr() and
   XP_GetAdminStr() defined below to simplify source code. */

/*****************/
/* SetLanguage() */
/*****************/
enum
{
	CLIENT_LANGUAGE,
	ADMIN_LANGUAGE,
	DEFAULT_LANGUAGE
};

NSAPI_PUBLIC
extern void
SetLanguage(int type, char *language);

/* Set language for Client, Admin and Default, XP_GetStringFromDatabase will
   base on the setting to retrieve correct string for specific language */
 
/***********************/
/* GetClientLanguage() */
/***********************/

NSAPI_PUBLIC
extern char*
GetClientLanguage(void);

/* Returns a pointer to a string with the name of the language requested by
   the current client; intended to be passed to XP_GetStringFromDatabase()
   and used by the front end macro XP_GetClientStr(). */

/**********************/
/* GetAdminLanguage() */
/**********************/

NSAPI_PUBLIC
extern char*
GetAdminLanguage(void);

/* Returns a pointer to a string with the name of the language requested by
   the administrator; intended to be passed to XP_GetStringFromDatabase()
   and used by the front end macro XP_GetAdminStr(). */

/************************/
/* GetDefaultLanguage() */
/************************/

NSAPI_PUBLIC
extern char*
GetDefaultLanguage(void);

/* Returns a pointer to a string with the name of the default language
   for the installation from the configuration file. */

/************************/
/* GetFileForLanguage() */
/************************/

NSAPI_PUBLIC
int
GetFileForLanguage(char* filepath,char* language,char* existingFilepath);

/* Looks for a file in the appropriate language.

   Input: filePath,language
   filePath is of the form "/xxx/xxx/$$LANGDIR/xxx/xxx/filename"
            or of the form "/xxx/xxx/xxx/xxx/filename".
   filename may or may not have an extension.
   language is an Accept-Language list; each language-range will be
     tried as a subdirectory name and possibly as a filename modifier.
     "*" is ignored - default always provided if needed.
     "-" is replaced by "_".
   $$LANGDIR is a special string replaced by language. It is optional.
     For the default case, $$LANGDIR/ is replaced by nothing
     (so // is not created).
   
   Returned: existingPath
   existingFilePath is the path of a satisfactory, existing file.
   if no file is found, an empty string "" is returned.
   
   int returned: -1 if no file found (existingFilePath = "")
                  0 if default file is returned
                  1 if language file is returned (any in list) */

/********************/
/* XP_AccLangList() */
/********************/

#define MAX_ACCEPT_LANGUAGE 16
#define MAX_ACCEPT_LENGTH 18

typedef char ACCEPT_LANGUAGE_LIST[MAX_ACCEPT_LANGUAGE][MAX_ACCEPT_LENGTH];

NSAPI_PUBLIC
int
XP_AccLangList(char* AcceptLanguage,
               ACCEPT_LANGUAGE_LIST AcceptLanguageList);

#ifdef __cplusplus
}
#endif


/*******************************************************************************/

/*
 * Function prototypes for building string database
 */

extern int XP_MakeStringDatabase(void);

/* Used to create the string database at build time; not used by the application
   itself.  Returns 0 is successful. */

extern void XP_PrintStringDatabase(void);

/* DEBUG: Prints out entire string database to standard output. */

/*******************************************************************************/

/*
 * Macros to simplify calls to XP_GetStringFromDatabase
 * (need one argument instead of three)
 */

#define XP_GetClientStr(DBTTokenName)                  \
        XP_GetStringFromDatabase(LIBRARY_NAME,         \
                                 GetClientLanguage(),  \
                                 DBTTokenName)

#define XP_GetAdminStr(DBTTokenName)                   \
        XP_GetStringFromDatabase(LIBRARY_NAME,         \
                                 "en",   \
                                 DBTTokenName)

/*******************************************************************************/


/*******************************************************************************/

/*
 * Define the ResDef macro to simplify the maintenance of strings which are to
 * be added to the library or application header file (dbtxxx.h). This enables
 * source code to refer to the strings by theit TokenNames, and allows the
 * strings to be stored in the database.
 *
 * Usage:   ResDef(TokenName,TokenValue,String)
 *
 * Example: ResDef(DBT_HelloWorld_, \
 *                 1,"Hello, World!")
 *          ResDef(DBT_TheCowJumpedOverTheMoon_, \
 *                 2,"The cow jumped over the moon.")
 *          ResDef(DBT_TheValueOfPiIsAbout31415926536_, \
 *                 3,"The value of PI is about 3.1415926536."
 *
 * RESOURCE_STR is used by makstrdb.c only.  It is not used by getstrdb.c or
 * in library or application source code.
 */
 
#if 0
#define BEGIN_STR(argLibraryName) \
                          enum {
#define ResDef(argToken,argID,argString) \
                          argToken = argID,
#define END_STR(argLibraryName) \
                          argLibraryName ## top };

#endif
/*******************************************************************************/

#endif
