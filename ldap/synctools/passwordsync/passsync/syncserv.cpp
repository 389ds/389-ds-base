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
#include "syncserv.h"

#include "prerror.h"
static char* certdbh;

// ****************************************************************
// passwdcb
// ****************************************************************
char* passwdcb(PK11SlotInfo* info, PRBool retry, void* arg)
{
	char* result = NULL;
	unsigned long resultLen = 0;
	DWORD type;
	HKEY regKey;

	if (!retry)
	{
		RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\PasswordSync", &regKey);
		RegQueryValueEx(regKey, "Install Path", NULL, &type, NULL, &resultLen);
		result = (char*)malloc(resultLen);
		RegQueryValueEx(regKey, "Cert Token", NULL, &type, (unsigned char*)result, &resultLen);
		RegCloseKey(regKey);
	}

	return result;
}

// ****************************************************************
// PassSyncService::PassSyncService
// ****************************************************************
PassSyncService::PassSyncService(const TCHAR *serviceName) : CNTService(serviceName)
{
	char sysPath[SYNCSERV_BUF_SIZE];
	char tempRegBuff[SYNCSERV_BUF_SIZE];
	HKEY regKey;
	DWORD type;
	unsigned long size;

	passhookEventHandle = CreateEvent(NULL, FALSE, FALSE, PASSHAND_EVENT_NAME);
	mainLdapConnection = NULL;
	results = NULL;
	currentResult = NULL;
	lastLdapError = LDAP_SUCCESS;
	certdbh = NULL;

	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\PasswordSync", &regKey);

	size = SYNCSERV_BUF_SIZE;
	if(RegQueryValueEx(regKey, "Log Level", NULL, &type, (unsigned char*)tempRegBuff, &size) == ERROR_SUCCESS)
	{
		logLevel = (unsigned long)atoi(tempRegBuff);
	}
	else
	{
		logLevel = 0;
	}

	size = SYNCSERV_BUF_SIZE;
	if(RegQueryValueEx(regKey, "Time To Live", NULL, &type, (unsigned char*)tempRegBuff, &size) == ERROR_SUCCESS)
	{
		maxBackoffTime = (unsigned long)atoi(tempRegBuff);
	}
	else
	{
		maxBackoffTime = (1 << 12) * SYNCSERV_BASE_BACKOFF_LEN;
	}

	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Install Path", NULL, &type, (unsigned char*)installPath, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Host Name", NULL, &type, (unsigned char*)ldapHostName, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Port Number", NULL, &type, (unsigned char*)ldapHostPort, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "User Name", NULL, &type, (unsigned char*)ldapAuthUsername, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Password", NULL, &type, (unsigned char*)ldapAuthPassword, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Search Base", NULL, &type, (unsigned char*)ldapSearchBase, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "User Name Field", NULL, &type, (unsigned char*)ldapUsernameField, &size);
	size = SYNCSERV_BUF_SIZE;
	RegQueryValueEx(regKey, "Password Field", NULL, &type, (unsigned char*)ldapPasswordField, &size);
	RegCloseKey(regKey);

	ExpandEnvironmentStrings("%SystemRoot%", sysPath, SYNCSERV_BUF_SIZE);
	_snprintf(certPath, SYNCSERV_BUF_SIZE, "%s", installPath);
	_snprintf(logPath, SYNCSERV_BUF_SIZE, "%spasssync.log", installPath);
	_snprintf(dataFilename, SYNCSERV_BUF_SIZE, "%s\\system32\\passhook.dat", sysPath);

	outLog.open(logPath, ios::out | ios::app);

	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "PassSync service started" << endl;
	}

	PK11_SetPasswordFunc(passwdcb);

	isRunning = false;
}

// ****************************************************************
// PassSyncService::~PassSyncService
// ****************************************************************
PassSyncService::~PassSyncService()
{
	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "PassSync service stopped" << endl;
	}
	outLog.close();
}

// ****************************************************************
// 
// ****************************************************************
void PassSyncService::OnStop()
{
	isRunning = false;
	SetEvent(passhookEventHandle);
}

// ****************************************************************
// 
// ****************************************************************
void PassSyncService::OnShutdown()
{
	isRunning = false;
	SetEvent(passhookEventHandle);
}

// ****************************************************************
// PassSyncService::Run
// ****************************************************************
void PassSyncService::Run()
{
	isRunning = true;

	// Initialize NSS
	if(ldapssl_client_init(certPath, &certdbh) != 0)
	{
		timeStamp(&outLog);
		outLog << "Error initializing SSL: err=" << PR_GetError() << endl;
		timeStamp(&outLog);
		outLog << "Ensure that your SSL is setup correctly" << endl;

		goto exit;
	}

	SyncPasswords();

	while(isRunning)
	{
		if(passInfoList.empty())
		{
			if(logLevel > 0) {
				timeStamp(&outLog);
				outLog << "Password list is empty.  Waiting for passhook event" << endl;
			}
			WaitForSingleObject(passhookEventHandle, INFINITE);
			if(logLevel > 0) {
				timeStamp(&outLog);
				outLog << "Received passhook event.  Attempting sync" << endl;
			}
		}
		else
		{
			if(logLevel > 0) {
				timeStamp(&outLog);
				outLog << "Backing off for " << BackoffTime(GetMinBackoff()) << "ms" << endl;
			}
			WaitForSingleObject(passhookEventHandle, BackoffTime(GetMinBackoff()));
			if(logLevel > 0) {
				timeStamp(&outLog);
				outLog << "Backoff time expired.  Attempting sync" << endl;
			}
		}

		SyncPasswords();
		UpdateBackoff();

		ResetEvent(passhookEventHandle);
	}

	if(passInfoList.size() > 0)
	{
		if(saveSet(&passInfoList, dataFilename) == 0)
		{
			if(logLevel > 0)
			{
				timeStamp(&outLog);
				outLog << passInfoList.size() << " entries saved to data file" << endl;
			}
		}
		else
		{
			timeStamp(&outLog);
			outLog << "Failed to save entries to data file" << endl;
		}
	}

exit:
	CloseHandle(passhookEventHandle);
}

// ****************************************************************
// PassSyncService::SyncPasswords
// ****************************************************************
int PassSyncService::SyncPasswords()
{
	int result = 0;
	PASS_INFO_LIST emptyPassInfoList;
	PASS_INFO_LIST_ITERATOR currentPassInfo;
	PASS_INFO_LIST_ITERATOR tempPassInfo;
	char* dn;
	int tempSize = passInfoList.size();

	if(loadSet(&passInfoList, dataFilename) == 0)
	{
		if((passInfoList.size() - tempSize) > 0)
		{
			if(logLevel > 0)
			{
				timeStamp(&outLog);
				outLog << passInfoList.size() - tempSize << " new entries loaded from data file" << endl;
			}

			if(saveSet(&emptyPassInfoList, dataFilename) == 0)
			{
				if(logLevel > 0)
				{
					timeStamp(&outLog);
					outLog << "Cleared contents of data file" << endl;
				}
			}
			else
			{
				timeStamp(&outLog);
				outLog << "Failed to clear contents of data file" << endl;
			}
		}
	}
	else
	{
		timeStamp(&outLog);
		outLog << "Failed to load entries from file" << endl;
	}

	if(passInfoList.size() > 0)
	{
		if(logLevel > 0)
		{
			timeStamp(&outLog);
			outLog << "Password list has " << passInfoList.size() << " entries" << endl;
		}
	}

	if(Connect(&mainLdapConnection, ldapAuthUsername, ldapAuthPassword) < 0)
	{
		// log connection failure.
		timeStamp(&outLog);
		outLog << "Can not connect to ldap server in SyncPasswords" << endl;

		goto exit;
	}

	currentPassInfo = passInfoList.begin();
	while(currentPassInfo != passInfoList.end())
	{
		if(logLevel > 0)
		{
			timeStamp(&outLog);
			outLog << "Attempting to sync password for " << currentPassInfo->username << endl;
		}

		if(QueryUsername(currentPassInfo->username) == 0)
		{
			while((dn = GetDN()) != NULL)
			{
				if(FutureOccurrence(currentPassInfo))
				{
					if(logLevel > 0)
					{
						timeStamp(&outLog);
						outLog << "Newer password changes for " << currentPassInfo->username << " exist" << endl;
					}
				}
				else if(MultipleResults() && !SYNCSERV_ALLOW_MULTI_MOD)
				{
					timeStamp(&outLog);
					outLog << "Multiple results not allowed: " << currentPassInfo->username << endl;
				}
				else if(CanBind(dn, currentPassInfo->password))
				{
					if(logLevel > 0)
					{
						timeStamp(&outLog);
						outLog << "Password match, no modify performed: " << currentPassInfo->username << endl;
					}
				}
				else if(ModifyPassword(dn, currentPassInfo->password) != 0)
				{
					// log modify failure.
					timeStamp(&outLog);
					outLog << "Modify password failed for remote entry: " << dn << endl;
				}
				else
				{
					if(logLevel > 0)
					{
						timeStamp(&outLog);
						outLog << "Password modified for remote entry: " << dn << endl;
					}
				}
				tempPassInfo = currentPassInfo;
				currentPassInfo++;
				if(logLevel > 0)
				{
					timeStamp(&outLog);
					outLog << "Removing password change from list" << endl;
				}
				passInfoList.erase(tempPassInfo);
			}
		}
		else
		{
			if(logLevel > 0)
			{
				timeStamp(&outLog);
				outLog << "Deferring password change for " << currentPassInfo->username << endl;
			}
			currentPassInfo++;
		}
	}

exit:
	if(mainLdapConnection != NULL)
	{
		Disconnect(&mainLdapConnection);
	}

	return result;
}

// ****************************************************************
// PassSyncService::Connect
// ****************************************************************
int PassSyncService::Connect(LDAP** connection, char* dn, char* auth)
{
	int result = 0;

	*connection = ldapssl_init(ldapHostName, atoi(ldapHostPort), 1);

	if(*connection == NULL)
	{
		timeStamp(&outLog);
		outLog << "ldapssl_init failed in Connect" << endl;

		result = -1;
		goto exit;
	}

	lastLdapError = ldap_simple_bind_s(*connection, dn, auth);

	if(lastLdapError != LDAP_SUCCESS)
	{
		// Log error if we're binding as ldapAuthUsername
		if(strcmp(dn, ldapAuthUsername) == 0)
		{
			timeStamp(&outLog);
			outLog << "Ldap bind error in Connect" << endl;
			outLog << "\t" << lastLdapError << ": " << ldap_err2string(lastLdapError) << endl;
		}

		result = -1;
		goto exit;
	}

exit:
	return result;
}

// ****************************************************************
// PassSyncService::Disconnect
// ****************************************************************
int PassSyncService::Disconnect(LDAP** connection)
{
	ldap_unbind(*connection);

	*connection = NULL;

	return 0;
}

// ****************************************************************
// PassSyncService::QueryUsername
// ****************************************************************
int PassSyncService::QueryUsername(char* username)
{
	int result = 0;
	char searchFilter[SYNCSERV_BUF_SIZE];

	results = NULL;

	_snprintf(searchFilter, SYNCSERV_BUF_SIZE, "(%s=%s)", ldapUsernameField, username);

	if(logLevel > 0)
	{
		timeStamp(&outLog);
		outLog << "Searching for (" << ldapUsernameField << "=" << username << ")" << endl;
	}

	lastLdapError = ldap_search_ext_s(mainLdapConnection, ldapSearchBase, LDAP_SCOPE_SUBTREE, searchFilter, NULL, 0, NULL, NULL, NULL, -1, &results);

	if(lastLdapError != LDAP_SUCCESS)
	{
		// log reason for search failure.
		timeStamp(&outLog);
		outLog << "Ldap error in QueryUsername" << endl;
		outLog << "\t" << lastLdapError << ": " << ldap_err2string(lastLdapError) << endl;
		result = -1;
		goto exit;
	}

	if(ldap_first_entry(mainLdapConnection, results) == NULL)
	{
		if(logLevel > 0)
		{
			timeStamp(&outLog);
			outLog << "There are no entries that match: " << username << endl;
		}
		result = -1;
		goto exit;
	}

exit:
	return result;
}

// ****************************************************************
// PassSyncService::GetDN
// ****************************************************************
char* PassSyncService::GetDN()
{
	char* result = NULL;

	if(currentResult == NULL)
	{
		currentResult = ldap_first_entry(mainLdapConnection, results);
	}
	else
	{
		currentResult = ldap_next_entry(mainLdapConnection, currentResult);
	}

	result = ldap_get_dn(mainLdapConnection, currentResult);

	return result;
}

// ****************************************************************
// PassSyncService::ModifyPassword
// ****************************************************************
int PassSyncService::ModifyPassword(char* dn, char* password)
{
	int result = 0;
	LDAPMod passMod;
	LDAPMod* mods[2] = {&passMod, NULL};
	char* modValues[2] = {password, NULL};

	passMod.mod_type = ldapPasswordField;
	passMod.mod_op = LDAP_MOD_REPLACE;
	passMod.mod_values = modValues;

	lastLdapError = ldap_modify_ext_s(mainLdapConnection, dn, mods, NULL, NULL);
	if(lastLdapError != LDAP_SUCCESS)
	{
		// log reason for modify failure.
		timeStamp(&outLog);
		outLog << "Ldap error in ModifyPassword" << endl;
		outLog << "\t" << lastLdapError << ": " << ldap_err2string(lastLdapError) << endl;
		result = -1;
	}

	return result;
}

// ****************************************************************
// PassSyncService::FutureOccurrence
// ****************************************************************
bool PassSyncService::FutureOccurrence(PASS_INFO_LIST_ITERATOR startingPassInfo)
{
	bool result = false;
	PASS_INFO_LIST_ITERATOR currentPassInfo;

	if((startingPassInfo != NULL) && (startingPassInfo != passInfoList.end()))
	{
		currentPassInfo = startingPassInfo;
		currentPassInfo++;

		while((currentPassInfo != passInfoList.end()) && (!result))
		{
			if(strcmp(currentPassInfo->username, startingPassInfo->username) == 0)
			{
				result = true;
			}

			currentPassInfo++;
		}
	}

	return result;
}

// ****************************************************************
// PassSyncService::MultipleResults
// ****************************************************************
bool PassSyncService::MultipleResults()
{
	bool result = false;

	if(ldap_next_entry(mainLdapConnection, ldap_first_entry(mainLdapConnection, results)) != NULL)
	{
		result = true;
	}

	return result;
}

// ****************************************************************
// PassSyncService::CanBind
// ****************************************************************
bool PassSyncService::CanBind(char* dn, char* password)
{
	bool result;
	LDAP* tempConnection = NULL;

	if(Connect(&tempConnection, dn, password) == 0)
	{
		result = true;
	}
	else
	{
		result = false;
	}
	
	if(tempConnection != NULL)
	{
		Disconnect(&tempConnection);
	}

	return result;
}

// ****************************************************************
// PassSyncService::BackoffTime
// ****************************************************************
unsigned long PassSyncService::BackoffTime(int backoff)
{
	unsigned long backoffTime = 0;

	if(backoff > 0)
	{
		backoffTime = (1 << backoff) * SYNCSERV_BASE_BACKOFF_LEN;
	}

	return backoffTime;
}

// ****************************************************************
// PassSyncService::UpdateBackoff
// ****************************************************************
void PassSyncService::UpdateBackoff()
{
	PASS_INFO_LIST_ITERATOR currentPassInfo;
	PASS_INFO_LIST_ITERATOR tempPassInfo;
	time_t currentTime;

	time(&currentTime);

	currentPassInfo = passInfoList.begin();
	while(currentPassInfo != passInfoList.end())
	{
		if(((unsigned long)currentPassInfo->atTime + (BackoffTime(currentPassInfo->backoffCount) / 1000)) <= (unsigned long)currentTime)
		{
			currentPassInfo->backoffCount++;
		}

		if(((unsigned long)currentTime - (unsigned long)currentPassInfo->atTime) > (maxBackoffTime / 1000))
		{
			timeStamp(&outLog);
			outLog << "Abandoning password change for " << currentPassInfo->username << ", backoff expired" << endl;

			tempPassInfo = currentPassInfo;
			currentPassInfo++;
			passInfoList.erase(tempPassInfo);
		}
		else
		{
			currentPassInfo++;
		}
	}
}

// ****************************************************************
// PassSyncService::GetMinBackoff
// ****************************************************************
int PassSyncService::GetMinBackoff()
{
	PASS_INFO_LIST_ITERATOR currentPassInfo;

	unsigned long minBackoff = INFINITE;

	for(currentPassInfo = passInfoList.begin(); currentPassInfo != passInfoList.end(); currentPassInfo++)
	{
		if((unsigned long)currentPassInfo->backoffCount < minBackoff)
		{
			minBackoff = currentPassInfo->backoffCount;
		}
	}

	return minBackoff;
}
