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
/* Code to implement result sorting */

#include "back-ldbm.h"

#define CHECK_INTERVAL 10 /* The frequency whith which we'll check the admin limits */

/* Structure to carry the things we need down the call stack */
struct baggage_carrier {
	backend *be; /* For id2entry */
	Slapi_PBlock *pb; /* For slapi_op_abandoned */
	time_t	stoptime; /* For timelimit policing */
	int lookthrough_limit;
	int check_counter; /* Used to avoid checking every 100ns */
};
typedef struct baggage_carrier baggage_carrier;

static int slapd_qsort (baggage_carrier *bc,IDList *list,sort_spec *s);
static int print_out_sort_spec(char* buffer,sort_spec *s,int *size);

static void sort_spec_thing_free(sort_spec_thing *s)
{
	if (NULL != s->type) {
		slapi_ch_free((void **)&s->type);
	}
	if (NULL != s->matchrule) {
		slapi_ch_free( (void**)&s->matchrule);
	}
	if (NULL != s->mr_pb) {
		destroy_matchrule_indexer(s->mr_pb);
	    slapi_pblock_destroy (s->mr_pb);
	}
	slapi_ch_free( (void**)&s);
}

static sort_spec_thing *sort_spec_thing_allocate()
{
	return (sort_spec_thing *) slapi_ch_calloc(1,sizeof (sort_spec_thing));
}

void sort_spec_free(sort_spec *s)
{
	/* Walk down the list freeing */
	sort_spec_thing *t = (sort_spec_thing*)s;
	sort_spec_thing *p = NULL;
	do {
		p = t->next;
		sort_spec_thing_free(t);
		t = p;
	} while (p);
}

static sort_spec_thing * sort_spec_thing_new(char *type, char* matchrule, int reverse)
{
	sort_spec_thing *s = sort_spec_thing_allocate();
	if (NULL == s) {
		return s;
	}
	s->type = type;
	s->matchrule = matchrule;
	s->order = reverse;
	return s;
}

void sort_log_access(Slapi_PBlock *pb,sort_spec_thing *s,IDList *candidates)
{
#define SORT_LOG_BSZ 64
#define SORT_LOG_PAD 22 /* space for the number of candidates */ 
	char stack_buffer[SORT_LOG_BSZ + SORT_LOG_PAD];
	char *buffer = NULL;
	int ret = 0;
	int size = SORT_LOG_BSZ + SORT_LOG_PAD;
	char *prefix = "SORT ";
	int prefix_size = strlen(prefix);

	buffer = stack_buffer;
	size -= PR_snprintf(buffer,sizeof(stack_buffer),"%s",prefix);
	ret = print_out_sort_spec(buffer+prefix_size,s,&size);
	if (0 != ret) {
		/* It wouldn't fit in the buffer */
		buffer = slapi_ch_malloc(prefix_size + size + SORT_LOG_PAD);
		sprintf(buffer,"%s",prefix);
		ret = print_out_sort_spec(buffer+prefix_size,s,&size);
	}
	if (candidates) {
		if (ALLIDS(candidates)) {
			sprintf(buffer+size+prefix_size,"(*)");
		} else {
			sprintf(buffer+size+prefix_size,"(%lu)",(u_long)candidates->b_nids);
		}
	}
	/* Now output it */
	ldbm_log_access_message(pb,buffer);
	if (buffer != stack_buffer) {
		slapi_ch_free( (void**)&buffer);
	}
}

/* Fix for bug # 394184, SD, 20 Jul 00 */
/* replace the hard coded return value by the appropriate LDAP error code */
/* also removed an useless if (0 == return_value) {} statement */
/* Given a candidate list and a list of sort order specifications, sort this, or cop out */
/* Returns:  0 -- sorted OK                  now is: LDAP_SUCCESS (fix for bug #394184)
 *			-1 -- protocol error             now is: LDAP_PROTOCOL_ERROR 
 *			-2 -- too hard to sort these     now is: LDAP_UNWILLING_TO_PERFORM
 *			-3 -- operation error            now is: LDAP_OPERATIONS_ERROR
 *			-4 -- timeout                    now is: LDAP_TIMELIMIT_EXCEEDED
 *			-5 -- admin limit exceeded       now is: LDAP_ADMINLIMIT_EXCEEDED
 *          -6 -- abandoned                  now is: LDAP_OTHER
 */
/* 
 * So here's the plan:
 * Plan A:  We do a regular quicksort on the entries.
 * Plan B:  Through some hint given us from on high, we
 *			determine that the entries are _already_
 *			sorted as requested, thus we do nothing !
 * Plan C:  We determine that sorting these suckers is
 *			far too hard for us to even try, so we refuse.
 */
int sort_candidates(backend *be,int lookthrough_limit,time_t time_up, Slapi_PBlock *pb, 
					IDList	*candidates, sort_spec_thing *s, char **sort_error_type)
{
	int return_value = LDAP_SUCCESS;
	baggage_carrier bc = {0};
	sort_spec_thing *this_s = NULL;

	/* We refuse to sort a non-existent IDlist */
	if (NULL == candidates) {
		return LDAP_UNWILLING_TO_PERFORM;
	}
	/* we refuse to sort a candidate list which is vast */
	if (ALLIDS(candidates)) {
		LDAPDebug( LDAP_DEBUG_TRACE, "Asked to sort ALLIDS candidate list, refusing\n",0, 0, 0 );
		return LDAP_UNWILLING_TO_PERFORM;
	}

	/* Iterate over the sort types */
	for (this_s = s; this_s; this_s=this_s->next) {
		if (NULL == this_s->matchrule) {
			void		*pi;
			int return_value = 0;
			return_value = slapi_attr_type2plugin( this_s->type, &pi );
			if (0 == return_value) {
				return_value = plugin_call_syntax_get_compare_fn( pi, &(this_s->compare_fn) );
			}
			if (return_value  != 0 ) {
				LDAPDebug( LDAP_DEBUG_TRACE, "Attempting to sort a non-ordered attribute (%s)\n",this_s->type, 0, 0 );
				/* DBDB we should set the error type here */
				return_value = LDAP_UNWILLING_TO_PERFORM;
				*sort_error_type = this_s->type;
				return return_value;
			}			
		} else {
			/* Need to---find the matching rule plugin, 
			 * tell it it needs to do ordering for this OID 
			 * see whether it agrees---if not signal error to client 
			 * Then later use it for generating ordering keys. 
			 * finally, free it up 
			 */
			return_value = create_matchrule_indexer(&this_s->mr_pb,this_s->matchrule,this_s->type);
			if (LDAP_SUCCESS != return_value) {
				*sort_error_type = this_s->type;
				return return_value;
			}
			this_s->compare_fn = slapi_berval_cmp; 
		}
	}

	bc.be = be; 
	bc.pb = pb; 
	bc.stoptime = time_up;
	bc.lookthrough_limit = lookthrough_limit;
	bc.check_counter = 1;

	return_value = slapd_qsort(&bc,candidates,s);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= Sorting done\n",0, 0, 0 );

	return return_value;
}
/* End  fix for bug # 394184 */

/*  Fix for bug # 394184, SD, 20 Jul 00 */
/* fix and cleanup (switch(code) {} removed) */
/* arg 'code' has now the correct sortResult value */
int
make_sort_response_control ( Slapi_PBlock *pb, int code, char *error_type) {

	LDAPControl	new_ctrl = {0};
    BerElement		*ber= NULL;    
	struct berval	*bvp = NULL;
	int rc = -1;
	int control_code = code;

	/*
	     SortResult ::= SEQUENCE {
        sortResult  ENUMERATED {
            success                   (0), -- results are sorted
            operationsError           (1), -- server internal failure
            timeLimitExceeded         (3), -- timelimit reached before
                                           -- sorting was completed
            strongAuthRequired        (8), -- refused to return sorted
                                           -- results via insecure
                                           -- protocol
            adminLimitExceeded       (11), -- too many matching entries
                                           -- for the server to sort
            noSuchAttribute          (16), -- unrecognized attribute
                                           -- type in sort key
            inappropriateMatching    (18), -- unrecognized or inappro-
                                           -- priate matching rule in
                                           -- sort key
            insufficientAccessRights (50), -- refused to return sorted
                                           -- results to this client
            busy                     (51), -- too busy to process
            unwillingToPerform       (53), -- unable to sort
            other                    (80)
            },
      attributeType [0] AttributeType OPTIONAL }

	 */
	
    if ( ( ber = ber_alloc()) == NULL ) {
		return -1;
    }

    if (( rc = ber_printf( ber, "{e", control_code )) != -1 ) {
		if ( rc != -1 && NULL != error_type ) {
			rc = ber_printf( ber, "s", error_type );
		}
		if ( rc != -1 ) {
			rc = ber_printf( ber, "}" );
		}
    }
    if ( rc != -1 ) {
		rc = ber_flatten( ber, &bvp );
    }
    
	ber_free( ber, 1 );

    if ( rc == -1 ) {
		return rc;
    }
        
	new_ctrl.ldctl_oid = LDAP_CONTROL_SORTRESPONSE;
	new_ctrl.ldctl_value = *bvp;
	new_ctrl.ldctl_iscritical = 1;         

	if ( slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl ) != 0 ) {
            ber_bvfree(bvp);
            return( -1 );
	}

        ber_bvfree(bvp);
	return( LDAP_SUCCESS );
}
/* End fix for bug #394184 */

static int term_tag(unsigned long tag)
{
	return ( (LBER_END_OF_SEQORSET == tag) || (LBER_ERROR == tag) );
}

/* hacky function to convert a sort spec to a string 
   you specify a buffer and a size. If the thing won't fit, it returns 
   non-zero, and the size needed. Pass NULL buffer to just get the size */
static int print_out_sort_spec(char* buffer,sort_spec *s,int *size)
{
	/* Walk down the list printing */
	sort_spec_thing *t = (sort_spec_thing*)s;
	sort_spec_thing *p = NULL;
	int buffer_size = 0;
	int input_size = 0;

	if (NULL != size) {
		input_size = *size;
	}
	do {
		p = t->next;

		buffer_size += strlen(t->type);
		if (t->order) {
			buffer_size += 1; /* For the '-' */
		}
		if (NULL != t->matchrule) {
			/* space for matchrule + semicolon */
			buffer_size += strlen(t->matchrule) + 1;
		}
		buffer_size += 1; /* for the space */
		if ( (NULL != buffer) && (buffer_size <= input_size) ) {
			/* write into the buffer */
			buffer += sprintf(buffer,"%s%s%s%s ", 
				t->order ? "-" : "",
				t->type,
				( NULL == t->matchrule ) ? "" : ";",
				( NULL == t->matchrule ) ? "" : t->matchrule);
		}

		t = p;
	} while (p);
	if (NULL != size) {
		*size = buffer_size;
	}
	if (buffer_size <= input_size) {
		return 0;
	} else {
		return 1;
	}
}

int parse_sort_spec(struct berval *sort_spec_ber, sort_spec **ps)
{
	/* So here we call ber_scanf to get the sort spec */
	/* This control looks like this : 
     SortKeyList ::= SEQUENCE OF SEQUENCE {
                attributeType   AttributeType,
                orderingRule    [0] MatchingRuleId OPTIONAL,
                reverseOrder    [1] BOOLEAN DEFAULT FALSE }	 
	*/
	BerElement *ber = NULL;
	sort_spec_thing *listhead = NULL;
	unsigned long tag = 0;
	unsigned long len = 0;
	char *last = NULL;
	sort_spec_thing *listpointer = NULL;
	char *type = NULL;
	char *matchrule = NULL;
	int rc = LDAP_SUCCESS;

	ber = ber_init(sort_spec_ber);
    if(ber==NULL)
    {
        return -1;
    }

	/* Work our way along the BER, one sort spec at a time */
	for ( tag = ber_first_element( ber, &len, &last ); !term_tag(tag); tag = ber_next_element( ber, &len, last )) {
		/* we're now pointing at the beginning of a sequence of type, matching rule and reverse indicator */

		char *inner_last = NULL;
		char *rtype = NULL;
		int reverse = 0;
		unsigned long next_tag = 0;
		sort_spec_thing *s = NULL;
    	unsigned long return_value;

		next_tag = ber_first_element( ber, &len, &inner_last );

		/* The type is not optional */

		return_value = ber_scanf(ber,"a",&rtype);
		if (LBER_ERROR == return_value) {
			rc = LDAP_PROTOCOL_ERROR;
                        goto err;
		}
		/* normalize */
		type = slapi_attr_syntax_normalize(rtype);
		free(rtype);

		/* Now look for the next tag. */

		next_tag = ber_next_element(ber,&len, inner_last);

		/* Are we done ? */
		if ( !term_tag(next_tag) ) { 
			/* Is it the matching rule ? */
			if (LDAP_TAG_SK_MATCHRULE == next_tag) {
				/* If so, get it */
				ber_scanf(ber,"a",&matchrule);
				/* That can be followed by a reverse indicator */
				next_tag = ber_next_element(ber,&len, inner_last);
				if (LDAP_TAG_SK_REVERSE == next_tag) {
					/* Get the reverse sort indicator here */
					ber_scanf(ber,"b",&reverse);
					/* The protocol police say--"You must have other than your default value" */
					if (0 == reverse) {
						/* Protocol error */
						rc = LDAP_PROTOCOL_ERROR;
                                                goto err;
					}
				} else {
					/* Perhaps we're done now ? */
					if (LBER_END_OF_SEQORSET != next_tag) {
						/* Protocol error---we got a matching rule, but followed by something other 
						 * than reverse or end of sequence.
						 */
						rc = LDAP_PROTOCOL_ERROR;
                                                goto err;
					}
				}
			} else {
				/* Is it the reverse indicator ? */
				if (LDAP_TAG_SK_REVERSE == next_tag) {
					/* If so, get it */
					ber_scanf(ber,"b",&reverse);
				} else {
					/* Protocol error---tag which isn't either of the legal ones came first */
					rc = LDAP_PROTOCOL_ERROR;
                                        goto err;
				}
			}
		}

		s = sort_spec_thing_new(type,matchrule,reverse);
		if (NULL == s) {
			/* Memory allocation failed */
			rc = LDAP_OPERATIONS_ERROR;
                        goto err;
		}
                type = matchrule = NULL;
		if (NULL != listpointer) {
			listpointer->next = s;
		}
		listpointer = s;
		if (NULL == listhead) {
			listhead = s;
		}

	}

	if (NULL == listhead) {  /* LP - defect #559792 - don't return null listhead */
		*ps = NULL;
		rc = LDAP_PROTOCOL_ERROR;
		goto err;
	}

   	/* the ber encoding is no longer needed */
   	ber_free(ber,1);

	*ps = (sort_spec *)listhead;

 
	return LDAP_SUCCESS;

 err: 
        if (listhead) sort_spec_free((sort_spec*) listhead);
        slapi_ch_free((void**)&type);
        slapi_ch_free((void**)&matchrule);
        ber_free(ber,1);

        return rc;
}

#if 0
static int attr_value_compare(struct berval *value_a, struct berval *value_b)
{
	/* return value_cmp(value_a,value_b,syntax,3); */
	return strcasecmp(value_a->bv_val, value_b->bv_val);
}
#endif

struct berval* attr_value_lowest(struct berval **values, value_compare_fn_type compare_fn)
{	
	/* We iterate through the values, storing our last best guess as to the lowest */
	struct berval *lowest_so_far = values[0];
	struct berval *this_one = NULL;

	for (this_one = *values; this_one; this_one = *values++) {
		if (compare_fn(lowest_so_far,this_one) > 0) {
			lowest_so_far = this_one;
		}
	}
	return lowest_so_far;
}

int sort_attr_compare(struct berval ** value_a, struct berval ** value_b, value_compare_fn_type compare_fn)
{
	/* So, the thing we need to do here is to look out for multi-valued
	 * attributes. When we get one of those, we need to look through all the 
	 * values to find the lowest one (per X.511 edict). We then use that one to
	 * compare against the other. We should really put some logic in here to
	 * prevent us partying on an attribute with thousands of values for a long time.
	 */
	struct berval *compare_value_a = NULL;
	struct berval *compare_value_b = NULL;

	compare_value_a = attr_value_lowest(value_a, compare_fn);
	compare_value_b = attr_value_lowest(value_b, compare_fn);

	return compare_fn(compare_value_a,compare_value_b);

}


#if 0
/* USE THE _SV VERSION NOW */

/* Comparison routine, called by qsort.
 * The job here is to return the correct value
 * for the operation a < b
 * Returns:
 * <0 when  a < b
 * 0  when a == b
 * >0 when a > b
 */
static int compare_entries(ID *id_a, ID *id_b, sort_spec *s,baggage_carrier *bc, int *error)
{
	/* We get passed the IDs, but need to fetch the entries in order to
	 * perform the comparison .
	 */
	struct backentry *a = NULL;
	struct backentry *b = NULL;
	int result = 0;
	sort_spec_thing *this_one = NULL;
	int return_value = -1;
	backend *be = bc->be;
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	int err;

	*error = 1;
	a = id2entry(be,*id_a,NULL,&err);
	if (NULL == a) {
		if (0 != err ) {
			LDAPDebug(LDAP_DEBUG_ANY,"compare_entries db err %d\n",err,0,0);
		}
		/* Were up a creek without paddle here */
		/* Best to log error and set some flag */
		return 0;
	}
	b = id2entry(be,*id_b,NULL,&err);
	if (NULL == b) {
		if (0 != err ) {
			LDAPDebug(LDAP_DEBUG_ANY,"compare_entries db err %d\n",err,0,0);
		}
		return 0;
	}
	/* OK, now we have the entries, so we work our way down the attribute list comparing as we go */
	for (this_one = (sort_spec_thing*)s; this_one ; this_one = this_one->next) {

		char *type = this_one->type;
		int order = this_one->order;
		Slapi_Attr *attr_a = NULL;
		Slapi_Attr *attr_b = NULL;
		struct berval **value_a = NULL;
		struct berval **value_b = NULL;

		/* Get the two attribute values from the entries */
		return_value = slapi_entry_attr_find(a->ep_entry,type,&attr_a);
		return_value = slapi_entry_attr_find(b->ep_entry,type,&attr_b);
		/* What do we do if one or more of the entries lacks this attribute ? */
		/* if one lacks the attribute */
		if (NULL == attr_a) {
			/* then if the other does too, they're equal */
			if (NULL == attr_b) {
				result = 0;
				continue;
			} else
			{
				/* If one has the attribute, and the other
				 * doesn't, the missing attribute is the
				 * LARGER one.  (bug #108154)  -robey
				 */
				result = 1;
				break;
			}
		}
		if (NULL == attr_b) {
			result = -1;
			break;
		}
		/* Somewhere in here, we need to go sideways for match rule case 
		 * we need to call the match rule plugin to get the attribute values
		 * converted into ordering keys. Then we proceed as usual to use those,
		 * but ensuring that we don't leak memory anywhere. This works as follows:
		 * the code assumes that the attrs are references into the entry, so 
		 * doesn't try to free them. We need to note at the right place that
		 * we're on the matchrule path, and accordingly free the keys---this turns out
		 * to be when we free the indexer */ 
		if (NULL == s->matchrule) {
			/* Non-match rule case */
		    /* xxxPINAKI
		       needs modification
		       
			value_a = attr_a->a_vals;
			value_b = attr_b->a_vals;
			*/
		} else {
			/* Match rule case */
			struct berval **actual_value_b = NULL;
			struct berval **temp_value = NULL;

			/* xxxPINAKI
			   needs modification
			struct berval **actual_value_a = NULL;
			   
			actual_value_a = attr_a->a_vals;
			actual_value_b = attr_b->a_vals;
			matchrule_values_to_keys(s->mr_pb,actual_value_a,&temp_value);
			*/
			/* Now copy it, so the second call doesn't crap on it */
			value_a = slapi_ch_bvecdup(temp_value); /* Really, we'd prefer to not call the chXXX variant...*/
			matchrule_values_to_keys(s->mr_pb,actual_value_b,&value_b);
		}
		/* Compare them */
		if (!order) {
			result = sort_attr_compare(value_a, value_b, s->compare_fn);
		} else {
			/* If reverse, invert the sense of the comparison */
			result = sort_attr_compare(value_b, value_a, s->compare_fn);
		}
		/* Time to free up the attribute allocated above */
		if (NULL != s->matchrule) {
			ber_bvecfree(value_a);
		}
		/* Are they equal ? */
		if (0 != result) {
			/* If not, we're done */
			break;
		} 
		/* If so, proceed to the next attribute for comparison */
	}
	cache_return(&inst->inst_cache,&a);
	cache_return(&inst->inst_cache,&b);
	*error = 0;
	return result;
}
#endif

/* Comparison routine, called by qsort.
 * The job here is to return the correct value
 * for the operation a < b
 * Returns:
 * <0 when  a < b
 * 0  when a == b
 * >0 when a > b
 */
static int compare_entries_sv(ID *id_a, ID *id_b, sort_spec *s,baggage_carrier *bc, int *error)
{
	/* We get passed the IDs, but need to fetch the entries in order to
	 * perform the comparison .
	 */
	struct backentry *a = NULL;
	struct backentry *b = NULL;
	int result = 0;
	sort_spec_thing *this_one = NULL;
	int return_value = -1;
	backend *be = bc->be;
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	int err;

	*error = 1;
	a = id2entry(be,*id_a,NULL,&err);
	if (NULL == a) {
		if (0 != err ) {
			LDAPDebug(LDAP_DEBUG_ANY,"compare_entries db err %d\n",err,0,0);
		}
		/* Were up a creek without paddle here */
		/* Best to log error and set some flag */
		return 0;
	}
	b = id2entry(be,*id_b,NULL,&err);
	if (NULL == b) {
		if (0 != err ) {
			LDAPDebug(LDAP_DEBUG_ANY,"compare_entries db err %d\n",err,0,0);
		}
		return 0;
	}
	/* OK, now we have the entries, so we work our way down the attribute list comparing as we go */
	for (this_one = (sort_spec_thing*)s; this_one ; this_one = this_one->next) {

		char *type = this_one->type;
		int order = this_one->order;
		Slapi_Attr *attr_a = NULL;
		Slapi_Attr *attr_b = NULL;
		struct berval **value_a = NULL;
		struct berval **value_b = NULL;

		/* Get the two attribute values from the entries */
		return_value = slapi_entry_attr_find(a->ep_entry,type,&attr_a);
		return_value = slapi_entry_attr_find(b->ep_entry,type,&attr_b);
		/* What do we do if one or more of the entries lacks this attribute ? */
		/* if one lacks the attribute */
		if (NULL == attr_a) {
			/* then if the other does too, they're equal */
			if (NULL == attr_b) {
				result = 0;
				continue;
			} else
			{
				/* If one has the attribute, and the other
				 * doesn't, the missing attribute is the
				 * LARGER one.  (bug #108154)  -robey
				 */
				result = 1;
				break;
			}
		}
		if (NULL == attr_b) {
			result = -1;
			break;
		}
		/* Somewhere in here, we need to go sideways for match rule case 
		 * we need to call the match rule plugin to get the attribute values
		 * converted into ordering keys. Then we proceed as usual to use those,
		 * but ensuring that we don't leak memory anywhere. This works as follows:
		 * the code assumes that the attrs are references into the entry, so 
		 * doesn't try to free them. We need to note at the right place that
		 * we're on the matchrule path, and accordingly free the keys---this turns out
		 * to be when we free the indexer */ 
		if (NULL == s->matchrule) {
			/* Non-match rule case */
		    valuearray_get_bervalarray(valueset_get_valuearray(&attr_a->a_present_values),&value_a);
		    valuearray_get_bervalarray(valueset_get_valuearray(&attr_b->a_present_values),&value_b);
		} else {
			/* Match rule case */
			struct berval **actual_value_a = NULL;
			struct berval **actual_value_b = NULL;
			struct berval **temp_value = NULL;

			valuearray_get_bervalarray(valueset_get_valuearray(&attr_a->a_present_values),&actual_value_a);
			valuearray_get_bervalarray(valueset_get_valuearray(&attr_b->a_present_values),&actual_value_b);
			matchrule_values_to_keys(s->mr_pb,actual_value_a,&temp_value);
			/* Now copy it, so the second call doesn't crap on it */
			value_a = slapi_ch_bvecdup(temp_value); /* Really, we'd prefer to not call the chXXX variant...*/
			matchrule_values_to_keys(s->mr_pb,actual_value_b,&value_b);
			if (actual_value_a) ber_bvecfree(actual_value_a);
			if (actual_value_b) ber_bvecfree(actual_value_b);
		}
		/* Compare them */
		if (!order) {
			result = sort_attr_compare(value_a, value_b, s->compare_fn);
		} else {
			/* If reverse, invert the sense of the comparison */
			result = sort_attr_compare(value_b, value_a, s->compare_fn);
		}
		/* Time to free up the attributes allocated above */
		if (NULL != s->matchrule) {
			ber_bvecfree(value_a);
		} else {
			ber_bvecfree(value_a);
			ber_bvecfree(value_b);
                }
		/* Are they equal ? */
		if (0 != result) {
			/* If not, we're done */
			break;
		} 
		/* If so, proceed to the next attribute for comparison */
	}
	cache_return(&inst->inst_cache,&a);
	cache_return(&inst->inst_cache,&b);
	*error = 0;
	return result;
}

/* Fix for bug # 394184, SD, 20 Jul 00 */
/* replace the hard coded return value by the appropriate LDAP error code */
/* 
 * Returns:
 *  0: Everything OK          now is: LDAP_SUCCESS (fix for bug #394184)
 * -1: A protocol error       now is: LDAP_PROTOCOL_ERROR 
 * -2: Too hard               now is: LDAP_UNWILLING_TO_PERFORM
 * -3: Operation error        now is: LDAP_OPERATIONS_ERROR
 * -4: Timeout                now is: LDAP_TIMELIMIT_EXCEEDED
 * -5: Admin limit exceeded   now is: LDAP_ADMINLIMIT_EXCEEDED
 * -6: Abandoned              now is: LDAP_OTHER
 */
static int sort_nazi(baggage_carrier *bc)
{
	time_t curtime = 0;
	/* check for abandon */
	if ( slapi_op_abandoned( bc->pb)) {
	    return LDAP_OTHER;
	}

	/* Check to see if our journey is really necessary */

	if (0 == ((bc->check_counter)++ % CHECK_INTERVAL) ) {

		/* check time limit */
		curtime = current_time();
		if ( bc->stoptime != -1 && curtime > bc->stoptime ) {
			return LDAP_TIMELIMIT_EXCEEDED;
		}
			
		/* Fix for bugid #394184, SD, 05 Jul 00 */
		/*  not sure this is the appropriate place to do this;
		   since the entries are swaped in slapd_qsort, some of them are most
		   probably counted more than once */
		/* hence commenting out the following test and moving it into slapd_qsort */
		/* check lookthrough limit */
		/* if ( bc->lookthrough_limit != -1 && (bc->lookthrough_limit -= CHECK_INTERVAL) < 0 ) {
		   return LDAP_ADMINLIMIT_EXCEEDED;
		   } */
		/* end  for bugid  #394184 */

	}
	return LDAP_SUCCESS;
}
/* End fix for bug # 394184 */

/* prototypes for local routines */
static void  shortsort(baggage_carrier *bc,ID *lo, ID *hi,sort_spec *s );
static void	 swap (ID *a,ID *b);

/* this parameter defines the cutoff between using quick sort and
   insertion sort for arrays; arrays with lengths shorter or equal to the
   below value use insertion sort */

#define CUTOFF 8            /* testing shows that this is good value */


/* Fix for bug # 394184, SD, 20 Jul 00 */
/* replace the hard coded return value by the appropriate LDAP error code */
/* Our qsort needs to police the client timeout and lookthrough limit ?
 * It knows how to compare entries, so we don't bother with all the void * stuff.
 */
/* 
 * Returns:
 *  0: Everything OK         now is: LDAP_SUCCESS (fix for bug #394184)
 * -1: A protocol error      now is: LDAP_PROTOCOL_ERROR
 * -2: Too hard              now is: LDAP_UNWILLING_TO_PERFORM 
 * -3: Operation error       now is: LDAP_OPERATIONS_ERROR
 * -4: Timeout               now is: LDAP_TIMELIMIT_EXCEEDED
 * -5: Admin limit exceeded  now is: LDAP_ADMINLIMIT_EXCEEDED
 * -6: Abandoned             now is: LDAP_OTHER
 */
static int  slapd_qsort(baggage_carrier *bc,IDList *list, sort_spec *s)
{
    ID *lo, *hi;              /* ends of sub-array currently sorting */
    ID *mid;                  /* points to middle of subarray */
    ID *loguy, *higuy;        /* traveling pointers for partition step */
    NIDS size;              /* size of the sub-array */
    ID *lostk[30], *histk[30];
    int stkptr;                 /* stack for saving sub-array to be processed */
	NIDS num = list->b_nids;
	int return_value = LDAP_SUCCESS;
	int error = 0;

    /* Note: the number of stack entries required is no more than
       1 + log2(size), so 30 is sufficient for any array */
    if (num < 2 )
        return LDAP_SUCCESS;                 /* nothing to do */

    stkptr = 0;                 /* initialize stack */

    lo = &(list->b_ids[0]);
    hi = &(list->b_ids[num-1]);        /* initialize limits */

	/* Fix for bugid #394184, SD, 20 Jul 00 */
	if ( bc->lookthrough_limit != -1 && ( bc->lookthrough_limit <= (int) list->b_nids) ) {
		return LDAP_ADMINLIMIT_EXCEEDED;
	} 
	/* end Fix for bugid #394184 */

    /* this entry point is for pseudo-recursion calling: setting
       lo and hi and jumping to here is like recursion, but stkptr is
       prserved, locals aren't, so we preserve stuff on the stack */
recurse:

    size = (hi - lo) + 1;        /* number of el's to sort */

    /* below a certain size, it is faster to use a O(n^2) sorting method */
    if (size <= CUTOFF) {
         shortsort(bc,lo, hi, s );
    }
    else {
        /* First we pick a partititioning element.  The efficiency of the
           algorithm demands that we find one that is approximately the
           median of the values, but also that we select one fast.  Using
           the first one produces bad performace if the array is already
           sorted, so we use the middle one, which would require a very
           wierdly arranged array for worst case performance.  Testing shows
           that a median-of-three algorithm does not, in general, increase
           performance. */

        mid = lo + (size / 2);      /* find middle element */
        swap(mid, lo);               /* swap it to beginning of array */

        /* We now wish to partition the array into three pieces, one
           consisiting of elements <= partition element, one of elements
           equal to the parition element, and one of element >= to it.  This
           is done below; comments indicate conditions established at every
           step. */

        loguy = lo;
        higuy = hi + 1;

        /* Note that higuy decreases and loguy increases on every iteration,
           so loop must terminate. */
        for (;;) {
            /* lo <= loguy < hi, lo < higuy <= hi + 1,
               A[i] <= A[lo] for lo <= i <= loguy,
               A[i] >= A[lo] for higuy <= i <= hi */

            do  {
                loguy ++;
            } while (loguy <= hi && compare_entries_sv(loguy, lo, s, bc, &error) <= 0);

            /* lo < loguy <= hi+1, A[i] <= A[lo] for lo <= i < loguy,
               either loguy > hi or A[loguy] > A[lo] */

            do  {
                higuy --;
            } while (higuy > lo && compare_entries_sv(higuy, lo, s, bc, &error) >= 0);

            /* lo-1 <= higuy <= hi, A[i] >= A[lo] for higuy < i <= hi,
               either higuy <= lo or A[higuy] < A[lo] */

            if (higuy < loguy)
                break;

            /* if loguy > hi or higuy <= lo, then we would have exited, so
               A[loguy] > A[lo], A[higuy] < A[lo],
               loguy < hi, highy > lo */

            swap(loguy, higuy);

			/* Check admin and time limits here on the sort */
			if ( LDAP_SUCCESS != (return_value = sort_nazi(bc)) )
			{
				return return_value;
			}

            /* A[loguy] < A[lo], A[higuy] > A[lo]; so condition at top
               of loop is re-established */
        }

        /*     A[i] >= A[lo] for higuy < i <= hi,
               A[i] <= A[lo] for lo <= i < loguy,
               higuy < loguy, lo <= higuy <= hi
           implying:
               A[i] >= A[lo] for loguy <= i <= hi,
               A[i] <= A[lo] for lo <= i <= higuy,
               A[i] = A[lo] for higuy < i < loguy */

        swap(lo, higuy);     /* put partition element in place */

        /* OK, now we have the following:
              A[i] >= A[higuy] for loguy <= i <= hi,
              A[i] <= A[higuy] for lo <= i < higuy
              A[i] = A[lo] for higuy <= i < loguy    */

        /* We've finished the partition, now we want to sort the subarrays
           [lo, higuy-1] and [loguy, hi].
           We do the smaller one first to minimize stack usage.
           We only sort arrays of length 2 or more.*/

        if ( higuy - 1 - lo >= hi - loguy ) {
            if (lo + 1 < higuy) {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy - 1;
                ++stkptr;
            }                           /* save big recursion for later */

            if (loguy < hi) {
                lo = loguy;
                goto recurse;           /* do small recursion */
            }
        }
        else {
            if (loguy < hi) {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                ++stkptr;               /* save big recursion for later */
            }

            if (lo + 1 < higuy) {
                hi = higuy - 1;
                goto recurse;           /* do small recursion */
            }
        }
    }

    /* We have sorted the array, except for any pending sorts on the stack.
       Check if there are any, and do them. */

    --stkptr;
    if (stkptr >= 0) {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse;           /* pop subarray from stack */
    }
    else
        return LDAP_SUCCESS;                 /* all subarrays done */
}
/* End  fix for bug # 394184 */


static void  shortsort (
	baggage_carrier *bc,
    ID *lo,
    ID *hi,
    sort_spec *s
    )
{
    ID *p, *max;
	int error = 0;

    /* Note: in assertions below, i and j are alway inside original bound of
       array to sort. */

    while (hi > lo) {
        /* A[i] <= A[j] for i <= j, j > hi */
        max = lo;
        for (p = lo+1; p <= hi; p++) {
            /* A[i] <= A[max] for lo <= i < p */
            if (compare_entries_sv(p,max,s,bc,&error) > 0) {
                max = p;
            }
            /* A[i] <= A[max] for lo <= i <= p */
        }

        /* A[i] <= A[max] for lo <= i <= hi */

        swap(max, hi);

        /* A[i] <= A[hi] for i <= hi, so A[i] <= A[j] for i <= j, j >= hi */

        hi--;

        /* A[i] <= A[j] for i <= j, j > hi, loop top condition established */
    }
    /* A[i] <= A[j] for i <= j, j > lo, which implies A[i] <= A[j] for i < j,
       so array is sorted */
}

static void swap (ID *a,ID *b)
{
	ID tmp;

    if ( a != b ) {
		tmp = *a;
		*a = *b;
		*b = tmp;
	}
}


