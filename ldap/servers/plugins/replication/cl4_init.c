/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* cl4_init.c - implments initialization/cleanup functions for 
                4.0 style changelog
 */

#include <string.h>

#include "slapi-plugin.h"
#include "cl4.h"
#include "repl.h"

/* forward declarations */
static int changelog4_create_be();
static int changelog4_start_be ();
static int changelog4_close();
static int changelog4_remove();

/*
 * Initialise the 4.0 Changelog
 */
int changelog4_init ()
{
    int rc= 0; /* OK */
    Slapi_Backend *rbe;
	changeNumber first_change = 0UL, last_change = 0UL;
	int lderr;
	
    if (changelog4_create_be() < 0 )
	{
		rc= -1;
	}
	else
	{
	rc = changelog4_start_be ();
	}

    if(rc == 0)
	{
        rbe = get_repl_backend();
    	if(rbe!=NULL)
    	{
    	    /* We have a Change Log. Check it's valid. */
			/* changelog has to be started before its 
			   data version can be read */ 
            const char *sdv= get_server_dataversion();
    	    const char *cdv= get_changelog_dataversion();
			char *suffix = changelog4_get_suffix ();
			if(!cdv || strcmp(sdv,cdv)!=0)
			{
				
			    /* The SDV and CDV are not the same.  The Change Log is invalid. 
			       It must be removed. */
				/* ONREPL - currently we go through this code when the changelog
				   is first created because we can't tell new backend from the
				   existing one.*/
				rc	= changelog4_close();
				rc	= changelog4_remove();

				/* now restart the changelog */
				changelog4_start_be ();

				create_entity( suffix, "extensibleobject");
                /* Write the Server Data Version onto the changelog suffix entry */
                /* JCMREPL - And the changelog database version number */
                set_changelog_dataversion(sdv);
				slapi_ch_free ((void **)&suffix);

			}
		}
	}
    
	if(rc != 0)
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, 
						"An error occurred configuring the changelog database\n" );
    }

	first_change = replog_get_firstchangenum( &lderr );
	last_change = replog_get_lastchangenum( &lderr );
	ldapi_initialize_changenumbers( first_change, last_change );

    return rc;
}

static int

changelog4_close()
{
	int rc= 0 /* OK */;
    Slapi_Backend *rbe= get_repl_backend();
	Slapi_PBlock *pb = slapi_pblock_new ();	
	IFP closefn = NULL;

	rc = slapi_be_getentrypoint (rbe, SLAPI_PLUGIN_CLOSE_FN, (void**)&closefn, pb);
	if (rc != 0)
	{
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: backend close entry point is missing. "
                        "Replication subsystem disabled.\n");
		slapi_pblock_destroy (pb);
		set_repl_backend( NULL );
		return -1;
	}

	rc = closefn (pb);

	if (rc != 0)
	{
		
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Error: the changelog database could "
    		"not be closed.  Replication subsystem disabled.\n");
		set_repl_backend( NULL );
		rc = -1;
	}

	slapi_pblock_destroy (pb);
	return rc;

}

static int
changelog4_remove()
{
    int rc= 0 /* OK */;
    Slapi_Backend *rbe= get_repl_backend();
	Slapi_PBlock *pb = slapi_pblock_new ();	
	IFP rmdbfn = NULL;

	rc = slapi_be_getentrypoint (rbe, SLAPI_PLUGIN_DB_RMDB_FN, (void**)&rmdbfn, pb);
	if (rc != 0)
	{
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: backend rmdb entry point is missing. "
                        "Replication subsystem disabled.\n");
		slapi_pblock_destroy (pb);
		set_repl_backend( NULL );
		return -1;
	}

	rc = rmdbfn (pb);

	if (rc != 0)
	{
		
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, "Error: the changelog database could "
    		"not be removed.  Replication subsystem disabled.\n");
		rc = -1;
	}
	else
	{
    	slapi_log_error( SLAPI_LOG_REPL, repl_plugin_name, "New database generation computed. "
    		"Changelog database removed.\n");
	}

	slapi_pblock_destroy (pb);
	return rc;
}

static Slapi_Backend *repl_backend = NULL;

Slapi_Backend
*get_repl_backend()
{
	return repl_backend;
}

void
set_repl_backend(Slapi_Backend *be)
{
	repl_backend = be;
}


int changelog4_shutdown ()
{
	/* ONREPL - will shutdown the backend */
	int rc = 1;

	return rc;
}

static void changelog4_init_trimming ()
{
	char *cl_maxage = changelog4_get_maxage ();
	unsigned long cl_maxentries = changelog4_get_maxentries ();
	time_t ageval = age_str2time (cl_maxage);
	
	slapi_ch_free ((void **)&cl_maxage);

    init_changelog_trimming(cl_maxentries, ageval );
}



/*
 * Function: changelog4_create_be
 * Arguments: none
 * Returns: 0 on success, non-0 on error
 * Description: configures changelog backend instance.  
 */

static int
changelog4_create_be()
{
	int i, dir_count = 5;
    Slapi_Backend *rbe;
	slapi_be_config config;
	char *cl_dir = changelog4_get_dir ();
	char *cl_suffix;

    if ( cl_dir == NULL ) {
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: no directory specified for changelog database.\n");
        return -1;
    }
	
	cl_suffix = changelog4_get_suffix ();

    if ( cl_suffix == NULL ) {
		slapi_ch_free ((void **)&cl_dir);
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: no suffix specified for changelog database.\n");
        return -1;
    }

	/* setup configuration parameters for backend initialization */   
	config.type			= CHANGELOG_LDBM_TYPE;
	config.suffix		= cl_suffix;
	config.is_private	= 1;	/* yes */ 
	config.log_change	= 0;	/* no */
	config.directives = (slapi_config_directive*)slapi_ch_calloc(
                         dir_count, sizeof(slapi_config_directive));
    config.dir_count  = dir_count;

	for (i = 0; i < dir_count; i++)
	{
		config.directives[i].file_name = "(internal)";
		config.directives[i].lineno	= 0;	
	}

	/* setup indexes */
	config.directives[0].argv = NULL;
	config.directives[0].argc = 3;
	charray_add( &(config.directives[0].argv), slapi_ch_strdup( "index" ));
	charray_add( &(config.directives[0].argv), slapi_ch_strdup( attr_changenumber ));
	charray_add( &(config.directives[0].argv), slapi_ch_strdup( "eq" ));

    /* Set up the database directory */
    config.directives[1].argv = NULL;
    config.directives[1].argc = 2;
    charray_add( &(config.directives[1].argv), slapi_ch_strdup( "directory" ));
    charray_add( &(config.directives[1].argv), slapi_ch_strdup( cl_dir ));

    /* Override the entry cache size */
    config.directives[2].argv = NULL;
    config.directives[2].argc = 2;
    charray_add( &(config.directives[2].argv), slapi_ch_strdup( "cachesize" ));
    charray_add( &(config.directives[2].argv), slapi_ch_strdup( "10" ));

    /* Override the database cache size */
    config.directives[3].argv = NULL;
    config.directives[3].argc = 2;
    charray_add( &(config.directives[3].argv), slapi_ch_strdup( "dbcachesize" ));
    charray_add( &(config.directives[3].argv), slapi_ch_strdup( "1000000" ));
   
    /* Override the allids threshold */
    config.directives[4].argv = NULL;
    config.directives[4].argc = 2;
    charray_add( &(config.directives[4].argv), slapi_ch_strdup( "allidsthreshold" ));
	/* assumes sizeof(int) >= 32 bits */
    charray_add( &(config.directives[4].argv), slapi_ch_strdup( "2147483647" )); 

	/* rbe = slapi_be_create_instance(&config, LDBM_TYPE); */
	rbe= NULL;

	/* free memory allocated to argv */
	for (i = 0; i < dir_count; i++)
	{
		charray_free (config.directives[i].argv);
	}

	slapi_ch_free ((void **)&config.directives);
	slapi_ch_free ((void **)&cl_dir);
	slapi_ch_free ((void **)&cl_suffix);
 
	if (rbe == NULL)
	{
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: failed to create changelog backend. "
						"Replication disabled.\n");
		return -1;
	}

    set_repl_backend (rbe);

	changelog4_init_trimming ();

	return 0;   
}

/* Name: changelog4_start_be
 * Parameters: none
 * Return: 0 if successful, non 0 otherwise
 * Description: starts the changelog backend; backend must be configured
 *				first via call to changelog4_create_be
 */
static int
changelog4_start_be ()
{
	int rc;
	IFP startfn = NULL;
	Slapi_PBlock *pb;
	Slapi_Backend *rbe = get_repl_backend ();
	
	if (rbe)
	{
		pb = slapi_pblock_new();
		rc = slapi_be_getentrypoint(rbe, SLAPI_PLUGIN_START_FN, (void**)&startfn, pb);
		if (rc != 0)
		{
			slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
							"Error: backend start entry point is missing. "
							"Replication subsystem disabled.\n");
			slapi_pblock_destroy (pb);
			set_repl_backend( NULL );
			return -1;
		}

		rc = startfn (pb);
		slapi_pblock_destroy (pb);

		if (rc != 0)
		{
			slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name, 
						"Error: Failed to start changelog backend. "
                        "Replication subsystem disabled.\n");
			set_repl_backend( NULL );
			return -1;
		}
	}

	return 0;
}

