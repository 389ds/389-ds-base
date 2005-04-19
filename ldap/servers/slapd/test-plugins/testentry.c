/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/************************************************************

 testentry.c

 This source file provides examples of entry store and
 entry fetch plug-in functions.  These plug-in functions are
 called by the server before writing an entry to disk and
 after reading an entry from disk.

 Entry store and entry fetch plug-in functions are passed 
 the string representation (in LDIF -- LDAP Data Interchange 
 Format) of the entry.

 In this example, the entry store function performs a bitwise
 exclusive-OR operation on each character in the entry 
 against the value 0xaa (10101010).  The entry fetch 
 function performs this again to revert each character 
 back to its initial value.

 NOTE: The Directory Server caches recently added and retrieved
 entries in memory.  The entry fetch plug-in function is called 
 only when reading the entry from the disk, *not* when reading
 the entry from the cache.  

 For example, if you add an entry and search for it, you will 
 not see a message in the server error log indicating that 
 the entry fetch plug-in function was called. In the process 
 of adding the entry to the directory, the server also added 
 the entry to the cache; the server then reads the entry from 
 the cache instead of from the disk and does not need to call 
 the entry fetch plug-in function.

 You can flush the cache by shutting down the server. 

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

dn: cn=Test entry,cn=plugins,cn=config
objectClass: top
objectClass: nsSlapdPlugin
objectClass: extensibleObject
cn: Test entry
nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
nsslapd-pluginInitfunc: testentry_init
nsslapd-pluginType: ldbmentryfetchstore
nsslapd-pluginEnabled: on
nsslapd-pluginId: test-entry

 ************************************************************/

#include <stdio.h>
#include <string.h>
#include "slapi-plugin.h"

Slapi_PluginDesc entrypdesc = { "test-entry", "Netscape", "0.5",
	"sample entry modification plugin" };

/* Entry store plug-in function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testentry_scramble( char **entry, unsigned long *len )
{
	unsigned long	i;

	/* Log an entry to the server's error log file whenever
	   this function is called. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testentry_scramble",  
		"Entry data scrambled.\n" );

	/* Perform a bitwise exclusive-OR operation on each
	   character in the entry. */
	for ( i = 0; i < *len - 1; i++ ) {
		(*entry)[i] ^= 0xaa;
	}

	return( 0 );
}

/* Entry fetch plug-in function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testentry_unscramble( char **entry, unsigned long *len )
{
	unsigned long	i;

	/* Some entries will not be scrambled, so check if the entry is
	   scrambled before attempting to unscramble the entry. */
	if ( !strncmp( *entry, "dn:", 3 ) ) {
		return( 0 );
	}

	/* Perform a bitwise exclusive-OR operation on each
	   character in the entry. */
	for ( i = 0; i < *len - 1; i++ ) {
		(*entry)[i] ^= 0xaa;
	}

	slapi_log_error( SLAPI_LOG_PLUGIN, "testentry_unscramble",
		"Entry data unscrambled.\n");
	return( 0 );
}

int 
testentry_init(Slapi_PBlock *pb)
{
	/* Register the store/fetch functions and specify
	   the server plug-in version. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, 
		SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, 
		(void *)&entrypdesc ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_ENTRY_FETCH_FUNC, 
		(void *) testentry_unscramble ) != 0 ||
	     slapi_pblock_set( pb, SLAPI_PLUGIN_ENTRY_STORE_FUNC, 
		(void *) testentry_scramble ) != 0 ) {

		slapi_log_error( SLAPI_LOG_PLUGIN, "testentry_init",
			"Failed to set version and functions\n" );
		return( -1 );
	}

	return 0;
}
