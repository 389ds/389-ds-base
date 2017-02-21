/***** includes *****/
#include "lfds710_hash_addonly_internal.h"

/***** private prototypes *****/
static void lfds710_hash_a_internal_validate( struct lfds710_hash_a_state *has,
                                              struct lfds710_misc_validation_info *vi,
                                              enum lfds710_misc_validity *lfds710_hash_a_validity );





/****************************************************************************/
void lfds710_hash_a_query( struct lfds710_hash_a_state *has,
                           enum lfds710_hash_a_query query_type,
                           void *query_input,
                           void *query_output )
{
  LFDS710_PAL_ASSERT( has != NULL );
  // TRD : query_type can be any value in its range

  LFDS710_MISC_BARRIER_LOAD;

  switch( query_type )
  {
    case LFDS710_HASH_A_QUERY_GET_POTENTIALLY_INACCURATE_COUNT:
    {
      struct lfds710_hash_a_iterate
        ai;

      struct lfds710_hash_a_element
        *hae;

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      *(lfds710_pal_uint_t *) query_output = 0;

      lfds710_hash_a_iterate_init( has, &ai );

      while( lfds710_hash_a_iterate(&ai, &hae) )
        ( *(lfds710_pal_uint_t *) query_output )++;
    }
    break;

    case LFDS710_HASH_A_QUERY_SINGLETHREADED_VALIDATE:
      // TRD: query_input can be any value in its range
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_hash_a_internal_validate( has, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output );
    break;
  }

  return;
}





/****************************************************************************/
static void lfds710_hash_a_internal_validate( struct lfds710_hash_a_state *has,
                                              struct lfds710_misc_validation_info *vi,
                                              enum lfds710_misc_validity *lfds710_hash_a_validity )
{
  lfds710_pal_uint_t
    lfds710_hash_a_total_number_elements = 0,
    lfds710_btree_au_total_number_elements = 0,
    number_elements;

  lfds710_pal_uint_t
    loop;

  LFDS710_PAL_ASSERT( has!= NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_hash_a_validity != NULL );

  /* TRD : validate every btree_addonly_unbalanced in the addonly_hash
           sum elements in each btree_addonly_unbalanced
           check matches expected element counts (if vi is provided)
  */

  *lfds710_hash_a_validity = LFDS710_MISC_VALIDITY_VALID;

  for( loop = 0 ; *lfds710_hash_a_validity == LFDS710_MISC_VALIDITY_VALID and loop < has->array_size ; loop++ )
    lfds710_btree_au_query( has->baus_array+loop, LFDS710_BTREE_AU_QUERY_SINGLETHREADED_VALIDATE, NULL, (void *) lfds710_hash_a_validity );

  if( *lfds710_hash_a_validity == LFDS710_MISC_VALIDITY_VALID )
  {
    for( loop = 0 ; loop < has->array_size ; loop++ )
    {
      lfds710_btree_au_query( has->baus_array+loop, LFDS710_BTREE_AU_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, (void *) &number_elements );
      lfds710_btree_au_total_number_elements += number_elements;
    }

    // TRD : first, check btree_addonly_unbalanced total vs the addonly_hash total
    lfds710_hash_a_query( has, LFDS710_HASH_A_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, &lfds710_hash_a_total_number_elements );

    // TRD : the btree_addonly_unbalanceds are assumed to speak the truth
    if( lfds710_hash_a_total_number_elements < lfds710_btree_au_total_number_elements )
      *lfds710_hash_a_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;

    if( lfds710_hash_a_total_number_elements > lfds710_btree_au_total_number_elements )
      *lfds710_hash_a_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

    // TRD : second, if we're still valid and vi is provided, check the btree_addonly_unbalanced total against vi
    if( *lfds710_hash_a_validity == LFDS710_MISC_VALIDITY_VALID and vi != NULL )
    {
      if( lfds710_btree_au_total_number_elements < vi->min_elements )
        *lfds710_hash_a_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

      if( lfds710_btree_au_total_number_elements > vi->max_elements )
        *lfds710_hash_a_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;
    }
  }

  return;
}

