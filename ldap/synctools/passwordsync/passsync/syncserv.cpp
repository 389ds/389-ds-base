/* --- BEGIN COPYRIGHT BLOCK ---
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
	HKEY regKey;
	DWORD type;
	unsigned long size;

	passhookEventHandle = CreateEvent(NULL, FALSE, FALSE, PASSHAND_EVENT_NAME);

	mainLdapConnection = NULL;
	results = NULL;
	currentResult = NULL;
	lastLdapError = LDAP_SUCCESS;
	certdbh = NULL;

	multipleModify = SYNCSERV_ALLOW_MULTI_MOD;
	isRunning = false;

	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\PasswordSync", &regKey);
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
		outLog << "begin log" << endl;
	}

	PK11_SetPasswordFunc(passwdcb);
}

// ****************************************************************
// PassSyncService::~PassSyncService
// ****************************************************************
PassSyncService::~PassSyncService()
{
	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "end log" << endl;
	}
	outLog.close();
}

// ****************************************************************
// PassSyncService::SyncPasswords
// ****************************************************************
int PassSyncService::SyncPasswords()
{
	int result = 0;
	PASS_INFO_LIST_ITERATOR currentPassInfo;
	PASS_INFO_LIST_ITERATOR tempPassInfo;
	char* dn;

	if(Connect(&mainLdapConnection, ldapAuthUsername, ldapAuthPassword) < 0)
	{
		// log connection failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "can not connect to ldap server in SyncPasswords" << endl;
		}

		goto exit;
	}

	if(loadSet(&passInfoList, dataFilename) == 0)
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << passInfoList.size() << " entries loaded from file" << endl;
		}
	}
	else
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "failed to load entries from file" << endl;
		}
	}

	while(passInfoList.size() > 0)
	{
		currentPassInfo = passInfoList.begin();

		while(currentPassInfo != passInfoList.end())
		{
			if(QueryUsername(currentPassInfo->username) != 0)
			{
				// log search failure.			
				if(outLog.is_open())
				{
					timeStamp(&outLog);
					outLog << "search for " << currentPassInfo->username << " failed in SyncPasswords" << endl;
				}
			}
			else
			{
				while((dn = GetDN()) != NULL)
				{
					if(CanBind(dn, currentPassInfo->password))
					{
						if(outLog.is_open())
						{
							timeStamp(&outLog);
							outLog << "password match, no modify preformed: " << currentPassInfo->username << endl;
						}
					}
					else if(ModifyPassword(dn, currentPassInfo->password) != 0)
					{
						// log modify failure.
						if(outLog.is_open())
						{
							timeStamp(&outLog);
							outLog << "modify password for " << currentPassInfo->username << " failed in SyncPasswords" << endl;
						}
					}
					else
					{
						if(outLog.is_open())
						{
							timeStamp(&outLog);
							outLog << "password for " << currentPassInfo->username << " modified" << endl;
							outLog << "\t" << dn << endl;
						}
					}
				} // end while((dn = GetDN()) != NULL)
			}

			tempPassInfo = currentPassInfo;
			currentPassInfo++;
			passInfoList.erase(tempPassInfo);
		} // end while(currentPassInfo != passInfoList.end())
	} // end while(passInfoList.size() > 0)

	if(saveSet(&passInfoList, dataFilename) == 0)
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << passInfoList.size() << " entries saved to file" << endl;
		}
	}
	else
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "failed to save entries to file" << endl;
		}
	}

	clearSet(&passInfoList);
	Disconnect(&mainLdapConnection);

exit:
	return result;
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
	SyncPasswords();

	while(isRunning)
	{
		WaitForSingleObject(passhookEventHandle, INFINITE);
		SyncPasswords();
		ResetEvent(passhookEventHandle);
	}

	CloseHandle(passhookEventHandle);
}

// ****************************************************************
// PassSyncService::Connect
// ****************************************************************
int PassSyncService::Connect(LDAP** connection, char* dn, char* auth)
{
	int result = 0;

	if(ldapssl_client_init(certPath, &certdbh) != 0)
	{
		result = PR_GetError();

		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldapssl_client_init failed in Connect" << endl;
			outLog << "\t" << result << ": " << ldap_err2string(result) << endl;
		}

		result = GetLastError();

		result = -1;
		goto exit;
	}

	*connection = ldapssl_init(ldapHostName, atoi(ldapHostPort), 1);

	if(*connection == NULL)
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldapssl_init failed in Connect" << endl;
		}

		result = -1;
		goto exit;
	}

	ResetBackoff();
	while(((lastLdapError = ldap_simple_bind_s(*connection, dn, auth)) != LDAP_SUCCESS) && Backoff())
	{
		// empty
	}

	if(lastLdapError != LDAP_SUCCESS)
	{
		// log reason for bind failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldap error in Connect" << endl;
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

	connection = NULL;

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

	ResetBackoff();
	while(Backoff())
	{
		lastLdapError = ldap_search_ext_s(mainLdapConnection, ldapSearchBase, LDAP_SCOPE_ONELEVEL, searchFilter, NULL, 0, NULL, NULL, NULL, -1, &results);

		if(lastLdapError != LDAP_SUCCESS)
		{
			// log reason for search failure.
			if(outLog.is_open())
			{
				timeStamp(&outLog);
				outLog << "ldap error in QueryUsername" << endl;
				outLog << "\t" << lastLdapError << ": " << ldap_err2string(lastLdapError) << endl;
			}
			result = -1;
			EndBackoff();
		}
		else if(ldap_first_entry(mainLdapConnection, results) == NULL)
		{
			if(outLog.is_open())
			{
				timeStamp(&outLog);
				outLog << "there are no entries that match: " << username << endl;
			}
			result = -1;
		}
		else if(ldap_next_entry(mainLdapConnection, ldap_first_entry(mainLdapConnection, results)) != NULL)
		{
			if(outLog.is_open())
			{
				timeStamp(&outLog);
				outLog << "there are multiple entries that match: " << username << endl;
			}

			if(!SYNCSERV_ALLOW_MULTI_MOD)
			{
				result = -1;
				EndBackoff();
			}
		}
	}

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
// PassSyncService::CanBind
// ****************************************************************
bool PassSyncService::CanBind(char* dn, char* password)
{
	bool result;
	LDAP* tempConnection = NULL;

	if(Connect(&tempConnection, dn, password) == 0)
	{
		Disconnect(&tempConnection);
		result = true;
	}
	else
	{
		result = false;
	}
	
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
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldap error in ModifyPassword" << endl;
			outLog << "\t" << lastLdapError << ": " << ldap_err2string(lastLdapError) << endl;
		}
		result = -1;
	}

	return result;
}

// ****************************************************************
// PassSyncService::ResetBackoff
// ****************************************************************
void PassSyncService::ResetBackoff()
{
	backoffCount = 0;
}

// ****************************************************************
// PassSyncService::EndBackoff
// ****************************************************************
void PassSyncService::EndBackoff()
{
	backoffCount = SYNCSERV_MAX_BACKOFF_COUNT;
}

// ****************************************************************
// PassSyncService::Backoff
// ****************************************************************
bool PassSyncService::Backoff()
{
	bool result;

	if(backoffCount == 0)
	{
		result = true;
	}
	else if(backoffCount < SYNCSERV_MAX_BACKOFF_COUNT)
	{
		Sleep((2 ^ backoffCount) * SYNCSERV_BASE_BACKOFF_LEN);
		result = true;
	}
	else
	{
		result = false;
	}

	backoffCount++;
	return result;
}
