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


#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#if defined(LINUX) || defined(__FreeBSD__) /* I bet other Unix would like \
                    * this flag. But don't want to                        \
                    * break other builds so far */
#include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "ldap.h"
#include "../slapi-plugin.h"
#include "../slap.h"
#include <nspr.h>
#include <nss.h>
#include "../../plugins/pwdstorage/pwdstorage.h"

int ldap_syslog;
int ldap_syslog_level;
/* int slapd_ldap_debug = LDAP_DEBUG_ANY; */
int detached;
FILE *error_logfp;
FILE *access_logfp;
struct pw_scheme *pwdhashscheme;
int heflag = 0;

static int slapd_config(const char *configdir, const char *configfile);
static int entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value);

static void
usage(char *name)
{
    fprintf(stderr, "usage: %s -D config-dir [-H] [-s scheme | -c comparepwd ] password...\n", name);
    exit(1);
}


/*
 * If global "heflag" is non-zero, un-hex-encode the string
 * and return a decoded copy.  Otherwise return a copy of the
 * string.
 */
static char *
decode(char *orig)
{
    char *r;

    if (NULL == orig) {
        return NULL;
    }
    r = slapi_ch_calloc(1, strlen(orig) + 2);
    strcpy(r, orig);

    if (heflag) {
        char *s;

        for (s = r; *s != '\0'; ++s) {
            if (*s == '%' && ldap_utf8isxdigit(s + 1) && ldap_utf8isxdigit(s + 2)) {
                memmove(s, s + 1, 2);
                s[2] = '\0';
                *s = strtoul(s, NULL, 16);
                memmove(s + 1, s + 3, strlen(s + 3) + 1);
            }
        }
    }
    return r;
}


static slapdFrontendConfig_t *
init_config(char *configdir)
{
    char *abs_configdir = NULL;
    char *configfile = NULL;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
    slapdFrontendConfig_t *slapdFrontendConfig = NULL;

    if (configdir == NULL) { /* use default */
        configdir = TEMPLATEDIR;
        configfile = "template-dse.ldif";
    }
    /* kexcoff: quite the same as slapd_bootstrap_config */
    FrontendConfig_init();

    abs_configdir = rel2abspath(configdir);
    if (config_set_configdir("configdir (-D)", abs_configdir,
                             errorbuf, 1) != LDAP_SUCCESS) {
        fprintf(stderr, "%s\n", errorbuf);
        slapi_ch_free_string(&abs_configdir);
        return (NULL);
    }
    slapi_ch_free_string(&abs_configdir);

    slapdFrontendConfig = getFrontendConfig();
    if (0 == slapd_config(slapdFrontendConfig->configdir, configfile)) {
        fprintf(stderr,
                "The configuration files in directory %s could not be read or were not found.  Please refer to the error log or output for more information.\n",
                slapdFrontendConfig->configdir);
        return (NULL);
    }

    return slapdFrontendConfig;
}


int
main(int argc, char *argv[])
{
    int i, rc;
    char *enc, *cmp, *name;
    char *decoded = NULL;
    struct pw_scheme *pwsp, *cmppwsp;
    extern int optind;
    char *cpwd = NULL; /* candidate password for comparison */
    slapdFrontendConfig_t *slapdFrontendConfig = NULL;

    char *opts = "Hs:c:D:";
    name = argv[0];
    pwsp = cmppwsp = NULL;

    PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

    /* Initialize NSS to make ds_salted_sha1_pw_enc() work */
    if (NSS_NoDB_Init(NULL) != SECSuccess) {
        fprintf(stderr, "Fatal error: unable to initialize the NSS subcomponent.");
        return (1);
    }


    while ((i = getopt(argc, argv, opts)) != EOF) {
        switch (i) {
        case 'D':
            if (slapdFrontendConfig) {
                fprintf(stderr, "The -D configdir argument must be given only once, and must be the first argument given\n");
                usage(name);
                return 1;
            }
            if (!(slapdFrontendConfig = init_config(optarg))) {
                return (1);
            }
            break;

        case 's': /* set hash scheme */
            if (!slapdFrontendConfig) {
                if (!(slapdFrontendConfig = init_config(NULL))) {
                    usage(name);
                    return (1);
                }
            }
            if ((pwsp = pw_name2scheme(optarg)) == NULL) {
                fprintf(stderr, "%s: unknown hash scheme \"%s\"\n", name,
                        optarg);
                return (1);
            }
            break;

        case 'c': /* compare encoded password to password */
            if (!slapdFrontendConfig) {
                if (!(slapdFrontendConfig = init_config(NULL))) {
                    usage(name);
                    return (1);
                }
            }
            cpwd = optarg;
            break;

        case 'H': /* password(s) is(are) hex-encoded */
            if (!slapdFrontendConfig) {
                if (!(slapdFrontendConfig = init_config(NULL))) {
                    usage(name);
                    return (1);
                }
            }
            heflag = 1;
            break;

        default:
            usage(name);
        }
    }

    if (!slapdFrontendConfig) {
        if (!init_config(NULL)) {
            usage(name);
            return (1);
        }
    }

    if (cpwd != NULL) {
        decoded = decode(cpwd);
        cmppwsp = pw_val2scheme(decoded, &cmp, 1);
    }

    if (cmppwsp != NULL && pwsp != NULL) {
        fprintf(stderr, "%s: do not use -s with -c\n", name);
        usage(name);
    }

    if (cmppwsp == NULL && pwsp == NULL) {
        if (slapdFrontendConfig != NULL) {
            char *rootschemename = config_get_rootpwstoragescheme();
            pwsp = pw_name2scheme(rootschemename);
            free(rootschemename);
        } else {
            pwsp = pw_name2scheme(DEFAULT_PASSWORD_SCHEME_NAME);
        }
    }

    if (argc <= optind) {
        usage(name);
    }

    if (cmppwsp == NULL && pwsp->pws_enc == NULL) {
        fprintf(stderr,
                "The scheme \"%s\" does not support password encoding.\n",
                pwsp->pws_name);
        rc = 1;
        goto out;
    }

    srand((int)time(NULL)); /* schemes such as crypt use random salt */

    for (rc = 0; optind < argc && rc == 0; ++optind) {
        if (cmppwsp == NULL) { /* encode passwords */
            decoded = decode(argv[optind]);
            if ((enc = (*pwsp->pws_enc)(decoded)) == NULL) {
                perror(name);
                rc = 1;
                slapi_ch_free_string(&decoded);
                goto out;
            }

            puts(enc);
            slapi_ch_free_string(&enc);
        } else { /* compare passwords */
            decoded = decode(argv[optind]);
            if ((rc = (*(cmppwsp->pws_cmp))(decoded, cmp)) == 0) {
                printf("%s: password ok.\n", name);
            } else {
                printf("%s: password does not match.\n", name);
            }
        }
        slapi_ch_free_string(&decoded);
    }

out:

    free_pw_scheme(pwsp);

    plugin_closeall(1 /* Close Backends */, 1 /* Close Globals */);

    /* Shutdown NSS to free values */
    (void)NSS_Shutdown();

    return (rc == 0 ? 0 : 1);
}

/* -------------------------------------------------------------- */

/*
    kexcoff: quite similar to slapd_bootstrap_config() from the server,
    but it only loads password storage scheme plugins
 */
static int
slapd_config(const char *configdir, const char *givenconfigfile)
{
    char configfile[MAXPATHLEN + 1];
    PRFileInfo64 prfinfo;
    int rc = 0; /* Fail */
    int done = 0;
    PRInt32 nr = 0;
    PRFileDesc *prfd = 0;
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    char *buf = 0;
    char *lastp = 0;
    char *entrystr = 0;
    char *rootschemename = NULL;

    if (!givenconfigfile) {
        givenconfigfile = CONFIG_FILENAME;
    }


    PR_snprintf(configfile, sizeof(configfile), "%s/%s", configdir, givenconfigfile);
    if ((rc = PR_GetFileInfo64(configfile, &prfinfo)) != PR_SUCCESS) {
        fprintf(stderr,
                "The given config file %s could not be accessed, error %d\n",
                configfile, rc);
        exit(1);
    } else if ((prfd = PR_Open(configfile, PR_RDONLY,
                               SLAPD_DEFAULT_FILE_MODE)) == NULL) {
        fprintf(stderr,
                "The given config file %s could not be read\n",
                configfile);
        exit(1);
    } else {
        /* read the entire file into core */
        buf = slapi_ch_malloc(prfinfo.size + 1);
        if ((nr = slapi_read_buffer(prfd, buf, prfinfo.size)) < 0) {
            fprintf(stderr,
                    "Could only read %d of %ld bytes from config file %s\n",
                    nr, (long int)prfinfo.size, configfile);
            exit(1);
        }

        (void)PR_Close(prfd);
        buf[nr] = '\0';

        if (!done) {
            /* Convert LDIF to entry structures */
            Slapi_DN plug_dn;
            slapi_sdn_init_dn_byref(&plug_dn, PLUGIN_BASE_DN);
            Slapi_DN config_dn;
            slapi_sdn_init_dn_byref(&config_dn, SLAPD_CONFIG_DN);
            while ((entrystr = dse_read_next_entry(buf, &lastp)) != NULL) {
                /*
                 * XXXmcs: it would be better to also pass
                 * SLAPI_STR2ENTRY_REMOVEDUPVALS in the flags, but
                 * duplicate value checking requires that the syntax
                 * and schema subsystems be initialized... and they
                 * are not yet.
                 */
                Slapi_Entry *e = slapi_str2entry(entrystr,  // this one
                                                 SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF);
                if (e == NULL) {
                    fprintf(stderr,
                            "The entry [%s] in the configfile %s was empty or could not be parsed\n",
                            entrystr, configfile);
                    continue;
                }

                /* see if the entry is a child of the plugin base dn */
                if (slapi_sdn_isgrandparent(&plug_dn,
                                            slapi_entry_get_sdn_const(e))) {
                    if (entry_has_attr_and_value(e, ATTR_PLUGIN_TYPE, "pwdstoragescheme")) {
                        /* add the syntax/matching/pwd storage scheme rule plugin */
                        /* Because add_entry is 1, plugin_entry is duplicated */
                        if (plugin_setup(e, 0, 0, 1, returntext))  // This one
                        {
                            fprintf(stderr,
                                    "The plugin entry [%s] in the configfile %s was invalid.  %s\n",
                                    slapi_entry_get_dn(e), configfile, returntext);
                            exit(1); /* yes this sucks, but who knows what else would go on if I did the right thing */
                        }
                    }
                } else if (slapi_sdn_compare(&config_dn, slapi_entry_get_sdn_const(e)) == 0) {
                    /* Get the root scheme out and initialise it (if it exists) */
                    slapi_ch_free_string(&rootschemename);
                    rootschemename = slapi_entry_attr_get_charptr(e, CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE);
                }

                slapi_entry_free(e);
            }

            slapi_sdn_done(&plug_dn);
            slapi_sdn_done(&config_dn);
            rc = 1; /* OK */
        }

        /* initialize rootpwstoragescheme and pw_storagescheme
         * in case they are not set by the configuration file.
         * This needs to be after we init the plugins else this fails to create the
         * scheme.
         */
        config_set_storagescheme();

        if (rootschemename != NULL) {
            config_set_rootpwstoragescheme(CONFIG_ROOTPWSTORAGESCHEME_ATTRIBUTE, rootschemename, NULL, 1);
            slapi_ch_free_string(&rootschemename);
        }

        slapi_ch_free_string(&buf);
    }

    return rc;
}

/*
    kexcoff: direclty copied fron the server code
  See if the given entry has an attribute with the given name and the
  given value; if value is NULL, just test for the presence of the given
  attribute; if value is an empty string (i.e. value[0] == 0),
  the first value in the attribute will be copied into the given buffer
  and returned
*/
static int
entry_has_attr_and_value(Slapi_Entry *e, const char *attrname, char *value)
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
                    strcpy(value, s);
                    retval = 1;
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
