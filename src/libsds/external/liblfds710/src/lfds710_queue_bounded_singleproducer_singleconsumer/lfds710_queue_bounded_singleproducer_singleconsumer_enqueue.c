/***** includes *****/
#include "lfds710_queue_bounded_singleproducer_singleconsumer_internal.h"





/****************************************************************************/
int lfds710_queue_bss_enqueue( struct lfds710_queue_bss_state *qbsss,
                               void *key,
                               void *value )
{
  struct lfds710_queue_bss_element
    *qbsse;

  LFDS710_PAL_ASSERT( qbsss != NULL );
  // TRD : key can be NULL
  // TRD : value can be NULL

  LFDS710_MISC_BARRIER_LOAD;

  if( ( (qbsss->write_index+1) & qbsss->mask ) != qbsss->read_index )
  {
    qbsse = qbsss->element_array + qbsss->write_index;

    qbsse->key = key;
    qbsse->value = value;

    LFDS710_MISC_BARRIER_STORE;

    qbsss->write_index = (qbsss->write_index + 1) & qbsss->mask;

    return 1;
  }

  return 0;
}

