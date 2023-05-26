/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include "nspr.h"
#include "repl_shared.h"

char *repl_plugin_name = REPL_PLUGIN_NAME;
char *windows_repl_plugin_name = REPL_PLUGIN_NAME " - windows sync";
char *repl_plugin_name_cl = REPL_PLUGIN_NAME " - changelog program";

/* String constants (no need to change these for I18N) */

#define CHANGETYPE_ADD "add"
#define CHANGETYPE_DELETE "delete"
#define CHANGETYPE_MODIFY "modify"
#define CHANGETYPE_MODRDN "modrdn"
#define CHANGETYPE_MODDN "moddn"
#define ATTR_CHANGENUMBER "changenumber"
#define ATTR_TARGETDN "targetdn"
#define ATTR_CHANGETYPE "changetype"
#define ATTR_NEWRDN "newrdn"
#define ATTR_DELETEOLDRDN "deleteoldrdn"
#define ATTR_CHANGES "changes"
#define ATTR_NEWSUPERIOR "newsuperior"
#define ATTR_CHANGETIME "changetime"
#define ATTR_DATAVERSION "dataVersion"
#define ATTR_CSN "csn"
#define TYPE_COPYINGFROM "copyingFrom"
#define TYPE_COPIEDFROM "copiedFrom"
#define FILTER_COPYINGFROM "copyingFrom=*"
#define FILTER_COPIEDFROM "copiedFrom=*"
#define FILTER_OBJECTCLASS "objectclass=*"


char *changetype_add = CHANGETYPE_ADD;
char *changetype_delete = CHANGETYPE_DELETE;
char *changetype_modify = CHANGETYPE_MODIFY;
char *changetype_modrdn = CHANGETYPE_MODRDN;
char *changetype_moddn = CHANGETYPE_MODDN;
char *repl_changenumber = ATTR_CHANGENUMBER;
char *repl_targetdn = ATTR_TARGETDN;
char *repl_changetype = ATTR_CHANGETYPE;
char *repl_newrdn = ATTR_NEWRDN;
char *repl_deleteoldrdn = ATTR_DELETEOLDRDN;
char *repl_changes = ATTR_CHANGES;
char *repl_newsuperior = ATTR_NEWSUPERIOR;
char *repl_changetime = ATTR_CHANGETIME;
char *attr_csn = ATTR_CSN;
char *type_copyingFrom = TYPE_COPYINGFROM;
char *type_copiedFrom = TYPE_COPIEDFROM;
char *filter_copyingFrom = FILTER_COPYINGFROM;
char *filter_copiedFrom = FILTER_COPIEDFROM;
char *filter_objectclass = FILTER_OBJECTCLASS;
char *type_cn = "cn";
char *type_objectclass = "objectclass";

/* Names for replica attributes */
const char *attr_replicaId = "nsDS5ReplicaId";
const char *attr_replicaRoot = "nsDS5ReplicaRoot";
const char *attr_replicaType = "nsDS5ReplicaType";
const char *attr_replicaBindDn = "nsDS5ReplicaBindDn";
const char *attr_replicaBindDnGroup = "nsDS5ReplicaBindDnGroup";
const char *attr_replicaBindDnGroupCheckInterval = "nsDS5ReplicaBindDnGroupCheckInterval";
const char *attr_state = "nsState";
const char *attr_flags = "nsds5Flags";
const char *attr_replicaName = "nsds5ReplicaName";
const char *attr_replicaReferral = "nsds5ReplicaReferral";
const char *type_ruvElement = "nsds50ruv";
const char *type_agmtMaxCSN = "nsds5AgmtMaxCSN";
const char *type_replicaPurgeDelay = "nsds5ReplicaPurgeDelay";
const char *type_replicaChangeCount = "nsds5ReplicaChangeCount";
const char *type_replicaTombstonePurgeInterval = "nsds5ReplicaTombstonePurgeInterval";
const char *type_ruvElementUpdatetime = "nsruvReplicaLastModified";
const char *type_replicaCleanRUV = "nsds5ReplicaCleanRUV";
const char *type_replicaAbortCleanRUV = "nsds5ReplicaAbortCleanRUV";
const char *type_replicaProtocolTimeout = "nsds5ReplicaProtocolTimeout";
const char *type_replicaReleaseTimeout = "nsds5ReplicaReleaseTimeout";
const char *type_replicaLingerTimeout = "nsds5ReplicaLingerTimeout";
const char *type_replicaBackoffMin = "nsds5ReplicaBackoffMin";
const char *type_replicaBackoffMax = "nsds5ReplicaBackoffMax";
const char *type_replicaPrecisePurge = "nsds5ReplicaPreciseTombstonePurging";
const char *type_replicaKeepAliveUpdateInterval = "nsds5ReplicaKeepAliveUpdateInterval";

/* Attribute names for replication agreement attributes */
const char *type_nsds5ReplicaHost = "nsds5ReplicaHost";
const char *type_nsds5ReplicaPort = "nsds5ReplicaPort";
const char *type_nsds5TransportInfo = "nsds5ReplicaTransportInfo";
const char *type_nsds5ReplicaBindDN = "nsds5ReplicaBindDN";
const char *type_nsds5ReplicaBindDNGroup = "nsds5ReplicaBindDNGroup";
const char *type_nsds5ReplicaBindDNGroupCheckInterval = "nsds5ReplicaBindDNGroupCheckInterval";
const char *type_nsds5ReplicaCredentials = "nsds5ReplicaCredentials";
const char *type_nsds5ReplicaBindMethod = "nsds5ReplicaBindMethod";
const char *type_nsds5ReplicaRoot = "nsds5ReplicaRoot";
const char *type_nsds5ReplicatedAttributeList = "nsds5ReplicatedAttributeList";
const char *type_nsds5ReplicatedAttributeListTotal = "nsds5ReplicatedAttributeListTotal";
const char *type_nsds5ReplicaUpdateSchedule = "nsds5ReplicaUpdateSchedule";
const char *type_nsds5ReplicaInitialize = "nsds5BeginReplicaRefresh";
const char *type_nsds5ReplicaTimeout = "nsds5ReplicaTimeout";
const char *type_nsds5ReplicaBusyWaitTime = "nsds5ReplicaBusyWaitTime";
const char *type_nsds5ReplicaSessionPauseTime = "nsds5ReplicaSessionPauseTime";
const char *type_nsds5ReplicaEnabled = "nsds5ReplicaEnabled";
const char *type_nsds5ReplicaStripAttrs = "nsds5ReplicaStripAttrs";
const char *type_nsds5ReplicaFlowControlWindow = "nsds5ReplicaFlowControlWindow";
const char *type_nsds5ReplicaFlowControlPause = "nsds5ReplicaFlowControlPause";
const char *type_nsds5WaitForAsyncResults = "nsds5ReplicaWaitForAsyncResults";
const char *type_replicaIgnoreMissingChange = "nsds5ReplicaIgnoreMissingChange";
const char *type_nsds5ReplicaBootstrapBindDN = "nsds5ReplicaBootstrapBindDN";
const char *type_nsds5ReplicaBootstrapCredentials = "nsds5ReplicaBootstrapCredentials";
const char *type_nsds5ReplicaBootstrapBindMethod = "nsds5ReplicaBootstrapBindMethod";
const char *type_nsds5ReplicaBootstrapTransportInfo = "nsds5ReplicaBootstrapTransportInfo";

/* windows sync specific attributes */
const char *type_nsds7WindowsReplicaArea = "nsds7WindowsReplicaSubtree";
const char *type_nsds7DirectoryReplicaArea = "nsds7DirectoryReplicaSubtree";
const char *type_nsds7CreateNewUsers = "nsds7NewWinUserSyncEnabled";
const char *type_nsds7CreateNewGroups = "nsds7NewWinGroupSyncEnabled";
const char *type_nsds7WindowsDomain = "nsds7WindowsDomain";
const char *type_nsds7DirsyncCookie = "nsds7DirsyncCookie";
const char *type_winSyncInterval = "winSyncInterval";
const char *type_oneWaySync = "oneWaySync";
const char *type_winsyncMoveAction = "winSyncMoveAction";
const char *type_winSyncWindowsFilter = "winSyncWindowsFilter";
const char *type_winSyncDirectoryFilter = "winSyncDirectoryFilter";
const char *type_winSyncSubtreePair = "winSyncSubtreePair";
const char *type_winSyncFlattenTree = "winSyncFlattenTree";

/* To Allow Consumer Initialization when adding an agreement - */
const char *type_nsds5BeginReplicaRefresh = "nsds5BeginReplicaRefresh";

static int repl_active_threads;

int
decrement_repl_active_threads(void)
{
    PR_AtomicIncrement(&repl_active_threads);
    return repl_active_threads;
}

int
increment_repl_active_threads(void)
{
    PR_AtomicDecrement(&repl_active_threads);
    return repl_active_threads;
}
