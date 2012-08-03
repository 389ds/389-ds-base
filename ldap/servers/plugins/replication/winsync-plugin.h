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
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef WINSYNC_PLUGIN_PUBLIC_API
#define WINSYNC_PLUGIN_PUBLIC_API

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* windows_private.c */

#include "slapi-plugin.h"

/*
 * WinSync plug-in API
 */
#define WINSYNC_v1_0_GUID "CDA8F029-A3C6-4EBB-80B8-A2E183DB0481"
#define WINSYNC_v2_0_GUID "706B83AA-FC51-444A-ACC9-53DC73D641D4"
#define WINSYNC_v3_0_GUID "6D7C2E54-638C-4564-B53F-D9C5354DEBA0"

/*
 * This callback is called when a winsync agreement is created.
 * The ds_subtree and ad_subtree from the agreement are read-only.
 * The callback can allocate some private data to return.  If so
 * the callback must define a winsync_plugin_destroy_agmt_cb so
 * that the private data can be freed.  This private data is passed
 * to every other callback function as the void *cookie argument.
 */
typedef void * (*winsync_plugin_init_cb)(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree);
#define WINSYNC_PLUGIN_INIT_CB 1
#define WINSYNC_PLUGIN_VERSION_1_BEGIN WINSYNC_PLUGIN_INIT_CB

/* agmt_dn - const - the original AD base dn from the winsync agreement
   scope - set directly e.g. *scope = 42;
   base, filter - malloced - to set, free first e.g.
       slapi_ch_free_string(filter);
       *base = slapi_ch_strdup("(objectclass=foobar)");
       winsync code will use slapi_ch_free_string to free this value, so no static strings
   attrs - NULL or null terminated array of strings - can use slapi_ch_array_add to add e.g.
       slapi_ch_array_add(attrs, slapi_ch_strdup("myattr"));
       attrs will be freed with slapi_ch_array_free, so caller must own the memory
   serverctrls - NULL or null terminated array of LDAPControl* - can use slapi_add_control_ext to add
       slapi_add_control_ext(serverctrls, mynewctrl, 1 / add a copy /);
       serverctrls will be freed with ldap_controls_free, so caller must own memory
*/
typedef void (*winsync_search_params_cb)(void *cookie, const char *agmt_dn, char **base, int *scope, char **filter, char ***attrs, LDAPControl ***serverctrls);
#define WINSYNC_PLUGIN_DIRSYNC_SEARCH_CB 2 /* serverctrls will already contain the DirSync control */
#define WINSYNC_PLUGIN_PRE_AD_SEARCH_CB 3
#define WINSYNC_PLUGIN_PRE_DS_SEARCH_ENTRY_CB 4
#define WINSYNC_PLUGIN_PRE_DS_SEARCH_ALL_CB 5
/*
 * These callbacks are the main entry points that allow the plugin
 * to intercept modifications to local and remote entries.
 * rawentry  - the raw AD entry, read directly from AD - this is read only
 * ad_entry  - the "cooked" AD entry - the DN in this entry should be set
 *             when the operation is to modify the AD entry
 * ds_entry  - the entry from the ds - the DN in this entry should be set
 *             when the operation is to modify the DS entry
 * smods     - the post-processing modifications - these should be modified
 *             by the plugin as needed
 * do_modify - if the code has some modifications that need to be applied, this
 *             will be set to true - if the plugin has added some items to smods
 *             this should be set to true - if the plugin has removed all of
 *             the smods, and no operation should be performed, this should
 *             be set to false
 */
typedef void (*winsync_pre_mod_cb)(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify);
#define WINSYNC_PLUGIN_PRE_AD_MOD_USER_CB 6
#define WINSYNC_PLUGIN_PRE_AD_MOD_GROUP_CB 7
#define WINSYNC_PLUGIN_PRE_DS_MOD_USER_CB 8
#define WINSYNC_PLUGIN_PRE_DS_MOD_GROUP_CB 9
/*
 * These callbacks are called when a new entry is being added to the
 * local directory server from AD.
 * rawentry  - the raw AD entry, read directly from AD - this is read only
 * ad_entry  - the "cooked" AD entry
 * ds_entry  - the entry to be added to the DS - all modifications should
 *             be made to this entry, including changing the DN if needed,
 *             since the DN of this entry will be used as the ADD target DN
 *             This entry will already have had the default schema mapping applied
 */
typedef void (*winsync_pre_add_cb)(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry);
#define WINSYNC_PLUGIN_PRE_DS_ADD_USER_CB 10
#define WINSYNC_PLUGIN_PRE_DS_ADD_GROUP_CB 11
/*
 * If a new entry has been added to AD, and we're sync'ing it over
 * to the DS, we may need to create a new DN for the entry.  The
 * code tries to come up with a reasonable DN, but the plugin may
 * have different ideas.  These callbacks allow the plugin to specify
 * what the new DN for the new entry should be.  This is called from
 * map_entry_dn_inbound which is called from various places where the DN for
 * the new entry is needed.  The winsync_plugin_call_pre_ds_add_* callbacks
 * can also be used to set the DN just before the entry is stored in the DS.
 * This is also used when we are mapping a dn valued attribute e.g. owner
 * or secretary
 * rawentry  - the raw AD entry, read directly from AD - this is read only
 * ad_entry  - the "cooked" AD entry
 * new_dn_string - the given value will be the default value created by the sync code
 *                 to change it, slapi_ch_free_string first, then malloc the value to use
 * ds_suffix - the suffix from the DS side of the sync agreement
 * ad_suffix - the suffix from the AD side of the sync agreement
 */
typedef void (*winsync_get_new_dn_cb)(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, char **new_dn_string,
  const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix);
#define WINSYNC_PLUGIN_GET_NEW_DS_USER_DN_CB 12
#define WINSYNC_PLUGIN_GET_NEW_DS_GROUP_DN_CB 13
/*
 * These callbacks are called when a mod operation is going to be replayed
 * to AD.  This case is different than the pre add or pre mod callbacks
 * above because in this context, we may only have the list of modifications
 * and the DN to which the mods were applied.
 * rawentry - the raw AD entry, read directly from AD - may be NULL
 * local_dn - the original local DN used in the modification
 * ds_entry - the current DS entry that has the operation nsUniqueID
 * origmods - the original mod list
 * remote_dn - this is the DN which will be used with the remote modify operation
 *             to AD - the winsync code may have already attempted to calculate its value
 * modstosend - this is the list of modifications which will be sent - the winsync
 *              code will already have done its default mapping to these values
 * 
 */
typedef void (*winsync_pre_ad_mod_mods_cb)(void *cookie, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend);
#define WINSYNC_PLUGIN_PRE_AD_MOD_USER_MODS_CB 14
#define WINSYNC_PLUGIN_PRE_AD_MOD_GROUP_MODS_CB 15

/*
 * Callbacks used to determine if an entry should be added to the
 * AD side if it does not already exist.
 * local_entry - the candidate entry to test
 * remote_DN - the candidate remote entry to add
 */
typedef int (*winsync_can_add_to_ad_cb)(void *cookie, const Slapi_Entry *local_entry, const Slapi_DN *remote_dn);
#define WINSYNC_PLUGIN_CAN_ADD_ENTRY_TO_AD_CB 16

/*
 * Callbacks called at begin and end of update
 * 
 * The ds subtree and the ad subtree from the sync agreement are passed in.
 * These are read only.
 * is_total will be true if this is a total update, or false if this
 * is an incremental update
 */
typedef void (*winsync_plugin_update_cb)(void *cookie, const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree, int is_total);
#define WINSYNC_PLUGIN_BEGIN_UPDATE_CB 17
#define WINSYNC_PLUGIN_END_UPDATE_CB 18

/*
 * Callbacks called when the agreement is destroyed.
 * 
 * The ds subtree and the ad subtree from the sync agreement are passed in.
 * These are read only.
 * The plugin must define this function to free the cookie allocated
 * in the init function, if any.
 */
typedef void (*winsync_plugin_destroy_agmt_cb)(void *cookie, const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree);
#define WINSYNC_PLUGIN_DESTROY_AGMT_CB 19
#define WINSYNC_PLUGIN_VERSION_1_END WINSYNC_PLUGIN_DESTROY_AGMT_CB

/* Functions added for API version 2.0 */
/*
 * These callbacks are called after a modify operation.  They are called upon both
 * success and failure of the modify operation.  The plugin is responsible for
 * looking at the result code of the modify to decide what action to take.  The
 * plugin may change the result code e.g. to force an error for an otherwise
 * successful operation, or to ignore certain errors.
 * rawentry  - the raw AD entry, read directly from AD - this is read only
 * ad_entry  - the "cooked" AD entry - the entry passed to the pre_mod callback
 * ds_entry  - the entry from the ds - the DS entry passed to the pre_mod callback
 * smods     - the mods used in the modify operation
 * result    - the result code from the modify operation - the plugin can change this
 */
typedef void (*winsync_post_mod_cb)(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *result);
#define WINSYNC_PLUGIN_POST_AD_MOD_USER_CB 20
#define WINSYNC_PLUGIN_POST_AD_MOD_GROUP_CB 21
#define WINSYNC_PLUGIN_POST_DS_MOD_USER_CB 22
#define WINSYNC_PLUGIN_POST_DS_MOD_GROUP_CB 23

#define WINSYNC_PLUGIN_VERSION_2_BEGIN WINSYNC_PLUGIN_POST_AD_MOD_USER_CB
/*
 * These callbacks are called after an attempt to add a new entry to the
 * local directory server from AD.  They are called upon success or failure
 * of the add attempt.  The result code tells if the operation succeeded.
 * The plugin may change the result code e.g. to force an error for an
 * otherwise successful operation, or to ignore certain errors.
 * rawentry  - the raw AD entry, read directly from AD - this is read only
 * ad_entry  - the "cooked" AD entry
 * ds_entry  - the entry attempted to be added to the DS
 * result    - the result code from the add operation - plugin may change this
 */
typedef void (*winsync_post_add_cb)(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, int *result);
#define WINSYNC_PLUGIN_POST_DS_ADD_USER_CB 24
#define WINSYNC_PLUGIN_POST_DS_ADD_GROUP_CB 25

/*
 * These callbacks are called when a new entry is being added to AD from
 * the local directory server.
 * ds_entry  - the local DS entry
 * ad_entry  - the entry to be added to AD - all modifications should
 *             be made to this entry, including changing the DN if needed,
 *             since the DN of this entry will be used as the ADD target DN
 *             This entry will already have had the default schema mapping applied
*/
typedef void (*winsync_pre_ad_add_cb)(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry);
#define WINSYNC_PLUGIN_PRE_AD_ADD_USER_CB 26
#define WINSYNC_PLUGIN_PRE_AD_ADD_GROUP_CB 27

/*
 * These callbacks are called after an attempt to add a new entry to AD from
 * the local directory server.  They are called upon success or failure
 * of the add attempt.  The result code tells if the operation succeeded.
 * The plugin may change the result code e.g. to force an error for an
 * otherwise successful operation, or to ignore certain errors.
 * ad_entry  - the AD entry
 * ds_entry  - the DS entry
 * result    - the result code from the add operation - plugin may change this
 */
typedef void (*winsync_post_ad_add_cb)(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry, int *result);
#define WINSYNC_PLUGIN_POST_AD_ADD_USER_CB 28
#define WINSYNC_PLUGIN_POST_AD_ADD_GROUP_CB 29

/*
 * These callbacks are called after a mod operation has been replayed
 * to AD.  This case is different than the pre add or pre mod callbacks
 * above because in this context, we may only have the list of modifications
 * and the DN to which the mods were applied.  If the plugin wants the modified
 * entry, the plugin can search for it from AD.  The plugin is called upon
 * success or failure of the modify operation.  The result parameter gives
 * the ldap result code of the operation.  The plugin may change the result code
 * e.g. to force an error for an otherwise successful operation, or to ignore
 * certain errors.
 * rawentry - the raw AD entry, read directly from AD - may be NULL
 * local_dn - the original local DN used in the modification
 * ds_entry - the current DS entry that has the operation nsUniqueID
 * origmods - the original mod list
 * remote_dn - the DN of the AD entry
 * modstosend - the mods sent to AD
 * result   - the result code of the modify operation
 * 
 */
typedef void (*winsync_post_ad_mod_mods_cb)(void *cookie, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod **modstosend, int *result);
#define WINSYNC_PLUGIN_POST_AD_MOD_USER_MODS_CB 30
#define WINSYNC_PLUGIN_POST_AD_MOD_GROUP_MODS_CB 31
#define WINSYNC_PLUGIN_VERSION_2_END WINSYNC_PLUGIN_POST_AD_MOD_GROUP_MODS_CB

typedef int (*winsync_plugin_precedence_cb)(void);
#define WINSYNC_PLUGIN_PRECEDENCE_CB 32
#define WINSYNC_PLUGIN_VERSION_3_END WINSYNC_PLUGIN_PRECEDENCE_CB

/* precedence works like regular slapi plugin precedence */
#define WINSYNC_PLUGIN_DEFAULT_PRECEDENCE 50

#endif /* WINSYNC_PLUGIN_PUBLIC_API */
