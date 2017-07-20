/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Dereference plug-in header file
 */
#include "slapi-plugin.h"
#include "slapi-private.h"

/*
 * Plug-in defines
 */
#define DEREF_PLUGIN_SUBSYSTEM "deref-plugin"
#define DEREF_FEATURE_DESC "Dereference"
#define DEREF_PLUGIN_DESC "Dereference plugin"
#define DEREF_INT_PREOP_DESC "Dereference internal preop plugin"
#define DEREF_PREOP_DESC "Dereference preop plugin"

#ifndef LDAP_CONTROL_X_DEREF
#define LDAP_CONTROL_X_DEREF "1.3.6.1.4.1.4203.666.5.16"
#endif
