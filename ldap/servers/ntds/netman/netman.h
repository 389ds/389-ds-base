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
// netman.h
// ****************************************************************
#include <windows.h>
#include <lm.h>
#include <stdio.h>

#define USER_INFO_LEVEL 3
#define USER_GROUPS_INFO_LEVEL 0
#define USER_LOCALGROUPS_INFO_LEVEL 0

#define GROUP_INFO_LEVEL 2
#define GROUP_USERS_INFO_LEVEL 0

#define LOCALGROUP_INFO_LEVEL 1
#define LOCALGROUP_USERS_INFO_LEVEL 0

#define NETMAN_BUF_LEN 256

// ****************************************************************
// NTUser
// ****************************************************************
class NTUser
{
public:
	NTUser();
	~NTUser();

	void NewUser(char* username);
	int RetriveUserByAccountName(char* username);
	int RetriveUserBySIDHexStr(char* sidHexStr);
	int StoreUser();
	int AddUser();
	int DeleteUser(char* username);
	int ChangeUsername(char* oldUsername, char* newUsername);
	char* GetAccountName();
	char* GetSIDHexStr();

	unsigned long GetAccountExpires();
	int SetAccountExpires(unsigned long accountExpires);
	unsigned long GetBadPasswordCount();
	unsigned long GetCodePage();
	int SetCodePage(unsigned long codePage);
	char* GetComment();
	int SetComment(char* comment);
	unsigned long GetCountryCode();
	int SetCountryCode(unsigned long countryCode);
	unsigned long GetFlags();
	int SetFlags(unsigned long flags);
	char* GetHomeDir();
	int SetHomeDir(char* path);
	char* GetHomeDirDrive();
	int SetHomeDirDrive(char* path);
	unsigned long GetLastLogoff();
	unsigned long GetLastLogon();
	char* GetLogonHours();
	int SetLogonHours(char* logonHours);
	unsigned long GetMaxStorage();
	int SetMaxStorage(unsigned long maxStorage);
	unsigned long GetNumLogons();
	char* GetProfile();
	int SetProfile(char* path);
	char* GetScriptPath();
	int SetScriptPath(char* path);
	char* GetWorkstations();
	int SetWorkstations(char* workstations);
	char* GetFullname();
	int SetFullname(char* fullname);
	int SetPassword(char* password);

	int AddToGroup(char* groupName);
	int RemoveFromGroup(char* groupName);
	int LoadGroups();
	bool HasMoreGroups();
	char* NextGroupName();

	int AddToLocalGroup(char* localGroupName);
	int RemoveFromLocalGroup(char* localGroupName);
	int LoadLocalGroups();
	bool HasMoreLocalGroups();
	char* NextLocalGroupName();

private:
	USER_INFO_3* userInfo;

	GROUP_USERS_INFO_0* groupsInfo;
	DWORD currentGroupEntry;
	DWORD groupEntriesRead;
	DWORD groupEntriesTotal;

	LOCALGROUP_USERS_INFO_0* localGroupsInfo;
	DWORD currentLocalGroupEntry;
	DWORD localGroupEntriesRead;
	DWORD localGroupEntriesTotal;
};

// ****************************************************************
// NTUserList
// ****************************************************************
class NTUserList
{
public:
	NTUserList();
	~NTUserList();

	int loadList();
	bool hasMore();
	char* nextUsername();

private:
	USER_INFO_3*  bufptr;
	DWORD entriesRead;
	DWORD totalEntries;
	DWORD resumeHandle;
	DWORD currentEntry;
};

// ****************************************************************
// NTGroup
// ****************************************************************
class NTGroup
{
public:
	NTGroup();
	~NTGroup();

	void NewGroup(char* groupName);
	int DeleteGroup(char* groupName);
	int RetriveGroupByAccountName(char* groupName);
	int RetriveGroupBySIDHexStr(char* sidHexStr);
	int AddGroup();
	int StoreGroup();
	char* GetAccountName();
	char* GetSIDHexStr();

	char* GetComment();
	int SetComment(char* comment);

	int AddUser(char* username);
	int RemoveUser(char* username);
	int LoadUsers();
	bool HasMoreUsers();
	char* NextUserName();

private:
	GROUP_INFO_2* groupInfo;

	LOCALGROUP_USERS_INFO_0* usersInfo;
	DWORD currentUserEntry;
	DWORD userEntriesRead;
	DWORD userEntriesTotal;

};

// ****************************************************************
// NTGroupList
// ****************************************************************
class NTGroupList
{
public:
	NTGroupList();
	~NTGroupList();

	int loadList();
	bool hasMore();
	char* nextGroupName();

private:
	GROUP_INFO_2* bufptr;
	DWORD entriesRead;
	DWORD totalEntries;
	DWORD resumeHandle;
	DWORD currentEntry;
};

// ****************************************************************
// NTLocalGroup
// ****************************************************************
class NTLocalGroup
{
public:
	NTLocalGroup();
	~NTLocalGroup();

	void NewLocalGroup(char* localGroupName);
	int DeleteLocalGroup(char* localGroupName);
	int RetriveLocalGroupByAccountName(char* localGroupName);
	int RetriveLocalGroupBySIDHexStr(char* sidHexStr);
	int AddLocalGroup();
	int StoreLocalGroup();
	char* GetAccountName();
	char* GetSIDHexStr();

	char* GetComment();
	int SetComment(char* comment);

	int AddUser(char* username);
	int RemoveUser(char* username);
	int LoadUsers();
	bool HasMoreUsers();
	char* NextUserName();

private:
	LOCALGROUP_INFO_1* localGroupInfo;

	LOCALGROUP_MEMBERS_INFO_0* usersInfo;
	DWORD currentUserEntry;
	DWORD userEntriesRead;
	DWORD userEntriesTotal;
};

// ****************************************************************
// NTLocalGroupList
// ****************************************************************
class NTLocalGroupList
{
public:
	NTLocalGroupList();
	~NTLocalGroupList();

	int loadList();
	bool hasMore();
	char* nextLocalGroupName();

private:
	LOCALGROUP_INFO_1* bufptr;
	DWORD entriesRead;
	DWORD totalEntries;
	DWORD resumeHandle;
	DWORD currentEntry;
};
