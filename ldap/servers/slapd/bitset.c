/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

unsigned char
slapi_setbit_uchar(unsigned char f,unsigned char bitnum)
{
    return (f | ((unsigned char)1 << bitnum));
}

unsigned char
slapi_unsetbit_uchar(unsigned char f,unsigned char bitnum)
{
/* TEMPORARY WORKAROUND FOR x86 compiler problem on solaris
 *   return (f & (~((unsigned char)1 << bitnum)));
 */
      unsigned char t;
    t = f & (~((unsigned char)1 << bitnum));
    return(t);
}

int
slapi_isbitset_uchar(unsigned char f,unsigned char bitnum)
{
    return (f & ((unsigned char)1 << bitnum));
}


unsigned int
slapi_setbit_int(unsigned int f,unsigned int bitnum)
{
    return (f | ((unsigned int)1 << bitnum));
}

unsigned int
slapi_unsetbit_int(unsigned int f,unsigned int bitnum)
{
    return (f & (~((unsigned int)1 << bitnum)));
}

int
slapi_isbitset_int(unsigned int f,unsigned int bitnum)
{
    return (f & ((unsigned int)1 << bitnum));
}
