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
 *  start.c
 */

#include "back-ldbm.h"
#include "dblayer.h"


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
ldbm_back_start(Slapi_PBlock *pb)
{
    struct ldbminfo *li;
    int retval = 0;
    dblayer_private *priv = NULL;
    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_start", "ldbm backend starting\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);

    /* initialize dblayer  */
    if( dblayer_setup(li)) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to setup dblayer\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* register with the binder-based resource limit subsystem so that    */
    /* lookthroughlimit can be supported on a per-connection basis.        */
    if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                LDBM_LOOKTHROUGHLIMIT_AT, &li->li_reslimit_lookthrough_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for lookthroughlimit\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* register with the binder-based resource limit subsystem so that    */
    /* allidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
    if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                LDBM_ALLIDSLIMIT_AT, &li->li_reslimit_allids_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for allidslimit\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* register with the binder-based resource limit subsystem so that    */
    /* pagedlookthroughlimit can be supported on a per-connection basis.        */
    if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                LDBM_PAGEDLOOKTHROUGHLIMIT_AT, &li->li_reslimit_pagedlookthrough_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for pagedlookthroughlimit\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* register with the binder-based resource limit subsystem so that    */
    /* pagedallidslimit (aka idlistscanlimit) can be supported on a per-connection basis.        */
    if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                LDBM_PAGEDALLIDSLIMIT_AT, &li->li_reslimit_pagedallids_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Resource limit registration failed for pagedallidslimit\n");
        return SLAPI_FAIL_GENERAL;
    }

    /* lookthrough limit for the rangesearch */
    if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT,
                                LDBM_RANGELOOKTHROUGHLIMIT_AT, &li->li_reslimit_rangelookthrough_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
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

    /* We are autotuning the caches. was:
     * retval = ldbm_back_start_autotune(li);
     * This involves caches specific to instances managed in the ldbm layer
     * and to caches specific to the db implementation.
     * The cache usage and requirements of the db is not known here, also it
     * might have impact on the sizing of the instance caches.
     * Therfor this functionality is moved to the db_xxx layer.
     * The latest autotune function was implemented only with BDB in mind
     * so it should be safe to move it to db_bdb.
     */
    priv = (dblayer_private *)li->li_dblayer_private;
    retval = priv->dblayer_auto_tune_fn(li);
    if (retval != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to set database tuning on backends\n");
        return SLAPI_FAIL_GENERAL;
    }

    retval = dblayer_start(li, DBLAYER_NORMAL_MODE);

    if (0 != retval) {
        const char *msg;
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to init database, err=%d %s\n",
                      retval, (msg = dblayer_strerror(retval)) ? msg : "");
        if (LDBM_OS_ERR_IS_DISKFULL(retval))
            return return_on_disk_full(li);
        else
            return SLAPI_FAIL_GENERAL;
    }

    /* Walk down the instance list, starting all the instances. */
    retval = ldbm_instance_startall(li);
    if (0 != retval) {
        const char *msg;
        slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to start databases, err=%d %s\n",
                      retval, (msg = dblayer_strerror(retval)) ? msg : "");
        if (LDBM_OS_ERR_IS_DISKFULL(retval))
            return return_on_disk_full(li);
        else {
            if ((li->li_cache_autosize > 0) && (li->li_cache_autosize <= 100)) {
                /* NOTE (LK): there are two problems with the following error message:
                 * First it reports a dbcache size which might not be available for
                 * all backend implementations.
                 * Second, there are many error conditions in ldbm_instance_startall
                 * which can result in retval != 0, so it might be misleading.
                 * For now do not change it, use a generic function to get db config
                 * params, but need to think about it
                 *
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to allocate %" PRIu64 " byte dbcache.  "
                                                                "Please reduce the value of %s and restart the server.\n",
                              li->li_dbcachesize, CONFIG_CACHE_AUTOSIZE);
                 */
                char dbcachesize[BUFSIZ];
                priv->dblayer_config_get_fn(li, CONFIG_DBCACHESIZE, dbcachesize);
                slapi_log_err(SLAPI_LOG_ERR, "ldbm_back_start", "Failed to allocate %s byte dbcache.  "
                                                                "Please reduce the value of %s and restart the server.\n",
                              dbcachesize, CONFIG_CACHE_AUTOSIZE);
            }
            return SLAPI_FAIL_GENERAL;
        }
    }


    /* this function is called every time new db is initialized   */
    /* currently it is called the 2nd time  when changelog db is  */
    /* dynamically created. Code below should only be called once */
    if (!initialized) {
        ldbm_compute_init();

        initialized = 1;
    }

    /* initialize the USN counter */
    ldbm_usn_init(li);

    slapi_log_err(SLAPI_LOG_TRACE, "ldbm_back_start", "ldbm backend done starting\n");

    return (0);
}
