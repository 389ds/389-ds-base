/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Easter egg encoder.  See ../fedse.c:egg_decode() for the mirror image.
 */
#include <stdio.h>

static unsigned char egg_nibble2char( int nibble );

int
main( int argc, char *argv[] )
{
	int		c, colcount;
	char	outc;

	if ( argc > 1 ) {
		fprintf( stderr, "usage: %s < in > out\n", argv[0] );
		return 2;
	}

	colcount = 0;
	while (( c = getchar()) != EOF ) {
		if ( 0 == colcount ) {
			putchar( '"' );
		}
		c ^= 122;
		outc = egg_nibble2char( (c & 0xF0) >> 4 );
		putchar( outc );
		++colcount;
		outc = egg_nibble2char( c & 0x0F );
		putchar( outc );
		++colcount;
		if ( colcount > 72 ) {
			colcount = 0;
			putchar( '"' );
			putchar( '\n' );
		}
	}

	if ( colcount > 0 ) {
		putchar( '"' );
		putchar( '\n' );
	}

	return 0;
}


static unsigned char
egg_nibble2char( int nibble )
{
	return ( nibble < 10 ) ? nibble + '0' : ( nibble - 10 ) + 'A';
}
