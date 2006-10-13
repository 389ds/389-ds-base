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
#if defined( XP_WIN32 )
#include <windows.h>
#include <io.h>
#else /* XP_WIN32 */
#   if defined( AIXV4 )
#   include <fcntl.h>
#   else /* AIXV4 */
#   include <sys/fcntl.h>
#   endif /* AIXV4 */
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#endif /* XP_WIN3 */
#include "dsalib.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

#include "nspr.h"
#include "plstr.h"

#define COPY_BUFFER_SIZE        4096
/* This is the separator string to use when outputting key/value pairs
   to be read by the non-HTML front end (Java console)
*/
static const char *SEPARATOR = ":"; /* from AdmTask.java */

#define LOGFILEENVVAR "DEBUG_LOGFILE" /* used for logfp */

static int internal_rm_rf(const char *path, DS_RM_RF_ERR_FUNC ds_rm_rf_err_func, void *arg);

/* return a FILE * opened in append mode to the log file
   caller must use fclose to close it
*/
static FILE *
get_logfp(void)
{
	FILE *logfp = NULL;
	char *logfile = getenv(LOGFILEENVVAR);

	if (logfile) {
		logfp = fopen(logfile, "a");
	}
	return logfp;
}

DS_EXPORT_SYMBOL int 
ds_file_exists(char *filename)
{
    struct stat finfo;

    if ( filename == NULL )
	return 0;

    if ( stat(filename, &finfo) == 0 )	/* successful */
        return 1;
    else
        return 0;
}

DS_EXPORT_SYMBOL int
ds_mkdir(char *dir, int mode)
{
    if(!ds_file_exists(dir)) {
#ifdef XP_UNIX
        if(mkdir(dir, mode) == -1)
#else  /* XP_WIN32 */
        if(!CreateDirectory(dir, NULL))
#endif /* XP_WIN32 */
            return -1;
    }
    return 0;
}


DS_EXPORT_SYMBOL char *
ds_mkdir_p(char *dir, int mode)
{
    static char errmsg[ERR_SIZE];
    struct stat fi;
    char *t;

#ifdef XP_UNIX
    t = dir + 1;
#else /* XP_WIN32 */
    t = dir + 3;
#endif /* XP_WIN32 */

    while(1) {
        t = strchr(t, FILE_PATHSEP);

        if(t) *t = '\0';
        if(stat(dir, &fi) == -1) {
            if(ds_mkdir(dir, mode) == -1) {
                PR_snprintf(errmsg, sizeof(errmsg), "mkdir %s failed (%s)", dir, ds_system_errmsg());
                return errmsg;
            }
        }
        if(t) *t++ = FILE_PATHSEP;
        else break;
    }
    return NULL;
}


/*
 * Given the name of a directory, return a NULL-terminated array of
 * the file names contained in that directory.  Returns NULL if the directory
 * does not exist or an error occurs, and returns an array with a
 * single NULL string if the directory exists but is empty.  The caller
 * is responsible for freeing the returned array of strings.
 * File names "." and ".." are not returned.
 */
#if !defined( XP_WIN32 )
DS_EXPORT_SYMBOL char **
ds_get_file_list( char *dir )
{
    DIR *dirp;
    struct dirent *direntp;
    char **ret = NULL;
    int nfiles = 0;

    if (( dirp = opendir( dir )) == NULL ) {
	return NULL;
    }
    
    if (( ret = malloc( sizeof( char * ))) == NULL ) {
	return NULL;
    };

    while (( direntp = readdir( dirp )) != NULL ) {
	if ( strcmp( direntp->d_name, "." ) &&
		strcmp( direntp->d_name, ".." )) {
	    if (( ret = (char **) realloc( ret,
		    sizeof( char * ) * ( nfiles + 2 ))) == NULL );
	    ret[ nfiles ] = strdup( direntp->d_name );
	    nfiles++;
	}
    }
    (void) closedir( dirp );

    ret[ nfiles ] = NULL;
    return ret;
}
#else
DS_EXPORT_SYMBOL char **
ds_get_file_list( char *dir )
{
	char szWildcardFileSpec[MAX_PATH];
	char **ret = NULL;
	long hFile;
	struct _finddata_t	fileinfo;
	int	nfiles = 0;

	if( ( dir == NULL ) || (strlen( dir ) == 0) )
		return NULL;

    if( ( ret = malloc( sizeof( char * ) ) ) == NULL ) 
		return NULL;

	PL_strncpyz(szWildcardFileSpec, dir, sizeof(szWildcardFileSpec));
	PL_strcatn(szWildcardFileSpec, sizeof(szWildcardFileSpec), "/*");

	hFile = _findfirst( szWildcardFileSpec, &fileinfo);
	if( hFile == -1 )
		return NULL;

	if( ( strcmp( fileinfo.name, "." ) != 0 ) &&
		( strcmp( fileinfo.name, ".." ) != 0 ) )
	{
	    ret[ nfiles++ ] = strdup( fileinfo.name );
	}

	while( _findnext( hFile, &fileinfo ) == 0 )
	{
		if( ( strcmp( fileinfo.name, "." ) != 0 ) &&
			( strcmp( fileinfo.name, ".." ) != 0 ) )
		{
			if( ( ret = (char **) realloc( ret, sizeof( char * ) * ( nfiles + 2 ) ) ) != NULL ) 
				ret[ nfiles++ ] = strdup( fileinfo.name);
		}
	}

	_findclose( hFile );
    ret[ nfiles ] = NULL;
	return ret;
}
#endif /* ( XP_WIN32 ) */


DS_EXPORT_SYMBOL time_t 
ds_get_mtime(char *filename)
{
    struct stat fi;

    if ( stat(filename, &fi) )
        return 0;
    return fi.st_mtime;
}

/*
 * Copy files: return is
 *    1: success
 *    0: failure
 * Print errors as needed.
 */
DS_EXPORT_SYMBOL int 
ds_cp_file(char *sfile, char *dfile, int mode)
{
#if defined( XP_WIN32 )
	return( CopyFile( sfile, dfile, FALSE ) ); /* Copy even if dfile exists */
#else
    int sfd, dfd, len;
    struct stat fi;
    char copy_buffer[COPY_BUFFER_SIZE];
    unsigned long read_len;
    char error[BIG_LINE];

/* Make sure we're in the right umask */
    umask(022);

    if( (sfd = open(sfile, O_RDONLY)) == -1) {
        PR_snprintf(error, sizeof(error), "Can't open file %s for reading.", sfile);
        ds_send_error(error, 1);
	return(0);
    }

    fstat(sfd, &fi);
    if (!(S_ISREG(fi.st_mode))) {
        PR_snprintf(error, sizeof(error), "File %s is not a regular file.", sfile);
        ds_send_error(error, 1);
        close(sfd);
	return(0);
    }
    len = fi.st_size;

    if( (dfd = open(dfile, O_RDWR | O_CREAT | O_TRUNC, mode)) == -1) {
        PR_snprintf(error, sizeof(error), "can't write to file %s", dfile);
        ds_send_error(error, 1);
        close(sfd);
	return(0);
    }
    while (len) {
        read_len = len>COPY_BUFFER_SIZE?COPY_BUFFER_SIZE:len;

        if ( (read_len = read(sfd, copy_buffer, read_len)) == -1) {
            PR_snprintf(error, sizeof(error), "Error reading file %s for copy.", sfile);
            ds_send_error(error, 1);
            close(sfd);
            close(dfd);
	    return(0);
        }

        if ( write(dfd, copy_buffer, read_len) != read_len) {
            PR_snprintf(error, sizeof(error), "Error writing file %s for copy.", dfile);
            ds_send_error(error, 1);
            close(sfd);
            close(dfd);
	    return(0);
        }

        len -= read_len;
    }
    close(sfd);
    close(dfd);
    return(1);
#endif
}

DS_EXPORT_SYMBOL void 
ds_unixtodospath(char *szText)
{
	if(szText)
	{
		while(*szText)
   		{
   			if( *szText == '/' )
   				*szText = '\\';
   			szText++;
		}
	}
}

/* converts '\' chars to '/' */
DS_EXPORT_SYMBOL void 
ds_dostounixpath(char *szText)
{
	if(szText)
	{
		while(*szText)
   		{
   			if( *szText == '\\' )
   				*szText = '/';
   			szText++;
		}
	}
}

/* converts ':' chars to ' ' */
DS_EXPORT_SYMBOL void 
ds_timetofname(char *szText)
{
	if(szText)
	{
		/* Replace trailing newline */
		szText[ strlen( szText ) -1 ] = 0;
		while(*szText)
   		{
			if( *szText == ':' ||
			    *szText == ' ' )
   				*szText = '_';
   			szText++;
		}
	}
}

/* Effects a rename in 2 steps, needed on NT because if the 
target of a rename() already exists, the rename() will fail. */
DS_EXPORT_SYMBOL int 
ds_saferename(char *szSrc, char *szTarget)
{
#ifdef XP_WIN32
	int iRetVal;
	char *szTmpFile;
	struct stat buf;
#endif

	if( !szSrc || !szTarget )
		return 1;

#if defined( XP_WIN32 )

	szTmpFile = mktemp("slrnXXXXXX" );
	if( stat( szTarget, &buf ) == 0 )
	{
		/* Target file exists */
		if( !szTmpFile )
			return 1;

		if( !ds_cp_file( szTarget, szTmpFile, 0644) )
			return( 1 );		

		unlink( szTarget );
		if( (iRetVal = rename( szSrc, szTarget )) != 0 )
		{
			/* Failed to rename, copy back. */
			ds_cp_file( szTmpFile, szTarget, 0644);			
		}
		/* Now remove temp file */
		unlink( szTmpFile );
	}
	else
		iRetVal = rename(szSrc, szTarget);

	return iRetVal;	
#else
	return rename(szSrc, szTarget);
#endif

}

DS_EXPORT_SYMBOL char*
ds_encode_all (const char* s)
{
    char* r;
    size_t l;
    size_t i;
    if (s == NULL || *s == '\0') {
	return strdup ("");
    }
    l = strlen (s);
    r = malloc (l * 3 + 1);
    for (i = 0; *s != '\0'; ++s) {
	r[i++] = '%';
	sprintf (r + i, "%.2X", 0xFF & (unsigned int)*s);
	i += 2;
    }
    r[i] = '\0';
    return r;
}

DS_EXPORT_SYMBOL char*
ds_URL_encode (const char* s)
{
    char* r;
    size_t l;
    size_t i;
    if (s == NULL || *s == '\0') {
	return strdup ("");
    }
    l = strlen (s) + 1;
    r = malloc (l);
    for (i = 0; *s != '\0'; ++s) {
	if (*s >= 0x20 && *s <= 0x7E && strchr (" <>\"#%{}[]|\\^~`?,;=+\n", *s) == NULL) {
	    if (l - i <= 1) r = realloc (r, l *= 2);
	    r[i++] = *s;
	} else { /* encode *s */
	    if (l - i <= 3) r = realloc (r, l *= 2);
	    r[i++] = '%';
	    sprintf (r + i, "%.2X", 0xFF & (unsigned int)*s);
	    i += 2;
	}
    }
    r[i] = '\0';
    return r;
}

DS_EXPORT_SYMBOL char*
ds_URL_decode (const char* original)
{
    char* r = strdup (original);
    char* s;
    for (s = r; *s != '\0'; ++s) {
	if (*s == '+') {
	    *s = ' ';
	}
	else if (*s == '%' && isxdigit(s[1]) && isxdigit(s[2])) {
	    memmove (s, s+1, 2);
	    s[2] = '\0';
	    *s = (char)strtoul (s, NULL, 16);
	    memmove (s+1, s+3, strlen (s+3) + 1);
	}
    }
    return r;
}

#if !defined( XP_WIN32 )
#include <errno.h> /* errno */
#include <pwd.h> /* getpwnam */

static int   saved_uid_valid = 0;
static uid_t saved_uid;
static int   saved_gid_valid = 0;
static gid_t saved_gid;

#if defined( HPUX )
#define SETEUID(id) setresuid((uid_t) -1, id, (uid_t) -1)
#else
#define SETEUID(id) seteuid(id)
#endif

#endif

DS_EXPORT_SYMBOL char*
ds_become_localuser_name (char *localuser)
{
#if !defined( XP_WIN32 )
    if (localuser != NULL) {
	struct passwd* pw = getpwnam (localuser);
	if (pw == NULL) {
	    fprintf (stderr, "getpwnam(%s) == NULL; errno %d",
		     localuser, errno);
		fprintf (stderr, "\n");
	    fflush (stderr);
	} else {
	    if ( ! saved_uid_valid) saved_uid = geteuid();
	    if ( ! saved_gid_valid) saved_gid = getegid();
	    if (setgid (pw->pw_gid) == 0) {
		saved_gid_valid = 1;
	    } else {
		fprintf (stderr, "setgid(%li) != 0; errno %d",
			 (long)pw->pw_gid, errno);
		fprintf (stderr, "\n");
		fflush (stderr);
	    }
	    if (SETEUID (pw->pw_uid) == 0) {
		saved_uid_valid = 1;
	    } else {
		fprintf (stderr, "seteuid(%li) != 0; errno %d",
			 (long)pw->pw_uid, errno);
		fprintf (stderr, "\n");
		fflush (stderr);
	    }
	}
    }
    return NULL;
#else
    return NULL;
#endif
}

DS_EXPORT_SYMBOL char*
ds_become_localuser (char **ds_config)
{
#if !defined( XP_WIN32 )
    char* localuser = ds_get_value (ds_config, ds_get_var_name(DS_LOCALUSER), 0, 1);
    if (localuser != NULL) {
	char	*rv = ds_become_localuser_name(localuser);

	free(localuser);
	return rv;
    }
    return NULL;
#else
    return NULL;
#endif
}

DS_EXPORT_SYMBOL char*
ds_become_original (char **ds_config)
{
#if !defined( XP_WIN32 )
    if (saved_uid_valid) {
	if (SETEUID (saved_uid) == 0) {
	    saved_uid_valid = 0;
	} else {
	    fprintf (stderr, "seteuid(%li) != 0; errno %d<br>n",
		     (long)saved_uid, errno);
	    fflush (stderr);
	}
    }
    if (saved_gid_valid) {
	if (setgid (saved_gid) == 0) {
	    saved_gid_valid = 0;
	} else {
	    fprintf (stderr, "setgid(%li) != 0; errno %d<br>\n",
		     (long)saved_gid, errno);
	    fflush (stderr);
	}
    }
    return NULL;
#else
    return NULL;
#endif
}

/*
 * When a path containing a long filename is passed to system(), the call
 * fails. Therfore, we need to use the short version of the path, when 
 * constructing the path to pass to system().
 */
DS_EXPORT_SYMBOL char*
ds_makeshort( char * filepath )
{
#if defined( XP_WIN32 )
	char *shortpath = malloc( MAX_PATH );
	DWORD dwStatus;
	if( shortpath )
	{
		dwStatus = GetShortPathName( filepath, shortpath, MAX_PATH );
		return( shortpath );
	}
#endif
	return filepath;
}

/* returns 1 if string "searchstring" found in file "filename" */
/* if found, returnstring is allocated and filled with the line */
/* caller should release the memory */
DS_EXPORT_SYMBOL int 
ds_search_file(char *filename, char *searchstring, char **returnstring)
{
    struct stat finfo;
	FILE * sf;
	char big_line[BIG_LINE];

    if( filename == NULL )
		return 0;

    if( stat(filename, &finfo) != 0 )	/* successful */
        return 0;

	if( !(sf = fopen(filename, "r")) )  
		return 0;

	while ( fgets(big_line, BIG_LINE, sf) ) {
		if( strstr( big_line, searchstring ) != NULL ) {
			*returnstring = (char *)malloc(strlen(big_line) + 1);
			if (NULL != *returnstring) {
				strcpy(*returnstring, big_line);
			}
		    fclose(sf);
			return 1;
		}
	}

	fclose(sf);

	return 0;
}

/*
 * on linux when running as root, doing something like 
 * system("date > out.log 2>&1") will fail, because of an 
 * ambigious redirect.  This works for /bin/sh, but not /bin/csh or /bin/tcsh
 *
 * using this would turn
 * 	system("date > out.log 2>&1");
 * into
 * 	system("/bin/sh/ -c \"date > out.log 2>&1\"")
 *
 */
DS_EXPORT_SYMBOL void
alter_startup_line(char *startup_line)
{
#if (defined Linux && !defined LINUX2_4)
        char temp_startup_line[BIG_LINE+40];
 
        PR_snprintf(temp_startup_line, sizeof(temp_startup_line), "/bin/sh -c \"%s\"", startup_line);
        PL_strncpyz(startup_line, temp_startup_line, BIG_LINE);
#else
	/* do nothing */
#endif /* Linux */
}

DS_EXPORT_SYMBOL void
ds_send_error(char *errstr, int print_errno)
{
	FILE *logfp;
	fprintf(stdout, "error%s%s\n", SEPARATOR, errstr);
	if (print_errno && errno)
		fprintf(stdout, "system_errno%s%d\n", SEPARATOR, errno);

    fflush(stdout);

	if ((logfp = get_logfp())) {
		fprintf(logfp, "error%s%s\n", SEPARATOR, errstr);
		if (print_errno && errno)
			fprintf(logfp, "system_errno%s%d\n", SEPARATOR, errno);
		fclose(logfp);
	}

}

DS_EXPORT_SYMBOL void
ds_send_status(char *str)
{
	FILE *logfp;
    fprintf(stdout, "[%s]: %s\n", ds_get_server_name(), str);
    fflush(stdout);

	if ((logfp = get_logfp())) {
	    fprintf(logfp, "[%s]: %s\n", ds_get_server_name(), str);
		fclose(logfp);
	}
}

/* type and doexit are unused
   I'm not sure what type is supposed to be used for
   removed the doexit code because we don't want to
   exit abruptly anymore, we must exit by returning an
   exit code from the return in main()
*/
static void
report_error(int type, char *msg, char *details, int doexit)
{
    char error[BIG_LINE*4] = {0};

	if (msg)
	{
		PL_strcatn(error, BIG_LINE*4, msg);
		PL_strcatn(error, BIG_LINE*4, SEPARATOR);
	}
	if (details)
		PL_strcatn(error, BIG_LINE*4, details);
	ds_send_error(error, 1);
}

DS_EXPORT_SYMBOL void
ds_report_error(int type, char *msg, char *details)
{
	/* richm - changed exit flag to 0 - we must not exit
	   abruptly, we should instead exit by returning a code
	   as the return value of main - this ensures that callers
	   are properly notified of the status
	*/
	report_error(type, msg, details, 0);
}

DS_EXPORT_SYMBOL void
ds_report_warning(int type, char *msg, char *details)
{
	report_error(type, msg, details, 0);
}

DS_EXPORT_SYMBOL void
ds_show_message(const char *message)
{
	FILE *logfp;
	printf("%s\n", message);
	fflush(stdout);

	if ((logfp = get_logfp())) {
		fprintf(logfp, "%s\n", message);
		fclose(logfp);
	}

	return;
}

DS_EXPORT_SYMBOL void
ds_show_key_value(char *key, char *value)
{
	FILE *logfp;
	printf("%s%s%s\n", key, SEPARATOR, value);

	if ((logfp = get_logfp())) {
		fprintf(logfp, "%s%s%s\n", key, SEPARATOR, value);
		fclose(logfp);
	}
	return;
}

/* Stolen from the Admin Server dsgw_escape_for_shell */
DS_EXPORT_SYMBOL char * 
ds_escape_for_shell( char *s ) 
{ 
    char        *escaped; 
    char        tmpbuf[4]; 
    size_t x,l; 
  
    if ( s == NULL ) { 
        return( s ); 
    } 
  
    l = 3 * strlen( s ) + 1; 
    escaped = malloc( l ); 
    memset( escaped, 0, l ); 
    for ( x = 0; s[x]; x++ ) { 
        if (( (unsigned char)s[x] & 0x80 ) == 0 ) { 
           strncat( escaped, &s[x], 1 ); 
        } else { 
           /* not an ASCII character - escape it */ 
           sprintf( tmpbuf, "\\%x", (unsigned)(((unsigned char)(s[x])) & 0xff) ); 
           strcat( escaped, tmpbuf ); 
        } 
    } 
    return( escaped ); 
}

DS_EXPORT_SYMBOL char *
ds_system_errmsg(void)
{
    static char static_error[BUFSIZ];
    char *lmsg = 0; /* Local message pointer */
    size_t msglen = 0;
    int sys_error = 0;
#ifdef XP_WIN32
    LPTSTR sysmsg = 0;
#endif

    /* Grab the OS error message */
#ifdef XP_WIN32
    sys_error = GetLastError();
#else
    sys_error = errno;
#endif

#if defined(XP_WIN32)
    msglen = FormatMessage(
	FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_ALLOCATE_BUFFER,
	NULL, 
	GetLastError(), 
	LOCALE_SYSTEM_DEFAULT, 
	(LPTSTR)&sysmsg, 
	0, 
	0);
    if (msglen > 0)
	lmsg = sysmsg;
    SetLastError(0);
#else
    lmsg = strerror(errno);
    errno = 0;
#endif

    if (!lmsg)
	static_error[0] = 0;
    else
    {
	/* At this point lmsg points to something. */
	int min = 0;
	msglen = strlen(lmsg);

	min = msglen > BUFSIZ ? BUFSIZ : msglen;
	strncpy(static_error, lmsg, min-1);
	static_error[min-1] = 0;
    }

#ifdef XP_WIN32
    /* NT's FormatMessage() dynamically allocated the msg; free it */
    if (sysmsg)
        LocalFree(sysmsg);
#endif

    return static_error;
}

#ifndef MAXPATHLEN
#define MAXPATHLEN  1024
#endif

enum {
	DB_DIRECTORY = 0,
	DB_LOGDIRECTORY,
	DB_CHANGELOGDIRECTORY,
	DB_HOME_DIRECTORY
};

static int
is_fullpath(char *path)
{
	int len;
	if (NULL == path || '\0' == *path)
		return 0;

	if (FILE_PATHSEP == *path) /* UNIX */
		return 1;

	len = strlen(path);
	if (len > 2)
	{
		if (':' == path[1] && ('/' == path[2] || '\\' == path[2])) /* Windows */
			return 1;
	}
	return 0;
}

static void
rm_db_dirs(char *fullpath, DS_RM_RF_ERR_FUNC ds_rm_rf_err_func, void *arg)
{
	FILE *fp = fopen(fullpath, "r");
	char buf[2][MAXPATHLEN];
	char *bufp, *nextbufp;
	char *retp;
	int readit = 0;

	if (fp == NULL)
	{
		ds_rm_rf_err_func(fullpath, "opening the config file", arg);
		return;
	}

	bufp = buf[0]; *bufp = '\0';
	nextbufp = buf[1]; *nextbufp = '\0';

	while (readit || (retp = fgets(bufp, MAXPATHLEN, fp)) != NULL)
	{
		int len = strlen(bufp);
		int type = -1;
		char *p, *q;

		if (strstr(bufp, "nsslapd-directory"))
			type = DB_DIRECTORY;
		else if (strstr(bufp, "nsslapd-db-home-directory"))
			type = DB_HOME_DIRECTORY;
		else if (strstr(bufp, "nsslapd-db-logdirectory"))
			type = DB_LOGDIRECTORY;
		else if (strstr(bufp, "nsslapd-changelogdir"))
			type = DB_CHANGELOGDIRECTORY;
		else
		{
			readit = 0;
			continue;
		}

		p = bufp + len;

		while ((retp = fgets(nextbufp, MAXPATHLEN, fp)) != NULL)
		{
			int thislen;
			if (*nextbufp == ' ')
			{
				thislen = strlen(nextbufp);
				len += thislen;
				if (len < MAXPATHLEN)
				{
					strncpy(p, nextbufp, thislen);
					p += thislen;
				}
				/* else too long as a path. ignore it */
			}
			else
				break;
		}
		if (retp == NULL)	/* done */
			break;

		p = strchr(bufp, ':');
		if (p == NULL)
		{
			char *tmpp = bufp;
			bufp = nextbufp;
			nextbufp = tmpp;
			readit = 1;
			continue;
		}

		while (*(++p) == ' ') ;

		q = p + strlen(p) - 1;
		while (*q == ' ' || *q == '\t' || *q == '\n')
			q--;
		*(q+1) = '\0';

		switch (type)
		{
		case DB_DIRECTORY:
		case DB_LOGDIRECTORY:
		case DB_CHANGELOGDIRECTORY:
			if (is_fullpath(p))
				internal_rm_rf(p, ds_rm_rf_err_func, NULL);
			break;
		case DB_HOME_DIRECTORY:
			internal_rm_rf(p, ds_rm_rf_err_func, NULL);
			break;
		}
	}

	fclose(fp);
}

static char *
get_dir_from_startslapd(char *loc, char *keyword)
{
	char *returnstr = NULL; 
	char *ptr = NULL;
	char *confdir = NULL;
if (ds_search_file(loc, keyword, &returnstr) > 0 && returnstr) {
		ptr = strchr(returnstr, '=');
		if (NULL != ptr) {
			confdir = strdup(++ptr);
		}
		free(returnstr);
	}
	return confdir;
}

static char *
get_dir_from_config(char *config_dir, char *config_attr)
{
    char *configfile = NULL;
	char *returnstr = NULL; 
	char *ptr = NULL;
	char *dir = NULL;
	configfile = PR_smprintf("%s%c%s", config_dir, FILE_PATHSEP, DS_CONFIG_FILE);
	if (configfile && ds_search_file(configfile, config_attr, &returnstr) > 0
		&& returnstr) {
		ptr = strchr(returnstr, ':');
		if (NULL != ptr) {
			while (' ' == *ptr || '\t' == *ptr) ptr++;
			dir = strdup(ptr);
		}
		free(returnstr);
		PR_smprintf_free(configfile);
	}
	return dir;
}

/* this function will recursively remove a directory hierarchy from the file
   system, like "rm -rf"
   In order to handle errors, the user supplies a callback function.  When an
   error occurs, the callback function is called with the file or directory name
   and the system errno.  The callback function should return TRUE if it wants
   to continue or FALSE if it wants the remove aborted.
   The error callback should use PR_GetError and/or PR_GetOSError to
   determine the cause of the failure
*/
/* you could locate db dirs non standard location
   we should remove them, as well.
*/
static int
internal_rm_rf(const char *path, DS_RM_RF_ERR_FUNC ds_rm_rf_err_func, void *arg)
{
	struct PRFileInfo prfi;
	int retval = 0;

	if (PR_GetFileInfo(path, &prfi) != PR_SUCCESS) {
		if (!ds_rm_rf_err_func(path, "reading directory", arg)) {
			return 1;
		}
	}

	if (prfi.type == PR_FILE_DIRECTORY)
	{
		PRDir *dir;
		PRDirEntry *dirent;

		if (!(dir = PR_OpenDir(path))) {
			if (!ds_rm_rf_err_func(path, "opening directory", arg)) {
				return 1;
			}
			return 0;
		}
			
		while ((dirent = PR_ReadDir(dir, PR_SKIP_BOTH))) {
			char *fullpath = PR_smprintf("%s%c%s", path, FILE_PATHSEP, dirent->name);
			if (PR_GetFileInfo(fullpath, &prfi) != PR_SUCCESS) {
				if (!ds_rm_rf_err_func(fullpath, "reading file", arg)) {
					PR_smprintf_free(fullpath);
					PR_CloseDir(dir);
					return 1;
				} /* else just continue */
			} else if (prfi.type == PR_FILE_DIRECTORY) {
				retval = internal_rm_rf(fullpath, ds_rm_rf_err_func, arg);
				if (retval) { /* non zero return means stop */
					PR_smprintf_free(fullpath);
					break;
				}
			} else {
				/* FHS changes the directory structure.
				 * Config dir is no longer in the instance dir.
				 * The info should be found in start-slapd,
				 * therefore get the path from the file here.
				 */
				if (0 == strcmp(dirent->name, "start-slapd")) {
				    char *config_dir = ds_get_config_dir();
					char *run_dir = ds_get_run_dir();
					if (NULL == config_dir || '\0' == *config_dir) {
						config_dir = get_dir_from_startslapd(fullpath, DS_CONFIG_DIR);
					}
					if (NULL == run_dir || '\0' == *run_dir) {
						char *ptr = NULL;
						run_dir = get_dir_from_startslapd(fullpath, PIDFILE);
						ptr = strrchr(run_dir, FILE_PATHSEP);
						if (NULL != ptr) {
							*ptr = '\0';	/* equiv to dirname */
						}
					}
					if (NULL != run_dir) {
						internal_rm_rf(run_dir, ds_rm_rf_err_func, NULL);
						free(run_dir);
					}
					if (NULL != config_dir) {
						char *lock_dir = get_dir_from_config(config_dir, DS_CONFIG_LOCKDIR);
						char *err_log = get_dir_from_config(config_dir, DS_CONFIG_ERRLOG);

						if (NULL != lock_dir) {
							internal_rm_rf(lock_dir, ds_rm_rf_err_func, NULL);
							free(lock_dir);
						}
						if (NULL != err_log) {
							char *ptr = strrchr(err_log, FILE_PATHSEP);
							if (NULL != ptr) {
								*ptr = '\0'; /* equiv to 'dirname' */
								internal_rm_rf(err_log, ds_rm_rf_err_func, NULL);
							}
							free(err_log);
						}
						/* removing db dirs */
						rm_db_dirs(config_dir, ds_rm_rf_err_func, arg);

						/* removing config dir */
						internal_rm_rf(config_dir, ds_rm_rf_err_func, NULL);
					}
				}
				/* 
				 * When the file is the config file, 
				 * check if db dir is in the instance dir or not.
				 * If db dir exists in the instance dir, it's an old structure.
				 * Let's clean the old db here, as well.
				 */
				if (0 == strcmp(dirent->name, DS_CONFIG_FILE)) {
					rm_db_dirs(fullpath, ds_rm_rf_err_func, arg);
				}

				if (PR_Delete(fullpath) != PR_SUCCESS) {
					if (!ds_rm_rf_err_func(fullpath, "deleting file", arg)) {
						PR_smprintf_free(fullpath);
						PR_CloseDir(dir);
						return 1;
					}
				}
			}
			PR_smprintf_free(fullpath);
		}
		PR_CloseDir(dir);
		if (PR_RmDir(path) != PR_SUCCESS) {
			if (!ds_rm_rf_err_func(path, "removing directory", arg)) {
				retval = 1;
			}
		}
	}

	return retval;
}

static int
default_err_func(const char *path, const char *op, void *arg)
{
	PRInt32 errcode = PR_GetError();
	char *msg;
	const char *errtext;

	if (!errcode || (errcode == PR_UNKNOWN_ERROR)) {
		errcode = PR_GetOSError();
		errtext = ds_system_errmsg();
	} else {
		errtext = PR_ErrorToString(errcode, PR_LANGUAGE_I_DEFAULT);
	}

	msg = PR_smprintf("%s %s: error code %d (%s)", op, path, errcode, errtext);
	ds_send_error(msg, 0);
	PR_smprintf_free(msg);
	return 1; /* just continue */	
}

/* dir: instance dir, e.g.,  "$NETSITE_ROOT/slapd-<id>" */
DS_EXPORT_SYMBOL int
ds_rm_rf(const char *dir, DS_RM_RF_ERR_FUNC ds_rm_rf_err_func, void *arg)
{
	struct PRFileInfo prfi;

	if (!dir) {
		ds_send_error("Could not remove NULL directory name", 1);
		return 1;
	}

	if (!ds_rm_rf_err_func) {
		ds_rm_rf_err_func = default_err_func;
	}

	if (PR_GetFileInfo(dir, &prfi) != PR_SUCCESS) {
		if (ds_rm_rf_err_func(dir, "reading directory", arg)) {
			return 0;
		} else {
			return 1;
		}
	}
	if (prfi.type != PR_FILE_DIRECTORY) {
		char *msg = PR_smprintf("Cannot remove directory %s because it is not a directory", dir);
		ds_send_error(msg, 0);
		PR_smprintf_free(msg);
		return 1;
	}

	return internal_rm_rf(dir, ds_rm_rf_err_func, arg);
}

DS_EXPORT_SYMBOL int
ds_remove_reg_key(void *base, const char *format, ...)
{
	int rc = 0;
#ifdef XP_WIN32
	int retries = 3;
	HKEY hkey = (HKEY)base;
	char *key;
	va_list ap;

	va_start(ap, format);
	key = PR_vsmprintf(format, ap);
	va_end(ap);

	do {
		if (ERROR_SUCCESS != RegDeleteKey(hkey, key)) {
			rc = GetLastError();
			if (rc == ERROR_BADKEY || rc == ERROR_CANTOPEN ||
				rc == ERROR_CANTREAD ||
				rc == ERROR_CANTWRITE || rc == ERROR_KEY_DELETED ||
				rc == ERROR_ALREADY_EXISTS || rc == ERROR_NO_MORE_FILES) {
				rc = 0; /* key already deleted - no error */
			} else if ((retries > 1) && (rc == ERROR_IO_PENDING)) {
				/* the key is busy - lets wait and try again */
				PR_Sleep(PR_SecondsToInterval(3));
				retries--;
			} else {
				char *errmsg = PR_smprintf("Could not remove registry key %s - error %d (%s)",
									   	key, rc, ds_system_errmsg());
				ds_send_error(errmsg, 0);
				PR_smprintf_free(errmsg);
				break; /* no retry, just fail */
			}
		}
	} while (rc && retries);
	PR_smprintf_free(key);
#endif
	return rc;
}
