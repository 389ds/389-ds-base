/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* features.c - routines for dealing with supportedFeatures rfc3674 */

#include <stdio.h>
#include "slap.h"
#include "slapi-plugin.h"

int slapi_register_supported_feature(char *featureoid);
static char **supported_features = NULL;
static Slapi_RWLock *supported_features_lock = NULL;

void
init_features(void)
{
    supported_features_lock = slapi_new_rwlock();
    if (supported_features_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "startup",
                      "init_features - failed to create lock.\n");
        exit(1);
    }
    slapi_register_supported_feature(LDAP_FEATURE_ALL_OP_ATTRS);
}

int
slapi_register_supported_feature(char *featureoid)
{
    slapi_rwlock_wrlock(supported_features_lock);
    charray_add(&supported_features, slapi_ch_strdup(featureoid));
    slapi_rwlock_unlock(supported_features_lock);
    return LDAP_SUCCESS;
}

int
slapi_get_supported_features_copy(char ***ftroidsp)
{
    slapi_rwlock_rdlock(supported_features_lock);
    if (ftroidsp != NULL) {
        *ftroidsp = charray_dup(supported_features);
    }
    slapi_rwlock_unlock(supported_features_lock);
    return LDAP_SUCCESS;
}
