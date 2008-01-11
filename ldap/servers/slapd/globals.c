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
 *  Copyright (c) 1996 Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  SLAPD globals.c -- SLAPD library global variables
 */

#if defined(NET_SSL)
#include "ldap.h"
#include <sslproto.h> /* cipher suite names */
#include <ldap_ssl.h>

#undef OFF
#undef LITTLE_ENDIAN

#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#if defined( _WIN32 )
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
#else
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include "slap.h"
#include "fe.h"

/* On UNIX, there's only one copy of slapd_ldap_debug */
/* On NT, each module keeps its own module_ldap_debug, which */
/* points to the process' slapd_ldap_debug */
#ifdef _WIN32
int		*module_ldap_debug;
#endif

int		should_detach = 1;
time_t		starttime;
PRThread	*listener_tid;
Slapi_PBlock	*repl_pb = NULL;

/*
 * global variables that need mutex protection
 */
PRInt32		ops_initiated;
PRInt32		ops_completed;
PRLock		*ops_mutex;
int		num_conns;
PRLock		*num_conns_mutex;

/*
  DEC/COMPAQ has released a patch for 4.0d (e?) which will speed up
  malloc/free considerably in multithreaded multiprocessor
  applications (like directory server!), sort of like SmartHeap but
  not as snazzy.  The last three parameters only take effect if the
  patch is installed, otherwise they are ignored.  The rest of the
  parameters are included along with their default values, but they
  are commented out except:
  unsigned long __noshrink = 1; old - default is 0; apparently this is ignored for now
  int __fast_free_max = INT_MAX; old - default is 13; may cause excessive memory consumption
*/
#if defined(OSF1) && defined(LDAP_DONT_USE_SMARTHEAP)
/* From an email from Dave Long at DEC/Compaq:

   The following is an example of how to tune for maximum speed on a
   system with three or more CPUs and with no concern for memory used:
   
   #include <limits.h>
   #include <sys/types.h>
*/
unsigned long __noshrink = 1; /* old - default is 0; apparently this is ignored for now */
/*
  size_t __minshrink = 65536;
  double __minshrinkfactor = 0.001;
  size_t __mingrow = 65536;
  double __mingrowfactor = 0.1;
  unsigned long __madvisor = 0;
  unsigned long __small_buff = 0;
*/
int __fast_free_max = INT_MAX; /* old - default is 13; may cause excessive memory consumption */
/*
  unsigned long __sbrk_override = 0;
  unsigned long __taso_mode = 0;
*/

/*
  These are the new parameters
*/
int __max_cache = 27;
int __first_fit = 2;
int __delayed_free = 1;
/*
  Note that the allowed values for the new __max_cache tuning variable
  are: 15, 18, 21, 24, 27. Any other value is likely to actually harm
  performance or even cause a core dump.
*/
#endif


#if defined( _WIN32 )
/* String constants (no change for international) */
SERVICE_STATUS LDAPServerStatus;
SERVICE_STATUS_HANDLE hLDAPServerServiceStatus;
#endif

Connection_Table *the_connection_table = NULL;

char *pid_file = "/dev/null";
char *start_pid_file = "/dev/null";

char	*attr_dataversion	= ATTR_DATAVERSION;

extern void set_dll_entry_points( slapdEntryPoints *sep );
void
set_entry_points()
{
    slapdEntryPoints *sep;

    sep = (slapdEntryPoints *) slapi_ch_malloc( sizeof( slapdEntryPoints ));
    sep->sep_ps_wakeup_all = (caddr_t)ps_wakeup_all;
    sep->sep_ps_service = (caddr_t)ps_service_persistent_searches;
    sep->sep_disconnect_server = (caddr_t)disconnect_server;
    sep->sep_slapd_SSL_client_init = (caddr_t)slapd_SSL_client_init;
    sep->sep_slapd_ssl_init = (caddr_t)slapd_ssl_init;
    sep->sep_slapd_ssl_init2 = (caddr_t)slapd_ssl_init2;
    set_dll_entry_points( sep );
   
}
