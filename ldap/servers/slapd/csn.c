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
 *  csn.c - CSN
 */

#include <string.h>
#include "slap.h"

#define _CSN_TSORDER_TSTAMP_OFFSET 0
#define _CSN_TSORDER_SEQNUM_OFFSET 8
#define _CSN_TSORDER_REPLID_OFFSET 12
#define _CSN_TSORDER_SUBSEQNUM_OFFSET 16

static PRBool _csnIsValidString(const char *csnStr);

/*
 * Debugging counters.
 */
#ifdef DEBUG
static int counters_created = 0;
static Slapi_Counter *slapi_csn_counter_created;
static Slapi_Counter *slapi_csn_counter_deleted;
static Slapi_Counter *slapi_csn_counter_exist;
#endif

/*
 * **************************************************************************
 * CSN Functions
 * **************************************************************************
 */

#ifdef DEBUG
static void
csn_create_counters(void)
{
    slapi_csn_counter_created = slapi_counter_new();
    slapi_csn_counter_deleted = slapi_counter_new();
    slapi_csn_counter_exist = slapi_counter_new();
    counters_created = 1;
}
#endif

CSN *
csn_new(void)
{
#ifdef DEBUG
    if (!counters_created) {
        csn_create_counters();
    }
    slapi_counter_increment(slapi_csn_counter_created);
    slapi_counter_increment(slapi_csn_counter_exist);
#endif
    return (CSN *)slapi_ch_calloc(sizeof(CSN), 1);
}

CSN *
csn_new_by_string(const char *s)
{
    CSN *newcsn = NULL;
    if (s != NULL) {
        if (_csnIsValidString(s)) {
            newcsn = csn_new();
            csn_init_by_string(newcsn, s);
        }
    }
    return newcsn;
}

void
csn_init(CSN *csn)
{
    if (csn != NULL) {
        memset(csn, 0, sizeof(CSN));
    }
}

void
csn_init_by_csn(CSN *csn1, const CSN *csn2)
{
    if (csn2 != NULL) {
        memcpy(csn1, csn2, sizeof(CSN));
    } else {
        csn_init(csn1);
    }
}

void
csn_init_by_string(CSN *csn, const char *s)
{
    if (_csnIsValidString(s)) {
        /* yes - time_t is long - but the CSN will only ever store the lowest 4 bytes of
           the timestamp */
        csn->tstamp = slapi_str_to_u32(s + _CSN_TSORDER_TSTAMP_OFFSET);
        csn->seqnum = slapi_str_to_u16(s + _CSN_TSORDER_SEQNUM_OFFSET);
        csn->rid = slapi_str_to_u16(s + _CSN_TSORDER_REPLID_OFFSET);
        csn->subseqnum = slapi_str_to_u16(s + _CSN_TSORDER_SUBSEQNUM_OFFSET);
    }
}

CSN *
csn_dup(const CSN *csn)
{
    CSN *newcsn = NULL;
    if (csn != NULL) {
        newcsn = csn_new();
        csn_init_by_csn(newcsn, csn);
    }
    return newcsn;
}

void
csn_done(CSN *csn __attribute__((unused)))
{
    return;
}

void
csn_free(CSN **csn)
{
    if (csn != NULL && *csn != NULL) {
#ifdef DEBUG
        if (!counters_created) {
            csn_create_counters();
        }
        slapi_counter_increment(slapi_csn_counter_deleted);
        slapi_counter_increment(slapi_csn_counter_exist);
#endif
        slapi_ch_free((void **)csn);
    }
    return;
}

void
csn_set_replicaid(CSN *csn, ReplicaId rid)
{
    csn->rid = rid;
}

void
csn_set_time(CSN *csn, time_t csntime)
{
    csn->tstamp = csntime;
}

void
csn_set_seqnum(CSN *csn, PRUint16 seqnum)
{
    csn->seqnum = seqnum;
}

ReplicaId
csn_get_replicaid(const CSN *csn)
{
    return csn->rid;
}

PRUint16
csn_get_seqnum(const CSN *csn)
{
    return csn->seqnum;
}

PRUint16
csn_get_subseqnum(const CSN *csn)
{
    return csn->subseqnum;
}

time_t
csn_get_time(const CSN *csn)
{
    if (csn == NULL) {
        return 0;
    } else {
        return csn->tstamp;
    }
}


/*
 * WARNING: ss must point to memory at least CSN_STRSIZE bytes long,
 * WARNING: or be NULL, which means this function will allocate the
 * WARNING: memory, which must be free'd by the caller.
 */
char *
csn_as_string(const CSN *csn, PRBool replicaIdOrder __attribute__((unused)), char *ss)
{
    char *s = ss;
    if (s == NULL) {
        s = slapi_ch_malloc(CSN_STRSIZE);
    }
    if (csn == NULL) {
        s[0] = '\0';
    } else {
        char *ptr = slapi_u32_to_hex(csn->tstamp, s, 0);
        ptr = slapi_u16_to_hex(csn->seqnum, ptr, 0);
        ptr = slapi_u16_to_hex(csn->rid, ptr, 0);
        ptr = slapi_u16_to_hex(csn->subseqnum, ptr, 0);
        *ptr = 0;
    }
    return s;
}


/*
 * WARNING: ss must point to memory at least (7+CSN_STRSIZE) bytes long,
 * WARNING: or be NULL, which means this function will allocate the
 * WARNING: memory, which must be free'd by the caller.
 */
char *
csn_as_attr_option_string(CSNType t, const CSN *csn, char *ss)
{
    char *s = ss;
    if (csn != NULL) {
        if (s == NULL) {
            s = slapi_ch_malloc(8 + CSN_STRSIZE);
        }
        s[0] = ';';
        switch (t) {
        case CSN_TYPE_UNKNOWN:
            s[1] = 'x';
            s[2] = '1';
            break;
        case CSN_TYPE_NONE:
            s[1] = 'x';
            s[2] = '2';
            break;
        case CSN_TYPE_ATTRIBUTE_DELETED:
            s[1] = 'a';
            s[2] = 'd';
            break;
        case CSN_TYPE_VALUE_UPDATED:
            s[1] = 'v';
            s[2] = 'u';
            break;
        case CSN_TYPE_VALUE_DELETED:
            s[1] = 'v';
            s[2] = 'd';
            break;
        case CSN_TYPE_VALUE_DISTINGUISHED:
            s[1] = 'm';
            s[2] = 'd';
            break;
        }
        s[3] = 'c';
        s[4] = 's';
        s[5] = 'n';
        s[6] = '-';
        csn_as_string(csn, PR_FALSE, s + 7);
    }
    return s;
}

int
csn_is_equal(const CSN *csn1, const CSN *csn2)
{
    int retval = 0;
    if ((csn1 == NULL && csn2 == NULL) ||
        (csn1 && csn2 &&
         csn1->tstamp == csn2->tstamp &&
         csn1->seqnum == csn2->seqnum &&
         csn1->rid == csn2->rid &&
         csn1->subseqnum == csn2->subseqnum)) {
        retval = 1;
    }
    return retval;
}

int
csn_compare_ext(const CSN *csn1, const CSN *csn2, unsigned int flags)
{
    PRInt32 retVal;
    if (csn1 != NULL && csn2 != NULL) {
        /* csns can't be compared via memcmp (previuos version of the code)
           because, on NT, bytes are reversed */
        if (csn1->tstamp < csn2->tstamp)
            retVal = -1;
        else if (csn1->tstamp > csn2->tstamp)
            retVal = 1;
        else {
            if (csn1->seqnum < csn2->seqnum)
                retVal = -1;
            else if (csn1->seqnum > csn2->seqnum)
                retVal = 1;
            else {
                if (csn1->rid < csn2->rid)
                    retVal = -1;
                else if (csn1->rid > csn2->rid)
                    retVal = 1;
                else if (!(flags & CSN_COMPARE_SKIP_SUBSEQ)) {
                    if (csn1->subseqnum < csn2->subseqnum)
                        retVal = -1;
                    else if (csn1->subseqnum > csn2->subseqnum)
                        retVal = 1;
                    else
                        retVal = 0;
                } else
                    retVal = 0;
            }
        }

    } else if (csn1 != NULL && csn2 == NULL) {
        retVal = 1; /* csn1>csn2 */
    } else if (csn1 == NULL && csn2 != NULL) {
        retVal = -1; /* csn1<csn2 */
    } else           /* (csn1==NULL && csn2==NULL) */
    {
        retVal = 0; /* The same */
    }

    return (retVal);
}

int
csn_compare(const CSN *csn1, const CSN *csn2)
{
    return csn_compare_ext(csn1, csn2, 0);
}

time_t
csn_time_difference(const CSN *csn1, const CSN *csn2)
{
    return csn_get_time(csn1) - csn_get_time(csn2);
}

const CSN *
csn_max(const CSN *csn1, const CSN *csn2)
{
    if (csn_compare(csn1, csn2) > 0) {
        return csn1;
    } else {
        return csn2;
    }
}

int
csn_increment_subsequence(CSN *csn)
{
    PRUint16 maxsubseq = (PRUint16)0xFFFFFFFF;
    if (csn == NULL) {
        return -1;
    } else if (csn->subseqnum == maxsubseq) {
        slapi_log_err(SLAPI_LOG_ERR, "csn_increment_subsequence",
                      "subsequence overflow\n");
        return -1;
    } else {
        csn->subseqnum++;
        return 0;
    }
}

/*
 * sizeof(vucsn-011111111222233334444)
 * Does not include the trailing '\0'
 */
size_t
csn_string_size()
{
    return LDIF_CSNPREFIX_MAXLENGTH + _CSN_VALIDCSN_STRLEN;
}

static PRBool
_csnIsValidString(const char *s)
{
    if (NULL == s) {
        return (PR_FALSE);
    }
    if (strlen(s) < _CSN_VALIDCSN_STRLEN) {
        return (PR_FALSE);
    }

    /* some more checks on validity of tstamp portion? */
    return (PR_TRUE);
}
