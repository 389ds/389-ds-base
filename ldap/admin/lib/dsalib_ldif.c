/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#include <process.h>
#include <malloc.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif
#include "dsalib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef XP_WIN32
#define SCRIPT_SUFFIX "" /* shell scripts have no suffix */
#else
#define SCRIPT_SUFFIX ".bat" /* batch file suffix */
#endif


static int
process_and_report( char *line, int line_size, FILE *cmd )
{
    int err = 0;
    while(fgets(line, line_size, cmd))  {
        /* Strip off line feeds */
        int ind = strlen( line ) - 1;
#ifdef DEBUG_CGI
		fprintf(stderr, "read line=[%s] ind=%d\n", line, ind);
#endif /* DEBUG_CGI */
        fprintf( stdout, ": %s", line );
        fflush(0);
        while ( (ind >= 0) &&
                ((line[ind] == '\n') ||
                 (line[ind] == '\r')) ) {
            line[ind] = 0;
            ind--;
        }
        if ( ind < 1 ) {
            continue;
        }
        ds_send_status(line);
        if ( (strstr(line, "bad LDIF") != NULL) ) {
#ifdef DEBUG_CGI
			fprintf(stderr, "invalid ldif file\n");
#endif /* DEBUG_CGI */
            err = DS_INVALID_LDIF_FILE;
        } else if ( 0 == err ) {
            if ( (strstr(line, "err=") != NULL) ) {
#ifdef DEBUG_CGI
				fprintf(stderr, "unknown error\n");
#endif /* DEBUG_CGI */
                err = DS_UNKNOWN_ERROR;
            }
        }
    }
#ifdef DEBUG_CGI
	fprintf(stderr, "process_and_report finished err=%d\n", err);
#endif /* DEBUG_CGI */
    return err;
}

static int exec_and_report( char *startup_line )
{
	FILE        *cmd = NULL;
	char        line[BIG_LINE];
	int         haderror = 0;

	PATH_FOR_PLATFORM( startup_line );
	alter_startup_line(startup_line);

	fflush(stdout);
	cmd = popen(startup_line, "r");
	if(!cmd) {
		printf("could not open pipe [%s]: %d\n",
			startup_line, errno);
#ifdef DEBUG_CGI
		fprintf(stderr, "could not open pipe [%s]: %d\n",
				startup_line, errno);
#endif /* DEBUG_CGI */
		return DS_CANNOT_EXEC;
	}
	haderror = process_and_report( line, sizeof(line), cmd );
	pclose(cmd);

    return haderror;
}

/*
 * Execute a shell command.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_exec_and_report( char *startup_line )
{
	return exec_and_report( startup_line );
}

/*
 * Create a database based on a file name.
 * 0:             success
 * anything else: failure
 */
static int
importldif(char *file, int preserve, char *backend, char *subtree)
{
    char        startup_line[BIG_LINE];
    char        *root;
    int         haderror = 0;
    int         i = 0, error = -1;
    int         status;
    struct stat fstats;
    char	errbuf[ BIG_LINE ];
    char	**db_files = NULL, *changelogdir = NULL;
    int		rc;

    errbuf[ 0 ] = '\0';

    if ( file == NULL ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: null file\n");
#endif /* DEBUG_CGI */
        return DS_NULL_PARAMETER;
    }
    status = ds_get_updown_status();
    if ( status == DS_SERVER_UP ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: server is not down\n");
#endif /* DEBUG_CGI */
        return DS_SERVER_MUST_BE_DOWN;
    }
    if ( (root = ds_get_install_root()) == NULL ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: could not get server root\n");
#endif /* DEBUG_CGI */
        return DS_NO_SERVER_ROOT;
    }

    if ( file[strlen(file) - 1] == '\n' )	/* strip out returns */
		file[strlen(file) - 1] = '\0';

    /* Make sure the file exists and is not a directory: 34347 */
    if( stat( file, &fstats ) == -1 && errno == ENOENT ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: could not open %s\n", file);
#endif /* DEBUG_CGI */
        return DS_CANNOT_OPEN_LDIF_FILE;
    } else if( fstats.st_mode & S_IFDIR ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: not a file %s\n", file);
#endif /* DEBUG_CGI */
        return DS_IS_A_DIRECTORY;
    }

	if ( preserve ) {
		sprintf(startup_line, "%s%cldif2db%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, file, ENQUOTE);
	} else if (backend) {
		sprintf(startup_line, "%s%cldif2db%s -n %s%s%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, backend, ENQUOTE,
				ENQUOTE, file, ENQUOTE);
	} else if (subtree) {
		sprintf(startup_line, "%s%cldif2db%s -s %s%s%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, subtree, ENQUOTE,
				ENQUOTE, file, ENQUOTE);
	} else {
		sprintf(startup_line, "%s%cldif2db%s -i %s%s%s -noconfig",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, file, ENQUOTE);
	}
    alter_startup_line(startup_line);
    fflush(stdout);
#ifdef DEBUG_CGI
	fprintf(stderr, "importldif: executing %s\n", startup_line);
#endif /* DEBUG_CGI */
    error = exec_and_report(startup_line);
    /*error = system(startup_line);*/
    if ( error != 0 ) {
#ifdef DEBUG_CGI
		fprintf(stderr, "importldif: error=%d\n", error);
#endif /* DEBUG_CGI */
        return error;
    }

    /* Remove the changelog database, if present */
    changelogdir = ds_get_config_value(0xdeadbeef);
    if ( changelogdir != NULL ) {
		db_files = ds_get_file_list( changelogdir );
		if ( db_files != NULL ) {
			ds_send_status("Removing changelog database...");
		}
		for ( i = 0; db_files != NULL && db_files[ i ] != NULL; i++ ) {
			char sbuf[ BIG_LINE ];
			char filename[ BIG_LINE ];
			if ( strlen( db_files[ i ]) > 0 ) {
				sprintf( filename, "%s%c%s", changelogdir,
						 FILE_SEP, db_files[ i ]);
				sprintf(sbuf, "Removing %s", filename);
				ds_send_status( sbuf );
				rc = unlink( filename);
				if ( rc != 0 ) {
					sprintf( errbuf, "Warning: some files in %s could not "
							 "be removed\n", changelogdir );
					haderror++;
				}
			}
		}
    }
    if ( strlen( errbuf ) > 0 ) {
		ds_send_error( errbuf, 0 );
    }

    return error;
}

/*
 * Create a database based on a file name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_ldif2db(char *file)
{
	return importldif( file, 0, NULL, NULL );
}

/*
 * Create a database based on a file name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_ldif2db_preserve(char *file)
{
	return importldif( file, 1, NULL, NULL );
}

/*
 * import an ldif file into a named backend or subtree
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_ldif2db_backend_subtree(char *file, char *backend, char *subtree)
{
	return importldif( file, 0, backend, subtree );
}

/*
 * Create a LDIF file based on a file name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_db2ldif_subtree(char *file, char *subtree)
{
    char        startup_line[BIG_LINE];
    char        statfile[PATH_MAX];
    char        outfile[PATH_MAX];
    char        scriptfile[PATH_MAX];
    char        *tmp_dir;
    char        *root;
    int         haderror = 0;
    int         error = -1;
    FILE        *sf = NULL;

    if ( (root = ds_get_install_root()) == NULL ) {
        return DS_NO_SERVER_ROOT;
    }

    if ( (file == NULL) || (strlen(file) == 0) )
        file = NULL;

	tmp_dir = ds_get_tmp_dir();
	sprintf(statfile, "%s%cdb2ldif.%d", tmp_dir, FILE_SEP, (int) getpid());

#if defined( XP_WIN32 )
	if( file == NULL )
	{
		time_t		ltime;
		file = malloc( BIG_LINE );

		time( &ltime );
		sprintf( file, "%s", ctime( &ltime ) );
		ds_timetofname( file );
	}
#endif

	if ( file == NULL )
		*outfile = 0;
	else
		strcpy( outfile, file );

	sprintf(scriptfile, "%s%cdb2ldif", root, FILE_SEP);

	PATH_FOR_PLATFORM( outfile );
	PATH_FOR_PLATFORM( scriptfile );

	if ( subtree == NULL ) {
		sprintf(startup_line,
				"%s "
				"%s%s%s > "
				"%s%s%s 2>&1",
				scriptfile,
				ENQUOTE, outfile, ENQUOTE,
				ENQUOTE, statfile, ENQUOTE);
	} else {
		sprintf(startup_line,
				"%s "
				"%s%s%s "
				"-s \"%s\" > "
				"%s%s%s 2>&1",
				scriptfile,
				ENQUOTE, outfile, ENQUOTE,
				subtree,
				ENQUOTE, statfile, ENQUOTE);
	}

    fflush(0);
    alter_startup_line(startup_line);
    error = system(startup_line);
    if ( error == -1 ) {
        return DS_CANNOT_EXEC;
    }
    sf = fopen(statfile, "r");
    if( sf ) {
        while ( fgets(startup_line, BIG_LINE, sf) ) {
            /*
              The db2ldif process will usually print out a summary at the
              end, but that is not an error
            */
            char *ptr = strstr(startup_line, "Processed");
            if (ptr && strstr(ptr, "entries."))
            {
                ds_show_message(startup_line);
            }
            else
            {
                haderror = 1;
                ds_send_error(startup_line, 0);
            }
        }
        fclose(sf);
        unlink(statfile);
    }

    if ( haderror )
        return DS_UNKNOWN_ERROR;
    return 0;
}

/*
 * Create a LDIF file based on a file name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_db2ldif(char *file)
{
	return ds_db2ldif_subtree(file, NULL);
}
