/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * AD rewriters
 *
 * This library contains filter rewriters and computed attribute rewriters.
 */

#include "slap.h"

static char *rewriter_name = "filter rewriter adfilter";

#define OBJECTCATEGORY "objectCategory"
#define OBJECTSID "objectSid"


/**********************************************************
 * Rewrite ObjectSID
 */

#define SID_ID_AUTHS 6
#define SID_SUB_AUTHS 15
typedef struct dom_sid {
        uint8_t sid_rev_num;
        int8_t num_auths;                  /* [range(0,15)] */
        uint8_t id_auth[SID_ID_AUTHS];     /* highest order byte has index 0 */
        uint32_t sub_auths[SID_SUB_AUTHS]; /* host byte-order */
} dom_sid_t;

typedef struct bin_sid {
    uint8_t *sid;
    size_t length;
} bin_sid_t;

/*
 * Borrowed from SSSD
 * src/lib/idmap/sss_idmap_conv.c:sss_idmap_dom_sid_to_bin_sid
 * returns:
 *   -1: if this is not a SID in dom_sid format
 *    0: success
 *    1: internal error
 */
static int32_t
dom_sid_to_bin_sid(dom_sid_t *dom_sid, bin_sid_t *res)
{
    bin_sid_t bin_sid = {0};
    size_t p = 0;
    uint32_t val;

    if (res == NULL) {
        return 1;
    }
    if (dom_sid->num_auths > SID_SUB_AUTHS) {
        return -1;
    }
    bin_sid.length = 2 + SID_ID_AUTHS + dom_sid->num_auths * sizeof(uint32_t);

    bin_sid.sid = (uint8_t *) slapi_ch_calloc(1, bin_sid.length);
    if (bin_sid.sid == NULL) {
        return 1;
    }

    bin_sid.sid[p] = dom_sid->sid_rev_num;
    p++;

    bin_sid.sid[p] = dom_sid->num_auths;
    p++;

    for (size_t i = 0; i < SID_ID_AUTHS; i++) {
        bin_sid.sid[p] = dom_sid->id_auth[i];
        p++;
    }

    for (size_t i = 0; i < dom_sid->num_auths; i++) {
        if ((p + sizeof(uint32_t)) > bin_sid.length) {
            return -1;
        }
        val = htole32(dom_sid->sub_auths[i]);
        memcpy(&bin_sid.sid[p], &val, sizeof(val));
        p += sizeof(val);
    }
    res->sid = bin_sid.sid;
    res->length = bin_sid.length;

    return 0;
}
/*
 * Borrowed from SSSD
 * src/lib/idmap/sss_idmap_conv.c:sss_idmap_sid_to_dom_sid
 * returns:
 *   -1: if this is not a SID in string format
 *    0: success
 *    1: internal error
 */
static int32_t
str_sid_to_dom_sid(char *sid_string, dom_sid_t *res)
{
    dom_sid_t sid = {0};
    uint64_t ul;
    char *r, *end;

    if (res == NULL) {
        return 1;
    }

    if ((sid_string == NULL) || strncasecmp(sid_string, "S-", 2)) {
        return -1;
    }

    /* Here we have a string SID i.e.: S-1-2-4..
     * S-X-2-3-4..  X is sid_rev_num 8bits
     */
    if (!isdigit(sid_string[2])) {
        return -1;
    }
    ul =  (uint64_t) strtoul(sid_string + 2, &r, 10);
    if (r == NULL || *r != '-' || ul > UINT8_MAX) {
        return -1;
    }
    sid.sid_rev_num = (uint8_t) ul;
    r++;

    /* r points to 2-3-4..
     * '2' is used for the id_auth
     */
    ul = strtoul(r, &r, 10);
    if (r == NULL || ul > UINT32_MAX) {
        return -1;
    }

    /* id_auth in the string should always be <2^32 in decimal */
    /* store values in the same order as the binary representation */
    sid.id_auth[0] = 0;
    sid.id_auth[1] = 0;
    sid.id_auth[2] = (ul & 0xff000000) >> 24;
    sid.id_auth[3] = (ul & 0x00ff0000) >> 16;
    sid.id_auth[4] = (ul & 0x0000ff00) >> 8;
    sid.id_auth[5] = (ul & 0x000000ff);

    /* Now r point the the sub_auth
     * There are maximum of 15 sub_auth (SID_SUB_AUTHS): -3-4-5...*/
    if (*r == '\0') {
        /* no sub auths given */
        return 0;
    }
    if (*r != '-') {
        return -1;
    }
    do {
        if (sid.num_auths >= SID_SUB_AUTHS) {
            return -1;
        }

        r++;
        if (!isdigit(*r)) {
            return -1;
        }

        ul = strtoul(r, &end, 10);
        if (ul > UINT32_MAX || end == NULL || (*end != '\0' && *end != '-')) {
            return -1;
        }

        sid.sub_auths[sid.num_auths++] = ul;

        r = end;
    } while (*r != '\0');

    memcpy(res, &sid, sizeof(sid));
    return 0;
}

/*
 * Callback to rewrite string objectSid
 * Borrowed from SSSD sss_idmap_sid_to_bin_sid
 */
static int
substitute_string_objectsid(Slapi_Filter *f, void *arg)
{
    char *filter_type;
    struct berval *bval;
    char *newval;
    char logbuf[1024] = {0};
    int32_t rc;
    char *objectsid_string_header="S-";
    dom_sid_t dom_sid = {0};
    bin_sid_t bin_sid = {0};
    int32_t loglevel;

    /* If (objectSid=S-1-2-3-4..) --> (objectSid=<binary representation of S-1-2-3-4..> */
    if ((slapi_filter_get_ava(f, &filter_type, &bval) == 0) &&
        (slapi_filter_get_choice(f) == LDAP_FILTER_EQUALITY) &&
        (bval->bv_val) &&
        (strcasecmp(filter_type, OBJECTSID) == 0) &&
        (strncasecmp(bval->bv_val, objectsid_string_header, strlen(objectsid_string_header)) == 0)) {
        /* This filter component is "(objectsid=S-..) let's try to convert it */

        rc = str_sid_to_dom_sid(bval->bv_val, &dom_sid);
        switch (rc) {
        case -1:
            /* This is not a valid string objectSid */
            slapi_log_err(SLAPI_LOG_ERR, rewriter_name, "substitute_string_objectsid component %s : is not a valid string sid\n",
                          slapi_filter_to_string(f, logbuf, sizeof (logbuf)));

            /* do not rewrite this component but continue with the others */
            return SLAPI_FILTER_SCAN_CONTINUE;
        case 1:
            /* internal error while converting string objectSid */
            slapi_log_err(SLAPI_LOG_ERR, rewriter_name, "substitute_string_objectsid component %s : fail to convert into dom_sid a string sid\n",
                          slapi_filter_to_string(f, logbuf, sizeof (logbuf)));

            /* do not rewrite this component but continue with the others */
            return SLAPI_FILTER_SCAN_CONTINUE;
        default:
                /* go through */
            break;
        }
        rc = dom_sid_to_bin_sid(&dom_sid, &bin_sid);
        switch (rc) {
        case -1:
            /* This is not a valid dom objectSid */
            slapi_log_err(SLAPI_LOG_ERR, rewriter_name, "substitute_string_objectsid component %s : is not a valid dom sid\n",
                          slapi_filter_to_string(f, logbuf, sizeof (logbuf)));

            /* do not rewrite this component but continue with the others */
            return SLAPI_FILTER_SCAN_CONTINUE;
        case 1:
            /* internal error while converting dom objectSid */
            slapi_log_err(SLAPI_LOG_ERR, rewriter_name, "substitute_string_objectsid component %s : fail to convert into binary sid a dom sid\n",
                          slapi_filter_to_string(f, logbuf, sizeof (logbuf)));

            /* do not rewrite this component but continue with the others */
            return SLAPI_FILTER_SCAN_CONTINUE;
        default:
                /* go through */
            break;
        }
        loglevel = LDAP_DEBUG_ANY;
        if (loglevel_is_set(loglevel)) {
            char logbuf[100] = {0};
            char filterbuf[1024] = {0};
            char *valueb64, *valueb64_sav;
            size_t lenb64;
            size_t maxcopy;

            if (sizeof(logbuf) <= ((bin_sid.length * 2) + 1)) {
                maxcopy = (sizeof(logbuf)/2) - 1;
            } else {
                maxcopy = bin_sid.length;
            }

            for (size_t i = 0, j = 0; i < maxcopy; i++) {
                PR_snprintf(logbuf + j, 3, "%02x", bin_sid.sid[i]);
                j += 2;
            }
            lenb64 = LDIF_SIZE_NEEDED(strlen("encodedb64"), bin_sid.length);
            valueb64 = valueb64_sav = (char *) slapi_ch_calloc(1, lenb64 + 1);
            slapi_ldif_put_type_and_value_with_options(&valueb64, "encodedb64", (const char *)bin_sid.sid, bin_sid.length, LDIF_OPT_NOWRAP);

            slapi_log_err(SLAPI_LOG_INFO, rewriter_name, "substitute_string_objectsid component %s : 0x%s (%s)\n",
                          slapi_filter_to_string(f, filterbuf, sizeof (filterbuf)),
                          logbuf,
                          valueb64_sav);
            slapi_ch_free_string(&valueb64_sav);
        }
        slapi_ch_free_string(&bval->bv_val);
        /* It consums the value returned by dom_sid_to_bin_sid
         * setting it into the bval it will be later freed
         * when the filter will be freed
         */
        bval->bv_val = bin_sid.sid;
        bval->bv_len = bin_sid.length;
    }

    /* Return continue because we should
     * substitute 'from' in all filter components
     */
    return SLAPI_FILTER_SCAN_CONTINUE;
}

/*
 * This is a filter rewriter function for 'ObjectSid' attribute
 *
 * Its rewriter config entry looks like
 * dn: cn=adfilter,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: adfilter
 * nsslapd-libpath: /lib/dirsrv/librewriters
 * nsslapd-filterrewriter: adfilter_rewrite_objectsid
 */
int32_t
adfilter_rewrite_objectsid(Slapi_PBlock *pb)
{
    Slapi_Filter *clientFilter = NULL;
    Slapi_DN *sdn = NULL;
    int error_code = 0;
    int rc;
    char *strFilter;

    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &clientFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &strFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);

    if (strFilter && (strcasestr(strFilter, OBJECTSID) == NULL)) {
        /* accelerator: returns if filter string does not contain objectSID */
        return SEARCH_REWRITE_CALLBACK_CONTINUE;
    }
    /* Now apply substitute_string_objectsid on each filter component */
    rc = slapi_filter_apply(clientFilter, substitute_string_objectsid, NULL /* no arg */, &error_code);
    if (rc == SLAPI_FILTER_SCAN_NOMORE) {
        return SEARCH_REWRITE_CALLBACK_CONTINUE; /* Let's others rewriter play */
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "adfilter_rewrite_objectSid", "Could not update the search filter - error %d (%d)\n",
                      rc, error_code);
        return SEARCH_REWRITE_CALLBACK_ERROR; /* operation error */
    }
}

/*********************************************************
 * Rewrite OBJECTCATEGORY as described in [1]
 * [1] https://social.technet.microsoft.com/wiki/contents/articles/5392.active-directory-ldap-syntax-filters.aspx#Filter_on_objectCategory_and_objectClass
 * static char *objectcategory_shortcuts[] = {"person", "computer", "user", "contact", "group", "organizationalPerson", NULL};
 */

typedef struct {
    char *attrtype;  /* type = objectCategory */
    char *format;
} objectCategory_arg_t;

static int
substitute_shortcut(Slapi_Filter *f, void *arg)
{
    objectCategory_arg_t *substitute_arg = (objectCategory_arg_t *) arg;
    char *filter_type;
    struct berval *bval;
    char *newval;
    char logbuf[1024] = {0};

    if ((substitute_arg == NULL) ||
        (substitute_arg->attrtype == NULL) ||
        (substitute_arg->format == NULL)) {
        return SLAPI_FILTER_SCAN_STOP;
    }

    /* If (objectCategory=<shortcut>) --> (objectCategory=cn=<shortcut>,cn=Schema,cn=Configuration,<suffix>) */
    if ((slapi_filter_get_ava(f, &filter_type, &bval) == 0) &&
        (slapi_filter_get_choice(f) == LDAP_FILTER_EQUALITY) &&
        (bval->bv_val) &&
        (strcasecmp(filter_type, substitute_arg->attrtype) == 0)) {
        newval = slapi_ch_smprintf(substitute_arg->format, bval->bv_val);
        slapi_log_err(SLAPI_LOG_FILTER, rewriter_name, "objectcategory_check_filter - 1 component %s : %s -> %s\n",
                      slapi_filter_to_string(f, logbuf, sizeof (logbuf)),
                      bval->bv_val,
                      newval);
        slapi_ch_free_string(&bval->bv_val);
        bval->bv_val = newval;
        bval->bv_len = strlen(newval);
    }

    /* Return continue because we should
     * substitute 'from' in all filter components
     */
    return SLAPI_FILTER_SCAN_CONTINUE;
}

/*
 * This is a filter rewriter function for 'ObjectCagerory' attribute
 *
 * Its rewriter config entry looks like
 * dn: cn=adfilter,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: adfilter
 * nsslapd-libpath: /lib/dirsrv/librewriters
 * nsslapd-filterrewriter: adfilter_rewrite_objectCategory
 */
int32_t
adfilter_rewrite_objectCategory(Slapi_PBlock *pb)
{
    Slapi_Filter *clientFilter = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Backend *be = NULL;
    const char *be_suffix = NULL;
    int error_code = 0;
    int rc;
    char *format;
    char *strFilter;
    objectCategory_arg_t arg;

    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &clientFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &strFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);

    if (strFilter && (strcasestr(strFilter, OBJECTCATEGORY) == NULL)) {
        /* accelerator: returns if filter string does not contain objectcategory */
        return SEARCH_REWRITE_CALLBACK_CONTINUE;
    }
    if ((be = slapi_be_select(sdn)) != NULL) {
        be_suffix = slapi_sdn_get_dn(slapi_be_getsuffix(be, 0));
    }

    /* prepare the argument of filter apply callback: a format and
     * the attribute type that trigger the rewrite
     */
    format = slapi_ch_smprintf("cn=%s,cn=Schema,cn=Configuration,%s", (char *) "%s", (char *) be_suffix);
    arg.attrtype = OBJECTCATEGORY;
    arg.format = format;

    /* Now apply substitute_shortcut on each filter component */
    rc = slapi_filter_apply(clientFilter, substitute_shortcut, &arg, &error_code);
    slapi_ch_free_string(&format);
    if (rc == SLAPI_FILTER_SCAN_NOMORE) {
        return SEARCH_REWRITE_CALLBACK_CONTINUE; /* Let's others rewriter play */
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "adfilter_rewrite_objectCategory", "Could not update the search filter - error %d (%d)\n",
                      rc, error_code);
        return SEARCH_REWRITE_CALLBACK_ERROR; /* operation error */
    }
}