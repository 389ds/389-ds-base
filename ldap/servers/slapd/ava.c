/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* ava.c - routines for dealing with attribute value assertions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

static void strcpy_special_undo();

int
get_ava(
    BerElement	*ber,
    struct ava	*ava
)
{
	char	*type;

	if ( ber_scanf( ber, "{ao}", &type, &ava->ava_value )
	    == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY, "  get_ava ber_scanf\n", 0, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );
	}
	ava->ava_type = slapi_attr_syntax_normalize(type);
	free( type );

	return( 0 );
}

void
ava_done(
    struct ava *ava
)
{
	slapi_ch_free( (void**)&(ava->ava_type) );
	slapi_ch_free( (void**)&(ava->ava_value.bv_val) );
}

int
rdn2ava(
    char	*rdn,
    struct ava	*ava
)
{
	char	*s;

	if ( (s = strchr( rdn, '=' )) == NULL ) {
		return( -1 );
	}
	*s++ = '\0';

	ava->ava_type = rdn;
	strcpy_special_undo( s, s );
	ava->ava_value.bv_val = s;
	ava->ava_value.bv_len = strlen( s );

	return( 0 );
}

/*
** This function takes a quoted attribute value of the form "abc",
** and strips off the enclosing quotes.  It also deals with quoted
** characters by removing the preceeding '\' character.
**
*/
static void
strcpy_special_undo( char *d, const char *s )
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

