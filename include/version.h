/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
   This file is included from both C source and the NT installation compiler.
   Because of that, no ifdefs are allowed, and strings must be simple strings
   (not concatenated).

   Because macros called PERSONAL_VERSION and ENTERPRISE_VERSION already
   exist, the PRODUCT_VERSION define has _DEF appended.
 */

#define DIRECTORY_VERSION_DEF "7.0"
#define DIRECTORY_COMPATIBLE "3.0"
#define DIRECTORY_VERSION_STRING "Netscape-DirServer/7.0"

#define DS_VERSION_DEF DIRECTORY_VERSION_DEF
#define DS_VERSION_STRING DIRECTORY_VERSION_STRING

#define DSS_VERSION_DEF DIRECTORY_VERSION_DEF
#define DSS_VERSION_STRING "Netscape-DirSynchService/7.0"

#define PROXY_VERSION_DEF "2.0"
#define PROXY_VERSION_STRING "Netscape-Proxy/2.0"

#define ADMSERV_VERSION_DEF "4.0b1"
#define ADMSERV_VERSION_STRING "Netscape-Administrator/4.0b1"
/* supposedly the trunk is currently the home of 3.x development */

#define PERSONAL_VERSION_DEF "3.01b1"
#define PERSONAL_VERSION_STRING "Netscape-FastTrack/3.01b1"

#define CATALOG_VERSION_DEF "1.0b2"
#define CATALOG_VERSION_STRING "Netscape-Catalog/1.0b2"

#define RDS_VERSION_DEF "1.0b2"
#define RDS_VERSION_STRING "Netscape-RDS/1.0b2"

#define ENTERPRISE_VERSION_DEF "3.01"
#define ENTERPRISE_VERSION_STRING "Netscape-Enterprise/3.01"

#define MAIL_VERSION_DEF "3.0a0"
#define MAIL_VERSION_STRING "Netscape-Mail/3.0a0"

#define NEWS_VERSION_STRING "Netscape 1.1"

#define BATMAN_VERSION_DEF "1.0a1"
#define BATMAN_VERSION_STRING "Batman/1.0a1"

#define VI_COMPANYNAME "Netscape Communications Corporation\0"
#define VI_COPYRIGHT   "Copyright 2001 Sun Microsystems, Inc.  Portions copyright 1999, 2001-2003 Netscape Communications Corporation.  All rights reserved.\0"
