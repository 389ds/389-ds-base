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

/*
 *  File: close.c
 *
 *  Functions:
 *
 *      ldif_back_close() - ldap ldif back-end close routine
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_free_db
 *
 *  Returns: void
 *
 *  Description: frees up the ldif database
 */
void
ldif_free_db(LDIF *db)
{
    ldif_Entry *cur; /*Used for walking down the list*/

    /*If db is null, there is nothing to do*/
    if (db == NULL) {
        return;
    }

    /*Walk down the list, freeing up the ldif_entries*/
    for (cur = db->ldif_entries; cur != NULL; cur = cur->next) {
        ldifentry_free(cur);
    }

    /*Free the ldif_file string, and then the db itself*/
    free((void *)db->ldif_file);
    free((void *)db);
}


/*
 *  Function: ldif_back_close
 *
 *  Returns: void
 *
 *  Description: closes the ldif backend, frees up the database
 */
void
ldif_back_close(Slapi_PBlock *pb)
{
    LDIF *db;

    slapi_log_err(SLAPI_LOG_TRACE, "ldif backend syncing\n", 0, 0, 0);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &db);
    ldif_free_db(db);
    slapi_log_err(SLAPI_LOG_TRACE, "ldif backend done syncing\n", 0, 0, 0);
}
