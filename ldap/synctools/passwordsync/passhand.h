/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

// Created: 2-8-2005
// Author(s): Scott Bridges
#ifndef _PASSHAND_H_
#define _PASSHAND_H_

#include <windows.h>
#include <time.h>
#include <strstream>
#include <fstream>
#include <list>
#include "nss.h"
#include "pk11func.h"
#include "prerror.h"

#define PASSHAND_EVENT_NAME "passhand_event"

#define PASSHAND_BUF_SIZE 256

using namespace std;

struct PASS_INFO
{
	char* username;
	char* password;
};

typedef list<PASS_INFO> PASS_INFO_LIST;
typedef list<PASS_INFO>::iterator PASS_INFO_LIST_ITERATOR;

void timeStamp(fstream* outFile);

int encrypt(char* plainTextBuf, int plainTextLen, char* cipherTextBuf, int cipherTextLen, int* resultTextLen);
int decrypt(char* cipherTextBuf, int cipherTextLen, char* plainTextBuf, int plainTextLen, int* resultTextLen);

int saveSet(PASS_INFO_LIST* passInfoList, char* filename);
int loadSet(PASS_INFO_LIST* passInfoList, char* filename);
int clearSet(PASS_INFO_LIST* passInfoList);

#endif
