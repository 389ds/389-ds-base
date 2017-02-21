/***** includes *****/
#include "lfds710_stack_internal.h"





/****************************************************************************/
int lfds710_stack_pop( struct lfds710_stack_state *ss,
                       struct lfds710_stack_element **se )
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

  LFDS710_MISC_BARRIER_LOAD;

  original_top[COUNTER] = ss->top[COUNTER];
  original_top[POINTER] = ss->top[POINTER];

  do
  {
    if( original_top[POINTER] == NULL )
    {
      *se = NULL;
      return 0;
    }

    new_top[COUNTER] = original_top[COUNTER] + 1;
    new_top[POINTER] = original_top[POINTER]->next;

    LFDS710_PAL_ATOMIC_DWCAS( ss->top, original_top, new_top, LFDS710_MISC_CAS_STRENGTH_WEAK, result );

    if( result == 0 )
    {
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( ss->pop_backoff, backoff_iteration );
      LFDS710_MISC_BARRIER_LOAD;
    }
  }
  while( result == 0 );

  *se = original_top[POINTER];

  LFDS710_BACKOFF_AUTOTUNE( ss->pop_backoff, backoff_iteration );

  return 1;
}

