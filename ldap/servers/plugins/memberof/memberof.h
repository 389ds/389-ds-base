/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * memberof.h - memberOf shared definitions
 *
 */

#ifndef _MEMBEROF_H_
#define _MEMBEROF_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "portable.h"
#include "slapi-plugin.h"
#include <nspr.h>

/* Private API: to get SLAPI_DSE_RETURNTEXT_SIZE, DSE_FLAG_PREOP, and DSE_FLAG_POSTOP */
#include "slapi-private.h"

/*
 * macros
 */
#define MEMBEROF_PLUGIN_SUBSYSTEM "memberof-plugin" /* used for logging */
#define MEMBEROF_INT_PREOP_DESC   "memberOf internal postop plugin"
#define MEMBEROF_PREOP_DESC       "memberof preop plugin"
#define MEMBEROF_GROUP_ATTR       "memberOfGroupAttr"
#define MEMBEROF_ATTR             "memberOfAttr"
#define MEMBEROF_BACKEND_ATTR     "memberOfAllBackends"
#define MEMBEROF_ENTRY_SCOPE_ATTR "memberOfEntryScope"
#define MEMBEROF_SKIP_NESTED_ATTR "memberOfSkipNested"
#define MEMBEROF_AUTO_ADD_OC      "memberOfAutoAddOC"
#define NSMEMBEROF                "nsMemberOf"
#define MEMBEROF_ENTRY_SCOPE_EXCLUDE_SUBTREE "memberOfEntryScopeExcludeSubtree"
#define DN_SYNTAX_OID             "1.3.6.1.4.1.1466.115.121.1.12"
#define NAME_OPT_UID_SYNTAX_OID   "1.3.6.1.4.1.1466.115.121.1.34"


/*
 * structs
 */
typedef struct memberofconfig
{
    char **groupattrs;
    char *memberof_attr;
    int allBackends;
    Slapi_DN **entryScopes;
    int entryScopeCount;
    Slapi_DN **entryScopeExcludeSubtrees;
    int entryExcludeScopeCount;
    Slapi_Filter *group_filter;
    Slapi_Attr **group_slapiattrs;
    int skip_nested;
    int fixup_task;
    char *auto_add_oc;
    PLHashTable *ancestors_cache;
    PLHashTable *fixup_cache;
} MemberOfConfig;

/* The key to access the hash table is the normalized DN
 * The normalized DN is stored in the value because:
 *  - It is used in slapi_valueset_find
 *  - It is used to fill the memberof_get_groups_data.group_norm_vals
 */
typedef struct _memberof_cached_value
{
    char *key;
    char *group_dn_val;
    char *group_ndn_val;
    int valid;
} memberof_cached_value;

/*
 * functions
 */
int memberof_config(Slapi_Entry *config_e, Slapi_PBlock *pb);
void memberof_copy_config(MemberOfConfig *dest, MemberOfConfig *src);
void memberof_free_config(MemberOfConfig *config);
MemberOfConfig *memberof_get_config(void);
void memberof_rlock_config(void);
void memberof_wlock_config(void);
void memberof_unlock_config(void);
int memberof_config_get_all_backends(void);
void memberof_set_config_area(Slapi_DN *sdn);
Slapi_DN *memberof_get_config_area(void);
void memberof_set_plugin_area(Slapi_DN *sdn);
Slapi_DN *memberof_get_plugin_area(void);
int memberof_shared_config_validate(Slapi_PBlock *pb);
int memberof_apply_config(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
void *memberof_get_plugin_id(void);
void memberof_release_config(void);
PRUint64 get_plugin_started(void);
void ancestor_hashtable_entry_free(memberof_cached_value *entry);
PLHashTable *hashtable_new(int usetxn);
int memberof_use_txn(void);

#endif /* _MEMBEROF_H_ */
