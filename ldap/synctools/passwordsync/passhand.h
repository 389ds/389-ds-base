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

struct USER_PASS_PAIR
{
	char* username;
	char* password;
};

void timeStamp(fstream* outFile);

class PasswordHandler
{
public:
	PasswordHandler();
	~PasswordHandler();

	int SaveSet(char* filename);
	int LoadSet(char* filename);
	int PushUserPass(char* username, char* password);
	int PeekUserPass(char* username, char* password);
	int PopUserPass();
private:
	int encrypt(char* plainTextBuf, int plainTextLen, char* cipherTextBuf, int cipherTextLen, int* resultTextLen);
	int decrypt(char* cipherTextBuf, int cipherTextLen, char* plainTextBuf, int plainTextLen, int* resultTextLen);

	list<USER_PASS_PAIR> userPassPairs;
	char* keyPath;
	fstream outLog;
};

#endif
