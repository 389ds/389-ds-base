/** BEGIN COPYRIGHT BLOCK
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
 * do so, delete this exception statement from your version. 
 * 
 * 
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
