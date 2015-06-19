/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
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

#include <sys/param.h>  /* MAXPATHLEN */

#include "../plugins/rever/rever.h"
#include "getopt_ext.h"

static void usage(char *name)
{
	fprintf(stderr, "usage: %s -o OldInstancePath -n NewInstancePath -c OldCredential [-p NewPluginPath]\n", name);
    fprintf(stderr, "New plugin path defaults to [%s] if not given\n", PLUGINDIR);
	exit(1);
}

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

				break;
			case 'n':
				newpath = strdup(optarg);
				break;
			case 'c':
				{
					char *end = NULL;
					int namelen;

					/* cred has the prefix, remove it before decoding */
					prefixCred = strdup(optarg);
					
					if (prefixCred && (*prefixCred == PWD_HASH_PREFIX_START) &&
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
				break;
			default: 
				usage(cmd);
		}
	}

	if ( !oldpath || !newpath || !cred )
	{
		free(oldpath);
		free(newpath);
		free(prefixCred);
		free(pluginpath);
		usage(cmd);
	}


#ifdef HPUX
#ifdef __ia64
	shared_lib = ".so";
#else
	shared_lib = ".sl";
#endif
#else
	shared_lib = ".so";
#endif

	if (!pluginpath) {
		pluginpath = strdup(PLUGINDIR);
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
		free(oldpath);
		free(newpath);
		free(prefixCred);
		free(pluginpath);
		usage(cmd);
		return(1);
	}

	newcred = (fct)(oldpath, newpath, cred);

	fprintf(stdout, "%s", newcred);
	free(oldpath);
	free(newpath);
	free(prefixCred);
	free(pluginpath);
	return(0);
	
}
