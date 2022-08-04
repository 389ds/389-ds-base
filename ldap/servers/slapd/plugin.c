/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* plugin.c - routines for setting up and calling plugins */

#include <stddef.h>
#include <stdio.h>
#include <plhash.h>
#include "slap.h"

/* this defines are used for plugin configuration */
#define LOCAL_DATA "local data"
#define REMOTE_DATA "remote data"
#define ALL_DATA "*"
#define ROOT_BIND "directory manager"
#define ANONYMOUS_BIND "anonymous"

/* dependency checking flags */
#define CHECK_ALL 0
#define CHECK_TYPE 1

/* plugin removal flags */
#define PLUGIN_NOT_FOUND 0
#define PLUGIN_REMOVED 1
#define PLUGIN_BUSY 2

static char *critical_plugins[] = {"cn=ldbm database,cn=plugins,cn=config",
                                   "cn=ACL Plugin,cn=plugins,cn=config",
                                   "cn=ACL preoperation,cn=plugins,cn=config",
                                   "cn=chaining database,cn=plugins,cn=config",
                                   "cn=Multisupplier Replication Plugin,cn=plugins,cn=config",
                                   NULL};

/* Forward Declarations */
static int plugin_call_list(struct slapdplugin *list, int operation, Slapi_PBlock *pb);
static int plugin_call_one(struct slapdplugin *list, int operation, Slapi_PBlock *pb);
static int plugin_call_func(struct slapdplugin *list, int operation, Slapi_PBlock *pb, int call_one);

static PRBool plugin_invoke_plugin_pb(struct slapdplugin *plugin, int operation, Slapi_PBlock *pb);
static PRBool plugin_matches_operation(Slapi_DN *target_spec, PluginTargetData *ptd, PRBool bindop, PRBool isroot, PRBool islocal, ber_tag_t method);

static void plugin_config_init(struct pluginconfig *config);
static void plugin_config_cleanup(struct pluginconfig *config);
static int plugin_config_set_action(int *action, char *value);
static struct pluginconfig *plugin_get_config(struct slapdplugin *plugin);
static void default_plugin_init(void);
static void ptd_init(PluginTargetData *ptd);
static void ptd_cleanup(PluginTargetData *ptd);
static void ptd_add_subtree(PluginTargetData *ptd, Slapi_DN *subtree);
static void ptd_set_special_data(PluginTargetData *ptd, int type);
static Slapi_DN *ptd_get_first_subtree(const PluginTargetData *ptd, int *cookie);
static Slapi_DN *ptd_get_next_subtree(const PluginTargetData *ptd, int *cookie);
static PRBool ptd_is_special_data_set(const PluginTargetData *ptd, int type);
int ptd_get_subtree_count(const PluginTargetData *ptd);
static void plugin_set_global(PluginTargetData *ptd);
static PRBool plugin_is_global(const PluginTargetData *ptd);
static void plugin_set_default_access(struct pluginconfig *config);
static int plugin_delete_check_dependency(struct slapdplugin *plugin_entry, int flag, char *returntext);
static char *plugin_get_type_str(int type);
static void plugin_cleanup_list(void);
static int plugin_remove_plugins(struct slapdplugin *plugin_entry, char *plugin_type);
static void plugin_remove_from_shutdown(struct slapdplugin *plugin_entry);
static void plugin_free(struct slapdplugin *plugin);

static PLHashTable *global_plugin_dns = NULL;

/* The global plugin list is indexed by the PLUGIN_LIST_* constants defined in slap.h */
static struct slapdplugin *global_plugin_list[PLUGIN_LIST_GLOBAL_MAX];

/* plugin structure used to configure internal operation issued by the core server */
static int global_server_plg_initialised = 0;
struct slapdplugin global_server_plg;

/* plugin structure used to configure internal operation issued by the core server */
static int global_server_plg_id_initialised = 0;
struct slapi_componentid global_server_id_plg;

/* plugin structure used to configure operations issued by the old plugins that
   do not pass their identity in the operation */
static struct slapdplugin global_default_plg;

/* Enable/disable plugin callbacks for clean startup */
static int global_plugin_callbacks_enabled = 0;

static Slapi_RWLock *global_rwlock = NULL;

void
global_plugin_init()
{
    if ((global_rwlock = slapi_new_rwlock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "global_plugin_init",
                      "Failed to create global plugin rwlock.\n");
        exit(1);
    }
}

static void
add_plugin_to_list(struct slapdplugin **list, struct slapdplugin *plugin)
{
    struct slapdplugin **tmp;
    struct slapdplugin *last = NULL;
    int plugin_added = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "add_plugin_to_list", "Adding %s \n", plugin->plg_name);

    /* Insert the plugin into list based off of precedence. */
    for (tmp = list; *tmp; tmp = &(*tmp)->plg_next) {
        if (plugin->plg_precedence < (*tmp)->plg_precedence) {
            if (last) {
                /* Insert item between last and tmp. */
                plugin->plg_next = *tmp;
                last->plg_next = plugin;
            } else {
                /* Add as the first list item. */
                plugin->plg_next = *tmp;
                *list = plugin;
            }

            plugin_added = 1;

            /* We've added the plug-in to the
              * list, so bail from the loop. */
            break;
        }

        /* Save a pointer to this plugin so we can
         * refer to it on the next loop iteration. */
        last = *tmp;
    }

    /* If we didn't add the plug-in to the list yet,
     * it needs to be added to the end of the list. */
    if (!plugin_added) {
        *tmp = plugin;
    }
}

struct slapdplugin *
get_plugin_list(int plugin_list_index)
{
    return global_plugin_list[plugin_list_index];
}

/*
 * As the plugin configuration information is read an array of
 * entries is built which reflect the plugins.  The entries
 * are added after the syntax plugins are started so that the
 * nodes in the attribute tree are initialised correctly.
 */
typedef struct entry_and_plugin
{
    Slapi_Entry *e;
    struct slapdplugin *plugin;
    struct entry_and_plugin *next;
} entry_and_plugin_t;

static entry_and_plugin_t *dep_plugin_entries = NULL; /* for dependencies */

static int
plugin_is_critical(Slapi_Entry *plugin_entry)
{
    char *plugin_dn = NULL;
    int i;

    plugin_dn = slapi_entry_get_ndn(plugin_entry);

    for (i = 0; critical_plugins[i]; i++) {
        if (strcasecmp(plugin_dn, critical_plugins[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static void
new_plugin_entry(entry_and_plugin_t **ep, Slapi_Entry *e, struct slapdplugin *plugin)
{
    entry_and_plugin_t *oldep = 0;
    entry_and_plugin_t *iterep = *ep;

    entry_and_plugin_t *newep = (entry_and_plugin_t *)slapi_ch_calloc(1, sizeof(entry_and_plugin_t));
    newep->e = e;
    newep->plugin = plugin;

    while (iterep) {
        oldep = iterep;
        iterep = iterep->next;
    }

    newep->next = 0;

    if (oldep) {
        oldep->next = newep;
    } else {
        *ep = newep;
    }
}

static void
add_plugin_entry_dn(const Slapi_DN *plugin_dn)
{
    if (!global_plugin_dns) {
        global_plugin_dns = PL_NewHashTable(20, PL_HashString,
                                            PL_CompareStrings,
                                            PL_CompareValues, 0, 0);
    }
    PL_HashTableAdd(global_plugin_dns,
                    slapi_sdn_get_ndn(plugin_dn),
                    (void *)plugin_dn);
}

#define SLAPI_PLUGIN_NONE_IF_NULL(s) ((s) == NULL ? "none" : (s))

/*
 * Allows a plugin to register a plugin.
 * This was added so that 'object' plugins could register all
 * the plugin interfaces that it supports.
 */
int
slapi_register_plugin(
    const char *plugintype,
    int enabled,
    const char *initsymbol,
    slapi_plugin_init_fnptr initfunc,
    const char *name,
    char **argv,
    void *group_identity)
{
    return slapi_register_plugin_ext(plugintype, enabled, initsymbol,
                                     initfunc, name, argv, group_identity, PLUGIN_DEFAULT_PRECEDENCE);
}

int
slapi_register_plugin_ext(
    const char *plugintype,
    int enabled,
    const char *initsymbol,
    slapi_plugin_init_fnptr initfunc,
    const char *name,
    char **argv,
    void *group_identity,
    int precedence)
{
    Slapi_Entry *e = NULL;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    char *dn = slapi_ch_smprintf("cn=%s,%s", name, PLUGIN_BASE_DN);
    Slapi_DN *sdn = slapi_sdn_new_normdn_passin(dn);
    int found_precedence;
    int ii = 0;
    int rc = 0;

    e = slapi_entry_alloc();
    /* this function consumes dn */
    slapi_entry_init_ext(e, sdn, NULL);
    slapi_sdn_free(&sdn);

    slapi_entry_attr_set_charptr(e, "cn", name);
    /* Need a valid objectClass! No plugin OC so just use extensible :( */
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", "extensibleObject");
    slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_TYPE, plugintype);
    if (!enabled) {
        slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_ENABLED, "off");
    }

    slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_INITFN, initsymbol);

    /* If the plugin belong to a group, get the precedence from the group */
    found_precedence = precedence;
    if ((found_precedence == PLUGIN_DEFAULT_PRECEDENCE) && group_identity) {
        struct slapi_componentid *cid = (struct slapi_componentid *)group_identity;
        if (cid->sci_plugin && (cid->sci_plugin->plg_precedence != PLUGIN_DEFAULT_PRECEDENCE)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "slapi_register_plugin_ext",
                          "Plugin precedence (%s) reset to group precedence (%s): %d \n",
                          name ? name : "",
                          cid->sci_plugin->plg_name ? cid->sci_plugin->plg_name : "",
                          cid->sci_plugin->plg_precedence);
            found_precedence = cid->sci_plugin->plg_precedence;
        }
    }
    slapi_entry_attr_set_int(e, ATTR_PLUGIN_PRECEDENCE, found_precedence);

    for (ii = 0; argv && argv[ii]; ++ii) {
        char argname[64];
        PR_snprintf(argname, sizeof(argname), "%s%d", ATTR_PLUGIN_ARG, ii);
        slapi_entry_attr_set_charptr(e, argname, argv[ii]);
    }

    /* plugin_setup copies the given entry */
    rc = plugin_setup(e, group_identity, initfunc, 0, returntext);
    slapi_entry_free(e);

    return rc;
}

int
plugin_call_plugins(Slapi_PBlock *pb, int whichfunction)
{
    int plugin_list_number = -1;
    int rc = 0;
    int do_op = global_plugin_callbacks_enabled;

    if (pb == NULL) {
        return (0);
    }

    switch (whichfunction) {
    case SLAPI_PLUGIN_PRE_BIND_FN:
    case SLAPI_PLUGIN_PRE_UNBIND_FN:
    case SLAPI_PLUGIN_PRE_SEARCH_FN:
    case SLAPI_PLUGIN_PRE_COMPARE_FN:
    case SLAPI_PLUGIN_PRE_MODIFY_FN:
    case SLAPI_PLUGIN_PRE_MODRDN_FN:
    case SLAPI_PLUGIN_PRE_ADD_FN:
    case SLAPI_PLUGIN_PRE_DELETE_FN:
    case SLAPI_PLUGIN_PRE_ABANDON_FN:
    case SLAPI_PLUGIN_PRE_ENTRY_FN:
    case SLAPI_PLUGIN_PRE_REFERRAL_FN:
    case SLAPI_PLUGIN_PRE_RESULT_FN:
        plugin_list_number = PLUGIN_LIST_PREOPERATION;
        break;
    case SLAPI_PLUGIN_POST_BIND_FN:
    case SLAPI_PLUGIN_POST_UNBIND_FN:
    case SLAPI_PLUGIN_POST_SEARCH_FN:
    case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
    case SLAPI_PLUGIN_POST_COMPARE_FN:
    case SLAPI_PLUGIN_POST_MODIFY_FN:
    case SLAPI_PLUGIN_POST_MODRDN_FN:
    case SLAPI_PLUGIN_POST_ADD_FN:
    case SLAPI_PLUGIN_POST_DELETE_FN:
    case SLAPI_PLUGIN_POST_ABANDON_FN:
    case SLAPI_PLUGIN_POST_ENTRY_FN:
    case SLAPI_PLUGIN_POST_REFERRAL_FN:
    case SLAPI_PLUGIN_POST_RESULT_FN:
        plugin_list_number = PLUGIN_LIST_POSTOPERATION;
        break;
    case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
    case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
    case SLAPI_PLUGIN_BE_PRE_ADD_FN:
    case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
    case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
        plugin_list_number = PLUGIN_LIST_BEPREOPERATION;
        do_op = 1; /* always allow backend callbacks (even during startup) */
        break;
    case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
    case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
    case SLAPI_PLUGIN_BE_POST_ADD_FN:
    case SLAPI_PLUGIN_BE_POST_DELETE_FN:
    case SLAPI_PLUGIN_BE_POST_OPEN_FN:
    case SLAPI_PLUGIN_BE_POST_EXPORT_FN:
    case SLAPI_PLUGIN_BE_POST_IMPORT_FN:
        plugin_list_number = PLUGIN_LIST_BEPOSTOPERATION;
        do_op = 1; /* always allow backend callbacks (even during startup) */
        break;
    case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
    case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
    case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
    case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
    case SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN:
        plugin_list_number = PLUGIN_LIST_INTERNAL_PREOPERATION;
        break;
    case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
    case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
    case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
    case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
        plugin_list_number = PLUGIN_LIST_INTERNAL_POSTOPERATION;
        break;
    case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
    case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
    case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
    case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN:
        plugin_list_number = PLUGIN_LIST_BETXNPREOPERATION;
        do_op = 1; /* always allow backend callbacks (even during startup) */
        break;
    case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
    case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
    case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
    case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
        plugin_list_number = PLUGIN_LIST_BETXNPOSTOPERATION;
        do_op = 1; /* always allow backend callbacks (even during startup) */
        break;
    case SLAPI_PLUGIN_PRE_EXTOP_FN:
        plugin_list_number = PLUGIN_LIST_PREEXTENDED_OPERATION;
        break;
    case SLAPI_PLUGIN_POST_EXTOP_FN:
        plugin_list_number = PLUGIN_LIST_POSTEXTENDED_OPERATION;
        break;
    }

    if (plugin_list_number != -1 && do_op) {
        /* We stash the pblock plugin pointer to preserve the callers context */
        struct slapdplugin *p;
        int locked = 0;

        locked = slapi_td_get_plugin_locked();
        if (!locked) {
            slapi_rwlock_rdlock(global_rwlock);
        }

        slapi_pblock_get(pb, SLAPI_PLUGIN, &p);
        /* Call the operation on the Global Plugins */
        rc = plugin_call_list(global_plugin_list[plugin_list_number], whichfunction, pb);
        slapi_pblock_set(pb, SLAPI_PLUGIN, p);

        if (!locked) {
            slapi_rwlock_unlock(global_rwlock);
        }
    } else {
        /* Programmer error! or the callback is denied during startup */
    }
    return rc;
}


void
plugin_call_entrystore_plugins(char **entrystr, uint *size)
{
    struct slapdplugin *p;
    for (p = global_plugin_list[PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE];
         p != NULL; p = p->plg_next) {
        if (p->plg_entrystorefunc)
            (*p->plg_entrystorefunc)(entrystr, size);
    }
}

void
plugin_call_entryfetch_plugins(char **entrystr, uint *size)
{
    struct slapdplugin *p;
    for (p = global_plugin_list[PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE];
         p != NULL; p = p->plg_next) {
        if (p->plg_entryfetchfunc)
            (*p->plg_entryfetchfunc)(entrystr, size);
    }
}

/*
 * plugin_determine_exop_plugins
 *
 * A call to this function will determine the correct plugin that is required
 * based on the extended operation ID.
 *
 * extoid : The extended operation oid as a *char
 * plugin: A pointer to a struct slapdplugin *. IE &*p This will be set by the function.
 * return: SLAPI_PLUGIN_EXTENDED_NOT_HANDLED if no plugin. Otherwise, the SLAPI_PLUGIN_* type that the plugin is.
 */
int
plugin_determine_exop_plugins(const char *oid, struct slapdplugin **plugin)
{
    struct slapdplugin *p = NULL;
    int list_type = 0;
    int i = 0;
    int l = 0;
    int rc = SLAPI_PLUGIN_EXTENDED_NOT_HANDLED;

    int list_types[] = {PLUGIN_LIST_EXTENDED_OPERATION, PLUGIN_LIST_BE_TXN_EXTENDED_OPERATION};

    for (l = 0; l < 2; ++l) {
        list_type = list_types[l];

        for (p = global_plugin_list[list_type]; p != NULL; p = p->plg_next) {
            if (p->plg_exhandler != NULL && p->plg_exoids != NULL) {
                for (i = 0; p->plg_exoids[i] != NULL; i++) {
                    if (strcasecmp(oid, p->plg_exoids[i]) == 0) {
                        *plugin = p;
                        rc = p->plg_type;
                        /* break; */
                        /* WB - 48870, return early so that we can get the plugin
                         * that is set earlier in the list. This prevents the
                         * need to change add_plugin_to_list, which may have
                         * side effects.
                         */
                        return rc;
                    }
                }
            }
        }
    }

    return rc;
}

/*
 * call extended operation plugins
 *
 * return SLAPI_PLUGIN_EXTENDED_SENT_RESULT if one of the extended operation
 *    plugins sent a result.
 * return SLAPI_PLUGIN_EXTENDED_NOT_HANDLED if no extended operation plugins
 *    handled the operation.
 * otherwise, return an LDAP error code (possibly a merge of the errors
 *    returned by the plugins we called).
 */
int
plugin_call_exop_plugins(Slapi_PBlock *pb, struct slapdplugin *p)
{
    int rc;
    slapi_pblock_set(pb, SLAPI_PLUGIN, p);
    set_db_default_result_handlers(pb);
    rc = (*p->plg_exhandler)(pb);
    return (rc);
}


/*
 * Attempt to convert the extended operation 'oid' to a string by
 * examining the registered plugins.  Returns NULL if no plugin is
 * registered for this OID.
 *
 * Our first choice is to use an OID-specific name that has been
 * registered by a plugin via the SLAPI_PLUGIN_EXT_OP_NAMELIST pblock setting.
 * Our second choice is to use the plugin's ID (short name).
 * Our third choice is to use the plugin's RDN (under cn=config).
 */
const char *
plugin_extended_op_oid2string(const char *oid)
{
    struct slapdplugin *p;
    int j = 0;
    int rc = 0;
    const char *rval = NULL;

    rc = plugin_determine_exop_plugins(oid, &p);
    if ((rc == SLAPI_PLUGIN_EXTENDEDOP || rc == SLAPI_PLUGIN_BETXNEXTENDEDOP) && p != NULL) {
        /* We have the plugin, p set, so lets fill it in */
        if (NULL != p->plg_exnames) {
            for (j = 0; p->plg_exnames[j] != NULL; ++j) {
                /* I'm not sure what this does ....*/
                ;
            }
            rval = p->plg_exnames[j]; /* OID-related name */
        }

        if (NULL == rval) {
            if (NULL != p->plg_desc.spd_id) {
                rval = p->plg_desc.spd_id; /* short name */
            } else {
                rval = p->plg_name; /* RDN */
            }
        }
    }

    return (rval);
}


Slapi_Backend *
plugin_extended_op_getbackend(Slapi_PBlock *pb, struct slapdplugin *p)
{
    // struct slapdplugin  *p;
    int rc;
    /* This could be an error type, but for now we expect the caller to check
     * that it's not null
     */
    Slapi_Backend *result = NULL;


    rc = (*p->plg_be_exhandler)(pb, &result);
    if (rc != LDAP_SUCCESS) {
        /* Do we need to do anything? Or it is the parents job? */
        result = NULL;
    }
    return (result);
}

static int
plugin_cmp_plugins(struct slapdplugin *p1, struct slapdplugin *p2)
{
    int rc = 0;

    if (p1->plg_dn) {
        if (p2->plg_dn && strcasecmp(p1->plg_dn, p2->plg_dn) == 0) {
            rc = 1;
        } else if (p2->plg_id && strcasecmp(p1->plg_dn, p2->plg_id) == 0) {
            rc = 1;
        }
    } else if (p1->plg_id) {
        if (p2->plg_id && strcasecmp(p1->plg_id, p2->plg_id) == 0) {
            rc = 1;
        } else if (p2->plg_dn && strcasecmp(p2->plg_dn, p1->plg_id) == 0) {
            rc = 1;
        }
    }

    return rc;
}

/*
 * kexcoff: return the slapdplugin structure
 */
struct slapdplugin *
plugin_get_pwd_storage_scheme(char *name, int len, int index)
{
    /* index could be PLUGIN_LIST_PWD_STORAGE_SCHEME or PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME */
    struct slapdplugin *p;

    for (p = global_plugin_list[index]; p != NULL; p = p->plg_next) {
        if ((int)strlen(p->plg_pwdstorageschemename) == len) {
            if (strncasecmp(p->plg_pwdstorageschemename, name, len) == 0) {
                return (p);
            }
        }
    }
    return (NULL);
}

char *
plugin_get_pwd_storage_scheme_list(int index)
{
    /* index could be PLUGIN_LIST_PWD_STORAGE_SCHEME or PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME */

    struct slapdplugin *p = NULL;
    char *names_list = NULL;
    int len = 0;

    /* first pass - calculate space needed for comma delimited list */
    for (p = global_plugin_list[index]; p != NULL; p = p->plg_next) {
        if (p->plg_pwdstorageschemeenc != NULL) {
            /* + 1 for comma, 1 for space, 1 for null */
            len += strlen(p->plg_pwdstorageschemename) + 3;
        }
    }

    /* no plugins? */
    if (!len)
        return NULL;

    /* next, allocate the space */
    names_list = (char *)slapi_ch_malloc(len + 1);
    *names_list = 0;

    /* second pass - write the string */
    for (p = global_plugin_list[index]; p != NULL; p = p->plg_next) {
        if (p->plg_pwdstorageschemeenc != NULL) {
            strcat(names_list, p->plg_pwdstorageschemename);
            if (p->plg_next != NULL)
                strcat(names_list, ", ");
        }
    }
    return (names_list);
}

int
slapi_send_ldap_intermediate(Slapi_PBlock *pb, LDAPControl **ectrls, char *responseName, struct berval *responseValue)
{
    /* no SLAPI_PLUGIN_DB_INTERMEDIATE_FN defined
     * always directly call slapd_ function
     */
    return send_ldap_intermediate(pb, ectrls, responseName, responseValue);
}

int
slapi_send_ldap_search_entry(Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **ectrls, char **attrs, int attrsonly)
{
    IFP fn = NULL;
    slapi_pblock_get(pb, SLAPI_PLUGIN_DB_ENTRY_FN, (void *)&fn);
    if (NULL == fn) {
        return -1;
    }
    return (*fn)(pb, e, ectrls, attrs, attrsonly);
}

void
slapi_set_ldap_result(Slapi_PBlock *pb, int err, char *matched, char *text, int nentries __attribute__((unused)), struct berval **urls __attribute__((unused)))
{
    char *old_matched = NULL;
    char *old_text = NULL;
    char *matched_copy = slapi_ch_strdup(matched);
    char *text_copy = slapi_ch_strdup(text);

    /* free the old matched and text, if any */
    slapi_pblock_get(pb, SLAPI_RESULT_MATCHED, &old_matched);
    slapi_ch_free_string(&old_matched);

    slapi_pblock_get(pb, SLAPI_RESULT_TEXT, &old_text);
    slapi_ch_free_string(&old_text);

    /* set the new stuff */
    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &err);
    slapi_pblock_set(pb, SLAPI_RESULT_MATCHED, matched_copy);
    slapi_pblock_set(pb, SLAPI_RESULT_TEXT, text_copy);
}

void
slapi_send_ldap_result_from_pb(Slapi_PBlock *pb)
{
    int err;
    char *matched;
    char *text;
    IFP fn = NULL;

    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
    slapi_pblock_get(pb, SLAPI_RESULT_TEXT, &text);
    slapi_pblock_get(pb, SLAPI_RESULT_MATCHED, &matched);

    slapi_pblock_get(pb, SLAPI_PLUGIN_DB_RESULT_FN, (void *)&fn);
    if (NULL != fn) {
        (*fn)(pb, err, matched, text, 0, NULL);
    }

    slapi_pblock_set(pb, SLAPI_RESULT_TEXT, NULL);
    slapi_pblock_set(pb, SLAPI_RESULT_MATCHED, NULL);
    slapi_ch_free((void **)&matched);
    slapi_ch_free((void **)&text);
}

void
slapi_send_ldap_result(Slapi_PBlock *pb, int err, char *matched, char *text, int nentries, struct berval **urls)
{
    IFP fn = NULL;
    Slapi_Operation *operation;
    long op_type;

    /* GB : for spanning requests over multiple backends */
    if (err == LDAP_NO_SUCH_OBJECT) {
        slapi_pblock_get(pb, SLAPI_OPERATION, &operation);

        op_type = operation_get_type(operation);
        if (op_type == SLAPI_OPERATION_SEARCH) {
            if (urls || nentries) {
                slapi_log_err(SLAPI_LOG_ERR, "slapi_send_ldap_result",
                              "urls or nentries set in sendldap_result while NO_SUCH_OBJECT returned\n");
            }

            slapi_set_ldap_result(pb, err, matched, text, 0, NULL);
            return;
        }
    }

    slapi_pblock_set(pb, SLAPI_RESULT_CODE, &err);

    slapi_pblock_get(pb, SLAPI_PLUGIN_DB_RESULT_FN, (void *)&fn);
    if (NULL == fn) {
        return;
    }

    /*
     * Call the result function. It should set pb->pb_op->o_status to
     * SLAPI_OP_STATUS_RESULT_SENT right after sending the result to
     * the client or otherwise consuming it.
     */
    (*fn)(pb, err, matched, text, nentries, urls);
}

int
slapi_send_ldap_referral(Slapi_PBlock *pb, Slapi_Entry *e, struct berval **refs, struct berval ***urls)
{
    IFP fn = NULL;
    slapi_pblock_get(pb, SLAPI_PLUGIN_DB_REFERRAL_FN, (void *)&fn);
    if (NULL == fn) {
        return -1;
    }
    return (*fn)(pb, e, refs, urls);
}


/***********************************************************
    start of plugin dependency code
************************************************************/

/* struct _plugin_dep_type
 * we shall not presume to know all plugin types
 * so as to allow new types to be added without
 * requiring changes to this code (hopefully)
 * so we need to dynamically keep track of them
 */

typedef struct _plugin_dep_type
{
    char *type;          /* the string descriptor */
    int num_not_started; /* the count of plugins which have yet to be started for this type */
    struct _plugin_dep_type *next;
} * plugin_dep_type;

/* _plugin_dep_config
 * we need somewhere to collect the plugin configurations
 * prior to attempting to resolve dependencies
 */

typedef struct _plugin_dep_config
{
    char *name;
    char *type;
    Slapi_PBlock *pb;
    struct slapdplugin *plugin;
    Slapi_Entry *e;
    int entry_created;
    int op_done;
    char **depends_type_list;
    int total_type;
    char **depends_named_list;
    int total_named;
    char *config_area;
    int removed;
} plugin_dep_config;

static void plugin_free_plugin_dep_config(plugin_dep_config **config);

/* list of plugins which should be shutdown in reverse order */
static plugin_dep_config *global_plugin_shutdown_order = 0;
static int global_plugins_started = 0;

/*
 * find_plugin_type
 *
 * searches the list for the plugin type
 * and returns the plugin_dep_type if found
 */

static plugin_dep_type
find_plugin_type(plugin_dep_type head, char *type)
{
    plugin_dep_type ret = 0;
    plugin_dep_type iter = head;

    while (iter) {
        if (!slapi_UTF8CASECMP(iter->type, type)) {
            ret = iter;
            break;
        }

        iter = iter->next;
    }

    return ret;
}


/*
 * increment_plugin_type
 *
 * searches the list for the plugin type
 * and increments its not started value
 * returns the current type count on success -1 on failure
 * to find the type
 */

static int
increment_plugin_type(plugin_dep_type head, char *type)
{
    int ret = -1;
    plugin_dep_type the_type;

    if ((the_type = find_plugin_type(head, type)) != NULL)
        ret = ++the_type->num_not_started;

    return ret;
}


/*
 * decrement_plugin_type
 *
 * searches the list for the plugin type
 * and decrements its not started value
 * returns the current type count on success -1 on failure
 * to find the type
 */

static int
decrement_plugin_type(plugin_dep_type head, char *type)
{
    int ret = -1;
    plugin_dep_type the_type;

    if ((the_type = find_plugin_type(head, type)) != NULL)
        ret = --the_type->num_not_started;

    return ret;
}

/*
 * add_plugin_type
 *
 * Either increments the count of the plugin type
 * or when it does not exist, adds it to the list
 */

static int
add_plugin_type(plugin_dep_type *head, char *type)
{
    int ret = -1;

    if (*head) {
        if (0 < increment_plugin_type(*head, type)) {
            ret = 0;
        }
    }

    if (ret) {
        /* create new head */
        plugin_dep_type tmp_head;

        tmp_head = (plugin_dep_type)slapi_ch_malloc(sizeof(struct _plugin_dep_type));
        tmp_head->num_not_started = 1;
        tmp_head->type = slapi_ch_strdup(type);
        ret = 0;
        tmp_head->next = *head;
        (*head) = tmp_head;
    }

    return ret;
}


/*
 * plugin_create_stringlist
 *
 * Creates a string list from values of the entries
 * attribute passed in as args - used to track dependencies
 *
 */
int
plugin_create_stringlist(Slapi_Entry *plugin_entry, char *attr_name, int *total_strings, char ***list)
{
    Slapi_Attr *attr = 0;
    int hint = 0;
    int num_vals = 0;
    int val_index = 0;
    Slapi_Value *val = NULL;

    if (0 == slapi_entry_attr_find(plugin_entry, attr_name, &attr)) {

        /* allocate memory for the string array */
        slapi_attr_get_numvalues(attr, &num_vals);
        if (num_vals) {
            *total_strings = num_vals;
            *list = (char **)slapi_ch_calloc(num_vals + 1, sizeof(char *));
        } else
            goto bail; /* if this ever happens, then they are running on a TSR-80 */

        val_index = 0;

        hint = slapi_attr_first_value(attr, &val);
        while (val_index < num_vals) {
            /* add the value to the array */
            (*list)[val_index] = (char *)slapi_ch_strdup(slapi_value_get_string(val));

            hint = slapi_attr_next_value(attr, hint, &val);
            val_index++;
        }
    } else
        *total_strings = num_vals;

bail:

    return num_vals;
}

/*
 * Remove the plugin from the DN hashtable
 */
static void
plugin_remove_from_list(char *plugin_dn)
{
    Slapi_DN *hash_sdn;

    hash_sdn = (Slapi_DN *)PL_HashTableLookup(global_plugin_dns, plugin_dn);
    if (hash_sdn) {
        PL_HashTableRemove(global_plugin_dns, plugin_dn);
        slapi_sdn_free(&hash_sdn);
    }
}

/*
 * The main plugin was successfully started, set it and its
 * registered plugin functions as started.
 */
static void
plugin_set_plugins_started(struct slapdplugin *plugin_entry)
{
    struct slapdplugin *plugin = NULL;
    int type = 0;

    /* look everywhere for other plugin functions with the plugin id */
    for (type = 0; type < PLUGIN_LIST_GLOBAL_MAX; type++) {
        plugin = global_plugin_list[type];
        while (plugin) {
            if (plugin_cmp_plugins(plugin_entry, plugin)) {
                plugin->plg_started = 1;
            }
            plugin = plugin->plg_next;
        }
    }
}
/*
 * A plugin dependency changed, update the dependency list
 */
void
plugin_update_dep_entries(Slapi_Entry *plugin_entry)
{
    entry_and_plugin_t *ep;

    slapi_rwlock_wrlock(global_rwlock);

    ep = dep_plugin_entries;
    while (ep) {
        if (ep->plugin && ep->e) {
            if (slapi_sdn_compare(slapi_entry_get_sdn(ep->e), slapi_entry_get_sdn(plugin_entry)) == 0) {
                slapi_entry_free(ep->e);
                ep->e = slapi_entry_dup(plugin_entry);
                break;
            }
        }
        ep = ep->next;
    }

    slapi_rwlock_unlock(global_rwlock);
}

/*
 * Attempt to start a plugin that was either just added or just enabled.
 */
int
plugin_start(Slapi_Entry *entry, char *returntext)
{
    entry_and_plugin_t *ep = dep_plugin_entries;
    plugin_dep_config *global_tmp_list = NULL;
    plugin_dep_config *config = NULL;
    plugin_dep_type the_plugin_type;
    plugin_dep_type plugin_head = 0;
    Slapi_PBlock *pb;
    Slapi_Entry *plugin_entry;
    struct slapdplugin *plugin;
    char *value;
    int plugins_started = 1;
    int num_plg_started = 0;
    int shutdown_index = 0;
    int total_plugins = 0;
    int plugin_index = 0;
    int plugin_idx = -1;
    int index = 0;
    int ret = 0;
    int i = 0;

    global_plugins_started = 0;

    /* Count the plugins so we can allocate memory for the config array */
    while (ep) {
        if (slapi_sdn_compare(slapi_entry_get_sdn(ep->e), slapi_entry_get_sdn(entry)) == 0) {
            plugin_idx = total_plugins;
        }
        total_plugins++;
        ep = ep->next;
    }

    /* allocate the config array */
    config = (plugin_dep_config *)slapi_ch_calloc(total_plugins + 1, sizeof(plugin_dep_config));

    ep = dep_plugin_entries;

    /* Collect relevant config */
    while (ep) {
        plugin = ep->plugin;

        if (plugin == 0) {
            continue;
        }

        pb = slapi_pblock_new();
        slapi_pblock_set(pb, SLAPI_ARGC, &plugin->plg_argc);
        slapi_pblock_set(pb, SLAPI_ARGV, &plugin->plg_argv);

        config[plugin_index].pb = pb;
        config[plugin_index].e = ep->e;

        /* add type */
        plugin_entry = ep->e;

        if (plugin_entry) {
            /*
             * Pass the plugin DN in SLAPI_TARGET_SDN and the plugin entry
             * in SLAPI_ADD_ENTRY.  For this to actually work, we need to
             * create an operation and include that in the pblock as well,
             * because these two items are stored in the operation parameters.
             */
            Operation *op = internal_operation_new(SLAPI_OPERATION_ADD, 0);
            slapi_pblock_set(config[plugin_index].pb, SLAPI_OPERATION, op);
            slapi_pblock_set(config[plugin_index].pb, SLAPI_TARGET_SDN,
                             (void *)(slapi_entry_get_sdn_const(plugin_entry)));
            slapi_pblock_set(config[plugin_index].pb, SLAPI_ADD_ENTRY,
                             plugin_entry);

            /* Pass the plugin alternate config area DN in SLAPI_PLUGIN_CONFIG_AREA. */
            value = slapi_entry_attr_get_charptr(plugin_entry, ATTR_PLUGIN_CONFIG_AREA);
            if (value) {
                config[plugin_index].config_area = value;
                value = NULL;
                slapi_pblock_set(config[plugin_index].pb, SLAPI_PLUGIN_CONFIG_AREA,
                                 config[plugin_index].config_area);
            }

            value = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype");
            if (value) {
                add_plugin_type(&plugin_head, value);
                config[plugin_index].type = value;
                value = NULL;
            }

            /* now the name */
            value = slapi_entry_attr_get_charptr(plugin_entry, "cn");
            if (value) {
                config[plugin_index].name = value;
                value = NULL;
            }

            config[plugin_index].plugin = plugin;

            /* now add dependencies */
            plugin_create_stringlist(plugin_entry, ATTR_PLUGIN_DEPENDS_ON_NAMED,
                                     &(config[plugin_index].total_named), &(config[plugin_index].depends_named_list));

            plugin_create_stringlist(plugin_entry, ATTR_PLUGIN_DEPENDS_ON_TYPE,
                                     &(config[plugin_index].total_type), &(config[plugin_index].depends_type_list));
        }
        plugin_index++;
        ep = ep->next;
    }

    /*
     * Prepare list of shutdown order (we need nothing fancier right now
     * than the reverse startup order)  The list may include NULL entries,
     * these will be plugins which were never started
     */
    shutdown_index = total_plugins - 1;

    global_tmp_list = (plugin_dep_config *)slapi_ch_calloc(total_plugins, sizeof(plugin_dep_config));

    /* now resolve dependencies
     * cycle through list, if a plugin has no dependencies then start it
     * then remove it from the dependency lists of all other plugins
     * and decrement the corresponding element of the plugin types array
     * for depends_type we will need to check the array of plugin types
     * to see if all type dependencies are at zero prior to start
     * if one cycle fails to load any plugins we have failed, however
     * we shall continue loading plugins in case a configuration error
     * can correct itself
     */
    while (plugins_started && num_plg_started < total_plugins) {
        for (plugin_index = 0, plugins_started = 0; plugin_index < total_plugins; plugin_index++) {
            /* perform op on plugins only once */
            if (config[plugin_index].op_done == 0) {
                int enabled = 0;
                int satisfied = 0;
                int break_out = 0;

                /*
                 * determine if plugin is enabled
                 * some processing is necessary even
                 * if it is not
                 */
                if (NULL != config[plugin_index].e) {
                    value = (char *)slapi_entry_attr_get_ref(config[plugin_index].e, ATTR_PLUGIN_ENABLED);
                    if (value && !strcasecmp(value, "on")) {
                        enabled = 1;
                    }
                }

                /*
                 * make sure named dependencies have been satisfied
                 * that means that the list of names should contain all
                 * null entries
                 */
                if (enabled && config[plugin_index].total_named) {
                    i = 0;
                    while (break_out == 0 && i < config[plugin_index].total_named) {
                        satisfied = 1;

                        if ((config[plugin_index].depends_named_list)[i] != 0) {
                            satisfied = 0;
                            break_out = 1;
                        }
                        i++;
                    }

                    if (!satisfied) {
                        continue;
                    }
                }

                /*
                 * make sure the type dependencies have been satisfied
                 * that means for each type in the list, it's number of
                 * plugins left not started is zero
                 */
                satisfied = 0;
                break_out = 0;

                if (enabled && config[plugin_index].total_type) {
                    i = 0;
                    while (break_out == 0 && i < config[plugin_index].total_type) {
                        satisfied = 1;
                        the_plugin_type = find_plugin_type(plugin_head, (config[plugin_index].depends_type_list)[i]);

                        if (the_plugin_type && the_plugin_type->num_not_started != 0) {
                            satisfied = 0;
                            break_out = 1;
                        }
                        i++;
                    }

                    if (!satisfied) {
                        continue;
                    }
                }

                /**** This plugins dependencies have now been satisfied ****/

                satisfied = 1; /* symbolic only */
                config[plugin_index].entry_created = 1;

                if (enabled) {
                    if (plugin_index == plugin_idx) {
                        /* finally, perform the op on the plugin */
                        slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_start", "Starting %s plugin %s\n",
                                      config[plugin_index].type, config[plugin_index].name);
                        /*
                         * Put the plugin into the temporary pblock so the startup functions have
                         * access to the real plugin for registering callbacks, task, etc.
                         */
                        slapi_pblock_set(config[plugin_index].pb, SLAPI_PLUGIN, config[plugin_index].plugin);
                        ret = plugin_call_one(config[plugin_index].plugin, SLAPI_PLUGIN_START_FN,
                                              config[plugin_index].pb);
                        // slapi_pblock_destroy(config[plugin_index].pb);

                        if (ret) {
                            /*
                             * Delete the plugin(undo everything), as we don't know how far the start
                             * function got.
                             */
                            slapi_log_err(SLAPI_LOG_ERR, "plugin_start", "Failed to start %s plugin %s\n",
                                          config[plugin_index].type, config[plugin_index].name);
                            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to start plugin \"%s\".  See errors log.",
                                        config[plugin_index].name);
                            plugin_delete(entry, returntext, 1);
                            goto done;
                        } else {
                            /*
                             * Set the plugin and its registered functions as started.
                             */
                            plugin_set_plugins_started(config[plugin_index].plugin);
                        }
                    }

                    /* Add this plugin to the shutdown list */
                    global_tmp_list[shutdown_index] = config[plugin_index];
                    shutdown_index--;
                    global_plugins_started++;

                    /* remove this named plugin from other plugins lists */
                    for (i = 0; i < total_plugins; i++) {
                        index = 0;
                        while (index < config[i].total_named) {
                            if ((config[i].depends_named_list)[index] != 0 &&
                                !slapi_UTF8CASECMP((config[i].depends_named_list)[index], config[plugin_index].name)) {
                                slapi_ch_free((void **)&((config[i].depends_named_list)[index]));
                            }
                            index++;
                        }
                    }
                    // } else {
                    //    /slapi_pblock_destroy(config[plugin_index].pb);
                }

                /* decrement the type counter for this plugin type */
                decrement_plugin_type(plugin_head, config[plugin_index].type);
                config[plugin_index].op_done = 1;
                num_plg_started++;
                plugins_started = 1;
            } /* !op_done */
        }     /* plugin loop */
    }         /* while plugins not started */

    if (plugins_started == 0) {
        /* a dependency was not resolved - error */
        slapi_log_err(SLAPI_LOG_ERR, "plugin_start", "Failed to resolve plugin dependencies\n");
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to resolve plugin dependencies.");

        /* list the plugins yet to perform op */
        i = 0;
        while (i < total_plugins) {
            if (config[i].op_done == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "plugin_start",
                              "%s plugin %s is not started\n", config[i].type, config[i].name);
                plugin_remove_plugins(config[i].plugin, config[i].type);
            }
            i++;
        }
        ret = -1;
    }

done:
    /*
     * Free the plugin list, then rebuild it
     */
    if (ret == LDAP_SUCCESS) {
        /* Ok, everything went well, use the new global_shutdown list */
        plugin_free_plugin_dep_config(&global_plugin_shutdown_order);
        global_plugin_shutdown_order = global_tmp_list;
    } else {
        /*
         * problem, undo what we've done in plugin_setup.
         */
        plugin_free_plugin_dep_config(&global_tmp_list);
    }

    /*
     * need the details in config to hang around for shutdown
     * config itself may be deleted since its contents have been
     * copied by value to the shutdown list
     */
    plugin_free_plugin_dep_config(&config);

    if (plugin_head) {
        plugin_dep_type next;
        while (plugin_head) {
            next = plugin_head->next;
            slapi_ch_free_string(&plugin_head->type);
            slapi_ch_free((void *)&plugin_head);
            plugin_head = next;
        }
    }

    /* Finally enable registered plugin functions */
    global_plugin_callbacks_enabled = 1;

    return ret;
}

static void
plugin_free_plugin_dep_config(plugin_dep_config **cfg)
{
    plugin_dep_config *config = *cfg;
    int index = 0;
    int i = 0;

    if (config) {
        while (config[index].plugin) {
            if (config[index].depends_named_list) {
                for (i = 0; i < config[index].total_named; i++) {
                    slapi_ch_free((void **)&(config[index].depends_named_list)[i]);
                }
                slapi_ch_free((void **)&(config[index].depends_named_list));
            }
            if (config[index].depends_type_list) {
                for (i = 0; i < config[index].total_type; i++) {
                    slapi_ch_free((void **)&(config[index].depends_type_list)[i]);
                }
                slapi_ch_free((void **)&(config[index].depends_type_list));
            }
            slapi_ch_free_string(&config[index].type);
            slapi_ch_free_string(&config[index].name);
            slapi_ch_free_string(&config[index].config_area);
            slapi_pblock_destroy(config[index].pb);
            index++;
        }
        slapi_ch_free((void **)&config);
        *cfg = NULL;
    }
}

/*
 * Take a given plugin and recursively set all the plugin dependency names
 */
void
plugin_get_plugin_dependencies(char *plugin_name, char ***names)
{
    entry_and_plugin_t *ep = dep_plugin_entries;
    char **depends = NULL;
    char *dep_attr = "nsslapd-plugin-depends-on-named";
    int i;

    /* Add the original plugin name to the list */
    if (!charray_inlist(*names, plugin_name)) {
        charray_add(names, slapi_ch_strdup(plugin_name));
    }

    /* Find the plugin and grab its dependencies */
    while (ep) {
        if (ep->plugin) {
            if (strcasecmp(ep->plugin->plg_name, plugin_name) == 0) {
                /* We found our plugin, now grab its dependencies */
                depends = slapi_entry_attr_get_charray(ep->e, dep_attr);
                break;
            }
        }
        ep = ep->next;
    }

    if (depends) {
        /* Add the plugin's dependencies */
        charray_merge_nodup(names, depends, 1);

        /* Add each dependency's dependencies */
        for (i = 0; depends[i]; i++) {
            /* recurse */
            plugin_get_plugin_dependencies(depends[i], names);
        }
        slapi_ch_array_free(depends);
    }
}

/*
 * plugin_dependency_startall()
 *
 * Starts all plugins (apart from syntax and matching rule) in order
 * of dependency.
 *
 * Dependencies will be determined by these multi-valued attributes:
 *
 * nsslapd-plugin-depends-on-type : all plugins whose type value matches one of these values must
 * be started prior to this plugin
 *
 * nsslapd-plugin-depends-on-named : the plugin whose cn value matches one of these values must
 * be started prior to this plugin
 */

static int
plugin_dependency_startall(int argc, char **argv, char *errmsg __attribute__((unused)), int operation, char **plugin_list)
{
    int ret = 0;
    Slapi_PBlock *pb;
    int total_plugins = 0;
    plugin_dep_config *config = 0;
    plugin_dep_type plugin_head = 0;
    int plugin_index = 0;
    Slapi_Entry *plugin_entry;
    int i = 0; /* general index iterator */
    plugin_dep_type the_plugin_type;
    int index = 0;
    char *value = NULL;
    int plugins_started;
    int num_plg_started;
    struct slapdplugin *plugin;
    entry_and_plugin_t *ep = dep_plugin_entries;
    int shutdown_index = 0;

    /*
     * Disable registered plugin functions so preops/postops/etc
     * dont get called prior to the plugin being started (due to
     * plugins performing ops on the DIT)
     */

    global_plugin_callbacks_enabled = 0;

    /* Count the plugins so we can allocate memory for the config array */
    while (ep) {
        total_plugins++;
        ep = ep->next;
    }

    /* allocate the config array */
    config = (plugin_dep_config *)slapi_ch_calloc(total_plugins + 1, sizeof(plugin_dep_config));
    ep = dep_plugin_entries;
    if (plugin_list) {
        /* We have a plugin list, so we need to reset the plugin count */
        total_plugins = 0;
    }

    /* Collect relevant config */
    while (ep) {
        plugin = ep->plugin;

        if (plugin == 0) {
            ep = ep->next;
            continue;
        }

        if (plugin_list) {
            /*
             * We have a specific list of plugins to start, skip the others...
             */
            int found = 0;
            for (i = 0; plugin_list[i]; i++) {
                if (strcasecmp(plugin->plg_name, plugin_list[i]) == 0) {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                /* Skip this plugin, it's not in the list */
                ep = ep->next;
                continue;
            } else {
                total_plugins++;
            }
        }

        pb = slapi_pblock_new();
        slapi_pblock_set(pb, SLAPI_ARGC, &argc);
        slapi_pblock_set(pb, SLAPI_ARGV, &argv);

        config[plugin_index].pb = pb;
        config[plugin_index].e = ep->e;

        /* add type */
        plugin_entry = ep->e;

        if (plugin_entry) {
            /*
             * Pass the plugin DN in SLAPI_TARGET_SDN and the plugin entry
             * in SLAPI_ADD_ENTRY.  For this to actually work, we need to
             * create an operation and include that in the pblock as well,
             * because these two items are stored in the operation parameters.
             */
            /* WARNING: memory leak here - op is only freed by a pblock_done,
               and this only happens below if the plugin is enabled - a short
               circuit goto bail may also cause a leak - however, since this
               only happens a few times at startup, this is not a very serious
               leak - just after the call to plugin_call_one */
            Operation *op = internal_operation_new(SLAPI_OPERATION_ADD, 0);
            slapi_pblock_set(config[plugin_index].pb, SLAPI_OPERATION, op);
            slapi_pblock_set(config[plugin_index].pb, SLAPI_TARGET_SDN,
                             (void *)(slapi_entry_get_sdn_const(plugin_entry)));
            slapi_pblock_set(config[plugin_index].pb, SLAPI_ADD_ENTRY,
                             plugin_entry);

            /* Pass the plugin alternate config area DN in SLAPI_PLUGIN_CONFIG_AREA. */
            value = slapi_entry_attr_get_charptr(plugin_entry, ATTR_PLUGIN_CONFIG_AREA);
            if (value) {
                config[plugin_index].config_area = value;
                value = NULL;
                slapi_pblock_set(config[plugin_index].pb, SLAPI_PLUGIN_CONFIG_AREA,
                                 config[plugin_index].config_area);
            }

            value = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype");
            if (value) {
                add_plugin_type(&plugin_head, value);
                config[plugin_index].type = value;
                value = NULL;
            }

            /* now the name */
            value = slapi_entry_attr_get_charptr(plugin_entry, "cn");
            if (value) {
                config[plugin_index].name = value;
                value = NULL;
            }


            config[plugin_index].plugin = plugin;

            /* now add dependencies */
            plugin_create_stringlist(plugin_entry, "nsslapd-plugin-depends-on-named",
                                     &(config[plugin_index].total_named), &(config[plugin_index].depends_named_list));

            plugin_create_stringlist(plugin_entry, "nsslapd-plugin-depends-on-type",
                                     &(config[plugin_index].total_type), &(config[plugin_index].depends_type_list));
        }
        plugin_index++;
        ep = ep->next;
    }

    /* prepare list of shutdown order (we need nothing fancier right now
     * than the reverse startup order)  The list may include NULL entries,
     * these will be plugins which were never started
     */
    shutdown_index = total_plugins - 1;

    global_plugin_shutdown_order = (plugin_dep_config *)slapi_ch_calloc(total_plugins + 1, sizeof(plugin_dep_config));

    /* now resolve dependencies
     * cycle through list, if a plugin has no dependencies then start it
     * then remove it from the dependency lists of all other plugins
     * and decrement the corresponding element of the plugin types array
     * for depends_type we will need to check the array of plugin types
     * to see if all type dependencies are at zero prior to start
     * if one cycle fails to load any plugins we have failed, however
     * we shall continue loading plugins in case a configuration error
     * can correct itself
     */

    plugins_started = 1;
    num_plg_started = 0;

    while (plugins_started && num_plg_started < total_plugins) {
        plugins_started = 0;

        for (plugin_index = 0; plugin_index < total_plugins; plugin_index++) {
            /* perform op on plugins only once */
            if (config[plugin_index].op_done == 0) {
                int enabled = 0;
                int satisfied = 0;
                int break_out = 0;

                /*
                 * determine if plugin is enabled
                 * some processing is necessary even
                 * if it is not
                 */
                if (NULL != config[plugin_index].e) {
                    value = (char *)slapi_entry_attr_get_ref(config[plugin_index].e, ATTR_PLUGIN_ENABLED);
                    if (value && !strcasecmp(value, "on")) {
                        enabled = 1;
                    }
                }

                /*
                 * make sure named dependencies have been satisfied
                 * that means that the list of names should contain all
                 * null entries
                 */

                if (enabled && config[plugin_index].total_named) {
                    i = 0;

                    while (break_out == 0 && i < config[plugin_index].total_named) {
                        satisfied = 1;

                        if ((config[plugin_index].depends_named_list)[i] != 0) {
                            satisfied = 0;
                            break_out = 1;
                        }

                        i++;
                    }

                    if (!satisfied) {
                        continue;
                    }
                }

                /*
                 * make sure the type dependencies have been satisfied
                 * that means for each type in the list, it's number of
                 * plugins left not started is zero
                 *
                 */
                satisfied = 0;
                break_out = 0;

                if (enabled && config[plugin_index].total_type) {
                    i = 0;

                    while (break_out == 0 && i < config[plugin_index].total_type) {
                        satisfied = 1;

                        the_plugin_type = find_plugin_type(plugin_head, (config[plugin_index].depends_type_list)[i]);

                        if (the_plugin_type && the_plugin_type->num_not_started != 0) {
                            satisfied = 0;
                            break_out = 1;
                        }

                        i++;
                    }

                    if (!satisfied) {
                        continue;
                    }
                }

                /**** This plugins dependencies have now been satisfied ****/

                satisfied = 1; /* symbolic only */

                /*
                 * Add the plugins entry to the DSE so the plugin can get
                 * its config (both enabled and disabled have entries
                 */

                if (!config[plugin_index].entry_created) {
                    int plugin_actions = 0;
                    Slapi_PBlock *newpb = slapi_pblock_new();
                    Slapi_Entry *newe;

                    newe = slapi_entry_dup(config[plugin_index].e);
                    slapi_add_entry_internal_set_pb(newpb, newe, NULL,
                                                    plugin_get_default_component_id(), plugin_actions);
                    slapi_pblock_set(newpb, SLAPI_TARGET_SDN, (void *)slapi_entry_get_sdn_const(newe));
                    slapi_add_internal_pb(newpb);
                    slapi_pblock_destroy(newpb);
                    config[plugin_index].entry_created = 1;
                }

                /*
                 *    only actually start plugin and remove from lists if its enabled
                 *    we do remove from plugin type list however - rule is dependency on
                 *    zero or more for type
                 */

                if (enabled) {
                    /* finally, perform the op on the plugin */

                    slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_dependency_startall",
                                  "Starting %s plugin %s\n", config[plugin_index].type, config[plugin_index].name);
                    /*
                     * Put the plugin into the temporary pblock so the startup functions have
                     * access to the real plugin for registering callbacks, tasks, etc.
                     */
                    slapi_pblock_set(config[plugin_index].pb, SLAPI_PLUGIN, config[plugin_index].plugin);
                    ret = plugin_call_one(config[plugin_index].plugin, operation, config[plugin_index].pb);

                    /* We used to call pblock done here, bet we try to restart
                     * the plugin. If we call done on the pb here, the next attempt
                     * can have even MORE issues!
                     */

                    if (ret) {
                        /*
                         * We will not exit here.  If we allow plugins to load normally it is
                         * possible that a configuration error (dependencies which were not
                         * configured properly) can be recovered from.  If there really is a
                         * problem then the plugin will never start and eventually it will
                         * trigger an exit anyway.
                         */
                        slapi_log_err(SLAPI_LOG_ERR, "plugin_dependency_startall",
                                      "Failed to start %s plugin %s\n",
                                      config[plugin_index].type, config[plugin_index].name);
                        continue;
                    } else {
                        /* now set the plugin and all its registered plugin functions as started */
                        plugin_set_plugins_started(config[plugin_index].plugin);
                    }

                    /* See above note about pblock done: Now we call it here
                     * because at this point we *have* started correctly, so we
                     * can now free this.
                     */
                    /* This is freed in free_plugin_dep_config */
                    /*
                        slapi_pblock_destroy(config[plugin_index].pb);
                    */

                    /* Add this plugin to the shutdown list */
                    global_plugin_shutdown_order[shutdown_index] = config[plugin_index];
                    shutdown_index--;
                    global_plugins_started++;

                    /* remove this named plugin from other plugins lists */

                    for (i = 0; i < total_plugins; i++) {
                        index = 0;

                        while (index < config[i].total_named) {
                            if ((config[i].depends_named_list)[index] != 0 && !slapi_UTF8CASECMP((config[i].depends_named_list)[index], config[plugin_index].name)) {
                                slapi_ch_free((void **)&((config[i].depends_named_list)[index]));
                                (config[i].depends_named_list)[index] = 0;
                            }

                            index++;
                        }
                    }
                } else { /* if (enabled) == false */
                         /* slapi_pblock_destroy(config[plugin_index].pb); */
                         /* This is freed in plugin_free_plugin_dep_config for us */
                }

                /* decrement the type counter for this plugin type */

                decrement_plugin_type(plugin_head, config[plugin_index].type);
                config[plugin_index].op_done = 1;
                num_plg_started++;
                plugins_started = 1;
            }
        }
    }

    if (plugins_started == 0) {
        /* a dependency was not resolved - error */
        slapi_log_err(SLAPI_LOG_ERR, "plugin_dependency_startall", "Failed to resolve plugin dependencies\n");

        /* list the plugins yet to perform op */
        i = 0;
        while (i < total_plugins) {
            if (config[i].op_done == 0) {
                slapi_log_err(SLAPI_LOG_ERR, "plugin_dependency_startall", "%s plugin %s is not started\n",
                              config[i].type, config[i].name);
            }
            i++;
        }
        plugin_closeall(1 /* Close Backends */, 1 /* Close Globals */);
        exit(1);
    }

    /*
     * need the details in config to hang around for shutdown
     * config itself may be deleted since its contents have been
     * copied by value to the shutdown list
     */
    plugin_free_plugin_dep_config(&config);

    if (plugin_head) {
        plugin_dep_type next;
        while (plugin_head) {
            next = plugin_head->next;
            slapi_ch_free_string(&plugin_head->type);
            slapi_ch_free((void *)&plugin_head);
            plugin_head = next;
        }
    }

    /* Finally enable registered plugin functions */
    global_plugin_callbacks_enabled = 1;

    return ret;
}

/*
 *    plugin_dependency_closeall
 *
 *    uses the shutdown list created at startup to close
 *    plugins in the correct order
 *
 *  For now this leaks the list and contents, but since
 *  it hangs around until shutdown anyway, we don't care
 *
 */
void
plugin_dependency_closeall(void)
{
    Slapi_PBlock *pb;
    int plugins_closed = 0;
    int index = 0;

    while (plugins_closed < global_plugins_started) {
        /*
         * the first few entries may not be valid
         * since the list was created in the reverse
         * order and some plugins may have been counted
         * for the purpose of list allocation but are
         * disabled and so were never started
         *
         * we check that here
         */
        if (global_plugin_shutdown_order[index].name) {
            if (!global_plugin_shutdown_order[index].removed) {
                pb = slapi_pblock_new();
                plugin_set_stopped(global_plugin_shutdown_order[index].plugin);
                plugin_op_all_finished(global_plugin_shutdown_order[index].plugin);
                plugin_call_one(global_plugin_shutdown_order[index].plugin, SLAPI_PLUGIN_CLOSE_FN, pb);
                /* set plg_closed to 1 to prevent any further plugin pre/post op function calls */
                global_plugin_shutdown_order[index].plugin->plg_closed = 1;
                slapi_pblock_destroy(pb);
            }
            plugins_closed++;
        }
        index++;
    }
}

void
plugin_freeall(void)
{
    struct slapdplugin *plugin = NULL;
    struct slapdplugin *plugin_next = NULL;
    int type = 0;

    /* look everywhere for other plugin functions with the plugin id */
    for (type = 0; type < PLUGIN_LIST_GLOBAL_MAX; type++) {
        plugin = global_plugin_list[type];
        while (plugin) {
            plugin_next = plugin->plg_next;
            plugin_free(plugin);
            plugin = plugin_next;
        }
    }
}

/*
 * Function: plugin_dependency_freeall
 *
 * Description: Free the plugin dependency list.
 * It need to be called after be_cleanupall so that the
 * ldbm_database plugin is not freed when be_cleanupall calls the cleanup
 * callback of the backend
 */
void
plugin_dependency_freeall()
{
    entry_and_plugin_t *iterp, *nextp;
    char *value;

    /* free the plugin dependency entry list */
    iterp = dep_plugin_entries;
    while (iterp) {
        nextp = iterp->next;
        if ((value = (char *)slapi_entry_attr_get_ref(iterp->e, ATTR_PLUGIN_ENABLED)) &&
            !strcasecmp(value, "off")) {
                plugin_free(iterp->plugin);
        }
        slapi_entry_free(iterp->e);
        slapi_ch_free((void **)&iterp);
        iterp = nextp;
    }
    dep_plugin_entries = NULL;
    plugin_freeall();
}

/***********************************************************
    end of plugin dependency code
************************************************************/


/*
 * Function: plugin_startall
 *
 * Returns: squat
 *
 * Description: Some plugins may need to do some stuff after all the config
 *              stuff is done with. So this function goes through and starts all plugins
 */
void
plugin_startall(int argc, char **argv, char **plugin_list)
{
    /* initialize special plugin structures */
    default_plugin_init();

    plugin_dependency_startall(argc, argv, "plugin startup failed\n",
                               SLAPI_PLUGIN_START_FN, plugin_list);
}

/*
 * Function: plugin_close_all
 *
 * Returns: squat
 *
 * Description: cleanup routine, allows plugins to kill threads, free memory started in start fn
 *
 */
void
plugin_closeall(int close_backends __attribute__((unused)), int close_globals __attribute__((unused)))
{
    plugin_dependency_closeall();
}


static int
plugin_call_list(struct slapdplugin *list, int operation, Slapi_PBlock *pb)
{
    return plugin_call_func(list, operation, pb, 0);
}

static int
plugin_call_one(struct slapdplugin *list, int operation, Slapi_PBlock *pb)
{
    return plugin_call_func(list, operation, pb, 1);
}


/*
 * Return codes:
 * - For preoperation plugins, returns the return code passed back from the first
 *   plugin that fails, or zero if all plugins succeed.
 * - For bepreop and bepostop plugins, returns a bitwise OR of the return codes
 *   returned by all the plugins called (there's only one bepreop and one bepostop
 *   in DS 5.0 anyway).
 * - For postoperation plugins, returns 0.
 */
static int
plugin_call_func(struct slapdplugin *list, int operation, Slapi_PBlock *pb, int call_one)
{
    /* Invoke the operation on the plugins that are registered for the subtree effected by the operation. */
    int rc;
    int return_value = 0;
    int count = 0;

    for (; list != NULL; list = list->plg_next) {
        IFP func = NULL;

        slapi_pblock_set(pb, SLAPI_PLUGIN, list);
        set_db_default_result_handlers(pb); /* JCM: What's this do? Is it needed here? */
        if (slapi_pblock_get(pb, operation, &func) == 0 &&
            func != NULL &&
            plugin_invoke_plugin_pb(list, operation, pb) &&
            list->plg_closed == 0) {
            char *n = list->plg_name;

            slapi_log_err(SLAPI_LOG_TRACE, "plugin_call_func",
                          "Calling plugin '%s' #%d type %d\n",
                          (n == NULL ? "noname" : n), count, operation);
            /* counters_to_errors_log("before plugin call"); */

            /*
             * Only call the plugin function if:
             *
             *  [1]  The plugin is started, and we are NOT trying to restart it.
             *  [2]  The plugin is started, and we are stopping it.
             *  [3]  The plugin is stopped, and we are trying to start it.
             *
             *  This frees up the plugins from having to check if the plugin is already started when
             *  calling the START and CLOSE functions - prevents double starts and stops.
             */
            slapi_plugin_op_started(list);
            if (((SLAPI_PLUGIN_START_FN == operation && !list->plg_started) || /* Starting it up for the first time */
                 (SLAPI_PLUGIN_CLOSE_FN == operation && !list->plg_stopped) || /* Shutting down, plugin has been stopped */
                 (SLAPI_PLUGIN_START_FN != operation && list->plg_started)) && /* Started, and not trying to start again */
                (rc = func(pb)) != 0) {
                slapi_plugin_op_finished(list);
                if (SLAPI_PLUGIN_PREOPERATION == list->plg_type ||
                    SLAPI_PLUGIN_INTERNAL_PREOPERATION == list->plg_type ||
                    SLAPI_PLUGIN_START_FN == operation) {
                    /*
                     * We bail out of plugin processing for preop plugins
                     * that return a non-zero return code. This allows preop
                     * plugins to cause further preop processing to terminate, and
                     * causes the operation to be vetoed.
                     */
                    return_value = rc;
                    break;
                } else if (SLAPI_PLUGIN_BEPREOPERATION == list->plg_type ||
                           SLAPI_PLUGIN_BETXNPREOPERATION == list->plg_type ||
                           SLAPI_PLUGIN_BEPOSTOPERATION == list->plg_type ||
                           SLAPI_PLUGIN_BETXNPOSTOPERATION == list->plg_type) {
                    /*
                     * respect fatal error SLAPI_PLUGIN_FAILURE (-1);
                     * should not OR it.
                     */
                    if (SLAPI_PLUGIN_FAILURE == rc) {
                        return_value = rc;
                    } else if (SLAPI_PLUGIN_FAILURE != return_value) {
                        /* OR the result into the return value
                         * for be pre/postops */
                        return_value |= rc;
                    }
                }
            } else {
                if (SLAPI_PLUGIN_CLOSE_FN == operation) {
                    /* successfully stopped the plugin */
                    list->plg_stopped = 1;
                }
                slapi_plugin_op_finished(list);
            }
            /* counters_to_errors_log("after plugin call"); */
        }

        count++;

        if (call_one)
            break;
    }

    return (return_value);
}

int
slapi_berval_cmp(const struct berval *L, const struct berval *R) /* JCM - This does not belong here. But, where should it go? */
{
    int result = 0;

    if (!L || !L->bv_val) {
        if (!R || !R->bv_val) {
            return 0;
        }
        return -1;
    }
    if (!R || !R->bv_val) {
        return 1;
    }
    if (L->bv_len < R->bv_len) {
        result = memcmp(L->bv_val, R->bv_val, L->bv_len);
        if (result == 0)
            result = -1;
    } else {
        result = memcmp(L->bv_val, R->bv_val, R->bv_len);
        if (result == 0 && (L->bv_len > R->bv_len))
            result = 1;
    }

    return result;
}


static char **supported_saslmechanisms = NULL;
static Slapi_RWLock *supported_saslmechanisms_lock = NULL;

/*
 * register a supported SASL mechanism so it will be returned as part of the
 * root DSE.
 */
void
slapi_register_supported_saslmechanism(char *mechanism)
{
    if (mechanism != NULL) {
        if (NULL == supported_saslmechanisms_lock) {
            /* This is thread safe, as it gets executed by
             * a single thread at init time (main->init_saslmechanisms) */
            supported_saslmechanisms_lock = slapi_new_rwlock();
            if (NULL == supported_saslmechanisms_lock) {
                /* Out of resources */
                slapi_log_err(SLAPI_LOG_ERR,
                              "slapi_register_supported_saslmechanism", "Failed to create lock.\n");
                exit(1);
            }
        }
        slapi_rwlock_wrlock(supported_saslmechanisms_lock);
        charray_add(&supported_saslmechanisms, slapi_ch_strdup(mechanism));
        slapi_rwlock_unlock(supported_saslmechanisms_lock);
    }
}


/*
 * return pointer to NULL-terminated array of supported SASL mechanisms.
 * This function is not MTSafe and should be deprecated.
 * slapi_get_supported_saslmechanisms_copy should be used instead.
 */
char **
slapi_get_supported_saslmechanisms(void)
{
    return (supported_saslmechanisms);
}


/*
 * return pointer to NULL-terminated array of supported SASL mechanisms.
 */
char **
slapi_get_supported_saslmechanisms_copy(void)
{
    char **ret = NULL;
    slapi_rwlock_rdlock(supported_saslmechanisms_lock);
    ret = charray_dup(supported_saslmechanisms);
    slapi_rwlock_unlock(supported_saslmechanisms_lock);
    return (ret);
}


static char **supported_extended_ops = NULL;
static Slapi_RWLock *extended_ops_lock = NULL;

/*
 * register all of the LDAPv3 extended operations we know about.
 */
void
ldapi_init_extended_ops(void)
{
    extended_ops_lock = slapi_new_rwlock();
    if (NULL == extended_ops_lock) {
        /* Out of resources */
        slapi_log_err(SLAPI_LOG_ERR, "ldapi_init_extended_ops",
                      "Failed to create lock.\n");
        exit(1);
    }

    slapi_rwlock_wrlock(extended_ops_lock);
    charray_add(&supported_extended_ops,
                slapi_ch_strdup(EXTOP_BULK_IMPORT_START_OID));
    charray_add(&supported_extended_ops,
                slapi_ch_strdup(EXTOP_BULK_IMPORT_DONE_OID));
    /* add future supported extops here... */
    slapi_rwlock_unlock(extended_ops_lock);
}


/*
 * register an extended op. so it can be returned as part of the root DSE.
 */
void
ldapi_register_extended_op(char **opoids)
{
    int i;

    slapi_rwlock_wrlock(extended_ops_lock);
    for (i = 0; opoids != NULL && opoids[i] != NULL; ++i) {
        if (!charray_inlist(supported_extended_ops, opoids[i])) {
            charray_add(&supported_extended_ops, slapi_ch_strdup(opoids[i]));
        }
    }
    slapi_rwlock_unlock(extended_ops_lock);
}


/*
 * retrieve supported extended operation OIDs
 * return 0 if successful and -1 if not.
 * This function is not MTSafe and should be deprecated.
 * slapi_get_supported_extended_ops_copy should be used instead.
 */
char **
slapi_get_supported_extended_ops(void)
{
    return (supported_extended_ops);
}


/*
 * retrieve supported extended operation OIDs
 * return 0 if successful and -1 if not.
 */
char **
slapi_get_supported_extended_ops_copy(void)
{
    char **ret = NULL;
    slapi_rwlock_rdlock(extended_ops_lock);
    ret = charray_dup(supported_extended_ops);
    slapi_rwlock_unlock(extended_ops_lock);
    return (ret);
}


/*
  looks up the given string type to convert to the internal integral type; also
  returns the plugin list associated with the plugin type
  returns 0 upon success and non-zero upon failure
*/
static int
plugin_get_type_and_list(
    const char *plugintype,
    int *type,
    struct slapdplugin ***plugin_list)
{
    int plugin_list_index = -1;
    if (strcasecmp(plugintype, "database") == 0) {
        *type = SLAPI_PLUGIN_DATABASE;
        plugin_list_index = PLUGIN_LIST_DATABASE;
    } else if (strcasecmp(plugintype, "extendedop") == 0) {
        *type = SLAPI_PLUGIN_EXTENDEDOP;
        plugin_list_index = PLUGIN_LIST_EXTENDED_OPERATION;
    } else if (strcasecmp(plugintype, "preoperation") == 0) {
        *type = SLAPI_PLUGIN_PREOPERATION;
        plugin_list_index = PLUGIN_LIST_PREOPERATION;
    } else if (strcasecmp(plugintype, "postoperation") == 0) {
        *type = SLAPI_PLUGIN_POSTOPERATION;
        plugin_list_index = PLUGIN_LIST_POSTOPERATION;
    } else if (strcasecmp(plugintype, "matchingrule") == 0) {
        *type = SLAPI_PLUGIN_MATCHINGRULE;
        plugin_list_index = PLUGIN_LIST_MATCHINGRULE;
    } else if (strcasecmp(plugintype, "syntax") == 0) {
        *type = SLAPI_PLUGIN_SYNTAX;
        plugin_list_index = PLUGIN_LIST_SYNTAX;
    } else if (strcasecmp(plugintype, "accesscontrol") == 0) {
        *type = SLAPI_PLUGIN_ACL;
        plugin_list_index = PLUGIN_LIST_ACL;
    } else if ( strcasecmp( plugintype, "mmr" ) == 0 ) {
        *type = SLAPI_PLUGIN_MMR;
        plugin_list_index = PLUGIN_LIST_MMR;
    } else if (strcasecmp(plugintype, "bepreoperation") == 0) {
        *type = SLAPI_PLUGIN_BEPREOPERATION;
        plugin_list_index = PLUGIN_LIST_BEPREOPERATION;
    } else if (strcasecmp(plugintype, "bepostoperation") == 0) {
        *type = SLAPI_PLUGIN_BEPOSTOPERATION;
        plugin_list_index = PLUGIN_LIST_BEPOSTOPERATION;
    } else if (strcasecmp(plugintype, "betxnpreoperation") == 0) {
        *type = SLAPI_PLUGIN_BETXNPREOPERATION;
        plugin_list_index = PLUGIN_LIST_BETXNPREOPERATION;
    } else if (strcasecmp(plugintype, "betxnpostoperation") == 0) {
        *type = SLAPI_PLUGIN_BETXNPOSTOPERATION;
        plugin_list_index = PLUGIN_LIST_BETXNPOSTOPERATION;
    } else if (strcasecmp(plugintype, "internalpreoperation") == 0) {
        *type = SLAPI_PLUGIN_INTERNAL_PREOPERATION;
        plugin_list_index = PLUGIN_LIST_INTERNAL_PREOPERATION;
    } else if (strcasecmp(plugintype, "internalpostoperation") == 0) {
        *type = SLAPI_PLUGIN_INTERNAL_POSTOPERATION;
        plugin_list_index = PLUGIN_LIST_INTERNAL_POSTOPERATION;
    } else if (strcasecmp(plugintype, "entry") == 0) {
        *type = SLAPI_PLUGIN_ENTRY;
        plugin_list_index = PLUGIN_LIST_ENTRY;
    } else if (strcasecmp(plugintype, "object") == 0) {
        *type = SLAPI_PLUGIN_TYPE_OBJECT;
        plugin_list_index = PLUGIN_LIST_OBJECT;
    } else if (strcasecmp(plugintype, "pwdstoragescheme") == 0) {
        *type = SLAPI_PLUGIN_PWD_STORAGE_SCHEME;
        plugin_list_index = PLUGIN_LIST_PWD_STORAGE_SCHEME;
    } else if (strcasecmp(plugintype, "reverpwdstoragescheme") == 0) {
        *type = SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME;
        plugin_list_index = PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME;
    } else if (strcasecmp(plugintype, "vattrsp") == 0) {
        *type = SLAPI_PLUGIN_VATTR_SP;
        plugin_list_index = PLUGIN_LIST_VATTR_SP;
    } else if (strcasecmp(plugintype, "ldbmentryfetchstore") == 0) {
        *type = SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE;
        plugin_list_index = PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE;
    } else if (strcasecmp(plugintype, "index") == 0) {
        *type = SLAPI_PLUGIN_INDEX;
        plugin_list_index = PLUGIN_LIST_INDEX;
    } else if (strcasecmp(plugintype, "betxnextendedop") == 0) {
        *type = SLAPI_PLUGIN_BETXNEXTENDEDOP;
        plugin_list_index = PLUGIN_LIST_BE_TXN_EXTENDED_OPERATION;
    } else if (strcasecmp(plugintype, "preextendedop") == 0) {
        *type = SLAPI_PLUGIN_PREEXTOPERATION;
        plugin_list_index = PLUGIN_LIST_PREEXTENDED_OPERATION;
    } else if (strcasecmp(plugintype, "postextendedop") == 0) {
        *type = SLAPI_PLUGIN_POSTEXTOPERATION;
        plugin_list_index = PLUGIN_LIST_POSTEXTENDED_OPERATION;
    } else {
        return (1); /* unknown plugin type - pass to backend */
    }

    if (plugin_list_index >= 0) {
        *plugin_list = &global_plugin_list[plugin_list_index];
    }

    return 0;
}

static char *
plugin_get_type_str(int type)
{
    if (type == SLAPI_PLUGIN_DATABASE) {
        return "database";
    } else if (type == SLAPI_PLUGIN_EXTENDEDOP) {
        return "extendedop";
    } else if (type == SLAPI_PLUGIN_PREOPERATION) {
        return "preoperation";
    } else if (type == SLAPI_PLUGIN_POSTOPERATION) {
        return "postoperation";
    } else if (type == SLAPI_PLUGIN_MATCHINGRULE) {
        return "matchingrule";
    } else if (type == SLAPI_PLUGIN_SYNTAX) {
        return "syntax";
    } else if (type == SLAPI_PLUGIN_ACL) {
        return "accesscontrol";
    } else if (type == SLAPI_PLUGIN_MMR){
        return "mmr";
    } else if (type == SLAPI_PLUGIN_BEPREOPERATION) {
        return "bepreoperation";
    } else if (type == SLAPI_PLUGIN_BEPOSTOPERATION) {
        return "bepostoperation";
    } else if (type == SLAPI_PLUGIN_BETXNPREOPERATION) {
        return "betxnpreoperation";
    } else if (type == SLAPI_PLUGIN_BETXNPOSTOPERATION) {
        return "betxnpostoperation";
    } else if (type == SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
        return "internalpreoperation";
    } else if (type == SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
        return "internalpostoperation";
    } else if (type == SLAPI_PLUGIN_ENTRY) {
        return "entry";
    } else if (type == SLAPI_PLUGIN_TYPE_OBJECT) {
        return "object";
    } else if (type == SLAPI_PLUGIN_PWD_STORAGE_SCHEME) {
        return "pwdstoragescheme";
    } else if (type == SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME) {
        return "reverpwdstoragescheme";
    } else if (type == SLAPI_PLUGIN_VATTR_SP) {
        return "vattrsp";
    } else if (type == SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE) {
        return "ldbmentryfetchstore";
    } else if (type == SLAPI_PLUGIN_INDEX) {
        return "index";
    } else {
        return NULL; /* unknown plugin type - pass to backend */
    }
}

static const char *
plugin_exists(const Slapi_DN *plugin_dn)
{
    /* check to see if the plugin name is unique */
    const char *retval = 0;
    if (global_plugin_dns && PL_HashTableLookup(global_plugin_dns,
                                                slapi_sdn_get_ndn(plugin_dn))) {
        retval = slapi_sdn_get_dn(plugin_dn);
    }

    return retval;
}

/*
 * A plugin config change has occurred, restart the plugin (delete/add).
 * If something goes wrong, revert to the original plugin entry.
 */
int
plugin_restart(Slapi_Entry *pentryBefore, Slapi_Entry *pentryAfter)
{
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE];
    int rc = LDAP_SUCCESS;

    /*
     * We can not restart a critical plugin, but the operation should still
     * be allowed
     */
    if (plugin_is_critical(pentryBefore)) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_restart",
                      "Plugin (%s) is critical to server operation.  "
                      "Server requires restart for changes to take effect.\n",
                      slapi_entry_get_dn(pentryBefore));
        return 0;
    }

    slapi_rwlock_wrlock(global_rwlock);
    slapi_td_set_plugin_locked();

    if (plugin_delete(pentryBefore, returntext, 1) == LDAP_SUCCESS) {
        if (plugin_add(pentryAfter, returntext, 1) == LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_restart",
                          "Plugin (%s) has been successfully restarted after configuration change.\n",
                          slapi_entry_get_dn(pentryAfter));
        } else {
            slapi_log_err(SLAPI_LOG_ERR, "plugin_restart",
                          "Plugin (%s) failed to restart after configuration change (%s).  "
                          "Reverting to original plugin entry.\n",
                          slapi_entry_get_dn(pentryAfter), returntext);
            if (plugin_add(pentryBefore, returntext, 1) != LDAP_SUCCESS) {
                slapi_log_err(SLAPI_LOG_ERR, "plugin_restart",
                              "Plugin (%s) failed to reload original plugin (%s)\n",
                              slapi_entry_get_dn(pentryBefore), returntext);
            }
            rc = 1;
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_restart",
                      "Failed to disable/stop the plugin (%s): %s\n",
                      slapi_entry_get_dn(pentryBefore), returntext);
        rc = 1;
    }

    slapi_rwlock_unlock(global_rwlock);
    slapi_td_set_plugin_unlocked();

    return rc;
}

static int
plugin_set_subtree_config(PluginTargetData *subtree_config, const char *val)
{
    int status = 0;

    if (strcasecmp(val, ALL_DATA) == 0) /* allow access to both local and remote data */
    {
        plugin_set_global(subtree_config);
    } else if (strcasecmp(val, LOCAL_DATA) == 0) /* allow access to all locally hosted data */
    {
        ptd_set_special_data(subtree_config, PLGC_DATA_LOCAL);
    } else if (strcasecmp(val, REMOTE_DATA) == 0) /* allow access to requests for remote data */
    {
        ptd_set_special_data(subtree_config, PLGC_DATA_REMOTE);
    } else /* dn */
    {
        ptd_add_subtree(subtree_config, slapi_sdn_new_dn_byval(val));
    }
    /* I suppose we could check the val at this point to make sure
       its a valid DN . . . */

    return status;
}

static int
set_plugin_config_from_entry(
    const Slapi_Entry *plugin_entry,
    struct slapdplugin *plugin)
{
    struct pluginconfig *config = &plugin->plg_conf;
    char *value = 0;
    char **values = 0;
    int i = 0;
    int status = 0;
    PRBool target_seen = PR_FALSE;
    PRBool bind_seen = PR_FALSE;

    if ((value = (char *)slapi_entry_attr_get_ref((Slapi_Entry *)plugin_entry, ATTR_PLUGIN_SCHEMA_CHECK)) != NULL) {
        if (plugin_config_set_action(&config->plgc_schema_check, value)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          value, ATTR_PLUGIN_SCHEMA_CHECK, slapi_entry_get_dn_const(plugin_entry));
            status = 1;
        }
    }

    if ((value = (char *)slapi_entry_attr_get_ref((Slapi_Entry *)plugin_entry, ATTR_PLUGIN_LOG_ACCESS)) != NULL) {
        if (plugin_config_set_action(&config->plgc_log_access, value)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          value, ATTR_PLUGIN_LOG_ACCESS, slapi_entry_get_dn_const(plugin_entry));
            status = 1;
        }
    }

    if ((value = (char *)slapi_entry_attr_get_ref((Slapi_Entry *)plugin_entry, ATTR_PLUGIN_LOG_AUDIT)) != NULL) {
        if (plugin_config_set_action(&config->plgc_log_audit, value)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          value, ATTR_PLUGIN_LOG_AUDIT, slapi_entry_get_dn_const(plugin_entry));
            status = 1;
        }
    }

    if ((value = (char *)slapi_entry_attr_get_ref((Slapi_Entry *)plugin_entry, ATTR_PLUGIN_INVOKE_FOR_REPLOP)) != NULL) {
        if (plugin_config_set_action(&config->plgc_invoke_for_replop, value)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          value, ATTR_PLUGIN_INVOKE_FOR_REPLOP,
                          slapi_entry_get_dn_const(plugin_entry));
            status = 1;
        }
    }

    values = slapi_entry_attr_get_charray(plugin_entry,
                                          ATTR_PLUGIN_TARGET_SUBTREE);
    for (i = 0; values && values[i]; i++) {
        if (plugin_set_subtree_config(&(config->plgc_target_subtrees), values[i])) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          values[i], ATTR_PLUGIN_TARGET_SUBTREE,
                          slapi_entry_get_dn_const(plugin_entry));
            status = 1;
            break;
        } else {
            target_seen = PR_TRUE;
        }
    }
    slapi_ch_array_free(values);

    values = slapi_entry_attr_get_charray(plugin_entry,
                                          ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE);
    for (i = 0; values && values[i]; i++) {
        if (plugin_set_subtree_config(&(config->plgc_excluded_target_subtrees), values[i])) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          values[i], ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE,
                          slapi_entry_get_dn_const(plugin_entry));
            status = 1;
            break;
        }
    }
    slapi_ch_array_free(values);

    values = slapi_entry_attr_get_charray(plugin_entry,
                                          ATTR_PLUGIN_BIND_SUBTREE);
    for (i = 0; values && values[i]; i++) {
        if (plugin_set_subtree_config(&(config->plgc_bind_subtrees), values[i])) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          values[i], ATTR_PLUGIN_BIND_SUBTREE,
                          slapi_entry_get_dn_const(plugin_entry));
            status = 1;
            break;
        } else {
            bind_seen = PR_TRUE;
        }
    }
    slapi_ch_array_free(values);

    values = slapi_entry_attr_get_charray(plugin_entry,
                                          ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE);
    for (i = 0; values && values[i]; i++) {
        if (plugin_set_subtree_config(&(config->plgc_excluded_bind_subtrees), values[i])) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "set_plugin_config_from_entry",
                          "Invalid value %s for attribute %s from entry %s\n",
                          values[i], ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE,
                          slapi_entry_get_dn_const(plugin_entry));
            status = 1;
            break;
        }
    }
    slapi_ch_array_free(values);

    /* set target subtree default - allow access to all data */
    if (!target_seen) {
        plugin_set_global(&(config->plgc_target_subtrees));
    }

    /* set bind subtree default - allow access to local data only */
    if (!bind_seen) {
        ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_LOCAL);
        ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_REMOTE);
    }

    return status;
}

/* This function is called after the plugin init function has been called
   which fills in the desc part of the plugin
*/
static int
add_plugin_description(Slapi_Entry *e, const char *attrname, char *val)
{
    struct berval desc;
    struct berval *newval[2] = {0, 0};
    int status = 0;

    desc.bv_val = SLAPI_PLUGIN_NONE_IF_NULL(val);
    desc.bv_len = strlen(desc.bv_val);
    newval[0] = &desc;
    if ((status = entry_replace_values(e, attrname, newval)) != 0) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "add_plugin_description",
                      "Error: failed to add value %s to attribute %s of entry %s\n",
                      val, attrname, slapi_entry_get_dn_const(e));
        status = 1;
    }

    return status;
}


/*
 * The plugin initfunc sets some vendor and version information in the plugin.
 * This function extracts that and adds it as attributes to `e'.  If
 * `plugin' is NULL, the plugin is located based on the DN in `e'.
 *
 * Returns 0 if all goes well and 1 if not.
 */
int
plugin_add_descriptive_attributes(Slapi_Entry *e, struct slapdplugin *plugin)
{
    int status = 0;

    if (NULL == plugin) {
        int i;
        const Slapi_DN *ednp = slapi_entry_get_sdn_const(e);
        Slapi_DN pdn;
        struct slapdplugin *plugtmp;

        for (i = 0; NULL == plugin && i < PLUGIN_LIST_GLOBAL_MAX; ++i) {
            for (plugtmp = global_plugin_list[i]; NULL == plugin && plugtmp;
                 plugtmp = plugtmp->plg_next) {
                slapi_sdn_init_normdn_byref(&pdn, plugtmp->plg_dn);
                if (0 == slapi_sdn_compare(&pdn, ednp)) {
                    plugin = plugtmp;
                }
                slapi_sdn_done(&pdn);
            }
        }

        if (NULL == plugin) {
            /* This can happen for things such as disabled syntax plug-ins.  We
             * just treat this as a warning to allow the description attributes
             * to be set to a default value to avoid an objectclass violation. */
            slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_add_descriptive_attributes",
                          "Couldn't find plugin %s in global list. "
                          "Adding default descriptive values.\n",
                          slapi_entry_get_dn_const(e));
        }
    }


    if (add_plugin_description(e, ATTR_PLUGIN_PLUGINID,
                               plugin ? plugin->plg_desc.spd_id : NULL)) {
        status = 1;
    }

    if (add_plugin_description(e, ATTR_PLUGIN_VERSION,
                               plugin ? plugin->plg_desc.spd_version : NULL)) {
        status = 1;
    }

    if (add_plugin_description(e, ATTR_PLUGIN_VENDOR,
                               plugin ? plugin->plg_desc.spd_vendor : NULL)) {
        status = 1;
    }

    if (add_plugin_description(e, ATTR_PLUGIN_DESC,
                               plugin ? plugin->plg_desc.spd_description : NULL)) {
        status = 1;
    }

    return status;
}


/*
  clean up the memory associated with the plugin
*/
static void
plugin_free(struct slapdplugin *plugin)
{
    slapi_log_err(SLAPI_LOG_TRACE, "plugin_free", "Freeing %s \n", plugin->plg_name);
    charray_free(plugin->plg_argv);
    slapi_ch_free_string(&plugin->plg_libpath);
    slapi_ch_free_string(&plugin->plg_initfunc);
    slapi_ch_free_string(&plugin->plg_name);
    slapi_ch_free_string(&plugin->plg_dn);
    if (plugin->plg_type == SLAPI_PLUGIN_PWD_STORAGE_SCHEME || plugin->plg_type == SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME) {
        slapi_ch_free_string(&plugin->plg_pwdstorageschemename);
    }
    if (plugin->plg_type == SLAPI_PLUGIN_SYNTAX) {
        slapi_ch_free_string(&plugin->plg_syntax_oid);
        slapi_ch_array_free(plugin->plg_syntax_names);
    }
    if (plugin->plg_type == SLAPI_PLUGIN_MATCHINGRULE) {
        slapi_ch_array_free(plugin->plg_mr_names);
    }
    release_componentid(plugin->plg_identity);
    slapi_counter_destroy(&plugin->plg_op_counter);
    if (!plugin->plg_group) {
        plugin_config_cleanup(&plugin->plg_conf);
    }
    slapi_ch_free((void **)&plugin);
}

/*
 * Check if a plugin is critical or not.
 * The plugins not critical are statically listed either
 * with their name or with the plugin path
 *  - GOST_YESCRYPT (name)
 *  - entryuuid (name)
 *  - libpwdchan (rust
 * Returns:
 *   1 - critical
 *   0 - non critical
 */
static int
plugin_load_critical(struct slapdplugin *plugin)
{
    char *non_critical_plugin_name[] = {"GOST_YESCRYPT", "entryuuid", NULL };
    char *non_critical_plugin_libpath[] = {"libpwdchan-plugin", NULL};
    for (int32_t i = 0; non_critical_plugin_name[i]; i++) {
        if (strcasecmp(plugin->plg_name, non_critical_plugin_name[i]) == 0) {
            /* this plugin in *not* critical */
            return 0;
        }
    }
    for (int32_t i = 0; non_critical_plugin_libpath[i]; i++) {
        if (strcasecmp(plugin->plg_libpath, non_critical_plugin_libpath[i]) == 0) {
            /* this plugin in *not* critical */
            return 0;
        }
    }
    return 1;
}
/***********************************
This is the main entry point for plugin configuration.  The plugin_entry argument
should already contain the necessary fields required to initialize the plugin and
to give it a proper name in the plugin configuration DIT.

Argument:
Slapi_Entry *plugin_entry - the required attributes are
    dn: the dn of the plugin entry
    cn: the unique name of the plugin
    nsslapd-pluginType: one of the several recognized plugin types e.g. "postoperation"

if p_initfunc is given, pluginPath and pluginInitFunc are optional
    nsslapd-pluginPath: full path and file name of the dll implementing the plugin
    nsslapd-pluginInitFunc: the name of the plugin initialization function

the optional attributes are:
    nsslapd-pluginArg0
    ...
    nsslapd-pluginArg[N-1] - the (old style) arguments to the plugin, where N varies
        from 0 to the number of arguments.  The numbers must be consecutive i.e. no
        skipping

    Instead of using nsslapd-pluginArgN, it is encouraged for you to use named
    parameters e.g.
    nsslapd-tweakThis: 1
    nsslapd-tweakThat: 2
    etc.

    nsslapd-pluginEnabled: "on"|"off" - by default, the plugin will be enabled unless
        this attribute is present and has the value "off"

    for other known attributes, see set_plugin_config_from_entry() above

    all other attributes will be ignored

    The reason this parameter is not const is because it may be modified.  This
    function will modify it if the plugin init function is called successfully
    to add the description attributes, and the plugin init function may modify
    it as well.

Argument:
group - the group to which this plugin will belong - each member of a plugin group
    shares the pluginconfig of the group leader; refer to the function plugin_get_config
    for more information

Argument:
add_entry - if true, the entry will be added to the DIT using the given
    DN in the plugin_entry - this is the default behavior; if false, the
    plugin entry will not show up in the DIT
************************************/
int
plugin_setup(Slapi_Entry *plugin_entry, struct slapi_componentid *group, slapi_plugin_init_fnptr p_initfunc, int add_entry, char *returntext)
{
    int ii = 0;
    char attrname[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    char *value = NULL;
    char **values = NULL;
    struct slapdplugin *plugin = NULL;
    struct slapdplugin **plugin_list = NULL;
    struct slapi_componentid *cid = NULL;
    const char *existname = 0;
    slapi_plugin_init_fnptr initfunc = p_initfunc;
    Slapi_PBlock *pb = NULL;
    int status = 0;
    int enabled = 1;
    char *configdir = 0;
    int skipped;

    attrname[0] = '\0';

    if (!slapi_entry_get_sdn_const(plugin_entry)) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "DN is missing from the plugin.\n");
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Plugin is missing dn.");
        return -1;
    }

    if ((existname = plugin_exists(slapi_entry_get_sdn_const(plugin_entry))) != NULL) {
        slapi_log_err(SLAPI_LOG_TRACE, "plugin_setup", "The plugin named %s already exists, "
                                                       "or is already setup.\n",
                      existname);
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "the plugin named %s already exists, or is already setup.", existname);
        return 1;
    }

    /*
     * create a new plugin structure, fill it in, and prepare to
     * call the plugin's init function. the init function will
     * set the plugin function pointers.
     */
    plugin = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));

    plugin->plg_dn = slapi_ch_strdup(slapi_entry_get_ndn(plugin_entry));
    plugin->plg_closed = 0;

    if (!(value = (char *)slapi_entry_attr_get_ref(plugin_entry, ATTR_PLUGIN_TYPE))) {
        /* error: required attribute %s missing */
        slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Required attribute %s is missing from entry \"%s\"\n",
                      ATTR_PLUGIN_TYPE, slapi_entry_get_dn_const(plugin_entry));
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Required attribute %s is missing from entry",
                    ATTR_PLUGIN_TYPE);
        status = -1;
        goto PLUGIN_CLEANUP;
    } else {
        status = plugin_get_type_and_list(value, &plugin->plg_type,
                                          &plugin_list);
        if (status != 0) {
            /* error: unknown plugin type */
            slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Unknown plugin type \"%s\" in entry \"%s\"\n",
                          value, slapi_entry_get_dn_const(plugin_entry));
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown plugin type \"%s\" in entry", value);
            status = -1;
            goto PLUGIN_CLEANUP;
        }
    }

    if (!status &&
        !(value = (char *)slapi_entry_attr_get_ref(plugin_entry, "cn"))) {
        /* error: required attribute %s missing */
        slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Required attribute %s is missing from entry \"%s\"\n",
                      "cn", slapi_entry_get_dn_const(plugin_entry));
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Required attribute \"cn\" is missing from entry");
        status = -1;
        goto PLUGIN_CLEANUP;
    } else {
        /* plg_name is normalized once here */
        plugin->plg_name = slapi_create_rdn_value("%s", value);
    }

    if (!(value = (char *)slapi_entry_attr_get_ref(plugin_entry, ATTR_PLUGIN_PRECEDENCE))) {
        /* A precedence isn't set, so just use the default. */
        plugin->plg_precedence = PLUGIN_DEFAULT_PRECEDENCE;
    } else {
        /* A precedence was set, so let's make sure it's valid. */
        int precedence = 0;
        char *endptr = NULL;

        /* Convert the value. */
        precedence = strtol(value, &endptr, 10);

        /* Make sure the precedence is within our valid
         * range and that we had no conversion errors. */
        if ((*value == '\0') || (*endptr != '\0') ||
            (precedence < PLUGIN_MIN_PRECEDENCE) || (precedence > PLUGIN_MAX_PRECEDENCE)) {
            slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Value for attribute %s must be "
                                                         "an integer between %d and %d\n",
                          ATTR_PLUGIN_PRECEDENCE,
                          PLUGIN_MIN_PRECEDENCE, PLUGIN_MAX_PRECEDENCE);
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Value for attribute %s must be "
                                                               "an integer between %d and %d.",
                        ATTR_PLUGIN_PRECEDENCE,
                        PLUGIN_MIN_PRECEDENCE, PLUGIN_MAX_PRECEDENCE);
            status = -1;
            goto PLUGIN_CLEANUP;
        } else {
            plugin->plg_precedence = precedence;
        }
    }

    if (!(value = slapi_entry_attr_get_charptr(plugin_entry, ATTR_PLUGIN_INITFN))) {
        if (!initfunc) {
            /* error: required attribute %s missing */
            slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Required attribute %s is missing from entry \"%s\"\n",
                          ATTR_PLUGIN_INITFN, slapi_entry_get_dn_const(plugin_entry));
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Required attribute %s is missing from entry.",
                        ATTR_PLUGIN_INITFN);
            status = -1;
            goto PLUGIN_CLEANUP;
        }
    } else {
        plugin->plg_initfunc = value; /* plugin owns value's memory now, don't free */
    }

    if (!initfunc) {
        PRBool loadNow = PR_FALSE;
        PRBool loadGlobal = PR_FALSE;

        if (!(value = slapi_entry_attr_get_charptr(plugin_entry,
                                                   ATTR_PLUGIN_PATH))) {
            /* error: required attribute %s missing */
            slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Required attribute %s is missing from entry \"%s\"\n",
                          ATTR_PLUGIN_PATH, slapi_entry_get_dn_const(plugin_entry));
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Required attribute %s is missing from entry.",
                        ATTR_PLUGIN_PATH);
            status = -1;
            goto PLUGIN_CLEANUP;
        } else {
            plugin->plg_libpath = value; /* plugin owns value's memory now, don't free */
            /* Is this plugins shared object in the removed list? */
            if (upgrade_plugin_removed(plugin->plg_libpath)) {
                slapi_log_err(SLAPI_LOG_INFO, "plugin_setup",
                        "Disabling plugin library %s, which is marked for removal ...\n", value);
                /* Disable it. */
                enabled = 0;
            }
        }

        /*
         * Enabled at this point is either true from the initial value, or false from the
         * removed list. The true enable status as configured is determined later, but the
         * syms are always loaded even if disabled. We have to use this check here because
         * if the plug is in the removed set, it may not exist on disk so we can dlopn it.
         */
        if (enabled) {
            loadNow = slapi_entry_attr_get_bool(plugin_entry, ATTR_PLUGIN_LOAD_NOW);
            loadGlobal = slapi_entry_attr_get_bool(plugin_entry, ATTR_PLUGIN_LOAD_GLOBAL);

            /* Upgrade the replication plugin name */
            upgrade_repl_plugin_name(plugin_entry, plugin);

            /*
             * load the plugin's init function
             */
            initfunc = (slapi_plugin_init_fnptr)sym_load_with_flags(plugin->plg_libpath,
                                                                         plugin->plg_initfunc, plugin->plg_name, 1 /* report errors */,
                                                                         loadNow, loadGlobal);
            if (initfunc == NULL) {
                /* failure to load that plugin or find its initfunct, let's fail only if it is critical */
                if (plugin_load_critical(plugin)) {
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to load plugin's init function.");
                    status = -1;
                    goto PLUGIN_CLEANUP;
                } else {
                    /* This plugin is not critical, just ignore it and continue */
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "plugin_setup", "\"%s\" plugin in library \"%s\" not initialized and ignored\n",
                                  plugin->plg_name, plugin->plg_libpath);
                    enabled = 0;
                }
            }
        }
    }

    if (!status && group) /* uses group's config; see plugin_get_config */
    {
        struct slapi_componentid *group_cid = (struct slapi_componentid *)group;
        plugin->plg_group = (struct slapdplugin *)group_cid->sci_plugin;
    } else if (!status) /* using own config */
    {
        plugin_config_init(&(plugin->plg_conf));
        set_plugin_config_from_entry(plugin_entry, plugin);
    }

    /*
     * If this is a registered plugin function, then set the plugin id so we can remove
     * this plugin later if needed.
     */
    if (group) {
        plugin->plg_id = group->sci_component_name;
    }

    /* add the plugin arguments */
    value = 0;
    ii = 0;
    PR_snprintf(attrname, sizeof(attrname), "%s%d", ATTR_PLUGIN_ARG, ii);
    skipped = 0;
#define MAXSKIPPED 10 /* Max allowed missing args */
    do {
        /*
         * nsslapd-pluginarg0: val0
         * nsslapd-pluginarg0: val00
         * nsslapd-pluginarg2: val2
         * nsslapd-pluginarg5: val5
         * ==>
         * treated as
         * nsslapd-pluginarg0: val0
         * nsslapd-pluginarg1: val00
         * nsslapd-pluginarg2: val2
         * nsslapd-pluginarg3: val5
         */
        char **vp = values = slapi_entry_attr_get_charray(plugin_entry, attrname);
        if (values) {
            charray_add(&plugin->plg_argv, slapi_ch_strdup(*vp));
            plugin->plg_argc++;
            for (vp++; vp && *vp; vp++) {
                charray_add(&plugin->plg_argv, slapi_ch_strdup(*vp));
                plugin->plg_argc++;
            }
            charray_free(values);
        } else {
            skipped++;
        }
        PR_snprintf(attrname, sizeof(attrname), "%s%d", ATTR_PLUGIN_ARG, ++ii);
    } while (skipped < MAXSKIPPED);

    pb = slapi_pblock_new();

    slapi_pblock_set(pb, SLAPI_PLUGIN, plugin);
    slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_CURRENT_VERSION);

    cid = generate_componentid(plugin, NULL);
    /* We take a copy of the pointer to this so we can free it correctly. */
    plugin->plg_identity = cid;
    slapi_pblock_set(pb, SLAPI_PLUGIN_IDENTITY, (void *)cid);

    configdir = config_get_configdir();
    slapi_pblock_set(pb, SLAPI_CONFIG_DIRECTORY, configdir);

    /* see if the plugin is enabled or not */
    /*
     * By default enabled is 1, so if all is good, this allows us to check the enabled
     * status. But if plg_libpath was in the removed list, enabled is 0, so we now don't
     * check the enabled attr here as we are force-disabling the plugin.
     */
    if (enabled) {
        if ((value = (char *)slapi_entry_attr_get_ref(plugin_entry, ATTR_PLUGIN_ENABLED)) &&
            !strcasecmp(value, "off")) {
            enabled = 0;
        } else {
            enabled = 1;
        }
    }

    slapi_pblock_set(pb, SLAPI_PLUGIN_ENABLED, &enabled);
    slapi_pblock_set(pb, SLAPI_PLUGIN_CONFIG_ENTRY, plugin_entry);
    plugin->plg_op_counter = slapi_counter_new();

    if (enabled && (*initfunc)(pb) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_setup", "Init function \"%s\" for \"%s\" plugin in library \"%s\" failed\n",
                      plugin->plg_initfunc, plugin->plg_name, plugin->plg_libpath);
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Init function \"%s\" for \"%s\" plugin in "
                                                           "library \"%s\" failed.",
                    plugin->plg_initfunc, plugin->plg_name, plugin->plg_libpath);
        status = -1;
        /*
         * The init function might have added the plugin to the global list before
         * it failed - attempt to remove it just in case it was added.
         */
        plugin_remove_plugins(plugin, value);
        goto PLUGIN_CLEANUP;
    }

    if (!status) {
        if (plugin_add_descriptive_attributes(plugin_entry, plugin) != 0) {
            status = -1;
        }
    }

    if (enabled) {
        /* don't use raw pointer from plugin_entry because it
           will be freed later by the caller */
        Slapi_DN *dn_copy = slapi_sdn_dup(slapi_entry_get_sdn_const(plugin_entry));
        add_plugin_to_list(plugin_list, plugin);
        add_plugin_entry_dn(dn_copy);
    }

    if (add_entry) {
        /* make a copy of the plugin entry for our own use because it will
           be freed later by the caller */
        Slapi_Entry *e_copy = slapi_entry_dup(plugin_entry);
        /* new_plugin_entry(&plugin_entries, plugin_entry, plugin); */
        new_plugin_entry(&dep_plugin_entries, e_copy, plugin);
    }

PLUGIN_CLEANUP:
    if (status) {
        plugin_free(plugin);
    }
    slapi_ch_free((void **)&configdir);
    slapi_pblock_destroy(pb);

    return status;
}

/*
 * We added a plugin, do our setup and then start the plugin.  This is the same as adding a plugin
 * or enabling a disabled plugin.
 */
int
plugin_add(Slapi_Entry *entry, char *returntext, int locked)
{
    int rc = LDAP_SUCCESS;

    if (!locked) {
        slapi_rwlock_wrlock(global_rwlock);
        slapi_td_set_plugin_locked();
    }

    if ((rc = plugin_setup(entry, 0, 0, 1, returntext)) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_add", "plugin_setup failed for (%s) %d\n",
                      slapi_entry_get_dn(entry), rc);
        goto done;
    }

    if ((rc = plugin_start(entry, returntext)) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_add", "plugin_start failed for (%s) %d\n",
                      slapi_entry_get_dn(entry), rc);
        goto done;
    }

done:
    if (!locked) {
        slapi_rwlock_unlock(global_rwlock);
        slapi_td_set_plugin_unlocked();
    }

    return rc;
}

static char *
get_dep_plugin_list(char **plugins)
{
    char output[1024];
    int first_plugin = 1;
    PRUint32 len = 0;
    int i;

    for (i = 0; plugins && plugins[i]; i++) {
        if (first_plugin) {
            PL_strncpyz(output, plugins[i], sizeof(output) - 3);
            len += strlen(plugins[i]);
            first_plugin = 0;
        } else {
            PL_strcatn(output, sizeof(output) - 3, ", ");
            PL_strcatn(output, sizeof(output) - 3, plugins[i]);
            len += strlen(plugins[i]);
        }
        if (len > (sizeof(output) - 3)) {
            /*
             * We could not print all the plugins, show that we truncated
             * the list by adding "..."
             */
            PL_strcatn(output, sizeof(output), "...");
        }
    }

    return slapi_ch_strdup(output);
}

/*
 * Make sure the removal of this plugin does not breaking any existing dependencies
 */
static int
plugin_delete_check_dependency(struct slapdplugin *plugin_entry, int flag, char *returntext)
{
    entry_and_plugin_t *ep = dep_plugin_entries;
    plugin_dep_config *config = NULL;
    struct slapdplugin *plugin = NULL;
    Slapi_Entry *pentry = NULL;
    char *plugin_name = plugin_entry->plg_name;
    char *plugin_type = plugin_get_type_str(plugin_entry->plg_type);
    char **dep_plugins = NULL;
    char *value = NULL;
    char **list = NULL;
    int dep_type_count = 0;
    int type_count = 0;
    int total_plugins = 0;
    int plugin_index = 0;
    int index = 0;
    int rc = LDAP_SUCCESS;
    int i;

    while (ep) {
        total_plugins++;
        ep = ep->next;
    }

    /* allocate the config array */
    config = (plugin_dep_config *)slapi_ch_calloc(total_plugins + 1, sizeof(plugin_dep_config));

    ep = dep_plugin_entries;

    /*
     * Collect relevant config
     */
    while (ep) {
        plugin = ep->plugin;

        if (plugin == 0) {
            goto next;
        }

        /*
         * We are not concerned with disabled plugins
         */
        value = (char *)slapi_entry_attr_get_ref(ep->e, ATTR_PLUGIN_ENABLED);
        if (value) {
            if (strcasecmp(value, "off") == 0) {
                goto next;
            }
        } else {
            goto next;
        }
        if (!ep->plugin) {
            goto next;
        }

        config[plugin_index].e = ep->e;
        pentry = ep->e;
        if (pentry) {
            config[plugin_index].plugin = plugin;
            plugin_create_stringlist(pentry, "nsslapd-plugin-depends-on-named",
                                     &(config[plugin_index].total_named), &(config[plugin_index].depends_named_list));
            plugin_create_stringlist(pentry, "nsslapd-plugin-depends-on-type",
                                     &(config[plugin_index].total_type), &(config[plugin_index].depends_type_list));
        }
        plugin_index++;
    next:
        ep = ep->next;
    }

    /*
     * Start checking every plugin for dependency issues
     */
    for (index = 0; index < plugin_index; index++) {
        if ((plugin = config[index].plugin)) {
            /*
             * We can skip ourselves, and our registered plugins.
             */
            if (plugin_cmp_plugins(plugin, plugin_entry)) {
                continue;
            }

            /*
             * Check all the plugins to see if one is depending on this plugin(name)
             */
            if (flag == CHECK_ALL && config[index].depends_named_list) {
                list = config[index].depends_named_list;
                for (i = 0; list && list[i]; i++) {
                    if (strcasecmp(list[i], plugin_name) == 0) {
                        /* We have a dependency, we can not disable this pluign */
                        slapi_log_err(SLAPI_LOG_ERR, "plugin_delete_check_dependency",
                                      "Can not disable plugin(%s) due to dependency name issues with plugin (%s)\n",
                                      plugin_name, config[index].plugin->plg_name);
                        rc = -1;
                        goto free_and_return;
                    }
                }
            }
            /*
             * Check all the plugins to see if one is depending on this plugin(type).
             */
            if (config[index].depends_type_list) {
                list = config[index].depends_type_list;
                for (i = 0; list && list[i]; i++) {
                    if (strcasecmp(list[i], plugin_type) == 0) {
                        charray_add(&dep_plugins, slapi_ch_strdup(plugin->plg_name));
                        dep_type_count++;
                        break;
                    }
                }
            }
        }
    }
    /*
     * Now check the dependency type count.
     */
    if (dep_type_count > 0) {
        /*
         * There are plugins that depend on this plugin type. Now, get a plugin count of this
         * type of plugin.  If we are the only plugin of this type, we can not disable it.
         */
        for (index = 0; index < plugin_index; index++) {
            if ((plugin = config[index].plugin)) {
                /* Skip ourselves, and our registered plugins. */
                if (plugin_cmp_plugins(plugin, plugin_entry)) {
                    continue;
                }
                if (plugin->plg_type == plugin_entry->plg_type) {
                    /* there is at least one other plugin of this type, its ok to disable */
                    type_count = 1;
                    break;
                }
            }
        }
        if (type_count == 0) { /* this is the only plugin of this type - return an error */
            char *plugins = get_dep_plugin_list(dep_plugins);

            /*
             * The plugin type was changed, but since other plugins currently have dependencies,
             * we can not dynamically apply the change.  This is will most likely cause issues
             * at the next server startup.
             */
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Plugin (%s) type (%s) is needed "
                                                               "by other plugins(%s), it can not be dynamically disabled/removed at this time.",
                        plugin_name, plugin_type, plugins);
            slapi_log_err(SLAPI_LOG_ERR, "plugin_delete_check_dependency", "%s\n",
                          returntext);

            slapi_ch_free_string(&plugins);
            rc = -1;
        }
    }

free_and_return:
    /*
     * Free the config list
     */
    charray_free(dep_plugins);
    if (config) {
        index = 0;
        while (config[index].plugin) {
            charray_free(config[index].depends_type_list);
            charray_free(config[index].depends_named_list);
            index++;
        }
        slapi_ch_free((void **)&config);
    }

    return rc;
}

/*
 * Mark the plugin in the shutdown list a removed
 */
static void
plugin_remove_from_shutdown(struct slapdplugin *plugin_entry)
{
    struct slapdplugin *plugin = NULL;
    int index = 0;

    for (; index < global_plugins_started; index++) {
        if ((plugin = global_plugin_shutdown_order[index].plugin)) {
            if (global_plugin_shutdown_order[index].removed) {
                continue;
            }
            /* "plugin_entry" can be the main plugin for registered function */
            if (plugin_cmp_plugins(plugin, plugin_entry)) {
                /*
                 * We have our index, just mark it as removed.  The global list gets rewritten
                 * the next time we add or enable a plugin.
                 */
                global_plugin_shutdown_order[index].removed = 1;
                return;
            }
        }
    }
}

/*
 * Free the plugins that have been set to be removed.
 */
static void
plugin_cleanup_list(void)
{
    struct slapdplugin *plugin = NULL;
    entry_and_plugin_t *ep = dep_plugin_entries;
    entry_and_plugin_t *ep_prev = NULL, *ep_next = NULL;

    while (ep) {
        plugin = ep->plugin;
        ep_next = ep->next;
        if (plugin && plugin->plg_removed) {
            if (ep_prev) {
                ep_prev->next = ep->next;
            } else {
                dep_plugin_entries = ep->next;
            }
            slapi_entry_free(ep->e);
            if (ep->plugin) {
                plugin_free(ep->plugin);
            }
            slapi_ch_free((void **)&ep);
        } else {
            ep_prev = ep;
        }
        ep = ep_next;
    }
}

/*
 * Look at all the plugins for any matches.  Then mark the ones we need
 * to delete.  After checking all the plugins, then we free the ones that
 * were marked to be removed.
 */
static int
plugin_remove_plugins(struct slapdplugin *plugin_entry, char *plugin_type __attribute__((unused)))
{
    struct slapdplugin *plugin = NULL;
    struct slapdplugin *plugin_next = NULL;
    struct slapdplugin *plugin_prev = NULL;
    int removed = PLUGIN_NOT_FOUND;
    int type;

    /* look everywhere for other plugin functions with the plugin id */
    for (type = 0; type < PLUGIN_LIST_GLOBAL_MAX; type++) {
        plugin = global_plugin_list[type];
        plugin_prev = NULL;
        while (plugin) {
            plugin_next = plugin->plg_next;
            /*
             * Check for the two types of plugins:
             * the main plugin, and its registered plugin functions.
             */
            if (plugin_cmp_plugins(plugin_entry, plugin)) {
                /*
                 * Call the close function, cleanup the hashtable & the global shutdown list
                 */
                plugin_set_stopped(plugin);
                if (slapi_counter_get_value(plugin->plg_op_counter) > 0) {
                    /*
                     * Plugin is still busy, and we might be blocking it
                     * by holding global plugin lock so return for now.
                     */
                    return PLUGIN_BUSY;
                }
                Slapi_PBlock *pb = slapi_pblock_new();
                plugin_call_one(plugin, SLAPI_PLUGIN_CLOSE_FN, pb);
                slapi_pblock_destroy(pb);

                if (plugin_prev) {
                    plugin_prev->plg_next = plugin_next;
                } else {
                    global_plugin_list[type] = plugin_next;
                }

                /*
                 * Remove plugin the DN hashtable, update the shutdown list,
                 * and mark the plugin for deletion
                 */
                plugin_remove_from_list(plugin->plg_dn);
                plugin_remove_from_shutdown(plugin);
                plugin->plg_removed = 1;
                plugin->plg_started = 0;
                removed = PLUGIN_REMOVED;
            } else {
                plugin_prev = plugin;
            }
            plugin = plugin_next;
        }
    }
    if (removed) {
        /*
         * Now free the marked plugins, we could not do this earlier because
         * we also needed to check for plugins registered functions.  As both
         * plugin types share the same slapi_plugin entry.
         */
        plugin_cleanup_list();
    }

    return removed;
}

/*
 * We are removing a plugin from the global list.  This happens when we delete a plugin, or disable it.
 */
int
plugin_delete(Slapi_Entry *plugin_entry, char *returntext, int locked)
{
    struct slapdplugin **plugin_list = NULL;
    struct slapdplugin *plugin = NULL;
    const char *plugin_dn = slapi_entry_get_dn_const(plugin_entry);
    char *value = NULL;
    int removed = PLUGIN_BUSY;
    int type = 0;
    int rc = LDAP_SUCCESS;

    /* Critical server plugins can not be disabled */
    if (plugin_is_critical(plugin_entry)) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_delete", "Plugin \"%s\" is critical to server operations, and can not be disabled\n",
                      slapi_entry_get_dn_const(plugin_entry));
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Plugin \"%s\" is critical to server operations, and can not "
                                                           "be disabled.\n",
                    slapi_entry_get_dn_const(plugin_entry));
        rc = -1;
        goto done;
    }

    if (!(value = (char *)slapi_entry_attr_get_ref(plugin_entry, ATTR_PLUGIN_TYPE))) {
        /* error: required attribute %s missing */
        slapi_log_err(SLAPI_LOG_ERR, "plugin_delete", "Required attribute %s is missing from entry \"%s\"\n",
                      ATTR_PLUGIN_TYPE, slapi_entry_get_dn_const(plugin_entry));
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Required attribute %s "
                                                           "is missing from entry.",
                    ATTR_PLUGIN_TYPE);
        rc = -1;
        goto done;
    } else {
        while (removed == PLUGIN_BUSY) {
            removed = PLUGIN_NOT_FOUND;
            if (!locked) {
                slapi_rwlock_wrlock(global_rwlock);
                slapi_td_set_plugin_locked();
            }

            rc = plugin_get_type_and_list(value, &type, &plugin_list);
            if (rc != 0) {
                /* error: unknown plugin type */
                slapi_log_err(SLAPI_LOG_ERR, "plugin_delete", "Unknown plugin type \"%s\" in entry \"%s\"\n",
                              value, slapi_entry_get_dn_const(plugin_entry));
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Unknown plugin type \"%s\" in entry.",
                            value);
                rc = -1;
                goto unlock;
            }

            /*
             * Skip syntax/matching rule/database plugins - these can not be disabled as it
             * could break existing schema.  We allow the update to occur, but it will
             * not take effect until the next server restart.
             */
            if (type == SLAPI_PLUGIN_SYNTAX || type == SLAPI_PLUGIN_MATCHINGRULE || type == SLAPI_PLUGIN_DATABASE) {
                removed = PLUGIN_REMOVED; /* avoids error check below */
                goto unlock;
            }

            /*
             * Now remove the plugin from the list and the hashtable
             */
            for (plugin = *plugin_list; plugin; plugin = plugin->plg_next) {
                if (strcasecmp(plugin->plg_dn, plugin_dn) == 0) {
                    /*
                     * Make sure there are no other plugins that depend on this one before removing it
                     */
                    if (plugin_delete_check_dependency(plugin, CHECK_ALL, returntext) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "plugin_delete", "Failed to disable/delete plugin (%s)\n",
                                      plugin->plg_dn);
                        rc = -1;
                        break;
                    }
                    removed = plugin_remove_plugins(plugin, value);
                    break;
                }
            }
        unlock:
            if (!locked) {
                slapi_rwlock_unlock(global_rwlock);
                slapi_td_set_plugin_unlocked();
            }
        }
    }

done:

    if (!removed && rc == 0) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Plugin delete failed: could not find plugin in the global list.");
        slapi_log_err(SLAPI_LOG_ERR,
                      "plugin_delete", "Did not find plugin (%s) in the global list.\n",
                      slapi_entry_get_dn_const(plugin_entry));
        rc = -1;
    }

    return rc;
}


/* set default configuration parameters */
static void
plugin_config_init(struct pluginconfig *config)
{
    PR_ASSERT(config);

    ptd_init(&config->plgc_target_subtrees);
    ptd_init(&config->plgc_excluded_target_subtrees);
    ptd_init(&config->plgc_bind_subtrees);
    ptd_init(&config->plgc_excluded_bind_subtrees);
    config->plgc_schema_check = PLGC_ON;
    config->plgc_invoke_for_replop = PLGC_ON;
    /* currently, we leave it up to plugin, but don't actually tell plugins that they can choose.
       We want changes to always be logged by regular plugins to avoid data inconsistency, but we
       want to allow internal plugins like replication to make the decision.*/
    config->plgc_log_change = PLGC_UPTOPLUGIN;
    config->plgc_log_access = PLGC_OFF;
    config->plgc_log_audit = PLGC_OFF;
}

static int
plugin_config_set_action(int *action, char *value)
{
    PR_ASSERT(action);
    PR_ASSERT(value);

    if (strcasecmp(value, "on") == 0) {
        *action = PLGC_ON;
    } else if (strcasecmp(value, "off") == 0) {
        *action = PLGC_OFF;
    } else if (strcasecmp(value, "uptoplugin") == 0) {
        *action = PLGC_UPTOPLUGIN;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_config_set_action",
                      "Invalid action %s\n", value);
        return -1;
    }

    return 0;
}

static void
plugin_config_cleanup(struct pluginconfig *config)
{
    PR_ASSERT(config);

    ptd_cleanup(&config->plgc_target_subtrees);
    ptd_cleanup(&config->plgc_excluded_target_subtrees);
    ptd_cleanup(&config->plgc_bind_subtrees);
    ptd_cleanup(&config->plgc_excluded_bind_subtrees);
}

static struct pluginconfig *
plugin_get_config(struct slapdplugin *plugin)
{
    struct slapdplugin *temp = plugin;

    PR_ASSERT(plugin);

    while (temp->plg_group) {
        temp = temp->plg_group;
    }

    return &(temp->plg_conf);
}

static PRBool
plugin_invoke_plugin_pb(struct slapdplugin *plugin, int operation, Slapi_PBlock *pb)
{
    Slapi_DN *target_spec;
    Operation *pb_op = NULL;
    PRBool rc;

    PR_ASSERT(plugin);
    PR_ASSERT(pb);

    /* we always allow initialization and cleanup operations */
    if (operation == SLAPI_PLUGIN_START_FN ||
        operation == SLAPI_PLUGIN_POSTSTART_FN ||
        operation == SLAPI_PLUGIN_CLOSE_FN ||
        operation == SLAPI_PLUGIN_CLEANUP_FN ||
        operation == SLAPI_PLUGIN_BE_PRE_CLOSE_FN ||
        operation == SLAPI_PLUGIN_BE_POST_OPEN_FN ||
        operation == SLAPI_PLUGIN_BE_POST_EXPORT_FN ||
        operation == SLAPI_PLUGIN_BE_POST_IMPORT_FN)
        return PR_TRUE;

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    if (pb_op == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_invoke_plugin_pb", "pb_op is NULL");
        PR_ASSERT(0);
        return PR_FALSE;
    }

    target_spec = operation_get_target_spec(pb_op);

    rc = plugin_invoke_plugin_sdn(plugin, operation, pb, target_spec);

    return rc;
}

PRBool
plugin_invoke_plugin_sdn(struct slapdplugin *plugin, int operation __attribute__((unused)), Slapi_PBlock *pb, Slapi_DN *target_spec)
{
    PluginTargetData *ptd;
    PluginTargetData *excludedPtd;
    struct pluginconfig *config;
    Slapi_Backend *be;
    int isroot;
    PRBool islocal;
    PRBool bindop;
    unsigned long op;
    ber_tag_t method = LBER_ERROR;

    PR_ASSERT(plugin);
    if (!pb) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_invoke_plugin_sdn", "NULL pblock.\n");
        return PR_FALSE;
    }

    Operation *pb_op;
    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);

    /* get configuration from the group plugin if necessary */
    config = plugin_get_config(plugin);
    slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);
    /* check if plugin is configured to service replicated operations */
    if (!config->plgc_invoke_for_replop) {
        int repl_op;

        slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
        if (repl_op) {
            return PR_FALSE;
        }
    }

    if (pb_op) {
        op = operation_get_type(pb_op);

        if (op == SLAPI_OPERATION_BIND || op == SLAPI_OPERATION_UNBIND) {
            bindop = PR_TRUE;
        } else {
            bindop = PR_FALSE;
        }

        slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    } else {
        bindop = PR_FALSE;
        isroot = 1;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    /* determine whether data are local or remote    */
    /* remote if chaining backend or default backend */

    if (be != NULL) {
        islocal = !(slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA));
    } else {
        islocal = be != defbackend_get_backend();
    }

    if (bindop) {
        ptd = &(config->plgc_bind_subtrees);
        excludedPtd = &(config->plgc_excluded_bind_subtrees);
    } else {
        ptd = &(config->plgc_target_subtrees);
        excludedPtd = &(config->plgc_excluded_target_subtrees);
    }

    if (plugin_matches_operation(target_spec, excludedPtd, bindop, isroot, islocal, method) == PR_TRUE) {
        return PR_FALSE;
    }

    return plugin_matches_operation(target_spec, ptd, bindop, isroot, islocal, method);
}

/* this interface is exposed to be used by internal operations.
 */
char *
plugin_get_dn(const struct slapdplugin *plugin)
{
    char *plugindn = NULL;
    char *pattern = "cn=%s," PLUGIN_BASE_DN; /* cn=plugins,cn=config */

    if (plugin == NULL) /* old plugin that does not pass identity - use default */
        plugin = &global_default_plg;

    if (plugin->plg_name == NULL)
        return NULL;

    /* plg_name is normalized in plugin_setup. So, we can use smprintf */
    plugindn = slapi_ch_smprintf(pattern, plugin->plg_name);
    if (NULL == plugindn) {
        slapi_log_err(SLAPI_LOG_ERR, "plugin_get_dn",
                      "Failed to create plugin dn (plugin name: %s)\n", plugin->plg_name);
        return NULL;
    }
    return plugindn;
}

static PRBool
plugin_is_global(const PluginTargetData *ptd)
{
    /* plugin is considered to be global if it is invoked for
       global data, local data and anonymous bind (bind target
       data only). We don't include directory manager here
       as it is considered to be part of local data */
    return (ptd_is_special_data_set(ptd, PLGC_DATA_LOCAL) &&
            ptd_is_special_data_set(ptd, PLGC_DATA_REMOTE) &&
            ptd_is_special_data_set(ptd, PLGC_DATA_BIND_ANONYMOUS) &&
            ptd_is_special_data_set(ptd, PLGC_DATA_BIND_ROOT));
}

static void
plugin_set_global(PluginTargetData *ptd)
{
    PR_ASSERT(ptd);

    /* plugin is global if it is allowed access to all data */
    ptd_set_special_data(ptd, PLGC_DATA_LOCAL);
    ptd_set_special_data(ptd, PLGC_DATA_REMOTE);
    ptd_set_special_data(ptd, PLGC_DATA_BIND_ANONYMOUS);
    ptd_set_special_data(ptd, PLGC_DATA_BIND_ROOT);
}

static void
plugin_set_default_access(struct pluginconfig *config)
{
    /* by default, plugins are invoked if dn is local for bind operations,
       and for all requests for all other operations */
    PR_ASSERT(config);

    plugin_set_global(&config->plgc_target_subtrees);
    ptd_set_special_data(&config->plgc_bind_subtrees, PLGC_DATA_LOCAL);
    ptd_set_special_data(&config->plgc_bind_subtrees, PLGC_DATA_REMOTE);
}

/* determine whether operation should be allowed based on plugin configuration */
PRBool
plugin_allow_internal_op(Slapi_DN *target_spec, struct slapdplugin *plugin)
{
    struct pluginconfig *config = plugin_get_config(plugin);
    Slapi_Backend *be;
    int islocal;

    if (plugin_is_global(&config->plgc_excluded_target_subtrees))
        return PR_FALSE;

    if (plugin_is_global(&config->plgc_target_subtrees))
        return PR_TRUE;

    /* ONREPL - we do be_select to decide whether the request is for local
                or remote data. We might need to reconsider how to do this
                for performance reasons since be_select will be done again
                once the operation goes through */
    be = slapi_be_select(target_spec);

    /* determine whether data are local or remote    */
    /* remote if chaining backend or default backend */

    if (be != NULL) {
        islocal = !(slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA));
    } else {
        islocal = be != defbackend_get_backend();
    }

    /* SIMPLE auth method sends us through original code path in plugin_mathches_operation */

    if (plugin_matches_operation(target_spec, &config->plgc_excluded_target_subtrees,
                                 PR_FALSE, PR_FALSE, islocal, LDAP_AUTH_SIMPLE) == PR_TRUE) {
        return PR_FALSE;
    }

    return plugin_matches_operation(target_spec, &config->plgc_target_subtrees,
                                    PR_FALSE, PR_FALSE, islocal, LDAP_AUTH_SIMPLE);
}

static PRBool
plugin_matches_operation(Slapi_DN *target_spec, PluginTargetData *ptd, PRBool bindop, PRBool isroot, PRBool islocal, ber_tag_t method)
{
    int cookie;
    Slapi_DN *subtree;

    /* check for special cases */

    if (plugin_is_global(ptd))
        return PR_TRUE;

    /* if method is SASL we can have a null DN so bypass this check*/
    if (method != LDAP_AUTH_SASL) {
        if (bindop && target_spec && (slapi_sdn_get_dn(target_spec) == NULL ||
                                      slapi_sdn_get_dn(target_spec)[0] == '\0')) {
            return (ptd_is_special_data_set(ptd, PLGC_DATA_BIND_ANONYMOUS));
        }
    }

    /* check for root bind */
    if (bindop && isroot) {
        return (ptd_is_special_data_set(ptd, PLGC_DATA_BIND_ROOT));
    }

    /* check for local data */
    if (ptd_is_special_data_set(ptd, PLGC_DATA_LOCAL) && islocal) {
        return PR_TRUE;
    }

    /* check for remote data */
    if (ptd_is_special_data_set(ptd, PLGC_DATA_REMOTE) && !islocal) {
        return (PR_TRUE);
    }

    subtree = ptd_get_first_subtree(ptd, &cookie);
    while (subtree) {
        if (slapi_sdn_issuffix(target_spec, subtree))
            return (PR_TRUE);

        subtree = ptd_get_next_subtree(ptd, &cookie);
    }

    return PR_FALSE;
}

/* build operation action bitmap based on plugin configuration and actions specified for the operation */
int
plugin_build_operation_action_bitmap(int input_actions __attribute__((unused)), const struct slapdplugin *plugin)
{
    int result_actions = 0;

    /* old plugin that does not pass its identity to the operation */
    if (plugin == NULL)
        plugin = &global_default_plg;

    if (plugin->plg_conf.plgc_log_access || config_get_plugin_logging())
        result_actions |= OP_FLAG_ACTION_LOG_ACCESS;

    if (plugin->plg_conf.plgc_log_audit || config_get_plugin_logging())
        result_actions |= OP_FLAG_ACTION_LOG_AUDIT;

    /*
     * OP_FLAG_ACTION_INVOKE_FOR_REPLOP is now used only by URP code.
     * If someday this code needs to reclaim the flag, it has to use
     * another flag to avoid the conflict with URP code.
     *
     * if (plugin->plg_conf.plgc_invoke_for_replop)
     *    result_actions |= OP_FLAG_ACTION_INVOKE_FOR_REPLOP;
     */

    switch (plugin->plg_conf.plgc_schema_check) {
    case PLGC_OFF:
        result_actions &= ~OP_FLAG_ACTION_SCHEMA_CHECK;
        break;

    case PLGC_ON:
        result_actions |= OP_FLAG_ACTION_SCHEMA_CHECK;
        break;

    case PLGC_UPTOPLUGIN:
        break;

    default:
        PR_ASSERT(PR_FALSE);
    }

    switch (plugin->plg_conf.plgc_log_change) {
    case PLGC_OFF:
        result_actions &= ~OP_FLAG_ACTION_LOG_CHANGES;
        break;

    case PLGC_ON:
        result_actions |= OP_FLAG_ACTION_LOG_CHANGES;
        break;

    case PLGC_UPTOPLUGIN:
        break;

    default:
        PR_ASSERT(PR_FALSE);
    }

    return result_actions;
}

const struct slapdplugin *
plugin_get_server_plg(void)
{
    if (!global_server_plg_initialised) {
        global_server_plg.plg_name = "server";
        plugin_set_global(&global_server_plg.plg_conf.plgc_target_subtrees);
        global_server_plg.plg_conf.plgc_log_access = 1;
        global_server_plg.plg_conf.plgc_log_audit = 1;
        global_server_plg.plg_conf.plgc_schema_check = 1;
        global_server_plg.plg_conf.plgc_log_change = 1;
        global_server_plg_initialised = 1;
        global_server_plg_initialised = 1;
    }
    return &global_server_plg;
}

struct slapi_componentid *
plugin_get_default_component_id(void)
{

    if (!global_server_plg_id_initialised) {
        global_server_id_plg.sci_plugin = plugin_get_server_plg();
        global_server_id_plg.sci_component_name =
            plugin_get_dn(global_server_id_plg.sci_plugin);
        global_server_plg_id_initialised = 1;
    }
    return &global_server_id_plg;
}

static void
default_plugin_init(void)
{
    global_default_plg.plg_name = "old plugin";
    plugin_config_init(&global_default_plg.plg_conf);
    plugin_set_default_access(&global_default_plg.plg_conf);
}

/* functions to manipulate PluginTargetData type */
static void
ptd_init(PluginTargetData *ptd)
{
    PR_ASSERT(ptd);

    dl_init(&ptd->subtrees, 0 /* initial count */);
    memset(&ptd->special_data, 0, sizeof(ptd->special_data));
}

static void
ptd_cleanup(PluginTargetData *ptd)
{
    PR_ASSERT(ptd);

    dl_cleanup(&ptd->subtrees, (FREEFN)slapi_sdn_free);
    memset(&ptd->special_data, 0, sizeof(ptd->special_data));
}

static void
ptd_add_subtree(PluginTargetData *ptd, Slapi_DN *subtree)
{
    PR_ASSERT(ptd);
    PR_ASSERT(subtree);

    dl_add(&ptd->subtrees, subtree);
}

static void
ptd_set_special_data(PluginTargetData *ptd, int type)
{
    PR_ASSERT(ptd);
    PR_ASSERT(type >= 0 && type < PLGC_DATA_MAX);

    ptd->special_data[type] = PR_TRUE;
}

static Slapi_DN *
ptd_get_first_subtree(const PluginTargetData *ptd, int *cookie)
{
    PR_ASSERT(ptd);

    return dl_get_first(&ptd->subtrees, cookie);
}

static Slapi_DN *
ptd_get_next_subtree(const PluginTargetData *ptd, int *cookie)
{
    PR_ASSERT(ptd);

    return dl_get_next(&ptd->subtrees, cookie);
}

static PRBool
ptd_is_special_data_set(const PluginTargetData *ptd, int type)
{
    PR_ASSERT(ptd);
    PR_ASSERT(type >= 0 && type < PLGC_DATA_MAX);

    return ptd->special_data[type];
}

int
ptd_get_subtree_count(const PluginTargetData *ptd)
{
    PR_ASSERT(ptd);

    return dl_get_count(&ptd->subtrees);
}

/* needed by command-line tasks to find an instance's plugin */
struct slapdplugin *
plugin_get_by_name(char *name)
{
    int x;
    struct slapdplugin *plugin;

    for (x = 0; x < PLUGIN_LIST_GLOBAL_MAX; x++) {
        for (plugin = global_plugin_list[x]; plugin; plugin = plugin->plg_next) {
            if (!strcmp(name, plugin->plg_name)) {
                return plugin;
            }
        }
    }

    return NULL;
}

struct slapi_componentid *
generate_componentid(struct slapdplugin *pp, char *name)
{
    struct slapi_componentid *idp;

    idp = (struct slapi_componentid *)slapi_ch_calloc(1, sizeof(*idp));
    if (pp) {
        idp->sci_plugin = pp;
    } else {
        idp->sci_plugin = (struct slapdplugin *)plugin_get_server_plg();
    }

    if (name) {
        idp->sci_component_name = slapi_ch_strdup(name);
    } else {
        /* Use plugin dn */
        idp->sci_component_name = plugin_get_dn(idp->sci_plugin);
    }

    if (idp->sci_component_name) {
        slapi_dn_normalize(idp->sci_component_name);
    }
    return idp;
}

void
release_componentid(struct slapi_componentid *id)
{
    if (id) {
        if (id->sci_component_name) {
            slapi_ch_free((void **)&id->sci_component_name);
            id->sci_component_name = NULL;
        }
        slapi_ch_free((void **)&id);
    }
}

/* used in main.c if -V flag is given */

static void
slapd_print_plugin_version(
    struct slapdplugin *plg,
    struct slapdplugin *prev)
{
    if (plg == NULL || plg->plg_libpath == NULL)
        return;

    /* same library as previous - don't print twice */
    if (prev != NULL && prev->plg_libpath != NULL) {
        if (strcmp(prev->plg_libpath, plg->plg_libpath) == 0) {
            return;
        }
    }

    printf("%s: %s\n",
           plg->plg_libpath,
           plg->plg_desc.spd_version ? plg->plg_desc.spd_version : "");
}

static void
slapd_print_pluginlist_versions(struct slapdplugin *plg)
{
    struct slapdplugin *p, *prev = NULL;

    for (p = plg; p != NULL; p = p->plg_next) {
        slapd_print_plugin_version(p, prev);
        prev = p;
    }
}

void
plugin_print_versions(void)
{
    int i;

    for (i = 0; i < PLUGIN_LIST_GLOBAL_MAX; i++) {
        slapd_print_pluginlist_versions(get_plugin_list(i));
    }
}

/*
 * Prints a list of plugins in execution order for each
 * plug-in type.  This will only be printed at the
 * SLAPI_LOG_PLUGIN log level.
 */
void
plugin_print_lists(void)
{
    int i;
    struct slapdplugin *list = NULL;
    struct slapdplugin *tmp = NULL;

    for (i = 0; i < PLUGIN_LIST_GLOBAL_MAX; i++) {
        if ((list = get_plugin_list(i))) {
            slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_print_lists",
                          "---- Plugin List (type %d) ----\n", i);
            for (tmp = list; tmp; tmp = tmp->plg_next) {
                slapi_log_err(SLAPI_LOG_PLUGIN, "plugin_print_lists", "  %s (precedence: %d)\n",
                              tmp->plg_name, tmp->plg_precedence);
            }
        }
    }
}

/*
 * check the spedified plugin entry and its nssladp-pluginEnabled value
 * Return Value: 1 if the plugin is on.
 *             : 0 otherwise.
 */
int
plugin_enabled(const char *plugin_name, void *identity)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL, **ep = NULL;
    Slapi_Value *on_off = slapi_value_new_string("on");
    char *filter = NULL;
    int rc = 0; /* disabled, by default */

    filter = slapi_filter_sprintf("cn=%s%s", ESC_NEXT_VAL, plugin_name);
    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, PLUGIN_BASE_DN, LDAP_SCOPE_ONELEVEL,
                                 filter, NULL, 0, NULL, NULL, identity, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (LDAP_SUCCESS != rc) { /* plugin is not available */
        rc = 0;               /* disabled, by default */
        goto bail;
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    for (ep = entries; ep && *ep; ep++) {
        if (slapi_entry_attr_has_syntax_value(*ep, "nsslapd-pluginEnabled", on_off)) {
            rc = 1; /* plugin is on */
            goto bail;
        }
    }

bail:
    slapi_value_free(&on_off);
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_ch_free_string(&filter);

    return rc;
}

/*
 * Set given "type: attr" to the plugin default config entry
 * (cn=plugin default config,cn=config) unless the same "type: attr" pair
 * already exists in the entry.
 */
int
slapi_set_plugin_default_config(const char *type, Slapi_Value *value)
{
    Slapi_PBlock *pb;
    Slapi_Entry **entries = NULL;
    int rc = LDAP_SUCCESS;
    char **search_attrs = NULL; /* used by search */

    if (NULL == type || '\0' == *type || NULL == value) { /* nothing to do */
        return rc;
    }

    pb = slapi_pblock_new();
    charray_add(&search_attrs, slapi_ch_strdup(type));

    /* cn=plugin default config,cn=config */
    slapi_search_internal_set_pb(pb,
                                 SLAPI_PLUGIN_DEFAULT_CONFIG, /* Base DN (normalized) */
                                 LDAP_SCOPE_BASE,
                                 "(objectclass=*)",
                                 search_attrs, /* Attrs */
                                 0,            /* AttrOnly */
                                 NULL,         /* Controls */
                                 NULL,         /* UniqueID */
                                 (void *)plugin_get_default_component_id(),
                                 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (LDAP_SUCCESS == rc && entries && *entries) {
        /* plugin default config entry exists */
        int exists = 0;
        Slapi_Attr *attr = NULL;
        rc = slapi_entry_attr_find(*entries, type, &attr);

        if (0 == rc) { /* type exists in the entry */
            if (0 ==
                slapi_attr_value_find(attr, slapi_value_get_berval(value))) {
                /* value exists in the entry; we don't have to do anything. */
                exists = 1;
            }
        }
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);

        if (!exists) {
            /* The argument attr is not in the plugin default config.
             * Let's add it. */
            Slapi_Mods smods;
            Slapi_Value *va[2];

            va[0] = value;
            va[1] = NULL;
            slapi_mods_init(&smods, 1);
            slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va);

            pb = slapi_pblock_new();
            slapi_modify_internal_set_pb(pb, SLAPI_PLUGIN_DEFAULT_CONFIG,
                                         slapi_mods_get_ldapmods_byref(&smods),
                                         NULL, NULL, /* UniqueID */
                                         (void *)plugin_get_default_component_id(),
                                         0 /* Flags */);
            slapi_modify_internal_pb(pb);
            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
            slapi_mods_done(&smods);
            slapi_pblock_destroy(pb);
        }
    } else { /* cn=plugin default config does not exist. Let's add it. */
        Slapi_Mods smods;
        Slapi_Value *va[2];

        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);

        va[0] = value;
        va[1] = NULL;
        slapi_mods_init(&smods, 1);

        slapi_mods_add_string(&smods, LDAP_MOD_ADD, "objectClass", "top");
        slapi_mods_add_string(&smods, LDAP_MOD_ADD, "objectClass",
                              "extensibleObject");
        slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va);

        pb = slapi_pblock_new();
        slapi_add_internal_set_pb(pb, SLAPI_PLUGIN_DEFAULT_CONFIG,
                                  slapi_mods_get_ldapmods_byref(&smods), NULL,
                                  (void *)plugin_get_default_component_id(),
                                  0 /* Flags */);
        slapi_add_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        slapi_mods_done(&smods);
        slapi_pblock_destroy(pb);
    }
    charray_free(search_attrs);

    return rc;
}

/*
 * Get attribute values of given type from the plugin default config entry
 * (cn=plugin default config,cn=config).
 *
 * Caller is responsible to free attrs by slapi_valueset_free.
 */
int
slapi_get_plugin_default_config(char *type, Slapi_ValueSet **valueset)
{
    Slapi_Entry **entries = NULL;
    int rc = LDAP_PARAM_ERROR;
    char **search_attrs = NULL; /* used by search */

    if (NULL == type || '\0' == *type || NULL == valueset) { /* nothing to do */
        return rc;
    }

    charray_add(&search_attrs, slapi_ch_strdup(type));

    Slapi_PBlock *pb = slapi_pblock_new();

    /* cn=plugin default config,cn=config */
    slapi_search_internal_set_pb(pb,
                                 SLAPI_PLUGIN_DEFAULT_CONFIG, /* Base DN (normalized) */
                                 LDAP_SCOPE_BASE,
                                 "(objectclass=*)",
                                 search_attrs, /* Attrs */
                                 0,            /* AttrOnly */
                                 NULL,         /* Controls */
                                 NULL,         /* UniqueID */
                                 (void *)plugin_get_default_component_id(),
                                 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (LDAP_SUCCESS == rc && entries && *entries) {
        /* default config entry exists */
        /* retrieve attribute values from the entry */
        Slapi_Attr *attr = NULL;
        rc = slapi_entry_attr_find(*entries, type, &attr);
        if (0 == rc) { /* type value exists */
            rc = slapi_attr_get_valueset(attr, valueset);
        } else {
            rc = LDAP_NO_SUCH_ATTRIBUTE;
        }
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    charray_free(search_attrs);

    return rc;
}

void
slapi_set_plugin_open_rootdn_bind(Slapi_PBlock *pb)
{
    struct slapdplugin *plug;
    slapi_pblock_get(pb, SLAPI_PLUGIN, &plug);
    struct pluginconfig *config = &plug->plg_conf;

    ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_BIND_ROOT);
}

PRBool
slapi_disorderly_shutdown(PRBool set)
{
    static PRBool is_disorderly_shutdown = PR_FALSE;

    if (set) {
        is_disorderly_shutdown = PR_TRUE;
    }
    return (is_disorderly_shutdown);
}

/*
 *  Plugin operation counters
 *
 *  Since most plugins can now be stopped and started dynamically we need
 *  to take special care when calling a close function.  Since many plugins
 *  use global locks and data structures, these can not be freed/destroyed
 *  while there are active operations using them.
 */

void
slapi_plugin_op_started(void *p)
{
    struct slapdplugin *plugin = (struct slapdplugin *)p;

    if (plugin) {
        slapi_counter_increment(plugin->plg_op_counter);
    }
}

void
slapi_plugin_op_finished(void *p)
{
    struct slapdplugin *plugin = (struct slapdplugin *)p;

    if (plugin) {
        slapi_counter_decrement(plugin->plg_op_counter);
    }
}

/*
 * Waits for the operation counter to hit zero
 */
void
plugin_op_all_finished(struct slapdplugin *p)
{
    while (p && slapi_counter_get_value(p->plg_op_counter) > 0) {
        DS_Sleep(PR_MillisecondsToInterval(100));
    }
}

void
plugin_set_started(struct slapdplugin *p)
{
    p->plg_started = 1;
    p->plg_stopped = 0;
}

void
plugin_set_stopped(struct slapdplugin *p)
{
    /*
     * We do not set "plg_stopped" here, because that is only used
     * once the plugin has called its CLOSE function.  Setting
     * "plg_started" to 0 will prevent new operations from calling
     * the plugin.
     */
    p->plg_started = 0;
}

int
slapi_plugin_running(Slapi_PBlock *pb)
{
    int rc = 0;

    struct slapdplugin *plugin;
    slapi_pblock_get(pb, SLAPI_PLUGIN, &plugin);

    if (plugin) {
        rc = plugin->plg_started;
    }

    return rc;
}

/*
 *  Allow "database" plugins to call the backend/backend txn plugins.
 */
int
slapi_plugin_call_preop_be_plugins(Slapi_PBlock *pb, int function)
{
    int be_func, betxn_func;
    int rc = 0;

    switch (function) {
    case SLAPI_PLUGIN_ADD_OP:
        be_func = SLAPI_PLUGIN_BE_PRE_ADD_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN;
        break;
    case SLAPI_PLUGIN_MOD_OP:
        be_func = SLAPI_PLUGIN_BE_PRE_MODIFY_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN;
        break;
    case SLAPI_PLUGIN_MODRDN_OP:
        be_func = SLAPI_PLUGIN_BE_PRE_MODRDN_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN;
        break;
    case SLAPI_PLUGIN_DEL_OP:
        be_func = SLAPI_PLUGIN_BE_PRE_DELETE_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN;
        break;
    default:
        /* invalid function */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_plugin_call_preop_be_plugins",
                      "Invalid function specified - backend plugins will not be called.\n");
        return 0;
    }

    /*
     * Call the be preop plugins.
     */
    plugin_call_plugins(pb, be_func);
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);

    /*
     * Call the betxn preop plugins.
     */
    if (rc == LDAP_SUCCESS) {
        plugin_call_plugins(pb, betxn_func);
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    }

    return rc;
}

int
slapi_plugin_call_postop_be_plugins(Slapi_PBlock *pb, int function)
{
    int be_func, betxn_func;
    int rc = 0;

    switch (function) {
    case SLAPI_PLUGIN_ADD_OP:
        be_func = SLAPI_PLUGIN_BE_POST_ADD_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
        break;
    case SLAPI_PLUGIN_MOD_OP:
        be_func = SLAPI_PLUGIN_BE_POST_MODIFY_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        break;
    case SLAPI_PLUGIN_MODRDN_OP:
        be_func = SLAPI_PLUGIN_BE_POST_MODRDN_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        break;
    case SLAPI_PLUGIN_DEL_OP:
        be_func = SLAPI_PLUGIN_BE_POST_DELETE_FN;
        betxn_func = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
        break;
    default:
        /* invalid function */
        slapi_log_err(SLAPI_LOG_ERR, "slapi_plugin_call_postop_be_plugins",
                      "Invalid function specified - backend plugins will not be called.\n");
        return 0;
    }

    /* next, give the be txn plugins a crack at it */;
    plugin_call_plugins(pb, betxn_func);
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);

    /* finally, give the be plugins a crack at it */
    plugin_call_plugins(pb, be_func);
    if (rc == LDAP_SUCCESS) {
        slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    }

    return rc;
}
