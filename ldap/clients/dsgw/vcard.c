/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * vcard.c -- vCard utility functions -- HTTP gateway
 *
 * Copyright (c) 1997 Netscape Communications Corp.
 * All rights reserved.
 */

#include "dsgw.h"
#include "dbtdsgw.h"
#include "ldif.h"


static int entry2vcard( LDAP *ld, char *dn, char *mimetype,
	dsgwvcprop *vcprops, char **lderrtxtp );
static void write_vcard_property( char *prop, char *val, char *val2,
	int is_mls );
static void emit_vcard_headers( char *mimetype );
static char **vcard_ldapattrs( dsgwvcprop *vcprops );
static void dsgw_puts( char *s );


#define DSGW_VCARD_MIMEHDR_TEXTDIR	"text/directory;profile=vcard"
#define DSGW_VCARD_MIMEHDR_XVCARD	"text/x-vcard"
#define DSGW_VCARD_VERSION		"2.1"
#define DSGW_VCARD_PROP_VERSION		"VERSION"
#define DSGW_VCARD_PROP_BEGIN		"BEGIN"
#define DSGW_VCARD_PROP_END		"END"
#define DSGW_VCARD_BEGINEND_VALUE	"vCard"


void
dsgw_vcard_from_entry( LDAP *ld, char *dn, char *mimetype )
{
    int		lderr;
    char	*lderrtxt;

    if (( lderr = entry2vcard( ld, dn, mimetype, gc->gc_vcardproperties,
	    &lderrtxt )) != LDAP_SUCCESS ) {
	dsgw_error( DSGW_ERR_LDAPGENERAL, NULL, DSGW_ERROPT_EXIT, lderr,
		lderrtxt );
    }
}


/*
 * Retrieve the LDAP entry "dn" and write a vCard representation of it
 * to stdout.
 */
static int
entry2vcard( LDAP *ld, char *dn, char *mimetype, dsgwvcprop *vcprops,
	char **lderrtxtp )
{
    int		i, rc, is_mls;
    char	**ldattrs, **vals, **vals2;
    dsgwvcprop	*vcp;
    LDAPMessage	*msgp, *entry;


    ldattrs = vcard_ldapattrs( vcprops );

    /* Read the entry. */
    if (( rc = ldap_search_s( ld, dn, LDAP_SCOPE_BASE, "objectClass=*",
            ldattrs, 0, &msgp )) != LDAP_SUCCESS ) {
	(void)ldap_get_lderrno( ld, NULL, lderrtxtp );
	return( rc );
    }
    if (( entry = ldap_first_entry( ld, msgp )) == NULL ) {
        ldap_msgfree( msgp );
	return( ldap_get_lderrno( ld, NULL, lderrtxtp ));
    }

    /*
     * Output the vCard headers plus the BEGIN marker and VERSION tag.
     * once we do this we are committed to producing a vCard MIME object
     * so we must return LDAP_SUCCESS.
     */
    emit_vcard_headers( mimetype );
    write_vcard_property( DSGW_VCARD_PROP_BEGIN, DSGW_VCARD_BEGINEND_VALUE,
	    NULL, 0 );
    write_vcard_property( DSGW_VCARD_PROP_VERSION, DSGW_VCARD_VERSION,
	    NULL, 0 );

    /* Output the properties.
     * Note that for the secondary LDAP attribute we only use the
     * first value returned by the server.  I am sure someone won't
     * like this but anything else is silly since the main vCard
     * property we use a secondary LDAP attribute for is the "N"
     * property which looks like "sn;givenName".  We really have no way
     * of knowing which surname goes with which givenName so it looks
     * better not to create lots of "N" properties if there are multiple
     * givenNames.
     */
    for ( vcp = vcprops; vcp != NULL; vcp = vcp->dsgwvcprop_next ) {
	vals = ldap_get_values( ld, entry, vcp->dsgwvcprop_ldaptype );
	if ( vcp->dsgwvcprop_ldaptype2 == NULL ) {
	    vals2 = NULL;
	} else {
	    vals2 = ldap_get_values( ld, entry, vcp->dsgwvcprop_ldaptype2 );
	}

	if ( vals == NULL && vals2 == NULL ) {
	    continue;
	}

	is_mls = ( strcmp( vcp->dsgwvcprop_syntax, "mls" ) == 0 );

	if ( vals != NULL ) {
	    for ( i = 0; vals[ i ] != NULL; ++i ) {
		write_vcard_property( vcp->dsgwvcprop_property,
			    vals[i], vals2 == NULL ? NULL : vals2[0], is_mls );
	    }
	}  else {
	    for ( i = 0; vals2[ i ] != NULL; ++i ) {
		write_vcard_property( vcp->dsgwvcprop_property,
			    NULL, vals2[i], is_mls );
	    }
	}

	if ( vals != NULL ) {
	    ldap_value_free( vals );
	}
	if ( vals2 != NULL ) {
	    ldap_value_free( vals2 );
	}
    }


    /* Output the vCard END marker. */
    write_vcard_property( DSGW_VCARD_PROP_END, DSGW_VCARD_BEGINEND_VALUE,
	    NULL, 0 );

    /* Cleanup after ourselves. */
    ldap_msgfree( msgp );

    return( LDAP_SUCCESS );
}


/*
 * output a single vCard text property.
 */
static void
write_vcard_property( char *prop, char *val, char *val2, int is_mls )
{
    char	*s, *p, *tmpv, *mlsv;

    tmpv = mlsv = NULL;

    if ( val == NULL ) {
	val = "";
    }

    if ( val2 != NULL ) {
	tmpv = (char *)dsgw_ch_malloc( strlen( val ) + strlen( val2 ) + 2 );
	sprintf( tmpv, "%s;%s", val, val2 );
	val = tmpv;
    }

    if ( is_mls ) {
	val = mlsv = dsgw_mls_convertlines( val, ";", NULL, 0, 0 );
    }

    if (( s = ldif_type_and_value( prop, val, strlen( val ))) != NULL ) {
	/*
	 * vCard base64 rules are different than for LDIF so check and repair
	 * if necessary.
	 */
	if (( p = strchr( s, ':' )) != NULL && *(p+1) == ':' ) {
	    *p++ = '\0'; ++p;
	    dsgw_emits( s );
	    dsgw_emits( ";BASE64:\n " );
	    dsgw_emits( p );
	    dsgw_emits( "\n" );
	} else {
	    dsgw_emits( s );
	}
	free( s );
    }

    if ( tmpv != NULL ) {
	free( tmpv );
    }
    if ( mlsv != NULL ) {
	free( mlsv );
    }
}


/*
 * emit vCard Content-Type header, etc.
 */
static void
emit_vcard_headers( char *mimetype )
{
    if ( mimetype == NULL || *mimetype == '\0' ) {
	mimetype = DSGW_VCARD_MIMEHDR_TEXTDIR;	/* default */
    }

    dsgw_puts( "Content-Type: " );
    dsgw_puts( mimetype );
    if ( gc->gc_charset != NULL && *gc->gc_charset != '\0' ) {
	dsgw_puts( ";charset=" );
	dsgw_puts( gc->gc_charset );
    }
    dsgw_puts( "\n\n" );
}


/*
 * output a simple string without charset conversion (used for MIME headers)
 */
static void
dsgw_puts( char *s )
{
    dsgw_fputn( stdout, s, strlen( s ));
}


/*
 * return list of LDAP attributes we need to fetch
 */
static char **
vcard_ldapattrs( dsgwvcprop *vcprops )
{
    dsgwvcprop	*vcp;
    int		count;
    static char	**attrs = NULL;

    if ( attrs != NULL ) {
	return( attrs );
    }

    count = 0;
    for ( vcp = vcprops; vcp != NULL; vcp = vcp->dsgwvcprop_next ) {
	++count;
	if ( vcp->dsgwvcprop_ldaptype2 != NULL ) {
	    ++count;
	}
    }

    attrs = (char **)dsgw_ch_malloc(( count + 1 ) * sizeof( char * ));
    count = 0;
    for ( vcp = vcprops; vcp != NULL; vcp = vcp->dsgwvcprop_next ) {
	attrs[ count++ ] = vcp->dsgwvcprop_ldaptype;
	if ( vcp->dsgwvcprop_ldaptype2 != NULL ) {
	    attrs[ count++ ] = vcp->dsgwvcprop_ldaptype2;
	}
    }
    attrs[ count ] = NULL;

    return( attrs );
}
