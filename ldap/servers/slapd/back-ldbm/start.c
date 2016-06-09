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

/*
 * Start the LDBM plugin, and all its instances.
 */
int
ldbm_back_start( Slapi_PBlock *pb )
{
  struct ldbminfo  *li;
  char *home_dir;
  int action;
  int retval; 
  int issane = 0;
  PRUint64 total_cache_size = 0;
  size_t pagesize = 0;
  size_t pages = 0;
  size_t procpages = 0;
  size_t availpages = 0;
  char *msg = ""; /* This will be set by one of the two cache sizing paths below. */

  char s[32];    /* big enough to hold %ld */
  unsigned long cache_size_to_configure = 0;
  int zone_pages;
  int db_pages;
  int entry_pages;
  int import_pages;
  size_t zone_size;
  size_t import_size;
  size_t total_size;
  Object *inst_obj;
  ldbm_instance *inst;   
  PRUint64 cache_size;
  PRUint64 dncache_size;
  PRUint64 db_size;
#ifndef LINUX
  PRUint64 memsize = pages * pagesize;
#endif

  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend starting\n", 0, 0, 0 );

  slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );

  /* parse the config file here */
  if (0 != ldbm_config_load_dse_info(li)) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Loading database configuration failed\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* lookthroughlimit can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_LOOKTHROUGHLIMIT_AT, &li->li_reslimit_lookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed for lookthroughlimit\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* allidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_ALLIDSLIMIT_AT, &li->li_reslimit_allids_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed for allidslimit\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* pagedlookthroughlimit can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_PAGEDLOOKTHROUGHLIMIT_AT, &li->li_reslimit_pagedlookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed for pagedlookthroughlimit\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* register with the binder-based resource limit subsystem so that    */
  /* pagedallidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_PAGEDALLIDSLIMIT_AT, &li->li_reslimit_pagedallids_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed for pagedallidslimit\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* lookthrough limit for the rangesearch */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_RANGELOOKTHROUGHLIMIT_AT, &li->li_reslimit_rangelookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed for rangelookthroughlimit\n",
            0, 0, 0 );
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

  /* sanity check the autosizing values,
     no value or sum of values larger than 100.
  */
  if ((li->li_cache_autosize > 100) ||
      (li->li_cache_autosize_split > 100) ||
      (li->li_import_cache_autosize > 100) ||
      ((li->li_cache_autosize > 0) && (li->li_import_cache_autosize > 0) &&
      (li->li_cache_autosize + li->li_import_cache_autosize > 100))) {
      LDAPDebug( LDAP_DEBUG_ANY, "cache autosizing: bad settings, "
        "value or sum of values can not larger than 100.\n", 0, 0, 0 );
  } else {
      if (util_info_sys_pages(&pagesize, &pages, &procpages, &availpages) != 0) {
          LDAPDebug( LDAP_DEBUG_ANY, "start: Unable to determine system page limits\n",
                0, 0, 0 );
          return SLAPI_FAIL_GENERAL;
      }
      if (pagesize) {
          if (li->li_cache_autosize == 0) {
              /* First, set our message. */
              msg = "This can be corrected by altering the values of nsslapd-dbcachesize, nsslapd-cachememsize and nsslapd-dncachememsize\n";

              for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
                   inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
                  inst = (ldbm_instance *)object_get_data(inst_obj);
                  cache_size = (PRUint64)cache_get_max_size(&(inst->inst_cache));
                  db_size = dblayer_get_id2entry_size(inst);
                  if (cache_size < db_size) {
                      LDAPDebug(LDAP_DEBUG_ANY,
                                "WARNING: %s: entry cache size %llu B is "
                                "less than db size %llu B; "
                                "We recommend to increase the entry cache size "
                                "nsslapd-cachememsize.\n",
                                inst->inst_name, cache_size, db_size);
                  } else {
                      LDAPDebug(LDAP_DEBUG_BACKLDBM,
                                "%s: entry cache size: %llu B; db size: %llu B\n",
                                inst->inst_name, cache_size, db_size);
                  }
                  /* Get the dn_cachesize */
                  dncache_size = (PRUint64)cache_get_max_size(&(inst->inst_dncache));
                  total_cache_size += cache_size + dncache_size;
                  LDAPDebug(LDAP_DEBUG_BACKLDBM,
                            "total cache size: %llu B; \n",
                            total_cache_size, 0 ,0 );
              }
              LDAPDebug(LDAP_DEBUG_BACKLDBM,
                        "Total entry cache size: %llu B; "
                        "dbcache size: %llu B; "
                        "available memory size: %llu B; \n",
#ifdef LINUX
                        (PRUint64)total_cache_size, (PRUint64)li->li_dbcachesize, availpages * pagesize
#else
                        (PRUint64)total_cache_size, (PRUint64)li->li_dbcachesize, memsize
#endif
                );

          /* autosizing dbCache and entryCache */
          } else if (li->li_cache_autosize > 0) {
              msg = "This can be corrected by altering the values of nsslapd-cache-autosize, nsslapd-cache-autosize-split and nsslapd-dncachememsize\n";
              zone_pages = (li->li_cache_autosize * pages) / 100;
              zone_size = zone_pages * pagesize;
              /* This is how much we "might" use, lets check it's sane. */
              /* In the case it is not, this will *reduce* the allocation */
              issane = util_is_cachesize_sane(&zone_size);
              if (!issane) {
                  LDAPDebug(LDAP_DEBUG_ANY, "Your autosized cache values have been reduced. Likely your nsslapd-cache-autosize percentage is too high.\n", 0,0,0);
                  LDAPDebug(LDAP_DEBUG_ANY, msg, 0,0,0);
              }
              /* It's valid, lets divide it up and set according to user prefs */
              zone_pages = zone_size / pagesize;
              db_pages = (li->li_cache_autosize_split * zone_pages) / 100;
              entry_pages = (zone_pages - db_pages) / objset_size(li->li_instance_set);
              /* We update this for the is-sane check below. */
              total_cache_size = (zone_pages - db_pages) * pagesize;

              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing. found %dk physical memory\n",
                pages*(pagesize/1024), 0, 0);
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing. found %dk avaliable\n",
                zone_pages*(pagesize/1024), 0, 0);
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing: db cache: %dk, "
                "each entry cache (%d total): %dk\n",
                db_pages*(pagesize/1024), objset_size(li->li_instance_set),
                entry_pages*(pagesize/1024));
    
              /* libdb allocates 1.25x the amount we tell it to,
               * but only for values < 500Meg 
               * For the larger memory, the overhead is relatively small. */
              cache_size_to_configure = (unsigned long)(db_pages * pagesize);
              if (cache_size_to_configure < (500 * MEGABYTE)) {
                  cache_size_to_configure = (unsigned long)((db_pages * pagesize) / 1.25);
              }
              sprintf(s, "%lu", cache_size_to_configure);
              ldbm_config_internal_set(li, CONFIG_DBCACHESIZE, s);
              li->li_cache_autosize_ec = (unsigned long)entry_pages * pagesize;
    
              for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
                  inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
                  inst = (ldbm_instance *)object_get_data(inst_obj);
                  cache_set_max_entries(&(inst->inst_cache), -1);
                  cache_set_max_size(&(inst->inst_cache),
                                    li->li_cache_autosize_ec, CACHE_TYPE_ENTRY);
                  /* We need to get each instances dncache size to add to the total */
                  /* Else we can't properly check the cache allocations below */
                  /* Trac 48831 exists to allow this to be auto-sized too ... */
                  total_cache_size += (PRUint64)cache_get_max_size(&(inst->inst_dncache));
              }
          }    
          /* autosizing importCache */
          if (li->li_import_cache_autosize > 0) {
              /* For some reason, -1 means 50 ... */
              if (li->li_import_cache_autosize == -1) {
                    li->li_import_cache_autosize = 50;
              }
              import_pages = (li->li_import_cache_autosize * pages) / 100;
              import_size = import_pages * pagesize;
              issane = util_is_cachesize_sane(&import_size);
              if (!issane) {
                  LDAPDebug(LDAP_DEBUG_ANY, "Your autosized import cache values have been reduced. Likely your nsslapd-import-cache-autosize percentage is too high.\n", 0,0,0);
              }
              /* We just accept the reduced allocation here. */
              import_pages = import_size / pagesize;
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing: import cache: %dk \n",
                import_pages*(pagesize/1024), NULL, NULL);
    
              sprintf(s, "%lu", (unsigned long)(import_pages * pagesize));
              ldbm_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, s);
          }
      }
  }

  /* Finally, lets check that the total result is sane. */

  total_size = total_cache_size + (PRUint64)li->li_dbcachesize;
  issane = util_is_cachesize_sane(&total_size);
  if (!issane) {
    /* Right, it's time to panic */
    LDAPDebug( LDAP_DEBUG_ANY, "CRITICAL: It is highly likely your memory configuration of all backends will EXCEED your systems memory.\n", 0, 0, 0 );
    LDAPDebug( LDAP_DEBUG_ANY, "CRITICAL: In a future release this WILL prevent server start up. You MUST alter your configuration.\n", 0, 0, 0 );
    LDAPDebug(LDAP_DEBUG_ANY,
              "Total entry cache size: %llu B; "
              "dbcache size: %llu B; "
              "available memory size: %llu B; \n",
#ifdef LINUX
              (PRUint64)total_cache_size, (PRUint64)li->li_dbcachesize, availpages * pagesize
#else
              (PRUint64)total_cache_size, (PRUint64)li->li_dbcachesize, memsize
#endif
    );
    LDAPDebug(LDAP_DEBUG_ANY, msg, 0,0,0);
    /* WB 2016 - This should be UNCOMMENTED in a future release */
    /* return SLAPI_FAIL_GENERAL; */
  }



  retval = check_db_version(li, &action);
  if (0 != retval)
  {
      LDAPDebug( LDAP_DEBUG_ANY, "start: db version is not supported\n",
                 0, 0, 0);
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
      LDAPDebug( LDAP_DEBUG_ANY, "start: Failed to init database, err=%d %s\n",
         retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
      if (LDBM_OS_ERR_IS_DISKFULL(retval)) return return_on_disk_full(li);
      else return SLAPI_FAIL_GENERAL;
  }

  /* Walk down the instance list, starting all the instances. */
  retval = ldbm_instance_startall(li);
  if (0 != retval) {
      char *msg;
      LDAPDebug( LDAP_DEBUG_ANY, "start: Failed to start databases, err=%d %s\n",
         retval, (msg = dblayer_strerror( retval )) ? msg : "", 0 );
      if (LDBM_OS_ERR_IS_DISKFULL(retval)) return return_on_disk_full(li);
      else {
        if ((li->li_cache_autosize > 0) && (li->li_cache_autosize <= 100)) {
          LDAPDebug( LDAP_DEBUG_ANY, "Failed to allocate %d byte dbcache.  "
                   "Please reduce the value of %s and restart the server.\n",
                   li->li_dbcachesize, CONFIG_CACHE_AUTOSIZE, 0);
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

  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done starting\n", 0, 0, 0 );

  return( 0 );

}
