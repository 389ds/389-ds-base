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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "portable.h"
#include "slapi-plugin.h"

/* This is naughty ... */
#include "slapi-private.h"

/* include NSPR header files */
#include "slap.h"
#include "prthread.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "prio.h"
#include "avl.h"
#include "vattr_spi.h"
#include "roles_cache.h"
#include "views.h"

#ifdef SOLARIS
#include <tnf/probe.h>
#else
#define TNF_PROBE_0(a, b, c)
#endif

#define MAX_NESTED_ROLES 30

static char *allUserAttributes[] = {
    LDAP_ALL_USER_ATTRS,
    NULL};

/* views scoping */
static void **views_api;

/* List of nested roles */
typedef struct _role_object_nested
{
    Slapi_DN *dn; /* value of attribute nsroledn in a nested role definition */
} role_object_nested;

/* Role object structure */
typedef struct _role_object
{
    Slapi_DN *dn;          /* dn of a role entry */
    Slapi_DN *rolescopedn; /* if set, this role will apply to any entry in the scope of this dn */
    int type;              /* ROLE_TYPE_MANAGED|ROLE_TYPE_FILTERED|ROLE_TYPE_NESTED */
    Slapi_Filter *filter;  /* if ROLE_TYPE_FILTERED */
    Avlnode *avl_tree;     /* if ROLE_TYPE_NESTED: tree of nested DNs (avl_data is a role_object_nested struct) */
} role_object;

/* Structure containing the roles definitions for a given suffix */
typedef struct _roles_cache_def
{

    /* Suffix DN*/
    Slapi_DN *suffix_dn;

    /* Module level thread control */
    PRThread *roles_tid;
    int keeprunning;

    Slapi_RWLock *cache_lock;
    Slapi_Mutex *stop_lock;

    Slapi_Mutex *change_lock;
    Slapi_CondVar *something_changed;

    Slapi_Mutex *create_lock;
    Slapi_CondVar *suffix_created;
    int is_ready;

    /* Root of the avl tree containing all the roles definitions
       NB: avl_data field is of type role_object
     */
    Avlnode *avl_tree;

    /* Next roles suffix definitions */
    struct _roles_cache_def *next;

    /* Info passed from the server when an notification is sent to the plugin */
    char *notified_dn;
    Slapi_Entry *notified_entry;
    int notified_operation;

} roles_cache_def;


/* Global list containing all the roles definitions per suffix */
static roles_cache_def *roles_list = NULL;

static Slapi_RWLock *global_lock = NULL;

/* Structure holding the nsrole values */
typedef struct _roles_cache_build_result
{
    Slapi_ValueSet **nsrole_values; /* nsrole computed values */
    Slapi_Entry *requested_entry;   /* entry to get nsrole from */
    int has_value;                  /* flag to determine if a new value has been added to the result */
    int need_value;                 /* flag to determine if we need the result */
    vattr_context *context;         /* vattr context */
} roles_cache_build_result;

/* Structure used to check if is_entry_member_of is part of a role defined in its suffix */
typedef struct _roles_cache_search_in_nested
{
    Slapi_Entry *is_entry_member_of;
    int present; /* flag to know if the entry is part of a role */
    int hint;    /* to check the depth of the nested */
} roles_cache_search_in_nested;

/* Structure used to handle roles searches */
typedef struct _roles_cache_search_roles
{
    roles_cache_def *suffix_def;
    int rc; /* to check the depth of the nested */
} roles_cache_search_roles;

static roles_cache_def *roles_cache_create_suffix(Slapi_DN *sdn);
static int roles_cache_add_roles_from_suffix(Slapi_DN *suffix_dn, roles_cache_def *suffix_def);
static void roles_cache_wait_on_change(void *arg);
static void roles_cache_trigger_update_suffix(void *handle, char *be_name, int old_be_state, int new_be_state);
static void roles_cache_trigger_update_role(char *dn, Slapi_Entry *role_entry, Slapi_DN *be_dn, int operation);
static int roles_cache_update(roles_cache_def *suffix_to_update);
static int roles_cache_create_role_under(roles_cache_def **roles_cache_suffix, Slapi_Entry *entry);
static int roles_cache_create_object_from_entry(Slapi_Entry *role_entry, role_object **result, int hint);
static int roles_cache_determine_class(Slapi_Entry *role_entry);
static int roles_cache_node_cmp(caddr_t d1, caddr_t d2);
static int roles_cache_insert_object(Avlnode **tree, role_object *object);
static int roles_cache_node_nested_cmp(caddr_t d1, caddr_t d2);
static int roles_cache_insert_object_nested(Avlnode **tree, role_object_nested *object);
static int roles_cache_object_nested_from_dn(Slapi_DN *role_dn, role_object_nested **result);
static int roles_cache_build_nsrole(caddr_t data, caddr_t arg);
static int roles_cache_find_node(caddr_t d1, caddr_t d2);
static int roles_cache_find_roles_in_suffix(Slapi_DN *target_entry_dn, roles_cache_def **list_of_roles);
static int roles_is_entry_member_of_object(caddr_t data, caddr_t arg);
static int roles_is_entry_member_of_object_ext(vattr_context *c, caddr_t data, caddr_t arg);
static int roles_check_managed(Slapi_Entry *entry_to_check, role_object *role, int *present);
static int roles_check_filtered(vattr_context *c, Slapi_Entry *entry_to_check, role_object *role, int *present);
static int roles_check_nested(caddr_t data, caddr_t arg);
static int roles_is_inscope(Slapi_Entry *entry_to_check, role_object *this_role);
static void berval_set_string(struct berval *bv, const char *string);
static void roles_cache_role_def_delete(roles_cache_def *role_def);
static void roles_cache_role_def_free(roles_cache_def *role_def);
static void roles_cache_role_object_free(role_object *this_role);
static int roles_cache_role_object_nested_free(role_object_nested *this_role);
static int roles_cache_dump(caddr_t data, caddr_t arg);
static int roles_cache_add_entry_cb(Slapi_Entry *e, void *callback_data);
static void roles_cache_result_cb(int rc, void *callback_data);
static Slapi_DN *roles_cache_get_top_suffix(Slapi_DN *suffix);

/*     ============== FUNCTIONS ================ */

/* roles_cache_init
   ----------------
   create the cache for all the existing suffixes
   starts up the threads which wait for changes
   also registers vattr callbacks

    return 0 if OK
    return -1 otherwise
*/
int
roles_cache_init()
{
    int rc = 0;
    void *node = NULL;
    Slapi_DN *sdn = NULL;
    roles_cache_def *new_suffix = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_init\n");

    if (global_lock == NULL) {
        global_lock = slapi_new_rwlock();
    }

    /* grab the views interface */
    if (slapi_apib_get_interface(Views_v1_0_GUID, &views_api)) {
        /* lets be tolerant if views is disabled */
        views_api = 0;
    }

    /* For each top suffix, get the roles definitions defined below it */
    slapi_rwlock_wrlock(global_lock);

    sdn = slapi_get_first_suffix(&node, 0);
    while (sdn) {

        if ((new_suffix = roles_cache_create_suffix(sdn)) == NULL) {
            slapi_destroy_rwlock(global_lock);
            global_lock = NULL;
            return (-1);
        }

        if (roles_cache_add_roles_from_suffix(sdn, new_suffix) != 0) {
            /* No roles in that suffix, stop the thread and remove it ? */
        }
        sdn = slapi_get_next_suffix(&node, 0);
    }
    slapi_rwlock_unlock(global_lock);

    /* to expose roles_check to ACL plugin */
    slapi_register_role_check(roles_check);

    /* Register a callback on backends creation|modification|deletion,
      so that we update the corresponding cache */
    slapi_register_backend_state_change(NULL, roles_cache_trigger_update_suffix);

    /* Service provider handler - only used once! and freed by vattr! */
    vattr_sp_handle *vattr_handle = NULL;


    if (slapi_vattrspi_register((vattr_sp_handle **)&vattr_handle,
                                roles_sp_get_value,
                                roles_sp_compare_value,
                                roles_sp_list_types)) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_init - slapi_vattrspi_register failed\n");

        slapi_destroy_rwlock(global_lock);
        global_lock = NULL;
        return (-1);
    } else if (slapi_vattrspi_regattr(vattr_handle, NSROLEATTR, "", NULL)) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_init - slapi_vattrspi_regattr failed\n");
        slapi_ch_free((void **)&vattr_handle);
        slapi_destroy_rwlock(global_lock);
        global_lock = NULL;
        return (-1);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_init\n");
    return rc;
}

/* roles_cache_create_suffix
   -------------------------
    Create a new entry in the global list
    return a pointer on the suffix stucture: OK
    return NULL: fail
 */
static roles_cache_def *
roles_cache_create_suffix(Slapi_DN *sdn)
{
    roles_cache_def *current_suffix = NULL;
    roles_cache_def *new_suffix = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_create_suffix\n");

    /* Allocate a new suffix block */
    new_suffix = (roles_cache_def *)slapi_ch_calloc(1, sizeof(roles_cache_def));
    if (new_suffix == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_suffix - Unable to allocate memory, cannot create role cache\n");
        return (NULL);
    }

    new_suffix->cache_lock = slapi_new_rwlock();
    new_suffix->change_lock = slapi_new_mutex();
    new_suffix->stop_lock = slapi_new_mutex();
    new_suffix->create_lock = slapi_new_mutex();
    if (new_suffix->stop_lock == NULL ||
        new_suffix->change_lock == NULL ||
        new_suffix->cache_lock == NULL ||
        new_suffix->create_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_suffix - Lock creation failed\n");
        roles_cache_role_def_free(new_suffix);
        return (NULL);
    }

    new_suffix->something_changed = slapi_new_condvar(new_suffix->change_lock);
    if (new_suffix->something_changed == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_suffix - ConVar creation failed\n");
        roles_cache_role_def_free(new_suffix);
        return (NULL);
    }

    new_suffix->suffix_created = slapi_new_condvar(new_suffix->create_lock);
    if (new_suffix->suffix_created == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_suffix - CondVar creation failed\n");
        roles_cache_role_def_free(new_suffix);
        return (NULL);
    }

    new_suffix->keeprunning = 1;

    new_suffix->suffix_dn = slapi_sdn_dup(sdn);

    /* those 3 items are used to give back info to the thread when
    it is awakened */
    new_suffix->notified_dn = NULL;
    new_suffix->notified_entry = NULL;
    new_suffix->notified_operation = 0;

    /* Create the global list */
    if (roles_list == NULL) {
        roles_list = new_suffix;
    } else {
        current_suffix = roles_list;
        while (current_suffix != NULL) {
            if (current_suffix->next == NULL) {
                current_suffix->next = new_suffix;
                break;
            } else {
                current_suffix = current_suffix->next;
            }
        }
    }

    /* to prevent deadlock */
    new_suffix->is_ready = 0;
    if ((new_suffix->roles_tid = PR_CreateThread(PR_USER_THREAD,
                                                 roles_cache_wait_on_change,
                                                 (void *)new_suffix,
                                                 PR_PRIORITY_NORMAL,
                                                 PR_GLOBAL_THREAD,
                                                 PR_UNJOINABLE_THREAD,
                                                 SLAPD_DEFAULT_THREAD_STACKSIZE)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_suffix - PR_CreateThread failed\n");
        roles_cache_role_def_delete(new_suffix);
        return (NULL);
    }

    slapi_lock_mutex(new_suffix->create_lock);
    if (new_suffix->is_ready != 1) {
        slapi_wait_condvar_pt(new_suffix->suffix_created, new_suffix->create_lock, NULL);
    }
    slapi_unlock_mutex(new_suffix->create_lock);

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_create_suffix\n");
    return (new_suffix);
}

/* roles_cache_wait_on_change
   --------------------------
   Sit around waiting on a notification that something has
   changed, then fires off the updates
 */
static void
roles_cache_wait_on_change(void *arg)
{
    roles_cache_def *roles_def = (roles_cache_def *)arg;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_wait_on_change\n");

    slapi_lock_mutex(roles_def->stop_lock);
    slapi_lock_mutex(roles_def->change_lock);

    while (roles_def->keeprunning) {
        slapi_unlock_mutex(roles_def->change_lock);
        slapi_lock_mutex(roles_def->change_lock);

        /* means that the thread corresponding to that suffix is ready to receive notifications
           from the server */
        slapi_lock_mutex(roles_def->create_lock);
        if (roles_def->is_ready == 0) {
            slapi_notify_condvar(roles_def->suffix_created, 1);
            roles_def->is_ready = 1;
        }
        slapi_unlock_mutex(roles_def->create_lock);

        /* XXX In case the BE containing this role_def signaled
            a shut down between the unlock/lock above should
            test roles_def->keeprunning before
            going to sleep.
        */
        slapi_wait_condvar_pt(roles_def->something_changed, roles_def->change_lock, NULL);

        slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "roles_cache_wait_on_change - notified\n");

        if (roles_def->keeprunning) {
            roles_cache_update(roles_def);
        }
    }

    /* shut down the cache */
    slapi_unlock_mutex(roles_def->change_lock);
    slapi_unlock_mutex(roles_def->stop_lock);

    roles_cache_role_def_free(roles_def);

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_wait_on_change\n");
}

/* roles_cache_trigger_update_suffix
   --------------------------------
   This is called when a backend changes state (created|modified|deleted)
   We simply signal to update the associated role cache in this case
 */
static void
roles_cache_trigger_update_suffix(void *handle __attribute__((unused)), char *be_name, int old_be_state __attribute__((unused)), int new_be_state)
{
    roles_cache_def *current_role = roles_list;
    const Slapi_DN *be_suffix_dn = NULL;
    Slapi_DN *top_suffix_dn = NULL;
    Slapi_Backend *be = NULL;
    int found = 0;

    slapi_rwlock_wrlock(global_lock);

    if ((new_be_state == SLAPI_BE_STATE_DELETE) || (new_be_state == SLAPI_BE_STATE_OFFLINE)) {
        /* Invalidate and rebuild the whole cache */
        roles_cache_def *curr_role = NULL;
        roles_cache_def *next_role = NULL;
        Slapi_DN *sdn = NULL;
        void *node = NULL;
        roles_cache_def *new_suffix = NULL;

        /* Go through all the roles list and trigger the associated structure */
        curr_role = roles_list;
        while (curr_role) {
            slapi_lock_mutex(curr_role->change_lock);
            curr_role->keeprunning = 0;
            next_role = curr_role->next;
            slapi_notify_condvar(curr_role->something_changed, 1);
            slapi_unlock_mutex(curr_role->change_lock);

            curr_role = next_role;
        }

        /* rebuild a new one */
        roles_list = NULL;

        sdn = slapi_get_first_suffix(&node, 0);
        while (sdn) {

            if ((new_suffix = roles_cache_create_suffix(sdn)) == NULL) {
                slapi_rwlock_unlock(global_lock);
                return;
            }

            roles_cache_add_roles_from_suffix(sdn, new_suffix);
            sdn = slapi_get_next_suffix(&node, 0);
        }
        slapi_rwlock_unlock(global_lock);
        return;
    }

    /* Backend back on line or new one created*/
    be = slapi_be_select_by_instance_name(be_name);
    if (be != NULL) {
        be_suffix_dn = slapi_be_getsuffix(be, 0);
        top_suffix_dn = roles_cache_get_top_suffix((Slapi_DN *)be_suffix_dn);
    }

    while ((current_role != NULL) && !found && (top_suffix_dn != NULL)) {
        /* The backend already exists (back online): so invalidate "old roles definitions" */
        if (slapi_sdn_compare(current_role->suffix_dn, top_suffix_dn) == 0) {
            roles_cache_role_def_delete(current_role);
            found = 1;
        } else {
            current_role = current_role->next;
        }
    }

    if (top_suffix_dn != NULL) {
        /* Add the new definitions in the cache */
        roles_cache_def *new_suffix = roles_cache_create_suffix(top_suffix_dn);

        if (new_suffix != NULL) {
            roles_cache_add_roles_from_suffix(top_suffix_dn, new_suffix);
        }
        slapi_sdn_free(&top_suffix_dn);
    }

    slapi_rwlock_unlock(global_lock);
}

/* roles_cache_trigger_update_role
   --------------------------------
    Call when an entry containing a role definition has been added, modified
    or deleted
 */
static void
roles_cache_trigger_update_role(char *dn, Slapi_Entry *roles_entry, Slapi_DN *be_dn, int operation)
{
    int found = 0;
    roles_cache_def *current_role = NULL;

    slapi_rwlock_wrlock(global_lock);

    current_role = roles_list;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_trigger_update_role: %p \n", roles_list);

    /* Go through all the roles list and trigger the associated structure */

    /* be_dn is already the top suffix for that dn */
    while ((current_role != NULL) && !found) {
        if (slapi_sdn_compare(current_role->suffix_dn, be_dn) == 0) {
            found = 1;
        } else {
            current_role = current_role->next;
        }
    }

    if (found) {
        slapi_lock_mutex(current_role->change_lock);

        slapi_entry_free(current_role->notified_entry);
        current_role->notified_entry = roles_entry;
        slapi_ch_free((void **)&(current_role->notified_dn));
        current_role->notified_dn = dn;
        current_role->notified_operation = operation;

        roles_cache_update(current_role);

        slapi_unlock_mutex(current_role->change_lock);
    }

    slapi_rwlock_unlock(global_lock);
    {
        /* A role definition has been updated, enable vattr handling */
        char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
        errorbuf[0] = '\0';
        config_set_ignore_vattrs(CONFIG_IGNORE_VATTRS, "off", errorbuf, 1);
        slapi_log_err(SLAPI_LOG_INFO,
                      "roles_cache_trigger_update_role",
                      "Because of virtual attribute definition (role), %s was set to 'off'\n", CONFIG_IGNORE_VATTRS);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_trigger_update_role: %p \n", roles_list);
}

/* roles_cache_update
   ------------------
   Update the cache associated to a suffix
    Return 0: ok
    Return -1: fail
 */
static int
roles_cache_update(roles_cache_def *suffix_to_update)
{
    int rc = 0;
    int operation;
    Slapi_Entry *entry = NULL;
    Slapi_DN *dn = NULL;
    role_object *to_delete = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_update \n");

    slapi_rwlock_wrlock(suffix_to_update->cache_lock);

    operation = suffix_to_update->notified_operation;
    entry = suffix_to_update->notified_entry;

    dn = slapi_sdn_new();
    if (!dn) {
        slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "Out of memory \n");
        rc = -1;
        goto done;
    }

    slapi_sdn_set_dn_byval(dn, suffix_to_update->notified_dn);

    if (entry != NULL) {
        if ((operation == SLAPI_OPERATION_MODIFY) ||
            (operation == SLAPI_OPERATION_DELETE)) {

            to_delete = (role_object *)avl_delete(&(suffix_to_update->avl_tree), dn, roles_cache_find_node);
            roles_cache_role_object_free(to_delete);
            to_delete = NULL;
            if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
                avl_apply(suffix_to_update->avl_tree, (IFP)roles_cache_dump, &rc, -1, AVL_INORDER);
            }
        }
        if ((operation == SLAPI_OPERATION_MODIFY) ||
            (operation == SLAPI_OPERATION_ADD)) {
            rc = roles_cache_create_role_under(&suffix_to_update, entry);
        }
        if (entry != NULL) {
            slapi_entry_free(entry);
        }
        suffix_to_update->notified_entry = NULL;
    }
done:
    slapi_rwlock_unlock(suffix_to_update->cache_lock);
    if (dn != NULL) {
        slapi_sdn_free(&dn);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_update \n");
    return (rc);
}

/* roles_cache_stop
   ----------------

    XXX the stop_lock of a roles_cache_def
    doesn't seem to serve any useful purpose...

 */
void
roles_cache_stop()
{
    roles_cache_def *current_role = NULL;
    roles_cache_def *next_role = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_stop\n");

    /* Go through all the roles list and trigger the associated structure */
    slapi_rwlock_wrlock(global_lock);
    current_role = roles_list;
    while (current_role) {
        slapi_lock_mutex(current_role->change_lock);
        current_role->keeprunning = 0;
        next_role = current_role->next;
        slapi_notify_condvar(current_role->something_changed, 1);
        slapi_unlock_mutex(current_role->change_lock);

        current_role = next_role;
    }
    slapi_rwlock_unlock(global_lock);
    roles_list = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_stop\n");
}

/* roles_cache_is_role_entry
   -------------------------
    Check if the entry is a role
    return -1: error in processing
    return 0: entry is not a role
    return 1: entry is a role
*/
static int
roles_cache_is_role_entry(struct slapi_entry *entry)
{
    Slapi_Attr *pObjclasses = NULL;
    Slapi_Value *val = NULL;
    char *pObj = NULL;
    int index = 0;

    int nsroledefinition = 0;
    int nsrolesimpleOrComplex = 0;
    int nsroletype = 0;

    if (entry == NULL) {
        return (0);
    }

    if (slapi_entry_attr_find(entry, "objectclass", &pObjclasses)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_is_role_entry - Failed to get objectclass from %s\n",
                      slapi_entry_get_dn_const(entry));
        return (-1);
    }

    /* Check out the object classes to see if this was a nsroledefinition */

    val = 0;
    index = slapi_attr_first_value(pObjclasses, &val);
    while (val) {
        const char *p;
        int len = 0;

        pObj = (char *)slapi_value_get_string(val);

        for (p = pObj, len = 0;
             (*p != '\0') && (*p != ' ');
             p++, len++) {
            ; /* NULL */
        }

        if (!strncasecmp(pObj, (char *)"nsroledefinition", len)) {
            nsroledefinition = 1;
        }
        if (!strncasecmp(pObj, (char *)"nssimpleroledefinition", len) ||
            !strncasecmp(pObj, (char *)"nscomplexroledefinition", len)) {
            nsrolesimpleOrComplex = 1;
        }
        if (!strncasecmp(pObj, (char *)"nsmanagedroledefinition", len) ||
            !strncasecmp(pObj, (char *)"nsfilteredroledefinition", len) ||
            !strncasecmp(pObj, (char *)"nsnestedroledefinition", len)) {
            nsroletype = 1;
        }
        index = slapi_attr_next_value(pObjclasses, index, &val);
    }
    if ((nsroledefinition == 0) ||
        (nsrolesimpleOrComplex == 0) ||
        (nsroletype == 0)) {
        return (0);
    }
    return (1);
}

/* roles_cache_change_notify
   -------------------------
   determines if the change effects the cache and if so
   signals a rebuild
    -- called when modify|modrdn|add|delete operation is performed --
    -- called from a postoperation on an entry
    XXX this implies that the client may have already received his LDAP response,
    but that there will be a delay before he sees the effect in the roles cache.
    Should we be doing this processing in a BE_POST_MODIFY postop
    which is called _before_ the response goes to the client ?
*/
void
roles_cache_change_notify(Slapi_PBlock *pb)
{
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    struct slapi_entry *e = NULL;
    struct slapi_entry *pre = NULL;
    struct slapi_entry *entry = NULL;
    Slapi_Backend *be = NULL;
    Slapi_Operation *pb_operation = NULL;
    int operation;
    int do_update = 0;
    int rc = -1;

    if (!slapi_plugin_running(pb)) {
        /* not initialized yet */
        return;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM,
                  "--> roles_cache_change_notify\n");

    /* if the current operation has failed, don't even try the post operation */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
    if (rc != LDAP_SUCCESS) {
        return;
    }

    /* Don't update local cache when remote entries are updated */
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if ((be == NULL) || (slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA))) {
        return;
    }

    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    if (sdn == NULL) {
        return;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_operation);
    if (NULL == pb_operation) {
        return;
    }
    operation = operation_get_type(pb_operation);

    switch (operation) {
    case SLAPI_OPERATION_DELETE:

        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
        if (e == NULL) {
            return;
        }
        break;

    case SLAPI_OPERATION_ADD:
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
        if (e == NULL) {
            return;
        }
        break;

    case SLAPI_OPERATION_MODIFY:
    case SLAPI_OPERATION_MODRDN:
        /* those operations are treated the same way and modify is a deletion followed by an addition.
               the only point to take care is that dn is the olddn */
        operation = SLAPI_OPERATION_MODIFY;
        slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &pre);
        if (pre == NULL) {
            return;
        }
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
        if (e == NULL) {
            return;
        }
        break;
    default:
        slapi_log_err(SLAPI_LOG_ERR,
                      ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_change_notify - Unknown operation %d\n", operation);
        return;
    }

    if (operation != SLAPI_OPERATION_MODIFY) {
        if (roles_cache_is_role_entry(e) != 1) {
            slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_change_notify - Not a role entry\n");
            return;
        }
        entry = slapi_entry_dup(e);
        do_update = 1;
    } else {
        int is_pre_role = roles_cache_is_role_entry(pre);
        int is_post_role = roles_cache_is_role_entry(e);
        if ((is_pre_role == 1) && (is_post_role == 1)) /* role definition has changed */
        {
            entry = slapi_entry_dup(e);
            do_update = 1;
        } else if (is_pre_role == 1) /* entry is no more a role */
        {
            operation = SLAPI_OPERATION_DELETE;
            do_update = 1;
        } else if (is_post_role == 1) /* entry is now a role */
        {
            operation = SLAPI_OPERATION_ADD;
            entry = slapi_entry_dup(e);
            do_update = 1;
        } else {
            slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_change_notify - Not a role entry\n");
            return;
        }
    }

    if (do_update) {
#ifdef moretrace
        if (e != NULL) {
            Slapi_Attr *attr = NULL;
            int rc;

            /* Get the list of nested roles */
            rc = slapi_entry_attr_find(e, ROLE_NESTED_ATTR_NAME, &attr);

            if ((rc == 0) && attr) {
                /* Recurse to get the definition objects for them */
                Slapi_Value **va = attr_get_present_values(attr);
                int i = 0;
                char *string = NULL;

                for (i = 0; va[i] != NULL; i++) {
                    string = (char *)slapi_value_get_string(va[i]);
                    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "roles_cache_change_notify - %s\n", string);
                }
            }
        }
#endif
        Slapi_DN *top_suffix = roles_cache_get_top_suffix((Slapi_DN *)slapi_be_getsuffix(be, 0));

        if (top_suffix != NULL) {
            dn = slapi_sdn_get_dn(sdn);
            roles_cache_trigger_update_role(slapi_ch_strdup(dn), entry,
                                            top_suffix,
                                            operation);

            slapi_sdn_free(&top_suffix);
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_change_notify\n");
}

/* roles_cache_get_top_suffix
    -------------------------
   The returned Slapi_DN must be freed with slapi_sdn_free().
*/
static Slapi_DN *
roles_cache_get_top_suffix(Slapi_DN *suffix)
{
    Slapi_DN *current_suffix = NULL;
    Slapi_DN parent_suffix;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_get_top_suffix\n");

    if (suffix == NULL) {
        return (NULL);
    }
    current_suffix = slapi_sdn_new();
    slapi_sdn_init(&parent_suffix);

    /* we must get the top suffix for that DN */
    slapi_sdn_copy(suffix, current_suffix);
    while (!slapi_sdn_isempty(current_suffix)) {
        if (slapi_is_root_suffix(current_suffix) != 1) {
            slapi_sdn_get_parent(current_suffix, &parent_suffix);
            slapi_sdn_copy(&parent_suffix, current_suffix);
        } else {
            slapi_sdn_done(&parent_suffix);
            return (current_suffix);
        }
    }
    /* we should not return that way ... */
    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_get_top_suffix\n");
    slapi_sdn_done(&parent_suffix);
    slapi_sdn_free(&current_suffix);
    return (NULL);
}

/* roles_cache_add_roles_from_suffix
   -------------------------------
    Get the roles entries under the suffix
    return 0: OK
    return -1: this suffix has no role defined
 */
static int
roles_cache_add_roles_from_suffix(Slapi_DN *suffix_dn, roles_cache_def *suffix_def)
{
    /* Search subtree-level under this entry */
    int rc = -1;
    roles_cache_search_roles info;
    Slapi_PBlock *int_search_pb = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_add_roles_from_suffix\n");

    info.suffix_def = suffix_def;
    info.rc = LDAP_NO_SUCH_OBJECT;

    /* Get the roles definitions of the given suffix_dn */
    int_search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(int_search_pb,
                                 (char *)slapi_sdn_get_ndn(suffix_dn),
                                 LDAP_SCOPE_SUBTREE,
                                 ROLE_DEFINITION_FILTER,
                                 allUserAttributes,
                                 0 /* attrsonly */,
                                 NULL /* controls */,
                                 NULL /* uniqueid */,
                                 roles_get_plugin_identity(),
                                 SLAPI_OP_FLAG_NEVER_CHAIN /* actions : get local roles only */);

    slapi_search_internal_callback_pb(int_search_pb,
                                      &info /* callback_data */,
                                      roles_cache_result_cb,
                                      roles_cache_add_entry_cb,
                                      NULL /* referral_callback */);

    slapi_pblock_destroy(int_search_pb);
    int_search_pb = NULL;

    if (info.rc == LDAP_SUCCESS) {
        rc = 0;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_add_roles_from_suffix\n");

    return (rc);
}

/* roles_cache_add_entry_cb
   -----------------------
*/
static int
roles_cache_add_entry_cb(Slapi_Entry *e, void *callback_data)
{
    roles_cache_search_roles *info = (roles_cache_search_roles *)callback_data;

    roles_cache_def *suffix = info->suffix_def;

    roles_cache_create_role_under(&suffix, e);
    return (0);
}

/* roles_cache_result_cb
   -----------------------
*/
static void
roles_cache_result_cb(int rc, void *callback_data)
{
    roles_cache_search_roles *info = (roles_cache_search_roles *)callback_data;

    info->rc = rc;
}


/* roles_cache_create_role_under
   ----------------------------
   Create the avl tree of roles definitions defined in the scope
   of the suffix
    Return 0: OK
    Return -1: fail
*/
static int
roles_cache_create_role_under(roles_cache_def **roles_cache_suffix, Slapi_Entry *entry)
{
    int rc;
    role_object *new_role = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_create_role_under - %s - %p\n",
                  slapi_sdn_get_dn((*roles_cache_suffix)->suffix_dn),
                  (*roles_cache_suffix)->avl_tree);

    rc = roles_cache_create_object_from_entry(entry, &new_role, 0);
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM,
                  "roles_cache_create_role_under - create node for entry %s - rc: %d SUFFIX: %p\n",
                  slapi_entry_get_dn_const(entry), rc, (*roles_cache_suffix)->avl_tree);

    if ((rc == 0) && new_role) {
        /* Add to the tree where avl_data is a role_object struct */
        rc = roles_cache_insert_object(&((*roles_cache_suffix)->avl_tree), new_role);
        slapi_log_err(SLAPI_LOG_PLUGIN,
                      ROLES_PLUGIN_SUBSYSTEM, "roles_cache_create_role_under - %s in tree %p rc: %d\n",
                      (char *)slapi_sdn_get_ndn(new_role->dn),
                      (*roles_cache_suffix)->avl_tree, rc);
    }
    return (rc);
}

/*
 * Check that we are not using nsrole in the filter, recurse over all the
 * nested filters.
 */
static int
roles_check_filter(Slapi_Filter *filter_list)
{
    Slapi_Filter *f;
    char *type = NULL;

    f = slapi_filter_list_first(filter_list);
    if (f == NULL) {
        /* Single filter */
        if (slapi_filter_get_attribute_type(filter_list, &type) == 0) {
            if (strcasecmp(type, NSROLEATTR) == 0) {
                return -1;
            }
        }
    }
    for (; f != NULL; f = slapi_filter_list_next(filter_list, f)) {
        /* Complex filter */
        if (slapi_filter_list_first(f)) {
            /* Another filter list - recurse */
            if (roles_check_filter(f) == -1) {
                /* Done, break out */
                return -1;
            }
        } else {
            /* Not a filter list, so check the type */
            if (slapi_filter_get_attribute_type(f, &type) == 0) {
                if (strcasecmp(type, NSROLEATTR) == 0) {
                    return -1;
                }
            }
        }
    }

    return 0;
}

/* roles_cache_create_object_from_entry
   ------------------------------------
    Create a node role_object from the information contained in role_entry
    Return 0
    Return ENOMEM: fail
    Return SLAPI_ROLE_DEFINITION_ERROR: fail
    Return SLAPI_ROLE_ERROR_NO_FILTER_SPECIFIED: fail
    Return SLAPI_ROLE_ERROR_FILTER_BAD: fail
*/
static int
roles_cache_create_object_from_entry(Slapi_Entry *role_entry, role_object **result, int hint)
{
    int rc = 0;
    int type = 0;
    role_object *this_role = NULL;
    char *rolescopeDN = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "--> roles_cache_create_object_from_entry\n");

    *result = NULL;

    /* Do not allow circular dependencies */
    if (hint > MAX_NESTED_ROLES) {
        char *ndn = NULL;

        ndn = slapi_entry_get_ndn(role_entry);
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_object_from_entry - Maximum roles nesting exceeded (%d), not retrieving roles "
                      "from entry %s--probable circular definition\n",
                      MAX_NESTED_ROLES, ndn);

        return (0);
    }

    /* Create the role cache definition */
    this_role = (role_object *)slapi_ch_calloc(1, sizeof(role_object));
    if (this_role == NULL) {
        return ENOMEM;
    }

    /* Check the entry is OK */
    /* Determine role type and assign to structure */
    /* We determine the role type by reading the objectclass */
    if (roles_cache_is_role_entry(role_entry) == 0) {
        /* Bad type */
        slapi_ch_free((void **)&this_role);
        return SLAPI_ROLE_DEFINITION_ERROR;
    }

    type = roles_cache_determine_class(role_entry);

    if (type != 0) {
        this_role->type = type;
    } else {
        /* Bad type */
        slapi_ch_free((void **)&this_role);
        return SLAPI_ROLE_DEFINITION_ERROR;
    }

    this_role->dn = slapi_sdn_new();
    slapi_sdn_copy(slapi_entry_get_sdn(role_entry), this_role->dn);

    rolescopeDN = slapi_entry_attr_get_charptr(role_entry, ROLE_SCOPE_DN);
    if (rolescopeDN) {
        Slapi_DN *rolescopeSDN;
        Slapi_DN *top_rolescopeSDN, *top_this_roleSDN;

        /* Before accepting to use this scope, first check if it belongs to the same suffix */
        rolescopeSDN = slapi_sdn_new_dn_byref(rolescopeDN);
        if ((strlen((char *)slapi_sdn_get_ndn(rolescopeSDN)) > 0) &&
            (slapi_dn_syntax_check(NULL, (char *)slapi_sdn_get_ndn(rolescopeSDN), 1) == 0)) {
            top_rolescopeSDN = roles_cache_get_top_suffix(rolescopeSDN);
            top_this_roleSDN = roles_cache_get_top_suffix(this_role->dn);
            if (slapi_sdn_compare(top_rolescopeSDN, top_this_roleSDN) == 0) {
                /* rolescopeDN belongs to the same suffix as the role, we can use this scope */
                this_role->rolescopedn = rolescopeSDN;
            } else {
                slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                              "roles_cache_create_object_from_entry - %s: invalid %s - %s not in the same suffix. Scope skipped.\n",
                              (char *)slapi_sdn_get_dn(this_role->dn),
                              ROLE_SCOPE_DN,
                              rolescopeDN);
                slapi_sdn_free(&rolescopeSDN);
            }
            slapi_sdn_free(&top_rolescopeSDN);
            slapi_sdn_free(&top_this_roleSDN);
        } else {
            /* this is an invalid DN, just ignore this parameter*/
            slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                          "roles_cache_create_object_from_entry - %s: invalid %s - %s not a valid DN. Scope skipped.\n",
                          (char *)slapi_sdn_get_dn(this_role->dn),
                          ROLE_SCOPE_DN,
                          rolescopeDN);
            slapi_sdn_free(&rolescopeSDN);
        }
    }

    /* Depending upon role type, pull out the remaining information we need */
    switch (this_role->type) {
    case ROLE_TYPE_MANAGED:
        /* Nothing further needed */
        break;

    case ROLE_TYPE_FILTERED: {
        Slapi_Filter *filter = NULL;
        char *filter_attr_value = NULL;
        Slapi_PBlock *pb = NULL;
        char *parent = NULL;

        /* Get the filter and retrieve the filter attribute */
        filter_attr_value = (char *)slapi_entry_attr_get_charptr(role_entry, ROLE_FILTER_ATTR_NAME);
        if (filter_attr_value == NULL) {
            /* Means probably no attribute or no value there */
            slapi_ch_free((void **)&this_role);
            return SLAPI_ROLE_ERROR_NO_FILTER_SPECIFIED;
        }

        /* search (&(objectclass=costemplate)(filter_attr_value))*/
        /* if found, reject it (returning SLAPI_ROLE_ERROR_FILTER_BAD) */
        pb = slapi_pblock_new();
        parent = slapi_dn_parent(slapi_entry_get_dn(role_entry));
        if (parent) {
            Slapi_Entry **cosentries = NULL;
            char *costmpl_filter = NULL;
            if ((*filter_attr_value == '(') &&
                (*(filter_attr_value + strlen(filter_attr_value) - 1) == ')')) {
                costmpl_filter =
                    slapi_ch_smprintf("(&(objectclass=costemplate)%s)",
                                      filter_attr_value);
            } else {
                costmpl_filter =
                    slapi_ch_smprintf("(&(objectclass=costemplate)(%s))",
                                      filter_attr_value);
            }
            slapi_search_internal_set_pb(pb, parent, LDAP_SCOPE_SUBTREE,
                                         costmpl_filter, NULL, 0, NULL,
                                         NULL, roles_get_plugin_identity(),
                                         0);
            slapi_search_internal_pb(pb);
            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                             &cosentries);
            slapi_ch_free_string(&costmpl_filter);
            slapi_ch_free_string(&parent);
            if (cosentries && *cosentries) {
                slapi_free_search_results_internal(pb);
                slapi_pblock_destroy(pb);
                slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                              "roles_cache_create_object_from_entry - %s: not allowed to refer virtual attribute "
                              "in the value of %s %s. The %s is disabled.\n",
                              (char *)slapi_sdn_get_ndn(this_role->dn),
                              ROLE_FILTER_ATTR_NAME, filter_attr_value,
                              ROLE_FILTER_ATTR_NAME);
                slapi_ch_free((void **)&this_role);
                slapi_ch_free_string(&filter_attr_value);
                return SLAPI_ROLE_ERROR_FILTER_BAD;
            }
        }
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);

        /* Turn it into a slapi filter object */
        filter = slapi_str2filter(filter_attr_value);
        if (filter == NULL) {
            /* An error has occured */
            slapi_ch_free((void **)&this_role);
            slapi_ch_free_string(&filter_attr_value);
            return SLAPI_ROLE_ERROR_FILTER_BAD;
        }
        if (roles_check_filter(filter)) {
            slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                          "roles_cache_create_object_from_entry - \"%s\": not allowed to use \"nsrole\" "
                          "in the role filter \"%s\".  %s is disabled.\n",
                          (char *)slapi_sdn_get_ndn(this_role->dn),
                          filter_attr_value,
                          ROLE_FILTER_ATTR_NAME);
            slapi_ch_free((void **)&this_role);
            slapi_ch_free_string(&filter_attr_value);
            return SLAPI_ROLE_ERROR_FILTER_BAD;
        }
        /* Store on the object */
        this_role->filter = filter;
        slapi_ch_free_string(&filter_attr_value);
        break;
    }

    case ROLE_TYPE_NESTED: {
        Slapi_Attr *attr = NULL;

        /* Get the list of nested roles */
        rc = slapi_entry_attr_find(role_entry, ROLE_NESTED_ATTR_NAME, &attr);

        if ((rc == 0) && attr) {
            /* Recurse to get the definition objects for them */
            Slapi_Value **va = attr_get_present_values(attr);
            int i = 0;
            char *string = NULL;
            Slapi_DN nested_role_dn;
            role_object_nested *nested_role_object = NULL;

            for (i = 0; va[i] != NULL; i++) {
                string = (char *)slapi_value_get_string(va[i]);

                /* Make a DN from the string */
                slapi_sdn_init_dn_byref(&nested_role_dn, string);

                slapi_log_err(SLAPI_LOG_PLUGIN,
                              ROLES_PLUGIN_SUBSYSTEM, "roles_cache_create_object_from_entry - dn %s, nested %s\n",
                              (char *)slapi_sdn_get_ndn(this_role->dn), string);

                /* Make a role object nested from the DN */
                rc = roles_cache_object_nested_from_dn(&nested_role_dn, &nested_role_object);

                /* Insert it into the nested list */
                if ((rc == 0) && nested_role_object) {
                    /* Add to the tree where avl_data is a role_object_nested struct */
                    rc = roles_cache_insert_object_nested(&(this_role->avl_tree), nested_role_object);
                }
                slapi_sdn_done(&nested_role_dn);
            }
        }

        break;
    }

    default:
        slapi_log_err(SLAPI_LOG_ERR, ROLES_PLUGIN_SUBSYSTEM,
                      "roles_cache_create_object_from_entry - wrong role type\n");
    }

    if (rc == 0) {
        *result = this_role;
    } else {
        slapi_ch_free((void **)&this_role);
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "<-- roles_cache_create_object_from_entry\n");

    return rc;
}

/* roles_cache_determine_class:
   ----------------------------
   Determine the type of role depending on the objectclass
    Return the type of the role
 */
static int
roles_cache_determine_class(Slapi_Entry *role_entry)
{
    /* Examine the entry's objectclass attribute */
    int found_managed = 0;
    int found_filtered = 0;
    int found_nested = 0;
    Slapi_Attr *attr = NULL;
    struct berval bv = {0};
    int rc = 0;
    int type = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "--> roles_cache_determine_class\n");

    rc = slapi_entry_attr_find(role_entry, "objectclass", &attr);
    if (rc != 0) {
        /* No objectclass, definitely an error */
        return 0;
    }

    berval_set_string(&bv, ROLE_OBJECTCLASS_MANAGED);
    rc = slapi_attr_value_find(attr, &bv);
    if (rc == 0) {
        found_managed = 1;
        type = ROLE_TYPE_MANAGED;
    }

    berval_set_string(&bv, ROLE_OBJECTCLASS_FILTERED);
    rc = slapi_attr_value_find(attr, &bv);
    if (rc == 0) {
        found_filtered = 1;
        type = ROLE_TYPE_FILTERED;
    }

    berval_set_string(&bv, ROLE_OBJECTCLASS_NESTED);
    rc = slapi_attr_value_find(attr, &bv);
    if (rc == 0) {
        found_nested = 1;
        type = ROLE_TYPE_NESTED;
    }

    if ((found_managed + found_nested + found_filtered) > 1) {
        /* Means some goofball configured a role definition which is trying to be more than one different type. error. */
        return 0;
    }

    if ((found_managed + found_nested + found_filtered) == 0) {
        /* Means this entry isn't any of the role types we handle. error. */
        return 0;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "<-- roles_cache_determine_class\n");

    /* Return the appropriate type ordinal */
    return type;
}

/* roles_cache_node_cmp:
   ---------------------
   Comparison function to add a new node in the avl tree (avl_data is of type role_object)
 */
static int
roles_cache_node_cmp(caddr_t d1, caddr_t d2)
{
    role_object *role_to_insert = (role_object *)d1;
    role_object *current_role = (role_object *)d2;

    /* role_to_insert and current_role are never NULL in that context */

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "roles_cache_node_cmp\n");

    return (slapi_sdn_compare((Slapi_DN *)role_to_insert->dn, (Slapi_DN *)current_role->dn));
}

/* roles_cache_insert_object:
   --------------------------
   Insert a new node in the avl tree of a specific suffix
 */
static int
roles_cache_insert_object(Avlnode **tree, role_object *object)
{

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "roles_cache_insert_object - %s in tree %p\n",
                  (char *)slapi_sdn_get_ndn(object->dn),
                  *tree);
    return (avl_insert(tree, (caddr_t)object, roles_cache_node_cmp, avl_dup_error));
}

/* roles_cache_node_nested_cmp:
   ----------------------------
   Comparison function to add a new node in the avl tree
 */
static int
roles_cache_node_nested_cmp(caddr_t d1, caddr_t d2)
{
    role_object_nested *role_to_insert = (role_object_nested *)d1;
    role_object_nested *current_role = (role_object_nested *)d2;

    /* role_to_insert and current_role are never NULL in that context */

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "roles_cache_node_nested_cmp\n");

    return slapi_sdn_compare(role_to_insert->dn, current_role->dn);
}

/* roles_cache_insert_object_nested:
   ---------------------------------
   Insert a new node in the avl tree of a specific suffix
 */
static int
roles_cache_insert_object_nested(Avlnode **tree, role_object_nested *object)
{
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "roles_cache_insert_object_nested - %s in tree %p: \n",
                  (char *)slapi_sdn_get_ndn(object->dn), *tree);

    return (avl_insert(tree, (caddr_t)object, roles_cache_node_nested_cmp, avl_dup_error));
}

/* roles_cache_object_nested_from_dn
   ----------------------------------
   Get the role associated to an entry DN
    Return 0: OK
    Return ENOMEM: fail
 */
static int
roles_cache_object_nested_from_dn(Slapi_DN *role_dn, role_object_nested **result)
{
    role_object_nested *nested_role = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "--> roles_cache_object_nested_from_dn\n");

    *result = NULL;

    /* Create the role cache definition */
    nested_role = (role_object_nested *)slapi_ch_calloc(1, sizeof(role_object_nested));
    if (nested_role == NULL) {
        return ENOMEM;
    }

    nested_role->dn = slapi_sdn_new();
    slapi_sdn_copy(role_dn, nested_role->dn);
    *result = nested_role;

    slapi_log_err(SLAPI_LOG_PLUGIN, ROLES_PLUGIN_SUBSYSTEM,
                  "<-- roles_cache_object_nested_from_dn\n");
    return 0;
}

/* roles_cache_listroles
   --------------------
   Lists all the roles an entry posesses
    return_values = 0 means that we don't need the nsrole values
    return_values = 1 means that we need the nsrole values
    Return 0: the entry has nsrole
    Return -1: the entry has no nsrole
 */
int
roles_cache_listroles(Slapi_Entry *entry, int return_values, Slapi_ValueSet **valueset_out)
{
    return roles_cache_listroles_ext(NULL, entry, return_values, valueset_out);
}

int
roles_cache_listroles_ext(vattr_context *c, Slapi_Entry *entry, int return_values, Slapi_ValueSet **valueset_out)
{
    roles_cache_def *roles_cache = NULL;
    int rc = 0;
    roles_cache_build_result arg;
    Slapi_Backend *be = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_listroles\n");

    be = slapi_mapping_tree_find_backend_for_sdn(slapi_entry_get_sdn(entry));
    if ((be != NULL) && slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
        /* the entry is not local, so don't return anything */
        return (-1);
    }

    if (return_values) {
        *valueset_out = (Slapi_ValueSet *)slapi_ch_calloc(1, sizeof(Slapi_ValueSet));
        slapi_valueset_init(*valueset_out);
    }

    /* First get a list of all the in-scope roles */
    /* XXX really need a mutex for this read operation ? */
    slapi_rwlock_rdlock(global_lock);

    rc = roles_cache_find_roles_in_suffix(slapi_entry_get_sdn(entry), &roles_cache);

    slapi_rwlock_unlock(global_lock);

    /* Traverse the tree checking if the entry has any of the roles */
    if (roles_cache != NULL) {
        if (roles_cache->avl_tree) {
            arg.nsrole_values = valueset_out;
            arg.need_value = return_values;
            arg.requested_entry = entry;
            arg.has_value = 0;
            arg.context = c;

            /* XXX really need a mutex for this read operation ? */
            slapi_rwlock_rdlock(roles_cache->cache_lock);

            avl_apply(roles_cache->avl_tree, (IFP)roles_cache_build_nsrole, &arg, -1, AVL_INORDER);

            slapi_rwlock_unlock(roles_cache->cache_lock);

            if (!arg.has_value) {
                if (return_values) {
                    slapi_valueset_free(*valueset_out);
                    *valueset_out = NULL;
                }
                rc = -1;
            }
            /* Free the list (we already did that) */
        } else {
            if (return_values) {
                slapi_valueset_free(*valueset_out);
                *valueset_out = NULL;
            }
            rc = -1;
        }
    } else {
        /* no roles associated */
        rc = -1;
    }
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_listroles\n");
    return rc;
}

/* roles_cache_build_nsrole
   ------------------------
   Traverse the tree containing roles definitions for a suffix and for each
   one of them, check wether the entry is a member of it or not
   For ones which check out positive, we add their DN to the value
   always return 0 to allow to trverse all the tree
 */
static int
roles_cache_build_nsrole(caddr_t data, caddr_t arg)
{
    Slapi_Value *value = NULL;
    roles_cache_build_result *result = (roles_cache_build_result *)arg;
    role_object *this_role = (role_object *)data;
    roles_cache_search_in_nested get_nsrole;
    /* Return a value different from the stop flag to be able
       to go through all the tree */
    int rc = 0;
    int tmprc = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_build_nsrole: role %s\n",
                  (char *)slapi_sdn_get_ndn(this_role->dn));

    value = slapi_value_new_string("");

    get_nsrole.is_entry_member_of = result->requested_entry;
    get_nsrole.present = 0;
    get_nsrole.hint = 0;

    tmprc = roles_is_entry_member_of_object_ext(result->context, (caddr_t)this_role, (caddr_t)&get_nsrole);
    if (SLAPI_VIRTUALATTRS_LOOP_DETECTED == tmprc) {
        /* all we want to detect and return is loop/stack overflow */
        rc = tmprc;
    }

    /* If so, add its DN to the attribute */
    if (get_nsrole.present) {
        result->has_value = 1;
        if (result->need_value) {
            slapi_value_set_string(value, (char *)slapi_sdn_get_ndn(this_role->dn));
            slapi_valueset_add_value(*(result->nsrole_values), value);
        } else {
            /* we don't need the value but we already know there is one nsrole.
               stop the traversal
             */
            rc = -1;
        }
    }

    slapi_value_free(&value);

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_build_nsrole\n");

    return rc;
}


/* roles_check
   -----------
   Checks if an entry has a presented role, assuming that we've already verified
that
   the role both exists and is in scope
       return 0: no processing error
       return -1: error
 */
int
roles_check(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present)
{
    roles_cache_def *roles_cache = NULL;
    role_object *this_role = NULL;
    roles_cache_search_in_nested get_nsrole;

    int rc = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_check\n");

    *present = 0;

    slapi_rwlock_rdlock(global_lock);

    if (roles_cache_find_roles_in_suffix(slapi_entry_get_sdn(entry_to_check),
                                         &roles_cache) != 0) {
        slapi_rwlock_unlock(global_lock);
        return -1;
    }
    slapi_rwlock_unlock(global_lock);

    this_role = (role_object *)avl_find(roles_cache->avl_tree, role_dn, (IFP)roles_cache_find_node);

    /* MAB: For some reason the assumption made by this function (the role exists and is in scope)
     * does not seem to be true... this_role might be NULL after the avl_find call (is the avl_tree
     * broken? Anyway, this is crashing the 5.1 server on 29-Aug-01, so I am applying the following patch
     * to avoid the crash inside roles_is_entry_member_of_object */
    /* Begin patch */
    if (!this_role) {
        /* Assume that the entry is not member of the role (*present=0) and leave... */
        return rc;
    }
    /* End patch */

    get_nsrole.is_entry_member_of = entry_to_check;
    get_nsrole.present = 0;
    get_nsrole.hint = 0;

    roles_is_entry_member_of_object((caddr_t)this_role, (caddr_t)&get_nsrole);
    *present = get_nsrole.present;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_check\n");

    return rc;
}

/* roles_cache_find_node:
   ---------------------
   Comparison function to add a new node in the avl tree
 */
static int
roles_cache_find_node(caddr_t d1, caddr_t d2)
{
    Slapi_DN *data = (Slapi_DN *)d1;
    role_object *role = (role_object *)d2;

    /* role is not NULL in that context */

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "roles_cache_find_node: %s %s\n",
                  slapi_sdn_get_dn(data), slapi_sdn_get_dn(role->dn));

    return (slapi_sdn_compare(data, (Slapi_DN *)role->dn));
}

/* roles_cache_find_roles_in_suffix
   -------------------------------
   Find all the roles in scope to an entry
 */
static int
roles_cache_find_roles_in_suffix(Slapi_DN *target_entry_dn, roles_cache_def **list_of_roles)
{
    int rc = -1;
    Slapi_Backend *be = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_find_roles_in_suffix\n");

    *list_of_roles = NULL;
    be = slapi_mapping_tree_find_backend_for_sdn(target_entry_dn);
    if ((be != NULL) && !slapi_be_is_flag_set(be, SLAPI_BE_FLAG_REMOTE_DATA)) {
        Slapi_DN *suffix = roles_cache_get_top_suffix((Slapi_DN *)slapi_be_getsuffix(be, 0));
        roles_cache_def *current_role = roles_list;

        /* Go through all the roles list and trigger the associated structure */
        while ((current_role != NULL) && (suffix != NULL)) {
            if (slapi_sdn_compare(current_role->suffix_dn, suffix) == 0) {
                *list_of_roles = current_role;
                /* OK, we have found one */
                slapi_sdn_free(&suffix);
                return 0;
            } else {
                current_role = current_role->next;
            }
        }
        if (suffix != NULL) {
            slapi_sdn_free(&suffix);
        }
        /* If we got out that way, means that we didn't have find
           roles definitions for that suffix */
        return rc;
    }

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_find_roles_in_suffix\n");
    return rc;
}

/* roles_is_entry_member_of_object
   --------------------------------
   Check if the entry is part of a role defined in its suffix
    return 0: ok
    return 1: fail
    -> to check the presence, see present
 */
static int
roles_is_entry_member_of_object(caddr_t data, caddr_t argument)
{
    return roles_is_entry_member_of_object_ext(NULL, data, argument);
}

static int
roles_is_entry_member_of_object_ext(vattr_context *c, caddr_t data, caddr_t argument)
{
    int rc = -1;

    roles_cache_search_in_nested *get_nsrole = (roles_cache_search_in_nested *)argument;
    role_object *this_role = (role_object *)data;

    Slapi_Entry *entry_to_check = get_nsrole->is_entry_member_of;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_is_entry_member_of_object\n");

    if (!this_role) {
        slapi_log_err(SLAPI_LOG_ERR,
                      ROLES_PLUGIN_SUBSYSTEM, "roles_is_entry_member_of_object - NULL role\n");
        goto done;
    }

    if (!roles_is_inscope(entry_to_check, this_role)) {
        slapi_log_err(SLAPI_LOG_PLUGIN,
                      ROLES_PLUGIN_SUBSYSTEM, "roles_is_entry_member_of_object - Entry not in scope of role\n");
        return rc;
    }

    if (this_role != NULL) {
        /* Determine the role type */
        switch (this_role->type) {
        case ROLE_TYPE_MANAGED:
            rc = roles_check_managed(entry_to_check, this_role, &get_nsrole->present);
            break;
        case ROLE_TYPE_FILTERED:
            rc = roles_check_filtered(c, entry_to_check, this_role, &get_nsrole->present);
            break;
        case ROLE_TYPE_NESTED: {
            /* Go through the tree of the nested DNs */
            get_nsrole->hint++;
            avl_apply(this_role->avl_tree, (IFP)roles_check_nested, get_nsrole, 0, AVL_INORDER);
            get_nsrole->hint--;

            /* kexcoff?? */
            rc = get_nsrole->present;
            break;
        }
        default:
            slapi_log_err(SLAPI_LOG_ERR,
                          ROLES_PLUGIN_SUBSYSTEM, "roles_is_entry_member_of_object - invalid role type\n");
        }
    }
done:
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_is_entry_member_of_object\n");
    return rc;
}

/* roles_check_managed
   -------------------------
   Check a managed role: we just need to check the content of the entry's nsRoleDN attribute
   against the role DN
    return 0: ok
    return 1: fail
    -> to check the presence, see present
 */
static int
roles_check_managed(Slapi_Entry *entry_to_check, role_object *role, int *present)
{
    int rc = 0;
    Slapi_Attr *attr = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_check_managed\n");
    /* Get the attribute */
    rc = slapi_entry_attr_find(entry_to_check, ROLE_MANAGED_ATTR_NAME, &attr);

    if (rc == 0) {
        struct berval bv = {0};
        char *dn_string = NULL;

        /* Check content against the presented DN */
        /* We assume that this function handles normalization and so on */
        dn_string = (char *)slapi_sdn_get_ndn(role->dn);
        berval_set_string(&bv, dn_string);
        rc = slapi_attr_value_find(attr, &bv);
        if (rc == 0) {
            *present = 1;
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM,
                  "<-- roles_check_managed - entry %s role %s present %d\n",
                  slapi_entry_get_dn_const(entry_to_check), (char *)slapi_sdn_get_ndn(role->dn), *present);
    return rc;
}

/* roles_check_filtered
   --------------------------
   Check a filtered role: call slapi_filter_test here on the entry
   and the filter from the role object
    return 0: ok
    return 1: fail
    -> to check the presence, see present
 */
static int
roles_check_filtered(vattr_context *c, Slapi_Entry *entry_to_check, role_object *role, int *present)
{
    int rc = 0;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_check_filtered\n");
    rc = slapi_vattr_filter_test_ext(slapi_vattr_get_pblock_from_context(c),
                                     entry_to_check, role->filter, 0, 0);
    if (rc == 0) {
        *present = 1;
    }
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM,
                  "<-- roles_check_filtered - Entry %s role %s present %d\n",
                  slapi_entry_get_dn_const(entry_to_check), (char *)slapi_sdn_get_ndn(role->dn), *present);
    return rc;
}


/* roles_check_nested
   ------------------------
   Check a nested role
    return 0: ok
    return -1: fail
    -> to check the presence, see present
 */
static int
roles_check_nested(caddr_t data, caddr_t arg)
{
    roles_cache_search_in_nested *get_nsrole = (roles_cache_search_in_nested *)arg;
    int rc = -1;
    role_object_nested *current_nested_role = (role_object_nested *)data;


    /* do not allow circular dependencies, the cheap and easy way */
    if (get_nsrole->hint > MAX_NESTED_ROLES) {
        char *ndn = NULL;

        ndn = slapi_entry_get_ndn(get_nsrole->is_entry_member_of);
        slapi_log_err(SLAPI_LOG_ERR,
                      ROLES_PLUGIN_SUBSYSTEM,
                      "roles_check_nested - Maximum roles nesting exceeded (max %d current %d), not checking roles in entry %s--probable circular definition\n",
                      MAX_NESTED_ROLES,
                      get_nsrole->hint,
                      ndn);

        /* Stop traversal value */
        return 0;
    }

    /* Here we traverse the list of nested roles, calling the appropriate
        evaluation function for those in turn */

    if (current_nested_role) {
        roles_cache_def *roles_cache = NULL;
        role_object *this_role = NULL;

        slapi_log_err(SLAPI_LOG_PLUGIN,
                      ROLES_PLUGIN_SUBSYSTEM,
                      "roles_check_nested - entry %s role %s present %d\n",
                      slapi_entry_get_dn_const(get_nsrole->is_entry_member_of),
                      (char *)slapi_sdn_get_ndn(current_nested_role->dn),
                      get_nsrole->present);

        if (roles_cache_find_roles_in_suffix(current_nested_role->dn,
                                             &roles_cache) != 0) {
            return rc;
        }

        if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
            avl_apply(roles_cache->avl_tree, (IFP)roles_cache_dump, &rc, -1, AVL_INORDER);
        }

        this_role = (role_object *)avl_find(roles_cache->avl_tree,
                                            current_nested_role->dn,
                                            (IFP)roles_cache_find_node);

        if (this_role == NULL) {
            /* the nested role doesn't exist */
            slapi_log_err(SLAPI_LOG_ERR,
                          ROLES_PLUGIN_SUBSYSTEM,
                          "roles_check_nested - The nested role %s doesn't exist\n",
                          (char *)slapi_sdn_get_ndn(current_nested_role->dn));
            return rc;
        }
        /* get the role_object data associated to that dn */
        if (roles_is_inscope(get_nsrole->is_entry_member_of, this_role)) {
            /* The list of nested roles is contained in the role definition */
            roles_is_entry_member_of_object((caddr_t)this_role, (caddr_t)get_nsrole);
            if (get_nsrole->present == 1) {
                return 0;
            }
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_check_nested\n");
    return rc;
}

/* roles_is_inscope
   ----------------------
   Tells us if a presented role is in scope with respect to the presented entry
 */
static int
roles_is_inscope(Slapi_Entry *entry_to_check, role_object *this_role)
{
    int rc;

    Slapi_DN role_parent;
    Slapi_DN *scope_dn = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_is_inscope\n");

    if (this_role->rolescopedn) {
        scope_dn = this_role->rolescopedn;
    } else {
        scope_dn = this_role->dn;
    }
    slapi_sdn_init(&role_parent);
    slapi_sdn_get_parent(scope_dn, &role_parent);

    rc = slapi_sdn_scope_test(slapi_entry_get_sdn(entry_to_check),
                              &role_parent,
                              LDAP_SCOPE_SUBTREE);
    /* we need to check whether the entry would be returned by a view in scope */
    if (!rc && views_api) {
        rc = views_entry_exists(views_api, (char *)slapi_sdn_get_ndn(&role_parent), entry_to_check);
    }

    slapi_sdn_done(&role_parent);


    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_is_inscope: entry %s role %s result %d\n",
                  slapi_entry_get_dn_const(entry_to_check), (char *)slapi_sdn_get_ndn(scope_dn), rc);

    return (rc);
}

static void
berval_set_string(struct berval *bv, const char *string)
{
    bv->bv_len = strlen(string);
    bv->bv_val = (void *)string; /* We cast away the const, but we're not going to change anything
*/
}

/* roles_cache_role_def_delete
   ----------------------------
*/
static void
roles_cache_role_def_delete(roles_cache_def *role_def)
{
    roles_cache_def *current = roles_list;
    roles_cache_def *previous = NULL;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_role_def_delete\n");

    while (current != NULL) {
        if (slapi_sdn_compare(current->suffix_dn, role_def->suffix_dn) == 0) {
            if (previous == NULL) {
                roles_list = current->next;
            } else {
                previous->next = current->next;
            }
            slapi_lock_mutex(role_def->change_lock);
            role_def->keeprunning = 0;
            slapi_notify_condvar(role_def->something_changed, 1);
            slapi_unlock_mutex(role_def->change_lock);
            break;
        } else {
            previous = current;
            current = current->next;
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_role_def_delete\n");
}

/* roles_cache_role_def_free
   ----------------------------
*/
static void
roles_cache_role_def_free(roles_cache_def *role_def)
{
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_role_def_free\n");
    if (role_def == NULL) {
        return;
    }

    slapi_lock_mutex(role_def->stop_lock);

    avl_free(role_def->avl_tree, (IFP)roles_cache_role_object_free);
    slapi_sdn_free(&(role_def->suffix_dn));
    slapi_destroy_rwlock(role_def->cache_lock);
    role_def->cache_lock = NULL;
    slapi_destroy_mutex(role_def->change_lock);
    role_def->change_lock = NULL;
    slapi_destroy_condvar(role_def->something_changed);
    role_def->something_changed = NULL;
    slapi_destroy_mutex(role_def->create_lock);
    role_def->create_lock = NULL;
    slapi_destroy_condvar(role_def->suffix_created);
    role_def->suffix_created = NULL;

    slapi_ch_free((void **)&role_def->notified_dn);
    if (role_def->notified_entry != NULL) {
        slapi_entry_free(role_def->notified_entry);
    }

    slapi_unlock_mutex(role_def->stop_lock);
    slapi_destroy_mutex(role_def->stop_lock);
    role_def->stop_lock = NULL;

    slapi_ch_free((void **)&role_def);

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_role_def_free\n");
}

/* roles_cache_role_object_free
   ----------------------------
*/
static void
roles_cache_role_object_free(role_object *this_role)
{
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_role_object_free\n");

    if (this_role == NULL) {
        return;
    }

    switch (this_role->type) {
    case ROLE_TYPE_MANAGED:
        /* Nothing further needed */
        break;
    case ROLE_TYPE_FILTERED:
        /* Free the filter */
        if (this_role->filter) {
            slapi_filter_free(this_role->filter, 1);
            this_role->filter = NULL;
        }
        break;
    case ROLE_TYPE_NESTED:
        /* Free the list of nested roles */
        {
            avl_free(this_role->avl_tree, roles_cache_role_object_nested_free);
        }
        break;
    }

    slapi_sdn_free(&this_role->dn);
    slapi_sdn_free(&this_role->rolescopedn);

    /* Free the object */
    slapi_ch_free((void **)&this_role);

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_role_object_free\n");
}

/* roles_cache_role_object_nested_free
   ------------------------------------
*/
static int
roles_cache_role_object_nested_free(role_object_nested *this_role)
{
    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "--> roles_cache_role_object_nested_free\n");

    if (this_role == NULL) {
        return 0;
    }

    slapi_sdn_free(&this_role->dn);

    /* Free the object */
    slapi_ch_free((void **)&this_role);

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "<-- roles_cache_role_object_nested_free\n");

    return 0;
}

static int
roles_cache_dump(caddr_t data, caddr_t arg __attribute__((unused)))
{
    role_object *this_role = (role_object *)data;

    slapi_log_err(SLAPI_LOG_PLUGIN,
                  ROLES_PLUGIN_SUBSYSTEM, "roles_cache_dump: %p - %s - %p\n",
                  this_role, (char *)slapi_sdn_get_ndn(this_role->dn), this_role->avl_tree);

    return 0;
}
