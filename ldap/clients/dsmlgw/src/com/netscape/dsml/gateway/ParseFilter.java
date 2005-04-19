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
