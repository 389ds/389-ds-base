/** --- BEGIN COPYRIGHT BLOCK ---
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
  --- END COPYRIGHT BLOCK ---  */
/*
 * cookie.c -- routines to generate and manipulate cookies for dsgw
 */

#include "dsgw.h" 

#include "../../include/portable.h"

#include <stdio.h>
#if !defined( XP_WIN32 )
#include <sys/param.h>
#endif

#include <ssl.h>
#ifdef NSS38_AND_OLDER
#include <secrng.h>
#else
#include "ecl-exp.h"
#endif
#include <nss.h>
#include <base64.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/locking.h>
#else /* _WIN32 */
#include <unistd.h>
#endif /* _WIN32 */

#include <pk11func.h>
#include <pkcs11.h>
#include <pk11pqg.h>


static char *dsgw_MungeString(const char *unmunged_string);
static char *dsgw_UnMungeString(const char *munged_string);
static char *dsgw_encDec(CK_ATTRIBUTE_TYPE operation, const char *msg);
void dsgw_initNSS(void);

static char  tokDes[34] = "Communicator Generic Crypto Svcs";
static char ptokDes[34] = "Internal (Software) Token        ";

int dsgw_NSSInitializedAlready = 0;

/* Table for converting binary values to and from hexadecimal */
static char hex[] = "0123456789abcdef";
#if 0
static char dec[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /*   0 -  15 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /*  16 -  37 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* ' ' - '/' */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,   /* '0' - '?' */
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* '@' - 'O' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* 'P' - '_' */
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* '`' - 'o' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* 'p' - DEL */
};
#endif


#define	CKLEN		32
#define	RNDBUFLEN	( CKLEN / 2 )
#define	CKBUFSIZ	255
/*
 * Given a buffer buf of length len, return a pointer to a string containing
 * the hex-encoded version of buf.  The caller is responsible for freeing
 * the memory this routine allocates.
 */
static char *
buf2str( unsigned char *buf, int len )
{
    char *obuf;
    int i;
    char *p;

    if ( buf == NULL ) {
	return NULL;
    }

    p = obuf = dsgw_ch_malloc( CKLEN + 1 );
    for ( i = 0; i < len; i++) {
	*p++ = hex[( buf[ i ] >> 4 ) & 0xf ];
	*p++ = hex[( buf[ i ]) & 0xf ];
    }
    *p++ = '\0';
    return obuf;
}



/*
 * Generate a random string of hex-encoded digits, CKLEN characters in
 * length.  This routine allocates memory which the caller is responsible
 * for freeing.
 */
char *
dsgw_mkrndstr()
{
    unsigned char buf[ RNDBUFLEN ];

    PK11_ConfigurePKCS11(NULL, NULL, tokDes, ptokDes, NULL, NULL, NULL, NULL, 0, 0 );	
    /*NSS_NoDB_Init(NULL);*/
    dsgw_initNSS();
    PK11_GenerateRandom(buf, sizeof(buf));
    return( buf2str( buf, sizeof(buf) ));
}


FILE *
dsgw_opencookiedb()
{
    FILE *fp;
    time_t now;
    int newfile = 0;
    char cdb[MAXPATHLEN]; /*DSGW_COOKIEDB_FNAME + context*/

#ifdef XP_WIN32
#ifndef F_OK
#define F_OK 0
#endif
#endif
    PR_snprintf(cdb, sizeof(cdb), "%s.%s", DSGW_COOKIEDB_FNAME, context);

    if ( access( cdb, F_OK ) == 0 ) {
	fp = fopen( cdb, "r+" );
    } else {
	newfile = 1;
	fp = fopen( cdb, "w" );
    }
    if ( fp == NULL ) {
	return NULL;
    }
    /* fseek( fp, 0L, SEEK_SET ); */
#ifdef XP_WIN32
    (void) chmod( cdb, _S_IREAD | _S_IWRITE );
#else
    (void) chmod( cdb, S_IRUSR | S_IWUSR );
#endif

    /* acquire a lock */
#ifdef _WIN32
    while ( _locking( _fileno( fp ), LK_NBLCK, 0xFFFFFFFF ) != 0 ) {
#else
#ifdef USE_LOCKF
    while ( lockf( fileno( fp ), F_LOCK, 0 ) != 0 ) {
#else /* _WIN32 */
    while ( flock( fileno( fp ), LOCK_EX ) != 0 ) {
#endif
#endif /* _WIN32 */
	    ;       /* NULL */
    }
    if ( newfile ) {
	time( &now );
	fprintf( fp, "lastpurge: %-20lu\n", now );
	fflush( fp );
	fseek( fp, 0L, SEEK_SET );
    }
    return fp;
}


void
dsgw_closecookiedb( FILE *fp )
{
#ifdef _WIN32
        _locking( _fileno( fp ), LK_UNLCK, 0xFFFFFFFF );
#else /* _WIN32 */
#ifdef USE_LOCKF
        lockf( fileno( fp ), F_ULOCK, 0 );
#else
        flock( fileno( fp ), LOCK_UN );
#endif
#endif /* _WIN32 */
	fclose( fp );
}



/*
 * Return a pointer to the password associated with the given
 * cookie and dn.  If the cookie was not found in the database,
 * or if the cookie has expired, 1 is returned.  On success, 0 is returned
 * and ret_pw is set to the password from the database.  As a side effect,
 * if the database has not been purged of expired entries in more than
 * 10 minutes, the database is purged.
 *
 * As a special case, if the cookie is expired and gc->gc_mode is
 * DSGW_MODE_DOMODIFY (that is, the user is saving a modified entry), then
 * return 0 if the cookie has been expired for no more than 5 minutes.
 * This keeps users from being frustrated by getting an editable view of
 * an entry and having the cookie expire while editing.
 * The caller is responsible for freeing ret_pw.
 */
int
dsgw_ckdn2passwd( char *rndstr, char *dn, char **ret_pw )
{
    FILE *fp;
    char buf[ CKBUFSIZ ];
    char *p, *pw, *lifetimestr, *cdn;
    time_t now;
    int expired = 0;

    if ( !strcmp( rndstr, DSGW_UNAUTHSTR )) {
	*ret_pw = NULL;
	return 0;
    }

    if (( fp = dsgw_opencookiedb()) == NULL ) {
	return DSGW_CKDB_CANTOPEN;
    }

    for (;;) {
	if ( fgets( buf, sizeof(buf), fp ) == NULL ) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	    dsgw_log( "dsgw_ckdn2passwd: cookie <%s> not found in db\n",
		    rndstr );
#endif
	    return DSGW_CKDB_KEY_NOT_PRESENT;
	}

#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: retrieved buf from db: <%s>\n", buf );
#endif
	if ( buf[ strlen( buf ) - 1 ] == '\n' ) {
	    buf[ strlen( buf ) - 1 ] = '\0';
	}

	if ( strncmp( buf, rndstr, strlen( rndstr ))) {
	    continue;
	}

	if (( p =  strchr( buf, ':' )) == NULL ) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: colon 1 missing\n" );
#endif
	    return DSGW_CKDB_DBERROR;
	}
	*p++ = '\0';
	lifetimestr = p;
	if (( p =  strchr( lifetimestr, ':' )) == NULL ) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: colon 2 missing\n" );
#endif
	    return DSGW_CKDB_DBERROR;
	}
	*p++ = '\0';
	pw = p;

	if (( p =  strchr( pw, ':' )) == NULL ) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: colon 3 missing\n" );
#endif
	    return DSGW_CKDB_DBERROR;
	}
	*p++ = '\0';
	cdn = p;

	if ( strcmp( dn, cdn )) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: dn <%s> != cdn <%s>\n", dn, cdn );
#endif
	    return DSGW_CKDB_KEY_NOT_PRESENT;
	}
	
	/* expired? */
	time( &now );
	if ( gc->gc_mode == DSGW_MODE_DOMODIFY ) {
	    if ( now > ( atoi( lifetimestr ) + DSGW_MODIFY_GRACEPERIOD )) {
		expired = 1;
	    } else {
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: cookie expired (%ld > %ld) but within domodify grace period\n", now, atoi( lifetimestr ));
#endif
	    }
	} else if ( now > atoi( lifetimestr )) {
		expired = 1;
	}
		
	if ( expired != 0 ) {
	    dsgw_closecookiedb( fp );
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_ckdn2passwd: expired (%ld > %ld)\n", now, atoi( lifetimestr ));
#endif
	    return DSGW_CKDB_EXPIRED;
	}

	*ret_pw = dsgw_UnMungeString( pw );
	dsgw_closecookiedb( fp );
	return ( *ret_pw == NULL ) ? 1 : 0;
    }
}



/*
 * Store the given cookie and password into the database.  The cookie
 * is marked to expire at the time given by "expires".  Returns 0 if
 * successful, otherwise returns an error as given in dsgw.h.
 * As a side effect, if the database has not been purged of expired
 * entries in more than 10 minutes, the database is purged.
 *
 * Note: DNs are stored unescaped in the cookie database.  Passwords
 * are stored as "munged" values (encrypted using a hard-coded key and
 * then converted to ASCII as described in RFC-1113) to make them a bit
 * less obvious and to avoid problems which might arise from embedded ":"
 * characters in the password (":" is the field separator in the database).
 */
int
dsgw_storecookie( char *rndstr, char *dn, char *password, time_t lifetime )
{
    FILE *fp;
    char *epw;
    time_t now, lp;

    if (( fp = dsgw_opencookiedb()) == NULL ) {
	return DSGW_CKDB_CANTOPEN;
    }
    
    /* append record */
    if ( fseek( fp, 0L, SEEK_END ) < 0 ) {
	return DSGW_CKDB_CANTAPPEND;
    }
    if (( epw = dsgw_MungeString( password )) == NULL ) {
	return DSGW_CKDB_CANTAPPEND;	/* error msg is close enough */
    }

    time( &now );
    if ( fprintf( fp, "%s:%lu:%s:%s\n", rndstr, lifetime + now, epw, dn )
	    < 0 ) {
	free( epw );
	return DSGW_CKDB_CANTAPPEND;
    }

    fflush( fp );
    
    dsgw_closecookiedb( fp );
    fp = dsgw_opencookiedb();
    lp = dsgw_getlastpurged( fp );
    dsgw_closecookiedb( fp );

    if ( lp + DSGW_CKPURGEINTERVAL < now ) {
	dsgw_purgedatabase( NULL );
    }
#ifdef DSGW_DEBUG
	dsgw_log( "dsgw_storecookie: stored %s:%lu:%s:%s\n", rndstr, lifetime + now, epw, dn );
#endif
    free( epw );
    return 0;
}


/* 
 * Remove a cookie from the database.
 * Format of cookie argument is "nsdsgwauth=cookie-string:escaped-dn"
 */
int
dsgw_delcookie( char *cookie )
{
    FILE *fp;
    char *rndstr, *dn, *dnp, *dbdn, *p;
    char buf[ CKBUFSIZ ];
    int rc;
    long buflen;

    /* Parse the given cookie - find the random string */
    if (( rndstr = strchr( cookie, '=' )) == NULL ) {
	/* malformed cookie */
	return -1;
    } else {
	/* Get the escaped DN */
	rndstr++;
	if (( dn = strchr( rndstr, ':' )) == NULL ) {
	    /* malformed cookie */
	    return -1;
	} else {
	    *dn++ = '\0';
	    dsgw_form_unescape( dn );
	}
    }

    /*
     * Open the cookie database, find the rndstr, make sure the DNs
     * match, and delete that entry if found.
     */
    if (( fp = dsgw_opencookiedb()) == NULL ) {
        return -1;
    }
    fgets( buf, sizeof(buf), fp );
    if ( strncmp( buf, "lastpurge:", 10 )) {
	dsgw_closecookiedb( fp );
	return -1;
    }
    rc = DSGW_CKDB_KEY_NOT_PRESENT;
    for (;;) {
	if ( fgets( buf, sizeof(buf), fp ) == NULL ) {
	    break;
	}
	if ( strncmp( buf, rndstr, CKLEN )) {
	    continue;
	}
	buflen = strlen( buf );
	/* Found the random string - check DN */
	if (( dbdn = strrchr( buf, ':' )) == NULL ) {
	    continue;
	} else {
	    dbdn++;
	    if ( dbdn[ strlen( dbdn) - 1 ] == '\n' ) {
		dbdn[ strlen( dbdn) - 1 ] = '\0';
	    }
	    if ( strcmp( dbdn, dn )) {
		continue;
	    } else {
		/* Found it.  Set the expiration time to zero and obliterate
		 * the password.
		 */
		p = strchr( buf, ':' );
		for ( p++; *p != ':'; p++ ) {
		    *p = '0'; /* yes, '0', not '\0' */
		}
		dnp = strrchr( buf, ':' );
		for ( p++; p < dnp; p++ ) {
		    *p = 'x';
		}
		p++;
		fseek( fp, -buflen, SEEK_CUR );
		fputs( buf, fp );
		fputs( "\n", fp );
		fflush( fp );
		rc = 0;
	    }
	}
    }

    dsgw_closecookiedb( fp );

    if ( rc == 0 ) {
	dsgw_purgedatabase( dn );
    }

    return rc;
}






/*
 * Retrieve the time of the last database purge.  Returns zero on error.
 * The caller must open and lock the cookie database before calling this
 * routine.  The file pointer's position in the file is preserved.
 */
time_t
dsgw_getlastpurged( FILE *fp )
{
    char	buf[ CKBUFSIZ ];
    char	*p;
    size_t	pos;
    time_t	ret;

    if ( fp == NULL ) {
	return (time_t) 0L;
    }

    pos = ftell( fp );
    fseek( fp, 0L, SEEK_SET );

    fgets( buf, sizeof(buf), fp );
    if ( strncmp( buf, "lastpurge:", 10 )) {
	ret = (time_t) 0L;
    } else {
	p = buf + 10;
	if ( *p != '\0' ) {
	    ret = (time_t) atol( p );
	} else {
	    ret = (time_t) 0L;
	}
    }
    fseek( fp, pos, SEEK_SET );
    return ret;
}


/*
 * Purge the database of any expired entries.  Returns the number of
 * entries purged, or -1 if an error occurred.   If "dn" is non-NULL,
 * then this routine will also remove any entries where the DN matches
 * "dn". 
 */
#define DSGW_CK_DEBUG 1
int
dsgw_purgedatabase( char *dn )
{
    FILE *fp, *ofp;
    time_t now;
    char buf[ CKBUFSIZ ];
    char *exp;
    char expbuf[ 32 ];
    char *nbuf;
    int purged = 0;
#ifdef _WIN32
    int fh;
#endif
    size_t osize;	/* original size of file */
    size_t csize;	/* current size of file */
    char cdb[MAXPATHLEN]; /*DSGW_COOKIEDB_FNAME + context*/
    
    PR_snprintf(cdb, sizeof(cdb), "%s.%s", DSGW_COOKIEDB_FNAME, context);

    if (( fp = dsgw_opencookiedb()) == NULL ) {
	return -1;
    }

    fseek( fp, 0L, SEEK_END );
    osize = ftell( fp );
    fseek( fp, 0L, SEEK_SET );

    if (( ofp = fopen( cdb, "r+" )) == NULL ) {
	dsgw_closecookiedb( fp );
	return -1;
    }

    /* re-write the last purge time */
    time( &now );
    fprintf( ofp, "lastpurge: %-20lu\n", now );

    for (;;) {
	char *p;
	char *dbdn;
	int nukeit;
	size_t maxlen = sizeof(expbuf);

	nukeit = 0;

	if ( fgets( buf, sizeof(buf), fp ) == NULL ) {
	    break;
	}
	if ( strncmp( buf, "lastpurge:", 10 ) == 0 ) {
	    continue;
	}
	if (( p = strchr( buf, ':' )) == NULL ) {
	    fclose( ofp );
	    dsgw_closecookiedb( fp );
	    return -1;
	}
	exp = ++p;
	if (( p = strchr( exp, ':' )) == NULL ) {
	    fclose( ofp );
	    dsgw_closecookiedb( fp );
	    return -1;
	}
	if ((p - exp) < maxlen) {
		maxlen = p - exp;
	} else {
		maxlen--; /* need a length, not a count */
	}
	strncpy( expbuf, exp, maxlen );
	expbuf[ maxlen ] = '\0';
	time( &now );

	/* Get the entry's DN */
	dbdn = strrchr( buf, ':' );
	dbdn++;
	dbdn = strdup( dbdn );
	if ( dbdn[ strlen( dbdn) - 1 ] == '\n' ) {
	    dbdn[ strlen( dbdn) - 1 ] = '\0';
	}

	/* Should we delete? */
	if ( dn != NULL ) {
	    if (( dbdn != NULL ) && !strcmp( dn, dbdn )) {
		/* Entry's DN is the same as the "dn" parameter - delete */
		nukeit = 1;
	    }
	}

	free( dbdn );
	if ( !nukeit && ( now > atol( expbuf ))) {
	    /* expired */
	    nukeit = 1;
	}

	if ( !nukeit ) {
	    /* Entry should stay */
	    fputs( buf, ofp );
	} else {
	    /* Entry should be purged */
	    purged++;
	}
    }

    /*
     * Overwrite  the rest of the file so we don't leave passwords
     * laying around.
     */
    csize = ftell( ofp );
    nbuf = dsgw_ch_malloc( osize - csize + 2 );
    memset( nbuf, 'x', osize - csize + 1 );
    nbuf[ osize - csize + 1 ] = '\0';
    fputs( nbuf, ofp );
    free( nbuf );
#ifdef _WIN32
    dsgw_closecookiedb( fp );    
    fflush( ofp );
    fclose( ofp );
    fh = open( cdb, _O_RDWR | _O_TEXT );
    chsize( fh, csize );
    close( fh );
#else /* _WIN32 */
    fclose( ofp );
    ftruncate( fileno( fp ), csize );
    dsgw_closecookiedb( fp );    
#endif /* _WIN32 */
    return purged;
}



/*
 * for debugging - traverse and print the db
 */
void
dsgw_traverse_db()
{
    FILE *fp;
    char *exp;
    int total, expired;
    time_t now;
    char buf[ CKBUFSIZ ];
    char expbuf[ 32 ];
    
    total = expired = 0;

    if (( fp = dsgw_opencookiedb()) == NULL ) {
	fprintf( stderr, "can't open db\n" );
	return;
    }

    if ( fgets( buf, sizeof(buf), fp ) == NULL ) {
	dsgw_closecookiedb( fp );
	printf( "Cookie database is empty (no lastpurge line)\n" );
	return;
    }
    puts( buf );

    for (;;) {
	size_t maxlen = sizeof(expbuf);
	char *p;
	if ( fgets( buf, sizeof(buf), fp ) == NULL ) {
	    dsgw_closecookiedb( fp );
	    printf( "%d entries, %d expired\n", total, expired );
	    return;
	}
	if (( p = strchr( buf, ':' )) == NULL ) {
	    dsgw_closecookiedb( fp );
	    return;
	}
	exp = ++p;
	if (( p = strchr( exp, ':' )) == NULL ) {
	    dsgw_closecookiedb( fp );
	    return;
	}
	printf( "%s", buf );
	if ((p - exp + 1) < maxlen) {
	    maxlen = p - exp + 1;
	} else {
	    maxlen--; /* need a length, not a count */
	}
	strncpy( expbuf, exp, maxlen );
	expbuf[ maxlen ] = '\0';
	time( &now );
	total++;
	if ( now > atol( expbuf )) {
	    /* not yet expired */
	    printf( " (expired)\n" );
	    expired++;
	} else {
	    printf( "\n" );
	}
    }
}



/*
 * Generate a complete authentication cookie header line and store
 * the relevant parts iit in the database.
 * Return a pointer to the cookie.  This routine allocates memory, which
 * the caller is responsible for freeing.
 * On error, this routine returns NULL and sets err to point to an
 * error code.
 */
char *
dsgw_mkcookie( char *dn, char *password, time_t lifetime, int *err )
{
    char *r;
    char *ckbuf;
    char *edn;
    int rc;

    if ( dn == NULL ) {
	*err = DSGW_CKDB_NODN;
	return NULL;
    }
    edn = dsgw_strdup_escaped( dn );

    if (( r = dsgw_mkrndstr()) == NULL ) {
	*err = DSGW_CKDB_RNDSTRFAIL;
	return NULL;
    }
    rc = dsgw_storecookie( r, dn, password, lifetime );
    if ( rc != 0 )  {
	free( r );
	free( edn );
	*err = rc;
	return NULL;
    }

    /* richm: replace with PR_smprintf */
    ckbuf = dsgw_ch_malloc( strlen( DSGW_CKHDR ) + strlen( r ) +
	    strlen( edn ) + strlen( DSGW_AUTHCKNAME ) + 2 + 20 );
    ckbuf[ 0 ] = '\0';
    strcpy( ckbuf, DSGW_CKHDR );
    strcat( ckbuf, DSGW_AUTHCKNAME );
    strcat( ckbuf, "=" );
    strcat( ckbuf, r );
    strcat( ckbuf, ":" );
    strcat( ckbuf, edn );
    strcat( ckbuf, "; path=/" );

    free( r );
    free( edn );
    return ckbuf;
}



/*
 * Password obfuscation, etc.
 * There is no real security here -- we just encrypt using a hard-coded key.
 * The original functions these are based on are called SECMOZ_MungeString()
 * and SECMOZ_UnMungeString().  They can be found in ns/lib/libsec/secmoz.c
 * (they don't get built as part of the server builds).  The only change I
 * made was to swap a few of the bytes in the secmoz_tmmdi array and add one
 * to all of them. -- Mark Smith <mcs@netscape.com>
 */

static unsigned char dsgw_tmmdi[] = {	/* tmmdi == They Made Me Do It */
    0x87,	/* repka, paquin */
    0x9d,	/* freier, elgamal */
    0xdf,	/* jonm, bobj */
    0xef,	/* fur, sharoni */
    0xd1,	/* jsw, karlton */
    0xec,	/* ari, sk */
    0x3f,	/* terry, atotic */
    0xc7	/* jevering, kent */
};

static char *
dsgw_MungeString(const char *unmunged_string)
{
  return(dsgw_encDec(CKA_ENCRYPT, unmunged_string));
}
static char *
dsgw_UnMungeString(const char *munged_string)
{
  return(dsgw_encDec(CKA_DECRYPT, munged_string));
}

/*
 * key import and encryption (using RC4)
 */
static char *
dsgw_encDec(CK_ATTRIBUTE_TYPE operation, const char *msg)
{
  CK_MECHANISM_TYPE type = CKM_RC4;
  PK11SlotInfo *slot = 0;
  PK11SymKey *key = 0;
  SECItem *params = 0;
  PK11Context *context = 0;
  unsigned char *output;
  unsigned char *input;
  char *edStr;
  int outLen;
  int len;
  SECStatus s;
  SECItem keyItem = { siBuffer, dsgw_tmmdi, sizeof dsgw_tmmdi };
  int noGood = 0;
  unsigned int inlen;

  if (msg == NULL) {
    return NULL;
  }

  if (*msg == '\0') {
    return PL_strdup(msg);
  }

  if (operation == CKA_DECRYPT) {
    input = ATOB_AsciiToData(msg, &inlen);
    if (msg == NULL)
      return NULL;
  } else { 
    inlen = PL_strlen(msg);
    input = (unsigned char *) msg;
  }

  output = (unsigned char *) malloc(inlen + 65);
  if (output == NULL) {
    return NULL;
  }

  /* Initialization */
  /*NSS_NoDB_Init(".");*/
  dsgw_initNSS();

  /*
   * Choose a "slot" to use.  Slots store keys (either
   *   temporarily or permanently) and perform
   *   cryptogrphic operations.
   *
   * Use the built-in key slot.  Another way to choose
   *   a slot is using PK11_GetBestSlot(), which chooses
   *   based on the mechanism.
   */
  slot = PK11_GetInternalKeySlot();
  if (!slot)
  {
    noGood = 1;
    goto dsgw_encDec_done;
  }

  /*
   * Get the encryption key.  Params may be passed in here,
   *   but most symmetric key generation requires only the key
   *   length.
   *
   * Warning: the key length is in bytes
   *
   * The key can also be imported (not recommended).  See importKey()
   * below for example code.
   */
  /*  This code generates a random key
      key = PK11_KeyGen(slot, type, 0, 128/8, 0);
      if (!key)
      {
      goto dsgw_encDec_done;
      }*/
  /* Here we are using a static key. This sucks, but we don't really
   * have much of a choice.*/
  key = PK11_ImportSymKey(slot, CKM_RC4, PK11_OriginGenerated, operation, &keyItem, 0);

  /*
   * Some encryption algorithms require parameters.  NSS provides
   * a generic way to create parameters for any algorithm.
   */
  params = PK11_GenerateNewParam(type, key);
  if (!params)
  {
    noGood = 1;
    goto dsgw_encDec_done;
  }

  /*if (params->data) printBuffer(params->data, params->len);*/

  /*
   * Cryptographic operations are performed using a "context"
   * Create one for doing encryption using the key and parameters
   * generated above.
   */
  context = PK11_CreateContextBySymKey(type, operation, key, params);
  if (!context)
  {
    noGood = 1;
    goto dsgw_encDec_done;
  }

  /*
   * Encrypt the data.  In general, the input data should be in multiples
   * of the cipher's block size, and the output size will match the input
   * size. However, this will not be true for mechanisms that provide
   * padding.
   */
  s = PK11_CipherOp(context, output, &outLen, inlen + 64, input, (int) inlen);
  if (s != SECSuccess)
  {
    noGood = 1;
    goto dsgw_encDec_done;
  }

  /*printBuffer(output, outLen);*/

  /*
   * When a mechanism that provides padding is used, there may be additional
   * data available after the last input data is processed.
   *
   * NOTE: The type of the length output here is different than in PK11_CipherOp
   */
  s = PK11_DigestFinal(context, &output[outLen], &len, sizeof output - outLen);
  if (s != SECSuccess)
  {
    noGood = 1;
    goto dsgw_encDec_done;
  }

  /*if (len != 0) printBuffer(&output[outLen], len);*/

  outLen += len;
  
  /*
   * Terminate the cryptographic operation.  Destroying the
   * context also performs this function.
   */
  PK11_Finalize(context);

  /*
   * Delete the encryption context block, this releases the reference to the key
   * and frees the context's copy of the parameters, etc.
   *
   * The second argument should always be PR_TRUE to free the context structure
   * itself, in addition to the contents.
   */
  PK11_DestroyContext(context, PR_TRUE);
  context = 0;

dsgw_encDec_done:
  if (context) PK11_DestroyContext(context, PR_TRUE);  /* freeit ?? */
  if (params) SECITEM_ZfreeItem(params, PR_TRUE);
  if (key) PK11_FreeSymKey(key);
  if (slot) PK11_FreeSlot(slot);

  if (noGood == 1) {
    return(NULL);
  }

  if (operation == CKA_DECRYPT) {
    edStr = (char *) output;
    edStr[outLen] = '\0';
  } else {
    edStr = BTOA_DataToAscii(output, outLen);
    free(output);
  }

  return(edStr);
}


void 
dsgw_initNSS(void)
{
  if (dsgw_NSSInitializedAlready == 1) {
    return;
  }

  if (gc->gc_ldapssl && gc->gc_securitypath != NULL ) {
    NSS_Init(gc->gc_securitypath);
  } else {
    NSS_NoDB_Init(NULL);    
  }
  dsgw_NSSInitializedAlready = 1;
}
