/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "retrocl.h"



/*
 * Function: retrocl_rootdse_search
 *
 * Returns: SLAPI_DSE_CALLBACK_OK always
 * 
 * Arguments: See plugin API
 *
 * Description: callback function plugged into base object search of root DSE.
 * Adds changelog, firstchangenumber and lastchangenumber attributes.
 *
 */
 
int
retrocl_rootdse_search(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{

	struct berval val;
	struct berval *vals[2];
	vals[0] = &val;
	vals[1] = NULL;
 
	/* Changelog information */
	if ( retrocl_be_changelog != NULL )
	{
		char buf[BUFSIZ];
	    changeNumber cnum;

	    /* Changelog suffix */
	    val.bv_val = RETROCL_CHANGELOG_DN;
	    if ( val.bv_val != NULL )
		{
    		val.bv_len = strlen( val.bv_val );
    		slapi_entry_attr_replace( e, "changelog", vals );
	    }

	    /* First change number contained in log */
	    cnum = retrocl_get_first_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "firstchangenumber", vals );

	    /* Last change number contained in log */
	    cnum = retrocl_get_last_changenumber();
	    sprintf( buf, "%lu", cnum );
	    val.bv_val = buf;
	    val.bv_len = strlen( val.bv_val );
	    slapi_entry_attr_replace( e, "lastchangenumber", vals );
	}

	return SLAPI_DSE_CALLBACK_OK;
}


