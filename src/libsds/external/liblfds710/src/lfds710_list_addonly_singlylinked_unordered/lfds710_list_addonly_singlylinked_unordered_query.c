/***** includes *****/
#include "lfds710_list_addonly_singlylinked_unordered_internal.h"

/***** private prototypes *****/
static void lfds710_list_asu_internal_validate( struct lfds710_list_asu_state *lasus,
                                                struct lfds710_misc_validation_info *vi,
                                                enum lfds710_misc_validity *lfds710_list_asu_validity );





/****************************************************************************/
void lfds710_list_asu_query( struct lfds710_list_asu_state *lasus,
                             enum lfds710_list_asu_query query_type,
                             void *query_input,
                             void *query_output )
{
  LFDS710_PAL_ASSERT( lasus != NULL );
  // TRD : query_type can be any value in its range

  LFDS710_MISC_BARRIER_LOAD;

  switch( query_type )
  {
    case LFDS710_LIST_ASU_QUERY_GET_POTENTIALLY_INACCURATE_COUNT:
    {
      struct lfds710_list_asu_element
        *lasue = NULL;

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      *(lfds710_pal_uint_t *) query_output = 0;

      while( LFDS710_LIST_ASU_GET_START_AND_THEN_NEXT(*lasus, lasue) )
        ( *(lfds710_pal_uint_t *) query_output )++;
    }
    break;

    case LFDS710_LIST_ASU_QUERY_SINGLETHREADED_VALIDATE:
      // TRD : query_input can be NULL
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_list_asu_internal_validate( lasus, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output );
    break;
  }

  return;
}






/****************************************************************************/
static void lfds710_list_asu_internal_validate( struct lfds710_list_asu_state *lasus,
                                                struct lfds710_misc_validation_info *vi,
                                                enum lfds710_misc_validity *lfds710_list_asu_validity )
{
  lfds710_pal_uint_t
    number_elements = 0;

  struct lfds710_list_asu_element
    *lasue_fast,
    *lasue_slow;

  LFDS710_PAL_ASSERT( lasus!= NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_list_asu_validity != NULL );

  *lfds710_list_asu_validity = LFDS710_MISC_VALIDITY_VALID;

  lasue_slow = lasue_fast = lasus->start->next;

  /* TRD : first, check for a loop
           we have two pointers
           both of which start at the start of the list
           we enter a loop
           and on each iteration
           we advance one pointer by one element
           and the other by two

           we exit the loop when both pointers are NULL
           (have reached the end of the queue)

           or

           if we fast pointer 'sees' the slow pointer
           which means we have a loop
  */

  if( lasue_slow != NULL )
    do
    {
      lasue_slow = lasue_slow->next;

      if( lasue_fast != NULL )
        lasue_fast = lasue_fast->next;

      if( lasue_fast != NULL )
        lasue_fast = lasue_fast->next;
    }
    while( lasue_slow != NULL and lasue_fast != lasue_slow );

  if( lasue_fast != NULL and lasue_slow != NULL and lasue_fast == lasue_slow )
    *lfds710_list_asu_validity = LFDS710_MISC_VALIDITY_INVALID_LOOP;

  /* TRD : now check for expected number of elements
           vi can be NULL, in which case we do not check
           we know we don't have a loop from our earlier check
  */

  if( *lfds710_list_asu_validity == LFDS710_MISC_VALIDITY_VALID and vi != NULL )
  {
    lfds710_list_asu_query( lasus, LFDS710_LIST_ASU_QUERY_GET_POTENTIALLY_INACCURATE_COUNT, NULL, &number_elements );

    if( number_elements < vi->min_elements )
      *lfds710_list_asu_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

    if( number_elements > vi->max_elements )
      *lfds710_list_asu_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;
  }

  return;
}

