// Created: 2-8-2005
// Author(s): Scott Bridges
#include <windows.h>
#include <ntsecapi.h>
#include  "../passhand.h"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)
#endif

NTSTATUS NTAPI PasswordChangeNotify(
	PUNICODE_STRING UserName,
	ULONG RelativeId,
	PUNICODE_STRING Password)
{
	PasswordHandler ourPasswordHandler;
	HANDLE passhookEventHandle = OpenEvent(EVENT_MODIFY_STATE, FALSE, PASSHAND_EVENT_NAME);

	ourPasswordHandler.LoadSet("passhook.dat");
	ourPasswordHandler.PushUserPass(UserName, Password);
	ourPasswordHandler.SaveSet("passhook.dat");

	if(passhookEventHandle == NULL)
	{
		// ToDo: Generate event sync service not running.
	}
	else
	{
		SetEvent(passhookEventHandle);
	}

	return STATUS_SUCCESS;
}

BOOL NTAPI PasswordFilter(
	PUNICODE_STRING UserName,
	PUNICODE_STRING FullName,
	PUNICODE_STRING Password,
	BOOL SetOperation)
{
	return TRUE;
}

BOOL NTAPI InitializeChangeNotify()
{
	return TRUE;
}