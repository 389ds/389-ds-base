/***** includes *****/
#include "lfds710_btree_addonly_unbalanced_internal.h"





/****************************************************************************/
void lfds710_btree_au_init_valid_on_current_logical_core( struct lfds710_btree_au_state *baus,
                                                          int (*key_compare_function)(void const *new_key, void const *existing_key),
                                                          enum lfds710_btree_au_existing_key existing_key,
                                                          void *user_state )
{
  LFDS710_PAL_ASSERT( baus != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &baus->root % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( key_compare_function != NULL );
  // TRD : existing_key can be any value in its range
  // TRD : user_state can be NULL

  baus->root = NULL;
  baus->key_compare_function = key_compare_function;
  baus->existing_key = existing_key;
  baus->user_state = user_state;

  lfds710_misc_internal_backoff_init( &baus->insert_backoff );

  LFDS710_MISC_BARRIER_STORE;

  lfds710_misc_force_store();

  return;
}

