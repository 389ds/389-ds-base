/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Requests an extended operation with the OID 1.2.3.4.
 * Use this client in conjunction with a server that can process
 * this extended operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ldap.h>

/* Name and port of the LDAP server you want to connect to. */
#define MY_HOST "localhost"
#define MY_PORT	389

/* DN of user (and password of user) who you want to authenticate as */
#define MGR_DN	"cn=Directory Manager"
#define MGR_PW	"23skidoo"

int
main( int argc, char **argv )
{

    /* OID of the extended operation that you are requesting */
    const char *oidrequest = "1.2.3.4";
    char *oidresult;
    struct berval valrequest;
    struct berval *valresult;
    LDAP *ld;
    int rc, version;

    /* Set up the value that you want to pass to the server */
    printf( "Setting up value to pass to server...\n" );
    valrequest.bv_val = "My Value";
    valrequest.bv_len = strlen( "My Value" );

    /* Get a handle to an LDAP connection */
    printf( "Getting the handle to the LDAP connection...\n" );
    if ( (ld = ldap_init( MY_HOST, MY_PORT )) == NULL ) {
	perror( "ldap_init" );
	ldap_unbind( ld );
	return( 1 );
    }

    /* Set the LDAP protocol version supported by the client
       to 3. (By default, this is set to 2. Extended operations
       are part of version 3 of the LDAP protocol.) */
    ldap_get_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version );
    printf( "Resetting version %d to 3.0...\n", version );
    version = LDAP_VERSION3;
    ldap_set_option( ld, LDAP_OPT_PROTOCOL_VERSION, &version );

    /* Authenticate to the directory as the Directory Manager */
    printf( "Binding to the directory...\n" );
    if ( ldap_simple_bind_s( ld, MGR_DN, MGR_PW ) != LDAP_SUCCESS ) {
	ldap_perror( ld, "ldap_simple_bind_s" );
	ldap_unbind( ld );
	return( 1 );
    }

    /* Initiate the extended operation */
    printf( "Initiating the extended operation...\n" );
    if ( ( rc = ldap_extended_operation_s( ld, oidrequest, &valrequest, NULL, NULL, &oidresult, &valresult ) ) != LDAP_SUCCESS ) {
	ldap_perror( ld, "ldap_extended failed: " );
	ldap_unbind( ld );
	return( 1 );
    } 

    /* Get the OID and the value from the result returned by the server. */
    printf( "Operation successful.\n" );
    printf( "\tReturned OID: %s\n", oidresult );
    printf( "\tReturned value: %s\n", valresult->bv_val );

    /* Disconnect from the server. */
    ldap_unbind( ld );
    return 0;
}

