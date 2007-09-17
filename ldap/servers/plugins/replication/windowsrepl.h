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
void windows_private_set_windows_subtree (const Repl_Agmt *ra,const Slapi_DN* sdn );
const Slapi_DN* windows_private_get_windows_subtree (const Repl_Agmt *ra);
void windows_private_set_directory_subtree (const Repl_Agmt *ra,const Slapi_DN* sdn );
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
static void windows_private_set_windows_domain(const Repl_Agmt *ra, char *domain);
int windows_private_get_isnt4(const Repl_Agmt *ra);
void windows_private_set_isnt4(const Repl_Agmt *ra, int isit);
int windows_private_get_iswin2k3(const Repl_Agmt *ra);
void windows_private_set_iswin2k3(const Repl_Agmt *ra, int isit);
Slapi_Filter* windows_private_get_directory_filter(const Repl_Agmt *ra);
Slapi_Filter* windows_private_get_deleted_filter(const Repl_Agmt *ra);
const char* windows_private_get_purl(const Repl_Agmt *ra);

/* in windows_connection.c */
ConnResult windows_conn_connect(Repl_Connection *conn);
void windows_conn_disconnect(Repl_Connection *conn);
void windows_conn_delete(Repl_Connection *conn);
void windows_conn_get_error(Repl_Connection *conn, int *operation, int *error);
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
