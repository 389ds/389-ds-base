/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

/**
 *
 * @author  elliot
 */
public class ParseValue {
    
    /** Creates a new instance of ParseValue */
    public ParseValue() {
    }
    
    public static byte[] parseValueFromNode(org.w3c.dom.Node n) {
        byte[] ret = null;
        // <xsd:union memberTypes="xsd:string xsd:base64Binary xsd:anyURI"/>
        
        org.w3c.dom.Node type = n.getAttributes().getNamedItem("xsi:type");
        if (type != null && type.getNodeValue().equalsIgnoreCase("xsd:base64Binary") ) {
            // This value is encoded in base64. decode it.
            sun.misc.BASE64Decoder bd = new sun.misc.BASE64Decoder();
            try {
                ret = bd.decodeBuffer( n.getFirstChild().getNodeValue() )  ; 
            } 
            catch (org.w3c.dom.DOMException de) {
                ret = "".getBytes();
            }
            catch (Exception e) {
                // couldn't decode auth info
            }
            
        } else {
            // anyURI is unsupported.
            ret = new String(n.getFirstChild().getNodeValue()).getBytes();
            
        }
        return ret;
        
    }
    
    
}
