/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

package com.netscape.xmltools;

import java.io.*;
import java.util.*;
import netscape.ldap.util.GetOpt;
import netscape.ldap.util.*;
import netscape.ldap.*;

/**
 * Tool for converting LDIF document to DSML document
 */
public class LDIF2DSML {

    /**
     * Default no argument constructor.
     */
    public LDIF2DSML() {
    }

    /**
     * Converter taking a filename argument for the input LDIF file
     *
     * @param filename system-dependent file name for the input LDIF file
     */
    public LDIF2DSML( String filename ) throws IOException {

        DataInputStream ds = null;

        if( filename == null || filename.length() == 0 ) {
            ds = new DataInputStream( System.in );
        }
        else {
            ds = new DataInputStream( new FileInputStream( filename) );
        }

        // set up the ldif parser with the input stream
        //
        m_ldifReader = new LDIF( ds );
    }

    /**
     * Converts an LDIF record to an LDAPEntry.  Only convert if the content
     * of the LDIF record is of the type LDIFContent.ATTRIBUTE_CONTENT.
     *
     * @param    rec   An LDIFRecord to be converted
     * @returns  The converted LDAPEntry.  null is returned for any ldif record not
     *           being recognized with attribute content
     */
    public LDAPEntry toLDAPEntry( LDIFRecord rec ) {

        String dn = rec.getDN();
        LDIFContent content = rec.getContent();

        if (content.getType() == LDIFContent.ATTRIBUTE_CONTENT) {
            LDIFAttributeContent entry = (LDIFAttributeContent)content;
            /* LDAPAttribute attrs[] = entry.getAttributes();
            for( int i = 0; i < attrs.length; i++ ) {
                System.err.println( attrs[i].toString() );
            }*/
            return new LDAPEntry( dn, new LDAPAttributeSet( entry.getAttributes()) );
        }
        return null;
    }

    /**
     * Conversion from ldif to dsml document.
     */
    public void convert() {
        DSMLWriter writer = new DSMLWriter( m_pw );
        int recCount = 0;
        int rejectCount = 0;

        try {

            m_pw.println( "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>");
            m_pw.println( "<dsml:dsml xmlns:dsml=\"http://www.dsml.org/DSML\">");
            m_pw.println( "  <dsml:directory-entries>" );

            for( LDIFRecord rec = m_ldifReader.nextRecord();
                 rec != null; rec = m_ldifReader.nextRecord(), recCount++ ) {
                LDAPEntry entry = toLDAPEntry( rec );
                if (entry != null) {
                     writer.printEntry( entry );
                     entry = null;
                }
            }

            m_pw.println( "  </dsml:directory-entries>" );
            m_pw.println( "</dsml:dsml>" );

        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            m_pw.flush();
            m_pw.close();
        }
    }

    /**
     * Parse the command line parameters and setup the options parameters
     */
    static private GetOpt parseParameters( String[] args ) {

        GetOpt options = new GetOpt("H?so:", args);

        if (options.hasOption('H') || options.hasOption('?')) {
            usage();
            System.exit(0);
        }

        if (options.hasOption('v')) {
            m_verbose = true;
        }

        if (options.hasOption('o')) {
            m_outfile = options.getOptionParam('o');
            if (m_outfile == null) {
                System.err.println( "Missing argument for output filename" );
                usage();
                System.exit(0);
            }

            try {
                /*
                m_pw = null;
                m_pw = new PrintWriter( new FileOutputStream(m_outfile), true );
                */

                m_pw = null;
                // always write DSML document out in UTF8 encoding
                //
                OutputStreamWriter os = new OutputStreamWriter( new FileOutputStream(m_outfile), "UTF8" );
                m_pw = new PrintWriter( os, true );
            } catch (IOException e) {
                System.err.println( "Can't open " + m_outfile );
                System.err.println( e.toString() );
                System.exit(1);
            }
        }

        Vector extras = options.getParameters();
        if (extras.size() == 1 ) {
            m_infile = new String( (String)extras.get(0) );
        } else {
            if ( options.hasOption('s') ) {
                // System.err.println( "Use standard input for input of ldif file" );
                m_infile = null;
            } else {
                usage();
                System.exit(0);
            }
        }
        return options;
    }

    /**
     * Print out usage of the tool
     */
    static private void usage() {
        System.err.println("Usage: java [-classpath $CLASSPATH] LDIF2DSML infile.ldif [-s] [-o outfile.dsml]");
        System.err.println("options");
        System.err.println("  -s              use standard input for input of ldif file" );
        System.err.println("  -o outfile      filename for the output DSML file" );
        System.err.println("  -H -?           for usage" );
        // System.err.println("  -v              for verbose mode" );

    }

    /**
     * Main routine for the tool.
     */
    static public void main(String[] args){

        m_verify = Boolean.getBoolean("verify");

        parseParameters( args);

        LDIF2DSML converter = null;

        try {
            if (m_infile== null) {
                converter = new LDIF2DSML( null );
            }
            else {
                converter = new LDIF2DSML( m_infile );
            }
        } catch( IOException e ) {
            System.err.println( "Error encountered in input" );
            System.err.println( e.toString() );
            System.exit( 1 );
        }

        if (m_verify) {
            System.exit( 0 );
        }

        converter.convert();
        System.exit( 0 );

    }

    static private PrintWriter m_pw = new PrintWriter(System.out, true);
    static private boolean m_verbose = false;
    static private String m_outfile = null;
    static private String m_infile = null;
    static private LDIF m_ldifReader = null;
    static private boolean m_verify = false;
}
