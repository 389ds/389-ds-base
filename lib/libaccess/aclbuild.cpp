/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (aclbuild.c)
 *
 *	This module provides functions for building Access Control List
 *	(ACL) structures in memory.
 *
 */

#include <assert.h>
#include "base/systems.h"
#include "netsite.h"
#include "libaccess/nsauth.h"
#include "libaccess/nsuser.h"
#include "libaccess/nsgroup.h"
#include "libaccess/nsadb.h"
#include "libaccess/aclerror.h"
#include "libaccess/aclstruct.h"
#include "libaccess/aclbuild.h"
#include "libaccess/aclparse.h"
#include "libaccess/acleval.h"
#include "libaccess/usi.h"

char * ACL_Program = "NSACL";		/* ACL facility name */

/*
 * Description (accCreate)
 *
 *	This function creates a new access control context, which
 *	provides context information for a set of ACL definitions.
 *	The caller also provides a handle for a symbol table to be
 *	used to store definitions of ACL and rights names.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	stp			- symbol table handle (may be null)
 *	pacc			- pointer to returned context handle
 *
 * Returns:
 *
 *	If the context is created successfully, the return value is zero.
 *	Otherwise it is a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 */

int accCreate(NSErr_t * errp, void * stp, ACContext_t **pacc)
{
    ACContext_t * acc;			/* pointer to new context */
    int rv;				/* result value */
    int eid;				/* error id */

    *pacc = 0;

    /* Do we need to create a symbol table? */
    if (stp == 0) {

	/* Yes, create a symbol table for ACL, rights, etc. names */
	rv = symTableNew(&stp);
	if (rv < 0) goto err_nomem1;
    }

    /* Allocate the context structure */
    acc = (ACContext_t *)MALLOC(sizeof(ACContext_t));
    if (acc == 0) goto err_nomem2;

    /* Initialize it */
    acc->acc_stp = stp;
    acc->acc_acls = 0;
    acc->acc_rights = 0;
    acc->acc_refcnt = 0;

    *pacc = acc;
    return 0;

  err_nomem1:
    rv = ACLERRNOMEM;
    eid = ACLERR3000;
    goto err_ret;

  err_nomem2:
    rv = ACLERRNOMEM;
    eid = ACLERR3020;

  err_ret:
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    return rv;
}

/*
 * Description (accDestroy)
 *
 *	This function destroys a set of ACL data structures referenced
 *	by a specified ACContext_t structure, including the ACContext_t
 *	itself.
 *
 * Arguments:
 *
 *	acc			- pointer to ACContext_t structure
 *	flags			- bit flags (unused - must be zero)
 */

void accDestroy(ACContext_t * acc, int flags)
{
    ACL_t * acl;

    if (acc != 0) {

	/*
	 * First destroy all ACLs and any unnamed structures they reference.
	 * Note that aclDestroy() modifies the acc_acls list.
	 */
	while ((acl = acc->acc_acls) != 0) {

	    aclDelete(acl);
	}

	/* If there's a symbol table, destroy everything it references */
	if (acc->acc_stp != 0) {
	    symTableEnumerate(acc->acc_stp, 0, accDestroySym);

	    /* Destroy the symbol table itself */
	    symTableDestroy(acc->acc_stp, 0);
	}

	/* Free the ACContext_t structure */
	FREE(acc);
    }
}

/*
 * Description (accDestroySym)
 *
 *	This function is called to destroy the data structure associated
 *	with a specified Symbol_t symbol table entry.  It examines the
 *	type of the symbol and calls the appropriate destructor.
 *
 * Arguments:
 *
 *	sym			- pointer to symbol table entry
 *	argp			- unused - must be zero
 *
 * Returns:
 *
 *	The return value is SYMENUMREMOVE.
 */

int accDestroySym(Symbol_t * sym, void * argp)
{
    switch (sym->sym_type) {
      case ACLSYMACL:				/* ACL */
	aclDestroy((ACL_t *)sym);
	break;

      case ACLSYMRIGHT:			/* access right */
	{
	    RightDef_t * rdp = (RightDef_t *)sym;

	    if (rdp->rd_sym.sym_name != 0) {
		FREE(rdp->rd_sym.sym_name);
	    }
	    FREE(rdp);
	}
	break;

      case ACLSYMRDEF:			/* access rights list */
	aclRightSpecDestroy((RightSpec_t *)sym);
	break;

      case ACLSYMREALM:			/* realm name */
	aclRealmSpecDestroy((RealmSpec_t *)sym);
	break;

      case ACLSYMHOST:			/* host specifications */
	aclHostSpecDestroy((HostSpec_t *)sym);
	break;

      case ACLSYMUSER:			/* user/group list */
	aclUserSpecDestroy((UserSpec_t *)sym);
	break;
    }

    return SYMENUMREMOVE;
}

/*
 * Description (accReadFile)
 *
 *	This function reads a specfied file containing ACL definitions
 *	and creates data structures in memory to represent the ACLs.
 *	The caller may provide a pointer to an existing ACContext_t
 *	structure which will serve as the root of the ACL structures,
 *	or else a new one will be created.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	aclfile			- pointer to the ACL filename string
 *	pacc			- value/result ACContext_t
 *
 * Returns:
 *
 *	If the ACL file is read successfully, the return value is zero.
 *	Otherwise it is a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 */

int accReadFile(NSErr_t * errp, char * aclfile, ACContext_t **pacc)
{
    ACContext_t * acc = *pacc;	/* pointer to ACL root structure */
    ACLFile_t * acf = 0;	/* pointer to ACL file handle */
    void * stp = 0;		/* ACL symbol table handle */
    int rv;			/* result value */
    int eid;			/* error id value */

    /* Initialize the ACL parser */
    rv = aclParseInit();
    if (rv < 0) goto err_init;

    /* Do we need to create a new ACContext_t structure? */
    if (acc == 0) {

	/* Yes, create a symbol table for ACL, rights, etc. names */
	rv = symTableNew(&stp);
	if (rv < 0) goto err_crsym;

	/* Create a root structure for the ACLs, including the symbol table */
	rv = accCreate(errp, stp, &acc);
	if (rv < 0) goto err_ret2;
    }

    /* Open the ACL definition file */
    rv = aclFileOpen(errp, aclfile, 0, &acf);
    if (rv < 0) goto err_ret3;

    /* Parse the ACL definitions, building ACL structures in memory */
    rv = aclACLParse(errp, acf, acc, 0);
    if (rv < 0) goto err_ret4;

    aclFileClose(acf, 0);

    if (pacc) *pacc = acc;

    return rv;

  err_init:
    eid = ACLERR3100;
    goto err_ret;

  err_crsym:
    eid = ACLERR3120;
    rv = ACLERRNOMEM;
    goto err_ret;

  err_ret4:
    aclFileClose(acf, 0);
  err_ret3:
    /* Destroy the ACContext_t if we just created it */
    if (acc != *pacc) {
	accDestroy(acc, 0);
    }
    goto err_ret;

  err_ret2:
    symTableDestroy(stp, 0);

  err_ret:
    return rv;
}

/*
 * Description (aclAuthDNSAdd)
 *
 *	This function adds a DNS name specification to the DNS filter
 *	associated with a given host list.  The DNS name specification is
 *	either a fully-qualified domain name or a domain name suffix,
 *	indicated by a leading ".", e.g. (".mcom.com").  The name
 *	components included in a suffix must be complete.  For example,
 *	".scape.com" will not match names ending in ".netscape.com".
 *
 * Arguments:
 *
 *	hspp			- pointer to host list pointer
 *	dnsspec			- DNS name or suffix string pointer
 *	fqdn			- non-zero if dnsspec is fully qualified
 *
 * Returns:
 *
 *	If successful, the return code is zero.
 *	An error is indicated by a negative return code (ACLERRxxxx
 *	- see aclerror.h).
 */

int aclAuthDNSAdd(HostSpec_t **hspp, char * dnsspec, int fqdn)
{
    HostSpec_t * hsp;		/* host list pointer */
    void * table;		/* access control hash table pointer */
    Symbol_t * sym;		/* hash table entry pointer */
    int rv;			/* result value */

    fqdn = (fqdn) ? 1 : 0;

    /* Create the HostSpec_t if it doesn't exist */
    hsp = *hspp;
    if (hsp == 0) {

	hsp = (HostSpec_t *)MALLOC(sizeof(HostSpec_t));
	if (hsp == 0) goto err_nomem;
	memset((void *)hsp, 0, sizeof(HostSpec_t));
	hsp->hs_sym.sym_type = ACLSYMHOST;
    }

    /* Get pointer to hash table used for DNS filter */
    table = hsp->hs_host.inh_dnf.dnf_hash;
    if (table == 0) {

	/* None there yet, so create one */
	rv = symTableNew(&table);
	if (rv < 0) goto punt;
	hsp->hs_host.inh_dnf.dnf_hash = table;
    }

    /* See if the DNS spec is already in the table */
    rv = symTableFindSym(table, dnsspec, fqdn, (void **)&sym);
    if (rv < 0) {
	if (rv != SYMERRNOSYM) goto punt;

	/* It's not there, so add it */
	sym = (Symbol_t *)MALLOC(sizeof(Symbol_t));
	sym->sym_name = STRDUP(dnsspec);
	sym->sym_type = fqdn;

	rv = symTableAddSym(table, sym, (void *)sym);
	if (rv < 0) goto err_nomem;
    }

    *hspp = hsp;

  punt:
    return rv;

  err_nomem:
    rv = ACLERRNOMEM;
    goto punt;
}

/*
 * Description (aclAuthIPAdd)
 *
 *	This function adds an IP address specification to the IP filter
 *	associated with a given host list.  The IP address specification
 *	consists of an IP host or network address and an IP netmask.
 *	For host addresses the netmask value is 255.255.255.255.
 *
 * Arguments:
 *
 *	hspp			- pointer to host list pointer
 *	ipaddr			- IP host or network address
 *	netmask			- IP netmask value
 *
 * Returns:
 *
 *	If successful, the return code is zero.
 *	An error is indicated by a negative return code (ACLERRxxxx
 *	- see aclerror.h).
 */

int aclAuthIPAdd(HostSpec_t **hspp, IPAddr_t ipaddr, IPAddr_t netmask)
{
    HostSpec_t * hsp;		/* host list pointer */
    IPFilter_t * ipf;		/* IP filter pointer */
    IPNode_t * ipn;		/* current node pointer */
    IPNode_t * lastipn;		/* last (lower) node pointer */
    IPLeaf_t * leaf;		/* leaf node pointer */
    IPAddr_t bitmask;		/* bit mask for current node */
    int lastbit;		/* number of last bit set in netmask */
    int i;			/* loop index */

    /* Create the HostSpec_t if it doesn't exist */
    hsp = *hspp;
    if (hsp == 0) {

	hsp = (HostSpec_t *)MALLOC(sizeof(HostSpec_t));
	if (hsp == 0) goto err_nomem;
	memset((void *)hsp, 0, sizeof(HostSpec_t));
	hsp->hs_sym.sym_type = ACLSYMHOST;
    }

    ipf = &hsp->hs_host.inh_ipf;

    /* If the filter doesn't have a root node yet, create it */
    if (ipf->ipf_tree == 0) {

	/* Allocate node */
	ipn = (IPNode_t *)MALLOC(sizeof(IPNode_t));
	if (ipn == 0) goto err_nomem;

	/* Initialize it to test bit 31, but without any descendants */
	ipn->ipn_type = IPN_NODE;
	ipn->ipn_bit = 31;
	ipn->ipn_parent = NULL;
	ipn->ipn_clear = NULL;
	ipn->ipn_set = NULL;
	ipn->ipn_masked = NULL;

	/* Set it as the root node in the radix tree */
	ipf->ipf_tree = ipn;
    }

    /* First we search the tree to see where this IP specification fits */

    lastipn = NULL;

    for (ipn = ipf->ipf_tree; (ipn != NULL) && (ipn->ipn_type == IPN_NODE); ) {

	/* Get a mask for the bit this node tests */
	bitmask = (IPAddr_t) 1<<ipn->ipn_bit;

	/* Save pointer to last internal node */
	lastipn = ipn;

	/* Is this a bit we care about? */
	if (bitmask & netmask) {

	    /* Yes, get address of set or clear descendant pointer */
	    ipn = (bitmask & ipaddr) ? ipn->ipn_set : ipn->ipn_clear;
	}
	else {
	    /* No, get the address of the masked descendant pointer */
	    ipn = ipn->ipn_masked;
	}
    }

    /* Did we end up at a leaf node? */
    if (ipn == NULL) {

	/*
         * No, well, we need to find a leaf node if possible.  The
         * reason is that we need an IP address and netmask to compare
         * to the IP address and netmask we're inserting.  We know that
         * they're the same up to the bit tested by the lastipn node,
         * but we need to know the *highest* order bit that's different.
         * Any leaf node below lastipn will do.
         */

	leaf = NULL;
        ipn = lastipn;

        while (ipn != NULL) {

            /* Look for any non-null child link of the current node */
            for (i = 0; i < IPN_NLINKS; ++i) {
                if (ipn->ipn_links[i]) break;
            }

            /*
             * Fail search for leaf if no non-null child link found.
             * This should only happen on the root node of the tree
             * when the tree is empty.
             */
            if (i >= IPN_NLINKS) {
                assert(ipn == ipf->ipf_tree);
                break;
            }

            /* Step to the child node */
            ipn = ipn->ipn_links[i];

            /* Is it a leaf? */
            if (ipn->ipn_type == IPN_LEAF) {

                /* Yes, search is over */
                leaf = (IPLeaf_t *)ipn;
                ipn = NULL;
                break;
	    }
	}
    }
    else {

	/* Yes, loop terminated on a leaf node */
	assert(ipn->ipn_type == IPN_LEAF);
	leaf = (IPLeaf_t *)ipn;
    }

    /* Got a leaf yet? */
    if (leaf != NULL) {

	/* Combine the IP address and netmask differences */
	bitmask = (leaf->ipl_ipaddr ^ ipaddr) | (leaf->ipl_netmask ^ netmask);

	/* Are both the IP address and the netmask the same? */
	if (bitmask == 0) {

	    /* Yes, duplicate entry */
	    return 0;
	}

	/* Find the bit number of the first different bit */
	for (lastbit = 31;
	     (bitmask & 0x80000000) == 0; --lastbit, bitmask <<= 1) ;

	/* Generate a bit mask with just that bit */
	bitmask = (IPAddr_t) (1 << lastbit);

	/*
	 * Go up the tree from lastipn, looking for an internal node
	 * that tests lastbit.  Stop if we get to a node that tests
	 * a higher bit number first.
	 */
	for (ipn = lastipn, lastipn = (IPNode_t *)leaf;
	     ipn != NULL; ipn = ipn->ipn_parent) {

	    if (ipn->ipn_bit >= lastbit) {
		if (ipn->ipn_bit == lastbit) {
		    /* Need to add a leaf off ipn node */
		    lastipn = NULL;
		}
		break;
	    }
	    lastipn = ipn;
	}

	assert(ipn != NULL);
    }
    else {

	/* Just hang a leaf off the lastipn node if no leaf */
	ipn = lastipn;
	lastipn = NULL;
	lastbit = ipn->ipn_bit;
    }

    /*
     * If lastipn is not NULL at this point, the new leaf will hang
     * off an internal node inserted between the upper node, referenced
     * by ipn, and the lower node, referenced by lastipn.  The lower
     * node may be an internal node or a leaf.
     */
    if (lastipn != NULL) {
	IPNode_t * parent = ipn;	/* parent of the new node */

	assert((lastipn->ipn_type == IPN_LEAF) ||
	       (ipn == lastipn->ipn_parent));

	/* Allocate space for the internal node */
	ipn = (IPNode_t *)MALLOC(sizeof(IPNode_t));
	if (ipn == NULL) goto err_nomem;

	ipn->ipn_type = IPN_NODE;
	ipn->ipn_bit = lastbit;
	ipn->ipn_parent = parent;
	ipn->ipn_clear = NULL;
	ipn->ipn_set = NULL;
	ipn->ipn_masked = NULL;

	bitmask = (IPAddr_t) (1 << lastbit);

	/*
	 * The values in the leaf we found above determine which
	 * descendant link of the new internal node will reference
	 * the subtree that we just ascended.
	 */
	if (leaf->ipl_netmask & bitmask) {
	    if (leaf->ipl_ipaddr & bitmask) {
		ipn->ipn_set = lastipn;
	    }
	    else {
		ipn->ipn_clear = lastipn;
	    }
	}
	else {
	    ipn->ipn_masked = lastipn;
	}

	/* Allocate space for the new leaf */
	leaf = (IPLeaf_t *)MALLOC(sizeof(IPLeaf_t));
	if (leaf == NULL) {
	    FREE((void *)ipn);
	    goto err_nomem;
	}

	/* Insert internal node in tree */

	/* First the downward link from the parent to the new node */
	for (i = 0; i < IPN_NLINKS; ++i) {
	    if (parent->ipn_links[i] == lastipn) {
		parent->ipn_links[i] = ipn;
		break;
	    }
	}

	/* Then the upward link from the child (if it's not a leaf) */
	if (lastipn->ipn_type == IPN_NODE) {
	    lastipn->ipn_parent = ipn;
	}
    }
    else {
	/* Allocate space for a leaf node only */
	leaf = (IPLeaf_t *)MALLOC(sizeof(IPLeaf_t));
	if (leaf == NULL) goto err_nomem;
    }

    /* Initialize the new leaf */
    leaf->ipl_type = IPN_LEAF;
    leaf->ipl_ipaddr = ipaddr;
    leaf->ipl_netmask = netmask;

    /*
     * Select the appropriate descendant link of the internal node
     * and point it at the new leaf.
     */
    bitmask = (IPAddr_t) (1 << ipn->ipn_bit);
    if (bitmask & netmask) {
	if (bitmask & ipaddr) {
	    assert(ipn->ipn_set == NULL);
	    ipn->ipn_set = (IPNode_t *)leaf;
	}
	else {
	    assert(ipn->ipn_clear == NULL);
	    ipn->ipn_clear = (IPNode_t *)leaf;
	}
    }
    else {
	assert(ipn->ipn_masked == NULL);
	ipn->ipn_masked = (IPNode_t *)leaf;
    }

    *hspp = hsp;

    /* Successful completion */
    return 0;

  err_nomem:
    return ACLERRNOMEM;
}

/*
 * Description (aclAuthNameAdd)
 *
 *	This function adds a user or group to a given user list,
 *	in the context of a specified ACL that is being created.  The
 *	name of the user or group is provided by the caller, and is
 *	looked up in the authentication database associated with the
 *	specified user list.  The return value indicates whether the name
 *	matched a user or group name, and whether the corresponding user
 *	or group id was already present in the given user list.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	usp			- pointer to user list specification
 *	rlm			- pointer to current authentication realm
 *	name			- pointer to user or group name string
 *
 * Returns:
 *
 *	The return value is zero if the name is not found in the
 *	authentication database.  If the name is found, the return value
 *	is a positive value containing bit flags:
 *
 *	AIF_GROUP		- name matches a group name
 *	AIF_USER		- name matches a user name
 *	AIF_DUP			- name was already represented in the
 *				  specified user list
 *
 *	An error is indicated by a negative return code (ACLERRxxxx
 *	- see aclerror.h), and an error frame will be generated if
 *	an error list is provided.
 */

int aclAuthNameAdd(NSErr_t * errp, UserSpec_t * usp,
		   Realm_t * rlm, char * name)
{
    void * guoptr;			/* group or user object pointer */
    int irv;				/* insert result value */
    int eid;				/* error id */
    int rv;				/* result value */

    /* There must be a realm specified in order to handle users */
    if (rlm == 0) goto err_norealm;
    
    /* Open the authentication database if it's not already */
    if (rlm->rlm_authdb == 0) {

	if (rlm->rlm_aif == 0) {
	    rlm->rlm_aif = &NSADB_AuthIF;
	}

	rv = (*rlm->rlm_aif->aif_open)(errp,
				       rlm->rlm_dbname, 0, &rlm->rlm_authdb);
	if (rv < 0) goto err_open;
    }

    /* Look up the name in the authentication DB */
    rv = (*rlm->rlm_aif->aif_findname)(errp, rlm->rlm_authdb, name,
				       (AIF_USER|AIF_GROUP), (void **)&guoptr);
    if (rv <= 0) {
	if (rv < 0) goto err_adb;

	/* The name was not found in the database */
	return 0;
    }

    /* The name was found.  Was it a user name? */
    if (rv == AIF_USER) {

	/* Yes, add the user id to the user list */
	irv = usiInsert(&usp->us_user.uu_user, ((UserObj_t *)guoptr)->uo_uid);
	rv = ANA_USER;
    }
    else {

	/* No, must be a group name.  Add group id to an_groups list. */
	irv = usiInsert(&usp->us_user.uu_group,
			((GroupObj_t *)guoptr)->go_gid);
	rv = ANA_GROUP;
    }

    /* Examine the result of the insert operation */
    if (irv <= 0) {
	if (irv < 0) goto err_ins;

	/* Id was already in the list */
	rv |= ANA_DUP;
    }

  punt:
    return rv;

  err_norealm:
    eid = ACLERR3400;
    rv = ACLERRNORLM;
    nserrGenerate(errp, rv, eid, ACL_Program, 1, name);
    goto punt;

  err_open:
    eid = ACLERR3420;
    rv = ACLERROPEN;
    nserrGenerate(errp, rv, eid, ACL_Program,
		  2, rlm->rlm_dbname, system_errmsg());
    goto punt;

  err_adb:
    /* Error accessing authentication database. */
    eid = ACLERR3440;
    rv = ACLERRADB;
    nserrGenerate(errp, rv, eid, ACL_Program, 2, rlm->rlm_dbname, name);
    goto punt;

  err_ins:
    /* Error on insert operation.  Must be lack of memory. */
    eid = ACLERR3460;
    rv = ACLERRNOMEM;
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    goto punt;
}

/*
 * Description (aclClientsDirCreate)
 *
 *	This function allocates and initializes a new ACClients_t
 *	ACL directive.
 *
 * Arguments:
 *
 *	None.
 *
 * Returns:
 *
 *	If successful, a pointer to the new ACClients_t is returned.
 *	A shortage of dynamic memory is indicated by a null return value.
 */

ACClients_t * aclClientsDirCreate()
{
    ACClients_t * acd;			/* pointer to new ACClients_t */

    acd = (ACClients_t *)MALLOC(sizeof(ACClients_t));
    if (acd != 0) {
	memset((void *)acd, 0, sizeof(ACClients_t));
    }

    return acd;
}

/*
 * Description (aclCreate)
 *
 *	This function creates a new ACL root structure.  The caller
 *	specifies the name to be associated with the ACL.  The ACL handle
 *	returned by this function is passed to other functions in this
 *	module when adding information to the ACL.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acc			- pointer to an access control context
 *	aclname			- pointer to ACL name string
 *	pacl			- pointer to returned ACL handle
 *
 * Returns:
 *
 *	The return value is zero if the ACL is created successfully.
 *	Otherwise it is a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 */

int aclCreate(NSErr_t * errp, ACContext_t * acc, char * aclname, ACL_t **pacl)
{
    ACL_t * acl;		/* pointer to created ACL */
    int rv;			/* result value */
    int eid;			/* error id */

    *pacl = 0;

    /* Allocate the ACL_t structure */
    acl = (ACL_t *) MALLOC(sizeof(ACL_t));
    if (acl == 0) goto err_nomem;

    /* Initialize the structure */
    memset((void *)acl, 0, sizeof(ACL_t));
    acl->acl_sym.sym_name = STRDUP(aclname);
    acl->acl_sym.sym_type = ACLSYMACL;
    acl->acl_acc = acc;
    acl->acl_refcnt = 1;

    /* Add it to the symbol table for the specified context */
    rv = symTableAddSym(acc->acc_stp, &acl->acl_sym, (void *)acl);
    if (rv < 0) goto err_addsym;

    /* Add it to the list of ACLs for the specified context */
    acl->acl_next = acc->acc_acls;
    acc->acc_acls = acl;
    acc->acc_refcnt += 1;

    *pacl = acl;
    return 0;

  err_nomem:
    rv = ACLERRNOMEM;
    eid = ACLERR3200;
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    goto done;

  err_addsym:
    FREE(acl);
    rv = ACLERRDUPSYM;
    eid = ACLERR3220;
    nserrGenerate(errp, rv, eid, ACL_Program, 1, aclname);

  done:
    return rv;
}

/*
 * Description (aclDestroy)
 *
 *	This function destroys an ACL structure and its sub-structures.
 *	It does not free the ACContext_t referenced by the ACL.
 *
 * Arguments:
 *
 *	acl			- pointer to ACL_t structure
 */

void aclDestroy(ACL_t * acl)
{
    ACL_t **pacl;		/* ACL list link pointer */
    ACDirective_t * acd;	/* ACL directive pointer */
    ACDirective_t * nacd;	/* next ACL directive pointer */

    /* Is there an ACContext_t structure? */
    if (acl->acl_acc != 0) {

	/* Remove this ACL from the list in the ACContext_t structure */
	for (pacl = &acl->acl_acc->acc_acls;
	     *pacl != 0; pacl = &(*pacl)->acl_next) {

	    if (*pacl == acl) {
		*pacl = acl->acl_next;
		acl->acl_acc->acc_refcnt -= 1;
		break;
	    }
	}
    }

    /* Destroy each ACL directive */
    for (acd = acl->acl_dirf; acd != 0; acd = nacd) {
	nacd = acd->acd_next;
	aclDirectiveDestroy(acd);
    }

    /* Free the ACL rights list if it is unnamed */
    if ((acl->acl_rights != 0) && (acl->acl_rights->rs_sym.sym_name == 0)) {
	aclRightSpecDestroy(acl->acl_rights);
    }

    /* Free the ACL name string, if any */
    if (acl->acl_sym.sym_name != 0) {
	FREE(acl->acl_sym.sym_name);
    }

    /* Free the ACL itself */
    FREE(acl);
}

/*
 * Description (aclDelete)
 *
 *	This function removes a specified ACL from the symbol table
 *	associated with its ACL context, and then destroys the ACL
 *	structure and any unnamed objects it references (other than
 *	the ACL context).
 *
 * Arguments:
 *
 *	acl			- pointer to the ACL
 */

void aclDelete(ACL_t * acl)
{
    ACContext_t * acc = acl->acl_acc;

    if ((acc != 0) && (acl->acl_sym.sym_name != 0)) {
	symTableRemoveSym(acc->acc_stp, &acl->acl_sym);
    }

    aclDestroy(acl);
}

/*
 * Description (aclDirectiveAdd)
 *
 *	This function adds a given directive to a specified ACL.
 *
 * Arguments:
 *
 *	acl			- pointer to the ACL
 *	acd			- pointer to the directive to be added
 *
 * Returns:
 *
 *	If successful, the return value is zero.  An error is indicated
 *	by a negative return value.
 */

int aclDirectiveAdd(ACL_t * acl, ACDirective_t * acd)
{
    /* Add the directive to the end of the ACL's directive list */
    acd->acd_next = 0;

    if (acl->acl_dirl == 0) {
	/* First entry in empty list */
	acl->acl_dirf = acd;
    }
    else {
	/* Append to end of list */
	acl->acl_dirl->acd_next = acd;
    }

    acl->acl_dirl = acd;

    return 0;
}

/*
 * Description (aclDirectiveCreate)
 *
 *	This function allocates and initializes a new ACDirective_t
 *	structure, representing an ACL directive.
 *
 * Arguments:
 *
 *	None.
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a new ACDirective_t.
 *	Otherwise the return value is null.
 */

ACDirective_t * aclDirectiveCreate()
{
    ACDirective_t * acd;

    acd = (ACDirective_t *) MALLOC(sizeof(ACDirective_t));
    if (acd != 0) {
	memset((void *)acd, 0, sizeof(ACDirective_t));
    }

    return acd;
}

/*
 * Description (aclDirectiveDestroy)
 *
 *	This function destroys an ACL directive structure.
 *
 * Arguments:
 *
 *	acd			- pointer to ACL directive structure
 */

void aclDirectiveDestroy(ACDirective_t * acd)
{
    switch (acd->acd_action) {
      case ACD_ALLOW:
      case ACD_DENY:
	{
	    ACClients_t * acp;
	    ACClients_t * nacp;

	    /* Free a list of ACClients_t structures */
	    for (acp = acd->acd_cl; acp != 0; acp = nacp) {
		nacp = acp->cl_next;

		/* Free the HostSpec_t if it's there and unnamed */
		if ((acp->cl_host != 0) &&
		    (acp->cl_host->hs_sym.sym_name == 0)) {
		    aclHostSpecDestroy(acp->cl_host);
		}

		/* Free the UserSpec_t if it's there and unnamed */
		if ((acp->cl_user != 0) &&
		    (acp->cl_user->us_sym.sym_name == 0)) {
		    aclUserSpecDestroy(acp->cl_user);
		}
	    }
	}
	break;

      case ACD_AUTH:
	{
	    RealmSpec_t * rsp = acd->acd_auth.au_realm;

	    /* Destroy the RealmSpec_t if it's unnamed */
	    if ((rsp != 0) && (rsp->rs_sym.sym_name == 0)) {
		aclRealmSpecDestroy(rsp);
	    }
	}
	break;
    }

    FREE(acd);
}

/*
 * Description (aclDNSSpecDestroy)
 *
 *	This function destroys an entry in a DNS filter.  It is intended
 *	mainly to be used by aclHostSpecDestroy().
 *
 * Arguments:
 *
 *	sym			- pointer to Symbol_t for DNS filter entry
 *	argp			- unused (must be zero)
 *
 * Returns:
 *
 *	The return value is SYMENUMREMOVE.
 */

int aclDNSSpecDestroy(Symbol_t * sym, void * argp)
{
    if (sym != 0) {

	/* Free the DNS specification string if any */
	if (sym->sym_name != 0) {
	    FREE(sym->sym_name);
	}

	/* Free the Symbol_t structure */
	FREE(sym);
    }

    /* Indicate that the symbol table entry should be removed */
    return SYMENUMREMOVE;
}

/*
 * Description (aclHostSpecDestroy)
 *
 *	This function destroys a HostSpec_t structure and its sub-structures.
 *
 * Arguments:
 *
 *	hsp			- pointer to HostSpec_t structure
 */

void aclHostSpecDestroy(HostSpec_t * hsp)
{
    if (hsp == 0) return;

    /* Free the IP filter if any */
    if (hsp->hs_host.inh_ipf.ipf_tree != 0) {
	IPNode_t * ipn;			/* current node pointer */
	IPNode_t * parent;		/* parent node pointer */
	int i;

	/* Traverse tree, freeing nodes */
	for (parent = hsp->hs_host.inh_ipf.ipf_tree; parent != NULL; ) {

	    /* Look for a link to a child node */
	    for (i = 0; i < IPN_NLINKS; ++i) {
		ipn = parent->ipn_links[i];
		if (ipn != NULL) break;
	    }

	    /* Any children for the parent node? */
	    if (ipn == NULL) {

		/* Otherwise back up the tree */
		ipn = parent;
		parent = ipn->ipn_parent;

		/* Free the lower node */
		FREE(ipn);
		continue;
	    }

	    /*
	     * Found a child node for the current parent.
	     * NULL out the downward link and check it out.
	     */
	    parent->ipn_links[i] = NULL;

	    /* Is it a leaf? */
	    if (ipn->ipn_type == IPN_LEAF) {
		/* Yes, free it */
		FREE(ipn);
		continue;
	    }

	    /* No, step down the tree */
	    parent = ipn;
	}
    }

    /* Free the DNS filter if any */
    if (hsp->hs_host.inh_dnf.dnf_hash != 0) {

	/* Destroy each entry in the symbol table */
	symTableEnumerate(hsp->hs_host.inh_dnf.dnf_hash, 0,
			  aclDNSSpecDestroy);

	/* Destroy the symbol table itself */
	symTableDestroy(hsp->hs_host.inh_dnf.dnf_hash, 0);
    }

    /* Free the symbol name if any */
    if (hsp->hs_sym.sym_name != 0) {
	FREE(hsp->hs_sym.sym_name);
    }

    /* Free the HostSpec_t structure */
    FREE(hsp);
}

/*
 * Description (aclRealmSpecDestroy)
 *
 *	This function destroys a RealmSpec_t structure.
 *
 * Arguments:
 *
 *	rsp			- pointer to RealmSpec_t structure
 */

void aclRealmSpecDestroy(RealmSpec_t * rsp)
{
    /* Close the realm authentication database if it appears open */
    if ((rsp->rs_realm.rlm_aif != 0) &&
	(rsp->rs_realm.rlm_authdb != 0)) {
	(*rsp->rs_realm.rlm_aif->aif_close)(rsp->rs_realm.rlm_authdb, 0);
    }

    /* Free the prompt string if any */
    if (rsp->rs_realm.rlm_prompt != 0) {
	FREE(rsp->rs_realm.rlm_prompt);
    }

    /* Free the database filename string if any */
    if (rsp->rs_realm.rlm_dbname != 0) {
	FREE(rsp->rs_realm.rlm_dbname);
    }

    /* Free the realm specification name if any */
    if (rsp->rs_sym.sym_name != 0) {
	FREE(rsp->rs_sym.sym_name);
    }

    /* Free the RealmSpec_t structure */
    FREE(rsp);
}

/*
 * Description (aclRightDef)
 *
 *	This function find or creates an access right with a specified
 *	name in a given ACL context.  If a new access right definition
 *	is created, it assigns a unique integer identifier to the the
 *	right, adds it to the ACL context symbol table and to the
 *	list of all access rights for the context.  Note that access
 *	right names are case-insensitive.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acc			- pointer to an access control context
 *	rname			- access right name (e.g. "GET")
 *	prd			- pointer to returned RightDef_t pointer
 *				  (may be null)
 *
 * Returns:
 *
 *	The return value is zero if the access right definition already
 *	existed or one if it was created successfully.  Otherwise it is
 *	a negative error code (ACLERRxxxx - see aclerror.h), and an error
 *	frame will be generated if an error list is provided.
 */

int aclRightDef(NSErr_t * errp,
		ACContext_t * acc, char * rname, RightDef_t **prd)
{
    RightDef_t * rdp;			/* pointer to right definition */
    int eid;				/* error id code */
    int rv;				/* result value */
    static int last_rid = 0;		/* last assigned right id */

    /* See if there's already a symbol table entry for it */
    rv = symTableFindSym(acc->acc_stp, rname, ACLSYMRIGHT, (void **)&rdp);
    if (rv) {

	/* No, create an entry */

	/* Allocate a right definition structure and initialize it */
	rdp = (RightDef_t *)MALLOC(sizeof(RightDef_t));
	if (rdp == 0) goto err_nomem;

	rdp->rd_sym.sym_name = STRDUP(rname);
	rdp->rd_sym.sym_type = ACLSYMRIGHT;
	rdp->rd_next = acc->acc_rights;
	rdp->rd_id = ++last_rid;

	/* Add the right name to the symbol table for the ACL context */
	rv = symTableAddSym(acc->acc_stp, &rdp->rd_sym, (void *)rdp);
	if (rv) goto err_stadd;

	/* Add the right definition to the list for the ACL context */
	acc->acc_rights = rdp;

	/* Indicate a new right definition was created */
	rv = 1;
    }

    /* Return a pointer to the RightDef_t structure if indicated */
    if (prd != 0) *prd = rdp;

    return rv;

  err_nomem:
    eid = ACLERR3600;
    rv = ACLERRNOMEM;
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    goto punt;

  err_stadd:
    FREE(rdp->rd_sym.sym_name);
    FREE(rdp);
    eid = ACLERR3620;
    rv = ACLERRDUPSYM;
    nserrGenerate(errp, rv, eid, ACL_Program, 1, rname);

  punt:
    return rv;
}

/*
 * Description (aclRightSpecDestroy)
 *
 *	This function destroys a RightSpec_t structure.
 *
 * Arguments:
 *
 *	rsp			- pointer to RightSpec_t structure
 */

void aclRightSpecDestroy(RightSpec_t * rsp)
{
    if (rsp != 0) {

	UILFREE(&rsp->rs_list);

	if (rsp->rs_sym.sym_name != 0) {
	    FREE(rsp->rs_sym.sym_name);
	}

	FREE(rsp);
    }
}

/*
 * Description (aclUserSpecCreate)
 *
 *	This function allocates and initializes a new UserSpec_t
 *	structure, representing a list of users and groups.
 *
 * Arguments:
 *
 *	None.
 *
 * Returns:
 *
 *	If successful, the return value is a pointer to a new UserSpec_t.
 *	Otherwise the return value is null.
 */

UserSpec_t * aclUserSpecCreate()
{
    UserSpec_t * usp;

    usp = (UserSpec_t *) MALLOC(sizeof(UserSpec_t));
    if (usp != 0) {
	memset((void *)usp, 0, sizeof(UserSpec_t));
	usp->us_sym.sym_type = ACLSYMUSER;
    }

    return usp;
}

/*
 * Description (aclUserSpecDestroy)
 *
 *	This function destroys a UserSpec_t structure.
 *
 * Arguments:
 *
 *	usp			- pointer to UserSpec_t structure
 */

void aclUserSpecDestroy(UserSpec_t * usp)
{
    if (usp != 0) {

	UILFREE(&usp->us_user.uu_user);
	UILFREE(&usp->us_user.uu_group);

	if (usp->us_sym.sym_name != 0) {
	    FREE(usp->us_sym.sym_name);
	}

	FREE(usp);
    }
}
