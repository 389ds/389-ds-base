/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* slapi_ch_malloc.c - malloc routines that test returns from malloc and friends */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strdup */
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#undef DEBUG                    /* disable counters */
#include <prcountr.h>
#include "slap.h"

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

#if defined(_WIN32)
static int recording= 0;
#endif

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

#if defined(_WIN32) && defined(DEBUG)
static void add_memory_record(void *p,unsigned long size);
static void remove_memory_record(void *p);
static int memory_record_dump( caddr_t data, caddr_t arg );
static int memory_record_delete( caddr_t data, caddr_t arg );
#endif

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

/* called when we have just detected an out of memory condition, before
 * we make any other library calls.  Note that LDAPDebug() calls malloc,
 * indirectly.  By making 64KB free, we should be able to have a few
 * mallocs' succeed before we shut down.
 */
void oom_occurred(void)
{
  int tmp_errno = errno;  /* callers will need the error from malloc */
  if (oom_emergency_lock == NULL) return;

  PR_Lock(oom_emergency_lock);
  if (oom_emergency_area) {
    free(oom_emergency_area);
    oom_emergency_area = NULL;
  }
  PR_Unlock(oom_emergency_lock);
  errno = tmp_errno;
}

static void
log_negative_alloc_msg( const char *op, const char *units, unsigned long size )
{
	slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		"cannot %s %lu %s;\n"
		"trying to allocate 0 or a negative number of %s is not portable and\n"
		"gives different results on different platforms.\n",
		op, size, units, units );
}

char *
slapi_ch_malloc(
    unsigned long	size
)
{
	char	*newmem;

	if (size <= 0) {
		log_negative_alloc_msg( "malloc", "bytes", size );
		return 0;
	}

	if ( (newmem = (char *) malloc( size )) == NULL ) {
		int	oserr = errno;

	  	oom_occurred();
		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		    "malloc of %lu bytes failed; OS error %d (%s)%s\n",
			size, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_malloc);
    PR_INCREMENT_COUNTER(slapi_ch_counter_created);
    PR_INCREMENT_COUNTER(slapi_ch_counter_exist);
#if defined(_WIN32) && defined(DEBUG)
	if(recording)
	{
		add_memory_record(newmem,size);
	}
#endif

	return( newmem );
}

char *
slapi_ch_realloc(
    char		*block,
    unsigned long	size
)
{
	char	*newmem;

	if ( block == NULL ) {
		return( slapi_ch_malloc( size ) );
	}

	if (size <= 0) {
		log_negative_alloc_msg( "realloc", "bytes", size );
		return block;
	}

	if ( (newmem = (char *) realloc( block, size )) == NULL ) {
		int	oserr = errno;

	  	oom_occurred();
		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		    "realloc of %lu bytes failed; OS error %d (%s)%s\n",
			size, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_realloc);
#if defined(_WIN32) && defined(DEBUG)
	if(recording)
	{
		remove_memory_record(block);
		add_memory_record(newmem,size);
	}
#endif

	return( newmem );
}

char *
slapi_ch_calloc(
    unsigned long	nelem,
    unsigned long	size
)
{
	char	*newmem;

	if (size <= 0) {
		log_negative_alloc_msg( "calloc", "bytes", size );
		return 0;
	}

	if (nelem <= 0) {
		log_negative_alloc_msg( "calloc", "elements", nelem );
		return 0;
	}

	if ( (newmem = (char *) calloc( nelem, size )) == NULL ) {
		int	oserr = errno;

	  	oom_occurred();
		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		    "calloc of %lu elems of %lu bytes failed; OS error %d (%s)%s\n",
			nelem, size, oserr, slapd_system_strerror( oserr ), oom_advice );
		exit( 1 );
	}
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_calloc);
    PR_INCREMENT_COUNTER(slapi_ch_counter_created);
    PR_INCREMENT_COUNTER(slapi_ch_counter_exist);
#if defined(_WIN32) && defined(DEBUG)
	if(recording)
	{
		add_memory_record(newmem,size);
	}
#endif
	return( newmem );
}

char*
slapi_ch_strdup ( const char* s1)
{
    char* newmem;
	
	/* strdup pukes on NULL strings...bail out now */
	if(NULL == s1)
		return NULL;
	newmem = strdup (s1);
    if (newmem == NULL) {
		int	oserr = errno;
        oom_occurred();

		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		    "strdup of %lu characters failed; OS error %d (%s)%s\n",
			(unsigned long)strlen(s1), oserr, slapd_system_strerror( oserr ),
			oom_advice );
		exit (1);
    }
	if(!counters_created)
	{
		create_counters();
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_ch_counter_strdup);
    PR_INCREMENT_COUNTER(slapi_ch_counter_created);
    PR_INCREMENT_COUNTER(slapi_ch_counter_exist);
#if defined(_WIN32) && defined(DEBUG)
	if(recording)
	{
		add_memory_record(newmem,strlen(s1)+1);
	}
#endif
    return newmem;
}

struct berval*
slapi_ch_bvdup (const struct berval* v)
{
    struct berval* newberval = ber_bvdup ((struct berval *)v);
    if (newberval == NULL) {
		int	oserr = errno;

	  	oom_occurred();
		slapi_log_error( SLAPI_LOG_FATAL, SLAPD_MODULE,
		    "ber_bvdup of %lu bytes failed; OS error %d (%s)%s\n",
			(unsigned long)v->bv_len, oserr, slapd_system_strerror( oserr ),
			oom_advice );
		exit( 1 );
    }
    return newberval;
}

struct berval**
slapi_ch_bvecdup (struct berval** v)
{
    struct berval** newberval = NULL;
    if (v != NULL) {
	size_t i = 0;
	while (v[i] != NULL) ++i;
	newberval = (struct berval**) slapi_ch_malloc ((i + 1) * sizeof (struct berval*));
	newberval[i] = NULL;
	while (i-- > 0) {
	    newberval[i] = slapi_ch_bvdup (v[i]);
	}
    }
    return newberval;
}

/*
 *  Function: slapi_ch_free 
 *
 *  Returns: nothing 
 *
 *  Description: frees the pointer, and then sets it to NULL to 
 *               prevent free-memory writes. 
 *               Note: pass in the address of the pointer you want to free.
 *               Note: you can pass in null pointers, it's cool.
 */
void 
slapi_ch_free(void **ptr)
{
	if (ptr==NULL || *ptr == NULL){
	return;
	}

#if defined(_WIN32) && defined(DEBUG)
	if(recording)
	{
		remove_memory_record(*ptr);
	}
#endif
	free (*ptr);
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


/* just like slapi_ch_free, takes the address of the struct berval pointer */
void
slapi_ch_bvfree(struct berval** v)
{
	if (v == NULL || *v == NULL)
		return;

	slapi_ch_free((void **)&((*v)->bv_val));
	slapi_ch_free((void **)v);

	return;
}

/* just like slapi_ch_free, but the argument is the address of a string
   This helps with compile time error checking
*/
void
slapi_ch_free_string(char **s)
{
	slapi_ch_free((void **)s);
}

/* ========================= NT Specific Leak Checking Code ================================== */

#if defined(_WIN32) && defined(DEBUG)
#define STOP_TRAVERSAL -2 
#define MR_CALL_STACK 16
#define MR_DUMP_AMOUNT 16
static Avlnode *mr_tree= NULL;
static PRLock *mr_tree_lock= NULL;
#endif

void
slapi_ch_start_recording()
{
#if defined(_WIN32) && defined(DEBUG)
	if(mr_tree_lock==NULL)
	{
        mr_tree_lock = PR_NewLock();
	}
    PR_Lock( mr_tree_lock );
	recording= 1;
    PR_Unlock( mr_tree_lock );
#endif
}

void
slapi_ch_stop_recording()
{
#if defined(_WIN32) && defined(DEBUG)
    PR_Lock( mr_tree_lock );
	recording= 0;
    avl_apply( mr_tree, memory_record_dump, NULL, STOP_TRAVERSAL, AVL_INORDER );
    avl_free( mr_tree, memory_record_delete );
	mr_tree= NULL;
    PR_Unlock( mr_tree_lock );
#endif
}

#if defined(_WIN32) && defined(DEBUG)

struct memory_record
{
	void *p;
	unsigned long size;
	DWORD ra[MR_CALL_STACK];
};


static void
mr_to_hex_dump(char* dest, void *addr, int size, int MaxBytes)
{
	int i;
	for (i=0; i<MaxBytes; i++)
	{
		if(i<size)
		{
			wsprintf(dest+i*2, "%02x", ((unsigned char*)addr)[i]);
		}
		else
		{
			strcpy(dest+i*2, "  ");
		}
	}
}

static void
mr_to_char_dump(char* dest, void *addr, int size, int MaxBytes)
{
	int i;
	char *c= (char*)addr;
	for(i=0;i<MaxBytes;i++)
	{
		if(i<size)
		{
			*(dest+i)= (isprint(*c)?*c:'.');
			c++;
		}
		else
		{
			*(dest+i)= ' ';
		}
	}
	*(dest+i)= '\0';
}


/*
 * Check that the address is (probably) valid
 */
static int ValidateBP(UINT bp)
{
    return !(IsBadReadPtr((void*)bp, 4) || IsBadWritePtr((void*)bp, 4));
}

/*
 * Check that the address is (probably) valid
 */
static int ValidateIP(UINT ip)
{
    return !IsBadReadPtr((void*)ip, 4);
}

static int
memory_record_delete( caddr_t data, caddr_t arg )
{
    struct memory_record *mr = (struct memory_record *)data;
	free(mr);
    return 0;
}

static int
memory_record_duplicate_disallow( caddr_t d1, caddr_t d2 )
{
    return -1;
}

static int
memory_record_compare( caddr_t d1, caddr_t d2 )
{
    struct memory_record *mr1 = (struct memory_record *)d1;
    struct memory_record *mr2 = (struct memory_record *)d2;
	return (mr1->p==mr2->p);
}

static void
grab_stack(DWORD *ra,int framestograb,int framestoskip)
{
	int framelookingat = 0;
	int framestoring = 0;
	DWORD _bp = 0;

	/* for every function the frame layout is:
	 * ---------
	 * |ret add|
	 * ---------
	 * |old bp | <- new bp
	 * ---------
	 */

	__asm mov _bp, ebp;

	if(framestoskip==0)
	{
		ra[framestoring]= _bp;
		framestoring++;
	}
	while (framelookingat < framestograb+framestoskip-1)
	{
		DWORD returnAddress = *(((DWORD*)_bp)+1);
		_bp = *((DWORD*)_bp);
		if (!ValidateBP(_bp)) break;
		if (!ValidateIP(returnAddress)) break;
		if(framelookingat>=framestoskip)
		{
			ra[framestoring]= returnAddress;
			framestoring++;
		}
		framelookingat++;
	}
	ra[framestoring]= 0;
}

static void
add_memory_record(void *p,unsigned long size)
{
    struct memory_record *mr= (struct memory_record *)malloc(sizeof(struct memory_record));
	mr->p= p;
	mr->size= size;
	grab_stack(mr->ra,MR_CALL_STACK,1);
    PR_Lock( mr_tree_lock );
    avl_insert( &mr_tree, mr, memory_record_compare, memory_record_duplicate_disallow );
    PR_Unlock( mr_tree_lock );
}

static void
remove_memory_record(void *p)
{
    struct memory_record *mr = NULL;
    struct memory_record search;
    PR_Lock( mr_tree_lock );
    search.p= p;
    mr = (struct memory_record *)avl_find( mr_tree, &search, memory_record_compare );
	if(mr!=NULL)
	{
		avl_delete( &mr_tree, mr, memory_record_compare );
	}
    PR_Unlock( mr_tree_lock );
}

#include <imagehlp.h>
#pragma comment(lib, "imagehlp")

static BOOL SymInitialized= FALSE;
static HANDLE s_hProcess= NULL;

BOOL InitialiseImageHelp()
{
	if (!SymInitialized)
	{
		/*
		 * searchpath= <instancedir>\bin\slapd\server;<instancedir>\lib
		 */
		char *searchpath= NULL;
		char *id= config_get_instancedir();
		if(id!=NULL)
		{
			char *p= id;
			while(p!=NULL)
			{
				p= strchr(id,'/');
				if(p!=NULL) *p='\\';
			}
			p= strrchr(id,'\\');
			if(p!=NULL)
			{
				*p= '\0';
				searchpath= slapi_ch_malloc(100+strlen(p)*2);
				strcpy(searchpath,id);
				strcat(searchpath,"\\bin\\slapd\\server;");
				strcat(searchpath,id);
				strcat(searchpath,"\\lib");
			}
		}
		s_hProcess = GetCurrentProcess();
		SymInitialized = SymInitialize(s_hProcess, searchpath, TRUE);
		slapi_ch_free((void**)&id);
		slapi_ch_free((void**)&searchpath);
		if (SymInitialized)
		{
			SymSetOptions(SYMOPT_DEFERRED_LOADS);
		}
	}
	return SymInitialized;
}

BOOL AddressToName(DWORD Addr, LPTSTR Str, int Max)
{
	DWORD base;
	if (!InitialiseImageHelp())
		return FALSE;
	base = SymGetModuleBase(s_hProcess, Addr);
	if (base)
	{
		struct
		{
			IMAGEHLP_SYMBOL ihs;
			char NameBuf[256];
		} SymInfo;
		DWORD Displacement = 0;
		SymInfo.ihs.SizeOfStruct = sizeof(SymInfo);
		SymInfo.ihs.MaxNameLength = sizeof(SymInfo.NameBuf);
		if (SymGetSymFromAddr(s_hProcess, Addr, &Displacement, &SymInfo.ihs))
		{
			if (Displacement)
				_snprintf(Str, Max-1, "%s+%x", SymInfo.ihs.Name, Displacement);
			else
				_snprintf(Str, Max-1, "%s", SymInfo.ihs.Name);
			return TRUE;
		}
		else
		{
			_snprintf(Str, Max, "SymGetSymFromAddr failed (%d)", GetLastError());
		}
	}
	else
	{
		_snprintf(Str, Max, "SymGetModuleBase failed (%d)", GetLastError());
	}
	return FALSE;
}

static int
memory_record_dump( caddr_t data, caddr_t arg )
{
	int frame= 0;
	char b1[MR_DUMP_AMOUNT*2+1];
	char b2[MR_DUMP_AMOUNT+1];
	char b3[128];
	int size= 0;
    struct memory_record *mr = (struct memory_record *)data;
    if(!IsBadReadPtr(mr->p, MR_DUMP_AMOUNT))
	{
		size= MR_DUMP_AMOUNT;
	}
	mr_to_hex_dump(b1, mr->p, size, MR_DUMP_AMOUNT);
	mr_to_char_dump(b2, mr->p, size, MR_DUMP_AMOUNT);
	sprintf(b3,"%p %ld %s %s",mr->p,mr->size,b1,b2);
	LDAPDebug( LDAP_DEBUG_ANY, "%s\n",b3,0,0);
	while(mr->ra[frame]!=0)
	{
		char fn[100];
		AddressToName(mr->ra[frame], fn, 100);
		LDAPDebug( LDAP_DEBUG_ANY, "%d %p %s\n",frame,mr->ra[frame],fn);
		frame++;
	}
    return 0;
}

#endif


