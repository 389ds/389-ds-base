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


#include <malloc.h>
#include <string.h>
#include <ldaputil/certmap.h>
#include <ldaputil/encode.h>

/* The magic set of 64 chars in the uuencoded data */
static unsigned char uuset[] = {
'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T',
'U','V','W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l','m','n',
'o','p','q','r','s','t','u','v','w','x','y','z','0','1','2','3','4','5','6','7',
'8','9','+','/' };
 
static int do_uuencode(unsigned char *src, unsigned char *dst, int srclen)
{
   int  i, r;
   unsigned char *p;
 
/* To uuencode, we snip 8 bits from 3 bytes and store them as
6 bits in 4 bytes.   6*4 == 8*3 (get it?) and 6 bits per byte
yields nice clean bytes
 
It goes like this:
        AAAAAAAA BBBBBBBB CCCCCCCC
turns into the standard set of uuencode ascii chars indexed by numbers:
        00AAAAAA 00AABBBB 00BBBBCC 00CCCCCC
 
Snip-n-shift, snip-n-shift, etc....
 
*/
 
   for (p=dst,i=0; i < srclen; i += 3) {
                /* Do 3 bytes of src */
                register char b0, b1, b2;
 
                b0 = src[0];
                if (i==srclen-1)
                        b1 = b2 = '\0';
                else if (i==srclen-2) {
                        b1 = src[1];
                        b2 = '\0';
                }
                else {
                        b1 = src[1];
                        b2 = src[2];
                }
 
                *p++ = uuset[b0>>2];
                *p++ = uuset[(((b0 & 0x03) << 4) | ((b1 & 0xf0) >> 4))];
                *p++ = uuset[(((b1 & 0x0f) << 2) | ((b2 & 0xc0) >> 6))];
                *p++ = uuset[b2 & 0x3f];
                src += 3;
   }
   *p = 0;      /* terminate the string */
   r = (unsigned char *)p - (unsigned char *)dst;/* remember how many we did */
 
   /* Always do 4-for-3, but if not round threesome, have to go
          clean up the last extra bytes */
 
   for( ; i != srclen; i--)
                *--p = '=';
 
   return r;
}
 
const unsigned char pr2six[256]={
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
    52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,64,0,1,2,3,4,5,6,7,8,9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,64,26,27,
    28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64
};

static char *_uudecode(const char *bufcoded)
{
    register const char *bufin = bufcoded;
    register unsigned char *bufout;
    register int nprbytes;
    unsigned char *bufplain;
    int nbytesdecoded;

    /* Find the length */
    while(pr2six[(int)*(bufin++)] <= 63);
    nprbytes = bufin - bufcoded - 1;
    nbytesdecoded = ((nprbytes+3)/4) * 3;

    bufout = (unsigned char *) malloc(nbytesdecoded + 1);
    bufplain = bufout;

    bufin = bufcoded;
    
    while (nprbytes > 0) {
        *(bufout++) = (unsigned char) 
            (pr2six[(int)(*bufin)] << 2 | pr2six[(int)bufin[1]] >> 4);
        *(bufout++) = (unsigned char) 
            (pr2six[(int)bufin[1]] << 4 | pr2six[(int)bufin[2]] >> 2);
        *(bufout++) = (unsigned char) 
            (pr2six[(int)bufin[2]] << 6 | pr2six[(int)bufin[3]]);
        bufin += 4;
        nprbytes -= 4;
    }
    
    if(nprbytes & 03) {
        if(pr2six[(int)bufin[-2]] > 63)
            nbytesdecoded -= 2;
        else
            nbytesdecoded -= 1;
    }
    bufplain[nbytesdecoded] = '\0';

    return (char *)bufplain;
}


char *dbconf_encodeval (const char *val)
{
    int len = strlen(val);
    char *dst = (char *)malloc(2*len);

    if (dst) {
	do_uuencode((unsigned char *)val, (unsigned char *)dst, len);
    }

    return dst;
}

char *dbconf_decodeval (const char *val)
{
    return _uudecode(val);
}

