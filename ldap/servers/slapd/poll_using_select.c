/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
