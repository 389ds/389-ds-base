/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
package com.netscape.xmltools;

import java.util.*;

import org.xml.sax.*;
import org.xml.sax.helpers.*;

import netscape.ldap.*;
import netscape.ldap.util.*;

/**
 * Content handler which dispatches each LDAP entry composed from a dsml:entry
 * element and its children. No document is built.
 */
public class DSMLSAXHandler extends DefaultHandler {
    /** The DSML namespace */
    private static final String DSML_NS = "http://www.dsml.org/DSML";

    /** Element stack */
    private Stack stack = new Stack();

    /** Whether to ignore ignorable whitespace */
    private boolean ignoringWhite = true;

    /** Keeps track of name spaces */
    private NamespaceSupport namespaces = new NamespaceSupport();

    /**
     * Creates a new <code>SAXHandler</code> that listens to SAX
     * events and dispatches LDAP entries as they are found
     */
    public DSMLSAXHandler() {
    }

    /**
     * Returns the value of a particular attribute from an array of
     * attributes
     *
     * @param atts Array of attributes to process
     * @param name The name of the attribute to return
     * @return The value of the matching attribute, or null if not there
     */
    private String findAttribute( Attributes atts, String name ) {
        for (int i = 0, len = atts.getLength(); i < len; i++) {
            String attLocalName = atts.getLocalName(i);
            String attQName = atts.getQName(i);
            // Bypass any xmlns attributes which might appear, as we got
            // them already in startPrefixMapping().

            if (attQName.startsWith("xmlns:") || attQName.equals("xmlns")) {
                continue;
            }

            if ( name.equalsIgnoreCase( attLocalName ) ) {
            return atts.getValue(i);
            }
        }
        return null;
    }

    /**
     * Creates and returns an object corresponding to the element type
     *
     * @param prefix Namespace
     * @param localName Element name without namespace
     * @param atts Any attributes of the element
     * @return An object corresponding to the element type
     */
    private Object getObjectForElement( String prefix,
                    String localName,
                    Attributes atts ) {
        // Should not assume the "dsml" prefix. Any prefix can
        // be mapped to the DSML namespace URI.
        String uri = namespaces.getURI( prefix );
        if ( (uri != null) && uri.equalsIgnoreCase( DSML_NS ) ) {
            if ( "entry".equalsIgnoreCase( localName ) ) {
                String dn = findAttribute( atts, "dn" );
                if ( dn != null ) {
                    LDAPEntry entry = new LDAPEntry( dn );
                    return entry;
                } else {
                    System.err.println( "No DN for entry" );
                }
            } else if ( "attr".equalsIgnoreCase( localName ) ) {
                String name = findAttribute( atts, "name" );
                if ( name != null ) {
                    LDAPAttribute attr = new LDAPAttribute( name );
                    return attr;
                } else {
                    System.err.println( "No name for attribute" );
                }
            } else if ( "objectclass".equalsIgnoreCase( localName ) ) {
                LDAPAttribute attr = new LDAPAttribute( "objectclass" );
                return attr;
            } else if ( "oc-value".equalsIgnoreCase( localName ) ) {
                return new StringAttrValue();
            } else if ( "value".equalsIgnoreCase( localName ) ) {
                String enc = findAttribute( atts, "encoding" );
                if ( "Base64".equalsIgnoreCase( enc ) ) {
                    return new BinaryAttrValue();
                } else {
                    return new StringAttrValue();
                }
            }
        }
        return null;
    }


    /**
     * This reports the occurrence of an actual element.  It will include
     *   the element's attributes, with the exception of XML vocabulary
     *   specific attributes, such as
     *   <code>xmlns:[namespace prefix]</code> and
     *   <code>xsi:schemaLocation</code>.
     *
     * @param namespaceURI <code>String</code> namespace URI this element
     *                     is associated with, or an empty
     *                     <code>String</code>
     * @param localName <code>String</code> name of element (with no
     *                  namespace prefix, if one is present)
     * @param qName <code>String</code> XML 1.0 version of element name:
     *                [namespace prefix]:[localName]
     * @param atts <code>Attributes</code> list for this element
     * @throws SAXException when things go wrong
     */
    public void startElement(String namespaceURI, String localName,
                             String qName, Attributes atts)
                             throws SAXException {
        Object obj = null;

        int split = qName.indexOf(":");
        String prefix = (split > 0) ? qName.substring(0, split) : null;
        if ( prefix != null ) {
            // Save the prefix to URI mapping
            if ((namespaceURI != null) && (!namespaceURI.equals(""))) {
                namespaces.pushContext();
                if ( namespaces.getPrefix( namespaceURI ) == null ) {
                    namespaces.declarePrefix( prefix, namespaceURI );
                }
            }
            obj = getObjectForElement( prefix, localName, atts );
            if ( obj instanceof LDAPEntry ) {
                m_entry = (LDAPEntry)obj;
            } else if ( obj instanceof LDAPAttribute ) {
                m_attr = (LDAPAttribute)obj;
            }
        } else {
            // This should never happen
            System.err.println( "No namespace for [" + qName + "]" );
            m_entry = null;
            m_attr = null;
        }

        if ( obj != null ) {
            // Put the DSML object on the stack
            stack.push(obj);
        }
    }

    /**
     * This will report character data (within an element)
     *
     * @param ch <code>char[]</code> character array with character data
     * @param start <code>int</code> index in array where data starts.
     * @param length <code>int</code> length of data.
     * @throws SAXException when things go wrong
     */
    public void characters(char[] ch, int start, int length)
        throws SAXException {

        // Ignore whitespace
        boolean empty = true;
        int maxOffset = start+length;
        for( int i = start; i < maxOffset; i++ ) {
            if ( (ch[i] != ' ') && (ch[i] != '\n') ) {
                empty = false;
                break;
            }
        }
        if ( empty ) {
             return;
        }

        m_sb.append( ch, start, length );
    }

    /**
     * Captures ignorable whitespace as text. If
     * setIgnoringElementContentWhitespace(true) has been called then this
     * method does nothing.
     *
     * @param ch <code>[]</code> - char array of ignorable whitespace
     * @param start <code>int</code> - starting position within array
     * @param length <code>int</code> - length of whitespace after start
     * @throws SAXException when things go wrong
     */
    public void ignorableWhitespace(char[] ch, int start, int length)
    throws SAXException {
        if (ignoringWhite) return;

        characters(ch, start, length);
    }

    /**
     * Takes any action appropriate for a particular object when it has
     * been composed from an element and any children
     *
     * @param obj A DSML object
     * @return The updated object
     */
    private Object processElementByObject( Object obj ) {
        if ( obj instanceof LDAPEntry ) {
            // An entry is ready to be processed
            LDAPEntry entry = (LDAPEntry)obj;
            // Rather than a static method, this should be an object that
            // implements an interface and is provided with a setEntryProcessor
            // method
                DSML2LDIF.processEntry( entry );
        } else if ( obj instanceof StringAttrValue ) {
            if ( m_attr == null ) {
                // This should never happen
                System.err.println( "dsml:value or dsml:oc-value element " +
                            "not inside dsml:attr or " +
                            "dsml:objectclass" );
                } else {
                    String val = new String( m_sb );
                    m_attr.addValue( val );
            }
            m_sb.setLength( 0 );
        } else if ( obj instanceof BinaryAttrValue ) {
            if ( m_attr == null ) {
                System.err.println( "dsml:value element not inside dsml:attr" );
            } else {
                ByteBuf inBuf = new ByteBuf( new String( m_sb ) );
                ByteBuf decodedBuf = new ByteBuf();
                // Decode base 64 into binary
                m_decoder.translate( inBuf, decodedBuf );
                int nBytes = decodedBuf.length();
                if ( nBytes > 0 ) {
                    String decodedValue = new String(decodedBuf.toBytes(), 0,
                                     nBytes);
                    m_attr.addValue( decodedValue);
                }
            }
            m_sb.setLength( 0 );
        } else if ( obj instanceof LDAPAttribute ) {
            if ( m_entry == null ) {
                // This should never happen
                System.err.println( "dsml:attr element not inside dsml:entry" );
            } else {
                m_entry.getAttributeSet().add( (LDAPAttribute)obj );
            }
            m_attr = null;
        }
        return obj;
    }

    /**
     * Indicates the end of an element
     *   (<code>&lt;/[element name]&gt;</code>) is reached.  Note that
     *   the parser does not distinguish between empty
     *   elements and non-empty elements, so this will occur uniformly.
     *
     * @param namespaceURI <code>String</code> URI of namespace this
     *                     element is associated with
     * @param localName <code>String</code> name of element without prefix
     * @param qName <code>String</code> name of element in XML 1.0 form
     * @throws SAXException when things go wrong
     */
    public void endElement(String namespaceURI, String localName,
                           String qName) throws SAXException {
        if ( !stack.empty() ) {
            Object obj = stack.pop();

            processElementByObject( obj );
        }

        if ((namespaceURI != null) &&
            (!namespaceURI.equals(""))) {
            namespaces.popContext();
        }
    }

    private LDAPEntry m_entry = null;
    private LDAPAttribute m_attr = null;
    private StringBuffer m_sb = new StringBuffer(1024);
    static private MimeBase64Decoder m_decoder = new MimeBase64Decoder();

    class StringAttrValue {
    }
    class BinaryAttrValue {
    }
}
