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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "slap.h"

/* Fix for bug # 394184, SD, 20 Jul 00 */
/* fix and cleanup (switch(code) {} removed) */
/* arg 'code' has now the correct sortResult value */
int
sort_make_sort_response_control ( Slapi_PBlock *pb, int code, char *error_type)
{

    LDAPControl     new_ctrl = {0};
    BerElement      *ber= NULL;    
    struct berval   *bvp = NULL;
    int             rc = -1;
    ber_int_t       control_code;

    if (code == CONN_GET_SORT_RESULT_CODE) {
        code = pagedresults_get_sort_result_code(pb->pb_conn);
    } else {
        Slapi_Operation *operation;
        slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
        if (operation->o_flags & OP_FLAG_PAGED_RESULTS) {
            pagedresults_set_sort_result_code(pb->pb_conn, code);
        }
    }

    control_code = code;

    /*
       SortResult ::= SEQUENCE {
         sortResult  ENUMERATED {
            success                   (0), -- results are sorted
            operationsError           (1), -- server internal failure
            timeLimitExceeded         (3), -- timelimit reached before
                                           -- sorting was completed
            strongAuthRequired        (8), -- refused to return sorted
                                           -- results via insecure
                                           -- protocol
            adminLimitExceeded       (11), -- too many matching entries
                                           -- for the server to sort
            noSuchAttribute          (16), -- unrecognized attribute
                                           -- type in sort key
            inappropriateMatching    (18), -- unrecognized or inappro-
                                           -- priate matching rule in
                                           -- sort key
            insufficientAccessRights (50), -- refused to return sorted
                                           -- results to this client
            busy                     (51), -- too busy to process
            unwillingToPerform       (53), -- unable to sort
            other                    (80)
         },
         attributeType [0] AttributeType OPTIONAL
       }
     */
    
    if ( ( ber = ber_alloc()) == NULL ) {
        return -1;
    }

    if (( rc = ber_printf( ber, "{e", control_code )) != -1 ) {
        if ( rc != -1 && NULL != error_type ) {
            rc = ber_printf( ber, "s", error_type );
        }
        if ( rc != -1 ) {
            rc = ber_printf( ber, "}" );
        }
    }
    if ( rc != -1 ) {
        rc = ber_flatten( ber, &bvp );
    }
    
    ber_free( ber, 1 );

    if ( rc == -1 ) {
        return rc;
    }
        
    new_ctrl.ldctl_oid = LDAP_CONTROL_SORTRESPONSE;
    new_ctrl.ldctl_value = *bvp;
    new_ctrl.ldctl_iscritical = 1;         

    if ( slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl ) != 0 ) {
            ber_bvfree(bvp);
            return( -1 );
    }

        ber_bvfree(bvp);
    return( LDAP_SUCCESS );
}
/* End fix for bug #394184 */
