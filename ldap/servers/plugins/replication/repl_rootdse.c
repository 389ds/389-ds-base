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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include <string.h>

#include "repl.h"
#include "cl4.h"
#include "slapi-plugin.h"
 
/* Forward Declartions */
static int repl_rootdse_search (Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

int
repl_rootdse_init()
{
    /* The FE DSE *must* be initialised before we get here */
    int return_value= LDAP_SUCCESS;
	
	slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,"",LDAP_SCOPE_BASE,"(objectclass=*)",repl_rootdse_search,NULL); 

    return return_value;
}

static int
repl_rootdse_search(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{

#if 0
	struct berval val;
	struct berval *vals[2];
	vals[0] = &val;
	vals[1] = NULL;
 
	/* machine data suffix */
	val.bv_val = REPL_CONFIG_TOP;
	val.bv_len = strlen( val.bv_val );
	slapi_entry_attr_replace( e, ATTR_NETSCAPEMDSUFFIX, vals );

	/* Changelog information */
/* ONREPL because we now support multiple 4.0 changelogs we no longer publish
   info in the rootdse */
	if ( get_repl_backend() != NULL )
	{
		char buf[BUFSIZ];
	    changeNumber cnum;

	    /* Changelog suffix */
	    val.bv_val = changelog4_get_suffix ();
	    if ( val.bv_val != NULL )
		{
    		val.bv_len = strlen( val.bv_val );
    		slapi_entry_attr_replace( e, "changelog", vals );
	    }
		slapi_ch_free ((void **)&val.bv_val);

	    /* First change number contained in log */
	    cnum = ldapi_get_first_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "firstchangenumber", vals );

	    /* Last change number contained in log */
	    cnum = ldapi_get_last_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "lastchangenumber", vals );
	}
#endif

	return SLAPI_DSE_CALLBACK_OK;
}



