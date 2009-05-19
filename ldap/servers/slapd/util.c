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

/* util.c   -- utility functions -- functions available form libslapd */
#ifdef _WIN32
#include <direct.h> /* for getcwd */
#else
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#endif
#include <libgen.h>
#include <pk11func.h>
#include "slap.h"
#include "prtime.h"
#include "prinrval.h"
#include "snmp_collator.h"
#include <ldap_ssl.h>
#include <ldappr.h>

#define UTIL_ESCAPE_NONE      0
#define UTIL_ESCAPE_HEX       1
#define UTIL_ESCAPE_BACKSLASH 2

#if defined( _WIN32 )
#define _PSEP "\\"
#define _PSEP2 "\\\\"
#define _CSEP '\\'
#else
#define _PSEP "/"
#define _PSEP2 "//"
#define _CSEP '/'
#endif


static int special_np(unsigned char c)
{
	if(c < 32 || c > 126) {
		return UTIL_ESCAPE_HEX;
	} else if ((c== '"') || (c=='\\')) 
	{
		return UTIL_ESCAPE_HEX;
	} 
    return UTIL_ESCAPE_NONE;
}

static int special_np_and_punct(unsigned char c)
{
    if (c < 32 || c > 126 || c == '*') return UTIL_ESCAPE_HEX;
    if (c == '\\' || c == '"') return UTIL_ESCAPE_BACKSLASH;
    return UTIL_ESCAPE_NONE;
}

static int special_filter(unsigned char c)
{
    /*
     * Escape all non-printing chars and double-quotes in addition 
     * to those required by RFC 2254 so that we can use the string
     * in log files.
     */
    return (c < 32 || 
            c > 126 || 
            c == '*' || 
            c == '(' || 
            c == ')' || 
            c == '\\' || 
            c == '"') ? UTIL_ESCAPE_HEX : UTIL_ESCAPE_NONE;
}

static const char*
do_escape_string (
    const char* str, 
    int len,                    /* -1 means str is nul-terminated */
    char buf[BUFSIZ],
    int (*special)(unsigned char)
)
{
    const char* s;
    const char* last;
    int esc;

    if (str == NULL) {
        *buf = '\0'; 
        return buf;
    }

    if (len == -1) len = strlen (str);
    if (len == 0) return str;

    last = str + len - 1;
    for (s = str; s <= last; ++s) {
	if ( (esc = (*special)((unsigned char)*s))) {
	    const char* first = str;
	    char* bufNext = buf;
	    int bufSpace = BUFSIZ - 4;
	    while (1) {
		if (bufSpace < (s - first)) s = first + bufSpace - 1;
		if (s > first) {
		    memcpy (bufNext, first, s - first);
		    bufNext  += (s - first);
		    bufSpace -= (s - first);
		}
		if (s > last) {
		    break;
		}
		do {
		    *bufNext++ = '\\'; --bufSpace;
		    if (bufSpace < 2) {
			memcpy (bufNext, "..", 2);
			bufNext += 2;
			goto bail;
		    }
		    if (esc == UTIL_ESCAPE_BACKSLASH) {
			*bufNext++ = *s; --bufSpace;
		    } else {    /* UTIL_ESCAPE_HEX */
			sprintf (bufNext, "%02x", (unsigned)*(unsigned char*)s);
			bufNext += 2; bufSpace -= 2;
		    }
	        } while (++s <= last && 
                         (esc = (*special)((unsigned char)*s)));
		if (s > last) break;
		first = s;
		while ( (esc = (*special)((unsigned char)*s)) == UTIL_ESCAPE_NONE && s <= last) ++s;
	    }
	  bail:
	    *bufNext = '\0';
	    return buf;
	}
    } 
    return str;
}

/*
 * Function: escape_string
 * Arguments: str: string
 *            buf: a char array of BUFSIZ length, in which the escaped string will
 *                 be returned.
 * Returns: a pointer to buf, if str==NULL or it needed to be escaped, or
 *          str itself otherwise.
 *
 * This function should only be used for generating loggable strings.
 */
const char*
escape_string (const char* str, char buf[BUFSIZ])
{
  return do_escape_string(str,-1,buf,special_np);
}

const char*
escape_string_with_punctuation(const char* str, char buf[BUFSIZ])
{
  return do_escape_string(str,-1,buf,special_np_and_punct);
}

const char*
escape_filter_value(const char* str, int len, char buf[BUFSIZ])
{
    return do_escape_string(str,len,buf,special_filter);
}

/*
** This function takes a quoted attribute value of the form "abc",
** and strips off the enclosing quotes.  It also deals with quoted
** characters by removing the preceeding '\' character.
**
*/
void
strcpy_unescape_value( char *d, const char *s )
{
    int gotesc = 0;
    const char *end = s + strlen(s);
    for ( ; *s; s++ )
    {
        switch ( *s )
        {
        case '\\':
            if ( gotesc ) {
                gotesc = 0;
            } else {
                gotesc = 1;
                if ( s+2 < end ) {
                    int n = hexchar2int( s[1] );
                    /* If 8th bit is on, the char is not ASCII (not UTF-8).  
                     * Thus, not UTF-8 */
                    if ( n >= 0 && n < 8 ) {
                        int n2 = hexchar2int( s[2] );
                        if ( n2 >= 0 ) {
                            n = (n << 4) + n2;
                            if (n == 0) { /* don't change \00 */
                                *d++ = *s++;
                                *d++ = *s++;
                                *d++ = *s;
                            } else { /* change \xx to a single char */
                                *d++ = (char)n;
                                s += 2;
                            }
                            gotesc = 0;
                        }
                    }
                }
                if (gotesc) {
                    *d++ = *s;
                }
            }
            break;
        default:
            if (gotesc) {
                d--;
            }
            *d++ = *s;
            gotesc = 0;
            break;
        }
    }
    *d = '\0';
}

/* functions to convert between an entry and a set of mods */
int slapi_mods2entry (Slapi_Entry **e, const char *idn, LDAPMod **iattrs)
{
    int             i, rc = LDAP_SUCCESS;
    LDAPMod         **attrs= NULL;

    PR_ASSERT (idn);
    PR_ASSERT (iattrs);
    PR_ASSERT (e);

    attrs = normalize_mods2bvals((const LDAPMod **)iattrs);
    PR_ASSERT (attrs);

    /* Construct an entry */
    *e = slapi_entry_alloc();
    PR_ASSERT (*e);
    slapi_entry_init(*e, slapi_ch_strdup(idn), NULL);

    for (i = 0; rc==LDAP_SUCCESS && attrs[ i ]!=NULL; i++)
    {
        char *normtype;
        Slapi_Value **vals;

        normtype = slapi_attr_syntax_normalize(attrs[ i ]->mod_type);
        valuearray_init_bervalarray(attrs[ i ]->mod_bvalues, &vals);
        if (strcasecmp(normtype, SLAPI_USERPWD_ATTR) == 0)
        {
            pw_encodevals(vals);
        }

		/* set entry uniqueid - also adds attribute to the list */
		if (strcasecmp(normtype, SLAPI_ATTR_UNIQUEID) == 0)
			slapi_entry_set_uniqueid (*e, slapi_ch_strdup (slapi_value_get_string(vals[0])));
		else
			rc = slapi_entry_add_values_sv(*e, normtype, vals);

        valuearray_free(&vals);
        if (rc != LDAP_SUCCESS)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "slapi_add_internal: add_values for type %s failed\n", normtype, 0, 0 );
            slapi_entry_free (*e);
            *e = NULL;
        }
        slapi_ch_free((void **) &normtype);
    }
    freepmods(attrs);

    return rc;
}

int slapi_entry2mods (const Slapi_Entry *e, char **dn, LDAPMod ***attrs)
{
	Slapi_Mods smods;
	Slapi_Attr *attr;
	Slapi_Value **va;
	char *type;
	int rc;

	PR_ASSERT (e && attrs);

	if (dn)
		*dn = slapi_ch_strdup (slapi_entry_get_dn ((Slapi_Entry *)e));
	slapi_mods_init (&smods, 0);

	rc = slapi_entry_first_attr(e, &attr);
	while (rc == 0)
	{
		if ( NULL != ( va = attr_get_present_values( attr ))) {
			slapi_attr_get_type(attr, &type);		
			slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va );
		}
		rc = slapi_entry_next_attr(e, attr, &attr);
	}

	*attrs = slapi_mods_get_ldapmods_passout (&smods);
	slapi_mods_done (&smods);

	return 0;
}

/******************************************************************************
*
*  normalize_mods2bvals
*
*     
*  
*
*******************************************************************************/

LDAPMod **
normalize_mods2bvals(const LDAPMod **mods)
{
    int        w, x, vlen, num_values, num_mods;
    LDAPMod    **normalized_mods;

    if (mods == NULL) 
    {
        return NULL;
    }

    /* first normalize the mods so they are bvalues */
    /* count the number of mods -- sucks but should be small */
    num_mods = 1;
    for (w=0; mods[w] != NULL; w++) num_mods++;
    
    normalized_mods = (LDAPMod **) slapi_ch_calloc(num_mods, sizeof(LDAPMod *));

    for (w = 0; mods[w] != NULL; w++) 
    {
        /* copy each mod into a normalized modbvalue */
        normalized_mods[w] = (LDAPMod *) slapi_ch_calloc(1, sizeof(LDAPMod));
        normalized_mods[w]->mod_op = mods[w]->mod_op | LDAP_MOD_BVALUES;

        normalized_mods[w]->mod_type = slapi_ch_strdup(mods[w]->mod_type);

        /*
         * count the number of values -- kinda sucks but probably
         * less expensive then reallocing, and num_values
         * should typically be very small
         */
        num_values = 0;
        if (mods[w]->mod_op & LDAP_MOD_BVALUES) 
        {
            for (x = 0; mods[w]->mod_bvalues != NULL && 
                    mods[w]->mod_bvalues[x] != NULL; x++) 
            {
                num_values++;
            }
        } else {
            for (x = 0; mods[w]->mod_values[x] != NULL &&
                    mods[w]->mod_values[x] != NULL; x++) 
            {
                num_values++;
            }
        }

        if (num_values > 0)
        {
            normalized_mods[w]->mod_bvalues = (struct berval **)
                slapi_ch_calloc(num_values + 1, sizeof(struct berval *));
        } else {
            normalized_mods[w]->mod_bvalues = NULL;
        }
       
        if (mods[w]->mod_op & LDAP_MOD_BVALUES) 
        {
            for (x = 0; mods[w]->mod_bvalues != NULL &&
                    mods[w]->mod_bvalues[x] != NULL; x++) 
            {
                normalized_mods[w]->mod_bvalues[x] = ber_bvdup(mods[w]->mod_bvalues[x]);
            }
        } else {
            for (x = 0; mods[w]->mod_values != NULL &&
                    mods[w]->mod_values[x] != NULL; x++) 
            {
                normalized_mods[w]->mod_bvalues[ x ] = (struct berval *)
                    slapi_ch_calloc(1, sizeof(struct berval));
		
                vlen = strlen(mods[w]->mod_values[x]);
                normalized_mods[w]->mod_bvalues[ x ]->bv_val =
                    slapi_ch_calloc(vlen + 1, sizeof(char));
                memcpy(normalized_mods[w]->mod_bvalues[ x ]->bv_val,
                         mods[w]->mod_values[x], vlen);
                normalized_mods[w]->mod_bvalues[ x ]->bv_val[vlen] = '\0';
                normalized_mods[w]->mod_bvalues[ x ]->bv_len = vlen;
            }
        }

        /* don't forget to null terminate it */
        if (num_values > 0) 
        {
            normalized_mods[w]->mod_bvalues[ x ] = NULL;
        }
    }
    
    /* don't forget to null terminate the normalize list of mods */
    normalized_mods[w] = NULL;

    return(normalized_mods);
}

/*
 * Return true if the given path is an absolute path, false otherwise
 */
int
is_abspath(const char *path)
{
	if (path == NULL || *path == '\0') {
		return 0; /* empty path is not absolute? */
	}

#if defined( XP_WIN32 )
	if (path[0] == '/' || path[0] == '\\' ||
		(isalpha(path[0]) && (path[1] == ':'))) {
		return 1; /* Windows abs path */
	}
#else
	if (path[0] == '/') {
		return 1; /* unix abs path */
	}
#endif

	return 0; /* not an abs path */
}

static void
clean_path(char **norm_path)
{
    char **np;

    for (np = norm_path; np && *np; np++)
        slapi_ch_free_string(np);
    slapi_ch_free((void  **)&norm_path);
}

static char **
normalize_path(char *path)
{
    char *dname = NULL;
    char *dnamep = NULL;
    char **dirs = (char **)slapi_ch_calloc(strlen(path), sizeof(char *));
    char **rdirs = (char **)slapi_ch_calloc(strlen(path), sizeof(char *));
    char **dp = dirs;
    char **rdp;
    int elimdots = 0;

    if (NULL == path || '\0' == *path) {
        return NULL;
    }

    dname = slapi_ch_strdup(path);
    do {
        dnamep = strrchr(dname, _CSEP);
        if (NULL == dnamep) {
            dnamep = dname;
        } else {
            *dnamep = '\0';
            dnamep++;
        }
        if (0 != strcmp(dnamep, ".") && strlen(dnamep) > 0) {
            *dp++ = slapi_ch_strdup(dnamep); /* rm "/./" and "//" in the path */
        }
    } while ( dnamep > dname /* == -> no more _CSEP */ );
    slapi_ch_free_string(&dname);

    /* remove "xxx/.." in the path */
    for (dp = dirs, rdp = rdirs; dp && *dp; dp++) {
        while (*dp && 0 == strcmp(*dp, "..")) {
            dp++; 
            elimdots++;
        }
        if (elimdots > 0) {
            elimdots--;
        } else if (*dp) {
            *rdp++ = slapi_ch_strdup(*dp);
        }
    }
    /* reverse */
    for (--rdp, dp = rdirs; rdp >= dp && rdp >= rdirs; --rdp, dp++) {
        char *tmpp = *dp;
        *dp = *rdp;
        *rdp = tmpp;
    }

    clean_path(dirs);
    return rdirs;
}

/*
 * Take "relpath" and prepend the current working directory to it
 * if it isn't an absolute pathname already.  The caller is responsible
 * for freeing the returned string. 
 */
char *
rel2abspath( char *relpath )
{
    return rel2abspath_ext( relpath, NULL );
}

char *
rel2abspath_ext( char *relpath, char *cwd )
{
    char abspath[ MAXPATHLEN + 1 ];
    char *retpath = NULL;

#if defined( _WIN32 )
   CHAR szDrive[_MAX_DRIVE];
   CHAR szDir[_MAX_DIR];
   CHAR szFname[_MAX_FNAME];
   CHAR szExt[_MAX_EXT];
#endif

    if ( relpath == NULL ) {
        return NULL;
    }

#if defined( _WIN32 )
    memset (&szDrive, 0, sizeof (szDrive));
    memset (&szDir, 0, sizeof (szDir));
    memset (&szFname, 0, sizeof (szFname));
    memset (&szExt, 0, sizeof (szExt));
    _splitpath( relpath, szDrive, szDir, szFname, szExt );
    if( szDrive[0] && szDir[0] )
        return( slapi_ch_strdup( relpath ));
#endif
    if ( relpath[ 0 ] == _CSEP ) {     /* absolute path */
        PR_snprintf(abspath, sizeof(abspath), "%s", relpath);
    } else {                        /* relative path */
        if ( NULL == cwd ) {
            if ( getcwd( abspath, MAXPATHLEN ) == NULL ) {
                perror( "getcwd" );
                LDAPDebug( LDAP_DEBUG_ANY, "Cannot determine current directory\n",
                        0, 0, 0 );
                exit( 1 );
            }
        } else {
            PR_snprintf(abspath, sizeof(abspath), "%s", cwd);
        }
    
        if ( strlen( relpath ) + strlen( abspath ) + 1  > MAXPATHLEN ) {
            LDAPDebug( LDAP_DEBUG_ANY, "Pathname \"%s" _PSEP "%s\" too long\n",
                    abspath, relpath, 0 );
            exit( 1 );
        }
    
        if ( strcmp( relpath, "." )) {
            if ( abspath[ 0 ] != '\0' &&
                 abspath[ strlen( abspath ) - 1 ] != _CSEP )
            {
                PL_strcatn( abspath, sizeof(abspath), _PSEP );
            }
            PL_strcatn( abspath, sizeof(abspath), relpath );
        }
    }
    retpath = slapi_ch_strdup(abspath);
    /* if there's no '.' or separators, no need to call normalize_path */
    if (NULL != strchr(abspath, '.') || NULL != strstr(abspath, _PSEP))
    {
        char **norm_path = normalize_path(abspath);
        char **np, *rp;
        int pathlen = strlen(abspath) + 1;
        int usedlen = 0;
        for (np = norm_path, rp = retpath; np && *np; np++) {
            int thislen = strlen(*np) + 1;
            if (0 != strcmp(*np, _PSEP))
                PR_snprintf(rp, pathlen - usedlen, "%c%s", _CSEP, *np);
            rp += thislen;
            usedlen += thislen;
        }
        clean_path(norm_path);
    }
    return retpath;
}


/*
 * Allocate a buffer large enough to hold a berval's
 * value and a terminating null byte. The returned buffer
 * is null-terminated. Returns NULL if bval is NULL or if
 * bval->bv_val is NULL.
 */
char *
slapi_berval_get_string_copy(const struct berval *bval)
{
	char *return_value = NULL;
	if (NULL != bval && NULL != bval->bv_val)
	{
		return_value = slapi_ch_malloc(bval->bv_len + 1);
		memcpy(return_value, bval->bv_val, bval->bv_len);
		return_value[bval->bv_len] = '\0';
	}
	return return_value;
}


	/* Takes a return code supposed to be errno or from a plugin
   which we don't expect to see and prints a handy log message */
void slapd_nasty(char* str, int c, int err)
{
	char *msg = NULL;
	char buffer[100];
	PR_snprintf(buffer,sizeof(buffer), "%s BAD %d",str,c);
	LDAPDebug(LDAP_DEBUG_ANY,"%s, err=%d %s\n",buffer,err,(msg = strerror( err )) ? msg : "");
}

/* ***************************************************
	Random function (very similar to rand_r())
   *************************************************** */
int
slapi_rand_r(unsigned int *randx)
{
    if (*randx)
	{
	    PK11_RandomUpdate(randx, sizeof(*randx));
	}
    PK11_GenerateRandom((unsigned char *)randx, (int)sizeof(*randx));
	return (int)(*randx & 0x7FFFFFFF);
}

/* ***************************************************
	Random function (very similar to rand_r() but takes and returns an array)
	Note: there is an identical function in plugins/pwdstorage/ssha_pwd.c.
	That module can't use a libslapd function because the module is included
	in libds_admin, which doesn't link to libslapd. Eventually, shared
	functions should be moved to a shared library.
   *************************************************** */
void
slapi_rand_array(void *randx, size_t len)
{
    PK11_RandomUpdate(randx, len);
    PK11_GenerateRandom((unsigned char *)randx, (int)len);
}

/* ***************************************************
	Random function (very similar to rand()...)
   *************************************************** */
int
slapi_rand()
{
    unsigned int randx = 0;
	return slapi_rand_r(&randx);
}



/************************************************************************
Function:	DS_Sleep(PRIntervalTime ticks)

Purpose:	To replace PR_Sleep()

Author:		Scott Hopson <shopson@netscape.com>

Description:
		Causes the current thread to wait for ticks number of intervals.

		In UNIX this is accomplished by using select()
		which should be supported across all UNIX platforms.

		In WIN32 we simply use the Sleep() function which yields
		for the number of milliseconds specified.

************************************************************************/


#if defined(_WIN32)

#include "windows.h"


void	DS_Sleep(PRIntervalTime ticks)
{
DWORD mSecs = PR_IntervalToMilliseconds(ticks);

	Sleep(mSecs);
}

#else	/*** UNIX ***/


#include <sys/time.h>


void	DS_Sleep(PRIntervalTime ticks)
{
long mSecs = PR_IntervalToMilliseconds(ticks);
struct timeval tm;

	tm.tv_sec = mSecs / 1000;
	tm.tv_usec = (mSecs % 1000) * 1000;

	select(0,NULL,NULL,NULL,&tm);
}

#endif


/*****************************************************************************
 * strarray2str(): convert the array of strings in "a" into a single
 * space-separated string like:
 *		str1 str2 str3
 * If buf is too small, the result will be truncated and end with "...".
 * If include_quotes is non-zero, double quote marks are included around
 * the string, e.g.,
 *		"str2 str2 str3"
 *
 * Returns: 0 if completely successful and -1 if result is truncated.
 */
int
strarray2str( char **a, char *buf, size_t buflen, int include_quotes )
{
	int		rc = 0;		/* optimistic */
	char	*p = buf;
	size_t	totlen = 0;


	if ( include_quotes ) {
		if ( buflen < 3 ) {
			return -1;		/* not enough room for the quote marks! */
		}
		*p++ = '"';
		++totlen;
	}

	if ( NULL != a ) {
		int ii;
		size_t len = 0;
		for ( ii = 0; a[ ii ] != NULL; ii++ ) {
			if ( ii > 0 ) {
				*p++ = ' ';
				totlen++;
			}
			len = strlen( a[ ii ]);
			if ( totlen + len > buflen - 5 ) {
				strcpy ( p, "..." );
				p += 3;
				totlen += 3;
				rc = -1;
				break;		/* result truncated */
			} else {
				strcpy( p, a[ ii ]);
				p += len;
				totlen += len;
			}
		}
	}

	if ( include_quotes ) {
		*p++ = '"';
		++totlen;
	}
	buf[ totlen ] = '\0';

	return( rc );
}
/*****************************************************************************/

/* Changes the ownership of the given file/directory if not
   already the owner
   Returns 0 upon success or non-zero otherwise, usually -1 if
   some system error occurred
*/
#ifndef _WIN32
int
slapd_chown_if_not_owner(const char *filename, uid_t uid, gid_t gid)
{
        struct stat statbuf;
        int result = 1;
        if (!filename)
                return result;

        memset(&statbuf, '\0', sizeof(statbuf));
        if (!(result = stat(filename, &statbuf)))
        {
                if (((uid != -1) && (uid != statbuf.st_uid)) ||
                        ((gid != -1) && (gid != statbuf.st_gid)))
                {
                        result = chown(filename, uid, gid);
                }
        }

        return result;
}
#endif

/*
 * Compare 2 pathes
 * Paths could contain ".", "..", "//" in the path, thus normalize them first.
 * One or two of the paths could be a relative path.
 */
int
slapd_comp_path(char *p0, char *p1)
{
	int rval = 0;
	char *norm_p0 = rel2abspath(p0);
	char *norm_p1 = rel2abspath(p1);

	rval = strcmp(norm_p0, norm_p1);
	slapi_ch_free_string(&norm_p0);
	slapi_ch_free_string(&norm_p1);
	return rval;
}

#ifdef MEMPOOL_EXPERIMENTAL
void _free_wrapper(void *ptr)
{
    slapi_ch_free(&ptr);
}
#endif

/*
 * Function: slapi_ldap_unbind()
 * Purpose: release an LDAP session obtained from a call to slapi_ldap_init().
 */
void
slapi_ldap_unbind( LDAP *ld )
{
    if ( ld != NULL ) {
	ldap_unbind( ld );
    }
}

const char *
slapi_urlparse_err2string( int err )
{
    const char *s="internal error";

    switch( err ) {
    case 0:
	s = "no error";
	break;
    case LDAP_URL_ERR_NOTLDAP:
	s = "missing ldap:// or ldaps:// or ldapi://";
	break;
    case LDAP_URL_ERR_NODN:
	s = "missing suffix";
	break;
    case LDAP_URL_ERR_BADSCOPE:
	s = "invalid search scope";
	break;
    case LDAP_URL_ERR_MEM:
	s = "unable to allocate memory";
	break;
    case LDAP_URL_ERR_PARAM:
	s = "bad parameter to an LDAP URL function";
	break;
    }

    return( s );
}

#include <sasl.h>

/*
  Perform LDAP init and return an LDAP* handle.  If ldapurl is given,
  that is used as the basis for the protocol, host, port, and whether
  to use starttls (given on the end as ldap://..../?????starttlsOID
  If hostname is given, LDAP or LDAPS is assumed, and this will override
  the hostname from the ldapurl, if any.  If port is > 0, this is the
  port number to use.  It will override the port in the ldapurl, if any.
  If no port is given in port or ldapurl, the default will be used based
  on the secure setting (389 for ldap, 636 for ldaps, 389 for starttls)
  secure takes 1 of 3 values - 0 means regular ldap, 1 means ldaps, 2
  means regular ldap with starttls.
  filename is the ldapi file name - if this is given, and no other options
  are given, ldapi is assumed.
 */
/* util_sasl_path: the string argument for putenv.
   It must be a global or a static */
char util_sasl_path[MAXPATHLEN];

LDAP *
slapi_ldap_init_ext(
    const char *ldapurl, /* full ldap url */
    const char *hostname, /* can also use this to override
			     host in url */
    int port, /* can also use this to override port in url */
    int secure, /* 0 for ldap, 1 for ldaps, 2 for starttls -
		   override proto in url */
    int shared, /* if true, LDAP* will be shared among multiple threads */
    const char *filename /* for ldapi */
)
{
    LDAPURLDesc	*ludp = NULL;
    LDAP *ld = NULL;
    int rc = 0;

    /* We need to provide a sasl path used for client connections, especially
       if the server is not set up to be a sasl server - since mozldap provides
       no way to override the default path programatically, we set the sasl
       path to the environment variable SASL_PATH. */
    char *configpluginpath = config_get_saslpath();
    char *pluginpath = configpluginpath;
    char *pp = NULL;

    if (NULL == pluginpath || (*pluginpath == '\0')) {
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext",
			"configpluginpath == NULL\n");
        if (!(pluginpath = getenv("SASL_PATH"))) {
#if defined(LINUX) && defined(__LP64__)
            pluginpath = "/usr/lib64/sasl2";
#else
            pluginpath = "/usr/lib/sasl2";
#endif
        }
    }
    if ('\0' == util_sasl_path[0] || /* first time */
        NULL == (pp = strchr(util_sasl_path, '=')) || /* invalid arg for putenv */
        (0 != strcmp(++pp, pluginpath)) /* sasl_path has been updated */ ) {
        PR_snprintf(util_sasl_path, sizeof(util_sasl_path),
                                        "SASL_PATH=%s", pluginpath);
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext",
			"putenv(%s)\n", util_sasl_path);
        putenv(util_sasl_path);
    }
    slapi_ch_free_string(&configpluginpath);

    /* if ldapurl is given, parse it */
    if (ldapurl && ((rc = ldap_url_parse_no_defaults(ldapurl, &ludp, 0)) ||
		    !ludp)) {
	slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
			"Could not parse given LDAP URL [%s] : error [%s]\n",
			ldapurl ? ldapurl : "NULL",
			slapi_urlparse_err2string(rc));
	goto done;
    }

    /* use url host if no host given */
    if (!hostname && ludp && ludp->lud_host) {
	hostname = ludp->lud_host;
    }

    /* use url port if no port given */
    if (!port && ludp && ludp->lud_port) {
	port = ludp->lud_port;
    }

    /* use secure setting from url if none given */
    if (!secure && ludp) {
	if (ludp->lud_options & LDAP_URL_OPT_SECURE) {
	    secure = 1;
	} else if (0/* starttls option - not supported yet in LDAP URLs */) {
	    secure = 2;
	}
    }

    /* ldap_url_parse doesn't yet handle ldapi */
    /*
      if (!filename && ludp && ludp->lud_file) {
      filename = ludp->lud_file;
      }
    */

#ifdef MEMPOOL_EXPERIMENTAL
    {
    /* 
     * slapi_ch_malloc functions need to be set to LDAP C SDK
     */
    struct ldap_memalloc_fns memalloc_fns;
    memalloc_fns.ldapmem_malloc = (LDAP_MALLOC_CALLBACK *)slapi_ch_malloc;
    memalloc_fns.ldapmem_calloc = (LDAP_CALLOC_CALLBACK *)slapi_ch_calloc;
    memalloc_fns.ldapmem_realloc = (LDAP_REALLOC_CALLBACK *)slapi_ch_realloc;
    memalloc_fns.ldapmem_free = (LDAP_FREE_CALLBACK *)_free_wrapper;
    }
    /* 
     * MEMPOOL_EXPERIMENTAL: 
     * These LDAP C SDK init function needs to be revisited.
     * In ldap_init called via ldapssl_init and prldap_init initializes
     * options and set default values including memalloc_fns, then it
     * initializes as sasl client by calling sasl_client_init.  In
     * sasl_client_init, it creates mechlist using the malloc function
     * available at the moment which could mismatch the malloc/free functions
     * set later.
     */
#endif
    if (filename) {
	/* ldapi in mozldap client is not yet supported */
    } else if (secure == 1) {
	ld = ldapssl_init(hostname, port, secure);
    } else { /* regular ldap and/or starttls */
	/*
	 * Leverage the libprldap layer to take care of all the NSPR
	 * integration.
	 * Note that ldapssl_init() uses libprldap implicitly.
	 */
	ld = prldap_init(hostname, port, shared);
    }

    /* Update snmp interaction table */
    if (hostname) {
	if (ld == NULL) {
	    set_snmp_interaction_row((char *)hostname, port, -1);
	} else {
	    set_snmp_interaction_row((char *)hostname, port, 0);
	}
    }

    if ((ld != NULL) && !filename) {
	/*
	 * Set the outbound LDAP I/O timeout based on the server config.
	 */
	int io_timeout_ms = config_get_outbound_ldap_io_timeout();
	if (io_timeout_ms > 0) {
	    if (prldap_set_session_option(ld, NULL, PRLDAP_OPT_IO_MAX_TIMEOUT,
					  io_timeout_ms) != LDAP_SUCCESS) {
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
				"failed: unable to set outbound I/O "
				"timeout to %dms\n",
				io_timeout_ms);
		slapi_ldap_unbind(ld);
		ld = NULL;
		goto done;
	    }
	}

	/*
	 * Set SSL strength (server certificate validity checking).
	 */
	if (secure > 0) {
	    int ssl_strength = 0;
	    LDAP *myld = NULL;

	    if (config_get_ssl_check_hostname()) {
		/* check hostname against name in certificate */
		ssl_strength = LDAPSSL_AUTH_CNCHECK;
	    } else {
		/* verify certificate only */
		ssl_strength = LDAPSSL_AUTH_CERT;
	    }

	    /* we can only use the set functions below with a real
	       LDAP* if it has already gone through ldapssl_init -
	       so, use NULL if using starttls */
	    if (secure == 1) {
		myld = ld;
	    }

	    if ((rc = ldapssl_set_strength(myld, ssl_strength)) ||
		(rc = ldapssl_set_option(myld, SSL_ENABLE_SSL2, PR_FALSE)) ||
		(rc = ldapssl_set_option(myld, SSL_ENABLE_SSL3, PR_TRUE)) ||
		(rc = ldapssl_set_option(myld, SSL_ENABLE_TLS, PR_TRUE))) {
		int prerr = PR_GetError();

		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
				"failed: unable to set SSL options ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				prerr, slapd_pr_strerror(prerr));

	    }
	    if (secure == 1) {
		/* tell bind code we are using SSL */
		ldap_set_option(ld, LDAP_OPT_SSL, LDAP_OPT_ON);
	    }
	}
    }

    if (ld && (secure == 2)) {
	/* We don't have a way to stash context data with the LDAP*, so we
	   stash the information in the client controls (currently unused).
	   We don't want to open the connection in ldap_init, since that's
	   not the semantic - the connection is not usually opened until
	   the first operation is sent, which is usually the bind - or
	   in this case, the start_tls - so we stash the start_tls so
	   we can do it in slapi_ldap_bind - note that this will get
	   cleaned up when the LDAP* is disposed of
	*/
	LDAPControl start_tls_dummy_ctrl;
	LDAPControl **clientctrls = NULL;

	/* returns copy of controls */
	ldap_get_option(ld, LDAP_OPT_CLIENT_CONTROLS, &clientctrls);

	start_tls_dummy_ctrl.ldctl_oid = slapi_ch_strdup(START_TLS_OID);
	start_tls_dummy_ctrl.ldctl_value.bv_val = NULL;
	start_tls_dummy_ctrl.ldctl_value.bv_len = 0;
	start_tls_dummy_ctrl.ldctl_iscritical = 0;
	slapi_add_control_ext(&clientctrls, &start_tls_dummy_ctrl, 1);
	/* set option frees old list and copies the new list */
	ldap_set_option(ld, LDAP_OPT_CLIENT_CONTROLS, clientctrls);
	ldap_controls_free(clientctrls); /* free the copy */
    }

    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext",
		    "Success: set up conn to [%s:%d]%s\n",
		    hostname, port,
		    (secure == 2) ? " using startTLS" :
		    ((secure == 1) ? " using SSL" : ""));
done:
    ldap_free_urldesc(ludp);

    return( ld );
}

/*
 * Function: slapi_ldap_init()
 * Description: just like ldap_ssl_init() but also arranges for the LDAP
 *	session handle returned to be safely shareable by multiple threads
 *	if "shared" is non-zero.
 * Returns:
 *	an LDAP session handle (NULL if some local error occurs).
 */
LDAP *
slapi_ldap_init( char *ldaphost, int ldapport, int secure, int shared )
{
    return slapi_ldap_init_ext(NULL, ldaphost, ldapport, secure, shared, NULL);
}

/*
 * Does the correct bind operation simple/sasl/cert depending
 * on the arguments passed in.  If the user specified to use
 * starttls in init, this will do the starttls first.  If using
 * ssl or client cert auth, this will initialize the client side
 * of that.
 */
int
slapi_ldap_bind(
    LDAP *ld, /* ldap connection */
    const char *bindid, /* usually a bind DN for simple bind */
    const char *creds, /* usually a password for simple bind */
    const char *mech, /* name of mechanism */
    LDAPControl **serverctrls, /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    struct timeval *timeout, /* timeout */
    int *msgidp /* pass in non-NULL for async handling */
)
{
    int rc = LDAP_SUCCESS;
    LDAPControl **clientctrls = NULL;
    int secure = 0;
    struct berval bvcreds = {0, NULL};
    LDAPMessage *result = NULL;
    struct berval *servercredp = NULL;

    /* do starttls if requested
       NOTE - starttls is an extop, not a control, but we don't have
       a place we can stash this information in the LDAP*, other
       than the currently unused clientctrls */
    ldap_get_option(ld, LDAP_OPT_CLIENT_CONTROLS, &clientctrls);
    if (clientctrls && clientctrls[0] &&
	slapi_control_present(clientctrls, START_TLS_OID, NULL, NULL)) {
	secure = 2;
    } else {
	ldap_get_option(ld, LDAP_OPT_SSL, &secure);
    }

    if ((secure > 0) && mech && !strcmp(mech, LDAP_SASL_EXTERNAL)) {
	/* SSL connections will use the server's security context
	   and cert for client auth */
	rc = slapd_SSL_client_auth(ld);

	if (rc != 0) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not configure the server for cert "
			    "auth - error %d - make sure the server is "
			    "correctly configured for SSL/TLS\n", rc);
	    goto done;
	} else {
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			    "Set up conn to use client auth\n");
        }
	bvcreds.bv_val = NULL; /* ignore username and passed in creds */
	bvcreds.bv_len = 0; /* for external auth */
	bindid = NULL;
    } else { /* other type of auth */
	bvcreds.bv_val = (char *)creds;
	bvcreds.bv_len = creds ? strlen(creds) : 0;
    }

    if (secure == 2) { /* send start tls */
	rc = ldap_start_tls_s(ld, NULL /* serverctrls?? */, NULL);
	if (LDAP_SUCCESS != rc) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not send startTLS request: "
			    "error %d (%s)\n",
			    rc, ldap_err2string(rc));
	    goto done;
	}
	slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			"startTLS started on connection\n");
    }

    /* The connection has been set up - now do the actual bind, depending on
       the mechanism and arguments */
    if (!mech || (mech == LDAP_SASL_SIMPLE) ||
	!strcmp(mech, LDAP_SASL_EXTERNAL)) {
	int mymsgid = 0;

	slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			"attempting %s bind with id [%s] creds [%s]\n",
			mech ? mech : "SIMPLE",
			bindid, creds);
	if ((rc = ldap_sasl_bind(ld, bindid, mech, &bvcreds, serverctrls,
				 NULL /* clientctrls */, &mymsgid))) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not send bind request for id "
			    "[%s] mech [%s]: error %d (%s) %d (%s) %d (%s)\n",
			    bindid ? bindid : "(anon)",
			    mech ? mech : "SIMPLE",
			    rc, ldap_err2string(rc),
			    PR_GetError(), slapd_pr_strerror(PR_GetError()),
			    errno, slapd_system_strerror(errno));
	    goto done;
	}

	if (msgidp) { /* let caller process result */
	    *msgidp = mymsgid;
	} else { /* process results */
	    rc = ldap_result(ld, mymsgid, LDAP_MSG_ALL, timeout, &result);
	    if (-1 == rc) { /* error */
		rc = ldap_get_lderrno(ld, NULL, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error reading bind response for id "
				"[%s] mech [%s]: error %d (%s)\n",
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE",
				rc, ldap_err2string(rc));
		goto done;
	    } else if (rc == 0) { /* timeout */
		rc = LDAP_TIMEOUT;
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error: timeout after [%ld.%ld] seconds reading "
				"bind response for [%s] mech [%s]\n",
				timeout ? timeout->tv_sec : 0,
				timeout ? timeout->tv_usec : 0,
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE");
		goto done;
	    }
	    /* if we got here, we were able to read success result */
	    /* Get the controls sent by the server if requested */
	    if (returnedctrls) {
                if ((rc = ldap_parse_result(ld, result, &rc, NULL, NULL,
					    NULL, returnedctrls,
					    0)) != LDAP_SUCCESS) {
		    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				    "Error: could not bind id "
				    "[%s] mech [%s]: error %d (%s)\n",
				    bindid ? bindid : "(anon)",
				    mech ? mech : "SIMPLE",
				    rc, ldap_err2string(rc));
		    goto done;
		}
	    }

	    /* parse the bind result and get the ldap error code */
	    if ((rc = ldap_parse_sasl_bind_result(ld, result, &servercredp,
						  0)) ||
		(rc = ldap_result2error(ld, result, 0))) {
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error: could not read bind results for id "
				"[%s] mech [%s]: error %d (%s)\n",
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE",
				rc, ldap_err2string(rc));
		goto done;
	    }
	}
    } else {
	/* a SASL mech - set the sasl ssf to 0 if using TLS/SSL */
	if (secure) {
	    sasl_ssf_t max_ssf = 0;
	    ldap_set_option(ld, LDAP_OPT_X_SASL_SSF_MAX, &max_ssf);
	}
	rc = slapd_ldap_sasl_interactive_bind(ld, bindid, creds, mech,
					      serverctrls, returnedctrls,
					      msgidp);
	if (LDAP_SUCCESS != rc) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not perform interactive bind for id "
			    "[%s] mech [%s]: error %d (%s)\n",
			    bindid ? bindid : "(anon)",
			    mech ? mech : "SIMPLE",
			    rc, ldap_err2string(rc));
	}
    }

done:
    slapi_ch_bvfree(&servercredp);
    ldap_msgfree(result);

    return rc;
}

/* the following implements the client side of sasl bind, for LDAP server
   -> LDAP server SASL */

typedef struct {
    char *mech;
    char *authid;
    char *username;
    char *passwd;
    char *realm;
} ldapSaslInteractVals;

#ifdef HAVE_KRB5
static void set_krb5_creds(
    const char *authid,
    const char *username,
    const char *passwd,
    const char *realm,
    ldapSaslInteractVals *vals
);
#endif

static void *
ldap_sasl_set_interact_vals(LDAP *ld, const char *mech, const char *authid,
			    const char *username, const char *passwd,
			    const char *realm)
{
    ldapSaslInteractVals *vals = NULL;
    char *idprefix = "";

    vals = (ldapSaslInteractVals *)
        slapi_ch_calloc(1, sizeof(ldapSaslInteractVals));

    if (!vals) {
        return NULL;
    }

    if (mech) {
        vals->mech = slapi_ch_strdup(mech);
    } else {
        ldap_get_option(ld, LDAP_OPT_X_SASL_MECH, &vals->mech);
    }

    if (vals->mech && !strcasecmp(vals->mech, "DIGEST-MD5")) {
        idprefix = "dn:"; /* prefix name and id with this string */
    }

    if (authid) { /* use explicit passed in value */
        vals->authid = slapi_ch_smprintf("%s%s", idprefix, authid);
    } else { /* use option value if any */
        ldap_get_option(ld, LDAP_OPT_X_SASL_AUTHCID, &vals->authid);
        if (!vals->authid) {
/* get server user id? */
            vals->authid = slapi_ch_strdup("");
        }
    }

    if (username) { /* use explicit passed in value */
        vals->username = slapi_ch_smprintf("%s%s", idprefix, username);
    } else { /* use option value if any */
        ldap_get_option(ld, LDAP_OPT_X_SASL_AUTHZID, &vals->username);
        if (!vals->username) { /* use default sasl value */
            vals->username = slapi_ch_strdup("");
        }
    }

    if (passwd) {
        vals->passwd = slapi_ch_strdup(passwd);
    } else {
        vals->passwd = slapi_ch_strdup("");
    }

    if (realm) {
        vals->realm = slapi_ch_strdup(realm);
    } else {
        ldap_get_option(ld, LDAP_OPT_X_SASL_REALM, &vals->realm);
        if (!vals->realm) { /* use default sasl value */
            vals->realm = slapi_ch_strdup("");
        }
    }

#ifdef HAVE_KRB5
    if (mech && !strcmp(mech, "GSSAPI")) {
        set_krb5_creds(authid, username, passwd, realm, vals);
    }
#endif /* HAVE_KRB5 */

    return vals;
}

static void
ldap_sasl_free_interact_vals(void *defaults)
{
    ldapSaslInteractVals *vals = defaults;

    if (vals) {
        slapi_ch_free_string(&vals->mech);
        slapi_ch_free_string(&vals->authid);
        slapi_ch_free_string(&vals->username);
        slapi_ch_free_string(&vals->passwd);
        slapi_ch_free_string(&vals->realm);
        slapi_ch_free(&defaults);
    }
}

static int 
ldap_sasl_get_val(ldapSaslInteractVals *vals, sasl_interact_t *interact, unsigned flags)
{
    const char	*defvalue = interact->defresult;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing */

    if (vals != NULL) {
        switch(interact->id) {
        case SASL_CB_AUTHNAME:
            defvalue = vals->authid;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_AUTHNAME\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_USER:
            defvalue = vals->username;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_USER\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_PASS:
            defvalue = vals->passwd;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_PASS\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_GETREALM:
            defvalue = vals->realm;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_GETREALM\n",
                            defvalue ? defvalue : "(null)");
            break;
        }
    }

    if (defvalue != NULL) {
        interact->result = defvalue;
        if ((char *)interact->result == NULL)
            return (LDAP_NO_MEMORY);
        interact->len = strlen((char *)(interact->result));
    }
    return (LDAP_SUCCESS);
}

static int
ldap_sasl_interact_cb(LDAP *ld, unsigned flags, void *defaults, void *prompts)
{
    sasl_interact_t *interact = NULL;
    ldapSaslInteractVals *sasldefaults = defaults;
    int rc;

    if (prompts == NULL) {
        return (LDAP_PARAM_ERROR);
    }

    for (interact = prompts; interact->id != SASL_CB_LIST_END; interact++) {
        /* Obtain the default value */
        if ((rc = ldap_sasl_get_val(sasldefaults, interact, flags)) != LDAP_SUCCESS) {
            return (rc);
        }
    }

    return (LDAP_SUCCESS);
}

/* figure out from the context and this error if we should
   attempt to retry the bind */
static int
can_retry_bind(LDAP *ld, const char *mech, const char *bindid,
               const char *creds, int rc, const char *errmsg)
{
    int localrc = 0;
    if (errmsg && strstr(errmsg, "Ticket expired")) {
        localrc = 1;
    }

    return localrc;
}

int
slapd_ldap_sasl_interactive_bind(
    LDAP *ld, /* ldap connection */
    const char *bindid, /* usually a bind DN for simple bind */
    const char *creds, /* usually a password for simple bind */
    const char *mech, /* name of mechanism */
    LDAPControl **serverctrls, /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    int *msgidp /* pass in non-NULL for async handling */
)
{
    int rc = LDAP_SUCCESS;
    int tries = 0;

    while (tries < 2) {
        void *defaults = ldap_sasl_set_interact_vals(ld, mech, bindid, bindid,
                                                     creds, NULL);
        /* have to first set the defaults used by the callback function */
        /* call the bind function */
        rc = ldap_sasl_interactive_bind_ext_s(ld, bindid, mech, serverctrls,
                                              NULL, LDAP_SASL_QUIET,
                                              ldap_sasl_interact_cb, defaults,
                                              returnedctrls);
        ldap_sasl_free_interact_vals(defaults);
        if (LDAP_SUCCESS != rc) {
            char *errmsg = NULL;
            rc = ldap_get_lderrno(ld, NULL, &errmsg);
            slapi_log_error(SLAPI_LOG_FATAL, "slapd_ldap_sasl_interactive_bind",
                            "Error: could not perform interactive bind for id "
                            "[%s] mech [%s]: error %d (%s) (%s)\n",
                            bindid ? bindid : "(anon)",
                            mech ? mech : "SIMPLE",
                            rc, ldap_err2string(rc), errmsg);
            if (can_retry_bind(ld, mech, bindid, creds, rc, errmsg)) {
                ; /* pass through to retry one time */
            } else {
                break; /* done - fail - cannot retry */
            }
        } else {
            break; /* done - success */
        }
        tries++;
    }

    return rc;
}

#ifdef HAVE_KRB5
#include <krb5.h>

/* for some reason this is not in the public API?
   but it is documented e.g. man kinit */
#ifndef KRB5_ENV_CCNAME
#define KRB5_ENV_CCNAME "KRB5CCNAME"
#endif

static void
show_one_credential(int authtracelevel,
                    krb5_context ctx, krb5_creds *cred)
{
    char *logname = "show_one_credential";
    krb5_error_code rc;
    char *name = NULL, *sname = NULL;
    char startts[BUFSIZ], endts[BUFSIZ], renewts[BUFSIZ];

    if ((rc = krb5_unparse_name(ctx, cred->client, &name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get client name from credential: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }
    if ((rc = krb5_unparse_name(ctx, cred->server, &sname))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get server name from credential: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }
    if (!cred->times.starttime) {
        cred->times.starttime = cred->times.authtime;
    }
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.starttime,
                               startts, sizeof(startts), NULL);
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.endtime,
                               endts, sizeof(endts), NULL);
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.renew_till,
                               renewts, sizeof(renewts), NULL);

    slapi_log_error(authtracelevel, logname,
                    "\tKerberos credential: client [%s] server [%s] "
                    "start time [%s] end time [%s] renew time [%s] "
                    "flags [0x%x]\n", name, sname, startts, endts,
                    renewts, (uint32_t)cred->ticket_flags);

cleanup:
    krb5_free_unparsed_name(ctx, name);
    krb5_free_unparsed_name(ctx, sname);

    return;
}

/*
 * Call this after storing the credentials in the cache
 */
static void
show_cached_credentials(int authtracelevel,
                        krb5_context ctx, krb5_ccache cc,
                        krb5_principal princ)
{
    char *logname = "show_cached_credentials";
    krb5_error_code rc = 0;
    krb5_creds creds;
    krb5_cc_cursor cur;
    char *princ_name = NULL;

    if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get principal name from principal: %d (%s)\n",
                        rc, error_message(rc));
	    goto cleanup;
    }

	slapi_log_error(authtracelevel, logname,
                    "Ticket cache: %s:%s\nDefault principal: %s\n\n",
                    krb5_cc_get_type(ctx, cc),
                    krb5_cc_get_name(ctx, cc), princ_name);

    if ((rc = krb5_cc_start_seq_get(ctx, cc, &cur))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get cursor to iterate cached credentials: "
                        "%d (%s)\n", rc, error_message(rc));
        goto cleanup;
    }

    while (!(rc = krb5_cc_next_cred(ctx, cc, &cur, &creds))) {
        show_one_credential(authtracelevel, ctx, &creds);
        krb5_free_cred_contents(ctx, &creds);
    }
    if (rc == KRB5_CC_END) {
        if ((rc = krb5_cc_end_seq_get(ctx, cc, &cur))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not close cached credentials cursor: "
                            "%d (%s)\n", rc, error_message(rc));
            goto cleanup;
        }
	}

cleanup:
	krb5_free_unparsed_name(ctx, princ_name);

    return;
}

static int
looks_like_a_dn(const char *username)
{
    return (username && strchr(username, '='));
}

static int
credentials_are_valid(
    krb5_context ctx,
    krb5_ccache cc,
    krb5_principal princ,
    const char *princ_name, 
    int *rc
)
{
    char *logname = "credentials_are_valid";
    int myrc = 0;
    krb5_creds mcreds; /* match these values */
    krb5_creds creds; /* returned creds */
    char *tgs_princ_name = NULL;
    krb5_timestamp currenttime;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing */
    int realm_len;
    char *realm_str;
    int time_buffer = 30; /* seconds - go ahead and renew if creds are
                             about to expire  */

    memset(&mcreds, 0, sizeof(mcreds));
    memset(&creds, 0, sizeof(creds));
    *rc = 0;
    if (!cc) {
        /* ok - no error */
        goto cleanup;
    }

    /* have to construct the tgs server principal in
       order to set mcreds.server required in order
       to use krb5_cc_retrieve_creds() */
    /* get default realm first */
    realm_len = krb5_princ_realm(ctx, princ)->length;
    realm_str = krb5_princ_realm(ctx, princ)->data;
    tgs_princ_name = slapi_ch_smprintf("%s/%*s@%*s", KRB5_TGS_NAME,
                                       realm_len, realm_str,
                                       realm_len, realm_str);

    if ((*rc = krb5_parse_name(ctx, tgs_princ_name, &mcreds.server))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could parse principal [%s]: %d (%s)\n",
                        tgs_princ_name, *rc, error_message(*rc));
        goto cleanup;
    }

    mcreds.client = princ;
    if ((*rc = krb5_cc_retrieve_cred(ctx, cc, 0, &mcreds, &creds))) {
        if (*rc == KRB5_CC_NOTFOUND) {
            /* ok - no creds for this princ in the cache */
            *rc = 0;
        }
        goto cleanup;
    }

    /* have the creds - now look at the timestamp */
    if ((*rc = krb5_timeofday(ctx, &currenttime))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get current time: %d (%s)\n",
                        *rc, error_message(*rc));
        goto cleanup;
    }

    if (currenttime > (creds.times.endtime + time_buffer)) {
        slapi_log_error(authtracelevel, logname,
                        "Credentials for [%s] have expired or will soon "
                        "expire - now [%d] endtime [%d]\n", princ_name,
                        currenttime, creds.times.endtime);
        goto cleanup;
    }

    myrc = 1; /* credentials are valid */
cleanup:
   	krb5_free_cred_contents(ctx, &creds);
    slapi_ch_free_string(&tgs_princ_name);
    if (mcreds.server) {
        krb5_free_principal(ctx, mcreds.server);
    }

    return myrc;
}

/*
 * This implementation assumes that we want to use the 
 * keytab from the default keytab env. var KRB5_KTNAME
 * as.  This code is very similar to kinit -k -t.  We
 * get a krb context, get the default keytab, get
 * the credentials from the keytab, authenticate with
 * those credentials, create a ccache, store the
 * credentials in the ccache, and set the ccache
 * env var to point to those credentials.
 */
static void
set_krb5_creds(
    const char *authid,
    const char *username,
    const char *passwd,
    const char *realm,
    ldapSaslInteractVals *vals
)
{
    char *logname = "set_krb5_creds";
    const char *cc_type = "MEMORY"; /* keep cred cache in memory */
    krb5_context ctx = NULL;
    krb5_ccache cc = NULL;
    krb5_principal princ = NULL;
    char *princ_name = NULL;
    krb5_error_code rc = 0;
    krb5_creds creds;
    krb5_keytab kt = NULL;
    char *cc_name = NULL;
    char ktname[MAX_KEYTAB_NAME_LEN];
    static char cc_env_name[1024+32]; /* size from ccdefname.c */
    int new_ccache = 0;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing 
                                             not sure what shell was
                                             used for, does not
                                             appear to be used 
                                             currently */

    /* probably have to put a mutex around this whole thing, to avoid
       problems with reentrancy, since we are setting a "global"
       variable via an environment variable */

    /* wipe this out so we can safely free it later if we
       short circuit */
    memset(&creds, 0, sizeof(creds));

    /* initialize the kerberos context */
    if ((rc = krb5_init_context(&ctx))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not init Kerberos context: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* see if there is already a ccache, and see if there are
       creds in the ccache */
    /* grab the default ccache - note: this does not open the cache */
    if ((rc = krb5_cc_default(ctx, &cc))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get default Kerberos ccache: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* use this cache - construct the full cache name */
    cc_name = slapi_ch_smprintf("%s:%s", krb5_cc_get_type(ctx, cc),
                                krb5_cc_get_name(ctx, cc));

    /* grab the principal from the ccache - will fail if there
       is no ccache */
    if ((rc = krb5_cc_get_principal(ctx, cc, &princ))) {
        if (KRB5_FCC_NOFILE == rc) { /* no cache - ok */
            slapi_log_error(authtracelevel, logname,
                            "The default credentials cache [%s] not found: "
                            "will create a new one.\n", cc_name);
            /* close the cache - we will create a new one below */
            krb5_cc_close(ctx, cc);
            cc = NULL;
            slapi_ch_free_string(&cc_name);
            /* fall through to the keytab auth code below */
        } else { /* fatal */
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not open default Kerberos ccache [%s]: "
                            "%d (%s)\n", cc_name, rc, error_message(rc));
            goto cleanup;
        }
    } else { /* have a valid ccache && found principal */
        if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Unable to get name of principal from ccache [%s]: "
                            "%d (%s)\n", cc_name, rc, error_message(rc));
            goto cleanup;
        }
        slapi_log_error(authtracelevel, logname,
                        "Using principal [%s] from ccache [%s]\n",
                        princ_name, cc_name);
    }

    /* if this is not our type of ccache, there is nothing more we can
       do - just punt and let sasl/gssapi take it's course - this
       usually means there has been an external kinit e.g. in the
       start up script, and it is the responsibility of the script to
       renew those credentials or face lots of sasl/gssapi failures
       This means, however, that the caller MUST MAKE SURE THERE IS NO
       DEFAULT CCACHE FILE or the server will attempt to use it (and
       likely fail) - THERE MUST BE NO DEFAULT CCACHE FILE IF YOU WANT
       THE SERVER TO AUTHENTICATE WITH THE KEYTAB
       NOTE: cc types are case sensitive and always upper case */
    if (cc && strcmp(cc_type, krb5_cc_get_type(ctx, cc))) {
        static int errmsgcounter = 0;
        int loglevel = SLAPI_LOG_FATAL;
        if (errmsgcounter) {
            loglevel = authtracelevel;
        }
        /* make sure we log this message once, in case the user has
           done something unintended, we want to make sure they know
           about it.  However, if the user knows what he/she is doing,
           by using an external ccache file, they probably don't want
           to be notified with an error every time. */
        slapi_log_error(loglevel, logname,
                        "The server will use the external SASL/GSSAPI "
                        "credentials cache [%s:%s].  If you want the "
                        "server to automatically authenticate with its "
                        "keytab, you must remove this cache.  If you "
                        "did not intend to use this cache, you will likely "
                        "see many SASL/GSSAPI authentication failures.\n",
                        krb5_cc_get_type(ctx, cc), krb5_cc_get_name(ctx, cc));
        errmsgcounter++;
        goto cleanup;
    }

    /* need to figure out which principal to use
       1) use the one from the ccache
       2) use username
       3) construct one in the form ldap/fqdn@REALM
    */
    if (!princ && username && !looks_like_a_dn(username) &&
        (rc = krb5_parse_name(ctx, username, &princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Error: could not convert [%s] into a kerberos "
                        "principal: %d (%s)\n", username,
                        rc, error_message(rc));
        goto cleanup;
    }

    /* if still no principal, construct one */
    if (!princ &&
        (rc = krb5_sname_to_principal(ctx, NULL, "ldap",
                                      KRB5_NT_SRV_HST, &princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Error: could not construct ldap service "
                        "principal: %d (%s)\n", rc, error_message(rc));
        goto cleanup;
    }

    if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get name of principal: "
                        "%d (%s)\n", rc, error_message(rc));
        goto cleanup;
    }

    slapi_log_error(authtracelevel, logname,
                    "Using principal named [%s]\n", princ_name);

    /* grab the credentials from the ccache, if any -
       if the credentials are still valid, we do not have
       to authenticate again */
    if (credentials_are_valid(ctx, cc, princ, princ_name, &rc)) {
        slapi_log_error(authtracelevel, logname,
                        "Credentials for principal [%s] are still "
                        "valid - no auth is necessary.\n",
                        princ_name);
        goto cleanup;
    } else if (rc) { /* some error other than "there are no credentials" */
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to verify cached credentials for "
                        "principal [%s]: %d (%s)\n", princ_name,
                        rc, error_message(rc));
        goto cleanup;
    }      

    /* find our default keytab */
    if ((rc = krb5_kt_default(ctx, &kt))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get default keytab: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* get name of keytab for debugging purposes */
    if ((rc = krb5_kt_get_name(ctx, kt, ktname, sizeof(ktname)))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get name of default keytab: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    slapi_log_error(authtracelevel, logname,
                    "Using keytab named [%s]\n", ktname);

    /* now do the actual kerberos authentication using
       the keytab, and get the creds */
    rc = krb5_get_init_creds_keytab(ctx, &creds, princ, kt,
                                    0, NULL, NULL);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get initial credentials for principal [%s] "
                        "in keytab [%s]: %d (%s)\n",
                        princ_name, ktname, rc, error_message(rc));
        goto cleanup;
    }

    /* completely done with the keytab now, close it */
    krb5_kt_close(ctx, kt);
    kt = NULL; /* no double free */

    /* we now have the creds and the principal to which the
       creds belong - use or allocate a new memory based
       cache to hold the creds */
    if (!cc_name) {
#if HAVE_KRB5_CC_NEW_UNIQUE
        /* krb5_cc_new_unique is a new convenience function which
           generates a new unique name and returns a memory
           cache with that name */
        if ((rc = krb5_cc_new_unique(ctx, cc_type, NULL, &cc))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not create new unique memory ccache: "
                            "%d (%s)\n",
                            rc, error_message(rc));
            goto cleanup;
        }
        cc_name = slapi_ch_smprintf("%s:%s", cc_type,
                                    krb5_cc_get_name(ctx, cc));
#else
        /* store the cache in memory - krb5_init_context uses malloc
           to create the ctx, so the address should be unique enough
           for our purposes */
        if (!(cc_name = slapi_ch_smprintf("%s:%p", cc_type, ctx))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could create Kerberos memory ccache: "
                            "out of memory\n");
            rc = 1;
            goto cleanup;
        }
#endif
        slapi_log_error(authtracelevel, logname,
                        "Generated new memory ccache [%s]\n", cc_name);
        new_ccache = 1; /* need to set this in env. */
    } else {
        slapi_log_error(authtracelevel, logname,
                        "Using existing ccache [%s]\n", cc_name);
    }

    /* krb5_cc_resolve is basically like an init -
       this creates the cache structure, and creates a slot
       for the cache in the static linked list in memory, if
       there is not already a slot -
       see cc_memory.c for details 
       cc could already have been created by new_unique above
    */
    if (!cc && (rc = krb5_cc_resolve(ctx, cc_name, &cc))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not create ccache [%s]: %d (%s)\n",
                        cc_name, rc, error_message(rc));
        goto cleanup;
    }

    /* wipe out previous contents of cache for this principal, if any */
    if ((rc = krb5_cc_initialize(ctx, cc, princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not initialize ccache [%s] for the new "
                        "credentials for principal [%s]: %d (%s)\n",
                        cc_name, princ_name, rc, error_message(rc));
        goto cleanup;
    }

    /* store the credentials in the cache */
    if ((rc = krb5_cc_store_cred(ctx, cc, &creds))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not store the credentials in the "
                        "ccache [%s] for principal [%s]: %d (%s)\n",
                        cc_name, princ_name, rc, error_message(rc));
        goto cleanup;
    }

    /* now, do a "klist" to show the credential information, and log it */
    show_cached_credentials(authtracelevel, ctx, cc, princ);

    /* set the CC env var to the value of the cc cache name */
    /* since we can't pass krb5 context up and out of here
       and down through the ldap sasl layer, we set this
       env var so that calls to krb5_cc_default_name will
       use this */
    if (new_ccache) {
        PR_snprintf(cc_env_name, sizeof(cc_env_name),
                    "%s=%s", KRB5_ENV_CCNAME, cc_name);
        PR_SetEnv(cc_env_name);
        slapi_log_error(authtracelevel, logname,
                        "Set new env for ccache: [%s]\n",
                        cc_env_name);
    }

cleanup:
    /* use NULL as username and authid */
    slapi_ch_free_string(&vals->username);
    slapi_ch_free_string(&vals->authid);

    krb5_free_unparsed_name(ctx, princ_name);
    if (kt) { /* NULL not allowed */
        krb5_kt_close(ctx, kt);
    }
    if (creds.client == princ) {
        creds.client = NULL;
    }
    krb5_free_cred_contents(ctx, &creds);
    slapi_ch_free_string(&cc_name);
    krb5_free_principal(ctx, princ);
    if (cc) {
        krb5_cc_close(ctx, cc);
    }
    if (ctx) { /* cannot pass NULL to free context */
        krb5_free_context(ctx);
    }
    return;
}

#endif /* HAVE_KRB5 */
