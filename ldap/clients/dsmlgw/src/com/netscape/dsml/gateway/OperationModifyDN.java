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

public class OperationModifyDN extends GenericOperation {
    
    org.w3c.dom.Node root = null;
    LDAPConnection ldapConn = null;
    javax.xml.soap.MessageFactory messageFactory = null ;
    javax.xml.soap.SOAPFactory sef = null;
    
    OperationModifyDN(){
        try {
            messageFactory = javax.xml.soap.MessageFactory.newInstance();
            sef = javax.xml.soap.SOAPFactory.newInstance();
        } catch (Exception E) {E.printStackTrace();
        }
    }
    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        LDAPException ldException = null;
        LDAPSearchResults results = null;
        LDAPSearchConstraints  searchConstraint = new LDAPSearchConstraints();
        
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
        String newRDN= root.getAttributes().getNamedItem("newrdn").getNodeValue().trim();
        
        boolean deleteOldRDN= true;
        
        try {
            if (root.getAttributes().getNamedItem("deleteoldrdn") != null)
            deleteOldRDN = new Boolean( root.getAttributes().getNamedItem("deleteoldrdn").getNodeValue().trim()).booleanValue();
        } catch (Exception e) { }
        
        
        // This is unsupported, but it's here for the sake of completion.
        String newSuperior = null;
        try {
	    if (root.getAttributes().getNamedItem("newSuperior") != null)
            newSuperior = root.getAttributes().getNamedItem("newSuperior").getNodeValue().trim();
        } catch (Exception e) { newSuperior = null; }
        
        int resultCode= 0;
        String errorMessage = "completed";
        
        javax.xml.soap.SOAPElement output = null;
        
        
        
        try {
            if (ctx.getConstraints() != null)
                ldapConn.rename(dn, newRDN, newSuperior, deleteOldRDN,ctx.getConstraints() );
            else
                ldapConn.rename(dn, newRDN, newSuperior, deleteOldRDN);
            
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
