/* --- BEGIN COPYRIGHT BLOCK ---
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
