/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _POLL_USING_SELECT_H
#define _POLL_USING_SELECT_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "slap.h"

/* uncomment for debugging info
#define DEBUG_POLL_AS_SELECT	
*/
#define MSECONDS		1000

struct my_pollfd {
    int fd;
    short events;
    short revents;
};
 
/* poll events */
 
#define POLLIN          0x0001          /* fd is readable */
#define POLLPRI         0x0002          /* high priority info at fd */
#define POLLOUT         0x0004          /* fd is writeable (won't block) */
#define POLLRDNORM      0x0040          /* normal data is readable */
#define POLLWRNORM      POLLOUT
#define POLLRDBAND      0x0080          /* out-of-band data is readable */
#define POLLWRBAND      0x0100          /* out-of-band data is writeable */
 
#define POLLNORM        POLLRDNORM
 
#define POLLERR         0x0008          /* fd has error condition */
#define POLLHUP         0x0010          /* fd has been hung up on */
#define POLLNVAL        0x0020          /* invalid pollfd entry */

int poll_using_select(struct my_pollfd *filedes, int nfds, int timeout);

#endif /* _POLL_USING_SELECT_H */
