/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* cl5_init.c - implments initialization/cleanup functions for 
                4.0 style changelog
 */

#include "slapi-plugin.h"
#include "cl5.h"
#include "repl5.h"

/* initializes changelog*/
int changelog5_init()
{
	int rc;
	changelog5Config config;

	rc = cl5Init ();
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"changelog5_init: failed to initialize changelog\n");
		return 1;
	}

	/* read changelog configuration */
	changelog5_config_init ();
	changelog5_read_config (&config);

	if (config.dir == NULL)
	{
		/* changelog is not configured - bail out */
		rc = 0; /* OK */
        goto done;
	}

	/* start changelog */
	rc = cl5Open (config.dir, &config.dbconfig);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
					"changelog5_init: failed to start changelog at %s\n", 
					config.dir);
		rc = 1;
        goto done;
	}	

	/* set trimming parameters */
	rc = cl5ConfigTrimming (config.maxEntries, config.maxAge);
	if (rc != CL5_SUCCESS)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name_cl, 
						"changelog5_init: failed to configure changelog trimming\n");
		rc = 1;
        goto done;
	}

	rc = 0;

done:
    changelog5_config_done (&config);
    return rc;
}

/* cleanups changelog data */
void changelog5_cleanup()
{
	/* close changelog */
	cl5Close ();
	cl5Cleanup ();

	/* cleanup config */
	changelog5_config_cleanup ();
}
