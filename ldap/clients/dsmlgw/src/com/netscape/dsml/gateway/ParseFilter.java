/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 *
 *
 */
package com.netscape.dsml.gateway;

import org.w3c.dom.*;
import java.util.logging.*;

public class ParseFilter {
    
    public static String parseFilterFromNode(org.w3c.dom.Node n) {
        Logger logger = Logger.getLogger("com.netscape.dsml.gateway.FilterProcess");
        StringBuffer sb = new java.lang.StringBuffer();
        String output = new String();
        
        if ( n != null && n.getNodeType() != Node.TEXT_NODE) {
            String NodeName = n.getLocalName();
            
            if ( NodeName.equals("present") ) {
                logger.log(Level.ALL, "got filter: present");
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue());
                sb.append("=*");
                
            } else if ( NodeName.equals("and") ) {
                logger.log(Level.FINE, "got filter: and");
                Node it = n.getFirstChild();
                
                sb.append('&');
                
                while (it != null ) {
                    if (it.getNodeType() != Node.TEXT_NODE)
                        sb.append( parseFilterFromNode( it ) );
                    it = it.getNextSibling();
                }
                
            } else if ( NodeName.equals("or") ) {
                logger.log(Level.FINE, "got filter: or");
                Node it = n.getFirstChild();
                
                sb.append('|');
                
                while (it != null ) {
                    if (it.getNodeType() != Node.TEXT_NODE)
                        sb.append( parseFilterFromNode( it ) );
                    it = it.getNextSibling();
                }
                
            } else if ( NodeName.equals("not") ) {
                logger.log(Level.FINE, "got filter: not");
                Node  it = n.getFirstChild();
                
                sb.append('!');
                
                if (it.getNodeType() == Node.TEXT_NODE)
                    it = it.getNextSibling();
                
                sb.append( parseFilterFromNode(it) );
                
            } else if ( NodeName.equals("greaterOrEqual") ) {
                logger.log(Level.FINE, "got filter: greaterOrEqual");
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue());
                sb.append(">=");
                
                Node  it = n.getFirstChild();
                if (it.getNodeType() == Node.TEXT_NODE)
                    it = it.getNextSibling();
                
                sb.append(it.getFirstChild().getNodeValue());
                
                // if (n.getFirstChild().getNextSibling().getFirstChild().getNodeValue() != null)
                //                    sb.append(it.getFirstChild().getNodeValue());
                //  else
                //   sb.append( n.getFirstChild().getFirstChild().getNodeValue());
                
            } else if ( NodeName.equals("lessOrEqual") ) {
                logger.log(Level.FINE, "got filter: lessOrEqual");
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue());
                sb.append("<=");
                Node  it = n.getFirstChild();
                if (it.getNodeType() == Node.TEXT_NODE)
                    it = it.getNextSibling();
                
                sb.append(it.getFirstChild().getNodeValue());
                
            } else if ( NodeName.equals("equalityMatch") ) {
                logger.log(Level.FINE, "got filter: equalityMatch");
                logger.log(Level.FINER," eq: " + n.getAttributes().getNamedItem("name").getNodeValue());
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue() );
                logger.log(Level.FINER," eq: = ");
                sb.append('=');
                Node  it = n.getFirstChild();
                if (it.getNodeType() == Node.TEXT_NODE)
                    it = it.getNextSibling();
                
                sb.append(it.getFirstChild().getNodeValue());
                
                
            } else if ( NodeName.equals("approxMatch") ) {
                logger.log(Level.FINE, "got filter: approxMatch");
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue() );
                sb.append("~=");
                Node  it = n.getFirstChild();
                if (it.getNodeType() == Node.TEXT_NODE)
                    it = it.getNextSibling();
                
                sb.append(it.getFirstChild().getNodeValue());
                
                
            } else if ( NodeName.equals("substrings") ) {
                logger.log(Level.FINE, "got filter: substrings");
                
                Node it = n.getFirstChild();
                
                sb.append(n.getAttributes().getNamedItem("name").getNodeValue());
                sb.append('=');
                
                boolean noStar = false;
                
                
                while (it != null) {
                    
                    if (it.getNodeType() != Node.TEXT_NODE){
                        if (it.getLocalName().equals("any")){
                            if ( ! noStar )
                                sb.append('*');
                            noStar = true;
                            sb.append(it.getFirstChild().getNodeValue());
                            sb.append('*');
                        } else if (it.getLocalName().equals("initial")){
                            noStar = true;
                            sb.append(it.getFirstChild().getNodeValue());
                            sb.append('*');
                        } if (it.getLocalName().equals("final")){
                            if (! noStar)
                                sb.append('*');
                            sb.append(it.getFirstChild().getNodeValue());
                        }
                    }
                    it=it.getNextSibling();
                }
            } else return new String();
            
            
        }
        logger.log(Level.INFO, "returning: (" + sb.toString() + ")" );
        return new StringBuffer().append('(').append(sb).append(')').toString();
    }
    
}
