// Created: 2-8-2005
// Author(s): Scott Bridges
#include "passhand.h"
#include <time.h>

#define KEY {0xe8, 0xa7, 0x7c, 0xe2, 0x05, 0x63, 0x6a, 0x31}
#define IV {0xe4, 0xbb, 0x3b, 0xd3, 0xc3, 0x71, 0x2e, 0x58}

void timeStamp(fstream* outFile)
{
	if(outFile->is_open())
	{
		char dateBuf[32];
		char timeBuf[32];

		_strdate(dateBuf);
		_strtime(timeBuf);
		*outFile << dateBuf << " " << timeBuf << ": ";
	}
}

PasswordHandler::PasswordHandler()
{
	outLog.open("./passhand.log", ios::out | ios::app);
}

PasswordHandler::~PasswordHandler()
{
	outLog.close();
}

int PasswordHandler::SaveSet(char* filename)
{
	int result = 0;
	fstream outFile;
	list<USER_PASS_PAIR>::iterator currentPair;
	strstream plainTextStream;
	char* cipherTextBuf;
	int usernameLen;
	int passwordLen;
	int plainTextLen;
	int cipherTextLen;
	int resultTextLen = 0;
	int pairCount = userPassPairs.size();

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "SaveSet: saving " << userPassPairs.size() << " entries to file" << endl;
	}

	// Write usernames and passwords to a strstream
	plainTextStream.write((char*)&pairCount, sizeof(pairCount));
	for(currentPair = userPassPairs.begin(); currentPair != userPassPairs.end(); currentPair++)
	{
		// Usernames
		usernameLen = strlen(currentPair->username) + 1;
		plainTextStream.write((char*)&usernameLen, sizeof(usernameLen));
		plainTextStream.write(currentPair->username, usernameLen);
		
		// Passwords
		passwordLen = strlen(currentPair->password) + 1;
		plainTextStream.write((char*)&passwordLen, sizeof(passwordLen));
		plainTextStream.write(currentPair->password, passwordLen);
	}


	plainTextLen = plainTextStream.tellp() - plainTextStream.tellg();
	// cipherTextBuf length must be at least plainTextLen + 8
	cipherTextLen = plainTextLen + 8;

	cipherTextBuf = (char*)malloc(cipherTextLen);

	if(encrypt(plainTextStream.str(), plainTextLen, cipherTextBuf, cipherTextLen, &resultTextLen) != 0)
	{
		result = -1;
		goto exit;
	}

	// Write cipher text to file
	outFile.open(filename, ios::out | ios::binary);
	if(!outFile.is_open())
	{
		result = -1;
		goto exit;
	}
	outFile.write(cipherTextBuf, resultTextLen);
	outFile.close();

	// ToDo: zero out memory

	userPassPairs.clear();

exit:
	return result;
}

int PasswordHandler::LoadSet(char* filename)
{
	int result = 0;
	int i;
	fstream inFile;
	USER_PASS_PAIR newPair;
	strstream* plainTextStream;
	char* cipherTextBuf;
	char* plainTextBuf;
	int usernameLen;
	int passwordLen;
	int plainTextLen;
	int cipherTextLen;
	int resultTextLen = 0;
	int pairCount;

	// Read in cipher text from file
	inFile.open(filename, ios::in | ios::binary);
	if(!inFile.is_open())
	{
		result = -1;
		goto exit;
	}
	// Determine file size
	inFile.seekg(0, ios::end);
	cipherTextLen = inFile.tellg();
	inFile.seekg(0, ios::beg);
	// plainTextLen length must be at least cipherTextLen
	plainTextLen = cipherTextLen;

	cipherTextBuf = (char*)malloc(cipherTextLen);
	plainTextBuf = (char*)malloc(plainTextLen);

	inFile.read(cipherTextBuf, cipherTextLen);
	inFile.close();

	if(decrypt(cipherTextBuf, cipherTextLen, plainTextBuf, plainTextLen, &resultTextLen) != 0)
	{
		result = -1;
		goto exit;
	}

	plainTextStream = new strstream(plainTextBuf, resultTextLen);

	plainTextStream->read((char*)&pairCount, sizeof(pairCount));

	// Read usernames and passwords from a strstream
	for(i = 0; i < pairCount; i++)
	{
		// Username
		plainTextStream->read((char*)&usernameLen, sizeof(usernameLen));
		newPair.username = (char*)malloc(usernameLen);
		plainTextStream->read((char*)newPair.username, usernameLen);

		// Password
		plainTextStream->read((char*)&passwordLen, sizeof(passwordLen));
		newPair.password = (char*)malloc(passwordLen);
		plainTextStream->read((char*)newPair.password, passwordLen);

		userPassPairs.push_back(newPair);
	}

	delete plainTextStream;

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "LoadSet: "<< userPassPairs.size() << " entries loaded from file" << endl;
	}

exit:
	return result;
}

int PasswordHandler::PushUserPass(char* username, char* password)
{
	USER_PASS_PAIR newPair;

	newPair.username = (char*)malloc(strlen(username) + 1);
	strcpy(newPair.username, username);

	newPair.password = (char*)malloc(strlen(password) + 1);
	strcpy(newPair.password, password);

	userPassPairs.push_back(newPair);

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "PushUserPass: pushed user password pair, new length " << userPassPairs.size() << endl;
	}

	return 0;
}

int PasswordHandler::PeekUserPass(char* username, char* password)
{
	int result = 0;
	list<USER_PASS_PAIR>::iterator currentPair;

	if(userPassPairs.size() < 1)
	{
		result = -1;
		goto exit;
	}

	currentPair = userPassPairs.begin();
	strcpy(username, currentPair->username);
	strcpy(password, currentPair->password);

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "PeekUserPass: current length " << userPassPairs.size() << endl;
	}

exit:
	return result;
}

int PasswordHandler::PopUserPass()
{
	// ToDo: zero out memory.

	userPassPairs.pop_front();

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "PopUserPass: popped user password pair, new length " << userPassPairs.size() << endl;
	}

	return 0;
}


int PasswordHandler::encrypt(char* plainTextBuf, int plainTextLen, char* cipherTextBuf, int cipherTextLen, int* resultTextLen)
{
	int result = 0;
	SECStatus rv1, rv2, rv3;
	PK11SlotInfo* slot = NULL;
	PK11SymKey* SymKey = NULL;
	SECItem* SecParam = NULL;
	PK11Context* EncContext = NULL;
	unsigned char gKey[] = KEY;
	unsigned char gIV[] = IV;
	PK11SymKey* key = NULL;
	SECItem keyItem;
	SECItem	ivItem;
	CK_MECHANISM_TYPE cipherMech = CKM_DES_CBC_PAD;
	int offset;
	int tempTextLen;

	// Initialize NSS
	rv1 = NSS_NoDB_Init(".");
	if(rv1 != SECSuccess)
	{
		result = PR_GetError();
		goto exit;
	}

	// Get a key slot
	slot = PK11_GetInternalKeySlot();
	if(slot == NULL)
	{
		result = PR_GetError();
		goto exit;
	}

	// Generate a symmetric key
	keyItem.data = gKey;
	keyItem.len = sizeof(gKey);
	SymKey = PK11_ImportSymKey(slot, cipherMech, PK11_OriginUnwrap, CKA_ENCRYPT, &keyItem, NULL);
	if(SymKey == NULL)
	{
		result = PR_GetError();
		goto exit;
	}

	// Set up the PKCS11 encryption paramters
	ivItem.data = gIV;
	ivItem.len = sizeof(gIV);
	SecParam = PK11_ParamFromIV(cipherMech, &ivItem);
	if(SecParam == NULL)
	{
		if(SymKey != NULL)
		{
			PK11_FreeSymKey(SymKey);
		}
		result = PR_GetError();
		goto exit;
	}

	// ToDo: check parameters


	// Encrypt
	tempTextLen = 0;
	EncContext = PK11_CreateContextBySymKey(cipherMech, CKA_ENCRYPT, SymKey, SecParam);
	rv2 = PK11_CipherOp(EncContext, (unsigned char*)cipherTextBuf, &tempTextLen, cipherTextLen, (unsigned char*)plainTextBuf, plainTextLen);
	offset = tempTextLen;
	rv3 = PK11_DigestFinal(EncContext, (unsigned char*)cipherTextBuf + offset, (unsigned int*)&tempTextLen, cipherTextLen - offset);
	*resultTextLen = offset + tempTextLen;

	// Clean up
	PK11_DestroyContext(EncContext, PR_TRUE);
	PK11_FreeSymKey(SymKey);
	SECITEM_FreeItem(SecParam, PR_TRUE);

	if((rv2 != SECSuccess) || (rv2 != SECSuccess))
	{
		result = PR_GetError();
		goto exit;
	}

exit:
	if(outLog.is_open())
	{
		if(result == 0)
		{
			timeStamp(&outLog);
			outLog << "encrypt: success" << endl;
		}
		else
		{
			timeStamp(&outLog);
			outLog << "encrypt: failure" << endl;
		}
	}

	return result;
}

int PasswordHandler::decrypt(char* cipherTextBuf, int cipherTextLen, char* plainTextBuf, int plainTextLen, int* resultTextLen)
{
	int result = 0;
	SECStatus rv1, rv2, rv3;
	PK11SlotInfo* slot = NULL;
	PK11SymKey* SymKey = NULL;
	SECItem* SecParam = NULL;
	PK11Context* EncContext = NULL;
	unsigned char gKey[] = KEY;
	unsigned char gIV[] = IV;
	PK11SymKey* key = NULL;
	SECItem keyItem;
	SECItem	ivItem;
	CK_MECHANISM_TYPE cipherMech = CKM_DES_CBC_PAD;
	int offset;
	int tempTextLen;

	// Initialize NSS
	rv1 = NSS_NoDB_Init(".");
	if(rv1 != SECSuccess)
	{
		result = PR_GetError();
		goto exit;
	}

	// Get a key slot
	slot = PK11_GetInternalKeySlot();
	if(slot == NULL)
	{
		result = PR_GetError();
		goto exit;
	}

	// Generate a symmetric key
	keyItem.data = gKey;
	keyItem.len = sizeof(gKey);
	SymKey = PK11_ImportSymKey(slot, cipherMech, PK11_OriginUnwrap, CKA_ENCRYPT, &keyItem, NULL);
	if(SymKey == NULL)
	{
		result = PR_GetError();
		goto exit;
	}

	// Set up the PKCS11 encryption paramters
	ivItem.data = gIV;
	ivItem.len = sizeof(gIV);
	SecParam = PK11_ParamFromIV(cipherMech, &ivItem);
	if(SecParam == NULL)
	{
		if(SymKey != NULL)
		{
			PK11_FreeSymKey(SymKey);
		}
		result = PR_GetError();
		goto exit;
	}

	// ToDo: check parameters


	// Decrypt
	tempTextLen = 0;
	EncContext = PK11_CreateContextBySymKey(cipherMech, CKA_DECRYPT, SymKey, SecParam);
	rv2 = PK11_CipherOp(EncContext, (unsigned char*)plainTextBuf, &tempTextLen, plainTextLen, (unsigned char*)cipherTextBuf, cipherTextLen);
	offset = tempTextLen;
	rv3 = PK11_DigestFinal(EncContext, (unsigned char*)plainTextBuf + offset, (unsigned int*)&tempTextLen, plainTextLen - offset);
	*resultTextLen = offset + tempTextLen;

	// Clean up
	PK11_DestroyContext(EncContext, PR_TRUE);
	PK11_FreeSymKey(SymKey);
	SECITEM_FreeItem(SecParam, PR_TRUE);

	if((rv2 != SECSuccess) || (rv2 != SECSuccess))
	{
		result = PR_GetError();
		goto exit;
	}

exit:
	if(outLog.is_open())
	{
		if(result == 0)
		{
			timeStamp(&outLog);
			outLog << "decrypt: success" << endl;
		}
		else
		{
			timeStamp(&outLog);
			outLog << "decrypt: failure" << endl;
		}
	}

	return result;
}
