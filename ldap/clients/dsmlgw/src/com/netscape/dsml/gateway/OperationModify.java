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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
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
                                attr.addValue( ParseValue.parseValueFromNode(Values.item(j)) );
                                
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
        
           ldapConn = ctx.getLdapConnection();
        
        try {
            if (ctx.getConstraints() != null)
                ldapConn.modify(dn, lm, ctx.getConstraints() );
            else
                ldapConn.modify(dn, lm);
            
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = E.getLDAPErrorMessage() ;
	        if (! ldapConn.isConnected()) {
	        	errorMessage = "GATEWAY NOT CONNECTED";
	        }
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
