/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#if defined( XP_WIN32 )
#include <windows.h>
#else
#include <sys/types.h>
#endif
#include <string.h>
#include "dsalib.h"
#include "portable.h"
#include <stdlib.h>

#define DNSEPARATOR(c)	(c == ',' || c == ';')
#define SEPARATOR(c)	(c == ',' || c == ';' || c == '+')
#define SPACE(c)	(c == ' ' || c == '\n')
#define NEEDSESCAPE(c)	(c == '\\' || c == '"')
#define B4TYPE		0
#define INTYPE		1
#define B4EQUAL		2
#define B4VALUE		3
#define INVALUE		4
#define INQUOTEDVALUE	5
#define B4SEPARATOR	6

DS_EXPORT_SYMBOL char*
dn_normalize( char *dn )
{
	char	*d, *s;
	int	state, gotesc;

	/* Debug( LDAP_DEBUG_TRACE, "=> dn_normalize \"%s\"\n", dn, 0, 0 ); */

	gotesc = 0;
	state = B4TYPE;
	for ( d = s = dn; *s; s++ ) {
		switch ( state ) {
		case B4TYPE:
			if ( ! SPACE( *s ) ) {
				state = INTYPE;
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
			if ( *s == '"' ) {
				state = INQUOTEDVALUE;
				*d++ = *s;
			} else if ( ! SPACE( *s ) ) { 
				state = INVALUE;
				*d++ = *s;
			}
			break;
		case INVALUE:
			if ( !gotesc && SEPARATOR( *s ) ) {
				while ( SPACE( *(d - 1) ) )
					d--;
				state = B4TYPE;
				if ( *s == '+' ) {
					*d++ = *s;
				} else {
					*d++ = ',';
				}
			} else if ( gotesc && !NEEDSESCAPE( *s ) &&
			    !SEPARATOR( *s ) ) {
				*--d = *s;
				d++;
			} else {
				*d++ = *s;
			}
			break;
		case INQUOTEDVALUE:
			if ( !gotesc && *s == '"' ) {
				state = B4SEPARATOR;
				*d++ = *s;
			} else if ( gotesc && !NEEDSESCAPE( *s ) ) {
				*--d = *s;
				d++;
			} else {
				*d++ = *s;
			}
			break;
		case B4SEPARATOR:
			if ( SEPARATOR( *s ) ) {
				state = B4TYPE;
				if ( *s == '+' ) {
					*d++ = *s;
				} else {
					*d++ = ',';
				}
			}
			break;
		default:
			break;
		}
		if ( *s == '\\' ) {
			gotesc = 1;
		} else {
			gotesc = 0;
		}
	}
	*d = '\0';

	/* Debug( LDAP_DEBUG_TRACE, "<= dn_normalize \"%s\"\n", dn, 0, 0 ); */
	return( dn );
}

DS_EXPORT_SYMBOL char*
ds_dn_expand (char* dn)
{
    char* edn;
    size_t i = 0;
    char* s;
    int state = B4TYPE;
    int gotesc = 0;

    if (dn == NULL) return NULL;
    edn = strdup (dn);
	if (edn == NULL) return NULL;
    for (s = dn; *s != '\0'; ++s, ++i) {
	switch (state) {
	  case B4TYPE:
	    if ( ! SPACE (*s)) {
		state = INTYPE;
	    }
	    break;
	  case INTYPE:
	    if (*s == '=') {
		state = B4VALUE;
	    } else if (SPACE (*s)) {
		state = B4EQUAL;
	    }
	    break;
	  case B4EQUAL:
	    if (*s == '=') {
		state = B4VALUE;
	    }
	    break;
	  case B4VALUE:
	    if (*s == '"') {
		state = INQUOTEDVALUE;
	    } else if ( ! SPACE (*s)) { 
		state = INVALUE;
	    }
	    break;
	  case INQUOTEDVALUE:
	    if (gotesc) {
		if ( ! NEEDSESCAPE (*s)) {
		    --i;
		    memmove (edn+i, edn+i+1, strlen (edn+i));
		}
	    } else {
		if (*s == '"') {
		    state = B4SEPARATOR;
		}
	    }
	    break;
	  case INVALUE:
	    if (gotesc) {
		if ( ! NEEDSESCAPE (*s) && ! SEPARATOR (*s)) {
		    --i;
		    memmove (edn+i, edn+i+1, strlen (edn+i));
		}
		break;
	    }
	  case B4SEPARATOR:
	    if (SEPARATOR (*s)) {
		state = B4TYPE;
		if ( ! SPACE (s[1])) {
		    /* insert a space following edn[i] */
		    edn = (char*) realloc (edn, strlen (edn)+2);
		    ++i;
		    memmove (edn+i+1, edn+i, strlen (edn+i)+1);
		    edn[i] = ' ';
		}
	    }
	    break;
	  default:
	    break;
	}
	gotesc = (*s == '\\');
    }
    return edn;
}

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
 */

DS_EXPORT_SYMBOL char *
dn_normalize_convert( char *dn )
{
    /* \xx is changed to \c.
       \c is changed to c, unless this would change its meaning.
       All values that contain 2 or more separators are "enquoted";
       all other values are not enquoted.
     */
	char	*value, *value_separator;
	char	*d, *s;
	char    *end;

	int	gotesc = 0;
	int	state = B4TYPE;
	if (NULL == dn)
		return dn;

	end = dn + strlen(dn);
	for ( d = s = dn; s != end; s++ ) {
		switch ( state ) {
		case B4TYPE:
			if ( ! SPACE( *s ) ) {
				state = INTYPE;
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
				auto char *L = NULL;
				auto char *R;
				for ( R = value; (R = strchr( R, '\\' )) && (R < d); L = ++R ) {
				    if ( SEPARATOR( R[1] )) {
					if ( L == NULL ) {
					    auto const size_t len = R - value;
					    if ( len > 0 ) memmove( value+1, value, len );
					    *value = '"'; /* opening quote */
					    value = R + 1;
					} else {
					    auto const size_t len = R - L;
					    if ( len > 0 ) {
						memmove( value, L, len );
						value += len;
					    }
					    --d;
					}
				    }
				}
				memmove( value, L, d - L + 1 );
				*d++ = '"'; /* closing quote */
			    }
			    state = B4TYPE;
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
			    *d++ = (*s == '+') ? '+' : ',';
			}
			break;
		default:
/*			LDAPDebug( LDAP_DEBUG_ANY,
			    "slapi_dn_normalize - unknown state %d\n", state, 0, 0 );*/
			break;
		}
		if ( *s != '\\' ) {
			gotesc = 0;
		} else {
			gotesc = 1;
			if ( s+2 < end ) {
			    auto int n = hexchar2int( s[1] );
			    if ( n >= 0 ) {
				auto int n2 = hexchar2int( s[2] );
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

	/* Trim trailing spaces */
	while ( d != dn && *(d - 1) == ' ' ) d--;

	*d = 0;

	return( dn );
}

/* if dn contains an unescaped quote return true */
DS_EXPORT_SYMBOL int
ds_dn_uses_LDAPv2_quoting(const char *dn)
{
	const char ESC = '\\';
	const char Q = '"';
	int ret = 0;
	const char *p = 0;

	/* check dn for a even number (incl. 0) of ESC followed by Q */
	if (!dn)
		return ret;

	p = strchr(dn, Q);
	if (p)
	{
		int nESC = 0;
		for (--p; (p >= dn) && (*p == ESC); --p)
			++nESC;
		if (!(nESC % 2))
			ret = 1;
	}

	return ret;
}
