/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
/***********************************************************************
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

