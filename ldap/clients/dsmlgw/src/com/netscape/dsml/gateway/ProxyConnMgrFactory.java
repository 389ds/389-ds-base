/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

/**
 * This provides a factory interface to the proxy connection manager. In the
 * proxy mode, the user connections are bound to the directory server using a
 * proxy user.
 */

public class ProxyConnMgrFactory implements IConnMgrFactoryFunctor {
    
    public ProxyConnMgrFactory() {
    }
    
    /**
     * @return An instance of the connection manager
     *
     * @exception DSMLConfigException if unable to initialize.
     */
    public synchronized IConnectionManager getInstance() {
 
        if (_proxyConnMgr == null) {
            _proxyConnMgr = ProxyConnectionManager.getInstance();
        }
        return _proxyConnMgr;
    }
    private ProxyConnectionManager _proxyConnMgr= null;
    
  
    
    
}
