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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <prcountr.h>
#include "slap.h"

#if defined(DEBUG)
struct counter
{
	const char *qname;
	const char *rname;
	const char *description;
	PRUint32 before_counter;
	PRUint32 after_counter;
};

static int num_counters= 0;
static struct counter *counters= NULL;

static int
count_counters()
{
	int i= 0;
	PR_DEFINE_COUNTER(qh);
	PR_INIT_COUNTER_HANDLE(qh,NULL);
	PR_FIND_NEXT_COUNTER_QNAME(qh,qh);
	while(qh!=NULL)
	{
		PR_DEFINE_COUNTER(rh);
		PR_INIT_COUNTER_HANDLE(rh,NULL);
		PR_FIND_NEXT_COUNTER_RNAME(rh,rh,qh);
		while(rh!=NULL)
		{
			i++;
			PR_FIND_NEXT_COUNTER_RNAME(rh,rh,qh);
		}
		PR_FIND_NEXT_COUNTER_QNAME(qh,qh);
	}
	return i;
}

static int
do_fetch_counters()
{
	int i= 0;
	PR_DEFINE_COUNTER(qh);
	PR_INIT_COUNTER_HANDLE(qh,NULL);
	PR_FIND_NEXT_COUNTER_QNAME(qh,qh);
	while(qh!=NULL)
	{
		PR_DEFINE_COUNTER(rh);
		PR_INIT_COUNTER_HANDLE(rh,NULL);
		PR_FIND_NEXT_COUNTER_RNAME(rh,rh,qh);
		while(rh!=NULL)
		{
			if(i<num_counters)
			{
				counters[i].before_counter= counters[i].after_counter;
				PR_GET_COUNTER_NAME_FROM_HANDLE(rh,&counters[i].qname,&counters[i].rname,&counters[i].description);
				PR_GET_COUNTER(counters[i].after_counter,rh);
			}
			i++;
			PR_FIND_NEXT_COUNTER_RNAME(rh,rh,qh);
		}
		PR_FIND_NEXT_COUNTER_QNAME(qh,qh);
	}
	return i;
}

static void
fetch_counters()
{
	int i;
	if(counters==NULL)
	{
		num_counters= count_counters();
		counters= (struct counter*)calloc(num_counters,sizeof(struct counter));
	}
	i= do_fetch_counters();
	if(i>num_counters)
	{
		free(counters);
		counters= NULL;
		num_counters= i;
		counters= (struct counter*)calloc(num_counters,sizeof(struct counter));
		do_fetch_counters();
	}
}

static size_t
counter_size(struct counter *counter)
{
	size_t r= 0;
	r+= (counter->qname?strlen(counter->qname):0);
	r+= (counter->rname?strlen(counter->rname):0);
	r+= (counter->description?strlen(counter->description):0);
	return r;
}

static void
counter_dump(char *name, char *value, int i)
{
	PRUint32 diff_counter;
	sprintf(name,"%s_%s_%s",counters[i].qname,counters[i].rname,counters[i].description);
	diff_counter= counters[i].after_counter-counters[i].before_counter;
	sprintf(value,"%d -> %d (%s%d)",
		counters[i].before_counter,
		counters[i].after_counter,
		(diff_counter>0?"+":""),
		diff_counter);
}
#endif

void
counters_as_entry(Slapi_Entry* e)
{
#if defined(DEBUG)
	int i;
	fetch_counters();
	for(i=0;i<num_counters;i++)
	{
		char value[40];
		char *type= (char*)malloc(counter_size(&counters[i])+4);
		counter_dump(type,value,i);
		slapi_entry_attr_set_charptr( e, type, value);
		free(type);
	}
#endif
}


void
counters_to_errors_log(const char *text)
{
#if defined(DEBUG)
	int i;
	fetch_counters();
	LDAPDebug( LDAP_DEBUG_ANY, "Counter Dump - %s\n",text, 0, 0);
	for(i=0;i<num_counters;i++)
	{
		char value[40];
		char *type= (char*)malloc(counter_size(&counters[i])+4);
		counter_dump(type,value,i);
		LDAPDebug( LDAP_DEBUG_ANY, "%s %s\n",type, value, 0);
		free(type);
	}
#endif
}
