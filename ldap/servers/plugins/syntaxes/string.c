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
	int lenstr, int prepost, int syntax, char *comp_buf, int *substrlens );

int
string_filter_ava( struct berval *bvfilter, Slapi_Value **bvals, int syntax,
    int ftype, Slapi_Value **retVal )
{
	int	i, rc;
	struct berval bvfilter_norm = {0, NULL};
	struct berval *pbvfilter_norm = &bvfilter_norm;
	char *alt = NULL;

	if(retVal) {
		*retVal = NULL;
	}
	if ( ftype == LDAP_FILTER_APPROX ) {
		return( string_filter_approx( bvfilter, bvals, retVal ) );
	}

	if (syntax & SYNTAX_NORM_FILT) {
		pbvfilter_norm = bvfilter; /* already normalized */
	} else {
		slapi_ber_bvcpy(&bvfilter_norm, bvfilter);
		/* 3rd arg: 1 - trim leading blanks */
		value_normalize_ext( bvfilter_norm.bv_val, syntax, 1, &alt );
		if (alt) {
			slapi_ber_bvdone(&bvfilter_norm);
			bvfilter_norm.bv_val = alt;
			alt = NULL;
		}
		bvfilter_norm.bv_len = strlen(bvfilter_norm.bv_val);
	}

	for ( i = 0; (bvals != NULL) && (bvals[i] != NULL); i++ ) {
		int norm_val = 1; /* normalize the first value only */
		/* if the NORMALIZED flag is set, skip normalizing */
		if (slapi_value_get_flags(bvals[i]) & SLAPI_ATTR_FLAG_NORMALIZED) {
			norm_val = 0;
		}
		/* note - do not return the normalized value in retVal - the
		   caller will usually want the "raw" value, and can normalize it later
		*/
		rc = value_cmp( (struct berval*)slapi_value_get_berval(bvals[i]), pbvfilter_norm, syntax, norm_val );
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

/*
 * return value:  0 -- approximately matched
 *               -1 -- did not match
 */
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
	for ( i = 0; (bvals != NULL) && (bvals[i] != NULL); i++ ) {
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
	if (0 != rc) {
		rc = -1;
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
	char		*p, *end, *realval, *tmpbuf = NULL, *bigpat = NULL;
	size_t		tmpbufsize;
	char		pat[BUFSIZ];
	char		buf[BUFSIZ];
	char		ebuf[BUFSIZ];
	time_t		curtime = 0;
	time_t		time_up = 0;
	time_t		optime = 0; /* time op was initiated */
	int		timelimit = 0; /* search timelimit */
	Operation	*op = NULL;
	Slapi_Regex	*re = NULL;
	const char  *re_result = NULL;
	char *alt = NULL;
	int filter_normalized = 0;
	int free_re = 1;
	struct subfilt *sf = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> string_filter_sub\n", 0, 0, 0 );
	if (pb) {
		slapi_pblock_get( pb, SLAPI_OPERATION, &op );
	}
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

	if (pb) {
		slapi_pblock_get( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
		slapi_pblock_get( pb, SLAPI_PLUGIN_SYNTAX_FILTER_DATA, &sf );
	}
	if ( sf ) {
		re = (Slapi_Regex *)sf->sf_private;
		if ( re ) {
			free_re = 0;
		}
	}

	if (!re) {
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
			/* 3rd arg: 1 - trim leading blanks */
			if (!filter_normalized) {
				value_normalize_ext( initial, syntax, 1, &alt );
			}
			*p++ = '^';
			if (alt) {
				filter_strcpy_special_ext( p, alt, FILTER_STRCPY_ESCAPE_RECHARS );
				slapi_ch_free_string(&alt);
			} else {
				filter_strcpy_special_ext( p, initial, FILTER_STRCPY_ESCAPE_RECHARS );
			}
			p = strchr( p, '\0' );
		}
		if ( any != NULL ) {
			for ( i = 0; any[i] != NULL; i++ ) {
				/* 3rd arg: 0 - DO NOT trim leading blanks */
				if (!filter_normalized) {
					value_normalize_ext( any[i], syntax, 0, &alt );
				}
				/* ".*" + value */
				*p++ = '.';
				*p++ = '*';
				if (alt) {
					filter_strcpy_special_ext( p, alt, FILTER_STRCPY_ESCAPE_RECHARS );
					slapi_ch_free_string(&alt);
				} else {
					filter_strcpy_special_ext( p, any[i], FILTER_STRCPY_ESCAPE_RECHARS );
				}
				p = strchr( p, '\0' );
			}
		}
		if ( final != NULL ) {
			/* 3rd arg: 0 - DO NOT trim leading blanks */
			if (!filter_normalized) {
				value_normalize_ext( final, syntax, 0, &alt );
			}
			/* ".*" + value */
			*p++ = '.';
			*p++ = '*';
			if (alt) {
				filter_strcpy_special_ext( p, alt, FILTER_STRCPY_ESCAPE_RECHARS );
				slapi_ch_free_string(&alt);
			} else {
				filter_strcpy_special_ext( p, final, FILTER_STRCPY_ESCAPE_RECHARS );
			}
			strcat( p, "$" );
		}

		/* compile the regex */
		p = (bigpat) ? bigpat : pat;
		tmpbuf = NULL;
		re = slapi_re_comp( p, &re_result );
		if (NULL == re) {
			LDAPDebug( LDAP_DEBUG_ANY, "re_comp (%s) failed (%s): %s\n",
					   pat, p, re_result?re_result:"unknown" );
			rc = LDAP_OPERATIONS_ERROR;
			goto bailout;
		} else {
			LDAPDebug( LDAP_DEBUG_TRACE, "re_comp (%s)\n",
					   escape_string( p, ebuf ), 0, 0 );
		}
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
	for ( j = 0; (bvals != NULL) && (bvals[j] != NULL); j++ ) {
		int	tmprc;
		size_t	len;
		const struct berval *bvp = slapi_value_get_berval(bvals[j]);

		len = bvp->bv_len;
		if ( len < sizeof(buf) ) {
			realval = buf;
			strncpy( realval, bvp->bv_val, sizeof(buf) );
		} else if ( len < tmpbufsize ) {
			realval = tmpbuf;
			strncpy( realval, bvp->bv_val, tmpbufsize );
		} else {
			tmpbufsize = len + 1;
			realval = tmpbuf = (char *) slapi_ch_realloc( tmpbuf, tmpbufsize );
			strncpy( realval, bvp->bv_val, tmpbufsize );
		}
		/* 3rd arg: 1 - trim leading blanks */
		if (!(slapi_value_get_flags(bvals[j]) & SLAPI_ATTR_FLAG_NORMALIZED)) {
			value_normalize_ext( realval, syntax, 1, &alt );
		} else if (syntax & SYNTAX_DN) {
			slapi_dn_ignore_case(realval);
		}
		if (alt) {
			tmprc = slapi_re_exec( re, alt, time_up );
			slapi_ch_free_string(&alt);
		} else {
			tmprc = slapi_re_exec( re, realval, time_up );
		}

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
	if (free_re) {
		slapi_re_free(re);
	}
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
	char *alt = NULL;

	if (NULL == ivals) {
		return 1;
	}
	*ivals = NULL;
	if (NULL == bvals) {
		return 1;
	}

	switch ( ftype ) {
	case LDAP_FILTER_EQUALITY:
		/* allocate a new array for the normalized values */
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			numbvals++;
		}
		nbvals = (Slapi_Value **) slapi_ch_calloc( (numbvals + 1), sizeof(Slapi_Value *));

		for ( bvlp = bvals, nbvlp = nbvals; bvlp && *bvlp; bvlp++, nbvlp++ )
		{
			unsigned long value_flags = slapi_value_get_flags(*bvlp);
			c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
			/* if the NORMALIZED flag is set, skip normalizing */
			if (!(value_flags & SLAPI_ATTR_FLAG_NORMALIZED)) {
				/* 3rd arg: 1 - trim leading blanks */
				value_normalize_ext( c, syntax, 1, &alt );
				value_flags |= SLAPI_ATTR_FLAG_NORMALIZED;
			} else if ((syntax & SYNTAX_DN) &&
			           (value_flags & SLAPI_ATTR_FLAG_NORMALIZED_CES)) {
				/* This dn value is normalized, but not case-normalized. */
				slapi_dn_ignore_case(c);
				/* This dn value is case-normalized */
				value_flags &= ~SLAPI_ATTR_FLAG_NORMALIZED_CES;
				value_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CIS;
			}
			if (alt) {
				slapi_ch_free_string(&c);
				*nbvlp = slapi_value_new_string_passin(alt);
				alt = NULL;
			} else {
				*nbvlp = slapi_value_new_string_passin(c);
				c = NULL;
			}
			/* new value is normalized */
			slapi_value_set_flags(*nbvlp, value_flags);
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
 		 * By default, begin == 3, middle == 3, end == 3 (defined in syntax.h)
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
			 * because value_normalize_ext(), which is called below
			 * before we actually create the substring keys, may
			 * reduce the length of the value in some cases or 
			 * increase the length in other cases. For example,
			 * spaces are removed when space insensitive strings 
			 * are normalized. Or if the value includes '\"' (2 bytes),
			 * it's normalized to '\22' (3 bytes). But it's okay 
			 * for nsubs to be too big. Since the ivals array is 
			 * NULL terminated, the only downside is that we 
			 * allocate more space than we really need.
			 */
			nsubs += slapi_value_get_length(*bvlp) - substrlens[INDEX_SUBSTRMIDDLE] + 3;
		}
		nsubs += substrlens[INDEX_SUBSTRMIDDLE] * 2 - substrlens[INDEX_SUBSTRBEGIN] - substrlens[INDEX_SUBSTREND];
		*ivals = (Slapi_Value **) slapi_ch_calloc( (nsubs + 1), sizeof(Slapi_Value *) );

		n = 0;

		bvdup= slapi_value_new(); 
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			unsigned long value_flags = slapi_value_get_flags(*bvlp);
			/* 3rd arg: 1 - trim leading blanks */
			if (!(value_flags & SLAPI_ATTR_FLAG_NORMALIZED)) {
				c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
				value_normalize_ext( c, syntax, 1, &alt );
				if (alt) {
					slapi_ch_free_string(&c);
					slapi_value_set_string_passin(bvdup, alt);
					alt = NULL;
				} else {
					slapi_value_set_string_passin(bvdup, c);
					c = NULL;
				}
				bvp = slapi_value_get_berval(bvdup);
				value_flags |= SLAPI_ATTR_FLAG_NORMALIZED;
			} else if ((syntax & SYNTAX_DN) &&
			           (value_flags & SLAPI_ATTR_FLAG_NORMALIZED_CES)) {
				/* This dn value is normalized, but not case-normalized. */
				c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
				slapi_dn_ignore_case(c);
				slapi_value_set_string_passin(bvdup, c);
				c = NULL;
				/* This dn value is case-normalized */
				value_flags &= ~SLAPI_ATTR_FLAG_NORMALIZED_CES;
				value_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CIS;
				bvp = slapi_value_get_berval(bvdup);
			} else {
				bvp = slapi_value_get_berval(*bvlp);
			}

			/* leading */
			if ( bvp->bv_len > substrlens[INDEX_SUBSTRBEGIN] - 2 ) {
				buf[0] = '^';
				for ( i = 0; i < substrlens[INDEX_SUBSTRBEGIN] - 1; i++ ) {
					buf[i + 1] = bvp->bv_val[i];
				}
				buf[substrlens[INDEX_SUBSTRBEGIN]] = '\0';
				(*ivals)[n] = slapi_value_new_string(buf);
				slapi_value_set_flags((*ivals)[n], value_flags);
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
				slapi_value_set_flags((*ivals)[n], value_flags);
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
				slapi_value_set_flags((*ivals)[n], value_flags);
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
    char *alt = NULL;
    unsigned long flags = val ? slapi_value_get_flags(val) : 0;

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
        if (!(flags & SLAPI_ATTR_FLAG_NORMALIZED)) {
            /* 3rd arg: 1 - trim leading blanks */
            value_normalize_ext(tmpval->bv.bv_val, syntax, 1, &alt );
            if (alt) {
                if (len >=  tmpval->bv.bv_len) {
                    slapi_ch_free_string(&tmpval->bv.bv_val);
                }
                tmpval->bv.bv_val = alt;
                alt = NULL;
            }
            tmpval->bv.bv_len=strlen(tmpval->bv.bv_val);
            flags |= SLAPI_ATTR_FLAG_NORMALIZED;
        } else if ((syntax & SYNTAX_DN) &&
                   (flags & SLAPI_ATTR_FLAG_NORMALIZED_CES)) {
            /* This dn value is normalized, but not case-normalized. */
            slapi_dn_ignore_case(tmpval->bv.bv_val);
            /* This dn value is case-normalized */
            flags &= ~SLAPI_ATTR_FLAG_NORMALIZED_CES;
            flags |= SLAPI_ATTR_FLAG_NORMALIZED_CIS;
        }
        slapi_value_set_flags(tmpval, flags);
        break;
	case LDAP_FILTER_EQUALITY:
		(*ivals) = (Slapi_Value **) slapi_ch_malloc( 2 * sizeof(Slapi_Value *) );
		(*ivals)[0] = val ? slapi_value_dup( val ) : NULL;
		if (val && !(flags & SLAPI_ATTR_FLAG_NORMALIZED)) {
			/* 3rd arg: 1 - trim leading blanks */
			value_normalize_ext( (*ivals)[0]->bv.bv_val, syntax, 1, &alt );
			if (alt) {
				slapi_ch_free_string(&(*ivals)[0]->bv.bv_val);
				(*ivals)[0]->bv.bv_val = alt;
				(*ivals)[0]->bv.bv_len = strlen( (*ivals)[0]->bv.bv_val );
				alt = NULL;
			}
			flags |= SLAPI_ATTR_FLAG_NORMALIZED;
		} else if ((syntax & SYNTAX_DN) &&
		           (flags & SLAPI_ATTR_FLAG_NORMALIZED_CES)) {
            /* This dn value is normalized, but not case-normalized. */
			slapi_dn_ignore_case((*ivals)[0]->bv.bv_val);
			/* This dn value is case-normalized */
			flags &= ~SLAPI_ATTR_FLAG_NORMALIZED_CES;
			flags |= SLAPI_ATTR_FLAG_NORMALIZED_CIS;
		}
		slapi_value_set_flags((*ivals)[0], flags);
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
	int initiallen = 0, finallen = 0;
	int *substrlens = NULL;
	int localsublens[3] = {SUBBEGIN, SUBMIDDLE, SUBEND};/* default values */
	int maxsublen;
	char	*comp_buf = NULL;
	/* altinit|any|final: store alt string from value_normalize_ext if any,
	 * otherwise the original string. And use for the real job */
	char *altinit = NULL;
	char **altany = NULL;
	char *altfinal = NULL;
	/* oaltinit|any|final: prepared to free altinit|any|final if allocated in
	 * value_normalize_ext */
	char *oaltinit = NULL;
	char **oaltany = NULL;
	char *oaltfinal = NULL;
	int anysize = 0;

	if (pb) {
		slapi_pblock_get(pb, SLAPI_SYNTAX_SUBSTRLENS, &substrlens);
	}

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
	 * insensitive strings), we call value_normalize_ext() before checking
	 * the length.
	 */
	nsubs = 0;
	if ( initial != NULL ) {
		/* 3rd arg: 0 - DO NOT trim leading blanks */
		value_normalize_ext( initial, syntax, 0, &altinit );
		oaltinit = altinit;
		if (NULL == altinit) {
			altinit = initial;
		}
		initiallen = strlen( altinit );
		if ( initiallen > substrlens[INDEX_SUBSTRBEGIN] - 2 ) {
			nsubs += 1; /* for the initial begin string key */
			/* the rest of the sub keys are "any" keys for this case */
			if ( initiallen >= substrlens[INDEX_SUBSTRMIDDLE] ) {
				nsubs += initiallen - substrlens[INDEX_SUBSTRMIDDLE] + 1;
			}
		} else {
			altinit = NULL;	/* save some work later */
		}
	}
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		anysize++;
	}
	altany = (char **)slapi_ch_calloc(anysize + 1, sizeof(char *));
	oaltany = (char **)slapi_ch_calloc(anysize + 1, sizeof(char *));
	for ( i = 0; any != NULL && any[i] != NULL; i++ ) {
		/* 3rd arg: 0 - DO NOT trim leading blanks */
		value_normalize_ext( any[i], syntax, 0, &altany[i] );
		if (NULL == altany[i]) {
			altany[i] = any[i];
		} else {
			oaltany[i] = altany[i];
		}
		len = strlen( altany[i] );
		if ( len >= substrlens[INDEX_SUBSTRMIDDLE] ) {
			nsubs += len - substrlens[INDEX_SUBSTRMIDDLE] + 1;
		}
	}
	if ( final != NULL ) {
		/* 3rd arg: 0 - DO NOT trim leading blanks */
		value_normalize_ext( final, syntax, 0, &altfinal );
		oaltfinal = altfinal;
		if (NULL == altfinal) {
			altfinal = final;
		}
		finallen = strlen( altfinal );
		if ( finallen > substrlens[INDEX_SUBSTREND] - 2 ) {
			nsubs += 1; /* for the final end string key */
			/* the rest of the sub keys are "any" keys for this case */
			if ( finallen >= substrlens[INDEX_SUBSTRMIDDLE] ) {
				nsubs += finallen - substrlens[INDEX_SUBSTRMIDDLE] + 1;
			}
		} else {
			altfinal = NULL; /* save some work later */
		}
	}
	if ( nsubs == 0 ) {	/* no keys to return */
		goto done;
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
	if ( altinit != NULL ) {
		substring_comp_keys( ivals, &nsubs, altinit, initiallen, '^', syntax,
							 comp_buf, substrlens );
	}
	for ( i = 0; altany != NULL && altany[i] != NULL; i++ ) {
		len = strlen( altany[i] );
		if ( len < substrlens[INDEX_SUBSTRMIDDLE] ) {
			continue;
		}
		substring_comp_keys( ivals, &nsubs, altany[i], len, 0, syntax,
							 comp_buf, substrlens );
	}
	if ( altfinal != NULL ) {
		substring_comp_keys( ivals, &nsubs, altfinal, finallen, '$', syntax,
							 comp_buf, substrlens );
	}
	(*ivals)[nsubs] = NULL;

done:
	slapi_ch_free_string(&oaltinit);
	for ( i = 0; altany != NULL && altany[i] != NULL; i++ ) {
		slapi_ch_free_string(&oaltany[i]);
	}
	slapi_ch_free((void **)&oaltany);
	slapi_ch_free_string(&oaltfinal);
	slapi_ch_free((void **)&altany);
	slapi_ch_free_string(&comp_buf);
	return( 0 );
}

static void
substring_comp_keys(
    Slapi_Value	***ivals,
    int			*nsubs,
    char		*str,
    int         lenstr,
    int			prepost,
    int			syntax,
	char		*comp_buf,
	int			*substrlens
)
{
    int     i, substrlen;
    char    *p;

	PR_ASSERT(NULL != comp_buf);
	PR_ASSERT(NULL != substrlens);

    LDAPDebug( LDAP_DEBUG_TRACE, "=> substring_comp_keys (%s) %d\n",
        str, prepost, 0 );

    /* prepend ^ for initial substring */
    if ( prepost == '^' )
    {
		substrlen = substrlens[INDEX_SUBSTRBEGIN];
		comp_buf[0] = '^';
		for ( i = 0; i < substrlen - 1; i++ )
		{
			comp_buf[i + 1] = str[i];
		}
		comp_buf[substrlen] = '\0';
		(*ivals)[*nsubs] = slapi_value_new_string(comp_buf);
		(*nsubs)++;
    }

    substrlen = substrlens[INDEX_SUBSTRMIDDLE];
    for ( p = str; p < (str + lenstr - substrlen + 1); p++ )
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
		substrlen = substrlens[INDEX_SUBSTREND];
		p = str + lenstr - substrlen + 1;
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
