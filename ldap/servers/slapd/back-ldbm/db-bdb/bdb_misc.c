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
#include "bdb_layer.h"

/* TODO: make this a 64-bit return value */
int
bdb_db_size(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    unsigned int size; /* TODO: make this a 64-bit return value */
    int rc;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    rc = dblayer_database_size(li, &size);     /* TODO: make this a 64-bit return value */
    slapi_pblock_set(pb, SLAPI_DBSIZE, &size); /* TODO: make this a 64-bit return value */

    return rc;
}
int
bdb_cleanup(struct ldbminfo *li)
{

    slapi_log_err(SLAPI_LOG_TRACE, "bdb_cleanup", "bdb backend specific cleanup\n");
    /* We assume that dblayer_close has been called already */
    dblayer_private *priv = li->li_dblayer_private;
    int rval = 0;

    if (NULL == priv) /* already terminated.  nothing to do */
        return rval;

    objset_delete(&(li->li_instance_set));

    slapi_ch_free_string(&BDB_CONFIG(li)->bdb_log_directory);
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
bdb_back_ok_to_dump(const char *dn, char **include, char **exclude)
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
bdb_back_fetch_incl_excl(Slapi_PBlock *pb, char ***include, char ***exclude)
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

PRUint64
bdb_get_id2entry_size(ldbm_instance *inst)
{
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
}

int
bdb_start_autotune(struct ldbminfo *li)
{
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    /* size_t is a platform unsigned int, IE uint64_t */
    uint64_t total_cache_size = 0;
    uint64_t entry_size = 0;
    uint64_t dn_size = 0;
    uint64_t zone_size = 0;
    uint64_t import_size = 0;
    uint64_t db_size = 0;
    /* For clamping the autotune value to a 64Mb boundary */
    uint64_t clamp_div = 0;
    /* Backend count */
    uint64_t backend_count = 0;

    int_fast32_t autosize_percentage = 0;
    int_fast32_t autosize_db_percentage_split = 0;
    int_fast32_t import_percentage = 0;
    util_cachesize_result issane;
    char *msg = "";       /* This will be set by one of the two cache sizing paths below. */
    char size_to_str[32]; /* big enough to hold %ld */


    /* == Begin autotune == */

    /*
    * The process that we take here now defaults to autotune first, then override
    * with manual values if so chosen.
    *
    * This means first off, we need to check for valid autosizing values.
    * We then calculate what our system tuning would be. We clamp these to the
    * nearest value. IE 487MB would be 510656512 bytes, so we clamp this to
    * 536870912 bytes, aka 512MB. This is aligned to 64MB boundaries.
    *
    * Now that we have these values, we then check the values of dbcachesize
    * and cachememsize. If they are 0, we set them to the auto-calculated value.
    * If they are non-0, we skip the value.
    *
    * This way, we are really autotuning on "first run", and if the admin wants
    * to up the values, they merely need to reset the value to 0, and let the
    * server restart.
    *
    * wibrown 2017
    */

    /* sanity check the autosizing values,
     no value or sum of values larger than 100.
    */
    backend_count = objset_size(li->li_instance_set);

    /* If autosize == 0, set autosize_per to 10. */
    if (li->li_cache_autosize <= 0) {
        /* First, set our message. In the case autosize is 0, we calculate some
         * sane defaults and populate these values, but it's only on first run.
         */
        msg = "This can be corrected by altering the values of nsslapd-dbcachesize, nsslapd-cachememsize and nsslapd-dncachememsize\n";
        autosize_percentage = 25;
    } else {
        /* In this case we really are setting the values each start up, so
         * change the msg.
         */
        msg = "This can be corrected by altering the values of nsslapd-cache-autosize, nsslapd-cache-autosize-split and nsslapd-dncachememsize\n";
        autosize_percentage = li->li_cache_autosize;
    }
    /* Has to be less than 0, 0 means to disable I think */
    if (li->li_import_cache_autosize < 0) {
        import_percentage = 50;
    } else {
        import_percentage = li->li_import_cache_autosize;
    }
    /* This doesn't control the availability of the feature, so we can take the
     * default from ldbm_config.c
     */
    if (li->li_cache_autosize_split == 0) {
        autosize_db_percentage_split = 25;
    } else {
        autosize_db_percentage_split = li->li_cache_autosize_split;
    }


    /* Check the values are sane. */
    if ((autosize_percentage > 100) || (import_percentage > 100) || (autosize_db_percentage_split > 100) ||
        ((autosize_percentage > 0) && (import_percentage > 0) && (autosize_percentage + import_percentage > 100))) {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start_autotune", "Cache autosizing: bad settings, value or sum of values can not larger than 100.\n");
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start_autotune", "You should change nsslapd-cache-autosize + nsslapd-import-cache-autosize in dse.ldif to be less than 100.\n");
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start_autotune", "Reasonable starting values are nsslapd-cache-autosize: 10, nsslapd-import-cache-autosize: -1.\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* Get our platform memory values. */
    slapi_pal_meminfo *mi = spal_meminfo_get();
    if (mi == NULL) {
        slapi_log_err(SLAPI_LOG_CRIT, "bdb_start_autotune", "Unable to determine system page limits\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* calculate the needed values */
    zone_size = (autosize_percentage * mi->system_total_bytes) / 100;
    /* This is how much we "might" use, lets check it's sane. */
    /* In the case it is not, this will *reduce* the allocation */
    issane = util_is_cachesize_sane(mi, &zone_size);
    if (issane == UTIL_CACHESIZE_REDUCED) {
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "Your autosized cache values have been reduced. Likely your nsslapd-cache-autosize percentage is too high.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "%s", msg);
    }
    /* It's valid, lets divide it up and set according to user prefs */
    db_size = (autosize_db_percentage_split * zone_size) / 100;

    /* Cap the DB size at 1.5G, as this doesn't help perf much more (lkrispen's advice) */
    /* NOTE: Do we need a minimum DB size? */
    if (db_size > (1536 * MEGABYTE)) {
        db_size = (1536 * MEGABYTE);
    }


    /* NOTE: Because of how we workout entry_size, even if
     * have autosize split to say ... 90% for dbcache, because
     * we cap db_size, we use zone_size - db_size, meaning that entry
     * cache still gets the remaining memory *even* though we didn't use it all.
     * If we didn't do this, entry_cache would only get 10% of of the avail, even
     * if db_size was caped at say 5% down from 90.
     */
    if (backend_count > 0) {
        /* Number of entry cache pages per backend. */
        entry_size = (zone_size - db_size) / backend_count;
        /* Now split this into dn and entry */
        dn_size = entry_size * 0.1;
        entry_size = entry_size * 0.9;
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
        if (entry_size % (64 * MEGABYTE) != 0) {
            /* If we want to clamp down, remove the "+1". This would change the above from 510mb -> 448mb. */
            clamp_div = (entry_size / (64 * MEGABYTE)) + 1;
            entry_size = clamp_div * (64 * MEGABYTE);
        }
        if (dn_size % (64 * MEGABYTE) != 0) {
            /* If we want to clamp down, remove the "+1". This would change the above from 510mb -> 448mb. */
            clamp_div = (dn_size / (64 * MEGABYTE)) + 1;
            dn_size = clamp_div * (64 * MEGABYTE);
        }
    }

    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "found %" PRIu64 "k physical memory\n", mi->system_total_bytes / 1024);
    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "found %" PRIu64 "k available\n", mi->system_available_bytes / 1024);

    /* We've now calculated the autotuning values. Do we need to apply it?
     * we use the logic of "if size is 0, or autosize is > 0. This way three
     * options can happen.
     *
     * First, during first run, dbcache is 0, and autosize is 0. So we apply
     * the autotuned value ONLY on first run.
     * Second, once the admin sets a value, or autotuning set a value, it sticks.
     * Third, if the admin really does want autosizing to take effect every
     * start up, we disregard the defined value.
     */

    /* First, check the dbcache */
    if (li->li_dbcachesize == 0 || li->li_cache_autosize > 0) {
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "cache autosizing: db cache: %" PRIu64 "k\n", db_size / 1024);
        if (db_size < (500 * MEGABYTE)) {
            db_size = db_size / 1.25;
        }
        /* Have to set this value through text. */
        sprintf(size_to_str, "%" PRIu64, db_size);
        bdb_config_internal_set(li, CONFIG_DBCACHESIZE, size_to_str);
    }
    total_cache_size += li->li_dbcachesize;

    /* For each backend */
    /*   apply the appropriate cache size if 0 */
    if (backend_count > 0) {
        li->li_cache_autosize_ec = entry_size;
        li->li_dncache_autosize_ec = dn_size;
    }

    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
         inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {

        inst = (ldbm_instance *)object_get_data(inst_obj);
        uint64_t cache_size = (uint64_t)cache_get_max_size(&(inst->inst_cache));
        uint64_t dncache_size = (uint64_t)cache_get_max_size(&(inst->inst_dncache));

        /* This is the point where we decide to apply or not.
         * We have to check for the mincachesize as setting 0 resets
         * to this value. This could cause an issue with a *tiny* install, but
         * it's highly unlikely.
         */
        if (cache_size == 0 || cache_size == MINCACHESIZE || li->li_cache_autosize > 0) {
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "cache autosizing: %s entry cache (%" PRIu64 " total): %" PRIu64 "k\n", inst->inst_name, backend_count, entry_size / 1024);
            cache_set_max_entries(&(inst->inst_cache), -1);
            cache_set_max_size(&(inst->inst_cache), li->li_cache_autosize_ec, CACHE_TYPE_ENTRY);
        }
        if (dncache_size == 0 || dncache_size == MINCACHESIZE || li->li_cache_autosize > 0) {
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "cache autosizing: %s dn cache (%" PRIu64 " total): %" PRIu64 "k\n", inst->inst_name, backend_count, dn_size / 1024);
            cache_set_max_entries(&(inst->inst_dncache), -1);
            cache_set_max_size(&(inst->inst_dncache), li->li_dncache_autosize_ec, CACHE_TYPE_DN);
        }
        /* Refresh this value now. */
        cache_size = (PRUint64)cache_get_max_size(&(inst->inst_cache));
        db_size = bdb_get_id2entry_size(inst);
        if (cache_size < db_size) {
            slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune",
                          "%s: entry cache size %" PRIu64 " B is "
                          "less than db size %" PRIu64 " B; "
                          "We recommend to increase the entry cache size "
                          "nsslapd-cachememsize.\n",
                          inst->inst_name, cache_size, db_size);
        }
        total_cache_size += cache_size;
        total_cache_size += dncache_size;
    }
    /* autosizing importCache */
    if (li->li_import_cache_autosize > 0) {
        /* Use import percentage here, as it's been corrected for -1 behaviour */
        import_size = (import_percentage * mi->system_total_bytes) / 100;
        issane = util_is_cachesize_sane(mi, &import_size);
        if (issane == UTIL_CACHESIZE_REDUCED) {
            slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "Your autosized import cache values have been reduced. Likely your nsslapd-import-cache-autosize percentage is too high.\n");
        }
        /* We just accept the reduced allocation here. */
        slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "cache autosizing: import cache: %" PRIu64 "k\n", import_size / 1024);

        sprintf(size_to_str, "%" PRIu64, import_size);
        ldbm_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, size_to_str);
    }

    /* Finally, lets check that the total result is sane. */
    slapi_log_err(SLAPI_LOG_NOTICE, "bdb_start_autotune", "total cache size: %" PRIu64 " B; \n", total_cache_size);

    issane = util_is_cachesize_sane(mi, &total_cache_size);
    if (issane != UTIL_CACHESIZE_VALID) {
        /* Right, it's time to panic */
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "It is highly likely your memory configuration of all backends will EXCEED your systems memory.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "In a future release this WILL prevent server start up. You MUST alter your configuration.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "Total entry cache size: %" PRIu64 " B; dbcache size: %" PRIu64 " B; available memory size: %" PRIu64 " B; \n",
                      total_cache_size, (uint64_t)li->li_dbcachesize, mi->system_available_bytes);
        slapi_log_err(SLAPI_LOG_WARNING, "bdb_start_autotune", "%s", msg);
        /* WB 2016 - This should be UNCOMMENTED in a future release */
        /* return SLAPI_FAIL_GENERAL; */
    }

    spal_meminfo_destroy(mi);

    /* == End autotune == */
    return 0;
}
