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


/* repl5_connection.c */
/*

 The connection object manages a connection to a single replication
 consumer.

XXXggood what to do on timeout? If we close connection, then we won't leave a
replica locked. Seems like right thing to do.
*/

#include "repl5.h"
#include "windowsrepl.h"
#if !defined(USE_OPENLDAP)
#include "ldappr.h"
#endif
#include "slap.h"
#include "nss.h"

typedef struct repl_connection
{
	char *hostname;
	int port;
	char *binddn;
	int bindmethod;
	int state;
	int last_operation;
	int last_ldap_error;
	const char *status;
	char *last_ldap_errmsg;
	PRUint32 transport_flags;
	LDAP *ld;
	int supports_ldapv3; /* 1 if does, 0 if doesn't, -1 if not determined */
	int supports_ds50_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
	int supports_ds40_repl; /* 1 if does, 0 if doesn't, -1 if not determined */
	int linger_time; /* time in seconds to leave an idle connection open */
	int supports_dirsync; /* 1 if does, 0 if doesn't, -1 if not determined */
	PRBool linger_active;
	Slapi_Eq_Context *linger_event;
	PRBool delete_after_linger;
	int refcnt;
	const Repl_Agmt *agmt;
	PRLock *lock;
	struct timeval timeout;
	int flag_agmt_changed;
	char *plain;
	int is_win2k3; /* 1 if it is win2k3 or later, 0 if not, -1 if not determined */
} repl_connection;

/* #define DEFAULT_LINGER_TIME (5 * 60) */ /* 5 minutes */
#define DEFAULT_LINGER_TIME (60) 

/* Controls we add on every outbound operation */

static LDAPControl manageDSAITControl = {LDAP_CONTROL_MANAGEDSAIT, {0, ""}, '\0'};
static int attribute_string_value_present(LDAP *ld, LDAPMessage *entry,
	const char *type, const char *value);
static int bind_and_check_pwp(Repl_Connection *conn, char * binddn, char *password);
static int do_simple_bind (Repl_Connection *conn, LDAP *ld, char * binddn, char *password);

static int s_debug_timeout = 0;
static int s_debug_level = 0;
static Slapi_Eq_Context repl5_start_debug_timeout(int *setlevel);
static void repl5_stop_debug_timeout(Slapi_Eq_Context eqctx, int *setlevel);
static void repl5_debug_timeout_callback(time_t when, void *arg);

#define STATE_CONNECTED 600
#define STATE_DISCONNECTED 601

#define STATUS_DISCONNECTED "disconnected"
#define STATUS_CONNECTED "connected" 
#define STATUS_PROCESSING_ADD "processing add operation"
#define STATUS_PROCESSING_DELETE "processing delete operation"
#define STATUS_PROCESSING_MODIFY "processing modify operation"
#define STATUS_PROCESSING_RENAME "processing rename operation"
#define STATUS_PROCESSING_EXTENDED_OPERATION "processing extended operation"
#define STATUS_LINGERING "lingering"
#define STATUS_SHUTTING_DOWN "shutting down"
#define STATUS_BINDING "connecting and binding"
#define STATUS_SEARCHING "processing search operation"

#define CONN_NO_OPERATION 0
#define CONN_ADD 1
#define CONN_DELETE 2
#define CONN_MODIFY 3
#define CONN_RENAME 4
#define CONN_EXTENDED_OPERATION 5
#define CONN_BIND 6
#define CONN_INIT 7
#define CONN_SEARCH 8

/* These are errors returned from ldap operations which should cause us to disconnect and
   retry the connection later */
#define IS_DISCONNECT_ERROR(rc) (rc == LDAP_SERVER_DOWN || rc == LDAP_CONNECT_ERROR || rc == LDAP_INVALID_CREDENTIALS || rc == LDAP_INAPPROPRIATE_AUTH || rc == LDAP_LOCAL_ERROR)

/* Forward declarations */
static void close_connection_internal(Repl_Connection *conn);

/*
 * Create a new conenction object. Returns a pointer to the object, or
 * NULL if an error occurs.
 */
Repl_Connection *
windows_conn_new(Repl_Agmt *agmt)
{
	Repl_Connection *rpc;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_new\n", 0, 0, 0 );

	rpc = (Repl_Connection *)slapi_ch_malloc(sizeof(repl_connection));
	if ((rpc->lock = PR_NewLock()) == NULL)
	{
		goto loser;
	}
	rpc->hostname = agmt_get_hostname(agmt);
	rpc->port = agmt_get_port(agmt);
	rpc->binddn = agmt_get_binddn(agmt);
	rpc->bindmethod = agmt_get_bindmethod(agmt);
	rpc->transport_flags = agmt_get_transport_flags(agmt);
	rpc->ld = NULL;
	rpc->state = STATE_DISCONNECTED;
	rpc->last_operation = CONN_NO_OPERATION;
	rpc->last_ldap_error = LDAP_SUCCESS;
	rpc->last_ldap_errmsg = NULL;
	rpc->supports_ldapv3 = -1;
	rpc->supports_ds40_repl = -1;
	rpc->supports_ds50_repl = -1;
	rpc->supports_dirsync = -1;
	rpc->is_win2k3 = -1;
	rpc->linger_active = PR_FALSE;
	rpc->delete_after_linger = PR_FALSE;
	rpc->linger_event = NULL;
	rpc->linger_time = DEFAULT_LINGER_TIME;
	rpc->status = STATUS_DISCONNECTED;
	rpc->agmt = agmt;
	rpc->refcnt = 1;
	rpc->timeout.tv_sec = agmt_get_timeout(agmt);
	rpc->timeout.tv_usec = 0;
	rpc->flag_agmt_changed = 0;
	rpc->plain = NULL;
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_new\n", 0, 0, 0 );
	return rpc;
loser:
	windows_conn_delete(rpc);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_new - loser\n", 0, 0, 0 );
	return NULL;
}


/*
 * Return PR_TRUE if the connection is in the connected state
 */
static PRBool
windows_conn_connected(Repl_Connection *conn)
{
	PRBool return_value;
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_connected\n", 0, 0, 0 );
	PR_Lock(conn->lock);
	return_value = STATE_CONNECTED == conn->state;
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_connected\n", 0, 0, 0 );
	return return_value;
}


/*
 * Destroy a connection object.
 */
static void
windows_conn_delete_internal(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_delete_internal\n", 0, 0, 0 );
	PR_ASSERT(NULL != conn);
	close_connection_internal(conn);
	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free((void **)&conn->hostname);
	slapi_ch_free((void **)&conn->binddn);
	slapi_ch_free((void **)&conn->plain);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_delete_internal\n", 0, 0, 0 );
}

/*
 * Destroy a connection. It is an error to use the connection object
 * after windows_conn_delete() has been called.
 */
void
windows_conn_delete(Repl_Connection *conn)
{
	PRBool destroy_it = PR_FALSE;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_delete\n", 0, 0, 0 );

	PR_ASSERT(NULL != conn);
	PR_Lock(conn->lock);
	if (conn->linger_active)
	{
		if (slapi_eq_cancel(conn->linger_event) == 1)
		{
			/* Event was found and cancelled. Destroy the connection object. */
			PR_Unlock(conn->lock);
			destroy_it = PR_TRUE;
		}
		else
		{
			/*
			 * The event wasn't found, but we think it's still active.
			 * That means an event is in the process of being fired
			 * off, so arrange for the event to destroy the object .
			 */
			conn->delete_after_linger = PR_TRUE;
			PR_Unlock(conn->lock);
		}
	}
	if (destroy_it)
	{
		windows_conn_delete_internal(conn);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_delete\n", 0, 0, 0 );
}


/*
 * Return the last operation type processed by the connection
 * object, and the LDAP error encountered.
 */
void
windows_conn_get_error(Repl_Connection *conn, int *operation, int *error)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_get_error\n", 0, 0, 0 );
	PR_Lock(conn->lock);
	*operation = conn->last_operation;
	*error = conn->last_ldap_error;
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_get_error\n", 0, 0, 0 );
}

void
windows_conn_set_error(Repl_Connection *conn, int error)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_set_error\n", 0, 0, 0 );
	PR_Lock(conn->lock);
	conn->last_ldap_error = error;
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_set_error\n", 0, 0, 0 );
}

/*
 * Common code to send an LDAPv3 operation and collect the result.
 * Return values:
 * CONN_OPERATION_SUCCESS - the operation succeeded
 * CONN_OPERATION_FAILED - the operation was sent to the consumer
 * and failed. Use conn_get_error() to determine the LDAP error
 * code.
 * CONN_NOT_CONNECTED - no connection is active. The caller should
 * use conn_connect() to connect to the replica and bind, then should
 * reacquire the replica (if needed).
 * CONN_BUSY - the server is busy with previous requests, must wait for a while 
 * before retrying
 */
static ConnResult
windows_perform_operation(Repl_Connection *conn, int optype, const char *dn,
	LDAPMod **attrs, const char *newrdn, const char *newparent,
	int deleteoldrdn, LDAPControl **server_controls,
	const char *extop_oid, struct berval *extop_payload, char **retoidp,
	struct berval **retdatap, LDAPControl ***returned_controls)
{
	int rc = -1;
	ConnResult return_value;
	LDAPControl **loc_returned_controls;
	const char *op_string = NULL;
	const char *extra_op_string = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_perform_operation\n", 0, 0, 0 );

	if (windows_conn_connected(conn))
	{
		int msgid = -2; /* should match no messages */

		conn->last_operation = optype;
		switch (optype)
		{
		case CONN_ADD:
			conn->status = STATUS_PROCESSING_ADD;
			op_string = "add";
			rc = ldap_add_ext(conn->ld, dn, attrs, server_controls,
				NULL /* clientctls */,  &msgid);
			break;
		case CONN_MODIFY:
			conn->status = STATUS_PROCESSING_MODIFY;
			op_string = "modify";
			rc = ldap_modify_ext(conn->ld, dn, attrs, server_controls,
				NULL /* clientctls */,  &msgid);
			break;
		case CONN_DELETE:
			conn->status = STATUS_PROCESSING_DELETE;
			op_string = "delete";
			rc = ldap_delete_ext(conn->ld, dn, server_controls,
				NULL /* clientctls */, &msgid);
			break;
		case CONN_RENAME:
			conn->status = STATUS_PROCESSING_RENAME;
			op_string = "rename";
			rc = ldap_rename(conn->ld, dn, newrdn, newparent, deleteoldrdn,
				server_controls, NULL /* clientctls */, &msgid);
			break;
		case CONN_EXTENDED_OPERATION:
			conn->status = STATUS_PROCESSING_EXTENDED_OPERATION;
			op_string = "extended";
			extra_op_string = extop_oid;
			rc = ldap_extended_operation(conn->ld, extop_oid, extop_payload,
				server_controls, NULL /* clientctls */, &msgid);
		}
		if (LDAP_SUCCESS == rc)
		{
			LDAPMessage *res = NULL;
			int setlevel = 0;
			Slapi_Eq_Context eqctx = repl5_start_debug_timeout(&setlevel);

			rc = ldap_result(conn->ld, msgid, 1, &conn->timeout, &res);
			repl5_stop_debug_timeout(eqctx, &setlevel);
			if (0 == rc)
			{
				/* Timeout */
				rc = slapi_ldap_get_lderrno(conn->ld, NULL, NULL);
				conn->last_ldap_error = LDAP_TIMEOUT;
				return_value = CONN_TIMEOUT;
			}
			else if (-1 == rc)
			{
				/* Error */
				char *s = NULL;
		
				rc = slapi_ldap_get_lderrno(conn->ld, NULL, &s);
                slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Received error %d: %s for %s operation\n",
						agmt_get_long_name(conn->agmt),
						rc, s ? s : "NULL",
						op_string ? op_string : "NULL");
				conn->last_ldap_error = rc;
				/* some errors will require a disconnect and retry the connection
				   later */
				if (IS_DISCONNECT_ERROR(rc))
				{
					windows_conn_disconnect(conn);
					return_value = CONN_NOT_CONNECTED;
				}
				else
				{
					conn->status = STATUS_CONNECTED;
					return_value = CONN_OPERATION_FAILED;
				}
			}
			else
			{
				int err;
				char *errmsg = NULL;
				char **referrals = NULL;
				char *matched = NULL;
                char *ptr;

				rc = ldap_parse_result(conn->ld, res, &err, &matched,
									   &errmsg, &referrals, &loc_returned_controls,
									   0 /* Don't free the result */);
				if (IS_DISCONNECT_ERROR(rc))
				{
					conn->last_ldap_error = rc;
					windows_conn_disconnect(conn);
					return_value = CONN_NOT_CONNECTED;
				}
				else if (IS_DISCONNECT_ERROR(err))
				{
					conn->last_ldap_error = err;
					windows_conn_disconnect(conn);
					return_value = CONN_NOT_CONNECTED;
				} 
				else if (err == LDAP_UNWILLING_TO_PERFORM && optype == CONN_MODIFY) 
				{
					/* this permits password updates to fail gracefully */
					conn->last_ldap_error = LDAP_SUCCESS;
					return_value = CONN_OPERATION_SUCCESS;
				} 
				else if (err == LDAP_ALREADY_EXISTS && optype == CONN_ADD)
				{
					conn->last_ldap_error = LDAP_SUCCESS;
					return_value = CONN_OPERATION_SUCCESS;
				}
				else if (err == LDAP_NO_SUCH_OBJECT && optype == CONN_DELETE)
				{
					conn->last_ldap_error = LDAP_SUCCESS;
					return_value = CONN_OPERATION_SUCCESS;
				}
				else /* regular operation, result returned */
				{
					if (NULL != returned_controls)
					{
						*returned_controls = loc_returned_controls;
					}
					if (LDAP_SUCCESS != rc)
					{
						conn->last_ldap_error = rc;
					}
					else
					{
						conn->last_ldap_error = err;
					}
					return_value = LDAP_SUCCESS == conn->last_ldap_error ? CONN_OPERATION_SUCCESS : CONN_OPERATION_FAILED;
				}
                /* remove extra newlines from AD error message */
                for (ptr = errmsg; ptr && *ptr; ++ptr) {
                    if ((*ptr == '\n') || (*ptr == '\r')) {
                        *ptr = ' ';
                    }
                }
                /* handle special case of constraint violation - give admin
                   enough information to allow them to fix the problem
                   and retry - bug 170350 */
                if (conn->last_ldap_error == LDAP_CONSTRAINT_VIOLATION) {
                    char ebuf[BUFSIZ];
                    slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Received error [%s] when attempting to %s"
                        " entry [%s]: Please correct the attribute specified "
                        "in the error message.  Refer to the Windows Active "
                        "Directory docs for more information.\n",
						agmt_get_long_name(conn->agmt),
						errmsg, op_string == NULL ? "" : op_string,
						escape_string(dn, ebuf));
                } else {
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: Received result code %d (%s) for %s operation %s%s\n",
						agmt_get_long_name(conn->agmt),
						conn->last_ldap_error, errmsg,
						op_string == NULL ? "" : op_string,
						extra_op_string == NULL ? "" : extra_op_string,
						extra_op_string ==  NULL ? "" : " ");
                }
				/*
				 * XXXggood do I need to free matched, referrals,
				 * anything else? Or can I pass NULL for the args
				 * I'm not interested in?
				 */
				/* Good question! Meanwhile, as RTM aproaches, let's free them... */
				slapi_ch_free((void **) &errmsg);
				slapi_ch_free((void **) &matched);
				charray_free(referrals);
				conn->status = STATUS_CONNECTED;
			}
			if (res) ldap_msgfree(res);
		}
		else
		{
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: Failed to send %s operation: LDAP error %d (%s)\n",
					agmt_get_long_name(conn->agmt),
					op_string ? op_string : "NULL", rc, ldap_err2string(rc));
			conn->last_ldap_error = rc;
			if (IS_DISCONNECT_ERROR(rc))
			{
				windows_conn_disconnect(conn);
				return_value = CONN_NOT_CONNECTED;
			}
			else
			{
				conn->status = STATUS_CONNECTED;
				return_value = CONN_OPERATION_FAILED;
			}
		}
	}
	else
	{
		/* conn->last_ldap_error has been set to a more specific value
		 * in windows_conn_connected()
		 * conn->last_ldap_error = LDAP_SERVER_DOWN;
		 */
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_perform_operation\n", 0, 0, 0 );
	return return_value;
}

/* Copied from the chaining backend*/
static Slapi_Entry * 
windows_LDAPMessage2Entry(Repl_Connection *conn, LDAPMessage * msg, int attrsonly) {

	Slapi_Entry *rawentry = NULL;
	Slapi_Entry *e = NULL;
	char *a = NULL;
	BerElement * ber = NULL;
	LDAP *ld = conn->ld;

	windows_private_set_raw_entry(conn->agmt, NULL); /* clear it first */

	if (msg == NULL) {
		return NULL;
	}
	
	/*
	 * dn not allocated by slapi
	 * attribute type and values ARE allocated
	 */

	e = slapi_entry_alloc();
	if ( e == NULL ) return NULL;
	slapi_entry_set_dn( e, ldap_get_dn( ld, msg ) );
	rawentry = slapi_entry_alloc();
	if ( rawentry == NULL ) {
		slapi_entry_free(e);
		return NULL;
	}
	slapi_entry_set_dn( rawentry, slapi_ch_strdup(slapi_entry_get_dn(e)) );
 
	for ( a = ldap_first_attribute( ld, msg, &ber ); a!=NULL; a=ldap_next_attribute( ld, msg, ber ) ) 
	{
		struct  berval ** aVal = ldap_get_values_len( ld, msg, a);
		slapi_entry_add_values(rawentry, a, aVal);

		if (0 == strcasecmp(a,"dnsRecord") || 0 == strcasecmp(a,"dnsproperty") ||
		    0 == strcasecmp(a,"dscorepropagationdata"))
		{
			/* AD returns us entries with these attributes that we are not interested in, 
			 * but they break the entry attribute code (I think it is looking at null-terminated
			 * string values, but the values are binary here). It appears that AD has some problems
			 * with allowing duplicate values for system-only multi-valued attributes. So we skip
			 * those attributes as a workaround.
			 */
			;
		} else
		{
			if (attrsonly) 
			{
				slapi_entry_add_value(e, a, (Slapi_Value *)NULL);
			} else 
			{
				char *type_to_use = NULL;
				/* Work around the fact that we alias street and streetaddress, while Microsoft do not */
				if (0 == strcasecmp(a,"streetaddress")) 
				{
					type_to_use = FAKE_STREET_ATTR_NAME;
				} else
				{
					type_to_use = a;
				}

				/* If the list of attribute values is null, we need to delete this attribute
				 * from the local entry.
                                 */
				if (aVal == NULL) {
					/* Windows will send us an attribute with no values if it was deleted
					 * on the AD side.  Add this attribute to the deleted attributes list */
					Slapi_Attr *attr = slapi_attr_new();
					slapi_attr_init(attr, type_to_use);
					entry_add_deleted_attribute_wsi(e, attr);
				} else { 
					slapi_entry_add_values( e, type_to_use, aVal);
				} 
                
			}
		}
		ldap_memfree(a);
		ldap_value_free_len(aVal);
	}
    if ( NULL != ber )
	{
        ber_free( ber, 0 );
	}

	windows_private_set_raw_entry(conn->agmt, rawentry); /* windows private now owns rawentry */

    return e;
}

/* Perform a simple search against Windows with no controls */
ConnResult
windows_search_entry(Repl_Connection *conn, char* searchbase, char *filter, Slapi_Entry **entry)
{
	return windows_search_entry_ext(conn, searchbase, filter, entry, NULL);
}

/* Perform a simple search against Windows with optional controls */
ConnResult
windows_search_entry_ext(Repl_Connection *conn, char* searchbase, char *filter, Slapi_Entry **entry, LDAPControl **serverctrls)
{
	ConnResult return_value = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_search_entry\n", 0, 0, 0 );

	*entry = NULL;

	if (windows_conn_connected(conn))
	{
		int ldap_rc = 0;
		LDAPMessage *res = NULL;
		char *searchbase_copy = slapi_ch_strdup(searchbase);
		int scope = LDAP_SCOPE_SUBTREE;
		char *filter_copy = slapi_ch_strdup(filter);
		char **attrs = NULL;
		LDAPControl **serverctrls_copy = NULL;

		slapi_add_controls(&serverctrls_copy, serverctrls, 1 /* make a copy we can free */);

		LDAPDebug( LDAP_DEBUG_REPL, "Calling windows entry search request plugin\n", 0, 0, 0 );

		winsync_plugin_call_pre_ad_search_cb(conn->agmt, NULL, &searchbase_copy, &scope, &filter_copy,
											 &attrs, &serverctrls_copy);
		
		ldap_rc = ldap_search_ext_s(conn->ld, searchbase_copy, scope,
			filter_copy, attrs, 0 /* attrsonly */,
			serverctrls_copy , NULL /* client controls */,
			&conn->timeout, 0 /* sizelimit */, &res);

		slapi_ch_free_string(&searchbase_copy);
		slapi_ch_free_string(&filter_copy);
		slapi_ch_array_free(attrs);
		attrs = NULL;
		ldap_controls_free(serverctrls_copy);
		serverctrls_copy = NULL;

		if (LDAP_SUCCESS == ldap_rc)
		{
			LDAPMessage *message = ldap_first_entry(conn->ld, res);

			if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
				int nummessages = 0;
				int numentries = 0;
				int numreferences = 0;
				nummessages = ldap_count_messages(conn->ld, res);
				numentries = ldap_count_entries(conn->ld, res);
				numreferences = ldap_count_references(conn->ld, res);
				LDAPDebug( LDAP_DEBUG_REPL, "windows_search_entry: received %d messages, %d entries, %d references\n",
                                   nummessages, numentries, numreferences );
			}

			if (NULL != entry)
			{
				*entry = windows_LDAPMessage2Entry(conn,message,0);
			}
			/* See if there are any more entries : if so then that's an error
			 * but we still need to get them to avoid gumming up the connection
			 */
			while (NULL != ( message = ldap_next_entry(conn->ld,message))) ;
			return_value = CONN_OPERATION_SUCCESS;
		}
		else if (IS_DISCONNECT_ERROR(ldap_rc))
		{
			windows_conn_disconnect(conn);
			return_value = CONN_NOT_CONNECTED;
		}
		else
		{
			return_value = CONN_OPERATION_FAILED;
		}
		conn->last_ldap_error = ldap_rc;
		if (NULL != res)
		{
			ldap_msgfree(res);
			res = NULL;
		}
	}
	else
	{
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_search_entry\n", 0, 0, 0 );
	return return_value;
}

ConnResult
send_dirsync_search(Repl_Connection *conn)
{
	ConnResult return_value;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_dirsync_search\n", 0, 0, 0 );
	
	if (windows_conn_connected(conn))
	{
		const char *op_string = NULL;
		int rc;
		int scope = LDAP_SCOPE_SUBTREE;
		char *filter = slapi_ch_strdup("(objectclass=*)");
		char **attrs = NULL;
		LDAPControl **server_controls = NULL;
		int msgid;
		/* need to strip the dn down to dc= */
		const char *old_dn = slapi_sdn_get_ndn( windows_private_get_windows_subtree(conn->agmt) );
		char *dn = slapi_ch_strdup(strstr(old_dn, "dc="));

		if (conn->supports_dirsync == 0)
		{
			/* unsupported */
		} else 
		{
			slapi_add_control_ext(&server_controls,
								  windows_private_dirsync_control(conn->agmt),
								  0 /* no copy - passin */);
		}

		conn->last_operation = CONN_SEARCH;
		conn->status = STATUS_SEARCHING;
		op_string = "search";

		LDAPDebug( LDAP_DEBUG_REPL, "Calling dirsync search request plugin\n", 0, 0, 0 );

		winsync_plugin_call_dirsync_search_params_cb(conn->agmt, old_dn, &dn, &scope, &filter,
													 &attrs, &server_controls);

		LDAPDebug( LDAP_DEBUG_REPL, "Sending dirsync search request\n", 0, 0, 0 );

		rc = ldap_search_ext( conn->ld, dn, scope, filter, attrs, PR_FALSE, server_controls,
                              NULL /* ClientControls */, 0,0, &msgid);

		if (LDAP_SUCCESS == rc)
		{
			return_value = 0;
		}
		else
		{
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: Failed to get %s operation: LDAP error %d (%s)\n",
					agmt_get_long_name(conn->agmt),
					op_string ? op_string : "NULL", rc, ldap_err2string(rc));
			conn->last_ldap_error = rc;
			if (IS_DISCONNECT_ERROR(rc))
			{
				windows_conn_disconnect(conn);
				return_value = CONN_NOT_CONNECTED;
			}
			else
			{
				conn->status = STATUS_CONNECTED;
				return_value = CONN_OPERATION_FAILED;
			}
		}
		/* cleanup */
		slapi_ch_free_string(&dn);
		slapi_ch_free_string(&filter);
		slapi_ch_array_free(attrs);
		attrs = NULL;
		ldap_controls_free(server_controls);
		server_controls = NULL;
	}
	else
	{
		/* conn->last_ldap_error has been set to a more specific value
		 * in windows_conn_connected()
		 * conn->last_ldap_error = LDAP_SERVER_DOWN;
		 */
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_dirsync_search\n", 0, 0, 0 );
	return return_value;
}


/*
 * Send an LDAP add operation.
 */
ConnResult
windows_conn_send_add(Repl_Connection *conn, const char *dn, LDAPMod **attrs,
	LDAPControl **server_controls, LDAPControl ***returned_controls)
{
	ConnResult res = 0;
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_send_add\n", 0, 0, 0 );
	res = windows_perform_operation(conn, CONN_ADD, dn, attrs, NULL /* newrdn */,
		NULL /* newparent */, 0 /* deleteoldrdn */, server_controls,
		NULL /* extop OID */, NULL /* extop payload */, NULL /* retoidp */,
		NULL /* retdatap */, returned_controls);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_send_add\n", 0, 0, 0 );
	return res;
}


/*
 * Send an LDAP delete operation.
 */
ConnResult
windows_conn_send_delete(Repl_Connection *conn, const char *dn,
	LDAPControl **server_controls, LDAPControl ***returned_controls)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_send_delete\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_send_delete\n", 0, 0, 0 );
	return windows_perform_operation(conn, CONN_DELETE, dn, NULL /* attrs */,
		NULL /* newrdn */, NULL /* newparent */, 0 /* deleteoldrdn */,
		server_controls, NULL /* extop OID */, NULL /* extop payload */,
		NULL /* retoidp */, NULL /* retdatap */, returned_controls);
}


/*
 * Send an LDAP modify operation.
 */
ConnResult
windows_conn_send_modify(Repl_Connection *conn, const char *dn, LDAPMod **mods,
	LDAPControl **server_controls, LDAPControl ***returned_controls)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_send_modify\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_send_modify\n", 0, 0, 0 );
	return windows_perform_operation(conn, CONN_MODIFY, dn, mods, NULL /* newrdn */,
		NULL /* newparent */, 0 /* deleteoldrdn */, server_controls,
		NULL /* extop OID */, NULL /* extop payload */, NULL /* retoidp */,
		NULL /* retdatap */, returned_controls);
}

/*
 * Send an LDAP moddn operation.
 */
ConnResult
windows_conn_send_rename(Repl_Connection *conn, const char *dn,
	const char *newrdn, const char *newparent, int deleteoldrdn,
	LDAPControl **server_controls, LDAPControl ***returned_controls)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_send_rename\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_send_rename\n", 0, 0, 0 );
	return windows_perform_operation(conn, CONN_RENAME, dn, NULL /* attrs */,
		newrdn, newparent, deleteoldrdn, server_controls,
		NULL /* extop OID */, NULL /* extop payload */, NULL /* retoidp */,
		NULL /* retdatap */, returned_controls);
}

/*
 * Send an LDAP search operation.
 */

Slapi_Entry * windows_conn_get_search_result(Repl_Connection *conn)
{
	int rc=0;
	LDAPMessage *res = NULL;
	Slapi_Entry *e = NULL;
	LDAPMessage *lm = NULL;
	char *dn = "";
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_get_search_result\n", 0, 0, 0 );

	if (windows_conn_connected(conn))
	{
		rc = ldap_result( conn->ld, LDAP_RES_ANY, 0, &conn->timeout, &res );
		switch (rc) {
		case 0:
		case -1:
		case LDAP_RES_SEARCH_REFERENCE:
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "error in windows_conn_get_search_result, rc=%d\n", rc);
			break;
		case LDAP_RES_SEARCH_RESULT:
			{
				LDAPControl **returned_controls = NULL;
				int code = 0;
				/* Purify says this is a leak : */
				if (LDAP_SUCCESS != (rc = ldap_parse_result( conn->ld, res, &code,  NULL, NULL,  NULL, &returned_controls, 0 ))) {
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
									"error reading search result in windows_conn_get_search_result, rc=%d:%s\n", rc, ldap_err2string(rc));
				}
				if (returned_controls)
				{
					windows_private_update_dirsync_control(conn->agmt, returned_controls);
					ldap_controls_free(returned_controls);
				}
				if (windows_private_dirsync_has_more(conn->agmt)) {
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"received hasmore from dirsync\n");
				}
			} 
			break;
		case LDAP_RES_SEARCH_ENTRY:
			{
				if (( dn = ldap_get_dn( conn->ld, res )) != NULL ) 
				{
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"received entry from dirsync: %s\n", dn);
					lm = ldap_first_entry( conn->ld, res );			
					e = windows_LDAPMessage2Entry(conn,lm,0);
					ldap_memfree(dn);
				}
			}
			break;

		} /* switch */
	} /* if */ 

	if (res) 
	{
		ldap_msgfree( res );
		res = NULL;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_get_search_result\n", 0, 0, 0 );
	return e;
}


/*
 * Send an LDAP extended operation.
 */
ConnResult
windows_conn_send_extended_operation(Repl_Connection *conn, const char *extop_oid,
	struct berval *payload, char **retoidp, struct berval **retdatap,
	LDAPControl **server_controls, LDAPControl ***returned_controls)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_send_extended_operation\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_send_extended_operation\n", 0, 0, 0 );
	return windows_perform_operation(conn, CONN_EXTENDED_OPERATION, NULL /* dn */, NULL /* attrs */,
		NULL /* newrdn */, NULL /* newparent */,  0 /* deleteoldrdn */,
		server_controls, extop_oid, payload, retoidp, retdatap,
		returned_controls);
}


/*
 * Synchronously read an entry and return a specific attribute's values.
 * Returns CONN_OPERATION_SUCCESS if successful. Returns
 * CONN_OPERATION_FAILED if the operation was sent but an LDAP error
 * occurred (conn->last_ldap_error is set in this case), and
 * CONN_NOT_CONNECTED if no connection was active.
 *
 * The caller must free the returned_bvals.
 */
ConnResult
windows_conn_read_entry_attribute(Repl_Connection *conn, const char *dn,
	char *type, struct berval ***returned_bvals)
{
	ConnResult return_value;
	int ldap_rc;
	LDAPControl *server_controls[2];
	LDAPMessage *res = NULL;
	char *attrs[2];

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_read_entry_attribute\n", 0, 0, 0 );

	PR_ASSERT(NULL != type);
	if (windows_conn_connected(conn))
	{
		server_controls[0] = &manageDSAITControl;
		server_controls[1] = NULL;
		attrs[0] = type;
		attrs[1] = NULL;
		ldap_rc = ldap_search_ext_s(conn->ld, dn, LDAP_SCOPE_BASE,
			"(objectclass=*)", attrs, 0 /* attrsonly */,
			server_controls, NULL /* client controls */,
			&conn->timeout, 0 /* sizelimit */, &res);
		if (LDAP_SUCCESS == ldap_rc)
		{
			LDAPMessage *entry = ldap_first_entry(conn->ld, res);
			if (NULL != entry)
			{
				*returned_bvals = ldap_get_values_len(conn->ld, entry, type);
			}
			return_value = CONN_OPERATION_SUCCESS;
		}
		else if (IS_DISCONNECT_ERROR(ldap_rc))
		{
			windows_conn_disconnect(conn);
			return_value = CONN_NOT_CONNECTED;
		}
		else
		{
			return_value = CONN_OPERATION_FAILED;
		}
		conn->last_ldap_error = ldap_rc;
		if (NULL != res)
		{
			ldap_msgfree(res);
			res = NULL;
		}
	}
	else
	{
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_read_entry_attribute\n", 0, 0, 0 );
	return return_value;
}


/*
 * Return an pointer to a string describing the connection's status.
*/

const char *
windows_conn_get_status(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_get_status\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_get_status\n", 0, 0, 0 );
	return conn->status;
}



/*
 * Cancel any outstanding linger timer. Should be called when
 * a replication session is beginning.
 */
void
windows_conn_cancel_linger(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_cancel_linger\n", 0, 0, 0 );
	PR_ASSERT(NULL != conn);
	PR_Lock(conn->lock);
	if (conn->linger_active)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: Cancelling linger on the connection\n",
			agmt_get_long_name(conn->agmt));
		conn->linger_active = PR_FALSE;
		if (slapi_eq_cancel(conn->linger_event) == 1)
		{
			conn->refcnt--;
		}
		conn->linger_event = NULL;
		conn->status = STATUS_CONNECTED;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: No linger to cancel on the connection\n",
			agmt_get_long_name(conn->agmt));
	}
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_cancel_linger\n", 0, 0, 0 );
}


/*
 * Called when our linger timeout timer expires. This means
 * we should check to see if perhaps the connection's become
 * active again, in which case we do nothing. Otherwise,
 * we close the connection.
 */
static void
linger_timeout(time_t event_time, void *arg)
{
	PRBool delete_now;
	Repl_Connection *conn = (Repl_Connection *)arg;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> linger_timeout\n", 0, 0, 0 );

	PR_ASSERT(NULL != conn);
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: Linger timeout has expired on the connection\n",
		agmt_get_long_name(conn->agmt));
	PR_Lock(conn->lock);
	if (conn->linger_active)
	{
		conn->linger_active = PR_FALSE;
		conn->linger_event = NULL;
		close_connection_internal(conn);
	}
	delete_now = conn->delete_after_linger;
	PR_Unlock(conn->lock);
	if (delete_now)
	{
		windows_conn_delete_internal(conn);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= linger_timeout\n", 0, 0, 0 );
}


/*
 * Indicate that a session is ending. The linger timer starts when
 * this function is called.
 */
void
windows_conn_start_linger(Repl_Connection *conn)
{
	time_t now;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_start_linger\n", 0, 0, 0 );

	PR_ASSERT(NULL != conn);
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: Beginning linger on the connection\n",
		agmt_get_long_name(conn->agmt));
	if (!windows_conn_connected(conn))
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: No linger on the closed conn\n",
			agmt_get_long_name(conn->agmt));
		return;
	}
	time(&now);
	PR_Lock(conn->lock);
	if (conn->linger_active)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: Linger already active on the connection\n",
			agmt_get_long_name(conn->agmt));
	}
	else
	{
		conn->linger_active = PR_TRUE;
		conn->linger_event = slapi_eq_once(linger_timeout, conn, now + conn->linger_time);
		conn->status = STATUS_LINGERING;
	}
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_start_linger\n", 0, 0, 0 );
}



/*
 * If no connection is currently active, opens a connection and binds to
 * the remote server. If a connection is open (e.g. lingering) then
 * this is a no-op.
 *
 * Returns CONN_OPERATION_SUCCESS on success, or CONN_OPERATION_FAILED
 * on failure. Sets conn->last_ldap_error and conn->last_operation;
 */
ConnResult
windows_conn_connect(Repl_Connection *conn)
{
	int optdata;
	int secure = 0;
	char* binddn = NULL;
	struct berval *creds;
	ConnResult return_value = CONN_OPERATION_SUCCESS;
	int pw_ret = 1;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_connect\n", 0, 0, 0 );

	/** Connection already open just return SUCCESS **/
	if(conn->state == STATE_CONNECTED) {
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_connect\n", 0, 0, 0 );
		return return_value;
	}

	PR_Lock(conn->lock);
	if (conn->flag_agmt_changed) {
		/* So far we cannot change Hostname and Port */
		/* slapi_ch_free((void **)&conn->hostname); */
		/* conn->hostname = agmt_get_hostname(conn->agmt); */
		/* conn->port = agmt_get_port(conn->agmt); */
		slapi_ch_free((void **)&conn->binddn);
		conn->binddn = agmt_get_binddn(conn->agmt);
		conn->bindmethod = agmt_get_bindmethod(conn->agmt);
		conn->transport_flags = agmt_get_transport_flags(conn->agmt);
		conn->timeout.tv_sec = agmt_get_timeout(conn->agmt);
		conn->flag_agmt_changed = 0;
		slapi_ch_free((void **)&conn->plain);
	}
	PR_Unlock(conn->lock);

	creds = agmt_get_credentials(conn->agmt);

	if (conn->plain == NULL) {

		char *plain = NULL;
		
		/* kexcoff: for reversible encryption */
		/* We need to test the return code of pw_rever_decode in order to decide
		 * if a free for plain will be needed (pw_ret == 0) or not (pw_ret != 0) */
		pw_ret = pw_rever_decode(creds->bv_val, &plain, type_nsds5ReplicaCredentials);
		/* Pb occured in decryption: stop now, binding will fail */
		if ( pw_ret == -1 )
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: Decoding of the credentials failed.\n",
				agmt_get_long_name(conn->agmt));
		
			return_value = CONN_OPERATION_FAILED;
			conn->last_ldap_error = LDAP_INVALID_CREDENTIALS;
			conn->state = STATE_DISCONNECTED;
			LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_connect\n", 0, 0, 0 );
			return (return_value);
		} /* Else, does not mean that the plain is correct, only means the we had no internal
		   decoding pb */
		conn->plain = slapi_ch_strdup (plain);
		if (!pw_ret) slapi_ch_free((void**)&plain);
	}
 
	

	/* ugaston: if SSL has been selected in the replication agreement, SSL client
	 * initialisation should be done before ever trying to open any connection at all.
	 */
	if (conn->transport_flags == TRANSPORT_FLAG_TLS) {
        secure = 2;
    } else if (conn->transport_flags == TRANSPORT_FLAG_SSL) {
        secure = 1;
    }
 
	if (secure > 0) {
		if (!NSS_IsInitialized()) {
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"%s: SSL Not Initialized, Replication over SSL FAILED\n",
							agmt_get_long_name(conn->agmt));
			conn->last_ldap_error = LDAP_INAPPROPRIATE_AUTH;
			conn->last_operation = CONN_INIT;
			ber_bvfree(creds);
			creds = NULL;
			return CONN_SSL_NOT_ENABLED;
		}
	}

	if (return_value == CONN_OPERATION_SUCCESS) {
#if !defined(USE_OPENLDAP)
		int io_timeout_ms;
#endif
		/* Now we initialize the LDAP Structure and set options */
		
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: Trying %s%s slapi_ldap_init_ext\n",
			agmt_get_long_name(conn->agmt),
			secure ? "secure" : "non-secure",
			(secure == 2) ? " startTLS" : "");
		
		conn->ld = slapi_ldap_init_ext(NULL, conn->hostname, conn->port, secure, 0, NULL);
		if (NULL == conn->ld)
		{
			return_value = CONN_OPERATION_FAILED;
			conn->state = STATE_DISCONNECTED;
			conn->last_operation = CONN_INIT;
			conn->last_ldap_error = LDAP_LOCAL_ERROR;
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: Failed to establish %s%sconnection to the consumer\n",
				agmt_get_long_name(conn->agmt),
				secure ? "secure " : "",
				(secure == 2) ? "startTLS " : "");
			ber_bvfree(creds);
			creds = NULL;
			LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_connect\n", 0, 0, 0 );
			return return_value;
		}
		
		/* slapi_ch_strdup is OK with NULL strings */
		binddn = slapi_ch_strdup(conn->binddn);

		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: binddn = %s,  passwd = %s\n",
			agmt_get_long_name(conn->agmt),
			binddn?binddn:"NULL", creds->bv_val?creds->bv_val:"NULL");
		
		/* Set some options for the connection. */
		optdata = LDAP_DEREF_NEVER; /* Don't dereference aliases */
		ldap_set_option(conn->ld, LDAP_OPT_DEREF, &optdata);
		
		optdata = LDAP_VERSION3; /* We need LDAP version 3 */
		ldap_set_option(conn->ld, LDAP_OPT_PROTOCOL_VERSION, &optdata);
		
		/* Don't chase any referrals (although we shouldn't get any) */
		ldap_set_option(conn->ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

		/* override the default timeout with the specified timeout */
#if defined(USE_OPENLDAP)
		ldap_set_option(conn->ld, LDAP_OPT_NETWORK_TIMEOUT, &conn->timeout);
#else
		io_timeout_ms = conn->timeout.tv_sec * 1000 + conn->timeout.tv_usec / 1000;
		prldap_set_session_option(conn->ld, NULL, PRLDAP_OPT_IO_MAX_TIMEOUT,
								  io_timeout_ms);
#endif
		/* We've got an ld. Now bind to the server. */
		conn->last_operation = CONN_BIND;
		
	}

	if ( bind_and_check_pwp(conn, binddn, conn->plain) == CONN_OPERATION_FAILED )
	{
		conn->last_ldap_error = slapi_ldap_get_lderrno (conn->ld, NULL, NULL);
		conn->state = STATE_DISCONNECTED;
		return_value = CONN_OPERATION_FAILED;
	}
	else
	{
		conn->last_ldap_error = LDAP_SUCCESS;
		conn->state = STATE_CONNECTED;
		return_value = CONN_OPERATION_SUCCESS;
	}
	
	{
		ConnResult supports = 0;
		supports = windows_conn_replica_supports_dirsync(conn);
		if (CONN_DOES_NOT_SUPPORT_DIRSYNC == supports)
		{
			/* We assume that a server that doesn't support dirsync is our NT4 LDAP service */
			windows_private_set_isnt4(conn->agmt,1);
			LDAPDebug( LDAP_DEBUG_REPL, "windows_conn_connect : detected NT4 peer\n", 0, 0, 0 );
		} else 
		{
			windows_private_set_isnt4(conn->agmt,0);
		}

		supports = windows_conn_replica_is_win2k3(conn);
		if (CONN_IS_WIN2K3 == supports)
		{
			windows_private_set_iswin2k3(conn->agmt,1);
			LDAPDebug( LDAP_DEBUG_REPL, "windows_conn_connect : detected Win2k3 peer\n", 0, 0, 0 );
		} else
		{
			windows_private_set_iswin2k3(conn->agmt,0);
		}
	}

	ber_bvfree(creds);
	creds = NULL;

	slapi_ch_free((void**)&binddn);

	if(return_value == CONN_OPERATION_FAILED)
	{
		close_connection_internal(conn);
	} else
	{
		conn->last_ldap_error = LDAP_SUCCESS;
		conn->state = STATE_CONNECTED;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_connect\n", 0, 0, 0 );
	return return_value;
}


static void
close_connection_internal(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> close_connection_internal\n", 0, 0, 0 );

	if (NULL != conn->ld)
	{
		/* Since we call slapi_ldap_init, 
		   we must call slapi_ldap_unbind */
		slapi_ldap_unbind(conn->ld);
	}
	conn->ld = NULL;
	conn->state = STATE_DISCONNECTED;
	conn->status = STATUS_DISCONNECTED;
	conn->supports_ds50_repl = -1;
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: Disconnected from the consumer\n", agmt_get_long_name(conn->agmt));
	LDAPDebug( LDAP_DEBUG_TRACE, "<= close_connection_internal\n", 0, 0, 0 );
}

void
windows_conn_disconnect(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_disconnect\n", 0, 0, 0 );
	PR_ASSERT(NULL != conn);
	PR_Lock(conn->lock);
	close_connection_internal(conn);
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_disconnect\n", 0, 0, 0 );
}


/*
 * Determine if the remote replica supports DS 5.0 replication.
 * Return codes:
 * CONN_SUPPORTS_DS5_REPL - the remote replica suport DS5 replication
 * CONN_DOES_NOT_SUPPORT_DS5_REPL - the remote replica does not
 * support DS5 replication.
 * CONN_OPERATION_FAILED - it could not be determined if the remote
 * replica supports DS5 replication.
 * CONN_NOT_CONNECTED - no connection was active.
 */
ConnResult
windows_conn_replica_supports_ds5_repl(Repl_Connection *conn)
{
	ConnResult return_value;
	int ldap_rc;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_replica_supports_ds5_repl\n", 0, 0, 0 );

	if (windows_conn_connected(conn))
	{
		if (conn->supports_ds50_repl == -1) {
			LDAPMessage *res = NULL;
			LDAPMessage *entry = NULL;
			char *attrs[] = {"supportedcontrol", "supportedextension", NULL};
			
			conn->status = STATUS_SEARCHING;
			ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
										"(objectclass=*)", attrs, 0 /* attrsonly */,
										NULL /* server controls */, NULL /* client controls */,
										&conn->timeout, LDAP_NO_LIMIT, &res);
			if (LDAP_SUCCESS == ldap_rc)
			{
				conn->supports_ds50_repl = 0;
				entry = ldap_first_entry(conn->ld, res);
				if (!attribute_string_value_present(conn->ld, entry, "supportedcontrol", REPL_NSDS50_UPDATE_INFO_CONTROL_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
				}
				else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_START_NSDS50_REPLICATION_REQUEST_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
				}
				else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_END_NSDS50_REPLICATION_REQUEST_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
				}
				else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
				}
				else if (!attribute_string_value_present(conn->ld, entry, "supportedextension", REPL_NSDS50_REPLICATION_RESPONSE_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DS5_REPL;
				}
				else
				{
					conn->supports_ds50_repl = 1;
					return_value = CONN_SUPPORTS_DS5_REPL;
				}
			}
			else
			{
				if (IS_DISCONNECT_ERROR(ldap_rc))
				{
					conn->last_ldap_error = ldap_rc;	/* specific reason */
					windows_conn_disconnect(conn);
					return_value = CONN_NOT_CONNECTED;
				}
				else
				{
					return_value = CONN_OPERATION_FAILED;
				}
			}
			if (NULL != res)
				ldap_msgfree(res);
		}
		else {
			return_value = conn->supports_ds50_repl ? CONN_SUPPORTS_DS5_REPL : CONN_DOES_NOT_SUPPORT_DS5_REPL;
		}
	}
	else
	{
		/* Not connected */
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_replica_supports_ds5_repl\n", 0, 0, 0 );
	return return_value;
}


ConnResult
windows_conn_replica_supports_dirsync(Repl_Connection *conn)
{
	ConnResult return_value;
	int ldap_rc;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_replica_supports_dirsync\n", 0, 0, 0 );

	if (getenv("WINSYNC_USE_DS")) {
		/* used to fake out dirsync to think it's talking to a real ad when in fact
		   it's just talking to another directory server */
		conn->supports_dirsync = 1;
		return CONN_SUPPORTS_DIRSYNC;
	}

	if (windows_conn_connected(conn))
	{
		if (conn->supports_dirsync == -1) {
			LDAPMessage *res = NULL;
			LDAPMessage *entry = NULL;
			char *attrs[] = {"supportedcontrol", NULL};
			
			conn->status = STATUS_SEARCHING;
			ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
										"(objectclass=*)", attrs, 0 /* attrsonly */,
										NULL /* server controls */, NULL /* client controls */,
										&conn->timeout, LDAP_NO_LIMIT, &res);
			if (LDAP_SUCCESS == ldap_rc)
			{
				conn->supports_dirsync = 0;
				entry = ldap_first_entry(conn->ld, res);
				if (!attribute_string_value_present(conn->ld, entry, "supportedcontrol", REPL_DIRSYNC_CONTROL_OID))
				{
					return_value = CONN_DOES_NOT_SUPPORT_DIRSYNC;
				}
				else
				{
				 
					conn->supports_dirsync =1;
					return_value = CONN_SUPPORTS_DIRSYNC;
				}
			}
			else
			{
				if (IS_DISCONNECT_ERROR(ldap_rc))
				{
					conn->last_ldap_error = ldap_rc;	/* specific reason */
					windows_conn_disconnect(conn);
					return_value = CONN_NOT_CONNECTED;
				}
				else
				{
					return_value = CONN_OPERATION_FAILED;
				}
			}
			if (NULL != res)
				ldap_msgfree(res);
		}
		else {
			return_value = conn->supports_dirsync ? CONN_SUPPORTS_DIRSYNC : CONN_DOES_NOT_SUPPORT_DIRSYNC;
		}
	}
	else
	{
		/* Not connected */
		return_value = CONN_NOT_CONNECTED;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_replica_supports_dirsync\n", 0, 0, 0 );
	return return_value;
}


/* Checks if the AD server is running win2k3 (or later) */
ConnResult
windows_conn_replica_is_win2k3(Repl_Connection *conn)
{
        ConnResult return_value;
        int ldap_rc;

        LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_replica_is_win2k3\n", 0, 0, 0 );

        if (windows_conn_connected(conn))
        {
                if (conn->is_win2k3 == -1) {
                        LDAPMessage *res = NULL;
                        LDAPMessage *entry = NULL;
                        char *attrs[] = {"supportedCapabilities", NULL};

                        conn->status = STATUS_SEARCHING;
                        ldap_rc = ldap_search_ext_s(conn->ld, "", LDAP_SCOPE_BASE,
                                                                                "(objectclass=*)", attrs, 0 /* attrsonly */,
                                                                                NULL /* server controls */, NULL /* client controls */,
                                                                                &conn->timeout, LDAP_NO_LIMIT, &res);
                        if (LDAP_SUCCESS == ldap_rc)
                        {
                                conn->is_win2k3 = 0;
                                entry = ldap_first_entry(conn->ld, res);
                                if (!attribute_string_value_present(conn->ld, entry, "supportedCapabilities", REPL_WIN2K3_AD_OID))
                                {
                                        return_value = CONN_NOT_WIN2K3;
                                }
                                else
                                {

                                        conn->is_win2k3 =1;
                                        return_value = CONN_IS_WIN2K3;
                                }
                        }
                        else
                        {
                                if (IS_DISCONNECT_ERROR(ldap_rc))
                                {
                                        conn->last_ldap_error = ldap_rc;        /* specific reason */
                                        windows_conn_disconnect(conn);
                                        return_value = CONN_NOT_CONNECTED;
                                }
                                else
                                {
                                        return_value = CONN_OPERATION_FAILED;
                                }
                        }
                        if (NULL != res)
                                ldap_msgfree(res);
                        }
                else {
                        return_value = conn->is_win2k3 ? CONN_IS_WIN2K3 : CONN_NOT_WIN2K3;
                }
        }
        else
        {
                /* Not connected */
                return_value = CONN_NOT_CONNECTED;
        }
        LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_replica_is_win2k3\n", 0, 0, 0 );
        return return_value;
}


/*
 * Return 1 if "value" is a value of attribute type "type" in entry "entry".
 * Otherwise, return 0.
 */
static int
attribute_string_value_present(LDAP *ld, LDAPMessage *entry, const char *type,
	const char *value)
{
	int return_value = 0;
	ber_len_t vallen;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> attribute_string_value_present\n", 0, 0, 0 );

	if (NULL != entry)
	{
		char *atype = NULL;
		BerElement *ber = NULL;

		vallen = strlen(value);
		atype = ldap_first_attribute(ld, entry, &ber);
		while (NULL != atype && 0 == return_value)
		{
			if (strcasecmp(atype, type) == 0)
			{
				struct berval **vals = ldap_get_values_len(ld, entry, atype);
				int i;
				for (i = 0; return_value == 0 && NULL != vals && NULL != vals[i]; i++)
				{
					if ((vallen == vals[i]->bv_len) && !strncmp(vals[i]->bv_val, value, vallen))
					{
						return_value = 1;
					}
				}
				if (NULL != vals)
				{
					ldap_value_free_len(vals);
				}
			}
			ldap_memfree(atype);
			atype = ldap_next_attribute(ld, entry, ber);
		}
		if (NULL != ber)
			ber_free(ber, 0);
		/* The last atype has not been freed yet */
		if (NULL != atype)
			ldap_memfree(atype);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= attribute_string_value_present\n", 0, 0, 0 );
	return return_value;
}




/*
 * Read the remote server's schema entry, then read the local schema entry,
 * and compare the nsschemacsn attribute. If the local csn is newer, or
 * the remote csn is absent, push the schema down to the consumer.
 * Return codes:
 * CONN_SCHEMA_UPDATED if the schema was pushed successfully
 * CONN_SCHEMA_NO_UPDATE_NEEDED if the schema was as new or newer than
 * the local server's schema
 * CONN_OPERATION_FAILED if an error occurred
 * CONN_NOT_CONNECTED if no connection was active
 * NOTE: Should only be called when a replication session has been
 * established by sending a startReplication extended operation.
 */

void
windows_conn_set_timeout(Repl_Connection *conn, long timeout)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_set_timeout\n", 0, 0, 0 );
	PR_ASSERT(NULL != conn);
	PR_ASSERT(timeout >= 0);
	PR_Lock(conn->lock);
	conn->timeout.tv_sec = timeout;
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_set_timeout\n", 0, 0, 0 );
}

void windows_conn_set_agmt_changed(Repl_Connection *conn)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_conn_set_agmt_changed\n", 0, 0, 0 );
	PR_ASSERT(NULL != conn);
	PR_Lock(conn->lock);
	if (NULL != conn->agmt)
		conn->flag_agmt_changed = 1;
	PR_Unlock(conn->lock);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_conn_set_agmt_changed\n", 0, 0, 0 );
}

static const char *
bind_method_to_mech(int bindmethod)
{
	switch (bindmethod) {
	case BINDMETHOD_SSL_CLIENTAUTH:
		return LDAP_SASL_EXTERNAL;
		break;
	case BINDMETHOD_SASL_GSSAPI:
		return "GSSAPI";
		break;
	case BINDMETHOD_SASL_DIGEST_MD5:
		return "DIGEST-MD5";
		break;
	default: /* anything else */
		return LDAP_SASL_SIMPLE;
	}

	return LDAP_SASL_SIMPLE;
}

/*
 * Check the result of an ldap_simple_bind operation to see we it
 * contains the expiration controls
 * return: -1 error, not bound
 *          0, OK bind has succeeded
 */
static int
bind_and_check_pwp(Repl_Connection *conn, char * binddn, char *password)
{

	LDAPControl	**ctrls = NULL;
	LDAP		*ld = conn->ld;
	int			rc;
	const char  *mech = bind_method_to_mech(conn->bindmethod);

	LDAPDebug( LDAP_DEBUG_TRACE, "=> bind_and_check_pwp\n", 0, 0, 0 );

	rc = slapi_ldap_bind(conn->ld, binddn, password, mech, NULL,
						 &ctrls, NULL, NULL);

	if ( rc == LDAP_SUCCESS ) 
	{
		if (conn->last_ldap_error != rc)
		{
			conn->last_ldap_error = rc;
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"%s: Replication bind with %s auth resumed\n",
							agmt_get_long_name(conn->agmt),
							mech ? mech : "SIMPLE");
		}

		if ( ctrls ) 
		{
			int i;
			for( i = 0; ctrls[ i ] != NULL; ++i ) 
			{
				if ( !(strcmp( ctrls[ i ]->ldctl_oid, LDAP_CONTROL_PWEXPIRED)) )
				{
					/* Bind is successfull but password has expired */
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"%s: Successfully bound %s to consumer, "
						"but password has expired on consumer.\n",
						agmt_get_long_name(conn->agmt), binddn);
				}					
				else if ( !(strcmp( ctrls[ i ]->ldctl_oid, LDAP_CONTROL_PWEXPIRING)) )
				{
					/* The password is expiring in n seconds */
					if ( (ctrls[ i ]->ldctl_value.bv_val != NULL) &&
						 (ctrls[ i ]->ldctl_value.bv_len > 0) )
					{
						int password_expiring = atoi( ctrls[ i ]->ldctl_value.bv_val );
						slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
							"%s: Successfully bound %s to consumer, "
							"but password is expiring on consumer in %d seconds.\n",
							agmt_get_long_name(conn->agmt), binddn, password_expiring);
					}
				}
			}	
			ldap_controls_free( ctrls );
		}

		LDAPDebug( LDAP_DEBUG_TRACE, "<= bind_and_check_pwp - CONN_OPERATION_SUCCESS\n", 0, 0, 0 );

		return (CONN_OPERATION_SUCCESS);
	}
	else 
	{
		ldap_controls_free( ctrls );
		/* Do not report the same error over and over again */
		if (conn->last_ldap_error != rc)
		{
			char *errmsg = NULL;
			conn->last_ldap_error = rc;
			/* errmsg is a pointer directly into the ld structure - do not free */
			rc = slapi_ldap_get_lderrno( ld, NULL, &errmsg );
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
							"%s: Replication bind with %s auth failed: LDAP error %d (%s) (%s)\n",
							agmt_get_long_name(conn->agmt),
							mech ? mech : "SIMPLE", rc,
							ldap_err2string(rc), errmsg);
		}

		LDAPDebug( LDAP_DEBUG_TRACE, "<= bind_and_check_pwp - CONN_OPERATION_FAILED\n", 0, 0, 0 );
		return (CONN_OPERATION_FAILED);
	}
}

/* Attempt to bind as a user to AD in order to see if we posess the
 * most current password. Returns the LDAP return code of the bind. */
int
windows_check_user_password(Repl_Connection *conn, Slapi_DN *sdn, char *password)
{
	const char *binddn = NULL;
	LDAPMessage *res = NULL;
	int rc = 0;
	int msgid = 0;

	/* If we're already connected, this will just return success */
	windows_conn_connect(conn);

	/* Get binddn from sdn */
	binddn =  slapi_sdn_get_dn(sdn);

	/* Attempt to do a bind on the existing connection 
	 * using the dn and password that were passed in. */
	msgid = do_simple_bind(conn, conn->ld, (char *) binddn, password);
	rc = ldap_result(conn->ld, msgid, LDAP_MSG_ALL, NULL, &res);
	if (0 > rc) { /* error */
		rc = slapi_ldap_get_lderrno(conn->ld, NULL, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"Error reading bind response for id "
						"[%s]: error %d (%s)\n",
						binddn ? binddn : "(anon)",
						rc, ldap_err2string(rc));
	} else if (rc == 0) { /* timeout */
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
						"Error: timeout reading "
						"bind response for [%s]\n",
						binddn ? binddn : "(anon)");
	}
	ldap_parse_result( conn->ld, res, &rc, NULL, NULL, NULL, NULL, 1 /* Free res */);

	/* rebind as the DN specified in the sync agreement */
	bind_and_check_pwp(conn, conn->binddn, conn->plain);

	return rc;
}

static int
do_simple_bind (Repl_Connection *conn, LDAP *ld, char * binddn, char *password)
{
	int msgid;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> do_simple_bind\n", 0, 0, 0 );

	if( LDAP_SUCCESS != slapi_ldap_bind( ld, binddn, password, LDAP_SASL_SIMPLE, NULL, NULL, NULL, &msgid ) ) 
	{
		char *ldaperrtext = NULL;
		int ldaperr;
		int prerr = PR_GetError();

		ldaperr = slapi_ldap_get_lderrno( ld, NULL, &ldaperrtext );
		/* Do not report the same error over and over again */
		if (conn->last_ldap_error != ldaperr)
		{
			conn->last_ldap_error = ldaperr;
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
				"%s: Simple bind failed, " 
				SLAPI_COMPONENT_NAME_LDAPSDK " error %d (%s) (%s), "
				SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
				agmt_get_long_name(conn->agmt),
				ldaperr, ldap_err2string(ldaperr),
				ldaperrtext ? ldaperrtext : "",
				prerr, slapd_pr_strerror(prerr));
		}
	}
	else if (conn->last_ldap_error != LDAP_SUCCESS)
	{
		conn->last_ldap_error = LDAP_SUCCESS;
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Simple bind resumed\n",
			agmt_get_long_name(conn->agmt));
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= do_simple_bind\n", 0, 0, 0 );
	return msgid;
}


static Slapi_Eq_Context
repl5_start_debug_timeout(int *setlevel)
{
	Slapi_Eq_Context eqctx = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> repl5_start_debug_timeout\n", 0, 0, 0 );

	if (s_debug_timeout && s_debug_level) {
		time_t now = time(NULL);
		eqctx = slapi_eq_once(repl5_debug_timeout_callback, setlevel,
							  s_debug_timeout + now);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= repl5_start_debug_timeout\n", 0, 0, 0 );
	return eqctx;
}

static void
repl5_stop_debug_timeout(Slapi_Eq_Context eqctx, int *setlevel)
{
	char buf[20];
	char msg[SLAPI_DSE_RETURNTEXT_SIZE];

	LDAPDebug( LDAP_DEBUG_TRACE, "=> repl5_stop_debug_timeout\n", 0, 0, 0 );

	if (eqctx && !*setlevel) {
		(void)slapi_eq_cancel(eqctx);
	}

	if (s_debug_timeout && s_debug_level && *setlevel) {
		/* No longer needed as we are including the one in slap.h */
		sprintf(buf, "%d", 0);
		config_set_errorlog_level("nsslapd-errorlog-level", buf, msg, 1);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= repl5_stop_debug_timeout\n", 0, 0, 0 );
}

static void
repl5_debug_timeout_callback(time_t when, void *arg)
{
	int *setlevel = (int *)arg;
	/* No longer needed as we are including the one in slap.h */
	char buf[20];
	char msg[SLAPI_DSE_RETURNTEXT_SIZE];

	LDAPDebug( LDAP_DEBUG_TRACE, "=> repl5_debug_timeout_callback\n", 0, 0, 0 );

	*setlevel = 1;
	sprintf(buf, "%d", s_debug_level);
	config_set_errorlog_level("nsslapd-errorlog-level", buf, msg, 1);

	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
		"repl5_debug_timeout_callback: set debug level to %d at %ld\n",
		s_debug_level, when);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= repl5_debug_timeout_callback\n", 0, 0, 0 );
}
