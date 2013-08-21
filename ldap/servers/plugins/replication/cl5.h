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

/* cl5.h - changelog related function */

#ifndef CL5_H
#define CL5_H

#include "cl5_api.h"	/* changelog access APIs */

typedef struct changelog5Config
{
	char *dir;
/* These 2 parameters are needed for changelog trimming. Already present in 5.0 */
	char *maxAge;
	int	 maxEntries;
/* the changelog DB configuration parameters are defined as CL5DBConfig in cl5_api.h */
	CL5DBConfig dbconfig;	
	char *symmetricKey;
	int	 compactInterval;
}changelog5Config;

/* initializes changelog*/
int changelog5_init();
/* cleanups changelog data */
void changelog5_cleanup();
/* initializes changelog configurationd */
int changelog5_config_init();
/* cleanups config data */
void changelog5_config_cleanup();
/* reads changelog configuration */
int changelog5_read_config (changelog5Config *config); 
/* cleanups the content of the config structure */
void changelog5_config_done (changelog5Config *config);
/* frees the content and the config structure */
void changelog5_config_free (changelog5Config **config);

#define MAX_TRIALS			50				/* number of retries on db operations */

#endif
