/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/****************************************************************************
    PROGRAM: pmddeml.c

    PURPOSE: DDEML interface with ProgMan

****************************************************************************/

#include <windows.h>    // required for all Windows applications
#include <ddeml.h>      // required for DDEML
#include <stdio.h>     // required for strcpy and strlen
#include "nt/ntos.h"    // specific to this program

BOOL PMDDEML_SendShellCommand (DWORD idInst, LPSTR lpCommand);

HDDEDATA CALLBACK PMDDEML_DdeCallback(  UINT  uType,	// transaction type
                                UINT  uFmt,	    // clipboard data format
                                HCONV  hconv,	// handle of the conversation
                                HSZ  hsz1,	    // handle of a string
                                HSZ  hsz2,	    // handle of a string
                                HDDEDATA  hdata,// handle of a global memory object
                                DWORD  dwData1,	// transaction-specific data
                                DWORD  dwData2 	// transaction-specific data
                               );	
/****************************************************************************
    FUNCTION: PMDDEML_Open()

    PURPOSE:  Open PMDDEML interface

    PARAMETERS:

    RETURNS:
        DWORD   handle used in subsequent calls
****************************************************************************/

DWORD PMDDEML_Open ( void )
{
    DWORD idInst = 0L;      // instance identifier

    // register this app with the DDEML
    if (DdeInitialize(&idInst,                      // receives instance ID
                      (PFNCALLBACK)PMDDEML_DdeCallback,       // address of callback function
                      APPCMD_CLIENTONLY,            // this is a client app
                      0L))                          // reserved
        return 0;
    return idInst;
}
/****************************************************************************
    FUNCTION: PMDDEML_Close()

    PURPOSE:  Closes PMDDEML interface

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_Close ( DWORD idInst )
{
    // free all DDEML resources associated with this app
    return DdeUninitialize ( idInst );
}
/****************************************************************************
    FUNCTION: PMDDEML_CreateProgramManagerGroup()

    PURPOSE:  Creates a program group

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open
        LPCTSTR lpszGroupNamee      name of group

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_CreateProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszGroupName == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[CreateGroup(%s)]", lpszGroupName );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}
/****************************************************************************
    FUNCTION: PMDDEML_DeleteProgramManagerGroup()

    PURPOSE:  Deletes a program group

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open
        LPCTSTR lpszGroupNamee      name of group

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_DeleteProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszGroupName == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[DeleteGroup(%s)]", lpszGroupName );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

BOOL PMDDEML_DeleteProgramCommonManagerGroup ( DWORD idInst,
											   LPCTSTR lpszGroupName )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszGroupName == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[DeleteGroup(%s,1)]", lpszGroupName );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

/****************************************************************************
    FUNCTION: PMDDEML_ShowProgramManagerGroup()

    PURPOSE:  Deletes a program group

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open
        LPCTSTR lpszGroupNamee      name of group

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_ShowProgramManagerGroup ( DWORD idInst, LPCTSTR lpszGroupName )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszGroupName == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[ShowGroup(%s,1)]", lpszGroupName );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

BOOL PMDDEML_ShowProgramManagerCommonGroup ( DWORD idInst,
											 LPCTSTR lpszGroupName )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszGroupName == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[ShowGroup(%s,1,1)]", lpszGroupName );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

/****************************************************************************
    FUNCTION: PMDDEML_AddIconToProgramManagerGroup()

    PURPOSE:  Deletes icon a program group

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open
        LPCTSTR lpszCmdLine           title of icon in group
        LPCTSTR lpszTitle           title of icon in group
        LPCTSTR lpszWorkingDir           title of icon in group
        BOOL bReplace               True, if icon should be replaced

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_AddIconToProgramManagerGroup ( DWORD idInst, LPCTSTR lpszCmdLine,
                 LPCTSTR lpszTitle, LPCTSTR lpszIconPath, LPCTSTR lpszWorkingDir, BOOL bReplace )
{
    char szDDEMsg[256];      // instance identifier

    if ( ( lpszCmdLine == NULL ) || ( lpszTitle == NULL ) || ( lpszWorkingDir == NULL )  )
        return FALSE;

    if ( bReplace ) {
        sprintf ( szDDEMsg, "[ReplaceItem(%s)]", lpszTitle );
        PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg );
    }

    sprintf ( szDDEMsg, "[AddItem(%s,%s,%s,,,,%s)]", lpszCmdLine, lpszTitle,
                         lpszIconPath, lpszWorkingDir );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

/****************************************************************************
    FUNCTION: PMDDEML_DeleteIconInProgramManagerGroup()

    PURPOSE:  Deletes icon a program group

    PARAMETERS:
        DWORD idInst                handle returned by  PMDDEML_Open
        LPCTSTR lpszTitle           title of icon in group

    RETURNS:
        TRUE, if successful                        
****************************************************************************/

BOOL PMDDEML_DeleteIconInProgramManagerGroup ( DWORD idInst, LPCTSTR lpszTitle )
{
    char szDDEMsg[256];      // instance identifier

    if ( lpszTitle == NULL )
        return FALSE;

    sprintf ( szDDEMsg, "[DeleteItem(%s)]", lpszTitle );

    if ( !PMDDEML_SendShellCommand(idInst, (LPSTR)szDDEMsg ) )
        return FALSE;
    return TRUE;
}

/****************************************************************************
    FUNCTION: PMDDEML_DdeCallback()

    PURPOSE:  Processes messages for DDEML conversation

    PARAMETERS:
        UINT  uType,	// transaction type
        UINT  uFmt,	    // clipboard data format
        HCONV  hconv,	// handle of the conversation
        HSZ  hsz1,	    // handle of a string
        HSZ  hsz2,	    // handle of a string
        HDDEDATA  hdata,// handle of a global memory object
        DWORD  dwData1,	// transaction-specific data
        DWORD  dwData2 	// transaction-specific data

    RETURNS:
        HDDEDATA
****************************************************************************/

HDDEDATA CALLBACK PMDDEML_DdeCallback(  UINT  uType,	// transaction type
                                UINT  uFmt,	    // clipboard data format
                                HCONV  hconv,	// handle of the conversation
                                HSZ  hsz1,	    // handle of a string
                                HSZ  hsz2,	    // handle of a string
                                HDDEDATA  hdata,// handle of a global memory object
                                DWORD  dwData1,	// transaction-specific data
                                DWORD  dwData2 	// transaction-specific data
                               )	
{
    // Nothing need be done here...
    return (HDDEDATA)NULL;
}


/****************************************************************************
    FUNCTION: PMDDEML_SendShellCommand()

    PURPOSE:  Sends the given command string to Program Manager

    PARAMETERS:
        LPSTR - pointer to command string

    RETURNS:
        BOOL  - TRUE if this function succeeds, FALSE otherwise
****************************************************************************/

BOOL PMDDEML_SendShellCommand (DWORD idInst,    // instance identifier
                       LPSTR lpCommand) // command string to execute
{
    HSZ      hszServTop;    // Service and Topic name are "PROGMAN"
    HCONV    hconv;         // handle of conversation
    int      nLen;          // length of command string
    HDDEDATA hData;         // return value of DdeClientTransaction
    DWORD    dwResult;      // result of transaction
    BOOL     bResult=FALSE; // TRUE if this function is successful

    // create string handle to service/topic
    hszServTop = DdeCreateStringHandle(idInst, "PROGMAN", CP_WINANSI);

    // attempt to start conversation with server app
    if ((hconv = DdeConnect(idInst, hszServTop, hszServTop, NULL))!= NULL)
    {
        // get length of the command string
        nLen = lstrlen((LPSTR)lpCommand);

        // send command to server app
        hData = DdeClientTransaction((LPBYTE)lpCommand, // data to pass
                                     nLen + 1,          // length of data
                                     hconv,             // handle of conversation
                                     NULL,              // handle of name-string
                                     CF_TEXT,           // clipboard format
                                     XTYP_EXECUTE,      // transaction type
                                     1000,              // timeout duration
                                     &dwResult);        // points to transaction result

        if (hData)
            bResult = TRUE;

        // end conversation
        DdeDisconnect(hconv);
    }

    // free service/topic string handle
    DdeFreeStringHandle(idInst, hszServTop);

    return bResult;
}


/****************************************************************************
    FUNCTION: PMDDEML_GetProgramGroupInfo()

    PURPOSE:  Gets group info from progman

    PARAMETERS:
        LPSTR - pointer to command string

    RETURNS:
        BOOL  - TRUE if this function succeeds, FALSE otherwise
****************************************************************************/
BOOL PMDDEML_GetProgramGroupInfo(DWORD idInst, LPSTR lpProgramGroup, char *szBuffer, DWORD cbBuffer)
{
    HSZ      hszServTop;    // Service and Topic name are "PROGMAN"
    HSZ      hszTopic;      // Topic name is the lpRequest
    HCONV    hconv;         // handle of conversation
    HDDEDATA hData = 0;     // return value of DdeClientTransaction
    DWORD    dwResult;      // result of transaction
    BOOL     bResult=FALSE; // TRUE if this function is successful

    hszServTop = DdeCreateStringHandle(idInst, "PROGMAN", CP_WINANSI);
    hszTopic = DdeCreateStringHandle(idInst, lpProgramGroup, CP_WINANSI);

    if((hconv = DdeConnect(idInst, hszServTop, hszServTop, NULL)) != NULL)
    {
        hData = DdeClientTransaction((LPBYTE)NULL,      // data to pass
                                     0L,                // length of data
                                     hconv,             // handle of conversation
                                     hszTopic,          // handle of name-string
                                     CF_TEXT,           // clipboard format
                                     XTYP_REQUEST,      // transaction type
                                     5000,              // timeout duration
                                     &dwResult);        // points to transaction result

        bResult = (BOOL)DdeGetData(hData, (void FAR*)szBuffer, cbBuffer, 0);
        DdeDisconnect(hconv);
    }

    // free service/topic string handle
    DdeFreeStringHandle(idInst, hszServTop);
    DdeFreeStringHandle(idInst, hszTopic);

    return bResult;
}
