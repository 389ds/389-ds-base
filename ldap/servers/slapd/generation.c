/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include <stdio.h>
#include <time.h>

#include "rwlock.h"
#include "slap.h"

/*
 * Create a new data version string.
 */
static const char *
new_dataversion()
{
    struct tm t;
    char* dataversion;
	time_t curtime= current_time();
#ifdef _WIN32
    memcpy (&t, gmtime (&curtime), sizeof(t));
#else
    gmtime_r (&curtime, &t);
#endif
    dataversion = slapi_ch_malloc(16);
    sprintf (dataversion, "0%.4li%.2i%.2i%.2i%.2i%.2i", 1900L + t.tm_year, 1 + t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	return dataversion;
}

/* ---------------- Database Data Version ---------------- */

/*
 * Return the generation ID for the database whose mapping tree node is "dn"
 */
char *
get_database_dataversion(const char *dn)
{
	char *dataversion= NULL;
    int r;
    Slapi_PBlock *resultpb= NULL;
    Slapi_Entry** entry = NULL;
    resultpb= slapi_search_internal( dn, LDAP_SCOPE_BASE, "objectclass=*", NULL, NULL, 1);
    slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entry );
    slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_RESULT, &r );
    if(r==LDAP_SUCCESS && entry!=NULL && entry[0]!=NULL)
    {
		dataversion= slapi_entry_attr_get_charptr( entry[0], "nsslapd-dataversion"); /* JCMREPL - Shouldn't be a Netscape specific attribute name */
    }
    slapi_free_search_results_internal(resultpb);
    slapi_pblock_destroy(resultpb);
	return dataversion;
}

void
set_database_dataversion(const char *dn, const char *dataversion)
{
    LDAPMod gen_mod;
	LDAPMod	*mods[2];
    struct berval* gen_vals[2];
    struct berval gen_val;
	Slapi_PBlock *pb;

	memset (&gen_mod, 0, sizeof(gen_mod));

	gen_mod.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
	gen_mod.mod_type = "nsslapd-dataversion"; /* JCMREPL - Shouldn't be a Netscape specific attribute name */
	gen_mod.mod_bvalues = gen_vals;
	gen_vals[0] = &gen_val;
	gen_vals[1] = NULL;
	gen_val.bv_val = (char *)dataversion;
	gen_val.bv_len = strlen (gen_val.bv_val);
	mods[0] = &gen_mod;
	mods[1] = NULL;

	pb = slapi_modify_internal (dn, mods, 0, 0 /* !log_change */);
	if (NULL != pb)
	{
	    Slapi_Entry *e;
	    slapi_pblock_get (pb, SLAPI_ENTRY_PRE_OP, &e);
   		slapi_entry_free(e);
	}
	slapi_pblock_destroy(pb);
}

/* ---------------- Server Data Version ---------------- */

static char *server_dataversion_id= NULL;

const char *
get_server_dataversion()
{
	lenstr *l = NULL;
	Slapi_Backend *be;
	char *cookie;

	/* we already cached the copy - just return it */
	if(server_dataversion_id!=NULL)
	{
		 return server_dataversion_id;
	}

	l= lenstr_new();

	/* Loop over the backends collecting the backend data versions */
	/* Combine them into a single blob */
	be = slapi_get_first_backend(&cookie);
	while ( be )
	{
	/* Don't generate dataversion for remote entries */
        if((!be->be_private) && !(slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
        {
			const char * dataversion;
			Slapi_DN be_configdn;
			slapi_sdn_init(&be_configdn);
			(void)be_getconfigdn(be, &be_configdn);
			dataversion= get_database_dataversion(slapi_sdn_get_ndn(&be_configdn));
			if(dataversion==NULL)
			{
				/* The database either doesn't support the storage of a dataverion, */
				/* or has just been created, or has been reinitialised */
				dataversion= new_dataversion();
				set_database_dataversion(slapi_sdn_get_ndn(&be_configdn), dataversion);
			}
			addlenstr(l, dataversion);
			slapi_ch_free((void**)&dataversion);
			slapi_sdn_done(&be_configdn);
        }

		be = slapi_get_next_backend(cookie);
	}
	slapi_ch_free ((void **)&cookie);
	if(l->ls_buf!=NULL)
	{
    	server_dataversion_id= slapi_ch_strdup(l->ls_buf);
	}
	lenstr_free(&l);
	return server_dataversion_id;
}
