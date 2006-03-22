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

int saveSet(PASS_INFO_LIST* passInfoList, char* filename)
{
	int result = 0;
	fstream outFile;
	PASS_INFO_LIST_ITERATOR currentPair;
	strstream plainTextStream;
	char* cipherTextBuf = NULL;
	int usernameLen;
	int passwordLen;
	int plainTextLen;
	int cipherTextLen;
	int resultTextLen = 0;
	int pairCount = passInfoList->size();

	// Write usernames and passwords to a strstream
	plainTextStream.write((char*)&pairCount, sizeof(pairCount));
	for(currentPair = passInfoList->begin(); currentPair != passInfoList->end(); currentPair++)
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

	if ((cipherTextBuf = (char*)malloc(cipherTextLen)) == NULL) {
		result = -1;
		goto exit;
	}

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

exit:
	free(cipherTextBuf);
	return result;
}

int loadSet(PASS_INFO_LIST* passInfoList, char* filename)
{
	int result = 0;
	int i;
	fstream inFile;
	PASS_INFO newPair;
	strstream* plainTextStream;
	char* cipherTextBuf = NULL;
	char* plainTextBuf = NULL;
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

	if ((cipherTextBuf == NULL) || (plainTextBuf == NULL)) {
		result = -1;
		inFile.close();
		goto exit;
	}

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

		// Backoff
		newPair.backoffCount = 0;

		// Load time
		time(&newPair.atTime);

		passInfoList->push_back(newPair);
	}

	delete plainTextStream;

exit:
	free(cipherTextBuf);
	free(plainTextBuf);
	return result;
}

int clearSet(PASS_INFO_LIST* passInfoList)
{
	while (!passInfoList->empty()) {
		PASS_INFO& pi = passInfoList->back();
		SecureZeroMemory(pi.password, strlen(pi.password));
		free(pi.password);
		free(pi.username);
		passInfoList->pop_back();
	}

	passInfoList->clear();

	return 0;
}

int encrypt(char* plainTextBuf, int plainTextLen, char* cipherTextBuf, int cipherTextLen, int* resultTextLen)
{
	int result = 0;
	SECStatus rv1, rv2, rv3;
	PK11SlotInfo* slot = NULL;
	PK11SymKey* SymKey = NULL;
	SECItem* SecParam = NULL;
	PK11Context* EncContext = NULL;
	unsigned char gKey[] = KEY;
	unsigned char gIV[] = IV;
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
		PK11_FreeSlot(slot);
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
			PK11_FreeSlot(slot);
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
	PK11_FreeSlot(slot);
	SECITEM_FreeItem(SecParam, PR_TRUE);

	if((rv2 != SECSuccess) || (rv2 != SECSuccess))
	{
		result = PR_GetError();
		goto exit;
	}

exit:
	return result;
}

int decrypt(char* cipherTextBuf, int cipherTextLen, char* plainTextBuf, int plainTextLen, int* resultTextLen)
{
	int result = 0;
	SECStatus rv1, rv2, rv3;
	PK11SlotInfo* slot = NULL;
	PK11SymKey* SymKey = NULL;
	SECItem* SecParam = NULL;
	PK11Context* EncContext = NULL;
	unsigned char gKey[] = KEY;
	unsigned char gIV[] = IV;
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
		PK11_FreeSlot(slot);
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
			PK11_FreeSlot(slot);
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
	PK11_FreeSlot(slot);
	SECITEM_FreeItem(SecParam, PR_TRUE);

	if((rv2 != SECSuccess) || (rv2 != SECSuccess))
	{
		result = PR_GetError();
		goto exit;
	}

exit:
	return result;
}
