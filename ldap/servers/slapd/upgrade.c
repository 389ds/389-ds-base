/* BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright (C) 2020 William Brown <william@blackhats.net.au>
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK */

#include <slap.h>
#include <slapi-private.h>

/*
 * This is called on server startup *before* plugins start
 * but after config dse is read for operations. This allows
 * us to make internal assertions about the state of the configuration
 * at start up, enable plugins, and more.
 *
 * The functions in this file are named as:
 * upgrade_xxx_yyy, where xxx is the minimum version of the project
 * and yyy is the feature that is having it's configuration upgrade
 * or altered.
 */

upgrade_status
upgrade_server(void)
{
    return UPGRADE_SUCCESS;
}
