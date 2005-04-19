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
