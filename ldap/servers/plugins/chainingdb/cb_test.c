/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

