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


/* repl5_protocol_util.c */
/*

Code common to both incremental and total protocols.

*/

#include "repl5.h"
#include "repl5_prot_private.h"
#include "windowsrepl.h"
#include "slap.h"

#include <unicode/ustring.h> /* UTF8 conversion */


int ruv_private_new( RUV **ruv, RUV *clone );

#ifdef FOR_DEBUGGING
static Slapi_Entry* windows_entry_already_exists(Slapi_Entry *e);
static void extract_guid_from_entry_bv(Slapi_Entry *e, const struct berval **bv);
#endif
static void windows_map_mods_for_replay(Private_Repl_Protocol *prp,LDAPMod **original_mods, LDAPMod ***returned_mods, int is_user, char** password);
static int is_subject_of_agreement_local(const Slapi_Entry *local_entry,const Repl_Agmt *ra);
static int is_dn_subject_of_agreement_local(const Slapi_DN *sdn, const Repl_Agmt *ra);
static int windows_create_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *original_entry, Slapi_DN *remote_sdn, Slapi_Entry **remote_entry, char** password);
static int windows_get_local_entry(const Slapi_DN* local_dn,Slapi_Entry **local_entry);
static int windows_get_local_entry_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry, int is_global);
static int windows_get_local_tombstone_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry);
static int windows_search_local_entry_by_uniqueid(Private_Repl_Protocol *prp, const char *uniqueid, char ** attrs, Slapi_Entry **ret_entry, int tombstone, void * component_identity, int is_global);
static int map_entry_dn_outbound(Slapi_Entry *e, Slapi_DN **dn, Private_Repl_Protocol *prp, int *missing_entry, int want_guid);
static char* extract_ntuserdomainid_from_entry(Slapi_Entry *e);
static char* extract_container(const Slapi_DN *entry_dn, const Slapi_DN *suffix_dn);
static int windows_get_remote_entry (Private_Repl_Protocol *prp, const Slapi_DN* remote_dn,Slapi_Entry **remote_entry);
static int windows_get_remote_tombstone(Private_Repl_Protocol *prp, const Slapi_DN* remote_dn,Slapi_Entry **remote_entry);
static int windows_reanimate_tombstone(Private_Repl_Protocol *prp, const Slapi_DN* tombstone_dn, const char* new_dn);
static const char* op2string (int op);
static int is_subject_of_agreement_remote(Slapi_Entry *e, const Repl_Agmt *ra);
static int map_entry_dn_inbound(Slapi_Entry *e, Slapi_DN **dn, const Repl_Agmt *ra);
static int map_entry_dn_inbound_ext(Slapi_Entry *e, Slapi_DN **dn, const Repl_Agmt *ra, int use_guid, int user_username);
static int windows_update_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry,int is_user);
static int is_guid_dn(Slapi_DN *remote_dn);
static int map_windows_tombstone_dn(Slapi_Entry *e, Slapi_DN **dn, Private_Repl_Protocol *prp, int *exists);
static int windows_check_mods_for_rdn_change(Private_Repl_Protocol *prp, LDAPMod **original_mods, 
		Slapi_Entry *local_entry, Slapi_DN *remote_dn, char **newrdn);
static int windows_get_superior_change(Private_Repl_Protocol *prp, Slapi_DN *local_dn, Slapi_DN *remote_dn, char **newsuperior, int to_windows);

/* Controls the direction of flow for mapped attributes */
typedef enum mapping_types {
	bidirectional,
	fromwindowsonly,
	towindowsonly,
	disabled
} mapping_types;

/* Controls if we sync the attibute always, or only when we're creating new entries */
/* Used for attributes like samaccountname, where we want to fill it in on a new entry, but
 * we never want to change it on an existing entry */
typedef enum creation_types {
	always,
	createonly
} creation_types;

typedef enum attr_types {
	normal,
	dnmap
} attr_types;

typedef struct _windows_attribute_map
{
	char *windows_attribute_name;
	char *ldap_attribute_name;
	mapping_types map_type;
	creation_types create_type;
	attr_types attr_type;
} windows_attribute_map;

/* List of attributes that are common to AD and LDAP, so we simply copy them over in both directions */
static char* windows_user_matching_attributes[] = 
{
	"description",
	"destinationIndicator",
	"facsimileTelephoneNumber",
	"givenName",
	"homePhone",
	"homePostalAddress",
	"initials",
	"l",
	"mail",
	"mobile",
	"o",
	"ou",
	"pager",
	"physicalDeliveryOfficeName",
	"postOfficeBox",
	"postalAddress",
	"postalCode",
	"registeredAddress",
	"sn",
	"st",
	"telephoneNumber",
	"teletexTerminalIdentifier",
	"telexNumber",
	"title",
	"userCertificate",
	"x121Address",
	NULL
};

static char* windows_group_matching_attributes[] = 
{
	"description",
	"destinationIndicator",
	"facsimileTelephoneNumber",
	"givenName",
	"homePhone",
	"homePostalAddress",
	"initials",
	"l",
	"mail",
	"manager",
	"mobile",
	"o",
	"ou",
	"pager",
	"physicalDeliveryOfficeName",
	"postOfficeBox",
	"postalAddress",
	"postalCode",
	"preferredDeliveryMethod",
	"registeredAddress",
	"sn",
	"st",
	"telephoneNumber",
	"teletexTerminalIdentifier",
	"telexNumber",
	"title",
	"userCertificate",
	"x121Address",
	NULL
};

/* List of attributes that are single-valued in AD, but multi-valued in DS */
static char * windows_single_valued_attributes[] =
{
	"facsimileTelephoneNumber",
	"givenName",
	"homePhone",
	"homePostalAddress",
	"initials",
	"l",
	"mail",
	"mobile",
	"pager",
	"physicalDeliveryOfficeName",
	"postalCode",
	"sn",
	"st",
	"street",
	FAKE_STREET_ATTR_NAME,
	"streetAddress",
	"telephoneNumber",
	"title",
	NULL
};

/* List of attributes that are common to AD and LDAP, so we simply copy them over in both directions */
static char* nt4_user_matching_attributes[] = 
{
	"description",
	NULL
};

static char* nt4_group_matching_attributes[] = 
{
	"description",
	NULL
};

static windows_attribute_map user_attribute_map[] = 
{
	{ "homeDirectory", "ntUserHomeDir", bidirectional, always, normal},
	{ "scriptPath", "ntUserScriptPath", bidirectional, always, normal},
	{ "lastLogon", "ntUserLastLogon", fromwindowsonly, always, normal},
	{ "lastLogoff", "ntUserLastLogoff", fromwindowsonly, always, normal},
	{ "accountExpires", "ntUserAcctExpires", bidirectional, always, normal},
	{ "codePage", "ntUserCodePage", bidirectional, always, normal},
	{ "logonHours", "ntUserLogonHours", bidirectional, always, normal},
	{ "maxStorage", "ntUserMaxStorage", bidirectional, always, normal},
	{ "profilePath", "ntUserProfile", bidirectional, always, normal},
	/* IETF schema has 'street' and 'streetaddress' as aliases, but Microsoft does not */
	{ "streetAddress", "street", towindowsonly, always, normal},
	{ FAKE_STREET_ATTR_NAME, "street", fromwindowsonly, always, normal},
	{ "userParameters", "ntUserParms", bidirectional, always, normal},
	{ "userWorkstations", "ntUserWorkstations", bidirectional, always, normal},
	{ "sAMAccountName", "ntUserDomainId", bidirectional, always, normal},
	/* AD uses cn as it's naming attribute.  We handle it as a special case */
	{ "cn", "cn", towindowsonly, createonly, normal},
	/* However, it isn't a naming attribute in DS (we use uid) so it's safe to accept changes inbound */
	{ "name", "cn", fromwindowsonly, always, normal},
	{ "manager", "manager", bidirectional, always, dnmap},
	{ "seealso", "seealso", bidirectional, always, dnmap},
	{NULL, NULL, -1}
};

static windows_attribute_map group_attribute_map[] = 
{
	{ "groupType", "ntGroupType",  bidirectional, createonly, normal},
	{ "sAMAccountName", "ntUserDomainId", bidirectional, always, normal},
	/* IETF schema has 'street' and 'streetaddress' as aliases, but Microsoft does not */
	{ "streetAddress", "street", towindowsonly, always, normal},
	{ FAKE_STREET_ATTR_NAME, "street", fromwindowsonly, always, normal},
	{ "member", "uniquemember", bidirectional, always, dnmap},
	{NULL, NULL, -1}
};

/* 
 * Notes on differences for NT4:
 * 1. NT4 returns the SID value in the objectGUID attribute value.
 *    The SID has variable length and does not match the length of a GUID.
 * 2. NT4 currently never generates tombstones. If it did, we'd need to parse the 
 *    different form of the GUID in the tombstone DNs.
 * 3. NT4 Does not implement the dirsync control. We always get all users and groups.
 * 4. NT4 generates and expects DNs with samaccountname as the RDN, not cn.
 * 5. NT4 handles the DN=<GUID> (remember that the '<' '>' characters are included!) DN form 
 *    for modifies and deletes, provided we use the value it gave us in the objectGUID attribute (which is actually the SID).
 * 6. NT4 has less and different schema from AD. For example users in NT4 have no firstname/lastname, only an optional 'description'.
 */

/* 
 * When we get an error from an LDAP operation, we call this
 * function to decide if we should just keep replaying
 * updates, or if we should stop, back off, and try again
 * later.
 * Returns PR_TRUE if we shoould keep going, PR_FALSE if
 * we should back off and try again later.
 *
 * In general, we keep going if the return code is consistent
 * with some sort of bug in URP that causes the consumer to
 * emit an error code that it shouldn't have, e.g. LDAP_ALREADY_EXISTS.
 * 
 * We stop if there's some indication that the server just completely
 * failed to process the operation, e.g. LDAP_OPERATIONS_ERROR.
 */
PRBool
windows_ignore_error_and_keep_going(int error)
{
	int return_value = PR_FALSE;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_ignore_error_and_keep_going\n", 0, 0, 0 );

	switch (error)
	{
	/* Cases where we keep going */
	case LDAP_SUCCESS:
	case LDAP_NO_SUCH_ATTRIBUTE:
	case LDAP_UNDEFINED_TYPE:
	case LDAP_CONSTRAINT_VIOLATION:
	case LDAP_TYPE_OR_VALUE_EXISTS:
	case LDAP_INVALID_SYNTAX:
	case LDAP_NO_SUCH_OBJECT:
	case LDAP_INVALID_DN_SYNTAX:
	case LDAP_IS_LEAF:
	case LDAP_INSUFFICIENT_ACCESS:
	case LDAP_NAMING_VIOLATION:
	case LDAP_OBJECT_CLASS_VIOLATION:
	case LDAP_NOT_ALLOWED_ON_NONLEAF:
	case LDAP_NOT_ALLOWED_ON_RDN:
	case LDAP_ALREADY_EXISTS:
	case LDAP_NO_OBJECT_CLASS_MODS:
		return_value = PR_TRUE;
		break;

	/* Cases where we stop and retry */
	case LDAP_OPERATIONS_ERROR:
	case LDAP_PROTOCOL_ERROR:
	case LDAP_TIMELIMIT_EXCEEDED:
	case LDAP_SIZELIMIT_EXCEEDED:
	case LDAP_STRONG_AUTH_NOT_SUPPORTED:
	case LDAP_STRONG_AUTH_REQUIRED:
	case LDAP_PARTIAL_RESULTS:
	case LDAP_REFERRAL:
	case LDAP_ADMINLIMIT_EXCEEDED:
	case LDAP_UNAVAILABLE_CRITICAL_EXTENSION:
	case LDAP_CONFIDENTIALITY_REQUIRED:
	case LDAP_SASL_BIND_IN_PROGRESS:
	case LDAP_INAPPROPRIATE_MATCHING:
	case LDAP_ALIAS_PROBLEM:
	case LDAP_ALIAS_DEREF_PROBLEM:
	case LDAP_INAPPROPRIATE_AUTH:
	case LDAP_INVALID_CREDENTIALS:
	case LDAP_BUSY:
	case LDAP_UNAVAILABLE:
	case LDAP_UNWILLING_TO_PERFORM:
	case LDAP_LOOP_DETECT:
	case LDAP_SORT_CONTROL_MISSING:
	case LDAP_INDEX_RANGE_ERROR:
	case LDAP_RESULTS_TOO_LARGE:
	case LDAP_AFFECTS_MULTIPLE_DSAS:
	case LDAP_OTHER:
	case LDAP_SERVER_DOWN:
	case LDAP_LOCAL_ERROR:
	case LDAP_ENCODING_ERROR:
	case LDAP_DECODING_ERROR:
	case LDAP_TIMEOUT:
	case LDAP_AUTH_UNKNOWN:
	case LDAP_FILTER_ERROR:
	case LDAP_USER_CANCELLED:
	case LDAP_PARAM_ERROR:
	case LDAP_NO_MEMORY:
	case LDAP_CONNECT_ERROR:
	case LDAP_NOT_SUPPORTED:
	case LDAP_CONTROL_NOT_FOUND:
	case LDAP_NO_RESULTS_RETURNED:
	case LDAP_MORE_RESULTS_TO_RETURN:
	case LDAP_CLIENT_LOOP:
	case LDAP_REFERRAL_LIMIT_EXCEEDED:
		return_value = PR_FALSE;
		break;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_ignore_error_and_keep_going\n", 0, 0, 0 );
	return return_value;
}

static const char*
op2string(int op)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "=> op2string\n", 0, 0, 0 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= op2string\n", 0, 0, 0 );
	switch (op) {
	case SLAPI_OPERATION_ADD:
		return "add";
	case SLAPI_OPERATION_MODIFY:
		return "modify";
	case SLAPI_OPERATION_DELETE:
		return "delete";
	case SLAPI_OPERATION_MODRDN:
		return "rename";
	case SLAPI_OPERATION_EXTENDED:
		return "extended";
	}
	
	return "unknown";
}

static void 
windows_dump_entry(const char *string, Slapi_Entry *e)
{
	int length = 0;
	char *buffer = NULL;
	if (slapi_is_loglevel_set(SLAPI_LOG_REPL))
	{
		buffer = slapi_entry2str(e,&length);
		slapi_log_error(SLAPI_LOG_REPL, NULL, "Windows sync entry: %s %s\n", string, buffer);
		if (buffer)
		{
			slapi_ch_free_string(&buffer);
		} 
	}
}

static void
map_dn_values(Private_Repl_Protocol *prp,Slapi_ValueSet *original_values, Slapi_ValueSet **mapped_values, int to_windows, int return_originals)
{
	Slapi_ValueSet *new_vs = NULL;
	Slapi_Value *original_value = NULL;
	int retval = 0;
	int i = 0;

	/* Set the keep raw entry flag to avoid overwriting the existing raw entry. */
	windows_private_set_keep_raw_entry(prp->agmt, 1);

	/* For each value: */
    i= slapi_valueset_first_value(original_values,&original_value);
    while ( i != -1 ) {

		int is_ours = 0;
		char *new_dn_string = NULL;
		const char *original_dn_string = NULL;
		int original_dn_string_length = 0;
		Slapi_DN *original_dn = NULL;

		original_dn_string = slapi_value_get_string(original_value);
		/* Sanity check the data was a valid string */
		original_dn_string_length = slapi_value_get_length(original_value);
		if (0 == original_dn_string_length) {
			slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: length of dn is 0\n");
		}
		/* Make a sdn from the string */
		original_dn = slapi_sdn_new_dn_byref(original_dn_string);
		if (!original_dn) {
			slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: unable to create Slapi_DN from %s.\n", original_dn_string);
			return;
		}

		if (to_windows)
		{
			Slapi_Entry *local_entry = NULL;
			/* Try to get the local entry */
			retval = windows_get_local_entry(original_dn,&local_entry);
			if (0 == retval && local_entry)
			{
				int missing_entry = 0;
				Slapi_DN *remote_dn = NULL;
				/* Now map the DN */
				is_ours = is_subject_of_agreement_local(local_entry,prp->agmt);
				if (is_ours)
				{
					map_entry_dn_outbound(local_entry,&remote_dn,prp,&missing_entry, 0 /* don't want GUID form here */);
					if (remote_dn)
					{
						if (!missing_entry)
						{
							/* Success */
							if (return_originals)
							{
								new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)));
							} else 
							{
								new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(remote_dn));
							}
						}
						slapi_sdn_free(&remote_dn);
					} else
					{
						slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: no remote dn found for %s\n", original_dn_string);					
					}
				} else
				{
					slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: this entry is not ours %s\n", original_dn_string);
				}
			} else {
				slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: no local entry found for %s\n", original_dn_string);
			}
			if (local_entry)
			{
				slapi_entry_free(local_entry);
				local_entry = NULL;
			}
		} else
		{
			Slapi_Entry *remote_entry = NULL;
			Slapi_DN *local_dn = NULL;
			/* Try to get the remote entry */
			retval = windows_get_remote_entry(prp,original_dn,&remote_entry);
			if (remote_entry && 0 == retval)
			{
				is_ours = is_subject_of_agreement_remote(remote_entry,prp->agmt);
				if (is_ours)
				{
					retval = map_entry_dn_inbound(remote_entry,&local_dn,prp->agmt);	
					if (0 == retval && local_dn)
					{
						if (return_originals)
						{
							new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)));
						} else
						{
							new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(local_dn));
						}
						slapi_sdn_free(&local_dn);
					} else
					{
						slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: no local dn found for %s\n", original_dn_string);
					}
				} else
				{
					slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: this entry is not ours %s\n", original_dn_string);
				}
			} else
			{
				slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: no remote entry found for %s\n", original_dn_string);
			}
			if (remote_entry)
			{
				slapi_entry_free(remote_entry);
				remote_entry = NULL;
			}
		}
		/* Extract the dn string and store in the new value */
		if (new_dn_string)
		{
			Slapi_Value *new_value = NULL;
			if (NULL == new_vs)
			{
				new_vs = slapi_valueset_new();
			}
			new_value = slapi_value_new_string_passin(new_dn_string);
			slapi_valueset_add_value(new_vs,new_value);
			slapi_value_free(&new_value);
		}
		/* If not then we skip it */
        i = slapi_valueset_next_value(original_values,i,&original_value);
		slapi_sdn_free(&original_dn);
    }/* while */
	if (new_vs)
	{
		*mapped_values = new_vs;
	}

	/* Restore the keep raw entry flag. */
	windows_private_set_keep_raw_entry(prp->agmt, 0);
}

static void
windows_dump_ruvs(Object *supl_ruv_obj, Object *cons_ruv_obj)
{
	if (slapi_is_loglevel_set(SLAPI_LOG_REPL))
	{
		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV:\n");
		if (supl_ruv_obj) {
			RUV* sup = NULL;
			object_acquire(supl_ruv_obj);
			sup = (RUV*)  object_get_data ( supl_ruv_obj );
			ruv_dump (sup, "supplier", NULL);
			object_release(supl_ruv_obj);
		} else
		{
			slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV = null\n");
		}
		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV:\n");

		if (cons_ruv_obj) 
		{
			RUV* con = NULL;
			object_acquire(cons_ruv_obj);
			con =  (RUV*) object_get_data ( cons_ruv_obj );
			ruv_dump (con,"consumer", NULL);
			object_release( cons_ruv_obj );
		} else {
			slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV = null\n");
		}
	}
}

/*
 * Acquire exclusive access to a replica. Send a start replication extended
 * operation to the replica. The response will contain a success code, and
 * optionally the replica's update vector if acquisition is successful.
 * This function returns one of the following:
 * ACQUIRE_SUCCESS - the replica was acquired, and we have exclusive update access
 * ACQUIRE_REPLICA_BUSY - another master was updating the replica
 * ACQUIRE_FATAL_ERROR - something bad happened, and it's not likely to improve
 *                       if we wait.
 * ACQUIRE_TRANSIENT_ERROR - something bad happened, but it's probably worth
 *                           another try after waiting a while.
 * If ACQUIRE_SUCCESS is returned, then ruv will point to the replica's update
 * vector. It's possible that the replica does something goofy and doesn't
 * return us an update vector, so be prepared for ruv to be NULL (but this is
 * an error).
 */
int
windows_acquire_replica(Private_Repl_Protocol *prp, RUV **ruv, int check_ruv)
{
  
	int return_value = ACQUIRE_SUCCESS;
	ConnResult crc = 0;
	Repl_Connection *conn = NULL;
	Replica *replica = NULL;
	Object *supl_ruv_obj, *cons_ruv_obj = NULL;
	PRBool is_newer = PR_FALSE;
	RUV *r = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_acquire_replica\n", 0, 0, 0 );

	if (NULL == ruv)
	{
        	slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "NULL ruv\n");
        	return_value = ACQUIRE_FATAL_ERROR;
		goto done;
	}

	PR_ASSERT(prp);

    if (prp->replica_acquired)  /* we already acquire replica */
    {
        slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"%s: Remote replica already acquired\n",
						agmt_get_long_name(prp->agmt));
								return_value = ACQUIRE_FATAL_ERROR;
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_acquire_replica\n", 0, 0, 0 );
        return ACQUIRE_SUCCESS;
    }

	if (NULL != *ruv)
	{
		ruv_destroy ( ruv );
	}

	object_acquire(prp->replica_object);
	replica = object_get_data(prp->replica_object);
	supl_ruv_obj = replica_get_ruv ( replica );
	cons_ruv_obj = agmt_get_consumer_ruv(prp->agmt);

	windows_dump_ruvs(supl_ruv_obj,cons_ruv_obj);
	is_newer = ruv_is_newer ( supl_ruv_obj, cons_ruv_obj );
	if (is_newer)
	{
		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV is newer\n");
	}
	
	/* Handle the pristine case */
	if (cons_ruv_obj == NULL) 
	{
		*ruv = NULL;		
	} else 
	{
		r = (RUV*)  object_get_data(cons_ruv_obj); 
		*ruv = ruv_dup(r);
	}

	if ( supl_ruv_obj ) object_release ( supl_ruv_obj );
	if ( cons_ruv_obj ) object_release ( cons_ruv_obj );
	object_release (prp->replica_object);
	replica = NULL;

	/* Once we get here we have a valid ruv */
 	if (is_newer == PR_FALSE && check_ruv) { 
 		prp->last_acquire_response_code = NSDS50_REPL_UPTODATE;
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_acquire_replica - ACQUIRE_CONSUMER_WAS_UPTODATE\n", 0, 0, 0 );
 		return ACQUIRE_CONSUMER_WAS_UPTODATE; 
 	} 

	prp->last_acquire_response_code = NSDS50_REPL_REPLICA_NO_RESPONSE;

	/* Get the connection */
	conn = prp->conn;

	crc = windows_conn_connect(conn);
	if (CONN_OPERATION_FAILED == crc)
	{
		return_value = ACQUIRE_TRANSIENT_ERROR;
	}
	else if (CONN_SSL_NOT_ENABLED == crc)
	{
		return_value = ACQUIRE_FATAL_ERROR;
	}
	else
	{
		/* we don't want the timer to go off in the middle of an operation */
		windows_conn_cancel_linger(conn);
		/* Does the remote replica support the dirsync protocol? 
	       if it update the conn object */
		windows_conn_replica_supports_dirsync(conn); 
		if (CONN_NOT_CONNECTED == crc || CONN_OPERATION_FAILED == crc)
		{
			/* We don't know anything about the remote replica. Try again later. */
			return_value = ACQUIRE_TRANSIENT_ERROR;
		}
		else
		{
			/* Good to go. Start the protocol. */
			CSN *current_csn = NULL;
			Slapi_DN *replarea_sdn;

			/* Obtain a current CSN */
			replarea_sdn = agmt_get_replarea(prp->agmt);
			current_csn = get_current_csn(replarea_sdn);
			if (NULL != current_csn)
			{
			    return_value = ACQUIRE_SUCCESS;
			}
			else
			{
				/* Couldn't get a current CSN */
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"%s: Unable to obtain current CSN. "
					"Replication is aborting.\n",
					agmt_get_long_name(prp->agmt));
				return_value = ACQUIRE_FATAL_ERROR;
			}
			slapi_sdn_free(&replarea_sdn);
			csn_free(&current_csn);
		}
	}

	if (ACQUIRE_SUCCESS != return_value)
	{
		/* could not acquire the replica, so reinstate the linger timer, since this
		   means we won't call release_replica, which also reinstates the timer */
	     windows_conn_start_linger(conn);
	}
    else
    {
        /* replica successfully acquired */
        prp->replica_acquired = PR_TRUE;
    }
done:
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_acquire_replica\n", 0, 0, 0 );

	return return_value;
}

void
windows_release_replica(Private_Repl_Protocol *prp)
{
  LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_release_replica\n", 0, 0, 0 );

  PR_ASSERT(NULL != prp);
  PR_ASSERT(NULL != prp->conn);

  if (!prp->replica_acquired)
    return;

  windows_conn_start_linger(prp->conn);

  prp->replica_acquired = PR_FALSE;

  LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_release_replica\n", 0, 0, 0 );

}

static void
to_little_endian_double_bytes(UChar *unicode_password, int32_t unicode_password_length)
{
	int32_t i = 0;
	for (i = 0 ; i < unicode_password_length; i++) 
	{
		UChar c = unicode_password[i];
		char *byte_ptr = (char*)&(unicode_password[i]);
		byte_ptr[0] = (char)(c & 0xff);
		byte_ptr[1] = (char)(c >> 8);
	}
}

/* this entry had a password, handle it seperately */
/* http://support.microsoft.com/?kbid=269190 */
static int
send_password_modify(Slapi_DN *sdn, char *password, Private_Repl_Protocol *prp)
{
		ConnResult pw_return = 0;

		if (!sdn || !slapi_sdn_get_dn(sdn) || !password)
		{
			return CONN_OPERATION_FAILED;
		}

		if (windows_private_get_isnt4(prp->agmt))
		{
			/* NT4 just wants a plaintext password */
			Slapi_Mods smods = {0};

			slapi_mods_init (&smods, 0);
			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "UnicodePwd", password);

			pw_return = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(sdn), slapi_mods_get_ldapmods_byref(&smods), NULL, NULL );

			slapi_mods_done(&smods);

		} else
		{
			/* We will attempt to bind to AD with the new password first. We do
			 * this to avoid playing a password change that originated from AD
			 * back to AD.  If we just played the password change back, then
			 * both sides would be in sync, but AD would contain the new password
			 * twice in it's password history, which undermines the password
			 * history policies in AD. */
			if (windows_check_user_password(prp->conn, sdn, password)) {
				char *quoted_password = NULL;
				/* AD wants the password in quotes ! */
				quoted_password = PR_smprintf("\"%s\"",password);
				if (quoted_password)
				{
					LDAPMod *pw_mods[2];
					LDAPMod pw_mod;
					struct berval bv = {0};
					UChar *unicode_password = NULL;
					int32_t unicode_password_length = 0; /* Length in _characters_ */
					int32_t buffer_size = 0; /* Size in _characters_ */
					UErrorCode error = U_ZERO_ERROR;
					struct berval *bvals[2];
					/* Need to UNICODE encode the password here */
					/* It's one of those 'ask me first and I will tell you the buffer size' functions */
					u_strFromUTF8(NULL, 0, &unicode_password_length, quoted_password, strlen(quoted_password), &error);
					buffer_size = unicode_password_length;
					unicode_password = (UChar *)slapi_ch_malloc(unicode_password_length * sizeof(UChar));
					if (unicode_password) {
						error = U_ZERO_ERROR;
						u_strFromUTF8(unicode_password, buffer_size, &unicode_password_length, quoted_password, strlen(quoted_password), &error);
	
						/* As an extra special twist, we need to send the unicode in little-endian order for AD to be happy */
						to_little_endian_double_bytes(unicode_password, unicode_password_length);
	
						bv.bv_len = unicode_password_length * sizeof(UChar);
						bv.bv_val = (char*)unicode_password;
				
						bvals[0] = &bv; 
						bvals[1] = NULL;
						
						pw_mod.mod_type = "UnicodePwd";
						pw_mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
						pw_mod.mod_bvalues = bvals;
					
						pw_mods[0] = &pw_mod;
						pw_mods[1] = NULL;

						pw_return = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(sdn), pw_mods, NULL, NULL );

						slapi_ch_free((void**)&unicode_password);
					}
					PR_smprintf_free(quoted_password);
				}
			} else {
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"%s: AD already has the current password for %s. "
					"Not sending password modify to AD.\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(sdn));
			}
		}

		return pw_return;
}

static int
send_accountcontrol_modify(Slapi_DN *sdn, Private_Repl_Protocol *prp, int missing_entry)
{
	ConnResult mod_return = 0;
	Slapi_Mods smods = {0};
	Slapi_Entry *remote_entry = NULL;
	int retval;
	unsigned long acctval = 0;
	char acctvalstr[32];

	/* have to first retrieve the existing entry - userAccountControl is
	   a bit array, and we must preserve the existing values if any */
	/* Get the remote entry */
	retval = windows_get_remote_entry(prp, sdn, &remote_entry);
	if (0 == retval && remote_entry) {
		acctval = slapi_entry_attr_get_ulong(remote_entry, "userAccountControl");
	}
	slapi_entry_free(remote_entry);
	/* if we are adding a new entry, we need to set the entry to be
	   enabled to allow AD login */
	if (missing_entry) {
	    slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			    "%s: New Windows entry %s will be enabled.\n",
			    agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(sdn));
	    acctval &= ~0x2; /* unset the disabled bit, if set */
	}
	/* set the account to be a normal account */
	acctval |= 0x0200; /* normal account == 512 */

	slapi_mods_init (&smods, 0);
	PR_snprintf(acctvalstr, sizeof(acctvalstr), "%lu", acctval);
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "userAccountControl", acctvalstr);

	mod_return = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(sdn), slapi_mods_get_ldapmods_byref(&smods), NULL, NULL );

    slapi_mods_done(&smods);
	return mod_return;
}

static int
windows_entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value)
{
	int retval = 0;
	Slapi_Attr *attr = NULL;
	if (NULL == e || NULL == attrname)
	{
		return retval;
	}
	/* see if the entry has the specified attribute name */
	if (0 == slapi_entry_attr_find(e, attrname, &attr) && attr)
	{
		/* if value is not null, see if the attribute has that
		   value */
		if (value)
		{
			Slapi_Value *v = NULL;
			int index = 0;
			for (index = slapi_attr_first_value(attr, &v);
				 v && (index != -1);
				 index = slapi_attr_next_value(attr, index, &v))
			{
				const char *s = slapi_value_get_string(v);
				if (NULL == s)
				{
					continue;
				}
				if (0 == strcasecmp(s, value))
				{
					retval = 1;
					break;
				}
			}
		}
	}
	return retval;
}

static void
windows_is_local_entry_user_or_group(Slapi_Entry *e, int *is_user, int *is_group)
{
	if (is_user) {
		*is_user = windows_entry_has_attr_and_value(e, "objectclass", "ntuser");
	}
	if (is_group) {
		*is_group = windows_entry_has_attr_and_value(e, "objectclass", "ntgroup");
	}
}

static void
windows_is_remote_entry_user_or_group(Slapi_Entry *e, int *is_user, int *is_group)
{
	*is_user = windows_entry_has_attr_and_value(e,"objectclass","person");
	*is_group = windows_entry_has_attr_and_value(e,"objectclass","group");
}

static int
add_remote_entry_allowed(Slapi_Entry *e)
{
	/* We say yes if the entry has the ntUserCreateNewAccount attribute set in the case of a user, or the ntGroupDeleteGroup
	 * attribute set in the case of a group
	 */
	/* Is this a user or a group ? */
	int is_user = 0;
	int is_group = 0;
	char *delete_attr = NULL;

	windows_is_local_entry_user_or_group(e,&is_user,&is_group);
	if (!is_user && !is_group)
	{
		/* Neither fish nor foul.. */
		return -1;
	}
	if (is_user && is_group) 
	{
		/* Now that's just really strange... */
		return -1;
	}
	if (is_user) 
	{
		delete_attr = "ntUserCreateNewAccount";
	} else 
	{
		delete_attr = "ntGroupCreateNewGroup";
	}
	/* Now test if the attribute value is set */
	return windows_entry_has_attr_and_value(e,delete_attr,"true");
}

/* Tells us if we're allowed to add this (remote) entry locally */
static int
add_local_entry_allowed(Private_Repl_Protocol *prp, Slapi_Entry *e)
{
	int is_user = 0;
	int is_group = 0;

	windows_is_remote_entry_user_or_group(e,&is_user,&is_group);	

	if (is_user)
	{
		return windows_private_create_users(prp->agmt);
	} 
	if (is_group)
	{
		return windows_private_create_groups(prp->agmt);
	}
	/* Default to 'no' */
	return 0;
}

static int
delete_remote_entry_allowed(Slapi_Entry *e)
{
	/* We say yes if the entry has the ntUserDeleteAccount attribute set in the case of a user, or the ntGroupDeleteGroup
	 * attribute set in the case of a group
	 */
	/* Is this a user or a group ? */
	int is_user = 0;
	int is_group = 0;
	char *delete_attr = NULL;

	windows_is_local_entry_user_or_group(e,&is_user,&is_group);
	if (!is_user && !is_group)
	{
		/* Neither fish nor foul.. */
		return 0;
	}
	if (is_user && is_group) 
	{
		/* Now that's just really strange... */
		return 0;
	}
	if (is_user) 
	{
		delete_attr = "ntUserDeleteAccount";
	} else 
	{
		delete_attr = "ntGroupDeleteGroup";
	}
	/* Now test if the attribute value is set */
	return windows_entry_has_attr_and_value(e,delete_attr,"true");
}

static void
windows_log_add_entry_remote(const Slapi_DN *local_dn,const Slapi_DN *remote_dn)
{
	const char* local_dn_string = slapi_sdn_get_dn(local_dn);
	const char* remote_dn_string = slapi_sdn_get_dn(remote_dn);
	slapi_log_error(SLAPI_LOG_REPL, NULL, "Attempting to add entry %s to AD for local entry %s\n",remote_dn_string,local_dn_string);
}

/*
 * The entry may have been modified to make it "sync-able", so the modify operation should
 * actually trigger the addition of the entry to windows
 * check the list of mods to see if the sync objectclass/attributes were added to the entry
 * and if so if the current local entry still has them
*/
static int 
sync_attrs_added(LDAPMod **original_mods, Slapi_Entry *local_entry) {
	int retval = 0;
	int ii = 0;
	char *useroc = "ntuser";
	char *groupoc = "ntgroup";
	size_t ulen = 6;
	size_t glen = 7;

	for (ii = 0; (retval == 0) && original_mods && original_mods[ii]; ++ii) {
		LDAPMod *mod = original_mods[ii];
		/* look for a mod/add or replace op with valid type and values */
		if (!(SLAPI_IS_MOD_ADD(mod->mod_op) || SLAPI_IS_MOD_REPLACE(mod->mod_op)) ||
			!mod->mod_type || !mod->mod_bvalues || !mod->mod_bvalues[0]) {
			continue; /* skip it */
		}
		/* if it has an objectclass mod, see if ntuser or ntgroup is one of them */
		if (!strcasecmp(mod->mod_type, "objectclass")) {
			int jj = 0;
			for (jj = 0; (retval == 0) && mod->mod_bvalues[jj]; ++jj) {
				struct berval *bv = mod->mod_bvalues[jj];
				if (((bv->bv_len == ulen) && !PL_strncasecmp(useroc, bv->bv_val, ulen)) ||
					((bv->bv_len == glen) && !PL_strncasecmp(groupoc, bv->bv_val, glen))) {
					retval = 1; /* has magic objclass value */
				}
			}
		}
	}

	/* if the modify op had the right values, see if they are still present in
	   the local entry */
	if (retval == 1) {
		retval = add_remote_entry_allowed(local_entry); /* check local entry */
		if (retval < 0) {
			retval = 0;
		}
	}

	return retval;
}

static ConnResult
process_replay_add(Private_Repl_Protocol *prp, Slapi_Entry *add_entry, Slapi_Entry *local_entry, Slapi_DN *local_dn, Slapi_DN *remote_dn, int is_user, int missing_entry, char **password)
{
	int remote_add_allowed = add_remote_entry_allowed(local_entry);
	ConnResult return_value = 0;
	int rc = 0;

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: process_replay_add: dn=\"%s\" (%s,%s)\n", agmt_get_long_name(prp->agmt),
		slapi_sdn_get_dn(remote_dn), missing_entry ? "not present" : "already present",
		remote_add_allowed ? "add allowed" : "add not allowed");

	if (missing_entry)
	{
		/* If DN is a GUID, we need to attempt to reanimate the tombstone */
		if (is_guid_dn(remote_dn)) {
			int tstone_exists = 0;
			int reanimate_rc = -1;
			char *new_dn_string = NULL;
			char *cn_string = NULL;
			Slapi_DN *tombstone_dn = NULL;

			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"%s: process_replay_add: dn=\"%s\" appears to have been"
				"  deleted on remote side.  Searching for tombstone.\n",
				agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn));

			/* Map local entry to tombstone DN and verify that it exists on
			 * AD side */
			map_windows_tombstone_dn(local_entry, &tombstone_dn, prp, &tstone_exists);

			/* We can't use a GUID DN, so rewrite to the new mapped DN. */
			cn_string = slapi_entry_attr_get_charptr(local_entry,"cn");
			if (!cn_string) {
				cn_string = slapi_entry_attr_get_charptr(local_entry,"ntuserdomainid");
			}

			if (cn_string) {
				char *container_str = NULL;
				const char *suffix = slapi_sdn_get_dn(windows_private_get_windows_subtree(prp->agmt));

				container_str = extract_container(slapi_entry_get_sdn_const(local_entry),
					windows_private_get_directory_subtree(prp->agmt));
				new_dn_string = slapi_create_dn_string("cn=\"%s\",%s%s", cn_string, container_str, suffix);

				if (new_dn_string) {
					/* If the tombstone exists, reanimate it. If the tombstone
					 * does not exist, we'll create a new entry in AD, which
					 * will end up getting a new GUID generated by AD. */
					if (tstone_exists) {
						slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: process_replay_add: Reanimating tombstone (dn=\"%s\") to"
							" normal entry (dn=\"%s\").\n", agmt_get_long_name(prp->agmt),
							slapi_sdn_get_dn(tombstone_dn), new_dn_string);
						reanimate_rc = windows_reanimate_tombstone(prp, tombstone_dn, (const char *)new_dn_string);
						if (reanimate_rc != 0) {
							slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: process_replay_add: Reanimation of tombstone"
								" (dn=\"%s\") failed.  A new entry (dn=\"%s\")"
								" will be added instead.\n", agmt_get_long_name(prp->agmt),
								slapi_sdn_get_dn(tombstone_dn), new_dn_string);
						}
					}

					/* Clear out the old GUID DN and use the new one. We hand off the memory
					 * for new_dn_string into the remote_dn. */
					slapi_sdn_done(remote_dn);
					slapi_sdn_set_dn_passin(remote_dn, new_dn_string);
				}

				slapi_ch_free_string(&cn_string);
				slapi_ch_free_string(&container_str);
			}

			if (tombstone_dn) {
				slapi_sdn_free(&tombstone_dn);
			}

			if (reanimate_rc == 0) {
				/* We reanimated a tombstone, so an add won't work.  We
				 * fallback to doing a modify of the newly reanimated
				 * entry. */
				goto modify_fallback;
			}
		}

		if (remote_add_allowed) {
			LDAPMod **entryattrs = NULL;
			Slapi_Entry *mapped_entry = NULL;
			/* First map the entry */
			rc = windows_create_remote_entry(prp,add_entry, remote_dn, &mapped_entry, password);
			/* Convert entry to mods */
			if (0 == rc && mapped_entry) 
			{
				if (is_user) {
					winsync_plugin_call_pre_ad_add_user_cb(prp->agmt, mapped_entry, add_entry);
				} else {
					winsync_plugin_call_pre_ad_add_group_cb(prp->agmt, mapped_entry, add_entry);
				}
				/* plugin may reset DN */
				slapi_sdn_copy(slapi_entry_get_sdn(mapped_entry), remote_dn);
				(void)slapi_entry2mods (mapped_entry , NULL /* &entrydn : We don't need it */, &entryattrs);
				slapi_entry_free(mapped_entry);
				mapped_entry = NULL;
				if (NULL == entryattrs)
				{
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"%s: windows_replay_add: Cannot convert entry to LDAPMods.\n",
						agmt_get_long_name(prp->agmt));
					return_value = CONN_LOCAL_ERROR;
				}
				else
				{
					int ldap_op = 0;
					int ldap_result_code = 0;
					windows_log_add_entry_remote(local_dn, remote_dn);
					return_value = windows_conn_send_add(prp->conn, slapi_sdn_get_dn(remote_dn),
						entryattrs, NULL, NULL);
					windows_conn_get_error(prp->conn, &ldap_op, &ldap_result_code);
					if ((return_value != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
						/* op failed but no ldap error code ??? */
						ldap_result_code = LDAP_OPERATIONS_ERROR;
					}
					if (is_user) {
						winsync_plugin_call_post_ad_add_user_cb(prp->agmt, mapped_entry, add_entry, &ldap_result_code);
					} else {
						winsync_plugin_call_post_ad_add_group_cb(prp->agmt, mapped_entry, add_entry, &ldap_result_code);
					}
					/* see if plugin reset success/error condition */
					if ((return_value != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
						return_value = CONN_OPERATION_SUCCESS;
						windows_conn_set_error(prp->conn, ldap_result_code);
					} else if ((return_value == CONN_OPERATION_SUCCESS) && ldap_result_code) {
						return_value = CONN_OPERATION_FAILED;
						windows_conn_set_error(prp->conn, ldap_result_code);
					}
					/* It's possible that the entry already exists in AD, in which
					 * case we fall back to modify it */
					/* NGK - This fallback doesn't seem to happen, at least not at this point
					 * in the code.  The only chance to fallback to doing a modify is if
					 * missing_entry is set to 0 at the top of this function. */
					if (return_value)
					{
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"%s: windows_replay_add: Cannot replay add operation.\n",
							agmt_get_long_name(prp->agmt));
					}
					ldap_mods_free(entryattrs, 1);
					entryattrs = NULL;
				}
			} else 
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: process_replay_add: failed to create mapped entry dn=\"%s\"\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn));
			}
		}
	} else 
	{
		Slapi_Entry *remote_entry;

modify_fallback:
		remote_entry = NULL;
		/* Fetch the remote entry */
		rc = windows_get_remote_entry(prp, remote_dn,&remote_entry);
		if (0 == rc && remote_entry) {
			return_value = windows_update_remote_entry(prp,remote_entry,local_entry,is_user);
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
	return return_value;
}

/* 
 * If the local entry is a user, this function is called for moving a node.
 * This is because the leaf RDN of the user on AD corresponds to the value of
 * ntUniqueId in the entry on DS.
 *
 * If the local entry is a group, it is called both for moving and renaming.
 * Group does not have the mapping.  The leaf RDNs are shared between AD
 * and DS.
 */
static ConnResult
process_replay_rename(Private_Repl_Protocol *prp,
					  Slapi_Entry *local_newentry,
					  Slapi_DN *local_origsdn,
					  const char *newrdn,
					  const char *newparent,
					  int deleteoldrdn,
					  int is_user,
					  int is_group)
{
	ConnResult rval = CONN_OPERATION_FAILED;
	char *newsuperior = NULL;
	const Repl_Agmt *winrepl_agmt;
	const char *remote_subtree = NULL; /* Normalized subtree of the remote entry */
	const char *local_subtree = NULL;  /* Normalized subtree of the local entry */
	char *norm_newparent = NULL;
	char *p = NULL;
	char *remote_rdn_val = NULL;
	char *remote_rdn = NULL;
	char *remote_dn = NULL;
	char *local_pndn = NULL;
	
	if (NULL == newparent || NULL == local_origsdn || NULL == local_newentry) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"process_replay_rename: %s is empty\n",
					NULL==newparent?"newparent":NULL==local_origsdn?"local sdn":
					"local entry");
		goto bail;
	}
	if (0 == is_user && 0 == is_group) {
		goto bail; /* nothing to do */
	}

	/* Generate newsuperior for AD */
	winrepl_agmt = prp->agmt;
	remote_subtree =
		slapi_sdn_get_ndn(windows_private_get_windows_subtree(winrepl_agmt));
	local_subtree =
		slapi_sdn_get_ndn(windows_private_get_directory_subtree(winrepl_agmt));
	if (NULL == remote_subtree || NULL == local_subtree ||
		'\0' == *remote_subtree || '\0' == *local_subtree) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"process_replay_rename: local subtree \"%s\" or "
					"remote subtree \"%s\" is empty\n",
					local_subtree?local_subtree:"empty",
					remote_subtree?remote_subtree:"empty");
		goto bail;
	}
	/* newparent is already normzlized; just ignore the case */
	norm_newparent = slapi_ch_strdup(newparent);
	slapi_dn_ignore_case(norm_newparent);
	p = strstr(norm_newparent, local_subtree);
	if (NULL == p) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"process_replay_rename: new superior \"%s\" is not "
					"in the local subtree \"%s\"\n",
					norm_newparent, local_subtree);
		goto bail; /* not in the subtree */
	}
	*p = '\0';
	if (p == norm_newparent) {
		newsuperior = PR_smprintf("%s", remote_subtree);
	} else {
		newsuperior = PR_smprintf("%s%s", norm_newparent, remote_subtree);
	}

	if (is_user) {
		/* Newrdn remains the same when this function is called,
		 * as RDN on AD is CN type. If CN in RDN is modified remotely,
		 * is taken care in modify not in modrdn locally. */
		remote_rdn_val = slapi_entry_attr_get_charptr(local_newentry, "cn");
		if (NULL == remote_rdn_val) {
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"process_replay_rename: local entry \"%s\" has no "
					"ntUserDomainId\n",
					slapi_entry_get_dn_const(local_newentry));
			goto bail;
		}
		remote_rdn = PR_smprintf("cn=%s", remote_rdn_val);
	} else if (is_group) {
		Slapi_RDN rdn = {0};
		const char *dn = slapi_sdn_get_dn(local_origsdn);
		slapi_rdn_set_dn(&rdn, dn);
		remote_rdn = slapi_ch_strdup(slapi_rdn_get_rdn(&rdn));
		slapi_rdn_done(&rdn);
	}

	/* local parent normalized dn */
	local_pndn = /* strdup'ed */
			slapi_dn_parent((const char *)slapi_sdn_get_ndn(local_origsdn));
	p = strstr(local_pndn, local_subtree);
	if (NULL == p) {
		/* Original entry is not in the subtree.
		 * To add the entry after returning from this function,
		 * we set LDAP_NO_SUCH_OBJECT to the ldap error */
		windows_conn_set_error(prp->conn, LDAP_NO_SUCH_OBJECT);
		goto bail;
	}
	*p = '\0';

	/* generate a remote dn */
	remote_dn = PR_smprintf("%s,%s%s", remote_rdn, local_pndn, remote_subtree);

	if (is_user) {
		rval = windows_conn_send_rename(prp->conn, remote_dn,
							remote_rdn, (const char *)newsuperior,
							deleteoldrdn, NULL, NULL /* returned controls */);
	} else {
		rval = windows_conn_send_rename(prp->conn, remote_dn,
							newrdn, (const char *)newsuperior,
							deleteoldrdn, NULL, NULL /* returned controls */);
	}
bail:
	slapi_ch_free_string(&norm_newparent);
	slapi_ch_free_string(&remote_rdn_val);
	slapi_ch_free_string(&remote_rdn);
	slapi_ch_free_string(&remote_dn);
	slapi_ch_free_string(&local_pndn);
	slapi_ch_free_string(&newsuperior);
	return rval;
}

/*
 * Given a changelog entry, construct the appropriate LDAP operations to sync
 * the operation to AD.
 */
ConnResult
windows_replay_update(Private_Repl_Protocol *prp, slapi_operation_parameters *op)
{
	ConnResult return_value = 0;
	int rc = 0;
	char *password = NULL;
	int is_ours = 0;
	int is_ours_force = 0; /* force to operate for RENAME/MODRDN */
	int is_user = 0;
	int is_group = 0;
	Slapi_DN *remote_dn = NULL;
	Slapi_DN *local_dn = NULL;
	Slapi_Entry *local_entry = NULL;
		
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_replay_update\n", 0, 0, 0 );

	local_dn = slapi_sdn_dup( op->target_address.sdn );

	/* Since we have the target uniqueid in the op structure, let's
	 * fetch the local entry here using it. We do not want to search
	 * across tombstone entries unless we are dealing with a delete
	 * operation here since searching across tombstones can be very
	 * inefficient as the tombstones build up.
	 */
	if (op->operation_type != SLAPI_OPERATION_DELETE) {
		rc = windows_get_local_entry_by_uniqueid(prp, op->target_address.uniqueid, &local_entry, 0);
	} else {
		rc = windows_get_local_tombstone_by_uniqueid(prp, op->target_address.uniqueid, &local_entry);
	}

	if (rc) 
	{
		if (SLAPI_OPERATION_MODRDN == op->operation_type) {
			/* Local entry was moved out of the subtree */
			/* We need to remove the entry on AD. */
			rc = windows_get_local_entry_by_uniqueid(prp,
				  op->target_address.uniqueid, &local_entry, 1 /* is_global */);
			if (rc) {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: windows_replay_update: failed to fetch local entry "
					"for %s operation dn=\"%s\"\n",
					agmt_get_long_name(prp->agmt),
					op2string(op->operation_type), 
					REPL_GET_DN(&op->target_address));
				goto error;
			}
			op->operation_type = SLAPI_OPERATION_DELETE;
			is_ours_force = 1;
		} else {
			/* We only searched within the subtree in the agreement, so we should not print
			 * an error if we didn't find the entry and the DN is outside of the agreement scope. */
			if (is_dn_subject_of_agreement_local(local_dn, prp->agmt)) {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: windows_replay_update: failed to fetch local entry for %s operation dn=\"%s\"\n",
					agmt_get_long_name(prp->agmt),
					op2string(op->operation_type), 
					REPL_GET_DN(&op->target_address));
			} else {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: windows_replay_update: Looking at %s operation local dn=\"%s\" (%s)\n",
					agmt_get_long_name(prp->agmt),
					op2string(op->operation_type), 
					REPL_GET_DN(&op->target_address), "ours");
			}
			/* Just bail on this change.  We don't want to do any
			 * further checks since we don't have a local entry. */
			goto error;
		}
	}

	if (is_ours_force) {
		is_ours = is_ours_force;
	} else {
		is_ours = is_subject_of_agreement_local(local_entry, prp->agmt);
	}
	windows_is_local_entry_user_or_group(local_entry,&is_user,&is_group);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: windows_replay_update: Looking at %s operation local dn=\"%s\" (%s,%s,%s)\n",
		agmt_get_long_name(prp->agmt),
		op2string(op->operation_type), 
		REPL_GET_DN(&op->target_address), is_ours ? "ours" : "not ours", 
		is_user ? "user" : "not user", is_group ? "group" : "not group");

	if (is_ours && (is_user || is_group) ) {
		int missing_entry = 0;
		/* Make the entry's DN */
		rc = map_entry_dn_outbound(local_entry,&remote_dn,prp,&missing_entry, 1);
		if (rc || NULL == remote_dn) 
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: windows_replay_update: failed map dn for %s operation dn=\"%s\""
				"rc=%d remote_dn = [%s]\n",
				agmt_get_long_name(prp->agmt),
				op2string(op->operation_type), 
				REPL_GET_DN(&op->target_address),
				rc, remote_dn ? slapi_sdn_get_dn(remote_dn) : "(null)");
			goto error;
		}
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: windows_replay_update: Processing %s operation local dn=\"%s\" remote dn=\"%s\"\n",
			agmt_get_long_name(prp->agmt),
			op2string(op->operation_type), 
			REPL_GET_DN(&op->target_address), slapi_sdn_get_dn(remote_dn));
		switch (op->operation_type) {
		case SLAPI_OPERATION_ADD:
			return_value = process_replay_add(prp,op->p.p_add.target_entry,local_entry,local_dn,remote_dn,is_user,missing_entry,&password);
			break;
		case SLAPI_OPERATION_MODIFY:
			{
				LDAPMod **mapped_mods = NULL;
				char *newrdn = NULL;

				/*
				 * If the magic objectclass and attributes have been added to the entry
				 * to make the entry sync-able, add the entry first, then apply the other
				 * mods
				 */
				if (sync_attrs_added(op->p.p_modify.modify_mods, local_entry)) {
					Slapi_Entry *ad_entry = NULL;

					return_value = process_replay_add(prp,local_entry,local_entry,local_dn,remote_dn,is_user,missing_entry,&password);
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: windows_replay_update: "
									"The modify operation added the sync objectclass and attribute, so "
									"the entry was added to windows - result [%d]\n",
									agmt_get_long_name(prp->agmt), return_value);
					if (return_value) {
						break; /* error adding entry - cannot continue */
					}
					/* the modify op needs the new remote entry, so retrieve it */
					windows_get_remote_entry(prp, remote_dn, &ad_entry);
					slapi_entry_free(ad_entry); /* getting sets windows_private_get_raw_entry */
				}


				windows_map_mods_for_replay(prp,op->p.p_modify.modify_mods, &mapped_mods, is_user, &password);
				if (is_user) {
					winsync_plugin_call_pre_ad_mod_user_mods_cb(prp->agmt,
																windows_private_get_raw_entry(prp->agmt),
																local_dn,
																local_entry,
																op->p.p_modify.modify_mods,
																remote_dn,
																&mapped_mods);
				} else if (is_group) {
					winsync_plugin_call_pre_ad_mod_group_mods_cb(prp->agmt,
																 windows_private_get_raw_entry(prp->agmt),
																 local_dn,
																 local_entry,
																 op->p.p_modify.modify_mods,
																 remote_dn,
																 &mapped_mods);
				}

				/* Check if a naming attribute is being modified. */
				if (windows_check_mods_for_rdn_change(prp, op->p.p_modify.modify_mods, local_entry, remote_dn, &newrdn)) {
					/* Issue MODRDN */
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s: renaming remote entry \"%s\" with new RDN of \"%s\"\n",
							agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn), newrdn);
					return_value = windows_conn_send_rename(prp->conn, slapi_sdn_get_dn(remote_dn),
						newrdn, NULL, 1 /* delete old RDN */,
						NULL, NULL /* returned controls */);
					slapi_ch_free_string(&newrdn);
				}

				/* It's possible that the mapping process results in an empty mod list, in which case we don't bother with the replay */
				if ( mapped_mods == NULL || *(mapped_mods)== NULL )
				{
					return_value = CONN_OPERATION_SUCCESS;
				} else 
				{
					int ldap_op = 0;
					int ldap_result_code = 0;
					if (slapi_is_loglevel_set(SLAPI_LOG_REPL))
					{
						int i = 0;
						slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,"dump mods for replay update:");
						for(i=0;mapped_mods[i];i++)
						{
							slapi_mod_dump(mapped_mods[i],i);
						}
					}
					return_value = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(remote_dn), mapped_mods, NULL, NULL /* returned controls */);
					windows_conn_get_error(prp->conn, &ldap_op, &ldap_result_code);
					if ((return_value != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
						/* op failed but no ldap error code ??? */
						ldap_result_code = LDAP_OPERATIONS_ERROR;
					}
					if (is_user) {
						winsync_plugin_call_post_ad_mod_user_mods_cb(prp->agmt,
																windows_private_get_raw_entry(prp->agmt),
																local_dn, local_entry,
																op->p.p_modify.modify_mods,
																remote_dn, mapped_mods, &ldap_result_code);
					} else if (is_group) {
						winsync_plugin_call_post_ad_mod_group_mods_cb(prp->agmt,
																 windows_private_get_raw_entry(prp->agmt),
																 local_dn, local_entry,
																 op->p.p_modify.modify_mods,
																 remote_dn, mapped_mods, &ldap_result_code);
					}
					/* see if plugin reset success/error condition */
					if ((return_value != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
						return_value = CONN_OPERATION_SUCCESS;
						windows_conn_set_error(prp->conn, ldap_result_code);
					} else if ((return_value == CONN_OPERATION_SUCCESS) && ldap_result_code) {
						return_value = CONN_OPERATION_FAILED;
						windows_conn_set_error(prp->conn, ldap_result_code);
					}
				}
				if (mapped_mods)
				{
					ldap_mods_free(mapped_mods,1);
					mapped_mods = NULL;
				}
			}
			break;
		case SLAPI_OPERATION_DELETE:
			if (delete_remote_entry_allowed(local_entry))
			{
				if (missing_entry) {
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: windows_replay_update: remote entry doesn't exist.  "
						"Skipping operation, dn=\"%s\"\n", agmt_get_long_name(prp->agmt),
						slapi_sdn_get_dn(remote_dn));
				} else {
					return_value = windows_conn_send_delete(prp->conn, slapi_sdn_get_dn(remote_dn), NULL, NULL /* returned controls */);
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: windows_replay_update: deleted remote entry, dn=\"%s\", result=%d\n",
						agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn), return_value);
				}
			} else 
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: windows_replay_update: delete not allowed on remote entry, dn=\"%s\"\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn));
			}
			break;
		case SLAPI_OPERATION_MODRDN:
		/* only move case (newsuperior: ...) comse here since local leaf RDN is
		 * not identical to the remote leaf RDN. */
			{
			return_value = process_replay_rename(prp, local_entry, local_dn,
								op->p.p_modrdn.modrdn_newrdn,
								REPL_GET_DN(&op->p.p_modrdn.modrdn_newsuperior_address),
								op->p.p_modrdn.modrdn_deloldrdn,
								is_user, is_group);
			if (CONN_OPERATION_FAILED == return_value) {
				int operation = 0;
				int error = 0;
				windows_conn_get_error(prp->conn, &operation, &error);
				/* The remote entry is missing. Let's add the renamed entry. */
				if (LDAP_NO_SUCH_OBJECT == error) {
					return_value = process_replay_add(prp,
									local_entry /* target_entry */,
									local_entry, local_dn, remote_dn,
									is_user, missing_entry, &password);
				}
			}
			break;
			}
		default:
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "%s: replay_update: Unknown "
				"operation type %lu found in changelog - skipping change.\n",
				agmt_get_long_name(prp->agmt), op->operation_type);
		}
		if (password) 
		{
			/* We need to have a non-GUID dn in send_password_modify in order to
			 * bind as the user to check if we need to send the password change.
			 * You are supposed to be able to bind using a GUID dn, but it doesn't
			 * seem to work over plain LDAP. */
			if (is_guid_dn(remote_dn)) {
				Slapi_DN *remote_dn_norm = NULL;
				int norm_missing = 0;

				map_entry_dn_outbound(local_entry,&remote_dn_norm,prp,&norm_missing, 0);
				return_value = send_password_modify(remote_dn_norm, password, prp);
				slapi_sdn_free(&remote_dn_norm);
			} else {
				return_value = send_password_modify(remote_dn, password, prp);
			}

			if (return_value)
			{
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"%s: windows_replay_update: update password returned %d\n",
					agmt_get_long_name(prp->agmt), return_value );
			}
		}
		/* If we successfully added an entry, and then subsequently changed
		 * its password, THEN we need to change its status in AD in order
		 * that it can be used (otherwise the user is marked as disabled).
		 * To do this we set this attribute and value:
		 *   userAccountControl: 512
		 * Or, if we added a new entry, we need to change the useraccountcontrol
		 * to make the new user enabled by default
		 * it is assumed that is_user is set for user entries and that only user entries need
		 * accountcontrol values
		 */
		if ((op->operation_type != SLAPI_OPERATION_DELETE) && (return_value == CONN_OPERATION_SUCCESS)
		    && remote_dn && (password || missing_entry) && is_user) {
			return_value = send_accountcontrol_modify(remote_dn, prp, missing_entry);
		}
	} else {
		/* We ignore operations that target entries outside of our sync'ed subtree, or which are not Windows users or groups */
	}
error:
	if (local_entry)
	{
		slapi_entry_free(local_entry);
	}
	if (local_dn)
	{
		slapi_sdn_free (&local_dn);
	}
	if (remote_dn)
	{
		slapi_sdn_free(&remote_dn);
	}
	slapi_ch_free_string(&password);
	return return_value;
}

static int
is_straight_mapped_attr(const char *type, int is_user /* or group */, int is_nt4)
{
	int found = 0;
	size_t offset = 0;
	char *this_attr = NULL;
	char **list = is_user ? (is_nt4 ? nt4_user_matching_attributes : windows_user_matching_attributes) : (is_nt4 ? nt4_group_matching_attributes : windows_group_matching_attributes);
	/* Look for the type in the list of straight mapped attrs for the appropriate object type */
	while ((this_attr = list[offset]))
	{
		if (0 == slapi_attr_type_cmp(this_attr, type, SLAPI_TYPE_CMP_SUBTYPE))
		{
			found = 1;
			break;
		}
		offset++;
	}
	return found;
}

static int
is_single_valued_attr(const char *type)
{
	int found = 0;
	size_t offset = 0;
	char *this_attr = NULL;

	/* Look for the type in the list of single-valued AD attributes */
	while ((this_attr = windows_single_valued_attributes[offset]))
	{
		if (0 == slapi_attr_type_cmp(this_attr, type, SLAPI_TYPE_CMP_SUBTYPE))
		{
			found = 1;
			break;
		}
		offset++;
	}
	return found;
}
		
static void 
windows_map_attr_name(const char *original_type , int to_windows, int is_user, int is_create, char **mapped_type, int *map_dn)
{
	char *new_type = NULL;
	windows_attribute_map *our_map = is_user ? user_attribute_map : group_attribute_map;
	windows_attribute_map *this_map = NULL;
	size_t offset = 0;

	*mapped_type = NULL;

	/* Iterate over the map entries looking for the type we have */
	while((this_map = &(our_map[offset])))
	{
		char *their_name = to_windows ? this_map->windows_attribute_name : this_map->ldap_attribute_name;
		char *our_name = to_windows ? this_map->ldap_attribute_name : this_map->windows_attribute_name;

		if (NULL == their_name)
		{
			/* End of the list */
			break;
		}
		if (0 == slapi_attr_type_cmp(original_type, our_name, SLAPI_TYPE_CMP_SUBTYPE))
		{
			if (!is_create && (this_map->create_type == createonly))
			{
				/* Skip create-only entries if we're not creating */
			} else
			{
				if ( (this_map->map_type == towindowsonly && to_windows) || (this_map->map_type == fromwindowsonly && !to_windows) 
					|| (this_map->map_type == bidirectional) )
				{
					new_type = slapi_ch_strdup(their_name);
					*map_dn = (this_map->attr_type == dnmap);
					break;
				}
			}
		}
		offset++;
	}

	if (new_type)
	{
		*mapped_type = new_type;
	}
}

/* 
 * Make a new entry suitable for the sync destination (indicated by the to_windows argument).
 * Returns the new entry ready to be passed to an LDAP ADD operation, either remote or local.
 * Also returns the plaintext value of any password contained in the original entry (only for the
 * to_windows direction). This is because passwords must be added to entries after they are added in AD.
 * Caller must free the new entry and any password returned.
 */
static int 
windows_create_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *original_entry, Slapi_DN *remote_sdn, Slapi_Entry **remote_entry, char** password) 
{  
	int retval = 0;
	char *entry_string = NULL;
	Slapi_Entry *new_entry = NULL;
	int rc = 0;
	int is_user = 0; 
	int is_group = 0;
	Slapi_Attr *attr = NULL;
	char *username = NULL;
	const char *dn_string = NULL;
	char *fqusername = NULL;
	const char *domain_name = windows_private_get_windows_domain(prp->agmt); 
	int is_nt4 = windows_private_get_isnt4(prp->agmt);

	char *remote_user_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:person\n"
		"objectclass:organizationalperson\n"
		"objectclass:user\n"
		"userPrincipalName:%s\n";

	char *remote_group_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:group\n";

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_create_remote_entry\n", 0, 0, 0 );

	windows_is_local_entry_user_or_group(original_entry,&is_user,&is_group);

	/* Create a new entry */
	/* Give it its DN and samaccountname */
	username = extract_ntuserdomainid_from_entry(original_entry);
	if (NULL == username)
	{
		goto error;
	}
	fqusername = PR_smprintf("%s@%s",username,domain_name);
	dn_string = slapi_sdn_get_dn(remote_sdn);
	if (is_user)
	{
		entry_string = slapi_ch_smprintf(remote_user_entry_template, dn_string, fqusername);
	} else
	{
		entry_string = slapi_ch_smprintf(remote_group_entry_template, dn_string);
	}
	PR_smprintf_free(fqusername);
	if (NULL == entry_string) 
	{
		goto error;
	}
	new_entry = slapi_str2entry(entry_string, 0);
	slapi_ch_free_string(&entry_string);
	if (NULL == new_entry) 
	{
		goto error;
	}
	/* Map the appropriate attributes sourced from the remote entry */
	/* Iterate over the local entry's attributes */
    for (rc = slapi_entry_first_attr(original_entry, &attr); rc == 0;
			rc = slapi_entry_next_attr(original_entry, attr, &attr)) 
	{
		char *type = NULL;
		Slapi_ValueSet *vs = NULL;
		int mapdn = 0;

		slapi_attr_get_type( attr, &type );
		slapi_attr_get_valueset(attr,&vs);

		if ( is_straight_mapped_attr(type,is_user,is_nt4) )
		{
			/* If this attribute is single-valued in AD,
			 * we only want to send the first value. */
			if (is_single_valued_attr(type))
			{
				if (slapi_valueset_count(vs) > 1) {
					int i = 0;
					Slapi_Value *value = NULL;
					Slapi_Value *new_value = NULL;

					i = slapi_valueset_first_value(vs,&value);
					if (i >= 0) {
						/* Dup the first value, trash the valueset, then copy in the dup'd value. */
						new_value = slapi_value_dup(value);
						slapi_valueset_done(vs);
						/* The below hands off the memory to the valueset */
						slapi_valueset_add_value_ext(vs, new_value, SLAPI_VALUE_FLAG_PASSIN);
					}
				}
			}

			/* The initials attribute is a special case.  AD has a constraint
			 * that limits the value length.  If we're sending a change to
			 * the initials attribute to AD, we trim if neccessary.
			 */
			if (0 == slapi_attr_type_cmp(type, "initials", SLAPI_TYPE_CMP_SUBTYPE)) {
				int i = 0;
				const char *initials_value = NULL;
				Slapi_Value *value = NULL;

				i = slapi_valueset_first_value(vs,&value);
				while (i >= 0) {
					initials_value = slapi_value_get_string(value);

					/* If > AD_INITIALS_LENGTH, trim the value */
					if (strlen(initials_value) > AD_INITIALS_LENGTH) {
						char *new_initials = PL_strndup(initials_value, AD_INITIALS_LENGTH);
						/* the below hands off memory */
						slapi_value_set_string_passin(value, new_initials);
						slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: windows_create_remote_entry: "
							"Trimming initials attribute to %d characters.\n",
							agmt_get_long_name(prp->agmt), AD_INITIALS_LENGTH);
					}

					i = slapi_valueset_next_value(vs, i, &value);
				}
			}

			/* copy over the attr values */
			slapi_entry_add_valueset(new_entry,type,vs);
		} else 
		{
			char *new_type = NULL;

			windows_map_attr_name(type , 1 /* to windows */, is_user, 1 /* create */, &new_type, &mapdn);
			if (new_type)
			{
				if (mapdn)
				{
					Slapi_ValueSet *mapped_values = NULL;
					map_dn_values(prp,vs,&mapped_values, 1 /* to windows */,0);
					if (mapped_values) 
					{
						slapi_entry_add_valueset(new_entry,new_type,mapped_values);
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					}
				} else 
				{
					Slapi_Attr *new_attr = NULL;

					/* AD treats cn and streetAddress as a single-valued attributes, while we define
					 * them as multi-valued attribute as it's defined in rfc 4519.  We only
					 * sync the first value to AD to avoid a constraint violation.
					 */
					if ((0 == slapi_attr_type_cmp(new_type, "streetAddress", SLAPI_TYPE_CMP_SUBTYPE)) ||
						(0 == slapi_attr_type_cmp(new_type, "cn", SLAPI_TYPE_CMP_SUBTYPE))) {
						if (slapi_valueset_count(vs) > 1) {
							int i = 0;
							Slapi_Value *value = NULL;
							Slapi_Value *new_value = NULL;

							i = slapi_valueset_first_value(vs,&value);
							if (i >= 0) {
								/* Dup the first value, trash the valueset, then copy
								 * in the dup'd value. */
								new_value = slapi_value_dup(value);
								slapi_valueset_done(vs);
								/* The below hands off the memory to the valueset */
								slapi_valueset_add_value_ext(vs, new_value, SLAPI_VALUE_FLAG_PASSIN);
							}
						}
					}
					
					slapi_entry_add_valueset(new_entry,type,vs);

					/* Reset the type to new_type here.  This is needed since
					 * slapi_entry_add_valueset will create the Slapi_Attrs using
					 * the schema definition, which can reset the type to something
					 * other than the type you pass into it. To be safe, we just
					 * create the attributes with the old type, then reset them. */
					if (slapi_entry_attr_find(new_entry, type, &new_attr) == 0) {
						slapi_attr_set_type(new_attr, new_type);
					}
				}
				slapi_ch_free_string(&new_type);
			}
			/* password mods are treated specially */
			if (0 == slapi_attr_type_cmp(type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD, SLAPI_TYPE_CMP_SUBTYPE) )
			{
				const char *password_value = NULL;
				Slapi_Value *value = NULL;

				slapi_valueset_first_value(vs,&value);
				password_value = slapi_value_get_string(value);
				/* We need to check if the first character of password_value is an 
				 * opening brace since strstr will simply return it's first argument
				 * if it is an empty string. */
				if (password_value && (*password_value == '{')) {
					if (strchr( password_value, '}' )) {
						/* A storage scheme is present.  Check if it's the
						 * clear storage scheme. */
						if ((strlen(password_value) >= PASSWD_CLEAR_PREFIX_LEN + 1) &&
						    (strncasecmp(password_value, PASSWD_CLEAR_PREFIX, PASSWD_CLEAR_PREFIX_LEN) == 0)) {
							/* This password is in clear text.  Strip off the clear prefix
							 * and sync it. */
							*password = slapi_ch_strdup(password_value + PASSWD_CLEAR_PREFIX_LEN);
						} else {
							/* This password is stored in a non-cleartext format.
							 * We can only sync cleartext passwords. */
							slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: windows_create_remote_entry: "
								"Password is already hashed.  Not syncing.\n",
								agmt_get_long_name(prp->agmt));
						}
					} else {
						/* This password doesn't have a storage prefix but
						 * just happens to start with the '{' character.  We'll
						 * assume that it's just a cleartext password without
						 * the proper storage prefix. */
						*password = slapi_ch_strdup(password_value);
					}
				} else {
					/* This password has no storage prefix, or the password is empty */
					*password = slapi_ch_strdup(password_value);
				}
			}

		}
		if (vs) 
		{
			slapi_valueset_free(vs);
			vs = NULL;
		}
	}
	/* NT4 must have the groupType attribute set for groups.  If it is not present, we will
	 * add it here with a value of 2 (global group).
	 */
	if (is_nt4 && is_group)
	{
		Slapi_Attr *ap = NULL;
		if(slapi_entry_attr_find(new_entry, "groupType", &ap))
		{
			/* groupType attribute wasn't found, so we'll add it */
			slapi_entry_attr_set_int(new_entry, "groupType", 2 /* global group */);
		}
	}

	if (remote_entry) 
	{
		*remote_entry = new_entry;
	}
error:
	if (username)
	{
		slapi_ch_free_string(&username);
	}
	if (new_entry) 
	{
		windows_dump_entry("Created new remote entry:\n",new_entry);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_create_remote_entry: %d\n", retval, 0, 0 );
	return retval;
}

#ifdef FOR_DEBUGGING
/* the entry has already been translated, so be sure to search for ntuserid
   and not samaccountname or anything else. */

static Slapi_Entry* 
windows_entry_already_exists(Slapi_Entry *e){

	int rc = 0;
	Slapi_DN *sdn = NULL;
	Slapi_Entry *entry = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_entry_already_exists\n", 0, 0, 0 );

	sdn = slapi_entry_get_sdn(e);
	rc  = slapi_search_internal_get_entry( sdn, NULL, &entry, repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION));

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_entry_already_exists\n", 0, 0, 0 );

	if (rc == LDAP_SUCCESS)
	{
		return entry;
	}
	else
	{
		return NULL;
	}

}
#endif

static int 
windows_delete_local_entry(Slapi_DN *sdn){

	Slapi_PBlock *pb = NULL;
	int return_value = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_delete_local_entry\n", 0, 0, 0 );

	pb = slapi_pblock_new();
	slapi_delete_internal_set_pb(pb, slapi_sdn_get_dn(sdn), NULL, NULL, repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
	slapi_delete_internal_pb(pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);
	slapi_pblock_destroy(pb);

	if (return_value) {
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"delete operation on local entry %s returned: %d\n", slapi_sdn_get_dn(sdn), return_value);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_delete_local_entry: %d\n", return_value, 0, 0 );

	return return_value;
}

/*
  Before we send the modify to AD, we need to check to see if the mod still
  applies - the entry in AD may have been modified, and those changes not sync'd
  back to the DS, since the way winsync currently works is that it polls periodically
  using DirSync for changes in AD - note that this does not guarantee that the mod
  will apply cleanly, since there is still a small window of time between the time
  we read the entry from AD and the time the mod op is sent, but doing this check
  here should substantially reduce the chances of these types of out-of-sync problems

  If we do find a mod that does not apply cleanly, we just discard it and log an
  error message to that effect.
*/
static int
mod_already_made(Private_Repl_Protocol *prp, Slapi_Mod *smod, Slapi_Entry *ad_entry)
{
	int retval = 0;
	int op = 0;
	const char *type = NULL;

	if (!slapi_mod_isvalid(smod)) { /* bogus */
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: mod_already_made: "
						"modify operation is null - skipping.\n",
						agmt_get_long_name(prp->agmt));
		return 1;
	}

	if (!ad_entry) { /* mods cannot already have been made */
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: mod_already_made: "
						"AD entry not found\n",
						agmt_get_long_name(prp->agmt));
		return retval; /* just allow - will probably fail later if entry really doesn't exist */
	}

	op = slapi_mod_get_operation(smod);
	type = slapi_mod_get_type(smod);
	if (SLAPI_IS_MOD_ADD(op)) { /* make sure value is not there */
		struct berval *bv = NULL;
		for (bv = slapi_mod_get_first_value(smod);
			 bv; bv = slapi_mod_get_next_value(smod)) {
			Slapi_Value *sv = slapi_value_new();
			slapi_value_init_berval(sv, bv); /* copies bv_val */
			if (slapi_entry_attr_has_syntax_value(ad_entry, type, sv)) {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: mod_already_made: "
								"remote entry attr [%s] already has value [%s] - will not send.\n",
								agmt_get_long_name(prp->agmt), type,
								slapi_value_get_string(sv));
				slapi_mod_remove_value(smod); /* removes the value at the current iterator pos */
			}
			slapi_value_free(&sv);
		}
		/* if all values were removed, no need to send the mod */
		if (slapi_mod_get_num_values(smod) == 0) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: mod_already_made: "
							"remote entry attr [%s] had all mod values removed - will not send.\n",
							agmt_get_long_name(prp->agmt), type);
			retval = 1;
		}
	} else if (SLAPI_IS_MOD_DELETE(op)) { /* make sure value or attr is there */
		Slapi_Attr *attr = NULL;

		/* if attribute does not exist, no need to send the delete */
		if (slapi_entry_attr_find(ad_entry, type, &attr) || !attr) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: mod_already_made: "
							"remote entry attr [%s] already deleted - will not send.\n",
							agmt_get_long_name(prp->agmt), type);
			retval = 1;
		} else if (slapi_mod_get_num_values(smod) > 0) {
			/* if attr exists, remove mods that have already been applied */
			struct berval *bv = NULL;
			for (bv = slapi_mod_get_first_value(smod);
				 bv; bv = slapi_mod_get_next_value(smod)) {
				Slapi_Value *sv = slapi_value_new();
				slapi_value_init_berval(sv, bv); /* copies bv_val */
				if (!slapi_entry_attr_has_syntax_value(ad_entry, type, sv)) {
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: mod_already_made: "
									"remote entry attr [%s] already deleted value [%s] - will not send.\n",
									agmt_get_long_name(prp->agmt), type,
									slapi_value_get_string(sv));
					slapi_mod_remove_value(smod); /* removes the value at the current iterator pos */
				}
				slapi_value_free(&sv);
			}
			/* if all values were removed, no need to send the mod */
			if (slapi_mod_get_num_values(smod) == 0) {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: mod_already_made: "
								"remote entry attr [%s] had all mod values removed - will not send.\n",
								agmt_get_long_name(prp->agmt), type);
				retval = 1;
			}
		} /* else if no values specified, this means delete the attribute */
	} else { /* allow this mod */
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: mod_already_made: "
						"skipping mod op [%d]\n",
						agmt_get_long_name(prp->agmt), op);
	}

	/* If the mod shouldn't be skipped, we should
	 * apply it to the entry that was passed in.  This
	 * allows a single modify operation with multiple
	 * mods to take prior mods into account when
	 * determining what can be skipped. */
	if (retval == 0) {
		slapi_entry_apply_mod(ad_entry, (LDAPMod *)slapi_mod_get_ldapmod_byref(smod));
	}

	return retval;
}

/*
 * Comparing the local_dn and the mapped_dn, return the newsuperior.
 * If to_windows is non-zero, the newsuperior is for the entry on AD.
 * If to_windows is 0, the newsuperior is on DS.
 *
 * Caller must be responsible to free newsuperior.
 *
 * Return value: 0 if successful
 *               non zero (and *newsuperior is NULL) if failed
 */
static int
windows_get_superior_change(Private_Repl_Protocol *prp,
							Slapi_DN *local_dn, Slapi_DN *mapped_dn,
							char **newsuperior, int to_windows)
{
	const Repl_Agmt *winrepl_agmt;
	const char *mapped_ndn = NULL;     /* Normalized dn of the remote entry */
	const char *local_ndn = NULL;      /* Normalized dn of the local entry */
	char *mapped_pndn = NULL;    /* Normalized parent dn of the remote entry */
	char *local_pndn = NULL;     /* Normalized parent dn of the local entry */
	const char *remote_subtree = NULL; /* Normalized subtree of the remote entry */
	const char *local_subtree = NULL;  /* Normalized subtree of the local entry */
	char *ptr = NULL;
	int rc = -1;

	if (NULL == newsuperior) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_get_superior_change: newsuperior is NULL\n");
		goto bail;
	}

	/* If the local and mapped DNs are the same, no rename is needed. */
	if (slapi_sdn_compare(local_dn, mapped_dn) == 0) {
		*newsuperior = NULL;
		rc = 0;
	}

	/* Check if modrdn with superior has happened on AD */
	winrepl_agmt = prp->agmt;
	remote_subtree =
		slapi_sdn_get_ndn(windows_private_get_windows_subtree(winrepl_agmt));
	local_subtree =
		slapi_sdn_get_ndn(windows_private_get_directory_subtree(winrepl_agmt));
	if (NULL == remote_subtree || NULL == local_subtree ||
		'\0' == *remote_subtree || '\0' == *local_subtree) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_get_superior_change: local subtree \"%s\" or "
					"remote subtree \"%s\" is empty\n",
					local_subtree?local_subtree:"empty",
					remote_subtree?remote_subtree:"empty");
		goto bail;
	}
	mapped_ndn = slapi_sdn_get_ndn(mapped_dn);
	local_ndn = slapi_sdn_get_ndn(local_dn);
	if (NULL == mapped_ndn || NULL == local_ndn ||
		'\0' == *mapped_ndn || '\0' == *local_ndn) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_get_superior_change: local dn \"%s\" or "
					"mapped dn \"%s\" is empty\n",
					local_ndn?local_ndn:"empty", mapped_ndn?mapped_ndn:"empty");
		goto bail;
	}
	mapped_pndn = slapi_dn_parent((const char *)mapped_ndn); /* strdup'ed */
	local_pndn = slapi_dn_parent((const char *)local_ndn);   /* strdup'ed */
	if (NULL == mapped_pndn || NULL == local_pndn ||
		'\0' == *mapped_pndn || '\0' == *local_pndn) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_get_superior_change: local parent dn \"%s\" or "
					"remote parent dn \"%s\" is empty\n",
					local_pndn?local_pndn:"empty",
					mapped_pndn?mapped_pndn:"empty");
		goto bail;
	}
	ptr = strstr(mapped_pndn, local_subtree);
	if (ptr) {
		*ptr = '\0'; /* if ptr != mapped_pndn, mapped_pndn ends with ',' */
		ptr = strstr(local_pndn, local_subtree);
		if (ptr) {
			*ptr = '\0'; /* if ptr != local_pndn, local_pndn ends with ',' */
			if (0 != strcmp(mapped_pndn, local_pndn)) {
				/* the remote parent is different from the local parent */
				if (to_windows) {
					*newsuperior = slapi_create_dn_string("%s%s", local_pndn, remote_subtree);
				} else {
					*newsuperior = slapi_create_dn_string("%s%s", mapped_pndn, local_subtree);
				}
				rc = 0;
			}
		} else {
			slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"windows_get_superior_change: local parent \"%s\" is not in "
				"DirectoryReplicaSubtree \"%s\"\n", local_pndn, local_subtree);
		}
	} else {
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"windows_get_superior_change: mapped parent \"%s\" is not in "
			"DirectoryReplicaSubtree \"%s\"\n", mapped_pndn, local_subtree);
	}
bail:
	slapi_ch_free_string(&mapped_pndn);
	slapi_ch_free_string(&local_pndn);

	return rc;
}

static int
windows_check_mods_for_rdn_change(Private_Repl_Protocol *prp, LDAPMod **original_mods, 
		Slapi_Entry *local_entry, Slapi_DN *remote_dn, char **newrdn)
{
	int ret = 0;
	int need_rename = 0;
	int got_entry = 0;
	Slapi_Entry *remote_entry = NULL;
	Slapi_Attr *remote_rdn_attr = NULL;
	Slapi_Value *remote_rdn_val = NULL;
	Slapi_Mods smods = {0};
	Slapi_Mod *smod = slapi_mod_new();
	Slapi_Mod *last_smod = smod;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_check_mods_for_rdn_change\n", 0, 0, 0 );

	/* Iterate through the original mods, looking for a modification to the RDN attribute */
	slapi_mods_init_byref(&smods, original_mods);
	smod = slapi_mods_get_first_smod(&smods, last_smod);
	while(smod) {
		/* Check if this is modifying the naming attribute (cn) */
		if (slapi_attr_types_equivalent(slapi_mod_get_type(smod), "cn")) {
			/* Fetch the remote entry so we can compare the new values
			 * against the existing remote value.  We only need to do
			 * this once for all mods. */
			if (!got_entry) {
				int free_entry = 0;

				/* See if we have already fetched the remote entry.
				 * If not, we just fetch it ourselves. */
				if ((remote_entry = windows_private_get_raw_entry(prp->agmt)) == NULL) {
					windows_get_remote_entry(prp, remote_dn, &remote_entry);
					free_entry = 1;
				}

				if (remote_entry) {
					/* Fetch and duplicate the cn attribute so we can perform comparisions */
					slapi_entry_attr_find(remote_entry, "cn", &remote_rdn_attr);
					if (remote_rdn_attr) {
						remote_rdn_attr = slapi_attr_dup(remote_rdn_attr);
						slapi_attr_first_value(remote_rdn_attr, &remote_rdn_val);
					}

					/* We only want to free the entry if we fetched it ourselves
					 * by calling windows_get_remote_entry(). */
					if (free_entry) {
						slapi_entry_free(remote_entry);
					}
				}
				got_entry = 1;

				/* If we didn't get the remote value for some odd reason, just bail out. */
				if (!remote_rdn_val) {
					slapi_mod_done(smod);
					goto done;
				}
			}

			if (SLAPI_IS_MOD_REPLACE(slapi_mod_get_operation(smod))) {
				/* For a replace, we just need to check if the old value that AD
				 * has is still present after the operation.  If not, we rename
				 * the entry in AD using the first new value as the RDN. */
				Slapi_Value *new_val = NULL;
				struct berval *bval = NULL;

				/* Assume that we're going to need to do a rename. */
				ret = 1;

				/* Get the first new value, which is to be used as the RDN if we decide
				 * that a rename is necessary. */
				bval = slapi_mod_get_first_value(smod);
				if (bval && bval->bv_val) {
					/* Fill in new RDN to return to caller. */
					slapi_ch_free_string(newrdn);
					*newrdn = slapi_ch_smprintf("cn=%s", bval->bv_val);

					/* Loop through all new values to check if they match
					 * the value present in AD. */
					do {
						new_val = slapi_value_new_berval(bval);
						if (slapi_value_compare(remote_rdn_attr, remote_rdn_val, new_val) == 0) {
							/* We have a match.  This means we don't want to rename the entry in AD. */
							slapi_ch_free_string(newrdn);
							slapi_value_free(&new_val);
							ret = 0;
							break;
						}
						slapi_value_free(&new_val);
						bval = slapi_mod_get_next_value(smod);
					} while (bval && bval->bv_val);
				}
			} else if (SLAPI_IS_MOD_DELETE(slapi_mod_get_operation(smod))) {
				/* We need to check if the cn in AD is the value being deleted.  If
				 * so, set a flag saying that we will need to do a rename.  We will either
				 * get a new value added from another mod in this op, or we will need to
				 * use an old value that is left over after the delete operation. */
				if (slapi_mod_get_num_values(smod) == 0) {
					/* All values are being deleted, so a rename will be needed.  One
					 * of the other mods will be adding the new values(s). */
					need_rename = 1;
				} else {
					Slapi_Value *del_val = NULL;
					struct berval *bval = NULL;

					bval = slapi_mod_get_first_value(smod);
					while (bval && bval->bv_val) {
						/* Is this value the same one that is used as the RDN in AD? */
						del_val = slapi_value_new_berval(bval);
						if (slapi_value_compare(remote_rdn_attr, remote_rdn_val, del_val) == 0) {
							/* We have a match.  This means we need to rename the entry in AD. */
							need_rename = 1;
							slapi_value_free(&del_val);
							break;
						}
						slapi_value_free(&del_val);
						bval = slapi_mod_get_next_value(smod);
					}
				}
			} else if (SLAPI_IS_MOD_ADD(slapi_mod_get_operation(smod))) {
				/* We only need to care about an add if the old value was deleted. */
				if (need_rename) {
					/* Just grab the first new value and use it to create the new RDN. */
					struct berval *bval = slapi_mod_get_first_value(smod);

					if (bval && bval->bv_val) {
						/* Fill in new RDN to return to caller. */
						slapi_ch_free_string(newrdn);
						*newrdn = slapi_ch_smprintf("cn=%s", bval->bv_val);
						need_rename = 0;
						ret = 1;
					}
				}
			}
		}

		/* Get the next mod from this op. */
		slapi_mod_done(smod);

		/* Need to prevent overwriting old smod with NULL return value and causing a leak. */
		smod = slapi_mods_get_next_smod(&smods, last_smod);
	}

done:
	/* We're done with the mods and the copied cn attr from the remote entry. */
	slapi_attr_free(&remote_rdn_attr);
	if (last_smod) {
		slapi_mod_free(&last_smod);
	}
	slapi_mods_done (&smods);

	if (need_rename) {
		/* We need to perform a rename, but we didn't get the value for the
		 * new RDN from this operation.  We fetch the first value from the local
		 * entry to create the new RDN. */
		if (local_entry) {
			char *newval = slapi_entry_attr_get_charptr(local_entry, "cn");
			if (newval) {
				/* Fill in new RDN to return to caller. */
				slapi_ch_free_string(newrdn);
				*newrdn = slapi_ch_smprintf("cn=%s", newval);
				slapi_ch_free_string(&newval);
				ret = 1;
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_check_mods_for_rdn_change: %d\n", ret, 0, 0 );

	return ret;
}


static void 
windows_map_mods_for_replay(Private_Repl_Protocol *prp,LDAPMod **original_mods, LDAPMod ***returned_mods, int is_user, char** password) 
{
	Slapi_Mods smods = {0};
	Slapi_Mods mapped_smods = {0};
	LDAPMod *mod = NULL;
	int is_nt4 = windows_private_get_isnt4(prp->agmt);
	Slapi_Mod *mysmod = NULL;
	const Slapi_Entry *ad_entry = NULL;
	Slapi_Entry *ad_entry_copy = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_map_mods_for_replay\n", 0, 0, 0 );

	/* Iterate through the original mods, looking each attribute type up in the maps for either user or group */

	/* Make a copy of the AD entry. */
	ad_entry = windows_private_get_raw_entry(prp->agmt);
	if (ad_entry) {
		ad_entry_copy = slapi_entry_dup(ad_entry);
	}
	
	slapi_mods_init_byref(&smods, original_mods);
	slapi_mods_init(&mapped_smods,10);
	mod = slapi_mods_get_first_mod(&smods);
	while(mod)
	{
		char *attr_type = mod->mod_type;
		int mapdn = 0;

		/* Check to see if this attribute is passed through */
		if (is_straight_mapped_attr(attr_type,is_user,is_nt4)) {
			/* If this attribute is single-valued in AD,
			 * we only want to send the first value. */
			if (is_single_valued_attr(attr_type)) {
				Slapi_Mod smod;

				slapi_mod_init_byref(&smod,mod);

				/* Check if there is more than one value */
				if (slapi_mod_get_num_values(&smod) > 1) {
					slapi_mod_get_first_value(&smod);
					/* Remove all values except for the first */
					while (slapi_mod_get_next_value(&smod)) {
						/* This modifies the bvalues in the mod itself */
						slapi_mod_remove_value(&smod);
					}
				}

				slapi_mod_done(&smod);
			}

			/* The initials attribute is a special case.  AD has a constraint
			 * that limits the value length.  If we're sending a change to
			 * the initials attribute to AD, we trim if neccessary.
			 */
			if (0 == slapi_attr_type_cmp(attr_type, "initials", SLAPI_TYPE_CMP_SUBTYPE)) {
				int i;
				for (i = 0; mod->mod_bvalues[i] != NULL; i++) {
					/* If > AD_INITIALS_LENGTH, trim the value */
					if (mod->mod_bvalues[i]->bv_len > AD_INITIALS_LENGTH) {
						mod->mod_bvalues[i]->bv_val[AD_INITIALS_LENGTH] = '\0';
						mod->mod_bvalues[i]->bv_len = AD_INITIALS_LENGTH;
						slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: windows_map_mods_for_replay: "
							"Trimming initials attribute to %d characters.\n",
							agmt_get_long_name(prp->agmt), AD_INITIALS_LENGTH);
					}
				}
			}

			/* create the new smod to add to the mapped_smods */
			mysmod = slapi_mod_new();
			slapi_mod_init_byval(mysmod, mod); /* copy contents */
		} else 
		{
			char *mapped_type = NULL;
			/* Check if this mod has its attribute type mapped */
			windows_map_attr_name(attr_type,1,is_user,0,&mapped_type, &mapdn);
			if (mapped_type)
			{
				/* If so copy over the mod with new type name */
				if (mapdn)
				{
					Slapi_ValueSet *mapped_values = NULL;
					Slapi_ValueSet *vs = NULL;
					Slapi_Mod smod;
                        
					vs = slapi_valueset_new();
					slapi_mod_init_byref(&smod,mod);
					slapi_valueset_set_from_smod(vs, &smod);
					map_dn_values(prp,vs,&mapped_values, 1 /* to windows */,0);
					if (mapped_values) 
					{
						mysmod = slapi_mod_new();
						slapi_mod_init_valueset_byval(mysmod, mod->mod_op, mapped_type, mapped_values);
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					} else 
					{
						/* this might be a del: mod, in which case there are no values */
						if (mod->mod_op & LDAP_MOD_DELETE)
						{
							mysmod = slapi_mod_new();
							slapi_mod_init(mysmod, 0);
							slapi_mod_set_operation(mysmod, LDAP_MOD_DELETE|LDAP_MOD_BVALUES);
							slapi_mod_set_type(mysmod, mapped_type);
						}
					}
					slapi_mod_done(&smod);
					slapi_valueset_free(vs);
				} else 
				{
					/* If this attribute is single-valued in AD,
					 * we only want to send the first value. */
					if (is_single_valued_attr(mapped_type)) {
						Slapi_Mod smod;

						slapi_mod_init_byref(&smod,mod);

						/* Check if there is more than one value */
						if (slapi_mod_get_num_values(&smod) > 1) {
							slapi_mod_get_first_value(&smod);
							/* Remove all values except for the first */
							while (slapi_mod_get_next_value(&smod)) {
								/* This modifies the bvalues in the mod itself */
								slapi_mod_remove_value(&smod);
							}
						}

						slapi_mod_done(&smod);
					}

					/* create the new smod to add to the mapped_smods */
					mysmod = slapi_mod_new();
					slapi_mod_init_byval(mysmod, mod); /* copy contents */
					slapi_mod_set_type(mysmod, mapped_type);
				}
				slapi_ch_free_string(&mapped_type);
			} else 
			{
				/* password mods are treated specially */
				if ((0 == slapi_attr_type_cmp(attr_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD, SLAPI_TYPE_CMP_SUBTYPE)) &&
					mod && mod->mod_bvalues && mod->mod_bvalues[0] && mod->mod_bvalues[0]->bv_val)
				{
					char *password_value = NULL;
					password_value = mod->mod_bvalues[0]->bv_val;
					/* We need to check if the first character of password_value is an 
					 * opening brace since strstr will simply return it's first argument
					 * if it is an empty string. */
					if (password_value && (*password_value == '{')) {
						if (strchr( password_value, '}' )) {
							/* A storage scheme is present.  Check if it's the
							 * clear storage scheme. */
							if ((strlen(password_value) >= PASSWD_CLEAR_PREFIX_LEN + 1) &&
							     (strncasecmp(password_value, PASSWD_CLEAR_PREFIX, PASSWD_CLEAR_PREFIX_LEN) == 0)) {
								/* This password is in clear text.  Strip off the clear prefix
								 * and sync it. */
								*password = slapi_ch_strdup(password_value + PASSWD_CLEAR_PREFIX_LEN);
							} else {
								/* This password is stored in a non-cleartext format.
								 * We can only sync cleartext passwords. */
								slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: windows_create_remote_entry: "
									"Password is already hashed.  Not syncing.\n",
									agmt_get_long_name(prp->agmt));
							}
						} else {
							/* This password doesn't have a storage prefix but
							 * just happens to start with the '{' character.  We'll
							 * assume that it's just a cleartext password without
							 * the proper storage prefix. */
							*password = slapi_ch_strdup(password_value);
						}
					} else {
						/* This password has no storage prefix, or the password is empty */
						*password = slapi_ch_strdup(password_value);
					}
				}
			}
		}
		/* Otherwise we do not copy this mod at all */
		if (mysmod && !mod_already_made(prp, mysmod, ad_entry_copy)) { /* make sure this mod is still valid to send */
			slapi_mods_add_ldapmod(&mapped_smods, slapi_mod_get_ldapmod_passout(mysmod));
		}
		if (mysmod) {
			slapi_mod_free(&mysmod);
		}
			
		mod = slapi_mods_get_next_mod(&smods);
	}

	slapi_entry_free(ad_entry_copy);
	slapi_mods_done (&smods);
	/* Extract the mods for the caller */
	*returned_mods = slapi_mods_get_ldapmods_passout(&mapped_smods);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_map_mods_for_replay\n", 0, 0, 0 );
}


/* Returns non-zero if the attribute value sets are identical. If you want to
 * compare the entire attribute value, set n to 0.  You can compare only the
 * first n characters of the values by passing in the legth as n. */
static int
attr_compare_equal(Slapi_Attr *a, Slapi_Attr *b, int n)
{
	/* For now only handle single values */
	Slapi_Value *va = NULL;
	Slapi_Value *vb = NULL;
	int num_a = 0;
	int num_b = 0;
	int match = 1;

	slapi_attr_get_numvalues(a,&num_a);
	slapi_attr_get_numvalues(b,&num_b);

	if (num_a == num_b) {
		slapi_attr_first_value(a, &va);
		slapi_attr_first_value(b, &vb);

		/* If either val is less than n, then check if the length, then values are
		 * equal.  If both are n or greater, then only compare the first n chars. 
		 * If n is 0, then just compare the entire attribute. */
		if ((va->bv.bv_len < n) || (vb->bv.bv_len < n) || (n == 0)) {
			if (va->bv.bv_len == vb->bv.bv_len) {
				if (slapi_attr_value_find(b, slapi_value_get_berval(va)) != 0) {
					match = 0;
				}
			} else {
				match = 0;
			}
		} else if (0 != memcmp(va->bv.bv_val, vb->bv.bv_val, n)) {
			match = 0;
		}
	} else {
		match = 0;
	}
	return match;
}

/* Returns non-zero if all of the values of attribute a are contained in attribute b.
 * You can compare only the first n characters of the values by passing in the length
 * as n.  If you want to compare the entire attribute value, set n to 0. */
static int
attr_compare_present(Slapi_Attr *a, Slapi_Attr *b, int n)
{
	int match = 1;
	int i = 0;
	int j = 0;
	Slapi_Value *va = NULL;
	Slapi_Value *vb = NULL;

	/* Iterate through values in attr a and search for each in attr b */
	for (i = slapi_attr_first_value(a, &va); va && (i != -1);
	     i = slapi_attr_next_value(a, i, &va)) {
		if (n == 0) {
			/* Compare the entire attribute value */
			if (slapi_attr_value_find(b, slapi_value_get_berval(va)) != 0) {
				match = 0;
				goto bail;
			}
		} else {
			/* Only compare up the values up to the specified length. */
			int found = 0;

			for (j = slapi_attr_first_value(b, &vb); vb && (j != -1);
				j = slapi_attr_next_value(b, j, &vb)) {
				/* If either val is less than n, then check if the length, then values are
				 * equal.  If both are n or greater, then only compare the first n chars. */
				if ((va->bv.bv_len < n) || (vb->bv.bv_len < n)) {
					if (va->bv.bv_len == vb->bv.bv_len) {
						if (0 == memcmp(va->bv.bv_val, vb->bv.bv_val, va->bv.bv_len)) {
							found = 1;
						}
					}
				} else if (0 == memcmp(va->bv.bv_val, vb->bv.bv_val, n)) {
					found = 1;
				}
			}

			/* If we didn't find this value from attr a in attr b, we're done. */
			if (!found) {
				match = 0;
				goto bail;
			}
		}
	}

bail:
	return match;
}


/* Helper functions for dirsync result processing */

/* Is this entry a tombstone ? */
static int 
is_tombstone(Private_Repl_Protocol *prp, Slapi_Entry *e)
{
	int retval = 0;

	if ( (slapi_filter_test_simple( e, (Slapi_Filter*)windows_private_get_deleted_filter(prp->agmt) ) == 0) )
	{
		retval = 1;
	}

	return retval;
}

#define ENTRY_NOTFOUND -1
#define ENTRY_NOT_UNIQUE -2

/* Search for an entry in AD */
static int
find_entry_by_attr_value_remote(const char *attribute, const char *value, Slapi_Entry **e,  Private_Repl_Protocol *prp)
{
	int retval = 0;
	ConnResult cres = 0;
	char *filter = NULL;
	const char *searchbase = NULL;
	Slapi_Entry *found_entry = NULL;
	char *filter_escaped_value = NULL;
	size_t vallen = 0;

	vallen = value ? strlen(value) : 0;
	filter_escaped_value = slapi_ch_calloc(sizeof(char), vallen*3+1);
	/* should not have to escape attribute names */
	filter = PR_smprintf("(%s=%s)",attribute,escape_filter_value(value, vallen, filter_escaped_value));
	slapi_ch_free_string(&filter_escaped_value);
	searchbase = slapi_sdn_get_dn(windows_private_get_windows_subtree(prp->agmt));
	cres = windows_search_entry(prp->conn, (char*)searchbase, filter, &found_entry);
	if (cres)
	{
		retval = -1;
	} else
		{
		if (found_entry)
		{
			*e = found_entry;
		}
	}
	if (filter)
	{
		PR_smprintf_free(filter);
		filter = NULL;
	}
	return retval;
}

/* Search for an entry in AD by DN */
static int
windows_get_remote_entry (Private_Repl_Protocol *prp, const Slapi_DN* remote_dn,Slapi_Entry **remote_entry)
{
	int retval = 0;
	ConnResult cres = 0;
	char *filter = "(objectclass=*)";
	const char *searchbase = NULL;
	Slapi_Entry *found_entry = NULL;

	searchbase = slapi_sdn_get_dn(remote_dn);
	cres = windows_search_entry_ext(prp->conn, (char*)searchbase, filter, &found_entry, NULL, LDAP_SCOPE_BASE);
	if (cres)
	{
		retval = -1;
	} else
	{
		if (found_entry)
		{
			*remote_entry = found_entry;
		}
	}
	return retval;
}

/* Search for a tombstone entry in AD by DN */
static int
windows_get_remote_tombstone (Private_Repl_Protocol *prp, const Slapi_DN* remote_dn,Slapi_Entry **remote_entry)
{
	int retval = 0;
	ConnResult cres = 0;
	char *filter = "(objectclass=*)";
	const char *searchbase = NULL;
	Slapi_Entry *found_entry = NULL;
	LDAPControl *server_controls[2];

	/* We need to send the "Return Deleted Objects" control to search
	 * for tombstones. */
	slapi_build_control(REPL_RETURN_DELETED_OBJS_CONTROL_OID, NULL, PR_TRUE,
			&server_controls[0]);
	server_controls[1] = NULL;

	searchbase = slapi_sdn_get_dn(remote_dn);
	cres = windows_search_entry_ext(prp->conn, (char*)searchbase, filter,
									&found_entry, server_controls, LDAP_SCOPE_SUBTREE);
	if (cres) {
		retval = -1;
	} else {
		if (found_entry) {
			*remote_entry = found_entry;
		}
	}

	ldap_control_free(server_controls[0]);
	return retval;
}

/* Reanimate a tombstone in AD.  Returns 0 on success, otherwise you get the
 * LDAP return code from the modify operation.  */
static int
windows_reanimate_tombstone(Private_Repl_Protocol *prp, const Slapi_DN* tombstone_dn, const char* new_dn)
{
	int retval = 0;
	LDAPControl *server_controls[2];
	Slapi_Mods smods = {0};

	/* We need to send the "Return Deleted Objects" control to modify
	 * tombstone entries. */
	slapi_build_control(REPL_RETURN_DELETED_OBJS_CONTROL_OID, NULL, PR_TRUE,
			&server_controls[0]);
	server_controls[1] = NULL;

	/* To reanimate a tombstone in AD, you need to send a modify
	 * operation that does two things.  It must remove the isDeleted
	 * attribute from the entry and it must modify the DN.  This DN
	 * does not have to be the same place in the tree that the entry
	 * previously existed. */ 
	slapi_mods_init (&smods, 0);
	slapi_mods_add_mod_values(&smods, LDAP_MOD_DELETE, "isDeleted", NULL);
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "distinguishedName", new_dn);

	retval = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(tombstone_dn),
				 slapi_mods_get_ldapmods_byref(&smods), server_controls, NULL );

	slapi_mods_done(&smods);
	ldap_control_free(server_controls[0]);
	return retval;
}

static int
find_entry_by_attr_value(const char *attribute, const char *value, Slapi_Entry **e,  const Repl_Agmt *ra)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    Slapi_Entry **entries = NULL, **ep = NULL;
	Slapi_Entry *entry_found = NULL;
    char *query = NULL;
	int found_or_not = ENTRY_NOTFOUND;
	int rval = 0;
	const char *subtree_dn = NULL;
	int not_unique = 0;
	char *subtree_dn_copy = NULL;
	int scope = LDAP_SCOPE_SUBTREE;
	char **attrs = NULL;
	LDAPControl **server_controls = NULL;
	char *filter_escaped_value = NULL;
	size_t vallen = 0;

    if (pb == NULL)
        goto done;

    vallen = value ? strlen(value) : 0;
    filter_escaped_value = slapi_ch_calloc(sizeof(char), vallen*3+1);
    /* should not have to escape attribute names */
    query = slapi_ch_smprintf("(%s=%s)", attribute, escape_filter_value(value, vallen, filter_escaped_value));
    slapi_ch_free_string(&filter_escaped_value);

    if (query == NULL)
		goto done;

	subtree_dn = slapi_sdn_get_dn(windows_private_get_directory_subtree(ra));
	subtree_dn_copy = slapi_ch_strdup(subtree_dn);

    winsync_plugin_call_pre_ds_search_entry_cb(ra, NULL, &subtree_dn_copy, &scope, &query,
                                               &attrs, &server_controls);

    slapi_search_internal_set_pb(pb, subtree_dn_copy,
        scope, query, attrs, 0, server_controls, NULL,
        (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free_string(&subtree_dn_copy);
    slapi_ch_free_string(&query);
    slapi_ch_array_free(attrs);
    attrs = NULL;
    ldap_controls_free(server_controls);
    server_controls = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rval);
    if (rval != LDAP_SUCCESS)
	{
		goto done;
	}

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if ((entries == NULL) || (entries[0] == NULL))
	{
		goto done;
	}
	entry_found = entries[0];
	for (ep = entries; *ep; ep++) {
		if (not_unique)
		{
			found_or_not = ENTRY_NOT_UNIQUE;
		}
		not_unique = 1;
	}
done:
	if (entry_found && (found_or_not != ENTRY_NOT_UNIQUE))
	{
		found_or_not = 0;
		*e = slapi_entry_dup(entry_found);
	}
	if (pb)
	{
		slapi_free_search_results_internal(pb);
		slapi_pblock_destroy(pb);
	}
	return found_or_not;
}

static int
find_entry_by_username(const char *username, Slapi_Entry **e, const Repl_Agmt *ra)
{
		return find_entry_by_attr_value("ntUserDomainId",username,e,ra);
}

/* Find an entry in the local server given its GUID, or return ENTRY_NOTFOUND */
static int
find_entry_by_guid(const char *guid, Slapi_Entry **e, const Repl_Agmt *ra)
{
		return find_entry_by_attr_value("ntUniqueId",guid,e,ra);
}

/* Remove dashes from a GUID string. */
static void
dedash_guid(char *str)
{
	char *p = str;
	char c = '\0';

	while ((c = *p))
	{
		if ('-' == c)
		{
			/* Move on down please */
			char *q = p;
			char *r = q + 1;
			while (*r) 
			{
				*q++ = *r++;
			}
			*q = '\0';
		} 
		p++;
	}
}

/* Add dashes into a GUID string.  If the guid is not formatted properly,
 * we will free it and set the pointer to NULL. */
static void
dash_guid(char **str)
{
	if (strlen(*str) == NTUNIQUEID_LENGTH) {
		char *p = NULL;
		/* Add extra room for the dashes */
		*str = slapi_ch_realloc(*str, AD_GUID_LENGTH + 1);

		/* GUID needs to be in 8-4-4-4-12 format */
		p = *str + 23;
		memmove(p + 1, *str + 20, 12);
		*p = '-';
		p = *str + 18;
		memmove(p + 1, *str + 16, 4);
		*p = '-';
		p = *str + 13;
		memmove(p + 1, *str + 12, 4);
		*p = '-';
		p = *str + 8;
		memmove(p + 1, *str + 8, 4);
		*p = '-';
		p = *str + 36;
		*p = '\0';
	} else {
		/* This GUID does not appear to be valid */
		slapi_ch_free_string(str);
	}
}

/* For reasons not clear, the GUID returned in the tombstone DN is all 
 * messed up, like the guy in the movie 'the fly' after he want in the tranporter device */
static void
decrypt_guid(char *guid)
{
	static int decrypt_offsets[] = {6,7,4,5,2,3,0,1,10,11,8,9,14,15,12,13,16,17,18,19,
		20,21,22,23,24,25,26,27,28,29,30,31};

	char *p = guid;
	int i = 0;
	char *cpy = slapi_ch_strdup(guid);

	while (*p && i < (sizeof(decrypt_offsets)/sizeof(int)))
	{
		*p = cpy[decrypt_offsets[i]];
		p++;
		i++;
	}
	slapi_ch_free_string(&cpy);
}

static char*
extract_guid_from_tombstone_dn(const char *dn)
{
	char *guid = NULL;
	char *colon_offset = NULL;
	char *comma_offset = NULL;

	/* example DN of tombstone: 
		"CN=WDel Userdb1\\\nDEL:551706bc-ecf2-4b38-9284-9a8554171d69,CN=Deleted Objects,DC=magpie,DC=com" */

	/* First find the 'DEL:' */
	if ((colon_offset = strchr(dn,':'))) {
		/* Then scan forward to the next ',' */
		comma_offset = strchr(colon_offset,',');
	}

	/* The characters inbetween are the GUID, copy them
	 * to a new string and return to the caller */
	if (comma_offset && colon_offset && comma_offset > colon_offset) {
		guid = slapi_ch_malloc(comma_offset - colon_offset);
		strncpy(guid,colon_offset+1,(comma_offset-colon_offset)-1);
		guid[comma_offset-colon_offset-1] = '\0';
		/* Finally remove the dashes since we don't store them on our side */
		dedash_guid(guid);
		decrypt_guid(guid);
	}
	return guid;
}

static char *
convert_to_hex(Slapi_Value *val)
{
	int offset = 0;
	const struct berval *bvp = NULL;
	int length = 0;
	char *result = NULL;

	bvp = slapi_value_get_berval(val);
	if (bvp)
	{
		char *new_buffer = NULL;
		length = bvp->bv_len;

		for (offset = 0; offset < length; offset++) 
		{
			unsigned char byte = ((unsigned char*)(bvp->bv_val))[offset];
			new_buffer = PR_sprintf_append(new_buffer, "%02x", byte );
		}
		if (new_buffer)
		{
			result = new_buffer;
		}
	}
	return result;
}

/* Given a local entry, map it to it's AD tombstone DN. An AD
 * tombstone DN is formatted like:
 *
 *     cn=<cn>\0ADEL:<guid>,cn=Deleted Objects,<suffix>
 * 
 * This function will allocate a new Slapi_DN.  It is up to the
 * caller to free it when they are finished with it. */
static int 
map_windows_tombstone_dn(Slapi_Entry *e, Slapi_DN **dn, Private_Repl_Protocol *prp, int *exists)
{
	int rc = 0;
	char *cn = NULL;
	char *guid = NULL;
	const char *suffix = NULL;
	char *tombstone_dn = NULL;
	Slapi_Entry *tombstone = NULL;

	/* Initialize the output values */
	*dn = NULL;
	*exists = 0;

	cn = slapi_entry_attr_get_charptr(e,"cn");
	if (!cn) {
		cn = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
	}

	guid = slapi_entry_attr_get_charptr(e,"ntUniqueId");
	if (guid) {
		/* the GUID is in a different form in the tombstone DN, so
		 * we need to transform it from the way we store it. */
		decrypt_guid(guid);
		dash_guid(&guid);
	}

	/* The tombstone suffix discards any containers, so we need
	 * to trim the DN to only dc components. */
	if ((suffix = slapi_sdn_get_dn(windows_private_get_windows_subtree(prp->agmt)))) {
		/* If this isn't found, it is treated as an error below. */
		suffix = (const char *) PL_strcasestr(suffix,"dc=");
	}

	if (cn && guid && suffix) {
		tombstone_dn = PR_smprintf("cn=%s\\0ADEL:%s,cn=Deleted Objects,%s",
				cn, guid, suffix);

		/* Hand off the memory to the Slapi_DN */
		*dn = slapi_sdn_new_dn_passin(tombstone_dn);

		windows_get_remote_tombstone(prp, *dn, &tombstone);
		if (tombstone) {
			*exists = 1;
			slapi_entry_free(tombstone);
		}
	} else {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: map_windows_tombstone_dn: Failed to map dn=\"%s\" "
			"to windows tombstone dn.\n", agmt_get_long_name(prp->agmt),
			slapi_entry_get_dn(e));
		rc = 1;
	}

	slapi_ch_free_string(&cn);
	slapi_ch_free_string(&guid);
	return rc;
}

static int is_guid_dn(Slapi_DN *remote_dn)
{
	if ((remote_dn != NULL) && (strncasecmp("<GUID=",
			slapi_sdn_get_dn(remote_dn), 6) == 0)) {
		return 1;
	} else {
		return 0;
	}
}

static char*
extract_guid_from_entry(Slapi_Entry *e, int is_nt4)
{
	char *guid = NULL;
	Slapi_Value *val = NULL;
	Slapi_Attr *attr = NULL;
    
	slapi_entry_attr_find(e, "objectGUID", &attr);
	if (attr)
	{
		slapi_attr_first_value(attr, &val);
		if (val) {
			if (is_nt4)
			{
				guid = slapi_ch_strdup(slapi_value_get_string(val));
			} else
			{
				guid = convert_to_hex(val);
			}
		}
	}
	return guid;
}

#ifdef FOR_DEBUGGING
static void
extract_guid_from_entry_bv(Slapi_Entry *e, const struct berval **bv)
{
	Slapi_Value *val = NULL;
	Slapi_Attr *attr = NULL;
    
	slapi_entry_attr_find(e, "objectGUID", &attr);
	if (attr)
	{
		slapi_attr_first_value(attr, &val);
		if (val) {
			*bv = slapi_value_get_berval(val);
		}
	}
}
#endif

static char*
extract_username_from_entry(Slapi_Entry *e)
{
	char *uid = NULL;
	uid = slapi_entry_attr_get_charptr(e,"samAccountName");
	return uid;
}

static char*
extract_ntuserdomainid_from_entry(Slapi_Entry *e)
{
	char *uid = NULL;
	uid = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
	return uid;
}

static Slapi_DN *make_dn_from_guid(char *guid, int is_nt4, const char* suffix)
{
	Slapi_DN *new_dn = NULL;
	char *dn_string = NULL;
	if (guid)
	{
		if (is_nt4)
		{
			dn_string = PR_smprintf("GUID=%s,%s",guid,suffix);
		} else
		{
			dn_string = PR_smprintf("<GUID=%s>",guid);
		}
		new_dn = slapi_sdn_new_dn_byval(dn_string);
		PR_smprintf_free(dn_string);
	}
	/* dn string is now inside the Slapi_DN, and will be freed by its owner */
	return new_dn;
}

static char*
extract_container(const Slapi_DN *entry_dn, const Slapi_DN *suffix_dn)
{
	char *result = NULL;
	/* First do a scope test to make sure that we weren't passed bogus arguments */
	if (slapi_sdn_scope_test(entry_dn,suffix_dn,LDAP_SCOPE_SUBTREE))
	{
		Slapi_DN parent;
		slapi_sdn_init(&parent);

		/* Find the portion of the entry_dn between the RDN and the suffix */
		/* Start with the parent of the entry DN */
		slapi_sdn_get_parent(entry_dn, &parent);
		/* Iterate finding the parent again until we have the suffix */
		while (0 != slapi_sdn_compare(&parent,suffix_dn))
		{
			Slapi_DN child;
			Slapi_RDN *rdn = slapi_rdn_new();
			char *rdn_type = NULL;
			char *rdn_str = NULL;
			/* Append the current RDN to the new container string */
			slapi_sdn_get_rdn(&parent,rdn);
			slapi_rdn_get_first(rdn, &rdn_type, &rdn_str);
			if (rdn_str)
			{
				result = PR_sprintf_append(result, "%s=\"%s\",", rdn_type,rdn_str );	
			}
			/* Don't free this until _after_ we've used the rdn_str */
			slapi_rdn_free(&rdn);
			/* Move to the next successive parent */
			slapi_sdn_init(&child);
			slapi_sdn_copy(&parent,&child);
			slapi_sdn_done(&parent);
			slapi_sdn_get_parent(&child, &parent);
			slapi_sdn_done(&child);
		}
		slapi_sdn_done(&parent);
	} 
	/* Always return something */
	if (NULL == result)
	{
		result = slapi_ch_strdup("");
	}
	return result;
}

/* Given a non-tombstone entry, return the DN of its peer in AD (whether present or not) */
static int 
map_entry_dn_outbound(Slapi_Entry *e, Slapi_DN **dn, Private_Repl_Protocol *prp, int *missing_entry, int guid_form)
{
	int retval = 0;
	char *guid = NULL;
	Slapi_DN *new_dn = NULL;
	int is_nt4 = windows_private_get_isnt4(prp->agmt);
	const char *suffix = slapi_sdn_get_dn(windows_private_get_windows_subtree(prp->agmt));
	/* To find the DN of the peer entry we first look for an ntUniqueId attribute
	 * on the local entry. If that's present, we generate a GUID-form DN.
	 * If there's no GUID, then we look for an ntUserDomainId attribute
	 * on the entry. If that's present we attempt to search for an entry with
	 * that samaccountName attribute value in AD. If we don't find any matching
	 * entry we generate a new DN using the entry's cn. If later, we find that
	 * this entry already exists, we handle that problem at the time. We don't
	 * check here. Note: for NT4 we always use ntUserDomainId for the samaccountname rdn, never cn.
	 */
	
	*missing_entry = 0;

	guid = slapi_entry_attr_get_charptr(e,"ntUniqueId");
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: map_entry_dn_outbound: looking for AD entry for DS "
			"dn=\"%s\" guid=\"%s\"\n",
			agmt_get_long_name(prp->agmt),
			slapi_entry_get_dn_const(e),
			guid ? guid : "(null)");
	if (guid && guid_form) 
	{
		int rc = 0;
		Slapi_Entry *remote_entry = NULL;
		new_dn = make_dn_from_guid(guid, is_nt4, suffix);
		if (!new_dn) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: map_entry_dn_outbound: unable to make dn from guid %s.\n",
					agmt_get_long_name(prp->agmt), guid);
			retval = -1;
			goto done;
		}
		/* There are certain cases where we will have a GUID, but the entry does not exist in
		 * AD.  This happens when you delete an entry, then add it back elsewhere in the tree
		 * without removing the ntUniqueID attribute.  We should verify that the entry really
		 * exists in AD. */
		rc = windows_get_remote_entry(prp, new_dn, &remote_entry);
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"%s: map_entry_dn_outbound: return code %d from search "
				"for AD entry dn=\"%s\" or dn=\"%s\"\n",
				agmt_get_long_name(prp->agmt), rc,
				slapi_sdn_get_dn(new_dn),
				remote_entry ? slapi_entry_get_dn_const(remote_entry) : "(null)");
		if (0 == rc && remote_entry) {
			if (!is_subject_of_agreement_remote(remote_entry,prp->agmt)) {
				/* The remote entry is our of scope of the agreement.
				 * Thus, we don't map the entry_dn.
				 * This occurs when the remote entry is moved out. */
				slapi_sdn_free(&new_dn);
				retval = -1;
			}
			slapi_entry_free(remote_entry);
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: map_entry_dn_outbound: entry not found - rc %d\n",
					agmt_get_long_name(prp->agmt), rc);
			/* We need to re-write the DN to a non-GUID DN if we're syncing to a
			 * Windows 2000 Server since tombstone reanimation is not supported.
			 * If we're syncing with Windows 2003 Server, we'll just use the GUID
			 * to reanimate the tombstone when processing the add operation. */
			*missing_entry = 1;
			if (!windows_private_get_iswin2k3(prp->agmt)) {
				char *new_dn_string = NULL;
				char *cn_string = NULL;

				/* We can't use a GUID DN, so rewrite to the mapped DN. */
				cn_string = slapi_entry_attr_get_charptr(e,"cn");
				if (!cn_string) {
					cn_string = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
				}

				if (cn_string) {
					char *container_str = NULL;

					container_str = extract_container(slapi_entry_get_sdn_const(e),
						windows_private_get_directory_subtree(prp->agmt));
					new_dn_string = slapi_create_dn_string("cn=\"%s\",%s%s", cn_string, container_str, suffix);

					if (new_dn_string) {
						slapi_sdn_free(&new_dn);
						new_dn = slapi_sdn_new_dn_byval(new_dn_string);
						PR_smprintf_free(new_dn_string);
					}

					slapi_ch_free_string(&cn_string);
					slapi_ch_free_string(&container_str);
				}
			}
		}
	} else 
	{
		/* No GUID found, try ntUserDomainId */
		Slapi_Entry *remote_entry = NULL;
		char *username = slapi_entry_attr_get_charptr(e,"ntUserDomainId");
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
				"%s: map_entry_dn_outbound: looking for AD entry for DS "
				"dn=\"%s\" username=\"%s\"\n",
				agmt_get_long_name(prp->agmt),
				slapi_entry_get_dn_const(e),
				username ? username : "(null)");
		if (username) {
			retval = find_entry_by_attr_value_remote("samAccountName",username,&remote_entry,prp);
			if (0 == retval && remote_entry) 
			{
				/* Get the entry's DN */
				new_dn = slapi_sdn_new();
				slapi_sdn_copy(slapi_entry_get_sdn_const(remote_entry), new_dn);
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: map_entry_dn_outbound: found AD entry dn=\"%s\"\n",
						agmt_get_long_name(prp->agmt),
						slapi_sdn_get_dn(new_dn));
			} else {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"%s: map_entry_dn_outbound: entry not found - rc %d\n",
						agmt_get_long_name(prp->agmt), retval);
				if (0 == retval)
				{
					char *new_dn_string = NULL;
					char *cn_string = NULL;

					*missing_entry = 1;
					/* This means that we failed to find a peer entry */
					/* In that case we need to generate the DN that we want to use */
					/* Generated DN's take the form : 
						cn=<cn from local entry>, ... in the case that the local entry has a cn, OR
						cn=<ntuserdomainid attribute value>, ... in the case that the local entry doesn't have a CN
					 */
					if (is_nt4)
					{
						cn_string = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
					} else
					{
						cn_string = slapi_entry_attr_get_charptr(e,"cn");
						if (!cn_string) 
						{
							cn_string = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
						}
					}
					if (cn_string) 
					{
						char *rdnstr = NULL;
						char *container_str = NULL;
					
						container_str = extract_container(slapi_entry_get_sdn_const(e), windows_private_get_directory_subtree(prp->agmt));
						
						rdnstr = is_nt4 ? "samaccountname=\"%s\",%s%s" : "cn=\"%s\",%s%s";

						new_dn_string = slapi_create_dn_string(rdnstr,cn_string,container_str,suffix);
						if (new_dn_string)
						{
							new_dn = slapi_sdn_new_dn_byval(new_dn_string);
							PR_smprintf_free(new_dn_string);
						}
						slapi_ch_free_string(&cn_string);
						slapi_ch_free_string(&container_str);
					}
				} else 
				{
					/* This means that we failed to talk to the AD for some reason, the operation should be re-tried */
					slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: map_entry_dn_outbound: failed to fetch entry from AD: dn=\"%s\", err=%d\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), retval);

					retval = -1;
				}
			}
			slapi_ch_free_string(&username);
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
done:
	if (new_dn) 
	{
		*dn = new_dn;
	}
	slapi_ch_free_string(&guid);
	return retval;
}

/* Given a tombstone entry, return the DN of its peer in this server (if present) */
static int 
map_tombstone_dn_inbound(Slapi_Entry *e, Slapi_DN **dn, const Repl_Agmt *ra)
{
	int retval = 0;
	Slapi_DN *new_dn = NULL;
	char *guid = NULL;
	const char *dn_string = NULL;
	Slapi_Entry *matching_entry = NULL;

	/* To map a tombstone's DN we need to first extract the entry's objectGUID from the DN
	 * CN=vpdxtAD_07\
		DEL:d4ca4e16-e35b-400d-834a-f02db600f3fa,CN=Deleted Objects,DC=magpie,DC=com
	 */
	*dn = NULL;

	dn_string = slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)); /* This is a pointer from inside the sdn, no need to free */
	guid = extract_guid_from_tombstone_dn(dn_string);

	if (guid) 
	{
		retval = find_entry_by_guid(guid,&matching_entry,ra); 
		if (retval) 
		{
			if (ENTRY_NOTFOUND == retval) 
			{
			} else 
			{
				if (ENTRY_NOT_UNIQUE == retval) 
				{
				} else
				{
					/* A real error */
				}
			}
		} else 
		{
			/* We found the matching entry : get its DN */
			new_dn = slapi_sdn_dup(slapi_entry_get_sdn_const(matching_entry));
		}
	}

	if (new_dn) 
	{
		*dn = new_dn;
	}

	if (guid) 
	{
		slapi_ch_free_string(&guid);
	}
	if (matching_entry)
	{
		slapi_entry_free(matching_entry);
	}
	return retval;
}

/* Given a non-tombstone entry, return the DN of its peer in this server (whether present or not) */
static int 
map_entry_dn_inbound(Slapi_Entry *e, Slapi_DN **dn, const Repl_Agmt *ra)
{
	return map_entry_dn_inbound_ext(e, dn, ra, 1 /* use_guid */, 1 /* use_username */);
}

/* Given a non-tombstone entry, return the DN of its peer in this server (whether present or not).
 * If use_guid is 1, the guid or samaccountname values will be used to find the matching local
 * entry.  If use_guid is 0, a new DN will be generated as if we are adding a brand new entry. */
static int
map_entry_dn_inbound_ext(Slapi_Entry *e, Slapi_DN **dn, const Repl_Agmt *ra, int use_guid, int use_username)
{
	int retval = 0;
	Slapi_DN *new_dn = NULL;
	char *guid = NULL;
	char *username = NULL;
	Slapi_Entry *matching_entry = NULL;
	int is_user = 0;
	int is_group = 0;
	int is_nt4 = windows_private_get_isnt4(ra);

	/* To map a non-tombstone's DN we need to first try to look it up by GUID.
	 * If we do not find it, then we need to generate the DN that it would have if added as a new entry.
	 */
	*dn = NULL;

	windows_is_remote_entry_user_or_group(e,&is_user,&is_group);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: map_entry_dn_inbound: looking for local entry "
					"matching AD entry [%s]\n",
					agmt_get_long_name(ra),
					slapi_entry_get_dn_const(e));
	if (use_guid) {
		guid = extract_guid_from_entry(e, is_nt4);
		if (guid) 
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: map_entry_dn_inbound: looking for local entry "
							"by guid [%s]\n",
							agmt_get_long_name(ra),
							guid);
			retval = find_entry_by_guid(guid,&matching_entry,ra);
			if (retval) 
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: map_entry_dn_inbound: problem looking for guid: %d\n",
								agmt_get_long_name(ra), retval);
				if (ENTRY_NOTFOUND == retval) 
				{
				} else 
				{
					if (ENTRY_NOT_UNIQUE == retval) 
					{
					} else
					{
						/* A real error */
						goto error;
					}
				}
			} else 
			{
				/* We found the matching entry : get its DN */
				new_dn = slapi_sdn_dup(slapi_entry_get_sdn_const(matching_entry));
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: map_entry_dn_inbound: found local entry [%s]\n",
								agmt_get_long_name(ra),
								slapi_sdn_get_dn(new_dn));
			}
		}
		else
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
							"%s: map_entry_dn_inbound: AD entry has no guid!\n",
							agmt_get_long_name(ra));
		}
	}

	/* If we failed to lookup by guid, try samaccountname */
	if (NULL == new_dn) 
	{
		username = extract_username_from_entry(e);
		if (use_username) {
			if (username) {
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: map_entry_dn_inbound: looking for local entry "
								"by uid [%s]\n",
								agmt_get_long_name(ra),
								username);
				retval = find_entry_by_username(username,&matching_entry,ra);
				if (retval) 
				{
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: map_entry_dn_inbound: problem looking for username: %d\n",
									agmt_get_long_name(ra), retval);
					if (ENTRY_NOTFOUND == retval) 
					{
					} else 
					{
						if (ENTRY_NOT_UNIQUE == retval) 
						{
						} else
						{
							/* A real error */
							goto error;
						}
					}
				} else 
				{
					/* We found the matching entry : get its DN */
					new_dn = slapi_sdn_dup(slapi_entry_get_sdn_const(matching_entry));
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: map_entry_dn_inbound: found local entry by name [%s]\n",
									agmt_get_long_name(ra),
									slapi_sdn_get_dn(new_dn));
				}
			}
			else
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"%s: map_entry_dn_inbound: AD entry has no username!\n",
								agmt_get_long_name(ra));
			}
		}
	}
	/* If we couldn't find a matching entry by either method, then we need to invent a new DN */
	if (NULL == new_dn) 
	{
		/* The new DN has the form: uid=<samaccountname>,<sync'ed subtree> */
		/* If an entry with this DN already exists, we fail and return no DN 
		 * this is because we don't want to second-guess what the admin wants here:
		 * they may want to associate this existing entry with the peer AD entry, 
		 * but if they intend that we say they must add the ntDomainUserId attribute to
		 * that entry.
		 */
		char *new_dn_string = NULL;
		if (username) 
		{
			const char *suffix = slapi_sdn_get_dn(windows_private_get_directory_subtree(ra));
			char *container_str = NULL;

			container_str = extract_container(slapi_entry_get_sdn_const(e), windows_private_get_windows_subtree(ra));
			/* Local DNs for users and groups are different */
			if (is_user)
			{
				new_dn_string = slapi_create_dn_string("uid=\"%s\",%s%s",username,container_str,suffix);
				winsync_plugin_call_get_new_ds_user_dn_cb(ra,
														  windows_private_get_raw_entry(ra),
														  e,
														  &new_dn_string,
														  windows_private_get_directory_subtree(ra),
														  windows_private_get_windows_subtree(ra));
			} else
			{
				new_dn_string = slapi_create_dn_string("cn=\"%s\",%s%s",username,container_str,suffix);
				if (is_group) {
					winsync_plugin_call_get_new_ds_group_dn_cb(ra,
															   windows_private_get_raw_entry(ra),
															   e,
															   &new_dn_string,
															   windows_private_get_directory_subtree(ra),
															   windows_private_get_windows_subtree(ra));
				}
			}
			/* 
			 * new_dn_string is created by slapi_create_dn_string,
			 * which is normalized. Thus, we can use _normdn_.
			 */
			new_dn = slapi_sdn_new_normdn_passin(new_dn_string);
			slapi_ch_free_string(&container_str);
			/* Clear any earlier error */
			retval = 0;
		} else 
		{
			/* Error, no username */
		}
	}
	if (new_dn) 
	{
		*dn = new_dn;
	}
error: 
	if (guid) 
	{
		PR_smprintf_free(guid);
	}
	if (matching_entry)
	{
		slapi_entry_free(matching_entry);
	}
	if (username)
	{
		slapi_ch_free_string(&username);
	}
	return retval;
}

/* Tests if the entry is subject to our agreement (i.e. is it in the sync'ed subtree in this server, and is it the right objectclass
 * and does it have the right attribute values for sync ?) 
 */
static int 
is_subject_of_agreement_local(const Slapi_Entry *local_entry, const Repl_Agmt *ra)
{
	int retval = 0;
	int is_in_subtree = 0;
	const Slapi_DN *agreement_subtree = NULL;
	
	/* First test for the sync'ed subtree */
	agreement_subtree = windows_private_get_directory_subtree(ra);
	if (NULL == agreement_subtree)
	{
		goto error;
	}
	is_in_subtree = slapi_sdn_scope_test(slapi_entry_get_sdn_const(local_entry), agreement_subtree, LDAP_SCOPE_SUBTREE);
	if (is_in_subtree) 
	{
		/* Next test for the correct kind of entry */
		if (local_entry) {
			if (slapi_filter_test_simple( (Slapi_Entry*)local_entry,
					(Slapi_Filter*)windows_private_get_directory_filter(ra)) == 0)
			{
				retval = 1;
			}
		} else 
		{
			/* Error: couldn't find the entry */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"failed to find entry in is_subject_of_agreement_local: %d\n", retval);
			retval = 0;
		}
	}
error:
	return retval;
}

/* Tests if a DN is within the scope of our agreement */
static int
is_dn_subject_of_agreement_local(const Slapi_DN *sdn, const Repl_Agmt *ra)
{
	int retval = 0;
	const Slapi_DN *agreement_subtree = NULL;

	/* Get the subtree from the agreement */
	agreement_subtree = windows_private_get_directory_subtree(ra);
	if (NULL == agreement_subtree)
	{
		goto error;
	}

	/* Check if the DN is within the subtree */
	retval = slapi_sdn_scope_test(sdn, agreement_subtree, LDAP_SCOPE_SUBTREE);

error:
	return retval;
}

/* Tests if the entry is subject to our agreement (i.e. is it in the sync'ed subtree in AD and either a user or a group ?) */
static int 
is_subject_of_agreement_remote(Slapi_Entry *e, const Repl_Agmt *ra)
{
	int retval = 0;
	int is_in_subtree = 0;
	const Slapi_DN *agreement_subtree = NULL;
	const Slapi_DN *sdn;
	
	/* First test for the sync'ed subtree */
	agreement_subtree = windows_private_get_windows_subtree(ra);
	if (NULL == agreement_subtree) 
	{
		goto error;
	}
	sdn = slapi_entry_get_sdn_const(e);
	is_in_subtree = slapi_sdn_scope_test(sdn, agreement_subtree, LDAP_SCOPE_SUBTREE);
	if (is_in_subtree) 
	{
		Slapi_DN psdn = {0};
		Slapi_Entry *pentry = NULL;
		/*
		 * Check whether the parent of the entry exists or not.
		 * If it does not, treat the entry e is out of scope.
		 * For instance, agreement_subtree is cn=USER,<SUFFIX>
		 * cn=container,cn=USER,<SUFFIX> is not synchronized.
		 * If 'e' is uid=test,cn=container,cn=USER,<SUFFIX>,
		 * the entry is not synchronized, either.  We treat
		 * 'e' as out of scope.
		 */
		slapi_sdn_get_parent(sdn, &psdn);
		if (0 == slapi_sdn_compare(&psdn, agreement_subtree)) {
			retval = 1;
		} else {
			/* If parent entry is not local, the entry is out of scope */
			int rc = windows_get_local_entry(&psdn, &pentry);
			if ((0 == rc) && pentry) {
				retval = 1;
				slapi_entry_free(pentry);
			}
		}
		slapi_sdn_done(&psdn);
	}
error:
	return retval;
}

static int
windows_create_local_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,const Slapi_DN* local_sdn)
{
	int retval = 0;
	char *entry_string = NULL;
	Slapi_Entry *local_entry = NULL;
	Slapi_PBlock* pb = NULL;
	int is_user = 0; 
	int is_group = 0;
	char *local_entry_template = NULL;
	char *user_entry_template = NULL;
	char *username = extract_username_from_entry(remote_entry);
	Slapi_Attr *attr = NULL;
	int rc = 0;
	char *guid_str = NULL;
	int is_nt4 = windows_private_get_isnt4(prp->agmt);
	Slapi_Entry *post_entry = NULL;

	char *local_user_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:person\n"
   		"objectclass:organizationalperson\n"
   		"objectclass:inetOrgPerson\n"
   		"objectclass:ntUser\n"
		"ntUserDeleteAccount:true\n"
   		"uid:%s\n";

	char *local_nt4_user_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:person\n"
   		"objectclass:organizationalperson\n"
   		"objectclass:inetOrgPerson\n"
   		"objectclass:ntUser\n"
		"ntUserDeleteAccount:true\n"
   		"uid:%s\n";

	char *local_group_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:groupofuniquenames\n"
   		"objectclass:ntGroup\n"
		"ntGroupDeleteGroup:true\n"
   		"cn:%s\n";

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_create_local_entry\n", 0, 0, 0 );

	windows_is_remote_entry_user_or_group(remote_entry,&is_user,&is_group);
	user_entry_template = is_nt4 ? local_nt4_user_entry_template : local_user_entry_template;
	local_entry_template = is_user ? user_entry_template : local_group_entry_template;
	/* Create a new entry */
	/* Give it its DN and username */
	entry_string = slapi_ch_smprintf(local_entry_template,slapi_sdn_get_dn(local_sdn),username, username);
	if (NULL == entry_string) 
	{
		goto error;
	}
	local_entry = slapi_str2entry(entry_string, 0);
	slapi_ch_free_string(&entry_string);
	if (NULL == local_entry) 
	{
		goto error;
	}
	/* Map the appropriate attributes sourced from the remote entry */
    for (rc = slapi_entry_first_attr(remote_entry, &attr); rc == 0;
			rc = slapi_entry_next_attr(remote_entry, attr, &attr)) 
	{
		char *type = NULL;
		Slapi_ValueSet *vs = NULL;
		int mapdn = 0;

		slapi_attr_get_type( attr, &type );
		slapi_attr_get_valueset(attr,&vs);

		if ( is_straight_mapped_attr(type,is_user,is_nt4) )
		{
			/* copy over the attr values */
			slapi_entry_add_valueset(local_entry,type,vs);
		} else 
		{
			char *new_type = NULL;

			windows_map_attr_name(type , 0 /* from windows */, is_user, 1 /* create */, &new_type, &mapdn);
			if (new_type)
			{
				if (mapdn)
				{
					Slapi_ValueSet *mapped_values = NULL;
					map_dn_values(prp,vs,&mapped_values, 0 /* from windows */,0);
					if (mapped_values) 
					{
						slapi_entry_add_valueset(local_entry,new_type,mapped_values);
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					}
				} else
				{
					slapi_entry_add_valueset(local_entry,new_type,vs);
				}
				slapi_ch_free_string(&new_type);
			}
		}
		if (vs) 
		{
			slapi_valueset_free(vs);
			vs = NULL;
		}
	}
	/* Copy over the GUID */
	guid_str = extract_guid_from_entry(remote_entry, is_nt4);
	if (guid_str) 
	{
		slapi_entry_add_string(local_entry,"ntUniqueId",guid_str);
	} else 
	{
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"extract_guid_from_entry entry %s failed to extract the guid\n", slapi_sdn_get_dn(local_sdn));
		/* Fatal error : need the guid */
		goto error;
	}
	/* Hack for NT4, which has no surname */
	if (is_nt4 && is_user)
	{
		slapi_entry_add_string(local_entry,"sn",username);
	}

	if (is_user) {
	    winsync_plugin_call_pre_ds_add_user_cb(prp->agmt,
						   windows_private_get_raw_entry(prp->agmt),
						   remote_entry,
						   local_entry);
	} else if (is_group) {
	    winsync_plugin_call_pre_ds_add_group_cb(prp->agmt,
							windows_private_get_raw_entry(prp->agmt),
						    remote_entry,
						    local_entry);
	}
	/* Store it */
	windows_dump_entry("Adding new local entry",local_entry);
	pb = slapi_pblock_new();
	slapi_add_entry_internal_set_pb(pb, local_entry, NULL,repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),0);
	post_entry = slapi_entry_dup(local_entry);
	slapi_add_internal_pb(pb);
	local_entry = NULL; /* consumed by add */
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &retval);

	if (is_user) {
	    winsync_plugin_call_post_ds_add_user_cb(prp->agmt,
						   windows_private_get_raw_entry(prp->agmt),
						   remote_entry, post_entry, &retval);
	} else if (is_group) {
	    winsync_plugin_call_post_ds_add_group_cb(prp->agmt,
							windows_private_get_raw_entry(prp->agmt),
						    remote_entry, post_entry, &retval);
	}
	slapi_entry_free(post_entry);

	if (retval) {
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"add operation of entry %s returned: %d\n", slapi_sdn_get_dn(local_sdn), retval);
	}
error:
	slapi_entry_free(local_entry);
	slapi_ch_free_string(&guid_str);
	if (pb)
	{
		slapi_pblock_destroy(pb);
	}
	if (username) 
	{
		slapi_ch_free_string(&username);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_create_local_entry\n", 0, 0, 0 );
	return retval;
}

/* Function to generate the correct mods to bring 'local' attribute values into consistency with given 'remote' values */
/* 'local' and 'remote' in quotes because we actually use this function in both directions */
/* This function expects the value sets to have been already pruned to exclude values that are not
 * subject to the agreement and present in the peer. */
static int
windows_generate_dn_value_mods(char *local_type, const Slapi_Attr *attr, Slapi_Mods *smods, Slapi_ValueSet *remote_values, Slapi_ValueSet *local_values, int *do_modify)
{
	int ret = 0;
	int i = 0;
	Slapi_Value *rv = NULL;
	Slapi_Value *lv = NULL;
	/* We need to generate an ADD mod for each entry that is in the remote values but not in the local values */
	/* Iterate over the remote values */
	i = slapi_valueset_first_value(remote_values,&rv);
	while (NULL != rv)
	{
		const char *remote_dn = slapi_value_get_string(rv);
		int value_present_in_local_values = (NULL != slapi_valueset_find(attr, local_values, rv));
		if (!value_present_in_local_values)
		{
			slapi_mods_add_string(smods,LDAP_MOD_ADD,local_type,remote_dn);
			*do_modify = 1;
		}
		i = slapi_valueset_next_value(remote_values,i,&rv);
	}
	/* We need to generate a DEL mod for each entry that is in the local values but not in the remote values */
	/* Iterate over the local values */
	i = slapi_valueset_first_value(local_values,&lv);
	while (NULL != lv)
	{
		const char *local_dn = slapi_value_get_string(lv);
		int value_present_in_remote_values = (NULL != slapi_valueset_find(attr, remote_values, lv));
		if (!value_present_in_remote_values)
		{
			slapi_mods_add_string(smods,LDAP_MOD_DELETE,local_type,local_dn);
			*do_modify = 1;
		}
		i = slapi_valueset_next_value(local_values,i,&lv);
	}
	return ret;
}

/* Generate the mods for an update in either direction.  Be careful... the "remote" entry is the DS entry in the to_windows case, but the AD entry in the other case. */
static int
windows_generate_update_mods(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry, int to_windows, Slapi_Mods *smods, int *do_modify)
{
	int retval = 0;
	Slapi_Attr *attr = NULL;
	Slapi_Attr *del_attr = NULL;
	int is_user = 0;
	int is_group = 0;
	int rc = 0;
	int is_nt4 = windows_private_get_isnt4(prp->agmt);
	/* Iterate over the attributes on the remote entry, updating the local entry where appropriate */
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_generate_update_mods\n", 0, 0, 0 );

	*do_modify = 0;

        if (!remote_entry || !local_entry) {
            slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
                            "%s: windows_generate_update_mods: remote_entry is [%s] local_entry is [%s] "
                            "cannot generate update mods\n", agmt_get_long_name(prp->agmt),
                            remote_entry ? slapi_entry_get_dn_const(remote_entry) : "NULL",
                            local_entry ? slapi_entry_get_dn_const(local_entry) : "NULL");
            goto bail;
        }

	if (to_windows)
	{
		windows_is_local_entry_user_or_group(remote_entry,&is_user,&is_group);
	} else
	{
		windows_is_remote_entry_user_or_group(remote_entry,&is_user,&is_group);
	}

    for (rc = slapi_entry_first_attr(remote_entry, &attr); rc == 0;
             rc = slapi_entry_next_attr(remote_entry, attr, &attr)) 
	{
		int is_present_local = 0;
		char *type = NULL;
		Slapi_ValueSet *vs = NULL;
		char *local_type = NULL;
		Slapi_Attr *local_attr = NULL;
		int is_guid = 0;
		int mapdn = 0;

		slapi_attr_get_type( attr, &type );
		slapi_attr_get_valueset(attr,&vs);

		/* First determine what we will do with this attr */
		/* If it's a GUID, we need to take special action */
		if (0 == slapi_attr_type_cmp(type,"objectGuid",SLAPI_TYPE_CMP_SUBTYPE) && !to_windows) 
		{
			is_guid = 1;
			local_type = slapi_ch_strdup("ntUniqueId");
		} else 
		{
			if ( is_straight_mapped_attr(type,is_user,is_nt4) ) {
				local_type = slapi_ch_strdup(type);
			} else {
				windows_map_attr_name(type , to_windows, is_user, 0 /* not create */, &local_type, &mapdn);
			}
			is_guid = 0;
		}
		if (NULL == local_type)
		{
			/* Means we do not map this attribute */
			if (vs) 
			{
				slapi_valueset_free(vs);
				vs = NULL;
			}
			continue;
		}

		if (to_windows && (0 == slapi_attr_type_cmp(local_type, "streetAddress", SLAPI_TYPE_CMP_SUBTYPE))) {
			slapi_entry_attr_find(local_entry,FAKE_STREET_ATTR_NAME,&local_attr);
		} else {
			slapi_entry_attr_find(local_entry,local_type,&local_attr);
		}

		is_present_local = (NULL == local_attr) ? 0 : 1;
		/* Is the attribute present on the local entry ? */
		if (is_present_local && !is_guid)
		{
			if (!mapdn)
			{
				int values_equal = 0;
				/* We only have to deal with processing the cn here for
				 * operations coming from AD since the mapping for the
				 * to_windows case has the create only flag set.  We
				 * just need to check if the value from the AD entry
				 * is already present in the DS entry. */
				if (!to_windows && (0 == slapi_attr_type_cmp(type, "name", SLAPI_TYPE_CMP_SUBTYPE))) {
					values_equal = attr_compare_present(attr, local_attr, 0);
				/* AD has a length contraint on the initials attribute (in addition
				 * to defining it as single-valued), so treat is as a special case. */
				} else if (0 == slapi_attr_type_cmp(type, "initials", SLAPI_TYPE_CMP_SUBTYPE)) {
					values_equal = attr_compare_present(attr, local_attr, AD_INITIALS_LENGTH);
				/* If this is a single valued type in AD, then we just check if the value
				 * in AD is present in our entry in DS.  In this case, attr is from the AD
				 * entry, and local_attr is from the DS entry. */
				} else if (!to_windows && is_single_valued_attr(type)) {
					values_equal = attr_compare_present(attr, local_attr, 0);
				/* If this is a single valued type in AD, then we just check if the AD
				 * entry already contains any value that is present in the DS entry.  In
				 * this case, attr is from the DS entry, and local_attr is from the AD entry. */
				} else if (to_windows && is_single_valued_attr(type)) {
					values_equal = attr_compare_present(local_attr, attr, 0);
				} else {
					/* Compare the entire attribute values */
					values_equal = attr_compare_equal(attr, local_attr, 0);
				}

				/* If it is then we need to replace the local values with the remote values if they are different */
				if (!values_equal)
				{
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"windows_generate_update_mods: %s, %s : values are different\n",
					slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)), local_type);

					if (to_windows && ((0 == slapi_attr_type_cmp(local_type, "streetAddress", SLAPI_TYPE_CMP_SUBTYPE)) ||
						(0 == slapi_attr_type_cmp(local_type, "telephoneNumber", SLAPI_TYPE_CMP_SUBTYPE)) ||
						(0 == slapi_attr_type_cmp(local_type, "physicalDeliveryOfficeName", SLAPI_TYPE_CMP_SUBTYPE)))) {
						/* These attributes are single-valued in AD, so make
						 * sure we don't try to send more than one value. */
						if (slapi_valueset_count(vs) > 1) {
							int i = 0;
							Slapi_Value *value = NULL;
							Slapi_Value *new_value = NULL;

							i = slapi_valueset_first_value(vs,&value);
							if (i >= 0) {
								/* Dup the first value, trash the valueset, then copy
								 * in the dup'd value. */
								new_value = slapi_value_dup(value);
								slapi_valueset_done(vs);
								/* The below hands off the memory to the valueset */
								slapi_valueset_add_value_ext(vs, new_value, SLAPI_VALUE_FLAG_PASSIN);
							}
						}
					} else if ((0 == slapi_attr_type_cmp(local_type, "initials",
						SLAPI_TYPE_CMP_SUBTYPE) && to_windows)) {
						/* initials is constratined to a max length of
						 * 6 characters in AD, so trim the value if
						 * needed before sending. */
						int i = 0;
						const char *initials_value = NULL;
						Slapi_Value *value = NULL;

						i = slapi_valueset_first_value(vs,&value);
						while (i >= 0) {
							initials_value = slapi_value_get_string(value);

							/* If > AD_INITIALS_LENGTH, trim the value */
							if (strlen(initials_value) > AD_INITIALS_LENGTH) {
								char *new_initials = PL_strndup(initials_value, AD_INITIALS_LENGTH);
								/* the below hands off memory */
								slapi_value_set_string_passin(value, new_initials);
								slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
									"%s: windows_generate_update_mods: "
									"Trimming initials attribute to %d characters.\n",
									agmt_get_long_name(prp->agmt), AD_INITIALS_LENGTH);
							}

							i = slapi_valueset_next_value(vs, i, &value);
						}
					}

					slapi_mods_add_mod_values(smods,LDAP_MOD_REPLACE,
						local_type,valueset_get_valuearray(vs));
					*do_modify = 1;
				} else
				{
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"windows_generate_update_mods: %s, %s : values are equal\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)), local_type);
				}
			} else {
				/* A dn-valued attribute : need to take special steps */
				Slapi_ValueSet *mapped_remote_values = NULL;
				/* First map all the DNs to that they're in a consistent domain */
				map_dn_values(prp,vs,&mapped_remote_values, to_windows,0);
				if (mapped_remote_values) 
				{
					Slapi_ValueSet *local_values = NULL;
					Slapi_ValueSet *restricted_local_values = NULL;
					/* Now do a compare on the values, generating mods to bring them into consistency (if any) */
					/* We ignore any DNs that are outside the scope of the agreement (on both sides) */
					slapi_attr_get_valueset(local_attr,&local_values);
					if (local_values) 
					{
						map_dn_values(prp,local_values,&restricted_local_values,!to_windows,1);
						if (restricted_local_values)
						{
							windows_generate_dn_value_mods(local_type,local_attr,smods,mapped_remote_values,restricted_local_values,do_modify);
							slapi_valueset_free(restricted_local_values);
							restricted_local_values = NULL;
						}
						else
						{
							slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
											"windows_generate_update_mods: no restricted local values found for "
											"local attribute %s in local entry %s for remote attribute "
											"%s in remote entry %s\n",
											local_type,
											slapi_entry_get_dn(local_entry),
											type ? type : "NULL",
											slapi_entry_get_dn(remote_entry));
						}
						slapi_valueset_free(local_values);
						local_values = NULL;
					} else {
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
										"windows_generate_update_mods: no local values found for "
										"local attribute %s in local entry %s for remote attribute "
										"%s in remote entry %s\n",
										local_type,
										slapi_entry_get_dn(local_entry),
										type ? type : "NULL",
										slapi_entry_get_dn(remote_entry));
					}
					slapi_valueset_free(mapped_remote_values);
					mapped_remote_values = NULL;
				} else {
					slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
									"windows_generate_update_mods: could not map the values in "
									"local attribute %s in local entry %s for remote attribute "
									"%s in remote entry %s\n",
									local_type,
									slapi_entry_get_dn(local_entry),
									type ? type : "NULL",
									slapi_entry_get_dn(remote_entry));
				}
			}
		} else
		{
			if (!is_present_local)
			{
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"windows_generate_update_mods: %s, %s : values not present on peer entry\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)), local_type);
				/* If it is currently absent, then we add the value from the remote entry */
				if (is_guid)
				{
					/* Translate the guid value */
					char *guid = extract_guid_from_entry(remote_entry, is_nt4);
					if (guid)
					{
						slapi_mods_add_string(smods,LDAP_MOD_ADD,local_type,guid);
						slapi_ch_free_string(&guid);
					}
				} else
				{
					/* Handle DN valued attributes here */
					if (mapdn)
					{
						Slapi_ValueSet *mapped_values = NULL;
						map_dn_values(prp,vs,&mapped_values, to_windows,0);
						if (mapped_values) 
						{
							slapi_mods_add_mod_values(smods,LDAP_MOD_ADD,local_type,valueset_get_valuearray(mapped_values));
							slapi_valueset_free(mapped_values);
							mapped_values = NULL;
						}
					} else
					{
					/* If this attribute is single-valued in AD,
					 * we only want to send the first value. */
						if (to_windows && is_single_valued_attr(local_type)) {
							if (slapi_valueset_count(vs) > 1) {
								int i = 0;
								Slapi_Value *value = NULL;
								Slapi_Value *new_value = NULL;

								i = slapi_valueset_first_value(vs,&value);
								if (i >= 0) {
									/* Dup the first value, trash the valueset, then copy
									 * in the dup'd value. */
									new_value = slapi_value_dup(value);
									slapi_valueset_done(vs);
									/* The below hands off the memory to the valueset */
									slapi_valueset_add_value_ext(vs, new_value, SLAPI_VALUE_FLAG_PASSIN);
								}
							}
						}

						if (to_windows && (0 == slapi_attr_type_cmp(local_type, "initials",
							SLAPI_TYPE_CMP_SUBTYPE))) {
							/* initials is constratined to a max length of
							 * 6 characters in AD, so trim the value if
							 * needed before sending. */
							int i = 0;
							const char *initials_value = NULL;
							Slapi_Value *value = NULL;

							i = slapi_valueset_first_value(vs,&value);
							while (i >= 0) {
								initials_value = slapi_value_get_string(value);

								/* If > AD_INITIALS_LENGTH, trim the value */
								if (strlen(initials_value) > AD_INITIALS_LENGTH) {
									char *new_initials = PL_strndup(initials_value, AD_INITIALS_LENGTH);
									/* the below hands off memory */
									slapi_value_set_string_passin(value, new_initials);
									slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
										"%s: windows_generate_update_mods: "
										"Trimming initials attribute to %d characters.\n",
                                                                        agmt_get_long_name(prp->agmt), AD_INITIALS_LENGTH);
								}

								i = slapi_valueset_next_value(vs, i, &value);
							}
						}
						slapi_mods_add_mod_values(smods,LDAP_MOD_ADD,local_type,valueset_get_valuearray(vs));
					}
				}

				/* Only set the do_modify flag if smods is not empty */
				if (slapi_mods_get_num_mods(smods) > 0) {
					*do_modify = 1;
				}
			}
		}

		if (vs) 
		{
			slapi_valueset_free(vs);
			vs = NULL;
		}

		slapi_ch_free_string(&local_type);
	}

        /* Check if any attributes were deleted from the remote entry */
        entry_first_deleted_attribute(remote_entry, &del_attr);
        while (del_attr != NULL) {
                Slapi_Attr *local_attr = NULL;
                char *type = NULL;
                char *local_type = NULL;
                int mapdn = 0;

                /* Map remote type to local type */
		slapi_attr_get_type(del_attr, &type);
                if ( is_straight_mapped_attr(type,is_user,is_nt4) ) {
                        local_type = slapi_ch_strdup(type);
                } else {
                        windows_map_attr_name(type , to_windows, is_user, 0 /* not create */, &local_type, &mapdn);
                }

                /* Check if this attr exists in the local entry */
                if (local_type) {
                        slapi_entry_attr_find(local_entry, local_type, &local_attr);
                        if (local_attr) {
                                slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
                                        "windows_generate_update_mods: deleting %s attribute from local entry\n", local_type);
                                /* Delete this attr from the local entry */
                                slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, local_type, NULL);
				*do_modify = 1;
                        }
                }

                entry_next_deleted_attribute(remote_entry, &del_attr);
		slapi_ch_free_string(&local_type);
        }

	if (to_windows) {
	    if (is_user) {
		winsync_plugin_call_pre_ad_mod_user_cb(prp->agmt,
						       windows_private_get_raw_entry(prp->agmt),
						       local_entry, /* the cooked ad entry */
						       remote_entry, /* the ds entry */
						       smods,
						       do_modify);
	    } else if (is_group) {
		winsync_plugin_call_pre_ad_mod_group_cb(prp->agmt,
							windows_private_get_raw_entry(prp->agmt),
							local_entry, /* the cooked ad entry */
							remote_entry, /* the ds entry */
							smods,
							do_modify);
	    }
	} else {
	    if (is_user) {
		winsync_plugin_call_pre_ds_mod_user_cb(prp->agmt,
						       windows_private_get_raw_entry(prp->agmt),
						       remote_entry, /* the cooked ad entry */
						       local_entry, /* the ds entry */
						       smods,
						       do_modify);
	    } else if (is_group) {
		winsync_plugin_call_pre_ds_mod_group_cb(prp->agmt,
							windows_private_get_raw_entry(prp->agmt),
							remote_entry, /* the cooked ad entry */
							local_entry, /* the ds entry */
							smods,
							do_modify);
	    }
	}

	if (slapi_is_loglevel_set(SLAPI_LOG_REPL) && *do_modify)
	{
		slapi_mods_dump(smods,"windows sync");
	}
bail:
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_generate_update_mods: %d\n", retval, 0, 0 );
	return retval;
}

static int
windows_update_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry, int is_user)
{
    Slapi_Mods smods = {0};
	int retval = 0;
	int do_modify = 0;
	int ldap_op = 0;
	int ldap_result_code = 0;

    slapi_mods_init (&smods, 0);
	retval = windows_generate_update_mods(prp,local_entry,remote_entry,1,&smods,&do_modify);
	/* Now perform the modify if we need to */
	if (0 == retval && do_modify)
	{
		const char *dn = slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry));
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"windows_update_remote_entry: modifying entry %s\n", dn);

		retval = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)),slapi_mods_get_ldapmods_byref(&smods), NULL,NULL);

		windows_conn_get_error(prp->conn, &ldap_op, &ldap_result_code);
		if ((retval != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
			/* op failed but no ldap error code ??? */
			ldap_result_code = LDAP_OPERATIONS_ERROR;
		}
		if (is_user) {
			winsync_plugin_call_post_ad_mod_user_cb(prp->agmt,
						       windows_private_get_raw_entry(prp->agmt),
						       remote_entry, /* the cooked ad entry */
						       local_entry, /* the ds entry */
						       &smods, &ldap_result_code);
		} else {
			winsync_plugin_call_post_ad_mod_group_cb(prp->agmt,
							windows_private_get_raw_entry(prp->agmt),
							remote_entry, /* the cooked ad entry */
							local_entry, /* the ds entry */
							&smods, &ldap_result_code);
		}
		/* see if plugin reset success/error condition */
		if ((retval != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
			retval = CONN_OPERATION_SUCCESS;
			windows_conn_set_error(prp->conn, ldap_result_code);
		} else if ((retval == CONN_OPERATION_SUCCESS) && ldap_result_code) {
			retval = CONN_OPERATION_FAILED;
			windows_conn_set_error(prp->conn, ldap_result_code);
		}
	} else
	{
		const char *dn = slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry));
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"no mods generated for remote entry: %s\n", dn);
	}

	slapi_mods_done(&smods);
	return retval;
}

static int
windows_update_local_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry)
{
    Slapi_Mods smods = {0};
	int retval = 0;
	Slapi_PBlock *pb = NULL;
	int do_modify = 0;
	char *newsuperior = NULL;
	Slapi_DN newsuperior_sdn;
	const char *newrdn = NULL;
	int is_user = 0, is_group = 0;
	const char *newdn = NULL;
	Slapi_DN *mapped_sdn = NULL;
	Slapi_RDN rdn = {0};
	Slapi_Entry *orig_local_entry = NULL;

	windows_is_local_entry_user_or_group(local_entry, &is_user, &is_group);

	/* Get the mapped DN.  We don't want to locate the existing entry by
	 * guid or username.  We want to get the mapped DN just as we would 
	 * if we were creating a new entry. */
	retval = map_entry_dn_inbound_ext(remote_entry, &mapped_sdn, prp->agmt, 0 /* use_guid */, 0 /* use_username */);
	if (retval != 0) {
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"unable to map remote entry to local DN\n");
		return retval;
	}

	/* Compare the local and mapped RDNs if it is a group */
	/* If they don't match, set it to newrdn. */
	if (is_group && strcmp(slapi_entry_get_ndn(local_entry),
	                       slapi_sdn_get_ndn(mapped_sdn))) {
		newdn = slapi_sdn_get_dn(mapped_sdn);
		slapi_rdn_set_dn(&rdn, newdn);
		newrdn = slapi_rdn_get_rdn(&rdn);
	}

	/* compare the parents */
	retval = windows_get_superior_change(prp,
										 slapi_entry_get_sdn(local_entry),
										 mapped_sdn,
										 &newsuperior, 0 /* to_windows */);

	if (newsuperior || newrdn) {
		/* remote parent is different from the local parent */
		Slapi_PBlock *pb = slapi_pblock_new ();

		if (NULL == newrdn) {
			newdn = slapi_entry_get_dn_const(local_entry);
			slapi_rdn_set_dn(&rdn, newdn);
			newrdn = slapi_rdn_get_rdn(&rdn);
		}

		/* rename entry */
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name, "renaming entry \"%s\" - "
				"(newrdn: \"%s\", newsuperior: \"%s\"\n", newdn,
				newrdn ? newrdn:"NULL", newsuperior ? newsuperior:"NULL");
		slapi_sdn_init_dn_byref(&newsuperior_sdn, newsuperior);
		slapi_rename_internal_set_pb_ext (pb,
				   slapi_entry_get_sdn(local_entry),
				   newrdn, &newsuperior_sdn, 1 /* delete old RDNS */,
				   NULL /* controls */, NULL /* uniqueid */,
				   repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
		slapi_modrdn_internal_pb (pb);
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &retval);
		slapi_sdn_done(&newsuperior_sdn);
		slapi_pblock_destroy (pb);
		if (LDAP_SUCCESS != retval) {
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
							"failed to rename entry (\"%s\"); LDAP error - %d "
							"(newrdn: \"%s\", newsuperior: \"%s\"\n", newdn, retval,
							newrdn ? newrdn:"NULL", newsuperior ? newsuperior:"NULL");
			slapi_ch_free_string(&newsuperior);
			slapi_rdn_done(&rdn);
			goto bail;
		}
		slapi_ch_free_string(&newsuperior);
		slapi_rdn_done(&rdn);

		/* We renamed the local entry, so we need to fetch it again using the new
		 * DN.  If we do this, we need to be sure to free the entry.  The caller
		 * will free the original local entry that was passed to us.*/
		orig_local_entry = local_entry;
		retval = windows_get_local_entry(mapped_sdn, &local_entry);
		if (retval != 0) {
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"failed to get local entry \"%s\" after rename\n",
					slapi_sdn_get_ndn(mapped_sdn));
			local_entry = orig_local_entry;
			orig_local_entry = NULL;
			goto bail;
		}
	}


	slapi_mods_init (&smods, 0);

	retval = windows_generate_update_mods(prp,remote_entry,local_entry,0,&smods,&do_modify);
	/* Now perform the modify if we need to */
	if (0 == retval && do_modify)
	{
		int rc = 0;
		pb = slapi_pblock_new();
		if (pb)
		{
			const char *dn = slapi_entry_get_dn_const(local_entry);
			slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"modifying entry: %s\n", dn);
			slapi_modify_internal_set_pb_ext (pb,
			    slapi_entry_get_sdn(local_entry),
			    slapi_mods_get_ldapmods_byref(&smods), NULL, NULL,
			    repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
			slapi_modify_internal_pb (pb);
			slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
			if (is_user) {
				winsync_plugin_call_post_ds_mod_user_cb(prp->agmt,
						windows_private_get_raw_entry(prp->agmt),
						remote_entry, /* the cooked ad entry */
						local_entry, /* the ds entry */
						&smods, &rc);
			} else if (is_group) {
				winsync_plugin_call_post_ds_mod_group_cb(prp->agmt,
						windows_private_get_raw_entry(prp->agmt),
						remote_entry, /* the cooked ad entry */
						local_entry, /* the ds entry */
						&smods, &rc);
			}
			if (rc) 
			{
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_update_local_entry: failed to modify entry %s - error %d:%s\n",
					dn, rc, ldap_err2string(rc));
			}
			slapi_pblock_destroy(pb);
		} else 
		{
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"failed to make pb in windows_update_local_entry\n");
		}

	} else
	{
		const char *dn = slapi_entry_get_dn_const(local_entry);
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"no mods generated for local entry: %s\n", dn);
	}

bail:
	slapi_sdn_free(&mapped_sdn);
	slapi_mods_done(&smods);
	if (orig_local_entry) {
		slapi_entry_free(local_entry);
		local_entry = orig_local_entry;
	}

	return retval;
}

static int
windows_process_total_add(Private_Repl_Protocol *prp,Slapi_Entry *e, Slapi_DN* remote_dn, int missing_entry)
{
	int retval = 0;
	LDAPMod **entryattrs = NULL;
	Slapi_Entry *mapped_entry = NULL;
	char *password = NULL;
	const Slapi_DN* local_dn = NULL;
	int can_add = winsync_plugin_call_can_add_entry_to_ad_cb(prp->agmt, e, remote_dn);
	/* First map the entry */
	local_dn = slapi_entry_get_sdn_const(e);
	int is_user;
	if (missing_entry) {
		if (can_add) {
			retval = windows_create_remote_entry(prp, e, remote_dn, &mapped_entry, &password);
		} else {
			return retval; /* cannot add and no entry to modify */
		}
	}
	/* Convert entry to mods */
	windows_is_local_entry_user_or_group(e, &is_user, NULL);
	if (0 == retval && mapped_entry) 
	{
		if (is_user) {
			winsync_plugin_call_pre_ad_add_user_cb(prp->agmt, mapped_entry, e);
		} else {
			winsync_plugin_call_pre_ad_add_group_cb(prp->agmt, mapped_entry, e);
		}
		/* plugin may reset DN */
		slapi_sdn_copy(slapi_entry_get_sdn(mapped_entry), remote_dn);
		(void)slapi_entry2mods (mapped_entry , NULL /* &entrydn : We don't need it */, &entryattrs);
		if (NULL == entryattrs)
		{
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot convert entry to LDAPMods.\n",agmt_get_long_name(prp->agmt));
			retval = CONN_LOCAL_ERROR;
		}
		else
		{
			int ldap_op = 0;
			int ldap_result_code = 0;
			windows_log_add_entry_remote(local_dn, remote_dn);
			retval = windows_conn_send_add(prp->conn, slapi_sdn_get_dn(remote_dn), entryattrs, NULL, NULL /* returned controls */);
			windows_conn_get_error(prp->conn, &ldap_op, &ldap_result_code);
			if ((retval != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
				/* op failed but no ldap error code ??? */
				ldap_result_code = LDAP_OPERATIONS_ERROR;
			}
			if (is_user) {
				winsync_plugin_call_post_ad_add_user_cb(prp->agmt, mapped_entry, e, &ldap_result_code);
			} else {
				winsync_plugin_call_post_ad_add_group_cb(prp->agmt, mapped_entry, e, &ldap_result_code);
			}
			/* see if plugin reset success/error condition */
			if ((retval != CONN_OPERATION_SUCCESS) && !ldap_result_code) {
				retval = CONN_OPERATION_SUCCESS;
				windows_conn_set_error(prp->conn, ldap_result_code);
			} else if ((retval == CONN_OPERATION_SUCCESS) && ldap_result_code) {
				retval = CONN_OPERATION_FAILED;
				windows_conn_set_error(prp->conn, ldap_result_code);
			}
			/* It's possible that the entry already exists in AD, in which case we fall back to modify it */
			if (retval)
			{
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot replay add operation.\n",agmt_get_long_name(prp->agmt));
			}
			ldap_mods_free(entryattrs, 1);
			entryattrs = NULL;

			if ((retval == 0) && is_user) {
			    /* set the account control bits only for users */
			    retval = send_accountcontrol_modify(remote_dn, prp, missing_entry);
			}
		}
	} else
	{
		/* Entry already exists, need to mod it instead */
		Slapi_Entry *remote_entry = NULL;
		/* Get the remote entry */
		retval = windows_get_remote_entry(prp, remote_dn,&remote_entry);
		if (0 == retval && remote_entry) 
		{
			retval = windows_update_remote_entry(prp,remote_entry,e,is_user);
			/* Detect the case where the error is benign */
			if (retval)
			{
				int operation = 0;
				int error = 0;
				
				windows_conn_get_error(prp->conn, &operation, &error);
				if (windows_ignore_error_and_keep_going(error))
				{
					retval = CONN_OPERATION_SUCCESS;
				}
			}
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
	slapi_entry_free(mapped_entry);
	mapped_entry = NULL;
	slapi_ch_free_string(&password);
	return retval;
}

/* Entry point for the total protocol */
int windows_process_total_entry(Private_Repl_Protocol *prp,Slapi_Entry *e)
{
	int retval = 0;
	int is_ours = 0;
	Slapi_DN *remote_dn = NULL;
	int missing_entry = 0;
	const Slapi_DN *local_dn = slapi_entry_get_sdn_const(e);
	/* First check if the entry is for us */
	is_ours = is_subject_of_agreement_local(e, prp->agmt);
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: windows_process_total_entry: Looking dn=\"%s\" (%s)\n",
		agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), is_ours ? "ours" : "not ours");
	if (is_ours) 
	{
		retval = map_entry_dn_outbound(e,&remote_dn,prp,&missing_entry,0 /* we don't want the GUID */);
		if (retval || NULL == remote_dn) 
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: windows_replay_update: failed map dn for total update dn=\"%s\"\n",
				agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(local_dn));
			goto error;
		}
		retval = windows_process_total_add(prp,e,remote_dn,missing_entry);
	}
	if (remote_dn)
	{
		slapi_sdn_free(&remote_dn);
	}
error:
	return retval;
}

static int
windows_search_local_entry_by_uniqueid(Private_Repl_Protocol *prp, const char *uniqueid, char ** attrs, Slapi_Entry **ret_entry, int tombstone, void * component_identity, int is_global)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *int_search_pb = NULL;
    int rc = 0;
	char *filter_string = NULL;
	const Slapi_DN *local_subtree = NULL;
    
    *ret_entry = NULL;
	if (is_global) { /* Search from the suffix (rename case) */
		local_subtree = agmt_get_replarea(prp->agmt); 
	} else {
		local_subtree = windows_private_get_directory_subtree(prp->agmt);
	}

	/* Searching for tombstones can be expensive, so the caller needs to specify if
	 * we should search for a tombstone entry, or a normal entry. */
	if (tombstone) {
		filter_string = PR_smprintf("(&(objectclass=nsTombstone)(nsUniqueid=%s))", uniqueid);
	} else {
		filter_string = PR_smprintf("(&(|(objectclass=*)(objectclass=ldapsubentry))(nsUniqueid=%s))",uniqueid);
	}

    int_search_pb = slapi_pblock_new ();
	slapi_search_internal_set_pb ( int_search_pb,  slapi_sdn_get_dn(local_subtree), LDAP_SCOPE_SUBTREE, filter_string,
								   attrs ,
								   0 /* attrsonly */, NULL /* controls */,
								   NULL /* uniqueid */,
								   component_identity, 0 /* actions */ );
	slapi_search_internal_pb ( int_search_pb );
    slapi_pblock_get( int_search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc );
    if ( LDAP_SUCCESS == rc ) {
		slapi_pblock_get( int_search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries );
		if ( NULL != entries && NULL != entries[ 0 ]) {
			Slapi_Entry *temp_entry = NULL;
			temp_entry = entries[ 0 ];
			*ret_entry = slapi_entry_dup(temp_entry);
		} else {
			/* No entry there */
			rc = LDAP_NO_SUCH_OBJECT;
		}
    }
    slapi_free_search_results_internal(int_search_pb);
    slapi_pblock_destroy(int_search_pb);
	int_search_pb = NULL;
	if (filter_string)
	{
		PR_smprintf_free(filter_string);
	}

    if (is_global) slapi_sdn_free((Slapi_DN **)&local_subtree);
    return rc;
}

static int
windows_get_local_entry_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry, int is_global)
{
	int retval = ENTRY_NOTFOUND;
	Slapi_Entry *new_entry = NULL;
	windows_search_local_entry_by_uniqueid( prp, uniqueid, NULL, &new_entry, 0, /* Don't search tombstones */
		repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), is_global);
	if (new_entry) 
	{
		*local_entry = new_entry;
		retval = 0;
	} 
	return retval;
}

static int
windows_get_local_tombstone_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry)
{
	int retval = ENTRY_NOTFOUND;
	Slapi_Entry *new_entry = NULL;
	windows_search_local_entry_by_uniqueid( prp, uniqueid, NULL, &new_entry, 1, /* Search for tombstones */
			repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
	if (new_entry)
	{
		*local_entry = new_entry;
		retval = 0;
	}
	return retval;
}

static int
windows_get_local_entry(const Slapi_DN* local_dn,Slapi_Entry **local_entry)
{
	int retval = ENTRY_NOTFOUND;
	Slapi_Entry *new_entry = NULL;
	slapi_search_internal_get_entry( (Slapi_DN*)local_dn, NULL, &new_entry,
			repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION));
	if (new_entry) 
	{
		*local_entry = new_entry;
		retval = 0;
	} 
	return retval;
}

static int
windows_unsync_entry(Private_Repl_Protocol *prp, Slapi_Entry *e)
{
	/* remote the ntuser/ntgroup objectclass and all attributes whose
	   name begins with "nt" - this will effectively cause the entry
	   to become "unsynced" with the corresponding windows entry */
	Slapi_Mods *smods = NULL;
	Slapi_Value *ntu = NULL, *ntg = NULL;
	Slapi_Value *va[2] = {NULL, NULL};
	char **syncattrs = NULL;
	PRUint32 ocflags = SLAPI_OC_FLAG_REQUIRED|SLAPI_OC_FLAG_ALLOWED;
	Slapi_PBlock *pb = NULL;
	int ii;
	int rc = -1;

	smods = slapi_mods_new();
	ntu = slapi_value_new_string("ntuser");
	ntg = slapi_value_new_string("ntgroup");

	if (slapi_entry_attr_has_syntax_value(e, "objectclass", ntu)) {
		syncattrs = slapi_schema_list_objectclass_attributes(slapi_value_get_string(ntu), ocflags);
		va[0] = ntu;
	} else if (slapi_entry_attr_has_syntax_value(e, "objectclass", ntg)) {
		syncattrs = slapi_schema_list_objectclass_attributes(slapi_value_get_string(ntg), ocflags);
		va[0] = ntg;
	} else {
		rc = 0; /* not an error */
		goto done; /* nothing to see here, move along */
	}
	slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, "objectclass", va);
	slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"%s: windows_unsync_entry: removing objectclass %s from %s\n",
					agmt_get_long_name(prp->agmt), slapi_value_get_string(va[0]),
					slapi_entry_get_dn_const(e));

	for (ii = 0; syncattrs && syncattrs[ii]; ++ii) {
		const char *type = syncattrs[ii];
		Slapi_Attr *attr = NULL;

		if (!slapi_entry_attr_find(e, type, &attr) && attr) {
			if (!PL_strncasecmp(type, "nt", 2)) { /* begins with "nt" */
				slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, type, NULL);
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
								"%s: windows_unsync_entry: removing attribute %s from %s\n",
								agmt_get_long_name(prp->agmt), type,
								slapi_entry_get_dn_const(e));
			}
		}
	}

	pb = slapi_pblock_new();
	if (!pb) {
		goto done;
	}
	slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
					"%s: windows_unsync_entry: modifying entry %s\n",
					agmt_get_long_name(prp->agmt), slapi_entry_get_dn_const(e));
	slapi_modify_internal_set_pb_ext(pb, slapi_entry_get_sdn(e),
									 slapi_mods_get_ldapmods_byref(smods), NULL, NULL,
									 repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
	slapi_modify_internal_pb(pb);
	slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (rc) {
		slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
						"%s: windows_unsync_entry: failed to modify entry %s - error %d:%s\n",
						agmt_get_long_name(prp->agmt), slapi_entry_get_dn_const(e),
						rc, ldap_err2string(rc));
	}
	slapi_pblock_destroy(pb);

done:
	slapi_ch_array_free(syncattrs);
	slapi_mods_free(&smods);
	slapi_value_free(&ntu);
	slapi_value_free(&ntg);

	return rc;
}

static int 
windows_process_dirsync_entry(Private_Repl_Protocol *prp,Slapi_Entry *e, int is_total)
{
	Slapi_DN* local_sdn = NULL;
	int rc = 0;
	int retried = 0;
	Slapi_Entry *found_entry = NULL;

	/* deleted users are outside the 'correct container'. 
	They live in cn=deleted objects, windows_private_get_directory_subtree( prp->agmt) */

	if (is_tombstone(prp, e))
	{
		rc = map_tombstone_dn_inbound(e, &local_sdn, prp->agmt);
		if ((0 == rc) && local_sdn)
		{
			/* Go ahead and delte the local peer */
			rc = windows_delete_local_entry(local_sdn);
			slapi_sdn_free(&local_sdn);
		} else
		{
			slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: failed to map tombstone dn.\n",agmt_get_long_name(prp->agmt));
		}
	} else 
	{
		/* Is this entry one we should be interested in ? */
		if (is_subject_of_agreement_remote(e,prp->agmt)) 
		{
retry:
			/* First make its local DN */
			rc = map_entry_dn_inbound(e, &local_sdn, prp->agmt);
			if ((0 == rc) && local_sdn) 
			{
				Slapi_Entry *local_entry = NULL;
				/* Get the local entry if it exists */
				rc = windows_get_local_entry(local_sdn,&local_entry);
				if ((0 == rc) && local_entry) 
				{
					/* Since the entry exists we should now make it match the entry we received from AD */
					/* Actually we are better off simply fetching the entire entry from AD and using that 
					 * because otherwise we don't get all the attributes we need to make sense of it such as
					 * objectclass */
					Slapi_Entry *remote_entry = NULL;
					windows_get_remote_entry(prp,slapi_entry_get_sdn_const(e),&remote_entry);
					if (remote_entry)
					{
						/* We need to check for any deleted attrs from the dirsync entry
						 * and pass them into the newly fetched remote entry. */
						Slapi_Attr *attr = NULL;
						Slapi_Attr *rem_attr = NULL;
						entry_first_deleted_attribute(e, &attr);
						while (attr != NULL) {
							/* We need to dup the attr and add it to the remote entry.
							 * rem_attr is now owned by remote_entry, so don't free it */
							rem_attr = slapi_attr_dup(attr);
							if (rem_attr) {
								entry_add_deleted_attribute_wsi(remote_entry, rem_attr);
								rem_attr = NULL;
							}
							entry_next_deleted_attribute(e, &attr);
						}

						rc = windows_update_local_entry(prp, remote_entry, local_entry);
						slapi_entry_free(remote_entry);
						remote_entry = NULL;
					} else 
					{
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: failed to fetch inbound entry.\n",agmt_get_long_name(prp->agmt));
					}
					slapi_entry_free(local_entry);
					if (rc) {
						/* Something bad happened */
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: failed to update inbound entry for %s.\n",agmt_get_long_name(prp->agmt),
							slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)));
					}
				} else 
				{
					/* If it doesn't exist, try to make it */
					if (add_local_entry_allowed(prp,e))
					{
						windows_create_local_entry(prp,e,local_sdn);
					} else
					{
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: not allowed to add entry %s.\n",agmt_get_long_name(prp->agmt)
							, slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)));
					}
				}
				slapi_sdn_free(&local_sdn);
			} else if (0 == retried) {
				/* We should try one more thing. */
				/* In case a remote entry is moved from the outside of scope of
			 	 * the agreement into the inside, the entry e only has
				 * attributes: parentguid, objectguid, instancetype, name.
			 	 * We search Windows with the dn and retry using the found 
				 * entry.
			 	 */
				ConnResult cres = 0;
				const char *searchbase = slapi_entry_get_dn_const(e);
				char *filter = "(objectclass=*)";

				retried = 1;
				cres = windows_search_entry_ext(prp->conn, (char*)searchbase, 
												filter, &found_entry, NULL, LDAP_SCOPE_BASE);
				if (0 == cres && found_entry) {
					/* 
					 * Entry e originally allocated in windows_dirsync_inc_run
					 * is freed in windows_dirsync_inc_run.  Assigning 
					 * found_entry to e does not break the logic.
					 * "found_entry" is freed at the end of this function.
					 */
					e = found_entry;
					goto retry;
				}
			} else {
				/* We should have been able to map the DN, so this is an error */
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
								"%s: windows_process_dirsync_entry: failed to map "
								"inbound entry %s - rc is %d dn is [%s].\n",
								agmt_get_long_name(prp->agmt),
								slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)),
								rc,
								local_sdn ? slapi_sdn_get_dn(local_sdn) : "null");
			}
		} /* subject of agreement */
		else { /* The remote entry might be moved out of scope of agreement. */
			/* First make its local DN */
			rc = map_entry_dn_inbound(e, &local_sdn, prp->agmt);
			if ((0 == rc) && local_sdn) 
			{
				Slapi_Entry *local_entry = NULL;
				/* Get the local entry if it exists */
				rc = windows_get_local_entry(local_sdn, &local_entry);
				if ((0 == rc) && local_entry) 
				{
					if (windows_private_get_move_action(prp->agmt) == MOVE_DOES_DELETE) {
						/* Need to delete the local entry since the remote counter
						 * part is now moved out of scope of the agreement. */
						/* Since map_Entry_dn_oubound returned local_sdn,
						 * the entry is either user or group. */
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
										"%s: windows_process_dirsync_entry: deleting out of "
										"scope entry %s\n", agmt_get_long_name(prp->agmt),
										slapi_sdn_get_dn(local_sdn));
						rc = windows_delete_local_entry(local_sdn);
					} else if (windows_private_get_move_action(prp->agmt) == MOVE_DOES_UNSYNC) {
						rc = windows_unsync_entry(prp, local_entry);
					} else {
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
										"%s: windows_process_dirsync_entry: windows "
										"inbound entry %s has the same name as local "
										"entry %s but the windows entry is out of the "
										"scope of the sync subtree [%s] - if you want "
										"these entries to be in sync, add the ntUser/ntGroup "
										"objectclass and required attributes to the local "
										"entry, and move the windows entry into scope\n",
										agmt_get_long_name(prp->agmt),
										slapi_entry_get_dn_const(e),
										slapi_sdn_get_dn(local_sdn),
										slapi_sdn_get_dn(windows_private_get_windows_subtree(prp->agmt)));
					}
				}
				slapi_entry_free(local_entry);
				slapi_sdn_free(&local_sdn);
			}
		}
	} /* is tombstone */
	slapi_entry_free(found_entry);
	return rc;
}

void 
windows_dirsync_inc_run(Private_Repl_Protocol *prp)
	{ 
	
	int rc = 0;
	int done = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_dirsync_inc_run\n", 0, 0, 0 );
	while (!done) {

		Slapi_Entry *e = NULL;

		rc = send_dirsync_search(prp->conn);
		if (rc != CONN_OPERATION_SUCCESS)
		{
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"failed to send dirsync search request: %d\n", rc);
			goto error;
		}

		while ( (e = windows_conn_get_search_result(prp->conn) ) != NULL)
		{
			rc = windows_process_dirsync_entry(prp,e,0);
			if (e) 
			{
				slapi_entry_free(e);
			}
		} /* While entry != NULL */
		if (!windows_private_dirsync_has_more(prp->agmt)) 
		{
			done = 1;
		}
	} /* While !done */
error:
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_dirsync_inc_run\n", 0, 0, 0 );
}

