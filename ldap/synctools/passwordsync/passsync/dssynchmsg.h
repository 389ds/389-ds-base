/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
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
// MessageId: EVMSG_INSTALLED
//
// MessageText:
//
//  The %1 service was installed.
//
#define EVMSG_INSTALLED                  0x00000064L

//
// MessageId: EVMSG_REMOVED
//
// MessageText:
//
//  The %1 service was removed.
//
#define EVMSG_REMOVED                    0x00000065L

//
// MessageId: EVMSG_NOTREMOVED
//
// MessageText:
//
//  The %1 service could not be removed.
//
#define EVMSG_NOTREMOVED                 0x00000066L

//
// MessageId: EVMSG_CTRLHANDLERNOTINSTALLED
//
// MessageText:
//
//  The control handler could not be installed.
//
#define EVMSG_CTRLHANDLERNOTINSTALLED    0x00000067L

//
// MessageId: EVMSG_FAILEDINIT
//
// MessageText:
//
//  The initialization process failed.
//
#define EVMSG_FAILEDINIT                 0x00000068L

//
// MessageId: EVMSG_STARTED
//
// MessageText:
//
//  The service was started.
//
#define EVMSG_STARTED                    0x00000069L

//
// MessageId: EVMSG_RESTART
//
// MessageText:
//
//  The service was restarted.
//
#define EVMSG_RESTART                    0x0000006AL

//
// MessageId: EVMSG_BADREQUEST
//
// MessageText:
//
//  The service received an unsupported request.
//
#define EVMSG_BADREQUEST                 0x0000006BL

//
// MessageId: EVMSG_SECURITYREGISTER
//
// MessageText:
//
//  The service has registered for security events.
//
#define EVMSG_SECURITYREGISTER           0x0000006CL

//
// MessageId: EVMSG_FAILEDNOTIFY
//
// MessageText:
//
//  The service failed on waiting for security event.
//
#define EVMSG_FAILEDNOTIFY               0x0000006DL

//
// MessageId: EVMSG_SECURITYNOTIFY
//
// MessageText:
//
//  There was a security event.
//
#define EVMSG_SECURITYNOTIFY             0x0000006EL

//
// MessageId: EVMSG_FAILEDSECURITYNOTIFY
//
// MessageText:
//
//  Failure waiting for security event involving Users.
//
#define EVMSG_FAILEDSECURITYNOTIFY       0x0000006FL

//
// MessageId: EVMSG_SECURITYAUDITINGENABLED
//
// MessageText:
//
//  Auditing of User and Group Management events has been enabled.
//
#define EVMSG_SECURITYAUDITINGENABLED    0x00000070L

//
// MessageId: EVMSG_SECURITYAUDITINGDISABLED
//
// MessageText:
//
//  Auditing of User and Group Management events has been turned off.
//  Notification-based monitoring of user changes has terminated.
//
#define EVMSG_SECURITYAUDITINGDISABLED   0x00000071L

//
// MessageId: EVMSG_FAILEDBINDDS
//
// MessageText:
//
//  Failed to bind to Directory Service on %1 at %2.
//
#define EVMSG_FAILEDBINDDS               0x00000072L

//
// MessageId: EVMSG_BINDDS
//
// MessageText:
//
//  Connected to Directory Service on %1 at %2.
//
#define EVMSG_BINDDS                     0x00000073L

//
// MessageId: EVMSG_FAILEDGETNTUSERS
//
// MessageText:
//
//  Failed to get user information from NT SAM.
//
#define EVMSG_FAILEDGETNTUSERS           0x00000074L

//
// MessageId: EVMSG_FAILEDGETDSUSERS
//
// MessageText:
//
//  Failed to get user information from Directory Server.
//
#define EVMSG_FAILEDGETDSUSERS           0x00000075L

//
// MessageId: EVMSG_FAILEDSETDSTIMESTAMPS
//
// MessageText:
//
//  Failed to set initial time stamps of NT users on Directory Server.
//
#define EVMSG_FAILEDSETDSTIMESTAMPS      0x00000076L

//
// MessageId: EVMSG_FAILEDGETNTUSERINFO
//
// MessageText:
//
//  Failed to get user information for <%1> from NT.
//
#define EVMSG_FAILEDGETNTUSERINFO        0x00000077L

//
// MessageId: EVMSG_FAILEDADDDSUSER
//
// MessageText:
//
//  Failed to add user <%1> to Directory Server.
//
#define EVMSG_FAILEDADDDSUSER            0x00000078L

//
// MessageId: EVMSG_FAILEDMODIFYDSUSER
//
// MessageText:
//
//  Failed to modify user <%1> on Directory Server.
//
#define EVMSG_FAILEDMODIFYDSUSER         0x00000079L

//
// MessageId: EVMSG_FAILEDDELETEDSUSER
//
// MessageText:
//
//  Failed to delete user <%1> on Directory Server.
//
#define EVMSG_FAILEDDELETEDSUSER         0x0000007AL

//
// MessageId: EVMSG_FAILEDADDNTUSER
//
// MessageText:
//
//  Failed to add user <%1> to NT.
//
#define EVMSG_FAILEDADDNTUSER            0x0000007BL

//
// MessageId: EVMSG_FAILEDMODIFYNTUSER
//
// MessageText:
//
//  Failed to modify user <%1> in NT.
//
#define EVMSG_FAILEDMODIFYNTUSER         0x0000007CL

//
// MessageId: EVMSG_FAILEDDELETENTUSER
//
// MessageText:
//
//  Failed to delete user <%1> in NT.
//
#define EVMSG_FAILEDDELETENTUSER         0x0000007DL

//
// MessageId: EVMSG_FAILEDREADEVENTLOG
//
// MessageText:
//
//  Failed reading Security Event Log.
//
#define EVMSG_FAILEDREADEVENTLOG         0x0000007EL

//
// MessageId: EVMSG_FAILEDCOMMANDOPEN
//
// MessageText:
//
//  Failed opening command socket at port %1.
//
#define EVMSG_FAILEDCOMMANDOPEN          0x0000007FL

//
// MessageId: EVMSG_FAILEDCOMMANDREAD
//
// MessageText:
//
//  Failed reading from command socket.
//
#define EVMSG_FAILEDCOMMANDREAD          0x00000080L

//
// MessageId: EVMSG_FAILEDCOMMANDWRITE
//
// MessageText:
//
//  Failed writing to command socket.
//
#define EVMSG_FAILEDCOMMANDWRITE         0x00000081L

//
// MessageId: EVMSG_FAILEDGETKEY
//
// MessageText:
//
//  Failed to get registry key <%1>.
//
#define EVMSG_FAILEDGETKEY               0x00000082L

//
// MessageId: EVMSG_FAILEDREADREGVALUE
//
// MessageText:
//
//  Failed to read registry value <%1>.
//
#define EVMSG_FAILEDREADREGVALUE         0x00000083L

//
// MessageId: EVMSG_FAILEDWRITEREGVALUE
//
// MessageText:
//
//  Failed to write registry value <%1>.
//
#define EVMSG_FAILEDWRITEREGVALUE        0x00000084L

//
// MessageId: EVMSG_FAILEDSAVESTATUS
//
// MessageText:
//
//  Failed to save status to registry.
//
#define EVMSG_FAILEDSAVESTATUS           0x00000085L

//
// MessageId: EVMSG_UserNotFound
//
// MessageText:
//
//  The user name could not be found.
//
#define EVMSG_UserNotFound               0x00000086L

//
// MessageId: EVMSG_UserExists
//
// MessageText:
//
//  The user account already exists.
//
#define EVMSG_UserExists                 0x00000087L

//
// MessageId: EVMSG_PasswordTooShort
//
// MessageText:
//
//  The password is shorter than required.
//
#define EVMSG_PasswordTooShort           0x00000088L

//
// MessageId: EVMSG_NameTooLong
//
// MessageText:
//
//  The user name %1 is too long.
//
#define EVMSG_NameTooLong                0x00000089L

//
// MessageId: EVMSG_PasswordHistConflict
//
// MessageText:
//
//  This password cannot be used now.
//
#define EVMSG_PasswordHistConflict       0x0000008AL

//
// MessageId: EVMSG_InvalidDatabase
//
// MessageText:
//
//  The security database is corrupted.
//
#define EVMSG_InvalidDatabase            0x0000008BL

//
// MessageId: EVMSG_NetworkError
//
// MessageText:
//
//  A general network error occurred.
//
#define EVMSG_NetworkError               0x0000008CL

//
// MessageId: EVMSG_BadUsername
//
// MessageText:
//
//  The user name or group name parameter is invalid.
//
#define EVMSG_BadUsername                0x0000008DL

//
// MessageId: EVMSG_UserUnkownError
//
// MessageText:
//
//  Unexpected error - number %1.
//
#define EVMSG_UserUnkownError            0x0000008EL

//
// MessageId: EVMSG_DEBUG
//
// MessageText:
//
//  Debug: %1
//
#define EVMSG_DEBUG                      0x0000008FL

//
// MessageId: EVMSG_STOPPED
//
// MessageText:
//
//  The service was stopped.
//
#define EVMSG_STOPPED                    0x00000090L

//
// MessageId: EVMSG_UIDNOTDUPLICATED
//
// MessageText:
//
//  The UID "%1" already exists in the subtree and has not been duplicated.
//
#define EVMSG_UIDNOTDUPLICATED           0x00000091L

//
// MessageId: EVMSG_UIDDUPLICATED
//
// MessageText:
//
//  The UID "%1" already exists in the subtree but HAS been duplicated because the user RDN component requires it.  Further action may need to be taken if there are systems using the Directory Server which require unique UIDs.
//
#define EVMSG_UIDDUPLICATED              0x00000092L

//
// MessageId: EVMSG_FAILEDCONTROLOPEN
//
// MessageText:
//
//  Failed opening control message socket at port %1.
//
#define EVMSG_FAILEDCONTROLOPEN          0x00000093L

//
// MessageId: EVMSG_FAILEDPREOPOPEN
//
// MessageText:
//
//  Failed opening preoperation processing socket at port %1.
//
#define EVMSG_FAILEDPREOPOPEN            0x00000094L

//
// MessageId: EVMSG_FAILEDPOSTOPEN
//
// MessageText:
//
//  Failed opening postoperation processing socket at port %1.
//
#define EVMSG_FAILEDPOSTOPEN             0x00000095L

//
// MessageId: EVMSG_FAILEDCONTROLWRITE
//
// MessageText:
//
//  Failed writing to control message socket at port %1.
//
#define EVMSG_FAILEDCONTROLWRITE         0x00000096L

//
// MessageId: EVMSG_FAILEDPREOPWRITE
//
// MessageText:
//
//  Failed writing to preoperation processing socket at port %1.
//
#define EVMSG_FAILEDPREOPWRITE           0x00000097L

//
// MessageId: EVMSG_FAILEDPOSTOPWRITE
//
// MessageText:
//
//  Failed writing to postoperation processing socket at port %1.
//
#define EVMSG_FAILEDPOSTOPWRITE          0x00000098L

//
// MessageId: EVMSG_FAILEDCONTROLREAD
//
// MessageText:
//
//  Failed reading from control message socket at port %1.
//
#define EVMSG_FAILEDCONTROLREAD          0x00000099L

//
// MessageId: EVMSG_FAILEDPREOPREAD
//
// MessageText:
//
//  Failed reading from preoperation processing socket at port %1.
//
#define EVMSG_FAILEDPREOPREAD            0x0000009AL

//
// MessageId: EVMSG_FAILEDPOSTOPREAD
//
// MessageText:
//
//  Failed reading from postoperation processing socket at port %1.
//
#define EVMSG_FAILEDPOSTOPREAD           0x0000009BL

//
// MessageId: EVMSG_FAILEDCONTROLBADBASE
//
// MessageText:
//
//  The subtree "%1" is invalid.
//
#define EVMSG_FAILEDCONTROLBADBASE       0x0000009CL

//
// MessageId: EVMSG_FAILEDGETNTGROUPINFO
//
// MessageText:
//
//  Failed to get group information for <%1> from NT.
//
#define EVMSG_FAILEDGETNTGROUPINFO       0x0000009DL

//
// MessageId: EVMSG_FAILEDADDDSGROUP
//
// MessageText:
//
//  Failed to add group <%1> to Directory Server.
//
#define EVMSG_FAILEDADDDSGROUP           0x0000009EL

//
// MessageId: EVMSG_FAILEDMODIFYDSGROUP
//
// MessageText:
//
//  Failed to modify group <%1> on Directory Server.
//
#define EVMSG_FAILEDMODIFYDSGROUP        0x0000009FL

//
// MessageId: EVMSG_FAILEDDELETEDSGROUP
//
// MessageText:
//
//  Failed to delete group <%1> on Directory Server.
//
#define EVMSG_FAILEDDELETEDSGROUP        0x000000A0L

//
// MessageId: EVMSG_FAILEDCONTROLDUPTREE
//
// MessageText:
//
//  Synch Service control connection rejected by Directory Server, duplicate users base <%1> or groups base <%2>.
//
#define EVMSG_FAILEDCONTROLDUPTREE       0x000000A1L

//
// MessageId: EVMSG_FAILEDCONTROLSETUP
//
// MessageText:
//
//  Synch Service control connection rejected by Directory Server, users base <%1>, groups base <%2>.
//
#define EVMSG_FAILEDCONTROLSETUP         0x000000A2L

