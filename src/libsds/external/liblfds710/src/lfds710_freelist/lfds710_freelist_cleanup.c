/***** includes *****/
#include "lfds710_freelist_internal.h"





/****************************************************************************/
void lfds710_freelist_cleanup( struct lfds710_freelist_state *fs,
                               void (*element_cleanup_callback)(struct lfds710_freelist_state *fs, struct lfds710_freelist_element *fe) )
{
  struct lfds710_freelist_element
    *fe,
    *fe_temp;

  LFDS710_PAL_ASSERT( fs != NULL );
  // TRD : element_cleanup_callback can be NULL

  LFDS710_MISC_BARRIER_LOAD;

  if( element_cleanup_callback != NULL )
  {
    fe = fs->top[POINTER];

    while( fe != NULL )
    {
      fe_temp = fe;
      fe = fe->next;

      element_cleanup_callback( fs, fe_temp );
    }
  }

  return;
}

