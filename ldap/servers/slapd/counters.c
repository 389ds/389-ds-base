/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

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
