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
  res_RESOURCE_TABLE  *restable;
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
 
#ifdef  RESOURCE_STR
#define BEGIN_STR(argLibraryName) \
                          RESOURCE_TABLE argLibraryName[] = { {0, #argLibraryName},
#define ResDef(argToken,argID,argString) \
                          {argID, argString},
#define END_STR(argLibraryName) \
                          {0, 0} };
#else
#define BEGIN_STR(argLibraryName) \
                          enum {
#define ResDef(argToken,argID,argString) \
                          argToken = argID,
#define END_STR(argLibraryName) \
                          argLibraryName ## top };
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


/******************************/
/* XP_GetStringFromDatabase() */
/******************************/

NSAPI_PUBLIC
extern const char*
XP_GetStringFromDatabase(const char* strLibraryName,
                         const char* strLanguage,
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

#define XP_GetAdminStr(DBTTokenName)                   \
        XP_GetStringFromDatabase(LIBRARY_NAME,         \
                                 "en",   \
                                 DBTTokenName)

/*******************************************************************************/

#endif
