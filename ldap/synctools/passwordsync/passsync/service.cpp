/* --- BEGIN COPYRIGHT BLOCK ---
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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

// Created: 2-8-2005
// Author(s): Scott Bridges

#include <windows.h>
#include <iostream>
#include "syncserv.h"
#include "dssynchmsg.h"
// syncserv.h
//   ntservice.h (modified)
//   ntservice.cpp (modified)
//     sysplat
//     uniutil
//     dssynchmsg

// Copied: 2-10-2005
// From: ntsynch.cpp
//#include "sysplat.h"

//#include "NTSynch.h"
//#include "ldapconn.h"
//#include "dsperson.h"
#include "synchcmds.h"
#include "dssynch.h"

#ifdef _DEBUG
void doDebug( PassSyncService *pSynch );
#endif // _DEBUG

//#include <nspr.h>

/////////////////////////////////////////////////////////////////
static void usage()
{
	printf( "DS Synchronization Service version %s\n",
		SYNCH_VERSION );
	printf(	"  -%c NUMBER    NT polling interval (minutes)\n",
								SYNCH_CMD_NT_POLL_INTERVAL );
//	printf(	"  -%c NUMBER    DS polling interval (minutes)\n",
//								SYNCH_CMD_DS_POLL_INTERVAL );
//	printf(	"  -%c CALENDAR  NT update schedule\n",
//								SYNCH_CMD_NT_CALENDAR );
//	printf(	"  -%c CALENDAR  DS update schedule\n",
//								SYNCH_CMD_DS_CALENDAR );
	printf(	"  -%c NUMBER    NT synchronization start time (minutes)\n",
								SYNCH_CMD_NT_CALENDAR );
//	printf(	"  -%c NUMBER    DS synchronization start time (minutes)\n",
//								SYNCH_CMD_DS_CALENDAR );
	printf(	"  -%c NAME      DS distinguished name\n",
								SYNCH_CMD_ADMIN_DN );
	printf(	"  -%c USERS_BASE      DS users base\n",
								SYNCH_CMD_DIRECTORY_USERS_BASE );
	printf(	"  -%c GROUPS_BASE     DS groups base\n",
								SYNCH_CMD_DIRECTORY_GROUPS_BASE );
	printf(	"  -%c HOST      DS host\n",
								SYNCH_CMD_DS_HOST );
	printf(	"  -%c NUMBER    DS port\n",
								SYNCH_CMD_DS_PORT );
	printf(	"  -%c PASSWORD  DS password\n",
								SYNCH_CMD_ADMIN_PASSWORD );
	printf(	"  -%c NUMBER    Command port\n",
								SYNCH_CMD_NT_PORT );
	printf(	"  -v           Display this message\n",
								SYNCH_CMD_NT_POLL_INTERVAL );
	printf(	"  -i           Install the service\n" );
	printf(	"  -u           Uninstall the service\n" );
	printf(	"  -%c           Synchronize all NT users to DS now\n",
								SYNCH_CMD_SYNCH_FROM_NT );
//	printf(	"  -%c           Synchronize DS users to NT now\n",
//								SYNCH_CMD_SYNCH_FROM_DS );
	printf(	"  -%c           Resynchronize changes to NT users now\n",
								SYNCH_CMD_SYNCH_CHANGES );
	printf(	"  -%c           Load settings from Registry\n",
								SYNCH_CMD_RELOAD_SETTINGS );
//	printf( "Options -t and -k are contradictory, as are -m and -y\n" );
}

#define OPT_NONE 0
#define OPT_START 1
#define OPT_APP 2
#define OPT_TERMINATE 3
#define OPT_START_DIRECT 4

/////////////////////////////////////////////////////////////////
static int checkOptions( PassSyncService *pSynch, int& argc, char *argv[] )
{
	int result = OPT_START;  // Default is to start the service

	// Check first for uninstall, since we shouldn't do anything else if set
	int i;
	for( i = 1; i < argc; i++ )
	{
		if ( !strncmp( argv[i], "-u", 2 ) )
		{
			// Uninstall
			if ( !pSynch->IsInstalled() )
				wprintf( L"%s is not installed\n", pSynch->m_szServiceName );
			else
			{
				// Try and remove the copy that's installed
				if ( pSynch->Uninstall() )
					wprintf( L"%s removed\n", pSynch->m_szServiceName );
				else
					wprintf( L"Could not remove %s. Error %d\n",
						pSynch->m_szServiceName, GetLastError() );
//				pSynch->ClearRegistry();
			}
			// Terminate after completion
			result = OPT_TERMINATE;
			argc = 1;
			return result;
		}
	}

	// Check command-line arguments
	for( i = 1; i < argc; )
	{
		if ( '-' != argv[i][0] )
		{
			i++;
			continue;
		}
		char opt = argv[i][1];
		BOOL bLocal = FALSE;

		// Usage
		if ( 'v' == opt )
		{
			result = OPT_NONE;
			usage();
			bLocal = TRUE;
		}
		// Secret option to start as app, not service
		else if ( 'a' == opt )
		{
			result = OPT_APP;
			bLocal = TRUE;
		}
		// Start service
		else if ( 'x' == opt )
		{
			result = OPT_START_DIRECT;
			bLocal = TRUE;
		}
/*
		// Command port
		else if ( 'c' == opt )
		{
			result = OPT_NONE;
			if ( i < (argc-1) )
			{
				i++;
				pSynch->SetCommandPort( atoi( argv[i] ) );
				bLocal = TRUE;
			}
		}
*/
		// Install
		else if ( 'i' == opt )
		{
			result = OPT_NONE;
			if ( pSynch->IsInstalled() )
				printf( "%S is already installed\n", pSynch->m_szServiceName );
			else
			{
				// Try and install the copy that's running
				if ( pSynch->Install() )
				{
					printf( "%S installed\n", pSynch->m_szServiceName );
				}
				else
				{
					printf( "%S failed to install. Error %d\n",
						pSynch->m_szServiceName, GetLastError() );
				}
			}
			bLocal = TRUE;
		}
		// Synchronize from NT to DS
		// Terminate after completion
		else if ( 'n' == opt )
		{
			result = OPT_NONE;
		}
		// Synchronize from DS to NT
		// Terminate after completion
		else if ( 's' == opt )
		{
			result = OPT_NONE;
		}
		// Synchronize both ways
		// Terminate after completion
		else if ( 'r' == opt )
		{
			result = OPT_NONE;
		}
		if ( bLocal )
		{
			for( int j = i; j < (argc-1); j++ )
			{
				argv[j] = argv[j+1];
			}
			argc--;
		}
		else
		{
			i++;
			if ( i >= argc )
				break;
		}
	}
	return result;
}

static int initialize( PassSyncService *pSynch, int argc, char *argv[] )
{
	// Check command-line arguments
	for( int i = 1; i < argc; i++ )
	{
		if ( '-' != argv[i][0] )
			continue;
		char opt = argv[i][1];

//		pSynch->argToSynch( opt, argv[i] );

		if ( i >= argc )
			break;
	}
	if ( argc > 1 )
	{
		// Save settings to Registry
//		pSynch->SaveConfig();
	}

	return 0;
}

/////////////////////////////////////////////////////////////////

int
main( int argc, char *argv[] )
{
	// Global single instance
	PassSyncService theSynch("Password Synchronization Service");

	// Process special install/uninstall switches; this does install/uninstall
	// It returns non-zero to actually start the service
	int nStart = checkOptions( &theSynch, argc, argv );

	// Set up configuration
	if ( OPT_TERMINATE != nStart )
		initialize( &theSynch, argc, argv );

	// Started by Service Control Manager
	if ( OPT_START == nStart )
	{
		// Start the service; doesn't return until the service is started
		BOOL bStarted = theSynch.StartService();
		if ( !bStarted )
		{
			printf( "Service could not be started\n" );
			return(1);
		}
		return 0;
	}
#if 0
	// Started from command line
	else if ( OPT_START_DIRECT == nStart )
	{
		// This may fail, but the rest still succeeds
		BOOL bStarted = theSynch.StartService();
		bStarted = theSynch.StartServiceDirect();
		if ( !bStarted )
		{
			printf( "Service could not be started\n" );
			return(1);
		}
		return 0;
	}
#endif
	// Secret debugging option - run as app instead of as service
	else if ( OPT_APP == nStart )
	{
		if ( theSynch.OnInit() )
			theSynch.Run();
	}

	exit(theSynch.m_Status.dwWin32ExitCode);


	////////// That's it - the rest is debugging stuff //////
#ifdef _DEBUG
	doDebug( &theSynch );
#endif
	return 0;
}
