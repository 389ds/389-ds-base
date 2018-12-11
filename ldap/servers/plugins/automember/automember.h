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
 * Auto Membership plug-in header file
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "prclist.h"
#include "ldif.h"

/*
 * Plug-in defines
 */
#define AUTOMEMBER_PLUGIN_SUBSYSTEM "auto-membership-plugin"
#define AUTOMEMBER_FEATURE_DESC "Auto Membership"
#define AUTOMEMBER_PLUGIN_DESC "Auto Membership plugin"
#define AUTOMEMBER_INT_POSTOP_DESC "Auto Membership internal postop plugin"
#define AUTOMEMBER_POSTOP_DESC "Auto Membership postop plugin"

/*
 * Config type defines
 */
#define AUTOMEMBER_SCOPE_TYPE "autoMemberScope"
#define AUTOMEMBER_FILTER_TYPE "autoMemberFilter"
#define AUTOMEMBER_EXC_REGEX_TYPE "autoMemberExclusiveRegex"
#define AUTOMEMBER_INC_REGEX_TYPE "autoMemberInclusiveRegex"
#define AUTOMEMBER_DEFAULT_GROUP_TYPE "autoMemberDefaultGroup"
#define AUTOMEMBER_GROUPING_ATTR_TYPE "autoMemberGroupingAttr"
#define AUTOMEMBER_DISABLED_TYPE "autoMemberDisabled"
#define AUTOMEMBER_TARGET_GROUP_TYPE "autoMemberTargetGroup"
#define AUTOMEMBER_DO_MODIFY "autoMemberProcessModifyOps"

/*
 * Config loading filters
 */
#define AUTOMEMBER_DEFINITION_FILTER "objectclass=autoMemberDefinition"
#define AUTOMEMBER_REGEX_RULE_FILTER "objectclass=autoMemberRegexRule"

/*
 * Helper defines
 */
#define IS_ATTRDESC_CHAR(c) (isalnum(c) || (c == '.') || (c == ';') || (c == '-'))
#define MEMBERSHIP_UPDATED 1
#define ADD_MEMBER 1
#define DEL_MEMBER 0


struct automemberRegexRule
{
    PRCList list;
    Slapi_DN *target_group_dn;
    char *attr;
    char *regex_str;
    Slapi_Regex *regex;
};

struct automemberDNListItem
{
    PRCList list;
    Slapi_DN *dn;
};

/*
 * Linked list of config entries.
 */
struct configEntry
{
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
PRCList *automember_get_config(void);

/*
 * Config cache locking functions
 */
void automember_config_read_lock(void);
void automember_config_write_lock(void);
void automember_config_unlock(void);

/*
 * Plugin identity functions
 */
void automember_set_plugin_id(void *pluginID);
void *automember_get_plugin_id(void);
void automember_set_plugin_dn(char *pluginDN);
char *automember_get_plugin_dn(void);
