/***** includes *****/
#include "lfds710_list_addonly_singlylinked_ordered_internal.h"





/****************************************************************************/
int lfds710_list_aso_get_by_key( struct lfds710_list_aso_state *lasos,
                                 void *key,
                                 struct lfds710_list_aso_element **lasoe )
{
  int
    cr = !0,
    rv = 1;

  LFDS710_PAL_ASSERT( lasos != NULL );
  // TRD : key can be NULL
  LFDS710_PAL_ASSERT( lasoe != NULL );

  while( cr != 0 and LFDS710_LIST_ASO_GET_START_AND_THEN_NEXT(*lasos, *lasoe) )
    cr = lasos->key_compare_function( key, (*lasoe)->key );

  if( *lasoe == NULL )
    rv = 0;

  return rv;
}

