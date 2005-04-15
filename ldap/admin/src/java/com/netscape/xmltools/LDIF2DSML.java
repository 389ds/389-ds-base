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
