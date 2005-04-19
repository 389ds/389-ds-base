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

NTSTATUS NTAPI PasswordChangeNotify(PUNICODE_STRING UserName, ULONG RelativeId, PUNICODE_STRING Password)
{
	char singleByteUsername[PASSHAND_BUF_SIZE];
	char singleBytePassword[PASSHAND_BUF_SIZE];
	HANDLE passhookEventHandle = OpenEvent(EVENT_MODIFY_STATE, FALSE, PASSHAND_EVENT_NAME);
	PASS_INFO newPassInfo;
	PASS_INFO_LIST passInfoList;
	HKEY regKey;
	DWORD type;
	unsigned long buffSize;
	char regBuff[PASSHAND_BUF_SIZE];
	unsigned long logLevel;
	fstream outLog;

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
	if(logLevel > 0)
	{
		outLog.open("passhook.log", ios::out | ios::app);
	}
	RegCloseKey(regKey);

	_snprintf(singleByteUsername, PASSHAND_BUF_SIZE, "%S", UserName->Buffer);
	singleByteUsername[UserName->Length / 2] = '\0';
	_snprintf(singleBytePassword, PASSHAND_BUF_SIZE, "%S", Password->Buffer);
	singleBytePassword[Password->Length / 2] = '\0';

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "user " << singleByteUsername << " password changed" << endl;
		//outLog << "user " << singleByteUsername << " password changed to " << singleBytePassword << endl;
	}

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

	newPassInfo.username = singleByteUsername;
	newPassInfo.password = singleBytePassword;
	passInfoList.push_back(newPassInfo);

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
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "failed to save entries to file" << endl;
		}
	}

	if(passhookEventHandle == NULL)
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "can not get password sync service event handle, service not running" << endl;
		}

	}
	else
	{
		SetEvent(passhookEventHandle);
	}

	outLog.close();

	return STATUS_SUCCESS;
}

BOOL NTAPI PasswordFilter(PUNICODE_STRING UserName, PUNICODE_STRING FullName, PUNICODE_STRING Password, BOOL SetOperation)
{
	return TRUE;
}

BOOL NTAPI InitializeChangeNotify()
{
	return TRUE;
}
