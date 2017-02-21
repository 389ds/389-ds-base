/***** includes *****/
#include "lfds710_queue_unbounded_manyproducer_manyconsumer_internal.h"





/****************************************************************************/
int lfds710_queue_umm_dequeue( struct lfds710_queue_umm_state *qumms,
                               struct lfds710_queue_umm_element **qumme )
{
  char unsigned
    result = 0;

  enum lfds710_misc_flag
    finished_flag = LFDS710_MISC_FLAG_LOWERED;

  enum lfds710_queue_umm_queue_state
    state = LFDS710_QUEUE_UMM_QUEUE_STATE_UNKNOWN;

  int
    rv = 1;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  struct lfds710_queue_umm_element LFDS710_PAL_ALIGN(LFDS710_PAL_ALIGN_DOUBLE_POINTER)
    *dequeue[PAC_SIZE],
    *enqueue[PAC_SIZE],
    *next[PAC_SIZE];

  void
    *key = NULL,
    *value = NULL;

  LFDS710_PAL_ASSERT( qumms != NULL );
  LFDS710_PAL_ASSERT( qumme != NULL );

  LFDS710_MISC_BARRIER_LOAD;

  do
  {
    /* TRD : note here the deviation from the white paper
             in the white paper, next is loaded from dequeue, not from qumms->dequeue
             what concerns me is that between the load of dequeue and the load of
             enqueue->next, the element can be dequeued by another thread *and freed*

             by ordering the loads (load barriers), and loading both from qumms,
             the following if(), which checks dequeue is still the same as qumms->enqueue
             still continues to ensure next belongs to enqueue, while avoiding the
             problem with free
    */

    dequeue[COUNTER] = qumms->dequeue[COUNTER];
    dequeue[POINTER] = qumms->dequeue[POINTER];

    LFDS710_MISC_BARRIER_LOAD;

    enqueue[COUNTER] = qumms->enqueue[COUNTER];
    enqueue[POINTER] = qumms->enqueue[POINTER];

    next[COUNTER] = qumms->dequeue[POINTER]->next[COUNTER];
    next[POINTER] = qumms->dequeue[POINTER]->next[POINTER];

    LFDS710_MISC_BARRIER_LOAD;

    if( qumms->dequeue[COUNTER] == dequeue[COUNTER] and qumms->dequeue[POINTER] == dequeue[POINTER] )
    {
      if( enqueue[POINTER] == dequeue[POINTER] and next[POINTER] == NULL )
        state = LFDS710_QUEUE_UMM_QUEUE_STATE_EMPTY;

      if( enqueue[POINTER] == dequeue[POINTER] and next[POINTER] != NULL )
        state = LFDS710_QUEUE_UMM_QUEUE_STATE_ENQUEUE_OUT_OF_PLACE;

      if( enqueue[POINTER] != dequeue[POINTER] )
        state = LFDS710_QUEUE_UMM_QUEUE_STATE_ATTEMPT_DEQUEUE;

      switch( state )
      {
        case LFDS710_QUEUE_UMM_QUEUE_STATE_UNKNOWN:
          // TRD : eliminates compiler warning
        break;

        case LFDS710_QUEUE_UMM_QUEUE_STATE_EMPTY:
          rv = 0;
          *qumme = NULL;
          result = 1;
          finished_flag = LFDS710_MISC_FLAG_RAISED;
        break;

        case LFDS710_QUEUE_UMM_QUEUE_STATE_ENQUEUE_OUT_OF_PLACE:
          next[COUNTER] = enqueue[COUNTER] + 1;
          LFDS710_PAL_ATOMIC_DWCAS( qumms->enqueue, enqueue, next, LFDS710_MISC_CAS_STRENGTH_WEAK, result );
          // TRD : in fact if result is 1 (successful) I think we can now simply drop down into the dequeue attempt
        break;

        case LFDS710_QUEUE_UMM_QUEUE_STATE_ATTEMPT_DEQUEUE:
          key = next[POINTER]->key;
          value = next[POINTER]->value;

          next[COUNTER] = dequeue[COUNTER] + 1;
          LFDS710_PAL_ATOMIC_DWCAS( qumms->dequeue, dequeue, next, LFDS710_MISC_CAS_STRENGTH_WEAK, result );

          if( result == 1 )
            finished_flag = LFDS710_MISC_FLAG_RAISED;
        break;
      }
    }
    else
      result = 0;

    if( result == 0 )
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( qumms->dequeue_backoff, backoff_iteration );
  }
  while( finished_flag == LFDS710_MISC_FLAG_LOWERED );

  if( result == 1 )
  {
    *qumme = dequeue[POINTER];
    (*qumme)->key = key;
    (*qumme)->value = value;
  }

  LFDS710_BACKOFF_AUTOTUNE( qumms->dequeue_backoff, backoff_iteration );

  return rv;
}

