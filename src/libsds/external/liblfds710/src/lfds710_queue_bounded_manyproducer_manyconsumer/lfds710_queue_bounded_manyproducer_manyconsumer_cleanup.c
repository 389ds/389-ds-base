/***** includes *****/
#include "lfds710_queue_bounded_manyproducer_manyconsumer_internal.h"





/****************************************************************************/
void lfds710_queue_bmm_cleanup( struct lfds710_queue_bmm_state *qbmms,
                                void (*element_cleanup_callback)(struct lfds710_queue_bmm_state *qbmms, void *key, void *value) )
{
  void
    *key,
    *value;

  LFDS710_PAL_ASSERT( qbmms != NULL );
  // TRD : element_cleanup_callback can be NULL

  LFDS710_MISC_BARRIER_LOAD;

  if( element_cleanup_callback != NULL )
    while( lfds710_queue_bmm_dequeue(qbmms,&key,&value) )
      element_cleanup_callback( qbmms, key, value );

  return;
}

