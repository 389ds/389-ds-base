/** BEGIN COPYRIGHT BLOCK
 * Copyright 2005 Red Hat
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * pam_ptdebug.c - debugging-related code for PAM Pass Through Authentication
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "pam_passthru.h"

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif
