/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dynalib.c - dynamic library routines */

#include <stdio.h>
#include "prlink.h"
#include "slap.h"
#if defined(SOLARIS)
#include <dlfcn.h> /* dlerror */
#endif


static struct dynalib {
	char		*dl_name;
	PRLibrary	*dl_handle;
} **libs = NULL;

static void symload_report_error( char *libpath, char *symbol, char *plugin,
		int libopen );

void *
sym_load( char *libpath, char *symbol, char *plugin, int report_errors )
{
	return sym_load_with_flags(libpath, symbol, plugin, report_errors, PR_FALSE, PR_FALSE);
}

void *
sym_load_with_flags( char *libpath, char *symbol, char *plugin, int report_errors, PRBool load_now, PRBool load_global )
{
	int	i;
	void	*handle;
	PRLibSpec libSpec;
	unsigned int flags = PR_LD_LAZY; /* default PR_LoadLibrary flag */

	libSpec.type = PR_LibSpec_Pathname;
	libSpec.value.pathname = libpath;

	for ( i = 0; libs != NULL && libs[i] != NULL; i++ ) {
		if ( strcasecmp( libs[i]->dl_name, libpath ) == 0 ) {
			handle = PR_FindSymbol( libs[i]->dl_handle, symbol );
			if ( NULL == handle && report_errors ) {
				symload_report_error( libpath, symbol, plugin, 1 /* lib open */ );
			}
			return handle;
		}
	}

	if (load_now) {
		flags = PR_LD_NOW;
	}
	if (load_global) {
		flags |= PR_LD_GLOBAL;
	}

	if ( (handle = PR_LoadLibraryWithFlags( libSpec, flags )) == NULL ) {
		if ( report_errors ) {
			symload_report_error( libpath, symbol, plugin, 0 /* lib not open */ );
		}
		return( NULL );
	}

	libs = (struct dynalib **) slapi_ch_realloc( (char *) libs, (i + 2) *
	    sizeof(struct dynalib) );
	libs[i] = (struct dynalib *) slapi_ch_malloc( sizeof(struct dynalib) );
	libs[i]->dl_name = slapi_ch_strdup( libpath );
	libs[i]->dl_handle = handle;
	libs[ i + 1 ] = NULL;

	handle = PR_FindSymbol( libs[i]->dl_handle, symbol );
	if ( NULL == handle && report_errors ) {
		symload_report_error( libpath, symbol, plugin, 1 /* lib open */ );
	}
	return handle;
}


static void
symload_report_error( char *libpath, char *symbol, char *plugin, int libopen )
{
	char	*errtext = NULL;
	PRInt32	errlen, err;

	errlen = PR_GetErrorTextLength();
	if ( errlen > 0 ) {
		errtext = slapi_ch_malloc( errlen );
		if (( err = PR_GetErrorText( errtext )) > 0 ) {
			LDAPDebug( LDAP_DEBUG_ANY, SLAPI_COMPONENT_NAME_NSPR " error %d: %s\n",
				PR_GetError(), errtext, 0 );
		}
		slapi_ch_free( (void **)&errtext );
	}
	if ( libopen ) {
		LDAPDebug( LDAP_DEBUG_ANY,
			"Could not load symbol \"%s\" from \"%s\" for plugin %s\n",
					symbol, libpath, plugin );
	} else {
		LDAPDebug( LDAP_DEBUG_ANY,
			"Could not open library \"%s\" for plugin %s\n",
			libpath, plugin, 0 );
	}
}
