/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* value.c - routines for dealing with values */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

/* 
 * Do not use the SDK ldap_utf8isspace directly until it is faster
 * than this one.
 */
static int
utf8isspace_fast( char* s )
{
    register unsigned char c = *(unsigned char*)s;
    if (0x80 & c) return(ldap_utf8isspace(s));
    switch (c) {
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x20:
        return 1;
    default: break;
    }
    return 0;
}

/*
** This function is used to normalizes search filter components,
** and attribute values.
**
** jcm: I added the trim_spaces flag since this function
** was incorrectly modifying search filter components.  A search
** of the form "cn=a* b*" (note the space) would be wrongly 
** normalized into "cn=a*b*", because this function is called
** once for "a" and once for " b".
*/
void
value_normalize(
    char	*s,
    int		syntax,
    int     trim_spaces
)
{
	char	*d;
	int	prevspace, curspace;

	if ( ! (syntax & SYNTAX_CIS) && ! (syntax & SYNTAX_CES) ) {
		return;
	}

	if ( syntax & SYNTAX_DN ) {
		(void) slapi_dn_normalize_case( s );
		return;
	}

	d = s;
	if (trim_spaces) {
	    /* strip leading blanks */
	  while (utf8isspace_fast(s)) {
	      LDAP_UTF8INC(s);
	  }
	}
	/* handle value of all spaces - turn into single space */
	/* unless space insensitive syntax - turn into zero length string */
	if ( *s == '\0' && s != d ) {
		if ( ! (syntax & SYNTAX_SI)) {
			*d++ = ' ';
		}
		*d = '\0';
		return;
	}
	prevspace = 0;
	while ( *s ) {
		curspace = utf8isspace_fast(s);

		/* ignore spaces and '-' in telephone numbers */
		if ( (syntax & SYNTAX_TEL) && (curspace || *s == '-') ) {
			LDAP_UTF8INC(s);
			continue;
		}

		/* ignore all spaces if this is a space insensitive value */
		if ( (syntax & SYNTAX_SI) && curspace ) {
			LDAP_UTF8INC(s);
			continue;
		}

		/* compress multiple blanks */
		if ( prevspace && curspace ) {
		    LDAP_UTF8INC(s);
		    continue;
		}
		prevspace = curspace;
		if ( syntax & SYNTAX_CIS ) {
			int ssz, dsz;
			slapi_utf8ToLower((unsigned char*)s, (unsigned char *)d, &ssz, &dsz);
			s += ssz;
			d += dsz;
		} else {
	            char *np;
		    int sz;
			
		    np = ldap_utf8next(s);
		    if (np == NULL || np == s) break;
		    sz = np - s;
		    memmove(d,s,sz);
		    d += sz;
		    s += sz;
		}
	}
	*d = '\0';
	/* strip trailing blanks */
	if (prevspace && trim_spaces) {
	    char *nd;

	    nd = ldap_utf8prev(d);
	    while (nd && utf8isspace_fast(nd)) {
	        d = nd;
	        nd = ldap_utf8prev(d);
		*d = '\0';
	    }
	}
}

int
value_cmp(
    struct berval	*v1,
    struct berval	*v2,
    int			syntax,
    int			normalize
)
{
	int		rc;
	struct berval bvcopy1;
	struct berval bvcopy2;
	char little_buffer[64];
	size_t buffer_space = sizeof(little_buffer);
	int buffer_offset = 0;
	int free_v1 = 0;
	int free_v2 = 0;

	/* This code used to call malloc up to four times in the copying
	 * of attributes to be normalized. Now we attempt to keep everything
	 * on the stack and only malloc if the data is big
	 */
	if ( normalize & 1 ) {
		/* Do we have space in the little buffer ? */
		if (v1->bv_len < buffer_space) {
			bvcopy1.bv_len = v1->bv_len;
			SAFEMEMCPY(&little_buffer[buffer_offset],v1->bv_val,v1->bv_len);
			bvcopy1.bv_val = &little_buffer[buffer_offset];
			bvcopy1.bv_val[v1->bv_len] = '\0';
			v1 = &bvcopy1;
			buffer_space-= v1->bv_len+1;
			buffer_offset+= v1->bv_len+1;
		} else {
			v1 = ber_bvdup( v1 );
			free_v1 = 1;
		}
		value_normalize( v1->bv_val, syntax, 1 /* trim leading blanks */ );
	}
	if ( normalize & 2 ) {
		/* Do we have space in the little buffer ? */
		if (v2->bv_len < buffer_space) {
			bvcopy2.bv_len = v2->bv_len;
			SAFEMEMCPY(&little_buffer[buffer_offset],v2->bv_val,v2->bv_len);
			bvcopy2.bv_val = &little_buffer[buffer_offset];
			bvcopy2.bv_val[v2->bv_len] = '\0';
			v2 = &bvcopy2;
			buffer_space-= v2->bv_len+1;
			buffer_offset+= v2->bv_len+1;
		} else {
			v2 = ber_bvdup( v2 );
			free_v2 = 1;
		}
		value_normalize( v2->bv_val, syntax, 1 /* trim leading blanks */ );
	}

	switch ( syntax ) {
	case SYNTAX_CIS:
	case (SYNTAX_CIS | SYNTAX_TEL):
	case (SYNTAX_CIS | SYNTAX_DN):
	case (SYNTAX_CIS | SYNTAX_SI):
		rc = slapi_utf8casecmp( (unsigned char *)v1->bv_val,
					(unsigned char *)v2->bv_val );
		break;

	case SYNTAX_CES:
		rc = strcmp( v1->bv_val, v2->bv_val );
		break;
	}

	if ( (normalize & 1) && free_v1) {
		ber_bvfree( v1 );
	}
	if ( (normalize & 2) && free_v2) {
		ber_bvfree( v2 );
	}

	return( rc );
}
