// Created: 2-8-2005
// Author(s): Scott Bridges
#ifndef _SYNCSERV_H_
#define _SYNCSERV_H_

#include <stdio.h>
#include <ldap.h>
#include <ldap_ssl.h>
#include "ntservice.h"
#include "../passhand.h"

#define REG_BUF_SIZE 64

class PassSyncService : public CNTService
{
public:
	PassSyncService(const TCHAR* serviceName);
	~PassSyncService();

	void Run();

	// ToDo: Move to private.
	int Connect();
	int Disconnect();
	int QueryUsername(char* username);
	int GetDN(char** dn);
	int ModifyPassword(char* dn, char* password);

	int SyncPasswords();

private:

	PasswordHandler ourPasswordHandler;
	HANDLE passhandEventHandle;

	// LDAP variables
	LDAP* pLdapConnection;
	LDAPMessage* results;
	LDAPMessage* currentResult;
	int lastLdapError;

	// Config variables
	char* dataFilename;
	char* logFilename;
	char* ldapHostName;
	char* ldpaHostPort;
	char* ldalAuthUsername;
	char* ldapAuthPassword;
	char* ldapSearchBase;
	char* ldapUsernameField;
	char* ldapPasswordField;
	bool multipleModify;
};

#endif