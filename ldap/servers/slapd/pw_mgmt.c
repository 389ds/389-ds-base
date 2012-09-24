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
	const Slapi_DN *sdn;
	passwdPolicy *pwpolicy = NULL;
	int	pwdGraceUserTime = 0;
	char graceUserTime[8];

	slapi_mods_init (&smods, 0);
	sdn = slapi_entry_get_sdn_const( e );
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
			
			pw_apply_mods(sdn, &smods);
		} else if (pwpolicy->pw_lockout == 1) {
			pw_apply_mods(sdn, &smods);
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
		pw_apply_mods(sdn, &smods);
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
			pw_apply_mods(sdn, &smods);
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
			disconnect_server( pb->pb_conn, pb->pb_op->o_connid,
				pb->pb_op->o_opid, SLAPD_DISCONNECT_UNBIND, 0);
		}
		/* Apply current modifications */
		pw_apply_mods(sdn, &smods);
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
			
		pw_apply_mods(sdn, &smods);
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

	pw_apply_mods(sdn, &smods);
	slapi_mods_done(&smods);
	/* Leftover from "changeafterreset" condition */
	if (pb->pb_conn->c_needpw == 1) {
		slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
	}
	delete_passwdPolicy(&pwpolicy);
	/* passes checking, return 0 */
	return( 0 );
}

void
pw_init ( void ) {
	slapdFrontendConfig_t *slapdFrontendConfig;

	pw_set_componentID(generate_componentid(NULL, COMPONENT_PWPOLICY));
	
	slapdFrontendConfig = getFrontendConfig();
	pw_mod_allowchange_aci (!slapdFrontendConfig->pw_policy.pw_change && 
                            !slapdFrontendConfig->pw_policy.pw_must_change);

	slapi_add_internal_attr_syntax( PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
	                                PSEUDO_ATTR_UNHASHEDUSERPASSWORD_OID,
	                                OCTETSTRING_SYNTAX_OID, 0, 
	                                /* Clients don't need to directly modify
	                                 * PSEUDO_ATTR_UNHASHEDUSERPASSWORD */
	                                SLAPI_ATTR_FLAG_NOUSERMOD);
}


