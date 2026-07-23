/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2026 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Per-operation equality lookup tables for large OR filters.
 *
 * The per-entry filter test walks every OR branch for every candidate
 * entry, and each branch re-normalizes the entry's values.  For a large
 * OR of equality tests on one attribute the walk is inverted: normalize
 * the entry's values once and look them up in a sorted table of the
 * branches' normalized assertion values.  A hit is re-verified with the
 * same access check and match call the walk would have made, so the
 * table only selects the branch to test (see vattr_test_filter_or_lookup
 * in filterentry.c).
 *
 * Tables are built only on the backend's per-search filter dups, after
 * slapi_filter_normalize(PR_TRUE): one owner, no locking, freed with the
 * node in slapi_filter_free.  Only syntaxes whose equality means "the
 * normalized bytes are equal" qualify; everything else stays on the
 * classic walk.
 */

#include "slap.h"
#include "slapi-private.h"

/*
 * Minimum same-type equality branches before a table is built.  Small
 * ORs (SSSD sends a handful of branches) keep the classic walk.
 */
#define FILTER_OR_LOOKUP_THRESHOLD 16

/* Mirrors FILTER_OPTIMISE_DEPTH_LIMIT (filter.c) for the annotate walk. */
#define FILTER_OR_LOOKUP_DEPTH_LIMIT 256

/*
 * Syntaxes whose equality compares the normalized forms byte-wise.
 * Octetstring is absent on purpose: it is the default for attribute
 * types unknown to schema.  Also absent: generalizedTime, boolean,
 * bitString, and nameAndOptionalUID.
 */
static const char *const or_lookup_syntax_oids[] = {
    DIRSTRING_SYNTAX_OID,
    IA5STRING_SYNTAX_OID,
    INTEGER_SYNTAX_OID,
    NUMERICSTRING_SYNTAX_OID,
    TELEPHONE_SYNTAX_OID,
    DN_SYNTAX_OID,
    NULL
};

/*
 * Equality rules implemented by the in-tree string family.  A custom or
 * collation rule may normalize differently, so anything else declines.
 */
static const char *const or_lookup_mr_oids[] = {
    "2.5.13.1",                  /* distinguishedNameMatch */
    "2.5.13.2",                  /* caseIgnoreMatch */
    "2.5.13.5",                  /* caseExactMatch */
    "2.5.13.8",                  /* numericStringMatch */
    "2.5.13.14",                 /* integerMatch */
    "2.5.13.20",                 /* telephoneNumberMatch */
    "1.3.6.1.4.1.1466.109.114.1", /* caseExactIA5Match */
    "1.3.6.1.4.1.1466.109.114.2", /* caseIgnoreIA5Match */
    NULL
};

struct or_lookup_family {
    const char *type; /* borrowed from the first branch of this family */
    size_t eligible;
    size_t usable;
    size_t first_ord;
    int32_t is_dn;
    int32_t supported;
};

static int
or_lookup_family_cmp(const void *ap, const void *bp)
{
    const struct or_lookup_family *a = (const struct or_lookup_family *)ap;
    const struct or_lookup_family *b = (const struct or_lookup_family *)bp;

    if (a->usable != b->usable) {
        return (a->usable > b->usable) ? -1 : 1;
    }
    if (a->first_ord != b->first_ord) {
        return (a->first_ord < b->first_ord) ? -1 : 1;
    }
    return 0;
}

static int32_t
or_lookup_oid_in_list(const char *const *list, const char *oid)
{
    if (oid == NULL) {
        return 0;
    }
    for (; *list; list++) {
        if (strcmp(*list, oid) == 0) {
            return 1;
        }
    }
    return 0;
}

static int32_t
or_lookup_mr_in_list(char **mr_names)
{
    const char *const *oid;

    if (mr_names == NULL) {
        return 0;
    }
    for (oid = or_lookup_mr_oids; *oid; oid++) {
        char **name;
        for (name = mr_names; *name; name++) {
            if (strcmp(*name, *oid) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * A branch may join a table for type T iff it is an equality test on
 * exactly T, without attribute options, whose value was normalized and
 * passed the schema check.
 */
static int32_t
or_lookup_child_hashable(const struct slapi_filter *fc, const char *type)
{
    if (fc->f_choice != LDAP_FILTER_EQUALITY) {
        return 0;
    }
    if (fc->f_flags & (SLAPI_FILTER_INVALID_ATTR_UNDEFINE | SLAPI_FILTER_INVALID_ATTR_WARN)) {
        return 0;
    }
    if ((fc->f_flags & SLAPI_FILTER_NORMALIZED_VALUE) == 0) {
        return 0;
    }
    if (fc->f_ava.ava_type == NULL || fc->f_ava.ava_value.bv_val == NULL) {
        return 0;
    }
    if (strchr(fc->f_ava.ava_type, ';') != NULL) {
        return 0;
    }
    return (strcasecmp(fc->f_ava.ava_type, type) == 0);
}

/*
 * filter_normalize_ava sets SLAPI_FILTER_NORMALIZED_VALUE even when DN
 * normalization failed, leaving raw bytes under the flag.  The classic
 * walk compares those raw against raw; a table probe never could.
 * Accept a key only if it is a fixed point of slapi_dn_normalize_case_ext.
 */
static int32_t
or_lookup_dn_key_valid(const char *key, size_t key_len)
{
    char *copy;
    char *dest = NULL;
    size_t dlen = 0;
    int rc;
    int32_t valid = 0;

    copy = slapi_ch_malloc(key_len + 1);
    memcpy(copy, key, key_len);
    copy[key_len] = '\0';

    rc = slapi_dn_normalize_case_ext(copy, key_len, &dest, &dlen);
    if (rc == 0) {
        /* normalized in place; not NUL terminated */
        valid = (dlen == key_len && memcmp(dest, key, key_len) == 0);
    } else if (rc > 0) {
        valid = (dlen == key_len && memcmp(dest, key, key_len) == 0);
        slapi_ch_free_string(&dest);
    }
    slapi_ch_free_string(&copy);
    return valid;
}

/* The type qualifies when its syntax and equality rule are both allowlisted. */
static int32_t
or_lookup_type_supported(const char *type, int32_t *is_dn)
{
    Slapi_Attr sattr = {0};
    const char *syntax_oid = NULL;
    int32_t supported = 0;

    *is_dn = 0;
    slapi_attr_init(&sattr, type);
    if (sattr.a_plugin == NULL) {
        slapi_attr_init_syntax(&sattr);
    }
    if (sattr.a_plugin != NULL) {
        syntax_oid = sattr.a_plugin->plg_syntax_oid;
    }
    if (!or_lookup_oid_in_list(or_lookup_syntax_oids, syntax_oid)) {
        goto done;
    }
    if (sattr.a_mr_eq_plugin != NULL &&
        !or_lookup_mr_in_list(sattr.a_mr_eq_plugin->plg_mr_names)) {
        goto done;
    }
    *is_dn = (strcmp(syntax_oid, DN_SYNTAX_OID) == 0);
    supported = 1;

done:
    attr_done(&sattr);
    return supported;
}

/* Return the table-key length, or zero when the branch stays on the walk. */
static size_t
or_lookup_child_key_len(const struct slapi_filter *fc, const char *type,
                        int32_t is_dn)
{
    size_t key_len;

    if (!or_lookup_child_hashable(fc, type)) {
        return 0;
    }

    /* filter_normalize_ava can shrink the value without refreshing bv_len. */
    key_len = strlen(fc->f_ava.ava_value.bv_val);
    if (key_len == 0 ||
        (is_dn && !or_lookup_dn_key_valid(fc->f_ava.ava_value.bv_val, key_len))) {
        return 0;
    }
    return key_len;
}

/* Sort by key (length, then bytes); equal keys by list position. */
static int
or_lookup_key_cmp(const void *ap, const void *bp)
{
    const struct slapi_filter_or_key *a = (const struct slapi_filter_or_key *)ap;
    const struct slapi_filter_or_key *b = (const struct slapi_filter_or_key *)bp;
    int rc;

    if (a->ok_len != b->ok_len) {
        return (a->ok_len < b->ok_len) ? -1 : 1;
    }
    rc = memcmp(a->ok_key, b->ok_key, a->ok_len);
    if (rc != 0) {
        return rc;
    }
    if (a->ok_ord != b->ok_ord) {
        return (a->ok_ord < b->ok_ord) ? -1 : 1;
    }
    return 0;
}

/* bsearch comparator: probe key against a table slot (keys are unique). */
static int
or_lookup_probe_cmp(const void *keyp, const void *slotp)
{
    const struct berval *key = (const struct berval *)keyp;
    const struct slapi_filter_or_key *slot = (const struct slapi_filter_or_key *)slotp;

    if ((size_t)key->bv_len != slot->ok_len) {
        return ((size_t)key->bv_len < slot->ok_len) ? -1 : 1;
    }
    return memcmp(key->bv_val, slot->ok_key, slot->ok_len);
}

struct slapi_filter *
filter_or_lookup_probe(const struct slapi_filter_or_lookup *ol, const struct berval *key)
{
    const struct slapi_filter_or_key *slot;

    slot = (const struct slapi_filter_or_key *)bsearch(key, ol->ol_tab, ol->ol_tab_len,
                                                       sizeof(struct slapi_filter_or_key),
                                                       or_lookup_probe_cmp);
    return slot ? slot->ok_branch : NULL;
}

void
filter_or_lookup_free(struct slapi_filter_or_lookup **ol)
{
    if (ol == NULL || *ol == NULL) {
        return;
    }
    slapi_ch_free((void **)&(*ol)->ol_tab);
    slapi_ch_free((void **)&(*ol)->ol_rest);
    slapi_ch_free_string(&(*ol)->ol_type);
    slapi_ch_free((void **)ol);
}

/*
 * Build the table for one OR node and one type.  Returns the member
 * count before dedup (the k the walk would have paid), or 0.
 */
static int32_t
or_lookup_annotate_type(struct slapi_filter *f, const char *type, int32_t is_dn,
                        int32_t boolean_ctx)
{
    struct slapi_filter *fc;
    struct slapi_filter_or_key *tab = NULL;
    struct slapi_filter **rest = NULL;
    struct slapi_filter_or_lookup *ol = NULL;
    size_t n_children = 0;
    size_t tab_n = 0;
    size_t rest_n = 0;
    size_t uniq;
    size_t i;
    uint32_t ord = 0;

    for (fc = f->f_or; fc != NULL; fc = fc->f_next) {
        n_children++;
    }

    tab = (struct slapi_filter_or_key *)slapi_ch_calloc(n_children, sizeof(*tab));
    rest = (struct slapi_filter **)slapi_ch_calloc(n_children, sizeof(*rest));

    for (fc = f->f_or; fc != NULL; fc = fc->f_next, ord++) {
        size_t key_len = or_lookup_child_key_len(fc, type, is_dn);

        if (key_len > 0) {
            tab[tab_n].ok_key = fc->f_ava.ava_value.bv_val;
            tab[tab_n].ok_len = key_len;
            tab[tab_n].ok_branch = fc;
            tab[tab_n].ok_ord = ord;
            tab_n++;
        } else {
            rest[rest_n++] = fc;
        }
    }

    /* The preflight count and this pass must agree. */
    if (tab_n < FILTER_OR_LOOKUP_THRESHOLD) {
        slapi_ch_free((void **)&tab);
        slapi_ch_free((void **)&rest);
        return 0;
    }

    qsort(tab, tab_n, sizeof(*tab), or_lookup_key_cmp);
    /* Collapse duplicate keys.  Same type and value means the same
     * outcome, so which branch wins is unobservable. */
    uniq = 0;
    for (i = 1; i < tab_n; i++) {
        if (tab[uniq].ok_len != tab[i].ok_len ||
            memcmp(tab[uniq].ok_key, tab[i].ok_key, tab[i].ok_len) != 0) {
            uniq++;
            if (uniq != i) {
                tab[uniq] = tab[i];
            }
        }
    }

    ol = (struct slapi_filter_or_lookup *)slapi_ch_calloc(1, sizeof(*ol));
    ol->ol_type = slapi_ch_strdup(type);
    ol->ol_type_is_dn = is_dn;
    ol->ol_boolean_ctx = boolean_ctx;
    ol->ol_tab = tab;
    ol->ol_tab_len = uniq + 1;
    ol->ol_rest = rest;
    ol->ol_rest_len = rest_n;
    f->f_or_lookup = ol;

    return (int32_t)tab_n;
}

static int32_t
or_lookup_annotate(struct slapi_filter *f, int32_t boolean_ctx)
{
    struct slapi_filter *fc;
    struct or_lookup_family *families = NULL;
    struct or_lookup_family *family;
    PLHashTable *by_type = NULL;
    size_t n_children = 0;
    size_t n_families = 0;
    size_t ord = 0;
    size_t i;
    int32_t k = 0;

    if (f->f_or_lookup != NULL) {
        return 0;
    }

    for (fc = f->f_or; fc != NULL; fc = fc->f_next) {
        n_children++;
    }
    if (n_children < FILTER_OR_LOOKUP_THRESHOLD) {
        return 0;
    }

    /* families[] keeps first-occurrence order; the hash is lookup only. */
    families = (struct or_lookup_family *)slapi_ch_calloc(n_children,
                                                           sizeof(*families));
    by_type = PL_NewHashTable((PRUint32)n_children,
                              hashNocaseString,
                              hashNocaseCompare,
                              PL_CompareValues, 0, 0);
    if (by_type == NULL) {
        goto done;
    }

    for (fc = f->f_or; fc != NULL; fc = fc->f_next, ord++) {
        const char *type;

        if (fc->f_choice != LDAP_FILTER_EQUALITY || fc->f_ava.ava_type == NULL ||
            strchr(fc->f_ava.ava_type, ';') != NULL) {
            continue;
        }

        type = fc->f_ava.ava_type;
        family = (struct or_lookup_family *)PL_HashTableLookup(by_type, type);
        if (family == NULL) {
            family = &families[n_families];
            family->type = type;
            family->first_ord = ord;
            if (PL_HashTableAdd(by_type, family->type, family) == NULL) {
                goto done;
            }
            n_families++;
        }
        if (or_lookup_child_hashable(fc, family->type)) {
            family->eligible++;
        }
    }

    /* Resolve support, then count exact usable members per family. */
    for (i = 0; i < n_families; i++) {
        if (families[i].eligible < FILTER_OR_LOOKUP_THRESHOLD) {
            continue;
        }
        families[i].supported = or_lookup_type_supported(families[i].type,
                                                         &families[i].is_dn);
    }
    for (fc = f->f_or; fc != NULL; fc = fc->f_next) {
        if (fc->f_choice != LDAP_FILTER_EQUALITY || fc->f_ava.ava_type == NULL ||
            strchr(fc->f_ava.ava_type, ';') != NULL) {
            continue;
        }
        family = (struct or_lookup_family *)PL_HashTableLookup(by_type,
                                                               fc->f_ava.ava_type);
        if (family != NULL && family->supported &&
            or_lookup_child_key_len(fc, family->type, family->is_dn) > 0) {
            family->usable++;
        }
    }

    PL_HashTableDestroy(by_type);
    by_type = NULL;
    qsort(families, n_families, sizeof(*families), or_lookup_family_cmp);

    for (i = 0;
         i < n_families && families[i].usable >= FILTER_OR_LOOKUP_THRESHOLD;
         i++) {
        k = or_lookup_annotate_type(f, families[i].type, families[i].is_dn,
                                    boolean_ctx);
        if (k > 0) {
            break;
        }
    }

done:
    if (by_type != NULL) {
        PL_HashTableDestroy(by_type);
    }
    slapi_ch_free((void **)&families);
    return k;
}

static int32_t
or_lookup_build_recurse(struct slapi_filter *f, int32_t depth, int32_t *largest,
                        int32_t boolean_ctx)
{
    struct slapi_filter *fc;
    int32_t count = 0;
    int32_t k;

    if (f == NULL || depth >= FILTER_OR_LOOKUP_DEPTH_LIMIT) {
        return 0;
    }

    switch (f->f_choice) {
    case LDAP_FILTER_OR:
        k = or_lookup_annotate(f, boolean_ctx);
        if (k > 0) {
            count++;
            if (k > *largest) {
                *largest = k;
            }
        }
    /* FALLTHROUGH */
    case LDAP_FILTER_AND:
        for (fc = f->f_list; fc != NULL; fc = fc->f_next) {
            count += or_lookup_build_recurse(fc, depth + 1, largest, boolean_ctx);
        }
        break;
    case LDAP_FILTER_NOT:
        /* Below a NOT the -1/undefined distinction is observable again. */
        for (fc = f->f_list; fc != NULL; fc = fc->f_next) {
            count += or_lookup_build_recurse(fc, depth + 1, largest, 0);
        }
        break;
    default:
        break;
    }
    return count;
}

/*
 * Annotate every qualifying OR node under f.  Returns the number of
 * annotated nodes; *largest gets the biggest member count.  Caller must
 * own f exclusively.  boolean_ctx means the operation reads the filter
 * test as plain match/non-match (not VLV); cleared while under a NOT.
 */
int32_t
filter_or_lookup_build(struct slapi_filter *f, int32_t *largest, int32_t boolean_ctx)
{
    if (!config_get_enable_or_filter_lookup()) {
        return 0;
    }
    return or_lookup_build_recurse(f, 0, largest, boolean_ctx);
}
