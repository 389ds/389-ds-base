/**
/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/***********************************************************************
**
**
** NAME
**  DSSynch.h
**
** DESCRIPTION
**  Exported name of Directory Synchronization Service
**
** AUTHOR
**   Rob Weltman <rweltman@netscape.com>
**
***********************************************************************/

#ifndef _DSSYNCH_H_
#define _DSSYNCH_H_

#define PLUGIN_STATE_UNKNOWN	0
#define PLUGIN_STATE_DISABLED	1
#define PLUGIN_STATE_ENABLED	2

#if defined(_UNICODE)
#define DS_SERVICE_NAME _T("Netscape Directory Synchronization Service")
#else
#define DS_SERVICE_NAME "Netscape Directory Synchronization Service"
#endif
#define DS_SERVICE_NAME_UNI L"Netscape Directory Synchronization Service"
#define DS_EVENT_NAME TEXT("Netscape DirSynch")
#define DSS_TERM_EVENT TEXT("NS_DSSYNCH")
#define SYNCH_VERSION "5.0"

#endif // _DSSYNCH_H_
