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

import org.w3c.dom.*;
import java.util.logging.*;

public class ParseControl {
    
    public static netscape.ldap.LDAPControl parseControlFromNode(org.w3c.dom.Node n) {
        Logger logger = Logger.getLogger("com.netscape.dsml.gateway.ParseControl");
        String type = null;
        byte[] value = null;
        boolean criticality = false;
        
        
        try {
            type = n.getAttributes().getNamedItem("type").getNodeValue();
        } catch (Exception e) {
            //      throw new gatewayException("control type can not be omitted");
        }
        
        try {
            criticality = Boolean.valueOf(  n.getAttributes().getNamedItem("criticality").getNodeValue()  ).booleanValue();
        } catch (Exception e) { /* ignore */  }
        
        
        netscape.ldap.LDAPControl lc = null;
        if (type.equals( netscape.ldap.controls.LDAPSortControl.SORTREQUEST ) ) {
            try {
                java.util.Vector ldskv = new java.util.Vector();
                
                org.w3c.dom.NodeList nl = n.getFirstChild().getNextSibling().getChildNodes();
                for (int i=0; i< nl.getLength(); i++) {
                    if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                        
                        if (nl.item(i).getLocalName().equals("attr") ) {
                            
                            // should check to make sure there's only one child to this filter
                            
                            Node mod = nl.item(i);
                            
                            String attribute = mod.getAttributes().getNamedItem("name").getNodeValue().trim();
                            boolean reverse = false;
                            
                            try {
                                reverse = new Boolean( mod.getAttributes().getNamedItem("reverse").getNodeValue().trim()).booleanValue();
                            } catch (Exception e) {}
                            
                            netscape.ldap.LDAPSortKey ldsk = new netscape.ldap.LDAPSortKey(attribute, reverse);
                            logger.log(Level.FINE, "SSS: reverse: {0}", String.valueOf(reverse));
                            logger.log(Level.FINE, "SSS: attribute: {0}",  attribute );
                            
                            ldskv.add(ldsk);
                            
                        }
                    }
                }
                
                netscape.ldap.LDAPSortKey sortkeys[] = new netscape.ldap.LDAPSortKey[ ldskv.size() ];
                for (int j=0; j< ldskv.size(); j++)
                    sortkeys[j] = (netscape.ldap.LDAPSortKey)  ldskv.get(j);
                lc = new netscape.ldap.controls.LDAPSortControl(sortkeys, criticality);
                
            } catch (Exception e)
            { e.printStackTrace(); }
            
            
        } else if (type.equals( netscape.ldap.controls.LDAPVirtualListControl.VIRTUALLIST ) ) {
            //lc = new netscape.ldap.controls.LDAPVirtualListControl(,);
            int  index = 0;
            int before = 0;
            int after  = 0;
            int content  = 0;
            java.util.Vector ldskv = new java.util.Vector();
            
            org.w3c.dom.NodeList nl = n.getFirstChild().getNextSibling().getChildNodes();
            for (int i=0; i< nl.getLength(); i++) {
                if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                    
                    if (nl.item(i).getLocalName().equals("index") ) {
                        try {
                            index = Integer.parseInt( nl.item(i).getFirstChild().getNodeValue() );
                        } catch (Exception e) {}
                        
                    } else if  (nl.item(i).getLocalName().equals("before") ) {
                        try {
                            before = Integer.parseInt( nl.item(i).getFirstChild().getNodeValue() );
                        } catch (Exception e) {}
                        
                    } else if  (nl.item(i).getLocalName().equals("after") ) {
                        try {
                            after = Integer.parseInt( nl.item(i).getFirstChild().getNodeValue() );
                        } catch (Exception e) {}
                    } else if  (nl.item(i).getLocalName().equals("count") ) {
                        try {
                            content = Integer.parseInt( nl.item(i).getFirstChild().getNodeValue() );
                        } catch (Exception e) {}
                        
                        
                        
                    }
                }
            }
            
            logger.log(Level.FINE, "VLV:   index: {0}", String.valueOf(index));
            logger.log(Level.FINE, "VLV:  before: {0}", String.valueOf(before));
            logger.log(Level.FINE, "VLV:   after: {0}", String.valueOf(after));
            logger.log(Level.FINE, "VLV: content: {0}", String.valueOf(content));
            lc =  new netscape.ldap.controls.LDAPVirtualListControl( index, before, after, content );
            
        } else {
            if (criticality) {
                // throw new gatewayException("unrecognized control oid");
            } else {
                // do nothing
            }
            
        }
        
        
        
        return lc;
    }
    
}
