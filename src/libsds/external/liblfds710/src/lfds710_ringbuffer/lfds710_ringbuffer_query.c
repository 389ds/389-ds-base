/***** includes *****/
#include "lfds710_ringbuffer_internal.h"

/***** private prototypes *****/
static void lfds710_ringbuffer_internal_validate( struct lfds710_ringbuffer_state *rs,
                                                  struct lfds710_misc_validation_info *vi,
                                                  enum lfds710_misc_validity *lfds710_queue_umm_validity,
                                                  enum lfds710_misc_validity *lfds710_freelist_validity );



/****************************************************************************/
void lfds710_ringbuffer_query( struct lfds710_ringbuffer_state *rs,
                               enum lfds710_ringbuffer_query query_type,
                               void *query_input,
                               void *query_output )
{
  LFDS710_PAL_ASSERT( rs != NULL );
  // TRD : query_type can be any value in its range

  LFDS710_MISC_BARRIER_LOAD;

  switch( query_type )
  {
    case LFDS710_RINGBUFFER_QUERY_SINGLETHREADED_GET_COUNT:
      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_queue_umm_query( &rs->qumms, LFDS710_QUEUE_UMM_QUERY_SINGLETHREADED_GET_COUNT, NULL, query_output );
    break;

    case LFDS710_RINGBUFFER_QUERY_SINGLETHREADED_VALIDATE:
      // TRD : query_input can be NULL
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_ringbuffer_internal_validate( rs, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output, ((enum lfds710_misc_validity *) query_output)+1 );
    break;
  }

  return;
}





/****************************************************************************/
static void lfds710_ringbuffer_internal_validate( struct lfds710_ringbuffer_state *rs,
                                                  struct lfds710_misc_validation_info *vi,
                                                  enum lfds710_misc_validity *lfds710_queue_umm_validity,
                                                  enum lfds710_misc_validity *lfds710_freelist_validity )
{
  LFDS710_PAL_ASSERT( rs != NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_queue_umm_validity != NULL );
  LFDS710_PAL_ASSERT( lfds710_freelist_validity != NULL );

  if( vi == NULL )
  {
    lfds710_queue_umm_query( &rs->qumms, LFDS710_QUEUE_UMM_QUERY_SINGLETHREADED_VALIDATE, NULL, lfds710_queue_umm_validity );
    lfds710_freelist_query( &rs->fs, LFDS710_FREELIST_QUERY_SINGLETHREADED_VALIDATE, NULL, lfds710_freelist_validity );
  }

  if( vi != NULL )
  {
    struct lfds710_misc_validation_info
      freelist_vi,
      queue_vi;

    queue_vi.min_elements = 0;
    freelist_vi.min_elements = 0;
    queue_vi.max_elements = vi->max_elements;
    freelist_vi.max_elements = vi->max_elements;

    lfds710_queue_umm_query( &rs->qumms, LFDS710_QUEUE_UMM_QUERY_SINGLETHREADED_VALIDATE, &queue_vi, lfds710_queue_umm_validity );
    lfds710_freelist_query( &rs->fs, LFDS710_FREELIST_QUERY_SINGLETHREADED_VALIDATE, &freelist_vi, lfds710_freelist_validity );
  }

  return;
}

