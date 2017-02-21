/***** includes *****/
#include "lfds710_queue_bounded_singleproducer_singleconsumer_internal.h"

/***** private prototypes *****/
static void lfds710_queue_bss_internal_validate( struct lfds710_queue_bss_state *qbsss,
                                                 struct lfds710_misc_validation_info *vi,
                                                 enum lfds710_misc_validity *lfds710_validity );





/****************************************************************************/
void lfds710_queue_bss_query( struct lfds710_queue_bss_state *qbsss,
                              enum lfds710_queue_bss_query query_type,
                              void *query_input,
                              void *query_output )
{
  LFDS710_PAL_ASSERT( qbsss != NULL );
  // TRD : query_type can be any value in its range

  switch( query_type )
  {
    case LFDS710_QUEUE_BSS_QUERY_GET_POTENTIALLY_INACCURATE_COUNT:
    {
      lfds710_pal_uint_t
        local_read_index,
        local_write_index;

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      LFDS710_MISC_BARRIER_LOAD;

      local_read_index = qbsss->read_index;
      local_write_index = qbsss->write_index;

      *(lfds710_pal_uint_t *) query_output = +( local_write_index - local_read_index );

      if( local_read_index > local_write_index )
        *(lfds710_pal_uint_t *) query_output = qbsss->number_elements - *(lfds710_pal_uint_t *) query_output;
    }
    break;

    case LFDS710_QUEUE_BSS_QUERY_VALIDATE:
      // TRD : query_input can be NULL
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_queue_bss_internal_validate( qbsss, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output );
    break;
  }

  return;
}





/****************************************************************************/
static void lfds710_queue_bss_internal_validate( struct lfds710_queue_bss_state *qbsss,
                                                 struct lfds710_misc_validation_info *vi,
                                                 enum lfds710_misc_validity *lfds710_validity )
{
  LFDS710_PAL_ASSERT( qbsss != NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_validity != NULL );

  *lfds710_validity = LFDS710_MISC_VALIDITY_VALID;

  if( vi != NULL )
  {
    lfds710_pal_uint_t
      number_elements;

    lfds710_queue_bss_query( qbsss, LFDS710_QUEUE_BSS_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, (void *) &number_elements );

    if( number_elements < vi->min_elements )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

    if( number_elements > vi->max_elements )
      *lfds710_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;
  }

  return;
}

