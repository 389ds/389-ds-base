/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
#include <sys/socket.h>
#include "slap.h"
#include "fe.h"

#if defined( SLAPD_MONITOR_DN )


int
monitor_info(Slapi_PBlock *pb __attribute__((unused)),
             Slapi_Entry* e,
             Slapi_Entry* entryAfter __attribute__((unused)),
             int *returncode,
             char *returntext __attribute__((unused)),
             void *arg __attribute__((unused)))
{
	char			buf[BUFSIZ];
	struct berval		val;
	struct berval	*vals[2];
	time_t			curtime = current_time();
	struct tm		utm;
	Slapi_Backend		*be;
	char			*cookie;

	vals[0] = &val;
	vals[1] = NULL;

	/* "version" value */
	val.bv_val = slapd_get_version_value();
	val.bv_len = strlen( val.bv_val );
	attrlist_replace( &e->e_attrs, "version", vals );
	slapi_ch_free( (void **) &val.bv_val );

	val.bv_len = snprintf( buf, sizeof(buf), "%lu", g_get_active_threadcnt() );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "threads", vals );

	connection_table_as_entry(the_connection_table, e);

	val.bv_len = snprintf( buf, sizeof(buf), "%" PRIu64, slapi_counter_get_value(ops_initiated) );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "opsinitiated", vals );

	val.bv_len = snprintf( buf, sizeof(buf), "%" PRIu64, slapi_counter_get_value(ops_completed) );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "opscompleted", vals );

	val.bv_len = snprintf ( buf, sizeof(buf), "%" PRIu64, g_get_num_entries_sent() );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "entriessent", vals );

	val.bv_len = snprintf ( buf, sizeof(buf), "%" PRIu64, g_get_num_bytes_sent() );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "bytessent", vals );

	gmtime_r( &curtime, &utm );
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "currenttime", vals );

	gmtime_r( &starttime, &utm );
	strftime( buf, sizeof(buf), "%Y%m%d%H%M%SZ", &utm );
	val.bv_val = buf;
	val.bv_len = strlen( buf );
	attrlist_replace( &e->e_attrs, "starttime", vals );

	val.bv_len = snprintf( buf, sizeof(buf), "%d", be_nbackends_public() );
	val.bv_val = buf;
	attrlist_replace( &e->e_attrs, "nbackends", vals );

#ifdef THREAD_SUNOS5_LWP
	val.bv_len = snprintf( buf, sizeof(buf), "%d", thr_getconcurrency() );
	val.bv_val = buf;
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
