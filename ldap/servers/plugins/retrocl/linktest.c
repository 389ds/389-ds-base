/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* This is a test program.  Not linked into the shared library */

#include "retrocl.h"

int main(int a,char **b)
{
	int r;

	r = retrocl_plugin_init(NULL);
}
