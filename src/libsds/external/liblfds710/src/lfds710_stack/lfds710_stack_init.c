/***** includes *****/
#include "lfds710_stack_internal.h"





/****************************************************************************/
void lfds710_stack_init_valid_on_current_logical_core( struct lfds710_stack_state *ss,
                                                       void *user_state )
{
  LFDS710_PAL_ASSERT( ss != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) ss->top % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &ss->user_state % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  // TRD : user_state can be NULL

  ss->top[POINTER] = NULL;
  ss->top[COUNTER] = 0;

  ss->user_state = user_state;

  lfds710_misc_internal_backoff_init( &ss->pop_backoff );
  lfds710_misc_internal_backoff_init( &ss->push_backoff );

  LFDS710_MISC_BARRIER_STORE;

  lfds710_misc_force_store();

  return;
}

