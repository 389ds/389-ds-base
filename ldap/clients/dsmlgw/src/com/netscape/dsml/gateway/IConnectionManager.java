/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 *
 */
package com.netscape.dsml.gateway;

import java.io.IOException;
import netscape.ldap.LDAPConnection;

public interface IConnectionManager {
    
    
    public void shutdown();
    public LDAPConnection getConnection();
    public LDAPConnection getLoginConnection();
    public void releaseConnection(LDAPConnection ld);
    /**
     * release the connection
     *
     * @param the login ctx
     * @param the LDAP connection
     */
    public void releaseConnection(String loginCtx, LDAPConnection ld);
    public LDAPConnection getConnection(String loginCtx);
    
    
}