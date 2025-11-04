/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * dyncerts.c - Dynamic Certificates Handler
 *
 */

#include "slap.h"
#include "fe.h"
#include <nss.h>
#include <nss3/pk11pub.h>
#include <nss3/certdb.h>
#include "svrcore.h" 
#include "dyncerts.h" 

#ifdef DEBUG
#define SLAPI_LOG_DYC SLAPI_LOG_INFO
#else
#define SLAPI_LOG_DYC SLAPI_LOG_TRACE
#endif

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct slapdplugin dyncerts_plugin = {0};
static struct dyncerts pdyncerts;

static const char *dyncerts_baseentry_str =
    "dn: " DYNCERTS_SUFFIX "\n"
    "objectclass: top\n"
    "objectclass: nsContainer\n"
    "cn: " DYNCERTS_CN "\n";

static const struct trust_flags_mask trust_flags[] = {
    { CERTDB_VALID_CA, CERTDB_TRUSTED_CA|CERTDB_TRUSTED_CLIENT_CA, 'c' },
    { CERTDB_TERMINAL_RECORD, CERTDB_TRUSTED, 'p' },
    { CERTDB_TRUSTED_CA, 0, 'C' },
    { CERTDB_TRUSTED_CLIENT_CA, 0, 'T' },
    { CERTDB_TRUSTED, 0, 'P' },
    { CERTDB_USER, 0, 'u' },
    { CERTDB_SEND_WARN, 0, 'w' },
    { CERTDB_INVISIBLE_CA, 0, 'I' },
    { CERTDB_GOVT_APPROVED_CA, 0, 'G' },
    { 0 }
};

/* Some forward definitions */
static DCSS *get_entry_list(const Slapi_DN *basesn, int scope, char **attrs);
int dyncerts_apply_cb(const char *nickname, dyc_action_cb_t cb, void *arg, char *errmsg);

/* Alloc a search set */
static DCSS *
ss_new()
{
    return (DCSS *)slapi_ch_calloc(1, sizeof (DCSS));
}

/* Free a parent search set */
static void
ss_destroy(DCSS **pss)
{
    DCSS *ss = *pss;
    if (ss) {
        for (size_t idx=0; idx<ss->nb_entries; idx++) {
            slapi_entry_free(ss->entries[idx]);
        }
        slapi_ch_free((void**)&ss->entries);
    }
    slapi_ch_free((void**)pss);
}

/* Add an entry into search set */
static void
ss_add_entry(DCSS *ss, Slapi_Entry *e)
{
    if (e == NULL) {
        return;
    }
    if (ss->nb_entries >= ss->max_entries) {
        if (ss->max_entries == 0) {
            ss->max_entries = 4;
        } else {
            ss->max_entries *= 2;
        }
        ss->entries = (Slapi_Entry **)slapi_ch_realloc((void*)(ss->entries), (sizeof (Slapi_Entry*)) * ss->max_entries);
    }
    ss->entries[ss->nb_entries++] = e;
}

/* Backend callback (operation not allowed) */
static int
be_unwillingtoperform(Slapi_PBlock *pb)
{
    send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Operation not allowed on Dynamic Certificate Entry", 0, NULL);
    return -1;
}

/* Get instance config data and store them in private data */
static void
read_config_info()
{
    DCSS *ss = ss_new();
    Slapi_PBlock *pb = NULL;
    Slapi_DN sdn = {0};
    Slapi_Entry *e = NULL;
    Slapi_Entry **e2 = NULL;

    /* Store cn=config entry */
    slapi_sdn_init_dn_byref(&sdn, CONFIG_DN1);
    slapi_search_internal_get_entry(&sdn, NULL, &e, plugin_get_default_component_id());
    ss_add_entry(ss, e);
    slapi_sdn_done(&sdn);
    /* Store cn=encryption,cn=config entry */
    slapi_sdn_init_dn_byref(&sdn, CONFIG_DN2);
    slapi_search_internal_get_entry(&sdn, NULL, &e, plugin_get_default_component_id());
    ss_add_entry(ss, e);
    /* Store cn=*,cn=encryption,cn=config active entries */
    pb = slapi_search_internal(CONFIG_DN2, LDAP_SCOPE_ONELEVEL, CONFIG_DN2_FILTER,
                                   NULL, NULL, 0);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e2);
    if (e2 != NULL) {
        while (*e2) {
            ss_add_entry(ss, slapi_entry_dup(*e2++));
        }
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    pdyncerts.config = ss;
}

/* Get a specific config entry by index */
static Slapi_Entry *
get_config_entry(int idx)
{
    if (pdyncerts.config && idx < pdyncerts.config->nb_entries) {
        return pdyncerts.config->entries[idx];
    }
    return NULL;
}

/* Backend callback (freeing search set pair) */
void
dyncerts_search_set_release(void **pss)
{
    DCSS *ss = *pss;
    if (ss) {
        ss_destroy(&ss->pdscc);
        slapi_ch_free((void**)&ss->entries);
        slapi_ch_free(pss);
    }
}

static Slapi_Entry *
dyncerts_find_entry(const Slapi_DN *basedn, int scope, char **attrs, DCSS **be_ss)
{
    Slapi_DN suffix = {0};
    Slapi_Entry *e = NULL;
    DCSS *ss = NULL;

    slapi_sdn_init_dn_byref(&suffix, DYNCERTS_SUFFIX);
    read_config_info();
    if (slapi_sdn_compare(basedn, &suffix) != 0) {
        scope = LDAP_SCOPE_SUBTREE;
    }
    ss =  get_entry_list(&suffix, scope, attrs);
    for(size_t idx = 0; idx<ss->nb_entries; idx++) {
        e = ss->entries[idx];
        if (slapi_sdn_compare(basedn, slapi_entry_get_sdn_const(e)) == 0) {
            break;
        }
        e = NULL;
    }
    ss_destroy(&pdyncerts.config);
    *be_ss = ss;
    return e;
}


/* Backend callback (search operation: generate search set) */
int
dyncerts_search(Slapi_PBlock *pb)
{
    Slapi_Operation *operation = NULL;
    Slapi_Filter *filter = NULL;
    Slapi_DN *basesdn = NULL;
    struct pdyncerts *pdata;
    char *strfilter = NULL;
    char **attrs = NULL;
    int attrsonly = 0;
    int scope = 0;
    DCSS *be_ss = NULL;
    DCSS *ss = NULL;
    int estimate = 0;
    Slapi_Entry *e = NULL;
    int rc = -1;

    /*
     * Get private information created in the init routine.
     * Also get the parameters of the search operation. These come
     * more or less directly from the client.
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdata) < 0 ||
        slapi_pblock_get(pb, SLAPI_OPERATION, &operation) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &basesdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &filter) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &strfilter) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs) < 0 ||
        slapi_pblock_get(pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly) < 0) {
        slapi_send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return (-1);
    }
    if (slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL)) {
        /*
         *  - acl discovery
         */
        if (attrs && strcasecmp(attrs[0], "aci")==0) {
            /*
             * aci discovery internal search ==>
             * lets compute contatiner entry
             */
            scope = LDAP_SCOPE_BASE;
        } else {
             /*
              * Bypass internal operations targetting all backends.
              *  Like the roles & cos discovery
              */
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ss);
            slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate);
            return 0;
        }
    }

    pthread_mutex_lock(&mutex);
    /* Let first build the list of all entries */
    e = dyncerts_find_entry(basesdn, scope, attrs, &be_ss);
    ss = ss_new();
    ss->pdscc = be_ss;

    /* Make sure that the base entry exists */
    if (!e) {
        slapi_send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, NULL, 0, NULL);
        slapi_log_err(SLAPI_LOG_PLUGIN, "dyncerts_search", "node %s was not found\n",
                      slapi_sdn_get_dn(basesdn));
        goto fail;
    }
    /* Then filter the matching entries */
    for(size_t idx = 0; idx<be_ss->nb_entries; idx++) {
        e = be_ss->entries[idx];
        if (slapi_sdn_scope_test(slapi_entry_get_sdn_const(e), basesdn, scope) &&
            slapi_vattr_filter_test(pb, e, filter, PR_TRUE) == 0) {
            ss_add_entry(ss, e);
        }
    }
    estimate = ss->nb_entries;
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, ss);
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE, &estimate);
    rc = 0;
fail:
    if (rc != 0) {
        dyncerts_search_set_release((void**)&ss);
    }
    pthread_mutex_unlock(&mutex);
    return rc;
}

/* Backend callback (get next search entryt from search set) */
int
dyncerts_next_search_entry(Slapi_PBlock *pb)
{
    DCSS *ss = NULL;
    Slapi_Entry *e = NULL;
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ss);

    /* no entries to return */
    if (ss == NULL) {
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, NULL);
        return 0;
    }

    if (ss->cur_entry < ss->nb_entries) {
        e = ss->entries[ss->cur_entry++];
    }
    slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, e);

    if (e == NULL) {
        /* we reached the end of the list */
        pagedresults_set_search_result_pb(pb, NULL, 0);
        dyncerts_search_set_release((void**)&ss);
        slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_SET, NULL);
    }
    return 0;
}

/* Backend callback (get previous search entry from search set) */
void
dyncerts_prev_search_results(void *vp)
{
    Slapi_PBlock *pb = (Slapi_PBlock *)vp;
    DCSS *ss = NULL;
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &ss);
    if (ss->cur_entry > 0) {
        ss->cur_entry--;
    }
}

/* Backend callback (release private data context) */
int
dyncerts_cleanup(Slapi_PBlock *pb)
{
    struct dyncerts *pdata = NULL;

    pthread_mutex_lock(&mutex);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdata);
    if (pdata) {
        while (pdata->sockets) {
            struct sock_elem *se = pdata->sockets->next;
            slapi_ch_free((void **)&pdata->sockets);
            pdata->sockets = se;
        }
        memset(pdata, 0, sizeof *pdata);
        pdata = NULL;
    }
    slapi_pblock_set(pb, SLAPI_PLUGIN_PRIVATE, pdata);
    pthread_mutex_unlock(&mutex);
    return 0;
}

/* Convert PRBool to string */
static char *
boolv(PRBool v)
{
    return (v == PR_TRUE) ? "TRUE" : "FALSE";
}

/* Convert PRTime to Zulu timestamp string */
static void
timev(PRTime t, char *str, size_t strsize)
{
    PRExplodedTime expt = {0};
    PR_ExplodeTime(t, PR_GMTParameters, &expt);
    PR_snprintf(str, strsize, "%04d%02d%02d%02d%02d%02dZ",
                expt.tm_year,
                expt.tm_month,
                expt.tm_mday,
                expt.tm_hour,
                expt.tm_min,
                expt.tm_sec);
}

/* Convert trust flags to string */
static char *
add_trust_flags(char *str, char *endstr, unsigned int flags)
{
    for (size_t i=0; trust_flags[i].flags && str<endstr; i++) {
        if ((flags & (trust_flags[i].flags | trust_flags[i].mask)) == trust_flags[i].flags) {
            *str++ = trust_flags[i].c;
        }
    }
    *str = 0;
    return str;
}

/* Convert certificate trust to string */
static void
trustv(CERTCertificate *cert, char *str, size_t strsize)
{
    char *endstr = str + strsize -1; /* -1 to keep space for \0 */
    CERTCertTrust trust = {0};
    CERT_GetCertTrust(cert, &trust);

    str = add_trust_flags(str, endstr, trust.sslFlags);
    if (str < endstr) {
        *str++ = ',';
        str = add_trust_flags(str, endstr, trust.emailFlags);
    }
    if (str < endstr) {
        *str++ = ',';
        str = add_trust_flags(str, endstr, trust.objectSigningFlags);
    }
}

/* Convert a SECItem to a value */
static void
secitemv(SECItem *si, Slapi_Value *v)
{
    struct berval bv = {0};
    if (si) {
        bv.bv_val = (char*)(si->data);
        bv.bv_len = si->len;
    }
    slapi_value_init_berval(v, &bv);
}

/* Determine if a certificate has a private key */
static PRBool
has_privkey(CERTCertificate *cert)
{
    SECKEYPrivateKey *privkey = PK11_FindKeyByDERCert(cert->slot, cert, NULL);
    return privkey ? PR_TRUE : PR_FALSE;
}

/* Determine the key algorythm */
static char *
key_algo(CERTCertificate *cert)
{
    SECKEYPublicKey *pubkey = CERT_ExtractPublicKey(cert);
    if (pubkey == NULL) {
        return "None";
    }
			switch (pubkey->keyType) {
        case rsaKey: return "RSA";
        case ecKey: return "EC";
        default: return "UNKNOWN";
    }
}

static char *
secitem2hex(SECItem *si)
{
    size_t len = 0;
    unsigned char *data = NULL;
    if (si) {
        len = si->len;
        data = (unsigned char *)(si->data);
    }
    char *pt = slapi_ch_malloc(si->len*2+1);
    pt[0] = 0;
    for (size_t i=0; i<len; i++) {
        sprintf(&pt[2*i], "%02x", data[i]);
    }
    return pt;
}



/* Helper function to determine if slotname is the internal slot */
static bool
is_internal_slot(const char *slotname)
{
    return ( strcasecmp(slotname, "internal (software)") == 0 ||
             strcasecmp(slotname, "Internal (Software) Token") == 0);
}

/* Determine if a certificate is the server certificate */
static PRBool
is_servercert(CERTCertificate *cert)
{
    const char *slotname = PK11_GetTokenName(cert->slot);
    const char *certname = cert->nickname;
    Slapi_Entry *e = NULL;
    /* Check iof certificate match one of the familly definition */
    for (size_t i=FIRST_FAMILY_CONFIG_ENTRY_IDX; (e=get_config_entry(i)); i++) {
        const char *ename = slapi_entry_attr_get_ref(e, "nsSSLPersonalitySSL");
        const char *eslot = slapi_entry_attr_get_ref(e, "nsSSLToken");
        if (strcasecmp(ename, certname) != 0) {
            continue;
        }
        if (is_internal_slot(eslot) && is_internal_slot(slotname)) {
            return PR_TRUE;
        }
        if (strcasecmp(eslot, slotname) == 0) {
            return PR_TRUE;
        }
    }
    return PR_FALSE;
}

static const char *
cert_token(CERTCertificate *cert)
{
    return PK11_GetTokenName(cert->slot);
}

/* Get certificate db pin */
static inline void * __attribute__((always_inline))
_get_pw(const char *token)
{
    struct sock_elem *se = pdyncerts.sockets;
    char *pw = NULL;
#ifdef WITH_SYSTEMD
    SVRCOREStdSystemdPinObj *StdPinObj = (SVRCOREStdSystemdPinObj *)SVRCORE_GetRegisteredPinObj();
#else
    SVRCOREStdPinObj *StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
#endif
    SVRCOREError err = SVRCORE_StdPinGetPin(&pw, StdPinObj, token);

    if (err != SVRCORE_Success || pw == NULL) {
        for (;pw == NULL && se; se=se->next) {
            pw = SSL_RevealPinArg(se->pr_sock);
        }
    }
    return pw;
}

/* Add certificate verification status to the entry */
static void
verify_cert(Slapi_Entry *e, CERTCertificate *cert, const char *attrname)
{
    SECCertificateUsage returnedUsages;
    SECCertificateUsage usage = 0;
    char *val = NULL;

    if (strcmp(slapi_entry_attr_get_ref(e, DYCATTR_ISSRVCERT), "TRUE") == 0) {
        usage = certificateUsageSSLServer;
    } else if (strcmp(slapi_entry_attr_get_ref(e, DYCATTR_ISCA), "TRUE") == 0) {
        usage = certificateUsageAnyCA;
    } else {
        usage = certificateUsageSSLClient;
    }

    int rv = CERT_VerifyCertificateNow(cert->dbhandle, cert, PR_TRUE,
                                       usage, _get_pw(cert_token(cert)), &returnedUsages);

    if (rv != SECSuccess) {
        int errorCode = PR_GetError();
        val = slapi_ch_smprintf("FAILURE: error %d - %s", errorCode,
                                slapd_pr_strerror(errorCode));
    } else {
        val = slapi_ch_strdup("SUCCESS");
	}
    slapi_entry_add_string(e, attrname, val);
    slapi_ch_free_string(&val);
}

/*
 * A callback for the Subject Alternate Name supported encoding table. Used for:
 *   - OtherName:
 *     EDIPartyName ::= SEQUENCE { nameAssigner [0] OPTIONAL, partyName [1] }
 *   or
 *   - EDIPartyName with optional nameAssigner
 *     OtherName ::= SEQUENCE { type-id OID, value [0] EXPLICIT ANY }
 */
static ber_tag_t
sanse_bv2 (BerElement *ber, general_name_value_t *val)
{
    val->nbvals = 2;
    return ber_scanf(ber, "{oo}", &val->vals[0], &val->vals[1]);
}

/*
 * A callback for the Subject Alternate Name supported encoding table. Used for:
 *   - EDIPartyName without optional nameAssigner
 *     OtherName ::= SEQUENCE { type-id OID, value [0] EXPLICIT ANY }
 */
static ber_tag_t
sanse_edi (BerElement *ber, general_name_value_t *val)
{
    val->nbvals = 1;
    return ber_scanf(ber, "{o}", &val->vals[0]);
}

/*
 * A callback for the Subject Alternate Name supported encoding table. Used for:
 *   - SomeName ::= OCTETSTRING
 */
static ber_tag_t
sanse_bv1 (BerElement *ber, general_name_value_t *val)
{
    val->nbvals = 1;
    return ber_scanf(ber, "o", &val->vals[0]);
}

/*
 * A callback for the Subject Alternate Name supported encoding table. Used for:
 *   - Skipping unknown format
 */
static ber_tag_t
sanse_bv0 (BerElement *ber, general_name_value_t *val)
{
    ber_tag_t tag = 0;
    val->nbvals = 0;
    return ber_scanf(ber, "T", &tag);
}

/* Free data within general_name_value_t */
static void
general_name_value_free(general_name_value_t *val)
{
    for (size_t i=0; i<val->nbvals; i++) {
        if (val->vals[i].bv_val) {
            ber_memfree(val->vals[i].bv_val);
            val->vals[i].bv_val = NULL;
        }
        val->vals[i].bv_len = 0;
    }
    val->nbvals = 0;
}

/*
 * Decode Alternate Subject Names (rfc 5280)
 * and call callback for each names
 */
static void
walk_subject_alt_names(BerValue *bv, gnw_cb_t cb, void *arg)
{
    BerElement *ber = ber_init(bv);
    char *cookie = NULL;
    size_t len = 0;
    ber_tag_t tag = 0;
    /* The subject alternate name supported encoding table */
    ber_tag_t (*sanset[])(BerElement*, general_name_value_t*) = {
        sanse_bv2,
        sanse_edi,
        sanse_bv1,
        sanse_bv0,
        NULL
    };

    /* Iterate through GeneralName entries */
    for (tag=ber_first_element(ber, &len, &cookie);
         tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
         tag=ber_next_element(ber, &len, cookie)) {
        general_name_value_t val = {0};
        ber_tag_t rc = LBER_ERROR;
        /*
         * Determine how the GeneralName is encoded
         *  (by walking supported encoding table)
         */
        for(size_t i=0; sanset[i] && rc == LBER_ERROR; i++) {
            /* ber state is modified by failed decoding attempt
             * ==> lets duplicate it
             */
            BerElement *b = ber_dup(ber);
            rc = sanset[i](b, &val);
            general_name_value_free(&val);
            if (rc != LBER_ERROR) {
                /*
                 * The right encoding has been found
                 * ==> Now we can decode the main buffer
                 *     and call the callback
                 */
                rc = sanset[i](ber, &val);
                cb(tag & 0x1F, &val, arg);
            }
            general_name_value_free(&val);
            ber_free(b, 0);
        }
    }
    ber_free(ber, 1);
}

/* Callback used to store subject alternate name in the entry */
static void
gnw_cb(general_name_type_t gnt, const general_name_value_t *val, void *arg)
{
    struct altname_ctx *ctx = arg;
    char *str = NULL;

    switch (val->nbvals) {
        case 1:
            if (gnt == gnt_ipaddress) {
                /* Should convert the address to string */
            } else {
                size_t len = val->vals[0].bv_len;
                str = malloc(len+1);
                memcpy(str, val->vals[0].bv_val, len);
                str[len] = 0;
            }
            break;
        case 2:
            {
                size_t len0 = val->vals[0].bv_len;
                size_t len1 = val->vals[1].bv_len;
                str = malloc(len0+len1+2);
                memcpy(str, val->vals[0].bv_val, len0);
                str[len0] = '=';
                memcpy(str+len0+1, val->vals[1].bv_val, len1);
                str[len0+1+len1] = 0;
            }
            break;
    }
    if (str) {
        slapi_entry_add_string(ctx->e, ctx->attrname, str);
        slapi_ch_free_string(&str);
    }
}

/* Store subject alternate name in the entry */
static void
store_alt_name(Slapi_Entry *e, CERTCertificate *cert, const char *attrname)
{
    SECItem si = {0};
    int rv = CERT_FindCertExtension(cert, SEC_OID_X509_SUBJECT_ALT_NAME, &si);
    struct altname_ctx ctx = { attrname, e };
    BerValue bv = {0};
    if (rv == 0) {
        bv.bv_len = si.len;
        bv.bv_val = (char*) si.data;
        walk_subject_alt_names(&bv, gnw_cb, &ctx);
    }
}

/* Get certificate nickname (including token name). Must be freed by caller */
static char *
get_cert_nickname(CERTCertificate *cert)
{
    char *token = PK11_GetTokenName(cert->slot);
    if (is_internal_slot(token)) {
        return slapi_ch_strdup(cert->nickname);
    }
    return slapi_ch_smprintf("%s:%s", token, cert->nickname);
}

/* Generate the certificate entry */
static Slapi_Entry *
dyncerts_cert2entry(CERTCertificate *cert)
{
    char *nickname = get_cert_nickname(cert);
    char *dn = slapi_ch_smprintf("cn=%s,%s", nickname, DYNCERTS_SUFFIX);
    Slapi_Entry *e = slapi_entry_alloc();
    char tmpbuff[32] = "";
    PRTime notBefore = 0;
    PRTime notAfter = 0;
    Slapi_Value tmpv = {0};
    char *tmpstr = NULL;

    slapi_entry_init(e, dn, NULL);
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", "extensibleobject");
    slapi_entry_add_string(e, DYCATTR_NICKNAME, nickname);
    slapi_entry_add_string(e, DYCATTR_SUBJECT, cert->subjectName);
    slapi_entry_add_string(e, DYCATTR_ISSUER, cert->issuerName);
    slapi_entry_add_string(e, DYCATTR_ISCA, boolv(CERT_IsCACert(cert, NULL)));
    slapi_entry_add_string(e, DYCATTR_ISROOTCA, boolv(cert->isRoot));
    slapi_entry_add_string(e, DYCATTR_HASPKEY, boolv(has_privkey(cert)));
    slapi_entry_add_string(e, DYCATTR_ISSRVCERT, boolv(is_servercert(cert)));
    slapi_entry_add_string(e, DYCATTR_KALGO, key_algo(cert));
    CERT_GetCertTimes(cert, &notBefore, &notAfter);
    timev(notBefore, tmpbuff, sizeof tmpbuff);
    slapi_entry_add_string(e, DYCATTR_NBEFORE, tmpbuff);
    timev(notAfter, tmpbuff, sizeof tmpbuff);
    slapi_entry_add_string(e, DYCATTR_NAFTER, tmpbuff);
    trustv(cert, tmpbuff, sizeof tmpbuff);
    slapi_entry_add_string(e, DYCATTR_TRUST, tmpbuff);
    COND_STR(e, DYCATTR_TYPE, "SSL CLIENT", cert->nsCertType & NS_CERT_TYPE_SSL_CLIENT);
    COND_STR(e, DYCATTR_TYPE, "SSL SERVER", cert->nsCertType & NS_CERT_TYPE_SSL_SERVER);
    COND_STR(e, DYCATTR_TYPE, "EMAIL", cert->nsCertType & NS_CERT_TYPE_EMAIL);
    COND_STR(e, DYCATTR_TYPE, "OBJECT SIGNING", cert->nsCertType & NS_CERT_TYPE_OBJECT_SIGNING);
    COND_STR(e, DYCATTR_TYPE, "SSL CA", cert->nsCertType & NS_CERT_TYPE_SSL_CA);
    COND_STR(e, DYCATTR_TYPE, "EMAIL CA", cert->nsCertType & NS_CERT_TYPE_EMAIL_CA);
    COND_STR(e, DYCATTR_TYPE, "OBJECT SIGNING CA", cert->nsCertType & NS_CERT_TYPE_OBJECT_SIGNING_CA);
    slapi_entry_add_string(e, DYCATTR_TOKEN, PK11_GetTokenName(cert->slot));
    secitemv(&cert->derCert, &tmpv);
    slapi_entry_add_value(e, DYCATTR_CERTDER, &tmpv);
    tmpstr = secitem2hex(&cert->serialNumber);
    slapi_entry_add_string(e, DYCATTR_SN, tmpstr);
    slapi_ch_free_string(&tmpstr);
    verify_cert(e, cert, DYCATTR_VERIF);
    store_alt_name(e, cert, DYCATTR_ALTNAME);
    slapi_ch_free_string(&nickname);
    return e;
}

/* Backend callback (unbind operation) */
int
dyncerts_unbind(Slapi_PBlock *pb __attribute__((unused)))
{
    return 0;
}

/* PK11_TraverseSlotCerts callback that adds a certificate entry in the parent search set */
static SECStatus
dyncerts_list_cert_cb(CERTCertificate *cert, SECItem *sitem, void *arg)
{
    /* slapi_log_err(SLAPI_LOG_INFO, "dyncerts_list_cert_cb", "See certificate %s\n", cert->nickname); */
    ss_add_entry(arg, dyncerts_cert2entry(cert));
    return 0;
}

/* Generate the parent search set */
static DCSS *
get_entry_list(const Slapi_DN *basesn, int scope, char **attrs)
{
    DCSS *ss = ss_new();
    int str2entry_flags = SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES |
                          SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF;
    Slapi_Entry *e = slapi_str2entry((char*)dyncerts_baseentry_str, str2entry_flags);

    /* Handle aci if requested */
    if (charray_inlist(attrs, "aci") || charray_inlist(attrs, "+") ||
        charray_inlist(attrs, "*")) {
        const struct slapi_value **va = NULL;
        Slapi_Entry *ce = get_config_entry(ENCRYPTION_CONFIG_ENTRY_IDX);
        if (ce) {
            va = slapi_entry_attr_get_valuearray(ce, "aci");
        }
        if (va) {
            slapi_entry_attr_replace_sv(e, "aci", (struct slapi_value **)va);
        }
    }
    ss_add_entry(ss, e);
    if (LDAP_SCOPE_BASE == scope) {
        /* Bypass getting cert list if only looking for the container */
        Slapi_DN sdn;
        slapi_sdn_init_dn_byref(&sdn, DYNCERTS_SUFFIX);
        if (slapi_sdn_compare(&sdn, slapi_entry_get_sdn_const(e)) == 0) {
            return ss;
        }
    }

    if (slapd_nss_is_initialized()) {
        (void) PK11_TraverseSlotCerts(dyncerts_list_cert_cb, ss, NULL);
    }
    return ss;
}

/* Create the dynamic certificate backend */
Slapi_Backend *
dyncert_init_be()
{
    pthread_mutex_lock(&mutex);
    if (!pdyncerts.be) {
        Slapi_Backend *be = slapi_be_new(DYNCERTS_BETYPE, DYNCERTS_BENAME, 1 /* Private */, 0 /* Do Not Log Changes */);
        Slapi_DN dn = {0};
        pdyncerts.be = be;

        be->be_database = &dyncerts_plugin;
        be->be_database->plg_private = &pdyncerts;
        be->be_database->plg_bind = &be_unwillingtoperform;
        be->be_database->plg_unbind = &dyncerts_unbind;
        be->be_database->plg_search = &dyncerts_search;
        be->be_database->plg_next_search_entry = &dyncerts_next_search_entry;
        be->be_database->plg_search_results_release = &dyncerts_search_set_release;
        be->be_database->plg_prev_search_results = &dyncerts_prev_search_results;
        be->be_database->plg_compare = &be_unwillingtoperform;
/*
        be->be_database->plg_modify = &dyncerts_modify;
        be->be_database->plg_modrdn = &be_unwillingtoperform;
        be->be_database->plg_add = &dyncerts_add;
        be->be_database->plg_delete = &dyncerts_delete;
*/
        be->be_database->plg_abandon = &be_unwillingtoperform;
        be->be_database->plg_cleanup = &dyncerts_cleanup;
        /* All the other function pointers default to NULL */

        slapi_sdn_init_ndn_byref(&dn, DYNCERTS_SUFFIX);
        be_addsuffix(be, &dn);
        slapi_sdn_done(&dn);
    }
    pthread_mutex_unlock(&mutex);
    return pdyncerts.be;
}

/*
 * Store socket fd and associated PRFileDesc in private data
 * to fetch the password
 */
void
dyncerts_register_socket(int sock, PRFileDesc *pr_sock)
{
    struct sock_elem *se;
    pthread_mutex_lock(&mutex);
    se = pdyncerts.sockets;
    /* If port already exist, lets reuse its slot */
    for (;se; se=se->next) {
        if (se->sock == sock) {
            break;
        }
    }
    if (!se) {
        /* Else alloc a new one */
        se = (struct sock_elem *) slapi_ch_calloc(1, sizeof(struct sock_elem));
        se->next = pdyncerts.sockets;
        pdyncerts.sockets = se;
    }
    se->sock = sock;
    se->pr_sock = pr_sock;
    pthread_mutex_unlock(&mutex);
}
