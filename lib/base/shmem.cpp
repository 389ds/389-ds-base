/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * shmem.h: Portable abstraction for memory shared among a server's workers
 * 
 * Rob McCool
 */


#include "shmem.h"

#if defined (SHMEM_UNIX_MMAP)

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <private/pprio.h>  /* for nspr20 binary release */

NSPR_BEGIN_EXTERN_C
#include <sys/mman.h>
NSPR_END_EXTERN_C

NSAPI_PUBLIC shmem_s *shmem_alloc(char *name, int size, int expose)
{
    shmem_s *ret = (shmem_s *) PERM_MALLOC(sizeof(shmem_s));
    char *growme;

    if( (ret->fd = PR_Open(name, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE, 0666)) == NULL) {
        PERM_FREE(ret);
        return NULL;
    }
    growme = (char *) PERM_MALLOC(size);
    ZERO(growme, size);
    if(PR_Write(ret->fd, (char *)growme, size) < 0) {
        PR_Close(ret->fd);
        PERM_FREE(growme);
        PERM_FREE(ret);
        return NULL;
    }
    PERM_FREE(growme);
    PR_Seek(ret->fd, 0, PR_SEEK_SET);
    if( (ret->data = (char *)mmap(NULL, size, PROT_READ | PROT_WRITE,
                          SHMEM_MMAP_FLAGS, PR_FileDesc2NativeHandle(ret->fd), 0)) == (caddr_t) -1)
    {
        PR_Close(ret->fd);
        PERM_FREE(ret);
        return NULL;
    }
    if(!expose) {
        ret->name = NULL;
        unlink(name);
    }
    else
        ret->name = STRDUP(name);
    ret->size = size;
    return ret;
}


NSAPI_PUBLIC void shmem_free(shmem_s *region)
{
    if(region->name) {
        unlink(region->name);
        PERM_FREE(region->name);
    }
    munmap((char *)region->data, region->size);  /* CLEARLY, C++ SUCKS */
    PR_Close(region->fd);
    PERM_FREE(region);
}

#elif defined (SHMEM_WIN32_MMAP)

#define PAGE_SIZE	(1024*8)
#define ALIGN(x)	( (x+PAGE_SIZE-1) & (~(PAGE_SIZE-1)) )
NSAPI_PUBLIC shmem_s *shmem_alloc(char *name, int size, int expose)
{
    shmem_s *ret = (shmem_s *) PERM_MALLOC(sizeof(shmem_s));
    HANDLE fHandle;

    ret->fd = 0; /* not used on NT */
  
    size = ALIGN(size);
    if( !(ret->fdmap = CreateFileMapping(
                           (HANDLE)0xffffffff,
                           NULL, 
                           PAGE_READWRITE,
                           0, 
                           size, 
                           name)) )
    {
        int err = GetLastError();
        PERM_FREE(ret);
        return NULL;
    }
    if( !(ret->data = (char *)MapViewOfFile (
                               ret->fdmap, 
                               FILE_MAP_ALL_ACCESS,
                               0, 
                               0, 
                               0)) )
    {
        CloseHandle(ret->fdmap);
        PERM_FREE(ret);
        return NULL;
    }
    ret->size = size;
    ret->name = NULL;

    return ret;
}


NSAPI_PUBLIC void shmem_free(shmem_s *region)
{
    if(region->name) {
        DeleteFile(region->name);
        PERM_FREE(region->name);
    }
    UnmapViewOfFile(region->data);
    CloseHandle(region->fdmap);
    PERM_FREE(region);
}

#endif /* SHMEM_WIN32_MMAP */
