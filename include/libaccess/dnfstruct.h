/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __dnfstruct_h
#define __dnfstruct_h

/*
 * Description (dnfstruct_h)
 *
 *	This file defines types and structures used to represent a DNS
 *	name filter in memory.  A DNS name filter contains specifications
 *	of fully or partially qualified DNS names.  Each of these
 *	specifications can be associated with whatever information is
 *	appropriate for a particular use of a DNS name filter.
 */

#include "nspr.h"
#include "plhash.h"

NSPR_BEGIN_EXTERN_C

/*
 * Description (DNSLeaf_t)
 *
 *	This type describes the structure of information associated with
 *	an entry in a DNS filter.  The filter itself is implemented as a
 *	hash table, keyed by the DNS name specification string.  The
 *	value associated with a key is a pointer to a DNSLeaf_t structure.
 */

typedef struct DNSLeaf_s DNSLeaf_t;
struct DNSLeaf_s {
    PLHashEntry dnl_he;		/* NSPR hash table entry */
};

#define dnl_next dnl_he.next		/* hash table collision link */
#define dnl_keyhash dnl_he.keyHash	/* symbol hash value */
#define dnl_key dnl_he.key		/* pointer to Symbol_t structure */
#define dnl_ref dnl_he.value		/* pointer to named structure */

typedef struct DNSFilter_s DNSFilter_t;
struct DNSFilter_s {
    DNSFilter_t * dnf_next;	/* link to next filter */
    void * dnf_hash;		/* pointer to constructed hash table */
};

NSPR_END_EXTERN_C

#endif /* __dnfstruct_h */
