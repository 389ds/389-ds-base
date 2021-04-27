/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#ifndef _H_RETROCL
#define _H_RETROCL 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slapi-private.h"
#include "slapi-plugin.h"
#include "ldif.h"
#include "slap.h"

/* max len of a long (2^64), represented as a string, including null byte */
#define CNUMSTR_LEN 21
typedef unsigned long changeNumber;

typedef struct _cnum_result_t
{
    int crt_nentries; /* number of entries returned from search */
    int crt_err;      /* err returned from backend */
    time_t crt_time;  /* The changetime of the entry retrieved from the backend */
} cnum_result_t;

typedef struct _cnumRet
{
    changeNumber cr_cnum;
    char *cr_time;
    int cr_lderr;
} cnumRet;

/* Operation types */
#define OP_MODIFY 1
#define OP_ADD    2
#define OP_DELETE 3
#define OP_MODRDN 4

/*
 * How often the changelog trimming thread runs. This is the minimum trim age.
 */
#define DEFAULT_CHANGELOGDB_TRIM_INTERVAL 300 /* in second */

#define CONFIG_CHANGELOG_TRIM_INTERVAL "nsslapd-changelog-trim-interval"

#if defined(__hpux) && defined(__ia64)
#define RETROCL_DLL_DEFAULT_THREAD_STACKSIZE 524288L
#else
#define RETROCL_DLL_DEFAULT_THREAD_STACKSIZE 131072L
#endif
#define RETROCL_BE_CACHEMEMSIZE "209715200"
#define RETROCL_BE_CACHESIZE    "-1"
#define RETROCL_PLUGIN_NAME     "DSRetroclPlugin"

/* was originally changelogmaximumage */
#define CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE    "nsslapd-changelogmaxage"
#define CONFIG_CHANGELOG_DIRECTORY_ATTRIBUTE "nsslapd-changelogdir"
#define CONFIG_CHANGELOG_INCLUDE_SUFFIX      "nsslapd-include-suffix"
#define CONFIG_CHANGELOG_EXCLUDE_SUFFIX      "nsslapd-exclude-suffix"
#define CONFIG_CHANGELOG_EXCLUDE_ATTRS       "nsslapd-exclude-attrs"

#define RETROCL_CHANGELOG_DN   "cn=changelog"
#define RETROCL_MAPPINGTREE_DN "cn=\"cn=changelog\",cn=mapping tree,cn=config"
#define RETROCL_PLUGIN_DN      "cn=Retro Changelog Plugin,cn=plugins,cn=config"
#define RETROCL_LDBM_DN        "cn=changelog,cn=ldbm database,cn=plugins,cn=config"
#define RETROCL_INDEX_DN       "cn=changenumber,cn=index,cn=changelog,cn=ldbm database,cn=plugins,cn=config"

/* Allow anonymous access to the changelog base only, but not to the
 * entries in the changelog.
 */
#define RETROCL_ACL "(target =\"ldap:///cn=changelog\")(targetattr != \"aci\")(version 3.0; acl \"changelog base\"; allow( read,search, compare ) userdn =\"ldap:///anyone\";)"

enum
{
    PLUGIN_RETROCL,
    PLUGIN_MAX
};

extern void *g_plg_identity[PLUGIN_MAX];
extern Slapi_Backend *retrocl_be_changelog;
extern int retrocl_log_deleted;
extern int retrocl_nattributes;
extern char **retrocl_attributes;
extern char **retrocl_aliases;

extern const char *retrocl_changenumber;
extern const char *retrocl_targetdn;
extern const char *retrocl_changetype;
extern const char *retrocl_newrdn;
extern const char *retrocl_newsuperior;
extern const char *retrocl_deleteoldrdn;
extern const char *retrocl_changes;
extern const char *retrocl_changetime;
extern const char *retrocl_objectclass;
extern const char *retrocl_nsuniqueid;
extern const char *retrocl_isreplicated;

extern PRLock *retrocl_internal_lock;
extern Slapi_RWLock *retrocl_cn_lock;

/* Functions */

/* from repl_shared.h: not sure where defined */
unsigned long strntoul(char *from, size_t len, int base);

extern int retrocl_plugin_init(Slapi_PBlock *pb);

extern int retrocl_bepostop_delete(Slapi_PBlock *pb);
extern int retrocl_postop_add(Slapi_PBlock *pb);
extern int retrocl_postop_delete(Slapi_PBlock *pb);
extern int retrocl_postop_modify(Slapi_PBlock *pb);
extern int retrocl_postop_modrdn(Slapi_PBlock *pb);
extern int retrocl_postob(Slapi_PBlock *, int);

extern int retrocl_rootdse_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

extern void retrocl_create_cle(void);
extern int retrocl_create_config(void);

extern changeNumber retrocl_get_first_changenumber(void);
extern void retrocl_set_first_changenumber(changeNumber cn);
extern changeNumber retrocl_get_last_changenumber(void);
extern void retrocl_commit_changenumber(void);
extern void retrocl_release_changenumber(void);
extern void retrocl_set_check_changenumber(void);
extern changeNumber retrocl_assign_changenumber(void);
extern int retrocl_get_changenumbers(void);
extern void retrocl_forget_changenumbers(void);
extern time_t retrocl_getchangetime(int type, int *err);

extern void retrocl_init_trimming(void);
extern void retrocl_stop_trimming(void);
extern char *retrocl_get_config_str(const char *attrt);

int retrocl_entry_in_scope(Slapi_Entry *e);
int retrocl_attr_in_exclude_attrs(char *attr, int attrlen);

#endif /* _H_RETROCL */
