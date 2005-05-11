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

// ****************************************************************
// netman.cpp
// ****************************************************************
#include "netman.h"

// ****************************************************************
// quickFree
// ****************************************************************
void quickFree(char** buf)
{
	if(*buf != NULL)
	{
		free(*buf);
		*buf = NULL;
	}
}

// ****************************************************************
// UTF16ToUTF8
// ****************************************************************
int UTF16ToUTF8(unsigned short* inStr, char* outStr, unsigned long* outStrBufLen)
{
	int result = 0;
	unsigned long length = WideCharToMultiByte(CP_ACP, 0, inStr, -1, 0, 0, 0, 0);

	if(outStr == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*outStrBufLen < length)
	{
		result = -1;
		goto exit;
	}

	WideCharToMultiByte(CP_ACP, 0, inStr, -1, outStr, length, 0, 0);

exit:
	*outStrBufLen = length;

	return result;
}

// ****************************************************************
// UTF8ToUTF16
// ****************************************************************
int UTF8ToUTF16(char* inStr, unsigned short* outStr, unsigned long* outStrBufLen)
{
	int result = 0;
	unsigned long length = MultiByteToWideChar(CP_ACP, 0, inStr, -1, 0, 0) * 2;

	if(outStr == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*outStrBufLen < length)
	{
		result = -1;
		goto exit;
	}

	MultiByteToWideChar(CP_ACP, 0, inStr, -1, outStr, length);

exit:
	*outStrBufLen = length;

	return result;
}

// ****************************************************************
// BinToHexStr
// ****************************************************************
int BinToHexStr(char* bin, unsigned long binLen, char* hexStr, unsigned long* hexStrBufLen)
{
	int result = 0;
	unsigned long length = binLen * 2 + 1;
	unsigned long i;

	if(hexStr == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*hexStrBufLen < length)
	{
		result = -1;
		goto exit;
	}

	for(i = 0; i < binLen; i++)
	{
		sprintf(&hexStr[i * 2], "%02X", (unsigned char)bin[i]);
	}

exit:
	*hexStrBufLen = length;

	return result;
}

// ****************************************************************
// HexStrToBin
// ****************************************************************
int HexStrToBin(char* hexStr, char* bin, unsigned long* binBufLen)
{
	int result = 0;
	unsigned long length = strlen(hexStr) / 2;
	unsigned long i;
	int temp;

	if(bin == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*binBufLen < length)
	{
		result = -1;
		goto exit;
	}

	for(i = 0; i < length; i++)
	{
		sscanf(&hexStr[i * 2], "%02X", &temp);
		bin[i] = (unsigned char)temp;
	}

exit:
	*binBufLen = length;

	return result;
}

// ****************************************************************
// GetSIDByAccountName
// ****************************************************************
int GetSIDByAccountName(char* accountName, char* sid, unsigned long* sidBufLen)
{
	int result = 0;
	unsigned long sidLen = 0;
	unsigned long domainLen = 0;
	char* domain = NULL;
	SID_NAME_USE testType;

	LookupAccountName(NULL, accountName, NULL, &sidLen, NULL, &domainLen, &testType);

	if(sid == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*sidBufLen < sidLen)
	{
		result = -1;
		goto exit;
	}
	domain = (char*)malloc(domainLen);

	if(LookupAccountName(NULL, accountName, (void*)sid, &sidLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
		goto exit;
	}

exit:
	*sidBufLen = sidLen;

	quickFree(&domain);

	return result;
}

// ****************************************************************
// GetAccountNameBySID
// ****************************************************************
int GetAccountNameBySID(char* sid, char* accountName, unsigned long* accountNameBufLen)
{
	int result = 0;
	unsigned long accountNameLen = 0;
	unsigned long domainLen = 0;
	char* domain = NULL;
	SID_NAME_USE testType;

	LookupAccountSid(NULL, sid, NULL, &accountNameLen, NULL, &domainLen, &testType);


	if(accountName == NULL)
	{
		result = 0;
		goto exit;
	}
	if(*accountNameBufLen < accountNameLen)
	{
		result = -1;
		goto exit;
	}
	domain = (char*)malloc(domainLen);

	if(LookupAccountSid(NULL, sid, accountName, &accountNameLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
		goto exit;
	}

exit:
	*accountNameBufLen = accountNameLen;

	quickFree(&domain);

	return result;
}

// ****************************************************************
// NTUser::NTUser
// ****************************************************************
NTUser::NTUser()
{
	currentAccountName = NULL;

	groupsInfo = NULL;
	currentGroupEntry = 0;
	groupEntriesRead = 0;
	groupEntriesTotal = 0;

	localGroupsInfo = NULL;
	currentLocalGroupEntry = 0;
	localGroupEntriesRead = 0;
	localGroupEntriesTotal = 0;

	resultBuf = NULL;
}

// ****************************************************************
// NTUser::~NTUser
// ****************************************************************
NTUser::~NTUser()
{
	quickFree((char**)&currentAccountName);
	if(groupsInfo != NULL)
	{
		NetApiBufferFree(groupsInfo);
		groupsInfo = NULL;
	}
	if(localGroupsInfo != NULL)
	{
		NetApiBufferFree(localGroupsInfo);
		localGroupsInfo = NULL;
	}
	quickFree((char**)&resultBuf);
}

// ****************************************************************
// NTUser::NewUser
// ****************************************************************
int NTUser::NewUser(char* username)
{
	int result = 0;
	unsigned long length;
	PUSER_INFO_3 info = NULL;
	DWORD badParam = 0;

	quickFree((char**)&currentAccountName);
	UTF8ToUTF16(username, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(username, currentAccountName, &length);

	info = (USER_INFO_3*)malloc(sizeof(USER_INFO_3));
	memset(info, 0, sizeof(USER_INFO_3));
	info->usri3_name = currentAccountName;

	// NT4 required inits for AddUser
	info->usri3_flags = UF_SCRIPT;
	info->usri3_primary_group_id = DOMAIN_GROUP_RID_USERS;

	// Other inits
	info->usri3_acct_expires = (unsigned long)-1;

	// Add user
	result = NetUserAdd(NULL, USER_INFO_LEVEL, (unsigned char*)info, &badParam);

	// Free buffers
	quickFree((char**)&info);

	return result;
}

// ****************************************************************
// NTUser::RetriveUserByUsername
// ****************************************************************
int NTUser::RetriveUserByAccountName(char* username)
{
	int result;
	unsigned long length = 0;
	PUSER_INFO_3 info;

	quickFree((char**)&currentAccountName);
	UTF8ToUTF16(username, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(username, currentAccountName, &length);

	result = NetUserGetInfo(NULL, currentAccountName, USER_INFO_LEVEL, (unsigned char**)&info);

	return result;
}

// ****************************************************************
// NTUser::RetriveUserBySIDHexStr
// ****************************************************************
int NTUser::RetriveUserBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	unsigned long length = 0;
	char* username;
	char* sid;

	quickFree((char**)&currentAccountName);

	HexStrToBin(sidHexStr, NULL, &length);
	sid = (char*)malloc(length);
	HexStrToBin(sidHexStr, sid, &length);

	if(GetAccountNameBySID(sid, NULL, &length) != 0)
	{
		result = -1;
		goto exit;
	}
	username = (char*)malloc(length);
	GetAccountNameBySID(sid, username, &length);

	if(RetriveUserByAccountName(username) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	quickFree(&sid);
	quickFree(&username);

	return result;
}

// ****************************************************************
// NTUser::DeleteUser
// ****************************************************************
int NTUser::DeleteUser()
{
	int result;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	result = NetUserDel(NULL, currentAccountName);

	quickFree((char**)&currentAccountName);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetAccountName
// ****************************************************************
char* NTUser::GetAccountName()
{
	char* result = NULL;
	unsigned long length = 0;

	if(currentAccountName == NULL)
	{
		goto exit;
		result = NULL;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(currentAccountName, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, resultBuf, &length);
	result = resultBuf;

exit:
	return result;
}

// ****************************************************************
// NTUser::GetSIDHexStr
// ****************************************************************
char* NTUser::GetSIDHexStr()
{
	char* result = NULL;
	unsigned long length = 0;
	unsigned long binLength = 0;
	char* username = NULL;
	char* sid = NULL;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	UTF16ToUTF8(currentAccountName, NULL, &length);
	username = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, username, &length);

	if(GetSIDByAccountName(username, NULL, &binLength) != 0)
	{
		result = NULL;
		goto exit;
	}
	sid = (char*)malloc(binLength);
	GetSIDByAccountName(username, sid, &binLength);

	quickFree(&resultBuf);
	BinToHexStr(sid, binLength, NULL, &length);
	resultBuf = (char*)malloc(length);
	BinToHexStr(sid, binLength, resultBuf, &length);
	result = resultBuf;

exit:
	quickFree(&username);
	quickFree(&sid);

	return result;
}

// ****************************************************************
// NTUser::GetAccountExpires
// ****************************************************************
unsigned long NTUser::GetAccountExpires()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if((result = NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info)) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_acct_expires;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetAccountExpires
// ****************************************************************
int NTUser::SetAccountExpires(unsigned long accountExpires)
{
	int result = 0;
	USER_INFO_1017 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	info.usri1017_acct_expires = accountExpires;
	result = NetUserSetInfo(NULL, currentAccountName, 1017, (unsigned char*)&info, NULL);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetBadPasswordCount
// ****************************************************************
unsigned long NTUser::GetBadPasswordCount()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_bad_pw_count;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::GetCodePage
// ****************************************************************
unsigned long NTUser::GetCodePage()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_code_page;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetCodePage
// ****************************************************************
int NTUser::SetCodePage(unsigned long codePage)
{
	int result = 0;
	USER_INFO_1025 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	info.usri1025_code_page = codePage;
	result = NetUserSetInfo(NULL, currentAccountName, 1025, (unsigned char*)&info, NULL);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetComment
// ****************************************************************
char* NTUser::GetComment()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_comment, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_comment, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetComment
// ****************************************************************
int NTUser::SetComment(char* comment)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1007 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(comment, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(comment, wideStr, &length);

	info.usri1007_comment = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1007, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetCountryCode
// ****************************************************************
unsigned long NTUser::GetCountryCode()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_country_code;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetCountryCode
// ****************************************************************
int NTUser::SetCountryCode(unsigned long countryCode)
{
	int result = 0;
	USER_INFO_1024 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	info.usri1024_country_code = countryCode;
	result = NetUserSetInfo(NULL, currentAccountName, 1024, (unsigned char*)&info, NULL);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetFlags
// ****************************************************************
unsigned long NTUser::GetFlags()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_flags;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetFlags
// ****************************************************************
int NTUser::SetFlags(unsigned long flags)
{
	int result = 0;
	USER_INFO_1008 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	info.usri1008_flags = flags;
	result = NetUserSetInfo(NULL, currentAccountName, 1008, (unsigned char*)&info, NULL);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetHomeDir
// ****************************************************************
char* NTUser::GetHomeDir()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_home_dir, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_home_dir, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetHomeDir
// ****************************************************************
int NTUser::SetHomeDir(char* path)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1006 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(path, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(path, wideStr, &length);

	info.usri1006_home_dir = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1006, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetHomeDirDrive
// ****************************************************************
char* NTUser::GetHomeDirDrive()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_home_dir_drive, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_home_dir_drive, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetHomeDirDrive
// ****************************************************************
int NTUser::SetHomeDirDrive(char* path)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1053 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(path, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(path, wideStr, &length);

	info.usri1053_home_dir_drive = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1053, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetLastLogoff
// ****************************************************************
unsigned long NTUser::GetLastLogoff()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_last_logoff;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::GetLastLogon
// ****************************************************************
unsigned long NTUser::GetLastLogon()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_last_logon;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::GetLogonHours
// ****************************************************************
char* NTUser::GetLogonHours()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	BinToHexStr((char*)info->usri3_logon_hours, 21, NULL, &length);
	resultBuf = (char*)malloc(length);
	BinToHexStr((char*)info->usri3_script_path, 21, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetLogonHours
// ****************************************************************
int NTUser::SetLogonHours(char* logonHours)
{
	int result = 0;
	unsigned long length;
	char* bin = NULL;
	USER_INFO_1020 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	HexStrToBin(logonHours, NULL, &length);
	bin = (char*)malloc(length);
	HexStrToBin(logonHours, bin, &length);

	info.usri1020_logon_hours = (unsigned char*)bin;
	result = NetUserSetInfo(NULL, currentAccountName, 1020, (unsigned char*)&info, NULL);

exit:
	quickFree(&bin);

	return result;
}

// ****************************************************************
// NTUser::GetMaxStorage
// ****************************************************************
unsigned long NTUser::GetMaxStorage()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_max_storage;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetMaxStorage
// ****************************************************************
int NTUser::SetMaxStorage(unsigned long maxStorage)
{
	int result = 0;
	USER_INFO_1018 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	info.usri1018_max_storage = maxStorage;
	result = NetUserSetInfo(NULL, currentAccountName, 1018, (unsigned char*)&info, NULL);

exit:
	return result;
}

// ****************************************************************
// NTUser::GetNumLogons
// ****************************************************************
unsigned long NTUser::GetNumLogons()
{
	unsigned long result = 0;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = 0;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = 0;
		goto exit;
	}

	result = info->usri3_num_logons;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::GetProfile
// ****************************************************************
char* NTUser::GetProfile()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_profile, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_profile, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetProfile
// ****************************************************************
int NTUser::SetProfile(char* path)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1052 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(path, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(path, wideStr, &length);

	info.usri1052_profile = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1052, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetScriptPath
// ****************************************************************
char* NTUser::GetScriptPath()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_script_path, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_script_path, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetScriptPath
// ****************************************************************
int NTUser::SetScriptPath(char* path)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1009 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(path, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(path, wideStr, &length);

	info.usri1009_script_path = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1009, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetWorkstations
// ****************************************************************
char* NTUser::GetWorkstations()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_workstations, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_workstations, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetWorkstations
// ****************************************************************
int NTUser::SetWorkstations(char* workstations)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1014 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(workstations, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(workstations, wideStr, &length);

	info.usri1014_workstations = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1014, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::GetFullname
// ****************************************************************
char* NTUser::GetFullname()
{
	char* result = NULL;
	unsigned long length;
	PUSER_INFO_3 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetUserGetInfo(NULL, currentAccountName, 3, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->usri3_full_name, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->usri3_full_name, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTUser::SetFullname
// ****************************************************************
int NTUser::SetFullname(char* fullname)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1011 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(fullname, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(fullname, wideStr, &length);

	info.usri1011_full_name = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1011, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::SetPassword
// ****************************************************************
int NTUser::SetPassword(char* password)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	USER_INFO_1003 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(password, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(password, wideStr, &length);

	info.usri1003_password = wideStr;
	result = NetUserSetInfo(NULL, currentAccountName, 1003, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::AddToGroup
// ****************************************************************
int NTUser::AddToGroup(char* groupName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(groupName, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(groupName, wideStr, &length);

	result = NetGroupAddUser(NULL, wideStr, currentAccountName);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::RemoveFromGroup
// ****************************************************************
int NTUser::RemoveFromGroup(char* groupName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(groupName, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(groupName, wideStr, &length);

	result = NetGroupDelUser(NULL, wideStr, currentAccountName);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::LoadGroups
// ****************************************************************
int NTUser::LoadGroups()
{
	int result = 0;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	if(groupsInfo != NULL)
	{
		NetApiBufferFree(groupsInfo);
		groupsInfo = NULL;
		currentGroupEntry = 0;
		groupEntriesRead = 0;
		groupEntriesTotal = 0;
	}

	result = NetUserGetGroups(NULL, currentAccountName, USER_GROUPS_INFO_LEVEL, (unsigned char**)&groupsInfo, MAX_PREFERRED_LENGTH, &groupEntriesRead, &groupEntriesTotal);

exit:
	return result;
}

// ****************************************************************
// NTUser::HasMoreGroups
// ****************************************************************
bool NTUser::HasMoreGroups()
{
	bool result;

	if(currentGroupEntry < groupEntriesRead)
	{
		result = true;
	}
	else
	{
		result = false; 
	}

	return result;
}

// ****************************************************************
// NTUser::NextGroupName
// ****************************************************************
char* NTUser::NextGroupName()
{
	char* result = NULL;
	unsigned long length;

	if(currentGroupEntry < groupEntriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(groupsInfo[currentGroupEntry].grui0_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(groupsInfo[currentGroupEntry].grui0_name, resultBuf, &length);
		result = resultBuf;

		currentGroupEntry++;
	}

	return result;
}

// ****************************************************************
// NTUser::AddToLocalGroup
// ****************************************************************
int NTUser::AddToLocalGroup(char* localGroupName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr;
	char userSID[256];
	wchar_t domain[256];
	DWORD SIDLen = sizeof(userSID);
	DWORD domainLen = sizeof(domain);
	SID_NAME_USE SIDUseIndicator;
	LOCALGROUP_MEMBERS_INFO_0 membersbuf[1];

	memset(&domain, 0, sizeof(domain));
	memset(&userSID, 0, sizeof(userSID));

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	result = LookupAccountName(NULL, GetAccountName(), &userSID, &SIDLen, (LPTSTR)&domain, &domainLen, &SIDUseIndicator);

	if(result != 0)
	{
		membersbuf[0].lgrmi0_sid = &userSID;

		UTF8ToUTF16(localGroupName, NULL, &length);
		wideStr = (unsigned short*)malloc(length);
		UTF8ToUTF16(localGroupName, wideStr, &length);
		result = NetLocalGroupAddMembers(NULL, wideStr, 0, (LPBYTE)&membersbuf, 1);
	}

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::RemoveFromLocalGroup
// ****************************************************************
int NTUser::RemoveFromLocalGroup(char* localGroupName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr;
	char userSID[256];
	wchar_t domain[256];
	DWORD SIDLen = sizeof(userSID);
	DWORD domainLen = sizeof(domain);
	SID_NAME_USE SIDUseIndicator;
	LOCALGROUP_MEMBERS_INFO_0 membersbuf[1];

	memset(&domain, 0, sizeof(domain));
	memset(&userSID, 0, sizeof(userSID));

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	result = LookupAccountName(NULL, GetAccountName(), &userSID, &SIDLen, (LPTSTR)&domain, &domainLen, &SIDUseIndicator);

	if(result != 0)
	{
		membersbuf[0].lgrmi0_sid = &userSID;

		UTF8ToUTF16(localGroupName, NULL, &length);
		wideStr = (unsigned short*)malloc(length);
		UTF8ToUTF16(localGroupName, wideStr, &length);
		result = NetLocalGroupDelMembers(NULL, wideStr, 0, (LPBYTE)&membersbuf, 1);
	}

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTUser::LoadLocalGroups
// ****************************************************************
int NTUser::LoadLocalGroups()
{
	int result = 0;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	if(localGroupsInfo != NULL)
	{
		NetApiBufferFree(localGroupsInfo);
		localGroupsInfo = NULL;
		currentLocalGroupEntry = 0;
		localGroupEntriesRead = 0;
		localGroupEntriesTotal = 0;
	}

	result = NetUserGetLocalGroups(NULL, currentAccountName, 0, USER_LOCALGROUPS_INFO_LEVEL, (unsigned char**)&localGroupsInfo, MAX_PREFERRED_LENGTH, &localGroupEntriesRead, &localGroupEntriesTotal);

exit:
	return result;
}

// ****************************************************************
// NTUser::HasMoreLocalGroups
// ****************************************************************
bool NTUser::HasMoreLocalGroups()
{
	bool result;

	if(currentLocalGroupEntry < localGroupEntriesRead)
	{
		result = true;
	}
	else
	{
		result = false;
	}

	return result;
}

// ****************************************************************
// NTUser::NextLocalGroupName
// ****************************************************************
char* NTUser::NextLocalGroupName()
{
	char* result = NULL;
	unsigned long length;

	if(currentLocalGroupEntry < localGroupEntriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(localGroupsInfo[currentLocalGroupEntry].lgrui0_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(localGroupsInfo[currentLocalGroupEntry].lgrui0_name, resultBuf, &length);
		result = resultBuf;

		currentLocalGroupEntry++;
	}

	return result;
}

// ****************************************************************
// NTUserList::NTUserList
// ****************************************************************
NTUserList::NTUserList()
{
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
	resultBuf = NULL;
}

// ****************************************************************
// NTUserList::~NTUserList
// ****************************************************************
NTUserList::~NTUserList()
{
	if(bufptr != NULL)
	{
		NetApiBufferFree(bufptr);
		bufptr = NULL;
	}
	quickFree(&resultBuf);
}

// ****************************************************************
// NTUserList::loadList
// ****************************************************************
int NTUserList::loadList()
{
	int result;

	result = NetUserEnum(NULL, USER_INFO_LEVEL, 0, (LPBYTE*)&bufptr, MAX_PREFERRED_LENGTH, &entriesRead, &totalEntries, &resumeHandle);

	return result;
}

// ****************************************************************
// NTUserList::hasMore
// ****************************************************************
bool NTUserList::hasMore()
{
	bool result;

	if(currentEntry < entriesRead)
	{
		result = true;
	}
	else
	{
		result = false;
	}

	return result;
}

// ****************************************************************
// NTUserList::nextUsername
// ****************************************************************
char* NTUserList::nextUsername()
{
	char* result = NULL;
	unsigned long length;

	if(currentEntry < entriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(bufptr[currentEntry].usri3_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(bufptr[currentEntry].usri3_name, resultBuf, &length);
		result = resultBuf;

		currentEntry++;
	}

	return result;
}

// ****************************************************************
// NTGroup::NTGroup
// ****************************************************************
NTGroup::NTGroup()
{
	currentAccountName = NULL;
	usersInfo = NULL;
	currentUserEntry = 0;
	userEntriesRead = 0;
	userEntriesTotal = 0;
	resultBuf = NULL;
}

// ****************************************************************
// NTGroup::~NTGroup
// ****************************************************************
NTGroup::~NTGroup()
{
	quickFree((char**)&currentAccountName);
	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
	}
	quickFree(&resultBuf);
}

// ****************************************************************
// NTGroup::NewGroup
// ****************************************************************
int NTGroup::NewGroup(char* groupName)
{
	int result = 0;
	unsigned long length;
	PGROUP_INFO_2 info = NULL;
	DWORD badParam = 0;

	quickFree((char**)&currentAccountName);

	UTF8ToUTF16(groupName, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(groupName, currentAccountName, &length);

	info = (PGROUP_INFO_2)malloc(sizeof(GROUP_INFO_2));
	memset(info, 0, sizeof(GROUP_INFO_2));
	info->grpi2_name = currentAccountName;

	// Add group
	result = NetGroupAdd(NULL, GROUP_INFO_LEVEL, (unsigned char*)info, &badParam);

	// Free buffers
	quickFree((char**)&info);

	return result;
}

// ****************************************************************
// NTGroup::RetriveGroupByAccountName
// ****************************************************************
int NTGroup::RetriveGroupByAccountName(char* groupName)
{
	int result;
	unsigned long length;
	PGROUP_INFO_2 info = NULL;

	quickFree((char**)&currentAccountName);
	UTF8ToUTF16(groupName, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(groupName, currentAccountName, &length);

	result = NetGroupGetInfo(NULL, currentAccountName, GROUP_INFO_LEVEL, (unsigned char**)&info);

	return result;
}

// ****************************************************************
// NTGroup::RetriveGroupBySIDHexStr
// ****************************************************************
int NTGroup::RetriveGroupBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	unsigned long length;
	char* groupName;
	char* sid;

	quickFree((char**)&currentAccountName);

	HexStrToBin(sidHexStr, NULL, &length);
	sid = (char*)malloc(length);
	HexStrToBin(sidHexStr, sid, &length);

	if(GetAccountNameBySID(sid, NULL, &length) != 0)
	{
		result = -1;
		goto exit;
	}
	groupName = (char*)malloc(length);
	GetAccountNameBySID(sid, groupName, &length);

	if(RetriveGroupByAccountName(groupName) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	quickFree(&sid);
	quickFree(&groupName);

	return result;
}

// ****************************************************************
// NTGroup::DeleteGroup
// ****************************************************************
int NTGroup::DeleteGroup()
{
	int result;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	result = NetGroupDel(NULL, currentAccountName);

	quickFree((char**)&currentAccountName);

exit:
	return result;
}

// ****************************************************************
// NTGroup::GetAccountName
// ****************************************************************
char* NTGroup::GetAccountName()
{
	char* result = NULL;
	unsigned long length = 0;

	if(currentAccountName == NULL)
	{
		goto exit;
		result = NULL;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(currentAccountName, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, resultBuf, &length);
	result = resultBuf;

exit:
	return result;
}

// ****************************************************************
// NTGroup::GetSIDHexStr
// ****************************************************************
char* NTGroup::GetSIDHexStr()
{	
	char* result = NULL;
	unsigned long length = 0;
	unsigned long binLength = 0;
	char* groupName = NULL;
	char* sid = NULL;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	UTF16ToUTF8(currentAccountName, NULL, &length);
	groupName = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, groupName, &length);

	if(GetSIDByAccountName(groupName, NULL, &binLength) != 0)
	{
		result = NULL;
		goto exit;
	}
	sid = (char*)malloc(binLength);
	GetSIDByAccountName(groupName, sid, &binLength);

	quickFree(&resultBuf);
	BinToHexStr(sid, binLength, NULL, &length);
	resultBuf = (char*)malloc(length);
	BinToHexStr(sid, binLength, resultBuf, &length);
	result = resultBuf;

exit:
	quickFree(&groupName);
	quickFree(&sid);

	return result;
}

// ****************************************************************
// NTGroup::GetComment
// ****************************************************************
char* NTGroup::GetComment()
{
	char* result = NULL;
	unsigned long length;
	PGROUP_INFO_2 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetGroupGetInfo(NULL, currentAccountName, 2, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->grpi2_comment, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->grpi2_comment, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTGroup::SetComment
// ****************************************************************
int NTGroup::SetComment(char* comment)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	GROUP_INFO_1002 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(comment, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(comment, wideStr, &length);

	info.grpi1002_comment = wideStr;
	result = NetGroupSetInfo(NULL, currentAccountName, 1002, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTGroup::AddUser
// ****************************************************************
int NTGroup::AddUser(char* userName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(userName, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(userName, wideStr, &length);

	result = NetGroupAddUser(NULL, currentAccountName, wideStr);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTGroup::RemoveUser
// ****************************************************************
int NTGroup::RemoveUser(char* userName)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(userName, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(userName, wideStr, &length);

	result = NetGroupDelUser(NULL, currentAccountName, wideStr);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTGroup::LoadUsers
// ****************************************************************
int NTGroup::LoadUsers()
{
	int result = 0;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
		currentUserEntry = 0;
		userEntriesRead = 0;
		userEntriesTotal = 0;
	}
	result = NetGroupGetUsers(NULL, currentAccountName, GROUP_USERS_INFO_LEVEL, (unsigned char**)&usersInfo, MAX_PREFERRED_LENGTH, &userEntriesRead, &userEntriesTotal, NULL);

exit:
	return result;
}

// ****************************************************************
// NTGroup::HasMoreUsers
// ****************************************************************
bool NTGroup::HasMoreUsers()
{
	bool result;

	if(currentUserEntry < userEntriesRead)
	{
		result = true;
	}
	else
	{
		result = false; 
	}

	return result;
}

// ****************************************************************
// NTGroup::NextUserName
// ****************************************************************
char* NTGroup::NextUserName()
{
	char* result = NULL;
	unsigned long length;

	if(currentUserEntry < userEntriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(usersInfo[currentUserEntry].lgrui0_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(usersInfo[currentUserEntry].lgrui0_name, resultBuf, &length);
		result = resultBuf;

		currentUserEntry++;
	}

	return result;
}

// ****************************************************************
// NTGroupList::NTGroupList
// ****************************************************************
NTGroupList::NTGroupList()
{
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
	resultBuf = NULL;
}

// ****************************************************************
// NTGroupList::~NTGroupList
// ****************************************************************
NTGroupList::~NTGroupList()
{
	if(bufptr != NULL)
	{
		NetApiBufferFree(bufptr);
		bufptr = NULL;
	}
	quickFree(&resultBuf);
}

// ****************************************************************
// NTGroupList::loadList
// ****************************************************************
int NTGroupList::loadList()
{
	int result;

	result = NetGroupEnum(NULL, GROUP_INFO_LEVEL, (LPBYTE*)&bufptr, MAX_PREFERRED_LENGTH, &entriesRead, &totalEntries, &resumeHandle);

	return result;
}

// ****************************************************************
// NTGroupList::hasMore
// ****************************************************************
bool NTGroupList::hasMore()
{
	bool result;

	if(currentEntry < entriesRead)
	{
		result = true;
	}
	else
	{
		result = false;
	}

	return result;
}

// ****************************************************************
// NTGroupList::nextGroupName
// ****************************************************************
char* NTGroupList::nextGroupName()
{
	char* result = NULL;
	unsigned long length;

	if(currentEntry < entriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(bufptr[currentEntry].grpi2_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(bufptr[currentEntry].grpi2_name, resultBuf, &length);
		result = resultBuf;

		currentEntry++;
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::NTLocalGroup
// ****************************************************************
NTLocalGroup::NTLocalGroup()
{
	currentAccountName = NULL;
	usersInfo = NULL;
	currentUserEntry = 0;
	userEntriesRead = 0;
	userEntriesTotal = 0;
	resultBuf = NULL;
}

// ****************************************************************
// NTLocalGroup::~NTLocalGroup
// ****************************************************************
NTLocalGroup::~NTLocalGroup()
{
	quickFree((char**)&currentAccountName);
	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
	}
	quickFree(&resultBuf);
}

// ****************************************************************
// NTLocalGroup::NewLocalGroup
// ****************************************************************
int NTLocalGroup::NewLocalGroup(char* localGroupName)
{

	int result = 0;
	unsigned long length;
	PLOCALGROUP_INFO_1 info = NULL;
	DWORD badParam = 0;

	quickFree((char**)&currentAccountName);
	UTF8ToUTF16(localGroupName, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(localGroupName, currentAccountName, &length);

	info = (PLOCALGROUP_INFO_1)malloc(sizeof(LOCALGROUP_INFO_1));
	memset(info, 0, sizeof(LOCALGROUP_INFO_1));
	info->lgrpi1_name = currentAccountName;

	// Add group
	result = NetLocalGroupAdd(NULL, LOCALGROUP_INFO_LEVEL, (unsigned char*)info, &badParam);

	// Free buffers
	quickFree((char**)&info);

	return result;
}

// ****************************************************************
// NTLocalGroup::RetriveLocalGroupByAccountName
// ****************************************************************
int NTLocalGroup::RetriveLocalGroupByAccountName(char* localGroupName)
{
	int result;
	unsigned long length;
	PLOCALGROUP_INFO_1 info = NULL;

	quickFree((char**)&currentAccountName);
	UTF8ToUTF16(localGroupName, NULL, &length);
	currentAccountName = (unsigned short*)malloc(length);
	UTF8ToUTF16(localGroupName, currentAccountName, &length);

	result = NetLocalGroupGetInfo(NULL, currentAccountName, LOCALGROUP_INFO_LEVEL, (unsigned char**)&info);

	return result;
}

// ****************************************************************
// NTGroup::RetriveLocalGroupBySIDHexStr
// ****************************************************************
int NTLocalGroup::RetriveLocalGroupBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	unsigned long length;
	char* localGroupName;
	char* sid;

	quickFree((char**)&currentAccountName);

	HexStrToBin(sidHexStr, NULL, &length);
	sid = (char*)malloc(length);
	HexStrToBin(sidHexStr, sid, &length);

	if(GetAccountNameBySID(sid, NULL, &length) != 0)
	{
		result = -1;
		goto exit;
	}
	localGroupName = (char*)malloc(length);
	GetAccountNameBySID(sid, localGroupName, &length);

	if(RetriveLocalGroupByAccountName(localGroupName) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	quickFree(&sid);
	quickFree(&localGroupName);

	return result;
}

// ****************************************************************
// NTLocalGroup::DeleteLocalGroup
// ****************************************************************
int NTLocalGroup::DeleteLocalGroup()
{
	int result;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	result = NetLocalGroupDel(NULL, currentAccountName);

	quickFree((char**)&currentAccountName);

exit:
	return result;
}

// ****************************************************************
// NTLocalGroup::GetAccountName
// ****************************************************************
char* NTLocalGroup::GetAccountName()
{
	char* result = NULL;
	unsigned long length = 0;

	if(currentAccountName == NULL)
	{
		goto exit;
		result = NULL;
	}

	quickFree((char**)&resultBuf);
	UTF16ToUTF8(currentAccountName, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, resultBuf, &length);
	result = resultBuf;

exit:
	return result;
}

// ****************************************************************
// NTLocalGroup::GetSIDHexStr
// ****************************************************************
char* NTLocalGroup::GetSIDHexStr()
{
	char* result = NULL;
	unsigned long length = 0;
	unsigned long binLength = 0;
	char* localGroupName = NULL;
	char* sid = NULL;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	UTF16ToUTF8(currentAccountName, NULL, &length);
	localGroupName = (char*)malloc(length);
	UTF16ToUTF8(currentAccountName, localGroupName, &length);

	if(GetSIDByAccountName(localGroupName, NULL, &binLength) != 0)
	{
		result = NULL;
		goto exit;
	}
	sid = (char*)malloc(binLength);
	GetSIDByAccountName(localGroupName, sid, &binLength);

	quickFree(&resultBuf);
	BinToHexStr(sid, binLength, NULL, &length);
	resultBuf = (char*)malloc(length);
	BinToHexStr(sid, binLength, resultBuf, &length);
	result = resultBuf;

exit:
	quickFree(&localGroupName);
	quickFree(&sid);

	return result;
}

// ****************************************************************
// NTLocalGroup::GetComment
// ****************************************************************
char* NTLocalGroup::GetComment()
{
	char* result = NULL;
	unsigned long length;
	PLOCALGROUP_INFO_1 info;

	if(currentAccountName == NULL)
	{
		result = NULL;
		goto exit;
	}

	if(NetLocalGroupGetInfo(NULL, currentAccountName, 1, (unsigned char**)&info) != NERR_Success)
	{
		result = NULL;
		goto exit;
	}

	quickFree(&resultBuf);
	UTF16ToUTF8(info->lgrpi1_comment, NULL, &length);
	resultBuf = (char*)malloc(length);
	UTF16ToUTF8(info->lgrpi1_comment, resultBuf, &length);
	result = resultBuf;

exit:
	NetApiBufferFree((void*)info);

	return result;
}

// ****************************************************************
// NTLocalGroup::SetComment
// ****************************************************************
int NTLocalGroup::SetComment(char* comment)
{
	int result = 0;
	unsigned long length;
	unsigned short* wideStr = NULL;
	LOCALGROUP_INFO_1002 info;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	UTF8ToUTF16(comment, NULL, &length);
	wideStr = (unsigned short*)malloc(length);
	UTF8ToUTF16(comment, wideStr, &length);

	info.lgrpi1002_comment = wideStr;
	result = NetLocalGroupSetInfo(NULL, currentAccountName, 1002, (unsigned char*)&info, NULL);

exit:
	quickFree((char**)&wideStr);

	return result;
}

// ****************************************************************
// NTLocalGroup::AddUser
// ****************************************************************
int NTLocalGroup::AddUser(char* username)
{
	int result = 0;
	unsigned long length;
	char* sid;
	LOCALGROUP_MEMBERS_INFO_0 members[1];

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	GetSIDByAccountName(username, NULL, &length);
	sid = (char*)malloc(length);
	GetSIDByAccountName(username, sid, &length);
	members[0].lgrmi0_sid = sid;

	result = NetLocalGroupAddMembers(NULL, currentAccountName, 0, (unsigned char*)&members, 1);

exit:
	quickFree(&sid);

	return result;
}

// ****************************************************************
// NTLocalGroup::RemoveUser
// ****************************************************************
int NTLocalGroup::RemoveUser(char* username)
{
	int result = 0;
	unsigned long length;
	char* sid;
	LOCALGROUP_MEMBERS_INFO_0 members[1];

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	GetSIDByAccountName(username, NULL, &length);
	sid = (char*)malloc(length);
	GetSIDByAccountName(username, sid, &length);
	members[0].lgrmi0_sid = sid;

	result = NetLocalGroupDelMembers(NULL, currentAccountName, 0, (unsigned char*)&members, 1);

exit:
	quickFree(&sid);

	return result;
}

// ****************************************************************
// NTLocalGroup::LoadUsers
// ****************************************************************
int NTLocalGroup::LoadUsers()
{
	int result = 0;

	if(currentAccountName == NULL)
	{
		result = -1;
		goto exit;
	}

	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
		currentUserEntry = 0;
		userEntriesRead = 0;
		userEntriesTotal = 0;
	}
	result = NetLocalGroupGetMembers(NULL, currentAccountName, LOCALGROUP_USERS_INFO_LEVEL, (unsigned char**)&usersInfo, MAX_PREFERRED_LENGTH, &userEntriesRead, &userEntriesTotal, NULL);

exit:
	return result;
}

// ****************************************************************
// NTLocalGroup::HasMoreUsers
// ****************************************************************
bool NTLocalGroup::HasMoreUsers()
{
	bool result;

	if(currentUserEntry < userEntriesRead)
	{
		result = true;
	}
	else
	{
		result = false; 
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::NextUserName
// ****************************************************************
char* NTLocalGroup::NextUserName()
{
	char* result = NULL;
	unsigned long length;

	if(currentUserEntry < userEntriesRead)
	{
		quickFree(&resultBuf);
		GetAccountNameBySID((char*)usersInfo[currentUserEntry].lgrmi0_sid, NULL, &length);
		resultBuf = (char*)malloc(length);
		GetAccountNameBySID((char*)usersInfo[currentUserEntry].lgrmi0_sid, resultBuf, &length);
		result = resultBuf;

		currentUserEntry++;
	}

	return result;
}

// ****************************************************************
// NTLocalGroupList::NTLocalGroupList
// ****************************************************************
NTLocalGroupList::NTLocalGroupList()
{
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
	resultBuf = NULL;
}

// ****************************************************************
// NTLocalGroupList::~NTLocalGroupList
// ****************************************************************
NTLocalGroupList::~NTLocalGroupList()
{
	if(bufptr != NULL)
	{
		NetApiBufferFree(bufptr);
		bufptr = NULL;
	}
	quickFree(&resultBuf);
}

// ****************************************************************
// NTLocalGroupList::loadList
// ****************************************************************
int NTLocalGroupList::loadList()
{
	int result;

	result = NetLocalGroupEnum(NULL, LOCALGROUP_INFO_LEVEL, (LPBYTE*)&bufptr, MAX_PREFERRED_LENGTH, &entriesRead, &totalEntries, &resumeHandle);

	return result;
}

// ****************************************************************
// NTLocalGroupList::hasMore
// ****************************************************************
bool NTLocalGroupList::hasMore()
{
	bool result;

	if(currentEntry < entriesRead)
	{
		result = true;
	}
	else
	{
		result = false;
	}

	return result;
}

// ****************************************************************
// NTLocalGroupList::nextLocalGroupName
// ****************************************************************
char* NTLocalGroupList::nextLocalGroupName()
{
	char* result = NULL;
	unsigned long length;

	if(currentEntry < entriesRead)
	{
		quickFree(&resultBuf);
		UTF16ToUTF8(bufptr[currentEntry].lgrpi1_name, NULL, &length);
		resultBuf = (char*)malloc(length);
		UTF16ToUTF8(bufptr[currentEntry].lgrpi1_name, resultBuf, &length);
		result = resultBuf;

		currentEntry++;
	}

	return result;
}
