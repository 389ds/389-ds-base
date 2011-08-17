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
static Slapi_RWLock *compute_evaluators_lock = NULL;

static int
compute_stock_evaluator(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn);

struct _compute_rewriter {
	struct _compute_rewriter *next;
	slapi_search_rewrite_callback_t function;
};
typedef struct _compute_rewriter compute_rewriter;

static compute_rewriter *compute_rewriters = NULL;
static Slapi_RWLock *compute_rewriters_lock = NULL;

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
	slapi_rwlock_rdlock(compute_evaluators_lock);
	for (current = compute_evaluators; (current != NULL) && (-1 == rc); current = current->next) {
		rc = (*(current->function))(c,type,e,outfn);
	}
	slapi_rwlock_unlock(compute_evaluators_lock);
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
	slapi_rwlock_wrlock(compute_evaluators_lock);
	new_eval = (compute_evaluator *)slapi_ch_calloc(1,sizeof (compute_evaluator));
	if (NULL == new_eval) {
		rc = ENOMEM;
	} else {
		new_eval->next = compute_evaluators;
		new_eval->function = function;
		compute_evaluators = new_eval;
	}
	slapi_rwlock_unlock(compute_evaluators_lock);
	return rc;
}

/* Call this on server startup, before the first LDAP operation is serviced */
int compute_init()
{
	/* Initialize the lock */
	compute_evaluators_lock = slapi_new_rwlock();
	if (NULL == compute_evaluators_lock) {
		/* Out of resources */
		return ENOMEM;
	}
	compute_rewriters_lock = slapi_new_rwlock();
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
		slapi_rwlock_wrlock(compute_evaluators_lock);
		while (current != NULL) {
			compute_evaluator *asabird = current;
			current = current->next;
			slapi_ch_free((void **)&asabird);
		}
		slapi_rwlock_unlock(compute_evaluators_lock);
		/* Free the lock */
		slapi_destroy_rwlock(compute_evaluators_lock);
	}
	if (NULL != compute_rewriters_lock) {
		compute_rewriter *current = compute_rewriters;
		slapi_rwlock_wrlock(compute_rewriters_lock);
		while (current != NULL) {
			compute_rewriter *asabird = current;
			current = current->next;
			slapi_ch_free((void **)&asabird);
		}
		slapi_rwlock_unlock(compute_rewriters_lock);
		slapi_destroy_rwlock(compute_rewriters_lock);
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
	new_rewriter = (compute_rewriter *)slapi_ch_calloc(1,sizeof (compute_rewriter));
	if (NULL == new_rewriter) {
		rc = ENOMEM;
	} else {
		slapi_rwlock_wrlock(compute_rewriters_lock);
		new_rewriter->next = compute_rewriters;
		new_rewriter->function = function;
		compute_rewriters = new_rewriter;
		slapi_rwlock_unlock(compute_rewriters_lock);
	}
	return rc;
}

int	compute_rewrite_search_filter(Slapi_PBlock *pb)
{
	/* Iterate through the listed rewriters until one says it matched */
	int rc = -1;
	compute_rewriter *current = NULL;
	/* Walk along the list (locked) calling the evaluator functions util one says yes, an error happens, or we finish */
	slapi_rwlock_rdlock(compute_rewriters_lock);
	for (current = compute_rewriters; (current != NULL) && (-1 == rc); current = current->next) {
		rc = (*(current->function))(pb);
		/* Meaning of the return code :
		 -1 : keep looking
		  0 : rewrote OK
		  1 : refuse to do this search
		  2 : operations error
		 */
	}
	slapi_rwlock_unlock(compute_rewriters_lock);
	return rc;

}


