/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <errno.h>
#include "slap.h"
#if defined(USE_SYSCONF) || defined(LINUX) || defined( __FreeBSD__ )
#include <unistd.h>
#endif /* USE_SYSCONF */

#include <ssl.h>
#include "fe.h"

#ifndef _PATH_RESCONF /* usually defined in <resolv.h> */
#define _PATH_RESCONF "/etc/resolv.conf"
#endif

#if defined (__hpux)
#if (MAXHOSTNAMELEN < 256)
#   undef MAXHOSTNAMELEN
#   define MAXHOSTNAMELEN 256
#endif
#endif

static char*
find_localhost_DNS(void)
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

        slapi_log_err(SLAPI_LOG_ERR, "find_localhost_DNS", 
                "gethostname() failed, error %d (%s)\n",
                oserr, slapd_system_strerror( oserr ));
        return NULL;
    }
    hp = GETHOSTBYNAME (hostname, &hent, hbuf, sizeof(hbuf), &err);
    if (hp == NULL) {
        int oserr = errno;

        slapi_log_err(SLAPI_LOG_ERR,
                "find_localhost_DNS - gethostbyname(\"%s\") failed, error %d (%s)\n",
                hostname, oserr, slapd_system_strerror( oserr ));
        return NULL;
    }
    if (hp->h_name == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "find_localhost_DNS",
                "gethostbyname(\"%s\")->h_name == NULL\n", hostname);
        return NULL;
    }
    if (strchr (hp->h_name, '.') != NULL) {
        slapi_log_err(SLAPI_LOG_CONFIG, "find_localhost_DNS", "h_name == %s\n", hp->h_name);
        return slapi_ch_strdup (hp->h_name);
    } else if (hp->h_aliases != NULL) {
        for (alias = hp->h_aliases; *alias != NULL; ++alias) {
            if (strchr  (*alias, '.') != NULL &&
                strncmp (*alias, hp->h_name, strlen (hp->h_name))) {
                slapi_log_err(SLAPI_LOG_CONFIG, "find_localhost_DNS", "h_alias == %s\n", *alias);
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
                slapi_log_err(SLAPI_LOG_CONFIG, "find_localhost_DNS", "%s: %s\n", _PATH_RESCONF, line);
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
        line[0] = '\0';
        if (getdomainname(line, sizeof(line)) < 0) { /* failure */
            slapi_log_err(SLAPI_LOG_ERR, "find_localhost_DNS", "getdomainname failed\n");
        } else {
            slapi_log_err(SLAPI_LOG_CONFIG, "find_localhost_DNS", "getdomainname(%s)\n", line);
        }
        if (line[0] != '\0') {
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
    slapi_log_err(SLAPI_LOG_CONFIG, "find_localhost_DNS", "hostname == %s\n", hostname);
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
set_localhost_DN(void)
{
    char *localhost_DNS = config_get_localhost();

    if (localhost_DNS != NULL) {
        localhost_DN = convert_DNS_to_DN (localhost_DNS);
        slapi_log_err(SLAPI_LOG_CONFIG, "set_localhost_DN",
                "DNS %s -> DN %s\n", localhost_DNS, localhost_DN);
    }
    slapi_ch_free_string(&localhost_DNS);
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
			slapi_log_err(SLAPI_LOG_CONFIG, "get_config_DN",
				"Get_locahost_DN() returned \"\"\n");
			c = slapi_ch_malloc (20);
		}
		sprintf (c, "cn=ldap://%s:%d", host ? host : "", config_get_port());
		config_DN = c;
	}

	return config_DN;
}

