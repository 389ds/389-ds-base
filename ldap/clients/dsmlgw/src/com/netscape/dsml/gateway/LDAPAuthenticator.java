/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 *
 */
package com.netscape.dsml.gateway;

import netscape.ldap.*;
/**
 *
 * @author  elliot
 */
public class LDAPAuthenticator {
    
    String DN = null;
    String PW = null;
    
    
    /** Creates a new instance of LDAPAuthenticator */
    public LDAPAuthenticator(String username, String password) {
        DN = username;
        PW = password;
    }
    
   public int  authenticate() {
       
       if (DN == null && PW == null)
           return 0;
      
        LDAPConnection ldc = null;
        
        ProxyConnMgrFactory pmc = new ProxyConnMgrFactory();
        IConnectionManager ldap_pool= pmc.getInstance();
        
        ldc = ldap_pool.getLoginConnection();
     
        if (ldc != null) {
            try {
                ldc.authenticate( DN, PW );
            } catch ( LDAPException e ) {
                switch( e.getLDAPResultCode() ) {
                    case LDAPException.NO_SUCH_OBJECT:
                        // System.out.println( "The specified user does not exist." );
                        return LDAPException.NO_SUCH_OBJECT;
                        
                    case LDAPException.INVALID_CREDENTIALS:
                        // System.out.println( "Invalid password." );
                        return LDAPException.INVALID_CREDENTIALS;
                        
                    default:
                        // System.out.println( "Error number: " + e.getLDAPResultCode() );
                        // System.out.println( "Failed to authentice as " + DN );
                        return e.getLDAPResultCode() ;
                        
                }
            }
            // System.out.println( "Authenticated as " + DN );
            return 0;
        } else {
            
            // System.out.println( "Can't establish connection to LDAP server");
            return LDAPException.UNAVAILABLE ;
        }
   }
}