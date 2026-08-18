/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _SLDAPD_AUTH_H_
#define _SLDAPD_AUTH_H_

#include <prio.h>

void client_auth_init(void);
void handle_handshake_done(PRFileDesc *prfd, void *clientData);
int handle_bad_certificate(void *clientData, PRFileDesc *prfd);

#endif
