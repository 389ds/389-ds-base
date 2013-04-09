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
	LDAPDebug(LDAP_DEBUG_ANY,
              "the function plugin_syntax2oid is deprecated - please use attr_get_syntax_oid instead\n", 0, 0, 0);
    PR_ASSERT(0);
	return( NULL );
}

int
plugin_call_syntax_get_compare_fn(
    void		*vpi,
	value_compare_fn_type *compare_fn
)
{
	LDAPDebug(LDAP_DEBUG_ANY,
              "the function plugin_call_syntax_get_compare_fn is deprecated - please use attr_get_value_cmp_fn instead\n", 0, 0, 0);
    PR_ASSERT(0);
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
	IFP ava_fn = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> plugin_call_syntax_filter_ava %s=%s\n", ava->ava_type,
	    ava->ava_value.bv_val, 0 );

	if ( ( a->a_mr_eq_plugin == NULL ) && ( a->a_mr_ord_plugin == NULL ) && ( a->a_plugin == NULL ) ) {
		LDAPDebug( LDAP_DEBUG_FILTER,
		    "<= plugin_call_syntax_filter_ava no plugin for attr (%s)\n",
		    a->a_type, 0, 0 );
		return( LDAP_PROTOCOL_ERROR );	/* syntax unkonwn */
	}

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );
	if (ava->ava_private) {
		int filter_normalized = 0;
		int f_flags = 0;
		f_flags = *(int *)ava->ava_private;
		filter_normalized = f_flags | SLAPI_FILTER_NORMALIZED_VALUE;
		slapi_pblock_set( &pipb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
	}

	rc = -1;	/* does not match by default */
	switch ( ftype ) {
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
		if ((a->a_mr_ord_plugin == NULL) &&
			((a->a_plugin->plg_syntax_flags &
			  SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING) == 0)) {
			LDAPDebug( LDAP_DEBUG_FILTER,
					   "<= plugin_call_syntax_filter_ava: attr (%s) has no ordering matching rule, and syntax does not define a compare function\n",
					   a->a_type, 0, 0 );
			rc = LDAP_PROTOCOL_ERROR;
			break;
		}
		/* if the attribute has an ordering matching rule plugin, use that,
		   otherwise, just use the syntax plugin */
		if (a->a_mr_ord_plugin != NULL) {
			slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_mr_ord_plugin );
			ava_fn = a->a_mr_ord_plugin->plg_mr_filter_ava;
		} else {
			slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );
			ava_fn = a->a_plugin->plg_syntax_filter_ava;
		}
		/* FALL */
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_APPROX:
		if (NULL == ava_fn) {
			/* if we have an equality matching rule plugin, use that,
			   otherwise, just use the syntax plugin */
			if (a->a_mr_eq_plugin) {
				slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_mr_eq_plugin );
				ava_fn = a->a_mr_eq_plugin->plg_mr_filter_ava;
			} else {
				slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );
				ava_fn = a->a_plugin->plg_syntax_filter_ava;
			}
		}

		if ( ava_fn != NULL ) {
			/* JCM - Maybe the plugin should use the attr value iterator too... */
			Slapi_Value **va;
			if(useDeletedValues) {
				va= valueset_get_valuearray(&a->a_deleted_values);
			} else {
				va= valueset_get_valuearray(&a->a_present_values);
			}
			if(va!=NULL) {
				rc = (*ava_fn)( &pipb, &ava->ava_value, va, ftype, retVal );
			}
		} else {
			LDAPDebug( LDAP_DEBUG_FILTER,
					   "<= plugin_call_syntax_filter_ava: attr (%s) has no ava filter function\n",
					   a->a_type, 0, 0 );
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY, "plugin_call_syntax_filter_ava: "
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
    Slapi_PBlock	*pb,
    Slapi_Attr		*a,
    struct subfilt	*fsub
)
{
	return(plugin_call_syntax_filter_sub_sv(pb,a,fsub));
}

int
plugin_call_syntax_filter_sub_sv(
    Slapi_PBlock	*pb,
    Slapi_Attr		*a,
    struct subfilt	*fsub
)
{
	Slapi_PBlock	pipb;
	int		rc;
	IFP sub_fn = NULL;
	int filter_normalized = 0;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> plugin_call_syntax_filter_sub_sv\n", 0, 0, 0 );

	if ( ( a->a_mr_sub_plugin == NULL ) && ( a->a_plugin == NULL ) ) {
		LDAPDebug( LDAP_DEBUG_FILTER,
				   "<= plugin_call_syntax_filter_sub_sv attribute (%s) has no substring matching rule or syntax plugin\n",
				   a->a_type, 0, 0 );
		return( -1 );	/* syntax unkonwn - does not match */
	}

	pblock_init( &pipb );
	if (pb) {
		slapi_pblock_get( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
		slapi_pblock_set( &pipb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
	}
	slapi_pblock_set( &pipb, SLAPI_PLUGIN_SYNTAX_FILTER_DATA, fsub );

	/* use the substr matching rule plugin if available, otherwise, use
	   the syntax plugin */
	if (a->a_mr_sub_plugin) {
		slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_mr_sub_plugin );
		sub_fn = a->a_mr_sub_plugin->plg_mr_filter_sub;
	} else {
		slapi_pblock_set( &pipb, SLAPI_PLUGIN, (void *) a->a_plugin );
		sub_fn = a->a_plugin->plg_syntax_filter_sub;
	}

	if ( sub_fn != NULL )
	{
		Slapi_Value **va= valueset_get_valuearray(&a->a_present_values);
		if (pb)
		{
			Operation *op = NULL;
			/* to pass SLAPI_SEARCH_TIMELIMIT & SLAPI_OPINITATED_TIME */
			slapi_pblock_get( pb, SLAPI_OPERATION, &op );
			slapi_pblock_set( &pipb, SLAPI_OPERATION, op );
		}
		rc = (*sub_fn)( &pipb, fsub->sf_initial, fsub->sf_any, fsub->sf_final, va);
	} else {
		rc = -1;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= plugin_call_syntax_filter_sub_sv %d\n",
	    rc, 0, 0 );
	return( rc );
}

/* Checks if the DN string is valid according to the Distinguished Name
 * syntax.  Setting override to 1 will force syntax checking to be performed,
 * even if syntax checking is disabled in the config.  Setting override to 0
 * will obey the config settings.
 *
 * Returns 1 if there is a syntax violation and sets the error message
 * appropriately.  Returns 0 if everything checks out fine.
 */
int
slapi_dn_syntax_check(
	Slapi_PBlock *pb, const char *dn, int override
)
{
	int ret = 0;
	int is_replicated_operation = 0;
	int syntaxcheck = config_get_syntaxcheck();
	int syntaxlogging = config_get_syntaxlogging();
	char errtext[ BUFSIZ ];
	char *errp = &errtext[0];
	struct slapdplugin *dn_plugin = NULL;
	struct berval dn_bval = {0};

	if (pb != NULL) {
		slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
	}

	/* If syntax checking and logging are off, or if this is a
	 * replicated operation, just return that the syntax is OK. */
	if (((syntaxcheck == 0) && (syntaxlogging == 0) && (override == 0)) ||
	    is_replicated_operation) {
		goto exit;
	}

	/* Locate the dn syntax plugin. */
	slapi_attr_type2plugin("distinguishedName", (void **)&dn_plugin);

	/* Assume the value is valid if we don't find a dn validate function */
	if (dn_plugin && dn_plugin->plg_syntax_validate != NULL) {
		/* Create a berval to pass to the validate function. */
		if (dn) {
			dn_bval.bv_val = (char *)dn;
			dn_bval.bv_len = strlen(dn);

			/* Validate the value. */
			if (dn_plugin->plg_syntax_validate(&dn_bval) != 0) {
				if (syntaxlogging) {
					slapi_log_error( SLAPI_LOG_FATAL, "Syntax Check",
						"DN value (%s) invalid per syntax\n", dn);
				}

				if (syntaxcheck || override) {
					if (pb) {
						errp += PR_snprintf( errp, sizeof(errtext),
								"DN value invalid per syntax\n" );
					}
					ret = 1;
				}
			}
		}
	}

	/* See if we need to set the error text in the pblock. */
	if (pb && errp != &errtext[0]) {
		/* SLAPI_PB_RESULT_TEXT duplicates the text in slapi_pblock_set */
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	}

exit:
	return( ret );
}

/* Checks if the values of all attributes in an entry are valid for the
 * syntax specified for the attribute in question.  Setting override to
 * 1 will force syntax checking to be performed, even if syntax checking
 * is disabled in the config.  Setting override to 0 will obey the config
 * settings.
 *
 * Returns 1 if there is a syntax violation and sets the error message
 * appropriately.  Returns 0 if everything checks out fine.
 * 
 * Note: this function allows NULL pb.  If NULL, is_replicated_operation
 * will not checked and error message will not be generated and returned.
 */
int
slapi_entry_syntax_check(
	Slapi_PBlock *pb, Slapi_Entry *e, int override
)
{
	int ret = 0;
	int i = 0;
	int is_replicated_operation = 0;
	int syntaxcheck = config_get_syntaxcheck();
	int syntaxlogging = config_get_syntaxlogging();
	Slapi_Attr *prevattr = NULL;
	Slapi_Attr *a = NULL;
	char errtext[ BUFSIZ ];
	char *errp = &errtext[0];
	size_t err_remaining = sizeof(errtext);

	if (pb != NULL) {
		slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
	}

	/* If syntax checking and logging are off, or if this is a
         * replicated operation, just return that the syntax is OK. */
	if (((syntaxcheck == 0) && (syntaxlogging == 0) && (override == 0)) ||
	    is_replicated_operation) {
		goto exit;
	}

	i = slapi_entry_first_attr(e, &a);

	while ((-1 != i) && a && (a->a_plugin != NULL)) {
		/* If no validate function is available for this type, just
		 * assume that the value is valid. */
		if ( a->a_plugin->plg_syntax_validate != NULL ) {
			int numvals = 0;

			slapi_attr_get_numvalues(a, &numvals);
			if ( numvals > 0 ) {
				Slapi_Value *val = NULL;
				const struct berval *bval = NULL;
				int hint = slapi_attr_first_value(a, &val);

				/* iterate through each value to check if it's valid */
				while (val != NULL) {
					bval = slapi_value_get_berval(val);
					if ((a->a_plugin->plg_syntax_validate( bval )) != 0) {
						if (syntaxlogging) {
							slapi_log_error( SLAPI_LOG_FATAL, "Syntax Check",
							                "\"%s\": (%s) value #%d invalid per syntax\n",
							                slapi_entry_get_dn(e), a->a_type, hint );
						}

						if (syntaxcheck || override) {
							if (pb) {
								/* Append new text to any existing text. */
								errp += PR_snprintf( errp, err_remaining,
								    "%s: value #%d invalid per syntax\n", a->a_type, hint );
								err_remaining -= errp - &errtext[0];
							}
							ret = 1;
						}
					}

					hint = slapi_attr_next_value(a, hint, &val);
				}
			}
		}

		prevattr = a;
		i = slapi_entry_next_attr(e, prevattr, &a);
	}

	/* See if we need to set the error text in the pblock. */
	if (pb && (errp != &errtext[0])) { /* Check pb for coverity  */
		/* SLAPI_PB_RESULT_TEXT duplicates the text in slapi_pblock_set */
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	}

exit:
	return( ret );
}

/* Checks if the values of all attributes being added in a Slapi_Mods
 * are valid for the syntax specified for the attribute in question.
 * The new values in an add or replace modify operation and the newrdn
 * value for a modrdn operation will be checked.
 * Returns 1 if there is a syntax violation and sets the error message
 * appropriately.  Returns 0 if everything checks out fine.
 *
 * Note: this function allows NULL pb.  If NULL, is_replicated_operation
 * will not checked and error message will not be generated and returned.
 */
int
slapi_mods_syntax_check(
	Slapi_PBlock *pb, LDAPMod **mods, int override
)
{
	int ret = 0;
	int i, j = 0;
	int is_replicated_operation = 0;
	int syntaxcheck = config_get_syntaxcheck();
	int syntaxlogging = config_get_syntaxlogging();
	char errtext[ BUFSIZ ];
	char *errp = &errtext[0];
	size_t err_remaining = sizeof(errtext);
	const char *dn = NULL;
	Slapi_DN *sdn = NULL;
	LDAPMod *mod = NULL;

	if (mods == NULL) {
		ret = 1;
		goto exit;
	}

	if (pb != NULL) {
		slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
		slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
		dn = slapi_sdn_get_dn(sdn);
	}

	/* If syntax checking and logging are  off, or if this is a
	 * replicated operation, just return that the syntax is OK. */
	if (((syntaxcheck == 0) && (syntaxlogging == 0) && (override == 0)) ||
	    is_replicated_operation) {
		goto exit;
	}

	/* Loop through mods */
	for (i = 0; mods[i] != NULL; i++) {
		mod = mods[i];

		/* We only care about replace and add modify operations that
		 * are truly adding new values to the entry. */
		if ((SLAPI_IS_MOD_REPLACE(mod->mod_op) || SLAPI_IS_MOD_ADD(mod->mod_op)) &&
		    (mod->mod_bvalues != NULL)) {
			struct slapdplugin *syntax_plugin = NULL;

			/* Find the plug-in for this type, then call it's
			 * validate function.*/
			slapi_attr_type2plugin(mod->mod_type, (void **)&syntax_plugin);
			if ((syntax_plugin != NULL) && (syntax_plugin->plg_syntax_validate != NULL)) {
				/* Loop through the values and validate each one */
				for (j = 0; mod->mod_bvalues[j] != NULL; j++) {
					if (syntax_plugin->plg_syntax_validate(mod->mod_bvalues[j]) != 0) {
						if (syntaxlogging) {
							slapi_log_error( SLAPI_LOG_FATAL, "Syntax Check", "\"%s\": (%s) value #%d invalid per syntax\n", 
							    dn ? dn : "NULL", mod->mod_type, j );
						}

						if (syntaxcheck || override) {
							if (pb) {
								/* Append new text to any existing text. */
								errp += PR_snprintf( errp, err_remaining,
								    "%s: value #%d invalid per syntax\n", mod->mod_type, j );
								err_remaining -= errp - &errtext[0];
							}
							ret = 1;
						}
					}
				}
			}
		}
	}

	/* See if we need to set the error text in the pblock. */
	if (pb && (errp != &errtext[0])) { /* Check pb for coverity  */
		/* SLAPI_PB_RESULT_TEXT duplicates the text in slapi_pblock_set */
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	}

exit:
	return( ret );
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

int
slapi_attr_values2keys_sv_pb(
    const Slapi_Attr	*sattr,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype,
	Slapi_PBlock	*pb
)
{
	int			rc;
	struct slapdplugin	*pi = NULL;
	IFP v2k_fn = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> slapi_attr_values2keys_sv\n",
	    0, 0, 0 );

	switch (ftype) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_APPROX:
		if (sattr->a_mr_eq_plugin) {
			pi = sattr->a_mr_eq_plugin;
			v2k_fn = sattr->a_mr_eq_plugin->plg_mr_values2keys;
		} else if (sattr->a_plugin) {
			pi = sattr->a_plugin;
			v2k_fn = sattr->a_plugin->plg_syntax_values2keys;
		}
		break;
	case LDAP_FILTER_SUBSTRINGS:
		if (sattr->a_mr_sub_plugin) {
			pi = sattr->a_mr_sub_plugin;
			v2k_fn = sattr->a_mr_sub_plugin->plg_mr_values2keys;
		} else if (sattr->a_plugin) {
			pi = sattr->a_plugin;
			v2k_fn = sattr->a_plugin->plg_syntax_values2keys;
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY, "<= slapi_attr_values2keys_sv: ERROR: unsupported filter type %d\n",
				   ftype, 0, 0 );
		rc = LDAP_PROTOCOL_ERROR;
		goto done;
	}

	slapi_pblock_set( pb, SLAPI_PLUGIN, pi );

	*ivals = NULL;
	rc = -1;	/* means no values2keys function */
	if ( ( pi != NULL ) && ( v2k_fn != NULL ) ) {
		rc = (*v2k_fn)( pb, vals, ivals, ftype );
	}

done:
	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_call_syntax_values2keys %d\n", rc, 0, 0 );
	return( rc );
}

int
slapi_attr_values2keys_sv(
    const Slapi_Attr	*sattr,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	Slapi_PBlock pb;
	pblock_init(&pb);
	return slapi_attr_values2keys_sv_pb(sattr, vals, ivals, ftype, &pb);
}

SLAPI_DEPRECATED int
slapi_attr_values2keys( /* JCM SLOW FUNCTION */
    const Slapi_Attr	*sattr,
    struct berval	**vals,
    struct berval	***ivals,
    int			ftype
)
{
	int rc;
	Slapi_Value **svin= NULL;
	Slapi_Value **svout= NULL;
	valuearray_init_bervalarray(vals,&svin); /* JCM SLOW FUNCTION */
	rc= slapi_attr_values2keys_sv(sattr,svin,&svout,ftype);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	valuearray_free(&svin);
	return rc;
}

/*
 * almost identical to slapi_call_syntax_values2keys_sv except accepting 
 * pblock to pass some info such as substrlen.
 */
int
slapi_call_syntax_values2keys_sv_pb(
    void			*vpi,
    Slapi_Value		**vals,
    Slapi_Value		***ivals,
    int				ftype,
	Slapi_PBlock	*pb
)
{
	int					rc;
	struct slapdplugin	*pi = vpi;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> slapi_call_syntax_values2keys\n",
	    0, 0, 0 );

	slapi_pblock_set( pb, SLAPI_PLUGIN, vpi );

	*ivals = NULL;
	rc = -1;	/* means no values2keys function */
	if ( pi != NULL && pi->plg_syntax_values2keys != NULL ) {
		rc = pi->plg_syntax_values2keys( pb, vals, ivals, ftype );
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

int
slapi_attr_assertion2keys_ava_sv(
    const Slapi_Attr *sattr,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	int			rc;
	Slapi_PBlock		pipb;
	struct slapdplugin	*pi = NULL;
	IFP a2k_fn = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> slapi_attr_assertion2keys_ava_sv\n", 0, 0, 0 );

	switch (ftype) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_APPROX:
	case LDAP_FILTER_EQUALITY_FAST: 
		if (sattr->a_mr_eq_plugin) {
			pi = sattr->a_mr_eq_plugin;
			a2k_fn = sattr->a_mr_eq_plugin->plg_mr_assertion2keys_ava;
		} else if (sattr->a_plugin) {
			pi = sattr->a_plugin;
			a2k_fn = sattr->a_plugin->plg_syntax_assertion2keys_ava;
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY, "<= slapi_attr_assertion2keys_ava_sv: ERROR: unsupported filter type %d\n",
				   ftype, 0, 0 );
		rc = LDAP_PROTOCOL_ERROR;
		goto done;
	}

	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, pi );

	rc = -1;	/* means no assertion2keys function */
	if ( a2k_fn != NULL ) {
		rc = (*a2k_fn)( &pipb, val, ivals, ftype );
	}
done:
	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_attr_assertion2keys_ava_sv %d\n", rc, 0, 0 );
	return( rc );
}

SLAPI_DEPRECATED int
slapi_attr_assertion2keys_ava( /* JCM SLOW FUNCTION */
    const Slapi_Attr *sattr,
    struct berval	*val,
    struct berval	***ivals,
    int			ftype
)
{
	int rc;
	Slapi_Value svin;
	Slapi_Value **svout= NULL;
	slapi_value_init_berval(&svin, val);
	rc= slapi_attr_assertion2keys_ava_sv(sattr,&svin,&svout,ftype);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	value_done(&svin);
	return rc;
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

int
slapi_attr_assertion2keys_sub_sv(
    const Slapi_Attr *sattr,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	int			rc;
	Slapi_PBlock		pipb;
	struct slapdplugin	*pi = NULL;
	IFP a2k_fn = NULL;

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "=> slapi_attr_assertion2keys_sub_sv\n", 0, 0, 0 );

	if (sattr->a_mr_sub_plugin) {
		pi = sattr->a_mr_sub_plugin;
		a2k_fn = sattr->a_mr_sub_plugin->plg_mr_assertion2keys_sub;
	} else if (sattr->a_plugin) {
		pi = sattr->a_plugin;
		a2k_fn = sattr->a_plugin->plg_syntax_assertion2keys_sub;
	}
	pblock_init( &pipb );
	slapi_pblock_set( &pipb, SLAPI_PLUGIN, pi );

	rc = -1;	/* means no assertion2keys function */
	*ivals = NULL;
	if ( a2k_fn != NULL ) {
		rc = (*a2k_fn)( &pipb, initial, any, final, ivals );
	}

	LDAPDebug( LDAP_DEBUG_FILTER,
	    "<= slapi_attr_assertion2keys_sub_sv %d\n", rc, 0, 0 );
	return( rc );
}

SLAPI_DEPRECATED int
slapi_attr_assertion2keys_sub( /* JCM SLOW FUNCTION */
    const Slapi_Attr *sattr,
    char		*initial,
    char		**any,
    char		*final,
    struct berval	***ivals
)
{
	int rc;
	Slapi_Value **svout= NULL;
	rc= slapi_attr_assertion2keys_sub_sv(sattr,initial,any,final,&svout);
	valuearray_get_bervalarray(svout,ivals); /* JCM SLOW FUNCTION */
	valuearray_free(&svout);
	return rc;
}

void
slapi_attr_value_normalize_ext(
	Slapi_PBlock *pb,
	const Slapi_Attr *sattr, /* if sattr is NULL, type must be attr type name */
	const char *type,
	char *val,
	int trim_spaces,
	char **retval,
	unsigned long filter_type
)
{
	Slapi_Attr myattr;
	VFPV norm_fn = NULL;

	if (!sattr) {
		sattr = slapi_attr_init(&myattr, type);
	}

	/* use the filter type to determine which matching rule to use */
	switch (filter_type) {
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
		if (sattr->a_mr_ord_plugin) {
			norm_fn = sattr->a_mr_ord_plugin->plg_mr_normalize;
		}
		break;
	case LDAP_FILTER_EQUALITY:
		if (sattr->a_mr_eq_plugin) {
			norm_fn = sattr->a_mr_eq_plugin->plg_mr_normalize;
		}
		break;
	case LDAP_FILTER_SUBSTRINGS:
		if (sattr->a_mr_sub_plugin) {
			norm_fn = sattr->a_mr_sub_plugin->plg_mr_normalize;
		}
		break;
	default:
		break;
	}

	if (!norm_fn && sattr->a_plugin) {
		/* no matching rule specific normalizer specified - use syntax default */
		norm_fn = sattr->a_plugin->plg_syntax_normalize;
	}
	if (norm_fn) {
		(*norm_fn)(pb, val, trim_spaces, retval);
	}
	if (sattr == &myattr) {
		attr_done(&myattr);
	}
	return;
}

void
slapi_attr_value_normalize(
	Slapi_PBlock *pb,
	const Slapi_Attr *sattr, /* if sattr is NULL, type must be attr type name */
	const char *type,
	char *val,
	int trim_spaces,
	char **retval
)
{
	slapi_attr_value_normalize_ext(pb, sattr, type, val, trim_spaces, retval, 0);
}
