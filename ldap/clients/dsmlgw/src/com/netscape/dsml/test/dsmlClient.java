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
package com.netscape.dsml.test;

import javax.xml.parsers.*;
import org.w3c.dom.*;
import org.w3c.dom.traversal.*;
import org.xml.sax.SAXException;
import java.io.IOException;
import java.io.File;

public class dsmlClient {
    
    
    public dsmlClient() {
    }
    
    
    public static void main(String[] args) {
        File f = null;
        
        if (args.length==2) {
            try {
                
                SOAPClient mySoapClient = new SOAPClient();
                f = File.createTempFile("TET-DSML",null);
                f.deleteOnExit();
                mySoapClient.setIn(  args[1] );
                mySoapClient.setOut( f.getPath() );
                
                mySoapClient.setSOAPUrl( args[0] );
                // mySoapClient.setSOAPUrl("http://desktop3:8180/axis/services/dsmlgw");
                
                mySoapClient.go(); }
            catch (Exception e) {
                e.printStackTrace();
                System.exit(  254  ); // no response or other error like it's not a dsml file
                
            }
            
            
            
            try {
                java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(f));
                String str;
                while ((str = in.readLine()) != null) {
                    System.out.println(str);
                }
                in.close();
            } catch (IOException e) {
            }

            try {
                javax.xml.parsers.DocumentBuilder db=  javax.xml.parsers.DocumentBuilderFactory.newInstance().newDocumentBuilder();
                org.w3c.dom.Document doc = db.parse( f );
                
                // Create the NodeIterator
                DocumentTraversal traversable = (DocumentTraversal) doc;
                NodeIterator iterator = traversable.createNodeIterator(doc, NodeFilter.SHOW_ALL , null, true);
                
                // Iterate over the comments
                Node node;
                while ((node = iterator.nextNode()) != null) {
                    if (node.getNodeName().equals("resultCode")) {
                        int result = Integer.parseInt(node.getAttributes().getNamedItem("code").getNodeValue());
                        System.exit(result);
                    } else if (node.getNodeName().equals("errorResponse ")) {
                        int result = Integer.parseInt(node.getAttributes().getNamedItem("code").getNodeValue());
                        System.exit(result);
                    } else if (node.getNodeName().equals("faultcode")) {
                          System.exit(253);
                    }
                }
                
            }
            catch (javax.xml.parsers.ParserConfigurationException pce ) {
                pce.printStackTrace();
                System.exit(  254  ); }
            catch (org.xml.sax.SAXException saxe) {
                saxe.printStackTrace();
                System.exit(  254  ); } // ?
            catch (java.io.IOException ioe) {
                ioe.printStackTrace();
                System.exit(  254  ); }   // bad url
            catch (Exception e) {
                e.printStackTrace();
                System.exit(  254  ); }   // bad url
            
        }
        else {
            System.err.println("wrong number of arguments");
            System.err.println("usage:  dsmlClient soap-url dsml-file");
            System.exit(255); // wrong arguments
        }
    }
    
}
