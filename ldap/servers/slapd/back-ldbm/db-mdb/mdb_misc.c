/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2019 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "mdb_layer.h"

/* TODO: make this a 64-bit return value */
int
dbmdb_db_size(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    uint64_t size64;
    uint sizekb;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    size64 = dbmdb_database_size(li);
    size64 /= 1024;
    sizekb = (uint)size64;
    slapi_pblock_set(pb, SLAPI_DBSIZE, &sizekb);

    return 0;
}

int
dbmdb_cleanup(struct ldbminfo *li)
{
    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_cleanup", "mdb backend specific cleanup\n");
    /* We assume that dblayer_close has been called already */
    dblayer_private *priv = li->li_dblayer_private;
    int rval = 0;

    if (NULL == priv) /* already terminated.  nothing to do */
        return rval;

    objset_delete(&(li->li_instance_set));

    slapi_ch_free((void **)&priv);
    li->li_dblayer_private = NULL;

    if (config_get_entryusn_global()) {
        slapi_counter_destroy(&li->li_global_usn_counter);
    }
    slapi_ch_free((void **)&(li->li_dblayer_config));

    return 0;
}

/* check if a DN is in the include list but NOT the exclude list
 * [used by both ldif2db and db2ldif]
 */
int
dbmdb_back_ok_to_dump(const char *dn, char **include, char **exclude)
{
    int i = 0;

    if (!(include || exclude))
        return (1);

    if (exclude) {
        i = 0;
        while (exclude[i]) {
            if (slapi_dn_issuffix(dn, exclude[i]))
                return (0);
            i++;
        }
    }

    if (include) {
        i = 0;
        while (include[i]) {
            if (slapi_dn_issuffix(dn, include[i]))
                return (1);
            i++;
        }
        /* not in include... bye. */
        return (0);
    }

    return (1);
}

/* fetch include/exclude DNs from the pblock and normalize them --
 * returns true if there are any include/exclude DNs
 * [used by both ldif2db and db2ldif]
 */
int
dbmdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude)
{
    char **pb_incl, **pb_excl;

    slapi_pblock_get(pb, SLAPI_LDIF2DB_INCLUDE, &pb_incl);
    slapi_pblock_get(pb, SLAPI_LDIF2DB_EXCLUDE, &pb_excl);
    if ((NULL == include) || (NULL == exclude)) {
        return 0;
    }
    *include = *exclude = NULL;

    /* pb_incl/excl are both normalized */
    *exclude = slapi_ch_array_dup(pb_excl);
    *include = slapi_ch_array_dup(pb_incl);

    return (pb_incl || pb_excl);
}

int
dbmdb_count_config_entries(char *filter, int *nbentries)
{
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;
    int count = 0;
    int rval;

    *nbentries = 0;
    search_pb = slapi_pblock_new();
    if (!search_pb) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_count_config_entries", "Out of memory\n");
        return 1;
    }
    slapi_search_internal_set_pb(search_pb, "cn=config", LDAP_SCOPE_SUBTREE, filter, NULL, 0, NULL, NULL, dbmdb_componentid, 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);

    if (rval != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "dbmdb_count_config_entries", "Failed to search cn=config err=%d\n", rval);
    } else {
        if (entries != NULL) {
            while (entries[count]) {
                count++;
            }
        }
    }

    *nbentries = count;
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    return rval;
}


int
dbmdb_start_autotune(struct ldbminfo *li)
{
    return 0;
}
