/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/******************************************************
 *
 *  ntuserpin.c - Prompts for the key
 *  database passphrase.
 *
 ******************************************************/

#if defined( _WIN32 ) && defined ( NET_SSL )

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
                        "Brandx Server", MB_ICONEXCLAMATION | MB_OK);
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
		    	    strncpy (pin, buf[i].Password,
					buf[i].PasswordLength);
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
	strcpy (buf[0].TokenName, tokenName);
	buf[0].TokenLength=strlen(tokenName);
	strcpy (buf[0].Password, password);
	buf[0].PasswordLength=strlen(password);
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
#endif /* defined( _WIN32 ) && defined ( NET_SSL ) */
