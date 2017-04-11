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
#  include <config.h>
#endif

#include "slap.h"

typedef struct slapi_pblock {
    /* common */
    Slapi_Backend       *pb_backend;
    Connection  *pb_conn;
    Operation   *pb_op;
    struct slapdplugin  *pb_plugin; /* plugin being called */
    int     pb_opreturn;
    void*       pb_object;  /* points to data private to plugin */
    IFP     pb_destroy_fn;
    int     pb_requestor_isroot;
    /* config file */
    char        *pb_config_fname;
    int     pb_config_lineno;
    int     pb_config_argc;
    char        **pb_config_argv;
    int     plugin_tracking;

    /* [pre|post]add arguments */
    struct slapi_entry  *pb_target_entry; /* JCM - Duplicated */
    struct slapi_entry  *pb_existing_dn_entry;
    struct slapi_entry  *pb_existing_uniqueid_entry;
    struct slapi_entry  *pb_parent_entry;
    struct slapi_entry  *pb_newparent_entry;

    /* state of entry before and after add/delete/modify/moddn/modrdn */
    struct slapi_entry  *pb_pre_op_entry;
    struct slapi_entry  *pb_post_op_entry;
    /* seq access arguments */
    int             pb_seq_type;
    char            *pb_seq_attrname;
    char            *pb_seq_val;
    /* dbverify argument */
    char *pb_dbverify_dbdir;
    /* ldif2db arguments */
    char        *pb_ldif_file;
    int     pb_removedupvals;
    char        **pb_db2index_attrs;
    int     pb_ldif2db_noattrindexes;
    /* db2ldif arguments */
    int     pb_ldif_printkey;
    /* ldif2db/db2ldif/db2bak/bak2db args */
    char *pb_instance_name;
    Slapi_Task      *pb_task;
    int     pb_task_flags;
    /* matching rule arguments */
    mrFilterMatchFn pb_mr_filter_match_fn;
    IFP     pb_mr_filter_index_fn;
    IFP     pb_mr_filter_reset_fn;
    IFP     pb_mr_index_fn; /* values and keys are struct berval ** */
    char*       pb_mr_oid;
    char*       pb_mr_type;
    struct berval*  pb_mr_value;
    struct berval** pb_mr_values;
    struct berval** pb_mr_keys;
    unsigned int    pb_mr_filter_reusable;
    int     pb_mr_query_operator;
    unsigned int    pb_mr_usage;

    /* arguments for password storage scheme (kexcoff) */
    char *pb_pwd_storage_scheme_user_passwd;
    char *pb_pwd_storage_scheme_db_passwd;

    /* controls we know about */
    int     pb_managedsait;

    /* additional fields for plugin_internal_ldap_ops */
    /* result code of internal ldap_operation */
    int     pb_internal_op_result;
    /* pointer to array of results returned on search */
    Slapi_Entry **pb_plugin_internal_search_op_entries;
    char        **pb_plugin_internal_search_op_referrals;
    void        *pb_plugin_identity; /* identifies plugin for internal operation */
    char        *pb_plugin_config_area; /* optional config area */
    void        *pb_parent_txn; /* parent transaction ID */
    void        *pb_txn;        /* transaction ID */
    IFP     pb_txn_ruv_mods_fn; /* Function to fetch RUV mods for txn */

    /* Size of the database on disk, in kilobytes */
    unsigned int    pb_dbsize;

    /* THINGS BELOW THIS LINE EXIST ONLY IN SLAPI v2 (slapd 4.0+) */

    /* ldif2db: array of files to import all at once */
    char **pb_ldif_files;

    char        **pb_ldif_include;
    char        **pb_ldif_exclude;
    int     pb_ldif_dump_replica;
    int     pb_ldif_dump_uniqueid;      /* dump uniqueid during db2ldif */
    int     pb_ldif_generate_uniqueid;  /* generate uniqueid during db2ldif */
    char*     pb_ldif_namespaceid;      /* used for name based uniqueid generation */ 
    int     pb_ldif_encrypt;        /* used to enable encrypt/decrypt on import and export */ 
    /*
     * notes to log with RESULT line in the access log
     * these are actually stored as a bitmap; see slapi-plugin.h for
     *  defined notes.
     */
    unsigned int    pb_operation_notes;
    /*
     * slapd command line arguments
     */
    int pb_slapd_argc;
    char** pb_slapd_argv;
    char *pb_slapd_configdir; /* the config directory passed to slapd on the command line */
    LDAPControl **pb_ctrls_arg; /* allows to pass controls as arguments before
                                   operation object is created  */
    int pb_dse_dont_add_write; /* if true, the dse is not written when an entry is added */
    int pb_dse_add_merge; /* if true, if a duplicate entry is found when adding, the 
                             new values are merged into the old entry */
    int pb_dse_dont_check_dups; /* if false, use the "enhanced" version of str2entry to catch
                                   more errors when adding dse entries; this can only be done
                                   after the schema and syntax and matching rule plugins are
                                   running */
    int pb_dse_is_primary_file; /* for read callbacks: non-zero for primary file */
    int pb_schema_flags;        /* schema flags */
                                /* . check/load info (schema reload task) */
                                /* . refresh user defined schema */

    /* NEW in 5.0 for getting back the backend result in frontend */
    int pb_result_code;         /* operation result code */
    char * pb_result_text;      /* result text when available */
    char * pb_result_matched;   /* macthed dn when NO SUCH OBJECT  error */
    int pb_nentries;            /* number of entries to be returned */
    struct berval **urls;       /* urls of referrals to be returned */

    /*
     * wire import (fast replica init) arguments
     */
    struct slapi_entry *pb_import_entry;
    int pb_import_state;

    int pb_destroy_content;     /* flag to indicate that pblock content should be
                                   destroyed when pblock is destroyed */
    int pb_dse_reapply_mods; /* if true, dse_modify will reapply mods after modify callback */
    char * pb_urp_naming_collision_dn;  /* replication naming conflict removal */
    char * pb_urp_tombstone_uniqueid;   /* replication change tombstone */
    int     pb_server_running; /* indicate that server is running */
    int     pb_backend_count;  /* instance count involved in the op */

    /* For password policy control */
    int     pb_pwpolicy_ctrl;
    void    *pb_vattr_context;      /* hold the vattr_context for roles/cos */

    int     *pb_substrlens; /* user specified minimum substr search key lengths:
                             * nsSubStrBegin, nsSubStrMiddle, nsSubStrEnd
                             */
    int     pb_plugin_enabled; /* nsslapd-pluginEnabled: on|off */
                               /* used in plugin init; pb_plugin is not ready, then */
    LDAPControl **pb_search_ctrls; /* for search operations, allows plugins to provide
                                      controls to pass for each entry or referral returned */
    IFP     pb_mr_index_sv_fn; /* values and keys are Slapi_Value ** */
    int     pb_syntax_filter_normalized; /* the syntax filter types/values are already normalized */
    void        *pb_syntax_filter_data; /* extra data to pass to a syntax plugin function */
    int pb_paged_results_index;    /* stash SLAPI_PAGED_RESULTS_INDEX */
        int pb_paged_results_cookie;   /* stash SLAPI_PAGED_RESULTS_COOKIE */
    passwdPolicy *pwdpolicy;
    void *op_stack_elem;

    /* For ACI Target Check */
    int pb_aci_target_check; /* this flag prevents duplicate checking of ACI's target existence */

    struct slapi_entry *pb_pw_entry; /* stash dup'ed entry that shadow info is added/replaced */
#ifdef PBLOCK_ANALYTICS
    uint32_t analytics_init;
    PLHashTable *analytics;
#endif
} slapi_pblock;

