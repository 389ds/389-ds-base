; BEGIN COPYRIGHT BLOCK
; Copyright 2001 Sun Microsystems, Inc.
; Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
; All rights reserved.
; END COPYRIGHT BLOCK
;
;/*
; *   nsctrs.h
; */
;//
;#ifndef _NSCTRMC_H_
;#define _NSCTRMC_H_
;//
MessageIdTypedef=DWORD
;//
;//     Perfutil messages
;//
MessageId=1900
Severity=Informational
Facility=Application
SymbolicName=UTIL_LOG_OPEN
Language=English
An extensible counter has opened the Event Log for NSCTRS.DLL
.
;//
MessageId=1999
Severity=Informational
Facility=Application
SymbolicName=UTIL_CLOSING_LOG
Language=English
An extensible counter has closed the Event Log for NSCTRS.DLL
.
;//
MessageId=2000
Severity=Error
Facility=Application
SymbolicName=NSPERF_OPEN_FILE_MAPPING_ERROR
Language=English
Unable to open mapped file containing NS driver performance data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=NSPERF_UNABLE_MAP_VIEW_OF_FILE
Language=English
Unable to map to shared memory file containing NS driver performance data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=NSPERF_UNABLE_OPEN_DRIVER_KEY
Language=English
Unable open "Performance" key of NS driver in registry. Status code is returned in data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=NSPERF_UNABLE_READ_FIRST_COUNTER
Language=English
Unable to read the "First Counter" value under the NS\Performance Key. Status codes returned in data.
.
;//
MessageId=+1
Severity=Error
Facility=Application
SymbolicName=NSPERF_UNABLE_READ_FIRST_HELP
Language=English
Unable to read the "First Help" value under the NS\Performance Key. Status codes returned in data.
.
;//
;#endif // _NSCTRMC_H_
