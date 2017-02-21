/***** includes *****/
#include "lfds710_list_addonly_singlylinked_unordered_internal.h"





/****************************************************************************/
void lfds710_list_asu_insert_at_position( struct lfds710_list_asu_state *lasus,
                                          struct lfds710_list_asu_element *lasue,
                                          struct lfds710_list_asu_element *lasue_predecessor,
                                          enum lfds710_list_asu_position position )
{
  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( lasue != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->next % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->value % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  // TRD : lasue_predecessor asserted in the switch
  // TRD : position can be any value in its range

  switch( position )
  {
    case LFDS710_LIST_ASU_POSITION_START:
      lfds710_list_asu_insert_at_start( lasus, lasue );
    break;

    case LFDS710_LIST_ASU_POSITION_END:
      lfds710_list_asu_insert_at_end( lasus, lasue );
    break;

    case LFDS710_LIST_ASU_POSITION_AFTER:
      lfds710_list_asu_insert_after_element( lasus, lasue, lasue_predecessor );
    break;
  }

  return;
}





/****************************************************************************/
void lfds710_list_asu_insert_at_start( struct lfds710_list_asu_state *lasus,
                                       struct lfds710_list_asu_element *lasue )
{
  char unsigned 
    result;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( lasue != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->next % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->value % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );

  LFDS710_MISC_BARRIER_LOAD;

  lasue->next = lasus->start->next;

  do
  {
    LFDS710_MISC_BARRIER_STORE;
    LFDS710_PAL_ATOMIC_CAS( &lasus->start->next, (struct lfds710_list_asu_element **) &lasue->next, lasue, LFDS710_MISC_CAS_STRENGTH_WEAK, result );
    if( result == 0 )
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( lasus->start_backoff, backoff_iteration );
  }
  while( result == 0 );

  LFDS710_BACKOFF_AUTOTUNE( lasus->start_backoff, backoff_iteration );

  return;
}





/****************************************************************************/
void lfds710_list_asu_insert_at_end( struct lfds710_list_asu_state *lasus,
                                     struct lfds710_list_asu_element *lasue )
{
  char unsigned 
    result;

  enum lfds710_misc_flag
    finished_flag = LFDS710_MISC_FLAG_LOWERED;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  struct lfds710_list_asu_element LFDS710_PAL_ALIGN(LFDS710_PAL_ALIGN_SINGLE_POINTER)
    *compare;

  struct lfds710_list_asu_element
    *volatile lasue_next,
    *volatile lasue_end;

  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( lasue != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->next % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->value % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );

  /* TRD : begin by assuming end is correctly pointing to the final element
           try to link (comparing for next being NULL)
           if we fail, move down list till we find last element
           and retry
           when successful, update end to ourselves

           note there's a leading dummy element
           so lasus->end always points to an element
  */

  LFDS710_MISC_BARRIER_LOAD;

  lasue->next = NULL;
  lasue_end = lasus->end;

  while( finished_flag == LFDS710_MISC_FLAG_LOWERED )
  {
    compare = NULL;

    LFDS710_MISC_BARRIER_STORE;
    LFDS710_PAL_ATOMIC_CAS( &lasue_end->next, &compare, lasue, LFDS710_MISC_CAS_STRENGTH_STRONG, result );

    if( result == 1 )
      finished_flag = LFDS710_MISC_FLAG_RAISED;
    else
    {
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( lasus->end_backoff, backoff_iteration );

      lasue_end = compare;
      lasue_next = LFDS710_LIST_ASU_GET_NEXT( *lasue_end );

      while( lasue_next != NULL )
      {
        lasue_end = lasue_next;
        lasue_next = LFDS710_LIST_ASU_GET_NEXT( *lasue_end );
      }
    }
  }

  lasus->end = lasue;

  LFDS710_BACKOFF_AUTOTUNE( lasus->end_backoff, backoff_iteration );

  return;
}





/****************************************************************************/
#pragma warning( disable : 4100 )

void lfds710_list_asu_insert_after_element( struct lfds710_list_asu_state *lasus,
                                            struct lfds710_list_asu_element *lasue,
                                            struct lfds710_list_asu_element *lasue_predecessor )
{
  char unsigned 
    result;

  lfds710_pal_uint_t
    backoff_iteration = LFDS710_BACKOFF_INITIAL_VALUE;

  LFDS710_PAL_ASSERT( lasus != NULL );
  LFDS710_PAL_ASSERT( lasue != NULL );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->next % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( (lfds710_pal_uint_t) &lasue->value % LFDS710_PAL_ALIGN_SINGLE_POINTER == 0 );
  LFDS710_PAL_ASSERT( lasue_predecessor != NULL );

  LFDS710_MISC_BARRIER_LOAD;

  lasue->next = lasue_predecessor->next;

  do
  {
    LFDS710_MISC_BARRIER_STORE;
    LFDS710_PAL_ATOMIC_CAS( &lasue_predecessor->next, (struct lfds710_list_asu_element **) &lasue->next, lasue, LFDS710_MISC_CAS_STRENGTH_WEAK, result );
    if( result == 0 )
      LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( lasus->after_backoff, backoff_iteration );
  }
  while( result == 0 );

  LFDS710_BACKOFF_AUTOTUNE( lasus->after_backoff, backoff_iteration );

  return;
}

#pragma warning( default : 4100 )

