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


#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef _WIN32
#include <sys/param.h>  /* MAXPATHLEN */
#endif

#include "../plugins/rever/rever.h"
#include "getopt_ext.h"

static void usage(char *name)
{
	fprintf(stderr, "usage: %s -o OldInstancePath -n NewInstancePath -c OldCredential [-p NewPluginPath]\n", name);
    fprintf(stderr, "New plugin path defaults to [%s] if not given\n", PLUGINDIR);
	exit(1);
}
	
#ifdef _WIN32
/* converts '\' chars to '/' */
static void dostounixpath(char *szText)
{
    if(szText)
    {
        while(*szText)
        {
            if( *szText == '\\' )
                *szText = '/';
            szText++;
        }
    }
}
#endif

/* Script used during migration: replication and
	chaining backend credentials must be converted.
	
   Assumption: the built-in des-plugin.so lib has been used 
	in the old version and is used in the new version

   Usage: migrateCred 
				-o <old instance path> 
				-n <new instance path> 
				-c <old credential, with prefix>

   Return new credential with prefix
*/

int
main( int argc, char **argv)
{
	char *cmd = argv[0];
	char *oldpath = NULL;
	char *newpath = NULL;
	char *pluginpath = NULL;
	char *prefixCred = NULL;
	char *cred = NULL;

	char *newcred = NULL;
	migrate_fn_type fct = NULL;
	char libpath[MAXPATHLEN];
	char *shared_lib;

	char *opts = "o:n:c:p:";
	int i;

	while (( i = getopt( argc, argv, opts )) != EOF ) 
	{
		switch (i) 
		{
			case 'o':
				oldpath = strdup(optarg);
#ifdef _WIN32
				dostounixpath(oldpath);
#endif /* _WIN32 */

				break;
			case 'n':
				newpath = strdup(optarg);
#ifdef _WIN32
				dostounixpath(newpath);
#endif /* _WIN32 */
				break;
			case 'c':
				{
					char *end = NULL;
					int namelen;

					/* cred has the prefix, remove it before decoding */
					prefixCred = strdup(optarg);
					
					if ((*prefixCred == PWD_HASH_PREFIX_START) &&
						((end = strchr(prefixCred, PWD_HASH_PREFIX_END)) != NULL) &&
						((namelen = end - prefixCred - 1 ) <= (3*PWD_MAX_NAME_LEN)) )
					{
						cred = prefixCred + namelen + 2;
					}
					else
					{
						fprintf(stderr, "Invalid -c argument: %s  (wrong prefix?).\n", prefixCred);
					}
				}
				break;
			case 'p':
				pluginpath = strdup(optarg);
#ifdef _WIN32
				dostounixpath(pluginpath);
#endif /* _WIN32 */

				break;
			default: 
				usage(cmd);
		}
	}

	if ( !oldpath || !newpath || !cred )
	{
		usage(cmd);
	}


#if defined( XP_WIN32 )
	shared_lib = ".dll";
#else
#ifdef HPUX
#ifdef __ia64
	shared_lib = ".so";
#else
	shared_lib = ".sl";
#endif
#else
#ifdef AIX
#if OSVERSION >= 4200
	shared_lib = ".so";
#else
	shared_lib = "_shr.a";
#endif
#else
	shared_lib = ".so";
#endif
#endif
#endif

	if (!pluginpath) {
		pluginpath = strdup(PLUGINDIR);
#ifdef _WIN32
		dostounixpath(pluginpath);
#endif /* _WIN32 */
	}

	if (access(pluginpath, R_OK)) {
		snprintf(libpath, sizeof(libpath), "%s/../lib/des-plugin%s", newpath, shared_lib);
		libpath[sizeof(libpath)-1] = 0;
	} else {
		snprintf(libpath, sizeof(libpath), "%s/libdes-plugin%s", pluginpath, shared_lib);
		libpath[sizeof(libpath)-1] = 0;
	}        

	fct = (migrate_fn_type)sym_load(libpath, "migrateCredentials",
			"DES Plugin", 1 /* report errors */ );
	if ( fct == NULL )
	{
		usage(cmd);
		return(1);
	}

	newcred = (fct)(oldpath, newpath, cred);

	fprintf(stdout, "%s", newcred);

	return(0);
	
}
