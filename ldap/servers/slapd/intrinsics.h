/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Header file used to declare functions which we beat on heavily as intrinsic */

/* For NT ...*/

#ifdef _WIN32
__inline static int  strcmpi_fast(const char * dst, const char * src)
{
	int f,l;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) );
	return(f - l);
}
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp(x,y) strcmpi_fast(x,y)
#ifdef strcmpi
#undef strcmpi
#endif
#define strcmpi(x,y) strcmpi_fast(x,y)

__inline static int tolower_fast(int c)
{
	if ( (c >= 'A') && (c <= 'Z') )
		c = c + ('a' - 'A');
	return c;
}
#ifdef tolower
#undef tolower
#endif
#define tolower(x) tolower_fast(x)

#else

#ifdef HPUX
#pragma INLINE strcmpi_fast,tolower_fast,toupper_fast,strncasecmp_fast
#endif
#ifdef LINUX
#define INLINE_DIRECTIVE __inline__
#else
#define INLINE_DIRECTIVE
#endif

INLINE_DIRECTIVE static int strcmpi_fast(const char * dst, const char * src)
{
	int f,l;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) );
	return(f - l);
}
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp(x,y) strcmpi_fast(x,y)
#ifdef strcmpi
#undef strcmpi
#endif
#define strcmpi(x,y) strcmpi_fast(x,y)

INLINE_DIRECTIVE static int tolower_fast(int c)
{
	if ( (c >= 'A') && (c <= 'Z') )
		c = c + ('a' - 'A');
	return c;
}
#ifdef tolower
#undef tolower
#endif
#define tolower(x) tolower_fast(x)

INLINE_DIRECTIVE static int toupper_fast(int c)
{
    if ( (c >= 'a') && (c <= 'z') )
	c = c - ('a' - 'A');
    return c;
}
#ifdef toupper
#undef toupper
#endif
#define toupper(x) toupper_fast(x)

INLINE_DIRECTIVE static int strncasecmp_fast(const char * dst, const char * src, int n)
{
	int f,l,x=0;
	do {
		if ( ((f = (unsigned char)(*(dst++))) >= 'A') && (f <= 'Z') )
			f -= ('A' - 'a');
		if ( ((l = (unsigned char)(*(src++))) >= 'A') && (l <= 'Z') )
			l -= ('A' - 'a');
	} while ( f && (f == l) && ++x < n );
	return(f - l);
}

#ifdef strncasecmp
#undef strncasecmp
#endif
#define strncasecmp(x,y,z) strncasecmp_fast(x,y,z)
#endif /* NT */
