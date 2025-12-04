/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2025 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifndef DYNCERT_H_
#define DYNCERT_H_
/*
 * dyncerts.h - Dynamic Certificates Handler
 */

#define DYNCERTS_BENAME "DYNCERTS"
#define DYNCERTS_BETYPE "DSE"

#define CONFIG_DN1 "cn=config"
#define CONFIG_DN2 "cn=encryption,cn=config"
#define CONFIG_DN2_FILTER "(&(objectclass=nsEncryptionModule)(nsSSLActivation=on))"

/* Indexes in config search set */
#define CONFIG_ENTRY_IDX 	            0
#define ENCRYPTION_CONFIG_ENTRY_IDX 	1
#define FIRST_FAMILY_CONFIG_ENTRY_IDX 	2

/* Conditionnaly add an attribute value */
#define COND_STR(e, a, v, c) do { if (c) { slapi_entry_add_string((e), (a), (v)); } } while (false)

/* The Dynamic certificate attribute names */
#define DYCATTR_NICKNAME		 "cn"
#define DYCATTR_PREFIX           "nsDynamicCertificate"
#define DYCATTR_ISSRVCERT        DYCATTR_PREFIX "IsServerCert"
#define DYCATTR_ISROOTCA	     DYCATTR_PREFIX "IsRootCA"
#define DYCATTR_ISCA	         DYCATTR_PREFIX "IsCA"
#define DYCATTR_SUBJECT		     DYCATTR_PREFIX "Subject"
#define DYCATTR_ISSUER		     DYCATTR_PREFIX "Issuer"
#define DYCATTR_HASPKEY		     DYCATTR_PREFIX "HasPrivateKey"
#define DYCATTR_KALGO		     DYCATTR_PREFIX "KeyAlgorithm"
#define DYCATTR_NBEFORE		     DYCATTR_PREFIX "NotBefore"
#define DYCATTR_NAFTER		     DYCATTR_PREFIX "NotAfter"
#define DYCATTR_TRUST		     DYCATTR_PREFIX "TrustFlags"
#define DYCATTR_TYPE		     DYCATTR_PREFIX "Type"
#define DYCATTR_TOKEN		     DYCATTR_PREFIX "TokenName"
#define DYCATTR_CERTDER		     DYCATTR_PREFIX "Der"
#define DYCATTR_PKEYDER		     DYCATTR_PREFIX "PrivateKeyDer"
#define DYCATTR_SN		         DYCATTR_PREFIX "SerialNumber"
#define DYCATTR_VERIF	         DYCATTR_PREFIX "VerificationStatus"
#define DYCATTR_ALTNAME	         DYCATTR_PREFIX "SubjectAltName"
#define DYCATTR_FORCE	         DYCATTR_PREFIX "Force"
#define DYCATTR_SWITCH	         DYCATTR_PREFIX "SwitchInProgress"

#define INTERNAL_SLOTNAME1    "Internal (Software)"
#define INTERNAL_SLOTNAME2    "Internal (Software) Token"

#define ERRMSG(ctx, rc, ...) { \
            (ctx)->ldaprc = (rc); \
            if ((ctx)->errmsg) { \
                PR_snprintf((ctx)->errmsg, SLAPI_DSE_RETURNTEXT_SIZE, __VA_ARGS__); \
            } \
            return (ctx)->ldaprc; \
        }

/* Entry type (container or certificate) used to check dn validity */
typedef enum {
    DYC_ERR,
    DYC_CONT,
    DYC_CERT,
    DYC_BOTH
} dyc_et_t;

struct certctx_str;
typedef void (*dyc_action_cb_t)(struct certctx_str *ctx);

/* Context used to store certificate and private key in NSS db */
typedef struct certctx_str {
    SECItem dercert;
    SECItem derpkey;
    char *fullnickname;
    char *nickname;
    char *token;
    char *trust;
    bool force;
    bool primary;
    bool internal_token;
    Slapi_DN *sdn;
    PK11SlotInfo *slot;
    CERTCertificate *cert;
    SECKEYPrivateKey *privkey;
    char *errmsg; /* SLAPI_DSE_RETURNTEXT_SIZE */
    int ldaprc;
    int verifyrc;
    dyc_action_cb_t action_cb;
    void *arg;
} CertCtx_t;

/*
 * Helper structs used to decode Subject Alternate Name
 */
typedef enum general_name_type {
    gnt_othername, gnt_rfc822name, gnt_dnsname, gnt_x400address, gnt_dirname,
    gnt_edipartyname, gnt_uri, gnt_ipaddress, gnt_registeredid,
} general_name_type_t;

typedef struct {
    BerValue vals[2];
    int nbvals;
} general_name_value_t;

struct altname_ctx {
    const char *attrname;
    Slapi_Entry *e;
};

/* Helper structs used to decode trust flags */
struct trust_flags_mask {
    unsigned int flags;
    unsigned int mask;
    char c;
};

/*
 * Search set struct is used to store a list of entries
 * The different search set are:
 * The whole backend search set
 *      containing the backend container and the certificates entries
 *      It has a NULL pdscc
 * The search search set containing the entrties that matches the search request
 *      The entries are links to the backend entries (so they should not be freed)
 *      Its pdscc point to the backend search set
 * The config search set containing cn=config and nsEncryptionModule entries
 */
typedef struct dyncerts_search_set {
    size_t max_entries;
    size_t nb_entries;
    Slapi_Entry **entries;
    size_t cur_entry;
    struct dyncerts_search_set *pdscc;
} DCSS;

/* List of ldaps sockets and associated PRFileDesc */
struct sock_elem {
    struct sock_elem *next;
    int sock;
    PRFileDesc *pr_sock;
};

/* The backend private data */
struct dyncerts {
    Slapi_Backend *be;
    DCSS *config;
    struct sock_elem *sockets;  /* Secure sockets (needed to get the pin) */
};

/* Alternate Name Decoder Callback */
typedef void (*gnw_cb_t)(general_name_type_t gnt, const general_name_value_t *val, void *arg);

#endif  /* DYNCERT_H_ */
