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

#include "repl.h"
#include "repl5.h"
#include "slap.h"
#include "slapi-plugin.h"
#include "winsync-plugin.h"
#include "windowsrepl.h"

struct windowsprivate {
  
  Slapi_DN *windows_subtree; /* DN of synchronized subtree  (on the windows side) */
  Slapi_DN *directory_subtree; /* DN of synchronized subtree on directory side */
                                /* this simplifies the mapping as it's simply
								   from the former to the latter container, or
								   vice versa */
  ber_int_t dirsync_flags;		
  ber_int_t dirsync_maxattributecount;
  char *dirsync_cookie; 
  int dirsync_cookie_len;
  PRBool dirsync_cookie_has_more;
  PRBool create_users_from_dirsync;
  PRBool create_groups_from_dirsync;
  char *windows_domain;
  int isnt4;
  int iswin2k3;
  /* This filter is used to determine if an entry belongs to this agreement.  We put it here
   * so we only have to allocate each filter once instead of doing it every time we receive a change. */
  Slapi_Filter *directory_filter; /* Used for checking if local entries need to be sync'd to AD */
  Slapi_Filter *deleted_filter; /* Used for checking if an entry is an AD tombstone */
  Slapi_Entry *raw_entry; /* "raw" un-schema processed last entry read from AD */
  int keep_raw_entry; /* flag to control when the raw entry is set */
  void *api_cookie; /* private data used by api callbacks */
  time_t sync_interval; /* how often to run the dirsync search, in seconds */
  int one_way; /* Indicates if this is a one-way agreement and which direction it is */
  int move_action; /* Indicates what to do with DS entry if AD entry is moved out of scope */
};

static void windows_private_set_windows_domain(const Repl_Agmt *ra, char *domain);

static int
true_value_from_string(char *val)
{
	if (strcasecmp (val, "on") == 0 || strcasecmp (val, "yes") == 0 ||
		strcasecmp (val, "true") == 0 || strcasecmp (val, "1") == 0)
	{
		return 1;
	} else 
	{
		return 0;
	}
}

/* yech - can't declare a constant string array because type_nsds7XX variables
   are not constant strings - so have to build a lookup table */
static int
get_next_disallow_attr_type(int *ii, const char **type)
{
	switch (*ii) {
	case 0: *type = type_nsds7WindowsReplicaArea; break;
	case 1: *type = type_nsds7DirectoryReplicaArea; break;
	case 2: *type = type_nsds7WindowsDomain; break;
	default: *type = NULL; break;
	}

	if (*type) {
		(*ii)++;
		return 1;
	}
	return 0;
}

static int
check_update_allowed(Repl_Agmt *ra, const char *type, Slapi_Entry *e, int *retval)
{
	int rc = 1;

	/* note - it is not an error to defer setting the value in the ra */
	*retval = 1;
	if (agmt_get_update_in_progress(ra)) {
		const char *distype = NULL;
		int ii = 0;
		while (get_next_disallow_attr_type(&ii, &distype)) {
			if (slapi_attr_types_equivalent(type, distype)) {
				char *tmpstr = slapi_entry_attr_get_charptr(e, type);
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
								"windows_parse_config_entry: setting %s to %s will be "
								"deferred until current update is completed\n",
								type, tmpstr);
				slapi_ch_free_string(&tmpstr);
				rc = 0;
				break;
			}
		}
	}

	return rc;
}

static int
windows_parse_config_entry(Repl_Agmt *ra, const char *type, Slapi_Entry *e)
{
	char *tmpstr = NULL;
	int retval = 0;

	if (!check_update_allowed(ra, type, e, &retval))
	{
		return retval;
	}
	
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7WindowsReplicaArea))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7WindowsReplicaArea);
		if (NULL != tmpstr)
		{
			windows_private_set_windows_subtree(ra, slapi_sdn_new_dn_passin(tmpstr) );
		}
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7DirectoryReplicaArea))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7DirectoryReplicaArea); 
		if (NULL != tmpstr)
		{
			windows_private_set_directory_subtree(ra, slapi_sdn_new_dn_passin(tmpstr) );
		}
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7CreateNewUsers))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7CreateNewUsers); 
		if (NULL != tmpstr && true_value_from_string(tmpstr))
		{
			windows_private_set_create_users(ra, PR_TRUE);
		}
		else
		{
			windows_private_set_create_users(ra, PR_FALSE);
		}
		slapi_ch_free((void**)&tmpstr);
		prot_notify_agmt_changed(agmt_get_protocol(ra), (char *)agmt_get_long_name(ra));
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7CreateNewGroups))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7CreateNewGroups); 
		if (NULL != tmpstr && true_value_from_string(tmpstr))
		{
			windows_private_set_create_groups(ra, PR_TRUE);
		}
		else
		{
			windows_private_set_create_groups(ra, PR_FALSE);
		}
		slapi_ch_free((void**)&tmpstr);
		prot_notify_agmt_changed(agmt_get_protocol(ra), (char *)agmt_get_long_name(ra));
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7WindowsDomain))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7WindowsDomain); 
		if (NULL != tmpstr)
		{
			windows_private_set_windows_domain(ra,tmpstr);
		}
		/* No need to free tmpstr because it was aliased by the call above */
		tmpstr = NULL;
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_winSyncInterval))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_winSyncInterval); 
		if (NULL != tmpstr)
		{
			windows_private_set_sync_interval(ra,tmpstr);
			prot_notify_agmt_changed(agmt_get_protocol(ra), (char *)agmt_get_long_name(ra));
		}
		slapi_ch_free_string(&tmpstr);
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_oneWaySync))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_oneWaySync);
		if (NULL != tmpstr)
		{
			if (strcasecmp(tmpstr, "fromWindows") == 0) {
				windows_private_set_one_way(ra, ONE_WAY_SYNC_FROM_AD);
			} else if (strcasecmp(tmpstr, "toWindows") == 0) {
				windows_private_set_one_way(ra, ONE_WAY_SYNC_TO_AD);
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Ignoring illegal setting for %s attribute in replication "
					"agreement \"%s\".  Valid values are \"toWindows\" or "
					"\"fromWindows\".\n", type_oneWaySync, slapi_entry_get_dn(e));
				windows_private_set_one_way(ra, ONE_WAY_SYNC_DISABLED);
			}
		}
		else
		{
			windows_private_set_one_way(ra, ONE_WAY_SYNC_DISABLED);
		}
		slapi_ch_free((void**)&tmpstr);
		prot_notify_agmt_changed(agmt_get_protocol(ra), (char *)agmt_get_long_name(ra));
		retval = 1;
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_winsyncMoveAction))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_winsyncMoveAction);
		if (NULL != tmpstr)
		{
			if (strcasecmp(tmpstr, "delete") == 0) {
				windows_private_set_move_action(ra, MOVE_DOES_DELETE);
			} else if (strcasecmp(tmpstr, "unsync") == 0) {
				windows_private_set_move_action(ra, MOVE_DOES_UNSYNC);
			} else if (strcasecmp(tmpstr, "none") == 0) {
				windows_private_set_move_action(ra, MOVE_DOES_NOTHING);
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Ignoring illegal setting for %s attribute in replication "
					"agreement \"%s\".  Valid values are \"delete\" or "
					"\"unsync\".\n", type_winsyncMoveAction, slapi_entry_get_dn(e));
				windows_private_set_move_action(ra, MOVE_DOES_NOTHING);
			}
		}
		else
		{
			windows_private_set_move_action(ra, MOVE_DOES_NOTHING);
		}
		slapi_ch_free((void**)&tmpstr);
		prot_notify_agmt_changed(agmt_get_protocol(ra), (char *)agmt_get_long_name(ra));
		retval = 1;
	}

	return retval;
}

/* Returns non-zero if the modify was ok, zero if not */
int
windows_handle_modify_agreement(Repl_Agmt *ra, const char *type, Slapi_Entry *e)
{
	/* Is this a Windows agreement ? */
	if (get_agmt_agreement_type(ra) == REPLICA_TYPE_WINDOWS)
	{
		return windows_parse_config_entry(ra,type,e);
	} else
	{
		return 0;
	}
}

void 
windows_update_done(Repl_Agmt *agmt, int is_total)
{
	/* "flush" the changes made during the update to the agmt */
	/* get the agmt entry */
	Slapi_DN *agmtdn = slapi_sdn_dup(agmt_get_dn_byref(agmt));
	Slapi_Entry *agmte = NULL;
	int rc = slapi_search_internal_get_entry(agmtdn, NULL, &agmte,
											 repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION));
	if ((rc == 0) && agmte) {
		int ii = 0;
		const char *distype = NULL;
		while (get_next_disallow_attr_type(&ii, &distype)) {
			windows_handle_modify_agreement(agmt, distype, agmte);
		}
	}
	slapi_entry_free(agmte);
	slapi_sdn_free(&agmtdn);
}

void
windows_init_agreement_from_entry(Repl_Agmt *ra, Slapi_Entry *e)
{
	agmt_set_priv(ra,windows_private_new());

	windows_parse_config_entry(ra,NULL,e);

	windows_plugin_init(ra);
}

const char* windows_private_get_purl(const Repl_Agmt *ra)
{
	const char* windows_purl;
	char *hostname;

	hostname = agmt_get_hostname(ra);
	if(slapi_is_ipv6_addr(hostname)){
		/* need to put brackets around the ipv6 address */
		windows_purl = slapi_ch_smprintf("ldap://[%s]:%d", hostname, agmt_get_port(ra));
	} else {
		windows_purl = slapi_ch_smprintf("ldap://%s:%d", hostname, agmt_get_port(ra));
	}
	slapi_ch_free_string(&hostname);

	return windows_purl;
}

Dirsync_Private* windows_private_new()
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_new\n" );

	dp = (Dirsync_Private *)slapi_ch_calloc(sizeof(Dirsync_Private),1);

	dp->dirsync_maxattributecount = -1;
	dp->directory_filter = NULL;
	dp->deleted_filter = NULL;
	dp->sync_interval = PERIODIC_DIRSYNC_INTERVAL;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_new\n" );
	return dp;

}

void windows_agreement_delete(Repl_Agmt *ra)
{

	Dirsync_Private *dp = (Dirsync_Private *) agmt_get_priv(ra);
	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_delete\n" );

	PR_ASSERT(dp  != NULL);
	
	winsync_plugin_call_destroy_agmt_cb(ra, dp->directory_subtree,
										dp->windows_subtree);

	windows_plugin_cleanup_agmt(ra);

	slapi_sdn_free(&dp->directory_subtree);
	slapi_sdn_free(&dp->windows_subtree);
	slapi_filter_free(dp->directory_filter, 1);
	slapi_filter_free(dp->deleted_filter, 1);
	slapi_entry_free(dp->raw_entry);
	slapi_ch_free_string(&dp->windows_domain);
	dp->raw_entry = NULL;
	dp->api_cookie = NULL;
	slapi_ch_free((void **)dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_delete\n" );

}

int windows_private_get_isnt4(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_isnt4\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_isnt4\n" );
	
		return dp->isnt4;	
}

void windows_private_set_isnt4(const Repl_Agmt *ra, int isit)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_isnt4\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		dp->isnt4 = isit;
		
		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_isnt4\n" );
}

int windows_private_get_iswin2k3(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_iswin2k3\n" );

	PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_iswin2k3\n" );

		return dp->iswin2k3;
}

void windows_private_set_iswin2k3(const Repl_Agmt *ra, int isit)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_iswin2k3\n" );

	PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		dp->iswin2k3 = isit;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_iswin2k3\n" );
}

/* Returns a copy of the Slapi_Filter pointer.  The caller should not free it */
Slapi_Filter* windows_private_get_directory_filter(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_directory_filter\n" );

	PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		if (dp->directory_filter == NULL) {
			char *string_filter = slapi_ch_strdup("(&(|(objectclass=ntuser)(objectclass=ntgroup))(ntUserDomainId=*))");
			/* The filter gets freed in windows_agreement_delete() */
                        dp->directory_filter = slapi_str2filter( string_filter );
			slapi_ch_free_string(&string_filter);
		}

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_directory_filter\n" );

		return dp->directory_filter;
}

/* Returns a copy of the Slapi_Filter pointer.  The caller should not free it */
Slapi_Filter* windows_private_get_deleted_filter(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_deleted_filter\n" );

	PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		if (dp->deleted_filter == NULL) {
			char *string_filter = slapi_ch_strdup("(isdeleted=*)");
			/* The filter gets freed in windows_agreement_delete() */
			dp->deleted_filter = slapi_str2filter( string_filter );
			slapi_ch_free_string(&string_filter);
		}

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_deleted_filter\n" );

		return dp->deleted_filter;
}

/* Returns a copy of the Slapi_DN pointer, no need to free it */
const Slapi_DN* windows_private_get_windows_subtree (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_windows_subtree\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_windows_subtree\n" );
	
		return dp->windows_subtree;	
}

const char *
windows_private_get_windows_domain(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_windows_domain\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_windows_domain\n" );
	
		return dp->windows_domain;	
}

static void
windows_private_set_windows_domain(const Repl_Agmt *ra, char *domain)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_windows_domain\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		slapi_ch_free_string(&dp->windows_domain);
		dp->windows_domain = domain;
		
		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_windows_domain\n" );
	}

/* Returns a copy of the Slapi_DN pointer, no need to free it */
const Slapi_DN* windows_private_get_directory_subtree (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_directory_replarea\n" );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_directory_replarea\n" );
	
		return dp->directory_subtree; 
}

/* Takes a copy of the sdn passed in */
void windows_private_set_windows_subtree (const Repl_Agmt *ra,Slapi_DN* sdn )
{

	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_windows_replarea\n" );

	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	slapi_sdn_free(&dp->windows_subtree);
	dp->windows_subtree = sdn;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_windows_replarea\n" );
}

/* Takes a copy of the sdn passed in */
void windows_private_set_directory_subtree (const Repl_Agmt *ra,Slapi_DN* sdn )
{

	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_directory_replarea\n" );

	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	
	slapi_sdn_free(&dp->directory_subtree);
	dp->directory_subtree = sdn;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_directory_replarea\n" );
}

PRBool windows_private_create_users(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_create_users\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_create_users\n" );

	return dp->create_users_from_dirsync;

}


void windows_private_set_create_users(const Repl_Agmt *ra, PRBool value)
{
	Dirsync_Private *dp;
	
	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_create_users\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->create_users_from_dirsync = value;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_create_users\n" );

}

PRBool windows_private_create_groups(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_create_groups\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_create_groups\n" );

	return dp->create_groups_from_dirsync;

}


void windows_private_set_create_groups(const Repl_Agmt *ra, PRBool value)
{
	Dirsync_Private *dp;
	
	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_create_groups\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->create_groups_from_dirsync = value;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_create_groups\n" );

}


int windows_private_get_one_way(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_one_way\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_one_way\n" );

	return dp->one_way;
}


void windows_private_set_one_way(const Repl_Agmt *ra, int value)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_one_way\n" );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->one_way = value;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_one_way\n" );
}


/* 
	This function returns the current Dirsync_Private that's inside 
	Repl_Agmt ra as a ldap control.

  */
LDAPControl* windows_private_dirsync_control(const Repl_Agmt *ra)
{

	LDAPControl *control = NULL;
	BerElement *ber;
	Dirsync_Private *dp;
	char iscritical = PR_TRUE;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_dirsync_control\n" );
	
	PR_ASSERT(ra);
	
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	ber = 	ber_alloc();

	ber_printf( ber, "{iio}", dp->dirsync_flags, dp->dirsync_maxattributecount, dp->dirsync_cookie ? dp->dirsync_cookie : "", dp->dirsync_cookie_len );

	/* Use a regular directory server instead of a real AD - for testing */
	if (getenv("WINSYNC_USE_DS")) {
		iscritical = PR_FALSE;
	}
	slapi_build_control( REPL_DIRSYNC_CONTROL_OID, ber, iscritical, &control);

	ber_free(ber,1);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_dirsync_control\n" );


	return control;

}

/* 
	This function scans the array of controls and updates the Repl_Agmt's 
	Dirsync_Private if the dirsync control is found. 

*/
void windows_private_update_dirsync_control(const Repl_Agmt *ra,LDAPControl **controls )
{

	Dirsync_Private *dp;
    int foundDirsyncControl;
	int i;
	LDAPControl *dirsync = NULL;
	BerElement *ber = NULL;
	ber_int_t hasMoreData;
	ber_int_t maxAttributeCount;
	BerValue  *serverCookie = NULL;
#ifdef FOR_DEBUGGING
	int return_value = LDAP_SUCCESS;
#endif

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_update_dirsync_control\n" );

    PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	
	if (NULL != controls )
	{
		foundDirsyncControl = 0;
		for ( i = 0; (( controls[i] != NULL ) && ( !foundDirsyncControl )); i++ ) {
			foundDirsyncControl = !strcmp( controls[i]->ldctl_oid, REPL_DIRSYNC_CONTROL_OID );
		}

		if ( !foundDirsyncControl )
		{
#ifdef FOR_DEBUGGING
			return_value = LDAP_CONTROL_NOT_FOUND;
#endif
			goto choke;
		}
		else if (!controls[i-1]->ldctl_value.bv_val) {
#ifdef FOR_DEBUGGING
			return_value = LDAP_CONTROL_NOT_FOUND;
#endif
			goto choke;
		}
		else
		{
			dirsync = slapi_dup_control( controls[i-1]);
		}

		if (!dirsync || !BV_HAS_DATA((&(dirsync->ldctl_value)))) {
#ifdef FOR_DEBUGGING
			return_value = LDAP_CONTROL_NOT_FOUND;
#endif
			goto choke;
		}
                
		ber = ber_init( &dirsync->ldctl_value ) ;

		if (ber_scanf( ber, "{iiO}", &hasMoreData, &maxAttributeCount, &serverCookie) == LBER_ERROR)
		{
#ifdef FOR_DEBUGGING
			return_value =  LDAP_CONTROL_NOT_FOUND;
#endif
			goto choke;
		}

		slapi_ch_free_string(&dp->dirsync_cookie);
		dp->dirsync_cookie = ( char* ) slapi_ch_malloc(serverCookie->bv_len + 1);

		memcpy(dp->dirsync_cookie, serverCookie->bv_val, serverCookie->bv_len);
		dp->dirsync_cookie_len = (int) serverCookie->bv_len; /* XXX shouldn't cast? */

		/* dp->dirsync_maxattributecount = maxAttributeCount; We don't need to keep this */
		dp->dirsync_cookie_has_more = hasMoreData;

choke:
		ber_bvfree(serverCookie);
		ber_free(ber,1);
		ldap_control_free(dirsync);
	}
	else
	{
#ifdef FOR_DEBUGGING
		return_value = LDAP_CONTROL_NOT_FOUND;
#endif
	}

#ifdef FOR_DEBUGGING
	LDAPDebug1Arg( LDAP_DEBUG_TRACE, "<= windows_private_update_dirsync_control: rc=%d\n", return_value);
#else
	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_update_dirsync_control\n" );
#endif
}

PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_dirsync_has_more\n" );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_dirsync_has_more\n" );

	return dp->dirsync_cookie_has_more;

}

void windows_private_null_dirsync_cookie(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_null_dirsync_control\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->dirsync_cookie_len = 0;
	slapi_ch_free_string(&dp->dirsync_cookie);
	dp->dirsync_cookie = NULL;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_null_dirsync_control\n" );
}

static 
Slapi_Mods *windows_private_get_cookie_mod(Dirsync_Private *dp, int modtype)
{
	Slapi_Mods *smods = NULL;
	smods = slapi_mods_new();

	slapi_mods_add( smods, modtype,
	 "nsds7DirsyncCookie", dp->dirsync_cookie_len , dp->dirsync_cookie);	

	return smods;

}


/*  writes the current cookie into dse.ldif under the replication agreement entry 
	returns: ldap result code of the operation. */
int 
windows_private_save_dirsync_cookie(const Repl_Agmt *ra)
{
	Dirsync_Private *dp = NULL;
    Slapi_PBlock *pb = NULL;
	Slapi_DN* sdn = NULL;
	int rc = 0;
	Slapi_Mods *mods = NULL;

    
  
	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_save_dirsync_cookie\n" );
	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);


	pb = slapi_pblock_new ();
  
    mods = windows_private_get_cookie_mod(dp, LDAP_MOD_REPLACE);
    sdn = slapi_sdn_dup( agmt_get_dn_byref(ra) );

    slapi_modify_internal_set_pb_ext (pb, sdn, 
            slapi_mods_get_ldapmods_byref(mods), NULL, NULL, 
            repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    if (rc == LDAP_NO_SUCH_ATTRIBUTE)
    {	/* try again, but as an add instead */
		slapi_mods_free(&mods);
		mods = windows_private_get_cookie_mod(dp, LDAP_MOD_ADD);
		slapi_modify_internal_set_pb_ext (pb, sdn,
		        slapi_mods_get_ldapmods_byref(mods), NULL, NULL, 
		        repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
		slapi_modify_internal_pb (pb);
	
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    }

	slapi_pblock_destroy (pb);
	slapi_mods_free(&mods);
	slapi_sdn_free(&sdn);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_save_dirsync_cookie\n" );
	return rc;
}

/*  reads the cookie in dse.ldif to the replication agreement entry
	returns: ldap result code of ldap operation, or 
			 LDAP_NO_SUCH_ATTRIBUTE. (this is the equilivent of a null cookie) */
int windows_private_load_dirsync_cookie(const Repl_Agmt *ra)
{
	Dirsync_Private *dp = NULL;
    Slapi_PBlock *pb = NULL;
  
	Slapi_DN* sdn = NULL;
	int rc = 0;
	Slapi_Entry *entry = NULL;
	Slapi_Attr *attr = NULL;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_load_dirsync_cookie\n" );
	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);


	pb = slapi_pblock_new ();
	sdn = slapi_sdn_dup( agmt_get_dn_byref(ra) );
	

	rc  = slapi_search_internal_get_entry(sdn, NULL, &entry, 
		                                  repl_get_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION));

	if (rc == 0)
	{
		rc= slapi_entry_attr_find( entry, type_nsds7DirsyncCookie, &attr );
		if (attr)
		{
			struct berval **vals;
			rc = slapi_attr_get_bervals_copy(attr, &vals );
		
			if (vals)
			{
				dp->dirsync_cookie_len = (int)  (vals[0])->bv_len;
				slapi_ch_free_string(&dp->dirsync_cookie);

				dp->dirsync_cookie = ( char* ) slapi_ch_malloc(dp->dirsync_cookie_len + 1);
				memcpy(dp->dirsync_cookie,(vals[0]->bv_val), (vals[0])->bv_len+1);

			}

			ber_bvecfree(vals);
			/* we do not free attr */

		}
		else
		{
			rc = LDAP_NO_SUCH_ATTRIBUTE;
		}
	}

	if (entry)
	{
		 slapi_entry_free(entry);
	}
	
	slapi_sdn_free( &sdn);
	slapi_pblock_destroy (pb);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_load_dirsync_cookie\n" );

	return rc;
}

/* get returns a pointer to the structure - do not free */
Slapi_Entry *windows_private_get_raw_entry(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_raw_entry\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_raw_entry\n" );

	return dp->raw_entry;
}

/* this is passin - windows_private owns the pointer, not a copy */
void windows_private_set_raw_entry(const Repl_Agmt *ra, Slapi_Entry *e)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_raw_entry\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	/* If the keep raw entry flag is set, just free the passed
	 * in entry and leave the current raw entry in place. */
	if (windows_private_get_keep_raw_entry(ra)) {
		slapi_entry_free(e);
	} else {
		slapi_entry_free(dp->raw_entry);
		dp->raw_entry = e;
	}

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_raw_entry\n" );
}

/* Setting keep to 1 will cause the current raw entry to remain, even if
 * windows_private_set_raw_entry() is called.  This behavior will persist
 * until this flag is set back to 0. */
void windows_private_set_keep_raw_entry(const Repl_Agmt *ra, int keep)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_keep_raw_entry\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->keep_raw_entry = keep;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_keep_raw_entry\n" );
}

int windows_private_get_keep_raw_entry(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_keep_raw_entry\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_keep_raw_entry\n" );

	return dp->keep_raw_entry;
}

void *windows_private_get_api_cookie(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_api_cookie\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_api_cookie\n" );

	return dp->api_cookie;
}

void windows_private_set_api_cookie(Repl_Agmt *ra, void *api_cookie)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_api_cookie\n" );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	dp->api_cookie = api_cookie;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_api_cookie\n" );
}

time_t
windows_private_get_sync_interval(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_sync_interval\n" );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_sync_interval\n" );

	return dp->sync_interval;	
}

void
windows_private_set_sync_interval(Repl_Agmt *ra, char *str)
{
	Dirsync_Private *dp;
	time_t tmpval = 0;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_sync_interval\n" );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	if (str && (tmpval = (time_t)atol(str))) {
		dp->sync_interval = tmpval;
	}

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_sync_interval\n" );
}

int
windows_private_get_move_action(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_get_move_action\n" );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_get_move_action\n" );

	return dp->move_action;	
}

void
windows_private_set_move_action(const Repl_Agmt *ra, int value)
{
	Dirsync_Private *dp;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "=> windows_private_set_move_action\n" );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	dp->move_action = value;

	LDAPDebug0Args( LDAP_DEBUG_TRACE, "<= windows_private_set_move_action\n" );
}

static PRCallOnceType winsync_callOnce = {0,0};

struct winsync_plugin {
    struct winsync_plugin *next; /* see PRCList - declare here to avoid lots of casting */
    struct winsync_plugin *prev; /* see PRCList - declare here to avoid lots of casting */
    void **api; /* the api - array of function pointers */
    int maxapi; /* the max index i.e. the api version */
    int precedence; /* lower number == higher precedence */
};
static struct winsync_plugin winsync_plugin_list;

#define DECL_WINSYNC_API_IDX_FUNC(theapi,idx,maxidx,thetype,thefunc)    \
    thetype thefunc = (theapi && (idx <= maxidx) && theapi[idx]) ? \
        (thetype)theapi[idx] : NULL;

#define WINSYNC_PLUGIN_CALL_PLUGINS_BEGIN(idx,thetype,thefunc) \
    struct winsync_plugin *elem; \
    for (elem = PR_LIST_HEAD(&winsync_plugin_list); \
         elem && (elem != &winsync_plugin_list); \
         elem = PR_NEXT_LINK(elem)) { \
        DECL_WINSYNC_API_IDX_FUNC(elem->api,idx,elem->maxapi,thetype,thefunc); \
        if (thefunc) {

#define WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(idx,thetype,thefunc) \
    WINSYNC_PLUGIN_CALL_PLUGINS_BEGIN(idx,thetype,thefunc) \
    void *cookie = winsync_plugin_cookie_find(ra, elem->api);

#define WINSYNC_PLUGIN_CALL_PLUGINS_END } /* this one matches if thefunc */ } /* this one matches the for loop */

/* this structure is per agreement - to store the cookie per agreement
   for each winsync plugin */
struct winsync_plugin_cookie {
    struct winsync_plugin_cookie *next; /* see PRCList - declare here to avoid lots of casting */
    struct winsync_plugin_cookie *prev; /* see PRCList - declare here to avoid lots of casting */
    void **api; /* the api - array of function pointers */
    void *cookie; /* plugin data */
};

static struct winsync_plugin *
new_winsync_plugin(void **theapi, int maxapi, int precedence)
{
    struct winsync_plugin *wpi = (struct winsync_plugin *)slapi_ch_calloc(1, sizeof(struct winsync_plugin));
    wpi->api = theapi;
    wpi->maxapi = maxapi;
    wpi->precedence = precedence;
    return wpi;
}

static struct winsync_plugin *
windows_plugin_find(void **theapi)
{
    struct winsync_plugin *elem = PR_LIST_HEAD(&winsync_plugin_list);
    while (elem && (elem != &winsync_plugin_list)) {
        if (theapi == elem->api) {
            return elem;
        }
        elem = PR_NEXT_LINK(elem);
    }
    return NULL;
}

/* returns 0 for success - 1 means already added - -1 means some error */
static int
windows_plugin_add(void **theapi, int maxapi)
{
    int precedence = WINSYNC_PLUGIN_DEFAULT_PRECEDENCE;
    DECL_WINSYNC_API_IDX_FUNC(theapi,WINSYNC_PLUGIN_PRECEDENCE_CB,maxapi,winsync_plugin_precedence_cb,thefunc);

    if (thefunc) {
        /* supports precedence */
        precedence = (*thefunc)();
    }
    if (PR_CLIST_IS_EMPTY(&winsync_plugin_list)) {
        struct winsync_plugin *wpi = new_winsync_plugin(theapi, maxapi, precedence);
        PR_INSERT_LINK(wpi, &winsync_plugin_list);
        return 0;
    } else if (windows_plugin_find(theapi)) {
        return 1; /* already in list */
    } else {
        struct winsync_plugin *wpi = new_winsync_plugin(theapi, maxapi, precedence);
        struct winsync_plugin *elem = PR_LIST_HEAD(&winsync_plugin_list);
        while (elem && (elem != &winsync_plugin_list)) {
            if (precedence < elem->precedence) {
                PR_INSERT_BEFORE(wpi, elem);
                wpi = NULL; /* owned by list now */
                break;
            }
            elem = PR_NEXT_LINK(elem);
        }
        if (elem && wpi) { /* was not added - precedence too high */
            /* just add to end of list */
            PR_INSERT_BEFORE(wpi, elem);
            wpi = NULL; /* owned by list now */
        }
        /* if we got here and wpi is not NULL we need to free wpi */
        slapi_ch_free((void **)&wpi);
        return 0;
    }
    return -1;
}

static PRStatus
windows_plugin_callonce(void)
{
    char *guids[] = {WINSYNC_v3_0_GUID, WINSYNC_v2_0_GUID, WINSYNC_v1_0_GUID, NULL};
    int maxapis[] = {WINSYNC_PLUGIN_VERSION_3_END, WINSYNC_PLUGIN_VERSION_2_END,
                     WINSYNC_PLUGIN_VERSION_1_END, 0};
    int ii;

    PR_INIT_CLIST(&winsync_plugin_list);
    /* loop through all of the registered winsync plugins - look for them in reverse
       version order (e.g. look for v3 first) - if there are no plugins registered
       for the given version, or we have already registered all plugins for a given
       version, just go to the next lowest version */
    for (ii = 0; guids[ii]; ++ii) {
        char *guid = guids[ii];
        int maxapi = maxapis[ii];
        void ***theapis = NULL;
        
        if (slapi_apib_get_interface_all(guid, &theapis) || (NULL == theapis)) {
            LDAPDebug1Arg(LDAP_DEBUG_PLUGIN,
                          "<-- windows_plugin_callonce -- no more windows plugin APIs registered "
                          "for GUID [%s] -- end\n",
                          guid);
        } else {
            int idx;
            for (idx = 0; theapis && theapis[idx]; ++idx) {
                if (windows_plugin_add(theapis[idx], maxapi)) {
                    LDAPDebug(LDAP_DEBUG_PLUGIN,
                              "<-- windows_plugin_callonce -- already added windows plugin API "
                              "[%d][0x%p] for GUID [%s] -- end\n",
                              idx, theapis[idx], guid);
                }
            }
        }
        slapi_ch_free((void **)&theapis);
    }
    return PR_SUCCESS;
}

static struct winsync_plugin_cookie *
new_winsync_plugin_cookie(void **theapi, void *cookie)
{
    struct winsync_plugin_cookie *wpc = (struct winsync_plugin_cookie *)slapi_ch_calloc(1, sizeof(struct winsync_plugin_cookie));
    wpc->api = theapi;
    wpc->cookie = cookie;
    return wpc;
}

static void *
winsync_plugin_cookie_find(const Repl_Agmt *ra, void **theapi)
{
    if (ra) {
        struct winsync_plugin_cookie *list = (struct winsync_plugin_cookie *)windows_private_get_api_cookie(ra);
        if (list) {
            struct winsync_plugin_cookie *elem = PR_LIST_HEAD(list);
            while (elem && (elem != list)) {
                if (theapi == elem->api) {
                    return elem->cookie;
                }
                elem = PR_NEXT_LINK(elem);
            }
        }
    }
    return NULL;
}

static void
winsync_plugin_cookie_add(struct winsync_plugin_cookie **list, void **theapi, void *cookie)
{
    struct winsync_plugin_cookie *elem = NULL;
    if (!*list) {
        *list = new_winsync_plugin_cookie(NULL, NULL);
        PR_INIT_CLIST(*list);
    }
    elem = new_winsync_plugin_cookie(theapi, cookie);
    PR_INSERT_BEFORE(elem, *list);
    return;
}

void
windows_plugin_init(Repl_Agmt *ra)
{
    struct winsync_plugin_cookie *list = NULL;
    void *cookie = NULL;
    PRStatus rv;

    LDAPDebug0Args( LDAP_DEBUG_PLUGIN, "--> windows_plugin_init_start -- begin\n");

    rv = PR_CallOnce(&winsync_callOnce, windows_plugin_callonce);

    /* call each plugin init function in turn - store the returned cookie
       indexed by the api */
    {
        WINSYNC_PLUGIN_CALL_PLUGINS_BEGIN(WINSYNC_PLUGIN_INIT_CB,winsync_plugin_init_cb,thefunc)
            cookie = (*thefunc)(windows_private_get_directory_subtree(ra),
                                windows_private_get_windows_subtree(ra));
            if (cookie) {
                winsync_plugin_cookie_add(&list, elem->api, cookie);
            }
        WINSYNC_PLUGIN_CALL_PLUGINS_END
    }
       
    windows_private_set_api_cookie(ra, list);

    LDAPDebug0Args( LDAP_DEBUG_PLUGIN, "<-- windows_plugin_init_start -- end\n");
    return;
}

void
windows_plugin_cleanup_agmt(Repl_Agmt *ra)
{
    struct winsync_plugin_cookie *list = (struct winsync_plugin_cookie *)windows_private_get_api_cookie(ra);
    struct winsync_plugin_cookie *elem = NULL;

    while (list && !PR_CLIST_IS_EMPTY(list)) {
        elem = PR_LIST_HEAD(list);
        PR_REMOVE_LINK(elem);
        slapi_ch_free((void **)&elem);
    }
    slapi_ch_free((void **)&list);
    windows_private_set_api_cookie(ra, NULL);
    return;
}

void
winsync_plugin_call_dirsync_search_params_cb(const Repl_Agmt *ra, const char *agmt_dn,
                                             char **base, int *scope, char **filter,
                                             char ***attrs, LDAPControl ***serverctrls)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_DIRSYNC_SEARCH_CB,winsync_search_params_cb,thefunc)
        (*thefunc)(cookie, agmt_dn, base, scope, filter, attrs, serverctrls);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_search_cb(const Repl_Agmt *ra, const char *agmt_dn,
                                     char **base, int *scope, char **filter,
                                     char ***attrs, LDAPControl ***serverctrls)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_SEARCH_CB,winsync_search_params_cb,thefunc)
        (*thefunc)(cookie, agmt_dn, base, scope, filter, attrs, serverctrls);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_search_entry_cb(const Repl_Agmt *ra, const char *agmt_dn,
                                           char **base, int *scope, char **filter,
                                           char ***attrs, LDAPControl ***serverctrls)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_SEARCH_ENTRY_CB,winsync_search_params_cb,thefunc)
        (*thefunc)(cookie, agmt_dn, base, scope, filter, attrs, serverctrls);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_search_all_cb(const Repl_Agmt *ra, const char *agmt_dn,
                                         char **base, int *scope, char **filter,
                                         char ***attrs, LDAPControl ***serverctrls)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_SEARCH_ALL_CB,winsync_search_params_cb,thefunc)
        (*thefunc)(cookie, agmt_dn, base, scope, filter, attrs, serverctrls);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                       Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                       Slapi_Mods *smods, int *do_modify)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_MOD_USER_CB,winsync_pre_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, do_modify);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                        Slapi_Mods *smods, int *do_modify)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_MOD_GROUP_CB,winsync_pre_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, do_modify);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                       Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                       Slapi_Mods *smods, int *do_modify)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_MOD_USER_CB,winsync_pre_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, do_modify);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                        Slapi_Mods *smods, int *do_modify)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_MOD_GROUP_CB,winsync_pre_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, do_modify);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_add_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                       Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_ADD_USER_CB,winsync_pre_add_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ds_add_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_DS_ADD_GROUP_CB,winsync_pre_add_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_get_new_ds_user_dn_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                          Slapi_Entry *ad_entry, char **new_dn_string,
                                          const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_GET_NEW_DS_USER_DN_CB,winsync_get_new_dn_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, new_dn_string, ds_suffix, ad_suffix);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_get_new_ds_group_dn_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                           Slapi_Entry *ad_entry, char **new_dn_string,
                                           const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_GET_NEW_DS_GROUP_DN_CB,winsync_get_new_dn_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, new_dn_string, ds_suffix, ad_suffix);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_mod_user_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                            const Slapi_DN *local_dn,
                                            const Slapi_Entry *ds_entry,
                                            LDAPMod * const *origmods,
                                            Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_MOD_USER_MODS_CB,winsync_pre_ad_mod_mods_cb,thefunc)
        (*thefunc)(cookie, rawentry, local_dn, ds_entry, origmods, remote_dn, modstosend);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_mod_group_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                             const Slapi_DN *local_dn,
                                             const Slapi_Entry *ds_entry,
                                             LDAPMod * const *origmods,
                                             Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_MOD_GROUP_MODS_CB,winsync_pre_ad_mod_mods_cb,thefunc)
        (*thefunc)(cookie, rawentry, local_dn, ds_entry, origmods, remote_dn, modstosend);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

int
winsync_plugin_call_can_add_entry_to_ad_cb(const Repl_Agmt *ra, const Slapi_Entry *local_entry,
                                           const Slapi_DN *remote_dn)
{
    int canadd = 1;
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_CAN_ADD_ENTRY_TO_AD_CB,winsync_can_add_to_ad_cb,thefunc)
        if (canadd) {
            canadd = (*thefunc)(cookie, local_entry, remote_dn);
        }
    WINSYNC_PLUGIN_CALL_PLUGINS_END;
    return canadd;
}

void
winsync_plugin_call_begin_update_cb(const Repl_Agmt *ra, const Slapi_DN *ds_subtree,
                                    const Slapi_DN *ad_subtree, int is_total)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_BEGIN_UPDATE_CB,winsync_plugin_update_cb,thefunc)
        (*thefunc)(cookie, ds_subtree, ad_subtree, is_total);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_end_update_cb(const Repl_Agmt *ra, const Slapi_DN *ds_subtree,
                                  const Slapi_DN *ad_subtree, int is_total)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_END_UPDATE_CB,winsync_plugin_update_cb,thefunc)
        (*thefunc)(cookie, ds_subtree, ad_subtree, is_total);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_destroy_agmt_cb(const Repl_Agmt *ra,
                                    const Slapi_DN *ds_subtree,
                                    const Slapi_DN *ad_subtree)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_DESTROY_AGMT_CB,winsync_plugin_destroy_agmt_cb,thefunc)
        (*thefunc)(cookie, ds_subtree, ad_subtree);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                        Slapi_Mods *smods, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_MOD_USER_CB,winsync_post_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                         Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                         Slapi_Mods *smods, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_MOD_GROUP_CB,winsync_post_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ds_mod_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                        Slapi_Mods *smods, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_DS_MOD_USER_CB,winsync_post_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ds_mod_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                         Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                         Slapi_Mods *smods, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_DS_MOD_GROUP_CB,winsync_post_mod_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, smods, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ds_add_user_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                        Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_DS_ADD_USER_CB,winsync_post_add_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ds_add_group_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                         Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_DS_ADD_GROUP_CB,winsync_post_add_cb,thefunc)
        (*thefunc)(cookie, rawentry, ad_entry, ds_entry, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_add_user_cb(const Repl_Agmt *ra, Slapi_Entry *ad_entry,
                                       Slapi_Entry *ds_entry)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_ADD_USER_CB,winsync_pre_ad_add_cb,thefunc)
        (*thefunc)(cookie, ad_entry, ds_entry);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_pre_ad_add_group_cb(const Repl_Agmt *ra, Slapi_Entry *ad_entry,
                                        Slapi_Entry *ds_entry)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_PRE_AD_ADD_GROUP_CB,winsync_pre_ad_add_cb,thefunc)
        (*thefunc)(cookie, ad_entry, ds_entry);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_add_user_cb(const Repl_Agmt *ra, Slapi_Entry *ad_entry,
                                        Slapi_Entry *ds_entry, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_ADD_USER_CB,winsync_post_ad_add_cb,thefunc)
        (*thefunc)(cookie, ad_entry, ds_entry, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_add_group_cb(const Repl_Agmt *ra, Slapi_Entry *ad_entry,
                                         Slapi_Entry *ds_entry, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_ADD_GROUP_CB,winsync_post_ad_add_cb,thefunc)
        (*thefunc)(cookie, ad_entry, ds_entry, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_mod_user_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                             const Slapi_DN *local_dn,
                                             const Slapi_Entry *ds_entry,
                                             LDAPMod * const *origmods,
                                             Slapi_DN *remote_dn, LDAPMod **modstosend, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_MOD_USER_MODS_CB,winsync_post_ad_mod_mods_cb,thefunc)
        (*thefunc)(cookie, rawentry, local_dn, ds_entry, origmods, remote_dn, modstosend, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}

void
winsync_plugin_call_post_ad_mod_group_mods_cb(const Repl_Agmt *ra, const Slapi_Entry *rawentry,
                                              const Slapi_DN *local_dn,
                                              const Slapi_Entry *ds_entry,
                                              LDAPMod * const *origmods,
                                              Slapi_DN *remote_dn, LDAPMod **modstosend, int *result)
{
    WINSYNC_PLUGIN_CALL_PLUGINS_COOKIE_BEGIN(WINSYNC_PLUGIN_POST_AD_MOD_GROUP_MODS_CB,winsync_post_ad_mod_mods_cb,thefunc)
        (*thefunc)(cookie, rawentry, local_dn, ds_entry, origmods, remote_dn, modstosend, result);
    WINSYNC_PLUGIN_CALL_PLUGINS_END;

    return;
}


/*
  The following are sample code stubs to show how to implement
  a plugin which uses this api
*/

#define WINSYNC_SAMPLE_CODE
#ifdef WINSYNC_SAMPLE_CODE

#include "slapi-plugin.h"
#include "winsync-plugin.h"

static char *test_winsync_plugin_name = "test_winsync_api";

static void *
test_winsync_api_init(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_init [%s] [%s] -- begin\n",
                    slapi_sdn_get_dn(ds_subtree),
                    slapi_sdn_get_dn(ad_subtree));

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_init -- end\n");

    return NULL;
}

static void
test_winsync_dirsync_search_params_cb(void *cbdata, const char *agmt_dn,
                                      char **base, int *scope, char **filter,
                                      char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_dirsync_search_params_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_dirsync_search_params_cb -- end\n");

    return;
}

/* called before searching for a single entry from AD - agmt_dn will be NULL */
static void
test_winsync_pre_ad_search_cb(void *cbdata, const char *agmt_dn,
                              char **base, int *scope, char **filter,
                              char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_search_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_search_cb -- end\n");

    return;
}

/* called before an internal search to get a single DS entry - agmt_dn will be NULL */
static void
test_winsync_pre_ds_search_entry_cb(void *cbdata, const char *agmt_dn,
                                    char **base, int *scope, char **filter,
                                    char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_search_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_search_cb -- end\n");

    return;
}

/* called before the total update to get all entries from the DS to sync to AD */
static void
test_winsync_pre_ds_search_all_cb(void *cbdata, const char *agmt_dn,
                                  char **base, int *scope, char **filter,
                                  char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_search_all_cb -- orig filter [%s] -- begin\n",
                    ((filter && *filter) ? *filter : "NULL"));

#ifdef THIS_IS_JUST_AN_EXAMPLE
    if (filter) {
        /* We only want to grab users from the ds side - no groups */
        slapi_ch_free_string(filter);
        /* maybe use ntUniqueId=* - only get users that have already been
           synced with AD already - ntUniqueId and ntUserDomainId are
           indexed for equality only - need to add presence? */
        *filter = slapi_ch_strdup("(&(objectclass=ntuser)(ntUserDomainId=*))");
        slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                        "--> test_winsync_pre_ds_search_all_cb -- new filter [%s]\n",
                        *filter ? *filter : "NULL"));
    }
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_search_all_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_mod_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry,
                                Slapi_Mods *smods, int *do_modify)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_mod_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_add_user_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_add_user_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_add_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ds_add_group_cb(void *cbdata, const Slapi_Entry *rawentry,
                                Slapi_Entry *ad_entry, Slapi_Entry *ds_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ds_add_group_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ds_add_group_cb -- end\n");

    return;
}

static void
test_winsync_get_new_ds_user_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
                                   Slapi_Entry *ad_entry, char **new_dn_string,
                                   const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_get_new_ds_user_dn_cb -- old dn [%s] -- begin\n",
                    *new_dn_string);

#ifdef THIS_IS_JUST_AN_EXAMPLE
    char **rdns = slapi_ldap_explode_dn(*new_dn_string, 0);
    if (!rdns || !rdns[0]) {
        slapi_ldap_value_free(rdns);
        return;
    }

    slapi_ch_free_string(new_dn_string);
    *new_dn_string = PR_smprintf("%s,%s", rdns[0], slapi_sdn_get_dn(ds_suffix));
    slapi_ldap_value_free(rdns);
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_get_new_ds_user_dn_cb -- new dn [%s] -- end\n",
                    *new_dn_string);

    return;
}

static void
test_winsync_get_new_ds_group_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
                                   Slapi_Entry *ad_entry, char **new_dn_string,
                                   const Slapi_DN *ds_suffix, const Slapi_DN *ad_suffix)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_get_new_ds_group_dn_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_get_new_ds_group_dn_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_user_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
                                     const Slapi_Entry *ds_entry,
                                     const Slapi_DN *local_dn, LDAPMod * const *origmods,
                                     Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_user_mods_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_user_mods_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_mod_group_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
                                     const Slapi_Entry *ds_entry,
                                     const Slapi_DN *local_dn, LDAPMod * const *origmods,
                                     Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_mod_group_mods_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_mod_group_mods_cb -- end\n");

    return;
}

static int
test_winsync_can_add_entry_to_ad_cb(void *cbdata, const Slapi_Entry *local_entry,
                                    const Slapi_DN *remote_dn)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_can_add_entry_to_ad_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_can_add_entry_to_ad_cb -- end\n");

    /*    return 0;*/ /* false - do not allow entries to be added to ad */
    return 1; /* true - allow entries to be added to ad */
}

static void
test_winsync_begin_update_cb(void *cbdata, const Slapi_DN *ds_subtree,
                             const Slapi_DN *ad_subtree, int is_total)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_begin_update_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_begin_update_cb -- end\n");

    return;
}

static void
test_winsync_end_update_cb(void *cbdata, const Slapi_DN *ds_subtree,
                           const Slapi_DN *ad_subtree, int is_total)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_end_update_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_end_update_cb -- end\n");

    return;
}

static void
test_winsync_destroy_agmt_cb(void *cbdata, const Slapi_DN *ds_subtree,
                             const Slapi_DN *ad_subtree)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_destroy_agmt_cb -- begin\n");

    /* free(cbdata); */
    
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_destroy_agmt_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_mod_user_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_mod_user_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying AD entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ad_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_mod_group_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_mod_group_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying AD entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ad_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_post_ds_mod_user_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ds_mod_user_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying DS entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ds_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ds_mod_user_cb -- end\n");

    return;
}

static void
test_winsync_post_ds_mod_group_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, Slapi_Mods *smods, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ds_mod_group_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying DS entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ds_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ds_mod_group_cb -- end\n");

    return;
}

static void
test_winsync_post_ds_add_user_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ds_add_user_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of adding DS entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ds_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ds_add_user_cb -- end\n");

    return;
}

static void
test_winsync_post_ds_add_group_cb(void *cookie, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry, Slapi_Entry *ds_entry, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ds_add_group_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of adding DS entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ds_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ds_add_group_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_add_user_cb(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_add_user_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Adding AD entry [%s] from add of DS entry [%s]\n",
                    slapi_entry_get_dn(ad_entry), slapi_entry_get_dn(ds_entry));
    /* make modifications to ad_entry here */
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_add_user_cb -- end\n");

    return;
}

static void
test_winsync_pre_ad_add_group_cb(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_pre_ad_add_group_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Adding AD entry [%s] from add of DS entry [%s]\n",
                    slapi_entry_get_dn(ad_entry), slapi_entry_get_dn(ds_entry));
    /* make modifications to ad_entry here */
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_pre_ad_add_group_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_add_user_cb(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_add_user_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of adding AD entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ad_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_add_user_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_add_group_cb(void *cookie, Slapi_Entry *ds_entry, Slapi_Entry *ad_entry, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_add_group_cb -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of adding AD entry [%s] was [%d:%s]\n",
                    slapi_entry_get_dn(ad_entry), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_add_group_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_mod_user_mods_cb(void *cookie, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_mod_user_mods_cb  -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying AD entry [%s] was [%d:%s]\n",
                    slapi_sdn_get_dn(remote_dn), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_mod_user_mods_cb -- end\n");

    return;
}

static void
test_winsync_post_ad_mod_group_mods_cb(void *cookie, const Slapi_Entry *rawentry, const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods, Slapi_DN *remote_dn, LDAPMod ***modstosend, int *result)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_post_ad_mod_group_mods_cb  -- begin\n");

#ifdef THIS_IS_JUST_AN_EXAMPLE
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "Result of modifying AD entry [%s] was [%d:%s]\n",
                    slapi_sdn_get_dn(remote_dn), *result, ldap_err2string(*result));
#endif

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_post_ad_mod_group_mods_cb -- end\n");

    return;
}

static int
test_winsync_precedence(void)
{
    return 99;
}

/**
 * Plugin identifiers
 */
static Slapi_PluginDesc test_winsync_pdesc = {
    "test-winsync-plugin",
    VENDOR,
    DS_PACKAGE_VERSION,
    "test winsync plugin"
};

static Slapi_ComponentId *test_winsync_plugin_id = NULL;

#ifdef TEST_V1_WINSYNC_API
static void *test_winsync_api_v1[] = {
    NULL, /* reserved for api broker use, must be zero */
    test_winsync_api_init,
    test_winsync_dirsync_search_params_cb,
    test_winsync_pre_ad_search_cb,
    test_winsync_pre_ds_search_entry_cb,
    test_winsync_pre_ds_search_all_cb,
    test_winsync_pre_ad_mod_user_cb,
    test_winsync_pre_ad_mod_group_cb,
    test_winsync_pre_ds_mod_user_cb,
    test_winsync_pre_ds_mod_group_cb,
    test_winsync_pre_ds_add_user_cb,
    test_winsync_pre_ds_add_group_cb,
    test_winsync_get_new_ds_user_dn_cb,
    test_winsync_get_new_ds_group_dn_cb,
    test_winsync_pre_ad_mod_user_mods_cb,
    test_winsync_pre_ad_mod_group_mods_cb,
    test_winsync_can_add_entry_to_ad_cb,
    test_winsync_begin_update_cb,
    test_winsync_end_update_cb,
    test_winsync_destroy_agmt_cb
};
#endif /* TEST_V1_WINSYNC_API */

#ifdef TEST_V2_WINSYNC_API
static void *test_winsync_api_v2[] = {
    NULL, /* reserved for api broker use, must be zero */
    test_winsync_api_init,
    test_winsync_dirsync_search_params_cb,
    test_winsync_pre_ad_search_cb,
    test_winsync_pre_ds_search_entry_cb,
    test_winsync_pre_ds_search_all_cb,
    test_winsync_pre_ad_mod_user_cb,
    test_winsync_pre_ad_mod_group_cb,
    test_winsync_pre_ds_mod_user_cb,
    test_winsync_pre_ds_mod_group_cb,
    test_winsync_pre_ds_add_user_cb,
    test_winsync_pre_ds_add_group_cb,
    test_winsync_get_new_ds_user_dn_cb,
    test_winsync_get_new_ds_group_dn_cb,
    test_winsync_pre_ad_mod_user_mods_cb,
    test_winsync_pre_ad_mod_group_mods_cb,
    test_winsync_can_add_entry_to_ad_cb,
    test_winsync_begin_update_cb,
    test_winsync_end_update_cb,
    test_winsync_destroy_agmt_cb,
    test_winsync_post_ad_mod_user_cb,
    test_winsync_post_ad_mod_group_cb,
    test_winsync_post_ds_mod_user_cb,
    test_winsync_post_ds_mod_group_cb,
    test_winsync_post_ds_add_user_cb,
    test_winsync_post_ds_add_group_cb,
    test_winsync_pre_ad_add_user_cb,
    test_winsync_pre_ad_add_group_cb,
    test_winsync_post_ad_add_user_cb,
    test_winsync_post_ad_add_group_cb,
    test_winsync_post_ad_mod_user_mods_cb,
    test_winsync_post_ad_mod_group_mods_cb
};
#endif /* TEST_V2_WINSYNC_API */

static void *test_winsync_api_v3[] = {
    NULL, /* reserved for api broker use, must be zero */
    test_winsync_api_init,
    test_winsync_dirsync_search_params_cb,
    test_winsync_pre_ad_search_cb,
    test_winsync_pre_ds_search_entry_cb,
    test_winsync_pre_ds_search_all_cb,
    test_winsync_pre_ad_mod_user_cb,
    test_winsync_pre_ad_mod_group_cb,
    test_winsync_pre_ds_mod_user_cb,
    test_winsync_pre_ds_mod_group_cb,
    test_winsync_pre_ds_add_user_cb,
    test_winsync_pre_ds_add_group_cb,
    test_winsync_get_new_ds_user_dn_cb,
    test_winsync_get_new_ds_group_dn_cb,
    test_winsync_pre_ad_mod_user_mods_cb,
    test_winsync_pre_ad_mod_group_mods_cb,
    test_winsync_can_add_entry_to_ad_cb,
    test_winsync_begin_update_cb,
    test_winsync_end_update_cb,
    test_winsync_destroy_agmt_cb,
    test_winsync_post_ad_mod_user_cb,
    test_winsync_post_ad_mod_group_cb,
    test_winsync_post_ds_mod_user_cb,
    test_winsync_post_ds_mod_group_cb,
    test_winsync_post_ds_add_user_cb,
    test_winsync_post_ds_add_group_cb,
    test_winsync_pre_ad_add_user_cb,
    test_winsync_pre_ad_add_group_cb,
    test_winsync_post_ad_add_user_cb,
    test_winsync_post_ad_add_group_cb,
    test_winsync_post_ad_mod_user_mods_cb,
    test_winsync_post_ad_mod_group_mods_cb,
    test_winsync_precedence
};

static int
test_winsync_plugin_start(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_start -- begin\n");

	if( slapi_apib_register(WINSYNC_v3_0_GUID, test_winsync_api_v3) ) {
        slapi_log_error( SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_start -- failed to register winsync api -- end\n");
        return -1;
	}
	
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_plugin_start -- end\n");
	return 0;
}

static int
test_winsync_plugin_close(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_close -- begin\n");

	slapi_apib_unregister(WINSYNC_v3_0_GUID);

    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "<-- test_winsync_plugin_close -- end\n");
	return 0;
}

/* this is the slapi plugin init function,
   not the one used by the winsync api
*/
int test_winsync_plugin_init(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                    "--> test_winsync_plugin_init -- begin\n");

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
                           SLAPI_PLUGIN_VERSION_01 ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                          (void *) test_winsync_plugin_start ) != 0 ||
         slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                          (void *) test_winsync_plugin_close ) != 0 ||
         slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&test_winsync_pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_init -- failed to register plugin -- end\n");
        return -1;
    }

    /* Retrieve and save the plugin identity to later pass to
       internal operations */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &test_winsync_plugin_id) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, test_winsync_plugin_name,
                         "<-- test_winsync_plugin_init -- failed to retrieve plugin identity -- end\n");
        return -1;
    }

    slapi_log_error( SLAPI_LOG_PLUGIN, test_winsync_plugin_name,
                     "<-- test_winsync_plugin_init -- end\n");
    return 0;
}

/*
dn: cn=Test Winsync API,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: Test Winsync API
nsslapd-pluginpath: libtestwinsync-plugin
nsslapd-plugininitfunc: test_winsync_plugin_init
nsslapd-plugintype: preoperation
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-pluginDescription: Test Winsync
nsslapd-pluginVendor: 389 project
nsslapd-pluginId: test-winsync
nsslapd-pluginVersion: 0.9
*/

#endif /* WINSYNC_SAMPLE_CODE */

/* #define WINSYNC_TEST_IPA */
#ifdef WINSYNC_TEST_IPA

#include "ipa-winsync.c"
#include "ipa-winsync-config.c"

#endif
