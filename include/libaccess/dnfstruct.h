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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
