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
#ifdef TODO
    struct ldbminfo *li;
    unsigned int size; /* TODO: make this a 64-bit return value */
    int rc;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    rc = dbmdb_database_size(li, &size);     /* TODO: make this a 64-bit return value */
    slapi_pblock_set(pb, SLAPI_DBSIZE, &size); /* TODO: make this a 64-bit return value */

    return rc;
#endif /* TODO */
}
int
dbmdb_cleanup(struct ldbminfo *li)
{
#ifdef TODO

    slapi_log_err(SLAPI_LOG_TRACE, "dbmdb_cleanup", "mdb backend specific cleanup\n");
    /* We assume that dblayer_close has been called already */
    dblayer_private *priv = li->li_dblayer_private;
    int rval = 0;

    if (NULL == priv) /* already terminated.  nothing to do */
        return rval;

    objset_delete(&(li->li_instance_set));

    slapi_ch_free_string(&BDB_CONFIG(li)->dbmdb_log_directory);
    slapi_ch_free((void **)&priv);
    li->li_dblayer_private = NULL;

    if (config_get_entryusn_global()) {
        slapi_counter_destroy(&li->li_global_usn_counter);
    }
    slapi_ch_free((void **)&(li->li_dblayer_config));

    return 0;
#endif /* TODO */
}

/* check if a DN is in the include list but NOT the exclude list
 * [used by both ldif2db and db2ldif]
 */
int
dbmdb_back_ok_to_dump(const char *dn, char **include, char **exclude)
{
#ifdef TODO
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
#endif /* TODO */
}

/* fetch include/exclude DNs from the pblock and normalize them --
 * returns true if there are any include/exclude DNs
 * [used by both ldif2db and db2ldif]
 */
int
dbmdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude)
{
#ifdef TODO
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
#endif /* TODO */
}

PRUint64
dbmdb_get_id2entry_size(ldbm_instance *inst)
{
#ifdef TODO
    struct ldbminfo *li = NULL;
    char *id2entry_file = NULL;
    PRFileInfo64 info;
    int rc;
    char inst_dir[MAXPATHLEN], *inst_dirp = NULL;

    if (NULL == inst) {
        return 0;
    }
    li = inst->inst_li;
    inst_dirp = dblayer_get_full_inst_dir(li, inst, inst_dir, MAXPATHLEN);
    id2entry_file = slapi_ch_smprintf("%s/%s", inst_dirp,
                                      ID2ENTRY LDBM_FILENAME_SUFFIX);
    if (inst_dirp != inst_dir) {
        slapi_ch_free_string(&inst_dirp);
    }
    rc = PR_GetFileInfo64(id2entry_file, &info);
    slapi_ch_free_string(&id2entry_file);
    if (rc) {
        return 0;
    }
    return info.size;
#endif /* TODO */
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
