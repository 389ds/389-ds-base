/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 *   nsctrs.h
 */
//
#ifndef _NSCTRMC_H_
#define _NSCTRMC_H_
//
//
//     Perfutil messages
//
//
//  Values are 32 bit values layed out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//


//
// MessageId: UTIL_LOG_OPEN
//
// MessageText:
//
//  An extensible counter has opened the Event Log for NSCTRS.DLL
//
#define UTIL_LOG_OPEN                    ((DWORD)0x4000076CL)

//
//
// MessageId: UTIL_CLOSING_LOG
//
// MessageText:
//
//  An extensible counter has closed the Event Log for NSCTRS.DLL
//
#define UTIL_CLOSING_LOG                 ((DWORD)0x400007CFL)

//
//
// MessageId: NSPERF_OPEN_FILE_MAPPING_ERROR
//
// MessageText:
//
//  Unable to open mapped file containing NS driver performance data.
//
#define NSPERF_OPEN_FILE_MAPPING_ERROR   ((DWORD)0xC00007D0L)

//
//
// MessageId: NSPERF_UNABLE_MAP_VIEW_OF_FILE
//
// MessageText:
//
//  Unable to map to shared memory file containing NS driver performance data.
//
#define NSPERF_UNABLE_MAP_VIEW_OF_FILE   ((DWORD)0xC00007D1L)

//
//
// MessageId: NSPERF_UNABLE_OPEN_DRIVER_KEY
//
// MessageText:
//
//  Unable open "Performance" key of NS driver in registry. Status code is returned in data.
//
#define NSPERF_UNABLE_OPEN_DRIVER_KEY    ((DWORD)0xC00007D2L)

//
//
// MessageId: NSPERF_UNABLE_READ_FIRST_COUNTER
//
// MessageText:
//
//  Unable to read the "First Counter" value under the NS\Performance Key. Status codes returned in data.
//
#define NSPERF_UNABLE_READ_FIRST_COUNTER ((DWORD)0xC00007D3L)

//
//
// MessageId: NSPERF_UNABLE_READ_FIRST_HELP
//
// MessageText:
//
//  Unable to read the "First Help" value under the NS\Performance Key. Status codes returned in data.
//
#define NSPERF_UNABLE_READ_FIRST_HELP    ((DWORD)0xC00007D4L)

//
#endif // _NSCTRMC_H_
