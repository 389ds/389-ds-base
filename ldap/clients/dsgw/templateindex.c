/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */
/*
 * templateindex.c -- CGI template indexer -- HTTP gateway
 */

#include "dsgw.h"
#if defined( XP_WIN32 )
#include <io.h>
struct dirent {
	char d_name[1];
};
#else
#include <dirent.h>
#endif

static void build_index();

#if defined( XP_WIN32 )
char **ds_get_file_list( char *dir )
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

	strcpy(szWildcardFileSpec, dir);
	strcat(szWildcardFileSpec, "/*");
	
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
			if( ( ret = (char **) realloc( ret, sizeof( char * ) * ( nfiles + 1 ) ) ) != NULL )
				ret[ nfiles++ ] = strdup( fileinfo.name);
		}
	}

	_findclose( hFile );

	ret[ nfiles ] = NULL;
	return ret;
}
#endif ( XP_WIN32 )


main( argc, argv, env )
    int		argc;
    char	*argv[];
#ifdef DSGW_DEBUG
    char	*env[];
#endif
{
    int		reqmethod;

    reqmethod = dsgw_init( argc, argv,  DSGW_METHOD_GET );
    dsgw_send_header();

#ifdef DSGW_DEBUG
   dsgw_logstringarray( "env", env ); 
#endif

    dsgw_html_begin( "Directory Server Gateway Template Indexer", 1 );

    build_index();

    dsgw_html_end();

    exit( 0 );
}


static void
build_index()
{
    FILE		*htmlfp;
#if !defined( XP_WIN32 )
    DIR			*dirp;
#endif
    struct dirent	*dep;
    char		*path, **argv, *classes, *p, line[ BIG_LINE ];
    char		**filelist;
    int			errcount, prefixlen, count, argc, filecount = 0;


    path = dsgw_file2path( gc->gc_tmpldir, "" );

#if defined( XP_WIN32 )
    if (( filelist = ds_get_file_list( path )) == NULL ) {
#else
    if (( dirp = opendir( path )) == NULL ) {
#endif
	dsgw_error( DSGW_ERR_OPENDIR, path, DSGW_ERROPT_EXIT, 0, NULL );
    }
    free( path );

    prefixlen = strlen( DSGW_CONFIG_DISPLAYPREFIX );
    errcount = count = 0;

    dsgw_emitf( "Remove any lines that begin with \"template\" from \n" );
    dsgw_emitf( "your dsgw.conf file and add these lines:<BR><PRE>\n" );

#if defined( XP_WIN32 )
    while( filelist != NULL && filelist[filecount] != NULL ) {
	dep = (struct dirent *)filelist[filecount];
#else
    while (( dep = readdir( dirp )) != NULL ) {
#endif
	if ( strlen( dep->d_name ) > prefixlen && strncasecmp( dep->d_name,
		DSGW_CONFIG_DISPLAYPREFIX, prefixlen ) == 0 && strcmp(
		".html", dep->d_name + strlen( dep->d_name ) - 5 ) == 0 ) {
	    ++count;
	    htmlfp = dsgw_open_html_file( dep->d_name, DSGW_ERROPT_EXIT );

	    while ( dsgw_next_html_line( htmlfp, line )) {
		if ( dsgw_parse_line( line, &argc, &argv, 1,
			dsgw_simple_cond_is_true, NULL )) {
		    if ( dsgw_directive_is( line, DRCT_DS_OBJECTCLASS )) {
			if (( classes = get_arg_by_name( "value", argc, argv ))
				== NULL ) {
			     dsgw_emitf(
    "Missing \"value=objectclass\" on line &lt%s<BR>\n", line+1 );
			    ++errcount;
			    continue;
			}
			dsgw_emitf( "template  %.*s",
				strlen( dep->d_name ) - prefixlen - 5,
				dep->d_name + prefixlen );
			for ( ; classes != NULL && *classes != '\0';
				classes = p ) {
			    if (( p = strchr( classes, ',' )) != NULL ) {
				*p++ = '\0';
				while ( ldap_utf8isspace( p )) {
				    LDAP_UTF8INC(p);
				}
			    }
			    dsgw_emitf( "  %s", classes );
			}
			dsgw_emits( "\n" );
		    }
		}
	    }
	    fclose( htmlfp );
            filecount++;
	}
    }

#if !defined( XP_WIN32 )
    closedir( dirp );
#endif

    dsgw_emits( "</PRE><H3>Template indexing " );

    if ( errcount == 0 ) {
	dsgw_emitf( "complete (%d files).<H3>\n", count );
    } else {
	dsgw_emitf( "failed (%d errors).<H3>\n", errcount );
    }
}
