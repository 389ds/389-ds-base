/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import netscape.ldap.*;
import org.w3c.dom.*;


/**
 *
 * @author  elliot
 */
public class gatewayContext {
    

    /**
     * Holds value of property ldapConnection.
     */
    private LDAPConnection ldapConnection;
    
    /**
     * Holds value of property constraints.
     */
    private netscape.ldap.LDAPSearchConstraints constraints;
    
    /**
     * Holds value of property rootNode.
     */
    private org.w3c.dom.Node rootNode;
    
    /**
     * Getter for property ldapConnection.
     * @return Value of property ldapConnection.
     */
    public LDAPConnection getLdapConnection() {
        return this.ldapConnection;
    }
    
    /**
     * Setter for property ldapConnection.
     * @param ldapConnection New value of property ldapConnection.
     */
    public void setLdapConnection(LDAPConnection ldapConnection) {
        this.ldapConnection = ldapConnection;
    }
    
    /**
     * Getter for property constraints.
     * @return Value of property constraints.
     */
    public netscape.ldap.LDAPSearchConstraints getConstraints() {
        return this.constraints;
    }
    
    /**
     * Setter for property constraints.
     * @param constraints New value of property constraints.
     */
    public void setConstraints(netscape.ldap.LDAPSearchConstraints constraints) {
        this.constraints = constraints;
    }
    
    /**
     * Getter for property rootNode.
     * @return Value of property rootNode.
     */
    public org.w3c.dom.Node getRootNode() {
        return this.rootNode;
    }
    
    /**
     * Setter for property rootNode.
     * @param rootNode New value of property rootNode.
     */
    public void setRootNode(org.w3c.dom.Node rootNode) {
        this.rootNode = rootNode;
    }
    
}
