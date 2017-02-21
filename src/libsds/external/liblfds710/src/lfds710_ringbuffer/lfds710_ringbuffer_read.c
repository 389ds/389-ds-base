/***** includes *****/
#include "lfds710_ringbuffer_internal.h"





/****************************************************************************/
int lfds710_ringbuffer_read( struct lfds710_ringbuffer_state *rs,
                             void **key,
                             void **value )
{
  int
    rv;

  struct lfds710_queue_umm_element
    *qumme;

  struct lfds710_ringbuffer_element
    *re;

  LFDS710_PAL_ASSERT( rs != NULL );
  // TRD : key can be NULL
  // TRD : value can be NULL
  // TRD : psts can be NULL

  rv = lfds710_queue_umm_dequeue( &rs->qumms, &qumme );

  if( rv == 1 )
  {
    re = LFDS710_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qumme );
    re->qumme_use = (struct lfds710_queue_umm_element *) qumme;
    if( key != NULL )
      *key = re->key;
    if( value != NULL )
      *value = re->value;
    LFDS710_FREELIST_SET_VALUE_IN_ELEMENT( re->fe, re );
    lfds710_freelist_push( &rs->fs, &re->fe, NULL );
  }

  return rv;
}

