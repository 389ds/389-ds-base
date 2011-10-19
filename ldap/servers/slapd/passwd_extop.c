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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
#include <plbase64.h>

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

/* ber tags for the PasswdModifyResponseValue sequence */
#define LDAP_EXTOP_PASSMOD_TAG_GENPWD	0x80U

/* number of bytes used for random password generation */
#define LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN 8

/* number of random bytes needed to generate password */
#define LDAP_EXTOP_PASSMOD_RANDOM_BYTES	6


Slapi_PluginDesc passwdopdesc = { "passwd_modify_plugin", VENDOR, DS_PACKAGE_VERSION,
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
static int 
passwd_apply_mods(Slapi_PBlock *pb_orig, const Slapi_DN *sdn, Slapi_Mods *mods,
	LDAPControl **req_controls, LDAPControl ***resp_controls) 
{
	Slapi_PBlock pb;
	LDAPControl **req_controls_copy = NULL;
	LDAPControl **pb_resp_controls = NULL;
	int ret=0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_apply_mods\n", 0, 0, 0 );

	if (mods && (slapi_mods_get_num_mods(mods) > 0)) 
	{
		/* We need to dup the request controls since the original
		 * pblock owns the ones that have been passed in. */
		if (req_controls) {
			slapi_add_controls(&req_controls_copy, req_controls, 1);
		}

		pblock_init(&pb);
		slapi_modify_internal_set_pb_ext (&pb, sdn, 
			slapi_mods_get_ldapmods_byref(mods),
			req_controls_copy, NULL, /* UniqueID */
			plugin_get_default_component_id(), /* PluginID */
			0); /* Flags */ 

		/* We copy the connection from the original pblock into the
		 * pblock we use for the internal modify operation.  We do
		 * this to allow the password policy code to be able to tell
		 * that the password change was initiated by the user who
		 * sent the extended operation instead of always assuming
		 * that it was done by the root DN. */
		pb.pb_conn = pb_orig->pb_conn;

		ret =slapi_modify_internal_pb (&pb);

		/* We now clean up the connection that we copied into the
		 * new pblock.  We want to leave it untouched. */
		pb.pb_conn = NULL;
  
		slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

		/* Retreive and duplicate the response controls since they will be
		 * destroyed along with the pblock used for the internal operation. */
		slapi_pblock_get(&pb, SLAPI_RESCONTROLS, &pb_resp_controls);
		if (pb_resp_controls) {
			slapi_add_controls(resp_controls, pb_resp_controls, 1);
		}

		if (ret != LDAP_SUCCESS){
			LDAPDebug(LDAP_DEBUG_TRACE, "WARNING: passwordPolicy modify error %d on entry '%s'\n",
				ret, slapi_sdn_get_dn(sdn), 0);
		}

		pblock_done(&pb);
 	}
 
 	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_apply_mods: %d\n", ret, 0, 0 );
 
 	return ret;
}



/* Modify the userPassword attribute field of the entry */
static int passwd_modify_userpassword(Slapi_PBlock *pb_orig, Slapi_Entry *targetEntry,
	const char *newPasswd, LDAPControl **req_controls, LDAPControl ***resp_controls)
{
	int ret = 0;
	Slapi_Mods smods;
	
    LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_modify_userpassword\n", 0, 0, 0 );
	
	slapi_mods_init (&smods, 0);
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, SLAPI_USERPWD_ATTR, newPasswd);


	ret = passwd_apply_mods(pb_orig, slapi_entry_get_sdn_const(targetEntry),
	                        &smods, req_controls, resp_controls);
 
	slapi_mods_done(&smods);
	
    LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_modify_userpassword: %d\n", ret, 0, 0 );

	return ret;
}

/* Generate a new, basic random password */
static int passwd_modify_generate_basic_passwd( int passlen, char **genpasswd )
{
	char *data = NULL;
	char *enc = NULL;
	int datalen = LDAP_EXTOP_PASSMOD_RANDOM_BYTES;

	if ( genpasswd == NULL ) {
		return LDAP_OPERATIONS_ERROR;
	}

	if ( passlen > 0 ) {
		datalen = passlen * 3 / 4 + 1;
	}

	data = slapi_ch_calloc( datalen, 1 );

	/* get random bytes from NSS */
	PK11_GenerateRandom( (unsigned char *)data, datalen );

	/* b64 encode the random bytes to get a password made up
	 * of printable characters. */
	enc = PL_Base64Encode( data, datalen, NULL );

	/* This will get freed by the caller */
	*genpasswd = slapi_ch_malloc( 1 + passlen );

	/* trim the password to the proper length */
	PL_strncpyz( *genpasswd, enc, passlen + 1 );

	slapi_ch_free_string( &data );
	slapi_ch_free_string( &enc );

	return LDAP_SUCCESS;
}

/* Generate a new, password-policy-based random password */
static int passwd_modify_generate_policy_passwd( passwdPolicy *pwpolicy,
											char **genpasswd, char **errMesg )
{
	unsigned char *data = NULL;
	int passlen = 0;
	int tmplen = 0;
	enum {
		idx_minuppers = 0,
		idx_minlowers,
		idx_mindigits,
		idx_minspecials,
		idx_end
	}; 
	int my_policy[idx_end];
	struct {
		int chr_start;
		int chr_range;
	} chr_table[] = { /* NOTE: the above enum order */
		{ 65, 26 }, /* [ A - Z ] */
		{ 97, 26 }, /* [ a - z ] */
		{ 48, 10 }, /* [ 0 - 9 ] */
		{ 58,  7 }  /* [ : - @ ] */
	};
#define gen_policy_pw_getchar(n, idx) \
( chr_table[(idx)].chr_start + (n) % chr_table[(idx)].chr_range )
	int i;

	if ( genpasswd == NULL ) {
		return LDAP_OPERATIONS_ERROR;
	}

	my_policy[idx_mindigits] = pwpolicy->pw_mindigits;
	my_policy[idx_minuppers] = pwpolicy->pw_minuppers;
	my_policy[idx_minlowers] = pwpolicy->pw_minlowers;
	my_policy[idx_minspecials] = pwpolicy->pw_minspecials;

	/* if only minalphas is set, divide it into minuppers and minlowers. */
	if ( pwpolicy->pw_minalphas > 0 &&
		 ( my_policy[idx_minuppers] == 0 && my_policy[idx_minlowers] == 0 )) {
		unsigned int x = (unsigned int)time(NULL);
		my_policy[idx_minuppers] = slapi_rand_r(&x) % pwpolicy->pw_minalphas;
		my_policy[idx_minlowers] = pwpolicy->pw_minalphas - my_policy[idx_minuppers];
	}

	if ( pwpolicy->pw_mincategories ) {
		int categories = 0;
		for ( i = 0; i < idx_end; i++ ) {
			if ( my_policy[i] > 0 ) {
				categories++;
			}
		}
		if ( pwpolicy->pw_mincategories > categories ) {
			categories = pwpolicy->pw_mincategories;
			for ( i = 0; i < idx_end; i++ ) {
				if ( my_policy[i] == 0 ) {
					/* force to add a policy to match the pw_mincategories */
					my_policy[i] = 1; 
				}
				if ( --categories == 0 ) {
					break;
				}
			}
			if ( categories > 0 ) {
				/* password generator does not support passwordMin8Bit */
    			LDAPDebug( LDAP_DEBUG_ANY,
					"Unable to generate a password that meets the current "
					"password syntax rules.  A minimum categories setting "
					"of %d is not supported with random password generation.\n",
					pwpolicy->pw_mincategories, 0, 0 );
				*errMesg = "Unable to generate new random password.  Please contact the Administrator.";
				return LDAP_CONSTRAINT_VIOLATION;
			}
		}
	}

	/* get the password length */
	tmplen = 0;
	for ( i = 0; i < idx_end; i++ ) {
		tmplen += my_policy[i];
	}
	passlen = tmplen;
	if ( passlen < pwpolicy->pw_minlength ) {
		passlen = pwpolicy->pw_minlength;
	}
	if ( passlen < LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN ) {
		passlen = LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN;
	}

	data = (unsigned char *)slapi_ch_calloc( passlen, 1 );

	/* get random bytes from NSS */
	PK11_GenerateRandom( data, passlen );

	/* if password length is longer the sum of my_policy's,
	   let them share the burden */
	if ( passlen > tmplen ) {
		unsigned int x = (unsigned int)time(NULL);
		int delta = passlen - tmplen;
		for ( i = 0; i < delta; i++ ) {
			my_policy[(x = slapi_rand_r(&x)) % idx_end]++;
		}
	}

	/* This will get freed by the caller */
	*genpasswd = slapi_ch_malloc( 1 + passlen );

	for ( i = 0; i < passlen; i++ ) {
		int idx = data[i] % idx_end;
		int isfirst = 1;
		/* choose a category based on the random value */
		while ( my_policy[idx] <= 0 ) {
			if ( ++idx == idx_end ) {
				idx = 0; /* if no rule is found, default is uppercase */
				if ( !isfirst ) {
					break;
				}
				isfirst = 0;
			}
		}
		my_policy[idx]--;
		(*genpasswd)[i] = gen_policy_pw_getchar(data[i], idx);
	}
	(*genpasswd)[passlen] = '\0';

	slapi_ch_free( (void **)&data );

	return LDAP_SUCCESS;
}

/* Generate a new random password */
static int passwd_modify_generate_passwd( passwdPolicy *pwpolicy,
										  char **genpasswd, char **errMesg )
{
	int minalphalen = 0;
	int passlen = LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN;
	int rval = LDAP_SUCCESS;

	if ( genpasswd == NULL ) {
		return LDAP_OPERATIONS_ERROR;
	}
	if ( pwpolicy->pw_min8bit > 0 ) {
    	LDAPDebug( LDAP_DEBUG_ANY, "Unable to generate a password that meets "
						"the current password syntax rules.  8-bit syntax "
						"restrictions are not supported with random password "
						"generation.\n", 0, 0, 0 );
		*errMesg = "Unable to generate new random password.  Please contact the Administrator.";
		return LDAP_CONSTRAINT_VIOLATION;
	}

	if ( pwpolicy->pw_minalphas || pwpolicy->pw_minuppers || 
		 pwpolicy->pw_minlowers || pwpolicy->pw_mindigits ||
		 pwpolicy->pw_minspecials || pwpolicy->pw_maxrepeats ||
		 pwpolicy->pw_mincategories > 2 ) {
		rval = passwd_modify_generate_policy_passwd( pwpolicy, genpasswd,
													 errMesg );
	} else {
		/* find out the minimum length to fulfill the passwd policy 
		   requirements */
		minalphalen = pwpolicy->pw_minuppers + pwpolicy->pw_minlowers;
		if ( minalphalen < pwpolicy->pw_minalphas ) {
			minalphalen = pwpolicy->pw_minalphas;
		}
		passlen = minalphalen + pwpolicy->pw_mindigits + 
				  pwpolicy->pw_minspecials + pwpolicy->pw_min8bit;
		if ( passlen < pwpolicy->pw_minlength ) {
			passlen = pwpolicy->pw_minlength;
		}
		if ( passlen < LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN ) {
			passlen = LDAP_EXTOP_PASSMOD_GEN_PASSWD_LEN;
		}
		rval = passwd_modify_generate_basic_passwd( passlen, genpasswd );
	}

	return rval;
}


/* Password Modify Extended operation plugin function */
int
passwd_modify_extop( Slapi_PBlock *pb )
{
	char		*oid = NULL;
	char 		*bindDN = NULL;
	Slapi_DN	*bindSDN = NULL;
	char		*authmethod = NULL;
	char		*rawdn = NULL;
	const char	*dn = NULL;
	char		*otdn = NULL;
	char		*oldPasswd = NULL;
	char		*newPasswd = NULL;
	char		*errMesg = NULL;
	int             ret=0, rc=0, sasl_ssf=0, local_ssf=0, need_pwpolicy_ctrl=0;
	ber_tag_t	tag=0;
	ber_len_t	len=(ber_len_t)-1;
	struct berval	*extop_value = NULL;
	struct berval	*gen_passwd = NULL;
	BerElement	*ber = NULL;
	BerElement	*response_ber = NULL;
	Slapi_Entry	*targetEntry=NULL;
	Connection      *conn = NULL;
	LDAPControl	**req_controls = NULL;
	LDAPControl	**resp_controls = NULL;
	passwdPolicy	*pwpolicy = NULL;
	Slapi_DN	*target_sdn = NULL;
	Slapi_Entry	*referrals = NULL;
	/* Slapi_DN sdn; */

    	LDAPDebug( LDAP_DEBUG_TRACE, "=> passwd_modify_extop\n", 0, 0, 0 );

	/* Before going any further, we'll make sure that the right extended operation plugin
	 * has been called: i.e., the OID shipped whithin the extended operation request must 
	 * match this very plugin's OID: EXTOP_PASSWD_OID. */
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
	
	if ( strcasecmp( oid, EXTOP_PASSWD_OID ) != 0) {
	        errMesg = "Request OID does not match Passwd OID.\n";
		rc = LDAP_OPERATIONS_ERROR;
		goto free_and_return;
	} else {
	        slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop", 
				 "Password Modify extended operation request confirmed.\n" );
	}
	
	/* Now , at least we know that the request was indeed a Password Modify one. */

#ifdef LDAP_EXTOP_PASSMOD_CONN_SECURE
	/* Allow password modify only for SSL/TLS established connections and
	 * connections using SASL privacy layers */
	conn = pb->pb_conn;
	if ( slapi_pblock_get(pb, SLAPI_CONN_SASL_SSF, &sasl_ssf) != 0) {
		errMesg = "Could not get SASL SSF from connection\n";
		rc = LDAP_OPERATIONS_ERROR;
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop",
				 errMesg );
		goto free_and_return;
	}

	if ( slapi_pblock_get(pb, SLAPI_CONN_LOCAL_SSF, &local_ssf) != 0) {
		errMesg = "Could not get local SSF from connection\n";
		rc = LDAP_OPERATIONS_ERROR;
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop",
				 errMesg );
		goto free_and_return;
	}

	if ( ((conn->c_flags & CONN_FLAG_SSL) != CONN_FLAG_SSL) &&
	      (sasl_ssf <= 1) && (local_ssf <= 1)) {
		errMesg = "Operation requires a secure connection.\n";
		rc = LDAP_CONFIDENTIALITY_REQUIRED;
		goto free_and_return;
	}
#endif

	/* Get the ber value of the extended operation */
	slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &extop_value);

	if (extop_value->bv_val == NULL)
	{
		/* The request field wasn't provided.  We'll
		 * now try to determine the userid and verify
		 * knowledge of the old password via other
		 * means.
		 */
		goto parse_req_done;
	}
	
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
	 *
	 * The request value field is optional. If it is
	 * provided, at least one field must be filled in.
	 */

	/* ber parse code */
	if ( ber_scanf( ber, "{") == LBER_ERROR )
    	{
		/* The request field wasn't provided.  We'll
		 * now try to determine the userid and verify
		 * knowledge of the old password via other
		 * means.
		 */
		goto parse_req_done;
    	} else {
		tag = ber_peek_tag( ber, &len);
	}

	
	/* identify userID field by tags */
	if (tag == LDAP_EXTOP_PASSMOD_TAG_USERID )
	{
		int rc = 0;
		if ( ber_scanf( ber, "a", &rawdn) == LBER_ERROR ) {
			slapi_ch_free_string(&rawdn);
			LDAPDebug( LDAP_DEBUG_ANY, "ber_scanf failed :{\n", 0, 0, 0 );
			errMesg = "ber_scanf failed at userID parse.\n";
			rc = LDAP_PROTOCOL_ERROR;
			goto free_and_return;
		}

		/* Check if we should be performing strict validation. */
		if (config_get_dn_validate_strict()) {
			/* check that the dn is formatted correctly */
			rc = slapi_dn_syntax_check(pb, rawdn, 1);
			if (rc) { /* syntax check failed */
				op_shared_log_error_access(pb, "EXT", rawdn?rawdn:"",
								"strict: invalid target dn");
				errMesg = "invalid target dn.\n";
				slapi_ch_free_string(&rawdn);
				rc = LDAP_INVALID_SYNTAX;
				goto free_and_return;
			}
		}
		tag = ber_peek_tag(ber, &len);
	} 
	
	/* identify oldPasswd field by tags */
	if (tag == LDAP_EXTOP_PASSMOD_TAG_OLDPWD ) {
		if ( ber_scanf( ber, "a", &oldPasswd ) == LBER_ERROR ) {
			slapi_ch_free_string(&oldPasswd);
			LDAPDebug( LDAP_DEBUG_ANY, "ber_scanf failed :{\n", 0, 0, 0 );
			errMesg = "ber_scanf failed at oldPasswd parse.\n";
			rc = LDAP_PROTOCOL_ERROR;
			goto free_and_return;
		}
		tag = ber_peek_tag( ber, &len);
	}
	
	/* identify newPasswd field by tags */
	if (tag ==  LDAP_EXTOP_PASSMOD_TAG_NEWPWD )
	{
		if ( ber_scanf( ber, "a", &newPasswd ) == LBER_ERROR ) {
			slapi_ch_free_string(&newPasswd);
			LDAPDebug( LDAP_DEBUG_ANY, "ber_scanf failed :{\n", 0, 0, 0 );
			errMesg = "ber_scanf failed at newPasswd parse.\n";
			rc = LDAP_PROTOCOL_ERROR;
			goto free_and_return;
		}
	}

parse_req_done:	
	/* Uncomment for debugging, otherwise we don't want to leak the password values into the log... */
	/* LDAPDebug( LDAP_DEBUG_ARGS, "passwd: dn (%s), oldPasswd (%s) ,newPasswd (%s)\n",
					 dn, oldPasswd, newPasswd); */

	/* Get Bind DN */
	slapi_pblock_get( pb, SLAPI_CONN_DN, &bindDN );

	/* If the connection is bound anonymously, we must refuse to process this operation. */
	if (bindDN == NULL || *bindDN == '\0') {
		/* Refuse the operation because they're bound anonymously */
		errMesg = "Anonymous Binds are not allowed.\n";
		rc = LDAP_INSUFFICIENT_ACCESS;
		goto free_and_return;
	}

	/* Find and set the target DN. */
	if (rawdn && *rawdn != '\0') {
	    target_sdn = slapi_sdn_new_dn_passin(rawdn);
	} else { /* We already checked (bindDN && *bindDN != '\0') above  */
	    target_sdn = bindSDN = slapi_sdn_new_normdn_byref(bindDN);
	}
	dn = slapi_sdn_get_ndn(target_sdn);
	if (dn == NULL || *dn == '\0') {
		/* Refuse the operation because they're bound anonymously */
		errMesg = "Invalid dn.\n";
		rc = LDAP_INVALID_DN_SYNTAX;
		goto free_and_return;
	}
	slapi_pblock_set(pb, SLAPI_TARGET_SDN, target_sdn);

	/* Check if we need to send any referrals. */
	if (slapi_dn_write_needs_referral(target_sdn, &referrals)) {
		rc = LDAP_REFERRAL;
		goto free_and_return;
	}

	if (oldPasswd == NULL || *oldPasswd == '\0') {
		/* If user is authenticated, they already gave their password during
		 * the bind operation (or used sasl or client cert auth or OS creds) */
		slapi_pblock_get(pb, SLAPI_CONN_AUTHMETHOD, &authmethod);
		if (!authmethod || !strcmp(authmethod, SLAPD_AUTH_NONE)) {
			errMesg = "User must be authenticated to the directory server.\n";
			rc = LDAP_INSUFFICIENT_ACCESS;
			goto free_and_return;
		}
	}

	/* Fetch the password policy.  We need this in case we need to
	 * generate a password as well as for some policy checks. */
	pwpolicy = new_passwdPolicy( pb, dn );
	 
	/* A new password was not supplied in the request, so we need to generate
	 * a random one and return it to the user in a response.
	 */
	if (newPasswd == NULL || *newPasswd == '\0') {
		int rval;
		/* Do a free of newPasswd here to be safe, otherwise we may leak 1 byte */
		slapi_ch_free_string( &newPasswd );

		/* Generate a new password */
		rval = passwd_modify_generate_passwd( pwpolicy, &newPasswd, &errMesg );

		if (rval != LDAP_SUCCESS) {
			if (!errMesg)
				errMesg = "Error generating new password.\n";
			rc = LDAP_OPERATIONS_ERROR;
			goto free_and_return;
		}

		/* Make sure a passwd was actually generated */
		if (newPasswd == NULL || *newPasswd == '\0') {
			errMesg = "Error generating new password.\n";
			rc = LDAP_OPERATIONS_ERROR;
			goto free_and_return;
		}

		/*
		 * Create the response message since we generated a new password.
		 *
		 * PasswdModifyResponseValue ::= SEQUENCE {
		 *     genPasswd       [0]     OCTET STRING OPTIONAL }
		 */
		if ( (response_ber = ber_alloc()) == NULL ) {
                        rc = LDAP_NO_MEMORY;
			goto free_and_return;
                }

		if ( LBER_ERROR == ( ber_printf( response_ber, "{ts}",
				LDAP_EXTOP_PASSMOD_TAG_GENPWD, newPasswd ) ) ) {
			ber_free( response_ber, 1 );
			rc = LDAP_ENCODING_ERROR;
			goto free_and_return;
                }

		ber_flatten(response_ber, &gen_passwd);

		/* We're done with response_ber now, so free it */
		ber_free( response_ber, 1 );
	 }
	 
	 slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, (void *)dn ); 

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

	/* Check if the pwpolicy control is present */
	slapi_pblock_get( pb, SLAPI_PWPOLICY, &need_pwpolicy_ctrl );

	ret = slapi_access_allowed ( pb, targetEntry, SLAPI_USERPWD_ATTR, NULL, SLAPI_ACL_WRITE );
	if ( ret != LDAP_SUCCESS ) {
		if (need_pwpolicy_ctrl) {
			slapi_pwpolicy_make_response_control ( pb, -1, -1, LDAP_PWPOLICY_PWDMODNOTALLOWED );
		}
		errMesg = "Insufficient access rights\n";
		rc = LDAP_INSUFFICIENT_ACCESS;
		goto free_and_return;	
	}
	 	 	 
	/* Now we have the entry which we want to modify
 	 * They gave us a password (old), check it against the target entry
	 * Is the old password valid ?
	 */
	if (oldPasswd && *oldPasswd) {
		ret = passwd_check_pwd(targetEntry, oldPasswd);
		if (ret) {
			/* No, then we fail this operation */
			errMesg = "Invalid oldPasswd value.\n";
			rc = ret;
			goto free_and_return;
		}
	}

	/* Check if password policy allows users to change their passwords.  We need to do
	 * this here since the normal modify code doesn't perform this check for
	 * internal operations. */
	if (!pb->pb_op->o_isroot && !pb->pb_conn->c_needpw && !pwpolicy->pw_change) {
		if (NULL == bindSDN) {
			bindSDN = slapi_sdn_new_normdn_byref(bindDN);
		}
		/* Is this a user modifying their own password? */
		if (slapi_sdn_compare(bindSDN, slapi_entry_get_sdn(targetEntry))==0) {
			if (need_pwpolicy_ctrl) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1, LDAP_PWPOLICY_PWDMODNOTALLOWED );
			}
			errMesg = "User is not allowed to change password\n";
			rc = LDAP_UNWILLING_TO_PERFORM;
			goto free_and_return;
		}
	}

	/* Fetch any present request controls so we can use them when
	 * performing the modify operation. */
	slapi_pblock_get(pb, SLAPI_REQCONTROLS, &req_controls);
	
	/* Now we're ready to make actual password change */
	ret = passwd_modify_userpassword(pb, targetEntry, newPasswd, req_controls, &resp_controls);

	/* Set the response controls if necessary.  We want to do this now
	 * so it is set for both the success and failure cases.  The pblock
	 * will now own the controls. */
	if (resp_controls) {
		slapi_pblock_set(pb, SLAPI_RESCONTROLS, resp_controls);
	}

	if (ret != LDAP_SUCCESS) {
		/* Failed to modify the password, e.g. because password policy, etc. */
		errMesg = "Failed to update password\n";
		rc = ret;
		goto free_and_return;
	}

	if (gen_passwd && (gen_passwd->bv_val != '\0')) {
		/* Set the reponse to let the user know the generated password */
		slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, gen_passwd);
	}
	
	LDAPDebug( LDAP_DEBUG_TRACE, "<= passwd_modify_extop: %d\n", rc, 0, 0 );
	
	/* Free anything that we allocated above */
free_and_return:
	slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_extop",
		errMesg ? errMesg : "success" );

	if ((rc == LDAP_REFERRAL) && (referrals)) {
		send_referrals_from_entry(pb, referrals);
	} else {
		send_ldap_result( pb, rc, NULL, errMesg, 0, NULL );
	}

	slapi_ch_free_string(&oldPasswd);
	slapi_ch_free_string(&newPasswd);
	/* Either this is the same pointer that we allocated and set above,
	 * or whoever used it should have freed it and allocated a new
	 * value that we need to free here */
	slapi_pblock_get( pb, SLAPI_ORIGINAL_TARGET, &otdn );
	if (otdn != dn) {
		slapi_ch_free_string(&otdn);
	}
	slapi_pblock_get(pb, SLAPI_TARGET_SDN, &target_sdn);
	if (bindSDN != target_sdn) {
		slapi_sdn_free(&bindSDN);
	}
	/* slapi_pblock_get SLAPI_CONN_DN does strdup */
	slapi_ch_free_string(&bindDN);
	slapi_sdn_free(&target_sdn);
	slapi_pblock_set(pb, SLAPI_TARGET_SDN, NULL);
	slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET, NULL );
	slapi_ch_free_string(&authmethod);
	delete_passwdPolicy(&pwpolicy);
	slapi_entry_free(referrals);

	if ( targetEntry != NULL ){
		slapi_entry_free (targetEntry); 
	}
	
	if ( ber != NULL ){
		ber_free(ber, 1);
		ber = NULL;
	}
	
	/* We can free the generated password bval now */
	ber_bvfree(gen_passwd);

	return( SLAPI_PLUGIN_EXTENDED_SENT_RESULT );

}/* passwd_modify_extop */


static char *passwd_oid_list[] = {
	EXTOP_PASSWD_OID,
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

	if ( argv == NULL || strcmp( argv[0], EXTOP_PASSWD_OID ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "passwd_modify_init", 
				 "OID is missing or is not %s\n", EXTOP_PASSWD_OID );
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

