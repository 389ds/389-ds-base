/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2008 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#ifdef MEMPOOL_EXPERIMENTAL

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <slap.h>
#include <prcountr.h>

struct mempool_object {
	struct mempool_object *mempool_next;
};

typedef int (*mempool_cleanup_callback)(void *object);

#ifdef SHARED_MEMPOOL
/* 
 * shared mempool among threads
 * contention causes the performance degradation
 * (Warning: SHARED_MEMPOOL code is obsolete)
 */
#define MEMPOOL_END NULL
static struct mempool {
	const char *mempool_name;
	struct mempool_object *mempool_head;
	PRLock *mempool_mutex;
	mempool_cleanup_callback mempool_cleanup_fn;
	unsigned long mempool_count;
} mempool[] = {
	{"2K", NULL, NULL, NULL, 0},
	{"4K", NULL, NULL, NULL, 0},
	{"8K", NULL, NULL, NULL, 0},
	{"16K", NULL, NULL, NULL, 0},
	{"32K", NULL, NULL, NULL, 0},
	{"64K", NULL, NULL, NULL, 0},
	{"128K", NULL, NULL, NULL, 0},
	{"256K", NULL, NULL, NULL, 0},
	{"512K", NULL, NULL, NULL, 0},
	{"1M", NULL, NULL, NULL, 0},
	{"2M", NULL, NULL, NULL, 0},
	{"4M", NULL, NULL, NULL, 0},
	{"8M", NULL, NULL, NULL, 0},
	{"16M", NULL, NULL, NULL, 0},
	{"32M", NULL, NULL, NULL, 0},
	{"64M", NULL, NULL, NULL, 0},
	{MEMPOOL_END, NULL, NULL, NULL, 0}
};
#else
/* 
 * mempool per thread; no lock is needed
 */
#define MAX_MEMPOOL 16
#define MEMPOOL_END 0
struct mempool {
	const char *mempool_name;
	struct mempool_object *mempool_head;
	mempool_cleanup_callback mempool_cleanup_fn;
	unsigned long mempool_count;
};

char *mempool_names[] =
{
	"2K", "4K", "8K", "16K", 
	"32K", "64K", "128K", "256K", 
	"512K", "1M", "2M", "4M", 
	"8M", "16M", "32M", "64M"
};
#endif

static PRUintn mempool_index;	/* thread private index used to store mempool
                                   in NSPR ThreadPrivateIndex */
static void mempool_destroy();

/*
 * mempool_init creates NSPR thread private index, 
 * then allocates per-thread-private.
 * mempool is initialized at the first mempool_return
 */
static void
mempool_init(struct mempool **my_mempool)
{
	int i;
	if (NULL == my_mempool) {
		return;
	} 
#ifdef SHARED_MEMPOOL
	for (i = 0; MEMPOOL_END != mempool[i].mempool_name; i++) {
		mempool[i].mempool_mutex = PR_NewLock();
		if (NULL == mempool[i].mempool_mutex) {
			PRErrorCode ec = PR_GetError();
			slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, "mempool", "mempool_init: "
				"failed to create mutex - (%d - %s); mempool(%s) is disabled",
				ec, slapd_pr_strerror(ec), mempool[i].mempool_name);
			rc = LDAP_OPERATIONS_ERROR;
		}
	}
#else
	PR_NewThreadPrivateIndex (&mempool_index, mempool_destroy);
	*my_mempool = (struct mempool *)slapi_ch_calloc(MAX_MEMPOOL, sizeof(struct mempool));
	for (i = 0; i < MAX_MEMPOOL; i++) {
		(*my_mempool)[i].mempool_name = mempool_names[i];
	}
#endif
}

/*
 * mempool_destroy is a callback which is set to NSPR ThreadPrivateIndex
 */
static void 
mempool_destroy()
{
	int i = 0;
	struct mempool *my_mempool;
#ifdef SHARED_MEMPOOL
	for (i = 0; MEMPOOL_END != mempool[i].mempool_name; i++) {
		struct mempool_object *object = NULL;
		if (NULL == mempool[i].mempool_mutex) {
			/* mutex is NULL; this mempool is not enabled */
			continue;
		}
		object = mempool[i].mempool_head;
		mempool[i].mempool_head = NULL;
		while (NULL != object) {
			struct mempool_object *next = object->mempool_next;
			if (NULL != mempool[i].mempool_cleanup_fn) {
				(mempool[i].mempool_cleanup_fn)((void *)object);
			}
			slapi_ch_free((void **)&object);
			object = next;
		}
		PR_DestroyLock(mempool[i].mempool_mutex);
		mempool[i].mempool_mutex = NULL;
	}
#else
	my_mempool = (struct mempool *)PR_GetThreadPrivate(mempool_index);
	if (NULL == my_mempool || my_mempool[0].mempool_name != mempool_names[0]) {
		/* mempool is not initialized */
		return;
	}
	for (i = 0; i < MAX_MEMPOOL; i++) {
		struct mempool_object *object = my_mempool[i].mempool_head;
		while (NULL != object) {
			struct mempool_object *next = object->mempool_next;
			if (NULL != my_mempool[i].mempool_cleanup_fn) {
				(my_mempool[i].mempool_cleanup_fn)((void *)object);
			}
			slapi_ch_free((void **)&object);
			object = next;
		}
		my_mempool[i].mempool_head = NULL;
		my_mempool[i].mempool_count = 0;
	}
	slapi_ch_free((void **)&my_mempool);
	PR_SetThreadPrivate (mempool_index, (void *)NULL);
#endif
}

/*
 * return memory to memory pool
 * (Callback cleanup function was intented to release nested memory in the 
 *  memory area.  Initially, memory had its structure which could point
 *  other memory area.  But the current code (#else) expects no structure.
 *  Thus, the cleanup callback is not needed)
 *  The current code (#else) uses the memory pool stored in the 
 *  per-thread-private data.
 */
int
mempool_return(int type, void *object, mempool_cleanup_callback cleanup)
{
	PR_ASSERT(type >= 0 && type < MEMPOOL_END);

	if (!config_get_mempool_switch()) {
		return LDAP_SUCCESS;	/* memory pool: off */
	}
#ifdef SHARED_MEMPOOL
	if (NULL == mempool[type].mempool_mutex) {
		/* mutex is NULL; this mempool is not enabled */
		return LDAP_SUCCESS;
	}
	PR_Lock(mempool[type].mempool_mutex);
	((struct mempool_object *)object)->mempool_next = mempool[type].mempool_head;
	mempool[type].mempool_head = (struct mempool_object *)object;
	mempool[type].mempool_cleanup_fn = cleanup;
	mempool[type].mempool_count++;
	PR_Unlock(mempool[type].mempool_mutex);
	return LDAP_SUCCESS;
#else
	{
	struct mempool *my_mempool;
	int maxfreelist;
	my_mempool = (struct mempool *)PR_GetThreadPrivate(mempool_index);
	if (NULL == my_mempool || my_mempool[0].mempool_name != mempool_names[0]) {
		/* mempool is not initialized */
		mempool_init(&my_mempool);
	} 
	((struct mempool_object *)object)->mempool_next = my_mempool[type].mempool_head;
	maxfreelist = config_get_mempool_maxfreelist();
	if ((maxfreelist > 0) && (my_mempool[type].mempool_count > maxfreelist)) {
		return LDAP_UNWILLING_TO_PERFORM;
	} else {
		((struct mempool_object *)object)->mempool_next = mempool[type].mempool_head;
		my_mempool[type].mempool_head = (struct mempool_object *)object;
		my_mempool[type].mempool_cleanup_fn = cleanup;
		my_mempool[type].mempool_count++;
		PR_SetThreadPrivate (mempool_index, (void *)my_mempool);
		return LDAP_SUCCESS;
	}
	}
#endif
}

/*
 * get memory from memory pool
 *  The current code (#else) uses the memory pool stored in the 
 *  per-thread-private data.
 */
void *
mempool_get(int type)
{
	struct mempool_object *object = NULL;
	struct mempool *my_mempool;
	PR_ASSERT(type >= 0 && type < MEMPOOL_END);

	if (!config_get_mempool_switch()) {
		return NULL;	/* memory pool: off */
	}
#ifdef SHARED_MEMPOOL
	if (NULL == mempool[type].mempool_mutex) {
		/* mutex is NULL; this mempool is not enabled */
		return NULL;
	}

	PR_Lock(mempool[type].mempool_mutex);
	object = mempool[type].mempool_head;
	if (NULL != object) {
		mempool[type].mempool_head = object->mempool_next;
		mempool[type].mempool_count--;
		object->mempool_next = NULL;
	}
	PR_Unlock(mempool[type].mempool_mutex);
#else
	my_mempool = (struct mempool *)PR_GetThreadPrivate(mempool_index);
	if (NULL == my_mempool || my_mempool[0].mempool_name != mempool_names[0]) {	/* mempool is not initialized */
		return NULL;
	} 

	object = my_mempool[type].mempool_head;
	if (NULL != object) {
		my_mempool[type].mempool_head = object->mempool_next;
		my_mempool[type].mempool_count--;
		object->mempool_next = NULL;
		PR_SetThreadPrivate (mempool_index, (void *)my_mempool);
	}
#endif
	return object;
}

/*****************************************************************************
 * The rest is slapi_ch_malloc and its friends, which are adjusted to mempool.
 * The challenge is mempool_return needs to know the size of the memory, but
 * free does not pass the info.  To work around it, malloc allocates the extra
 * space in front of the memory to be returned and store the size in the extra
 * space.  
 *
 * Also, to simplify the code, it allocates the smallest 2^n size which
 * could store the requested size.  We should make the granurality higher for
 * the real use.  
 *
 * Above 64MB, the functions call mmap directly.  The reason
 * why I chose mmap over mempool is in mempool, the memory stays until the
 * server is shutdown even if the memory is never be requested.  By using mmap,
 * the memory is returned to the system and it's guaranteed to shrink the 
 * process size.
 *
 * In this implementation, it changes the behavior based on the requested 
 * size (+ size space -- unsigned long)* :
 * 1B ~ 1KB: call system *alloc/free; but still it needs to store the size to
 *           support realloc.  The function needs to know if the passed address
 *           is the real address or shifted for the size.
 * 1KB + 1B ~ 64MB: use mempool
 * 64MB + 1B ~ : call mmap
 */
#include <sys/mman.h>
static int slapi_ch_munmap_no_roundup(void **start, unsigned long len);
char *slapi_ch_mmap(unsigned long len);

static int counters_created= 0;
PR_DEFINE_COUNTER(slapi_ch_counter_malloc);
PR_DEFINE_COUNTER(slapi_ch_counter_calloc);
PR_DEFINE_COUNTER(slapi_ch_counter_realloc);
PR_DEFINE_COUNTER(slapi_ch_counter_strdup);
PR_DEFINE_COUNTER(slapi_ch_counter_free);
PR_DEFINE_COUNTER(slapi_ch_counter_created);
PR_DEFINE_COUNTER(slapi_ch_counter_exist);

#define OOM_PREALLOC_SIZE  65536
static void *oom_emergency_area = NULL;
static PRLock *oom_emergency_lock = NULL;

#define SLAPD_MODULE	"memory allocator"

static const char* const oom_advice =
  "\nThe server has probably allocated all available virtual memory. To solve\n"
  "this problem, make more virtual memory available to your server, or reduce\n"
  "one or more of the following server configuration settings:\n"
  "  nsslapd-cachesize        (Database Settings - Maximum entries in cache)\n"
  "  nsslapd-cachememsize     (Database Settings - Memory available for cache)\n"
  "  nsslapd-dbcachesize      (LDBM Plug-in Settings - Maximum cache size)\n"
  "  nsslapd-import-cachesize (LDBM Plug-in Settings - Import cache size).\n"
  "Can't recover; calling exit(1).\n";

static void
create_counters()
{
	PR_CREATE_COUNTER(slapi_ch_counter_malloc,"slapi_ch","malloc","");
	PR_CREATE_COUNTER(slapi_ch_counter_calloc,"slapi_ch","calloc","");
	PR_CREATE_COUNTER(slapi_ch_counter_realloc,"slapi_ch","realloc","");
	PR_CREATE_COUNTER(slapi_ch_counter_strdup,"slapi_ch","strdup","");
	PR_CREATE_COUNTER(slapi_ch_counter_free,"slapi_ch","free","");
	PR_CREATE_COUNTER(slapi_ch_counter_created,"slapi_ch","created","");
	PR_CREATE_COUNTER(slapi_ch_counter_exist,"slapi_ch","exist","");

	/* ensure that we have space to allow for shutdown calls to malloc()
	 * from should we run out of memory.
	 */
	if (oom_emergency_area == NULL) {
	  oom_emergency_area = malloc(OOM_PREALLOC_SIZE);
	}
	oom_emergency_lock = PR_NewLock();
}

static void
log_negative_alloc_msg( const char *op, const char *units, unsigned long size )
{
	slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		"cannot %s %lu %s;\n"
		"trying to allocate 0 or a negative number of %s is not portable and\n"
		"gives different results on different platforms.\n",
		op, size, units, units );
}

static char *
slapi_ch_malloc_core( unsigned long	lsize )
{
	char	*newmem;

	if ( (newmem = (char *) malloc( lsize )) == NULL ) {
		int	oserr = errno;
	
		oom_occurred();
		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
			"malloc of %lu bytes failed; OS error %d (%s)%s\n",
			lsize, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	*(unsigned long *)newmem = lsize;
	newmem += sizeof(unsigned long);

	return newmem;
}

char *
slapi_ch_malloc( unsigned long	size )
{
	char	*newmem;
    unsigned long	lsize;

	if (size <= 0) {
		log_negative_alloc_msg( "malloc", "bytes", size );
		return 0;
	}

	lsize = size + sizeof(unsigned long);
	if (lsize <= 1024) {
		newmem = slapi_ch_malloc_core( lsize );
	} else if (lsize <= 67108864) {
		/* return 2KB ~ 64MB memory to memory pool */
		unsigned long roundup = 1;
		int n = 0;
		while (1) {
			roundup <<= 1;
			n++;
			if (roundup >= lsize) {
				break;
			}
		}
		PR_ASSERT(n >= 11 && n <= 26);
		newmem = (char *)mempool_get(n-11);	/* 11: 2^11 = 2K */
		if (NULL == newmem) {
			newmem = slapi_ch_malloc_core( roundup );
		}
	} else {
		newmem = slapi_ch_mmap( size );
	}

	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
	PR_INCREMENT_COUNTER(slapi_ch_counter_malloc);
	PR_INCREMENT_COUNTER(slapi_ch_counter_created);
	PR_INCREMENT_COUNTER(slapi_ch_counter_exist);

	return( newmem );
}

static char *
slapi_ch_realloc_core( char *block, unsigned long lsize )
{
	char	*realblock;
	char	*newmem;

	realblock = block - sizeof(unsigned long);
	if ( (newmem = (char *) realloc( realblock, lsize )) == NULL ) {
		int	oserr = errno;
	
		oom_occurred();
		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "realloc of %lu bytes failed; OS error %d (%s)%s\n",
			lsize, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	*(unsigned long *)newmem = lsize;
	newmem += sizeof(unsigned long);

	return newmem;
}

char *
slapi_ch_realloc( char *block, unsigned long size )
{
	char	*newmem;
    unsigned long lsize;
	unsigned long origsize;
	char *realblock;
	char *realnewmem;

	if ( block == NULL ) {
		return( slapi_ch_malloc( size ) );
	}

	if (size <= 0) {
		log_negative_alloc_msg( "realloc", "bytes", size );
		return block;
	}

	lsize = size + sizeof(unsigned long);
	if (lsize <= 1024) {
		newmem = slapi_ch_realloc_core( block, lsize );
	} else if (lsize <= 67108864) {
		/* return 2KB ~ 64MB memory to memory pool */
		unsigned long roundup = 1;
		int n = 0;
		while (1) {
			roundup <<= 1;
			n++;
			if (roundup >= lsize) {
				break;
			}
		}
		PR_ASSERT(n >= 11 && n <= 26);
		newmem = (char *)mempool_get(n-11);	/* 11: 2^11 = 2K */
		if (NULL == newmem) {
			newmem = slapi_ch_realloc_core( block, roundup );
		} else {
			realblock = block - sizeof(unsigned long);
			origsize = *(unsigned long *)realblock - sizeof(unsigned long);;
			memcpy(newmem, block, origsize);
			slapi_ch_free_string(&block);
		}
	} else {
		realblock = block - sizeof(unsigned long);
		origsize = *(unsigned long *)realblock - sizeof(unsigned long);;
		newmem = slapi_ch_mmap( size );
		memcpy(newmem, block, origsize);
		realnewmem = newmem - sizeof(unsigned long);
		*(unsigned long *)realnewmem = lsize;
		slapi_ch_free_string(&block);
	}
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_realloc);

	return( newmem );
}

static char *
slapi_ch_calloc_core( unsigned long	lsize )
{
	char	*newmem;

	if ( (newmem = (char *) calloc( 1, lsize )) == NULL ) {
		int	oserr = errno;
	
		oom_occurred();
		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "calloc of %lu bytes failed; OS error %d (%s)%s\n",
			lsize, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	*(unsigned long *)newmem = lsize;
	newmem += sizeof(unsigned long);

	return newmem;
}

char *
slapi_ch_calloc( unsigned long nelem, unsigned long size )
{
	char	*newmem;
    unsigned long	lsize;

	if (size <= 0) {
		log_negative_alloc_msg( "calloc", "bytes", size );
		return 0;
	}

	if (nelem <= 0) {
		log_negative_alloc_msg( "calloc", "elements", nelem );
		return 0;
	}

	lsize = nelem * size + sizeof(unsigned long);
	if (lsize <= 1024) {
		newmem = slapi_ch_calloc_core( lsize );
	} else if (lsize <= 67108864) {
		/* return 2KB ~ 64MB memory to memory pool */
		unsigned long roundup = 1;
		int n = 0;
		while (1) {
			roundup <<= 1;
			n++;
			if (roundup >= lsize) {
				break;
			}
		}
		PR_ASSERT(n >= 11 && n <= 26);
		newmem = (char *)mempool_get(n-11);	/* 11: 2^11 = 2K */
		if (NULL == newmem) {
			newmem = slapi_ch_calloc_core( roundup );
		} else {
			memset (newmem, 0, size * nelem);
		}
	} else {
		unsigned long mysize = size * nelem;
		newmem = slapi_ch_mmap( mysize );
		memset(newmem, 0, mysize);
	}
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_calloc);
    PR_INCREMENT_COUNTER(slapi_ch_counter_created);
    PR_INCREMENT_COUNTER(slapi_ch_counter_exist);

	return( newmem );
}

char *
slapi_ch_strdup ( const char* s1 )
{
    char* newmem;
    unsigned long	lsize;
	
	/* strdup pukes on NULL strings...bail out now */
	if(NULL == s1)
		return NULL;

	lsize = strlen(s1) + sizeof(unsigned long) + 1;
	newmem = slapi_ch_malloc( lsize );
	sprintf(newmem, "%s", s1);	

	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_strdup);
    PR_INCREMENT_COUNTER(slapi_ch_counter_created);
    PR_INCREMENT_COUNTER(slapi_ch_counter_exist);

    return newmem;
}

/*
 * Function: slapi_ch_free 
 *
 * Returns: nothing 
 *
 * Description: frees the pointer, and then sets it to NULL to 
 *              prevent free-memory writes. 
 *              Note: pass in the address of the pointer you want to free.
 *              Note: you can pass in null pointers, it's cool.
 *
 * Implementation: get the size from the size space, and determine the behavior
 *                 based upon the size:
 *      1B ~ 1KB: call system free
 *      1KB + 1B ~ 64MB: return memory to mempool
 *      64MB + 1B ~ : call munmap
 */
void 
slapi_ch_free(void **ptr)
{
	void *realptr;
	unsigned long size;

	if (ptr==NULL || *ptr == NULL){
		return;
	}

	realptr = (void *)((char *)*ptr - sizeof(unsigned long));
	size = *(unsigned long *)realptr;
	if (size <= 1024) {
		free (realptr);
	} else if (size <= 67108864) {
		/* return 2KB ~ 64MB memory to memory pool */
		unsigned long roundup = 1;
		int n = 0;
		int rc = LDAP_SUCCESS;
		while (1) {
			roundup <<= 1;
			n++;
			if (roundup >= size) {
				break;
			}
		}
        PR_ASSERT(n >= 11 && n <= 26);
        rc = mempool_return(n-11, *ptr, (mempool_cleanup_callback)NULL);
        if (LDAP_SUCCESS != rc) {
			free (realptr);
        }
	} else {
		slapi_ch_munmap_no_roundup( ptr, size );
	}
	*ptr = NULL;

	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_free);
    PR_DECREMENT_COUNTER(slapi_ch_counter_exist);
	return;
}

char *
slapi_ch_mmap(unsigned long len)
{
	char   *newmem;
	long   sc_page_size = config_get_system_page_size();
	int	   sc_page_bits = config_get_system_page_bits();
	unsigned long roundup = (len&(sc_page_size-1))?(((len>>sc_page_bits)+1)<<sc_page_bits):len;
	if ( (newmem = (char *)mmap(NULL, roundup, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0/*ignored */)) == MAP_FAILED ) {
		int	oserr = errno;

	  	oom_occurred();
		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "mmap of %lu bytes failed; OS error %d (%s)%s\n",
			roundup, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	*(unsigned long *)newmem = roundup;
	newmem += sizeof(unsigned long);
	return( newmem );
}

int
slapi_ch_munmap(void **start, unsigned long len)
{
	long   sc_page_size = config_get_system_page_size();
	int	   sc_page_bits = config_get_system_page_bits();
	unsigned long roundup = (len&(sc_page_size-1))?(((len>>sc_page_bits)+1)<<sc_page_bits):len;
	void  *realstart = *start - sizeof(unsigned long);
	int    rc = munmap(realstart, roundup);
	if (0 != rc) {
		int	oserr = errno;

		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "munmap of %lu bytes failed; OS error %d (%s)\n",
			roundup, oserr, slapd_system_strerror( oserr ) );
		/* Leaked. This should not happen */
	}
	*start = NULL;
	return rc;
}

static char *
slapi_ch_mmap_no_roundup( unsigned long	size)
{
	char	*newmem;
	unsigned long	mysize;

	if ( (newmem = (char *)mmap(NULL, size + sizeof(unsigned long),
					PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
					-1, 0/*ignored */)) == MAP_FAILED ) {
		int	oserr = errno;
	
		oom_occurred();
		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "mmap of %lu bytes failed; OS error %d (%s)%s\n",
			size + sizeof(unsigned long), oserr,
			slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	*(unsigned long *)newmem = size;
	newmem += sizeof(unsigned long);

	return newmem;
}

static int
slapi_ch_munmap_no_roundup(void **start, unsigned long len)
{
	void  *realstart = *start - sizeof(unsigned long);
	int    reallen = len + sizeof(unsigned long);
	int    rc = munmap(realstart, reallen);
	if (0 != rc) {
		int	oserr = errno;

		slapi_log_error(SLAPI_LOG_FATAL, LOG_ERR, SLAPD_MODULE,
		    "munmap of %lu bytes failed; OS error %d (%s)\n",
			len, oserr, slapd_system_strerror( oserr ) );
		/* Leaked. This should not happen */
	}
	*start = NULL;
	return rc;
}

/*
  This function is just like PR_smprintf.  It works like sprintf
  except that it allocates enough memory to hold the result
  string and returns that allocated memory to the caller.  The
  caller must use slapi_ch_free_string to free the memory.
  It should only be used in those situations that will eventually free
  the memory using slapi_ch_free_string e.g. allocating a string
  that will be freed as part of pblock cleanup, or passed in to create
  a Slapi_DN, or things of that nature.  If you have control of the
  flow such that the memory will be allocated and freed in the same
  scope, better to just use PR_smprintf and PR_smprintf_free instead
  because it is likely faster.
*/
/*
  This implementation is the same as PR_smprintf.  
  The above comment does not apply to this function for now.
  see [150809] for more details.
  WARNING - with this fix, this means we are now mixing PR_Malloc with
  slapi_ch_free.  Which is ok for now - they both use malloc/free from
  the operating system.  But if this changes in the future, this
  function will have to change as well.
*/
char *
slapi_ch_smprintf(const char *fmt, ...)
{
	char *p = NULL, *q = NULL;
	va_list ap;

	if (NULL == fmt) {
		return NULL;
	}

	va_start(ap, fmt);
	p = PR_vsmprintf(fmt, ap);
	va_end(ap);

	q = slapi_ch_strdup (p); 	/* ugly ...; hope there's any better way */
	free(p);

	return q;
}
#endif /* MEMPOOL_EXPERIMENTAL */
