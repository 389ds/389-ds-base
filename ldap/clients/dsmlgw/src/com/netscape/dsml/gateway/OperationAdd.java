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

public class OperationAdd extends GenericOperation {
   
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        LDAPAttributeSet ldAttrSet = new LDAPAttributeSet();
        // XXX is this really necessary?
        // if (ldAttrSet == null ) {
        //            throw new Exception("MEMORY_ALLOCATION_ERROR");
        //        }
        
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
        
        org.w3c.dom.NodeList nl = root.getChildNodes();
        for (int i=0; i< nl.getLength(); i++) {
            
            if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                try {
                    Node attr = nl.item(i).getFirstChild();
                    if (attr.getNodeType() == Node.TEXT_NODE)
                        attr = attr.getNextSibling();
                      
                    String attrName = nl.item(i).getAttributes().getNamedItem("name").getNodeValue();
                    String attrValue;
                    
                    if (attr.getNodeType() == Node.ELEMENT_NODE)
                        attrValue=attr.getFirstChild().getNodeValue();
                    else
                        attrValue=attr.getNextSibling().getFirstChild().getNodeValue();
                    
                    LDAPAttribute ldapAttr = new LDAPAttribute( attrName, attrValue);
    
                        if (ldapAttr != null ) {
                            ldAttrSet.add(ldapAttr);
                        }
                        
                    
                    
                }
                catch (Exception e) { e.printStackTrace();}
            }
            
        }
        
        int resultCode= 0;
        String errorMessage = "completed";
        LDAPEntry entry = new LDAPEntry(dn,ldAttrSet);
        javax.xml.soap.SOAPElement output = null;
        
        try {
            
            
            
            
            if (ctx.getConstraints() != null) {
                ldapConn.add(entry, ctx.getConstraints() ); }
            else {
                ldapConn.add( entry ); }
            
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage();
            
            
        }
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("addResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            if (errorMessage != null)
                sbe.addChildElement("errorMessage").addTextNode(errorMessage);
            
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
    }
    
}
