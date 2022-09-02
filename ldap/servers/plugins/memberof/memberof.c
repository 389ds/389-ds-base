/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK
 **/

/* The memberof plugin updates the memberof attribute of entries
 * based on modifications performed on groupofuniquenames entries
 *
 * In addition the plugin provides a DS task that may be started
 * administrative clients and that creates the initial memberof
 * list for imported entries and/or fixes the memberof list of
 * existing entries that have inconsistent state (for example,
 * if the memberof attribute was incorrectly edited directly)
 *
 * To start the memberof task add an entry like:
 *
 * dn: cn=mytask, cn=memberof task, cn=tasks, cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: mytask
 * basedn: dc=example, dc=com
 * filter: (uid=test4)
 *
 * where "basedn" is required and refers to the top most node to perform the
 * task on, and where "filter" is an optional attribute that provides a filter
 * describing the entries to be worked on
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <time.h>
#include "slapi-plugin.h"
#include "string.h"
#include "nspr.h"
#include "plhash.h"
#include "memberof.h"

static Slapi_PluginDesc pdesc = {"memberof", VENDOR,
                                 DS_PACKAGE_VERSION, "memberof plugin"};

static void *_PluginID = NULL;
static Slapi_DN *_ConfigAreaDN = NULL;
static Slapi_RWLock *config_rwlock = NULL;
static Slapi_DN* _pluginDN = NULL;
MemberOfConfig *qsortConfig = 0;
static int usetxn = 0;
static int premodfn = 0;
static PRBool fixup_running = PR_FALSE;
static PRLock *fixup_lock = NULL;
static int32_t fixup_progress_count = 0;
static int64_t fixup_progress_elapsed = 0;
static int64_t fixup_start_time = 0;
#define FIXUP_PROGRESS_LIMIT 1000

typedef struct _memberofstringll
{
    const char *dn;
    void *next;
} memberofstringll;

typedef struct _memberof_get_groups_data
{
    MemberOfConfig *config;
    Slapi_Value *memberdn_val;
    Slapi_ValueSet **groupvals;
    Slapi_ValueSet **group_norm_vals;
    Slapi_ValueSet **already_seen_ndn_vals;
    PRBool use_cache;
} memberof_get_groups_data;

struct cache_stat
{
    int total_lookup;
    int successfull_lookup;
    int total_add;
    int total_remove;
    int total_enumerate;
    int cumul_duration_lookup;
    int cumul_duration_add;
    int cumul_duration_remove;
    int cumul_duration_enumerate;
};
static struct cache_stat cache_stat;
#define MEMBEROF_CACHE_DEBUG 0

typedef struct _task_data
{
    char *dn;
    char *bind_dn;
    char *filter_str;
} task_data;

/*** function prototypes ***/

/* exported functions */
int memberof_postop_init(Slapi_PBlock *pb);
static int memberof_internal_postop_init(Slapi_PBlock *pb);
static int memberof_preop_init(Slapi_PBlock *pb);

/* plugin callbacks */
static int memberof_postop_del(Slapi_PBlock *pb);
static int memberof_postop_modrdn(Slapi_PBlock *pb);
static int memberof_postop_modify(Slapi_PBlock *pb);
static int memberof_postop_add(Slapi_PBlock *pb);
static int memberof_postop_start(Slapi_PBlock *pb);
static int memberof_postop_close(Slapi_PBlock *pb);

/* supporting cast */
static int memberof_oktodo(Slapi_PBlock *pb);
static Slapi_DN *memberof_getsdn(Slapi_PBlock *pb);
static int memberof_modop_one(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *op_this_sdn, Slapi_DN *op_to_sdn);
static int memberof_modop_one_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_DN *op_to_sdn, memberofstringll *stack);
static int memberof_add_one(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *addthis_sdn, Slapi_DN *addto_sdn);
static int memberof_del_one(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *delthis_sdn, Slapi_DN *delfrom_sdn);
static int memberof_mod_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_Mod *smod);
static int memberof_add_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Mod *smod);
static int memberof_del_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Mod *smod);
static int memberof_mod_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_Attr *attr);
static int memberof_mod_attr_list_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_Attr *attr, memberofstringll *stack);
static int memberof_add_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Attr *attr);
static int memberof_del_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Attr *attr);
static int memberof_moddn_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *pre_sdn, Slapi_DN *post_sdn, Slapi_Attr *attr);
static int memberof_replace_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn);
static void memberof_set_plugin_id(void *plugin_id);
static int memberof_compare(MemberOfConfig *config, const void *a, const void *b);
static int memberof_qsort_compare(const void *a, const void *b);
static void memberof_load_array(Slapi_Value **array, Slapi_Attr *attr);
static int memberof_del_dn_from_groups(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *sdn);
static int memberof_call_foreach_dn(Slapi_PBlock *pb, Slapi_DN *sdn, MemberOfConfig *config, char **types, plugin_search_entry_callback callback, void *callback_data, int *cached, PRBool use_grp_cache);
static int memberof_is_direct_member(MemberOfConfig *config, Slapi_Value *groupdn, Slapi_Value *memberdn);
static int memberof_is_grouping_attr(char *type, MemberOfConfig *config);
static Slapi_ValueSet *memberof_get_groups(MemberOfConfig *config, Slapi_DN *member_sdn);
static int memberof_get_groups_r(MemberOfConfig *config, Slapi_DN *member_sdn, memberof_get_groups_data *data);
static int memberof_get_groups_callback(Slapi_Entry *e, void *callback_data);
static int memberof_test_membership(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn);
static int memberof_test_membership_callback(Slapi_Entry *e, void *callback_data);
static int memberof_del_dn_type_callback(Slapi_Entry *e, void *callback_data);
static int memberof_replace_dn_type_callback(Slapi_Entry *e, void *callback_data);
static int memberof_replace_dn_from_groups(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *pre_sdn, Slapi_DN *post_sdn);
static int memberof_modop_one_replace_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_DN *replace_with_sdn, Slapi_DN *op_to_sdn, memberofstringll *stack);
static int memberof_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static void memberof_task_destructor(Slapi_Task *task);
static void memberof_fixup_task_thread(void *arg);
static int memberof_fix_memberof(MemberOfConfig *config, Slapi_Task *task, task_data *td);
static int memberof_fix_memberof_callback(Slapi_Entry *e, void *callback_data);
static int memberof_entry_in_scope(MemberOfConfig *config, Slapi_DN *sdn);
static int memberof_add_objectclass(char *auto_add_oc, const char *dn);
static int memberof_add_memberof_attr(LDAPMod **mods, const char *dn, char *add_oc);
static memberof_cached_value *ancestors_cache_lookup(MemberOfConfig *config, const char *ndn);
static PRBool ancestors_cache_remove(MemberOfConfig *config, const char *ndn);
static PLHashEntry *ancestors_cache_add(MemberOfConfig *config, const void *key, void *value);

/*** implementation ***/


/*** exported functions ***/

/*
 * memberof_postop_init()
 *
 * Register plugin call backs
 *
 */
int
memberof_postop_init(Slapi_PBlock *pb)
{
    int ret = 0;
    char *memberof_plugin_identity = 0;
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    char *preop_plugin_type = NULL;
    int delfn = SLAPI_PLUGIN_POST_DELETE_FN;
    int mdnfn = SLAPI_PLUGIN_POST_MODRDN_FN;
    int modfn = SLAPI_PLUGIN_POST_MODIFY_FN;
    int addfn = SLAPI_PLUGIN_POST_ADD_FN;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_init\n");

    /* get args */
    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        usetxn = 1;
        delfn = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
        mdnfn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        modfn = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        addfn = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
    }

    if (usetxn) {
        preop_plugin_type = "betxnpreoperation";
        premodfn = SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN;
    } else {
        preop_plugin_type = "preoperation";
        premodfn = SLAPI_PLUGIN_PRE_MODIFY_FN;
    }

    if (plugin_entry) {
        memberof_set_plugin_area(slapi_entry_get_sdn(plugin_entry));
    }

    /*
     * Get plugin identity and stored it for later use
     * Used for internal operations
     */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &memberof_plugin_identity);
    PR_ASSERT(memberof_plugin_identity);
    memberof_set_plugin_id(memberof_plugin_identity);

    ret = (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
           slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0 ||
           slapi_pblock_set(pb, delfn, (void *)memberof_postop_del) != 0 ||
           slapi_pblock_set(pb, mdnfn, (void *)memberof_postop_modrdn) != 0 ||
           slapi_pblock_set(pb, modfn, (void *)memberof_postop_modify) != 0 ||
           slapi_pblock_set(pb, addfn, (void *)memberof_postop_add) != 0 ||
           slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)memberof_postop_start) != 0 ||
           slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)memberof_postop_close) != 0);

    if (!ret && !usetxn &&
        slapi_register_plugin("internalpostoperation",       /* op type */
                              1,                             /* Enabled */
                              "memberof_postop_init",        /* this function desc */
                              memberof_internal_postop_init, /* init func */
                              MEMBEROF_INT_PREOP_DESC,       /* plugin desc */
                              NULL,                          /* ? */
                              memberof_plugin_identity /* access control */)) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_postop_init - Failed\n");
        ret = -1;
    } else if (ret) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_postop_init - Failed\n");
        ret = -1;
    }
    /*
     * Setup the preop plugin for shared config updates
     */
    if (!ret && slapi_register_plugin(preop_plugin_type,     /* op type */
                                      1,                     /* Enabled */
                                      "memberof_preop_init", /* this function desc */
                                      memberof_preop_init,   /* init func */
                                      MEMBEROF_PREOP_DESC,   /* plugin desc */
                                      NULL,                  /* ? */
                                      memberof_plugin_identity /* access control */)) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_preop_init - Failed\n");
        ret = -1;
    } else if (ret) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_preop_init - Failed\n");
        ret = -1;
    }

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_init\n");

    return ret;
}

static int
memberof_preop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, premodfn, (void *)memberof_shared_config_validate) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_preop_init: Failed to register plugin\n");
        status = -1;
    }

    return status;
}

static int
memberof_internal_postop_init(Slapi_PBlock *pb)
{
    int status = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *)memberof_postop_del) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *)memberof_postop_modrdn) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *)memberof_postop_modify) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *)memberof_postop_add) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_internal_postop_init - Failed to register plugin\n");
        status = -1;
    }

    return status;
}

/*
 * memberof_postop_start()
 *
 * Do plugin start up stuff
 *
 */
int
memberof_postop_start(Slapi_PBlock *pb)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    Slapi_Entry *config_e = NULL; /* entry containing plugin config */
    char *config_area = NULL;
    int result = 0;
    int rc = 0;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_start\n");

    if (config_rwlock == NULL) {
        if ((config_rwlock = slapi_new_rwlock()) == NULL) {
            rc = -1;
            goto bail;
        }
    }

    if (fixup_lock == NULL) {
        if ((fixup_lock = PR_NewLock()) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                    "memberof_postop_start - Failed to create fixup lock.\n");
            rc = -1;
            goto bail;
        }
    }

    /* Set the alternate config area if one is defined. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_AREA, &config_area);
    if (config_area) {
        search_pb = slapi_pblock_new();
        slapi_search_internal_set_pb(search_pb, config_area, LDAP_SCOPE_BASE, "objectclass=*",
                                     NULL, 0, NULL, NULL, memberof_get_plugin_id(), 0);
        slapi_search_internal_pb(search_pb);
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        if (LDAP_SUCCESS != result) {
            if (result == LDAP_NO_SUCH_OBJECT) {
                /* log an error and use the plugin entry for the config */
                slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                              "memberof_postop_start - Config entry \"%s\" does "
                              "not exist.\n",
                              config_area);
                rc = -1;
                goto bail;
            }
        } else {
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
            if (entries && entries[0]) {
                config_e = entries[0];
            } else {
                slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                              "memberof_postop_start - Config entry \"%s\" was "
                              "not located.\n",
                              config_area);
                rc = -1;
                goto bail;
            }
        }
    } else {
        /* The plugin entry itself contains the config */
        if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &config_e) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                          "memberof_postop_start - Missing config entry\n");
            rc = -1;
            goto bail;
        }
    }

    memberof_set_config_area(slapi_entry_get_sdn(config_e));
    if ((rc = memberof_config(config_e, pb)) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_postop_start - Configuration failed (%s)\n", ldap_err2string(rc));
        rc = -1;
        goto bail;
    }

    rc = slapi_plugin_task_register_handler("memberof task", memberof_task_add, pb);
    if (rc) {
        goto bail;
    }

    /*
     * TODO: start up operation actor thread
     * need to get to a point where server failure
     * or shutdown doesn't hose our operations
     * so we should create a task entry that contains
     * all required information to complete the operation
     * then the tasks can be restarted safely if
     * interrupted
     */

bail:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_start\n");

    return rc;
}

/*
 * memberof_postop_close()
 *
 * Do plugin shut down stuff
 *
 */
int
memberof_postop_close(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_close\n");

    slapi_plugin_task_unregister_handler("memberof task", memberof_task_add);
    memberof_release_config();
    slapi_sdn_free(&_ConfigAreaDN);
    slapi_sdn_free(&_pluginDN);
    slapi_destroy_rwlock(config_rwlock);
    config_rwlock = NULL;
    PR_DestroyLock(fixup_lock);
    fixup_lock = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_close\n");
    return 0;
}

void
memberof_set_config_area(Slapi_DN *dn)
{
    slapi_rwlock_wrlock(config_rwlock);
    slapi_sdn_free(&_ConfigAreaDN);
    _ConfigAreaDN = slapi_sdn_dup(dn);
    slapi_rwlock_unlock(config_rwlock);
}

Slapi_DN *
memberof_get_config_area()
{
    return _ConfigAreaDN;
}

int
memberof_sdn_config_cmp(Slapi_DN *sdn)
{
    int rc = 0;

    slapi_rwlock_rdlock(config_rwlock);
    rc = slapi_sdn_compare(sdn, memberof_get_config_area());
    slapi_rwlock_unlock(config_rwlock);

    return rc;
}

void
memberof_set_plugin_area(Slapi_DN *sdn)
{
    slapi_sdn_free(&_pluginDN);
    _pluginDN = slapi_sdn_dup(sdn);
}

Slapi_DN *
memberof_get_plugin_area()
{
    return _pluginDN;
}

/*
 * memberof_postop_del()
 *
 * All entries with a memberOf attribute that contains the group DN get retrieved
 * and have the their memberOf attribute regenerated (it is far too complex and
 * error prone to attempt to change only those dn values involved in this case -
 * mainly because the deleted group may itself be a member of other groups which
 * may be members of other groups etc. in a big recursive mess involving dependency
 * chains that must be created and traversed in order to decide if an entry should
 * really have those groups removed too)
 */
int
memberof_postop_del(Slapi_PBlock *pb)
{
    int ret = SLAPI_PLUGIN_SUCCESS;
    MemberOfConfig *mainConfig = NULL;
    MemberOfConfig configCopy = {0};
    Slapi_DN *sdn;
    void *caller_id = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_del\n");

    /* We don't want to process internal modify
     * operations that originate from this plugin. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &caller_id);
    if (caller_id == memberof_get_plugin_id()) {
        /* Just return without processing */
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (memberof_oktodo(pb) && (sdn = memberof_getsdn(pb))) {
        struct slapi_entry *e = NULL;

        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
        memberof_rlock_config();
        mainConfig = memberof_get_config();
        if (!memberof_entry_in_scope(mainConfig, slapi_entry_get_sdn(e))) {
            /* The entry is not in scope, bail...*/
            memberof_unlock_config();
            goto bail;
        }
        memberof_copy_config(&configCopy, memberof_get_config());
        memberof_unlock_config();

        /* remove this DN from the
         * membership lists of groups
         */
        if ((ret = memberof_del_dn_from_groups(pb, &configCopy, sdn))) {
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                          "memberof_postop_del - Error deleting dn (%s) from group. Error (%d)\n",
                          slapi_sdn_get_dn(sdn), ret);
            goto bail;
        }

        /* is the entry of interest as a group? */
        if (e && configCopy.group_filter && 0 == slapi_filter_test_simple(e, configCopy.group_filter)) {
            int i = 0;
            Slapi_Attr *attr = 0;

            /* Loop through to find each grouping attribute separately. */
            for (i = 0; configCopy.groupattrs && configCopy.groupattrs[i] && ret == LDAP_SUCCESS; i++) {
                if (0 == slapi_entry_attr_find(e, configCopy.groupattrs[i], &attr)) {
                    if ((ret = memberof_del_attr_list(pb, &configCopy, sdn, attr))) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_del - Error deleting attr list - dn (%s). Error (%d)\n",
                                      slapi_sdn_get_dn(sdn), ret);
                    }
                }
            }
        }
    bail:
        memberof_free_config(&configCopy);
    }

    if (ret) {
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        ret = SLAPI_PLUGIN_FAILURE;
    }
    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_del\n");
    return ret;
}

typedef struct _memberof_del_dn_data
{
    char *dn;
    char *type;
} memberof_del_dn_data;

/* Deletes a member dn from all groups that refer to it. */
static int
memberof_del_dn_from_groups(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *sdn)
{
    int i = 0;
    char *groupattrs[2] = {0, 0};
    int rc = LDAP_SUCCESS;
    int cached = 0;

    /* Loop through each grouping attribute to find groups that have
     * dn as a member.  For any matches, delete the dn value from the
     * same grouping attribute. */
    for (i = 0; config->groupattrs && config->groupattrs[i] && rc == LDAP_SUCCESS; i++) {
        memberof_del_dn_data data = {(char *)slapi_sdn_get_dn(sdn),
                                     config->groupattrs[i]};

        groupattrs[0] = config->groupattrs[i];

        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                "memberof_del_dn_from_groups: Ancestors of %s   attr: %s\n",
                slapi_sdn_get_dn(sdn),
                groupattrs[0]);
        rc = memberof_call_foreach_dn(pb, sdn, config, groupattrs,
                                      memberof_del_dn_type_callback, &data, &cached, PR_FALSE);
    }

    return rc;
}

int
memberof_del_dn_type_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    LDAPMod mod;
    LDAPMod *mods[2];
    char *val[2];
    Slapi_PBlock *mod_pb = 0;

    mod_pb = slapi_pblock_new();

    mods[0] = &mod;
    mods[1] = 0;

    val[0] = ((memberof_del_dn_data *)callback_data)->dn;
    val[1] = 0;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = ((memberof_del_dn_data *)callback_data)->type;
    mod.mod_values = val;

    slapi_modify_internal_set_pb_ext(
        mod_pb, slapi_entry_get_sdn(e),
        mods, 0, 0,
        memberof_get_plugin_id(), SLAPI_OP_FLAG_BYPASS_REFERRALS);

    slapi_modify_internal_pb(mod_pb);

    slapi_pblock_get(mod_pb,
                     SLAPI_PLUGIN_INTOP_RESULT,
                     &rc);

    slapi_pblock_destroy(mod_pb);

    if (rc == LDAP_NO_SUCH_ATTRIBUTE && val[0] == NULL) {
        /* if no memberof attribute exists handle as success */
        rc = LDAP_SUCCESS;
    }
    return rc;
}

static void
add_ancestors_cbdata(memberof_cached_value *ancestors, void *callback_data)
{
    Slapi_Value *sval;
    int val_index;
    Slapi_ValueSet *group_norm_vals_cbdata = *((memberof_get_groups_data *)callback_data)->group_norm_vals;
    Slapi_ValueSet *group_vals_cbdata = *((memberof_get_groups_data *)callback_data)->groupvals;
    Slapi_Value *memberdn_val = ((memberof_get_groups_data *)callback_data)->memberdn_val;
    int added_group;
    PRBool empty_ancestor = PR_FALSE;

    for (val_index = 0, added_group = 0; ancestors[val_index].valid; val_index++) {
        /* For each of its ancestor (not already present) add it to callback_data */

        if (ancestors[val_index].group_ndn_val == NULL) {
            /* This is a node with no ancestor
             * ancestors should only contains this empty valid value
             * but just in case let the loop continue instead of a break
             */
            empty_ancestor = PR_TRUE;
            continue;
        }

        sval = slapi_value_new_string(ancestors[val_index].group_ndn_val);
        if (sval) {
            if (!slapi_valueset_find(
                    ((memberof_get_groups_data *)callback_data)->config->group_slapiattrs[0], group_norm_vals_cbdata, sval)) {
                /* This ancestor was not already present in the callback data
                 * => Add it to the callback_data
                 * Using slapi_valueset_add_value it consumes sval
                 * so do not free sval
                 */
                slapi_valueset_add_value_ext(group_norm_vals_cbdata, sval, SLAPI_VALUE_FLAG_PASSIN);
                sval = slapi_value_new_string(ancestors[val_index].group_dn_val);
                slapi_valueset_add_value_ext(group_vals_cbdata, sval, SLAPI_VALUE_FLAG_PASSIN);
                added_group++;
            } else {
                /* This ancestor was already present, free sval that will not be consumed */
                slapi_value_free(&sval);
            }
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "add_ancestors_cbdata: Ancestors of %s contained %d groups. %d added. %s\n",
                  slapi_value_get_string(memberdn_val), val_index, added_group, empty_ancestor ? "no ancestors" : "");
}

/*
 * Does a callback search of "type=dn" under the db suffix that "dn" is in,
 * unless all_backends is set, then we look at all the backends.  If "dn"
 * is a user, you'd want "type" to be "member".  If "dn" is a group, you
 * could want type to be either "member" or "memberOf" depending on the case.
 */
int
memberof_call_foreach_dn(Slapi_PBlock *pb __attribute__((unused)), Slapi_DN *sdn, MemberOfConfig *config, char **types, plugin_search_entry_callback callback, void *callback_data, int *cached, PRBool use_grp_cache)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_DN *base_sdn = NULL;
    Slapi_Backend *be = NULL;
    char *escaped_filter_val;
    char *filter_str = NULL;
    char *cookie = NULL;
    int all_backends = config->allBackends;
    int types_name_len = 0;
    int num_types = 0;
    int dn_len = slapi_sdn_get_ndn_len(sdn);
    int free_it = 0;
    int rc = 0;
    int i = 0;

    *cached = 0;

    if (!memberof_entry_in_scope(config, sdn)) {
        return (rc);
    }

    /* This flags indicates memberof_call_foreach_dn is called to retrieve ancestors (groups).
     * To improve performance, it can use a cache. (it will not in case of circular groups)
     * When this flag is true it means no circular group are detected (so far) so we can use the cache
     */
    if (use_grp_cache) {
        /* Here we will retrieve the ancestor of sdn.
         * The key access is the normalized sdn
         * This is done through recursive internal searches of parents
         * If the ancestors of sdn are already cached, just use
         * this value
         */
        memberof_cached_value *ht_grp = NULL;
        const char *ndn = slapi_sdn_get_ndn(sdn);

        ht_grp = ancestors_cache_lookup(config, (const void *)ndn);
        if (ht_grp) {
#if MEMBEROF_CACHE_DEBUG
            slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_call_foreach_dn: Ancestors of %s already cached (%x)\n", ndn, ht_grp);
#endif
            add_ancestors_cbdata(ht_grp, callback_data);
            *cached = 1;
            return (rc);
        }
    }
#if MEMBEROF_CACHE_DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_call_foreach_dn: Ancestors of %s not cached\n", ndn);
#endif
    /* Count the number of types. */
    for (num_types = 0; types && types[num_types]; num_types++) {
        /* Add up the total length of all attribute names.
         * We need to know this for building the filter. */
        types_name_len += strlen(types[num_types]);
    }

    /* Escape the dn, and build the search filter. */
    escaped_filter_val = slapi_escape_filter_value((char *)slapi_sdn_get_dn(sdn), dn_len);
    if (escaped_filter_val) {
        dn_len = strlen(escaped_filter_val);
        free_it = 1;
    } else {
        escaped_filter_val = (char *)slapi_sdn_get_dn(sdn);
    }

    if (num_types > 1) {
        int bytes_out = 0;
        int filter_str_len = types_name_len + (num_types * (3 + dn_len)) + 4;

        /* Allocate enough space for the filter */
        filter_str = slapi_ch_malloc(filter_str_len);

        /* Add beginning of filter. */
        bytes_out = snprintf(filter_str, filter_str_len - bytes_out, "(|");

        /* Add filter section for each type. */
        for (i = 0; types[i]; i++) {
            bytes_out += snprintf(filter_str + bytes_out, filter_str_len - bytes_out,
                                  "(%s=%s)", types[i], escaped_filter_val);
        }

        /* Add end of filter. */
        snprintf(filter_str + bytes_out, filter_str_len - bytes_out, ")");
    } else if (num_types == 1) {
        filter_str = slapi_ch_smprintf("(%s=%s)", types[0], escaped_filter_val);
    }

    if (free_it)
        slapi_ch_free_string(&escaped_filter_val);

    if (filter_str == NULL) {
        return rc;
    }

    search_pb = slapi_pblock_new();
    be = slapi_get_first_backend(&cookie);
    while (be) {
        PRBool do_suffix_search = PR_TRUE;
        if (!all_backends) {
            be = slapi_be_select(sdn);
            if (be == NULL) {
                break;
            }
        }
        if ((base_sdn = (Slapi_DN *)slapi_be_getsuffix(be, 0)) == NULL) {
            if (!all_backends) {
                break;
            } else {
                /* its ok, goto the next backend */
                be = slapi_get_next_backend(cookie);
                continue;
            }
        }

        if (config->entryScopes || config->entryScopeExcludeSubtrees) {
            if (memberof_entry_in_scope(config, base_sdn)) {
                /* do nothing, entry scope is spanning
                 * multiple suffixes, start at suffix */
            } else if (config->entryScopes) {
                for (size_t i = 0; config->entryScopes[i]; i++) {
                    if (slapi_sdn_issuffix(config->entryScopes[i], base_sdn)) {
                        /* Search each include scope */
                        slapi_search_internal_set_pb(search_pb, slapi_sdn_get_dn(config->entryScopes[i]),
                                                     LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0,
                                                     memberof_get_plugin_id(), 0);
                        slapi_search_internal_callback_pb(search_pb, callback_data, 0, callback, 0);
                        /* We already did the search for this backend, don't
                         * do it again when we fall through */
                        do_suffix_search = PR_FALSE;
                    }
                }
            } else if (!all_backends) {
                break;
            } else {
                /* its ok, goto the next backend */
                be = slapi_get_next_backend(cookie);
                continue;
            }
        }

        if (do_suffix_search) {
            slapi_search_internal_set_pb(search_pb, slapi_sdn_get_dn(base_sdn),
                                         LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0, memberof_get_plugin_id(), 0);
            slapi_search_internal_callback_pb(search_pb, callback_data, 0, callback, 0);
            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
            if (rc != LDAP_SUCCESS) {
                break;
            }
        }

        if (!all_backends) {
            break;
        }
        /* Reset pb for next loop */
        slapi_pblock_init(search_pb);
        be = slapi_get_next_backend(cookie);
    }

    slapi_pblock_destroy(search_pb);
    slapi_ch_free((void **)&cookie);
    slapi_ch_free_string(&filter_str);

    return rc;
}

/*
 * memberof_postop_modrdn()
 *
 * All entries with a memberOf attribute that contains the old group DN get retrieved
 * and have the old group DN deleted and the new group DN added to their memberOf attribute
 */
int
memberof_postop_modrdn(Slapi_PBlock *pb)
{
    int ret = SLAPI_PLUGIN_SUCCESS;
    void *caller_id = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_modrdn\n");

    /* We don't want to process internal modify
     * operations that originate from this plugin. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &caller_id);
    if (caller_id == memberof_get_plugin_id()) {
        /* Just return without processing */
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (memberof_oktodo(pb)) {
        MemberOfConfig *mainConfig = 0;
        MemberOfConfig configCopy = {0};
        struct slapi_entry *pre_e = NULL;
        struct slapi_entry *post_e = NULL;
        Slapi_DN *pre_sdn = 0;
        Slapi_DN *post_sdn = 0;

        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &pre_e);
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);
        if (pre_e && post_e) {
            pre_sdn = slapi_entry_get_sdn(pre_e);
            post_sdn = slapi_entry_get_sdn(post_e);
        }

        if (pre_sdn && post_sdn && slapi_sdn_compare(pre_sdn, post_sdn) == 0) {
            /* Regarding memberof plugin, this rename is a no-op
             * but it can be expensive to process it. So skip it
             */
            slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                    "memberof_postop_modrdn: Skip modrdn operation because src/dst identical %s\n",
                    slapi_sdn_get_dn(post_sdn));
            goto skip_op;
        }

        /* copy config so it doesn't change out from under us */
        memberof_rlock_config();
        mainConfig = memberof_get_config();
        memberof_copy_config(&configCopy, mainConfig);
        memberof_unlock_config();

        /* Need to check both the pre/post entries */
        if ((pre_sdn && !memberof_entry_in_scope(&configCopy, pre_sdn)) &&
            (post_sdn && !memberof_entry_in_scope(&configCopy, post_sdn))) {
            /* The entry is not in scope */
            goto bail;
        }

        /*  update any downstream members */
        if (pre_sdn && post_sdn && configCopy.group_filter &&
            0 == slapi_filter_test_simple(post_e, configCopy.group_filter)) {
            int i = 0;
            Slapi_Attr *attr = 0;

            /* get a list of member attributes present in the group
             * entry that is being renamed. */
            for (i = 0; configCopy.groupattrs && configCopy.groupattrs[i]; i++) {
                if (0 == slapi_entry_attr_find(post_e, configCopy.groupattrs[i], &attr)) {
                    if ((ret = memberof_moddn_attr_list(pb, &configCopy, pre_sdn, post_sdn, attr)) != 0) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_modrdn - Update failed for (%s), error (%d)\n",
                                      slapi_sdn_get_dn(pre_sdn), ret);
                        break;
                    }
                }
            }
        }

        /* It's possible that this is an entry who is a member
         * of other group entries.  We need to update any member
         * attributes to refer to the new name. */
        if (ret == LDAP_SUCCESS && pre_sdn && post_sdn) {
            if (!memberof_entry_in_scope(&configCopy, post_sdn)) {
                /*
                 * After modrdn the group contains both the pre and post DN's as
                 * members, so we need to cleanup both in this case.
                 */
                if ((ret = memberof_del_dn_from_groups(pb, &configCopy, pre_sdn))) {
                    slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                  "memberof_postop_modrdn - Delete dn failed for preop entry(%s), error (%d)\n",
                                  slapi_sdn_get_dn(pre_sdn), ret);
                }
                if ((ret = memberof_del_dn_from_groups(pb, &configCopy, post_sdn))) {
                    slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                  "memberof_postop_modrdn - Delete dn failed for postop entry(%s), error (%d)\n",
                                  slapi_sdn_get_dn(post_sdn), ret);
                }

                if (ret == LDAP_SUCCESS && pre_e && configCopy.group_filter &&
                    0 == slapi_filter_test_simple(pre_e, configCopy.group_filter)) {
                    /* is the entry of interest as a group? */
                    int i = 0;
                    Slapi_Attr *attr = 0;

                    /* Loop through to find each grouping attribute separately. */
                    for (i = 0; configCopy.groupattrs && configCopy.groupattrs[i] && ret == LDAP_SUCCESS; i++) {
                        if (0 == slapi_entry_attr_find(pre_e, configCopy.groupattrs[i], &attr)) {
                            if ((ret = memberof_del_attr_list(pb, &configCopy, pre_sdn, attr))) {
                                slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                              "memberof_postop_modrdn - Error deleting attr list - dn (%s). Error (%d)\n",
                                              slapi_sdn_get_dn(pre_sdn), ret);
                            }
                        }
                    }
                }
                if (ret == LDAP_SUCCESS) {
                    memberof_del_dn_data del_data = {0, configCopy.memberof_attr};
                    if ((ret = memberof_del_dn_type_callback(post_e, &del_data))) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_modrdn - Delete dn callback failed for (%s), error (%d)\n",
                                      slapi_entry_get_dn(post_e), ret);
                    }
                }
            } else {
                if ((ret = memberof_replace_dn_from_groups(pb, &configCopy, pre_sdn, post_sdn))) {
                    slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                  "memberof_postop_modrdn - Replace dn failed for (%s), error (%d)\n",
                                  slapi_sdn_get_dn(pre_sdn), ret);
                }
            }
        }
    bail:
        memberof_free_config(&configCopy);
    }

skip_op:
    if (ret) {
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        ret = SLAPI_PLUGIN_FAILURE;
    }
    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_modrdn\n");
    return ret;
}

typedef struct _replace_dn_data
{
    char *pre_dn;
    char *post_dn;
    char *type;
    char *add_oc;
} replace_dn_data;


/* Finds any groups that have pre_dn as a member and modifies them to
 * to use post_dn instead. */
static int
memberof_replace_dn_from_groups(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *pre_sdn, Slapi_DN *post_sdn)
{
    int i = 0;
    char *groupattrs[2] = {0, 0};
    int ret = LDAP_SUCCESS;
    int cached = 0;

    /* Loop through each grouping attribute to find groups that have
     * pre_dn as a member.  For any matches, replace pre_dn with post_dn
     * using the same grouping attribute. */
    for (i = 0; config->groupattrs && config->groupattrs[i]; i++) {
        replace_dn_data data = {(char *)slapi_sdn_get_dn(pre_sdn),
                                (char *)slapi_sdn_get_dn(post_sdn),
                                config->groupattrs[i],
                                config->auto_add_oc};

        groupattrs[0] = config->groupattrs[i];

        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_replace_dn_from_groups: Ancestors of %s\n", slapi_sdn_get_dn(post_sdn));
        if ((ret = memberof_call_foreach_dn(pb, pre_sdn, config, groupattrs,
                                            memberof_replace_dn_type_callback,
                                            &data, &cached, PR_FALSE))) {
            break;
        }
    }

    return ret;
}


int
memberof_replace_dn_type_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    LDAPMod delmod;
    LDAPMod addmod;
    LDAPMod *mods[3];
    char *delval[2];
    char *addval[2];
    char *dn = NULL;

    dn = slapi_entry_get_dn(e);

    mods[0] = &delmod;
    mods[1] = &addmod;
    mods[2] = 0;

    delval[0] = ((replace_dn_data *)callback_data)->pre_dn;
    delval[1] = 0;

    delmod.mod_op = LDAP_MOD_DELETE;
    delmod.mod_type = ((replace_dn_data *)callback_data)->type;
    delmod.mod_values = delval;

    addval[0] = ((replace_dn_data *)callback_data)->post_dn;
    addval[1] = 0;

    addmod.mod_op = LDAP_MOD_ADD;
    addmod.mod_type = ((replace_dn_data *)callback_data)->type;
    addmod.mod_values = addval;

    rc = memberof_add_memberof_attr(mods, dn,
                                    ((replace_dn_data *)callback_data)->add_oc);

    return rc;
}

/*
 * memberof_postop_modify()
 *
 * Added members are retrieved and have the group DN added to their memberOf attribute
 * Deleted members are retrieved and have the group DN deleted from their memberOf attribute
 * On replace of the membership attribute values:
 *     1. Sort old and new values
 *    2. Iterate through both lists at same time
 *    3. Any value not in old list but in new list - add group DN to memberOf attribute
 *    4. Any value in old list but not in new list - remove group DN from memberOf attribute
 *
 * Note: this will suck for large groups but nonetheless is optimal (it's linear) given
 * current restrictions i.e. originally adding members in sorted order would allow
 * us to sort one list only (the new one) but that is under server control, not this plugin
 */
int
memberof_postop_modify(Slapi_PBlock *pb)
{
    int ret = SLAPI_PLUGIN_SUCCESS;
    Slapi_DN *sdn = NULL;
    Slapi_Mods *smods = 0;
    Slapi_Mod *smod = 0;
    LDAPMod **mods;
    Slapi_Mod *next_mod = 0;
    void *caller_id = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_modify\n");

    /* We don't want to process internal modify
     * operations that originate from this plugin. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &caller_id);
    if (caller_id == memberof_get_plugin_id()) {
        /* Just return without processing */
        return SLAPI_PLUGIN_SUCCESS;
    }

    /* check if we are updating the shared config entry */
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (memberof_sdn_config_cmp(sdn) == 0) {
        Slapi_Entry *entry = NULL;
        char returntext[SLAPI_DSE_RETURNTEXT_SIZE];
        int result = 0;

        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &entry);
        if (entry) {
            if (SLAPI_DSE_CALLBACK_ERROR == memberof_apply_config(pb, NULL, entry, &result, returntext, NULL)) {
                slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM, "%s", returntext);
                ret = SLAPI_PLUGIN_FAILURE;
                goto done;
            }
        } else {
            /* this should not happen since this was validated in preop */
            ret = SLAPI_PLUGIN_FAILURE;
            goto done;
        }
        /* we're done, no need to do the other processing */
        goto done;
    }

    if (memberof_oktodo(pb)) {
        int config_copied = 0;
        MemberOfConfig *mainConfig = 0;
        MemberOfConfig configCopy = {0};

        /* get the mod set */
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
        smods = slapi_mods_new();
        slapi_mods_init_byref(smods, mods);

        next_mod = slapi_mod_new();
        smod = slapi_mods_get_first_smod(smods, next_mod);
        while (smod) {
            int interested = 0;
            char *type = (char *)slapi_mod_get_type(smod);

            /* We only want to copy the config if we encounter an
             * operation that we need to act on.  We also want to
             * only copy the config the first time it's needed so
             * it remains the same for all mods in the operation,
             * despite any config changes that may be made. */
            if (!config_copied) {
                memberof_rlock_config();
                mainConfig = memberof_get_config();

                if (memberof_is_grouping_attr(type, mainConfig)) {
                    interested = 1;
                    if (!memberof_entry_in_scope(mainConfig, sdn)) {
                        /* Entry is not in scope */
                        memberof_unlock_config();
                        goto bail;
                    }
                    /* copy config so it doesn't change out from under us */
                    memberof_copy_config(&configCopy, mainConfig);
                    config_copied = 1;
                }
                memberof_unlock_config();
            } else {
                if (memberof_is_grouping_attr(type, &configCopy)) {
                    interested = 1;
                }
            }

            if (interested) {
                int op = slapi_mod_get_operation(smod);

                /* the modify op decides the function */
                switch (op & ~LDAP_MOD_BVALUES) {
                case LDAP_MOD_ADD: {
                    /* add group DN to targets */
                    if ((ret = memberof_add_smod_list(pb, &configCopy, sdn, smod))) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_modify - Failed to add dn (%s) to target.  "
                                      "Error (%d)\n",
                                      slapi_sdn_get_dn(sdn), ret);
                        slapi_mod_done(next_mod);
                        goto bail;
                    }
                    break;
                }

                case LDAP_MOD_DELETE: {
                    /* If there are no values in the smod, we should
                         * just do a replace instead.  The  user is just
                         * trying to delete all members from this group
                         * entry, which the replace code deals with. */
                    if (slapi_mod_get_num_values(smod) == 0) {
                        if ((ret = memberof_replace_list(pb, &configCopy, sdn))) {
                            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                          "memberof_postop_modify - Failed to replace list (%s).  "
                                          "Error (%d)\n",
                                          slapi_sdn_get_dn(sdn), ret);
                            slapi_mod_done(next_mod);
                            goto bail;
                        }
                    } else {
                        /* remove group DN from target values in smod*/
                        if ((ret = memberof_del_smod_list(pb, &configCopy, sdn, smod))) {
                            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                          "memberof_postop_modify: failed to remove dn (%s).  "
                                          "Error (%d)\n",
                                          slapi_sdn_get_dn(sdn), ret);
                            slapi_mod_done(next_mod);
                            goto bail;
                        }
                    }
                    break;
                }

                case LDAP_MOD_REPLACE: {
                    /* replace current values */
                    if ((ret = memberof_replace_list(pb, &configCopy, sdn))) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_modify - Failed to replace values in  dn (%s).  "
                                      "Error (%d)\n",
                                      slapi_sdn_get_dn(sdn), ret);
                        slapi_mod_done(next_mod);
                        goto bail;
                    }
                    break;
                }

                default: {
                    slapi_log_err(
                        SLAPI_LOG_ERR,
                        MEMBEROF_PLUGIN_SUBSYSTEM,
                        "memberof_postop_modify - Unknown mod type\n");
                    ret = SLAPI_PLUGIN_FAILURE;
                    break;
                }
                }
            }

            slapi_mod_done(next_mod);
            smod = slapi_mods_get_next_smod(smods, next_mod);
        }

    bail:
        if (config_copied) {
            memberof_free_config(&configCopy);
        }

        slapi_mod_free(&next_mod);
        slapi_mods_free(&smods);
    }

done:
    if (ret) {
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        ret = SLAPI_PLUGIN_FAILURE;
    }

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_modify\n");
    return ret;
}


/*
 * memberof_postop_add()
 *
 * All members in the membership attribute of the new entry get retrieved
 * and have the group DN added to their memberOf attribute
 */
int
memberof_postop_add(Slapi_PBlock *pb)
{
    int ret = SLAPI_PLUGIN_SUCCESS;
    int interested = 0;
    Slapi_DN *sdn = 0;
    void *caller_id = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_postop_add\n");

    /* We don't want to process internal modify
     * operations that originate from this plugin. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &caller_id);
    if (caller_id == memberof_get_plugin_id()) {
        /* Just return without processing */
        return SLAPI_PLUGIN_SUCCESS;
    }

    if (memberof_oktodo(pb) && (sdn = memberof_getsdn(pb))) {
        struct slapi_entry *e = NULL;
        MemberOfConfig configCopy = {0};
        MemberOfConfig *mainConfig;
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);

        /* is the entry of interest? */
        memberof_rlock_config();
        mainConfig = memberof_get_config();
        if (e && mainConfig && mainConfig->group_filter &&
            0 == slapi_filter_test_simple(e, mainConfig->group_filter))

        {
            interested = 1;
            if (!memberof_entry_in_scope(mainConfig, slapi_entry_get_sdn(e))) {
                /* Entry is not in scope */
                memberof_unlock_config();
                goto bail;
            }
            memberof_copy_config(&configCopy, memberof_get_config());
        }
        memberof_unlock_config();

        if (interested) {
            int i = 0;
            Slapi_Attr *attr = 0;

            for (i = 0; configCopy.groupattrs && configCopy.groupattrs[i]; i++) {
                if (0 == slapi_entry_attr_find(e, configCopy.groupattrs[i], &attr)) {
                    if ((ret = memberof_add_attr_list(pb, &configCopy, sdn, attr))) {
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_postop_add - Failed to add dn(%s), error (%d)\n",
                                      slapi_sdn_get_dn(sdn), ret);
                        break;
                    }
                }
            }
            memberof_free_config(&configCopy);
        }
    }

bail:
    if (ret) {
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &ret);
        ret = SLAPI_PLUGIN_FAILURE;
    }

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_postop_add\n");

    return ret;
}

/*** Support functions ***/

/*
 * memberof_oktodo()
 *
 * Check that the op succeeded
 * Note: we also respond to replicated ops so we don't test for that
 * this does require that the memberOf attribute not be replicated
 * and this means that memberof is consistent with local state
 * not the network system state
 *
 */
int
memberof_oktodo(Slapi_PBlock *pb)
{
    int ret = 1;
    int oprc = 0;

    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> memberof_oktodo\n");

    if (!slapi_plugin_running(pb)) {
        ret = 0;
        goto bail;
    }

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &oprc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_oktodo - Could not get parameters\n");
        ret = -1;
    }

    /* this plugin should only execute if the operation succeeded */
    if (oprc != 0) {
        ret = 0;
    }

bail:
    slapi_log_err(SLAPI_LOG_TRACE, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- memberof_oktodo\n");

    return ret;
}

/*
 * Return 1 if the entry is in the scope.
 * For MODRDN the caller should check both the preop
 * and postop entries.  If we are moving out of, or
 * into scope, we should process it.
 */
static int
memberof_entry_in_scope(MemberOfConfig *config, Slapi_DN *sdn)
{
    if (config->entryScopeExcludeSubtrees) {
        int i = 0;

        /* check the excludes */
        while (config->entryScopeExcludeSubtrees[i]) {
            if (slapi_sdn_issuffix(sdn, config->entryScopeExcludeSubtrees[i])) {
                return 0;
            }
            i++;
        }
    }
    if (config->entryScopes) {
        int i = 0;

        /* check the excludes */
        while (config->entryScopes[i]) {
            if (slapi_sdn_issuffix(sdn, config->entryScopes[i])) {
                return 1;
            }
            i++;
        }
        return 0;
    }

    return 1;
}

static Slapi_DN *
memberof_getsdn(Slapi_PBlock *pb)
{
    Slapi_DN *sdn = NULL;

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);

    return sdn;
}

/*
 * memberof_modop_one()
 *
 * Perform op on memberof attribute of op_to using op_this as the value
 * However, if op_to happens to be a group, we must arrange for the group
 * members to have the mod performed on them instead, and we must take
 * care to not recurse when we have visted a group before
 *
 * Also, we must not delete entries that are a member of the group
 */
int
memberof_modop_one(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *op_this_sdn, Slapi_DN *op_to_sdn)
{
    return memberof_modop_one_r(pb, config, mod_op, op_this_sdn,
                                op_this_sdn, op_to_sdn, 0);
}

/* memberof_modop_one_r()
 *
 * recursive function to perform above (most things don't need the replace arg)
 */

int
memberof_modop_one_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_DN *op_to_sdn, memberofstringll *stack)
{
    return memberof_modop_one_replace_r(
        pb, config, mod_op, group_sdn, op_this_sdn, 0, op_to_sdn, stack);
}

/* memberof_modop_one_replace_r()
 *
 * recursive function to perform above (with added replace arg)
 */
int
memberof_modop_one_replace_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod_op, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_DN *replace_with_sdn, Slapi_DN *op_to_sdn, memberofstringll *stack)
{
    Slapi_PBlock *entry_pb = NULL;
    int rc = 0;
    LDAPMod mod;
    LDAPMod replace_mod;
    LDAPMod *mods[3];
    char *val[2];
    char *replace_val[2];
    Slapi_Entry *e = 0;
    memberofstringll *ll = 0;
    char *op_str = 0;
    const char *op_to;
    const char *op_this;
    Slapi_Value *to_dn_val = NULL;
    Slapi_Value *this_dn_val = NULL;

    op_to = slapi_sdn_get_ndn(op_to_sdn);
    op_this = slapi_sdn_get_ndn(op_this_sdn);

    /* Make sure we have valid DN's for the group(op_this) and the new member(op_to) */
    if (op_to && op_this) {
        to_dn_val = slapi_value_new_string(op_to);
        this_dn_val = slapi_value_new_string(op_this);
    }
    if (to_dn_val == NULL) {
        const char *udn = op_to_sdn ? slapi_sdn_get_udn(op_to_sdn) : "";
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_modop_one_replace_r - Failed to get DN value from "
                      "member value (%s)\n",
                      udn);
        goto bail;
    }
    if (this_dn_val == NULL) {
        const char *udn = op_this_sdn ? slapi_sdn_get_udn(op_this_sdn) : "";
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_modop_one_replace_r - Failed to get DN value from"
                      "group (%s)\n",
                      udn);
        goto bail;
    }
    /* op_this and op_to are both case-normalized */
    slapi_value_set_flags(this_dn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);
    slapi_value_set_flags(to_dn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    if (config == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_modop_one_replace_r - NULL config parameter\n");
        goto bail;
    }

    /* determine if this is a group op or single entry */
    slapi_search_get_entry(&entry_pb, op_to_sdn, config->groupattrs, &e, memberof_get_plugin_id());
    if (!e) {
        /* In the case of a delete, we need to worry about the
         * missing entry being a nested group.  There's a small
         * window where another thread may have deleted a nested
         * group that our group_dn entry refers to.  This has the
         * potential of us missing some indirect member entries
         * that need to be updated. */
        if (LDAP_MOD_DELETE == mod_op) {
            Slapi_PBlock *search_pb = slapi_pblock_new();
            Slapi_DN *base_sdn = 0;
            Slapi_Backend *be = 0;
            char *filter_str;
            char *cookie = NULL;
            int n_entries = 0;
            int all_backends = config->allBackends;

            filter_str = slapi_filter_sprintf("(%s=%s%s)", config->memberof_attr, ESC_NEXT_VAL, op_to);
            be = slapi_get_first_backend(&cookie);
            while (be) {
                /*
                 * We can't tell for sure if the op_to entry is a
                 * user or a group since the entry doesn't exist
                 * anymore.  We can safely ignore the missing entry
                 * if no other entries have a memberOf attribute that
                 * points to the missing entry.
                 */
                if (!all_backends) {
                    be = slapi_be_select(op_to_sdn);
                    if (be == NULL) {
                        break;
                    }
                }
                if ((base_sdn = (Slapi_DN *)slapi_be_getsuffix(be, 0)) == NULL) {
                    if (!all_backends) {
                        break;
                    } else {
                        be = slapi_get_next_backend(cookie);
                        continue;
                    }
                }
                if (filter_str) {
                    slapi_search_internal_set_pb(search_pb, slapi_sdn_get_dn(base_sdn),
                                                 LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0,
                                                 memberof_get_plugin_id(), 0);

                    if (slapi_search_internal_pb(search_pb)) {
                        /* get result and log an error */
                        int res = 0;
                        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
                        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                                      "memberof_modop_one_replace_r - Error searching for members: %d\n", res);
                    } else {
                        slapi_pblock_get(search_pb, SLAPI_NENTRIES, &n_entries);
                        if (n_entries > 0) {
                            /* We want to fixup the membership for the
                             * entries that referred to the missing group
                             * entry.  This will fix the references to
                             * the missing group as well as the group
                             * represented by op_this. */
                            memberof_test_membership(pb, config, op_to_sdn);
                        }
                    }
                    slapi_free_search_results_internal(search_pb);
                }
                slapi_pblock_init(search_pb);
                if (!all_backends) {
                    break;
                }
                be = slapi_get_next_backend(cookie);
            }
            slapi_pblock_destroy(search_pb);
            slapi_ch_free_string(&filter_str);
            slapi_ch_free((void **)&cookie);
        }
        goto bail;
    }

    if (LDAP_MOD_DELETE == mod_op) {
        op_str = "DELETE";
    } else if (LDAP_MOD_ADD == mod_op) {
        op_str = "ADD";
    } else if (LDAP_MOD_REPLACE == mod_op) {
        op_str = "REPLACE";
    } else {
        op_str = "UNKNOWN";
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "memberof_modop_one_replace_r - %s %s in %s\n", op_str, op_this, op_to);

    if (config->group_filter && 0 == slapi_filter_test_simple(e, config->group_filter)) {
        /* group */
        Slapi_Value *ll_dn_val = 0;
        Slapi_Attr *members = 0;
        int i = 0;

        ll = stack;

        /* have we been here before? */
        while (ll) {
            ll_dn_val = slapi_value_new_string(ll->dn);
            /* ll->dn is case-normalized */
            slapi_value_set_flags(ll_dn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

            if (0 == memberof_compare(config, &ll_dn_val, &to_dn_val)) {
                slapi_value_free(&ll_dn_val);

                /*     someone set up infinitely
                    recursive groups - bail out */
                slapi_log_err(SLAPI_LOG_PLUGIN,
                              MEMBEROF_PLUGIN_SUBSYSTEM,
                              "memberof_modop_one_replace_r - Group recursion"
                              " detected in %s\n",
                              op_to);
                goto bail;
            }

            slapi_value_free(&ll_dn_val);
            ll = ll->next;
        }

        /* do op on group */
        slapi_log_err(SLAPI_LOG_PLUGIN,
                      MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_modop_one_replace_r - Descending into group %s\n",
                      op_to);
        /* Add the nested group's DN to the stack so we can detect loops later. */
        ll = (memberofstringll *)slapi_ch_malloc(sizeof(memberofstringll));
        ll->dn = op_to;
        ll->next = stack;

        /* Go through each grouping attribute one at a time. */
        for (i = 0; config->groupattrs && config->groupattrs[i]; i++) {
            slapi_entry_attr_find(e, config->groupattrs[i], &members);
            if (members) {
                if ((rc = memberof_mod_attr_list_r(pb, config, mod_op, group_sdn,
                                                   op_this_sdn, members, ll)) != 0) {
                    goto bail;
                }
            }
        }

        {
            /* Craziness follows:
             * strict-aliasing doesn't like the required cast
             * to void for slapi_ch_free so we are made to
             * juggle to get a normal thing done
             */
            void *pll = ll;
            slapi_ch_free(&pll);
            ll = 0;
        }
    }
    /* continue with operation */
    {
        /* We want to avoid listing a group as a memberOf itself
         * in case someone set up a circular grouping.
         */
        if (0 == memberof_compare(config, &this_dn_val, &to_dn_val)) {
            const char *strval = "NULL";
            if (this_dn_val) {
                strval = slapi_value_get_string(this_dn_val);
            }
            slapi_log_err(SLAPI_LOG_PLUGIN,
                          MEMBEROF_PLUGIN_SUBSYSTEM,
                          "memberof_modop_one_replace_r - Not processing memberOf "
                          "operations on self entry: %s\n",
                          strval);
            goto bail;
        }

        /* For add and del modify operations, we just regenerate the
         * memberOf attribute. */
        if (LDAP_MOD_DELETE == mod_op || LDAP_MOD_ADD == mod_op) {
            /* find parent groups and replace our member attr */
            rc = memberof_fix_memberof_callback(e, config);
        } else {
            /* single entry - do mod */
            mods[0] = &mod;
            if (LDAP_MOD_REPLACE == mod_op) {
                mods[1] = &replace_mod;
                mods[2] = 0;
            } else {
                mods[1] = 0;
            }

            val[0] = (char *)op_this;
            val[1] = 0;
            mod.mod_op = LDAP_MOD_REPLACE == mod_op ? LDAP_MOD_DELETE : mod_op;
            mod.mod_type = config->memberof_attr;
            mod.mod_values = val;

            if (LDAP_MOD_REPLACE == mod_op) {
                replace_val[0] = (char *)slapi_sdn_get_dn(replace_with_sdn);
                replace_val[1] = 0;

                replace_mod.mod_op = LDAP_MOD_ADD;
                replace_mod.mod_type = config->memberof_attr;
                replace_mod.mod_values = replace_val;
            }
            rc = memberof_add_memberof_attr(mods, op_to, config->auto_add_oc);
            if (rc == LDAP_NO_SUCH_ATTRIBUTE || rc == LDAP_TYPE_OR_VALUE_EXISTS) {
                if (rc == LDAP_TYPE_OR_VALUE_EXISTS) {
                    /*
                     * For some reason the new modrdn value is present, so retry
                     * the delete by itself and ignore the add op by tweaking
                     * the mod array.
                     */
                    mods[1] = NULL;
                    rc = memberof_add_memberof_attr(mods, op_to, config->auto_add_oc);
                } else {
                    /*
                     * The memberof value to be replaced does not exist so just
                     * add the new value.  Shuffle the mod array to apply only
                     * the add operation.
                     */
                    mods[0] = mods[1];
                    mods[1] = NULL;
                    rc = memberof_add_memberof_attr(mods, op_to, config->auto_add_oc);
                    if (rc == LDAP_TYPE_OR_VALUE_EXISTS) {
                        /*
                         * The entry already has the expected memberOf value, no
                         * problem just return success.
                         */
                        rc = LDAP_SUCCESS;
                    }
                }
            }
        }
    }

bail:
    slapi_value_free(&to_dn_val);
    slapi_value_free(&this_dn_val);
    slapi_search_get_entry_done(&entry_pb);
    return rc;
}


/*
 * memberof_add_one()
 *
 * Add addthis DN to the memberof attribute of addto
 *
 */
int
memberof_add_one(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *addthis_sdn, Slapi_DN *addto_sdn)
{
    return memberof_modop_one(pb, config, LDAP_MOD_ADD, addthis_sdn, addto_sdn);
}

/*
 * memberof_del_one()
 *
 * Delete delthis DN from the memberof attribute of delfrom
 *
 */
int
memberof_del_one(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *delthis_sdn, Slapi_DN *delfrom_sdn)
{
    return memberof_modop_one(pb, config, LDAP_MOD_DELETE, delthis_sdn, delfrom_sdn);
}

/*
 * memberof_mod_smod_list()
 *
 * Perform mod for group DN to the memberof attribute of the list of targets
 *
 */
int
memberof_mod_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_Mod *smod)
{
    int rc = 0;
    struct berval *bv = slapi_mod_get_first_value(smod);
    int last_size = 0;
    char *last_str = 0;
    Slapi_DN *sdn = slapi_sdn_new();

    while (bv) {
        char *dn_str = 0;

        if (last_size > bv->bv_len) {
            dn_str = last_str;
        } else {
            int the_size = (bv->bv_len * 2) + 1;

            if (last_str)
                slapi_ch_free_string(&last_str);

            dn_str = (char *)slapi_ch_malloc(the_size);

            last_str = dn_str;
            last_size = the_size;
        }

        memset(dn_str, 0, last_size);

        strncpy(dn_str, bv->bv_val, (size_t)bv->bv_len);
        slapi_sdn_set_dn_byref(sdn, dn_str);

        if ((rc = memberof_modop_one(pb, config, mod, group_sdn, sdn))) {
            break;
        }

        bv = slapi_mod_get_next_value(smod);
    }

    slapi_sdn_free(&sdn);
    if (last_str)
        slapi_ch_free_string(&last_str);

    return rc;
}

/*
 * memberof_add_smod_list()
 *
 * Add group DN to the memberof attribute of the list of targets
 *
 */
int
memberof_add_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Mod *smod)
{
    return memberof_mod_smod_list(pb, config, LDAP_MOD_ADD, group_sdn, smod);
}


/*
 * memberof_del_smod_list()
 *
 * Remove group DN from the memberof attribute of the list of targets
 *
 */
int
memberof_del_smod_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Mod *smod)
{
    return memberof_mod_smod_list(pb, config, LDAP_MOD_DELETE, group_sdn, smod);
}

/**
 * Plugin identity mgmt
 */
void
memberof_set_plugin_id(void *plugin_id)
{
    _PluginID = plugin_id;
}

void *
memberof_get_plugin_id()
{
    return _PluginID;
}


/*
 * memberof_mod_attr_list()
 *
 * Perform mod for group DN to the memberof attribute of the list of targets
 *
 */
int
memberof_mod_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_Attr *attr)
{
    return memberof_mod_attr_list_r(pb, config, mod, group_sdn, group_sdn,
                                    attr, 0);
}

int
memberof_mod_attr_list_r(Slapi_PBlock *pb, MemberOfConfig *config, int mod, Slapi_DN *group_sdn, Slapi_DN *op_this_sdn, Slapi_Attr *attr, memberofstringll *stack)
{
    int rc = 0;
    Slapi_Value *val = 0;
    Slapi_Value *op_this_val = 0;
    int last_size = 0;
    char *last_str = 0;
    int hint = slapi_attr_first_value(attr, &val);
    Slapi_DN *sdn = slapi_sdn_new();

    op_this_val = slapi_value_new_string(slapi_sdn_get_ndn(op_this_sdn));
    slapi_value_set_flags(op_this_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    while (val && rc == 0) {
        char *dn_str = 0;
        struct berval *bv = 0;

        /* We don't want to process a memberOf operation on ourselves. */
        if (0 != memberof_compare(config, &val, &op_this_val)) {
            bv = (struct berval *)slapi_value_get_berval(val);

            if (last_size > bv->bv_len) {
                dn_str = last_str;
            } else {
                int the_size = (bv->bv_len * 2) + 1;

                if (last_str)
                    slapi_ch_free_string(&last_str);

                dn_str = (char *)slapi_ch_malloc(the_size);

                last_str = dn_str;
                last_size = the_size;
            }

            memset(dn_str, 0, last_size);

            strncpy(dn_str, bv->bv_val, (size_t)bv->bv_len);

            /* If we're doing a replace (as we would in the MODRDN case), we need
             * to specify the new group DN value */
            slapi_sdn_set_normdn_byref(sdn, dn_str); /* dn_str is normalized */
            if (mod == LDAP_MOD_REPLACE) {
                rc = memberof_modop_one_replace_r(pb, config, mod, group_sdn,
                                                  op_this_sdn, group_sdn,
                                                  sdn, stack);
            } else {
                rc = memberof_modop_one_r(pb, config, mod, group_sdn,
                                          op_this_sdn, sdn, stack);
            }
        }

        hint = slapi_attr_next_value(attr, hint, &val);
    }

    slapi_value_free(&op_this_val);

    slapi_sdn_free(&sdn);
    if (last_str)
        slapi_ch_free_string(&last_str);

    return rc;
}

/*
 * memberof_add_attr_list()
 *
 * Add group DN to the memberof attribute of the list of targets
 *
 */
int
memberof_add_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Attr *attr)
{
    return memberof_mod_attr_list(pb, config, LDAP_MOD_ADD, group_sdn, attr);
}

/*
 * memberof_del_attr_list()
 *
 * Remove group DN from the memberof attribute of the list of targets
 *
 */
int
memberof_del_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn, Slapi_Attr *attr)
{
    return memberof_mod_attr_list(pb, config, LDAP_MOD_DELETE, group_sdn, attr);
}

/*
 * memberof_moddn_attr_list()
 *
 * Perform mod for group DN to the memberof attribute of the list of targets
 *
 */
int
memberof_moddn_attr_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *pre_sdn, Slapi_DN *post_sdn, Slapi_Attr *attr)
{
    int rc = 0;
    Slapi_Value *val = 0;
    int last_size = 0;
    char *last_str = 0;
    int hint = slapi_attr_first_value(attr, &val);
    Slapi_DN *sdn = slapi_sdn_new();

    while (val && (rc == 0)) {
        char *dn_str = 0;
        struct berval *bv = (struct berval *)slapi_value_get_berval(val);

        if (last_size > bv->bv_len) {
            dn_str = last_str;
        } else {
            int the_size = (bv->bv_len * 2) + 1;

            if (last_str)
                slapi_ch_free_string(&last_str);

            dn_str = (char *)slapi_ch_malloc(the_size);

            last_str = dn_str;
            last_size = the_size;
        }

        memset(dn_str, 0, last_size);

        strncpy(dn_str, bv->bv_val, (size_t)bv->bv_len);

        slapi_sdn_set_normdn_byref(sdn, dn_str); /* dn_str is normalized */
        rc = memberof_modop_one_replace_r(pb, config, LDAP_MOD_REPLACE,
                                     post_sdn, pre_sdn, post_sdn, sdn, 0);

        hint = slapi_attr_next_value(attr, hint, &val);
    }

    slapi_sdn_free(&sdn);
    if (last_str)
        slapi_ch_free_string(&last_str);

    return rc;
}

/* memberof_get_groups()
 *
 * Gets a list of all groups that an entry is a member of.
 * This is done by looking only at member attribute values.
 * A Slapi_ValueSet* is returned.  It is up to the caller to
 * free it.
 */
Slapi_ValueSet *
memberof_get_groups(MemberOfConfig *config, Slapi_DN *member_sdn)
{
    Slapi_ValueSet *groupvals = slapi_valueset_new();
    Slapi_ValueSet *group_norm_vals = slapi_valueset_new();
    Slapi_ValueSet *already_seen_ndn_vals = slapi_valueset_new();
    Slapi_Value *memberdn_val =
        slapi_value_new_string(slapi_sdn_get_ndn(member_sdn));
    slapi_value_set_flags(memberdn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    memberof_get_groups_data data = {config, memberdn_val, &groupvals, &group_norm_vals, &already_seen_ndn_vals, PR_TRUE};

    memberof_get_groups_r(config, member_sdn, &data);

    slapi_value_free(&memberdn_val);
    slapi_valueset_free(group_norm_vals);
    slapi_valueset_free(already_seen_ndn_vals);

    return groupvals;
}

void
dump_cache_entry(memberof_cached_value *double_check, const char *msg)
{
    int i;
    for (i = 0; double_check[i].valid; i++) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "dump_cache_entry: %s -> %s\n",
                      msg ? msg : "<no key>",
                      double_check[i].group_dn_val ? double_check[i].group_dn_val : "NULL");
    }
}
/*
 * A cache value consist of an array of all dn/ndn of the groups member_ndn_val belongs to
 * The last element of the array has 'valid=0'
 * the firsts elements of the array has 'valid=1' and the dn/ndn of group it belong to
 */
static void
cache_ancestors(MemberOfConfig *config, Slapi_Value **member_ndn_val, memberof_get_groups_data *groups)
{
    Slapi_ValueSet *groupvals = *((memberof_get_groups_data *)groups)->groupvals;
    Slapi_Value *sval;
    Slapi_DN *sdn = 0;
    const char *dn;
    const char *ndn;
    const char *key;
    char *key_copy;
    int hint = 0;
    int count;
    int index;
    memberof_cached_value *cache_entry;
#if MEMBEROF_CACHE_DEBUG
    memberof_cached_value *double_check;
#endif

    if ((member_ndn_val == NULL) || (*member_ndn_val == NULL)) {
        slapi_log_err(SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors: Fail to cache groups ancestor of unknown member\n");
        return;
    }

    /* Allocate the cache entry and fill it */
    count = slapi_valueset_count(groupvals);
    if (count == 0) {
        /* There is no group containing member_ndn_val
         * so cache the NULL value
         */
        cache_entry = (memberof_cached_value *)slapi_ch_calloc(2, sizeof(memberof_cached_value));
        if (!cache_entry) {
            slapi_log_err(SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors: Fail to cache no group are ancestor of %s\n",
                          slapi_value_get_string(*member_ndn_val));
            return;
        }
        index = 0;
        cache_entry[index].key = NULL; /* only the last element (valid=0) contains the key */
        cache_entry[index].group_dn_val = NULL;
        cache_entry[index].group_ndn_val = NULL;
        cache_entry[index].valid = 1; /* this entry is valid and indicate no group contain member_ndn_val */
#if MEMBEROF_CACHE_DEBUG
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors: For %s cache %s\n",
                      slapi_value_get_string(*member_ndn_val), cache_entry[index].group_dn_val ? cache_entry[index].group_dn_val : "<empty>");
#endif
        index++;

    } else {
        cache_entry = (memberof_cached_value *)slapi_ch_calloc(count + 1, sizeof(memberof_cached_value));
        if (!cache_entry) {
            slapi_log_err(SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors: Fail to cache groups ancestor of %s\n",
                          slapi_value_get_string(*member_ndn_val));
            return;
        }

        /* Store the dn/ndn into the cache_entry */
        index = 0;
        hint = slapi_valueset_first_value(groupvals, &sval);
        while (sval) {
            /* In case of recursion the member_ndn can be present
             * in its own ancestors. Skip it
             */
            if (memberof_compare(groups->config, member_ndn_val, &sval)) {
                /* add this dn/ndn even if it is NULL
                                 * in fact a node belonging to no group needs to be cached
                                 */
                dn = slapi_value_get_string(sval);
                sdn = slapi_sdn_new_dn_byval(dn);
                ndn = slapi_sdn_get_ndn(sdn);

                cache_entry[index].key = NULL; /* only the last element (valid=0) contains the key */
                cache_entry[index].group_dn_val = slapi_ch_strdup(dn);
                cache_entry[index].group_ndn_val = slapi_ch_strdup(ndn);
                cache_entry[index].valid = 1;
#if MEMBEROF_CACHE_DEBUG
                slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors:    %s\n",
                              cache_entry[index].group_dn_val ? cache_entry[index].group_dn_val : "<empty>");
#endif
                index++;
                slapi_sdn_free(&sdn);
            }

            hint = slapi_valueset_next_value(groupvals, hint, &sval);
        }
    }
    /* This is marking the end of the cache_entry */
    key = slapi_value_get_string(*member_ndn_val);
    key_copy = slapi_ch_strdup(key);
    cache_entry[index].key = key_copy;
    cache_entry[index].group_dn_val = NULL;
    cache_entry[index].group_ndn_val = NULL;
    cache_entry[index].valid = 0;

/* Cache the ancestor of member_ndn_val using the
     * normalized DN key
     */
#if MEMBEROF_CACHE_DEBUG
    dump_cache_entry(cache_entry, key);
#endif
    if (ancestors_cache_add(config, (const void*) key_copy, (void *) cache_entry) == NULL) {
        slapi_log_err( SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM, "cache_ancestors: Failed to cache ancestor of %s\n", key);
        ancestor_hashtable_entry_free(cache_entry);
        slapi_ch_free ((void**)&cache_entry);
        return;
    }
#if MEMBEROF_CACHE_DEBUG
    if (double_check = ancestors_cache_lookup(config, (const void*) key)) {
        dump_cache_entry(double_check, "read back");
    }
#endif
}
/*
 * Add in v2 the values that are in v1
 * If the values are already present in v2, they are skipped
 * It does not consum the values in v1
 */
static void
merge_ancestors(Slapi_Value **member_ndn_val, memberof_get_groups_data *v1, memberof_get_groups_data *v2)
{
    Slapi_Value *sval_dn = 0;
    Slapi_Value *sval_ndn = 0;
    Slapi_Value *sval;
    Slapi_DN *val_sdn = 0;
    int hint = 0;
    MemberOfConfig *config = ((memberof_get_groups_data *)v2)->config;
    Slapi_ValueSet *v1_groupvals = *((memberof_get_groups_data *)v1)->groupvals;
    Slapi_ValueSet *v2_groupvals = *((memberof_get_groups_data *)v2)->groupvals;
    Slapi_ValueSet *v2_group_norm_vals = *((memberof_get_groups_data *)v2)->group_norm_vals;
    int merged_cnt = 0;

    hint = slapi_valueset_first_value(v1_groupvals, &sval);
    while (sval) {
        if (memberof_compare(config, member_ndn_val, &sval)) {
            sval_dn = slapi_value_new_string(slapi_value_get_string(sval));
            if (sval_dn) {
                /* Use the normalized dn from v1 to search it
                                 * in v2
                                 */
                val_sdn = slapi_sdn_new_dn_byval(slapi_value_get_string(sval_dn));
                sval_ndn = slapi_value_new_string(slapi_sdn_get_ndn(val_sdn));
                if (!slapi_valueset_find(
                        ((memberof_get_groups_data *)v2)->config->group_slapiattrs[0], v2_group_norm_vals, sval_ndn)) {
/* This ancestor was not already present in v2 => Add it
                                         * Using slapi_valueset_add_value it consumes val
                                         * so do not free sval
                                         */
#if MEMBEROF_CACHE_DEBUG
                    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "merge_ancestors: add %s\n", slapi_value_get_string(sval_ndn));
#endif
                    slapi_valueset_add_value_ext(v2_groupvals, sval_dn, SLAPI_VALUE_FLAG_PASSIN);
                    slapi_valueset_add_value_ext(v2_group_norm_vals, sval_ndn, SLAPI_VALUE_FLAG_PASSIN);
                    merged_cnt++;
                } else {
/* This ancestor was already present, free sval_ndn/sval_dn that will not be consumed */
#if MEMBEROF_CACHE_DEBUG
                    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "merge_ancestors: skip (already present) %s\n", slapi_value_get_string(sval_ndn));
#endif
                    slapi_value_free(&sval_dn);
                    slapi_value_free(&sval_ndn);
                }
                slapi_sdn_free(&val_sdn);
            }
        }
        hint = slapi_valueset_next_value(v1_groupvals, hint, &sval);
    }
}

int
memberof_get_groups_r(MemberOfConfig *config, Slapi_DN *member_sdn, memberof_get_groups_data *data)
{
    Slapi_ValueSet *groupvals = slapi_valueset_new();
    Slapi_ValueSet *group_norm_vals = slapi_valueset_new();
    Slapi_Value *member_ndn_val =
        slapi_value_new_string(slapi_sdn_get_ndn(member_sdn));
    int rc;
    int cached = 0;

    slapi_value_set_flags(member_ndn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    memberof_get_groups_data member_data = {config, member_ndn_val, &groupvals, &group_norm_vals, data->already_seen_ndn_vals, data->use_cache};

/* Search for any grouping attributes that point to memberdn.
     * For each match, add it to the list, recurse and do same search */
#if MEMBEROF_CACHE_DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_get_groups_r: Ancestors of %s\n", slapi_sdn_get_dn(member_sdn));
#endif
    rc = memberof_call_foreach_dn(NULL, member_sdn, config, config->groupattrs,
                                  memberof_get_groups_callback, &member_data, &cached, member_data.use_cache);

    merge_ancestors(&member_ndn_val, &member_data, data);
    if (!cached && member_data.use_cache)
        cache_ancestors(config, &member_ndn_val, &member_data);

    slapi_value_free(&member_ndn_val);
    slapi_valueset_free(groupvals);
    slapi_valueset_free(group_norm_vals);

    return rc;
}

/* memberof_get_groups_callback()
 *
 * Callback to perform work of memberof_get_groups()
 */
int
memberof_get_groups_callback(Slapi_Entry *e, void *callback_data)
{
    Slapi_DN *group_sdn = slapi_entry_get_sdn(e);
    char *group_ndn = slapi_entry_get_ndn(e);
    char *group_dn = slapi_entry_get_dn(e);
    Slapi_Value *group_ndn_val = 0;
    Slapi_Value *group_dn_val = 0;
    Slapi_Value *already_seen_ndn_val = 0;
    Slapi_ValueSet *groupvals = *((memberof_get_groups_data *)callback_data)->groupvals;
    Slapi_ValueSet *group_norm_vals = *((memberof_get_groups_data *)callback_data)->group_norm_vals;
    Slapi_ValueSet *already_seen_ndn_vals = *((memberof_get_groups_data *)callback_data)->already_seen_ndn_vals;
    MemberOfConfig *config = ((memberof_get_groups_data *)callback_data)->config;
    int rc = 0;

    if (slapi_is_shutting_down()) {
        rc = -1;
        goto bail;
    }

    if (!groupvals || !group_norm_vals) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_get_groups_callback - NULL groupvals or group_norm_vals\n");
        rc = -1;
        goto bail;
    }
    /* get the DN of the group */
    group_ndn_val = slapi_value_new_string(group_ndn);
    /* group_dn is case-normalized */
    slapi_value_set_flags(group_ndn_val, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    /* check if e is the same as our original member entry */
    if (0 == memberof_compare(((memberof_get_groups_data *)callback_data)->config,
                              &((memberof_get_groups_data *)callback_data)->memberdn_val, &group_ndn_val)) {
        /* A recursive group caused us to find our original
         * entry we passed to memberof_get_groups().  We just
         * skip processing this entry. */
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_get_groups_callback - Group recursion"
                      " detected in %s\n",
                      group_ndn);
        slapi_value_free(&group_ndn_val);
        ((memberof_get_groups_data *)callback_data)->use_cache = PR_FALSE;
        goto bail;
    }

    /* Have we been here before?  Note that we don't loop through all of the group_slapiattrs
     * in config.  We only need this attribute for it's syntax so the comparison can be
     * performed.  Since all of the grouping attributes are validated to use the Dinstinguished
     * Name syntax, we can safely just use the first group_slapiattr. */
    if (slapi_valueset_find(
            ((memberof_get_groups_data *)callback_data)->config->group_slapiattrs[0], already_seen_ndn_vals, group_ndn_val)) {
        /* we either hit a recursive grouping, or an entry is
         * a member of a group through multiple paths.  Either
         * way, we can just skip processing this entry since we've
         * already gone through this part of the grouping hierarchy. */
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_get_groups_callback - Possible group recursion"
                      " detected in %s\n",
                      group_ndn);
        slapi_value_free(&group_ndn_val);
        ((memberof_get_groups_data *)callback_data)->use_cache = PR_FALSE;
        goto bail;
    }

    /* if the group does not belong to an excluded subtree, adds it to the valueset */
    if (memberof_entry_in_scope(config, group_sdn)) {
        /* Push group_dn_val into the valueset.  This memory is now owned
             * by the valueset. */
        slapi_valueset_add_value_ext(group_norm_vals, group_ndn_val, SLAPI_VALUE_FLAG_PASSIN);

        group_dn_val = slapi_value_new_string(group_dn);
        slapi_valueset_add_value_ext(groupvals, group_dn_val, SLAPI_VALUE_FLAG_PASSIN);

        /* push this ndn to detect group recursion */
        already_seen_ndn_val = slapi_value_new_string(group_ndn);
        slapi_valueset_add_value_ext(already_seen_ndn_vals, already_seen_ndn_val, SLAPI_VALUE_FLAG_PASSIN);
    }
    if (!config->skip_nested || config->fixup_task) {
        /* now recurse to find ancestors groups of e */
        memberof_get_groups_r(((memberof_get_groups_data *)callback_data)->config,
                              group_sdn, callback_data);
    }

bail:
    return rc;
}

/* memberof_is_direct_member()
 *
 * tests for direct membership of memberdn in group groupdn
 * returns non-zero when true, zero otherwise
 */
int
memberof_is_direct_member(MemberOfConfig *config, Slapi_Value *groupdn, Slapi_Value *memberdn)
{
    Slapi_PBlock *pb = NULL;
    int rc = 0;
    Slapi_DN *sdn = 0;
    Slapi_Entry *group_e = 0;
    Slapi_Attr *attr = 0;
    int i = 0;

    sdn = slapi_sdn_new_normdn_byref(slapi_value_get_string(groupdn));

    slapi_search_get_entry(&pb, sdn, config->groupattrs,
                           &group_e, memberof_get_plugin_id());

    if (group_e) {
        /* See if memberdn is referred to by any of the group attributes. */
        for (i = 0; config->groupattrs && config->groupattrs[i]; i++) {
            slapi_entry_attr_find(group_e, config->groupattrs[i], &attr);
            if (attr && (0 == slapi_attr_value_find(attr, slapi_value_get_berval(memberdn)))) {
                rc = 1;
                break;
            }
        }
    }
    slapi_search_get_entry_done(&pb);

    slapi_sdn_free(&sdn);
    return rc;
}

/* memberof_is_grouping_attr()
 *
 * Checks if a supplied attribute is one of the configured
 * grouping attributes.
 *
 * Returns non-zero when true, zero otherwise.
 */
static int
memberof_is_grouping_attr(char *type, MemberOfConfig *config)
{
    int match = 0;
    int i = 0;

    for (i = 0; config && config->groupattrs && config->groupattrs[i]; i++) {
        match = slapi_attr_types_equivalent(type, config->groupattrs[i]);
        if (match) {
            /* If we found a match, we're done. */
            break;
        }
    }

    return match;
}

/* memberof_test_membership()
 *
 * Finds all entries who are a "memberOf" the group
 * represented by "group_dn".  For each matching entry, we
 * call memberof_test_membership_callback().
 *
 * for each attribute in the memberof attribute
 * determine if the entry is still a member.
 *
 * test each for direct membership
 * move groups entry is memberof to member group
 * test remaining groups for membership in member groups
 * iterate until a pass fails to move a group over to member groups
 * remaining groups should be deleted
 */
int
memberof_test_membership(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn)
{
    char *attrs[2] = {config->memberof_attr, 0};
    int cached = 0;

    return memberof_call_foreach_dn(pb, group_sdn, config, attrs,
                                    memberof_test_membership_callback, config, &cached, PR_FALSE);
}

/*
 * memberof_test_membership_callback()
 *
 * A callback function to do the work of memberof_test_membership().
 * Note that this not only tests membership, but updates the memberOf
 * attributes in the entry to be correct.
 */
int
memberof_test_membership_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    Slapi_Attr *attr = 0;
    int total = 0;
    Slapi_Value **member_array = 0;
    Slapi_Value **candidate_array = 0;
    Slapi_Value *entry_dn = 0;
    Slapi_DN *entry_sdn = 0;
    MemberOfConfig *config = (MemberOfConfig *)callback_data;
    Slapi_DN *sdn = slapi_sdn_new();

    entry_sdn = slapi_entry_get_sdn(e);
    entry_dn = slapi_value_new_string(slapi_entry_get_ndn(e));
    if (entry_dn == NULL) {
        goto bail;
    }
    slapi_value_set_flags(entry_dn, SLAPI_ATTR_FLAG_NORMALIZED_CIS);

    /* divide groups into member and non-member lists */
    slapi_entry_attr_find(e, config->memberof_attr, &attr);
    if (attr) {
        slapi_attr_get_numvalues(attr, &total);
        if (total) {
            Slapi_Value *val = 0;
            int hint = 0;
            int c_index = 0;
            int m_index = 0;
            int member_found = 1;
            int outer_index = 0;

            candidate_array =
                (Slapi_Value **)
                    slapi_ch_malloc(sizeof(Slapi_Value *) * total);
            memset(candidate_array, 0, sizeof(Slapi_Value *) * total);
            member_array =
                (Slapi_Value **)
                    slapi_ch_malloc(sizeof(Slapi_Value *) * total);
            memset(member_array, 0, sizeof(Slapi_Value *) * total);

            hint = slapi_attr_first_value(attr, &val);

            while (val) {
                /* test for direct membership */
                if (memberof_is_direct_member(config, val, entry_dn)) {
                    /* it is a member */
                    member_array[m_index] = val;
                    m_index++;
                } else {
                    /* not a member, still a candidate */
                    candidate_array[c_index] = val;
                    c_index++;
                }

                hint = slapi_attr_next_value(attr, hint, &val);
            }

            /* now iterate over members testing for membership
               in candidate groups and moving candidates to members
               when successful, quit when a full iteration adds no
               new members
            */
            while (member_found) {
                member_found = 0;

                /* For each group that this entry is a verified member of, see if
                 * any of the candidate groups are members.  If they are, add them
                 * to the list of verified groups that this entry is a member of.
                 */
                while (outer_index < m_index) {
                    int inner_index = 0;

                    while (inner_index < c_index) {
                        /* Check for a special value in this position
                         * that indicates that the candidate was moved
                         * to the member array. */
                        if ((void *)1 == candidate_array[inner_index]) {
                            /* was moved, skip */
                            inner_index++;
                            continue;
                        }

                        if (memberof_is_direct_member(
                                config,
                                candidate_array[inner_index],
                                member_array[outer_index])) {
                            member_array[m_index] =
                                candidate_array
                                    [inner_index];
                            m_index++;

                            candidate_array[inner_index] =
                                (void *)1;

                            member_found = 1;
                        }

                        inner_index++;
                    }

                    outer_index++;
                }
            }

            /* here we are left only with values to delete
               from the memberof attribute in the candidate list
            */
            outer_index = 0;
            while (outer_index < c_index) {
                /* Check for a special value in this position
                 * that indicates that the candidate was moved
                 * to the member array. */
                if ((void *)1 == candidate_array[outer_index]) {
                    /* item moved, skip */
                    outer_index++;
                    continue;
                }

                slapi_sdn_set_normdn_byref(sdn,
                                           slapi_value_get_string(candidate_array[outer_index]));
                memberof_del_one(0, config, sdn, entry_sdn);

                outer_index++;
            }
            {
                /* crazyness follows:
                 * strict-aliasing doesn't like the required cast
                 * to void for slapi_ch_free so we are made to
                 * juggle to get a normal thing done
                 */
                void *pmember_array = member_array;
                void *pcandidate_array = candidate_array;
                slapi_ch_free(&pcandidate_array);
                slapi_ch_free(&pmember_array);
                candidate_array = 0;
                member_array = 0;
            }
        }
    }

bail:
    slapi_sdn_free(&sdn);
    slapi_value_free(&entry_dn);

    return rc;
}

/*
 * memberof_replace_list()
 *
 * Perform replace the group DN list in the memberof attribute of the list of targets
 *
 */
int
memberof_replace_list(Slapi_PBlock *pb, MemberOfConfig *config, Slapi_DN *group_sdn)
{
    struct slapi_entry *pre_e = NULL;
    struct slapi_entry *post_e = NULL;
    Slapi_Attr *pre_attr = 0;
    Slapi_Attr *post_attr = 0;
    int rc = 0;
    int i = 0;

    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &pre_e);
    slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &post_e);

    for (i = 0; config && config->groupattrs && config->groupattrs[i]; i++) {
        if (pre_e && post_e) {
            slapi_entry_attr_find(pre_e, config->groupattrs[i], &pre_attr);
            slapi_entry_attr_find(post_e, config->groupattrs[i], &post_attr);
        }

        if (pre_attr || post_attr) {
            int pre_total = 0;
            int post_total = 0;
            Slapi_Value **pre_array = 0;
            Slapi_Value **post_array = 0;
            int pre_index = 0;
            int post_index = 0;
            Slapi_DN *sdn = slapi_sdn_new();

            /* create arrays of values */
            if (pre_attr) {
                slapi_attr_get_numvalues(pre_attr, &pre_total);
            }

            if (post_attr) {
                slapi_attr_get_numvalues(post_attr, &post_total);
            }

            /* Stash a plugin global pointer here and have memberof_qsort_compare
             * use it.  We have to do this because we use memberof_qsort_compare
             * as the comparator function for qsort, which requires the function
             * to only take two void* args.  This is thread-safe since we only
             * store and use the pointer while holding the memberOf operation
             * lock. */
            qsortConfig = config;

            if (pre_total) {
                pre_array =
                    (Slapi_Value **)
                        slapi_ch_malloc(sizeof(Slapi_Value *) * pre_total);
                memberof_load_array(pre_array, pre_attr);
                qsort(
                    pre_array,
                    pre_total,
                    sizeof(Slapi_Value *),
                    memberof_qsort_compare);
            }

            if (post_total) {
                post_array =
                    (Slapi_Value **)
                        slapi_ch_malloc(sizeof(Slapi_Value *) * post_total);
                memberof_load_array(post_array, post_attr);
                qsort(
                    post_array,
                    post_total,
                    sizeof(Slapi_Value *),
                    memberof_qsort_compare);
            }

            qsortConfig = 0;


            /*     work through arrays, following these rules:
                in pre, in post, do nothing
                in pre, not in post, delete from entry
                not in pre, in post, add to entry
            */
            while (rc == 0 && (pre_index < pre_total || post_index < post_total)) {
                if (pre_index == pre_total) {
                    /* add the rest of post */
                    slapi_sdn_set_normdn_byref(sdn,
                                               slapi_value_get_string(post_array[post_index]));
                    rc = memberof_add_one(pb, config, group_sdn, sdn);

                    post_index++;
                } else if (post_index == post_total) {
                    /* delete the rest of pre */
                    slapi_sdn_set_normdn_byref(sdn,
                                               slapi_value_get_string(pre_array[pre_index]));
                    rc = memberof_del_one(pb, config, group_sdn, sdn);

                    pre_index++;
                } else {
                    /* decide what to do */
                    int cmp = memberof_compare(
                        config,
                        &(pre_array[pre_index]),
                        &(post_array[post_index]));

                    if (cmp < 0) {
                        /* delete pre array */
                        slapi_sdn_set_normdn_byref(sdn,
                                                   slapi_value_get_string(pre_array[pre_index]));
                        rc = memberof_del_one(pb, config, group_sdn, sdn);

                        pre_index++;
                    } else if (cmp > 0) {
                        /* add post array */
                        slapi_sdn_set_normdn_byref(sdn,
                                                   slapi_value_get_string(post_array[post_index]));
                        rc = memberof_add_one(pb, config, group_sdn, sdn);

                        post_index++;
                    } else {
                        /* do nothing, advance */
                        pre_index++;
                        post_index++;
                    }
                }
            }
            slapi_sdn_free(&sdn);
            slapi_ch_free((void **)&pre_array);
            slapi_ch_free((void **)&post_array);
        }
    }

    return rc;
}

/* memberof_load_array()
 *
 * put attribute values in array structure
 */
void
memberof_load_array(Slapi_Value **array, Slapi_Attr *attr)
{
    Slapi_Value *val = 0;
    int hint = slapi_attr_first_value(attr, &val);

    while (val) {
        *array = val;
        array++;
        hint = slapi_attr_next_value(attr, hint, &val);
    }
}

/* memberof_compare()
 *
 * compare two attr values
 */
int
memberof_compare(MemberOfConfig *config, const void *a, const void *b)
{
    Slapi_Value *val1;
    Slapi_Value *val2;

    if (a == NULL && b != NULL) {
        return 1;
    } else if (a != NULL && b == NULL) {
        return -1;
    } else if (a == NULL && b == NULL) {
        return 0;
    }
    val1 = *((Slapi_Value **)a);
    val2 = *((Slapi_Value **)b);

    /* We only need to provide a Slapi_Attr here for it's syntax.  We
     * already validated all grouping attributes to use the Distinguished
     * Name syntax, so we can safely just use the first attr. */
    return slapi_attr_value_cmp_ext(config->group_slapiattrs[0], val1, val2);
}

/* memberof_qsort_compare()
 *
 * This is a version of memberof_compare that uses a plugin
 * global copy of the config.  We'd prefer to pass in a copy
 * of config that is local to the running thread, but we can't
 * do this since qsort is using us as a comparator function.
 * We should only use this function when using qsort, and only
 * when the memberOf lock is acquired.
 */
int
memberof_qsort_compare(const void *a, const void *b)
{
    Slapi_Value *val1 = *((Slapi_Value **)a);
    Slapi_Value *val2 = *((Slapi_Value **)b);

    /* We only need to provide a Slapi_Attr here for it's syntax.  We
     * already validated all grouping attributes to use the Distinguished
     * Name syntax, so we can safely just use the first attr. */
    return slapi_attr_value_cmp_ext(qsortConfig->group_slapiattrs[0],
                                    val1, val2);
}

void
memberof_fixup_task_thread(void *arg)
{
    MemberOfConfig configCopy = {0};
    Slapi_Task *task = (Slapi_Task *)arg;
    task_data *td = NULL;
    int rc = 0;
    Slapi_PBlock *fixup_pb = NULL;

    if (!task) {
        return; /* no task */
    }

    PR_Lock(fixup_lock);
    fixup_running = PR_TRUE;
    fixup_progress_count = 0;
    fixup_progress_elapsed = slapi_current_rel_time_t();
    fixup_start_time = slapi_current_rel_time_t();
    PR_Unlock(fixup_lock);

    slapi_task_inc_refcount(task);

    /* Fetch our task data from the task */
    td = (task_data *)slapi_task_get_data(task);

    /* set bind DN in the thread data */
    slapi_td_set_dn(slapi_ch_strdup(td->bind_dn));

    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "Memberof task starts (arg: %s) ...",
                          td->filter_str);
    slapi_log_err(SLAPI_LOG_INFO, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "memberof_fixup_task_thread - Memberof task starts (filter: \"%s\") ...\n",
                  td->filter_str);

    /* We need to get the config lock first.  Trying to get the
     * config lock after we already hold the op lock can cause
     * a deadlock. */
    memberof_rlock_config();
    /* copy config so it doesn't change out from under us */
    memberof_copy_config(&configCopy, memberof_get_config());
    memberof_unlock_config();

    /* Mark this as a task operation */
    configCopy.fixup_task = 1;
    configCopy.task = task;

    if (usetxn) {
        Slapi_DN *sdn = slapi_sdn_new_dn_byref(td->dn);
        Slapi_Backend *be = slapi_be_select_exact(sdn);
        slapi_sdn_free(&sdn);
        if (be) {
            fixup_pb = slapi_pblock_new();
            slapi_pblock_set(fixup_pb, SLAPI_BACKEND, be);
            rc = slapi_back_transaction_begin(fixup_pb);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                              "memberof_fixup_task_thread - Failed to start transaction\n");
                goto done;
            }
        } else {
            slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                          "memberof_fixup_task_thread - Failed to get be backend from (%s)\n",
                          td->dn);
            slapi_task_log_notice(task, "Memberof task - Failed to get be backend from (%s)",
                                  td->dn);
            rc = -1;
            goto done;
        }
    }

    /* do real work */
    rc = memberof_fix_memberof(&configCopy, task, td);

done:
    if (usetxn && fixup_pb) {
        if (rc) { /* failed */
            slapi_back_transaction_abort(fixup_pb);
        } else {
            slapi_back_transaction_commit(fixup_pb);
        }
        slapi_pblock_destroy(fixup_pb);
    }
    memberof_free_config(&configCopy);

    slapi_task_log_notice(task, "Memberof task finished (processed %d entries in %ld seconds)",
                          fixup_progress_count, slapi_current_rel_time_t() - fixup_start_time);
    slapi_task_log_status(task, "Memberof task finished (processed %d entries in %ld seconds)",
                          fixup_progress_count, slapi_current_rel_time_t() - fixup_start_time);
    slapi_task_inc_progress(task);

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rc);
    slapi_task_dec_refcount(task);

    PR_Lock(fixup_lock);
    fixup_running = PR_FALSE;
    PR_Unlock(fixup_lock);

    slapi_log_err(SLAPI_LOG_INFO, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "memberof_fixup_task_thread - Memberof task finished (processed %d entries in %ld seconds)\n",
                  fixup_progress_count, slapi_current_rel_time_t() - fixup_start_time);
}

int
memberof_task_add(Slapi_PBlock *pb,
                  Slapi_Entry *e,
                  Slapi_Entry *eAfter __attribute__((unused)),
                  int *returncode,
                  char *returntext __attribute__((unused)),
                  void *arg)
{
    PRThread *thread = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    task_data *mytaskdata = NULL;
    Slapi_Task *task = NULL;
    char *bind_dn;
    const char *filter;
    const char *dn = 0;

    *returncode = LDAP_SUCCESS;

    PR_Lock(fixup_lock);
    if (fixup_running) {
        PR_Unlock(fixup_lock);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                "memberof_task_add - there is already a fixup task running\n");
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    PR_Unlock(fixup_lock);

    /* get arg(s) */
    if ((dn = slapi_entry_attr_get_ref(e, "basedn")) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    if ((filter = slapi_fetch_attr(e, "filter", "(|(objectclass=inetuser)(objectclass=inetadmin)(objectclass=nsmemberof))")) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* setup our task data */
    slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &bind_dn);
    mytaskdata = (task_data *)slapi_ch_malloc(sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mytaskdata->dn = slapi_ch_strdup(dn);
    mytaskdata->filter_str = slapi_ch_strdup(filter);
    mytaskdata->bind_dn = slapi_ch_strdup(bind_dn);

    /* allocate new task now */
    task = slapi_plugin_new_task(slapi_entry_get_ndn(e), arg);

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, memberof_task_destructor);

    /* Stash a pointer to our data in the task */
    slapi_task_set_data(task, mytaskdata);

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, memberof_fixup_task_thread,
                             (void *)task, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "unable to create task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_task_finish(task, *returncode);
        rv = SLAPI_DSE_CALLBACK_ERROR;
    } else {
        rv = SLAPI_DSE_CALLBACK_OK;
    }

out:
    return rv;
}

void
memberof_task_destructor(Slapi_Task *task)
{
    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "memberof_task_destructor -->\n");
    if (task) {
        task_data *mydata = (task_data *)slapi_task_get_data(task);
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
        if (mydata) {
            slapi_ch_free_string(&mydata->dn);
            slapi_ch_free_string(&mydata->bind_dn);
            slapi_ch_free_string(&mydata->filter_str);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **)&mydata);
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                  "memberof_task_destructor <--\n");
}

int
memberof_fix_memberof(MemberOfConfig *config, Slapi_Task *task, task_data *td)
{
    int rc = 0;
    Slapi_PBlock *search_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(search_pb, td->dn,
                                 LDAP_SCOPE_SUBTREE, td->filter_str, 0, 0,
                                 0, 0,
                                 memberof_get_plugin_id(),
                                 0);

    rc = slapi_search_internal_callback_pb(search_pb,
                                           config,
                                           0, memberof_fix_memberof_callback,
                                           0);
    if (rc) {
        char *errmsg;
        int result;

        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
        errmsg = ldap_err2string(result);
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_fix_memberof - Failed (%s)\n", errmsg);
        slapi_task_log_notice(task, "Memberof task failed (%s)", errmsg);
    }

    slapi_pblock_destroy(search_pb);

    return rc;
}

static memberof_cached_value *
ancestors_cache_lookup(MemberOfConfig *config, const char *ndn)
{
    memberof_cached_value *e;
#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    long int start;
    struct timespec tsnow;
#endif

    cache_stat.total_lookup++;

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        start = 0;
    } else {
        start = tsnow.tv_nsec;
    }
#endif

    e = (memberof_cached_value *) PL_HashTableLookupConst(config->ancestors_cache, (const void *) ndn);

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (start) {
        if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0) {
            cache_stat.cumul_duration_lookup += (tsnow.tv_nsec - start);
        }
    }
#endif

    if (e)
        cache_stat.successfull_lookup++;
    return e;
}
static PRBool
ancestors_cache_remove(MemberOfConfig *config, const char *ndn)
{
    PRBool rc;
#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    long int start;
    struct timespec tsnow;
#endif

    cache_stat.total_remove++;

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        start = 0;
    } else {
        start = tsnow.tv_nsec;
    }
#endif


    rc = PL_HashTableRemove(config->ancestors_cache, (const void *)ndn);

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (start) {
        if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0) {
            cache_stat.cumul_duration_remove += (tsnow.tv_nsec - start);
        }
    }
#endif
    return rc;
}

static PLHashEntry *
ancestors_cache_add(MemberOfConfig *config, const void *key, void *value)
{
    PLHashEntry *e;
#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    long int start;
    struct timespec tsnow;
#endif
    cache_stat.total_add++;

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (clock_gettime(CLOCK_REALTIME, &tsnow) != 0) {
        start = 0;
    } else {
        start = tsnow.tv_nsec;
    }
#endif

    e = PL_HashTableAdd(config->ancestors_cache, key, value);

#if defined(DEBUG) && defined(HAVE_CLOCK_GETTIME)
    if (start) {
        if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0) {
            cache_stat.cumul_duration_add += (tsnow.tv_nsec - start);
        }
    }
#endif
    return e;
}

/* memberof_fix_memberof_callback()
 * Add initial and/or fix up broken group list in entry
 *
 * 1. Remove all present memberOf values
 * 2. Add direct group membership memberOf values
 * 3. Add indirect group membership memberOf values
 */
int
memberof_fix_memberof_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    Slapi_DN *sdn = slapi_entry_get_sdn(e);
    MemberOfConfig *config = (MemberOfConfig *)callback_data;
    memberof_del_dn_data del_data = {0, config->memberof_attr};
    Slapi_ValueSet *groups = 0;
    const char *ndn;
    char *dn_copy;

    /*
     * If the server is ordered to shutdown, stop the fixup and return an error.
     */
    if (slapi_is_shutting_down()) {
        rc = -1;
        goto bail;
    }

    /* Check if the entry has not already been fixed */
    ndn = slapi_sdn_get_ndn(sdn);
    if (ndn && config->fixup_cache && PL_HashTableLookupConst(config->fixup_cache, (void *)ndn)) {
        slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_fix_memberof_callback - "
                "Entry %s already fixed up\n", ndn);
        goto bail;
    }

    /* get a list of all of the groups this user belongs to */
    groups = memberof_get_groups(config, sdn);

    if (config->group_filter) {
        if (slapi_filter_test_simple(e, config->group_filter)) {
            memberof_cached_value *ht_grp;

            /* This entry is not a group
             * if (likely) we cached its ancestor it is useless
             * so free this memory
             */
#if MEMBEROF_CACHE_DEBUG
            slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                    "memberof_fix_memberof_callback: This is NOT a group %s\n", ndn);
#endif
            ht_grp = ancestors_cache_lookup(config, (const void *)ndn);
            if (ht_grp) {
                if (ancestors_cache_remove(config, (const void *)ndn)) {
                    slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                            "memberof_fix_memberof_callback - free cached values for %s\n", ndn);
                    ancestor_hashtable_entry_free(ht_grp);
                    slapi_ch_free((void **)&ht_grp);
                } else {
                    slapi_log_err(SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM,
                            "memberof_fix_memberof_callback - Fail to remove that leaf node %s\n", ndn);
                }
            } else {
                /* This is quite unexpected, after a call to memberof_get_groups
                 * ndn ancestors should be in the cache
                 */
                slapi_log_err(SLAPI_LOG_PLUGIN, MEMBEROF_PLUGIN_SUBSYSTEM,
                        "memberof_fix_memberof_callback - Weird, %s is not in the cache\n", ndn);
            }
        }
    }
    /* If we found some groups, replace the existing memberOf attribute
     * with the found values.  */
    if (groups && slapi_valueset_count(groups)) {
        Slapi_Value *val = 0;
        Slapi_Mod *smod;
        LDAPMod **mods = (LDAPMod **)slapi_ch_malloc(2 * sizeof(LDAPMod *));
        int hint = 0;

        smod = slapi_mod_new();
        slapi_mod_init(smod, 0);
        slapi_mod_set_operation(smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
        slapi_mod_set_type(smod, config->memberof_attr);

        /* Loop through all of our values and add them to smod */
        hint = slapi_valueset_first_value(groups, &val);
        while (val) {
            /* this makes a copy of the berval */
            slapi_mod_add_value(smod, slapi_value_get_berval(val));
            hint = slapi_valueset_next_value(groups, hint, &val);
        }

        mods[0] = slapi_mod_get_ldapmod_passout(smod);
        mods[1] = 0;

        rc = memberof_add_memberof_attr(mods, slapi_sdn_get_dn(sdn), config->auto_add_oc);

        ldap_mods_free(mods, 1);
        slapi_mod_free(&smod);
    } else {
        /* No groups were found, so remove the memberOf attribute
         * from this entry. */
        memberof_del_dn_type_callback(e, &del_data);
    }

    slapi_valueset_free(groups);

    /* records that this entry has been fixed up */
    if (config->fixup_cache) {
        dn_copy = slapi_ch_strdup(ndn);
        if (PL_HashTableAdd(config->fixup_cache, dn_copy, dn_copy) == NULL) {
            slapi_log_err(SLAPI_LOG_FATAL, MEMBEROF_PLUGIN_SUBSYSTEM, "memberof_fix_memberof_callback - "
                          "failed to add dn (%s) in the fixup hashtable; NSPR error - %d\n",
                          dn_copy, PR_GetError());
            slapi_ch_free((void **)&dn_copy);
            /* let consider this as not a fatal error, it just skip an optimization */
        }
    }

    if (config->task) {
        fixup_progress_count++;
        if (fixup_progress_count % FIXUP_PROGRESS_LIMIT == 0 ) {
            slapi_task_log_notice(config->task,
                    "Processed %d entries in %ld seconds (+%ld seconds)",
                    fixup_progress_count,
                    slapi_current_rel_time_t() - fixup_start_time,
                    slapi_current_rel_time_t() - fixup_progress_elapsed);
            slapi_task_log_status(config->task,
                    "Processed %d entries in %ld seconds (+%ld seconds)",
                    fixup_progress_count,
                    slapi_current_rel_time_t() - fixup_start_time,
                    slapi_current_rel_time_t() - fixup_progress_elapsed);
            slapi_task_inc_progress(config->task);
            fixup_progress_elapsed = slapi_current_rel_time_t();
        }
    }

bail:
    return rc;
}

/*
 * Add the "memberof" attribute to the entry.  If we get an objectclass violation,
 * check if we are auto adding an objectclass.  IF so, add the oc, and try the
 * operation one more time.
 */
static int
memberof_add_memberof_attr(LDAPMod **mods, const char *dn, char *add_oc)
{
    Slapi_PBlock *mod_pb = NULL;
    int added_oc = 0;
    int rc = 0;

    while (1) {
        mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb(
            mod_pb, dn, mods, 0, 0,
            memberof_get_plugin_id(), SLAPI_OP_FLAG_BYPASS_REFERRALS);
        slapi_modify_internal_pb(mod_pb);

        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc == LDAP_OBJECT_CLASS_VIOLATION) {
            if (!add_oc || added_oc) {
                /*
                 * We aren't auto adding an objectclass, or we already
                 * added the objectclass, and we are still failing.
                 */
                break;
            }
            rc = memberof_add_objectclass(add_oc, dn);
            slapi_log_err(SLAPI_LOG_WARNING, MEMBEROF_PLUGIN_SUBSYSTEM,
                    "Entry %s - schema violation caught - repair operation %s\n",
                    dn ? dn : "unknown",
                    rc ? "failed" : "succeeded");
            if (rc) {
                /* Failed to add objectclass */
                rc = LDAP_OBJECT_CLASS_VIOLATION;
                break;
            }
            added_oc = 1;
            slapi_pblock_destroy(mod_pb);
        } else if (rc) {
            /* Some other fatal error */
            break;
        } else {
            /* success */
            break;
        }
    }
    slapi_pblock_destroy(mod_pb);

    return rc;
}

/*
 * Add the "auto add" objectclass to an entry
 */
static int
memberof_add_objectclass(char *auto_add_oc, const char *dn)
{
    Slapi_PBlock *mod_pb = NULL;
    LDAPMod mod;
    LDAPMod *mods[2];
    char *val[2];
    int rc = 0;

    mod_pb = slapi_pblock_new();
    mods[0] = &mod;
    mods[1] = 0;
    val[0] = auto_add_oc;
    val[1] = 0;

    mod.mod_op = LDAP_MOD_ADD;
    mod.mod_type = "objectclass";
    mod.mod_values = val;

    slapi_modify_internal_set_pb(
        mod_pb, dn, mods, 0, 0,
        memberof_get_plugin_id(), SLAPI_OP_FLAG_BYPASS_REFERRALS);
    slapi_modify_internal_pb(mod_pb);

    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, MEMBEROF_PLUGIN_SUBSYSTEM,
                      "memberof_add_objectclass - Failed to add objectclass (%s) to entry (%s)\n",
                      auto_add_oc, dn);
    }
    slapi_pblock_destroy(mod_pb);

    return rc;
}

int
memberof_use_txn()
{
    return usetxn;
}
