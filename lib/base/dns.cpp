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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * dns.c: DNS resolution routines
 * 
 * Rob McCool
 */
#define DNS_GUESSING

#include "netsite.h"
#ifdef XP_UNIX
#include "systems.h"
#else /* XP_WIN32 */
#include "base/systems.h"
#endif /* XP_WIN32 */

/* Under NT, these are taken care of by net.h including winsock.h */
#ifdef XP_UNIX
#include <arpa/inet.h>  /* inet_ntoa */
#include <netdb.h>  /* struct hostent */
#ifdef NEED_GHN_PROTO
extern "C" int gethostname (char *name, size_t namelen);
#endif
#endif
#include <stdio.h>
#include <nspr.h>

/* ---------------------------- dns_find_fqdn ----------------------------- */


/* defined in dnsdmain.c */
extern "C"  NSAPI_PUBLIC char *dns_guess_domain(char * hname);

char *net_find_fqdn(PRHostEnt *p)
{
    int x;

    if((!p->h_name) || (!p->h_aliases))
        return NULL;

    if(!strchr(p->h_name, '.')) {
        for(x = 0; p->h_aliases[x]; ++x) {
            if((strchr(p->h_aliases[x], '.')) && 
               (!strncmp(p->h_aliases[x], p->h_name, strlen(p->h_name))))
            {
                return STRDUP(p->h_aliases[x]);
            }
        }
#ifdef DNS_GUESSING
	return dns_guess_domain(p->h_name);
#else
	return NULL;
#endif /* DNS_GUESSING */
    } 
    else 
        return STRDUP(p->h_name);
}


/* ----------------------------- dns_ip2host ------------------------------ */


char *dns_ip2host(char *ip, int verify)
{
    /*    struct in_addr iaddr;  */
    PRNetAddr iaddr;
    char *hn;
    static unsigned long laddr = 0;
    static char myhostname[256];
    PRHostEnt   hent;
    char        buf[PR_NETDB_BUF_SIZE];
    PRStatus    err;


    err = PR_InitializeNetAddr(PR_IpAddrNull, 0, &iaddr);

    if((iaddr.inet.ip = inet_addr(ip)) == -1)
        goto bong;

    /*
     * See if it happens to be the localhost IP address, and try
     * the local host name if so.
     */
    if (laddr == 0) {
	laddr = inet_addr("127.0.0.1");
	myhostname[0] = 0;
	PR_GetSystemInfo(PR_SI_HOSTNAME, myhostname, sizeof(myhostname));
    }

    /* Have to match the localhost IP address and have a hostname */
    if ((iaddr.inet.ip == laddr) && (myhostname[0] != 0)) {
        /*
         * Now try for a fully-qualified domain name, starting with
         * the local hostname.
         */
        err =  PR_GetHostByName(myhostname,
				buf,
				PR_NETDB_BUF_SIZE,
				&hent);

        /* Don't verify if we get a fully-qualified name this way */
        verify = 0;
    }
    else {
      err = PR_GetHostByAddr(&iaddr, 
			     buf,
			     PR_NETDB_BUF_SIZE,
			     &hent);
    }

    if ((err == PR_FAILURE) || !(hn = net_find_fqdn(&hent))) goto bong;


    if(verify) {
        char **haddr = 0;
       	err = PR_GetHostByName(hn,
			       buf,
			       PR_NETDB_BUF_SIZE,
			       &hent);
 
        if(err == PR_SUCCESS) {
            for(haddr = hent.h_addr_list; *haddr; haddr++) {
                if(((struct in_addr *)(*haddr))->s_addr == iaddr.inet.ip)
                    break;
            }
        }

        if((err == PR_FAILURE) || (!(*haddr)))
            goto bong;
    }

    return hn;
  bong:
    return NULL;
}
