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

/*
 * uid.c
 *
 * Implements a directory server pre-operation plugin to test
 * attributes for uniqueness within a defined subtree in the
 * directory.
 *
 * Called uid.c since the original purpose of the plugin was to
 * check the uid attribute in user entries.
 */
#include <slapi-plugin.h>
#include <portable.h>
#include <slapi-private.h>
#include <string.h>
#include "plugin-utils.h"
#include "nspr.h"

#if defined(LDAP_ERROR_LOGGING) && !defined(DEBUG)
#define DEBUG
#endif

#define UNTAGGED_PARAMETER 12

/* Quoting routine - this should be in a library somewhere (slapi?) */
int ldap_quote_filter_value(
    char *value,
    int len,
    char *out,
    int maxLen,
    int *outLen);


static int search_one_berval(Slapi_DN *baseDN, const char **attrNames, const struct berval *value, const char *requiredObjectClass, Slapi_DN *target, Slapi_DN **excludes);

/*
 * ISSUES:
 *   How should this plugin handle ACL issues?  It seems wrong to reject
 *   adds and modifies because there is already a conflicting UID, when
 *   the request would have failed because of an ACL check anyway.
 *
 *   This code currently defines a maximum filter string size of 512.  Is
 *   this large enough?
 *
 *   This code currently does not quote the value portion of the filter as
 *   it is created.  This is a bug.
 */

/* */
#define BEGIN do {
#define END   \
    }         \
    while (0) \
        ;

/*
 * Slapi plugin descriptor
 */
static char *plugin_name = "NSUniqueAttr";
static Slapi_PluginDesc
    pluginDesc = {
        "NSUniqueAttr", VENDOR, DS_PACKAGE_VERSION,
        "Enforce unique attribute values"};
static void *plugin_identity = NULL;
typedef struct attr_uniqueness_config
{
    const char **attrs;
    char *attr_friendly;
    Slapi_DN **subtrees;
    Slapi_DN **exclude_subtrees;
    PRBool unique_in_all_subtrees;
    char *top_entry_oc;
    char *subtree_entries_oc;
    struct attr_uniqueness_config *next;
} attr_uniqueness_config_t;

#define ATTR_UNIQUENESS_ATTRIBUTE_NAME      "uniqueness-attribute-name"
#define ATTR_UNIQUENESS_SUBTREES            "uniqueness-subtrees"
#define ATTR_UNIQUENESS_EXCLUDE_SUBTREES    "uniqueness-exclude-subtrees"
#define ATTR_UNIQUENESS_ACROSS_ALL_SUBTREES "uniqueness-across-all-subtrees"
#define ATTR_UNIQUENESS_TOP_ENTRY_OC        "uniqueness-top-entry-oc"
#define ATTR_UNIQUENESS_SUBTREE_ENTRIES_OC  "uniqueness-subtree-entries-oc"

static int getArguments(Slapi_PBlock *pb, char **attrName, char **markerObjectClass, char **requiredObjectClass);
static struct attr_uniqueness_config *uniqueness_entry_to_config(Slapi_PBlock *pb, Slapi_Entry *config_entry);

/*
 * More information about constraint failure
 */
static char *moreInfo =
    "Another entry with the same attribute value already exists (attribute: \"%s\")";

static void
free_uniqueness_config(struct attr_uniqueness_config *config)
{
    int i;

    for (i = 0; config->attrs && config->attrs[i]; i++) {
        slapi_ch_free_string((char **)&(config->attrs[i]));
    }
    for (i = 0; config->subtrees && config->subtrees[i]; i++) {
        slapi_sdn_free(&config->subtrees[i]);
    }
    for (i = 0; config->exclude_subtrees && config->exclude_subtrees[i]; i++) {
        slapi_sdn_free(&config->exclude_subtrees[i]);
    }
    slapi_ch_free((void **)&config->attrs);
    slapi_ch_free((void **)&config->subtrees);
    slapi_ch_free((void **)&config->exclude_subtrees);
    slapi_ch_free_string(&config->attr_friendly);
    slapi_ch_free_string((char **)&config->top_entry_oc);
    slapi_ch_free_string((char **)&config->subtree_entries_oc);
}

/*
 * New styles:
 * ----------
 *
 * uniqueness-attribute-name: uid
 * uniqueness-subtrees: dc=people,dc=example,dc=com
 * uniqueness-subtrees: dc=sales, dc=example,dc=com
 * uniqueness-exclude-subtrees: dc=machines, dc=examples, dc=com
 * uniqueness-across-all-subtrees: on
 *
 * or
 *
 * uniqueness-attribute-name: uid
 * uniqueness-top-entry-oc: organizationalUnit
 * uniqueness-subtree-entries-oc: person
 *
 * If both are present:
 *  - uniqueness-subtrees
 *  - uniqueness-top-entry-oc/uniqueness-subtree-entries-oc
 * Then uniqueness-subtrees has the priority
 *
 * Old styles:
 * ----------
 *
 * nsslapd-pluginarg0: uid
 * nsslapd-pluginarg1: dc=people,dc=example,dc=com
 * nsslapd-pluginarg2: dc=sales, dc=example,dc=com
 *
 * or
 *
 * nsslapd-pluginarg0: attribute=uid
 * nsslapd-pluginarg1: markerobjectclass=organizationalUnit
 * nsslapd-pluginarg2: requiredobjectclass=person
 *
 * From a Slapi_Entry of the config entry, it creates a attr_uniqueness_config.
 * It returns a (attr_uniqueness_config *) if the configuration is valid
 * Else it returns NULL
 */
static struct attr_uniqueness_config *
uniqueness_entry_to_config(Slapi_PBlock *pb, Slapi_Entry *config_entry)
{
    attr_uniqueness_config_t *tmp_config = NULL;
    char **values = NULL;
    int argc;
    char **argv = NULL;
    int rc = SLAPI_PLUGIN_SUCCESS;
    int i;
    int attrLen = 0;
    char *fp;
    int nb_subtrees = 0;

    if (config_entry == NULL) {
        rc = SLAPI_PLUGIN_FAILURE;
        goto done;
    }


    /* We are going to fill tmp_config in a first phase */
    if ((tmp_config = (attr_uniqueness_config_t *)slapi_ch_calloc(1, sizeof(attr_uniqueness_config_t))) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - "
                                                  "Failed to allocate configuration\n");
        rc = SLAPI_PLUGIN_FAILURE;
        goto done;
    } else {
        /* set these to -1 for config validation */
    }

    /* Check if this is new/old config style */
    slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (argc == 0) {
        /* This is new config style
                 * uniqueness-attribute-name: uid
                 * uniqueness-subtrees: dc=people,dc=example,dc=com
                 * uniqueness-subtrees: dc=sales, dc=example,dc=com
                 * uniqueness-across-all-subtrees: on
                 *
                 * or
                 *
                 * uniqueness-attribute-name: uid
                 * uniqueness-top-entry-oc: organizationalUnit
                 * uniqueness-subtree-entries-oc: person
                 */

        /* Attribute name of the attribute we are going to check value uniqueness */
        values = slapi_entry_attr_get_charray(config_entry, ATTR_UNIQUENESS_ATTRIBUTE_NAME);
        if (values) {
            for (i = 0; values && values[i]; i++)
                ;
            tmp_config->attrs = (const char **)slapi_ch_calloc(i + 1, sizeof(char *));
            for (i = 0; values && values[i]; i++) {
                tmp_config->attrs[i] = slapi_ch_strdup(values[i]);
                slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "uniqueness_entry_to_config - "
                                                             "Adding attribute %s to uniqueness set\n",
                              tmp_config->attrs[i]);
            }
            slapi_ch_array_free(values);
            values = NULL;
        }

        /* Subtrees where uniqueness is tested  */
        values = slapi_entry_attr_get_charray(config_entry, ATTR_UNIQUENESS_SUBTREES);
        if (values) {
            for (i = 0; values && values[i]; i++)
                ;
            /* slapi_ch_calloc never returns NULL unless the 2 args are 0 or negative. */
            tmp_config->subtrees = (Slapi_DN **)slapi_ch_calloc(i + 1, sizeof(Slapi_DN *));
            /* copy the valid subtree DN into the config */
            for (i = 0, nb_subtrees = 0; values && values[i]; i++) {
                if (slapi_dn_syntax_check(pb, values[i], 1)) { /* syntax check failed */
                    slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - "
                                                              "Invalid DN (skipped): %s\n",
                                  values[i]);
                    continue;
                }
                tmp_config->subtrees[nb_subtrees] = slapi_sdn_new_dn_byval(values[i]);
                nb_subtrees++;
            }
            slapi_ch_array_free(values);
            values = NULL;
        }

        /* Subtrees where uniqueness is explicitly ignored */
        values = slapi_entry_attr_get_charray(config_entry, ATTR_UNIQUENESS_EXCLUDE_SUBTREES);
        if (values) {
            for (i = 0; values && values[i]; i++)
                ;
            /* slapi_ch_calloc never returns NULL unless the 2 args are 0 or negative. */
            tmp_config->exclude_subtrees = (Slapi_DN **)slapi_ch_calloc(i + 1, sizeof(Slapi_DN *));
            /* copy the valid subtree DN into the config */
            for (i = 0, nb_subtrees = 0; values && values[i]; i++) {
                if (slapi_dn_syntax_check(pb, values[i], 1)) { /* syntax check failed */
                    slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - "
                                                              "Invalid DN (skipped): %s\n",
                                  values[i]);
                    continue;
                }
                tmp_config->exclude_subtrees[nb_subtrees] = slapi_sdn_new_dn_byval(values[i]);
                nb_subtrees++;
            }

            slapi_ch_array_free(values);
            values = NULL;
        }

        /* Uniqueness may be enforced accross all the subtrees, by default it is not */
        tmp_config->unique_in_all_subtrees = slapi_entry_attr_get_bool(config_entry, ATTR_UNIQUENESS_ACROSS_ALL_SUBTREES);

        /* enforce uniqueness only if the modified entry has this objectclass */
        tmp_config->top_entry_oc = slapi_entry_attr_get_charptr(config_entry, ATTR_UNIQUENESS_TOP_ENTRY_OC);

        /* enforce uniqueness, in the modified entry subtree, only to entries having this objectclass */
        tmp_config->subtree_entries_oc = slapi_entry_attr_get_charptr(config_entry, ATTR_UNIQUENESS_SUBTREE_ENTRIES_OC);

    } else {
        int result;
        char *attrName = NULL;
        char *markerObjectClass = NULL;
        char *requiredObjectClass = NULL;

        /* using the old style of configuration */
        result = getArguments(pb, &attrName, &markerObjectClass, &requiredObjectClass);
        if (LDAP_OPERATIONS_ERROR == result) {
            slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "uniqueness_entry_to_config - "
                                                         "Unable to parse old style\n");
            rc = SLAPI_PLUGIN_FAILURE;
            goto done;
        }
        if (UNTAGGED_PARAMETER == result) {
            /* This is
                         * nsslapd-pluginarg0: uid
                         * nsslapd-pluginarg1: dc=people,dc=example,dc=com
                         * nsslapd-pluginarg2: dc=sales, dc=example,dc=com
                         *
                         * config attribute are in argc/argv
                         *
                         * attrName is set
                         * markerObjectClass/requiredObjectClass are NOT set
                         */

            if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc) || slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv)) {
                slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "uniqueness_entry_to_config - "
                                                             "Only attribute name is valid\n");
                rc = SLAPI_PLUGIN_FAILURE;
                goto done;
            }

            /* Store attrName in the config */
            tmp_config->attrs = (const char **)slapi_ch_calloc(2, sizeof(char *));
            tmp_config->attrs[0] = slapi_ch_strdup(attrName);
            argc--;
            argv++; /* First argument was attribute name and remaining are subtrees */

            /* Store the subtrees */
            nb_subtrees = 0;
            if ((tmp_config->subtrees = (Slapi_DN **)slapi_ch_calloc(argc + 1, sizeof(Slapi_DN *))) == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - "
                                                          "Fail to allocate subtree array\n");
                rc = SLAPI_PLUGIN_FAILURE;
                goto done;
            }


            for (; argc > 0; argc--, argv++) {
                if (slapi_dn_syntax_check(pb, *argv, 1)) { /* syntax check failed */
                    slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - "
                                                              "Invalid DN  (skipped): %s\n",
                                  *argv);
                    continue;
                }
                tmp_config->subtrees[nb_subtrees] = slapi_sdn_new_dn_byval(*argv);
                nb_subtrees++;
            }

            /* this interface does not configure accross subtree uniqueness*/
            tmp_config->unique_in_all_subtrees = PR_FALSE;

            /* Not really usefull, but it clarifies the config */
            tmp_config->subtree_entries_oc = NULL;
            tmp_config->top_entry_oc = NULL;
        } else {
            /* This is
                         * nsslapd-pluginarg0: attribute=uid
                         * nsslapd-pluginarg1: markerobjectclass=organizationalUnit
                         * nsslapd-pluginarg2: requiredobjectclass=person
                         *
                         * config attributes are in
                         *  - attrName
                         *  - markerObjectClass
                         *  - requiredObjectClass
                         */
            /* Store attrName in the config */
            tmp_config->attrs = (const char **)slapi_ch_calloc(2, sizeof(char *));
            tmp_config->attrs[0] = slapi_ch_strdup(attrName);

            /* There is no subtrees */
            tmp_config->subtrees = NULL;

            /* this interface does not configure accross subtree uniqueness*/
            tmp_config->unique_in_all_subtrees = PR_FALSE;

            /* set the objectclasses retrieved by getArgument */
            tmp_config->subtree_entries_oc = slapi_ch_strdup(requiredObjectClass);
            tmp_config->top_entry_oc = slapi_ch_strdup(markerObjectClass);
        }
    }

    /* Time to check that the new configuration is valid */
    /* Check that we have 1 or more value */
    if (tmp_config->attrs == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - Attribute name not defined \n");
        rc = SLAPI_PLUGIN_FAILURE;
        goto done;
    }
    /* If the config is valid, prepare the friendly string for error messages */
    for (i = 0; tmp_config->attrs && (tmp_config->attrs)[i]; i++) {
        attrLen += strlen((tmp_config->attrs)[i]) + 1;
    }
    tmp_config->attr_friendly = (char *)slapi_ch_calloc(attrLen + 1, sizeof(char));
    fp = tmp_config->attr_friendly;
    for (i = 0; tmp_config->attrs && (tmp_config->attrs)[i]; i++) {
        strcpy(fp, (tmp_config->attrs)[i]);
        fp += strlen((tmp_config->attrs)[i]);
        strcpy(fp, " ");
        fp++;
    }

    /* Only ensure subtrees are set, no need to check excluded subtrees as setting exclusion without actual subtrees make little sense */
    if (tmp_config->subtrees == NULL) {
        /* Uniqueness is enforced on entries matching objectclass */
        if (tmp_config->subtree_entries_oc == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - Objectclass for subtree entries is not defined\n");
            rc = SLAPI_PLUGIN_FAILURE;
            goto done;
        }
    } else if (tmp_config->subtrees[0] == NULL) {
        /* Uniqueness is enforced on subtrees but none are defined */
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "uniqueness_entry_to_config - No valid subtree is defined \n");
        rc = SLAPI_PLUGIN_FAILURE;
        goto done;
    }

done:
    if (rc != SLAPI_PLUGIN_SUCCESS) {
        if (tmp_config) {
            free_uniqueness_config(tmp_config);
            slapi_ch_free((void **)&tmp_config);
        }
        return NULL;
    } else {

        return tmp_config;
    }
}

static void
freePblock(Slapi_PBlock *spb)
{
    if (spb) {
        slapi_free_search_results_internal(spb);
        slapi_pblock_destroy(spb);
    }
}

/* ------------------------------------------------------------ */
/*
 * op_error - Record (and report) an operational error.
 * name changed to uid_op_error so as not to conflict with the external function
 * of the same name thereby preventing compiler warnings.
 */
static int
uid_op_error(int internal_error)
{
    slapi_log_err(
        SLAPI_LOG_PLUGIN,
        plugin_name,
        "Internal error: %d\n",
        internal_error);

    return LDAP_OPERATIONS_ERROR;
}

/* ------------------------------------------------------------ */
/*
 * Create an LDAP search filter from the attribute
 *   name and value supplied.
 */

static char *
create_filter(const char **attributes, const struct berval *value, const char *requiredObjectClass)
{
    char *filter = NULL;
    char *fp;
    char *max;
    int *attrLen = NULL;
    int totalAttrLen = 0;
    int attrCount = 0;
    int valueLen;
    int classLen = 0;
    int filterLen;
    int i = 0;

    PR_ASSERT(attributes);

    /* Compute the length of the required buffer */
    for (attrCount = 0; attributes && attributes[attrCount]; attrCount++)
        ;
    attrCount++;
    attrLen = (int *)slapi_ch_calloc(attrCount, sizeof(int));
    for (i = 0; attributes && attributes[i]; i++) {
        attrLen[i] += strlen(attributes[i]);
        totalAttrLen += attrLen[i];
    }

    /* if attrCount is 1, attrLen is already corect for usage.*/
    if (attrCount > 1) {
        /* Filter will be (|(attr=value)(attr=value)) */
        /* 3 for the (| ) */
        /* 3 for each attr for (=) not in attr or value */
        totalAttrLen += 3 + (attrCount * 3);
    } else {
        totalAttrLen += 3;
    }

    if (ldap_quote_filter_value(value->bv_val, value->bv_len, 0, 0, &valueLen)) {
        slapi_ch_free((void **)&attrLen);
        return filter;
    }

    if (requiredObjectClass) {
        classLen = strlen(requiredObjectClass);
        /* "(&(objectClass=)<Filter here>)" == 17 */
        filterLen = totalAttrLen + 1 + (valueLen * attrCount) + classLen + 17 + 1;
    } else {
        filterLen = totalAttrLen + 1 + (valueLen * attrCount) + 1;
    }

    /* Allocate the buffer */
    filter = (char *)slapi_ch_calloc(1, filterLen + 1);
    fp = filter;
    max = &filter[filterLen];

    /* Place AND expression and objectClass in filter */
    if (requiredObjectClass) {
        strcpy(fp, "(&(objectClass=");
        fp += 15;
        strcpy(fp, requiredObjectClass);
        fp += classLen;
        *fp++ = ')';
    }

    if (attrCount == 1) {
        *fp++ = '(';
        /* Place attribute name in filter */
        strcpy(fp, attributes[0]);
        fp += attrLen[0];

        /* Place comparison operator */
        *fp++ = '=';

        /* Place value in filter */
        if (ldap_quote_filter_value(value->bv_val, value->bv_len, fp, max - fp, &valueLen)) {
            slapi_ch_free_string(&filter);
            slapi_ch_free((void **)&attrLen);
            return 0;
        }
        fp += valueLen;
        *fp++ = ')';
    } else {
        strcpy(fp, "(|");
        fp += 2;

        for (i = 0; attributes && attributes[i]; i++) {
            strcpy(fp, "(");
            fp += 1;
            /* Place attribute name in filter */
            strcpy(fp, attributes[i]);
            fp += attrLen[i];

            /* Place comparison operator */
            *fp++ = '=';

            /* Place value in filter */
            if (ldap_quote_filter_value(value->bv_val, value->bv_len, fp, max - fp, &valueLen)) {
                slapi_ch_free_string(&filter);
                slapi_ch_free((void **)&attrLen);
                return 0;
            }
            fp += valueLen;

            strcpy(fp, ")");
            fp += 1;
        }
        strcpy(fp, ")");
        fp += 1;
    }

    /* Close AND expression if a requiredObjectClass was set */
    if (requiredObjectClass) {
        *fp++ = ')';
    }

    /* Terminate */
    *fp = 0;

    slapi_ch_free((void **)&attrLen);

    return filter;
}

/* ------------------------------------------------------------ */
/*
 * search - search a subtree for entries with a named attribute matching
 *   the list of values.  An entry matching the 'target' DN is
 *   not considered in the search.
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     target dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
search(Slapi_DN *baseDN, const char **attrNames, Slapi_Attr *attr, struct berval **values, const char *requiredObjectClass, Slapi_DN *target, Slapi_DN **excludes)
{
    int result;

#ifdef DEBUG
    /* Fix this later to print all the attr names */
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "search - SEARCH baseDN=%s attr=%s target=%s\n", slapi_sdn_get_dn(baseDN), attrNames[0],
                  target ? slapi_sdn_get_dn(target) : "None");
#endif

    result = LDAP_SUCCESS;

    /* If no values, can't possibly be a conflict */
    if ((Slapi_Attr *)NULL == attr && (struct berval **)NULL == values)
        return result;

    /*
   * Perform the search for each value provided
   *
   * Another possibility would be to search for all the values at once.
   * However, this is more complex (for filter creation) and unique
   * attributes values are probably only changed one at a time anyway.
   */
    if ((Slapi_Attr *)NULL != attr) {
        Slapi_Value *v = NULL;
        int vhint = -1;

        for (vhint = slapi_attr_first_value(attr, &v);
             vhint != -1 && LDAP_SUCCESS == result;
             vhint = slapi_attr_next_value(attr, vhint, &v)) {
            result = search_one_berval(baseDN, attrNames,
                                       slapi_value_get_berval(v),
                                       requiredObjectClass, target, excludes);
        }
    } else {
        for (; *values != NULL && LDAP_SUCCESS == result; values++) {
            result = search_one_berval(baseDN, attrNames, *values, requiredObjectClass,
                                       target, excludes);
        }
    }

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "search - SEARCH result = %d\n", result);
#endif

    return (result);
}


static int
search_one_berval(Slapi_DN *baseDN, const char **attrNames, const struct berval *value, const char *requiredObjectClass, Slapi_DN *target, Slapi_DN **excludes)
{
    int result;
    char *filter;
    Slapi_PBlock *spb;

    result = LDAP_SUCCESS;

    /* If no value, can't possibly be a conflict */
    if ((struct berval *)NULL == value)
        return result;

    filter = 0;
    spb = 0;

    BEGIN
    int err;
    int sres;
    Slapi_Entry **entries;
    static char *attrs[] = {"1.1", 0};

    /* Create the filter - this needs to be freed */
    filter = create_filter(attrNames, value, requiredObjectClass);

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "search_one_berval - SEARCH filter=%s\n", filter);
#endif

    /* Perform the search using the new internal API */
    spb = slapi_pblock_new();
    if (!spb) {
        result = uid_op_error(2);
        break;
    }

    slapi_search_internal_set_pb_ext(spb, baseDN, LDAP_SCOPE_SUBTREE,
                                     filter, attrs, 0 /* attrs only */, NULL, NULL, plugin_identity, 0 /* actions */);
    slapi_search_internal_pb(spb);

    err = slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_RESULT, &sres);
    if (err) {
        result = uid_op_error(3);
        break;
    }

    /* Allow search to report that there is nothing in the subtree */
    if (sres == LDAP_NO_SUCH_OBJECT)
        break;

    /* Other errors are bad */
    if (sres) {
        result = uid_op_error(3);
        break;
    }

    err = slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                           &entries);
    if (err) {
        result = uid_op_error(4);
        break;
    }

    /*
       * Look at entries returned.  Any entry found must be the
       * target entry or the constraint fails.
       */
    for (; *entries; entries++) {
#ifdef DEBUG
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                      "search_one_berval - SEARCH entry dn=%s\n", slapi_entry_get_dn(*entries));
#endif

        /*
         * It is a Constraint Violation if any entry is found, unless
         * the entry is the target entry (if any).
         */
        if (!target || slapi_sdn_compare(slapi_entry_get_sdn(*entries), target) != 0) {
            int i;
            result = LDAP_CONSTRAINT_VIOLATION;
            if (excludes == NULL || *excludes == NULL) {
                break;
            }

            /* Do the same check for excluded subtrees as resulted entries may have matched them */
            for (i = 0; excludes && excludes[i]; i++) {
                Slapi_DN *entry_dn = slapi_entry_get_sdn(*entries);
                if (slapi_sdn_issuffix(entry_dn, excludes[i])) {
                    result = LDAP_SUCCESS;
                    break;
                }
            }

            if (result == LDAP_CONSTRAINT_VIOLATION) {
                break;
            }
        }
    }

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "search_one_berval - SEARCH complete result=%d\n", result);
#endif
    END

        /* Clean-up */
        if (spb)
    {
        slapi_free_search_results_internal(spb);
        slapi_pblock_destroy(spb);
    }

    slapi_ch_free((void **)&filter);

    return result;
}

/* ------------------------------------------------------------ */
/*
 * searchAllSubtrees - search all subtrees in argv for entries
 *   with a named attribute matching the list of values, by
 *   calling search for each one.
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     destination (target) dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
searchAllSubtrees(Slapi_DN **subtrees, Slapi_DN **exclude_subtrees, const char **attrNames, Slapi_Attr *attr, struct berval **values, const char *requiredObjectClass, Slapi_DN *sourceSDN, Slapi_DN *destinationSDN, PRBool unique_in_all_subtrees)
{
    int result = LDAP_SUCCESS;
    int i;

    if (unique_in_all_subtrees) {
        PRBool in_a_subtree = PR_FALSE;

        /* we need to check that the added values of this attribute
           * are unique in all the monitored subtrees
           */

        /* First check the source entry is in one of
           * the monitored subtree, so adding 'values' would
           * violate constraint
           */
        for (i = 0; subtrees && subtrees[i]; i++) {
            if (slapi_sdn_issuffix(sourceSDN, subtrees[i])) {
                in_a_subtree = PR_TRUE;
                break;
            }
        }
        if (!in_a_subtree) {
            return result;
        }
    }

    /* If DN is in the excluded subtrees, we should ignore it in any case, not only
   * in the case of uniqueness in all subtrees. */
    if (exclude_subtrees != NULL) {
        PRBool in_a_subtree = PR_FALSE;
        for (i = 0; exclude_subtrees && exclude_subtrees[i]; i++) {
            if (slapi_sdn_issuffix(sourceSDN, exclude_subtrees[i])) {
                in_a_subtree = PR_TRUE;
                break;
            }
        }
        if (in_a_subtree) {
            return result;
        }
    }

    /*
   * For each DN in the managed list, do uniqueness checking if
   * the destination (target) DN is a subnode in the tree.
   */
    for (i = 0; subtrees && subtrees[i]; i++) {
        Slapi_DN *sufdn = subtrees[i];
        /*
     * The DN should already be normalized, so we don't have to
     * worry about that here.
     */
        if (unique_in_all_subtrees || slapi_sdn_issuffix(sourceSDN, sufdn)) {
            result = search(sufdn, attrNames, attr, values, requiredObjectClass, destinationSDN, exclude_subtrees);
            if (result)
                break;
        }
    }
    return result;
}

/* ------------------------------------------------------------ */
/*
 * getArguments - parse invocation parameters
 * Return:
 *   0 - success
 *   >0 - error parsing parameters
 */
static int
getArguments(Slapi_PBlock *pb, char **attrName, char **markerObjectClass, char **requiredObjectClass)
{
    int argc;
    char **argv;

    /*
   * Get the arguments
   */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc)) {
        return uid_op_error(10);
    }

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv)) {
        return uid_op_error(11);
    }

    /*
   * Required arguments: attribute and markerObjectClass
   * Optional argument: requiredObjectClass
   */
    for (; argc > 0; argc--, argv++) {
        char *param = *argv;
        char *delimiter = strchr(param, '=');
        if (NULL == delimiter) {
            /* Old style untagged parameter */
            *attrName = *argv;
            return UNTAGGED_PARAMETER;
        }
        if (strncasecmp(param, "attribute", delimiter - param) == 0) {
            /* It's OK to set a pointer here, because ultimately it points
           * inside the argv array of the pblock, which will be staying
           * arround.
           */
            *attrName = delimiter + 1;
        } else if (strncasecmp(param, "markerobjectclass", delimiter - param) == 0) {
            *markerObjectClass = delimiter + 1;
        } else if (strncasecmp(param, "requiredobjectclass", delimiter - param) == 0) {
            *requiredObjectClass = delimiter + 1;
        }
    }
    if (!*attrName || !*markerObjectClass) {
        return uid_op_error(13);
    }

    return 0;
}

/* ------------------------------------------------------------ */
/*
 * findSubtreeAndSearch - walk up the tree to find an entry with
 * the marker object class; if found, call search from there and
 * return the result it returns
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     destination (target) dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
findSubtreeAndSearch(Slapi_DN *sourceSDN, const char **attrNames, Slapi_Attr *attr, struct berval **values, const char *requiredObjectClass, Slapi_DN *destinationSDN, const char *markerObjectClass, Slapi_DN **excludes)
{
    int result = LDAP_SUCCESS;
    Slapi_PBlock *spb = NULL;
    Slapi_DN *curpar = slapi_sdn_new();
    Slapi_DN *newpar = NULL;

    slapi_sdn_get_parent(sourceSDN, curpar);
    while (slapi_sdn_get_dn(curpar) != NULL) {
        if ((spb = dnHasObjectClass(curpar, markerObjectClass))) {
            freePblock(spb);
            /*
           * Do the search.   There is no entry that is allowed
           * to have the attribute already.
           */
            result = search(curpar, attrNames, attr, values, requiredObjectClass,
                            destinationSDN, excludes);
            break;
        }
        newpar = slapi_sdn_new();
        slapi_sdn_copy(curpar, newpar);
        slapi_sdn_get_parent(newpar, curpar);
        slapi_sdn_free(&newpar);
    }
    slapi_sdn_free(&curpar);
    return result;
}


/* ------------------------------------------------------------ */
/*
 * preop_add - pre-operation plug-in for add
 */
static int
preop_add(Slapi_PBlock *pb)
{
    int result;
    char *errtext = NULL;
    const char **attrNames = NULL;
    char *attr_friendly = NULL;

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "ADD begin\n");
#endif

    result = LDAP_SUCCESS;

    /*
   * Do constraint check on the added entry.  Set result.
   */

    BEGIN
    int err;
    char *markerObjectClass = NULL;
    char *requiredObjectClass = NULL;
    Slapi_DN *targetSDN = NULL;
    int isupdatedn;
    Slapi_Entry *e;
    Slapi_Attr *attr;
    struct attr_uniqueness_config *config = NULL;
    int i = 0;

    /*
         * If this is a replication update, just be a noop.
         */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) {
        result = uid_op_error(50);
        break;
    }
    if (isupdatedn) {
        break;
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (config == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "preop_add - Failed to retrieve the config\n");
        result = LDAP_OPERATIONS_ERROR;
        break;
    }

    /*
         * Get the arguments
         */
    attrNames = config->attrs;
    markerObjectClass = config->top_entry_oc;
    requiredObjectClass = config->subtree_entries_oc;
    attr_friendly = config->attr_friendly;

    /*
     * Get the target SDN for this add operation
     */
    err = slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &targetSDN);
    if (err) {
        result = uid_op_error(51);
        break;
    }

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "preop_add - ADD target=%s\n", slapi_sdn_get_dn(targetSDN));
#endif

    /*
         * Get the entry data for this add. Check whether it
         * contains a value for the unique attribute
         */
    err = slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    if (err) {
        result = uid_op_error(52);
        break;
    }

    /*
         * Check if it contains the required object class
         */
    if (NULL != requiredObjectClass) {
        if (!entryHasObjectClass(pb, e, requiredObjectClass)) {
            /* No, so we don't have to do anything */
            break;
        }
    }

    for (i = 0; attrNames && attrNames[i]; i++) {
        err = slapi_entry_attr_find(e, attrNames[i], &attr);
        if (!err) {
            /*
                 * Passed all the requirements - this is an operation we
                 * need to enforce uniqueness on. Now find all parent entries
                 * with the marker object class, and do a search for each one.
                 */
            if (NULL != markerObjectClass) {
                /* Subtree defined by location of marker object class */
                result = findSubtreeAndSearch(targetSDN, attrNames, attr, NULL,
                                              requiredObjectClass, targetSDN,
                                              markerObjectClass, config->exclude_subtrees);
            } else {
                /* Subtrees listed on invocation line */
                result = searchAllSubtrees(config->subtrees, config->exclude_subtrees, attrNames, attr, NULL,
                                           requiredObjectClass, targetSDN, targetSDN, config->unique_in_all_subtrees);
            }
            if (result != LDAP_SUCCESS) {
                break;
            }
        }
    }
    END

        if (result)
    {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                      "preop_add - ADD result %d\n", result);

        if (result == LDAP_CONSTRAINT_VIOLATION) {
            errtext = slapi_ch_smprintf(moreInfo, attr_friendly);
        } else {
            errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
        }

        /* Send failure to the client */
        slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

        slapi_ch_free_string(&errtext);
    }

    return (result == LDAP_SUCCESS) ? 0 : -1;
}

static void
addMod(LDAPMod ***modary, int *capacity, int *nmods, LDAPMod *toadd)
{
    if (*nmods == *capacity) {
        *capacity += 4;
        if (*modary) {
            *modary = (LDAPMod **)slapi_ch_realloc((char *)*modary, *capacity * sizeof(LDAPMod *));
        } else {
            *modary = (LDAPMod **)slapi_ch_malloc(*capacity * sizeof(LDAPMod *));
        }
    }
    (*modary)[*nmods] = toadd;
    (*nmods)++;
}

/* ------------------------------------------------------------ */
/*
 * preop_modify - pre-operation plug-in for modify
 */
static int
preop_modify(Slapi_PBlock *pb)
{

    int result = LDAP_SUCCESS;
    Slapi_PBlock *spb = NULL;
    LDAPMod **checkmods = NULL;
    int checkmodsCapacity = 0;
    char *errtext = NULL;
    const char **attrNames = NULL;
    struct attr_uniqueness_config *config = NULL;
    char *attr_friendly = NULL;

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "preop_modify - MODIFY begin\n");
#endif

    BEGIN
    int err;
    char *markerObjectClass = NULL;
    char *requiredObjectClass = NULL;
    LDAPMod **mods;
    int modcount = 0;
    int ii;
    LDAPMod *mod;
    Slapi_DN *targetSDN = NULL;
    int isupdatedn;
    int i = 0;

    /*
     * If this is a replication update, just be a noop.
     */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) {
        result = uid_op_error(60);
        break;
    }
    if (isupdatedn) {
        break;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (config == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "preop_modify - Failed to retrieve the config\n");
        result = LDAP_OPERATIONS_ERROR;
        break;
    }
    /*
     * Get the arguments
     */
    attrNames = config->attrs;
    markerObjectClass = config->top_entry_oc;
    requiredObjectClass = config->subtree_entries_oc;
    attr_friendly = config->attr_friendly;


    err = slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    if (err) {
        result = uid_op_error(61);
        break;
    }

    /* There may be more than one mod that matches e.g.
       changetype: modify
       delete: uid
       uid: balster1950
       -
       add: uid
       uid: scottg

       So, we need to first find all mods that contain the attribute
       which are add or replace ops and are bvalue encoded
    */
    /* find out how many mods meet this criteria */
    for (; mods && *mods; mods++) {
        mod = *mods;
        for (i = 0; attrNames && attrNames[i]; i++) {
            if ((slapi_attr_type_cmp(mod->mod_type, attrNames[i], 1) == 0) && /* mod contains target attr */
                (mod->mod_op & LDAP_MOD_BVALUES) &&                           /* mod is bval encoded (not string val) */
                (mod->mod_bvalues && mod->mod_bvalues[0]) &&                  /* mod actually contains some values */
                (SLAPI_IS_MOD_ADD(mod->mod_op) ||                             /* mod is add */
                 SLAPI_IS_MOD_REPLACE(mod->mod_op)))                          /* mod is replace */
            {
                addMod(&checkmods, &checkmodsCapacity, &modcount, mod);
            }
        }
    }
    if (modcount == 0) {
        break; /* no mods to check, we are done */
    }

    /* Get the target SDN */
    err = slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &targetSDN);
    if (err) {
        result = uid_op_error(11);
        break;
    }

    /*
     * Check if it has the required object class
     */
    if (requiredObjectClass &&
        !(spb = dnHasObjectClass(targetSDN, requiredObjectClass))) {
        break;
    }

    /*
     * Passed all the requirements - this is an operation we
     * need to enforce uniqueness on. Now find all parent entries
     * with the marker object class, and do a search for each one.
     */
    /*
     * stop checking at first mod that fails the check
     */
    for (ii = 0; (result == 0) && (ii < modcount); ++ii) {
        mod = checkmods[ii];
        if (NULL != markerObjectClass) {
            /* Subtree defined by location of marker object class */
            result = findSubtreeAndSearch(targetSDN, attrNames, NULL,
                                          mod->mod_bvalues, requiredObjectClass,
                                          targetSDN, markerObjectClass, config->exclude_subtrees);
        } else {
            /* Subtrees listed on invocation line */
            result = searchAllSubtrees(config->subtrees, config->exclude_subtrees, attrNames, NULL,
                                       mod->mod_bvalues, requiredObjectClass, targetSDN, targetSDN, config->unique_in_all_subtrees);
        }
    }
    END

    slapi_ch_free((void **)&checkmods);
    freePblock(spb);
    if (result) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                      "preop_modify - MODIFY result %d\n", result);

        if (result == LDAP_CONSTRAINT_VIOLATION) {
            errtext = slapi_ch_smprintf(moreInfo, attr_friendly);
        } else {
            errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
        }

        slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

        slapi_ch_free_string(&errtext);
    }

    return (result == LDAP_SUCCESS) ? 0 : -1;
}

/* ------------------------------------------------------------ */
/*
 * preop_modrdn - Pre-operation call for modify RDN
 *
 * Check that the new RDN does not include attributes that
 * cause a constraint violation
 */
static int
preop_modrdn(Slapi_PBlock *pb)
{
    Slapi_PBlock *entry_pb = NULL;
    int result = LDAP_SUCCESS;
    Slapi_Entry *e = NULL;
    Slapi_Value *sv_requiredObjectClass = NULL;
    char *errtext = NULL;
    const char **attrNames = NULL;
    struct attr_uniqueness_config *config = NULL;

#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "preop_modrdn - MODRDN begin\n");
#endif

    BEGIN
    int err;
    char *markerObjectClass = NULL;
    char *requiredObjectClass = NULL;
    Slapi_DN *sourceSDN = NULL;
    Slapi_DN *superiorSDN;
    char *rdn;
    int deloldrdn = 0;
    int isupdatedn;
    Slapi_Attr *attr;
    int i = 0;

    /*
         * If this is a replication update, just be a noop.
         */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) {
        result = uid_op_error(30);
        break;
    }
    if (isupdatedn) {
        break;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (config == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, plugin_name, "preop_modrdn - Failed to retrieve the config\n");
        result = LDAP_OPERATIONS_ERROR;
        break;
    }
    /*
         * Get the arguments
         */
    attrNames = config->attrs;
    markerObjectClass = config->top_entry_oc;
    requiredObjectClass = config->subtree_entries_oc;

    /* Create a Slapi_Value for the requiredObjectClass to use
     * for checking the entry. */
    if (requiredObjectClass) {
        sv_requiredObjectClass = slapi_value_new_string(requiredObjectClass);
    }

    /* Get the DN of the entry being renamed */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &sourceSDN);
    if (err) {
        result = uid_op_error(31);
        break;
    }

    /* Get superior value - unimplemented in 3.0/4.0/5.0 DS */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &superiorSDN);
    if (err) {
        result = uid_op_error(32);
        break;
    }

    /*
     * No superior means the entry is just renamed at
     * its current level in the tree.  Use the source SDN for
     * determining which managed tree this belongs to
     */
    if (!superiorSDN)
        superiorSDN = sourceSDN;

    /* Get the new RDN - this has the attribute values */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &rdn);
    if (err) {
        result = uid_op_error(33);
        break;
    }
#ifdef DEBUG
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "preop_modrdn - MODRDN newrdn=%s\n", rdn);
#endif

    /* See if the old RDN value is being deleted. */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &deloldrdn);
    if (err) {
        result = uid_op_error(34);
        break;
    }

    /* Get the entry that is being renamed so we can make a dummy copy
     * of what it will look like after the rename. */
    err = slapi_search_get_entry(&entry_pb, sourceSDN, NULL, &e, plugin_identity);
    if (err != LDAP_SUCCESS) {
        result = uid_op_error(35);
        /* We want to return a no such object error if the source SDN doesn't exist. */
        if (err == LDAP_NO_SUCH_OBJECT) {
            result = err;
        }
        break;
    }

    /* Apply the rename operation to the dummy entry. */
    /* slapi_entry_rename does not expect rdn normalized */
    err = slapi_entry_rename(e, rdn, deloldrdn, superiorSDN);
    if (err != LDAP_SUCCESS) {
        result = uid_op_error(36);
        break;
    }


    /*
     * Check if it has the required object class
     */
    if (requiredObjectClass &&
        !slapi_entry_attr_has_syntax_value(e, SLAPI_ATTR_OBJECTCLASS, sv_requiredObjectClass)) {
        break;
    }

    /*
     * Find any unique attribute data in the new RDN
     */
    for (i = 0; attrNames && attrNames[i]; i++) {
        err = slapi_entry_attr_find(e, attrNames[i], &attr);
        if (!err) {
            /*
             * Passed all the requirements - this is an operation we
             * need to enforce uniqueness on. Now find all parent entries
             * with the marker object class, and do a search for each one.
             */
            if (NULL != markerObjectClass) {
                /* Subtree defined by location of marker object class */
                result = findSubtreeAndSearch(superiorSDN, attrNames, attr, NULL,
                                              requiredObjectClass, sourceSDN,
                                              markerObjectClass, config->exclude_subtrees);
            } else {
                /* Subtrees listed on invocation line */
                result = searchAllSubtrees(config->subtrees, config->exclude_subtrees, attrNames, attr, NULL,
                                           requiredObjectClass, superiorSDN, sourceSDN, config->unique_in_all_subtrees);
            }
            if (result != LDAP_SUCCESS) {
                break;
            }
        }
    }
    END
        /* Clean-up */
        slapi_value_free(&sv_requiredObjectClass);

    slapi_search_get_entry_done(&entry_pb);

    if (result) {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                      "preop_modrdn - MODRDN result %d\n", result);

        if (result == LDAP_CONSTRAINT_VIOLATION) {
            errtext = slapi_ch_smprintf(moreInfo, config->attr_friendly);
        } else {
            errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
        }

        slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

        slapi_ch_free_string(&errtext);
    }

    return (result == LDAP_SUCCESS) ? 0 : -1;
}

static int
uiduniq_start(Slapi_PBlock *pb)
{
    Slapi_Entry *plugin_entry = NULL;
    struct attr_uniqueness_config *config = NULL;

    if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &plugin_entry) == 0) {
        /* load the config into the config list */
        if ((config = uniqueness_entry_to_config(pb, plugin_entry)) == NULL) {
            return SLAPI_PLUGIN_FAILURE;
        }
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, (void *)config);
    }

    return 0;
}

static int
uiduniq_close(Slapi_PBlock *pb)
{
    struct attr_uniqueness_config *config = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &config);
    if (config) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, NULL);
        free_uniqueness_config(config);
        slapi_ch_free((void **)&config);
    }
    return 0;
}

/* ------------------------------------------------------------ */
/*
 * Initialize the plugin
 *
 * uidunique_init (the old name) is deprecated
 */
int
NSUniqueAttr_Init(Slapi_PBlock *pb)
{
    int err = 0;
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    int preadd = SLAPI_PLUGIN_PRE_ADD_FN;
    int premod = SLAPI_PLUGIN_PRE_MODIFY_FN;
    int premdn = SLAPI_PLUGIN_PRE_MODRDN_FN;

    BEGIN

    /* Declare plugin version */
    err = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                           SLAPI_PLUGIN_VERSION_01);
    if (err)
        break;

    /*
     * Get plugin identity and store it for later use
     * Used for internal operations
     */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    /* PR_ASSERT (plugin_identity); */

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        preadd = SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN;
        premod = SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN;
        premdn = SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN;
    }

    /* Provide descriptive information */
    err = slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pluginDesc);
    if (err)
        break;

    /* Register functions */
    err = slapi_pblock_set(pb, preadd, (void *)preop_add);
    if (err)
        break;

    err = slapi_pblock_set(pb, premod, (void *)preop_modify);
    if (err)
        break;

    err = slapi_pblock_set(pb, premdn, (void *)preop_modrdn);
    if (err)
        break;

    err = slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *)uiduniq_start);
    if (err)
        break;

    err = slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *)uiduniq_close);
    if (err)
        break;


    END

        if (err)
    {
        slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "NSUniqueAttr_Init - Error: %d\n", err);
        err = -1;
    }
    else slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name, "NSUniqueAttr_Init - plugin loaded\n");

    return err;
}


/* ------------------------------------------------------------ */
/*
 * ldap_quote_filter_value
 *
 * Quote the filter value according to RFC 2254 (Dec 1997)
 *
 * value - a UTF8 string containing the value.  It may contain
 *   any of the chars needing quotes ( '(' ')' '*' '/' and NUL ).
 * len - the length of the UTF8 value
 * out - a buffer to recieve the converted value.  May be NULL, in
 *   which case, only the length of the output is computed (and placed in
 *   outLen).
 * maxLen - the size of the output buffer.  It is an error if this length
 *   is exceeded.  Ignored if out is NULL.
 * outLen - recieves the size of the output.  If an error occurs, this
 *   result is not available.
 *
 * Returns
 *   0 - success
 *  -1 - failure (usually a buffer overflow)
 */
int /* Error value */
    ldap_quote_filter_value(
        char *value,
        int len,
        char *out,
        int maxLen,
        int *outLen)
{
    int err;
    char *eValue;
    int resLen;
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
    static char hexchars[16] = "0123456789abcdef";
#endif

    err = 0;
    eValue = &value[len];
    resLen = 0;

    /*
   * Convert each character in the input string
   */
    while (value < eValue) {
        switch (*value) {
        case '(':
        case ')':
        case '*':
        case '\\':
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
        case 0:
#endif
/* Handle characters needing special escape sequences */

/* Compute size of output */
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
            resLen += 3;
#else
            resLen += 2;
#endif

            /* Generate output if requested */
            if (out) {
                /* Check for overflow */
                if (resLen > maxLen) {
                    err = -1;
                    break;
                }

                *out++ = '\\';
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
                *out++ = hexchars[(*value >> 4) & 0xF];
                *out++ = hexchars[*value & 0xF];
#else
                *out++ = *value;
#endif
            }

            break;

        default:
            /* Compute size of output */
            resLen += 1;

            /* Generate output if requested */
            if (out) {
                if (resLen > maxLen) {
                    err = -1;
                    break;
                }
                *out++ = *value;
            }

            break;
        }

        if (err)
            break;

        value++;
    }

    if (!err)
        *outLen = resLen;

    return err;
}
