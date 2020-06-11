/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* config.c - configuration file handling routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include "slap.h"
#include "pw.h"
#include <sys/stat.h>
#include <prio.h>

#define MAXARGS 1000

extern int should_detach;
extern Slapi_PBlock *repl_pb;
extern char *slapd_SSL3ciphers;
extern char *localuser;
char *rel2abspath(char *);

/*
 * WARNING - this can only bootstrap PASSWORD and SYNTAX plugins!
 * see fedse.c instead!
 */
static char *bootstrap_plugins[] = {
    "dn: cn=PBKDF2_SHA256,cn=Password Storage Schemes,cn=plugins,cn=config\n"
    "objectclass: top\n"
    "objectclass: nsSlapdPlugin\n"
    "cn: PBKDF2_SHA256\n"
    "nsslapd-pluginpath: libpwdstorage-plugin\n"
    "nsslapd-plugininitfunc: pbkdf2_sha256_pwd_storage_scheme_init\n"
    "nsslapd-plugintype: pwdstoragescheme\n"
    "nsslapd-pluginenabled: on",

#ifdef RUST_ENABLE
    "dn: cn=entryuuid_syntax,cn=plugins,cn=config\n"
    "objectclass: top\n"
    "objectclass: nsSlapdPlugin\n"
    "cn: entryuuid_syntax\n"
    "nsslapd-pluginpath: libentryuuid-syntax-plugin\n"
    "nsslapd-plugininitfunc: entryuuid_syntax_plugin_init\n"
    "nsslapd-plugintype: syntax\n"
    "nsslapd-pluginenabled: on\n"
    "nsslapd-pluginId: entryuuid_syntax\n"
    "nsslapd-pluginVersion: none\n"
    "nsslapd-pluginVendor: 389 Project\n"
    "nsslapd-pluginDescription: entryuuid_syntax\n",
#endif

    NULL
};

/*
  See if the given entry has an attribute with the given name and the
  given value; if value is NULL, just test for the presence of the given
  attribute; if value is an empty string (i.e. value[0] == 0),
  the first value in the attribute will be copied into the given buffer
  and returned
*/
static int
entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value, size_t valuebufsize)
{
    int retval = 0;
    Slapi_Attr *attr = 0;
    if (!e || !attrname)
        return retval;

    /* see if the entry has the specified attribute name */
    if (!slapi_entry_attr_find(e, attrname, &attr) && attr) {
        /* if value is not null, see if the attribute has that
           value */
        if (!value) {
            retval = 1;
        } else {
            Slapi_Value *v = 0;
            int index = 0;
            for (index = slapi_attr_first_value(attr, &v);
                 v && (index != -1);
                 index = slapi_attr_next_value(attr, index, &v)) {
                const char *s = slapi_value_get_string(v);
                if (!s)
                    continue;

                if (!*value) {
                    size_t len = strlen(s);

                    if (len < valuebufsize) {
                        strcpy(value, s);
                        retval = 1;
                    } else {
                        slapi_log_err(SLAPI_LOG_ERR, "bootstrap config",
                                      "Ignoring extremely large value for"
                                      " configuration attribute %s"
                                      " (length=%ld, value=%40.40s...)\n",
                                      attrname, (long int)len, s);
                        retval = 0; /* value is too large: ignore it */
                    }
                    break;
                } else if (!strcasecmp(s, value)) {
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
    char configfile[MAXPATHLEN + 1];
    PRFileInfo64 prfinfo;
    int rc = 0; /* Fail */
    int done = 0;
    PRInt32 nr = 0;
    PRFileDesc *prfd = 0;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    char *buf = 0;
    char *lastp = 0;
    char *entrystr = 0;
    char tmpfile[MAXPATHLEN + 1];

    if (NULL == configdir) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                      "Passed null config directory\n");
        return rc; /* Fail */
    }
    PR_snprintf(configfile, sizeof(configfile), "%s/%s", configdir, CONFIG_FILENAME);
    PR_snprintf(tmpfile, sizeof(tmpfile), "%s/%s.bak", configdir, CONFIG_FILENAME);
    rc = dse_check_file(configfile, tmpfile);
    if (rc == 0) {
        /* EVERYTHING IS GOING WRONG, ARRGHHHHHH */
        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "No valid configurations can be accessed! You must restore %s from backup!\n", configfile);
        return 0;
    }

    if ((rc = PR_GetFileInfo64(configfile, &prfinfo)) != PR_SUCCESS) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                      "The given config file %s could not be accessed, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      configfile, prerr, slapd_pr_strerror(prerr));
        return rc;
    } else if ((prfd = PR_Open(configfile, PR_RDONLY,
                               SLAPD_DEFAULT_FILE_MODE)) == NULL) {
        PRErrorCode prerr = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                      "The given config file %s could not be opened for reading, " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                      configfile, prerr, slapd_pr_strerror(prerr));
        return rc; /* Fail */
    } else {
        /* read the entire file into core */
        buf = slapi_ch_malloc(prfinfo.size + 1);
        if ((nr = slapi_read_buffer(prfd, buf, prfinfo.size)) < 0) {
            slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                          "Could only read %d of %ld bytes from config file %s\n",
                          nr, (long int)prfinfo.size, configfile);
            rc = 0; /* Fail */
            done = 1;
        }

        (void)PR_Close(prfd);
        buf[nr] = '\0';

        if (!done) {
            char workpath[MAXPATHLEN + 1];
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
            char moddn_aci[BUFSIZ];
            Slapi_DN plug_dn;

            workpath[0] = loglevel[0] = maxdescriptors[0] = '\0';
            val[0] = logenabled[0] = schemacheck[0] = syntaxcheck[0] = '\0';
            syntaxlogging[0] = _localuser[0] = '\0';
            plugintracking[0] = dn_validate_strict[0] = moddn_aci[0] = '\0';

            /* Convert LDIF to entry structures */
            slapi_sdn_init_ndn_byref(&plug_dn, PLUGIN_BASE_DN);
            while ((entrystr = dse_read_next_entry(buf, &lastp)) != NULL) {
                char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
                /*
                 * XXXmcs: it would be better to also pass
                 * SLAPI_STR2ENTRY_REMOVEDUPVALS in the flags, but
                 * duplicate value checking requires that the syntax
                 * and schema subsystems be initialized... and they
                 * are not yet.
                 */
                Slapi_Entry *e = slapi_str2entry(entrystr,
                                                 SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF);
                if (e == NULL) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - "
                                                 "The entry [%s] in the configfile %s was empty or could not be parsed\n",
                                  entrystr, configfile, 0);
                    continue;
                }
                /* increase file descriptors */
                if (!maxdescriptors[0] &&
                    entry_has_attr_and_value(e, CONFIG_MAXDESCRIPTORS_ATTRIBUTE,
                                             maxdescriptors, sizeof(maxdescriptors))) {
                    if (config_set_maxdescriptors(
                            CONFIG_MAXDESCRIPTORS_ATTRIBUTE,
                            maxdescriptors, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - "
                                                     "%s: %s: %s\n",
                                      configfile, CONFIG_MAXDESCRIPTORS_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to enable error logging */
                if (!logenabled[0] &&
                    entry_has_attr_and_value(e,
                                             CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE,
                                             logenabled, sizeof(logenabled))) {
                    if (log_set_logging(
                            CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE,
                            logenabled, SLAPD_ERROR_LOG, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - %s: %s: %s\n",
                                      configfile, CONFIG_ERRORLOG_LOGGING_ENABLED_ATTRIBUTE, errorbuf);
                    }
                }

                /* set the local user name; needed to set up error log */
                if (!_localuser[0] &&
                    entry_has_attr_and_value(e, CONFIG_LOCALUSER_ATTRIBUTE,
                                             _localuser, sizeof(_localuser))) {
                    if (config_set_localuser(CONFIG_LOCALUSER_ATTRIBUTE,
                                             _localuser, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - %s: %s: %s. \n",
                                      configfile, CONFIG_LOCALUSER_ATTRIBUTE, errorbuf);
                    }
                }

                /* set the log file name */
                workpath[0] = '\0';
                if (!workpath[0] &&
                    entry_has_attr_and_value(e, CONFIG_ERRORLOG_ATTRIBUTE,
                                             workpath, sizeof(workpath))) {
                    if (config_set_errorlog(CONFIG_ERRORLOG_ATTRIBUTE,
                                            workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - %s: %s: %s. \n",
                                      configfile, CONFIG_ERRORLOG_ATTRIBUTE, errorbuf);
                    }
                }
                /* set the error log level */
                if (!loglevel[0] &&
                    entry_has_attr_and_value(e, CONFIG_LOGLEVEL_ATTRIBUTE,
                                             loglevel, sizeof(loglevel))) {
                    if (should_detach || !config_get_errorlog_level()) { /* -d wasn't on command line */
                        if (config_set_errorlog_level(CONFIG_LOGLEVEL_ATTRIBUTE,
                                                      loglevel, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                            slapi_log_err(SLAPI_LOG_ERR, "%s: %s: %s. \n", configfile,
                                          CONFIG_LOGLEVEL_ATTRIBUTE, errorbuf);
                        }
                    }
                }

                /* set the cert dir; needed in slapd_nss_init */
                workpath[0] = '\0';
                if (entry_has_attr_and_value(e, CONFIG_CERTDIR_ATTRIBUTE,
                                             workpath, sizeof(workpath))) {
                    if (config_set_certdir(CONFIG_CERTDIR_ATTRIBUTE,
                                           workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s. \n",
                                      configfile, CONFIG_CERTDIR_ATTRIBUTE, errorbuf);
                    }
                }

                /* set the sasl path; needed in main */
                workpath[0] = '\0';
                if (entry_has_attr_and_value(e, CONFIG_SASLPATH_ATTRIBUTE,
                                             workpath, sizeof(workpath))) {
                    if (config_set_saslpath(CONFIG_SASLPATH_ATTRIBUTE,
                                            workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s. \n",
                                      configfile, CONFIG_SASLPATH_ATTRIBUTE, errorbuf);
                    }
                }
#if defined(ENABLE_LDAPI)
                /* set the ldapi file path; needed in main */
                workpath[0] = '\0';
                if (entry_has_attr_and_value(e, CONFIG_LDAPI_FILENAME_ATTRIBUTE,
                                             workpath, sizeof(workpath))) {
                    if (config_set_ldapi_filename(CONFIG_LDAPI_FILENAME_ATTRIBUTE,
                                                  workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s. \n",
                                      configfile, CONFIG_LDAPI_FILENAME_ATTRIBUTE, errorbuf);
                    }
                }

                /* set the ldapi switch; needed in main */
                workpath[0] = '\0';
                if (entry_has_attr_and_value(e, CONFIG_LDAPI_SWITCH_ATTRIBUTE,
                                             workpath, sizeof(workpath))) {
                    if (config_set_ldapi_switch(CONFIG_LDAPI_SWITCH_ATTRIBUTE,
                                                workpath, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s. \n",
                                      configfile, CONFIG_LDAPI_SWITCH_ATTRIBUTE, errorbuf);
                    }
                }
#endif
                /* see if the entry is a child of the plugin base dn */
                if (slapi_sdn_isparent(&plug_dn,
                                       slapi_entry_get_sdn_const(e))) {
                    if (entry_has_attr_and_value(e, "objectclass",
                                                 "nsSlapdPlugin", 0) &&
                        (entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
                                                  "syntax", 0) ||
                         entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
                                                  "matchingrule", 0))) {
                        /* add the syntax/matching scheme rule plugin */
                        if (plugin_setup(e, 0, 0, 1, returntext)) {
                            slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                                          "The plugin entry [%s] in the configfile %s was invalid. %s\n",
                                          slapi_entry_get_dn(e), configfile, returntext);
                            rc = 0;
                            slapi_sdn_done(&plug_dn);
                            goto bail;
                        }
                    }
                }

                /* see if the entry is a grand child of the plugin base dn */
                if (slapi_sdn_isgrandparent(&plug_dn, slapi_entry_get_sdn_const(e))) {
                    if (entry_has_attr_and_value(e, "objectclass",
                                                 "nsSlapdPlugin", 0) &&
                        (entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
                                                  "pwdstoragescheme", 0) ||
                         entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE,
                                                  "reverpwdstoragescheme", 0))) {
                        /* add the pwd storage scheme rule plugin */
                        if (plugin_setup(e, 0, 0, 1, returntext)) {
                            slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                                          "The plugin entry [%s] in the configfile %s was invalid. %s\n",
                                          slapi_entry_get_dn(e), configfile, returntext);
                            rc = 0;
                            slapi_sdn_done(&plug_dn);
                            goto bail;
                        }
                    }
                }

                /* see if we need to disable schema checking */
                if (!schemacheck[0] &&
                    entry_has_attr_and_value(e, CONFIG_SCHEMACHECK_ATTRIBUTE,
                                             schemacheck, sizeof(schemacheck))) {
                    if (config_set_schemacheck(CONFIG_SCHEMACHECK_ATTRIBUTE,
                                               schemacheck, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_SCHEMACHECK_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to enable plugin binddn tracking */
                if (!plugintracking[0] &&
                    entry_has_attr_and_value(e, CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE,
                                             plugintracking, sizeof(plugintracking))) {
                    if (config_set_plugin_tracking(CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE,
                                                   plugintracking, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_PLUGIN_BINDDN_TRACKING_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we allow moddn aci */
                if (!moddn_aci[0] &&
                    entry_has_attr_and_value(e, CONFIG_MODDN_ACI_ATTRIBUTE,
                                             moddn_aci, sizeof(moddn_aci))) {
                    if (config_set_moddn_aci(CONFIG_MODDN_ACI_ATTRIBUTE,
                                             moddn_aci, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_MODDN_ACI_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to enable syntax checking */
                if (!syntaxcheck[0] &&
                    entry_has_attr_and_value(e, CONFIG_SYNTAXCHECK_ATTRIBUTE,
                                             syntaxcheck, sizeof(syntaxcheck))) {
                    if (config_set_syntaxcheck(CONFIG_SYNTAXCHECK_ATTRIBUTE,
                                               syntaxcheck, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_SYNTAXCHECK_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to enable syntax warnings */
                if (!syntaxlogging[0] &&
                    entry_has_attr_and_value(e, CONFIG_SYNTAXLOGGING_ATTRIBUTE,
                                             syntaxlogging, sizeof(syntaxlogging))) {
                    if (config_set_syntaxlogging(CONFIG_SYNTAXLOGGING_ATTRIBUTE,
                                                 syntaxlogging, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_SYNTAXLOGGING_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to enable strict dn validation */
                if (!dn_validate_strict[0] &&
                    entry_has_attr_and_value(e, CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE,
                                             dn_validate_strict, sizeof(dn_validate_strict))) {
                    if (config_set_dn_validate_strict(CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE,
                                                      dn_validate_strict, errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config - %s: %s: %s\n",
                                      configfile, CONFIG_DN_VALIDATE_STRICT_ATTRIBUTE, errorbuf);
                    }
                }

                /* see if we need to expect quoted schema values */
                if (entry_has_attr_and_value(e, CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE,
                                             val, sizeof(val))) {
                    if (config_set_enquote_sup_oc(
                            CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, val, errorbuf,
                            CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                /* see if we need to maintain case in AT and OC names */
                if (entry_has_attr_and_value(e,
                                             CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, val, sizeof(val))) {
                    if (config_set_return_exact_case(
                            CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, val,
                            errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_RETURN_EXACT_CASE_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                /* see if we should allow attr. name exceptions, e.g. '_'s */
                if (entry_has_attr_and_value(e,
                                             CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE,
                                             val, sizeof(val))) {
                    if (config_set_attrname_exceptions(
                            CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE, val,
                            errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_ATTRIBUTE_NAME_EXCEPTION_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                /* see if we need to maintain schema compatibility with 4.x */
                if (entry_has_attr_and_value(e,
                                             CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, val, sizeof(val))) {
                    if (config_set_ds4_compatible_schema(
                            CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, val,
                            errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_DS4_COMPATIBLE_SCHEMA_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                /* see if we need to allow trailing spaces in OC and AT names */
                if (entry_has_attr_and_value(e,
                                             CONFIG_SCHEMA_IGNORE_TRAILING_SPACES, val, sizeof(val))) {
                    if (config_set_schema_ignore_trailing_spaces(
                            CONFIG_SCHEMA_IGNORE_TRAILING_SPACES, val,
                            errorbuf, CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_SCHEMA_IGNORE_TRAILING_SPACES, errorbuf);
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
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_REWRITE_RFC1274_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                /* what is our localhost name */
                if (entry_has_attr_and_value(e, CONFIG_LOCALHOST_ATTRIBUTE,
                                             val, sizeof(val))) {
                    if (config_set_localhost(
                            CONFIG_LOCALHOST_ATTRIBUTE, val, errorbuf,
                            CONFIG_APPLY) != LDAP_SUCCESS) {
                        slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config", "%s: %s: %s\n",
                                      configfile, CONFIG_LOCALHOST_ATTRIBUTE, errorbuf);
                    }
                    val[0] = 0;
                }

                if (e) {
                    slapi_entry_free(e);
                }
            } /* (entrystr = dse_read_next_entry(buf, &lastp) */
            /*
             * Okay, now we have to add "fake" plugins into memory
             * so that password can work. They'll be created properly
             * later in dse.ldif.
             */

            for (size_t i = 0; bootstrap_plugins[i] != NULL; i++) {
                /* Convert the str to an entry */
                char *temp = strdup(bootstrap_plugins[i]);
                Slapi_Entry *e = slapi_str2entry(temp, 0);
                slapi_ch_free_string(&temp);
                /* Try and apply it */
                if (e == NULL) {
                    continue;
                }
                rc = plugin_setup(e, 0, 0, 1, returntext);
                if (rc == 0) {
                    slapi_log_err(SLAPI_LOG_TRACE, "slapd_bootstrap_config", "Application of plugin SUCCESS\n");
                } else if (rc == 1) {
                    /* It means that the plugin entry already exists */
                    slapi_log_err(SLAPI_LOG_TRACE, "slapd_bootstrap_config",
                                  "The plugin entry [%s] in the configfile %s was invalid. %s\n",
                                  slapi_entry_get_dn(e), configfile, returntext);
                } else {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                                  "The plugin entry [%s] in the configfile %s was invalid. %s\n",
                                  slapi_entry_get_dn(e), configfile, returntext);
                }
                slapi_entry_free(e);
            }

            /* kexcoff: initialize rootpwstoragescheme and pw_storagescheme
             *          if not explicilty set in the config file
             */
            if (config_set_storagescheme()) {
                /* default scheme plugin not loaded */
                slapi_log_err(SLAPI_LOG_ERR, "slapd_bootstrap_config",
                              "The default password storage scheme could not be read or was not found in the file %s. It is mandatory.\n", configfile);
                exit(1);
            } else {
                slapi_sdn_done(&plug_dn);
                rc = 1; /* OK */
            }
        }

        slapi_ch_free_string(&buf);
    }

bail:
    slapi_ch_free_string(&buf);
    return rc;
}
