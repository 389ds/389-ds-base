#include "slapi-plugin.h"
#include "nspr.h"

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

/* interface function */
int
posix_group_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode,
    char *returntext, void *arg);

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

    /* get arg(s) */
    /* default: set replication basedn */
    if ((dn = fetch_attr(e, "basedn", slapi_sdn_get_dn(posix_winsync_config_get_suffix()))) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    if ((filter = fetch_attr(e, "filter", "(&(objectclass=posixGroup)(uniquemember=*))")) == NULL) {
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* setup our task data */
    mytaskdata = (task_data*) slapi_ch_malloc(sizeof(task_data));
    if (mytaskdata == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    mytaskdata->dn = slapi_ch_strdup(dn);
    mytaskdata->filter_str = slapi_ch_strdup(filter);

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));

    /* register our destructor for cleaning up our private data */
    slapi_task_set_destructor_fn(task, posix_group_task_destructor);

    /* Stash a pointer to our data in the task */
    slapi_task_set_data(task, mytaskdata);

    /* start the sample task as a separate thread */
    thread = PR_CreateThread(PR_USER_THREAD, posix_group_fixup_task_thread, (void *) task,
                             PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD,
                             SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME,
                        "unable to create task thread!\n");
        *returncode = LDAP_OPERATIONS_ERROR;
        rv = SLAPI_DSE_CALLBACK_ERROR;
        slapi_task_finish(task, *returncode);
    } else {
        rv = SLAPI_DSE_CALLBACK_OK;
    }

    out: return rv;
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
    int rc = 0;
    char *dn = slapi_entry_get_dn(e);
    Slapi_DN *sdn = slapi_entry_get_sdn(e);

    Slapi_Attr *obj_attr = NULL;

    rc = slapi_entry_attr_find(e, "uniquemember", &obj_attr);
    if (rc == 0) { /* Found uniquemember, so...  */
        int i;
        Slapi_Value * value = slapi_value_new(); /* new memberuid Attribute values        */
        Slapi_Value * uniqval = NULL; /* uniquemeber Attribute values        */
        Slapi_ValueSet *uids = slapi_valueset_new();

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "_fix_memberuid scan uniquemember, group %s\n", dn);
        for (i = slapi_attr_first_value(obj_attr, &uniqval); i != -1;
             i = slapi_attr_next_value(obj_attr, i, &uniqval)) {
            const char *member = NULL;
            char * uid = NULL;
            member = slapi_value_get_string(uniqval);
            /* search uid for member (DN) */
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "search %s\n", member);
            if ((uid = searchUid(member)) != NULL) {
                slapi_value_set_string(value, uid);
                /* add uids ValueSet */
                slapi_valueset_add_value(uids, value);
            }
        }
        slapi_value_free(&value);

        /* If we found some posix members, replace the existing memberuid attribute
         * with the found values.  */
        if (uids && slapi_valueset_count(uids)) {
            Slapi_PBlock *mod_pb = slapi_pblock_new();
            Slapi_Value *val = 0;
            Slapi_Mod *smod;
            LDAPMod **mods = (LDAPMod **) slapi_ch_malloc(2 * sizeof(LDAPMod *));
            int hint = 0;
            cb_data *the_cb_data = (cb_data *) callback_data;

            smod = slapi_mod_new();
            slapi_mod_init(smod, 0);
            slapi_mod_set_operation(smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
            slapi_mod_set_type(smod, "memberuid");

            /* Loop through all of our values and add them to smod */
            hint = slapi_valueset_first_value(uids, &val);
            while (val) {
                /* this makes a copy of the berval */
                slapi_mod_add_value(smod, slapi_value_get_berval(val));
                hint = slapi_valueset_next_value(uids, hint, &val);
            }

            mods[0] = slapi_mod_get_ldapmod_passout(smod);
            mods[1] = 0;

            slapi_modify_internal_set_pb_ext(mod_pb, sdn, mods, 0, 0,
                                             posix_winsync_get_plugin_identity(), 0);

            slapi_pblock_set(mod_pb, SLAPI_TXN, the_cb_data->txn);
            slapi_modify_internal_pb(mod_pb);

            slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

            ldap_mods_free(mods, 1);
            slapi_mod_free(&smod);
            slapi_pblock_destroy(mod_pb);
        } else {
            /* No member were found, so remove the memberuid attribute
             * from this entry. */
            posix_group_del_memberuid_callback(e, callback_data);
        }
        slapi_valueset_free(uids);
    }
    return rc;
}

static void
posix_group_fixup_task_thread(void *arg)
{
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

    /* this will queue the destruction of the task */
    slapi_task_finish(task, rc);
}
