/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 *
 */
package com.netscape.dsml.gateway;

import netscape.ldap.*;
import org.w3c.dom.*;

public class OperationAuth extends GenericOperation {
   
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("authResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer( netscape.ldap.LDAPException.AUTH_METHOD_NOT_SUPPORTED).toString() );
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
    }
    
    
    
    
}
