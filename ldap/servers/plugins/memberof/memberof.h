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
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * memberof.h - memberOf shared definitions
 *
 */

#ifndef _MEMBEROF_H_
#define _MEMBEROF_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "portable.h"
#include "slapi-plugin.h"
#include <nspr.h>

/* Private API: to get SLAPI_DSE_RETURNTEXT_SIZE, DSE_FLAG_PREOP, and DSE_FLAG_POSTOP */
#include "slapi-private.h"

/*
 * macros
 */
#define MEMBEROF_PLUGIN_SUBSYSTEM   "memberof-plugin"   /* used for logging */
#define MEMBEROF_INT_PREOP_DESC "memberOf internal postop plugin"
#define MEMBEROF_GROUP_ATTR "memberOfGroupAttr"
#define MEMBEROF_ATTR "memberOfAttr"
#define MEMBEROF_BACKEND_ATTR "memberOfAllBackends"
#define DN_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.12"
#define NAME_OPT_UID_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.34"


/*
 * structs
 */
typedef struct memberofconfig {
	char **groupattrs;
	char *memberof_attr;
	int allBackends;
	Slapi_Filter *group_filter;
	Slapi_Attr **group_slapiattrs;
} MemberOfConfig;


/*
 * functions
 */
int memberof_config(Slapi_Entry *config_e);
void memberof_copy_config(MemberOfConfig *dest, MemberOfConfig *src);
void memberof_free_config(MemberOfConfig *config);
MemberOfConfig *memberof_get_config();
void memberof_lock();
void memberof_unlock();
void memberof_rlock_config();
void memberof_wlock_config();
void memberof_unlock_config();
int memberof_config_get_all_backends();

#endif	/* _MEMBEROF_H_ */
