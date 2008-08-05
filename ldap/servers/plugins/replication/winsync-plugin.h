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

/*
 * The plugin will define this callback in order to initialize itself.
 * The ds subtree and the ad subtree from the sync agreement are passed in.
 * These are read only.
 * The return value is private data to the plugin that will be passed back
 * at each callback
 */
typedef void * (*winsync_plugin_init_cb)(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree);
#define WINSYNC_PLUGIN_INIT_CB 1
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
 * rawentry  - the raw AD entry, read directly from AD - may be NULL
 * local_dn - the original local DN used in the modification
 * origmods - the original mod list
 * remote_dn - this is the DN which will be used with the remote modify operation
 *             to AD - the winsync code may have already attempted to calculate its value
 * modstosend - this is the list of modifications which will be sent - the winsync
 *              code will already have done its default mapping to these values
 * 
 */
typedef void (*winsync_pre_ad_mod_mods_cb)(void *cookie, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend);
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
  The following are sample code stubs to show how to implement
  a plugin which uses this api
*/

#ifdef WINSYNC_SAMPLE_CODE

#include "slapi-plugin.h"
#include "winsync-plugin.h"

static char *test_winsync_plugin_name = "test_winsync_api";

static void *
test_winsync_api_init(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_init [%s] [%s] -- begin\n",
                    slapi_sdn_get_dn(ds_subtree),
                    slapi_sdn_get_dn(ad_subtree));

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_init -- end\n");

    return NULL;
}

static void
test_winsync_dirsync_search_params_cb(void *cbdata, const char *agmt_dn,
                                      char **base, int *scope, char **filter,
                                      char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_dirsync_search_params_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_dirsync_search_params_cb -- end\n");

    return;
}

/* called before searching for a single entry from AD - agmt_dn will be NULL */
static void
test_winsync_pre_ad_search_cb(void *cbdata, const char *agmt_dn,
                              char **base, int *scope, char **filter,
                              char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_search_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_search_cb -- end\n");

    return;
}

/* called before an internal search to get a single DS entry - agmt_dn will be NULL */
static void
test_winsync_pre_ds_search_entry_cb(void *cbdata, const char *agmt_dn,
                                    char **base, int *scope, char **filter,
                                    char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_search_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_search_cb -- end\n");

    return;
}

/* called before the total update to get all entries from the DS to sync to AD */
static void
test_winsync_pre_ds_search_all_cb(void *cbdata, const char *agmt_dn,
                                  char **base, int *scope, char **filter,
                                  char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_search_all_cb -- orig filter [%s] -- begin\n",
                    ((filter && *filter) ? *filter : "NULL"));

    /* We only want to grab users from the ds side - no groups */
    slapi_ch_free_string(filter);
    /* maybe use ntUniqueId=* - only get users that have already been
       synced with AD already - ntUniqueId and ntUserDomainId are
       indexed for equality only - need to add presence? */
    *filter = slapi_ch_strdup("(&(objectclass=ntuser)(ntUserDomainId=*))");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_search_all_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_mod_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_mod_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_add_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_add_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_add_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_add_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_add_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_add_group_cb -- end\n");

    return;
}

static void
test_winsync_get_new_ds_user_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
                                   Slapi_Entry *ad_entry, char **new_dn_string,
                                   const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    char **rdns = NULL;

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_get_new_ds_user_dn_cb -- old dn [%s] -- begin\n",
                    *new_dn_string);

    rdns = ldap_explode_dn(*new_dn_string, 0);
    if (!rdns || !rdns[0]) {
        ldap_value_free(rdns);
        return;
    }

    slapi_ch_free_string(new_dn_string);
    *new_dn_string = PR_smprintf("%s,%s", rdns[0], slapi_sdn_get_dn(ds_suffix));
    ldap_value_free(rdns);

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_get_new_ds_user_dn_cb -- new dn [%s] -- end\n",
                    *new_dn_string);

    return;
}

static void
test_winsync_get_new_ds_group_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
                                   Slapi_Entry *ad_entry, char **new_dn_string,
                                   const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_get_new_ds_group_dn_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_get_new_ds_group_dn_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_user_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
                                     const Slapi_DN *local_dn, LDAPMod * const *origmods,
                                     Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_user_mods_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_user_mods_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_group_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
                                     const Slapi_DN *local_dn, LDAPMod * const *origmods,
                                     Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_group_mods_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_group_mods_cb -- end\n");

    return;
}

static int
test_winsync_can_add_entry_to_ad_cb(void *cbdata, const Slapi_Entry *local_entry,
                                    const Slapi_DN *remote_dn)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_can_add_entry_to_ad_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_can_add_entry_to_ad_cb -- end\n");

    return 0; /* false - do not allow entries to be added to ad */
}

/**
 * Plugin identifiers
 */
static Slapi_PluginDesc test_winsync_pdesc = {
    "test-winsync-plugin",
    PLUGIN_MAGIC_VENDOR_STR,
    PRODUCTTEXT,
    "test winsync plugin"
};

static Slapi_ComponentId *test_winsync_plugin_id = NULL;

static void *test_winsync_api[] = {
    NULL, /* reserved for api broker use, must be zero */
    test_winsync_api_init,
    test_winsync_dirsync_search_params_cb,
    test_winsync_pre_ad_search_cb,
    test_winsync_pre_ds_search_entry_cb,
    test_winsync_pre_ds_search_all_cb,
    test_winsync_pre_ad_mod_user_cb,
    test_winsync_pre_ad_mod_group_cb,
    test_winsync_pre_ds_mod_user_cb,
    test_winsync_pre_ds_mod_group_cb,
    test_winsync_pre_ds_add_user_cb,
    test_winsync_pre_ds_add_group_cb,
    test_winsync_get_new_ds_user_dn_cb,
    test_winsync_get_new_ds_group_dn_cb,
    test_winsync_pre_ad_mod_user_mods_cb,
    test_winsync_pre_ad_mod_group_mods_cb,
    test_winsync_can_add_entry_to_ad_cb
};

static int
test_winsync_plugin_start(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_start -- begin\n");

	if( slapi_apib_register(WINSYNC_v1_0_GUID, test_winsync_api) ) {
        slapi_log_error( SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_start -- failed to register winsync api -- end\n");
        return -1;
	}
	
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_plugin_start -- end\n");
	return 0;
}

static int
test_winsync_plugin_close(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_close -- begin\n");

	slapi_apib_unregister(WINSYNC_v1_0_GUID);

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_plugin_close -- end\n");
	return 0;
}

/* this is the slapi plugin init function,
   not the one used by the winsync api
*/
int test_winsync_plugin_init(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_init -- begin\n");

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
                           SLAPI_PLUGIN_VERSION_01 ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                          (void *) test_winsync_plugin_start ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                          (void *) test_winsync_plugin_close ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&test_winsync_pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_init -- failed to register plugin -- end\n");
        return -1;
    }

    /* Retrieve and save the plugin identity to later pass to
       internal operations */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &test_winsync_plugin_id) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_init -- failed to retrieve plugin identity -- end\n");
        return -1;
    }

    slapi_log_error( SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                     "<-- test_winsync_plugin_init -- end\n");
    return 0;
}

/*
dn: cn=Test Winsync API,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: Test Winsync API
nsslapd-pluginpath: libtestwinsync-plugin
nsslapd-plugininitfunc: test_winsync_plugin_init
nsslapd-plugintype: preoperation
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-plugin-depends-on-named: Multimaster Replication Plugin
*/

#endif /* WINSYNC_SAMPLE_CODE */

#endif /* WINSYNC_PLUGIN_PUBLIC_API */
