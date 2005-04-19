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