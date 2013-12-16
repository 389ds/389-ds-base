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

/* schema.c - routines to enforce schema definitions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <prio.h>
#include <plstr.h>
#include <plhash.h>
#include "slap.h"

#if defined(USE_OPENLDAP)
#include <ldap_schema.h> /* openldap schema parser */
#endif

typedef struct sizedbuffer
{
    char *buffer;
	size_t size;
} sizedbuffer;

typedef char *(*schema_strstr_fn_t)( const char *big, const char *little);

/*
 * The schema_oc_kind_strings array is indexed by oc_kind values, i.e.,
 * OC_KIND_STRUCTURAL (0), OC_KIND_AUXILIARY (1), or OC_KIND_ABSTRACT (2).
 * The leading and trailing spaces are intentional.
 */
#define SCHEMA_OC_KIND_COUNT		3
static char *schema_oc_kind_strings_with_spaces[] = {
	" ABSTRACT ",
	" STRUCTURAL ",
	" AUXILIARY ",
};

/* constant strings (used in a few places) */
static const char *schema_obsolete_with_spaces =   " OBSOLETE ";
static const char *schema_collective_with_spaces = " COLLECTIVE ";
static const char *schema_nousermod_with_spaces =  " NO-USER-MODIFICATION ";

/* user defined origin array */
static char *schema_user_defined_origin[] = {
	"user defined",
	NULL
};

/*
 * pschemadse is based on the general implementation in dse
 */

static struct dse *pschemadse= NULL;

static void oc_add_nolock(struct objclass *newoc);
static int oc_delete_nolock (char *ocname);
static int oc_replace_nolock(const char *ocname, struct objclass *newoc); 
static int oc_check_required(Slapi_PBlock *, Slapi_Entry *,struct objclass *);
static int oc_check_allowed_sv(Slapi_PBlock *, Slapi_Entry *e, const char *type, struct objclass **oclist );
static int schema_delete_objectclasses ( Slapi_Entry *entryBefore,
		LDAPMod *mod, char *errorbuf, size_t errorbufsize,
		int schema_ds4x_compat );
static int schema_delete_attributes ( Slapi_Entry *entryBefore,
		LDAPMod *mod, char *errorbuf, size_t errorbufsize);
static int schema_add_attribute ( Slapi_PBlock *pb, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize, int schema_ds4x_compat );
static int schema_add_objectclass ( Slapi_PBlock *pb, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize, int schema_ds4x_compat );
static int schema_replace_attributes ( Slapi_PBlock *pb, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize );
static int schema_replace_objectclasses ( Slapi_PBlock *pb, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize );
static int schema_check_name(char *name, PRBool isAttribute, char *errorbuf,
		size_t errorbufsize );
static int schema_check_oid(const char *name, const char *oid,
		PRBool isAttribute, char *errorbuf, size_t errorbufsize);
static int isExtensibleObjectclass(const char *objectclass);
static int strip_oc_options ( struct objclass *poc );
static char *stripOption (char *attr);
static int schema_extension_cmp(schemaext *e1, schemaext *e2);
static int put_tagged_oid( char *outp, const char *tag, const char *oid,
		const char *suffix, int enquote );
static void strcat_oids( char *buf, char *prefix, char **oids,
		int schema_ds4x_compat );
static size_t strcat_extensions( char *buf, schemaext *extension );
static size_t strlen_null_ok(const char *s);
static int strcpy_count( char *dst, const char *src );
static int refresh_user_defined_schema(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg);
static int schema_check_oc_attrs ( struct objclass *poc, char *errorbuf,
		size_t errorbufsize, int stripOptions );
static struct objclass *oc_find_nolock( const char *ocname_or_oid, struct objclass *oc_private, PRBool use_private );
static struct objclass *oc_find_oid_nolock( const char *ocoid );
static void oc_free( struct objclass **ocp );
static PRBool oc_equal( struct objclass *oc1, struct objclass *oc2 );
static PRBool attr_syntax_equal( struct asyntaxinfo *asi1,
		struct asyntaxinfo *asi2 );
static int schema_strcmp( const char *s1, const char *s2 );
static int schema_strcmp_array( char **sa1, char **sa2,
		const char *ignorestr );
static PRBool schema_type_is_interesting( const char *type );
static void schema_create_errormsg( char *errorbuf, size_t errorbufsize,
		const char *prefix, const char *name, const char *fmt, ... )
#ifdef __GNUC__
        __attribute__ ((format (printf, 5, 6)));
#else
        ;
#endif
static int parse_at_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize,
        PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote);
static int extension_is_user_defined( schemaext *extensions );
static size_t strcat_qdlist( char *buf, char *prefix, char **qdlist );
#if defined (USE_OPENLDAP)
/*
 *  openldap
 */
static int parse_attr_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize,
        PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote);
static int parse_objclass_str(const char *input, struct objclass **oc, char *errorbuf, size_t errorbufsize,
        PRUint32 schema_flags, int is_user_defined,	int schema_ds4x_compat, struct objclass* private_schema );

#else
/*
 *  mozldap
 */
static char **parse_qdescrs(const char *s, int *n);
static char **parse_qdstrings(const char *s, int *n);
static char **parse_qdlist(const char *s, int *n, int strip_options);
static void free_qdlist(char **vals, int n);
static int read_at_ldif(const char *input, struct asyntaxinfo **asipp,
		char *errorbuf, size_t errorbufsize, PRUint32 flags,
		int is_user_defined, int schema_ds4x_compat, int is_remote);
static int read_oc_ldif ( const char *input, struct objclass **oc,
		char *errorbuf, size_t errorbufsize, PRUint32 flags, int is_user_defined,
		int schema_ds4x_compat );
static int get_flag_keyword( const char *keyword, int flag_value,
		const char **inputp, schema_strstr_fn_t strstr_fn );
static char *get_tagged_oid( const char *tag, const char **inputp,
		schema_strstr_fn_t strstr_fn );
static char **read_dollar_values ( char *vals);
static schemaext *parse_extensions( const char *schema_value, char **default_list );
#endif




/*
 * Some utility functions for dealing with a dynamic buffer
 */
static struct sizedbuffer *sizedbuffer_construct(size_t size);
static void sizedbuffer_destroy(struct sizedbuffer *p);
static void sizedbuffer_allocate(struct sizedbuffer *p, size_t sizeneeded);

/*
 * Constant strings that we pass to schema_create_errormsg().
 */
static const char *schema_errprefix_oc = "object class %s: ";
static const char *schema_errprefix_at = "attribute type %s: ";
static const char *schema_errprefix_generic = "%s: ";


/*
 * A "cached" copy of the "ignore trailing spaces" config. setting.
 * This is set during initialization only (server restart required for
 * changes to take effect). We do things this way to avoid lock/unlock
 * mutex sequences inside performance critical code.
 */
static int schema_ignore_trailing_spaces =
			SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES;

/* R/W lock used to serialize access to the schema DSE */
static Slapi_RWLock	*schema_dse_lock = NULL;

/*
 * The schema_dse_mandatory_init_callonce structure is used by NSPR to ensure
 * that schema_dse_mandatory_init() is called at most once.
 */
static PRCallOnceType schema_dse_mandatory_init_callonce = { 0, 0, 0 };

static int parse_at_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize,
        PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote)
{
#ifdef USE_OPENLDAP
    return parse_attr_str(input, asipp, errorbuf, errorbufsize, schema_flags, is_user_defined,schema_ds4x_compat,is_remote);
#else
    return read_at_ldif(input, asipp, errorbuf, errorbufsize, schema_flags, is_user_defined,schema_ds4x_compat,is_remote);
#endif
}

static int parse_oc_str(const char *input, struct objclass **oc, char *errorbuf,
		size_t errorbufsize, PRUint32 schema_flags, int is_user_defined,
		int schema_ds4x_compat, struct objclass* private_schema )
{
#ifdef USE_OPENLDAP
    return parse_objclass_str (input, oc, errorbuf, errorbufsize, schema_flags, is_user_defined, schema_ds4x_compat, private_schema );
#else
    return read_oc_ldif (input, oc, errorbuf, errorbufsize, schema_flags, is_user_defined, schema_ds4x_compat );
#endif
}


/* Essential initialization.  Returns PRSuccess if successful */
static PRStatus
schema_dse_mandatory_init( void )
{
	if ( NULL == ( schema_dse_lock = slapi_new_rwlock())) {
		slapi_log_error( SLAPI_LOG_FATAL, "schema_dse_mandatory_init",
				"slapi_new_rwlock() for schema DSE lock failed\n" );
		return PR_FAILURE;
	}

	schema_ignore_trailing_spaces = config_get_schema_ignore_trailing_spaces();
	return PR_SUCCESS;
}


static void
schema_dse_lock_read( void )
{
	if ( NULL != schema_dse_lock ||
			PR_SUCCESS == PR_CallOnce( &schema_dse_mandatory_init_callonce,
					schema_dse_mandatory_init )) {
		slapi_rwlock_rdlock( schema_dse_lock );
	}
}


static void
schema_dse_lock_write( void )
{
	if ( NULL != schema_dse_lock ||
			PR_SUCCESS == PR_CallOnce( &schema_dse_mandatory_init_callonce,
					schema_dse_mandatory_init )) {
		slapi_rwlock_wrlock( schema_dse_lock );
	}
}


static void
schema_dse_unlock( void )
{
	if ( schema_dse_lock != NULL ) {
		slapi_rwlock_unlock( schema_dse_lock );
	}
}


static int
dont_allow_that(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
	*returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

#if !defined(USE_OPENLDAP)
static const char *
skipWS(const char *s)
{
	while (s && isascii(*s) && isspace(*s) )
		++s;

	if ((isascii(*s)) == 0) {
		return NULL;
	}
	return s;
}

/*
 * like strchr() but strings within single quotes are skipped.
 */
static char *
strchr_skip_quoted_strings( char *s, int c )
{
	int	in_quote = 0;

	while ( *s != '\0' ) {
		if ( *s == '\'' ) {
			in_quote = 1 - in_quote;		/* toggle */
		} else if ( !in_quote && *s == c ) {
			return s;
		}
		++s;
	}

	return( NULL );
}
/**
 * parses a string containing a qdescrs or qdstrings (as described by
 * RFC 2252, section 4.1) into an array of strings; the second parameter
 * will hold the actual number of strings in the array.  The returned array
 * is NULL terminated.
 *
 * This function can handle qdescrs or qdstrings because the only
 * difference between the two is that fewer characters are allowed in
 * a qdescr (our parsing code does not check anyway) and we want to
 * strip attribute options when parsing qdescrs (indicated by a non-zero
 * strip_options parameter).
 */
static char **
parse_qdlist(const char *s, int *n, int strip_options)
{
	char **retval = 0;
	char *work = 0;
	char *start = 0, *end = 0;
	int num = 0;
    int in_quote = 0;

	if (n)
		*n = 0;

	if (!s || !*s || !n) {
		return retval;
	}

	/* make a working copy of the given string */
	work = slapi_ch_strdup(s);

	/* count the number of qdescr items in the string e.g. just count
	   the number of spaces */
	/* for a single qdescr, the terminal character will be the final
	   single quote; for a qdesclist, the terminal will be the close
	   parenthesis */
	end = strrchr(work, '\'');
	if ((start = strchr_skip_quoted_strings(work, '(')) != NULL)
		end = strchr_skip_quoted_strings(work, ')');
	else
		start = strchr(work, '\'');

	if (!end) /* already nulled out */
		end = work + strlen(work);

	if (start) {
		num = 1;
		/* first pass: count number of items and zero out non useful tokens */
		for (; *start && (start != end); ++start) {
			if (*start == '\'' ) {
				in_quote = 1 - in_quote;	/* toggle */
				*start = 0;
			} else if ( !in_quote && ((*start == ' ') || (*start == '(') ||
					(*start == ')'))) {
				if (*start == ' ') {
					num++;
				}
				*start = 0;
			}
		}
		*start = 0;

		/* allocate retval; num will be >= actual number of items */
		retval = (char**)slapi_ch_calloc(num+1, sizeof(char *));

		/* second pass: copy strings into the return value and set the
		   actual number of items returned */
		start = work;
		while (start != end) {
			/* skip over nulls */
			while (!*start && (start != end))
				++start;
			if (start == end)
				break;
			retval[*n] = slapi_ch_strdup(start);
			/*
			 * A qdescr list may contain attribute options; we just strip
			 * them here.  In the future, we may want to support them or do
			 * something really fancy with them
			 */
			if ( strip_options ) {
				stripOption(retval[*n]);
			}
			(*n)++;
			start += strlen(start);
		}
		PR_ASSERT( *n <= num );		/* sanity check */
		retval[*n] = NULL;
	} else {
		/* syntax error - no start and/or end delimiters */
	}

	/* free the working string */
	slapi_ch_free((void **)&work);

	return retval;
}

/**
 * parses a string containing a qdescrs (as described by RFC 2252, section 4.1)
 * into an array of strings; the second parameter will hold the actual number
 * of strings in the array.  The returned array is NULL terminated.
 */
static char **
parse_qdescrs(const char *s, int *n)
{
	return parse_qdlist( s, n, 1 /* strip attribute options */ );
}


/*
 * Parses a string containing a qdstrings (see RFC 2252, section 4.1) into
 * an array of strings; the second parameter will hold the actual number
 * of strings in the array.
 */
static char **
parse_qdstrings(const char *s, int *n)
{
	return parse_qdlist( s, n, 0 /* DO NOT strip attribute options */ );
}

static void
free_qdlist(char **vals, int n)
{
	int ii;
	for (ii = 0; ii < n; ++ii)
		slapi_ch_free((void **)&(vals[ii]));
	slapi_ch_free((void **)&vals);
}

#endif /* not openldap */

/*
 * slapi_entry_schema_check - check that entry e conforms to the schema
 * required by its object class(es). returns 0 if so, non-zero otherwise.
 * [ the pblock is used to check if this is a replicated operation.
 *   you may pass in NULL if this isn't part of an operation. ]
 * the pblock is also used to return a reason why schema checking failed.
 * it is also used to get schema flags
 * if replicated operations should be checked use slapi_entry_schema_check_ext
 */
int
slapi_entry_schema_check( Slapi_PBlock *pb, Slapi_Entry *e )
{
	return (slapi_entry_schema_check_ext(pb, e, 0));
}

int
slapi_entry_schema_check_ext( Slapi_PBlock *pb, Slapi_Entry *e, int repl_check )
{
  struct objclass **oclist;
  struct objclass *oc;
  const char *ocname;
  Slapi_Attr	*a, *aoc;
  Slapi_Value *v;
  int		ret = 0;
  int     schemacheck = config_get_schemacheck();
  int     is_replicated_operation = 0;
  int     is_extensible_object = 0;
  int i, oc_count = 0;
  int unknown_class = 0;
  char errtext[ BUFSIZ ];
  PRUint32 schema_flags = 0;

  /*
   * say the schema checked out ok if we're not checking schema at
   * all, or if this is a replication update.
   */
  if (pb != NULL) {
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
	slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, &schema_flags);
  }
  if ( schemacheck == 0 || (is_replicated_operation && !repl_check)) {
    return( 0 );
  }

  /* find the object class attribute - could error out here */
  if ( (aoc = attrlist_find( e->e_attrs, "objectclass" )) == NULL ) {
    LDAPDebug( LDAP_DEBUG_ANY,
	       "Entry \"%s\" required attribute \"objectclass\" missing\n",
	       slapi_entry_get_dn_const(e), 0, 0 );
	if (pb) {
		PR_snprintf( errtext, sizeof( errtext ),
	       "missing required attribute \"objectclass\"\n" );
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	}
    return( 1 );
  }

  /*
   * Create an array of pointers to the objclass definitions.
   */

  i= slapi_attr_first_value(aoc,&v);
  while (i != -1) {
    oc_count++;
    i= slapi_attr_next_value(aoc,i,&v);
  }

  oclist = (struct objclass**) 
    slapi_ch_malloc((oc_count+1)*sizeof(struct objclass*));

  /*
   * Need the read lock to create the oc array and while we use it.
   */
  if (!(schema_flags & DSE_SCHEMA_LOCKED))
    oc_lock_read();

  oc_count = 0;
  for (i= slapi_attr_first_value(aoc,&v); i != -1;
       i= slapi_attr_next_value(aoc,i,&v)) {

    ocname = slapi_value_get_string(v);

    if ( !ocname ) {
	LDAPDebug( LDAP_DEBUG_ANY,
	       "Entry \"%s\" \"objectclass\" value missing\n",
	       slapi_entry_get_dn_const(e), 0, 0 );
	if (pb) {
		PR_snprintf( errtext, sizeof( errtext ),
	       "missing \"objectclass\" value\n" );
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	}
	ret = 1;
	goto out;
    }

    if ( isExtensibleObjectclass( ocname )) {
      /* 
       *  if the entry is an extensibleObject, just check to see if
       *  the required attributes for whatever other objectclasses the
       *  entry might be are present. All other attributes are allowed 
       */
      is_extensible_object = 1;
      continue;
    }

    if ((oc = oc_find_nolock( ocname, NULL, PR_FALSE )) != NULL ) {
      oclist[oc_count++] = oc;
    } else {
      /* we don't know about the oc; return an appropriate error message */
      char			ebuf[ BUFSIZ ];
	  size_t		ocname_len = strlen( ocname );
	  const char	*extra_msg = "";

	  if ( ocname_len > 0 && isspace( ocname[ ocname_len-1 ] )) {
		if ( ocname_len > 1 && isspace( ocname[ ocname_len-2 ] )) {
			extra_msg = " (remove the trailing spaces)";
		} else {
			extra_msg = " (remove the trailing space)";
		}
	  }

      LDAPDebug( LDAP_DEBUG_ANY,
		 "Entry \"%s\" has unknown object class \"%s\"%s\n",
		 slapi_entry_get_dn_const(e),
		 escape_string(ocname, ebuf), extra_msg );
	  if (pb) {
		PR_snprintf( errtext, sizeof( errtext ),
		"unknown object class \"%s\"%s\n",
		 escape_string(ocname, ebuf), extra_msg );
		slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
	  }
      unknown_class = 1;
    }

  }
  oclist[oc_count] = NULL;

  if (unknown_class) {
    /* failure */
    ret = 1;
    goto out;
  }

  /*
   * go through all the checking so we can log everything
   * wrong with the entry. some day, we might want to return
   * this information to the client as an error message.
   */

  /*
   * check that the entry has required attrs for each oc
   */
  for (i = 0; oclist[i] != NULL; i++) {
    if ( oc_check_required( pb, e, oclist[i] ) != 0 ) {				
      ret = 1;
      goto out;
    }			
  }

  /*
   * check that each attr in the entry is allowed by some oc,
   * and that single-valued attrs only have one value
   */

  {
    Slapi_Attr *prevattr;
    i = slapi_entry_first_attr(e, &a);
    while (-1 != i && 0 == ret)
      {
	if (is_extensible_object == 0 && 
	    unknown_class == 0 &&
	    !slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_OPATTR))
	  {
	    char *attrtype;
	    slapi_attr_get_type(a, &attrtype);
	    if (oc_check_allowed_sv(pb, e, attrtype, oclist) != 0)
	      {
		ret = 1;
	      }
	  }
	
	if ( slapi_attr_flag_is_set( a, SLAPI_ATTR_FLAG_SINGLE ) ) {
	  if (slapi_valueset_count(&a->a_present_values) > 1)
	    {
          LDAPDebug( LDAP_DEBUG_ANY,
	         "Entry \"%s\" single-valued attribute \"%s\" has multiple values\n",
			 slapi_entry_get_dn_const(e),
			 a->a_type, 0 );
		  if (pb) {
			PR_snprintf( errtext, sizeof( errtext ),
	         	"single-valued attribute \"%s\" has multiple values\n",
				a->a_type );
			slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
		  }
	      ret = 1;
	    }
	}
	prevattr = a;
	i = slapi_entry_next_attr(e, prevattr, &a);
      }
  }

 out:
  /* Done with the oc array so can release the lock */
  if (!(schema_flags & DSE_SCHEMA_LOCKED))
    oc_unlock();
  slapi_ch_free((void**)&oclist);

  return( ret );
}

/*
 * The caller must obtain a read lock first by calling oc_lock_read().
 */
static int
oc_check_required( Slapi_PBlock *pb, Slapi_Entry *e, struct objclass *oc )
{
    int i;
    int        rc = 0; /* success, by default */
    Slapi_Attr *a;

    if (oc == NULL || oc->oc_required == NULL || oc->oc_required[0] == NULL) {
        return 0;  /* success, as none required  */
    }

    /* for each required attribute */
    for ( i = 0; oc->oc_required[i] != NULL; i++ ) {
        /* see if it's in the entry */
        for ( a = e->e_attrs; a != NULL; a = a->a_next ) {
            if ( slapi_attr_type_cmp( oc->oc_required[i], a->a_type,
                                      SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
                break;
            }
        }
        
        /* not there => schema violation */
        if ( a == NULL ) {
            char errtext[ BUFSIZ ];
            LDAPDebug( LDAP_DEBUG_ANY,
                       "Entry \"%s\" missing attribute \"%s\" required"
                       " by object class \"%s\"\n",
                       slapi_entry_get_dn_const(e),
                       oc->oc_required[i], oc->oc_name);
            if (pb) {
                PR_snprintf( errtext, sizeof( errtext ),
                       "missing attribute \"%s\" required"
                       " by object class \"%s\"\n",
                       oc->oc_required[i], oc->oc_name );
                        slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
            }
            rc = 1; /* failure */
        }
    }
    
    return rc;
}



/*
 * The caller must obtain a read lock first by calling oc_lock_read().
 */
static int
oc_check_allowed_sv(Slapi_PBlock *pb, Slapi_Entry *e, const char *type, struct objclass **oclist )
{
    struct objclass *oc;
    int i, j;
    int	rc = 1;    /* failure */
 
    /* always allow objectclass and entryid attributes */
    /* MFW XXX  THESE SHORTCUTS SHOULD NOT BE NECESSARY BUT THEY MASK 
     * MFW XXX  OTHER BUGS IN THE SERVER.
     */
    if ( slapi_attr_type_cmp( type, "objectclass", SLAPI_TYPE_CMP_EXACT ) == 0 ) {
      return( 0 );
    } else if ( slapi_attr_type_cmp( type, "entryid", SLAPI_TYPE_CMP_EXACT ) == 0 ) {
      return( 0 );
    }
    
    /* check that the type appears as req or opt in at least one oc */
    for (i = 0; rc != 0 && oclist[i] != NULL; i++) {
      oc = oclist[i];

      /* does it require the type? */
      for ( j = 0; oc->oc_required && oc->oc_required[j] != NULL; j++ ) {
        if ( slapi_attr_type_cmp( oc->oc_required[j],
                                  type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
          rc = 0;
          break;
        }
      }
          
      if ( 0 != rc ) {
        /* does it allow the type? */
        for ( j = 0; oc->oc_allowed && oc->oc_allowed[j] != NULL; j++ ) {
          if ( slapi_attr_type_cmp( oc->oc_allowed[j],
                                    type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 || 
               strcmp( oc->oc_allowed[j],"*" ) == 0 ) {
            rc = 0;
            break;
          }
        }
        /* maybe the next oc allows it */
      }
    }

    if ( 0 != rc ) {
      char errtext[ BUFSIZ ];
      char ebuf[ BUFSIZ ];
      LDAPDebug( LDAP_DEBUG_ANY,
         "Entry \"%s\" -- attribute \"%s\" not allowed\n",
         slapi_entry_get_dn_const(e),
         escape_string( type, ebuf ),
         0);

      if (pb) {
        PR_snprintf( errtext, sizeof( errtext ),
         "attribute \"%s\" not allowed\n",
         escape_string( type, ebuf ) );
        slapi_pblock_set( pb, SLAPI_PB_RESULT_TEXT, errtext );
      }
    }
    
    return rc;
}




/*
 * oc_find_name() will return a strdup'd string or NULL if the objectclass
 * could not be found.
 */
char *
oc_find_name( const char *name_or_oid )
{
	struct objclass	*oc;
	char			*ocname = NULL;

	oc_lock_read();
	if ( NULL != ( oc = oc_find_nolock( name_or_oid, NULL, PR_FALSE ))) {
		ocname = slapi_ch_strdup( oc->oc_name );
	}
	oc_unlock();

	return ocname;
}


/*
 * oc_find_nolock will return a pointer to the objectclass which has the
 *		same name OR oid.
 * NULL is returned if no match is found or `name_or_oid' is NULL.
 */
static struct objclass *
oc_find_nolock( const char *ocname_or_oid, struct objclass *oc_private, PRBool use_private)
{
	struct objclass	*oc;

	if ( NULL != ocname_or_oid ) {
		if ( !schema_ignore_trailing_spaces ) {
                        if (use_private) {
                                oc = oc_private;
                        } else {
                                oc = g_get_global_oc_nolock(); 
                        }
            for ( ; oc != NULL; oc = oc->oc_next ) {
                if ( ( strcasecmp( oc->oc_name, ocname_or_oid ) == 0 )
						|| ( oc->oc_oid &&
						strcasecmp( oc->oc_oid, ocname_or_oid ) == 0 )) {
					return( oc );
				}
			}
		} else {
            const char *p;
            size_t len;

            /* 
             * Ignore trailing spaces when comparing object class names.
             */
            for ( p = ocname_or_oid, len = 0;  (*p != '\0') && (*p != ' '); 
						p++, len++ ) {
				;	/* NULL */
            }
            
            if (use_private) {
                    oc = oc_private;
            } else {
                    oc = g_get_global_oc_nolock();
            }
            for ( ; oc != NULL; oc = oc->oc_next ) {
                if ( ( (strncasecmp( oc->oc_name, ocname_or_oid, len ) == 0) 
                       && (len == strlen(oc->oc_name)) )
                     || 
                     ( oc->oc_oid &&
                       ( strncasecmp( oc->oc_oid, ocname_or_oid, len ) == 0) 
                       && (len == strlen(oc->oc_oid)) ) ) {
                    return( oc );
                }
            }
		}
	}

	return( NULL );
}

/*
 * oc_find_oid_nolock will return a pointer to the objectclass which has
 *		the same oid.
 * NULL is returned if no match is found or `ocoid' is NULL.
 */
static struct objclass *
oc_find_oid_nolock( const char *ocoid )
{
	struct objclass	*oc;

	if ( NULL != ocoid ) {
		for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next ) {
			if ( ( oc->oc_oid &&
				   ( strcasecmp( oc->oc_oid, ocoid  ) == 0)) ){
				return( oc );
			}
		}
	}

	return( NULL );
}


/* 
    We need to keep the objectclasses in the same order as defined in the ldif files. If not
    SUP dependencies will break. When the user redefines an existing objectclass this code
    makes sure it is put back in the same order it was read to from the ldif file. It also
    verifies that the entries oc_superior value preceeds it in the chain. If not it will not
    allow the entry to be added. This makes sure that the ldif will be written back correctly.
*/

static int
oc_replace_nolock(const char *ocname, struct objclass *newoc) {
    struct objclass	*oc, *pnext;
    int				rc = LDAP_SUCCESS;	
    PRBool saw_sup=PR_FALSE;
    
    oc  = g_get_global_oc_nolock();
    
    if(newoc->oc_superior == NULL) 
    {
        saw_sup=PR_TRUE;
    }
    /* don't check SUP dependency for first one because it always/should be top */
    if (strcasecmp (oc->oc_name, ocname) == 0) {
        newoc->oc_next=oc->oc_next;
        g_set_global_oc_nolock ( newoc );
        oc_free( &oc );
    } else {
        for (pnext = oc ; pnext  != NULL;
        oc = pnext, pnext = pnext->oc_next) {
            if(pnext->oc_name == NULL) {
                rc = LDAP_OPERATIONS_ERROR;
                break;
            }
            if(newoc->oc_superior != NULL) {
                if(strcasecmp( pnext->oc_name, newoc->oc_superior) == 0) 
                {
                    saw_sup=PR_TRUE;
                }
            }
            if (strcasecmp ( pnext->oc_name, ocname ) == 0) {
                if(saw_sup)
                {
                    oc->oc_next=newoc;
                    newoc->oc_next=pnext->oc_next;
                    oc_free( &pnext );
                    break;
                    
                } else 
                {
                    rc = LDAP_TYPE_OR_VALUE_EXISTS;
                    break;
                }
                
            }
        }
    }
    return rc;
}


static int
oc_delete_nolock (char *ocname)
{
	struct objclass	*oc, *pnext;
	int				rc = 0;	/* failure */

	oc  = g_get_global_oc_nolock();
  
	/* special case if we're removing the first oc */
	if (strcasecmp (oc->oc_name, ocname) == 0) {
		g_set_global_oc_nolock ( oc->oc_next );
		oc_free( &oc );
		rc = 1;
	} else {
		for (pnext = oc->oc_next ; pnext  != NULL;
					oc = pnext, pnext = pnext->oc_next) {
			if (strcasecmp ( pnext->oc_name, ocname ) == 0) {
				oc->oc_next = pnext->oc_next;
				oc_free( &pnext );
				rc = 1;
				break;
			}
		}
	}

	return rc;
}

static void
oc_delete_all_nolock( void )
{
	struct objclass	*oc, *pnext;

	oc = g_get_global_oc_nolock();
	for (pnext = oc->oc_next; oc;
		 oc = pnext, pnext = oc?oc->oc_next:NULL) {
		oc_free( &oc );
	}
	g_set_global_oc_nolock ( NULL );
}


/*
 * Compare two objectclass definitions for equality.  Return PR_TRUE if
 * they are equivalent and PR_FALSE if not.
 *
 * The oc_required and oc_allowed arrays are ignored.
 * The string "user defined" is ignored within the origins array.
 * The following flags are ignored:
 *		OC_FLAG_STANDARD_OC
 *		OC_FLAG_USER_OC
 *		OC_FLAG_REDEFINED_OC
 */
static PRBool
oc_equal( struct objclass *oc1, struct objclass *oc2 )
{
	PRUint8	flagmask;

	if ( schema_strcmp( oc1->oc_name, oc2->oc_name ) != 0
			|| schema_strcmp( oc1->oc_desc, oc2->oc_desc ) != 0
			|| schema_strcmp( oc1->oc_oid, oc2->oc_oid ) != 0
			|| schema_strcmp( oc1->oc_superior, oc2->oc_superior ) != 0 ) {
		return PR_FALSE;
	}

	flagmask = ~(OC_FLAG_STANDARD_OC | OC_FLAG_USER_OC | OC_FLAG_REDEFINED_OC);
	if ( oc1->oc_kind != oc2->oc_kind
			|| ( oc1->oc_flags & flagmask ) != ( oc2->oc_flags & flagmask )) {
		return PR_FALSE;
	}

	if ( schema_strcmp_array( oc1->oc_orig_required, oc2->oc_orig_required,
			NULL ) != 0
			|| schema_strcmp_array( oc1->oc_orig_allowed, oc2->oc_orig_allowed,
			NULL ) != 0
			|| schema_extension_cmp( oc1->oc_extensions, oc2->oc_extensions ) != 0 ) {
		return PR_FALSE;
	}

	return PR_TRUE;
}


#ifdef OC_DEBUG

static int
oc_print( struct objclass *oc )
{
	int	i;

	printf( "object class %s\n", oc->oc_name );
	if ( oc->oc_required != NULL ) {
		printf( "\trequires %s", oc->oc_required[0] );
		for ( i = 1; oc->oc_required[i] != NULL; i++ ) {
			printf( ",%s", oc->oc_required[i] );
		}
		printf( "\n" );
	}
	if ( oc->oc_allowed != NULL ) {
		printf( "\tallows %s", oc->oc_allowed[0] );
		for ( i = 1; oc->oc_allowed[i] != NULL; i++ ) {
			printf( ",%s", oc->oc_allowed[i] );
		}
		printf( "\n" );
	}
	return 0;
}
#endif

/*
 *  Compare the X-ORIGIN extension, other extensions can be ignored
 */
static int
schema_extension_cmp(schemaext *e1, schemaext *e2)
{
    schemaext *e1_head = e1;
    schemaext *e2_head = e2;
    int found = 0;
    int e1_has_origin = 0;
    int e2_has_origin = 0;
    int i, ii;

    if(e1 == NULL && e2 == NULL){
         return 0; /* match */
    } else if (e1 == NULL || e2 == NULL){
         return -1;
    }
    while(e1){
        if(strcmp(e1->term, "X-ORIGIN")){
            e1 = e1->next;
            continue;
        }
        e1_has_origin = 1;
        while(e2){
            if(strcmp(e1->term, e2->term) == 0)
            {
                e2_has_origin = 1;
                if(e1->values == NULL && e2->values == NULL){
                    return 0;
                } else if (e1->values == NULL || e2->values == NULL){
                    return -1;
                }
                for (i = 0; e1->values[i]; i++)
                {
                    found = 0;
                    for(ii = 0; e2->values[ii]; ii++)
                    {
                         if(strcmp(e1->values[i], e2->values[ii]) == 0){
                             found = 1;
                             break;
                         }
                    }
                    if(!found){
                        return -1;
                    }
                }
                /* So far so good, move on to the next check */
                goto next;
            }
            e2 = e2->next;
        }
        e2 = e2_head;
        e1 = e1->next;
    }

    if(e1_has_origin != e2_has_origin){
        return -1;
    } else if (e1_has_origin == 0 && e2_has_origin == 0){
        return 0;
    }

next:
    /*
     *  We know that e2 has the same extensions as e1, but does e1 have all the extensions as e2?
     *  Run the compare in reverse...
     */
    found = 0;
    e1 = e1_head;
    e2 = e2_head;

    while(e2){
        if(strcmp(e2->term, "X-ORIGIN")){
            e2 = e2->next;
            continue;
        }
        while(e1){
            if(strcmp(e2->term, e1->term) == 0)
            {
                if(e2->values == NULL && e1->values == NULL){
                    return 0;
                } else if (e1->values == NULL || e2->values == NULL){
                    return -1;
                }
                for (i = 0; e2->values[i]; i++)
                {
                    found = 0;
                    for(ii = 0; e1->values[ii]; ii++)
                    {
                         if(strcmp(e2->values[i], e1->values[ii]) == 0){
                             found = 1;
                             break;
                         }
                    }
                    if(!found){
                        return -1;
                    }
                }
                return 0;
            }
            e1 = e1->next;
        }
        e1 = e1_head;
        e2 = e2->next;
    }

    return 0;
}

/*
 * Compare two attrsyntax definitions for equality.  Return PR_TRUE if
 * they are equivalent and PR_FALSE if not.
 *
 * The string "user defined" is ignored within the origins array.
 * The following flags are ignored:
 *    SLAPI_ATTR_FLAG_STD_ATTR
 *    SLAPI_ATTR_FLAG_NOLOCKING
 *    SLAPI_ATTR_FLAG_OVERRIDE
 */
static PRBool
attr_syntax_equal( struct asyntaxinfo *asi1, struct asyntaxinfo *asi2 )
{
	unsigned long flagmask;

	flagmask = ~( SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_NOLOCKING
				| SLAPI_ATTR_FLAG_OVERRIDE );

	if ( schema_strcmp( asi1->asi_oid, asi2->asi_oid ) != 0
				|| schema_strcmp( asi1->asi_name, asi2->asi_name ) != 0
				|| schema_strcmp( asi1->asi_desc, asi2->asi_desc ) != 0
				|| schema_strcmp( asi1->asi_superior, asi2->asi_superior ) != 0
				|| schema_strcmp( asi1->asi_mr_equality, asi2->asi_mr_equality )
						!= 0
				|| schema_strcmp( asi1->asi_mr_ordering, asi2->asi_mr_ordering )
						!= 0
				|| schema_strcmp( asi1->asi_mr_substring,
						asi2->asi_mr_substring ) != 0 ) {
		return PR_FALSE;
	}

	if ( schema_strcmp_array( asi1->asi_aliases, asi2->asi_aliases, NULL ) != 0
				|| schema_extension_cmp (asi1->asi_extensions, asi2->asi_extensions) != 0
				|| asi1->asi_plugin != asi2->asi_plugin
				|| ( asi1->asi_flags & flagmask ) !=
						( asi2->asi_flags & flagmask )
				|| asi1->asi_syntaxlength != asi2->asi_syntaxlength ) {
		return PR_FALSE;
	}

	return PR_TRUE;
}



/*
 * Like strcmp(), but a NULL string pointer is treated as equivalent to
 * another NULL one and NULL is treated as "less than" all non-NULL values.
 */
static int
schema_strcmp( const char *s1, const char *s2 )
{
	if ( s1 == NULL ) {
		if ( s2 == NULL ) {
			return 0;	/* equal */
		}
		return -1;		/* s1 < s2 */
	}

	if ( s2 == NULL ) {
		return 1;		/* s1 > s2 */
	}

	return strcmp( s1, s2 );
}


/*
 * Invoke strcmp() on each string in an array.  If one array has fewer elements
 * than the other, it is treated as "less than" the other.  Two NULL or
 * empty arrays (or one NULL and one empty) are considered to be equivalent.
 *
 * If ignorestr is non-NULL, occurrences of that string are ignored.
 */
static int
schema_strcmp_array( char **sa1, char **sa2, const char *ignorestr )
{
	int		i1, i2, rc;

	if ( sa1 == NULL || *sa1 == NULL ) {
		if ( sa2 == NULL || *sa2 == NULL ) {
			return 0;	/* equal */
		}
		return -1;		/* sa1 < sa2 */
	}

	if ( sa2 == NULL || *sa2 == NULL ) {
		return 1;	/* sa1 > sa2 */
	}

	rc = 0;
	i1 = i2 = 0;
	while ( sa1[i1] != NULL && sa2[i2] != NULL ) {
		if ( NULL != ignorestr ) {
			if ( 0 == strcmp( sa1[i1], ignorestr )) {
				++i1;
				continue;
			}
			if ( 0 == strcmp( sa2[i2], ignorestr )) {
				++i2;
				continue;
			}
		}
		rc = strcmp( sa1[i1], sa2[i2] );
		++i1;
		++i2;
	}

	if ( rc == 0 ) {	/* all matched so far */
		/* get rid of trailing ignored strings (if any) */
		if ( NULL != ignorestr ) {
			if ( sa1[i1] != NULL && 0 == strcmp( sa1[i1], ignorestr )) {
				++i1;
			}
			if ( sa2[i2] != NULL && 0 == strcmp( sa2[i2], ignorestr )) {
				++i2;
			}
		}

		/* check for differing array lengths */
		if ( sa2[i2] != NULL ) {
			rc = -1;	/* sa1 < sa2 -- fewer elements */
		} else if ( sa1[i1] != NULL ) {
			rc = 1;	/* sa1 > sa2 -- more elements */
		}
	}

	return rc;
}


struct attr_enum_wrapper {
	Slapi_Attr **attrs;
	int enquote_sup_oc;
	struct sizedbuffer *psbAttrTypes;
	int user_defined_only;
	int schema_ds4x_compat;
};

static int
schema_attr_enum_callback(struct asyntaxinfo *asip, void *arg)
{
	struct attr_enum_wrapper *aew = (struct attr_enum_wrapper *)arg;
	int aliaslen = 0;
	struct berval val;
	struct berval *vals[2] = {0, 0};
    const char *attr_desc, *syntaxoid;
	char *outp, syntaxlengthbuf[ 128 ];
	int	i;

	vals[0] = &val;

	if (!asip) {
		LDAPDebug(LDAP_DEBUG_ANY,
				"Error: no attribute types in schema_attr_enum_callback\n",
				0, 0, 0);
		return ATTR_SYNTAX_ENUM_NEXT;
	}

	if (aew->user_defined_only &&
			(asip->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR)) {
		return ATTR_SYNTAX_ENUM_NEXT; /* not user defined */
	}

	if ( aew->schema_ds4x_compat ) {
		attr_desc = ( asip->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR)
				? ATTR_STANDARD_STRING : ATTR_USERDEF_STRING;
	} else {
		attr_desc = asip->asi_desc;
	}

	if ( asip->asi_aliases != NULL ) {
		for ( i = 0; asip->asi_aliases[i] != NULL; ++i ) {
			aliaslen += strlen( asip->asi_aliases[i] );
		}
	}

	syntaxoid = asip->asi_plugin->plg_syntax_oid;

	if ( !aew->schema_ds4x_compat &&
				asip->asi_syntaxlength != SLAPI_SYNTAXLENGTH_NONE ) {
		/* sprintf() is safe because syntaxlengthbuf is large enough */
		sprintf( syntaxlengthbuf, "{%d}", asip->asi_syntaxlength );
	} else {
		*syntaxlengthbuf = '\0';
	}

	/*
	 * XXX: 256 is a magic number... it must be big enough to account for
	 * all of the fixed sized items we output.
	 */
	sizedbuffer_allocate(aew->psbAttrTypes,256+strlen(asip->asi_oid)+
			strlen(asip->asi_name) +
			aliaslen + strlen_null_ok(attr_desc) +
			strlen(syntaxoid) +
			strlen_null_ok(asip->asi_superior) +
			strlen_null_ok(asip->asi_mr_equality) +
			strlen_null_ok(asip->asi_mr_ordering) +
			strlen_null_ok(asip->asi_mr_substring) +
			strcat_extensions( NULL, asip->asi_extensions ));

	/*
	 * Overall strategy is to maintain a pointer to the next location in
	 * the output buffer so we can do simple strcpy's, sprintf's, etc.
	 * That pointer is `outp'.  Each item that is output includes a trailing
	 * space, so there is no need to include a leading one in the next item.
	 */
	outp = aew->psbAttrTypes->buffer;
	outp += sprintf(outp, "( %s NAME ", asip->asi_oid);
	if ( asip->asi_aliases == NULL || asip->asi_aliases[0] == NULL ) {
		/* only one name */
		outp += sprintf(outp, "'%s' ", asip->asi_name);
	} else {
		/* several names */
		outp += sprintf(outp, "( '%s' ", asip->asi_name);
		for ( i = 0; asip->asi_aliases[i] != NULL; ++i ) {
			outp += sprintf(outp, "'%s' ", asip->asi_aliases[i]);
		}
		outp += strcpy_count(outp, ") ");
	}

	/* DESC is optional */
	if (attr_desc && *attr_desc) {
		outp += sprintf( outp, "DESC '%s'", attr_desc );
	}
	if ( !aew->schema_ds4x_compat &&
				( asip->asi_flags & SLAPI_ATTR_FLAG_OBSOLETE )) {
		outp += strcpy_count( outp, schema_obsolete_with_spaces );
	} else {
		outp += strcpy_count( outp, " " );
	}

	if ( !aew->schema_ds4x_compat ) {
		outp += put_tagged_oid( outp, "SUP ",
				asip->asi_superior, NULL, aew->enquote_sup_oc );
		outp += put_tagged_oid( outp, "EQUALITY ",
				asip->asi_mr_equality, NULL, aew->enquote_sup_oc );
		outp += put_tagged_oid( outp, "ORDERING ",
				asip->asi_mr_ordering, NULL, aew->enquote_sup_oc );
		outp += put_tagged_oid( outp, "SUBSTR ",
				asip->asi_mr_substring, NULL, aew->enquote_sup_oc );
	}

	outp += put_tagged_oid( outp, "SYNTAX ", syntaxoid, syntaxlengthbuf,
			aew->enquote_sup_oc );

	if (asip->asi_flags & SLAPI_ATTR_FLAG_SINGLE) {
		outp += strcpy_count(outp, "SINGLE-VALUE ");
	}
	if ( !aew->schema_ds4x_compat ) {
		if (asip->asi_flags & SLAPI_ATTR_FLAG_COLLECTIVE ) {
			outp += strcpy_count( outp, 1 + schema_collective_with_spaces );
		}
		if (asip->asi_flags & SLAPI_ATTR_FLAG_NOUSERMOD ) {
			outp += strcpy_count( outp, 1 + schema_nousermod_with_spaces );
		}
		if (asip->asi_flags & SLAPI_ATTR_FLAG_OPATTR) {
			outp += strcpy_count(outp, "USAGE directoryOperation ");
		}

		outp += strcat_extensions( outp, asip->asi_extensions );
	}
	outp += strcpy_count(outp, ")");

	val.bv_val = aew->psbAttrTypes->buffer;
	val.bv_len = outp - aew->psbAttrTypes->buffer;
	attrlist_merge(aew->attrs, "attributetypes", vals);

	return ATTR_SYNTAX_ENUM_NEXT;
}


struct syntax_enum_wrapper {
	Slapi_Attr **attrs;
	struct sizedbuffer *psbSyntaxDescription;
};

static int
schema_syntax_enum_callback(char **names, Slapi_PluginDesc *plugindesc,
		void *arg)
{
	struct syntax_enum_wrapper *sew = (struct syntax_enum_wrapper *)arg;
	char	*oid, *desc;
	int		i;
	struct berval val;
	struct berval *vals[2] = {0, 0};
	vals[0] = &val;

	oid = NULL;
	if ( names != NULL ) {
		for ( i = 0; names[i] != NULL; ++i ) {
			if ( isdigit( names[i][0] )) {
				oid = names[i];
				break;
			}
		}
	}

	if ( oid == NULL ) {	/* must have an OID */
		LDAPDebug(LDAP_DEBUG_ANY, "Error: no OID found in"
				" schema_syntax_enum_callback for syntax %s\n",
				( names == NULL ) ? "unknown" : names[0], 0, 0);
		return 1;
	}

	desc = names[0];  /* by convention, the first name is the "official" one */

	/*
	 * RFC 2252 section 4.3.3 Syntax Description says:
     *
	 * The following BNF may be used to associate a short description with a
	 * syntax OBJECT IDENTIFIER. Implementors should note that future
	 * versions of this document may expand this definition to include
	 * additional terms.  Terms whose identifier begins with "X-" are
	 * reserved for private experiments, and MUST be followed by a
	 * <qdstrings>.
	 *
	 * SyntaxDescription = "(" whsp
	 *     numericoid whsp
	 *     [ "DESC" qdstring ]
	 *     whsp ")"
     *
     * And section 5.3.1 ldapSyntaxes says:
	 *
	 * Servers MAY use this attribute to list the syntaxes which are
	 * implemented.  Each value corresponds to one syntax.
	 *
	 *	( 1.3.6.1.4.1.1466.101.120.16 NAME 'ldapSyntaxes'
	 *	  EQUALITY objectIdentifierFirstComponentMatch
	 *	  SYNTAX 1.3.6.1.4.1.1466.115.121.1.54 USAGE directoryOperation )
     */
	if ( desc == NULL ) {
		/* allocate enough room for "(  )" and '\0' at end */
		sizedbuffer_allocate(sew->psbSyntaxDescription, strlen(oid) + 5);
		sprintf(sew->psbSyntaxDescription->buffer, "( %s )", oid );
	} else {
		/* allocate enough room for "(  ) DESC '' " and '\0' at end */
		sizedbuffer_allocate(sew->psbSyntaxDescription,
				strlen(oid) + strlen(desc) + 13);
		sprintf(sew->psbSyntaxDescription->buffer, "( %s DESC '%s' )",
				oid, desc );
	}

	val.bv_val = sew->psbSyntaxDescription->buffer;
	val.bv_len = strlen(sew->psbSyntaxDescription->buffer);
	attrlist_merge(sew->attrs, "ldapSyntaxes", vals);

	return 1;
}

struct listargs{
        char **attrs;
        unsigned long flag;
};

static int
schema_list_attributes_callback(struct asyntaxinfo *asi, void *arg)
{
        struct listargs *aew = (struct listargs *)arg;

        if (!asi) {
                LDAPDebug(LDAP_DEBUG_ANY, "Error: no attribute types in schema_list_attributes_callback\n",
                                  0, 0, 0);
                return ATTR_SYNTAX_ENUM_NEXT;
        }
        if (aew->flag && (asi->asi_flags & aew->flag)) {
#if defined(USE_OLD_UNHASHED)
           /* skip unhashed password */
           if (!is_type_forbidden(asi->asi_name)) {
#endif
                charray_add(&aew->attrs, slapi_ch_strdup(asi->asi_name));
                if (NULL != asi->asi_aliases) {
                    int        i;

                    for ( i = 0; asi->asi_aliases[i] != NULL; ++i ) {
                        charray_add(&aew->attrs,
                                    slapi_ch_strdup(asi->asi_aliases[i]));
                    }
                }
#if defined(USE_OLD_UNHASHED)
            }
#endif
        }
        return ATTR_SYNTAX_ENUM_NEXT;
}

/* Return the list of attributes names matching attribute flags */
char **
slapi_schema_list_attribute_names(unsigned long flag)
{
        struct listargs aew;
        memset(&aew,0,sizeof(struct listargs));
        aew.flag=flag;

        attr_syntax_enumerate_attrs(schema_list_attributes_callback, &aew,
                                    PR_FALSE);
        return aew.attrs;
}


/*
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int 
read_schema_dse(
	Slapi_PBlock *pb,
	Slapi_Entry *pschema_info_e,
	Slapi_Entry *entryAfter,
	int *returncode,
	char *returntext /* not used */,
	void *arg /* not used */ )
{
    struct berval   val;
    struct berval   *vals[2];
    struct objclass *oc;
    struct matchingRuleList *mrl=NULL;
    struct sizedbuffer *psbObjectClasses= sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbAttrTypes= sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbMatchingRule= sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbSyntaxDescription = sizedbuffer_construct(BUFSIZ);
    struct attr_enum_wrapper aew;
    struct syntax_enum_wrapper sew;
    const CSN *csn;
    char *mr_desc, *mr_name, *oc_description;
    char **allowed, **required;
    PRUint32 schema_flags = 0;
    int enquote_sup_oc = config_get_enquote_sup_oc();
    int schema_ds4x_compat = config_get_ds4_compatible_schema();
    int user_defined_only = 0;
    int i;

    vals[0] = &val;
    vals[1] = NULL;

    slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, (void*)&schema_flags);
    user_defined_only = (schema_flags & DSE_SCHEMA_USER_DEFINED_ONLY) ? 1 : 0;
  
    attrlist_delete (&pschema_info_e->e_attrs, "objectclasses");
    attrlist_delete (&pschema_info_e->e_attrs, "attributetypes");
    attrlist_delete (&pschema_info_e->e_attrs, "matchingRules");
    attrlist_delete (&pschema_info_e->e_attrs, "ldapSyntaxes");
    /*
     * attrlist_delete (&pschema_info_e->e_attrs, "matchingRuleUse");
     */

    schema_dse_lock_read();
    oc_lock_read();

    /* return the objectclasses */
    for (oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next)
    {
	    size_t size= 0;
        int need_extra_space = 1;

        if (user_defined_only &&
            !((oc->oc_flags & OC_FLAG_USER_OC) ||
              (oc->oc_flags & OC_FLAG_REDEFINED_OC) ))
        {
            continue;
        }
        /*
         * XXX: 256 is a magic number... it must be large enough to fit
         * all of the fixed size items including description (DESC),
         * kind (STRUCTURAL, AUXILIARY, or ABSTRACT), and the OBSOLETE flag.
         */
        if ( schema_ds4x_compat ) {
            oc_description = (oc->oc_flags & OC_FLAG_STANDARD_OC) ?
                             OC_STANDARD_STRING : OC_USERDEF_STRING;
        } else {
            oc_description = oc->oc_desc;
        }
        size = 256+strlen_null_ok(oc->oc_oid) + strlen(oc->oc_name) +
               strlen_null_ok(oc_description) +	strcat_extensions( NULL, oc->oc_extensions );
        required = schema_ds4x_compat ? oc->oc_required : oc->oc_orig_required;
        if (required && required[0]) {
            for (i = 0 ; required[i]; i++)
                size+= 16 + strlen(required[i]);
        }
        allowed = schema_ds4x_compat ? oc->oc_allowed : oc->oc_orig_allowed;
        if (allowed && allowed[0]) {
            for (i = 0 ; allowed[i]; i++)
                size+= 16 + strlen(allowed[i]);
        }
        sizedbuffer_allocate(psbObjectClasses,size);
        /* put the OID and the NAME */
        sprintf (psbObjectClasses->buffer, "( %s NAME '%s'", (oc->oc_oid) ? oc->oc_oid : "", oc->oc_name);
        /* The DESC (description) is OPTIONAL */
        if (oc_description) {
            strcat(psbObjectClasses->buffer, " DESC '");
            /*
             * We want to list an empty description
             * element if it was defined that way.
             */
            if (*oc_description) {
                strcat(psbObjectClasses->buffer, oc_description);
            }
            strcat(psbObjectClasses->buffer, "'");
            need_extra_space = 1;
        }
        /* put the OBSOLETE keyword */
        if (!schema_ds4x_compat && (oc->oc_flags & OC_FLAG_OBSOLETE)) {
            strcat(psbObjectClasses->buffer, schema_obsolete_with_spaces);
            need_extra_space = 0;
        }
        /* put the SUP superior objectclass */
        if (0 != strcasecmp(oc->oc_name, "top")) { /* top has no SUP */
            /*
             * Some AUXILIARY AND ABSTRACT objectclasses may not have a SUP either
             * for compatability, every objectclass other than top must have a SUP
             */
            if (schema_ds4x_compat || (oc->oc_superior && *oc->oc_superior)) {
                if (need_extra_space) {
                    strcat(psbObjectClasses->buffer, " ");
                }
                strcat(psbObjectClasses->buffer, "SUP ");
                strcat(psbObjectClasses->buffer, (enquote_sup_oc ? "'" : ""));
                strcat(psbObjectClasses->buffer, ((oc->oc_superior && *oc->oc_superior) ?
                    oc->oc_superior : "top"));
                strcat(psbObjectClasses->buffer, (enquote_sup_oc ? "'" : ""));
                need_extra_space = 1;
            }
        }
        /* put the kind of objectclass */
        if (schema_ds4x_compat) {
            if (need_extra_space) {
                strcat(psbObjectClasses->buffer, " ");
            }
        } else {

            strcat(psbObjectClasses->buffer, schema_oc_kind_strings_with_spaces[oc->oc_kind]);
        }
        strcat_oids( psbObjectClasses->buffer, "MUST", required, schema_ds4x_compat );
        strcat_oids( psbObjectClasses->buffer, "MAY", allowed, schema_ds4x_compat );
        if ( !schema_ds4x_compat ) {
            strcat_extensions( psbObjectClasses->buffer, oc->oc_extensions );
        }
        strcat( psbObjectClasses->buffer, ")");
        val.bv_val = psbObjectClasses->buffer;
        val.bv_len = strlen (psbObjectClasses->buffer);
        attrlist_merge (&pschema_info_e->e_attrs, "objectclasses", vals);
    }

    oc_unlock();

    /* now return the attrs */
    aew.attrs = &pschema_info_e->e_attrs;
    aew.enquote_sup_oc = enquote_sup_oc;
    aew.psbAttrTypes = psbAttrTypes;
    aew.user_defined_only = user_defined_only;
    aew.schema_ds4x_compat = schema_ds4x_compat;
    attr_syntax_enumerate_attrs(schema_attr_enum_callback, &aew, PR_FALSE);

    /* return the set of matching rules we support */
    for (mrl = g_get_global_mrl(); !user_defined_only && mrl != NULL; mrl = mrl->mrl_next) {
        mr_name = mrl->mr_entry->mr_name ? mrl->mr_entry->mr_name : "";
        mr_desc = mrl->mr_entry->mr_desc ? mrl->mr_entry->mr_desc : "";
        sizedbuffer_allocate(psbMatchingRule,128 + strlen_null_ok(mrl->mr_entry->mr_oid) +
		    strlen(mr_name)+ strlen(mr_desc) + strlen_null_ok(mrl->mr_entry->mr_syntax));
        if ( schema_ds4x_compat ) {
        sprintf(psbMatchingRule->buffer,
                "( %s NAME '%s' DESC '%s' SYNTAX %s%s%s )",
                (mrl->mr_entry->mr_oid ? mrl->mr_entry->mr_oid : ""),
                mr_name, mr_desc, enquote_sup_oc ? "'" : "",
                mrl->mr_entry->mr_syntax ? mrl->mr_entry->mr_syntax : "" ,
                enquote_sup_oc ? "'" : "");
        } else if ( NULL != mrl->mr_entry->mr_oid &&
                    NULL != mrl->mr_entry->mr_syntax ){
            char *p;

            sprintf(psbMatchingRule->buffer, "( %s ", mrl->mr_entry->mr_oid );
            p = psbMatchingRule->buffer + strlen(psbMatchingRule->buffer);
            if ( *mr_name != '\0' ) {
                sprintf(p, "NAME '%s' ", mr_name );
                p += strlen(p);
            }
            if ( *mr_desc != '\0' ) {
                sprintf(p, "DESC '%s' ", mr_desc );
                p += strlen(p);
            }
            sprintf(p, "SYNTAX %s )", mrl->mr_entry->mr_syntax );
        }
        val.bv_val = psbMatchingRule->buffer;
        val.bv_len = strlen (psbMatchingRule->buffer);
        attrlist_merge (&pschema_info_e->e_attrs, "matchingRules", vals);
    }
    if ( !schema_ds4x_compat && !user_defined_only ) {
        /* return the set of syntaxes we support */
        sew.attrs = &pschema_info_e->e_attrs;
        sew.psbSyntaxDescription = psbSyntaxDescription;
        plugin_syntax_enumerate(schema_syntax_enum_callback, &sew);
    }
    csn = g_get_global_schema_csn();
    if (NULL != csn) {
        char csn_str[CSN_STRSIZE + 1];

        csn_as_string(csn, PR_FALSE, csn_str);
        slapi_entry_attr_delete(pschema_info_e, "nsschemacsn");
        slapi_entry_add_string(pschema_info_e, "nsschemacsn", csn_str);
    }

    schema_dse_unlock();

    sizedbuffer_destroy(psbObjectClasses);
    sizedbuffer_destroy(psbAttrTypes);
    sizedbuffer_destroy(psbMatchingRule);
    sizedbuffer_destroy(psbSyntaxDescription);
    *returncode= LDAP_SUCCESS;

    return SLAPI_DSE_CALLBACK_OK;
}

/* helper for deleting mods (we do not want to be applied) from the mods array */
static void
mod_free(LDAPMod *mod)
{
	ber_bvecfree(mod->mod_bvalues);
	slapi_ch_free((void**)&(mod->mod_type));
	slapi_ch_free((void**)&mod);
}

/*
 * modify_schema_dse: called by do_modify() when target is cn=schema
 *
 * Add/Delete attributes and objectclasses from the schema 
 * Supported mod_ops are LDAP_MOD_DELETE and LDAP_MOD_ADD
 *
 * Note that the in-memory DSE Slapi_Entry object does NOT hold the
 * attributeTypes and objectClasses attributes -- it only holds
 * non-schema related attributes such as aci.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int
modify_schema_dse (Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
  int i, rc= SLAPI_DSE_CALLBACK_OK;	/* default is to apply changes to the DSE */
  char *schema_dse_attr_name;
  LDAPMod **mods = NULL;
  int num_mods = 0; /* count the number of mods */
  int schema_ds4x_compat = config_get_ds4_compatible_schema();
  int schema_modify_enabled = config_get_schemamod();
  int reapply_mods = 0;
  int is_replicated_operation = 0;

  if (!schema_modify_enabled) {
	*returncode = LDAP_UNWILLING_TO_PERFORM;
	schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				schema_errprefix_generic, "Generic", 
				"schema update is disabled" );
	return (SLAPI_DSE_CALLBACK_ERROR);
  }

  slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
  slapi_pblock_get( pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
  schema_dse_lock_write();

  /*
   * Process each modification.  Stop as soon as we hit an error.
   *
   * XXXmcs: known bugs: we don't operate on a copy of the schema, so it
   * is possible for some schema changes to be made but not all of them.
   * True for DS 4.x as well, although it tried to keep going even after
   * an error was detected (which was very wrong).
   */
  for (i = 0; rc == SLAPI_DSE_CALLBACK_OK && mods[i]; i++) {
	schema_dse_attr_name  = (char *) mods[i]->mod_type;
	num_mods++; /* incr the number of mods */

	/*
	 * skip attribute types that we do not recognize (the DSE code will
	 * handle them).
	 */
	if ( !schema_type_is_interesting( schema_dse_attr_name )) {
	  continue;
	}

	/*
	 * Delete an objectclass or attribute 
	 */
	if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
	  if (strcasecmp (mods[i]->mod_type, "objectclasses") == 0) {
		*returncode = schema_delete_objectclasses (entryBefore, mods[i],
					returntext, SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat );
	  }
	  else if (strcasecmp (mods[i]->mod_type, "attributetypes") == 0) {
		*returncode = schema_delete_attributes (entryBefore, mods[i],
					returntext, SLAPI_DSE_RETURNTEXT_SIZE );
	  }
	  else {
		*returncode= LDAP_NO_SUCH_ATTRIBUTE;
		schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				schema_errprefix_generic, mods[i]->mod_type, 
				"Only object classes and attribute types may be deleted" );
	  }

	  if ( LDAP_SUCCESS != *returncode ) {
		  rc= SLAPI_DSE_CALLBACK_ERROR;
	  } else {
		  reapply_mods = 1;
	  }
	}
	
	/*
	 * Replace an objectclass,attribute, or schema CSN
	 */
	else if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) {
	  int     replace_allowed = 0;
	  slapdFrontendConfig_t *slapdFrontendConfig;

	  slapdFrontendConfig = getFrontendConfig();
	  CFG_LOCK_READ( slapdFrontendConfig );
	  if ( 0 == strcasecmp( slapdFrontendConfig->schemareplace,
				CONFIG_SCHEMAREPLACE_STR_ON )) {
			replace_allowed = 1;
	  } else if ( 0 == strcasecmp( slapdFrontendConfig->schemareplace,
				CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY )) {
			replace_allowed =  is_replicated_operation;
	  }
	  CFG_UNLOCK_READ( slapdFrontendConfig );

	  if ( !replace_allowed ) {
		  *returncode= LDAP_UNWILLING_TO_PERFORM;
		  schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					schema_errprefix_generic, mods[i]->mod_type, 
					"Replace is not allowed on the subschema subentry" );
		  rc = SLAPI_DSE_CALLBACK_ERROR;
	  } else {
		  if (strcasecmp (mods[i]->mod_type, "attributetypes") == 0) {
			/* 
			 * Replace all attribute types
			 */
			*returncode = schema_replace_attributes( pb, mods[i], returntext,
					SLAPI_DSE_RETURNTEXT_SIZE );
		  } else if (strcasecmp (mods[i]->mod_type, "objectclasses") == 0) {
                          
                          if (is_replicated_operation) {
                                  /* before accepting the schema checks if the local consumer schema is not
                                   * a superset of the supplier schema
                                   */
                                  if (schema_objectclasses_superset_check(mods[i]->mod_bvalues, OC_CONSUMER)) {
                                          
                                          schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                                  schema_errprefix_generic, mods[i]->mod_type,
                                                  "Replace is not possible, local consumer schema is a superset of the supplier" );
                                          slapi_log_error(SLAPI_LOG_FATAL, "schema",
                                                  "Local %s must not be overwritten (set replication log for additional info)\n",
                                                  mods[i]->mod_type);
                                          *returncode = LDAP_UNWILLING_TO_PERFORM;
                                  } else {
                                          /*
                                           * Replace all objectclasses
                                           */
                                          *returncode = schema_replace_objectclasses(pb, mods[i],
                                                  returntext, SLAPI_DSE_RETURNTEXT_SIZE);
                                  }                                
                         } else {
                                  /*
                                   * Replace all objectclasses
                                   */
                                  *returncode = schema_replace_objectclasses(pb, mods[i],
                                                  returntext, SLAPI_DSE_RETURNTEXT_SIZE);                                 
                         }
		  } else if (strcasecmp (mods[i]->mod_type, "nsschemacsn") == 0) {
			if (is_replicated_operation) {
				/* Update the schema CSN */
				if (mods[i]->mod_bvalues && mods[i]->mod_bvalues[0] &&
						mods[i]->mod_bvalues[0]->bv_val &&
						mods[i]->mod_bvalues[0]->bv_len > 0) {
					char new_csn_string[CSN_STRSIZE + 1];
					CSN *new_schema_csn;
					memcpy(new_csn_string, mods[i]->mod_bvalues[0]->bv_val,
							mods[i]->mod_bvalues[0]->bv_len);
					new_csn_string[mods[i]->mod_bvalues[0]->bv_len] = '\0';
					new_schema_csn = csn_new_by_string(new_csn_string);
					if (NULL != new_schema_csn) {
						g_set_global_schema_csn(new_schema_csn); /* csn is consumed */
					}
				}
			}
		  } else {
			*returncode= LDAP_UNWILLING_TO_PERFORM;	/* XXXmcs: best error? */
			schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
					schema_errprefix_generic, mods[i]->mod_type, 
					"Only object classes and attribute types may be replaced" );
		  }
	  }

	  if ( LDAP_SUCCESS != *returncode ) {
		  rc= SLAPI_DSE_CALLBACK_ERROR;
	  } else {
		  reapply_mods = 1; /* we have at least some modifications we need to reapply */
	  }
	}

	
	/* 
	 * Add an objectclass or attribute
	 */
	else if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
	  if (strcasecmp (mods[i]->mod_type, "attributetypes") == 0) {
		/* 
		 * Add a new attribute 
		 */
		*returncode = schema_add_attribute ( pb, mods[i], returntext,
				SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat );
	  }
	  else if (strcasecmp (mods[i]->mod_type, "objectclasses") == 0) {
		/*
		 * Add a new objectclass
		 */
		*returncode = schema_add_objectclass ( pb, mods[i], returntext,
				SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat );
	  }
	  else {
		if ( schema_ds4x_compat ) {
			*returncode= LDAP_NO_SUCH_ATTRIBUTE;
		} else {
			*returncode= LDAP_UNWILLING_TO_PERFORM;	/* XXXmcs: best error? */
		}
		schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
				schema_errprefix_generic, mods[i]->mod_type, 
				"Only object classes and attribute types may be added" );
	  }

	  if ( LDAP_SUCCESS != *returncode ) {
		  rc= SLAPI_DSE_CALLBACK_ERROR;
	  } else {
		  reapply_mods = 1; /* we have at least some modifications we need to reapply */
	  }
	}

	/* 
	** No value was specified to modify, the user probably tried 
	** to delete all attributetypes or all objectclasses, which
	** isn't allowed 
	*/
	if (!mods[i]->mod_vals.modv_strvals)
	{
	  if ( schema_ds4x_compat ) {
		  *returncode= LDAP_INVALID_SYNTAX;
	  } else {
		  *returncode= LDAP_UNWILLING_TO_PERFORM;	/* XXXmcs: best error? */
	  }
	  schema_create_errormsg( returntext, SLAPI_DSE_RETURNTEXT_SIZE,
			schema_errprefix_generic, mods[i]->mod_type, 
			"No target attribute type or object class specified" );
      rc= SLAPI_DSE_CALLBACK_ERROR;
	}
  }

  if(rc==SLAPI_DSE_CALLBACK_OK && reapply_mods)
  {
	CSN *new_schema_csn;
	int newindex = 0; /* mods array index */

	/* tell the "unholy" dse_modify code to reapply the mods and use
	   that result instead of the initial result; we must remove the attributes
	   we manage in this code from the mods
	*/
	slapi_pblock_set(pb, SLAPI_DSE_REAPPLY_MODS, (void *)&reapply_mods);

	/* because we are reapplying the mods, we want the entryAfter to
	   look just like the entryBefore, except that "our" attributes
	   will have been removed
	*/
	/* delete the mods from the mods array */
	for (i = 0; i < num_mods ; i++) {
		const char *attrname = mods[i]->mod_type;

		/* delete this attr from the entry */
		slapi_entry_attr_delete(entryAfter, attrname);

		if ( schema_type_is_interesting( attrname )) {
			mod_free(mods[i]);
			mods[i] = NULL;
		} else {
			/* add the original value of the attr back to the entry after */
			Slapi_Attr *origattr = NULL;
			Slapi_ValueSet *origvalues = NULL;
			slapi_entry_attr_find(entryBefore, attrname, &origattr);
			if (NULL != origattr) {
				slapi_attr_get_valueset(origattr, &origvalues);
				if (NULL != origvalues) {
					slapi_entry_add_valueset(entryAfter, attrname, origvalues);
					slapi_valueset_free(origvalues);
				}
			}
			mods[newindex++] = mods[i];
		}
	}
	mods[newindex] = NULL;

	/*
	 * Since we successfully updated the schema, we need to generate
	 * a new schema CSN for non-replicated operations.
	 */
	/* XXXmcs: I wonder if we should update the schema CSN even when no
	 * attribute types or OCs were changed?  That way, an administrator
	 * could force schema replication to occur by submitting a modify
	 * operation that did not really do anything, such as:
	 *
	 * dn:cn=schema
	 * changetype:modify
	 * replace:cn
	 * cn:schema
	 */
	if (!is_replicated_operation)
	{
		new_schema_csn = csn_new();
		if (NULL != new_schema_csn) {
			char csn_str[CSN_STRSIZE + 1];
			csn_set_replicaid(new_schema_csn, 0);
			csn_set_time(new_schema_csn, current_time());
			g_set_global_schema_csn(new_schema_csn);
			slapi_entry_attr_delete(entryBefore, "nsschemacsn");
			csn_as_string(new_schema_csn, PR_FALSE, csn_str);
			slapi_entry_add_string(entryBefore, "nsschemacsn", csn_str);
		}
	}
  }

  schema_dse_unlock();

  return rc;
}

CSN *
dup_global_schema_csn()
{
	CSN *schema_csn;

	schema_dse_lock_read();
	schema_csn = csn_dup ( g_get_global_schema_csn() );
	schema_dse_unlock();
	return schema_csn;
}

/*
 * Remove all attribute types and objectclasses from the entry and
 * then add back the user defined ones based on the contents of the
 * schema hash tables.
 *
 * Returns SLAPI_DSE_CALLBACK_OK is all goes well.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
static int
refresh_user_defined_schema( Slapi_PBlock *pb, Slapi_Entry *pschema_info_e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg /* not used */ )
{
	int rc;
	Slapi_PBlock *mypbptr = pb;
	Slapi_PBlock mypb;
	const CSN *schema_csn;
	PRUint32 schema_flags = DSE_SCHEMA_USER_DEFINED_ONLY;

	pblock_init(&mypb);

    slapi_entry_attr_delete( pschema_info_e, "objectclasses");
    slapi_entry_attr_delete( pschema_info_e, "attributetypes");

	/* for write callbacks, no pb is supplied, so use our own */
	if (!mypbptr) {
		mypbptr = &mypb;
	}

	slapi_pblock_set(mypbptr, SLAPI_SCHEMA_FLAGS, &schema_flags);
	rc = read_schema_dse(mypbptr, pschema_info_e, NULL, returncode, returntext, NULL);
	schema_csn = g_get_global_schema_csn();
	if (NULL != schema_csn) {
		char csn_str[CSN_STRSIZE + 1];
		slapi_entry_attr_delete(pschema_info_e, "nsschemacsn");
		csn_as_string(schema_csn, PR_FALSE, csn_str);
		slapi_entry_add_string(pschema_info_e, "nsschemacsn", csn_str);
	}
	pblock_done(&mypb);

	return rc;
}


/*  oc_add_nolock
 *  Add the objectClass newoc to the global list of objectclasses 
 */ 
static void
oc_add_nolock(struct objclass *newoc)
{
	struct objclass *poc;

	poc = g_get_global_oc_nolock();

	if ( NULL == poc ) {
		g_set_global_oc_nolock(newoc);
	} else {
		for ( ; (poc != NULL) && (poc->oc_next != NULL); poc = poc->oc_next) {
			;
		}
		poc->oc_next = newoc;
		newoc->oc_next = NULL;
	}
}

/*
 * Delete one or more objectClasses from our internal data structure.
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 * If an error occurs, explanatory text is copied into 'errorbuf'.
 *
 * This function should not send an LDAP result; that is the caller's
 * responsibility.
 */
static int 
schema_delete_objectclasses( Slapi_Entry *entryBefore, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize, int schema_ds4x_compat )
{
  int i;
  int rc = LDAP_SUCCESS;	/* optimistic */
  struct objclass *poc, *poc2,  *delete_oc = NULL;
  
  if ( NULL == mod->mod_bvalues ) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				NULL, "Cannot remove all schema object classes" );
		return LDAP_UNWILLING_TO_PERFORM;
  }

  for (i = 0; mod->mod_bvalues[i]; i++) {
	if ( LDAP_SUCCESS != ( rc = parse_oc_str (
				(const char *)mod->mod_bvalues[i]->bv_val, &delete_oc,
				errorbuf, errorbufsize, 0, 0, schema_ds4x_compat, NULL))) {
	  return rc;
	}

	oc_lock_write();

	if ((poc = oc_find_nolock(delete_oc->oc_name, NULL, PR_FALSE)) != NULL) {

	  /* check to see if any objectclasses inherit from this oc */
	  for (poc2 = g_get_global_oc_nolock(); poc2 != NULL; poc2 = poc2->oc_next) {
		if (poc2->oc_superior &&  
			(strcasecmp (poc2->oc_superior, delete_oc->oc_name) == 0)) {
		  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
					delete_oc->oc_name, "Cannot delete an object class"
					" which has child object classes" );
		  rc = LDAP_UNWILLING_TO_PERFORM;
		  goto unlock_and_return;
		}
	  }

	  if ( (poc->oc_flags & OC_FLAG_STANDARD_OC) == 0) {
		oc_delete_nolock (poc->oc_name);
	  }
	  
	  else {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				delete_oc->oc_name, "Cannot delete a standard object class" );
		rc = LDAP_UNWILLING_TO_PERFORM;
		goto unlock_and_return;
	  }
	}
	else {
	  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				delete_oc->oc_name, "Is unknown.  Cannot delete." );
	  rc = schema_ds4x_compat ? LDAP_NO_SUCH_OBJECT : LDAP_NO_SUCH_ATTRIBUTE;
	  goto unlock_and_return;
	}

	oc_free( &delete_oc );
    oc_unlock();
  }

  return rc;

unlock_and_return:
	oc_free( &delete_oc );
	oc_unlock();
	return rc;
}


static int
schema_return(int rc,struct sizedbuffer * psb1,struct sizedbuffer *psb2,struct sizedbuffer *psb3,struct sizedbuffer *psb4)
{
    sizedbuffer_destroy(psb1);
    sizedbuffer_destroy(psb2);
    sizedbuffer_destroy(psb3);
    sizedbuffer_destroy(psb4);
    return rc;
}

/*
 * Delete one or more attributeTypes from our internal data structure.
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 * If an error occurs, explanatory text is copied into 'errorbuf'.
 *
 * This function should not send an LDAP result; that is the caller's
 * responsibility.
 */
static int 
schema_delete_attributes ( Slapi_Entry *entryBefore, LDAPMod *mod,
		char *errorbuf, size_t errorbufsize)
{
  char *attr_ldif, *oc_list_type = "";
  asyntaxinfo *a;
  struct objclass *oc = NULL;
  int i, k, attr_in_use_by_an_oc = 0;
  struct sizedbuffer *psbAttrName= sizedbuffer_construct(BUFSIZ);
  struct sizedbuffer *psbAttrOid= sizedbuffer_construct(BUFSIZ);
  struct sizedbuffer *psbAttrSyntax= sizedbuffer_construct(BUFSIZ);

  if (NULL == mod->mod_bvalues) {
	schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
			NULL, "Cannot remove all schema attribute types" );
    return schema_return(LDAP_UNWILLING_TO_PERFORM,psbAttrOid,psbAttrName,
			psbAttrSyntax,NULL);
  }

  for (i = 0; mod->mod_bvalues[i]; i++) {
	attr_ldif =(char *)  mod->mod_bvalues[i]->bv_val;

	/* normalize the attr ldif */
	for ( k = 0; attr_ldif[k]; k++) {
	  if (attr_ldif[k] == '\'' ||
		  attr_ldif[k] == '('  ||
		  attr_ldif[k] == ')' ) {
		attr_ldif[k] = ' ';
	  }
	  attr_ldif[k] = tolower (attr_ldif[k]);

	}
	
    sizedbuffer_allocate(psbAttrName,strlen(attr_ldif));
    sizedbuffer_allocate(psbAttrOid,strlen(attr_ldif));
    sizedbuffer_allocate(psbAttrSyntax,strlen(attr_ldif));
	
	sscanf (attr_ldif, "%s name %s syntax %s",
			psbAttrOid->buffer, psbAttrName->buffer, psbAttrSyntax->buffer);
	if ((a = attr_syntax_get_by_name ( psbAttrName->buffer)) != NULL ) {
	  /* only modify attrs which were user defined */
	  if (a->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
				psbAttrName->buffer,
				"Cannot delete a standard attribute type" );
		attr_syntax_return( a );
        return schema_return(LDAP_UNWILLING_TO_PERFORM,psbAttrOid,psbAttrName,
				psbAttrSyntax,NULL);
	  }

	  /* Do not allow deletion if referenced by an object class. */
	  oc_lock_read();
	  attr_in_use_by_an_oc = 0;
	  for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next ) {
		if (NULL != oc->oc_required) {
		  for ( k = 0; oc->oc_required[k] != NULL; k++ ) {
			if ( 0 == slapi_attr_type_cmp( oc->oc_required[k], a->asi_name,
					SLAPI_TYPE_CMP_EXACT )) {
			  oc_list_type = "MUST";
			  attr_in_use_by_an_oc = 1;
			  break;
			}
		  }
		}

		if (!attr_in_use_by_an_oc && NULL != oc->oc_allowed) {
		  for ( k = 0; oc->oc_allowed[k] != NULL; k++ ) {
			if ( 0 == slapi_attr_type_cmp( oc->oc_allowed[k], a->asi_name,
					SLAPI_TYPE_CMP_EXACT )) {
			  oc_list_type = "MAY";
			  attr_in_use_by_an_oc = 1;
			  break;
			}
		  }
	    }

		if (attr_in_use_by_an_oc) {
		  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
				psbAttrName->buffer, "Is included in the %s list for object class %s.  Cannot delete.",
				oc_list_type, oc->oc_name );
		  break;
		}
	  }
	  oc_unlock();
	  if (attr_in_use_by_an_oc) {
		attr_syntax_return( a );
        return schema_return(LDAP_UNWILLING_TO_PERFORM,psbAttrOid,psbAttrName,
				psbAttrSyntax,NULL);
	  }

	  /* Delete it. */
	  attr_syntax_delete( a );
	  attr_syntax_return( a );
	}
	else {
	  /* unknown attribute */
	  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
				psbAttrName->buffer, "Is unknown.  Cannot delete." );
      return schema_return(LDAP_NO_SUCH_ATTRIBUTE,psbAttrOid,psbAttrName,
				psbAttrSyntax,NULL);
	}
  }

  return schema_return(LDAP_SUCCESS,psbAttrOid,psbAttrName,psbAttrSyntax,
			NULL);
}

static int
schema_add_attribute ( Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf,
		size_t errorbufsize, int schema_ds4x_compat )
{
  int i;
  char *attr_ldif;
/* LPXXX: Eventually, we should not allocate the buffers in parse_at_str
 * for each attribute, but use the same buffer for all.
 * This is not done yet, so it's useless to allocate buffers for nothing.
 */
/*   struct sizedbuffer *psbAttrName= sizedbuffer_construct(BUFSIZ); */
/*   struct sizedbuffer *psbAttrOid= sizedbuffer_construct(BUFSIZ); */
/*   struct sizedbuffer *psbAttrDesc= sizedbuffer_construct(BUFSIZ); */
/*   struct sizedbuffer *psbAttrSyntax= sizedbuffer_construct(BUFSIZ); */
  int status = 0;
  
  for (i = 0; LDAP_SUCCESS == status && mod->mod_bvalues[i]; i++) {
	PRUint32 nolock = 0; /* lock global resources during normal operation */
	attr_ldif = (char *) mod->mod_bvalues[i]->bv_val;

	status = parse_at_str(attr_ldif, NULL, errorbuf, errorbufsize,
				nolock, 1 /* user defined */, schema_ds4x_compat, 1);
	if ( LDAP_SUCCESS != status ) {
		break;		/* stop on first error */
	}
  }

  /* free everything */
/*   sizedbuffer_destroy(psbAttrOid); */
/*   sizedbuffer_destroy(psbAttrName); */
/*   sizedbuffer_destroy(psbAttrDesc); */
/*   sizedbuffer_destroy(psbAttrSyntax); */

  return status;
}

/*
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well)
 */
static int
add_oc_internal(struct objclass *pnew_oc, char *errorbuf, size_t errorbufsize,
		int schema_ds4x_compat, PRUint32 flags )
{
	struct objclass *oldoc_by_name, *oldoc_by_oid, *psup_oc = NULL;
	int redefined_oc = 0, rc=0;
	asyntaxinfo *pasyntaxinfo = 0;

	if (!(flags & DSE_SCHEMA_LOCKED))
		oc_lock_write();

	oldoc_by_name = oc_find_nolock (pnew_oc->oc_name, NULL, PR_FALSE);
	oldoc_by_oid = oc_find_nolock (pnew_oc->oc_oid, NULL, PR_FALSE);

	/* Check to see if the objectclass name and the objectclass oid are already
	 * in use by an existing objectclass. If an existing objectclass is already 
	 * using the name or oid, the name and the oid should map to the same objectclass.
	 * Otherwise, return an error.
	 */	   
	if ( oldoc_by_name != oldoc_by_oid ) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				pnew_oc->oc_name, "The name does not match the OID \"%s\". "
				"Another object class is already using the name or OID.",
				 pnew_oc->oc_oid);
		rc = LDAP_TYPE_OR_VALUE_EXISTS;
	}

	/* 
	 * Set a flag so we know if we are updating an existing OC definition.
	 */
	if ( !rc ) {
		if ( NULL != oldoc_by_name ) {
			redefined_oc = 1;
		} else {
			/*
			 * If we are not updating an existing OC, check that the new
			 * oid is not already in use.
			 */
			if ( NULL != oldoc_by_oid ) {
				schema_create_errormsg( errorbuf, errorbufsize,
						schema_errprefix_oc, pnew_oc->oc_name,
						"The OID \"%s\" is already used by the object class \"%s\"",
						pnew_oc->oc_oid, oldoc_by_oid->oc_name);
				rc = LDAP_TYPE_OR_VALUE_EXISTS;
			}
		}
	}

	/* check to see if the superior oc exists */
	if (!rc && pnew_oc->oc_superior &&
				((psup_oc = oc_find_nolock (pnew_oc->oc_superior, NULL, PR_FALSE)) == NULL)) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				pnew_oc->oc_name, "Superior object class \"%s\" does not exist",
				pnew_oc->oc_superior);
		rc = LDAP_TYPE_OR_VALUE_EXISTS;
	}
	
	/* inherit the attributes from the superior oc */
	if (!rc && psup_oc ) {
		if ( psup_oc->oc_required ) {
			charray_merge( &pnew_oc->oc_required, psup_oc->oc_required, 1 );
		}
		if ( psup_oc->oc_allowed ) {
			charray_merge ( &pnew_oc->oc_allowed, psup_oc->oc_allowed, 1 );
		}
	}

	/* check to see if the oid is already in use by an attribute */
	if (!rc && (pasyntaxinfo = attr_syntax_get_by_oid(pnew_oc->oc_oid))) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				pnew_oc->oc_name,
				"The OID \"%s\" is also used by the attribute type \"%s\"",
				pnew_oc->oc_oid, pasyntaxinfo->asi_name);
		rc = LDAP_TYPE_OR_VALUE_EXISTS;
		attr_syntax_return( pasyntaxinfo );
	}
	
	/* check to see if the objectclass name is valid */
	if (!rc && !(flags & DSE_SCHEMA_NO_CHECK) &&
		schema_check_name ( pnew_oc->oc_name, PR_FALSE, errorbuf, errorbufsize )
			== 0 ) {
		rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;
	}

	/* check to see if the oid is valid */
	if (!rc && !(flags & DSE_SCHEMA_NO_CHECK))
	{
	    struct sizedbuffer *psbOcOid, *psbOcName;
	
	    psbOcName = sizedbuffer_construct(strlen(pnew_oc->oc_name) + 1);
	    psbOcOid = sizedbuffer_construct(strlen(pnew_oc->oc_oid) + 1);
	    strcpy(psbOcName->buffer, pnew_oc->oc_name);
	    strcpy(psbOcOid->buffer, pnew_oc->oc_oid);

	    if (!schema_check_oid ( psbOcName->buffer, psbOcOid->buffer, PR_FALSE,
					errorbuf, errorbufsize))
			rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;

	    sizedbuffer_destroy(psbOcName);
	    sizedbuffer_destroy(psbOcOid);	
	}

	/* check to see if the oc's attributes are valid */
	if (!rc && !(flags & DSE_SCHEMA_NO_CHECK) &&
		schema_check_oc_attrs ( pnew_oc, errorbuf, errorbufsize,
								0 /* don't strip options */ ) == 0 ) {
		rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;
	}
	/* insert new objectclass exactly where the old one one in the linked list*/
	if ( !rc && redefined_oc ) {
		pnew_oc->oc_flags |= OC_FLAG_REDEFINED_OC;
        rc=oc_replace_nolock( pnew_oc->oc_name, pnew_oc);
	}

	if (!rc && !redefined_oc ) {
		oc_add_nolock(pnew_oc);
	}

	if (!rc && redefined_oc ) {
		oc_update_inheritance_nolock( pnew_oc );
	}

	if (!(flags & DSE_SCHEMA_LOCKED))
		oc_unlock();
	return rc;
}


/*
 * Process a replace modify suboperation for attributetypes.
 *
 * XXXmcs: At present, readonly (bundled) schema definitions can't be
 * removed.  If that is attempted, we just keep them without generating
 * an error.
 *
 * Our algorithm is:
 *
 *   Clear the "keep" flags on the all existing attr. definitions.
 *
 *   For each replacement value:
 *      If the value exactly matches an existing schema definition,
 *      set that definition's keep flag.
 *
 *      Else if the OID in the replacement value matches an existing
 *      definition, delete the old definition and add the new one.  Set
 *      the keep flag on the newly added definition.
 *
 *      Else add the new definition.  Set the keep flag on the newly
 *      added definition.
 *
 *   For each definition that is not flagged keep, delete.
 *
 *   Clear all remaining "keep" flags.
 *
 * Note that replace was not supported at all before iDS 5.0.
 */
static int
schema_replace_attributes ( Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf,
		size_t errorbufsize )
{
	int                i, rc = LDAP_SUCCESS;
	struct asyntaxinfo *newasip, *oldasip;
	PRUint32           schema_flags = 0;

	if ( NULL == mod->mod_bvalues ) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
				NULL, "Cannot remove all schema attribute types" );
		return LDAP_UNWILLING_TO_PERFORM;
	}

	slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, &schema_flags);
	if (!(schema_flags & (DSE_SCHEMA_NO_LOAD|DSE_SCHEMA_NO_CHECK))) {
	    /* clear all of the "keep" flags unless it's from schema-reload */
		attr_syntax_all_clear_flag( SLAPI_ATTR_FLAG_KEEP );
	}

	for ( i = 0; mod->mod_bvalues[i] != NULL; ++i ) {
		if ( LDAP_SUCCESS != ( rc = parse_at_str( mod->mod_bvalues[i]->bv_val,
					&newasip, errorbuf, errorbufsize, 0, 1, 0, 0 ))) {
			goto clean_up_and_return;
		}

		/*
		 * Check for a match with an existing type and
		 * handle the various cases.
		 */
		if ( NULL == ( oldasip =
					attr_syntax_get_by_oid( newasip->asi_oid ))) {
			/* new attribute type */
			LDAPDebug( LDAP_DEBUG_TRACE, "schema_replace_attributes:"
					" new type %s (OID %s)\n",
					newasip->asi_name, newasip->asi_oid, 0 );
		} else {
			/* the name matches -- check the rest */
			if ( attr_syntax_equal( newasip, oldasip )) {
				/* unchanged attribute type -- just mark it as one to keep */
				oldasip->asi_flags |= SLAPI_ATTR_FLAG_KEEP;
				attr_syntax_free( newasip );
				newasip = NULL;
			} else {
				/* modified attribute type */
				LDAPDebug( LDAP_DEBUG_TRACE, "schema_replace_attributes:"
						" replacing type %s (OID %s)\n",
						newasip->asi_name, newasip->asi_oid, 0 );
				/* flag for deletion */
				attr_syntax_delete( oldasip );
			}

			attr_syntax_return( oldasip );
		}

		if ( NULL != newasip ) {	/* add new or replacement definition */
			rc = attr_syntax_add( newasip );
			if ( LDAP_SUCCESS != rc ) {
				schema_create_errormsg( errorbuf, errorbufsize,
						schema_errprefix_at, newasip->asi_name,
						"Could not be added (OID is \"%s\")",
						newasip->asi_oid );
				attr_syntax_free( newasip );
				goto clean_up_and_return;
			}

			newasip->asi_flags |= SLAPI_ATTR_FLAG_KEEP;
		}
	}

	/*
 	 * Delete all of the definitions that are not marked "keep" or "standard".
	 *
	 * XXXmcs: we should consider reporting an error if any read only types
	 * remain....
	 */
	attr_syntax_delete_all_not_flagged( SLAPI_ATTR_FLAG_KEEP | 
	                                    SLAPI_ATTR_FLAG_STD_ATTR );

clean_up_and_return:
	if (!(schema_flags & (DSE_SCHEMA_NO_LOAD|DSE_SCHEMA_NO_CHECK))) {
	    /* clear all of the "keep" flags unless it's from schema-reload */
		attr_syntax_all_clear_flag( SLAPI_ATTR_FLAG_KEEP );
	}

	return rc;
}


static int
schema_add_objectclass ( Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf,
		size_t errorbufsize, int schema_ds4x_compat )
{
	struct objclass *pnew_oc = NULL;
	char *newoc_ldif;
	int j, rc=0;

	for (j = 0; mod->mod_bvalues[j]; j++) {
		newoc_ldif  = (char *) mod->mod_bvalues[j]->bv_val;
		if ( LDAP_SUCCESS != (rc = parse_oc_str ( newoc_ldif, &pnew_oc,
					errorbuf, errorbufsize, 0, 1 /* user defined */,
					schema_ds4x_compat, NULL))) {
			oc_free( &pnew_oc );
			return rc;
		}

		if ( LDAP_SUCCESS != (rc = add_oc_internal(pnew_oc, errorbuf,
					errorbufsize, schema_ds4x_compat, 0/* no restriction */))) {
			oc_free( &pnew_oc );
			return rc;
		}

		normalize_oc();
	}

	return LDAP_SUCCESS;
}



/*
 * Process a replace modify suboperation for objectclasses.
 *
 * XXXmcs: At present, readonly (bundled) schema definitions can't be
 * removed.  If that is attempted, we just keep them without generating
 * an error.
 *
 * Our algorithm is:
 *
 *   Lock the global objectclass linked list.
 *
 *   Create a new empty (temporary) linked list, initially empty.
 *
 *   For each replacement value:
 *      If the value exactly matches an existing schema definition,
 *      move the existing definition from the current global list to the
 *      temporary list
 *
 *      Else if the OID in the replacement value matches an existing
 *      definition, delete the old definition from the current global
 *      list and add the new one to the temporary list.
 *
 *      Else add the new definition to the temporary list.
 * 
 *   Delete all definitions that remain on the current global list.
 *
 *   Make the temporary list the current global list.
 *
 * Note that since the objectclass definitions are stored in a linked list,
 * this algorithm is O(N * M) where N is the number of existing objectclass
 * definitions and M is the number of replacement definitions.
 * XXXmcs: Yuck.  We should use a hash table for the OC definitions.
 *
 * Note that replace was not supported at all by DS versions prior to 5.0
 */

static int
schema_replace_objectclasses ( Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf,
		size_t errorbufsize )
{
	struct objclass		*newocp, *curlisthead, *prevocp, *tmpocp;
	struct objclass		*newlisthead = NULL, *newlistend = NULL;
	int					i, rc = LDAP_SUCCESS;

	if ( NULL == mod->mod_bvalues ) {
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				NULL, "Cannot remove all schema object classes" );
		return LDAP_UNWILLING_TO_PERFORM;
	}

	oc_lock_write();

	curlisthead = g_get_global_oc_nolock();

	for ( i = 0; mod->mod_bvalues[i] != NULL; ++i ) {
		struct objclass *addocp = NULL;

		if ( LDAP_SUCCESS != ( rc = parse_oc_str( mod->mod_bvalues[i]->bv_val,
					&newocp, errorbuf, errorbufsize, DSE_SCHEMA_NO_GLOCK,
					1 /* user defined */, 0 /* no DS 4.x compat issues */ , NULL))) {
			rc = LDAP_INVALID_SYNTAX;
			goto clean_up_and_return;
		}

		prevocp = NULL;
		for ( tmpocp = curlisthead; tmpocp != NULL; tmpocp = tmpocp->oc_next ) {
			if ( 0 == strcasecmp( tmpocp->oc_oid, newocp->oc_oid ) ) {
				/* the names match -- remove from the current list */
				if ( tmpocp == curlisthead ) {
					curlisthead = tmpocp->oc_next;
					/* The global oc list is scanned in parse_oc_str above,
					   if there are multiple objectclasses to be updated.
					   Needs to maintain the list dynamically. */
					g_set_global_oc_nolock( curlisthead );
				} else {
					if (prevocp) prevocp->oc_next = tmpocp->oc_next;
				}
				tmpocp->oc_next = NULL;

				/* check for a full match */
				if ( oc_equal( tmpocp, newocp )) {
					/* no changes: keep existing definition and discard new */
					oc_free( &newocp );
					addocp = tmpocp;
				} else {
					/* some differences: discard old and keep the new one */
					oc_free( &tmpocp );
					LDAPDebug( LDAP_DEBUG_TRACE, "schema_replace_objectclasses:"
							" replacing object class %s (OID %s)\n", 
							newocp->oc_name, newocp->oc_oid, 0 );
					addocp = newocp;
				}
				break;	/* we found it -- exit the loop */

			}
			prevocp = tmpocp;
		}

		if ( NULL == addocp ) {
			LDAPDebug( LDAP_DEBUG_TRACE, "schema_replace_objectclasses:"
					" new object class %s (OID %s)\n", 
					newocp->oc_name, newocp->oc_oid, 0 );
			addocp = newocp;
		}

		/* add the objectclass to the end of the new list */
		if ( NULL != addocp ) {
			if ( NULL == newlisthead ) {
				newlisthead = addocp;
			} else {
				newlistend->oc_next = addocp;
			}
			newlistend = addocp;
		}
	}

clean_up_and_return:
	if ( LDAP_SUCCESS == rc ) {
		/*
		 * Delete all remaining OCs that are on the old list AND are not
		 * "standard" classes.
		 */
		struct objclass	*nextocp;

		prevocp = NULL;
		for ( tmpocp = curlisthead; tmpocp != NULL; tmpocp = nextocp ) {
			if ( 0 == ( tmpocp->oc_flags & OC_FLAG_STANDARD_OC )) {
				/* not a standard definition -- remove it */
				if ( tmpocp == curlisthead ) {
					curlisthead = tmpocp->oc_next;
				} else {
					if (prevocp) {
						prevocp->oc_next = tmpocp->oc_next;
					}
				}
				nextocp = tmpocp->oc_next;
				oc_free( &tmpocp );
			} else {
				/*
				 * XXXmcs: we could generate an error, but for now we do not.
				 */
				nextocp = tmpocp->oc_next;
				prevocp = tmpocp;

#if 0

				schema_create_errormsg( errorbuf, errorbufsize,
						schema_errprefix_oc, tmpocp->oc_name,
						"Cannot delete a standard object class" );
				rc = LDAP_UNWILLING_TO_PERFORM;
				break;
#endif
			}
		}
	}

	/*
	 * Combine the two lists by adding the new list to the end of the old
	 * one.
	 */
	if ( NULL != curlisthead ) {
		for ( tmpocp = curlisthead; tmpocp->oc_next != NULL;
					tmpocp = tmpocp->oc_next ) {
			;/*NULL*/
		}
		tmpocp->oc_next = newlisthead;
		newlisthead = curlisthead;
	}

	/*
	 * Install the new list as the global one, replacing the old one.
	 */
	g_set_global_oc_nolock( newlisthead );

	oc_unlock();
	return rc;
}

schemaext *
schema_copy_extensions(schemaext *extensions)
{
    schemaext *ext = NULL, *head = NULL;
    while(extensions){
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
        newext->term = slapi_ch_strdup(extensions->term);
        newext->values = charray_dup(extensions->values);
        newext->value_count = extensions->value_count;
        if(ext == NULL){
            ext = newext;
            head = newext;
        } else {
            ext->next = newext;
            ext = newext;
        }
        extensions = extensions->next;
    }

    return head;
}

void
schema_free_extensions(schemaext *extensions)
{
    if(extensions){
        schemaext *prev;

        while(extensions){
            slapi_ch_free_string(&extensions->term);
            charray_free(extensions->values);
            prev = extensions;
            extensions = extensions->next;
            slapi_ch_free( (void **)&prev);
        }
    }
}

static void
oc_free( struct objclass **ocp )
{
	struct objclass	*oc;

	if ( NULL != ocp && NULL != *ocp ) {
		oc = *ocp;
		slapi_ch_free( (void **)&oc->oc_name );
		slapi_ch_free( (void **)&oc->oc_desc );
		slapi_ch_free( (void **)&oc->oc_oid );
		slapi_ch_free( (void **)&oc->oc_superior );
		charray_free( oc->oc_required );
		charray_free( oc->oc_allowed );
		charray_free( oc->oc_orig_required );
		charray_free( oc->oc_orig_allowed );
		schema_free_extensions( oc->oc_extensions );
		slapi_ch_free( (void **)&oc );
		*ocp = NULL;
	}
}

#if !defined (USE_OPENLDAP)

/*
 * read_oc_ldif_return
 * Free all the memory that read_oc_ldif() allocated, and return the retVal
 *
 * It's nice to do all the freeing in one spot, as read_oc_ldif() returns sideways
 */

static int 
read_oc_ldif_return( int retVal,
					 char *oid,
					 struct sizedbuffer *name,
					 char *sup,
					 char *desc )
{
  slapi_ch_free((void **)&oid);
  sizedbuffer_destroy( name );
  slapi_ch_free((void **)&sup);
  slapi_ch_free((void **)&desc);
  return retVal;
}

/*
 * read_oc_ldif
 * Read the value of the objectclasses attribute in cn=schema, convert it
 * into an objectclass struct.
 *
 * Arguments:  
 *
 *     input             : value of objectclasses attribute to read
 *     oc                : pointer write the objectclass to
 *     errorbuf          : buffer to write any errors to
 *     is_user_defined   : if non-zero, force objectclass to be user defined
 *     schema_flags      : Any or none of the following bits could be set
 *                         DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *                         DSE_SCHEMA_NO_GLOCK -- don't lock global resources
 *                         DSE_SCHEMA_LOCKED   -- already locked with
 *                                                reload_schemafile_lock;
 *                                                no further lock needed
 *     schema_ds4x_compat: if non-zero, act like Netscape DS 4.x
 *
 * Returns: an LDAP error code
 *  
 *       LDAP_SUCCESS if the objectclass was sucessfully read, the new 
 *         objectclass will be written to oc
 *  
 *       All others:  there was an error, an error message will 
 *         be written to errorbuf
 */
static int
read_oc_ldif ( const char *input, struct objclass **oc, char *errorbuf,
		size_t errorbufsize, PRUint32 schema_flags, int is_user_defined,
		int schema_ds4x_compat )
{
  int i, j;
  const char *pstart, *nextinput;
  struct objclass *pnew_oc, *psup_oc;
  char **RequiredAttrsArray, **AllowedAttrsArray;
  char **OrigRequiredAttrsArray, **OrigAllowedAttrsArray;
  char *pend, *pOcOid, *pOcSup, *pOcDesc;
  struct sizedbuffer *psbOcName= sizedbuffer_construct(BUFSIZ);
  PRUint8 kind, flags;
  int invalid_syntax_error;
  schema_strstr_fn_t keyword_strstr_fn;
  schemaext *extensions = NULL;

  /*
   * From RFC 2252 section 4.4:
   *
   *      ObjectClassDescription = "(" whsp
   *          numericoid whsp      ; ObjectClass identifier
   *          [ "NAME" qdescrs ]
   *          [ "DESC" qdstring ]
   *          [ "OBSOLETE" whsp ]
   *          [ "SUP" oids ]       ; Superior ObjectClasses
   *          [ ( "ABSTRACT" / "STRUCTURAL" / "AUXILIARY" ) whsp ]
   *                               ; default structural
   *          [ "MUST" oids ]      ; AttributeTypes
   *          [ "MAY" oids ]       ; AttributeTypes
   *      whsp ")"
   *
   * XXXmcs: Our parsing technique is poor.  In (Netscape) DS 4.12 and earlier
   * releases, parsing was mostly done by looking anywhere within the input
   * string for various keywords such as "MUST".  But if, for example, a
   * description contains the word "must", the parser would take assume that
   * the tokens following the word were attribute types or OIDs.  Bad news.
   *
   * In iDS 5.0 and later, we parse in order left to right and advance a
   * pointer as we consume the input string (the nextinput variable).  We
   * also use a case-insensitive search when looking for keywords such as
   * DESC.  But the parser will still be fooled by sequences like:
   *
   *		( 1.2.3.4 NAME 'testOC' MUST ( DESC cn ) )
   *
   * Someday soon we will need to write a real parser.
   *
   * Compatibility notes: if schema_ds4x_compat is set, we:
   *   1. always parse from the beginning of the string
   *   2. use a case-insensitive compare when looking for keywords, e.g., MUST
   */

  if ( schema_ds4x_compat ) {
	keyword_strstr_fn = PL_strcasestr;
	invalid_syntax_error = LDAP_OPERATIONS_ERROR;
  } else {
	keyword_strstr_fn = PL_strstr;
	invalid_syntax_error = LDAP_INVALID_SYNTAX;
  }

  flags = 0;
  pOcOid = pOcSup = pOcDesc = NULL;

  if ( NULL == input || '\0' == input[0] ) {
	
	schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc, NULL,
			"One or more values are required for the objectClasses attribute" );
	LDAPDebug ( LDAP_DEBUG_ANY, "NULL args passed to read_oc_ldif\n",0,0,0);
	return read_oc_ldif_return( LDAP_OPERATIONS_ERROR, pOcOid, psbOcName,
			pOcSup, pOcDesc );
  }

  nextinput = input;

  /* look for the OID */
  if ( NULL == ( pOcOid = get_tagged_oid( "(", &nextinput,
			keyword_strstr_fn ))) {
	schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
			input, "Value is malformed. It must include a \"(\"");
	return read_oc_ldif_return( invalid_syntax_error, pOcOid, psbOcName,
			pOcSup, pOcDesc );
  }

  if ( schema_ds4x_compat || ( strcasecmp(pOcOid, "NAME") == 0))
	  nextinput = input;

  /* look for the NAME */
  if ( (pstart = (*keyword_strstr_fn)(nextinput, "NAME '")) != NULL ) {
	pstart += 6;
	sizedbuffer_allocate(psbOcName,strlen(pstart)+1);
	if ( sscanf ( pstart, "%s", psbOcName->buffer ) > 0 ) {
		/* strip the trailing single quote */
		if ( psbOcName->buffer[strlen(psbOcName->buffer)-1] == '\'' ) {
		   psbOcName->buffer[strlen(psbOcName->buffer)-1] = '\0';
		   nextinput = pstart + strlen(psbOcName->buffer) + 1;
		} else {
		  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
			input, "Value is malformed. It must include a single quote around"
			" the name" );
		  return read_oc_ldif_return( invalid_syntax_error, pOcOid, psbOcName,
			pOcSup, pOcDesc );
		}
	}
  } else {
	  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
			input, "Value is malformed. It must include a \"NAME '\"");
	  return read_oc_ldif_return( invalid_syntax_error, pOcOid, psbOcName,
			pOcSup, pOcDesc );
  }

  /* 
  ** if the objectclass ldif doesn't have an OID, we'll make the oid
  ** ocname-oid
  */
  if ( strcasecmp ( pOcOid, "NAME" ) == 0 ) {
	slapi_ch_free_string( &pOcOid );
	pOcOid = slapi_ch_smprintf("%s-oid", psbOcName->buffer );
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for an optional DESCription */
  if ( (pstart = (*keyword_strstr_fn) ( nextinput, " DESC '")) != NULL ) {
	pstart += 7;
	if (( pend = strchr( pstart, '\'' )) == NULL ) {
		pend = (char *)(pstart + strlen(pstart));
	}
	pOcDesc = slapi_ch_malloc( pend - pstart + 1 );
	memcpy( pOcDesc, pstart, pend - pstart );
	pOcDesc[ pend - pstart ] = '\0';
	nextinput = pend + 1;
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for the optional OBSOLETE marker */
  flags |= get_flag_keyword( schema_obsolete_with_spaces,
			OC_FLAG_OBSOLETE, &nextinput, keyword_strstr_fn );

  if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
	oc_lock_read();		/* needed because we access the superior oc */
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /*
   * Look for the superior objectclass.  We first look for a parenthesized
   * list and if not found we look for a simple OID.
   *
   * XXXmcs: Since we do not yet support multiple superior objectclasses, we
   * just grab the first OID in a parenthesized list.
   */
  if ( NULL == ( pOcSup = get_tagged_oid( " SUP (", &nextinput,
				keyword_strstr_fn ))) {
      pOcSup = get_tagged_oid( " SUP ", &nextinput, keyword_strstr_fn );
  }
  psup_oc = oc_find_nolock ( pOcSup, NULL, PR_FALSE);

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for the optional kind (ABSTRACT, STRUCTURAL, AUXILIARY) */
  for ( i = 0; i < SCHEMA_OC_KIND_COUNT; ++i ) {
	if ( NULL != ( pstart = (*keyword_strstr_fn)( nextinput,
			schema_oc_kind_strings_with_spaces[i] ))) {
	  kind = i;
	  nextinput = pstart + strlen( schema_oc_kind_strings_with_spaces[i] ) - 1;
	  break;
	}
  }
  if ( i >= SCHEMA_OC_KIND_COUNT ) {    /* not found */
	if ( NULL != psup_oc && OC_KIND_ABSTRACT != psup_oc->oc_kind ) {
		/* inherit kind from superior class if not ABSTRACT */
		kind = psup_oc->oc_kind;
	} else {
		/* according to RFC 2252, the default is structural */
		kind = OC_KIND_STRUCTURAL;
	}
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for required attributes (MUST) */
  if ( (pstart = (*keyword_strstr_fn) (nextinput, " MUST ")) != NULL ) {
	char *pRequiredAttrs;
	int saw_open_paren = 0;

	pstart += 6;
	pstart = skipWS( pstart ); /* skip past any extra white space */
	if ( *pstart == '(' ) {
		saw_open_paren = 1;
		++pstart;
	}
	pRequiredAttrs = slapi_ch_strdup ( pstart );
	if ( saw_open_paren && (pend = strchr (pRequiredAttrs, ')')) != NULL ) {
		*pend = '\0';
	} else if ((pend = strchr (pRequiredAttrs, ' ' )) != NULL ) {
		*pend = '\0';
	} else {
		pend = pRequiredAttrs + strlen(pRequiredAttrs);	/* at end of string */
	}
	nextinput = pstart + ( pend - pRequiredAttrs );
	RequiredAttrsArray = read_dollar_values (pRequiredAttrs);
	slapi_ch_free((void**)&pRequiredAttrs);
  } else {
	RequiredAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
	RequiredAttrsArray[0] = NULL;
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for allowed attributes (MAY) */
  if ( (pstart = (*keyword_strstr_fn) (nextinput, " MAY ")) != NULL ) {
	char *pAllowedAttrs;
	int saw_open_paren = 0;

	pstart += 5;
	pstart = skipWS( pstart ); /* skip past any extra white space */
	if ( *pstart == '(' ) {
		saw_open_paren = 1;
		++pstart;
	}
	pAllowedAttrs = slapi_ch_strdup ( pstart );
	if ( saw_open_paren && (pend = strchr (pAllowedAttrs, ')')) != NULL ) {
		*pend = '\0';
	} else if ((pend = strchr (pAllowedAttrs, ' ' )) != NULL ) {
		*pend = '\0';
	} else {
		pend = pAllowedAttrs + strlen(pAllowedAttrs);	/* at end of string */
	}
	nextinput = pstart + ( pend - pAllowedAttrs );
	AllowedAttrsArray = read_dollar_values (pAllowedAttrs);
	slapi_ch_free((void**)&pAllowedAttrs);
  } else {
	AllowedAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
	AllowedAttrsArray[0] = NULL;
  }

  if ( schema_ds4x_compat ) nextinput = input;

  /* look for X-ORIGIN list */
  if (is_user_defined) {
      /* add X-ORIGIN 'user defined' */
      extensions = parse_extensions( nextinput, schema_user_defined_origin );
      flags |= OC_FLAG_USER_OC;
  } else {
      /* add nothing */
	  extensions = parse_extensions( nextinput, NULL );
	  flags |= OC_FLAG_STANDARD_OC;
  }

  /* generate OrigRequiredAttrsArray and OrigAllowedAttrsArray */
  if (psup_oc) {
	int found_it;

	OrigRequiredAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
	OrigRequiredAttrsArray[0] = NULL;
	OrigAllowedAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
	OrigAllowedAttrsArray[0] = NULL;

	if (psup_oc->oc_required) {
	  for (i = 0; RequiredAttrsArray[i]; i++) {
		for (j = 0, found_it = 0; psup_oc->oc_required[j]; j++) {
		  if (strcasecmp (psup_oc->oc_required[j], RequiredAttrsArray[i]) == 0) {
			found_it = 1;
		  }
		}
		if (!found_it) {
		  charray_add (&OrigRequiredAttrsArray, slapi_ch_strdup ( RequiredAttrsArray[i] ) );
		}
	  }
	}
	if (psup_oc->oc_allowed) {
	  for (i = 0; AllowedAttrsArray[i]; i++) {
		for (j = 0, found_it=0; psup_oc->oc_allowed[j]; j++) {
		  if (strcasecmp (psup_oc->oc_allowed[j], AllowedAttrsArray[i]) == 0) {
			found_it = 1;
		  }
		}
		if (!found_it) {
		  charray_add (&OrigAllowedAttrsArray, slapi_ch_strdup (AllowedAttrsArray[i]) );
		}
	  }
	}
  }
  else {
	/* if no parent oc */
	OrigRequiredAttrsArray = charray_dup ( RequiredAttrsArray );
	OrigAllowedAttrsArray = charray_dup ( AllowedAttrsArray );
  }

  if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
	  oc_unlock();	/* we are done accessing superior oc (psup_oc) */
  }

  /* finally -- create new objclass structure */
  pnew_oc = (struct objclass *) slapi_ch_malloc (1 * sizeof (struct objclass));
  pnew_oc->oc_name = slapi_ch_strdup ( psbOcName->buffer );
  pnew_oc->oc_superior = pOcSup;
  pOcSup = NULL;		/* don't free this later */
  pnew_oc->oc_oid = pOcOid;
  pOcOid = NULL;		/* don't free this later */
  pnew_oc->oc_desc = pOcDesc;
  pOcDesc = NULL;		/* don't free this later */
  pnew_oc->oc_required = RequiredAttrsArray;
  pnew_oc->oc_allowed = AllowedAttrsArray;
  pnew_oc->oc_orig_required = OrigRequiredAttrsArray;
  pnew_oc->oc_orig_allowed =  OrigAllowedAttrsArray;
  pnew_oc->oc_extensions = extensions;
  pnew_oc->oc_next = NULL;
  pnew_oc->oc_flags = flags;
  pnew_oc->oc_kind = kind;

  *oc = pnew_oc;

  return read_oc_ldif_return( LDAP_SUCCESS, pOcOid, psbOcName, pOcSup, pOcDesc );
}

static char **read_dollar_values ( char *vals) {
  int i,k;
  char **retVal;
  static const char *charsToRemove = " ()";

  /* get rid of all the parens and spaces */
  for ( i = 0, k = 0; vals[i]; i++) {
	if (!strchr(charsToRemove, vals[i])) {
	  vals[k++] = vals[i];
	}
  }
  vals[k] = '\0';
  retVal = slapi_str2charray (vals, "$");
  return retVal;
}

/*
 * if asipp is NULL, the attribute type is added to the global set of schema.
 * if asipp is not NULL, the AT is not added but *asipp is set.  When you are
 * finished with *asipp, use attr_syntax_free() to dispose of it.
 * 
 *    schema_flags: Any or none of the following bits could be set
 *        DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *        DSE_SCHEMA_NO_GLOCK -- locking of global resources is turned off;
 *                               this saves time during initialization since 
 *                               the server operates in single threaded mode 
 *                               at that time or in reload_schemafile_lock.
 *        DSE_SCHEMA_LOCKED   -- already locked with reload_schemafile_lock;
 *                               no further lock needed
 *
 * if is_user_defined is true, force attribute type to be user defined.
 *
 * returns an LDAP error code (LDAP_SUCCESS if all goes well)
*/
static int
read_at_ldif(const char *input, struct asyntaxinfo **asipp, char *errorbuf,
        size_t errorbufsize, PRUint32 schema_flags, int is_user_defined,
        int schema_ds4x_compat, int is_remote)
{
    char *pStart, *pEnd;
    char *pOid, *pSyntax, *pSuperior, *pMREquality, *pMROrdering, *pMRSubstring;
    const char *nextinput;
    struct sizedbuffer *psbAttrName= sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbAttrDesc= sizedbuffer_construct(BUFSIZ);
    int status = 0;
    int syntaxlength;
    char **attr_names = NULL;
    char *first_attr_name = NULL;
    int num_names = 0;
    unsigned long flags = SLAPI_ATTR_FLAG_OVERRIDE;
    const char *ss = 0;
    struct asyntaxinfo    *tmpasip;
    int invalid_syntax_error;
    schema_strstr_fn_t keyword_strstr_fn;
    schemaext *extensions = NULL;

  /*
   * From RFC 2252 section 4.2:
   *
   *      AttributeTypeDescription = "(" whsp
   *            numericoid whsp              ; AttributeType identifier
   *          [ "NAME" qdescrs ]             ; name used in AttributeType
   *          [ "DESC" qdstring ]            ; description
   *          [ "OBSOLETE" whsp ]
   *          [ "SUP" woid ]                 ; derived from this other
   *                                         ; AttributeType
   *          [ "EQUALITY" woid              ; Matching Rule name
   *          [ "ORDERING" woid              ; Matching Rule name
   *          [ "SUBSTR" woid ]              ; Matching Rule name
   *          [ "SYNTAX" whsp noidlen whsp ] ; see section 4.3
   *          [ "SINGLE-VALUE" whsp ]        ; default multi-valued
   *          [ "COLLECTIVE" whsp ]          ; default not collective
   *          [ "NO-USER-MODIFICATION" whsp ]; default user modifiable
   *          [ "USAGE" whsp AttributeUsage ]; default userApplications
   *          whsp ")"
   *
   *      AttributeUsage =
   *          "userApplications"     /
   *          "directoryOperation"   /
   *          "distributedOperation" / ; DSA-shared
   *          "dSAOperation"          ; DSA-specific, value depends on server
   *
   * XXXmcs: Our parsing technique is poor.  In (Netscape) DS 4.12 and earlier
   * releases, parsing was mostly done by looking anywhere within the input
   * string for various keywords such as "EQUALITY".  But if, for example, a
   * description contains the word "equality", the parser would take assume
   * that the token following the word was a matching rule.  Bad news.
   *
   * In iDS 5.0 and later, we parse in order left to right and advance a
   * pointer as we consume the input string (the nextinput variable).  We
   * also use a case-insensitive search when looking for keywords such as
   * DESC.  This is still less than ideal.
   *
   * Someday soon we will need to write a real parser.
   *
   * Compatibility notes: if schema_ds4x_compat is set, we:
   *   1. always parse from the beginning of the string
   *   2. use a case-insensitive compare when looking for keywords, e.g., DESC
   */

    if ( schema_ds4x_compat ) {
        keyword_strstr_fn = PL_strcasestr;
        invalid_syntax_error = LDAP_OPERATIONS_ERROR;
    } else {
        keyword_strstr_fn = PL_strstr;
        invalid_syntax_error = LDAP_INVALID_SYNTAX;
    }

    if (schema_flags & DSE_SCHEMA_NO_GLOCK)
        flags |= SLAPI_ATTR_FLAG_NOLOCKING;

    psbAttrName->buffer[0] = '\0';
    psbAttrDesc->buffer[0] = '\0';
    pOid = pSyntax = pSuperior = NULL;
    pMREquality = pMROrdering = pMRSubstring = NULL;
    syntaxlength = SLAPI_SYNTAXLENGTH_NONE;

    nextinput = input;

    /* get the OID */
        pOid = get_tagged_oid( "(", &nextinput, keyword_strstr_fn );

    if (NULL == pOid) {
      schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                  input, "Missing or invalid OID" );
      status = invalid_syntax_error;
      goto done;
    }

    if ( schema_ds4x_compat || (strcasecmp(pOid, "NAME") == 0))
        nextinput = input;

    /* look for the NAME (single or list of names) */
    if ( (pStart = (*keyword_strstr_fn) ( nextinput, "NAME ")) != NULL ) {
        pStart += 5;
        sizedbuffer_allocate(psbAttrName,strlen(pStart)+1);
        strcpy ( psbAttrName->buffer, pStart);
        if (*pStart == '(')
            pEnd = strchr(psbAttrName->buffer, ')');
        else
            pEnd = strchr(psbAttrName->buffer+1, '\'');
        if (pEnd)
            *(pEnd+1) = 0;
        nextinput = pStart + strlen(psbAttrName->buffer) + 1;
        attr_names = parse_qdescrs(psbAttrName->buffer, &num_names);
        if ( NULL != attr_names ) {
            first_attr_name = attr_names[0];
        } else { /* NAME followed by nothing violates syntax */
            schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                                    input, "Missing or invalid attribute name" );
            status = invalid_syntax_error;
            goto done;
        }
    }

    if ( schema_ds4x_compat ) nextinput = input;

    /*
     * if the attribute ldif doesn't have an OID, we'll make the oid 
     * attrname-oid 
     */ 
    if ( (strcasecmp ( pOid, "NAME" ) == 0) && (first_attr_name)) { 
        slapi_ch_free_string( &pOid ); 
        pOid = slapi_ch_smprintf("%s-oid", first_attr_name ); 
    } 

    /* look for the optional DESCription */
    if ( (pStart = (*keyword_strstr_fn) ( nextinput, "DESC '")) != NULL ) {
        pStart += 6;
        sizedbuffer_allocate(psbAttrDesc,strlen(pStart)+1);
        strcpy ( psbAttrDesc->buffer, pStart);
        if ( (pEnd = strchr (psbAttrDesc->buffer, '\'' )) != NULL ){
            *pEnd ='\0';
        }
        nextinput = pStart + strlen(psbAttrDesc->buffer) + 1;
    }

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional OBSOLETE marker */
    flags |= get_flag_keyword( schema_obsolete_with_spaces,
            SLAPI_ATTR_FLAG_OBSOLETE, &nextinput, keyword_strstr_fn );

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional SUPerior type */
    pSuperior = get_tagged_oid( "SUP ", &nextinput, keyword_strstr_fn );

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional matching rules */
    pMREquality = get_tagged_oid( "EQUALITY ", &nextinput, keyword_strstr_fn );
    if ( schema_ds4x_compat ) nextinput = input;
    pMROrdering = get_tagged_oid( "ORDERING ", &nextinput, keyword_strstr_fn );
    if ( schema_ds4x_compat ) nextinput = input;
    pMRSubstring = get_tagged_oid( "SUBSTR ", &nextinput, keyword_strstr_fn );
    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional SYNTAX */
    if ( NULL != ( pSyntax = get_tagged_oid( "SYNTAX ", &nextinput,
            keyword_strstr_fn ))) {
        /*
         * Check for an optional {LEN}, which if present indicates a
         * suggested maximum size for values of this attribute type.
         *
         * XXXmcs: we do not enforce length restrictions, but we do read
         * and include them in the subschemasubentry.
         */
        if ( (pEnd = strchr ( pSyntax, '{')) != NULL /* balance } */ ) {
            *pEnd = '\0';
            syntaxlength = atoi( pEnd + 1 );
        }
    }

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional SINGLE-VALUE marker */
    flags |= get_flag_keyword( " SINGLE-VALUE ",
            SLAPI_ATTR_FLAG_SINGLE, &nextinput, keyword_strstr_fn );

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional COLLECTIVE marker */
    flags |= get_flag_keyword( schema_collective_with_spaces,
            SLAPI_ATTR_FLAG_COLLECTIVE, &nextinput, keyword_strstr_fn );

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional NO-USER-MODIFICATION marker */
    flags |= get_flag_keyword( schema_nousermod_with_spaces,
            SLAPI_ATTR_FLAG_NOUSERMOD, &nextinput, keyword_strstr_fn );

    if ( schema_ds4x_compat ) nextinput = input;

    /* look for the optional USAGE */
    if (NULL != (ss = (*keyword_strstr_fn)(nextinput, " USAGE "))) {
        ss += 7;
        ss = skipWS(ss);
        if (ss) {
            if ( !PL_strncmp(ss, "directoryOperation",
                    strlen("directoryOperation"))) {
                flags |= SLAPI_ATTR_FLAG_OPATTR;
            }
            if ( NULL == ( nextinput = strchr( ss, ' ' ))) {
                nextinput = ss + strlen(ss);
            }
        }
    }

    if ( schema_ds4x_compat ) nextinput = input;

    /* X-ORIGIN list */
    if (is_user_defined) {
        /* add X-ORIGIN 'user defined' */
        extensions = parse_extensions( nextinput, schema_user_defined_origin );
    } else {
        /* add nothing extra*/
        extensions = parse_extensions( nextinput, NULL );
        flags |= SLAPI_ATTR_FLAG_STD_ATTR;
    }

    /* Do some sanity checking to make sure everything was read correctly */
    
    if (NULL == pOid) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name, "Missing OID" );
        status = invalid_syntax_error;
    }
    if (!status && (!attr_names || !num_names)) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name,
                "Missing name (OID is \"%s\")", pOid );
        status = invalid_syntax_error;
    }

    if (!status && (NULL != pSuperior)) {
        struct asyntaxinfo *asi_parent;
        
        asi_parent = attr_syntax_get_by_name(pSuperior);
        /* if we find no match then server won't start or add the attribute type */
        if (asi_parent == NULL) {
            LDAPDebug (LDAP_DEBUG_PARSE,
                "Cannot find parent attribute type \"%s\"\n",pSuperior,
                NULL,NULL);
            schema_create_errormsg( errorbuf, errorbufsize,
                schema_errprefix_at, first_attr_name,
                "Missing parent attribute syntax OID");
            status = invalid_syntax_error;
        /* We only want to use the parent syntax if a SYNTAX
         * wasn't explicitly specified for this attribute. */
        } else if ((NULL == pSyntax) || (NULL == pMREquality) || (NULL == pMRSubstring) ||
                   (NULL == pMROrdering)) {
            char *pso = asi_parent->asi_plugin->plg_syntax_oid;
            
            if (pso && (NULL == pSyntax)) {
                pSyntax = slapi_ch_strdup(pso);
                LDAPDebug (LDAP_DEBUG_TRACE,
                    "Inheriting syntax %s from parent type %s\n",
                    pSyntax, pSuperior,NULL);
            } else if (NULL == pSyntax) {
                schema_create_errormsg( errorbuf, errorbufsize,
                    schema_errprefix_at, first_attr_name,
                    "Missing parent attribute syntax OID");
                status = invalid_syntax_error;
            }
            
            if (NULL == pMREquality) {
                pMREquality = slapi_ch_strdup(asi_parent->asi_mr_equality);
            }
            if (NULL == pMRSubstring) {
                pMRSubstring = slapi_ch_strdup(asi_parent->asi_mr_substring);
            }
            if (NULL == pMROrdering) {
                pMROrdering = slapi_ch_strdup(asi_parent->asi_mr_ordering);
            }
            attr_syntax_return( asi_parent );
        }
    }

    if (!status && (NULL == pSyntax)) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name, "Missing attribute syntax OID");
        status = invalid_syntax_error;

    }
    
    if (!status && (plugin_syntax_find ( pSyntax ) == NULL) ) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name, "Unknown attribute syntax OID \"%s\"",
                pSyntax );
        status = invalid_syntax_error;
    }

    if (!status) {
        struct objclass *poc;
        /* check to make sure that the OID isn't being used by an objectclass */
        if (!(schema_flags & DSE_SCHEMA_LOCKED))
            oc_lock_read();
        poc = oc_find_oid_nolock( pOid );
        if ( poc != NULL) {
            schema_create_errormsg( errorbuf, errorbufsize,
                    schema_errprefix_at, first_attr_name,
                    "The OID \"%s\" is also used by the object class \"%s\"",
                    pOid, poc->oc_name);
            status = LDAP_TYPE_OR_VALUE_EXISTS;
        }
        if (!(schema_flags & DSE_SCHEMA_LOCKED))
            oc_unlock();
    }

    if (!(schema_flags & DSE_SCHEMA_NO_CHECK) && !status) {
        int ii;
        /* check to see if the attribute name is valid */
        for (ii = 0; !status && (ii < num_names); ++ii) {
            if ( schema_check_name(attr_names[ii], PR_TRUE, errorbuf,
                    errorbufsize) == 0 ) {
                status = invalid_syntax_error;
            }
            else if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) &&
                     attr_syntax_exists(attr_names[ii])) {
                schema_create_errormsg( errorbuf, errorbufsize,
                    schema_errprefix_at, attr_names[ii],
                    "Could not be added because it already exists" );
                status = LDAP_TYPE_OR_VALUE_EXISTS;
            }
        }
    }

    if (!(schema_flags & DSE_SCHEMA_NO_CHECK) && !status) {
        if ( schema_check_oid ( first_attr_name, pOid, PR_TRUE, errorbuf,
                errorbufsize ) == 0 ) {
            status = invalid_syntax_error;
        }
    }

    if (!status) {
        struct asyntaxinfo    *tmpasi;

        if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) &&
            ( NULL != ( tmpasi = attr_syntax_get_by_oid(pOid)))) {
            schema_create_errormsg( errorbuf, errorbufsize,
                schema_errprefix_at, first_attr_name,
                "Could not be added because the OID \"%s\" is already in use",
                pOid);
            status = LDAP_TYPE_OR_VALUE_EXISTS;
            attr_syntax_return( tmpasi );
        }
    }

    
    if (!status) {
        status = attr_syntax_create( pOid, attr_names,
                *psbAttrDesc->buffer == '\0' ? NULL : psbAttrDesc->buffer,
                pSuperior, pMREquality, pMROrdering, pMRSubstring, extensions,
                pSyntax, syntaxlength, flags, &tmpasip );
    }

    if (!status) {
        if ( NULL != asipp ) {
            *asipp = tmpasip;    /* just return it */
        } else {                /* add the new attribute to the global store */
            status = attr_syntax_add( tmpasip );
            if ( LDAP_SUCCESS != status ) {
                if ( 0 != (flags & SLAPI_ATTR_FLAG_OVERRIDE) &&
                            LDAP_TYPE_OR_VALUE_EXISTS == status ) {
                    /*
                     * This can only occur if the name and OID don't match the
                     * attribute we are trying to override (all other cases of
                     * "type or value exists" were trapped above).
                     */
                    schema_create_errormsg( errorbuf, errorbufsize,
                            schema_errprefix_at, first_attr_name,
                            "Does not match the OID \"%s\". Another attribute"
                            " type is already using the name or OID.", pOid);
                } else {
                    schema_create_errormsg( errorbuf, errorbufsize,
                            schema_errprefix_at, first_attr_name,
                            "Could not be added (OID is \"%s\")", pOid );
                }
                attr_syntax_free( tmpasip );
            }
        }
    }

 done:
    /* free everything */
    free_qdlist(attr_names, num_names);
    sizedbuffer_destroy(psbAttrName);
    sizedbuffer_destroy(psbAttrDesc);
    slapi_ch_free((void **)&pOid);
    slapi_ch_free((void **)&pSuperior);
    slapi_ch_free((void **)&pMREquality);
    slapi_ch_free((void **)&pMROrdering);
    slapi_ch_free((void **)&pMRSubstring);
    slapi_ch_free((void **)&pSyntax);
    schema_free_extensions(extensions);
    return status;
}

#else /* (USE_OPENLDAP) */


/* openldap attribute parser */
/*
 * if asipp is NULL, the attribute type is added to the global set of schema.
 * if asipp is not NULL, the AT is not added but *asipp is set.  When you are
 * finished with *asipp, use attr_syntax_free() to dispose of it.
 *
 *    schema_flags: Any or none of the following bits could be set
 *        DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *        DSE_SCHEMA_NO_GLOCK -- locking of global resources is turned off;
 *                               this saves time during initialization since
 *                               the server operates in single threaded mode
 *                               at that time or in reload_schemafile_lock.
 *        DSE_SCHEMA_LOCKED   -- already locked with reload_schemafile_lock;
 *                               no further lock needed
 *
 * if is_user_defined is true, force attribute type to be user defined.
 *
 * returns an LDAP error code (LDAP_SUCCESS if all goes well)
*/
static int
parse_attr_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf,
        size_t errorbufsize, PRUint32 schema_flags, int is_user_defined,
        int schema_ds4x_compat, int is_remote)
{
    struct asyntaxinfo *tmpasip;
    struct asyntaxinfo *tmpasi;
    schemaext *extensions = NULL, *head = NULL;
    struct objclass *poc;
    LDAPAttributeType *atype = NULL;
    const char *errp;
    char *first_attr_name = NULL;
    char **attr_names = NULL;
    unsigned long flags = SLAPI_ATTR_FLAG_OVERRIDE;
    /* If we ever accept openldap schema directly, then make parser_flags configurable */
    const int parser_flags = LDAP_SCHEMA_ALLOW_NONE | LDAP_SCHEMA_ALLOW_NO_OID;
    int invalid_syntax_error;
    int syntaxlength = SLAPI_SYNTAXLENGTH_NONE;
    int num_names = 0;
    int status = 0;
    int rc = 0;
    int a, aa;

    /*
     *      OpenLDAP AttributeType struct
     *
     *          typedef struct ldap_attributetype {
     *                 char *at_oid;            OID
     *                 char **at_names;         Names
     *                 char *at_desc;           Description
     *                 int  at_obsolete;        Is obsolete?
     *                 char *at_sup_oid;        OID of superior type
     *                 char *at_equality_oid;   OID of equality matching rule
     *                 char *at_ordering_oid;   OID of ordering matching rule
     *                 char *at_substr_oid;     OID of substrings matching rule
     *                 char *at_syntax_oid;     OID of syntax of values
     *                 int  at_syntax_len;      Suggested minimum maximum length
     *                 int  at_single_value;    Is single-valued?
     *                 int  at_collective;      Is collective?
     *                 int  at_no_user_mod;     Are changes forbidden through LDAP?
     *                 int  at_usage;           Usage, see below
     *                 LDAPSchemaExtensionItem **at_extensions; Extensions
     *         } LDAPAttributeType;
     */

    /*
     *  Set the appropriate error code
     */
    if ( schema_ds4x_compat ) {
        invalid_syntax_error = LDAP_OPERATIONS_ERROR;
    } else {
        invalid_syntax_error = LDAP_INVALID_SYNTAX;
    }
    /*
     *  Verify we have input
     */
    if(input == NULL || '\0' == input[0]){
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at, NULL,
            "One or more values are required for the attributeTypes attribute" );
        LDAPDebug ( LDAP_DEBUG_ANY, "NULL args passed to parse_attr_str\n",0,0,0);
        return invalid_syntax_error;
    }
    /*
     *  Parse the line and create of attribute structure
     */
    while(isspace(*input)){
        /* trim any leading spaces */
        input++;
    }
    if((atype = ldap_str2attributetype(input, &rc, &errp, parser_flags )) == NULL){
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at, input,
                               "Failed to parse attribute, error(%d - %s) at (%s)", rc, ldap_scherr2str(rc), errp );
        return invalid_syntax_error;
    }

    if (schema_flags & DSE_SCHEMA_NO_GLOCK){
        flags |= SLAPI_ATTR_FLAG_NOLOCKING;
    }
    /*
     *  Check the NAME and update our name list
     */
    if ( NULL != atype->at_names ) {
        for(; atype->at_names[num_names]; num_names++){
            charray_add(&attr_names, slapi_ch_strdup(atype->at_names[num_names]));
        }
        first_attr_name = atype->at_names[0];
    } else { /* NAME followed by nothing violates syntax */
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at, input,
            "Missing or invalid attribute name" );
        status = invalid_syntax_error;
        goto done;
    }
    /*
     *  If the attribute type doesn't have an OID, we'll make the oid attrname-oid.
     */
    if (NULL == atype->at_oid) {
        atype->at_oid = slapi_ch_smprintf("%s-oid", first_attr_name );
    }
    /*
     *  Set the flags
     */
    if(atype->at_obsolete){
        flags |= SLAPI_ATTR_FLAG_OBSOLETE;
    }
    if(atype->at_single_value){
        flags |= SLAPI_ATTR_FLAG_SINGLE;
    }
    if(atype->at_collective){
        flags |= SLAPI_ATTR_FLAG_COLLECTIVE;
    }
    if(atype->at_no_user_mod){
        flags |= SLAPI_ATTR_FLAG_NOUSERMOD;
    }
    if(atype->at_usage == LDAP_SCHEMA_DIRECTORY_OPERATION){
        flags |= SLAPI_ATTR_FLAG_OPATTR;
    }
    /*
     * Check the superior, and use it fill in any missing oids on this attribute
     */
    if (NULL != atype->at_sup_oid) {
        struct asyntaxinfo *asi_parent;

        asi_parent = attr_syntax_get_by_name(atype->at_sup_oid);
        /* if we find no match then server won't start or add the attribute type */
        if (asi_parent == NULL) {
            LDAPDebug (LDAP_DEBUG_PARSE, "Cannot find parent attribute type \"%s\"\n",
                atype->at_sup_oid, NULL, NULL);
            schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at, first_attr_name,
                "Missing parent attribute syntax OID");
            status = invalid_syntax_error;
            goto done;

        } else if ((NULL == atype->at_syntax_oid) || (NULL == atype->at_equality_oid) ||
                   (NULL == atype->at_substr_oid) || (NULL == atype->at_ordering_oid))
        {
            /*
             * We only want to use the parent syntax if a SYNTAX
             * wasn't explicitly specified for this attribute.
             */
            char *pso = asi_parent->asi_plugin->plg_syntax_oid;

            if (pso && (NULL == atype->at_syntax_oid)) {
                atype->at_syntax_oid = slapi_ch_strdup(pso);
                LDAPDebug (LDAP_DEBUG_TRACE,
                    "Inheriting syntax %s from parent type %s\n",
                    atype->at_syntax_oid, atype->at_sup_oid,NULL);
            } else if (NULL == atype->at_syntax_oid) {
                schema_create_errormsg( errorbuf, errorbufsize,
                    schema_errprefix_at, first_attr_name,
                    "Missing parent attribute syntax OID");
                status = invalid_syntax_error;
                goto done;
            }

            if (NULL == atype->at_equality_oid) {
                atype->at_equality_oid = slapi_ch_strdup(asi_parent->asi_mr_equality);
            }
            if (NULL == atype->at_substr_oid) {
                atype->at_substr_oid = slapi_ch_strdup(asi_parent->asi_mr_substring);
            }
            if (NULL == atype->at_ordering_oid) {
                atype->at_ordering_oid = slapi_ch_strdup(asi_parent->asi_mr_ordering);
            }
            attr_syntax_return( asi_parent );
        }
    }
    /*
     *  Make sure we have a syntax oid set
     */
    if (NULL == atype->at_syntax_oid) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name, "Missing attribute syntax OID");
        status = invalid_syntax_error;
        goto done;
    }
    /*
     *  Make sure the OID is known
     */
    if (!status && (plugin_syntax_find ( atype->at_syntax_oid ) == NULL) ) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
                first_attr_name, "Unknown attribute syntax OID \"%s\"",
                atype->at_syntax_oid );
        status = invalid_syntax_error;
        goto done;
    }
    /*
     * Check to make sure that the OID isn't being used by an objectclass
     */
    if (!(schema_flags & DSE_SCHEMA_LOCKED)){
        oc_lock_read();
    }
    poc = oc_find_oid_nolock( atype->at_oid );
    if ( poc != NULL) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at, first_attr_name,
            "The OID \"%s\" is also used by the object class \"%s\"", atype->at_oid, poc->oc_name);
        status = LDAP_TYPE_OR_VALUE_EXISTS;
    }
    if (!(schema_flags & DSE_SCHEMA_LOCKED)){
        oc_unlock();
    }
    if(status){
        goto done;
    }
    /*
     *  Walk the "at_extensions" and set the schema extensions
     */
    for(a = 0; atype->at_extensions && atype->at_extensions[a]; a++){
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
        newext->term = slapi_ch_strdup(atype->at_extensions[a]->lsei_name);
        for (aa = 0; atype->at_extensions[a]->lsei_values && atype->at_extensions[a]->lsei_values[aa]; aa++){
            charray_add(&newext->values, slapi_ch_strdup(atype->at_extensions[a]->lsei_values[aa]));
            newext->value_count++;
        }
        if(extensions == NULL){
            extensions = newext;
            head = newext;
        } else {
            extensions->next = newext;
            extensions = newext;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Make sure if we are user-defined, that the attr_origins represents it
     */
    if (!extension_is_user_defined( extensions )) {
        if ( is_user_defined ) {
            int added = 0;
            /* see if we have a X-ORIGIN term already */
            while(extensions){
                if(strcmp(extensions->term, "X-ORIGIN") == 0){
                    charray_add(&extensions->values, slapi_ch_strdup(schema_user_defined_origin[0]));
                    extensions->value_count++;
                    added = 1;
                    break;
                }
                extensions = extensions->next;
            }
            if(!added){
                /* X-ORIGIN is completely missing, add it */
            	extensions = head;
				schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
				newext->term = slapi_ch_strdup("X-ORIGIN");
				charray_add(&newext->values, slapi_ch_strdup(schema_user_defined_origin[0]));
				newext->value_count++;
				while(extensions && extensions->next){
					/* move to the end of the list */
					extensions = extensions->next;
				}
				if(extensions == NULL){
					extensions = newext;
					head = extensions;
				} else {
					extensions->next = newext;
				}
            }
        } else {
            flags |= SLAPI_ATTR_FLAG_STD_ATTR;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Check to see if the attribute name is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        for (a = 0; a < num_names; ++a) {
            if ( schema_check_name(attr_names[a], PR_TRUE, errorbuf, errorbufsize) == 0 ) {
                status = invalid_syntax_error;
                goto done;
            } else if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) && attr_syntax_exists(attr_names[a])) {
                schema_create_errormsg( errorbuf, errorbufsize,
                    schema_errprefix_at, attr_names[a],
                    "Could not be added because it already exists" );
                status = LDAP_TYPE_OR_VALUE_EXISTS;
                goto done;
            }
        }
    }
    /*
     *  Check if the OID is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        if ( schema_check_oid ( first_attr_name, atype->at_oid, PR_TRUE, errorbuf,
                errorbufsize ) == 0 ) {
            status = invalid_syntax_error;
            goto done;
        }
    }
    /*
     *  Check if the OID is already being used
     */
    if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) && ( NULL != (tmpasi = attr_syntax_get_by_oid(atype->at_oid)))) {
        schema_create_errormsg( errorbuf, errorbufsize,
            schema_errprefix_at, first_attr_name,
            "Could not be added because the OID \"%s\" is already in use", atype->at_oid);
        status = LDAP_TYPE_OR_VALUE_EXISTS;
        attr_syntax_return( tmpasi );
        goto done;
    }
    /*
     *  Finally create the attribute
     */
    status = attr_syntax_create( atype->at_oid, attr_names, atype->at_desc, atype->at_sup_oid,
                                 atype->at_equality_oid, atype->at_ordering_oid, atype->at_substr_oid,
                                 extensions, atype->at_syntax_oid, syntaxlength, flags, &tmpasip );
    if (!status) {
        if ( NULL != asipp ) {
            *asipp = tmpasip; /* just return it */
        } else {
            /* add the new attribute to the global store */
            status = attr_syntax_add( tmpasip );
            if ( LDAP_SUCCESS != status ) {
                if ( 0 != (flags & SLAPI_ATTR_FLAG_OVERRIDE) &&
                            LDAP_TYPE_OR_VALUE_EXISTS == status ) {
                    /*
                     * This can only occur if the name and OID don't match the
                     * attribute we are trying to override (all other cases of
                     * "type or value exists" were trapped above).
                     */
                    schema_create_errormsg( errorbuf, errorbufsize,
                            schema_errprefix_at, first_attr_name,
                            "Does not match the OID \"%s\". Another attribute "
                            "type is already using the name or OID.", atype->at_oid);
                } else {
                    schema_create_errormsg( errorbuf, errorbufsize,
                            schema_errprefix_at, first_attr_name,
                            "Could not be added (OID is \"%s\")", atype->at_oid );
                }
                attr_syntax_free( tmpasip );
            }
        }
    }

 done:
    /* free everything */
    ldap_attributetype_free(atype);
    charray_free(attr_names);
    schema_free_extensions(extensions);

    return status;
}

/*
 * parse_objclass_str
 *
 * Read the value of the objectclasses attribute in cn=schema, convert it
 * into an objectclass struct.
 *
 * Arguments:
 *
 *     input             : value of objectclasses attribute to read
 *     oc                : pointer write the objectclass to
 *     errorbuf          : buffer to write any errors to
 *     is_user_defined   : if non-zero, force objectclass to be user defined
 *     schema_flags      : Any or none of the following bits could be set
 *                         DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *                         DSE_SCHEMA_NO_GLOCK -- don't lock global resources
 *                         DSE_SCHEMA_LOCKED   -- already locked with
 *                                                reload_schemafile_lock;
 *                                                no further lock needed
 *     schema_ds4x_compat: if non-zero, act like Netscape DS 4.x
 *
 * Returns: an LDAP error code
 *
 *       LDAP_SUCCESS if the objectclass was sucessfully read, the new
 *         objectclass will be written to oc
 *
 *       All others:  there was an error, an error message will
 *         be written to errorbuf
 */
static int
parse_objclass_str ( const char *input, struct objclass **oc, char *errorbuf,
		size_t errorbufsize, PRUint32 schema_flags, int is_user_defined,
		int schema_ds4x_compat, struct objclass *private_schema )
{
    LDAPObjectClass *objClass;
    struct objclass *pnew_oc = NULL, *psup_oc = NULL;
    schemaext *extensions = NULL, *head = NULL;
    const char *errp;
    char **OrigRequiredAttrsArray, **OrigAllowedAttrsArray;
    char *first_oc_name = NULL;
    /* If we ever accept openldap schema directly, then make parser_flags configurable */
    const int parser_flags = LDAP_SCHEMA_ALLOW_NONE | LDAP_SCHEMA_ALLOW_NO_OID;
    PRUint8 flags = 0;
    int invalid_syntax_error;
    int i, j;
    int rc = 0;

    /*
     *     openLDAP Objectclass struct
     *
     *          typedef struct ldap_objectclass {
     *                   char *oc_oid;            OID
     *                   char **oc_names;         Names
     *                   char *oc_desc;           Description
     *                   int  oc_obsolete;        Is obsolete?
     *                   char **oc_sup_oids;      OIDs of superior classes
     *                   int  oc_kind;            Kind
     *                   char **oc_at_oids_must;  OIDs of required attribute types
     *                   char **oc_at_oids_may;   OIDs of optional attribute types
     *                   LDAPSchemaExtensionItem **oc_extensions;  Extensions
     *           } LDAPObjectClass;
     */

    /*
     *  Set the appropriate error code
     */
    if ( schema_ds4x_compat ) {
        invalid_syntax_error = LDAP_OPERATIONS_ERROR;
    } else {
        invalid_syntax_error = LDAP_INVALID_SYNTAX;
    }
    /*
     *  Verify we have input
     */
    if ( NULL == input || '\0' == input[0] ) {
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc, NULL,
            "One or more values are required for the objectClasses attribute" );
        LDAPDebug ( LDAP_DEBUG_ANY, "NULL args passed to read_oc_ldif\n",0,0,0);
        return LDAP_OPERATIONS_ERROR;
    }
    /*
     *  Parse the input and create the openLdap objectclass structure
     */
    while(isspace(*input)){
    	/* trim any leading spaces */
        input++;
    }
    if((objClass = ldap_str2objectclass(input, &rc, &errp, parser_flags )) == NULL){
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc, input,
                               "Failed to parse objectclass, error(%d) at (%s)", rc, errp );
        return invalid_syntax_error;
    }
    /*
     *  Check the NAME and update our name list
     */
    if ( NULL != objClass->oc_names ) {
        first_oc_name = objClass->oc_names[0];
    } else { /* NAME followed by nothing violates syntax */
        schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc, input,
            "Missing or invalid objectclass name" );
        rc = invalid_syntax_error;
        goto done;
    }
    /*
     *  If the objectclass type doesn't have an OID, we'll make the oid objClass-oid.
     */
    if (NULL == objClass->oc_oid) {
        objClass->oc_oid = slapi_ch_smprintf("%s-oid", first_oc_name );
    }
    /*
     *  Check to see if the objectclass name is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        for (i = 0; objClass->oc_names[i]; ++i) {
            if ( schema_check_name(objClass->oc_names[i], PR_TRUE, errorbuf, errorbufsize) == 0 ) {
                rc = invalid_syntax_error;
                goto done;
            }
        }
    }
    /*
     *  Check if the OID is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        if ( schema_check_oid ( first_oc_name, objClass->oc_oid, PR_TRUE, errorbuf,
                errorbufsize ) == 0 ) {
            rc = invalid_syntax_error;
            goto done;
        }
    }
    /*
     * Look for the superior objectclass.  We first look for a parenthesized
     * list and if not found we look for a simple OID.
     *
     * XXXmcs: Since we do not yet support multiple superior objectclasses, we
     * just grab the first OID in a parenthesized list.
     */
    if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
        /* needed because we access the superior oc */
        oc_lock_read();
    }
    if(objClass->oc_sup_oids && objClass->oc_sup_oids[0]) {
                if (schema_flags & DSE_SCHEMA_USE_PRIV_SCHEMA) {
                        /* We have built an objectclass list on a private variable
                         * This is used to check the schema of a remote consumer
                         */
                        psup_oc = oc_find_nolock(objClass->oc_sup_oids[0], private_schema, PR_TRUE);
                } else {
                        psup_oc = oc_find_nolock(objClass->oc_sup_oids[0], NULL, PR_FALSE);
                }   
    }
    /*
     *  Walk the "oc_extensions" and set the schema extensions
     */
    for(i = 0; objClass->oc_extensions && objClass->oc_extensions[i]; i++){
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
        newext->term = slapi_ch_strdup(objClass->oc_extensions[i]->lsei_name);
        for (j = 0; objClass->oc_extensions[i]->lsei_values && objClass->oc_extensions[i]->lsei_values[j]; j++){
             charray_add(&newext->values, slapi_ch_strdup(objClass->oc_extensions[i]->lsei_values[j]));
             newext->value_count++;
        }
        if(extensions == NULL){
            extensions = newext;
            head = extensions;
        } else {
            extensions->next = newext;
            extensions = newext;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Set the remaining flags
     */
    if(objClass->oc_obsolete){
        flags |= OC_FLAG_OBSOLETE;
    }
    if ( extension_is_user_defined( extensions )) {
        flags |= OC_FLAG_USER_OC;
    } else if ( is_user_defined ) {
        int added = 0;
        /* see if we have a X-ORIGIN term already */
        while(extensions){
            if(strcmp(extensions->term, "X-ORIGIN") == 0){
                charray_add(&extensions->values, slapi_ch_strdup(schema_user_defined_origin[0]));
                extensions->value_count++;
                added = 1;
                break;
            }
            extensions = extensions->next;
        }
        if(!added){
            /* X-ORIGIN is completely missing, add it */
            extensions = head;
            schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
            newext->term = slapi_ch_strdup("X-ORIGIN");
            charray_add( &newext->values, slapi_ch_strdup( schema_user_defined_origin[0] ));
            newext->value_count++;
            while(extensions && extensions->next){
                extensions = extensions->next;
            }
            if(extensions == NULL){
                extensions = newext;
                head = extensions;
            } else {
                extensions->next = newext;
            }
        }
        flags |= OC_FLAG_USER_OC;
    } else {
        flags |= OC_FLAG_STANDARD_OC;
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Generate OrigRequiredAttrsArray and OrigAllowedAttrsArray from the superior oc
     */
    if (psup_oc) {
        int found_it;

        OrigRequiredAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
        OrigRequiredAttrsArray[0] = NULL;
        OrigAllowedAttrsArray = (char **) slapi_ch_malloc (1 * sizeof(char *)) ;
        OrigAllowedAttrsArray[0] = NULL;
        if (psup_oc->oc_required && objClass->oc_at_oids_must) {
            for (i = 0; objClass->oc_at_oids_must[i]; i++) {
                found_it = 0;
                for (j = 0; psup_oc->oc_required[j]; j++) {
                    if (strcasecmp (psup_oc->oc_required[j], objClass->oc_at_oids_must[i]) == 0) {
                        found_it = 1;
                        break;
                    }
                }
                if (!found_it) {
                    charray_add (&OrigRequiredAttrsArray, slapi_ch_strdup ( objClass->oc_at_oids_must[i] ) );
                }
            }
        } else {
            /* we still need to set the originals */
            charray_free(OrigRequiredAttrsArray);
            OrigRequiredAttrsArray = charray_dup(objClass->oc_at_oids_must);
        }
        if (psup_oc->oc_allowed && objClass->oc_at_oids_may) {
            for (i = 0; objClass->oc_at_oids_may[i]; i++) {
                found_it = 0;
                for (j = 0; psup_oc->oc_allowed[j]; j++) {
                    if (strcasecmp (psup_oc->oc_allowed[j], objClass->oc_at_oids_may[i]) == 0) {
                        found_it = 1;
                        break;
                    }
                }
                if (!found_it) {
                    charray_add (&OrigAllowedAttrsArray, slapi_ch_strdup (objClass->oc_at_oids_may[i]) );
                }
            }
        } else {
            /* we still need to set the originals */
            charray_free(OrigAllowedAttrsArray);
            OrigAllowedAttrsArray = charray_dup(objClass->oc_at_oids_may);
        }
    } else {
        /* if no parent oc */
        OrigRequiredAttrsArray = charray_dup ( objClass->oc_at_oids_must );
        OrigAllowedAttrsArray = charray_dup ( objClass->oc_at_oids_may );
    }
    if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
        oc_unlock(); /* we are done accessing superior oc (psup_oc) */
    }
    /*
     *  Finally - create new the objectclass
     */
    pnew_oc = (struct objclass *) slapi_ch_calloc (1, sizeof (struct objclass));
    pnew_oc->oc_name = slapi_ch_strdup ( objClass->oc_names[0] );
    if(objClass->oc_sup_oids){
        pnew_oc->oc_superior = slapi_ch_strdup( objClass->oc_sup_oids[0] );
    }
    pnew_oc->oc_oid = slapi_ch_strdup( objClass->oc_oid );
    pnew_oc->oc_desc = slapi_ch_strdup( objClass->oc_desc );
    pnew_oc->oc_required = charray_dup( objClass->oc_at_oids_must );
    pnew_oc->oc_allowed = charray_dup( objClass->oc_at_oids_may );
    pnew_oc->oc_orig_required = OrigRequiredAttrsArray;
    pnew_oc->oc_orig_allowed = OrigAllowedAttrsArray;
    pnew_oc->oc_kind = objClass->oc_kind;
    pnew_oc->oc_extensions = extensions;
    pnew_oc->oc_next = NULL;
    pnew_oc->oc_flags = flags;

    *oc = pnew_oc;

done:
    ldap_objectclass_free(objClass);

    return rc;
}
#endif

/* 
 * schema_check_oc_attrs: 
 * Check to see if the required and allowed attributes are valid attributes
 *
 * arguments: poc         : pointer to the objectclass to check
 *            errorbuf    : buffer to write any error messages to
 *            stripOptions: 1 if you want to silently strip any options
 *                          0 if options should cause an error
 *
 * Returns:
 *
 * 0 if there's a unknown attribute, and errorbuf will contain an
 * error message.
 *
 * 1 if everything is ok
 *
 * Note: no locking of poc is needed because poc is always a newly allocated
 * objclass struct (this function is only called by add_oc_internal).
 */
static int 
schema_check_oc_attrs ( struct objclass *poc, 
						char *errorbuf, size_t errorbufsize,
						int stripOptions )
{
	int i;

	if ( errorbuf == NULL || poc == NULL || poc->oc_name == NULL) {
		/* error */
		LDAPDebug (LDAP_DEBUG_PARSE,  
				   "Null args passed to schema_check_oc_attrs\n",
				   NULL, NULL, NULL);
		return -1;
	}

	/* remove any options like ;binary from the oc's attributes */
	if ( strip_oc_options( poc ) && !stripOptions) {
		/* there were options present, this oc should be rejected */
		schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
				poc->oc_name, "Contains attribute options. "
				 "Attribute options, such as \";binary\" are not allowed in "
				 "object class definitions." );
		return 0;
	}
	  
	for ( i = 0; poc->oc_allowed && poc->oc_allowed[i]; i++ ) {
		if ( attr_syntax_exists ( poc->oc_allowed[i] ) == 0 ) {
			schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
					poc->oc_name, "Unknown allowed attribute type \"%s\"",
					poc->oc_allowed[i]);
			return 0;
		}
	}
  
  
	for ( i = 0; poc->oc_required && poc->oc_required[i]; i++ ) {
		if ( attr_syntax_exists ( poc->oc_required[i] ) == 0 ) {
			schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_oc,
					poc->oc_name, "Unknown required attribute type \"%s\"",
					poc->oc_required[i]);
			return 0;
		}
	}

	return 1;
}

/*
 * schema_check_name:
 * Check if the attribute or objectclass name is valid.  Names can only contain
 * characters, digits, and hyphens. In addition, names must begin with
 * a character. If the nsslapd-attribute-name-exceptions attribute in cn=config
 * is true, then we also allow underscores.
 *
 * XXX We're also supposed to allow semicolons, but we already use them to deal
 *     with attribute options XXX
 * 
 * returns 1 if the attribute has a legal name
 *         0 if not
 *
 * If the attribute name is invalid, an error message will be written to msg
 */

static int 
schema_check_name(char *name, PRBool isAttribute, char *errorbuf,
		size_t errorbufsize )
{
  int i;

  /* allowed characters */
  static char allowed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-";

  /* additional characters to allow if allow_exceptions is true */
  static char allowedExceptions[] = "_"; 
  int allow_exceptions = config_get_attrname_exceptions();
  
  if ( name == NULL || errorbuf == NULL) {
	/* this is bad */
	return 0;
  }

  if (!strcasecmp(name, PSEUDO_ATTR_UNHASHEDUSERPASSWORD)) {
      /* explicitly allow this badly named attribute */
      return 1;
  }

  /* attribute names must begin with a letter */
  if ( (isascii (name[0]) == 0) || (isalpha (name[0]) == 0)) {
	if ( (strlen(name) + 80) < BUFSIZ ) {
	  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
			name, "The name is invalid. Names must begin with a letter" );
	}
	else {
	  schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
			name, "The name is invalid, and probably too long. "
			"Names must begin with a letter" );
	}
	return 0;
  }

  for (i = 1; name[i]; i++ ) {
	if ( (NULL == strchr( allowed, name[i] )) &&
		 (!allow_exceptions ||
		  (NULL == strchr(allowedExceptions, name[i])) ) ) {
	  if ( (strlen(name) + 80) < BUFSIZ ) {
	    schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
			name, "The name contains the invalid character \"%c\"", name[i] );
	  }
	  else {
	    schema_create_errormsg( errorbuf, errorbufsize, schema_errprefix_at,
			name, "The name contains the invalid character \"%c\".  The name"
			" is also probably too long.", name[i] );
	  }
	  return 0;
	}
  }
  return 1;
}



/*
 * schema_check_oid:
 * Check if the oid is valid. 
 *
 * returns 1 if the attribute has a legal oid
 *         0 if not
 *
 * If the oid is invalid, an error message will be written to errorbuf
 *
 * Oids can either have the form <attr/oc name>-oid or 
 * start and end with a digit, and contain only digits and periods
 */

static int 
schema_check_oid( const char *name, const char *oid, PRBool isAttribute,
		char *errorbuf, size_t errorbufsize ) {

  int i = 0, length_oid = 0, rc = 0;
  char *namePlusOid = NULL;

  if ( name == NULL || oid == NULL) {
	/* this is bad */
	LDAPDebug (LDAP_DEBUG_ANY, "NULL passed to schema_check_oid\n",0,0,0);
	return 0;
  }
  
  /* check to see if the OID is <name>-oid */
  namePlusOid = slapi_ch_smprintf("%s-oid", name );
  rc = strcasecmp( oid, namePlusOid );
  slapi_ch_free( (void **) &namePlusOid );

  if ( 0 == rc ) {
	return 1;
  }

  /* If not, the OID must begin and end with a digit, and contain only 
	 digits and dots */
  
  /* check to see that it begins and ends with a digit */
  length_oid = strlen(oid);
  if ( !isdigit(oid[0]) || 
	   !isdigit(oid[length_oid-1]) ) {
	schema_create_errormsg( errorbuf, errorbufsize,
			isAttribute ? schema_errprefix_at : schema_errprefix_oc,
			name,
			"The OID \"%s\" must begin and end with a digit, or be \"%s-oid\"",
			oid, name );
	return 0;
  }

  /* check to see that it contains only digits and dots */
  for ( i = 0; i < length_oid; i++ ) {
	if ( !isdigit(oid[i]) && oid[i] != '.'  ){
		schema_create_errormsg( errorbuf, errorbufsize,
				isAttribute ? schema_errprefix_at : schema_errprefix_oc,
				name,
				 "The OID \"%s\" contains an invalid character: \"%c\"; the"
				" OID must contain only digits and periods, or be \"%s-oid\"",
				 oid, oid[i], name );
	  return 0;
	}
  }

  /* The oid is OK if we're here */
  return 1;
  

}


/*
 * Some utility functions for dealing with a dynamically
 * allocated buffer.
 */

static struct sizedbuffer *sizedbuffer_construct(size_t size)
{
    struct sizedbuffer *p= (struct sizedbuffer *)slapi_ch_malloc(sizeof(struct sizedbuffer));
	p->size= size;
	if(size>0)
	{
	    p->buffer= (char*)slapi_ch_malloc(size);
		p->buffer[0]= '\0';
	}
	else
	{
       	p->buffer= NULL;
	}
	return p;
}

static void sizedbuffer_destroy(struct sizedbuffer *p)
{
    if(p!=NULL)
	{
        slapi_ch_free((void**)&p->buffer);
	}
	slapi_ch_free((void**)&p);
}

static void sizedbuffer_allocate(struct sizedbuffer *p, size_t sizeneeded)
{
    if(p!=NULL)
	{
        if(sizeneeded>p->size)
    	{
    	    if(p->buffer!=NULL)
    		{
    		    slapi_ch_free((void**)&p->buffer);
    		}
    		p->buffer= (char*)slapi_ch_malloc(sizeneeded);
    		p->buffer[0]= '\0';
			p->size= sizeneeded;
    	}
	}
}

/*
 * Check if the object class is extensible
 */
static int isExtensibleObjectclass(const char *objectclass)
{
	if ( strcasecmp( objectclass, "extensibleobject" ) == 0 ) {
		return( 1 );
	}
	/* The Easter Egg is based on a special object class */
	if ( strcasecmp( objectclass, EGG_OBJECT_CLASS ) == 0 ) {
		return( 1 );
	}
	return 0;
}



/* 
 * strip_oc_options: strip any attribute options from the objectclass'
 *					 attributes (remove things like ;binary from the attrs)
 *
 * argument: pointer to an objectclass, attributes will have their 
 *           options removed in place
 *
 * returns:  number of options removed
 *
 * Note: no locking of poc is needed because poc is always a newly allocated
 * objclass struct (this function is only called by schema_check_oc_attrs,
 * which is only called by add_oc_internal).
 */

static int
strip_oc_options( struct objclass *poc ) {
  int i, numRemoved = 0;
  char *mod = NULL;

  for ( i = 0; poc->oc_allowed && poc->oc_allowed[i]; i++ ) {
	if ( (mod = stripOption( poc->oc_allowed[i] )) != NULL ){
	  LDAPDebug (LDAP_DEBUG_ANY, 
				 "Removed option \"%s\" from allowed attribute type "
				 "\"%s\" in object class \"%s\".\n",
				 mod, poc->oc_allowed[i], poc->oc_name );
	  numRemoved++;
	}
  }
    
  for ( i = 0; poc->oc_required && poc->oc_required[i]; i++ ) {
	if ( (mod = stripOption( poc->oc_required[i] )) != NULL ){
	  LDAPDebug (LDAP_DEBUG_ANY, 
				 "Removed option \"%s\" from required attribute type "
				 "\"%s\" in object class \"%s\".\n",
				 mod, poc->oc_required[i], poc->oc_name );
	  numRemoved++;
	}
  }
  return numRemoved;
}


/* 
 * stripOption:
 * removes options such as ";binary" from attribute names
 *   
 * argument: pointer to an attribute name, such as "userCertificate;binary"
 *
 * returns: pointer to the option, such as "binary"
 *          NULL if there's no option
 *
 */ 

static char *
stripOption(char *attr) {
  char *pSemiColon = strchr( attr, ';' );

  if (pSemiColon) {
	*pSemiColon = '\0';
  }

  return pSemiColon ? pSemiColon + 1 : NULL;
}


/*
 * load_schema_dse: called by dse_read_file() when target is cn=schema
 * 
 * Initialize attributes and objectclasses from the schema 
 *
 * Note that this function removes all values for `attributetypes'
 *        and `objectclasses' attributes from the entry `e'.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int
load_schema_dse(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *ignored,
                int *returncode, char *returntext, void *arg)
{
    Slapi_Attr *attr = 0;
    int primary_file = 0;    /* this is the primary (writeable) schema file */
    int schema_ds4x_compat = config_get_ds4_compatible_schema();
    PRUint32 flags = *(PRUint32 *)arg;

    *returncode = 0;

    /*
     * Note: there is no need to call schema_lock_write() here because this
     * function is only called during server startup.
     */

    slapi_pblock_get( pb, SLAPI_DSE_IS_PRIMARY_FILE, &primary_file );

    if (!slapi_entry_attr_find(e, "attributetypes", &attr) && attr)
    {
        /* enumerate the values in attr */
        Slapi_Value *v = 0;
        int index = 0;
        for (index = slapi_attr_first_value(attr, &v);
             v && (index != -1);
             index = slapi_attr_next_value(attr, index, &v))
        {
            const char *s = slapi_value_get_string(v);
            if (!s)
                continue;
            if (flags & DSE_SCHEMA_NO_LOAD)
            {
                struct asyntaxinfo *tmpasip = NULL;
                if ((*returncode = parse_at_str(s, &tmpasip, returntext,
                        SLAPI_DSE_RETURNTEXT_SIZE, flags,
                        primary_file /* force user defined? */,
                        schema_ds4x_compat, 0)) != 0)
                    break;
                attr_syntax_free( tmpasip );    /* trash it */
            }
            else
            {
                if ((*returncode = parse_at_str(s, NULL, returntext,
                        SLAPI_DSE_RETURNTEXT_SIZE, flags,
                        primary_file /* force user defined? */,
                        schema_ds4x_compat, 0)) != 0)
                    break;
            }
        }
        slapi_entry_attr_delete(e, "attributetypes");
    }

    if (*returncode)
        return SLAPI_DSE_CALLBACK_ERROR;

    flags |= DSE_SCHEMA_NO_GLOCK; /* don't lock global resources
                                     during initialization */
    if (!slapi_entry_attr_find(e, "objectclasses", &attr) && attr)
    {
        /* enumerate the values in attr */
        Slapi_Value *v = 0;
        int index = 0;
        for (index = slapi_attr_first_value(attr, &v);
             v && (index != -1);
             index = slapi_attr_next_value(attr, index, &v))
        {
            struct objclass *oc = 0;
            const char *s = slapi_value_get_string(v);
            if (!s)
                continue;
            if ( LDAP_SUCCESS != (*returncode = parse_oc_str(s, &oc, returntext,
                        SLAPI_DSE_RETURNTEXT_SIZE, flags,
                        primary_file /* force user defined? */,
                        schema_ds4x_compat, NULL)))
            {
            	oc_free( &oc );
                break;
            }
            if (flags & DSE_SCHEMA_NO_LOAD)
            {
                /* we don't load the objectclase; free it */
                oc_free( &oc );
            }
            else
            {
                if ( LDAP_SUCCESS !=
                        (*returncode = add_oc_internal(oc, returntext,
                        SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat,
                        flags))) {
                    oc_free( &oc );
                    break;
                }
            }
        }
        slapi_entry_attr_delete(e, "objectclasses");
    }

    /* Set the schema CSN */
    if (!(flags & DSE_SCHEMA_NO_LOAD) &&
        !slapi_entry_attr_find(e, "nsschemacsn", &attr) && attr)
    {
        Slapi_Value *v = NULL;
        slapi_attr_first_value(attr, &v);
        if (NULL != v) {
            const char *s = slapi_value_get_string(v);
            if (NULL != s) {
                CSN *csn = csn_new_by_string(s);
                g_set_global_schema_csn(csn);
            }
        }
    }

    return (*returncode == LDAP_SUCCESS) ? SLAPI_DSE_CALLBACK_OK
            : SLAPI_DSE_CALLBACK_ERROR;
}

/*
 * Try to initialize the schema from the LDIF file.  Read
 * the file and convert it to the avl tree of DSEs.  If the
 * file doesn't exist, we try to create it and put a minimal
 * schema entry into it.
 * 
 * Returns 1 for OK, 0 for Fail.
 *
 * schema_flags:
 * DSE_SCHEMA_NO_LOAD      -- schema won't get loaded
 * DSE_SCHEMA_NO_CHECK     -- schema won't be checked
 * DSE_SCHEMA_NO_BACKEND   -- don't add as backend
 * DSE_SCHEMA_LOCKED       -- already locked; no further lock needed
 */
static int
init_schema_dse_ext(char *schemadir, Slapi_Backend *be,
				struct dse **local_pschemadse, PRUint32 schema_flags)
{
	int rc= 1; /* OK */
	char *userschemafile = 0;
	char *userschematmpfile = 0;
	char **filelist = 0;
	char *myschemadir = NULL;
	Slapi_DN schema;

	if (NULL == local_pschemadse)
	{
		return 0;	/* cannot proceed; return failure */
	}

	*local_pschemadse = NULL;
	slapi_sdn_init_ndn_byref(&schema,"cn=schema");

	/* get schemadir if not given */
	if (NULL == schemadir)
	{
		myschemadir = config_get_schemadir();
		if (NULL == myschemadir)
		{
			return 0;	/* cannot proceed; return failure */
		}
	}
	else
	{
		myschemadir = schemadir;
	}

	filelist = get_priority_filelist(myschemadir, ".*ldif$");
	if (!filelist || !*filelist)
	{
		slapi_log_error(SLAPI_LOG_FATAL, "schema",
			"No schema files were found in the directory %s\n", myschemadir);
		free_filelist(filelist);
		rc = 0;
	}
	else
	{
		/* figure out the last file in the list; it is the user schema */
		int ii = 0;
		while (filelist[ii]) ++ii;
		userschemafile = filelist[ii-1];
		userschematmpfile = slapi_ch_smprintf("%s.tmp", userschemafile);
	}

	if(rc)
	{
		*local_pschemadse = dse_new_with_filelist(userschemafile,
						userschematmpfile, NULL, NULL, myschemadir, filelist);
	}
	PR_ASSERT(*local_pschemadse);
	if ((rc = (*local_pschemadse != NULL)) != 0)
	{
		/* pass schema_flags as arguments */
		dse_register_callback(*local_pschemadse,
								  DSE_OPERATION_READ, DSE_FLAG_PREOP, &schema,
								  LDAP_SCOPE_BASE, NULL,
								  load_schema_dse, (void *)&schema_flags);
	}
	slapi_ch_free_string(&userschematmpfile);
	if (NULL == schemadir)
		slapi_ch_free_string(&myschemadir); /* allocated in this function */

	if(rc)
	{
		char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
		char *attr_str;
		int dont_write = 1;
		int merge = 1;
		int dont_dup_check = 1;
		Slapi_PBlock pb;
		memset(&pb, 0, sizeof(pb));
		/* don't write out the file when reading */
		slapi_pblock_set(&pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, (void*)&dont_write);
		/* duplicate entries are allowed */
		slapi_pblock_set(&pb, SLAPI_DSE_MERGE_WHEN_ADDING, (void*)&merge);
		/* use the non duplicate checking str2entry */
		slapi_pblock_set(&pb, SLAPI_DSE_DONT_CHECK_DUPS, (void*)&dont_dup_check);
		/* borrow the task flag space */
		slapi_pblock_set(&pb, SLAPI_SCHEMA_FLAGS, (void*)&schema_flags);

		/* add the objectclass attribute so we can do some basic schema
		   checking during initialization; this will be overridden when
		   its "real" definition is read from the schema conf files */

#ifdef USE_OPENLDAP
		attr_str = "( 2.5.4.0 NAME 'objectClass' "
				   "DESC 'Standard schema for LDAP' SYNTAX "
				   "1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'RFC 2252' )";
#else
		attr_str = "attributeTypes: ( 2.5.4.0 NAME 'objectClass' "
				  "DESC 'Standard schema for LDAP' SYNTAX "
				  "1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'RFC 2252' )";
#endif
		if (schema_flags & DSE_SCHEMA_NO_LOAD)
		{
			struct asyntaxinfo *tmpasip = NULL;
			rc = parse_at_str(attr_str, &tmpasip, errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
						  DSE_SCHEMA_NO_GLOCK|schema_flags, 0, 0, 0);
			attr_syntax_free( tmpasip );	/* trash it */
		}
		else
		{
			rc = parse_at_str(attr_str, NULL, errorbuf, SLAPI_DSE_RETURNTEXT_SIZE,
						  schema_flags, 0, 0, 0);
		}
		if (rc)
		{
			slapi_log_error(SLAPI_LOG_FATAL, "schema", "Could not add"
				" attribute type \"objectClass\" to the schema: %s\n",
				errorbuf);
		}

		rc = dse_read_file(*local_pschemadse, &pb);
	}

	if (rc && !(schema_flags & DSE_SCHEMA_NO_BACKEND))
	{
		/* make sure the schema is normalized */
		if (schema_flags & DSE_SCHEMA_LOCKED)
			normalize_oc_nolock();
		else
			normalize_oc();

		/* register callbacks */
		dse_register_callback(*local_pschemadse, SLAPI_OPERATION_SEARCH,
							  DSE_FLAG_PREOP,&schema, LDAP_SCOPE_BASE,
							  NULL, read_schema_dse, NULL);
		dse_register_callback(*local_pschemadse, SLAPI_OPERATION_MODIFY,
							  DSE_FLAG_PREOP,&schema, LDAP_SCOPE_BASE,
							  NULL, modify_schema_dse, NULL);
		dse_register_callback(*local_pschemadse, SLAPI_OPERATION_DELETE,
							  DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
							  NULL,dont_allow_that,NULL);
		dse_register_callback(*local_pschemadse, DSE_OPERATION_WRITE,
							  DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
							  NULL, refresh_user_defined_schema, NULL);

		if (rc) {
			if (NULL == be) {	/* be is not given. select it */
				be = slapi_be_select_by_instance_name( DSE_SCHEMA );
			}
			if (NULL == be) {	/* first time */
				/* add as a backend */
				be = be_new_internal(*local_pschemadse, "DSE", DSE_SCHEMA);
				be_addsuffix(be, &schema);
			} else {					/* schema file reload */
				struct slapdplugin *backend_plugin = NULL;
				be_replace_dse_internal(be, *local_pschemadse);

				/* ldbm has some internal attributes to be added */
				backend_plugin = plugin_get_by_name("ldbm database");
				if (backend_plugin) {
					if (backend_plugin->plg_add_schema) {
						(backend_plugin->plg_add_schema)( NULL );
					} else {
						slapi_log_error( SLAPI_LOG_FATAL, "init_schema_dse",
							"backend has not set internal schema\n" );
					}
				}
			}
		}
	}

	slapi_sdn_done(&schema);
	return rc;
}

int
init_schema_dse(const char *configdir)
{
	char *schemadir = config_get_schemadir();
	int rc = 0;
	if (NULL == schemadir)
	{
		schemadir = slapi_ch_smprintf("%s/%s", configdir, SCHEMA_SUBDIR_NAME);
	}
	rc = init_schema_dse_ext(schemadir, NULL, &pschemadse, DSE_SCHEMA_NO_GLOCK);
	slapi_ch_free_string(&schemadir);
	return rc;
}

#if !defined (USE_OPENLDAP)

static char **
parse_xstring_list( const char *schema_value, const char *name, size_t namelen, int *num_xvalsp,
        char **default_list )
{
    char    *start, *end, *xval_tmp;
    char    **xvals = NULL;

    if (( start = PL_strstr ( schema_value, name )) != NULL) {
        start += namelen+1; /* +1 for space */
        xval_tmp = slapi_ch_strdup( start );
        if ( *start == '(' ) {
            end = strchr( xval_tmp, ')' );
        } else {
            end = strchr( xval_tmp + 1, '\'' );
        }
        if (end) {
            *(end+1) = 0;
        }
        xvals = parse_qdstrings( xval_tmp, num_xvalsp );
        slapi_ch_free( (void **)&xval_tmp );
    } else {
        xvals = NULL;
        *num_xvalsp = 0;
    }

    if ( NULL == xvals && NULL != default_list ) {
        int        i;

        for ( i = 0; default_list[i] != NULL; ++i ) {
            ;
        }
        *num_xvalsp = i;
        xvals = (char **)slapi_ch_malloc( (i+1) * sizeof(char *));
        for ( i = 0; default_list[i] != NULL; ++i ) {
            xvals[i] = slapi_ch_strdup( default_list[i] );
        }
        xvals[i] = NULL;
    }

/* for debugging
if ( xvals == NULL || xvals[0] == NULL ) {
    LDAPDebug( LDAP_DEBUG_ANY, "no xstring values for xstring (%s) in (%s)\n", name, schema_value, 0 );
}
*/

    return xvals;
}

/* used by mozldap read_at_ldif() & read_oc_ldif() */
static schemaext*
parse_extensions( const char *schema_value, char **default_list )
{
    schemaext *extensions = NULL, *head = NULL;
    char *input, *token, *iter = NULL;
    int i;

    /*
     *  First make a copy of the input, then grab all the "X-" words,
     *  and extract the values.
     */
    input = slapi_ch_strdup(schema_value);
    token = ldap_utf8strtok_r(input, " ", &iter);
    while(token){
        if(strncmp(token,"X-", 2) == 0){
            /* we have a new extension */
            schemaext *newext;
            newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
            newext->term = slapi_ch_strdup(token);
            newext->values = parse_xstring_list(schema_value, token, strlen(token), &newext->value_count, default_list);
	        if(extensions == NULL){
                extensions = newext;
                head = newext;
            } else {
                extensions->next = newext;
                extensions = extensions->next;
            }
        }
        token = ldap_utf8strtok_r(iter, " ", &iter);
    }
    extensions = head;
    /*
     *  Ok, if X-ORIGIN was not specified, then add it with the default values
     */
    if ( !extension_is_user_defined(extensions) && default_list ) {
        int added = 0;
        while(extensions){
            if(strcmp(extensions->term, "X-ORIGIN") == 0){
                charray_add(&extensions->values, slapi_ch_strdup(default_list[0]) );
                extensions->value_count++;
                added = 1;
                extensions = head;
                break;
            }
            extensions = extensions->next;
        }
        if(!added){
            schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof (schemaext));
            newext->term = slapi_ch_strdup("X-ORIGIN");
            for ( i = 0; default_list[i]; ++i ){
                newext->value_count++;
                charray_add(&newext->values, slapi_ch_strdup(default_list[i]) );
            }
            extensions = head;
            while(extensions && extensions->next){
                /* move to the end of the list */
                extensions = extensions->next;
            }
            if(extensions == NULL){
                extensions = newext;
            } else {
                extensions->next = newext;
            }
        }
    }
    slapi_ch_free_string(&input);

    return extensions;
}

/*
 * Look for `keyword' within `*inputp' and return the flag_value if found
 * (zero if returned if not found).
 *
 * If the keyword is found, `*inputp' is set to point just beyond the end of
 * the keyword.  If the keyword is not found, `*inputp' is not changed.
 *
 * The `strstr_fn' function pointer is used to search for `keyword', e.g., it
 * could be PL_strcasestr().
 *
 * The string passed in `keyword' MUST include a trailing space, e.g.,
 *
 *       flag |= get_flag_keyword( " COLLECTIVE ", SLAPI_ATTR_FLAG_COLLECTIVE,
 *				&input, PL_strcasestr );
 *
 */
static int
get_flag_keyword( const char *keyword, int flag_value, const char **inputp,
		schema_strstr_fn_t strstr_fn )
{
	const char	*kw;

	PR_ASSERT( NULL != inputp );
	PR_ASSERT( NULL != *inputp );
	PR_ASSERT( ' ' == keyword[ strlen( keyword ) - 1 ] );

	if ( NULL == strstr_fn ) {
		strstr_fn = PL_strcasestr;
	}

	kw = (*strstr_fn)( *inputp, keyword );
	if ( NULL == kw ) { 
		flag_value = 0;	/* not found -- return no value */
	} else {
		*inputp = kw + strlen( keyword ) - 1;	/* advance input */
	}

	return flag_value;
}

/*
 * Look for `tag' within `*inputp' and return the OID string following `tag'. 
 * If the OID has single quotes around it they are removed (they are allowed
 * for compatibility with DS 3.x and 4.x).
 *
 * If the tag is found, `*inputp' is set to point just beyond the end of
 * the OID that was extracted and returned.  If the tag is not found,
 * `*inputp' is not changed.
 *
 * The `strstr_fn' function pointer is used to search for `tag', e.g., it
 * could be PL_strcasestr().
 *
 * The string passed in `tag' SHOULD generally include a trailing space, e.g.,
 *
 *       pSuperior = get_tagged_oid( "SUP ", &input, PL_strcasestr );
 *
 * The exception to this is when the tag contains '(' as a trailing character.
 * This is used to process lists of oids, such as the following:
 *
 *       SUP (inetOrgPerson $ testUser)
 *
 * A malloc'd string is returned if `tag; is found and NULL if not.
 */
static char *
get_tagged_oid( const char *tag, const char **inputp,
		schema_strstr_fn_t strstr_fn )
{
	const char		*startp, *endp;
	char			*oid;

	PR_ASSERT( NULL != inputp );
	PR_ASSERT( NULL != *inputp );
	PR_ASSERT( NULL != tag );
	PR_ASSERT( '\0' != tag[ 0 ] );
       	if('(' !=tag[0]) 
	  PR_ASSERT((' ' == tag[ strlen( tag ) - 1 ]) || ('(' == tag[ strlen( tag ) - 1 ]));

	if ( NULL == strstr_fn ) {
		strstr_fn = PL_strcasestr;
	}

	oid = NULL;
	if ( NULL != ( startp = (*strstr_fn)( *inputp, tag ))) {
		startp += strlen( tag );

		/* skip past any extra white space */
		if ( NULL == ( startp = skipWS( startp ))) {
			return( NULL );
		}

		/* skip past the leading single quote, if present */
		if ( *startp == '\'' ) {
			++startp;
                        /* skip past any extra white space */
                        startp = skipWS( startp );
		}

		/* locate the end of the OID */
		if ((NULL != ( endp = strchr( startp, ' '))) ||
		    (NULL != (endp = strchr( startp, ')'))) ) {
			if ( '\'' == *(endp-1) && endp > startp ) {
				--endp;		/* ignore trailing quote */
			}
		} else {
			endp = startp + strlen( startp );	/* remainder of input */
		}

		oid = slapi_ch_malloc( endp - startp + 1 );
		memcpy( oid, startp, endp - startp );
		oid[ endp - startp ] = '\0';
		*inputp = endp;
	}

	return( oid );
}

#endif

/*
 * sprintf to `outp' the contents of `tag' followed by `oid' followed by a
 * trailing space.  If enquote is non-zero, single quotes are included
 * around the `oid' string.  If `suffix' is not NULL, it is output directly
 * after the `oid' (before the trailing space).
 * Note that `tag' should typically include a trailing space, e.g.,
 *
 *		outp += put_tagged_oid( outp, "SUP ", "1.2.3.4", NULL, enquote_oids );
 *
 * Returns the number of bytes copied to `outp' or 0 if `oid' is NULL.
 */
static int
put_tagged_oid( char *outp, const char *tag, const char *oid,
		const char *suffix, int enquote )
{
	int			count = 0;

	if ( NULL == suffix ) {
		suffix = "";
	}

	if ( NULL != oid ) {
		if ( enquote ) {
			count = sprintf( outp, "%s'%s%s' ", tag, oid, suffix );
		} else {
			count = sprintf( outp, "%s%s%s ", tag, oid, suffix );
		}
	}

	return( count );
}


/*
 * Add to `buf' a string of the form:
 *
 *    prefix SPACE ( oid1 $ oid2 ... ) SPACE
 * OR
 *    prefix SPACE oid SPACE
 *
 * The part after <prefix> matches the `oids' definition
 *		from RFC 2252 section 4.1.
 *
 * If oids is NULL or an empty array, `buf' is not touched.
 */
static void
strcat_oids( char *buf, char *prefix, char **oids, int schema_ds4x_compat )
{
	char	*p;
	int		i;
	
	if ( NULL != oids && NULL != oids[0] ) {
		p = buf + strlen(buf);		/* skip past existing content */
		if ( NULL == oids[1] && !schema_ds4x_compat ) {
			sprintf( p, "%s %s ", prefix, oids[0] );	/* just one oid */
		} else {
			sprintf( p, "%s ( ", prefix );			/* oidlist */
			for ( i = 0; oids[i] != NULL; ++i ) {
				if ( i > 0 ) {
					strcat( p, " $ " );
				}
				strcat( p, oids[i] );
			}
			strcat( p, " ) " );
		}
	}
}

/*
 * Add to `buf' a string of the form:
 *
 *    prefix SPACE ( 's1' 's2' ... ) SPACE
 * OR
 *    prefix SPACE 's1' SPACE
 *
 * The part after <prefix> matches the qdescs definition
 *		from RFC 2252 section 4.1.
 *
 * A count of the number of bytes added to buf or needed is returned.
 *
 * If buf is NULL, no copying is done but the number of bytes needed
 * is calculated and returned.  This is useful if you need to allocate
 * space before calling this function will a buffer.
 *
 */
static size_t
strcat_qdlist( char *buf, char *prefix, char **qdlist )
{
        int                     i;
        char            *start, *p;
        size_t          len = 0;

        if ( NULL != qdlist && NULL != qdlist[0] ) {
                if ( NULL == buf ) {    /* calculate length only */
                        len += strlen( prefix );
                        if ( NULL != qdlist[1] ) {
                                len += 4;       /* surrounding spaces and '(' and ')' */
                        }
                        for ( i = 0; NULL != qdlist[i]; ++i ) {
                                len += 3;       /* leading space and quote marks */
                                len += strlen(qdlist[i]);
                        }
                        ++len;                  /* trailing space */

                } else {
                        p = start = buf + strlen(buf);  /* skip past existing content */
                        if ( NULL == qdlist[1] ) {              /* just one string */
                                p += sprintf( p, "%s '%s' ", prefix, qdlist[0] );
                        } else {                                                /* a list of strings */
                                p += sprintf( p, "%s (", prefix );
                                for ( i = 0; qdlist[i] != NULL; ++i ) {
                                        p += sprintf( p, " '%s'", qdlist[i] );
                                }
                                *p++ = ' ';
                                *p++ = ')';
                                *p++ = ' ';
                                *p = '\0';
                        }
                        len = p - start;
                }
        }

        return( len );
}

/*
 *  Loop over the extensions calling strcat_qdlist for each one.
 */
static size_t
strcat_extensions( char *buf, schemaext *extension )
{
    size_t len = 0;

    while(extension){
        len += strcat_qdlist(buf, extension->term, extension->values);
        extension = extension->next;
    }

    return( len );
}

/*
 * Just like strlen() except that 0 is returned if `s' is NULL.
 */
static size_t
strlen_null_ok(const char *s)
{
	if ( NULL == s ) {
		return( 0 );
	}

	return( strlen( s ));
}



/*
 * Like strcpy() except a count of the number of bytes copied is returned.
 */
static int
strcpy_count( char *dst, const char *src )
{
	char *p;

	p = dst;
	while ( *src != '\0' ) {
		*p++ = *src++;
	}

	*p = '\0';
	return( p - dst );
}

static int
extension_is_user_defined( schemaext *extensions )
{
    while(extensions){
        if(strcasecmp(extensions->term, "X-ORIGIN") == 0 ){
            int i = 0;
            while(extensions->values && extensions->values[i]){
                if(strcasecmp(schema_user_defined_origin[0], extensions->values[i]) == 0) {
                    return 1;
                }
                i++;
            }
        }
        extensions = extensions->next;
    }

    return 0;
}


/*
 * Return PR_TRUE if the attribute type named 'type' is one of those that
 * we handle directly in this file (in the scheme DSE callbacks).
 * Other types are handled by the generic DSE code in dse.c.
 */
/* subschema DSE attribute types we handle within the DSE callback */
static char *schema_interesting_attr_types[] = {
	"dITStructureRules",
	"nameForms",
	"dITContentRules",
	"objectClasses",
	"attributeTypes",
	"matchingRules",
	"matchingRuleUse",
	"ldapSyntaxes",
	"nsschemacsn",
	NULL
};


static PRBool
schema_type_is_interesting( const char *type )
{
	int	i;

	for ( i = 0; schema_interesting_attr_types[i] != NULL; ++i ) {
		if ( 0 == strcasecmp( type, schema_interesting_attr_types[i] )) {
			return PR_TRUE;
		}
	}

	return PR_FALSE;
}


static void
schema_create_errormsg(
	char 		*errorbuf,
	size_t		errorbufsize,
	const char	*prefix,
	const char	*name,
	const char 	*fmt,
	...
)
{
	if ( NULL != errorbuf ) {
		va_list		ap;
		int			rc = 0;

		va_start( ap, fmt );

		if ( NULL != name ) {
			rc = PR_snprintf( errorbuf, errorbufsize, prefix, name );
		}
		/* ok to cast here because rc is positive */
		if ( (rc >= 0) && ((size_t)rc < errorbufsize) ) {
			(void)PR_vsnprintf( errorbuf + rc, errorbufsize - rc, fmt, ap );
		}
		va_end( ap );
	}
}


/*
 * va_locate_oc_val finds an objectclass within the array of values in va.
 * First oc_name is used, falling back to oc_oid.  oc_oid can be NULL.
 * oc_name and oc_oid should be official names (no trailing spaces). But
 * trailing spaces within the va are ignored if appropriate.
 *
 * Returns >=0 if found (index into va) and -1 if not found.
 */
static int
va_locate_oc_val( Slapi_Value **va, const char *oc_name, const char *oc_oid )
{
    int			i;
    const char	*strval;

    if ( NULL == va || oc_name == NULL ) {	/* nothing to look for */
        return -1;
    }

	if ( !schema_ignore_trailing_spaces ) {
		for ( i = 0; va[i] != NULL; i++ ) {
			strval = slapi_value_get_string(va[i]);
			if ( NULL != strval ) {
				if ( 0 == strcasecmp(strval, oc_name)) {
					return i;
				}
					
				if ( NULL != oc_oid
						&& 0 == strcasecmp( strval, oc_oid )) {
					return i;
				}
			}
		}
	} else {
		/* 
		 * Ignore trailing spaces when comparing object class names.
		 */
		size_t		len;
		const char	*p;

		for ( i = 0; va[i] != NULL; i++ ) {
			strval = slapi_value_get_string(va[i]);
			if ( NULL != strval ) {
				for ( p = strval, len = 0;  (*p != '\0') && (*p != ' '); 
						p++, len++ ) {
					;       /* NULL */
				}

				if ( 0 == strncasecmp(oc_name, strval, len )
						&& ( len == strlen(oc_name))) {
					return i;
				}
					
				if ( NULL != oc_oid
						&& ( 0 == strncasecmp( oc_oid, strval, len ))
						&& ( len == strlen(oc_oid))) {
					return i;
				}
			}
		}
	}

    return -1;					/* not found */
}


/*
 * va_expand_one_oc is used to add missing superclass values to the 
 * objectclass attribute when an entry is added or modified.
 *
 * missing values are always added to the end of the 'vap' array.
 *
 * Note: calls to this function MUST be bracketed by lock()/unlock(), i.e.,
 *
 *		oc_lock_read();
 *		va_expand_one_oc( b, o );
 *		oc_unlock();
 */
static void
va_expand_one_oc( const char *dn, const Slapi_Attr *a, Slapi_ValueSet *vs, const char *ocs )
{
	struct objclass	*this_oc, *sup_oc;
	int p;
	Slapi_Value **va = vs->va;


	this_oc = oc_find_nolock( ocs, NULL, PR_FALSE );
  
	if ( this_oc == NULL ) {
		return;			/* skip unknown object classes */
	}
  
	if ( this_oc->oc_superior == NULL ) {
		return;			/* no superior */
	}

	sup_oc = oc_find_nolock( this_oc->oc_superior, NULL, PR_FALSE );
	if ( sup_oc == NULL ) {
		return;			/* superior is unknown -- ignore */
	}

	p = va_locate_oc_val( va, sup_oc->oc_name, sup_oc->oc_oid );

	if ( p != -1 ) {
		return;			/* value already present -- done! */
	}
  
	if ( slapi_valueset_count(vs) > 1000 ) {
		return;
	}
  
  	slapi_valueset_add_attr_value_ext(a, vs, slapi_value_new_string(sup_oc->oc_name), SLAPI_VALUE_FLAG_PASSIN);

	LDAPDebug( LDAP_DEBUG_TRACE,
			"Entry \"%s\": added missing objectClass value %s\n",
			dn, sup_oc->oc_name, 0 );
}


/*
 * Expand the objectClass values in 'e' to take superior classes into account.
 * All missing superior classes are added to the objectClass attribute, as
 * is 'top' if it is missing.
 */
static void
schema_expand_objectclasses_ext( Slapi_Entry *e, int lock)
{
	Slapi_Attr		*sa;
	Slapi_Value		*v;
	Slapi_ValueSet		*vs;
	const char		*dn = slapi_entry_get_dn_const( e );
	int				i;

	if ( 0 != slapi_entry_attr_find( e, SLAPI_ATTR_OBJECTCLASS, &sa )) {
		return;		/* no OC values -- nothing to do */
	}

	vs = &sa->a_present_values;
	if ( slapi_valueset_isempty(vs) ) {
		return;		/* no OC values -- nothing to do */
	}

	if (lock)
		oc_lock_read();

	/*
	 * This loop relies on the fact that bv_expand_one_oc()
	 * always adds to the end
	 */
	i = slapi_valueset_first_value(vs,&v);
	while ( v != NULL) {
		if ( NULL != slapi_value_get_string(v) ) {
			va_expand_one_oc( dn, sa, &sa->a_present_values, slapi_value_get_string(v) );
 		}
		i = slapi_valueset_next_value(vs, i, &v);
	}
  
	/* top must always be present */
	va_expand_one_oc( dn, sa, &sa->a_present_values, "top" );
	if (lock)
		oc_unlock();
}
void
slapi_schema_expand_objectclasses( Slapi_Entry *e )
{
	schema_expand_objectclasses_ext( e, 1);
}

void
schema_expand_objectclasses_nolock( Slapi_Entry *e )
{
	schema_expand_objectclasses_ext( e, 0);
}

/* lock to protect both objectclass and schema_dse */
static void
reload_schemafile_lock()
{
	oc_lock_write();
	schema_dse_lock_write();
}

static void
reload_schemafile_unlock()
{
	schema_dse_unlock();
	oc_unlock();
}

/* API to validate the schema files */
int
slapi_validate_schema_files(char *schemadir)
{
	struct dse *my_pschemadse = NULL;
	int rc = init_schema_dse_ext(schemadir, NULL, &my_pschemadse,
	                             DSE_SCHEMA_NO_LOAD | DSE_SCHEMA_NO_BACKEND);
	dse_destroy(my_pschemadse); /* my_pschemadse was created just to 
	                               validate the schema */
	if (rc) {
		return LDAP_SUCCESS;
	} else {
		slapi_log_error( SLAPI_LOG_FATAL, "schema_reload",
		                 "schema file validation failed\n" );
		return LDAP_OBJECT_CLASS_VIOLATION;
	}
}

/* 
 * API to reload the schema files.
 * Rule: this function is called when slapi_validate_schema_files is passed.
 *       Schema checking is skipped in this function.
 */
int
slapi_reload_schema_files(char *schemadir)
{
	int rc = LDAP_SUCCESS;
	struct dse *my_pschemadse = NULL;
	/* get be to lock */
	Slapi_Backend *be = slapi_be_select_by_instance_name( DSE_SCHEMA );

	if (NULL == be)
	{
		slapi_log_error( SLAPI_LOG_FATAL, "schema_reload",
				"schema file reload failed\n" );
		return LDAP_LOCAL_ERROR;
	}
	slapi_be_Wlock(be);	/* be lock must be outer of schemafile lock */
	reload_schemafile_lock();
	/* Exclude attr_syntax not to grab from the hash table while cleaning up  */
	attr_syntax_write_lock();
	attr_syntax_delete_all_for_schemareload(SLAPI_ATTR_FLAG_KEEP);
	oc_delete_all_nolock();
	attr_syntax_unlock_write();
	rc = init_schema_dse_ext(schemadir, be, &my_pschemadse,
	                         DSE_SCHEMA_NO_CHECK | DSE_SCHEMA_LOCKED);
	if (rc) {
		dse_destroy(pschemadse);
		pschemadse = my_pschemadse;
		reload_schemafile_unlock();
		slapi_be_Unlock(be);
		return LDAP_SUCCESS;
	} else {
		reload_schemafile_unlock();
		slapi_be_Unlock(be);
		slapi_log_error( SLAPI_LOG_FATAL, "schema_reload",
				"schema file reload failed\n" );
		return LDAP_LOCAL_ERROR;
	}
}

/* 
 * slapi_schema_list_objectclass_attributes:
 *         Return the list of attributes belonging to the objectclass
 *
 * The caller is responsible to free the returned list with charray_free.
 * flags: one of them or both:
 *         SLAPI_OC_FLAG_REQUIRED
 *         SLAPI_OC_FLAG_ALLOWED
 */
char **
slapi_schema_list_objectclass_attributes(const char *ocname_or_oid,
                                         PRUint32 flags)
{
	struct objclass *oc = NULL;
	char **attrs = NULL;
	PRUint32 mask = SLAPI_OC_FLAG_REQUIRED | SLAPI_OC_FLAG_ALLOWED;

	if (!flags) {
		return attrs;
	}
		
	oc_lock_read();
	oc = oc_find_nolock(ocname_or_oid, NULL, PR_FALSE);
	if (oc) {
		switch (flags & mask) {
		case SLAPI_OC_FLAG_REQUIRED:
			attrs = charray_dup(oc->oc_required);
			break;
		case SLAPI_OC_FLAG_ALLOWED:
			attrs = charray_dup(oc->oc_allowed);
			break;
		case SLAPI_OC_FLAG_REQUIRED|SLAPI_OC_FLAG_ALLOWED:
			attrs = charray_dup(oc->oc_required);
			charray_merge(&attrs, oc->oc_allowed, 1/*copy_strs*/);
			break;
		default:
			slapi_log_error( SLAPI_LOG_FATAL, "list objectclass attributes",
				"flag 0x%x not supported\n", flags );
			break;
		}
	}
	oc_unlock();
	return attrs;
}

/* 
 * slapi_schema_get_superior_name:
 *         Return the name of the superior objectclass
 *
 * The caller is responsible to free the returned name
 */
char *
slapi_schema_get_superior_name(const char *ocname_or_oid)
{
	struct objclass *oc = NULL;
	char *superior = NULL;

	oc_lock_read();
	oc = oc_find_nolock(ocname_or_oid, NULL, PR_FALSE);
	if (oc) {
		superior = slapi_ch_strdup(oc->oc_superior);
	}
	oc_unlock();
	return superior;
}



/* Check if the oc_list1 is a superset of oc_list2.
 * oc_list1 is a superset if it exists objectclass in oc_list1 that
 * do not exist in oc_list2. Or if a OC in oc_list1 required more attributes
 * that the OC in oc_list2. Or if a OC in oc_list1 allowed more attributes
 * that the OC in oc_list2.
 * 
 * It returns 1 if oc_list1 is a superset of oc_list2, else it returns 0
 * 
 * If oc_list1 or oc_list2 is global_oc, the caller must hold the oc_lock 
 */
static int
schema_oc_superset_check(struct objclass *oc_list1, struct objclass *oc_list2, char *message) {
        struct objclass *oc_1, *oc_2;
        char *description;
        int rc, i, j;
        int found;

        if (message == NULL) {
                description = "";
        } else {
                description = message;
        }
        
        /* by default assum oc_list1 == oc_list2 */
        rc = 0;

        /* Check if all objectclass in oc_list1
         *   - exists in oc_list2
         *   - required attributes are also required in oc_2
         *   - allowed attributes are also allowed in oc_2
         */
        for (oc_1 = oc_list1; oc_1 != NULL; oc_1 = oc_1->oc_next) {

                /* Retrieve the remote objectclass in our local schema */
                oc_2 = oc_find_nolock(oc_1->oc_oid, oc_list2, PR_TRUE);
                if (oc_2 == NULL) {
                        /* try to retrieve it with the name*/
                        oc_2 = oc_find_nolock(oc_1->oc_name, oc_list2, PR_TRUE);
                }
                if (oc_2 == NULL) {
                        slapi_log_error(SLAPI_LOG_REPL, "schema", "Fail to retrieve in the %s schema [%s or %s]\n", 
                                description,
                                oc_1->oc_name, 
                                oc_1->oc_oid);

                        /* The oc_1 objectclasses is supperset */
                        rc = 1;

                        continue; /* we continue to check all the objectclass */
                }

                /* First check the MUST */
                if (oc_1->oc_orig_required) {
                        for (i = 0; oc_1->oc_orig_required[i] != NULL; i++) {
                                /* For each required attribute from the remote schema check that 
                                 * it is also required in the local schema
                                 */
                                found = 0;
                                if (oc_2->oc_orig_required) {
                                        for (j = 0; oc_2->oc_orig_required[j] != NULL; j++) {
                                                if (strcasecmp(oc_2->oc_orig_required[j], oc_1->oc_orig_required[i]) == 0) {
                                                        found = 1;
                                                        break;
                                                }
                                        }
                                }
                                if (!found) {
                                        /* The required attribute in the remote protocol (remote_oc->oc_orig_required[i])
                                         * is not required in the local protocol
                                         */
                                        slapi_log_error(SLAPI_LOG_REPL, "schema", "Attribute %s is not required in '%s' of the %s schema\n",
                                                oc_1->oc_orig_required[i],
                                                oc_1->oc_name,
                                                description);

                                        /* The oc_1 objectclasses is supperset */
                                        rc = 1;
                                                
                                        continue; /* we continue to check all attributes */
                                }
                        }
                }

                /* Second check the MAY */
                if (oc_1->oc_orig_allowed) {
                        for (i = 0; oc_1->oc_orig_allowed[i] != NULL; i++) {
                                /* For each required attribute from the remote schema check that 
                                 * it is also required in the local schema
                                 */
                                found = 0;
                                if (oc_2->oc_orig_allowed) {
                                        for (j = 0; oc_2->oc_orig_allowed[j] != NULL; j++) {
                                                if (strcasecmp(oc_2->oc_orig_allowed[j], oc_1->oc_orig_allowed[i]) == 0) {
                                                        found = 1;
                                                        break;
                                                }
                                        }
                                }
                                if (!found) {
                                        /* The required attribute in the remote protocol (remote_oc->oc_orig_allowed[i])
                                         * is not required in the local protocol
                                         */
                                        slapi_log_error(SLAPI_LOG_REPL, "schema", "Attribute %s is not allowed in '%s' of the %s schema\n",
                                                oc_1->oc_orig_allowed[i],
                                                oc_1->oc_name,
                                                description);

                                        /* The oc_1 objectclasses is supperset */
                                        rc = 1;
                                        
                                        continue; /* we continue to check all attributes */
                                }
                        }
                }
        }
        
        return rc;
}

static void
schema_oclist_free(struct objclass *oc_list)
{
        struct objclass *oc, *oc_next;
        
        for (oc = oc_list; oc != NULL; oc = oc_next) {
                oc_next = oc->oc_next;
                oc_free(&oc);
        }
}

static
struct objclass *schema_berval_to_oclist(struct berval **oc_berval) {
        struct objclass *oc, *oc_list, *oc_tail;
        char errorbuf[BUFSIZ];
        int schema_ds4x_compat, rc;
        int i;
        
        schema_ds4x_compat = config_get_ds4_compatible_schema();
        rc = 0;
        
        oc_list = NULL;
        oc_tail = NULL;
        if (oc_berval != NULL) {
                for (i = 0; oc_berval[i] != NULL; i++) {
                        /* parse the objectclass value */
                        if (LDAP_SUCCESS != (rc = parse_oc_str(oc_berval[i]->bv_val, &oc,
                                errorbuf, sizeof (errorbuf), DSE_SCHEMA_NO_CHECK | DSE_SCHEMA_USE_PRIV_SCHEMA, 0,
                                schema_ds4x_compat, oc_list))) {
                                oc_free(&oc);
                                rc = 1;
                                break;
                        }
                        
                        /* Add oc at the end of the oc_list */
                        oc->oc_next = NULL;
                        if (oc_list == NULL) {
                                oc_list = oc;
                                oc_tail = oc;
                        } else {
                                oc_tail->oc_next = oc;
                                oc_tail = oc;
                        }
                }
        }
        if (rc) {
                schema_oclist_free(oc_list);
                oc_list = NULL;
        }
        return oc_list;
}

int
schema_objectclasses_superset_check(struct berval **remote_schema, char *type) {
        int  rc;
        struct objclass *remote_oc_list;

        rc = 0;
        
        /* head is the future list of the objectclass of the remote schema */
        remote_oc_list = NULL;
        
        if (remote_schema != NULL) {
                /* First build an objectclass list from the remote schema */
                if ((remote_oc_list = schema_berval_to_oclist(remote_schema)) == NULL) {
                        rc = 1;
                        return rc;
                }
                
                
                /* Check that for each object from the remote schema
                 *         - MUST attributes are also MUST in local schema
                 *         - ALLOWED attributes are also ALLOWED in local schema
                 */

                if (remote_oc_list) {
                        oc_lock_read();
                        if (strcmp(type, OC_SUPPLIER) == 0) {
                                /* Check if the remote_oc_list from a consumer are or not 
                                 * a superset of the objectclasses of the local supplier schema
                                 */
                                rc = schema_oc_superset_check(remote_oc_list, g_get_global_oc_nolock(), "local supplier" );
                        } else {
                                /* Check if the objectclasses of the local consumer schema are or not
                                 * a superset of the remote_oc_list from a supplier
                                 */
                                rc = schema_oc_superset_check(g_get_global_oc_nolock(), remote_oc_list, "remote supplier");
                        }
                        
                        oc_unlock();
                }
                
                /* Free the remote schema list*/
                schema_oclist_free(remote_oc_list);
        }
        return rc;
}
