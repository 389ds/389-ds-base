/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
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

    if (dbmdb_componentid != NULL) {
        release_componentid(dbmdb_componentid);
        dbmdb_componentid = NULL;
    }

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
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    dbmdb_ctx_t *conf = MDB_CONFIG(li);
    int return_value = -1;
    uint64_t total_cache_size = 0;
    uint64_t zone_size = 0;
    uint64_t minspace_per_be = 64 * MEGABYTE;
    uint64_t backend_count = 0;
    uint32_t total_pages = 0;
    int_fast32_t autosize_percentage = 0;
    util_cachesize_result issane;
    char *msg = ""; /* This will be set by one of the two cache sizing paths below */
    bool private_env = false;
    char size_buffer[10] = {0};

    backend_count = objset_size(li->li_instance_set);

    /* If autosize == 0, set autosize_per to 25 */
    if (li->li_cache_autosize <= 0) {
        /* First, set our message. In the case autosize is 0, we calculate some
         * sane defaults and populate these values, but it's only on first run. */
        msg = "This can be corrected by altering the values of nsslapd-cachememsize and nsslapd-dncachememsize";
        autosize_percentage = 25;
    } else {
        /* In this case we really are setting the values each start up, so
         * change the msg. */
        msg = "This can be corrected by altering the values of nsslapd-cache-autosize";
        autosize_percentage = li->li_cache_autosize;
    }

    /* Check the values are sane. */
    if (autosize_percentage > 100) {
        slapi_log_err(SLAPI_LOG_CRIT, "mdb_start_autotune",
                "Cache autosizing: bad settings, value or sum of values can not larger than 100.\n");
        slapi_log_err(SLAPI_LOG_CRIT, "mdb_start_autotune",
                "You should change nsslapd-cache-autosize in dse.ldif to be less than 100.\n");
        slapi_log_err(SLAPI_LOG_CRIT, "mdb_start_autotune",
                "Reasonable starting values are nsslapd-cache-autosize: 25\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* Get our platform memory values. */
    slapi_pal_meminfo *mi = spal_meminfo_get();
    if (mi == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "mdb_start_autotune",
                      "Unable to determine system page limits\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* Walk the instances and get the total page count, but at startup we need
     * to make a temporary/private db env first */
    if (conf->env == NULL) {
        dblayer_init_pvt_txn();
        return_value = dbmdb_make_env(conf, 0, 0644);
        if (return_value != 0) {
            return 0;
        }
        li->li_max_key_len = mdb_env_get_maxkeysize(MDB_CONFIG(li)->env) - sizeof (ID);
        private_env = true;
    }

    /* Gather the total number of database pages */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        total_pages += dbmdb_get_inst_page_count(li, inst);
    }

    if (private_env) {
        /* close our private env */
        dbmdb_ctx_close(conf);
    }

    /* calculate the needed values */
    zone_size = (autosize_percentage * (mi->system_total_bytes - dbmdb_database_size(li))) / 100;
    /* This is how much we "might" use, lets check it's sane.
     * In the case it is not, this will *reduce* the allocation */
    issane = util_is_cachesize_sane(mi, &zone_size);
    if (issane == UTIL_CACHESIZE_REDUCED) {
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune",
                "Your autosized cache values have been reduced. "
                "Likely your nsslapd-cache-autosize percentage is too high.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune", "%s\n", msg);
    }

    slapi_log_err(SLAPI_LOG_NOTICE, "mdb_start_autotune",
                  "found %s physical memory\n",
                  convert_bytes_to_str((double)(mi->system_total_bytes), size_buffer, 0));
    slapi_log_err(SLAPI_LOG_NOTICE, "mdb_start_autotune",
                  "found %s available\n",
                  convert_bytes_to_str((double)(mi->system_available_bytes), size_buffer, 0));

    /* We've now calculated the autotuning values. Do we need to apply it?
     * we use the logic of "if size is 0, or autosize is > 0. This way three
     * options can happen.
     *
     * First, during first run autosize is 0. So we apply the autotuned value
     * ONLY on first run.
     * Second, once the admin sets a value, or autotuning set a value, it sticks.
     * Third, if the admin really does want autosizing to take effect every
     * start up, we disregard the defined value.
     */
    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
    {
        inst = (ldbm_instance *)object_get_data(inst_obj);
        uint64_t cache_size = (uint64_t)cache_get_max_size(&(inst->inst_cache));
        uint64_t dncache_size = (uint64_t)cache_get_max_size(&(inst->inst_dncache));
        uint64_t ec_size = MINCACHESIZE;
        uint64_t dn_size = DEFAULT_DNCACHE_SIZE;
        uint64_t clamp_div = 0; /* For clamping the autotune value to a 64Mb boundary */

        /* Calculate this instance's slice of memory it can use */
        if (total_pages) {
            double weight = (double)inst->inst_page_count / (double)total_pages;
            uint64_t reserved_space = (backend_count-1) * minspace_per_be;

            if (reserved_space >= zone_size) {
                /* There is not enough memory to autosize, use minimum */
                ec_size = MINCACHESIZE;
            } else {
                ec_size = (zone_size-reserved_space) * weight;
            }

            slapi_log_err(SLAPI_LOG_CACHE, "mdb_start_autotune",
                    "backend=%s autosize=%d zone_size=%lu page_count=%u "
                    "total_pages=%u weight=%lf ec_size=%ld\n",
                    inst->inst_name,
                    li->li_cache_autosize,
                    zone_size,
                    inst->inst_page_count,
                    total_pages,
                    weight,
                    ec_size);

            if (ec_size < MINCACHESIZE) {
                ec_size = MINCACHESIZE;
            }
        }

        /* Now split this into dn and entry caches*/
        dn_size = ec_size * 0.1;
        ec_size = ec_size * 0.9;

        /* Now, clamp this value to a 64mb boundary. */
        /* Now divide the entry pages by this, and also mod. If mod != 0, we need
         * to add 1 to the diveded number. This should give us:
         * 510 * 1024 * 1024 == 510MB
         * 534773760 bytes
         * 130560 pages at 4096 pages.
         * 16384 pages for 64Mb
         * 130560 / 16384 = 7
         * 130560 % 16384 = 15872 which is != 0
         * therfore 7 + 1, aka 8 * 16384 = 131072 pages = 536870912 bytes = 512MB.
         */
        if (ec_size % (64 * MEGABYTE) != 0) {
            /* If we want to clamp down, remove the "+1". This would change the above from 510mb -> 448mb. */
            clamp_div = (ec_size / (64 * MEGABYTE)) + 1;
            ec_size = clamp_div * (64 * MEGABYTE);
        }
        if (dn_size % (64 * MEGABYTE) != 0) {
            /* If we want to clamp down, remove the "+1". This would change the above from 510mb -> 448mb. */
            clamp_div = (dn_size / (64 * MEGABYTE)) + 1;
            dn_size = clamp_div * (64 * MEGABYTE);
        }

        /* This is the point where we decide to apply or not. If the cache
         * size is equal or less than MINCACHESIZE then we assume it does not
         * have a custom value and we can autotune
         */
        if (cache_size <= MINCACHESIZE) {
            slapi_log_err(SLAPI_LOG_NOTICE, "mdb_start_autotune",
                          "cache autosizing: %s entry cache (%" PRIu64 " total): %s\n",
                          inst->inst_name, backend_count,
                          convert_bytes_to_str((double)ec_size, size_buffer, 0));
            cache_set_max_entries(&(inst->inst_cache), -1, true /* autotuned */);
            cache_set_max_size(&(inst->inst_cache), ec_size, CACHE_TYPE_ENTRY, true);
        }
        if (dncache_size <= DEFAULT_DNCACHE_SIZE) {
            slapi_log_err(SLAPI_LOG_NOTICE, "mdb_start_autotune",
                          "cache autosizing: %s dn cache (%" PRIu64 " total): %s\n",
                          inst->inst_name, backend_count,
                          convert_bytes_to_str((double)dn_size, size_buffer, 0));
            cache_set_max_entries(&(inst->inst_dncache), -1, true);
            cache_set_max_size(&(inst->inst_dncache), dn_size, CACHE_TYPE_DN, true /* autotuned */);
        }
        /* Refresh this value now. */
        cache_size = (uint64_t)cache_get_max_size(&(inst->inst_cache));
        dncache_size = (uint64_t)cache_get_max_size(&(inst->inst_dncache));
        total_cache_size += cache_size;
        total_cache_size += dncache_size;
    }

    /* Finally, lets check that the total result is sane. */
    slapi_log_err(SLAPI_LOG_NOTICE, "mdb_start_autotune",
                  "total cache size: %s\n",
                  convert_bytes_to_str((double)total_cache_size, size_buffer, 0));

    issane = util_is_cachesize_sane(mi, &total_cache_size);
    if (issane != UTIL_CACHESIZE_VALID) {
        /* Right, it's time to panic */
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune",
                "It is highly likely your memory configuration of all backends will EXCEED your systems memory.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune",
                "In a future release this WILL prevent server start up. You MUST alter your configuration.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune",
                "Total entry cache size: %s; available memory size: %s; \n",
                convert_bytes_to_str((double)total_cache_size, size_buffer, 0),
                convert_bytes_to_str((double)(mi->system_available_bytes), size_buffer, 0));
        slapi_log_err(SLAPI_LOG_WARNING, "mdb_start_autotune", "%s\n", msg);
        /* WB 2016 - This should be UNCOMMENTED in a future release */
        /* return SLAPI_FAIL_GENERAL; */
    }

    spal_meminfo_destroy(mi);

    return 0;
}
