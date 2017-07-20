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

#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "cos_cache.h"
#include "vattr_spi.h"
#include <sys/stat.h>


/*** secret slapd stuff ***/

/*
    these are required here because they are not available
    in any public header.  They must exactly match their
    counterparts in the server or they will fail to work
    correctly.
*/

/*** from proto-slap.h ***/

int slapd_log_error_proc(int sev_level, char *subsystem, char *fmt, ...);

/*** end secrets ***/

#define COS_PLUGIN_SUBSYSTEM "cos-plugin" /* used for logging */

/* subrelease in the following version info is for odd-ball cos releases
 * which do not fit into a general release, this can be used for beta releases
 * and other (this version stuff is really to help outside applications which
 * may wish to update cos decide whether the cos version they want to update to
 * is a higher release than the installed plugin)
 *
 * note: release origin is 00 for directory server
 *       sub-release should be:
 *         50 for initial RTM products
 *         from 0 increasing for alpha/beta releases
 *         from 51 increasing for patch releases
 */
#define COS_VERSION 0x00050050 /* version format: 0x release origin 00 major 05 minor 00 sub-release 00 */

/* other function prototypes */
int cos_init(Slapi_PBlock *pb);
int cos_compute(computed_attr_context *c, char *type, Slapi_Entry *e, slapi_compute_output_t outputfn);
int cos_start(Slapi_PBlock *pb);
int cos_close(Slapi_PBlock *pb);
int cos_post_op(Slapi_PBlock *pb);

static Slapi_PluginDesc pdesc = {"cos", VENDOR, DS_PACKAGE_VERSION,
                                 "class of service plugin"};

static void *cos_plugin_identity = NULL;


/*
** Plugin identity mgmt
*/

void
cos_set_plugin_identity(void *identity)
{
    cos_plugin_identity = identity;
}

void *
cos_get_plugin_identity(void)
{
    return cos_plugin_identity;
}

int
cos_version(void)
{
    return COS_VERSION;
}

/*
 * cos_postop_init: registering cos_post_op
 * cos_post_op just calls cos_cache_change_notify, which does not have any
 * backend operations.  Thus, no need to be in transaction.  Rather, it is
 * harmful if putting in the transaction since tring to hold change_lock
 * inside of transaction would cause a deadlock.
 */
int
cos_postop_init(Slapi_PBlock *pb)
{
    int rc = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN, (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *)cos_post_op) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, COS_PLUGIN_SUBSYSTEM,
                      "cos_postop_init - Failed to register plugin\n");
        rc = -1;
    }
    return rc;
}

int
cos_internalpostop_init(Slapi_PBlock *pb)
{
    int rc = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN,
                         (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN,
                         (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN,
                         (void *)cos_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN,
                         (void *)cos_post_op) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, COS_PLUGIN_SUBSYSTEM,
                      "cos_internalpostop_init - Failed to register plugin\n");
        rc = -1;
    }
    return rc;
}

/*
    cos_init
    --------
    adds our callbacks to the list
*/
int
cos_init(Slapi_PBlock *pb)
{
    int ret = 0;
    void *plugin_identity = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "--> cos_init\n");

    /*
    ** Store the plugin identity for later use.
    ** Used for internal operations
    */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    cos_set_plugin_identity(plugin_identity);

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)cos_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)cos_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, COS_PLUGIN_SUBSYSTEM,
                      "cos_init - Failed to register plugin\n");
        ret = -1;
        goto bailout;
    }
    ret = slapi_register_plugin("postoperation", 1 /* Enabled */,
                                "cos_postop_init", cos_postop_init,
                                "Class of Service postoperation plugin", NULL,
                                plugin_identity);
    if (ret < 0) {
        goto bailout;
    }

    ret = slapi_register_plugin("internalpostoperation", 1 /* Enabled */,
                                "cos_internalpostop_init", cos_internalpostop_init,
                                "Class of Service internalpostoperation plugin", NULL,
                                plugin_identity);

bailout:
    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "<-- cos_init\n");
    return ret;
}

/*
    cos_start
    ---------
    This function registers the computed attribute evaluator
    and inits the cos cache.
    It is called after cos_init.
*/
int
cos_start(Slapi_PBlock *pb __attribute__((unused)))
{
    int ret = 0;

    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "--> cos_start\n");

    if (!cos_cache_init()) {
        slapi_log_err(SLAPI_LOG_PLUGIN, COS_PLUGIN_SUBSYSTEM, "cos_start - Ready for service\n");
    } else {

        /* problems we are hosed */
        cos_cache_stop();
        slapi_log_err(SLAPI_LOG_ERR, COS_PLUGIN_SUBSYSTEM, "cos_start - Failed to initialise\n");
        ret = -1;
    }

    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "<-- cos_start\n");
    return ret;
}

/*
    cos_close
    ---------
    closes down the cache
*/
int
cos_close(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "--> cos_close\n");

    cos_cache_stop();

    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "<-- cos_close\n");

    return 0;
}

/*
    cos_compute
    -----------
    called when evaluating named attributes in a search
    and attributes remain unfound in the entry,
    this function checks the attribute for a match with
    those in the class of service definitions, and if a
    match is found, adds the attribute and value to the
    output list

    returns
        0 on success
        1 on outright failure
        -1 when doesn't know about attribute
*/
int
cos_compute(computed_attr_context *c __attribute__((unused)),
            char *type __attribute__((unused)),
            Slapi_Entry *e __attribute__((unused)),
            slapi_compute_output_t outputfn __attribute__((unused)))
{
    int ret = -1;

    return ret;
}


/*
    cos_post_op
    -----------
    Catch all for all post operations that change entries
    in some way - this simply notifies the cache of a
    change - the cache decides if action is necessary
*/
int
cos_post_op(Slapi_PBlock *pb)
{
    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "--> cos_post_op\n");

    cos_cache_change_notify(pb);

    slapi_log_err(SLAPI_LOG_TRACE, COS_PLUGIN_SUBSYSTEM, "<-- cos_post_op\n");

    return SLAPI_PLUGIN_SUCCESS; /* always succeed */
}
