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
 *
 *  This {interface|class} is the standard request. Specific requests / reponses
 * inherit from this.
 */
class GenericOperation {
    org.w3c.dom.Node root = null;
    LDAPConnection ldapConn = null;
    javax.xml.soap.MessageFactory messageFactory = null ;
    javax.xml.soap.SOAPFactory sef = null;
    
    /** Creates a new instance of OperationCompare */
    public GenericOperation() {
        try {
            messageFactory = javax.xml.soap.MessageFactory.newInstance();
            sef = javax.xml.soap.SOAPFactory.newInstance();
        } catch (Exception E) {E.printStackTrace();}
    }
    
        
    public void setRoot(org.w3c.dom.Node op) {
        if (root == null)
            root = op;
        
    }
    
    public void setLDAPConnection(LDAPConnection lc) {
        if (ldapConn == null)
            ldapConn = lc;
    }
    // abstract javax.xml.soap.SOAPElement getResponse(gatewayContext ctx);
}
