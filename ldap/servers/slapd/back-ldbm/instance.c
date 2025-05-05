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

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations */
static void ldbm_instance_destructor(void **arg);
Slapi_Entry *ldbm_instance_init_config_entry(char *cn_val, char *v1, char *v2, char *v3, char *v4);


/* Creates and initializes a new ldbm_instance structure.
 * Also sets up some default indexes for the new instance.
 */
int
ldbm_instance_create(backend *be, char *name)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_private *priv = (dblayer_private *)li->li_dblayer_private;
    ldbm_instance *inst = NULL;
    int rc = 0;

    /* Allocate storage for the ldbm_instance structure.  Information specific
     * to this instance of the ldbm backend will be held here. */
    inst = (ldbm_instance *)slapi_ch_calloc(1, sizeof(ldbm_instance));

    /* Record the name of this instance. */
    inst->inst_name = slapi_ch_strdup(name);

    /* initialize the entry cache */
    if (!cache_init(&(inst->inst_cache), inst, DEFAULT_CACHE_SIZE,
                    DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "cache_init failed\n");
        rc = -1;
        goto error;
    }

    /*
     * initialize the dn cache
     * It is needed when converting the db from DN to RDN format.
     */
    if (!cache_init(&(inst->inst_dncache), inst, DEFAULT_DNCACHE_SIZE,
                    DEFAULT_DNCACHE_MAXCOUNT, CACHE_TYPE_DN)) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "ldbm_instance_create", "dn cache_init failed\n");
        rc = -1;
        goto error;
    }

    /* Lock for the list of open db handles */
    inst->inst_handle_list_mutex = PR_NewLock();
    if (NULL == inst->inst_handle_list_mutex) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "PR_NewLock failed\n");
        rc = -1;
        goto error;
    }

    /* Lock used to synchronize modify operations. */
    inst->inst_db_mutex = PR_NewMonitor();
    if (NULL == inst->inst_db_mutex) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "PR_NewMonitor failed\n");
        rc = -1;
        goto error;
    }

    if ((inst->inst_config_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "PR_NewLock failed\n");
        rc = -1;
        goto error;
    }

    if ((inst->inst_nextid_mutex = PR_NewLock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "PR_NewLock failed\n");
        rc = -1;
        goto error;
    }

    if ((inst->inst_indexer_cv = PR_NewCondVar(inst->inst_nextid_mutex)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "PR_NewCondVar failed\n");
        rc = -1;
        goto error;
    }

    /* Keeps track of how many operations are currently using this instance */
    inst->inst_ref_count = slapi_counter_new();

    inst->inst_be = be;
    inst->inst_li = li;
    be->be_instance_info = inst;

    /* Initialize the fields with some default values. */
    ldbm_instance_config_setup_default(inst);

    /* Call the backend implementation specific instance creation function */
    priv->instance_create_fn(inst);

    /* Add this new instance to the the set of instances */
    {
        Object *instance_obj;

        instance_obj = object_new((void *)inst, &ldbm_instance_destructor);
        objset_add_obj(li->li_instance_set, instance_obj);
        object_release(instance_obj);
    }
    goto done;

error:
    slapi_ch_free_string(&inst->inst_name);
    slapi_ch_free((void **)&inst);

done:
    return rc;
}

/*
 * Take a bunch of strings, and create a index config entry
 */
Slapi_Entry *
ldbm_instance_init_config_entry(char *cn_val, char *val1, char *val2, char *val3, char *val4)
{
    Slapi_Entry *e = slapi_entry_alloc();
    struct berval *vals[2];
    struct berval val;

    vals[0] = &val;
    vals[1] = NULL;

    slapi_entry_set_dn(e, slapi_ch_strdup("cn=indexContainer"));

    val.bv_val = cn_val;
    val.bv_len = strlen(cn_val);
    slapi_entry_add_values(e, "cn", vals);

    val.bv_val = val1;
    val.bv_len = strlen(val1);
    slapi_entry_add_values(e, "nsIndexType", vals);

    if (val2) {
        val.bv_val = val2;
        val.bv_len = strlen(val2);
        slapi_entry_add_values(e, "nsIndexType", vals);
    }
    if (val3) {
        val.bv_val = val3;
        val.bv_len = strlen(val3);
        slapi_entry_add_values(e, "nsIndexType", vals);
    }
    if (val4) {
        val.bv_val = val4;
        val.bv_len = strlen(val4);
        slapi_entry_add_values(e, "nsIndexType", vals);
    }

    return e;
}

/* create the default indexes separately
 * (because when we're creating a new backend while the server is running,
 * the DSE needs to be pre-seeded first.)
 */
int
ldbm_instance_create_default_indexes(backend *be)
{
    Slapi_Entry *e;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    /* write the dse file only on the final index */
    int flags = LDBM_INSTANCE_CONFIG_DONT_WRITE;

    /*
     * Always index (entrydn or entryrdn), parentid, objectclass,
     * subordinatecount, copiedFrom, and aci,
     * since they are used by some searches, replication and the
     * ACL routines.
     */
    e = ldbm_instance_init_config_entry(LDBM_ENTRYRDN_STR, "subtree", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(LDBM_PARENTID_STR, "eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("objectclass", "eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("aci", "pres", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(LDBM_NUMSUBORDINATES_STR, "pres", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(SLAPI_ATTR_UNIQUEID, "eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* For MMR, we need this attribute (to replace use of dncomp in delete). */
    e = ldbm_instance_init_config_entry(ATTR_NSDS5_REPLCONFLICT, "eq", "pres", 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* write the dse file only on the final index */
    e = ldbm_instance_init_config_entry(SLAPI_ATTR_NSCP_ENTRYDN, "eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* ldbm_instance_config_add_index_entry(inst, 2, argv); */
    e = ldbm_instance_init_config_entry(LDBM_PSEUDO_ATTR_DEFAULT, "none", 0, 0, 0);
    attr_index_config(be, "ldbm index init", 0, e, 1, 0, NULL);
    slapi_entry_free(e);

    /*
     * ancestorid is special, there is actually no such attr type
     * but we still want to use the attr index file APIs.
     */
    e = ldbm_instance_init_config_entry(LDBM_ANCESTORID_STR, "eq", 0, 0, 0);
    attr_index_config(be, "ldbm index init", 0, e, 1, 0, NULL);
    slapi_entry_free(e);

    return 0;
}


/* Starts a backend instance */
int
ldbm_instance_start(backend *be)
{
    int rc;
    PR_Lock(be->be_state_lock);

    if (be->be_state != BE_STATE_STOPPED &&
        be->be_state != BE_STATE_DELETED) {
        slapi_log_err(SLAPI_LOG_TRACE, "ldbm_instance_start",
                      "Warning - backend is in a wrong state - %d\n",
                      be->be_state);
        PR_Unlock(be->be_state_lock);
        return 0;
    }

    rc = dblayer_instance_start(be, DBLAYER_NORMAL_MODE);
    be->be_state = BE_STATE_STARTED;

    PR_Unlock(be->be_state_lock);

    return rc;
}


/* Stops a backend instance */
void
ldbm_instance_stop_cache(backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    cache_destroy_please(&inst->inst_cache, CACHE_TYPE_ENTRY);
    cache_destroy_please(&inst->inst_dncache, CACHE_TYPE_DN);
}

static void
ldbm_instance_set_flags(ldbm_instance *inst)
{
    dblayer_private *priv = (dblayer_private *)inst->inst_li->li_dblayer_private;

    if (dblayer_is_restored()) {
        slapi_be_set_flag(inst->inst_be, SLAPI_BE_FLAG_POST_RESTORE);
    }
    if (priv->dblayer_import_file_check_fn(inst)) {
        slapi_be_set_flag(inst->inst_be, SLAPI_BE_FLAG_POST_IMPORT);
    }
}

/* Walks down the set of instances, starting each one. */
int
ldbm_instance_startall(struct ldbminfo *li)
{
    Object *inst_obj;
    ldbm_instance *inst;
    int rc = 0;

    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL) {
        int rc1;
        inst = (ldbm_instance *)object_get_data(inst_obj);
        ldbm_instance_set_flags(inst);
        rc1 = ldbm_instance_start(inst->inst_be);
        if (rc1 != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_startall", "failed to start instance %s. err=%d\n", inst->inst_name, rc1);
            rc = rc1;
        } else {
            ldbm_instance_register_modify_callback(inst);
            vlv_init(inst);
            slapi_mtn_be_started(inst->inst_be);
        }
        if (slapi_exist_referral(inst->inst_be)) {
            slapi_be_set_flag(inst->inst_be, SLAPI_BE_FLAG_CONTAINS_REFERRAL);
        } else {
            slapi_be_unset_flag(inst->inst_be, SLAPI_BE_FLAG_CONTAINS_REFERRAL);
        }
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
    }

    return rc;
}


/* Walks down the set of instances, stopping each one. */
int
ldbm_instance_stopall_caches(struct ldbminfo *li)
{
    Object *inst_obj;
    ldbm_instance *inst;

    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        ldbm_instance_stop_cache(inst->inst_be);
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
    }

    return 0;
}


/* Walks down the set of instance, looking for one
 * with the given name.  Returns a pointer to the
 * instance if found, and NULL if not found.  The
 * string compare on the instance name is NOT case
 * sensitive.
 */
/* Currently this function doesn't bump
 * the ref count of the instance returned.
 */
ldbm_instance *
ldbm_instance_find_by_name(struct ldbminfo *li, char *name)
{
    Object *inst_obj;
    ldbm_instance *inst;

    if (name == NULL) {
        return NULL;
    }

    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL) {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        if (!strcasecmp(inst->inst_name, name)) {
            /* Currently we release the object here.  There is no
             * function for callers of this function to call to
             * release the object.
             */
            object_release(inst_obj);
            return inst;
        }
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
    }
    return NULL;
}

/* Called when all references to the instance are gone. */
/* (ie, only when an instance is being deleted) */
static void
ldbm_instance_destructor(void **arg)
{
    ldbm_instance *inst = (ldbm_instance *)*arg;

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_instance_destructor",
                  "Destructor for instance %s called\n",
                  inst->inst_name);

    slapi_counter_destroy(&(inst->inst_ref_count));
    slapi_ch_free_string(&inst->inst_name);
    PR_DestroyLock(inst->inst_config_mutex);
    slapi_ch_free_string(&inst->inst_dir_name);
    slapi_ch_free_string(&inst->inst_parent_dir_name);
    PR_DestroyMonitor(inst->inst_db_mutex);
    PR_DestroyLock(inst->inst_handle_list_mutex);
    PR_DestroyLock(inst->inst_nextid_mutex);
    PR_DestroyCondVar(inst->inst_indexer_cv);
    attrinfo_deletetree(inst);
    slapi_ch_free((void **)&inst->inst_dataversion);
    /* cache has already been destroyed */

    slapi_ch_free((void **)&inst);
}


static int
ldbm_instance_comparator(Object *object, const void *name)
{
    void *data = object_get_data(object);
    return (data == name) ? 0 : 1;
}


/* find the instance in the objset and remove it */
int
ldbm_instance_destroy(ldbm_instance *inst)
{
    Object *object = NULL;
    struct ldbminfo *li = inst->inst_li;

    object = objset_find(li->li_instance_set, ldbm_instance_comparator, inst);
    if (object == NULL) {
        return -1;
    }
    /* decref from objset_find */
    object_release(object);

    /* now remove from the instance set */
    objset_remove_obj(li->li_instance_set, object);
    return 0;
}
