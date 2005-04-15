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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* vlv_srch.h */


#if !defined(__VLV_SRCH_H)
#define __VLV_SRCH_H

extern char* const type_vlvName;
extern char* const type_vlvBase;
extern char* const type_vlvScope;
extern char* const type_vlvFilter;
extern char* const type_vlvSort;
extern char* const type_vlvFilename;
extern char* const type_vlvEnabled;
extern char* const type_vlvUses;

/*
 * This structure is the internal representation of a VLV Search.
 */
struct vlvSearch
{
    /* The VLV Search Specification Entry */
    const Slapi_Entry *vlv_e;

    /* Extracted from the VLV Search Specification entry */
    Slapi_DN *vlv_dn;
    char *vlv_name;
    Slapi_DN *vlv_base;
    int vlv_scope;
    char *vlv_filter;
    int vlv_initialized;

    /* Derived from the VLV Entry */
    Slapi_Filter *vlv_slapifilter;

    /* List of Indexes for this Search */
    struct vlvIndex* vlv_index;

    /* The next VLV Search in the list */
    struct vlvSearch* vlv_next;
};

struct vlvIndex
{
    char *vlv_name;
    char *vlv_sortspec;

    /* Derived from the VLV Entry */
    LDAPsortkey **vlv_sortkey;

    /* The Index filename */
    char *vlv_filename;

    /* Attribute Structure maps filename onto index */
    struct attrinfo *vlv_attrinfo;

    /* Syntax Plugin.  One for each LDAPsortkey */
    void **vlv_syntax_plugin;

    /* Matching Rule PBlock. One for each LDAPsortkey */
	Slapi_PBlock **vlv_mrpb;

    /* Keep track of the index length */
    PRLock *vlv_indexlength_lock;
    int vlv_indexlength_cached;
    db_recno_t vlv_indexlength;

    int vlv_enabled;            /* index file is there & ready */
    int vlv_online;             /* turned off when generating index */

    /* The last time we checked to see if the index file was available */
    time_t vlv_lastchecked;

    /* The number of uses this search has received since start up */
    PRUint32 vlv_uses;

	struct backend* vlv_be; /* need backend to remove the index when done */

    /* The parent Search Specification for this Index */
    struct vlvSearch* vlv_search;

    /* The next VLV Index in the list */
    struct vlvIndex* vlv_next;
};

struct vlvSearch* vlvSearch_new();
void vlvSearch_init(struct vlvSearch*, Slapi_PBlock *pb, const Slapi_Entry *e, ldbm_instance *inst);
void vlvSearch_reinit(struct vlvSearch* p, const struct backentry *base);
void vlvSearch_delete(struct vlvSearch** ppvs);
void vlvSearch_addtolist(struct vlvSearch* p, struct vlvSearch** pplist);
struct vlvSearch* vlvSearch_find(const struct vlvSearch* plist, const char *base, int scope, const char *filter, const char *sortspec);
struct vlvIndex* vlvSearch_findenabled(backend *be,struct vlvSearch* plist, const Slapi_DN *base, int scope, const char *filter, const sort_spec* sort_control);
struct vlvSearch* vlvSearch_finddn(const struct vlvSearch* plist, const Slapi_DN *dn);
struct vlvIndex* vlvSearch_findname(const struct vlvSearch* plist, const char *name);
struct vlvIndex* vlvSearch_findindexname(const struct vlvSearch* plist, const char *name);
char *vlvSearch_getnames(const struct vlvSearch* plist);
void vlvSearch_removefromlist(struct vlvSearch** pplist, const Slapi_DN *dn);
int vlvSearch_accessallowed(struct vlvSearch *p, Slapi_PBlock *pb);
const Slapi_DN *vlvSearch_getBase(struct vlvSearch* p);
int vlvSearch_getScope(struct vlvSearch* p);
Slapi_Filter *vlvSearch_getFilter(struct vlvSearch* p);
int vlvSearch_isVlvSearchEntry(Slapi_Entry *e);
void vlvSearch_addIndex(struct vlvSearch *pSearch, struct vlvIndex *pIndex);


struct vlvIndex* vlvIndex_new();
void vlvIndex_init(struct vlvIndex* p, backend *be, struct vlvSearch* pSearch, const Slapi_Entry *e);
void vlvIndex_delete(struct vlvIndex** ppvs);
PRUint32 vlvIndex_get_indexlength(struct vlvIndex* p, DB *db, back_txn *txn);
void vlvIndex_increment_indexlength(struct vlvIndex* p, DB *db, back_txn *txn);
void vlvIndex_decrement_indexlength(struct vlvIndex* p, DB *db, back_txn *txn);
void vlvIndex_incrementUsage(struct vlvIndex* p);
const char *vlvIndex_filename(const struct vlvIndex* p);
int vlvIndex_enabled(const struct vlvIndex* p);
int vlvIndex_online(const struct vlvIndex *p);
void vlvIndex_go_offline(struct vlvIndex *p, backend *be);
void vlvIndex_go_online(struct vlvIndex *p, backend *be);
int vlvIndex_accessallowed(struct vlvIndex *p, Slapi_PBlock *pb);
const Slapi_DN *vlvIndex_getBase(struct vlvIndex* p);
int vlvIndex_getScope(struct vlvIndex* p);
Slapi_Filter *vlvIndex_getFilter(struct vlvIndex* p);
const char *vlvIndex_getName(struct vlvIndex* p);
int vlvIndex_isVlvIndexEntry(Slapi_Entry *e);

#define VLV_ACCESS_DENIED -1
#define VLV_BLD_LIST_FAILED -2
#define VLV_FIND_SEARCH_FAILED -3


#endif
