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

public class OperationCompare extends GenericOperation {
    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
        
        LDAPAttribute attr = null;
        org.w3c.dom.NodeList nl = root.getChildNodes();
        for (int i=0; i< nl.getLength(); i++) {
            if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                try {
                    if (nl.item(i).getLocalName().equals("assertion") ) {
                        
                        String field = nl.item(i).getAttributes().getNamedItem("name").getNodeValue();
                        attr = new LDAPAttribute(field);
                        
                        NodeList Values = nl.item(i).getChildNodes();
                        
                        for (int j=0; j< Values.getLength(); j++) {
                            if (Values.item(j).getNodeType() == Node.ELEMENT_NODE &&
                            Values.item(j).getLocalName().equals("value")) {
                                attr.addValue(Values.item(j).getFirstChild().getNodeValue());
                                
                            }
                            
                        }
                    }
                } catch (Exception e) { }
            }
        }
         
        boolean result;
        int resultCode= 0;
        String errorMessage = "completed";
        
        javax.xml.soap.SOAPElement output = null;
        LDAPAttribute attribute=null;
        
        
        
        try {
            if (ctx.getConstraints() != null)
                result = ldapConn.compare(dn, attr, ctx.getConstraints() );
            else
                result = ldapConn.compare(dn, attr);
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage() ;
        }
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("modDNResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            if (errorMessage != null)
                sbe.addChildElement("errorMessage").addTextNode(errorMessage);
            
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
        
    }
}
