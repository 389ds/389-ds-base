/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright 2001 Netscape Communications Corp.
 * All rights reserved.
 */
#include <ctype.h>
#include <string.h>
#include "slapi-plugin.h"

/*
 * These are examples of distribution function as declared in mapping tree node
 * This function will be called for every operations
 * reaching this node, including subtree search operations that are started
 * above this node
 * 
 * Parameters :
 * . pb is the pblock of the operation
 * . target_dn is the target DN of the operation
 * . mtn_be_names is the list of names of backends declared for this node
 * . be_count is the number of backends declared
 * . node_dn is the node where the distribution function is set
 * 
 * The return value of the functions should be the indice of the backend
 * in the mtn_be_names table
 * For search operation, the value SLAPI_BE_ALL_BACKENDS can be used to
 * specify that all backends must be searched
 * The use of value SLAPI_BE_ALL_BACKENDS for operation other than search
 * is not supported and may give random results
 * 
 */

/*
 * Distribute the entries based on the first letter of their rdn
 *
 * . Entries starting with anything other that a-z or A-Z will always 
 *   go in backend 0 
 * . Entries starting with letter (a-z or A-Z) will be shared between
 *   the backends depending following the alphabetic order
 * Example : if 3 backends are used, entries starting with A-I will go
 * in backend 0, entries starting with J-R will go in backend 1, entries
 * starting with S-Z will go in backend 2
 *
 * Of course this won't work for all locales...
 *
 * This example only works for a flat namespace below the node DN
 */
int alpha_distribution(Slapi_PBlock *pb, Slapi_DN * target_dn,
	 char **mtn_be_names, int be_count, Slapi_DN * node_dn)
{
	unsigned long op_type;
	Slapi_Operation *op;
	char *rdn_type;
    char *rdn_value;
	Slapi_RDN *rdn = NULL;
	char c;

	/* first check the operation type
	 * searches at node level or above it should go in all backends 
	 * searches below node level should go in only one backend
	 */
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	op_type = slapi_op_get_type(op);
	if ((op_type == SLAPI_OPERATION_SEARCH) &&
				 	slapi_sdn_issuffix(node_dn, target_dn))
		return SLAPI_BE_ALL_BACKENDS;

	/* now choose the backend 
	 * anything starting with a char different from a-z or A-Z will 
	 * go in backend 0
	 */

	/* get the first char of first value of rdn */
	rdn = slapi_rdn_new();
	slapi_sdn_get_rdn(target_dn, rdn);
	slapi_rdn_get_first(rdn, &rdn_type, &rdn_value);
	c = rdn_value[0];
	
	if (!(((c >= 'a') && (c <= 'z')) ||
		 ((c >= 'A') && (c <= 'Z')) ))
	{
		return 0;
	}

	slapi_rdn_free(&rdn);

	/* for entries with rdn starting with alphabetic characters 
	 * use the formula :  (c - 'A') * be_count/26 
	 * to calculate the backend number 
	 */
	return (toupper(c) - 'A') * be_count/26;
}

/*
 * Distribute the entries based on a simple hash algorithme
 */
int hash_distribution(Slapi_PBlock *pb, Slapi_DN * target_dn,
	 char **mtn_be_names, int be_count, Slapi_DN * node_dn)
{
	unsigned long op_type;
	Slapi_Operation *op;
	char *rdn_type;
    char *rdn_value;
	Slapi_RDN *rdn = NULL;
	int hash_value;

	/* first check the operation type
	 * searches at node level or above it should go in all backends 
	 * searches below node level should go in only one backend
	 */
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	op_type = slapi_op_get_type(op);
	if ((op_type == SLAPI_OPERATION_SEARCH) &&
				 	slapi_sdn_issuffix(node_dn, target_dn))
		return SLAPI_BE_ALL_BACKENDS;

	/* now choose the backend 
	 */

	/* get the rdn and hash it to compute the backend number
	 * use a simple hash for this example
	 */
	rdn = slapi_rdn_new();
	slapi_sdn_get_rdn(target_dn, rdn);
	slapi_rdn_get_first(rdn, &rdn_type, &rdn_value);

	/* compute the hash value */
	hash_value = 0;
	while (*rdn_value)
	{
		hash_value += *rdn_value;
		rdn_value++;
	}
	hash_value = hash_value % be_count;

	slapi_rdn_free(&rdn);

	/* use the hash_value as the returned backend number */
	return hash_value;
}

/*
 * This plugin allows to use a local backend in conjonction with
 * a chaining backend
 * The ldbm backend is considered a read-only replica of the data
 * The chaining backend point to a red-write replica of the data
 * This distribution logic forward the update request to the chaining 
 * backend, and send the search request to the local dbm database
 * 
 * The mechanism for updating the local read-only replica is not 
 * taken into account by this plugin
 *
 * To be able to use it one must define one ldbm backend and one chaining
 * backend in the mapping tree node
 * 
 */
int chaining_distribution(Slapi_PBlock *pb, Slapi_DN * target_dn,
	 char **mtn_be_names, int be_count, Slapi_DN * node_dn)
{
	char * requestor_dn;
	unsigned long op_type;
	Slapi_Operation *op;
	int repl_op = 0;
	int local_backend = -1;
	int chaining_backend = -1;
	int i;
	char * name;

	/* first, we have to decide which backend is the local backend
	 * and which is the chaining one
	 * For the purpose of this example use the backend name :
	 *  the backend with name starting with ldbm is local
	 *  the bakend with name starting with chaining is remote 
	 */
	local_backend = -1;
	chaining_backend = -1;
	for (i=0; i<be_count; i++)
	{
		name = mtn_be_names[i];
		if ((0 == strncmp(name, "ldbm", 4)) ||
		    (0 == strncmp(name, "user", 4)))
			local_backend = i;
		else if (0 == strncmp(name, "chaining", 8))
			chaining_backend = i;
	}

	/* Check the operation type
	 * read-only operation will go to the local backend
	 * updates operation will go to the chaining backend
	 */
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
	op_type = slapi_op_get_type(op);
	if ((op_type == SLAPI_OPERATION_SEARCH) ||
	    (op_type == SLAPI_OPERATION_BIND) ||
	    (op_type == SLAPI_OPERATION_UNBIND) ||
	    (op_type == SLAPI_OPERATION_COMPARE))
		return local_backend;

	/* if the operation is done by directory manager
	 * use local database even for updates because it is an administrative
	 * operation
	 * remarks : one could also use an update DN in the same way
	 * to let update operation go to the local backend when they are done 
	 * by specific administrator user but let all the other user 
	 * go to the read-write replica
	 */
	slapi_pblock_get( pb, SLAPI_REQUESTOR_DN, &requestor_dn );
	if (slapi_dn_isroot(requestor_dn))
		return local_backend;

	/* if the operation is a replicated operation
	 * use local database even for updates to avoid infinite loops
	 */
	slapi_pblock_get (pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
	if (repl_op)
		return local_backend;

	/* all other case (update while not directory manager) :
	 * use the chaining backend 
	 */
	return chaining_backend;
}
