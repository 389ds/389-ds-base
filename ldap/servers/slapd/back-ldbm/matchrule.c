/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* matchrule.c */


#include "back-ldbm.h"

/* NPCTE fix for bug # 394184, SD, 20 Jul 00 */
/* replace the hard coded return value by the appropriate LDAP error code */
/*
 * Returns:  0 -- OK                now is: LDAP_SUCCESS (fix for bug #394184)
 *			-1 -- protocol error    now is: LDAP_PROTOCOL_ERROR 
 *			-3 -- operation error   now is: LDAP_OPERATIONS_ERROR
 */
int
create_matchrule_indexer(Slapi_PBlock **pb,char* matchrule,char* type)
{
	IFP mrINDEX = NULL;
	int return_value = LDAP_SUCCESS;
	unsigned int sort_indicator = SLAPI_PLUGIN_MR_USAGE_SORT; 

    if(pb==NULL)
    {
        return LDAP_OPERATIONS_ERROR;
    }

    if(*pb==NULL)
    {
    	*pb = slapi_pblock_new();
    }
    if(*pb==NULL)
	{
		/* Memory allocation faliure */
		/* Operations error to the calling routine */
		return LDAP_OPERATIONS_ERROR;
	}
	
	/* If these fail, it's an operations error */
	return_value |= slapi_pblock_set (*pb, SLAPI_PLUGIN_MR_OID, matchrule);
	return_value |= slapi_pblock_set (*pb, SLAPI_PLUGIN_MR_TYPE, type);
	return_value |= slapi_pblock_set (*pb, SLAPI_PLUGIN_MR_USAGE, (void*)&sort_indicator);
	if (0 != return_value)
	{
		return LDAP_OPERATIONS_ERROR;
	}

	/* If this fails, could be operations error, or that OID is not supported */
	return_value = slapi_mr_indexer_create (*pb);
	if (0 != return_value)
	{
		return LDAP_PROTOCOL_ERROR;
	}

	/* If these fail, ops error */
	return_value = slapi_pblock_get (*pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);

	if ( (0 != return_value) || (mrINDEX == NULL) )
	{
		return LDAP_OPERATIONS_ERROR;
	}
	else
	{
		return LDAP_SUCCESS;
	}
}
/* End NPCTE fix for bug # 394184 */

int 
destroy_matchrule_indexer(Slapi_PBlock *pb)
{
	IFP mrDESTROY = NULL;
	if (!slapi_pblock_get (pb, SLAPI_PLUGIN_DESTROY_FN, &mrDESTROY))
	{
	    if (mrDESTROY != NULL)
        {
    		mrDESTROY (pb);
        }
	}
	return 0;
}


/*
 * This routine returns pointer to memory which is owned by the plugin, so don't 
 * free it. Gets freed by the next call to this routine, or when the indexer
 * is destroyed
 */
int
matchrule_values_to_keys(Slapi_PBlock *pb,struct berval **input_values,struct berval ***output_values)
{
	IFP mrINDEX = NULL;

	slapi_pblock_get (pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);
	slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUES, input_values);
	mrINDEX (pb);
	slapi_pblock_get (pb, SLAPI_PLUGIN_MR_KEYS, output_values);
	return 0;
}
	
/*
 * This routine returns pointer to memory which is owned by the plugin, so don't 
 * free it. Gets freed by the next call to this routine, or when the indexer
 * is destroyed
 */
int
matchrule_values_to_keys_sv(Slapi_PBlock *pb,Slapi_Value **input_values,Slapi_Value ***output_values)
{
	IFP mrINDEX = NULL;
        struct berval **bvi, **bvo;

        valuearray_get_bervalarray(input_values, &bvi);

	slapi_pblock_get (pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);
	slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUES, bvi);
	mrINDEX (pb);
	slapi_pblock_get (pb, SLAPI_PLUGIN_MR_KEYS, &bvo);

        slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUES, NULL);
        ber_bvecfree(bvi);

        valuearray_init_bervalarray(bvo, output_values);
	return 0;
}
