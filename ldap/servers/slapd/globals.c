/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
int		active_threads;
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
