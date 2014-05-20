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
static Slapi_RWLock *oid2asi_lock = NULL;
static PLHashTable *internalasi = NULL;

/* global attribute linked list */
static asyntaxinfo *global_at = NULL;

/*
 * This hashtable maps the name or alias of the attribute to the
 * syntax info structure for that attribute.  An attribute type has as
 * many entries in the name2asi table as it has names and aliases, but
 * all entries point to the same struct asyntaxinfo.
 */
static PLHashTable *name2asi = NULL;
/* read/write lock to protect table */
static Slapi_RWLock *name2asi_lock = NULL;
static int asi_locking = 1;

#define AS_LOCK_READ(l)		if (asi_locking) { slapi_rwlock_rdlock(l); }
#define AS_LOCK_WRITE(l)	if (asi_locking) { slapi_rwlock_wrlock(l); }
#define AS_UNLOCK_READ(l)	if (asi_locking) { slapi_rwlock_unlock(l); }
#define AS_UNLOCK_WRITE(l)	if (asi_locking) { slapi_rwlock_unlock(l); }


static struct asyntaxinfo *default_asi = NULL;

static void *attr_syntax_get_plugin_by_name_with_default( const char *type );
static void attr_syntax_delete_no_lock( struct asyntaxinfo *asip,
		PRBool remove_from_oid_table );
static struct asyntaxinfo *attr_syntax_get_by_oid_locking_optional( const
		char *oid, PRBool use_lock);
static void attr_syntax_insert( struct asyntaxinfo *asip );
static void attr_syntax_remove( struct asyntaxinfo *asip );

#ifdef ATTR_LDAP_DEBUG
static void attr_syntax_print();
#endif
static int attr_syntax_init(void);

struct asyntaxinfo *
attr_syntax_get_global_at()
{
	return global_at;
}

void
attr_syntax_read_lock(void)
{
	if (0 != attr_syntax_init()) return;

	AS_LOCK_READ(oid2asi_lock);
	AS_LOCK_READ(name2asi_lock);
}

void
attr_syntax_write_lock(void)
{
	if (0 != attr_syntax_init()) return;

	AS_LOCK_WRITE(oid2asi_lock);
	AS_LOCK_WRITE(name2asi_lock);
}

void
attr_syntax_unlock_read(void)
{
	AS_UNLOCK_READ(name2asi_lock);
	AS_UNLOCK_READ(oid2asi_lock);
}

void
attr_syntax_unlock_write(void)
{
	AS_UNLOCK_WRITE(name2asi_lock);
	AS_UNLOCK_WRITE(oid2asi_lock);
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
	slapi_ch_free_string(&a->asi_name );
	slapi_ch_free_string(&a->asi_desc );
	slapi_ch_free_string(&a->asi_oid );
	slapi_ch_free_string(&a->asi_superior );
	slapi_ch_free_string(&a->asi_mr_equality );
	slapi_ch_free_string(&a->asi_mr_ordering );
	slapi_ch_free_string(&a->asi_mr_substring );
	slapi_ch_free_string(&a->asi_syntax_oid);
	schema_free_extensions(a->asi_extensions);
	slapi_ch_free( (void **) &a );
}

static struct asyntaxinfo *
attr_syntax_new()
{
	return (struct asyntaxinfo *)slapi_ch_calloc(1, sizeof(struct asyntaxinfo));
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
	return attr_syntax_get_by_oid_locking_optional( oid, PR_TRUE);
}


/*
 * A version of attr_syntax_get_by_oid() that allows you to bypass using
 * a lock to access the global oid hash table.
 *
 * Note: once the caller is finished using it, the structure must be
 * returned by calling attr_syntax_return_locking_optional() with the
 * same use_lock parameter.
 */
static struct asyntaxinfo *
attr_syntax_get_by_oid_locking_optional( const char *oid, PRBool use_lock )
{
	struct asyntaxinfo *asi = 0;
	if (oid2asi)
	{
		if ( use_lock ) {
			AS_LOCK_READ(oid2asi_lock);
		}
		asi = (struct asyntaxinfo *)PL_HashTableLookup_const(oid2asi, oid);
		if (asi)
		{
			PR_AtomicIncrement( &asi->asi_refcnt );
		}
		if ( use_lock ) {
			AS_UNLOCK_READ(oid2asi_lock);
		}
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

	if (lock) {
		AS_LOCK_WRITE(oid2asi_lock);
	}

	PL_HashTableAdd(oid2asi, oid, a);

	if (lock) {
		AS_UNLOCK_WRITE(oid2asi_lock);
	}
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
	return attr_syntax_get_by_name_locking_optional(name, PR_TRUE);
}

struct asyntaxinfo *
attr_syntax_get_by_name_with_default(const char *name)
{
struct asyntaxinfo *asi = NULL;
	asi = attr_syntax_get_by_name_locking_optional(name, PR_TRUE);
	if (asi == NULL)
		asi = attr_syntax_get_by_name(ATTR_WITH_OCTETSTRING_SYNTAX);
	if ( asi == NULL ) 
		asi = default_asi;
	return asi;
}

/*
 * A version of attr_syntax_get_by_name() that allows you to bypass using
 * a lock around the global name hashtable.
 *
 * Note: once the caller is finished using it, the structure must be
 * returned by calling attr_syntax_return_locking_optional() with the
 * same use_lock parameter.
 */
struct asyntaxinfo *
attr_syntax_get_by_name_locking_optional(const char *name, PRBool use_lock)
{
	struct asyntaxinfo *asi = 0;
	if (name2asi)
	{
		if ( use_lock ) {
			AS_LOCK_READ(name2asi_lock);
		}
		asi = (struct asyntaxinfo *)PL_HashTableLookup_const(name2asi, name);
		if ( NULL != asi ) {
			PR_AtomicIncrement( &asi->asi_refcnt );
		}
		if ( use_lock ) {
			AS_UNLOCK_READ(name2asi_lock);
		}
	}
	if (!asi) /* given name may be an OID */
		asi = attr_syntax_get_by_oid_locking_optional(name, use_lock);

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
attr_syntax_return_locking_optional(struct asyntaxinfo *asi, PRBool use_lock)
{
	int locked = 0;
	if(use_lock) {
		AS_LOCK_READ(name2asi_lock);
		locked = 1;
	}
	if ( NULL != asi ) {
		PRBool		delete_it = PR_FALSE;
		if ( 0 == PR_AtomicDecrement( &asi->asi_refcnt )) {
			delete_it = asi->asi_marked_for_delete;
		}

		if (delete_it) {
			if ( asi->asi_marked_for_delete ) {	/* one final check */
				if(use_lock) {
					AS_UNLOCK_READ(name2asi_lock);
					AS_LOCK_WRITE(name2asi_lock);
				}
				/* ref count is 0 and it's flagged for
				 * deletion, so it's safe to free now */
				attr_syntax_remove(asi);
				attr_syntax_free(asi);
				if(use_lock) {
					AS_UNLOCK_WRITE(name2asi_lock);
					locked = 0;
				}
			}
		}
	}
	if(locked) {
		AS_UNLOCK_READ(name2asi_lock);
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

	if (lock) {
		AS_LOCK_WRITE(name2asi_lock);
	}

	/* insert the attr into the global linked list */
	attr_syntax_insert(a);

	PL_HashTableAdd(name2asi, a->asi_name, a);
	if ( a->asi_aliases != NULL ) {
		int		i;

		for ( i = 0; a->asi_aliases[i] != NULL; ++i ) {
			PL_HashTableAdd(name2asi, a->asi_aliases[i], a);
		}
	}

	if (lock) {
		AS_UNLOCK_WRITE(name2asi_lock);
	}
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
			/* This is ok, but the correct thing is to call delete first, 
			 * then to call return.  The last return will then take care of
			 * the free.  The only way this free would happen here is if
			 * you return the syntax before calling delete. */
			attr_syntax_remove(asi);
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
	char *r = NULL;

	if((asi=attr_syntax_get_by_name(s)) != NULL ) {
		r = slapi_ch_strdup(asi->asi_name);
		attr_syntax_return( asi );
	}
	if ( NULL == asi ) {
		slapi_ch_free_string( &r );
		r = attr_syntax_normalize_no_lookup( s );
	}
	return r;
}

/* 
 * flags: 
 * 0 -- same as slapi_attr_syntax_normalize
 * ATTR_SYNTAX_NORM_ORIG_ATTR -- In addition to slapi_attr_syntax_normalize,
 *                               a space and following characters are removed
 *                               from the given string 's'.
 */
char *
slapi_attr_syntax_normalize_ext( char *s, int flags )
{
	struct asyntaxinfo *asi = NULL;
	char *r = NULL;

	if((asi=attr_syntax_get_by_name(s)) != NULL ) {
		r = slapi_ch_strdup(asi->asi_name);
		attr_syntax_return( asi );
	}
	if ( NULL == asi ) {
		slapi_ch_free_string( &r );
		r = attr_syntax_normalize_no_lookup_ext( s, flags );
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
	char *check_attr_name = NULL;
	char *p = NULL;
	int free_attr = 0;

	/* Ignore any attribute subtypes. */
	if ((p = strchr(attr_name, ';'))) {
		int check_attr_len = p - attr_name + 1;

		check_attr_name = (char *)slapi_ch_malloc(check_attr_len);
		PR_snprintf(check_attr_name, check_attr_len, "%s", attr_name);
		free_attr = 1;
 	} else {
		check_attr_name = (char *)attr_name;
	}

	asi = attr_syntax_get_by_name(check_attr_name);
	attr_syntax_return( asi );

	if (free_attr) {
		slapi_ch_free_string(&check_attr_name);
	}

	if ( asi != NULL )
	{
		return 1;
	}
	return 0;
}

static void default_dirstring_normalize_int(char *s, int trim_spaces);

static
int default_dirstring_filter_ava( struct berval *bvfilter, Slapi_Value **bvals,int ftype, Slapi_Value **retVal )
{
	return(0);
}

static
int default_dirstring_values2keys( Slapi_PBlock *pb, Slapi_Value **bvals,Slapi_Value ***ivals, int ftype )
{
	int		numbvals = 0;
	Slapi_Value	**nbvals, **nbvlp;
	Slapi_Value 	**bvlp;
	char		*c;

	if (NULL == ivals) {
		return 1;
	}
	*ivals = NULL;
	if (NULL == bvals) {
		return 1;
	}
	switch ( ftype ) {
	case LDAP_FILTER_EQUALITY:
		/* allocate a new array for the normalized values */
		for ( bvlp = bvals; bvlp && *bvlp; bvlp++ ) {
			numbvals++;
		}
		nbvals = (Slapi_Value **) slapi_ch_calloc( (numbvals + 1), sizeof(Slapi_Value *));

		for ( bvlp = bvals, nbvlp = nbvals; bvlp && *bvlp; bvlp++, nbvlp++ ) {
			c = slapi_ch_strdup(slapi_value_get_string(*bvlp));
			default_dirstring_normalize_int( c, 1 );
			*nbvlp = slapi_value_new_string_passin(c);
			c = NULL;
		}
		*ivals = nbvals;
		break;

	case LDAP_FILTER_APPROX:
	case LDAP_FILTER_SUBSTRINGS:
	default:
		/* default plugin only handles equality so far */
		LDAPDebug( LDAP_DEBUG_ANY,
		    "default_dirstring_values2keys: unsupported ftype 0x%x\n",
		    ftype, 0, 0 );
		break;
	}
	return(0);
}

static
int default_dirstring_assertion2keys_ava(Slapi_PBlock *pb,Slapi_Value *val,Slapi_Value ***ivals,int ftype  )
{
	return(0);
}

static
int default_dirstring_cmp(struct berval	*v1,struct berval *v2, int normalize)
{
	return(0);
}

static
void default_dirstring_normalize(Slapi_PBlock *pb, char *s, int trim_spaces, char **alt)
{
	default_dirstring_normalize_int(s, trim_spaces);
}

static
void default_dirstring_normalize_int(char *s, int trim_spaces)
{
	char *head = s;
	char *d;
	int  prevspace, curspace;

	if (NULL == s) {
		return;
	}
	d = s;
	if (trim_spaces) {
		/* strip leading blanks */
		while (ldap_utf8isspace(s)) {
			LDAP_UTF8INC(s);
		}
	}

	/* handle value of all spaces - turn into single space */
	if ( *s == '\0' && s != d ) {
		*d++ = ' ';
		*d = '\0';
		return;
	}
	prevspace = 0;
	while ( *s ) {
		int ssz, dsz;
		curspace = ldap_utf8isspace(s);

		/* compress multiple blanks */
		if ( prevspace && curspace ) {
			LDAP_UTF8INC(s);
			continue;
		}
		prevspace = curspace;
			slapi_utf8ToLower((unsigned char*)s, (unsigned char *)d, &ssz, &dsz);
			s += ssz;
			d += dsz;
	}
	*d = '\0';
	/* strip trailing blanks */
	if (prevspace && trim_spaces) {
		char *nd;

		nd = ldap_utf8prev(d);
		while (nd && nd >= head && ldap_utf8isspace(nd)) {
			d = nd;
			nd = ldap_utf8prev(d);
			*d = '\0';
		}
	}
}

static struct slapdplugin *
attr_syntax_default_plugin ( const char *nameoroid )
{

	struct slapdplugin *pi = NULL;
	/*
	 * create a new plugin structure and
	 * set the plugin function pointers.
	 */
	pi = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));

	pi->plg_dn = slapi_ch_strdup("default plugin for directory string syntax");
	pi->plg_closed = 0;
	pi->plg_syntax_oid = slapi_ch_strdup(nameoroid);


	pi->plg_syntax_filter_ava = (IFP) default_dirstring_filter_ava;
	pi->plg_syntax_values2keys = (IFP) default_dirstring_values2keys;
	pi->plg_syntax_assertion2keys_ava = (IFP) default_dirstring_assertion2keys_ava;
	pi->plg_syntax_compare = (IFP) default_dirstring_cmp;
	pi->plg_syntax_normalize = (VFPV) default_dirstring_normalize;

	return (pi);

}
/* check syntax */

static void *
attr_syntax_get_plugin_by_name_with_default( const char *type )
{
	struct asyntaxinfo	*asi;
	void				*plugin = NULL;

	/*
	 * first we look for this attribute type explictly
	 */
	if ( (asi = attr_syntax_get_by_name(type)) == NULL ) {
		/*
		 * no syntax for this type... return Octet String
		 * syntax.  we accomplish this by looking up a well known
		 * attribute type that has that syntax.
		 */
		asi = attr_syntax_get_by_name(ATTR_WITH_OCTETSTRING_SYNTAX);
		if (asi == NULL) 
			asi = default_asi;
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
	newas->asi_extensions = schema_copy_extensions( a->asi_extensions );
	newas->asi_plugin = a->asi_plugin;
	newas->asi_flags = a->asi_flags;
	newas->asi_oid = slapi_ch_strdup( a->asi_oid);
	newas->asi_syntaxlength = a->asi_syntaxlength;
	newas->asi_mr_eq_plugin = a->asi_mr_eq_plugin;
	newas->asi_mr_ord_plugin = a->asi_mr_ord_plugin;
	newas->asi_mr_sub_plugin = a->asi_mr_sub_plugin;
	newas->asi_syntax_oid = slapi_ch_strdup(a->asi_syntax_oid);
	newas->asi_next = NULL;
	newas->asi_prev = NULL;

	return( newas );
}

static void
attr_syntax_insert(struct asyntaxinfo *asip )
{
    /* Insert at top of list */
    asip->asi_prev = NULL;
    asip->asi_next = global_at;
    if(global_at){
        global_at->asi_prev = asip;
        global_at = asip;
    } else {
        global_at = asip;
    }
}

static void
attr_syntax_remove(struct asyntaxinfo *asip )
{
    struct asyntaxinfo *prev, *next;

    prev = asip->asi_prev;
    next = asip->asi_next;
    if(prev){
        prev->asi_next = next;
        if(next){
            next->asi_prev = prev;
        }
    } else {
        if(next){
            next->asi_prev = NULL;
        }
        global_at = next;
    }
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
					asip->asi_oid, !nolock))) {
		if ( 0 == (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE)) {
			/* failure - OID is in use; no override flag */
			rc = LDAP_TYPE_OR_VALUE_EXISTS;
			goto cleanup_and_return;
		}
	}
	/*
	 * Make sure the primary name is unique OR, if override is allowed, that
	 * the primary name and OID point to the same schema definition.
	 */
	if ( NULL != ( oldas_from_name = attr_syntax_get_by_name_locking_optional(
					asip->asi_name, !nolock))) {
		if ( 0 == (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE)
					|| ( oldas_from_oid != oldas_from_name )) {
			/* failure; no override flag OR OID and name don't match */
			rc = LDAP_TYPE_OR_VALUE_EXISTS;
			goto cleanup_and_return;
		}
		/* Flag for deletion.  We are going to override this attr */
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
							asip->asi_aliases[i], !nolock))) {
				if (asip->asi_flags & SLAPI_ATTR_FLAG_OVERRIDE) {
					/* Flag for tmpasi for deletion.  It will be free'd
					 * when attr_syntax_return is called. */
					attr_syntax_delete(tmpasi);
				} else {
					/* failure - one of the aliases is already in use */
					rc = LDAP_TYPE_OR_VALUE_EXISTS;
				}

				attr_syntax_return_locking_optional( tmpasi, !nolock );
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
	attr_syntax_return_locking_optional( oldas_from_oid, !nolock );
	attr_syntax_return_locking_optional( oldas_from_name, !nolock );
	return rc;
}

static int
attr_syntax_create_default( const char *name, const char *oid,
		const char *syntax, unsigned long extraflags )
{
	int rc = 0;
	char *names[2];
	unsigned long std_flags = SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_OPATTR;

	names[0] = (char *)name;
	names[1] = NULL;

	if (default_asi) 
		return (rc);

	rc = attr_syntax_create( oid, names,
			"internal server defined attribute type",
			 NULL,			/* superior */
			 NULL, NULL, NULL,	/* matching rules */
			 NULL, syntax,
			 SLAPI_SYNTAXLENGTH_NONE,
			 std_flags | extraflags,
			 &default_asi );
	if ( rc == 0 && default_asi->asi_plugin == 0)
		default_asi->asi_plugin = attr_syntax_default_plugin (syntax );
	return (rc);
}

/*
 * Returns an LDAP result code.
 */
int
attr_syntax_create(
	const char		*attr_oid,
	char *const		*attr_names,
	const char		*attr_desc,
	const char		*attr_superior,
	const char		*mr_equality,
	const char		*mr_ordering,
	const char		*mr_substring,
	schemaext		*extensions,
	const char		*attr_syntax,
	int			syntaxlength,
	unsigned long		flags,
	struct asyntaxinfo	**asip
)
{
	char			*s;
	struct asyntaxinfo	a;
	int rc = LDAP_SUCCESS;

	/* XXXmcs: had to cast away const in many places below */
	memset(&a, 0, sizeof(a));
	*asip = NULL;
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
	a.asi_extensions = extensions;
	a.asi_plugin = plugin_syntax_find( attr_syntax );
	a.asi_syntax_oid = (char *)attr_syntax ;
	a.asi_syntaxlength = syntaxlength;
	/* ideally, we would report an error and fail to start if there was some problem
	   with the matching rule - but since this functionality is new, and we might
	   cause havoc if lots of servers failed to start because of bogus schema, we
	   just report an error here - at some point in the future, we should actually
	   report an error and exit, or allow the user to control the behavior - for
	   now, just log an error, and address each case
	*/
	if (mr_equality && !slapi_matchingrule_is_compat(mr_equality, attr_syntax)) {
		slapi_log_error(SLAPI_LOG_FATAL, "attr_syntax_create",
						"Error: the EQUALITY matching rule [%s] is not compatible "
						"with the syntax [%s] for the attribute [%s]\n",
						mr_equality, attr_syntax, attr_names[0]);
#ifdef ENFORCE_MR_SYNTAX_COMPAT
		rc = LDAP_INAPPROPRIATE_MATCHING;
		goto done;
#endif /* ENFORCE_MR_SYNTAX_COMPAT */
	}
	a.asi_mr_eq_plugin = plugin_mr_find( mr_equality );
	if (mr_ordering && !slapi_matchingrule_is_compat(mr_ordering, attr_syntax)) {
		slapi_log_error(SLAPI_LOG_FATAL, "attr_syntax_create",
						"Error: the ORDERING matching rule [%s] is not compatible "
						"with the syntax [%s] for the attribute [%s]\n",
						mr_ordering, attr_syntax, attr_names[0]);
#ifdef ENFORCE_MR_SYNTAX_COMPAT
		rc = LDAP_INAPPROPRIATE_MATCHING;
		goto done;
#endif /* ENFORCE_MR_SYNTAX_COMPAT */
	}
	a.asi_mr_ord_plugin = plugin_mr_find( mr_ordering );
	if (mr_substring && !slapi_matchingrule_is_compat(mr_substring, attr_syntax)) {
		slapi_log_error(SLAPI_LOG_FATAL, "attr_syntax_create",
						"Error: the SUBSTR matching rule [%s] is not compatible "
						"with the syntax [%s] for the attribute [%s]\n",
						mr_substring, attr_syntax, attr_names[0]);
#ifdef ENFORCE_MR_SYNTAX_COMPAT
		rc = LDAP_INAPPROPRIATE_MATCHING;
		goto done;
#endif /* ENFORCE_MR_SYNTAX_COMPAT */
	}
	a.asi_mr_sub_plugin = plugin_mr_find( mr_substring );
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

#ifdef ENFORCE_MR_SYNTAX_COMPAT
done:
#endif /* ENFORCE_MR_SYNTAX_COMPAT */
	slapi_ch_free((void **)&a.asi_name);

	return rc;
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
	slapi_ch_free_string(&tmp);

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
	const char *oid;
	if (a && ((oid = attr_get_syntax_oid(a)))) {
		*oidp = slapi_ch_strdup(oid);
		return( 0 );
	} else {
		*oidp = NULL;
		return( -1 );
	}
}

int
slapi_attr_is_dn_syntax_attr(Slapi_Attr *attr)
{
	const char *syntaxoid = NULL;
	int dn_syntax = 0; /* not DN, by default */

	if (attr && attr->a_flags & SLAPI_ATTR_FLAG_SYNTAX_IS_DN)
		/* it was checked before */
		return(1);

	if (attr && attr->a_plugin == NULL) {
 	    slapi_attr_init_syntax (attr);
 	}
	if (attr && attr->a_plugin) { /* If not set, there is no way to get the info */
		if ((syntaxoid = attr_get_syntax_oid(attr))) {
			dn_syntax = ((0 == strcmp(syntaxoid, NAMEANDOPTIONALUID_SYNTAX_OID))
						 || (0 == strcmp(syntaxoid, DN_SYNTAX_OID)));
		}
		if (dn_syntax)
			attr->a_flags |= SLAPI_ATTR_FLAG_SYNTAX_IS_DN;
	}
	return dn_syntax;
}

int
slapi_attr_is_dn_syntax_type(char *type)
{
	const char *syntaxoid = NULL;
	int dn_syntax = 0; /* not DN, by default */
	struct asyntaxinfo * asi;

	asi = attr_syntax_get_by_name(type);

	if (asi && asi->asi_plugin) { /* If not set, there is no way to get the info */
		if ((syntaxoid = asi->asi_plugin->plg_syntax_oid)) {
			dn_syntax = ((0 == strcmp(syntaxoid, NAMEANDOPTIONALUID_SYNTAX_OID))
						 || (0 == strcmp(syntaxoid, DN_SYNTAX_OID)));
		}
	}
	return dn_syntax;
}

#ifdef ATTR_LDAP_DEBUG

PRIntn
attr_syntax_printnode(PLHashEntry *he, PRIntn i, void *arg)
{
	char *alias = (char *)he->key;
	struct asyntaxinfo *a = (struct asyntaxinfo *)he->value;
	schemaext *ext = a->asi_extensions;

	printf( "  name: %s\n", a->asi_name );
	printf( "\t flags       : 0x%x\n", a->asi_flags );
	printf( "\t alias       : %s\n", alias );
	printf( "\t desc        : %s\n", a->asi_desc );
	printf( "\t oid         : %s\n", a->asi_oid );
	printf( "\t superior    : %s\n", a->asi_superior );
	printf( "\t mr_equality : %s\n", a->asi_mr_equality );
	printf( "\t mr_ordering : %s\n", a->asi_mr_ordering );
	printf( "\t mr_substring: %s\n", a->asi_mr_substring );
	while( ext ) {
		for ( i = 0; ext->values && ext->values[i]; i++ ) {
			printf( "\t %s      : %s\n", ext->term, ext->values[i]);
		}
		ext = ext->next;
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
attr_syntax_normalize_no_lookup_ext( char *s, int flags )
{
	char	*save, *tmps;

	tmps = slapi_ch_strdup(s);
	for ( save = tmps; (*tmps != '\0') && (*tmps != ' '); tmps++ )
	{
		*tmps = TOLOWER( *tmps );
	}
	*tmps = '\0';
	if (flags & ATTR_SYNTAX_NORM_ORIG_ATTR) {
		/* Chop trailing spaces + following strings */
		*(s + (tmps - save)) = '\0';
	}

	return save;
}

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

static void
attr_syntax_enumerate_attrs_ext( PLHashTable *ht,
                                 AttrEnumFunc aef, void *arg )
{
	struct enum_arg_wrapper eaw;
	eaw.aef = aef;
	eaw.arg = arg;

	if (!ht)
		return;

	PL_HashTableEnumerateEntries(ht, attr_syntax_enumerate_internal, &eaw);
}

void
attr_syntax_enumerate_attrs(AttrEnumFunc aef, void *arg, PRBool writelock )
{
	if (!oid2asi)
		return;

	if ( writelock ) {
		AS_LOCK_WRITE(oid2asi_lock);
		AS_LOCK_WRITE(name2asi_lock);
	} else {
		AS_LOCK_READ(oid2asi_lock);
		AS_LOCK_READ(name2asi_lock);
	}

	attr_syntax_enumerate_attrs_ext(oid2asi, aef, arg);

	if ( writelock ) {
		AS_UNLOCK_WRITE(name2asi_lock);
		AS_UNLOCK_WRITE(oid2asi_lock);
	} else {
		AS_UNLOCK_READ(name2asi_lock);
		AS_UNLOCK_READ(oid2asi_lock);
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

/*
 * Delete all attribute definitions without attr_syntax lock.
 * The caller is responsible for the lock.
 */
void
attr_syntax_delete_all_for_schemareload(unsigned long flag)
{
	struct attr_syntax_enum_flaginfo fi;

	memset(&fi, 0, sizeof(fi));
	fi.asef_flag = flag;
	attr_syntax_enumerate_attrs_ext(oid2asi, attr_syntax_delete_if_not_flagged,
	                                (void *)&fi);
}

#define ATTR_DEFAULT_SYNTAX_OID	"1.1"
#define ATTR_DEFAULT_SYNTAX	"defaultdirstringsyntax"
static int
attr_syntax_init(void)
{
	int schema_modify_enabled = config_get_schemamod();
	if (!schema_modify_enabled) asi_locking = 0;

	if (!oid2asi)
	{
		oid2asi = PL_NewHashTable(2047, hashNocaseString,
								  hashNocaseCompare,
								  PL_CompareValues, 0, 0);
		if ( asi_locking && NULL == ( oid2asi_lock = slapi_new_rwlock())) {
			if(oid2asi) PL_HashTableDestroy(oid2asi);
			oid2asi = NULL;

			slapi_log_error( SLAPI_LOG_FATAL, "attr_syntax_init",
					"slapi_new_rwlock() for oid2asi lock failed\n" );
			return 1;
		}
	}

	if (!name2asi)
	{
		name2asi = PL_NewHashTable(2047, hashNocaseString,
								   hashNocaseCompare,
								   PL_CompareValues, 0, 0);
		if ( asi_locking && NULL == ( name2asi_lock = slapi_new_rwlock())) {
			if(name2asi) PL_HashTableDestroy(name2asi);
			name2asi = NULL;

			slapi_log_error( SLAPI_LOG_FATAL, "attr_syntax_init",
					"slapi_new_rwlock() for oid2asi lock failed\n" );
			return 1;
		}
		/* add a default syntax plugin as fallback, required during startup
		*/
		attr_syntax_create_default( ATTR_DEFAULT_SYNTAX,
	                                ATTR_DEFAULT_SYNTAX_OID,
	                                DIRSTRING_SYNTAX_OID, 
	                                SLAPI_ATTR_FLAG_NOUSERMOD| SLAPI_ATTR_FLAG_NOEXPOSE);
	}
	return 0;
}

int
slapi_attr_syntax_exists(const char *attr_name)
{
    return attr_syntax_exists(attr_name);
}

/*
 * Keep the internally added schema in the hash table,
 * which are re-added if the schema is reloaded.
 */
static int
attr_syntax_internal_asi_add_ht(struct asyntaxinfo *asip)
{
	if (!internalasi) {
		internalasi = PL_NewHashTable(64, hashNocaseString,
		                              hashNocaseCompare,
		                              PL_CompareValues, 0, 0);
	}
	if (!internalasi) {
		slapi_log_error(SLAPI_LOG_FATAL, "attr_syntax_internal_asi_add_ht",
		                "Failed to create HashTable.\n");
		return 1;
	}
	if (!PL_HashTableLookup(internalasi, asip->asi_oid)) {
		struct asyntaxinfo *asip_copy = attr_syntax_dup(asip);
		if (!asip_copy) {
			slapi_log_error(SLAPI_LOG_FATAL, "attr_syntax_internal_asi_add_ht",
		                    "Failed to duplicate asyntaxinfo: %s.\n",
		                    asip->asi_name);
			return 1;
		}
		PL_HashTableAdd(internalasi, asip_copy->asi_oid, asip_copy);
	}
	return 0;
}

/*
 * Add an attribute syntax using some default flags, etc.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well)
 */
int
slapi_add_internal_attr_syntax( const char *name, const char *oid,
		const char *syntax, const char *mr_equality, unsigned long extraflags )
{
	int rc = LDAP_SUCCESS;
	struct asyntaxinfo	*asip;
	char *names[2];
	unsigned long std_flags = SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_OPATTR;

	names[0] = (char *)name;
	names[1] = NULL;

	rc = attr_syntax_create( oid, names,
			"internal server defined attribute type",
			 NULL,						/* superior */
			 mr_equality, NULL, NULL,	/* matching rules */
			 NULL, syntax,
			 SLAPI_SYNTAXLENGTH_NONE,
			 std_flags | extraflags,
			 &asip );

	if ( rc == LDAP_SUCCESS ) {
		rc = attr_syntax_add( asip );
		if ( rc == LDAP_SUCCESS ) {
			if (attr_syntax_internal_asi_add_ht(asip)) {
				slapi_log_error(SLAPI_LOG_FATAL,
				                "slapi_add_internal_attr_syntax",
				                "Failed to stash internal asyntaxinfo: %s.\n",
				                asip->asi_name);
			}
		}
	}

	return rc;
}

/* Adding internal asyncinfo via slapi_reload_internal_attr_syntax */
static int
attr_syntax_internal_asi_add(struct asyntaxinfo *asip, void *arg)
{
	struct asyntaxinfo *asip_copy;
	int rc = 0;

	if (!asip) {
		return 1;
	}
	/* Copy is needed since when reloading the schema,
	 * existing syntax info is cleaned up. */
	asip_copy = attr_syntax_dup(asip);
	rc = attr_syntax_add(asip_copy);
	if (LDAP_SUCCESS != rc) {
		attr_syntax_free(asip_copy);
	}
	return rc;
}

/* Reload internal attribute syntax stashed in the internalasi hashtable. */
int
slapi_reload_internal_attr_syntax()
{
	int rc = LDAP_SUCCESS;
	if (!internalasi) {
		slapi_log_error(SLAPI_LOG_TRACE, "attr_reload_internal_attr_syntax",
		                "No internal attribute syntax to reload.\n");
		return rc;
	}
	attr_syntax_enumerate_attrs_ext(internalasi, attr_syntax_internal_asi_add, NULL);
	return rc;
}

/*
 * See if the attribute at1 is in the list of at2.  Change by name, and oid(if necessary).
 */
struct asyntaxinfo *
attr_syntax_find(struct asyntaxinfo *at1, struct asyntaxinfo *at2)
{
	struct asyntaxinfo *asi;

	for(asi = at2; asi != NULL; asi = asi->asi_next){
		if(strcasecmp(at1->asi_name, asi->asi_name) == 0 || strcmp(at1->asi_oid, asi->asi_oid) == 0){
			/* found it */
			return asi;
		}
	}

	return NULL;
}
