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


#include <stdio.h>
#include <string.h>
#include <ctype.h>
/* This was malloc.h - but it's moved to stdlib.h on most platforms, and FBSD is strict */
/* Make it stdlib.h, and revert to malloc.h with ifdefs if we have issues here. WB 2016 */
#include <stdlib.h>

/* removed for ns security integration
#include <sec.h>
*/
#include <plstr.h>
#include <prlink.h>
#include <prprf.h>

#include <keyhi.h>
#include <cert.h>
#define DEFINE_LDAPU_STRINGS 1
#include <ldaputil/certmap.h>
#include <ldaputil/ldapauth.h>
#include <ldaputil/errors.h>
#include <ldaputil/ldaputil.h>
#include "ldaputili.h"

#ifndef BIG_LINE
#define BIG_LINE 1024
#endif

/* This is hack, the function is defined in cert/alg1485.c */
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

static char this_dllname[256];
static const char *LIB_DIRECTIVE = "certmap";
static const int LIB_DIRECTIVE_LEN = 7; /* strlen("LIB_DIRECTIVE") */

static void *ldapu_propval_free(void *propval_in, void *arg);

typedef struct
{
    FILE *fp;
    void *arg;
} LDAPUPrintInfo_t;

static LDAPUCertMapListInfo_t *certmap_listinfo = 0;
static LDAPUCertMapInfo_t *default_certmap_info = 0;

static const char *certmap_attrs[] = {
    0,
    0,
    0,
    0};

const long CERTMAP_BIT_POS_UNKNOWN = 0;    /* unknown OID */
const long CERTMAP_BIT_POS_CN = 1L << 1;   /* Common Name */
const long CERTMAP_BIT_POS_OU = 1L << 2;   /* Organization unit */
const long CERTMAP_BIT_POS_O = 1L << 3;    /* Organization */
const long CERTMAP_BIT_POS_C = 1L << 4;    /* Country */
const long CERTMAP_BIT_POS_L = 1L << 5;    /* Locality */
const long CERTMAP_BIT_POS_ST = 1L << 6;   /* State or Province */
const long CERTMAP_BIT_POS_MAIL = 1L << 7; /* E-mail Address */
const long CERTMAP_BIT_POS_UID = 1L << 8;  /* UID */
const long CERTMAP_BIT_POS_DC = 1L << 9;   /* DC */

const int SEC_OID_AVA_UNKNOWN = 0; /* unknown OID */

/*
 * Size of escaped character to represent a dn in a filter
 * 1 ==> c
 * 3 ==> \xx
 */
const static char value2filter_sizes[128] = {
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,

    1, 1, 1, 1, 1, 1, 1, 1,
    3, 3, 3, 1, 1, 1, 1, 1,  /* ( ) * */
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,

    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 3, 1, 1, 1,  /* \ */

    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 3
};

const static char b2hex[] = "0123456789ABCDEF";


static int
value2filter(char *dst, size_t dstlen, const char *src, size_t srclen)
{
    /*
     * Escape the dn to store it as a value in a filter
     * To be sure that the function is successfull caller should
     * ensure that dstlen >= 3*srclen+1
     */

    if (dstlen < 1) {
        return LDAPU_ERR_OUT_OF_MEMORY;
    }
    while (dstlen >= 2 && srclen > 0) {
        char c = *src++;
        int ecsize = (c & 0x80) ? 3 : value2filter_sizes[c & 0x7f];
        srclen--;
        dstlen -= ecsize;
        if (dstlen < 1) {
            /* Not enough space to write the final \0 */
            return LDAPU_ERR_OUT_OF_MEMORY;
        }
        switch ( ecsize ) {
            default:
                return LDAPU_ERR_INTERNAL;
            case 1:
                *dst++ = c;
                break;
            case 3:
                *dst ++ = '\\';
                *dst ++ = b2hex[(c >> 4) & 0xf];
                *dst ++ = b2hex[c & 0xf];
                break;
        }
    }
    if (srclen > 0) {
        return LDAPU_ERR_OUT_OF_MEMORY;
    }
    *dst = 0;
    return LDAPU_SUCCESS;
}


static long
certmap_secoid_to_bit_pos(int oid)
{
    switch (oid) {
    case SEC_OID_AVA_COUNTRY_NAME:
        return CERTMAP_BIT_POS_C;
    case SEC_OID_AVA_ORGANIZATION_NAME:
        return CERTMAP_BIT_POS_O;
    case SEC_OID_AVA_COMMON_NAME:
        return CERTMAP_BIT_POS_CN;
    case SEC_OID_AVA_LOCALITY:
        return CERTMAP_BIT_POS_L;
    case SEC_OID_AVA_STATE_OR_PROVINCE:
        return CERTMAP_BIT_POS_ST;
    case SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME:
        return CERTMAP_BIT_POS_OU;
    case SEC_OID_RFC1274_UID:
        return CERTMAP_BIT_POS_UID;
    /* Map "E" and "MAIL" to the same bit position */
    case SEC_OID_PKCS9_EMAIL_ADDRESS:
        return CERTMAP_BIT_POS_MAIL;
    case SEC_OID_RFC1274_MAIL:
        return CERTMAP_BIT_POS_MAIL;
    case SEC_OID_AVA_DC:
        return CERTMAP_BIT_POS_DC;
    default:
        return CERTMAP_BIT_POS_UNKNOWN;
    }
}

static const char *
certmap_secoid_to_name(int oid)
{
    switch (oid) {
    case SEC_OID_AVA_COUNTRY_NAME:
        return "C";
    case SEC_OID_AVA_ORGANIZATION_NAME:
        return "O";
    case SEC_OID_AVA_COMMON_NAME:
        return "CN";
    case SEC_OID_AVA_LOCALITY:
        return "L";
    case SEC_OID_AVA_STATE_OR_PROVINCE:
        return "ST";
    case SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME:
        return "OU";
    case SEC_OID_RFC1274_UID:
        return "UID";
    /* Map both 'e' and 'mail' to 'mail' in LDAP */
    case SEC_OID_PKCS9_EMAIL_ADDRESS:
        return "MAIL";
    case SEC_OID_RFC1274_MAIL:
        return "MAIL";
    case SEC_OID_AVA_DC:
        return "DC";
    default:
        return 0;
    }
}

static void
tolower_string(char *str)
{
    if (str) {
        while (*str) {
            *str = tolower(*str);
            str++;
        }
    }
}

static long
certmap_name_to_bit_pos(const char *str)
{
    if (!ldapu_strcasecmp(str, "c"))
        return CERTMAP_BIT_POS_C;
    if (!ldapu_strcasecmp(str, "o"))
        return CERTMAP_BIT_POS_O;
    if (!ldapu_strcasecmp(str, "cn"))
        return CERTMAP_BIT_POS_CN;
    if (!ldapu_strcasecmp(str, "l"))
        return CERTMAP_BIT_POS_L;
    if (!ldapu_strcasecmp(str, "st"))
        return CERTMAP_BIT_POS_ST;
    if (!ldapu_strcasecmp(str, "ou"))
        return CERTMAP_BIT_POS_OU;
    if (!ldapu_strcasecmp(str, "uid"))
        return CERTMAP_BIT_POS_UID;
    /* Map "E" and "MAIL" to the same bit position */
    if (!ldapu_strcasecmp(str, "e"))
        return CERTMAP_BIT_POS_MAIL;
    if (!ldapu_strcasecmp(str, "mail"))
        return CERTMAP_BIT_POS_MAIL;
    if (!ldapu_strcasecmp(str, "dc"))
        return CERTMAP_BIT_POS_DC;

    return CERTMAP_BIT_POS_UNKNOWN;
}

#if 0 /* may need this in the future */
static int certmap_name_to_secoid (const char *str)
{
    if (!ldapu_strcasecmp(str, "c")) return SEC_OID_AVA_COUNTRY_NAME;
    if (!ldapu_strcasecmp(str, "o")) return SEC_OID_AVA_ORGANIZATION_NAME;
    if (!ldapu_strcasecmp(str, "cn")) return SEC_OID_AVA_COMMON_NAME;
    if (!ldapu_strcasecmp(str, "l")) return SEC_OID_AVA_LOCALITY;
    if (!ldapu_strcasecmp(str, "st")) return SEC_OID_AVA_STATE_OR_PROVINCE;
    if (!ldapu_strcasecmp(str, "ou")) return SEC_OID_AVA_ORGANIZATIONAL_UNIT_NAME;
    if (!ldapu_strcasecmp(str, "uid")) return SEC_OID_RFC1274_UID;
    if (!ldapu_strcasecmp(str, "e")) return SEC_OID_PKCS9_EMAIL_ADDRESS;
    if (!ldapu_strcasecmp(str, "mail")) return SEC_OID_RFC1274_MAIL;
    if (!ldapu_strcasecmp(str, "dc")) return SEC_OID_AVA_DC;

    return SEC_OID_AVA_UNKNOWN;    /* return invalid OID */
}
#endif

NSAPI_PUBLIC int
ldapu_list_alloc(LDAPUList_t **list)
{
    *list = (LDAPUList_t *)calloc(1, sizeof(LDAPUList_t));

    if (!*list)
        return LDAPU_ERR_OUT_OF_MEMORY;

    return LDAPU_SUCCESS;
}

static int
ldapu_list_add_node(LDAPUList_t *list, LDAPUListNode_t *node)
{
    if (list->head) {
        node->prev = list->tail;
        list->tail->next = node;
    } else {
        node->prev = 0;
        list->head = node;
    }

    node->next = 0;
    list->tail = node;
    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC int
ldapu_list_add_info(LDAPUList_t *list, void *info)
{
    LDAPUListNode_t *node;

    /* Allocate the list node and set info in the node. */
    node = (LDAPUListNode_t *)calloc(1, sizeof(LDAPUListNode_t));

    if (!node) {
        return LDAPU_ERR_OUT_OF_MEMORY;
    }

    node->info = info;

    return ldapu_list_add_node(list, node);
}


static void
ldapu_list_free(LDAPUList_t *list, LDAPUListNodeFn_t free_fn)
{
    if (list) {
        auto LDAPUListNode_t *node = list->head;
        while (node) {
            auto LDAPUListNode_t *next = node->next;
            if (free_fn) {
                (*free_fn)(node->info, 0);
                node->info = 0;
            }
            node->info = 0;
            free(node);
            node = next;
        }
        list->head = 0;
        list->tail = 0;
    }
    return;
}

NSAPI_PUBLIC int
ldapu_propval_alloc(const char *prop, const char *val, LDAPUPropVal_t **propval)
{
    *propval = (LDAPUPropVal_t *)malloc(sizeof(LDAPUPropVal_t));

    if (!*propval)
        return LDAPU_ERR_OUT_OF_MEMORY;

    (*propval)->prop = prop ? strdup(prop) : 0;
    (*propval)->val = val ? strdup(val) : 0;

    if ((!prop || (*propval)->prop) && (!val || (*propval)->val)) {
        /* strdup worked */
        return LDAPU_SUCCESS;
    } else {
        return LDAPU_ERR_OUT_OF_MEMORY;
    }
}


static int
PresentInComps(long comps_bitmask, int tag)
{
    long bit = certmap_secoid_to_bit_pos(tag);

    if (comps_bitmask & bit)
        return 1;
    else
        return 0;
}


static int
dbconf_to_certmap_err(int err)
{
    switch (err) {
    case LDAPU_ERR_DBNAME_IS_MISSING:
        return LDAPU_ERR_CANAME_IS_MISSING;
    case LDAPU_ERR_PROP_IS_MISSING:
        return LDAPU_ERR_CAPROP_IS_MISSING;
    default:
        return err;
    }
}

/* CAUTION: this function hijacks some substructures from db_info and make
 * the pointers to it NULL in the db_info.  It is safe to deallocate db_info.
 */
static int
dbinfo_to_certinfo(DBConfDBInfo_t *db_info,
                   LDAPUCertMapInfo_t **certinfo_out)
{
    LDAPUCertMapInfo_t *certinfo = NULL;
    LDAPUPropValList_t *propval_list = NULL;
    int rv = LDAPU_SUCCESS;

    *certinfo_out = 0;

    certinfo = (LDAPUCertMapInfo_t *)calloc(1, sizeof(LDAPUCertMapInfo_t));

    if (!certinfo) {
        rv = LDAPU_ERR_OUT_OF_MEMORY;
        goto error;
    }

    /* hijack few structures rather then copy.  Make the pointers to the
       structures NULL in the original structure so that they don't freed up
       when db_info is freed. */
    certinfo->issuerName = db_info->dbname;
    db_info->dbname = 0;

    /* Parse the Issuer DN. */
    certinfo->issuerDN = CERT_AsciiToName(db_info->url);
    if (NULL == certinfo->issuerDN                            /* invalid DN */
            && ldapu_strcasecmp(db_info->url, "default") != 0 /* not "default" */) {
        rv = LDAPU_ERR_MALFORMED_SUBJECT_DN;
        goto error;
    }

    /* hijack actual prop-vals from dbinfo -- to avoid strdup calls */
    if (db_info->firstprop) {
        LDAPUPropVal_t *propval;
        DBPropVal_t *dbpropval;

        dbpropval = db_info->firstprop;

        rv = ldapu_list_alloc(&propval_list);

        if (rv != LDAPU_SUCCESS) {
            goto error;
        }

        while (dbpropval) {
            propval = (LDAPUPropVal_t *)malloc(sizeof(LDAPUPropVal_t));

            if (!propval) {
                rv = LDAPU_ERR_OUT_OF_MEMORY;
                goto error;
            }

            propval->prop = dbpropval->prop;
            dbpropval->prop = 0;

            propval->val = dbpropval->val;
            dbpropval->val = 0;

            rv = ldapu_list_add_info(propval_list, propval);

            if (rv != LDAPU_SUCCESS) {
                ldapu_propval_free((void *)propval, (void *)propval);
                goto error;
            }

            dbpropval = dbpropval->next;
        }

        certinfo->propval = propval_list;
    }

    *certinfo_out = certinfo;
    goto done;

error:
    if (propval_list)
        ldapu_propval_list_free(propval_list);
    if (certinfo)
        free(certinfo);

done:
    return rv;
}

static int
ldapu_binary_cmp_certs(void *subject_cert,
                       void *entry_cert_binary,
                       unsigned long entry_cert_len)
{
    SECItem derCert = ((CERTCertificate *)subject_cert)->derCert;
    int rv;

    /* binary compare the two certs */
    if (derCert.len == entry_cert_len &&
        !memcmp(derCert.data, entry_cert_binary, entry_cert_len)) {
        rv = LDAPU_SUCCESS;
    } else {
        rv = LDAPU_ERR_CERT_VERIFY_FAILED;
    }

    return rv;
}


static int
ldapu_cert_verifyfn_default(void *subject_cert, LDAP *ld, void *certmap_info __attribute__((unused)), LDAPMessage *res, LDAPMessage **entry_out)
{
    LDAPMessage *entry;
    struct berval **bvals;
    int i;
    int rv = LDAPU_ERR_CERT_VERIFY_FAILED;
    char *cert_attr = ldapu_strings[LDAPU_STR_ATTR_CERT];
    char *cert_attr_nosubtype = ldapu_strings[LDAPU_STR_ATTR_CERT_NOSUBTYPE];

    *entry_out = 0;

    for (entry = ldapu_first_entry(ld, res); entry != NULL;
         entry = ldapu_next_entry(ld, entry)) {
        if (((bvals = ldapu_get_values_len(ld, entry, cert_attr)) == NULL) &&
            ((bvals = ldapu_get_values_len(ld, entry, cert_attr_nosubtype)) == NULL)) {
            rv = LDAPU_ERR_CERT_VERIFY_NO_CERTS;
            continue;
        }

        for (i = 0; bvals[i] != NULL; i++) {
            rv = ldapu_binary_cmp_certs(subject_cert,
                                        bvals[i]->bv_val,
                                        bvals[i]->bv_len);

            if (rv == LDAPU_SUCCESS) {
                break;
            }
        }

        ldapu_value_free_len(ld, bvals);

        if (rv == LDAPU_SUCCESS) {
            *entry_out = entry;
            break;
        }
    }

    return rv;
}

static int
parse_into_bitmask(const char *comps_in, long *bitmask_out, long default_val)
{
    long bitmask;
    char *comps = comps_in ? strdup(comps_in) : 0;

    if (!comps) {
        /* Not present in the config file */
        bitmask = default_val;
    } else if (!*comps) {
        /* present but empty */
        bitmask = 0;
    } else {
        char *ptr = comps;
        char *name = comps;
        long bit;
        int break_loop = 0;

        bitmask = 0;

        while (*name) {
            /* advance ptr to delimeter */
            while (*ptr && !isspace(*ptr) && *ptr != ',')
                ptr++;

            if (!*ptr)
                break_loop = 1;
            else
                *ptr++ = 0;

            bit = certmap_name_to_bit_pos(name);
            bitmask |= bit;

            if (break_loop)
                break;
            /* skip delimeters */
            while (*ptr && (isspace(*ptr) || *ptr == ','))
                ptr++;
            name = ptr;
        }
    }

    if (comps)
        free(comps);
    *bitmask_out = bitmask;
    /*     print_oid_bitmask(bitmask); */
    return LDAPU_SUCCESS;
}

static int
process_certinfo(LDAPUCertMapInfo_t *certinfo)
{
    int rv = LDAPU_SUCCESS;
    char *dncomps = 0;
    char *filtercomps = 0;
    char *libname = 0;
    char *verify = 0;
    char *fname = 0;
    char *searchAttr = 0;

    if (!ldapu_strcasecmp(certinfo->issuerName, "default")) {
        default_certmap_info = certinfo;
    } else if (!certinfo->issuerDN) {
        return LDAPU_ERR_NO_ISSUERDN_IN_CONFIG_FILE;
    } else {
        rv = ldapu_list_add_info(certmap_listinfo, certinfo);
    }

    if (rv != LDAPU_SUCCESS)
        return rv;

    /* look for dncomps property and parse it into the dncomps bitmask */
    rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_DNCOMPS, &dncomps);

    if (rv == LDAPU_SUCCESS && dncomps) {
        certinfo->dncompsState = COMPS_HAS_ATTRS;
        tolower_string(dncomps);
    } else if (rv == LDAPU_FAILED) {
        certinfo->dncompsState = COMPS_COMMENTED_OUT;
        rv = LDAPU_SUCCESS;
    } else if (rv == LDAPU_SUCCESS && !dncomps) {
        certinfo->dncompsState = COMPS_EMPTY;
        dncomps = strdup(""); /* present but empty */
    }

    rv = parse_into_bitmask(dncomps, &certinfo->dncomps, -1);

    free(dncomps);
    dncomps = NULL;

    if (rv != LDAPU_SUCCESS)
        return rv;

    /* look for filtercomps property and parse it into the filtercomps bitmask */
    rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_FILTERCOMPS,
                                    &filtercomps);

    if (rv == LDAPU_SUCCESS && filtercomps) {
        certinfo->filtercompsState = COMPS_HAS_ATTRS;
        tolower_string(filtercomps);
    } else if (rv == LDAPU_FAILED) {
        certinfo->filtercompsState = COMPS_COMMENTED_OUT;
        rv = LDAPU_SUCCESS;
    } else if (rv == LDAPU_SUCCESS && !filtercomps) {
        certinfo->filtercompsState = COMPS_EMPTY;
        filtercomps = strdup(""); /* present but empty */
    }

    rv = parse_into_bitmask(filtercomps, &certinfo->filtercomps, 0);

    free(filtercomps);
    filtercomps = NULL;

    if (rv != LDAPU_SUCCESS)
        return rv;

    /* look for "CmapLdapAttr" property and store it into searchAttr */
    rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_CERTMAP_LDAP_ATTR,
                                    &searchAttr);

    if (rv == LDAPU_FAILED || !searchAttr) {
        rv = LDAPU_SUCCESS;
    } else {
        certinfo->searchAttr = searchAttr;

        if (searchAttr && !certinfo->searchAttr)
            rv = LDAPU_ERR_OUT_OF_MEMORY;
        else
            rv = LDAPU_SUCCESS;
    }

    if (rv != LDAPU_SUCCESS)
        return rv;

    /* look for verifycert property and set the default verify function */
    /* The value of the verifycert property is ignored */
    rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_VERIFYCERT, &verify);

    if (rv == LDAPU_SUCCESS) {
        if (!ldapu_strcasecmp(verify, "on"))
            certinfo->verifyCert = 1;
        else if (!ldapu_strcasecmp(verify, "off"))
            certinfo->verifyCert = 0;
        else if (!verify || !*verify) /* for mail/news backward compatibilty */
            certinfo->verifyCert = 1; /* otherwise, this should be an error */
        else
            rv = LDAPU_ERR_MISSING_VERIFYCERT_VAL;
    } else if (rv == LDAPU_FAILED)
        rv = LDAPU_SUCCESS;

    if (verify)
        free(verify);

    if (rv != LDAPU_SUCCESS)
        return rv;

    {
        PRLibrary *lib = 0;

        /* look for the library property and load it */
        rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_LIBRARY, &libname);

        if (rv == LDAPU_SUCCESS) {
            if (libname && *libname) {
                lib = PR_LoadLibrary(libname);
                if (!lib)
                    rv = LDAPU_ERR_UNABLE_TO_LOAD_PLUGIN;
            } else {
                rv = LDAPU_ERR_MISSING_LIBNAME;
            }
        } else if (rv == LDAPU_FAILED)
            rv = LDAPU_SUCCESS;

        if (libname)
            free(libname);
        if (rv != LDAPU_SUCCESS)
            return rv;

        /* look for the InitFn property, find it in the libray and call it */
        rv = ldapu_certmap_info_attrval(certinfo, LDAPU_ATTR_INITFN, &fname);

        if (rv == LDAPU_SUCCESS) {
            if (fname && *fname) {
                /* If lib is NULL, PR_FindSymbol will search all libs loaded
                 * through PR_LoadLibrary.
                 */
                CertMapInitFn_t fn = (CertMapInitFn_t)PR_FindSymbol(lib, fname);

                if (!fn) {
                    rv = LDAPU_ERR_MISSING_INIT_FN_IN_LIB;
                } else {
                    rv = (*fn)(certinfo, certinfo->issuerName,
                               certinfo->issuerDN, this_dllname);
                }
            } else {
                rv = LDAPU_ERR_MISSING_INIT_FN_NAME;
            }
        } else if (lib) {
            /* If library is specified, init function must be specified */
            /* If init fn is specified, library may not be specified */
            rv = LDAPU_ERR_MISSING_INIT_FN_IN_CONFIG;
        } else if (rv == LDAPU_FAILED) {
            rv = LDAPU_SUCCESS;
        }

        if (fname)
            free(fname);

        if (rv != LDAPU_SUCCESS)
            return rv;
    }

    return rv;
}

/* This function will read multiple certmap directives and set the information
 * in the global certmap_listinfo structure.
 */
int
certmap_read_certconfig_file(const char *file)
{
    DBConfInfo_t *conf_info = 0;
    int rv;

    /* Read the config file */
    rv = dbconf_read_config_file_sub(file, LIB_DIRECTIVE, LIB_DIRECTIVE_LEN,
                                     &conf_info);

    /* Convert the conf_info into certmap_listinfo.  Some of the
     * sub-structures are simply hijacked rather than copied since we are
     * going to (carefully) free the conf_info anyway.
     */

    if (rv == LDAPU_SUCCESS && conf_info) {
        DBConfDBInfo_t *nextdb;
        DBConfDBInfo_t *curdb;
        LDAPUCertMapInfo_t *certinfo;

        curdb = conf_info->firstdb;

        while (curdb) {
            nextdb = curdb->next;
            rv = dbinfo_to_certinfo(curdb, &certinfo);
            if (rv != LDAPU_SUCCESS) {
                dbconf_free_confinfo(conf_info);
                return rv;
            }

            rv = process_certinfo(certinfo);
            if (rv != LDAPU_SUCCESS) {
                ldapu_certinfo_free(certinfo);
                dbconf_free_confinfo(conf_info);
                return rv;
            }

            curdb = nextdb;
        }

        dbconf_free_confinfo(conf_info);
    } else {
        rv = dbconf_to_certmap_err(rv);
    }

    return rv;
}

/* This function will read the "certmap default" directive from the config
 * file and set the information in the global certmap_info.
 */
int
certmap_read_default_certinfo(const char *file)
{
    DBConfDBInfo_t *db_info = 0;
    int rv;

    rv = dbconf_read_default_dbinfo_sub(file, LIB_DIRECTIVE, LIB_DIRECTIVE_LEN,
                                        &db_info);

    if (rv != LDAPU_SUCCESS)
        return rv;

    rv = dbinfo_to_certinfo(db_info, &default_certmap_info);

    dbconf_free_dbinfo(db_info);
    return rv;
}

static int
ldapu_cert_searchfn_default(void *cert, LDAP *ld, void *certmap_info_in, const char *basedn, const char *dn, const char *filter, const char **attrs, LDAPMessage ***res)
{
    int rv = LDAPU_FAILED;
    const char *ldapdn;
    LDAPUCertMapInfo_t *certmap_info = (LDAPUCertMapInfo_t *)certmap_info_in;
    LDAPMessage *single_res = NULL;
    LDAPMessage **multiple_res = NULL;


    if (certmap_info && certmap_info->searchAttr) {
        char *subjectDN = 0;

        rv = ldapu_get_cert_subject_dn(cert, &subjectDN);
        if (rv != LDAPU_SUCCESS || !subjectDN) {
            return rv;
        }
        size_t subjectDN_len = strlen(subjectDN);
        size_t eqout_len = 3 * subjectDN_len + 1; /* big enough for worst case (and final \0) */
        size_t searchAttr_len = strlen(certmap_info->searchAttr);
        size_t len = searchAttr_len + 1 + eqout_len;
        char *certFilter = (char *)ldapu_malloc(len * sizeof(char));
        if (!certFilter) {
            free(subjectDN);
            return LDAPU_ERR_OUT_OF_MEMORY;
        }
        strcpy(certFilter, certmap_info->searchAttr);
        certFilter[searchAttr_len] = '=';
        rv = value2filter(certFilter+searchAttr_len+1, eqout_len, subjectDN, subjectDN_len);
        free(subjectDN);
        len = strlen(certFilter);
        if (rv != LDAPU_SUCCESS || len <= 0) {
            ldapu_free((void *)certFilter);
            return LDAPU_ERR_INVALID_ARGUMENT;
        }

        if (ldapu_strcasecmp(basedn, "")) {
            rv = ldapu_find(ld, basedn, LDAP_SCOPE_SUBTREE, certFilter, attrs, 0, &single_res);
            ldapu_free((void *)certFilter);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = (LDAPMessage **)ldapu_malloc(2 * sizeof(LDAPMessage *));
                (*res)[0] = single_res;
                (*res)[1] = NULL;
                return rv;
            } else if (single_res) {
                ldapu_msgfree(ld, single_res);
                single_res = 0;
            }
        } else {
            rv = ldapu_find_entire_tree(ld, LDAP_SCOPE_SUBTREE, certFilter, attrs, 0, &multiple_res);
            ldapu_free((void *)certFilter);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = multiple_res;
                return rv;
            } else if (multiple_res) {
                int n;
                for (n = 0; multiple_res[n] != NULL; n++)
                    ldapu_msgfree(ld, multiple_res[n]);
                ldapu_memfree(ld, multiple_res);
            }
        }
    }

    if (dn && *dn) {
        /* First do the base level search --- NOT ANY MORE!! */
        /* We actually do the search on the whole subtree hanging from "ldapdn" since we want to
     * find all the entries that match the filter.
     * If we get more than one matching entry in return, it'll be at verify time that we'll
     * choose the correct one among them all.
     * However, if certificate verify is not active, certificate mapping will fail and will
     * consequently display an error message (something done at 'handle_handshake_done' level,
     * for instance). */
        ldapdn = dn;

        if (ldapu_strcasecmp(ldapdn, "")) {
            rv = ldapu_find(ld, ldapdn, LDAP_SCOPE_SUBTREE, filter, attrs, 0, &single_res);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = (LDAPMessage **)ldapu_malloc(2 * sizeof(LDAPMessage *));
                (*res)[0] = single_res;
                (*res)[1] = NULL;
                return rv;
            } else if (single_res) {
                ldapu_msgfree(ld, single_res);
                single_res = 0;
            }
        } else {
            rv = ldapu_find_entire_tree(ld, LDAP_SCOPE_SUBTREE, filter, attrs, 0, &multiple_res);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = multiple_res;
                return rv;
            } else if (multiple_res) {
                int n;
                for (n = 0; multiple_res[n] != NULL; n++)
                    ldapu_msgfree(ld, multiple_res[n]);
                ldapu_memfree(ld, multiple_res);
            }
        }
    } else {
        /* default the dn and filter for subtree search */
        ldapdn = basedn;
        if (!filter || !*filter) {
            if (certmap_info && certmap_info->searchAttr) {
                /* dn & filter returned by the mapping function are both NULL
           and 'searchAttr' based search has failed.  Don't do brute
           force search if 'searchAttr' is being used.  Otherwise,
           this search will result in all LDAP entries being
           returned.
           */
            } else {
                filter = "objectclass=*";
            }
        }
    }

    /* For local LDAP DB, the LDAP_SCOPE_BASE search may fail for dn == basedn
     * since that object doesn't actually exists.
     */
    if ((rv == LDAPU_FAILED || rv == LDAP_NO_SUCH_OBJECT) && filter && (!dn || !*dn)) {

        /* Try the subtree search only if the filter is non-NULL */
        if (ldapu_strcasecmp(ldapdn, "")) {
            rv = ldapu_find(ld, ldapdn, LDAP_SCOPE_SUBTREE, filter, 0, 0, &single_res);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = (LDAPMessage **)ldapu_malloc(2 * sizeof(LDAPMessage *));
                (*res)[0] = single_res;
                (*res)[1] = NULL;
                return rv;
            } else if (single_res) {
                ldapu_msgfree(ld, single_res);
                single_res = 0;
            }
        } else {
            rv = ldapu_find_entire_tree(ld, LDAP_SCOPE_SUBTREE, filter, 0, 0, &multiple_res);
            if (rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                *res = multiple_res;
                return rv;
            } else if (multiple_res) {
                int n;
                for (n = 0; multiple_res[n] != NULL; n++)
                    ldapu_msgfree(ld, multiple_res[n]);
                ldapu_memfree(ld, multiple_res);
            }
        }
    }

    if (rv == LDAPU_FAILED) {
        /* Not an error but couldn't map the cert */
        rv = LDAPU_ERR_MAPPED_ENTRY_NOT_FOUND;
    } else if ((!dn || !*dn) && (rv == LDAP_NO_SUCH_OBJECT)) {
        rv = LDAPU_ERR_INVALID_SUFFIX;
    }

    return rv;
}

NSAPI_PUBLIC int
ldapu_issuer_certinfo(const CERTName *issuerDN, void **certmap_info)
{
    *certmap_info = 0;

    if (certmap_listinfo) {
        LDAPUListNode_t *cur = certmap_listinfo->head;
        while (cur) {
            LDAPUCertMapInfo_t *info = (LDAPUCertMapInfo_t *)cur->info;

            if (NULL == info->issuerDN) {
                /* no DN to compare to (probably the default certmap info) */
                continue;
            }

            if (CERT_CompareName(issuerDN, info->issuerDN) == SECEqual) {
                *certmap_info = cur->info;
                break;
            }
            cur = cur->next;
        }
    }
    return *certmap_info ? LDAPU_SUCCESS : LDAPU_FAILED;
}

NSAPI_PUBLIC int
ldapu_certmap_info_attrval(void *certmap_info_in,
                           const char *attr,
                           char **val)
{
    /* Look for given attr in the certmap_info and return its value */
    LDAPUCertMapInfo_t *certmap_info = (LDAPUCertMapInfo_t *)certmap_info_in;
    LDAPUListNode_t *curprop = certmap_info->propval ? certmap_info->propval->head : 0;
    LDAPUPropVal_t *propval;
    int rv = LDAPU_FAILED;

    *val = 0;
    while (curprop) {
        propval = (LDAPUPropVal_t *)curprop->info;
        if (!ldapu_strcasecmp(propval->prop, attr)) {
            *val = propval->val ? strdup(propval->val) : 0;
            rv = LDAPU_SUCCESS;
            break;
        }
        curprop = curprop->next;
    }

    return rv;
}

static int
AddAVAToBuf(char *buf, int size, int *len, const char *tagName, CERTAVA *ava, int is_filter)
{
    int lenLen;
    int taglen;
    SECStatus rv;

    buf += *len;

    /* Copied from ns/lib/libsec ...
     * XXX this code is incorrect in general
     * -- should use a DER template.
     */
    lenLen = 2;
    if (ava->value.len >= 128)
        lenLen = 3;

    taglen = PL_strlen(tagName);
    memcpy(buf, tagName, taglen);
    buf[taglen++] = '=';

    if (is_filter != 0) {
        /* values should not be quoted in filter but * must be escaped */
        rv = value2filter(buf + taglen,
                          size - taglen,
                          (char *)ava->value.data + lenLen,
                          ava->value.len - lenLen);
        *len += strlen(buf);
        return rv;
    } else {
        rv = CERT_RFC1485_EscapeAndQuote(buf + taglen,
                                         size - taglen,
                                         (char *)ava->value.data + lenLen,
                                         ava->value.len - lenLen);
        *len += strlen(buf);
        return (rv == SECSuccess ? LDAPU_SUCCESS : LDAPU_FAILED);
    }
}

static int
AddToLdapDN(char *ldapdn, int size, int *dnlen, const char *tagName, CERTAVA *ava)
{
    char *dn = ldapdn + *dnlen;

    if (*dnlen) {
        strcat(dn, ", ");
        dn += 2;
        *dnlen += 2;
    }
    return AddAVAToBuf(ldapdn, size, dnlen, tagName, ava, 0);
}

static int
AddToFilter(char *filter, int size, int *flen, const char *tagName, CERTAVA *ava)
{
    int rv;

    /* Append opening parenthesis */
    strcat(filter + *flen, " (");
    *flen += 2;
    rv = AddAVAToBuf(filter, size, flen, tagName, ava, 1);

    if (rv != LDAPU_SUCCESS)
        return rv;

    /* Append closing parenthesis */
    strcat(filter + *flen, ")");
    (*flen)++;

    return rv;
}

NSAPI_PUBLIC int
ldapu_free_cert_ava_val(char **val)
{
    char **ptr = val;

    if (!val)
        return LDAPU_SUCCESS;

    while (*ptr)
        free(*ptr++);
    free(val);

    return LDAPU_SUCCESS;
}

static int
ldapu_cert_mapfn_default(void *cert_in, LDAP *ld __attribute__((unused)), void *certmap_info_in, char **ldapDN_out, char **filter_out)
{
    CERTCertificate *cert = (CERTCertificate *)cert_in;
    LDAPUCertMapInfo_t *certmap_info = (LDAPUCertMapInfo_t *)certmap_info_in;
    int rv = LDAPU_SUCCESS;

    *ldapDN_out = *filter_out = 0;

    if (!certmap_info) {
        /* Use subject DN as is -- identity mapping function */
        rv = ldapu_get_cert_subject_dn(cert, ldapDN_out);

        return rv;
    } else {
        /*
     * Iterate over rdns from the subject and collect AVAs depending on
     * dnComps and filtercomps to form ldapDN and filter respectively.
     * certmap_info->dncomps
     */
        CERTName *subject = &cert->subject;
        CERTRDN **rdns = subject->rdns;
        CERTRDN **lastRdn;
        CERTRDN **rdn;
        CERTAVA **avas;
        CERTAVA *ava;
        char ldapdn[BIG_LINE];
        char filter[BIG_LINE];
        int dnlen = 0;    /* ldap DN length */
        int flen = 0;     /* filter length */
        int numfavas = 0; /* no of avas added to filter */

        if (rdns == NULL) {
            /* error */
            return LDAPU_ERR_INTERNAL;
        }

        /* find last RDN */
        lastRdn = rdns;
        while (*lastRdn)
            lastRdn++;
        lastRdn--;

        /* Initialize filter to "(&" */
        strcpy(filter, "(&");
        flen = 2;

        /*
     * Loop over subject rdns in the _reverse_ order while forming ldapDN
     * and filter.
     */
        for (rdn = lastRdn; rdn >= rdns; rdn--) {
            avas = (*rdn)->avas;
            while ((ava = *avas++) != NULL) {
                int tag = CERT_GetAVATag(ava);
                const char *tagName = certmap_secoid_to_name(tag);

                if (PresentInComps(certmap_info->dncomps, tag)) {
                    rv = AddToLdapDN(ldapdn, BIG_LINE, &dnlen, tagName, ava);
                    if (rv != LDAPU_SUCCESS)
                        return rv;
                }

                if (PresentInComps(certmap_info->filtercomps, tag)) {
                    rv = AddToFilter(filter, BIG_LINE, &flen, tagName, ava);
                    if (rv != LDAPU_SUCCESS)
                        return rv;
                    numfavas++;
                }
            }
        }

        if (numfavas == 0) {
            /* nothing added to filter */
            *filter = 0;
        } else if (numfavas == 1) {
            /* one ava added to filter -- remove "(& (" from the front and ")"
         * from the end.
         */
            *filter_out = strdup(filter + 4);
            if (!*filter_out)
                return LDAPU_ERR_OUT_OF_MEMORY;
            (*filter_out)[strlen(*filter_out) - 1] = 0;
        } else {
            /* Add the closing parenthesis to filter */
            strcat(filter + flen, ")");
            *filter_out = strdup(filter);
        }

        if (dnlen >= BIG_LINE)
            return LDAPU_FAILED;
        ldapdn[dnlen] = 0;
        *ldapDN_out = *ldapdn ? strdup(ldapdn) : 0;

        if ((numfavas && !*filter_out) || (dnlen && !*ldapDN_out)) {
            /* strdup failed */
            return LDAPU_ERR_OUT_OF_MEMORY;
        }

        if ((certmap_info->dncompsState == COMPS_HAS_ATTRS && dnlen == 0) ||
            (certmap_info->filtercompsState == COMPS_HAS_ATTRS &&
             numfavas == 0)) {
            /* At least one attr in DNComps should be present in the cert */
            /* Same is true for FilterComps */
            rv = LDAPU_ERR_MAPPED_ENTRY_NOT_FOUND;
        }
    }

    return rv;
}

NSAPI_PUBLIC int
ldapu_set_cert_mapfn(const CERTName *issuerDN,
                     CertMapFn_t mapfn)
{
    LDAPUCertMapInfo_t *certmap_info;
    int rv;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    rv = ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);

    /* Don't set the mapping function if certmap_info doesen't exist */
    if (rv != LDAPU_SUCCESS)
        return rv;

    certmap_info->mapfn = mapfn;
    return LDAPU_SUCCESS;
}

static CertMapFn_t
ldapu_get_cert_mapfn_sub(LDAPUCertMapInfo_t *certmap_info)
{
    CertMapFn_t mapfn;

    if (certmap_info && certmap_info->mapfn)
        mapfn = certmap_info->mapfn;
    else if (default_certmap_info && default_certmap_info->mapfn)
        mapfn = default_certmap_info->mapfn;
    else
        mapfn = ldapu_cert_mapfn_default;

    return mapfn;
}

NSAPI_PUBLIC CertMapFn_t
ldapu_get_cert_mapfn(const CERTName *issuerDN)
{
    LDAPUCertMapInfo_t *certmap_info = 0;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);
    /* certmap_info may be NULL -- use the default */

    return ldapu_get_cert_mapfn_sub(certmap_info);
}

NSAPI_PUBLIC int
ldapu_set_cert_searchfn(const CERTName *issuerDN,
                        CertSearchFn_t searchfn)
{
    LDAPUCertMapInfo_t *certmap_info;
    int rv;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    rv = ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);

    /* Don't set the mapping function if certmap_info doesen't exist */
    if (rv != LDAPU_SUCCESS)
        return rv;

    certmap_info->searchfn = searchfn;
    return LDAPU_SUCCESS;
}

static CertSearchFn_t
ldapu_get_cert_searchfn_sub(LDAPUCertMapInfo_t *certmap_info)
{
    CertSearchFn_t searchfn;

    if (certmap_info && certmap_info->searchfn)
        searchfn = certmap_info->searchfn;
    else if (default_certmap_info && default_certmap_info->searchfn)
        searchfn = default_certmap_info->searchfn;
    else
        searchfn = ldapu_cert_searchfn_default;

    return searchfn;
}

NSAPI_PUBLIC CertSearchFn_t
ldapu_get_cert_searchfn(const CERTName *issuerDN)
{
    LDAPUCertMapInfo_t *certmap_info = 0;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);
    /* certmap_info may be NULL -- use the default */

    return ldapu_get_cert_searchfn_sub(certmap_info);
}

NSAPI_PUBLIC int
ldapu_set_cert_verifyfn(const CERTName *issuerDN,
                        CertVerifyFn_t verifyfn)
{
    LDAPUCertMapInfo_t *certmap_info;
    int rv;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    rv = ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);

    /* Don't set the verify function if certmap_info doesen't exist */
    if (rv != LDAPU_SUCCESS)
        return rv;

    certmap_info->verifyfn = verifyfn;
    return LDAPU_SUCCESS;
}

static CertVerifyFn_t
ldapu_get_cert_verifyfn_sub(LDAPUCertMapInfo_t *certmap_info)
{
    CertVerifyFn_t verifyfn;

    if (certmap_info && certmap_info->verifyfn)
        verifyfn = certmap_info->verifyfn;
    else if (default_certmap_info && default_certmap_info->verifyfn)
        verifyfn = default_certmap_info->verifyfn;
    else
        verifyfn = ldapu_cert_verifyfn_default;

    return verifyfn;
}

NSAPI_PUBLIC CertVerifyFn_t
ldapu_get_cert_verifyfn(const CERTName *issuerDN)
{
    LDAPUCertMapInfo_t *certmap_info = 0;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);
    /* certmap_info may be NULL -- use the default */

    return ldapu_get_cert_verifyfn_sub(certmap_info);
}

#if 0 /* may need this in the future */
static int ldapu_certinfo_copy (const LDAPUCertMapInfo_t *from,
                const char *newIssuerName,
                const char *newIssuerDN,
                LDAPUCertMapInfo_t *to)
{
    /* This function is not tested and is not used */
    int rv;

    to->issuerName = newIssuerName ? strdup(newIssuerName) : 0;
    to->issuerDN = newIssuerDN ? strdup(newIssuerDN) : 0;
    if (from->propval) {
    rv = ldapu_list_copy(from->propval, &to->propval, ldapu_propval_copy);
    if (rv != LDAPU_SUCCESS) return rv;
    }
    else {
    to->propval = 0;
    }

    return process_certinfo(to);
}
#endif

NSAPI_PUBLIC int
ldapu_cert_to_ldap_entry(void *cert, LDAP *ld, const char *basedn, LDAPMessage **res)
{
    char *ldapDN = 0;
    char *filter = 0;
    LDAPUCertMapInfo_t *certmap_info;
    LDAPMessage **res_array = NULL;
    CertMapFn_t mapfn;
    CertVerifyFn_t verifyfn;
    CertSearchFn_t searchfn;
    int rv, i, j;

    *res = 0;

    if (!certmap_attrs[0]) {
        /* Initialize certmap_attrs */
        certmap_attrs[0] = ldapu_strings[LDAPU_STR_ATTR_USER];
        certmap_attrs[1] = ldapu_strings[LDAPU_STR_ATTR_CERT];
        certmap_attrs[2] = ldapu_strings[LDAPU_STR_ATTR_CERT_NOSUBTYPE];
        certmap_attrs[3] = 0;
    }

    CERTName *issuerDN = ldapu_get_cert_issuer_dn_as_CERTName(cert);
    /*        ^ don't need to free this; it will be freed with ^ the cert */

    if (NULL == issuerDN)
        return LDAPU_ERR_NO_ISSUERDN_IN_CERT;

    /* don't free the certmap_info -- its a pointer to an internal structure */
    rv = ldapu_issuer_certinfo(issuerDN, (void **)&certmap_info);

    if (!certmap_info)
        certmap_info = default_certmap_info;

    /* Get the mapping function from the certmap_info */
    mapfn = ldapu_get_cert_mapfn_sub(certmap_info);

    rv = (*mapfn)(cert, ld, certmap_info, &ldapDN, &filter);

    if (rv != LDAPU_SUCCESS) {
        free(ldapDN);
        free(filter);
        return rv;
    }

    /* Get the search function from the certmap_info - certinfo maybe NULL */
    searchfn = ldapu_get_cert_searchfn_sub(certmap_info);

    rv = (*searchfn)(cert, ld, certmap_info, basedn, ldapDN, filter,
                     certmap_attrs, &res_array);

    free(ldapDN);
    free(filter);

    /*
     * Get the verify cert function & call it.
     */
    j = 0;
    if ((rv == LDAPU_SUCCESS || rv == LDAPU_ERR_MULTIPLE_MATCHES) &&
        (certmap_info ? certmap_info->verifyCert : 0)) {
        verifyfn = ldapu_get_cert_verifyfn_sub(certmap_info);

        if (verifyfn) {
            int verify_rv;

            i = 0;
            do {
                LDAPMessage *entry;
                verify_rv = (*verifyfn)(cert, ld, certmap_info, res_array[i], &entry);

                if (rv == LDAPU_ERR_MULTIPLE_MATCHES) {
                    if (verify_rv == LDAPU_SUCCESS) {
                        /* 'entry' points to the matched entry */
                        /* Get the 'res' which only contains this entry */
                        char *dn = ldapu_get_dn(ld, entry);
                        if (*res)
                            ldapu_msgfree(ld, *res);
                        rv = ldapu_find(ld, dn, LDAP_SCOPE_BASE, 0, certmap_attrs, 0, res);
                        ldapu_memfree(ld, dn);
                    } else {
                        /* Verify failed for multiple matches -- keep rv */
                        /* multiple matches err is probably more interesting to
               the caller then any other error returned by the verify
               fn */
                    }
                } else /* rv == LDAPU_SUCCESS */ {
                    if (verify_rv == LDAPU_SUCCESS) {
                        *res = res_array[0];
                        j = 1;
                    } else {
                        rv = verify_rv;
                    }
                }
            } while ((verify_rv != LDAPU_SUCCESS) && (res_array[++i] != NULL));
        }
    } else {
        if (rv == LDAPU_SUCCESS) {
            *res = res_array[0];
            j = 1;
        }
    }


    if (rv != LDAPU_SUCCESS) {
        if (*res) {
            ldapu_msgfree(ld, *res);
            *res = 0;
        }
    }

    i = j; /* ugaston - if the search had been successful, despite verifyCert being "off",
         * mapping is considered successful, so we keep the first (and only) response message.
         * If, on the other hand, the search had returned multiple matches, the fact
         * of having verifyCert "off" automatically turns the mapping faulty, so we
         * don't need to care about keeping any response at all.
         */

    if (res_array) {
        while (res_array[i] != NULL) {
            ldapu_msgfree(ld, res_array[i]);
            res_array[i++] = 0;
        }
        ldapu_memfree(ld, res_array);
    }
    return rv;
}

/* The caller shouldn't free the entry */
NSAPI_PUBLIC int
ldapu_cert_to_user(void *cert, LDAP *ld, const char *basedn, LDAPMessage **res_out, char **user)
{
    int rv;
    LDAPMessage *res;
    LDAPMessage *entry;
    int numEntries;
    char **attrVals = NULL;

    *res_out = 0;
    *user = 0;

    rv = ldapu_cert_to_ldap_entry(cert, ld, basedn, &res);

    if (rv != LDAPU_SUCCESS) {
        goto done;
    }

    if (!res) {
        rv = LDAPU_ERR_EMPTY_LDAP_RESULT;
        goto done;
    }

    /* Extract user login (the 'uid' attr) from 'res' */
    numEntries = ldapu_count_entries(ld, res);

    if (numEntries != 1) {
        rv = LDAPU_ERR_MULTIPLE_MATCHES;
        goto done;
    }

    entry = ldapu_first_entry(ld, res);

    if (!entry) {
        rv = LDAPU_ERR_MISSING_RES_ENTRY;
        goto done;
    }

    attrVals = ldapu_get_values(ld, entry,
                                ldapu_strings[LDAPU_STR_ATTR_USER]);

    if (!attrVals || !attrVals[0]) {
        rv = LDAPU_ERR_MISSING_UID_ATTR;
        goto done;
    }

    *user = strdup(attrVals[0]);

    /*     ldapu_msgfree(res); */

    if (!*user) {
        rv = LDAPU_ERR_OUT_OF_MEMORY;
        goto done;
    }

    *res_out = res;

done:
    if (attrVals) {
        ldapu_value_free(ld, attrVals);
    }

    return rv;
}

static void *
ldapu_propval_free(void *propval_in, void *arg __attribute__((unused)))
{
    LDAPUPropVal_t *propval = (LDAPUPropVal_t *)propval_in;

    if (propval->prop)
        free(propval->prop);
    if (propval->val)
        free(propval->val);
    memset((void *)propval, 0, sizeof(LDAPUPropVal_t));
    free(propval);
    return 0;
}

void
ldapu_certinfo_free(void *info_in)
{
    LDAPUCertMapInfo_t *certmap_info = (LDAPUCertMapInfo_t *)info_in;

    if (certmap_info->issuerName)
        free(certmap_info->issuerName);
    if (certmap_info->issuerDN)
        free(certmap_info->issuerDN);
    if (certmap_info->propval)
        ldapu_list_free(certmap_info->propval, ldapu_propval_free);
    if (certmap_info->searchAttr)
        free(certmap_info->searchAttr);
    memset((void *)certmap_info, 0, sizeof(LDAPUCertMapInfo_t));
    free(certmap_info);
}

static void *
ldapu_certinfo_free_helper(void *info, void *arg __attribute__((unused)))
{
    ldapu_certinfo_free(info);
    return (void *)LDAPU_SUCCESS;
}

void
ldapu_certmap_listinfo_free(void *_certmap_listinfo)
{
    LDAPUCertMapListInfo_t *list = (LDAPUCertMapListInfo_t *)_certmap_listinfo;
    ldapu_list_free(list, ldapu_certinfo_free_helper);
}

void
ldapu_propval_list_free(void *propval_list)
{
    LDAPUPropValList_t *list = (LDAPUPropValList_t *)propval_list;
    ldapu_list_free(list, ldapu_propval_free);
    free(list);
}

int
ldapu_certmap_init(const char *config_file,
                   const char *dllname,
                   LDAPUCertMapListInfo_t **certmap_list,
                   LDAPUCertMapInfo_t **certmap_default)
{
    int rv;
    certmap_listinfo = (LDAPUCertMapListInfo_t *)calloc(1, sizeof(LDAPUCertMapListInfo_t));

    *certmap_list = 0;
    *certmap_default = 0;
    PR_snprintf(this_dllname, sizeof(this_dllname), "%s", dllname);

    if (!certmap_listinfo)
        return LDAPU_ERR_OUT_OF_MEMORY;

    rv = certmap_read_certconfig_file(config_file);

    if (rv == LDAPU_SUCCESS) {
        *certmap_list = certmap_listinfo;
        *certmap_default = default_certmap_info;
    }

    return rv;
}

NSAPI_PUBLIC int
ldaputil_exit()
{
    if (default_certmap_info) {
        ldapu_certinfo_free(default_certmap_info);
        default_certmap_info = 0;
    }

    if (certmap_listinfo) {
        ldapu_certmap_listinfo_free(certmap_listinfo);
        certmap_listinfo = 0;
    }

    return LDAPU_SUCCESS;
}


NSAPI_PUBLIC void
ldapu_free(void *ptr)
{
    if (ptr)
        free(ptr);
}

NSAPI_PUBLIC void
ldapu_free_old(char *ptr)
{
    free((void *)ptr);
}

NSAPI_PUBLIC void *
ldapu_malloc(int size)
{
    return malloc(size);
}

NSAPI_PUBLIC char *
ldapu_strdup(const char *ptr)
{
    return strdup(ptr);
}

NSAPI_PUBLIC void *
ldapu_realloc(void *ptr, int size)
{
    return realloc(ptr, size);
}
