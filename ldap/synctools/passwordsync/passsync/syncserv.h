/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

// Created: 2-8-2005
// Author(s): Scott Bridges
#ifndef _SYNCSERV_H_
#define _SYNCSERV_H_

#include <stdio.h>
#include <math.h>
#include "ldap.h"
#include "ldap_ssl.h"
#include "ldappr.h"
#include "ntservice.h"
#include "../passhand.h"

#define SYNCSERV_BUF_SIZE 256
#define SYNCSERV_TIMEOUT 10000
#define SYNCSERV_ALLOW_MULTI_MOD false
#define SYNCSERV_BASE_BACKOFF_LEN 1000

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
	int Connect(LDAP** connection, char* dn, char* auth);
	int Disconnect(LDAP** connection);
	int QueryUsername(char* username);
	char* GetDN();
	int ModifyPassword(char* dn, char* password);

	bool FutureOccurrence(PASS_INFO_LIST_ITERATOR startingPassInfo);
	bool MultipleResults();
	bool CanBind(char* dn, char* password);

	unsigned long BackoffTime(int backoff);
	void UpdateBackoff();
	int GetMinBackoff();

	PASS_INFO_LIST passInfoList;
	HANDLE passhookEventHandle;

	// LDAP variables
	LDAP* mainLdapConnection;
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
	unsigned long maxBackoffTime;
	int logLevel;
	bool isRunning;
	fstream outLog;
};

#endif