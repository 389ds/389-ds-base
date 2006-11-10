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

/* pw_mgmt.c
*/

#include <time.h>
#include <string.h>
#include "slap.h"

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/

/* need_new_pw() is called when non rootdn bind operation succeeds with authentication */ 
int
need_new_pw( Slapi_PBlock *pb, long *t, Slapi_Entry *e, int pwresponse_req )
{
	time_t 		cur_time, pw_exp_date;
 	LDAPMod 	*mod;
	Slapi_Mods smods;
	double		diff_t = 0;
	char 		*cur_time_str = NULL;
	char *passwordExpirationTime;
	char *timestring;
	char *dn;
	passwdPolicy *pwpolicy = NULL;
	int	pwdGraceUserTime = 0;
	char graceUserTime[8];

	slapi_mods_init (&smods, 0);
	dn = slapi_entry_get_ndn( e );
	pwpolicy = new_passwdPolicy(pb, dn);

	/* after the user binds with authentication, clear the retry count */
	if ( pwpolicy->pw_lockout == 1)
	{
		if(slapi_entry_attr_get_int( e, "passwordRetryCount") > 0)
		{
			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordRetryCount", "0");
		}
	}

	cur_time = current_time();

	/* get passwordExpirationTime attribute */
	passwordExpirationTime= slapi_entry_attr_get_charptr(e, "passwordExpirationTime");

	if (passwordExpirationTime == NULL)
	{
		/* password expiration date is not set.
		 * This is ok for data that has been loaded via ldif2ldbm
		 * Set expiration time if needed,
		 * don't do further checking and return 0 */
		if ( pwpolicy->pw_exp == 1) {
			pw_exp_date = time_plus_sec ( cur_time, 
				pwpolicy->pw_maxage );

			timestring = format_genTime (pw_exp_date);			
			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
			slapi_ch_free((void **)&timestring);
			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "0");
			
			pw_apply_mods(dn, &smods);
		}
		slapi_mods_done(&smods);
		delete_passwdPolicy(&pwpolicy);
		return ( 0 );
	}

	pw_exp_date = parse_genTime(passwordExpirationTime);

        slapi_ch_free((void**)&passwordExpirationTime);

	/* Check if password has been reset */
	if ( pw_exp_date == NO_TIME ) {

		/* check if changing password is required */  
		if ( pwpolicy->pw_must_change ) {
			/* set c_needpw for this connection to be true.  this client 
			   now can only change its own password */
			pb->pb_conn->c_needpw = 1;
			*t=0;
			/* We need to include "changeafterreset" error in
			 * passwordpolicy response control. So, we will not be
			 * done here. We remember this scenario by (c_needpw=1)
			 * and check it before sending the control from various
			 * places. We will also add LDAP_CONTROL_PWEXPIRED control
			 * as the return value used to be (1).
			 */
			goto skip;
		}
		/* Mark that first login occured */
		pw_exp_date = NOT_FIRST_TIME;
		timestring = format_genTime(pw_exp_date);
		slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
		slapi_ch_free((void **)&timestring);
	}

skip:
	/* if password never expires, don't need to go on; return 0 */
	if ( pwpolicy->pw_exp == 0 ) {
		/* check for "changeafterreset" condition */
		if (pb->pb_conn->c_needpw == 1) {
			if (pwresponse_req) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1, LDAP_PWPOLICY_CHGAFTERRESET );
			} 
			slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
		}
		pw_apply_mods(dn, &smods);
		slapi_mods_done(&smods);
		delete_passwdPolicy(&pwpolicy);
		return ( 0 );
	}

	/* check if password expired.  If so, abort bind. */
	cur_time_str = format_genTime ( cur_time );
	if ( pw_exp_date != NO_TIME  && 
		 pw_exp_date != NOT_FIRST_TIME && 
		 (diff_t = difftime ( pw_exp_date, 
			parse_genTime ( cur_time_str ))) <= 0 ) {
	
		slapi_ch_free_string(&cur_time_str); /* only need this above */
		/* password has expired. Check the value of 
		 * passwordGraceUserTime and compare it
		 * against the value of passwordGraceLimit */
		pwdGraceUserTime = slapi_entry_attr_get_int( e, "passwordGraceUserTime");
		if ( pwpolicy->pw_gracelimit > pwdGraceUserTime ) {
			pwdGraceUserTime++;
			sprintf ( graceUserTime, "%d", pwdGraceUserTime );
			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE,
				"passwordGraceUserTime", graceUserTime);	
			pw_apply_mods(dn, &smods);
			slapi_mods_done(&smods);
			if (pwresponse_req) {
				/* check for "changeafterreset" condition */
				if (pb->pb_conn->c_needpw == 1) {
					slapi_pwpolicy_make_response_control( pb, -1, 
						((pwpolicy->pw_gracelimit) - pwdGraceUserTime),
						LDAP_PWPOLICY_CHGAFTERRESET);
				} else {
					slapi_pwpolicy_make_response_control( pb, -1, 
						((pwpolicy->pw_gracelimit) - pwdGraceUserTime),
						-1);
				}
			}
			
			if (pb->pb_conn->c_needpw == 1) {
				slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
			}
			delete_passwdPolicy(&pwpolicy);
			return ( 0 );
		}

		/* password expired and user exceeded limit of grace attemps.
		 * Send result and also the control */
		slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
		if (pwresponse_req) {
			slapi_pwpolicy_make_response_control ( pb, -1, -1, LDAP_PWPOLICY_PWDEXPIRED );
		}
		slapi_send_ldap_result ( pb, LDAP_INVALID_CREDENTIALS, NULL,
			"password expired!", 0, NULL );
		
		/* abort bind */
		/* pass pb to do_unbind().  pb->pb_op->o_opid and 
		   pb->pb_op->o_tag are not right but I don't see 
		   do_unbind() checking for those.   We might need to 
		   create a pb for unbind operation.  Also do_unbind calls
		   pre and post ops.  Maybe we don't want to call them */
		if (pb->pb_conn && (LDAP_VERSION2 == pb->pb_conn->c_ldapversion)) {
			/* We close the connection only with LDAPv2 connections */
			do_unbind( pb );
		}
		/* Apply current modifications */
		pw_apply_mods(dn, &smods);
		slapi_mods_done(&smods);
		delete_passwdPolicy(&pwpolicy);
		return (-1);
	} 
	slapi_ch_free((void **) &cur_time_str );

	/* check if password is going to expire within "passwordWarning" */
	/* Note that if pw_exp_date is NO_TIME or NOT_FIRST_TIME,
	 * we must send warning first and this changes the expiration time.
	 * This is done just below since diff_t is 0 
  	 */
	if ( diff_t <= pwpolicy->pw_warning ) {
		int pw_exp_warned = 0;
		
		pw_exp_warned= slapi_entry_attr_get_int( e, "passwordExpWarned");
		if ( !pw_exp_warned ){
			/* first time send out a warning */
			/* reset the expiration time to current + warning time 
			 * and set passwordExpWarned to true
			 */
			if (pb->pb_conn->c_needpw != 1) {
				pw_exp_date = time_plus_sec ( cur_time, 
					pwpolicy->pw_warning );
			}
			
			timestring = format_genTime(pw_exp_date);
			/* At this time passwordExpirationTime may already be
			 * in the list of mods: Remove it */
			for (mod = slapi_mods_get_first_mod(&smods); mod != NULL; 
				 mod = slapi_mods_get_next_mod(&smods))
			{
				if (!strcmp(mod->mod_type, "passwordExpirationTime"))
					slapi_mods_remove(&smods);
			}

			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestring);
			slapi_ch_free((void **)&timestring);

			slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "1");
			
			*t = pwpolicy->pw_warning;

		} else {
			*t = (long)diff_t; /* jcm: had to cast double to long */
		}
			
		pw_apply_mods(dn, &smods);
		slapi_mods_done(&smods);
		if (pwresponse_req) {
			/* check for "changeafterreset" condition */
			if (pb->pb_conn->c_needpw == 1) {
					slapi_pwpolicy_make_response_control( pb, *t, -1,
						LDAP_PWPOLICY_CHGAFTERRESET);
				} else {
					slapi_pwpolicy_make_response_control( pb, *t, -1,
						-1);
				}
		}

		if (pb->pb_conn->c_needpw == 1) {
			slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
		}
		delete_passwdPolicy(&pwpolicy);
		return (2);
	}

	pw_apply_mods(dn, &smods);
	slapi_mods_done(&smods);
	/* Leftover from "changeafterreset" condition */
	if (pb->pb_conn->c_needpw == 1) {
		slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
	}
	delete_passwdPolicy(&pwpolicy);
	/* passes checking, return 0 */
	return( 0 );
}

/* check_account_lock is called before bind opeation; this could be a pre-op. */
int
check_account_lock ( Slapi_PBlock *pb, Slapi_Entry * bind_target_entry, int pwresponse_req) {

	time_t		unlock_time;
	time_t		cur_time;
	double		diff_t;
	char		*cur_time_str = NULL;
	char *accountUnlockTime;
	passwdPolicy *pwpolicy = NULL;
	char *dn = NULL;

	/* kexcoff: account inactivation */
	int rc = 0;
	Slapi_ValueSet *values = NULL;
	int type_name_disposition = 0;
	char *actual_type_name = NULL;
	int attr_free_flags = 0;
	/* kexcoff - end */

	if ( bind_target_entry == NULL ) 
		return -1;

	dn = slapi_entry_get_ndn(bind_target_entry);
	pwpolicy = new_passwdPolicy(pb, dn);

	/* kexcoff: account inactivation */
	/* check if the entry is locked by nsAccountLock attribute - account inactivation feature */

	rc = slapi_vattr_values_get(bind_target_entry, "nsAccountLock", 
								&values, 
								&type_name_disposition, &actual_type_name,
								SLAPI_VIRTUALATTRS_REQUEST_POINTERS,
								&attr_free_flags);
	if ( rc == 0 )
	{
		Slapi_Value *v = NULL;	
		const struct berval *bvp = NULL;

		if ( (slapi_valueset_first_value( values, &v ) != -1) &&
				( bvp = slapi_value_get_berval( v )) != NULL )
		{
			if ( (bvp != NULL) && (strcasecmp(bvp->bv_val, "true") == 0) )
			{
				/* account inactivated */
				if (pwresponse_req) {
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
							LDAP_PWPOLICY_ACCTLOCKED );
				}
				send_ldap_result ( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							"Account inactivated. Contact system administrator.",
							0, NULL );
				slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
				goto locked;
			}
		} /* else, account "activated", keep on the process */

		if ( values != NULL )
			slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
	}
	/* kexcoff - end */

	/*
	 * Check if the password policy has to be checked or not
	 */
	if ( pwpolicy->pw_lockout == 0 ) {
		goto notlocked;
	}

	/*
	 * Check the attribute of the password policy
	 */

	/* check if account is locked out.  If so, send result and return 1 */
	{
		unsigned int maxfailure= pwpolicy->pw_maxfailure;
		/* It's locked if passwordRetryCount >= maxfailure */
		if ( slapi_entry_attr_get_uint(bind_target_entry,"passwordRetryCount") < maxfailure )
		{
			/* Not locked */
			goto notlocked;	
		}
	}

	/* locked but maybe it's time to unlock it */
	accountUnlockTime= slapi_entry_attr_get_charptr(bind_target_entry, "accountUnlockTime");
	if (accountUnlockTime != NULL)
	{
		unlock_time = parse_genTime(accountUnlockTime);
		slapi_ch_free((void **) &accountUnlockTime );

		if ( pwpolicy->pw_unlock == 0 && 
			unlock_time == NO_TIME ) {

	        /* account is locked forever. contact admin to reset */
			if (pwresponse_req) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_ACCTLOCKED );
			}
	        send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL,
	                "Exceed password retry limit. Contact system administrator to reset."
	                , 0, NULL );
			goto locked;
		}
		cur_time = current_time();
		cur_time_str = format_genTime( cur_time);
		if ( ( diff_t = difftime ( parse_genTime( cur_time_str ), 
			unlock_time ) ) < 0 ) {

			/* account is locked, cannot do anything */	
			if (pwresponse_req) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_ACCTLOCKED );
			}
			send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL,
				"Exceed password retry limit. Please try later."				, 0, NULL );
			slapi_ch_free((void **) &cur_time_str );
			goto locked;
		} 
		slapi_ch_free((void **) &cur_time_str );
	}

notlocked:
	/* account is not locked. */ 
	delete_passwdPolicy(&pwpolicy);
	return ( 0 );	
locked:
	delete_passwdPolicy(&pwpolicy);
	return (1);

}

void
pw_init ( void ) {
	slapdFrontendConfig_t *slapdFrontendConfig;

	pw_set_componentID(generate_componentid(NULL, COMPONENT_PWPOLICY));
	
	slapdFrontendConfig = getFrontendConfig();
	pw_mod_allowchange_aci (!slapdFrontendConfig->pw_policy.pw_change && 
                            !slapdFrontendConfig->pw_policy.pw_must_change);
}


