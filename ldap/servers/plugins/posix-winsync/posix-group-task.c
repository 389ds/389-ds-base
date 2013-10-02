#include "slapi-plugin.h"
#include "nspr.h"
#include <string.h>

#include "posix-wsp-ident.h"
#include "posix-group-func.h"

typedef struct _task_data
{
    char *dn; /* search base */
    char *filter_str; /* search filter */
} task_data;

typedef struct _cb_data
{
    char *dn;
    void *txn;
} cb_data;
/*
 typedef struct _posix_group_task_data
 {
 POSIX_WinSync_Config *config;
 Slapi_Value *memberdn_val;
 Slapi_ValueSet **uidvals;
 void *txn;
 } posix_group_data_data;
 */

Slapi_Value **
valueset_get_valuearray(const Slapi_ValueSet *vs); /* stolen from proto-slap.h */

/* interface function */
int
posix_group_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode,
    char *returntext, void *arg);

Slapi_Entry *
getEntry(const char *udn, char **attrs);

static void
posix_group_task_destructor(Slapi_Task *task);
static void
posix_group_fixup_task_thread(void *arg);
static int
posix_group_fix_memberuid_callback(Slapi_Entry *e, void *callback_data);

/* extract a single value from the entry (as a string) -- if it's not in the
 * entry, the default will be returned (which can be NULL).
 * you do not need to free anything returned by this.
 */
static const char *
fetch_attr(Slapi_Entry *e, const char *attrname, const char *default_val)
{
    Slapi_Attr *attr;
    Slapi_Value *val = NULL;

    if (slapi_entry_attr_find(e, attrname, &attr) != 0)
        return default_val;
    slapi_attr_first_value(attr, &val);
    return slapi_value_get_string(val);
}

/* e configEntry */
int
posix_group_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode,
    char *returntext, void *arg)
{
    PRThread *thread = NULL;
    int rv = SLAPI_DSE_CALLBACK_OK;
    task_data *mytaskdata = NULL;
    Slapi_Task *task = NULL;
    const char *filter;
    const char *dn = 0;

    *returncode = LDAP_SUCCESS;


    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: ==>\n");

    /* get arg(s) */
    /* default: set replication basedn */
    if ((dn = fetch_attr(e, "basedn", slapi_sdn_get_dn(posix_winsync_config_get_suffix()))) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: retrieved basedn: %s\n", dn);

    if ((filter = fetch_attr(e, "filter", "(objectclass=ntGroup)")) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: retrieved filter: %s\n", filter);

    /* setup our task data */
    mytaskdata = (task_data*) slapi_ch_malloc(sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mytaskdata->dn = slapi_ch_strdup(dn);
    mytaskdata->filter_str = slapi_ch_strdup(filter);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: task data allocated\n");

    /* allocate new task now */
    char * ndn = slapi_entry_get_ndn(e);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: creating task object: %s\n",
                    ndn);

    task = slapi_new_task(ndn);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: task object created\n");

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, posix_group_task_destructor);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: task destructor set\n");

    /* Stash a pointer to our data in the task */
    slapi_task_set_data(task, mytaskdata);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: task object initialized\n");

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, posix_group_fixup_task_thread, (void *) task,
                             PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                             SLAPD_DEFAULT_THREAD_STACKSIZE);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: thread created\n");

    if (thread == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME,
                        "unable to create task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_task_finish(task, *returncode);
    } else {
        rv = SLAPI_DSE_CALLBACK_OK;
    }

out: 
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "posix_group_task_add: <==\n");

    return rv;
}

static void
posix_group_task_destructor(Slapi_Task *task)
{
    if (task) {
        task_data *mydata = (task_data *) slapi_task_get_data(task);
        if (mydata) {
            slapi_ch_free_string(&mydata->dn);
            slapi_ch_free_string(&mydata->filter_str);
            /* Need to cast to avoid a compiler warning */
            slapi_ch_free((void **) &mydata);
        }
    }
}

#ifdef USE_POSIX_GROUP_DEL_MEMBERUID
static int
posix_group_del_memberuid_callback(Slapi_Entry *e, void *callback_data)
{
    int rc = 0;
    LDAPMod mod;
    LDAPMod *mods[2];
    char *val[2];
    Slapi_PBlock *mod_pb = 0;
    cb_data *the_cb_data = (cb_data *) callback_data;

    mod_pb = slapi_pblock_new();

    mods[0] = &mod;
    mods[1] = 0;

    val[0] = 0; /* all */
    val[1] = 0;

    mod.mod_op = LDAP_MOD_DELETE;
    mod.mod_type = "memberuid";
    mod.mod_values = val;

    slapi_modify_internal_set_pb_ext(mod_pb, slapi_entry_get_sdn(e), mods, 0, 0,
                                     posix_winsync_get_plugin_identity(), 0);

    slapi_pblock_set(mod_pb, SLAPI_TXN, the_cb_data->txn);
    slapi_modify_internal_pb(mod_pb);

    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    slapi_pblock_destroy(mod_pb);

    return rc;
}
#endif

static int
posix_group_fix_memberuid(char *dn, char *filter_str, void *txn)
{
    int rc = 0;
    struct _cb_data callback_data = { dn, txn };
    Slapi_PBlock *search_pb = slapi_pblock_new();

    /* char *attrs[]={"uniquemember","memberuid",NULL}; */

    slapi_search_internal_set_pb(search_pb, dn, LDAP_SCOPE_SUBTREE, filter_str, 0, 0, 0, 0,
                                 posix_winsync_get_plugin_identity(), 0);

    slapi_pblock_set(search_pb, SLAPI_TXN, txn); /* set transaction id */
    rc = slapi_search_internal_callback_pb(search_pb, &callback_data, 0,
                                           posix_group_fix_memberuid_callback, 0);

    slapi_pblock_destroy(search_pb);

    return rc;
}

/* posix_group_fix_memberuid_callback()
 * Add initial and/or fix up broken group list in entry
 *
 * 1. forall uniquemember search if posixAccount ? add uid : ""
 */
static int
posix_group_fix_memberuid_callback(Slapi_Entry *e, void *callback_data)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "_fix_memberuid ==>\n");
    cb_data *the_cb_data = (cb_data *) callback_data;

    int rc;
    Slapi_Attr *muid_attr = NULL;
    Slapi_Value *v = NULL;

    Slapi_Mods *smods = slapi_mods_new();

    char *dn = slapi_entry_get_dn(e);
    Slapi_DN *sdn = slapi_entry_get_sdn(e);
    LDAPMod **mods = NULL;

/* Clean out memberuids and dsonlymemberuids without a valid referant */
    rc = slapi_entry_attr_find(e, "memberuid", &muid_attr);
    if (rc == 0 && muid_attr) {
        Slapi_PBlock *search_pb = slapi_pblock_new();

        Slapi_Attr *dsmuid_attr = NULL;
        Slapi_ValueSet *dsmuid_vs = NULL;

        char *attrs[] = { "uid", NULL };

        rc = slapi_entry_attr_find(e, "dsonlymemberuid", &dsmuid_attr);
        if (rc == 0 && dsmuid_attr) {
            slapi_attr_get_valueset(dsmuid_attr, &dsmuid_vs);
        }

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_fix_memberuid scan for orphaned memberuids\n");

        int i;
        for (i = slapi_attr_first_value(muid_attr, &v); i != -1;
             i = slapi_attr_next_value(muid_attr, i, &v)) {
            char *muid = (char *)slapi_value_get_string(v);

            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "_fix_memberuid iterating memberuid: %s\n",
                            muid);

            size_t vallen = muid ? strlen(muid) : 0;
            char *filter_escaped_value = slapi_escape_filter_value(muid, vallen);
            char *filter = slapi_ch_smprintf("(uid=%s)", filter_escaped_value);
            slapi_ch_free_string(&filter_escaped_value);

            Slapi_Entry **search_entries = NULL;

            slapi_search_internal_set_pb(search_pb,
                                         the_cb_data->dn,
                                         LDAP_SCOPE_SUBTREE,
                                         filter,
                                         attrs, 0, NULL, NULL,
                                         posix_winsync_get_plugin_identity(), 0);

            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "_fix_memberuid searching %s with filter: %s\n",
                            the_cb_data->dn, filter);

            rc = slapi_search_internal_pb(search_pb);

            slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &search_entries);

            if (!search_entries || !search_entries[0]) {
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "_fix_memberuid Adding bad memberuid %s\n",
                                slapi_value_get_string(v));

                slapi_mods_add_string(smods, LDAP_MOD_DELETE, "memberuid", slapi_value_get_string(v));

                if (dsmuid_vs && slapi_valueset_find(dsmuid_attr, dsmuid_vs, v)) {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "_fix_memberuid Adding bad dsonlymemberuid %s\n",
                                    slapi_value_get_string(v));

                    slapi_mods_add_string(smods, LDAP_MOD_DELETE, "dsonlymemberuid", slapi_value_get_string(v));
                }
            }

            slapi_free_search_results_internal(search_pb);
            slapi_pblock_init(search_pb);
            slapi_ch_free_string(&filter);
        }

        if (dsmuid_vs) {
            slapi_valueset_free(dsmuid_vs); dsmuid_vs = NULL;
        }

        slapi_pblock_destroy(search_pb); search_pb = NULL;
    }

    /* Cleanup uniquemembers without a referent, and verify memberuid otherwise */
    Slapi_Attr *obj_attr = NULL;
    rc = slapi_entry_attr_find(e, "uniquemember", &obj_attr);
    if (rc == 0 && obj_attr) {
        int fixMembership = 0;
        Slapi_ValueSet *bad_ums = NULL;

        int i;
        Slapi_Value * uniqval = NULL;            /* uniquemeber Attribute values          */

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_fix_memberuid scan uniquemember, group %s\n", dn);
        for (i = slapi_attr_first_value(obj_attr, &uniqval); i != -1;
             i = slapi_attr_next_value(obj_attr, i, &uniqval)) {

            const char *member = slapi_value_get_string(uniqval);
            char *attrs[] = { "uid", "objectclass", NULL };
            Slapi_Entry *child = getEntry(member, attrs);

            if (!child) {
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "_fix_memberuid orphaned uniquemember found: %s\n", member);

                if (strncasecmp(member, "cn=", 3) == 0) {
                    fixMembership = 1;
                }
                if (!bad_ums) {
                    bad_ums = slapi_valueset_new();
                }
                slapi_valueset_add_value(bad_ums, uniqval);
            }
        }

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_fix_memberuid Finishing...\n");

        if (fixMembership  && posix_winsync_config_get_mapNestedGrouping()) {
            Slapi_ValueSet *del_nested_vs = slapi_valueset_new();

            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "_fix_memberuid group deleted, recalculating nesting\n");
            propogateDeletionsUpward(e, sdn, bad_ums, del_nested_vs, 0);

            slapi_valueset_free(del_nested_vs); del_nested_vs = NULL;
        }

        if (bad_ums) {
            slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, "uniquemember", valueset_get_valuearray(bad_ums));
            slapi_valueset_free(bad_ums); bad_ums = NULL;
        }
    }

    mods = slapi_mods_get_ldapmods_passout(smods);
    if (mods) {
        Slapi_PBlock *mod_pb = NULL;
        mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb_ext(mod_pb, sdn, mods, 0, 0,
                                        posix_winsync_get_plugin_identity(), 0);

        slapi_pblock_set(mod_pb, SLAPI_TXN, the_cb_data->txn);
        slapi_modify_internal_pb(mod_pb);

        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        slapi_pblock_destroy(mod_pb);
    }
    slapi_mods_free(&smods);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "_fix_memberuid <==\n");
    return rc;
}

static void
posix_group_fixup_task_thread(void *arg)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "_task_thread ==>\n");

    Slapi_Task *task = (Slapi_Task *) arg;
    task_data *td = NULL;
    int rc = 0;

    /* Fetch our task data from the task */
    td = (task_data *) slapi_task_get_data(task);

    slapi_task_begin(task, 1);
    slapi_task_log_notice(task, "posix_group task starts (arg: %s) ...\n", td->filter_str);

    /* get the memberOf operation lock */
    memberUidLock();

    /* do real work */
    rc = posix_group_fix_memberuid(td->dn, td->filter_str, NULL /* no txn? */);

    /* release the memberOf operation lock */
    memberUidUnlock();
    

    slapi_task_log_notice(task, "posix_group task finished.");
    slapi_task_log_status(task, "posix_group task finished.");
    slapi_task_inc_progress(task);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "_task_thread finishing\n");

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rc);

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "_task_thread <==\n");
}
