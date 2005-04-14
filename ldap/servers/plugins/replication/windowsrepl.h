/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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
Slapi_Entry *windows_conn_get_search_result(Repl_Connection *conn );
void windows_private_update_dirsync_control(const Repl_Agmt *ra,LDAPControl **controls );
PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra);
void windows_private_null_dirsync_cookie(const Repl_Agmt *ra);
int windows_private_save_dirsync_cookie(const Repl_Agmt *ra);
int windows_private_load_dirsync_cookie(const Repl_Agmt *ra);
void windows_private_set_create_users(const Repl_Agmt *ra, PRBool value);
PRBool windows_private_create_users(const Repl_Agmt *ra);
const char *windows_private_get_windows_domain(const Repl_Agmt *ra);
static void windows_private_set_windows_domain(const Repl_Agmt *ra, char *domain);
int windows_private_get_isnt4(const Repl_Agmt *ra);
void windows_private_set_isnt4(const Repl_Agmt *ra, int isit);
const char* windows_private_get_purl(const Repl_Agmt *ra);

/* in windows_connection.c */
ConnResult windows_conn_connect(Repl_Connection *conn);
void windows_conn_disconnect(Repl_Connection *conn);
void windows_conn_delete(Repl_Connection *conn);
void windows_conn_get_error(Repl_Connection *conn, int *operation, int *error);
ConnResult windows_conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_delete(Repl_Connection *conn, const char *dn,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_rename(Repl_Connection *conn, const char *dn,
	const char *newrdn, const char *newparent, int deleteoldrdn,
	LDAPControl *update_control, LDAPControl ***returned_controls);
ConnResult windows_conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid,
	struct berval *payload, char **retoidp, struct berval **retdatap,
	LDAPControl *update_control, LDAPControl ***returned_controls);
const char *windows_conn_get_status(Repl_Connection *conn);
void windows_conn_start_linger(Repl_Connection *conn);
void windows_conn_cancel_linger(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_ds5_repl(Repl_Connection *conn);
ConnResult windows_conn_replica_supports_dirsync(Repl_Connection *conn);
ConnResult windows_conn_read_entry_attribute(Repl_Connection *conn, const char *dn, char *type,
	struct berval ***returned_bvals);
ConnResult windows_conn_push_schema(Repl_Connection *conn, CSN **remotecsn);
void windows_conn_set_timeout(Repl_Connection *conn, long timeout);
void windows_conn_set_agmt_changed(Repl_Connection *conn);

