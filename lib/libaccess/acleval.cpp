/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (acleval.c)
 *
 *	This module provides functions for evaluating Access Control List
 *	(ACL) structures in memory.
 *
 */

#include "base/systems.h"
#include "netsite.h"
#include "libaccess/symbols.h"
#include "libaccess/aclerror.h"
#include "libaccess/acleval.h"
#include <assert.h>

/*
 * Description (RLMEQUIV)
 *
 *	Macro for realm comparison.  Both realm pointers must be non-null.
 *	The realms are equivalent if the pointers are equal, or if the
 *	authentication methods and database names are the same.  The
 *	prompt string is not considered.
 */
#define RLMEQUIV(rlm1, rlm2) (((rlm1) != 0) && ((rlm2) != 0) && \
			      (((rlm1) == (rlm2)) || \
			       (((rlm1)->rlm_ameth == (rlm2)->rlm_ameth) && \
				((rlm1)->rlm_dbname != 0) && \
				((rlm2)->rlm_dbname != 0) && \
				!strcmp((rlm1)->rlm_dbname, \
					(rlm2)->rlm_dbname))))

int aclDNSLookup(DNSFilter_t * dnf, char * dnsspec, int fqdn, char **match)
{
    char * subdns;		/* suffix of client DNS name */
    void * table;		/* hash table pointer */
    Symbol_t * sym;		/* DNS spec symbol pointer */
    int rv;			/* result value */

    fqdn = (fqdn) ? 1 : 0;

    if (match) *match = 0;

    /* Handle null or empty filter */
    if ((dnf == 0) || (dnf->dnf_hash == 0)) {

	return ACL_NOMATCH;
    }

    /* Got the client's DNS name? */
    if (!dnsspec || !*dnsspec) {
	/* No, use special one */
	dnsspec = "unknown";
    }
    
    /* Get hash table pointer */
    table = dnf->dnf_hash;

    /*
     * Look up each possible suffix for the client domain name,
     * starting with the entire string, and working toward the
     * last component.
     */

    subdns = dnsspec;

    while (subdns != 0) {

	/* Look up the domain name suffix in the hash table */
	rv = symTableFindSym(table, subdns, fqdn, (void **)&sym);
	if (rv == 0) break;

	/* Step to the next level */
	if (subdns[0] == '.') subdns += 1;
	subdns = strchr(subdns, '.');

	/* If it was fully qualified, now it's not */
	fqdn = 0;
    }

    /* One more possibility if nothing found yet... */
    if (rv) {
	rv = symTableFindSym(table, "*", 0, (void **)&sym);
    }

    if (rv == 0) {
	if (match) *match = sym->sym_name;
	rv = ACL_DNMATCH;
    }
    else rv = ACL_NOMATCH;

    return rv;
}

int aclIPLookup(IPFilter_t * ipf, IPAddr_t ipaddr, void **match)
{
    IPLeaf_t * leaf;		/* radix tree leaf pointer */
    IPAddr_t bitmask;		/* bit mask for current node */
    IPNode_t * ipn;		/* current internal node */
    IPNode_t * lastipn;		/* last internal node seen in search */
    IPNode_t * mipn;		/* ipn_masked subtree root pointer */

    if (match) *match = 0;

    /* Handle null or empty IP filter */
    if ((ipf == 0) || (ipf->ipf_tree == 0)) goto fail;

    lastipn = NULL;
    ipn = ipf->ipf_tree;

    /*
     * The tree traversal first works down the tree, under the assumption
     * that all of the bits in the given IP address may be significant.
     * The internal nodes of the tree will cause particular bits of the
     * IP address to be tested, and the ipn_clear or ipn_set link to
     * a descendant followed accordingly.  The internal nodes are arranged
     * in such a way that high-order bits are tested before low-order bits.
     * Usually some bits are skipped, as they are not needed to distinguish
     * the entries in the tree.
     *
     * At the bottom of the tree, a leaf node may be found, or the last
     * descendant link may be NULL.  If a leaf node is found, it is
     * tested for a match against the given IP address.  If it doesn't
     * match, or the link was NULL, backtracking begins, as described
     * below.
     *
     * Backtracking follows the ipn_parent links back up the tree from
     * the last internal node, looking for internal nodes with ipn_masked
     * descendants.  The subtrees attached to these links are traversed
     * downward, as before, with the same processing at the bottom as
     * the first downward traversal.  Following the ipn_masked links is
     * essentially examining the possibility that the IP address bit
     * associated with the internal node may be masked out by the
     * ipl_netmask in a leaf at the bottom of such a subtree.  Since
     * the ipn_masked links are examined from the bottom of the tree
     * to the top, this looks at the low-order bits first.
     */

    while (ipn != NULL) {

	/*
	 * Work down the tree testing bits in the IP address indicated
	 * by the internal nodes.  Exit the loop when there are no more
	 * internal nodes.
	 */
	while ((ipn != NULL) && (ipn->ipn_type == IPN_NODE)) {

	    /* Save pointer to internal node */
	    lastipn = ipn;

	    /* Get a mask for the bit this node tests */
	    bitmask = (IPAddr_t) 1<<ipn->ipn_bit;

	    /* Select link to follow for this IP address */
	    ipn = (bitmask & ipaddr) ? ipn->ipn_set : ipn->ipn_clear;
	}

	/* Did we end up with a non-NULL node pointer? */
	if (ipn != NULL) {

	    /* It must be a leaf node */
	    assert(ipn->ipn_type == IPN_LEAF);
	    leaf = (IPLeaf_t *)ipn;

	    /* Is it a matching leaf? */
	    if (leaf->ipl_ipaddr == (ipaddr & leaf->ipl_netmask)) goto win;
	}

	/*
	 * Backtrack, starting at lastipn. Search each subtree
	 * emanating from an ipn_masked link.  Step up the tree
	 * until the ipn_masked link of the node referenced by
	 * "ipf->ipf_tree" has been considered.
	 */

	for (ipn = lastipn; ipn != NULL; ipn = ipn->ipn_parent) {

	    /*
	     * Look for a node with a non-NULL masked link, but don't
	     * go back to the node we just came from.
	     */

	    if ((ipn->ipn_masked != NULL) && (ipn->ipn_masked != lastipn)) {

		/* Get the root of this subtree */
		mipn = ipn->ipn_masked;

		/* If this is an internal node, start downward traversal */
		if (mipn->ipn_type == IPN_NODE) {
		    ipn = mipn;
		    break;
		}

		/* Otherwise it's a leaf */
		assert(mipn->ipn_type == IPN_LEAF);
		leaf = (IPLeaf_t *)mipn;

		/* Is it a matching leaf? */
		if (leaf->ipl_ipaddr == (ipaddr & leaf->ipl_netmask)) goto win;
	    }

	    /* Don't consider nodes above the given root */
	    if (ipn == ipf->ipf_tree) goto fail;

	    lastipn = ipn;
	}
    }

  fail:
    /* No matching entry found */
    return ACL_NOMATCH;

  win:
    /* Found a match in leaf */
    if (match) *match = (void *)leaf;

    return ACL_IPMATCH;
}

int aclUserLookup(UidUser_t * uup, UserObj_t * uoptr)
{
    int gl1cnt;			/* elements left in uup->uu_group list */
    int gl2cnt;			/* elements left in uoptr->uo_groups list */
    USI_t * gl1ptr;		/* pointer to next group in uup->uu_group */
    USI_t * gl2ptr;		/* pointer to next group in uoptr->uo_groups */

    /* Try for a direct match on the user id */
    if (usiPresent(&uup->uu_user, uoptr->uo_uid)) {
	return ACL_USMATCH;
    }

    /*
     * Now we want to see if there are any matches between the
     * uup->uu_group group id list and the list of groups in the
     * user object.
     */

    gl1cnt = UILCOUNT(&uup->uu_group);
    gl1ptr = UILLIST(&uup->uu_group);
    gl2cnt = UILCOUNT(&uoptr->uo_groups);
    gl2ptr = UILLIST(&uoptr->uo_groups);

    while ((gl1cnt > 0) && (gl2cnt > 0)) {

	if (*gl1ptr == *gl2ptr) {
	    return ACL_GRMATCH;
	}

	if (*gl1ptr < *gl2ptr) {
	    ++gl1ptr;
	    --gl1cnt;
	}
	else {
	    ++gl2ptr;
	    --gl2cnt;
	}
    }

    return ACL_NOMATCH;
}

/*
 * Description (aclEvaluate)
 *
 *	This function evaluates a given ACL against specified client
 *	information and a particular access right that is needed to
 *	service the client.  It can optionally return the ACL directive
 *	number which allows or denies the client's access.
 *
 * Arguments:
 *
 *	acl			- pointer to ACL to evaluate
 *	arid			- desired access right id value
 *	clauth			- pointer to client authentication information
 *	padn			- pointer to returned ACL directive number
 *				  (may be null)
 *
 * Returns:
 *
 *	A return value of zero indicates that the given ACL does not
 *	control the desired access right, or contains no directives which
 *	match the specified client.  A positive return value contains a
 *	value of ACD_ALLOW, ACD_DENY, or ACD_AUTH, and may also have the
 *	ACD_ALWAYS bit flag set.  The value indicates whether the client
 *	should be allowed or denied access, or whether authentication is
 *	needed.  The ACD_ALWAYS flag indicates if the action should occur
 *	immediately, terminating any further ACL evaluation.  An error
 *	is indicated by a negative error code (ACLERRxxxx - see aclerror.h).
 */

int aclEvaluate(ACL_t * acl, USI_t arid, ClAuth_t * clauth, int * padn)
{
    ACDirective_t * acd;	/* current ACL directive pointer */
    RightSpec_t * rsp;		/* pointer to rights controlled by ACL */
    ACClients_t * csp;		/* pointer to clients specification */
    HostSpec_t * hsp;		/* pointer to host specification */
    UserSpec_t * usp;		/* pointer to user specification */
    Realm_t * rlm = 0;		/* current authentication realm pointer */
    Realm_t * authrlm = 0;	/* realm to be used for authentication */
    int ndir;			/* ACL directive number */
    int rv;			/* result value */
    int decision = 0;		/* current access control decision */
    int result = 0;		/* function return value */
    int mdn = 0;		/* matching directive number */

    if (padn) *padn = 0;

    /* Does this ACL control the desired access right? */

    rsp = acl->acl_rights;
    if ((rsp == 0) || !usiPresent(&rsp->rs_list, arid)) {

	/* No, nothing to do */
	return 0;
    }

    ndir = 0;

    /* Loop on each ACL directive */
    for (acd = acl->acl_dirf; acd != 0; acd = acd->acd_next) {

	/* Bump directive number */
	++ndir;

	/* Dispatch on directive action code */
	switch (acd->acd_action) {

	  case ACD_ALLOW:
	  case ACD_DENY:

	    /* Loop to process list of client specifications */
	    for (csp = acd->acd_cl; csp != 0; csp = csp->cl_next) {

		/* Is there a host list? */
		hsp = csp->cl_host;
		if (hsp != 0) {

		    /* An empty host list will not match */
		    rv = 0;

		    /* Yes, is there an IP address filter? */
		    if (hsp->hs_host.inh_ipf.ipf_tree != 0) {

			/*
			 * Yes, see if the the client's IP address
			 * matches anything in the IP filter.
			 */
			rv = aclIPLookup(&hsp->hs_host.inh_ipf,
					 clauth->cla_ipaddr, 0);
		    }

		    /* If no IP match, is there a DNS filter? */
		    if (!rv && (hsp->hs_host.inh_dnf.dnf_hash != 0)) {

			/* Yes, try for a DNS match */
			rv = aclDNSLookup(&hsp->hs_host.inh_dnf,
					  clauth->cla_dns, 1, 0);
		    }

		    /*
		     * Does the client match the host list?  If not, skip
		     * to the next client specification.
		     */
		    if (!rv) continue;
		}

		/* Is there a user list? */
		usp = csp->cl_user;
		if (usp != 0) {

		    /* Yes, has the client user been authenticated yet? */
		    if ((clauth->cla_realm != 0) && (clauth->cla_uoptr != 0)) {

			/*
			 * Yes, has the client user been authenticated in the
			 * realm associated with this user list?
			 */
			if (RLMEQUIV(rlm, clauth->cla_realm)) {

			    /*
			     * Yes, does the user spec allow all
			     * authenticated users?
			     */
			    rv = (usp->us_flags & ACL_USALL) ? ACL_GRMATCH : 0;
			    if (!rv) {

				/*
				 * No, need to check client user against list.
				 */
				rv = aclUserLookup(&usp->us_user,
						   clauth->cla_uoptr);
			    }

			    /* Got a match yet? */
			    if (rv) {

				/*
				 * Yes, update the the access control decision,
				 * clearing any pending authentication request
				 * flag.
				 */
				authrlm = 0;
				decision = acd->acd_action;

				/* Copy the "always" flag to the result */
				result = (acd->acd_flags & ACD_ALWAYS);
				mdn = ndir;
			    }
			}
			else {

			    /*
			     * The client has been authenticated already,
			     * but not in the realm used by this directive.
			     * Since directives in a given ACL are not
			     * independent policy statements, it seems that
			     * the proper thing to do here is to reject
			     * this ACL in its entirity.  This case is not
			     * an authentication failure per se, but rather
			     * an inability to evaluate this particular
			     * ACL directive which requires authentication.
			     */
			    return 0;
			}
		    }
		    else {

			/*
			 * The client user has not been authenticated in this
			 * realm yet, but could potentially be one of the
			 * users on this user list.  This directive is
			 * therefore "potentially matching".  The question
			 * is: would it change the current decision to allow
			 * or deny the client if the client user actually did
			 * match the user list?
			 */
			if ((authrlm == 0) && (decision != acd->acd_action)) {

			    /*
			     * Yes, set the "request authentication" flag,
			     * along with ACD_ALWAYS if it is set in the
			     * directive.
			     */
			    authrlm = rlm;
			    decision = ACD_AUTH;
			    result = (acd->acd_flags & ACD_ALWAYS);
			    mdn = ndir;
			}
		    }
		}
		else {

		    /*
		     * There is no user list.  Therefore any user,
		     * authenticated or not, is considered a match.
		     * Update the decision, and clear the
		     * "authentication requested" flag.
		     */
		    authrlm = 0;
		    decision = acd->acd_action;
		    result = (acd->acd_flags & ACD_ALWAYS);
		    mdn = ndir;
		}

		/*
		 * If we hit a client specification that requires
		 * immediate action, exit the loop.
		 */
		if (result & ACD_ALWAYS) break;
	    }
	    break;

	  case ACD_AUTH:

	    /* Got a pointer to a realm specification? */
	    if (acd->acd_auth.au_realm != 0) {

		/* Yes, update the current realm pointer */
		rlm = &acd->acd_auth.au_realm->rs_realm;

		/* Has the client already successfully authenticated? */
		if ((clauth->cla_realm == 0) || (clauth->cla_uoptr == 0)) {

		    /*
		     * No, if this is an "always" directive, override any
		     * previously selected realm and request authentication.
		     */
		    if ((acd->acd_flags & ACD_ALWAYS) != 0) {

			/* Set decision to request authentication */
			authrlm = rlm;
			decision = ACD_AUTH;
			result = ACD_ALWAYS;
			mdn = ndir;
		    }
		}
	    }
	    break;

	  case ACD_EXEC:

	    /* Conditionally terminate ACL evaluation */
	    switch (decision) {
	      case ACD_ALLOW:
		if (acd->acd_flags & ACD_EXALLOW) {
		    result = (acd->acd_flags & ACD_ALWAYS);
		    goto out;
		}
		break;
	      case ACD_DENY:
		if (acd->acd_flags & ACD_EXDENY) {
		    result = (acd->acd_flags & ACD_ALWAYS);
		    goto out;
		}
		break;
	      case ACD_AUTH:
		if (acd->acd_flags & ACD_EXAUTH) {
		    result = (acd->acd_flags & ACD_ALWAYS);
		    goto out;
		}
		break;
	      default:
		break;
	    }
	    break;
		
	  default:
	    break;
	}

	/*
	 * If we hit a directive that requires immediate action, exit
	 * the loop.
	 */
	if (result & ACD_ALWAYS) break;
    }

  out:
    /* If the decision is to request authentication, set the desired realm */
    if (decision == ACD_AUTH) {
	clauth->cla_realm = authrlm;
    }

    /* Combine decision with flags already in result */
    result |= decision;

    /* Return matching directive number if desired */
    if (padn) *padn = mdn;

    return result;
}
