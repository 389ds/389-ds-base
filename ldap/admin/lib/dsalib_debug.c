/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#endif
#include "dsalib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined( XP_WIN32 )
int ldap_debug = 0;
#endif

DS_EXPORT_SYMBOL void
ds_log_env(char **envp)
{
    FILE    *file;
	char	admin_logfile[PATH_MAX], *tmp_dir;
	
	tmp_dir = ds_get_tmp_dir();
	memset( admin_logfile, 0, sizeof( admin_logfile ) );
	strcat( admin_logfile, tmp_dir );
#if defined( XP_WIN32 )
	if( tmp_dir )
	{
		free( tmp_dir );
		tmp_dir = NULL;
	}
#endif
	strcat( admin_logfile, "/admin.log");

    file = fopen(admin_logfile, "a+");
    if (file != NULL) {
        int     i;
        for ( i = 0; envp[i] != (char *) 0; i++ ) {
            char        envstr[200];
 
            sprintf(envstr, "%s\n", envp[i]);
            fwrite(envstr, strlen(envstr), 1, file);
        }
        fclose(file);
    }
}
 
DS_EXPORT_SYMBOL void
ds_log_debug_message(char *msg)
{
    FILE    *file;
	char	admin_logfile[PATH_MAX], *tmp_dir;
	
	tmp_dir = ds_get_tmp_dir();
	memset( admin_logfile, 0, sizeof( admin_logfile ) );
	strcat( admin_logfile, tmp_dir );
#if defined( XP_WIN32 )
	if( tmp_dir )
	{
		free( tmp_dir );
		tmp_dir = NULL;
	}
#endif
	strcat( admin_logfile, "/admin.log");
 
    file = fopen(admin_logfile, "a+");
    if (file != NULL) {
        fwrite(msg, strlen(msg), 1, file);
        fclose(file);
    }
}

