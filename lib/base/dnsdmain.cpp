/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * dnsdmain.c: DNS domain guessing stuff moved out of dns.c because of the
 * string ball problems
 */


#include "netsite.h"
#include <string.h>
#include <stdio.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif
#include <ctype.h>
#include "util.h"

/* Under NT, this is taken care of by net.h including winsock.h */
#ifdef XP_UNIX
#include <netdb.h>  /* struct hostent */
#endif
extern "C" {
#include <nspr.h>
}



/* This is contained in resolv.h on most systems. */
#define _PATH_RESCONF "/etc/resolv.conf"

#ifdef XP_UNIX
NSPR_BEGIN_EXTERN_C
#ifdef Linux
extern int getdomainname(char *, size_t);
#else 
extern int getdomainname(char *, int);
#endif /* Linux */
#if defined(HPUX) || defined (UnixWare) || defined(Linux) || defined(IRIX6_5)
extern int gethostname (char *name, size_t namelen);
#else
#ifndef AIX
extern int gethostname (char *name, int namelen);
#endif
#endif
NSPR_END_EXTERN_C
#endif


/* ---------------------------- dns_guess_domain -------------------------- */


extern "C" NSAPI_PUBLIC char *dns_guess_domain(char * hname)
{
    FILE *f;
    char * cp;
    int hnlen;
    char line[256];
    static int dnlen = 0;
    static char * domain = 0;
    PRHostEnt   hent;
    char        buf[PR_NETDB_BUF_SIZE];
    PRStatus    err;

    /* Sanity check */
    if (strchr(hname, '.')) {
	return STRDUP(hname);
    }

    if (dnlen == 0) {

	/* First try a little trick that seems to often work... */

	/*
	 * Get the local host name, even it doesn't come back
	 * fully qualified.
	 */
	line[0] = 0;
	gethostname(line, sizeof(line));
	if (line[0] != 0) {
	  /* Is it fully qualified? */
	  domain = strchr(line, '.');
	  if (domain == 0) {
	    /* No, try gethostbyname() */
	    err = PR_GetHostByName(line,
				      buf,
				      PR_NETDB_BUF_SIZE,
				      &hent);
	    if (err == PR_SUCCESS) {
	      /* See if h_name is fully-qualified */
	      if (hent.h_name) 	domain = strchr(hent.h_name, '.');

	      /* Otherwise look for a fully qualified alias */
	      if ((domain == 0) &&
		  (hent.h_aliases && hent.h_aliases[0])) {
		char **p;
		for (p = hent.h_aliases; *p; ++p) {
		  domain = strchr(*p, '.');
		  if (domain) break;
		}
	      }
	    }
	  }
	}

	/* Still no luck? */
	if (domain == 0) {

	    f = fopen(_PATH_RESCONF, "r");

	    /* See if there's a domain entry in their resolver configuration */
	    if(f) {
		while(fgets(line, sizeof(line), f)) {
		    if(!strncasecmp(line, "domain ", 7)) {
			for (cp = &line[7]; *cp && isspace(*cp); ++cp) ;
			if (*cp) {
			    domain = cp;
			    for (; *cp && !isspace(*cp); ++cp) ;
			    *cp = 0;
			}
			break;
		    }
		}
		fclose(f);
	    }
	}

#ifndef NO_DOMAINNAME
	if (domain == 0) {
	    /* No domain found. Try getdomainname. */
	    getdomainname(line, sizeof(line));
	    if (line[0] != 0) domain = &line[0];
	}
#endif

	if (domain != 0) {
	    if (domain[0] == '.') ++domain;
	    domain = STRDUP(domain);
	    dnlen = strlen(domain);
	}
	else dnlen = -1;
    }

    if (domain != 0) {
	hnlen = strlen(hname);
	if ((hnlen + dnlen + 2) <= sizeof(line)) {
	    strcpy(line, hname);
	    line[hnlen] = '.';
	    strcpy(&line[hnlen+1], domain);
	    return STRDUP(line);
	}
    }

    return 0;
}
