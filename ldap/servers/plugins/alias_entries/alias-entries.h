/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2023 anilech
 * Copyright (C) 2023 Red Hat, Inc.
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
#include "slapi-plugin.h"
#include "slapi-private.h"

#define PLUGINNAME "Alias Entries"
#define PLUGINVNDR "anilech"
#define PLUGINVERS "0.1"
#define PLUGINDESC "alias entries plugin [base search only]"

#define MAXALIASCHAIN 8
#define ALIASFILTER "(&(objectClass=alias)(aliasedObjectName=*))"

// function prototypes
int alias_entry_init(Slapi_PBlock *pb);
int alias_entry_srch(Slapi_PBlock *pb);
