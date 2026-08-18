/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* This file handles configuration information that is specific
 * to ldbm instances.
 */

#include "mdb_layer.h"


/*------------------------------------------------------------------------
 * callback for instence entry handling in the mdb layer
 * so far only used for post delete operations, but for
 * completeness all potential callbacks are defined
 *----------------------------------------------------------------------*/
int
dbmdb_instance_postadd_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
    /* callback to be defined, does nothing for now */
    return SLAPI_DSE_CALLBACK_OK;
}

int
dbmdb_instance_add_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
    /* callback to be defined, does nothing for now */
    return SLAPI_DSE_CALLBACK_OK;
}

int
dbmdb_instance_post_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
    if (MDB_CONFIG(li)->env) {
        /* unregister the monitor */
        dbmdb_instance_unregister_monitor(inst);
    } /* non-null pEnv */
    return SLAPI_DSE_CALLBACK_OK;
}

int
dbmdb_instance_delete_instance_entry_callback(struct ldbminfo *li, struct ldbm_instance *inst)
{
    if (MDB_CONFIG(li)->env) {
        if (inst->inst_dir_name == NULL) {
            dblayer_get_instance_data_dir(inst->inst_be);
        }
        dbmdb_dbi_rmdir(inst->inst_be);

        /* unregister the monitor */
        dbmdb_instance_unregister_monitor(inst);
    } /* non-null pEnv */

    return SLAPI_DSE_CALLBACK_OK;
}

/* adding mdb instance specific attributes, instance lock must be held */
int
dbmdb_instance_search_callback(Slapi_Entry *e, int *returncode, char *returntext, ldbm_instance *inst)
{
    /* callback to be defined, does nothing for now */
    return LDAP_SUCCESS;
}

/* Returns LDAP_SUCCESS on success */
int
dbmdb_instance_config_set(ldbm_instance *inst, char *attrname, int mod_apply, int mod_op, int phase, struct berval *value)
{
    /* callback to be defined, does nothing for now */
    return LDAP_SUCCESS;
}


int
dbmdb_instance_cleanup(struct ldbm_instance *inst)
{
    /* callback to be defined, does nothing for now */
    return LDAP_SUCCESS;
}

int
dbmdb_instance_create(struct ldbm_instance *inst)
{
    /* callback to be defined, does nothing for now */
    return LDAP_SUCCESS;
}
