/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef _H_RETROCL
#define _H_RETROCL 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slapi-private.h"
#include "slapi-plugin.h"
/* #include "portable.h" */
#include "dirver.h"
#include "ldaplog.h"
#include "ldif.h"
#include "slap.h"
#include <dirlite_strings.h>

/* max len of a long (2^64), represented as a string, including null byte */
#define	CNUMSTR_LEN		21
typedef unsigned long changeNumber;

typedef struct _cnum_result_t {
    int		crt_nentries;	/* number of entries returned from search */
    int		crt_err;	/* err returned from backend */
    Slapi_Entry	*crt_entry;	/* The entry returned from the backend */
} cnum_result_t;

typedef struct _cnumRet {
    changeNumber	cr_cnum;
    char		*cr_time;
    int			cr_lderr;
} cnumRet;

/* Operation types */
#define	OP_MODIFY  1
#define OP_ADD     2
#define OP_DELETE  3
#define OP_MODRDN  4

/* 
 * How often the changelog trimming thread runs. This is the minimum trim age.
 */
#define	CHANGELOGDB_TRIM_INTERVAL	300*1000 /* 5 minutes */

#define RETROCL_DLL_DEFAULT_THREAD_STACKSIZE 131072L
#define RETROCL_BE_CACHEMEMSIZE  "2097152"
#define RETROCL_BE_CACHESIZE "-1"
#define RETROCL_PLUGIN_NAME "DSRetroclPlugin"

/* was originally changelogmaximumage */
#define CONFIG_CHANGELOG_MAXAGE_ATTRIBUTE     "nsslapd-changelogmaxage"
#define CONFIG_CHANGELOG_DIRECTORY_ATTRIBUTE  "nsslapd-changelogdir"

#define RETROCL_CHANGELOG_DN "cn=changelog"
#define RETROCL_MAPPINGTREE_DN "cn=\"cn=changelog\",cn=mapping tree,cn=config"
#define RETROCL_PLUGIN_DN "cn=Retro Changelog Plugin,cn=plugins,cn=config"
#define RETROCL_LDBM_DN "cn=changelog,cn=ldbm database,cn=plugins,cn=config"
#define RETROCL_INDEX_DN "cn=changenumber,cn=index,cn=changelog,cn=ldbm database,cn=plugins,cn=config"

/* Allow anonymous access to the changelog base only, but not to the
 * entries in the changelog.
 */
#define RETROCL_ACL "(target =\"ldap:///cn=changelog\")(targetattr != \"aci\")(version 3.0; acl \"changelog base\"; allow( read,search, compare ) userdn =\"ldap:///anyone\";)"

enum {
  PLUGIN_RETROCL,
  PLUGIN_MAX
};

extern void* g_plg_identity [PLUGIN_MAX];
extern Slapi_Backend *retrocl_be_changelog;

extern const char *attr_changenumber;
extern const char *attr_targetdn;
extern const char *attr_changetype;
extern const char *attr_newrdn;
extern const char *attr_newsuperior;
extern const char *attr_deleteoldrdn;
extern const char *attr_changes;
extern const char *attr_changetime;
extern const char *attr_objectclass;

extern PRLock *retrocl_internal_lock;

/* Functions */

/* from repl_shared.h: not sure where defined */
unsigned long strntoul( char *from, size_t len, int base );

extern int retrocl_plugin_init(Slapi_PBlock *pb);

extern int retrocl_bepostop_delete (Slapi_PBlock *pb);
extern int retrocl_postop_add (Slapi_PBlock *pb);
extern int retrocl_postop_delete (Slapi_PBlock *pb);
extern int retrocl_postop_modify (Slapi_PBlock *pb);
extern int retrocl_postop_modrdn (Slapi_PBlock *pb);
extern int retrocl_postob(Slapi_PBlock *,int);

extern int retrocl_rootdse_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

extern void retrocl_create_cle(void);
extern int  retrocl_create_config(void);

extern changeNumber retrocl_get_first_changenumber(void);
extern void         retrocl_set_first_changenumber(changeNumber cn);
extern changeNumber retrocl_get_last_changenumber(void);
extern void         retrocl_commit_changenumber(void);
extern void         retrocl_release_changenumber(void);
extern changeNumber retrocl_assign_changenumber(void);
extern int          retrocl_get_changenumbers(void);
extern void         retrocl_forget_changenumbers(void);
extern time_t       retrocl_getchangetime( int type, int *err );

extern void retrocl_init_trimming(void);
extern void retrocl_stop_trimming(void);
extern char *retrocl_get_config_str(const char *attrt);

#endif /* _H_RETROCL */
