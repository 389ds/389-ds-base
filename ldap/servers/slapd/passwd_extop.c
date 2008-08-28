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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Password Modify - LDAP Extended Operation.
 * RFC 3062
 *
 *
 * This plugin implements the "Password Modify - LDAP3" 
 * extended operation for LDAP. The plugin function is called by
 * the server if an LDAP client request contains the OID:
 * "1.3.6.1.4.1.4203.1.11.1".
 *
 */

#include <stdio.h>
#include <string.h>
#include <private/pprio.h>


#include <prio.h>
#include <ssl.h>
#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"

/* Type of connection for this operation;*/
#define LDAP_EXTOP_PASSMOD_CONN_SECURE

/* Uncomment the following line FOR TESTING: allows non-SSL connections to use the password change extended op */
/* #undef LDAP_EXTOP_PASSMOD_CONN_SECURE */

/* ber tags for the PasswdModifyRequestValue sequence */
#define LDAP_EXTOP_PASSMOD_TAG_USERID	0x80U
#define LDAP_EXTOP_PASSMOD_TAG_OLDPWD	0x81U
#define LDAP_EXTOP_PASSMOD_TAG_NEWPWD	0x82U

/* OID of the extended operation handled by this plug-in */
#define EXOP_PASSWD_OID	"1.3.6.1.4.1.4203.1.11.1"


Slapi_PluginDesc passwdopdesc = { "passwd_modify_plugin", "Fedora", "0.1",
	"Password Modify extended operation plugin" };

/* Check SLAPI_USERPWD_ATTR attribute of the directory entry 
 * return 0, if the userpassword attribute contains the given pwd value
 * return -1, if userPassword attribute is absent for given Entry
 * return LDAP_INVALID_CREDENTIALS,if userPassword attribute and given pwd don't match
 */
static int passwd_check_pwd(Slapi_Entry *targetEntry, const char *pwd){
	int rc = LDAP_SUCCESS;
	Slapi_Attr *attr = NULL;
	Slapi_Value cv;
	Slapi_Value **bvals; 

	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_check_pwd\n", 0, 0, 0 );
	
	slapi_value_init_string(&cv,pwd);
	
	if ( (rc = slapi_entry_attr_find( targetEntry, SLAPI_USERPWD_ATTR, &attr )) == 0 )
	{ /* we have found the userPassword attribute and it has some value */
		bvals = attr_get_present_values( attr );
		if ( slapi_pw_find_sv( bvals, &cv ) != 0 )
		{
			rc = LDAP_INVALID_CREDENTIALS;
		}
	}

	value_done(&cv);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_check_pwd: %d\n", rc, 0, 0 );
	
	/* if the userPassword attribute is absent then rc is -1 */
	return rc;
}


/* Searches the dn in directory, 
 *  If found	 : fills in slapi_entry structure and returns 0
 *  If NOT found : returns the search result as LDAP_NO_SUCH_OBJECT
 */
static int 
passwd_modify_getEntry( const char *dn, Slapi_Entry **e2 ) {
	int		search_result = 0;
	Slapi_DN 	sdn;
	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_modify_getEntry\n", 0, 0, 0 );
	slapi_sdn_init_dn_byref( &sdn, dn );
	if ((search_result = slapi_search_internal_get_entry( &sdn, NULL, e2,
 					plugin_get_default_component_id())) != LDAP_SUCCESS ){
	 LDAPDebug (LDAP_DEBUG_TRACE, "passwd_modify_getEntry: No such entry-(%s), err (%d)\n",
					 dn, search_result, 0);
	}

	slapi_sdn_done( &sdn );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_modify_getEntry: %d\n", search_result, 0, 0 );
	return search_result;
}


/* Construct Mods pblock and perform the modify operation 
 * Sets result of operation in SLAPI_PLUGIN_INTOP_RESULT 
 */
static int passwd_apply_mods(const char *dn, Slapi_Mods *mods) 
{
	Slapi_PBlock pb;
	Slapi_Operation *operation= NULL;
	int ret=0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_apply_mods\n", 0, 0, 0 );

	if (mods && (slapi_mods_get_num_mods(mods) > 0)) 
	{
		pblock_init(&pb);
		slapi_modify_internal_set_pb (&pb, dn, 
		  slapi_mods_get_ldapmods_byref(mods),
		  NULL, /* Controls */
		  NULL, /* UniqueID */
		  pw_get_componentID(), /* PluginID */
		  0); /* Flags */ 

	 /* Plugin operations are INTERNAL by default, bypass it to enforce ACL checks */
	 slapi_pblock_get (&pb, SLAPI_OPERATION, &operation);

	 ret =slapi_modify_internal_pb (&pb);
  
	 slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

	 if (ret != LDAP_SUCCESS){
	  LDAPDebug(LDAP_DEBUG_TRACE, "WARNING: passwordPolicy modify error %d on entry '%s'\n",
			ret, dn, 0);
	 }

	pblock_done(&pb);
 	}
 
 	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_apply_mods: %d\n", ret, 0, 0 );
 
 	return ret;
}



/* Modify the userPassword attribute field of the entry */
static int passwd_modify_userpassword(Slapi_Entry *targetEntry, const char *newPasswd)
{
	char *dn = NULL;
	int ret = 0;
	Slapi_Mods smods;
	
    LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_modify_userpassword\n", 0, 0, 0 );
	
	slapi_mods_init (&smods, 0);
	dn = slapi_entry_get_ndn( targetEntry );
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, SLAPI_USERPWD_ATTR, newPasswd);


	ret = passwd_apply_mods(dn, &smods);
 
	slapi_mods_done(&smods);
	
    LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_modify_userpassword: %d\n", ret, 0, 0 );

	return ret;
}

/* Password Modify Extended operation plugin function */
int
passwd_modify_extop( Slapi_PBlock *pb )
{
	char		*oid = NULL;
	char		*bindDN = NULL;
	char		*authmethod = NULL;
	char		*dn = NULL;
	char		*otdn = NULL;
	char		*oldPasswd = NULL;
	char		*newPasswd = NULL;
	char		*errMesg = NULL;
	int             ret=0, rc=0;
	unsigned long	tag=0, len=-1;
	struct berval *extop_value = NULL;
	BerElement	*ber = NULL;
	Slapi_Entry *targetEntry=NULL;
	Connection      *conn = NULL;
	/* Slapi_DN sdn; */

    	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_modify_extop\n", 0, 0, 0 );
	/* Get the pb ready for sending Password Modify Extended Responses back to the client. 
	 * The only requirement is to set the LDAP OID of the extended response to the EXOP_PASSWD_OID. */

	if ( slapi_pblock_set( pb, SLAPI_EXT_OP_RET_OID, EXOP_PASSWD_OID ) != 0 ) {
		errMesg = "Could not set extended response oid.\n";
		rc = LDAP_OPERATIONS_ERROR;
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
				 errMesg );
		goto free_and_return;
	}

	/* Before going any further, we'll make sure that the right extended operation plugin
	 * has been called: i.e., the OID shipped whithin the extended operation request must 
	 * match this very plugin's OID: EXOP_PASSWD_OID. */
	if ( slapi_pblock_get( pb, SLAPI_EXT_OP_REQ_OID, &oid ) != 0 ) {
		errMesg = "Could not get OID value from request.\n";
		rc = LDAP_OPERATIONS_ERROR;
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
				 errMesg );
		goto free_and_return;
	} else {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
				 "Received extended operation request with OID %s\n", oid );
	}
	
	if ( strcasecmp( oid, EXOP_PASSWD_OID ) != 0) {
	        errMesg = "Request OID does not match Passwd OID.\n";
		rc = LDAP_OPERATIONS_ERROR;
		goto free_and_return;
	} else {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
				 "Password Modify extended operation request confirmed.\n" );
	}
	
	/* Now , at least we know that the request was indeed a Password Modify one. */

#ifdef LDAP_EXTOP_PASSMOD_CONN_SECURE
	/* Allow password modify only for SSL/TLS established connections */
	conn = pb->pb_conn;
	if ( (conn->c_flags & CONN_FLAG_SSL) != CONN_FLAG_SSL) {
		errMesg = "Operation requires a secure connection.\n";
		rc = LDAP_CONFIDENTIALITY_REQUIRED;
		goto free_and_return;
	}
#endif

	/* Get the ber value of the extended operation */
	slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &extop_value);
	
	if ((ber = ber_init(extop_value)) == NULL)
	{
		errMesg = "PasswdModify Request decode failed.\n";
		rc = LDAP_PROTOCOL_ERROR;
		goto free_and_return;
	}

	/* Format of request to parse
	*
   	* PasswdModifyRequestValue ::= SEQUENCE {
     	* userIdentity    [0]  OCTET STRING OPTIONAL
     	* oldPasswd       [1]  OCTET STRING OPTIONAL
     	* newPasswd       [2]  OCTET STRING OPTIONAL }
	*/
	slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &extop_value);

	/* ber parse code */
	if ( ber_scanf( ber, "{") == LBER_ERROR )
    	{
    		LDAPDebug( LDAP_DEBUG_ANY,
    		    "ber_scanf failed :{\n", 0, 0, 0 );
    		errMesg = "ber_scanf failed\n";
		rc = LDAP_PROTOCOL_ERROR;
		goto free_and_return;
    	} else {
		tag = ber_peek_tag( ber, &len);
	}

	
	/* identify userID field by tags */
	if (tag == LDAP_EXTOP_PASSMOD_TAG_USERID )
	{
		if ( ber_scanf( ber, "a", &dn) == LBER_ERROR )
    		{
    		slapi_ch_free_string(&dn);
    		LDAPDebug( LDAP_DEBUG_ANY,
    		    "ber_scanf failed :{\n", 0, 0, 0 );
    		errMesg = "ber_scanf failed at userID parse.\n";
		rc = LDAP_PROTOCOL_ERROR;
		goto free_and_return;
    		}
		
		tag = ber_peek_tag( ber, &len);
	} 
	
	
	/* identify oldPasswd field by tags */
	if (tag == LDAP_EXTOP_PASSMOD_TAG_OLDPWD )
	{
		if ( ber_scanf( ber, "a", &oldPasswd ) == LBER_ERROR )
    		{
    		slapi_ch_free_string(&oldPasswd);
    		LDAPDebug( LDAP_DEBUG_ANY,
    		    "ber_scanf failed :{\n", 0, 0, 0 );
    		errMesg = "ber_scanf failed at oldPasswd parse.\n";
		rc = LDAP_PROTOCOL_ERROR;
		goto free_and_return;
    		}
		tag = ber_peek_tag( ber, &len);
	} else {
		errMesg = "Current passwd must be supplied by the user.\n";
		rc = LDAP_PARAM_ERROR;
		goto free_and_return;
	}
	
	/* identify newPasswd field by tags */
	if (tag ==  LDAP_EXTOP_PASSMOD_TAG_NEWPWD )
	{
		if ( ber_scanf( ber, "a", &newPasswd ) == LBER_ERROR )
    		{
    		slapi_ch_free_string(&newPasswd);
    		LDAPDebug( LDAP_DEBUG_ANY,
    		    "ber_scanf failed :{\n", 0, 0, 0 );
    		errMesg = "ber_scanf failed at newPasswd parse.\n";
		rc = LDAP_PROTOCOL_ERROR;
		goto free_and_return;
    		}
	} else {
		errMesg = "New passwd must be supplied by the user.\n";
		rc = LDAP_PARAM_ERROR;
		goto free_and_return;
	}
	
	/* Uncomment for debugging, otherwise we don't want to leak the password values into the log... */
	/* LDAPDebug( LDAP_DEBUG_ARGS, "passwd: dn (%s), oldPasswd (%s) ,newPasswd (%s)\n",
					 dn, oldPasswd, newPasswd); */

	 
	 if (oldPasswd == NULL || *oldPasswd == '\0') {
	 /* Refuse to handle this operation because current password is not provided */
		errMesg = "Current passwd must be supplied by the user.\n";
		rc = LDAP_PARAM_ERROR;
		goto free_and_return;
	 }
	 
	 /* We don't implement password generation, so if the request implies 
	  * that they asked us to do that, we must refuse to process it  */
	 if (newPasswd == NULL || *newPasswd == '\0') {
	 /* Refuse to handle this operation because we don't allow password generation */
		errMesg = "New passwd must be supplied by the user.\n";
		rc = LDAP_PARAM_ERROR;
		goto free_and_return;
	 }
	 
	 /* Get Bind DN */
	slapi_pblock_get( pb, SLAPI_CONN_DN, &bindDN );

	/* If the connection is bound anonymously, we must refuse to process this operation. */
	 if (bindDN == NULL || *bindDN == '\0') {
	 	/* Refuse the operation because they're bound anonymously */
		errMesg = "Anonymous Binds are not allowed.\n";
		rc = LDAP_INSUFFICIENT_ACCESS;
		goto free_and_return;
	 }
	 
	 /* Determine the target DN for this operation */
	 /* Did they give us a DN ? */
	 if (dn == NULL || *dn == '\0') {
	 	/* Get the DN from the bind identity on this connection */
        slapi_ch_free_string(&dn);
        dn = slapi_ch_strdup(bindDN);
		LDAPDebug( LDAP_DEBUG_ANY,
    		    "Missing userIdentity in request, using the bind DN instead.\n",
		     0, 0, 0 );
	 }
	 
	 slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, dn ); 

	 /* Now we have the DN, look for the entry */
	 ret = passwd_modify_getEntry(dn, &targetEntry);
	 /* If we can't find the entry, then that's an error */
	 if (ret) {
	 	/* Couldn't find the entry, fail */
		errMesg = "No such Entry exists.\n" ;
		rc = LDAP_NO_SUCH_OBJECT ;
		goto free_and_return;
	 }
	 
	 /* First thing to do is to ask access control if the bound identity has
	    rights to modify the userpassword attribute on this entry. If not, then
		we fail immediately with insufficient access. This means that we don't
		leak any useful information to the client such as current password
		wrong, etc.
	  */

	operation_set_target_spec (pb->pb_op, slapi_entry_get_sdn(targetEntry));
	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &pb->pb_op->o_isroot );

	/* In order to perform the access control check , we need to select a backend (even though
	 * we don't actually need it otherwise).
	 */
	{
		Slapi_Backend *be = NULL;

		be = slapi_mapping_tree_find_backend_for_sdn(slapi_entry_get_sdn(targetEntry));
		if (NULL == be) {
			errMesg = "Failed to find backend for target entry";
			rc = LDAP_OPERATIONS_ERROR;
			goto free_and_return;
		}
		slapi_pblock_set(pb, SLAPI_BACKEND, be);
	}

	ret = slapi_access_allowed ( pb, targetEntry, SLAPI_USERPWD_ATTR, NULL, SLAPI_ACL_WRITE );
    if ( ret != LDAP_SUCCESS ) {
		errMesg = "Insufficient access rights\n";
		rc = LDAP_INSUFFICIENT_ACCESS;
		goto free_and_return;	
	}
	 	 	 
	/* Now we have the entry which we want to modify
 	 * They gave us a password (old), check it against the target entry
	 * Is the old password valid ?
	 */
	ret = passwd_check_pwd(targetEntry, oldPasswd);
	if (ret) {
		/* No, then we fail this operation */
		errMesg = "Invalid oldPasswd value.\n";
		rc = ret;
		goto free_and_return;
	}
	

	/* Now we're ready to make actual password change */
	ret = passwd_modify_userpassword(targetEntry, newPasswd);
	if (ret != LDAP_SUCCESS) {
		/* Failed to modify the password, e.g. because insufficient access allowed */
		errMesg = "Failed to update password\n";
		rc = ret;
		goto free_and_return;
	}
	
	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_modify_extop: %d\n", rc, 0, 0 );
	
	/* Free anything that we allocated above */
	free_and_return:
	slapi_ch_free_string(&bindDN); /* slapi_pblock_get SLAPI_CONN_DN does strdup */
    slapi_ch_free_string(&oldPasswd);
    slapi_ch_free_string(&newPasswd);
    /* Either this is the same pointer that we allocated and set above,
       or whoever used it should have freed it and allocated a new
       value that we need to free here */
	slapi_pblock_get( pb, SLAPI_ORIGINAL_TARGET, &otdn );
	if (otdn != dn) {
		slapi_ch_free_string(&dn);
	}
    slapi_ch_free_string(&otdn);
	slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, NULL );
    slapi_ch_free_string(&authmethod);

	if ( targetEntry != NULL ){
		slapi_entry_free (targetEntry); 
	}
	
	if ( ber != NULL ){
		ber_free(ber, 1);
		ber = NULL;
	}
	
	
	slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
                     errMesg ? errMesg : "success" );
	send_ldap_result( pb, rc, NULL, errMesg, 0, NULL );
	

	return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );

}/* passwd_modify_extop */


static char *passwd_oid_list[] = {
	EXOP_PASSWD_OID,
	NULL
};


static char *passwd_name_list[] = {
	"passwd_modify_extop",
	NULL
};


/* Initialization function */
int passwd_modify_init( Slapi_PBlock *pb )
{
	char	**argv;
	char	*oid;

	/* Get the arguments appended to the plugin extendedop directive. The first argument 
	 * (after the standard arguments for the directive) should contain the OID of the
	 * extended operation.
	 */ 

	if ( slapi_pblock_get( pb, SLAPI_PLUGIN_ARGV, &argv ) != 0 ) {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_init", "Could not get argv\n" );
		return( -1 );
	}

	/* Compare the OID specified in the configuration file against the Passwd OID. */

	if ( argv == NULL || strcmp( argv[0], EXOP_PASSWD_OID ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_init", 
				 "OID is missing or is not %s\n", EXOP_PASSWD_OID );
		return( -1 );
	} else {
		oid = slapi_ch_strdup( argv[0] );
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_init", 
				 "Registering plug-in for Password Modify extended op %s.\n", oid );
	}

	/* Register the plug-in function as an extended operation
	 * plug-in function that handles the operation identified by
	 * OID 1.3.6.1.4.1.4203.1.11.1 .  Also specify the version of the server 
	 * plug-in */ 
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	     slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&passwdopdesc ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *) passwd_modify_extop ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, passwd_oid_list ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, passwd_name_list ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_init",
				 "Failed to set plug-in version, function, and OID.\n" );
		return( -1 );
	}
	
	return( 0 );
}

int passwd_modify_register_plugin()
{
	slapi_register_plugin( "extendedop", 1 /* Enabled */, "passwd_modify_init", 
			passwd_modify_init, "Password Modify extended operation",
			passwd_oid_list, NULL );

	return 0;
}

