/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Handles computed attributes for entries as they're returned to the client */

#include "slap.h"


/* Structure used to pass the context needed for completing a computed attribute operation */
struct _computed_attr_context {
	BerElement *ber;
	int attrsonly;
	char *requested_type;
	Slapi_PBlock *pb;
};


struct _compute_evaluator {
	struct _compute_evaluator *next;
	slapi_compute_callback_t function;
};
typedef struct _compute_evaluator compute_evaluator;

static compute_evaluator *compute_evaluators = NULL;
static PRRWLock *compute_evaluators_lock = NULL;

static int
compute_stock_evaluator(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn);

struct _compute_rewriter {
	struct _compute_rewriter *next;
	slapi_search_rewrite_callback_t function;
};
typedef struct _compute_rewriter compute_rewriter;

static compute_rewriter *compute_rewriters = NULL;
static PRRWLock *compute_rewriters_lock = NULL;

/* Function called by evaluators to have the value output */
static int
compute_output_callback(computed_attr_context *c,Slapi_Attr *a , Slapi_Entry *e)
{
	return encode_attr (c->pb, c->ber, e, a, c->attrsonly, c->requested_type);
}

static int
compute_call_evaluators(computed_attr_context *c,slapi_compute_output_t outfn,char *type,Slapi_Entry *e)
{
	int rc = -1;
	compute_evaluator *current = NULL;
	/* Walk along the list (locked) calling the evaluator functions util one says yes, an error happens, or we finish */
	PR_RWLock_Rlock(compute_evaluators_lock);
	for (current = compute_evaluators; (current != NULL) && (-1 == rc); current = current->next) {
		rc = (*(current->function))(c,type,e,outfn);
	}
	PR_RWLock_Unlock(compute_evaluators_lock);
	return rc;
}

/* Returns : -1 if no attribute matched the requested type */
/*			 0  if one matched and it was processed without error */
/*           >0 if an error happened */
int
compute_attribute(char *type, Slapi_PBlock *pb,BerElement *ber,Slapi_Entry *e,int attrsonly,char *requested_type)
{
	computed_attr_context context;

	context.ber = ber;
	context.attrsonly = attrsonly;
	context.requested_type = requested_type;
	context.pb = pb;

	return compute_call_evaluators(&context,compute_output_callback,type,e);
}

static int
compute_stock_evaluator(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn)
{
	int rc= -1;
	static char* subschemasubentry = "subschemasubentry";

	if ( strcasecmp (type, subschemasubentry ) == 0)
	{
		Slapi_Attr our_attr;
		slapi_attr_init(&our_attr, subschemasubentry);
		our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
		valueset_add_string(&our_attr.a_present_values,SLAPD_SCHEMA_DN,CSN_TYPE_UNKNOWN,NULL);
		rc = (*outputfn) (c, &our_attr, e);
		attr_done(&our_attr);
		return (rc);
	}
	return rc; /* I see no ships */
}

int slapi_compute_add_evaluator(slapi_compute_callback_t function)
{
	int rc = 0;
	compute_evaluator *new_eval = NULL;
	PR_ASSERT(NULL != function);
	PR_ASSERT(NULL != compute_evaluators_lock);
	PR_RWLock_Wlock(compute_evaluators_lock);
	new_eval = calloc(1,sizeof (compute_evaluator));
	if (NULL == new_eval) {
		rc = ENOMEM;
	} else {
		new_eval->next = compute_evaluators;
		new_eval->function = function;
		compute_evaluators = new_eval;
	}
	PR_RWLock_Unlock(compute_evaluators_lock);
	return rc;
}

/* Call this on server startup, before the first LDAP operation is serviced */
int compute_init()
{
	/* Initialize the lock */
	compute_evaluators_lock = PR_NewRWLock( PR_RWLOCK_RANK_NONE, "compute_attr_lock" );
	if (NULL == compute_evaluators_lock) {
		/* Out of resources */
		return ENOMEM;
	}
	compute_rewriters_lock = PR_NewRWLock( PR_RWLOCK_RANK_NONE, "compute_rewriters_lock" );
	if (NULL == compute_rewriters_lock) {
		/* Out of resources */
		return ENOMEM;
	}
	/* Now add the stock evaluators to the list */
	return slapi_compute_add_evaluator(compute_stock_evaluator);	
}

/* Call this on server shutdown, after the last LDAP operation has
terminated */
int compute_terminate()
{
	/* Free the list */
	if (NULL != compute_evaluators_lock) {
		compute_evaluator *current = compute_evaluators;
		PR_RWLock_Wlock(compute_evaluators_lock);
		while (current != NULL) {
			compute_evaluator *asabird = current;
			current = current->next;
			free(asabird);
		}
		PR_RWLock_Unlock(compute_evaluators_lock);
		/* Free the lock */
		PR_DestroyRWLock(compute_evaluators_lock);
	}
	if (NULL != compute_rewriters_lock) {
		compute_rewriter *current = compute_rewriters;
		PR_RWLock_Wlock(compute_rewriters_lock);
		while (current != NULL) {
			compute_rewriter *asabird = current;
			current = current->next;
			free(asabird);
		}
		PR_RWLock_Unlock(compute_rewriters_lock);
		PR_DestroyRWLock(compute_rewriters_lock);
	}	
	return 0;
}

/* Functions dealing with re-writing of search filters */

int slapi_compute_add_search_rewriter(slapi_search_rewrite_callback_t function)
{
	int rc = 0;
	compute_rewriter *new_rewriter = NULL;
	PR_ASSERT(NULL != function);
	PR_ASSERT(NULL != compute_rewriters_lock);
	new_rewriter = calloc(1,sizeof (compute_rewriter));
	if (NULL == new_rewriter) {
		rc = ENOMEM;
	} else {
		PR_RWLock_Wlock(compute_rewriters_lock);
		new_rewriter->next = compute_rewriters;
		new_rewriter->function = function;
		compute_rewriters = new_rewriter;
		PR_RWLock_Unlock(compute_rewriters_lock);
	}
	return rc;
}

int	compute_rewrite_search_filter(Slapi_PBlock *pb)
{
	/* Iterate through the listed rewriters until one says it matched */
	int rc = -1;
	compute_rewriter *current = NULL;
	/* Walk along the list (locked) calling the evaluator functions util one says yes, an error happens, or we finish */
	PR_RWLock_Rlock(compute_rewriters_lock);
	for (current = compute_rewriters; (current != NULL) && (-1 == rc); current = current->next) {
		rc = (*(current->function))(pb);
		/* Meaning of the return code :
		 -1 : keep looking
		  0 : rewrote OK
		  1 : refuse to do this search
		  2 : operations error
		 */
	}
	PR_RWLock_Unlock(compute_rewriters_lock);
	return rc;

}


