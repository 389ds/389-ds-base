/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

package com.netscape.xmltools;

import java.io.*;
import java.util.*;
import org.xml.sax.*;
import netscape.ldap.*;
import netscape.ldap.util.*;

/**
 * Class for reading a DSML document according to the DSML 1.0 spec.
 */
public class DSMLReader {

    /**
     * Default no argument constructor
     */
    public DSMLReader() throws IOException {
        m_ds = new DataInputStream( System.in );
    }

    /**
     * Constructor for a dsml reader based on a file
     *
     * @param filename system-dependent filename
     */
    public DSMLReader( String filename ) throws IOException {
        m_ds = new DataInputStream( new FileInputStream( filename ) );
    }

    /**
     * Parses the input stream as a DSML document
     *
     * @throws SAXException on parsing errors
     */
    public void parseDocument() throws SAXException {
        DSMLSAXBuilder builder = new DSMLSAXBuilder();
        builder.parse( new InputSource( m_ds ) );
    }

    private DataInputStream m_ds = null;
}
