/* --- BEGIN COPYRIGHT BLOCK ---
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
	fstream outLog;

	outLog.open("passhook.log", ios::out | ios::app);

	_snprintf(singleByteUsername, PASSHAND_BUF_SIZE, "%S", UserName->Buffer);
	singleByteUsername[UserName->Length / 2] = '\0';
	_snprintf(singleBytePassword, PASSHAND_BUF_SIZE, "%S", Password->Buffer);
	singleBytePassword[Password->Length / 2] = '\0';

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "user " << singleByteUsername << " password changed" << endl;
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