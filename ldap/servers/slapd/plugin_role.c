/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * plugin_role.c - routines for calling roles plugins
 */

#include "slap.h"

static roles_check_fn_type roles_check_exported = NULL;

int slapi_role_check(Slapi_Entry *entry_to_check, Slapi_DN *role_dn, int *present)
{
	int rc = 0;

	if ( roles_check_exported != NULL )
	{
		rc = (roles_check_exported)(entry_to_check, role_dn, present);
	}

	return rc;
}

void slapi_register_role_check(roles_check_fn_type check_fn)
{
	roles_check_exported = check_fn;
}
