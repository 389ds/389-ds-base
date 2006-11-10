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
#include "nspr.h"
#include "plstr.h"

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
		PR_snprintf(startup_line, BIG_LINE, "%s%cldif2db%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, file, ENQUOTE);
	} else if (backend) {
		PR_snprintf(startup_line, BIG_LINE, "%s%cldif2db%s -n %s%s%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, backend, ENQUOTE,
				ENQUOTE, file, ENQUOTE);
	} else if (subtree) {
		PR_snprintf(startup_line, BIG_LINE, "%s%cldif2db%s -s %s%s%s -i %s%s%s",
				root, FILE_SEP, SCRIPT_SUFFIX,
				ENQUOTE, subtree, ENQUOTE,
				ENQUOTE, file, ENQUOTE);
	} else {
		PR_snprintf(startup_line, BIG_LINE, "%s%cldif2db%s -i %s%s%s -noconfig",
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
				PR_snprintf( filename, BIG_LINE, "%s%c%s", changelogdir,
						 FILE_SEP, db_files[ i ]);
				PR_snprintf(sbuf, BIG_LINE, "Removing %s", filename);
				ds_send_status( sbuf );
				rc = unlink( filename);
				if ( rc != 0 ) {
					PR_snprintf( errbuf, BIG_LINE, "Warning: some files in %s could not "
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
	PR_snprintf(statfile, PATH_MAX, "%s%cdb2ldif.%d", tmp_dir, FILE_SEP, (int) getpid());

#if defined( XP_WIN32 )
	if( file == NULL )
	{
		time_t		ltime;
		file = malloc( BIG_LINE );

		time( &ltime );
		PR_snprintf( file, BIG_LINE, "%s", ctime( &ltime ) );
		ds_timetofname( file );
	}
#endif

	if ( file == NULL )
		*outfile = 0;
	else
		PL_strncpyz( outfile, file, sizeof(outfile) );

	PR_snprintf(scriptfile, PATH_MAX, "%s%cdb2ldif", root, FILE_SEP);

	PATH_FOR_PLATFORM( outfile );
	PATH_FOR_PLATFORM( scriptfile );

	if ( subtree == NULL ) {
		PR_snprintf(startup_line, sizeof(startup_line),
				"%s "
				"%s%s%s > "
				"%s%s%s 2>&1",
				scriptfile,
				ENQUOTE, outfile, ENQUOTE,
				ENQUOTE, statfile, ENQUOTE);
	} else {
		PR_snprintf(startup_line, sizeof(startup_line),
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
