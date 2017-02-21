/***** includes *****/
#include "lfds710_list_addonly_singlylinked_unordered_internal.h"





/****************************************************************************/
void lfds710_list_asu_cleanup( struct lfds710_list_asu_state *lasus,
                               void (*element_cleanup_callback)(struct lfds710_list_asu_state *lasus, struct lfds710_list_asu_element *lasue) )
{
  struct lfds710_list_asu_element
    *lasue,
    *temp;

  LFDS710_PAL_ASSERT( lasus != NULL );
  // TRD : element_cleanup_callback can be NULL

  LFDS710_MISC_BARRIER_LOAD;

  if( element_cleanup_callback == NULL )
    return;

  lasue = LFDS710_LIST_ASU_GET_START( *lasus );

  while( lasue != NULL )
  {
    temp = lasue;

    lasue = LFDS710_LIST_ASU_GET_NEXT( *lasue );

    element_cleanup_callback( lasus, temp );
  }

  return;
}

