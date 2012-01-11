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

/* pw_retry.c
*/

#include <time.h>
#include "slap.h"

/****************************************************************************/
/* prototypes                                                               */
/****************************************************************************/
/* Slapi_Entry *get_entry ( Slapi_PBlock *pb, const char *dn ); */
static void set_retry_cnt ( Slapi_PBlock *pb, int count);
static void set_retry_cnt_and_time ( Slapi_PBlock *pb, int count, time_t cur_time);

/*
 * update_pw_retry() is called when bind operation fails 
 * with LDAP_INVALID_CREDENTIALS (in backend bind.c ). 
 * It checks to see if the retry count can be reset,
 * increments retry count, and then check if need to lock the acount. 
 * To have a global password policy, these mods should be chained to the
 * master, and not applied locally. If they are applied locally, they should
 * not get replicated from master... 
 */

int update_pw_retry ( Slapi_PBlock *pb )
{
    Slapi_Entry           *e;
	int             retry_cnt=0; 
	time_t          reset_time; 
	time_t          cur_time;
	char            *cur_time_str = NULL;
	char *retryCountResetTime;
	int passwordRetryCount;

    /* get the entry */
    e = get_entry ( pb, NULL );
	if ( e == NULL ) {
		return ( 1 );
	}

    cur_time = current_time();

    /* check if the retry count can be reset. */
	retryCountResetTime= slapi_entry_attr_get_charptr(e, "retryCountResetTime");
	if(retryCountResetTime!=NULL)
	{
        reset_time = parse_genTime (retryCountResetTime);
		slapi_ch_free((void **) &retryCountResetTime );

		cur_time_str = format_genTime ( cur_time );
        if ( difftime ( parse_genTime( cur_time_str ), reset_time) >= 0 )
        {
            /* set passwordRetryCount to 1 */
            /* reset retryCountResetTime */
			set_retry_cnt_and_time ( pb, 1, cur_time );
			slapi_ch_free((void **) &cur_time_str );
			slapi_entry_free( e );
            return ( 0 ); /* success */
        } else {
			slapi_ch_free((void **) &cur_time_str );
		}
    } else {
		/* initialize passwordRetryCount and retryCountResetTime */
		set_retry_cnt_and_time ( pb, 1, cur_time );
		slapi_entry_free( e );
        return ( 0 ); /* success */
	}
	passwordRetryCount = slapi_entry_attr_get_int(e, "passwordRetryCount"); 
    if (passwordRetryCount >= 0)
	{
        retry_cnt = passwordRetryCount + 1;
   		if ( retry_cnt == 1 ) {
        	/* set retryCountResetTime */
        	set_retry_cnt_and_time ( pb, retry_cnt, cur_time );
		} else {
			/* set passwordRetryCount to retry_cnt */
			set_retry_cnt ( pb, retry_cnt );
		}
    }	
	slapi_entry_free( e );
	return 0; /* success */
}

static
void set_retry_cnt_and_time ( Slapi_PBlock *pb, int count, time_t cur_time ) {
	const char  *dn = NULL;
	Slapi_DN    *sdn = NULL;
	Slapi_Mods	smods;
	time_t      reset_time;
	char		*timestr;
	passwdPolicy *pwpolicy = NULL;
	void *txn = NULL;

	slapi_pblock_get( pb, SLAPI_TXN, &txn );
	slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
	dn = slapi_sdn_get_dn(sdn);
	pwpolicy = new_passwdPolicy(pb, dn);

	slapi_mods_init(&smods, 0);

	reset_time = time_plus_sec ( cur_time, 
		pwpolicy->pw_resetfailurecount );
	
	timestr = format_genTime ( reset_time );
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "retryCountResetTime", timestr);
	slapi_ch_free((void **)&timestr);

	set_retry_cnt_mods(pb, &smods, count);
	
	pw_apply_mods_ext(sdn, &smods, txn);
	slapi_mods_done(&smods);
	delete_passwdPolicy(&pwpolicy);
}

void set_retry_cnt_mods(Slapi_PBlock *pb, Slapi_Mods *smods, int count)
{
	char 		*timestr;
	time_t		unlock_time;
	char        retry_cnt[8]; /* 1-65535 */
	const char *dn = NULL; 
	Slapi_DN *sdn = NULL; 
	passwdPolicy *pwpolicy = NULL;

	slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
	dn = slapi_sdn_get_dn(sdn);
	pwpolicy = new_passwdPolicy(pb, dn);

	if (smods) {
		sprintf ( retry_cnt, "%d", count );
		slapi_mods_add_string(smods, LDAP_MOD_REPLACE, "passwordRetryCount", retry_cnt);
		/* lock account if reache retry limit */
		if ( count >=  pwpolicy->pw_maxfailure ) {
			/* Remove lock_account function to perform all mods at once */
			/* lock_account ( pb ); */
			/* reach the retry limit, lock the account  */
			if ( pwpolicy->pw_unlock == 0 ) {
				/* lock until admin reset password */
				unlock_time = NO_TIME;
			} else {
				unlock_time = time_plus_sec ( current_time(),
											  pwpolicy->pw_lockduration );
			}
			timestr= format_genTime ( unlock_time );
			slapi_mods_add_string(smods, LDAP_MOD_REPLACE, "accountUnlockTime", timestr);
			slapi_ch_free((void **)&timestr);
		}
	}
	delete_passwdPolicy(&pwpolicy);
	return;
}

static
void set_retry_cnt ( Slapi_PBlock *pb, int count)
{
	Slapi_DN *sdn = NULL; 
	Slapi_Mods	smods;
	void *txn = NULL;

	slapi_pblock_get( pb, SLAPI_TXN, &txn );
	slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
	slapi_mods_init(&smods, 0);
	set_retry_cnt_mods(pb, &smods, count);
	pw_apply_mods_ext(sdn, &smods, txn);
	slapi_mods_done(&smods);
}


Slapi_Entry *get_entry ( Slapi_PBlock *pb, const char *dn)
{
	int             search_result = 0;
	Slapi_Entry     *retentry = NULL;
	Slapi_DN        *target_sdn = NULL;
	Slapi_DN        sdn;
	void            *txn = NULL;

	if (NULL == pb) {
		LDAPDebug(LDAP_DEBUG_ANY, "get_entry - no pblock specified.\n",
		          0, 0, 0);
		goto bail;
	}

	slapi_pblock_get( pb, SLAPI_TARGET_SDN, &target_sdn );
	slapi_pblock_get( pb, SLAPI_TXN, &txn );

	if (dn == NULL) {
		dn = slapi_sdn_get_dn(target_sdn);
	}

	if (dn == NULL) {
		LDAPDebug (LDAP_DEBUG_TRACE, "WARNING: 'get_entry' - no dn specified.\n", 0, 0, 0);
		goto bail;
	}

	slapi_sdn_init_dn_byref(&sdn, dn);

	if (slapi_sdn_compare(&sdn, target_sdn)) { /* does not match */
	    target_sdn = &sdn;
	}

	search_result = slapi_search_internal_get_entry_ext(target_sdn, NULL,
														&retentry,
														pw_get_componentID(), txn);
	if (search_result != LDAP_SUCCESS) {
		LDAPDebug (LDAP_DEBUG_TRACE, "WARNING: 'get_entry' can't find entry '%s', err %d\n", dn, search_result, 0);
	}
	slapi_sdn_done(&sdn);
bail:
	return retentry;
}

void
pw_apply_mods_ext(const Slapi_DN *sdn, Slapi_Mods *mods, void *txn) 
{
	Slapi_PBlock pb;
	int res;
	
	if (mods && (slapi_mods_get_num_mods(mods) > 0)) 
	{
		pblock_init(&pb);
		/* We don't want to overwrite the modifiersname, etc. attributes,
		 * so we set a flag for this operation */
		slapi_modify_internal_set_pb_ext (&pb, sdn, 
					  slapi_mods_get_ldapmods_byref(mods),
					  NULL, /* Controls */
					  NULL, /* UniqueID */
					  pw_get_componentID(), /* PluginID */
					  OP_FLAG_SKIP_MODIFIED_ATTRS); /* Flags */
		slapi_pblock_set(&pb, SLAPI_TXN, txn);
		slapi_modify_internal_pb (&pb);
		
		slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
		if (res != LDAP_SUCCESS){
			LDAPDebug2Args(LDAP_DEBUG_ANY,
			        "WARNING: passwordPolicy modify error %d on entry '%s'\n",
					res, slapi_sdn_get_dn(sdn));
		}
		
		pblock_done(&pb);
	}
	
	return;
}

void
pw_apply_mods(const Slapi_DN *sdn, Slapi_Mods *mods)
{
	pw_apply_mods_ext(sdn, mods, NULL);
}
/* Handle the component ID for the password policy */

static struct slapi_componentid * pw_componentid = NULL;

void pw_set_componentID(struct slapi_componentid *cid)
{
	pw_componentid = cid;
}

struct slapi_componentid * pw_get_componentID()
{
	return pw_componentid;
}
