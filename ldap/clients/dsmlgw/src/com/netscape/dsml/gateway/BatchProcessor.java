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

import org.apache.xerces.parsers.DOMParser;
import java.util.logging.*;
import netscape.ldap.controls.LDAPProxiedAuthControl;
import netscape.ldap.LDAPSearchConstraints;

public class BatchProcessor {
    
    private static Logger logger = Logger.getLogger("com.netscape.dsml.gateway.BatchProcessor");
    
    private boolean attribute_resumeOnError = false;
    private boolean FirstRequest = true;
    private boolean dontHalt = true;
    
    private javax.xml.soap.SOAPElement root = null;
    private javax.xml.soap.SOAPElement batch = null;
    private java.util.Vector requests = new java.util.Vector(1);
    
    static private javax.xml.soap.MessageFactory messageFactory ;
    static private javax.xml.soap.SOAPFactory sef ;
    
    private IConnectionManager ldap_pool = null;
    
    public javax.xml.parsers.DocumentBuilderFactory dbf;
    public javax.xml.parsers.DocumentBuilder builder;
    LDAPSearchConstraints proxyAuth = null;
    
    public BatchProcessor(javax.xml.soap.SOAPBody sb) {
        
        root = (javax.xml.soap.SOAPElement) sb;
        
        
        
    }
    
    public void init() throws gatewayException {
        
        try {
            ProxyConnMgrFactory pmc = new ProxyConnMgrFactory();
            ldap_pool = pmc.getInstance();
        }
        catch (Exception e) {
            throw new gatewayException( e.getMessage() );
            
        }
        
        try {
            dbf = javax.xml.parsers.DocumentBuilderFactory.newInstance();
            builder = dbf.newDocumentBuilder();
            org.w3c.dom.Document d = builder.newDocument();
            
        }
        catch (Exception e) {
            throw new gatewayException( e.getMessage() );
        }
        
    }
    
    public void setProxy(String DN){
        LDAPProxiedAuthControl ctrl = new LDAPProxiedAuthControl( DN, true );
        proxyAuth = new LDAPSearchConstraints();
        proxyAuth.setServerControls( ctrl );
        
    }
    
    public boolean preprocess() throws javax.xml.soap.SOAPException  {
        
        if (root.getChildElements().hasNext() &&
        ((javax.xml.soap.SOAPElement) root.getChildElements().next()).getLocalName().equalsIgnoreCase("batchRequest")) {
            
            this.batch = (javax.xml.soap.SOAPElement) root.getChildElements().next();
            
            /*  attributes for batchRequest:
             *  "reponseOrder" -- not implemented
             *  "processing"   -- not implemented
             *  "onError"      --     implemented
             */
            if (batch.getAttribute("onError") == null )
                attribute_resumeOnError = false;
            else
                attribute_resumeOnError = (batch.getAttribute("onError").equalsIgnoreCase("resume ")) ? true : false ;
                
                java.util.Iterator i = batch.getChildElements();
                
                while (i.hasNext()) {
                    requests.add( i.next() );
                    logger.log(Level.INFO, "adding request");
                }
                return true;
        }
        else {
            // error, no batchrequest... exit gracefully
            
            logger.log(Level.INFO, "NO batchRequest in this envelope");
            return false;
        }
        
        
        
    }

    void process(int index) {
    	
            /* This is a hack:
            * This code is required because of Axis' incomplete
            * implementation. Without these, whenever getNodeValue() and friends are
            * called, exceptions are deliberately thrown. When Axis is fully
            * functional it should be able to be removed. The following lines
            * probably impact performance negatively.       */
            DOMParser p = new DOMParser();
            try {
                p.parse(new org.xml.sax.InputSource(new java.io.StringReader(this.requests.get(index).toString())));
            } catch (Exception E) { E.printStackTrace(); }
            /* END */
            
            Document doc = p.getDocument();
            Node myRequest = doc.getDocumentElement();
            
            String RequestType  = myRequest.getLocalName();
            gatewayContext context = new gatewayContext();
          
            
            Node res = null;
            if (proxyAuth != null)
                context.setConstraints( proxyAuth);
            context.setRootNode(myRequest.cloneNode(true));
            
            logger.log(Level.INFO, "Processing: starting {0}", RequestType);
            
            if (FirstRequest && RequestType.equals("authRequest")){
                if (FirstRequest) {
                    
                    OperationAuth CurrentOperation = new OperationAuth();
                    res = CurrentOperation.getResponse(context) ;
                    
                    FirstRequest = false;
                } else {
                    // error
                }
                
            } else if (RequestType.equals("searchRequest")) {
                
                OperationSearch CurrentOperation = new OperationSearch();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("modifyRequest")){
                
                OperationModify CurrentOperation= new OperationModify();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("addRequest")) {
                
                OperationAdd CurrentOperation = new OperationAdd();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("delRequest")) {
                
                OperationDelete CurrentOperation = new OperationDelete();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("modDNRequest")) {
                
                OperationModifyDN CurrentOperation = new OperationModifyDN();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("compareRequest")) {
                
                OperationCompare CurrentOperation = new OperationCompare();
                res = CurrentOperation.getResponse(context) ;
                
            } else if (RequestType.equals("extendedRequest")) {
                
                OperationExtended CurrentOperation = new OperationExtended();
                res = CurrentOperation.getResponse(context) ;
                
            } else {
                //   output = echoHeaderStringHandler.sef.createElement("errorReponse");
            }
            
            
            
            
            requests.set(index,res);
            logger.log(Level.INFO, "Processing: finished {0}", RequestType);
            FirstRequest = false;
            ldap_pool.releaseConnection( context.getLdapConnection() );
    }
    
    
    
    public boolean Error(){
        return !this.dontHalt;
    }
    
    public int getRequestCount(){
        return  this.requests.size();
    }
    
    public javax.xml.soap.SOAPElement getRequestItem(int index) {
        return   (javax.xml.soap.SOAPElement) this.requests.get(index);
    }
    
}
