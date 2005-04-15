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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/***********************************************************************
** NAME
**  utils.c
**
** DESCRIPTION
**
**
** AUTHOR
**   <rweltman@netscape.com>
**
***********************************************************************/


/***********************************************************************
** Includes
***********************************************************************/

#include "plugin-utils.h"
#include "nspr.h"

static char *plugin_name = "utils";

/*
 * Lock for updating a counter (global for all counters)
 */
static Slapi_Mutex *counter_lock = NULL;

/* ------------------------------------------------------------ */
/*
 * op_error - Record (and report) an operational error.
 */
int
op_error(int internal_error) {
	slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
					"Internal error: %d\n", internal_error);

	return LDAP_OPERATIONS_ERROR;
}

int initCounterLock() {
	if ( NULL == counter_lock ) {
		if ( !(counter_lock = slapi_new_mutex()) ) {
			return 200;
		}
	}
	return 0;
}

/* ------------------------------------------------------------ */
/*
 * readPblockAndEntry - search for and read an entry
 * Return:
 *   A pblock containing the entry, or NULL
 */
Slapi_PBlock *
readPblockAndEntry( const char *baseDN, const char *filter,
					char *attrs[] ) {
	int result = 0;
	Slapi_PBlock *spb = NULL;

	BEGIN
        int sres;

		/* Perform the search - the new pblock needs to be freed */
		spb = slapi_search_internal((char *)baseDN, LDAP_SCOPE_BASE,
									(char *)filter, NULL, attrs, 0);
		if ( !spb ) {
			result = op_error(20);
			break;
		}
 
		if ( slapi_pblock_get( spb, SLAPI_PLUGIN_INTOP_RESULT, &sres ) ) {
			result = op_error(21);
			break;
		} else if (sres) {
			result = op_error(22);
			break;
		}
    END

	return spb;
}

/* ------------------------------------------------------------ */
/*
 * hasObjectClass - read an entry and check if it has a
 *   particular object class value
 * Return:
 *   1 - the entry contains the object class value
 *   0 - the entry doesn't contain the object class value
 */
int
entryHasObjectClass(Slapi_PBlock *pb, Slapi_Entry *e,
					const char *objectClass) {
	Slapi_Attr *attr;
	Slapi_Value *v;
	const struct berval *bv;
	int vhint;

	if ( slapi_entry_attr_find(e, "objectclass", &attr) ) {
		return 0;  /* no objectclass values! */
	}

	/*
	 * Check each of the object class values in turn.
	 */
	for ( vhint = slapi_attr_first_value( attr, &v );
			vhint != -1;
			vhint = slapi_attr_next_value( attr, vhint, &v )) {
		bv = slapi_value_get_berval(v);
		if ( NULL != bv && NULL != bv->bv_val &&
				!strcasecmp(bv->bv_val, objectClass) ) {
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------ */
/*
 * dnHasObjectClass - read an entry if it has a particular object class
 * Return:
 *   A pblock containing the entry, or NULL
 */
Slapi_PBlock *
dnHasObjectClass( const char *baseDN, const char *objectClass ) {
	int result = 0;
	char *filter = NULL;
	Slapi_PBlock *spb = NULL;

	BEGIN
		Slapi_Entry **entries;
		char *attrs[2];

		/* Perform the search - the new pblock needs to be freed */
		attrs[0] = "objectclass";
		attrs[1] = NULL;
		filter = PR_smprintf("objectclass=%s", objectClass );
		if ( !(spb = readPblockAndEntry( baseDN, filter, attrs) ) ) {
			break;
		}
 
		if ( slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
							  &entries) ) {
			result = op_error(23);
			break;
		}
		/*
		 * Can only be one entry returned on a base search; just check
		 * the first one
		 */
		if ( !*entries ) {
			/* Clean up */
			slapi_free_search_results_internal(spb);
			slapi_pblock_destroy(spb);
			spb = NULL;
		}
    END

	if (filter) {
		PR_smprintf_free(filter);
	}
	return spb;
}

/* ------------------------------------------------------------ */
/*
 * dnHasAttribute - read an entry if it has a particular attribute
 * Return:
 *   The entry, or NULL
 */
Slapi_PBlock *
dnHasAttribute( const char *baseDN, const char *attrName ) {
	int result = 0;
	Slapi_PBlock *spb = NULL;
	char *filter = NULL;

	BEGIN
        int sres;
		Slapi_Entry **entries;
		char *attrs[2];

		/* Perform the search - the new pblock needs to be freed */
		attrs[0] = (char *)attrName;
		attrs[1] = NULL;
		filter = PR_smprintf( "%s=*", attrName );
		spb = slapi_search_internal((char *)baseDN, LDAP_SCOPE_BASE,
									filter, NULL, attrs, 0);
		if ( !spb ) {
			result = op_error(20);
			break;
		}
 
		if ( slapi_pblock_get( spb, SLAPI_PLUGIN_INTOP_RESULT, &sres ) ) {
			result = op_error(21);
			break;
		} else if (sres) {
			result = op_error(22);
			break;
		}

		if ( slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
							  &entries) ) {
			result = op_error(23);
			break;
		}
		/*
		 * Can only be one entry returned on a base search; just check
		 * the first one
		 */
		if ( !*entries ) {
			/* Clean up */
			slapi_free_search_results_internal(spb);
			slapi_pblock_destroy(spb);
			spb = NULL;
		}
    END

	if (filter) {
		PR_smprintf_free(filter);
	}
	return spb;
}

/* ------------------------------------------------------------ */
/*
 * setCounter - set the value of a counter
 * 
 * Return:
 *   LDAP_SUCCESS - updated the attribute
 *   other - failure to update the count
 */
int
setCounter( Slapi_Entry *e, const char *attrName, int value ) {
	int result = LDAP_SUCCESS;
	Slapi_PBlock *modifySpb = NULL;
	char strValue[16];
	char *strValues[2] = { NULL };
	LDAPMod mod;
	LDAPMod *mods[2];
	int res;

	BEGIN
		/* Store the updated value */
		strValues[0] = strValue;
		sprintf( strValue, "%d", value );
		mod.mod_op = LDAP_MOD_REPLACE;
		mod.mod_type = (char *)attrName;
		mod.mod_values = strValues;
		mods[0] = &mod;
		mods[1] = NULL;
		modifySpb = slapi_modify_internal( slapi_entry_get_dn(e), mods, 
										   NULL, 1 );
		/* Check if the operation succeeded */
		if ( slapi_pblock_get( modifySpb, SLAPI_PLUGIN_INTOP_RESULT,
							   &res ) ) {
			result = op_error(33);
			break;
		} else if (res) {
			result = op_error(34);
			break;
		}
		slapi_pblock_destroy(modifySpb);
	END
	return result;
}

/* ------------------------------------------------------------ */
/*
 * updateCounter - read and increment/decrement the value of a counter
 * 
 * Return:
 *   LDAP_SUCCESS - updated the attribute
 *   other - failure to update the count
 */
int
updateCounter( Slapi_Entry *e, const char *attrName, int increment ) {
	int result = LDAP_SUCCESS;
	Slapi_PBlock *modifySpb = NULL;
	Slapi_Attr *attr;
	int value = 0;
	char strValue[16];
	char *strValues[2] = { NULL };
	LDAPMod mod;
	LDAPMod *mods[2];
	int res;

	BEGIN
		/* Lock the entry */
		slapi_lock_mutex(counter_lock);
	    /* Get the count attribute */
	    if ( slapi_entry_attr_find(e, (char *)attrName, &attr) ) {
			/* No count yet; that's OK */
		} else {
			/* Get the first value for the attribute */
			Slapi_Value *v = NULL;
			const struct berval *bv = NULL;

			if ( -1 == slapi_attr_first_value( attr, &v ) || NULL == v ||
					NULL == ( bv = slapi_value_get_berval(v)) ||
					NULL == bv->bv_val ) {
				/* No values yet; that's OK, too */
			} else {
				value = atoi( bv->bv_val );
			}
		}
		/* Add the increment */
		value += increment;
		if ( value < 0 ) {
			value = 0;
		}
		/* Store the updated value */
		strValues[0] = strValue;
		sprintf( strValue, "%d", value );
		mod.mod_op = LDAP_MOD_REPLACE;
		mod.mod_type = (char *)attrName;
		mod.mod_values = strValues;
		mods[0] = &mod;
		mods[1] = NULL;
		modifySpb = slapi_modify_internal( slapi_entry_get_dn(e), mods, 
										   NULL, 1 );
		/* Check if the operation succeeded */
		if ( slapi_pblock_get( modifySpb, SLAPI_PLUGIN_INTOP_RESULT,
							   &res ) ) {
			result = op_error(33);
			break;
		} else if (res) {
			result = op_error(34);
			break;
		}
		slapi_pblock_destroy(modifySpb);
		/* Unlock the entry */
		slapi_unlock_mutex(counter_lock);
#ifdef DEBUG
		slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
						"adjusted %s in %s by %d to %d\n",
						attrName, slapi_entry_get_dn(e), increment, value);
#endif

	END
	return result;
}

/* ------------------------------------------------------------ */
/*
 * updateCounterByDN - read and increment/decrement the value of a counter
 * 
 * Return:
 *   LDAP_SUCCESS - updated the attribute
 *   other - failure to update the count
 */
int
updateCounterByDN( const char *dn, const char *attrName, int increment ) {
	int result = LDAP_SUCCESS;
	Slapi_PBlock *spb = NULL;
	Slapi_Entry **entries;

	BEGIN
		char *attrs[2];
        int sres;

		/* Perform the search - the new pblock needs to be freed */
		attrs[0] = (char *)attrName;
		attrs[1] = NULL;
		spb = slapi_search_internal((char *)dn, LDAP_SCOPE_BASE,
									"objectclass=*", NULL, attrs, 0);
		if ( !spb ) {
			result = op_error(20);
			break;
		}
 
		if ( slapi_pblock_get( spb, SLAPI_PLUGIN_INTOP_RESULT, &sres ) ) {
			result = op_error(21);
			break;
		} else if (sres) {
			result = op_error(22);
			break;
		}

		if ( slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
							  &entries) ) {
			result = op_error(23);
			break;
		}
	END
	if ( 0 == result ) {
		result = updateCounter( *entries, attrName, increment );
	}
	if ( NULL != spb ) {
		/* Clean up */
		slapi_free_search_results_internal(spb);
		slapi_pblock_destroy(spb);
	}
	return result;
}

/*
 * Lock for accessing a cache (global for all caches)
 */
static Slapi_Mutex *cache_lock = NULL;

DNLink *cacheInit() {
	DNLink *root;
	slapi_lock_mutex(cache_lock);
	root = (DNLink *)malloc( sizeof(DNLink) );
	root->next = NULL;
	root->data = NULL;
	root->dn = (char *)malloc(1);
	root->dn[0] = 0;
	slapi_unlock_mutex(cache_lock);
	return root;
}

DNLink *cacheAdd( DNLink *root, char *dn, void *data ) {
	if ( NULL == root ) {
		return NULL;
	}
	slapi_lock_mutex(cache_lock);
	for( ; root->next; root = root->next ) {
	}
	root->next = (DNLink *)malloc( sizeof(DNLink) );
	root = root->next;
	root->dn = dn;
	root->data = data;
	root->next = NULL;
	slapi_unlock_mutex(cache_lock);
	return root;
}

char *cacheRemove( DNLink *root, char *dn ) {
	char *found = NULL;
	DNLink *current = root;
	DNLink *prev = NULL;
	if ( NULL == root ) {
		return NULL;
	}
	slapi_lock_mutex(cache_lock);
	for( ; current; prev = current, current = current->next ) {
		if ( !strcmp( current->dn, dn ) ) {
			found = current->dn;
			prev->next = current->next;
			slapi_ch_free( (void **)&current );
			break;
		}
	}
	slapi_unlock_mutex(cache_lock);
	return found;
}

int cacheDelete( DNLink *root, char *dn ) {
	char *found = cacheRemove( root, dn );
	if ( found ) {
		slapi_ch_free( (void **)&found );
		return 0;
	} else {
		return 1;
	}
}

DNLink *cacheFind( DNLink *root, char *dn ) {
	if ( NULL == root ) {
		return NULL;
	}
	slapi_lock_mutex(cache_lock);
	for( ; root && strcmp(dn, root->dn); root = root->next ) {
	}
	slapi_unlock_mutex(cache_lock);
	return root;
}
