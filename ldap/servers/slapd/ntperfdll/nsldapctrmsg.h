/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * nsctrmsg.h
 *
 * Defines EventLog error handling stuff for performance monitor dll.
 *
 */


#ifndef  _NSCTRMSG_H_
#define  _NSCTRMSG_H_
//
// Report error message ID's for Counters
//

#define APP_NAME  "nsctrs"

/*    Levels:  LOG_NONE = No event log messages ever
 *             LOG_USER = User event log messages (e.g. errors)
 *             LOG_DEBUG = Minimum Debugging 
 *             LOG_VERBOSE = Maximum Debugging 
 */

#define  LOG_NONE     0
#define  LOG_USER     1
#define  LOG_DEBUG    2
#define  LOG_VERBOSE  3

#define  MESSAGE_LEVEL_DEFAULT  LOG_USER

#define REPORT_SUCCESS(i,l) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_INFORMATION_TYPE, \
   0, i, (PSID)NULL, 0, 0, NULL, (PVOID)NULL) : FALSE)

#define REPORT_INFORMATION(i,l) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_INFORMATION_TYPE, \
   0, i, (PSID)NULL, 0, 0, NULL, (PVOID)NULL) : FALSE)

#define REPORT_WARNING(i,l) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_WARNING_TYPE, \
   0, i, (PSID)NULL, 0, 0, NULL, (PVOID)NULL) : FALSE)

#define REPORT_ERROR(i,l) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_ERROR_TYPE, \
   0, i, (PSID)NULL, 0, 0, NULL, (PVOID)NULL) : FALSE)

#define REPORT_INFORMATION_DATA(i,l,d,s) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_INFORMATION_TYPE, \
   0, i, (PSID)NULL, 0, s, NULL, (PVOID)(d)) : FALSE)

#define REPORT_WARNING_DATA(i,l,d,s) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_WARNING_TYPE, \
   0, i, (PSID)NULL, 0, s, NULL, (PVOID)(d)) : FALSE)

#define REPORT_ERROR_DATA(i,l,d,s) (MESSAGE_LEVEL >= l ? ReportEvent (hEventLog, EVENTLOG_ERROR_TYPE, \
   0, i, (PSID)NULL, 0, s, NULL, (PVOID)(d)) : FALSE)

extern HANDLE hEventLog;		/* handle to event log */
extern DWORD  dwLogUsers;		/* counter of event log using routines */
extern DWORD  MESSAGE_LEVEL;	/* event logging detail level */

#endif /* _NSCTRMSG_H_ */
