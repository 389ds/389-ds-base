/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
	Virtual Attributes 
	
	This file implements the "virtual attribute API", which is the interface
	by which any joe servercode gets the values of attributes associated with an
	entry which are _not_ stored with the entry. Attributes which _are_ stored
	with the entry will also be returned by this interface, unless the caller
	specifies the SLAPI_REALATTRS_ONLY flag. This means that the casual caller
	looking for attribute values should probably call the virtual attribute interface
	rather than the regular Slapi_Entry_Attr_Find... and so on calls.
	This interface is different from that one in that the data returned is
	a copy, to be freed by the caller. Details on how to free the returned data is 
	given individually for each function.

	The thing is implemented with a service provider model. The code in here
	does NOT know how to generate computed attr values etc, it just calls the
	registered providers of such information. It also takes care of any crusty
	special cases. Everything above the line here is crispy clean.

	Implicit in this interface is the assumption that the value of an attribute
	can only depend on the entry and some independent stored state. For example,
	the value can't depend upon who is asking, since we don't pass that information
	over the interface.

  More design: we'd like to have the regular result returning code in result.c call this
  interface. However, as it stands, his would incur a malloc, a copy and a free for every
  value. Too expensive. So, it would be good to modify the interface such that when we 
  retrieve values from inside the entry, we just fetch a pointer like before. When we
  retrieve values which are generated, we get copies of the data (or can we get pointers
  there too ?? --- no). 

  One way to achieve this: allow the caller to say that they perfer to receive pointers
  and not copies. They are then informed by the function whether they did receive pointers
  or copies. They then call the free function only when needed. The implicit lifetime of
  the returned pointers is the lifetime of the entry object passed into the function.
  Nasty, but one has to do these things in the name of the god performance.

  DBDB: remember to rename the structures mark complained weren't compliant with the slapi naming scheme.
	
*/

#include "slap.h"

#include "vattr_spi.h"
#include "statechange.h"

#ifdef SOURCEFILE 
#undef SOURCEFILE
#endif
#define SOURCEFILE "vattr.c"
static char *sourcefile = SOURCEFILE;

/* Define only for module test code */
/* #define VATTR_TEST_CODE */

/* Loop context structure */
struct _vattr_context {
	Slapi_PBlock *pb;
	unsigned int vattr_context_loop_count;
	unsigned int error_displayed;
};
#define VATTR_LOOP_COUNT_MAX 50

typedef  vattr_sp_handle vattr_sp_handle_list;

/* Local prototypes */
static int vattr_map_create();
static void vattr_map_destroy();
int vattr_map_sp_insert(char *type_to_add, vattr_sp_handle *sp, void *hint);
vattr_sp_handle_list *vattr_map_sp_getlist(char *type_to_find);
vattr_sp_handle_list *vattr_map_namespace_sp_getlist(Slapi_DN *dn, const char *type_to_find);
vattr_sp_handle_list *vattr_map_sp_get_complete_list();
vattr_sp_handle *vattr_map_sp_first(vattr_sp_handle_list *list,void **hint);
vattr_sp_handle *vattr_map_sp_next(vattr_sp_handle_list *list,void **hint);
vattr_sp_handle *vattr_list_sp_first(vattr_sp_handle_list *list);
vattr_sp_handle *vattr_list_sp_next(vattr_sp_handle_list *list);
int vattr_call_sp_get_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *buffer_flags, void *hint);
int vattr_call_sp_get_batch_values(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, char **type, Slapi_ValueSet*** results,int **type_name_disposition, char*** actual_type_name, int flags, int *buffer_flags, void** hint);
int vattr_call_sp_compare_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, const char *type, Slapi_Value* test_this,int *result, int flags, void* hint);
int vattr_call_sp_get_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags);
void schema_changed_callback(Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data);
int slapi_vattrspi_register_internal(vattr_sp_handle **h, vattr_get_fn_type get_fn, vattr_get_ex_fn_type get_ex_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn, void *options);

#ifdef VATTR_TEST_CODE
int vattr_basic_sp_init();
#endif

void **statechange_api;

/* Housekeeping Functions, called by server startup/shutdown code */

/* Called on server startup, init all structures etc */
void vattr_init()
{
	statechange_api = 0;
	vattr_map_create();

#ifdef VATTR_TEST_CODE
	vattr_basic_sp_init(); 
#endif
}

/* Called on server shutdown, free all structures, inform service providers that we're going down etc */
void vattr_cleanup()
{
	vattr_map_destroy();
}

/* The public interface functions start here */

/* Function which returns the value(s) of an attribute, given an entry and the attribute type name */
/* Return 0 if OK, ?? if attr doesn't exist, ?? if some error condition ?? if result doesn't need to be free'ed */

int slapi_vattr_values_get(/* Entry we're interested in */ Slapi_Entry *e, /* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *buffer_flags) 
{
	vattr_context *c = NULL;
	return slapi_vattr_values_get_sp(c,e,type,results,type_name_disposition,actual_type_name,flags, buffer_flags);
}

int slapi_vattr_values_get_ex(/* Entry we're interested in */ Slapi_Entry *e,/* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet*** results,int **type_name_disposition, char*** actual_type_name, int flags, int *buffer_flags, int *subtype_count) 
{
	vattr_context *c = NULL;
	return slapi_vattr_values_get_sp_ex(c,e,type,results,type_name_disposition,actual_type_name,flags, buffer_flags, subtype_count);
}

int slapi_vattr_namespace_values_get(/* Entry we're interested in */ Slapi_Entry *e, /* backend namespace dn*/ Slapi_DN *namespace_dn, /* attr type name */ char *type, /* pointer to result set */ Slapi_ValueSet*** results,int **type_name_disposition, char*** actual_type_name, int flags, int *buffer_flags, int *subtype_count) 
{
	vattr_context *c = NULL;
	return slapi_vattr_namespace_values_get_sp(c,e,namespace_dn,type,results,type_name_disposition,actual_type_name,flags, buffer_flags, subtype_count);
}


/*
 * If pointers into the entry were requested then we might have
 * a stashed pointer to the entry values, otherwise will be null.
 */
Slapi_ValueSet *vattr_typethang_get_values(vattr_type_thang *t)
{
	return t->type_values;
}

/*
 * This can be much faster than using slapi_vattr_values_get()
 * when you have a vattr_type_thang list returned from slapi_vattr_list_types().
 *
 * We call the vattr SPs to get values for an attribute type in the list only
 * if the results field for that attribute type is null.
 * If the type list comes from slapi_vattr_list_types() then the value is null
 * only for attributes for which an SP wishes to provide a value, so fo the other
 * ones don't bother calling the SPs again--just use the real value we picked up
 * from the entry.
 * 
 */
int slapi_vattr_values_type_thang_get(
    Slapi_Entry *e,
    vattr_type_thang *type_thang,
    Slapi_ValueSet **results,
    int *type_name_disposition,
    char **actual_type_name,
    int flags,
    int *buffer_flags
)
{
    int rc = 0;
    char *type = NULL;

    type = vattr_typethang_get_name(type_thang);
    *results = vattr_typethang_get_values(type_thang);

    if (*results != NULL) {
        /* we already have a pointer directly into the entry */
        *actual_type_name = type;
		*type_name_disposition = 
	    			SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
		*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_POINTERS;
    } else {
        /* fall back to usual implementation */
        rc = slapi_vattr_values_get(e,type,results,
				    type_name_disposition, 
				    actual_type_name,
				    flags,buffer_flags);
    }

    return rc;
}

static void vattr_helper_get_entry_conts(Slapi_Entry *e,char *type, vattr_get_thang *my_get)
{
	Slapi_Attr *a = NULL;
	void *dummy = 0;

	a = attrlist_find_ex(e->e_attrs,type,&(my_get->get_name_disposition), &(my_get->get_type_name), &dummy);
	if (a) {
		my_get->get_present = 1;
		my_get->get_present_values = &(a->a_present_values);
		my_get->get_attr = a;
	}
}

static int vattr_helper_get_entry_conts_with_subtypes(Slapi_Entry *e,const char *type, vattr_get_thang **my_get)
{
	Slapi_Attr *a = NULL;
	void *hint = 0;
	int counter = 0;
	int attr_count = attrlist_count_subtypes(e->e_attrs,type);

	if(attr_count > 0)
	{
		*my_get = (vattr_get_thang *)slapi_ch_calloc(attr_count, sizeof(vattr_get_thang));

		/* pick up attributes with sub-types and slip into the get_thang list */
		for(counter = 0; counter < attr_count; counter++)
		{
			a = attrlist_find_ex(e->e_attrs,type,&((*my_get)[counter].get_name_disposition), &((*my_get)[counter].get_type_name), &hint);
			if (a) {
				(*my_get)[counter].get_present = 1;
				(*my_get)[counter].get_present_values = &(a->a_present_values);
				(*my_get)[counter].get_attr = a;
			}
		}
	}

	return attr_count;
}

static int vattr_helper_get_entry_conts_no_subtypes(Slapi_Entry *e,const char *type, vattr_get_thang **my_get)
{
        int                     attr_count = 0;
        Slapi_Attr *a = attrlist_find(e->e_attrs,type);

        if (a) {
                attr_count = 1;
                *my_get = (vattr_get_thang *)slapi_ch_calloc(1, sizeof(vattr_get_thang));
                (*my_get)[0].get_present = 1;
                (*my_get)[0].get_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
                (*my_get)[0].get_type_name = a->a_type;
                (*my_get)[0].get_present_values = &(a->a_present_values);
                (*my_get)[0].get_attr = a;
        }

        return attr_count;
}

static int vattr_helper_get_entry_conts_ex(Slapi_Entry *e,const char *type, vattr_get_thang **my_get, int suppress_subtypes)
{
        int             rc;

        if ( suppress_subtypes ) {
                rc = vattr_helper_get_entry_conts_no_subtypes(e, type, my_get); 
        } else {
                rc = vattr_helper_get_entry_conts_with_subtypes(e, type, my_get); 
        }

        return rc;
}


vattr_context *vattr_context_new( Slapi_PBlock *pb )
{
	vattr_context *c = (vattr_context *)slapi_ch_calloc(1, sizeof(vattr_context));
	/* The payload is zero, which is what we want */
	if ( c ) {
		c->pb = pb;
	}

	return c;
}

static int vattr_context_check(vattr_context *c)
{
	/* Observe the loop count and see if it's higher than is allowed */
	if (c->vattr_context_loop_count > VATTR_LOOP_COUNT_MAX) {
		return SLAPI_VIRTUALATTRS_LOOP_DETECTED;
	} else return 0;
}

static void vattr_context_mark(vattr_context *c)
{
	c->vattr_context_loop_count += 1;
}

static int vattr_context_unmark(vattr_context *c)
{
	return (c->vattr_context_loop_count -= 1);
}

/* modify the context structure on exit from a vattr sp function */
static void  vattr_context_ungrok(vattr_context **c)
{
	/* Decrement the loop count */
	if (0 == vattr_context_unmark(*c)) {
		/* If necessary, delete the structure */
		slapi_ch_free((void **)c);
	}
}

/* Check and mess with the context structure on entry to a vattr sp function */
static int vattr_context_grok(vattr_context **c)
{
	int rc = 0;
		/* First check that we've not got into an infinite loop.
	   We do so by means of the vattr_context structure.
	 */

	/* Do we have a context at all ?? */
	if (NULL == *c) {
		/* No, so let's make one */
		*c = vattr_context_new( NULL );
		if (NULL == *c) {
			return ENOMEM;
		}
	} else {
		/* Yes, so let's check its contents */
		rc = vattr_context_check(*c);
	}
	/* mark the context as having been used once */
	vattr_context_mark(*c);
	return rc;
}



/* keep track of error messages so we don't spam the error log */
static void vattr_context_set_loop_msg_displayed(vattr_context **c)
{
	(*c)->error_displayed = 1;
}

static int vattr_context_is_loop_msg_displayed(vattr_context **c)
{
	return (*c)->error_displayed;
}

/*
 * vattr_test_filter:
 * 
 * . tests an ava, presence or substring filter against e.
 * . group these filter types together to avoid having to duplicate the
 * . vattr specific code in three seperate routines.
 * . handles virtual attrs in the filter.
 * . does update the vattrcache if a call to this calculates vattrs
 *
 * returns: 0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code 
 *
*/
int vattr_test_filter( /* Entry we're interested in */ Slapi_Entry *e,    					
						Slapi_Filter *f,
						filter_type_t filter_type,
						char * type) {	
	int rc = -1;
	int sp_bit = 0; /* Set if an SP supplied an answer */
	vattr_sp_handle_list *list = NULL;
	Slapi_DN *sdn;
	Slapi_Backend *be;
	Slapi_DN *namespace_dn;

	/* get the namespace this entry belongs to */
	sdn = slapi_entry_get_sdn( e );
	be = slapi_be_select( sdn );
	namespace_dn = (Slapi_DN*)slapi_be_getsuffix(be, 0);
		
	/* Look for attribute in the map */
	
	if(namespace_dn)
	{
		list = vattr_map_namespace_sp_getlist(namespace_dn, type);
	}
	else
	{
		list = vattr_map_namespace_sp_getlist(NULL, type);
	}

	if (list) {
		vattr_sp_handle *current_handle = NULL;
		Slapi_Attr *cache_attr = NULL;
		/* first lets consult the cache to save work */
		int cache_status;
					
		cache_status = slapi_entry_vattrcache_findAndTest(e, type,
														f,
														filter_type,
														&rc);
		switch(cache_status)
		{
			case SLAPI_ENTRY_VATTR_RESOLVED_EXISTS: /* cached vattr */
			{
				sp_bit = 1;				
				break;
			}

			case SLAPI_ENTRY_VATTR_RESOLVED_ABSENT: /* does not exist */
				break; /* look in entry */

			case SLAPI_ENTRY_VATTR_NOT_RESOLVED: /* not resolved */
			default: /* any other result, resolve */
			{
				int flags = SLAPI_VIRTUALATTRS_REQUEST_POINTERS;
				void *hint = NULL;
				Slapi_ValueSet **results = NULL; /* pointer to result set */
				int  *type_name_disposition;
				char **actual_type_name;
				int buffer_flags;
				vattr_get_thang my_get = {0};
				vattr_context ctx;
				/* bit cacky, but need to make a null terminated lists for now
				 * for the (unimplemented and so fake) batch attribute request
				 */
				char *types[2];
				void *hint_list[2];

				types[0] = type;
				types[1] = 0;
				hint_list[1] = 0;

				/* set up some local context */
				ctx.vattr_context_loop_count=1;
				ctx.error_displayed = 0;

				for (current_handle = vattr_map_sp_first(list,&hint); current_handle; current_handle = vattr_map_sp_next(current_handle,&hint))
				{			
					hint_list[0] = hint;

					rc = vattr_call_sp_get_batch_values(current_handle,&ctx,e,
							&my_get,types,&results,&type_name_disposition,
								&actual_type_name,flags,&buffer_flags, hint_list);
						
					if (0 == rc)
					{
						sp_bit = 1;
						break;
					}
				}

				if(!sp_bit)
				{
					/*
					 * No vattr sp supplied an answer so will look in the
					 * entry itself.
					 * but first lets cache the no result
					*/			
					slapi_entry_vattrcache_merge_sv(e, type, NULL );

				}
				else
				{
					/*
					 * A vattr sp supplied an answer.
					 * so turn the value into a Slapi_Attr, pass
					 *  to the syntax plugin for comparison.
					*/

					if ( filter_type == FILTER_TYPE_AVA ||
							filter_type == FILTER_TYPE_SUBSTRING ) {				
						Slapi_Attr *a = NULL;
						int i=0;
						/* may need this when we have a passin interface for set_valueset */
						/*Slapi_ValueSet null_valueset = {0};*/

						rc=-1;

						if(results && actual_type_name && type_name_disposition)
						{
							while(results[i] && rc)
							{
    	    					a = slapi_attr_new();
        						slapi_attr_init(a, type);
								/* a now contains a *copy* of results */
        						slapi_attr_set_valueset( a, results[i]);

								if ( filter_type == FILTER_TYPE_AVA ) {
									rc = plugin_call_syntax_filter_ava( a,
															f->f_choice, &f->f_ava );
								} else if ( filter_type == FILTER_TYPE_SUBSTRING) {
									rc = plugin_call_syntax_filter_sub( a,
																		&f->f_sub);
								}

								/*
								 * Cache stuff: dups results
								*/
								slapi_entry_vattrcache_merge_sv(e, actual_type_name[i],
																results[i] );
								/*
								 * Free stuff, just in case we did not
								 * get pointers.					 
								*/
								slapi_vattr_values_free( &(results[i]),
														&(actual_type_name[i]),
														buffer_flags);
								/* may need this when we support a passin set_valueset */
								/*slapi_attr_set_valueset( a, &null_valueset);*/
								/* since a contains a copy of results, we must free it */
								slapi_attr_free(&a);
								i++;
							}
						}

						slapi_ch_free((void**)&results);
						slapi_ch_free((void**)&actual_type_name);
						slapi_ch_free((void**)&type_name_disposition);

					} else if ( filter_type == FILTER_TYPE_PRES ) {
						/*
						 * Cache stuff: dups results
						*/
						int i=0;

						while(results[i])
						{
							slapi_entry_vattrcache_merge_sv(e, actual_type_name[i],
															results[i] );
							/*
							 * Free stuff, just in case we did not
							 * get pointers.					 
							*/
							slapi_vattr_values_free( &results[i],
													&actual_type_name[i],
													buffer_flags);
						}
						slapi_ch_free((void**)&results);
						slapi_ch_free((void**)&actual_type_name);
						slapi_ch_free((void**)&type_name_disposition);
					}
				}

				break;
			}			
		}/* switch */		
	}
	/* If no SP supplied the answer, take it from the entry */
	if (!sp_bit) 
	{		
		int acl_test_done;

		if ( filter_type == FILTER_TYPE_AVA ) {
			
			rc = test_ava_filter( NULL /* pb not needed */,
									e, e->e_attrs, &f->f_ava,
									f->f_choice,
									0 /* no access check */,
									0 /* do test filter */,
									&acl_test_done);
								    		
    	} else if ( filter_type == FILTER_TYPE_SUBSTRING ) {
			
			rc = test_substring_filter( NULL, e, f, 0 /* no access check */,
									0 /* do test filter */, &acl_test_done);
				
		} else if ( filter_type == FILTER_TYPE_PRES ) {
	
			rc = test_presence_filter( NULL, e, f->f_type,
										0 /* no access check */,
										0 /* do test filter */,
										&acl_test_done);
		}
	} 
	return rc;
}
/*
 * deprecated in favour of slapi_vattr_values_get_sp_ex() which
 * returns subtypes too.
*/
SLAPI_DEPRECATED int
slapi_vattr_values_get_sp(vattr_context *c,
							/* Entry we're interested in */ Slapi_Entry *e,
							/* attr type name */ char *type,
							/* pointer to result set */ Slapi_ValueSet** results,
							int *type_name_disposition,
							char** actual_type_name, int flags,
							int *buffer_flags) 
{

  PRBool use_local_ctx=PR_FALSE;
  vattr_context ctx;
	int rc = 0;
	int sp_bit = 0; /* Set if an SP supplied an answer */
	vattr_sp_handle_list *list = NULL;

	vattr_get_thang my_get = {0};

	if (c != NULL) {
	  rc = vattr_context_grok(&c);
	  if (0 != rc) {
		if(!vattr_context_is_loop_msg_displayed(&c))
		{
			/* Print a handy error log message */
			LDAPDebug(LDAP_DEBUG_ANY,"Detected virtual attribute loop in get on entry %s, attribute %s\n", slapi_entry_get_dn_const(e), type, 0);
			vattr_context_set_loop_msg_displayed(&c);
		}
	    return rc;
	  }
	} else {
	  use_local_ctx=PR_TRUE;
	  ctx.vattr_context_loop_count=1;
	  ctx.error_displayed = 0;
	}

	/* For attributes which are in the entry, we just need to get to the Slapi_Attr structure and yank out the slapi_value_set 
	   structure. We either return a pointer directly to it, or we copy it, depending upon whether the caller asked us to try to 
	   avoid copying.
	*/

	/* First grok the entry, and remember what we saw. This call does no more than walk down the entry attribute list, do some string compares and copy pointers. */
	vattr_helper_get_entry_conts(e,type, &my_get);
	/* Having done that, we now consult the attribute map to find service providers who are interested */
	/* Look for attribute in the map */
	if(!(flags & SLAPI_REALATTRS_ONLY))
	{
		list = vattr_map_sp_getlist(type);
		if (list) {
			vattr_sp_handle *current_handle = NULL;
			void *hint = NULL;
			Slapi_Attr* cache_attr = 0;
			char *vattr_type = NULL;
			/* first lets consult the cache to save work */
			int cache_status;
						
			cache_status =
				slapi_entry_vattrcache_find_values_and_type(e, type,
															results,
															actual_type_name);															
			switch(cache_status)
			{
				case SLAPI_ENTRY_VATTR_RESOLVED_EXISTS: /* cached vattr */
				{
					sp_bit = 1;					

					/* Complete analysis of type matching */
					if ( 0 == slapi_attr_type_cmp( type , *actual_type_name, SLAPI_TYPE_CMP_EXACT) )
					{
						*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
					} else {
						*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE;
					}

					break;
				}

				case SLAPI_ENTRY_VATTR_RESOLVED_ABSENT: /* does not exist */
					break; /* look in entry */

				case SLAPI_ENTRY_VATTR_NOT_RESOLVED: /* not resolved */
				default: /* any other result, resolve */
				{
					for (current_handle = vattr_map_sp_first(list,&hint); current_handle; current_handle = vattr_map_sp_next(current_handle,&hint))
					{
						if (use_local_ctx)
						{
							rc = vattr_call_sp_get_value(current_handle,&ctx,e,&my_get,type,results,type_name_disposition,actual_type_name,flags,buffer_flags, hint);
						}
						else
						{
							/* call this SP */
							rc = vattr_call_sp_get_value(current_handle,c,e,&my_get,type,results,type_name_disposition,actual_type_name,flags,buffer_flags, hint);
						}

						if (0 == rc)
						{
							sp_bit = 1;
							break;
						}
					}

					if(!sp_bit)
					{
						/* clean up, we have failed and must now examine the
						 * entry itself
						 * But first lets cache the no result
						 * Creates the type (if necessary).
						*/
						slapi_entry_vattrcache_merge_sv(e, type, NULL );

					}
					else
					{
						/*
						 * we need to cache the virtual attribute
						 * creates the type (if necessary) and dups
						 * results.
						*/
						slapi_entry_vattrcache_merge_sv(e, *actual_type_name,
														*results );
					}

					break;
				}
			}
		}
	}
	/* If no SP supplied the answer, take it from the entry */
	if (!sp_bit && !(flags & SLAPI_VIRTUALATTRS_ONLY)) 
	{
		rc = 0; /* reset return code (cause an sp must have failed) */
		*type_name_disposition = my_get.get_name_disposition;

		if (my_get.get_present) {
			if (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS) {
				*results = my_get.get_present_values;
				*actual_type_name = my_get.get_type_name;
			} else {
				*results = valueset_dup(my_get.get_present_values);
				if (NULL == *results) {
					rc = ENOMEM;
				} else {
					*actual_type_name = slapi_ch_strdup(my_get.get_type_name);
					if (NULL == *actual_type_name) {
						rc = ENOMEM;
					}
				}
			}
			if (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS) {
					*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_POINTERS;
			} else {
				*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;
			}
		} else {
			rc = SLAPI_VIRTUALATTRS_NOT_FOUND;
		}
	}
	if (!use_local_ctx) {
	  vattr_context_ungrok(&c);
	}
	return rc;
}

/*
 *
 * returns 0: (no_error && type found ) in which case:
 *              results: contains the current values for type and
 *						all it's subtypes in e
 *              type_name_disposition: how each type was matched
 *                      SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS
 *                      SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE
 *              actual_type_name: type name as found
 *              flags: bit mask of options:
 *                      SLAPI_REALATTRS_ONLY
 *                      SLAPI_VIRTUALATTRS_ONLY
 *                      SLAPI_VIRTUALATTRS_REQUEST_POINTERS
 *                      SLAPI_VIRTUALATTRS_LIST_OPERATIONAL_ATTRS
 *              buffer_flags: bit mask to be used as input flags for
 *                              slapi_values_free()
 *                  SLAPI_VIRTUALATTRS_RETURNED_POINTERS
 *                  SLAPI_VIRTUALATTRS_RETURNED_COPIES
 *                  SLAPI_VIRTUALATTRS_REALATTRS_ONLY
 *				item_count: number of subtypes matched
 * otherwise:
 *          SLAPI_VIRTUALATTRS_LOOP_DETECTED (failed to eval a vattr)
 *          SLAPI_VIRTUALATTRS_NOT_FOUND     (type not recognised by any vattr
 *                                              sp && not a real attr in entry )
 *          ENOMEM                           (memory error)
 *
 *	Note:
 *  . modifes the virtual cache in the entry.
 *  . for cached vattrs you always get a copy, so it will need to be
 * 	freed via slapi_values_free().
 *
 */

int slapi_vattr_values_get_sp_ex(vattr_context *c,
							/* Entry we're interested in */ Slapi_Entry *e,
							/* attr type name */ char *type,
							/* pointer to result set */ Slapi_ValueSet*** results,
							int **type_name_disposition,
							char*** actual_type_name, int flags,
							int *buffer_flags, int *item_count) 
{
	return slapi_vattr_namespace_values_get_sp(
		c, e, NULL, type, results, type_name_disposition,
		actual_type_name, flags, buffer_flags, item_count
		);
}

int slapi_vattr_namespace_values_get_sp(vattr_context *c,
							/* Entry we're interested in */ Slapi_Entry *e,
							/* DN denoting backend namespace */ Slapi_DN *namespace_dn,
							/* attr type name */ char *type,
							/* pointer to result set */ Slapi_ValueSet*** results,
							int **type_name_disposition,
							char*** actual_type_name, int flags,
							int *buffer_flags, int *item_count) 
{

	int rc = 0;
	int sp_bit = 0; /* Set if an SP supplied an answer */
	vattr_sp_handle_list *list = NULL;
	vattr_get_thang *my_get = NULL;
	int attr_count = 0;

	rc = vattr_context_grok(&c);
	if (0 != rc) {
		/* Print a handy error log message */
		if(!vattr_context_is_loop_msg_displayed(&c))
		{
			LDAPDebug(LDAP_DEBUG_ANY,"Detected virtual attribute loop in get on entry %s, attribute %s\n", slapi_entry_get_dn_const(e), type, 0);
			vattr_context_set_loop_msg_displayed(&c);
		}
		return rc;
	}

	/* Having done that, we now consult the attribute map to find service providers who are interested */
	/* Look for attribute in the map */
	if(!(flags & SLAPI_REALATTRS_ONLY))
	{
		/* we use the vattr namespace aware version of this */
		list = vattr_map_namespace_sp_getlist(namespace_dn, type);
		if (list) {
			vattr_sp_handle *current_handle = NULL;
			void *hint = NULL;
			Slapi_Attr* cache_attr = 0;
			char *vattr_type=NULL;
			/* first lets consult the cache to save work */
			int cache_status;
			
			cache_status =
					slapi_entry_vattrcache_find_values_and_type_ex(e, type,
															results,
															actual_type_name);
			switch(cache_status)
			{
				case SLAPI_ENTRY_VATTR_RESOLVED_EXISTS: /* cached vattr */
				{
					sp_bit = 1;					

					/* Complete analysis of type matching */
					*type_name_disposition =
							(int *)slapi_ch_malloc(sizeof(*type_name_disposition));
					if ( 0 == slapi_attr_type_cmp( type , **actual_type_name, SLAPI_TYPE_CMP_EXACT) )
					{
						**type_name_disposition =
							SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
					} else {
						**type_name_disposition =
							SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE;
					}

					*item_count = 1;

					break;
				}

				case SLAPI_ENTRY_VATTR_RESOLVED_ABSENT: /* does not exist */
					break; /* look in entry */

				case SLAPI_ENTRY_VATTR_NOT_RESOLVED: /* not resolved */
				default: /* any other result, resolve */
				{
					/* bit cacky, but need to make a null terminated lists for now
					 * for the (unimplemented and so fake) batch attribute request
					 */
					char **type_list = (char**)slapi_ch_calloc(2,sizeof(char*));
					void **hint_list = (void**)slapi_ch_calloc(2,sizeof(void*));

					type_list[0] = type;

					for (current_handle = vattr_map_sp_first(list,&hint); current_handle; current_handle = vattr_map_sp_next(current_handle,&hint)) {
						/* call this SP */

						hint_list[0] = hint;

						rc = vattr_call_sp_get_batch_values(current_handle,c,e,my_get,
								type_list,results,type_name_disposition,
								actual_type_name, flags,buffer_flags, hint_list);
						if (0 == rc) {
							sp_bit = 1; /* this sp provided an answer, we are done */

							/* count the items in the (null terminated) result list */
							*item_count = 0;

							while((*results)[*item_count])
							{
								(*item_count)++;
							}

							break;
						}
					}

					slapi_ch_free((void**)&type_list);
					slapi_ch_free((void**)&hint_list);

					if(!sp_bit)
					{
						/* we have failed and must now examine the entry itself
						 *
						 * But first lets cache the no result
						 * dups the type (if necessary).
						*/
						slapi_entry_vattrcache_merge_sv(e, type, NULL );

					}
					else
					{
						/*
						 * we need to cache the virtual attribute
						 * dups the type (if necessary) and results.
						*/
						/*
						 * this (and above) will of course need to get updated
						 * when we do real batched attributes
						 */
						slapi_entry_vattrcache_merge_sv(e, **actual_type_name,
															**results );
					}
				}
			}
		}
	}

	/* If no SP supplied the answer, take it from the entry */
	if (!sp_bit && !(flags & SLAPI_VIRTUALATTRS_ONLY)) 
	{
		int counter;

		/* First grok the entry - allocates memory for list */
		attr_count = vattr_helper_get_entry_conts_ex(e,type, &my_get,
				(0 != (flags & SLAPI_VIRTUALATTRS_SUPPRESS_SUBTYPES)));
		*item_count = attr_count;
		rc = 0; /* reset return code (cause an sp must have failed) */

		if(attr_count > 0)
		{	

			*results = (Slapi_ValueSet**)slapi_ch_calloc(1, sizeof(*results) * attr_count);
			*type_name_disposition = (int *)slapi_ch_malloc(sizeof(*type_name_disposition)  * attr_count);
			*actual_type_name = (char**)slapi_ch_malloc(sizeof(*actual_type_name)  * attr_count);

			/* For attributes which are in the entry, we just need to get to the Slapi_Attr structure and yank out the slapi_value_set 
			   structure. We either return a pointer directly to it, or we copy it, depending upon whether the caller asked us to try to 
			   avoid copying.
			*/
			for(counter = 0; counter < attr_count; counter++)
			{
				(*type_name_disposition)[counter] = my_get[counter].get_name_disposition;

				if (my_get[counter].get_present) {
					if (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS) {
						/* pointers will do, good */
						(*results)[counter] = my_get[counter].get_present_values;
						(*actual_type_name)[counter] = my_get[counter].get_type_name;
					} else {
						/* need to copy the values */
						(*results)[counter] = valueset_dup(my_get[counter].get_present_values);
						if (NULL == (*results)[counter]) {
							rc = ENOMEM;
						} else {
							(*actual_type_name)[counter] = slapi_ch_strdup(my_get[counter].get_type_name);
							if (NULL == (*actual_type_name)[counter]) {
								rc = ENOMEM;
							}
						}
					}
					if (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS) {
							*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_POINTERS;
					} else {
						*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;
					}
				} else {
					rc = SLAPI_VIRTUALATTRS_NOT_FOUND;
				}
			}

			slapi_ch_free((void **)&my_get);
		}
	}
	vattr_context_ungrok(&c);
	return rc;
}

/* Do we need a call to free the results from the above ? */

void slapi_vattr_values_free(Slapi_ValueSet **value, char** actual_type_name,
								int flags)
{
	/* Check whether we need to free the strings */
	if (flags & SLAPI_VIRTUALATTRS_RETURNED_POINTERS) {
		/* We don't need to free the values */
	} else {
		/* Free the valueset */
		if (*value) {
			slapi_valueset_free(*value);
		}
		if (*actual_type_name) {
			slapi_ch_free((void **)actual_type_name);
		}
	}
	*actual_type_name = NULL;
	*value = NULL;
}

/* Function like the one above but doing a compare operation */
/* Same return codes as above. Compare result value returned in "result": 0 if compare true, 1 if compare false */

int slapi_vattr_value_compare(Slapi_Entry *e, char *type, Slapi_Value *test_this, int *result, int flags)
{
	return slapi_vattr_namespace_value_compare_sp(NULL,e,NULL,type,test_this,result,flags);
}

/* namespace version of above */
int slapi_vattr_namespace_value_compare(Slapi_Entry *e, Slapi_DN *namespace_dn, const char *type, Slapi_Value *test_this, int *result, int flags)
{
	return slapi_vattr_namespace_value_compare_sp(NULL,e,namespace_dn,type,test_this,result,flags);
}

int slapi_vattr_value_compare_sp(vattr_context *c,/* Entry we're interested in */ Slapi_Entry *e,/* attr type name */ char *type, Slapi_Value *test_this,/* pointer to result */ int *result, int flags)
{
	return slapi_vattr_namespace_value_compare_sp(c,e,NULL,type,test_this,result,flags);
}

int slapi_vattr_namespace_value_compare_sp(vattr_context *c,/* Entry we're interested in */ Slapi_Entry *e, /* backend namespace dn*/Slapi_DN *namespace_dn, /* attr type name */ const char *type, Slapi_Value *test_this,/* pointer to result */ int *result, int flags)
{
	int rc = 0;

	int sp_bit = 0; /* Set if an SP supplied an answer */
	vattr_sp_handle_list *list = NULL;

	vattr_get_thang *my_get = 0;

	*result = 0;	/* return "compare false" by default */

	rc = vattr_context_grok(&c);
	if (0 != rc) {
		/* Print a handy error log message */
		LDAPDebug(LDAP_DEBUG_ANY,"Detected virtual attribute loop in compare on entry %s, attribute %s\n", slapi_entry_get_dn_const(e), type, 0);
		return rc;
	}

	/* Having done that, we now consult the attribute map to find service providers who are interested */
	/* Look for attribute in the map */
	list = vattr_map_namespace_sp_getlist(namespace_dn, type);
	if (list) {
		vattr_sp_handle *current_handle = NULL;
		void *hint = NULL;
		for (current_handle = vattr_map_sp_first(list,&hint); current_handle; current_handle = vattr_map_sp_next(list,&hint)) {
			/* call this SP */
			rc = vattr_call_sp_compare_value(current_handle,c,e,my_get,type,test_this,result,flags, hint);
			if (0 == rc) {
				sp_bit = 1;
				break;
			}
		}
	}
	/* If no SP supplied the answer, take it from the entry */
	if (!sp_bit) {
		/* Grok the entry, and remember what we saw. This call does no more than walk down the entry attribute list, do some string compares and copy pointers. */
		int attr_count = vattr_helper_get_entry_conts_ex(e,type, &my_get,
			(0 != (flags & SLAPI_VIRTUALATTRS_SUPPRESS_SUBTYPES)));

		if (my_get && my_get->get_present) {
			int i;
			Slapi_Value *Dummy_value = NULL;
	
			/* Put the required stuff in the fake attr */

			for(i=0;i<attr_count;i++)
			{
				Dummy_value = slapi_valueset_find( my_get[i].get_attr, my_get[i].get_present_values, test_this );

				if (Dummy_value) {
					*result = 1;	/* return "compare true" */

					break;
				}
			}
		} else {
			rc = SLAPI_VIRTUALATTRS_NOT_FOUND;
		}
	}
	vattr_context_ungrok(&c);
	slapi_ch_free((void **)&my_get);
	return rc;
}

/* Function to obtain the list of attribute types which are available on this entry */

/*
	Need to call service providers here:
	We know only the entry's DN and so we could restrict our choice of SPs based on that.
	However, we don't yet implement such fancyness.
	For now, we just crawl through all the SPs, asking them in turn to contribute.
	This is pretty inefficient because we will trawl the list  for each wildcard attribute type search operation.
	Service providers should therefore handle the call as fast as they can.
 */

struct _vattr_type_list_context {
	 unsigned int vattr_context_loop_count;
	 vattr_type_thang *types;
	 int realattrs_only;	/* TRUE implies list contains only real attrs */
	 int list_is_copies;
	 int flags;
	 size_t list_length;
	 size_t block_length;
};

/* Helper function which converts a type list from pointers to copies */
static int vattr_convert_type_list(vattr_type_list_context *thang)
{
	vattr_type_thang *old_list = NULL;
	vattr_type_thang *new_list = NULL;
	size_t index = 0;

	old_list = thang->types;
	/* Make a new list */
	new_list = (vattr_type_thang*)slapi_ch_calloc(thang->block_length, sizeof(vattr_type_thang));
	if (NULL == new_list) {
		return ENOMEM;
	}
	/* Walk the list strdup'ing the type name and copying the rest of the structure contents */
	for (index = 0; index < thang->list_length; index++) {
		new_list[index].type_flags = old_list[index].type_flags;
		new_list[index].type_name = slapi_ch_strdup(old_list[index].type_name);
		/*
		 * list_is_copies does not affect type_values as it
		 * is always a pointer never a copy.
		 */
		new_list[index].type_values = old_list[index].type_values;
	}
	/* Mark it a copy list */
	thang->list_is_copies = 1;
	/* Free the original list */
	slapi_ch_free((void **)&(thang->types));
	/* swap lists */
	thang->types = new_list;

	return 0;
}

/*
 * Returns all the attribute types from e, both real and virtual.
 *
 * Any of the attributes found in the entry to start with, for
 * whom no vattr SP volunteered, will also have a pointer to the
 * original valueset in the entry--this fact can be used by
 * calling slapi_vattr_values_type_thang_get(), which will
 * take the values present in the vattr_type_thang list
 * rather than calling slapi_vattr_values_get() to retrieve the value.
*/ 

#define TYPE_LIST_EXTRA_SPACE 5 /* Number of extra slots we allow for SP's to insert types */

int slapi_vattr_list_attrs(/* Entry we're interested in */ Slapi_Entry *e,
					/* pointer to receive the list */ vattr_type_thang **types,
					int flags, int *buffer_flags)
{
	int rc = 0;
	size_t attr_count = 0;
	vattr_type_thang *result_array = NULL	;
	Slapi_Attr *current_attr = NULL;
	size_t i = 0;
	int list_is_copies = 0;
	vattr_sp_handle_list *list = NULL;
	size_t list_length = 0;
	size_t block_length = 0;
	vattr_type_list_context type_context = {0};

	block_length  = 1 + TYPE_LIST_EXTRA_SPACE;

	if(!(flags & SLAPI_VIRTUALATTRS_ONLY))
	{
		/* First find what's in the entry itself*/
		/* Count the attributes */
		for (current_attr = e->e_attrs; current_attr != NULL; current_attr = current_attr->a_next, attr_count++) ;
		block_length  += attr_count;
		/* Allocate the pointer array */
		result_array = (vattr_type_thang*)slapi_ch_calloc(block_length,sizeof(vattr_type_thang));
		if (NULL == result_array) {
			return ENOMEM;
		}
		list_length = attr_count;

		/* Now copy the pointers into the array */
		i  = 0;
		for (current_attr = e->e_attrs; current_attr != NULL; current_attr = current_attr->a_next) {
			char *mypointer = NULL;
			if (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS) {
				mypointer = current_attr->a_type;
				result_array[i].type_values = &(current_attr->a_present_values);
			} else {
				mypointer = slapi_ch_strdup(current_attr->a_type);
				list_is_copies = 1;
			}
			result_array[i].type_flags = current_attr->a_flags;
			result_array[i++].type_name = mypointer;
		}
		PR_ASSERT(i == attr_count);
	}

	/* if we didnt send real attrs we need to allocate result array */
	if(NULL == result_array)
	{
		result_array = (vattr_type_thang*)slapi_ch_calloc(block_length,sizeof(vattr_type_thang));
		if (NULL == result_array) {
			return ENOMEM;
		}
	}

	/* Then ask the service providers for their input */
	type_context.flags = flags;
	type_context.realattrs_only = 1; /* until we know otherwise */
	type_context.list_is_copies = list_is_copies;
	type_context.types = result_array;
	type_context.list_length = list_length;
	type_context.block_length = block_length;

	/*
	 * At this point type_context.types is a list of all
	 * the attributetypes (copies or pointers) and values (copies, if present)
	 * found in the entry.
	*/

	if(!(flags & SLAPI_REALATTRS_ONLY))
	{
		list = vattr_map_sp_get_complete_list();
		if (list) {
			vattr_sp_handle *current_handle = NULL;
			
			for (current_handle = vattr_list_sp_first(list); current_handle; current_handle = vattr_list_sp_next((vattr_sp_handle_list *)current_handle)) {
				/* call this SP */
				rc = vattr_call_sp_get_types(current_handle,e,&type_context,
											flags);
				if (0 != rc) {
					/* DBDB do what on error condition ? */
				}
			}
			/* assert enough space for the null terminator */
			PR_ASSERT( type_context.list_length < type_context.block_length);
			i = type_context.list_length;
		}
	}

	/*
	 * Now type_context.types is a list of all the types in this entry--
	 * real and virtual.  For real ones, the values field is filled in.
	 * For virtual ones (including virtual ones which will overwrite a real
	 * value) the values field is null--this fact may used
	 * subsequently by callers of slapi_vattr_list_attrs()
	 * by calling slapi_vattr_values_type_thang_get() which
	 * will only calculate the values for attributetypes with
	 * non-null values.
	*/
		
	flags = type_context.flags;
	list_is_copies = type_context.list_is_copies;
	result_array = type_context.types;
	list_length = type_context.list_length;
	block_length = type_context.block_length;

	if (list_is_copies) {
		*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;
	} else {
		*buffer_flags = SLAPI_VIRTUALATTRS_RETURNED_POINTERS;
	}

	/* Let the caller know if the list contains only real attrs */
	if (type_context.realattrs_only) {
		*buffer_flags |= SLAPI_VIRTUALATTRS_REALATTRS_ONLY;
	}

	result_array[i].type_name = NULL;

	if (types && list_length) {
		*types = result_array;
	}
	else
		*types = 0;

	return rc;
}

static int vattr_add_thang_to_list(vattr_type_list_context *c, vattr_type_thang *thang)
{
	vattr_type_thang *new_list = NULL;

	/* Will it fit in the current block ? */
	if (c->list_length == c->block_length - 1) {
		c->block_length *= 2;

		new_list = (vattr_type_thang*)slapi_ch_realloc((char *)c->types,
													   c->block_length * (int)sizeof(vattr_type_thang));
		if (NULL == new_list) {
			return ENOMEM;
		}

		c->types = new_list;
	} 
	/* Add to the end of the list */
	c->types[c->list_length ] = *thang;
	c->list_length += 1;
	return 0;
}

/* Called by SP's during their get_types call, to have the server add a
 * type to the type list.
 *
 * c: the vattr_type_list_context passed from the original caller of
 *		slapi_vattr_list_attrs()
 * thang: the new type to be added to the type list.
 * flags: tells the routine whether to copy the thang or not.
 *			SLAPI_VIRTUALATTRS_REQUEST_POINTERS--no need to copy it.
 *			!(SLAPI_VIRTUALATTRS_REQUEST_POINTERS)--need to copy it.
 *
 * Checks to see if the requested type is already in the list,
 * and if it is, if the value of the attribute is
 * non-null, it resets the value to null.  This means that when used
 * subsequently, the type list indicates whether, for a given attribute type,
 * the SP's need to be called to retrieve the value. 
 *
*/
int slapi_vattrspi_add_type(vattr_type_list_context *c,
								vattr_type_thang *thang, int flags)
{
	int rc = 0;
	unsigned int index = 0;
	vattr_type_thang thang_to_add = {0};

	int found_it = 0;

	PR_ASSERT(c);

	/* We are no longer sure that the list contains only real attrs */
	c->realattrs_only = 0;

	/* Check to see if the type is in the list already */
	for (index = 0; index < c->list_length; index++) {
		if (0 == slapi_UTF8CASECMP(c->types[index].type_name,thang->type_name)) {
			found_it = 1;
			break;
		}
	}
	/*
	 * If found and there are values specified in the vattr_type_thang, then
	 * that means it's a real attribute--because SP's do not add values
	 * when called via slapi_vattr_list_attrs()/vattr_call_sp_get_types().
	 * However, an SP is willing to claim responsibility for this attribute
	 * type, so to ensure that that virtual value will get calculated, set the
	 * original real value to NULL here.
	 * The guiding rule here is "if an SP is prepared
	 * to provide a value for an attribute type, then that is the one
	 * that must be returned to the user".  Note, this does not prevent
	 * the SP implementing a "merge real with virtual policy", if they
	 * wish.
	*/
	
	if (found_it) {
	
		if ( c->types[index].type_values != NULL ) {			
				/*
				 * Any values in this list are always pointers.
				 * See slapi_vattr_list_types() and vattr_convert_type_list()
				*/
				c->types[index].type_values = NULL;			
		}
	
		return 0;
	}
	/*
	 * If it's not in the list then we need to add it.
	 * If the list is already copies, then we need to make a copy of what the
	 * SP passed us.
	 * If the SP indicated that the type name it passed us is volatile,
	 * !(flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS)
	 * then we need to copy it anyway.
	*/

	/* rbyrneXXX optimisation for COS: if each type_thang could have a
	 * free_flag
	 * then we would not have to convert all the real attrs to copies
	 * just because an SP returned a copy (note: only COS rrequires copies;
	 * roles returns a pointer to a static string "nsRole").
	*/
	if (!c->list_is_copies &&
						(0 == (flags & SLAPI_VIRTUALATTRS_REQUEST_POINTERS))) {
		/* Means we need to turn the existing list into a list of copies */
		vattr_convert_type_list(c);
	}
	if (c->list_is_copies) {
		thang_to_add.type_flags = thang->type_flags;
		thang_to_add.type_name = slapi_ch_strdup(thang->type_name);
	} else {
		thang_to_add = *thang;
	}
	/* Lastly, we add to the list, using a function which hides the complexity of extending the list if it fills up */
	vattr_add_thang_to_list(c, &thang_to_add);

	return rc;
}

/* Function to free the list returned in the function above */ 
/* Written, reviewed, debug stepped */

void slapi_vattr_attrs_free(vattr_type_thang **types, int flags)
{
	if (NULL == *types) {
		return;
	}
	/* Check whether we need to free the strings */
	if (flags & SLAPI_VIRTUALATTRS_RETURNED_POINTERS) {
		/* We don't need to free the values */
	} else {
		char *attr_name_to_free = NULL;
		size_t i = 0;
		/* Walk down the set of values, fr eeing each one in turn */
		for (; (attr_name_to_free = (*types)[i].type_name) != NULL; i++) {
			slapi_ch_free((void **)&attr_name_to_free);
		}
	}
	/* We always need to free the pointer block */
	slapi_ch_free((void **)types);
}

char *vattr_typethang_get_name(vattr_type_thang *t)
{
	return t->type_name;
}

unsigned long vattr_typethang_get_flags(vattr_type_thang *t)
{
	return t->type_flags;
}

vattr_type_thang *vattr_typethang_next(vattr_type_thang *t)
{
	t++;
	if (t->type_name) {
		return t;
	} else {
		return NULL;
	}
}

vattr_type_thang *vattr_typethang_first(vattr_type_thang *t)
{
	return t;
}

vattr_get_thang *slapi_vattr_getthang_first(vattr_get_thang *t)
{
	return t;
}

vattr_get_thang *slapi_vattr_getthang_next(vattr_get_thang *t)
{
	t++;
	if (t->get_present) {
		return t;
	} else {
		return NULL;
	}
}




/* End of the public interface functions */

/* Everything below here is the SPI interface, only callable by vattr service providers.
   Currently this interface is not public. Interface definitions are in vattr_spi.h
 */

/* Structures used in the SPI interface */

/* Service provider object */


struct _vattr_sp {
	/* vtbl */
	vattr_get_fn_type sp_get_fn;
	vattr_get_ex_fn_type sp_get_ex_fn;
	vattr_compare_fn_type sp_compare_fn;
	vattr_types_fn_type sp_types_fn;
	void *sp_data; /* Pointer for use by the Service Provider */
};
typedef struct _vattr_sp vattr_sp;

/* Service provider handle */
struct _vattr_sp_handle {
	vattr_sp *sp;
	struct _vattr_sp_handle *next; /* So we can link them together in the map */
	void *hint; /* Hint to the SP */
};

/* Calls made by Service Providers */
/* Call to register as a service provider */
/* This function needs to do the following: 
	o Let the provider say "hey, I'm here". It should probably get some handle back which it can use in future calls.
	o Say whether it wants to be called to resolve every single query, or whether it will say in advance what attrs it services.
 */

static vattr_sp_handle *vattr_sp_list = NULL;

int slapi_vattrspi_register(vattr_sp_handle **h, vattr_get_fn_type get_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn)
{
	return slapi_vattrspi_register_internal(h, get_fn, 0, compare_fn, types_fn, 0);
}

int slapi_vattrspi_register_ex(vattr_sp_handle **h, vattr_get_ex_fn_type get_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn, void *options)
{
	return slapi_vattrspi_register_internal(h, 0, get_fn, compare_fn, types_fn, options);
}

/* options not used yet - for future expansion */
int slapi_vattrspi_register_internal(vattr_sp_handle **h, vattr_get_fn_type get_fn, vattr_get_ex_fn_type get_ex_fn, vattr_compare_fn_type compare_fn, vattr_types_fn_type types_fn, void *options)
{
	vattr_sp_handle *return_to_caller = NULL;
	vattr_sp_handle *list_handle = NULL;
	vattr_sp *new_sp = NULL;
	/* Make a service provider handle */
	new_sp = (vattr_sp*)slapi_ch_calloc(1,sizeof(vattr_sp));
	if (NULL == new_sp) {
		slapd_nasty(sourcefile,7,0);
		return ENOMEM;
	}
	return_to_caller = (vattr_sp_handle*)slapi_ch_calloc(1,sizeof(vattr_sp_handle));
	if (NULL == return_to_caller) {
		slapd_nasty(sourcefile,8,0);
		return ENOMEM;
	}
	new_sp->sp_get_fn = get_fn;
	new_sp->sp_get_ex_fn = get_ex_fn;
	new_sp->sp_compare_fn = compare_fn;
	new_sp->sp_types_fn = types_fn;
	return_to_caller->sp = new_sp;
	/* Add to the service provider list */
	/* Make a handle for the list */
	list_handle = (vattr_sp_handle*)slapi_ch_calloc(1, sizeof (vattr_sp_handle));
	if (NULL == list_handle) {
		return ENOMEM;
	}
	*list_handle = *return_to_caller;
	list_handle->next = vattr_sp_list;
	vattr_sp_list = list_handle;
	/* Return the handle to the caller */
	*h = return_to_caller;
	return 0;
}

vattr_sp_handle_list *vattr_map_sp_get_complete_list()
{
	return vattr_sp_list;
}

int slapi_vattrspi_regattr(vattr_sp_handle *h,char *type_name_to_register, char* DN , void *hint)
{
	int ret = 0;
	char *type_to_add;
	int free_type_to_add = 0;

	/* Supplying a DN means that the plugin requires to be called
	 * only when the considering attributes in relevant entries - almost
	 *
	 * Actually the smallest namespace is the backend.
	 *
	 * atttributes that are tied to backends have the format DN::attr
	 * this essentially makes the attribute have split namespaces
	 * that are treated as separate attributes in the vattr code
	 */
	if(DN)
	{
		/* as a coutesy we accept any old DN and will convert
		 * to a namespace DN, this helps to hide details
		 * (that we might decide to change) anyway
		 */
		Slapi_DN original_dn;
		Slapi_Backend *be;
		Slapi_DN *namespace_dn;
		
		slapi_sdn_init(&original_dn);
		slapi_sdn_set_dn_byref(&original_dn,DN);
		be = slapi_be_select( &original_dn );
		namespace_dn = (Slapi_DN*)slapi_be_getsuffix(be, 0);

		if(namespace_dn && be != defbackend_get_backend()) /* just in case someone thinks "" is a good namespace */
		{
			type_to_add = (char*)PR_smprintf("%s::%s",
				(char*)slapi_sdn_get_dn(namespace_dn), 
				type_name_to_register);

			if(!type_to_add)
			{
				ret = -1;
			}

			free_type_to_add = 1;
		}
		else
		{
			type_to_add = type_name_to_register;
		}

		slapi_sdn_done(&original_dn);
	}
	else
	{
		type_to_add = type_name_to_register;
	}

	ret = vattr_map_sp_insert(type_to_add,h,hint);

	if(free_type_to_add)
	{
		PR_smprintf_free(type_to_add);
	}

	return ret;
}


/* Call to advertise or refute that you generate the value for some attribute, and to signal that the definition for this attribute has changed */
/* This call needs to do the following:
	o Let the SP say "you know, I will generate the value of attribute "foo", within subtree "bar". (we might ignore the subtree for now to keep things simple
	o Same as above, but say "you know, I'm not doing that any more". (we'll do that with a addordel flag).
	o Say, "you know that definition I said I did, well it's changed".
*/

/* Functions to handle the context stucture */

int slapi_vattr_context_create(vattr_context **c)
{
	return 0;
}

void slapi_vattr_context_destroy(vattr_context *c)
{
}

int vattr_call_sp_get_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *buffer_flags, void* hint)
{
	int ret = -1;

	if(handle->sp->sp_get_fn)
	{
		ret = ((handle->sp->sp_get_fn)(handle,c,e,type,results,type_name_disposition,actual_type_name,flags,buffer_flags, hint)); 
	}

	return ret;
}

int vattr_call_sp_get_batch_values(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, char **type, Slapi_ValueSet*** results,int **type_name_disposition, char*** actual_type_name, int flags, int *buffer_flags, void** hint)
{
	int ret = 0;

	if(handle->sp->sp_get_ex_fn)
	{
		ret = ((handle->sp->sp_get_ex_fn)(handle,c,e,type,results,type_name_disposition,actual_type_name,flags,buffer_flags, hint)); 
	}
	else
	{
		/* make our args look like the simple non-batched case */
		*results = (Slapi_ValueSet**)slapi_ch_calloc(2, sizeof(*results)); /* 2 for null terminated list */
		*type_name_disposition = (int *)slapi_ch_calloc(2, sizeof(*type_name_disposition));
		*actual_type_name = (char**)slapi_ch_calloc(2, sizeof(*actual_type_name));

		ret =((handle->sp->sp_get_fn)(handle,c,e,*type,*results,*type_name_disposition,*actual_type_name,flags,buffer_flags, hint)); 
		if(ret)
		{
			slapi_ch_free((void**)results );
			slapi_ch_free((void**)type_name_disposition );
			slapi_ch_free((void**)actual_type_name );
		}
	}

	return ret;
}

int vattr_call_sp_compare_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, vattr_get_thang *my_get, const char *type, Slapi_Value* test_this,int *result, int flags, void* hint)
{
	return ((handle->sp->sp_compare_fn)(handle,c,e,(char*)type,test_this,result,flags, hint)); 
}

int vattr_call_sp_get_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags)
{
		return ((handle->sp->sp_types_fn)(handle,e,type_context,flags)); 
}

/* Service provider entry point prototypes */

/* Call which allows a SP to say what attributes can be used to define a given attribute, used for loop detection */
/* Not implementing this yet */
/* typedef int (*vattrspi_explain_definition) (); */

/* Function called to indicate to the SP that the game is over, we free the handle, not the SP */
typedef int (*vattrspi_deregister) (vattr_sp_handle *h);

/* End of the SPI functions */

/* Implementation functions only after here */

/* The attribute map */

/* We need a structure which can map attribute type names to SP's which might tell us
something useful. We need a function which, given an attr type name, can return
a set of SP's to ask about said attr. SP's which don't tell us which attrs they are
handling always get asked. The first SP which tells us the answer wins, we don't
attempt to arbitrate between them. One might imaging doing this stuff in a unified
way with other stuff pertaining to types (schema, syntax). Someday. For now this
is a separate thing in the insterests of stability.
*/

/* Virtual Attribute map implementation */

/* The virtual attribute type map is implemented as a hash table.
   The single key is the attribute type base name (not an alias)
   (what about subtypes??). The hash table takes care of translating
   this key to a list of probable service providers for the type.

   Other than the obvious lookup function, we need functions to
   allow entries to be added and removed from the map.

   Because the map is likely to be consulted at least once for every search
   operation performed by the server, it's important that it is free from
   performance deficiencies such as lock contention (is the NSPR hash table
   implementation even thread safe ?

 */

#define VARRT_MAP_HASHTABLE_SIZE 10

/* Attribute map oject */
/* Needs to contain: a linked list of pointers to provider handles handles,
		The type name,
		other stuff ?

   Access to the entire map will be controlled via a single RW lock.
 */

struct _objAttrValue
{
	struct _objAttrValue *pNext;
	Slapi_Value *val;
};
typedef struct _objAttrValue objAttrValue;

struct _vattr_map_entry {
	char * /* currect ? */ type_name;
	vattr_sp_handle *sp_list;
	objAttrValue *objectclasses; /* objectclasses for this type to check schema with*/
};
typedef struct _vattr_map_entry vattr_map_entry;


vattr_map_entry test_entry = {NULL};

struct _vattr_map {
	PRRWLock  *lock;
	PLHashTable *hashtable; /* Hash table */  
};
typedef struct _vattr_map vattr_map;

static vattr_map *the_map = NULL;

static PRIntn vattr_hash_compare_keys(const void *v1, const void *v2)
{
	/* Should this be subtype aware, etc ? */
    return ( (0 == strcasecmp(v1,v2)) ? 1 : 0) ;
}

static PRIntn vattr_hash_compare_values(const void *v1, const void *v2)
{
    return( ((char *)v1 == (char *)v2 ) ? 1 : 0);
}

static PLHashNumber vattr_hash_fn(const void *type_name)
{
	/* need a hash function here */
	/* Sum the bytes for now */
	PLHashNumber result = 0;
	char * current_position = NULL;
	char current_char = 0;
	for (current_position = (char*) type_name; *current_position; current_position++) {
		current_char = tolower(*current_position);
		result += current_char;
	}
    return result;
}

static int vattr_map_create()
{
	the_map = (vattr_map*)slapi_ch_calloc(1, sizeof(vattr_map));
	if (NULL == the_map) {
		slapd_nasty(sourcefile,1,0);
		return ENOMEM;
	}

	the_map->hashtable = PL_NewHashTable(VARRT_MAP_HASHTABLE_SIZE,
	vattr_hash_fn, vattr_hash_compare_keys,
	vattr_hash_compare_values, NULL, NULL);

	if (NULL == the_map->hashtable) {
		slapd_nasty(sourcefile,2,0);
		return ENOMEM;
	}

	the_map->lock = PR_NewRWLock(0,SOURCEFILE "1");
	if (NULL == the_map) {
		slapd_nasty(sourcefile,3,0);
		return ENOMEM;
	}
	return 0;
}

static void vattr_map_destroy()
{
	if (the_map) {
		if (the_map->hashtable) {
			PL_HashTableDestroy(the_map->hashtable);
		}
		if (the_map->lock) {
			PR_DestroyRWLock(the_map->lock);
		}
	}
	slapi_ch_free ((void**)&the_map);
}

/* Returns 0 if present, entry returned in result. Returns SLAPI_VIRTUALATTRS_NOT_FOUND if not found */
static int vattr_map_lookup(const char *type_to_find, vattr_map_entry **result)
{
	char *basetype = 0;
	char *tmp = 0;
	char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];

	*result = NULL;
	PR_ASSERT(the_map);

	/* vattrs need to support subtypes
	 * we insist that one service provider
	 * will support all subtypes for a
	 * given superior, hence we look for
	 * superiors only here
	 */

	tmp = slapi_attr_basetype( type_to_find, buf, sizeof(buf));
	if(tmp)
	{
		basetype = tmp;
	}
	else
	{
		basetype = buf;
	}

	/* Get the reader lock */
	PR_RWLock_Rlock(the_map->lock);
    *result = (vattr_map_entry*)PL_HashTableLookupConst(the_map->hashtable,
					      (void*)basetype);
	/* Release ze lock */
	PR_RWLock_Unlock(the_map->lock);

	if(tmp)
	{
		slapi_ch_free_string(&tmp);
	}

	if (*result) {
		return 0;
	} else {
		return SLAPI_VIRTUALATTRS_NOT_FOUND;
	}
}

/* Insert an entry into the attribute map */
int vattr_map_insert(vattr_map_entry *vae)
{
	char *copy_of_type_name = NULL;
	PR_ASSERT(the_map);
	copy_of_type_name = slapi_ch_strdup(vae->type_name);
	if (NULL == copy_of_type_name) {
		slapd_nasty(sourcefile,6,0);
		return ENOMEM;
	}
	/* Get the writer lock */
	PR_RWLock_Wlock(the_map->lock);
	/* Insert the thing */
	/* It's illegal to call this function if the entry is already there */
	PR_ASSERT(NULL == PL_HashTableLookupConst(the_map->hashtable,(void*)copy_of_type_name));
    PL_HashTableAdd(the_map->hashtable,(void*)copy_of_type_name,(void*)vae);
	/* Unlock and we're done */
	PR_RWLock_Unlock(the_map->lock);
	return 0;
}

/* 
	vattr_delete_attrvals
	---------------------
	deletes a value list
*/
void vattr_delete_attrvals(objAttrValue **attrval)
{
	objAttrValue *val = *attrval;

	while(val)
	{
		objAttrValue *next = val->pNext;
		slapi_value_free(&val->val);
		slapi_ch_free((void**)&val);
		val = next;
	}
}

/* 
	vattr_add_attrval
	-----------------
	adds a value to an attribute value list
*/
int vattr_add_attrval(objAttrValue **attrval, char *val)
{
	int ret = 0;
	objAttrValue *theVal;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> vattr_add_attrval\n",0,0,0);

	/* create the attrvalue */
	theVal = (objAttrValue*) slapi_ch_malloc(sizeof(objAttrValue));
	if(theVal)
	{
		theVal->val = slapi_value_new_string(val);
		if(theVal->val)
		{
			theVal->pNext = *attrval;
			*attrval = theVal;
		}
		else
		{
			slapi_ch_free((void**)&theVal);
			LDAPDebug( LDAP_DEBUG_ANY, "vattr_add_attrval: failed to allocate memory\n",0,0,0);
			ret = -1;
		}
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "vattr_add_attrval: failed to allocate memory\n",0,0,0);
		ret = -1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- vattr_add_attrval\n",0,0,0);
	return ret;
}


objAttrValue *vattr_map_entry_build_schema(char *type_name)
{
	objAttrValue *ret = 0;
	struct objclass	*oc;
	int attr_index = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> vattr_map_entry_build_schema\n",0,0,0);

	/* this is the first opportunity to register
	 * with the statechange api, our init function
	 * gets called prior to loading plugins, so it
	 * was not available then
	 */
	if(!statechange_api)
	{
		/* grab statechange api - we never release this */
		if(!slapi_apib_get_interface(StateChange_v1_0_GUID, &statechange_api))
		{
			/* register for schema changes via dn */
			statechange_register(statechange_api, "vattr", "cn=schema", NULL, NULL, (notify_callback) schema_changed_callback);
		}
	}

	if(!config_get_schemacheck())
	{
		LDAPDebug( LDAP_DEBUG_TRACE, "<-- vattr_map_entry_build_schema - schema off\n",0,0,0);
		return 0;
	}

    oc_lock_read();
	for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next )
	{
		char **pppAttrs[2];
		int index;
		int attrType = 0;
		int oc_added = 0;

		pppAttrs[0] = oc->oc_required;
		pppAttrs[1] = oc->oc_allowed;

		/* we need to check both required and allowed attributes */
		while(!oc_added && attrType < 2)
		{
			if(pppAttrs[attrType])
			{
				index = 0;

				while(pppAttrs[attrType][index])
				{
					if(!PL_strcasecmp(pppAttrs[attrType][index], type_name))
					{
						vattr_add_attrval(&ret, oc->oc_name);
						oc_added = 1;
						break;
					}
					index++;
				}
			}
			attrType++;
		}
	}
    oc_unlock();

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- vattr_map_entry_build_schema\n",0,0,0);
	return ret;
}

static PRIntn vattr_map_entry_rebuild_schema(PLHashEntry *he, PRIntn i, void *arg)
{
	vattr_map_entry *entry = (vattr_map_entry *)(he->value);

	if(entry->objectclasses)
		vattr_delete_attrvals(&(entry->objectclasses));
	
	entry->objectclasses = vattr_map_entry_build_schema(entry->type_name);

	return HT_ENUMERATE_NEXT;
}

void schema_changed_callback(Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data)
{
	/* Get the writer lock */
	PR_RWLock_Wlock(the_map->lock);

	/* go through the list */
	PL_HashTableEnumerateEntries(the_map->hashtable, vattr_map_entry_rebuild_schema, 0);

	/* Unlock and we're done */
	PR_RWLock_Unlock(the_map->lock);
}


int slapi_vattr_schema_check_type(Slapi_Entry *e, char *type)
{
	int ret = 0;
	vattr_map_entry *map_entry;

	if(config_get_schemacheck())
	{		
		Slapi_Attr *attr;

		if(0 == slapi_entry_attr_find(e, "objectclass", &attr))
		{
			Slapi_ValueSet *vs;

			if(0 == slapi_attr_get_valueset(attr, &vs))
			{
				objAttrValue *obj;

				if(0 == vattr_map_lookup(type, &map_entry))
				{
					PR_RWLock_Rlock(the_map->lock);

					obj = map_entry->objectclasses;

					while(obj)
					{
						if(slapi_valueset_find(attr, vs, obj->val))
						{
							/* this entry has an objectclass
							 * that allows or requires the
							 * attribute type
							 */
							ret = -1;
							break;
						}

						obj = obj->pNext;
					}

					PR_RWLock_Unlock(the_map->lock);
				}

				slapi_valueset_free(vs);
			}
		}

	}
	else
		ret = -1;

	return ret;
}

vattr_map_entry *vattr_map_entry_new(char *type_name, vattr_sp_handle *sph, void* hint)
{
	vattr_map_entry *result = NULL;
	vattr_sp_handle *sp_copy = NULL;
	sp_copy = (vattr_sp_handle*)slapi_ch_calloc(1, sizeof (vattr_sp_handle));
	if (!sp_copy) {
		return NULL;
	}
	sp_copy->sp = sph->sp;
	sp_copy->hint = hint;
	result = (vattr_map_entry*)slapi_ch_calloc(1, sizeof (vattr_map_entry));
	if (result) {
		result->type_name = slapi_ch_strdup(type_name);
		result->sp_list = sp_copy;
	}

	/* go get schema */
	result->objectclasses = vattr_map_entry_build_schema(type_name);

	return result;
}

/* On top of the map, we need functions to manipulate the SP handles from within */

/* Function to get the SP list from the map, given a type name */
/* The resulting list is in the map, but is safe to read regardless of concurrent updates */
vattr_sp_handle_list *vattr_map_sp_getlist(char *type_to_find)
{
	int ret = 0;
	vattr_map_entry *result = NULL;
	ret = vattr_map_lookup(type_to_find,&result);
	if (0 == ret) {
		return (vattr_sp_handle_list*) result->sp_list;
	} else {
		return NULL;
	}
}

/* same as above, but filters the list based on the supplied backend dn
 * when we stored these dn based attributes, we concatenated them with
 * the dn like this dn::attribute, so we need to do two checks for the
 * attribute, one with, and one without the dn
 */
vattr_sp_handle_list *vattr_map_namespace_sp_getlist(Slapi_DN *dn, const char *type_to_find)
{
	int ret = 0;
	vattr_map_entry *result = NULL;
	vattr_sp_handle_list* return_list = 0;

	ret = vattr_map_lookup(type_to_find,&result);
	if (0 == ret) {
		return_list = (vattr_sp_handle_list*) result->sp_list;
	} else {
		/* we have allowed the global namespace provider a shot
		 * now it is time to query for split namespace providers
		 */
		if(dn) {
			char *split_dn = (char*)slapi_sdn_get_dn(dn);
			char *split_type_to_find = 
				(char*)PR_smprintf("%s::%s",split_dn, type_to_find);

			if(split_type_to_find)
			{
				ret = vattr_map_lookup(split_type_to_find,&result);
				if (0 == ret) {
					return_list = (vattr_sp_handle_list*) result->sp_list;
				}

				PR_smprintf_free(split_type_to_find);
			}
		}
	}

	return return_list;
}


/* Iterator function for the list */
vattr_sp_handle *vattr_map_sp_next(vattr_sp_handle_list *list, void **hint)
{
	vattr_sp_handle *result = NULL;
	vattr_sp_handle *current = (vattr_sp_handle*) list;
	PR_ASSERT(list);
	PR_ASSERT(hint);
	result = current->next;
	*hint = current->hint;
	return result;
}

/* Iterator function for the list */
vattr_sp_handle *vattr_map_sp_first(vattr_sp_handle_list *list, void **hint)
{
	vattr_sp_handle *result = NULL;
	PR_ASSERT(list);
	PR_ASSERT(hint);
	result = (vattr_sp_handle*)list;
	*hint = result->hint;
	return result;
}

/* Iterator function for the list */
vattr_sp_handle *vattr_list_sp_next(vattr_sp_handle_list *list)
{
	vattr_sp_handle *result = NULL;
	vattr_sp_handle *current = (vattr_sp_handle*) list;
	PR_ASSERT(list);
	result = current->next;
	return result;
}

/* Iterator function for the list */
vattr_sp_handle *vattr_list_sp_first(vattr_sp_handle_list *list)
{
	vattr_sp_handle *result = NULL;
	PR_ASSERT(list);
	result = (vattr_sp_handle*)list;
	return result;
}

/* Function to insert an SP into the map entry for a given type name */
/* Note that SP's can't ever remmove themselves from the map---if they could
we'd need to hold a lock on the read path, which we don't want to do.
So any SP which relinquishes its need to handle a type needs to continue
to handle the calls on it, but return nothing */
/* DBDB need to sort out memory ownership here, it's not quite right */

int vattr_map_sp_insert(char *type_to_add, vattr_sp_handle *sp, void *hint)
{
	int result = 0;
	vattr_map_entry *map_entry = NULL;
	/* Is this type already there ? */
	result = vattr_map_lookup(type_to_add,&map_entry);
	/* If it is, add this SP to the list, safely even if readers are traversing the list at the same time */
	if (0 == result) {
		int found = 0;
		vattr_sp_handle *list_entry = NULL;
		/* Walk the list checking that the daft SP isn't already here */
		for (list_entry = map_entry->sp_list ; list_entry; list_entry = list_entry->next) {
			if (list_entry == sp) {
				found = 1;
				break;
			}
		}
		/* If it is, we do nothing */
		if(found) {
			return 0;
		}
		/* We insert the SP handle into the linked list at the head */
		sp->next = map_entry->sp_list;
		map_entry->sp_list = sp;
	} else {
	/* If not, add it */
		map_entry = vattr_map_entry_new(type_to_add,sp,hint);
		if (NULL == map_entry) {
			return ENOMEM;
		}
		return vattr_map_insert(map_entry);
	}
	return 0;
}

/*
 * returns non-zero if type is a cachable virtual attr, zero otherwise.
 *
 * Decision point for caching vattrs...to be expanded to be configurable,
 * allow sp's to have a say etc.
*/

static int cache_all = 0;

int slapi_vattrcache_iscacheable( const char * type ) {

    int rc = 0;

	if(/*cache_all ||*/ !slapi_UTF8CASECMP((char *)type, "nsrole")) {
        rc = 1;
    }

    return(rc);
}

/*
 * slapi_vattrcache_cache_all and slapi_vattrcache_cache_none
 * ----------------------------------------------------------
 * limited control for deciding whether to
 * cache anything, in reality controls whether
 * to cache cos attributes right now
 */
void slapi_vattrcache_cache_all()
{
	cache_all = -1;
}

void slapi_vattrcache_cache_none()
{
	cache_all = 0;
}

void vattrcache_entry_READ_LOCK(const Slapi_Entry *e){
	PR_RWLock_Rlock(e->e_virtual_lock);
}

void vattrcache_entry_READ_UNLOCK(const Slapi_Entry *e) {
	PR_RWLock_Unlock(e->e_virtual_lock);

}
void vattrcache_entry_WRITE_LOCK(const Slapi_Entry *e){
	PR_RWLock_Wlock(e->e_virtual_lock);

}
void vattrcache_entry_WRITE_UNLOCK(const Slapi_Entry *e){
	PR_RWLock_Unlock(e->e_virtual_lock);
}

#ifdef VATTR_TEST_CODE

/* Prototype SP begins here */

/* Get value function */
int vattr_basic_sp_get_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *buffer_flags, void *hint)
{
	Slapi_Value *value = NULL;
	*buffer_flags = 0;
	*actual_type_name = slapi_ch_strdup("dbtestattr");
	value = slapi_value_new_string("Hello Client");
	*results = slapi_ch_calloc(1,sizeof(Slapi_ValueSet));
	slapi_valueset_init(*results);
	slapi_valueset_add_value_ext(*results,value,SLAPI_VALUE_FLAG_PASSIN);
	return 0;
}

/* Compare value function */
int vattr_basic_sp_compare_value(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result,int flags, void *hint)
{
	*result = 0;
	return 0;
}

int vattr_basic_sp_list_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags)
{
	static char* test_type_name = "dbtestattr";
	vattr_type_thang thang = {0};

	thang.type_name = test_type_name;
	thang.type_flags = 0;

	slapi_vattrspi_add_type(type_context,&thang,SLAPI_VIRTUALATTRS_REQUEST_POINTERS);
	return 0;
}

int vattr_basic_sp_init()
{
	int ret = 0;
	vattr_sp_handle *my_handle = NULL;
	/* Register SP */
	ret = slapi_vattrspi_register(&my_handle,vattr_basic_sp_get_value, vattr_basic_sp_compare_value, vattr_basic_sp_list_types);
	if (ret) {
		slapd_nasty(sourcefile,4,0);
		return ret;
	}
	/* Register interest in some attribute over the entire tree */
	ret = slapi_vattrspi_regattr(my_handle,"dbtestattr","", NULL);
	if (ret) {
		slapd_nasty(sourcefile,5,0);
		return ret;
	}
	/* Register interest in some attribute over the entire tree */
	ret = slapi_vattrspi_regattr(my_handle,"dbtestattr1","", NULL);
	if (ret) {
		slapd_nasty(sourcefile,5,0);
		return ret;
	}
	/* Register interest in some attribute over the entire tree */
	ret = slapi_vattrspi_regattr(my_handle,"dbtestattr2","", NULL);
	if (ret) {
		slapd_nasty(sourcefile,5,0);
		return ret;
	}
	/* Register interest in some attribute over the entire tree */
	ret = slapi_vattrspi_regattr(my_handle,"dbtestatt3r","", NULL);
	if (ret) {
		slapd_nasty(sourcefile,5,0);
		return ret;
	}
	return ret;
}

/* What do we do on shutdown ? */
int vattr_basic_sp_cleanup()
{
	return 0;
}

#endif




