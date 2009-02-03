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

/* phonetic.c - routines to do phonetic matching */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include "syntax.h"
#include "portable.h"

#if !defined(METAPHONE) && !defined(SOUNDEX)
#define METAPHONE
#endif

#define iswordbreak(s) \
(isascii(*(s)) \
? (isspace(*(s)) || \
   ispunct(*(s)) || \
   isdigit(*(s)) || \
   *(s) == '\0') \
: utf8iswordbreak(s))

static int
utf8iswordbreak( const char* s )
{
    switch( LDAP_UTF8GETCC( s )) {
      case 0x00A0: /* non-breaking space */
      case 0x3000: /* ideographic space */
      case 0xFEFF: /* zero-width non-breaking space */
        return 1;
      default: break;
    }
    return 0;
}

char *
first_word( char *s )
{
        if ( s == NULL ) {
                return( NULL );
        }

        while ( iswordbreak( s ) ) {
                if ( *s == '\0' ) {
                        return( NULL );
                } else {
                        LDAP_UTF8INC( s );
                }
        }

        return( s );
}

char *
next_word( char *s )
{
        if ( s == NULL ) {
                return( NULL );
        }

        while ( ! iswordbreak( s ) ) {
                LDAP_UTF8INC( s );
        }

        while ( iswordbreak( s ) ) {
                if ( *s == '\0' ) {
                        return( NULL );
                } else {
                        LDAP_UTF8INC( s );
                }
        }

        return( s );
}

char *
word_dup( char *w )
{
        char        *s, *ret;
        char        save;

        for ( s = w; !iswordbreak( s ); LDAP_UTF8INC( s ))
                ;        /* NULL */
        save = *s;
        *s = '\0';
        ret = slapi_ch_strdup( w );
        *s = save;

        return( ret );
}

#ifndef MAXPHONEMELEN
#define MAXPHONEMELEN        6
#endif

#if defined(SOUNDEX)

/* lifted from isode-8.0 */
char *
phonetic( char *s )
{
        char        code, adjacent, ch;
        char        *p;
        char        **c;
        int        i, cmax;
        char        phoneme[MAXPHONEMELEN + 1];

        p = s;
        if (  p == NULL || *p == '\0' ) {
                return( NULL );
        }

        adjacent = '0';
        phoneme[0] = TOUPPER(*p);

        phoneme[1]  = '\0';
        for ( i = 0; i < 99 && (! iswordbreak(p)); LDAP_UTF8INC( p )) {
                ch = TOUPPER (*p);

                code = '0';

                switch (ch) {
                case 'B':
                case 'F':
                case 'P':
                case 'V':
                        code = (adjacent != '1') ? '1' : '0';
                        break;
                case 'S':
                case 'C':
                case 'G':
                case 'J':
                case 'K':
                case 'Q':
                case 'X':
                case 'Z':
                        code = (adjacent != '2') ? '2' : '0';
                        break;
                case 'D':
                case 'T':
                        code = (adjacent != '3') ? '3' : '0';
                        break;
                case 'L':
                        code = (adjacent != '4') ? '4' : '0';
                        break;
                case 'M':
                case 'N':
                        code = (adjacent != '5') ? '5' : '0';
                        break;
                case 'R':
                        code = (adjacent != '6') ? '6' : '0';
                        break;
                default:
                        adjacent = '0';
                }

                if ( i == 0 ) {
                        adjacent = code;
                        i++;
                } else if ( code != '0' ) {
                        if ( i == MAXPHONEMELEN )
                                break;
                        adjacent = phoneme[i] = code;
                        i++;
                }
        }

        if ( i > 0 )
                phoneme[i] = '\0';

        return( slapi_ch_strdup( phoneme ) );
}

#else
#if defined(METAPHONE)

/*
 * Metaphone copied from C Gazette, June/July 1991, pp 56-57,
 * author Gary A. Parker, with changes by Bernard Tiffany of the
 * University of Michigan, and more changes by Tim Howes of the
 * University of Michigan.
 */

/* Character coding array */
static char     vsvfn[26] = {
           1, 16, 4, 16, 9, 2, 4, 16, 9, 2, 0, 2, 2,
        /* A   B  C   D  E  F  G   H  I  J  K  L  M  */
           2, 1, 4, 0, 2, 4, 4, 1, 0, 0, 0, 8, 0};
        /* N  O  P  Q  R  S  T  U  V  W  X  Y  Z  */

/* Macros to access character coding array */
#define vowel(x)     ((*(x) != '\0' && vsvfn[(*(x)) - 'A'] & 1) || /* AEIOU */ \
   (((*(x)==0xC3) && (*((x)+1))) ?       ((0x80<=*((x)+1) && *((x)+1)<0x87) || \
     (0x88<=*((x)+1) && *((x)+1)<0x90) || (0x92<=*((x)+1) && *((x)+1)<0x97) || \
     (0x98<=*((x)+1) && *((x)+1)<0x9D) || (0xA0<=*((x)+1) && *((x)+1)<0xA7) || \
     (0xA8<=*((x)+1) && *((x)+1)<0xB0) || (0xB2<=*((x)+1) && *((x)+1)<0xB7) || \
     (0xB8<=*((x)+1) && *((x)+1)<0xBD)) : 0 ) /* Latin-1 characters */ )
/*
    case 0xC3:
*/
#define same(x)         ((x) != '\0' && vsvfn[(x) - 'A'] & 2)        /* FJLMNR */
#define varson(x)       ((x) != '\0' && vsvfn[(x) - 'A'] & 4)        /* CGPST */
#define frontv(x)   ((*(x) != '\0' && vsvfn[(*(x)) - 'A'] & 8) ||    /* EIY */ \
   (((*(x)==0xC3) && (*((x)+1))) ?       ((0x88<=*((x)+1) && *((x)+1)<0x90) || \
     (0xA8<=*((x)+1) && *((x)+1)<0xB0)) : 0 ) /* Latin-1 E/I */ )
#define noghf(x)        ((x) != '\0' && vsvfn[(x) - 'A'] & 16)        /* BDH */

char *
phonetic( char *Word )
{
    unsigned char   *n, *n_start, *n_end;        /* pointers to string */
    char            *metaph_end;        /* pointers to metaph */
    char            ntrans[42];        /* word with uppercase letters */
    int             KSflag;        /* state flag for X -> KS */
    char                buf[MAXPHONEMELEN + 2];
    char                *Metaph;

    /*
     * Copy Word to internal buffer, dropping non-alphabetic characters
     * and converting to upper case
     */
    n = ntrans + 4; n_end = ntrans + 35;
    while (!iswordbreak( Word ) && n < n_end) {
        if (isascii(*Word)) {
            if (isalpha(*Word)) {
                *n++ = TOUPPER(*Word);
            }
            ++Word;
        } else {
            auto const size_t len = LDAP_UTF8COPY(n, Word);
            n += len; Word += len;
        }
    }
    Metaph = buf;
    *Metaph = '\0';
    if (n == ntrans + 4) {
            return( slapi_ch_strdup( buf ) );                /* Return if null */
    }
    n_end = n;                /* Set n_end to end of string */

    /* ntrans[0] will always be == 0 */
    ntrans[0] = '\0';
    ntrans[1] = '\0';
    ntrans[2] = '\0';
    ntrans[3] = '\0';
    *n++ = 0;
    *n++ = 0;
    *n++ = 0;
    *n = 0;                        /* Pad with nulls */
    n = ntrans + 4;                /* Assign pointer to start */

    /* Check for PN, KN, GN, AE, WR, WH, and X at start */
    switch (*n) {
    case 'P':
    case 'K':
    case 'G':
        /* 'PN', 'KN', 'GN' becomes 'N' */
        if (*(n + 1) == 'N')
            *n++ = 0;
        break;
    case 'A':
        /* 'AE' becomes 'E' */
        if (*(n + 1) == 'E')
            *n++ = 0;
        break;
    case 'W':
        /* 'WR' becomes 'R', and 'WH' to 'H' */
        if (*(n + 1) == 'R')
            *n++ = 0;
        else if (*(n + 1) == 'H') {
            *n++ = 0;
        }
        break;
    case 'X':
        /* 'X' becomes 'S' */
        *n = 'S';
        break;
    case 0xC3:
        switch (*(n+1)) {
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
            *n++ = 0;
            *n = 'A';
            break;
        case 0x87:
            *n++ = 0;
            *n = 'C';
            break;
        case 0x86:
        case 0x88:
        case 0x89:
        case 0x8A:
        case 0x8B:
            *n++ = 0;
            *n = 'E';
            break;
        case 0x8C:
        case 0x8D:
        case 0x8E:
        case 0x8F:
            *n++ = 0;
            *n = 'I';
            break;
        case 0x90:    /* eth: TH */
            *n++ = 0;
            *n = '0';
            break;
        case 0x91:
            *n++ = 0;
            *n = 'N';
            break;
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x98:
            *n++ = 0;
            *n = 'O';
            break;
        case 0x99:
        case 0x9A:
        case 0x9B:
        case 0x9C:
            *n++ = 0;
            *n = 'U';
            break;
        case 0x9D:
            *n++ = 0;
            *n = 'Y';
            break;
        case 0x9E:
            *n++ = 0;
            *n = '0';    /* thorn: TH */
            break;
        case 0x9F:
            *n++ = 0;
            *n = 's';
            break;
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
        case 0xA4:
        case 0xA5:
            *n++ = 0;
            *n = 'a';
            break;
        case 0xA6:
            *n++ = 0;
            *n = 'e';
            break;
        case 0xA7:
            *n++ = 0;
            *n = 'c';
            break;
        case 0xA8:
        case 0xA9:
        case 0xAA:
        case 0xAB:
            *n++ = 0;
            *n = 'e';
            break;
        case 0xAC:
        case 0xAD:
        case 0xAE:
        case 0xAF:
            *n++ = 0;
            *n = 'i';
            break;
        case 0xB0:
            *n++ = 0;
            *n = '0';    /* eth: th */
            break;
        case 0xB1:
            *n++ = 0;
            *n = 'n';
            break;
        case 0xB2:
        case 0xB3:
        case 0xB4:
        case 0xB5:
        case 0xB6:
        case 0xB8:
            *n++ = 0;
            *n = 'o';
            break;
        case 0xB9:
        case 0xBA:
        case 0xBB:
        case 0xBC:
            *n++ = 0;
            *n = 'u';
            break;
        case 0xBD:
        case 0xBF:
            *n++ = 0;
            *n = 'y';
            break;
        case 0xBE:
            *n++ = 0;
            *n = '0';    /* thorn: th */
            break;
        }
        break;
    }

    /*
     * Now, loop step through string, stopping at end of string or when
     * the computed 'metaph' is MAXPHONEMELEN characters long
     */

    KSflag = 0;                /* state flag for KS translation */
    for (metaph_end = Metaph + MAXPHONEMELEN, n_start = n;
         n <= n_end && Metaph < metaph_end; n++) {
        if (KSflag) {
            KSflag = 0;
            *Metaph++ = 'S';
        } else if (!isascii(*n)) {
            switch (*n) {
            case 0xC3:
                if (n+1 <= n_end) {
                    switch (*(++n)) {
                    case 0x87:    /* C with cedilla */
                    case 0x9F:    /* ess-zed */
                    case 0xA7:    /* c with cedilla */
                        *Metaph++ = 'S';
                        break;
                    case 0x90:    /* eth: TH */
                    case 0x9E:    /* thorn: TH */
                    case 0xB0:    /* eth: th */
                    case 0xBE:    /* thorn: th */
                        *Metaph++ = '0';
                        break;
                    case 0x91:
                    case 0xB1:
                        *Metaph++ = 'N';
                        break;
                    case 0x9D:
                    case 0xBD:
                    case 0xBF:
                        *Metaph++ = 'Y';
                        break;
                    default:      /* skipping the rest */
                        break;
                    }
                }
                break;
            default:
                *Metaph++ = *n;
            }
        } else {
            /* Drop duplicates except for CC */
            if (*(n - 1) == *n && *n != 'C')
                continue;
            /* Check for F J L M N R or first letter vowel */
            if (same(*n) || (n == n_start && vowel(n))) {
                *Metaph++ = *n;
            } else {
                switch (*n) {
                case 'B':

                    /*
                     * B unless in -MB
                     */
                    if (n < (n_end - 1) && *(n - 1) != 'M') {
                        *Metaph++ = *n;
                    }
                    break;
                case 'C':

                    /*
                     * X if in -CIA-, -CH- else S if in
                     * -CI-, -CE-, -CY- else dropped if
                     * in -SCI-, -SCE-, -SCY- else K
                     */
                    if (*(n - 1) != 'S' || !frontv((n + 1))) {
                        if (*(n + 1) == 'I' && *(n + 2) == 'A') {
                            *Metaph++ = 'X';
                        } else if (frontv((n + 1))) {
                            *Metaph++ = 'S';
                        } else if (*(n + 1) == 'H') {
                            *Metaph++ = ((n == n_start && !vowel((n + 2)))
                             || *(n - 1) == 'S')
                                ? (char) 'K' : (char) 'X';
                        } else {
                            *Metaph++ = 'K';
                        }
                    }
                    break;
                case 'D':

                    /*
                     * J if in DGE or DGI or DGY else T
                     */
                    *Metaph++ = (*(n + 1) == 'G' && frontv((n + 2)))
                        ? (char) 'J' : (char) 'T';
                    break;
                case 'G':

                    /*
                     * F if in -GH and not B--GH, D--GH,
                     * -H--GH, -H---GH else dropped if
                     * -GNED, -GN, -DGE-, -DGI-, -DGY-
                     * else J if in -GE-, -GI-, -GY- and
                     * not GG else K
                     */
                    if ((*(n + 1) != 'J' || vowel((n + 2))) &&
                        (*(n + 1) != 'N' || ((n + 1) < n_end &&
                                 (*(n + 2) != 'E' || *(n + 3) != 'D'))) &&
                        (*(n - 1) != 'D' || !frontv((n + 1))))
                        *Metaph++ = (frontv((n + 1)) &&
                                 *(n + 2) != 'G') ? (char) 'G' : (char) 'K';
                    else if (*(n + 1) == 'H' && !noghf(*(n - 3)) &&
                         *(n - 4) != 'H')
                        *Metaph++ = 'F';
                    break;
                case 'H':

                    /*
                     * H if before a vowel and not after
                     * C, G, P, S, T else dropped
                     */
                    if (!varson(*(n - 1)) && (!vowel((n - 1)) ||
                               vowel((n + 1))))
                        *Metaph++ = 'H';
                    break;
                case 'K':

                    /*
                     * dropped if after C else K
                     */
                    if (*(n - 1) != 'C')
                        *Metaph++ = 'K';
                    break;
                case 'P':

                    /*
                     * F if before H, else P
                     */
                    *Metaph++ = *(n + 1) == 'H' ?
                        (char) 'F' : (char) 'P';
                    break;
                case 'Q':

                    /*
                     * K
                     */
                    *Metaph++ = 'K';
                    break;
                case 'S':

                    /*
                     * X in -SH-, -SIO- or -SIA- else S
                     */
                    *Metaph++ = (*(n + 1) == 'H' ||
                             (*(n + 1) == 'I' && (*(n + 2) == 'O' ||
                              *(n + 2) == 'A')))
                        ? (char) 'X' : (char) 'S';
                    break;
                case 'T':

                    /*
                     * X in -TIA- or -TIO- else 0 (zero)
                     * before H else dropped if in -TCH-
                     * else T
                     */
                    if (*(n + 1) == 'I' && (*(n + 2) == 'O' ||
                               *(n + 2) == 'A'))
                        *Metaph++ = 'X';
                    else if (*(n + 1) == 'H')
                        *Metaph++ = '0';
                    else if (*(n + 1) != 'C' || *(n + 2) != 'H')
                        *Metaph++ = 'T';
                    break;
                case 'V':

                    /*
                     * F
                     */
                    *Metaph++ = 'F';
                    break;
                case 'W':

                    /*
                     * W after a vowel, else dropped
                     */
                case 'Y':

                    /*
                     * Y unless followed by a vowel
                     */
                    if (vowel((n + 1)))
                        *Metaph++ = *n;
                    break;
                case 'X':

                    /*
                     * KS
                     */
                    if (n == n_start)
                        *Metaph++ = 'S';
                    else {
                        *Metaph++ = 'K';    /* Insert K, then S */
                        KSflag = 1;
                    }
                    break;
                case 'Z':

                    /*
                     * S
                     */
                    *Metaph++ = 'S';
                    break;
                }
            }
        }
    }

    *Metaph = 0;                /* Null terminate */
    return( slapi_ch_strdup( buf ) );
}

#endif /* METAPHONE */
#endif /* !SOUNDEX */
