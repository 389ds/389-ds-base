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

/* id.c - keep track of the next id to be given out */

#include "back-ldbm.h"

ID
next_id(backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    ID id;

    PR_Lock(inst->inst_nextid_mutex);

    /* Test if nextid hasn't been initialized. */
    if (inst->inst_nextid < 1) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "next_id", "nextid not initialized... exiting.\n");
        exit(1);
    }

    /* Increment the in-memory nextid */
    inst->inst_nextid++;
    id = inst->inst_nextid - 1;

    PR_Unlock(inst->inst_nextid_mutex);

    /* if ID is above the threshold, the database may need rebuilding soon */
    if (id >= ID_WARNING_THRESHOLD) {
        if (id >= MAXID) {
            slapi_log_err(SLAPI_LOG_ALERT,
                          "next_id", "FATAL ERROR: backend '%s' has no"
                                     "IDs left. DATABASE MUST BE REBUILT.\n",
                          be->be_name);
            id = MAXID;
        } else {
            slapi_log_err(SLAPI_LOG_WARNING,
                          "next_id", "Backend '%s' may run out "
                                     "of IDs. Please, rebuild database.\n",
                          be->be_name);
        }
    }
    return (id);
}

void
next_id_return(backend *be, ID id)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;

    /*Lock*/
    PR_Lock(inst->inst_nextid_mutex);

    /*Test if nextid hasn't been initialized. */
    if (inst->inst_nextid < 1) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "next_id_return", "nextid not initialized... exiting\n");
        exit(1);
    }

    if (id != inst->inst_nextid - 1) {
        PR_Unlock(inst->inst_nextid_mutex);
        return;
    }

    /*decrement the in-memory version*/
    inst->inst_nextid--;

    /*unlock this bad boy*/
    PR_Unlock(inst->inst_nextid_mutex);
}

ID
next_id_get(backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    ID id;

    /*lock*/
    PR_Lock(inst->inst_nextid_mutex);

    /*Test if nextid hasn't been initialized.*/
    if (inst->inst_nextid < 1) {
        slapi_log_err(SLAPI_LOG_CRIT,
                      "next_id_get", "nextid not initialized... exiting\n");
        exit(1);
    }

    id = inst->inst_nextid;
    PR_Unlock(inst->inst_nextid_mutex);

    return (id);
}

/*
 *  Function: get_ids_from_disk
 *
 *  Returns: squat
 *
 *  Description: Opend the id2entry file and obtains the largest
 *               ID in use, and sets li->li_nextid.  If no IDs
 *               could be read from id2entry, li->li_nextid
 *               is set to 1.
 */
void
get_ids_from_disk(backend *be)
{
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
    DB *id2entrydb; /*the id2entry database*/
    int return_value = -1;

    /*For the nextid, we go directly to the id2entry database,
   and grab the max ID*/

    /*Get a copy of the id2entry database*/
    if ((return_value = dblayer_get_id2entry(be, &id2entrydb)) != 0) {
        id2entrydb = NULL;
    }

    /* lock the nextid mutex*/
    PR_Lock(inst->inst_nextid_mutex);

    /*
   * If there is no id2entry database, then we can assume that there
   * are no entries, and that nextid should be 1
   */
    if (id2entrydb == NULL) {
        inst->inst_nextid = 1;

        /* unlock */
        PR_Unlock(inst->inst_nextid_mutex);
        return;

    } else {

        /*Get the last key*/
        DBC *dbc = NULL;
        DBT key = {0}; /*For the nextid*/
        DBT Value = {0};

        Value.flags = DB_DBT_MALLOC;
        key.flags = DB_DBT_MALLOC;
        return_value = id2entrydb->cursor(id2entrydb, NULL, &dbc, 0);
        if (0 == return_value) {
            return_value = dbc->c_get(dbc, &key, &Value, DB_LAST);
            if ((0 == return_value) && (NULL != key.dptr)) {
                inst->inst_nextid = id_stored_to_internal(key.dptr) + 1;
            } else {
                inst->inst_nextid = 1; /* error case: set 1 */
            }
            slapi_ch_free(&(key.data));
            slapi_ch_free(&(Value.data));
            dbc->c_close(dbc);
        } else {
            inst->inst_nextid = 1; /* when there is no id2entry, start from id 1 */
        }
    }

    /*close the cache*/
    dblayer_release_id2entry(be, id2entrydb);

    /* unlock */
    PR_Unlock(inst->inst_nextid_mutex);

    return;
}


/* routines to turn an internal machine-representation ID into the one we store (big-endian) */

void
id_internal_to_stored(ID i, char *b)
{
    if (sizeof(ID) > 4) {
        (void)memset(b + 4, 0, sizeof(ID) - 4);
    }

    b[0] = (char)(i >> 24);
    b[1] = (char)(i >> 16);
    b[2] = (char)(i >> 8);
    b[3] = (char)i;
}

ID
id_stored_to_internal(char *b)
{
    ID i;
    i = (ID)b[3] & 0x000000ff;
    i |= (((ID)b[2]) << 8) & 0x0000ff00;
    i |= (((ID)b[1]) << 16) & 0x00ff0000;
    i |= ((ID)b[0]) << 24;
    return i;
}

void
sizeushort_internal_to_stored(size_t i, char *b)
{
    PRUint16 ui = (PRUint16)(i & 0xffff);
    b[0] = (char)(ui >> 8);
    b[1] = (char)ui;
}

size_t
sizeushort_stored_to_internal(char *b)
{
    size_t i;
    i = (PRUint16)b[1] & 0x000000ff;
    i |= (((PRUint16)b[0]) << 8) & 0x0000ff00;
    return i;
}
