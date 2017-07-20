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


#ifndef I18N_H
#define I18N_H

/* Make NSAPI_PUBLIC available */
#include "base/systems.h"

/* This stuff was copied from libadminutil/resource.h so we could
   remove the dependency on adminutil which is not being open sourced
   this first round.
*/
#ifndef COPIED_FROM_LIBADMINUTIL_RESOURCE_H
/* Resource contains the name of the
   property file w/ paht information
*/
typedef struct
{
    char *path;
    char *package;
    void *propset;
} Resource;

/*******************************************************************************/
/*
 * this table contains library name
 * (stored in the first string entry, with id=0),
 * and the id/string pairs which are used by library
 */

typedef struct res_RESOURCE_TABLE
{
    int id;
    char *str;
} res_RESOURCE_TABLE;

/*******************************************************************************/

/*
 * resource global contains resource table list which is used
 * to generate the database.
 * Also used for "in memory" version of XP_GetStringFromDatabase()
 */

typedef struct res_RESOURCE_GLOBAL
{
    res_RESOURCE_TABLE *restable;
} res_RESOURCE_GLOBAL;

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

#ifdef RESOURCE_STR
#define BEGIN_STR(argLibraryName) \
    RESOURCE_TABLE argLibraryName[] = {{0, #argLibraryName},
#define ResDef(argToken, argID, argString) \
    {argID, argString},
#define END_STR(argLibraryName) \
    {                           \
        0, 0                    \
    }                           \
    }                           \
    ;
#else
#define BEGIN_STR(argLibraryName) \
    enum                          \
    {
#define ResDef(argToken, argID, argString) \
    argToken = argID,
#define END_STR(argLibraryName) \
    argLibraryName##top         \
    }                           \
    ;
#endif

#endif /* COPIED_FROM_LIBADMINUTIL_RESOURCE_H */

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

#define AdminFprintf fprintf
#define DebugFprintf fprintf

#define ClientSprintf sprintf
#define AdminSprintf sprintf
#define DebugSprintf sprintf

#define ClientFputs fputs
#define AdminFputs fputs
#define DebugFputs fputs

/* more #define, as needed */

/*******************************************************************************/

/*
 * Function prototypes for application and libraries
 */


#ifdef __cplusplus
extern "C" {
#endif


/******************************/
/* XP_GetStringFromDatabase() */
/******************************/

NSAPI_PUBLIC
extern const char *
XP_GetStringFromDatabase(const char *strLibraryName,
                         const char *strLanguage,
                         int iToken);

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

#define XP_GetAdminStr(DBTTokenName)       \
    XP_GetStringFromDatabase(LIBRARY_NAME, \
                             "en",         \
                             DBTTokenName)

/*******************************************************************************/

#endif
