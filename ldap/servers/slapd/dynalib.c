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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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

static void symload_report_error( const char *libpath, char *symbol, char *plugin,
		int libopen );

static void free_plugin_name(char *name)
{
	PR_smprintf_free(name);
}

void *
sym_load( char *libpath, char *symbol, char *plugin, int report_errors )
{
	return sym_load_with_flags(libpath, symbol, plugin, report_errors, PR_FALSE, PR_FALSE);
}

/* libpath is the pathname from the plugin config entry - it may be an absolute path
   or a relative path.  It does not have to have the shared lib/dll suffix.  The
   PR_GetLibraryName function will create the correct library name and path, including
   the correct shared library suffix for the platform.  So, for example, if you just
   pass in "libacl-plugin" as the libpath, and you are running on linux, the code
   will first test for the existence of "libacl-plugin", then will construct the full
   pathname to load as "PLUGINDIR/libacl-plugin.so" where PLUGINDIR is set during
   build time to something like /usr/lib/brand/plugins.
*/
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

	if (PR_SUCCESS != PR_Access(libpath, PR_ACCESS_READ_OK)) {
		if (strncmp(libpath, PLUGINDIR, strlen(PLUGINDIR))) {
			libSpec.value.pathname = slapi_get_plugin_name(PLUGINDIR, libpath);
		} else {
			libSpec.value.pathname = slapi_get_plugin_name(NULL, libpath);
		}
		/* then just handle that failure case with symload_report_error below */
	}

	if ( (handle = PR_LoadLibraryWithFlags( libSpec, flags )) == NULL ) {
		if ( report_errors ) {
			symload_report_error( libSpec.value.pathname, symbol, plugin, 0 /* lib not open */ );
		}
		if (libSpec.value.pathname != libpath) {
			free_plugin_name((char *)libSpec.value.pathname); /* cast ok - allocated by slapi_get_plugin_name */
		}
		return( NULL );
	}

	libs = (struct dynalib **) slapi_ch_realloc( (char *) libs, (i + 2) *
	    sizeof(struct dynalib *) );
	libs[i] = (struct dynalib *) slapi_ch_malloc( sizeof(struct dynalib) );
	libs[i]->dl_name = slapi_ch_strdup( libpath );
	libs[i]->dl_handle = handle;
	libs[ i + 1 ] = NULL;

	handle = PR_FindSymbol( libs[i]->dl_handle, symbol );
	if ( NULL == handle && report_errors ) {
		symload_report_error( libSpec.value.pathname, symbol, plugin, 1 /* lib open */ );
	}
	if (libSpec.value.pathname != libpath) {
		free_plugin_name((char *)libSpec.value.pathname); /* cast ok - allocated by PR_GetLibraryName */
	}
	return handle;
}


static void
symload_report_error( const char *libpath, char *symbol, char *plugin, int libopen )
{
	char	*errtext = NULL;
	PRInt32	errlen;

	errlen = PR_GetErrorTextLength();
	if ( errlen > 0 ) {
		errtext = slapi_ch_malloc( errlen+1 );
		if ( PR_GetErrorText( errtext ) > 0 ) {
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
