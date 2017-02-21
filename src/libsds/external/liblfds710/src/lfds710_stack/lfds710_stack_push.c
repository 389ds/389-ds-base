/***** includes *****/
#include "lfds710_stack_internal.h"





/****************************************************************************/
void lfds710_stack_push( struct lfds710_stack_state *ss,
                         struct lfds710_stack_element *se )
{
  char unsigned
    result;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  struct lfds710_stack_element LFDS710_PAL_ALIGN(LFDS710_PAL_ALIGN_DOUBLE_POINTER)
    *new_top[PAC_SIZE],
    *volatile original_top[PAC_SIZE];

  LFDS710_PAL_ASSERT( ss != NULL );
  LFDS710_PAL_ASSERT( se != NULL );

  new_top[POINTER] = se;

  original_top[COUNTER] = ss->top[COUNTER];
  original_top[POINTER] = ss->top[POINTER];

  do
  {
    se->next = original_top[POINTER];
    LFDS710_MISC_BARRIER_STORE;

    new_top[COUNTER] = original_top[COUNTER] + 1;
    LFDS710_PAL_ATOMIC_DWCAS( ss->top, original_top, new_top, LFDS710_MISC_CAS_STRENGTH_WEAK, result );

    if( result == 0 )
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( ss->push_backoff, backoff_iteration );
  }
  while( result == 0 );

  LFDS710_BACKOFF_AUTOTUNE( ss->push_backoff, backoff_iteration );

  return;
}

