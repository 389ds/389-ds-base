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

/* uniqueid2entry.c - given a dn return an entry */

#include "back-ldbm.h"

/*
 * uniqueid2entry - look up uniqueid in the cache/indexes and return the
 * corresponding entry.
 */

struct backentry *
uniqueid2entry(
    backend *be,
    const char *uniqueid,
    back_txn *txn,
    int *err)
{
#ifdef UUIDCACHE_ON
    ldbm_instance *inst = (ldbm_instance *)be->be_instance_info;
#endif
    struct berval idv;
    IDList *idl = NULL;
    struct backentry *e = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "uniqueid2entry", "=> \"%s\"\n", uniqueid);
#ifdef UUIDCACHE_ON
    e = cache_find_uuid(&inst->inst_cache, uniqueid);
#endif
    if (e == NULL) {
        /* convert dn to entry id */
        PR_ASSERT(uniqueid);
        *err = 0;
        idv.bv_val = (void *)uniqueid;
        idv.bv_len = strlen(idv.bv_val);

        if ((idl = index_read(be, SLAPI_ATTR_UNIQUEID, indextype_EQUALITY, &idv, txn,
                              err)) == NULL) {
            if (*err != 0 && *err != DB_NOTFOUND) {
                goto ext;
            }
        } else {
            /* convert entry id to entry */
            if ((e = id2entry(be, idl_firstid(idl), txn, err)) != NULL) {
                goto ext;
            } else {
                if (*err != 0 && *err != DB_NOTFOUND) {
                    goto ext;
                }
                /*
                 * this is pretty bad anyway. the dn was in the
                 * SLAPI_ATTR_UNIQUEID index, but we could not
                 * read the entry from the id2entry index.
                 * what should we do?
                 */
            }
        }
    } else {
        goto ext;
    }

ext:
    if (NULL != idl) {
        slapi_ch_free((void **)&idl);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "uniqueid2entry", "<= %p\n", e);
    return (e);
}
