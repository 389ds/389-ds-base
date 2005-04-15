/* --- BEGIN COPYRIGHT BLOCK ---
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
        
        if (this.ldapConnection  == null || this.ldapConnection.isConnected() == false ) {
            
            IConnectionManager ldap_pool = null;
            try {
                ProxyConnMgrFactory pmc = new ProxyConnMgrFactory();
                ldap_pool = pmc.getInstance();
            }
            catch (Exception e) {
                
            }
            this.ldapConnection = ldap_pool.getConnection();
        }
        
        return this.ldapConnection;
    }
    
    /**
     * Setter for property ldapConnection.
     * @param ldapConnection New value of property ldapConnection.
     */
    public void setLdapConnection(LDAPConnection ldapConnection) {
        // this.ldapConnection = ldapConnection;
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
