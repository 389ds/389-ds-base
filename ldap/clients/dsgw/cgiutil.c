/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * cgiutil.c -- CGI-related utility functions -- HTTP gateway
 *
 * Note: tihs code is derived from the extras/changepw.c code that ships
 *	with the FastTrack 2.0 server
 *
 * Copyright (c) 1996 Netscape Communications Corp.
 * All rights reserved.
 */

#include "dsgw.h"
#include "dbtdsgw.h"

#include <prprf.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>

/* globals */
static char **formvars = NULL;

/* functions */
static char **dsgw_string_to_vec(char *in);

static void
dsgw_vec_convert (char** vec)
    /* Convert input from the charset named in it (if any) to UTF_8.
       Either return s, or free(s) and return the converted string.
    */
{
    static const char* prefix = "charset=";
    const size_t prefix_len = strlen (prefix);
    char** v;

    if (vec) for (v = vec; *v; ++v) {
	if (!strncmp (*v, prefix, prefix_len)) {
	    char* charset = *v + prefix_len;
	    UConverter* converter = NULL;
	    UErrorCode err = U_ZERO_ERROR;
	    if ( ! is_UTF_8 (charset) && (converter = ucnv_open(charset, &err)) &&
		 (err == U_ZERO_ERROR) ) {
		for (v = vec; *v; ++v) {
		    char* s = strchr (*v, '=');
		    if (s != NULL) {
			char *t = NULL;
			const size_t nlen = (++s) - *v;
			const size_t slen = strlen (s);
			size_t tlen = 0;
			size_t reallen = 0;
			int result;

			if (ucnv_getMaxCharSize(converter) == 1) {
			    tlen = slen + 2; /* best case - ascii or other 7/8 bit */
			} else { /* assume worst case utf8 - each char is 3 bytes */
			    tlen = (slen * 3) + 2;
			}
			do {
			    char *tptr;
			    size_t realSlen = 0;
			    err = U_ZERO_ERROR;

			    if (t) {
				t = dsgw_ch_realloc(t, nlen + tlen);
			    } else {
				t = dsgw_ch_malloc(nlen + tlen);
			    }
			    tptr = t + nlen;

			    /* copy the converted characters into t after the '=', and
			       leave room for the trailing 0 */
			    result = dsgw_convert(DSGW_TO_UTF8, converter,
						  &tptr, (tlen - nlen - 1), &reallen,
						  s, slen, &realSlen, &err);
			    tlen += slen; /* if failed, make more room */
			} while (result == 0);
			if ((result == 1) && (err == U_ZERO_ERROR)) {
			    memcpy (t, *v, nlen);
			    t[nlen+reallen] = '\0';
			    free (*v);
			    *v = t;
			} else {
			    free (t);
			}
			ucnv_reset (converter); /* back to initial shift state */
		    }
		}
		ucnv_close (converter);
	    }
	    if (U_FAILURE(err)) {
		dsgw_error(DSGW_ERR_CHARSET_NOT_SUPPORTED, charset, 0, 0, 0);
	    }
	    break;
	}
    }
}

/* Read in the variables from stdin, unescape them, and then put them in 
 * the static vector.
 *
 * Return 0 if all goes well; DSGW error code otherwise
 */
int
dsgw_post_begin(FILE *in) 
{
    char *ct, *vars = NULL, *tmp = NULL;
    int cl;

    if (( ct = getenv( "CONTENT_TYPE" )) == NULL ||
	    strcasecmp( ct, "application/x-www-form-urlencoded" ) != 0 ||
	    ( tmp = getenv( "CONTENT_LENGTH" )) == NULL ) {
	return( DSGW_ERR_BADFORMDATA );
    }

    cl = atoi(tmp);

    vars = (char *)dsgw_ch_malloc(cl+1);

    if ( fread(vars, 1, cl, in) != cl ) {
	return( DSGW_ERR_BADFORMDATA );
    }

    vars[cl] = '\0';
#ifdef DSGW_DEBUG
    dsgw_log ("vars=\"%s\"\n", vars);
#endif
    formvars = dsgw_string_to_vec (vars);
    free( vars );
    dsgw_vec_convert (formvars);

#ifdef DSGW_DEBUG
    dsgw_logstringarray( "formvars", formvars );
if (0) {
    char** var = formvars;
    if (var) {
	printf ("Content-type: text/html;charset=UTF-8\n\n<HTML><BODY>\n");
	for (; *var; ++var) {
	    printf ("%s<br>\n", *var);
	}
	printf ("</BODY></HTML>\n");
	exit (1);
    }
}
#endif

    return( 0 );
}


/* Unescape the %xx variables as they're sent in. */
void
dsgw_form_unescape(char *str) 
{
    register int x = 0, y = 0;
    int l = strlen(str);
    char digit;

    while(x < l)  {
        if((str[x] == '%') && (x < (l - 2)))  {
            ++x;
            digit = (str[x] >= 'A' ? 
                         ((str[x] & 0xdf) - 'A')+10 : (str[x] - '0'));
            digit *= 16;

            ++x;
            digit += (str[x] >= 'A' ? 
                         ((str[x] & 0xdf) - 'A')+10 : (str[x] - '0'));

            str[y] = digit;
        } 
        else if(str[x] == '+')  {
            str[y] = ' ';
        } else {
            str[y] = str[x];
        }
        x++;
        y++;
    }
    str[y] = '\0';
}


/* Return the value of a POSTed variable, or NULL if none was sent. */
char *
dsgw_get_cgi_var(char *varname, int required)
{
    register int x = 0;
    int len = strlen(varname);
    char *ans = NULL;
   
    while(formvars != NULL && formvars[x])  {
    /*  We want to get rid of the =, so len, len+1 */
        if((!strncmp(formvars[x], varname, len)) &&
		(*(formvars[x]+len) == '='))  {
            ans = dsgw_ch_strdup(formvars[x] + len + 1);
            if(!strcmp(ans, "")) {
		free(ans);
                ans = NULL;
	    }
            break;
        }  else
            x++;
    }

    if ( required == DSGW_CGIVAR_REQUIRED && ans == NULL ) {
	char errbuf[ 256 ];
	PR_snprintf( errbuf, 256,
		XP_GetClientStr(DBT_missingFormDataElement100s_), varname );
	dsgw_error( DSGW_ERR_BADFORMDATA, errbuf, DSGW_ERROPT_EXIT, 0, NULL );
    }

    return ans;
}


/*
 * Return integer equivalent of POSTed value.  If no variable POSTed,
 * return defval.
 */
int
dsgw_get_int_var( char *varname, int required, int defval )
{
    char	*val;
    int		rc;

    if (( val = dsgw_get_cgi_var( varname, required )) == NULL ) {
	rc = defval;
    } else {
	rc = atoi( val );
	free( val );
    }

    return( rc );
}


/*
 * Return non-zero if POSTed variable is "true" or "yes".  If !required
 * and no variable POSTed, return defval.
 */
int
dsgw_get_boolean_var( char *varname, int required, int defval )
{
    char	*val;
    int		rc;

    if (( val = dsgw_get_cgi_var( varname, required )) == NULL ) {
	rc = defval;
    } else {
	rc = ( strcasecmp( val, "true" ) == 0 ||
		strcasecmp( val, "yes" ) == 0 );
	free( val );
    }

    return( rc );
}


/*
 * If a CGI variable named "varname_escaped" was POST'd, unescape it and
 *	return its value.
 * Otherwise if "varname" is not NULL and a CGI variable called "varname"
 *	was POST'd, return its value.
 * Otherwise return NULL.
 */
char *
dsgw_get_escaped_cgi_var( char *varname_escaped, char *varname, int required )
{
    char	*val;

    if (( val = dsgw_get_cgi_var( varname_escaped,
	    ( varname == NULL ) ? required: DSGW_CGIVAR_OPTIONAL )) != NULL ) {
	dsgw_form_unescape( val );
    } else if ( varname != NULL ) {
	 val = dsgw_get_cgi_var( varname, required );
    }

    return( val );
}


/* Convert the input from stdin to a usable variable vector. */
static char **
dsgw_string_to_vec(char *in)
{
    char **ans;
    int vars = 0;
    register int x = 0;
    char *tmp;
    
    while(in[x])
        if(in[x++]=='=')
            vars++;
    
    ans = (char **) dsgw_ch_malloc((sizeof(char *)) * (vars+1));
  
    x=0;
    /* strtok() is not MT safe, but it is okay to call here because it is used in monothreaded env */
    tmp = strtok(in, "&");
    ans[x]=dsgw_ch_strdup(tmp);
    dsgw_form_unescape(ans[x++]);

    while((tmp = strtok(NULL, "&")))  {
	if ( strchr( tmp, '=' ) == NULL ) {
	    break;
	}
        ans[x] = dsgw_ch_strdup(tmp);
        dsgw_form_unescape(ans[x++]);
    }
    ans[x] = NULL;

    return(ans);
}


/*
 * Step through all the CGI POSTed variables.  A malloc'd copy of the variable
 * name is returned and *valuep is set to point to the value (not malloc'd).
 * If there are no more variables, NULL is returned.
 * 
 * The first time this is called, *indexp should be zero.  On subsequent
 * calls, pass the same indexp as on the first call.
 */
char *
dsgw_next_cgi_var( int *indexp, char **valuep )
{
    char	*name;
    int		namelen;

    if ( formvars == NULL || formvars[ *indexp ] == NULL ) {
	return( NULL );
    }

    if (( *valuep = strchr( formvars[ *indexp ], '=' )) == NULL ) {
	namelen = strlen( formvars[ *indexp ] );
    } else {
	namelen = *valuep - formvars[ *indexp ];
	++(*valuep);
    }
    name = dsgw_ch_malloc( namelen + 1 );
    memcpy( name, formvars[ *indexp ], namelen );
    name[ namelen ] = '\0';
    
    *indexp += 1;

    return( name );
}

/*
 * converts a buffer of characters to/from UTF8 from/to a native charset
 * the given converter will handle the native charset
 * returns 0 if not all of source was converted, 1 if all of source
 * was converted, -1 upon error
 * all of source will be converted if there is enough room in dest to contain
 * the entire conversion, or if dest is null and we are malloc'ing space for dest
 */
int
dsgw_convert(
    int direction, /* false for native->utf8, true for utf8->native */
    UConverter *nativeConv, /* convert from/to native charset */
    char **dest, /* *dest is the destination buffer - if *dest == NULL, it will be malloced */
    size_t destSize, /* size of dest buffer (ignored if *dest == NULL) */
    size_t *nDest, /* number of chars written to dest */
    const char *source, /* source buffer to convert - either in native encoding (from) or utf8 (to) */
    size_t sourceSize, /* size of source buffer - if 0, assume source is NULL terminated */
    size_t *nSource, /* number of chars read from source buffer */
    UErrorCode *pErrorCode /* will be reset each time through */
)
{
#define CHUNK_SIZE 1024
    UChar pivotBuffer[CHUNK_SIZE];
    UChar *pivot, *pivot2;
    static UConverter *utf8Converter = NULL;
    UConverter *inConverter, *outConverter;
    char *myDest;
    const char *mySource;
    const char *destLimit;
    const char *sourceLimit;
    int destAlloc = 0; /* set to true if we allocated *dest */

    *pErrorCode = U_ZERO_ERROR;

    if(sourceSize<0 || source==NULL || nDest==NULL || nSource==NULL)
    {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }

    *nSource = 0;
    *nDest = 0;

    /* if source size is 0, assume source is null terminated and use strlen */
    if(sourceSize==0) {
	sourceSize = strlen(source);
    }

    /* create the converters */
    if (!utf8Converter) {
	utf8Converter = ucnv_open(UNICODE_ENCODING_UTF_8, pErrorCode);
	if(U_FAILURE(*pErrorCode)) {
	    return -1;
	}
    }
    /* reset utf8Converter if done or error */

    if (direction) {
	inConverter = utf8Converter; /* source is utf8 */
	outConverter = nativeConv; /* dest is native charset */
    } else {
	inConverter = nativeConv; /* source is native charset */
	outConverter = utf8Converter; /* dest is utf8 */
    }

    /* if dest is NULL, allocate space for it - may be reallocated later */
    if (!*dest) {
	/* good approximation of size is n chars in source * max dest char size */
	destSize = ucnv_getMaxCharSize(outConverter) * sourceSize;
	*dest = dsgw_ch_malloc(destSize);
	destAlloc = 1;
    }

    /* set up the other variables */
    mySource = source;
    sourceLimit = source + sourceSize;
    pivot = pivot2 = pivotBuffer;
    myDest = *dest;
    destLimit = *dest + destSize;

    /*
     * loops until the input buffer is completely consumed
     * or an error is encountered;
     * first we convert from inConverter codepage to Unicode
     * then from Unicode to outConverter codepage
     */
    do {
	pivot = pivotBuffer;
	ucnv_toUnicode(inConverter,
		       &pivot, pivotBuffer + CHUNK_SIZE,
		       &mySource, sourceLimit,
		       NULL,
		       TRUE,
		       pErrorCode);

	/* U_BUFFER_OVERFLOW_ERROR only means that the pivot buffer is full */
	if(U_SUCCESS(*pErrorCode) || (*pErrorCode == U_BUFFER_OVERFLOW_ERROR)) {
	    pivot2 = pivotBuffer;

	    /* convert and write bytes from the pivot buffer to the dest -
	       if dest is allocated and we run out of space in dest, grow
	       dest and try again - otherwise, just bail out and let the
	       caller know that their dest buffer is full and they need
	       to try again */
	    do {
		*pErrorCode = U_ZERO_ERROR;
		ucnv_fromUnicode(outConverter,
				 &myDest, destLimit,
				 (const UChar **)&pivot2, pivot,
				 NULL,
				 (UBool)(mySource == sourceLimit),
				 pErrorCode);

		/* we overflowed dest and dest is allocated, so let's increase
		   the dest size */
		if ((*pErrorCode == U_BUFFER_OVERFLOW_ERROR) && destAlloc) {
		    /* figure out where myDest was pointing */
		    size_t myDestOffset = myDest - *dest;
		    /* probably don't need this much more room . . . */
		    destSize += CHUNK_SIZE;
		    /* realloc *dest for new size */
		    *dest = dsgw_ch_realloc(*dest, destSize);
		    /* reset myDest in new *dest */
		    myDest = *dest + myDestOffset;
		    /* set new destLimit */
		    destLimit = *dest + destSize;
		} else {
		    break; /* skip it */
		}
	    } while(*pErrorCode == U_BUFFER_OVERFLOW_ERROR);
	    /*
	     * If this overflows the fixed size dest, then we must stop
	     * converting and return what we already have
	     * in this case, pErrorCode will be buffer overflow error because
	     * we have overflowed the dest buffer
	     * the outer while loop will break because !U_SUCCESS
	     */
	}
    } while(U_SUCCESS(*pErrorCode) && source != sourceLimit);

    *nSource = mySource - source; /* n chars read from source */
    *nDest = myDest - *dest; /* n chars written to dest */

    if (U_SUCCESS(*pErrorCode) && source == sourceLimit) {
	/* reset internal converter */
	ucnv_reset(utf8Converter);
	return 1; /* converted entire string */
    }

    if (source != sourceLimit) {
	/* not done with conversion yet */
	/* no reset here - preserve state for next call */
	return 0;
    }

    /* error */
    ucnv_reset(utf8Converter);
    return -1;
}
