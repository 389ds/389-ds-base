/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* index.c - routines for dealing with attribute indexes */

#include "back-ldbm.h"

static const char *errmsg = "database index operation failed";

static int is_indexed(const char *indextype, int indexmask, char **index_rules);
static int index_get_allids(int *allids, const char *indextype, struct attrinfo *ai, const struct berval *val, unsigned int flags);

static Slapi_Value **
valuearray_minus_valuearray(
    const Slapi_Attr *sattr,
    Slapi_Value **a,
    Slapi_Value **b);

const char *indextype_PRESENCE = "pres";
const char *indextype_EQUALITY = "eq";
const char *indextype_APPROX = "approx";
const char *indextype_SUB = "sub";

static char prefix_PRESENCE[2] = {PRES_PREFIX, 0};
static char prefix_EQUALITY[2] = {EQ_PREFIX, 0};
static char prefix_APPROX[2] = {APPROX_PREFIX, 0};
static char prefix_SUB[2] = {SUB_PREFIX, 0};

/* Yes, prefix_PRESENCE and prefix_SUB are identical.
 * It works because SUB is always followed by a key value,
 * but PRESENCE never is.  Too slick by half.
 */


/* Structures for index key buffering magic used by import code */
struct _index_buffer_bin
{
    dbi_val_t key;
    IDList *value;
};
typedef struct _index_buffer_bin index_buffer_bin;

struct _index_buffer_handle
{
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
                           unsigned char high_key_byte_range,
                           unsigned char low_key_byte_range,
                           size_t max_key_length,
                           unsigned char special_byte_a,
                           unsigned char special_byte_b,
                           int flags,
                           void **h)
{
    size_t bin_count = 0;
    /* Allocate the handle */
    index_buffer_bin *bins = NULL;
    size_t i = 0;
    size_t byte_range = 0;
    int rc = 0;

    index_buffer_handle *handle = (index_buffer_handle *)slapi_ch_calloc(1, sizeof(index_buffer_handle));
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
    for (i = 0; i < max_key_length - 2; i++) {
        bin_count *= byte_range;
    }
    handle->buffer_size = bin_count;
    bins = (index_buffer_bin *)slapi_ch_calloc(bin_count, sizeof(index_buffer_bin));
    if (NULL == bins) {
        rc = -1;
        goto error;
    }
    handle->bins = bins;
    *h = (void *)handle;
    goto done;

error:
    slapi_ch_free((void **)&handle);

done:
    return rc;
}

int
index_buffer_init(size_t size, int flags, void **h)
{
    return index_buffer_init_internal(size, 'z', 'a', 5, '^', '$', flags, h);
}

static int
index_put_idl(index_buffer_bin *bin, backend *be, dbi_txn_t *txn, struct attrinfo *a)
{
    int ret = 0;
    dbi_db_t *db = NULL;
    int need_to_freed_new_idl = 0;
    IDList *old_idl = NULL;
    IDList *new_idl = NULL;

    if ((ret = dblayer_get_index_file(be, a, &db, DBOPEN_CREATE)) != 0) {
        return ret;
    }
    if (bin->key.data && bin->value) {
        /* Need to read the IDL at the key, if present, and form the union with what we have */
        ret = NEW_IDL_NOOP; /* this flag is for new idl only;
                             * but this func is called only from index_buffer,
                             * which is enabled only for old idl.
                             */
        old_idl = idl_fetch(be, db, &bin->key, txn, a, &ret);
        if ((0 != ret) && (DBI_RC_NOTFOUND != ret)) {
            goto error;
        }
        if ((old_idl != NULL) && !ALLIDS(old_idl)) {
            /* We need to merge in our block with what was there */
            new_idl = idl_union(be, old_idl, bin->value);
            need_to_freed_new_idl = 1;
        } else {
            /* Nothing there previously, we store just what we have */
            new_idl = bin->value;
        }
        /* Then write back the result, but only if the existing idl wasn't ALLIDS */
        if (!old_idl || (old_idl && !ALLIDS(old_idl))) {
            ret = idl_store_block(be, db, &bin->key, new_idl, txn, a);
        }
        if (0 != ret) {
            goto error;
        }
        dblayer_value_free(be, &bin->key);
        idl_free(&(bin->value));
        /* If we're already at allids, store an allids block to prevent needless accumulation of blocks */
        if (old_idl && ALLIDS(old_idl)) {
            bin->value = idl_allids(be);
        } else {
            bin->value = NULL;
        }
    }
error:
    if (old_idl) {
        idl_free(&old_idl);
    }
    if (new_idl && need_to_freed_new_idl) {
        idl_free(&new_idl);
    }
    dblayer_release_index_file(be, a, db);
    return ret;
}

/* The caller MUST check for DBI_RC_RUNRECOVERY being returned */

int
index_buffer_flush(void *h, backend *be, dbi_txn_t *txn, struct attrinfo *a)
{
    index_buffer_handle *handle = (index_buffer_handle *)h;
    index_buffer_bin *bin = NULL;
    int ret = 0;
    size_t i = 0;
    dbi_db_t *db = NULL;

    PR_ASSERT(h);

    /* Note to the wary: here we do NOT create the index file up front */
    /* This is becuase there may be no buffers to flush, and the goal is to
     * never create the index file (merging gets confused by this, among other things */

    /* Walk along the bins, writing them to the database */
    for (i = 0; i < handle->buffer_size; i++) {
        bin = &(handle->bins[i]);
        if (bin->key.data && bin->value) {
            if (NULL == db) {
                if ((ret = dblayer_get_index_file(be, a, &db, DBOPEN_CREATE)) != 0) {
                    return ret;
                }
            }
            ret = index_put_idl(bin, be, txn, a);
            if (0 != ret) {
                goto error;
            }
        }
    }
error:
    if (NULL != db) {
        dblayer_release_index_file(be, a, db);
    }
    return ret;
}

int
index_buffer_terminate(backend *be, void *h)
{
    index_buffer_handle *handle = (index_buffer_handle *)h;
    index_buffer_bin *bin = NULL;
    size_t i = 0;

    PR_ASSERT(h);
    /* Free all the buffers */
    /* First walk down the bins, freeing the IDLs and the bins they're in */
    for (i = 0; i < handle->buffer_size; i++) {
        bin = &(handle->bins[i]);
        if (bin->value) {
            idl_free(&(bin->value));
            bin->value = NULL;
        }
        dblayer_value_free(be, &bin->key);
    }
    slapi_ch_free((void **)&(handle->bins));
    /* Now free the handle */
    slapi_ch_free((void **)&handle);
    return 0;
}

/* This function returns -1 or -2 for local errors, and DB_ errors as well. */

static int
index_buffer_insert(void *h, dbi_val_t *key, ID id, backend *be, dbi_txn_t *txn, struct attrinfo *a)
{
    index_buffer_handle *handle = (index_buffer_handle *)h;
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
    if ((unsigned char)((char *)key->data)[0] != SUB_PREFIX) {
        return -2;
    }
    /* Compute the bin index from the key */
    /* Walk along the key data, byte by byte */
    for (i = 1; i < (key->size - 1); i++) {
        /* foreach byte, normalize to the range we accept */
        x = (unsigned char)((char *)key->data)[i];
        if ((x == handle->special_byte_a) || (x == handle->special_byte_b)) {
            if (x == handle->special_byte_a) {
                x = handle->high_key_byte_range + 1;
            }
            if (x == handle->special_byte_b) {
                x = handle->high_key_byte_range + 2;
            }
        } else {
            if (x >= '0' && x <= '9') {
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
    if (0 != (unsigned char)((char *)key->data)[i]) {
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
        memcpy(bin->key.data, key->data, key->size);
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
            /* So, we need to write it out to the dbi_db_t and zero out the pointers */
            ret = index_put_idl(bin, be, txn, a);
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
    struct backentry *e,
    int flags,
    back_txn *txn)
{
    char *type = NULL;
    Slapi_Value **svals;
    int rc, result;
    Slapi_Attr *attr;

    slapi_log_err(SLAPI_LOG_TRACE, "index_addordel_entry", "=> %s ( \"%s\", %lu )\n",
                  (flags & BE_INDEX_ADD) ? "add" : "del",
                  backentry_get_ndn(e), (u_long)e->ep_id);

    /* if we are adding a tombstone entry (see ldbm_add.c) */
    if ((flags & BE_INDEX_TOMBSTONE) && (flags & BE_INDEX_ADD)) {
        const CSN *tombstone_csn = NULL;
        char deletion_csn_str[CSN_STRSIZE];
        char *entryusn_str;
        Slapi_DN parent;
        Slapi_DN *sdn = slapi_entry_get_sdn(e->ep_entry);

        slapi_sdn_init(&parent);
        slapi_sdn_get_parent(sdn, &parent);
        /*
         * Just index the "nstombstone" attribute value from the objectclass
         * attribute, and the nsuniqueid attribute value, and the
         * nscpEntryDN value of the deleted entry.
         *
         * 2017 - add entryusn to this to improve entryusn tombstone tasks.
         */
        result = index_addordel_string(be, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE, e->ep_id, flags, txn);
        if (result != 0) {
            ldbm_nasty("index_addordel_entry", errmsg, 1010, result);
            return (result);
        }
        result = index_addordel_string(be, SLAPI_ATTR_UNIQUEID, slapi_entry_get_uniqueid(e->ep_entry), e->ep_id, flags, txn);
        if (result != 0) {
            ldbm_nasty("index_addordel_entry", errmsg, 1020, result);
            return (result);
        }
        result = index_addordel_string(be, SLAPI_ATTR_NSCP_ENTRYDN, slapi_sdn_get_ndn(&parent), e->ep_id, flags, txn);
        if (result != 0) {
            ldbm_nasty("index_addordel_entry", errmsg, 1021, result);
            return (result);
        }

        if ((tombstone_csn = entry_get_deletion_csn(e->ep_entry))) {
            csn_as_string(tombstone_csn, PR_FALSE, deletion_csn_str);
            result = index_addordel_string(be, SLAPI_ATTR_TOMBSTONE_CSN, deletion_csn_str, e->ep_id, flags, txn);
            if (result != 0) {
                ldbm_nasty("index_addordel_entry", errmsg, 1021, result);
                return (result);
            }
        }

        /*
         * add the entryusn to the list of indexed attributes, even if it's a tombstone
         */
        entryusn_str = (char *)slapi_entry_attr_get_ref(e->ep_entry, SLAPI_ATTR_ENTRYUSN);
        if (entryusn_str != NULL) {
            result = index_addordel_string(be, SLAPI_ATTR_ENTRYUSN, entryusn_str, e->ep_id, flags, txn);
            if (result != 0) {
                ldbm_nasty("index_addordel_entry", errmsg, 1021, result);
                return result;
            }
        }

        slapi_sdn_done(&parent);
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            Slapi_Attr *attr;
            /* Even if this is a tombstone, we have to add it to entryrdn
             * to maintain the full DN
             */
            result = entryrdn_index_entry(be, e, flags, txn);
            if (result != 0) {
                ldbm_nasty("index_addordel_entry", errmsg, 1023, result);
                return (result);
            }
            /* To maintain tombstonenumsubordinates,
             * parentid is needed for tombstone, as well. */
            slapi_entry_attr_find(e->ep_entry, LDBM_PARENTID_STR, &attr);
            if (attr) {
                svals = attr_get_present_values(attr);
                result = index_addordel_values_sv(be, LDBM_PARENTID_STR, svals, NULL,
                                                  e->ep_id, flags, txn);
                if (result != 0) {
                    ldbm_nasty("index_addordel_entry", errmsg, 1022, result);
                    return (result);
                }
            }
        }
    } else { /* NOT a tombstone or delete a tombstone */
        /* add each attribute to the indexes */
        rc = 0, result = 0;
        int entryrdn_done = 0;
        for (rc = slapi_entry_first_attr(e->ep_entry, &attr); rc == 0;
             rc = slapi_entry_next_attr(e->ep_entry, attr, &attr)) {
            slapi_attr_get_type(attr, &type);
            svals = attr_get_present_values(attr);
            if (!entryrdn_done && (0 == strcmp(type, LDBM_ENTRYDN_STR))) {
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
            result = index_addordel_values_sv(be, type, svals, NULL,
                                              e->ep_id, flags, txn);
            if (result != 0) {
                ldbm_nasty("index_addordel_entry", errmsg, 1030, result);
                return (result);
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
                if (result != 0) {
                    return (result);
                }
            }
        }
        if (entryrdn_get_switch()) { /* subtree-rename: on */
            result = entryrdn_index_entry(be, e, flags, txn);
            if (result != 0) {
                ldbm_nasty("index_addordel_entry", errmsg, 1031, result);
                return (result);
            }
        }
    }

    slapi_log_err(SLAPI_LOG_TRACE, "<= index_addordel_entry", "%s %s %d\n",
                  (flags & BE_INDEX_ADD) ? "add" : "del",
                  (flags & BE_INDEX_TOMBSTONE) ? " (tombstone)" : "", result);
    return (result);
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
    struct backentry *olde,
    struct backentry *newe,
    back_txn *txn)
{
    int rc = 0;
    int i, j;
    ID id = olde->ep_id;
    int flags = 0;
    char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    char *basetype = NULL;
    char *tmp = NULL;
    Slapi_Attr *curr_attr = NULL;
    struct attrinfo *ai = NULL;
    Slapi_ValueSet *all_vals = NULL;
    Slapi_ValueSet *mod_vals = NULL;
    Slapi_Value **evals = NULL;              /* values that still exist after a
                                               * delete.
                                               */
    Slapi_Value **mods_valueArray = NULL;    /* values that are specified in this
                                               * operation.
                                               */
    Slapi_Value **deleted_valueArray = NULL; /* values whose index entries
                                               * should be deleted.
                                               */

    for (i = 0; mods && mods[i] != NULL; i++) {
        /* Get base attribute type */
        basetype = buf;
        tmp = slapi_attr_basetype(mods[i]->mod_type, buf, sizeof(buf));
        if (tmp != NULL) {
            basetype = tmp; /* basetype was malloc'd */
        }
        ainfo_get(be, basetype, &ai);
        if (ai == NULL || ai->ai_indexmask == 0 || ai->ai_indexmask == INDEX_OFFLINE) {
            /* this attribute is not being indexed, skip it. */
            goto error;
        }

        /* Get a list of all remaining values for the base type
         * and any present subtypes.
         */
        all_vals = slapi_valueset_new();

        for (curr_attr = newe->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
            if (slapi_attr_type_cmp(basetype, curr_attr->a_type, SLAPI_TYPE_CMP_BASE) == 0) {
                slapi_valueset_join_attr_valueset(curr_attr, all_vals, &curr_attr->a_present_values);
            }
        }

        evals = valueset_get_valuearray(all_vals);

        /* Get a list of all values specified in the operation.
         */
        if (mods[i]->mod_bvalues != NULL) {
            valuearray_init_bervalarray(mods[i]->mod_bvalues, &mods_valueArray);
        }

        switch (mods[i]->mod_op & ~LDAP_MOD_BVALUES) {
        case LDAP_MOD_REPLACE:
            flags = BE_INDEX_DEL;
            /* Get a list of all values being deleted.
             */
            mod_vals = slapi_valueset_new();

            for (curr_attr = olde->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
                if (slapi_attr_type_cmp(mods[i]->mod_type, curr_attr->a_type, SLAPI_TYPE_CMP_EXACT) == 0) {
                    slapi_valueset_join_attr_valueset(curr_attr, mod_vals, &curr_attr->a_present_values);
                }
            }

            deleted_valueArray = valueset_get_valuearray(mod_vals);

            /* If subtypes exist, don't remove the presence
             * index.
             */
            if (evals != NULL && deleted_valueArray != NULL) {
                /* evals will contain the new value that is being
                 * added as part of the replace operation if one
                 * was specified.  We must remove this value from
                 * evals to know if any subtypes are present.
                 */
                slapi_entry_attr_find(olde->ep_entry, mods[i]->mod_type, &curr_attr);
                if (mods_valueArray != NULL) {
                    for (j = 0; mods_valueArray[j] != NULL; j++) {
                        Slapi_Value *rval = valueset_remove_value(curr_attr, all_vals, mods_valueArray[j]);
                        slapi_value_free(&rval);
                    }
                }

                /* Search evals for the values being deleted.  If
                 * they don't exist, delete the equality index.
                 */
                for (j = 0; deleted_valueArray[j] != NULL; j++) {
                    if (!slapi_valueset_find(curr_attr, all_vals, deleted_valueArray[j])) {
                        if (!(flags & BE_INDEX_EQUALITY)) {
                            flags |= BE_INDEX_EQUALITY;
                        }
                    } else {
                        Slapi_Value *rval = valueset_remove_value(curr_attr, mod_vals, deleted_valueArray[j]);
                        slapi_value_free(&rval);
                        j--;
                        /* indicates there was some conflict */
                        mods[i]->mod_op |= LDAP_MOD_IGNORE;
                    }
                }
            } else {
                flags |= BE_INDEX_PRESENCE | BE_INDEX_EQUALITY;
            }

            /* We need to first remove the old values from the
             * index, if any. */
            if (deleted_valueArray) {
                rc = index_addordel_values_sv(be, mods[i]->mod_type,
                                              deleted_valueArray, evals, id,
                                              flags, txn);
                if (rc) {
                    ldbm_nasty("index_add_mods", errmsg, 1041, rc);
                    goto error;
                }
            }

            /* Free valuearray */
            slapi_valueset_free(mod_vals);
            mod_vals = NULL;
        case LDAP_MOD_ADD:
            if (mods_valueArray == NULL) {
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
                        if (!slapi_valueset_find(curr_attr, &curr_attr->a_present_values,
                                                 mods_valueArray[j])) {
                            /* The value is NOT in newe, remove it. */
                            Slapi_Value *rval;
                            rval = valuearray_remove_value(curr_attr,
                                                           mods_valueArray,
                                                           mods_valueArray[j]);
                            slapi_value_free(&rval);
                            /* indicates there was some conflict */
                            mods[i]->mod_op |= LDAP_MOD_IGNORE;
                        }
                    }
                    if (mods_valueArray[0]) {
                        rc = index_addordel_values_sv(be, mods[i]->mod_type,
                                                      mods_valueArray, NULL,
                                                      id, BE_INDEX_ADD, txn);
                    } else {
                        rc = 0;
                    }
                    if (rc) {
                        ldbm_nasty("index_add_mods", errmsg, 1042, rc);
                        goto error;
                    }
                }
            }
            break;

        case LDAP_MOD_DELETE:
            if ((mods[i]->mod_bvalues == NULL) ||
                (mods[i]->mod_bvalues[0] == NULL)) {
                rc = 0;
                flags = BE_INDEX_DEL;

                /* Get a list of all values that are being
                 * deleted.
                 */
                mod_vals = slapi_valueset_new();

                for (curr_attr = olde->ep_entry->e_attrs; curr_attr != NULL; curr_attr = curr_attr->a_next) {
                    if (slapi_attr_type_cmp(mods[i]->mod_type, curr_attr->a_type, SLAPI_TYPE_CMP_EXACT) == 0) {
                        slapi_valueset_join_attr_valueset(curr_attr, mod_vals, &curr_attr->a_present_values);
                    }
                }

                deleted_valueArray = valueset_get_valuearray(mod_vals);

                /* If subtypes exist, don't remove the
                 * presence index.
                 */
                if (evals != NULL) {
                    for (curr_attr = newe->ep_entry->e_attrs; (curr_attr != NULL);
                         curr_attr = curr_attr->a_next) {
                        if (slapi_attr_type_cmp(basetype, curr_attr->a_type, SLAPI_TYPE_CMP_BASE) == 0) {
                            /* Check if the any values being deleted
                             * also exist in a subtype.
                             */
                            for (j = 0; deleted_valueArray && deleted_valueArray[j]; j++) {
                                if (!slapi_valueset_find(curr_attr, all_vals, deleted_valueArray[j])) {
                                    /* If the equality flag isn't already set, set it */
                                    if (!(flags & BE_INDEX_EQUALITY)) {
                                        flags |= BE_INDEX_EQUALITY;
                                    }
                                } else {
                                    /* Remove duplicate value from the mod list */
                                    Slapi_Value *rval = valueset_remove_value(curr_attr, mod_vals, deleted_valueArray[j]);
                                    slapi_value_free(&rval);
                                    j--;
                                }
                            }
                        }
                    }
                } else {
                    flags = BE_INDEX_DEL | BE_INDEX_PRESENCE | BE_INDEX_EQUALITY;
                }

                /* Update the index, if necessary */
                if (deleted_valueArray) {
                    rc = index_addordel_values_sv(be, mods[i]->mod_type,
                                                  deleted_valueArray, evals, id,
                                                  flags, txn);
                    if (rc) {
                        ldbm_nasty("index_add_mods", errmsg, 1043, rc);
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
                    flags = BE_INDEX_DEL | BE_INDEX_PRESENCE | BE_INDEX_EQUALITY;
                } else {
                    flags = BE_INDEX_DEL;
                    curr_attr = NULL;
                    slapi_entry_attr_find(olde->ep_entry,
                                          mods[i]->mod_type,
                                          &curr_attr);
                    if (curr_attr) {
                        for (j = 0; mods_valueArray && mods_valueArray[j] != NULL; j++) {
                            if (!slapi_valueset_find(curr_attr, all_vals, mods_valueArray[j])) {
                                /*
                                 * If the mod del value is not found in all_vals
                                 * we need to update the equality index as the
                                 * final value(s) have changed
                                 */
                                if (!(flags & BE_INDEX_EQUALITY)) {
                                    flags |= BE_INDEX_EQUALITY;
                                }
                                break;
                            }
                        }
                    }
                }

                rc = index_addordel_values_sv(be, basetype,
                                              mods_valueArray,
                                              evals, id, flags, txn);
                if (rc) {
                    ldbm_nasty("index_add_mods", errmsg, 1044, rc);
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

        if (rc != 0) {
            ldbm_nasty("index_add_mods", errmsg, 1040, rc);
            return (rc);
        }
    } /* for ( i = 0; mods[i] != NULL; i++ ) */

    return (0);
}


/*
 * Convert a 'struct berval' into a displayable ASCII string
 */

#define SPECIAL(c) (c < 32 || c > 126 || c == '\\' || c == '"')

const char *
encode(const struct berval *data, char buf[BUFSIZ])
{
    char *s;
    char *last;
    if (data == NULL || data->bv_len == 0)
        return "";
    last = data->bv_val + data->bv_len - 1;
    for (s = data->bv_val; s < last; ++s) {
        if (SPECIAL(*s)) {
            char *first = data->bv_val;
            char *bufNext = buf;
            size_t bufSpace = BUFSIZ - 4;
            while (1) {
                /* printf ("%lu bytes ASCII\n", (unsigned long)(s - first)); */
                if (bufSpace < (size_t)(s - first))
                    s = first + bufSpace - 1;
                if (s != first) {
                    memcpy(bufNext, first, s - first);
                    bufNext += (s - first);
                    bufSpace -= (s - first);
                }
                do {
                    if (bufSpace) {
                        *bufNext++ = '\\';
                        --bufSpace;
                    }
                    if (bufSpace < 2) {
                        memcpy(bufNext, "..", 2);
                        bufNext += 2;
                        goto bail;
                    }
                    if (*s == '\\' || *s == '"') {
                        *bufNext++ = *s;
                        --bufSpace;
                    } else {
                        sprintf(bufNext, "%02x", (unsigned)*(unsigned char *)s);
                        bufNext += 2;
                        bufSpace -= 2;
                    }
                } while (++s <= last && SPECIAL(*s));
                if (s > last)
                    break;
                first = s;
                while (!SPECIAL(*s) && s <= last)
                    ++s;
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

static const char *
encoded(dbi_val_t *d, char buf[BUFSIZ])
{
    struct berval data;
    data.bv_len = d->dsize;
    data.bv_val = d->dptr;
    return encode(&data, buf);
}

IDList *
index_read(
    backend *be,
    char *type,
    const char *indextype,
    const struct berval *val,
    back_txn *txn,
    int *err)
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
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    const char *indextype,
    const struct berval *val,
    back_txn *txn,
    int *err,
    int *unindexed,
    int allidslimit)
{
    dbi_db_t *db = NULL;
    dbi_txn_t *db_txn = NULL;
    dbi_val_t key = {0};
    IDList *idl = NULL;
    char *prefix;
    char buf[BUFSIZ];
    char typebuf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    struct attrinfo *ai = NULL;
    char *basetmp, *basetype;
    int retry_count = 0;
    struct berval *encrypted_val = NULL;
    int is_and = 0;
    unsigned int ai_flags = 0;

    *err = 0;

    if (unindexed != NULL)
        *unindexed = 0;
    prefix = index_index2prefix(indextype);
    if (prefix == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "index_read_ext_allids", "NULL prefix\n");
        return NULL;
    }
    if (slapi_is_loglevel_set(LDAP_DEBUG_TRACE)) {
        slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids", "=> ( \"%s\" %s \"%s\" )\n",
                      type, prefix, encode(val, buf));
    }

    basetype = typebuf;
    if ((basetmp = slapi_attr_basetype(type, typebuf, sizeof(typebuf))) != NULL) {
        basetype = basetmp;
    }

    ainfo_get(be, basetype, &ai);
    if (ai == NULL) {
        index_free_prefix(prefix);
        slapi_ch_free_string(&basetmp);
        return NULL;
    }

    slapi_log_err(SLAPI_LOG_ARGS, "index_read_ext_allids", "indextype: \"%s\" indexmask: 0x%x\n",
                  indextype, ai->ai_indexmask);

    /* If entryrdn switch is on AND the type is entrydn AND the prefix is '=',
     * use the entryrdn index directly */
    if (entryrdn_get_switch() && (*prefix == '=') &&
        (0 == PL_strcasecmp(basetype, LDBM_ENTRYDN_STR))) {
        int rc = 0;
        ID id = 0;
        Slapi_DN sdn;

        /* We don't need these values... */
        index_free_prefix(prefix);
        slapi_ch_free_string(&basetmp);
        if (NULL == val || NULL == val->bv_val) {
            /* entrydn value was not given */
            return NULL;
        }
        slapi_sdn_init_dn_byval(&sdn, val->bv_val);
        rc = entryrdn_index_read(be, &sdn, &id, txn);
        slapi_sdn_done(&sdn);
        if (rc == DBI_RC_NOTFOUND) {
            /* return an empty list */
            return idl_alloc(0);
        } else if (rc) { /* failure */
            return NULL;
        } else { /* success */
            rc = idl_append_extend(&idl, id);
            if (rc) { /* failure */
                return NULL;
            }
            return idl;
        }
    }

    if (!is_indexed(indextype, ai->ai_indexmask, ai->ai_index_rules)) {
        idl = idl_allids(be);
        if (unindexed != NULL)
            *unindexed = 1;
        slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids", "<= %lu candidates "
                                                                "(allids - not indexed)\n",
                      (u_long)IDL_NIDS(idl));
        index_free_prefix(prefix);
        slapi_ch_free_string(&basetmp);
        return (idl);
    }
    if (pb) {
        slapi_pblock_get(pb, SLAPI_SEARCH_IS_AND, &is_and);
    }
    ai_flags = is_and ? INDEX_ALLIDS_FLAG_AND : 0;
    /* the caller can pass in a value of 0 - just ignore those - but if the index
     * config sets the allidslimit to 0, this means to skip the index
     */
    if (index_get_allids(&allidslimit, indextype, ai, val, ai_flags) &&
        (allidslimit == 0)) {
        idl = idl_allids(be);
        if (unindexed != NULL)
            *unindexed = 1;
        slapi_log_err(SLAPI_LOG_BACKLDBM, "index_read_ext_allids", "<= %lu candidates "
                                                                   "(do not use index)\n",
                      (u_long)IDL_NIDS(idl));
        slapi_log_err(SLAPI_LOG_BACKLDBM, "index_read_ext_allids", "<= index attr %s type %s "
                                                                   "for value %s does not use index\n",
                      basetype, indextype,
                      (val && val->bv_val) ? val->bv_val : "ALL");
        index_free_prefix(prefix);
        slapi_ch_free_string(&basetmp);
        return (idl);
    }
    if ((*err = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE)) != 0) {
        slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids",
                      "<=  NULL (index file open for attr %s)\n",
                      basetype);
        index_free_prefix(prefix);
        slapi_ch_free_string(&basetmp);
        return (NULL);
    }

    if (val != NULL) {
        size_t vlen;
        int ret = 0;

        /* If necessary, encrypt this index key */
        ret = attrcrypt_encrypt_index_key(be, ai, val, &encrypted_val);
        if (ret) {
            slapi_log_err(SLAPI_LOG_ERR, "index_read_ext_allids",
                          "Failed to encrypt index key for %s\n", basetype);
        }
        if (encrypted_val) {
            val = encrypted_val;
        }
        vlen = val->bv_len;
        dblayer_value_concat(be, &key, buf, sizeof(buf), 3,
            prefix, strlen(prefix), val->bv_val, vlen, "", 1);
    } else {
        dblayer_value_concat(be, &key, buf, sizeof(buf), 2, prefix, strlen(prefix), "", 1);
    }
    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }
    for (retry_count = 0; retry_count < IDL_FETCH_RETRY_COUNT; retry_count++) {
        *err = NEW_IDL_DEFAULT;
        PRIntervalTime interval;
        idl_free(&idl);
        idl = idl_fetch_ext(be, db, &key, db_txn, ai, err, allidslimit);
        if (*err == DBI_RC_RETRY) {
            ldbm_nasty("index_read_ext_allids", "index read retrying transaction", 1045, *err);
#ifdef FIX_TXN_DEADLOCKS
#error can only retry here if txn == NULL - otherwise, have to abort and retry txn
#endif
            interval = PR_MillisecondsToInterval(slapi_rand() % 100);
            DS_Sleep(interval);
            continue;
        } else if (*err != 0 || idl == NULL) {
            /* The database might not exist. We have to assume it means empty set */
            slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids", "Failed to access idl index for %s\n", basetype);
            slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids", "Assuming %s has no index values\n", basetype);
            idl_free(&idl);
            idl = idl_alloc(0);
            break;
        } else {
            break;
        }
    }
    if (retry_count == IDL_FETCH_RETRY_COUNT) {
        ldbm_nasty("index_read_ext_allids", "index_read retry count exceeded", 1046, *err);
    } else if (*err != 0 && *err != DBI_RC_NOTFOUND) {
        ldbm_nasty("index_read_ext_allids", errmsg, 1050, *err);
    }
    slapi_ch_free_string(&basetmp);
    dblayer_value_free(be, &key);

    dblayer_release_index_file(be, ai, db);

    index_free_prefix(prefix);

    if (encrypted_val) {
        ber_bvfree(encrypted_val);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "index_read_ext_allids", "<=  %lu candidates\n",
                  (u_long)IDL_NIDS(idl));
    return (idl);
}

IDList *
index_read_ext(
    backend *be,
    char *type,
    const char *indextype,
    const struct berval *val,
    back_txn *txn,
    int *err,
    int *unindexed)
{
    return index_read_ext_allids(NULL, be, type, indextype, val, txn, err, unindexed, 0);
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
int
dbi_value_cmp(dbi_val_t *L, dbi_val_t *R, value_compare_fn_type cmp_fn)
{
    struct berval Lv;
    struct berval Rv;

    if ((L->data && (L->size > 1) && (*((char *)L->data) == EQ_PREFIX)) &&
        (R->data && (R->size > 1) && (*((char *)R->data) == EQ_PREFIX))) {
        Lv.bv_val = (char *)L->data + 1;
        Lv.bv_len = (ber_len_t)L->size - 1;
        Rv.bv_val = (char *)R->data + 1;
        Rv.bv_len = (ber_len_t)R->size - 1;
        /* use specific compare fn, if any */
        cmp_fn = (cmp_fn ? cmp_fn : slapi_berval_cmp);
    } else {
        Lv.bv_val = (char *)L->data;
        Lv.bv_len = (ber_len_t)L->size;
        Rv.bv_val = (char *)R->data;
        Rv.bv_len = (ber_len_t)R->size;
        /* just compare raw bervals */
        cmp_fn = slapi_berval_cmp;
    }
    return cmp_fn(&Lv, &Rv);
}

/* Steps to the next key without keeping a cursor open */
/* Returns the new key value in the dbi_val_t */
static int
index_range_next_key(Slapi_Backend *be, dbi_db_t *db, dbi_val_t *key, dbi_txn_t *db_txn)
{
    dbi_cursor_t cursor = {0};
    dbi_val_t data = {0};
    int ret = 0;

/* Make cursor */
retry:
    ret = dblayer_new_cursor(be, db, db_txn, &cursor);
    if (0 != ret) {
        return ret;
    }
    /* Seek to the last key */
    dblayer_value_init(be, &data);
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY, key, &data); /* both key and data could be allocated */
    /* data allocated here, we don't need it */
    dblayer_value_free(be, &data);
    if (DBI_RC_NOTFOUND == ret) {
        /* If this happens, it means that we tried to seek to a key which has just been deleted */
        /* So, we seek to the nearest one instead */
        ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_NEAR_KEY, key, &data); /* both key and data could be allocated */
        /* a new key and data are allocated here, need to free them both */
        dblayer_value_free(be, &data);
    }
    if (0 != ret) {
        if (DBI_RC_RETRY == ret) {
            /* Deadlock detected, retry the operation */
            dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the cursor
#endif
            goto retry;
        } else {
            goto error;
        }
    }
    /* Seek to the next one
     * [612498] NODUP is needed for new idl to get the next non-duplicated key
     * No effect on old idl since there's no dup there (i.e., DB_NEXT == DB_NEXT_NODUP)
     */
    ret = dblayer_cursor_op(&cursor, DBI_OP_NEXT_KEY, key, &data); /* both key and data could be allocated */
    dblayer_value_free(be, &data);
    if (DBI_RC_RETRY == ret) {
        /* Deadlock detected, retry the operation */
        dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL); /* both key and data could be allocated */
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the cursor
#endif
        goto retry;
    }
error:
    /* Close the cursor */
    dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    return ret;
}

/* This routine add in a given index (parentid)
 * the key/value = '=0'/<suffix entryID>
 * Input:
 *      info->key contains the key to lookup (i.e. '0')
 *      info->index index name used to retrieve syntax and db file
 *      info->id  the entryID of the suffix
 */
int
set_suffix_key(Slapi_Backend *be, struct _back_info_index_key *info)
{
    struct ldbminfo *li;
    int rc;
    back_txn txn;
    Slapi_Value *sv_key[2];
    Slapi_Value tmpval;

    if (info->index== NULL || info->key == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "set_suffix_key", "Invalid index %s or key %s\n",
                info->index ? info->index : "NULL",
                info->key ? info->key : "NULL");
        return -1;
    }

    /* Start a txn */
    li = (struct ldbminfo *)be->be_database->plg_private;
    dblayer_txn_init(li, &txn);
    if ((rc = dblayer_txn_begin(be, txn.back_txn_txn, &txn))) {
        slapi_log_err(SLAPI_LOG_ERR, "set_suffix_key", "Fail to update %s index with  %s/%d (key/ID): txn begin fails\n",
                  info->index, info->key, info->id);
        return rc;
    }

    sv_key[0] = &tmpval;
    sv_key[1] = NULL;
    slapi_value_init_string(sv_key[0], info->key);

    if ((rc = index_addordel_values_sv(be, info->index, sv_key, NULL, info->id, BE_INDEX_ADD, &txn))) {
        value_done(sv_key[0]);
        dblayer_txn_abort(be, &txn);
        slapi_log_err(SLAPI_LOG_ERR, "set_suffix_key", "Fail to update %s index with  %s/%d (key/ID): index_addordel_values_sv fails\n",
                  info->index, info->key, info->id);
        return rc;
    }

    value_done(sv_key[0]);
    if ((rc = dblayer_txn_commit(be, &txn))) {
        slapi_log_err(SLAPI_LOG_ERR, "set_suffix_key", "Fail to update %s index with  %s/%d (key/ID): commit fails\n",
                  info->index, info->key, info->id);
        return rc;
    }

    return 0;
}
/* This routine retrieves from a given index (parentid)
 * the key/value = '=0'/<suffix entryID>
 * Input:
 *      info->key contains the key to lookup (i.e. '0')
 *      info->index index name used to retrieve syntax and db file
 * Output
 *      info->id It returns the first id that is found for the key.
 *               If the key is not found, or there is no value for the key
 *               it contains '0'
 *      info->key_found  Boolean that says if the key leads to a valid ID in info->id
 */
int
get_suffix_key(Slapi_Backend *be, struct _back_info_index_key *info)
{
    struct berval bv;
    int err;
    IDList *idl = NULL;
    ID id;
    int rc = 0;

    if (info->index== NULL || info->key == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "get_suffix_key", "Invalid index %s or key %s\n",
                info->index ? info->index : "NULL",
                info->key ? info->key : "NULL");
        return -1;
    }

    /* This is the key to retrieve */
    bv.bv_val = info->key;
    bv.bv_len = strlen(bv.bv_val);

    /* Assuming we are not going to find the key*/
    info->key_found = PR_FALSE;
    id = 0;
    idl = index_read(be, info->index, indextype_EQUALITY, &bv, NULL, &err);

    if (idl == NULL) {
        if (err != 0 && err != DBI_RC_NOTFOUND) {
            slapi_log_err(SLAPI_LOG_ERR, "get_suffix_key", "Fail to read key %s (err=%d)\n",
                    info->key ? info->key : "NULL",
                    err);
            rc = err;
        }
    } else {
        /* info->key was found */
        id = idl_firstid(idl);
        if (id != NOID) {
            info->key_found = PR_TRUE;
        } else {
            /* there is no ID in that key, make it as it was not found */
            id = 0;
        }
        idl_free(&idl);
    }

    /* now set the returned id */
    info->id = id;

    return rc;
}

static void set_range_limit(
    backend *be,
    struct berval *val,
    char *prefix,
    int plen,
    dbi_val_t *limit)
{
    /* set up the starting or ending keys for a range search */
    if (val != NULL) { /* compute a key from val */
        dblayer_value_concat(be, limit, NULL, 0, 3, prefix, plen, val->bv_val, val->bv_len, "", 1);
    } else {
        dblayer_value_concat(be, limit, NULL, 0, 2, prefix, plen, "", 1);
        limit->size = limit->ulen;   /* Include \0 in the value */
    }
}


IDList *
index_range_read_ext(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    const char *indextype,
    int
    operator,
    struct berval *val,
    struct berval *nextval,
    int range,
    back_txn *txn,
    int *err,
    int allidslimit)
{
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    dbi_db_t *db;
    dbi_txn_t *db_txn = NULL;
    dbi_cursor_t dbc = {0};
    dbi_val_t lowerkey = {0};
    dbi_val_t upperkey = {0};
    dbi_val_t cur_key = {0};
    dbi_val_t data = {0};
    IDList *idl = NULL;
    char *prefix = NULL;
    size_t plen;
    ID i;
    struct attrinfo *ai = NULL;
    int lookthrough_limit = -1; /* default no limit */
    int is_and = 0;
    int sizelimit = 0;
    struct timespec expire_time;
    int timelimit = -1;
    back_search_result_set *sr = NULL;
    int isroot = 0;
    int coreop = operator&SLAPI_OP_RANGE;
    char *tmpbuf = NULL;

    if (!pb) {
        slapi_log_err(SLAPI_LOG_ERR, "index_range_read_ext", "NULL pblock\n");
        return NULL;
    }

    *err = 0;

    prefix = index_index2prefix(indextype);
    if (prefix == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "index_range_read_ext", "NULL prefix\n");
        return (NULL);
    }

    plen = strlen(prefix);
    slapi_pblock_get(pb, SLAPI_SEARCH_IS_AND, &is_and);
    if (!is_and) {
        slapi_pblock_get(pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit);
    }
    slapi_pblock_get(pb, SLAPI_SEARCH_TIMELIMIT, &timelimit);
    Slapi_Operation *op;
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_operation_time_expiry(op, (time_t)timelimit, &expire_time);

    /*
     * Determine the lookthrough_limit from the PBlock.
     * No limit if there is no search result set and the requestor is root.
     */
    slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_SET, &sr);
    if (sr != NULL) {
        /* the normal case */
        lookthrough_limit = sr->sr_lookthroughlimit;
    }
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    if (!isroot) {
        if (lookthrough_limit > li->li_rangelookthroughlimit) {
            lookthrough_limit = li->li_rangelookthroughlimit;
        }
    }

    slapi_log_err(SLAPI_LOG_TRACE, "index_range_read_ext", "lookthrough_limit=%d\n",
                  lookthrough_limit);

    switch (coreop) {
    case SLAPI_OP_LESS:
    case SLAPI_OP_LESS_OR_EQUAL:
    case SLAPI_OP_GREATER_OR_EQUAL:
    case SLAPI_OP_GREATER:
        break;
    default:
        slapi_log_err(SLAPI_LOG_ERR,
                      "index_range_read_ext", "(%s,%s) NULL (operator %i)\n",
                      type, prefix, coreop);
        index_free_prefix(prefix);
        return (NULL);
    }
    ainfo_get(be, type, &ai);
    if (ai == NULL) {
        index_free_prefix(prefix);
        return NULL;
    }
    slapi_log_err(SLAPI_LOG_ARGS, "index_range_read_ext", "indextype: \"%s\" indexmask: 0x%x\n",
                  indextype, ai->ai_indexmask);
    if (!is_indexed(indextype, ai->ai_indexmask, ai->ai_index_rules)) {

        /* Mark that the search has an unindexed component */
        slapi_pblock_set_flag_operation_notes(pb, SLAPI_OP_NOTE_UNINDEXED);

        idl = idl_allids(be);
        slapi_log_err(SLAPI_LOG_TRACE,
                      "index_range_read_ext", "(%s,%s) %lu candidates (allids)\n",
                      type, prefix, (u_long)IDL_NIDS(idl));
        index_free_prefix(prefix);
        return (idl);
    }
    if ((*err = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "index_range_read_ext", "(%s,%s) NULL (could not open index file)\n",
                      type, prefix);
        index_free_prefix(prefix);
        return (NULL); /* why not allids? */
    }
    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    }
    /* get a cursor so we can walk over the table */
    *err = dblayer_new_cursor(be, db, db_txn, &dbc);
    if (0 != *err) {
        ldbm_nasty("index_range_read_ext", errmsg, 1060, *err);
        slapi_log_err(SLAPI_LOG_ERR,
                      "index_range_read_ext", "(%s,%s) NULL: db->cursor() == %i\n",
                      type, prefix, *err);
        dblayer_release_index_file(be, ai, db);
        index_free_prefix(prefix);
        return (NULL); /* why not allids? */
    }

    /* set up the starting and ending keys for a range search */
    if (range != 1) { /* open range search */
        /* this is a search with only one boundary value */
        switch (coreop) {
        case SLAPI_OP_LESS:
        case SLAPI_OP_LESS_OR_EQUAL:
            dblayer_value_strdup(be, &lowerkey, prefix);
            set_range_limit(be, val, prefix, plen, &upperkey);
            break;
        case SLAPI_OP_GREATER_OR_EQUAL:
        case SLAPI_OP_GREATER:
            set_range_limit(be, val, prefix, plen, &lowerkey);
            /* upperkey = a value slightly greater than prefix */
            dblayer_value_concat(be, &upperkey, NULL, 0, 2, prefix, plen, "", 1);
            tmpbuf = upperkey.data;
            ++(tmpbuf[plen - 1]);
            tmpbuf = NULL;
            /* ... but not greater than the last key in the index */
            dblayer_value_init(be, &cur_key);
            dblayer_value_init(be, &data);
            *err = dblayer_cursor_op(&dbc, DBI_OP_MOVE_TO_LAST, &cur_key, &data);
            dblayer_value_free(be, &data);
            /* Note that cur_key needs to get freed somewhere below */
            if (0 != *err) {
                if (DBI_RC_NOTFOUND == *err) {
                    /* There are no keys in the index so we should return no candidates. */
                    *err = 0;
                    idl = NULL;
                    dblayer_value_free(be, &cur_key);
                    dblayer_cursor_op(&dbc, DBI_OP_CLOSE, NULL, NULL);
                    goto error;
                } else {
                    ldbm_nasty("index_range_read_ext", errmsg, 1070, *err);
                    slapi_log_err(SLAPI_LOG_ERR,
                                  "index_range_read_ext", "(%s,%s) seek to end of index file err %i\n",
                                  type, prefix, *err);
                }
            } else if (dbi_value_cmp(&upperkey, &cur_key, ai->ai_key_cmp_fn) > 0) {
                dblayer_value_free(be, &upperkey); /* upper >= last :no need to check upper bound */
            }
            dblayer_value_free(be, &cur_key);
            break;
        }
    } else { /* closed range search: e.g., (&(x >= a)(x <= b)) */
        /* this is a search with two boundary values (starting and ending) */
        /* set up the starting and ending keys for search */
        set_range_limit(be, val, prefix, plen, &lowerkey);
        set_range_limit(be, nextval, prefix, plen, &upperkey);
    }
    /* if (LDAP_DEBUG_FILTER)  {
        char encbuf [BUFSIZ];
        slapi_log_err(SLAPI_LOG_FILTER, "   lowerkey=%s(%li bytes)\n",
              encoded (&lowerkey, encbuf), (long)lowerkey.dsize, 0 );
        slapi_log_err(SLAPI_LOG_FILTER, "   upperkey=%s(%li bytes)\n",
              encoded (&upperkey, encbuf), (long)upperkey.dsize, 0 );
    } */
    dblayer_value_init(be, &data);
    *err = dblayer_cursor_op(&dbc, DBI_OP_MOVE_NEAR_KEY, &lowerkey, &data); /* both key and data could be allocated */
    /* If the seek above fails due to DBI_RC_NOTFOUND, this means that there are no keys
     * which are >= the target key. This means that we should return no candidates
     */
    if (0 != *err) {
        /* Free the key we just read above */
        dblayer_value_free(be, &lowerkey);
        if (DBI_RC_NOTFOUND == *err) {
            *err = 0;
            idl = idl_alloc(0);
        } else {
            idl = idl_allids(be);
            ldbm_nasty("index_range_read_ext", errmsg, 1080, *err);
            slapi_log_err(SLAPI_LOG_ERR,
                          "index_range_read_ext", "(%s,%s) allids (seek to lower key in index file err %i)\n",
                          type, prefix, *err);
        }
        dblayer_cursor_op(&dbc, DBI_OP_CLOSE, NULL, NULL);
        goto error;
    }
    /* We now close the cursor, since we're about to iterate over many keys */
    dblayer_cursor_op(&dbc, DBI_OP_CLOSE, NULL, NULL);

    /* step through the indexed db to retrive IDs within the search range */
    dblayer_value_init(be, &data);
    cur_key = lowerkey;
    dblayer_value_init(be, &lowerkey);   /* Clear lowerkey to avoid double free */
    *err = 0;
    if (coreop == SLAPI_OP_GREATER) {
        *err = index_range_next_key(be, db, &cur_key, db_txn);
        if (*err) {
            slapi_log_err(SLAPI_LOG_ERR, "index_range_read_ext",
                          "(%s,%s) op==GREATER, no next key: %i)\n",
                          type, prefix, *err);
            goto error;
        }
    }
    if (operator&SLAPI_OP_RANGE_NO_ALLIDS) {
        *err = NEW_IDL_NO_ALLID;
    }
    if (idl_get_idl_new()) { /* new idl */
        slapi_log_err(SLAPI_LOG_FILTER,
                      "index_range_read_ext", "Getting index range from keys %s to %s.\n", (char*)cur_key.data, (char*)upperkey.data);
        idl = idl_new_range_fetch(be, db, &cur_key, &upperkey, db_txn,
                                  ai, err, allidslimit, sizelimit, &expire_time,
                                  lookthrough_limit, operator);
    } else { /* old idl */
        int retry_count = 0;
        while (*err == 0 &&
               (upperkey.data &&
                        (coreop == SLAPI_OP_LESS)
                    ? dbi_value_cmp(&cur_key, &upperkey, ai->ai_key_cmp_fn) < 0
                    : dbi_value_cmp(&cur_key, &upperkey, ai->ai_key_cmp_fn) <= 0)) {
            /* exit the loop when we either run off the end of the table,
             * fail to read a key, or read a key that's out of range.
             */
            IDList *tmp;
            /*
            char encbuf [BUFSIZ];
            slapi_log_err(SLAPI_LOG_FILTER, "   cur_key=%s(%li bytes)\n",
                   encoded (&cur_key, encbuf), (long)cur_key.dsize, 0 );
            */
            /* lookthrough limit and size limit check */
            if (idl) {
                if ((lookthrough_limit != -1) &&
                    (idl->b_nids > (ID)lookthrough_limit)) {
                    idl_free(&idl);
                    idl = idl_allids(be);
                    slapi_log_err(SLAPI_LOG_TRACE,
                                  "index_range_read_ext", "lookthrough_limit exceeded\n");
                    *err = LDAP_ADMINLIMIT_EXCEEDED;
                    break;
                }
                if ((sizelimit > 0) && (idl->b_nids > (ID)sizelimit)) {
                    slapi_log_err(SLAPI_LOG_TRACE,
                                  "index_range_read_ext", "sizelimit exceeded\n");
                    *err = LDAP_SIZELIMIT_EXCEEDED;
                    break;
                }
            }
            /* check time limit */
            if (slapi_timespec_expire_check(&expire_time) == TIMER_EXPIRED) {
                slapi_log_err(SLAPI_LOG_TRACE,
                              "index_range_read_ext", "timelimit exceeded\n");
                *err = LDAP_TIMELIMIT_EXCEEDED;
                break;
            }
            /* Check to see if the operation has been abandoned (also happens
             * when the connection is closed by the client).
             */
            if (slapi_op_abandoned(pb)) {
                if (NULL != idl) {
                    idl_free(&idl);
                    idl = NULL;
                }
                slapi_log_err(SLAPI_LOG_TRACE,
                              "index_range_read_ext", "Operation abandoned\n");
                break; /* clean up happens outside the while() loop */
            }

            /* the cur_key dbi_val_t already has the first entry in it when we enter
             * the loop, so we process the entry then step to the next one */
            cur_key.flags = 0;
            for (retry_count = 0;
                 retry_count < IDL_FETCH_RETRY_COUNT;
                 retry_count++) {
                *err = NEW_IDL_DEFAULT;
                tmp = idl_fetch_ext(be, db, &cur_key, NULL, ai, err, allidslimit);
                if (*err == DBI_RC_RETRY) {
                    ldbm_nasty("index_range_read_ext", "Retrying transaction", 1090, *err);
#ifdef FIX_TXN_DEADLOCKS
#error if txn != NULL, have to abort and retry the transaction, not just the fetch
#endif
                    continue;
                } else {
                    break;
                }
            }
            if (retry_count == IDL_FETCH_RETRY_COUNT) {
                ldbm_nasty("index_range_read_ext", "Retry count exceeded", 1095, *err);
            }
            if (!tmp) {
                if (slapi_is_loglevel_set(LDAP_DEBUG_TRACE)) {
                    char encbuf[BUFSIZ];
                    slapi_log_err(SLAPI_LOG_TRACE,
                                  "index_range_read_ext", "cur_key=%s(%li bytes) was deleted - skipping\n",
                                  encoded(&cur_key, encbuf), (long)cur_key.dsize);
                }
            } else {
                /* idl tmp only contains one id */
                /* append it at the end here; sort idlist at the end */
                if (ALLIDS(tmp)) {
                    idl_free(&idl);
                    idl = tmp;
                } else {
                    ID id;
                    for (id = idl_firstid(tmp);
                         id != NOID; id = idl_nextid(tmp, id)) {
                        *err = idl_append_extend(&idl, id);
                        if (*err) {
                            ldbm_nasty("index_range_read_ext", "Failed to generate idlist",
                                       1097, *err);
                        }
                    }
                    idl_free(&tmp);
                }
                if (ALLIDS(idl)) {
                    slapi_log_err(SLAPI_LOG_TRACE,
                                  "index_range_read_ext", "Hit an allids value\n");
                    break;
                }
            }
            if (KEY_EQ(&cur_key, &upperkey)) { /* this is the last key */
                break;
                /* Another c_get would return the same key, with no error. */
            }
            dblayer_value_init(be, &data);
            dblayer_value_init(be, &cur_key);
            *err = index_range_next_key(be, db, &cur_key, db_txn);
            /* *err = dbc->c_get(dbc,&cur_key,&data,DB_NEXT); */
            if (*err == DBI_RC_NOTFOUND) {
                *err = 0;
                break;
            }
        }
        /* sort idl */
        if (idl && !ALLIDS(idl)) {
            qsort((void *)&idl->b_ids[0], idl->b_nids,
                  (size_t)sizeof(ID), idl_sort_cmp);
        }
    }
    if (*err) {
        slapi_log_err(SLAPI_LOG_FILTER,
                      "index_range_read_ext", "index_range_read_ext failed to read the range db error == %i\n", *err);
    }
#ifdef LDAP_ERROR_LOGGING
    /* this is for debugging only */
    if (idl != NULL) {
        if (ALLIDS(idl)) {
            slapi_log_err(SLAPI_LOG_FILTER, "index_range_read_ext", "idl=ALLIDS\n");
        } else {
            slapi_log_err(SLAPI_LOG_FILTER,
                          "index_range_read_ext", "idl->b_nids=%d\n", idl->b_nids);
            slapi_log_err(SLAPI_LOG_FILTER,
                          "index_range_read_ext", "idl->b_nmax=%d\n", idl->b_nmax);

            for (i = 0; i < idl->b_nids; i++) {
                slapi_log_err(SLAPI_LOG_FILTER,
                              "index_range_read_ext", "idl->b_ids[%d]=%d\n", i, idl->b_ids[i]);
            }
        }
    }
#endif
error:
    slapi_log_err(SLAPI_LOG_TRACE, "index_range_read_ext", "(%s,%s) %lu candidates\n",
                  type, prefix ? prefix : "", (u_long)IDL_NIDS(idl));

    index_free_prefix(prefix);
    dblayer_value_free(be, &cur_key);
    dblayer_value_free(be, &lowerkey);
    dblayer_value_free(be, &upperkey);
    dblayer_release_index_file(be, ai, db);

    return (idl);
}

IDList *
index_range_read(
    Slapi_PBlock *pb,
    backend *be,
    char *type,
    const char *indextype,
    int
    operator,
    struct berval *val,
    struct berval *nextval,
    int range,
    back_txn *txn,
    int *err)
{
    return index_range_read_ext(pb, be, type, indextype, operator, val, nextval, range, txn, err, 0);
}

static int
addordel_values_sv(
    backend *be,
    dbi_db_t *db,
    char *type __attribute__((unused)),
    const char *indextype,
    Slapi_Value **vals,
    ID id,
    int flags, /* BE_INDEX_ADD, etc */
    back_txn *txn,
    struct attrinfo *a,
    int *idl_disposition,
    void *buffer_handle)
{
    int rc = 0;
    int i = 0;
    dbi_val_t key = {0};
    dbi_txn_t *db_txn = NULL;
    size_t plen, vlen, len;
    char *tmpbuf = NULL;
    size_t tmpbuflen = 0;
    char *realbuf;
    char *prefix = NULL;
    const struct berval *bvp;
    struct berval *encrypted_bvp = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, "addordel_values_sv", "%s_values\n",
                  (flags & BE_INDEX_ADD) ? "add" : "del");

    prefix = index_index2prefix(indextype);
    if (prefix == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "addordel_values_sv", "NULL prefix\n");
        return (-1);
    }

    if (vals == NULL) {
        dblayer_value_set(be, &key, prefix, strlen(prefix) + 1);   /* Key may change */
        dblayer_value_protect_data(be, &key);                      /* But the prefix buffer should not be freed */
        if (NULL != txn) {
            db_txn = txn->back_txn_txn;
        }

        if (flags & BE_INDEX_ADD) {
            rc = idl_insert_key(be, db, &key, id, db_txn, a, idl_disposition);
        } else {
            rc = idl_delete_key(be, db, &key, id, db_txn, a);
            /* check for no such key/id - ok in some cases */
            if (rc == DBI_RC_NOTFOUND || rc == -666) {
                rc = 0;
            }
        }

        if (rc != 0) {
            ldbm_nasty("addordel_values_sv", errmsg, 1120, rc);
        }
        dblayer_value_free(be, &key);
        index_free_prefix(prefix);
        slapi_log_err(SLAPI_LOG_TRACE, "addordel_values_sv", "%s_values %d\n",
                      (flags & BE_INDEX_ADD) ? "add" : "del", rc);
        return (rc);
    }

    plen = strlen(prefix);
    for (i = 0; vals[i] != NULL; i++) {
        bvp = slapi_value_get_berval(vals[i]);

        /* Encrypt the index key if necessary */
        {
            if (a->ai_attrcrypt && (0 == (flags & BE_INDEX_DONT_ENCRYPT))) {
                rc = attrcrypt_encrypt_index_key(be, a, bvp, &encrypted_bvp);
                if (rc) {
                    slapi_log_err(SLAPI_LOG_ERR, "addordel_values_sv",
                                  "Failed to encrypt index key for %s\n", a->ai_type);
                } else {
                    bvp = encrypted_bvp;
                }
            }
        }

        vlen = bvp->bv_len;
        len = plen + vlen;

        if (len < tmpbuflen) {
            realbuf = tmpbuf;
        } else {
            tmpbuf = slapi_ch_realloc(tmpbuf, len + 1);
            tmpbuflen = len + 1;
            realbuf = tmpbuf;
        }

        memcpy(realbuf, prefix, plen);
        memcpy(realbuf + plen, bvp->bv_val, vlen);
        realbuf[len] = '\0';
        /* Free the encrypted berval if necessary */
        if (encrypted_bvp) {
            ber_bvfree(encrypted_bvp);
            encrypted_bvp = NULL;
        }
        /* should be okay to use USERMEM here because we know what
         * the key is and it should never return a different value
         * than the one we pass in.
         */
        dblayer_value_set_buffer(be, &key, realbuf, sizeof realbuf);
        key.size = plen + vlen + 1;
        key.ulen = tmpbuflen;

        if (slapi_is_loglevel_set(LDAP_DEBUG_TRACE)) {
            char encbuf[BUFSIZ];

            slapi_log_err(SLAPI_LOG_TRACE, "addordel_values_sv", "%s_value(\"%s\")\n",
                          (flags & BE_INDEX_ADD) ? "add" : "del",
                          encoded(&key, encbuf));
        }

        if (NULL != txn) {
            db_txn = txn->back_txn_txn;
        }

        if (flags & BE_INDEX_ADD) {
            if (buffer_handle) {
                rc = index_buffer_insert(buffer_handle, &key, id, be, db_txn, a);
                if (rc == -2) {
                    rc = idl_insert_key(be, db, &key, id, db_txn, a, idl_disposition);
                }
            } else {
                rc = idl_insert_key(be, db, &key, id, db_txn, a, idl_disposition);
            }
        } else {
            rc = idl_delete_key(be, db, &key, id, db_txn, a);
            /* check for no such key/id - ok in some cases */
            if (rc == DBI_RC_NOTFOUND || rc == -666) {
                rc = 0;
            }
        }
        if (rc != 0) {
            ldbm_nasty("addordel_values_sv", errmsg, 1130, rc);
            break;
        }
        if (NULL != key.dptr && realbuf != key.dptr) { /* realloc'ed */
            tmpbuf = key.dptr;
            tmpbuflen = key.size;
        }
    }
    index_free_prefix(prefix);
    if (tmpbuf != NULL) {
        slapi_ch_free((void **)&tmpbuf);
    }

    if (rc != 0) {
        ldbm_nasty("addordel_values_sv", errmsg, 1140, rc);
    }
    slapi_log_err(SLAPI_LOG_TRACE, "addordel_values_sv", "%s_values %d\n",
                  (flags & BE_INDEX_ADD) ? "add" : "del", rc);
    return (rc);
}

int
index_addordel_string(backend *be, const char *type, const char *s, ID id, int flags, back_txn *txn)
{
    Slapi_Value *svp[2];
    Slapi_Value sv;

    memset(&sv, 0, sizeof(Slapi_Value));
    sv.bv.bv_len = strlen(s);
    sv.bv.bv_val = (void *)s;
    svp[0] = &sv;
    svp[1] = NULL;
    if (flags & BE_INDEX_NORMALIZED)
        slapi_value_set_flags(&sv, BE_INDEX_NORMALIZED);
    return index_addordel_values_ext_sv(be, type, svp, NULL, id, flags, txn, NULL, NULL);
}

int
index_addordel_values_sv(
    backend *be,
    const char *type,
    Slapi_Value **vals,
    Slapi_Value **evals, /* existing values */
    ID id,
    int flags,
    back_txn *txn)
{
    return index_addordel_values_ext_sv(be, type, vals, evals,
                                        id, flags, txn, NULL, NULL);
}

int
index_addordel_values_ext_sv(
    backend *be,
    const char *type,
    Slapi_Value **vals,
    Slapi_Value **evals,
    ID id,
    int flags,
    back_txn *txn,
    int *idl_disposition,
    void *buffer_handle)
{
    dbi_db_t *db;
    struct attrinfo *ai = NULL;
    int err = -1;
    Slapi_Value **ivals;
    char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
    char *basetmp, *basetype;

    slapi_log_err(SLAPI_LOG_TRACE,
                  "index_addordel_values_ext_sv", "( \"%s\", %lu )\n", type, (u_long)id);

    basetype = buf;
    if ((basetmp = slapi_attr_basetype(type, buf, sizeof(buf))) != NULL) {
        basetype = basetmp;
    }

    ainfo_get(be, basetype, &ai);
    if (ai == NULL || ai->ai_indexmask == 0 || ai->ai_indexmask == INDEX_OFFLINE) {
        slapi_ch_free_string(&basetmp);
        return (0);
    }
    slapi_log_err(SLAPI_LOG_ARGS, "index_addordel_values_ext_sv", "indexmask 0x%x\n",
                  ai->ai_indexmask);
    if ((err = dblayer_get_index_file(be, ai, &db, DBOPEN_CREATE)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "index_addordel_values_ext_sv", "index_read NULL (could not open index attr %s)\n",
                      basetype);
        slapi_ch_free_string(&basetmp);
        if (err != 0) {
            ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1210, err);
        }
        goto bad;
    }

    /*
     * presence index entry
     */
    if ((ai->ai_indexmask & INDEX_PRESENCE) &&
        (flags & (BE_INDEX_ADD | BE_INDEX_PRESENCE))) {
        /* on delete, only remove the presence index if the
         * BE_INDEX_PRESENCE flag is set.
         */
        err = addordel_values_sv(be, db, basetype, indextype_PRESENCE,
                                 NULL, id, flags, txn, ai, idl_disposition, NULL);
        if (err != 0) {
            ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1220, err);
            goto bad;
        }
    }

    /*
     * equality index entry
     */
    if ((ai->ai_indexmask & INDEX_EQUALITY) &&
        (flags & (BE_INDEX_ADD | BE_INDEX_EQUALITY))) {
        /* on delete, only remove the equality index if the
         * BE_INDEX_EQUALITY flag is set.
         */
        slapi_attr_values2keys_sv(&ai->ai_sattr, vals, &ivals, LDAP_FILTER_EQUALITY);

        err = addordel_values_sv(be, db, basetype, indextype_EQUALITY,
                                 ivals != NULL ? ivals : vals, id, flags, txn, ai, idl_disposition, NULL);
        if (ivals != NULL) {
            valuearray_free(&ivals);
        }
        if (err != 0) {
            ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1230, err);
            goto bad;
        }
    }

    /*
     * approximate index entry
     */
    if (ai->ai_indexmask & INDEX_APPROX) {
        slapi_attr_values2keys_sv(&ai->ai_sattr, vals, &ivals, LDAP_FILTER_APPROX);

        if (ivals != NULL) {
            err = addordel_values_sv(be, db, basetype,
                                     indextype_APPROX, ivals, id, flags, txn, ai, idl_disposition, NULL);
            valuearray_free(&ivals);
            if (err != 0) {
                ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1240, err);
                goto bad;
            }
        }
    }

    /*
     * substrings index entry
     */
    if (ai->ai_indexmask & INDEX_SUB) {
        Slapi_Value **esubvals = NULL;
        Slapi_Value **substresult = NULL;
        Slapi_Value **origvals = NULL;
        Slapi_PBlock *pipb = slapi_pblock_new();

        /* prepare pblock to pass ai_substr_lens */
        slapi_pblock_set(pipb, SLAPI_SYNTAX_SUBSTRLENS, ai->ai_substr_lens);
        slapi_attr_values2keys_sv_pb(&ai->ai_sattr, vals, &ivals, LDAP_FILTER_SUBSTRINGS, pipb);

        origvals = ivals;
        /* delete only: if the attribute has multiple values,
         * figure out the substrings that should remain
         * by slapi_attr_values2keys,
         * then get rid of them from the being deleted values
         */
        if (evals != NULL) {
            slapi_attr_values2keys_sv_pb(&ai->ai_sattr, evals, &esubvals, LDAP_FILTER_SUBSTRINGS, pipb);
            substresult = valuearray_minus_valuearray(&ai->ai_sattr, ivals, esubvals);
            ivals = substresult;
            valuearray_free(&esubvals);
        }
        slapi_pblock_destroy(pipb);
        if (ivals != NULL) {
            err = addordel_values_sv(be, db, basetype, indextype_SUB,
                                     ivals, id, flags, txn, ai, idl_disposition, buffer_handle);
            if (ivals != origvals) {
                valuearray_free(&origvals);
            }
            valuearray_free(&ivals);
            if (err != 0) {
                ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1250, err);
                goto bad;
            }

            ivals = NULL;
        }
    }

    /*
     * matching rule index entries
     */
    if (ai->ai_indexmask & INDEX_RULES) {
        Slapi_PBlock *pb = slapi_pblock_new();
        char **oid = ai->ai_index_rules;
        for (; *oid != NULL; ++oid) {
            if (create_matchrule_indexer(&pb, *oid, basetype) == 0) {
                char *officialOID = NULL;
                if (!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_OID, &officialOID) && officialOID != NULL) {
                    Slapi_Value **keys = NULL;
                    matchrule_values_to_keys_sv(pb, vals, &keys);
                    /* the matching rule indexer owns keys now */
                    if (keys != NULL && keys[0] != NULL) {
                        /* we've computed keys */
                        err = addordel_values_sv(be, db, basetype, officialOID, keys, id, flags, txn, ai, idl_disposition, NULL);
                        if (err != 0) {
                            ldbm_nasty("index_addordel_values_ext_sv", errmsg, 1260, err);
                        }
                    }
                    /*
                     * It would improve speed to save the indexer, for future use.
                     * But, for simplicity, we destroy it now:
                     */
                    /* this will also free keys */
                    destroy_matchrule_indexer(pb);
                    if (err != 0) {
                        goto bad;
                    }
                }
            }
        }
        slapi_pblock_destroy(pb);
    }

    dblayer_release_index_file(be, ai, db);
    if (basetmp != NULL) {
        slapi_ch_free((void **)&basetmp);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "index_addordel_values_ext_sv", "<=\n");
    return (0);

bad:
    dblayer_release_index_file(be, ai, db);
    return err;
}

int
index_delete_values(
    struct ldbminfo *li __attribute__((unused)),
    char *type __attribute__((unused)),
    struct berval **vals __attribute__((unused)),
    ID id __attribute__((unused)))
{
    return -1;
}

static int
is_indexed(const char *indextype, int indexmask, char **index_rules)
{
    int indexed;
    if (indextype == indextype_PRESENCE)
        indexed = INDEX_PRESENCE & indexmask;
    else if (indextype == indextype_EQUALITY)
        indexed = INDEX_EQUALITY & indexmask;
    else if (indextype == indextype_APPROX)
        indexed = INDEX_APPROX & indexmask;
    else if (indextype == indextype_SUB)
        indexed = INDEX_SUB & indexmask;
    else { /* matching rule */
        indexed = 0;
        if (INDEX_RULES & indexmask) {
            char **rule;
            for (rule = index_rules; *rule; ++rule) {
                if (!strcmp(*rule, indextype)) {
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

char *
index_index2prefix(const char *indextype)
{
    char *prefix;
    if (indextype == NULL)
        prefix = NULL;
    else if (indextype == indextype_PRESENCE)
        prefix = prefix_PRESENCE;
    else if (indextype == indextype_EQUALITY)
        prefix = prefix_EQUALITY;
    else if (indextype == indextype_APPROX)
        prefix = prefix_APPROX;
    else if (indextype == indextype_SUB)
        prefix = prefix_SUB;
    else { /* indextype is a matching rule name */
        const size_t len = strlen(indextype);
        char *p = slapi_ch_malloc(len + 3);
        p[0] = RULE_PREFIX;
        memcpy(p + 1, indextype, len);
        p[len + 1] = ':';
        p[len + 2] = '\0';
        prefix = p;
    }
    return (prefix);
}

void
index_free_prefix(char *prefix)
{
    if (prefix == NULL ||
        prefix == prefix_PRESENCE ||
        prefix == prefix_EQUALITY ||
        prefix == prefix_APPROX ||
        prefix == prefix_SUB) {
        /* do nothing */
    } else {
        slapi_ch_free_string(&prefix);
    }
}

/* helper stuff for valuearray_minus_valuearray */

typedef struct
{
    value_compare_fn_type cmp_fn;
    Slapi_Value *data;
} SVSORT;

static int
svsort_cmp(const void *x, const void *y)
{
    return ((SVSORT *)x)->cmp_fn(slapi_value_get_berval(((SVSORT *)x)->data), slapi_value_get_berval(((SVSORT *)y)->data));
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
    Slapi_Value **b)
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
    for (acnt = 0; a && a[acnt] != NULL; acnt++)
        ;

    /* determine length of b */
    for (bcnt = 0; b && b[bcnt] != NULL; bcnt++)
        ;

    /* allocate return array as big as a */
    c = (Slapi_Value **)slapi_ch_calloc(acnt + 1, sizeof(Slapi_Value *));
    if (acnt == 0)
        return c;

    /* sort a */
    atmp = (SVSORT *)slapi_ch_malloc(acnt * sizeof(SVSORT));
    for (i = 0; i < acnt; i++) {
        atmp[i].cmp_fn = cmp_fn;
        atmp[i].data = a[i];
    }
    qsort((void *)atmp, acnt, (size_t)sizeof(SVSORT), svsort_cmp);

    /* sort b */
    if (bcnt > 0) {
        btmp = (SVSORT *)slapi_ch_malloc(bcnt * sizeof(SVSORT));
        for (i = 0; i < bcnt; i++) {
            btmp[i].cmp_fn = cmp_fn;
            btmp[i].data = b[i];
        }
        qsort((void *)btmp, bcnt, (size_t)sizeof(SVSORT), svsort_cmp);
    }

    /* lock step through a and b */
    for (i = 0, j = 0, k = 0; i < acnt && j < bcnt;) {
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
    slapi_ch_free((void **)&atmp);
    if (btmp)
        slapi_ch_free((void **)&btmp);

    return c;
}

/*
 * Find the most specific match for the given index type, flags, and value, and return the allids value
 * for that match.  The priority is as follows, from highest to lowest:
 * * match type, flags, value
 * * match type, value
 * * match type, flags
 * * match type
 * * match flags
 * Note that for value to match, the type must be one that supports values e.g. eq or sub, so that
 * in order for value to match, there must be a type
 * For example, if you have
 * dn: cn=objectclass,...
 * objectclass: nsIndex
 * nsIndexType: eq
 * nsIndexIDListScanLimit: limit=0 type=eq flags=AND value=inetOrgPerson
 * nsIndexIDListScanLimit: limit=1 type=eq value=inetOrgPerson
 * nsIndexIDListScanLimit: limit=2 type=eq flags=AND
 * nsIndexIDListScanLimit: limit=3 type=eq
 * nsIndexIDListScanLimit: limit=4 flags=AND
 * nsIndexIDListScanLimit: limit=5
 * If the search filter is (&(objectclass=inetOrgPerson)(uid=foo)) then the limit=0 because all
 *  3 of type, flags, and value match
 * If the search filter is (objectclass=inetOrgPerson) then the limit=1 because type and value match
 *  but flag does not
 * If the search filter is (&(objectclass=posixAccount)(uid=foo)) the the limit=2 because type and
 *  flags match
 * If the search filter is (objectclass=posixAccount) then the limit=3 because only the type matches
 * If the search filter is (&(objectclass=*account*)(objectclass=*)) then the limit=4 because only
 *  flags match but not the types (sub and pres)
 * If the search filter is (objectclass=*account*) then the limit=5 because only the attribute matches
 *  but none of flags, type, or value matches
 */
#define AI_HAS_VAL 0x04
#define AI_HAS_TYPE 0x02
#define AI_HAS_FLAG 0x01
static int
index_get_allids(int *allids, const char *indextype, struct attrinfo *ai, const struct berval *val, unsigned int flags)
{
    int found = 0;
    Slapi_Value sval;
    struct index_idlistsizeinfo *iter; /* iterator */
    int cookie = 0;
    int best_score = 0;
    struct index_idlistsizeinfo *best_match = NULL;

    if (!ai->ai_idlistinfo) {
        return found;
    }

    if (val) { /* val should already be a Slapi_Value, but some paths do not use Slapi_Value */
        sval.bv.bv_val = val->bv_val;
        sval.bv.bv_len = val->bv_len;
        sval.v_csnset = NULL;
        sval.v_flags = SLAPI_ATTR_FLAG_NORMALIZED; /* the value must be a normalized key */
    }

    /* loop through all of the idlistinfo objects to find the best match */
    for (iter = (struct index_idlistsizeinfo *)dl_get_first(ai->ai_idlistinfo, &cookie); iter;
         iter = (struct index_idlistsizeinfo *)dl_get_next(ai->ai_idlistinfo, &cookie)) {
        int iter_score = 0;

        if (iter->ai_indextype != 0) { /* info defines a type which must match */
            if (is_indexed(indextype, iter->ai_indextype, ai->ai_index_rules)) {
                iter_score |= AI_HAS_TYPE;
            } else {
                continue; /* does not match, go to next one */
            }
        }
        if (iter->ai_flags != 0) {
            if (flags & iter->ai_flags) {
                iter_score |= AI_HAS_FLAG;
            } else {
                continue; /* does not match, go to next one */
            }
        }
        if (iter->ai_values != NULL) {
            if ((val != NULL) && slapi_valueset_find(&ai->ai_sattr, iter->ai_values, &sval)) {
                iter_score |= AI_HAS_VAL;
            } else {
                continue; /* does not match, go to next one */
            }
        }

        if (iter_score >= best_score) {
            best_score = iter_score;
            best_match = iter;
        }
    }

    if (best_match) {
        *allids = best_match->ai_idlistsizelimit;
        found = 1; /* found a match */
    }

    return found;
}
