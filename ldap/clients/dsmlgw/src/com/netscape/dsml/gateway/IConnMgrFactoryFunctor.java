/*
 * Copyright 2004 Netscape Communications Corporation.
 * All rights reserved.
 *
 * 
 * 
 */
package com.netscape.dsml.gateway;

/**
 * This interface defines the factory interfaces. Each connection manager implements
 * must use the factory interface. The design patterns used are: Abstract Factory, 
 * Factory Method and Functor patterns in the GoF book.
 */

public interface IConnMgrFactoryFunctor 
{    
    /**
     * @return An instance of the connection manager factory
     */
    public IConnectionManager getInstance() throws Exception;      
} 
