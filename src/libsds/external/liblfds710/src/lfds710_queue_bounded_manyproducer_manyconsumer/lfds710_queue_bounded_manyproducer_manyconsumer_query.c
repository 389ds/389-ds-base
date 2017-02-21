/***** includes *****/
#include "lfds710_queue_bounded_manyproducer_manyconsumer_internal.h"

/***** private prototypes *****/
static void lfds710_queue_bmm_internal_validate( struct lfds710_queue_bmm_state *qbmms,
                                                 struct lfds710_misc_validation_info *vi,
                                                 enum lfds710_misc_validity *lfds710_validity );





/****************************************************************************/
void lfds710_queue_bmm_query( struct lfds710_queue_bmm_state *qbmms,
                              enum lfds710_queue_bmm_query query_type,
                              void *query_input,
                              void *query_output )
{
  LFDS710_PAL_ASSERT( qbmms != NULL );
  // TRD : query_type can be any value in its range

  switch( query_type )
  {
    case LFDS710_QUEUE_BMM_QUERY_GET_POTENTIALLY_INACCURATE_COUNT:
    {
      lfds710_pal_uint_t
        local_read_index,
        local_write_index;

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      LFDS710_MISC_BARRIER_LOAD;

      local_read_index = qbmms->read_index;
      local_write_index = qbmms->write_index;

      *(lfds710_pal_uint_t *) query_output = +( local_write_index - local_read_index );

      if( local_read_index > local_write_index )
        *(lfds710_pal_uint_t *) query_output = ((lfds710_pal_uint_t) -1) - *(lfds710_pal_uint_t *) query_output;
    }
    break;

    case LFDS710_QUEUE_BMM_QUERY_SINGLETHREADED_VALIDATE:
      // TRD : query_input can be NULL
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_queue_bmm_internal_validate( qbmms, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output );
    break;
  }

  return;
}





/****************************************************************************/
static void lfds710_queue_bmm_internal_validate( struct lfds710_queue_bmm_state *qbmms,
                                                 struct lfds710_misc_validation_info *vi,
                                                 enum lfds710_misc_validity *lfds710_validity )
{
  lfds710_pal_uint_t
    expected_sequence_number,
    loop,
    number_elements,
    sequence_number;

  LFDS710_PAL_ASSERT( qbmms != NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_validity != NULL );

  *lfds710_validity = LFDS710_MISC_VALIDITY_VALID;

  /* TRD : starting from the read_index, we should find number_elements of incrementing sequence numbers
           we then perform a second scan from the write_index onwards, which should have (max elements in queue - number_elements) incrementing sequence numbers
  */

  lfds710_queue_bmm_query( qbmms, LFDS710_QUEUE_BMM_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, (void *) &number_elements );

  expected_sequence_number = qbmms->element_array[ qbmms->read_index & qbmms->mask ].sequence_number;

  for( loop = 0 ; loop < number_elements ; loop++ )
  {
    sequence_number = qbmms->element_array[ (qbmms->read_index + loop) & qbmms->mask ].sequence_number;

    if( sequence_number != expected_sequence_number )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_ORDER;

    if( sequence_number == expected_sequence_number )
      expected_sequence_number = sequence_number + 1;
  }

  // TRD : now the write_index onwards

  expected_sequence_number = qbmms->element_array[ qbmms->write_index & qbmms->mask ].sequence_number;

  for( loop = 0 ; loop < qbmms->number_elements - number_elements ; loop++ )
  {
    sequence_number = qbmms->element_array[ (qbmms->write_index + loop) & qbmms->mask ].sequence_number;

    if( sequence_number != expected_sequence_number )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_ORDER;

    if( sequence_number == expected_sequence_number )
      expected_sequence_number = sequence_number + 1;
  }

  // TRD : now check against the expected number of elements

  if( *lfds710_validity == LFDS710_MISC_VALIDITY_VALID and vi != NULL )
  {
    lfds710_pal_uint_t
      number_elements;

    lfds710_queue_bmm_query( qbmms, LFDS710_QUEUE_BMM_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, (void *) &number_elements );

    if( number_elements < vi->min_elements )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

    if( number_elements > vi->max_elements )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;
  }

  return;
}

