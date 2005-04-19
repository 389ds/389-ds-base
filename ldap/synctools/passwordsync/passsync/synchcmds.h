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
/***********************************************************************
**
**
** NAME
**  synchcmds.h
**
** DESCRIPTION
**  Commands accepted by DS Synchronization Service
**
** AUTHOR
**   Rob Weltman <rweltman@netscape.com>
**
***********************************************************************/

#ifndef _SYNCHCMDS_H_
#define _SYNCHCMDS_H_

#define SYNCH_CMD_NT_POLL_INTERVAL	't'
#define SYNCH_CMD_DS_POLL_INTERVAL	'm'
#define SYNCH_CMD_ADMIN_DN			'd'
#define SYNCH_CMD_ADMIN_PASSWORD	'w'
#define SYNCH_CMD_DIRECTORY_USERS_BASE	'b'
#define SYNCH_CMD_DIRECTORY_GROUPS_BASE	'f'
#define SYNCH_CMD_DS_HOST			'h'
#define SYNCH_CMD_DS_PORT			'p'
#define SYNCH_CMD_NT_PORT			'c'
#define SYNCH_CMD_NT_CALENDAR		'k'
#define SYNCH_CMD_DS_CALENDAR		'y'
#define SYNCH_CMD_SYNCH_FROM_NT		'n'
#define SYNCH_CMD_SYNCH_FROM_DS		's'
#define SYNCH_CMD_SYNCH_CHANGES		'r'
#define SYNCH_CMD_RELOAD_SETTINGS	'l'

/* LDAP error codes */
#define SYNCH_ERR_PARTIAL_RESULTS		0x09
#define SYNCH_ERR_INVALID_DN_SYNTAX		0x22
#define SYNCH_ERR_INAPPROPRIATE_AUTH	0x30
#define SYNCH_ERR_INVALID_CREDENTIALS	0x31
#define SYNCH_ERR_INSUFFICIENT_ACCESS	0x32
#define SYNCH_ERR_CONNECT_ERROR			0x5b

#define SYNCH_ERR_INVALID_USERS_BASE			100
#define SYNCH_ERR_INVALID_GROUPS_BASE			200
#define SYNCH_ERR_INVALID_UID_UNIQUE_BASE		300
#define SYNCH_ERR_BAD_CONFIG			400

#endif _SYNCHCMDS_H_

