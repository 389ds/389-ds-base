/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* dn.c - routines for dealing with distinguished names */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#endif
#include "slap.h"

#undef SDN_DEBUG

static void add_rdn_av( char *avstart, char *avend, int *rdn_av_countp,
	struct berval **rdn_avsp, struct berval *avstack );
static void reset_rdn_avs( struct berval **rdn_avsp, int *rdn_av_countp );
static void sort_rdn_avs( struct berval *avs, int count );
static int rdn_av_cmp( struct berval *av1, struct berval *av2 );
static void rdn_av_swap( struct berval *av1, struct berval *av2 );


int
hexchar2int( char c )
{
    if ( '0' <= c && c <= '9' ) {
	return( c - '0' );
    }
    if ( 'a' <= c && c <= 'f' ) {
	return( c - 'a' + 10 );
    }
    if ( 'A' <= c && c <= 'F' ) {
	return( c - 'A' + 10 );
    }
    return( -1 );
}

#define DNSEPARATOR(c)	(c == ',' || c == ';')
#define SEPARATOR(c)	(c == ',' || c == ';' || c == '+')
#define SPACE(c)	(c == ' ' || c == '\n')   /* XXX 518524 */
#define NEEDSESCAPE(c)	(c == '\\' || c == '"')
#define B4TYPE		0
#define INTYPE		1
#define B4EQUAL		2
#define B4VALUE		3
#define INVALUE		4
#define INQUOTEDVALUE	5
#define B4SEPARATOR	6

#define SLAPI_DNNORM_INITIAL_RDN_AVS	10
#define SLAPI_DNNORM_SMALL_RDN_AV	512

/*
 * substr_dn_normalize - map a DN to a canonical form.
 * The DN is read from *dn through *(end-1) and normalized in place.
 * The new end is returned; that is, the canonical form is in
 * *dn through *(the_return_value-1).
 */

/* The goals of this function are:
 * 1. be compatible with previous implementations.  Especially, enable
 *    a server running this code to find database index keys that were
 *    computed by Directory Server 3.0 with a prior version of this code.
 * 2. Normalize in place; that is, avoid allocating memory to contain
 *    the canonical form.
 * 3. eliminate insignificant differences; that is, any two DNs are
 *    not significantly different if and only if their canonical forms
 *    are identical (ignoring upper/lower case).
 * 4. handle a DN in the syntax defined by RFC 2253.
 * 5. handle a DN in the syntax defined by RFC 1779.
 *
 * Goals 3 through 5 are not entirely achieved by this implementation,
 * because it can't be done without violating goal 1.  Specifically,
 * DNs like cn="a,b" and cn=a\,b are not mapped to the same canonical form,
 * although they're not significantly different.  Likewise for any pair
 * of DNs that differ only in their choice of quoting convention.
 * A previous version of this code changed all DNs to the most compact
 * quoting convention, but that violated goal 1, since Directory Server
 * 3.0 did not.
 *
 * Also, this implementation handles the \xx convention of RFC 2253 and
 * consequently violates RFC 1779, according to which this type of quoting
 * would be interpreted as a sequence of 2 numerals (not a single byte).
 *
 * Finally, if the DN contains any RDNs that are multivalued, we sort
 * the values in the RDN(s) to help meet goal 3.  Ordering is based on a
 * case-insensitive comparison of the "attribute=value" pairs.
 *
 * This function does not support UTF-8 multi-byte encoding for attribute
 * values, in particular it does not support UTF-8 whitespace.  First the 
 * SPACE macro above is limited, but also its frequent use of '-1' indexing 
 * into a char[] may hit the middle of a multi-byte UTF-8 whitespace character
 * encoding (518524).
 */

char *
substr_dn_normalize( char *dn, char *end )
{
    /* \xx is changed to \c.
     * \c is changed to c, unless this would change its meaning.
     * All values that contain 2 or more separators are "enquoted";
     * all other values are not enquoted.
     */
	char		*value = NULL;
	char 		*value_separator = NULL;
	char		*d = NULL;
	char 		*s = NULL;
	char		*typestart = NULL;
	int		gotesc = 0;
	int		state = B4TYPE;
	int		rdn_av_count = 0;
	struct berval	*rdn_avs = NULL;
	struct berval	initial_rdn_av_stack[ SLAPI_DNNORM_INITIAL_RDN_AVS ];

	for ( d = s = dn; s != end; s++ ) {
		switch ( state ) {
		case B4TYPE:
			if ( ! SPACE( *s ) ) {
				state = INTYPE;
				typestart = d;
				*d++ = *s;
			}
			break;
		case INTYPE:
			if ( *s == '=' ) {
				state = B4VALUE;
				*d++ = *s;
			} else if ( SPACE( *s ) ) {
				state = B4EQUAL;
			} else {
				*d++ = *s;
			}
			break;
		case B4EQUAL:
			if ( *s == '=' ) {
				state = B4VALUE;
				*d++ = *s;
			} else if ( ! SPACE( *s ) ) {
				/* not a valid dn - but what can we do here? */
				*d++ = *s;
			}
			break;
		case B4VALUE:
			if ( *s == '"' || ! SPACE( *s ) ) {
				value_separator = NULL;
				value = d;
				state = ( *s == '"' ) ? INQUOTEDVALUE : INVALUE;
				*d++ = *s;
			}
			break;
		case INVALUE:
			if ( gotesc ) {
			    if ( SEPARATOR( *s ) ) {
				if ( value_separator ) value_separator = dn;
				else value_separator = d;
			    } else if ( ! NEEDSESCAPE( *s ) ) {
				--d; /* eliminate the \ */
			    }
			} else if ( SEPARATOR( *s ) ) {
			    while ( SPACE( *(d - 1) ) )
				d--;
			    if ( value_separator == dn ) { /* 2 or more separators */
				/* convert to quoted value: */
				char *L = NULL; /* char after last seperator */
				char *R; /* value character iterator */
				int escape_skips = 0; /* number of escapes we have seen after the first */

				for ( R = value; (R = strchr( R, '\\' )) && (R < d); L = ++R ) {
				    if ( SEPARATOR( R[1] )) {
						if ( L == NULL ) {
							/* executes once, at first escape, adds opening quote */
							const size_t len = R - value;
							
							/* make room for quote by covering escape */
							if ( len > 0 ) {
								memmove( value+1, value, len );
							}

							*value = '"'; /* opening quote */
							value = R + 1; /* move passed what has been parsed */
						} else {
							const size_t len = R - L;
							if ( len > 0 ) {
								/* remove the seperator */
								memmove( value, L, len );
								value += len; /* move passed what has been parsed */
							}
							--d;
							++escape_skips;
						}
				    }
				}
				memmove( value, L, d - L + escape_skips );
				*d++ = '"'; /* closing quote */
			    }
			    state = B4TYPE;

			    /*
			     * Track and sort attribute values within
			     * multivalued RDNs.
			     */
			    if ( *s == '+' || rdn_av_count > 0 ) {
				add_rdn_av( typestart, d, &rdn_av_count,
					&rdn_avs, initial_rdn_av_stack );
			    }
			    if ( *s != '+' ) {	/* at end of this RDN */
				if ( rdn_av_count > 1 ) {
				    sort_rdn_avs( rdn_avs, rdn_av_count );
				}
				if ( rdn_av_count > 0 ) {
				    reset_rdn_avs( &rdn_avs, &rdn_av_count );
				}
			    }

			    *d++ = (*s == '+') ? '+' : ',';
			    break;
			}
			*d++ = *s;
			break;
		case INQUOTEDVALUE:
			if ( gotesc ) {
			    if ( ! NEEDSESCAPE( *s ) ) {
				--d; /* eliminate the \ */
			    }
			} else if ( *s == '"' ) {
			    state = B4SEPARATOR;
			    if ( value_separator == dn /* 2 or more separators */
				|| SPACE( value[1] ) || SPACE( d[-1] ) ) {
				*d++ = *s;
			    } else {
				/* convert to non-quoted value: */
				if ( value_separator == NULL ) { /* no separators */
				    memmove ( value, value+1, (d-value)-1 );
				    --d;
				} else { /* 1 separator */
				    memmove ( value, value+1, (value_separator-value)-1 );
				    *(value_separator - 1) = '\\';
				}
			    }
			    break;
			}
			if ( SEPARATOR( *s )) {
			    if ( value_separator ) value_separator = dn;
			    else value_separator = d;
			}
			*d++ = *s;
			break;
		case B4SEPARATOR:
			if ( SEPARATOR( *s ) ) {
			    state = B4TYPE;

			    /*
			     * Track and sort attribute values within
			     * multivalued RDNs.
			     */
			    if ( *s == '+' || rdn_av_count > 0 ) {
				add_rdn_av( typestart, d, &rdn_av_count,
					&rdn_avs, initial_rdn_av_stack );
			    }
			    if ( *s != '+' ) {	/* at end of this RDN */
				if ( rdn_av_count > 1 ) {
				    sort_rdn_avs( rdn_avs, rdn_av_count );
				}
				if ( rdn_av_count > 0 ) {
				    reset_rdn_avs( &rdn_avs, &rdn_av_count );
				}
			    }

			    *d++ = (*s == '+') ? '+' : ',';
			}
			break;
		default:
			LDAPDebug( LDAP_DEBUG_ANY,
			    "slapi_dn_normalize - unknown state %d\n", state, 0, 0 );
			break;
		}
		if ( *s != '\\' ) {
			gotesc = 0;
		} else {
			gotesc = 1;
			if ( s+2 < end ) {
			    int n = hexchar2int( s[1] );
			    if ( n >= 0 ) {
				int n2 = hexchar2int( s[2] );
				if ( n2 >= 0 ) {
				    n = (n << 4) + n2;
				    if (n == 0) { /* don't change \00 */
					*d++ = *++s;
					*d++ = *++s;
					gotesc = 0;
				    } else { /* change \xx to a single char */
					++s;
					*(unsigned char*)(s+1) = n;
				    }
				}
			    }
			}
		}
	}

	/*
	 * Track and sort attribute values within multivalued RDNs.
	 */
	if ( rdn_av_count > 0 ) {
	    add_rdn_av( typestart, d, &rdn_av_count,
		    &rdn_avs, initial_rdn_av_stack );
	}
	if ( rdn_av_count > 1 ) {
	    sort_rdn_avs( rdn_avs, rdn_av_count );
	}
	if ( rdn_av_count > 0 ) {
	    reset_rdn_avs( &rdn_avs, &rdn_av_count );
	}

	/* Trim trailing spaces */
	while ( d != dn && *(d - 1) == ' ' ) d--;  /* XXX 518524 */

	return( d );
}



/*
 * Append previous AV to the attribute value array if multivalued RDN.
 * We use a stack based array at first and if we overflow that, we
 * allocate a larger one from the heap, copy the stack based data in,
 * and continue to grow the heap based one as needed.
 */
static void
add_rdn_av( char *avstart, char *avend, int *rdn_av_countp,
	struct berval **rdn_avsp, struct berval *avstack )
{
    if ( *rdn_av_countp == 0 ) {
	*rdn_avsp = avstack;
    } else if ( *rdn_av_countp == SLAPI_DNNORM_INITIAL_RDN_AVS ) {
	struct berval	*tmpavs;

	tmpavs = (struct berval *)slapi_ch_calloc(
		SLAPI_DNNORM_INITIAL_RDN_AVS * 2, sizeof( struct berval ));
	memcpy( tmpavs, *rdn_avsp,
		SLAPI_DNNORM_INITIAL_RDN_AVS * sizeof( struct berval ));
	*rdn_avsp = tmpavs;
    } else if (( *rdn_av_countp % SLAPI_DNNORM_INITIAL_RDN_AVS ) == 0 ) {
	*rdn_avsp = (struct berval *)slapi_ch_realloc( (char *)*rdn_avsp,
		(*rdn_av_countp + SLAPI_DNNORM_INITIAL_RDN_AVS)*sizeof(struct berval) );
    }

    /*
     * Note: The bv_val's are just pointers into the dn itself.  Also,
     * we DO NOT zero-terminate the bv_val's.  The sorting code in
     * sort_rdn_avs() takes all of this into account.
     */
    (*rdn_avsp)[ *rdn_av_countp ].bv_val = avstart;
    (*rdn_avsp)[ *rdn_av_countp ].bv_len = avend - avstart;
    ++(*rdn_av_countp);
}


/*
 * Reset RDN attribute value array, freeing memory if any was allocated.
 */
static void
reset_rdn_avs( struct berval **rdn_avsp, int *rdn_av_countp )
{
    if ( *rdn_av_countp > SLAPI_DNNORM_INITIAL_RDN_AVS ) {
	slapi_ch_free( (void **)rdn_avsp );
    }
    *rdn_avsp = NULL;
    *rdn_av_countp = 0;
}


/*
 * Perform an in-place, case-insensitive sort of RDN attribute=value pieces.
 * This function is always called with more than one element in "avs".
 *
 * Note that this is used by the DN normalization code, so if any changes
 * are made to the comparison function used for sorting customers will need
 * to rebuild their database/index files.
 *
 * Also note that the bv_val's in the "avas" array are not zero-terminated.
 */
static void
sort_rdn_avs( struct berval *avs, int count )
{
    int		i, j, swaps;

    /*
     * Since we expect there to be a small number of AVs, we use a
     * simple bubble sort.  rdn_av_swap() only works correctly on
     * adjacent values anyway.
     */
    for ( i = 0; i < count - 1; ++i ) {
	swaps = 0;
	for ( j = 0; j < count - 1; ++j ) {
	    if ( rdn_av_cmp( &avs[j], &avs[j+1] ) > 0 ) {
		rdn_av_swap( &avs[j], &avs[j+1] );
		++swaps;
	    }
	}
	if ( swaps == 0 ) {
	    break;	/* stop early if no swaps made during the last pass */
	}
    } 
}


/*
 * strcasecmp()-like function for RDN attribute values.
 */
static int
rdn_av_cmp( struct berval *av1, struct berval *av2 )
{
    int		rc;

    rc = strncasecmp( av1->bv_val, av2->bv_val,
	    ( av1->bv_len < av2->bv_len ) ? av1->bv_len : av2->bv_len );

    if ( rc == 0 ) {
	return( av1->bv_len - av2->bv_len );	/* longer is greater */
    } else {
	return( rc );
    }
}


/*
 * Swap two adjacent attribute=value pieces within an (R)DN.
 * Avoid allocating any heap memory for reasonably small AVs.
 */
static void
rdn_av_swap( struct berval *av1, struct berval *av2 )
{
    char	*buf1, *buf2;
    char	stackbuf1[ SLAPI_DNNORM_SMALL_RDN_AV ];
    char	stackbuf2[ SLAPI_DNNORM_SMALL_RDN_AV ];
    int		len1, len2;

    /*
     * Copy the two avs into temporary buffers.  We use stack-based buffers
     * if the avs are small and allocate buffers from the heap to hold
     * large values.
     */
    if (( len1 = av1->bv_len ) <= SLAPI_DNNORM_SMALL_RDN_AV ) {
	buf1 = stackbuf1;
    } else {
	buf1 = slapi_ch_malloc( len1 );
    }
    memcpy( buf1, av1->bv_val, len1 );

    if (( len2 = av2->bv_len ) <= SLAPI_DNNORM_SMALL_RDN_AV ) {
	buf2 = stackbuf2;
    } else {
	buf2 = slapi_ch_malloc( len2 );
    }
    memcpy( buf2, av2->bv_val, len2 );

    /*
     * Copy av2 over av1 and reset length of av1.
     */
    memcpy( av1->bv_val, buf2, av2->bv_len );
    av1->bv_len = len2;

    /*
     * Add separator character (+) and copy av1 into place.
     * Also reset av2 pointer and length.
     */
    av2->bv_val = av1->bv_val + len2;
    *(av2->bv_val)++ = '+';
    memcpy( av2->bv_val, buf1, len1 );
    av2->bv_len = len1;

    /*
     * Clean up.
     */
    if ( len1 > SLAPI_DNNORM_SMALL_RDN_AV ) {
	slapi_ch_free( (void **)&buf1 );
    }
    if ( len2 > SLAPI_DNNORM_SMALL_RDN_AV ) {
	slapi_ch_free( (void **)&buf2 );
    }
}


/*
 * slapi_dn_normalize - put dn into a canonical format.  the dn is
 * normalized in place, as well as returned.
 */

char *
slapi_dn_normalize( char *dn )
{
	/* LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    *(substr_dn_normalize( dn, dn + strlen( dn ))) = '\0';
	/* LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
    return dn;
}

/* Note that this routine  normalizes to the end and doesn't null terminate */
char *
slapi_dn_normalize_to_end( char *dn , char *end)
{
    return ( substr_dn_normalize( dn, end ? end : dn + strlen( dn )) );
}

/*
 * dn could contain UTF-8 multi-byte characters,
 * which also need to be converted to the lower case.
 */
char *
slapi_dn_ignore_case( char *dn )
{
    unsigned char *s, *d;
    int ssz, dsz;
    /* normalize case (including UTF-8 multi-byte chars) */
    for ( s = d = (unsigned char *)dn; *s; s += ssz, d += dsz ) {
	slapi_utf8ToLower( s, d, &ssz, &dsz );
    }
    *d = '\0';	/* utf8ToLower result may be shorter than the original */
    return( dn );
}

/*
 * slapi_dn_normalize_case - put dn into a canonical form suitable for storing
 * in a hash database.  this involves normalizing the case as well as
 * the format.  the dn is normalized in place as well as returned.
 */

char *
slapi_dn_normalize_case( char *dn )
{
	/* normalize format */
	slapi_dn_normalize( dn );

	/* normalize case */
	return( slapi_dn_ignore_case( dn ));
}

/*
 * slapi_dn_beparent - return a copy of the dn of dn's parent,
 *                     NULL if the DN is a suffix of the backend.
 */
char *
slapi_dn_beparent(
    Slapi_PBlock	*pb,
    const char	*dn
)
{
    char *r= NULL;
	if ( dn != NULL && *dn != '\0')
	{
    	if(!slapi_dn_isbesuffix( pb, dn ))
		{
        	r= slapi_dn_parent( dn );
		}
	}
	return r;
}

char*
slapi_dn_parent( const char *dn )
{
	const char *s;
	int	inquote;

	if ( dn == NULL || *dn == '\0' ) {
		return( NULL );
	}

	/*
	 * An X.500-style distinguished name looks like this:
	 * foo=bar,sha=baz,...
	 */

	inquote = 0;
	for ( s = dn; *s; s++ ) {
		if ( *s == '\\' ) {
			if ( *(s + 1) )
				s++;
			continue;
		}
		if ( inquote ) {
			if ( *s == '"' )
				inquote = 0;
		} else {
			if ( *s == '"' )
				inquote = 1;
			else if ( DNSEPARATOR( *s ) )
				return( slapi_ch_strdup( s + 1 ) );
		}
	}

	return( NULL );
}

/*
 * slapi_dn_issuffix - tells whether suffix is a suffix of dn.  both dn
 * and suffix must be normalized.
 */
int
slapi_dn_issuffix(const char *dn, const char *suffix)
{
	int	dnlen, suffixlen;

	if ( dn==NULL || suffix==NULL)
	{
		return( 0 );
	}

	suffixlen = strlen( suffix );
	dnlen = strlen( dn );

	if ( suffixlen > dnlen )
	{
		return( 0 );
	}
	
	if ( suffixlen == 0 )
	{
		return ( 1 );
	}

	return( (slapi_utf8casecmp( (unsigned char *)(dn + dnlen - suffixlen),
				   (unsigned char *)suffix ) == 0)
			&& ( (dnlen == suffixlen) || DNSEPARATOR(dn[dnlen-suffixlen-1])) );
}

int
slapi_dn_isbesuffix( Slapi_PBlock *pb, const char *dn )
{
    int r;
    Slapi_DN sdn;
	slapi_sdn_init_dn_byref(&sdn,dn);
	r= slapi_be_issuffix( pb->pb_backend, &sdn );
	slapi_sdn_done(&sdn);
	return r;
}

/*
 * slapi_dn_isparent - returns non-zero if parentdn is the parent of childdn,
 * 0 otherwise
 */
int
slapi_dn_isparent( const char *parentdn, const char *childdn )
{
	char *realparentdn, *copyparentdn;
	int	rc;

	/* child is root - has no parent */
	if ( childdn == NULL || *childdn == '\0' ) {
		return( 0 );
	}

	/* construct the actual parent dn and normalize it */
	if ( (realparentdn = slapi_dn_parent( childdn )) == NULL ) {
		return( parentdn == NULL || *parentdn == '\0' );
	}
	slapi_dn_normalize( realparentdn );

	/* normalize the purported parent dn */
	copyparentdn = slapi_ch_strdup( (char *)parentdn );
	slapi_dn_normalize( copyparentdn );

	/* compare them */
	rc = ! strcasecmp( realparentdn, copyparentdn );
	slapi_ch_free( (void**)&copyparentdn );
	slapi_ch_free( (void**)&realparentdn );

	return( rc );
}

/* 
 * Function: slapi_dn_isroot
 * 
 * Returns: 1 if "dn" is the root dn
 *          0 otherwise.
 * dn must be normalized
 *
 */
int
slapi_dn_isroot( const char *dn )
{
	int	rc;
	char *rootdn;

	if ( NULL == dn ) { 
	    return( 0 );
	}
 	if ( NULL == (rootdn = config_get_rootdn())) {
	    return( 0 );
	}

	/* note:  global root dn is normalized when read from config. file */
	rc = (strcasecmp( rootdn, dn ) == 0);
	slapi_ch_free ( (void **) &rootdn );
	return( rc );
}

int
slapi_is_rootdse( const char *dn )
{
    if ( NULL != dn )
    {
	    if ( *dn == '\0' )	
	    {
            return 1;
        }
    }
    return 0;
}



/*
** This function takes a quoted attribute value of the form "abc",
** and strips off the enclosing quotes.  It also deals with quoted
** characters by removing the preceeding '\' character.
**
*/
static void
strcpy_unescape_dnvalue( char *d, const char *s )
{
    const char *end = s + strlen(s);
	for ( ; *s; s++ )
	{
		switch ( *s )
		{
		case '"':
			break;
		case '\\':
            {
            /*
             * The '\' could be escaping a single character, ie \"
             * or could be escaping a hex byte, ie \01
             */
            int singlecharacter= 1;
            if ( s+2 < end )
            {
                int n = hexchar2int( s[1] );
                if ( n >= 0 )
                {
                    int n2 = hexchar2int( s[2] );
                    if ( n2 >= 0 )
                    {
                        singlecharacter= 0;
                        n = (n << 4) + n2;
                        if (n == 0)
                        {
                            /* don't change \00 */
                            *d++ = *++s;
                            *d++ = *++s;
                        }
                        else
                        {
                            /* change \xx to a single char */
                            ++s;
                            *(unsigned char*)(s+1) = n;
                        }
                    }
                }
            }
            if(singlecharacter)
            {
                s++;
                *d++ = *s;
            }
            break;
            }
		default:
			*d++ = *s;
			break;
		}
	}
	*d = '\0';
}



int
slapi_rdn2typeval(
    char        	*rdn,
    char		**type,
    struct berval	*bv
)
{
    char    *s;

    if ( (s = strchr( rdn, '=' )) == NULL ) {
        return( -1 );
    }
    *s++ = '\0';

    *type = rdn;

    /* MAB 9 Oct 00 : explicit bug fix of 515715
                      implicit bug fix of 394800 (can't reproduce anymore)
       When adding the rdn attribute in the entry, we need to remove
       all special escaped characters included in the value itself,
       i.e., strings like "\;" must be converted to ";" and so on... */
    strcpy_unescape_dnvalue(s,s);

    bv->bv_val = s;
    bv->bv_len = strlen( s );

    return( 0 );
}

/*
 * Add an RDN to a DN, getting back the new DN.
 */
char *
slapi_dn_plus_rdn(const char *dn, const char *rdn)
{
	/* rdn + separator + dn + null */
	char *newdn = (char *) slapi_ch_malloc( strlen( dn ) + strlen( rdn ) + 2 );
	strcpy( newdn, rdn );
	strcat( newdn, "," );
	strcat( newdn, dn );
	return newdn;
}

/* ======  Slapi_DN functions ====== */

#ifdef SDN_DEBUG
#define SDN_DUMP(sdn,name) sdn_dump(sdn,name)
static void sdn_dump( const Slapi_DN *sdn, const char *text);
#else
#define SDN_DUMP(sdn,name) ((void)0)
#endif

#ifndef SLAPI_DN_COUNTERS
#undef DEBUG                    /* disable counters */
#endif
#include <prcountr.h>

static int counters_created= 0;
PR_DEFINE_COUNTER(slapi_sdn_counter_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_exist);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_dn_exist);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_created);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_deleted);
PR_DEFINE_COUNTER(slapi_sdn_counter_ndn_exist);

static void
sdn_create_counters()
{
	PR_CREATE_COUNTER(slapi_sdn_counter_created,"Slapi_DN","created","");
	PR_CREATE_COUNTER(slapi_sdn_counter_deleted,"Slapi_DN","deleted","");
	PR_CREATE_COUNTER(slapi_sdn_counter_exist,"Slapi_DN","exist","");
	PR_CREATE_COUNTER(slapi_sdn_counter_dn_created,"Slapi_DN","internal_dn_created","");
	PR_CREATE_COUNTER(slapi_sdn_counter_dn_deleted,"Slapi_DN","internal_dn_deleted","");
	PR_CREATE_COUNTER(slapi_sdn_counter_dn_exist,"Slapi_DN","internal_dn_exist","");
	PR_CREATE_COUNTER(slapi_sdn_counter_ndn_created,"Slapi_DN","internal_ndn_created","");
	PR_CREATE_COUNTER(slapi_sdn_counter_ndn_deleted,"Slapi_DN","internal_ndn_deleted","");
	PR_CREATE_COUNTER(slapi_sdn_counter_ndn_exist,"Slapi_DN","internal_ndn_exist","");
	counters_created= 1;
}

#define FLAG_ALLOCATED 0
#define FLAG_DN 1
#define FLAG_NDN 2

Slapi_DN *
slapi_sdn_new()
{
    Slapi_DN *sdn= (Slapi_DN *)slapi_ch_malloc(sizeof(Slapi_DN));    
    slapi_sdn_init(sdn);
    sdn->flag= slapi_setbit_uchar(sdn->flag,FLAG_ALLOCATED);
    SDN_DUMP( sdn, "slapi_sdn_new");
	PR_INCREMENT_COUNTER(slapi_sdn_counter_created);
	PR_INCREMENT_COUNTER(slapi_sdn_counter_exist);
    return sdn;
}

Slapi_DN *
slapi_sdn_init(Slapi_DN *sdn)
{
    sdn->flag= 0;
    sdn->dn= NULL;
    sdn->ndn= NULL;
	sdn->ndn_len=0;
	if(!counters_created)
	{
		sdn_create_counters();
	}
	return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_byref(Slapi_DN *sdn,const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_byref(sdn,dn);
	return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_byval(Slapi_DN *sdn,const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_byval(sdn,dn);
	return sdn;
}

Slapi_DN *
slapi_sdn_init_dn_passin(Slapi_DN *sdn,const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_dn_passin(sdn,dn);
	return sdn;
}

/* use when dn is normalized previously */
Slapi_DN *
slapi_sdn_init_dn_ndn_byref(Slapi_DN *sdn,const char *dn) {
	  slapi_sdn_init(sdn);
	  slapi_sdn_set_dn_byref(sdn,dn);
	  /* slapi_sdn_set_ndn_byref nulls out dn set in above statement */
	  sdn->flag= slapi_unsetbit_uchar(sdn->flag,FLAG_NDN);
      sdn->ndn= dn;
	  if(dn == NULL) {
		  sdn->ndn_len=0;
	  } else {
	   sdn->ndn_len=strlen(dn);
	  }
	  return sdn;
}

Slapi_DN *
slapi_sdn_init_ndn_byref(Slapi_DN *sdn,const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_ndn_byref(sdn,dn);
	return sdn;
}

Slapi_DN *
slapi_sdn_init_ndn_byval(Slapi_DN *sdn,const char *dn)
{
    slapi_sdn_init(sdn);
    slapi_sdn_set_ndn_byval(sdn,dn);
	return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_byval(const char *dn)
{
    Slapi_DN *sdn= slapi_sdn_new();
    slapi_sdn_set_dn_byval(sdn,dn);
    SDN_DUMP( sdn, "slapi_sdn_new_dn_byval");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_ndn_byval(const char *ndn)
{
    Slapi_DN *sdn= slapi_sdn_new();
    slapi_sdn_set_ndn_byval(sdn,ndn);
    SDN_DUMP( sdn, "slapi_sdn_new_ndn_byval");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_byref(const char *dn)
{
    Slapi_DN *sdn= slapi_sdn_new();
    slapi_sdn_set_dn_byref(sdn,dn);
    SDN_DUMP( sdn, "slapi_sdn_new_dn_byref");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_dn_passin(const char *dn)
{
    Slapi_DN *sdn= slapi_sdn_new();
    slapi_sdn_set_dn_passin(sdn,dn);
    SDN_DUMP( sdn, "slapi_sdn_new_dn_passin");
    return sdn;
}

Slapi_DN *
slapi_sdn_new_ndn_byref(const char *ndn)
{
    Slapi_DN *sdn= slapi_sdn_new();
    slapi_sdn_set_ndn_byref(sdn,ndn);
    SDN_DUMP( sdn, "slapi_sdn_new_ndn_byref");
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_byval(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag= slapi_setbit_uchar(sdn->flag,FLAG_DN);
	if(dn!=NULL)
	{
		sdn->dn= slapi_ch_strdup(dn);
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
	}
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_byref(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag= slapi_unsetbit_uchar(sdn->flag,FLAG_DN);
    sdn->dn= dn;
    return sdn;
}

Slapi_DN *
slapi_sdn_set_dn_passin(Slapi_DN *sdn, const char *dn)
{
    slapi_sdn_done(sdn);
    sdn->flag= slapi_setbit_uchar(sdn->flag,FLAG_DN);
    sdn->dn= dn;
	if(dn!=NULL)
	{
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
	}
    return sdn;
}

Slapi_DN *
slapi_sdn_set_ndn_byval(Slapi_DN *sdn, const char *ndn)
{
    slapi_sdn_done(sdn);
    sdn->flag= slapi_setbit_uchar(sdn->flag,FLAG_NDN);
	if(ndn!=NULL)
	{
		sdn->ndn= slapi_ch_strdup(ndn);
		sdn->ndn_len=strlen(ndn);
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_created);
	    PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
	} 
    return sdn;
}

Slapi_DN *
slapi_sdn_set_ndn_byref(Slapi_DN *sdn, const char *ndn)
{
    slapi_sdn_done(sdn);
    sdn->flag= slapi_unsetbit_uchar(sdn->flag,FLAG_NDN);
    sdn->ndn= ndn;
	if(ndn == NULL) {
		sdn->ndn_len=0;
	} else {
		sdn->ndn_len=strlen(ndn);
	}
    return sdn;
}

/*
 * Set the RDN of the DN.
 */
Slapi_DN *
slapi_sdn_set_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn)
{
	const char *rawrdn= slapi_rdn_get_rdn(rdn);
    if(slapi_sdn_isempty(sdn))
	{
		slapi_sdn_set_dn_byval(sdn,rawrdn);
	}
	else
	{
		/* NewDN= NewRDN + OldParent */
		char *parentdn= slapi_dn_parent(sdn->dn);
		char *newdn= slapi_ch_malloc(strlen(rawrdn)+1+strlen(parentdn)+1);
		strcpy( newdn, rawrdn );
		strcat( newdn, "," );
		strcat( newdn, parentdn );
		slapi_ch_free((void**)&parentdn);
		slapi_sdn_set_dn_passin(sdn,newdn);
	}
	return sdn;
}

/*
 * Add the RDN to the DN.
 */
Slapi_DN *
slapi_sdn_add_rdn(Slapi_DN *sdn, const Slapi_RDN *rdn)
{
	const char *rawrdn= slapi_rdn_get_rdn(rdn);
    if(slapi_sdn_isempty(sdn))
	{
		slapi_sdn_set_dn_byval(sdn,rawrdn);
	}
	else
	{
		/* NewDN= NewRDN + DN */
		const char *dn= slapi_sdn_get_dn(sdn);
		char *newdn= slapi_ch_malloc(strlen(rawrdn)+1+strlen(dn)+1);
		strcpy( newdn, rawrdn );
		strcat( newdn, "," );
		strcat( newdn, dn );
		slapi_sdn_set_dn_passin(sdn,newdn);
	}
	return sdn;
}

/*
 * Set the parent of the DN.
 */
Slapi_DN *
slapi_sdn_set_parent(Slapi_DN *sdn, const Slapi_DN *parentdn)
{
    if(slapi_sdn_isempty(sdn))
	{
		slapi_sdn_copy(parentdn, sdn);
	}
	else
	{
		/* NewDN= OldRDN + NewParent */
		Slapi_RDN rdn;
		const char *rawrdn;
		slapi_rdn_init_dn(&rdn,sdn->dn);
		rawrdn= slapi_rdn_get_rdn(&rdn);
	    if(slapi_sdn_isempty(parentdn))
		{
			slapi_sdn_set_dn_byval(sdn,rawrdn);
		}
		else
		{
			char *newdn;
			newdn= slapi_ch_malloc(strlen(rawrdn)+1+strlen(parentdn->dn)+1);
			strcpy( newdn, rawrdn );
			strcat( newdn, "," );
			strcat( newdn, parentdn->dn );
			slapi_sdn_set_dn_passin(sdn,newdn);
		}
		slapi_rdn_done(&rdn);
	}
	return sdn;
}

void
slapi_sdn_done(Slapi_DN *sdn)
{
    /* sdn_dump( sdn, "slapi_sdn_done"); */
    if(sdn->dn!=NULL)
    {
        if(slapi_isbitset_uchar(sdn->flag,FLAG_DN))
        {
            slapi_ch_free((void**)&(sdn->dn));
            sdn->flag= slapi_unsetbit_uchar(sdn->flag,FLAG_DN);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_dn_exist);
        }
        else
        {
            sdn->dn= NULL;
        }
    }
    if(sdn->ndn!=NULL)
    {
        if(slapi_isbitset_uchar(sdn->flag,FLAG_NDN))
        {
            slapi_ch_free((void**)&(sdn->ndn));
            sdn->flag= slapi_unsetbit_uchar(sdn->flag,FLAG_NDN);
			sdn->ndn_len=0;
            PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_deleted);
            PR_DECREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
        }
        else
        {
            sdn->ndn= NULL;
			sdn->ndn_len=0;
        }
    }
}

void
slapi_sdn_free(Slapi_DN **sdn)
{
	if(sdn!=NULL && *sdn!=NULL)
	{
	    SDN_DUMP( *sdn, "slapi_sdn_free");
	    slapi_sdn_done(*sdn);
	    if(slapi_isbitset_uchar((*sdn)->flag,FLAG_ALLOCATED))
	    {
	        slapi_ch_free((void**)sdn);
	        PR_INCREMENT_COUNTER(slapi_sdn_counter_deleted);
	        PR_DECREMENT_COUNTER(slapi_sdn_counter_exist);
	    }
	}
}

const char *
slapi_sdn_get_dn(const Slapi_DN *sdn)
{
    return (sdn->dn!=NULL ? sdn->dn : sdn->ndn);
}

const char *
slapi_sdn_get_ndn(const Slapi_DN *sdn)
{
    if(sdn->ndn==NULL)
    {
        if(sdn->dn!=NULL)
        {
            char *p= slapi_ch_strdup(sdn->dn);
			Slapi_DN *ncsdn= (Slapi_DN*)sdn; /* non-const Slapi_DN */
            slapi_dn_normalize_case(p);
            ncsdn->ndn= p;
			ncsdn->ndn_len=strlen(p);
            ncsdn->flag= slapi_setbit_uchar(sdn->flag,FLAG_NDN);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_created);
            PR_INCREMENT_COUNTER(slapi_sdn_counter_ndn_exist);
        }
    }
    return sdn->ndn;
}

void
slapi_sdn_get_parent(const Slapi_DN *sdn,Slapi_DN *sdn_parent)
{
    const char *parentdn= slapi_dn_parent(slapi_sdn_get_dn(sdn));
    slapi_sdn_set_dn_passin(sdn_parent,parentdn);
    sdn_parent->flag= slapi_setbit_uchar(sdn_parent->flag,FLAG_DN);
    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_created);
    PR_INCREMENT_COUNTER(slapi_sdn_counter_dn_exist);
}

void
slapi_sdn_get_backend_parent(const Slapi_DN *sdn,Slapi_DN *sdn_parent,const Slapi_Backend *backend)
{
    if(slapi_sdn_isempty(sdn) || slapi_be_issuffix( backend, sdn ))
	{
	    slapi_sdn_done(sdn_parent);
	}
	else
	{
	    slapi_sdn_get_parent(sdn,sdn_parent);
	}
}

void
slapi_sdn_get_rdn(const Slapi_DN *sdn,Slapi_RDN *rdn)
{
	slapi_rdn_set_dn(rdn,sdn->dn);
}

Slapi_DN *
slapi_sdn_dup(const Slapi_DN *sdn)
{
	Slapi_DN *tmp;
    SDN_DUMP( sdn, "slapi_sdn_dup");
    tmp=slapi_sdn_new_dn_byval(slapi_sdn_get_dn(sdn));
	/* can't use slapi_set_ndn_byval -- it nulls the dn */
	tmp->flag= slapi_setbit_uchar(tmp->flag,FLAG_NDN);
	if(sdn->ndn!=NULL)
	{
		tmp->ndn= slapi_ch_strdup(sdn->ndn);
		tmp->ndn_len=sdn->ndn_len;
	} else tmp->ndn=NULL;
	return tmp;
}

void
slapi_sdn_copy(const Slapi_DN *from, Slapi_DN *to)
{
    SDN_DUMP( from, "slapi_sdn_copy from");
    SDN_DUMP( to, "slapi_sdn_copy to");
	slapi_sdn_done(to);
	slapi_sdn_set_dn_byval(to,slapi_sdn_get_dn(from));
}

int
slapi_sdn_compare( const Slapi_DN *sdn1, const Slapi_DN *sdn2 )
{
    int rc;
    const char *ndn1= slapi_sdn_get_ndn(sdn1);
    const char *ndn2= slapi_sdn_get_ndn(sdn2);
	if(ndn1==ndn2)
	{
	    rc= 0;
	}
	else
	{
	    if(ndn1==NULL)
		{
		    rc= -1;
		}
		else
		{
    	    if(ndn2==NULL)
    		{
    		    rc= 1;
    		}
    		else
    		{
                rc= strcmp(ndn1,ndn2);
			}
		}
	}
	return rc;
}

int
slapi_sdn_isempty( const Slapi_DN *sdn)
{
    const char *dn= slapi_sdn_get_dn(sdn);
	return (dn==NULL || dn[0]=='\0');
}

int
slapi_sdn_issuffix(const Slapi_DN *sdn, const Slapi_DN *suffixsdn)
{
    int rc;
    const char *dn= slapi_sdn_get_ndn(sdn);
	const char *suffixdn= slapi_sdn_get_ndn(suffixsdn);
	if(dn!=NULL && suffixdn!=NULL)
	{
    	int dnlen = slapi_sdn_get_ndn_len(sdn);
        int suffixlen= slapi_sdn_get_ndn_len(suffixsdn);
		if (dnlen<suffixlen)
		{
		    rc= 0;
		}
		else
		{
			if ( suffixlen == 0 )
			{
				return ( 1 );
			}

        	rc= ( (strcasecmp(suffixdn, dn+dnlen-suffixlen)==0)
					&& ( (dnlen == suffixlen)
						 || DNSEPARATOR(dn[dnlen-suffixlen-1])) );
		}
	}
	else
	{
		rc= 0;
	}
	return rc;
}

/* normalizes sdn if it hasn't already been done */
int
slapi_sdn_get_ndn_len(const Slapi_DN *sdn)
{
    int r= 0;
	const char *ndn=slapi_sdn_get_ndn(sdn);
	if(sdn->ndn!=NULL)
	{
		r= sdn->ndn_len;	
	}
	return r;
}

int
slapi_sdn_isparent( const Slapi_DN *parent, const Slapi_DN *child )
{
	int	rc= 0;

	/* child is root - has no parent */
	if ( !slapi_sdn_isempty(child) )
	{
	    Slapi_DN childparent;
		slapi_sdn_init(&childparent);
        slapi_sdn_get_parent(child,&childparent);
		rc= (slapi_sdn_compare(parent,&childparent)==0);
		slapi_sdn_done(&childparent);
	}
	return( rc );
}

int
slapi_sdn_isgrandparent( const Slapi_DN *parent, const Slapi_DN *child )
{
	int	rc= 0;

	/* child is root - has no parent */
	if ( !slapi_sdn_isempty(child) )
	{
	    Slapi_DN childparent;
		slapi_sdn_init(&childparent);
        slapi_sdn_get_parent(child,&childparent);
		if ( !slapi_sdn_isempty(&childparent) )
		{
			Slapi_DN childchildparent;
			slapi_sdn_init(&childchildparent);
			slapi_sdn_get_parent(&childparent,&childchildparent);
			rc= (slapi_sdn_compare(parent,&childchildparent)==0);
			slapi_sdn_done(&childchildparent);
		}
		slapi_sdn_done(&childparent);
	}
	return( rc );
}

/* 
 * Return non-zero if "dn" matches the scoping criteria
 * given by "base" and "scope".
 */
int
slapi_sdn_scope_test( const Slapi_DN *dn, const Slapi_DN *base, int scope )
{
    int rc = 0;

    switch ( scope ) {
    case LDAP_SCOPE_BASE:
    	rc = ( slapi_sdn_compare( dn, base ) == 0 );
    	break;
    case LDAP_SCOPE_ONELEVEL:
    	rc = ( slapi_sdn_isparent( base, dn ) != 0 );
    	break;
    case LDAP_SCOPE_SUBTREE:
    	rc = ( slapi_sdn_issuffix( dn, base ) != 0 );
    	break;
    }
    return rc;
}

/*
 * build the new dn of an entry for moddn operations
 */
char *
slapi_moddn_get_newdn(Slapi_DN *dn_olddn, char *newrdn, char *newsuperiordn)
{
    char *newdn;
	
    if( newsuperiordn!=NULL)
	{
		/* construct the new dn */
		newdn= slapi_dn_plus_rdn(newsuperiordn, newrdn); /* JCM - Use Slapi_RDN */
	}
	else
	{
    	/* construct the new dn */
		char *pdn;
		const char *dn= slapi_sdn_get_dn(dn_olddn);
    	pdn = slapi_dn_parent( dn );
    	if ( pdn != NULL )
    	{
            newdn= slapi_dn_plus_rdn(pdn, newrdn); /* JCM - Use Slapi_RDN */
    	}
    	else
    	{
    		newdn= slapi_ch_strdup(newrdn);
    	}
    	slapi_ch_free( (void**)&pdn );
	}
	return newdn;
}

/* JCM slapi_sdn_get_first ? */
/* JCM slapi_sdn_get_next ? */

#ifdef SDN_DEBUG
static void
sdn_dump( const Slapi_DN *sdn, const char *text)
{
    LDAPDebug( LDAP_DEBUG_ANY, "SDN %s ptr=%lx dn=%s\n", text, sdn, (sdn->dn==NULL?"NULL":sdn->dn));
}
#endif
