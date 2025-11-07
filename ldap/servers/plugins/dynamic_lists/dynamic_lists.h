/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Dynamic Lists plug-in header files
 */
#include "slapi-plugin.h"
#include "slapi-private.h"

/*
 * Plug-in defines
 */
#define DYNAMIC_LISTS_OBJECTCLASS "groupOfUrls"
#define DYNAMIC_LISTS_URL_ATTR "memberUrl"
#define DYNAMIC_LISTS_LIST_ATTR "member"

#define DYNAMIC_LISTS_PLUGIN_SUBSYSTEM "dynamic-lists-plugin"
#define DYNAMIC_LISTS_FEATURE_DESC "Dynamic Lists"
#define DYNAMIC_LISTS_PLUGIN_DESC "Dynamic Lists plugin"
#define DYNAMIC_LISTS_INT_PREOP_DESC "Dynamic Lists internal preop plugin"
#define DYNAMIC_LISTS_PREOP_DESC "Dynamic Lists preop plugin"

/*
 * Config settings
 */
 #define DYNAMIC_LIST_CONFIG_OC "dynamicListObjectclass"
 #define DYNAMIC_LIST_CONFIG_URL_ATTR "dynamicListUrlAttr"
 #define DYNAMIC_LIST_CONFIG_LIST_ATTR "dynamicListAttr"

/* Dynamic config struct */
typedef struct dynamic_lists_config
{
    char *oc;      /* objectclass to identify entry that has a dynamic list */
    char *URLAttr; /* attribute that contains the URL of the dynamic list */
    char *attr;    /* attribute used to store the values of the dynamic list */
} DynamicListsConfig;

/*
 * function prototypes
 */
int dynamic_lists_load_config(Slapi_PBlock *pb, DynamicListsConfig *config);
void * dynamic_lists_get_plugin_id(void);
