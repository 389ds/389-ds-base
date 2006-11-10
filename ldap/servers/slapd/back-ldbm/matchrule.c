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
