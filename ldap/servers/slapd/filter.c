/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* filter.c - routines for parsing and dealing with filters */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "slapi-plugin.h"

static int
get_filter_list( Connection *conn, BerElement *ber,
		struct slapi_filter **f, char **fstr, int maxdepth, int curdepth,
		int *subentry_dont_rewrite, int *has_tombstone_filter);
static int	get_substring_filter();
static int	get_extensible_filter( BerElement *ber, mr_filter_t* );

static int get_filter_internal( Connection *conn, BerElement *ber,
		struct slapi_filter **filt, char **fstr, int maxdepth, int curdepth,
		int *subentry_dont_rewrite, int *has_tombstone_filter);
static int tombstone_check_filter(Slapi_Filter *f);
static void filter_optimize(Slapi_Filter *f);



/*
 * Read a filter off the wire and create a slapi_filter and string representation.
 * Both filt and fstr are allocated by this function, so must be freed by the caller.
 *
 * If the scope is not base and (objectclass=ldapsubentry) does not occur
 * in the filter then we add (!(objectclass=ldapsubentry)) to the filter
 * so that subentries are not returned.
 * If the scope is base or (objectclass=ldapsubentry) occurs in the filter,
 * then the caller is explicitly handling subentries himself and so we leave
 * the filter as is.
 */
int
get_filter( Connection *conn, BerElement *ber, int scope,
			struct slapi_filter **filt, char **fstr )
{
	int subentry_dont_rewrite = 0; /* Re-write unless we're told not to */
	int has_tombstone_filter = 0; /* Check if nsTombstone appears */
	int return_value = 0;
	char 	*logbuf = NULL;
	size_t	logbufsize = 0;

	return_value = get_filter_internal(conn, ber, filt, fstr,
			config_get_max_filter_nest_level(),	/* maximum depth */
			0, /* current depth */
			&subentry_dont_rewrite, &has_tombstone_filter);

	if (0 == return_value) { /* Don't try to re-write if there was an error */
		if (subentry_dont_rewrite || scope == LDAP_SCOPE_BASE)
		  (*filt)->f_flags |= SLAPI_FILTER_LDAPSUBENTRY;
		if (has_tombstone_filter)
			(*filt)->f_flags |= SLAPI_FILTER_TOMBSTONE;
	}

	if (LDAPDebugLevelIsSet( LDAP_DEBUG_FILTER ) && *filt != NULL
			&& *fstr != NULL) {
		logbufsize = strlen(*fstr) + 1;
		logbuf = slapi_ch_malloc(logbufsize);
		*logbuf = '\0';
		slapi_log_error( SLAPI_LOG_FATAL, "get_filter", "before optimize: %s\n",
				slapi_filter_to_string(*filt, logbuf, logbufsize), 0, 0 );
	}

	filter_optimize(*filt);

	if (NULL != logbuf) {
		slapi_log_error( SLAPI_LOG_FATAL, "get_filter", " after optimize: %s\n",
				slapi_filter_to_string(*filt, logbuf, logbufsize), 0, 0 );
		slapi_ch_free_string( &logbuf );
	}

	return return_value;
}


#define FILTER_EQ_FMT       "(%s=%s)"
#define FILTER_GE_FMT       "(%s>=%s)"
#define FILTER_LE_FMT       "(%s<=%s)"
#define FILTER_APROX_FMT    "(%s~=%s)"
#define FILTER_EXTENDED_FMT "(%s%s%s%s:=%s)"
#define FILTER_EQ_LEN	4
#define FILTER_GE_LEN	5
#define FILTER_LE_LEN	5
#define FILTER_APROX_LEN 5


/* returns escaped filter string for extended filters only*/

static char *
filter_escape_filter_value_extended(struct slapi_filter *f) 
{
    char ebuf[BUFSIZ], *ptr;
    const char *estr;
    
    estr  = escape_filter_value( f->f_mr_value.bv_val, f->f_mr_value.bv_len, ebuf );
	ptr = slapi_ch_smprintf(FILTER_EXTENDED_FMT,
        f->f_mr_type ? f->f_mr_type : "",
        f->f_mr_dnAttrs ? ":dn" : "",
        f->f_mr_oid ? ":" : "",
        f->f_mr_oid ? f->f_mr_oid : "",
        estr );
    return ptr;
}

/* returns escaped filter string for EQ, LE, GE and APROX filters */

static char *
filter_escape_filter_value(struct slapi_filter *f, const char *fmt, size_t len) 
{
    char ebuf[BUFSIZ], *ptr;
    const char *estr;
    
	estr  = escape_filter_value( f->f_avvalue.bv_val, f->f_avvalue.bv_len, ebuf );
	filter_compute_hash(f);
    ptr = slapi_ch_smprintf(fmt, f->f_avtype, estr );
    return ptr;
}


/*
 * get_filter_internal(): extract an LDAP filter from a BerElement and create
 *	a slapi_filter structure (*filt) and a string equivalent (*fstr).
 *
 * This function is recursive. It calls itself (to process NOT filters) and
 *	it calls get_filter_list() for AND and OR filters, and get_filter_list()
 *	calls this function again.
 */
static int
get_filter_internal( Connection *conn, BerElement *ber, 
	struct slapi_filter **filt, char **fstr, int maxdepth, int curdepth,
	int *subentry_dont_rewrite, int *has_tombstone_filter )
{
    unsigned long	len;
    int		err;
    struct slapi_filter	*f;
    char		*ftmp, *type;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> get_filter_internal\n", 0, 0, 0 );

	/*
	 * Track and check the depth of nesting.  Use post-increment on
	 * current depth here because this function is called for the
	 * top-level filter (which does not count towards the maximum depth).
	 */
	if ( ( curdepth++ > maxdepth ) && ( maxdepth > 0 )) {
		*filt = NULL;
		*fstr = NULL;
		err = LDAP_UNWILLING_TO_PERFORM;
		LDAPDebug( LDAP_DEBUG_FILTER, "<= get_filter_internal %d"
				" (maximum nesting level of %d exceeded)\n",
				err, maxdepth, 0 );
		return( err );
	}

	/*
	 * A filter looks like this coming in:
	 *	Filter ::= CHOICE {
	 *		and		[0]	SET OF Filter,
	 *		or		[1]	SET OF Filter,
	 *		not		[2]	Filter,
	 *		equalityMatch	[3]	AttributeValueAssertion,
	 *		substrings	[4]	SubstringFilter,
	 *		greaterOrEqual	[5]	AttributeValueAssertion,
	 *		lessOrEqual	[6]	AttributeValueAssertion,
	 *		present		[7]	AttributeType,
	 *		approxMatch	[8]	AttributeValueAssertion,
	 *		extensibleMatch	[9]	MatchingRuleAssertion --v3 only
	 *	}
	 *
	 *	SubstringFilter ::= SEQUENCE {
	 *		type               AttributeType,
	 *		SEQUENCE OF CHOICE {
	 *			initial          [0] IA5String,
	 *			any              [1] IA5String,
	 *			final            [2] IA5String
	 *		}
	 *	}
	 *
	 * The extensibleMatch was added in LDAPv3:
	 *
	 *	MatchingRuleAssertion ::= SEQUENCE {
	 *		matchingRule	[1] MatchingRuleID OPTIONAL,
	 *		type		[2] AttributeDescription OPTIONAL,
	 *		matchValue	[3] AssertionValue,
	 *		dnAttributes	[4] BOOLEAN DEFAULT FALSE
	 *	}
	 */

	f = (struct slapi_filter *) slapi_ch_calloc( 1, sizeof(struct slapi_filter) );

	err = 0;
	*fstr = NULL;
	f->f_choice = ber_peek_tag( ber, &len );
	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		LDAPDebug( LDAP_DEBUG_FILTER, "EQUALITY\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {

			if ( 0 == strcasecmp ( f->f_avtype, "objectclass")) {
				/* Process objectclass oid's here */
				if (strchr (f->f_avvalue.bv_val, '.')) {
					char	*ocname = oc_find_name( f->f_avvalue.bv_val );

					if ( NULL != ocname ) {
						slapi_ch_free((void**)&f->f_avvalue.bv_val );
						f->f_avvalue.bv_val = ocname;
						f->f_avvalue.bv_len = strlen ( f->f_avvalue.bv_val );
					}
				}

				/*
				 * Process subentry searches here.
				 * Only set (*subentry_dont_rewrite) if it's not already set.
				 */

				if (!(*subentry_dont_rewrite)) {
					*subentry_dont_rewrite = subentry_check_filter(f);
				}
				/* 
				 * Check if it's a Tomstone filter.
				 * We need to do it once per filter, so if flag is already set,
				 * don't bother doing it
				 */
				if (!(*has_tombstone_filter)) {
					*has_tombstone_filter = tombstone_check_filter(f);
				}
			} 
			*fstr=filter_escape_filter_value(f, FILTER_EQ_FMT, FILTER_EQ_LEN);
		}
		break;

	case LDAP_FILTER_SUBSTRINGS:
		LDAPDebug( LDAP_DEBUG_FILTER, "SUBSTRINGS\n", 0, 0, 0 );
		err = get_substring_filter( conn, ber, f, fstr );
		break;

	case LDAP_FILTER_GE:
		LDAPDebug( LDAP_DEBUG_FILTER, "GE\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
		  *fstr=filter_escape_filter_value(f, FILTER_GE_FMT, FILTER_GE_LEN);
		}
		break;

	case LDAP_FILTER_LE:
		LDAPDebug( LDAP_DEBUG_FILTER, "LE\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
		  *fstr=filter_escape_filter_value(f, FILTER_LE_FMT, FILTER_LE_LEN);
		}
		break;

	case LDAP_FILTER_PRESENT:
		LDAPDebug( LDAP_DEBUG_FILTER, "PRESENT\n", 0, 0, 0 );
		if ( ber_scanf( ber, "a", &type ) == LBER_ERROR ) {
			err = LDAP_PROTOCOL_ERROR;
		} else {
			err = LDAP_SUCCESS;
			f->f_type = slapi_attr_syntax_normalize( type );
			free( type );
			filter_compute_hash(f);
			*fstr = slapi_ch_smprintf( "(%s=*)", f->f_type );
		}
		break;

	case LDAP_FILTER_APPROX:
		LDAPDebug( LDAP_DEBUG_FILTER, "APPROX\n", 0, 0, 0 );
		if ( (err = get_ava( ber, &f->f_ava )) == 0 ) {
		  *fstr=filter_escape_filter_value(f, FILTER_APROX_FMT, FILTER_APROX_LEN);
		}
		break;

    case LDAP_FILTER_EXTENDED:
        LDAPDebug( LDAP_DEBUG_FILTER, "EXTENDED\n", 0, 0, 0 );
        if ( conn->c_ldapversion < 3 ) {
            LDAPDebug( LDAP_DEBUG_ANY,
                "extensible filter received from v2 client\n",
                0, 0, 0 );
            err = LDAP_PROTOCOL_ERROR;
        } else if ( (err = get_extensible_filter( ber, &f->f_mr )) == LDAP_SUCCESS ) {           
            *fstr=filter_escape_filter_value_extended(f); 
            LDAPDebug (LDAP_DEBUG_FILTER, "%s\n", *fstr, 0, 0);
            if(f->f_mr_oid==NULL) {
            /*
            * We accept:
            * A) attr ":=" value
            * B) attr ":dn" ":=" value
                */
                err = LDAP_SUCCESS;
            } else {
                err = plugin_mr_filter_create (&f->f_mr);
            }
        }
        break;

	case LDAP_FILTER_AND:
		LDAPDebug( LDAP_DEBUG_FILTER, "AND\n", 0, 0, 0 );
		if ( (err = get_filter_list( conn, ber, &f->f_and, &ftmp, maxdepth,
					curdepth, subentry_dont_rewrite, has_tombstone_filter ))
					== 0 ) {
			filter_compute_hash(f);
			*fstr = slapi_ch_smprintf( "(&%s)", ftmp );
			slapi_ch_free((void**)&ftmp );
		}
		break;

	case LDAP_FILTER_OR:
		LDAPDebug( LDAP_DEBUG_FILTER, "OR\n", 0, 0, 0 );
		if ( (err = get_filter_list( conn, ber, &f->f_or, &ftmp, maxdepth,
					curdepth, subentry_dont_rewrite, has_tombstone_filter ))
					== 0 ) {
			filter_compute_hash(f);
			*fstr = slapi_ch_smprintf( "(|%s)", ftmp );
			slapi_ch_free((void**)&ftmp );
		}
		break;

	case LDAP_FILTER_NOT:
		LDAPDebug( LDAP_DEBUG_FILTER, "NOT\n", 0, 0, 0 );
		(void) ber_skip_tag( ber, &len );
		if ( (err = get_filter_internal( conn, ber, &f->f_not, &ftmp, maxdepth,
					curdepth, subentry_dont_rewrite, has_tombstone_filter ))
					== 0 ) {
			filter_compute_hash(f);
			*fstr = slapi_ch_smprintf( "(!%s)", ftmp );
			slapi_ch_free((void**)&ftmp );
		}
		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY, "get_filter_internal: unknown type 0x%lX\n",
		    f->f_choice, 0, 0 );
		err = LDAP_PROTOCOL_ERROR;
		break;
	}

	if ( err != 0 ) {
		slapi_filter_free( f, 1 );
		f = NULL;
		slapi_ch_free( (void**)fstr );
	}
	*filt = f;
	LDAPDebug( LDAP_DEBUG_FILTER, "<= get_filter_internal %d\n", err, 0, 0 );
	return( err );
}

static int
get_filter_list( Connection *conn, BerElement *ber,
				struct slapi_filter **f, char **fstr, int maxdepth,
				int curdepth, int *subentry_dont_rewrite,
				int *has_tombstone_filter)
{
	struct slapi_filter	**new;
	int		err;
	unsigned long	tag, len;
	char		*last;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> get_filter_list\n", 0, 0, 0 );

	*fstr = NULL;
	new = f;
	for ( tag = ber_first_element( ber, &len, &last );
	    tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
	    tag = ber_next_element( ber, &len, last ) ) {
		char *ftmp;
		if ( (err = get_filter_internal( conn, ber, new, &ftmp, maxdepth,
					curdepth, subentry_dont_rewrite, has_tombstone_filter))
					!= 0 ) {
		    if ( *fstr != NULL ) {
			slapi_ch_free((void**)fstr );
		    }
		    return( err );
		}
		if ( *fstr == NULL ) {
			*fstr = ftmp;
		} else {
			*fstr = slapi_ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( ftmp ) + 1 );
			strcat( *fstr, ftmp );
			slapi_ch_free((void**)&ftmp );
		}
		new = &(*new)->f_next;
	}
	*new = NULL;

	if ( tag == LBER_ERROR && *fstr != NULL ) {
		slapi_ch_free((void**)fstr );
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= get_filter_list\n", 0, 0, 0 );
	return(( *fstr == NULL ) ? LDAP_PROTOCOL_ERROR : 0 );
}

static int
get_substring_filter(
    Connection		*conn,
    BerElement		*ber,
    struct slapi_filter	*f,
    char		**fstr
)
{
	unsigned long	tag, len, rc;
	char		*val, *last, *type;
	char		ebuf[BUFSIZ];

	LDAPDebug( LDAP_DEBUG_FILTER, "=> get_substring_filter\n", 0, 0, 0 );

	if ( ber_scanf( ber, "{a", &type ) == LBER_ERROR ) {
		return( LDAP_PROTOCOL_ERROR );
	}
	f->f_sub_type = slapi_attr_syntax_normalize( type );
	free( type );
	f->f_sub_initial = NULL;
	f->f_sub_any = NULL;
	f->f_sub_final = NULL;

	*fstr = slapi_ch_malloc( strlen( f->f_sub_type ) + 3 );
	sprintf( *fstr, "(%s=", f->f_sub_type );
	for ( tag = ber_first_element( ber, &len, &last );
	    tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
	    tag = ber_next_element( ber, &len, last ) )
	{
		rc = ber_scanf( ber, "a", &val );
		if ( rc == LBER_ERROR ) {
			return( LDAP_PROTOCOL_ERROR );
		}
		if ( val == NULL || *val == '\0' ) {
			if ( val != NULL ) {
				free( val );
			}
			return( LDAP_INVALID_SYNTAX );
		}

		switch ( tag ) {
		case LDAP_SUBSTRING_INITIAL:
			LDAPDebug( LDAP_DEBUG_FILTER, "  INITIAL\n", 0, 0, 0 );
			if ( f->f_sub_initial != NULL ) {
				return( LDAP_PROTOCOL_ERROR );
			}
			f->f_sub_initial = val;
    	    /* jcm: Had to cast away a const */
			val = (char*)escape_filter_value( val, -1, ebuf );
			*fstr = slapi_ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 1 );
			strcat( *fstr, val );
			break;

		case LDAP_SUBSTRING_ANY:
			LDAPDebug( LDAP_DEBUG_FILTER, "  ANY\n", 0, 0, 0 );
			charray_add( &f->f_sub_any, val );
    	    /* jcm: Had to cast away a const */
			val = (char*)escape_filter_value( val, -1, ebuf );
			*fstr = slapi_ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 2 );
			strcat( *fstr, "*" );
			strcat( *fstr, val );
			break;

		case LDAP_SUBSTRING_FINAL:
			LDAPDebug( LDAP_DEBUG_FILTER, "  FINAL\n", 0, 0, 0 );
			if ( f->f_sub_final != NULL ) {
				return( LDAP_PROTOCOL_ERROR );
			}
			f->f_sub_final = val;
    	    /* jcm: Had to cast away a const */
			val = (char*)escape_filter_value( val, -1, ebuf );
			*fstr = slapi_ch_realloc( *fstr, strlen( *fstr ) +
			    strlen( val ) + 2 );
			strcat( *fstr, "*" );
			strcat( *fstr, val );
			break;

		default:
			LDAPDebug( LDAP_DEBUG_FILTER, "  unknown tag 0x%lX\n", tag, 0, 0 );
			return( LDAP_PROTOCOL_ERROR );
		}
	}

	if ( tag == LBER_ERROR ) {
		return( LDAP_PROTOCOL_ERROR );
	}
	if ( f->f_sub_initial == NULL && f->f_sub_any == NULL &&
	    f->f_sub_final == NULL ) {
		return( LDAP_PROTOCOL_ERROR );
	}

	filter_compute_hash(f);
	*fstr = slapi_ch_realloc( *fstr, strlen( *fstr ) + 3 );
	if ( f->f_sub_final == NULL ) {
		strcat( *fstr, "*" );
	}
	strcat( *fstr, ")" );

	LDAPDebug( LDAP_DEBUG_FILTER, "<= get_substring_filter\n", 0, 0, 0 );
	return( 0 );
}

static int
get_extensible_filter( BerElement *ber, mr_filter_t* mrf )
{
	int		gotelem, gotoid, gotvalue;
	unsigned long	tag, len;
	char		*last;
	int		rc = LDAP_PROTOCOL_ERROR;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> get_extensible_filter\n", 0, 0, 0 );
	memset (mrf, 0, sizeof (mr_filter_t));

	gotelem = gotoid = gotvalue = 0;
	for ( tag = ber_first_element( ber, &len, &last );
	    tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
	    tag = ber_next_element( ber, &len, last ) ) {
		/*
		 * order of elements goes like this:
		 *
		 *	[oid][type]value[dnattr]
		 *
		 * where either oid or type is required.
		 */
		switch ( tag ) {
		case LDAP_TAG_MRA_OID:
			if ( gotelem != 0 ) {
				goto parsing_error;
			}
			rc = ber_scanf( ber, "a", &mrf->mrf_oid );
			gotoid = 1;
			gotelem++;
			break;
		case LDAP_TAG_MRA_TYPE:
			if ( gotelem != 0 ) {
				if ( gotelem != 1 || gotoid != 1 ) {
					goto parsing_error;
				}
			}
			{
			    char* type;
			    if (ber_scanf( ber, "a", &type ) == LBER_ERROR) {
				rc = LDAP_PROTOCOL_ERROR;
			    } else {
				mrf->mrf_type = slapi_attr_syntax_normalize(type);
				free (type);
			    }
			}
			gotelem++;
			break;
		case LDAP_TAG_MRA_VALUE:
			if ( gotelem != 1 && gotelem != 2 ) {
				goto parsing_error;
			}
			rc = ber_scanf( ber, "o", &mrf->mrf_value );
			gotvalue = 1;
			gotelem++;
			break;
		case LDAP_TAG_MRA_DNATTRS:
			if ( gotvalue != 1 ) {
				goto parsing_error;
			}
			rc = ber_scanf( ber, "b", &mrf->mrf_dnAttrs );
			gotelem++;
			break;
		default:
			goto parsing_error;
		}
		if ( rc == -1 ) {
			goto parsing_error;
		}
		rc = LDAP_SUCCESS;
	}

	if ( tag == LBER_ERROR ) {
		goto parsing_error;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= get_extensible_filter %i\n", rc, 0, 0 );
	return rc;

parsing_error:;
	LDAPDebug( LDAP_DEBUG_ANY, "error parsing extensible filter\n",
	    0, 0, 0 );
	return( LDAP_PROTOCOL_ERROR );
}


Slapi_Filter *
slapi_filter_dup(Slapi_Filter *f)
{
	Slapi_Filter *out = 0;
	struct slapi_filter *fl = 0;
	struct slapi_filter **outl = 0;
	struct slapi_filter *lastout = 0;

	if ( f == NULL ) {
		return NULL;
	}

	out = (struct slapi_filter*)calloc(1, sizeof(struct slapi_filter));
	if ( out == NULL ) {
		LDAPDebug(LDAP_DEBUG_ANY, "slapi_filter_dup: memory allocation error\n",
		    0, 0, 0 );
		return NULL;
	}

	out->f_choice = f->f_choice;
	out->f_hash = f->f_hash;

	LDAPDebug( LDAP_DEBUG_FILTER, "slapi_filter_dup type 0x%lX\n", f->f_choice, 0, 0 );
	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		out->f_ava.ava_type = slapi_ch_strdup(f->f_ava.ava_type);
		out->f_ava.ava_value.bv_val = slapi_ch_malloc(f->f_ava.ava_value.bv_len+1);
		memcpy(out->f_ava.ava_value.bv_val,f->f_ava.ava_value.bv_val,f->f_ava.ava_value.bv_len);
		out->f_ava.ava_value.bv_val[f->f_ava.ava_value.bv_len] = 0; /* terminate */
		out->f_ava.ava_value.bv_len = f->f_ava.ava_value.bv_len;
		break;

	case LDAP_FILTER_SUBSTRINGS:

		out->f_sub_type = slapi_ch_strdup(f->f_sub_type);
		out->f_sub_initial = slapi_ch_strdup(f->f_sub_initial );
		out->f_sub_any = charray_dup( f->f_sub_any );
		out->f_sub_final = slapi_ch_strdup(f->f_sub_final );
		break;

	case LDAP_FILTER_PRESENT:
		out->f_type = slapi_ch_strdup( f->f_type );
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		outl = &out->f_list;

/*		out->f_list = slapi_filter_dup(f->f_list);
*/
		for (fl = f->f_list; fl != NULL; fl = fl->f_next) {
			(*outl) = slapi_filter_dup( fl );
			(*outl)->f_next = 0;
			if(lastout)
				lastout->f_next = *outl;
			lastout = *outl;
			outl = &((*outl)->f_next);
		}
		break;

	case LDAP_FILTER_EXTENDED:
		/* something needs to be done here, but Im not sure how to do it
		slapi_ch_free((void**)&f->f_mr_oid);
		slapi_ch_free((void**)&f->f_mr_type);
		slapi_ch_free((void **)&f->f_mr_value.bv_val );
		if (f->f_mr.mrf_destroy != NULL) {
		    Slapi_PBlock pb;
		    pblock_init (&pb);
		    if ( ! slapi_pblock_set (&pb, SLAPI_PLUGIN_OBJECT, f->f_mr.mrf_object)) {
			f->f_mr.mrf_destroy (&pb);
		    }
		}
		*/
		break;

	default:
		LDAPDebug(LDAP_DEBUG_FILTER, "slapi_filter_dup: unknown type 0x%lX\n",
		    f->f_choice, 0, 0 );
		break;
	}
	
	return out;
}

void
slapi_filter_free( struct slapi_filter *f, int recurse )
{
	if ( f == NULL ) {
		return;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "slapi_filter_free type 0x%lX\n", f->f_choice, 0, 0 );
	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		ava_done( &f->f_ava );
		break;

	case LDAP_FILTER_SUBSTRINGS:
		slapi_ch_free((void**)&f->f_sub_type );
		slapi_ch_free((void**)&f->f_sub_initial );
		charray_free( f->f_sub_any );
		slapi_ch_free((void**)&f->f_sub_final );
		break;

	case LDAP_FILTER_PRESENT:
		slapi_ch_free((void**)&f->f_type );
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		if ( recurse ) {
			struct slapi_filter *fl, *next;

			for (fl = f->f_list; fl != NULL; fl = next) {
			    next = fl->f_next;
			    fl->f_next = NULL;
			    slapi_filter_free( fl, recurse );
			    fl = next;
			}
		}
		break;

	case LDAP_FILTER_EXTENDED:
		slapi_ch_free((void**)&f->f_mr_oid);
		slapi_ch_free((void**)&f->f_mr_type);
		slapi_ch_free((void **)&f->f_mr_value.bv_val );
		if (f->f_mr.mrf_destroy != NULL) {
		    Slapi_PBlock pb;
		    pblock_init (&pb);
		    if ( ! slapi_pblock_set (&pb, SLAPI_PLUGIN_OBJECT, f->f_mr.mrf_object)) {
			f->f_mr.mrf_destroy (&pb);
		    }
		}
		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY, "slapi_filter_free: unknown type 0x%lX\n",
		    f->f_choice, 0, 0 );
		break;
	}
	slapi_ch_free((void**)&f);
}

#if 0
static void
filter_list_insert( struct slapi_filter **into, struct slapi_filter *from )
{
    struct slapi_filter *f;
    if (into == NULL || from == NULL) return;
    if (*into != NULL) {
	for (f = from; f->f_next != NULL; f = f->f_next);
	f->f_next = *into;
    }
    *into = from;
}
#endif


struct slapi_filter *
slapi_filter_join( int ftype, struct slapi_filter *f1, struct slapi_filter *f2)
{
	return slapi_filter_join_ex( ftype, f1, f2, 1 );

}


struct slapi_filter *
slapi_filter_join_ex( int ftype, struct slapi_filter *f1, struct slapi_filter *f2, int recurse_always )
{
	struct slapi_filter *fjoin;
	struct slapi_filter *add_to;
	struct slapi_filter *add_this;
	struct slapi_filter *return_this;
	int insert = 0;

	if(!recurse_always)
	{
		/* try to optimise the filter join */
		switch(ftype)
		{
		case LDAP_FILTER_AND:
		case LDAP_FILTER_OR:
			if(ftype == (int)f1->f_choice)
			{
				add_to = f1;
				add_this = f2;
				insert = 1;
			}
			else if(ftype == (int)f2->f_choice)
			{
				add_to = f2;
				add_this = f1;
				insert = 1;
			}

		default:
			break;
		}
	}

	if(insert)
	{
		/* try to avoid ! filters as the first arg */
		if(add_to->f_list->f_choice == LDAP_FILTER_NOT)
		{
			add_this->f_next = add_to->f_list;
			add_to->f_list = add_this;
			filter_compute_hash(add_to);
			return_this = add_to;
		}
		else
		{
			/* find end of list, add the filter */
			for (fjoin = add_to->f_list; fjoin != NULL; fjoin = fjoin->f_next) {
				if(fjoin->f_next == NULL)
				{
					fjoin->f_next = add_this;
					filter_compute_hash(add_to);
					return_this = add_to;
					break;
				}
			}
		}
	}
	else
	{
		fjoin = (struct slapi_filter *) slapi_ch_calloc( 1, sizeof(struct slapi_filter) );
		fjoin->f_choice = ftype;
		fjoin->f_next = NULL;
		/* try to ensure ! filters dont cause allid search */
		if(f1->f_choice == LDAP_FILTER_NOT && f2)
		{
			fjoin->f_list = f2;
			f2->f_next = f1;
		}
		else
		{
			fjoin->f_list = f1;
			f1->f_next = f2;
		}
		filter_compute_hash(fjoin);
		return_this = fjoin;
	}

	return( return_this );
}

int
slapi_filter_get_choice( struct slapi_filter *f )
{
	return( f->f_choice );
}

int
slapi_filter_get_ava( struct slapi_filter *f, char **type, struct berval **bval )
{
	switch ( f->f_choice ) {
	  case LDAP_FILTER_EQUALITY:
	  case LDAP_FILTER_GE:
	  case LDAP_FILTER_LE:
	  case LDAP_FILTER_APPROX:
	    break;
	  default:
	    *type = NULL;
	    *bval = NULL;
	    return( -1 );
	}
	*type = f->f_avtype;
	*bval = &f->f_avvalue;
	return( 0 );
}

/* Deprecated--use slapi_filter_get_attribute_type() now */

SLAPI_DEPRECATED int
slapi_filter_get_type( struct slapi_filter *f, char **type )
{
	if ( f->f_choice != LDAP_FILTER_PRESENT ) {
		return( -1 );
	}
	*type = f->f_type;

	return( 0 );
}

/*
 * Return the attribute type for all simple filter choices into type.
 * ie. for all except LDAP_FILTER_AND, LDAP_FILTER_OR and LDAP_FILTER_NOT.
 *
 * The returned type is "as is" and so may not be normalized.
 *  Returns 0 for success, -1 otherwise.
*/

int 
slapi_filter_get_attribute_type( Slapi_Filter *f, char **type )
{    

    if ( f == NULL ) {
		return -1;
    }

    switch ( f->f_choice ) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
		*type = f->f_ava.ava_type;
	break;
    case LDAP_FILTER_SUBSTRINGS:
		*type = f->f_sub_type;	
	break;
    case LDAP_FILTER_PRESENT:
 		*type = f->f_type;
	break;
    case LDAP_FILTER_EXTENDED:
		*type = f->f_mr_type;
	break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
		return(-1);
    default:
		/* Unknown filter choice */
		return -1;
    }

	/* success */
	return(0); 
}

struct slapi_filter *
slapi_filter_list_first( struct slapi_filter *f )
{
	if ( f->f_choice != LDAP_FILTER_AND && f->f_choice != LDAP_FILTER_OR
	    && f->f_choice != LDAP_FILTER_NOT ) {
		return( NULL );
	}
	return( f->f_list );
}

struct slapi_filter *
slapi_filter_list_next( struct slapi_filter *f, struct slapi_filter *fprev )
{
	return( fprev->f_next );
}

int
slapi_filter_get_subfilt(
    struct slapi_filter	*f,
    char		**type,
    char		**initial,
    char		***any,
    char		**final
)
{
	if ( f->f_choice != LDAP_FILTER_SUBSTRINGS ) {
		return( -1 );
	}
	*type = f->f_sub_type;
	*initial = f->f_sub_initial;
	*any = f->f_sub_any;
	*final = f->f_sub_final;

	return( 0 );
}

static void
filter_normalize_ava( struct ava *ava, int ftype )
{
    char	*tmp;

    if ( ava == NULL ) {
	return;
    }
    tmp = ava->ava_type;
    ava->ava_type = slapi_attr_syntax_normalize(tmp);
    slapi_ch_free((void**)&tmp );
    /* value will be normalized later */
}


void filter_normalize( struct slapi_filter *f );

static void
filter_normalize_list( struct slapi_filter *flist )
{
    struct slapi_filter *f;

    for ( f = flist; f != NULL; f = f->f_next ) {
	filter_normalize( f );
    }
}



/*
 * Normalize all values and types in a filter.  This isn't necessary
 * when we've read the slapi_filter off the wire, but if we've hand-constructed
 * a filter inside slapd (e.g. when calling the routines in wrapper.c),
 * we've called slapi_str2filter on something which *didn't* come over the wire,
 * so the attribute names and filters in the filter struct aren't
 * normalized.
 */
void
filter_normalize( struct slapi_filter *f )
{
    char	*tmp;

    if ( f == NULL ) {
	return;
    }

    switch ( f->f_choice ) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
	filter_normalize_ava( &f->f_ava, f->f_choice );
	break;
    case LDAP_FILTER_SUBSTRINGS:
	tmp = f->f_sub_type;
	f->f_sub_type = slapi_attr_syntax_normalize(tmp);
	slapi_ch_free((void**)&tmp );
	/* value will be normalized later */
	break;
    case LDAP_FILTER_PRESENT:
	tmp = f->f_type;
	f->f_type = slapi_attr_syntax_normalize(tmp);
	slapi_ch_free((void**)&tmp );
	break;
    case LDAP_FILTER_EXTENDED:
	tmp = f->f_mr_type;
	f->f_mr_type = slapi_attr_syntax_normalize(tmp);
	slapi_ch_free((void**)&tmp );
	break;
    case LDAP_FILTER_AND:
	filter_normalize_list( f->f_and );
	break;
    case LDAP_FILTER_OR:
	filter_normalize_list( f->f_or );
	break;
    case LDAP_FILTER_NOT:
	filter_normalize_list( f->f_not );
	break;
    default:
	return;
    }
}
	
void
filter_print( struct slapi_filter *f )
{
	int	i;
	struct slapi_filter *p;

	if ( f == NULL ) {
		printf( "NULL" );
		return;
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		printf( "(%s=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_GE:
		printf( "(%s>=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_LE:
		printf( "(%s<=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_APPROX:
		printf( "(%s~=%s)", f->f_ava.ava_type,
		    f->f_ava.ava_value.bv_val );
		break;

	case LDAP_FILTER_SUBSTRINGS:
		printf( "(%s=", f->f_sub_type );
		if ( f->f_sub_initial != NULL ) {
			printf( "%s", f->f_sub_initial );
		}
		if ( f->f_sub_any != NULL ) {
			for ( i = 0; f->f_sub_any[i] != NULL; i++ ) {
				printf( "*%s", f->f_sub_any[i] );
			}
		}
		if ( f->f_sub_final != NULL ) {
			printf( "*%s", f->f_sub_final );
		}
		printf( ")" );
		break;

	case LDAP_FILTER_PRESENT:
		printf( "(%s=*)", f->f_type );
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		printf( "(%c", f->f_choice == LDAP_FILTER_AND ? '&' :
		    f->f_choice == LDAP_FILTER_OR ? '|' : '!' );
		for ( p = f->f_list; p != NULL; p = p->f_next ) {
			filter_print( p );
		}
		printf( ")" );
		break;

	default:
		printf( "unknown type 0x%lX", f->f_choice );
		break;
	}
	fflush( stdout );
}

/* filter_to_string
 * ----------------
 * translates the supplied filter to
 * the string representation and places
 * the result in buf
 *
 * NOTE: intended for debug purposes, buffer must be 
 * large enough to contain filter string
 */

char *
slapi_filter_to_string_internal( const struct slapi_filter *f, char *buf, size_t *bufsize )
{
	int	i;
	char *return_buf = buf;
	struct slapi_filter *p;
	size_t size;
	char *operator = ""; /* for comparison operators */

	if(buf == NULL)
		return 0;
	else
		*buf = 0; /* make sure buf is null terminated */

	if ( f == NULL ) {
		sprintf( buf, "NULL" );
		return 0;
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		operator = "=";
		break;

	case LDAP_FILTER_GE:
		operator = ">=";
		break;

	case LDAP_FILTER_LE:
		operator = "<=";
		break;

	case LDAP_FILTER_APPROX:
		operator = "~=";
		break;

	default: break;
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		/* +3 -> 1 for (, 1 for ), and one for the trailing null */
		size = strlen(f->f_ava.ava_type) + f->f_ava.ava_value.bv_len + strlen(operator) + 3;

		if(size < *bufsize)
		{
			/* bv_val may not be null terminated, so use the max field width
			   specifier .* with the bv_len as the length to avoid reading
			   past bv_len in bv_val */
			sprintf( buf, "(%s%s%.*s)", f->f_ava.ava_type, operator,
					 (int)f->f_ava.ava_value.bv_len,
					 f->f_ava.ava_value.bv_val );
			*bufsize -= size;
		}
		break;

	case LDAP_FILTER_SUBSTRINGS:
		size = strlen(f->f_sub_type) + 2;

		if(size < *bufsize)
		{
			sprintf( buf, "(%s=", f->f_sub_type );
			*bufsize -= size;

			if ( f->f_sub_initial != NULL ) {
				size = strlen(f->f_sub_initial);

				if(size < *bufsize)
				{
					buf += strlen(buf);
					sprintf( buf, "%s", f->f_sub_initial );
					*bufsize -= size;
				}
			}
			if ( f->f_sub_any != NULL ) {
				for ( i = 0; f->f_sub_any[i] != NULL; i++ ) {
					size = strlen(f->f_sub_any[i]) + 1;

					if(size < *bufsize)
					{
						buf += strlen(buf);
						sprintf( buf, "*%s", f->f_sub_any[i] );
						*bufsize -= size;
					}
				}
			}
			if ( f->f_sub_final != NULL ) {
					size = strlen(f->f_sub_final) + 1;

					if(size < *bufsize)
					{
						buf += strlen(buf);
						sprintf( buf, "*%s", f->f_sub_final );
						*bufsize -= size;
					}
			}
			buf += strlen(buf);

			if(1 < *bufsize)
			{
				sprintf( buf, ")" );
				*bufsize--;
			}
		}
		break;

	case LDAP_FILTER_PRESENT:
		size = strlen(f->f_type) + 4;
		
		if(size < *bufsize)
		{
			sprintf( buf, "(%s=*)", f->f_type );
			*bufsize -= size;
		}
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		if(2 < *bufsize)
		{
			sprintf( buf, "(%c", f->f_choice == LDAP_FILTER_AND ? '&' :
				f->f_choice == LDAP_FILTER_OR ? '|' : '!' );
			*bufsize -= 2;

			for ( p = f->f_list; p != NULL; p = p->f_next ) {
					buf += strlen(buf);
					slapi_filter_to_string_internal( p, buf, bufsize );
			}
			buf += strlen(buf);

			if(1 < *bufsize)
			{
				sprintf( buf, ")" );
				*bufsize--;
			}
		}
		break;

	default:
		size = 25;
		
		if(size < *bufsize)
		{
			sprintf( buf, "unsupported type 0x%lX", f->f_choice );
			*bufsize -= 25;
		}
		break;
	}

	return return_buf;
}

char *
slapi_filter_to_string( const struct slapi_filter *f, char *buf, size_t bufsize )
{
	size_t size = bufsize;

	return slapi_filter_to_string_internal( f, buf, &size );
}

/* rbyrne */

static int
filter_apply_list( struct slapi_filter *flist, FILTER_APPLY_FN fn, caddr_t arg,
					int *error_code )
{
    struct slapi_filter *f;
	int rc;

    for ( f = flist; f != NULL; f = f->f_next ) {
		rc = slapi_filter_apply( f, fn, arg, error_code );
		if ( rc == SLAPI_FILTER_SCAN_STOP || rc == SLAPI_FILTER_SCAN_ERROR) {
			return(rc);
		}
    }

	/* If we get here we've applied the whole list sucessfully so return 0 */

	return(SLAPI_FILTER_SCAN_NOMORE);
}

/*
  * 
  * The idea here is to apply, fn() to each "simple filter" in f as follows:
  * fn( Slapi_Filter *simple_filter, caddr_t arg).
  *
  * A 'simple filter' is anything other than AND, OR or NOT.
  * 
  * If fn() wants the seasrch to abort it returns FILTER_SCAN_STOP.
  * In this case, FILTER_SCAN_STOP is returned by slapi_filter_apply().
  * Otherwise fn() should return FILTER_SCAN_CONTINUE.
  * 
  * If the whole filter is traversed, FILTER_SCAN_NO_MORE is returned.
  * If an error occurred during the traverse, the scan is aborted and
  * FILTER_SCAN_ERROR is returned, and in this case error_code can be checked
  * for more details--right now the only error is
  * SLAPI_FILTER_UNKNOWN_FILTER_TYPE.
  * 
  * 
 */
int 
slapi_filter_apply( struct slapi_filter *f, FILTER_APPLY_FN fn, void *arg,
					int *error_code)
{
	int rc = SLAPI_FILTER_SCAN_ERROR;

    if ( f == NULL ) {
	return SLAPI_FILTER_SCAN_NOMORE;
    }

    switch ( f->f_choice ) {
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
    case LDAP_FILTER_EQUALITY:
	rc = (*fn)(f, arg );
	break;
    case LDAP_FILTER_SUBSTRINGS:	
	rc = (*fn)(f, arg);
	/* value will be normalized later */
	break;
    case LDAP_FILTER_PRESENT:
	rc = (*fn)(f, arg);
	break;
    case LDAP_FILTER_EXTENDED:	
	rc = (*fn)(f, arg);
	break;
    case LDAP_FILTER_AND:
	rc = filter_apply_list( f->f_and, fn, arg, error_code );
	break;
    case LDAP_FILTER_OR:
	rc = filter_apply_list( f->f_or, fn, arg, error_code );
	break;
    case LDAP_FILTER_NOT:
	rc = filter_apply_list( f->f_not, fn, arg, error_code );
	break;
    default:
	/* Unknown filter choice */
	*error_code = SLAPI_FILTER_UNKNOWN_FILTER_TYPE;
	rc = SLAPI_FILTER_SCAN_ERROR;
    }

	/*
	 * We propagate back FILTER_SCAN_ERROR and
	 * FILTER_SCAN_STOP, anything else is success.
	*/

	if (rc != SLAPI_FILTER_SCAN_ERROR && rc != SLAPI_FILTER_SCAN_STOP) {
		rc = SLAPI_FILTER_SCAN_NOMORE;
	}
	
	return(rc);
}


int
filter_flag_is_set(const Slapi_Filter *f, unsigned char flag) {
  return(f->f_flags & flag);
}


static int 
tombstone_check_filter(Slapi_Filter *f)
{
	if ( 0 == strcasecmp ( f->f_avvalue.bv_val, SLAPI_ATTR_VALUE_TOMBSTONE)) {
		return 1; /* Contains a nsTombstone filter */
	}
	return 0; /* Not nsTombstone filter */
}

/* filter_optimize
 * ---------------
 * takes a filter and optimizes it for fast evaluation
 * currently this merely ensures that any AND or OR
 * does not start with a NOT sub-filter if possible
 */
static void
filter_optimize(Slapi_Filter *f)
{
	if(!f)
		return;

	switch(f->f_choice)
	{
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
		{
			/* first optimize children */
			filter_optimize(f->f_list);

			/* optimize this */
			if(f->f_list->f_choice == LDAP_FILTER_NOT)
			{
				Slapi_Filter *f_prev = 0;
				Slapi_Filter *f_child = 0;

				/* grab a non not filter to place at start */
				for(f_child = f->f_list; f_child != 0; f_child = f_child->f_next)
				{
					if(f_child->f_choice != LDAP_FILTER_NOT)
					{
						/* we have a winner, do swap */
						f_prev->f_next = f_child->f_next;
						f_child->f_next = f->f_list;
						f->f_list = f_child;
						break;
					}

					f_prev = f_child;
				}
			}
		}
	default:
		filter_optimize(f->f_next);
		break;
	}
}


/* slapi_filter_changetype
 * ------------------------
 * changes the type used in equality/>/</approx filters
 * handy for features that do type mapping
 */
int slapi_filter_changetype(Slapi_Filter *f, const char *newtype)
{
	char **target = 0;

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		target = &f->f_ava.ava_type;
		break;

	case LDAP_FILTER_SUBSTRINGS:
		target = &f->f_sub_type;
		break;

	case LDAP_FILTER_PRESENT:
		target = &f->f_type;
		break;

	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
	default:
		goto bail;
		break;
	}

	slapi_ch_free_string(target);
	*target = slapi_ch_strdup(newtype);

bail:
	return (!target);
}

