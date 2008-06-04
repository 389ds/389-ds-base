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

/* attrsyntax.c */

#include "slap.h"
#include <plhash.h>

/*
 * Note: if both the oid2asi and name2asi locks are acquired at the
 * same time, the old2asi one should be acquired first,
 */

/*
 * This hashtable maps the oid to the struct asyntaxinfo for that oid.
 */
static PLHashTable *oid2asi = NULL;
/* read/write lock to protect table */
static PRRWLock *oid2asi_lock = NULL;

/*
 * This hashtable maps the name or alias of the attribute to the
 * syntax info structure for that attribute.  An attribute type has as
 * many entries in the name2asi table as it has names and aliases, but
 * all entries point to the same struct asyntaxinfo.
 */
static PLHashTable *name2asi = NULL;
/* read/write lock to protect table */
static PRRWLock *name2asi_lock = NULL;

#define AS_LOCK_READ(l)		PR_RWLock_Rlock(l)
#define AS_LOCK_WRITE(l)	PR_RWLock_Wlock(l)
#define AS_UNLOCK_READ(l)	PR_RWLock_Unlock(l)
#define AS_UNLOCK_WRITE(l)	PR_RWLock_Unlock(l)



static void *attr_syntax_get_plugin_by_name_with_default( const char *type );
static void attr_syntax_delete_no_lock( struct asyntaxinfo *asip,
		PRBool remove_from_oid_table );
static struct asyntaxinfo *attr_syntax_get_by_oid_locking_optional( const
		char *oid, PRBool use_lock, PRBool ref_count);

#ifdef ATTR_LDAP_DEBUG
static void attr_syntax_print();
#endif
static int attr_syntax_init(void);

void
attr_syntax_read_lock(void)
{
	if (0 != attr_syntax_init()) return;

	AS_LOCK_READ(oid2asi_lock);
	AS_LOCK_READ(name2asi_lock);
}

void
attr_syntax_unlock_read(void)
{
	if(name2asi_lock) AS_UNLOCK_READ(name2asi_lock);
	if(oid2asi_lock) AS_UNLOCK_READ(oid2asi_lock);
}



#if 0
static int 
check_oid( const char *oid ) {

  int i = 0, length_oid = 0, rc = 0;

  if ( oid == NULL) {
	/* this is bad */
	LDAPDebug (LDAP_DEBUG_ANY, "NULL passed to check_oid\n",0,0,0);
	return 0;
  }
  
  length_oid = strlen(oid);
  if (length_oid < 4) {
	/* this is probably bad */
	LDAPDebug (LDAP_DEBUG_ANY, "Bad oid %s passed to check_oid\n",oid,0,0);
	return 0;
  }

  rc = strcasecmp(oid+(length_oid-4), "-oid");

  if ( 0 == rc ) {
	return 1;
  }

  /* If not, the OID must begin and end with a digit, and contain only 
	 digits and dots */
  
  if ( !isdigit(oid[0]) || 
	   !isdigit(oid[length_oid-1]) ) {
	LDAPDebug (LDAP_DEBUG_ANY, "Non numeric oid %s passed to check_oid\n",oid,0,0);
	return 0;
  }

  /* check to see that it contains only digits and dots */
  for ( i = 0; i < length_oid; i++ ) {
	if ( !isdigit(oid[i]) && oid[i] != '.'  ){
	  LDAPDebug (LDAP_DEBUG_ANY, "Non numeric oid %s passed to check_oid\n",oid,0,0);
	  return 0;
	}
  }

  /* The oid is OK if we're here */
  return 1;
  

}
#endif

#define NBUCKETS(ht)    (1 << (PL_HASH_BITS - (ht)->shift))

#if 0
static int
attr_syntax_check_oids()
{
	int ii = 0;
	int nbad = 0;
	AS_LOCK_READ(oid2asi_lock);
	ii = NBUCKETS(oid2asi);
    for (;ii;--ii) {
		PLHashEntry *he = oid2asi->buckets[ii-1];
		for (; he; he = he->next) {
			if (!check_oid(he->key)) {
				LDAPDebug(LDAP_DEBUG_ANY, "Error: bad oid %s in bucket %d\n",
						  he->key, ii-1, 0);
				nbad++;
			}
		}
    }

	AS_UNLOCK_READ(oid2asi_lock);
	return nbad;
}
#endif

void
attr_syntax_free( struct asyntaxinfo *a )
{
	PR_ASSERT( a->asi_refcnt == 0 );

	cool_charray_free( a->asi_aliases );
	slapi_ch_free( (void**)&a->asi_name );
	slapi_ch_free( (void **)&a->asi_desc );
	slapi_ch_free( (void **)&a->asi_oid );
	slapi_ch_free( (void **)&a->asi_superior );
	slapi_ch_free( (void **)&a->asi_mr_equality );
	slapi_ch_free( (void **)&a->asi_mr_ordering );
	slapi_ch_free( (void **)&a->asi_mr_substring );
	cool_charray_free( a->asi_origin );
	slapi_ch_free( (void **) &a );
}

static struct asyntaxinfo *
attr_syntax_new()
{
	return (struct asyntaxinfo *)slapi_ch_calloc(1, sizeof(struct asyntaxinfo));
}

/*
 * hashNocaseString - used for case insensitive hash lookups
 */
static PLHashNumber
hashNocaseString(const void *key)
{
    PLHashNumber h = 0;
    const unsigned char *s;
 
    for (s = key; *s; s++)
        h = (h >> 28) ^ (h << 4) ^ (tolower(*s));
    return h;
}

/*
 * hashNocaseCompare - used for case insensitive hash key comparisons
 */
static PRIntn
hashNocaseCompare(const void *v1, const void *v2)
{
	return (strcasecmp((char *)v1, (char *)v2) == 0);
}

/*
 * Given an OID, return the syntax info.  If there is more than one
 * attribute syntax with the same OID (i.e. aliases), the first one
 * will be returned.  This is usually the "canonical" one, but it may
 * not be.
 *
 * Note: once the caller is finished using it, the structure returned must
 * be returned by calling to attr_syntax_return().
 */
struct asyntaxinfo *
attr_syntax_get_by_oid(const char *oid)
{
	return attr_syntax_get_by_oid_locking_optional( oid, PR_TRUE, PR_TRUE);
}


static struct asyntaxinfo *
attr_syntax_get_by_oid_locking_optional( const char *oid, PRBool use_lock, PRBool ref_count )
{
	struct asyntaxinfo *asi = 0;
	if (oid2asi)
	{
		if ( use_lock ) AS_LOCK_READ(oid2asi_lock);
		asi = (struct asyntaxinfo *)PL_HashTableLookup_const(oid2asi, oid);
		if (asi)
		{
			if(ref_count) PR_AtomicIncrement( &asi->asi_refcnt );
		}
		if ( use_lock ) AS_UNLOCK_READ(oid2asi_lock);
	}

	return asi;
}

/*
 * Add the syntax info pointer to the look-up-by-oid table.
 * The lock parameter is used by the initialization code.  Normally, we want
 * to acquire a write lock before we modify the table, but during
 * initialization, we are running in single threaded mode, so we don't have
 * to worry about resource contention.
 */
static void
attr_syntax_add_by_oid(const char *oid, struct asyntaxinfo *a, int lock)
{
	if (0 != attr_syntax_init()) return;

	if (lock)
		AS_LOCK_WRITE(oid2asi_lock);

	PL_HashTableAdd(oid2asi, oid, a);

	if (lock)
		AS_UNLOCK_WRITE(oid2asi_lock);
}

/*
 * Return the syntax info given an attribute name.  The name may be the
 * "canonical" name, an alias, or an OID.  The given name need not be
 * normalized since the look up is done case insensitively.
 *
 * Note: once the caller is finished using it, the structure returned must
 * be returned by calling to attr_syntax_return().
 */
struct asyntaxinfo *
attr_syntax_get_by_name(const char *name)
{
	return attr_syntax_get_by_name_locking_optional(name, PR_TRUE, PR_TRUE);
}


struct asyntaxinfo *
attr_syntax_get_by_name_locking_optional(const char *name, PRBool use_lock, PRBool ref_count)
{
	struct asyntaxinfo *asi = 0;
	if (name2asi)
	{
		if ( use_lock ) AS_LOCK_READ(name2asi_lock);
		asi = (struct asyntaxinfo *)PL_HashTableLookup_const(name2asi, name);
		if ( NULL != asi ) {
			if(ref_count) PR_AtomicIncrement( &asi->asi_refcnt );
		}
		if ( use_lock ) AS_UNLOCK_READ(name2asi_lock);
	}
	if (!asi) /* given name may be an OID */
		asi = attr_syntax_get_by_oid_locking_optional(name, use_lock, ref_count);

	return asi;
}


/*
 * Give up a reference to an asi.
 * If the asi has been marked for delete, free it.  This would be a bit
 * easier if we could upgrade a read lock to a write one... but NSPR does
 * not support that paradigm.
 */
void
attr_syntax_return( struct asyntaxinfo *asi )
{
	attr_syntax_return_locking_optional( asi, PR_TRUE );
}

void
attr_syntax_return_locking_optional( struct asyntaxinfo *asi, PRBool use_lock )
{
	if ( NULL != asi ) {
		if ( 0 == PR_AtomicDecrement( &asi->asi_refcnt ))
		{
			PRBool		delete_it;

			if(use_lock) AS_LOCK_READ(name2asi_lock);
			delete_it = asi->asi_marked_for_delete;
			if(use_lock) AS_UNLOCK_READ(name2asi_lock);

			if ( delete_it )
			{
				AS_LOCK_WRITE(name2asi_lock);		/* get a write lock */
				if ( asi->asi_marked_for_delete )	/* one final check */
				{
					attr_syntax_free(asi);
				}
				AS_UNLOCK_WRITE(name2asi_lock);
			}
		}
	}
}

/*
 * Add the syntax info to the look-up-by-name table.  The asi_name and
 * elements of the asi_aliasses field of the syntax info are the keys.
 * These need not be normalized since the look up table is case insensitive.
 * The lock parameter is used by the initialization code.  Normally, we want
 * to acquire a write lock before we modify the table, but during
 * initialization, we are running in single threaded mode, so we don't have
 * to worry about resource contention.
 */
static void
attr_syntax_add_by_name(struct asyntaxinfo *a, int lock)
{
	if (0 != attr_syntax_init()) return;

	if (lock)
		AS_LOCK_WRITE(name2asi_lock);

	PL_HashTableAdd(name2asi, a->asi_name, a);
	if ( a->asi_aliases != NULL ) {
		int		i;

		for ( i = 0; a->asi_aliases[i] != NULL; ++i ) {
			PL_HashTableAdd(name2asi, a->asi_aliases[i], a);
		}
	}

	if (lock)
		AS_UNLOCK_WRITE(name2asi_lock);
}


/*
 * Delete the attribute syntax and all entries corresponding to aliases
 * and oids.
 */
void
attr_syntax_delete( struct asyntaxinfo *asi )
{
	PR_ASSERT( asi );

	if (oid2asi && name2asi) {
		AS_LOCK_WRITE(oid2asi_lock);
		AS_LOCK_WRITE(name2asi_lock);

		attr_syntax_delete_no_lock( asi, PR_TRUE );

		AS_UNLOCK_WRITE(name2asi_lock);
		AS_UNLOCK_WRITE(oid2asi_lock);
	}
}


/*
 * Dispose of a node.  The caller is responsible for locking.  See
 * attr_syntax_delete() for an example.
 */
static void
attr_syntax_delete_no_lock( struct asyntaxinfo *asi,
		PRBool remove_from_oidtable )
{
	int		i;

	if (oid2asi && remove_from_oidtable ) {
		PL_HashTableRemove(oid2asi, asi->asi_oid);
	}

	if(name2asi) {
		PL_HashTableRemove(name2asi, asi->asi_name);
		if ( asi->asi_aliases != NULL ) {
			for ( i = 0; asi->asi_aliases[i] != NULL; ++i ) {
				PL_HashTableRemove(name2asi, asi->asi_aliases[i]);
			}
		}
		if ( asi->asi_refcnt > 0 ) {
			asi->asi_marked_for_delete = PR_TRUE;
		} else {
			attr_syntax_free(asi);
		}
	}
}


/*
 * Look up the attribute type in the syntaxes and return a copy of the
 * normalised attribute type. If it's not there then return a normalised
 * copy of what the caller gave us.
 *
 * Warning: The caller must free the returned string.
 */



char *
slapi_attr_syntax_normalize( const char *s )
{
	struct asyntaxinfo *asi = NULL;
	char *r;
	

    if((asi=attr_syntax_get_by_name_locking_optional(s, PR_TRUE, PR_FALSE)) != NULL ) {
		r = slapi_ch_strdup(asi->asi_name);
		attr_syntax_return( asi );
	}
	if ( NULL == asi ) {
		r = attr_syntax_normalize_no_lookup( s );
	}
	return r;
}


/*
 * attr_syntax_exists: return 1 if attr_name exists, 0 otherwise
 *
 */
int
attr_syntax_exists(const char *attr_name)
{
	struct asyntaxinfo	*asi;

	asi = attr_syntax_get_by_name(attr_name);
	attr_syntax_return( asi );

	if ( asi != NULL )
	{
		return 1;
	}
	return 0;
}

/* check syntax without incrementing refcount -- handles locking itself */

static void *
attr_syntax_get_plugin_by_name_with_default( const char *type )
{
	struct asyntaxinfo	*asi;
	void				*plugin = NULL;

	/*
	 * first we look for this attribute type explictly
	 */
	if ( (asi = attr_syntax_get_by_name_locking_optional(type, PR_TRUE, PR_FALSE)) == NULL ) {
		/*
		 * no syntax for this type... return DirectoryString
		 * syntax.  we accomplish this by looking up a well known
		 * attribute type that has that syntax.
		 */
		asi = attr_syntax_get_by_name_locking_optional(
				ATTR_WITH_DIRSTRING_SYNTAX, PR_TRUE, PR_FALSE);
	}
	if ( NULL != asi ) {
		plugin = asi->asi_plugin;
		attr_syntax_return( asi );
	}
	return( plugin );
}


static struct asyntaxinfo *
attr_syntax_dup( struct asyntaxinfo *a )
{
	struct asyntaxinfo *newas = attr_syntax_new();

	newas->asi_aliases = cool_charray_dup( a->asi_aliases );
	newas->asi_name = slapi_ch_strdup( a->asi_name );
	newas->asi_desc = slapi_ch_strdup( a->asi_desc );
	newas->asi_superior = slapi_ch_strdup( a->asi_superior );
	newas->asi_mr_equality = slapi_ch_strdup( a->asi_mr_equality );
	newas->asi_mr_ordering = slapi_ch_strdup( a->asi_mr_ordering );
	newas->asi_mr_substring = slapi_ch_strdup( a->asi_mr_substring );
	newas->asi_origin = cool_charray_dup( a->asi_origin );
	newas->asi_plugin = a->asi_plugin;
	newas->asi_flags = a->asi_flags;
	newas->asi_oid = slapi_ch_strdup( a->asi_oid);
	newas->asi_syntaxlength = a->asi_syntaxlength;

	return( newas );
}


/*
 * Add a new attribute type to the schema.
 *
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well).
 */
int 
attr_syntax_add( struct asyntaxinfo *asip )
{
	int i, rc = LDAP_SUCCESS;
	int nolock = asip->asi_flags & SLAPI_ATTR_FLAG_NOLOCKING;
	struct asyntaxinfo *oldas_from_oid = NULL, *oldas_from_name = NULL;
	/* attr names may have subtypes in them, and we may not want this
	   if strip_subtypes is true, the ; and anything after it in the
	   attr name or alias will be stripped */
	/*int strip_subtypes = 1;*/

	/* make sure the oid is unique */
	if ( NULL != ( oldas_from_oid = attr_syntax_get_by_oid_locking_optional(
					asip->asi_oid, !nolock, PR_TRUE))) {
		if ( 0 == (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE)) {
			/* failure - OID is in use; no override flag */
			rc = LDAP_TYPE_OR_VALUE_EXISTS;
			goto cleanup_and_return;
		}
	}

	/* make sure the primary name is unique OR, if override is allowed, that
     * the primary name and OID point to the same schema definition.
	 */
	if ( NULL != ( oldas_from_name = attr_syntax_get_by_name_locking_optional(
					asip->asi_name, !nolock, PR_TRUE))) {
		if ( 0 == (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE)
					|| ( oldas_from_oid != oldas_from_name )) {
			/* failure; no override flag OR OID and name don't match */
			rc = LDAP_TYPE_OR_VALUE_EXISTS;
			goto cleanup_and_return;
		}
		attr_syntax_delete(oldas_from_name);
	} else if ( NULL != oldas_from_oid ) {
		/* failure - OID is in use but name does not exist */
		rc = LDAP_TYPE_OR_VALUE_EXISTS;
		goto cleanup_and_return;
	}

	if ( NULL != asip->asi_aliases ) {
		/* make sure the aliases are unique */
		for (i = 0; asip->asi_aliases[i] != NULL; ++i) {
			struct asyntaxinfo	*tmpasi;

			if ( NULL != ( tmpasi =
							attr_syntax_get_by_name_locking_optional(
							asip->asi_aliases[i], !nolock,PR_TRUE))) {
				if (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE) {
					attr_syntax_delete(tmpasi);
				} else {
					/* failure - one of the aliases is already in use */
					rc = LDAP_TYPE_OR_VALUE_EXISTS;
				}

				attr_syntax_return( tmpasi );
				if ( LDAP_SUCCESS != rc ) {
					goto cleanup_and_return;
				}
			}
		}
	}

	/* the no lock flag is not worth keeping around */
	asip->asi_flags &= ~SLAPI_ATTR_FLAG_NOLOCKING;
	/* ditto for the override one */
	asip->asi_flags &= ~SLAPI_ATTR_FLAG_OVERRIDE;
	
	attr_syntax_add_by_oid( asip->asi_oid, asip, !nolock);
	attr_syntax_add_by_name( asip, !nolock);

cleanup_and_return:
	attr_syntax_return( oldas_from_oid );
	attr_syntax_return( oldas_from_name );
	return rc;
}


/*
 * Returns an LDAP result code.
 */
int
attr_syntax_create(
	const char			*attr_oid,
	char *const			*attr_names,
	int					num_names,
	const char			*attr_desc,
	const char			*attr_superior,
	const char			*mr_equality,
	const char			*mr_ordering,
	const char			*mr_substring,
	char *const			*attr_origins,
	const char			*attr_syntax,
	int					syntaxlength,
	unsigned long		flags,
	struct asyntaxinfo	**asip
)
{
	char					*s;
	struct asyntaxinfo		a;

	/* XXXmcs: had to cast away const in many places below */
	memset(&a, 0, sizeof(a));
	a.asi_name = slapi_ch_strdup(attr_names[0]);
	if ( NULL != attr_names[1] ) {
		a.asi_aliases = (char **)&attr_names[1]; /* all but the zero'th element */
	}
	a.asi_desc = (char*)attr_desc;
	a.asi_oid = (char*)attr_oid;
	a.asi_superior = (char*)attr_superior;
	a.asi_mr_equality = (char*)mr_equality;
	a.asi_mr_ordering = (char*)mr_ordering;
	a.asi_mr_substring = (char*)mr_substring;
	a.asi_origin = (char **)attr_origins;
	a.asi_plugin = plugin_syntax_find( attr_syntax );
	a.asi_syntaxlength = syntaxlength;
	a.asi_flags = flags;

	/*
	 * If the 'return exact case' option is on (the default), we store the
	 * first name (the canonical one) unchanged so that attribute names are
	 * returned exactly as they appear in the schema configuration files.
	 * But if 'return exact case' has been turned off, we convert the name
	 * to lowercase.  In Netscape Directory Server 4.x and earlier versions,
	 * the default was to convert to lowercase.
	 */
	if (!config_get_return_exact_case()) {
		for (s = a.asi_name; *s; ++s) {
			*s = TOLOWER(*s);
		}
	}

	*asip = attr_syntax_dup(&a);
	slapi_ch_free((void **)&a.asi_name);

	return LDAP_SUCCESS;
}


/*
 * slapi_attr_type2plugin - return the plugin handling the attribute type
 * if type is unknown, we return the caseIgnoreString plugin used by the
 *     objectClass attribute type.
 */

int
slapi_attr_type2plugin( const char *type, void **pi )
{
	char			buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
	char			*tmp, *basetype;
	int				rc;

	basetype = buf;
	if ( (tmp = slapi_attr_basetype( type, buf, sizeof(buf) )) != NULL ) {
		basetype = tmp;
	}
	rc = -1;
	*pi = attr_syntax_get_plugin_by_name_with_default( basetype );
	if ( NULL != *pi ) {
		rc = 0;
	}
	if ( tmp != NULL ) {
		free( tmp );
	}

	return( rc );
}

/* deprecated -- not MT safe (pointer into asi is returned!) */
int
slapi_attr_get_oid( const Slapi_Attr *a, char **oid )
{
	struct asyntaxinfo *asi = attr_syntax_get_by_name(a->a_type);
	if (asi) {
		*oid = asi->asi_oid;
		attr_syntax_return(asi);
		return( 0 );
	} else {
		*oid = NULL;
		return( -1 );
	}
}


/* The caller must dispose of oid by calling slapi_ch_free(). */
int
slapi_attr_get_oid_copy( const Slapi_Attr *a, char **oidp )
{
	struct asyntaxinfo *asi = attr_syntax_get_by_name(a->a_type);
	if (asi) {
		*oidp = slapi_ch_strdup( asi->asi_oid );
		attr_syntax_return(asi);
		return( 0 );
	} else {
		*oidp = NULL;
		return( -1 );
	}
}

/* Returns the oid of the syntax of the Slapi_Attr that's passed in.
 * The caller must dispose of oid by calling slapi_ch_free_string(). */
int
slapi_attr_get_syntax_oid_copy( const Slapi_Attr *a, char **oidp )
{
	void *pi = NULL;

	if (a && (slapi_attr_type2plugin(a->a_type, &pi) == 0)) {
		*oidp = slapi_ch_strdup(plugin_syntax2oid(pi));
		return( 0 );
	} else {
		*oidp = NULL;
		return( -1 );
	}
}

#ifdef ATTR_LDAP_DEBUG

PRIntn
attr_syntax_printnode(PLHashEntry *he, PRIntn i, void *arg)
{
	char *alias = (char *)he->key;
	struct asyntaxinfo *a = (struct asyntaxinfo *)he->value;
	printf( "  name: %s\n", a->asi_name );
	printf( "\t flags       : 0x%x\n", a->asi_flags );
	printf( "\t alias       : %s\n", alias );
	printf( "\t desc        : %s\n", a->asi_desc );
	printf( "\t oid         : %s\n", a->asi_oid );
	printf( "\t superior    : %s\n", a->asi_superior );
	printf( "\t mr_equality : %s\n", a->asi_mr_equality );
	printf( "\t mr_ordering : %s\n", a->asi_mr_ordering );
	printf( "\t mr_substring: %s\n", a->asi_mr_substring );
	if ( NULL != a->asi_origin ) {
		for ( i = 0; NULL != a->asi_origin[i]; ++i ) {
			printf( "\t origin      : %s\n", a->asi_origin[i] );
		}
	}
	printf( "\tplugin: %p\n", a->asi_plugin );
	printf( "--------------\n" );

	return HT_ENUMERATE_NEXT;
}

void
attr_syntax_print()
{
	printf( "*** attr_syntax_print ***\n" );
	PL_HashTableEnumerateEntries(name2asi, attr_syntax_printnode, 0);
}

#endif


/* lowercase the attr name and chop trailing spaces */
/* note that s may contain options also, e.g., userCertificate;binary */
char *
attr_syntax_normalize_no_lookup( const char *s )
{
	char	*save, *tmps;

    tmps = slapi_ch_strdup(s);
    for ( save = tmps; (*tmps != '\0') && (*tmps != ' '); tmps++ )
	{
	  *tmps = TOLOWER( *tmps );
	}
	*tmps = '\0';

	return save;
}

struct enum_arg_wrapper {
	AttrEnumFunc aef;
	void *arg;
};

PRIntn
attr_syntax_enumerate_internal(PLHashEntry *he, PRIntn i, void *arg)
{
	struct enum_arg_wrapper *eaw = (struct enum_arg_wrapper *)arg;
	int	rc;

	rc = (eaw->aef)((struct asyntaxinfo *)he->value, eaw->arg);
	if ( ATTR_SYNTAX_ENUM_STOP == rc ) {
		rc = HT_ENUMERATE_STOP;
	} else if ( ATTR_SYNTAX_ENUM_REMOVE == rc ) {
		rc = HT_ENUMERATE_REMOVE;
	} else {
		rc = HT_ENUMERATE_NEXT;
	}

	return rc;
}

void
attr_syntax_enumerate_attrs(AttrEnumFunc aef, void *arg, PRBool writelock )
{
	struct enum_arg_wrapper eaw;
	eaw.aef = aef;
	eaw.arg = arg;

	if (!oid2asi)
		return;

	if ( writelock ) {
		AS_LOCK_WRITE(oid2asi_lock);
		AS_LOCK_WRITE(name2asi_lock);
	} else {
		AS_LOCK_READ(oid2asi_lock);
		AS_LOCK_READ(name2asi_lock);
	}

	PL_HashTableEnumerateEntries(oid2asi, attr_syntax_enumerate_internal, &eaw);

	if ( writelock ) {
		AS_UNLOCK_WRITE(oid2asi_lock);
		AS_UNLOCK_WRITE(name2asi_lock);
	} else {
		AS_UNLOCK_READ(oid2asi_lock);
		AS_UNLOCK_READ(name2asi_lock);
	}
}


struct attr_syntax_enum_flaginfo {
	unsigned long	asef_flag;
};

static int
attr_syntax_clear_flag_callback(struct asyntaxinfo *asip, void *arg)
{
	struct attr_syntax_enum_flaginfo	*fi;

	PR_ASSERT( asip != NULL );
	fi = (struct attr_syntax_enum_flaginfo *)arg;
	PR_ASSERT( fi != NULL );

	asip->asi_flags &= ~(fi->asef_flag);

	return ATTR_SYNTAX_ENUM_NEXT;
}


static int
attr_syntax_delete_if_not_flagged(struct asyntaxinfo *asip, void *arg)
{
	struct attr_syntax_enum_flaginfo	*fi;

	PR_ASSERT( asip != NULL );
	fi = (struct attr_syntax_enum_flaginfo *)arg;
	PR_ASSERT( fi != NULL );

	if ( 0 == ( asip->asi_flags & fi->asef_flag )) {
		attr_syntax_delete_no_lock( asip, PR_FALSE );
		return ATTR_SYNTAX_ENUM_REMOVE;
	} else {
		return ATTR_SYNTAX_ENUM_NEXT;
	}
}

static int
attr_syntax_force_to_delete(struct asyntaxinfo *asip, void *arg)
{
	struct attr_syntax_enum_flaginfo	*fi;

	PR_ASSERT( asip != NULL );
	fi = (struct attr_syntax_enum_flaginfo *)arg;
	PR_ASSERT( fi != NULL );

	attr_syntax_delete_no_lock( asip, PR_FALSE );
	return ATTR_SYNTAX_ENUM_REMOVE;
}


/*
 * Clear 'flag' within all attribute definitions.
 */
void
attr_syntax_all_clear_flag( unsigned long flag )
{
	struct attr_syntax_enum_flaginfo fi;

	memset( &fi, 0, sizeof(fi));
	fi.asef_flag = flag;
	attr_syntax_enumerate_attrs( attr_syntax_clear_flag_callback,
				(void *)&fi, PR_TRUE );
}


/*
 * Delete all attribute definitions that do not contain any bits of 'flag'
 * in their flags.
 */
void
attr_syntax_delete_all_not_flagged( unsigned long flag )
{
	struct attr_syntax_enum_flaginfo fi;

	memset( &fi, 0, sizeof(fi));
	fi.asef_flag = flag;
	attr_syntax_enumerate_attrs( attr_syntax_delete_if_not_flagged,
				(void *)&fi, PR_TRUE );
}

/*
 * Delete all attribute definitions 
 */
void
attr_syntax_delete_all()
{
	struct attr_syntax_enum_flaginfo fi;

	memset( &fi, 0, sizeof(fi));
	attr_syntax_enumerate_attrs( attr_syntax_force_to_delete,
				(void *)&fi, PR_TRUE );
}

static int
attr_syntax_init(void)
{
	if (!oid2asi)
	{
		oid2asi = PL_NewHashTable(2047, hashNocaseString,
								  hashNocaseCompare,
								  PL_CompareValues, 0, 0);
		if ( NULL == ( oid2asi_lock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,
				"attrsyntax oid rwlock" ))) {
			if(oid2asi) PL_HashTableDestroy(oid2asi);
			oid2asi = NULL;

			slapi_log_error( SLAPI_LOG_FATAL, "attr_syntax_init",
					"PR_NewRWLock() for oid2asi lock failed\n" );
			return 1;
		}
	}

	if (!name2asi)
	{
		name2asi = PL_NewHashTable(2047, hashNocaseString,
								   hashNocaseCompare,
								   PL_CompareValues, 0, 0);
		if ( NULL == ( name2asi_lock = PR_NewRWLock( PR_RWLOCK_RANK_NONE,
				"attrsyntax name2asi rwlock"))) {
			if(name2asi) PL_HashTableDestroy(name2asi);
			name2asi = NULL;

			slapi_log_error( SLAPI_LOG_FATAL, "attr_syntax_init",
					"PR_NewRWLock() for oid2asi lock failed\n" );
			return 1;
		}
	}
	return 0;
}
