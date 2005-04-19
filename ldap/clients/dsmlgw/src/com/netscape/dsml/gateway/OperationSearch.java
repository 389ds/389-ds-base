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

import netscape.ldap.*;
import org.w3c.dom.*;

import java.util.logging.*;

import org.w3c.dom.DOMImplementation;
import org.w3c.dom.Node;
import org.w3c.dom.traversal.NodeIterator;
import org.w3c.dom.traversal.DocumentTraversal;
import org.w3c.dom.traversal.TreeWalker;
import org.w3c.dom.traversal.NodeFilter;

class OperationSearch extends GenericOperation {
    
    org.w3c.dom.Node root = null;
    LDAPConnection ldapConn = null;
    javax.xml.soap.MessageFactory messageFactory = null ;
    javax.xml.soap.SOAPFactory sef = null;
    private static Logger logger =
    Logger.getLogger("com.netscape.dsml.service.ProxyConnectionManager");
    
    OperationSearch(){
        try {
            messageFactory = javax.xml.soap.MessageFactory.newInstance();
            sef = javax.xml.soap.SOAPFactory.newInstance();
        } catch (Exception E) {E.printStackTrace();
        }
    }
    
    public void setRoot(org.w3c.dom.Node op){
        if (root == null)
            root = op;
    }
    
    public void setLDAPConnection(LDAPConnection lc) {
        if (ldapConn == null)
            ldapConn = lc;
    }
    
    public javax.xml.soap.SOAPElement getResponse(gatewayContext ctx) {
        
      
        root = ctx.getRootNode();
        
        
        int scope = -1;
        int derefAliases = -1;
        
        LDAPException ldException = null;
        LDAPSearchResults results = null;
        
        String dn = root.getAttributes().getNamedItem("dn").getNodeValue().trim();
        String scopeRaw = root.getAttributes().getNamedItem("scope").getNodeValue().trim();
        String derefAliasesRaw = root.getAttributes().getNamedItem("derefAliases").getNodeValue().trim();
        
        int sizeLimit = 0;
        try {
            if ( root.getAttributes().getNamedItem("sizeLimit") != null)
            sizeLimit = Integer.parseInt( root.getAttributes().getNamedItem("sizeLimit").getNodeValue());
        } catch (Exception e) {}
        
        
        int timeLimit = 0;
        try {
            if ( root.getAttributes().getNamedItem("timeLimit") != null)
            timeLimit = Integer.parseInt( root.getAttributes().getNamedItem("timeLimit").getNodeValue());
        } catch (Exception e) {}
        
        boolean typesOnly = false;
        try {
            if ( root.getAttributes().getNamedItem("typesOnly") != null )
            typesOnly = Boolean.valueOf(root.getAttributes().getNamedItem("typesOnly").getNodeValue()).booleanValue();
        } catch (Exception e) {}
        
        
        String attributeList[] = null;
        java.util.Vector attributeRaw = new java.util.Vector();
        java.util.Vector Controls = new java.util.Vector();
        String filterString = new String();
        
        root.normalize();
        
        int whattoshow = NodeFilter.SHOW_ALL;
        NodeFilter nodefilter = null;
        boolean expandreferences = false;
        
        
        org.w3c.dom.NodeList nl = root.getChildNodes();
        for (int i=0; i< nl.getLength(); i++) {
            if (nl.item(i).getNodeType() == Node.ELEMENT_NODE) {
                try {
                    if (nl.item(i).getLocalName().equals("filter") ) {
                        // should check to make sure there's only one child to this filter
                        NodeList filters = nl.item(i).getChildNodes();
                        for (int j=0; j< filters.getLength(); j++) {
                            if (filters.item(j).getNodeType() == Node.ELEMENT_NODE){
                                filterString =   ParseFilter.parseFilterFromNode( filters.item(j));
                            }
                        }
                        
                        
                    } else if (nl.item(i).getLocalName().equals("attributes") ) {
                        NodeList attributes = nl.item(i).getChildNodes();
                        
                        for (int j=0; j< attributes.getLength(); j++) {
                            if (attributes.item(j).getNodeType() == Node.ELEMENT_NODE){
                                attributeRaw.add( attributes.item(j).getAttributes().getNamedItem("name").getNodeValue() );
                            }
                        }
                        attributeList= new String[ attributeRaw.size()];
                        for (int j=0; j< attributeRaw.size(); j++)
                            attributeList[j] = (String) attributeRaw.get(j);
                        
                    } else if (nl.item(i).getLocalName().equals("control") ) {
                        Controls.add( ParseControl.parseControlFromNode(nl.item(i)) );
                        
                    }
                    
                }
                catch (Exception e) { e.printStackTrace();}
                
            } }
        // XXX
        
        if (scopeRaw.equals("baseObject"))
            scope = ldapConn.SCOPE_BASE;
        else if (scopeRaw.equals("singleLevel"))
            scope = ldapConn.SCOPE_ONE;
        else if (scopeRaw.equals("wholeSubtree"))
            scope = ldapConn.SCOPE_SUB;
        else
            scope = ldapConn.SCOPE_BASE;
        
        
        if (derefAliasesRaw.equals("neverDerefAliases"))
            derefAliases = ldapConn.DEREF_NEVER;
        else if (derefAliasesRaw.equals("derefInSearching"))
            derefAliases = ldapConn.DEREF_SEARCHING;
        else if (derefAliasesRaw.equals("derefFindingBaseObj"))
            derefAliases = ldapConn.DEREF_FINDING;
        else if (derefAliasesRaw.equals("derefAlways"))
            derefAliases = ldapConn.DEREF_ALWAYS;
        else
            derefAliases = ldapConn.DEREF_NEVER;
        
        
        
        logger.log(Level.INFO, "dn: {0}", dn);
        logger.log(Level.INFO, "scope: {0}", scopeRaw);
        logger.log(Level.INFO, "derefAliases: {0}", derefAliasesRaw);
        
        logger.log(Level.INFO, "sizeLimit: {0}", String.valueOf(sizeLimit) );
        logger.log(Level.INFO, "timeLimit: {0}", String.valueOf(timeLimit) );
        logger.log(Level.INFO, "typesOnly: {0}", String.valueOf(typesOnly) );
        if (attributeList ==null)
            logger.log(Level.INFO, "attributeList: {0}", "null");
        else
            logger.log(Level.INFO, "attributeList: {0}",  attributeList.toString() );
        logger.log(Level.INFO, "filter: {0}", filterString );
        
        /*
         * System.out.println("dn  : " + dn  );
         * System.out.println("scope  : " +  scopeRaw );
         * System.out.println("derefAliases : " + derefAliasesRaw  );
         * System.out.println("sizeLimit : " + sizeLimit  );
         * System.out.println("timeLimit : " +  timeLimit );
         * System.out.println("typesOnly : " +  typesOnly );
         * System.out.println("filterString : " + filterString   );
         *
         * System.out.println("attributeList[]");
         * if (attributeList == null)
         *    System.out.println("null");
         * else {
         *   for (int i=0; i < attributeList.length ; i++ )
         *       System.out.println("attributeList[" + i + "] : " + attributeList[i]);
         *}
         */
        
        int resultCode = 0;
        String errorMessage = "completed";
        
        ctx.setConstraints( new LDAPSearchConstraints() );
        ctx.getConstraints().setTimeLimit(timeLimit);
        ctx.getConstraints().setDereference(derefAliases);
        ctx.getConstraints().setMaxResults(sizeLimit);
        
        try {
            if (Controls.size() >0 ) {
                if  (ctx.getConstraints() == null) {
                    ctx.setConstraints( new LDAPSearchConstraints() );
                }
                // XXX this gave a class cast exception, don't understand why
                
                netscape.ldap.LDAPControl tmp[] = new netscape.ldap.LDAPControl[ Controls.size() ];
                for (int i=0; i< Controls.size(); i++) {
                    tmp[i] =  (netscape.ldap.LDAPControl) Controls.get(i);
                }
                
                ctx.getConstraints().setServerControls( tmp );
                
            }
            
            ldapConn = ctx.getLdapConnection();
            
            if (ctx.getConstraints() != null)
                results = ldapConn.search(dn, scope, filterString,  attributeList, false, ctx.getConstraints() );
            else
                results =  ldapConn.search( dn, scope, filterString,  attributeList, false);
            
        } catch (LDAPException E) {
            resultCode = E.getLDAPResultCode();
            errorMessage = "LDAP Error code " + new Integer(resultCode).toString();
            // This line should work
            // XXX errorMessage = LDAPException.errorCodeToString(resultCode);
            E.printStackTrace();
        }
        
        
        javax.xml.soap.SOAPEnvelope elementFactory = null;
        javax.xml.soap.SOAPBody entryFactory = null;
        javax.xml.soap.SOAPBodyElement sbe = null;
        try {
            sbe = messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("searchResponse") );
            entryFactory =  messageFactory.createMessage().getSOAPBody();
            elementFactory = messageFactory.createMessage().getSOAPPart().getEnvelope();
        } catch (Exception E) { };
        
        if (results != null) {
            while (results.hasMoreElements()) {
                try {
                    LDAPEntry entry = results.next();
                    LDAPAttributeSet attributeset = entry.getAttributeSet();
                    java.util.Enumeration attribs = attributeset.getAttributes();
                    dn = entry.getDN();
                    
                    javax.xml.soap.SOAPBodyElement searchresultentry = null;
                    try {
                        searchresultentry = entryFactory.addBodyElement(elementFactory.createName("searchResultEntry") );
                        searchresultentry.addAttribute(elementFactory.createName("dn"), dn);
                        sbe.addChildElement(searchresultentry);
                    } catch (Exception E) { E.printStackTrace(); }
                    
                    
                    while (attribs.hasMoreElements()) {
                        LDAPAttribute att = (LDAPAttribute) attribs.nextElement();
                        
                        String[] values =   att.getStringValueArray();
                        javax.xml.soap.SOAPElement attr  = null;
                        attr = searchresultentry.addChildElement( elementFactory.createName("attr"));
                        attr.addAttribute(elementFactory.createName( "name" ), att.getName() );
                        
                        for (int k=0; k< values.length;k++) {
                            javax.xml.soap.SOAPElement val = attr.addChildElement("value");
                            val.addTextNode( values[k] );
                        }
                        
                    }
                    
                } catch (LDAPException E) {
                    resultCode = E.getLDAPResultCode();
                    errorMessage = E.getLDAPErrorMessage() ;
                } catch (Exception E) { E.printStackTrace(); }
                
            }
            
        }
        
        try {
            javax.xml.soap.SOAPBodyElement resultdone = null;
            
            resultdone = messageFactory.createMessage().getSOAPBody().addBodyElement(elementFactory.createName("searchResultDone") );
            
            /* if there are controls to add to this envelope, they go in the searchresultdone tag */
            
            netscape.ldap.LDAPControl serverControls[] = null;
            if (results!=null)
                serverControls = results.getResponseControls();
            
            if (serverControls != null && serverControls.length >0) {
                
                for (int k=0; k< serverControls.length; k++) {
                    if ( serverControls[k] instanceof netscape.ldap.controls.LDAPVirtualListResponse ) {
                        netscape.ldap.controls.LDAPVirtualListResponse lvlr = (netscape.ldap.controls.LDAPVirtualListResponse) serverControls[k];
                        javax.xml.soap.SOAPBodyElement ctl =   messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("control") );
                        ctl.addAttribute( messageFactory.createMessage().getSOAPPart().getEnvelope().createName("type"), netscape.ldap.controls.LDAPVirtualListResponse.VIRTUALLISTRESPONSE );
                        resultCode = lvlr.getResultCode();
                        
                        javax.xml.soap.SOAPElement controlValue = messageFactory.createMessage().getSOAPBody().addChildElement("controlValue", "", "");
                        controlValue.addChildElement("begin", "").addTextNode( new Integer( lvlr.getFirstPosition() ).toString() );
                        controlValue.addChildElement("count", "").addTextNode( new Integer( lvlr.getContentCount() ).toString() );
                        
                        resultdone.addChildElement( controlValue);
                        
                        resultdone.addChildElement(ctl);
                    } else if ( serverControls[k] instanceof netscape.ldap.controls.LDAPSortControl ) {
                        netscape.ldap.controls.LDAPSortControl lsc =  (netscape.ldap.controls.LDAPSortControl) serverControls[k];
                        javax.xml.soap.SOAPBodyElement ctl =   messageFactory.createMessage().getSOAPBody().addBodyElement(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("control") );
                        ctl.addAttribute( messageFactory.createMessage().getSOAPPart().getEnvelope().createName("type"), netscape.ldap.controls.LDAPSortControl.SORTRESPONSE );
                        resultdone.addChildElement(ctl);
                        resultCode = lsc.getResultCode(); // new result code now by way of control
                    }
                }
            }          /* end of control section */
            
            
            resultdone.addChildElement("resultCode").addAttribute(messageFactory.createMessage().getSOAPPart().getEnvelope().createName("code"), new Integer(resultCode).toString() );
            sbe.addChildElement(resultdone);
        } catch (Exception E) {E.printStackTrace(); }
        
        return sbe;
        
    }
    
}
