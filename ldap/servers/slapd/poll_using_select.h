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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
