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
 * do so, delete this exception statement from your version. 
 * 
 * 
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
