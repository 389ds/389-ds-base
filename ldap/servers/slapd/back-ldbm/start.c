/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* 
 *  start.c
 */

#include "back-ldbm.h"



static int initialized = 0;

int
ldbm_back_isinitialized()
{
    return initialized;
}

static int
ldbm_back_start_autotune(struct ldbminfo *li) {
    Object *inst_obj = NULL;
    ldbm_instance *inst = NULL;
    /* size_t is a platform unsigned int, IE uint64_t */
    size_t total_cache_size = 0;
    size_t pagesize = 0;
    size_t pages = 0;
    size_t procpages __attribute__((unused)) = 0;
    size_t availpages = 0;
    size_t cache_size_to_configure = 0;
    size_t zone_pages = 0;
    size_t db_pages = 0;
    size_t entry_pages = 0;
    size_t import_pages = 0;
    size_t zone_size = 0;
    size_t import_size = 0;
    size_t cache_size = 0;
    size_t db_size = 0;
    /* For clamping the autotune value to a 64Mb boundary */
    size_t clamp_pages = 0;
    size_t clamp_div = 0;
    size_t clamp_mod = 0;
    /* Backend count */
    size_t backend_count = 0;

    int_fast32_t autosize_percentage = 0;
    int_fast32_t autosize_db_percentage_split = 0;
    int_fast32_t import_percentage = 0;
    int32_t issane = 0;
    char *msg = ""; /* This will be set by one of the two cache sizing paths below. */
    char size_to_str[32];    /* big enough to hold %ld */

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
        autosize_percentage = 10;
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
    autosize_db_percentage_split = li->li_cache_autosize_split;


    /* Check the values are sane. */
    if ((autosize_percentage > 100) || (import_percentage > 100) || (autosize_db_percentage_split > 100) ||
            ((autosize_percentage > 0) && (import_percentage > 0) && (autosize_percentage + import_percentage > 100))) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_start", "Cache autosizing: bad settings, value or sum of values can not larger than 100.\n");
        return SLAPI_FAIL_GENERAL;
    }

    if (util_info_sys_pages(&pagesize, &pages, &procpages, &availpages) != 0) {
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_start", "Unable to determine system page limits\n");
        return SLAPI_FAIL_GENERAL;
    }

    if (pagesize == 0) {
        /* If this happens, we are in a very bad state indeed... */
        slapi_log_err(SLAPI_LOG_CRIT, "ldbm_back_start", "Unable to determine system page size\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* calculate the needed values */
    zone_pages = (autosize_percentage * pages) / 100;
    zone_size = zone_pages * pagesize;
    /* This is how much we "might" use, lets check it's sane. */
    /* In the case it is not, this will *reduce* the allocation */
    issane = util_is_cachesize_sane(&zone_size);
    if (!issane) {
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "Your autosized cache values have been reduced. Likely your nsslapd-cache-autosize percentage is too high.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "%s", msg);
    }
    /* It's valid, lets divide it up and set according to user prefs */
    zone_pages = zone_size / pagesize;
    db_pages = (autosize_db_percentage_split * zone_pages) / 100;

    /* Cap the DB size at 512MB, as this doesn't help perf much more (lkrispen's advice) */
    if ((db_pages * pagesize) > (512 * MEGABYTE)) {
        db_pages = (512 * MEGABYTE) / pagesize;
    }

    if (backend_count > 0 ) {
        /* Number of entry cache pages per backend. */
        entry_pages = (zone_pages - db_pages) / backend_count;
        /* Now, clamp this value to a 64mb boundary. */
        /* How many pages are in 64mb? */
        clamp_pages = (64 * MEGABYTE) / pagesize;
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
        clamp_div = entry_pages / clamp_pages;
        clamp_mod = entry_pages % clamp_pages;
        if (clamp_mod != 0) {
            /* If we want to clamp down, remove this line. This would change the above from 510mb -> 448mb. */
            clamp_div += 1;
            entry_pages = clamp_div * clamp_pages;
        }
    }

    slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "found %luk physical memory\n", pages*(pagesize/1024));
    slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "found %luk avaliable\n", zone_pages*(pagesize/1024));

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
        slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "cache autosizing: db cache: %luk\n", db_pages*(pagesize/1024));
        cache_size_to_configure = (unsigned long)(db_pages * pagesize);
        if (cache_size_to_configure < (500 * MEGABYTE)) {
            cache_size_to_configure = (unsigned long)((db_pages * pagesize) / 1.25);
        }
        /* Have to set this value through text. */
        sprintf(size_to_str, "%lu", cache_size_to_configure);
        ldbm_config_internal_set(li, CONFIG_DBCACHESIZE, size_to_str);
    }
    total_cache_size += li->li_dbcachesize;

    /* For each backend */
    /*   apply the appropriate cache size if 0 */
    if (backend_count > 0 ) {
        li->li_cache_autosize_ec = (unsigned long)entry_pages * pagesize;
    }

    for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
            inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {

        inst = (ldbm_instance *)object_get_data(inst_obj);
        cache_size = (PRUint64)cache_get_max_size(&(inst->inst_cache));

        /* This is the point where we decide to apply or not.
         * We have to check for the mincachesize as setting 0 resets
         * to this value. This could cause an issue with a *tiny* install, but
         * it's highly unlikely.
         */
        if (cache_size == 0 || cache_size == MINCACHESIZE || li->li_cache_autosize > 0) {
            slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "cache autosizing: %s entry cache (%lu total): %luk\n", inst->inst_name, backend_count, entry_pages*(pagesize/1024));
            cache_set_max_entries(&(inst->inst_cache), -1);
            cache_set_max_size(&(inst->inst_cache), li->li_cache_autosize_ec, CACHE_TYPE_ENTRY);
        }
        /* Refresh this value now. */
        cache_size = (PRUint64)cache_get_max_size(&(inst->inst_cache));
        db_size = dblayer_get_id2entry_size(inst);
        if (cache_size < db_size) {
            slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start",
                  "%s: entry cache size %lu B is "
                  "less than db size %lu B; "
                  "We recommend to increase the entry cache size "
                  "nsslapd-cachememsize.\n",
                  inst->inst_name, cache_size, db_size);
        }
        /* We need to get each instances dncache size to add to the total */
        /* Else we can't properly check the cache allocations below */
        /* Trac 48831 exists to allow this to be auto-sized too ... */
        total_cache_size += (PRUint64)cache_get_max_size(&(inst->inst_dncache));
        total_cache_size += cache_size;
    }
    /* autosizing importCache */
    if (li->li_import_cache_autosize > 0) {
        /* Use import percentage here, as it's been corrected for -1 behaviour */
        import_pages = (import_percentage * pages) / 100;
        import_size = import_pages * pagesize;
        issane = util_is_cachesize_sane(&import_size);
        if (!issane) {
            slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "Your autosized import cache values have been reduced. Likely your nsslapd-import-cache-autosize percentage is too high.\n");
        }
        /* We just accept the reduced allocation here. */
        import_pages = import_size / pagesize;
        slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "cache autosizing: import cache: %luk\n",
        import_pages*(pagesize/1024));

        sprintf(size_to_str, "%lu", (unsigned long)(import_pages * pagesize));
        ldbm_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, size_to_str);
    }

    /* Finally, lets check that the total result is sane. */
    slapi_log_err(SLAPI_LOG_NOTICE, "ldbm_back_start", "total cache size: %lu B; \n", total_cache_size);

    issane = util_is_cachesize_sane(&total_cache_size);
    if (!issane) {
        /* Right, it's time to panic */
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "It is highly likely your memory configuration of all backends will EXCEED your systems memory.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "In a future release this WILL prevent server start up. You MUST alter your configuration.\n");
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "Total entry cache size: %lu B; dbcache size: %lu B; available memory size: %lu B; \n",
                  (PRUint64)total_cache_size, (PRUint64)li->li_dbcachesize, availpages * pagesize
        );
        slapi_log_err(SLAPI_LOG_WARNING, "ldbm_back_start", "%s\n", msg);
        /* WB 2016 - This should be UNCOMMENTED in a future release */
        /* return SLAPI_FAIL_GENERAL; */
    }

    /* == End autotune == */
    return 0;
}

/*
 * Start the LDBM plugin, and all its instances.
 */
int
ldbm_back_start( Slapi_PBlock *pb )
{
  struct ldbminfo  *li;
  char *home_dir = NULL;
  int action = 0 ;
  int retval = 0;

  slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_start", "ldbm backend starting\n");

  slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );

  /* parse the config file here */
  if (0 != ldbm_config_load_dse_info(li)) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Loading database configuration failed\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* lookthroughlimit can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_LOOKTHROUGHLIMIT_AT, &li->li_reslimit_lookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for lookthroughlimit\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* allidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_ALLIDSLIMIT_AT, &li->li_reslimit_allids_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for allidslimit\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* pagedlookthroughlimit can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_PAGEDLOOKTHROUGHLIMIT_AT, &li->li_reslimit_pagedlookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for pagedlookthroughlimit\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* pagedallidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_PAGEDALLIDSLIMIT_AT, &li->li_reslimit_pagedallids_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for pagedallidslimit\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* lookthrough limit for the rangesearch */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_RANGELOOKTHROUGHLIMIT_AT, &li->li_reslimit_rangelookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for rangelookthroughlimit\n");
      return SLAPI_FAIL_GENERAL;
  }

  /* If the db directory hasn't been set yet, we need to set it to 
   * the default. */
  if (NULL == li->li_directory || '\0' == li->li_directory[0]) {
      /* "get default" is a special string that tells the config
       * routines to figure out the default db directory by 
       * reading cn=config. */
      ldbm_config_internal_set(li, CONFIG_DIRECTORY, "get default");
  }

  retval = ldbm_back_start_autotune(li);
  if (retval != 0) {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to set database tuning on backends\n");
      return SLAPI_FAIL_GENERAL;
  }

  retval = check_db_version(li, &action);
  if (0 != retval)
  {
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "db version is not supported\n");
      return SLAPI_FAIL_GENERAL;
  }

  if (action &
      (DBVERSION_UPGRADE_3_4|DBVERSION_UPGRADE_4_4|DBVERSION_UPGRADE_4_5))
  {
      retval = dblayer_start(li,DBLAYER_CLEAN_RECOVER_MODE);
  }
  else
  {
      retval = dblayer_start(li,DBLAYER_NORMAL_MODE);
  }
  if (0 != retval) {
      char *msg;
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to init database, err=%d %s\n",
         retval, (msg = dblayer_strerror( retval )) ? msg : "");
      if (LDBM_OS_ERR_IS_DISKFULL(retval)) return return_on_disk_full(li);
      else return SLAPI_FAIL_GENERAL;
  }

  /* Walk down the instance list, starting all the instances. */
  retval = ldbm_instance_startall(li);
  if (0 != retval) {
      char *msg;
      slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to start databases, err=%d %s\n",
         retval, (msg = dblayer_strerror( retval )) ? msg : "");
      if (LDBM_OS_ERR_IS_DISKFULL(retval)) return return_on_disk_full(li);
      else {
        if ((li->li_cache_autosize > 0) && (li->li_cache_autosize <= 100)) {
          slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to allocate %lu byte dbcache.  "
                   "Please reduce the value of %s and restart the server.\n",
                   li->li_dbcachesize, CONFIG_CACHE_AUTOSIZE);
        }
        return SLAPI_FAIL_GENERAL;
      }
  }

  /* write DBVERSION file if one does not exist */
  home_dir = dblayer_get_home_dir(li, NULL);
  if (!dbversion_exists(li, home_dir))
  {
      dbversion_write (li, home_dir, NULL, DBVERSION_ALL);
  }


  /* this function is called every time new db is initialized   */
  /* currently it is called the 2nd time  when changelog db is  */
  /* dynamically created. Code below should only be called once */
  if (!initialized)
  {
    ldbm_compute_init();

    initialized = 1;
  }

  /* initialize the USN counter */
  ldbm_usn_init(li);

  slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_start", "ldbm backend done starting\n");

  return( 0 );

}
