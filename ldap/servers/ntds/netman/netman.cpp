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
// UTF16ToUTF8
// ****************************************************************
char* UTF16ToUTF8(unsigned short* inString)
{
	int length = WideCharToMultiByte(CP_ACP, 0, inString, -1, 0, 0, 0, 0);
	char* outString = NULL;

	outString =	(char*)malloc(length);

	WideCharToMultiByte(CP_ACP, 0, inString, -1, outString, length, 0, 0);

	return outString;
}

// ****************************************************************
// UTF8ToUTF16
// ****************************************************************
unsigned short* UTF8ToUTF16(char* inString)
{
	unsigned short* outString = NULL;
	int length = MultiByteToWideChar(CP_ACP, 0, inString, -1, 0, 0);

	outString =	(unsigned short*)malloc(length * 2);

	MultiByteToWideChar(CP_ACP, 0, inString, -1, outString, length);

	return outString;
}

// ****************************************************************
// BinToHexStr
// ****************************************************************
int BinToHexStr(char* bin, unsigned long binLen, char** hexStr)
{
	int hexStrLen = binLen * 2 + 1;

	*hexStr = (char*)calloc(hexStrLen, sizeof(char));

	for(unsigned long i = 0; i < binLen; i++)
	{
		sprintf(&(*hexStr)[i * 2], "%02X", (unsigned char)bin[i]);
	}

	return 0;
}

// ****************************************************************
// HexStrToBin
// ****************************************************************
int HexStrToBin(char* hexStr, char** bin, unsigned long* binLen)
{
	int temp;
	*binLen = strlen(hexStr) / 2;

	*bin = (char*)malloc(*binLen);

	for(unsigned long i = 0; i < *binLen; i++)
	{
		sscanf(&hexStr[i * 2], "%02X", &temp);
		(*bin)[i] = (unsigned char)temp;
	}

	return 0;
}

// ****************************************************************
// GetSIDByAccountName
// ****************************************************************
int GetSIDByAccountName(char* accountName, char** sid)
{
	int result = 0;
	unsigned long sidLen = 0;
	char* domain;
	unsigned long domainLen = 0;
	SID_NAME_USE testType;

	if(LookupAccountName(NULL, accountName, NULL, &sidLen, NULL, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}

	*sid = (char*)malloc(sidLen);
	domain = (char*)malloc(domainLen);

	if(LookupAccountName(NULL, accountName, *sid, &sidLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}
	else
	{
		result = 0;
	}

	return result;
}

// ****************************************************************
// GetAccountNameBySID
// ****************************************************************
int GetAccountNameBySID(char* sid, char** accountName)
{
	int result = 0;
	unsigned long sidLen = 0;
	char* domain;
	unsigned long domainLen = 0;
	SID_NAME_USE testType;

	unsigned long accountNameLen = 0;

	if(LookupAccountSid(NULL, sid, NULL, &accountNameLen, NULL, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}

	domain = (char*)malloc(domainLen);
	*accountName = (char*)calloc(accountNameLen, sizeof(char));

	if(LookupAccountSid(NULL, sid, *accountName, &accountNameLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}
	else
	{
		result = 0;
	}

	return result;
}

// ****************************************************************
// GetSIDHexStrByAccountName
// ****************************************************************
int GetSIDHexStrByAccountName(char* accountName, char** sidHexStr)
{
	int result = 0;
	char* sid;
	unsigned long sidLen = 0;
	char* domain;
	unsigned long domainLen = 0;
	SID_NAME_USE testType;

	if(LookupAccountName(NULL, accountName, NULL, &sidLen, NULL, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}

	sid = (char*)malloc(sidLen);
	domain = (char*)malloc(domainLen);

	if(LookupAccountName(NULL, accountName, sid, &sidLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}
	else
	{
		result = 0;
	}


	BinToHexStr(sid, sidLen, sidHexStr);

	return result;
}

// ****************************************************************
// GetAccountNameBySIDHexStr
// ****************************************************************
int GetAccountNameBySIDHexStr(char* sidHexStr, char** accountName)
{
	int result = 0;
	char* sid;
	unsigned long sidLen = 0;
	char* domain;
	unsigned long domainLen = 0;
	SID_NAME_USE testType;

	unsigned long accountNameLen = 0;

	HexStrToBin(sidHexStr, &sid, &sidLen);

	if(LookupAccountSid(NULL, sid, NULL, &accountNameLen, NULL, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}

	domain = (char*)malloc(domainLen);
	*accountName = (char*)calloc(accountNameLen, sizeof(char));

	if(LookupAccountSid(NULL, sid, *accountName, &accountNameLen, domain, &domainLen, &testType) == 0)
	{
		result = GetLastError();
	}
	else
	{
		result = 0;
	}

	return result;
}

// ****************************************************************
// NTUser::NTUser
// ****************************************************************
NTUser::NTUser()
{
	userInfo = NULL;

	groupsInfo = NULL;
	currentGroupEntry = 0;
	groupEntriesRead = 0;
	groupEntriesTotal = 0;

	localGroupsInfo = NULL;
	currentLocalGroupEntry = 0;
	localGroupEntriesRead = 0;
	localGroupEntriesTotal = 0;
}

// ****************************************************************
// NTUser::~NTUser
// ****************************************************************
NTUser::~NTUser()
{
	if(userInfo != NULL)
	{
		NetApiBufferFree(userInfo);
		userInfo = NULL;
	}
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
}

// ****************************************************************
// NTUser::NewUser
// ****************************************************************
void NTUser::NewUser(char* username)
{
	if(userInfo != NULL)
	{
		NetApiBufferFree(userInfo);
		userInfo = NULL;
	}

	NetApiBufferAllocate(sizeof(USER_INFO_3),(LPVOID*)&userInfo);
	memset(userInfo, 0, sizeof(USER_INFO_3));
	userInfo->usri3_name = UTF8ToUTF16(username);

	// Possible required inits for AddUser
	//userInfo->usri3_priv = USER_PRIV_USER;
	//userInfo->usri3_home_dir = NULL;
	//userInfo->usri3_comment = NULL;
	//userInfo->usri3_script_path = NULL;

	// NT4 required inits for AddUser
	userInfo->usri3_flags = UF_SCRIPT;
	userInfo->usri3_primary_group_id = DOMAIN_GROUP_RID_USERS;

	// Other inits
	userInfo->usri3_acct_expires = (unsigned long)-1;
}

// ****************************************************************
// NTUser::RetriveUserByUsername
// ****************************************************************
int NTUser::RetriveUserByAccountName(char* username)
{
	int result;

	if(userInfo != NULL)
	{
		NetApiBufferFree(userInfo);
		userInfo = NULL;
	}

	result = NetUserGetInfo(NULL, UTF8ToUTF16(username), USER_INFO_LEVEL, (unsigned char**)&userInfo);

	return result;
}

// ****************************************************************
// NTUser::RetriveUserBySIDHexStr
// ****************************************************************
int NTUser::RetriveUserBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	char* username;

	if(userInfo != NULL)
	{
		NetApiBufferFree(userInfo);
		userInfo = NULL;
	}

	if(GetAccountNameBySIDHexStr(sidHexStr, &username) != 0)
	{
		result = -1;
		goto exit;
	}

	if(RetriveUserByAccountName(username) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	return result;
}

// ****************************************************************
// NTUser::StoreUser
// ****************************************************************
int NTUser::StoreUser()
{
	int result = 0;

	if(userInfo != NULL)
	{
		result = NetUserSetInfo(NULL, userInfo->usri3_name, USER_INFO_LEVEL, (unsigned char*)userInfo, NULL);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::AddUser
// ****************************************************************
int NTUser::AddUser()
{
	int result;
	DWORD badParam = 0;

	result = NetUserAdd(NULL, USER_INFO_LEVEL, (unsigned char*)userInfo, &badParam);

	return result;
}

// ****************************************************************
// NTUser::DeleteUser
// ****************************************************************
int NTUser::DeleteUser(char* username)
{
	int result;

	result = NetUserDel(NULL, UTF8ToUTF16(username));

	return result;
}

// ****************************************************************
// NTUser::ChangeUsername
// ****************************************************************
int NTUser::ChangeUsername(char* oldUsername, char* newUsername)
{
	int result;

	if((result = RetriveUserByAccountName(oldUsername)) == 0)
	{
		userInfo->usri3_name = UTF8ToUTF16(newUsername);
		if((result = AddUser()) == 0)
		{
			if((result = DeleteUser(oldUsername)) != 0)
			{
				DeleteUser(newUsername);
			}
		}
	}

	return result;
}

// ****************************************************************
// NTUser::GetAccountName
// ****************************************************************
char* NTUser::GetAccountName()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_name);
	}

	return result;
}

// ****************************************************************
// NTUser::GetSIDHexStr
// ****************************************************************
char* NTUser::GetSIDHexStr()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		GetSIDHexStrByAccountName(UTF16ToUTF8(userInfo->usri3_name), &result);
	}

	return result;
}

// ****************************************************************
// NTUser::GetAccountExpires
// ****************************************************************
unsigned long NTUser::GetAccountExpires()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_acct_expires;
	}

	return result;
}

// ****************************************************************
// NTUser::SetAccountExpires
// ****************************************************************
int NTUser::SetAccountExpires(unsigned long accountExpires)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_acct_expires = accountExpires;
	}
	else
	{
		result = -1;
	}
	
	return result;
}

// ****************************************************************
// NTUser::GetBadPasswordCount
// ****************************************************************
unsigned long NTUser::GetBadPasswordCount()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_bad_pw_count;
	}

	return result;
}

// ****************************************************************
// NTUser::GetCodePage
// ****************************************************************
unsigned long NTUser::GetCodePage()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_code_page;
	}

	return result;
}

// ****************************************************************
// NTUser::SetCodePage
// ****************************************************************
int NTUser::SetCodePage(unsigned long codePage)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_code_page = codePage;
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetComment
// ****************************************************************
char* NTUser::GetComment()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result =  UTF16ToUTF8(userInfo->usri3_comment);
	}

	return result;
}

// ****************************************************************
// NTUser::SetComment
// ****************************************************************
int NTUser::SetComment(char* comment)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_comment = UTF8ToUTF16(comment);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetCountryCode
// ****************************************************************
unsigned long NTUser::GetCountryCode()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_country_code;
	}

	return result;
}

// ****************************************************************
// NTUser::SetCountryCode
// ****************************************************************
int NTUser::SetCountryCode(unsigned long countryCode)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_country_code = countryCode;
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetFlags
// ****************************************************************
unsigned long NTUser::GetFlags()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_flags;
	}

	return result;
}

// ****************************************************************
// NTUser::SetFlags
// ****************************************************************
int NTUser::SetFlags(unsigned long flags)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_flags = flags;
	}
	else
	{
		result = -1;
	}
	
	return result;
}

// ****************************************************************
// NTUser::GetHomeDir
// ****************************************************************
char* NTUser::GetHomeDir()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_home_dir);
	}

	return result;
}

// ****************************************************************
// NTUser::SetHomeDir
// ****************************************************************
int NTUser::SetHomeDir(char* path)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_home_dir = UTF8ToUTF16(path);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetHomeDirDrive
// ****************************************************************
char* NTUser::GetHomeDirDrive()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_home_dir_drive);
	}

	return result;
}

// ****************************************************************
// NTUser::SetHomeDirDrive
// ****************************************************************
int NTUser::SetHomeDirDrive(char* path)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_home_dir_drive = UTF8ToUTF16(path);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetLastLogoff
// ****************************************************************
unsigned long NTUser::GetLastLogoff()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_last_logoff;
	}

	return result;
}

// ****************************************************************
// NTUser::GetLastLogon
// ****************************************************************
unsigned long NTUser::GetLastLogon()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_last_logon;
	}

	return result;
}

// ****************************************************************
// NTUser::GetLogonHours
// ****************************************************************
char* NTUser::GetLogonHours()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		BinToHexStr((char*)userInfo->usri3_logon_hours, 21, &result);
	}

	return result;
}

// ****************************************************************
// NTUser::SetLogonHours
// ****************************************************************
int NTUser::SetLogonHours(char* logonHours)
{
	int result = 0;
	char* binValue;
	unsigned long binLen = 0;

	if(userInfo != NULL)
	{
		HexStrToBin(logonHours, &binValue, &binLen);
		userInfo->usri3_logon_hours = (unsigned char*)binValue;
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetMaxStorage
// ****************************************************************
unsigned long NTUser::GetMaxStorage()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_max_storage;
	}

	return result;
}

// ****************************************************************
// NTUser::SetMaxStorage
// ****************************************************************
int NTUser::SetMaxStorage(unsigned long maxStorage)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_max_storage = maxStorage;
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetNumLogons
// ****************************************************************
unsigned long NTUser::GetNumLogons()
{
	unsigned long result = 0;

	if(userInfo != NULL)
	{
		result = userInfo->usri3_num_logons;
	}

	return result;
}

// ****************************************************************
// NTUser::GetProfile
// ****************************************************************
char* NTUser::GetProfile()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_profile);
	}

	return result;
}

// ****************************************************************
// NTUser::SetProfile
// ****************************************************************
int NTUser::SetProfile(char* path)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_profile = UTF8ToUTF16(path);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetScriptPath
// ****************************************************************
char* NTUser::GetScriptPath()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_script_path);
	}

	return result;
}

// ****************************************************************
// NTUser::SetScriptPath
// ****************************************************************
int NTUser::SetScriptPath(char* path)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_script_path = UTF8ToUTF16(path);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetWorkstations
// ****************************************************************
char* NTUser::GetWorkstations()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_workstations);
	}

	return result;
}

// ****************************************************************
// NTUser::SetWorkstations
// ****************************************************************
int NTUser::SetWorkstations(char* workstations)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_workstations = UTF8ToUTF16(workstations);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::GetFullname
// ****************************************************************
char* NTUser::GetFullname()
{
	char* result = NULL;

	if(userInfo != NULL)
	{
		result = UTF16ToUTF8(userInfo->usri3_full_name);
	}

	return result;
}

// ****************************************************************
// NTUser::SetFullname
// ****************************************************************
int NTUser::SetFullname(char* fullname)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_full_name = UTF8ToUTF16(fullname);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::SetPassword
// ****************************************************************
int NTUser::SetPassword(char* password)
{
	int result = 0;

	if(userInfo != NULL)
	{
		userInfo->usri3_password = UTF8ToUTF16(password);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::AddToGroup
// ****************************************************************
int NTUser::AddToGroup(char* groupName)
{
	int result = 0;

	if(userInfo != NULL)
	{
		result = NetGroupAddUser(NULL, UTF8ToUTF16(groupName), userInfo->usri3_name);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::RemoveFromGroup
// ****************************************************************
int NTUser::RemoveFromGroup(char* groupName)
{
	int result = 0;

	if(userInfo != NULL)
	{
		result = NetGroupDelUser(NULL, UTF8ToUTF16(groupName), userInfo->usri3_name);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::LoadGroups
// ****************************************************************
int NTUser::LoadGroups()
{
	int result = 0;

	if(userInfo != NULL)
	{
		if(groupsInfo != NULL)
		{
			NetApiBufferFree(groupsInfo);
			groupsInfo = NULL;
			currentGroupEntry = 0;
			groupEntriesRead = 0;
			groupEntriesTotal = 0;
		}
		result = NetUserGetGroups(NULL, userInfo->usri3_name, USER_GROUPS_INFO_LEVEL, (unsigned char**)&groupsInfo, MAX_PREFERRED_LENGTH, &groupEntriesRead, &groupEntriesTotal);
	}
	else
	{
		result = -1;
	}

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
	char* groupName = NULL;
	GROUP_USERS_INFO_0* thisEntry;

	if(currentGroupEntry < groupEntriesRead)
	{
		thisEntry = &(groupsInfo[currentGroupEntry]);
		groupName = UTF16ToUTF8(thisEntry->grui0_name);
		currentGroupEntry++;
	}

	return groupName;
}

// ****************************************************************
// NTUser::AddToLocalGroup
// ****************************************************************
int NTUser::AddToLocalGroup(char* localGroupName)
{
	int result = 0;
	char userSID[256];
	wchar_t domain[256];
	DWORD SIDLen = sizeof(userSID);
	DWORD domainLen = sizeof(domain);
	SID_NAME_USE SIDUseIndicator;
	LOCALGROUP_MEMBERS_INFO_0 membersbuf[1];

	memset(&domain, 0, sizeof(domain));
	memset(&userSID, 0, sizeof(userSID));

	if(userInfo != NULL)
	{
		result = LookupAccountName(NULL, UTF16ToUTF8(userInfo->usri3_name), &userSID, &SIDLen, (LPTSTR)&domain, &domainLen, &SIDUseIndicator);

		if(result != 0)
		{
			membersbuf[0].lgrmi0_sid = &userSID;
			result = NetLocalGroupAddMembers(NULL, UTF8ToUTF16(localGroupName), 0, (LPBYTE)&membersbuf, 1);
		}
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::RemoveFromLocalGroup
// ****************************************************************
int NTUser::RemoveFromLocalGroup(char* localGroupName)
{
	int result = 0;
	char userSID[256];
	wchar_t domain[256];
	DWORD SIDLen = sizeof(userSID);
	DWORD domainLen = sizeof(domain);
	SID_NAME_USE SIDUseIndicator;
	LOCALGROUP_MEMBERS_INFO_0 membersbuf[1];

	memset(&domain, 0, sizeof(domain));
	memset(&userSID, 0, sizeof(userSID));

	if(userInfo != NULL)
	{
		result = LookupAccountName(NULL, UTF16ToUTF8(userInfo->usri3_name), &userSID, &SIDLen, (LPTSTR)&domain, &domainLen, &SIDUseIndicator);

		if(result != 0)
		{
			membersbuf[0].lgrmi0_sid = &userSID;
			result = NetLocalGroupDelMembers(NULL, UTF8ToUTF16(localGroupName), 0, (LPBYTE)&membersbuf, 1);
		}
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTUser::LoadLocalGroups
// ****************************************************************
int NTUser::LoadLocalGroups()
{
	int result = 0;

	if(userInfo != NULL)
	{
		if(localGroupsInfo != NULL)
		{
			NetApiBufferFree(localGroupsInfo);
			localGroupsInfo = NULL;
			currentLocalGroupEntry = 0;
			localGroupEntriesRead = 0;
			localGroupEntriesTotal = 0;
		}

		result = NetUserGetLocalGroups(NULL, userInfo->usri3_name, 0, USER_LOCALGROUPS_INFO_LEVEL, (unsigned char**)&localGroupsInfo, MAX_PREFERRED_LENGTH, &localGroupEntriesRead, &localGroupEntriesTotal);
	}
	else
	{
		result = -1;
	}

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
	char* localGroupName = NULL;
	LOCALGROUP_USERS_INFO_0* thisEntry;

	if(currentLocalGroupEntry < localGroupEntriesRead)
	{
		thisEntry = &(localGroupsInfo[currentLocalGroupEntry]);
		localGroupName = UTF16ToUTF8(thisEntry->lgrui0_name);
		currentLocalGroupEntry++;
	}

	return localGroupName;
}

// ****************************************************************
// NTUserList::NTUserList
// ****************************************************************
NTUserList::NTUserList()
{
	entriesRead = 0;
	totalEntries = 0;
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
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
	char* username = NULL;
	USER_INFO_3* thisEntry;

	if(currentEntry < entriesRead)
	{
		thisEntry = &(bufptr[currentEntry]);
		username = UTF16ToUTF8(thisEntry->usri3_name);
		currentEntry++;
	}

	return username;
}

// ****************************************************************
// NTGroup::NTGroup
// ****************************************************************
NTGroup::NTGroup()
{
	groupInfo = NULL;
	usersInfo = NULL;
	currentUserEntry = 0;
	userEntriesRead = 0;
	userEntriesTotal = 0;
}

// ****************************************************************
// NTGroup::~NTGroup
// ****************************************************************
NTGroup::~NTGroup()
{
	if(groupInfo != NULL)
	{
		NetApiBufferFree(groupInfo);
		groupInfo = NULL;
	}
	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
	}
}

// ****************************************************************
// NTGroup::NewGroup
// ****************************************************************
void NTGroup::NewGroup(char* groupName)
{
	if(groupInfo != NULL)
	{
		NetApiBufferFree(groupInfo);
		groupInfo = NULL;
	}

	NetApiBufferAllocate(sizeof(GROUP_INFO_2),(LPVOID*)&groupInfo);
	memset(groupInfo, 0, sizeof(GROUP_INFO_2));
	groupInfo->grpi2_name = UTF8ToUTF16(groupName);
}

// ****************************************************************
// NTGroup::DeleteGroup
// ****************************************************************
int NTGroup::DeleteGroup(char* groupName)
{
	int result;

	result = NetGroupDel(NULL, UTF8ToUTF16(groupName));

	return result;
}

// ****************************************************************
// NTGroup::RetriveGroup
// ****************************************************************
int NTGroup::RetriveGroupByAccountName(char* groupName)
{
	int result;

	if(groupInfo != NULL)
	{
		NetApiBufferFree(groupInfo);
		groupInfo = NULL;
	}

	result = NetGroupGetInfo(NULL, UTF8ToUTF16(groupName), GROUP_INFO_LEVEL, (unsigned char**)&groupInfo);

	return result;
}

// ****************************************************************
// NTGroup::RetriveGroupBySIDHexStr
// ****************************************************************
int NTGroup::RetriveGroupBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	char* groupName;

	if(groupInfo != NULL)
	{
		NetApiBufferFree(groupInfo);
		groupInfo = NULL;
	}

	if(GetAccountNameBySIDHexStr(sidHexStr, &groupName) != 0)
	{
		result = -1;
		goto exit;
	}

	if(RetriveGroupByAccountName(groupName) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	return result;
}

// ****************************************************************
// NTGroup::AddGroup
// ****************************************************************
int NTGroup::AddGroup()
{
	int result;
	DWORD badParam = 0;

	result = NetGroupAdd(NULL, GROUP_INFO_LEVEL, (unsigned char*)groupInfo, &badParam);

	return result;
}

// ****************************************************************
// NTGroup::StoreGroup
// ****************************************************************
int NTGroup::StoreGroup()
{
	int result = -1;

	if(groupInfo != NULL)
	{
		result = NetGroupSetInfo(NULL, groupInfo->grpi2_name, GROUP_INFO_LEVEL, (unsigned char*)groupInfo, NULL);
	}

	return result;
}

// ****************************************************************
// NTGroup::GetAccountName
// ****************************************************************
char* NTGroup::GetAccountName()
{
	char* result = NULL;

	if(groupInfo != NULL)
	{
		result = UTF16ToUTF8(groupInfo->grpi2_name);
	}

	return result;
}

// ****************************************************************
// NTGroup::GetSIDHexStr
// ****************************************************************
char* NTGroup::GetSIDHexStr()
{
	char* result = NULL;

	if(groupInfo != NULL)
	{
		GetSIDHexStrByAccountName(UTF16ToUTF8(groupInfo->grpi2_name), &result);
	}

	return result;
}

// ****************************************************************
// NTGroup::GetComment
// ****************************************************************
char* NTGroup::GetComment()
{
	char* result = NULL;

	if(groupInfo != NULL)
	{
		result = UTF16ToUTF8(groupInfo->grpi2_comment);
	}

	return result;
}

// ****************************************************************
// NTGroup::SetComment
// ****************************************************************
int NTGroup::SetComment(char* comment)
{
	int result = 0;

	if(groupInfo != NULL)
	{
		groupInfo->grpi2_comment = UTF8ToUTF16(comment);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTGroup::AddUser
// ****************************************************************
int NTGroup::AddUser(char* userName)
{
	int result = 0;

	if(groupInfo != NULL)
	{
		result = NetGroupAddUser(NULL, groupInfo->grpi2_name, UTF8ToUTF16(userName));
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTGroup::RemoveUser
// ****************************************************************
int NTGroup::RemoveUser(char* userName)
{
	int result = 0;

	if(groupInfo != NULL)
	{
		result = NetGroupDelUser(NULL, groupInfo->grpi2_name, UTF8ToUTF16(userName));
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTGroup::LoadUsers
// ****************************************************************
int NTGroup::LoadUsers()
{
	int result = 0;

	if(groupInfo != NULL)
	{
		if(usersInfo != NULL)
		{
			NetApiBufferFree(usersInfo);
			usersInfo = NULL;
			currentUserEntry = 0;
			userEntriesRead = 0;
			userEntriesTotal = 0;
		}
		result = NetGroupGetUsers(NULL, groupInfo->grpi2_name, GROUP_USERS_INFO_LEVEL, (unsigned char**)&usersInfo, MAX_PREFERRED_LENGTH, &userEntriesRead, &userEntriesTotal, NULL);
	}
	else
	{
		result = -1;
	}

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
	char* userName = NULL;
	LOCALGROUP_USERS_INFO_0* thisEntry;

	if(currentUserEntry < userEntriesRead)
	{
		thisEntry = &(usersInfo[currentUserEntry]);
		userName = UTF16ToUTF8(thisEntry->lgrui0_name);
		currentUserEntry++;
	}

	return userName;
}

// ****************************************************************
// NTGroupList::NTGroupList
// ****************************************************************
NTGroupList::NTGroupList()
{
	entriesRead = 0;
	totalEntries = 0;
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
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
	char* groupName = NULL;
	GROUP_INFO_2* thisEntry;

	if(currentEntry < entriesRead)
	{
		thisEntry = &(bufptr[currentEntry]);
		groupName = UTF16ToUTF8(thisEntry->grpi2_name);
		currentEntry++;
	}

	return groupName;
}

// ****************************************************************
// NTLocalGroup::NTLocalGroup
// ****************************************************************
NTLocalGroup::NTLocalGroup()
{
	localGroupInfo = NULL;
	usersInfo = NULL;
	currentUserEntry = 0;
	userEntriesRead = 0;
	userEntriesTotal = 0;
}

// ****************************************************************
// NTLocalGroup::~NTLocalGroup
// ****************************************************************
NTLocalGroup::~NTLocalGroup()
{
	if(localGroupInfo != NULL)
	{
		NetApiBufferFree(localGroupInfo);
		localGroupInfo = NULL;
	}
	if(usersInfo != NULL)
	{
		NetApiBufferFree(usersInfo);
		usersInfo = NULL;
	}
}

// ****************************************************************
// NTLocalGroup::NewLocalGroup
// ****************************************************************
void NTLocalGroup::NewLocalGroup(char* localGroupName)
{
	if(localGroupInfo != NULL)
	{
		NetApiBufferFree(localGroupInfo);
		localGroupInfo = NULL;
	}

	NetApiBufferAllocate(sizeof(LOCALGROUP_INFO_1),(LPVOID*)&localGroupInfo);
	memset(localGroupInfo, 0, sizeof(LOCALGROUP_INFO_1));
	localGroupInfo->lgrpi1_name = UTF8ToUTF16(localGroupName);
}

// ****************************************************************
// NTLocalGroup::DeleteLocalGroup
// ****************************************************************
int NTLocalGroup::DeleteLocalGroup(char* localGroupName)
{
	int result;

	result = NetLocalGroupDel(NULL, UTF8ToUTF16(localGroupName));

	return result;
}

// ****************************************************************
// NTLocalGroup::RetriveLocalGroup
// ****************************************************************
int NTLocalGroup::RetriveLocalGroupByAccountName(char* localGroupName)
{
	int result;

	if(localGroupInfo != NULL)
	{
		NetApiBufferFree(localGroupInfo);
		localGroupInfo = NULL;
	}

	result = NetLocalGroupGetInfo(NULL, UTF8ToUTF16(localGroupName), LOCALGROUP_INFO_LEVEL, (unsigned char**)&localGroupInfo);

	return result;
}

// ****************************************************************
// NTGroup::RetriveLocalGroupBySIDHexStr
// ****************************************************************
int NTLocalGroup::RetriveLocalGroupBySIDHexStr(char* sidHexStr)
{
	int result = 0;
	char* localGroupName;

	if(localGroupInfo != NULL)
	{
		NetApiBufferFree(localGroupInfo);
		localGroupInfo = NULL;
	}

	if(GetAccountNameBySIDHexStr(sidHexStr, &localGroupName) != 0)
	{
		result = -1;
		goto exit;
	}

	if(RetriveLocalGroupByAccountName(localGroupName) != 0)
	{
		result =-1;
		goto exit;
	}

exit:
	return result;
}

// ****************************************************************
// NTLocalGroup::AddLocalGroup
// ****************************************************************
int NTLocalGroup::AddLocalGroup()
{
	int result;
	DWORD badParam = 0;

	result = NetLocalGroupAdd(NULL, LOCALGROUP_INFO_LEVEL, (unsigned char*)localGroupInfo, &badParam);

	return result;
}

// ****************************************************************
// NTLocalGroup::StoreLocalGroup
// ****************************************************************
int NTLocalGroup::StoreLocalGroup()
{
	int result = 0;

	if(localGroupInfo != NULL)
	{
		result = NetLocalGroupSetInfo(NULL, localGroupInfo->lgrpi1_name, LOCALGROUP_INFO_LEVEL, (unsigned char*)localGroupInfo, NULL);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::GetAccountName
// ****************************************************************
char* NTLocalGroup::GetAccountName()
{
	char* result = NULL;

	if(localGroupInfo != NULL)
	{
		result = UTF16ToUTF8(localGroupInfo->lgrpi1_name);
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::GetSIDHexStr
// ****************************************************************
char* NTLocalGroup::GetSIDHexStr()
{
	char* result = NULL;

	if(localGroupInfo != NULL)
	{
		GetSIDHexStrByAccountName(UTF16ToUTF8(localGroupInfo->lgrpi1_name), &result);
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::GetComment
// ****************************************************************
char* NTLocalGroup::GetComment()
{
	char* result = NULL;

	if(localGroupInfo != NULL)
	{
		result = UTF16ToUTF8(localGroupInfo->lgrpi1_comment);
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::SetComment
// ****************************************************************
int NTLocalGroup::SetComment(char* comment)
{
	int result = 0;

	if(localGroupInfo != NULL)
	{
		localGroupInfo->lgrpi1_comment = UTF8ToUTF16(comment);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::AddUser
// ****************************************************************
int NTLocalGroup::AddUser(char* username)
{
	int result = 0;
	LOCALGROUP_MEMBERS_INFO_0 members[1];

	if(localGroupInfo != NULL)
	{
		GetSIDByAccountName(username, (char**)&members[0].lgrmi0_sid);
		result = NetLocalGroupAddMembers(NULL, localGroupInfo->lgrpi1_name, 0, (unsigned char*)&members, 1);
		free(members[0].lgrmi0_sid);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::RemoveUser
// ****************************************************************
int NTLocalGroup::RemoveUser(char* username)
{
	int result = 0;
	LOCALGROUP_MEMBERS_INFO_0 members[1];

	if(localGroupInfo != NULL)
	{
		GetSIDByAccountName(username, (char**)&members[0].lgrmi0_sid);
		result = NetLocalGroupDelMembers(NULL, localGroupInfo->lgrpi1_name, 0, (unsigned char*)&members, 1);
		free(members[0].lgrmi0_sid);
	}
	else
	{
		result = -1;
	}

	return result;
}

// ****************************************************************
// NTLocalGroup::LoadUsers
// ****************************************************************
int NTLocalGroup::LoadUsers()
{
	int result = 0;

	if(localGroupInfo != NULL)
	{
		if(usersInfo != NULL)
		{
			NetApiBufferFree(usersInfo);
			usersInfo = NULL;
			currentUserEntry = 0;
			userEntriesRead = 0;
			userEntriesTotal = 0;
		}
		result = NetLocalGroupGetMembers(NULL, localGroupInfo->lgrpi1_name, LOCALGROUP_USERS_INFO_LEVEL, (unsigned char**)&usersInfo, MAX_PREFERRED_LENGTH, &userEntriesRead, &userEntriesTotal, NULL);
	}
	else
	{
		result = -1;
	}

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
	char* username = NULL;
	LOCALGROUP_MEMBERS_INFO_0* thisEntry;

	if(currentUserEntry < userEntriesRead)
	{
		thisEntry = &(usersInfo[currentUserEntry]);
		GetAccountNameBySID((char*)thisEntry->lgrmi0_sid, &username);
		currentUserEntry++;
	}

	return username;
}

// ****************************************************************
// NTLocalGroupList::NTLocalGroupList
// ****************************************************************
NTLocalGroupList::NTLocalGroupList()
{
	entriesRead = 0;
	totalEntries = 0;
	bufptr = NULL;
	currentEntry = 0;
	resumeHandle = 0;
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
	char* localGroupName = NULL;
	LOCALGROUP_INFO_1* thisEntry;

	if(currentEntry < entriesRead)
	{
		thisEntry = &(bufptr[currentEntry]);
		localGroupName = UTF16ToUTF8(thisEntry->lgrpi1_name);
		currentEntry++;
	}

	return localGroupName;
}
