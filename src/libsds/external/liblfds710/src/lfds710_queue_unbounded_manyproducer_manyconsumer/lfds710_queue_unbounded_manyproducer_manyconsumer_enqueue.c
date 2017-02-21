/***** includes *****/
#include "lfds710_queue_unbounded_manyproducer_manyconsumer_internal.h"





/****************************************************************************/
void lfds710_queue_umm_enqueue( struct lfds710_queue_umm_state *qumms,
                                struct lfds710_queue_umm_element *qumme )
{
  char unsigned
    result = 0;

  enum lfds710_misc_flag
    finished_flag = LFDS710_MISC_FLAG_LOWERED;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  struct lfds710_queue_umm_element LFDS710_PAL_ALIGN(LFDS710_PAL_ALIGN_DOUBLE_POINTER)
    *volatile enqueue[PAC_SIZE],
    *new_enqueue[PAC_SIZE],
    *volatile next[PAC_SIZE];

  LFDS710_PAL_ASSERT( qumms != NULL );
  LFDS710_PAL_ASSERT( qumme != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) qumme->next % LFDS710_PAL_ALIGN_DOUBLE_POINTER == 0 );

  qumme->next[POINTER] = NULL;
  LFDS710_PAL_ATOMIC_ADD( &qumms->aba_counter, 1, qumme->next[COUNTER], struct lfds710_queue_umm_element * );
  LFDS710_MISC_BARRIER_STORE;

  new_enqueue[POINTER] = qumme;

  LFDS710_MISC_BARRIER_LOAD;

  do
  {
    /* TRD : note here the deviation from the white paper
             in the white paper, next is loaded from enqueue, not from qumms->enqueue
             what concerns me is that between the load of enqueue and the load of
             enqueue->next, the element can be dequeued by another thread *and freed*

             by ordering the loads (load barriers), and loading both from qumms,
             the following if(), which checks enqueue is still the same as qumms->enqueue
             still continues to ensure next belongs to enqueue, while avoiding the
             problem with free
    */

    enqueue[COUNTER] = qumms->enqueue[COUNTER];
    enqueue[POINTER] = qumms->enqueue[POINTER];

    LFDS710_MISC_BARRIER_LOAD;

    next[COUNTER] = qumms->enqueue[POINTER]->next[COUNTER];
    next[POINTER] = qumms->enqueue[POINTER]->next[POINTER];

    LFDS710_MISC_BARRIER_LOAD;

    if( qumms->enqueue[COUNTER] == enqueue[COUNTER] and qumms->enqueue[POINTER] == enqueue[POINTER] )
    {
      if( next[POINTER] == NULL )
      {
        new_enqueue[COUNTER] = next[COUNTER] + 1;
        LFDS710_PAL_ATOMIC_DWCAS( enqueue[POINTER]->next, next, new_enqueue, LFDS710_MISC_CAS_STRENGTH_WEAK, result );
        if( result == 1 )
          finished_flag = LFDS710_MISC_FLAG_RAISED;
      }
      else
      {
        next[COUNTER] = enqueue[COUNTER] + 1;
        // TRD : strictly, this is a weak CAS, but we do an extra iteration of the main loop on a fake failure, so we set it to be strong
        LFDS710_PAL_ATOMIC_DWCAS( qumms->enqueue, enqueue, next, LFDS710_MISC_CAS_STRENGTH_STRONG, result );
      }
    }
    else
      result = 0;

    if( result == 0 )
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( qumms->enqueue_backoff, backoff_iteration );
  }
  while( finished_flag == LFDS710_MISC_FLAG_LOWERED );

  // TRD : move enqueue along; only a weak CAS as the dequeue will solve this if it's out of place
  new_enqueue[COUNTER] = enqueue[COUNTER] + 1;
  LFDS710_PAL_ATOMIC_DWCAS( qumms->enqueue, enqueue, new_enqueue, LFDS710_MISC_CAS_STRENGTH_WEAK, result );

  if( result == 0 )
    LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( qumms->enqueue_backoff, backoff_iteration );

  LFDS710_BACKOFF_AUTOTUNE( qumms->enqueue_backoff, backoff_iteration );

  return;
}

