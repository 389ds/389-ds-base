/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* bind.c  - ldbm backend bind and unbind routines */

#include "back-ldbm.h"

#if defined( XP_WIN32 )

typedef enum LDAPWAEnum {
	LDAPWA_NoDomainAttr = -3,
	LDAPWA_InvalidCredentials = -2,
	LDAPWA_Failure = -1,
    LDAPWA_Success= 0
} LDAPWAStatus;

int
GetDomainUsername(
	char *pszNTuserdomainid,
	char *pszNTDomain,
	char *pszNTUsername 
)
{
	char	*pszAttr, *pDomain, *pUsername;

	if( !pszNTuserdomainid )
		return( 1 );

	// Split the specially constructed attribute.
	pszAttr = slapi_ch_strdup( pszNTuserdomainid );

	pDomain = pszAttr;

	pUsername = strchr( pszAttr, ':' );
	if( pUsername == NULL )
		return( 1 );

	// Set the end of the NT Domain name, 
	// and the start of the NT username.
	*pUsername = (char)NULL;
	pUsername++;

	strcpy( pszNTDomain, pDomain);
	strcpy( pszNTUsername, pUsername);

	slapi_ch_free( (void**)&pszAttr );

	return( 0 );
}

/* Attempt Windows NT Authentication, using the password from the client app, 
   with the NT Domain and NT username, both stored in the entry.  
   If successful, the ldap_bind() is completed successsfully. */

LDAPWAStatus
WindowsAuthentication( 
	struct backentry	*e,
    struct berval	*cred
)
{
	Slapi_Attr	*a; 
	Slapi_Value *sval = NULL;
	int iStatus;
	char szNTDomain[MAX_PATH], szNTUsername[MAX_PATH];
	HANDLE	hToken = NULL;
	BOOL bLogonStatus = FALSE;
	int i= -1;
 
	/* Get the NT Domain and username - if the entry has such an attribute */
	if( !e || !e->ep_entry ||
		slapi_entry_attr_find( e->ep_entry, "ntuserdomainid", &a ) != 0)
	{
		return( LDAPWA_NoDomainAttr );
	}

	i= slapi_attr_first_value( a, &sval );
	if(sval==NULL)
	{
		return( LDAPWA_NoDomainAttr );	
	}

    while(i != -1)
	{
		const struct berval *val = slapi_value_get_berval(sval);
		char * colon = NULL;

		if (!val->bv_val || (strlen(val->bv_val) > (MAX_PATH<<1))) {
			LDAPDebug( LDAP_DEBUG_TRACE,
				"WindowsAuthentication => validation FAILED for \"%s\" on NT Domain : "
				"ntuserdomainid attr value too long\n",
				val->bv_val, 0, 0);
			i= slapi_attr_next_value(a, i, &sval);
			continue;
		}
		colon = strchr( val->bv_val, ':' );
		if (!colon || ((colon - val->bv_val)/sizeof(char) > MAX_PATH)) {
			if (!colon) {
				LDAPDebug( LDAP_DEBUG_TRACE,
					"WindowsAuthentication => validation FAILED for \"%s\" on NT Domain : "
					"a colon is missing in ntuserdomainid attr value\n",
					val->bv_val, 0, 0);
			}
			else {
				LDAPDebug( LDAP_DEBUG_TRACE,
					"WindowsAuthentication => validation FAILED for \"%s\" on NT Domain : "
					"domain in ntuserdomainid attr value too long\n",
					val->bv_val, 0, 0);
			}
			i= slapi_attr_next_value(a, i, &sval);
			continue;
		}

		if(( iStatus = GetDomainUsername( val->bv_val, 
										  szNTDomain, 
										  szNTUsername )) != 0) 
		{
			i= slapi_attr_next_value(a, i, &sval);
			continue;
		}

#if !defined( LOGON32_LOGON_NETWORK )
/* This is specified in the WIn32 LogonUser() documentation, but not defined
   in the Visual C++ 4.2 include file winbase.h. A search of the lastest version 
   of this file at www.microsoft.com finds that LOGON32_LOGON_NETWORK == 3.
 */
#define LOGON32_LOGON_NETWORK 3
#endif
		/* Now do the Logon attempt */	
		bLogonStatus = LogonUser( szNTUsername, // string that specifies the user name
								  szNTDomain,   // string that specifies the domain or server
								  cred->bv_val,	// string that specifies the password
								  LOGON32_LOGON_NETWORK, //  the type of logon operation, 
								  LOGON32_PROVIDER_DEFAULT,	 // specifies the logon provider
							      &hToken ); // pointer to variable to receive token handle 
		if( bLogonStatus && hToken ) 
			CloseHandle( hToken );
		
		if( bLogonStatus ) 
		{
			// Successful validation
			LDAPDebug( LDAP_DEBUG_TRACE,
				"WindowsAuthentication => validated \"%s\" on NT Domain \"%s\"\n",
				szNTUsername, szNTDomain, 0 );
			return( LDAPWA_Success );
		}
		else
		{
			LDAPDebug( LDAP_DEBUG_TRACE,
				"WindowsAuthentication => validation FAILED for \"%s\" on NT Domain \"%s\", reason %d\n",
				szNTUsername, szNTDomain, GetLastError() );
			return( LDAPWA_InvalidCredentials );
		}
		i= slapi_attr_next_value(a, i, &sval);
	}
 

	return( LDAPWA_Failure );	

}
#endif

int
ldbm_back_bind( Slapi_PBlock *pb )
{
	backend *be;
	ldbm_instance *inst;
	int			method;
	struct berval		*cred;
	struct ldbminfo		*li;
	struct backentry	*e;
	Slapi_Attr		*attr;
	Slapi_Value **bvals;
	entry_address *addr;

	/* get parameters */
	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &li );
	slapi_pblock_get( pb, SLAPI_TARGET_ADDRESS, &addr );
	slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method );
	slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &cred );
	
	inst = (ldbm_instance *) be->be_instance_info;

	/* always allow noauth simple binds (front end will send the result) */
	if ( method == LDAP_AUTH_SIMPLE && cred->bv_len == 0 ) {
		return( SLAPI_BIND_ANONYMOUS );
	}

	/*
	 * find the target entry.  find_entry() takes care of referrals
	 *   and sending errors if the entry does not exist.
	 */
	if (( e = find_entry( pb, be, addr, NULL /* no txn */ )) == NULL ) {
		return( SLAPI_BIND_FAIL );
	}

	switch ( method ) {
	case LDAP_AUTH_SIMPLE:
		{
		Slapi_Value cv;
		if ( slapi_entry_attr_find( e->ep_entry, "userpassword", &attr ) != 0 ) {
#if defined( XP_WIN32 )
			if( WindowsAuthentication( e, cred ) == LDAPWA_Success ) {
				break;
			}
#endif
			slapi_send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
			    NULL, 0, NULL );
			cache_return( &inst->inst_cache, &e );
			return( SLAPI_BIND_FAIL );
		}
		bvals= attr_get_present_values(attr);
		slapi_value_init_berval(&cv,cred);
		if ( slapi_pw_find_sv( bvals, &cv ) != 0 ) {
#if defined( XP_WIN32 )
			/* One last try - attempt Windows authentication, 
			   if the user has a Windows account. */
			if( WindowsAuthentication( e, cred ) == LDAPWA_Success ) {
				break;
			}
#endif
			slapi_send_ldap_result( pb, LDAP_INVALID_CREDENTIALS, NULL,
			    NULL, 0, NULL );
			cache_return( &inst->inst_cache, &e );
			value_done(&cv);
			return( SLAPI_BIND_FAIL );
		}
		value_done(&cv);
		}
		break;

	default:
		slapi_send_ldap_result( pb, LDAP_STRONG_AUTH_NOT_SUPPORTED, NULL,
		    "auth method not supported", 0, NULL );
		cache_return( &inst->inst_cache, &e );
		return( SLAPI_BIND_FAIL );
	}

	cache_return( &inst->inst_cache, &e );

	/* success:  front end will send result */
	return( SLAPI_BIND_SUCCESS );
}
