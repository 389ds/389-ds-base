/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import java.lang.Exception ;
import java.util.*;
import java.util.logging.*;
import netscape.ldap.LDAPConnection;
import netscape.ldap.util.ConnectionPool;
import netscape.ldap.LDAPConstraints;
import netscape.ldap.LDAPSearchConstraints;
import netscape.ldap.controls.LDAPProxiedAuthControl;
import netscape.ldap.LDAPBind;
import netscape.ldap.LDAPException;
import java.io.IOException;

/**
 * ProxyConnectionManager is respossible for creating a pool of connections
 * to the directory server. The pool is initialized with configured set of
 * connections. Two pools are created; one for authentication and the other
 * for operations. The pool parameters (max and min) can be configured.
 *
 */
class ProxyConnectionManager implements IConnectionManager {
    /*
     * Default maximum backlog queue size
     */
    static final int MAX_BACKLOG = 100;
    static final String LDAP_MAXBACKLOG = "maxbacklog";
    static final String LDAP_REFERRAL = "referral";
    
    
    private static Logger logger =
    Logger.getLogger("com.netscape.dsml.service.ProxyConnectionManager");
    static private ConnectionPool _ldapPool = null;
    static private ConnectionPool _ldapLoginPool = null;
    static private LDAPConnection _trialConn = null;
    static private LDAPSearchConstraints _defaultSearchConstraints = null;
    static private ProxyConnectionManager m_instance = null;
    
    private String m_host = null;
    private int    m_port = 0;
    private String m_user = "";
    private String m_password = "";
    
    /**
     * Initialize by reading the informaton from the config manager.
     *
     * throws DSMLConfigException if unable to get the config
     *        information.
     */
    private ProxyConnectionManager()  {
        init();
    }
    
    
    private void init() {
        // Get an instance of the config manager
        Configuration config = Configuration.getInstance();
        
        m_host = config.getServerHost();
        m_port = config.getServerPort();
        m_user = config.getBindDN();
        m_password = config.getBindPW();
        
        logger.log(Level.INFO, "m_host: {0}", m_host);
        logger.log(Level.INFO, "m_port: {0}", String.valueOf(m_port) );
        logger.log(Level.INFO, "m_user: {0}", m_user);
        logger.log(Level.INFO, "m_password: {0}", m_password);
        
        logger.log(Level.FINER, "Initializing the ldap pool");
        initLdapPool( config.getMinPool(), config.getMaxPool());
        
        logger.log(Level.FINER, "Initializing the ldap LOGIN pool");
        initLoginPool(config.getMinLoginPool(), config.getMaxLoginPool());
        
        logger.log(Level.FINER, "Pool initialization done");
    }
    
    
    /**
     *  create the singelton LDAPLayer object if it doesn't exist already.
     *
     * @exception DSMLConfigException if unable initialize configurations.
     */
    public static synchronized ProxyConnectionManager getInstance( ) {
        if (m_instance == null) {
            m_instance = new ProxyConnectionManager();
        }
        return m_instance;
    }
    
    /**
     * Get a connection to authenticate.
     *
     * @return a ldap connection handle
     *
     */
    public LDAPConnection getLoginConnection() {
        if (_ldapLoginPool == null ) {
            return null;
        }
        return _ldapLoginPool.getConnection();
    }
    
    /**
     * Returns the connection (used for authentication) to the pool.
     *
     */
    public void releaseLoginConnection(String _loginCtx, LDAPConnection conn ) {
        // Release the connection
        _ldapLoginPool.close( conn );
    }
    
    public void releaseLoginConnection(LDAPConnection conn ) {
        // Release the connection
        _ldapLoginPool.close( conn );
    }
    
    /**
     * Get connection from pool. This connection is used for
     * all operations except authentication.
     *
     * @param the loginctx (or the authenticated token)
     * @return connection that is available to use or null otherwise
     */
    public LDAPConnection getConnection(String loginCtx) {
        
        // XXX this behaves poorly if the server goes down.
        if (_ldapPool == null ) {
            return null;
        }
        
        LDAPConnection conn ;
        
        conn= _ldapPool.getConnection();
        
        return conn;
    }
    
    
    public LDAPConnection getConnection() {
        return getConnection("");
    }
    
    /**
     * Just call the pool method to release the connection so that the
     * given connection is free for others to use
     *
     * @param the login ctx used.
     * @param conn connection in the pool to be released for others to use
     */
    public void releaseConnection( String loginCtx, LDAPConnection conn ) {
        // Since we return the connecton to the pool and use a
        //proxy mode, loginCtx is not used.
        // XXX should change function  signature
        
        if (_ldapPool == null || conn == null) return;
        
        // XXX reset the original constraints after before connection is released
       // conn.setSearchConstraints(_defaultSearchConstraints);
        
        // A soft close on the connection.
        // Returns the connection to the pool and make it available.
        _ldapPool.close( conn );
    }
    
    
    public void releaseConnection( LDAPConnection conn ) {
        if (_ldapPool == null || conn == null)
            return;
        
        // reset the original constraints
        conn.setSearchConstraints(_defaultSearchConstraints);
        
        // A soft close on the connection.
        // Returns the connection to the pool and make it available.
        _ldapPool.close( conn );
    }
    
    
    /**
     * Initialize the pool shared by all. It is expected that the
     * host and port (and all configuration) information has been
     * initialized.
     */
    private synchronized void initLdapPool(int poolMin, int poolMax) {
        // Don't do anything if pool is already initialized
        if (_ldapPool != null) {
            logger.log(Level.FINER, "The pool is already initialized");
            return;
        }
        int maxBackLog   = 10;
        boolean referrals = false;
        
        try {
            logger.log(Level.FINER, "Host={0}", m_host);
            logger.log(Level.FINER, "Port={0}", String.valueOf(m_port) );
            logger.log(Level.FINER, "DN={0}", m_user);
            logger.log(Level.FINE,  "password={0}", m_password);
            
            _ldapPool = new ConnectionPool(poolMin, poolMax, m_host, m_port, m_user, m_password);
            
            logger.log(Level.FINER, "Pool initialized");
         
        } catch (LDAPException lde) {
            _ldapPool = null;
            logger.log(Level.SEVERE, "Pool not initialized\nError Code: " +
            lde.getLDAPResultCode() + "\n" + lde.getMessage() );
            
        } catch (Exception ex) {
            //logger.log(Level.SEVERE, "Pool init failed:{0}", ex.getMessage());
            //XXX throw new Exception("couldn't connect to ldap server");
            
        }
    }
    
    /*
     * Initialize the login pool. This pool of connections is used
     * for authentication.
     */
    private synchronized void initLoginPool(int poolMin, int poolMax) {
        if ( _ldapLoginPool != null)
            return;
        
        LDAPConnection conn = new LDAPConnection();
        logger.log(Level.FINER, "Creating the LOGIN Pool");
        try {
            conn.connect(3, m_host, m_port, m_user, m_password);
            _ldapLoginPool = new ConnectionPool(poolMin, poolMax, m_host, m_port, m_user, m_password);
            
        } catch (LDAPException lde) {
            _ldapLoginPool = null;
            logger.log(Level.SEVERE, "Pool not initialized\nError Code: " +
            lde.getLDAPResultCode() + "\n" +
            lde.getMessage() );
            
        } catch (Exception ex) {
            //  logger.log(Level.SEVERE, "Pool init failed:{0}", ex.getMessage());
            //XXX  throw new Exception("couldn't connect to ldap server");
            _ldapLoginPool = null;
        }
    }
    
    public void shutdown() {
        try {
        _ldapPool.destroy();
        } catch (java.lang.NullPointerException e ) {}
        try {
        _ldapLoginPool.destroy();
          } catch (java.lang.NullPointerException e) {}
    }
    
    
}
