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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "nspr.h"
#include "repl.h"

char *repl_plugin_name = REPL_PLUGIN_NAME;
char *windows_repl_plugin_name = REPL_PLUGIN_NAME;
char *repl_plugin_name_cl = REPL_PLUGIN_NAME " - changelog program";

/* String constants (no need to change these for I18N) */

#define	CHANGETYPE_ADD		"add"
#define	CHANGETYPE_DELETE	"delete"
#define	CHANGETYPE_MODIFY	"modify"
#define	CHANGETYPE_MODRDN	"modrdn"
#define	CHANGETYPE_MODDN	"moddn"
#define	ATTR_CHANGENUMBER	"changenumber"
#define	ATTR_TARGETDN		"targetdn"
#define	ATTR_CHANGETYPE		"changetype"
#define	ATTR_NEWRDN		"newrdn"
#define	ATTR_DELETEOLDRDN	"deleteoldrdn"
#define	ATTR_CHANGES		"changes"
#define	ATTR_NEWSUPERIOR	"newsuperior"
#define	ATTR_CHANGETIME		"changetime"
#define	ATTR_DATAVERSION	"dataVersion"
#define ATTR_CSN			"csn"
#define	TYPE_COPYINGFROM	"copyingFrom"
#define	TYPE_COPIEDFROM		"copiedFrom"
#define	FILTER_COPYINGFROM	"copyingFrom=*"
#define	FILTER_COPIEDFROM	"copiedFrom=*"
#define	FILTER_OBJECTCLASS	"objectclass=*"


char	*changetype_add		= CHANGETYPE_ADD;
char	*changetype_delete	= CHANGETYPE_DELETE;
char	*changetype_modify	= CHANGETYPE_MODIFY;
char	*changetype_modrdn	= CHANGETYPE_MODRDN;
char	*changetype_moddn	= CHANGETYPE_MODDN;
char	*attr_changenumber	= ATTR_CHANGENUMBER;
char	*attr_targetdn		= ATTR_TARGETDN;
char	*attr_changetype	= ATTR_CHANGETYPE;
char	*attr_newrdn		= ATTR_NEWRDN;
char	*attr_deleteoldrdn	= ATTR_DELETEOLDRDN;
char	*attr_changes		= ATTR_CHANGES;
char	*attr_newsuperior	= ATTR_NEWSUPERIOR;
char	*attr_changetime	= ATTR_CHANGETIME;
char	*attr_dataversion	= ATTR_DATAVERSION;
char	*attr_csn			= ATTR_CSN;
char	*type_copyingFrom	= TYPE_COPYINGFROM;
char	*type_copiedFrom	= TYPE_COPIEDFROM;
char	*filter_copyingFrom	= FILTER_COPYINGFROM;
char	*filter_copiedFrom	= FILTER_COPIEDFROM;
char	*filter_objectclass	= FILTER_OBJECTCLASS;
char	*type_cn		= "cn";
char	*type_objectclass	= "objectclass";

/* Names for replica attributes */
const char *attr_replicaId = "nsDS5ReplicaId";
const char *attr_replicaRoot = "nsDS5ReplicaRoot";
const char *attr_replicaType = "nsDS5ReplicaType";
const char *attr_replicaBindDn = "nsDS5ReplicaBindDn";
const char *attr_state = "nsState";
const char *attr_flags = "nsds5Flags";
const char *attr_replicaName = "nsds5ReplicaName";
const char *attr_replicaReferral = "nsds5ReplicaReferral";
const char *type_ruvElement = "nsds50ruv";
const char *type_replicaPurgeDelay = "nsds5ReplicaPurgeDelay";
const char *type_replicaChangeCount = "nsds5ReplicaChangeCount";
const char *type_replicaTombstonePurgeInterval = "nsds5ReplicaTombstonePurgeInterval";
const char *type_replicaLegacyConsumer = "nsds5ReplicaLegacyConsumer";
const char *type_ruvElementUpdatetime = "nsruvReplicaLastModified";

/* Attribute names for replication agreement attributes */
const char *type_nsds5ReplicaHost = "nsds5ReplicaHost";
const char *type_nsds5ReplicaPort = "nsds5ReplicaPort";
const char *type_nsds5TransportInfo = "nsds5ReplicaTransportInfo";
const char *type_nsds5ReplicaBindDN = "nsds5ReplicaBindDN";
const char *type_nsds5ReplicaCredentials = "nsds5ReplicaCredentials";
const char *type_nsds5ReplicaBindMethod = "nsds5ReplicaBindMethod";
const char *type_nsds5ReplicaRoot = "nsds5ReplicaRoot";
const char *type_nsds5ReplicatedAttributeList = "nsds5ReplicatedAttributeList";
const char *type_nsds5ReplicaUpdateSchedule = "nsds5ReplicaUpdateSchedule";
const char *type_nsds5ReplicaInitialize = "nsds5BeginReplicaRefresh";
const char *type_nsds5ReplicaTimeout = "nsds5ReplicaTimeout";
const char *type_nsds5ReplicaBusyWaitTime = "nsds5ReplicaBusyWaitTime";
const char *type_nsds5ReplicaSessionPauseTime = "nsds5ReplicaSessionPauseTime";

/* windows sync specifica attributes */
const char *type_nsds7WindowsReplicaArea = "nsds7WindowsReplicaSubtree";
const char *type_nsds7DirectoryReplicaArea = "nsds7DirectoryReplicaSubtree";
const char *type_nsds7CreateNewUsers = "nsds7NewWinUserSyncEnabled";
const char *type_nsds7CreateNewGroups = "nsds7NewWinGroupSyncEnabled";
const char *type_nsds7WindowsDomain = "nsds7WindowsDomain";
const char *type_nsds7DirsyncCookie = "nsds7DirsyncCookie";

/* To Allow Consumer Initialisation when adding an agreement - */
const char *type_nsds5BeginReplicaRefresh = "nsds5BeginReplicaRefresh";

static int repl_active_threads;

int
decrement_repl_active_threads()
{
    PR_AtomicIncrement(&repl_active_threads);
	return repl_active_threads;
}

int
increment_repl_active_threads()
{
	PR_AtomicDecrement(&repl_active_threads);
    return repl_active_threads;
}
