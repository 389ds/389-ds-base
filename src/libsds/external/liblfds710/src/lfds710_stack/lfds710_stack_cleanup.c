/***** includes *****/
#include "lfds710_stack_internal.h"





/****************************************************************************/
void lfds710_stack_cleanup( struct lfds710_stack_state *ss,
                            void (*element_cleanup_callback)(struct lfds710_stack_state *ss, struct lfds710_stack_element *se) )
{
  struct lfds710_stack_element
    *se,
    *se_temp;

  LFDS710_PAL_ASSERT( ss != NULL );
  // TRD : element_cleanup_callback can be NULL

  LFDS710_MISC_BARRIER_LOAD;

  if( element_cleanup_callback != NULL )
  {
    se = ss->top[POINTER];

    while( se != NULL )
    {
      se_temp = se;
      se = se->next;

      element_cleanup_callback( ss, se_temp );
    }
  }

  return;
}

