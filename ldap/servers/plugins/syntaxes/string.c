/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* string.c - common string syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"
#if defined(IRIX)
#include <unistd.h>
#endif
#if defined( MACOS ) || defined( DOS ) || defined( _WIN32 ) || defined( NEED_BSDREGEX )
#include "regex.h"
#endif

static int string_filter_approx( struct berval *bvfilter,
	Slapi_Value **bvals, Slapi_Value **retVal );
static void substring_comp_keys( Slapi_Value ***ivals, int *nsubs, char *str,
	int prepost, int syntax );

int
string_filter_ava( struct berval *bvfilter, Slapi_Value **bvals, int syntax,
    int ftype, Slapi_Value **retVal )
{
	int	i, rc;
	struct berval bvfilter_norm;

	if(retVal) {
		*retVal = NULL;
	}
	if ( ftype == LDAP_FILTER_APPROX ) {
		return( string_filter_approx( bvfilter, bvals, retVal ) );
	}

	bvfilter_norm.bv_val = slapi_ch_malloc( bvfilter->bv_len + 1 );
	SAFEMEMCPY( bvfilter_norm.bv_val, bvfilter->bv_val, bvfilter->bv_len );
	bvfilter_norm.bv_val[bvfilter->bv_len] = '\0';
	value_normalize( bvfilter_norm.bv_val, syntax, 1 /* trim leading blanks */ );

	for ( i = 0; bvals[i] != NULL; i++ ) {
		rc = value_cmp( (struct berval*)slapi_value_get_berval(bvals[i]), &bvfilter_norm, syntax, 1/* Normalise the first value only */ );
                switch ( ftype ) {
                case LDAP_FILTER_GE:
                        if ( rc >= 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
								slapi_ch_free ((void**)&bvfilter_norm.bv_val);
                                return( 0 );
                        }
                        break;
                case LDAP_FILTER_LE:
                        if ( rc <= 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
								slapi_ch_free ((void**)&bvfilter_norm.bv_val);
                                return( 0 );
                        }
                        break;
                case LDAP_FILTER_EQUALITY:
                        if ( rc == 0 ) {
							    if(retVal) {
									*retVal = bvals[i];
								}
								slapi_ch_free ((void**)&bvfilter_norm.bv_val);
                                return( 0 );
                        }
                        break;
                }
        }

	slapi_ch_free ((void**)&bvfilter_norm.bv_val);
	return( -1 );
}

static int
string_filter_approx( struct berval *bvfilter, Slapi_Value **bvals,
							 Slapi_Value **retVal)
{
	int	i, rc;
	int	ava_wordcount;
	char	*w1, *w2, *c1, *c2;

	/*
	 * try to match words in each filter value in order
	 * in the attribute value.
	 * XXX should do this once for the filter and save it XXX
	 */
	rc = -1;
	if(retVal) {
		*retVal = NULL;
	}
	for ( i = 0; bvals[i] != NULL; i++ ) {
		w2 = (char*)slapi_value_get_string(bvals[i]); /* JCM cast */
		ava_wordcount = 0;
		/* for each word in the filter value */
		for ( w1 = first_word( bvfilter->bv_val ); w1 != NULL;
		    w1 = next_word( w1 ) ) {
			++ava_wordcount;
			if ( (c1 = phonetic( w1 )) == NULL ) {
				break;
			}

			/*
			 * for each word in the attribute value from
			 * where we left off...
			 */
			for ( w2 = first_word( w2 ); w2 != NULL;
			    w2 = next_word( w2 ) ) {
				c2 = phonetic( w2 );
				rc = strcmp( c1, c2 );
				slapi_ch_free((void**)&c2 );
				if ( rc == 0 ) {
					if(retVal) {
						*retVal = bvals[i];
					}
					break;
				}
			}
			slapi_ch_free((void**)&c1 );

			/*
			 * if we stopped because we ran out of words
			 * before making a match, go on to the next
			 * value.  otherwise try to keep matching
			 * words in this value from where we left off.
			 */
			if ( w2 == NULL ) {
				break;
			} else {
				w2 = next_word( w2 );
			}
		}
		/*
		 * if we stopped because we ran out of words and
		 * we found at leasy one word, we have a match.
		 */
		if ( w1 == NULL && ava_wordcount > 0 ) {
			rc = 0;
			break;
		}
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= string_filter_approx %d\n",
	    rc, 0, 0 );

	return( rc );
}

int
string_filter_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,
    Slapi_Value **bvals, int syntax )
{
	int		i, j, rc;
	char		*p, *end, *realval, *tmpbuf;
	size_t		tmpbufsize;
	char		pat[BUFSIZ];
	char		buf[BUFSIZ];
	char		ebuf[BUFSIZ];

	LDAPDebug( LDAP_DEBUG_FILTER, "=> string_filter_sub\n",
	    0, 0, 0 );

	/*
	 * construct a regular expression corresponding to the
	 * filter and let regex do the work for each value
	 * XXX should do this once and save it somewhere XXX
	 */
	pat[0] = '\0';
	p = pat;
	end = pat + sizeof(pat) - 2;	/* leave room for null */
	if ( initial != NULL ) {
		value_normalize( initial, syntax, 1 /* trim leading blanks */ );
		strcpy( p, "^" );
		p = strchr( p, '\0' );
		/* 2 * in case every char is special */
		if ( p + 2 * strlen( initial ) > end ) {
			LDAPDebug( LDAP_DEBUG_ANY, "not enough pattern space\n",
			    0, 0, 0 );
			return( -1 );
		}
		filter_strcpy_special( p, initial );
		p = strchr( p, '\0' );
	}
	if ( any != NULL ) {
		for ( i = 0; any[i] != NULL; i++ ) {
			value_normalize( any[i], syntax, 0 /* DO NOT trim leading blanks */ );
			/* ".*" + value */
			if ( p + 2 * strlen( any[i] ) + 2 > end ) {
				LDAPDebug( LDAP_DEBUG_ANY,
				    "not enough pattern space\n", 0, 0, 0 );
				return( -1 );
			}
			strcpy( p, ".*" );
			p = strchr( p, '\0' );
			filter_strcpy_special( p, any[i] );
			p = strchr( p, '\0' );
		}
	}
	if ( final != NULL ) {
		value_normalize( final, syntax, 0 /* DO NOT trim leading blanks */ );
		/* ".*" + value */
		if ( p + 2 * strlen( final ) + 2 > end ) {
			LDAPDebug( LDAP_DEBUG_ANY, "not enough pattern space\n",
			    0, 0, 0 );
			return( -1 );
		}
		strcpy( p, ".*" );
		p = strchr( p, '\0' );
		filter_strcpy_special( p, final );
		p = strchr( p, '\0' );
		strcpy( p, "$" );
	}

	/* compile the regex */
	slapd_re_lock();
	if ( (p = slapd_re_comp( pat )) != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "re_comp (%s) failed (%s)\n",
		    pat, p, 0 );
		slapd_re_unlock();
		return( -1 );
	} else {
		LDAPDebug( LDAP_DEBUG_TRACE, "re_comp (%s)\n",
				   escape_string( pat, ebuf ), 0, 0 );
	}

	/*
	 * test the regex against each value
	 */
	rc = -1;
	tmpbuf = NULL;
	tmpbufsize = 0;
	for ( j = 0; bvals[j] != NULL; j++ ) {
		int	tmprc;
		size_t	len;
                const struct berval *bvp = slapi_value_get_berval(bvals[j]);

		len = bvp->bv_len;
		if ( len < sizeof(buf) ) {
			strcpy( buf, bvp->bv_val );
			realval = buf;
		} else if ( len < tmpbufsize ) {
			strcpy( buf, bvp->bv_val );
			realval = tmpbuf;
		} else {
			tmpbuf = (char *) slapi_ch_realloc( tmpbuf, len + 1 );
			strcpy( tmpbuf, bvp->bv_val );
			realval = tmpbuf;
		}
		value_normalize( realval, syntax, 1 /* trim leading blanks */ );

		tmprc = slapd_re_exec( realval );

		LDAPDebug( LDAP_DEBUG_TRACE, "re_exec (%s) %i\n",
				   escape_string( realval, ebuf ), tmprc, 0 );
		if ( tmprc != 0 ) {
			rc = 0;
			break;
		}
	}
	slapd_re_unlock();
	if ( tmpbuf != NULL ) {
		slapi_ch_free((void**)&tmpbuf );
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= string_filter_sub %d\n",
	    rc, 0, 0 );
	return( rc );
}

int
string_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,
    Slapi_Value ***ivals, int syntax, int ftype )
{
	int		nsubs, numbvals, i, n, j;
	Slapi_Value	**nbvals;
	char		*w, *c, *p;
	char		buf[SUBLEN+1];

	switch ( ftype ) {
	case LDAP_FILTER_EQUALITY:
		/* allocate a new array for the normalized values */
		for ( numbvals = 0; bvals[numbvals] != NULL; numbvals++ ) {
			/* NULL */
		}
		nbvals = (Slapi_Value **) slapi_ch_malloc( (numbvals+1) * sizeof(Slapi_Value *));

		for ( i = 0; i < numbvals; i++ )
		{
                    c = slapi_ch_strdup(slapi_value_get_string(bvals[i]));
                    value_normalize( c, syntax, 1 /* trim leading blanks */ );
		    nbvals[i] = slapi_value_new_string_passin(c);
		}
		nbvals[i] = NULL;
		*ivals = nbvals;
		break;

	case LDAP_FILTER_APPROX:
		/* XXX should not do this twice! XXX */
		/* get an upper bound on the number of ivals */
		numbvals = 0;
		for ( i = 0; bvals[i] != NULL; i++ ) {
			for ( w = first_word( (char*)slapi_value_get_string(bvals[i]) ); w != NULL;
			    w = next_word( w ) ) {
				numbvals++;
			}
		}
		nbvals = (Slapi_Value **) slapi_ch_malloc( (numbvals + 1) * sizeof(Slapi_Value *) );

		n = 0;
		for ( i = 0; bvals[i] != NULL; i++ ) {
			for ( w = first_word( (char*)slapi_value_get_string(bvals[i]) ); w != NULL;
			    w = next_word( w ) ) {
				if ( (c = phonetic( w )) != NULL ) {
				  nbvals[n] = slapi_value_new_string_passin(c);
				  n++;
				}
			}
		}
		nbvals[n] = NULL;

		if ( n == 0 ) {
			slapi_ch_free((void**)ivals );
			return( 0 );
		}
		*ivals = nbvals;
		break;

	case LDAP_FILTER_SUBSTRINGS:
		{
		/* XXX should remove duplicates! XXX */
		Slapi_Value *bvdup;
                const struct berval *bvp;
		nsubs = 0;
		for ( i = 0; bvals[i] != NULL; i++ ) {
			/*
			 * Note: this calculation may err on the high side,
			 * because value_normalize(), which is called below
			 * before we actually create the substring keys, may
			 * reduce the length of the value in some cases. For
			 * example, spaces are removed when space insensitive
			 * strings are normalized. But it's okay for nsubs to
			 * be too big. Since the ivals array is NULL terminated,
			 * the only downside is that we allocate more space than
			 * we really need.
			 */
			nsubs += slapi_value_get_length(bvals[i]) - SUBLEN + 3;
		}
		*ivals = (Slapi_Value **) slapi_ch_malloc( (nsubs + 1) * sizeof(Slapi_Value *) );

		buf[SUBLEN] = '\0';
		n = 0;

		bvdup= slapi_value_new(); 
		for ( i = 0; bvals[i] != NULL; i++ )
		{
			c = slapi_ch_strdup(slapi_value_get_string(bvals[i]));
			value_normalize( c, syntax, 1 /* trim leading blanks */ );
                        slapi_value_set_string_passin(bvdup, c);

                        bvp = slapi_value_get_berval(bvdup);

			/* leading */
			if ( bvp->bv_len > SUBLEN - 2 ) {
				buf[0] = '^';
				for ( j = 0; j < SUBLEN - 1; j++ ) {
					buf[j + 1] = bvp->bv_val[j];
				}
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}

			/* any */
			for ( p = bvp->bv_val;
			    p < (bvp->bv_val + bvp->bv_len - SUBLEN + 1);
			    p++ ) {
				for ( j = 0; j < SUBLEN; j++ ) {
					buf[j] = p[j];
				}
				buf[SUBLEN] = '\0';
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}

			/* trailing */
			if ( bvp->bv_len > SUBLEN - 2 ) {
				p = bvp->bv_val + bvp->bv_len - SUBLEN + 1;
				for ( j = 0; j < SUBLEN - 1; j++ ) {
					buf[j] = p[j];
				}
				buf[SUBLEN - 1] = '$';
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}
		}
		slapi_value_free(&bvdup);
		(*ivals)[n] = NULL;
		}
		break;
	}

	return( 0 );
}


/* we've added code to make our equality filter processing faster */

int
string_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			syntax,
    int			ftype
)
{
	int		i, numbvals;
    size_t len;
	char		*w, *c;
    Slapi_Value *tmpval=NULL;

    switch ( ftype ) {
    case LDAP_FILTER_EQUALITY_FAST: 
        /* this code is trying to avoid multiple malloc/frees */
        len=slapi_value_get_length(val);
        tmpval=(*ivals)[0];
        if (len >=  tmpval->bv.bv_len) {
            tmpval->bv.bv_val=(char *)slapi_ch_malloc(len+1);
        }
        memcpy(tmpval->bv.bv_val,slapi_value_get_string(val),len);
        tmpval->bv.bv_val[len]='\0';
        value_normalize(tmpval->bv.bv_val, syntax, 1 /* trim leading blanks */ );
        tmpval->bv.bv_len=strlen(tmpval->bv.bv_val);
        break;
	case LDAP_FILTER_EQUALITY:
		(*ivals) = (Slapi_Value **) slapi_ch_malloc( 2 * sizeof(Slapi_Value *) );
		(*ivals)[0] = slapi_value_dup( val );
		value_normalize( (*ivals)[0]->bv.bv_val, syntax, 1 /* trim leading blanks */ );
		(*ivals)[0]->bv.bv_len = strlen( (*ivals)[0]->bv.bv_val );
		(*ivals)[1] = NULL;
		break;

	case LDAP_FILTER_APPROX:
		/* XXX should not do this twice! XXX */
		/* get an upper bound on the number of ivals */
		numbvals = 0;
		for ( w = first_word( (char*)slapi_value_get_string(val) ); w != NULL;
		    w = next_word( w ) ) {
			numbvals++;
		}
		(*ivals) = (Slapi_Value **) slapi_ch_malloc( (numbvals + 1) *
		    sizeof(Slapi_Value *) );

		i = 0;
		for ( w = first_word( (char*)slapi_value_get_string(val) ); w != NULL;
		    w = next_word( w ) ) {
			if ( (c = phonetic( w )) != NULL ) {
				(*ivals)[i] = slapi_value_new_string_passin(c);
				i++;
			}
		}
		(*ivals)[i] = NULL;

		if ( i == 0 ) {
			slapi_ch_free((void**)ivals );
			return( 0 );
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY,
		    "string_assertion2keys_ava: unknown ftype 0x%x\n",
		    ftype, 0, 0 );
		break;
	}

	return( 0 );
}

int
string_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals,
    int			syntax
)
{
	int		nsubs, i, len;

	*ivals = NULL;

	/*
	 * First figure out how many keys we will return. The answer is based
	 * on the length of each assertion value. Since normalization may
	 * reduce the length (such as when spaces are removed from space
	 * insensitive strings), we call value_normalize() before checking
	 * the length.
	 */
	nsubs = 0;
	if ( initial != NULL ) {
		value_normalize( initial, syntax, 0 /* do not trim leading blanks */ );
		if ( strlen( initial ) > SUBLEN - 2 ) {
			nsubs += strlen( initial ) - SUBLEN + 2;
		} else {
			initial = NULL;	/* save some work later */
		}
	}
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		value_normalize( any[i], syntax, 0 /* do not trim leading blanks */ );
		len = strlen( any[i] );
		if ( len >= SUBLEN ) {
			nsubs += len - SUBLEN + 1;
		}
	}
	if ( final != NULL ) {
		value_normalize( final, syntax, 0 /* do not trim leading blanks */ );
		if ( strlen( final ) > SUBLEN - 2 ) {
			nsubs += strlen( final ) - SUBLEN + 2;
		} else {
			final = NULL; /* save some work later */
		}
	}
	if ( nsubs == 0 ) {	/* no keys to return */
		return( 0 );
	}

	/*
	 * Next, allocated the ivals array and fill it in with the actual
	 * keys.  *ivals is a NULL terminated array of Slapi_Value pointers.
	 */

	*ivals = (Slapi_Value **) slapi_ch_malloc( (nsubs + 1) * sizeof(Slapi_Value *) );

	nsubs = 0;
	if ( initial != NULL ) {
		substring_comp_keys( ivals, &nsubs, initial, '^', syntax );
	}
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		if ( strlen( any[i] ) < SUBLEN ) {
			continue;
		}
		substring_comp_keys( ivals, &nsubs, any[i], 0, syntax );
	}
	if ( final != NULL ) {
		substring_comp_keys( ivals, &nsubs, final, '$', syntax );
	}
	(*ivals)[nsubs] = NULL;

	return( 0 );
}

static void
substring_comp_keys(
    Slapi_Value	***ivals,
    int			*nsubs,
    char		*str,
    int			prepost,
    int			syntax
)
{
    int     i, len;
    char    *p;
    char    buf[SUBLEN + 1];

    LDAPDebug( LDAP_DEBUG_TRACE, "=> substring_comp_keys (%s) %d\n",
        str, prepost, 0 );

    len = strlen( str );

    /* prepend ^ for initial substring */
    if ( prepost == '^' )
    {
		buf[0] = '^';
		for ( i = 0; i < SUBLEN - 1; i++ )
		{
			buf[i + 1] = str[i];
		}
		buf[SUBLEN] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(buf);
		(*nsubs)++;
    }

    for ( p = str; p < (str + len - SUBLEN + 1); p++ )
    {
		for ( i = 0; i < SUBLEN; i++ )
		{
			buf[i] = p[i];
		}
		buf[SUBLEN] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(buf);
		(*nsubs)++;
    }

	if ( prepost == '$' )
	{
		p = str + len - SUBLEN + 1;
		for ( i = 0; i < SUBLEN - 1; i++ )
		{
			buf[i] = p[i];
		}
		buf[SUBLEN - 1] = '$';
		buf[SUBLEN] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(buf);
		(*nsubs)++;
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= substring_comp_keys\n", 0, 0, 0 );
}
