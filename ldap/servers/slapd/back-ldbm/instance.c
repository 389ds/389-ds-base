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
Slapi_Entry *ldbm_instance_init_config_entry(char *cn_val, char *v1, char *v2, char *v3, char *v4, char *mr);


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
    if (!cache_init(&(inst->inst_cache), DEFAULT_CACHE_SIZE,
                    DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_create", "cache_init failed\n");
        rc = -1;
        goto error;
    }

    /*
     * initialize the dn cache
     * We do so, regardless of the subtree-rename value.
     * It is needed when converting the db from DN to RDN format.
     */
    if (!cache_init(&(inst->inst_dncache), DEFAULT_DNCACHE_SIZE,
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
ldbm_instance_init_config_entry(char *cn_val, char *val1, char *val2, char *val3, char *val4, char *mr)
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

    if (mr) {
        val.bv_val = mr;
        val.bv_len = strlen(mr);
        slapi_entry_add_values(e, "nsMatchingRule", vals);
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
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        e = ldbm_instance_init_config_entry(LDBM_ENTRYRDN_STR, "subtree", 0, 0, 0, 0);
        ldbm_instance_config_add_index_entry(inst, e, flags);
        slapi_entry_free(e);
    } else {
        e = ldbm_instance_init_config_entry(LDBM_ENTRYDN_STR, "eq", 0, 0, 0, 0);
        ldbm_instance_config_add_index_entry(inst, e, flags);
        slapi_entry_free(e);
    }

    e = ldbm_instance_init_config_entry(LDBM_PARENTID_STR, "eq", 0, 0, 0, "integerOrderingMatch");
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("objectclass", "eq", 0, 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("aci", "pres", 0, 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(LDBM_NUMSUBORDINATES_STR, "pres", 0, 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(SLAPI_ATTR_UNIQUEID, "eq", 0, 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* For MMR, we need this attribute (to replace use of dncomp in delete). */
    e = ldbm_instance_init_config_entry(ATTR_NSDS5_REPLCONFLICT, "eq", "pres", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* write the dse file only on the final index */
    e = ldbm_instance_init_config_entry(SLAPI_ATTR_NSCP_ENTRYDN, "eq", 0, 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* ldbm_instance_config_add_index_entry(inst, 2, argv); */
    e = ldbm_instance_init_config_entry(LDBM_PSEUDO_ATTR_DEFAULT, "none", 0, 0, 0, 0);
    attr_index_config(be, "ldbm index init", 0, e, 1, 0, NULL);
    slapi_entry_free(e);

    if (!entryrdn_get_noancestorid()) {
        /*
         * ancestorid is special, there is actually no such attr type
         * but we still want to use the attr index file APIs.
         */
        e = ldbm_instance_init_config_entry(LDBM_ANCESTORID_STR, "eq", 0, 0, 0, "integerOrderingMatch");
        attr_index_config(be, "ldbm index init", 0, e, 1, 0, NULL);
        slapi_entry_free(e);
    }

    return 0;
}


/*
 * Check if an index has integerOrderingMatch configured in DSE.
 *
 * This function performs an internal LDAP search to check if the index
 * configuration entry has nsMatchingRule: integerOrderingMatch.
 *
 * Parameters:
 *   inst_name - backend instance name (e.g., "userRoot")
 *   index_name - name of the index to check (e.g., "parentid", "ancestorid")
 *
 * Returns:
 *   PR_TRUE if integerOrderingMatch is configured
 *   PR_FALSE if not configured or index entry doesn't exist
 */
static PRBool
ldbm_instance_index_has_int_order_in_dse(const char *inst_name, const char *index_name)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **entries = NULL;
    char *idx_dn = NULL;
    PRBool has_int_order = PR_FALSE;

    idx_dn = slapi_create_dn_string("cn=%s,cn=index,cn=%s,cn=ldbm database,cn=plugins,cn=config",
                                     index_name, inst_name);
    if (idx_dn == NULL) {
        return PR_FALSE;
    }

    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, idx_dn, LDAP_SCOPE_BASE,
                                  "(objectclass=nsIndex)", NULL, 0, NULL, NULL,
                                  plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if (entries && entries[0]) {
        Slapi_Attr *mr_attr = NULL;
        if (slapi_entry_attr_find(entries[0], "nsMatchingRule", &mr_attr) == 0) {
            Slapi_Value *sval = NULL;
            int idx;
            for (idx = slapi_attr_first_value(mr_attr, &sval);
                 idx != -1;
                 idx = slapi_attr_next_value(mr_attr, idx, &sval)) {
                const struct berval *bval = slapi_value_get_berval(sval);
                if (bval && bval->bv_val &&
                    strcasecmp(bval->bv_val, "integerOrderingMatch") == 0) {
                    has_int_order = PR_TRUE;
                    break;
                }
            }
        }
    }

    slapi_ch_free_string(&idx_dn);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return has_int_order;
}

/*
 * Check a system index for ordering mismatch between config and on-disk data.
 *
 * This function compares what's configured in DSE (nsMatchingRule) with
 * what's actually on disk. A mismatch can occur in two scenarios:
 * 1. Ordering rule is configured but disk has lexicographic order
 *    (rule was added after index was created)
 * 2. No ordering rule configured but disk has integer order
 *    (rule was removed after index was created with it)
 *
 * This function reads the first keys from the specified index and checks
 * if they are stored in lexicographic order (string: "1" < "10" < "2") or
 * integer order (numeric: "1" < "2" < "10").
 *
 * Parameters:
 *   be - backend
 *   index_name - name of the index to check (e.g., "parentid", "ancestorid")
 *
 */
static void
ldbm_instance_check_index_config(backend *be, const char *index_name)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    struct attrinfo *ai = NULL;
    dbi_db_t *db = NULL;
    dbi_cursor_t dbc = {0};
    dbi_val_t key = {0};
    dbi_val_t data = {0};
    int ret = 0;
    PRBool config_has_int_order = PR_FALSE;
    PRBool disk_has_int_order = PR_TRUE;  /* Assume integer order until proven otherwise */
    ID prev_id = 0;
    int key_count = 0;
    PRBool first_key = PR_TRUE;
    PRBool found_ordering_evidence = PR_FALSE;

    slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_instance_check_index_config",
            "Backend '%s': checking %s index ordering...\n",
            inst->inst_name, index_name);

    /* Check if integerOrderingMatch is configured in DSE */
    config_has_int_order = ldbm_instance_index_has_int_order_in_dse(inst->inst_name, index_name);

    /* Get attrinfo for the index */
    ainfo_get(be, (char *)index_name, &ai);
    if (ai == NULL || strcmp(ai->ai_type, index_name) != 0) {
        /* No index config found */
        slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_instance_check_index_config",
                "Backend '%s': no %s attrinfo found, skipping check\n",
                inst->inst_name, index_name);
        return;
    }

    /* Open the index file */
    ret = dblayer_get_index_file(be, ai, &db, 0);
    if (ret != 0 || db == NULL) {
        /* Index file doesn't exist or can't be opened - this is fine for new instances */
        slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_instance_check_index_config",
                "Backend '%s': could not open %s index file (ret=%d), skipping order check\n",
                inst->inst_name, index_name, ret);
        return;
    }

    /* Create a cursor to read keys */
    ret = dblayer_new_cursor(be, db, NULL, &dbc);
    if (ret != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_check_index_config",
                "Backend '%s': could not create cursor on %s index (ret=%d)\n",
                inst->inst_name, index_name, ret);
        dblayer_release_index_file(be, ai, db);
        return;
    }

    dblayer_value_init(be, &key);
    dblayer_value_init(be, &data);

    /*
     * Read up to 100 unique keys and check their ordering.
     * With lexicographic ordering: "1" < "10" < "100" < "2" < "20" < "3"
     * With integer ordering: "1" < "2" < "3" < "10" < "20" < "100"
     *
     * If we find a case where prev_id > current_id (numerically), but the
     * keys are still in order (lexicographically), then the index uses
     * lexicographic ordering.
     */
    while (key_count < 100) {
        ID current_id;

        ret = dblayer_cursor_op(&dbc, first_key ? DBI_OP_MOVE_TO_FIRST : DBI_OP_NEXT_KEY, &key, &data);
        first_key = PR_FALSE;  /* Always advance cursor on next iteration */
        if (ret != 0) {
            break;  /* No more keys or error */
        }

        /* Skip non-equality keys */
        if (key.size < 2 || *(char *)key.data != EQ_PREFIX) {
            continue;
        }

        /* Parse the ID from the key (format: "=<id>") */
        current_id = (ID)strtoul((char *)key.data + 1, NULL, 10);
        if (current_id == 0) {
            continue;  /* Invalid ID, skip */
        }

        key_count++;

        if (prev_id != 0) {
            /*
             * Check ordering: if prev_id > current_id numerically,
             * but we got this key after prev in DB order, then
             * the index is using lexicographic ordering.
             *
             * Example: if we see "10" followed by "2", that's lexicographic
             * because "10" < "2" as strings, but 10 > 2 as integers.
             */
            if (prev_id > current_id) {
                /* Found evidence of lexicographic ordering */
                disk_has_int_order = PR_FALSE;
                found_ordering_evidence = PR_TRUE;
                break;
            } else if (prev_id < current_id) {
                /*
                 * This is consistent with integer ordering, but we need
                 * to find a case that proves lexicographic ordering.
                 * For example, seeing "1" followed by "2" is ambiguous,
                 * but seeing "1" followed by "10" (not "2") proves lexicographic.
                 *
                 * A definitive test: if we see an ID followed by a smaller
                 * ID, that's lexicographic. If all IDs are strictly increasing,
                 * it could be either (or the index only has sequential IDs).
                 */
                found_ordering_evidence = PR_TRUE;
            }
        }
        prev_id = current_id;
    }

    /* Close the cursor and free values */
    dblayer_cursor_op(&dbc, DBI_OP_CLOSE, NULL, NULL);
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);

    /* Release the index file */
    dblayer_release_index_file(be, ai, db);

    /*
     * Report findings and check for config/disk mismatch.
     * Log an error if there's a discrepancy between what's configured
     * in DSE and what's actually on disk.
     */
    if (!found_ordering_evidence) {
        slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_instance_check_index_config",
                "Backend '%s': %s index ordering check - "
                "could not determine on-disk ordering (index may be empty or have sequential IDs only). "
                "Config has integerOrderingMatch: %s\n",
                inst->inst_name, index_name, config_has_int_order ? "yes" : "no");
    } else if (config_has_int_order && !disk_has_int_order) {
        /* Config expects integer ordering, but disk has lexicographic - MISMATCH */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_check_index_config",
                "Backend '%s': MISMATCH - %s index has integerOrderingMatch configured, "
                "but on-disk data uses lexicographic ordering. "
                "This will cause searches to return incorrect or incomplete results. "
                "Please reindex the %s attribute: "
                "dsconf <instance> backend index reindex --attr %s %s\n",
                inst->inst_name, index_name, index_name, index_name, inst->inst_name);
    } else if (!config_has_int_order && disk_has_int_order) {
        /* Config expects lexicographic ordering, but disk has integer - MISMATCH */
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_instance_check_index_config",
                "Backend '%s': MISMATCH - %s index does not have integerOrderingMatch configured, "
                "but on-disk data uses integer ordering. "
                "This will cause searches to return incorrect or incomplete results. "
                "Please reindex the %s attribute: "
                "dsconf <instance> backend index reindex --attr %s %s\n",
                inst->inst_name, index_name, index_name, index_name, inst->inst_name);
    } else {
        /* Config and disk ordering match - no action needed */
        slapi_log_err(SLAPI_LOG_DEBUG, "ldbm_instance_check_index_config",
                "Backend '%s': %s index ordering check passed - "
                "config has integerOrderingMatch: %s, on-disk data matches.\n",
                inst->inst_name, index_name, config_has_int_order ? "yes" : "no");
    }
}

/*
 * Check system indexes for ordering mismatches.
 * If a mismatch is detected, log an error advising the administrator
 * to reindex the affected attribute.
 *
 * Note: We only check parentid here. The ancestorid index is a special
 * system index that has no DSE config entry - its ordering is hardcoded
 * in ldbm_instance_init_config_entry() and cannot be changed by users.
 */
static void
ldbm_instance_check_indexes(backend *be)
{
    /* Check parentid index */
    ldbm_instance_check_index_config(be, LDBM_PARENTID_STR);
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
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        cache_destroy_please(&inst->inst_dncache, CACHE_TYPE_DN);
    }
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
            /* Check index configuration for potential issues */
            ldbm_instance_check_indexes(inst->inst_be);
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
