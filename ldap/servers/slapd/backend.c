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

/* backend.c - Slapi_Backend methods */

#include "slap.h"
#include "nspr.h"

static PRMonitor *global_backend_mutex = NULL;

void
be_init(Slapi_Backend *be, const char *type, const char *name, int isprivate, int logchanges, int sizelimit, int timelimit)
{
    slapdFrontendConfig_t *fecfg;
    be->be_suffix = NULL;
    /* e.g. dn: cn=config,cn=NetscapeRoot,cn=ldbm database,cn=plugins,cn=config */
    be->be_basedn = slapi_create_dn_string("cn=%s,cn=%s,cn=plugins,cn=config",
                                           name, type);
    if (NULL == be->be_basedn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "be_init", "Failed create instance dn for plugin %s, "
                                 "instance %s\n",
                      type, name);
    }
    be->be_configdn = slapi_create_dn_string("cn=config,cn=%s,cn=%s,cn=plugins,cn=config",
                                             name, type);
    if (NULL == be->be_configdn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "be_init", "Failed create instance config dn for "
                                 "plugin %s, instance %s\n",
                      type, name);
    }
    be->be_monitordn = slapi_create_dn_string("cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config",
                                              name, type);
    if (NULL == be->be_monitordn) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "be_init", "Failed create instance monitor dn for "
                                 "plugin %s, instance %s\n",
                      type, name);
    }
    be->be_sizelimit = sizelimit;
    be->be_pagedsizelimit = config_get_pagedsizelimit();
    be->be_timelimit = timelimit;
    /* maximum group nesting level before giving up */
    be->be_maxnestlevel = SLAPD_DEFAULT_GROUPNESTLEVEL;
    be->be_noacl = 0;
    be->be_flags = 0;
    if ((fecfg = getFrontendConfig()) != NULL) {
        if (fecfg->backendconfig != NULL && fecfg->backendconfig[0] != NULL) {
            be->be_backendconfig = slapi_ch_strdup(fecfg->backendconfig[0]);
        } else {
            be->be_backendconfig = NULL;
        }
        be->be_readonly = fecfg->readonly;
    } else {
        be->be_readonly = 0;
        be->be_backendconfig = NULL;
    }
    be->be_lastmod = LDAP_UNDEFINED;
    be->be_type = slapi_ch_strdup(type);
    be->be_include = NULL;
    be->be_private = isprivate;
    be->be_logchanges = logchanges;
    be->be_database = NULL;
    be->be_writeconfig = NULL;
    be->be_delete_on_exit = 0;
    be->be_state = BE_STATE_STOPPED;
    be->be_state_lock = PR_NewLock();
    be->be_name = slapi_ch_strdup(name);
    be->be_mapped = 0;
    be->be_usn_counter = NULL;
}

void
be_done(Slapi_Backend *be)
{
    slapi_sdn_free(&be->be_suffix);
    slapi_ch_free_string(&be->be_basedn);
    slapi_ch_free_string(&be->be_configdn);
    slapi_ch_free_string(&be->be_monitordn);
    slapi_ch_free_string(&be->be_type);
    slapi_ch_free_string(&be->be_backendconfig);
    /* JCM char **be_include; ??? */
    slapi_ch_free_string(&be->be_name);
    if (!config_get_entryusn_global()) {
        slapi_counter_destroy(&be->be_usn_counter);
    }
    PR_DestroyLock(be->be_state_lock);
    if (be->be_lock != NULL) {
        slapi_destroy_rwlock(be->be_lock);
        be->be_lock = NULL;
    }
}

void
global_backend_lock_init()
{
    global_backend_mutex = PR_NewMonitor();
}

int
global_backend_lock_requested()
{
    return config_get_global_backend_lock();
}
void
global_backend_lock_lock()
{
    if (global_backend_mutex) {
        PR_EnterMonitor(global_backend_mutex);
    }
}

void
global_backend_lock_unlock()
{
    if (global_backend_mutex) {
        PR_ExitMonitor(global_backend_mutex);
    }
}

void
slapi_be_delete_onexit(Slapi_Backend *be)
{
    be->be_delete_on_exit = 1;
}

void
slapi_be_set_readonly(Slapi_Backend *be, int readonly)
{
    be->be_readonly = readonly;
}

int
slapi_be_get_readonly(Slapi_Backend *be)
{
    return be->be_readonly;
}

/*
 * Check if suffix, exactly matches a registered
 * suffix of this backend.
 */
int
slapi_be_issuffix(const Slapi_Backend *be, const Slapi_DN *suffix)
{
    /* this backend is no longer valid */
    if (be && be->be_state != BE_STATE_DELETED) {
        if (slapi_sdn_compare(be->be_suffix, suffix) == 0) {
            return 1;
        }
    }
    return 0;
}

int
be_isdeleted(const Slapi_Backend *be)
{
    return ((be == NULL) || (BE_STATE_DELETED == be->be_state));
}

void
be_addsuffix(Slapi_Backend *be, const Slapi_DN *suffix)
{
    if (be->be_state != BE_STATE_DELETED) {
        be->be_suffix = slapi_sdn_dup(suffix);;
    }
}

void
slapi_be_addsuffix(Slapi_Backend *be, const Slapi_DN *suffix)
{
    be_addsuffix(be, suffix);
}

const Slapi_DN *
slapi_be_getsuffix(Slapi_Backend *be, int n __attribute__((unused)))
{
    if (be && be->be_state != BE_STATE_DELETED) {
        return be->be_suffix;
    } else {
        return NULL;
    }
}

const char *
slapi_be_gettype(Slapi_Backend *be)
{
    const char *r = NULL;
    if (be->be_state != BE_STATE_DELETED) {
        r = be->be_type;
    }
    return r;
}

Slapi_DN *
be_getconfigdn(Slapi_Backend *be, Slapi_DN *dn)
{
    if (be->be_state == BE_STATE_DELETED) {
        slapi_sdn_set_ndn_byref(dn, NULL);
    } else {
        slapi_sdn_set_ndn_byref(dn, be->be_configdn);
    }
    return dn;
}

Slapi_DN *
be_getmonitordn(Slapi_Backend *be, Slapi_DN *dn)
{
    if (be->be_state == BE_STATE_DELETED) {
        slapi_sdn_set_ndn_byref(dn, NULL);
    } else {
        slapi_sdn_set_ndn_byref(dn, be->be_monitordn);
    }
    return dn;
}

int
be_writeconfig(Slapi_Backend *be)
{
    Slapi_PBlock *newpb;

    if (be->be_state == BE_STATE_DELETED || be->be_private ||
        (be->be_writeconfig == NULL)) {
        return -1;
    } else {
        newpb = slapi_pblock_new();
        slapi_pblock_set(newpb, SLAPI_PLUGIN, (void *)be->be_database);
        slapi_pblock_set(newpb, SLAPI_BACKEND, (void *)be);
        (be->be_writeconfig)(newpb);
        slapi_pblock_destroy(newpb);
        return 1;
    }
}

/*
 * Find out if changes made to entries in this backend
 * should be recorded in the changelog.
 */
int
slapi_be_logchanges(Slapi_Backend *be)
{
    if (be->be_state == BE_STATE_DELETED)
        return 0;

    return be->be_logchanges;
}

int
slapi_be_private(Slapi_Backend *be)
{
    if (be != NULL) {
        return (be->be_private);
    }

    return 0;
}

void *
slapi_be_get_instance_info(Slapi_Backend *be)
{
    PR_ASSERT(NULL != be);
    return be->be_instance_info;
}

void
slapi_be_set_instance_info(Slapi_Backend *be, void *data)
{
    PR_ASSERT(NULL != be);
    be->be_instance_info = data;
}

int
slapi_be_getentrypoint(Slapi_Backend *be, int entrypoint, void **ret_fnptr, Slapi_PBlock *pb)
{
    PR_ASSERT(NULL != be);

    /* this is something needed for most of the entry points */
    if (pb) {
        slapi_pblock_set(pb, SLAPI_PLUGIN, be->be_database);
        slapi_pblock_set(pb, SLAPI_BACKEND, be);
    }

    switch (entrypoint) {
    case SLAPI_PLUGIN_DB_BIND_FN:
        *ret_fnptr = (void *)be->be_bind;
        break;
    case SLAPI_PLUGIN_DB_UNBIND_FN:
        *ret_fnptr = (void *)be->be_unbind;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_FN:
        *ret_fnptr = (void *)be->be_search;
        break;
    case SLAPI_PLUGIN_DB_COMPARE_FN:
        *ret_fnptr = (void *)be->be_compare;
        break;
    case SLAPI_PLUGIN_DB_MODIFY_FN:
        *ret_fnptr = (void *)be->be_modify;
        break;
    case SLAPI_PLUGIN_DB_MODRDN_FN:
        *ret_fnptr = (void *)be->be_modrdn;
        break;
    case SLAPI_PLUGIN_DB_ADD_FN:
        *ret_fnptr = (void *)be->be_add;
        break;
    case SLAPI_PLUGIN_DB_DELETE_FN:
        *ret_fnptr = (void *)be->be_delete;
        break;
    case SLAPI_PLUGIN_DB_ABANDON_FN:
        *ret_fnptr = (void *)be->be_abandon;
        break;
    case SLAPI_PLUGIN_DB_CONFIG_FN:
        *ret_fnptr = (void *)be->be_config;
        break;
    case SLAPI_PLUGIN_CLOSE_FN:
        *ret_fnptr = (void *)be->be_close;
        break;
    case SLAPI_PLUGIN_START_FN:
        *ret_fnptr = (void *)be->be_start;
        break;
    case SLAPI_PLUGIN_DB_RESULT_FN:
        *ret_fnptr = (void *)be->be_result;
        break;
    case SLAPI_PLUGIN_DB_LDIF2DB_FN:
        *ret_fnptr = (void *)be->be_ldif2db;
        break;
    case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        *ret_fnptr = (void *)be->be_db2ldif;
        break;
    case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        *ret_fnptr = (void *)be->be_archive2db;
        break;
    case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        *ret_fnptr = (void *)be->be_db2archive;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
        *ret_fnptr = (void *)be->be_next_search_entry;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
        *ret_fnptr = (void *)be->be_next_search_entry_ext;
        break;
    case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
        *ret_fnptr = (void *)be->be_entry_release;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
        *ret_fnptr = (void *)be->be_search_results_release;
        break;
    case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
        *ret_fnptr = be->be_prev_search_results;
        break;
    case SLAPI_PLUGIN_DB_SIZE_FN:
        *ret_fnptr = (void *)be->be_dbsize;
        break;
    case SLAPI_PLUGIN_DB_TEST_FN:
        *ret_fnptr = (void *)be->be_dbtest;
        break;
    case SLAPI_PLUGIN_DB_RMDB_FN:
        *ret_fnptr = (void *)be->be_rmdb;
        break;
    case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
        *ret_fnptr = (void *)be->be_init_instance;
        break;
    case SLAPI_PLUGIN_DB_SEQ_FN:
        *ret_fnptr = (void *)be->be_seq;
        break;
    case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        *ret_fnptr = (void *)be->be_db2index;
        break;
    case SLAPI_PLUGIN_CLEANUP_FN:
        *ret_fnptr = (void *)be->be_cleanup;
        break;
    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_be_getentrypoint",
                      "Unknown entry point %d\n", entrypoint);
        return -1;
    }
    return 0;
}

int
slapi_be_setentrypoint(Slapi_Backend *be, int entrypoint, void *ret_fnptr, Slapi_PBlock *pb)
{
    PR_ASSERT(NULL != be);

    /* this is something needed for most of the entry points */
    if (pb) {
        struct slapdplugin *pb_plugin = NULL;
        slapi_pblock_get(pb, SLAPI_PLUGIN, &pb_plugin);
        be->be_database = pb_plugin;
        return 0;
    }

    switch (entrypoint) {
    case SLAPI_PLUGIN_DB_BIND_FN:
        be->be_bind = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_UNBIND_FN:
        be->be_unbind = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_FN:
        be->be_search = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_COMPARE_FN:
        be->be_compare = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_MODIFY_FN:
        be->be_modify = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_MODRDN_FN:
        be->be_modrdn = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_ADD_FN:
        be->be_add = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_DELETE_FN:
        be->be_delete = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_ABANDON_FN:
        be->be_abandon = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_CONFIG_FN:
        be->be_config = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_CLOSE_FN:
        be->be_close = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_START_FN:
        be->be_start = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_RESULT_FN:
        be->be_result = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_LDIF2DB_FN:
        be->be_ldif2db = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        be->be_db2ldif = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        be->be_archive2db = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        be->be_db2archive = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
        be->be_next_search_entry = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
        be->be_next_search_entry_ext = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
        be->be_entry_release = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
        be->be_search_results_release = (VFPP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
        be->be_prev_search_results = (VFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_SIZE_FN:
        be->be_dbsize = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_TEST_FN:
        be->be_dbtest = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_RMDB_FN:
        be->be_rmdb = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
        be->be_init_instance = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_SEQ_FN:
        be->be_seq = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        be->be_db2index = (IFP)ret_fnptr;
        break;
    case SLAPI_PLUGIN_CLEANUP_FN:
        be->be_cleanup = (IFP)ret_fnptr;
        break;
    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_be_setentrypoint",
                      "Unknown entry point %d\n", entrypoint);
        return -1;
    }
    return 0;
}

int
slapi_be_is_flag_set(Slapi_Backend *be, int flag)
{
    return be->be_flags & flag;
}

void
slapi_be_set_flag(Slapi_Backend *be, int flag)
{
    be->be_flags |= flag;
}

void
slapi_be_unset_flag(Slapi_Backend *be, int flag)
{
    be->be_flags &= ~flag;
}

char *
slapi_be_get_name(Slapi_Backend *be)
{
    return be->be_name;
}

void
be_set_sizelimit(Slapi_Backend *be, int sizelimit)
{
    be->be_sizelimit = sizelimit;
}

void
be_set_timelimit(Slapi_Backend *be, int timelimit)
{
    be->be_timelimit = timelimit;
}

void
be_set_pagedsizelimit(Slapi_Backend *be, int sizelimit)
{
    be->be_pagedsizelimit = sizelimit;
}

int
slapi_back_get_info(Slapi_Backend *be, int cmd, void **info)
{
    int rc = -1;
    if (!be || !be->be_get_info || !info) {
        return rc;
    }
    rc = (*be->be_get_info)(be, cmd, info);
    return rc;
}

int
slapi_back_set_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !be->be_set_info || !info) {
        return rc;
    }
    rc = (*be->be_set_info)(be, cmd, info);
    return rc;
}

int
slapi_back_ctrl_info(Slapi_Backend *be, int cmd, void *info)
{
    int rc = -1;
    if (!be || !be->be_ctrl_info || !info) {
        return rc;
    }
    rc = (*be->be_ctrl_info)(be, cmd, info);
    return rc;
}

/* API to expose DB transaction begin */
/* See memberof.c for usage. */
int
slapi_back_transaction_begin(Slapi_PBlock *pb)
{
    IFP txn_begin;
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_DB_BEGIN_FN, (void *)&txn_begin) ||
        !txn_begin) {
        return SLAPI_BACK_TRANSACTION_NOT_SUPPORTED;
    } else {
        return txn_begin(pb);
    }
}

/* API to expose DB transaction commit */
int
slapi_back_transaction_commit(Slapi_PBlock *pb)
{
    IFP txn_commit;
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_DB_COMMIT_FN, (void *)&txn_commit) ||
        !txn_commit) {
        return SLAPI_BACK_TRANSACTION_NOT_SUPPORTED;
    } else {
        return txn_commit(pb);
    }
}

/* API to expose DB transaction abort */
int
slapi_back_transaction_abort(Slapi_PBlock *pb)
{
    IFP txn_abort;
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_DB_ABORT_FN, (void *)&txn_abort) ||
        !txn_abort) {
        return SLAPI_BACK_TRANSACTION_NOT_SUPPORTED;
    } else {
        return txn_abort(pb);
    }
}
