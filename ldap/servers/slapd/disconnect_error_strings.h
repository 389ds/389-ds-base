/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* disconnect_error_strings.h
 *
 * Strings describing the errors used in logging the reason a connection
 * was closed.
 */
#ifndef __DISCONNECT_ERROR_STRINGS_H_
#define __DISCONNECT_ERROR_STRINGS_H_

ER2(SLAPD_DISCONNECT_ABORT, "A1")
ER2(SLAPD_DISCONNECT_BAD_BER_TAG, "B1")
ER2(SLAPD_DISCONNECT_BER_TOO_BIG, "B2")
ER2(SLAPD_DISCONNECT_BER_PEEK, "B3")
ER2(SLAPD_DISCONNECT_BER_FLUSH, "B4")
ER2(SLAPD_DISCONNECT_IDLE_TIMEOUT, "T1")
ER2(SLAPD_DISCONNECT_REVENTS, "R1")
ER2(SLAPD_DISCONNECT_IO_TIMEOUT, "T2")
ER2(SLAPD_DISCONNECT_PLUGIN, "P1")
ER2(SLAPD_DISCONNECT_UNBIND, "U1")
ER2(SLAPD_DISCONNECT_POLL, "P2")
ER2(SLAPD_DISCONNECT_NTSSL_TIMEOUT, "T2")
ER2(SLAPD_DISCONNECT_SASL_FAIL, "S1")


#endif /* __DISCONNECT_ERROR_STRINGS_H_ */
