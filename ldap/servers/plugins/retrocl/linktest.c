/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* This is a test program.  Not linked into the shared library */

#include "retrocl.h"

int main(int a,char **b)
{
	int r;

	r = retrocl_plugin_init(NULL);
}
