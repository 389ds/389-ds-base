// Created: 2-8-2005
// Author(s): Scott Bridges
#include "syncserv.h"

#include "prerror.h"
static char* certdbh;

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

PassSyncService::PassSyncService(const TCHAR *serviceName) : CNTService(serviceName)
{
	char sysPath[SYNCSERV_BUF_SIZE];
	HKEY regKey;
	DWORD type;
	unsigned long size;

	passhookEventHandle = CreateEvent(NULL, FALSE, FALSE, PASSHAND_EVENT_NAME);

	pLdapConnection = NULL;
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

PassSyncService::~PassSyncService()
{
	if(outLog.is_open())
	{
		timeStamp(&outLog);
		outLog << "end log" << endl;
	}
	outLog.close();
}

int PassSyncService::SyncPasswords()
{
	int result = 0;
	char username[PASSHAND_BUF_SIZE];
	char password[PASSHAND_BUF_SIZE];
	char* dn;

	if(Connect() < 0)
	{
		// ToDo: generate event connection failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "can not connect to ldap server in SyncPasswords" << endl;
		}
		result = -1;
		goto exit;
	}

	ourPasswordHandler.LoadSet(dataFilename);

	while(ourPasswordHandler.PeekUserPass(username, password) == 0)
	{
		if(QueryUsername(username) != 0)
		{
			// ToDo: generate event search failure.			
			if(outLog.is_open())
			{
				timeStamp(&outLog);
				outLog << "search for " << username << " failed in SyncPasswords" << endl;
			}
		}
		else
		{
			while((dn = GetDN()) != NULL)
			{
				if(ModifyPassword(dn, password) != 0)
				{
					// ToDo: generate event modify failure.					
					if(outLog.is_open())
					{
						timeStamp(&outLog);
						outLog << "modify password for " << username << " failed in SyncPasswords" << endl;
					}
				}
				else
				{
					if(outLog.is_open())
					{
						timeStamp(&outLog);
						outLog << "password for " << username << " modified" << endl;
						outLog << "\t" << dn << endl;
					}
				}
			}
		}
		// ToDo: zero out buffers

		ourPasswordHandler.PopUserPass();
	}

	ourPasswordHandler.SaveSet(dataFilename);

	Disconnect();

exit:
	return result;
}

void PassSyncService::OnStop()
{
	isRunning = false;
	SetEvent(passhookEventHandle);
}

void PassSyncService::OnShutdown()
{
	isRunning = false;
	SetEvent(passhookEventHandle);
}

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
}

int PassSyncService::Connect()
{
	int result = 0;

	if(ldapssl_client_init(certPath, &certdbh) != 0)
	{
		result = PR_GetError();

		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldapssl_client_init failed in Connect" << endl;
			outLog << "\t" << result << ": " <<  ldapssl_err2string(result) << endl;
		}

		result = GetLastError();

		result = -1;
		goto exit;
	}

	pLdapConnection = ldapssl_init(ldapHostName, atoi(ldapHostPort), 1);

	if(pLdapConnection == NULL)
	{
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldapssl_init failed in Connect" << endl;
		}

		result = -1;
		goto exit;
	}

	lastLdapError = ldap_simple_bind_s(pLdapConnection, ldapAuthUsername, ldapAuthPassword);

	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: log reason for bind failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldap error in Connect" << endl;
			outLog << "\t" << lastLdapError << ": " << ldapssl_err2string(lastLdapError) << endl;
		}

		result = -1;
		goto exit;
	}
exit:
	return result;
}

int PassSyncService::Disconnect()
{
	ldap_unbind(pLdapConnection);

	pLdapConnection = NULL;

	return 0;
}

int PassSyncService::QueryUsername(char* username)
{
	int result = 0;
	char searchFilter[SYNCSERV_BUF_SIZE];

	results = NULL;

	_snprintf(searchFilter, SYNCSERV_BUF_SIZE, "(%s=%s)", ldapUsernameField, username);

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

	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: log reason for search failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldap error in QueryUsername" << endl;
			outLog << "\t" << lastLdapError << ": " << ldapssl_err2string(lastLdapError) << endl;
		}
		result = -1;
		goto exit;
	}

exit:
	return result;
}

char* PassSyncService::GetDN()
{
	char* result = NULL;

	if(multipleModify)
	{
		if(currentResult == NULL)
		{
			currentResult = ldap_first_entry(pLdapConnection, results);
		}
		else
		{
			currentResult = ldap_next_entry(pLdapConnection, currentResult);
		}

		result = ldap_get_dn(pLdapConnection, currentResult);
	}
	else
	{
		if(currentResult == NULL)
		{
			currentResult = ldap_first_entry(pLdapConnection, results);
			if(ldap_next_entry(pLdapConnection, currentResult) != NULLMSG)
			{
				// Too many results
				if(outLog.is_open())
				{
					timeStamp(&outLog);
					outLog << "too many results in GetDN" << endl;
				}
				currentResult = NULL;
				goto exit;
			}

			result = ldap_get_dn(pLdapConnection, currentResult);
		}
		else
		{
			currentResult = NULL;
			goto exit;
		}
	}

exit:
	return result;
}

int PassSyncService::ModifyPassword(char* dn, char* password)
{
	int result = 0;
	LDAPMod passMod;
	LDAPMod* mods[2] = {&passMod, NULL};
	char* modValues[2] = {password, NULL};

	passMod.mod_type = ldapPasswordField;
	passMod.mod_op = LDAP_MOD_REPLACE;
	passMod.mod_values = modValues;

	lastLdapError = ldap_modify_ext_s(pLdapConnection, dn, mods, NULL, NULL);
	if(lastLdapError != LDAP_SUCCESS)
	{
		// ToDo: log the reason for the modify failure.
		if(outLog.is_open())
		{
			timeStamp(&outLog);
			outLog << "ldap error in ModifyPassword" << endl;
			outLog << "\t" << lastLdapError << ": " << ldapssl_err2string(lastLdapError) << endl;
		}
		result = -1;
	}

	return result;
}