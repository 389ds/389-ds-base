/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#include <process.h>
#include <io.h>
#endif
#include "dsalib.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#if !defined( XP_WIN32 )
#include <dirent.h>
#include <unistd.h>
#else
#define popen _popen
#define pclose _pclose
#endif
#include "portable.h"
#include "nspr.h"

/*
 * Get a listing of backup directories
 * Return NULL for errors  and a NULL list for an empty list.
 */
 
DS_EXPORT_SYMBOL char **
ds_get_bak_dirs()
{
    char	format_str[PATH_MAX];
    char    *root;
    int		i = 0;
    char	**bak_dirs = NULL;

    if ( (root = ds_get_install_root()) == NULL ) 
	{
        ds_send_error("Cannot find server root directory.", 0);
        return(bak_dirs);
    }

    PR_snprintf( format_str, PATH_MAX, "%s%cbak", root, FILE_SEP );
	bak_dirs = ds_get_file_list( format_str );
	if( bak_dirs )
	{
		while( bak_dirs[i] != NULL )
		{
			/* Prepend the filename with the install root */
			char filename[PATH_MAX];
			PR_snprintf( filename, PATH_MAX, "%s%cbak%c%s", root, FILE_SEP,
					 FILE_SEP, bak_dirs[i] );
			free( bak_dirs[i] );
			bak_dirs[i] = strdup( filename );
#if defined( XP_WIN32 )
			ds_dostounixpath( bak_dirs[i] );
#endif
			i++;
		}
	}

    return(bak_dirs);
}

/*
 * Restore a database based on a backup directory name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_bak2db(char *file)
{
    char        startup_line[BIG_LINE];
    char        statfile[PATH_MAX];
    char        *tmp_dir;
    char        *root;
    int         haderror = 0;
    int         error = -1;
    int         status;
    FILE        *sf = NULL;
	struct stat	fstats;

    if ( file == NULL ) {
        return DS_NULL_PARAMETER;
    }
    status = ds_get_updown_status();
    if ( status == DS_SERVER_UP ) {
        return DS_SERVER_MUST_BE_DOWN;
    }
    if ( (root = ds_get_install_root()) == NULL ) {
        return DS_NO_SERVER_ROOT;
    }

    if ( file[strlen(file) - 1] == '\n' )	/* strip out returns */
		file[strlen(file) - 1] = '\0';

    if( stat( file, &fstats ) == -1 && errno == ENOENT ) {
        return DS_CANNOT_OPEN_BACKUP_FILE;
    } else if( !(fstats.st_mode & S_IFDIR) ) {
        return DS_NOT_A_DIRECTORY;
    }

    tmp_dir = ds_get_tmp_dir();
    PR_snprintf(statfile, PATH_MAX, "%s%cbak2db.%d", tmp_dir, FILE_SEP, (int)getpid());
    PR_snprintf(startup_line, BIG_LINE,
			"%s%cbak2db "
			"%s%s%s > "
			"%s%s%s 2>&1",
			root, FILE_SEP, 
			ENQUOTE, file, ENQUOTE, 
			ENQUOTE, statfile, ENQUOTE );
    alter_startup_line(startup_line);
    fflush(0);
    error = system(startup_line);
    fflush(0);
    if ( error == -1 ) {
        return DS_CANNOT_EXEC;
    }
	fflush(0);
    if( !(sf = fopen(statfile, "r")) )  {
        return DS_CANNOT_OPEN_STAT_FILE;
    }

    while ( fgets(startup_line, BIG_LINE, sf) ) {
		if ((strstr(startup_line, "- Restoring file")) || 
			(strstr(startup_line, "- Checkpointing"))) {
			ds_show_message(startup_line);
		} else {
			haderror = 1;
			ds_send_error(startup_line, 0);
		}
    }

    fclose(sf);
    unlink(statfile);

    if ( haderror )
        return DS_UNKNOWN_ERROR;
    return 0;
}

/*
 * Create a backup based on a file name.
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_db2bak(char *file)
{
    char        startup_line[BIG_LINE];
    char        statfile[PATH_MAX];
    char        *tmp_dir;
    char        *root;
    int         haderror = 0;
    int         error = -1;
    FILE        *sf = NULL;
    int		lite = 0;
#ifdef XP_WIN32
    time_t	ltime;
#endif

    if ( (root = ds_get_install_root()) == NULL ) {
        return DS_NO_SERVER_ROOT;
    }

    if ( (file == NULL) || (strlen(file) == 0) )
        file = NULL;

	tmp_dir = ds_get_tmp_dir();
    PR_snprintf(statfile, PATH_MAX, "%s%cdb2bak.%d", tmp_dir, FILE_SEP, (int)getpid());
	
					
#if defined( XP_WIN32 )
	if( file == NULL )
	{
		file = malloc( BIG_LINE );

		time( &ltime );
		PR_snprintf( file, BIG_LINE, "%s", ctime( &ltime ) );
		ds_timetofname( file );
	}

	/* Check if the directory exists or can be created */
	if ( !ds_file_exists( file ) ) {
		char *errmsg = ds_mkdir_p( file, NEWDIR_MODE );
		if( errmsg != NULL ) {
/*			ds_send_error(errmsg, 10);
 */
			return DS_CANNOT_CREATE_DIRECTORY;
		}
	}
#endif

/* DBDB: note on the following line. 
 * Originally this had quotes round the directory name.
 * I found that this made the script not work becuase
 * a path of the form "foo"/bar/"baz" was passed to slapd.
 * the c runtime didn't like this. Perhaps there's a simple
 * solution, but for now I've modified this line here to 
 * not quote the directory name. This means that backup
 * directories can't have spaces in them.
 */


    PR_snprintf(startup_line, sizeof(startup_line),
			"%s%cdb2bak "
			"%s%s%s > "
			"%s%s%s 2>&1",
			root, FILE_SEP,
			ENQUOTE,
			(file == NULL) ? "" : file,
			ENQUOTE,
			ENQUOTE, statfile, ENQUOTE);

	PATH_FOR_PLATFORM( startup_line );
    alter_startup_line(startup_line);
    fflush(0);
    error = system(startup_line);
    if ( error == -1 ) {
        return DS_CANNOT_EXEC;
    }
    if( !(sf = fopen(statfile, "r")) )  {
        return DS_CANNOT_OPEN_STAT_FILE;
    }

    while ( fgets(startup_line, BIG_LINE, sf) ) {
        if (strstr(startup_line, " - Backing up file") ||
            strstr(startup_line, " - Checkpointing database")) {
            ds_show_message(startup_line);
        } else {
            haderror = 1;
            if (strstr ( startup_line, "restricted mode")) {
                lite = 1;
            }
            ds_send_error(startup_line, 0);
        }
    }
    fclose(sf);
    unlink(statfile);

    if ( lite && haderror )
	return DS_HAS_TOBE_READONLY_MODE;

    if ( haderror )
        return DS_UNKNOWN_ERROR;
    return 0;
}

static void
process_and_report( char *line, int line_size, FILE *cmd )
{
	while(fgets(line, line_size, cmd))  {
		/* Strip off line feeds */
		int ind = strlen( line ) - 1;
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
	}
}

static int exec_and_report( char *startup_line )
{
	FILE        *cmd = NULL;
    char        line[BIG_LINE];
    int         haderror = 0;

	PATH_FOR_PLATFORM( startup_line );
    alter_startup_line(startup_line);

	/*
	  fprintf( stdout, "Launching <%s>\n", startup_line );
	*/

    fflush(0);
	cmd = popen(startup_line, "r");
	if(!cmd) {
        return DS_CANNOT_EXEC;
    }
	process_and_report( line, sizeof(line), cmd );
	pclose(cmd);

    /*
    ** The VLV indexing code prints OK,
    ** if the index was successfully created.
    */
	if (strcmp(line,"OK")==0) {
		haderror = 0;
	} else {
		haderror = DS_UNKNOWN_ERROR;
	}

    return haderror;
}

/*
 * Create a vlv index
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_vlvindex(char **backendList, char **vlvList)
{
    char        startup_line[BIG_LINE];
    char        *root;
    char        *instroot;
	char		**vlvc = NULL;
			

    root = ds_get_server_root();
    instroot = ds_get_install_root();
    if ( (root == NULL) || (instroot == NULL) ) {
        return DS_NO_SERVER_ROOT;
    }

    PR_snprintf(startup_line, sizeof(startup_line), "%s/bin/slapd/server/%s db2index "
			"-D %s%s/%s "
			"-n %s ",
			root, SLAPD_NAME,			
			ENQUOTE, instroot, ENQUOTE,
			backendList[0]);


	/* Create vlv TAG */
	vlvc=vlvList;
	while( *vlvc != NULL ) {
		PR_snprintf( startup_line, sizeof(startup_line), "%s -T %s%s%s", startup_line,"\"",*vlvc,"\"" );
		vlvc++;
	}	
   
	return exec_and_report( startup_line );
}

/*
 * Create one or more indexes
 * 0:             success
 * anything else: failure
 */
DS_EXPORT_SYMBOL int
ds_addindex(char **attrList, char *backendName)
{
    char        startup_line[BIG_LINE];
    char        *root;
    char        *instroot;
 
    root = ds_get_server_root();
    instroot = ds_get_install_root();

    if ( (root == NULL) || (instroot == NULL) ) {
        return DS_NO_SERVER_ROOT;
    }

	PR_snprintf(startup_line, sizeof(startup_line), "%s/bin/slapd/server/%s db2index "
			"-D %s%s%s "
			"-n %s",
			root, SLAPD_NAME,			
			ENQUOTE, instroot, ENQUOTE,
			backendName);

	while( *attrList != NULL ) {
		PR_snprintf( startup_line, sizeof(startup_line), "%s -t %s", startup_line, *attrList );
		attrList++;
	}

	return exec_and_report( startup_line );
}
