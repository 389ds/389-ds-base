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
import netscape.ldap.util.ByteBuf;
import netscape.ldap.util.MimeBase64Decoder;

public class OperationExtended  extends GenericOperation {
    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        
        
        
        LDAPException ldException = null;
        LDAPSearchResults results = null;
        LDAPSearchConstraints  searchConstraint = new LDAPSearchConstraints();
        java.util.Vector modifications = new java.util.Vector(1);
        
        String oid = new String();
        ByteBuf value=new ByteBuf();
        String binaryStr = null;
        org.w3c.dom.NodeList nl = root.getChildNodes();
        for (int i=0; i< nl.getLength(); i++) {
            if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                try {
                    if (nl.item(i).getLocalName().equals("requestName") ) {
                        oid = nl.item(i).getFirstChild().getNodeValue().trim();
                        
                    } else if (nl.item(i).getLocalName().equals("requestValue") ) {
                        
                        // Assuming the it is base64Binary
                        binaryStr =  nl.item(i).getFirstChild().getNodeValue();
                        ByteBuf inputBuf = new ByteBuf(binaryStr);
                        value = new ByteBuf();
                        MimeBase64Decoder decoder = new MimeBase64Decoder();
                        decoder.translate(inputBuf, value);
                        decoder.eof(value);
                        
                    }
                    
                } catch (Exception e) { }
            }
        }
        
        
        LDAPExtendedOperation extendedOperation = new LDAPExtendedOperation(oid, null );
        
        int resultCode= 0;
        String errorMessage = "completed";
        
        javax.xml.soap.SOAPElement output = null;
        
        
        LDAPExtendedOperation result = null;
        try {
            if (ctx.getConstraints() != null)
                result = ldapConn.extendedOperation(extendedOperation,  ctx.getConstraints() );
            else
                result = ldapConn.extendedOperation(extendedOperation);
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage() ;
        }
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("extendedResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            sbe.addChildElement("reponse").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("xsi:type"), "xsd:base64Binary" ).setNodeValue( result.getValue().toString() );
            
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
        
        
        
    }
}