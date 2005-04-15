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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import java.io.*;
import java.util.Properties;
import java.util.logging.*;

public class Configuration {
    private static Logger logger = Logger.getLogger("com.netscape.dsml.gateway.Configuration");
    
    private static String propertiesFilename;
    private final static String header = "properties file for the Netscape DSMLGW";
    private final static String[][] defaults = new String[][]
    {   { "MinPool", "5"  },
        { "MaxPool", "10" },
        { "MinLoginPool", "2" },
        { "MaxLoginPool", "5" },
        { "ServerHost", "localhost" },
        { "ServerPort", "389" },
        { "BindDN", "" },
        { "BindPW" , "" },
        { "UseAuth", "false" }
    };
    
    private static Configuration _instance = null;
    private static Object lock = new Object();
    private static Properties properties = null;
    
    /** Creates a new instance of Config */
    private Configuration() {
       
        propertiesFilename = System.getProperty("user.home","") + System.getProperty("file.separator") + "dsmlgw.cfg";
        logger.log( Level.CONFIG, "using properties filename " + propertiesFilename);
        load();
    }
    
    public static Configuration  getInstance() {
        if (null == _instance ) {
            synchronized(lock) {
                if (_instance == null )
                    _instance = new Configuration();
                
            }
        }
        return _instance;
    }
    
    
    private  void load() {
        try {
            
            properties = new Properties();
            
            for (int i=0; i< defaults.length; i++)
                properties.setProperty(defaults[i][0], defaults[i][1] );
            
            
            FileInputStream in = null;
            in = new FileInputStream(propertiesFilename);
            properties.load(in);
            
        } catch (java.io.FileNotFoundException e) {
            
            System.err.println("Can't find properties file: " + propertiesFilename + ". " +
            "Using defaults.");
            
        } catch (java.io.IOException e) {
            
            System.err.println("Can't read properties file: " + propertiesFilename + ". " +
            "Using defaults.");
            
        }
        
    }
    
    
    public int getMinPool() { return Integer.parseInt(properties.getProperty("MinPool")); }
    public int getMaxPool() { return Integer.parseInt(properties.getProperty("MaxPool")); }
    public int getMinLoginPool() { return Integer.parseInt(properties.getProperty("MinLoginPool")); }
    public int getMaxLoginPool() { return Integer.parseInt(properties.getProperty("MaxLoginPool")); }
    public int getServerPort() { return Integer.parseInt(properties.getProperty("ServerPort")); }
    
    public String getServerHost() { return properties.getProperty("ServerHost"); }
    
    public String getBindDN() { return properties.getProperty("BindDN"); }
    public String getBindPW() { return properties.getProperty("BindPW"); }
    
    public boolean getUseAuth() { return Boolean.valueOf( properties.getProperty("UseAuth").trim() ).booleanValue(); }
  
}

