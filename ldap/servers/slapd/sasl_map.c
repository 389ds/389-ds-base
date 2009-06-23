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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include "slap.h"
#include "slapi-plugin.h"
#include "fe.h"

/*
 * Map SASL identities to LDAP searches
 */

/* 
 * We maintain a list of mappings to consult
 */

typedef struct sasl_map_data_ sasl_map_data;
struct sasl_map_data_ {
	char *name;
	char *regular_expression;
	char *template_base_dn;
	char *template_search_filter;
	sasl_map_data *next; /* For linked list */
}; 

typedef struct _sasl_map_private {
	PRLock *lock;
	sasl_map_data *map_data_list;
} sasl_map_private;

static char * configDN = "cn=mapping,cn=sasl,cn=config";

/*
 * DBDB: this is ugly, but right now there is _no_ server-wide
 * dynamic structure (like a Slapi_Server * type thing). All the code
 * that needs such a thing instead maintains a static or global variable.
 * Until we implement the 'right thing', we'll just follow suit here :(
 */

static sasl_map_private *sasl_map_static_priv = NULL;

static 
sasl_map_private *sasl_map_get_global_priv()
{
	/* ASSERT(sasl_map_static_priv) */
	return sasl_map_static_priv;
}

static 
sasl_map_private *sasl_map_new_private()
{
	PRLock *new_lock = PR_NewLock();
	sasl_map_private *new_priv = NULL;
	if (NULL == new_lock) {
		return NULL;
	}
	new_priv = (sasl_map_private *)slapi_ch_calloc(1,sizeof(sasl_map_private));
	new_priv->lock = new_lock;
	if (NULL == new_lock) {
		slapi_ch_free((void**)new_priv);
		return NULL;
	}
	return new_priv;
}

static void 
sasl_map_free_private(sasl_map_private **priv)
{
	PR_DestroyLock((*priv)->lock);
	slapi_ch_free((void**)priv);
	*priv = NULL;
}

/* This function does a shallow copy on the payload data supplied, so the caller should not free it, and it needs to be allocated using slapi_ch_malloc() */
static 
sasl_map_data *sasl_map_new_data(char *name, char *regex, char *dntemplate, char *filtertemplate)
{
	sasl_map_data *new_dp = (sasl_map_data *) slapi_ch_calloc(1,sizeof(sasl_map_data));
	new_dp->name = name;
	new_dp->regular_expression = regex;
	new_dp->template_base_dn = dntemplate;
	new_dp->template_search_filter = filtertemplate;
	return new_dp;
}

static 
sasl_map_data *sasl_map_next(sasl_map_data *dp)
{
	return dp->next;
}

static void 
sasl_map_free_data(sasl_map_data **dp)
{
	slapi_ch_free_string(&(*dp)->name);
	slapi_ch_free_string(&(*dp)->regular_expression);
	slapi_ch_free_string(&(*dp)->template_base_dn);
	slapi_ch_free_string(&(*dp)->template_search_filter);
	slapi_ch_free((void**)dp);
}

static int 
sasl_map_remove_list_entry(sasl_map_private *priv, char *removeme)
{
	int ret = 0;
	int foundit = 0;
	sasl_map_data *current = NULL;
	sasl_map_data *previous = NULL;
	PR_Lock(priv->lock);
	current = priv->map_data_list;
	while (current) {
		if (0 == strcmp(current->name,removeme)) {
			foundit = 1;
			if (previous) {
				/* Unlink it */
				previous->next = current->next;
			} else {
				/* That was the first list entry */
				priv->map_data_list = current->next;
			}
			/* Payload free */
			sasl_map_free_data(&current);
			/* And no need to look further */
			break;
		}
		previous = current;
		current = current->next;
	}
	if (!foundit) {
		ret = -1;
	}
	PR_Unlock(priv->lock);
	return ret;
}

static int 
sasl_map_insert_list_entry(sasl_map_private *priv, sasl_map_data *dp)
{
	int ret = 0;
	int ishere = 0;
	sasl_map_data *current = NULL;
	PR_Lock(priv->lock);
	/* Check to see if it's here already */
	current = priv->map_data_list;
	while (current && current->next) {
		current = current->next;
	}
	if (ishere) {
		return -1;
	}
	/* current now points to the end of the list or NULL */
	if (NULL == priv->map_data_list) {
		priv->map_data_list = dp;
	} else {
		current->next = dp;
	}
	PR_Unlock(priv->lock);
	return ret;
}

/*
 * Functions to handle config operations 
 */

/**
 * Get a list of child DNs
 * DBDB these functions should be folded into libslapd because it's a copy of a function in ssl.c
 */
static char **
getChildren( char *dn ) {
	Slapi_PBlock    *new_pb = NULL;
	Slapi_Entry     **e;
	int             search_result = 1;
	int             nEntries = 0;
	char            **list = NULL;

	new_pb = slapi_search_internal ( dn, LDAP_SCOPE_ONELEVEL,
									 "(objectclass=nsSaslMapping)",
									 NULL, NULL, 0);

	slapi_pblock_get( new_pb, SLAPI_NENTRIES, &nEntries);
	if ( nEntries > 0 ) {
		slapi_pblock_get( new_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
		slapi_pblock_get( new_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e);
		if ( e != NULL ) {
			int i;
			list = (char **)slapi_ch_malloc( sizeof(*list) * (nEntries + 1));
			for ( i = 0; e[i] != NULL; i++ ) {
				list[i] = slapi_ch_strdup(slapi_entry_get_dn(e[i]));
			}
			list[nEntries] = NULL;
		}
	}
	slapi_free_search_results_internal(new_pb);
	slapi_pblock_destroy(new_pb );
	return list;
}

/**
 * Free a list of child DNs
 */
static void
freeChildren( char **list ) {
	if ( list != NULL ) {
		int i;
		for ( i = 0; list[i] != NULL; i++ ) {
			slapi_ch_free( (void **)(&list[i]) );
		}
		slapi_ch_free( (void **)(&list) );
	}
}


/**
 * Get a particular entry
 */
static Slapi_Entry *
getConfigEntry( const char *dn, Slapi_Entry **e2 ) {
	Slapi_DN	sdn;

	slapi_sdn_init_dn_byref( &sdn, dn );
	slapi_search_internal_get_entry( &sdn, NULL, e2,
			plugin_get_default_component_id());
	slapi_sdn_done( &sdn );
	return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry( Slapi_Entry ** e ) {
	if ( (e != NULL) && (*e != NULL) ) {
		slapi_entry_free( *e );
		*e = NULL;
	}
}

/*
 * unescape parenthesis in the regular expression.
 * E.g., ^u:\(.*\) ==> ^u:(.*)
 * This unescape is necessary for the new regex code using PCRE 
 * to keep the backward compatibility.
 */
char *
_sasl_unescape_parenthesis(char *input)
{
	char *s = NULL;
	char *d = NULL;

	for (s = input, d = input; s && *s; s++) {
		if (*s == '\\' && *(s+1) && (*(s+1) == '(' || *(s+1) == ')')) {
			*d++ = *(++s);
		} else {
			*d++ = *s;
		}
	}
	*d = '\0';
	return input;
}

static int
sasl_map_config_parse_entry(Slapi_Entry *entry, sasl_map_data **new_dp)
{
	int ret = 0;
	char *regex = NULL;
	char *basedntemplate = NULL;
	char *filtertemplate = NULL;
	char *map_name = NULL;

	*new_dp = NULL;
	regex = _sasl_unescape_parenthesis(slapi_entry_attr_get_charptr( entry, "nsSaslMapRegexString" ));
	basedntemplate = slapi_entry_attr_get_charptr( entry, "nsSaslMapBaseDNTemplate" );
	filtertemplate = slapi_entry_attr_get_charptr( entry, "nsSaslMapFilterTemplate" );
	map_name = slapi_entry_attr_get_charptr( entry, "cn" );

	if ( (NULL == map_name) || (NULL == regex) ||
		 (NULL == basedntemplate) || (NULL == filtertemplate) ) {
		/* Invalid entry */
		ret = -1;
	} else {
		/* Make the new dp */
		*new_dp = sasl_map_new_data(map_name, regex, basedntemplate, filtertemplate);
	}

	if (ret) {
		slapi_ch_free((void **) &map_name);
		slapi_ch_free((void **) &regex);
		slapi_ch_free((void **) &basedntemplate);
		slapi_ch_free((void **) &filtertemplate);
	}
	return ret;
}

static int
sasl_map_read_config_startup(sasl_map_private *priv)
{
	char **map_entry_list = NULL;
	int ret = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "-> sasl_map_read_config_startup\n", 0, 0, 0 );
    if((map_entry_list = getChildren(configDN))) {
		char **map_entry = NULL;
		Slapi_Entry *entry = NULL;
		sasl_map_data *dp = NULL;

		for (map_entry = map_entry_list; *map_entry && !ret; map_entry++) {
			LDAPDebug( LDAP_DEBUG_CONFIG, "sasl_map_read_config_startup - proceesing [%s]\n", *map_entry, 0, 0 );
 			getConfigEntry( *map_entry, &entry );
			if ( entry == NULL ) {
				continue;
			}
			ret = sasl_map_config_parse_entry(entry,&dp);
			if (ret) {
				    LDAPDebug( LDAP_DEBUG_ANY, "sasl_map_read_config_startup failed to parse entry\n", 0, 0, 0 );
			} else {
				ret = sasl_map_insert_list_entry(priv,dp);
				if (ret) {
					LDAPDebug( LDAP_DEBUG_ANY, "sasl_map_read_config_startup failed to insert entry\n", 0, 0, 0 );
				} else {
					LDAPDebug( LDAP_DEBUG_CONFIG, "sasl_map_read_config_startup - processed [%s]\n", *map_entry, 0, 0 );
				}
			}
			freeConfigEntry( &entry );
		}
		freeChildren( map_entry_list );
    }
	LDAPDebug( LDAP_DEBUG_TRACE, "<- sasl_map_read_config_startup\n", 0, 0, 0 );
	return ret;
}

int 
sasl_map_config_add(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
	int ret = 0;
	sasl_map_data *dp = NULL;
	sasl_map_private *priv = sasl_map_get_global_priv();
	LDAPDebug( LDAP_DEBUG_TRACE, "-> sasl_map_config_add\n", 0, 0, 0 );
	ret = sasl_map_config_parse_entry(entryBefore,&dp);
	if (!ret && dp) {
		ret = sasl_map_insert_list_entry(priv,dp);
	}
	if (0 == ret) {
		ret = SLAPI_DSE_CALLBACK_OK;
	} else {
		returntext = "sasl map entry rejected";
		*returncode = LDAP_UNWILLING_TO_PERFORM;
		ret = SLAPI_DSE_CALLBACK_ERROR;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<- sasl_map_config_add\n", 0, 0, 0 );
	return ret;
}

int
sasl_map_config_delete(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg)
{
	int ret = 0;
	sasl_map_private *priv = sasl_map_get_global_priv();
	char *entry_name = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "-> sasl_map_config_delete\n", 0, 0, 0 );
	entry_name = slapi_entry_attr_get_charptr( entryBefore, "cn" );
	if (entry_name) {
		/* remove this entry from the list */
		ret = sasl_map_remove_list_entry(priv,entry_name);
		slapi_ch_free((void **) &entry_name);
	}
	if (ret) {
		ret = SLAPI_DSE_CALLBACK_ERROR;
		returntext = "can't delete sasl map entry";
		*returncode = LDAP_OPERATIONS_ERROR;
	} else {
		ret = SLAPI_DSE_CALLBACK_OK;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<- sasl_map_config_delete\n", 0, 0, 0 );
	return ret;
}

/* Start and stop the sasl mapping code */
int sasl_map_init()
{
	int ret = 0;
	sasl_map_private *priv = NULL;
	/* Make the private structure */
	priv = sasl_map_new_private();
	if (priv) {
		/* Store in the static var */
		sasl_map_static_priv = priv;
		/* Read the config on startup */
		ret = sasl_map_read_config_startup(priv);
	} else {
		ret = -1;
	}
	return ret;
}

int sasl_map_done()
{
	int ret = 0;
	sasl_map_private *priv = sasl_map_get_global_priv();
	sasl_map_data *dp = NULL;

	/* Free the map list */
	PR_Lock(priv->lock);
	dp = priv->map_data_list;
	while (dp) {
		sasl_map_data *dp_next = dp->next;
		sasl_map_free_data(&dp);
		dp = dp_next;
	}
	PR_Unlock(priv->lock);

	/* Free the private structure */
	sasl_map_free_private(&priv);
	return ret;
}

static sasl_map_data*
sasl_map_first(sasl_map_private *priv)
{
	sasl_map_data *result = NULL;
	PR_Lock(priv->lock);
	result = priv->map_data_list;
	PR_Unlock(priv->lock);
	return result;
}

static int
sasl_map_check(sasl_map_data *dp, char *sasl_user_and_realm, char **ldap_search_base, char **ldap_search_filter)
{
	Slapi_Regex *re = NULL;
	int ret = 0;
	int matched = 0;
	char *recomp_result = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "-> sasl_map_check\n", 0, 0, 0 );
	/* Compiles the regex */
	re = slapi_re_comp(dp->regular_expression, &recomp_result);
	if (NULL == re) {
		LDAPDebug( LDAP_DEBUG_ANY,
			"sasl_map_check: slapi_re_comp failed for expression (%s): %s\n",
			dp->regular_expression, recomp_result?recomp_result:"unknown", 0 );
	} else {
		/* Matches the compiled regex against sasl_user_and_realm */
		matched = slapi_re_exec(re, sasl_user_and_realm, -1 /* no timelimit */);
		LDAPDebug( LDAP_DEBUG_TRACE, "regex: %s, id: %s, %s\n",
			dp->regular_expression, sasl_user_and_realm,
			matched ? "matched" : "didn't match" );
	}
	if (matched) {
		if (matched == 1) {
			char escape_base[BUFSIZ];
			char escape_filt[BUFSIZ];
			int ldap_search_base_len, ldap_search_filter_len;
			int rc = 0;

			/* Allocate buffers for the returned strings */
			/* We already computed this, so we could pass it in to speed up
			 * a little */
			size_t userrealmlen = strlen(sasl_user_and_realm); 
			/* These lengths could be precomputed and stored in the dp */
			ldap_search_base_len =
					userrealmlen + strlen(dp->template_base_dn) + 1;
			ldap_search_filter_len =
					userrealmlen + strlen(dp->template_search_filter) + 1;
			*ldap_search_base = (char *)slapi_ch_malloc(ldap_search_base_len);
			*ldap_search_filter =
					(char *)slapi_ch_malloc(ldap_search_filter_len);
			/* Substitutes '&' and/or "\#" in template_base_dn */
			rc = slapi_re_subs(re, sasl_user_and_realm, dp->template_base_dn,
					ldap_search_base, ldap_search_base_len);
			if (0 != rc) {
				LDAPDebug( LDAP_DEBUG_ANY,
					"sasl_map_check: slapi_re_subs failed: "
					"subject: %s, subst str: %s (%d)\n",
					sasl_user_and_realm, dp->template_base_dn, rc);
			} else {
				/* Substitutes '&' and/or "\#" in template_search_filter */
				rc = slapi_re_subs(re, sasl_user_and_realm,
					dp->template_search_filter, ldap_search_filter,
					ldap_search_filter_len);
				if (0 != rc) {
					LDAPDebug( LDAP_DEBUG_ANY,
						"sasl_map_check: slapi_re_subs failed: "
						"subject: %s, subst str: %s (%d)\n",
						sasl_user_and_realm, dp->template_search_filter, rc);
				} else {
					/* these values are internal regex representations with
					 * lots of unprintable control chars - escape for logging */
					LDAPDebug( LDAP_DEBUG_TRACE,
						"mapped base dn: %s, filter: %s\n",
						escape_string( *ldap_search_base, escape_base ),
					escape_string( *ldap_search_filter, escape_filt ), 0 );
					ret = 1;
				}
			}
		} else {
			LDAPDebug( LDAP_DEBUG_ANY,
				"sasl_map_check: slapi_re_exec failed: "
				"regex: %s, subject: %s (%d)\n",
				dp->regular_expression, sasl_user_and_realm, matched);
		}
		slapi_re_free(re);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<- sasl_map_check\n", 0, 0, 0 );
	return ret;
}

static char *
sasl_map_str_concat(char *s1, char *s2)
{
	if (NULL == s2) {
		return (slapi_ch_strdup(s1));
	} else {
		char *newstr = slapi_ch_smprintf("%s@%s",s1,s2);
		return newstr;
	}
}

/* Actually perform a mapping 
 * Takes a sasl identity string, and returns an LDAP search spec to be used to find the entry
 * returns 1 if matched, 0 otherwise
 */
int
sasl_map_domap(char *sasl_user, char *sasl_realm, char **ldap_search_base, char **ldap_search_filter)
{
	int ret = 0;
	sasl_map_data *this_map = NULL;
	char *sasl_user_and_realm = NULL;
	sasl_map_private *priv = sasl_map_get_global_priv();
	*ldap_search_base = NULL;
	*ldap_search_filter = NULL;
	LDAPDebug( LDAP_DEBUG_TRACE, "-> sasl_map_domap\n", 0, 0, 0 );
	sasl_user_and_realm = sasl_map_str_concat(sasl_user,sasl_realm);
	/* Walk the list of maps */
	this_map = sasl_map_first(priv);
	while (this_map) {
		int matched = 0;
		/* If one matches, then make the search params */
		LDAPDebug( LDAP_DEBUG_TRACE, "sasl_map_domap - trying map [%s]\n", this_map->name, 0, 0 );
		matched = sasl_map_check(this_map, sasl_user_and_realm, ldap_search_base, ldap_search_filter);
		if (1 == matched) {
			ret = 1;
			break;
		}
		this_map = sasl_map_next(this_map);
	}
	if (sasl_user_and_realm) {
		slapi_ch_free((void**)&sasl_user_and_realm);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<- sasl_map_domap (%s)\n", (1 == ret) ? "mapped" : "not mapped", 0, 0 );
	return ret;
}

