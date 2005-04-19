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
/* backend_manager.c - routines for dealing with back-end databases */

#include "slap.h"

#define BACKEND_GRAB_SIZE	10

/* JCM - searching the backend array is linear... */

static int defsize = SLAPD_DEFAULT_SIZELIMIT;
static int deftime = SLAPD_DEFAULT_TIMELIMIT;
static int nbackends= 0;
static Slapi_Backend **backends= NULL;
static int maxbackends= 0;

Slapi_Backend *
slapi_be_new( const char *type, const char *name, int isprivate, int logchanges )
{
	Slapi_Backend *be;
	int i;

	/* should add some locking here to prevent concurrent access */
	if ( nbackends == maxbackends )
	{
		int oldsize = maxbackends;
		maxbackends += BACKEND_GRAB_SIZE;
		backends = (Slapi_Backend **) slapi_ch_realloc( (char *) backends, maxbackends * sizeof(Slapi_Backend *) );
		memset( &backends[oldsize], '\0', BACKEND_GRAB_SIZE * sizeof(Slapi_Backend *) );
	}

	for (i=0; ((i<maxbackends) && (backends[i])); i++)
		;

	PR_ASSERT(i<maxbackends);

	be = (Slapi_Backend *) slapi_ch_calloc(1, sizeof(Slapi_Backend));
	be->be_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, name );
	be_init( be, type, name, isprivate, logchanges, defsize, deftime );

	backends[i] = be;
	nbackends++;
	return( be );
}

void 
slapi_be_stopping (Slapi_Backend *be)
{
	int i;

	PR_Lock (be->be_state_lock);
	for (i=0; ((i<maxbackends) && backends[i] != be); i++)
		;

	PR_ASSERT(i<maxbackends);

	backends[i] = NULL;
	be->be_state = BE_STATE_DELETED;
	if (be->be_lock != NULL)
	{
		PR_DestroyRWLock(be->be_lock);
		be->be_lock = NULL;
	}

	nbackends--;
	PR_Unlock (be->be_state_lock);
}


void
slapi_be_free(Slapi_Backend **be)
{
    be_done(*be);
    slapi_ch_free((void**)be);
    *be = NULL;
}

static int
be_plgfn_unwillingtoperform(Slapi_PBlock *pb)
{
    send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, "Operation on Directory Specific Entry not allowed", 0, NULL );
    return -1;
}

/* JCM - Seems rather DSE specific... why's it here?... Should be in fedse.c... */

Slapi_Backend *
be_new_internal(struct dse *pdse, const char *type, const char *name)
{
    Slapi_Backend *be= slapi_be_new(type, name, 1 /* Private */, 0 /* Do Not Log Changes */);
	be->be_database = (struct slapdplugin *) slapi_ch_calloc( 1, sizeof(struct slapdplugin) );
    be->be_database->plg_private= (void*)pdse;
    be->be_database->plg_bind= &dse_bind;
    be->be_database->plg_unbind= &dse_unbind;
    be->be_database->plg_search= &dse_search;
    be->be_database->plg_next_search_entry= &dse_next_search_entry;
    be->be_database->plg_compare= &be_plgfn_unwillingtoperform;
    be->be_database->plg_modify= &dse_modify;
    be->be_database->plg_modrdn= &be_plgfn_unwillingtoperform;
    be->be_database->plg_add= &dse_add;
    be->be_database->plg_delete= &dse_delete;
    be->be_database->plg_abandon= &be_plgfn_unwillingtoperform;
    be->be_database->plg_cleanup = dse_deletedse;
    /* All the other function pointers default to NULL */
    return be;
}

Slapi_Backend* 
slapi_get_first_backend (char **cookie)
{
	int i;

	for (i = 0; i < maxbackends; i++)
	{
		if ( backends[i] && (backends[i]->be_state != BE_STATE_DELETED))
		{
			*cookie = (char*)slapi_ch_malloc (sizeof (int));
			memcpy (*cookie, &i, sizeof (int));
			return backends[i];
		}
	}

	return NULL;
}

Slapi_Backend* 
slapi_get_next_backend (char *cookie)
{
	int i, last_be;
	if (cookie == NULL)
	{
		LDAPDebug( LDAP_DEBUG_ARGS, "slapi_get_next_backend: NULL argument\n", 
				  0, 0, 0 );
		return NULL;
	}

	last_be = *(int *)cookie;

	if ( last_be < 0 || last_be >= maxbackends)
	{
		LDAPDebug( LDAP_DEBUG_ARGS, "slapi_get_next_backend: argument out of range\n", 
				  0, 0, 0 );
		return NULL;
	}

	if (last_be == maxbackends - 1)
		return NULL; /* done */

	for (i = last_be + 1; i < maxbackends; i++)
	{
		if (backends[i] && (backends[i]->be_state != BE_STATE_DELETED))
		{
			memcpy (cookie, &i, sizeof (int));
			return backends [i];
		}	
	}

	return NULL;	
}

Slapi_Backend *
g_get_user_backend( int n )
{
	int	i, useri;

	useri = 0;
	for ( i = 0; i < maxbackends; i++ ) {
		if ( (backends[i] == NULL) || (backends[i]->be_private == 1) ) {
			continue;
		}

		if ( useri == n ) {
			if (backends[i]->be_state != BE_STATE_DELETED)
				return backends[i];				
			else
				return NULL;
		}
		useri++;
	}
	return NULL;
}

void
g_set_deftime(int val)
{
	deftime = val;
}

void
g_set_defsize(int val)
{
	defsize = val;
}

int
g_get_deftime()
{
	return deftime;
}

int
g_get_defsize()
{
	return defsize;
}

int strtrimcasecmp(const char *s1, const char *s2)
{
	char * s1bis, *s2bis;
	int len_s1 = 0, len_s2 = 0;

	if ( ((s1 == NULL) && (s2 != NULL))
		|| ((s2 == NULL) && (s1 != NULL)) )
		return 1;

	if ((s1 == NULL) && (s2 == NULL))
		return 0;

	while (*s1 == ' ')
		s1++;

	while (*s2 == ' ')
		s2++;

	s1bis = (char *) s1;
	while ((*s1bis != ' ') && (*s1bis != 0))
	{
		len_s1 ++;
		s1bis ++;
	}

	s2bis = (char *) s2;
	while ((*s2bis != ' ') && (*s2bis != 0))
	{
		len_s2 ++;
		s2bis ++;
	}

	if (len_s2 != len_s1)
		return 1;

	return strncasecmp(s1, s2, len_s1);
}
/*
 * Find the backend of the given type.
 */
Slapi_Backend *
slapi_be_select_by_instance_name( const char *name )
{
	int i;
	for ( i = 0; i < maxbackends; i++ )
	{
		if ( backends[i] && (backends[i]->be_state != BE_STATE_DELETED) &&
			strtrimcasecmp( backends[i]->be_name, name ) == 0)
		{
			return backends[i];
		}
	}
	return NULL;
}

/* void
be_cleanupall()
{
	int		i;
	Slapi_PBlock	pb;

    for ( i = 0; i < maxbackends; i++ ) 
	{
		if ( backends[i] &&
			 backends[i]->be_cleanup != NULL &&
			(backends[i]->be_state == BE_STATE_STOPPED ||
			 backends[i]->be_state == BE_STATE_DELETED)) 
		{
			slapi_pblock_set( &pb, SLAPI_PLUGIN, backends[i]->be_database );
			slapi_pblock_set( &pb, SLAPI_BACKEND, backends[i] );
            
    		(*backends[i]->be_cleanup)( &pb );
		}
	}
}*/

void
be_cleanupall()
{
    int             i;
    Slapi_PBlock    pb;

    for ( i = 0; i < maxbackends; i++ ) 
    {
        if (backends[i] &&
            backends[i]->be_cleanup != NULL &&
            (backends[i]->be_state == BE_STATE_STOPPED ||
            backends[i]->be_state == BE_STATE_DELETED)) 
        {
            slapi_pblock_set( &pb, SLAPI_PLUGIN, backends[i]->be_database );
            slapi_pblock_set( &pb, SLAPI_BACKEND, backends[i] );

            (*backends[i]->be_cleanup)( &pb );
            be_done(backends[i]);
            slapi_ch_free((void **)&backends[i]);
        }
    }
    slapi_ch_free((void**)&backends);
}

void
be_flushall()
{
	int		i;
	Slapi_PBlock	pb;

	for ( i = 0; i < maxbackends; i++ )
	{
		if ( backends[i] &&
			 backends[i]->be_state == BE_STATE_STARTED &&
			 backends[i]->be_flush != NULL )
		{
			slapi_pblock_set( &pb, SLAPI_PLUGIN,  backends[i]->be_database );
			slapi_pblock_set( &pb, SLAPI_BACKEND, backends[i] );            
    		(*backends[i]->be_flush)( &pb );
		}
	}
}

void
be_unbindall(Connection *conn, Operation *op)
{
	int		i;
	Slapi_PBlock	pb;

	for ( i = 0; i < maxbackends; i++ )
	{
		if ( backends[i] && (backends[i]->be_unbind != NULL) )
		{
			pblock_init_common( &pb, backends[i], conn, op );

			if ( plugin_call_plugins( &pb, SLAPI_PLUGIN_PRE_UNBIND_FN ) == 0 )
			{
				int	rc;
				slapi_pblock_set( &pb, SLAPI_PLUGIN, backends[i]->be_database );
                if(backends[i]->be_state != BE_STATE_DELETED && 
				   backends[i]->be_unbind!=NULL)
                {
    				rc = (*backends[i]->be_unbind)( &pb );
                }
				slapi_pblock_set( &pb, SLAPI_PLUGIN_OPRETURN, &rc );
				(void) plugin_call_plugins( &pb, SLAPI_PLUGIN_POST_UNBIND_FN );
			}
		}
	}
}

int
be_nbackends_public()
{
    int i;
	int	n= 0;
	for ( i = 0; i < maxbackends; i++ )
	{
		if ( backends[i] && 
			(backends[i]->be_state != BE_STATE_DELETED) &&
			(!backends[i]->be_private) )
        {
            n++;
		}
	}
    return n;
}

/* backend instance management */
/* JCM - These are hardcoded for the LDBM database */
#define LDBM_CLASS_PREFIX	"cn=ldbm database,cn=plugins,cn=config"
#define LDBM_CONFIG_ENTRY	"cn=config,cn=ldbm database,cn=plugins,cn=config"	
#define INSTANCE_ATTR		"nsslapd-instance"
#define	SUFFIX_ATTR			"nsslapd-suffix"
#define CACHE_ATTR			"nsslapd-cachememsize"

void 
slapi_be_Rlock(Slapi_Backend * be)
{
	PR_RWLock_Rlock(be->be_lock);
}

void
slapi_be_Wlock(Slapi_Backend * be)
{
	PR_RWLock_Wlock(be->be_lock);
}

void
slapi_be_Unlock(Slapi_Backend * be)
{
	PR_RWLock_Unlock(be->be_lock);
}

/*
 * lookup instance names by suffix.
 * if isexact == 0: returns instances including ones that associates with
 *					its sub suffixes.
 *					e.g., suffix: "o=<suffix>" is given, these are returned:
 *						  suffixes: o=<suffix>, ou=<ou>,o=<suffix>, ...
 *						  instances: inst of "o=<suffix>",
 *									 inst of "ou=<ou>,o=<suffix>",
 *										...
 * if isexact != 0: returns an instance that associates with the given suffix
 *					e.g., suffix: "o=<suffix>" is given, these are returned:
 *						  suffixes: "o=<suffix>"
 *						  instances: inst of "o=<suffix>"
 * Note: if suffixes 
 */
int
slapi_lookup_instance_name_by_suffix(char *suffix,
							char ***suffixes, char ***instances, int isexact)
{
    Slapi_Backend *be = NULL;
    char *cookie = NULL;
	const char *thisdn;
	int thisdnlen;
	int suffixlen;
	int i;
	int rval = -1;

	if (instances == NULL)
		return rval;

	PR_ASSERT(suffix);

	rval = 0;
	suffixlen = strlen(suffix);
    cookie = NULL;
    be = slapi_get_first_backend (&cookie);
    while (be) {
       	if (NULL == be->be_suffix) {
       		be = (backend *)slapi_get_next_backend (cookie);
			continue;
		}
        PR_Lock(be->be_suffixlock);
    	for (i = 0; be->be_suffix && i < be->be_suffixcount; i++) {
    		thisdn = slapi_sdn_get_ndn(be->be_suffix[i]);
    		thisdnlen = slapi_sdn_get_ndn_len(be->be_suffix[i]);
			if (isexact?suffixlen!=thisdnlen:suffixlen>thisdnlen)
				continue;
			if (isexact?(!slapi_UTF8CASECMP(suffix, (char *)thisdn)):
				(!slapi_UTF8CASECMP(suffix,
									(char *)thisdn+thisdnlen-suffixlen))) {
				charray_add(instances, slapi_ch_strdup(be->be_name));
				if (suffixes)
					charray_add(suffixes, slapi_ch_strdup(thisdn));
			}
		}
        PR_Unlock(be->be_suffixlock);
       	be = (backend *)slapi_get_next_backend (cookie);
    }
	
	return rval;
}

/*
 * lookup instance names by included suffixes and excluded suffixes.
 *
 * Get instance names associated with the given included suffixes
 * as well as the excluded suffixes.
 * Subtract the excluded instances from the included instance.
 * Assign the result to instances.
 */
int
slapi_lookup_instance_name_by_suffixes(char **included, char **excluded,
									   char ***instances)
{
	char **incl_instances, **excl_instances;
	char **p;
	int rval = -1;

	if (instances == NULL)
		return rval;

	*instances = NULL;
	incl_instances = NULL;
	for (p = included; p && *p; p++) {
		if (slapi_lookup_instance_name_by_suffix(*p, NULL, &incl_instances, 0)
																	< 0)
			return rval;
	}

	excl_instances = NULL;
	for (p = excluded; p && *p; p++) {
		/* okay to be empty */
		slapi_lookup_instance_name_by_suffix(*p, NULL, &excl_instances, 0);
	}

	rval = 0;
	if (excl_instances) {
		charray_subtract(incl_instances, excl_instances, NULL);
		charray_free(excl_instances);
	}
	*instances = incl_instances;
	return rval;
}
