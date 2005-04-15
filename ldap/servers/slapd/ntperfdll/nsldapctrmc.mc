; BEGIN COPYRIGHT BLOCK
; This Program is free software; you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the Free Software
; Foundation; version 2 of the License.
; 
; This Program is distributed in the hope that it will be useful, but WITHOUT
; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
; FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
; 
; You should have received a copy of the GNU General Public License along with
; this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
; Place, Suite 330, Boston, MA 02111-1307 USA.
; 
; In addition, as a special exception, Red Hat, Inc. gives You the additional
; right to link the code of this Program with code not covered under the GNU
; General Public License ("Non-GPL Code") and to distribute linked combinations
; including the two, subject to the limitations in this paragraph. Non-GPL Code
; permitted under this exception must only link to the code of this Program
; through those well defined interfaces identified in the file named EXCEPTION
; found in the source code files (the "Approved Interfaces"). The files of
; Non-GPL Code may instantiate templates or use macros or inline functions from
; the Approved Interfaces without causing the resulting work to be covered by
; the GNU General Public License. Only Red Hat, Inc. may make changes or
; additions to the list of Approved Interfaces. You must obey the GNU General
; Public License in all respects for all of the Program code and other code used
; in conjunction with the Program except the Non-GPL Code covered by this
; exception. If you modify this file, you may extend this exception to your
; version of the file, but you are not obligated to do so. If you do not wish to
; do so, delete this exception statement from your version. 
; 
; 
; Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
; Copyright (C) 2005 Red Hat, Inc.
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
