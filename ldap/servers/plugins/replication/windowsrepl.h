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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* windows_private.c */
typedef struct windowsprivate Dirsync_Private;
Dirsync_Private* windows_private_new();
void windows_private_set_windows_subtree (const Repl_Agmt *ra,Slapi_DN* sdn );
const Slapi_DN* windows_private_get_windows_subtree (const Repl_Agmt *ra);
void windows_private_set_directory_subtree (const Repl_Agmt *ra,Slapi_DN* sdn );
const Slapi_DN* windows_private_get_directory_subtree (const Repl_Agmt *ra);
LDAPControl* windows_private_dirsync_control(const Repl_Agmt *ra);
ConnResult send_dirsync_search(Repl_Connection *conn);
ConnResult windows_search_entry(Repl_Connection *conn, char* searchbase, char *filter, Slapi_Entry **entry);
ConnResult windows_search_entry_ext(Repl_Connection *conn, char* searchbase, char *filter, Slapi_Entry **entry, LDAPControl **serverctrls);
Slapi_Entry *windows_conn_get_search_result(Repl_Connection *conn );
void windows_private_update_dirsync_control(const Repl_Agmt *ra,LDAPControl **controls );
PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra);
void windows_private_null_dirsync_cookie(const Repl_Agmt *ra);
int windows_private_save_dirsync_cookie(const Repl_Agmt *ra);
int windows_private_load_dirsync_cookie(const Repl_Agmt *ra);
void windows_private_set_create_users(const Repl_Agmt *ra, PRBool value);
PRBool windows_private_create_users(const Repl_Agmt *ra);
void windows_private_set_create_groups(const Repl_Agmt *ra, PRBool value);
PRBool windows_private_create_groups(const Repl_Agmt *ra);
const char *windows_private_get_windows_domain(const Repl_Agmt *ra);
int windows_private_get_isnt4(const Repl_Agmt *ra);
void windows_private_set_isnt4(const Repl_Agmt *ra, int isit);
int windows_private_get_iswin2k3(const Repl_Agmt *ra);
void windows_private_set_iswin2k3(const Repl_Agmt *ra, int isit);
Slapi_Filter* windows_private_get_directory_filter(const Repl_Agmt *ra);
Slapi_Filter* windows_private_get_deleted_filter(const Repl_Agmt *ra);
const char* windows_private_get_purl(const Repl_Agmt *ra);
/*
 * The raw entry is the last raw entry read from AD - raw as opposed
 * "cooked" - that is, having had schema processing done
 */
/* get returns a pointer to the structure - do not free */
Slapi_Entry *windows_private_get_raw_entry(const Repl_Agmt *ra);
/* this is passin - windows_private owns the pointer, not a copy */
void windows_private_set_raw_entry(const Repl_Agmt *ra, Slapi_Entry *e);
void windows_private_set_keep_raw_entry(const Repl_Agmt *ra, int keep);
int windows_private_get_keep_raw_entry(const Repl_Agmt *ra);
void *windows_private_get_api_cookie(const Repl_Agmt *ra);
void windows_private_set_api_cookie(Repl_Agmt *ra, void *cookie);
time_t windows_private_get_sync_interval(const Repl_Agmt *ra);
void windows_private_set_sync_interval(Repl_Agmt *ra, char *str);
PRBool windows_private_get_one_way(const Repl_Agmt *ra);
void windows_private_set_one_way(const Repl_Agmt *ra, PRBool value);

/* in windows_connection.c */
ConnResult windows_conn_connect(Repl_Connection *conn);
void windows_conn_disconnect(Repl_Connection *conn);
void windows_conn_get_error(Repl_Connection *conn, int *operation, int *error);
void windows_conn_set_error(Repl_Connection *conn, int error);
ConnResult windows_conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs,
	LDAPControl **server_controls, LDAPControl ***returned_controls);
ConnResult windows_conn_send_delete(Repl_Connection *conn, const char *dn,
	LDAPControl **server_controls, LDAPControl ***returned_controls);
ConnResult windows_conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods,
	LDAPControl **server_controls, LDAPControl ***returned_controls);
ConnResult windows_conn_send_rename(Repl_Connection *conn, const char *dn,
	const char *newrdn, const char *newparent, int deleteoldrdn,
	LDAPControl **server_controls, LDAPControl ***returned_controls);
ConnResult windows_conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid,
	struct berval *payload, char **retoidp, struct berval **retdatap,
	LDAPControl **server_controls, LDAPControl ***returned_controls);
const char *windows_conn_get_status(Repl_Connection *conn);
void windows_conn_start_linger(Repl_Connection *conn);
void windows_conn_cancel_linger(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_ds5_repl(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_dirsync(Repl_Connection *conn);
ConnResult windows_conn_replica_is_win2k3(Repl_Connection *conn);
ConnResult windows_conn_read_entry_attribute(Repl_Connection *conn, const char *dn, char *type,
	struct berval ***returned_bvals);
ConnResult windows_conn_push_schema(Repl_Connection *conn, CSN **remotecsn);
void windows_conn_set_timeout(Repl_Connection *conn, long timeout);
void windows_conn_set_agmt_changed(Repl_Connection *conn);
int windows_check_user_password(Repl_Connection *conn, Slapi_DN *sdn, char *password);

/* Used to work around a schema incompatibility between Microsoft and the IETF */
#define FAKE_STREET_ATTR_NAME "in#place#of#streetaddress"
/* Used to work around contrained attribute legth for initials on AD */
#define AD_INITIALS_LENGTH 6
/* Used to check for pre-hashed passwords when syncing */
#define PASSWD_CLEAR_PREFIX "{clear}"
#define PASSWD_CLEAR_PREFIX_LEN 7
/* Used for GUID format conversion */
#define NTUNIQUEID_LENGTH 32
#define AD_GUID_LENGTH 36

/*
 * Periodic synchronization interval.  This is used for scheduling the periodic_dirsync event.
 * The time is in seconds.
 */
#define PERIODIC_DIRSYNC_INTERVAL 5 * 60 /* default value is 5 minutes */

/*
 * One way sync flags.  Used to indicate the direction when one-way sync is used.
 */
#define ONE_WAY_SYNC_DISABLED 0
#define ONE_WAY_SYNC_FROM_AD 1
#define ONE_WAY_SYNC_TO_AD 2

/* called for each replication agreement - so the winsync
   plugin can be agreement specific and store agreement
   specific data
*/
void windows_plugin_init(Repl_Agmt *ra);

void winsync_plugin_call_dirsync_search_params_cb(const Repl_Agmt *ra, const char *agmt_dn, char **base, int *scope, char **filter, char ***attrs, LDAPControl ***serverctrls);
/* called before searching for a single entry from AD - agmt_dn will be NULL */
void winsync_plugin_call_pre_ad_search_cb(const Repl_Agmt *ra, const char *agmt_dn, char **base, int *scope, char **filter, char ***attrs, LDAPControl ***serverctrls);
/* called before an internal search to get a single DS entry - agmt_dn will be NULL */
void winsync_plugin_call_pre_ds_search_entry_cb(const Repl_Agmt *ra, const char *agmt_dn, char **base, int *scope, char **filter, char ***attrs, LDAPControl ***serverctrls);
/* called before the total update to get all entries from the DS to sync to AD */
void winsync_plugin_call_pre_ds_search_all_cb(const Repl_Agmt *ra, const char *agmt_dn, char **base, int *scope, char **filter, char ***attrs, LDAPControl ***serverctrls);

void winsync_plugin_call_pre_ad_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify);
void winsync_plugin_call_pre_ad_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify);
void winsync_plugin_call_pre_ds_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify);
void winsync_plugin_call_pre_ds_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify);

void winsync_plugin_call_pre_ds_add_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry);
void winsync_plugin_call_pre_ds_add_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry);

void winsync_plugin_call_get_new_ds_user_dn_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
                                               char **new_dn_string, const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix);
void winsync_plugin_call_get_new_ds_group_dn_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
                                                char **new_dn_string, const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix);

void winsync_plugin_call_pre_ad_mod_user_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend);
void winsync_plugin_call_pre_ad_mod_group_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend);

int winsync_plugin_call_can_add_entry_to_ad_cb(const Repl_Agmt *ra, const Slapi_Entry *local_entry, const Slapi_DN *remote_dn);
void winsync_plugin_call_begin_update_cb(const Repl_Agmt *ra, const Slapi_DN *ds_subtree,
                                         const Slapi_DN *ad_subtree, int is_total);
void winsync_plugin_call_end_update_cb(const Repl_Agmt *ra, const Slapi_DN *ds_subtree,
                                       const Slapi_DN *ad_subtree, int is_total);
void winsync_plugin_call_destroy_agmt_cb(const Repl_Agmt *ra,
                                         const Slapi_DN *ds_subtree,
                                         const Slapi_DN *ad_subtree);
/*
  Call stack for all places where windows_LDAPMessage2Entry is called:

  windows_LDAPMessage2Entry
  ++windows_seach_entry_ext
  ++++windows_search_entry
  ++++++windows_get_remote_entry
          map_dn_values
            windows_create_remote_entry
              process_replay_add
              windows_process_total_add
            windows_map_mods_for_replay
              windows_replay_update
                send_updates
                  windows_inc_run
            windows_create_local_entry
              windows_process_dirsync_entry
            windows_generate_update_mods
              windows_update_remote_entry
                process_replay_add
                windows_process_total_add
              windows_update_local_entry
                windows_process_dirsync_entry
          process_replay_add
            windows_replay_update
          map_entry_dn_outbound
            map_dn_values
            windows_replay_update
            windows_process_total_entry
              send_entry
                windows_tot_run
          windows_process_total_add
            windows_process_total_entry
              send_entry
                windows_tot_run
          windows_process_dirsync_entry
            windows_dirsync_inc_run
        find_entry_by_attr_value_remote
          map_entry_dn_outbound
  ++++windows_get_remote_tombstone
        map_windows_tombstone_dn
          process_replay_add
  ++windows_conn_get_search_result
      windows_dirsync_inc_run


  windows_inc_protocol
  ++send_updates
  ++++windows_replay_update
*/
