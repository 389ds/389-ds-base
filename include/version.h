/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
   This file is included from both C source and the NT installation compiler.
   Because of that, no ifdefs are allowed, and strings must be simple strings
   (not concatenated).

   Because macros called PERSONAL_VERSION and ENTERPRISE_VERSION already
   exist, the PRODUCT_VERSION define has _DEF appended.
 */

#define DIRECTORY_VERSION_DEF "7.1"
#define DIRECTORY_COMPATIBLE "3.0"
#define DIRECTORY_VERSION_STRING "Fedora-DirServer/7.1"

#define DS_VERSION_DEF DIRECTORY_VERSION_DEF
#define DS_VERSION_STRING DIRECTORY_VERSION_STRING

#define DSS_VERSION_DEF DIRECTORY_VERSION_DEF
#define DSS_VERSION_STRING "Fedora-DirSynchService/7.1"

#define ADMSERV_VERSION_DEF "7.0"
#define ADMSERV_VERSION_STRING "Fedora-Administrator/7.0"
/* supposedly the trunk is currently the home of 3.x development */

#define ENTERPRISE_VERSION_DEF "3.01"
#define ENTERPRISE_VERSION_STRING "Netscape-Enterprise/3.01"

#define VI_COMPANYNAME "Fedora Project\0"
#define VI_COPYRIGHT   "Copyright (C) 2001 Sun Microsystems, Inc. Used by permission. Copyright (C) 2005 Red Hat, Inc. All rights reserved.\0"
