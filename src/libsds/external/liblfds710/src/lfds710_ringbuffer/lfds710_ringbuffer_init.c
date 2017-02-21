/***** includes *****/
#include "lfds710_ringbuffer_internal.h"





/****************************************************************************/
void lfds710_ringbuffer_init_valid_on_current_logical_core( struct lfds710_ringbuffer_state *rs,
                                                            struct lfds710_ringbuffer_element *re_array_inc_dummy,
                                                            lfds710_pal_uint_t number_elements_inc_dummy,
                                                            void *user_state )
{
  lfds710_pal_uint_t
    loop;

  LFDS710_PAL_ASSERT( rs != NULL );
  LFDS710_PAL_ASSERT( re_array_inc_dummy != NULL );
  LFDS710_PAL_ASSERT( number_elements_inc_dummy >= 2 );
  // TRD : user_state can be NULL

  rs->user_state = user_state;

  re_array_inc_dummy[0].qumme_use = &re_array_inc_dummy[0].qumme;

  lfds710_freelist_init_valid_on_current_logical_core( &rs->fs, NULL, 0, rs );
  lfds710_queue_umm_init_valid_on_current_logical_core( &rs->qumms, &re_array_inc_dummy[0].qumme, rs );

  for( loop = 1 ; loop < number_elements_inc_dummy ; loop++ )
  {
    re_array_inc_dummy[loop].qumme_use = &re_array_inc_dummy[loop].qumme;
    LFDS710_FREELIST_SET_VALUE_IN_ELEMENT( re_array_inc_dummy[loop].fe, &re_array_inc_dummy[loop] );
    lfds710_freelist_push( &rs->fs, &re_array_inc_dummy[loop].fe, NULL );
  }

  LFDS710_MISC_BARRIER_STORE;

  lfds710_misc_force_store();

  return;
}

