/* --- BEGIN COPYRIGHT BLOCK ---
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
