/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: unbind.c
 *
 *  Functions:
 * 
 *      ldif_back_unbind() - ldap ldif back-end unbind routine
 *
 */
#include "back-ldif.h"

/*
 *  Function: ldif_back_unbind
 *
 *  Returns: returns 0 
 *  
 *  Description: performs an ldap unbind.
 */
int
ldif_back_unbind( Slapi_PBlock *pb )
{
  return( 0 );
}
