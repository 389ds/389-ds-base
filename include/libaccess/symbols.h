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

#ifndef __symbols_h
#define __symbols_h

/*
 * Description (symbols.h)
 *
 *	This file describes the interface to an ACL symbol table
 *	implementation.  The symbol table provides for storing symbols
 *	keyed by name and type, creating a separate name space for
 *	each symbol type.
 */

#ifdef __PRIVATE_SYMBOLS

#include "plhash.h"
#include "base/crit.h"

/*
 * Description (SymTable_t)
 *
 *	This type describes a symbols table.  It contains a pointer to
 *	an NSPR hash table and a pointer to a monitor.  The monitor is
 *	needed even for read access to the symbol table because NSPR
 *	modifies the list for a hash bucket when a name is looked up.
 */

typedef struct SymTable_s SymTable_t;
struct SymTable_s {
    CRITICAL stb_crit;			/* monitor pointer */
    PLHashTable * stb_ht;		/* hash table pointer */
};


/* Private functions defined in symbols.c */
/*
static PLHashEntry * symAllocEntry(void * pool, const void *unused);
static void * symAllocTable(void * pool, PRSize size);
static int symCmpName(const void * name1, const void * name2);
static int symCmpValue(const void * value1, const void * value2);
static PLHashNumber symHash(const void * symkey);
static void symFreeEntry(void * pool, PLHashEntry * he, PRUintn flag);
static void symFreeTable(void * pool, void * item);
*/
#endif /* __PRIVATE_SYMBOLS */

/*
 * Description (Symbol_t)
 *
 *	This type describes a symbol table entry.  A symbol is
 *	identified by the combination of its name and type.  This
 *	structure is normally embedded in a structure for a particular
 *	symbol type, which will contain the symbol "value" information
 *	as well.
 */

typedef struct Symbol_s Symbol_t;
struct Symbol_s {
    const char * sym_name;			/* pointer to symbol name string */
    int sym_type;			/* symbol type */
    void *sym_data;			/* symbol data storage */
};

/* Define error return codes */
#define SYMERRNOMEM	-1		/* insufficient dynamic memory */
#define SYMERRDUPSYM	-2		/* duplicate symbol name and type */
#define SYMERRNOSYM	-3		/* symbol name and type not found */

/* Define return flags for symTableEnumerate() func() */
#define SYMENUMSTOP	0x1		/* terminate enumeration */
#define SYMENUMREMOVE	0x2		/* remove entry from symbol table */

NSPR_BEGIN_EXTERN_C

/* Public functions defined in symbols.c */
extern int symTableAddSym(void * table, Symbol_t * newsym, void * symref);
extern void symTableRemoveSym(void * table, Symbol_t * sym);
extern void symTableDestroy(void * table, int flags);

/* for ANSI C++ on SCO UDK, otherwise fn name is managled */
#ifdef UnixWare
typedef int (*ArgFn_symTableEnum)(Symbol_t * sym, void * parg);
extern void symTableEnumerate(void * table, void * argp, ArgFn_symTableEnum);
#else /* UnixWare */
extern void symTableEnumerate(void * table, void * argp,
                              int (*func)(Symbol_t * sym, void * parg));
#endif /* UnixWare */

extern int symTableFindSym(void * table, const char * symname,
			   int symtype, void **psymref);
extern int symTableNew(void **ptable);

NSPR_END_EXTERN_C

#endif /* __symbols_h */
