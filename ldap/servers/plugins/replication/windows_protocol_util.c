/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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


static Slapi_Entry* windows_entry_already_exists(Slapi_Entry *e);
static void windows_dirsync_now  (Private_Repl_Protocol *prp);
static Slapi_DN* map_dn_user(Slapi_DN *sdn, int map_to, const Slapi_DN *root);
static Slapi_DN* map_dn_group(Slapi_DN *sdn, int map_to, const Slapi_DN *root);
static void make_mods_from_entries(Slapi_Entry *new_entry, Slapi_Entry *existing_entry, LDAPMod ***attrs);
static void windows_map_mods_for_replay(Private_Repl_Protocol *prp,LDAPMod **original_mods, LDAPMod ***returned_mods, int is_user, char** password);
static int is_subject_of_agreemeent_local(const Slapi_Entry *local_entry,const Repl_Agmt *ra);
static int windows_create_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *original_entry, Slapi_DN *remote_sdn, Slapi_Entry **remote_entry, char** password);
static int windows_get_local_entry(const Slapi_DN* local_dn,Slapi_Entry **local_entry);
static int windows_get_local_entry_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry);
static int map_entry_dn_outbound(Slapi_Entry *e, const Slapi_DN **dn, Private_Repl_Protocol *prp, int *missing_entry, int want_guid);
static char* extract_ntuserdomainid_from_entry(Slapi_Entry *e);
static int windows_get_remote_entry (Private_Repl_Protocol *prp, Slapi_DN* remote_dn,Slapi_Entry **remote_entry);
static const char* op2string (int op);
static int is_subject_of_agreemeent_remote(Slapi_Entry *e, const Repl_Agmt *ra);
static int map_entry_dn_inbound(Slapi_Entry *e, const Slapi_DN **dn, const Repl_Agmt *ra);
static int windows_update_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry);


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
	"seeAlso",
	"sn",
	"st",
	"street",
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
	"seeAlso",
	"sn",
	"st",
	"street",
	"telephoneNumber",
	"teletexTerminalIdentifier",
	"telexNumber",
	"title",
	"userCertificate",
	"x121Address",
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
	{ "profilePath", "ntUserParms", bidirectional, always, normal},
	{ "userParameters", "ntUserProfile", bidirectional, always, normal},
	{ "userWorkstations", "ntUserWorkstations", bidirectional, always, normal},
    { "sAMAccountName", "ntUserDomainId", bidirectional, createonly, normal},
	/* cn is a naming attribute in AD, so we don't want to change it after entry creation */
    { "cn", "cn", towindowsonly, createonly, normal},
	/* However, it isn't a naming attribute in DS (we use uid) so it's safe to accept changes inbound */
    { "name", "cn", fromwindowsonly, always, normal},
    { "manager", "manager", bidirectional, always, dnmap},
    { "secretary", "secretary", bidirectional, always, dnmap},
    { "seealso", "seealso", bidirectional, always, dnmap},
	{NULL, NULL, -1}
};

static windows_attribute_map group_attribute_map[] = 
{
	{ "groupType", "ntGroupType",  bidirectional, createonly, normal},
	{ "sAMAccountName", "ntUserDomainId", bidirectional, createonly, normal},
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
			slapi_ch_free((void**)&buffer);
		}
	}
}

static void
map_dn_values(Private_Repl_Protocol *prp,Slapi_ValueSet *original_values, Slapi_ValueSet **mapped_values, int to_windows)
{
	Slapi_ValueSet *new_vs = NULL;
	Slapi_Value *original_value = NULL;
	int retval = 0;
	int i = 0;
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
		/* Make a sdn from the string */
		original_dn = slapi_sdn_new_dn_byref(original_dn_string);
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
				is_ours = is_subject_of_agreemeent_local(local_entry,prp->agmt);
				if (is_ours)
				{
					map_entry_dn_outbound(local_entry,&remote_dn,prp,&missing_entry, 0 /* don't want GUID form here */);
					if (remote_dn)
					{
						if (!missing_entry)
						{
							/* Success */
							new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(remote_dn));
						}
						slapi_sdn_free(&remote_dn);
					}
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
			is_ours = is_subject_of_agreemeent_remote(remote_entry,prp->agmt);
			if (is_ours)
			{
				retval = map_entry_dn_inbound(remote_entry,&local_dn,prp->agmt);	
				if (0 == retval && local_dn)
				{
					new_dn_string = slapi_ch_strdup(slapi_sdn_get_dn(local_dn));
					slapi_sdn_free(&local_dn);
				} else
				{
					slapi_log_error(SLAPI_LOG_REPL, NULL, "map_dn_values: no remote entry found for %s\n", original_dn_string);
				}
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
		if (original_dn)
		{
			slapi_sdn_free(&original_dn);
		}
    }/* while */
	if (new_vs)
	{
		*mapped_values = new_vs;
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
	ConnResult crc;
	Repl_Connection *conn;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_acquire_replica\n", 0, 0, 0 );

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

	{
		Replica *replica;
		Object *supl_ruv_obj, *cons_ruv_obj;
		PRBool is_newer = PR_FALSE;
		RUV *r;

	
		if (prp->agmt)
		{		
			cons_ruv_obj = agmt_get_consumer_ruv(prp->agmt);
		}





		object_acquire(prp->replica_object);
		replica = object_get_data(prp->replica_object);
		supl_ruv_obj = replica_get_ruv ( replica );

		/* make a copy of the existing RUV as a starting point 
		   XXX this is probably a not-so-elegant hack */

		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV:\n");
		if (supl_ruv_obj) {
		object_acquire(supl_ruv_obj);
			ruv_dump ((RUV*)  object_get_data ( supl_ruv_obj ), "supplier", NULL);
			object_release(supl_ruv_obj);
		}else
				slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, supplier RUV = null\n");
		
		slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV:\n");

		if (cons_ruv_obj) 
		{
			RUV* con;
			object_acquire(cons_ruv_obj);
			con =  (RUV*) object_get_data ( cons_ruv_obj );
			ruv_dump (con,"consumer", NULL);
			object_release( cons_ruv_obj );
		} else {
			slapi_log_error(SLAPI_LOG_REPL, NULL, "acquire_replica, consumer RUV = null\n");
		}

		is_newer = ruv_is_newer ( supl_ruv_obj, cons_ruv_obj );
		
		/* This follows ruv_is_newer, since it's always newer if it's null */
		if (cons_ruv_obj == NULL) 
		{
			RUV *s;
			s = (RUV*)  object_get_data ( replica_get_ruv ( replica ) );
			
			agmt_set_consumer_ruv(prp->agmt, s );
			object_release ( replica_get_ruv ( replica ) );
			cons_ruv_obj =  agmt_get_consumer_ruv(prp->agmt);		
		}

		r = (RUV*)  object_get_data ( cons_ruv_obj); 
		*ruv = r;
		


		if ( supl_ruv_obj ) object_release ( supl_ruv_obj );
		if ( cons_ruv_obj ) object_release ( cons_ruv_obj );
		object_release (prp->replica_object);
		replica = NULL;

 		if (is_newer == PR_FALSE && check_ruv) { 
 			prp->last_acquire_response_code = NSDS50_REPL_UPTODATE;
			LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_acquire_replica - ACQUIRE_CONSUMER_WAS_UPTODATE\n", 0, 0, 0 );
 			return ACQUIRE_CONSUMER_WAS_UPTODATE; 
 		} 
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

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_acquire_replica\n", 0, 0, 0 );

	return return_value;
}

void
windows_release_replica(Private_Repl_Protocol *prp)
{

  struct berval *retdata = NULL;
  char *retoid = NULL;
  struct berval *payload = NULL;
  Slapi_DN *replarea_sdn = NULL;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_release_replica\n", 0, 0, 0 );

  PR_ASSERT(NULL != prp);
  PR_ASSERT(NULL != prp->conn);

  if (!prp->replica_acquired)
    return;

  windows_conn_start_linger(prp->conn);

  prp->replica_acquired = PR_FALSE;

  LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_release_replica\n", 0, 0, 0 );

}

/* this entry had a password, handle it seperately */
/* http://support.microsoft.com/?kbid=269190 */
static int
send_password_modify(Slapi_DN *sdn, char *password, Private_Repl_Protocol *prp)
{
		ConnResult pw_return = 0;
		LDAPMod *pw_mods[2];
		LDAPMod pw_mod;
		struct berval bv = {0};
		UChar *unicode_password = NULL;
		int32_t unicode_password_length = 0; /* Length in _characters_ */
		int32_t buffer_size = 0; /* Size in _characters_ */
		UErrorCode error = U_ZERO_ERROR;
		char *quoted_password = NULL;
		struct berval *bvals[2];

		/* AD wants the password in quotes ! */
		quoted_password = PR_smprintf("\"%s\"",password);
		if (quoted_password)
		{
			/* Need to UNICODE encode the password here */
			/* It's one of those 'ask me first and I will tell you the buffer size' functions */
			u_strFromUTF8(NULL, 0, &unicode_password_length, quoted_password, strlen(quoted_password), &error);
			buffer_size = unicode_password_length;
			unicode_password = (UChar *)slapi_ch_malloc(unicode_password_length * sizeof(UChar));
			if (unicode_password) {
				error = U_ZERO_ERROR;
				u_strFromUTF8(unicode_password, buffer_size, &unicode_password_length, quoted_password, strlen(quoted_password), &error);

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

		return pw_return;
}

static int
windows_entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value)
{
	int retval = 0;
	Slapi_Attr *attr = 0;
	if (!e || !attrname)
		return retval;

	/* see if the entry has the specified attribute name */
	if (!slapi_entry_attr_find(e, attrname, &attr) && attr)
	{
		/* if value is not null, see if the attribute has that
		   value */
		if (!value)
		{
			retval = 1;
		}
		else
		{
			Slapi_Value *v = 0;
			int index = 0;
			for (index = slapi_attr_first_value(attr, &v);
				 v && (index != -1);
				 index = slapi_attr_next_value(attr, index, &v))
			{
				const char *s = slapi_value_get_string(v);
				if (!s)
					continue;

				if (!strcasecmp(s, value))
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
	*is_user = windows_entry_has_attr_and_value(e,"objectclass","ntuser");
	*is_group = windows_entry_has_attr_and_value(e,"objectclass","ntgroup");
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
		return -1;
	}
	if (is_user && is_group) 
	{
		/* Now that's just really strange... */
		return -1;
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

static ConnResult
process_replay_add(Private_Repl_Protocol *prp, slapi_operation_parameters *op, Slapi_Entry *local_entry, Slapi_DN *local_dn, Slapi_DN *remote_dn, int is_user, int missing_entry, char **password)
{
	int remote_add_allowed = add_remote_entry_allowed(local_entry);
	ConnResult return_value = 0;
	int rc = 0;

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: process_replay_add: dn=\"%s\" (%s,%s)\n",
		agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn), missing_entry ? "not present" : "already present" , remote_add_allowed ? "add allowed" : "add not allowed");

	if (missing_entry)
	{
		if (remote_add_allowed) {
			LDAPMod **entryattrs = NULL;
			Slapi_Entry *mapped_entry = NULL;
			/* First map the entry */
			rc = windows_create_remote_entry(prp,op->p.p_add.target_entry, remote_dn, &mapped_entry, password);
			/* Convert entry to mods */
			if (0 == rc && mapped_entry) 
			{
				(void)slapi_entry2mods (mapped_entry , NULL /* &entrydn : We don't need it */, &entryattrs);
				slapi_entry_free(mapped_entry);
				mapped_entry = NULL;
				if (NULL == entryattrs)
				{
					slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot convert entry to LDAPMods.\n",agmt_get_long_name(prp->agmt));
					return_value = CONN_LOCAL_ERROR;
				}
				else
				{
					windows_log_add_entry_remote(local_dn, remote_dn);
					return_value = windows_conn_send_add(prp->conn, slapi_sdn_get_dn(remote_dn), entryattrs, NULL, NULL);
					/* It's possible that the entry already exists in AD, in which case we fall back to modify it */
					if (return_value)
					{
						slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot replay add operation.\n",agmt_get_long_name(prp->agmt));
					}
					ldap_mods_free(entryattrs, 1);
					entryattrs = NULL;
				}
			} else 
			{
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"%s: process_replay_add: failed to create mapped entry dn=\"%s\"\n",agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn));
			}
		}
	} else 
	{

		Slapi_Entry *remote_entry = NULL;

		/* Fetch the remote entry */
		rc = windows_get_remote_entry(prp, remote_dn,&remote_entry);
		if (0 == rc && remote_entry) {
			return_value = windows_update_remote_entry(prp,remote_entry,local_entry);
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
	return return_value;
}

/*
 * Given a changelog entry, construct the appropriate LDAP operations to sync
 * the operation to AD.
 */
ConnResult
windows_replay_update(Private_Repl_Protocol *prp, slapi_operation_parameters *op)
{
	ConnResult return_value = 0;
	LDAPControl *update_control = NULL; /* No controls used for AD */
	int rc = 0;
	char *password = NULL;
	int is_ours = 0;
	int is_user = 0;
	int is_group = 0;
	Slapi_DN *remote_dn = NULL;
	Slapi_DN *local_dn = NULL;
	Slapi_Entry *local_entry = NULL;
		
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_replay_update\n", 0, 0, 0 );

	local_dn = slapi_sdn_new_dn_byref( op->target_address.dn );

	/* Since we have the target uniqueid in the op structure, let's
	 * fetch the local entry here using it.
	 */
	
	rc = windows_get_local_entry_by_uniqueid(prp, op->target_address.uniqueid, &local_entry);

	if (rc) 
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: windows_replay_update: failed to fetch local entry for %s operation dn=\"%s\"\n",
			agmt_get_long_name(prp->agmt),
			op2string(op->operation_type), op->target_address.dn);
		goto error;
	}

	is_ours = is_subject_of_agreemeent_local(local_entry, prp->agmt);
	windows_is_local_entry_user_or_group(local_entry,&is_user,&is_group);

	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: windows_replay_update: Looking at %s operation local dn=\"%s\" (%s,%s,%s)\n",
		agmt_get_long_name(prp->agmt),
		op2string(op->operation_type), op->target_address.dn, is_ours ? "ours" : "not ours", 
		is_user ? "user" : "not user", is_group ? "group" : "not group");

	if (is_ours && (is_user || is_group) ) {
		int missing_entry = 0;
		/* Make the entry's DN */
		rc = map_entry_dn_outbound(local_entry,&remote_dn,prp,&missing_entry, 1);
		if (rc || NULL == remote_dn) 
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: windows_replay_update: failed map dn for %s operation dn=\"%s\"\n",
				agmt_get_long_name(prp->agmt),
				op2string(op->operation_type), op->target_address.dn);
			goto error;
		}
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
			"%s: windows_replay_update: Processing %s operation local dn=\"%s\" remote dn=\"%s\"\n",
			agmt_get_long_name(prp->agmt),
			op2string(op->operation_type), op->target_address.dn, slapi_sdn_get_dn(remote_dn));
		switch (op->operation_type) {
		/* For an ADD operation, we map the entry and then send the operation, which may fail if the peer entry already existed */
		case SLAPI_OPERATION_ADD:
			return_value = process_replay_add(prp,op,local_entry,local_dn,remote_dn,is_user,missing_entry,&password);
			break;
		case SLAPI_OPERATION_MODIFY:
			{
				LDAPMod **mapped_mods = NULL;

				windows_map_mods_for_replay(prp,op->p.p_modify.modify_mods, &mapped_mods, is_user, &password);
				/* It's possible that the mapping process results in an empty mod list, in which case we don't bother with the replay */
				if ( mapped_mods == NULL || *(mapped_mods)== NULL )
				{
					return_value = CONN_OPERATION_SUCCESS;
				} else 
				{
					return_value = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(remote_dn), mapped_mods, update_control,NULL /* returned controls */);
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
				return_value = windows_conn_send_delete(prp->conn, slapi_sdn_get_dn(remote_dn), update_control, NULL /* returned controls */);
					slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: windows_replay_update: deleted remote entry, dn=\"%s\", result=%d\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn), return_value);
			} else 
			{
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
					"%s: windows_replay_update: delete not allowed on remote entry, dn=\"%s\"\n",
					agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(remote_dn));
			}
			break;
		case SLAPI_OPERATION_MODRDN:
			return_value = windows_conn_send_rename(prp->conn, op->target_address.dn,
				op->p.p_modrdn.modrdn_newrdn,
				op->p.p_modrdn.modrdn_newsuperior_address.dn,
				op->p.p_modrdn.modrdn_deloldrdn,
				update_control, NULL /* returned controls */);
			break;
		default:
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name, "%s: replay_update: Unknown "
				"operation type %d found in changelog - skipping change.\n",
				agmt_get_long_name(prp->agmt), op->operation_type);
		}
		if (password) 
		{
			return_value = send_password_modify(remote_dn, password, prp);
			if (return_value)
			{
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name, "%s: windows_replay_update: update password returned %d\n",
					agmt_get_long_name(prp->agmt), return_value );
			}
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
	return return_value;
}

static int
is_straight_mapped_attr(const char *type, int is_user /* or group */)
{
	int found = 0;
	size_t offset = 0;
	char *this_attr = NULL;
	char **list = is_user ? windows_user_matching_attributes : windows_group_matching_attributes;
	/* Look for the type in the list of straight mapped attrs for the appropriate object type */
	while (this_attr = list[offset])
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
	while(this_map = &(our_map[offset]))
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
	Slapi_PBlock* pb = NULL;
	int rc = 0;
	int is_user = 0; 
	int is_group = 0;
	Slapi_Attr *attr = NULL;
	char *username = NULL;
	const char *dn_string = NULL;
	char *remote_entry_template = NULL;
	char *fqusername = NULL;
	const char *domain_name = windows_private_get_windows_domain(prp->agmt); 

	char *remote_user_entry_template = 
		"dn: %s\n"
		"objectclass:top\n"
   		"objectclass:person\n"
		"objectclass:organizationalperson\n"
		"objectclass:user\n"
		"userPrincipalName:%s\n"
		"userAccountControl:512\n";

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
	slapi_ch_free((void**)&entry_string);
	if (NULL == new_entry) 
	{
		goto error;
	}
	/* Map the appropriate attributes sourced from the remote entry */
	/* Iterate over the local entry's attributes */
    for (rc = slapi_entry_first_attr(original_entry, &attr); rc == 0;
			rc = slapi_entry_next_attr(original_entry, attr, &attr)) 
	{
		Slapi_Value	*value = NULL;
		char *type = NULL;
		Slapi_ValueSet *vs = NULL;
		int mapdn = 0;

		slapi_attr_get_type( attr, &type );
		slapi_attr_get_valueset(attr,&vs);

		if ( is_straight_mapped_attr(type,is_user) )
		{
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
					map_dn_values(prp,vs,&mapped_values, 1 /* to windows */);
					if (mapped_values) 
					{
						slapi_entry_add_valueset(new_entry,new_type,mapped_values);
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					}
				} else 
				{
					slapi_entry_add_valueset(new_entry,new_type,vs);
				}
				slapi_ch_free((void**)&new_type);
			}
			/* password mods are treated specially */
			if (0 == slapi_attr_type_cmp(type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD, SLAPI_TYPE_CMP_SUBTYPE) )
			{
				const char *password_value = NULL;
				Slapi_Value *value = NULL;

				slapi_valueset_first_value(vs,&value);
				password_value = slapi_value_get_string(value);
				*password = slapi_ch_strdup(password_value);
			}

		}
		if (vs) 
		{
			slapi_valueset_free(vs);
			vs = NULL;
		}
	}
	if (remote_entry) 
	{
		*remote_entry = new_entry;
	}
error:
	if (username)
	{
		slapi_ch_free((void**)&username);
	}
	if (new_entry) 
	{
		windows_dump_entry("Created new remote entry:\n",new_entry);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_create_remote_entry: %d\n", retval, 0, 0 );
	return retval;
}

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

static void 
windows_map_mods_for_replay(Private_Repl_Protocol *prp,LDAPMod **original_mods, LDAPMod ***returned_mods, int is_user, char** password) 
{
	Slapi_Mods smods = {0};
	Slapi_Mods mapped_smods = {0};
	LDAPMod *mod = NULL;
	int i=0; 

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_map_mods_for_replay\n", 0, 0, 0 );

	/* Iterate through the original mods, looking each attribute type up in the maps for either user or group */
	
	slapi_mods_init_byref(&smods, original_mods);
	slapi_mods_init(&mapped_smods,10);
	mod = slapi_mods_get_first_mod(&smods);
	while(mod)
	{
		char *attr_type = mod->mod_type;
		int mapdn = 0;

		/* Check to see if this attribute is passed through */
		if (is_straight_mapped_attr(attr_type,is_user)) {
			/* If so then just copy over the mod */
			slapi_mods_add_modbvps(&mapped_smods,mod->mod_op,attr_type,mod->mod_bvalues);
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
					map_dn_values(prp,vs,&mapped_values, 1 /* to windows */);
					if (mapped_values) 
					{
						slapi_mods_add_mod_values(&mapped_smods,mod->mod_op,mapped_type,valueset_get_valuearray(mapped_values));
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					}
					slapi_mod_done(&smod);
					slapi_valueset_free(vs);
				} else 
				{
					slapi_mods_add_modbvps(&mapped_smods,mod->mod_op,mapped_type,mod->mod_bvalues);
				}
				slapi_ch_free((void**)&mapped_type);
			} else 
			{
				/* password mods are treated specially */
				if (0 == slapi_attr_type_cmp(attr_type, PSEUDO_ATTR_UNHASHEDUSERPASSWORD, SLAPI_TYPE_CMP_SUBTYPE) )
				{
					char *password_value = NULL;
					password_value = mod->mod_bvalues[0]->bv_val;
					*password = slapi_ch_strdup(password_value);
				}
			}
		}
		/* Otherwise we do not copy this mod at all */
		mod = slapi_mods_get_next_mod(&smods);
	}
	slapi_mods_done (&smods);
	/* Extract the mods for the caller */
	*returned_mods = slapi_mods_get_ldapmods_passout(&mapped_smods);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_map_mods_for_replay\n", 0, 0, 0 );
}

/* Returns non-zero if the attribute value sets are identical */
static int 
attr_compare_equal(Slapi_Attr *a, Slapi_Attr *b)
{
	/* For now only handle single values */
	Slapi_Value *va = NULL;
	Slapi_Value *vb = NULL;
	int num_a = 0;
	int num_b = 0;
	int match = 1;

	slapi_attr_get_numvalues(a,&num_a);
	slapi_attr_get_numvalues(b,&num_b);

	if (num_a == num_b) 
	{
		slapi_attr_first_value(a, &va);
		slapi_attr_first_value(b, &vb);

		if (va->bv.bv_len == va->bv.bv_len) 
		{
			if (0 != memcmp(va->bv.bv_val,vb->bv.bv_val,va->bv.bv_len))
			{
				match = 0;
			}
		} else
		{
			match = 0;
		}
	} else
	{
		match = 0;
	}
	return match;
}

/* Helper functions for dirsync result processing */

/* Is this entry a tombstone ? */
static int 
is_tombstone(Slapi_Entry *e)
{
	int retval = 0;

	char *string_deleted = "(isdeleted=*)";

	/* DBDB: we should allocate these filters once and keep them around for better performance */
	Slapi_Filter *filter_deleted = slapi_str2filter( string_deleted );
	
	/* DBDB: this should be one filter, the code originally tested separately and hasn't been fixed yet */
	if ( (slapi_filter_test_simple( e, filter_deleted ) == 0) )
	{
		retval = 1;
	}

	slapi_filter_free(filter_deleted,1);
	filter_deleted = NULL;

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

	filter = PR_smprintf("(%s=%s)",attribute,value);
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
windows_get_remote_entry (Private_Repl_Protocol *prp, Slapi_DN* remote_dn,Slapi_Entry **remote_entry)
{
	int retval = 0;
	ConnResult cres = 0;
	char *filter = "(objectclass=*)";
	const char *searchbase = NULL;
	Slapi_Entry *found_entry = NULL;

	searchbase = slapi_sdn_get_dn(remote_dn);
	cres = windows_search_entry(prp->conn, (char*)searchbase, filter, &found_entry);
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

    if (pb == NULL)
        goto done;

    query = slapi_ch_smprintf("(%s=%s)", attribute, value);

    if (query == NULL)
		goto done;

	subtree_dn = slapi_sdn_get_dn(windows_private_get_directory_subtree(ra));

    slapi_search_internal_set_pb(pb, subtree_dn,
        LDAP_SCOPE_SUBTREE, query, NULL, 0, NULL, NULL,
        (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_ch_free((void **)&query);

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

/* Remove dashes from a GUID string */
static void
dedash(char *str)
{
	char *p = str;
	char c = '\0';

	while (c = *p)
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

/* For reasons not clear, the GUID returned in the tombstone DN is all 
 * messed up, like the guy in the movie 'the fly' after he want in the tranporter device */
static void
decrypt(char *guid)
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
	slapi_ch_free((void**)&cpy);
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
	colon_offset = strchr(dn,':');
	/* Then scan forward to the next ',' */
	comma_offset = strchr(dn,',');
	/* The characters inbetween are the GUID, copy them to a new string and return to the caller */
	if (comma_offset && colon_offset && comma_offset > colon_offset) {
		guid = slapi_ch_malloc(comma_offset - colon_offset);
		strncpy(guid,colon_offset+1,(comma_offset-colon_offset)-1);
		guid[comma_offset-colon_offset-1] = '\0';
		/* Finally remove the dashes since we don't store them on our side */
		dedash(guid);
		decrypt(guid);
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
		new_dn = slapi_sdn_new();
		if (is_nt4)
		{
			dn_string = PR_smprintf("GUID=%s,%s",guid,suffix);
		} else
		{
			dn_string = PR_smprintf("<GUID=%s>",guid);
		}
		slapi_sdn_init_dn_byval(new_dn,dn_string);
		PR_smprintf_free(dn_string);
	}
	/* dn string is now inside the Slapi_DN, and will be freed by its owner */
	return new_dn;
}

/* Given a non-tombstone entry, return the DN of its peer in AD (whether present or not) */
static int 
map_entry_dn_outbound(Slapi_Entry *e, const Slapi_DN **dn, Private_Repl_Protocol *prp, int *missing_entry, int guid_form)
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
	 * check here.
	 */
	
	*missing_entry = 0;

	guid = slapi_entry_attr_get_charptr(e,"ntUniqueId");
	if (guid && guid_form) 
	{
		new_dn = make_dn_from_guid(guid, is_nt4, suffix);
		slapi_ch_free((void**)&guid);
	} else 
	{
		/* No GUID found, try ntUserDomainId */
		Slapi_Entry *remote_entry = NULL;
		char *username = slapi_entry_attr_get_charptr(e,"ntUserDomainId");
		if (username) {
			retval = find_entry_by_attr_value_remote("samAccountName",username,&remote_entry,prp);
			if (0 == retval && remote_entry) 
			{
				/* Get the entry's DN */
				new_dn = slapi_sdn_new();
				slapi_sdn_copy(slapi_entry_get_sdn_const(remote_entry), new_dn);
			} else {
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
					cn_string = slapi_entry_attr_get_charptr(e,"cn");
					if (!cn_string) 
					{
						cn_string = slapi_entry_attr_get_charptr(e,"ntuserdomainid");
					}
					if (cn_string) 
					{
						char *rdnstr = NULL;
						
						rdnstr = is_nt4 ? "samaccountname=%s,%s" : "cn=%s,%s";

						new_dn_string = PR_smprintf(rdnstr,cn_string,suffix);
						if (new_dn_string)
						{
							new_dn = slapi_sdn_new_dn_byval(new_dn_string);
							PR_smprintf_free(new_dn_string);
						}
						slapi_ch_free((void**)&cn_string);
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
			slapi_ch_free((void**)&username);
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
	if (new_dn) 
	{
		*dn = new_dn;
	}
	return retval;
}

/* Given a tombstone entry, return the DN of its peer in this server (if present) */
static int 
map_tombstone_dn_inbound(Slapi_Entry *e, const Slapi_DN **dn, const Repl_Agmt *ra)
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
		slapi_ch_free((void**)&guid);
	}
	if (matching_entry)
	{
		slapi_entry_free(matching_entry);
	}
	return retval;
}

/* Given a non-tombstone entry, return the DN of its peer in this server (whether present or not) */
static int 
map_entry_dn_inbound(Slapi_Entry *e, const Slapi_DN **dn, const Repl_Agmt *ra)
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

	guid = extract_guid_from_entry(e, is_nt4);
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
					goto error;
				}
			}
		} else 
		{
			/* We found the matching entry : get its DN */
			new_dn = slapi_sdn_dup(slapi_entry_get_sdn_const(matching_entry));
		}
	}
	/* If we failed to lookup by guid, try samaccountname */
	if (NULL == new_dn) 
	{
		username = extract_username_from_entry(e);
		if (username) {
			retval = find_entry_by_username(username,&matching_entry,ra);
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
						goto error;
					}
				}
			} else 
			{
				/* We found the matching entry : get its DN */
				new_dn = slapi_sdn_dup(slapi_entry_get_sdn_const(matching_entry));
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
			/* Local DNs for users and groups are different */
			if (is_user)
			{
				new_dn_string = PR_smprintf("uid=%s,%s",username,suffix);
			} else
			{
				new_dn_string = PR_smprintf("cn=%s,%s",username,suffix);
			}
			new_dn = slapi_sdn_new_dn_byval(new_dn_string);
			PR_smprintf_free(new_dn_string);
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
		slapi_ch_free((void **) &username);
	}
	return retval;
}

/* Tests if the entry is subject to our agreement (i.e. is it in the sync'ed subtree in this server, and is it the right objectclass
 * and does it have the right attribute values for sync ?) 
 */
static int 
is_subject_of_agreemeent_local(const Slapi_Entry *local_entry, const Repl_Agmt *ra)
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
			/* DBDB: we should allocate these filters once and keep them around for better performance */
			char *string_filter = "(&(|(objectclass=ntuser)(objectclass=ntgroup))(ntUserDomainId=*))";
			Slapi_Filter *filter = slapi_str2filter( string_filter );
			
			if (slapi_filter_test_simple( (Slapi_Entry*)local_entry, filter ) == 0)
			{
				retval = 1;
			}

			slapi_filter_free(filter,1);
			filter = NULL;
		} else 
		{
			/* Error: couldn't find the entry */
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
				"failed to find entry in is_subject_of_agreemeent_local: %d\n", retval);
			retval = 0;
		}
	}
error:
	return retval;
}

/* Tests if the entry is subject to our agreement (i.e. is it in the sync'ed subtree in AD and either a user or a group ?) */
static int 
is_subject_of_agreemeent_remote(Slapi_Entry *e, const Repl_Agmt *ra)
{
	int retval = 0;
	int is_in_subtree = 0;
	const Slapi_DN *agreement_subtree = NULL;
	
	/* First test for the sync'ed subtree */
	agreement_subtree = windows_private_get_windows_subtree(ra);
	if (NULL == agreement_subtree) 
	{
		goto error;
	}
	is_in_subtree = slapi_sdn_scope_test(slapi_entry_get_sdn_const(e), agreement_subtree, LDAP_SCOPE_SUBTREE);
	if (is_in_subtree) 
	{
		retval = 1;
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
   		"objectclass:mailGroup\n"
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
	slapi_ch_free((void**)&entry_string);
	if (NULL == local_entry) 
	{
		goto error;
	}
	/* Map the appropriate attributes sourced from the remote entry */
    for (rc = slapi_entry_first_attr(remote_entry, &attr); rc == 0;
			rc = slapi_entry_next_attr(remote_entry, attr, &attr)) 
	{
		Slapi_Value	*value = NULL;
		char *type = NULL;
		Slapi_ValueSet *vs = NULL;
		int mapdn = 0;

		slapi_attr_get_type( attr, &type );
		slapi_attr_get_valueset(attr,&vs);

		if ( is_straight_mapped_attr(type,is_user) )
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
					map_dn_values(prp,vs,&mapped_values, 0 /* from windows */);
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
				slapi_ch_free((void**)&new_type);
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
	if (is_nt4)
	{
		slapi_entry_add_string(local_entry,"sn",username);
	}
	/* Store it */
	windows_dump_entry("Adding new local entry",local_entry);
	pb = slapi_pblock_new();
	slapi_add_entry_internal_set_pb(pb, local_entry, NULL,repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),0);  
	slapi_add_internal_pb(pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &retval);

	if (retval) {
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"add operation of entry %s returned: %d\n", slapi_sdn_get_dn(local_sdn), retval);
	}
error:
	if (pb)
	{
		slapi_pblock_destroy(pb);
	}
	if (username) 
	{
		slapi_ch_free((void**)&username);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_create_local_entry\n", 0, 0, 0 );
	return retval;
}

static int
windows_generate_update_mods(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry, int to_windows, Slapi_Mods *smods, int *do_modify)
{
	int retval = 0;
	Slapi_Attr *attr = NULL;
	int is_user = 0;
	int is_group = 0;
	int rc = 0;
	int is_nt4 = windows_private_get_isnt4(prp->agmt);
	/* Iterate over the attributes on the remote entry, updating the local entry where appropriate */
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_update_local_entry\n", 0, 0, 0 );

	*do_modify = 0;
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
		Slapi_Value	*value = NULL;
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
			if ( is_straight_mapped_attr(type,is_user) ) {
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
		slapi_entry_attr_find(local_entry,local_type,&local_attr);
		is_present_local = (NULL == local_attr) ? 0 : 1;
		/* Is the attribute present on the local entry ? */
		if (is_present_local && !is_guid)
		{
			int values_equal = attr_compare_equal(attr,local_attr);
			/* If it is then we need to replace the local values with the remote values if they are different */
			if (!values_equal)
			{
				if (mapdn)
				{
					Slapi_ValueSet *mapped_values = NULL;
					map_dn_values(prp,vs,&mapped_values, to_windows);
					if (mapped_values) 
					{
						slapi_mods_add_mod_values(smods,LDAP_MOD_REPLACE,local_type,valueset_get_valuearray(mapped_values));
						slapi_valueset_free(mapped_values);
						mapped_values = NULL;
					}
				} else
				{
					slapi_mods_add_mod_values(smods,LDAP_MOD_REPLACE,local_type,valueset_get_valuearray(vs));
				}
				*do_modify = 1;
			} else
			{
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"windows_update_local_entry: %s, %s : values are equal\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)), local_type);
			}
		} else
		{
			if (!is_present_local)
			{
				/* If it is currently absent, then we add the value from the remote entry */
				if (is_guid)
				{
					/* Translate the guid value */
					char *guid = extract_guid_from_entry(remote_entry, is_nt4);
					if (guid)
					{
						slapi_mods_add_string(smods,LDAP_MOD_ADD,local_type,guid);
						slapi_ch_free((void**)&guid);
					}
				} else
				{
					/* Handle DN valued attributes here */
					if (mapdn)
					{
						Slapi_ValueSet *mapped_values = NULL;
						map_dn_values(prp,vs,&mapped_values, to_windows);
						if (mapped_values) 
						{
							slapi_mods_add_mod_values(smods,LDAP_MOD_ADD,local_type,valueset_get_valuearray(mapped_values));
							slapi_valueset_free(mapped_values);
							mapped_values = NULL;
						}
					} else
					{
						slapi_mods_add_mod_values(smods,LDAP_MOD_ADD,local_type,valueset_get_valuearray(vs));
					}
				}
				*do_modify = 1;
			}
		}
		if (vs) 
		{
			slapi_valueset_free(vs);
			vs = NULL;
		}
		if (local_type)
		{
			slapi_ch_free((void**)&local_type);
			local_type = NULL;
		}
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_update_local_entry: %d\n", retval, 0, 0 );
	return retval;
}

static int
windows_update_remote_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry)
{
    Slapi_Mods smods = {0};
	int retval = 0;
	int do_modify = 0;

    slapi_mods_init (&smods, 0);
	retval = windows_generate_update_mods(prp,local_entry,remote_entry,1,&smods,&do_modify);
	/* Now perform the modify if we need to */
	if (0 == retval && do_modify)
	{
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"windows_update_remote_entry: modifying entry %s\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)));

		retval = windows_conn_send_modify(prp->conn, slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)),slapi_mods_get_ldapmods_byref(&smods), NULL,NULL);
	} else
	{
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"no mods generated for entry: %s\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)));
	}
    slapi_mods_done(&smods);
	return retval;
}

static int
windows_update_local_entry(Private_Repl_Protocol *prp,Slapi_Entry *remote_entry,Slapi_Entry *local_entry)
{
    Slapi_Mods smods = {0};
	int retval = 0;
	int rc = 0;
	Slapi_PBlock *pb = NULL;
	int do_modify = 0;

    slapi_mods_init (&smods, 0);

	retval = windows_generate_update_mods(prp,remote_entry,local_entry,0,&smods,&do_modify);
	/* Now perform the modify if we need to */
	if (0 == retval && do_modify)
	{
		int rc = 0;
		pb = slapi_pblock_new();
		if (pb)
		{
			slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
				"modifying entry: %s\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)));
			slapi_modify_internal_set_pb (pb, slapi_entry_get_ndn(local_entry), slapi_mods_get_ldapmods_byref(&smods), NULL, NULL,
					repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION), 0);
			slapi_modify_internal_pb (pb);		
			slapi_pblock_get (pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
			if (rc) 
			{
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
					"windows_update_local_entry: failed to modify entry %s\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(local_entry)));
			}
			slapi_pblock_destroy(pb);
		} else 
		{
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,
			"failed to make pb in windows_update_local_entry\n");
		}

	} else
	{
		slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,
			"no mods generated for entry: %s\n", slapi_sdn_get_dn(slapi_entry_get_sdn_const(remote_entry)));
	}
    slapi_mods_done(&smods);
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
	/* First map the entry */
	local_dn = slapi_entry_get_sdn_const(e);
	if (missing_entry)
	retval = windows_create_remote_entry(prp, e, remote_dn, &mapped_entry, &password);
	/* Convert entry to mods */
	if (0 == retval && mapped_entry) 
	{
		(void)slapi_entry2mods (mapped_entry , NULL /* &entrydn : We don't need it */, &entryattrs);
		slapi_entry_free(mapped_entry);
		mapped_entry = NULL;
		if (NULL == entryattrs)
		{
			slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot convert entry to LDAPMods.\n",agmt_get_long_name(prp->agmt));
			retval = CONN_LOCAL_ERROR;
		}
		else
		{
			windows_log_add_entry_remote(local_dn, remote_dn);
			retval = windows_conn_send_add(prp->conn, slapi_sdn_get_dn(remote_dn), entryattrs, NULL, NULL /* returned controls */);
			/* It's possible that the entry already exists in AD, in which case we fall back to modify it */
			if (retval)
			{
				slapi_log_error(SLAPI_LOG_FATAL, windows_repl_plugin_name,"%s: windows_replay_update: Cannot replay add operation.\n",agmt_get_long_name(prp->agmt));
			}
			ldap_mods_free(entryattrs, 1);
			entryattrs = NULL;
		}
	} else
	{
		/* Entry already exists, need to mod it instead */
		Slapi_Entry *remote_entry = NULL;
		/* Get the remote entry */
		retval = windows_get_remote_entry(prp, remote_dn,&remote_entry);
		if (0 == retval && remote_entry) 
		{
			retval = windows_update_remote_entry(prp,remote_entry,e);
		}
		if (remote_entry)
		{
			slapi_entry_free(remote_entry);
		}
	}
	return retval;
}

static int 
windows_process_total_delete(Private_Repl_Protocol *prp,Slapi_Entry *e, Slapi_DN* remote_dn)
{
	int retval = 0;
	if (delete_remote_entry_allowed(e))
	{			
		retval = windows_conn_send_delete(prp->conn, slapi_sdn_get_dn(remote_dn), NULL, NULL /* returned controls */);
	}
	return retval;
}

/* Entry point for the total protocol */
int windows_process_total_entry(Private_Repl_Protocol *prp,Slapi_Entry *e)
{
	int retval = 0;
	int is_ours = 0;
	int is_tombstone = 0;
	Slapi_DN *remote_dn = NULL;
	int missing_entry = 0;
	const Slapi_DN *local_dn = slapi_entry_get_sdn_const(e);
	/* First check if the entry is for us */
	is_ours = is_subject_of_agreemeent_local(e, prp->agmt);
	slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
		"%s: windows_process_total_entry: Looking dn=\"%s\" (%s)\n",
		agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)), is_ours ? "ours" : "not ours");
	if (is_ours) 
	{
		retval = map_entry_dn_outbound(e,&remote_dn,prp,&missing_entry,1 /* want GUID */);
		if (retval || NULL == remote_dn) 
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"%s: windows_replay_update: failed map dn for total update dn=\"%s\"\n",
				agmt_get_long_name(prp->agmt), slapi_sdn_get_dn(local_dn));
			goto error;
		}
		/* Either the entry is a tombstone, or not a tombstone */
		if (is_tombstone)
		{
			retval = windows_process_total_delete(prp,e,remote_dn);
		} else
		{
			retval = windows_process_total_add(prp,e,remote_dn,missing_entry);
		}
	}
	if (remote_dn)
	{
		slapi_sdn_free(&remote_dn);
	}
error:
	return retval;
}

int
windows_search_local_entry_by_uniqueid(Private_Repl_Protocol *prp, const char *uniqueid, char ** attrs, Slapi_Entry **ret_entry , void * component_identity)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *int_search_pb = NULL;
    int rc = 0;
	char *filter_string = NULL;
	const Slapi_DN *local_subtree = NULL;
    
    *ret_entry = NULL;
	local_subtree = windows_private_get_directory_subtree(prp->agmt);
	filter_string = PR_smprintf("(&(|(objectclass=*)(objectclass=ldapsubentry)(objectclass=nsTombstone))(nsUniqueid=%s))",uniqueid);
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
    return rc;
}

static int
windows_get_local_entry_by_uniqueid(Private_Repl_Protocol *prp,const char* uniqueid,Slapi_Entry **local_entry)
{
	int retval = ENTRY_NOTFOUND;
	Slapi_Entry *new_entry = NULL;
	windows_search_local_entry_by_uniqueid( prp, uniqueid, NULL, &new_entry,
			repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION));
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
windows_process_dirsync_entry(Private_Repl_Protocol *prp,Slapi_Entry *e, int is_total)
{
	Slapi_DN* local_sdn = NULL;
	int rc = 0;

	/* deleted users are outside the 'correct container'. 
	They live in cn=deleted objects, windows_private_get_directory_subtree( prp->agmt) */

	if (is_tombstone(e))
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
		if (is_subject_of_agreemeent_remote(e,prp->agmt)) 
		{
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
					rc = windows_update_local_entry(prp, e, local_entry);
					slapi_entry_free(local_entry);
					if (rc) {
						/* Something bad happened */
						slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: failed to update inbound entry.\n",agmt_get_long_name(prp->agmt));
					}
				} else 
				{
					/* If it doesn't exist, try to make it */
					windows_create_local_entry(prp,e,local_sdn);
				}
				slapi_sdn_free(&local_sdn);
			} else 
			{
				/* We should have been able to map the DN, so this is an error */
				slapi_log_error(SLAPI_LOG_REPL, windows_repl_plugin_name,"%s: windows_process_dirsync_entry: failed to map inbound entry.\n",agmt_get_long_name(prp->agmt));
			}
		} /* subject of agreement */
	} /* is tombstone */
	return rc;
}

void 
windows_dirsync_inc_run(Private_Repl_Protocol *prp)
	{ 
	
	int rc = 0;
	int msgid=0;
    Slapi_PBlock *pb = NULL;
	Slapi_Filter *filter_user = NULL;
	Slapi_Filter *filter_user_deleted = NULL;
	Slapi_Filter *filter_group = NULL;
	Slapi_Filter *filter_group_deleted = NULL;
	int done = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_dirsync_inc_run\n", 0, 0, 0 );
	while (!done) {

		Slapi_Entry *e = NULL;
		int filter_ret = 0;
		PRBool create_users_from_dirsync = windows_private_create_users(prp->agmt);

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

