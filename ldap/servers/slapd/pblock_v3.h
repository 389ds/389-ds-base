/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* Hide the pblock implementtation types to keep it truly private */

#pragma once

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"

#ifdef DEBUG
/* #define PBLOCK_ANALYTICS 1 */
#else
#undef PBLOCK_ANALYTICS
#endif

typedef struct _slapi_pblock_dse
{
    int dont_add_write;  /* if true, the dse is not written when an entry is added */
    int add_merge;       /* if true, if a duplicate entry is found when adding, the
                      new values are merged into the old entry */
    int dont_check_dups; /* if false, use the "enhanced" version of str2entry to catch
                            more errors when adding dse entries; this can only be done
                            after the schema and syntax and matching rule plugins are
                            running */
    int is_primary_file; /* for read callbacks: non-zero for primary file */
    int reapply_mods;    /* if true, dse_modify will reapply mods after modify callback */
    int schema_flags;    /* schema flags */
                         /* . check/load info (schema reload task) */
                         /* . refresh user defined schema */

} slapi_pblock_dse;

typedef struct _slapi_pblock_task
{
    char *instance_name;
    Slapi_Task *task;
    char *seq_attrname;
    char *seq_val;
    char *dbverify_dbdir;
    char *ldif_file;
    char **db2index_attrs;

    /*
     * wire import (fast replica init) arguments
     */
    struct slapi_entry *import_entry;
    /* ldif2db: array of files to import all at once */
    char **ldif_files;
    char **ldif_include;
    char **ldif_exclude;
    char *ldif_namespaceid; /* used for name based uniqueid generation */
    int ldif_dump_replica;
    int ldif_dump_uniqueid;     /* dump uniqueid during db2ldif */
    int ldif_include_changelog;     /* include changelog for import/export */
    int ldif_generate_uniqueid; /* generate uniqueid during db2ldif */
    int ldif_encrypt;           /* used to enable encrypt/decrypt on import and export */
    int seq_type;
    int removedupvals;
    int ldif2db_noattrindexes;
    int ldif_printkey;
    int task_flags;
    int32_t task_warning;
    int import_state;

    int server_running; /* indicate that server is running */
} slapi_pblock_task;

typedef struct _slapi_pblock_matching_rule
{
    /* matching rule arguments */
    mrFilterMatchFn filter_match_fn;
    IFP filter_index_fn;
    IFP filter_reset_fn;
    IFP index_fn;    /* values and keys are struct berval ** */
    IFP index_sv_fn; /* values and keys are Slapi_Value ** */
    char *oid;
    char *type;
    struct berval *value;
    struct berval **values;
    struct berval **keys;
    unsigned int filter_reusable;
    int query_operator;
    unsigned int usage;
} slapi_pblock_matching_rule;

typedef struct _slapi_pblock_intplugin
{
    void *pb_object;             /* points to data private to plugin */
    void *pb_plugin_identity;    /* identifies plugin for internal operation */
    char *pb_plugin_config_area; /* optional config area */
    void *pb_vattr_context;      /* hold the vattr_context for roles/cos */

    void *pb_syntax_filter_data; /* extra data to pass to a syntax plugin function */
    int *pb_substrlens;          /* user specified minimum substr search key lengths:
                             * nsSubStrBegin, nsSubStrMiddle, nsSubStrEnd
                             */
    char *pb_slapd_configdir;    /* the config directory passed to slapd on the command line */
    IFP pb_destroy_fn;
    int pb_plugin_enabled;           /* nsslapd-pluginEnabled: on|off */
                                     /* used in plugin init; pb_plugin is not ready, then */
    int pb_syntax_filter_normalized; /* the syntax filter types/values are already normalized */

} slapi_pblock_intplugin;

/* Contains parts for internal operations that are NOT in the pb_op */
typedef struct _slapi_pblock_intop
{
    void *op_stack_elem;

    void *pb_txn;           /* transaction ID */
    IFP pb_txn_ruv_mods_fn; /* Function to fetch RUV mods for txn */
    passwdPolicy *pwdpolicy;
    LDAPControl **pb_ctrls_arg;      /* allows to pass controls as arguments before
                                   operation object is created  */
    struct slapi_entry *pb_pw_entry; /* stash dup'ed entry that shadow info is added/replaced */

    /* [pre|post]add arguments */
    struct slapi_entry *pb_target_entry; /* JCM - Duplicated */
    struct slapi_entry *pb_existing_dn_entry;
    struct slapi_entry *pb_existing_uniqueid_entry;
    struct slapi_entry *pb_parent_entry;
    struct slapi_entry *pb_newparent_entry;

    /* state of entry before and after add/delete/modify/moddn/modrdn */
    struct slapi_entry *pb_pre_op_entry;
    struct slapi_entry *pb_post_op_entry;

    /* pointer to array of results returned on search */
    Slapi_Entry **pb_plugin_internal_search_op_entries;
    char **pb_plugin_internal_search_op_referrals;
    LDAPControl **pb_search_ctrls; /* for search operations, allows plugins to provide
                                      controls to pass for each entry or referral returned */
    /* NEW in 5.0 for getting back the backend result in frontend */
    char *pb_result_text;             /* result text when available */
    char *pb_urp_naming_collision_dn; /* replication naming conflict removal */
    char *pb_urp_tombstone_uniqueid;  /* replication change tombstone */
    char * pb_urp_tombstone_conflict_dn; /* urp changed tombstone to conflict */
    int pb_opreturn;
    /* controls we know about */
    int pb_managedsait;
    /* additional fields for plugin_internal_ldap_ops */
    /* result code of internal ldap_operation */
    int pb_internal_op_result;
    int pb_requestor_isroot;
    int pb_nentries; /* number of entries to be returned */
    /*
     * notes to log with RESULT line in the access log
     * these are actually stored as a bitmap; see slapi-plugin.h for
     *  defined notes.
     */
    unsigned int pb_operation_notes;
    /* For password policy control */
    int pb_pwpolicy_ctrl;

    int pb_paged_results_index;  /* stash SLAPI_PAGED_RESULTS_INDEX */
    int pb_paged_results_cookie; /* stash SLAPI_PAGED_RESULTS_COOKIE */
    int32_t pb_usn_tombstone_incremented; /* stash SLAPI_PAGED_RESULTS_COOKIE */

    /* For memberof deferred thread
     * It is set by be_txn_postop with the task that
     * will be processed by the memberof deferred thread
     * It is reset by the be_postop, once the txn is committed
     * when it pushes the task to list of deferred tasks
     */
    void *memberof_deferred_task;
} slapi_pblock_intop;

/* Stuff that is rarely used, but still present */
typedef struct _slapi_pblock_misc
{
    char **pb_slapd_argv;
    /* Size of the database on disk, in kilobytes */
    unsigned int pb_dbsize;
    int pb_backend_count; /* instance count involved in the op */
    /*
     * slapd command line arguments
     */
    int pb_slapd_argc;
    /* For ACI Target Check */
    int pb_aci_target_check; /* this flag prevents duplicate checking of ACI's target existence */
} slapi_pblock_misc;

/* This struct is full of stuff we rarely, or never use. */
typedef struct _slapi_pblock_deprecated
{
    int pb_result_code;  /* operation result code */
    void *pb_parent_txn; /* parent transaction ID */
    int plugin_tracking;
    int pb_config_argc;
    char **pb_config_argv;
    char *pb_result_matched; /* macthed dn when NO SUCH OBJECT  error */
    int pb_destroy_content;  /* flag to indicate that pblock content should be
                                   destroyed when pblock is destroyed */
    struct berval **urls;    /* urls of referrals to be returned */
    /* arguments for password storage scheme (kexcoff) */
    char *pb_pwd_storage_scheme_user_passwd;
    char *pb_pwd_storage_scheme_db_passwd;
    /* config file */
    char *pb_config_fname;
    int pb_config_lineno;
} slapi_pblock_deprecated;

typedef struct slapi_pblock
{
    /* common */
    Slapi_Backend *pb_backend;
    Connection *pb_conn;
    Operation *pb_op;
    struct slapdplugin *pb_plugin; /* plugin being called */

    /* Private tree of fields for our operations, grouped by usage patterns */
    struct _slapi_pblock_dse *pb_dse;
    struct _slapi_pblock_task *pb_task;
    struct _slapi_pblock_matching_rule *pb_mr;
    struct _slapi_pblock_misc *pb_misc;
    struct _slapi_pblock_intop *pb_intop;
    struct _slapi_pblock_intplugin *pb_intplugin;
    struct _slapi_pblock_deprecated *pb_deprecated;
    int pb_deferred_memberof;

#ifdef PBLOCK_ANALYTICS
    uint32_t analytics_init;
    PLHashTable *analytics;
#endif
} slapi_pblock;
