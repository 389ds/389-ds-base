/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
