/* BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#pragma once

/**
 * \defgroup sds_misc SDS Miscelaneous
 * Miscelaneous functions for SDS. This may be replaced by a PAL from ns-slapd soon.
 */

/**
 * \defgroup sds_queue SDS Queue
 * Unprotected queue for void types
 */

/**
 * \defgroup sds_tqueue SDS Thread Safe Queue
 * Thread safe queue, protected by platform mutexs.
 */

/**
 * \defgroup sds_lqueue SDS Lock Free Queue
 * Thread safe lock free queue. Falls back to platform mutex if cpu intrinsics are not present.
 */

/**
 * \defgroup sds_bptree SDS B+Tree
 * Unprotected B+Tree for void types. Supports fast set operations, search, insert, delete and build.
 *
 * This structure should be considered for all locations where unprotected AVL treels, or array based
 * binary search trees are used.
 */

/**
 * \defgroup sds_bptree_cow SDS Copy on Write B+Tree
 * Thread safe Copy of Write B+Tree. Supports parallel read transactions and serialised writes with ref counted members.
 *
 * This is a very important structure, and is the primary reason to consume this library. There are a number of important properties
 * that this tree provides.
 *
 * A key concept of this structure is that all values and keys contained have their lifetimes bound to the life of the tree and it's
 * nodes. This is due to the transactional nature of the structure. Consider a value that has been inserted into the tree. We take a
 * read transaction of the tree in thread A. Thread B now deletes the value that was inserted. Because A still refers to the value,
 * it is not freed until thread A closes it's read transaction. This guarantees that all read transactions are always valid, while
 * also maintaining that memory can be freed correctly. This has many applications including the Directory Server plugin subsystem.
 * We can use this property to guarantee a plugin exists for the life of an operation, and we only free it once all consumers have
 * completed their operations with it.
 *
 * Another key aspect of this tree is that write transactions *do not* block read transactions. Reads can be completed in parallel
 * to writes with complete safety. Write operations are serialised in this structure. This makes the structure extremely effective
 * for long read operations with many queries, without interrupting other threads.
 *
 * Along with the value and key ownership properties, we can guarantee that the content of the tree within a read transaction
 * will *not* be removed or added and will be consistent within the tree (however, we can't guarantee a user doesn't change the value.)
 *
 * As a comparison, the single thread tree is on paper, much faster than the COW version due to less memory operations. However in a
 * multi thread use case, the COW version is signifigantly faster *and* safer than a mutex protected single thread tree.
 *
 * This tree can be used as a form of object manager for C, as well as a way to allow async operations between threads while maintaining,
 * thread safety across an application. This assumes that the operations are largely parallel and batch read oriented over write heavy small
 * transactions.
 */

/** \addtogroup sds_misc
 * @{
 */

/**
 * sds_result encapsulates the set of possible success or failures an operation
 * may have during processing. You must always check this value from function
 * calls.
 */
typedef enum _sds_result {
    /**
     * SDS_SUCCESS 0 represents that the operation had no errors in processing.
     */
    SDS_SUCCESS = 0,
    /**
     * SDS_UNKNOWN_ERROR 1 represents that an unexpected issue was encountered,
     * you should raise a bug about this if you encounter it!
     */
    SDS_UNKNOWN_ERROR = 1,
    /**
     * SDS_NULL_POINTER 2 represents that a NULL pointer was provided to a function
     * that expects a true pointer to valid data. It may also represent that
     * the internals of a datastructure have been corrupted with an unexpected NULL
     * pointer, and the operation can not proceed.
     */
    SDS_NULL_POINTER = 2,
    /**
     * SDS_TEST_FAILED 3 represents that a test case has failed. You may need to
     * go back to the test case to see the originating error.
     */
    SDS_TEST_FAILED = 3,
    /**
     * SDS_DUPLICATE_KEY 4 represents that the key value already exists in the
     * datastructure. This is generally seen during insertion operations.
     */
    SDS_DUPLICATE_KEY = 4,
    /**
     * SDS_CHECKSUM_FAILURE 5 represents that the in memory data does not pass
     * checksum validation. This may be due to the fault of the application or
     * hardware errors.
     */
    SDS_CHECKSUM_FAILURE = 5,
    /**
     * SDS_INVALID_NODE_ID 6 represents that the internal data of a datastructure
     * node contains invalid identifying information.
     */
    SDS_INVALID_NODE_ID = 6,
    /**
     * SDS_INVALID_KEY 7 represents that a key is invalid in the state of this node.
     * generally this is due to a bug in the datastructure code where keys are
     * out of order, or incorrectly nulled.
     */
    SDS_INVALID_KEY = 7,
    /**
     * SDS_INVALID_VALUE_SIZE 8 represents that the size of the value provided
     * does not hold true for certain assumptions. For example, a null pointer
     * with a non zero size, or a pointer with a zero size.
     */
    SDS_INVALID_VALUE_SIZE = 8,
    /**
     * SDS_INVALID_POINTER 9 represents that within the datastructure an invalid
     * pointer structure has been created that may cause further dataloss or
     * corruption. This could be due to error in the datastructure code, or
     * memory issues.
     */
    SDS_INVALID_POINTER = 9,
    /**
     * SDS_INVALID_NODE 10 represents that certain datastructure properties of
     * this node have not been upheld. For example, branches should never contain
     * data in a b+tree, or that the capacity of the node is incorrect and a
     * split or merge should have occured.
     */
    SDS_INVALID_NODE = 10,
    /**
     * SDS_INVALID_KEY_ORDER 11 indicates that the ordering of keys within a
     * structure is incorrect, and may cause data loss.
     */
    SDS_INVALID_KEY_ORDER = 11,
    /**
     * SDS_RETRY 12 represents that the previous operation should be attempted
     * again. This is only for INTERNAL use. You will never see this returned.
     */
    SDS_RETRY = 12,
    /**
     * SDS_KEY_PRESENT 13 is a SUCCESS condition, similar to SDS_SUCCESS. This
     * differs from SDS_SUCCESS in that it shows that a key value exists within
     * some datastructure, and you must explcitly check for this case (vs
     * SDS_KEY_NOT_PRESENT).
     */
    SDS_KEY_PRESENT = 13,
    /**
     * SDS_KEY_NOT_PRESENT 14 is a SUCCESS condition, similar to SDS_SUCCESS. This
     * differs from SDS_SUCCESS in that is show that a key value does not exist
     * with some datastructure, but the operation was still a SUCCESS in completion.
     * You must explicitly check for this case to ensure you know the true result
     * of a function.
     */
    SDS_KEY_NOT_PRESENT = 14,
    /**
     * SDS_INCOMPATIBLE_INSTANCE 15 represents that during a set operation, you
     * are attempting to manipulate datastructures with potentiall different key
     * and value types. This would result in datacorruption or undefined behaviour
     * so instead, we return a pre-emptive error.
     */
    SDS_INCOMPATIBLE_INSTANCE = 15,
    /**
     * SDS_LIST_EXHAUSTED 16 represents that there are no more elements in this
     * datastructure to be consumed during an iteration.
     */
    SDS_LIST_EXHAUSTED = 16,
    /**
     * SDS_INVALID_TXN 17 represents that a transaction identifier is invalid
     * for the requested operation. IE using a read only transaction to attempt
     * a write.
     */
    SDS_INVALID_TXN = 17,
} sds_result;

/**
 * sds_malloc wraps the system memory allocator with an OOM check. During debugging
 * this call guarantees that memory is zeroed before use.
 *
 * \param size the amount of memory to allocate in bytes.
 * \retval pointer to the allocated memory.
 */
void *sds_malloc(size_t size);
/**
 * sds_calloc wraps the system calloc call with an OOM check. This call like calloc,
 * guarantees the returned memory is zeroed before usage.
 *
 * \param size the amount of memory to allocate in bytes.
 * \retval pointer to the allocated memory.
 */
void *sds_calloc(size_t size);
/**
 * sds_memalign wraps the posix_memalign function with an OOM and alignment check. During debugging
 * this call guarantees that memory is zeroed before use.
 *
 * \param size the amount of memory to allocate in bytes.
 * \param alignment the size to align to in bytes. Must be a power of 2.
 * \retval pointer to the allocated memory.
 */
void *sds_memalign(size_t size, size_t alignment);
/**
 * sds_free wraps the system free call with a null check.
 *
 * \param ptr the memory to free.
 */
void sds_free(void *ptr);

/**
 * sds_crc32c uses the crc32c algorithm to create a verification checksum of data.
 * This checksum is for data verification, not cryptographic purposes. It is used
 * largely in debugging to find cases when bytes in structures are updated incorrectly,
 * or to find memory bit flips during operation. If available, this will use the
 * intel sse4 crc32c hardware acceleration.
 *
 * \param crc The running CRC value. Initially should be 0. If in doubt, use 0.
 * \param data Pointer to the data to checksum.
 * \param length number of bytes to validate.
 * \retval rcrc The crc of this data. May be re-used in subsequent sds_crc32c calls
 * for certain datatypes.
 */
uint32_t sds_crc32c(uint32_t crc, const unsigned char *data, size_t length);

/**
 * sds_siphash13 provides an implementation of the siphash algorithm for use in
 * hash based datastructures. It is chosen due to is resilance against hash
 * attacks, such that it makes it higly secure as a choice for a hashmap.
 *
 * \param src The data to hash
 * \param src_sz The size of the data to hash
 * \param key The security key to mix with the hash. This should be randomised once
 * at startup and stored for use with further operations.
 * \retval The uint64_t representing the hash of the data.
 */
uint64_t sds_siphash13(const void *src, size_t src_sz, const char key[16]);

/**
 * sds_uint64_t_compare takes two void *, and treats them as uint64_t *.
 *
 * \param a The first uint64_t *.
 * \param b The second uint64_t *.
 * \retval result of the comparison.
 */
int64_t sds_uint64_t_compare(void *a, void *b);
/**
 * Free the allocated uint64_t * from sds_uint64_t_alloc.
 *
 * \param key Do nothing to the key.
 */
void sds_uint64_t_free(void *key);
/**
 * sds_uint64_t_dup Returns a copy of the uint64_t * to the caller.
 *
 * \param key The uint64_t * to copy.
 * \retval result the copy of the value.
 */
void *sds_uint64_t_dup(void *key);
/**
 * sds_strcmp exists to wrap the system strcmp call with the correct types needed
 * for libsds datastructures to operate correctly.
 *
 * \param a Pointer to the first string.
 * \param b Pointer to the second string.
 * \retval Difference between the values. 0 indicates they are the same.
 */
int64_t sds_strcmp(void *a, void *b);
/**
 * sds_strdup exists to wrap the system strdup call with the correct types needed for
 * libsds datastructures to operate correctly.
 *
 * \param key Pointer to the string to duplicate.
 * \retval Pointer to the newly allocated string.
 */
void *sds_strdup(void *key);


/**
 * @}
 */
/* end sds_misc */

/** \addtogroup sds_bptree_cow
 * @{
 */

/**
 * sds_txt_state lists the possible states of a transaction.
 */
typedef enum _sds_txn_state {
    /**
     * SDS_TXN_READ 0 This is a read only transaction. Not mutable actions may
     * be taken.
     */
    SDS_TXN_READ = 0,
    /**
     * SDS_TXN_WRITE 1 This is a read and write transaction. You may perform
     * all read operations, and also all write operations.
     */
    SDS_TXN_WRITE = 1,
} sds_txn_state;

/**
 * sds_bptree_node_list stores a linked list of sds_bptree_nodes for tracking.
 * Internally this is used extensively for transaction references.
 */
typedef struct _sds_bptree_node_list
{
    /**
     * checksum of the node list item.
     */
    uint32_t checksum;
    /**
     * Pointer to the node item.
     */
    struct _sds_bptree_node *node;
    /**
     * Pointer to the next node list item. NULL indicates list termination.
     */
    struct _sds_bptree_node_list *next;
} sds_bptree_node_list;

/**
 * sds_bptree_transaction Manages the content and lifetime of nodes within the tree. It is the basis of our
 * garbage collection system, using atomic reference counts to synchronise our behaviour.
 */
typedef struct _sds_bptree_transaction
{
    /**
     * Checksum of the data in this structure.
     */
    uint32_t checksum;
    /**
     * Reference count to the number of consumers of this transaction. This is
     * atomically updated. If this is the "active" primary transaction, this is
     * guaranteed to be 1 or greater. When a new write transaction is commited,
     * if this reference count falls to 0, this transaction is implicitly
     * destroy.
     */
    uint32_t reference_count;
    /**
     * The state of the transaction. All transactions start as SDS_WRITE_TXN,
     * and upon commit move to the SDS_READ_TXN state.
     */
    sds_txn_state state;
    /**
     * Pointer to the cow b+tree instance that created us.
     */
    struct _sds_bptree_cow_instance *binst;
    /**
     * Pointer to the b+tree instance that this transaction holds.
     */
    struct _sds_bptree_instance *bi;
    /**
     * The unique identifier of this transaction.
     */
    uint64_t txn_id;
    /**
     * The list of nodes that this transaction "owns". When the reference count
     * moves to 0, these nodes will be freed. This list is created during a write
     * transaction of "items that will not be needed when we are removed.".
     *
     * IE, when we have a txn A, then a new transaction B is made, we "copy" node
     * 1a to node 1b. Node 1b is "created" by txn B, and node 1a is "owend" by
     * txn A, because it's the last transaction that depends on this nodes existance.
     */
    sds_bptree_node_list *owned;
    /**
     * The list of nodes that this transaction "created". This is used during an
     * abort of the txn to roll back any changes that we made.
     */
    sds_bptree_node_list *created;
    /**
     * The current root node. Each transaction owns a unique root node which
     * anchors the branches of the copy on write structure.
     */
    struct _sds_bptree_node *root;
    /**
     * The previous transaction that we are derived from.
     */
    struct _sds_bptree_transaction *parent_txn;
    /**
     * The next transaction that derives from us.
     */
    struct _sds_bptree_transaction *child_txn;
} sds_bptree_transaction;

/**
 * @}
 */
/* end sds_bptree_cow */


/** \addtogroup sds_bptree
 * @{
 */

/**
 * SDS_BPTREE_DEFAULT_CAPACITY 5 represements that there are 5 values held per
 * in the B+Tree and COW B+Tree. A signifigant amount of testing went into the
 * tuning of this value for best performance.
 */
#define SDS_BPTREE_DEFAULT_CAPACITY 3
/**
 * SDS_BPTREE_HALF_CAPACITY 3 is pre-calculated as "ceiling(default capacity / 2)".
 * This is used to assert when node splits and merges should be taken.
 */
#define SDS_BPTREE_HALF_CAPACITY 2
/**
 * SDS_BPTREE_BRANCH 6 indicates that each node may potentially have up to 6 child
 * nodes. This yields a broad tree, that requires fewer cache misses to travese
 * (despite the higher number of comparisons to search).
 */
#define SDS_BPTREE_BRANCH 4

/**
 * This is the core of the B+Tree structures. This node represents the branches
 * and leaves of the structure.
 */
typedef struct _sds_bptree_node
{
#ifdef SDS_DEBUG
    /**
     * checksum of the structure data to detect errors. Must be the first element
     * in the struct.
     */
    uint32_t checksum;
#endif
    /**
     * Statically sized array of pointers to the keys of this structure.
     */
    void *keys[SDS_BPTREE_DEFAULT_CAPACITY];
    /**
     * Statically sized array of pointers to values. This is tagged by the level
     * flag. If level is 0, this is a set of values that have been inserted
     * by the consumer. If the level is > 0, this is the pointers to further
     * node structs.
     *
     * In a leaf, this is [value, value, value, value, value, link -> ]
     *
     * In a non-leaf, this is [link, link, link, link, link, link]
     */
    void *values[SDS_BPTREE_BRANCH];
    /**
     * The number of values currently stored in this structure.
     */
    uint32_t item_count;
    /**
     * This number of "rows" above the leaves this node is. 0 represents a true
     * leaf node, anything greater is a branch.
     */
    uint32_t level;
    /* Put these last because they are only used in the insert / delete path, not the search.
     * This way the keys and values are always in the cache associated with the ptr.
     */
    /**
     * The id of the transaction that created this node. This is used so that
     * within a transaction, we don't double copy values if we already copied
     * them.
     */
    uint64_t txn_id;
    /**
     * Back reference to our parent. This is faster than creating a traversal
     * list during each insertion (by a large factor).
     */
    struct _sds_bptree_node *parent;
} sds_bptree_node;

/**
 * The instance of the B+Tree. Stores references to function pointers for
 * manipulation of keys and values within the tree. Maintains the root checksum
 * which estabilshes the "root of trust" to all other nodes in the tree.
 */
typedef struct _sds_bptree_instance
{
    /**
     * checksum of the instance data.
     */
    uint32_t checksum;
    /**
     * This flag determines if we maintain and update checksum values during
     * tree operations, and that we verify these during the verification operation.
     */
    uint16_t offline_checksumming;
    /**
     * This flag determines if we verify all checksums during the read and write
     * paths of the code. Adds a large performance overhead, but guarantees
     * the data in the tree is "consistent".
     */
    uint16_t search_checksumming;
    /**
     * Internal tracking id for tree display.
     */
    uint16_t print_iter;

    /**
     * Pointer to the current tree root node.
     */
    sds_bptree_node *root; /* The current root node of the DB */

    /**
     * This function should be able to free values of the type that will be
     * inserted into the tree.
     */
    void (*value_free_fn)(void *value);
    /**
     * This function should be able to duplicate value of the type that will be
     * inserted into the tree. Values may be freed and duplicated in an order
     * you do not expect, so blindly returning a pointer to the same data may
     * not be wise if free then destroys it.
     */
    void *(*value_dup_fn)(void *value);
    /**
     * Key comparison function. This must return an int64_t of the difference in
     * the values. A result of < 0 indicates that a is less than b. A result of
     * 0 indicates that the values of a and b are identical. A result of > 0
     * indicates that a is greater than b.
     */
    int64_t (*key_cmp_fn)(void *a, void *b); /* Comparison function pointer */
    /**
     * Key free function. This must free the pointer provided by key.
     */
    void (*key_free_fn)(void *key);
    /**
     * Key duplication function. This duplicates the value of key and returns a
     * new pointer to it. This is used extensively in the tree structure, and
     * the key or result may be freed out of order, so don't blindly return the
     * same data.
     */
    void *(*key_dup_fn)(void *key);
} sds_bptree_instance;

/**
 * @}
 */
/* end sds_bptree */


/** \addtogroup sds_bptree_cow
 * @{
 */

/**
 * The copy on write tree instance. This stores the current active reading
 * transaction, number of active transactions, and pointers to the root of the
 * b+tree.
 *
 * This instance is the most important part of the tree and provides us with memory safety. Once we
 * have memory safety, individual transactions are free to garbage collect on their own. Garbage collection
 * is coordinated from the instance.
 *
 * ## MEMORY SAFTEY
 *
 * Within the transaction, we have a small number of important fields. These are:
 *
 * - read_lock
 * - write_lock
 * - tail_txn
 *
 * ### Read Transaction Begin
 *
 * At the beginning of a read transaction, we take a rolock on read_lock. This ensures that on
 * our working thread, that the correct sequence of barriers for memory consistency of the tree
 * is issued.
 *
 * Within the read_lock, we use cpu atomics to increment the transaction reference count by 1.
 * We then release the read_lock.
 *
 * In this way, we allow multiple read transactions to be initiated at the same time, with the
 * only serialisation point being the atomic increment.
 *
 * While the read transaction is "active" (IE the transaction that is listed in binst, where new
 * transactions will reference), the reference count will never go below 1.
 *
 * ### Write Transaction
 *
 * At the beginning of a write transaction, we take the write_lock mutex. This guarantees we are
 * the only writer in the tree at a point in time. Due to the way mutexes work, this guarantees
 * our memory barries of the tree are issued, so we see all other writes that have occured.
 *
 * During a write, we store a list of all nodes that were cloned: Both so that we know who we cloned
 * *from* but aso what nodes we *created*.
 *
 * ### Write Abort
 *
 * During a write abort, we release the write lock immediately. We then use the created node list, and
 * free all contents as they were never made active.
 *
 * ### Write commit
 *
 * During a commit, we prepare the transaction by changing it's state flags, and setting it's reference
 * count atomically. We relinquish the created list, because we have no need to free the nodes that are
 * about to become part of the tree. However, we update the previous transaction with the "owned" list,
 * as these are nodes we cloned from: IE these are nodes that serve no *future* role in the tree, so the
 * previous transaction is the last point where they are valid and needed - The last transaction now must
 * clean up after itself when it's ready to GC!
 *
 * We set our reference count to 2, to indicate that a following node (the previous active transaction)
 * relies on us, and that we are also active.
 *
 * We now take the write variant of the rw read_lock. This waits for all in progress read transactions to
 * begin, and we take exclusive access of this lock. We then pivot the older transaction out with our
 * transaction. The read_lock is released, and read transactions may now resume.
 *
 * At this point, we release the write_lock, and a new write may begin.
 *
 * We decrement the reference count of the previous transaction which may cause it to drop to 0. This is
 * the equivalent of a read transaction close. After this point, the previous transactions reference
 * count will only decrement, as previous holders close their transactions. Increment only occurs from the
 * active transaction!
 *
 * ### Read transaction close.
 *
 * When we close a read transaction, we require no locks. We use cpu atomic to set and fetch the
 * reference count.
 *
 * If the reference count remains positive, we complete, and continue.
 *
 * If the reference count drops to 0, we take a reference to the next transaction in the series,
 * and we free our node. We then decrement the reference counter of the next transaction and check
 * the result.
 *
 * Either, the reference count is > 0 because it is the active transaction *or* there is a current
 * read transaction.
 *
 * OR, the reference count reaches 0 because there are no active transactions, so we can free this node.
 * in the same operation, we then decrement the reference count to the next node in the series,
 * and we repeat this til we reach a node with a positive reference count.
 *
 * Due to the design of this system, we are guarantee no thread races, as one and only one thread
 * can ever set the reference count to 0: either the closing read, or the closing previous transaction.
 * This gives us complete thread safety, but without the need for a mutex to ensure this.
 *
 * ### Usage of the memory safe properties.
 *
 * Due to the way the barriers are issued, when you have a read transaction or a write transaction, you can
 * guarantee that the tree memory is consistent and correct, and that all values will remain alive until
 * you close the transaction. This means you can use this to allow parallel tasks in complex cases. Directory
 * Server has such a case with the plugin subsystem. Consider every operation needs a consistent list of plugins.
 *
 * You build a tree of the plugin references  (dlopen pointers, reference count, pblock) and store them.
 *
 * When you begin the operation you take a read transaction of this. This means every plugin in the operation is
 * guaranteed to be in the tree and held open for the duration of this operation. During the read, if another thread
 * commits the delete on a plugin, this drops the ref count, but not below 0 - the operation can continue safely.
 *
 * Once the operation is complete, the transaction is closed. At this point, the vacuum runs during close, which would
 * trigger the value free function. This would now actually close the plugin pointers, values. Additionally, the correct use
 * of the transaction guarantees the memory content of the plugin is correct due to the barriers inside of the transaction.
 *
 */
typedef struct _sds_bptree_cow_instance
{
    /**
     * checksum of the instance data.
     */
    uint32_t checksum;
    /**
     * Pointer to the current b+tree instance. This contains our function pointers
     * for free, dup of key and values.
     */
    sds_bptree_instance *bi;
    /**
     * The pointer to the current "latest" commited transaction. When a new read
     * transaction is taken, this is the value that is returned (until a new
     * write transaction is commited.)
     */
    sds_bptree_transaction *txn;
    /**
     * The pointer to the current "oldest" alive transaction. This is needed for
     * correct cleanup without corrupting intermediate trees.
     *
     * Consider we have a set of transactions, such as A -> B -> C. Between each
     * a set of changes has occured. We assume that if we have branches in these,
     * then during a cow operation they can be updated. So we end up in this
     * situation.
     *
     *        [ A ]         [ B ]         [ C ]
     *          |             |             |
     *      [ root a ]    [ root b ]    [ root c ]
     *       /     \\       /     \\       /     \\
     *     [ a ]  [ x ]  [ a ]  [ b ]  [ c ]  [ b ]
     *
     * Now, when we clone txn B, because C performed a cow on the leaf A, txn B
     * "owns" leaf A.
     * At this point, leaf A will be freed, which frees it from underneath
     * transaction A. We have corrupted the tree of a previous node!
     *
     * To prevent this, we only truly free a transaction when the ref count is 0
     * AND it's the last transaction in the list of live transactions. This way, B
     * even though ref count reached 0, would not be freed til A is also freed.
     *
     */
    sds_bptree_transaction *tail_txn;
    /**
     * The number of active transactions in the system.
     */
    uint64_t txn_count;
    /**
     * The transaction read lock. This is used to prevent corruption of the txn
     * pointer during a read or write transaction acquisition.
     */
    pthread_rwlock_t *read_lock;
    /**
     * The transaction write lock. During a write transaction, this is held for
     * the duration to allow only a single writer.
     */
    pthread_mutex_t *write_lock;
} sds_bptree_cow_instance;

/**
 * @}
 */
/* end sds_bptree_cow */

/* Queue operations */

/** \addtogroup sds_queue
 * @{
 */

/**
 * An internal queue node element.
 */
typedef struct _sds_queue_node
{
    /**
     * The data this node holds.
     */
    void *element;
    /**
     * The pointer to the next element of the queue.
     */
    struct _sds_queue_node *next;
    /**
     * The pointer to the previous element of the queue.
     */
    struct _sds_queue_node *prev;
} sds_queue_node;

/**
 * A queue that is internally a doubly linked list.
 */
typedef struct _sds_queue
{
    /**
     * The pointer to the current active head node. This is the "next" node that
     * will be dequeued and acted upon during the dequeue (pop) operation.
     */
    struct _sds_queue_node *head;
    /**
     * The tail of the queue. This is the "last" value that was inserted.
     */
    struct _sds_queue_node *tail;
    /**
     * If there are remaining nodes when the queue is destroyed, the queue will
     * be drained and this free function called on the value of within.
     */
    void (*value_free_fn)(void *value);
} sds_queue;

/**
 * Initialise a queue suitable for access within a single thread. Free function
 * must be set.
 *
 * \param q_ptr A pointer to the struct pointer you wish to allocate. This may be
 * on the heap or stack.
 * \param value_free_fn A function pointer to free values that are enqueued to
 * this structure.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_queue_init(sds_queue **q_ptr, void (*value_free_fn)(void *value));
/**
 * Enqueue an item to the tail of this queue. Equivalent to "push".
 *
 * \param q The struct pointer to the queue you wish to push to.
 * \param elem The data to enqueue.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_queue_enqueue(sds_queue *q, void *elem);
/**
 * Dequeue an item from the head of the queue. Equivalent to "pop".
 *
 * \param q The struct pointer to the queue you wish to pop from.
 * \param elem a pointer to a location which will be over-ridden with the data that
 * is dequeued. This may be NULLed, even during an error.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_queue_dequeue(sds_queue *q, void **elem);
/**
 * Indicate you are complete with the queue. Free and delete any remaining
 * internal structures, and free and still queued nodes. You must *always*
 * call this function to dispose of this structure.
 *
 * \param q The struct pointer to the queue you wish to dispose of.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_queue_destroy(sds_queue *q);

/**
 * @}
 */
/* end sds_queue */

/** \addtogroup sds_tqueue
 * @{
 */

/**
 * Implement a thread safe wrapper around an sds queue. This guarantees multithread
 * safety to the operations of the queue.
 */
typedef struct _sds_tqueue
{
    /**
     * Pointer to the underlying queue structure.
     */
    sds_queue *uq;
    /**
     * Lock that protects queue operations.
     */
    pthread_mutex_t lock;
} sds_tqueue;
/* Thread safe queue operations. */

/**
 * Initialise a queue suitable for access across multiple threads. Free function
 * must be set.
 *
 * \param q_ptr A pointer to the struct pointer you wish to allocate. This may be
 * on the heap or stack.
 * \param value_free_fn A function pointer to free values that are enqueued to
 * this structure.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_tqueue_init(sds_tqueue **q_ptr, void (*value_free_fn)(void *value));
/**
 * Enqueue an item to the tail of this queue. Equivalent to "push".
 * The safety of this operation is implicit, you do not need any other mutexes
 * to protect this operation.
 *
 * \param q The struct pointer to the queue you wish to push to.
 * \param elem The data to enqueue.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_tqueue_enqueue(sds_tqueue *q, void *elem);
/**
 * Dequeue an item from the head of the queue. Equivalent to "pop".
 * The safety of this operation is implicit, you do not need any other mutexes
 * to protect this operation.
 *
 * \param q The struct pointer to the queue you wish to pop from.
 * \param elem a pointer to a location which will be over-ridden with the data that
 * is dequeued. This may be NULLed, even during an error.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_tqueue_dequeue(sds_tqueue *q, void **elem);
/**
 * Indicate you are complete with the queue. Free and delete any remaining
 * internal structures, and free and still queued nodes. You must *always*
 * call this function to dispose of this structure.
 *
 * \param q The struct pointer to the queue you wish to dispose of.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_tqueue_destroy(sds_tqueue *q);

/**
 * @}
 */
/* end sds_tqueue */

/** \addtogroup sds_lqueue
 * @{
 */
/* Lock free queue operations. */

#ifdef ATOMIC_QUEUE_OPERATIONS
/* Mask the definition of the queue */
struct _sds_lqueue;
/**
 * Structure that provides a wrapper to the liblfds queue. This *must* be allocated
 * with memalign to a 128 byte boundary! The SDS library will *correctly* do this
 * for you!
 */
typedef struct _sds_lqueue sds_lqueue;
#else
/**
 * Type definition of lock free queue to the mutex queue in the case that cpu
 * intrinsics are not available, or the operating system is unsupported.
 */
typedef struct _sds_tqueue sds_lqueue;
#endif

/**
 * Initialise a queue suitable for access across multiple threads. Free function
 * must be set. This utilises hardware intrinsics to provide atomicity that may
 * be faster in highly contended applications. Use this structure in place of a
 * tqueue only if you know that content of the queue will be high.
 *
 * \param q_ptr A pointer to the struct pointer you wish to allocate. This may be
 * on the heap or stack.
 * \param value_free_fn A function pointer to free values that are enqueued to
 * this structure.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_lqueue_init(sds_lqueue **q_ptr, void (*value_free_fn)(void *value));
/**
 * Initialise this thread ready to use the lqueue. This is *critical*, and accessing
 * the queue without calling tprep may result in undefined behaviour.
 */
sds_result sds_lqueue_tprep(sds_lqueue *q);
/**
 * Enqueue an item to the tail of this queue. Equivalent to "push".
 * The safety of this operation is implicit, you do not need any other mutexes
 * to protect this operation.
 *
 * \param q The struct pointer to the queue you wish to push to.
 * \param elem The data to enqueue.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_lqueue_enqueue(sds_lqueue *q, void *elem);
/**
 * Dequeue an item from the head of the queue. Equivalent to "pop".
 * The safety of this operation is implicit, you do not need any other mutexes
 * to protect this operation.
 *
 * \param q The struct pointer to the queue you wish to pop from.
 * \param elem a pointer to a location which will be over-ridden with the data that
 * is dequeued. This may be NULLed, even during an error.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_lqueue_dequeue(sds_lqueue *q, void **elem);
/**
 * Indicate you are complete with the queue. Free and delete any remaining
 * internal structures, and free and still queued nodes. You must *always*
 * call this function to dispose of this structure.
 *
 * Important to note, is that all thread consumers of this queue must be stopped
 * before you call lqueue destroy. This is your responsibility to ensure this.
 *
 * \param q The struct pointer to the queue you wish to dispose of.
 * \retval sds_result to indicate the status of the operation.
 */
sds_result sds_lqueue_destroy(sds_lqueue *q);

/**
 * @}
 */
/* end sds_lqueue */

/*
 * How does the COW locking work? It depends on which operation is occuring.
 *
 * If we are beginning a search, first we take the read_lock. We then increment
 * txn ref_count, and release the lock after taking a reference.
 *
 * If we are doing a write, we take the write_lock, perform the operation. If
 * we roll back, release the write_lock.
 * If we commit, take the read_lock, now update the current active txn. Dec the
 * ref count to the former transaction.
 */

/** \addtogroup sds_bptree
 * @{
 */

/**
 * Initialise an sds b+tree for usage.
 *
 * \param binst_ptr Pointer to a struct pointer for the instance to initialise.
 * \param checksumming Flag to enable online and search checksumming. 0 to disable.
 * \param key_cmp_fn Key comparison function.
 * \param value_free_fn Function to free values that are assigned to this structure.
 * \param key_free_fn Function to free keys that are inside of this structure.
 * \param key_dup_fn Function to duplication keys within the structure.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_init(sds_bptree_instance **binst_ptr, uint16_t checksumming, int64_t (*key_cmp_fn)(void *a, void *b), void (*value_free_fn)(void *value), void (*key_free_fn)(void *key), void *(*key_dup_fn)(void *key));
/**
 * Bulk load data into a b+tree instance. The existing data in the b+tree instance
 * will be destroyed during the operation. Keys *must* be sorted prior to calling
 * this function. The index of items in values must match the corresponding key
 * in keys, or be NULL. values may be NULL, which is assumed that all keys have null
 * values associated. Count is the number of elements in keys and values. Key values
 * are owned by the tree from this point, you MUST NOT free them. They must be able to
 * be freed with your key_free_fn.
 *
 * If you must load a large amount of data to the tree, this operation is
 * signifigantly faster than repeated calls to insert, but relies on you using
 * an appropriate qsort function.
 *
 * \param binst Instance to purge and load.
 * \param keys Array of sorted keys to load.
 * \param values Array of sorted values to load.
 * \param count Number of values in the arrays.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_load(sds_bptree_instance *binst, void **keys, void **values, size_t count);

/* Operations */

/**
 * Destroy the instance. This frees all remaining values and keys from the structure.
 * After this is called, the bptree may not be accessed.
 *
 * \param binst Instance to destroy.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_destroy(sds_bptree_instance *binst);
/**
 * Search the instance for a key. Returns a SDS_KEY_PRESENT or SDS_KEY_NOT_PRESENT.
 *
 * \param binst Instance to search.
 * \param key Key to search for.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_search(sds_bptree_instance *binst, void *key);
/**
 * Retrieve the value associated with key from this structure.
 *
 * \param binst Instance to retrieve the value from.
 * \param key Key to search for.
 * \param target Destination for the value to end up in. May be NULLed even if the
 * key is not found.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_retrieve(sds_bptree_instance *binst, void *key, void **target);
/**
 * Delete a key and value from the tree. This implies that the values are freed
 * as part of this process.
 *
 * \param binst Instance to remove the key and value from.
 * \param key Key to delete, along with it's data.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_delete(sds_bptree_instance *binst, void *key);
/**
 * Insert the key and value to the tree. If the key already exists, this operation
 * will fail. The key is duplicated on insert, and the duplicated key's life is bound
 * to the tree. This allows you to use stack values as keys, as we duplicate them
 * on insert.
 *
 * \param binst Instance to insert into.
 * \param key Key to insert.
 * \param value Value to insert associated with key. May be NULL.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_insert(sds_bptree_instance *binst, void *key, void *value);
/**
 * Verify the contents of the tree are correct and sane. If checksumming is enabled,
 * this will validate all checksums. This is generally useful for debugging only
 * or if you think you have some data issue related to your key comparison function.
 *
 * \param binst Instance to verify.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_verify(sds_bptree_instance *binst);

/* Set manipulation */

/**
 * Map a function over the instance. This does not create a new instance, you
 * are expected to use the function to hand the data out to some other function.
 *
 * WARNING! This function will probably change soon!
 *
 * \param binst The instance to map over.
 * \param fn The function to be applied to each key-value pair.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_map(sds_bptree_instance *binst, void (*fn)(void *k, void *v));
/**
 * From instance a, and instance b, create a new insance that contains the
 * keys and values where keys exist in a or b but not both.
 *
 * \param binst_a Instance A
 * \param binst_b Instance B
 * \param binst_difference Output for a new instance containing clones of different elements.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_difference(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_difference);
/**
 * Union the sets A and B, and return a new instance that contains keys and values
 * that are present in either or both.
 *
 * \param binst_a Instance A
 * \param binst_b Instance B
 * \param binst_union Output for a new instance containing the elements from both sets.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_union(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_union);
/**
 * Intersect the sets A and B, and return the elements that are present in both
 * sets (but not either).
 *
 * \param binst_a Instance A
 * \param binst_b Instance B
 * \param binst_intersect Output for a new instance containing the elements that are intersecting.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_intersect(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_intersect);
/**
 * Return the set where elements that exist in A but not B only are returned.
 *
 * \param binst_a Instance A
 * \param binst_b Instance B
 * \param  binst_compliment Output for a new instance that contains elements unique to A.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_compliment(sds_bptree_instance *binst_a, sds_bptree_instance *binst_b, sds_bptree_instance **binst_compliment);
/**
 * Filter the set by applying a predicate, and return a new set containing the matching elements.
 *
 * \param binst_a Instance A
 * \param fn Predicate to apply. If this function returns 0 the element is excluded. All other values include the key/value.
 * \param binst_subset Output for a new instance containing the matched elements.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_filter(sds_bptree_instance *binst_a, int64_t (*fn)(void *k, void *v), sds_bptree_instance **binst_subset);

/**
 * @}
 */
/* end sds_bptree */

/*
sds_result sds_bptree_subset(struct sds_bptree_instance *binst_a, struct sds_bptree_instance *binst_b);
*/

/** \addtogroup sds_bptree_cow
 * @{
 */

// Similar for the COW versions

/**
 * Initialise an sds cow b+tree for usage. This allocates space for the tree
 * and bootstraps the initial blank transaction.
 *
 * \param binst_ptr The pointer you wish to have filled with the cow b+tree instance.
 * \param checksumming During DEBUG, this flag enables tree content checksumming.
 * \param key_cmp_fn Comparison function for keys in the tree.
 * \param value_free_fn During operation, the values assigned to this tree are owned by the instance.
 *               This allows us to free the values when required.
 * \param value_dup_fn During a copy on write operation, we need to be able to copy the values in the tree.
 * \param key_free_fn Free the key value
 * \param key_dup_fn Duplicate a key value
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_init(sds_bptree_cow_instance **binst_ptr, uint16_t checksumming, int64_t (*key_cmp_fn)(void *a, void *b), void (*value_free_fn)(void *value), void *(*value_dup_fn)(void *key), void (*key_free_fn)(void *key), void *(*key_dup_fn)(void *key));
/**
 * Destroy an instance. This will destroy all open transactions and free all
 * tree elements.
 *
 * \param binst  The cow b+tree to destroy.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_destroy(sds_bptree_cow_instance *binst);
/**
 * Verify an instance holds under a number of properties. This should only be used
 * in debbuging issues. If you find an issue, add it to the test cases!
 *
 * \param binst The cow b+tree to verify.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_verify(sds_bptree_cow_instance *binst);

/**
 * Begin a read only transaction on the tree. This guarantees the memory consistency
 * of all values in the tree for the duration of the operation.
 *
 * \param binst The cow b+tree to start a transaction in.
 * \param btxn The pointer to a transaction to be allocated.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_rotxn_begin(sds_bptree_cow_instance *binst, sds_bptree_transaction **btxn);
/**
 * Complete a read only transaction. After you have closed the transaction, it may
 * not be used again. This may trigger a garbage collection. After you close the
 * transaction, you may NOT reference any elements you viewed in the tree. Given
 * that there is no penalty to holding this open, just keep the transaction open
 * til you are sure you do not need the values any longer.
 *
 * \param btxn The transaction to close.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_rotxn_close(sds_bptree_transaction **btxn);
/**
 * Begin a write transaction. This allows you to alter the content of the tree.
 * Due to the exclusive nature of this transaction, it is best if you are able
 * to keep this transaction for the "minimal" time possible as it is serialised.
 * Changes made in a write transaction are *guaranteed* to have no impact on any
 * types currently in a read transaction.
 *
 * \param binst The b+tree to begin a write transaction in.
 * \param btxn The pointer to a location for the transaction to be created into.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_wrtxn_begin(sds_bptree_cow_instance *binst, sds_bptree_transaction **btxn);
/**
 * Abort and roll back the changes made during a write transaction. This operation
 * is guaranteed safe to all other transactions, including future writes. After the
 * abort function is called, you must *NOT* access this transaction again.
 *
 * \param btxn The transaction to abort and destroy.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_wrtxn_abort(sds_bptree_transaction **btxn);
/**
 * Commit the transaction. After this operation, the transaction must not be accessed
 * again. All changes to the tree are now visible after this call is made, and new
 * read transactions will have this view. Commit does not affect pre-existing
 * read transactions data.
 *
 * \param btxn The transaction to commit.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_wrtxn_commit(sds_bptree_transaction **btxn);

/**
 * Search a tree with a valid transaction reference. This returns KEY_PRESENT
 * or KEY_NOT_PRESENT if the search suceeds or not. Search may operation on a valid
 * uncommited write transaction, or a read transaction.
 *
 * \param btxn The transaction point to search.
 * \param key The key to search for.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_search(sds_bptree_transaction *btxn, void *key);
/**
 * Search a tree with a valid transaction reference. This returns KEY_PRESENT
 * or KEY_NOT_PRESENT if the search suceeds or not. Additionally, the value
 * attached to key is placed into the pointer for target. Search may operation on a valid
 * uncommited write transaction, or a read transaction.
 *
 * \param btxn The transaction point to search.
 * \param key The key to search for.
 * \param target The pointer where value will be placed on sucessful search. NULL is a
 *  valid value, so be sure to check the result.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_retrieve(sds_bptree_transaction *btxn, void *key, void **target);
/**
 * Delete key and the associated data from the tree. This operates only on a valid
 * write transaction, and changes made are not reflected until a commit is made.
 * existing reads will never see this change til they close and open new transactions.
 *
 * \param btxn The write transaction to operate on.
 * \param key The key to delete, along with associated data. If you have called retrieve
 * on this key, the key and value must not be accessed after this point within this
 * transactions lifetime as the values may be invalidated.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_delete(sds_bptree_transaction *btxn, void *key);
/**
 * Insert a key and associated value to the tree via a valid write transaction.
 * values and keys inserted to the tree now completely belong to the tree, and may
 * be duplicated or freed at any time. After you have given a key and value to the
 * tree, you must only access them via the retrieve interface in valid scenarios.
 *
 * \param btxn The write transaction to operate on.
 * \param key The key to insert. If a duplicate key is detected, and error is returned.
 * \param value The value to insert. May be NULL.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_insert(sds_bptree_transaction *btxn, void *key, void *value);

/**
 * Update a key to have a new associated value within a valid write transaction.
 * This is more efficient than delete -> insert, so for updates it's preferred.
 * This is needed in the case that you have previous read transactions, and want
 * to alter a value, without affecting the read. You would use this by calling
 * retrieve, copying the value, then calling update on the b+tree.
 *
 * \param btxn The write transaction to operate on.
 * \param key The key to update. If the key does not exist, fall back to insert.
 * \param value The value to update. May be NULL.
 * \retval Result of the operation as sds_result.
 */

sds_result sds_bptree_cow_update(sds_bptree_transaction *btxn, void *key, void *value);

/**
 * Search atomic functions as search, but implies a single short lived read transaction.
 *
 * If you have multiple searches to make, it is better to use a read transaction due to
 * the memory design of the transaction. Multiple atomics may cause contention on
 * certain parts of the transaction code.
 *
 * \param binst The cow b+tree to search.
 * \param key The key to search for.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_search_atomic(sds_bptree_cow_instance *binst, void *key);
/**
 * Retrieve atomic functions as retrieve, but implies a single short lived read transaction. Calling this implies that you *must* free tha value returned to you.
 *
 * If you have multiple searches to make, it is better to use a read transaction due to
 * the memory design of the transaction. Multiple atomics may cause contention on
 * certain parts of the transaction code.
 *
 * \param binst The cow b+tree to search.
 * \param key The key to search for.
 * \param target The value retrieved. You must free this after use, with the same free function as the binst holds.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_retrieve_atomic(sds_bptree_cow_instance *binst, void *key, void **target);
/**
 * Delete atomic functions as delete, but implise a single short livied write transaction, and commit phase.
 *
 * If you have multiple searches to make, it is better to use a write transaction due to
 * the memory design of the transaction. Multiple atomics may cause contention on
 * certain parts of the transaction code.
 *
 * \param binst The cow b+tree to delete from.
 * \param key The key to delete. This removes the associated value.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_delete_atomic(sds_bptree_cow_instance *binst, void *key);
/**
 * Insert atomic functions as insert, but implies a single short lived write transaction and commit phase.
 *
 * If you have multiple searches to make, it is better to use a write transaction due to
 * the memory design of the transaction. Multiple atomics may cause contention on
 * certain parts of the transaction code.
 *
 * \param binst The cow b+tree to insert to.
 * \param key The key to insert.
 * \param value The value to insert associated with key.
 * \retval Result of the operation as sds_result.
 */
sds_result sds_bptree_cow_insert_atomic(sds_bptree_cow_instance *binst, void *key, void *value);


/**
 * @}
 */
/* end sds_bptree_cow */

#define HT_SLOTS 16

typedef enum _sds_ht_slot_state {
    SDS_HT_EMPTY = 0,
    SDS_HT_VALUE = 1,
    SDS_HT_BRANCH = 2,
} sds_ht_slot_state;

/**
 * ht values
 */
typedef struct _sds_ht_value
{
    uint32_t checksum;  /**< the checksum */
    void *key;  /**< the key */
    void *value;  /**< the key value */
    // may make this a LL of values later for collisions
} sds_ht_value;

/**
 * ht slot
 */
typedef struct _sds_ht_slot
{
    sds_ht_slot_state state; /**< the checksum */
    union
    {
        sds_ht_value *value;
        struct _sds_ht_node *node;
    } slot;  /**< slot union */
} sds_ht_slot;

/**
 *  ht node
 */
typedef struct _sds_ht_node
{
    uint32_t checksum; /**< the checksum */
    uint64_t txn_id; /**< transaction id */
    uint_fast32_t count; /**< the count */
#ifdef SDS_DEBUG
    uint64_t depth;
#endif
    struct _sds_ht_node *parent; /**< the parent */
    size_t parent_slot; /**< the parent slot */
    sds_ht_slot slots[HT_SLOTS]; /**< the slots */
} sds_ht_node;

/**
 *  ht instance
 */
typedef struct _sds_ht_instance
{
    uint32_t checksum; /**< the checksum */
    char hkey[16]; /**< the key */
    sds_ht_node *root; /**< the root */
    int64_t (*key_cmp_fn)(void *a, void *b); /**< the keycompare function */
    uint64_t (*key_size_fn)(void *key); /**< the key size function */
    void *(*key_dup_fn)(void *key); /**< the key dup function */
    void (*key_free_fn)(void *key); /**< the key free function */
    void *(*value_dup_fn)(void *value); /**< the value dup function */
    void (*value_free_fn)(void *value); /**< the value free function */
} sds_ht_instance;

uint64_t sds_uint64_t_size(void *key);

sds_result
sds_ht_init(sds_ht_instance **ht_ptr,
            int64_t (*key_cmp_fn)(void *a, void *b),
            void (*value_free_fn)(void *value),
            void *(*key_dup_fn)(void *key),
            void (*key_free_fn)(void *key),
            uint64_t (*key_size_fn)(void *key));

sds_result sds_ht_insert(sds_ht_instance *ht_ptr, void *key, void *value);
sds_result sds_ht_search(sds_ht_instance *ht_ptr, void *key, void **value);
sds_result sds_ht_delete(sds_ht_instance *ht_ptr, void *key);
sds_result sds_ht_verify(sds_ht_instance *ht_ptr);
sds_result sds_ht_destroy(sds_ht_instance *ht_ptr);
