/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _PLIST_PVT_H
#define _PLIST_PVT_H

/*
 * FILE:        plist_pvt.h
 *
 * DESCRIPTION:
 *
 *      This file contains private definitions for the property list
 *      utility implementation.
 */

#include "base/pool.h"

/* Forward declarations */
typedef struct PLValueStruct_s PLValueStruct_t;
typedef struct PLSymbol_s PLSymbol_t;
typedef struct PLSymbolTable_s PLSymbolTable_t;
typedef struct PListStruct_s PListStruct_t;

/*
 * TYPE:        PLValueStruct_t
 *
 * DESCRIPTION:
 *
 *      This type represents a property value. It is dynamically
 *      allocated when a new property is added to a property list.
 *      It contains a reference to a property list that contains
 *      information about the property value, and a reference to
 *      the property value data.
 */

#include <stddef.h>

struct PLValueStruct_s {
    pb_entry pv_pbentry;	/* used for pblock compatibility */
    pb_param pv_pbparam;	/* property name and value pointers */
    PLValueStruct_t *pv_next;   /* property name hash collision link */
    PListStruct_t *pv_type;     /* property value type reference */
    int pv_pi;                  /* property index */
    int pv_flags;               /* bit flags */
};

#define pv_name pv_pbparam.name
#define pv_value pv_pbparam.value

/* pv_flags definitions */
#define PVF_MALLOC              0x1     /* allocated via MALLOC */

/* Offset to pv_pbparam in PLValueStruct_t */
#define PVPBOFFSET offsetof(struct PLValueStruct_s,pv_pbparam)

/* Convert pb_param pointer to PLValueStruct_t pointer */
#define PATOPV(p) ((PLValueStruct_t *)((char *)(p) - PVPBOFFSET))

/*
 * TYPE:        PLSymbolTable_t
 *
 * DESCRIPTION:
 *
 *      This type represents a symbol table that maps property names
 *      to properties.  It is dynamically allocated the first time a
 *      property is named.
 */

#define PLSTSIZES       {7, 19, 31, 67, 123, 257, 513}
#define PLMAXSIZENDX    (sizeof(plistHashSizes)/sizeof(plistHashSizes[0]))

struct PLSymbolTable_s {
    int pt_sizendx;             /* pt_hash size, as an index in PLSTSIZES */
    int pt_nsyms;               /* number of symbols in table */
    PLValueStruct_t *pt_hash[1];/* variable-length array */
};

/*
 * TYPE:        PListStruct_t
 *
 * DESCRIPTION:
 *
 *      This type represents the top-level of a property list structure.
 *      It is dynamically allocated when a property list is created, and
 *      freed when the property list is destroyed.  It references a
 *      dynamically allocated array of pointers to property value
 *      structures (PLValueStruct_t).
 */

#define PLIST_DEFSIZE   8       /* default initial entries in pl_ppval */
#define PLIST_DEFGROW   16      /* default incremental entries for pl_ppval */

struct PListStruct_s {
    pblock pl_pb;		/* pblock subset of property list head */
    PLSymbolTable_t *pl_symtab; /* property name to index symbol table */
    pool_handle_t *pl_mempool;  /* associated memory pool handle */
    int pl_maxprop;             /* maximum number of properties */
    int pl_resvpi;              /* number of reserved property indices */
    int pl_lastpi;              /* last allocated property index */
    int pl_cursize;             /* current size of pl_ppval in entries */
};

#define pl_initpi pl_pb.hsize    /* number of pl_ppval entries initialized */
#define pl_ppval pl_pb.ht	/* pointer to array of value pointers */

/* Convert pblock pointer to PListStruct_t pointer */
#define PBTOPL(p) ((PListStruct_t *)(p))

#define PLSIZENDX(i) (plistHashSizes[i])
#define PLHASHSIZE(i) (sizeof(PLSymbolTable_t) + \
                       (PLSIZENDX(i) - 1)*sizeof(PLValueStruct_t *))

extern int plistHashSizes[7];

extern int PListHashName(PLSymbolTable_t *symtab, const char *pname);

#endif /* _PLIST_PVT_H */
