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

/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/* The functions in this file allow plugins to perform bulk import of data 
   comming over ldap connection. Note that the code will not work if
   there is now active connection since import state is stored in the
   connection extension */

#include "slap.h"

/* forward declarations */
static int process_bulk_import_op (Slapi_PBlock *pb, int state, Slapi_Entry *e);

/* This function initiates bulk import. The pblock must contain 
   SLAPI_LDIF2DB_GENERATE_UNIQUEID -- currently always set to TIME_BASED 
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported 
   or 
   SLAPI_TARGET_DN that contains root of the imported area.   
   The function returns LDAP_SUCCESS or LDAP error code 
*/

int slapi_start_bulk_import (Slapi_PBlock *pb)
{
    return (process_bulk_import_op (pb, SLAPI_BI_STATE_START, NULL));
}

/* This function stops bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- the backend being imported 
   or 
   SLAPI_TARGET_DN that contains root of the imported area.   
   The function returns LDAP_SUCCESS or LDAP error code 
*/
int slapi_stop_bulk_import (Slapi_PBlock *pb)
{
    return (process_bulk_import_op (pb, SLAPI_BI_STATE_DONE, NULL));    
}
 
/* This function adds an entry to the bulk import. The pblock must contain
   SLAPI_CONNECTION -- connection over which bulk import is coming
   SLAPI_BACKEND -- optional backend pointer; if missing computed based on entry dn 
   The function returns LDAP_SUCCESS or LDAP error code 
*/
int slapi_import_entry (Slapi_PBlock *pb, Slapi_Entry *e)
{
    return (process_bulk_import_op (pb, SLAPI_BI_STATE_ADD, e));     
} 

static int 
process_bulk_import_op (Slapi_PBlock *pb, int state, Slapi_Entry *e)
{
    int rc;
    Slapi_Backend *be = NULL;
    char *dn = NULL;
    Slapi_DN sdn;
    const Slapi_DN *target_sdn = NULL;

    if (pb == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "process_bulk_import_op: NULL pblock\n");
        return LDAP_OPERATIONS_ERROR;
    }

    if (state == SLAPI_BI_STATE_ADD && e == NULL)
    {
       slapi_log_error(SLAPI_LOG_FATAL, NULL, "process_bulk_import_op: NULL entry\n");
       return LDAP_OPERATIONS_ERROR;
    }

    slapi_pblock_get (pb, SLAPI_BACKEND, &be);
    if (be == NULL)
    {
        /* try to get dn to select backend */
        if (e)
        {
            target_sdn = slapi_entry_get_sdn_const (e);
            be = slapi_be_select (target_sdn);
        }
        else
        {
			slapi_sdn_init(&sdn);
            slapi_pblock_get (pb, SLAPI_TARGET_DN, &dn);
            if (dn)
            {
                slapi_sdn_init_dn_byref(&sdn, dn);
                be = slapi_be_select (&sdn);
				target_sdn = &sdn;
            }
        }
        
        if (be) 
        {
			if (state == SLAPI_BI_STATE_START && (!slapi_be_issuffix(be, target_sdn)))
			{
            	slapi_log_error(SLAPI_LOG_FATAL, NULL,
				 	"process_bulk_import_op: wrong backend suffix\n");
            	return LDAP_OPERATIONS_ERROR;    
			}
            slapi_pblock_set (pb, SLAPI_BACKEND, be);
        }
        else
        {
            slapi_log_error(SLAPI_LOG_FATAL, NULL, "process_bulk_import_op: NULL backend\n");
            return LDAP_OPERATIONS_ERROR;    
        }        

		if (NULL == e)
            slapi_sdn_done (&sdn);
    }

    if (be->be_wire_import == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "slapi_start_bulk_import: "
                        "bulk import is not supported by this (%s) backend\n",
                        be->be_type);
        return LDAP_NOT_SUPPORTED;   
    }

    /* set required parameters */
    slapi_pblock_set (pb, SLAPI_BULK_IMPORT_STATE, &state);
    if (e)
        slapi_pblock_set (pb, SLAPI_BULK_IMPORT_ENTRY, e);

    rc = be->be_wire_import (pb);
    if (rc != 0)
    {
        /* The caller will free the entry (e), so we just
         * leave it alone here. */
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "slapi_start_bulk_import: "
                        "failed; error = %d\n", rc);
        return LDAP_OPERATIONS_ERROR;
    }

    return LDAP_SUCCESS;
}
