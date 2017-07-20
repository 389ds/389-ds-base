/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2011 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 *  Root DN access control plug-in header file
 */
#include "slapi-plugin.h"
#include "slapi-private.h"
#include <nspr.h>
#include <time.h>
#include <ctype.h>

#define ROOTDN_PLUGIN_SUBSYSTEM "rootdn-access-control-plugin"
#define ROOTDN_FEATURE_DESC     "RootDN Access Control"
#define ROOTDN_PLUGIN_DESC      "RootDN Access Control plugin"
#define ROOTDN_PLUGIN_TYPE_DESC "RootDN Access Control plugin"
#define ROOTDN_PLUGIN_DN        "cn=RootDN Access Control,cn=plugins,cn=config"
