/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#endif
#include "dsalib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nspr.h"
#include "plstr.h"

/*
 * Returns the server root. Info is 
 * returned in a static area. The caller must copy it 
 * for reuse if needed.
 */
DS_EXPORT_SYMBOL char *
ds_get_server_root()
{
    char        *root;
 
    if ( (root = getenv("NETSITE_ROOT")) == NULL )
        return(NULL);

	/* WIN32: Needed to take care of embedded space, */
	/* otherwise system() call fails */
	root = ds_makeshort( root );

	return root;
}

/*
 * Returns the install location of the server. Info is 
 * returned in a static area. The caller must copy it 
 * for reuse if needed.
 */
DS_EXPORT_SYMBOL char *
ds_get_install_root()
{
    char        *root;
    char        *ds_name;
    static char install_root[PATH_MAX];
 
    if ( (root = ds_get_server_root()) == NULL )
        return(NULL);
    if ( (ds_name = ds_get_server_name()) == NULL )
        return(NULL);

    PR_snprintf(install_root, sizeof(install_root), "%s/%s", root, ds_name);
    return(install_root);
}

/*
 * Returns the install location of the server under the admserv
 * directory.
 */
DS_EXPORT_SYMBOL char *
ds_get_admserv_based_root()
{
    char        *root;
    char        *ds_name;
    static char install_root[PATH_MAX];
 
    if ( (root = getenv("ADMSERV_ROOT")) == NULL )
        return(NULL);
    if ( (ds_name = ds_get_server_name()) == NULL )
        return(NULL);
    PR_snprintf(install_root, sizeof(install_root), "%s/%s", root, ds_name);
    return(install_root);
}

DS_EXPORT_SYMBOL char *
ds_get_server_name()
{
    if( getenv("SERVER_NAMES") )
		return( getenv("SERVER_NAMES") );
	else {
		static char logfile[PATH_MAX];
		char *buf;
		char *out = logfile;
		buf = getenv("SCRIPT_NAME");
		if ( buf && (*buf == '/') )
			buf++;
		while ( *buf && (*buf != '/') ) {
			*out++ = *buf++;
		}
		*out = 0;
		return logfile;
	}
}

DS_EXPORT_SYMBOL char *
ds_get_logfile_name(int config_type)
{
    char        *filename;
    char	**ds_config = NULL;
    static char logfile[PATH_MAX+1];
 
    if ( (ds_config = ds_get_config(DS_REAL_CONFIG)) == NULL ) {
		/* For DS 4.0, no error output if file doesn't exist - that's
		   a normal situation */
		/* ds_send_error("ds_get_config(DS_REAL_CONFIG) == NULL", 0); */
        return(NULL);
    }
    filename = ds_get_value(ds_config, ds_get_var_name(config_type), 0, 1);

    if ( filename == NULL ) {
		/* For DS 4.0, no error output if file doesn't exist - that's
		   a normal situation */
		/* ds_send_error("ds_get_logfile_name: filename == NULL", 0); */
        return(NULL);
    }
    if ( ((int) strlen(filename)) >= PATH_MAX ) {
		ds_send_error("ds_get_logfile_name: filename too long", 0);
		free(filename);
        return(NULL);
    }
    PL_strncpyz(logfile, filename, sizeof(logfile));
	free(filename);
    return(logfile);
}

DS_EXPORT_SYMBOL char *
ds_get_errors_name()
{
    return( ds_get_logfile_name(DS_ERRORLOG) );
}

DS_EXPORT_SYMBOL char *
ds_get_access_name()
{
    return( ds_get_logfile_name(DS_ACCESSLOG) );
}

DS_EXPORT_SYMBOL char *
ds_get_audit_name()
{
    return( ds_get_logfile_name(DS_AUDITFILE) );
}

