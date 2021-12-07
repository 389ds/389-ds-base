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

ER2(SLAPD_DISCONNECT_ABORT, "Connection aborted - A1")
ER2(SLAPD_DISCONNECT_BAD_BER_TAG, "Bad Ber Tag or uncleanly closed connection - B1")
ER2(SLAPD_DISCONNECT_BER_TOO_BIG, "Ber Too Big (nsslapd-maxbersize) - B2")
ER2(SLAPD_DISCONNECT_BER_PEEK, "Ber peak tag - B3")
ER2(SLAPD_DISCONNECT_BER_FLUSH, "Server failed to flush response back to Client - B4")
ER2(SLAPD_DISCONNECT_IDLE_TIMEOUT, "Idle Timeout (nsslapd-idletimeout) - T1")
ER2(SLAPD_DISCONNECT_REVENTS, "Poll revents - R1")
ER2(SLAPD_DISCONNECT_IO_TIMEOUT, "IO Block Timeout (nsslapd-ioblocktimeout) - T2")
ER2(SLAPD_DISCONNECT_PLUGIN, "Plugin - P1")
ER2(SLAPD_DISCONNECT_UNBIND, "Cleanly Closed Connection - U1")
ER2(SLAPD_DISCONNECT_POLL, "Poll - P2")
ER2(SLAPD_DISCONNECT_NTSSL_TIMEOUT, "NTSSL Timeout - T2")
ER2(SLAPD_DISCONNECT_SASL_FAIL, "SASL Failure - S1")


#endif /* __DISCONNECT_ERROR_STRINGS_H_ */
