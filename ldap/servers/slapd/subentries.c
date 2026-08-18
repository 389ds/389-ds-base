/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* Author: Anton Bobrov <abobrov@redhat.com> */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* subentries.c - routines for dealing with RFC 3672 LDAP Subentries control */

#include <ldap.h>

#include "slap.h"

/*
 * Decode the SUBENTRIES REQUEST control.
 *
 * Returns:
 *       false: normal entries are visible and subentries are not (0),
 *       true: subentries are visible and normal entries are not (1),
 *       error (-1),
 *
 */
int
subentries_parse_request_control(struct berval *subentries_spec_ber)
{
    /* The subentries control is an LDAP Control whose controlType is
       1.3.6.1.4.1.4203.1.10.1, criticality is TRUE or FALSE (hence absent),
       and controlValue contains a BER-encoded BOOLEAN indicating
       visibility.  A controlValue containing the value TRUE indicates that
       subentries are visible and normal entries are not.  A controlValue
       containing the value FALSE indicates that normal entries are visible
       and subentries are not.
       Note that TRUE visibility has the three octet encoding { 01 01 FF }
       and FALSE visibility has the three octet encoding { 01 01 00 }.
       The controlValue SHALL NOT be absent.
    */

    BerElement *ber = NULL;
    int return_value = -1;

    if (!BV_HAS_DATA(subentries_spec_ber)) {
        return return_value;
    }

    ber = ber_init(subentries_spec_ber);
    if (ber == NULL) {
        return return_value;
    }

    ber_int_t visibility = 0;

    /* There are many ways to encode a boolean. Lets hope
       the ber_scanf can decode this boolean correctly. */
    if (ber_scanf(ber, "b", &visibility) == LBER_ERROR) {
        return_value = -1;
    } else {
        return_value = visibility ? 1 : 0;
        slapi_log_err(SLAPI_LOG_TRACE, "subentries_request_control",
                      "Visibility=%d\n", return_value);
    }

    /* the ber encoding is no longer needed */
    ber_free(ber, 1);

    return return_value;
}