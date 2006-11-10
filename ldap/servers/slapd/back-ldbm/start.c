/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* 
 *  start.c
 */

#include "back-ldbm.h"

/*
 * Start the LDBM plugin, and all its instances.
 */
int
ldbm_back_start( Slapi_PBlock *pb )
{
  struct ldbminfo  *li;
  static int initialized = 0;
  char *home_dir;
  int action;
  int retval; 

  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend starting\n", 0, 0, 0 );

  slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );

  /* parse the config file here */
  ldbm_config_load_dse_info(li);

  /* register with the binder-based resource limit subsystem so that    */
  /* lookthroughlimit can be supported on a per-connection basis.        */
  if ( slapi_reslimit_register( SLAPI_RESLIMIT_TYPE_INT,
            LDBM_LOOKTHROUGHLIMIT_AT, &li->li_reslimit_lookthrough_handle )
            != SLAPI_RESLIMIT_STATUS_SUCCESS ) {
      LDAPDebug( LDAP_DEBUG_ANY, "start: Resource limit registration failed\n",
            0, 0, 0 );
      return SLAPI_FAIL_GENERAL;
  }

  /* If the db directory hasn't been set yet, we need to set it to 
   * the default. */
  if ('\0' == li->li_directory[0]) {
      /* "get default" is a special string that tells the config
       * routines to figure out the default db directory by 
       * reading cn=config. */
      ldbm_config_internal_set(li, CONFIG_DIRECTORY, "get default");
  }

  /* sanity check the autosizing values,
     no value or sum of values larger than 100.
  */
  if (     (li->li_cache_autosize > 100) ||
    (li->li_cache_autosize_split > 100) ||
    (li->li_import_cache_autosize > 100) ||
    ((li->li_cache_autosize > 0) && (li->li_import_cache_autosize > 0) &&
    (li->li_cache_autosize + li->li_import_cache_autosize > 100)) )
  {
      LDAPDebug( LDAP_DEBUG_ANY, "cache autosizing: bad settings, "
        "value or sum of values can not larger than 100.\n", 0, 0, 0 );
  } else
  /* if cache autosize was selected, select the cache sizes now */
  if ((li->li_cache_autosize > 0) || (li->li_import_cache_autosize > 0)) {
      size_t pagesize, pages, procpages, availpages;

      dblayer_sys_pages(&pagesize, &pages, &procpages, &availpages);
      if (pagesize) {
          char s[32];    /* big enough to hold %ld */
		  unsigned long cache_size_to_configure = 0;
          int zone_pages, db_pages, entry_pages, import_pages;
          Object *inst_obj;
          ldbm_instance *inst;   
          /* autosizing dbCache and entryCache */
          if (li->li_cache_autosize > 0) {
              zone_pages = (li->li_cache_autosize * pages) / 100;
              /* now split it according to user prefs */
              db_pages = (li->li_cache_autosize_split * zone_pages) / 100;
              /* fudge an extra instance into our calculations... */
              entry_pages = (zone_pages - db_pages) /
                      (objset_size(li->li_instance_set) + 1);
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing. found %dk physical memory\n",
                pages*(pagesize/1024), 0, 0);
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing: db cache: %dk, "
                "each entry cache (%d total): %dk\n",
                db_pages*(pagesize/1024), objset_size(li->li_instance_set),
                entry_pages*(pagesize/1024));
    
              /* libdb allocates 1.25x the amount we tell it to, but only for values < 500Meg */
			  if (cache_size_to_configure < (500 * MEGABYTE)) {
					cache_size_to_configure = (unsigned long)((db_pages * pagesize) / 1.25);
			  } else {
					cache_size_to_configure = (unsigned long)(db_pages * pagesize);
			  }
              sprintf(s, "%lu", cache_size_to_configure);
              ldbm_config_internal_set(li, CONFIG_DBCACHESIZE, s);
              li->li_cache_autosize_ec = (unsigned long)entry_pages * pagesize;
    
              for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
                       inst_obj = objset_next_obj(li->li_instance_set, inst_obj)) {
                      inst = (ldbm_instance *)object_get_data(inst_obj);
                      cache_set_max_entries(&(inst->inst_cache), -1);
                      cache_set_max_size(&(inst->inst_cache), li->li_cache_autosize_ec);
              }
          }    
          /* autosizing importCache */
          if (li->li_import_cache_autosize > 0) {
			  /* For some reason, -1 means 50 ... */
			  if (li->li_import_cache_autosize == -1) {
					li->li_import_cache_autosize = 50;
			  }
              import_pages = (li->li_import_cache_autosize * pages) / 100;
              LDAPDebug(LDAP_DEBUG_ANY, "cache autosizing: import cache: %dk \n",
                import_pages*(pagesize/1024), NULL, NULL);
    
              sprintf(s, "%lu", (unsigned long)(import_pages * pagesize));
              ldbm_config_internal_set(li, CONFIG_IMPORT_CACHESIZE, s);
          }
      }
  }

  retval = check_db_version(li, &action);
  if (0 != retval)
  {
      LDAPDebug( LDAP_DEBUG_ANY, "start: db version is not supported\n",
                 0, 0, 0);
      return SLAPI_FAIL_GENERAL;
  }

  if (action & DBVERSION_UPGRADE_3_4)
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
		} else {
          LDAPDebug( LDAP_DEBUG_ANY, "Failed to allocate %d byte dbcache.  "
                   "Please reduce %s and Restart the server.\n",
                   li->li_dbcachesize, CONFIG_CACHE_AUTOSIZE, 0);
        }
        return SLAPI_FAIL_GENERAL;
	  }
  }

  /* write DBVERSION file if one does not exist */
  home_dir = dblayer_get_home_dir(li, NULL);
  if (!dbversion_exists(li, home_dir))
  {
      dbversion_write (li, home_dir, NULL);
  }


  /* this function is called every time new db is initialized   */
  /* currently it is called the 2nd time  when changelog db is  */
  /* dynamically created. Code below should only be called once */
  if (!initialized)
  {
    ldbm_compute_init();

    initialized = 1;
  }

  LDAPDebug( LDAP_DEBUG_TRACE, "ldbm backend done starting\n", 0, 0, 0 );

  return( 0 );

}
