/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
