/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 /*
 Microsoft Developer Support
 Copyright (c) 1992 Microsoft Corporation

 This file contains the message definitions for the Win32
 messages.exe sample program.
-------------------------------------------------------------------------
 HEADER SECTION

 The header section defines names and language identifiers for use
 by the message definitions later in this file. The MessageIdTypedef,
 SeverityNames, FacilityNames, and LanguageNames keywords are
 optional and not required.



 The MessageIdTypedef keyword gives a typedef name that is used in a
 type cast for each message code in the generated include file. Each
 message code appears in the include file with the format: #define
 name ((type) 0xnnnnnnnn) The default value for type is empty, and no
 type cast is generated. It is the programmer's responsibility to
 specify a typedef statement in the application source code to define
 the type. The type used in the typedef must be large enough to
 accomodate the entire 32-bit message code.



 The SeverityNames keyword defines the set of names that are allowed
 as the value of the Severity keyword in the message definition. The
 set is delimited by left and right parentheses. Associated with each
 severity name is a number that, when shifted left by 30, gives the
 bit pattern to logical-OR with the Facility value and MessageId
 value to form the full 32-bit message code. The default value of
 this keyword is:

 SeverityNames=(
   Success=0x0
   Informational=0x1
   Warning=0x2
   Error=0x3
   )

 Severity values occupy the high two bits of a 32-bit message code.
 Any severity value that does not fit in two bits is an error. The
 severity codes can be given symbolic names by following each value
 with :name



 The FacilityNames keyword defines the set of names that are allowed
 as the value of the Facility keyword in the message definition. The
 set is delimited by left and right parentheses. Associated with each
 facility name is a number that, when shift it left by 16 bits, gives
 the bit pattern to logical-OR with the Severity value and MessageId
 value to form the full 32-bit message code. The default value of
 this keyword is:

 FacilityNames=(
   System=0x0FF
   Application=0xFFF
   )

 Facility codes occupy the low order 12 bits of the high order
 16-bits of a 32-bit message code. Any facility code that does not
 fit in 12 bits is an error. This allows for 4,096 facility codes.
 The first 256 codes are reserved for use by the system software. The
 facility codes can be given symbolic names by following each value
 with :name


 The LanguageNames keyword defines the set of names that are allowed
 as the value of the Language keyword in the message definition. The
 set is delimited by left and right parentheses. Associated with each
 language name is a number and a file name that are used to name the
 generated resource file that contains the messages for that
 language. The number corresponds to the language identifier to use
 in the resource table. The number is separated from the file name
 with a colon. The initial value of LanguageNames is:

 LanguageNames=(English=1:MSG00001)

 Any new names in the source file which don't override the built-in
 names are added to the list of valid languages. This allows an
 application to support private languages with descriptive names.


-------------------------------------------------------------------------
 MESSAGE DEFINITION SECTION

 Following the header section is the body of the Message Compiler
 source file. The body consists of zero or more message definitions.
 Each message definition begins with one or more of the following
 statements:

 MessageId = [number|+number]
 Severity = severity_name
 Facility = facility_name
 SymbolicName = name

 The MessageId statement marks the beginning of the message
 definition. A MessageID statement is required for each message,
 although the value is optional. If no value is specified, the value
 used is the previous value for the facility plus one. If the value
 is specified as +number then the value used is the previous value
 for the facility, plus the number after the plus sign. Otherwise, if
 a numeric value is given, that value is used. Any MessageId value
 that does not fit in 16 bits is an error.

 The Severity and Facility statements are optional. These statements
 specify additional bits to OR into the final 32-bit message code. If
 not specified they default to the value last specified for a message
 definition. The initial values prior to processing the first message
 definition are:

 Severity=Success
 Facility=Application

 The value associated with Severity and Facility must match one of
 the names given in the FacilityNames and SeverityNames statements in
 the header section. The SymbolicName statement allows you to
 associate a C/C++ symbolic constant with the final 32-bit message
 code.
 */
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
#define FACILITY_SYSTEM                  0x0
#define FACILITY_STARTUP                 0x5
#define FACILITY_RUNTIME                 0x1
#define FACILITY_REGISTRY                0x7
#define FACILITY_NETWORK                 0x4
#define FACILITY_SERVICE                 0x3
#define FACILITY_FILESYSTEM              0x6
#define FACILITY_CGI                     0x2


//
// Define the severity codes
//
#define STATUS_SEVERITY_WARNING          0x2
#define STATUS_SEVERITY_SUCCESS          0x0
#define STATUS_SEVERITY_INFORMATIONAL    0x1
#define STATUS_SEVERITY_ERROR            0x3


//
// MessageId: MSG_BAD_CONF_INIT
//
// MessageText:
//
//  Netsite:%1 %2
//
#define MSG_BAD_CONF_INIT                ((DWORD)0xC0050001L)

//
// MessageId: MSG_BAD_EREPORT_INIT
//
// MessageText:
//
//  Netsite:%1 %2
//
#define MSG_BAD_EREPORT_INIT             ((DWORD)0xC0050002L)

//
// MessageId: MSG_BAD_STARTUP
//
// MessageText:
//
//  Netsite:%1 %2
//
#define MSG_BAD_STARTUP                  ((DWORD)0xC0050003L)

//
// MessageId: MSG_BAD_WINSOCK_INIT
//
// MessageText:
//
//  Netsite Initialization:%1 %2
//
#define MSG_BAD_WINSOCK_INIT             ((DWORD)0xC0050004L)

//
// MessageId: MSG_BAD_CGISEM_CREATE
//
// MessageText:
//
//  Netsite Initialization:%1 %2
//
#define MSG_BAD_CGISEM_CREATE            ((DWORD)0xC0050005L)

//
// MessageId: MSG_BAD_PROCESSSEM_CREATE
//
// MessageText:
//
//  Netsite:Initialization:%1 %2
//
#define MSG_BAD_PROCESSSEM_CREATE        ((DWORD)0xC0050006L)

//
// MessageId: MSG_STARTUP_SUCCESSFUL
//
// MessageText:
//
//  Netsite:%1 %2
//
#define MSG_STARTUP_SUCCESSFUL           ((DWORD)0x00050007L)

//
// MessageId: MSG_BAD_REGISTRY_PARAMETER
//
// MessageText:
//
//  Netsite:%1 %2
//
#define MSG_BAD_REGISTRY_PARAMETER       ((DWORD)0x80050008L)

//
// MessageId: MSG_BAD_GENERAL_FUNCTION
//
// MessageText:
//
//  Netsite:Execution of Initialization Function failed %1 %2
//
#define MSG_BAD_GENERAL_FUNCTION         ((DWORD)0xC0050009L)

//
// MessageId: MSG_BAD_SETCIPHERS
//
// MessageText:
//
//  Netsite: %1 %2
//
#define MSG_BAD_SETCIPHERS               ((DWORD)0xC0050010L)

//
// MessageId: MSG_BAD_REGISTRY_KEY_OPEN
//
// MessageText:
//
//  Netsite Initialization:Open of %1 %2
//
#define MSG_BAD_REGISTRY_KEY_OPEN        ((DWORD)0xC0050011L)

//
// MessageId: MSG_BAD_REGISTRY_KEY_ENUM
//
// MessageText:
//
//  Netsite Initialization:Enumeration of %1 %2
//
#define MSG_BAD_REGISTRY_KEY_ENUM        ((DWORD)0xC0050012L)

//
// MessageId: MSG_BAD_REGISTRY_VALUE_ENUM
//
// MessageText:
//
//  Netsite Initialization:Enumeration of Values of %1 %2
//
#define MSG_BAD_REGISTRY_VALUE_ENUM      ((DWORD)0xC0050013L)

//
// MessageId: MSG_BAD_OBJECT_VALUE
//
// MessageText:
//
//  Netsite startup:Use Values "name" or "ppath" for object key.Incorrect Parameter %1 %2
//
#define MSG_BAD_OBJECT_VALUE             ((DWORD)0xC0050014L)

//
// MessageId: MSG_BAD_PBLOCK
//
// MessageText:
//
//  Netsite startup:Could not enter Parameter %1 %2
//
#define MSG_BAD_PBLOCK                   ((DWORD)0xC0050015L)

//
// MessageId: MSG_BAD_CLIENT_VALUE
//
// MessageText:
//
//  Netsite startup:Use Values "dns" or "ip" for client key.Incorrect Parameter	%1 %2
//
#define MSG_BAD_CLIENT_VALUE             ((DWORD)0xC0050016L)

//
// MessageId: MSG_BAD_DIRECTIVE
//
// MessageText:
//
//  Netsite startup:Incorrect Directive Value %1 %2
//
#define MSG_BAD_DIRECTIVE                ((DWORD)0xC0050017L)

//
// MessageId: MSG_BAD_PARAMETER
//
// MessageText:
//
//  Netsite startup:Incorrect Parameter	%1 %2
//
#define MSG_BAD_PARAMETER                ((DWORD)0xC0050018L)

//
// MessageId: MSG_WD_RESTART
//
// MessageText:
//
//  Web Server: %1
//  The server terminated abnormally with error code %2.
//  An attempt will be made to restart it.
//
#define MSG_WD_RESTART                   ((DWORD)0xC0050019L)

//
// MessageId: MSG_WD_STARTFAILED
//
// MessageText:
//
//  Web Server: %1
//  The server could not be started.
//  Command line used: %2
//
#define MSG_WD_STARTFAILED               ((DWORD)0xC005001AL)

//
// MessageId: MSG_WD_BADPASSWORD
//
// MessageText:
//
//  Web Server: %1
//  Incorrect SSL password entered.
//
#define MSG_WD_BADPASSWORD               ((DWORD)0xC005001BL)

//
// MessageId: MSG_WD_BADCMDLINE
//
// MessageText:
//
//  Web Server: %1
//  Invalid command line specified: %2
//
#define MSG_WD_BADCMDLINE                ((DWORD)0xC005001CL)

//
// MessageId: MSG_WD_STRING
//
// MessageText:
//
//  Web Server: %1
//  %2
//
#define MSG_WD_STRING                    ((DWORD)0xC005001DL)

//
// MessageId: MSG_WD_REGISTRY
//
// MessageText:
//
//  Web Server: %1
//  Could not open registry key: %2
//
#define MSG_WD_REGISTRY                  ((DWORD)0xC005001EL)

//
// MessageId: MSG_CRON_STARTFAILED
//
// MessageText:
//
//  Web Server: %1
//  The scheduled job (%2) could not be started.
//
#define MSG_CRON_STARTFAILED             ((DWORD)0xC005001FL)

