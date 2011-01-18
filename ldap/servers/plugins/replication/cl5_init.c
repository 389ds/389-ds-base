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
		/* Note: but still changelog needs to be initialized to allow it
		 * to configure after this point. (don't call cl5Cleanup) */
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
