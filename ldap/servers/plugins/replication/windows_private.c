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
};

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

static int
windows_parse_config_entry(Repl_Agmt *ra, const char *type, Slapi_Entry *e)
{
	char *tmpstr = NULL;
	int retval = 0;
	
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7WindowsReplicaArea))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7WindowsReplicaArea);
		if (NULL != tmpstr)
		{
			windows_private_set_windows_subtree(ra, slapi_sdn_new_dn_passin(tmpstr) );
		}
		retval = 1;
		slapi_ch_free((void**)&tmpstr);
	}
	if (type == NULL || slapi_attr_types_equivalent(type,type_nsds7DirectoryReplicaArea))
	{
		tmpstr = slapi_entry_attr_get_charptr(e, type_nsds7DirectoryReplicaArea); 
		if (NULL != tmpstr)
		{
			windows_private_set_directory_subtree(ra, slapi_sdn_new_dn_passin(tmpstr) );
		}
		retval = 1;
		slapi_ch_free((void**)&tmpstr);
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
		retval = 1;
		slapi_ch_free((void**)&tmpstr);
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
		retval = 1;
		slapi_ch_free((void**)&tmpstr);
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
windows_init_agreement_from_entry(Repl_Agmt *ra, Slapi_Entry *e)
{
	agmt_set_priv(ra,windows_private_new());

	windows_parse_config_entry(ra,NULL,e);
}

const char* windows_private_get_purl(const Repl_Agmt *ra)
{
	const char* windows_purl;
	char *hostname;

	hostname = agmt_get_hostname(ra);
	windows_purl = slapi_ch_smprintf("ldap://%s:%d", hostname, agmt_get_port(ra));
	slapi_ch_free_string(&hostname);

	return windows_purl;
}

Dirsync_Private* windows_private_new()
{
	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_new\n", 0, 0, 0 );

	dp = (Dirsync_Private *)slapi_ch_calloc(sizeof(Dirsync_Private),1);

	dp->dirsync_maxattributecount = -1;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_new\n", 0, 0, 0 );
	return dp;

}

void windows_agreement_delete(Repl_Agmt *ra)
{

	Dirsync_Private *dp = (Dirsync_Private *) agmt_get_priv(ra);
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_delete\n", 0, 0, 0 );

	PR_ASSERT(dp  != NULL);
	
	/* DBDB: need to free payoad here */
	
	slapi_ch_free((void **)dp);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_delete\n", 0, 0, 0 );

}

int windows_private_get_isnt4(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_get_isnt4\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_get_isnt4\n", 0, 0, 0 );
	
		return dp->isnt4;	
}

void windows_private_set_isnt4(const Repl_Agmt *ra, int isit)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_isnt4\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		dp->isnt4 = isit;
		
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_isnt4\n", 0, 0, 0 );
}


/* Returns a copy of the Slapi_DN pointer, no need to free it */
const Slapi_DN* windows_private_get_windows_subtree (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_get_windows_subtree\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_get_windows_subtree\n", 0, 0, 0 );
	
		return dp->windows_subtree;	
}

const char *
windows_private_get_windows_domain(const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_get_windows_domain\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);
		
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_get_windows_domain\n", 0, 0, 0 );
	
		return dp->windows_domain;	
}

static void
windows_private_set_windows_domain(const Repl_Agmt *ra, char *domain)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_windows_domain\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		dp->windows_domain = domain;
		
		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_windows_domain\n", 0, 0, 0 );
	}

/* Returns a copy of the Slapi_DN pointer, no need to free it */
const Slapi_DN* windows_private_get_directory_subtree (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;

		LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_get_directory_replarea\n", 0, 0, 0 );

        PR_ASSERT(ra);

		dp = (Dirsync_Private *) agmt_get_priv(ra);
		PR_ASSERT (dp);

		LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_get_directory_replarea\n", 0, 0, 0 );
	
		return dp->directory_subtree; 
}

/* Takes a copy of the sdn passed in */
void windows_private_set_windows_subtree (const Repl_Agmt *ra,const Slapi_DN* sdn )
{

	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_windows_replarea\n", 0, 0, 0 );

	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	
	dp->windows_subtree = slapi_sdn_dup(sdn);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_windows_replarea\n", 0, 0, 0 );
}

/* Takes a copy of the sdn passed in */
void windows_private_set_directory_subtree (const Repl_Agmt *ra,const Slapi_DN* sdn )
{

	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_directory_replarea\n", 0, 0, 0 );

	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	
	dp->directory_subtree = slapi_sdn_dup(sdn);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_directory_replarea\n", 0, 0, 0 );
}

PRBool windows_private_create_users(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_create_users\n", 0, 0, 0 );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_create_users\n", 0, 0, 0 );

	return dp->create_users_from_dirsync;

}


void windows_private_set_create_users(const Repl_Agmt *ra, PRBool value)
{
	Dirsync_Private *dp;
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_create_users\n", 0, 0, 0 );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->create_users_from_dirsync = value;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_create_users\n", 0, 0, 0 );

}

PRBool windows_private_create_groups(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_create_groups\n", 0, 0, 0 );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_create_groups\n", 0, 0, 0 );

	return dp->create_groups_from_dirsync;

}


void windows_private_set_create_groups(const Repl_Agmt *ra, PRBool value)
{
	Dirsync_Private *dp;
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_set_create_groups\n", 0, 0, 0 );

	PR_ASSERT(ra);
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->create_groups_from_dirsync = value;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_set_create_groups\n", 0, 0, 0 );

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

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_dirsync_control\n", 0, 0, 0 );
	
	PR_ASSERT(ra);
	
	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);
	ber = 	ber_alloc();

	ber_printf( ber, "{iio}", dp->dirsync_flags, dp->dirsync_maxattributecount, dp->dirsync_cookie, dp->dirsync_cookie_len );

	slapi_build_control( REPL_DIRSYNC_CONTROL_OID, ber, PR_TRUE, &control);

	ber_free(ber,1);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_dirsync_control\n", 0, 0, 0 );


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
	LDAPControl *dirsync;
	BerElement *ber;
	ber_int_t hasMoreData;
	ber_int_t maxAttributeCount;
	BerValue  *serverCookie;
	int return_value = LDAP_SUCCESS;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_update_dirsync_control\n", 0, 0, 0 );

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
			return_value = LDAP_CONTROL_NOT_FOUND;
			goto choke;
		}
		else
		{
			dirsync = slapi_dup_control( controls[i-1]);
		}

		ber = ber_init( &dirsync->ldctl_value ) ;

		if (ber_scanf( ber, "{iiO}", &hasMoreData, &maxAttributeCount, &serverCookie) == LBER_ERROR)
		{
			return_value =  LDAP_CONTROL_NOT_FOUND;
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
	}
	else
	{
		return_value = LDAP_CONTROL_NOT_FOUND;
	}


	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_update_dirsync_control\n", 0, 0, 0 );
	/* return return_value; */
}

PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_dirsync_has_more\n", 0, 0, 0 );

	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_dirsync_has_more\n", 0, 0, 0 );

	return dp->dirsync_cookie_has_more;

}

void windows_private_null_dirsync_cookie(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_null_dirsync_control\n", 0, 0, 0 );

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);

	dp->dirsync_cookie_len = 0;
	slapi_ch_free_string(&dp->dirsync_cookie);
	dp->dirsync_cookie = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_null_dirsync_control\n", 0, 0, 0 );
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
    const char* dn = NULL;
	Slapi_DN* sdn = NULL;
	int rc = 0;
	Slapi_Mods *mods = NULL;

    
  
	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_save_dirsync_cookie\n", 0, 0, 0 );
	PR_ASSERT(ra);

	dp = (Dirsync_Private *) agmt_get_priv(ra);
	PR_ASSERT (dp);


	pb = slapi_pblock_new ();
  
	mods = windows_private_get_cookie_mod(dp, LDAP_MOD_REPLACE);
   	sdn = slapi_sdn_dup( agmt_get_dn_byref(ra) );
	dn = slapi_sdn_get_dn(sdn);

    slapi_modify_internal_set_pb (pb, dn, slapi_mods_get_ldapmods_byref(mods), NULL, NULL, 
                                  repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb (pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    if (rc == LDAP_NO_SUCH_ATTRIBUTE)
    {	/* try again, but as an add instead */
		mods = windows_private_get_cookie_mod(dp, LDAP_MOD_ADD);
		slapi_modify_internal_set_pb (pb, dn, slapi_mods_get_ldapmods_byref(mods), NULL, NULL, 
                                      repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
		slapi_modify_internal_pb (pb);
	
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    }

	slapi_pblock_destroy (pb);
	slapi_mods_free(&mods);
	slapi_sdn_free(&sdn);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_save_dirsync_cookie\n", 0, 0, 0 );
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

	LDAPDebug( LDAP_DEBUG_TRACE, "=> windows_private_load_dirsync_cookie\n", 0, 0, 0 );
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

	LDAPDebug( LDAP_DEBUG_TRACE, "<= windows_private_load_dirsync_cookie\n", 0, 0, 0 );

	return rc;
}

