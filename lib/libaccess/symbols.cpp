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


/*
 * Description (symbols.c)
 *
 *	This module implements a symbol table for ACL-related structures.
 *	The symbol table associates string names and types with pointers
 *	to various kinds of structures.
 */
/*
#include <base/systems.h>
*/
#include <plhash.h>
#include <base/util.h>
#include <netsite.h>
#define __PRIVATE_SYMBOLS
#include "libaccess/symbols.h"
#include <ctype.h>

static PLHashEntry * symAllocEntry(void * pool, const void *unused);
static void * symAllocTable(void * pool, PRSize size);
static int symCmpName(const void * name1, const void * name2);
static int symCmpValue(const void * value1, const void * value2);
static PLHashNumber symHash(const void * symkey);
static void symFreeEntry(void * pool, PLHashEntry * he, PRUintn flag);
static void symFreeTable(void * pool, void * item);

/* Table of pointers to functions associated with the hash table */
static PLHashAllocOps SymAllocOps = {
    symAllocTable,			/* allocate the hash table */
    symFreeTable,			/* free the hash table */
    symAllocEntry,			/* allocate a table entry */
    symFreeEntry,			/* free a table entry */
};

static void * symAllocTable(void * pool, PRSize size)
{
    return (void *)PERM_MALLOC(size);
}

static void symFreeTable(void * pool, void * item)
{
    PERM_FREE(item);
}



static PLHashEntry * symAllocEntry(void * pool, const void *ignored)
{
    PLHashEntry * he;

    he =  (PLHashEntry *) PERM_MALLOC(sizeof(PLHashEntry));

    return he;
}

static void symFreeEntry(void * pool, PLHashEntry * he, PRUintn flag)
{
    if (flag == HT_FREE_ENTRY) {
	/* Just free the hash entry, not anything it references */
	PERM_FREE(he);
    }
}


static int symCmpName(const void * name1, const void * name2)
{
    Symbol_t * sym1 = (Symbol_t *)name1;
    Symbol_t * sym2 = (Symbol_t *)name2;

    return ((sym1->sym_type == sym2->sym_type) &&
	    !strcasecmp(sym1->sym_name, sym2->sym_name));
}

static int symCmpValue(const void * value1, const void * value2)
{
    return (value1 == value2);
}

static PLHashNumber symHash(const void * symkey)
{
    Symbol_t * sym = (Symbol_t *)symkey;
    const char * cp;
    PLHashNumber h;

    h = sym->sym_type;
    cp = sym->sym_name;
    if (cp) {
	while (*cp) {
	    h = (h << 3) ^ tolower(*cp);
	    ++cp;
	}
    }

    return h;
}

/* Helper function for symTableEnumerate() */
typedef struct {
    int (*func)(Symbol_t * sym, void * parg);
    void * argp;
} SymTableEnum_t;

static int symTableEnumHelp(PLHashEntry * he, int n, void * step)
{
    SymTableEnum_t * ste = (SymTableEnum_t *)step;
    int ret = 0;
    int rv;

    rv = (*ste->func)((Symbol_t *)(he->key), ste->argp);
    if (rv != 0) {
	if (rv & SYMENUMREMOVE) ret = HT_ENUMERATE_REMOVE;
	if (rv & SYMENUMSTOP) ret |= HT_ENUMERATE_STOP;
    }

    return ret;
}

NSPR_BEGIN_EXTERN_C

/*
 * Description (symTableAddSym)
 *
 *	This function adds a symbol definition to the symbol table.
 *	The symbol definition includes a name string, a type, and a
 *	reference to a structure.
 *
 * Arguments:
 *
 *	table			- handle for symbol table
 *	newsym			- pointer to new symbol name and type
 *	symref			- pointer to structure named by symbol
 *
 * Returns:
 *
 *	If successful, the return code is zero.  An error is indicated
 *	by a negative return code (SYMERRxxxx - see symbols.h).
 */

int symTableAddSym(void * table, Symbol_t * newsym, void * symref)
{
    SymTable_t * st = (SymTable_t *)table;
    PLHashEntry **hep;
    PLHashNumber keyhash;
    int rv = 0;

    /* Compute the hash value for this symbol */
    keyhash = symHash((const void *)newsym);

    crit_enter(st->stb_crit);

    /* See if another symbol already has the same name and type */
    hep = PL_HashTableRawLookup(st->stb_ht, keyhash, (void *)newsym);
    if (*hep == 0) {

	/* Expand the hash table if necessary and allocate an entry */
	PL_HashTableRawAdd(st->stb_ht,
				hep, keyhash, (void *)newsym, symref);
    }
    else {
	/* The symbol is already there.  It's an error */
	rv = SYMERRDUPSYM;
    }

    crit_exit(st->stb_crit);
    return rv;
}

/*
 * Description (symTableRemoveSym)
 *
 *	This function removes an entry from a symbol table.  It does
 *	not free the entry itself, just the hash entry that references
 *	it.
 *
 * Arguments:
 *
 *	table			- symbol table handle
 *	sym			- pointer to symbol structure
 */

void symTableRemoveSym(void * table, Symbol_t * sym)
{
    SymTable_t * st = (SymTable_t *)table;

    if (sym->sym_name != 0) {
	crit_enter(st->stb_crit);
	PL_HashTableRemove(st->stb_ht, (void *)sym);
	crit_exit(st->stb_crit);
    }
}

/*
 * Description (symTableEnumerate)
 *
 *	This function enumerates all of the entries in a symbol table,
 *	calling a specified function for each entry.  The function
 *	specified by the caller may return flags indicating actions
 *	to be taken for each entry or whether to terminate the
 *	enumeration.  These flags are defined in symbols.h as
 *	SYMENUMxxxx.
 *
 * Arguments:
 *
 *	table				- symbol table handle
 *	argp				- argument for caller function
 *	func				- function to be called for each entry
 */

void symTableEnumerate(void * table, void * argp,
#ifdef UnixWare /* Fix bug in UnixWare compiler for name mangeling - nedh@sco.com */
		       ArgFn_symTableEnum func)
#else
		       int (*func)(Symbol_t * sym, void * parg))
#endif
{
    SymTable_t * st = (SymTable_t *)table;
    SymTableEnum_t ste;		/* enumeration arguments */

    ste.func = func;
    ste.argp = argp;

    crit_enter(st->stb_crit);
    (void)PL_HashTableEnumerateEntries(st->stb_ht,
				       symTableEnumHelp, (void *)&ste);
    crit_exit(st->stb_crit);
}

/*
 * Description (symTableFindSym)
 *
 *	This function locates a symbol with a specified name and type
 *	in a given symbol table.  It returns a pointer to the structure
 *	named by the symbol.
 *
 * Arguments:
 *
 *	table				- symbol table handle
 *	symname				- symbol name string pointer
 *	symtype				- symbol type code
 *	psymref				- pointer to returned structure pointer
 *
 * Returns:
 *
 *	If successful, the return code is zero and the structure pointer
 *	associated with the symbol name and type is returned in the
 *	location specified by 'psymref'.  An error is indicated by a
 *	negative return code (SYMERRxxxx - see symbols.h).
 */

int symTableFindSym(void * table, const char * symname,
		      int symtype, void **psymref)
{
    SymTable_t * st = (SymTable_t *)table;
    Symbol_t sym;
    void * symref;

    /* Create temporary entry with fields needed by symHash() */
    sym.sym_name = symname;
    sym.sym_type = symtype;
    
    crit_enter(st->stb_crit);

    symref = PL_HashTableLookup(st->stb_ht, (void *)&sym);

    crit_exit(st->stb_crit);

    *psymref = symref;

    return (symref) ? 0 : SYMERRNOSYM;
}

/*
 * Description (symTableDestroy)
 *
 *	This function destroys a symbol table created by symTableNew().
 *
 * Arguments:
 *
 *	table			- symbol table handle from symTableNew()
 *	flags			- bit flags (unused - must be zero)
 */

void symTableDestroy(void * table, int flags)
{
    SymTable_t * st = (SymTable_t *)table;

    if (st) {
	if (st->stb_crit) {
	    crit_terminate(st->stb_crit);
	}

	if (st->stb_ht) {
	    PL_HashTableDestroy(st->stb_ht);
	}

	PERM_FREE(st);
    }
}

/*
 * Description (symTableNew)
 *
 *	This function creates a new symbol table, and returns a handle
 *	for it.
 *
 * Arguments:
 *
 *	ptable			- pointer to returned symbol table handle
 *
 * Returns:
 *
 *	If successful, the return code is zero and a handle for the new
 *	symbol table is returned in the location specified by 'ptable'.
 *	An error is indicated by a negative return code (SYMERRxxxx
 *	- see symbols.h).
 */

int symTableNew(void **ptable)
{
    SymTable_t * st;

    /* Allocate the symbol table object */
    st = (SymTable_t *)PERM_MALLOC(sizeof(SymTable_t));
    if (st == 0) goto err_nomem;

    /* Get a monitor for it */
    st->stb_crit = crit_init();

    st->stb_ht = PL_NewHashTable(0, symHash, symCmpName, symCmpValue,
				 &SymAllocOps, 0);
    if (st->stb_ht == 0) goto err_nomem;

    *ptable = st;
    return 0;

  err_nomem:
    if (st) {
	symTableDestroy(st, 0);
    }
    return SYMERRNOMEM;
}

NSPR_END_EXTERN_C

