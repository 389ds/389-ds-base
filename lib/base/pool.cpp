/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Generic pool handling routines.
 *
 * Hopefully these reduce the number of malloc/free calls.
 *
 *
 * Thread warning:
 *	This implementation is thread safe.  However, simultaneous 
 *	mallocs/frees to the same "pool" are not safe.  If you wish to
 *	use this module across multiple threads, you should define
 *	POOL_LOCKING which will make the malloc pools safe. 
 *
 * Mike Belshe
 * 11-20-95
 *
 */

#include "netsite.h"
#include "base/systems.h"
#include "base/systhr.h"

#ifdef MALLOC_POOLS
#include "base/pool.h"
#include "base/ereport.h"
#include "base/util.h"
#include "base/crit.h"

#include "base/dbtbase.h"

#ifdef DEBUG
#define POOL_ZERO_DEBUG
#endif
#undef POOL_LOCKING

#define BLOCK_SIZE		(32 * 1024)
#define MAX_FREELIST_SIZE	(BLOCK_SIZE * 32)

/* WORD SIZE 8 sets us up for 8 byte alignment. */
#define WORD_SIZE	8   
#undef	ALIGN
#define ALIGN(x)	( (x + WORD_SIZE-1) & (~(WORD_SIZE-1)) )

/* block_t
 * When the user allocates space, a BLOCK_SIZE (or larger) block is created in 
 * the pool.  This block is used until all the space is eaten within it.
 * When all the space is gone, a new block is created.
 *
 */
typedef struct block_t {
	char		*data;		/* the real alloc'd space */
	char		*start;		/* first free byte in block */
	char		*end;		/* ptr to end of block */
	struct block_t	*next;		/* ptr to next block */
} block_t;

/* pool_t
 * A pool is a collection of blocks.  The blocks consist of multiple 
 * allocations of memory, but a single allocation cannot be freed by
 * itself.  Once the memory is allocated it is allocated until the
 * entire pool is freed.
 */
typedef struct pool_t {
#ifdef DEBUG_CACHES
	time_t		time_created;
#endif
#ifdef POOL_LOCKING
	CRITICAL	lock;		/* lock for modifying the pool */
#endif
	block_t		*curr_block;	/* current block being used */
	block_t		*used_blocks;	/* blocks that are all used up */
	long		size;		/* size of memory in pool */
	struct pool_t	*next;		/* known_pools list */
} pool_t;

/* known_pools
 * Primarily for debugging, keep a list of all active malloc pools.
 */
static pool_t *known_pools = NULL;
static CRITICAL known_pools_lock = NULL;
static unsigned long pool_blocks_created = 0;
static unsigned long pool_blocks_freed = 0;

/* freelist
 * Internally we maintain a list of free blocks which we try to pull from
 * whenever possible.  This list will never have more than MAX_FREELIST_SIZE
 * bytes within it.
 */
static CRITICAL freelist_lock = NULL;
static block_t	*freelist = NULL;
static unsigned long	freelist_size = 0;
static unsigned long	freelist_max = MAX_FREELIST_SIZE;
static int pool_disable = 0;

int 
pool_internal_init()
{
	if (pool_disable == 0) {
		if (known_pools_lock == NULL) {
			known_pools_lock = crit_init();
			freelist_lock = crit_init();
		}
	}

	return 0;
}

static block_t *
_create_block(int size)
{
	block_t *newblock = NULL;
	long bytes = ALIGN(size);
	block_t	*free_ptr,
		*last_free_ptr = NULL;

	/* check freelist for large enough block first */

	crit_enter(freelist_lock);
	free_ptr = freelist;
	while(free_ptr && ((free_ptr->end - free_ptr->data) < bytes)) {
		last_free_ptr = free_ptr;
		free_ptr = free_ptr->next;
	}

	if (free_ptr) {
		newblock = free_ptr;
		if (last_free_ptr)
			last_free_ptr->next = free_ptr->next;
		else
			freelist = free_ptr->next;
		freelist_size -= (newblock->end - newblock->data);
		crit_exit(freelist_lock);
		bytes = free_ptr->end - free_ptr->data;
	}
	else {
                pool_blocks_created++;
		crit_exit(freelist_lock);
		if (((newblock = (block_t *)PERM_MALLOC(sizeof(block_t))) == NULL) || 
		    ((newblock->data = (char *)PERM_MALLOC(bytes)) == NULL)) {
			ereport(LOG_CATASTROPHE, XP_GetAdminStr(DBT_poolCreateBlockOutOfMemory_));
			if (newblock)
				PERM_FREE(newblock);
			return NULL;
		}
	}
	newblock->start	= newblock->data;
	newblock->end	= newblock->data + bytes;
	newblock->next	= NULL;

	return newblock;
}

/* Caller must hold lock for the pool */
static void 
_free_block(block_t *block)
{

#ifdef POOL_ZERO_DEBUG
	memset(block->data, 0xa, block->end-block->data);
#endif /* POOL_ZERO_DEBUG */

	if ((freelist_size + block->end - block->data) > freelist_max) {
		/* Just have to delete the whole block! */

		crit_enter(freelist_lock);
                pool_blocks_freed++;
		crit_exit(freelist_lock);

		PERM_FREE(block->data);
#ifdef POOL_ZERO_DEBUG
		memset(block, 0xa, sizeof(block));
#endif /* POOL_ZERO_DEBUG */

		PERM_FREE(block);
		return;
	}
	crit_enter(freelist_lock);
	freelist_size += (block->end - block->data);
	block->start = block->data;

	block->next = freelist;
	freelist = block;
	crit_exit(freelist_lock);
}

/* ptr_in_pool()
 * Checks to see if the given pointer is in the given pool.
 * If true, returns a ptr to the block_t containing the ptr;
 * otherwise returns NULL
 */
block_t * 
_ptr_in_pool(pool_t *pool, void *ptr)
{
	block_t *block_ptr = NULL;

	/* try to find a block which contains this ptr */

	if (	((char *)ptr < (char *)pool->curr_block->end) && 
		((char *)ptr >= (char *)pool->curr_block->data) ) 
		block_ptr = pool->curr_block;
	else 
		for(	block_ptr = pool->used_blocks; 
			block_ptr && 
			(((char *)ptr >= (char *)block_ptr->end) && 
			 ((char *)ptr < (char *)block_ptr->data)); 
			block_ptr = block_ptr->next);

	return block_ptr;
}


NSAPI_PUBLIC pool_handle_t *
pool_create()
{
	pool_t *newpool;

	if (pool_disable)
		return NULL;

	newpool = (pool_t *)PERM_MALLOC(sizeof(pool_t));

	if (newpool) {
		/* Have to initialize now, as pools get created sometimes
		 * before pool_init can be called...
		 */
		if (known_pools_lock == NULL) {
			known_pools_lock = crit_init();
			freelist_lock = crit_init();
		}

		if ( (newpool->curr_block =_create_block(BLOCK_SIZE)) == NULL) {
			ereport(LOG_CATASTROPHE, XP_GetAdminStr(DBT_poolCreateOutOfMemory_));
			PERM_FREE(newpool);
			return NULL;
		}
		newpool->used_blocks = NULL;
		newpool->size = 0;
		newpool->next = NULL;
#ifdef POOL_LOCKING
		newpool->lock = crit_init();
#endif
#ifdef DEBUG_CACHES
		newpool->time_created = time(NULL);
#endif

		/* Add to known pools list */
		crit_enter(known_pools_lock);
		newpool->next = known_pools;
		known_pools = newpool;
		crit_exit(known_pools_lock);
	}
	else 
		ereport(LOG_CATASTROPHE, XP_GetAdminStr(DBT_poolCreateOutOfMemory_1));

	return (pool_handle_t *)newpool;
}

NSAPI_PUBLIC void
pool_destroy(pool_handle_t *pool_handle)
{
	pool_t *pool = (pool_t *)pool_handle;
	block_t *tmp_blk;
	pool_t *last, *search;

	if (pool_disable)
		return;

	crit_enter(known_pools_lock);
#ifdef POOL_LOCKING
	crit_enter(pool->lock);
#endif

	if (pool->curr_block)
		_free_block(pool->curr_block);

	while(pool->used_blocks) {
		tmp_blk = pool->used_blocks;
		pool->used_blocks = pool->used_blocks->next;
		_free_block(tmp_blk);
	}

	/* Remove from the known pools list */
	for (last = NULL, search = known_pools; search; 
		last = search, search = search->next)
		if (search == pool) 
			break;
	if (search) {
		if(last) 
			last->next = search->next;
		else
			known_pools = search->next;
	
	}

#ifdef POOL_LOCKING
	crit_exit(pool->lock);
	crit_terminate(pool->lock);
#endif
	crit_exit(known_pools_lock);

#ifdef POOL_ZERO_DEBUG
	memset(pool, 0xa, sizeof(pool));
#endif /* POOL_ZERO_DEBUG */

	PERM_FREE(pool);

	return;
}


NSAPI_PUBLIC void *
pool_malloc(pool_handle_t *pool_handle, size_t size)
{
	pool_t *pool = (pool_t *)pool_handle;
	long reqsize, blocksize;
	char *ptr;

	if (pool == NULL || pool_disable) {
		return PERM_MALLOC(size);
	}

#ifdef DEBUG
	if (size == 0)
		return NULL;
#endif

#ifdef POOL_LOCKING
	crit_enter(pool->lock);
#endif

        reqsize = ALIGN(size);
	ptr = pool->curr_block->start;
	pool->curr_block->start += reqsize;

	/* does this fit into the last allocated block? */
	if (pool->curr_block->start > pool->curr_block->end) {

		/* Did not fit; time to allocate a new block */

		pool->curr_block->start -= reqsize;  /* keep structs in tact */

		pool->curr_block->next = pool->used_blocks;
		pool->used_blocks = pool->curr_block;
	
		/* Allocate a chunk of memory which is a multiple of BLOCK_SIZE
		 * bytes 
		 */
		blocksize = ( (size + BLOCK_SIZE-1) / BLOCK_SIZE ) * BLOCK_SIZE;
		if ( (pool->curr_block = _create_block(blocksize)) == NULL) {
			ereport(LOG_CATASTROPHE, XP_GetAdminStr(DBT_poolMallocOutOfMemory_));
#ifdef POOL_LOCKING
			crit_exit(pool->lock);
#endif
			return NULL;
		}

                ptr = pool->curr_block->start;
		reqsize = ALIGN(size);
		pool->curr_block->start += reqsize;
	}

	pool->size += reqsize;

#ifdef POOL_LOCKING
	crit_exit(pool->lock);
#endif
	return ptr;
}

void _pool_free_error()
{
	ereport(LOG_WARN, XP_GetAdminStr(DBT_freeUsedWherePermFreeShouldHaveB_));

	return;
}

NSAPI_PUBLIC void
pool_free(pool_handle_t *pool_handle, void *ptr)
{
	if (pool_handle == NULL || pool_disable) {
		PERM_FREE(ptr);
		return;
	}

#ifdef DEBUG
	/* Just to be nice, check to see if the ptr was allocated in a pool.
	 * If not, issue a warning and do a REAL free just to make sure that
	 * we don't leak memory.
	 */
	if ( !_ptr_in_pool((pool_t *)pool_handle, ptr) ) {
		_pool_free_error();

		PERM_FREE(ptr);
	}
#endif
	return;
}

NSAPI_PUBLIC void *
pool_calloc(pool_handle_t *pool_handle, size_t nelem, size_t elsize)
{
	void *ptr;

	if (pool_handle == NULL || pool_disable)
		return PERM_CALLOC(elsize * nelem);

	ptr = pool_malloc(pool_handle, elsize * nelem);
	if (ptr)
		memset(ptr, 0, elsize * nelem);
	return ptr;
}

NSAPI_PUBLIC void *
pool_realloc(pool_handle_t *pool_handle, void *ptr, size_t size)
{
	pool_t *pool = (pool_t *)pool_handle;
	void *newptr;
	block_t *block_ptr;
	int oldsize;

	if (pool_handle == NULL || pool_disable)
		return PERM_REALLOC(ptr, size);

	if ( (newptr = pool_malloc(pool_handle, size)) == NULL) 
		return NULL;

	/* With our structure we don't know exactly where the end
	 * of the original block is.  But we do know an upper bound
	 * which is a valid ptr.  Search the outstanding blocks
	 * for the block which contains this ptr, and copy...
	 */
#ifdef POOL_LOCKING
	crit_enter(pool->lock);
#endif

	if ( !(block_ptr = _ptr_in_pool(pool, ptr)) ) {
		/* User is trying to realloc nonmalloc'd space! */
		return newptr;
	}

	oldsize = block_ptr->end - (char *)ptr ;
	if (oldsize > size)
		oldsize = size;
	memmove((char *)newptr, (char *)ptr, oldsize);
#ifdef POOL_LOCKING
	crit_exit(pool->lock);
#endif

	return newptr;
}

NSAPI_PUBLIC char *
pool_strdup(pool_handle_t *pool_handle, const char *orig_str)
{
	char *new_str;
	int len = strlen(orig_str);

	if (pool_handle == NULL || pool_disable)
		return PERM_STRDUP(orig_str);

	new_str = (char *)pool_malloc(pool_handle, len+1);

	if (new_str) 
		memcpy(new_str, orig_str, len+1);

	return new_str;
}

NSAPI_PUBLIC long
pool_space(pool_handle_t *pool_handle)
{
	pool_t *pool = (pool_t *)pool_handle;

	return pool->size;
}

NSAPI_PUBLIC int pool_enabled()
{
#ifndef THREAD_ANY
	/* we don't have USE_NSPR defined so systhread_getdata is undef'ed */
	return 0;
#else
	if (pool_disable || (getThreadMallocKey() == -1) )
		return 0;

	if (!systhread_getdata(getThreadMallocKey()))
		return 0;

	return 1;
#endif
}


/* pool_service_debug()
 * NSAPI service routine to print state information about the existing 
 * pools.  Hopefully useful in debugging.
 *
 */
#define MAX_DEBUG_LINE	1024
#ifdef DEBUG_CACHES  /* XXXrobm causes entanglement in install and admserv cgis */
NSAPI_PUBLIC int
pool_service_debug(pblock *pb, Session *sn, Request *rq) 
{
	char tmp_buf[MAX_DEBUG_LINE];
	char cbuf[DEF_CTIMEBUF];
	int len;
	pool_t *pool_ptr;
	block_t *block_ptr;
	int pool_cnt, block_cnt;

	param_free(pblock_remove("content-type", rq->srvhdrs));
	pblock_nvinsert("content-type", "text/html", rq->srvhdrs);

	protocol_status(sn, rq, PROTOCOL_OK, NULL);
	protocol_start_response(sn, rq);

	len = util_sprintf(tmp_buf, "<H2>Memory pool status report</H2>\n");
	net_write(sn->csd, tmp_buf, len);

	len = util_sprintf(tmp_buf, "Note: The 0 block in each pool is \
the currently used block <P>\n");
	net_write(sn->csd, tmp_buf, len);

	len = util_sprintf(tmp_buf, "Freelist size: %d/%d<P>", freelist_size,
		freelist_max);
	net_write(sn->csd, tmp_buf, len);

	len = util_sprintf(tmp_buf, "Pool disabled: %d<P>", pool_disable);
	net_write(sn->csd, tmp_buf, len);

	len = util_sprintf(tmp_buf, "Blocks created: %d<P> Blocks freed: %d", 
		pool_blocks_created, pool_blocks_freed);
	net_write(sn->csd, tmp_buf, len);

	/* Create an HTML table */
	len = util_sprintf(tmp_buf, "<UL><TABLE BORDER=4>\n"); 
	net_write(sn->csd, tmp_buf, len);
	len = util_sprintf(tmp_buf, "<TH>Pool #</TH>\n");
	net_write(sn->csd, tmp_buf, len);
	len = util_sprintf(tmp_buf, "<TH>Pool size #</TH>\n");
	net_write(sn->csd, tmp_buf, len);
#ifdef DEBUG_CACHES
	len = util_sprintf(tmp_buf, "<TH>Time Created</TH>\n");
	net_write(sn->csd, tmp_buf, len);
#endif
	len = util_sprintf(tmp_buf, "<TH>Blocks</TH>\n");
	net_write(sn->csd, tmp_buf, len);

	crit_enter(known_pools_lock);
	for (pool_cnt = 0, pool_ptr = known_pools; pool_ptr; 
		pool_ptr = pool_ptr->next, pool_cnt++) {

#ifdef POOL_LOCKING
		crit_enter(pool_ptr->lock);
#endif
		len = util_snprintf(tmp_buf, MAX_DEBUG_LINE, 
#ifndef DEBUG_CACHES
"<tr align=right> <td>%d</td> <td>%d</td> <td> <TABLE BORDER=2> <TH>Block #</TH><TH>data</TH><TH>curr size</TH> <TH>max size</TH>\n", 
#else
"<tr align=right> <td>%d</td> <td>%d</td> <td>%s</td> <td> <TABLE BORDER=2> <TH>Block #</TH><TH>data</TH><TH>curr size</TH> <TH>max size</TH>\n", 
#endif
			pool_cnt, pool_space((pool_handle_t *)pool_ptr)
#ifdef DEBUG_CACHES
			, util_ctime(&(pool_ptr->time_created), cbuf, DEF_CTIMEBUF));
#else
			);
#endif
		net_write(sn->csd, tmp_buf, len);

		/* Print the first block */
		len = util_snprintf(tmp_buf, MAX_DEBUG_LINE, "\
<tr align=right> \
<td>%d</td> \
<td>%d</td> \
<td>%d</td> \
<td>%d</td> \
</tr>\n",
			0, pool_ptr->curr_block->data, 
			pool_ptr->curr_block->start -pool_ptr->curr_block->data,
			pool_ptr->curr_block->end - pool_ptr->curr_block->data);

		net_write(sn->csd, tmp_buf, len);

		for (block_cnt = 1, block_ptr = pool_ptr->used_blocks; block_ptr; 
			block_ptr = block_ptr->next, block_cnt++) {

			len = util_snprintf(tmp_buf, MAX_DEBUG_LINE, "\
<tr align=right> \
<td>%d</td> \
<td>%d</td> \
<td>%d</td> \
<td>%d</td> \
</tr>\n",
				block_cnt, block_ptr->data, 
				block_ptr->start - block_ptr->data, 
				block_ptr->end - block_ptr->data);

			net_write(sn->csd, tmp_buf, len);
		}
#ifdef POOL_LOCKING
		crit_exit(pool_ptr->lock);
#endif

		len = util_snprintf(tmp_buf, MAX_DEBUG_LINE, "</TABLE></TD></TR>");
		
		net_write(sn->csd, tmp_buf, len);
	}
	crit_exit(known_pools_lock);

	len = util_sprintf(tmp_buf, "</TABLE></UL>\n");
	net_write(sn->csd, tmp_buf, len);
	
	return REQ_PROCEED;

}
#endif /* 0 */
#endif /* MALLOC_POOLS */

