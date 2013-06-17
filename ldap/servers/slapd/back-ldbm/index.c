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


/* index.c - routines for dealing with attribute indexes */

#include "back-ldbm.h"
#if ( defined ( OSF1 ))
#undef BUFSIZ
#define BUFSIZ	1024
#endif

static const char *errmsg = "database index operation failed";

static int   is_indexed (const char* indextype, int indexmask, char** index_rules);
static Slapi_Value **
valuearray_minus_valuearray(
    const Slapi_Attr *sattr, 
    Slapi_Value **a, 
    Slapi_Value **b
);

const char* indextype_PRESENCE = "pres";
const char* indextype_EQUALITY = "eq";
const char* indextype_APPROX   = "approx";
const char* indextype_SUB      = "sub";

static char prefix_PRESENCE[2] = {PRES_PREFIX, 0};
static char prefix_EQUALITY[2] = {EQ_PREFIX, 0};
static char prefix_APPROX  [2] = {APPROX_PREFIX, 0};
static char prefix_SUB     [2] = {SUB_PREFIX, 0};

/* Yes, prefix_PRESENCE and prefix_SUB are identical.
 * It works because SUB is always followed by a key value,
 * but PRESENCE never is.  Too slick by half.
 */


/* Structures for index key buffering magic used by import code */
struct _index_buffer_bin {
	DBT key;
	IDList *value;
};
typedef struct _index_buffer_bin index_buffer_bin;

struct _index_buffer_handle {
	int flags;
	size_t buffer_size;
	size_t idl_size;
	size_t max_key_length;
	index_buffer_bin *bins;
	unsigned char high_key_byte_range;
	unsigned char low_key_byte_range;
	unsigned char special_byte_a;
	unsigned char special_byte_b;
	size_t byte_range;
	/* Statistics */
	int inserts;
	int keys;
};
typedef struct _index_buffer_handle index_buffer_handle;
#define INDEX_BUFFER_FLAG_SERIALIZE 1
#define INDEX_BUFFER_FLAG_STATS 2

/* Index buffering functions */

static int
index_buffer_init_internal(size_t idl_size, 
				  unsigned char high_key_byte_range, unsigned char low_key_byte_range,
				  size_t max_key_length,unsigned char special_byte_a, unsigned char special_byte_b, 
				  int flags,void **h)
{
	size_t bin_count = 0;
	/* Allocate the handle */
	index_buffer_bin *bins = NULL;
	size_t i = 0;
	size_t byte_range = 0;
	int rc = 0;

	index_buffer_handle *handle = (index_buffer_handle *) slapi_ch_calloc(1,sizeof(index_buffer_handle));
	if (NULL == handle) {
		rc = -1;
		goto error;
	}
	handle->idl_size = idl_size;
	handle->flags = flags;
	handle->high_key_byte_range = high_key_byte_range;
	handle->low_key_byte_range = low_key_byte_range;
	handle->special_byte_a = special_byte_a;
	handle->special_byte_b = special_byte_b;
	handle->max_key_length = max_key_length;
	byte_range = (high_key_byte_range - low_key_byte_range) + 3 + 10;
	handle->byte_range = byte_range;
	/* Allocate the bins */
	bin_count = 1;
	for (i = 0 ; i < max_key_length - 2; i++) {
		bin_count *= byte_range;
	}
	handle->buffer_size = bin_count;
	bins = (index_buffer_bin *)slapi_ch_calloc(bin_count, sizeof(index_buffer_bin));
	if (NULL == bins) {
		rc = -1;
		goto error;
	}
	handle->bins = bins;
	*h = (void*) handle;
	goto done;

error:
	slapi_ch_free((void**)&handle);

done:
	return rc;
}

int index_buffer_init(size_t size,int flags,void **h)
{
	return index_buffer_init_internal(size,'z','a',5,'^','$',flags,h);
}

static int 
index_put_idl(index_buffer_bin *bin,backend *be, DB_TXN *txn,struct attrinfo *a)
{
	int ret = 0;
	DB *db = NULL;
	int need_to_freed_new_idl = 0;
	IDList *old_idl = NULL;
	IDList *new_idl = NULL;

	if ( (ret = dblayer_get_index_file( be, a, &db, DBOPEN_CREATE )) != 0 ) {
		return ret;
	}
	if (bin->key.data && bin->value) {
		/* Need to read the IDL at the key, if present, and form the union with what we have */
		ret = NEW_IDL_NOOP;	/* this flag is for new idl only;
							 * but this func is called only from index_buffer,
							 * which is enabled only for old idl.
							 */
		old_idl = idl_fetch(be,db,&bin->key,txn,a,&ret);
		if ( (0 != ret) && (DB_NOTFOUND != ret)) {
			goto error;
		}
		if ( (old_idl != NULL) && !ALLIDS(old_idl)) {
			/* We need to merge in our block with what was there */
			new_idl = idl_union(be,old_idl,bin->value);
			need_to_freed_new_idl = 1;
		} else {
			/* Nothing there previously, we store just what we have */
			new_idl = bin->value;
		}
		/* Then write back the result, but only if the existing idl wasn't ALLIDS */
		if (!old_idl || (old_idl && !ALLIDS(old_idl))) {
			ret = idl_store_block(be,db,&bin->key,new_idl,txn,a);
		}
		if (0 != ret) {
			goto error;
		}
		slapi_ch_free( &(bin->key.data) );
		idl_free(bin->value);
		/* If we're already at allids, store an allids block to prevent needless accumulation of blocks */
		if (old_idl && ALLIDS(old_idl)) {
			bin->value = idl_allids(be);
		} else {
			bin->value = NULL;
		}
	}
error:
	if (old_idl) {
		idl_free(old_idl);
	}
	if (new_idl && need_to_freed_new_idl) {
		idl_free(new_idl);
	}
	dblayer_release_index_file( be, a, db );
	return ret;
}

/* The caller MUST check for DB_RUNRECOVERY being returned */

int 
index_buffer_flush(void *h,backend *be, DB_TXN *txn,struct attrinfo *a)
{
	index_buffer_handle *handle = (index_buffer_handle *) h;
	index_buffer_bin *bin = NULL;
	int ret = 0;
	size_t i = 0;
	DB *db = NULL;

	PR_ASSERT(h);

	/* Note to the wary: here we do NOT create the index file up front */
	/* This is becuase there may be no buffers to flush, and the goal is to 
	 * never create the index file (merging gets confused by this, among other things */

	/* Walk along the bins, writing them to the database */
	for (i = 0; i < handle->buffer_size; i++) {
		bin = &(handle->bins[i]);
		if (bin->key.data && bin->value) {
		if (NULL == db) {
			if ( (ret = dblayer_get_index_file( be, a, &db, DBOPEN_CREATE )) != 0 ) {
				return ret;
			}
		}
			ret = index_put_idl(bin,be,txn,a);
			if (0 != ret) {
				goto error;
			}
		}
	}
error:
	if (NULL != db) {
		dblayer_release_index_file( be, a, db );
	}
	return ret;
}

int
index_buffer_terminate(void *h)
{
	index_buffer_handle *handle = (index_buffer_handle *) h;
	index_buffer_bin *bin = NULL;
	size_t i = 0;

	PR_ASSERT(h);
	/* Free all the buffers */
	/* First walk down the bins, freeing the IDLs and the bins they're in */
	for (i = 0; i < handle->buffer_size; i++) {
		bin = &(handle->bins[i]);
		if (bin->value) {
			idl_free(bin->value);
			bin->value = NULL;
		}
		slapi_ch_free(&(bin->key.data));
	}
	slapi_ch_free((void **)&(handle->bins));
	/* Now free the handle */
	slapi_ch_free((void **)&handle);
	return 0;
}

/* This function returns -1 or -2 for local errors, and DB_ errors as well. */

static int 
index_buffer_insert(void *h, DBT *key, ID id,backend *be, DB_TXN *txn,struct attrinfo *a)
{
	index_buffer_handle *handle = (index_buffer_handle *) h;
	index_buffer_bin *bin = NULL;
	size_t index = 0;
	int idl_ret = 0;
	unsigned char x = 0;
	unsigned int i = 0;
	int ret = 0;

	PR_ASSERT(h);

	/* Check key length for validity */
	if (key->size > handle->max_key_length) {
		return -2;
	}
	/* discard the first character, as long as its the substring prefix */
	if ((unsigned char)((char*)key->data)[0] != SUB_PREFIX) {
		return -2;
	}
	/* Compute the bin index from the key */
	/* Walk along the key data, byte by byte */
	for (i = 1; i < (key->size - 1); i++) {
		/* foreach byte, normalize to the range we accept */
		x = (unsigned char) ((char*)key->data)[i];
		if ( (x == handle->special_byte_a) || (x == handle->special_byte_b) ) {
			if (x == handle->special_byte_a) {
				x = handle->high_key_byte_range + 1;
			}
			if (x == handle->special_byte_b) {
				x = handle->high_key_byte_range + 2;
			}
		} else {
			if ( x >= '0' && x <= '9' ) {
				x = (x - '0') + handle->high_key_byte_range + 3;
			} else {
				if (x > handle->high_key_byte_range) {
					return -2; /* Out of range */
				}
				if (x < handle->low_key_byte_range) {
					return -2; /* Out of range */
				}
			}
		}
		x = x - handle->low_key_byte_range;
		index *= handle->byte_range;
		index += x;
	}
	/* Check that the last byte in the key is zero */
	if (0 != (unsigned char)((char*)key->data)[i]) { 
		return -2;
	}
	PR_ASSERT(index < handle->buffer_size);
	/* Get the bin */
	bin = &(handle->bins[index]);
	/* Is the key already there ? */
retry:
	if (!(bin->key).data) {
		(bin->key).size = key->size;
		(bin->key).data = slapi_ch_malloc(key->size);
		if (NULL == bin->key.data) {
			return -1;
		}
		memcpy(bin->key.data,key->data,key->size);
		/* Make the IDL */
		bin->value = idl_alloc(handle->idl_size);
		if (!bin->value) {
			return -1;
		}
	}
	idl_ret = idl_append(bin->value, id);
	if (0 != idl_ret) {
		if (1 == idl_ret) {
			/* ID already present */
		} else {
			/* If we get to here, it means that we've overflowed our IDL */
			/* So, we need to write it out to the DB and zero out the pointers */
			ret = index_put_idl(bin,be,txn,a);
			/* Now we need to append the ID we have at hand */
			if (0 == ret) {
				goto retry;
			}
		}
	}
	return ret;
}

/*
 * Add or Delete an entry from the attribute indexes.
 * 'flags' is either BE_INDEX_ADD or BE_INDEX_DEL
 */
int
index_addordel_entry(
    backend *be,
    struct backentry	*e,
    int			flags,
    back_txn		*txn
)
{
    char		*type = NULL;
    Slapi_Value	**svals;
    int		rc, result;
    Slapi_Attr		*attr;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> index_%s_entry( \"%s\", %lu )\n",
               (flags & BE_INDEX_ADD) ? "add" : "del",
               backentry_get_ndn(e), (u_long)e->ep_id );

    /* if we are adding a tombstone entry (see ldbm_add.c) */
    if ((flags & BE_INDEX_TOMBSTONE) && (flags & BE_INDEX_ADD))
    {
        Slapi_DN parent;
        Slapi_DN *sdn = slapi_entry_get_sdn(e->ep_entry);
        slapi_sdn_init(&parent);
        slapi_sdn_get_parent(sdn, &parent);
        /*
         * Just index the "nstombstone" attribute value from the objectclass
         * attribute, and the nsuniqueid attribute value, and the 
         * nscpEntryDN value of the deleted entry.
         */
        result = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE, e->ep_id, flags, txn);
        if ( result != 0 ) {
            ldbm_nasty(errmsg, 1010, result);
            return( result );
        }
        result = index_addordel_string(be, SLAPI_ATTR_UNIQUEID, slapi_entry_get_uniqueid(e->ep_entry), e->ep_id, flags, txn);
        if ( result != 0 ) {
            ldbm_nasty(errmsg, 1020, result);
            return( result );
        }
        result = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(&parent), e->ep_id, flags, txn);
        if ( result != 0 ) {
            ldbm_nasty(errmsg, 1021, result);
            return( result );
        }
        slapi_sdn_done(&parent);
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            Slapi_Attr* attr;
            /* Even if this is a tombstone, we have to add it to entryrdn
             * to maintain the full DN
             */ 
            result = entryrdn_index_entry(be, e, flags, txn);
            if ( result != 0 ) {
                ldbm_nasty(errmsg, 1023, result);
                return( result );
            }
            /* To maintain tombstonenumsubordinates,
             * parentid is needed for tombstone, as well. */
            slapi_entry_attr_find(e->ep_entry, LDBM_PARENTID_STR, &attr);
            if (attr) {
                svals = attr_get_present_values(attr);
                result = index_addordel_values_sv(be, LDBM_PARENTID_STR, svals, NULL,
                                                  e->ep_id, flags, txn);
                if ( result != 0 ) {
                    ldbm_nasty(errmsg, 1022, result);
                    return( result );
                }
            }
        }
    }
    else
    {   /* NOT a tombstone or delete a tombstone */
        /* add each attribute to the indexes */
        rc = 0, result = 0;
        int entryrdn_done = 0;
        for ( rc = slapi_entry_first_attr( e->ep_entry, &attr ); rc == 0;
              rc = slapi_entry_next_attr( e->ep_entry, attr, &attr ) ) {
            slapi_attr_get_type( attr, &type );
            svals = attr_get_present_values(attr);
            if ( !entryrdn_done && (0 == strcmp( type, LDBM_ENTRYDN_STR ))) {
                entryrdn_done = 1;
                if (entryrdn_get_switch()) { /* subtree-rename: on */
                    /* skip "entrydn" */
                    continue;
                } else {
                    /* entrydn is case-normalized */
                    slapi_values_set_flags(svals,
                                           SLAPI_ATTR_FLAG_NORMALIZED_CIS);
                }
            }
            result = index_addordel_values_sv( be, type, svals, NULL,
                                               e->ep_id, flags, txn );
            if ( result != 0 ) {
                ldbm_nasty(errmsg, 1030, result);
                return( result );
            }
        }

        if (!entryrdn_get_noancestorid()) {
            /* update ancestorid index . . . */
            /* . . . only if we are not deleting a tombstone entry -
             * tombstone entries are not in the ancestor id index -
             * see bug 603279
             */
            if (!((flags & BE_INDEX_TOMBSTONE) && (flags & BE_INDEX_DEL))) {
                result = ldbm_ancestorid_index_entry(be, e, flags, txn);
                if ( result != 0 ) {
                    return( result );
                }
            }
        }
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            result = entryrdn_index_entry(be, e, flags, txn);
            if ( result != 0 ) {
                ldbm_nasty(errmsg, 1031, result);
                return( result );
            }
        }
    }
    
    LDAPDebug( LDAP_DEBUG_TRACE, "<= index_%s_entry%s %d\n",
               (flags & BE_INDEX_ADD) ? "add" : "del", 
               (flags & BE_INDEX_TOMBSTONE) ? " (tombstone)" : "", result );
    return( result );
}

/*
 * Add ID to attribute indexes for which Add/Replace/Delete modifications exist
 * [olde is the OLD entry, before modifications]
 * [newe is the NEW entry, after modifications]
 * the old entry is used for REPLACE; the new for DELETE */
int 
index_add_mods(
    backend *be,
    LDAPMod **mods,
    struct backentry 	*olde,
    struct backentry 	*newe,
    back_txn *txn
)
{
    int rc = 0;
    int i, j;
    ID 	id = olde->ep_id;
    int flags = 0;
    char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    char *basetype = NULL;
    char *tmp = NULL;
    Slapi_Attr *curr_attr = NULL;
    Slapi_ValueSet *all_vals = NULL;
    Slapi_ValueSet *mod_vals = NULL;
    Slapi_Value **evals = NULL;               /* values that still exist after a
                                               * delete.
                                               */
    Slapi_Value **mods_valueArray = NULL;     /* values that are specified in this
                                               * operation.
                                               */
    Slapi_Value **deleted_valueArray = NULL;  /* values whose index entries
                                               * should be deleted.
                                               */

    for ( i = 0; mods && mods[i] != NULL; i++ ) {
        /* Get base attribute type */
        basetype = buf;
        tmp = slapi_attr_basetype(mods[i]->mod_type, buf, sizeof(buf));
        if(tmp != NULL) {
            basetype = tmp; /* basetype was malloc'd */
        }

        /* Get a list of all remaining values for the base type
         * and any present subtypes.
         */
        all_vals = slapi_valueset_new();

        for (curr_attr = newe->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
            if (slapi_attr_type_cmp( basetype, curr_attr->a_type, SLAPI_TYPE_CMP_BASE ) == 0) {
                valueset_add_valuearray(all_vals, attr_get_present_values(curr_attr));
            }
        }
 
        evals = valueset_get_valuearray(all_vals);

        /* Get a list of all values specified in the operation.
         */
        if ( mods[i]->mod_bvalues != NULL ) {
            valuearray_init_bervalarray(mods[i]->mod_bvalues, &mods_valueArray);
        }

        switch ( mods[i]->mod_op & ~LDAP_MOD_BVALUES ) {
        case LDAP_MOD_REPLACE:
            flags = BE_INDEX_DEL;
            /* Get a list of all values being deleted.
             */
            mod_vals = slapi_valueset_new();

            for (curr_attr = olde->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
                if (slapi_attr_type_cmp( mods[i]->mod_type, curr_attr->a_type, SLAPI_TYPE_CMP_EXACT ) == 0) {
                    valueset_add_valuearray(mod_vals, attr_get_present_values(curr_attr));
                }
            }
                                                                                                                            
            deleted_valueArray = valueset_get_valuearray(mod_vals);

            /* If subtypes exist, don't remove the presence
             * index.
             */
            if ( evals != NULL && deleted_valueArray != NULL) {
                /* evals will contain the new value that is being
                 * added as part of the replace operation if one
                 * was specified.  We must remove this value from
                 * evals to know if any subtypes are present.
                 */
                slapi_entry_attr_find( olde->ep_entry, mods[i]->mod_type, &curr_attr );
                if ( mods_valueArray != NULL ) {
                    for ( j = 0; mods_valueArray[j] != NULL; j++ ) {
                        Slapi_Value *rval = valuearray_remove_value(curr_attr, evals, mods_valueArray[j]);
                        slapi_value_free( &rval );
                    }
                }

                /* Search evals for the values being deleted.  If
                 * they don't exist, delete the equality index.
                 */
                for ( j = 0; deleted_valueArray[j] != NULL; j++ ) {
                    if (valuearray_find(curr_attr, evals, deleted_valueArray[j]) == -1) {
                        if (!(flags & BE_INDEX_EQUALITY)) {
                            flags |= BE_INDEX_EQUALITY;
                        }
                    } else {
                        Slapi_Value *rval = valuearray_remove_value(curr_attr, deleted_valueArray, deleted_valueArray[j]);
                        slapi_value_free( &rval );
                        j--;
                        /* indicates there was some conflict */
                        mods[i]->mod_op |= LDAP_MOD_IGNORE;
                    }
                }
            } else {
                flags |= BE_INDEX_PRESENCE|BE_INDEX_EQUALITY;
            }

            /* We need to first remove the old values from the 
             * index, if any. */
            if (deleted_valueArray) {
                rc = index_addordel_values_sv( be, mods[i]->mod_type,
                                               deleted_valueArray, evals, id, 
                                               flags, txn );
                if (rc) {
                    ldbm_nasty(errmsg, 1041, rc);
                    goto error;
                }
            }

            /* Free valuearray */
            slapi_valueset_free(mod_vals);
            mod_vals = NULL;
        case LDAP_MOD_ADD:
            if ( mods_valueArray == NULL ) {
                rc = 0;
            } else {
                /* Verify if the value is in newe.
                 * If it is in, we will add the attr value to the index file. */
                curr_attr = NULL;
                slapi_entry_attr_find(newe->ep_entry, 
                                      mods[i]->mod_type, &curr_attr);
                
                if (curr_attr) { /* found the type */
                    for (j = 0; mods_valueArray[j] != NULL; j++) {
                        /* mods_valueArray[j] is in curr_attr ==> return 0 */
                        if (slapi_attr_value_find(curr_attr,
                                slapi_value_get_berval(mods_valueArray[j]))) {
                            /* The value is NOT in newe, remove it. */
                            Slapi_Value *rval;
                            rval = valuearray_remove_value(curr_attr,
                                                           mods_valueArray,
                                                           mods_valueArray[j]);
                            slapi_value_free( &rval );
                            /* indicates there was some conflict */
                            mods[i]->mod_op |= LDAP_MOD_IGNORE;
                        }
                    }
                    if (mods_valueArray) {
                        rc = index_addordel_values_sv( be,
                                                   mods[i]->mod_type, 
                                                   mods_valueArray, NULL,
                                                   id, BE_INDEX_ADD, txn );
                        if (rc) {
                            ldbm_nasty(errmsg, 1042, rc);
                            goto error;
                        }
                    } else {
                        rc = 0;
                    }
                }
            }
            break;

        case LDAP_MOD_DELETE:
            if ( (mods[i]->mod_bvalues == NULL) ||
                 (mods[i]->mod_bvalues[0] == NULL) ) {
                rc = 0;
                flags = BE_INDEX_DEL;

                /* Get a list of all values that are being
                 * deleted.
                 */
                mod_vals = slapi_valueset_new();

                for (curr_attr = olde->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
                        if (slapi_attr_type_cmp( mods[i]->mod_type, curr_attr->a_type, SLAPI_TYPE_CMP_EXACT ) == 0) {
                            valueset_add_valuearray(mod_vals, attr_get_present_values(curr_attr));
                        }
                }

                deleted_valueArray = valueset_get_valuearray(mod_vals);

                /* If subtypes exist, don't remove the
                 * presence index.
                 */
                if (evals != NULL) {
                    for (curr_attr = newe->ep_entry->e_attrs; (curr_attr != NULL);
                         curr_attr = curr_attr->a_next) {
                        if (slapi_attr_type_cmp( basetype, curr_attr->a_type, SLAPI_TYPE_CMP_BASE ) == 0) {
                            /* Check if the any values being deleted
                             * also exist in a subtype.
                             */
                            for (j = 0; deleted_valueArray && deleted_valueArray[j]; j++) {
                                if ( valuearray_find(curr_attr, evals, deleted_valueArray[j]) == -1 ) {
                                    /* If the equality flag isn't already set, set it */
                                    if (!(flags & BE_INDEX_EQUALITY)) {
                                        flags |= BE_INDEX_EQUALITY;
                                    }
                                } else {
                                    /* Remove duplicate value from the mod list */
                                    Slapi_Value *rval = valuearray_remove_value(curr_attr, deleted_valueArray, deleted_valueArray[j]);
                                    slapi_value_free( &rval );
                                    j--;
                                }
                            }
                        }
                    }
                } else {
                    flags = BE_INDEX_DEL|BE_INDEX_PRESENCE|BE_INDEX_EQUALITY;
                }

                /* Update the index, if necessary */
                if (deleted_valueArray) {
                    rc = index_addordel_values_sv( be, mods[i]->mod_type,
                                                   deleted_valueArray, evals, id, 
                                                   flags, txn );
                    if (rc) {
                        ldbm_nasty(errmsg, 1043, rc);
                        goto error;
                    }
                }

                slapi_valueset_free(mod_vals);
                mod_vals = NULL;
            } else {

                /* determine if the presence key should be
                 * removed (are we removing the last value
                 * for this attribute?)
                 */
                if (evals == NULL || evals[0] == NULL) {
                    /* The new entry newe does not have the attribute at all
                     * including the one with subtypes.  Thus it's safe to
                     * remove the presence and equality index.
                     */
                    flags = BE_INDEX_DEL|BE_INDEX_PRESENCE|BE_INDEX_EQUALITY;
                } else {
                    flags = BE_INDEX_DEL;

                    /* If the same value doesn't exist in a subtype, set
                     * BE_INDEX_EQUALITY flag so the equality index is
                     * removed.
                     */
                    curr_attr = NULL;
                    slapi_entry_attr_find(olde->ep_entry,
                                          mods[i]->mod_type, &curr_attr);
                    if (curr_attr) {
                        int found = 0;
                        for (j = 0; mods_valueArray[j] != NULL; j++ ) {
                            if ( valuearray_find(curr_attr, evals, mods_valueArray[j]) > -1 ) {
                                /* The same value found in evals. 
                                 * We don't touch the equality index. */
                                found = 1;
                                break;
                            }
                        }
                        /* 
                         * to-be-deleted curr_attr does not exist in the 
                         * new value set evals.  So, we can remove it.
                         */
                        if (!found && !(flags & BE_INDEX_EQUALITY)) {
                            flags |= BE_INDEX_EQUALITY;
                        }
                    } 
                }

                rc = index_addordel_values_sv( be, basetype,
                                               mods_valueArray,
                                               evals, id, flags, txn );
                if (rc) {
                    ldbm_nasty(errmsg, 1044, rc);
                    goto error;
                }
            }
            rc = 0;
            break;
        } /* switch ( mods[i]->mod_op & ~LDAP_MOD_BVALUES ) */

error:
        /* free memory */
        slapi_ch_free((void **)&tmp);
        tmp = NULL;
        valuearray_free(&mods_valueArray);
        mods_valueArray = NULL;
        slapi_valueset_free(all_vals);
        all_vals = NULL;
        slapi_valueset_free(mod_vals);
        mod_vals = NULL;

        if ( rc != 0 ) {
            ldbm_nasty(errmsg, 1040, rc);
            return( rc );
        }
    } /* for ( i = 0; mods[i] != NULL; i++ ) */

    return( 0 );
}


/*
 * Convert a 'struct berval' into a displayable ASCII string
 */

#define SPECIAL(c) (c < 32 || c > 126 || c == '\\' || c == '"')

const char*
encode (const struct berval* data, char buf[BUFSIZ])
{
    char* s;
    char* last;
    if (data == NULL || data->bv_len == 0) return "";
    last = data->bv_val + data->bv_len - 1;
    for (s = data->bv_val; s < last; ++s) {
	if ( SPECIAL (*s)) {
	    char* first = data->bv_val;
	    char* bufNext = buf;
	    size_t bufSpace = BUFSIZ - 4;
	    while (1) {
/* printf ("%lu bytes ASCII\n", (unsigned long)(s - first)); */
		if (bufSpace < (size_t)(s - first)) s = first + bufSpace - 1;
		if (s != first) {
		    memcpy (bufNext, first, s - first);
		    bufNext  += (s - first);
		    bufSpace -= (s - first);
		}
		do {
		    *bufNext++ = '\\'; --bufSpace;
		    if (bufSpace < 2) {
			memcpy (bufNext, "..", 2);
			bufNext += 2;
			goto bail;
		    }
		    if (*s == '\\' || *s == '"') {
			*bufNext++ = *s; --bufSpace;
		    } else {
			sprintf (bufNext, "%02x", (unsigned)*(unsigned char*)s);
			bufNext += 2; bufSpace -= 2;
		    }
	        } while (++s <= last && SPECIAL (*s));
		if (s > last) break;
		first = s;
		while ( ! SPECIAL (*s) && s <= last) ++s;
	    }
	  bail:
	    *bufNext = '\0';
/* printf ("%lu chars in buffer\n", (unsigned long)(bufNext - buf)); */
	    return buf;
	}
    } 
/* printf ("%lu bytes, all ASCII\n", (unsigned long)(s - data->bv_val)); */
    return data->bv_val;
}

static const char*
encoded (DBT* d, char buf [BUFSIZ])
{
    struct berval data;
    data.bv_len = d->dsize;
    data.bv_val = d->dptr;
    return encode (&data, buf);
}

IDList *
index_read(
    backend *be,
    char		*type,
    const char		*indextype,
    const struct berval	*val,
    back_txn		*txn,
    int			*err
)
{
    return index_read_ext(be, type, indextype, val, txn, err, NULL);
}

/*
 * Extended version of index_read.
 * The unindexed flag can be used to distinguish between a
 * return of allids due to the attr not being indexed or
 * the value really being allids.
 * You can pass in the value of the allidslimit (aka idlistscanlimit)
 * with this version of the function
 * if the value is 0, it will use the old method of getting the value
 * from the attrinfo*.
 */
IDList *
index_read_ext_allids(
    backend *be,
    char		*type,
    const char		*indextype,
    const struct berval	*val,
    back_txn		*txn,
    int			*err,
    int			*unindexed,
    int         allidslimit
)
{
	DB		*db = NULL;
	DB_TXN 		*db_txn = NULL;
	DBT   		key = {0};
	IDList		*idl = NULL;
	char		*prefix;
	char		*tmpbuf = NULL;
	char		buf[BUFSIZ];
	char		typebuf[ SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH ];
	struct attrinfo	*ai = NULL;
	char		*basetmp, *basetype;
	int retry_count  = 0;
	struct berval	*encrypted_val = NULL;

	*err = 0;

	if (unindexed != NULL) *unindexed = 0;
	prefix = index_index2prefix( indextype );
	if (prefix == NULL) {
		LDAPDebug0Args( LDAP_DEBUG_ANY, "index_read_ext: NULL prefix\n" );
		return NULL;
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "=> index_read( \"%s\" %s \"%s\" )\n",
		   type, prefix, encode (val, buf));

	basetype = typebuf;
	if ( (basetmp = slapi_attr_basetype( type, typebuf, sizeof(typebuf) ))
	    != NULL ) {
		basetype = basetmp;
	}

	ainfo_get( be, basetype, &ai );
	if (ai == NULL) {
		index_free_prefix( prefix );
		slapi_ch_free_string( &basetmp );
		return NULL;
	}

	LDAPDebug( LDAP_DEBUG_ARGS, "   indextype: \"%s\" indexmask: 0x%x\n",
	    indextype, ai->ai_indexmask, 0 );

	/* If entryrdn switch is on AND the type is entrydn AND the prefix is '=', 
	 * use the entryrdn index directly */
	if (entryrdn_get_switch() && (*prefix == '=') && 
		(0 == PL_strcasecmp(basetype, LDBM_ENTRYDN_STR))) {
		int rc = 0;
		ID id = 0;
		Slapi_DN sdn;

		/* We don't need these values... */
		index_free_prefix( prefix );
		slapi_ch_free_string( &basetmp );
		if (NULL == val || NULL == val->bv_val) {
			/* entrydn value was not given */
			return NULL;
		}
		slapi_sdn_init_dn_byval(&sdn, val->bv_val);
		rc = entryrdn_index_read(be, &sdn, &id, txn);
		slapi_sdn_done(&sdn);
		if (rc) { /* failure */
			return NULL;
		} else {  /* success */
			rc = idl_append_extend(&idl, id);
			if (rc) { /* failure */
				return NULL;
			}
			return idl;
		}
	}

	if ( !is_indexed( indextype, ai->ai_indexmask, ai->ai_index_rules ) ) {
		idl =  idl_allids( be );
                if (unindexed != NULL) *unindexed = 1;
		LDAPDebug( LDAP_DEBUG_TRACE, "<= index_read %lu candidates "
		    "(allids - not indexed)\n", (u_long)IDL_NIDS(idl), 0, 0 );
		index_free_prefix( prefix );
		slapi_ch_free_string( &basetmp );
		return( idl );
	}
	if ( (*err = dblayer_get_index_file( be, ai, &db, DBOPEN_CREATE )) != 0 ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= index_read NULL (index file open for attr %s)\n",
		    basetype, 0, 0 );
		index_free_prefix (prefix);
		slapi_ch_free_string( &basetmp );
		return( NULL );
	}

	if ( val != NULL ) {
		size_t		plen, vlen;
                char		*realbuf;
		int ret = 0;
		
		/* If necessary, encrypt this index key */
		ret = attrcrypt_encrypt_index_key(be, ai, val, &encrypted_val);
		if (ret) {
			LDAPDebug( LDAP_DEBUG_ANY,
		    "index_read failed to encrypt index key for %s\n",
		    basetype, 0, 0 );
		}
		if (encrypted_val) {
			val = encrypted_val;
		}
		plen = strlen( prefix );
		vlen = val->bv_len;
		realbuf = (plen + vlen < sizeof(buf)) ?
		    buf : (tmpbuf = slapi_ch_malloc( plen + vlen + 1 ));
		memcpy( realbuf, prefix, plen );
		memcpy( realbuf+plen, val->bv_val, vlen );
		realbuf[plen+vlen] = '\0';
		key.data = realbuf;
		key.size = key.ulen = plen + vlen + 1;
		key.flags = DB_DBT_USERMEM;	
	} else {
		key.data = prefix;
		key.size = key.ulen = strlen( prefix ) + 1; /* include 0 terminator */
		key.flags = DB_DBT_USERMEM;	
	}
	if (NULL != txn) {
		db_txn = txn->back_txn_txn;
	}
	for (retry_count = 0; retry_count < IDL_FETCH_RETRY_COUNT; retry_count++) {
	  *err = NEW_IDL_DEFAULT;
	  idl = idl_fetch_ext( be, db, &key, db_txn, ai, err, allidslimit );
	  if(*err == DB_LOCK_DEADLOCK) {
	    ldbm_nasty("index read retrying transaction", 1045, *err);
#ifdef FIX_TXN_DEADLOCKS
#error can only retry here if txn == NULL - otherwise, have to abort and retry txn
#endif
	    continue;
	  } else {
	    break;
	  }
	}
	if(retry_count == IDL_FETCH_RETRY_COUNT) {
	  ldbm_nasty("index_read retry count exceeded",1046,*err);
	} else if ( *err != 0 && *err != DB_NOTFOUND ) {
	  ldbm_nasty(errmsg, 1050, *err);
	}
	slapi_ch_free_string( &basetmp );
	slapi_ch_free_string(&tmpbuf);

	dblayer_release_index_file( be, ai, db );

	index_free_prefix (prefix);

	if (encrypted_val) {
		ber_bvfree(encrypted_val);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= index_read %lu candidates\n",
                   (u_long)IDL_NIDS(idl), 0, 0 );
	return( idl );
}

IDList *
index_read_ext(
    backend *be,
    char		*type,
    const char		*indextype,
    const struct berval	*val,
    back_txn		*txn,
    int			*err,
    int			*unindexed
)
{
    return index_read_ext_allids(be, type, indextype, val, txn, err, unindexed, 0);
}

/* This function compares two index keys.  It is assumed
   that the values are already normalized, since they should have
   been when the index was created (by int_values2keys).

   richm - actually, the current syntax compare functions
   always normalize both arguments.  We need to add an additional
   syntax compare function that does not normalize or takes
   an argument like value_cmp to specify to normalize or not.

   More fun - this function is used to compare both raw database
   keys (e.g. with the prefix '=' or '+' or '*' etc.) and without
   (in the case of two equality keys, we want to strip off the
   leading '=' to compare the actual values).  We only use the
   value_compare function if both keys are equality keys with
   some data after the equality prefix.  In every other case,
   we will just use a standard berval cmp function.

   see also dblayer_bt_compare
*/
static int
DBTcmp (DBT* L, DBT* R, value_compare_fn_type cmp_fn)
{
    struct berval Lv;
    struct berval Rv;

    if ((L->data && (L->size>1) && (*((char*)L->data) == EQ_PREFIX)) &&
        (R->data && (R->size>1) && (*((char*)R->data) == EQ_PREFIX))) {
        Lv.bv_val = (char*)L->data+1; Lv.bv_len = (ber_len_t)L->size-1;
        Rv.bv_val = (char*)R->data+1; Rv.bv_len = (ber_len_t)R->size-1;
        /* use specific compare fn, if any */
        cmp_fn = (cmp_fn ? cmp_fn : slapi_berval_cmp);
    } else {
        Lv.bv_val = (char*)L->data; Lv.bv_len = (ber_len_t)L->size;
        Rv.bv_val = (char*)R->data; Rv.bv_len = (ber_len_t)R->size;
        /* just compare raw bervals */
        cmp_fn = slapi_berval_cmp;
    }
    return cmp_fn(&Lv, &Rv);
}

/* This only works with normalized keys, which
   should be ok because at this point both L and R
   should have already been normalized
*/
#define DBT_EQ(L,R) ((L)->dsize == (R)->dsize &&\
 ! memcmp ((L)->dptr, (R)->dptr, (L)->dsize))


#define DBT_FREE_PAYLOAD(d) if ((d).data) {free((d).data);(d).data=NULL;}

/* Steps to the next key without keeping a cursor open */
/* Returns the new key value in the DBT */
static int index_range_next_key(DB *db,DBT *key,DB_TXN *db_txn)
{
	DBC *cursor = NULL;
	DBT data = {0};
	int ret = 0;
	void *saved_key = key->data;

	/* Make cursor */
retry:
	ret = db->cursor(db,db_txn,&cursor, 0);
	if (0 != ret) {
		return ret;
	}
	/* Seek to the last key */
	data.flags = DB_DBT_MALLOC;
	ret = cursor->c_get(cursor,key,&data,DB_SET); /* both key and data could be allocated */
	/* data allocated here, we don't need it */
	DBT_FREE_PAYLOAD(data);
	if (DB_NOTFOUND == ret) {
		void *old_key_buffer = key->data;
		/* If this happens, it means that we tried to seek to a key which has just been deleted */
		/* So, we seek to the nearest one instead */
		ret = cursor->c_get(cursor,key,&data,DB_SET_RANGE); 
		/* a new key and data are allocated here, need to free them both */
		if (old_key_buffer != key->data) {
			DBT_FREE_PAYLOAD(*key);
		}
		DBT_FREE_PAYLOAD(data);
	}
	if (0 != ret) {
		if (DB_LOCK_DEADLOCK == ret)
		{
			/* Deadlock detected, retry the operation */
			cursor->c_close(cursor);
			cursor = NULL;
			key->data = saved_key;
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the cursor
#endif
			goto retry;
		} else
		{
			goto error;
		}
	}
	if (saved_key != key->data) {
		/* key could be allocated in the above c_get */
		DBT_FREE_PAYLOAD(*key);
	}
	/* Seek to the next one 
	 * [612498] NODUP is needed for new idl to get the next non-duplicated key
	 * No effect on old idl since there's no dup there (i.e., DB_NEXT == DB_NEXT_NODUP)
	 */
	ret = cursor->c_get(cursor,key,&data,DB_NEXT_NODUP); /* new key and data are allocated, we only need the key */
	DBT_FREE_PAYLOAD(data);
	if (DB_LOCK_DEADLOCK == ret)
	{
		/* Deadlock detected, retry the operation */
		cursor->c_close(cursor);
		cursor = NULL;
		key->data = saved_key;
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the cursor
#endif
		goto retry;
	}
error:
	/* Close the cursor */
	cursor->c_close(cursor);
	if (saved_key) { /* Need to free the original key passed in */
		if (saved_key == key->data) {
			/* Means that we never allocated a new key */
			;
		} else {
			slapi_ch_free(&saved_key);
		}
	}
	return ret;
}

IDList *
index_range_read_ext(
    Slapi_PBlock *pb,
    backend *be,
    char            *type,
    const char      *indextype,
    int             operator,
    struct berval   *val,
    struct berval   *nextval,
    int             range,
    back_txn        *txn,
    int             *err,
    int             allidslimit
)
{
    struct ldbminfo *li = (struct ldbminfo *) be->be_database->plg_private;
    DB     *db;
    DB_TXN *db_txn = NULL;
    DBC    *dbc = NULL;
    DBT    lowerkey = {0};
    DBT    upperkey = {0};
    DBT    cur_key = {0};
    DBT    data = {0} ;
    IDList *idl= NULL;
    char   *prefix = NULL;
    char   *realbuf, *nextrealbuf;
    size_t reallen, nextreallen;
    size_t plen;
    ID     i;
    struct attrinfo    *ai = NULL;
    int lookthrough_limit = -1; /* default no limit */
    int retry_count = 0;
    int is_and = 0;
    int sizelimit = 0;
    time_t curtime, stoptime, optime;
    int timelimit = -1;
    back_search_result_set *sr = NULL;
    int isroot = 0;

    if (!pb) {
        LDAPDebug(LDAP_DEBUG_ANY, "index_range_read: NULL pblock\n",
                  0, 0, 0);
        return NULL;
    }

    *err = 0;

    prefix = index_index2prefix( indextype );
    if (prefix == NULL) {
        LDAPDebug0Args( LDAP_DEBUG_ANY, "index_range_read: NULL prefix\n" );
        return( NULL );
    }

    plen = strlen(prefix);
    slapi_pblock_get(pb, SLAPI_SEARCH_IS_AND, &is_and);
    if (!is_and)
    {
        slapi_pblock_get(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);
    }
    slapi_pblock_get( pb, SLAPI_OPINITIATED_TIME, &optime );
    slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);
    stoptime = optime + timelimit;

    /*
     * Determine the lookthrough_limit from the PBlock.
     * No limit if there is no search result set and the requestor is root.
     */

    slapi_pblock_get( pb, SLAPI_SEARCH_RESULT_SET, &sr );
    if (sr != NULL) {
        /* the normal case */
        lookthrough_limit = sr->sr_lookthroughlimit;
    }
    slapi_pblock_get( pb, SLAPI_REQUESTOR_ISROOT, &isroot );
    if (!isroot) {
        if (lookthrough_limit > li->li_rangelookthroughlimit) {
            lookthrough_limit = li->li_rangelookthroughlimit; 
        }
    }

    LDAPDebug(LDAP_DEBUG_TRACE, "index_range_read lookthrough_limit=%d\n",
              lookthrough_limit, 0, 0);

    switch( operator ) {
      case SLAPI_OP_LESS:
      case SLAPI_OP_LESS_OR_EQUAL:
      case SLAPI_OP_GREATER_OR_EQUAL:
      case SLAPI_OP_GREATER:
        break;
      default:
        LDAPDebug( LDAP_DEBUG_ANY,
              "<= index_range_read(%s,%s) NULL (operator %i)\n",
              type, prefix, operator );
        index_free_prefix(prefix);
        return( NULL );
    }
    ainfo_get( be, type, &ai );
    if (ai == NULL) {
        index_free_prefix(prefix);
        return NULL;
    }
    LDAPDebug( LDAP_DEBUG_ARGS, "   indextype: \"%s\" indexmask: 0x%x\n",
        indextype, ai->ai_indexmask, 0 );
    if ( !is_indexed( indextype, ai->ai_indexmask, ai->ai_index_rules )) {
        idl = idl_allids( be );
        LDAPDebug( LDAP_DEBUG_TRACE,
            "<= index_range_read(%s,%s) %lu candidates (allids)\n",
            type, prefix, (u_long)IDL_NIDS(idl) );
        index_free_prefix(prefix);
        return( idl );
    }
    if ( (*err = dblayer_get_index_file( be, ai, &db, DBOPEN_CREATE )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY,
            "<= index_range_read(%s,%s) NULL (could not open index file)\n",
            type, prefix, 0 );
        index_free_prefix(prefix);
        return( NULL ); /* why not allids? */
    }
    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }
    /* get a cursor so we can walk over the table */
    *err = db->cursor(db,db_txn,&dbc,0);
    if (0 != *err ) {
                ldbm_nasty(errmsg, 1060, *err);
        LDAPDebug( LDAP_DEBUG_ANY,
            "<= index_range_read(%s,%s) NULL: db->cursor() == %i\n",
            type, prefix, *err );
        dblayer_release_index_file( be, ai, db );
        index_free_prefix(prefix);
        return( NULL ); /* why not allids? */
    }

    /* set up the starting and ending keys for a range search */ 
    if ( val != NULL ) { /* compute a key from val */
        const size_t vlen = val->bv_len;
        reallen = plen + vlen + 1;
        realbuf = slapi_ch_malloc( reallen );
        memcpy( realbuf, prefix, plen );
        memcpy( realbuf+plen, val->bv_val, vlen );
        realbuf[plen+vlen] = '\0';
    } else {
        reallen = plen + 1; /* include 0 terminator */
        realbuf = slapi_ch_strdup(prefix);
    }
    if (range != 1) {
        char *tmpbuf = NULL;
        /* this is a search with only one boundary value */
        switch( operator ) {
          case SLAPI_OP_LESS:
          case SLAPI_OP_LESS_OR_EQUAL:
            lowerkey.dptr = slapi_ch_strdup(prefix);
            lowerkey.dsize = plen;
            upperkey.dptr = realbuf;
            upperkey.dsize = reallen;
            break;
          case SLAPI_OP_GREATER_OR_EQUAL:
          case SLAPI_OP_GREATER:
            lowerkey.dptr = realbuf;
            lowerkey.dsize = reallen;
            /* upperkey = a value slightly greater than prefix */
            tmpbuf = slapi_ch_malloc (plen + 1);
            memcpy (tmpbuf, prefix, plen + 1);
            ++(tmpbuf[plen-1]);
            upperkey.dptr = tmpbuf;
            upperkey.dsize = plen;
            tmpbuf = NULL;
            /* ... but not greater than the last key in the index */
            cur_key.flags = DB_DBT_MALLOC;
            data.flags = DB_DBT_MALLOC;
            *err = dbc->c_get(dbc,&cur_key,&data,DB_LAST); /* key and data allocated here, need to free them */
            DBT_FREE_PAYLOAD(data);
            /* Note that cur_key needs to get freed somewhere below */
            if (0 != *err) {
                if (DB_NOTFOUND == *err) { 
                    /* There are no keys in the index so we should return no candidates. */
                    *err = 0; 
                    idl = NULL; 
                    slapi_ch_free( (void**)&realbuf);
                    dbc->c_close(dbc); 
                    goto error;
                } else {
                    ldbm_nasty(errmsg, 1070, *err);
                    LDAPDebug( LDAP_DEBUG_ANY,
                        "index_range_read(%s,%s) seek to end of index file err %i\n",
                        type, prefix, *err );
                }
            } else if (DBTcmp (&upperkey, &cur_key, ai->ai_key_cmp_fn) > 0) {
                tmpbuf = slapi_ch_realloc (tmpbuf, cur_key.dsize);
                memcpy (tmpbuf, cur_key.dptr, cur_key.dsize);
                DBT_FREE_PAYLOAD(upperkey);
                upperkey.dptr = tmpbuf;
                upperkey.dsize = cur_key.dsize;
            }
            break;
        }
    } else {
        /* this is a search with two boundary values (starting and ending) */
        if ( nextval != NULL ) { /* compute a key from nextval */
            const size_t vlen = nextval->bv_len;
            nextreallen = plen + vlen + 1;
            nextrealbuf =  slapi_ch_malloc( plen + vlen + 1 );
            memcpy( nextrealbuf, prefix, plen );
            memcpy( nextrealbuf+plen, nextval->bv_val, vlen );
            nextrealbuf[plen+vlen] = '\0';
        } else {
            nextreallen = plen + 1; /* include 0 terminator */
            nextrealbuf = slapi_ch_strdup(prefix);
        }
        /* set up the starting and ending keys for search */ 
        switch( operator ) {
          case SLAPI_OP_LESS:
          case SLAPI_OP_LESS_OR_EQUAL:
            lowerkey.dptr = nextrealbuf;
            lowerkey.dsize = nextreallen;
            upperkey.dptr = realbuf;
            upperkey.dsize = reallen;
            break;
          case SLAPI_OP_GREATER_OR_EQUAL:
          case SLAPI_OP_GREATER:
            lowerkey.dptr = realbuf;
            lowerkey.dsize = reallen;
            upperkey.dptr = nextrealbuf;
            upperkey.dsize = nextreallen;
            break;
        }
    }
    /* if (LDAP_DEBUG_FILTER)  {
        char encbuf [BUFSIZ];
        LDAPDebug( LDAP_DEBUG_FILTER, "   lowerkey=%s(%li bytes)\n",
              encoded (&lowerkey, encbuf), (long)lowerkey.dsize, 0 );
        LDAPDebug( LDAP_DEBUG_FILTER, "   upperkey=%s(%li bytes)\n",
              encoded (&upperkey, encbuf), (long)upperkey.dsize, 0 );
    } */
    data.flags = DB_DBT_MALLOC;
    lowerkey.flags = DB_DBT_MALLOC;
    {
        void *old_lower_key_data = lowerkey.data;
        *err = dbc->c_get(dbc,&lowerkey,&data,DB_SET_RANGE); /* lowerkey, if allocated and needs freed */
        DBT_FREE_PAYLOAD(data);
        if (old_lower_key_data != lowerkey.data) {
            slapi_ch_free(&old_lower_key_data);
        }
    }
    /* If the seek above fails due to DB_NOTFOUND, this means that there are no keys 
    which are >= the target key. This means that we should return no candidates */ 
    if (0 != *err) { 
        /* Free the key we just read above */
            DBT_FREE_PAYLOAD(lowerkey);
        if (DB_NOTFOUND == *err) { 
            *err = 0; 
            idl = NULL; 
        } else { 
            idl = idl_allids( be ); 
            ldbm_nasty(errmsg, 1080, *err);
            LDAPDebug( LDAP_DEBUG_ANY, 
                "<= index_range_read(%s,%s) allids (seek to lower key in index file err %i)\n", 
                type, prefix, *err ); 
        } 
        dbc->c_close(dbc); 
        goto error;
    }         
    /* We now close the cursor, since we're about to iterate over many keys */
    *err = dbc->c_close(dbc);

    /* step through the indexed db to retrive IDs within the search range */
    DBT_FREE_PAYLOAD(cur_key);
    cur_key.data = lowerkey.data;
    cur_key.size = lowerkey.size;
    lowerkey.data = NULL; /* Don't need this any more, since the memory will be freed from cur_key */
    if (operator == SLAPI_OP_GREATER) {
        *err = index_range_next_key(db,&cur_key,db_txn);
    }
    while (*err == 0 &&
           (operator == SLAPI_OP_LESS) ?
           DBTcmp(&cur_key, &upperkey, ai->ai_key_cmp_fn) < 0 :
           DBTcmp(&cur_key, &upperkey, ai->ai_key_cmp_fn) <= 0) {
        /* exit the loop when we either run off the end of the table,
         * fail to read a key, or read a key that's out of range.
         */
        IDList *tmp;
        /*
        char encbuf [BUFSIZ];
        LDAPDebug( LDAP_DEBUG_FILTER, "   cur_key=%s(%li bytes)\n",
               encoded (&cur_key, encbuf), (long)cur_key.dsize, 0 );
        */
        /* Check to see if we've already looked too hard */
        if (idl != NULL && lookthrough_limit != -1 && idl->b_nids > (ID)lookthrough_limit) {
            if (NULL != idl) {
                idl_free(idl);
            }
            idl = idl_allids( be );
            LDAPDebug(LDAP_DEBUG_TRACE, "index_range_read lookthrough_limit exceeded\n",
                                  0, 0, 0);
            break;
        }
        if (idl != NULL && sizelimit > 0 && idl->b_nids > (ID)sizelimit)
        {
            LDAPDebug(LDAP_DEBUG_TRACE, "index_range_read sizelimit exceeded\n",
                                  0, 0, 0);
            break;
        }
        /* check time limit */
        curtime = current_time();
        if ( timelimit != -1 && curtime >= stoptime )
        {
            LDAPDebug(LDAP_DEBUG_TRACE, "index_range_read timelimit exceeded\n",
                                  0, 0, 0);
            break;
        }

        /* Check to see if the operation has been abandoned (also happens
         * when the connection is closed by the client).
         */
        if ( slapi_op_abandoned( pb )) {
            if (NULL != idl) {
                idl_free(idl);
                idl = NULL;
            }
            LDAPDebug(LDAP_DEBUG_TRACE,
                    "index_range_read - operation abandoned\n", 0, 0, 0);
            break;    /* clean up happens outside the while() loop */
        }

        /* the cur_key DBT already has the first entry in it when we enter the loop */
        /* so we process the entry then step to the next one */
        cur_key.flags = 0;
        for (retry_count = 0; retry_count < IDL_FETCH_RETRY_COUNT; retry_count++) {
          *err = NEW_IDL_DEFAULT;
          tmp = idl_fetch_ext( be, db, &cur_key, NULL, ai, err, allidslimit );
          if(*err == DB_LOCK_DEADLOCK) {
            ldbm_nasty("index_range_read retrying transaction", 1090, *err);
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the fetch
#endif
            continue;
          } else {
            break;
          }
        }
        if(retry_count == IDL_FETCH_RETRY_COUNT) {
          ldbm_nasty("index_range_read retry count exceeded",1095,*err);
        }
        if (!tmp) {
            if (slapi_is_loglevel_set(LDAP_DEBUG_TRACE)) {
                char encbuf[BUFSIZ];
                LDAPDebug2Args(LDAP_DEBUG_TRACE,
                               "index_range_read_ext: cur_key=%s(%li bytes) was deleted - skipping\n",
                               encoded(&cur_key, encbuf), (long)cur_key.dsize);
            }
        } else {
            /* idl tmp only contains one id */
            /* append it at the end here; sort idlist at the end */
            if (ALLIDS(tmp)) {
                idl_free(idl);
                idl = tmp;
            } else {
                ID id;
                for (id = idl_firstid(tmp); id != NOID; id = idl_nextid(tmp, id)) {
                    *err = idl_append_extend(&idl, id);
                    if (*err) {
                        ldbm_nasty("index_range_read - failed to generate idlist",
                                   1097, *err);
                    }
                }
                idl_free(tmp);
            }
            if (ALLIDS(idl)) {
                LDAPDebug(LDAP_DEBUG_TRACE, "index_range_read hit an allids value\n",
                          0, 0, 0);
                break;
            }
        }
        if (DBT_EQ (&cur_key, &upperkey)) { /* this is the last key */
            break;
            /* Another c_get would return the same key, with no error. */
        }
        data.flags = DB_DBT_MALLOC;
        cur_key.flags = DB_DBT_MALLOC;
        *err = index_range_next_key(db,&cur_key,db_txn);
        /* *err = dbc->c_get(dbc,&cur_key,&data,DB_NEXT); */
        if (*err == DB_NOTFOUND) {
            *err = 0;
            break;
        }
    }
    if (*err) LDAPDebug( LDAP_DEBUG_FILTER, "   dbc->c_get(...DB_NEXT) == %i\n", *err, 0, 0);
#ifdef LDAP_DEBUG
        /* this is for debugging only */
        if (idl != NULL)
        {
            if (ALLIDS(idl)) {
                LDAPDebug( LDAP_DEBUG_FILTER,
                        "   idl=ALLIDS\n", 0, 0, 0 );
            } else {
                LDAPDebug( LDAP_DEBUG_FILTER,
                        "   idl->b_nids=%d\n", idl->b_nids, 0, 0 );
                LDAPDebug( LDAP_DEBUG_FILTER,
                        "   idl->b_nmax=%d\n", idl->b_nmax, 0, 0 );

                for ( i= 0; i< idl->b_nids; i++)
                {
                    LDAPDebug( LDAP_DEBUG_FILTER,
                                "   idl->b_ids[%d]=%d\n", i, idl->b_ids[i], 0);
                }
            }
        }
#endif
error:
    index_free_prefix(prefix);
    DBT_FREE_PAYLOAD(cur_key);
    DBT_FREE_PAYLOAD(upperkey);

    dblayer_release_index_file( be, ai, db );

    /* sort idl */
    if (idl && !ALLIDS(idl)) {
        qsort((void *)&idl->b_ids[0], idl->b_nids,
              (size_t)sizeof(ID), idl_sort_cmp);
    }

    LDAPDebug( LDAP_DEBUG_TRACE, "<= index_range_read(%s,%s) %lu candidates\n",
                   type, prefix, (u_long)IDL_NIDS(idl) );
    return( idl );
}

IDList *
index_range_read(
    Slapi_PBlock *pb,
    backend *be,
    char            *type,
    const char      *indextype,
    int             operator,
    struct berval   *val,
    struct berval   *nextval,
    int             range,
    back_txn        *txn,
    int             *err
)
{
    return index_range_read_ext(pb, be, type, indextype, operator, val, nextval, range, txn, err, 0);
}

/* DBDB: this function is never actually called */
#if 0
static int
addordel_values(
    backend *be,
    DB			*db,
    char		*type,
    const char		*indextype,
    struct berval	**vals,
    ID			id,
    int			flags,		/* BE_INDEX_ADD, etc */
    back_txn		*txn,
    struct attrinfo	*a,
	int *idl_disposition,
	void *buffer_handle
)
{
	int	rc = 0;
	int  i = 0;
	DBT	key = {0};
	DB_TXN	*db_txn = NULL;
	size_t	plen, vlen, len;
	char	*tmpbuf = NULL;
	size_t	tmpbuflen = 0;
	char	*realbuf;
	char	*prefix;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> %s_values\n",
		   (flags & BE_INDEX_ADD) ? "add" : "del", 0, 0);

	prefix = index_index2prefix( indextype );

	if (prefix == NULL) {
		LDAPDebug( LDAP_DEBUG_ANY, "<= %s_values: NULL prefix\n",
			(flags & BE_INDEX_ADD) ? "add" : "del", 0, 0 );
		return( -1 );
	}

	if ( vals == NULL ) {
		key.dptr  = prefix;
		key.dsize = strlen( prefix ) + 1; /* include null terminator */
		key.flags = DB_DBT_MALLOC;	
		if (NULL != txn) {
			db_txn = txn->back_txn_txn;
		}

		if (flags & BE_INDEX_ADD) {
			rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
		} else {
			rc = idl_delete_key( be, db, &key, id, db_txn, a );
			/* check for no such key/id - ok in some cases */
			if ( rc == DB_NOTFOUND || rc == -666 ) {
				rc = 0;
			}
		}

		if ( rc != 0)
		{
                        ldbm_nasty(errmsg, 1096, rc);
		}
		index_free_prefix (prefix);
		if (NULL != key.dptr && prefix != key.dptr)
			slapi_ch_free( (void**)&key.dptr );
		LDAPDebug( LDAP_DEBUG_TRACE, "<= %s_values %d\n",
			   (flags & BE_INDEX_ADD) ? "add" : "del", rc, 0 );
		return( rc );
	}

	plen = strlen( prefix );
	for ( i = 0; vals[i] != NULL; i++ ) {
		vlen = vals[i]->bv_len;
		len = plen + vlen;

		if ( len < tmpbuflen ) {
			realbuf = tmpbuf;
		} else {
			tmpbuf = slapi_ch_realloc( tmpbuf, len + 1 );
			tmpbuflen = len + 1;
			realbuf = tmpbuf;
		}

		memcpy( realbuf, prefix, plen );
		memcpy( realbuf+plen, vals[i]->bv_val, vlen );
		realbuf[len] = '\0';
		key.dptr = realbuf;
		key.size = plen + vlen + 1;
                /* should be okay to use USERMEM here because we know what
                 * the key is and it should never return a different value
                 * than the one we pass in.
                 */
		key.flags = DB_DBT_USERMEM;
                key.ulen = tmpbuflen;
#ifdef LDAP_DEBUG
		/* XXX if ( slapd_ldap_debug & LDAP_DEBUG_TRACE )  XXX */
		{
			char encbuf[BUFSIZ];

			LDAPDebug (LDAP_DEBUG_TRACE, "   %s_value(\"%s\")\n",
				   (flags & BE_INDEX_ADD) ? "add" : "del",
				   encoded (&key, encbuf), 0);
		}
#endif

		if (NULL != txn) {
			db_txn = txn->back_txn_txn;
		}

		if ( flags & BE_INDEX_ADD ) {
			if (buffer_handle) {
				rc = index_buffer_insert(buffer_handle,&key,id,be,db_txn,a);	
				if (rc == -2) {
					rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
				}
			} else {
				rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
			}
		} else {
			rc = idl_delete_key( be, db, &key, id, db_txn, a );
			/* check for no such key/id - ok in some cases */
			if ( rc == DB_NOTFOUND || rc == -666 ) {
				rc = 0;
			}
		}
		if ( rc != 0 ) {
                        ldbm_nasty(errmsg, 1100, rc);
			break;
		}
		if ( NULL != key.dptr && realbuf != key.dptr) {	/* realloc'ed */
			tmpbuf = key.dptr;
			tmpbuflen = key.size;
		}
	}
	index_free_prefix (prefix);
	if ( tmpbuf != NULL ) {
		slapi_ch_free( (void**)&tmpbuf );
	}

	if ( rc != 0 )
	{
            ldbm_nasty(errmsg, 1110, rc);
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= %s_values %d\n",
	    (flags & BE_INDEX_ADD) ? "add" : "del", rc, 0 );
	return( rc );
}
#endif

static int
addordel_values_sv(
    backend *be,
    DB			*db,
    char		*type,
    const char		*indextype,
    Slapi_Value 	**vals,
    ID			id,
    int			flags,		/* BE_INDEX_ADD, etc */
    back_txn		*txn,
    struct attrinfo	*a,
    int *idl_disposition,
    void *buffer_handle
)
{
    int	rc = 0;
    int  i = 0;
    DBT	key = {0};
    DB_TXN	*db_txn = NULL;
    size_t	plen, vlen, len;
    char	*tmpbuf = NULL;
    size_t	tmpbuflen = 0;
    char	*realbuf;
    char	*prefix = NULL;
    const struct berval *bvp;
    struct berval *encrypted_bvp = NULL;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> %s_values\n",
               (flags & BE_INDEX_ADD) ? "add" : "del", 0, 0);

    prefix = index_index2prefix( indextype );
    if (prefix == NULL) {
        LDAPDebug0Args( LDAP_DEBUG_ANY, "addordel_values_sv: NULL prefix\n" );
        return( -1 );
    }

    if ( vals == NULL ) {
        key.dptr  = prefix;
        key.dsize = strlen( prefix ) + 1; /* include null terminator */
        /* key could be read in idl_{insert,delete}_key. 
         * It must be DB_DBT_MALLOC. It's freed if key.dptr != prefix. */
        key.flags = DB_DBT_MALLOC;
        if (NULL != txn) {
            db_txn = txn->back_txn_txn;
        }

        if (flags & BE_INDEX_ADD) {
            rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
        } else {
            rc = idl_delete_key( be, db, &key, id, db_txn, a );
            /* check for no such key/id - ok in some cases */
            if ( rc == DB_NOTFOUND || rc == -666 ) {
                rc = 0;
            }
        }

        if ( rc != 0 ) {
            ldbm_nasty(errmsg, 1120, rc);
        }
        if (NULL != key.dptr && prefix != key.dptr) {
            slapi_ch_free( (void**)&key.dptr );
        }
        index_free_prefix (prefix);
        LDAPDebug( LDAP_DEBUG_TRACE, "<= %s_values %d\n",
                   (flags & BE_INDEX_ADD) ? "add" : "del", rc, 0 );
        return( rc );
    }

    plen = strlen( prefix );
    for ( i = 0; vals[i] != NULL; i++ ) {
        bvp = slapi_value_get_berval(vals[i]);

		/* Encrypt the index key if necessary */
		{
			if (a->ai_attrcrypt && (0 == (flags & BE_INDEX_DONT_ENCRYPT))) 
			{
				rc = attrcrypt_encrypt_index_key(be,a,bvp,&encrypted_bvp);
				if (rc) 
				{
					LDAPDebug (LDAP_DEBUG_ANY, "Failed to encrypt index key for %s\n", a->ai_type ,0,0);
				} else {
					bvp = encrypted_bvp;
				}
			}
		}

        vlen = bvp->bv_len;
        len = plen + vlen;

        if ( len < tmpbuflen ) {
            realbuf = tmpbuf;
        } else {
            tmpbuf = slapi_ch_realloc( tmpbuf, len + 1 );
            tmpbuflen = len + 1;
            realbuf = tmpbuf;
        }

        memcpy( realbuf, prefix, plen );
        memcpy( realbuf+plen, bvp->bv_val, vlen );
        realbuf[len] = '\0';
        key.dptr = realbuf;
        key.size = plen + vlen + 1;
		/* Free the encrypted berval if necessary */
		if (encrypted_bvp) 
		{
			ber_bvfree(encrypted_bvp);
			encrypted_bvp = NULL;
		}
        /* should be okay to use USERMEM here because we know what
         * the key is and it should never return a different value
         * than the one we pass in.
         */
        key.flags = DB_DBT_USERMEM;
        key.ulen = tmpbuflen;
#ifdef LDAP_DEBUG
        /* XXX if ( slapd_ldap_debug & LDAP_DEBUG_TRACE )  XXX */
        {
            char encbuf[BUFSIZ];

            LDAPDebug (LDAP_DEBUG_TRACE, "   %s_value(\"%s\")\n",
                       (flags & BE_INDEX_ADD) ? "add" : "del",
                       encoded (&key, encbuf), 0);
        }
#endif

        if (NULL != txn) {
            db_txn = txn->back_txn_txn;
        }

        if ( flags & BE_INDEX_ADD ) {
            if (buffer_handle) {
                rc = index_buffer_insert(buffer_handle,&key,id,be,db_txn,a);	
                if (rc == -2) {
                    rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
                }
            } else {
                rc = idl_insert_key( be, db, &key, id, db_txn, a, idl_disposition );
            }
        } else {
            rc = idl_delete_key( be, db, &key, id, db_txn, a );
            /* check for no such key/id - ok in some cases */
            if ( rc == DB_NOTFOUND || rc == -666 ) {
                rc = 0;
            }
        }
        if ( rc != 0 ) {
            ldbm_nasty(errmsg, 1130, rc);
            break;
        }
        if ( NULL != key.dptr && realbuf != key.dptr) {	/* realloc'ed */
            tmpbuf = key.dptr;
            tmpbuflen = key.size;
        }
    }
    index_free_prefix (prefix);
    if ( tmpbuf != NULL ) {
        slapi_ch_free( (void**)&tmpbuf );
    }

    if ( rc != 0 )
    {
        ldbm_nasty(errmsg, 1140, rc);
    }
    LDAPDebug( LDAP_DEBUG_TRACE, "<= %s_values %d\n",
               (flags & BE_INDEX_ADD) ? "add" : "del", rc, 0 );
    return( rc );
}

int
index_addordel_string(backend *be, const char *type, const char *s, ID id, int flags, back_txn *txn)
{
    Slapi_Value *svp[2];
    Slapi_Value sv;

    memset(&sv,0,sizeof(Slapi_Value));
    sv.bv.bv_len= strlen(s);
    sv.bv.bv_val= (void*)s;
    svp[0] = &sv;
    svp[1] = NULL;
    if (flags & BE_INDEX_NORMALIZED)
        slapi_value_set_flags(&sv, BE_INDEX_NORMALIZED);
    return index_addordel_values_ext_sv(be,type,svp,NULL,id,flags,txn,NULL,NULL);
}

int
index_addordel_values_sv(
    backend *be,
    const char		*type,
    Slapi_Value	**vals,
    Slapi_Value	**evals,	/* existing values */
    ID			id,
    int			flags,
    back_txn		*txn
)
{
    return index_addordel_values_ext_sv(be,type,vals,evals,
                                        id,flags,txn,NULL,NULL);
}

int
index_addordel_values_ext_sv(
    backend *be,
    const char		*type,
    Slapi_Value 	**vals,
    Slapi_Value 	**evals,
    ID			id,
    int			flags,
    back_txn		*txn,
    int *idl_disposition,
    void *buffer_handle
)
{
    DB		*db;
    struct attrinfo	*ai = NULL;
    int		err = -1;
    Slapi_Value	**ivals;
    char	buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    char	*basetmp, *basetype;
    
    LDAPDebug( LDAP_DEBUG_TRACE,
               "=> index_addordel_values_ext_sv( \"%s\", %lu )\n", type, (u_long)id, 0 );

    basetype = buf;
    if ( (basetmp = slapi_attr_basetype( type, buf, sizeof(buf) ))
         != NULL ) {
        basetype = basetmp;
    }

    ainfo_get( be, basetype, &ai );
    if ( ai == NULL || ai->ai_indexmask == 0
				|| ai->ai_indexmask == INDEX_OFFLINE ) {
		slapi_ch_free_string( &basetmp );
        return( 0 );
    }
    LDAPDebug( LDAP_DEBUG_ARGS, "   index_addordel_values_ext_sv indexmask 0x%x\n",
               ai->ai_indexmask, 0, 0 );
    if ( (err = dblayer_get_index_file( be, ai, &db, DBOPEN_CREATE )) != 0 ) {
        LDAPDebug( LDAP_DEBUG_ANY,
                   "<= index_read NULL (could not open index attr %s)\n",
                   basetype, 0, 0 );
		slapi_ch_free_string( &basetmp );
        if ( err != 0 ) {
            ldbm_nasty(errmsg, 1210, err);
        }
        goto bad;
    }

    /*
     * presence index entry
     */
    if (( ai->ai_indexmask & INDEX_PRESENCE ) &&
        (flags & (BE_INDEX_ADD|BE_INDEX_PRESENCE))) {
        /* on delete, only remove the presence index if the 
         * BE_INDEX_PRESENCE flag is set.
         */
        err = addordel_values_sv( be, db, basetype, indextype_PRESENCE,
                                  NULL, id, flags, txn, ai, idl_disposition, NULL );
        if ( err != 0 ) {
            ldbm_nasty(errmsg, 1220, err);
            goto bad;
        }
    }

    /*
     * equality index entry
     */
    if (( ai->ai_indexmask & INDEX_EQUALITY ) &&
        (flags & (BE_INDEX_ADD|BE_INDEX_EQUALITY))) {
        /* on delete, only remove the equality index if the
         * BE_INDEX_EQUALITY flag is set.
         */
        slapi_attr_values2keys_sv( &ai->ai_sattr, vals, &ivals, LDAP_FILTER_EQUALITY );

        err = addordel_values_sv( be, db, basetype, indextype_EQUALITY,
                                  ivals != NULL ? ivals : vals, id, flags, txn, ai, idl_disposition, NULL );
        if ( ivals != NULL ) {
            valuearray_free( &ivals );
        }
        if ( err != 0 ) {
            ldbm_nasty(errmsg, 1230, err);
            goto bad;
        }
    }

    /*
     * approximate index entry
     */
    if ( ai->ai_indexmask & INDEX_APPROX ) {
        slapi_attr_values2keys_sv( &ai->ai_sattr, vals, &ivals, LDAP_FILTER_APPROX );

        if ( ivals != NULL ) {
            err = addordel_values_sv( be, db, basetype,
                                      indextype_APPROX, ivals, id, flags, txn, ai, idl_disposition, NULL );
            valuearray_free( &ivals );
            if ( err != 0 ) {
                ldbm_nasty(errmsg, 1240, err);
                goto bad;
            }
        }
    }

    /*
     * substrings index entry
     */
    if ( ai->ai_indexmask & INDEX_SUB ) {
        Slapi_Value	**esubvals = NULL;
        Slapi_Value	**substresult = NULL;
        Slapi_Value   **origvals = NULL;
		Slapi_PBlock		pipb;

		/* prepare pblock to pass ai_substr_lens */
		pblock_init( &pipb );
		slapi_pblock_set( &pipb, SLAPI_SYNTAX_SUBSTRLENS, ai->ai_substr_lens );
        slapi_attr_values2keys_sv_pb( &ai->ai_sattr, vals, &ivals, 
                                          LDAP_FILTER_SUBSTRINGS, &pipb );

        origvals = ivals;
        /* delete only: if the attribute has multiple values,
         * figure out the substrings that should remain
         * by slapi_attr_values2keys,
         * then get rid of them from the being deleted values
         */
        if ( evals != NULL ) {
            slapi_attr_values2keys_sv_pb( &ai->ai_sattr, evals,
							&esubvals, LDAP_FILTER_SUBSTRINGS, &pipb );
            substresult = valuearray_minus_valuearray( &ai->ai_sattr, ivals, esubvals );
            ivals = substresult;
            valuearray_free( &esubvals );
        }
        if ( ivals != NULL ) {
            err = addordel_values_sv( be, db, basetype, indextype_SUB,
                                      ivals, id, flags, txn, ai, idl_disposition, buffer_handle );
            if ( ivals != origvals )
                valuearray_free( &origvals );
            valuearray_free( &ivals );
            if ( err != 0 ) {
                ldbm_nasty(errmsg, 1250, err);
                goto bad;
            }

            ivals = NULL;
        }
    }

    /*
     * matching rule index entries
     */
    if ( ai->ai_indexmask & INDEX_RULES )
    {
        Slapi_PBlock* pb = slapi_pblock_new();
        char** oid = ai->ai_index_rules;
        for (; *oid != NULL; ++oid)
        {
            if(create_matchrule_indexer(&pb,*oid,basetype)==0)
            {
                char* officialOID = NULL;
                if (!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &officialOID) && officialOID != NULL)
                {
                    Slapi_Value** keys = NULL;
                    matchrule_values_to_keys_sv(pb,vals,&keys);
                    /* the matching rule indexer owns keys now */
                    if(keys != NULL && keys[0] != NULL)
            	    {
            	        /* we've computed keys */
                        err = addordel_values_sv (be, db, basetype, officialOID, keys, id, flags, txn, ai, idl_disposition, NULL);
                        if ( err != 0 )
                        {
                            ldbm_nasty(errmsg, 1260, err);
                        }
                    }
                    /*
                     * It would improve speed to save the indexer, for future use.
                     * But, for simplicity, we destroy it now:
                     */
                    /* this will also free keys */
                    destroy_matchrule_indexer(pb);
                    if ( err != 0 ) {
                        goto bad;
                    }
                }
            }
        }
        slapi_pblock_destroy (pb);
    }

    dblayer_release_index_file( be, ai, db );
    if ( basetmp != NULL ) {
        slapi_ch_free( (void**)&basetmp );
    }

    LDAPDebug (LDAP_DEBUG_TRACE, "<= index_addordel_values_ext_sv\n", 0, 0, 0 );
    return( 0 );

 bad:
    dblayer_release_index_file(be, ai, db);
    return err;
}

int
index_delete_values(
    struct ldbminfo	*li,
    char		*type,
    struct berval	**vals,
    ID			id
)
{
	return -1;
}

static int
is_indexed (const char* indextype, int indexmask, char** index_rules)
{
    int indexed;
    if      (indextype == indextype_PRESENCE) indexed = INDEX_PRESENCE & indexmask;
    else if (indextype == indextype_EQUALITY) indexed = INDEX_EQUALITY & indexmask;
    else if (indextype == indextype_APPROX)   indexed = INDEX_APPROX   & indexmask;
    else if (indextype == indextype_SUB)      indexed = INDEX_SUB      & indexmask;
    else { /* matching rule */
	indexed = 0;
	if (INDEX_RULES & indexmask) {
	    char** rule;
	    for (rule = index_rules; *rule; ++rule) {
		if ( ! strcmp( *rule, indextype )) {
		    indexed = INDEX_RULES;
		    break;
		}
	    }
	}
    }

    /* if index is currently being generated, pretend it doesn't exist */
    if (indexmask & INDEX_OFFLINE)
        indexed = 0;

    return indexed;
}

char*
index_index2prefix (const char* indextype)
{
    char* prefix;
    if      ( indextype == NULL ) prefix = NULL;
    else if ( indextype == indextype_PRESENCE ) prefix = prefix_PRESENCE;
    else if ( indextype == indextype_EQUALITY ) prefix = prefix_EQUALITY;
    else if ( indextype == indextype_APPROX   ) prefix = prefix_APPROX;
    else if ( indextype == indextype_SUB      ) prefix = prefix_SUB;
    else { /* indextype is a matching rule name */
	const size_t len = strlen (indextype);
	char* p = slapi_ch_malloc (len + 3);
	p[0] = RULE_PREFIX;
	memcpy( p+1, indextype, len );
	p[len+1] = ':';
	p[len+2] = '\0';
	prefix = p;
    }
    return( prefix );
}

void
index_free_prefix (char* prefix)
{
    if (prefix == NULL ||
	prefix == prefix_PRESENCE ||
	prefix == prefix_EQUALITY ||
	prefix == prefix_APPROX ||
	prefix == prefix_SUB) {
	/* do nothing */
    } else {
	slapi_ch_free( (void**)&prefix);
    }
}

/* helper stuff for valuearray_minus_valuearray */

typedef struct {
    value_compare_fn_type cmp_fn;
    Slapi_Value *data;
} SVSORT;

static int 
svsort_cmp(const void *x, const void *y)
{
    return ((SVSORT*)x)->cmp_fn(slapi_value_get_berval(((SVSORT*)x)->data), 
                                slapi_value_get_berval(((SVSORT*)y)->data));
}

static int
bvals_strcasecmp(const struct berval *a, const struct berval *b)
{
    return strcasecmp(a->bv_val, b->bv_val);
}

/* a - b = c */
/* the returned array of Slapi_Value needs to be freed. */
static Slapi_Value **
valuearray_minus_valuearray(
    const Slapi_Attr *sattr, 
    Slapi_Value **a, 
    Slapi_Value **b
)
{
    int rc, i, j, k, acnt, bcnt;
    SVSORT *atmp = NULL, *btmp = NULL;
    Slapi_Value **c;
    value_compare_fn_type cmp_fn;

    /* get berval comparison function */
    attr_get_value_cmp_fn(sattr, &cmp_fn);
    if (cmp_fn == NULL) {
        cmp_fn = (value_compare_fn_type)bvals_strcasecmp;
    }

    /* determine length of a */
    for (acnt = 0; a && a[acnt] != NULL; acnt++);

    /* determine length of b */
    for (bcnt = 0; b && b[bcnt] != NULL; bcnt++);

    /* allocate return array as big as a */
    c = (Slapi_Value**)slapi_ch_calloc(acnt+1, sizeof(Slapi_Value*));
    if (acnt == 0) return c;

    /* sort a */
    atmp = (SVSORT*) slapi_ch_malloc(acnt*sizeof(SVSORT));
    for (i = 0; i < acnt; i++) {
        atmp[i].cmp_fn = cmp_fn;
        atmp[i].data = a[i];
    }
    qsort((void*)atmp, acnt, (size_t)sizeof(SVSORT), svsort_cmp);

    /* sort b */
    if (bcnt > 0) {
        btmp = (SVSORT*) slapi_ch_malloc(bcnt*sizeof(SVSORT));
        for (i = 0; i < bcnt; i++) {
            btmp[i].cmp_fn = cmp_fn;
            btmp[i].data = b[i];
        }
        qsort((void*)btmp, bcnt, (size_t)sizeof(SVSORT), svsort_cmp);
    }

    /* lock step through a and b */
    for (i = 0, j = 0, k = 0; i < acnt && j < bcnt; ) {
        rc = svsort_cmp(&atmp[i], &btmp[j]);
        if (rc == 0) {
            i++;
        } else if (rc < 0) {
            c[k++] = slapi_value_new_value(atmp[i++].data);
        } else {
            j++;
        }
    }

    /* copy what's left from a */
    while (i < acnt) {
        c[k++] = slapi_value_new_value(atmp[i++].data);
    }

    /* clean up */
    slapi_ch_free((void**)&atmp);
    if (btmp) slapi_ch_free((void**)&btmp);

    return c;
}
