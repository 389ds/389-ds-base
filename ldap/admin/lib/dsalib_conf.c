/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#include <process.h>
#endif
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsalib.h"
#include <ldaplog.h>
#include "portable.h"
#include <ctype.h>

#define CONF_FILE_NAME "config/dse.ldif"
#define CONF_SUFFIX "cn=config"

DS_EXPORT_SYMBOL char *
ds_get_var_name(int varnum)
{
    if ( (varnum >= DS_CFG_MAX) || (varnum < 0) )
        return(NULL);                      /* failure */
    return(ds_cfg_info[varnum].dci_varname);
}

/*
 * Get config info.
 */
DS_EXPORT_SYMBOL char **
ds_get_config(int type)
{
    char        conffile[PATH_MAX];
    char        *root;
    FILE        *sf = NULL;
    char        **conf_list = NULL;

    if ( (type != DS_REAL_CONFIG) && (type != DS_TMP_CONFIG) ) {
        ds_send_error("Invalid config file type.", 0);
        return(NULL);
    }

    if ( (root = ds_get_install_root()) == NULL ) {
        ds_send_error("Cannot find server root directory.", 0);
        return(NULL);
    }

    sprintf(conffile, "%s/%s", root, CONF_FILE_NAME);

    if ( !(sf = fopen(conffile, "r")) )  {
        ds_send_error("could not read config file.", 1);
        return(NULL);
    }

    conf_list = ds_get_conf_from_file(sf);

    fclose(sf);
    if (!conf_list) {
        ds_send_error("failed to read the config file successfully.", 0);
        return(NULL);
    }
    return(conf_list);
}

/*
 * NOTE: the ordering of the following array elements must be kept in sync
 * with the ordering of the #defines in ../include/dsalib.h.
 */
struct ds_cfg_info ds_cfg_info[] = {
{"nsslapd-errorlog-level" },
{"nsslapd-referral" },
{"nsslapd-auditlog" },
{"nsslapd-localhost" },
{"nsslapd-port" },
{"nsslapd-security" },
{"nsslapd-secureport" },
{"nsslapd-ssl3ciphers"},
{"passwordstoragescheme"},
{"nsslapd-accesslog"},
{"nsslapd-errorlog"},
{"nsslapd-rootdn"},
{"nsslapd-rootpwstoragescheme"},
{"nsslapd-suffix"},
{"nsslapd-localuser"},
{0}
};

/*
 * Open the config file and look for option "option".  Return its
 * value, or NULL if the option was not found.
 */
DS_EXPORT_SYMBOL char *
ds_get_config_value( int option )
{
    char **all, *value;
    int i;
    char *attr = ds_get_var_name(option);

    if (attr == NULL)
	return NULL;

    all = ds_get_config( DS_REAL_CONFIG );
    if ( all == NULL ) {
	return NULL;
    }
    for ( i = 0; all[ i ] != NULL; i++ ) {
	if (( value = strchr( all[ i ], ':' )) != NULL ) {
	    *value = '\0';
	    ++value;
	    while (*value && isspace(*value))
		++value;
	}
	if ( !strcasecmp( attr, all[ i ] )) {
	    return strdup( value );
	}
    }
    return NULL;
}

static size_t
count_quotes (const char* s)
{
    size_t count = 0;
    const char* t = s;
    if (t) while ((t = strpbrk (t, "\"\\")) != NULL) {
	++count;
	++t;
    }
    return count;
}

DS_EXPORT_SYMBOL char*
ds_enquote_config_value (int paramnum, char* s)
{
    char* result;
    char* brkcharset = "\"\\ \t\r\n";
	char *encoded_quote = "22"; /* replace quote with \22 */
	int encoded_quote_len = strlen(encoded_quote);
	char *begin = s;
    if (*s && ! strpbrk (s, brkcharset) && 
		! (paramnum == DS_AUDITFILE || paramnum == DS_ACCESSLOG ||
#if defined( XP_WIN32 )
		   paramnum == DS_SUFFIX ||
#endif
		   paramnum == DS_ERRORLOG)) {
		result = s;
    } else {
		char* t = malloc (strlen (s) + count_quotes (s) + 3);
		result = t;
		*t++ = '"';
		while (*s) {
			switch (*s) {

			case '"':
				/* convert escaped quotes by replacing the quote with
				   escape code e.g. 22 so that \" is converted to \22 "*/
				if ((s > begin) && (*(s - 1) == '\\'))
				{
					strcpy(t, encoded_quote);
					t += encoded_quote_len;
				}
				else /* unescaped ", just replace with \22 "*/
				{
					*t++ = '\\';
					strcpy(t, encoded_quote);
					t += encoded_quote_len;					
				}
				++s;
				break;

			default: 
				*t++ = *s++; /* just copy it */
				break;
			}
		}
		*t++ = '"';
		*t = '\0';
    }
    return result;
}

DS_EXPORT_SYMBOL char*
ds_DNS_to_DN (char* DNS)
{
    static const char* const RDN = "dc=";
    char* DN;
    char* dot;
    size_t components;
    if (DNS == NULL || *DNS == '\0') {
	return strdup ("");
    }
    components = 1;
    for (dot = strchr (DNS, '.'); dot != NULL; dot = strchr (dot + 1, '.')) {
	++components;
    }
    DN = malloc (strlen (DNS) + (components * strlen(RDN)) + 1);
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
    dn_normalize (DN);
    return DN;
}
