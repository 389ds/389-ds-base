/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _SLDAPD_AUTH_H_
#define _SLDAPD_AUTH_H_

#include <prio.h>

void client_auth_init();
void handle_handshake_done (PRFileDesc *prfd, void* clientData);
int handle_bad_certificate (void* clientData, PRFileDesc *prfd);

#endif
