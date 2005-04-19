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

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#ifdef _WIN32
#define MAXHOSTNAMELEN 256
#else
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>
#endif
#include <errno.h>
#include "slap.h"
#if defined(USE_SYSCONF) || defined(LINUX)
#include <unistd.h>
#endif /* USE_SYSCONF */

#if defined( NET_SSL )
#include <ssl.h>
#include "fe.h"
#endif /* defined(NET_SSL) */

#ifndef _PATH_RESCONF /* usually defined in <resolv.h> */
#define _PATH_RESCONF "/etc/resolv.conf"
#endif

#if !defined(NO_DOMAINNAME) && defined(_WINDOWS)
#define NO_DOMAINNAME 1
#endif

#if defined (__hpux)
#if (MAXHOSTNAMELEN < 256)
#   undef MAXHOSTNAMELEN
#   define MAXHOSTNAMELEN 256
#endif
#endif

static char*
find_localhost_DNS()
{
    /* This implementation could (and should) be entirely replaced by:
       dns_ip2host ("127.0.0.1", 1); defined in ldapserver/lib/base/dns.c
     */
    char hostname [MAXHOSTNAMELEN + 1];
    struct hostent *hp;
#ifdef GETHOSTBYNAME_BUF_T
    struct hostent hent;
    GETHOSTBYNAME_BUF_T hbuf;
    int err;
#endif
    char** alias;
    FILE* f;
    char* cp;
    char* domain;
    char line [MAXHOSTNAMELEN + 8];

    if (gethostname (hostname, MAXHOSTNAMELEN)) {
	int oserr = errno;

	LDAPDebug (LDAP_DEBUG_ANY, "gethostname() failed, error %d (%s)\n",
	    oserr, slapd_system_strerror( oserr ), 0 );
	return NULL;
    }
    hp = GETHOSTBYNAME (hostname, &hent, hbuf, sizeof(hbuf), &err);
    if (hp == NULL) {
	int oserr = errno;

	LDAPDebug( LDAP_DEBUG_ANY,
	    "gethostbyname(\"%s\") failed, error %d (%s)\n",
	       hostname, oserr, slapd_system_strerror( oserr ));
	return NULL;
    }
    if (hp->h_name == NULL) {
	LDAPDebug (LDAP_DEBUG_ANY, "gethostbyname(\"%s\")->h_name == NULL\n", hostname, 0, 0);
	return NULL;
    }
    if (strchr (hp->h_name, '.') != NULL) {
	LDAPDebug (LDAP_DEBUG_CONFIG, "h_name == %s\n", hp->h_name, 0, 0);
	return slapi_ch_strdup (hp->h_name);
    } else if (hp->h_aliases != NULL) {
	for (alias = hp->h_aliases; *alias != NULL; ++alias) {
	    if (strchr  (*alias, '.') != NULL &&
		strncmp (*alias, hp->h_name, strlen (hp->h_name))) {
		LDAPDebug (LDAP_DEBUG_CONFIG, "h_alias == %s\n", *alias, 0, 0);
		return slapi_ch_strdup (*alias);
	    }
	}
    }
    /* The following is copied from dns_guess_domain(),
       in ldapserver/lib/base/dnsdmain.c */
    domain = NULL;
    f = fopen (_PATH_RESCONF, "r");  /* This fopen() will fail on NT, as expected */
    if (f != NULL) {
	while (fgets (line, sizeof(line), f)) {
	    if (strncasecmp (line, "domain", 6) == 0 && isspace (line[6])) {
		LDAPDebug (LDAP_DEBUG_CONFIG, "%s: %s\n", _PATH_RESCONF, line, 0);
		for (cp = &line[7]; *cp && isspace(*cp); ++cp);
		if (*cp) {
		    domain = cp;
		    /* ignore subsequent whitespace: */
		    for (; *cp && ! isspace (*cp); ++cp);
		    if (*cp) {
			*cp = '\0';
		    }
		}
		break;
	    }
	}
	fclose (f);
    }
#ifndef NO_DOMAINNAME
    if (domain == NULL) {
	/* No domain found. Try getdomainname. */
	getdomainname (line, sizeof(line));
	LDAPDebug (LDAP_DEBUG_CONFIG, "getdomainname(%s)\n", line, 0, 0);
	if (line[0] != 0) {
	    domain = &line[0];
	}
    }
#endif
    if (domain == NULL) {
	return NULL;
    }
    PL_strncpyz (hostname, hp->h_name, sizeof(hostname));
    if (domain[0] == '.') ++domain;
    if (domain[0]) {
	PL_strcatn (hostname, sizeof(hostname), ".");
	PL_strcatn (hostname, sizeof(hostname), domain);
    }
    LDAPDebug (LDAP_DEBUG_CONFIG, "hostname == %s\n", hostname, 0, 0);
    return slapi_ch_strdup (hostname);
}

static const char* const RDN = "dc=";

static char*
convert_DNS_to_DN (char* DNS)
{
    char* DN;
    char* dot;
    size_t components;
    if (*DNS == '\0') {
	return slapi_ch_strdup ("");
    }
    components = 1;
    for (dot = strchr (DNS, '.'); dot != NULL; dot = strchr (dot + 1, '.')) {
	++components;
    }
    DN = slapi_ch_malloc (strlen (DNS) + (components * strlen(RDN)) + 1);
    strcpy (DN, RDN);
    for (dot = strchr (DNS, '.'); dot != NULL; dot = strchr (dot + 1, '.')) {
	*dot = '\0';
	strcat (DN, DNS);
	strcat (DN, ",");
	strcat (DN, RDN);
	DNS = dot + 1;
	*dot = '.';
    }
    strcat (DN, DNS);
    slapi_dn_normalize (DN);
    return DN;
}

static char* localhost_DN = NULL;

char*
get_localhost_DNS()
{
  char *retVal;
  if ( (retVal = config_get_localhost()) == NULL) {
	/* find_localhost_DNS() returns strdup result */
	retVal = find_localhost_DNS();
  }
    return retVal;
}

static void
set_localhost_DN()
{
  char *localhost_DNS = config_get_localhost();
  
  if (localhost_DNS != NULL) {
	localhost_DN = convert_DNS_to_DN (localhost_DNS);
	LDAPDebug (LDAP_DEBUG_CONFIG, "DNS %s -> DN %s\n", localhost_DNS, localhost_DN, 0);
  }
  slapi_ch_free( (void **) &localhost_DNS );
}


char*
get_localhost_DN()
/* Return the Distinguished Name of the local host; that is,
   its DNS name converted to a DN according to RFC 1279.
   The caller should _not_ free this pointer. */
{
    if (localhost_DN == NULL) {
	set_localhost_DN();
    }
    return localhost_DN;
}

static char* config_DN = NULL;

char *
get_config_DN()
{
	char *c;
	char *host;

	if ( config_DN == NULL )
	{
		host = get_localhost_DN();
		if ( host )
			c = slapi_ch_malloc (20 + strlen (host));
		else {
			LDAPDebug (LDAP_DEBUG_CONFIG, "get_locahost_DN() returned \"\"\n",
						 0, 0, 0);
			c = slapi_ch_malloc (20);
		}
		sprintf (c, "cn=ldap://%s:%d", host ? host : "", config_get_port());
		config_DN = c;
	}

	return config_DN;
}

