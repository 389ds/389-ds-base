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
import java.io.BufferedReader;
import java.io.FileReader;

public class dsmlSearch {
    
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
                
                mySoapClient.go(); }
            catch (Exception e) {
                e.printStackTrace();
                System.exit(  254  ); // no response or other error like it's not a dsml file
                
            }
            
            try {
                BufferedReader in = new BufferedReader(new FileReader(  f.getPath() ));
                String str;
                while (( str = in.readLine()) != null) {
                  
                    System.out.println(str); }
                in.close(); }
            catch (IOException e) {
                e.printStackTrace();
                System.exit(  254  );
            }
            
        }
        else {
           System.err.println("wrong number of arguments");
            System.err.println("usage:  dsmlSearch soap-url dsml-file");
            System.exit(255); // wrong arguments
        }
    }
    
}
