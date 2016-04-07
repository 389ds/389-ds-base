/*
 * Copyright (C) 1998 Netscape Communications Corporation.
 * All Rights Reserved.
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/***********************************************************
 *
 *  ntgetpin.c - Prompts for the key database passphrase.
 *
 ***********************************************************/

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if defined( _WIN32 ) 

#include <windows.h>
#include <nspr.h>
#include "ntresource.h"

#undef Debug
#undef OFF
#undef LITTLE_ENDIAN

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

static char password[512];

static void CenterDialog(HWND hwndParent, HWND hwndDialog)
{
    RECT DialogRect;
    RECT ParentRect;
    POINT Point;
    int nWidth;
    int nHeight;
    
    // Determine if the main window exists. This can be useful when
    // the application creates the dialog box before it creates the
    // main window. If it does exist, retrieve its size to center
    // the dialog box with respect to the main window.
    if( hwndParent != NULL ) 
	{
		GetClientRect(hwndParent, &ParentRect);
    } 
	else 
	{
		// if main window does not exist, center with respect to desktop
		hwndParent = GetDesktopWindow();
		GetWindowRect(hwndParent, &ParentRect);
    }
    
    // get the size of the dialog box
    GetWindowRect(hwndDialog, &DialogRect);
    
    // calculate height and width for MoveWindow()
    nWidth = DialogRect.right - DialogRect.left;
    nHeight = DialogRect.bottom - DialogRect.top;
    
    // find center point and convert to screen coordinates
    Point.x = (ParentRect.right - ParentRect.left) / 2;
    Point.y = (ParentRect.bottom - ParentRect.top) / 2;
    
    ClientToScreen(hwndParent, &Point);
    
    // calculate new X, Y starting point
    Point.x -= nWidth / 2;
    Point.y -= nHeight / 2;
    
    MoveWindow(hwndDialog, Point.x, Point.y, nWidth, nHeight, FALSE);
}

static BOOL CALLBACK PinDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message) 
	{
      case WM_INITDIALOG:
				SetDlgItemText( hDlg, IDC_TOKEN_NAME, (char *)lParam);
				CenterDialog(NULL, hDlg);
				SendDlgItemMessage(hDlg, IDEDIT, EM_SETLIMITTEXT, sizeof(password), 0);
				EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
				return(FALSE);
	
      case WM_COMMAND:
				if(LOWORD(wParam) == IDEDIT) 
				{
					if(HIWORD(wParam) == EN_CHANGE) 
					{
						if(GetDlgItemText(hDlg, IDEDIT, password,
								  sizeof(password)) > 0) 
						{
							EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
						} 
						else 
						{
							EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
						}
					}
					return (FALSE);
				} 
				else if(LOWORD(wParam) == IDOK) 
				{
					GetDlgItemText(hDlg, IDEDIT, password, sizeof(password));
					EndDialog(hDlg, IDOK);
					return (TRUE);
				} 
				else if(LOWORD(wParam) == IDCANCEL) 
				{
					memset(password, 0, sizeof(password));
					EndDialog(hDlg, IDCANCEL);
					return(FALSE);
				}
    }
    return (FALSE);
}
char*
NT_PromptForPin (const char *tokenName)
{
    int iResult = 0;
    
    iResult = DialogBoxParam( GetModuleHandle( NULL ), 
	MAKEINTRESOURCE(IDD_DATABASE_PASSWORD),
	HWND_DESKTOP, (DLGPROC) PinDialogProc, (LPARAM)tokenName);
    if( iResult == -1 ) 
	{
		iResult = GetLastError();
/*
		ReportSlapdEvent( EVENTLOG_INFORMATION_TYPE, 
			MSG_SERVER_PASSWORD_DIALOG_FAILED, 0, NULL );
*/
		return NULL;
    }
    /* Return no-response if the user click on cancel */
    if (password[0] == 0) return 0;
    return strdup(password);
}

#endif /* defined( _WIN32 )  */
