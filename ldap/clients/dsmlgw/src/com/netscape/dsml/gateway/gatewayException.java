/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 *
 */

package com.netscape.dsml.gateway;

public class gatewayException extends Exception {
    
    /** Creates a new instance of gatewayException */
    public gatewayException(String message) {
        super(message);
    }
    
     public gatewayException() {
        super("LDAP Server Unavailable");
    }
    
    public Throwable getCause() {
        Throwable retValue;
        
        retValue = super.getCause();
        return retValue;
    }
    
    public String getMessage() {
        String retValue;
        
        retValue = super.getMessage();
        return retValue;
    }
    
}
