/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * 
 * Requests an extended operation with the OID 1.2.3.4.
 * Use this client in conjunction with a server that can process
 * this extended operation.
 *
 */

import netscape.ldap.*;
import java.util.*;
import java.io.*;

public class ReqExtOp {
    public static void main( String[] args )
	{
		LDAPConnection ld = null;
		int status = -1;
		try {
			ld = new LDAPConnection();

			/* Connect to server */
			String MY_HOST = "localhost";
			int MY_PORT = 389;
			ld.connect( MY_HOST, MY_PORT );
			System.out.println( "Connected to server." );

			/* Authenticate to the server as directory manager */
			String MGR_DN = "cn=Directory Manager";
			String MGR_PW = "23skidoo";
			if ( ld.LDAP_VERSION < 3 ) {
				ld.authenticate( 3, MGR_DN, MGR_PW );
			} else {
				System.out.println( "Specified LDAP server does not support v3 of the LDAP protocol." );
				ld.disconnect();
				System.exit(1);
			}
			System.out.println( "Authenticated to directory." );

			/* Create an extended operation object */
			String myval = "My Value";
			byte vals[] = myval.getBytes( "UTF8" );
			LDAPExtendedOperation exop = new LDAPExtendedOperation( "1.2.3.4", vals );
			System.out.println( "Created LDAPExtendedOperation object." );

			/* Request the extended operation from the server. */
			LDAPExtendedOperation exres = ld.extendedOperation( exop );

			System.out.println( "Performed extended operation." );

			/* Get data from the response sent by the server. */
			System.out.println( "OID: " + exres.getID() );
			String retValue = new String( exres.getValue(), "UTF8" );
			System.out.println( "Value: " + retValue );
		}
		catch( LDAPException e ) {
			System.out.println( "Error: " + e.toString() );
		}
		catch( UnsupportedEncodingException e ) {
			System.out.println( "Error: UTF8 not supported" );
		}

		/* Done, so disconnect */
		if ( (ld != null) && ld.isConnected() ) {
			try {
			    ld.disconnect();
			} catch ( LDAPException e ) {
				System.out.println( "Error: " + e.toString() );
			}
		}
		System.exit(status);
	}
}
