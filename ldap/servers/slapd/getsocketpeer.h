/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#if defined(ENABLE_LDAPI)
#if !defined(GETSOCKETPEER_H)
#define GETSOCKETPEER_H
int slapd_get_socket_peer(PRFileDesc *nspr_fd, uid_t *uid, gid_t *gid);
#endif
#endif /* ENABLE_LDAPI */
