/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 *
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception.
 *
 *
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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

/*
 * Plug-in defines
 */
#define MEP_PLUGIN_SUBSYSTEM  "managed-entries-plugin"
#define MEP_FEATURE_DESC      "Managed Entries"
#define MEP_PLUGIN_DESC       "Managed Entries plugin"
#define MEP_INT_POSTOP_DESC   "Managed Entries internal postop plugin"
#define MEP_POSTOP_DESC       "Managed Entries postop plugin"

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
#define IS_ATTRDESC_CHAR(c) ( isalnum(c) || (c == '.') || (c == ';') || (c == '-') )

/*
 * Linked list of config entries.
 */
struct configEntry {
    PRCList list;
    Slapi_DN *sdn;
    char *origin_scope;
    Slapi_Filter *origin_filter;
    char *managed_base;
    Slapi_DN *template_sdn;
    Slapi_Entry *template_entry;
};

/*
 * Config fetch function
 */
PRCList *mep_get_config();

/*
 * Config cache locking functions
 */
void mep_config_read_lock();
void mep_config_write_lock();
void mep_config_unlock();

/*
 * Plugin identity functions
 */
void mep_set_plugin_id(void *pluginID);
void *mep_get_plugin_id();
void mep_set_plugin_dn(char *pluginDN);
char *mep_get_plugin_dn();

