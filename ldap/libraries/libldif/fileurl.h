/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 *  LDIF tools fileurl.h -- defines for file URL functions.
 *  Used by ldif_parse_line.
 */

/*
 * ldif_fileurl2path() convert a file URL to a local path.
 *
 * If successful, LDIF_FILEURL_SUCCESS is returned and *localpathp is
 * set point to an allocated string.  If not, an differnet LDIF_FILEURL_
 * error code is returned.
 */
int ldif_fileurl2path( char *fileurl, char **localpathp );


/*
 * Convert a local path to a file URL.
 *
 * If successful, LDIF_FILEURL_SUCCESS is returned and *urlp is
 * set point to an allocated string.  If not, an different LDIF_FILEURL_
 * error code is returned.  At present, the only possible error is
 * LDIF_FILEURL_NOMEMORY.
 *
 */
int ldif_path2fileurl( char *path, char **urlp );


/*
 * Possible return codes for ldif_fileurl2path and ldif_path2fileurl.
 */
#define LDIF_FILEURL_SUCCESS	0
#define LDIF_FILEURL_NOTAFILEURL	1
#define LDIF_FILEURL_MISSINGPATH	2
#define LDIF_FILEURL_NONLOCAL	3
#define LDIF_FILEURL_NOMEMORY	4
