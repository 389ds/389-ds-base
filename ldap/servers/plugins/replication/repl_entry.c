/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "slapi-plugin.h"
#include "repl.h"

static int dumping_to_ldif= 0;
static int doing_replica_init= 0;
static char **include_suffix= NULL;

/*
 * This is passed the slapd command line arguments.
 */
void
repl_entry_init(int argc, char** argv)
{
    int i;
	for(i=1;i<argc;i++)
	{
	    if(strcmp(argv[i],"db2ldif")==0)
		{
            dumping_to_ldif= 1;
		}
	    if(strcmp(argv[i],"-r")==0)
		{
            doing_replica_init= 1;
		}
	    if(strcmp(argv[i],"-s")==0)
		{
		    char *s= slapi_dn_normalize ( slapi_ch_strdup(argv[i+1]) );
		    charray_add(&include_suffix,s);
		    i++;
		}
	}
}
