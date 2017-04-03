/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/


/* See also https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html */

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

#include "bpt_cow.h"

sds_bptree_transaction *
sds_bptree_txn_create(sds_bptree_cow_instance *binst) {
    /* Allocate the transaction id
     * The trick here is that when we do this, we do txn_id + 1 to the txn, but
     * we don't alter the id of the binst. This is because when we commit the txn
     * we push the id into the binst then.
     */
    sds_bptree_transaction *btxn = sds_memalign(sizeof(sds_bptree_transaction), SDS_CACHE_ALIGNMENT);
    // Reference our creator.
    btxn->binst = binst;
    btxn->bi = binst->bi;
    // All transactions are created as writable
    btxn->state = SDS_TXN_WRITE;
    btxn->owned = NULL;
    btxn->created = NULL;

    if (binst->txn == NULL) {
        // Starting a new tree!
        btxn->txn_id = 1;
        btxn->root = sds_bptree_cow_node_create(btxn);
        btxn->root->level = 0;
    } else {
        // This means we have a txn, so use it!
        btxn->txn_id = binst->txn->txn_id + 1;
        btxn->root = binst->txn->root;
    }
    // We have no children yet as we aren't commited.
    btxn->child_txn = NULL;
    btxn->parent_txn = binst->txn;

    // The initial ref count is 0, and we only up to 1 when we commit.
    // Atomically set this to 0.
    __atomic_and_fetch(&(btxn->reference_count), 0, __ATOMIC_SEQ_CST);

#ifdef DEBUG
    // Update our needed checksums
    if (binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_btxn(btxn);
        sds_bptree_crc32c_update_node(btxn->root);
    }
#endif

    return btxn;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

// Should be caled by txn decrement.
static void
sds_bptree_txn_free(sds_bptree_transaction *btxn) {
#ifdef DEBUG
    sds_log("sds_bptree_txn_free", "        Freeing READ txn_%p rc %d", btxn, btxn->reference_count);
#endif
    sds_bptree_node *target_node = sds_bptree_node_list_pop(&(btxn->owned));
    // This frees only the nodes *related* to this txn.
    while (target_node != NULL) {
#ifdef DEBUG
        sds_log("sds_bptree_txn_free", "        READ txn_%p owned node node_%p", btxn, target_node);
#endif
        sds_bptree_node_destroy(btxn->binst->bi, target_node);
        // Right, what do we do here about binst!!!
        target_node = sds_bptree_node_list_pop(&(btxn->owned));
    }
    sds_free(btxn);
#ifdef DEBUG
    sds_log("sds_bptree_txn_free", "        Freed READ txn");
#endif
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

static void
sds_bptree_txn_increment(sds_bptree_transaction *btxn) {
    __atomic_add_fetch(&(btxn->reference_count), 1, __ATOMIC_SEQ_CST);

    // PR_AtomicIncrement(&(btxn->reference_count));
#ifdef DEBUG
    if (btxn->binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

static void
sds_bptree_txn_decrement(sds_bptree_transaction *btxn) {
#ifdef DEBUG
    sds_log("sds_bptree_txn_decrement", "    txn_%p %d - 1", btxn, btxn->reference_count );
#endif
    sds_bptree_cow_instance *binst = btxn->binst;

    // Atomic dec the counter.
    // PR_AtomicDecrement returns the set value.
    uint32_t result = __atomic_sub_fetch(&(btxn->reference_count), 1, __ATOMIC_SEQ_CST);
    /* WARNING: After this point, another thread MAY free btxn under us.
     * You MUST *not* deref btxn after this point.
     */
#ifdef DEBUG
    sds_log("sds_bptree_txn_decrement", "                        == %d", result );
    /* WARNING: This *may* in some cases trigger a HUAF ... 
     * Is this reason to ditch the checksum of the txn, or to make a txn lock?
     */
    if (result > 0 && binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_btxn(btxn);
    }
#endif
    // If the counter is 0 && we are the tail transaction.
    if (result == 0) {
        while (result == 0 && btxn != NULL && btxn == binst->tail_txn) {
#ifdef DEBUG
            sds_log("sds_bptree_txn_decrement", "    txn_%p has reached 0, and is at the tail, vacuumming!", btxn);
#endif
            binst->tail_txn = btxn->child_txn;
            sds_bptree_txn_free(btxn);
            // Now, we need to check to see if the next txn is ready to free also ....
            // I'm not sure if this is okay, as we may not have barriered properly.
            btxn = binst->tail_txn;
            // we decrement this txn by 1 to say "there is no more parents behind this"
            // as a result, 1 and ONLY ONE thread can be the one to cause this decrement, because
            // * there are more parents left, so we are > 0
            // * there are still active holders left, so we are > 0
            if (btxn != NULL) {
                result = __atomic_sub_fetch(&(btxn->reference_count), 1, __ATOMIC_SEQ_CST);
            }
        }
    }
#ifdef DEBUG
    if (btxn != NULL) {
        sds_log("sds_bptree_txn_decrement", "    txn_%p is at count %d, and is at the tail, NOT vacuumming!", btxn, result);
    } else {
        sds_log("sds_bptree_txn_decrement", "    txn tail is now NULL, NOT vacuumming!", btxn, result);
    }
#endif

#ifdef DEBUG
    // Update our needed checksums
    if (binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_cow_instance(binst);
    }
#endif
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

/* Public functions */

sds_result
sds_bptree_cow_rotxn_begin(sds_bptree_cow_instance *binst, sds_bptree_transaction **btxn) {

#ifdef DEBUG
    sds_result result = SDS_SUCCESS;
    if (binst->bi->offline_checksumming) {
        result = sds_bptree_crc32c_verify_cow_instance(binst);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_instance(binst->bi);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif

    /* Lock the read transaction. */
    pthread_rwlock_rdlock(binst->read_lock);
    // PR_Lock(binst->read_lock);
    /* Take a copy */
    *btxn = binst->txn;
    /* Increment. */
    if (*btxn != NULL) {
        sds_bptree_txn_increment(*btxn);
    }
    /* Unlock */
    pthread_rwlock_unlock(binst->read_lock);
#ifdef DEBUG
    if (*btxn != NULL) {
        sds_log("sds_bptree_cow_rotxn_begin", "==> Beginning READ txn_%p rc %d", *btxn, (*btxn)->reference_count);
    } else {
        sds_log("sds_bptree_cow_rotxn_begin", "==> Beginning READ txn FAILED. Likely that we are SHUTTING DOWN.");
    }
#endif
    if (*btxn == NULL) {
        return SDS_INVALID_TXN;
    }
    return SDS_SUCCESS;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

sds_result
sds_bptree_cow_rotxn_close(sds_bptree_transaction **btxn) {
    if (btxn == NULL || *btxn == NULL) {
        return SDS_INVALID_TXN;
    }
    /* Decrement the counter */
#ifdef DEBUG
    sds_log("sds_bptree_cow_rotxn_close", "==> Closing READ txn_%p rc %d - 1", *btxn, (*btxn)->reference_count);
#endif
    sds_bptree_txn_decrement(*btxn);
    /* Remove the callers reference to us. */
    *btxn = NULL;
    return SDS_SUCCESS;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

sds_result sds_bptree_cow_wrtxn_begin(sds_bptree_cow_instance *binst, sds_bptree_transaction **btxn) {
#ifdef DEBUG
    sds_result result = SDS_SUCCESS;
    if (binst->bi->offline_checksumming) {
        result = sds_bptree_crc32c_verify_cow_instance(binst);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_instance(binst->bi);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif
    // Take the write lock.
    pthread_mutex_lock(binst->write_lock);
    // Create the txn
    *btxn = sds_bptree_txn_create(binst);

#ifdef DEBUG
    sds_log("sds_bptree_cow_wrtxn_begin", "==> Beginning WRITE txn_%p", *btxn);
#endif

    return SDS_SUCCESS;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

sds_result sds_bptree_cow_wrtxn_abort(sds_bptree_transaction **btxn) {
    if (btxn == NULL || *btxn == NULL) {
        return SDS_INVALID_TXN;
    }
    // This transaction is defunct, mark it read only.
    (*btxn)->state = SDS_TXN_READ;
    // Unlock the write lock.
    pthread_mutex_unlock((*btxn)->binst->write_lock);
#ifdef DEBUG
    sds_log("sds_bptree_cow_wrtxn_abort", "==> Aborting WRITE txn_%p", *btxn);
#endif
    // Free and *remove* the list of nodes that we created, they are irrelevant!
    sds_bptree_node *node = sds_bptree_node_list_pop(&((*btxn)->created));
    while (node != NULL) {
        sds_bptree_node_destroy((*btxn)->binst->bi, node);
        node = sds_bptree_node_list_pop(&((*btxn)->created));
    }
    // Destroy the ownership list, we never really took these nodes.
    sds_bptree_node_list_release(&((*btxn)->owned));
    (*btxn)->owned = NULL;
    // Free the transaction.
    sds_bptree_txn_free(*btxn);
    /* Remove the callers reference to us */
    *btxn = NULL;
    return SDS_SUCCESS;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

sds_result sds_bptree_cow_wrtxn_commit(sds_bptree_transaction **btxn) {
    if (btxn == NULL || *btxn == NULL) {
        return SDS_INVALID_TXN;
    }
#ifdef DEBUG
    sds_log("sds_bptree_cow_wrtxn_commit", "==> Committing WRITE txn_%p", *btxn);
#endif

    // This prevents a huaf in decrement at the tail of this fn
    sds_bptree_transaction *parent_txn = (*btxn)->parent_txn;

#ifdef DEBUG
    sds_result result = SDS_SUCCESS;
    if ((*btxn)->binst->bi->offline_checksumming) {
        result = sds_bptree_crc32c_verify_btxn(*btxn);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_cow_instance((*btxn)->binst);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_crc32c_verify_instance((*btxn)->binst->bi);
        if (result != SDS_SUCCESS) {
            return result;
        }
        result = sds_bptree_map_nodes((*btxn)->bi, (*btxn)->root, sds_bptree_cow_verify_node);
        if (result != SDS_SUCCESS) {
            return result;
        }
    }
#endif
    // Take a reference to the last txn.
    // sds_bptree_transaction *btxn_last = btxn->binst->txn;
    // Mark the transaction read only now.
    (*btxn)->state = SDS_TXN_READ;
    // Give the ownership list to the last txn so it can cleanup correctly.
    parent_txn->owned = (*btxn)->owned;
    (*btxn)->owned = NULL;
    // Dump our creation list, we are past the point of no return.
    sds_bptree_node_list_release(&((*btxn)->created));

    // Take the read lock now at the last possible moment.
    pthread_rwlock_wrlock((*btxn)->binst->read_lock);
    // Say we are alive and commited - 2 means "our former transaction owns us"
    // and "we are the active root".
    uint32_t default_ref_count = 2;
    __atomic_store(&((*btxn)->reference_count), &default_ref_count, __ATOMIC_SEQ_CST);
    // Set it.
    (*btxn)->binst->txn = *btxn;
    // Update our parent to reference us.
    parent_txn->child_txn = *btxn;
#ifdef DEBUG
    // Update our needed checksums
    if ((*btxn)->binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_cow_instance((*btxn)->binst);
        sds_bptree_crc32c_update_btxn(*btxn);
        sds_bptree_crc32c_update_btxn((*btxn)->parent_txn);
    }
    sds_log("sds_bptree_cow_wrtxn_commit", "<== Commited WRITE txn_%p rc %d", *btxn, (*btxn)->reference_count);
#endif
    // Unlock the read
    pthread_rwlock_unlock((*btxn)->binst->read_lock);
    // Unlock the write
    pthread_mutex_unlock((*btxn)->binst->write_lock);
    // Decrement the last transaction.
    sds_bptree_txn_decrement(parent_txn);

    /* Remove the callers reference to us */
    *btxn = NULL;

    return SDS_SUCCESS;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */

sds_result
sds_bptree_cow_txn_destroy_all(sds_bptree_cow_instance *binst)
{
    sds_result result = SDS_SUCCESS;
    // Take the read lock!!!
    pthread_rwlock_wrlock(binst->read_lock);

    // At this point, the last transaction should "own" a few nodes from past trees.
    // Free the whole tree now.
    result = sds_bptree_map_nodes(binst->bi, binst->txn->root, sds_bptree_node_destroy);

    if (result != SDS_SUCCESS) {
        return result;
    }

    // !!! do I need to take the read and write lock here to block out issues?
    // Or do  Ineed a condvar or other flag?

    while (binst->tail_txn != NULL) {
        // Does this need to be an atomic set?
        // Perhaps we need a lock on the txn?
        binst->tail_txn->reference_count = 1;
        // Set the ref count to 1, and just do a decrement. Vacuum should catch
        sds_bptree_txn_decrement(binst->tail_txn);
        // these pretty quickly.
    }
    pthread_rwlock_unlock(binst->read_lock);

#ifdef DEBUG
    // Update our needed checksums
    if (binst->bi->offline_checksumming) {
        sds_bptree_crc32c_update_cow_instance(binst);
    }
#endif

    return result;
}

/* ========================= WARNING ============================
 * UNLESS YOU HAVE READ:
 *  https://www.kernel.org/doc/Documentation/memory-barriers.txt
 * and SERIOUSLY understand it, and how it works you *MUST* not
 * edit this file. This section of the code relies on a deep
 * understanding of locking and memory barriers.
 * ==============================================================
 */
