/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _UX_DIALOG_H_
#define _UX_DIALOG_H_

#include "dialog.h"
extern DialogYesNo askReconfig;
extern DialogInput askSlapdServerName;
extern DialogInput askAdminPort;
extern DialogInput askSlapdPort;
extern DialogYesNo askSecurity;
extern DialogInput askSlapdSecPort;
extern DialogInput askSlapdServerID;
extern DialogInput askSlapdSysUser;
extern DialogYesNo askConfigForMC;
extern DialogInput askMCAdminID;
extern DialogInput askSlapdSuffix;
extern DialogInput askSlapdRootDN;
extern DialogYesNo askReplication;
extern DialogYesNo askSample;
extern DialogYesNo askPopulate;
extern DialogInput askOrgSize;
extern DialogYesNo askCIR;
extern DialogInput askCIRHost;
extern DialogInput askCIRPort;
extern DialogInput askCIRDN;
extern DialogInput askCIRSuffix;
extern DialogYesNo askCIRSSL;
extern DialogInput askCIRInterval;
extern DialogInput askCIRDays;
extern DialogInput askCIRTimes;
extern DialogYesNo askSIR;
extern DialogInput askChangeLogSuffix;
extern DialogInput askChangeLogDir;
extern DialogInput askReplicationDN;
extern DialogInput askConsumerDN;
extern DialogYesNo askSIR;
extern DialogInput askSIRHost;
extern DialogInput askSIRPort;
extern DialogInput askSIRDN;
extern DialogInput askSIRSuffix;
extern DialogYesNo askSIRSSL;
extern DialogInput askSIRDays;
extern DialogInput askSIRTimes;
extern DialogYesNo askUseExistingMC;
extern DialogInput askMCHost;
extern DialogInput askMCPort;
extern DialogInput askMCDN;
extern DialogYesNo askDisableSchemaChecking;
extern DialogInput askMCAdminDomain;
extern DialogInput askAdminDomain;
extern DialogYesNo askUseExistingUG;
extern DialogInput askUGHost;
extern DialogInput askUGPort;
extern DialogInput askUGDN;
extern DialogInput askUGSuffix;
extern DialogInput askReconfigMCAdminPwd;

// these keywords and values are used in the Dialog::setUserData to
// control the behavior of the dialogs
#define SETUP_DEFAULTS "SETUP_DEFAULTS"
const int SETUP_ONLY = 1;
#define ACTION "ACTION"

#endif // _UX_DIALOG_H_
