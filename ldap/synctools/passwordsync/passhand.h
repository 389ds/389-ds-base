// --- BEGIN COPYRIGHT BLOCK ---
// Copyright (C) 2005 Red Hat, Inc.
// All rights reserved.
// --- END COPYRIGHT BLOCK ---

// Created: 2-8-2005
// Author(s): Scott Bridges
#ifndef _PASSHAND_H_
#define _PASSHAND_H_

#include <windows.h>
#include <ntsecapi.h>
#include <fstream>
#include <list>

#define PASSHAND_EVENT_NAME "passhand_event"

#define STRSTREAM_BUF_SIZE 1024

using namespace std;

struct USER_PASS_PAIR
{
	UNICODE_STRING username;
	UNICODE_STRING password;
};

class PasswordHandler
{
public:
	PasswordHandler();
	~PasswordHandler();

	//WritePassToStorage(PUNICODE_STRING username, PUNICODE_STRING password);
	//ReadPassFromStorage(PUNICODE_STRING username, PUNICODE_STRING password);
	int SaveSet(char* filename);
	int LoadSet(char* filename);
	int PushUserPass(PUNICODE_STRING username, PUNICODE_STRING password);
	int PeekUserPass(PUNICODE_STRING username, PUNICODE_STRING password);
	int PopUserPass();
private:
	list<USER_PASS_PAIR> userPassPairs;
};

#endif
