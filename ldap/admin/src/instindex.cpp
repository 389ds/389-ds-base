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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * index.c: Shows the first page you see on install
 * 
 * Rob McCool
 */

#include <nss.h>
#include <libadminutil/distadm.h>

#include "create_instance.h"
#include "configure_instance.h"

#include "dsalib.h"
#include "ldap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------- main --------------------------------- */

static void
printInfo(int argc, char *argv[], char *envp[], FILE* fp)
{
	int ii = 0;
	if (!fp)
		fp = stdout;

	fprintf(fp, "Program name = %s\n", argv[0]);
	for (ii = 1; ii < argc; ++ii)
	{
		fprintf(fp, "argv[%d] = %s\n", ii, argv[ii]);
	}

	for (ii = 0; envp[ii]; ++ii)
	{
		fprintf(fp, "%s\n", envp[ii]);
	}

	fprintf(fp, "#####################################\n");
}

#if defined (__hpux) && defined (__ia64)
int main(int argc, char *argv[], char *envp[])
#else
int main(int argc, char *argv[], char * /*envp*/ [])
#endif
{
    char *rm = getenv("REQUEST_METHOD");
    int status = 0;
    server_config_s cf;
	char *infFileName = 0;
	int reconfig = 0;
	int ii = 0;
	int cgi = 0;

	(void)ADMUTIL_Init();
	
	/* Initialize NSS to make ds_salted_sha1_pw_enc() happy */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		ds_report_error(DS_GENERAL_FAILURE, " initialization failure",
				"Unable to initialize the NSS subcomponent.");
		exit(1);
	}

	/* make stdout unbuffered */
	setbuf(stdout, 0);

#ifdef XP_WIN32
    if ( getenv("DEBUG_DSINST") )
	DebugBreak();
#endif

    memset(&cf, 0, sizeof(cf));
    set_defaults(0, 0, &cf);

	/* scan cmd line arguments */
	for (ii = 0; ii < argc; ++ii)
	{
		if (!strcmp(argv[ii], "-f") && (ii + 1) < argc &&
			argv[ii+1])
			infFileName = argv[ii+1];
		else if (!strcmp(argv[ii], "-r"))
			reconfig = 1;
	}

    /* case 1: being called as program -f inffile */
    if (infFileName)
    {
		FILE *infFile = fopen(infFileName, "r");
		if (!infFile)
		{
			ds_report_error(DS_INCORRECT_USAGE, infFileName,
				"This file could not be opened.  A valid file must be given.");
			status = 1;
		}
		else
			fclose(infFile);

		if (reconfig)
			status = reconfigure_instance(argc, argv);
		else
		{
			if (!status)
				status = create_config_from_inf(&cf, argc, argv);
			if (!status)
				status = create_config(&cf);
			if (!status)
				status = configure_instance();
		}
    }
    /* case 2: being called as a CGI */
    else if (rm)
    {
		cgi = 1;
        status = parse_form(&cf);
		if (!status)
			status = create_config(&cf);
		if (!status)
			status = configure_instance_with_config(&cf, 1, 0);
    }
    /* case 3: punt */
    else
    {
		ds_report_error (
			DS_INCORRECT_USAGE,
			"No request method specified",
			"A REQUEST_METHOD must be specified (POST, GET) to run this CGI program.");
		status = 1;
    }

	if (cgi)
	{
		/* The line below is used by the console to detect
		   the end of the operation. See replyHandler() in
		   MigrateCreate.java */
		fprintf(stdout, "NMC_Status: %d\n", status);
		/* In the past, we used to call rpt_success() or rpt_err() 
		   according to status. However these functions are not designed
		   for our case: they print an HTTP header line "Content-type: text/html" */
	}

#if defined( hpux )
    _exit(status);
#endif
    return status;
}
