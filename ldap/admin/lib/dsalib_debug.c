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

#if defined( XP_WIN32 )
int ldap_debug = 0;
#endif

DS_EXPORT_SYMBOL void
ds_log_env(char **envp)
{
    FILE    *file;
	char	admin_logfile[PATH_MAX], *tmp_dir;
	
	tmp_dir = ds_get_tmp_dir();
	PL_strncpyz( admin_logfile, tmp_dir, sizeof(admin_logfile) );
#if defined( XP_WIN32 )
	if( tmp_dir )
	{
		free( tmp_dir );
		tmp_dir = NULL;
	}
#endif
	PL_strcatn( admin_logfile, sizeof(admin_logfile), "/admin.log");

    file = fopen(admin_logfile, "a+");
    if (file != NULL) {
        int     i;
        for ( i = 0; envp[i] != (char *) 0; i++ ) {
            char        envstr[200];
 
            PR_snprintf(envstr, sizeof(envstr), "%s\n", envp[i]);
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
	PL_strncpyz( admin_logfile, tmp_dir, sizeof(admin_logfile) );
#if defined( XP_WIN32 )
	if( tmp_dir )
	{
		free( tmp_dir );
		tmp_dir = NULL;
	}
#endif
	PL_strcatn( admin_logfile, sizeof(admin_logfile), "/admin.log");
 
    file = fopen(admin_logfile, "a+");
    if (file != NULL) {
        fwrite(msg, strlen(msg), 1, file);
        fclose(file);
    }
}

