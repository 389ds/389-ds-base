/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef _WIN32
#include <sys/param.h>  /* MAXPATHLEN */
#endif

#include "../plugins/rever/rever.h"
#include "getopt_ext.h"

static void usage(char *name)
{
	fprintf(stderr, "usage: %s -o 5.0InstancePath -n 5.1InstancePath -c 5.0Credential\n", name);
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

/* Script used during 5.0 to 5.1 migration: replication and
	chaining backend credentials must be converted.
	
   Assumption: the built-in des-plugin.so lib has been used 
	in 5.0 and is used in 5.1

   Usage: migrateCred 
				-o <5.0 instance path> 
				-n <5.1 instance path> 
				-c <5.0 credential, with prefix>

   Return 5.1 credential with prefix
*/

int
main( int argc, char **argv)
{
	char *cmd = argv[0];
	char *oldpath = NULL;
	char *newpath = NULL;
	char *prefixCred = NULL;
	char *cred = NULL;

	char *newcred = NULL;
	migrate_fn_type fct = NULL;
	char libpath[MAXPATHLEN];
	char *shared_lib;

	char *opts = "o:n:c:";
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
	shared_lib = ".sl";
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

	sprintf(libpath, "%s/../lib/des-plugin%s", newpath, shared_lib);

	fct = (migrate_fn_type)sym_load(libpath, "migrateCredentials",
			"DES Plugin", 1 /* report errors */ );
	if ( fct == NULL )
	{
		return(1);
	}

	newcred = (fct)(oldpath, newpath, cred);

	fprintf(stdout, "%s", newcred);

	return(0);
	
}
