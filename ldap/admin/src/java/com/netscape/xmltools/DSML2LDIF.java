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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
