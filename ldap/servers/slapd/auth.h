/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _SLDAPD_AUTH_H_
#define _SLDAPD_AUTH_H_

#include <prio.h>

void client_auth_init();
void handle_handshake_done (PRFileDesc *prfd, void* clientData);
int handle_bad_certificate (void* clientData, PRFileDesc *prfd);

#endif
