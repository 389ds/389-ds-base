/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
