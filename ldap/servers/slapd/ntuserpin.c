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
/******************************************************
 *
 *  ntuserpin.c - Prompts for the key
 *  database passphrase.
 *
 ******************************************************/

#if defined( _WIN32 )

#include <windows.h>
#include "ntwatchdog.h"
#include "slapi-plugin.h"
#include "fe.h"

#undef Debug
#undef OFF
#undef LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"
#include "fe.h"

static int i=0;
static int cbRemotePassword = 0;

extern char* NT_PromptForPin(const char *tokenName);

static const char nt_retryWarning[] =
"Warning: You entered an incorrect PIN. Incorrect PIN may result in disabling the token";

struct SVRCORENTUserPinObj
{
  SVRCOREPinObj base;
};
static const struct SVRCOREPinMethods vtable;

/* ------------------------------------------------------------ */
SVRCOREError
SVRCORE_CreateNTUserPinObj(SVRCORENTUserPinObj **out)
{
  SVRCOREError err = 0;
  SVRCORENTUserPinObj *obj = 0;

  do {
    obj = (SVRCORENTUserPinObj*)malloc(sizeof (SVRCORENTUserPinObj));
    if (!obj) { err = 1; break; }

    obj->base.methods = &vtable;

  } while(0);

  if (err)
  {
    SVRCORE_DestroyNTUserPinObj(obj);
    obj = 0;
  }

  *out = obj;
  return err;
}

void
SVRCORE_DestroyNTUserPinObj(SVRCORENTUserPinObj *obj)
{
  if (obj) free(obj);
}

static void destroyObject(SVRCOREPinObj *obj)
{
  SVRCORE_DestroyNTUserPinObj((SVRCORENTUserPinObj*)obj);
}

/* First try to retrieve the password from the watchdog,
   if this is not available, prompt for the passphrase. */

static char *getPin(SVRCOREPinObj *obj, const char *tokenName, PRBool retry)
{
    HWND hwndRemote;
    char *szRemotePassword = NULL;
    HANDLE hRemoteProcess;
    DWORD dwNumberOfBytesRead=0;
    DWORD dwNumberOfBytesWritten=0;
    PK11_PIN *buf= NULL;
    char *password = NULL;
    char pin[MAX_PASSWORD];
    BOOL ret;
    DWORD err = 0;

    // Find Watchdog application window
    if( pszServerName && (hwndRemote = FindWindow("slapd", pszServerName)) && 
	(hRemoteProcess = (HANDLE)GetWindowLong( hwndRemote, 
	GWL_PROCESS_HANDLE)))
    {
	cbRemotePassword = GetWindowLong(hwndRemote, GWL_PASSWORD_LENGTH);
	szRemotePassword = (HANDLE)GetWindowLong(hwndRemote, GWL_PASSWORD_ADDR);
	
	// if retry, don't get the pin from watchdog 
    	if (retry)
    	{
            MessageBox(GetDesktopWindow(), nt_retryWarning,
                        "Fedora Server", MB_ICONEXCLAMATION | MB_OK);
	} else {
	    if((cbRemotePassword != 0) && (szRemotePassword != 0))
	    {
	        buf = (PK11_PIN *)slapi_ch_malloc(sizeof
		    (PK11_PIN)*cbRemotePassword);
	    	if(ReadProcessMemory(hRemoteProcess, szRemotePassword, 
		    (LPVOID)buf,sizeof(PK11_PIN)*cbRemotePassword , 
		    &dwNumberOfBytesRead))
	        {

		    for (i=0; i < cbRemotePassword; i++) {
		        if (strncmp (tokenName, buf[i].TokenName,
				buf[i].TokenLength)==0) 
			{
			    memset(pin, '\0', MAX_PASSWORD);
		    	    PL_strncpyz (pin, buf[i].Password, sizeof(pin));
	    		    slapi_ch_free ((void **) &buf);
			    return slapi_ch_strdup(pin);
		        }
		    }
	        }
	    }
        }
    }

   /* Didn't get the password from Watchdog, or this is a retry,
    prompt the user. */

   password = NT_PromptForPin(tokenName);

   /* Store the password back to nt watchdog */
    if (password != NULL && hwndRemote && hRemoteProcess) 
    {
	slapi_ch_free ((void **) &buf);
	buf = (PK11_PIN *)slapi_ch_malloc(sizeof(PK11_PIN));
	PL_strncpyz (buf[0].TokenName, tokenName, sizeof(buf[0].TokenName));
	buf[0].TokenLength=strlen(buf[0].TokenName);
	PL_strncpyz (buf[0].Password, password, sizeof(buf[0].Password));
	buf[0].PasswordLength=strlen(buf[0].Password);
	if (i== cbRemotePassword)
    	{
	    /* Add a new token and password to the end of the table.*/

	    SetWindowLong(hwndRemote, GWL_PASSWORD_LENGTH, 
	    (LONG)cbRemotePassword+1);
	    ret = WriteProcessMemory(hRemoteProcess, 
	    szRemotePassword+cbRemotePassword*sizeof(PK11_PIN),
	    (LPVOID)buf, sizeof(PK11_PIN), &dwNumberOfBytesWritten);
	    if( !ret )
		err = GetLastError();
	} else {
	    /* This is a retry due to a wrong password stored in watchdog. */
	    ret  = WriteProcessMemory(hRemoteProcess, 
		szRemotePassword+i*sizeof(PK11_PIN),(LPVOID)buf,
        	sizeof(PK11_PIN), &dwNumberOfBytesWritten);
	    if( !ret )
		err = GetLastError();
	}
    }
    slapi_ch_free ((void **) &buf);
    return (password);
}

/*
 * VTable
 */
static const SVRCOREPinMethods vtable =
{ 0, 0, destroyObject, getPin };
#endif /* defined( _WIN32 ) */
