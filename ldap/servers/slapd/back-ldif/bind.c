/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: bind.c
 *
 *  Functions:
 * 
 *      ldif_back_bind() - ldap ldif back-end bind routine
 *
 */

#include "back-ldif.h"

/*
 *  Function: ldif_back_bind
 *
 *  Returns: returns 0|1 if good, -1 else.
 *  
 *  Description: performs an ldap bind.
 */
int
ldif_back_bind( Slapi_PBlock *pb )
{
  char			*dn;        /*Storage for the dn*/
  int			method;     /*Storage for the bind method*/
  struct berval		*cred;      /*Storage for the bind credentials*/
  struct berval		**bvals; 
  LDIF  		*db;        /*The database*/
  ldif_Entry            *e, *prev;  /*Used for searching the db*/
  int			rc, syntax; /*Storage for error return values*/
  Slapi_Attr		*attr;

  LDAPDebug( LDAP_DEBUG_TRACE, "=> ldif_back_bind\n", 0, 0, 0 );

  prev = NULL;
  
  /*Get the parameters*/
  if (slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &db ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_TARGET, &dn ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method ) < 0 ||
      slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &cred ) < 0){
    slapi_send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL );
    return(-1);
  }

  /*Lock the database*/
  PR_Lock( db->ldif_lock );


  /*Find the entry that the person is attempting to bind as*/
  if ( (e = (ldif_Entry *)ldif_find_entry( pb, db, dn, &prev )) == NULL ) {

    /* Allow noauth binds */
    if ( method == LDAP_AUTH_SIMPLE && cred->bv_len == 0 ) {
      rc = SLAPI_BIND_ANONYMOUS;
    } else {
      slapi_send_ldap_result( pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL );
      rc = SLAPI_BIND_FAIL;
    }

    /*Unlock the database*/
    PR_Unlock( db->ldif_lock );
    
    return( rc );
  }
  
  switch ( method ) {
  case LDAP_AUTH_SIMPLE:
    if ( cred->bv_len == 0 ) {
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_ANONYMOUS );
    }
    
    if ( slapi_entry_attr_find( e->lde_e, "userpassword", &attr ) != 0 ) {
      slapi_send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL, NULL, 0, NULL );
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_FAIL );
    }
	/*
	 * XXXmcs: slapi_attr_get_values() is deprecated and should be avoided
	 * See XXXmcs comments in ../attr.c for detailed information.
	 */
    slapi_attr_get_values( attr, &bvals );
    
    if ( slapi_pw_find( bvals, cred ) != 0 ) {
      slapi_send_ldap_result( pb, LDAP_INVALID_CREDENTIALS, NULL, NULL, 0, NULL );
      PR_Unlock( db->ldif_lock );
      return( SLAPI_BIND_FAIL );
    }
    break;
    
  default:
    slapi_send_ldap_result( pb, LDAP_STRONG_AUTH_NOT_SUPPORTED, NULL,
		     "auth method not supported", 0, NULL );
    PR_Unlock( db->ldif_lock );
    return( SLAPI_BIND_FAIL );
  }
  
  PR_Unlock( db->ldif_lock );  

  LDAPDebug( LDAP_DEBUG_TRACE, "<= ldif_back_bind\n", 0, 0, 0 );  

  /* success:  front end will send result */
  return( SLAPI_BIND_SUCCESS );
}
