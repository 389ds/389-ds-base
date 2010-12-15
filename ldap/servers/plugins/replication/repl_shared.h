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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* repl_shared.h - definitions shared between 4.0 and 5.0 replication
				   modules
 */	

#ifndef REPL_SHARED_H
#define REPL_SHARED_H

#include "slapi-private.h"
#include "slapi-plugin.h"
#include "ldif.h" /* GGOODREPL - is this cheating? */

#ifdef _WIN32
#define FILE_PATHSEP '\\'
#else
#define FILE_PATHSEP '/'
#endif

#define	CHANGELOGDB_TRIM_INTERVAL	300 /* 5 minutes */

#define CONFIG_CHANGELOG_DIR_ATTRIBUTE		"nsslapd-changelogdir"
#define CONFIG_CHANGELOG_MAXENTRIES_ATTRIBUTE	"nsslapd-changelogmaxentries"
#define CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE	"nsslapd-changelogmaxage"
/* Changelog Internal Configuration Parameters -> Changelog Cache related */
#define CONFIG_CHANGELOG_MAX_CONCURRENT_WRITES	"nsslapd-changelogmaxconcurrentwrites"
#define CONFIG_CHANGELOG_ENCRYPTION_ALGORITHM	"nsslapd-encryptionalgorithm"
#define CONFIG_CHANGELOG_SYMMETRIC_KEY	"nsSymmetricKey"

#define	T_CHANGETYPESTR		"changetype"
#define	T_CHANGETYPE		1
#define	T_TIMESTR		"time"
#define	T_TIME			2
#define	T_DNSTR			"dn"
#define	T_DN			3
#define	T_CHANGESTR		"change"
#define	T_CHANGE		4

#define	T_ADDCTSTR		"add"
#define	T_ADDCT			4
#define	T_MODIFYCTSTR		"modify"
#define	T_MODIFYCT		5
#define	T_DELETECTSTR		"delete"
#define	T_DELETECT		6
#define	T_MODRDNCTSTR		"modrdn"
#define	T_MODRDNCT		7
#define	T_MODDNCTSTR		"moddn"
#define	T_MODDNCT		8

#define	T_MODOPADDSTR		"add"
#define	T_MODOPADD		9
#define	T_MODOPREPLACESTR	"replace"
#define	T_MODOPREPLACE		10
#define	T_MODOPDELETESTR	"delete"
#define	T_MODOPDELETE		11
#define	T_MODSEPSTR		"-"
#define	T_MODSEP		12

#define	T_NEWRDNSTR		"newrdn"
#define T_NEWSUPERIORSTR	ATTR_NEWSUPERIOR
#define	T_DRDNFLAGSTR		"deleteoldrdn"

#define	T_ERR			-1
#define	AWAITING_OP		-1

#define STATE_REFERRAL "referral"
#define STATE_UPDATE_REFERRAL "referral on update"
#define STATE_BACKEND "backend"

#define REPL_PLUGIN_NAME "NSMMReplicationPlugin"
/*
 * Changed version from 1.0 to 2.0 when we switched from libdb32 to libdb33
 * richm 20020708
 * also changed name from REPL_PLUGIN_VERSION to CHANGELOG_DB_VERSION since we use
 * a different version for the plugin itself and this particular version is only
 * used for the changelog database
*/
/* the current CHANGELOG_DB_VERSION: DB_VERSION_MAJOR"."DB_VERSION_MINOR" */
/* this string is left for the backward compatibility */
#define CHANGELOG_DB_VERSION "4.0"
extern char *repl_plugin_name;
extern char *windows_repl_plugin_name;
extern char *repl_plugin_name_cl;

/* repl_monitor.c */
int repl_monitor_init();

/* In replutil.c */
char ** get_cleattrs();
unsigned long strntoul( char *from, size_t len, int base );
void freepmods( LDAPMod **pmods );
char *copy_berval (struct berval* from);
void entry_print(Slapi_Entry *e);
int copyfile(char* source, char *destination, int overwrite, int mode);
time_t age_str2time (const char *age);
const char* changeType2Str (int type);
int str2ChangeType (const char *str);
lenstr *make_changes_string(LDAPMod **ldm, char **includeattrs);
Slapi_Mods* parse_changes_string(char *str);
PRBool IsValidOperation (const slapi_operation_parameters *op);
const char *map_repl_root_to_dbid(Slapi_DN *repl_root);
PRBool is_ruv_tombstone_entry (Slapi_Entry *e);

/* replication plugins */
enum {
		PLUGIN_LEGACY_REPLICATION,
		PLUGIN_MULTIMASTER_REPLICATION,
		PLUGIN_MAX
};						

void* repl_get_plugin_identity (int pluginID);
void  repl_set_plugin_identity (int pluginID, void *identity);

#endif
