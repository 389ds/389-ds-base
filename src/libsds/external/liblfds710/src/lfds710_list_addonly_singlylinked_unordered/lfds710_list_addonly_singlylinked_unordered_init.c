/***** includes *****/
#include "lfds710_list_addonly_singlylinked_unordered_internal.h"





/****************************************************************************/
void lfds710_list_asu_init_valid_on_current_logical_core( struct lfds710_list_asu_state *lasus,
                                                          void *user_state )
{
  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasus->dummy_element % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasus->end % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasus->start % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  // TRD : user_state can be NULL

  // TRD : dummy start element - makes code easier when you can always use ->next
  lasus->start = lasus->end = &lasus->dummy_element;

  lasus->start->next = NULL;
  lasus->start->value = NULL;
  lasus->user_state = user_state;

  lfds710_misc_internal_backoff_init( &lasus->after_backoff );
  lfds710_misc_internal_backoff_init( &lasus->start_backoff );
  lfds710_misc_internal_backoff_init( &lasus->end_backoff );

  LFDS710_MISC_BARRIER_STORE;

  lfds710_misc_force_store();

  return;
}

