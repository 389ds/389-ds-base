/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

// Created: 2-8-2005
// Author(s): Scott Bridges
#include "syncserv.h"

PassSyncService::PassSyncService(const TCHAR *serviceName) : CNTService(serviceName)
{
	HKEY regKey;
	DWORD type;
	unsigned long size;

	passhandEventHandle = CreateEvent(NULL, FALSE, FALSE, PASSHAND_EVENT_NAME);

	pLdapConnection = NULL;
	results = NULL;
	currentResult = NULL;
	lastLdapError = LDAP_SUCCESS;

	dataFilename = "C:\\WINDOWS\\system32\\passhook.dat";
	logFilename = NULL;
	multipleModify = true;

	ldapHostName = (char*)malloc(REG_BUF_SIZE);
	ldpaHostPort = (char*)malloc(REG_BUF_SIZE);
	ldalAuthUsername = (char*)malloc(REG_BUF_SIZE);
	ldapAuthPassword = (char*)malloc(REG_BUF_SIZE);
	ldapSearchBase = (char*)malloc(REG_BUF_SIZE);
	ldapUsernameField = (char*)malloc(REG_BUF_SIZE);
	ldapPasswordField = (char*)malloc(REG_BUF_SIZE);

	RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\PasswordSync", &regKey);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "Host Name", NULL, &type, (unsigned char*)ldapHostName, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "Port Number", NULL, &type, (unsigned char*)ldpaHostPort, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "User Name", NULL, &type, (unsigned char*)ldalAuthUsername, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "Password", NULL, &type, (unsigned char*)ldapAuthPassword, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "Search Base", NULL, &type, (unsigned char*)ldapSearchBase, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "User Name Field", NULL, &type, (unsigned char*)ldapUsernameField, &size);
	size = REG_BUF_SIZE;
	RegQueryValueEx(regKey, "Password Field", NULL, &type, (unsigned char*)ldapPasswordField, &size);
	RegCloseKey(regKey);
}

PassSyncService::~PassSyncService()
{
}

int PassSyncService::SyncPasswords()
{
	UNICODE_STRING uUsername;
	UNICODE_STRING uPassword;
	char* username;
	char* password;
	char* dn;

	if(Connect() < 0)
	{
		// ToDo: Generate event connection failure.
		return -1;
	}

	ourPasswordHandler.LoadSet(dataFilename);

	while(ourPasswordHandler.PeekUserPass(&uUsername, &uPassword) > -1)
	{

		username = (char*)malloc(uUsername.Length);
		password = (char*)malloc(uPassword.Length);

		sprintf(username, "%S", uUsername.Buffer);
		sprintf(password, "%S", uPassword.Buffer);

		results = NULL;
		currentResult = NULL;
		if(QueryUsername(username) < 0)
		{
			// ToDo: Generate event search failure.
		}
		else
		{
			while(dn != NULL)
			{
				if(GetDN(&dn) < 0)
				{
					// ToDo: Generate event multiple results.
				}
				else
				{
					if(ModifyPassword(dn, password) < 0)
					{
						// ToDo: Generate event modify failure.
					}
					else
					{
						ourPasswordHandler.PopUserPass();
					}
				}
			}
		}

		// ToDo: Zero out buffers
		free(username);
		free(password);
	}

	ourPasswordHandler.SaveSet(dataFilename);

	Disconnect();

	return 0;
}

void PassSyncService::Run()
{
	while(true)
	{
		WaitForSingleObject(passhandEventHandle, INFINITE);
		SyncPasswords();
		ResetEvent(passhandEventHandle);
		//Sleep(60000);
	}
}

int PassSyncService::Connect()
{
	pLdapConnection = ldap_init(ldapHostName, atoi(ldpaHostPort));

	lastLdapError = ldap_simple_bind_s(pLdapConnection, ldalAuthUsername, ldapAuthPassword);
	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: Log reason for bind failure.
		return -1;
	}

	return 0;
}

int PassSyncService::Disconnect()
{
	ldap_unbind(pLdapConnection);

	pLdapConnection = NULL;

	return 0;
}

int PassSyncService::QueryUsername(char* username)
{
	char* searchFilter = (char*)malloc(strlen(ldapUsernameField) + strlen(username) + 4);

	sprintf(searchFilter, "(%s=%s)", ldapUsernameField, username);

	lastLdapError = ldap_search_ext_s(
		pLdapConnection,
		ldapSearchBase,
		LDAP_SCOPE_ONELEVEL,
		searchFilter,
		NULL,
		0,
		NULL,
		NULL,
		NULL,
		-1,
		&results);

	free(searchFilter);

	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: Log reason for search failure.
		return -1;
	}

	return 0;
}

int PassSyncService::GetDN(char** dn)
{
	if(multipleModify)
	{
		if(currentResult == NULL)
		{
			currentResult = ldap_first_entry(pLdapConnection, results);
		}
		else
		{
			currentResult = ldap_next_entry(pLdapConnection, results);
		}

		if(currentResult == NULL)
		{
			*dn = NULL;
			return 0;
		}

		*dn = ldap_get_dn(pLdapConnection, currentResult);
		return 0;
	}
	else
	{
		currentResult = ldap_first_entry(pLdapConnection, results);
		if(ldap_next_entry(pLdapConnection, results) != NULLMSG)
		{
			// ToDo: Log that multiple results for username were found.
			*dn = NULL;
			return -1;
		}

		*dn = ldap_get_dn(pLdapConnection, currentResult);
		return 0;
	}
}

int PassSyncService::ModifyPassword(char* dn, char* password)
{
	LDAPMod passMod;
	LDAPMod* mods[2] = {&passMod, NULL};
	char* modValues[2] = {password, NULL};

	passMod.mod_type = ldapPasswordField;
	passMod.mod_op = LDAP_MOD_REPLACE;
	passMod.mod_values = modValues;

	lastLdapError = ldap_modify_ext_s(pLdapConnection, dn, mods, NULL, NULL);
	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: Log the reason for the modify failure.
		return -1;
	}

	return 0;
}
