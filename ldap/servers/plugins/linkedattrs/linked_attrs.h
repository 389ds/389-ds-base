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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
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
#include "prclist.h"
#include "ldif.h"

/*
 * Plug-in defines
 */
#define LINK_PLUGIN_SUBSYSTEM  "linkedattrs-plugin"
#define LINK_FEATURE_DESC      "Linked Attributes"
#define LINK_PLUGIN_DESC       "Linked Attributes plugin"
#define LINK_INT_POSTOP_DESC   "Linked Attributes internal postop plugin"
#define LINK_POSTOP_DESC       "Linked Attributes postop plugin"

/*
 * Config type defines
 */
#define LINK_LINK_TYPE         "linkType"
#define LINK_MANAGED_TYPE      "managedType"
#define LINK_SCOPE             "linkScope"

/*
 * Other defines
 */
#define DN_SYNTAX_OID          "1.3.6.1.4.1.1466.115.121.1.12"

/*
 * Linked list of config entries.
 */
struct configEntry {
    PRCList list;
    char *dn;
    char *linktype;
    char *managedtype;
    char *scope;
    Slapi_Mutex *lock;
};

/*
 * Linked list used for indexing config entries
 * by managed type.
 */
struct configIndex {
    PRCList list;
    struct configEntry *config;
};

/*
 * Fixup task private data.
 */
typedef struct _task_data
{
    char *linkdn;
} task_data;


/*
 * Debug functions - global, for the debugger
 */
void linked_attrs_dump_config();
void linked_attrs_dump_config_index();
void linked_attrs_dump_config_entry(struct configEntry *);

/*
 * Config fetch function
 */
PRCList *linked_attrs_get_config();

/*
 * Config cache locking functions
 */
void linked_attrs_read_lock();
void linked_attrs_write_lock();
void linked_attrs_unlock();

/*
 * Plugin identity functions
 */
void linked_attrs_set_plugin_id(void *pluginID);
void *linked_attrs_get_plugin_id();
void linked_attrs_set_plugin_dn(char *pluginDN);
char *linked_attrs_get_plugin_dn();

/*
 * Fixup task callback
 */
int linked_attrs_fixup_task_add(Slapi_PBlock *pb, Slapi_Entry *e,
                           Slapi_Entry *eAfter, int *returncode,
                           char *returntext, void *arg);

