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

/* string.c - common string syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"
#if defined(IRIX)
#include <unistd.h>
#endif

#define MAX_VAL(x,y)                    ((x)>(y)?(x):(y))

static int string_filter_approx( struct berval *bvfilter,
	Slapi_Value **bvals, Slapi_Value **retVal );
static void substring_comp_keys( Slapi_Value ***ivals, int *nsubs, char *str,
	int prepost, int syntax, char *comp_buf, int substrlen );

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
	bvfilter_norm.bv_len = strlen(bvfilter_norm.bv_val);

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
	int		i, j, rc, size=0;
	char		*p, *end, *realval, *tmpbuf, *bigpat = NULL;
	size_t		tmpbufsize;
	char		pat[BUFSIZ];
	char		buf[BUFSIZ];
	char		ebuf[BUFSIZ];
	time_t		curtime = 0;
	time_t		time_up = 0;
	time_t		optime = 0; /* time op was initiated */
	int		timelimit = 0; /* search timelimit */
	Operation *op = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> string_filter_sub\n",
	    0, 0, 0 );
	slapi_pblock_get( pb, SLAPI_OPERATION, &op );
	if (NULL != op) {
		slapi_pblock_get( pb, SLAPI_SEARCH_TIMELIMIT, &timelimit );
		slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime );
	} else {
		/* timelimit is not passed via pblock */
		timelimit = -1;
	}
	/*
	 * (timelimit==-1) means no time limit
	 */
	time_up = ( timelimit==-1 ? -1 : optime + timelimit);

	/*
	 * construct a regular expression corresponding to the
	 * filter and let regex do the work for each value
	 * XXX should do this once and save it somewhere XXX
	 */
	pat[0] = '\0';
	p = pat;
	end = pat + sizeof(pat) - 2;	/* leave room for null */

	if ( initial != NULL ) {
		size = strlen( initial ) + 1; /* add 1 for "^" */
	}

	if ( any != NULL ) {
		i = 0;
		while ( any[i] ) {
			size += strlen(any[i++]) + 2; /* add 2 for ".*" */
		}
	}

	if ( final != NULL ) {
		 size += strlen( final ) + 3; /* add 3 for ".*" and "$" */
	}

	size *= 2; /* doubled in case all filter chars need escaping */
	size++; /* add 1 for null */

	if ( p + size > end ) {
		bigpat = slapi_ch_malloc( size );
		p = bigpat;
	}

	if ( initial != NULL ) {
		value_normalize( initial, syntax, 1 /* trim leading blanks */ );
		*p++ = '^';
		filter_strcpy_special( p, initial );
		p = strchr( p, '\0' );
	}
	if ( any != NULL ) {
		for ( i = 0; any[i] != NULL; i++ ) {
			value_normalize( any[i], syntax, 0 /* DO NOT trim leading blanks */ );
			/* ".*" + value */
			*p++ = '.';
			*p++ = '*';
			filter_strcpy_special( p, any[i] );
			p = strchr( p, '\0' );
		}
	}
	if ( final != NULL ) {
		value_normalize( final, syntax, 0 /* DO NOT trim leading blanks */ );
		/* ".*" + value */
		*p++ = '.';
		*p++ = '*';
		filter_strcpy_special( p, final );
		strcat( p, "$" );
	}

	/* compile the regex */
	p = (bigpat) ? bigpat : pat;
	slapd_re_lock();
	if ( (tmpbuf = slapd_re_comp( p )) != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "re_comp (%s) failed (%s): %s\n",
		    pat, p, tmpbuf );
		rc = LDAP_OPERATIONS_ERROR;
		goto bailout;
	} else {
 		LDAPDebug( LDAP_DEBUG_TRACE, "re_comp (%s)\n",
				   escape_string( p, ebuf ), 0, 0 );
	}

	curtime = current_time();
	if ( time_up != -1 && curtime > time_up ) {
		rc = LDAP_TIMELIMIT_EXCEEDED;
		goto bailout;
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

		tmprc = slapd_re_exec( realval, time_up );

		LDAPDebug( LDAP_DEBUG_TRACE, "re_exec (%s) %i\n",
				   escape_string( realval, ebuf ), tmprc, 0 );
		if ( tmprc == 1 ) {
			rc = 0;
			break;
		} else if ( tmprc != 0 ) {
			rc = tmprc;
			break;
		}
	}
bailout:
	slapd_re_unlock();
	slapi_ch_free((void**)&tmpbuf );	/* NULL is fine */
	slapi_ch_free((void**)&bigpat );	/* NULL is fine */

	LDAPDebug( LDAP_DEBUG_FILTER, "<= string_filter_sub %d\n",
	    rc, 0, 0 );
	return( rc );
}

int
string_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,
    Slapi_Value ***ivals, int syntax, int ftype )
{
	int		nsubs, numbvals = 0, n;
	Slapi_Value	**nbvals, **nbvlp;
	Slapi_Value **bvlp;
	char		*w, *c, *p;

	switch ( ftype ) {
	case LDAP_FILTER_EQUALITY:
		/* allocate a new array for the normalized values */
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			numbvals++;
		}
		nbvals = (Slapi_Value **) slapi_ch_calloc( (numbvals + 1), sizeof(Slapi_Value *));

		for ( bvlp = bvals, nbvlp = nbvals; bvlp && *bvlp; bvlp++, nbvlp++ )
		{
			c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
			/* if the NORMALIZED flag is set, skip normalizing */
			if (!(slapi_value_get_flags(*bvlp) & SLAPI_ATTR_FLAG_NORMALIZED))
				value_normalize( c, syntax, 1 /* trim leading blanks */ );
		    *nbvlp = slapi_value_new_string_passin(c);
		}
		*ivals = nbvals;
		break;

	case LDAP_FILTER_APPROX:
		/* XXX should not do this twice! XXX */
		/* get an upper bound on the number of ivals */
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			for ( w = first_word( (char*)slapi_value_get_string(*bvlp) );
				  w != NULL; w = next_word( w ) ) {
				numbvals++;
			}
		}
		nbvals = (Slapi_Value **) slapi_ch_calloc( (numbvals + 1), sizeof(Slapi_Value *) );

		n = 0;
		nbvlp = nbvals;
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			for ( w = first_word( (char*)slapi_value_get_string(*bvlp) );
				  w != NULL; w = next_word( w ) ) {
				if ( (c = phonetic( w )) != NULL ) {
				  *nbvlp = slapi_value_new_string_passin(c);
				  nbvlp++;
				}
			}
		}

		/* even if (n == 0), we should return the array nbvals w/ NULL items */
		*ivals = nbvals;
		break;

	case LDAP_FILTER_SUBSTRINGS:
		{
		/* XXX should remove duplicates! XXX */
		Slapi_Value *bvdup;
		const struct berval *bvp;
		char *buf;
		int i;
		int *substrlens = NULL;
		int localsublens[3] = {SUBBEGIN, SUBMIDDLE, SUBEND};/* default values */
		int maxsublen;
		/*
 		 * Substring key has 3 types:
		 * begin (e.g., *^a)
		 * middle (e.g., *abc)
		 * end (e.g., *xy$)
		 *
		 * the each has its own key length, which can be configured as follows:
 		 * Usage: turn an index object to extensibleobject and 
 		 *        set an integer value for each.
 		 * dn: cn=sn, cn=index, cn=userRoot, cn=ldbm database, cn=plugins, 
		 *  cn=config
 		 * objectClass: extensibleObject
 		 * nsSubStrBegin: 2
 		 * nsSubStrMiddle: 3
 		 * nsSubStrEnd: 2
 		 * [...]
 		 * 
 		 * By default, begin == 2, middle == 3, end == 2 (defined in syntax.h)
		 */

		/* If nsSubStrLen is specified in each index entry,
		   respect the length for the substring index key length.
		   Otherwise, the deafult value SUBLEN is used */
		slapi_pblock_get(pb, SLAPI_SYNTAX_SUBSTRLENS, &substrlens);

		if (NULL == substrlens) {
			substrlens = localsublens;
		}
		if (0 == substrlens[INDEX_SUBSTRBEGIN]) {
			substrlens[INDEX_SUBSTRBEGIN] = SUBBEGIN;
		}
		if (0 == substrlens[INDEX_SUBSTRMIDDLE]) {
			substrlens[INDEX_SUBSTRMIDDLE] = SUBMIDDLE;
		}
		if (0 == substrlens[INDEX_SUBSTREND]) {
			substrlens[INDEX_SUBSTREND] = SUBEND;
		}
		maxsublen = MAX_VAL(substrlens[INDEX_SUBSTRBEGIN], substrlens[INDEX_SUBSTRMIDDLE]);
		maxsublen = MAX_VAL(maxsublen, substrlens[INDEX_SUBSTREND]);

		buf = (char *)slapi_ch_calloc(1, maxsublen + 1);

		nsubs = 0;
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
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
			nsubs += slapi_value_get_length(*bvlp) - substrlens[INDEX_SUBSTRMIDDLE] + 3;
		}
		nsubs += substrlens[INDEX_SUBSTRMIDDLE] * 2 - substrlens[INDEX_SUBSTRBEGIN] - substrlens[INDEX_SUBSTREND];
		*ivals = (Slapi_Value **) slapi_ch_calloc( (nsubs + 1), sizeof(Slapi_Value *) );

		n = 0;

		bvdup= slapi_value_new(); 
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
			value_normalize( c, syntax, 1 /* trim leading blanks */ );
			slapi_value_set_string_passin(bvdup, c);

			bvp = slapi_value_get_berval(bvdup);

			/* leading */
			if ( bvp->bv_len > substrlens[INDEX_SUBSTRBEGIN] - 2 ) {
				buf[0] = '^';
				for ( i = 0; i < substrlens[INDEX_SUBSTRBEGIN] - 1; i++ ) {
					buf[i + 1] = bvp->bv_val[i];
				}
				buf[substrlens[INDEX_SUBSTRBEGIN]] = '\0';
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}

			/* any */
			for ( p = bvp->bv_val;
			    p < (bvp->bv_val + bvp->bv_len - substrlens[INDEX_SUBSTRMIDDLE] + 1);
			    p++ ) {
				for ( i = 0; i < substrlens[INDEX_SUBSTRMIDDLE]; i++ ) {
					buf[i] = p[i];
				}
				buf[substrlens[INDEX_SUBSTRMIDDLE]] = '\0';
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}

			/* trailing */
			if ( bvp->bv_len > substrlens[INDEX_SUBSTREND] - 2 ) {
				p = bvp->bv_val + bvp->bv_len - substrlens[INDEX_SUBSTREND] + 1;
				for ( i = 0; i < substrlens[INDEX_SUBSTREND] - 1; i++ ) {
					buf[i] = p[i];
				}
				buf[substrlens[INDEX_SUBSTREND] - 1] = '$';
				buf[substrlens[INDEX_SUBSTREND]] = '\0';
				(*ivals)[n] = slapi_value_new_string(buf);
				n++;
			}
		}
		slapi_value_free(&bvdup);
		slapi_ch_free_string(&buf);
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
	int *substrlens = NULL;
	int localsublens[3] = {SUBBEGIN, SUBMIDDLE, SUBEND};/* default values */
	int maxsublen;
	char	*comp_buf = NULL;

	slapi_pblock_get(pb, SLAPI_SYNTAX_SUBSTRLENS, &substrlens);

	if (NULL == substrlens) {
		substrlens = localsublens;
	}
	if (0 == substrlens[INDEX_SUBSTRBEGIN]) {
		substrlens[INDEX_SUBSTRBEGIN] = SUBBEGIN;
	}
	if (0 == substrlens[INDEX_SUBSTRMIDDLE]) {
		substrlens[INDEX_SUBSTRMIDDLE] = SUBMIDDLE;
	}
	if (0 == substrlens[INDEX_SUBSTREND]) {
		substrlens[INDEX_SUBSTREND] = SUBEND;
	}

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
		if ( strlen( initial ) > substrlens[INDEX_SUBSTRBEGIN] - 2 ) {
			nsubs += strlen( initial ) - substrlens[INDEX_SUBSTRBEGIN] + 2;
		} else {
			initial = NULL;	/* save some work later */
		}
	}
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		value_normalize( any[i], syntax, 0 /* do not trim leading blanks */ );
		len = strlen( any[i] );
		if ( len >= substrlens[INDEX_SUBSTRMIDDLE] ) {
			nsubs += len - substrlens[INDEX_SUBSTRMIDDLE] + 1;
		}
	}
	if ( final != NULL ) {
		value_normalize( final, syntax, 0 /* do not trim leading blanks */ );
		if ( strlen( final ) > substrlens[INDEX_SUBSTREND] - 2 ) {
			nsubs += strlen( final ) - substrlens[INDEX_SUBSTREND] + 2;
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
	
	maxsublen = MAX_VAL(substrlens[INDEX_SUBSTRBEGIN], substrlens[INDEX_SUBSTRMIDDLE]);
	maxsublen = MAX_VAL(maxsublen, substrlens[INDEX_SUBSTREND]);

	nsubs = 0;
	comp_buf = (char *)slapi_ch_malloc(maxsublen + 1);
	if ( initial != NULL ) {
		substring_comp_keys( ivals, &nsubs, initial, '^', syntax,
							 comp_buf, substrlens[INDEX_SUBSTRBEGIN] );
	}
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		if ( strlen( any[i] ) < substrlens[INDEX_SUBSTRMIDDLE] ) {
			continue;
		}
		substring_comp_keys( ivals, &nsubs, any[i], 0, syntax,
							 comp_buf, substrlens[INDEX_SUBSTRMIDDLE] );
	}
	if ( final != NULL ) {
		substring_comp_keys( ivals, &nsubs, final, '$', syntax,
							 comp_buf, substrlens[INDEX_SUBSTREND] );
	}
	(*ivals)[nsubs] = NULL;
	slapi_ch_free_string(&comp_buf);

	return( 0 );
}

static void
substring_comp_keys(
    Slapi_Value	***ivals,
    int			*nsubs,
    char		*str,
    int			prepost,
    int			syntax,
	char		*comp_buf,
	int			substrlen
)
{
    int     i, len;
    char    *p;

	PR_ASSERT(NULL != comp_buf);

    LDAPDebug( LDAP_DEBUG_TRACE, "=> substring_comp_keys (%s) %d\n",
        str, prepost, 0 );

    len = strlen( str );

    /* prepend ^ for initial substring */
    if ( prepost == '^' )
    {
		comp_buf[0] = '^';
		for ( i = 0; i < substrlen - 1; i++ )
		{
			comp_buf[i + 1] = str[i];
		}
		comp_buf[substrlen] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(comp_buf);
		(*nsubs)++;
    }

    for ( p = str; p < (str + len - substrlen + 1); p++ )
    {
		for ( i = 0; i < substrlen; i++ )
		{
			comp_buf[i] = p[i];
		}
		comp_buf[substrlen] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(comp_buf);
		(*nsubs)++;
    }

	if ( prepost == '$' )
	{
		p = str + len - substrlen + 1;
		for ( i = 0; i < substrlen - 1; i++ )
		{
			comp_buf[i] = p[i];
		}
		comp_buf[substrlen - 1] = '$';
		comp_buf[substrlen] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(comp_buf);
		(*nsubs)++;
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= substring_comp_keys\n", 0, 0, 0 );
}
