/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* windows_private.c */

#include "repl.h"
#include "repl5.h"


struct windowsprivate {
  
  Slapi_DN *windows_replarea; /* DN of replicated area (on the windows side) */
  Slapi_DN *directory_replarea; /* DN of replicated area on directory side */
                                /* this simplifies the mapping as it's simply
								   from the former to the latter container, or
								   vice versa */

  int dirsync_flags;		
  int dirsync_maxattributecount;
  char *dirsync_cookie; 
  int dirsync_cookie_len;
  PRBool dirsync_cookie_has_more;
  
  PRBool create_users_from_dirsync;
};




Dirsync_Private* windows_private_new()
{
	Dirsync_Private *dp;
	dp = (Dirsync_Private *)slapi_ch_malloc(sizeof(Dirsync_Private));

	dp->windows_replarea = NULL;
	dp->dirsync_flags = 0;
	dp->dirsync_maxattributecount = -1;
	dp->dirsync_cookie = NULL;
	dp->dirsync_cookie_len = 0;
	dp->dirsync_cookie_has_more = 0;
	dp->create_users_from_dirsync = PR_TRUE;
	return dp;

}

void windows_private_delete(Dirsync_Private **dp)
{

	PR_ASSERT(dp  != NULL);
	PR_ASSERT(*dp != NULL);
	
	/* TODO: free cookie */
	
	/* slapi_sdn_done((**dp).windows_replarea); */
	slapi_ch_free((void **)dp);

}



const Slapi_DN* windows_private_get_windows_replarea (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;
        PR_ASSERT(ra);

		dp = (Dirsync_Private *) get_priv_from_agmt(ra);
		PR_ASSERT (dp);
	
		if(dp->windows_replarea)
			return slapi_sdn_dup (dp->windows_replarea); 
		else
			return NULL;
}

const Slapi_DN* windows_private_get_directory_replarea (const Repl_Agmt *ra)
{
		Dirsync_Private *dp;
        PR_ASSERT(ra);

		dp = (Dirsync_Private *) get_priv_from_agmt(ra);
		PR_ASSERT (dp);
	
		if(dp->directory_replarea)
			return slapi_sdn_dup (dp->directory_replarea); 
		else
			return NULL;
}


void windows_private_set_windows_replarea (const Repl_Agmt *ra,const Slapi_DN* sdn )
{

	Dirsync_Private *dp;
	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);
	
	dp->windows_replarea = slapi_sdn_dup(sdn);
}

void windows_private_set_directory_replarea (const Repl_Agmt *ra,const Slapi_DN* sdn )
{

	Dirsync_Private *dp;
	PR_ASSERT(ra);
	PR_ASSERT(sdn);

	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);
	
	dp->directory_replarea = slapi_sdn_dup(sdn);
}

PRBool windows_private_create_users(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;
	PR_ASSERT(ra);
	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);

	return dp->create_users_from_dirsync;

}


void windows_private_set_create_users(const Repl_Agmt *ra, PRBool value)
{
	Dirsync_Private *dp;
	PR_ASSERT(ra);
	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);

	dp->create_users_from_dirsync = value;

}


/* 
	This function returns the current Dirsync_Private that's inside 
	Repl_Agmt ra as a ldap control.

  */
LDAPControl* windows_private_dirsync_control(const Repl_Agmt *ra){

	LDAPControl *control = NULL;
	LDAPControl **lc = &control ;
	BerElement *ber;
	Dirsync_Private *dp;
	
	PR_ASSERT(ra);
	
	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);
	ber = 	ber_alloc();

	ber_printf( ber, "{iio}", dp->dirsync_flags, dp->dirsync_maxattributecount, dp->dirsync_cookie, dp->dirsync_cookie_len );

	slapi_build_control( REPL_DIRSYNC_CONTROL_OID, ber, PR_TRUE, &control);


	return control;

}

/* 
	This function scans the array of controls and updates the Repl_Agmt's 
	Dirsync_Private if the dirsync control is found. 

*/
void windows_private_update_dirsync_control(const Repl_Agmt *ra,LDAPControl **controls ){

	Dirsync_Private *dp;
    int foundDirsyncControl;
	int i;
	LDAPControl *dirsync;
	BerElement *ber;
	int hasMoreData;
	int maxAttributeCount;
	
	
	BerValue       *serverCookie;

    PR_ASSERT(ra);

	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);
	
	if (NULL != controls )
	{
		foundDirsyncControl = 0;
		for ( i = 0; (( controls[i] != NULL ) && ( !foundDirsyncControl )); i++ ) {
			foundDirsyncControl = !strcmp( controls[i]->ldctl_oid, REPL_DIRSYNC_CONTROL_OID );
		}

		if ( !foundDirsyncControl )
			return;
		else
			dirsync = slapi_dup_control( controls[i-1]);

		ber = ber_init( &dirsync->ldctl_value ) ;

		ber_scanf( ber, "{iiO}", &hasMoreData, &maxAttributeCount, &serverCookie);

		slapi_ch_free(&dp->dirsync_cookie);
		dp->dirsync_cookie = ( char* ) slapi_ch_malloc(serverCookie->bv_len + 1);

		memcpy(dp->dirsync_cookie, serverCookie->bv_val, serverCookie->bv_len);
		dp->dirsync_cookie_len = (int) serverCookie->bv_len; /* XXX shouldn't cast? */

		dp->dirsync_maxattributecount = maxAttributeCount;
		dp->dirsync_cookie_has_more = hasMoreData;

		ber_bvfree(serverCookie);
		ber_free(ber,1);
	}

}

PRBool windows_private_dirsync_has_more(const Repl_Agmt *ra)
{
	Dirsync_Private *dp;  
	PR_ASSERT(ra);

	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);

	return dp->dirsync_cookie_has_more;

}

void windows_private_null_dirsync_control(const Repl_Agmt *ra){

	Dirsync_Private *dp;
	dp = (Dirsync_Private *) get_priv_from_agmt(ra);
	PR_ASSERT (dp);

	dp->dirsync_cookie_len = 0;
	/* XXX should this value be free'd? */
	dp->dirsync_cookie = NULL;
}