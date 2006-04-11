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
#if defined( _WIN32 )
#include <sys/stat.h> /* for S_IREAD and S_IWRITE */
#include <windows.h>
#include <time.h>
#include "proto-ntutil.h"
#else
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#if defined(LINUX) /* I bet other Unix would like
					* this flag. But don't want to
					* break other builds so far */
#include <unistd.h>
#endif
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "ldap.h"
#include "ldif.h"
#include "../slapi-plugin.h"
#include "../slap.h"
#include <nspr.h>
#include <nss.h>
#include "../../plugins/pwdstorage/pwdstorage.h"

int			ldap_syslog;
int			ldap_syslog_level;
int			slapd_ldap_debug = LDAP_DEBUG_ANY;
#ifdef _WIN32
int *module_ldap_debug;
#endif
int			detached;
FILE			*error_logfp;
FILE			*access_logfp;
struct pw_scheme	*pwdhashscheme;
int			heflag = 0;

static int slapd_config(const char *configdir);
static int entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value);

static void
usage( name )
char	*name;
{
    fprintf( stderr, "usage: %s -D instance-dir [-H] [-s scheme | -c comparepwd ] password...\n", name );
    exit( 1 );
}


/*
 * If global "heflag" is non-zero, un-hex-encode the string
 * and return a decoded copy.  Otherwise return a copy of the
 * string.
 */
static char *
decode( char *orig )
{
    char *r;

    if ( NULL == orig ) {
	return NULL;
    }
    r = calloc( 1, strlen( orig ) + 2 );
    strcpy( r, orig );

    if ( heflag ) {
	char *s;

	for ( s = r; *s != '\0'; ++s ) {
	    if ( *s == '%' && ldap_utf8isxdigit( s+1 ) && ldap_utf8isxdigit( s+2 )) {
		memmove( s, s + 1, 2 );
		s[ 2 ] = '\0';
		*s = strtoul( s, NULL, 16 );
		memmove( s + 1, s + 3, strlen( s + 3 ) + 1 );
	    }
	}
    }
    return r;
}


int
main( argc, argv )
    int		argc;
    char	*argv[];
{
    int			i, rc;
    char		*enc, *cmp, *name;
    struct pw_scheme	*pwsp, *cmppwsp;
    extern int		optind;
    char 		*cpwd = NULL;	/* candidate password for comparison */
	char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
	slapdFrontendConfig_t *slapdFrontendConfig =  NULL;

	char *opts = "Hs:c:D:";
	char *instancedir = NULL;
    name = argv[ 0 ];
    pwsp = cmppwsp = NULL;

#ifdef _WIN32
	module_ldap_debug = &slapd_ldap_debug;
	libldap_init_debug_level(&slapd_ldap_debug);
#endif

	PR_Init( PR_USER_THREAD, PR_PRIORITY_NORMAL, 0 );
	
	/* Initialize NSS to make ds_salted_sha1_pw_enc() work */
 	if (NSS_NoDB_Init(NULL) != SECSuccess) {
 		fprintf( stderr, "Fatal error: unable to initialize the NSS subcomponent." );
 		return( 1 );
 	}
	

	while (( i = getopt( argc, argv, opts )) != EOF ) {
		switch ( i ) {
		case 'D':
			/* kexcoff: quite the same as slapd_bootstrap_config */
			FrontendConfig_init();

			instancedir = rel2abspath( optarg );
			if ( config_set_instancedir( "configdir (-D)", instancedir,
						errorbuf, 1) != LDAP_SUCCESS ) {
				fprintf( stderr, "%s\n", errorbuf );
				return( 1 );
			}
			slapi_ch_free((void **)&instancedir);


			slapdFrontendConfig = getFrontendConfig();
			if (0 == slapd_config(slapdFrontendConfig->configdir)) {
				fprintf(stderr,
						 "The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
						slapdFrontendConfig->configdir);
				return(1);
			}
			break;

		case 's':	/* set hash scheme */
			if (!slapdFrontendConfig) {
				usage( name );
				return( 1 );
			}
			if (( pwsp = pw_name2scheme( optarg )) == NULL ) {
			fprintf( stderr, "%s: unknown hash scheme \"%s\"\n", name,
				optarg );
			return( 1 );
			}
			break;

		case 'c':	/* compare encoded password to password */
			if (!slapdFrontendConfig) {
				usage( name );
				return( 1 );
			}
			cpwd = optarg;
			break;

		case 'H':	/* password(s) is(are) hex-encoded */
			if (!slapdFrontendConfig) {
				usage( name );
				return( 1 );
				}
				heflag = 1;
				break;

		default:
			usage( name );
		}
    }

	if (!slapdFrontendConfig) {
		usage( name );
		return( 1 );
	}

    if ( cpwd != NULL ) {
	cmppwsp = pw_val2scheme( decode( cpwd ), &cmp, 1 );
    }
    
    if ( cmppwsp != NULL && pwsp != NULL ) {
	fprintf( stderr, "%s: do not use -s with -c\n", name );
	usage( name );
    }

    if ( cmppwsp == NULL && pwsp == NULL ) {
	pwsp = pw_name2scheme( SALTED_SHA1_SCHEME_NAME );
    }

    if ( argc <= optind ) {
	usage( name );
    }

    if ( cmppwsp == NULL && pwsp->pws_enc == NULL ) {
	fprintf( stderr,
		"The scheme \"%s\" does not support password encoding.\n",
		pwsp->pws_name );
	return( 1 );
    }

    srand((int)time(NULL));	/* schemes such as crypt use random salt */

    for ( rc = 0; optind < argc && rc == 0; ++optind ) {
		if ( cmppwsp == NULL ) {	/* encode passwords */
			if (( enc = (*pwsp->pws_enc)( decode( argv[ optind ] ))) == NULL ) {
			perror( name );
			return( 1 );
			}

			puts( enc );
			free( enc );
		} else {		/* compare passwords */
			if (( rc = (*(cmppwsp->pws_cmp))( decode( argv[ optind ]), cmp )) == 0 ) {
			printf( "%s: password ok.\n", name );
			} else {
			printf( "%s: password does not match.\n", name );
			}
		}
    }

    return( rc == 0 ? 0 : 1 );
}

/* -------------------------------------------------------------- */

/*
	kexcoff: quite similar to slapd_bootstrap_config() from the server,
	but it only loads password storage scheme plugins
 */
static int
slapd_config(const char *configdir)
{
	char configfile[MAXPATHLEN+1];
    PRFileInfo prfinfo;
    int rc = 0; /* Fail */
	int done = 0;
    PRInt32 nr = 0;
	PRFileDesc *prfd = 0;
	char *buf = 0;
	char *lastp = 0;
	char *entrystr = 0;

	PR_snprintf(configfile, sizeof(configfile), "%s/%s", configdir, CONFIG_FILENAME);
	if ( (rc = PR_GetFileInfo( configfile, &prfinfo )) != PR_SUCCESS )
	{
		fprintf(stderr,
				"The given config file %s could not be accessed, error %d\n",
				configfile, rc);
		exit( 1 );
	}
	else if (( prfd = PR_Open( configfile, PR_RDONLY,
							   SLAPD_DEFAULT_FILE_MODE )) == NULL )
	{
		fprintf(stderr,
				"The given config file %s could not be read\n",
				configfile);
		exit( 1 );
	}
	else
	{
		/* read the entire file into core */
		buf = slapi_ch_malloc( prfinfo.size + 1 );
		if (( nr = slapi_read_buffer( prfd, buf, prfinfo.size )) < 0 )
		{
			fprintf(stderr,
					"Could only read %d of %d bytes from config file %s\n",
					nr, prfinfo.size, configfile);
			exit( 1 );
		}
                          
		(void)PR_Close(prfd);
		buf[ nr ] = '\0';

		if(!done)
		{
			/* Convert LDIF to entry structures */
			Slapi_DN plug_dn;
			slapi_sdn_init_dn_byref(&plug_dn, PLUGIN_BASE_DN);
			while ((entrystr = dse_read_next_entry(buf, &lastp)) != NULL)
			{
				/*
				 * XXXmcs: it would be better to also pass
				 * SLAPI_STR2ENTRY_REMOVEDUPVALS in the flags, but
				 * duplicate value checking requires that the syntax
				 * and schema subsystems be initialized... and they
				 * are not yet.
				 */
				Slapi_Entry	*e = slapi_str2entry(entrystr,
									SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF);
				if (e == NULL)
				{
					  fprintf(stderr,
							"The entry [%s] in the configfile %s was empty or could not be parsed\n",
							entrystr, configfile);
					continue;
				}

				/* see if the entry is a child of the plugin base dn */
				if (slapi_sdn_isgrandparent(&plug_dn,
									   slapi_entry_get_sdn_const(e)))
				{
					if ( entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE, "pwdstoragescheme"))
					{
						/* add the syntax/matching/pwd storage scheme rule plugin */
						if (plugin_setup(e, 0, 0, 1))
						{
							fprintf(stderr,
									"The plugin entry [%s] in the configfile %s was invalid\n",
									slapi_entry_get_dn(e), configfile);
							exit(1); /* yes this sucks, but who knows what else would go on if I did the right thing */
						}
						else
						{
							e = 0; /* successful plugin_setup consumes entry */
						}
					}
				}

				if (e)
					slapi_entry_free(e);
			}

			/* kexcoff: initialize rootpwstoragescheme and pw_storagescheme
			 *			if not explicilty set in the config file
			 */
			config_set_storagescheme();

			slapi_sdn_done(&plug_dn);
			rc= 1; /* OK */
		}

		slapi_ch_free((void **)&buf);
	}

	return rc;
}

/*
	kexcoff: direclty copied fron the server code
  See if the given entry has an attribute with the given name and the
  given value; if value is NULL, just test for the presence of the given
  attribute; if value is an empty string (i.e. value[0] == 0),
  the first value in the attribute will be copied into the given buffer
  and returned
*/
static int
entry_has_attr_and_value(Slapi_Entry *e, const char *attrname,
                         char *value)
{
    int retval = 0;
    Slapi_Attr *attr = 0;
    if (!e || !attrname)
        return retval;

    /* see if the entry has the specified attribute name */
    if (!slapi_entry_attr_find(e, attrname, &attr) && attr)
    {
        /* if value is not null, see if the attribute has that
           value */
        if (!value)
        {
            retval = 1;
        }
        else
        {
            Slapi_Value *v = 0;
            int index = 0;
            for (index = slapi_attr_first_value(attr, &v);
                 v && (index != -1);
                 index = slapi_attr_next_value(attr, index, &v))
            {
                const char *s = slapi_value_get_string(v);
                if (!s)
                    continue;

                if (!*value)
                {
                    strcpy(value, s);
                    retval = 1;
                    break;
                }
                else if (!strcasecmp(s, value))
                {
                    retval = 1;
                    break;
                }
            }    
        }
    }    

    return retval;
}

