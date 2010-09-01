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


#define LIBRARY_NAME "base"

#ifdef RESOURCE_STR
static char dbtbaseid[] = "$DBT: base referenced v1 $";
#endif

#include "i18n.h"

BEGIN_STR(base)
	ResDef( DBT_LibraryID_, -1, dbtbaseid )/* extracted from dbtbase.h*/
	ResDef( DBT_insufficientMemoryToCreateHashTa_, 1, "insufficient memory to create hash table" )/*extracted from cache.cpp*/
	ResDef( DBT_insufficientMemoryToCreateHashTa_1, 2, "insufficient memory to create hash table" )/*extracted from cache.cpp*/
	ResDef( DBT_cacheDestroyCacheTablesAppearCor_, 3, "cache_destroy: cache tables appear corrupt." )/*extracted from cache.cpp*/
	ResDef( DBT_unableToAllocateHashEntry_, 4, "unable to allocate hash entry" )/*extracted from cache.cpp*/
	ResDef( DBT_cacheInsertUnableToCreateCacheEn_, 5, "cache_insert: unable to create cache entry" )/*extracted from cache.cpp*/
	ResDef( DBT_http10200OkNcontentTypeTextHtmlN_, 6, "HTTP/1.0 200 OK\nContent-type: text/html\n\n" )/*extracted from cache.cpp*/
	ResDef( DBT_H2NetscapeCacheStatusReportH2N_, 7, "<H2>Cache status report</H2>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_noCachesOnSystemP_, 8, "No caches on system<P>" )/*extracted from cache.cpp*/
	ResDef( DBT_H2SCacheH2N_, 9, "<H2>%s cache</H2>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_cacheHitRatioDDFPNPN_, 10, "Cache hit ratio: %d/%d (%f)</P>\n</P>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_cacheSizeDDPNPN_, 11, "Cache size: %d/%d</P>\n</P>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_hashTableSizeDPNPN_, 12, "Hash table size: %d</P>\n</P>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_mruDPNlruDPN_, 13, "mru       : %d</P>\nlru       : %d</P>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_UlTableBorder4ThBucketThThAddres_, 14, "<UL><TABLE BORDER=4> <TH>Bucket</TH> <TH>Address</TH> <TH>Key</TH> <TH>Access Count</TH> <TH>Delete</TH> <TH>Next</TH> <TH>LRU</TH> <TH>MRU</TH> <TH>Data</TH>\n" )/*extracted from cache.cpp*/
	ResDef( DBT_munmapFailedS_, 15, "munmap failed (%s)" )/*extracted from buffer.cpp*/
	ResDef( DBT_munmapFailedS_1, 16, "munmap failed (%s)" )/*extracted from buffer.cpp*/
	ResDef( DBT_closeFailedS_, 17, "close failed (%s)" )/*extracted from buffer.cpp*/
	ResDef( DBT_daemonUnableToForkNewProcessSN_, 18, "daemon: unable to fork new process (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_daemonSetsidFailedSN_, 19, "daemon: setsid failed (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_daemonCanTLogPidToSSN_, 20, "daemon: can't log pid to %s (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningCouldNotSetGroupIdToDSN_, 21, "warning: could not set group id to %d (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningCouldNotSetUserIdToDSN_, 22, "warning: could not set user id to %d (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningDaemonIsRunningAsSuperUse_, 23, "warning: daemon is running as super-user\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_couldNotDetermineCurrentUserName_, 24, "could not determine current user name\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_errorChrootToSFailedSN_, 25, "error: chroot to %s failed (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_AddressS_, 27, ", address %s" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningStatisticsDisabledSN_, 28, "warning: statistics disabled (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_securityHandshakeTimedOutForPidD_, 29, "security handshake timed out for pid %d" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningStatisticsDisabledSN_1, 30, "warning: statistics disabled (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_secureHandshakeFailedCodeDN_, 31, "secure handshake failed (code %d)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_acceptFailedS_, 32, "accept failed (%s)" )/*extracted from daemon.cpp*/
	ResDef( DBT_warningStatisticsDisabledSN_2, 33, "warning: statistics disabled (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_selectThreadMiss_, 34, "select thread miss" )/*extracted from daemon.cpp*/
	ResDef( DBT_keepaliveWorkerAwokenWithNoWorkT_, 35, "keepalive worker awoken with no work to do" )/*extracted from daemon.cpp*/
	ResDef( DBT_couldNotCreateNewThreadDS_, 36, "could not create new thread: %d (%s)" )/*extracted from daemon.cpp*/
	ResDef( DBT_waitForSemaSucceededButNothingTo_, 37, "wait for sema succeeded, but nothing to dequeue" )/*extracted from daemon.cpp*/
	ResDef( DBT_queueSemaCreationFailure_, 38, "queue-sema creation failure" )/*extracted from daemon.cpp*/
	ResDef( DBT_errorGettingProcessorInfoForProc_, 39, "error getting processor info for processor %d" )/*extracted from daemon.cpp*/
	ResDef( DBT_errorBindingToProcessorD_, 40, "Error binding to processor %d" )/*extracted from daemon.cpp*/
	ResDef( DBT_boundProcessDToProcessorD_, 41, "bound process %d to processor %d" )/*extracted from daemon.cpp*/
	ResDef( DBT_netscapeServerIsNotExplicitlyBin_, 42, "Server is not explicitly binding to any processors." )/*extracted from daemon.cpp*/
	ResDef( DBT_cacheMonitorExited_, 43, "cache monitor exited" )/*extracted from daemon.cpp*/
	ResDef( DBT_cacheBatchUpdateDaemonExited_, 44, "cache batch update daemon exited" )/*extracted from daemon.cpp*/
	ResDef( DBT_usingSingleThreadedAccepts_, 45, "Using single threaded accepts." )/*extracted from daemon.cpp*/
	ResDef( DBT_usingMultiThreadedAccepts_, 46, "Using multi threaded accepts." )/*extracted from daemon.cpp*/
	ResDef( DBT_usingPartialSingleThreadedAccept_, 47, "Using partial single threaded accepts." )/*extracted from daemon.cpp*/
	ResDef( DBT_thisMachineHasDProcessors_, 48, "This machine has %d processors." )/*extracted from daemon.cpp*/
	ResDef( DBT_errorCallingThrSeconcurrencyDS_, 49, "Error calling thr_seconcurrency(%d)- (%s)" )/*extracted from daemon.cpp*/
	ResDef( DBT_setConncurrencyToD_, 50, "Set conncurrency to %d." )/*extracted from daemon.cpp*/
	ResDef( DBT_warningNetscapeExecutableAndLibr_, 51, "WARNING! executable and library have different versions.\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_seminitFailedSN_, 54, "seminit failed (%s)\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_thisBetaSoftwareHasExpiredN_, 55, "This beta software has expired.\n" )/*extracted from daemon.cpp*/
	ResDef( DBT_cacheMonitorRespawned_, 56, "Cache monitor respawned" )/*extracted from daemon.cpp*/
	ResDef( DBT_cacheBatchUpdateDaemonRespawned_, 57, "Cache batch update daemon respawned" )/*extracted from daemon.cpp*/
	ResDef( DBT_canTFindEmptyStatisticsSlot_, 58, "can't find empty statistics slot" )/*extracted from daemon.cpp*/
	ResDef( DBT_canTForkNewProcessS_, 59, "can't fork new process (%s)" )/*extracted from daemon.cpp*/
	ResDef( DBT_assertFailedSN_, 60, "assert failed! %s\n" )/*extracted from multiplex.c*/
	ResDef( DBT_mrTableInit_, 61, "mr_table_init()" )/*extracted from multiplex.c*/
	ResDef( DBT_mallocFailed_, 62, "malloc failed" )/*extracted from multiplex.c*/
	ResDef( DBT_mallocFailed_1, 63, "malloc failed!" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAddIoDTypeDFileD_, 64, "mr_add_io(%d, type %d, file %d)" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAddIoStage1_, 65, "mr_add_io - stage 1" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAddIoStage2_, 66, "mr_add_io - stage 2" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAddIoFoundInvalidIoTypeD_, 67, "mr_add_io found invalid IO type %d" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAddIoAddingTimeout_, 68, "mr_add_io - adding timeout" )/*extracted from multiplex.c*/
	ResDef( DBT_outOfMemoryN_, 69, "Out of memory!\n" )/*extracted from multiplex.c*/
	ResDef( DBT_doneWithMrAddIo_, 70, "done with mr_add_io" )/*extracted from multiplex.c*/
	ResDef( DBT_mrDelIoDTypeDFileD_, 71, "mr_del_io(%d, type %d, file %d)" )/*extracted from multiplex.c*/
	ResDef( DBT_mrDelIoFoundInvalidIoTypeD_, 72, "mr_del_io found invalid IO type %d" )/*extracted from multiplex.c*/
	ResDef( DBT_mrLookupIoD_, 73, "mr_lookup_io(%d)" )/*extracted from multiplex.c*/
	ResDef( DBT_mrAsyncIoDDBytesFileD_, 74, "mr_async_io(%d, %d bytes, file %d)" )/*extracted from multiplex.c*/
	ResDef( DBT_mallocFailureAddingAsyncIo_, 75, "malloc failure adding async IO" )/*extracted from multiplex.c*/
	ResDef( DBT_errorAddingAsyncIo_, 76, "Error adding async io!" )/*extracted from multiplex.c*/
	ResDef( DBT_cannotSeekForRead_, 77, "Cannot seek for read!" )/*extracted from multiplex.c*/
	ResDef( DBT_readFailureDS_, 78, "read failure! (%d, %s)" )/*extracted from multiplex.c*/
	ResDef( DBT_doReadReadDBytesForFileD_, 79, "do_read read %d bytes for file %d" )/*extracted from multiplex.c*/
	ResDef( DBT_cannotSeekForWrite_, 80, "Cannot seek for write!" )/*extracted from multiplex.c*/
	ResDef( DBT_writevFailureDS_, 81, "writev failure! (%d, %s)" )/*extracted from multiplex.c*/
	ResDef( DBT_writeFailureDS_, 82, "write failure! (%d, %s)" )/*extracted from multiplex.c*/
	ResDef( DBT_doWriteWroteDBytesForFileD_, 83, "do_write wrote %d bytes for file %d" )/*extracted from multiplex.c*/
	ResDef( DBT_doTimeoutMrpD_, 84, "do_timeout(mrp %d)" )/*extracted from multiplex.c*/
	ResDef( DBT_doTimeoutFoundIoTimerDTimeD_, 85, "do_timeout: found IO (timer=%d, time=%d)" )/*extracted from multiplex.c*/
	ResDef( DBT_errorDeletingIo_, 86, "error deleting io" )/*extracted from multiplex.c*/
	ResDef( DBT_timeoutCallbackFailureForDN_, 87, "timeout callback failure for %d\n" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventDOutstandingIoD_, 88, "mr_get_event(%d) - outstanding io %d" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventWaitingForReadsOnFd_, 89, "mr_get_event: Waiting for reads on FD:" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventWaitingForWritesOnFd_, 90, "mr_get_event: Waiting for writes on FD:" )/*extracted from multiplex.c*/
	ResDef( DBT_TD_, 91, "\t%d" )/*extracted from multiplex.c*/
	ResDef( DBT_TD_1, 92, "\t%d" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventSetNoTimeout_, 93, "mr_get_event set no timeout" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventSetTimeoutToDDSec_, 94, "mr_get_event set timeout to: %d.%d sec" )/*extracted from multiplex.c*/
	ResDef( DBT_errorInSelectDS_, 95, "error in select (%d, %s)" )/*extracted from multiplex.c*/
	ResDef( DBT_mrGetEventSelectFoundD_, 96, "mr_get_event() - select found %d" )/*extracted from multiplex.c*/
	ResDef( DBT_errorLookingUpIoFdD_, 97, "error looking up IO fd %d" )/*extracted from multiplex.c*/
	ResDef( DBT_readFailedForFdD_, 98, "read failed for fd %d" )/*extracted from multiplex.c*/
	ResDef( DBT_errorDeletingIo_1, 99, "error deleting io" )/*extracted from multiplex.c*/
	ResDef( DBT_callbackFailureForDN_, 100, "callback failure for %d\n" )/*extracted from multiplex.c*/
	ResDef( DBT_errorLookingUpIoFdD_1, 101, "error looking up IO fd %d" )/*extracted from multiplex.c*/
	ResDef( DBT_writingHeaderLenDWritelenDTotalD_, 102, "writing: header len %d, writelen %d, total %d" )/*extracted from multiplex.c*/
	ResDef( DBT_writeFailedForFdD_, 103, "write failed for fd %d" )/*extracted from multiplex.c*/
	ResDef( DBT_errorDeletingIo_2, 104, "error deleting io" )/*extracted from multiplex.c*/
	ResDef( DBT_callbackFailureForDN_1, 105, "callback failure for %d\n" )/*extracted from multiplex.c*/
	ResDef( DBT_errorCreatingDnsCache_, 106, "Error creating dns cache" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInitHashSize0UsingD_, 107, "dns_cache_init: hash_size <= 0, using %d" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInitCacheSizeDUsingD_, 108, "dns_cache_init: cache-size <= %d, using %d" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInitCacheSizeIsDIsTooLar_, 109, "dns_cache_init: cache-size is %d is too large, using %d." )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInitExpireTime0UsingD_, 110, "dns_cache_init: expire_time <= 0, using %d" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInitExpireIsDIsTooLargeU_, 111, "dns_cache_init: expire is %d is too large, using %d seconds." )/*extracted from dns_cache.cpp*/
	ResDef( DBT_errorCreatingDnsCache_1, 112, "Error creating dns cache" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInsertErrorAllocatingEnt_, 113, "dns-cache-insert: Error allocating entry" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_dnsCacheInsertMallocFailure_, 114, "dns-cache-insert: malloc failure" )/*extracted from dns_cache.cpp*/
	ResDef( DBT_successfulServerStartup_, 115, "successful server startup" )/*extracted from ereport.cpp*/
	ResDef( DBT_SBS_, 116, "%s B%s" )/*extracted from ereport.cpp*/
	ResDef( DBT_netscapeExecutableAndSharedLibra_, 117, "executable and shared library have different versions" )/*extracted from ereport.cpp*/
	ResDef( DBT_executableVersionIsS_, 118, "   executable version is %s" )/*extracted from ereport.cpp*/
	ResDef( DBT_sharedLibraryVersionIsS_, 119, "   shared library version is %s" )/*extracted from ereport.cpp*/
	ResDef( DBT_errorReportingShuttingDown_, 120, "error reporting shutting down" )/*extracted from ereport.cpp*/
	ResDef( DBT_warning_, 121, "warning" )/*extracted from ereport.cpp*/
	ResDef( DBT_config_, 122, "config" )/*extracted from ereport.cpp*/
	ResDef( DBT_security_, 123, "security" )/*extracted from ereport.cpp*/
	ResDef( DBT_failure_, 124, "failure" )/*extracted from ereport.cpp*/
	ResDef( DBT_catastrophe_, 125, "catastrophe" )/*extracted from ereport.cpp*/
	ResDef( DBT_info_, 126, "info" )/*extracted from ereport.cpp*/
	ResDef( DBT_verbose_, 127, "verbose" )/*extracted from ereport.cpp*/
	ResDef( DBT_eventHandlerFailedToWaitOnEvents_, 128, "event_handler:Failed to wait on events %s" )/*extracted from eventhandler.cpp*/
	ResDef( DBT_couldNotWaitOnResumeEventEventS_, 129, "could not wait on resume event event  (%s)" )/*extracted from eventhandler.cpp*/
	ResDef( DBT_dlopenOfSFailedS_, 130, "dlopen of %s failed (%s)" )/*extracted from LibMgr.cpp*/
	ResDef( DBT_dlopenOfSFailedS_1, 131, "dlopen of %s failed (%s)" )/*extracted from LibMgr.cpp*/
	ResDef( DBT_theServerIsTerminatingDueToAnErr_, 132, "The server is terminating due to an error. Check the event viewer for the error message. SERVER EXITING!" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_terminatingTheServerS_, 133, "Terminating the server %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_killServerCannotOpenServerEventS_, 134, "kill_server:cannot open server event %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_killServerCannotSetServerEventS_, 135, "kill_server:cannot set server event %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_errorCouldNotGetSocketSN_, 136, "error: could not get socket (%s)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_errorCouldNotSetSocketOptionSN_, 137, "error: could not set socket option (%s)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_terminatingServiceErrorCouldNotB_, 138, "Terminating Service:error: could not bind to address %s port %d (%s)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_terminatingServiceErrorCouldNotB_1, 139, "Terminating Service:error: could not bind to port %d (%s)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_sethandlenoninheritableCouldNotD_, 140, "SetHandleNonInheritable: could not duplicate socket (%s)" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_sethandlenoninheritableClosingTh_, 141, "SetHandleNonInheritable: closing the original socket failed (%s)" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_couldNotSethandleinformationS_, 142, "Could not SetHandleInformation (%s)" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_terminatingServiceFailureCouldNo_, 143, "Terminating Service:Failure: Could not open statistics file (%s)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_couldNotSetThreadLocalStorageVal_, 144, "Could not set Thread Local Storage Value for thread at slot %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_secureHandshakeFailedCodeDN_1, 145, "secure handshake failed (code %d)\n" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_acceptFailedDS_, 146, "accept failed %d (%s)" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_failedToPulseEventDS_, 147, "Failed to pulse Event %d %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_failedToSendMobgrowthEventToPare_, 148, "Failed to send MobGrowth Event to parent %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_pulsingMobrespawnEventD_, 149, "Pulsing MobRespawn Event %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_respawnThreadPoolToDD_, 150, "respawn thread pool to %d (%d)" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_couldNotOpenEventToSignalRotateA_, 151, "Could not open event to signal rotate application. Could not create the MoveLog event:%s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_failedToSendMovelogEventToRotate_, 152, "Failed to send MoveLog Event to rotate app %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_growingThreadPoolFromDToD_, 153, "growing thread pool from %d to %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_couldNotOpenTheServicecontrolman_, 154, "Could not open the ServiceControlManager, Error %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_startnetsiteserviceCouldNotOpenT_, 155, "StartNetsiteService:Could not open the service %s: Error %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_startnetsiteserviceCouldNotStart_, 156, "StartNetsiteService:Could not start the service %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_serviceStartupCouldNotAllocateSe_, 157, "Service Startup: Could not allocate security descriptor" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_serviceStartupCouldNotInitSecuri_, 158, "Service Startup: Could not init security descriptor" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_serviceStartupCouldNotSetTheSecu_, 159, "Service Startup: Could not set the security Dacl" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_terminatingServiceWinsockInitFai_, 160, "Terminating Service:WinSock init failed: %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_httpdServerStartupFailedS_, 161, "Httpd Server Startup failed: %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_canTFindEmptyStatisticsSlot_1, 162, "can't find empty statistics slot" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_ntDaemonCouldNotCreateNewThreadD_, 163, "NT daemon: could not create new thread %d" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_serviceStartupFailureTerminating_, 164, "Service Startup Failure. Terminating Service:Could not create event %d:%s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_serviceStartupErrorCouldNotCreat_, 165, "Service Startup Error. Could not create the MoveLog event:%s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_failedToWaitOnEventObjectsS_, 166, "Failed to wait on Event objects %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_failedToWaitOnEventObjectsS_1, 167, "Failed to wait on Event objects %s" )/*extracted from ntdaemon.cpp*/
	ResDef( DBT_pipebufBuf2sdPipebufGrabIoErrorD_, 168, "pipebuf_buf2sd: pipebuf_grab IO_ERROR %d" )/*extracted from ntpipe.cpp*/
	ResDef( DBT_poolInitMemoryPoolsDisabled_, 169, "pool-init: memory pools disabled" )/*extracted from pool.cpp*/
	ResDef( DBT_poolInitFreeSize0UsingD_, 170, "pool-init: free_size <= 0, using %d" )/*extracted from pool.cpp*/
	ResDef( DBT_poolCreateBlockOutOfMemory_, 171, "pool-create-block: out of memory" )/*extracted from pool.cpp*/
	ResDef( DBT_poolCreateOutOfMemory_, 172, "pool-create: out of memory" )/*extracted from pool.cpp*/
	ResDef( DBT_poolCreateOutOfMemory_1, 173, "pool-create: out of memory" )/*extracted from pool.cpp*/
	ResDef( DBT_poolMallocOutOfMemory_, 174, "pool-malloc: out of memory" )/*extracted from pool.cpp*/
	ResDef( DBT_freeUsedWherePermFreeShouldHaveB_, 175, "FREE() used where PERM_FREE() should have been used- problem corrected and supressing further warnings." )/*extracted from pool.cpp*/
	ResDef( DBT_regexErrorSRegexS_, 176, "regex error: %s (regex: '%s')" )/*extracted from regexp.cpp*/
	ResDef( DBT_canTCreateIpcPipeS_, 177, "can't create IPC pipe (%s)" )/*extracted from thrconn.cpp*/
	ResDef( DBT_writeToWakeupPipeFailedS_, 178, "write to wakeup pipe failed (%s)" )/*extracted from thrconn.cpp*/
	ResDef( DBT_flushingDConnectionsCurrentDTotD_, 179, "flushing %d connections; current %d; tot %d" )/*extracted from thrconn.cpp*/
	ResDef( DBT_acceptFailedS_1, 180, "accept failed (%s)" )/*extracted from thrconn.cpp*/
	ResDef( DBT_errorCreatingTimeCache_, 181, "Error creating time cache" )/*extracted from time_cache.cpp*/
	ResDef( DBT_timeCacheCacheDisabled_, 182, "time-cache: cache disabled" )/*extracted from time_cache.cpp*/
	ResDef( DBT_timeCacheInitHashSizeDUsingDefau_, 183, "time_cache_init: hash_size < %d, using default, %d" )/*extracted from time_cache.cpp*/
	ResDef( DBT_timeCacheInitHashSizeDUsingDefau_1, 184, "time_cache_init: hash_size > %d, using default, %d" )/*extracted from time_cache.cpp*/
	ResDef( DBT_timeCacheInitCacheSizeDUsingDefa_, 185, "time_cache_init: cache_size < %d, using default, %d" )/*extracted from time_cache.cpp*/
	ResDef( DBT_timeCacheInitCacheSizeDUsingDefa_1, 186, "time_cache_init: cache_size > %d, using default, %d" )/*extracted from time_cache.cpp*/
	ResDef( DBT_errorAllocatingMemoryForTimeCach_, 187, "Error allocating memory for time_cache" )/*extracted from time_cache.cpp*/
	ResDef( DBT_errorAllocatingMemoryForTimeCach_1, 188, "Error allocating memory for time_cache entry" )/*extracted from time_cache.cpp*/
	ResDef( DBT_errorAllocatingMemoryForTimeCach_2, 189, "Error allocating memory for time_cache entry" )/*extracted from time_cache.cpp*/
	ResDef( DBT_errorInsertingNewTimeCacheEntry_, 190, "Error inserting new time_cache entry" )/*extracted from time_cache.cpp*/
	ResDef( DBT_errorAllocatingMemoryForTimeCach_3, 191, "Error allocating memory for time_cache" )/*extracted from time_cache.cpp*/
	ResDef( DBT_csTerminateFailureS_, 192, "cs-terminate failure (%s)" )/*extracted from crit.cpp*/
	ResDef( DBT_csInitFailureS_, 193, "cs-init failure (%s)" )/*extracted from crit.cpp*/
	ResDef( DBT_csWaitFailureS_, 194, "cs-wait failure (%s)" )/*extracted from crit.cpp*/
	ResDef( DBT_csPostFailureS_, 195, "cs-post failure (%s)" )/*extracted from crit.cpp*/
	ResDef( DBT_unableToCreateNonblockingSocketS_, 196, "Unable to create nonblocking socket (%s)" )/*extracted from net.cpp*/
	ResDef( DBT_errorCouldNotSetKeepaliveSN_, 197, "error: could not set keepalive (%s)\n" )/*extracted from net.cpp*/
	ResDef( DBT_errorCouldNotSetRecvTimeoutSN_, 198, "error: could not set recv timeout (%s)\n" )/*extracted from net.cpp*/
	ResDef( DBT_errorCouldNotSetSendTimeoutSN_, 199, "error: could not set send timeout (%s)\n" )/*extracted from net.cpp*/
	ResDef( DBT_unableToCreateNonblockingSocketS_1, 200, "Unable to create nonblocking socket (%s)" )/*extracted from net.cpp*/
	ResDef( DBT_semGrabFailedS_, 201, "sem_grab failed (%s)" )/*extracted from net.cpp*/
	ResDef( DBT_semReleaseFailedS_, 202, "sem_release failed (%s)" )/*extracted from net.cpp*/
	ResDef( DBT_semReleaseFailedS_1, 203, "sem_release failed (%s)" )/*extracted from net.cpp*/
	ResDef( DBT_couldNotRemoveTemporaryDirectory_, 204, "Could not remove temporary directory %s,  Error %d" )/*extracted from util.cpp*/
	ResDef( DBT_couldNotRemoveTemporaryDirectory_1, 205, "Could not remove temporary directory %s, Error %d" )/*extracted from util.cpp*/
END_STR(base)
