/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __ipfstruct_h
#define __ipfstruct_h

/*
 * Description (ipfstruct.h)
 *
 *	This file defines types and structures used to represent an
 *	IP address filter in memory.  An IP address filter contains
 *	specifications of IP host and network addresses.  Each of
 *	these specifications can be associated with whatever information
 *	is appropriate for a particular use of an IP address filter.
 */

/* Define a scalar IP address value */
#ifndef __IPADDR_T_
#define __IPADDR_T_
typedef unsigned long IPAddr_t;
#endif /* __IPADDR_T_ */

/*
 * Description (IPNode_t)
 *
 * This type describes an internal node in the radix tree.  An internal
 * node has a link up the tree to its parent, and up to three links
 * down the tree to its descendants.  Each internal node is used to
 * test a particular bit in a given IP address, and traverse down the
 * tree in a direction which depends on whether the bit is set, clear,
 * or masked out.  The descendants of an internal node may be internal
 * nodes or leaf nodes (IPLeaf_t).
 */

/* Define indices of links in an IPNode_t */
#define IPN_CLEAR	0	/* link to node with ipn_bit clear */
#define IPN_SET		1	/* link to node with ipn_bit set */
#define IPN_MASKED	2	/* link to node with ipn_bit masked out */
#define IPN_NLINKS	3	/* number of links */

typedef struct IPNode_s IPNode_t;
struct IPNode_s {
    char ipn_type;		/* node type */
#define IPN_LEAF	0	/* leaf node */
#define IPN_NODE	1	/* internal node */

    char ipn_bit;		/* bit number (31-0) to test */
    IPNode_t * ipn_parent;	/* link to parent node */
    IPNode_t * ipn_links[IPN_NLINKS];	
};

/* Helper definitions */
#define ipn_clear	ipn_links[IPN_CLEAR]
#define ipn_set		ipn_links[IPN_SET]
#define ipn_masked	ipn_links[IPN_MASKED]

/*
 * Description (IPLeaf_t)
 *
 * This type describes a leaf node in the radix tree.  A leaf node
 * contains an IP host or network address, and a network mask.  A
 * given IP address matches a leaf node if the IP address, when masked
 * by ipl_netmask, equals ipl_ipaddr.
 */

typedef struct IPLeaf_s IPLeaf_t;
struct IPLeaf_s {
    char ipl_type;		/* see ipn_type in IPNode_t */
    IPAddr_t ipl_netmask;	/* IP network mask */
    IPAddr_t ipl_ipaddr;	/* IP address of host or network */
};

typedef struct IPFilter_s IPFilter_t;
struct IPFilter_s {
    IPFilter_t * ipf_next;	/* link to next filter */
    IPNode_t * ipf_tree;	/* pointer to radix tree structure */
};

#endif /* __ipfstruct_h */
