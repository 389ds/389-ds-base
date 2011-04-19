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
 * Copyright (C) 2011 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * Auto Membership plug-in header file
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
#define AUTOMEMBER_PLUGIN_SUBSYSTEM  "auto-membership-plugin"
#define AUTOMEMBER_FEATURE_DESC      "Auto Membership"
#define AUTOMEMBER_PLUGIN_DESC       "Auto Membership plugin"
#define AUTOMEMBER_INT_POSTOP_DESC   "Auto Membership internal postop plugin"
#define AUTOMEMBER_POSTOP_DESC       "Auto Membership postop plugin"

/*
 * Config type defines
 */
#define AUTOMEMBER_SCOPE_TYPE         "autoMemberScope"
#define AUTOMEMBER_FILTER_TYPE        "autoMemberFilter"
#define AUTOMEMBER_EXC_REGEX_TYPE     "autoMemberExclusiveRegex"
#define AUTOMEMBER_INC_REGEX_TYPE     "autoMemberInclusiveRegex"
#define AUTOMEMBER_DEFAULT_GROUP_TYPE "autoMemberDefaultGroup"
#define AUTOMEMBER_GROUPING_ATTR_TYPE "autoMemberGroupingAttr"
#define AUTOMEMBER_DISABLED_TYPE      "autoMemberDisabled"

/*
 * Helper defines
 */
#define IS_ATTRDESC_CHAR(c) ( isalnum(c) || (c == '.') || (c == ';') || (c == '-') )

struct automemberRegexRule {
    PRCList list;
    Slapi_DN *target_group_dn;
    char *desc;
    char *attr;
    char *regex_str;
    Slapi_Regex *regex;
};

struct automemberDNListItem {
    PRCList list;
    Slapi_DN *dn;
};

/*
 * Linked list of config entries.
 */
struct configEntry {
    PRCList list;
    char *dn;
    char *scope;
    Slapi_Filter *filter;
    struct automemberRegexRule *exclusive_rules;
    struct automemberRegexRule *inclusive_rules;
    char **default_groups;
    char *grouping_attr;
    char *grouping_value;
};

/*
 * Config fetch function
 */
PRCList *automember_get_config();

/*
 * Config cache locking functions
 */
void automember_config_read_lock();
void automember_config_write_lock();
void automember_config_unlock();

/*
 * Plugin identity functions
 */
void automember_set_plugin_id(void *pluginID);
void *automember_get_plugin_id();
void automember_set_plugin_dn(char *pluginDN);
char *automember_get_plugin_dn();

