/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
