/* --- BEGIN COPYRIGHT BLOCK ---
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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

// Created: 2-8-2005
// Author(s): Scott Bridges
#include <windows.h>
#include <ntsecapi.h>
// Work around for enum redefinition
// Effects nssILockOp enumeration in nssilckt.h
#define Unlock Unlock_ntsecapi
#include "../passhand.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)
#endif

DWORD WINAPI SavePasshookChange( LPVOID passinfo );
static HANDLE passhookMutexHandle;
static unsigned long logLevel;

NTSTATUS NTAPI PasswordChangeNotify(PUNICODE_STRING UserName, ULONG RelativeId, PUNICODE_STRING Password)
{
	PASS_INFO *newPassInfo = NULL;
	HANDLE passhookThreadHandle;
	fstream outLog;
	DWORD waitRes;

	// This memory will be freed in SavePasshookChange
	if ( newPassInfo = (PASS_INFO *) malloc(sizeof(PASS_INFO)) ) {
		// These get freed in SavePasshookChange by calling clearSet
		newPassInfo->username = (char*)malloc((UserName->Length / 2) + 1);
		newPassInfo->password = (char*)malloc((Password->Length / 2) + 1);
	} else {
		goto exit;
	}

	// Fill in the password change struct
	if (newPassInfo->username && newPassInfo->password) {
                _snprintf(newPassInfo->username, (UserName->Length / 2), "%S", UserName->Buffer);
                _snprintf(newPassInfo->password, (Password->Length / 2), "%S", Password->Buffer);
                newPassInfo->username[UserName->Length / 2] = '\0';
                newPassInfo->password[Password->Length / 2] = '\0';

		// Backoff
                newPassInfo->backoffCount = 0;

                // Load time
                time(&(newPassInfo->atTime));
	} else {
		// Memory error.  Free everything we allocated.
		free(newPassInfo->username);
		free(newPassInfo->password);
		free(newPassInfo);
		goto exit;
	}

	// Fire off a thread to do the real work
	passhookThreadHandle = CreateThread(NULL, 0, SavePasshookChange, newPassInfo, 0, NULL); 

	// We need to close the handle to the thread we created.  Doing
	// this will not terminate the thread.
	if (passhookThreadHandle != NULL) {
		CloseHandle(passhookThreadHandle);
	} else {
		// Acquire the mutex so we can log an error
		waitRes = WaitForSingleObject(passhookMutexHandle, PASSHOOK_TIMEOUT);

		// If we got the mutex, log the error, otherwise it's not safe to log
		if (waitRes == WAIT_OBJECT_0) {
                	outLog.open("passhook.log", ios::out | ios::app);

		        if(outLog.is_open()) {
       		         	timeStamp(&outLog);
		                outLog << "Failed to start thread.  Aborting change for " << newPassInfo->username << endl;
       		 	}

			outLog.close();

			// Release mutex
			ReleaseMutex(passhookMutexHandle);
		}
	}

exit:
	return STATUS_SUCCESS;
}

BOOL NTAPI PasswordFilter(PUNICODE_STRING UserName, PUNICODE_STRING FullName, PUNICODE_STRING Password, BOOL SetOperation)
{
	return TRUE;
}

BOOL NTAPI InitializeChangeNotify()
{
	HKEY regKey;
	DWORD type;
	unsigned long buffSize;
	char regBuff[PASSHAND_BUF_SIZE];
	fstream outLog;

	// check if logging is enabled
	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\PasswordSync", &regKey);
        buffSize = PASSHAND_BUF_SIZE;
        if(RegQueryValueEx(regKey, "Log Level", NULL, &type, (unsigned char*)regBuff, &buffSize) == ERROR_SUCCESS)
        {
                logLevel = (unsigned long)atoi(regBuff);
        }
        else
        {
                logLevel = 0;
        }
        RegCloseKey(regKey);

	// Create mutex for passhook data file and log file access
	passhookMutexHandle = CreateMutex(NULL, FALSE, PASSHOOK_MUTEX_NAME);

	if (passhookMutexHandle == NULL) {
		// Log an error.
		outLog.open("passhook.log", ios::out | ios::app);
		timeStamp(&outLog);
		outLog << "Failed to create passhook mutex.  Passhook DLL will not be loaded." << endl;
		outLog.close();

		return FALSE;
	} else {
		return TRUE;
	}
}

// This function will save the password change to the passhook data file.  It
// will be run as a separate thread.
DWORD WINAPI SavePasshookChange( LPVOID passinfo ) 
{
	PASS_INFO *newPassInfo = NULL;
        PASS_INFO_LIST passInfoList;
        HANDLE passhookEventHandle = OpenEvent(EVENT_MODIFY_STATE, FALSE, PASSHAND_EVENT_NAME);
	fstream outLog;

	if ((newPassInfo = (PASS_INFO *)passinfo) == NULL) {
		goto exit;
	}

        // Acquire the mutex for passhook.dat.  This mutex also guarantees
	// that we can write to outLog safely.
        WaitForSingleObject(passhookMutexHandle, INFINITE);

	// Open the log file if logging is enabled
        if(logLevel > 0)
        {
                outLog.open("passhook.log", ios::out | ios::app);
        }

	if(outLog.is_open())
        {
                timeStamp(&outLog);
                outLog << "user " <<  newPassInfo->username << " password changed" << endl;
                //outLog << "user " <<  newPassInfo->username << " password changed to " <<  newPassInfo->passname << endl;
        }

        // loadSet allocates memory for the usernames and password.  We need to be
        // sure to free it by calling clearSet.
        if(loadSet(&passInfoList, "passhook.dat") == 0)
        {
                if(outLog.is_open())
                {
                        timeStamp(&outLog);
                        outLog << passInfoList.size() << " entries loaded from file" << endl;
                }
        }
        else
        {
                if(outLog.is_open())
                {
                        timeStamp(&outLog);
                        outLog << "failed to load entries from file" << endl;
                }
        }

	// Add the new change to the list
        passInfoList.push_back(*newPassInfo);

        // Save the list to disk
        if(saveSet(&passInfoList, "passhook.dat") == 0)
        {
                if(outLog.is_open())
                {
                        timeStamp(&outLog);
                        outLog << passInfoList.size() << " entries saved to file" << endl;
                }
        }
        else
        {
		// We always want to log this error condition
                if(!outLog.is_open())
                {
			// We need to open the log since debug logging is turned off
			outLog.open("passhook.log", ios::out | ios::app);
		}

                timeStamp(&outLog);
                outLog << "failed to save entries to file" << endl;
        }

	// Close the log file before we release the mutex.
	outLog.close();

        // Release the mutex for passhook.dat
        ReleaseMutex(passhookMutexHandle);

        // We need to call clearSet so memory gets free'd
        clearSet(&passInfoList);

exit:
	// Free the passed in struct from the heap
	free(newPassInfo);

        if (passhookEventHandle != NULL) {
                SetEvent(passhookEventHandle);
		CloseHandle(passhookEventHandle);
        }

	return 0;
}
