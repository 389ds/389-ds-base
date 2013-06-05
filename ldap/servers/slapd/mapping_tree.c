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

/* mapping_tree.c - Maps the DIT onto backends and/or referrals. */


#include "slap.h"

/* distribution plugin prototype */
typedef int (* mtn_distrib_fct) (Slapi_PBlock *pb, Slapi_DN * target_dn,
         char **mtn_be_names, int be_count, Slapi_DN * mtn_node_dn, int *mtn_be_states);

struct mt_node
{
    Slapi_DN *mtn_subtree;      /* dn for the node's subtree */
    Slapi_Backend **mtn_be;     /* backend pointer - the list of backends implementing this 
                                 * node usually there is only one back-end here, there can
                                 * be several only when distribution is done on this node
                                 */
    int *mtn_be_states;            /* states for the backends in table mtn_be */
    char **mtn_backend_names;   /* list of backend names */
    int mtn_be_list_size;        /* size of the previous three tables */
    int mtn_be_count;                /* number of backends implementing this node */
    char **mtn_referral;            /* referral or list of referrals */
    Slapi_Entry *mtn_referral_entry;            /* referral or list of referrals */
    struct mt_node *mtn_children; 
    struct mt_node *mtn_parent;
    struct mt_node *mtn_brother;  /* list of other nodes with the same parent */
    int mtn_state;
    int mtn_private;              /* Never show this node to the user. only used for 
                                   * cn=config, cn=schema and root node */
    char * mtn_dstr_plg_lib;      /* distribution plugin library name */
    char * mtn_dstr_plg_name;      /* distribution plugin function name */
    mtn_distrib_fct mtn_dstr_plg;          /* pointer to the actual ditribution function */
    void *mtn_extension;          /* plugins can extend a mapping tree node */
};

#define BE_LIST_INIT_SIZE 10
#define BE_LIST_INCREMENT 10

/* Mapping tree locking strategy
 *
 * There are two goals for the mapping tree locking 
 * - protect the mapping tree structures 
 * - prevent stop or re-initialisation of a backend which is currently
 *   used by an LDAP operation
 * This must be done without preventing parallelisation of LDAP operations
 *
 * we use
 * - one RW lock will be used to protect access to mapping tree structures 
 *   accessed throuh mtn_lock(), mtn_unlock(), mtn_wlock()
 * - one RW lock for each backend. This lock is taken in READ for each
 *   LDAP operation in progree on the backend
 *   and is taken in write for administrative operations like stop or 
 *   disable the backend
 *   accessed through slapi_be_Rlock(), slapi_be_Wlock(), slapi_be_Unlock()
 * - a state flag for each backend : mtn_be_states[]
 *   this state is set to SLAPI_BE_STATE_DELETE or SLAPI_BE_STATE_OFFLINE at the beginning
 *   of stop/disable operation to ensure that no new operation starts 
 *   while the backend is stopped/disabled
 *
 * The algorithme for LDAP OPERATIONS is :
 *
 *      lock mapping tree in read mode 
 *      get backend    
 *      check that backend is not in SLAPI_BE_STATE_DELETE or SLAPI_BE_STATE_OFFLINE state
 *      lock backend in read mode
 *      unlock mapping tree 
 *      do LDAP operation
 *      release backend lock
 *
 * The algorithme for maintenance operation is 
 *      lock mapping tree in write mode
 *      set state to SLAPI_BE_STATE_DELETE or SLAPI_BE_STATE_OFFLINE
 *      unlock mapping tree
 *      get backend lock in write mode
 *      release backend lock 
 *
 */
static Slapi_RWLock    *myLock;    /* global lock on the mapping tree structures */


static mapping_tree_node *mapping_tree_root = NULL;
static int mapping_tree_inited = 0;
static int mapping_tree_freed = 0;
static int extension_type = -1;    /* type returned from the factory */

/* The different states a mapping tree node can be in. */
#define    MTN_DISABLED                 0    /* The server acts like the node isn't there. */
#define MTN_BACKEND                  1    /* This node represents a backend. */
#define MTN_REFERRAL                 2    /* A referral is returned instead of a backend. */
#define MTN_REFERRAL_ON_UPDATE         3    /* A referral is returned for update operations. */
#define MTN_CONTAINER                 4    /* This node represents a container for backends. */ 

/* Need to add a modifier flag to the state - such as round robin */

/* Note: This DN is no need to be normalized. */
#define MAPPING_TREE_BASE_DN "cn=mapping tree,cn=config"
#define MAPPING_TREE_PARENT_ATTRIBUTE "nsslapd-parent-suffix"

void mtn_wlock();
void mtn_lock();
void mtn_unlock();

static mapping_tree_node * mtn_get_mapping_tree_node_by_entry(
    mapping_tree_node* node, const Slapi_DN *dn);
static void mtn_remove_node(mapping_tree_node * node);
static void mtn_free_node (mapping_tree_node **node);
static int mtn_get_be_distributed(Slapi_PBlock *pb,
    mapping_tree_node * target_node, Slapi_DN *target_sdn, int * flag_stop);
static int mtn_get_be(mapping_tree_node *target_node, Slapi_PBlock *pb,
    Slapi_Backend **be, int * index, Slapi_Entry **referral, char *errorbuf);
static mapping_tree_node * mtn_get_next_node(mapping_tree_node * node,
    mapping_tree_node * node_list, int scope);
static mapping_tree_node * mtn_get_first_node(mapping_tree_node * node,
    int scope);
static mapping_tree_node *
    get_mapping_tree_node_by_name(mapping_tree_node * node, char * be_name);
static int _mtn_update_config_param(int op, char *type, char *strvalue);

#ifdef DEBUG
#ifdef USE_DUMP_MAPPING_TREE
static void dump_mapping_tree(mapping_tree_node *parent, int depth);
#endif
#endif

/* structure and static local variable used to store the
 * list of plugins that have registered to a callback when backend state
 * change
 */
struct mtn_be_ch_list { 
    void * handle;
    struct mtn_be_ch_list * next;
    slapi_backend_state_change_fnptr fnct;
    };

static struct mtn_be_ch_list * mtn_plug_list = NULL;

/* API for registering to a callback when backend state change */
void slapi_register_backend_state_change(void * handle, slapi_backend_state_change_fnptr funct)
{
     struct mtn_be_ch_list * new_be_ch_plg;
     new_be_ch_plg = (struct mtn_be_ch_list *)
                         slapi_ch_malloc(sizeof(struct mtn_be_ch_list));
     new_be_ch_plg->next = mtn_plug_list;
     new_be_ch_plg->handle = handle;
     new_be_ch_plg->fnct = funct;
     mtn_plug_list = new_be_ch_plg;
}

/* To unregister all the state change callbacks registered on the mapping tree */
int slapi_unregister_backend_state_change_all()
{
    struct mtn_be_ch_list *cur_be_ch_plg;
    while (mtn_plug_list)
    {
        cur_be_ch_plg = mtn_plug_list;
        mtn_plug_list = mtn_plug_list->next;
        slapi_ch_free((void **) &cur_be_ch_plg);
    }
    return 1;
}


int slapi_unregister_backend_state_change(void * handle)
{
    struct mtn_be_ch_list * cur_be_ch_plg = mtn_plug_list;
    struct mtn_be_ch_list * prev_be_ch_plg = mtn_plug_list;
    while (cur_be_ch_plg)
    {
        if (cur_be_ch_plg->handle == handle)
        {
            if (cur_be_ch_plg == mtn_plug_list)
            {
                mtn_plug_list = mtn_plug_list->next;
                slapi_ch_free((void **) &cur_be_ch_plg);
                return 0;
            }
            else
            {
                prev_be_ch_plg->next = cur_be_ch_plg->next;
                slapi_ch_free((void **) &cur_be_ch_plg);
                return 0;
            }
        }
        prev_be_ch_plg = cur_be_ch_plg;
        cur_be_ch_plg = cur_be_ch_plg->next;
    }
    return 1;
}

void mtn_be_state_change(char * be_name, int old_state, int new_state)
{
    struct mtn_be_ch_list * cur_be_ch_plg = mtn_plug_list;

    while (cur_be_ch_plg)
    {
        (* (cur_be_ch_plg->fnct))(cur_be_ch_plg->handle, be_name,
             old_state, new_state);
        cur_be_ch_plg = cur_be_ch_plg->next;
    }
}


Slapi_DN* slapi_mtn_get_dn(mapping_tree_node *node)
{
    return (node->mtn_subtree);
}

/* this will turn an array of url into a referral entry */
static Slapi_Entry *
referral2entry(char ** url_array, Slapi_DN *target_sdn)
{
    int i;
    struct berval bv0,bv1,*bvals[3];
    Slapi_Entry *anEntry;

    if (url_array == NULL)
        return NULL;
        
    anEntry = slapi_entry_alloc();
    slapi_entry_set_sdn(anEntry, target_sdn);

    bvals[2]=NULL;
    bvals[1]=&bv1;
    bv1.bv_val="referral";
    bv1.bv_len=strlen(bv1.bv_val);
    bvals[0]=&bv0;
    bv0.bv_val="top";
    bv0.bv_len=strlen(bv0.bv_val);
    slapi_entry_add_values( anEntry, "objectClass", bvals);

    bvals[1]=NULL;
    for (i=0; url_array[i]; i++) {
        bv0.bv_val=url_array[i];
        bv0.bv_len=strlen(bv0.bv_val);
        slapi_entry_attr_merge( anEntry, "ref", bvals);
    }
    return anEntry;
}


/* mapping tree node extension */
int
mapping_tree_get_extension_type ()
{
    if(extension_type==-1)
    {
        /* The factory is given the name of the object type, in
         * return for a type handle. Whenever the object is created
         * or destroyed the factory is called with the handle so
         * that it may call the constructors or destructors registered
         * with it.
         */
        extension_type= factory_register_type(SLAPI_EXT_MTNODE, 
                                              offsetof(mapping_tree_node, mtn_extension));
    }
    return extension_type;
}

static mapping_tree_node *
mapping_tree_node_new(Slapi_DN *dn, Slapi_Backend **be, char **backend_names, int *be_states,
             int count, int size,
             char **referral, mapping_tree_node *parent,
             int state, int private, char *plg_lib, char *plg_fct,
             mtn_distrib_fct plg)
{
    Slapi_RDN rdn;
    mapping_tree_node *node;
    node = (mapping_tree_node *) slapi_ch_calloc(1, sizeof(mapping_tree_node));
    node->mtn_subtree = dn;
    node->mtn_be = be;
    node->mtn_be_states = be_states;
    node->mtn_backend_names = backend_names;
    node->mtn_referral = referral;
    node->mtn_referral_entry = referral2entry(referral, dn) ;
    node->mtn_parent = parent;
    node->mtn_children = NULL;
    node->mtn_brother = NULL;
    node->mtn_state = state;
    node->mtn_private = private;
    node->mtn_be_list_size = size;
    node->mtn_be_count = count;
    /* We use this count of the rdn components in the mapping tree to help
     * when selecting a mapping tree node for a dn. */
    slapi_rdn_init_sdn(&rdn,dn);
    slapi_rdn_done(&rdn);
    node->mtn_dstr_plg_lib = plg_lib;
    node->mtn_dstr_plg_name = plg_fct;
    node->mtn_dstr_plg = plg;

    slapi_log_error(SLAPI_LOG_TRACE, "mapping_tree",
                    "Created new mapping tree node for suffix [%s] backend [%s] [%p]\n",
                    slapi_sdn_get_dn(dn),
                    backend_names && backend_names[0] ? backend_names[0] : "null",
                    be ? be[0] : NULL);

    return node;
}

/* 
 * Description:
 * Adds a mapping tree node to the child list of another mapping tree node.
 * 
 * Arguments:
 * parent and child are pointers to mapping tree nodes.  child will be added
 * to parent's child list.  For now, the child is added to the head of the 
 * linked list.  Later we may come up a way to ordering the entries in the
 * list.
 *
 * Returns:
 * nothing
 */
static void 
mapping_tree_node_add_child(mapping_tree_node *parent, mapping_tree_node* child)
{
    /* WARNING:
     * As for now the mapping tree is not locked when a child is added 
     * this is possible only because the child is added into the mapping
     * the structure by a single operation after being fully initialized
     * should this be changed, the lock policy would have to be checked
     * see mapping_tree_entry_add_callback() 
     */
    child->mtn_brother = parent->mtn_children;
    parent->mtn_children = child;
    /* for debugging: dump_mapping_tree(mapping_tree_root, 0); */
}

static Slapi_DN *
get_parent_from_entry(Slapi_Entry * entry)
{
    Slapi_Attr *attr = NULL;
    char *origparent = NULL;
    char *parent = NULL;
    Slapi_Value *val = NULL;
    Slapi_DN *parent_sdn = NULL;

    if (slapi_entry_attr_find(entry, MAPPING_TREE_PARENT_ATTRIBUTE, &attr))
        return NULL;

    slapi_attr_first_value(attr, &val);

    origparent = parent = slapi_ch_strdup(slapi_value_get_string(val));
    if (parent) {
        if(*parent == '"') {
            char *ptr = NULL;
            parent++; /* skipping the starting '"' */
            ptr = PL_strnrchr(parent, '"', strlen(parent));
            if (ptr) {
                *ptr = '\0';
            }
        }
        parent_sdn = slapi_sdn_new_dn_byval(parent);
        slapi_ch_free_string(&origparent);
    }
    
    return parent_sdn;
}

/* extract the subtree managed by a mapping tree entry from the entry
 */
static Slapi_DN *
get_subtree_from_entry(Slapi_Entry * entry)
{
    Slapi_Attr *attr = NULL;
    char *origcn = NULL;
    char *cn = NULL;
    Slapi_Value *val = NULL;
    Slapi_DN *subtree = NULL;

    if (slapi_entry_attr_find(entry, "cn", &attr))
        return NULL;

    /* should check that there is only one value for cn attribute */
    slapi_attr_first_value(attr, &val);

    /* The value of cn is the dn of the subtree for this node. 
     * There is a slight problem though.  The cn value is 
     * quoted.  We have to remove the quotes here.  I'm sure
     * there is a proper way to do this, but for now we'll
     * just assume that the first and last chars are ".  Later
     * we'll have to revisit this because things could be a 
     * lot more complicated.  Especially if there are quotes 
     * in the dn of the subtree root dn! */
    /* JCM - Need to dequote correctly. */
    /* GB : I think removing the first and last " in the cn value
     * is the right stuff to do
     */
    origcn = cn = slapi_ch_strdup(slapi_value_get_string(val));
    if (cn) {
        if(*cn == '"') {
            char *ptr = NULL;
            cn++; /* skipping the starting '"' */
            ptr = PL_strnrchr(cn, '"', strlen(cn));
            if (ptr) {
                *ptr = '\0';
            }
        }
        subtree = slapi_sdn_new_dn_byval(cn);
        slapi_ch_free_string(&origcn);
    }
    
    return subtree;
}

static int 
mtn_state_to_int(const char * state_string, Slapi_Entry *entry)
{
    if (!strcasecmp(state_string, "disabled")) {
        return MTN_DISABLED;
    } else if (!strcasecmp(state_string, "backend")) {
        return MTN_BACKEND;
    } else if (!strcasecmp(state_string, "referral")) {
        return MTN_REFERRAL;
    } else if (!strcasecmp(state_string, "referral on update")) {
        return MTN_REFERRAL_ON_UPDATE;
    } else if (!strcasecmp(state_string, "container")) {
        return MTN_CONTAINER;
    } else {
        LDAPDebug(LDAP_DEBUG_ANY,
                "Warning: Unknown state, %s, for mapping tree node %s."
                " Defaulting to DISABLED\n",
                state_string, slapi_entry_get_dn(entry), 0);
        return MTN_DISABLED;
    }
}

static char **
mtn_get_referral_from_entry(Slapi_Entry * entry)
{
    int nb;
    int hint;
    char ** referral;
    Slapi_Attr * attr;
    Slapi_Value *val = NULL;

    if (slapi_entry_attr_find(entry, "nsslapd-referral", &attr))
        return NULL;

    slapi_attr_get_numvalues(attr, &nb);
    hint = slapi_attr_first_value(attr, &val);
    if (NULL == val) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: The nsslapd-referral attribute has no value for the mapping tree node %s\n", slapi_entry_get_dn(entry), 0, 0);
    return NULL;
    }

    referral = (char **) slapi_ch_malloc(sizeof(char *) * (nb+1));
    nb = 0;
    while (val)
    {
        referral[nb++] = slapi_ch_strdup(slapi_value_get_string(val));
        hint = slapi_attr_next_value(attr, hint, &val);
    }
    referral[nb] = NULL;

    return referral;
}
            
static int
get_backends_from_attr(Slapi_Attr *attr, backend ***be_list, char ***be_names,
    int ** be_states, int *be_list_count, int *be_list_size,
    mapping_tree_node * node)
{
    Slapi_Value *val = NULL;
    backend *tmp_be = NULL;
    char * tmp_backend_name;
    int hint;
    mapping_tree_node* new_node;

    *be_list_size = BE_LIST_INIT_SIZE;
    *be_list_count = 0;

    *be_list = (backend **) slapi_ch_malloc(sizeof(backend *) * BE_LIST_INIT_SIZE);
    *be_names = (char **) slapi_ch_malloc(sizeof(char *) * BE_LIST_INIT_SIZE);
    *be_states = (int *) slapi_ch_malloc(sizeof(int) * BE_LIST_INIT_SIZE);

    hint = slapi_attr_first_value(attr, &val);
    if (NULL == val) {
        slapi_ch_free ((void **)be_list);
        *be_list = NULL;
        return 0;
    }

    while (val)
    {
        tmp_backend_name = (char *) slapi_ch_strdup(slapi_value_get_string(val));
           if (*be_list_count >= *be_list_size)
        {
            (*be_list_size) += BE_LIST_INCREMENT;
            *be_names = (char **) slapi_ch_realloc((char *) (*be_names),
                                     sizeof(char *) * (*be_list_size));
            *be_list = (backend **) slapi_ch_realloc((char *) (*be_list),
                                     sizeof(backend *) * (*be_list_size));
            *be_states = (int  *) slapi_ch_realloc((char *) (*be_states),
                                     sizeof(int) * (*be_list_size));
        }
        (*be_names)[*be_list_count] = tmp_backend_name;

        /* set backend as started by default */
        (*be_states)[*be_list_count] = SLAPI_BE_STATE_ON;

        /* We now need to find the backend with name backend_name. */
        tmp_be= slapi_be_select_by_instance_name(tmp_backend_name);
        new_node = get_mapping_tree_node_by_name(mapping_tree_root,
            tmp_backend_name);
        if (new_node && (new_node != node))
        {
                   LDAPDebug(LDAP_DEBUG_ANY,
                   "ERROR: backend %s is already pointed to by a mapping tree"
                " node.  Only one mapping tree node can point to a backend\n",
                   tmp_backend_name, 0, 0);
                tmp_be = NULL;
                return -1;
        }
        if(tmp_be!=NULL)
        {
            tmp_be->be_mapped = 1;
            (*be_list)[*be_list_count] = tmp_be;
        }
        else
           {
            /* It's just not here yet. That's OK. We'll fix it up at runtime. */
            (*be_list)[*be_list_count] = NULL;
        }
        (*be_list_count) ++;
           hint = slapi_attr_next_value(attr, hint, &val);
    }

    return 0;
}

/* 
 * Description:
 * Free the data allocated for mapping tree node arrays
 */
static void
free_mapping_tree_node_arrays(backend ***be_list, char ***be_names,
    int ** be_states, int *be_list_count)
{
    int i;

    /* sanity check */
    PR_ASSERT (be_list != NULL && be_names != NULL && be_states != NULL && be_list_count != NULL);

    if (*be_names != NULL) for (i = 0; i < *be_list_count; ++i) {
        slapi_ch_free ((void **)&((*be_names)[i]));
    }
    slapi_ch_free ((void **)be_names);
    slapi_ch_free ((void **)be_list);
    slapi_ch_free ((void **)be_states);
    *be_list_count = 0;
}

/* 
 * Description:
 * Takes an entry and creates a mapping tree node from it.  Loops through the
 * attributes, pulling needed info from them.  Right now, each node can only
 * have one backend and one referral.  Once we move to supporting more than
 * one node and more than one referral, this function will need to be 
 * massaged a little.
 * 
 * We should make a objectclass for a mapping tree node entry.  That way
 * schema checking would make this function more robust.
 *
 * Arguments:
 * A mapping tree node entry read in from the DIT.
 *
 * Returns:
 * An LDAP result code (LDAP_SUCCESS if all goes well).
 * If the return value is LDAP_SUCCESS, *newnodep is set to the new mapping
 * tree node (guaranteed to be non-NULL).
 */
static int
mapping_tree_entry_add(Slapi_Entry *entry, mapping_tree_node **newnodep )
{
    Slapi_DN *subtree = NULL;
    const char *tmp_ndn;
    int be_list_count = 0;
    int be_list_size = 0;
    backend **be_list = NULL;
    char **be_names = NULL;
    int * be_states = NULL;
    char * plugin_funct = NULL;
    char * plugin_lib = NULL;
    mtn_distrib_fct plugin = NULL;
    
    char **referral = NULL;
    int state = MTN_DISABLED;
    Slapi_Attr *attr = NULL;
    mapping_tree_node *node = NULL;
    mapping_tree_node *parent_node = mapping_tree_root;
    int rc;
    int lderr = LDAP_UNWILLING_TO_PERFORM;    /* our default result code */
    char *tmp_backend_name;
    Slapi_Backend *be;

    PR_ASSERT(newnodep != NULL);
    *newnodep = NULL;

    subtree = get_subtree_from_entry(entry);
    /* Make sure we know the root dn of the subtree for this node. */
    if (NULL == subtree) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: Unable to determine the subtree represented by the mapping tree node %s\n",
            slapi_entry_get_dn(entry), 0, 0);
        return lderr;
    }
    
    tmp_ndn = slapi_sdn_get_ndn( subtree );
    if (tmp_ndn && ( '\0' == *tmp_ndn)) {
     
       /* This entry is associated with the "" subtree.  Treat this is
        * a special case (no parent; will replace the internal root
        * node (mapping_tree_root) with data from this entry).
        */
        slapi_log_error( SLAPI_LOG_ARGS, "mapping_tree_entry_add", "NULL suffix\n" );
        parent_node = NULL;
    }


    /* Make sure a node does not already exist for this subtree */
    if (  parent_node != NULL && NULL != mtn_get_mapping_tree_node_by_entry(mapping_tree_root,
                subtree)) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: a mapping tree node for the"
            " subtree %s already exists; unable to add the node %s\n",
            slapi_sdn_get_dn(subtree), slapi_entry_get_dn(entry), 0);
        slapi_sdn_free(&subtree);
        return LDAP_ALREADY_EXISTS;
    }

    /* Loop through the attributes and handle the ones we care about. */
    for (rc = slapi_entry_first_attr(entry, &attr);
        !rc && attr;
        rc = slapi_entry_next_attr(entry, attr, &attr)) {
        
        char *type = NULL;
        Slapi_Value *val = NULL;

        slapi_attr_get_type(attr, &type);
        if (NULL == type) {
            /* strange... I wonder if we should give a warning here? */
            continue;
        }

        if (!strcasecmp(type, "nsslapd-backend")) {

            if (get_backends_from_attr(attr, &be_list, &be_names, &be_states,
                                       &be_list_count, &be_list_size, NULL)) {
                free_mapping_tree_node_arrays(&be_list, &be_names, &be_states, &be_list_count);
                slapi_sdn_free(&subtree);
                return lderr;
            }

            if (NULL == be_list)
            {
                LDAPDebug(LDAP_DEBUG_ANY,
                "Warning: The nsslapd-backend attribute has no value for the mapping tree node %s\n",
                    slapi_entry_get_dn(entry), 0, 0);
                continue;
            }

        } else if (!strcasecmp(type, "nsslapd-referral")) {
            referral = mtn_get_referral_from_entry(entry);

        } else if (!strcasecmp(type, "nsslapd-state")) {
            const char *state_string;
            
            slapi_attr_first_value(attr, &val);
            if (NULL == val) {
                LDAPDebug(LDAP_DEBUG_ANY, "Warning: Can't determine the state of the mapping tree node %s\n", 
                    slapi_entry_get_dn(entry), 0, 0);
                continue;
            }
            /* Convert the string representation for the state to an int */
            state_string = slapi_value_get_string(val);
            state = mtn_state_to_int(state_string, entry);

        } else if (!strcasecmp(type, "nsslapd-distribution-plugin")) {
            slapi_attr_first_value(attr, &val);
            if (NULL == val) {
                LDAPDebug(LDAP_DEBUG_ANY, "Warning: The nsslapd-distribution-plugin attribute has no value for the mapping tree node %s\n",
                    slapi_entry_get_dn(entry), 0, 0);
                continue;
            }
            plugin_lib = slapi_ch_strdup(slapi_value_get_string(val));
        } else if (!strcasecmp(type, "nsslapd-distribution-funct")) {
            slapi_attr_first_value(attr, &val);
            if (NULL == val) {
                LDAPDebug(LDAP_DEBUG_ANY, "Warning: The nsslapd-distribution-plugin attribute has no value for the mapping tree node %s\n",
                    slapi_entry_get_dn(entry), 0, 0);
                continue;
            }
            plugin_funct = slapi_ch_strdup(slapi_value_get_string(val));
        } else if (!strcasecmp(type, MAPPING_TREE_PARENT_ATTRIBUTE)) {
            Slapi_DN *parent_node_dn = get_parent_from_entry(entry);
            parent_node = mtn_get_mapping_tree_node_by_entry(
                mapping_tree_root, parent_node_dn);

            if (parent_node == NULL)
            {
                parent_node = mapping_tree_root;
                LDAPDebug(LDAP_DEBUG_ANY,
                    "Warning: could not find parent for %s defaulting to root\n",
                     slapi_entry_get_dn(entry), 0, 0);
            }
            slapi_sdn_free(&parent_node_dn);
        }
    }

    if (state == MTN_CONTAINER)
    {
        /* this can be extended later to include the general 
           null suffix, */     
        /* The "default" backend is used by the container node */
        be = defbackend_get_backend();
        if(be == NULL)
        {
             LDAPDebug(LDAP_DEBUG_ANY,
                        "ERROR: default container has not been created for the NULL SUFFIX node.\n",
                         0, 0, 0);
             slapi_sdn_free(&subtree);
             return -1;
        }

        be_list_size = 1;
        be_list_count = 0;

        be_list = (backend **) slapi_ch_calloc(1, sizeof(backend *) );
        be_names = (char **) slapi_ch_calloc(1, sizeof(char *) );
        be_states = (int *) slapi_ch_calloc(1, sizeof(int) );

        tmp_backend_name = (char *) slapi_ch_strdup("default"); /* "NULL_CONTAINER" */
        (be_names)[be_list_count] = tmp_backend_name;

        /* set backend as started by default */
        (be_states)[be_list_count] = SLAPI_BE_STATE_ON;

        be->be_mapped = 1;
        (be_list)[be_list_count] = be; 
        be_list_count++;

    }
    /* check that all required attributes for the givene state are there :
     * state backend -> need nsslapd-backend attribute
     * state referral or referral on update  -> need nsslapd-referral attribute
     */
    
    if (((state == MTN_BACKEND) || (state == MTN_REFERRAL_ON_UPDATE))
         && (be_names == NULL))
    {
        LDAPDebug(LDAP_DEBUG_ANY,
        "ERROR: node %s must define a backend\n",
        slapi_entry_get_dn(entry), 0, 0);
        slapi_sdn_free(&subtree);
        free_mapping_tree_node_arrays(&be_list, &be_names, &be_states, &be_list_count);
        return lderr;
    }
    if (((state == MTN_REFERRAL) || (state == MTN_REFERRAL_ON_UPDATE))
        && (referral == NULL))
    {
        LDAPDebug(LDAP_DEBUG_ANY,
        "ERROR: node %s must define referrals to be in referral state\n",
        slapi_entry_get_dn(entry), 0, 0);
        slapi_sdn_free(&subtree);
        free_mapping_tree_node_arrays(&be_list, &be_names, &be_states, &be_list_count);
        return lderr;
    }

    if (plugin_lib && plugin_funct)
    {
        plugin = (mtn_distrib_fct)sym_load(plugin_lib, plugin_funct,
                                           "Entry Distribution", 1);
        if (plugin == NULL)
        {
            LDAPDebug(LDAP_DEBUG_ANY,
                "ERROR: node %s cannot find distribution plugin. "
                SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
                slapi_entry_get_dn(entry), PR_GetError(), slapd_pr_strerror(PR_GetError()));
            slapi_sdn_free(&subtree);
            slapi_ch_free((void **) &plugin_funct);
            slapi_ch_free((void **) &plugin_lib);
            free_mapping_tree_node_arrays(&be_list, &be_names, &be_states, &be_list_count);
            return lderr;
        }
    }
    else if ((plugin_lib == NULL) && (plugin_funct == NULL))
    {
        /* nothing configured -> OK */
        plugin = NULL;
    }
    else
    {
        /* only one parameter configured -> ERROR */
        LDAPDebug(LDAP_DEBUG_ANY,
            "ERROR: node %s must define both lib and funct"
            " for distribution plugin\n",
            slapi_entry_get_dn(entry), 0, 0);
        slapi_sdn_free(&subtree);
        slapi_ch_free((void **) &plugin_funct);
        slapi_ch_free((void **) &plugin_lib);
        free_mapping_tree_node_arrays(&be_list, &be_names, &be_states, &be_list_count);
        return lderr;
    }
        
    /* Now we can create the node for this mapping tree entry. */
    /* subtree is consumed. */
    node= mapping_tree_node_new(subtree, be_list, be_names, be_states, be_list_count,
             be_list_size, referral, parent_node, state,
             0 /* Normal node. People can see and change it. */,
             plugin_lib, plugin_funct, plugin);

    tmp_ndn = slapi_sdn_get_ndn( subtree );
    if ( NULL != node && NULL == parent_node && tmp_ndn 
                                && ('\0' == *tmp_ndn )) {
        /* The new node is actually the "" node.  Replace the root
         * node with this new one by copying all information (we can't
         * free the root node completely because children of the root
         * node hold pointers to it in their mtn_parent field).
         */

        slapi_log_error( SLAPI_LOG_ARGS, "mapping_tree_entry_add", "fix up NULL suffix\n" );

        node->mtn_children = mapping_tree_root->mtn_children;
        node->mtn_brother = mapping_tree_root->mtn_brother;
        *mapping_tree_root = *node;     /* struct copy */
        slapi_ch_free( (void **)&node );
        node = mapping_tree_root;
    }


    if ( NULL != node ) {
        lderr = LDAP_SUCCESS;
        *newnodep = node;
    }

    return lderr;
}

/*
 * Recursive procedure used to create node extensions once the mapping tree
 * is fully initialized
 * This is done after full init of the mapping tree so that the extensions can do
 * searches
 */
void mtn_create_extension(mapping_tree_node *node)
{
    if (node == NULL)
        return;

    node->mtn_extension = factory_create_extension(mapping_tree_get_extension_type(),
                        node, NULL /* parent */);

    mtn_create_extension(node->mtn_children);
    mtn_create_extension(node->mtn_brother);
}


/* 
 * Description:
 * Does the main work of building the in memory mapping tree form the entries
 * in the DIT.  This function is called recursively on all the given nodes
 * children to build up the tree.  Basically it does an internal search for
 * all the entries who have the target node as a parent.
 * 
 * Arguments:
 * The target node and a flag that tells if it's the root of the tree.
 *
 * Returns:
 * Nothing
 */
static int 
mapping_tree_node_get_children(mapping_tree_node *target, int is_root)
{
    Slapi_PBlock *pb;
    char * filter = NULL;
    int res;
    Slapi_Entry **entries = NULL;
    int x;
    int result = 0;

    pb = slapi_pblock_new();
    
    /* Remember that the root node of the mapping tree is the NULL suffix.
     * Since we don't really support it, children of the root node won't
     * have a MAPPING_TREE_PARENT_ATTRIBUTE. */
    if (is_root) {
        filter = slapi_ch_smprintf("(&(objectclass=nsMappingTree)(!(%s=*)))",
                 MAPPING_TREE_PARENT_ATTRIBUTE);
    } else {
        filter = slapi_ch_smprintf("(&(objectclass=nsMappingTree)(|(%s=\"%s\")(%s=%s)))",
            MAPPING_TREE_PARENT_ATTRIBUTE, 
            slapi_sdn_get_dn(target->mtn_subtree),
            MAPPING_TREE_PARENT_ATTRIBUTE, 
            slapi_sdn_get_dn(target->mtn_subtree));
    }

    slapi_search_internal_set_pb(pb, MAPPING_TREE_BASE_DN, LDAP_SCOPE_ONELEVEL,
        filter, NULL, 0, NULL, NULL, (void *) plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

    if (res != LDAP_SUCCESS) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: Mapping tree unable to read %s: %d\n", 
            MAPPING_TREE_BASE_DN, res, 0);
        result = -1;
        goto done;
    }

    /* We now create the mapping tree node and call this function for each
     * of the target's children. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (NULL == entries) {
        LDAPDebug(LDAP_DEBUG_ANY, "WARNING: No mapping tree node entries found under %s\n",
            MAPPING_TREE_BASE_DN, 0, 0);
        result = -1;
        goto done;
    }

    for(x = 0; entries[x] != NULL; x++) {
        mapping_tree_node *child = NULL;

        if (LDAP_SUCCESS != mapping_tree_entry_add(entries[x], &child)) {
            LDAPDebug(LDAP_DEBUG_ANY,
             "ERROR: could not add mapping tree node %s\n",
            slapi_entry_get_dn(entries[x]), 0, 0);
            result = -1;
            goto done;
        }
        if(target == child){
            /* the mapping tree root got replaced 
             * nothing to do 
             */ 
        } else {

             child->mtn_parent = target;
             mapping_tree_node_add_child(target, child);
        }

            
        if (mapping_tree_node_get_children(child, 0 /* not the root node */))
        {
            result = -1;
            goto done;
        }
    }
    slapi_free_search_results_internal(pb);

done:
    slapi_pblock_destroy(pb);
    if (filter)
        slapi_ch_free((void **) &filter);
    return result;
}

#if 0
/* defined but not used */
/* 
 * Description:
 * A first attempt at walking over the mapping tree and making sure things
 * make sense.  Right now it just makes sure that each parent node has a 
 * subtree that is the suffix of its children's subtrees.  This function
 * is called recursively.
 * 
 * Arguments:
 * The root node of the mapping tree.
 *
 * Returns:
 * Nothing - it just prints warnings.  This should probably change.
 */
static void 
mapping_tree_node_validate(mapping_tree_node *node)
{
    mapping_tree_node *child_entry;

    /* Call this function for all of nodes children */
    for (child_entry = node->mtn_children; child_entry; child_entry = child_entry->mtn_brother) {
        mapping_tree_node_validate(child_entry);
    }

    if (node->mtn_parent) {
        if (!slapi_sdn_issuffix(node->mtn_subtree, node->mtn_parent->mtn_subtree)) {
            LDAPDebug(LDAP_DEBUG_ANY,
                "Warning: Invalid mapping tree.  %s can not be a child of %s\n",
                slapi_sdn_get_ndn(node->mtn_subtree), 
                slapi_sdn_get_ndn(node->mtn_parent->mtn_subtree), 0);
        }
    }
}
#endif

static void
mtn_free_referral_in_node (mapping_tree_node *node)
{
    char ** referral = node->mtn_referral;
    
    if (referral)
    {
        int i;

        for (i=0; referral[i]; i++)
            slapi_ch_free((void **) &(referral[i]));
        slapi_ch_free((void **) &referral);
    }
    if (node->mtn_referral_entry)
        slapi_entry_free(node->mtn_referral_entry);

    node->mtn_referral = NULL;
    node->mtn_referral_entry = NULL;
}

int mapping_tree_entry_modify_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg) 
{
    LDAPMod **mods;
    int i;
    mapping_tree_node * node;
    Slapi_DN * subtree;
    Slapi_Attr * attr;
    Slapi_Value *val = NULL;
    int be_list_count = 0;
    int be_list_size = 0;
    backend **backends = NULL;
    char **be_names = NULL;
    int * be_states = NULL;
    char * plugin_fct = NULL;
    char * plugin_lib = NULL;
    int plugin_flag = 0;
    mtn_distrib_fct plugin = NULL;

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    subtree = get_subtree_from_entry(entryAfter);
    node = mtn_get_mapping_tree_node_by_entry(mapping_tree_root, subtree);
    if (node == NULL)
    {
        /* should never happen */
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    for (i = 0; mods[i] != NULL; i++) {
        if ( (strcasecmp(mods[i]->mod_type, "cn") == 0) ||
             (strcasecmp(mods[i]->mod_type,
                 MAPPING_TREE_PARENT_ATTRIBUTE) == 0) )
        {
            mapping_tree_node * parent_node;
            /* if we are deleting this attribute the new parent 
             * node will be mapping_tree_root
             */
            if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op))
            {
                parent_node = mapping_tree_root;
            }
            else if ((strcasecmp(mods[i]->mod_type, "cn") == 0) &&
                      SLAPI_IS_MOD_ADD(mods[i]->mod_op))
            {
                /* Allow to add an additional cn.
                 * e.g., cn: "<suffix>" for the backward compatibility.
                 * No need to update the mapping tree node itself.
                 */
                continue;
            }
            else
            {
                /* we have to find the new parent node */
                Slapi_DN *parent_node_dn;
                parent_node_dn = get_parent_from_entry(entryAfter);
                parent_node = mtn_get_mapping_tree_node_by_entry(
                    mapping_tree_root, parent_node_dn);
                if (parent_node == NULL)
                {
                    parent_node = mapping_tree_root;
                    LDAPDebug(LDAP_DEBUG_ANY,
                          "Error: could not find parent for %s\n",
                          slapi_entry_get_dn(entryAfter), 0, 0);
                    slapi_sdn_free(&subtree);
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    return SLAPI_DSE_CALLBACK_ERROR;
                }
                slapi_sdn_free(&parent_node_dn);
            }

            mtn_wlock();
            /* modifying the parent of a node means moving it to an 
             * other place of the tree
             * this can be done simply by removing it from its old place and 
             * moving it to the new one
             */
            mtn_remove_node(node);
            mapping_tree_node_add_child(parent_node, node);
            node->mtn_parent = parent_node;
            mtn_unlock();

        }
        else if (strcasecmp(mods[i]->mod_type, "nsslapd-backend" ) == 0)
        {
            slapi_entry_attr_find(entryAfter, "nsslapd-backend", &attr);
            if (NULL == attr)
            {
                /* if nsslapd-backend attribute is empty all backends have
                 * been suppressed, set backend list to NULL
                 * checks on the state are done a bit later 
                 */
                backends = NULL;
                be_names = NULL;
                be_states = NULL;
                be_list_count = 0;
                be_list_size = 0;
            }
            else if (get_backends_from_attr(attr, &backends, &be_names,
                         &be_states, &be_list_count, &be_list_size, node))
            {
                free_mapping_tree_node_arrays(&backends, &be_names, &be_states, &be_list_count);
                slapi_sdn_free(&subtree);
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                return SLAPI_DSE_CALLBACK_ERROR;
            }

            mtn_wlock();

            if ((backends == NULL) && (node->mtn_state == MTN_BACKEND))
            {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "mapping tree entry need at least one nsslapd-backend\n");
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                mtn_unlock();
                free_mapping_tree_node_arrays(&backends, &be_names, &be_states, &be_list_count);
                slapi_sdn_free(&subtree);
                return SLAPI_DSE_CALLBACK_ERROR;
            }

            /* free any old data */
            free_mapping_tree_node_arrays(&node->mtn_be, &node->mtn_backend_names, &node->mtn_be_states, &node->mtn_be_count);
            node->mtn_be_states = be_states;
            node->mtn_be = backends;
            node->mtn_backend_names = be_names;
            node->mtn_be_count = be_list_count;
            node->mtn_be_list_size = be_list_size;

            mtn_unlock();
            
        }
        else if (strcasecmp(mods[i]->mod_type, "nsslapd-state" ) == 0)
        {
            Slapi_Value * val;
            const char * new_state;
            Slapi_Attr * attr;

            /* state change
             * for now only allow replace
             */
            if (!SLAPI_IS_MOD_REPLACE(mods[i]->mod_op))
            {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "must use replace operation to change state\n");
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                slapi_sdn_free(&subtree);
                return SLAPI_DSE_CALLBACK_ERROR;
            }
            if ((mods[i]->mod_bvalues == NULL) || (mods[i]->mod_bvalues[0] == NULL))
            {
                slapi_sdn_free(&subtree);
                *returncode = LDAP_OPERATIONS_ERROR;
                return SLAPI_DSE_CALLBACK_ERROR;
            }

            slapi_entry_attr_find(entryAfter, "nsslapd-state", &attr);
            slapi_attr_first_value(attr, &val);
            new_state = slapi_value_get_string(val);

            if (mtn_state_to_int(new_state, entryAfter) == MTN_BACKEND)
            {
                if (slapi_entry_attr_find(entryAfter, "nsslapd-backend", &attr))
                {
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "need to set nsslapd-backend before moving to backend state\n");
                    slapi_sdn_free(&subtree);
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    return SLAPI_DSE_CALLBACK_ERROR;
                }
            }

            if ((mtn_state_to_int(new_state, entryAfter) == MTN_REFERRAL) ||
                 (mtn_state_to_int(new_state, entryAfter) == MTN_REFERRAL_ON_UPDATE))
            {
                if (slapi_entry_attr_find(entryAfter, "nsslapd-referral", &attr))
                {
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "need to set nsslapd-referral before moving to referral state\n");
                    slapi_sdn_free(&subtree);
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    return SLAPI_DSE_CALLBACK_ERROR;
                }
            }

            mtn_wlock();

            node->mtn_state = mtn_state_to_int(new_state, entryAfter);
                    
            mtn_unlock();
        }
        else if (strcasecmp(mods[i]->mod_type, "nsslapd-referral" ) == 0)
        {
            char ** referral;

            mtn_wlock();

            if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)
                || SLAPI_IS_MOD_ADD(mods[i]->mod_op))
            {
                /* delete old referrals, set new ones */
                mtn_free_referral_in_node(node);
                referral = mtn_get_referral_from_entry(entryAfter);
                node->mtn_referral = referral;
                node->mtn_referral_entry = referral2entry(referral, subtree);
            } else if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op))
            {
                /* it is not OK to delete the referrals if they are still
                 * used 
                 */
                if ((node->mtn_state == MTN_REFERRAL) ||
                    (node->mtn_state == MTN_REFERRAL_ON_UPDATE))
                {
                    PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                             "cannot delete referrals in this state\n");
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    mtn_unlock();
                    slapi_sdn_free(&subtree);
                    return SLAPI_DSE_CALLBACK_ERROR;
                }

                mtn_free_referral_in_node(node);

            } else
            {
                *returncode = LDAP_OPERATIONS_ERROR;
                mtn_unlock();
                slapi_sdn_free(&subtree);
                return SLAPI_DSE_CALLBACK_ERROR;
            }

            mtn_unlock();
            slapi_sdn_free(&subtree);
            *returncode = LDAP_SUCCESS;
            return SLAPI_DSE_CALLBACK_OK;
        }
        else if (strcasecmp(mods[i]->mod_type,
                         "nsslapd-distribution-funct" ) == 0)
        {
            if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)
                || SLAPI_IS_MOD_ADD(mods[i]->mod_op))
            {
                slapi_entry_attr_find(entryAfter,
                             "nsslapd-distribution-funct", &attr);
                slapi_attr_first_value(attr, &val);
                if (NULL == val) {
                    LDAPDebug(LDAP_DEBUG_ANY,
                    "Warning: The nsslapd-distribution-funct attribute"
                    " has no value for the mapping tree node %s\n",
                    slapi_entry_get_dn(entryAfter), 0, 0);
                    plugin_fct = NULL;
                }
                plugin_fct = slapi_ch_strdup(slapi_value_get_string(val));
            }
            else if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op))
            {
                plugin_fct = NULL;
            }
            plugin_flag = 1;
        }
        else if (strcasecmp(mods[i]->mod_type,
                         "nsslapd-distribution-plugin" ) == 0)
        {
            if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)
                || SLAPI_IS_MOD_ADD(mods[i]->mod_op))
            {
                slapi_entry_attr_find(entryAfter,
                             "nsslapd-distribution-plugin", &attr);
                slapi_attr_first_value(attr, &val);
                if (NULL == val) {
                    LDAPDebug(LDAP_DEBUG_ANY,
                        "Warning: The nsslapd-distribution-plugin attribute"
                        " has no value for the mapping tree node %s\n",
                    slapi_entry_get_dn(entryAfter), 0, 0);
                    plugin_lib = NULL;
                }
                plugin_lib = slapi_ch_strdup(slapi_value_get_string(val));
            }
            else if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op))
            {
                plugin_lib = NULL;
            }
            plugin_flag = 1;

        }
    }

    /* if distribution plugin has been configured or modified
     * check that the library and function exist 
     * and if yes apply the modifications
     */
    if (plugin_flag)
    {
        if (plugin_lib && plugin_fct)
        {
            plugin = (mtn_distrib_fct) sym_load(plugin_lib, plugin_fct, "Entry Distribution", 1);

            if (plugin == NULL)
            {
                PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "cannot find distribution plugin\n");
                slapi_ch_free((void **) &plugin_fct);
                slapi_ch_free((void **) &plugin_lib);
                slapi_sdn_free(&subtree);
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                return SLAPI_DSE_CALLBACK_ERROR;
            }
        }
        else if ((plugin_lib == NULL) && (plugin_fct == NULL))
        {
            /* nothing configured -> OK */
            plugin = NULL;
        }
        else
        {
            /* only one parameter configured -> ERROR */
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                 "must define distribution function and library\n");
            slapi_ch_free((void **) &plugin_fct);
            slapi_ch_free((void **) &plugin_lib);
            slapi_sdn_free(&subtree);
            *returncode = LDAP_UNWILLING_TO_PERFORM;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
        
        mtn_wlock();
        if (node->mtn_dstr_plg_lib)
            slapi_ch_free((void **) &node->mtn_dstr_plg_lib);
        node->mtn_dstr_plg_lib = plugin_lib;
        if (node->mtn_dstr_plg_name)
            slapi_ch_free((void **) &node->mtn_dstr_plg_name);
        node->mtn_dstr_plg_name = plugin_fct;
        node->mtn_dstr_plg = plugin;
        mtn_unlock();
    }

    slapi_sdn_free(&subtree);
    return SLAPI_DSE_CALLBACK_OK;
}

int mapping_tree_entry_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg) 
{
    mapping_tree_node *node = NULL;
    int i;
    backend * be;

    /* WARNING
     * for adds we don't need to grab the mapping tree global lock,
     * because the add operation in the tree is atomic because
     * only one pointer is updated in the tree.
     * Should the mapping tree stucture change, this  would have to
     * be checked again 
     */
    *returncode = mapping_tree_entry_add(entryBefore, &node);
    if (LDAP_SUCCESS != *returncode || !node)
    {
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    if(node->mtn_parent != NULL && node != mapping_tree_root )
    {
     /* If the node has a parent and the node is not the mapping tree root,
      * then add it as a child node. Note that the special case when the
      * node is the mapping tree root and has no parent is handled inside
      * the mapping_tree_entry_add() function by replacing the contents of
      * the mapping tree root node with information from the add request.
      */ 
      mapping_tree_node_add_child(node->mtn_parent, node);
    }

    for (i = 0; ((i < node->mtn_be_count) && (node->mtn_backend_names) &&
        (node->mtn_backend_names[i])); i++)
    {
        if ((be = slapi_be_select_by_instance_name(node->mtn_backend_names[i])) 
                && (be->be_state == BE_STATE_STARTED))
        {
            mtn_be_state_change(node->mtn_backend_names[i], SLAPI_BE_STATE_DELETE,
                node->mtn_be_states[i]);
        }
    }

    node->mtn_extension = factory_create_extension(mapping_tree_get_extension_type(), node, NULL);

    /* 
     * Check defaultNamingContext is set.
     * If it is not set, set the to-be-added suffix to the config param.
     */
    if (NULL == config_get_default_naming_context()) {
        char *suffix = 
                slapi_rdn_get_value(slapi_entry_get_nrdn_const(entryBefore));
        char *escaped = slapi_ch_strdup(suffix);
        if (suffix && escaped) {
            strcpy_unescape_value(escaped, suffix);
        }
        if (escaped) {
            int rc = _mtn_update_config_param(LDAP_MOD_REPLACE,
                                              CONFIG_DEFAULT_NAMING_CONTEXT,
                                              escaped);
            if (rc) {
                LDAPDebug(LDAP_DEBUG_ANY,
                    "mapping_tree_entry_add_callback: "
                    "setting %s to %s failed: RC=%d\n",
                    escaped, CONFIG_DEFAULT_NAMING_CONTEXT, rc);
            }
        }
        slapi_ch_free_string(&suffix);
        slapi_ch_free_string(&escaped);
    }

    return SLAPI_DSE_CALLBACK_OK;
}

/* utility function to remove a node from the tree of mapping_tree_node
 */
static void mtn_remove_node(mapping_tree_node * node)
{
    if (node->mtn_parent->mtn_children == node)
        node->mtn_parent->mtn_children = node->mtn_brother;
    else
    {
        mapping_tree_node * tmp_node = node->mtn_parent->mtn_children;
        while (tmp_node && (tmp_node->mtn_brother != node))
            tmp_node = tmp_node->mtn_brother;

        PR_ASSERT(tmp_node != NULL);

        if (tmp_node) tmp_node->mtn_brother = node->mtn_brother;
    }
    node->mtn_brother = NULL;
}

int mapping_tree_entry_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg) 
{
    int result = SLAPI_DSE_CALLBACK_OK;
    mapping_tree_node *node = NULL;
    Slapi_DN * subtree;
    int i;
    int removed = 0;

    mtn_wlock();
    subtree = get_subtree_from_entry(entryBefore);

    if (subtree == NULL)
    {
        /* there is no cn attribute in this entry
         * -> this is not a mapping tree node
         * -> nothing to do
         */
        result = SLAPI_DSE_CALLBACK_OK;
        goto done;
    }

    node = slapi_get_mapping_tree_node_by_dn(subtree);
    if (node == NULL)
    {
        /* should never happen */
        *returncode = LDAP_OPERATIONS_ERROR;
        result = SLAPI_DSE_CALLBACK_ERROR;
        goto done;
    }

    if (slapi_sdn_compare(subtree, node->mtn_subtree))
    {
        /* There is no node associated to this entry
         * -> nothing to do
         */
        result = SLAPI_DSE_CALLBACK_OK;
        goto done;
    }
    
    /* if node has children we must refuse the delete */
    if (node->mtn_children)
    {
        result = SLAPI_DSE_CALLBACK_ERROR;
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "this node has some children");
        goto done;
    }

    /* at this point the node should be different from mapping_tree_root
     * and therefore have a parent 
     */
    PR_ASSERT(node->mtn_parent != NULL);

    /* lets get the node out of the mapping tree */
    mtn_remove_node(node);

    result = SLAPI_DSE_CALLBACK_OK;
    removed = 1;

done:
    mtn_unlock();

    /* Remove defaultNamingContext if it is the to-be-deleted suffix.
     * It should be done outside of mtn lock. */
    if (SLAPI_DSE_CALLBACK_OK == result) {
        char *default_naming_context = config_get_default_naming_context();
        char *suffix, *escaped;
        if (default_naming_context) {
            suffix = 
                  slapi_rdn_get_value(slapi_entry_get_nrdn_const(entryBefore));
            escaped = slapi_ch_strdup(suffix);
            if (suffix && escaped) {
                strcpy_unescape_value(escaped, suffix);
            }
            if (escaped && (0 == strcasecmp(escaped, default_naming_context))) {
                int rc = _mtn_update_config_param(LDAP_MOD_DELETE,
                                                  CONFIG_DEFAULT_NAMING_CONTEXT,
                                                  NULL);
                if (rc) {
                    LDAPDebug2Args(LDAP_DEBUG_ANY,
                                   "mapping_tree_entry_delete_callback: "
                                   "deleting config param %s failed: RC=%d\n",
                                   CONFIG_DEFAULT_NAMING_CONTEXT, rc);
                }
                if (LDAP_SUCCESS == rc) {
                    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE];
                    /* Removing defaultNamingContext from cn=config entry
                     * was successful.  The remove does not reset the
                     * global parameter.  We need to reset it separately. */
                    if (config_set_default_naming_context(
                                                CONFIG_DEFAULT_NAMING_CONTEXT,
                                                NULL, errorbuf, CONFIG_APPLY)) {
                        LDAPDebug2Args(LDAP_DEBUG_ANY,
                                       "mapping_tree_entry_delete_callback: "
                                       "setting NULL to %s failed. %s\n",
                                       CONFIG_DEFAULT_NAMING_CONTEXT, errorbuf);
                    }
                }
            }
            slapi_ch_free_string(&suffix);
            slapi_ch_free_string(&escaped);
        }
    }
    slapi_sdn_free(&subtree);
    if (SLAPI_DSE_CALLBACK_OK == result && removed)
    {
        /* Signal the plugins that a new backend-suffix has been deleted
         * rq : we have to unlock the mapping tree in that case because
         * most of the plugins will try to search upon this notification
         * and should we keep the lock we would end with a dead-lock
         */
        for (i = 0; ((i < node->mtn_be_count) && (node->mtn_backend_names) &&
            (node->mtn_backend_names[i])); i++)
        {
            if ((node->mtn_be_states[i] != SLAPI_BE_STATE_DELETE) && 
                (NULL != slapi_be_select_by_instance_name(
                    node->mtn_backend_names[i])))
            {
                mtn_be_state_change(node->mtn_backend_names[i],
                    node->mtn_be_states[i], SLAPI_BE_STATE_DELETE);
            }
        }

        /* at this point the node is out of the mapping tree, 
         * we can now free the structure
         */
        mtn_free_node(&node);
    }
    return result;
}

/*
 * Add an internal mapping tree node.
 */
static mapping_tree_node * 
add_internal_mapping_tree_node(const char *subtree, Slapi_Backend *be, mapping_tree_node *parent)
{
    Slapi_DN *dn;
    mapping_tree_node *node;
    backend ** be_list = (backend **) slapi_ch_malloc(sizeof(backend *));

    be_list[0] = be;

    dn = slapi_sdn_new_dn_byval(subtree);
    node= mapping_tree_node_new(
            dn,
            be_list,
            NULL, /* backend_name */
            NULL,
            1,    /* number of backends at this node */
            1,    /* size of backend list structure */
            NULL, /* referral */
            parent,
            MTN_BACKEND,
            1, /* The config  node is a private node.
                *  People can't see or change it. */
            NULL, NULL, NULL);
    return node;
}

/* 
 * Description:
 * Inits the mapping tree.  The mapping tree is rooted at a node with
 * subtree "".  Think of this node as the node for the NULL suffix
 * even though we don't really support it.  This function will 
 * create the root node and then consult the DIT for the rest of 
 * the nodes.  It will also add the node for cn=config.
 *
 * One thing to note... Until the mapping tree is inited.  We use
 * slapi_be_select for all our selection needs.  To read in the mapping
 * tree from the DIT, we need to some internal operations.  These 
 * operations need to use slapi_be_select.
 * 
 * Arguments:
 * Nothing
 *
 * Returns:
 * Right now it always returns 0.  This will most likely change.  Right
 * now, we just log warnings when ever something goes wrong.
 */
int 
mapping_tree_init()
{
    Slapi_Backend *be;
    mapping_tree_node *node;
    /* Create the root of the mapping tree. */

    /* The root of the mapping tree is the NULL suffix.  It's always there,
     * but, because we don't really support it, we won't have an entry in
     * the dit for the NULL suffix mapping tree node. */

    /* Once we support the NULL suffix we should do something more clever here.
     * For now will use the current backend we use for "" */

    /* I'm not really sure what the state of the root node should be.  The root
     * node will end up being selected if none of the suffices for the backends
     * would work with the target.  For now when the root node is selected, 
     * the default backend will be returned.  (The special case where the
     * target dn is "" is handled differently.) */
    
    /* we call this function from a single thread, so it should be ok */

    if(mapping_tree_freed){
    /* shutdown has been detected */
      return 0;
    }

    if (mapping_tree_inited)
        return 0;

    /* ONREPL - I have moved this up because otherwise we can endup calling this 
     * function recursively */

    mapping_tree_inited = 1;

    slapi_register_supported_control(MTN_CONTROL_USE_ONE_BACKEND_OID,
                     SLAPI_OPERATION_SEARCH);
    slapi_register_supported_control(MTN_CONTROL_USE_ONE_BACKEND_EXT_OID,
                     SLAPI_OPERATION_SEARCH);

    myLock = slapi_new_rwlock();

    be= slapi_be_select_by_instance_name(DSE_BACKEND);
    mapping_tree_root= add_internal_mapping_tree_node("", be, NULL);

    /* We also need to add the config and schema backends to the mapping tree.
     * They are special in that users will not know about it's node in the 
     * mapping tree.  This is to prevent them from disableing it or 
     * returning a referral for it. */
    node= add_internal_mapping_tree_node("cn=config", be, mapping_tree_root);
    mapping_tree_node_add_child(mapping_tree_root, node);
    node= add_internal_mapping_tree_node("cn=monitor", be, mapping_tree_root);
    mapping_tree_node_add_child(mapping_tree_root, node);
    be= slapi_be_select_by_instance_name( DSE_SCHEMA );
    node= add_internal_mapping_tree_node("cn=schema", be, mapping_tree_root);
    mapping_tree_node_add_child(mapping_tree_root, node);

    /* 
     * Now we need to look under cn=mapping tree, cn=config to find the rest
     * of the mapping tree entries.
     * Builds the mapping tree from entries in the DIT.  This function just
     * calls mapping_tree_node_get_children with the special case for the
     * root node.
     */
    if (mapping_tree_node_get_children(mapping_tree_root, 1))
        return -1;

    mtn_create_extension(mapping_tree_root);

    /* setup the dse callback functions for the ldbm instance config entry */
    {
        slapi_config_register_callback(SLAPI_OPERATION_MODIFY,
             DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_SUBTREE,
             "(objectclass=nsMappingTree)",
             mapping_tree_entry_modify_callback, NULL);
        slapi_config_register_callback(SLAPI_OPERATION_ADD,
            DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_SUBTREE,
            "(objectclass=nsMappingTree)", mapping_tree_entry_add_callback,
            NULL);
        slapi_config_register_callback(SLAPI_OPERATION_DELETE,
            DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_SUBTREE,
            "(objectclass=nsMappingTree)", mapping_tree_entry_delete_callback,
            NULL);
    }
    return 0;
}

static void
mtn_free_node  (mapping_tree_node **node)
{
    mapping_tree_node *child = (*node)->mtn_children;

    /* free children first */
    while  (child)
    {
        mapping_tree_node * tmp_child = child->mtn_brother;
        mtn_free_node (&child);
        child = tmp_child;
    }
    (*node)->mtn_children = NULL;
    (*node)->mtn_parent = NULL;

    /* free this node */
    /* ONREPL - not quite sure which fields should be freed. For now,
       only freeing fields explicitely allocated in the new_node function */
    factory_destroy_extension (mapping_tree_get_extension_type(), *node, NULL,
                               &((*node)->mtn_extension));

    slapi_sdn_free(&((*node)->mtn_subtree));

    mtn_free_referral_in_node(*node);

    if ((*node)->mtn_be_count > 0)
    {
        free_mapping_tree_node_arrays(&((*node)->mtn_be), &((*node)->mtn_backend_names),
                                      &((*node)->mtn_be_states), &((*node)->mtn_be_count)); 
    }

    slapi_ch_free_string(&((*node)->mtn_dstr_plg_lib));
    slapi_ch_free_string(&((*node)->mtn_dstr_plg_name));

    slapi_ch_free ((void**) node);
}

/* Description: frees the tree; should be called when the server shuts down
 */
void 
mapping_tree_free ()
{
    /* unregister dse callbacks */
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_BASE, "(objectclass=*)", mapping_tree_entry_modify_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_BASE, "(objectclass=*)", mapping_tree_entry_add_callback);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, MAPPING_TREE_BASE_DN, LDAP_SCOPE_BASE, "(objectclass=*)", mapping_tree_entry_delete_callback);

    /* The state change plugins registered on the mapping tree
     * should not get any state change information 
     * - unregister all those callbacks
     */ 
    slapi_unregister_backend_state_change_all();
    /* recursively free tree nodes */
    mtn_free_node (&mapping_tree_root);
    mapping_tree_freed = 1;
}

/* This function returns the first node to parse when a search is done 
 * on a given node in the mapping tree
 */
static mapping_tree_node *
mtn_get_first_node(mapping_tree_node * node, int scope)
{
    if (node == NULL) 
        return NULL;

    /* never climb down the tree from base "" */
    if (node == mapping_tree_root)
    {
        return node;
    }

    if (scope == LDAP_SCOPE_BASE)
        return node;

    if (scope == LDAP_SCOPE_ONELEVEL)
    {
         if (node->mtn_children)
            return node->mtn_children;
        else
            return node;
    }

    while (node->mtn_children)
        node = node->mtn_children;

    return node;
}

int slapi_mtn_get_first_be(mapping_tree_node * node_list,
         mapping_tree_node ** node, Slapi_PBlock *pb, Slapi_Backend **be,
         int * be_index, Slapi_Entry **referral, char *errorbuf, int scope)
{
    *node = mtn_get_first_node(node_list, scope);
    if (scope == LDAP_SCOPE_BASE)
        *be_index = -1;
    else
        *be_index = 0;

    return mtn_get_be(*node, pb, be, be_index, referral, errorbuf);
}

int slapi_mtn_get_next_be(mapping_tree_node * node_list,
         mapping_tree_node ** node, Slapi_PBlock *pb, Slapi_Backend **be,
         int * be_index, Slapi_Entry **referral, char *errorbuf, int scope)
{
    int rc;

    if (((*node)->mtn_parent == NULL) ||  /* -> node has been deleted */
        (scope == LDAP_SCOPE_BASE)) 

    {
        *node = NULL;
        *be = NULL;
        *referral = NULL;
        return 0;
    }
        
    /* never climb down the tree from the rootDSE */
    if (node_list == mapping_tree_root)
    {
        *node = NULL;
        *be = NULL;
        *referral = NULL;
        return 0;
    }

    rc = mtn_get_be(*node, pb, be, be_index, referral, errorbuf);

    if (rc != LDAP_SUCCESS)
    {
        *node = mtn_get_next_node(*node, node_list, scope);
        return rc;
    }

    if  ((*be == NULL) && (*referral == NULL))
    {
        *node = mtn_get_next_node(*node, node_list, scope);
        if (*node == NULL)
        {
            *be = NULL;
            return 0;
        }
        *be_index = 0;
        return mtn_get_be(*node, pb, be, be_index, referral, errorbuf);
    }

    return LDAP_SUCCESS;
}

/* This function returns the next node to parse when a subtree search is done 
 * on a given node in the mapping tree
 */
static mapping_tree_node *
mtn_get_next_node(mapping_tree_node * node, mapping_tree_node * node_list, int scope)
{
    if (scope == LDAP_SCOPE_BASE)
        return NULL;

    /* if we are back to the top of the subtree searched then we have finished */
    if (node == node_list)
        node = NULL;

    else if (node->mtn_brother)
    {
        node = node->mtn_brother;
        if (scope == LDAP_SCOPE_SUBTREE)
            while (node->mtn_children)
                node = node->mtn_children;
    }
    else
        node = node->mtn_parent;

    return node;
}

/* Description :
 * return 0 if the given entry does not have any child node in the mapping tree
 * != otherwise
 *
 */
int
mtn_sdn_has_child(Slapi_DN *target_sdn)
{
    mapping_tree_node *node;

    /* algo : get the target node for the given dn
     * then loop through all its child  to check if one of them is below 
     * the target dn
     */
    node = slapi_get_mapping_tree_node_by_dn(target_sdn);
    
    /* if there is no node for this dn then there is no child either */
    if (node == NULL)
        return 0;

    node = node->mtn_children;
    while (node)
    {
        if (slapi_sdn_issuffix(node->mtn_subtree, target_sdn))
            return 1;
        node = node->mtn_brother;
    }
    return 0;
}


/* Description:
 * Find the backend that would be used to store a dn.
 */
Slapi_Backend *slapi_mapping_tree_find_backend_for_sdn(Slapi_DN *sdn)
{
    mapping_tree_node *target_node;
    Slapi_Backend *be;
    int flag_stop = 0, index;
    Slapi_PBlock *pb;
    Slapi_Operation *op;

    mtn_lock();
    target_node = slapi_get_mapping_tree_node_by_dn(sdn);

    if ((target_node == mapping_tree_root) &&
        (slapi_sdn_get_ndn_len(sdn) > 0)) {
        /* couldn't find a matching node */
        be = defbackend_get_backend();
        goto done;
    }

    if ((target_node == NULL) || (target_node->mtn_be_count == 0)) {
        /* no backend configured for this node */
        be = NULL;
        goto done;
    }

    if (target_node->mtn_be_count == 1) {
        /* not distributed, so we've already found it */
        if (target_node->mtn_be[0] == NULL) {
            target_node->mtn_be[0] = slapi_be_select_by_instance_name(
                target_node->mtn_backend_names[0]);
        }
        be = target_node->mtn_be[0];
        goto done;
    }

    /* have to call the distribution plugin */
    be = defbackend_get_backend();
    pb = slapi_pblock_new();
    if (!pb) {
        goto done;
    }
    op = internal_operation_new(SLAPI_OPERATION_ADD, 0);
    if (!op) {
        slapi_pblock_destroy(pb);
        goto done;
    }
    operation_set_target_spec(op, sdn);
    slapi_pblock_set(pb, SLAPI_OPERATION, op);
    index = mtn_get_be_distributed(pb, target_node, sdn, &flag_stop);
    slapi_pblock_destroy(pb);   /* also frees the operation */

    if (target_node->mtn_be[index] == NULL) {
        target_node->mtn_be[index] = slapi_be_select_by_instance_name(
            target_node->mtn_backend_names[index]);
    }
    be = target_node->mtn_be[index];

done:
    mtn_unlock();
    return be;
}

/* Check if the target dn is '\0' - the null dn */
static int sdn_is_nulldn(const Slapi_DN *sdn){

    if(sdn){
        const char *dn= slapi_sdn_get_ndn(sdn); 
        if(dn && ( '\0' == *dn)){
                return 1;    
        }
    }
    return 0;
}

/* Checks if a write operation for a particular DN would
 * require a referral to be sent. */
int slapi_dn_write_needs_referral(Slapi_DN *target_sdn, Slapi_Entry **referral)
{
    mapping_tree_node *target_node = NULL;
    int ret = 0;

    if(mapping_tree_freed){
        /* shutdown detected */
        goto done;
    }

    if(!mapping_tree_inited) {
        mapping_tree_init();
    }

    if (target_sdn) {
        mtn_lock();

        /* Get the mapping tree node that is the best match for the target dn. */
        target_node = slapi_get_mapping_tree_node_by_dn(target_sdn);
        if (target_node == NULL) {
            target_node = mapping_tree_root;
        }

        /* See if we need to return a referral. */
        if ((target_node->mtn_state == MTN_REFERRAL) ||
            (target_node->mtn_state == MTN_REFERRAL_ON_UPDATE)) {
            *referral = (target_node->mtn_referral_entry ?
                         slapi_entry_dup(target_node->mtn_referral_entry) :
                         NULL);
            if (*referral) {
                ret = 1;
            }
        }

        mtn_unlock();
    }

  done:
    return ret;
}
/* 
 * Description:
 * The reason we have a mapping tree.  This function selects a backend or
 * referral to handle a given request.  Uses the target of the operation to
 * find a mapping tree node, then based on the operation type, bind dn, state
 * of the node, etc. it selects a backend or referral.
 * 
 * In this initial implementation of the mapping tree, each node can only have
 * one backend and one referral.  Later we should change this so each node has
 * a list of backends and a list of referrals.  Then we should add a modifier
 * to the state of the node.  For example, MTN_MODIFIER_ROUND_ROBIN could be a
 * modifer on the way a backend or referral is returned from the lists.
 *
 * Arguments:
 * pb is the pblock being used to service the operation.
 * be is an output param that will be set to the selected backend.
 * referral is an output param that will be set to the selected referral.
 * errorbuf is a pointer to a buffer that an error string will be written to
 *    if there is an error.  The caller is responsible for passing in a big 
 *    enough chunk of memory.  BUFSIZ should be fine.  If errorbuf is NULL, 
 *    no error string is written to it.  The string returned in errorbuf
 *    would be a good candidate for sending back to the client to describe the
 *    error.
 *
 * Returns:
 * LDAP_SUCCESS on success, other LDAP result codes if there is a problem.
 */
int slapi_mapping_tree_select(Slapi_PBlock *pb, Slapi_Backend **be, Slapi_Entry **referral, char *errorbuf)
{
    Slapi_DN *target_sdn = NULL;
    mapping_tree_node *target_node;
    Slapi_Operation *op;
    int index;
    int ret;
    int scope=LDAP_SCOPE_BASE;
    int op_type;
    

    if(mapping_tree_freed){
        /* shutdown detected */ 
        return LDAP_OPERATIONS_ERROR;
    }


    if (errorbuf) {
        errorbuf[0] = '\0';
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);
    slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);

    /* Get the target for this op */
    target_sdn = operation_get_target_spec (op);

    if(!mapping_tree_inited) {
        mapping_tree_init();
    } 

    be[0] = NULL;
    referral[0] = NULL;

    mtn_lock();

    /* Get the mapping tree node that is the best match for the target dn. */
    target_node = slapi_get_mapping_tree_node_by_dn(target_sdn);
    if (target_node == NULL)
        target_node = mapping_tree_root;

    /* The processing of the base scope root DSE search and all other LDAP operations on "" 
     *  will be transferred to the internal DSE backend 
     */
    if( sdn_is_nulldn(target_sdn) &&
        (((op_type == SLAPI_OPERATION_SEARCH) && (scope == LDAP_SCOPE_BASE)) ||
         (op_type != SLAPI_OPERATION_SEARCH)) ) {

        mtn_unlock();
        *be = slapi_be_select_by_instance_name(DSE_BACKEND);
        if(*be != NULL && !be_isdeleted(*be))
        {
            ret  = LDAP_SUCCESS;
            slapi_be_Rlock(*be);    /* also done inside mtn_get_be() below */
        } else {
            ret = LDAP_OPERATIONS_ERROR;
        }
        return ret;
    }

    /* index == -1 is used to specify that we want only one backend not a list
     * used for BASE search, ADD, DELETE, MODIFY
     */
    index = -1;
    ret = mtn_get_be(target_node, pb, be, &index, referral, errorbuf);
    slapi_pblock_set(pb, SLAPI_BACKEND_COUNT, &index);

    mtn_unlock();

    /* if a backend was returned, make sure that all non-search operations
     * fail if the backend is read-only, 
     * or if the whole server is readonly AND backend is public (!private)
     */
    if ((ret == LDAP_SUCCESS) && *be && !be_isdeleted(*be) &&
        ((*be)->be_readonly ||
        (slapi_config_get_readonly() && !slapi_be_private(*be)))) {
        unsigned long op_type = operation_get_type(op);

        if ((op_type != SLAPI_OPERATION_SEARCH) &&
            (op_type != SLAPI_OPERATION_COMPARE) &&
            (op_type != SLAPI_OPERATION_BIND) && 
            (op_type != SLAPI_OPERATION_UNBIND)) {
            ret = LDAP_UNWILLING_TO_PERFORM;
            PL_strncpyz(errorbuf, slapi_config_get_readonly() ? 
                    "Server is read-only" :
                    "database is read-only", BUFSIZ);
            slapi_be_Unlock(*be);
            *be = NULL;
        }
    }

    return ret;
}

int slapi_mapping_tree_select_all(Slapi_PBlock *pb, Slapi_Backend **be_list,
        Slapi_Entry **referral_list, char *errorbuf)
{
    Slapi_DN *target_sdn = NULL;
    mapping_tree_node *node_list;
    mapping_tree_node *node;
    Slapi_Operation *op;
    int index;
    int ret;
    int ret_code = LDAP_SUCCESS;
    int be_index = 0 ;
    int referral_index = 0 ;
    Slapi_Backend * be;
    Slapi_Entry * referral;
    int scope = LDAP_SCOPE_BASE;
    Slapi_DN *sdn = NULL;
    int flag_partial_result = 0;
    int op_type;
    
    if(mapping_tree_freed){
        return LDAP_OPERATIONS_ERROR;
    }

    if (errorbuf) {
        errorbuf[0] = '\0';
    }

    /* get the operational parameters */
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);
    if (NULL == sdn) {
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "Error: Null target DN");
        return LDAP_OPERATIONS_ERROR;
    }
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    target_sdn = operation_get_target_spec (op);
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);
    slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);

    if(!mapping_tree_inited){
        mapping_tree_init();
    } 

    mtn_lock();

    be_list[0] = NULL;
    referral_list[0] = NULL;
        
    /* Get the mapping tree node that is the best match for the target dn. */
    node_list = slapi_get_mapping_tree_node_by_dn(target_sdn);
    if (node_list == NULL)
        node_list = mapping_tree_root;

    if( sdn_is_nulldn(target_sdn) &&  ( op_type == SLAPI_OPERATION_SEARCH)
            && (scope == LDAP_SCOPE_BASE) )  { 
        mtn_unlock();
        be = slapi_be_select_by_instance_name(DSE_BACKEND);
        if(be != NULL && !be_isdeleted(be))
        {
            be_list[0]=be;
            be_list[1] = NULL;
            ret_code  = LDAP_SUCCESS;
            slapi_be_Rlock(be); /* also done inside mtn_get_be() below */
        } else {
            ret_code = LDAP_OPERATIONS_ERROR;
        }
        return ret_code;
    }

    ret = slapi_mtn_get_first_be(node_list, &node, pb, &be, &index, &referral, errorbuf, scope);

    while ((node) && (be_index <= BE_LIST_SIZE))
    {
        if (ret != LDAP_SUCCESS)
        {
            /* flag we have problems at least on part of the tree */
            flag_partial_result = 1;
        }
        else if ( ( ((!slapi_sdn_issuffix(sdn, slapi_mtn_get_dn(node))
            && !slapi_sdn_issuffix(slapi_mtn_get_dn(node), sdn))) 
            || ((node_list == mapping_tree_root) && node->mtn_private
            && (scope != LDAP_SCOPE_BASE)) )
                    && (!be || strncmp(be->be_name, "default", 8)))
        {
            if (be && !be_isdeleted(be))
            {
                /* wrong backend or referall, ignore it */
                slapi_log_error(SLAPI_LOG_ARGS, NULL,
                     "mapping tree release backend : %s\n",
                     slapi_be_get_name(be));
                slapi_be_Unlock(be);
            }
        }
        else
        {
            if (be && !be_isdeleted(be))
            {
                if (be_index == BE_LIST_SIZE) { /* error - too many backends */
                    ret_code = LDAP_ADMINLIMIT_EXCEEDED;
                    PR_snprintf(errorbuf, BUFSIZ-1,
                                "Error: too many backends match search request - cannot proceed");
                    slapi_log_error(SLAPI_LOG_FATAL, NULL, "%s\n", errorbuf);
                    break;
                } else {
                    be_list[be_index++]=be;
                }
            }

            if (referral)
            {
                referral_list[referral_index++] = referral;

                /* if we hit a referral at the base of the search 
                 * we must return a REFERRAL error with only this referral
                 * all backend or referral below this node are ignored 
                 */
                if (slapi_sdn_issuffix(target_sdn, slapi_mtn_get_dn(node)))
                {
                    ret_code = LDAP_REFERRAL;
                    break;    /* get out of the while loop */
                }
            }
        }

        ret = slapi_mtn_get_next_be(node_list, &node, pb, &be, &index,
                     &referral, errorbuf, scope);
    }
    mtn_unlock();
    be_list[be_index] = NULL;
    referral_list[referral_index] = NULL;

    if (flag_partial_result)
    {
        /* if no node in active has been found -> return LDAP_OPERATIONS_ERROR
         * but if only part of the nodes are disabled
         * do not return an error to allow directory browser to work OK 
         * in the console
         * It would be better to return a meaningfull error
         * unfortunately LDAP_PARTIAL_RESULTS is not usable because
         * it is already used for V2 referrals
         * leave no error for now and fix this later
         */
        if ((be_index == 0) && (referral_index == 0))
            return LDAP_OPERATIONS_ERROR;
        else
            return ret_code;
    }
    else
        return ret_code;
}

void slapi_mapping_tree_free_all(Slapi_Backend **be_list, Slapi_Entry **referral_list)
{
    int index = 0;

    /* go through the list of all backends that was used for the operation
     * and unlock them 
     * go through the list of referrals and free them
     * free the two tables that were used to store the two lists 
     */
    if (be_list[index] != NULL)
    {
        Slapi_Backend * be;
    
        while ((be = be_list[index++]))
        {
            slapi_log_error(SLAPI_LOG_ARGS, NULL, "mapping tree release backend : %s\n", slapi_be_get_name(be));
            slapi_be_Unlock(be);
        }
    }

    index = 0;
    if (referral_list[index] != NULL)
    {
        Slapi_Entry * referral;
        while ((referral = referral_list[index++]))
        {
            slapi_entry_free(referral);
        }
    }
}


/* same as slapi_mapping_tree_select() but will also check that the supplied
 * newdn is in the same backend
 */
int slapi_mapping_tree_select_and_check(Slapi_PBlock *pb,char *newdn, Slapi_Backend **be, Slapi_Entry **referral, char *errorbuf)
{
    Slapi_DN *target_sdn = NULL;
    Slapi_DN dn_newdn;
    Slapi_Backend * new_be = NULL;
    Slapi_Backend * def_be = defbackend_get_backend();
    Slapi_Entry * new_referral = NULL;
    mapping_tree_node *target_node;
    int index;
    Slapi_Operation *op;
    int ret;
    int need_unlock = 0;
    
    if(mapping_tree_freed){
        return LDAP_OPERATIONS_ERROR;
     }

    slapi_sdn_init(&dn_newdn);
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    target_sdn = operation_get_target_spec (op);

    * referral = NULL;
    ret = slapi_mapping_tree_select(pb, be, referral, errorbuf); 
    if (ret)
        goto unlock_and_return;

    slapi_sdn_init_dn_byref(&dn_newdn,newdn);

    /* acquire lock now, after slapi_mapping_tree_select() which also locks,
       because we are accessing mt internals */
    mtn_lock();
    need_unlock = 1; /* we have now acquired the lock */
    target_node = slapi_get_mapping_tree_node_by_dn(&dn_newdn);
    if (target_node == NULL)
        target_node = mapping_tree_root;
    index = -1;
    ret = mtn_get_be(target_node, pb, &new_be, &index, &new_referral, errorbuf);
    if (ret)
        goto unlock_and_return;

    if (*be)
    {
        /* suffix is a part of mapping tree. We should not free it */
        const Slapi_DN *suffix = slapi_get_suffix_by_dn(target_sdn);
        if ((*be != def_be) && (NULL == suffix))
        {
            ret = LDAP_NO_SUCH_OBJECT;
            PR_snprintf(errorbuf, BUFSIZ,
                        "Target entry \"%s\" does not exist\n", 
                        slapi_sdn_get_dn(target_sdn));
            goto unlock_and_return;
        }
        if (suffix && (0 == slapi_sdn_compare(target_sdn, suffix)))
        {
            /* target_sdn is a suffix */
            const Slapi_DN *new_suffix = NULL;
            /* new_suffix is a part of mapping tree. We should not free it */
            new_suffix = slapi_get_suffix_by_dn(&dn_newdn);
            if (!slapi_be_exist((const Slapi_DN *)&dn_newdn))
            {
                /* new_be is an empty backend */
                ret = LDAP_NO_SUCH_OBJECT;
                PR_snprintf(errorbuf, BUFSIZ,
                           "Backend for suffix \"%s\" does not exist\n", newdn);
                goto unlock_and_return;
            }
            if (0 == slapi_sdn_compare(&dn_newdn, new_suffix))
            {
                ret = LDAP_ALREADY_EXISTS;
                PR_snprintf(errorbuf, BUFSIZ,
                            "Suffix \"%s\" already exists\n", newdn);
                goto unlock_and_return;
            }
            ret = LDAP_NAMING_VIOLATION;
            PR_snprintf(errorbuf, BUFSIZ, "Cannot rename suffix \"%s\"\n",
                        slapi_sdn_get_dn(target_sdn));
            goto unlock_and_return;
        }
        else
        {
            if ((*be != new_be) || mtn_sdn_has_child(target_sdn))
            {
                ret = LDAP_AFFECTS_MULTIPLE_DSAS;
                PR_snprintf(errorbuf, BUFSIZ,
                                "Cannot move entries across backends\n");
                goto unlock_and_return;
            }
        }
    }

unlock_and_return:
    /* if slapi_mapping_tree_select failed, we won't have the lock */
    if (need_unlock) {
        mtn_unlock();
    }

    slapi_sdn_done(&dn_newdn);

    if (new_be)
        slapi_be_Unlock(new_be);

    if (new_referral)
        slapi_entry_free(new_referral);

    if (ret != LDAP_SUCCESS)
    {
        if (be && *be && !be_isdeleted(*be))
        {
            slapi_be_Unlock(*be);
            *be = NULL;
        }
        if (*referral)
        {
            slapi_entry_free(*referral);
            *referral = NULL;
        }
    }

    return ret;
}

/*
 * allow to solve the distribution problem when several back-ends are defined 
 */
static int
mtn_get_be_distributed(Slapi_PBlock *pb, mapping_tree_node * target_node,
    Slapi_DN *target_sdn, int * flag_stop)
{
    int index;
    *flag_stop = 0;

    if (target_node->mtn_dstr_plg)
    {
        index = (*target_node->mtn_dstr_plg)(pb, target_sdn,
                 target_node->mtn_backend_names, target_node->mtn_be_count,
                 target_node->mtn_subtree, target_node->mtn_be_states);

        if (index == SLAPI_BE_ALL_BACKENDS)
        {
            /* special value to indicate all backends must be scanned
             * start with first one 
             */
            index = 0;
        }
        /* paranoid check, never trust another programmer */
        else if ((index >= target_node->mtn_be_count) || (index < 0))
        {
                LDAPDebug(LDAP_DEBUG_ANY,
                    "Warning: distribution plugin returned wrong backend"
                    " : %d for entry %s at node %s\n",
                     index, slapi_sdn_get_ndn(target_sdn),
                     slapi_sdn_get_ndn(target_node->mtn_subtree));
            index = 0;
        }
        else 
        {
            /* only one backend to scan 
             * set flag_stop to indicate we must stop the search here 
             */
            *flag_stop = 1;
        }
    }
    else 
    {
        /* there is several backends but no distribution function
         * return the first backend 
         */
        LDAPDebug(LDAP_DEBUG_ANY,
        "Warning: distribution plugin not configured at node : %s\n",
        slapi_sdn_get_ndn(target_node->mtn_subtree), 0, 0);
        index = 0;
    }

    return index;
}
/* 
 * this function is in charge of choosing the right backend for a given
 * mapping tree node
 * In case when several backends are used it is in charge of the spanning the 
 * request among all the backend or choosing the only backend to use depending
 * on the type and scope of the LDAP operation
 *
 * index == -1 is used to specify that we want only the one best backend
 * used for BASE search, ADD, DELETE, MODIFY
 * index >0 means we are doing a SUBTREE or ONELEVEL search and that the be in
 * that position must be returned
 */
static int mtn_get_be(mapping_tree_node *target_node, Slapi_PBlock *pb,
   Slapi_Backend **be, int * index, Slapi_Entry **referral, char *errorbuf)
{
    Slapi_DN *target_sdn;
    Slapi_Operation *op;
    int result = LDAP_SUCCESS;
    int override_referral = 0;
    unsigned long op_type;
    int flag_stop = 0;
    struct slapi_componentid *cid = NULL;

    if(mapping_tree_freed){
        /* shut down detected */
        return LDAP_OPERATIONS_ERROR; 
    }
    /* Get usefull stuff like the type of operation, target dn */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    op_type = operation_get_type(op);
    target_sdn = operation_get_target_spec (op);

    if (target_node->mtn_state == MTN_DISABLED) {
        if (errorbuf) {
            PR_snprintf(errorbuf, BUFSIZ,
                "Warning: Operation attempted on a disabled node : %s\n",
                slapi_sdn_get_dn(target_node->mtn_subtree));
        }
        result = LDAP_OPERATIONS_ERROR;
        return result;
    }

    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &cid);

    override_referral =
        ((cid != NULL) && (pw_get_componentID() != NULL) && (pw_get_componentID() == cid)) ||
        operation_is_flag_set(op, OP_FLAG_LEGACY_REPLICATION_DN) || /* 4.0 lgacy update */
        operation_is_flag_set(op, OP_FLAG_REPLICATED) || /* 5.0 replication update */
        operation_is_flag_set(op, OP_FLAG_TOMBSTONE_ENTRY) || /* 5.1 fix to enable tombstone delete on a R-O consumer */
        operation_is_flag_set(op, SLAPI_OP_FLAG_BYPASS_REFERRALS); /* 6.1 fix to allow internal updates from plugins on R-O consumer */
    if ((target_node->mtn_state == MTN_BACKEND) ||
    (target_node->mtn_state == MTN_CONTAINER ) ||
        ((target_node->mtn_state == MTN_REFERRAL_ON_UPDATE) && 
         ((SLAPI_OPERATION_SEARCH == op_type)||(SLAPI_OPERATION_BIND == op_type) || 
         (SLAPI_OPERATION_UNBIND == op_type) || (SLAPI_OPERATION_COMPARE == op_type))) ||
        override_referral) {
        if ((target_node == mapping_tree_root) ){
            /* If we got here, then we couldn't find a matching node 
             * for the target. We'll use the default backend.  Once
             * we fully support the NULL suffix, we should do something more
             * clever here.
             */
             *be = defbackend_get_backend(); 
    
        } else {
            if ((*index == -1) || (*index == 0)) {
                /* In this case, we are doing 
                 * a READ, ADD, MODIDY or DELETE on a single entry
                 * or we are starting a SEARCH 
                 * if there is several possible backend we want to apply
                 * the distribution plugin
                 */
                if (target_node->mtn_be_count <= 1) {
                    /* there is only one backend no choice possible */
                    *index = 0;
                } else {
                    *index = mtn_get_be_distributed(pb, target_node,
                         target_sdn, &flag_stop);
                }
            }

            if ((*index == -2) || (*index >= target_node->mtn_be_count)) {
        /* we have already returned all backends -> return NULL */
                *be = NULL;
                *referral = NULL;
            } else {
                /* return next backend, increment index */
                *be = target_node->mtn_be[*index];
                if(*be==NULL) {
                    if (NULL != target_node->mtn_be_states &&
                        target_node->mtn_be_states[*index] == SLAPI_BE_STATE_DELETE) {
                        /* This MTN is being deleted */
                        *be = defbackend_get_backend();
                    } else {
                        /* This MTN has not been linked to its backend
                         * instance yet. */
                        target_node->mtn_be[*index] =
                            slapi_be_select_by_instance_name(
                                target_node->mtn_backend_names[*index]);
                        *be = target_node->mtn_be[*index];
                        if(*be==NULL) {
                            LDAPDebug(LDAP_DEBUG_ANY,
             "Warning: Mapping tree node entry for %s point to "
             "an unknown backend : %s\n",
             slapi_sdn_get_dn(target_node->mtn_subtree),
             target_node->mtn_backend_names[*index], 0);
                            /* Well there's still not backend instance for
                             * this MTN, so let's have the default backend
                             * deal with this.
                             */
                            *be = defbackend_get_backend();
                        }
                    }
                }
                if ((target_node->mtn_be_states) &&
                    (target_node->mtn_be_states[*index] == SLAPI_BE_STATE_OFFLINE)) {
                    LDAPDebug(LDAP_DEBUG_TRACE,
                        "Warning: Operation attempted on backend in OFFLINE "
                        "state : %s\n",
                         target_node->mtn_backend_names[*index], 0, 0);
            result = LDAP_OPERATIONS_ERROR;
                    *be = defbackend_get_backend();
                }
                if (flag_stop)
                    *index = -2;
                else
                    (*index)++;
            }
        }        
        *referral = NULL;
    } else {
        /* otherwise we must return the referral
         * if ((target_node->mtn_state == MTN_REFERRAL) ||
         * (target_node->mtn_state == MTN_REFERRAL_ON_UPDATE)) */

        if (*index > 0) {
            /* we have already returned this referral
             * send back NULL to jump to next node 
             */
            *be = NULL;
            *referral = NULL;
            result = LDAP_SUCCESS;
        } else {
            /* first time we hit this referral -> return it
             * set the be variable to NULL to indicate we use a referral
             * and increment index to rememeber later that we already
             * returned this referral
             */
            *be = NULL;
            *referral = (target_node->mtn_referral_entry ?
                         slapi_entry_dup(target_node->mtn_referral_entry) :
                         NULL);
            (*index)++;
            if (NULL == *referral) {
                if (errorbuf) {
                    PR_snprintf(errorbuf, BUFSIZ,
                    "Mapping tree node for %s is set to return a referral,"
                        " but no referral is configured for it",
                        slapi_sdn_get_ndn(target_node->mtn_subtree));
                }
                result = LDAP_OPERATIONS_ERROR;
            } else {
                result = LDAP_SUCCESS;
            }
        }
    }

    if (result == LDAP_SUCCESS) {
        if (*be && !be_isdeleted(*be)) {
            slapi_log_error(SLAPI_LOG_ARGS, NULL,
                "mapping tree selected backend : %s\n",
                slapi_be_get_name(*be));
            slapi_be_Rlock(*be);
        } else if (*referral) {
            slapi_log_error(SLAPI_LOG_ARGS, NULL,
                "mapping tree selected referral at node : %s\n",
                slapi_sdn_get_dn(target_node->mtn_subtree));
        }
    }

    return result;
}

/* 
 * Description:
 * Finds the best match for the targetdn from the children of parent.  Uses 
 * slapi_sdn_issuffix and the number of rdns to pick the best node.
 * 
 * Arguments:
 * parent is a pointer to a mapping tree node.
 * targetdn is the dn we're trying to find the best match for.
 *
 * Returns:
 * A pointer to the child of parent that best matches the targetdn.  NULL
 * if there were no good matches.
 */
static mapping_tree_node *best_matching_child(mapping_tree_node *parent,
     const Slapi_DN *targetdn)
{
    mapping_tree_node *highest_match_node = NULL;
    mapping_tree_node *current;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }
    for (current = parent->mtn_children; current;
        current = current->mtn_brother) {
        if (slapi_sdn_issuffix(targetdn, current->mtn_subtree)) {
            if ( (highest_match_node == NULL) ||
                 ((slapi_sdn_get_ndn_len(current->mtn_subtree)) >
                 slapi_sdn_get_ndn_len(highest_match_node->mtn_subtree)) ) {
                highest_match_node = current;
            }
        }
    }

    return highest_match_node;
}


/* 
 * look for the exact mapping tree node corresponding to a given entry dn
 */
static mapping_tree_node *
mtn_get_mapping_tree_node_by_entry(mapping_tree_node* node, const Slapi_DN *dn)
{
    mapping_tree_node *found_node = NULL;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }

    if(NULL == dn){
        /* bad mapping tree entry operation */
        return NULL;
    }

    if  (slapi_sdn_compare(node->mtn_subtree, dn) == 0)
    {
        return node;
    }

    if (node->mtn_children)
    {
        found_node = mtn_get_mapping_tree_node_by_entry(node->mtn_children, dn);
        if (found_node)
            return found_node;
    }

    if (node->mtn_brother)
    {
        found_node = mtn_get_mapping_tree_node_by_entry(node->mtn_brother, dn);
    }
    return found_node;
}
/* 
 * Description:
 * Gets a mapping tree node that best matches the given dn.  If the root
 * node is returned and the target dn is not "", then no match was found. 
 * 
 * Arguments:
 * dn is the target of the search.
 *
 * Returns:
 * The best matching node for the dn 
 * if nothing match, NULL is returned
 */
mapping_tree_node *
slapi_get_mapping_tree_node_by_dn(const Slapi_DN *dn)
{
    mapping_tree_node *current_best_match = mapping_tree_root;
    mapping_tree_node *next_best_match = mapping_tree_root;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }
    /* Handle special case where the dn is "" and the mapping root
     * does not belong to the frontend-internal (DSE_BACKEND); 
     * it has been assigned to a different backend.
     * e.g: a container backend 
     */
    if ( sdn_is_nulldn(dn) && mapping_tree_root && mapping_tree_root->mtn_be[0] &&
         mapping_tree_root->mtn_be[0] != slapi_be_select_by_instance_name(DSE_BACKEND)) {
        return( mapping_tree_root );
    }

    /* Start at the root and walk down the tree to find the best match. */
    while (next_best_match) {
        current_best_match = next_best_match;
        next_best_match = best_matching_child(current_best_match, dn);
    }

    if (current_best_match == mapping_tree_root)
        return NULL;
    else
        return current_best_match;
}


static  mapping_tree_node *
get_mapping_tree_node_by_name(mapping_tree_node * node, char * be_name)
{
    int i;
    mapping_tree_node *found_node = NULL;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }
    /* now search the backend in this node */
    i = 0;
    while ( ( i < node->mtn_be_count) &&
            (node->mtn_backend_names) &&
            (node->mtn_backend_names[i]) &&
            (strcmp(node->mtn_backend_names[i],be_name)))
    {
        i++;
    }

    if  ((i < node->mtn_be_count) &&
            (node->mtn_backend_names != NULL) &&
            (node->mtn_backend_names[i] != NULL))
    {
        return node;
    }

    if (node->mtn_children)
    {
        found_node = get_mapping_tree_node_by_name(node->mtn_children, be_name);
        if (found_node)
            return found_node;
    }

    if (node->mtn_brother)
    {
        found_node = get_mapping_tree_node_by_name(node->mtn_brother, be_name);
    }
    return found_node;
}

/* 
 * Description: construct the dn of the configuration entry for the
 *              node originated at the root. The function just constructs
 *              the dn it does not verify that the entry actually exist.
 *              The format of the dn is
 *                  cn="<normalized root>",cn=mapping tree,cn=config
 *              
 * Arguments:   root - root of the node
 *
 * Returns:     dn of the configuration entry if successful and null otherwise.
 */
char* 
slapi_get_mapping_tree_node_configdn (const Slapi_DN *root)
{
    char *dn = NULL;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }
    if (root == NULL)
        return NULL;

    /* This function converts the old DN style to the new one. */
    dn = slapi_create_dn_string("cn=\"%s\",%s", 
                                slapi_sdn_get_udn(root), MAPPING_TREE_BASE_DN);
    if (NULL == dn) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "slapi_get_mapping_tree_node_configdn: "
                      "failed to crate mapping tree dn for %s\n", 
                      slapi_sdn_get_dn(root));
        return NULL;
    }

    return dn;
}

Slapi_DN *
slapi_get_mapping_tree_node_configsdn (const Slapi_DN *root)
{
    char *dn = NULL;
    Slapi_DN *sdn = NULL;

    if(mapping_tree_freed){
        /* shutdown detected */
        return NULL;
    }
    if (root == NULL)
        return NULL;

    /* This function converts the old DN style to the new one. */
    dn = slapi_create_dn_string("cn=\"%s\",%s", 
                                slapi_sdn_get_udn(root), MAPPING_TREE_BASE_DN);
    if (NULL == dn) {
        LDAPDebug1Arg(LDAP_DEBUG_ANY,
                      "slapi_get_mapping_tree_node_configsdn: "
                      "failed to crate mapping tree dn for %s\n", 
                      slapi_sdn_get_dn(root));
        return NULL;
    }

    sdn = slapi_sdn_new_normdn_passin(dn);

    return sdn;
}

/* 
 * Description: this function returns root of the subtree to which the node applies
 *              
 * Arguments:   node - mapping tree node
 *
 * Returns:     root of the subtree if function is successful and NULL otherwise.
 */

const Slapi_DN* slapi_get_mapping_tree_node_root (const mapping_tree_node *node)
{
    if (node)
        return node->mtn_subtree;
    else
        return NULL;
}

/* GB : there is a potential problems with this function 
 * when several backends are used
 */
PRBool slapi_mapping_tree_node_is_set (const mapping_tree_node *node, PRUint32 flag)
{
    if (flag & SLAPI_MTN_LOCAL)
        return PR_TRUE;

    if (flag & SLAPI_MTN_PRIVATE)
        return ((node->mtn_be_count>0) && node->mtn_be && node->mtn_be[0] && node->mtn_private);

    if (flag & SLAPI_MTN_READONLY)
        return ((node->mtn_be_count>0) &&  node->mtn_be && node->mtn_be[0] && node->mtn_be[0]->be_readonly);

    return PR_FALSE;
}

/* 
 * Description: this function returns root of the subtree to which the node applies
 *              
 * Arguments:   node
 *
 * Returns:     dn of the parent of mapping tree node configuration entry.
 */

const char* slapi_get_mapping_tree_config_root ()
{
    return MAPPING_TREE_BASE_DN;
}

/*
 * slapi_be_select() finds the backend that should be used to service dn.
 * If no backend with an appropriate suffix is configured, the default backend
 * is returned.  This function never returns NULL.
 */
Slapi_Backend *
slapi_be_select( const Slapi_DN *sdn ) /* JCM - The name of this should change??? */
{
    Slapi_Backend *be;
    mapping_tree_node *node= slapi_get_mapping_tree_node_by_dn(sdn);
    if((node!=NULL) && (node->mtn_be!=NULL))
        be= node->mtn_be[0];
    else
        be = NULL;

    if(be==NULL)
        be= defbackend_get_backend();

    return be;
}

/* Check if the dn targets an internal reserved backends */
int
slapi_on_internal_backends(const Slapi_DN *sdn)
{
        char *backend_names[] = {DSE_BACKEND, DSE_SCHEMA};
        int internal = 1;
        int numOfInternalBackends = 2;                           
        int count;
                                                                 
        Slapi_Backend *internal_be;
                                                                 
        Slapi_Backend *be = slapi_be_select(sdn);
    
        
        for (count=0; count < numOfInternalBackends ; ++count){ 
        /* the internal backends are always in the begining of the list 
         * so should not be very inefficient 
         */ 
            internal_be = slapi_be_select_by_instance_name(backend_names[count]);
            if(be == internal_be){
                return internal; 
            }
        }
    return 0;
}   

/* Some of the operations are not allowed from the plugins
 * but default to specialized use of those operations 
 * e.g rootDse search, ConfigRoot searches
 * cn=config, cn=schema etc
 */

int slapi_op_reserved(Slapi_PBlock *pb)
{   
    int scope=LDAP_SCOPE_BASE;
    int reservedOp=0;
    int op_type;
    Slapi_Operation *op = NULL;
    Slapi_DN *target_sdn=NULL;
        
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &op_type);
    /* Get the target for this op */
    target_sdn = operation_get_target_spec (op);

    if( op_type == SLAPI_OPERATION_SEARCH){
        slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
        if( sdn_is_nulldn(target_sdn) && (scope == LDAP_SCOPE_BASE) ){
                reservedOp = 1;
        } 
                
    }            
    
    if(slapi_on_internal_backends(target_sdn)){
        reservedOp = 1;
    }
                    
    return reservedOp;
}



/* 
 * Returns the name of the Backend that contains specified DN, 
 * if only one matches. Otherwise returns NULL
 * The name is pointing to the mapping tree structure 
 * and should not be altered.
 */
const char *
slapi_mtn_get_backend_name( const Slapi_DN *sdn)
{
    mapping_tree_node *node= slapi_get_mapping_tree_node_by_dn(sdn);
    if ((node != NULL) && 
        (node->mtn_be_count == 1) &&
        (node->mtn_backend_names != NULL))
        /* There's only one name, return it */
        return node->mtn_backend_names[0];
    else
        return NULL;
}

/* Check if the backend that contains specified DN exists */
int 
slapi_be_exist(const Slapi_DN *sdn) /* JCM - The name of this should change??? */
{
    Slapi_Backend *def_be = defbackend_get_backend();
    Slapi_Backend *be = slapi_be_select (sdn);

    return (be != def_be);
}

/* The two following functions can be used to 
 * parse the list of the root suffix of the DIT
 * Using 
 */
Slapi_DN *
slapi_get_first_suffix(void ** node, int show_private)
{
    mapping_tree_node *first_node;

    if ((NULL == node) || (NULL == mapping_tree_root)) {
        return NULL;
    }
    first_node = mapping_tree_root->mtn_children;
    *node = (void * ) first_node ;
    while (first_node && (first_node->mtn_private && (show_private == 0)))
            first_node = first_node->mtn_brother;
    return (first_node ? first_node->mtn_subtree : NULL);
}

Slapi_DN * 
slapi_get_next_suffix(void ** node, int show_private)
{
    mapping_tree_node *next_node;

    if ((NULL == node) || (NULL == mapping_tree_root)) {
        return NULL;
    }
    next_node = *node;
    if (next_node == NULL) {
        return NULL;
    }
    next_node = next_node->mtn_brother;
    while (next_node && (next_node->mtn_private && (show_private == 0)))
            next_node = next_node->mtn_brother;
    *node = next_node;
    return (next_node ? next_node->mtn_subtree : NULL);
}

/* get mapping tree node recursively */
Slapi_DN * 
slapi_get_next_suffix_ext(void ** node, int show_private)
{
    mapping_tree_node * next_node = NULL;

    if (NULL == node) {
        return NULL;
    }
    next_node = *node;
    if (next_node == NULL) {
        return NULL;
    }
    if (next_node->mtn_children) {
        next_node = next_node->mtn_children;
    } else if (next_node->mtn_brother) {
        next_node = next_node->mtn_brother;
    } else {
        next_node = next_node->mtn_parent;
        if (next_node) {
            next_node = next_node->mtn_brother;
        }
    }
    while (next_node && (next_node->mtn_private && (show_private == 0)))
            next_node = next_node->mtn_brother;

    if (next_node) {
        *node = next_node;
    }

    return (next_node ? next_node->mtn_subtree : NULL);
}

/* check if a suffix is a root of the DIT
 * return 1 if yes, 0 if no
 */
int slapi_is_root_suffix(Slapi_DN * dn)
{
    void * node;
    Slapi_DN * suffix = slapi_get_first_suffix (&node, 1);

    while (suffix)
    {
        if ( slapi_sdn_compare(dn, suffix) == 0 )
            return 1;
        suffix = slapi_get_next_suffix(&node, 1);
    }
    return 0 ;
}

/* Return value is a part mapping tree; Don't free it. */
const Slapi_DN *
slapi_get_suffix_by_dn(const Slapi_DN *dn)
{
    mapping_tree_node *node = slapi_get_mapping_tree_node_by_dn(dn);
    const Slapi_DN *suffix = NULL;
    if (node)
    {
        suffix = (const Slapi_DN *)slapi_mtn_get_dn(node);
    }
    return suffix;
}

/*
 * set referrals for the node
 * notes :
 *  - referral is consumed by this function
 *  - node must exist before calling this function
 *  - mapping tree node state is not changed by this function
 */
int
slapi_mtn_set_referral(const Slapi_DN *sdn, char ** referral)
{
    Slapi_PBlock pb;
    Slapi_Mods smods;
    int rc = LDAP_SUCCESS,i = 0, j = 0;
    Slapi_DN* node_sdn;
    char **values = NULL;
    int do_modify = 0;

    slapi_mods_init (&smods, 0);
    node_sdn = slapi_get_mapping_tree_node_configsdn(sdn);
    if(!node_sdn){
     /* shutdown has been detected */
        return LDAP_OPERATIONS_ERROR;
    }
    
    if ((referral == NULL) || (referral[0] == NULL))
    {
        /* NULL referral means we want to delete existing referral 
         */
        slapi_mods_add(&smods, LDAP_MOD_DELETE, "nsslapd-referral", 0, NULL);
        do_modify = 1;
    }
    else
    {
        int changes = 1;
        int referralCount = 0;

        for(; referral[referralCount]; referralCount++);
        if ( (values = slapi_mtn_get_referral(sdn)) != NULL )
        {
            /* Check if there are differences between current values and values to be set */
            
            for (i=0; values[i]; i++);
            if (i == referralCount) {
                changes = 0;
                for (i=0;values[i];i++){
                    int found = 0;
                    for (j=0;referral[j];j++){
                        if (strcmp(values[i], referral[j]) == 0){
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        changes = 1;
                        break;
                    }
                }
            }

            i=0;
            while(values[i])
                slapi_ch_free((void**)&values[i++]);
            slapi_ch_free((void**)&values);

        }
        if (changes){
            Slapi_Value *val;
            Slapi_Value ** svals = NULL;

            do_modify = 1;
            for (j =0; referral[j];j++) {
                val = slapi_value_new_string(referral[j]);
                valuearray_add_value(&svals, val);
                slapi_value_free(&val);
            }
            slapi_mods_add_mod_values(&smods, LDAP_MOD_REPLACE, "nsslapd-referral", svals);
            valuearray_free(&svals);
        }
    }
    
    if ( do_modify )
    {
        pblock_init (&pb);
        slapi_modify_internal_set_pb_ext (&pb, node_sdn,
                                slapi_mods_get_ldapmods_byref(&smods), NULL,
                                NULL, (void *) plugin_get_default_component_id(), 0);
        slapi_modify_internal_pb (&pb);

        slapi_pblock_get (&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        pblock_done(&pb);
    }

    slapi_mods_done(&smods);
    slapi_sdn_free(&node_sdn);

    return rc;
} 

/*
 * Change the state of a mapping tree node entry
 * notes :
 *   - sdn argument is the dn of the subtree of the DIT managed by this node 
 *     not the dn of the mapping tree entry
 *   - mapping tree node must exist before calling this function
 */
int
slapi_mtn_set_state(const Slapi_DN *sdn, char *state)
{
    Slapi_PBlock pb;
    Slapi_Mods smods;
    int rc = LDAP_SUCCESS;
    Slapi_DN *node_sdn;
    char * value;

    if (NULL == state) {
        return LDAP_OPERATIONS_ERROR;
    }

    node_sdn = slapi_get_mapping_tree_node_configsdn(sdn);
    if(!node_sdn){
     /* shutdown has been detected */
        return LDAP_OPERATIONS_ERROR;
    }

    if ( (value = slapi_mtn_get_state(sdn)) != NULL )
    {
        if ( strcasecmp(value, state) == 0 )
        {
            /* Same state, don't change anything */
            goto bail;
        }
    }
    
    /* Otherwise, means that the state has changed, modify it */
    slapi_mods_init (&smods, 1);
    slapi_mods_add(&smods, LDAP_MOD_REPLACE, "nsslapd-state", strlen(state), state);
    pblock_init (&pb);
    slapi_modify_internal_set_pb_ext (&pb, node_sdn,
                            slapi_mods_get_ldapmods_byref(&smods), NULL,
                            NULL, (void *) plugin_get_default_component_id(), 0);
    slapi_modify_internal_pb (&pb);

    slapi_pblock_get (&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);

    slapi_mods_done(&smods);
    pblock_done(&pb);
bail:
    slapi_ch_free_string(&value);
    slapi_sdn_free(&node_sdn);
    return rc;
}

/*
  returns a copy of the attr - the caller must slapi_attr_free it
*/
Slapi_Attr *
mtn_get_attr(char* node_dn, char * type)
{
    Slapi_PBlock pb;
    int res = 0;
    Slapi_Entry **entries = NULL;
    Slapi_Attr *attr = NULL;
    Slapi_Attr *ret_attr = NULL;
    char **attrs = NULL;

    attrs = (char **)slapi_ch_calloc(2, sizeof(char *));
    attrs[0] = slapi_ch_strdup(type);
    pblock_init(&pb);
    slapi_search_internal_set_pb(&pb, node_dn, LDAP_SCOPE_BASE,
        "objectclass=nsMappingTree", attrs, 0, NULL, NULL,
         (void *) plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(&pb);
    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

    if (res != LDAP_SUCCESS) {
        goto done;
    }

    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (NULL == entries || NULL == entries[0]) {
        goto done;
    }

    /* always at most one entry entries[0] */
    res = slapi_entry_attr_find(entries[0], type, &attr);
    if (res == 0)
        /* we need to make a copy here so we can free the search results */
        ret_attr = slapi_attr_dup(attr);

    slapi_free_search_results_internal(&pb);

done:
    slapi_ch_free((void **)&attrs[0]);
    slapi_ch_free((void **)&attrs);
    pblock_done(&pb);
    return ret_attr;
}

/*
 * Get the referral associated to the mapping tree node entry
 * notes :
 *   - sdn argument is the dn of the subtree of the DIT managed by this node 
 *     not the dn of the mapping tree entry
 *   - return NULL if no referral 
 *   - caller is reponsible for freeing the returned referrals
 */
char **
slapi_mtn_get_referral(const Slapi_DN *sdn)
{
    int i, hint, nb;
    char * node_dn;
    Slapi_Attr *attr;
    char ** referral = NULL;
    Slapi_Value *val = NULL;

    node_dn = slapi_get_mapping_tree_node_configdn(sdn);
    if(!node_dn){
     /* shutdown has been detected */
        return NULL;
    }

    attr = mtn_get_attr(node_dn, "nsslapd-referral");

    if (attr)
    {
        /* if there are some referrals set in the entry build a list 
         * to be returned to the caller 
         */
        slapi_attr_get_numvalues(attr, &nb);
        referral = (char**) slapi_ch_malloc(sizeof(char*) * (nb+1));
        hint = slapi_attr_first_value(attr, &val);
        i = 0;

        while (val)
        {
            referral[i++] = slapi_ch_strdup(slapi_value_get_string(val));
            hint = slapi_attr_next_value(attr, hint, &val);
        }
        referral[i] = NULL;
        slapi_attr_free(&attr);
    }

    slapi_ch_free_string(&node_dn);
    return referral;
}

/*
 * Get the state of a mapping tree node entry
 * notes :
 *   - sdn argument is the dn of the subtree of the DIT managed by this node 
 *     not the dn of the mapping tree entry
 *     - the state is return in a newly allocated string that must be freed by
 *     the caller
 */
char *
slapi_mtn_get_state(const Slapi_DN *sdn)
{
    char * node_dn;
    Slapi_Attr *attr;
    char * state = NULL;
    Slapi_Value *val = NULL;

    node_dn = slapi_get_mapping_tree_node_configdn(sdn);
    if(!node_dn){
     /* shutdown has been detected */
        return NULL;
    }

    attr = mtn_get_attr(node_dn, "nsslapd-state");

    if (attr)
    {
        /* entry state was found */
        slapi_attr_first_value(attr, &val);
        state = slapi_ch_strdup(slapi_value_get_string(val));
        slapi_attr_free(&attr);
    }

    slapi_ch_free_string(&node_dn);
    return state;
}

static void
mtn_internal_be_set_state(Slapi_Backend *be, int state)
{
    mapping_tree_node * node;
    char * be_name;
    int i;
    int change_callback = 0;
    int old_state;

    mtn_wlock();
    be_name = slapi_ch_strdup(slapi_be_get_name(be));
    node = get_mapping_tree_node_by_name(mapping_tree_root, be_name);
    if (node == NULL)
    {
        LDAPDebug(LDAP_DEBUG_TRACE,
            "Warning: backend %s is not declared in mapping tree\n",
             be_name, 0 ,0);
        goto done;
    }
    
        
    /* now search the backend in this node */
    i = 0;
    while ( (i < node->mtn_be_count) &&
            (node->mtn_backend_names) &&
            (node->mtn_backend_names[i]) &&
            (strcmp(node->mtn_backend_names[i],be_name)))
    {
        i++;
    }

    if  ( (i >= node->mtn_be_count) || (node->mtn_backend_names == NULL) ||
          (node->mtn_backend_names[i] == NULL) )
    {
        /* backend is not declared in the mapping tree node
         * print out a warning
         */
        LDAPDebug(LDAP_DEBUG_TRACE,
            "Warning: backend %s is not declared in mapping node entry\n",
             be_name, 0 ,0);
        goto done;
    }

    change_callback = 1;
    old_state = node->mtn_be_states[i];

    /* OK we found the backend at last, now do the real job: set the state */
    switch (state)
    {
        case SLAPI_BE_STATE_OFFLINE:
            node->mtn_be[i] = be;
            node->mtn_be_states[i] = SLAPI_BE_STATE_OFFLINE;
        break;

        case SLAPI_BE_STATE_ON:
            node->mtn_be[i] = be;
            node->mtn_be_states[i] = SLAPI_BE_STATE_ON;
        break;

        case SLAPI_BE_STATE_DELETE:
            node->mtn_be[i] = NULL;
            node->mtn_be_states[i] = SLAPI_BE_STATE_DELETE;
        break;
    }    

done:
    mtn_unlock();
    if (change_callback)
        mtn_be_state_change(be_name, old_state, state);
    slapi_ch_free( (void **) &be_name);
}

/*
 * This procedure must be called by previously stopped backends
 * to signal that they have started and are ready to process requests
 * The backend must be fully ready to handle requests before calling this 
 * procedure
 * At startup tiem it is not mandatory for the backends to 
 * call this procedure: backends are assumed on by default
 */
void
slapi_mtn_be_started(Slapi_Backend *be)
{
    /* Find the node where this backend stay
     * then update the backend structure
     * In the long term, the backend should have only one suffix and
     * stay in only one node as for now, check all suffixes 
     * Rq : since mapping tree is initiatized very soon in the server
     * startup, we can be sure at that time that the mapping
     * tree is initialized
     */

    mtn_internal_be_set_state(be, SLAPI_BE_STATE_ON);
}

/* these procedure can be called when backends need to be put in maintenance mode
 * after call to slapi_mtn_be_disable, the backend will still be known
 * by a server but the mapping tree won't route requests to it anymore 
 * The slapi_mtn_be_enable function enable to route requests to the backend
 * again 
 * the slapi_mtn_be_disable function only returns when there is no more 
 * request in progress in the backend
 */
void
slapi_mtn_be_disable(Slapi_Backend *be)
{
    mtn_internal_be_set_state(be, SLAPI_BE_STATE_OFFLINE);

    /* the two following lines can seem weird, but they allow to check that no 
     * LDAP operation is in progress on the backend
     */
    slapi_be_Wlock(be);
    slapi_be_Unlock(be);
}

void
slapi_mtn_be_enable(Slapi_Backend *be)
{
    mtn_internal_be_set_state(be, SLAPI_BE_STATE_ON);
}

/*
 * This procedure must be called by backends before stopping
 * if some operations are in progress when this procedure
 * is called, this procedure will block until completion
 * of these operations
 * The backend must wait return from this procedure before stopping operation
 * Backends must serve operation until the return from this procedure.
 * Once this procedure return they will not be issued request anymore
 * and they have been removed from the server list of backends
 * It is also the bakend responsability to free the Slapi_Backend structures
 * that was given by slapi_be_new at startup time.
 * Should the backend start again, it would need to issue slapi_be_new again
 */
void
slapi_mtn_be_stopping(Slapi_Backend *be)
{
    mtn_internal_be_set_state(be, SLAPI_BE_STATE_DELETE);

    /* the two following lines can seem weird, but they allow to check that no 
     * LDAP operation is in progress on the backend
     */
    slapi_be_Wlock(be);
    slapi_be_Unlock(be);

    slapi_be_stopping(be);
}

/*
 * Switch a backend into read-only mode, or back to read-write mode.
 * To switch to read-only mode, we need to wait for all pending operations
 * to finish.
 */
void
slapi_mtn_be_set_readonly(Slapi_Backend *be, int readonly)
{
    if (readonly) {
        slapi_be_Wlock(be);
        slapi_be_set_readonly(be, 1);
        slapi_be_Unlock(be);
    } else {
        slapi_be_set_readonly(be, 0);
    }
}


#ifdef DEBUG
static int lock_count = 0;
#endif

void mtn_wlock()
{
    slapi_rwlock_wrlock(myLock);
#ifdef DEBUG
    lock_count--;
    LDAPDebug(LDAP_DEBUG_ARGS, "mtn_wlock : lock count : %d\n", lock_count, 0, 0);
#endif
}

void mtn_lock()
{
    slapi_rwlock_rdlock(myLock);
#ifdef DEBUG
    lock_count++;
    LDAPDebug(LDAP_DEBUG_ARGS, "mtn_lock : lock count : %d\n", lock_count, 0, 0);
#endif
}

void mtn_unlock()
{
    
#ifdef DEBUG
    if (lock_count > 0)
        lock_count--;
    else if (lock_count < 0)
        lock_count++;
    else
        lock_count = (int) 11111111;   /* this happening means problems */
    LDAPDebug(LDAP_DEBUG_ARGS, "mtn_unlock : lock count : %d\n", lock_count, 0, 0);
#endif
    slapi_rwlock_unlock(myLock);
}

#ifdef TEST_FOR_REGISTER_CHANGE
void my_test_fnct1(void *handle, char *be_name, int old_state, int new_state)
{
        slapi_log_error(SLAPI_LOG_ARGS, NULL,
        "my_test_fnct1 : handle %d, be %s, old state %d, new state %d\n",
        handle,be_name, old_state, new_state);

        if (old_state == 2)
            slapi_unregister_backend_state_change(handle);
}

void my_test_fnct2(void *handle, char *be_name, int old_state, int new_state)
{
        slapi_log_error(SLAPI_LOG_ARGS, NULL,
        "my_test_fnct2 : handle %d, be %s, old state %d, new state %d\n",
        handle, be_name, old_state, new_state);
}

void test_register()
{
    slapi_register_backend_state_change((void *) 1234, my_test_fnct1);
    slapi_register_backend_state_change((void *) 4321, my_test_fnct2);
}
#endif

#ifdef DEBUG
#ifdef USE_DUMP_MAPPING_TREE
static void dump_mapping_tree(mapping_tree_node *parent, int depth)
{
    mapping_tree_node *current = NULL;
    static char dump_indent[256];
    int i;

    if (depth == 0)
    {
        LDAPDebug(LDAP_DEBUG_ANY, "dump_mapping_tree\n", 0, 0, 0);
    }
    dump_indent[0] = '\0';
    for (i = 0; i < depth; i++)
        PL_strcatn(dump_indent, sizeof(dump_indent), "  ");
    for (current = parent->mtn_children; current;
         current = current->mtn_brother)
    {
        if (strlen(current->mtn_subtree->dn) == 0)
        {
            LDAPDebug(LDAP_DEBUG_ANY, "MT_DUMP: %s%s (0x%x)\n",
                dump_indent, "none", current);
        }
        else
        {
            LDAPDebug(LDAP_DEBUG_ANY, "MT_DUMP: %s%s (0x%x)\n",
                dump_indent, current->mtn_subtree->dn, current);
        }
        dump_mapping_tree(current, depth+1);
    }
    return;
}
#endif /* USE_DUMP_MAPPING_TREE */
#endif /* DEBUG */

/* helper function to set/remove the config param in cn=config */
static int
_mtn_update_config_param(int op, char *type, char *strvalue)
{
    Slapi_PBlock confpb;
    Slapi_DN sdn;
    Slapi_Mods smods;
    LDAPMod **mods;
    int rc = LDAP_PARAM_ERROR;

    pblock_init (&confpb);
    slapi_mods_init (&smods, 0);
    switch (op) {
    case LDAP_MOD_DELETE:
        slapi_mods_add(&smods, op, type, 0, NULL);
        break;
    case LDAP_MOD_ADD:
    case LDAP_MOD_REPLACE:
        slapi_mods_add_string(&smods, op, type, strvalue);
        break;
    default:
        return rc;
    }
    slapi_sdn_init_ndn_byref(&sdn, SLAPD_CONFIG_DN);
    slapi_modify_internal_set_pb_ext(&confpb, &sdn,
                                  slapi_mods_get_ldapmods_byref(&smods),
                                  NULL, NULL,
                                  (void *)plugin_get_default_component_id(), 0);
    slapi_modify_internal_pb(&confpb);
    slapi_pblock_get (&confpb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_sdn_done(&sdn);
    /* need to free passed out mods 
     * since the internal modify could realloced mods. */
    slapi_pblock_get(&confpb, SLAPI_MODIFY_MODS, &mods);
    ldap_mods_free (mods, 1 /* Free the Array and the Elements */);
    pblock_done(&confpb);

    return rc;
}
