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


/* value.c - routines for dealing with values */

#undef DEBUG /* disable counters */
#include <prcountr.h>
#include "slap.h"
#include "slapi-private.h"

/*
 * Functions needed when a berval is embedded in a struct or
 * allocated on the stack rather than the heap.
 */

static void
ber_bvdone(struct berval *bvp)
{
    if (bvp == NULL)
        return;

    slapi_ch_free_string(&bvp->bv_val);
    bvp->bv_len = 0;

    return;
}

static void
ber_bvcpy(struct berval *bvd, const struct berval *bvs)
{
    size_t len;

    if (bvd == NULL || bvs == NULL)
        return;

    len = bvs->bv_len;
    bvd->bv_val = slapi_ch_malloc(len + 1);
    bvd->bv_len = len;
    memcpy(bvd->bv_val, bvs->bv_val, len);
    bvd->bv_val[len] = '\0';

    return;
}

void
slapi_ber_bvdone(struct berval *bvp)
{
    ber_bvdone(bvp);
}

void
slapi_ber_bvcpy(struct berval *bvd, const struct berval *bvs)
{
    ber_bvcpy(bvd, bvs);
}

/* <=========================== Slapi_Value ==========================> */

#ifdef VALUE_DEBUG
static void value_dump(const Slapi_Value *value, const char *text);
#define VALUE_DUMP(value, name) value_dump(value, name)
#else
#define VALUE_DUMP(value, name) ((void)0)
#endif

static int counters_created = 0;
PR_DEFINE_COUNTER(slapi_value_counter_created);
PR_DEFINE_COUNTER(slapi_value_counter_deleted);
PR_DEFINE_COUNTER(slapi_value_counter_exist);

Slapi_Value *
slapi_value_new()
{
    return value_new(NULL, CSN_TYPE_NONE, NULL);
}

Slapi_Value *
slapi_value_new_berval(const struct berval *bval)
{
    return value_new(bval, CSN_TYPE_NONE, NULL);
}

Slapi_Value *
slapi_value_new_value(const Slapi_Value *v)
{
    return slapi_value_dup(v);
}

Slapi_Value *
slapi_value_new_string(const char *s)
{
    Slapi_Value *v = value_new(NULL, CSN_TYPE_UNKNOWN, NULL);
    slapi_value_set_string(v, s);
    return v;
}

Slapi_Value *
slapi_value_new_string_passin(char *s)
{
    Slapi_Value *v = value_new(NULL, CSN_TYPE_UNKNOWN, NULL);
    slapi_value_set_string_passin(v, s);
    return v;
}

Slapi_Value *
slapi_value_init(Slapi_Value *v)
{
    return value_init(v, NULL, CSN_TYPE_NONE, NULL);
}

Slapi_Value *
slapi_value_init_berval(Slapi_Value *v, struct berval *bval)
{
    return value_init(v, bval, CSN_TYPE_NONE, NULL);
}

Slapi_Value *
slapi_value_init_string(Slapi_Value *v, const char *s)
{
    value_init(v, NULL, CSN_TYPE_UNKNOWN, NULL);
    slapi_value_set_string(v, s);
    return v;
}

Slapi_Value *
slapi_value_init_string_passin(Slapi_Value *v, char *s)
{
    value_init(v, NULL, CSN_TYPE_UNKNOWN, NULL);
    slapi_value_set_string_passin(v, s);
    return v;
}

Slapi_Value *
slapi_value_dup(const Slapi_Value *v)
{
    Slapi_Value *newvalue = value_new(&v->bv, CSN_TYPE_UNKNOWN, NULL);
    newvalue->v_csnset = csnset_dup(v->v_csnset);
    newvalue->v_flags = v->v_flags;
    return newvalue;
}

Slapi_Value *
value_new(const struct berval *bval, CSNType t, const CSN *csn)
{
    Slapi_Value *v;
    v = (Slapi_Value *)slapi_ch_malloc(sizeof(Slapi_Value));
    value_init(v, bval, t, csn);
    if (!counters_created) {
        PR_CREATE_COUNTER(slapi_value_counter_created, "Slapi_Value", "created", "");
        PR_CREATE_COUNTER(slapi_value_counter_deleted, "Slapi_Value", "deleted", "");
        PR_CREATE_COUNTER(slapi_value_counter_exist, "Slapi_Value", "exist", "");
        counters_created = 1;
    }
    PR_INCREMENT_COUNTER(slapi_value_counter_created);
    PR_INCREMENT_COUNTER(slapi_value_counter_exist);
    VALUE_DUMP(v, "value_new");
    return v;
}

Slapi_Value *
value_init(Slapi_Value *v, const struct berval *bval, CSNType t, const CSN *csn)
{
    PR_ASSERT(v != NULL);
    memset(v, 0, sizeof(Slapi_Value));
    if (csn != NULL) {
        value_update_csn(v, t, csn);
    }
    slapi_value_set_berval(v, bval);
    return v;
}

void
slapi_value_set_flags(Slapi_Value *v, unsigned long flags)
{
    PR_ASSERT(v != NULL);
    v->v_flags = flags;
}

void
slapi_values_set_flags(Slapi_Value **vs, unsigned long flags)
{
    PR_ASSERT(vs != NULL);
    Slapi_Value **v;
    for (v = vs; v && *v; v++) {
        slapi_value_set_flags(*v, flags);
    }
}

unsigned long
slapi_value_get_flags(Slapi_Value *v)
{
    PR_ASSERT(v != NULL);
    return v->v_flags;
}

void
slapi_value_free(Slapi_Value **v)
{
    if (v != NULL && *v != NULL) {
        VALUE_DUMP(*v, "value_free");
        value_done(*v);
        slapi_ch_free((void **)v);
        *v = NULL;
        PR_INCREMENT_COUNTER(slapi_value_counter_deleted);
        PR_DECREMENT_COUNTER(slapi_value_counter_exist);
    }
}

void
value_done(Slapi_Value *v)
{
    if (v != NULL) {
        if (NULL != v->v_csnset) {
            csnset_free(&(v->v_csnset));
        }
        ber_bvdone(&v->bv);
    }
}

const CSNSet *
value_get_csnset(const Slapi_Value *value)
{
    if (value)
        return value->v_csnset;
    else
        return NULL;
}

const CSN *
value_get_csn(const Slapi_Value *value, CSNType t)
{
    const CSN *csn = NULL;
    if (NULL != value) {
        csn = csnset_get_csn_of_type(value->v_csnset, t);
    }
    return csn;
}

int
value_contains_csn(const Slapi_Value *value, CSN *csn)
{
    int r = 0;
    if (NULL != value) {
        r = csnset_contains(value->v_csnset, csn);
    }
    return r;
}

Slapi_Value *
value_update_csn(Slapi_Value *value, CSNType t, const CSN *csn)
{
    if (value != NULL) {
        csnset_update_csn(&value->v_csnset, t, csn);
    }
    return value;
}

Slapi_Value *
value_add_csn(Slapi_Value *value, CSNType t, const CSN *csn)
{
    if (value != NULL) {
        csnset_add_csn(&value->v_csnset, t, csn);
    }
    return value;
}

const struct berval *
slapi_value_get_berval(const Slapi_Value *value)
{
    const struct berval *bval = NULL;
    if (NULL != value) {
        bval = &value->bv;
    }
    return bval;
}

Slapi_Value *
slapi_value_set(Slapi_Value *value, void *val, unsigned long len)
{
    struct berval bv;
    bv.bv_len = len;
    bv.bv_val = val; /* We cast away the const, but we're not going to change anything */
    slapi_value_set_berval(value, &bv);
    return value;
}

Slapi_Value *
slapi_value_set_value(Slapi_Value *value, const Slapi_Value *vfrom)
{
    slapi_value_set_berval(value, &vfrom->bv);
    csnset_free(&value->v_csnset);
    value->v_csnset = csnset_dup(vfrom->v_csnset);
    return value;
}

Slapi_Value *
value_remove_csn(Slapi_Value *value, CSNType t)
{
    if (value != NULL) {
        csnset_remove_csn(&value->v_csnset, t);
    }
    return value;
}

Slapi_Value *
slapi_value_set_berval(Slapi_Value *value, const struct berval *bval)
{
    if (value != NULL) {
        ber_bvdone(&value->bv);
        if (bval != NULL) {
            ber_bvcpy(&value->bv, bval);
        }
    }
    return value;
}

int
slapi_value_set_string(Slapi_Value *value, const char *strVal)
{
    return slapi_value_set_string_passin(value, slapi_ch_strdup(strVal));
}

int
slapi_value_set_string_passin(Slapi_Value *value, char *strVal)
{
    int rc = -1;
    if (NULL != value) {
        ber_bvdone(&value->bv);
        value->bv.bv_val = strVal;
        value->bv.bv_len = strlen(strVal);
        rc = 0;
    }
    return rc;
}

int
slapi_value_set_int(Slapi_Value *value, int intVal)
{
    int rc = -1;
    if (NULL != value) {
        char valueBuf[80];
        ber_bvdone(&value->bv);
        sprintf(valueBuf, "%d", intVal);
        value->bv.bv_val = slapi_ch_strdup(valueBuf);
        value->bv.bv_len = strlen(value->bv.bv_val);
        rc = 0;
    }
    return rc;
}

/*
 * Warning: The value may not be '\0' terminated!
 * Make sure that you know this is a C string.
 */
const char *
slapi_value_get_string(const Slapi_Value *value)
{
    const char *r = NULL;
    if (value != NULL) {
        r = (const char *)value->bv.bv_val;
    }
    return r;
}

size_t
slapi_value_get_length(const Slapi_Value *value)
{
    size_t r = 0;
    if (NULL != value) {
        r = value->bv.bv_len;
    }
    return r;
}

int
slapi_value_get_int(const Slapi_Value *value)
{
    int r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = atoi(p);
        slapi_ch_free((void **)&p);
    }
    return r;
}

unsigned int
slapi_value_get_uint(const Slapi_Value *value)
{
    unsigned int r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = (unsigned int)atoi(p);
        slapi_ch_free((void **)&p);
    }
    return r;
}

long
slapi_value_get_long(const Slapi_Value *value)
{
    long r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = atol(p);
        slapi_ch_free((void **)&p);
    }
    return r;
}

unsigned long
slapi_value_get_ulong(const Slapi_Value *value)
{
    unsigned long r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = (unsigned long)atol(p);
        slapi_ch_free((void **)&p);
    }
    return r;
}

long long
slapi_value_get_longlong(const Slapi_Value *value)
{
    long long r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = strtoll(p, (char **)NULL, 0);
        slapi_ch_free((void **)&p);
    }
    return r;
}

unsigned long long
slapi_value_get_ulonglong(const Slapi_Value *value)
{
    unsigned long long r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = strtoull(p, (char **)NULL, 0);
        slapi_ch_free((void **)&p);
    }
    return r;
}

long
slapi_value_get_timelong(const Slapi_Value *value)
{
    long r = 0;
    if (NULL != value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = parse_duration_32bit(p);
        slapi_ch_free((void **)&p);
    }
    return r;
}

long long
slapi_value_get_timelonglong(const Slapi_Value *value)
{
    long long r = 0;
    if (value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = parse_duration_time_t(p);
        slapi_ch_free_string(&p);
    }
    return r;
}

time_t
slapi_value_get_time_time_t(const Slapi_Value *value)
{
    long long r = 0;
    if (value) {
        char *p;
        p = slapi_ch_malloc(value->bv.bv_len + 1);
        memcpy(p, value->bv.bv_val, value->bv.bv_len);
        p[value->bv.bv_len] = '\0';
        r = parse_duration_time_t(p);
        slapi_ch_free_string(&p);
    }
    return r;
}


int
slapi_value_compare(const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2)
{
    int r = 0;
    if (v1 != NULL && v2 != NULL) {
        r = slapi_attr_value_cmp_ext(a, (Slapi_Value *)v1, (Slapi_Value *)v2);
    } else if (v1 != NULL && v2 == NULL) {
        r = 1; /* v1>v2 */
    } else if (v1 == NULL && v2 != NULL) {
        r = -1; /* v1<v2 */
    } else      /* (v1==NULL && v2==NULL) */
    {
        r = 0; /* The same */
    }
    return r;
}

size_t
value_size(const Slapi_Value *v)
{
    size_t s = v->bv.bv_len;
    s += csnset_size(v->v_csnset);
    s += sizeof(Slapi_Value);
    return s;
}


#ifdef VALUE_DEBUG
static void
value_dump(const Slapi_Value *value, const char *text)
{
    slapi_log_err(SLAPI_LOG_DEBUG, "Slapi_Value %s ptr=%lx\n", text, value, 0);
    /* JCM - Dump value contents... */
}
#endif

int
value_dn_normalize_value(Slapi_Value *value)
{
    Slapi_DN *sdn = NULL;
    int rc = 0;

    if (NULL == value) {
        return rc;
    }

    sdn = slapi_sdn_new_dn_passin(value->bv.bv_val);
    if (slapi_sdn_get_dn(sdn)) {
        value->bv.bv_val = slapi_ch_strdup(slapi_sdn_get_dn(sdn));
        value->bv.bv_len = slapi_sdn_get_ndn_len(sdn);
        slapi_sdn_free(&sdn);
        slapi_value_set_flags(value, SLAPI_ATTR_FLAG_NORMALIZED_CES);
    } else {
        rc = 1;
        slapi_ch_free((void **)&sdn); /* free just Slapi_DN */
    }

    return rc;
}
