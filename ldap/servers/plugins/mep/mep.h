/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Managed entries plug-in header file
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-plugin.h"
#include "prclist.h"
#include "ldif.h"
#include "slapi-private.h"

/*
 * Plug-in defines
 */
#define MEP_PLUGIN_SUBSYSTEM "managed-entries-plugin"
#define MEP_FEATURE_DESC     "Managed Entries"
#define MEP_PLUGIN_DESC      "Managed Entries plugin"
#define MEP_INT_POSTOP_DESC  "Managed Entries internal postop plugin"
#define MEP_POSTOP_DESC      "Managed Entries postop plugin"

/*
 * Config type defines
 */
#define MEP_SCOPE_TYPE            "originScope"
#define MEP_FILTER_TYPE           "originFilter"
#define MEP_MANAGED_BASE_TYPE     "managedBase"
#define MEP_MANAGED_TEMPLATE_TYPE "managedTemplate"

/*
 * Link type defines
 */
#define MEP_MANAGED_ENTRY_TYPE "mepManagedEntry"
#define MEP_MANAGED_BY_TYPE    "mepManagedBy"

/*
 * Template type defines
 */
#define MEP_STATIC_ATTR_TYPE "mepStaticAttr"
#define MEP_MAPPED_ATTR_TYPE "mepMappedAttr"
#define MEP_RDN_ATTR_TYPE    "mepRDNAttr"

/*
 * Objectclass defines
 */
#define MEP_MANAGED_OC  "mepManagedEntry"
#define MEP_TEMPLATE_OC "mepTemplateEntry"
#define MEP_ORIGIN_OC   "mepOriginEntry"

/*
 * Helper defines
 */
#define IS_ATTRDESC_CHAR(c) (isalnum(c) || (c == '.') || (c == ';') || (c == '-'))

/*
 * Linked list of config entries.
 */
struct configEntry
{
    PRCList list;
    Slapi_DN *sdn;
    char *origin_scope;
    Slapi_Filter *origin_filter;
    char *managed_base;
    Slapi_DN *template_sdn;
    Slapi_Entry *template_entry;
    char **origin_attrs;
};

/*
 * Config fetch function
 */
PRCList *mep_get_config(void);

/*
 * Config cache locking functions
 */
void mep_config_read_lock(void);
void mep_config_write_lock(void);
void mep_config_unlock(void);

/*
 * Plugin identity functions
 */
void mep_set_plugin_id(void *pluginID);
void *mep_get_plugin_id(void);
void mep_set_plugin_dn(char *pluginDN);
char *mep_get_plugin_dn(void);
