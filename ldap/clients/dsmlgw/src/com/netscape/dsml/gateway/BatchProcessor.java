/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 *
 *
 */
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
    
    protected void finalize() {
        try { ldap_pool.shutdown(); }
        catch (Exception e) {  }
    }
    
    void process(int index) {
        netscape.ldap.LDAPConnection  ldc = null;
        
        ldc = ldap_pool.getConnection("");
        
        if (ldc == null) {
            requests.set(index,null);
        }
        else {
            
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
            context.setLdapConnection(ldc);
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
            
            ldap_pool.releaseConnection("",  ldc);
        }
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
