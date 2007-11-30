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
