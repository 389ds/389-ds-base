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

/*
 * poll_using_select.c
 *
 * poll_using_select() is an implementation of poll using select.
 * I borrowed this code from the client source, ns/nspr/src/mdunix.c
 * from the Nav404_OEM_BRANCH
 * 
 * The reason we need this is on Linux 2.0 (and possibly other platforms)
 * there is no poll.  ldapserver/ldap/servers/slapd/daemon.c calls poll
 *
 * In Linux 2.1 there will be a native poll, so this will go away for Linux
 * but other platforms may need it.
 *
 * 1-30-98 
 * sspitzer
 */
#include "poll_using_select.h"

int
poll_using_select(struct my_pollfd *filed, int nfds, int timeout)
{
    int width;
    fd_set rd;
    fd_set wr;
    fd_set ex;
    struct timeval tv;
    int a, b, retval;

#ifdef DEBUG_POLL_AS_SELECT
    LDAPDebug(LDAP_DEBUG_ANY, "poll convert nfds=%d, timeout=%d\n", nfds, timeout,0);
#endif /* DEBUG_POLL_AS_SELECT */

    FD_ZERO( &rd );
    FD_ZERO( &wr );
    FD_ZERO( &ex );

    /*
     * If the fd member of all pollfd structures is less than 0,
     * the poll() function will return 0 and have no other results.
     */
    for(a=0, b=0; a<nfds; a++){
	if( filed[a].fd >= 0 ) b++;
    }
    if( b == 0 ){ return 0; }

    for( width=0,a=0; a<nfds; a++ ){
	short temp_events;

#ifdef DEBUG_POLL_AS_SELECT
	if (filed[a].events != 0) {
		LDAPDebug(LDAP_DEBUG_ANY, "poll events = %d for fd=%d\n", filed[a].events, filed[a].fd,0);
	}
#endif /* DEBUG_POLL_AS_SELECT */

	temp_events = filed[a].events;

	filed[a].revents = 0;

	if( temp_events & POLLIN  ) FD_SET( filed[a].fd, &rd );
	if( temp_events & POLLOUT ) FD_SET( filed[a].fd, &wr );
	if( temp_events & POLLPRI ) FD_SET( filed[a].fd, &ex );

	temp_events &= ~(POLLIN|POLLOUT|POLLPRI);

#ifdef DEBUG_POLL_AS_SELECT
	if( temp_events != 0 ){
	    LDAPDebug(LDAP_DEBUG_ANY, "Unhandled poll event type=0x%x on FD(%d)\n",filed[a].events,filed[a].fd,0);
	}
#endif /* DEBUG_POLL_AS_SELECT */

	width = width>(filed[a].fd+1)?width:(filed[a].fd+1);
    }

    if( timeout != -1 ){
	tv.tv_sec = timeout/MSECONDS;
	tv.tv_usec= timeout%MSECONDS;

	retval = select ( width, &rd, &wr, &ex, &tv );
    }
    else
    {
	retval = select ( width, &rd, &wr, &ex, 0 );
    }

    if( retval <= 0 ) return( retval );

#ifdef DEBUG_POLL_AS_SELECT
    LDAPDebug(LDAP_DEBUG_ANY, "For\n",0,0,0);
#endif /* DEBUG_POLL_AS_SELECT */

    for( b=0; b<nfds;b++){

	if( filed[b].fd >= 0 ){
	    a = filed[b].fd;

	    if( FD_ISSET( a, &rd )) filed[b].revents |= POLLIN;
	    if( FD_ISSET( a, &wr )) filed[b].revents |= POLLOUT;
	    if( FD_ISSET( a, &ex )) filed[b].revents |= POLLPRI;

#ifdef DEBUG_POLL_AS_SELECT
	    if ((filed[b].events != 0) || (filed[b].revents != 0)) {
	 	LDAPDebug(LDAP_DEBUG_ANY, "   file des=%d, events=0x%x, revents=0x%x\n", filed[b].fd, filed[b].events ,filed[b].revents);
	    }
#endif /* DEBUG_POLL_AS_SELECT */
	}
    }

    for( a=0, retval=0 ; a<nfds; a++ ){
	if( filed[a].revents ) retval++;
    }

#ifdef DEBUG_POLL_AS_SELECT
    LDAPDebug(LDAP_DEBUG_ANY, "poll returns %d (converted poll)\n",retval,0,0);
#endif /* DEBUG_POLL_AS_SELECT */

    return( retval );
}
