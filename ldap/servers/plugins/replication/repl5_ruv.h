/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl5_ruv.h - interface for replica update vector */

#ifndef REPL5_RUV
#define REPL5_RUV

#include "slapi-private.h"

#ifdef __cplusplus 
extern "C" {
#endif

typedef struct _ruv RUV;

enum 
{
	RUV_SUCCESS=0,
	RUV_BAD_DATA,
	RUV_NOTFOUND,
	RUV_MEMORY_ERROR,
	RUV_NSPR_ERROR,
	RUV_BAD_FORMAT,
	RUV_UNKNOWN_ERROR,
	RUV_ALREADY_EXIST,
    RUV_CSNPL_ERROR,
    RUV_COVERS_CSN
};

typedef struct ruv_enum_data
{
    CSN *csn;
    CSN *min_csn;
} ruv_enum_data;

typedef int (*FNEnumRUV) (const ruv_enum_data *element, void *arg);
int ruv_init_new (const char *replGen, ReplicaId rid, const char *purl, RUV **ruv);
int ruv_init_from_bervals(struct berval** vals, RUV **ruv);
int ruv_init_from_slapi_attr(Slapi_Attr *attr, RUV **ruv);
int ruv_init_from_slapi_attr_and_check_purl(Slapi_Attr *attr, RUV **ruv, ReplicaId *rid);
RUV* ruv_dup (const RUV *ruv);
void ruv_destroy (RUV **ruv);
void ruv_copy_and_destroy (RUV **srcruv, RUV **destruv);
int ruv_replace_replica_purl (RUV *ruv, ReplicaId rid, const char *replica_purl);
int ruv_delete_replica (RUV *ruv, ReplicaId rid); 
int ruv_add_replica (RUV *ruv, ReplicaId rid, const char *replica_purl);
int ruv_add_index_replica (RUV *ruv, ReplicaId rid, const char *replica_purl, int index);
PRBool ruv_contains_replica (const RUV *ruv, ReplicaId rid);
int ruv_get_largest_csn_for_replica(const RUV *ruv, ReplicaId rid, CSN **csn);
int ruv_get_smallest_csn_for_replica(const RUV *ruv, ReplicaId rid, CSN **csn);
int ruv_set_csns(RUV *ruv, const CSN *csn, const char *replica_purl);  
int ruv_set_csns_keep_smallest(RUV *ruv, const CSN *csn);  
int ruv_set_max_csn(RUV *ruv, const CSN *max_csn, const char *replica_purl);
int ruv_set_min_csn(RUV *ruv, const CSN *min_csn, const char *replica_purl);
const char *ruv_get_purl_for_replica(const RUV *ruv, ReplicaId rid);
char *ruv_get_replica_generation (const RUV *ruv);
void ruv_set_replica_generation (RUV *ruv, const char *generation);
PRBool ruv_covers_ruv(const RUV *covering_ruv, const RUV *covered_ruv);
PRBool ruv_covers_csn(const RUV *ruv, const CSN *csn);
PRBool ruv_covers_csn_strict(const RUV *ruv, const CSN *csn);
int ruv_get_min_csn(const RUV *ruv, CSN **csn);
int ruv_get_max_csn(const RUV *ruv, CSN **csn);
int ruv_enumerate_elements (const RUV *ruv, FNEnumRUV fn, void *arg);
int ruv_to_smod(const RUV *ruv, Slapi_Mod *smod);
int ruv_last_modified_to_smod(const RUV *ruv, Slapi_Mod *smod);
int ruv_to_bervals(const RUV *ruv, struct berval ***bvals);
PRInt32 ruv_replica_count (const RUV *ruv);
char **ruv_get_referrals(const RUV *ruv);
void ruv_dump(const RUV *ruv, char *ruv_name, PRFileDesc *prFile);
int ruv_add_csn_inprogress (RUV *ruv, const CSN *csn);
int ruv_cancel_csn_inprogress (RUV *ruv, const CSN *csn);
int ruv_update_ruv (RUV *ruv, const CSN *csn, const char *replica_purl, PRBool isLocal);
int ruv_move_local_supplier_to_first(RUV *ruv, ReplicaId rid);
int ruv_get_first_id_and_purl(RUV *ruv, ReplicaId *rid, char **replica_purl );
int ruv_local_contains_supplier(RUV *ruv, ReplicaId rid);
/* returns true if the ruv has any csns, false otherwise - used for testing
   whether or not an RUV is empty */
PRBool ruv_has_csns(const RUV *ruv);
PRBool ruv_is_newer (Object *sruv, Object *cruv);
#ifdef __cplusplus
}
#endif

#endif
