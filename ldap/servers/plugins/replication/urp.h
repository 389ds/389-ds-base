/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 */

#define REASON_ANNOTATE_DN		"namingConflict"
#define REASON_RESURRECT_ENTRY	"deletedEntryHasChildren"

/*
 * urp.c
 */
int urp_modify_operation( Slapi_PBlock *pb );
int urp_add_operation( Slapi_PBlock *pb );
int urp_delete_operation( Slapi_PBlock *pb );
int urp_post_delete_operation( Slapi_PBlock *pb );
int urp_modrdn_operation( Slapi_PBlock *pb );
int urp_post_modrdn_operation( Slapi_PBlock *pb );

/* urp internal ops */
int urp_fixup_add_entry (Slapi_Entry *e, const char *target_uniqueid, const char *parentuniqueid, CSN *opcsn, int opflags);
int urp_fixup_delete_entry (const char *uniqueid, const char *dn, CSN *opcsn, int opflags);
int urp_fixup_rename_entry (Slapi_Entry *entry, const char *newrdn, int opflags);
int urp_fixup_modify_entry (const char *uniqueid, const char *dn, CSN *opcsn, Slapi_Mods *smods, int opflags);

int is_suffix_dn (Slapi_PBlock *pb, const Slapi_DN *dn, Slapi_DN **parenddn);

/*
 * urp_glue.c
 */
int is_glue_entry(const Slapi_Entry* entry);
int create_glue_entry ( Slapi_PBlock *pb, char *sessionid, Slapi_DN *dn, const char *uniqueid, CSN *opcsn );
int entry_to_glue(char *sessionid, const Slapi_Entry* entry, const char *reason, CSN *opcsn);
int glue_to_entry (Slapi_PBlock *pb, Slapi_Entry *entry );
PRBool get_glue_csn(const Slapi_Entry *entry, const CSN **gluecsn);

/*
 * urp_tombstone.c
 */
int is_tombstone_entry(const Slapi_Entry* entry);
int tombstone_to_glue(Slapi_PBlock *pb, const char *sessionid, Slapi_Entry *entry, const Slapi_DN *parentdn, const char *reason, CSN *opcsn);
int entry_to_tombstone ( Slapi_PBlock *pb, Slapi_Entry *entry );
PRBool get_tombstone_csn(const Slapi_Entry *entry, const CSN **delcsn);
