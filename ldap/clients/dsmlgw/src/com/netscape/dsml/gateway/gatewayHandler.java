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

import java.io.*;
import java.util.Iterator;
import javax.xml.namespace.QName;
import javax.xml.parsers.*;
import javax.xml.rpc.handler.Handler;
import javax.xml.rpc.handler.HandlerInfo;
import javax.xml.rpc.handler.soap.SOAPMessageContext;
import javax.xml.soap.Name;
import javax.xml.soap.SOAPEnvelope;
import javax.xml.soap.SOAPHeader;
import javax.xml.soap.SOAPHeaderElement;
import javax.xml.soap.SOAPMessage;
import javax.xml.soap.SOAPPart;
import javax.xml.soap.SOAPConstants;
import javax.xml.transform.*;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;
import org.w3c.dom.Document;
import org.xml.sax.*;
import org.w3c.dom.*;
import javax.xml.soap.*;
import org.apache.axis.AxisFault;
import org.apache.axis.Message;
import org.apache.axis.MessageContext;
import org.apache.axis.handlers.BasicHandler;

public class gatewayHandler  extends BasicHandler {
    private HandlerInfo handlerInfo;
    static private javax.xml.soap.MessageFactory messageFactory ;
    static private javax.xml.soap.SOAPFactory sef ;
    private static boolean ready = false;
    
    public gatewayHandler() {
        super();
        
        try {
            messageFactory = javax.xml.soap.MessageFactory.newInstance();
            sef = javax.xml.soap.SOAPFactory.newInstance();
        } catch (Exception e) { }
    }
    
    
    public void invoke(MessageContext context) throws AxisFault {
	    if (context.getPastPivot() == false) {
	    	handleRequest(context);
	    }
	 }
    
    public void cleanup() {
        super.cleanup();
    }

	 
    public boolean handleRequest(MessageContext context) {
    /*
     * this section will set user, pwd, if it came via a http authentication header
     *
     */
	Configuration config = Configuration.getInstance();
        String tmp = (String)context.getProperty("Authorization");
        
        String user=null ;
        String pwd =null;
        if ( tmp != null )
            tmp = tmp.trim();
        if ( tmp != null && tmp.startsWith("Basic ") ) {
            
            int  i ;
            sun.misc.BASE64Decoder bd = new sun.misc.BASE64Decoder();
            try {
                tmp = new String( (bd.decodeBuffer(tmp.substring(6)  )));
            }
            catch (Exception e) {
                // couldn't decode auth info
            }
            
            i = tmp.indexOf( ':' );
            if ( i == -1 )
                user = new String(tmp) ;
            else
                user = new String(tmp.substring( 0, i));
            
            if ( i != -1 )  {
                pwd= new String(tmp.substring(i+1));
                if ( pwd != null && pwd.equals("") ) pwd = null ;
                
            }
        }
        
        org.apache.axis.Message out_m = null;
        SOAPEnvelope out_env = null;
        javax.xml.soap.SOAPBody out_body = null;
        javax.xml.soap.SOAPElement out_fResponse = null;
        
        try {
            javax.xml.soap.SOAPPart sp = ((SOAPMessageContext) context).getMessage().getSOAPPart();
            javax.xml.soap.SOAPEnvelope se =  sp.getEnvelope();
            javax.xml.soap.SOAPBody sb = se.getBody();
            
            out_m= new Message(context.getRequestMessage().getSOAPEnvelope());
            out_env = out_m.getSOAPPart().getEnvelope();
            out_body = out_env.getBody();
            out_fResponse = out_body.addBodyElement(out_env.createName("batchResponse"));
            out_fResponse.addAttribute(out_env.createName("xmlns"), "urn:oasis:names:tc:DSML:2:0:core");
            
            int authorizationCode = new com.netscape.dsml.gateway.LDAPAuthenticator(user, pwd).authenticate();
            
	    if (config.getUseAuth() && authorizationCode >0 ) {
                out_fResponse.addChildElement("errorResponse").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), String.valueOf(authorizationCode) );
            }
            else {
                BatchProcessor batchProcessor = new BatchProcessor(sb);
                batchProcessor.init();
                
                if (config.getUseAuth())
                    batchProcessor.setProxy(user);
		else
		    batchProcessor.setProxy("");
                
                boolean preprocess_successful = batchProcessor.preprocess();
                Message request= context.getRequestMessage();
                if (request != null)
                {
                    java.util.Iterator request_elements = request.getSOAPBody().getChildElements();
                    if (request_elements != null)
                    {
                        if (request_elements.hasNext()){
                            ((org.apache.axis.message.RPCElement) request_elements.next()).detachNode();
                        }
                    }
                }
              
                
                if (preprocess_successful) {
                    int i = -1;
                    int NumberRequests = batchProcessor.getRequestCount();
                    
                    while (++i < NumberRequests && ! batchProcessor.Error()) {
                        batchProcessor.process(i);
                        SOAPElement RequestedItem = batchProcessor.getRequestItem(i);
                        if ( RequestedItem != null )
                            out_fResponse.addChildElement( RequestedItem  );
                    }
                }
		
		if ( out_fResponse.getChildElements().hasNext() == false) {
		    try {
			/* This is slightly inaccurate. This simply checks to see if the batch is empty, and if it is, return
			 * something. the error isn't always 91, but hopefully it's slightly more descriptive to the end user */
			out_fResponse.addChildElement("errorResponse").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), "91" );
		    }
		    catch (javax.xml.soap.SOAPException soapException) { } /* Not important to catch this */
		}
            }
        }
        catch (gatewayException gwe) {
            try {
                out_fResponse.addChildElement("errorResponse").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), "81" );
            }
            catch (javax.xml.soap.SOAPException soapException) {
                /* We did our best to try and exit gracefully. Give up. */
            }
        }
        
        catch (javax.xml.soap.SOAPException soapException) {
        }
        catch (Exception e) {
            e.printStackTrace();
            
        }
        
        /* To return false means do not try and continue onto the
         * deployed service. Since we context.setProperty our
         * SOAPResponse message, it will be sent as soon as the request
         * turns the other way into a reponse.
         */
        context.setResponseMessage( (Message)out_m  );
        return false;
        
    }
    
        
    
    public boolean handleFault(MessageContext context) {
        return false;
    }
    
    public void init(HandlerInfo config) {
        handlerInfo = config;
    }
    
    public QName[] getHeaders() {
        return handlerInfo.getHeaders();
    }
    
    public void destroy() {
        
        /* Bad things happen if the pool isn't shutdown before the servlet goes away */
        IConnectionManager ldap_pool = null;
        ProxyConnMgrFactory pmc = new ProxyConnMgrFactory();
        try {
            ldap_pool = pmc.getInstance();
            ldap_pool.shutdown();
        }
        catch (Exception e) {   }
        
        
    }
    
 
}
