/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 *
 */
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

