/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.test;


import java.util.Properties;
import java.io.*;
import java.net.*;
import java.io.FileInputStream;
import javax.xml.transform.stream.StreamSource;
import javax.xml.soap.*;
/**
 *
 * @author  elliot
 */
public class SOAPClient {
    
    private String in = null;
    private String out = null;
    private String SOAPUrl = null;
    
    public SOAPClient() { }
    
    public void setIn(String in) {
        this.in = in;
    }
    
    public void setOut(String out) {
        this.out = out;
    }
    
    public void setSOAPUrl(String SOAPUrl) {
        this.SOAPUrl = SOAPUrl;
    }
    
    public void go() throws Exception {
        
        String SOAPAction = "";
        
        URL url = new URL(SOAPUrl);
        URLConnection connection = url.openConnection();
        HttpURLConnection httpConn = (HttpURLConnection) connection;
        
        FileInputStream fin = new FileInputStream(in);
        FileOutputStream fout = new FileOutputStream(out);
         
        SOAPMessage message = javax.xml.soap.MessageFactory.newInstance().createMessage();
        
        SOAPPart soapPart = message.getSOAPPart();
        
        StreamSource preppedMsgSrc = new StreamSource(new FileInputStream(in));
        
        soapPart.setContent(preppedMsgSrc);
        message.saveChanges();
        
        SOAPConnection con = SOAPConnectionFactory.newInstance().createConnection();
        
        SOAPMessage response = con.call(message, url);
        
        response.writeTo(fout);
        
        fout.close();
        fin.close();
        con.close();
        
    }
    
}
