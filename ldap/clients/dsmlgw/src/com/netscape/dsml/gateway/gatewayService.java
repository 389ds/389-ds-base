/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

import javax.activation.DataHandler;


/**
 * This is the dummy endpoint for axis. It doesn't actually do anything. The
 * "real" meat is in gatewayHandler.
 * @author elliot@bozemanpass.com
 */
public class gatewayService {
    
  
    public gatewayService() {
    }
    
    /**
     *
     * @param inputData
     * @return
     */    
    public javax.activation.DataHandler process( javax.activation.DataHandler inputData) {
        return inputData;
    }
    
    /**
     *
     * @param inputData the javax.activation.DataHandler "attachment" of the incoming xml data
     * from the SOAP client.
     * @return the javax.activation.DataHandler "attachment" of the outgoing xml data
     * (responses) to the SOAP client.
     */    
    public javax.activation.DataHandler batchRequest(javax.activation.DataHandler inputData) {
        return inputData;
    }
    
}
