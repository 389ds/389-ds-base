/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
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
