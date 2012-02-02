/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "back-ldbm.h"

/* Forward declarations */
static void ldbm_instance_destructor(void **arg);
Slapi_Entry *ldbm_instance_init_config_entry(char *cn_val, char *v1, char *v2, char *v3, char *v4);


/* Creates and initializes a new ldbm_instance structure.
 * Also sets up some default indexes for the new instance.
 */
int ldbm_instance_create(backend *be, char *name)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    ldbm_instance *inst = NULL;
    int rc = 0;

    /* Allocate storage for the ldbm_instance structure.  Information specific
     * to this instance of the ldbm backend will be held here. */
    inst = (ldbm_instance *) slapi_ch_calloc(1, sizeof(ldbm_instance));

    /* Record the name of this instance. */
    inst->inst_name = slapi_ch_strdup(name);

    /* initialize the entry cache */
    if (! cache_init(&(inst->inst_cache), DEFAULT_CACHE_SIZE,
                     DEFAULT_CACHE_ENTRIES, CACHE_TYPE_ENTRY)) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: cache_init failed\n",
                  0, 0, 0);
        rc = -1;
        goto error;
    }

    /*
     * initialize the dn cache 
     * We do so, regardless of the subtree-rename value.
     * It is needed when converting the db from DN to RDN format.
     */
    if (! cache_init(&(inst->inst_dncache), DEFAULT_DNCACHE_SIZE,
                     DEFAULT_DNCACHE_MAXCOUNT, CACHE_TYPE_DN)) {
        LDAPDebug0Args(LDAP_DEBUG_ANY,
                       "ldbm_instance_create: dn cache_init failed\n");
        rc = -1;
        goto error;
    }

    /* Lock for the list of open db handles */
    inst->inst_handle_list_mutex = PR_NewLock();
    if (NULL == inst->inst_handle_list_mutex) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: PR_NewLock failed\n",
                  0, 0, 0);
        rc = -1;
        goto error;
    }

    /* Lock used to synchronize modify operations. */
    inst->inst_db_mutex = PR_NewMonitor();
    if (NULL == inst->inst_db_mutex) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: PR_NewMonitor failed\n",
                  0, 0, 0);
        rc = -1;
        goto error;
    }

    if ((inst->inst_config_mutex = PR_NewLock()) == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: PR_NewLock failed\n",
                  0, 0, 0);
        rc = -1;
        goto error;
    }

    if ((inst->inst_nextid_mutex = PR_NewLock()) == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: PR_NewLock failed\n",
                  0, 0, 0);
        rc = -1;
        goto error;
    }

    if ((inst->inst_indexer_cv = PR_NewCondVar(inst->inst_nextid_mutex)) == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY, "ldbm_instance_create: PR_NewCondVar failed\n", 0, 0, 0 );
        rc = -1;
        goto error;
    }

    inst->inst_be = be;
    inst->inst_li = li;
    be->be_instance_info = inst;

    /* Initialize the fields with some default values. */
    ldbm_instance_config_setup_default(inst);

    /* Add this new instance to the the set of instances */
    {
        Object *instance_obj;

        instance_obj = object_new((void *) inst, &ldbm_instance_destructor);
        objset_add_obj(li->li_instance_set, instance_obj);
        object_release(instance_obj);
    }
    goto done;

error:
    slapi_ch_free_string(&inst->inst_name);
    slapi_ch_free((void**)&inst);

done:
    return rc;
}

/*
 * Take a bunch of strings, and create a index config entry
 */
Slapi_Entry *
ldbm_instance_init_config_entry(char *cn_val, char *val1, char *val2, char *val3, char *val4){
    Slapi_Entry *e = slapi_entry_alloc();
    struct berval *vals[2];
    struct berval val;

    vals[0] = &val;
    vals[1] = NULL;

    slapi_entry_set_dn(e,slapi_ch_strdup("cn=indexContainer"));

    val.bv_val = cn_val;
    val.bv_len = strlen(cn_val);
    slapi_entry_add_values(e,"cn",vals);

    val.bv_val = val1;
    val.bv_len = strlen(val1);
    slapi_entry_add_values(e,"nsIndexType",vals);

    if(val2){
        val.bv_val = val2;
        val.bv_len = strlen(val2);
        slapi_entry_add_values(e,"nsIndexType",vals);
    }
    if(val3){
        val.bv_val = val3;
        val.bv_len = strlen(val3);
        slapi_entry_add_values(e,"nsIndexType",vals);
    }
    if(val4){
        val.bv_val = val4;
        val.bv_len = strlen(val4);
        slapi_entry_add_values(e,"nsIndexType",vals);
    }

    return e;
}

/* create the default indexes separately
 * (because when we're creating a new backend while the server is running,
 * the DSE needs to be pre-seeded first.)
 */
int ldbm_instance_create_default_indexes(backend *be)
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
        e = ldbm_instance_init_config_entry(LDBM_ENTRYRDN_STR,"subtree", 0, 0, 0);
        ldbm_instance_config_add_index_entry(inst, e, flags);
        slapi_entry_free(e);
    } else {
        e = ldbm_instance_init_config_entry(LDBM_ENTRYDN_STR,"eq", 0, 0, 0);
        ldbm_instance_config_add_index_entry(inst, e, flags);
        slapi_entry_free(e);
    }

    e = ldbm_instance_init_config_entry(LDBM_PARENTID_STR,"eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("objectclass","eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry("aci","pres", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

#if 0    /* don't need copiedfrom */
    e = ldbm_instance_init_config_entry("copiedfrom","pres",0 ,0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);
#endif

    e = ldbm_instance_init_config_entry(LDBM_NUMSUBORDINATES_STR,"pres", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    e = ldbm_instance_init_config_entry(SLAPI_ATTR_UNIQUEID,"eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* For MMR, we need this attribute (to replace use of dncomp in delete). */
    e = ldbm_instance_init_config_entry(ATTR_NSDS5_REPLCONFLICT,"eq", "pres", 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* write the dse file only on the final index */
    e = ldbm_instance_init_config_entry(SLAPI_ATTR_NSCP_ENTRYDN,"eq", 0, 0, 0);
    ldbm_instance_config_add_index_entry(inst, e, flags);
    slapi_entry_free(e);

    /* ldbm_instance_config_add_index_entry(inst, 2, argv); */
    e = ldbm_instance_init_config_entry(LDBM_PSEUDO_ATTR_DEFAULT,"none", 0, 0, 0);
    attr_index_config( be, "ldbm index init", 0, e, 1, 0 );
    slapi_entry_free(e);

    if (!entryrdn_get_noancestorid()) {
        /* 
         * ancestorid is special, there is actually no such attr type
         * but we still want to use the attr index file APIs.
         */
        e = ldbm_instance_init_config_entry(LDBM_ANCESTORID_STR,"eq", 0, 0, 0);
        attr_index_config( be, "ldbm index init", 0, e, 1, 0 );
        slapi_entry_free(e);
    }

    return 0;
}


/* Starts a backend instance */
int 
ldbm_instance_start(backend *be)
{
    int rc;
    PR_Lock (be->be_state_lock);

    if (be->be_state != BE_STATE_STOPPED &&
        be->be_state != BE_STATE_DELETED) {
        LDAPDebug( LDAP_DEBUG_TRACE, 
                   "ldbm_instance_start: warning - backend is in a wrong state - %d\n", 
                   be->be_state, 0, 0 );
        PR_Unlock (be->be_state_lock);
        return 0;
    }

    rc = dblayer_instance_start(be, DBLAYER_NORMAL_MODE);
    be->be_state = BE_STATE_STARTED;

    PR_Unlock (be->be_state_lock);

    return rc;
}


/* Stops a backend instance */
int 
ldbm_instance_stop(backend *be)
{
    int rc;
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    PR_Lock (be->be_state_lock);

    if (be->be_state != BE_STATE_STARTED) {
        LDAPDebug( LDAP_DEBUG_ANY, 
                   "ldbm_back_close: warning - backend %s is in the wrong state - %d\n", 
                   inst ? inst->inst_name : "", be->be_state, 0 );
        PR_Unlock (be->be_state_lock);
        return 0;
    }

    rc = dblayer_instance_close(be);

    be->be_state = BE_STATE_STOPPED;
    PR_Unlock (be->be_state_lock);

    cache_destroy_please(&inst->inst_cache, CACHE_TYPE_ENTRY);
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        cache_destroy_please(&inst->inst_dncache, CACHE_TYPE_DN);
    }

    return rc;
}


/* Walks down the set of instances, starting each one. */
int 
ldbm_instance_startall(struct ldbminfo *li)
{
    Object *inst_obj;
    ldbm_instance *inst;
    int rc = 0;

    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL)  {
        int rc1;
        inst = (ldbm_instance *) object_get_data(inst_obj);
        rc1 = ldbm_instance_start(inst->inst_be);
    if (rc1 != 0) {
        rc = rc1;
    } else {
        vlv_init(inst);
        slapi_mtn_be_started(inst->inst_be);
    }
        inst_obj = objset_next_obj(li->li_instance_set, inst_obj);
    }

    return rc;
}


/* Walks down the set of instances, stopping each one. */
int ldbm_instance_stopall(struct ldbminfo *li)
{
    Object *inst_obj;
    ldbm_instance *inst;
    
    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL)  {
        inst = (ldbm_instance *) object_get_data(inst_obj);
        ldbm_instance_stop(inst->inst_be);
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

    inst_obj = objset_first_obj(li->li_instance_set);
    while (inst_obj != NULL)  {
        inst = (ldbm_instance *) object_get_data(inst_obj);
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
    ldbm_instance *inst = (ldbm_instance *) *arg;

    LDAPDebug(LDAP_DEBUG_ANY, "Destructor for instance %s called\n", 
              inst->inst_name, 0, 0);

    slapi_ch_free_string(&inst->inst_name);
    PR_DestroyLock(inst->inst_config_mutex);
    slapi_ch_free_string(&inst->inst_dir_name);
    slapi_ch_free_string(&inst->inst_parent_dir_name);
    PR_DestroyMonitor(inst->inst_db_mutex);
    PR_DestroyLock(inst->inst_handle_list_mutex);
    PR_DestroyLock(inst->inst_nextid_mutex);
    PR_DestroyCondVar(inst->inst_indexer_cv);
    attrinfo_deletetree(inst);
    if (inst->inst_dataversion) {
        slapi_ch_free((void **)&inst->inst_dataversion);
    }
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
