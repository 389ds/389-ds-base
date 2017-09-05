/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 */

#define REASON_ANNOTATE_DN "namingConflict"
#define REASON_RESURRECT_ENTRY "deletedEntryHasChildren"

/*
 * urp.c
 */
int urp_modify_operation(Slapi_PBlock *pb);
int urp_add_operation(Slapi_PBlock *pb);
int urp_delete_operation(Slapi_PBlock *pb);
int urp_post_add_operation(Slapi_PBlock *pb);
int urp_post_delete_operation(Slapi_PBlock *pb);
int urp_modrdn_operation(Slapi_PBlock *pb);
int urp_post_modrdn_operation(Slapi_PBlock *pb);
char *get_rdn_plus_uniqueid(char *sessionid, const char *olddn, const char *uniqueid);

/* urp internal ops */
int urp_fixup_add_entry(Slapi_Entry *e, const char *target_uniqueid, const char *parentuniqueid, CSN *opcsn, int opflags);
int urp_fixup_delete_entry(const char *uniqueid, const char *dn, CSN *opcsn, int opflags);
int urp_fixup_rename_entry(const Slapi_Entry *entry, const char *newrdn, const char *parentuniqueid, int opflags);
int urp_fixup_modify_entry(const char *uniqueid, const Slapi_DN *sdn, CSN *opcsn, Slapi_Mods *smods, int opflags);
int urp_fixup_modrdn_entry(const Slapi_DN *entrydn, const char *newrdn, const Slapi_DN *newsuperior, const char *entryuniqueid, const char *parentuniqueid, CSN *opcsn, int opflags);

int is_suffix_dn(Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parenddn);
int is_suffix_dn_ext(Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parenddn, int is_tombstone);

/*
 * urp_glue.c
 */
int is_glue_entry(const Slapi_Entry *entry);
int is_conflict_entry(const Slapi_Entry *entry);
int create_glue_entry(Slapi_PBlock *pb, char *sessionid, Slapi_DN *dn, const char *uniqueid, CSN *opcsn);
int entry_to_glue(char *sessionid, const Slapi_Entry *entry, const char *reason, CSN *opcsn);
int glue_to_entry(Slapi_PBlock *pb, Slapi_Entry *entry);
PRBool get_glue_csn(const Slapi_Entry *entry, const CSN **gluecsn);

/*
 * urp_tombstone.c
 */
int is_tombstone_entry(const Slapi_Entry *entry);
int tombstone_to_glue(Slapi_PBlock *pb, char *sessionid, Slapi_Entry *entry, const Slapi_DN *parentdn, const char *reason, CSN *opcsn, Slapi_DN **newparentdn);
int tombstone_to_conflict(char *sessionid, Slapi_Entry *entry, const Slapi_DN *conflictdn, const char *reason, CSN *opcsn,     Slapi_DN **newparentdn);
int conflict_to_tombstone(char *sessionid, Slapi_Entry *entry, CSN *opcsn);
int tombstone_to_conflict_check_parent( char *sessionid, char *parentdn, const char *uniqueid, const char *parentuniqueid, CSN *opcsn, Slapi_DN *conflictdn);
int entry_to_tombstone(Slapi_PBlock *pb, Slapi_Entry *entry);
PRBool get_tombstone_csn(const Slapi_Entry *entry, const CSN **delcsn);
