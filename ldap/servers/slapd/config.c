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

/* config.c - configuration file handling routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h> /* for getcwd */
#else
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#endif
#include "slap.h"
#include "pw.h"
#include <sys/stat.h>
#include <prio.h>

#define MAXARGS	1000


extern int		should_detach;
extern Slapi_PBlock	*repl_pb;


extern char*   slapd_SSL3ciphers;

#ifndef _WIN32
extern char	*localuser;
#endif

char*		rel2abspath( char * );

/*
  See if the given entry has an attribute with the given name and the
  given value; if value is NULL, just test for the presence of the given
  attribute; if value is an empty string (i.e. value[0] == 0),
  the first value in the attribute will be copied into the given buffer
  and returned
*/
static int
entry_has_attr_and_value(Slapi_Entry *e, const char *attrname,
						 char *value, size_t valuebufsize )
{
	int retval = 0;
	Slapi_Attr *attr = 0;
	if (!e || !attrname)
		return retval;

	/* see if the entry has the specified attribute name */
	if (!slapi_entry_attr_find(e, attrname, &attr) && attr)
	{
		/* if value is not null, see if the attribute has that
		   value */
		if (!value)
		{
			retval = 1;
		}
		else
		{
			Slapi_Value *v = 0;
			int index = 0;
			for (index = slapi_attr_first_value(attr, &v);
				 v && (index != -1);
				 index = slapi_attr_next_value(attr, index, &v))
			{
				const char *s = slapi_value_get_string(v);
				if (!s)
					continue;

				if (!*value)
				{
					size_t		len = strlen(s);

					if ( len < valuebufsize )
					{
						strcpy(value, s);
						retval = 1;
					}
					else
					{
						slapi_log_error( SLAPI_LOG_FATAL, "bootstrap config",
								"Ignoring extremely large value for"
								" configuration attribute %s"
								" (length=%ld, value=%40.40s...)\n",
								attrname, len, s );
						retval = 0;	/* value is too large: ignore it */
					}
					break;
				}
				else if (!strcasecmp(s, value))
				{
					retval = 1;
					break;
				}
			}
		}
	}

	return retval;
}


/*
  Extract just the configuration information we need for bootstrapping
  purposes
  1) set up error logging
  2) disable syntax checking
  3) load the syntax plugins
  etc.
*/
int
slapd_bootstrap_config(const char *configdir)
{
	char configfile[MAXPATHLEN+1];
	PRFileInfo64 prfinfo;
	int rc = 0; /* Fail */
	int done = 0;
	PRInt32 nr = 0;
	PRFileDesc *prfd = 0;
	char *buf = 0;
	char *lastp = 0;
	char *entrystr = 0;
	char tmpfile[MAXPATHLEN+1];

	if (NULL == configdir) {
		slapi_log_error(SLAPI_LOG_FATAL,
						"startup", "Passed null config directory\n");
		return rc; /* Fail */
	}
	PR_snprintf(configfile, sizeof(configfile), "%s/%s", configdir,
				CONFIG_FILENAME);
	PR_snprintf(tmpfile, sizeof(tmpfile), "%s/%s.tmp", configdir,
					CONFIG_FILENAME);
	if ( (rc = dse_check_file(configfile, tmpfile)) == 0 ) {
		PR_snprintf(tmpfile, sizeof(tmpfile), "%s/%s.bak", configdir,
					CONFIG_FILENAME);
		rc = dse_check_file(configfile, tmpfile);
	}

	if ( (rc = PR_GetFileInfo64( configfile, &prfinfo )) != PR_SUCCESS )
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error(SLAPI_LOG_FATAL, "config", "The given config file %s could not be accessed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
						configfile, prerr, slapd_pr_strerror(prerr));
		return rc;
	}
	else if (( prfd = PR_Open( configfile, PR_RDONLY,
							   SLAPD_DEFAULT_FILE_MODE )) == NULL )
	{
		PRErrorCode prerr = PR_GetError();
		slapi_log_error(SLAPI_LOG_FATAL, "config", "The given config file %s could not be opened for reading, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
						configfile, prerr, slapd_pr_strerror(prerr));
		return rc; /* Fail */
	}
	else
	{
		/* read the entire file into core */
		buf = slapi_ch_malloc( prfinfo.size + 1 );
		if (( nr = slapi_read_buffer( prfd, buf, prfinfo.size )) < 0 )
		{
			slapi_log_error(SLAPI_LOG_FATAL, "config", "Could only read %d of %ld bytes from config file %s\n",
							nr, prfinfo.size, configfile);
			rc = 0; /* Fail */
			done= 1;
		}
                          
		(void)PR_Close(prfd);
		buf[ nr ] = '\0';

		if(!done)
		{
			char workpath[MAXPATHLEN+1];
			char loglevel[BUFSIZ];
			char maxdescriptors[BUFSIZ];
			char val[BUFSIZ];
			char _localuser[BUFSIZ];
			char logenabled[BUFSIZ];
			char schemacheck[BUFSIZ];
			char syntaxcheck[BUFSIZ];
			char syntaxlogging[BUFSIZ];
			char plugintracking[BUFSIZ];
			char dn_validate_strict[BUFSIZ];
			Slapi_DN plug_dn;

			workpath[0] = loglevel[0] = maxdescriptors[0] = '\0';
			val[0] = logenabled[0] = schemacheck[0] = syntaxcheck[0] = '\0';
			syntaxlogging[0] = _localuser[0] = '\0';
			plugintracking [0] = dn_validate_strict[0] = '\0';

			/* Convert LDIF to entry structures */
			slapi_sdn_init_ndn_byref(&plug_dn, PLUGIN_BASE_DN);
			while ((entrystr = dse_read_next_entry(buf, &lastp)) != NULL)
			{
				char errorbuf[BUFSIZ];
				/*
				 * XXXmcs: it would be better to also pass
				 * SLAPI_STR2ENTRY_REMOVEDUPVALS in the flags, but
				 * duplicate value checking requires that the syntax
				 * and schema subsystems be initialized... and they
				 * are not yet.
				 */
				Slapi_Entry	*e = slapi_str2entry(entrystr,
							SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF);
				if (e == NULL)
				{
					  LDAPDebug(LDAP_DEBUG_ANY, "The entry [%s] in the configfile %s was empty or could not be parsed\n",
								entrystr, configfile, 0);
					continue;
				}
				/* increase file descriptors */
#if !defined(_WIN32) && !defined(AIX)
				if (!maxdescriptors[0] &&
					entry_has_attr_and_value(e, CONFIG_MAXDESCRIPTORS_ATTRIBUTE,
									 maxdescriptors, sizeof(maxdescriptors)))
				{
					if (config_set_maxdescriptors(
									CONFIG_MAXDESCRIPTORS_ATTRIBUTE,
									maxdescriptors, errorbuf, CONFIG_APPLY)
						!= LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_MAXDESCRIPTORS_ATTRIBUTE, errorbuf);
					}
				}
#endif /* !defined(_WIN32) && !defined(AIX) */

				/* see if we need to enable error logging */
				if (!logenabled[0] &&
					entry_has_attr_and_value(e,
											 CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE,
											 logenabled, sizeof(logenabled)))
				{
					if (log_set_logging(
						CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE,
						logenabled, SLAPD_ERROR_LOG, errorbuf, CONFIG_APPLY)
						!= LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE, errorbuf);
					}
				}

#ifndef _WIN32
				/* set the local user name; needed to set up error log */
				if (!_localuser[0] &&
					entry_has_attr_and_value(e, CONFIG_LOCALUSER_ATTRIBUTE,
								_localuser, sizeof(_localuser)))
				{
					if (config_set_localuser(CONFIG_LOCALUSER_ATTRIBUTE,
						_localuser, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
								  CONFIG_LOCALUSER_ATTRIBUTE, errorbuf);
					}
				}
#endif
				
				/* set the log file name */
				workpath[0] = '\0';
				if (!workpath[0] &&
					entry_has_attr_and_value(e, CONFIG_ERRORLOG_ATTRIBUTE,
								workpath, sizeof(workpath)))
				{
					if (config_set_errorlog(CONFIG_ERRORLOG_ATTRIBUTE,
						workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
								  CONFIG_ERRORLOG_ATTRIBUTE, errorbuf);
					}
				}
				/* set the error log level */
				if (!loglevel[0] &&
					entry_has_attr_and_value(e, CONFIG_LOGLEVEL_ATTRIBUTE,
						loglevel, sizeof(loglevel)))
				{
					if (should_detach || !config_get_errorlog_level())
					{ /* -d wasn't on command line */
						if (config_set_errorlog_level(CONFIG_LOGLEVEL_ATTRIBUTE,
							loglevel, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
						{
							LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
									  CONFIG_LOGLEVEL_ATTRIBUTE, errorbuf);
						}
					}
					else
					{
						LDAPDebug(LDAP_DEBUG_ANY,
								  "%s: ignoring %s (since -d %d was given on "
								  "the command line)\n",
								  CONFIG_LOGLEVEL_ATTRIBUTE, loglevel,
								  config_get_errorlog_level());
					}
				}

				/* set the cert dir; needed in slapd_nss_init */
				workpath[0] = '\0';
				if (entry_has_attr_and_value(e, CONFIG_CERTDIR_ATTRIBUTE,
						workpath, sizeof(workpath)))
				{
					if (config_set_certdir(CONFIG_CERTDIR_ATTRIBUTE,
							workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
									  CONFIG_CERTDIR_ATTRIBUTE, errorbuf);
					}
				}

				/* set the sasl path; needed in main */
				 workpath[0] = '\0';
				if (entry_has_attr_and_value(e, CONFIG_SASLPATH_ATTRIBUTE,
						workpath, sizeof(workpath)))
				{
					if (config_set_saslpath(CONFIG_SASLPATH_ATTRIBUTE,
							workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
									  CONFIG_SASLPATH_ATTRIBUTE, errorbuf);
					}
				}
#if defined(ENABLE_LDAPI)
				/* set the ldapi file path; needed in main */
				workpath[0] = '\0';
				if (entry_has_attr_and_value(e, CONFIG_LDAPI_FILENAME_ATTRIBUTE,
						workpath, sizeof(workpath)))
				{
					if (config_set_ldapi_filename(CONFIG_LDAPI_FILENAME_ATTRIBUTE,
							workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
									  CONFIG_LDAPI_FILENAME_ATTRIBUTE, errorbuf);
					}
				}

				/* set the ldapi switch; needed in main */
				workpath[0] = '\0';
				if (entry_has_attr_and_value(e, CONFIG_LDAPI_SWITCH_ATTRIBUTE,
						workpath, sizeof(workpath)))
				{
					if (config_set_ldapi_switch(CONFIG_LDAPI_SWITCH_ATTRIBUTE,
							workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s. \n", configfile,
									  CONFIG_LDAPI_SWITCH_ATTRIBUTE, errorbuf);
					}
				}
#endif
				/* see if the entry is a child of the plugin base dn */
				if (slapi_sdn_isparent(&plug_dn,
									   slapi_entry_get_sdn_const(e)))
				{
					if (entry_has_attr_and_value(e, "objectclass",
												 "nsSlapdPlugin", 0) &&
						(entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
												 "syntax", 0) ||
						 entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
												  "matchingrule", 0)))
					{
						/* add the syntax/matching scheme rule plugin */
						if (plugin_setup(e, 0, 0, 1))
						{
							LDAPDebug(LDAP_DEBUG_ANY, "The plugin entry [%s] in the configfile %s was invalid\n", slapi_entry_get_dn(e), configfile, 0);
							rc = 0;
							slapi_sdn_done(&plug_dn);
							goto bail;
						}
					}
				}

				/* see if the entry is a grand child of the plugin base dn */
				if (slapi_sdn_isgrandparent(&plug_dn,
											slapi_entry_get_sdn_const(e)))
				{
					if (entry_has_attr_and_value(e, "objectclass",
												 "nsSlapdPlugin", 0) &&
						(	entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
												"pwdstoragescheme", 0) ||
							entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
												"reverpwdstoragescheme", 0)	) )
					{
						/* add the pwd storage scheme rule plugin */
						if (plugin_setup(e, 0, 0, 1))
						{
							LDAPDebug(LDAP_DEBUG_ANY, "The plugin entry [%s] in the configfile %s was invalid\n", slapi_entry_get_dn(e), configfile, 0);
							rc = 0;
							slapi_sdn_done(&plug_dn);
							goto bail;
						}
					}
				}

				/* see if we need to disable schema checking */
				if (!schemacheck[0] &&
					entry_has_attr_and_value(e, CONFIG_SCHEMACHECK_ATTRIBUTE,
											 schemacheck, sizeof(schemacheck)))
				{
					if (config_set_schemacheck(CONFIG_SCHEMACHECK_ATTRIBUTE,
								schemacheck, errorbuf, CONFIG_APPLY)
								!= LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_SCHEMACHECK_ATTRIBUTE, errorbuf);
					}
				}

				/* see if we need to enable plugin binddn tracking */
				if (!plugintracking[0] &&
					entry_has_attr_and_value(e, CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE,
											 plugintracking, sizeof(plugintracking)))
				{
					if (config_set_plugin_tracking(CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE,
							plugintracking, errorbuf, CONFIG_APPLY)
								!= LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE, errorbuf);
					}
				}

				/* see if we need to enable syntax checking */
				if (!syntaxcheck[0] &&
				    entry_has_attr_and_value(e, CONFIG_SYNTAXCHECK_ATTRIBUTE,
				    syntaxcheck, sizeof(syntaxcheck)))
				{
					if (config_set_syntaxcheck(CONFIG_SYNTAXCHECK_ATTRIBUTE,
					                           syntaxcheck, errorbuf, CONFIG_APPLY)
						                   != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
						          CONFIG_SYNTAXCHECK_ATTRIBUTE, errorbuf);
					}
				}

				/* see if we need to enable syntax warnings */
				if (!syntaxlogging[0] &&
				    entry_has_attr_and_value(e, CONFIG_SYNTAXLOGGING_ATTRIBUTE,
				    syntaxlogging, sizeof(syntaxlogging)))
				{
					if (config_set_syntaxlogging(CONFIG_SYNTAXLOGGING_ATTRIBUTE,
					                          syntaxlogging, errorbuf, CONFIG_APPLY)
					                          != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
						          CONFIG_SYNTAXLOGGING_ATTRIBUTE, errorbuf);
					}
				}

				/* see if we need to enable strict dn validation */
				if (!dn_validate_strict[0] &&
				    entry_has_attr_and_value(e, CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE,
				    dn_validate_strict, sizeof(dn_validate_strict)))
				{
					if (config_set_dn_validate_strict(CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE,
					                           dn_validate_strict, errorbuf, CONFIG_APPLY)
					                           != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
						          CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE, errorbuf);
					}
				}

				/* see if we need to expect quoted schema values */
				if (entry_has_attr_and_value(e, CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE,
											 val, sizeof(val)))
				{
					if (config_set_enquote_sup_oc(
								CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, val, errorbuf, 
								CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, errorbuf);
					}
					val[0] = 0;
				}

				/* see if we need to maintain case in AT and OC names */
				if (entry_has_attr_and_value(e,
						CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, val, sizeof(val)))
				{
					if (config_set_return_exact_case(
								CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, val,
								errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, errorbuf);
					}
					val[0] = 0;
				}

				/* see if we should allow attr. name exceptions, e.g. '_'s */
				if (entry_has_attr_and_value(e,
						CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE,
						val, sizeof(val)))
				{
					if (config_set_attrname_exceptions(
								CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE, val,
								errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE,
								  errorbuf);
					}
					val[0] = 0;
				}

				/* see if we need to maintain schema compatibility with 4.x */
				if (entry_has_attr_and_value(e,
						CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, val, sizeof(val)))
				{
					if (config_set_ds4_compatible_schema(
								CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, val,
								errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE,
								  errorbuf);
					}
					val[0] = 0;
				}

				/* see if we need to allow trailing spaces in OC and AT names */
				if (entry_has_attr_and_value(e,
						CONFIG_SCHEMA_IGNORE_TRAILING_SPACES, val, sizeof(val)))
				{
					if (config_set_schema_ignore_trailing_spaces(
								CONFIG_SCHEMA_IGNORE_TRAILING_SPACES, val,
								errorbuf, CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_SCHEMA_IGNORE_TRAILING_SPACES,
								  errorbuf);
					}
					val[0] = 0;
				}

				/* rfc1274-rewrite */
				if (entry_has_attr_and_value(e, 
							     CONFIG_REWRITE_RFC1274_ATTRIBUTE,
							     val, sizeof(val))) {
				  if (config_set_rewrite_rfc1274(
								 CONFIG_REWRITE_RFC1274_ATTRIBUTE, val, 
								 errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
				    LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", 
					      configfile,
					      CONFIG_REWRITE_RFC1274_ATTRIBUTE, 
					      errorbuf);
				  }
				  val[0] = 0;
				}

				/* what is our localhost name */
				if (entry_has_attr_and_value(e, CONFIG_LOCALHOST_ATTRIBUTE,
											 val, sizeof(val)))
				{
					if (config_set_localhost(
								CONFIG_LOCALHOST_ATTRIBUTE, val, errorbuf, 
								CONFIG_APPLY) != LDAP_SUCCESS)
					{
						LDAPDebug(LDAP_DEBUG_ANY, "%s: %s: %s\n", configfile,
								  CONFIG_LOCALHOST_ATTRIBUTE, errorbuf);
					}
					val[0] = 0;
				}

				if (e)
					slapi_entry_free(e);
			}
			/* kexcoff: initialize rootpwstoragescheme and pw_storagescheme
			 *			if not explicilty set in the config file
			 */
			if ( config_set_storagescheme() ) {		/* default scheme plugin not loaded */
				slapi_log_error(SLAPI_LOG_FATAL, "startup",
								"The default password storage scheme SSHA could not be read or was not found in the file %s. It is mandatory.\n",
								configfile);
				exit (1);
			}
			else {
				slapi_sdn_done(&plug_dn);
				rc= 1; /* OK */
			}
		}

		slapi_ch_free_string(&buf);
	}

bail:
	slapi_ch_free_string(&buf);
	return rc;
}

