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
 * Linked attributes plug-in header file
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
#define LINK_PLUGIN_SUBSYSTEM "linkedattrs-plugin"
#define LINK_FEATURE_DESC     "Linked Attributes"
#define LINK_PLUGIN_DESC      "Linked Attributes plugin"
#define LINK_INT_POSTOP_DESC  "Linked Attributes internal postop plugin"
#define LINK_POSTOP_DESC      "Linked Attributes postop plugin"

/*
 * Config type defines
 */
#define LINK_LINK_TYPE    "linkType"
#define LINK_MANAGED_TYPE "managedType"
#define LINK_SCOPE        "linkScope"

/*
 * Other defines
 */
#define DN_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.12"

/*
 * Linked list of config entries.
 */
struct configEntry
{
    PRCList list;
    char *dn;
    char *linktype;
    char *managedtype;
    char *scope;
    Slapi_DN *suffix;
    Slapi_Mutex *lock;
};

/*
 * Linked list used for indexing config entries
 * by managed type.
 */
struct configIndex
{
    PRCList list;
    struct configEntry *config;
};

/*
 * Fixup task private data.
 */
typedef struct _task_data
{
    char *linkdn;
    char *bind_dn;
} task_data;


/*
 * Debug functions - global, for the debugger
 */
void linked_attrs_dump_config(void);
void linked_attrs_dump_config_index(void);
void linked_attrs_dump_config_entry(struct configEntry *);

/*
 * Config fetch function
 */
PRCList *linked_attrs_get_config(void);

/*
 * Config cache locking functions
 */
void linked_attrs_read_lock(void);
void linked_attrs_write_lock(void);
void linked_attrs_unlock(void);

/*
 * Plugin identity functions
 */
void linked_attrs_set_plugin_id(void *pluginID);
void *linked_attrs_get_plugin_id(void);
void linked_attrs_set_plugin_dn(const char *pluginDN);
char *linked_attrs_get_plugin_dn(void);

/*
 * Fixup task callback
 */
int linked_attrs_fixup_task_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);

extern int plugin_is_betxn;
