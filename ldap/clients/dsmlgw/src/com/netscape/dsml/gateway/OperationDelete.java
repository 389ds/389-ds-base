/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import netscape.ldap.*;
import org.w3c.dom.*;


public class OperationDelete extends GenericOperation {

    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
          
        int resultCode= 0;
        String errorMessage = "completed";
        
        javax.xml.soap.SOAPElement output = null;
        
        
        
        try {
            if (ctx.getConstraints() != null)
                ldapConn.delete(dn,  ctx.getConstraints() );
            else
                ldapConn.delete(dn);
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage() ;
        }
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("deleteResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            if (errorMessage != null)
                sbe.addChildElement("errorMessage").addTextNode(errorMessage);
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
        
        
    }
    
}
