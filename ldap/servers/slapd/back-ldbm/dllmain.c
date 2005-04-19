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
/*
 * Microsoft Windows specifics for BACK-LDBM DLL
 */
#include "back-ldbm.h"


#ifdef _WIN32
/* Lifted from Q125688
 * How to Port a 16-bit DLL to a Win32 DLL
 * on the MSVC 4.0 CD
 */
BOOL WINAPI DllMain (HANDLE hModule, DWORD fdwReason, LPVOID lpReserved)
{

	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		 /* Code from LibMain inserted here.  Return TRUE to keep the
		    DLL loaded or return FALSE to fail loading the DLL.

		    You may have to modify the code in your original LibMain to
		    account for the fact that it may be called more than once.
		    You will get one DLL_PROCESS_ATTACH for each process that
		    loads the DLL. This is different from LibMain which gets
		    called only once when the DLL is loaded. The only time this
		    is critical is when you are using shared data sections.
		    If you are using shared data sections for statically
		    allocated data, you will need to be careful to initialize it
		    only once. Check your code carefully.

		    Certain one-time initializations may now need to be done for
		    each process that attaches. You may also not need code from
		    your original LibMain because the operating system may now
		    be doing it for you.
		 */
		/*
		 * 16 bit code calls UnlockData()
		 * which is mapped to UnlockSegment in windows.h
		 * in 32 bit world UnlockData is not defined anywhere
		 * UnlockSegment is mapped to GlobalUnfix in winbase.h
		 * and the docs for both UnlockSegment and GlobalUnfix say 
		 * ".. function is oboslete.  Segments have no meaning 
		 *  in the 32-bit environment".  So we do nothing here.
		 */


		break;

	case DLL_THREAD_ATTACH:
		/* Called each time a thread is created in a process that has
		   already loaded (attached to) this DLL. Does not get called
		   for each thread that exists in the process before it loaded
		   the DLL.

		   Do thread-specific initialization here.
		*/
		break;

	case DLL_THREAD_DETACH:
		/* Same as above, but called when a thread in the process
		   exits.

		   Do thread-specific cleanup here.
		*/
		break;

	case DLL_PROCESS_DETACH:
		/* Code from _WEP inserted here.  This code may (like the
		   LibMain) not be necessary.  Check to make certain that the
		   operating system is not doing it for you.
		*/

		break;
	}
	/* The return value is only used for DLL_PROCESS_ATTACH; all other
	conditions are ignored.  */
	return TRUE;   // successful DLL_PROCESS_ATTACH
}
#else
int CALLBACK
LibMain( HINSTANCE hinst, WORD wDataSeg, WORD cbHeapSize, LPSTR lpszCmdLine )
{
	/*UnlockData( 0 );*/
 	return( 1 );
}
#endif

#ifdef LDAP_DEBUG
#ifndef _WIN32
#include <stdarg.h>
#include <stdio.h>

void LDAPDebug( int level, char* fmt, ... )
{
	static char debugBuf[1024];

	if (slapd_ldap_debug & level)
	{
		va_list ap;
		va_start (ap, fmt);
		_snprintf (debugBuf, sizeof(debugBuf), fmt, ap);
		va_end (ap);

		OutputDebugString (debugBuf);
	}
}
#endif
#endif

#ifndef _WIN32

/* The 16-bit version of the RTL does not implement perror() */

#include <stdio.h>

void perror( const char *msg )
{
	char buf[128];
	wsprintf( buf, "%s: error %d\n", msg, WSAGetLastError()) ;
	OutputDebugString( buf );
}

#endif
