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
/* uniqueidgen.c  - implementation for uniqueID generator */

#include <string.h>

#ifndef _WIN32			 /* for ntoh* functions */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#endif

#include "nspr.h"
#include "slap.h"
#include "uuid.h"

#define MODULE "uniqueid generator"

/* converts from guid -> UniqueID */
/* static void uuid2UniqueID (const guid_t *uuid, Slapi_UniqueID *uId); */
/* converts from UniqueID -> guid */
/* static void uniqueID2uuid (const Slapi_UniqueID *uId, guid_t *uuid); */
/* validates directory */
static int  validDir (const char *configDir);

/* Function:    uniqueIDGenInit
   Description: this function initializes the generator
   Parameters:  configDir - directory in which generators state is stored
				configDN - DIT entry with state information
				mtGen - indicates whether multiple threads will use generator
   Return:      UID_SUCCESS if function succeeds
                UID_BADDATA if invalif directory is passed
                UID_SYSTEM_ERROR if any other failure occurs 
*/
int uniqueIDGenInit (const char *configDir, const Slapi_DN *configDN, PRBool mtGen)
{
    int rt;
    if ((configDN == NULL && (configDir == NULL || !validDir(configDir))) ||
        (configDN && configDir))
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDGenInit: invalid arguments\n");
		
        return UID_BADDATA;
	}

    rt = uuid_init (configDir, configDN, mtGen);

    if (rt == UUID_SUCCESS)
        return UID_SUCCESS;
    else
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDGenInit: "
						 "generator initialization failed\n");
        return UID_SYSTEM_ERROR;
	}
}

/* Function:    uniqueIDGenCleanup
   Description: cleanup
   Parameters:  none
   Return:      none
*/
void uniqueIDGenCleanup (){
    uuid_cleanup ();
}

/* Function:    slapi_uniqueIDGenerate    
   Description: this function generates UniqueID; exposed to the plugins.
   Parameters:  uId - structure in which new id will be return 
   Return:      UID_SUCCESS, if operation is successful
                UID_BADDATA, if null pointer is passed to the function 
                UID_SYSTEM_ERROR, if update to persistent storage failed 
*/

int slapi_uniqueIDGenerate (Slapi_UniqueID *uId){
    int    rt;

    if (uId == NULL)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDGenerate: "
						 "NULL paramter is passed to the function.\n");
        return UID_BADDATA;
	}

    rt = uuid_create(uId); 
    if (rt != UUID_SUCCESS)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDGenerate: "
						 "id generation failed.\n");
        return UID_SYSTEM_ERROR;
	}
    return UID_SUCCESS;
}

/* Function:    slapi_uniqueIDGenerateString    
   Description: this function generates uniqueid an returns it as a string
                This function returns the data in the format generated by 
                slapi_uniqueIDFormat.
   Parameters:  uId - buffer to receive the ID.	Caller is responsible for
				freeing uId buffer.
   Return:      UID_SUCCESS if function succeeds;
                UID_BADDATA if invalid pointer passed to the function;
				UID_MEMORY_ERROR if malloc fails;
                UID_SYSTEM_ERROR update to persistent storage failed. 
*/

int slapi_uniqueIDGenerateString (char **uId)
{
	Slapi_UniqueID uIdTemp;
	int rc;

	rc = slapi_uniqueIDGenerate (&uIdTemp);

	if (rc != UID_SUCCESS)
		return rc;

	rc = slapi_uniqueIDFormat (&uIdTemp, uId);
	
	return rc;
}

/* Function:	slapi_uniqueIDGenerateFromName
   Description:	this function generates an id from name. See uuid
				draft for more details. This function is thread safe.
   Parameters:	uId		- generated id
				uIDBase - uid used for generation to distinguish different
				name - buffer containing name from which to generate the id
				namelen - length of the name buffer
				name spaces
   Return:		UID_SUCCESS if function succeeds
				UID_BADDATA if invalid argument is passed to the
				function.
*/

int slapi_uniqueIDGenerateFromName (Slapi_UniqueID *uId, const Slapi_UniqueID *uIdBase, 
									const void *name, int namelen)
{
	if (uId == NULL || uIdBase == NULL || name == NULL || namelen <= 0)
	{
		slapi_log_error (SLAPI_LOG_FATAL, MODULE, "uniqueIDGenerateMT: "
						 "invalid paramter is passed to the function.\n");
		return UID_BADDATA;
	}

	uuid_create_from_name(uId, *uIdBase, name, namelen);

	return UID_SUCCESS;
}

/* Function:	slapi_uniqueIDGenerateFromName
   Description:	this function generates an id from a name and returns
                it in the string format. See uuid draft for more
				details. This function can be used in both a
				singlethreaded and a multithreaded environments.
   Parameters:	uId		- generated id in string form
				uIDBase - uid used for generation to distinguish among 
				different name spaces in string form; NULL means to use
				empty id as the base.
				name - buffer containing name from which to generate the id
				namelen - length of the name buffer
   Return:		UID_SUCCESS if function succeeds
				UID_BADDATA if invalid argument is passed to the
				function.
*/

int slapi_uniqueIDGenerateFromNameString (char **uId, 
										  const char *uIdBase, 
										  const void *name, int namelen)
{
	int rc;
	Slapi_UniqueID idBase;
	Slapi_UniqueID idGen;

	/* just use Id of all 0 as base id */
	if (uIdBase == NULL)
	{
		memset (&idBase, 0, sizeof (idBase));
		memset (&idGen, 0, sizeof (idGen));
	}
	else
	{
		rc = slapi_uniqueIDScan (&idBase, uIdBase);
		if (rc != UID_SUCCESS)
		{
			return rc;
		}
	}

	rc = slapi_uniqueIDGenerateFromName (&idGen, &idBase, name, namelen);
	if (rc != UID_SUCCESS)
	{
		return rc;
	}

	rc = slapi_uniqueIDFormat (&idGen, uId);

	return rc;
}

/* helper fumctions */

static int validDir(const char *configDir){
    PRDir* dir;

    /* empty string means this directory */
    if (strlen(configDir) == 0)
		return 1;
    dir = PR_OpenDir(configDir);
    if (dir){
        PR_CloseDir (dir);
        return 1;
    }

    return 0;
}
