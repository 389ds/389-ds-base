/***** includes *****/
#include "lfds710_list_addonly_singlylinked_unordered_internal.h"





/****************************************************************************/
int lfds710_list_asu_get_by_key( struct lfds710_list_asu_state *lasus,
                                 int (*key_compare_function)(void const *new_key, void const *existing_key),
                                 void *key, 
                                 struct lfds710_list_asu_element **lasue )
{
  int
    cr = !0,
    rv = 1;

  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( key_compare_function != NULL );
  // TRD : key can be NULL
  LFDS710_PAL_ASSERT( lasue != NULL );

  *lasue = NULL;

  while( cr != 0 and LFDS710_LIST_ASU_GET_START_AND_THEN_NEXT(*lasus, *lasue) )
    cr = key_compare_function( key, (*lasue)->key );

  if( *lasue == NULL )
    rv = 0;

  return rv;
}

