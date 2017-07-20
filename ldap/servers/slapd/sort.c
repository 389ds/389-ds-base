/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "slap.h"

/* Fix for bug # 394184, SD, 20 Jul 00 */
/* fix and cleanup (switch(code) {} removed) */
/* arg 'code' has now the correct sortResult value */
int
sort_make_sort_response_control(Slapi_PBlock *pb, int code, char *error_type)
{

    LDAPControl new_ctrl = {0};
    BerElement *ber = NULL;
    struct berval *bvp = NULL;
    int rc = -1;
    ber_int_t control_code;
    int pr_idx = -1;
    Connection *pb_conn;
    Slapi_Operation *operation;

    slapi_pblock_get(pb, SLAPI_PAGED_RESULTS_INDEX, &pr_idx);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);

    if (code == CONN_GET_SORT_RESULT_CODE) {
        code = pagedresults_get_sort_result_code(pb_conn, operation, pr_idx);
    } else {
        if (op_is_pagedresults(operation)) {
            pagedresults_set_sort_result_code(pb_conn, operation, code, pr_idx);
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

    if ((ber = ber_alloc()) == NULL) {
        return -1;
    }

    if ((rc = ber_printf(ber, "{e", control_code)) != -1) {
        if (rc != -1 && NULL != error_type) {
            rc = ber_printf(ber, "s", error_type);
        }
        if (rc != -1) {
            rc = ber_printf(ber, "}");
        }
    }
    if (rc != -1) {
        rc = ber_flatten(ber, &bvp);
    }

    ber_free(ber, 1);

    if (rc == -1) {
        return rc;
    }

    new_ctrl.ldctl_oid = LDAP_CONTROL_SORTRESPONSE;
    new_ctrl.ldctl_value = *bvp;
    new_ctrl.ldctl_iscritical = 1;

    if (slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, &new_ctrl) != 0) {
        ber_bvfree(bvp);
        return (-1);
    }

    ber_bvfree(bvp);
    return (LDAP_SUCCESS);
}
/* End fix for bug #394184 */
