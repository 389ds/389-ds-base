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

#include "cb.h"

int cb_back_test( Slapi_PBlock *pb )
{

	Slapi_Backend 		* be;
        cb_backend      	* cb;
	cb_backend_instance 	* inst;
	Slapi_PBlock 		* apb;
	int 			res;
	int 			rc=0;
	const			Slapi_DN *aSuffix=NULL;
	const char 		* aSuffixString;
	char 			* theTarget;
	

        slapi_pblock_get( pb, SLAPI_PLUGIN_PRIVATE, &cb );
        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
        inst = cb_get_instance(be);
	apb = slapi_pblock_new();

	/*
	** Try to open a connection to the farm server
	** Try to get a dummy entry BELOW the suffix managed
	** by the chaining backend, in case the local root is shared
	** across different backend
	*/

	printf("Begin test instance %s.\n",inst->inst_name);

        aSuffix = slapi_be_getsuffix(be,0);
        aSuffixString=slapi_sdn_get_dn(aSuffix);
        /* Remove leading white spaces */
        for (aSuffixString; *aSuffixString==' ';aSuffixString++) {}
	theTarget=slapi_ch_smprintf("cn=test,%s",aSuffixString);

	/* XXXSD make sure chaining allowed for this plugin... */
        slapi_search_internal_set_pb (apb, theTarget, LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, NULL, NULL,
		cb->identity,0 );
        slapi_search_internal_pb (apb);

	slapi_ch_free((void **)&theTarget);

	if ( NULL == apb ) {
		printf("Can't contact farm server. (Internal error).\n");
		rc=-1;
		goto the_end;
	}
		
   	slapi_pblock_get(apb, SLAPI_PLUGIN_INTOP_RESULT, &res);
	/* OPERATIONS ERRORS also returned when bind failed */
        if (CB_LDAP_CONN_ERROR(res) || (res==LDAP_OPERATIONS_ERROR )) 
        {
		printf("Can't contact the remote farm server %s. (%s).\n",inst->pool->hostname,ldap_err2string(res));
		rc=-1;
		goto the_end;
        } else {
		printf("Connection established with the remote farm server %s.\n",inst->pool->hostname);
	}
 
the_end:
        if (apb)
        {
                slapi_free_search_results_internal(apb);
                slapi_pblock_destroy (apb);
        }
        
        return rc;
}

