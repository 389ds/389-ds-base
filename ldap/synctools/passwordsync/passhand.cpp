// --- BEGIN COPYRIGHT BLOCK ---
// Copyright (C) 2005 Red Hat, Inc.
// All rights reserved.
// --- END COPYRIGHT BLOCK ---

// Created: 2-8-2005
// Author(s): Scott Bridges
#include "passhand.h"

PasswordHandler::PasswordHandler()
{
}

PasswordHandler::~PasswordHandler()
{
}

int PasswordHandler::SaveSet(char* filename)
{
	fstream outFile;
	list<USER_PASS_PAIR>::iterator currentPair;

	outFile.open(filename, ios::out | ios::binary);

	if(!outFile.is_open())
	{
		return -1;
	}

	for(currentPair = userPassPairs.begin(); currentPair != userPassPairs.end(); currentPair++)
	{
		outFile.write((char*)&currentPair->username.Length, sizeof(currentPair->username.Length));
		outFile.write((char*)currentPair->username.Buffer, currentPair->username.Length);

		outFile.write((char*)&currentPair->password.Length, sizeof(currentPair->password.Length));
		outFile.write((char*)currentPair->password.Buffer, currentPair->password.Length);
	}

	// ToDo: Zero out memory.
	userPassPairs.clear();

	return 0;
}

int PasswordHandler::LoadSet(char* filename)
{
	fstream inFile;
	USER_PASS_PAIR newPair;
	
	inFile.open(filename, ios::in | ios::binary);

	if(!inFile.is_open())
	{
		return -1;
	}

	while(!inFile.eof())
	{
		inFile.read((char*)&newPair.username.Length, sizeof(newPair.username.Length));
		newPair.username.Buffer = (unsigned short*)malloc(newPair.username.Length);
		inFile.read((char*)newPair.username.Buffer, newPair.username.Length);
		newPair.username.MaximumLength = newPair.username.Length;

		inFile.read((char*)&newPair.password.Length, sizeof(newPair.password.Length));
		newPair.password.Buffer = (unsigned short*)malloc(newPair.password.Length);
		inFile.read((char*)newPair.password.Buffer, newPair.password.Length);
		newPair.password.MaximumLength = newPair.password.Length;

		if(!inFile.eof())
		{
			userPassPairs.push_back(newPair);
		}
	}

	return 0;
}

int PasswordHandler::PushUserPass(PUNICODE_STRING username, PUNICODE_STRING password)
{
	USER_PASS_PAIR newPair;

	newPair.username.Length = username->Length;
	newPair.username.Buffer = (unsigned short*)malloc(username->Length);
	memcpy(newPair.username.Buffer, username->Buffer, username->Length);
	newPair.username.MaximumLength = newPair.username.Length;

	newPair.password.Length = password->Length;
	newPair.password.Buffer = (unsigned short*)malloc(password->Length);
	memcpy(newPair.password.Buffer, password->Buffer, password->Length);
	newPair.password.MaximumLength = newPair.password.Length;

	userPassPairs.push_back(newPair);

	return 0;
}

int PasswordHandler::PeekUserPass(PUNICODE_STRING username, PUNICODE_STRING password)
{
	list<USER_PASS_PAIR>::iterator currentPair;

	if(userPassPairs.size() == 0)
	{
		return -1;
	}

	currentPair = userPassPairs.begin();

	username->Length = currentPair->username.Length;
	username->Buffer = (unsigned short*)malloc(username->Length);
	memcpy(username->Buffer, currentPair->username.Buffer, username->Length);
	username->MaximumLength = username->Length;

	password->Length = currentPair->password.Length;
	password->Buffer = (unsigned short*)malloc(password->Length);
	memcpy(password->Buffer, currentPair->password.Buffer, password->Length);
	password->MaximumLength = password->Length;

	return 0;
}

int PasswordHandler::PopUserPass()
{
	// ToDo: Zero out memory.
	userPassPairs.pop_front();

	return 0;
}
