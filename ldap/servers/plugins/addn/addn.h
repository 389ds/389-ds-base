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

/*
 * AD DN bind plugin header
 */

#include "slapi-plugin.h"
/* Because we are an internal plugin, we need this for the macro ... slapi_log_err */
#include "slapi-private.h"
#include <plstr.h>

struct addn_config
{
    char *default_domain;
    size_t default_domain_len;
};
