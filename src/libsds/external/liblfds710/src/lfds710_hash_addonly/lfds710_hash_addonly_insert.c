/***** includes *****/
#include "lfds710_hash_addonly_internal.h"





/****************************************************************************/
enum lfds710_hash_a_insert_result lfds710_hash_a_insert( struct lfds710_hash_a_state *has,
                                                         struct lfds710_hash_a_element *hae,
                                                         struct lfds710_hash_a_element **existing_hae )
{
  enum lfds710_hash_a_insert_result
    apr = LFDS710_HASH_A_PUT_RESULT_SUCCESS;

  enum lfds710_btree_au_insert_result
    alr;

  lfds710_pal_uint_t
    hash = 0;

  struct lfds710_btree_au_element
    *existing_baue;

  LFDS710_PAL_ASSERT( has != NULL );
  LFDS710_PAL_ASSERT( hae != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &hae->value % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  // TRD : existing_hae can be NULL

  // TRD : alignment checks
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &hae->baue % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );

  has->key_hash_function( hae->key, &hash );

  LFDS710_BTREE_AU_SET_KEY_IN_ELEMENT( hae->baue, hae->key );
  LFDS710_BTREE_AU_SET_VALUE_IN_ELEMENT( hae->baue, hae );

  alr = lfds710_btree_au_insert( has->baus_array + (hash % has->array_size), &hae->baue, &existing_baue );

  switch( alr )
  {
    case LFDS710_BTREE_AU_INSERT_RESULT_FAILURE_EXISTING_KEY:
      if( existing_hae != NULL )
        *existing_hae = LFDS710_BTREE_AU_GET_VALUE_FROM_ELEMENT( *existing_baue );

      apr = LFDS710_HASH_A_PUT_RESULT_FAILURE_EXISTING_KEY;
    break;

    case LFDS710_BTREE_AU_INSERT_RESULT_SUCCESS_OVERWRITE:
      apr = LFDS710_HASH_A_PUT_RESULT_SUCCESS_OVERWRITE;
    break;

    case LFDS710_BTREE_AU_INSERT_RESULT_SUCCESS:
      apr = LFDS710_HASH_A_PUT_RESULT_SUCCESS;
    break;
  }

  return apr;
}

