// Created: 2-8-2005
// Author(s): Scott Bridges
#ifndef _SYNCSERV_H_
#define _SYNCSERV_H_

#include <stdio.h>
#include "ldap.h"
#include "ldap_ssl.h"
#include "ldappr.h"
#include "ntservice.h"
#include "../passhand.h"

#define SYNCSERV_BUF_SIZE 256
#define SYNCSERV_TIMEOUT 10000
#define SYNCSERV_ALLOW_MULTI_MOD false

class PassSyncService : public CNTService
{
public:
	PassSyncService(const TCHAR* serviceName);
	~PassSyncService();

	void OnStop();
	void OnShutdown();
	void Run();

	int SyncPasswords();

private:
	int Connect();
	int Disconnect();
	int QueryUsername(char* username);
	char* GetDN();
	int ModifyPassword(char* dn, char* password);

	PasswordHandler ourPasswordHandler;
	HANDLE passhookEventHandle;

	// LDAP variables
	LDAP* pLdapConnection;
	LDAPMessage* results;
	LDAPMessage* currentResult;
	int lastLdapError;
	char certPath[SYNCSERV_BUF_SIZE];
	char logPath[SYNCSERV_BUF_SIZE];

	// Config variables
	char installPath[SYNCSERV_BUF_SIZE];
	char dataFilename[SYNCSERV_BUF_SIZE];
	char ldapHostName[SYNCSERV_BUF_SIZE];
	char ldapHostPort[SYNCSERV_BUF_SIZE];
	char ldapAuthUsername[SYNCSERV_BUF_SIZE];
	char ldapAuthPassword[SYNCSERV_BUF_SIZE];
	char ldapSearchBase[SYNCSERV_BUF_SIZE];
	char ldapUsernameField[SYNCSERV_BUF_SIZE];
	char ldapPasswordField[SYNCSERV_BUF_SIZE];
	bool multipleModify;
	bool isRunning;
	fstream outLog;
};

#endif