/** BEGIN COPYRIGHT BLOCK
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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
package com.netscape.xmltools;

import javax.xml.parsers.*;
import org.xml.sax.*;
import org.xml.sax.helpers.*;

/**
 * A skeletal SAX driver, which doesn't create a document. Its content handler
 * is a DSMLSAXHandler which creates and dispatches LDAP entries from a
 * DSML document on the fly.
 */
public class DSMLSAXBuilder {

    /**
     * <p>
     * Creates a new DSMLSAXBuilder with parser that will not validate.
     * </p>
     */
    public DSMLSAXBuilder() {
    }

    /**
     * <p>
     * Creates a new DSMLSAXBuilder which will validate
     * according to the given parameter.
     * </p>
     *
     * @param validate <code>boolean</code> indicating if
     *                 validation should occur.
     */
    public DSMLSAXBuilder(boolean validate) {
        m_validate = validate;
    }

    /**
     * Processes the supplied input source
     *
     * @param in <code>InputSource</code> to read from
     * @throws SAXException when errors occur in parsing
     */
    public void parse(InputSource in) throws SAXException {
        DSMLSAXHandler contentHandler = null;

        try {
            // Create the parser
            SAXParserFactory fac = SAXParserFactory.newInstance();
        fac.setValidating( m_validate );

            XMLReader parser = fac.newSAXParser().getXMLReader();
            parser.setFeature( "http://xml.org/sax/features/namespaces",
                   true );
            parser.setFeature( "http://xml.org/sax/features/namespace-prefixes",
                   true );

            // Create and configure the content handler
            parser.setContentHandler( new DSMLSAXHandler() );

            // Parse the document
            parser.parse( in );
        } catch (Exception e) {
            if (e instanceof SAXParseException) {
                SAXParseException p = (SAXParseException)e;
                String systemId = p.getSystemId();
                if (systemId != null) {
                    throw new SAXException("Error on line " +
                              p.getLineNumber() + " of document "
                              + systemId, e);
                } else {
                    throw new SAXException("Error on line " +
                              p.getLineNumber(), e);
                }
            } else if (e instanceof SAXException) {
                throw (SAXException)e;
            } else {
                throw new SAXException("Error in parsing", e);
            }
        }
    }

    private boolean m_validate = false;
}
