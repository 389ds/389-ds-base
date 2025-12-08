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


/*
 * dsDynamicCertificateSwitchInProgress is always seen as FALSE
 * Because accept and listening thread are blocked
 * when it is switched to TRUE (so it cannot be queried)
 */
static const char *dyncerts_baseentry_str =
    "dn: " DYNCERTS_SUFFIX "\n"
    "objectclass: top\n"
    "objectclass: nsContainer\n"
    "objectclass: extensibleObject\n"
    "dsDynamicCertificateSwitchInProgress: FALSE\n"
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
static void dyncert_nickname_free(Nickname_t *n);

/* Alloc a search set */
static DCSS *
ss_new(void)
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
read_config_info(void)
{
    if (pdyncerts.config == NULL) {
        DCSS *ss = ss_new();
        Slapi_PBlock *pb = NULL;
        Slapi_DN sdn = {0};
        Slapi_Entry *e = NULL;
        Slapi_Entry **e2 = NULL;

        /* Store cn=config entry */
        slapi_sdn_init_dn_byref(&sdn, CONFIG_DN1);
        pthread_mutex_lock(&mutex);
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
        slapi_sdn_done(&sdn);
        pdyncerts.config = ss;
    }
}

/* Free private data instance config data */
static void
free_config_info(void)
{
    if (pdyncerts.config != NULL) {
        ss_destroy(&pdyncerts.config);
        pthread_mutex_unlock(&mutex);
    }
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
    Slapi_Entry *e = NULL;
    DCSS *ss = NULL;

    read_config_info();
    if (slapi_sdn_compare(basedn, &pdyncerts.suffix_sdn) != 0) {
        scope = LDAP_SCOPE_SUBTREE;
    }
    ss =  get_entry_list(&pdyncerts.suffix_sdn, scope, attrs);
    for(size_t idx = 0; idx<ss->nb_entries; idx++) {
        e = ss->entries[idx];
        if (slapi_sdn_compare(basedn, slapi_entry_get_sdn_const(e)) == 0) {
            break;
        }
        e = NULL;
    }
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
    free_config_info();
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
    if (ss && ss->cur_entry > 0) {
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
        slapi_sdn_done(&pdyncerts.suffix_sdn);
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
    PRBool res = privkey ? PR_TRUE : PR_FALSE;
    SECKEY_DestroyPrivateKey(privkey);
    return res;
}

/* Determine the key algorythm */
static char *
key_algo(CERTCertificate *cert)
{
    SECKEYPublicKey *pubkey = CERT_ExtractPublicKey(cert);\
    CK_KEY_TYPE keyType = 0;
    if (pubkey == NULL) {
        return "None";
    }
    keyType = pubkey->keyType;
    SECKEY_DestroyPublicKey(pubkey);
	switch (keyType) {
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
    char *pt = slapi_ch_malloc(len*2+1);
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
    return ( strcasecmp(slotname, INTERNAL_SLOTNAME1) == 0 ||
             strcasecmp(slotname, INTERNAL_SLOTNAME2) == 0);
}

/* Determine if a certificate is the server certificate */
static PRBool
is_servercert_int(const char *slotname, const char *nickname)
{
    Slapi_Entry *e = NULL;
    if (slotname == NULL) {
        slotname = INTERNAL_SLOTNAME1;
    }
    /* Check iof certificate match one of the familly definition */
    for (size_t i=FIRST_FAMILY_CONFIG_ENTRY_IDX; (e=get_config_entry(i)); i++) {
        const char *ename = slapi_entry_attr_get_ref(e, "nsSSLPersonalitySSL");
        const char *eslot = slapi_entry_attr_get_ref(e, "nsSSLToken");
        if (!ename || !eslot) {
            continue;
        }
        if (strcasecmp(ename, nickname) != 0) {
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

/* Determine if a certificate is the server certificate */
static PRBool
is_servercert(CERTCertificate *cert)
{
    const char *slotname = PK11_GetTokenName(cert->slot);
    const char *nickname = cert->nickname;
    return is_servercert_int(slotname, nickname);
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
    /* mutex is held in all backend operation callbacks */
    struct sock_elem *se = pdyncerts.sockets;
    char *pw = NULL;
    SVRCOREStdPinObj *StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
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
    char *pw = _get_pw(cert_token(cert));

    if (strcmp(slapi_entry_attr_get_ref(e, DYCATTR_ISSRVCERT), "TRUE") == 0) {
        usage = certificateUsageSSLServer;
    } else if (strcmp(slapi_entry_attr_get_ref(e, DYCATTR_ISCA), "TRUE") == 0) {
        usage = certificateUsageAnyCA;
    } else {
        usage = certificateUsageSSLClient;
    }

    int rv = CERT_VerifyCertificateNow(cert->dbhandle, cert, PR_TRUE,
                                       usage, pw, &returnedUsages);
    slapi_ch_free_string(&pw);

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
                str = slapi_ch_malloc(len+1);
                memcpy(str, val->vals[0].bv_val, len);
                str[len] = 0;
            }
            break;
        case 2:
            {
                size_t len0 = val->vals[0].bv_len;
                size_t len1 = val->vals[1].bv_len;
                str = slapi_ch_malloc(len0+len1+2);
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
    SECITEM_FreeItem(&si, PR_FALSE);
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
    char tmpbuff[TRUST_SIZE] = "";
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
    value_done(&tmpv);
    slapi_entry_add_value(e, DYCATTR_CERTDER, &tmpv);
    tmpstr = secitem2hex(&cert->serialNumber);
    slapi_entry_add_string(e, DYCATTR_SN, tmpstr);
    slapi_ch_free_string(&tmpstr);
    verify_cert(e, cert, DYCATTR_VERIF);
    store_alt_name(e, cert, DYCATTR_ALTNAME);
    slapi_ch_free_string(&nickname);
    return e;
}

/* Free data alloced by nss_add_cert_and_key within CertCtx_t*/
static void
dyncerts_free_nss_add_cert_and_key(CertCtx_t *ctx)
{
    if (ctx->cert) {
        CERT_DestroyCertificate(ctx->cert);
        ctx->cert = NULL;
    }
    if (ctx->privkey) {
        if (ctx->ldaprc) {
            PK11_DeleteTokenPrivateKey(ctx->privkey, PR_FALSE);
        } else {
            SECKEY_DestroyPrivateKey(ctx->privkey);
        }
        ctx->privkey = NULL;
    }
}


/* Free data within CertCtx_t */
void
dyncerts_cert_ctx_done(CertCtx_t *ctx)
{
    if (ctx->dercert.data) {
         slapi_ch_free((void**)&ctx->dercert.data);
         ctx->dercert.data = NULL;
    }
    if (ctx->derpkey.data) {
        slapi_ch_free((void**)&ctx->derpkey.data);
        ctx->derpkey.data = NULL;
    }
    dyncert_nickname_free(&ctx->n);
    slapi_ch_free_string(&ctx->trust);
    ctx->force = false;
    if (ctx->slot) {
        PK11_FreeSlot(ctx->slot);
        ctx->slot = NULL;
    }
    dyncerts_free_nss_add_cert_and_key(ctx);
    ctx->ldaprc = 0;
}

/* Split nicklname into nickname + tokenname */
int
dyncert_resolve_token(CertCtx_t *ctx)
{
    char *del = NULL;
    if (ctx->n.nickname == 0) {
        ERRMSG(ctx, LDAP_OBJECT_CLASS_VIOLATION, "%s attribute is missing.", DYCATTR_NICKNAME);
    }
    ctx->internal_token = (ctx->n.token == NULL);
    del = strchr(ctx->n.nickname, ':');
    if (del) {
        ERRMSG(ctx, LDAP_UNWILLING_TO_PERFORM, "%s attribute should not contain any : except for the token name.", DYCATTR_NICKNAME);
    }
    if (ctx->internal_token) {
        ctx->slot = slapd_pk11_getInternalKeySlot();
        ctx->n.token = PK11_GetTokenName(ctx->slot);
    } else {
        ctx->slot = PK11_FindSlotByName(ctx->n.token);
        if (!ctx->slot) {
            ERRMSG(ctx, LDAP_UNWILLING_TO_PERFORM, "Cannot find token %s", ctx->n.token);
        }
    }

    return 0;
}

/*
 * Add a certificate and optionnaly its private key in NSS db
 *
 * CertCtx_t input:
 *    - nickname
 *    - dercert
 *    - derpkey (optionnal)
 *    - trust (optionnal)
 *    - force (ignore verification failure)
 *    - errmsg
 * CertCtx_t output:
 *    - primary (Is the primary server certificate)
 *    - trust
 *    - cert
 *    - ldaprc
 *    - errmsg
 */
static SECStatus
nss_add_cert_and_key(CertCtx_t *ctx, bool verifyOnly)
{
#define CHECK_ERR(msg)    if (rv != SECSuccess) { errmsg = msg; goto done; }
    SECStatus rv = SECFailure;
    CERTCertificate *cert = ctx->cert;
    SECKEYPrivateKey *privkey = NULL;
    SECKEYPublicKey *pubkey = NULL;
    SECCertificateUsage returnedUsages = 0;
    SECCertificateUsage usage = 0;
    CERTCertTrust trust = {0};
    const char *errmsg = NULL;
    char *pw = _get_pw(ctx->n.token);

    if (PK11_NeedLogin(ctx->slot)) {
        rv = PK11_Authenticate(ctx->slot, PR_TRUE, pw);
        CHECK_ERR("Failed to authenticate to token")
    }
    cert = CERT_DecodeCertFromPackage((char *)ctx->dercert.data, ctx->dercert.len);
    ctx->cert = cert;
    if (cert == NULL) {
        errmsg = "Failed to decode certificate";
        goto done;
    }
    if (cert->isperm && !(verifyOnly && strcasecmp(ctx->n.nickname, cert->nickname) == 0)) {
        /* Certificate already exists so NSS will not add it again
         * and we are not doing a modify operation that replace the certificate
         * by itself
         */
        rv = SECFailure;
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf((ctx)->errmsg, SLAPI_DSE_RETURNTEXT_SIZE, "Confict with certificate %s",
                    cert->nickname);
        goto done2;
    }
    ctx->primary = is_servercert_int(ctx->n.token, ctx->n.nickname);
    if (!ctx->trust) {
        ctx->trust = ",,";
        if (cert->nsCertType & NS_CERT_TYPE_SSL_CA) {
            if (ctx->primary) {
                ctx->trust = "CTu,u,u";
            } else {
                ctx->trust = "CT,,";
            }
        } else if (ctx->cert->nsCertType & (NS_CERT_TYPE_SSL_CLIENT|NS_CERT_TYPE_SSL_SERVER)) {
            ctx->trust = "u,u,u";
        }
        ctx->trust = slapi_ch_strdup(ctx->trust);
    }
    rv = CERT_DecodeTrustString(&trust, ctx->trust);
    CHECK_ERR("Failed to decode the trust string");
    if (ctx->derpkey.data) {
        /* Import the private key (PKCS#8 format) */
        rv = PK11_ImportDERPrivateKeyInfoAndReturnKey(
            ctx->slot,
            &ctx->derpkey,
            NULL,              /* nickname */
            NULL,              /* publicValue */
            PR_TRUE,           /* isPerm */
            PR_TRUE,           /* isPrivate */
            KU_ALL,            /* key usage */
            &privkey,
            NULL               /* wincx */
        );
        CHECK_ERR("Failed to import private key")
    }
    pubkey = CERT_ExtractPublicKey(cert);
    if (!pubkey) {
        rv = SECFailure;
        CHECK_ERR("Failed to extract public key from certificate");
    }
    if (cert->nsCertType & NS_CERT_TYPE_SSL_CA) {
        usage = certificateUsageAnyCA;
    } else {
        usage = certificateUsageSSLServer;
    }
    rv = CERT_VerifyCertificateNow(cert->dbhandle, cert, PR_TRUE,
                                   usage, pw, &returnedUsages);
    if (!ctx->force) {
        CHECK_ERR("Failed to extract public key from certificate");
    }
    rv = SECSuccess;
    if (!verifyOnly) {
        rv = PK11_ImportCert(ctx->slot, cert, CK_INVALID_HANDLE, ctx->n.nickname, PR_FALSE);
        CHECK_ERR("Failed to import certificate");
        rv = CERT_ChangeCertTrust(CERT_GetDefaultCertDB(), cert, &trust);
        CHECK_ERR("Failed to set certificate trust")
    }
    rv = SECSuccess;
done:
    if (rv) {
        PRErrorCode err = PR_GetError();
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        if (ctx->errmsg) {
            PR_snprintf((ctx)->errmsg, SLAPI_DSE_RETURNTEXT_SIZE, "%s: %d - %s",
                        errmsg, err, slapd_pr_strerror(err));
        }
    }
done2:
    ctx->privkey = privkey;
    if (pubkey) {
        SECKEY_DestroyPublicKey(pubkey);
    }
    if (verifyOnly) {
        dyncerts_free_nss_add_cert_and_key(ctx);
    }
    slapi_ch_free_string(&pw);
    return rv;
}

/* Tell daemon and ssl layers that certificate must be refresh */
static void
dyncert_refresh_certs()
{
    /* TBD in  phase3 */
}

/* Import the certificate and the key */
int
dyncerts_import_cert_and_key(CertCtx_t *ctx, bool verifyOnly)
{
    Slapi_Entry *e = NULL;
    SECStatus rv = 0;
    DCSS *ss = NULL;

    if (dyncert_resolve_token(ctx)) {
        return ctx->ldaprc;
    }
    if (ctx->dercert.data == 0) {
        ERRMSG(ctx, LDAP_OBJECT_CLASS_VIOLATION, "%s attribute is missing.", DYCATTR_CERTDER);
    }
    if (!ctx->slot) {
        slapi_log_err(SLAPI_LOG_ERR, "dyncerts_decode_cert", "Failed to get NSS internal key slot.\n");
        ERRMSG(ctx, LDAP_UNWILLING_TO_PERFORM, "Failed to get NSS internal key slot.");
    }
    rv = nss_add_cert_and_key(ctx, verifyOnly);
    if (rv != SECSuccess) {
        return ctx->ldaprc;
    }
    if (verifyOnly) {
        return 0;
    }
    e = dyncerts_find_entry(ctx->sdn, LDAP_SCOPE_BASE, NULL, &ss);
    ss_destroy(&ss);
    if (!e) {
        slapi_log_err(SLAPI_LOG_ERR, "dyncerts_add",
                      "Failed to add certificate %s (entry not found after import).\n",
                      ctx->n.fullnickname);
        ERRMSG(ctx, LDAP_UNWILLING_TO_PERFORM,
               "Failed to add certificate %s (entry not found after import).\n",
               ctx->n.fullnickname);
    }
    if (ctx->primary) {
        dyncert_refresh_certs();
    }

    return 0;
}

/* Tell if dn is the container entry */
static bool
dyncert_is_suffix(Slapi_DN *sdn)
{
    return slapi_sdn_compare(sdn, &pdyncerts.suffix_sdn) == 0;
}

/* Release nickname buffer */
static void
dyncert_nickname_free(Nickname_t *n)
{
    if (n->data != n->buf) {
        slapi_ch_free_string(&n->data);
    }
    memset(n, 0, sizeof *n);
}

#if 0
/* Fill nickname from token and nickname */
static void
dyncert_nickname_from_token_and_name(Nickname_t *n, const char *token, const char *nickname)
{
    size_t nlen = strlen(nickname);
    size_t tlen = (token && !is_internal_slot(token)) ? strlen(token) : 0;
    size_t len = tlen+nlen+1;
    if (2*len+2 >= sizeof n->buf) {
        n->data = slapi_ch_malloc(2*len+2);
    } else {
        n->data = n->buf;
    }
    if (tlen == 0) {
        strcpy(n->data, nickname);
        n->nickname = n->fullnickname = n->data;
        n->token = NULL;
    } else {
        n->token = n->data;
        n->nickname = n->token + tlen +1;
        n->fullnickname = n->nickname + nlen + 1;
        strcpy(n->token, token);
        strcpy(n->nickname, nickname);
        strcpy(n->fullnickname, token);
        strcpy(n->fullnickname + tlen + 1, nickname);
        n->fullnickname[tlen] = ':';
    }
}
#endif

/* Fill nickname from full_nickname */
static void
dyncert_nickname_from_full_nickname(Nickname_t *n, const char *fullnickname)
{
    size_t len = strlen(fullnickname);
    size_t len2 = strcspn(fullnickname, ":");
    if (2*len+2 >= sizeof n->buf) {
        n->data = slapi_ch_malloc(2*len+2);
    } else {
        n->data = n->buf;
    }
    strcpy(n->data, fullnickname);
    strcpy(n->data+len+1, fullnickname);
    n->fullnickname = n->data+len+1;
    if (len2<len) {
        n->token = n->data;
        n->token[len2] = 0;
        n->nickname = n->token + len2+1;
        if (is_internal_slot(n->token)) {
            n->token = NULL;
        }
    } else {
        n->token = NULL;
        n->nickname = n->data;
    }
}

/* Extract the certificate nickname from the dn */
static const char *
dyncert_nickname_from_dn(Nickname_t *n, Slapi_DN *sdn)
{
    Slapi_DN parent = {0};
    Slapi_RDN rdn = {0};
    const char *pt = NULL;

    n->nickname = NULL;
    slapi_sdn_init(&parent);
    slapi_sdn_get_parent(sdn, &parent);
    slapi_sdn_get_rdn(sdn, &rdn);
    pt = slapi_rdn_get_rdn(&rdn);
    if (dyncert_is_suffix(&parent) &&
        !slapi_rdn_is_multivalued(&rdn) &&
        strncasecmp(pt, "cn=", 3) == 0) {
        dyncert_nickname_from_full_nickname(n, pt+3);
    }
    slapi_sdn_done(&parent);
    slapi_rdn_done(&rdn);
    return n->nickname;
}

static SECItem
slapi_entry_attr_get_secitem(const Slapi_Entry *e, const char *type)
{
    const struct berval *bv = NULL;
    Slapi_Attr *attr = NULL;
    Slapi_Value *v = NULL;
    SECItem si = {0};

    if (slapi_entry_attr_find(e, type, &attr) == 0) {
        slapi_attr_first_value(attr, &v);
        bv = slapi_value_get_berval(v);
        si.data = (unsigned char *) slapi_ch_malloc(bv->bv_len);
        memcpy(si.data, bv->bv_val, bv->bv_len);
        si.len = bv->bv_len;
    }
    return si;
}

int
dyncerts_check_entry(Slapi_PBlock *pb, Slapi_Entry *e, Nickname_t *n, char *errmsg, bool needcert)
{
    int rc = LDAP_SUCCESS;
    Slapi_Attr *a = NULL;
    char *allowedattrs[] = {
        DYCATTR_NICKNAME,
        DYCATTR_CERTDER,
        DYCATTR_PKEYDER,
        DYCATTR_TRUST,
        DYCATTR_FORCE,
        NULL };
    char *skipped_attrs[] = {
        SLAPI_ATTR_OBJECTCLASS,
        SLAPI_ATTR_UNIQUEID,
        "creatorsName",
        "modifiersName",
        "createTimestamp",
        "modifyTimestamp",
        NULL };

    /* Check that dn has the expected format */
    if (!n->nickname) {
        PR_snprintf(errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Invalid DN for a Dynamic certificate entry."
                    " DN should be like cn=nickname,%s", DYNCERTS_SUFFIX);
        return LDAP_NAMING_VIOLATION;
    }
    /*
     * Check to make sure the entry passes the schema check
     */
    if (slapi_entry_schema_check(pb, e) != 0) {
        char *err;
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &err);
        if (err && err[0]) {
            PL_strncpyz(errmsg, err, SLAPI_DSE_RETURNTEXT_SIZE);
        }
        return LDAP_OBJECT_CLASS_VIOLATION;
    }
    /* Check if the attribute values in the entry obey the syntaxes */
    if (slapi_entry_syntax_check(pb, e, 0) != 0) {
        char *err;
        slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &err);
        if (err && err[0]) {
            PL_strncpyz(errmsg, err, SLAPI_DSE_RETURNTEXT_SIZE);
        }
        return LDAP_INVALID_SYNTAX;
    }
    /* Check that only expected attributes are present. */
    slapi_entry_first_attr(e, &a);
    for (; a != NULL; slapi_entry_next_attr(e, a, &a)) {
        Slapi_Value *v = NULL;
        int i = 0;
        if (charray_inlist(skipped_attrs, a->a_type)) {
            continue;
        }
        i = slapi_attr_first_value(a, &v);
        if (i == -1 || slapi_attr_next_value(a, i, &v) != -1) {
            PR_snprintf(errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Attribute %s should be single valued", a->a_type);
            return LDAP_UNWILLING_TO_PERFORM;
        }
        if (!charray_inlist(allowedattrs, a->a_type)) {
            PR_snprintf(errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Unexpected attribute: %s", a->a_type);
            return LDAP_OBJECT_CLASS_VIOLATION;
        }
    }
    /* Check that nsDynamicCertificateDER attribute is present */
    if ((needcert || slapi_entry_attr_get_ref(e, DYCATTR_PKEYDER)) &&
        !slapi_entry_attr_get_ref(e, DYCATTR_CERTDER)) {
        PR_snprintf(errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Attribute %s should be present", DYCATTR_CERTDER);
        return LDAP_UNWILLING_TO_PERFORM;
    }
    return rc;
}

/* Store the entry in NSS db */
int
dyncerts_import_entry(Slapi_Entry *e, char *errmsg, bool verifyOnly)
{
    int rc = LDAP_SUCCESS;
    CertCtx_t ctx = {0};

    dyncert_nickname_from_full_nickname(&ctx.n, slapi_entry_attr_get_ref(e, DYCATTR_NICKNAME));
    ctx.dercert = slapi_entry_attr_get_secitem(e, DYCATTR_CERTDER);
    ctx.derpkey = slapi_entry_attr_get_secitem(e, DYCATTR_PKEYDER);
    ctx.trust = slapi_entry_attr_get_charptr(e, DYCATTR_TRUST);
    ctx.force = (slapi_entry_attr_get_bool(e, DYCATTR_FORCE) == PR_TRUE);
    ctx.errmsg = errmsg;
    ctx.sdn = (Slapi_DN*) slapi_entry_get_sdn_const(e);
    rc = dyncerts_import_cert_and_key(&ctx, verifyOnly);
    dyncerts_cert_ctx_done(&ctx);
    return rc;
}

/* Backend callback (add operation) */
int
dyncerts_add(Slapi_PBlock *pb)
{
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    struct dyncerts *pdcerts = NULL;
    int error = LDAP_SUCCESS;
    int rc = LDAP_SUCCESS;
    Slapi_Entry *e = NULL;
    const char *dn = "???";
    Slapi_DN *sdn = NULL;
    Nickname_t n = {0};
    DCSS *ss = NULL;

    /*
     * Get the database, the dn and the entry to add
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdcerts) < 0 ||
        slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e) < 0 || (NULL == pdcerts)) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }
    dn = slapi_sdn_get_dn(slapi_entry_get_sdn_const(e));

    /* Check entry attributes */
    (void) dyncert_nickname_from_dn(&n, sdn);
    rc = dyncerts_check_entry(pb, e, &n, returntext, true);
    if (rc) {
        e = NULL; /* caller will free upon error */
        goto done;
    }
    /* Check that entry does not exist */
    if (dyncerts_find_entry(sdn, LDAP_SCOPE_BASE, NULL, &ss)) {
        e = NULL; /* caller will free upon error */
        rc = LDAP_ALREADY_EXISTS;
        goto done;
    }
    rc = dyncerts_import_entry(e, returntext, false);
done:
    /* make sure OPRETURN and RESULT_CODE are set */
    slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &error);
    if (rc) {
        if (!error) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        }
        slapi_log_err(SLAPI_LOG_DYC, "dyncerts_add",
                      "Add operation of entry %s failed: rc=%d : %s\n",
                      dn, rc, returntext);
    }
    ss_destroy(&ss);
    free_config_info();
    dyncert_nickname_free(&n);
    slapi_send_ldap_result(pb, rc, NULL, returntext[0] ? returntext : NULL, 0, NULL);
    /* The frontend does not free the added entry, so we should do it now */
    if (e) {
        slapi_entry_free(e);
    }
    slapi_pblock_set(pb, SLAPI_ADD_ENTRY, NULL);
    return rc;
}

/* Create pseudo entry to add from modifiers */
static int
dyncerts_mods2entry(Slapi_PBlock *pb, Slapi_DN *sdn, LDAPMod **mods, Nickname_t *n, char *errmsg, Slapi_Entry **pte)
{
    const char *nickname = dyncert_nickname_from_dn(n, sdn);
    char *dn = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
    Slapi_Entry *e = *pte = slapi_entry_alloc();
    const char *val = NULL;
    int rc = LDAP_SUCCESS;

    slapi_entry_init(e, dn, NULL);
    slapi_entry_add_string(e, "objectclass", "top");
    slapi_entry_add_string(e, "objectclass", "extensibleobject");
    if (!nickname) {
        /* Should not happen (unless heap corruption) because we already
         * tested that the modified entry exists
         */
        rc = LDAP_NAMING_VIOLATION;
        goto done;
    }
    slapi_entry_add_string(e, DYCATTR_NICKNAME, nickname);
    rc = slapi_entry_apply_mods(e, mods);
    if (rc) {
        goto done;
    }
    val = slapi_entry_attr_get_charptr(e, DYCATTR_NICKNAME);
    if ((val == NULL) || (strcasecmp(val, nickname) != 0)) {
        /* cn attribute was changed ! */
        rc = LDAP_NAMING_VIOLATION;
        goto done;
    }
    rc = dyncerts_check_entry(pb, e, n, errmsg, false);
done:
    return rc;
}

/* dyncerts_apply_cb callback to change trust flags */
void
dyncert_set_trust_cb(CertCtx_t *ctx)
{
    Slapi_Entry *e = ctx->arg;
    CERTCertTrust trust = {0};
    int rv = 0;

    ctx->trust = slapi_entry_attr_get_charptr(e, DYCATTR_TRUST);
    rv = CERT_DecodeTrustString(&trust, ctx->trust);
    rv = CERT_ChangeCertTrust(ctx->cert->dbhandle, ctx->cert, &trust);
    if (rv != SECSuccess) {
        rv = PR_GetError();
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(ctx->errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Failed to change trust for certificate %s. Error is %d - %s.\n",
                    ctx->n.fullnickname, rv, slapd_pr_strerror(rv));
    }
}

/* dyncerts_apply_cb callback to delete a certificate */
void
dyncert_delete_cb(CertCtx_t *ctx)
{
    int rc = SEC_DeletePermCertificate(ctx->cert);
    if (rc == SECFailure) {
        rc = PR_GetError();
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(ctx->errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Failed to delete certificate %s. Error is %d - %s.\n",
                    ctx->n.fullnickname, rc, slapd_pr_strerror(rc));
    }
}

/* modify a certificate entry */
int
dyncerts_modify_cert(Slapi_PBlock *pb, Slapi_Entry *e, LDAPMod **mods, DCSS *ss, char *errmsg)
{
    Slapi_DN *sdn = (Slapi_DN *) slapi_entry_get_sdn_const(e);
    Slapi_Entry *newe = NULL;
    Nickname_t n = {0};
    int rc = dyncerts_mods2entry(pb, sdn, mods, &n, errmsg, &newe);
    const char *nickname = n.nickname;
    PR_ASSERT(n.nickname);
    if (slapi_entry_attr_get_charptr(newe, DYCATTR_CERTDER)) {
        /* need to change the certificate ==> import the entry */
        rc = dyncerts_import_entry(newe, errmsg, true);
        if (rc == LDAP_SUCCESS) {
            /* Remove current certificate */
            rc = dyncerts_apply_cb(nickname, dyncert_delete_cb, NULL, errmsg);
            /* Then add new one */
            rc = dyncerts_import_entry(newe, errmsg, false);
        }
    } else {
        /* Just change the trust */
        if (!nickname) {
            /* Should not happen (unless heap corruption) because we already
             * tested that the modified entry exists
             */
            rc = LDAP_NAMING_VIOLATION;
            goto done;
        }
        rc = dyncerts_apply_cb(nickname, dyncert_set_trust_cb, newe, errmsg);
    }
done:
    slapi_entry_free(newe);
    dyncert_nickname_free(&n);
    return rc;
}

/* modify the container entry */
int
dyncerts_modify_cont(Slapi_Entry *e, LDAPMod **mods, DCSS *ss, char *errmsg)
{
    Slapi_PBlock *mod_pb = NULL;
    Slapi_DN sdn = {0};
    int rc = LDAP_SUCCESS;
    bool has_aci = false;
    char *allowed_attrs[] = {
        DYCATTR_SWITCH,
        "modifiersName",
        "modifyTimestamp",
        NULL
    };

    rc = slapi_entry_apply_mods(e, mods);
    if (rc) {
        return rc;
    }

    for (size_t i=0; mods[i]; i++) {
        char *attr = mods[i]->mod_type;
        if (strcasecmp(attr, "aci") == 0) {
            has_aci = true;
            continue;
        }
        if (!charray_inlist(allowed_attrs, attr)) {
            PR_snprintf(errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Attribute %s cannot be changed on %s entry", attr, DYNCERTS_SUFFIX);
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }
    if (has_aci) {
        /* Lets replace the aci(s) on cn=encryption,cn=config */
        const Slapi_Value **va = slapi_entry_attr_get_valuearray(e, "aci");
        LDAPMod acimod = { 0 };
        LDAPMod *acimods[2] = { &acimod, NULL };

        slapi_sdn_init_dn_byref(&sdn, CONFIG_DN2);
        valuearray_get_bervalarray((Slapi_Value **)va, &acimod.mod_vals.modv_bvals);
        acimod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        acimod.mod_type = "aci";
        mod_pb = slapi_pblock_new();
        slapi_modify_internal_set_pb_ext(mod_pb, &sdn, acimods, NULL, NULL, plugin_get_default_component_id(), 0);
        slapi_modify_internal_pb(mod_pb);
        slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != LDAP_SUCCESS) {
            char *err;
            slapi_pblock_get(mod_pb, SLAPI_PB_RESULT_TEXT, &err);
            if (err && err[0]) {
                PL_strncpyz(errmsg, err, SLAPI_DSE_RETURNTEXT_SIZE);
            }
        }
        ber_bvecfree(acimod.mod_vals.modv_bvals);
    }
    if (slapi_entry_attr_get_bool(e, DYCATTR_SWITCH)) {
        dyncert_refresh_certs();
    }
    slapi_pblock_destroy(mod_pb);
    slapi_sdn_done(&sdn);
    return rc;
}

/* Backend callback (modify operation) */
int
dyncerts_modify(Slapi_PBlock *pb)
{
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    struct dyncerts *pdcerts = NULL;
    int rc = LDAP_SUCCESS;
    Slapi_Entry *e = NULL;
    LDAPMod **mods = NULL;
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    DCSS *ss = NULL;

    /*
     * Get the database, the dn and the modifiers
     */
    PR_ASSERT(pb);
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdcerts) < 0 ||
        slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &sdn) < 0 ||
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods) < 0 || (NULL == pdcerts)) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    dn = slapi_sdn_get_dn(sdn);
    e = dyncerts_find_entry(sdn, LDAP_SCOPE_BASE, NULL, &ss);
    if (!e) {
        rc = LDAP_NO_SUCH_OBJECT;
        goto done;
    }
    if (e == ss->entries[0]) {
        rc =  dyncerts_modify_cont(e, mods, ss, returntext);
    } else {
        rc =  dyncerts_modify_cert(pb, e, mods, ss, returntext);
    }
done:
    if (rc) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        slapi_log_err(SLAPI_LOG_DYC, "dyncerts_modify",
                      "Modify operation of entry %s failed: rc=%d : %s\n",
                      dn, rc, returntext);
    }
    ss_destroy(&ss);
    free_config_info();
    slapi_send_ldap_result(pb, rc, NULL, returntext[0] ? returntext : NULL, 0, NULL);

    return rc;
}

/* dyncerts_apply_cb/PK11_TraverseSlotCerts callback */
static SECStatus
dyncert_find_cert_cb(CERTCertificate *cert, SECItem *sitem, void *arg)
{
    CertCtx_t *ctx = arg;
    ctx->cert = cert;
    const char *slotname = PK11_GetTokenName(cert->slot);
    if (ctx->internal_token != is_internal_slot(slotname)) {
        /* Wrong slot ! */
        return SECSuccess;
    }
    if (!ctx->internal_token && strcasecmp(ctx->n.token, slotname)) {
        /* Still wrong slot ! */
        return SECSuccess;
    }
    if (strcasecmp(cert->nickname, ctx->n.nickname)==0) {
        ctx->ldaprc = LDAP_SUCCESS;
        ctx->action_cb(ctx);
    }
    return (ctx->ldaprc == LDAP_NO_SUCH_OBJECT) ? SECSuccess: SECFailure;
}

/* Apply a callback on the certificate with the nickname */
int
dyncerts_apply_cb(const char *nickname, dyc_action_cb_t cb, void *arg, char *errmsg)
{
    CertCtx_t ctx = {0};
    int rc = LDAP_SUCCESS;

    dyncert_nickname_from_full_nickname(&ctx.n, nickname);
    ctx.errmsg = errmsg;
    ctx.arg = arg;
    ctx.action_cb = cb;
    ctx.ldaprc = LDAP_NO_SUCH_OBJECT;
    rc = dyncert_resolve_token(&ctx);
    /* Could probably avoid to look in all slots but PK11_TraverseCertsInSlot is private */
    if (rc == 0) {
        (void) PK11_TraverseSlotCerts(dyncert_find_cert_cb, &ctx, NULL);
    }
    rc = ctx.ldaprc;
    dyncerts_cert_ctx_done(&ctx);
    return rc;
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
        Slapi_DN sdn = {0};
        if (slapi_sdn_compare(&pdyncerts.suffix_sdn, slapi_entry_get_sdn_const(e)) == 0) {
            return ss;
        }
        slapi_sdn_done(&sdn);
    }

    if (slapd_nss_is_initialized()) {
        (void) PK11_TraverseSlotCerts(dyncerts_list_cert_cb, ss, NULL);
    }
    return ss;
}

/* Backend callback (delete operation) */
int
dyncerts_delete(Slapi_PBlock *pb)
{
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    struct dyncerts *pdcerts = NULL;
    const char *nickname = NULL;
    int rc = LDAP_SUCCESS;
    Slapi_Entry *e = NULL;
    const char *dn = "???";
    Slapi_DN *sdn = NULL;
    Nickname_t n = {0};
    DCSS *ss = NULL;

    /*
     * Get the backend context and the dn
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdcerts) < 0 ||
        slapi_pblock_get(pb, SLAPI_DELETE_TARGET_SDN, &sdn) < 0 ||
        (pdcerts == NULL)) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }
    /* Check that the entry exists */
    dn = slapi_sdn_get_dn(sdn);
    e = dyncerts_find_entry(sdn, LDAP_SCOPE_BASE, NULL, &ss);
    if (e==NULL) {
        rc = LDAP_NO_SUCH_OBJECT;
        goto done;
    }
    nickname = dyncert_nickname_from_dn(&n, sdn);
    if (!nickname) {
        /* Attempt to delete the container entry */
        rc = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }
    rc = dyncerts_apply_cb(nickname, dyncert_delete_cb, NULL, returntext);

done:
    if (rc) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        slapi_log_err(SLAPI_LOG_DYC, "dyncerts_delete",
                      "Delete operation of entry %s failed: rc=%d : %s\n",
                      dn, rc, returntext);
    }
    ss_destroy(&ss);
    free_config_info();
    slapi_send_ldap_result(pb, rc, NULL, returntext[0] ? returntext : NULL, 0, NULL);
    dyncert_nickname_free(&n);

    return rc;
}

/* Duplicate SECItem content */
static void
copy_secitem(SECItem *to, const SECItem *from)
{
    if (from && from->len>0) {
        to->len = from->len;
        to->data = (unsigned char *) slapi_ch_malloc(from->len);
        memcpy(to->data, from->data, from->len);
    } else {
        to->len = 0;
        to->data = NULL;
    }
}


/* dyncerts_apply_cb callback to modrdn a certificate */
void
dyncert_rename_cb(CertCtx_t *ctx)
{
    const char *new_dn = ctx->arg;
    CertCtx_t new_ctx = {0};
    Slapi_DN sdn_new = {0};
    int rc = 0;

    slapi_sdn_init_dn_byref(&sdn_new, new_dn);

    (void) dyncert_nickname_from_dn(&new_ctx.n, &sdn_new);
    new_ctx.errmsg = ctx->errmsg;
    rc = dyncert_resolve_token(&new_ctx);
    /* Could probably avoid to look in all slots but PK11_TraverseCertsInSlot is private */
    if (rc != 0) {
        ctx->ldaprc = rc;
    }
    if (ctx->internal_token != new_ctx.internal_token) {
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(ctx->errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Cannot use modrdn operation to change the token name");
        goto done;
    }
    if (rc == 0 && !ctx->internal_token &&
        strcasecmp(ctx->n.token, new_ctx.n.token) != 0) {
        ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(ctx->errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Cannot use modrdn operation to change the token name");
        goto done;
    }
    if (rc == 0) {
        /* __PK11_SetCertificateNickname used by certutil cannot be used because
         * it deos not update NSS in memory data.
         * Furthermore adding an already existing certificate leads to increase
         * reference count toward that certificate. So we cannot add the new name
         * before having deleted the old one:
         * ==> There is a risk of loosing the certificate in case of error
         *     after the deletion
         */
        CERTCertificate *cert = ctx->cert;
        SECKEYPrivateKeyInfo *pkeyinfo = PK11_ExportPrivateKeyInfo(cert, NULL);
        int rc = 0;

        if (pkeyinfo) {
            copy_secitem(&new_ctx.derpkey, &pkeyinfo->privateKey);
            SECKEY_DestroyPrivateKeyInfo(pkeyinfo, PR_TRUE);
            pkeyinfo = NULL;
        }
        copy_secitem(&new_ctx.dercert, &cert->derCert);
        new_ctx.trust = slapi_ch_malloc(TRUST_SIZE);
        trustv(cert, new_ctx.trust, TRUST_SIZE);

        rc = SEC_DeletePermCertificate(ctx->cert);
        if (rc == SECFailure) {
            rc = PR_GetError();
            ctx->ldaprc = LDAP_UNWILLING_TO_PERFORM;
            PR_snprintf(ctx->errmsg, SLAPI_DSE_RETURNTEXT_SIZE,
                        "Failed to delete certificate %s. Error is %d - %s.\n",
                        ctx->n.fullnickname, rc, slapd_pr_strerror(rc));
            goto done;
        } else {
            rc = nss_add_cert_and_key(&new_ctx, false);
        }
    }
done:
    dyncerts_cert_ctx_done(&new_ctx);
    slapi_sdn_done(&sdn_new);
}

/* Backend callback (modrdn operation) */
int
dyncerts_rename(Slapi_PBlock *pb)
{
    char returntext[SLAPI_DSE_RETURNTEXT_SIZE] = "";
    struct dyncerts *pdcerts = NULL;
    const char *old_nickname = NULL;
    const char *new_nickname = NULL;
    int rc = LDAP_SUCCESS;
    Slapi_Entry *e = NULL;
    const char *old_dn = "???";
    Slapi_DN *sdn = NULL;
    Slapi_DN *sdn_newsup = NULL;
    Slapi_DN sdn_new = {0};
    DCSS *ss = NULL;
    char *new_rdn = NULL;
    Nickname_t n_old = {0};
    Nickname_t n_new = {0};
    const char *new_sup = NULL;
    const char *new_dn = NULL;

    slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &sdn);
    /*
     * Get the backend context and the dn
     */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &pdcerts) < 0 ||
        slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &sdn) < 0 ||
        (pdcerts == NULL)) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }
    old_dn = slapi_sdn_get_dn(sdn);
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &new_rdn);
    if (new_rdn == NULL) {
        rc = LDAP_OPERATIONS_ERROR;
        goto done;
    }
    slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &sdn_newsup);
    new_sup = slapi_sdn_get_dn(sdn_newsup);
    if (new_sup) {
        new_dn = slapi_ch_smprintf("%s,%s", new_rdn, new_sup);
    } else {
        new_dn = slapi_ch_smprintf("%s,%s", new_rdn, slapi_dn_find_parent(old_dn));
    }
    slapi_sdn_init_dn_passin(&sdn_new, new_dn);
    if (slapi_sdn_compare(&sdn_new, sdn) == 0) {
        /* NOOP */
        rc = LDAP_SUCCESS;
        goto done;
    }
    e = dyncerts_find_entry(sdn, LDAP_SCOPE_BASE, NULL, &ss);
    ss_destroy(&ss);
    if (e == NULL) {
        rc = LDAP_NO_SUCH_OBJECT;
        goto done;
    }
    old_nickname = dyncert_nickname_from_dn(&n_old, sdn);
    if (!old_nickname) {
        /* Attempt to delete the container entry */
        rc = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }
    e = dyncerts_find_entry(&sdn_new, LDAP_SCOPE_BASE, NULL, &ss);
    ss_destroy(&ss);
    if (e != NULL) {
        rc = LDAP_ALREADY_EXISTS;
        goto done;
    }
    new_nickname = dyncert_nickname_from_dn(&n_new, &sdn_new);
    if (!new_nickname) {
        rc = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }
    rc = dyncerts_apply_cb(old_nickname, dyncert_rename_cb, (void*) new_dn, returntext);

done:
    if (rc) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &rc);
        slapi_log_err(SLAPI_LOG_DYC, "dyncerts_delete",
                      "Modrdn operation of entry %s failed: rc=%d : %s\n",
                      old_dn, rc, returntext);
    }
    slapi_sdn_done(&sdn_new);
    free_config_info();
    slapi_send_ldap_result(pb, rc, NULL, returntext[0] ? returntext : NULL, 0, NULL);
    dyncert_nickname_free(&n_old);
    dyncert_nickname_free(&n_new);

    return rc;
}

/* Create the dynamic certificate backend */
Slapi_Backend *
dyncert_init_be()
{
    pthread_mutex_lock(&mutex);
    if (!pdyncerts.be) {
        Slapi_Backend *be = slapi_be_new(DYNCERTS_BETYPE, DYNCERTS_BENAME, 1 /* Private */, 0 /* Do Not Log Changes */);
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
        be->be_database->plg_add = &dyncerts_add;
        be->be_database->plg_modify = &dyncerts_modify;
        be->be_database->plg_modrdn = &dyncerts_rename;
        be->be_database->plg_delete = &dyncerts_delete;
        be->be_database->plg_abandon = &be_unwillingtoperform;
        be->be_database->plg_cleanup = &dyncerts_cleanup;
        /* All the other function pointers default to NULL */

        slapi_sdn_init_dn_byref(&pdyncerts.suffix_sdn, DYNCERTS_SUFFIX);
        be_addsuffix(be, &pdyncerts.suffix_sdn);
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
