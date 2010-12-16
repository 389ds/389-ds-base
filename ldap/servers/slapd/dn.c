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
static void sort_rdn_avs( struct berval *avs, int count, int escape );
static int rdn_av_cmp( struct berval *av1, struct berval *av2 );
static void rdn_av_swap( struct berval *av1, struct berval *av2, int escape );


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

#define ISBLANK(c)	((c) == ' ')
#define ISBLANKSTR(s)	(((*(s)) == '2') && (*((s)+1) == '0'))
#define ISSPACE(c)	(ISBLANK(c) || ((c) == '\n') || ((c) == '\r'))   /* XXX 518524 */

#define ISEQUAL(c) ((c) == '=')
#define ISEQUALSTR(s) \
  ((*(s) == '3') && ((*((s)+1) == 'd') || (*((s)+1) == 'D')))

#define ISPLUS(c) ((c) == '+')
#define ISPLUSSTR(s) \
  ((*(s) == '2') && ((*((s)+1) == 'b') || (*((s)+1) == 'B')))

#define ISESCAPE(c) ((c) == '\\')
#define ISQUOTE(c) ((c) == '"')

#define DNSEPARATOR(c)	(((c) == ',') || ((c) == ';'))
#define DNSEPARATORSTR(s) \
  (((*(s) == '2') && ((*((s)+1) == 'c') || (*((s)+1) == 'C'))) || \
   ((*(s) == '3') && ((*((s)+1) == 'b') || (*((s)+1) == 'B'))))

#define SEPARATOR(c)	(DNSEPARATOR(c) || ISPLUS(c))
#define SEPARATORSTR(s)	(DNSEPARATORSTR(s) || ISPLUSSTR(s))

#define NEEDSESCAPE(c)	(ISESCAPE(c) || ISQUOTE(c) || SEPARATOR(c) || \
  ((c) == '<') || ((c) == '>') || ISEQUAL(c))
#define NEEDSESCAPESTR(s) \
  (((*(s) == '2') && ((*((s)+1) == '2') || \
	  (*((s)+1) == 'b') || (*((s)+1) == 'B') || \
	  (*((s)+1) == 'c') || (*((s)+1) == 'C'))) || \
   ((*(s) == '3') && (((*((s)+1) >= 'b') && (*((s)+1) < 'f')) || \
      ((*((s)+1) >= 'B') && (*((s)+1) < 'F')))) || \
   ((*(s) == '5') && ((*((s)+1) == 'c') || (*((s)+1) == 'C'))))

#define LEADNEEDSESCAPE(c)	(ISBLANK(c) || ((c) == '#') || NEEDSESCAPE(c))
#define LEADNEEDSESCAPESTR(s) (NEEDSESCAPESTR(s) || \
  ((*(s) == '2') && (*((s)+1) == '3')))

#define ISCLOSEBRACKET(c) (((c) == ')') || ((c) == ']'))

#define MAYBEDN(eq) ( \
    (eq) && ((eq) != subtypestart) && \
    ((eq) != subtypestart + strlen(subtypestart) - 3) \
)

#define B4TYPE           0
#define INTYPE           1
#define B4EQUAL          2
#define B4VALUE          3
#define INVALUE          4
#define INQUOTEDVALUE    5
#define B4SEPARATOR      6
#define INVALUE1ST       7
#define INQUOTEDVALUE1ST 8

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
 * ISSPACE macro above is limited, but also its frequent use of '-1' indexing 
 * into a char[] may hit the middle of a multi-byte UTF-8 whitespace character
 * encoding (518524).
 */

char *
substr_dn_normalize_orig( char *dn, char *end )
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
	char		*rdnbegin = NULL;
	char		*lastesc = NULL;
	int		gotesc = 0;
	int		state = B4TYPE;
	int		rdn_av_count = 0;
	struct berval	*rdn_avs = NULL;
	struct berval	initial_rdn_av_stack[ SLAPI_DNNORM_INITIAL_RDN_AVS ];

	for ( d = s = dn; s != end; s++ ) {
		switch ( state ) {
		case B4TYPE:
			if ( ! ISSPACE( *s ) ) {
				state = INTYPE;
				typestart = d;
				*d++ = *s;
			}
			break;
		case INTYPE:
			if ( *s == '=' ) {
				state = B4VALUE;
				*d++ = *s;
			} else if ( ISSPACE( *s ) ) {
				state = B4EQUAL;
			} else {
				*d++ = *s;
			}
			break;
		case B4EQUAL:
			if ( *s == '=' ) {
				state = B4VALUE;
				*d++ = *s;
			} else if ( ! ISSPACE( *s ) ) {
				/* not a valid dn - but what can we do here? */
				*d++ = *s;
			}
			break;
		case B4VALUE:
			if ( *s == '"' || ! ISSPACE( *s ) ) {
				value_separator = NULL;
				value = d;
				state = ( *s == '"' ) ? INQUOTEDVALUE : INVALUE1ST;
				rdnbegin = d;
				lastesc = NULL;
				*d++ = *s;
			}
			break;
		case INVALUE1ST:
		case INVALUE:
			if ( gotesc ) {
				if ( SEPARATOR( *s ) ) {
					value_separator = d;
				}
				if ( INVALUE1ST == state ) {
					if ( !LEADNEEDSESCAPE( *s )) {
						/* checking the leading char + special chars */
						--d; /* eliminate the \ */
					}
				} else if ( !NEEDSESCAPE( *s ) ) {
					--d; /* eliminate the \ */
					lastesc = d;
				}
			} else if ( SEPARATOR( *s ) ) {
				/* handling a trailing escaped space */
				/* assuming a space is the only an extra character which
				 * is not escaped if it appears in the middle, but should
				 * be if it does at the end of the RDN value */
				/* e.g., ou=ABC  \   ,o=XYZ --> ou=ABC  \ ,o=XYZ */
				if ( lastesc ) {
					while ( ISSPACE( *(d - 1) ) && d > lastesc ) {
						d--;
					}
					if ( d == lastesc ) {
						*d++ = '\\';
						*d++ = ' '; /* escaped trailing space */
					}
				} else {
					while ( ISSPACE( *(d - 1) ) ) {
						d--;
					}
				}
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
						} /* if ( SEPARATOR( R[1] )) */
					} /* for */
					memmove( value, L, d - L + escape_skips );
					*d++ = '"'; /* closing quote */
				} /* if (value_separator == dn) */
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
						sort_rdn_avs( rdn_avs, rdn_av_count, 0 );
					}
					if ( rdn_av_count > 0 ) {
						reset_rdn_avs( &rdn_avs, &rdn_av_count );
					}
				}

				*d++ = (*s == '+') ? '+' : ',';
				break;
			} /* else if ( SEPARATOR( *s ) ) */
			if ( INVALUE1ST == state ) {
				state = INVALUE;
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
					 || ISSPACE( value[1] ) || ISSPACE( d[-1] ) ) {
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
						sort_rdn_avs( rdn_avs, rdn_av_count, 0 );
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
		if ( *s == '\\' ) {
			if ( gotesc ) { /* '\\', again */
				/* <type>=XXX\\\\7AYYY; we should keep \\\\. */
				gotesc = 0;
			} else {
				gotesc = 1;
				if ( s+2 < end ) {
					int n = hexchar2int( s[1] );
					if ( n >= 0 && n < 16 ) {
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
		} else {
			gotesc = 0;
		}
	}

	/*
	 * Track and sort attribute values within multivalued RDNs.
	 */
	/* We may still be in an unexpected state, such as B4TYPE if
	 * we encountered something odd like a '+' at the end of the
	 * rdn.  If this is the case, we don't want to add this bogus
	 * rdn to our list to sort.  We should only be in the INVALUE
	 * or B4SEPARATOR state if we have a valid rdn component to 
	 * be added. */
	if ((rdn_av_count > 0) && ((state == INVALUE1ST) || 
		(state == INVALUE) || (state == B4SEPARATOR))) {
		add_rdn_av( typestart, d, &rdn_av_count,
			&rdn_avs, initial_rdn_av_stack );
	}
	if ( rdn_av_count > 1 ) {
		sort_rdn_avs( rdn_avs, rdn_av_count, 0 );
	}
	if ( rdn_av_count > 0 ) {
		reset_rdn_avs( &rdn_avs, &rdn_av_count );
	}
	/* Trim trailing spaces */
	while ( d != dn && *(d - 1) == ' ' ) d--;  /* XXX 518524 */

	return( d );
}

char *
substr_dn_normalize( char *dn, char *end )
{
	/* no op */
	return end;
}

static int
ISEOV(char *s, char *ends)
{
    char *p;
    for (p = s; p && *p && p < ends; p++) {
        if (SEPARATOR(*p)) {
            return 1;
        } else if (!ISBLANK(*p)) {
            return 0; /* not the end of the value */
        }
    }
    return 1;
}

/*
 * 1) Escaped NEEDSESCAPE chars (e.g., ',', '<', '=', etc.) are converted to 
 * ESC HEX HEX (e.g., \2C, \3C, \3D, etc.)
 * Input could be a string in double quotes 
 * (= old DN format: dn: cn="x=x,y=y",... --> dn: cn=x\3Dx\2Cy\3Dy,...) or 
 * an escaped string 
 * (= new DN format dn: cn=x\=x\,y\=y,... -> dn: cn=x\3Dx\2Cy\3Dy,...)
 *
 * 2) All the other ESC HEX HEX are converted to the real characters.
 *
 * 3) Spaces around separator ',', ';', and '+' are removed.
 *
 * Input:
 * src: src DN
 * src_len: length of src; 0 is acceptable if src is NULL terminated.
 * Output:
 * dest: address of the converted string; NULL terminated
 *       (could store src address if no need to convert)
 * dest_len: length of dest
 *
 * Return values:
 *  0: nothing was done; dest is identical to src (src is passed in).
 *  1: successfully escaped; dest is different from src. src needs to be freed.
 * -1: failed; dest is NULL; invalid DN
 */
int
slapi_dn_normalize_ext(char *src, size_t src_len, char **dest, size_t *dest_len)
{
    int rc = -1;
    int state = B4TYPE;
    char *s = NULL; /* work pointer for src */
    char *d = NULL; /* work pointer for dest */
    char *ends = NULL;
    char *endd = NULL;
    char *lastesc = NULL;
    /* rdn avs for the main DN */
    char *typestart = NULL;
    int rdn_av_count = 0;
    struct berval *rdn_avs = NULL;
    struct berval initial_rdn_av_stack[ SLAPI_DNNORM_INITIAL_RDN_AVS ];
    /* rdn avs for the nested DN */
    char *subtypestart = NULL; /* used for nested rdn avs */
    int subrdn_av_count = 0;
    struct berval *subrdn_avs = NULL;
    struct berval subinitial_rdn_av_stack[ SLAPI_DNNORM_INITIAL_RDN_AVS ];
    int chkblank = 0;
    int avstat = 0;
    int is_dn_syntax = 0;

    if (NULL == dest) {
        goto bail;
    }
    if (NULL == src) {
        *dest = NULL;
        *dest_len = 0;
        goto bail;
    }
    if (0 == src_len) {
        src_len = strlen(src);
    }
    s = PL_strnchr(src, '\\', src_len);
    if (s) {
        *dest_len = src_len * 3;
        *dest = slapi_ch_malloc(*dest_len); /* max length */
        rc = 1;
    } else {
        s = PL_strnchr(src, '"', src_len);
        if (s) {
            *dest_len = src_len * 3;
            *dest = slapi_ch_malloc(*dest_len); /* max length */
            rc = 1;
        } else {
            *dest_len = src_len;
            *dest = src; /* just removing spaces around separators */
            rc = 0;
        }
    }

    ends = src + src_len;
    endd = *dest + *dest_len;
    for (s = src, d = *dest; s < ends && d < endd; ) {
        switch (state) {
        case B4TYPE: /* before type; cn=... */
                     /*             ^       */
            if (ISSPACE(*s)) {
                s++; /* skip leading spaces */
            } else {
                state = INTYPE;
                typestart = d;
                *d++ = *s++;
            }
            break;
        case INTYPE: /* in type; cn=... */
                     /*          ^      */
            if (ISEQUAL(*s)) {
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;
                Slapi_Attr test_attr;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0'; 

                slapi_attr_init(&test_attr, typestart);
                is_dn_syntax = slapi_attr_is_dn_syntax_attr(&test_attr);
                attr_done(&test_attr);

                /* Reset the character we modified. */
                *d = savechar;

                state = B4VALUE;
                *d++ = *s++;
            } else if (ISCLOSEBRACKET(*s)) { /* special care for ACL macro */
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;
                Slapi_Attr test_attr;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0';

                slapi_attr_init(&test_attr, typestart);
                is_dn_syntax = slapi_attr_is_dn_syntax_attr(&test_attr);
                attr_done(&test_attr);

                /* Reset the character we modified. */
                *d = savechar;

                state = INVALUE; /* skip a trailing space */
                *d++ = *s++;
            } else if (ISSPACE(*s)) {
                /* See if the type is defined to use
                 * the Distinguished Name syntax. */
                char savechar;
                Slapi_Attr test_attr;

                /* We need typestart to be a string containing only
                 * the type.  We terminate the type and then reset
                 * the string after we check the syntax. */
                savechar = *d;
                *d = '\0';

                slapi_attr_init(&test_attr, typestart);
                is_dn_syntax = slapi_attr_is_dn_syntax_attr(&test_attr);
                attr_done(&test_attr);

                /* Reset the character we modified. */
                *d = savechar;

                state = B4EQUAL; /* skip a trailing space */
            } else {
                *d++ = *s++;
            }
            break;
        case B4EQUAL: /* before equal; cn =... */
                      /*                 ^     */
            if (ISEQUAL(*s)) {
                state = B4VALUE;
                *d++ = *s++;
            } else if (ISSPACE(*s)) {
                s++; /* skip trailing spaces */
            } else {
                /* type includes spaces; not a valid dn */
                rc = -1;
                goto bail;
            }
            break;
        case B4VALUE: /* before value; cn= ABC */
                      /*                  ^    */
            if (ISSPACE(*s)) {
                s++;
            } else {
                if (ISQUOTE(*s)) {
                    s++; /* start with the first char in quotes */
                    state = INQUOTEDVALUE1ST;
                } else {
                    state = INVALUE1ST;
                }
                lastesc = NULL;
                /* process *s in INVALUE or INQUOTEDVALUE */
            }
            break;
        case INVALUE1ST: /* 1st char in value; cn=ABC */
                         /*                       ^   */
            if (ISSPACE(*s)) { /* skip leading spaces */
                s++;
                continue;
            } else if (SEPARATOR(*s)) {
                /* 1st char in value is separator; invalid dn */
                rc = -1;
                goto bail;
            } /* otherwise, go through */
            if (!is_dn_syntax || ISESCAPE(*s)) {
                subtypestart = NULL; /* if escaped, can't be multivalued dn */
            } else {
                subtypestart = d; /* prepare for '+' in the nested DN, if any */
            }
            subrdn_av_count = 0;
        case INVALUE:    /* in value; cn=ABC */
                         /*               ^  */
            if (ISESCAPE(*s)) {
                if (s + 1 >= ends) {
                    /* DN ends with '\'; invalid dn */
                    rc = -1;
                    goto bail;
                }
                if (((state == INVALUE1ST) && LEADNEEDSESCAPE(*(s+1))) ||
                           ((state == INVALUE) && NEEDSESCAPE(*(s+1)))) {
                    if (d + 2 >= endd) {
                        /* Not enough space for dest; this never happens! */
                        rc = -1;
                        goto bail;
                    } else {
                        if (ISEQUAL(*(s+1)) && is_dn_syntax) {
                            while (ISSPACE(*(d-1))) {
                                /* remove trailing spaces */
                                d--;
                            }
                        } else if (SEPARATOR(*(s+1)) && is_dn_syntax) {
                            /* separator is a subset of needsescape */
                            while (ISSPACE(*(d-1))) {
                                /* remove trailing spaces */
                                d--;
                                chkblank = 1;
                            }
                            if (chkblank && ISESCAPE(*(d-1)) && ISBLANK(*d)) {
                                /* last space is escaped "cn=A\ ,ou=..." */
                                /*                             ^         */
                                PR_snprintf(d, 3, "%X", *d);    /* hexpair */
                                d += 2;
                                chkblank = 0;
                            }
                            /*
                             * Track and sort attribute values within
                             * multivalued RDNs.
                             */
                            if (subtypestart &&
                                (ISPLUS(*(s+1)) || subrdn_av_count > 0)) {
                                 /* if subtypestart is not valid DN,
                                  * we do not do sorting.*/
                                 char *p = PL_strcasestr(subtypestart, "\\3d");
                                 if (MAYBEDN(p)) {
                                     add_rdn_av(subtypestart, d, 
                                               &subrdn_av_count,
                                               &subrdn_avs, 
                                               subinitial_rdn_av_stack);
                                 } else {
                                     reset_rdn_avs(&subrdn_avs, 
                                                   &subrdn_av_count);
                                     subtypestart = NULL;
                                 }
                            }
                            if (!ISPLUS(*(s+1))) {    /* at end of this RDN */
                                if (subrdn_av_count > 1) {
                                    sort_rdn_avs( subrdn_avs, 
                                                  subrdn_av_count, 1 );
                                }
                                if (subrdn_av_count > 0) {
                                    reset_rdn_avs( &subrdn_avs,
                                                   &subrdn_av_count );
                                    subtypestart = NULL;
                                }
                            }
                        }
                        /* dn: cn=x\=x\,... -> dn: cn=x\3Dx\2C,... */
                        *d++ = *s++;            /* '\\' */
                        PR_snprintf(d, 3, "%X", *s);    /* hexpair */
                        d += 2;
                        if (ISPLUS(*s) && is_dn_syntax) {
                            /* next type start of multi values */
                            /* should not be a escape char AND should be 
                             * followed by \\= or \\3D */
                            if ((PL_strnstr(s, "\\=", ends - s) ||
                                 PL_strncaserstr(s, "\\3D", ends - s))) {
                                subtypestart = d;
                            } else {
                                subtypestart = NULL;
                            }
                        }
                        if ((SEPARATOR(*s) || ISEQUAL(*s)) && is_dn_syntax) {
                            while (ISSPACE(*(s+1)))
                                s++; /* remove leading spaces */
                            s++;
                        } else {
                            s++;
                        }
                    }
                } else if (((state == INVALUE1ST) &&
                            (s+2 < ends) && LEADNEEDSESCAPESTR(s+1)) ||
                           ((state == INVALUE) && 
                            (((s+2 < ends) && NEEDSESCAPESTR(s+1)) ||
                             (ISEOV(s+3, ends) && ISBLANKSTR(s+1))))) {
                             /* e.g., cn=abc\20 ,... */
                             /*             ^        */
                    if (ISEQUALSTR(s+1) && is_dn_syntax) {
                        while (ISSPACE(*(d-1))) {
                            /* remove trailing spaces */
                            d--;
                        }
                    } else if (SEPARATORSTR(s+1) && is_dn_syntax) {
                        /* separator is a subset of needsescape */
                        while (ISSPACE(*(d-1))) {
                            /* remove trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        if (chkblank && ISESCAPE(*(d-1)) && ISBLANK(*d)) {
                            /* last space is escaped "cn=A\ ,ou=..." */
                            /*                             ^         */
                            PR_snprintf(d, 3, "%X", *d);    /* hexpair */
                            d += 2;
                            chkblank = 0;
                        }
                        /*
                         * Track and sort attribute values within
                         * multivalued RDNs.
                         */
                        if (subtypestart &&
                            (ISPLUSSTR(s+1) || subrdn_av_count > 0)) {
                            /* if subtypestart is not valid DN,
                             * we do not do sorting.*/
                            char *p = PL_strcasestr(subtypestart, "\\3d");
                            if (MAYBEDN(p)) {
                                add_rdn_av(subtypestart, d, &subrdn_av_count,
                                       &subrdn_avs, subinitial_rdn_av_stack);
                            } else {
                                reset_rdn_avs( &subrdn_avs, &subrdn_av_count );
                                subtypestart = NULL;
                            }
                        }
                        if (!ISPLUSSTR(s+1)) {    /* at end of this RDN */
                            if (subrdn_av_count > 1) {
                                sort_rdn_avs( subrdn_avs, subrdn_av_count, 1 );
                            }
                            if (subrdn_av_count > 0) {
                                reset_rdn_avs( &subrdn_avs, &subrdn_av_count );
                                subtypestart = NULL;
                            }
                        }
                    }
                    *d++ = *s++;            /* '\\' */
                    *d++ = *s++;            /* HEX */
                    *d++ = *s++;            /* HEX */
                    if (ISPLUSSTR(s-2) && is_dn_syntax) {
                        /* next type start of multi values */
                        /* should not be a escape char AND should be followed
                         * by \\= or \\3D */
                        if (!ISESCAPE(*s) && (PL_strnstr(s, "\\=", ends - s) ||
                            PL_strncaserstr(s, "\\3D", ends - s))) {
                            subtypestart = d;
                        } else {
                            subtypestart = NULL;
                        }
                    }
                    if ((SEPARATORSTR(s-2) || ISEQUALSTR(s-2)) && is_dn_syntax) {
                        while (ISSPACE(*s)) {/* remove leading spaces */
                            s++;
                        }
                    }
                } else if (s + 2 < ends &&
                           isxdigit(*(s+1)) && isxdigit(*(s+2))) {
                    /* esc hexpair ==> real character */
                    int n = hexchar2int(*(s+1));
                    int n2 = hexchar2int(*(s+2));
                    n = (n << 4) + n2;
                    if (n == 0) { /* don't change \00 */
                        *d++ = *++s;
                        *d++ = *++s;
                    } else {
                        *d++ = n;
                        s += 3;
                    }
                } else {
                    /* ignore an escape for now */
                    lastesc = d; /* position of the previous escape */
                    s++;
                }
            } else if (SEPARATOR(*s)) { /* cn=ABC , ... */
                                        /*        ^     */
                /* handling a trailing escaped space */
                /* assuming a space is the only an extra character which
                 * is not escaped if it appears in the middle, but should
                 * be if it does at the end of the RDN value */
                /* e.g., ou=ABC  \   ,o=XYZ --> ou=ABC  \ ,o=XYZ */
                if (lastesc) {
                    while (ISSPACE(*(d-1)) && d > lastesc ) {
                        d--;
                    }
                    if (d == lastesc) {
                        /* esc hexpair of space: \20 */
                        *d++ = '\\';
                        *d++ = '2';
                        *d++ = '0';
                    }
                } else {
                    while (ISSPACE(*(d-1))) {
                        d--;
                    }
                }
                state = B4SEPARATOR;
                break;
            } else { /* else if (SEPARATOR(*s)) */
                *d++ = *s++;
            }
            if (state == INVALUE1ST) {
                state = INVALUE;
            }
            break;
        case INQUOTEDVALUE1ST:
            if (ISSPACE(*s) && (s+1 < ends && ISSPACE(*(s+1)))) {
                /* skip leading spaces but need to leave one */
                s++;
                continue;
            }
            if (is_dn_syntax) {
                subtypestart = d; /* prepare for '+' in the quoted value, if any */
            }
            subrdn_av_count = 0;
        case INQUOTEDVALUE:
            if (ISQUOTE(*s)) {
                if (ISESCAPE(*(d-1))) { /* the quote is escaped */
                    PR_snprintf(d, 3, "%X", *(s++));    /* hexpair */
                } else { /* end of INQUOTEVALUE */
                    if (is_dn_syntax) {
                        while (ISSPACE(*(d-1))) { /* eliminate trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        /* We have to keep the last ' ' of a value in quotes.
                         * The same idea as the escaped last space:
                         * "cn=A,ou=B " */
                        /*           ^  */
                        if (chkblank && ISBLANK(*d)) {
                            PR_snprintf(d, 4, "\\%X", *d);    /* hexpair */
                            d += 3;
                            chkblank = 0;
                        }
                    } else if (ISSPACE(*(d-1))) {
                        /* Convert last trailing space to hex code */
                        d--;
                        PR_snprintf(d, 4, "\\%X", *d);    /* hexpair */
                        d += 3;
                    }

                    state = B4SEPARATOR;
                    s++;
                }
            } else if (((state == INQUOTEDVALUE1ST) && LEADNEEDSESCAPE(*s)) || 
                        (state == INQUOTEDVALUE && NEEDSESCAPE(*s))) {
                if (d + 2 >= endd) {
                    /* Not enough space for dest; this never happens! */
                    rc = -1;
                    goto bail;
                } else {
                    if (ISEQUAL(*s) && is_dn_syntax) {
                        while (ISSPACE(*(d-1))) { /* remove trailing spaces */
                            d--;
                        }
                    } else if (SEPARATOR(*s) && is_dn_syntax) {
                        /* separator is a subset of needsescape */
                        while (ISSPACE(*(d-1))) { /* remove trailing spaces */
                            d--;
                            chkblank = 1;
                        }
                        /* We have to keep the last ' ' of a value in quotes.
                         * The same idea as the escaped last space:
                         * "cn=A\ ,ou=..." */
                        /*       ^         */
                        if (chkblank && ISBLANK(*d)) {
                            PR_snprintf(d, 4, "\\%X", *d);    /* hexpair */
                            d += 3;
                            chkblank = 0;
                        }
                        /*
                         * Track and sort attribute values within
                         * multivalued RDNs.
                         */
                        if (subtypestart &&
                            (ISPLUS(*s) || subrdn_av_count > 0)) {
                            /* if subtypestart is not valid DN,
                             * we do not do sorting.*/
                            char *p = PL_strcasestr(subtypestart, "\\3d");
                            if (MAYBEDN(p)) {
                                add_rdn_av(subtypestart, d, &subrdn_av_count,
                                       &subrdn_avs, subinitial_rdn_av_stack);
                            } else {
                                reset_rdn_avs( &subrdn_avs, &subrdn_av_count );
                                subtypestart = NULL;
                            }
                        }
                        if (!ISPLUS(*s)) {    /* at end of this RDN */
                            if (subrdn_av_count > 1) {
                                sort_rdn_avs( subrdn_avs, subrdn_av_count, 1 );
                            }
                            if (subrdn_av_count > 0) {
                                reset_rdn_avs( &subrdn_avs, &subrdn_av_count );
                                subtypestart = NULL;
                            }
                        }
                    }
                    
                    /* dn: cn="x=x,..",... -> dn: cn=x\3Dx\2C,... */
                    *d++ = '\\';
                    PR_snprintf(d, 3, "%X", *s);    /* hexpair */
                    d += 2;
                    if (ISPLUS(*s++) && is_dn_syntax) {
                        subtypestart = d; /* next type start of multi values */
                    }
                    if ((SEPARATOR(*(s-1)) || ISEQUAL(*(s-1))) && is_dn_syntax) {
                        while (ISSPACE(*s)) /* remove leading spaces */
                            s++;
                    }
                }
            } else {
                *d++ = *s++;
            }
            if (state == INQUOTEDVALUE1ST) {
                state = INQUOTEDVALUE;
            }
            break;
        case B4SEPARATOR:
            if (SEPARATOR(*s)) {
                state = B4TYPE;

                /*
                 * Track and sort attribute values within
                 * multivalued RDNs.
                 */
                if (typestart &&
                    (ISPLUS(*s) || rdn_av_count > 0)) {
                    add_rdn_av(typestart, d, &rdn_av_count,
                               &rdn_avs, initial_rdn_av_stack);
                }
                /* Sub type sorting might be also ongoing */
                if (subtypestart && subrdn_av_count > 0) {
                    add_rdn_av(subtypestart, d, &subrdn_av_count,
                               &subrdn_avs, subinitial_rdn_av_stack);
                }
                if (!ISPLUS(*s)) {    /* at end of this RDN */
                    if (rdn_av_count > 1) {
                        sort_rdn_avs( rdn_avs, rdn_av_count, 0 );
                    }
                    if (rdn_av_count > 0) {
                        reset_rdn_avs( &rdn_avs, &rdn_av_count );
                        typestart = NULL;
                    }
                    /* If in the middle of sub type sorting, finish it. */
                    if (subrdn_av_count > 1) {
                        sort_rdn_avs( subrdn_avs, subrdn_av_count, 1 );
                    }
                    if (subrdn_av_count > 0) {
                        reset_rdn_avs( &subrdn_avs, &subrdn_av_count );
                        subtypestart = NULL;
                    }
                }

                *d++ = (ISPLUS(*s++)) ? '+' : ',';
            } else {
                s++;
            }
            break;
        default:
            LDAPDebug( LDAP_DEBUG_ANY,
                "slapi_dn_normalize - unknown state %d\n", state, 0, 0 );
            break;
        }
    }

    /*
     * Track and sort attribute values within multivalued RDNs.
     */
    /* We may still be in an unexpected state, such as B4TYPE if
     * we encountered something odd like a '+' at the end of the
     * rdn.  If this is the case, we don't want to add this bogus
     * rdn to our list to sort.  We should only be in the INVALUE
     * or B4SEPARATOR state if we have a valid rdn component to 
     * be added. */
    if (typestart && (rdn_av_count > 0) && ((state == INVALUE1ST) || 
        (state == INVALUE) || (state == B4SEPARATOR))) {
        add_rdn_av(typestart, d, &rdn_av_count, &rdn_avs, initial_rdn_av_stack);
    }
    if ( rdn_av_count > 1 ) {
        sort_rdn_avs( rdn_avs, rdn_av_count, 0 );
    }
    if ( rdn_av_count > 0 ) {
        reset_rdn_avs( &rdn_avs, &rdn_av_count );
    }
    /* Trim trailing spaces */
    while (d > *dest && ISBLANK(*(d-1))) {
        --d;  /* XXX 518524 */
    }
    *dest_len = d - *dest;
bail:
    if (rc < 0) {
        if (dest != NULL) {
            if (*dest != src) {
                slapi_ch_free_string(dest);
            } else {
                *dest = NULL;
            }
        }
        *dest_len = 0;
    } else if (rc > 0) {
        /* We terminate the str with NULL only when we allocate the str */
        *d = '\0';
    }
    return rc;
}

char *
slapi_create_dn_string(const char *fmt, ...)
{
    char *src = NULL;
    char *dest = NULL;
    size_t dest_len = 0;
    va_list ap;
    int rc = 0;

    if (NULL == fmt) {
        return NULL;
    }

    va_start(ap, fmt);
    src = PR_vsmprintf(fmt, ap);
    va_end(ap);

    rc = slapi_dn_normalize_ext(src, strlen(src), &dest, &dest_len);
    if (rc < 0) {
        slapi_ch_free_string(&src);
        return NULL;
    } else if (rc == 0) { /* src is passed in. */
        *(dest + dest_len) = '\0';
    } else {
        slapi_ch_free_string(&src);
    }
    return dest;
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
        struct berval *tmpavs;

        tmpavs = (struct berval *)slapi_ch_calloc(
                SLAPI_DNNORM_INITIAL_RDN_AVS * 2, sizeof( struct berval ));
        memcpy( tmpavs, *rdn_avsp,
                SLAPI_DNNORM_INITIAL_RDN_AVS * sizeof( struct berval ));
        *rdn_avsp = tmpavs;
    } else if (( *rdn_av_countp % SLAPI_DNNORM_INITIAL_RDN_AVS ) == 0 ) {
        *rdn_avsp = (struct berval *)slapi_ch_realloc( (char *)*rdn_avsp,
                    (*rdn_av_countp +
                     SLAPI_DNNORM_INITIAL_RDN_AVS)*sizeof(struct berval) );
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
sort_rdn_avs( struct berval *avs, int count, int escape )
{
    int i, j, swaps;

    /*
     * Since we expect there to be a small number of AVs, we use a
     * simple bubble sort.  rdn_av_swap() only works correctly on
     * adjacent values anyway.
     */
    for ( i = 0; i < count - 1; ++i ) {
        swaps = 0;
        for ( j = 0; j < count - 1; ++j ) {
            if ( rdn_av_cmp( &avs[j], &avs[j+1] ) > 0 ) {
                rdn_av_swap( &avs[j], &avs[j+1], escape );
                ++swaps;
            }
        }
        if ( swaps == 0 ) {
            break;        /* stop early if no swaps made during the last pass */
        }
    } 
}


/*
 * strcasecmp()-like function for RDN attribute values.
 */
static int
rdn_av_cmp( struct berval *av1, struct berval *av2 )
{
    int rc;

    rc = strncasecmp( av1->bv_val, av2->bv_val,
            ( av1->bv_len < av2->bv_len ) ? av1->bv_len : av2->bv_len );

    if ( rc == 0 ) {
        return( av1->bv_len - av2->bv_len );        /* longer is greater */
    } else {
        return( rc );
    }
}


/*
 * Swap two adjacent attribute=value pieces within an (R)DN.
 * Avoid allocating any heap memory for reasonably small AVs.
 */
static void
rdn_av_swap( struct berval *av1, struct berval *av2, int escape )
{
    char    *buf1, *buf2;
    char    stackbuf1[ SLAPI_DNNORM_SMALL_RDN_AV ];
    char    stackbuf2[ SLAPI_DNNORM_SMALL_RDN_AV ];
    int     len1, len2;

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
    if (escape) {
        *(av2->bv_val)++ = '\\';
        PR_snprintf(av2->bv_val, 3, "%X", '+');    /* hexpair */
        av2->bv_val += 2;
    } else {
        *(av2->bv_val)++ = '+';
    }
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
 * DEPRECATED: this function does nothing.
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

/* Introduced for the upgrade tool. DON'T USE THIS API! */
char *
slapi_dn_normalize_original( char *dn )
{
	/* LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
	*(substr_dn_normalize_orig( dn, dn + strlen( dn ))) = '\0';
	/* LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */

	return( dn );
}

/* Introduced for the upgrade tool. DON'T USE THIS API! */
char *
slapi_dn_normalize_case_original( char *dn )
{
	/* LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */
	*(substr_dn_normalize_orig( dn, dn + strlen( dn ))) = '\0';
	/* LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_dn_normalize \"%s\"\n", dn, 0, 0 ); */

	/* normalize case */
	return( slapi_dn_ignore_case( dn ));
}

/*
 * DEPRECATED: this function does nothing.
 * Note that this routine  normalizes to the end and doesn't null terminate
 */
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
    unsigned char *s = NULL, *d = NULL;
    int ssz, dsz;
    /* normalize case (including UTF-8 multi-byte chars) */
    for ( s = d = (unsigned char *)dn; s && *s; s += ssz, d += dsz ) {
        slapi_utf8ToLower( s, d, &ssz, &dsz );
    }
    if (d) {
        *d = '\0'; /* utf8ToLower result may be shorter than the original */
    }
    return( dn );
}

char *
dn_ignore_case_to_end( char *dn, char *end )
{
    unsigned char *s = NULL, *d = NULL;
    int ssz, dsz;
    /* normalize case (including UTF-8 multi-byte chars) */
    for (s = d = (unsigned char *)dn; s && s < (unsigned char *)end && *s;
         s += ssz, d += dsz) {
        slapi_utf8ToLower( s, d, &ssz, &dsz );
    }
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

int
slapi_dn_normalize_case_ext(char *src, size_t src_len, 
                            char **dest, size_t *dest_len)
{
    int rc = slapi_dn_normalize_ext(src, src_len, dest, dest_len);

    if (rc >= 0) {
        dn_ignore_case_to_end(*dest, *dest + *dest_len);
    }
    return rc;
}

char *
slapi_create_dn_string_case(const char *fmt, ...)
{
    char *src = NULL;
    char *dest = NULL;
    size_t dest_len = 0;
    va_list ap;
    int rc = 0;

    if (NULL == fmt) {
        return NULL;
    }

    va_start(ap, fmt);
    src = PR_vsmprintf(fmt, ap);
    va_end(ap);

    rc = slapi_dn_normalize_ext(src, strlen(src), &dest, &dest_len);
    if (rc < 0) {
        slapi_ch_free_string(&src);
    } else if (rc == 0) { /* src is passed in. */
        *(dest + dest_len) = '\0';
    } else {
        slapi_ch_free_string(&src);
    }

    return slapi_dn_ignore_case(dest);
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

/*
 * This function is used for speed.  Instead of returning a newly allocated
 * dn string that contains the parent, this function just returns a pointer
 * to the address _within_ the given string where the parent dn of the
 * given dn starts e.g. if you call this with "dc=example,dc=com", the
 * function will return "dc=com" - that is, the char* returned will be the
 * address of the 'd' after the ',' in "dc=example,dc=com".  This function
 * also checks for bogus things like consecutive ocurrances of unquoted
 * separators e.g. DNs like cn=foo,,,,,,,,,,,cn=bar,,,,,,,
 * This function is useful for "interating" over a DN returning the ancestors
 * of the given dn e.g.
 *
 * const char *dn = somedn;
 * while (dn = slapi_dn_find_parent(dn)) {
 *   see if parent exists
 *   etc.
 * }
 */
const char*
slapi_dn_find_parent( const char *dn )
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
			else {
                if ( DNSEPARATOR( *s ) ) {
                    while ( *s && DNSEPARATOR( *s ) ) {
                        ++s;
                    }
                    if (*s) {
                        return( s );
                    }
                }
            }
		}
	}

	return( NULL );
}

char*
slapi_dn_parent( const char *dn )
{
	const char *s = slapi_dn_find_parent(dn);

	if ( s == NULL || *s == '\0' ) {
		return( NULL );
	}

    return( slapi_ch_strdup( s ) );
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
    strcpy_unescape_value(s,s);

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
	char *newdn = slapi_ch_smprintf("%s,%s", rdn, dn);
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
	if (ndn) {
		sdn->ndn_len = strlen(ndn);
	} else {
		sdn->ndn_len = 0;
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
		char *newdn = slapi_ch_smprintf("%s,%s", rawrdn, parentdn);
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
    if(sdn==NULL)
    {
        return;
    }
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
            char *normed = NULL;
            size_t dnlen = 0;
            int rc = 0;

            Slapi_DN *ncsdn= (Slapi_DN*)sdn; /* non-const Slapi_DN */
            rc = slapi_dn_normalize_case_ext(p, 0, &normed, &dnlen);
            if (rc < 0) {
                /* we give up, just set dn to ndn */
                slapi_dn_ignore_case(p); /* ignore case */
                ncsdn->ndn = p;
                ncsdn->ndn_len = strlen(p);
            } else if (rc == 0) { /* p is passed in */
                *(normed + dnlen) = '\0';
                ncsdn->ndn = normed;
                ncsdn->ndn_len = dnlen;
            } else { /* rc > 0 */
                slapi_ch_free_string(&p);
                ncsdn->ndn = normed;
                ncsdn->ndn_len = dnlen;
            }
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
	(void)slapi_sdn_get_ndn(sdn); /* does the normalization if needed */
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
