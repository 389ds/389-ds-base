/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

package com.netscape.xmltools;

import java.util.*;
import java.io.*;
import org.xml.sax.SAXException;
import netscape.ldap.*;
import netscape.ldap.util.*;

/**
 * Tool for converting DSML document to LDIF document
 */
public class DSML2LDIF {

    /**
     * Default no argument constructor.
     */
    public DSML2LDIF() {
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
                m_pw = null;
                m_pw = new PrintWriter( new FileOutputStream(m_outfile), true );
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
                // Use standard input for input of dsml file
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
        System.err.println("Usage: java [-classpath CLASSPATH] DSML2LDIF infile.dsml -s [-o outfile.ldif]");
        System.err.println("options");
        System.err.println("  -s              use standard input for input of dsml file" );
        System.err.println("  -o outfile      filename for the output LDIF file" );
        System.err.println("  -H -?           for usage" );
        // System.err.println("  -v              for verbose mode" );
    }

    static void processEntry( LDAPEntry entry ) {

        long now = 0;
        long later = 0;
        long writeTime;
        if ( m_doProfile ) {
            now = System.currentTimeMillis();
        }
        try {
            if (entry != null) {
                m_ldifWriter.printEntry( entry );
                // e = null;
            }
            if ( m_doProfile ) {
                later = System.currentTimeMillis();
                writeTime = later - now;
                m_lapWriteTime += writeTime;
                now = later;
            }
        } catch (Exception e) {
            ;
        }

        if ( m_doProfile ) {
            m_entryCount++;
            if ( (m_entryCount % LAP_LENGTH) == 0 ) {
                System.err.println( m_entryCount + " entries" );
                System.err.println( "  " + m_lapWriteTime +
                            " ms to write" );
                m_lapWriteTime = 0;
                System.err.println( "  " + (now - m_lapTime) +
                            " ms total this lap" );
                m_lapTime = now;
            }
        }
    }

    /**
     * Temporary class to subclass from LDIFWriter
     */
    static class MyLDIFWriter extends netscape.ldap.util.LDIFWriter {
        public MyLDIFWriter( PrintWriter pw ) {
            super( pw );
        }

        /**
         * Print prologue to entry
         *
         * @param dn the DN of the entry
         */
        protected void printEntryStart( String dn ) {
            if ( dn == null ) {
                dn = "";
            } else {
                byte[] b = null;
                try {
                    b = dn.getBytes( "UTF8" );
                } catch ( UnsupportedEncodingException ex ) {
                }
                if ( !LDIF.isPrintable(b) ) {
                    dn = getPrintableValue( b );
                    printString( "dn" + "::" + " " + dn );
                    return;
                }
            }
            printString( "dn" + ":" + " " + dn );
        }
    }

    /**
     * Main routine for the tool.
     */
    public static void main(String[] args) {

        m_verify = Boolean.getBoolean("verify");
        parseParameters( args );

        DSMLReader reader = null;

        try {
            if ( m_infile == null ) {
                reader = new DSMLReader();
            }
            else {
                reader = new DSMLReader( m_infile );
            }
        } catch (Exception e) {
            System.err.println("Error encountered in input");
            System.err.println(e.toString());
            System.exit(1);
        }

        m_ldifWriter = new MyLDIFWriter( m_pw );

        if (m_verify) {
            if (m_pw != null) {
                m_pw.close();
            }
            System.exit( 0 );
        }

        try {
            if ( m_doProfile ) {
                m_startTime = System.currentTimeMillis();
                m_lapTime = m_startTime;
            }
            reader.parseDocument();
        } catch( SAXException e ) {
            System.err.println("Error encountered in parsing the DSML document");
            System.err.println(e.getMessage());
            e.printStackTrace();
            System.exit(1);
        } catch( Exception e ) {
            e.printStackTrace();
        } finally {
            if (m_pw != null) {
                m_pw.flush();
                m_pw.close();
            }
        }

        if ( m_doProfile ) {
            System.err.println(
               (System.currentTimeMillis() - m_startTime) / 1000 +
               " seconds total for " + m_entryCount + " entries" );
        }

        System.exit( 0 );
    }

    static private PrintWriter m_pw = new PrintWriter(System.out, true);;
    static private boolean m_verbose = false;
    static private String m_outfile = null;
    static private String m_infile = null;
    static private LDIFWriter m_ldifWriter = null;
    static private boolean m_verify = false;
    static boolean m_doProfile = Boolean.getBoolean("com.netscape.xmltools.doProfile");
    static long m_startTime;
    static long m_toEntryTime = 0;
    static long m_writeTime = 0;
    static long m_lapTime;
    static long m_lapToEntryTime = 0;
    static long m_lapWriteTime = 0;
    static long m_entryCount;
    static final int LAP_LENGTH = 1000;
}
