/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */

#if defined( XP_WIN32 )
#undef MCC_HTTPD
#endif

#include "netsite.h"			/* to get MAGNUS_VERSION_STRING */

#ifdef MAGNUS_VERSION_STRING
#define DSGW_VER_STR	MAGNUS_VERSION_STRING
#else
#include "dirver.h"	/* to get PRODUCTTEXT */
#define DSGW_VER_STR	PRODUCTTEXT
#endif

char *Versionstr = "Netscape-Directory-Gateway/"DSGW_VER_STR;
