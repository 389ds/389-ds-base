/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"
#include "fe.h"

#if defined( SLAPD_MONITOR_DN )


int
monitor_info(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
	char			buf[BUFSIZ];
	struct berval		val;
	struct berval	*vals[2];
	time_t			curtime = current_time();
	struct tm		utm;
	Slapi_Backend		*be;
	char			*cookie;
	PRUint32		len;

	vals[0] = &val;
	vals[1] = NULL;

	/* "version" value */
	val.bv_val = slapd_get_version_value();
	val.bv_len = strlen( val.bv_val );
	attrlist_replace( &e->e_attrs, "version", vals );
	slapi_ch_free( (void **) &val.bv_val );

	sprintf( buf, "%d", active_threads );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "threads", vals );

	connection_table_as_entry(the_connection_table, e);

	PR_Lock( ops_mutex );
	sprintf( buf, "%ld", (long) ops_initiated );
	PR_Unlock( ops_mutex );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "opsinitiated", vals );

	PR_Lock( ops_mutex );
	sprintf( buf, "%ld", (long) ops_completed );
	PR_Unlock( ops_mutex );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "opscompleted", vals );

	PR_Lock( g_get_num_sent_mutex() );
	len = PR_snprintf ( buf, BUFSIZ, "%llu", g_get_num_entries_sent() );
	PR_Unlock( g_get_num_sent_mutex() );
	val.bv_val = buf;
	val.bv_len = ( unsigned long ) len;
	attrlist_replace( &e->e_attrs, "entriessent", vals );

	PR_Lock( g_get_num_sent_mutex() );
	len = PR_snprintf ( buf, BUFSIZ, "%llu", g_get_num_bytes_sent() );
	PR_Unlock( g_get_num_sent_mutex() );
	val.bv_val = buf;
	val.bv_len = ( unsigned long ) len;
	attrlist_replace( &e->e_attrs, "bytessent", vals );

#ifdef _WIN32
	{
	struct tm *pt;
	pt = gmtime( &curtime );
	memcpy(&utm, pt, sizeof(struct tm) );
	}
#else
	gmtime_r( &curtime, &utm );
#endif
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "currenttime", vals );

#ifdef _WIN32
	{
	struct tm *pt;
	pt = gmtime( &starttime );
	memcpy(&utm, pt, sizeof(struct tm) );
	}
#else
	gmtime_r( &starttime, &utm );
#endif
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "starttime", vals );

	sprintf( buf, "%d", be_nbackends_public() );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "nbackends", vals );

#ifdef THREAD_SUNOS5_LWP
	sprintf( buf, "%d", thr_getconcurrency() );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "concurrency", vals );
#endif

	/*Loop through the backends, and stuff the monitordns
	 into the entry we're sending back*/
	attrlist_delete( &e->e_attrs, "backendmonitordn");
	cookie = NULL;
	be = slapi_get_first_backend(&cookie);
	while (be)
    {
	    if ( !be->be_private )
		{
		    Slapi_DN dn;
			slapi_sdn_init(&dn);
            be_getmonitordn(be,&dn);
    		val.bv_val = (char*)slapi_sdn_get_dn(&dn);
    		val.bv_len = strlen( val.bv_val );
    		attrlist_merge( &e->e_attrs, "backendmonitordn", vals );
			slapi_sdn_done(&dn);
	    }
		be = slapi_get_next_backend(cookie);
    }

        slapi_ch_free((void **)&cookie);

    *returncode= LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

#endif /* SLAPD_MONITOR_DN */


/*
 * Return a malloc'd version value.
 * Used for the monitor entry's 'version' attribute.
 * Used for the root DSE's 'vendorVersion' attribute.
 */
char *
slapd_get_version_value( void )
{
        char    *versionstring, *buildnum, *vs;

        versionstring = config_get_versionstring();
        buildnum = config_get_buildnum();

        vs = slapi_ch_smprintf("%s B%s", versionstring, buildnum );

        slapi_ch_free( (void **) &buildnum);
        slapi_ch_free( (void **) &versionstring);

        return vs;
}
