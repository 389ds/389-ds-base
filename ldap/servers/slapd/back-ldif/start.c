/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: start.c
 *
 *  Functions:
 * 
 *      ldif_back_start() - ldap ldif back-end start routine
 *
 */
#include "back-ldif.h"

/*
 *  Function: ldif_back_start
 *
 *  Returns: returns 0 
 *  
 *  Description: After the config file is read, the backend start function is called.
 *               This allows the backend writer to start any threads or perform any
 *               operations that need to be done after the config file has been read in.
 *               The ldif backend requires no such operations to be performed.
 *               
 */
int
ldif_back_start( Slapi_PBlock *pb )
{
  return( 0 );
}
