/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
/* Changelog Internal Configuration Parameters -> DB related */
#define CONFIG_CHANGELOG_DB_DBCACHESIZE			"nsslapd-dbcachesize"
#define CONFIG_CHANGELOG_DB_DURABLE_TRANSACTIONS	"nsslapd-db-durable-transaction"
#define CONFIG_CHANGELOG_DB_CHECKPOINT_INTERVAL		"nsslapd-db-checkpoint-interval"
#define CONFIG_CHANGELOG_DB_CIRCULAR_LOGGING		"nsslapd-db-circular-logging"
#define CONFIG_CHANGELOG_DB_PAGE_SIZE			"nsslapd-db-page-size"
#define CONFIG_CHANGELOG_DB_LOGFILE_SIZE		"nsslapd-db-logfile-size"
#define CONFIG_CHANGELOG_DB_MAXTXN_SIZE			"nsslapd-db-max-txn"
#define CONFIG_CHANGELOG_DB_VERBOSE			"nsslapd-db-verbose"
#define CONFIG_CHANGELOG_DB_DEBUG			"nsslapd-db-debug"
#define CONFIG_CHANGELOG_DB_TRICKLE_PERCENTAGE		"nsslapd-db-trickle-percentage"
#define CONFIG_CHANGELOG_DB_SPINCOUNT			"nsslapd-db-spin-count"
/* Changelog Internal Configuration Parameters -> Changelog Cache related */
#define CONFIG_CHANGELOG_CACHESIZE			"nsslapd-cachesize"
#define CONFIG_CHANGELOG_CACHEMEMSIZE			"nsslapd-cachememsize"
#define CONFIG_CHANGELOG_NB_LOCK	"nsslapd-db-locks"
#define CONFIG_CHANGELOG_MAX_CONCURRENT_WRITES	"nsslapd-changelogmaxconcurrentwrites"

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
/*
 * Changed version from 2.0 to 3.0 when we switched from libdb33 to libdb41
 * noriko 20021203
 */
#define CHANGELOG_DB_VERSION_PREV "3.0"
#define CHANGELOG_DB_VERSION "4.0"
extern char *repl_plugin_name;
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
