/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import netscape.ldap.*;
import org.w3c.dom.*;
import java.util.logging.*;

public class OperationModify extends GenericOperation {    
    private static Logger logger = Logger.getLogger("com.netscape.dsml.gateway.OperationModify");
    org.w3c.dom.Node root = null;
    LDAPConnection ldapConn = null;
    javax.xml.soap.MessageFactory messageFactory = null ;
    javax.xml.soap.SOAPFactory sef = null;
    
    OperationModify(){
        try {
            messageFactory = javax.xml.soap.MessageFactory.newInstance();
            sef = javax.xml.soap.SOAPFactory.newInstance();
        } catch (Exception E) {E.printStackTrace();
        }
    }
    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
        ldapConn = ctx.getLdapConnection();
        root = ctx.getRootNode();
        
        
        LDAPException ldException = null;
        LDAPSearchResults results = null;
        LDAPSearchConstraints  searchConstraint = new LDAPSearchConstraints();
        java.util.Vector modifications = new java.util.Vector(1);
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
        
        org.w3c.dom.NodeList nl = root.getChildNodes();
        for (int i=0; i< nl.getLength(); i++) {
            if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                try {
                    if (nl.item(i).getLocalName().equals("modification") ) {
                        
                        // should check to make sure there's only one child to this filter
                        
                        Node mod = nl.item(i);
                        
                        String modify_field = mod.getAttributes().getNamedItem("name").getNodeValue().trim();
                        String modify_op    = mod.getAttributes().getNamedItem("operation").getNodeValue();
                        
                        logger.log(Level.INFO, "modify_field: {0}", modify_field);
                        logger.log(Level.INFO, "modify_op: {0}", modify_op);
                        
                        int op = -1;
                        LDAPAttribute attr = new LDAPAttribute(modify_field);
                        
                        NodeList Values = mod.getChildNodes();
                        
                        for (int j=0; j< Values.getLength(); j++) {
                            if (Values.item(j).getNodeType() == Node.ELEMENT_NODE &&
                            Values.item(j).getLocalName().equals("value")) {
                                attr.addValue(Values.item(j).getFirstChild().getNodeValue());
                                
                            }
                            
                        }
                        
                        
                        
                        if (modify_op.equals("replace")) {
                            op =LDAPModification.REPLACE;
                        } else if (modify_op.equals("delete")) {
                            op =LDAPModification.DELETE;
                        } else if (modify_op.equals("add")) {
                            op =LDAPModification.ADD;
                        }
                        
                        modifications.add(new LDAPModification(op, attr));
                        
                    }
                    
                    
                    
                }
                catch (Exception e) { e.printStackTrace();}
                
            }
        }
        
        LDAPModification[] lm = new LDAPModification[modifications.size()];
        for (int i=0; i< modifications.size(); i++)
            lm[i] = (LDAPModification) modifications.get(i);
        
        int resultCode= 0;
        String errorMessage = "completed";
        
        javax.xml.soap.SOAPElement output = null;
        
        
        
        try {
            if (ctx.getConstraints() != null)
                ldapConn.modify(dn, lm, ctx.getConstraints() );
            else
                ldapConn.modify(dn, lm);
            
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage() ;
        }
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("modifyResponse") );
            sbe.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            if (errorMessage != null)
                sbe.addChildElement("errorMessage").addTextNode(errorMessage);
            
            
        } catch (Exception E) {
            E.printStackTrace();
        }
        
        return sbe;
        
        
    }
    
    
    
}
