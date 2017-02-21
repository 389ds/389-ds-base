/***** includes *****/
#include "lfds710_prng_internal.h"





/****************************************************************************/
void lfds710_prng_init_valid_on_current_logical_core( struct lfds710_prng_state *ps, lfds710_pal_uint_t seed )
{
  LFDS710_PAL_ASSERT( ps != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &ps->entropy % LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES == 0 );
  // TRD : seed can be any value in its range (unlike for the mixing function)

  LFDS710_PRNG_ST_MIXING_FUNCTION( seed );

  ps->entropy = seed;

  LFDS710_MISC_BARRIER_STORE;

  lfds710_misc_force_store();

  return;
}





/****************************************************************************/
void lfds710_prng_st_init( struct lfds710_prng_st_state *psts, lfds710_pal_uint_t seed )
{
  LFDS710_PAL_ASSERT( psts != NULL );
  LFDS710_PAL_ASSERT( seed != 0 );

  LFDS710_PRNG_ST_MIXING_FUNCTION( seed );

  psts->entropy = seed;

  return;
}

