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
                    byte[] attrValue;
                    
                    if (nl.item(i).getFirstChild().getNodeType() == Node.ELEMENT_NODE)
                        attrValue = ParseValue.parseValueFromNode( nl.item(i).getFirstChild()  );
                    else
                        attrValue = ParseValue.parseValueFromNode(  nl.item(i).getFirstChild().getNextSibling() );
                        
                    
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
