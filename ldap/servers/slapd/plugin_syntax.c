/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * plugin_syntax.c - routines for calling syntax plugins
 */


#include "slap.h"


struct slapdplugin *
plugin_syntax_find( const char *nameoroid )
{
	struct slapdplugin	*pi;

	/* LDAPDebug( LDAP_DEBUG_FILTER, "=> plugin_syntax_find (%s)\n", nameoroid, 0, 0 ); */
	for ( pi = get_plugin_list(PLUGIN_LIST_SYNTAX); pi != NULL; pi = pi->plg_next ) {
		if ( charray_inlist( pi->plg_syntax_names, (char *)nameoroid ) ) {
			break;
		}
	}
	/* LDAPDebug( LDAP_DEBUG_FILTER, "<= plugin_syntax_find %d\n", pi, 0, 0 ); */
	return ( pi );
}


/*
 * Enumerate all the syntax plugins, calling (*sef)() for each one.
 */
void
plugin_syntax_enumerate( SyntaxEnumFunc sef, void *arg )
{
	struct slapdplugin	*pi;

	for ( pi = get_plugin_list(PLUGIN_LIST_SYNTAX); pi != NULL;
			pi = pi->plg_next ) {
		(*sef)( pi->plg_syntax_names, &pi->plg_desc, arg );
	}
}


struct slapdplugin *
slapi_get_global_syntax_plugins()
{
  return get_plugin_list(PLUGIN_LIST_SYNTAX);
}

char *
plugin_syntax2oid( struct slapdplugin *pi )
{
	return( pi->plg_syntax_oid );
}

int
plugin_call_syntax_get_compare_fn(
    void		*vpi,
	value_compare_fn_type *compare_fn
)
{
	struct slapdplugin	*pi = vpi;
	*compare_fn = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE,
	    "=> plugin_call_syntax_get_compare_fn\n",0,0, 0 );

	if ( pi == NULL ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= plugin_syntax no plugin for attribute type\n",
		    0, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );	/* syntax unkonwn */
	}

	if ( (pi->plg_syntax_flags & SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING) == 0 ) {
		return( LDAP_PROTOCOL_ERROR );
	}

	if (pi->plg_syntax_filter_ava == NULL) {
		LDAPDebug( LDAP_DEBUG_ANY, "<= plugin_call_syntax_get_compare_fn: "
		    "no filter_ava found for attribute type\n", 0, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );
	}

	if (pi->plg_syntax_compare == NULL) {
		return( LDAP_PROTOCOL_ERROR );
	}

	*compare_fn = (value_compare_fn_type) pi->plg_syntax_compare;

	LDAPDebug( LDAP_DEBUG_TRACE,
	    "<= plugin_call_syntax_get_compare_fn \n", 0, 0, 0 );
	return( 0 );
}

int
plugin_call_syntax_filter_ava(
    const Slapi_Attr	*a,
    int		ftype,
    struct ava	*ava
)
{
	return(plugin_call_syntax_filter_ava_sv(a,ftype,ava,NULL,0 /*Present*/));
}

int
plugin_call_syntax_filter_ava_sv(
    const Slapi_Attr	*a,
    int		ftype,
    struct ava	*ava,
	Slapi_Value **retVal,
	int useDeletedValues
)
{
	int		rc;
	Slapi_PBlock	pipb;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> plugin_call_syntax_filter_ava %s=%s\n", ava->ava_type,
	    ava->ava_value.bv_val, 0 );

	if ( a->a_plugin == NULL ) {
		LDAPDebug( LDAP_DEBUG_FILTER,
		    "<= plugin_syntax no plugin for attr (%s)\n",
		    a->a_type, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );	/* syntax unkonwn */
	}

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );

	rc = -1;	/* does not match by default */
	switch ( ftype ) {
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
		if ( (a->a_plugin->plg_syntax_flags &
		    SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING) == 0 ) {
			rc = LDAP_PROTOCOL_ERROR;
			break;
		}
		/* FALL */
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_APPROX:
		if ( a->a_plugin->plg_syntax_filter_ava != NULL )
		{
			/* JCM - Maybe the plugin should use the attr value iterator too... */
			Slapi_Value **va;
			if(useDeletedValues)
				va= valueset_get_valuearray(&a->a_deleted_values);
			else
				va= valueset_get_valuearray(&a->a_present_values);
			if(va!=NULL)
			{
				rc = a->a_plugin->plg_syntax_filter_ava( &pipb,
				    &ava->ava_value,
					va,
	                ftype, retVal );
			}
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY, "plugin_call_syntax_filter: "
		    "unknown filter type %d\n", ftype, 0, 0 );
		rc = LDAP_PROTOCOL_ERROR;
		break;
	}

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= plugin_call_syntax_filter_ava %d\n", rc, 0, 0 );
	return( rc );
}

int
plugin_call_syntax_filter_sub(
    Slapi_Attr		*a,
    struct subfilt	*fsub
)
{
	return(plugin_call_syntax_filter_sub_sv(a,fsub));
}

int
plugin_call_syntax_filter_sub_sv(
    Slapi_Attr		*a,
    struct subfilt	*fsub
)
{
	Slapi_PBlock	pipb;
	int		rc;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> plugin_call_syntax_filter_sub\n", 0, 0, 0 );

	if ( a->a_plugin == NULL ) {
		LDAPDebug( LDAP_DEBUG_FILTER,
		    "<= plugin_call_syntax_filter no plugin\n", 0, 0, 0 );
		return( -1 );	/* syntax unkonwn - does not match */
	}

	if ( a->a_plugin->plg_syntax_filter_sub != NULL )
	{
		Slapi_Value **va= valueset_get_valuearray(&a->a_present_values);
		pblock_init( &pipb );
		slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );
		rc = a->a_plugin->plg_syntax_filter_sub( &pipb,
		    fsub->sf_initial, fsub->sf_any, fsub->sf_final, va);
	} else {
		rc = -1;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= plugin_call_syntax_filter_sub_sv %d\n",
	    rc, 0, 0 );
	return( rc );
}

SLAPI_DEPRECATED int
slapi_call_syntax_values2keys( /* JCM SLOW FUNCTION */
    void		*vpi,
    struct berval	**vals,
    struct berval	***ivals,
    int			ftype
)
{
	int rc;
	Slapi_Value **svin= NULL;
	Slapi_Value **svout= NULL;
	valuearray_init_bervalarray(vals,&svin); /* JCM SLOW FUNCTION */
	rc= slapi_call_syntax_values2keys_sv(vpi,svin,&svout,ftype);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	valuearray_free(&svin);
	return rc;
}

int
slapi_call_syntax_values2keys_sv(
    void		*vpi,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	int			rc;
	Slapi_PBlock		pipb;
	struct slapdplugin	*pi = vpi;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> slapi_call_syntax_values2keys\n",
	    0, 0, 0 );

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, vpi );

	*ivals = NULL;
	rc = -1;	/* means no values2keys function */
	if ( pi != NULL && pi->plg_syntax_values2keys != NULL ) {
		rc = pi->plg_syntax_values2keys( &pipb, vals, ivals, ftype );
	}

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_call_syntax_values2keys %d\n", rc, 0, 0 );
	return( rc );
}

SLAPI_DEPRECATED int
slapi_call_syntax_assertion2keys_ava( /* JCM SLOW FUNCTION */
    void		*vpi,
    struct berval	*val,
    struct berval	***ivals,
    int			ftype
)
{
	int rc;
	Slapi_Value svin;
	Slapi_Value **svout= NULL;
	slapi_value_init_berval(&svin, val);
	rc= slapi_call_syntax_assertion2keys_ava_sv(vpi,&svin,&svout,ftype);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	value_done(&svin);
	return rc;
}

SLAPI_DEPRECATED int
slapi_call_syntax_assertion2keys_ava_sv(
    void		*vpi,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	int			rc;
	Slapi_PBlock		pipb;
	struct slapdplugin	*pi = vpi;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> slapi_call_syntax_assertion2keys_ava\n", 0, 0, 0 );

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, vpi );

	rc = -1;	/* means no assertion2keys function */
	if ( pi->plg_syntax_assertion2keys_ava != NULL ) {
		rc = pi->plg_syntax_assertion2keys_ava( &pipb, val, ivals, ftype );
	}

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_call_syntax_assertion2keys_ava %d\n", rc, 0, 0 );
	return( rc );
}

SLAPI_DEPRECATED int
slapi_call_syntax_assertion2keys_sub( /* JCM SLOW FUNCTION */
    void		*vpi,
    char		*initial,
    char		**any,
    char		*final,
    struct berval	***ivals
)
{
	int rc;
	Slapi_Value **svout= NULL;
	rc= slapi_call_syntax_assertion2keys_sub_sv(vpi,initial,any,final,&svout);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	return rc;
}

int
slapi_call_syntax_assertion2keys_sub_sv(
    void		*vpi,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	int			rc;
	Slapi_PBlock		pipb;
	struct slapdplugin	*pi = vpi;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> slapi_call_syntax_assertion2keys_sub\n", 0, 0, 0 );

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, vpi );

	rc = -1;	/* means no assertion2keys function */
	*ivals = NULL;
	if ( pi->plg_syntax_assertion2keys_sub != NULL ) {
		rc = pi->plg_syntax_assertion2keys_sub( &pipb, initial, any,
		    final, ivals );
	}

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_call_syntax_assertion2keys_sub %d\n", rc, 0, 0 );
	return( rc );
}

