/***** includes *****/
#include "lfds710_freelist_internal.h"

/***** private prototypes *****/
static void lfds710_freelist_internal_freelist_validate( struct lfds710_freelist_state *fs,
                                                         struct lfds710_misc_validation_info *vi,
                                                         enum lfds710_misc_validity *lfds710_freelist_validity );





/****************************************************************************/
void lfds710_freelist_query( struct lfds710_freelist_state *fs,
                             enum lfds710_freelist_query query_type,
                             void *query_input,
                             void *query_output )
{
  struct lfds710_freelist_element
    *fe;

  LFDS710_PAL_ASSERT( fs != NULL );
  // TRD : query_type can be any value in its range

  LFDS710_MISC_BARRIER_LOAD;

  switch( query_type )
  {
    case LFDS710_FREELIST_QUERY_SINGLETHREADED_GET_COUNT:
    {
      lfds710_pal_uint_t
        loop,
        subloop;

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      *(lfds710_pal_uint_t *) query_output = 0;

      // TRD : count the elements in the elimination array
      for( loop = 0 ; loop < fs->elimination_array_size_in_elements ; loop++ )
        for( subloop = 0 ; subloop < LFDS710_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS ; subloop++ )
          if( fs->elimination_array[loop][subloop] != NULL )
            ( *(lfds710_pal_uint_t *) query_output )++;

      // TRD : count the elements on the freelist
      fe = (struct lfds710_freelist_element *) fs->top[POINTER];

      while( fe != NULL )
      {
        ( *(lfds710_pal_uint_t *) query_output )++;
        fe = (struct lfds710_freelist_element *) fe->next;
      }
    }
    break;

    case LFDS710_FREELIST_QUERY_SINGLETHREADED_VALIDATE:
      // TRD : query_input can be NULL
      LFDS710_PAL_ASSERT( query_output != NULL );

      lfds710_freelist_internal_freelist_validate( fs, (struct lfds710_misc_validation_info *) query_input, (enum lfds710_misc_validity *) query_output );
    break;

    case LFDS710_FREELIST_QUERY_GET_ELIMINATION_ARRAY_EXTRA_ELEMENTS_IN_FREELIST_ELEMENTS:
    {
      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      ( *(lfds710_pal_uint_t *) query_output ) = (fs->elimination_array_size_in_elements-1) * LFDS710_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS;
    }
    break;
  }

  return;
}





/****************************************************************************/
static void lfds710_freelist_internal_freelist_validate( struct lfds710_freelist_state *fs,
                                                         struct lfds710_misc_validation_info *vi,
                                                         enum lfds710_misc_validity *lfds710_freelist_validity )
{
  lfds710_pal_uint_t
    number_elements = 0;

  struct lfds710_freelist_element
    *fe_slow,
    *fe_fast;

  LFDS710_PAL_ASSERT( fs != NULL );
  // TRD : vi can be NULL
  LFDS710_PAL_ASSERT( lfds710_freelist_validity != NULL );

  *lfds710_freelist_validity = LFDS710_MISC_VALIDITY_VALID;

  fe_slow = fe_fast = (struct lfds710_freelist_element *) fs->top[POINTER];

  /* TRD : first, check for a loop
           we have two pointers
           both of which start at the top of the freelist
           we enter a loop
           and on each iteration
           we advance one pointer by one element
           and the other by two

           we exit the loop when both pointers are NULL
           (have reached the end of the freelist)

           or

           if we fast pointer 'sees' the slow pointer
           which means we have a loop
  */

  if( fe_slow != NULL )
    do
    {
      fe_slow = fe_slow->next;

      if( fe_fast != NULL )
        fe_fast = fe_fast->next;

      if( fe_fast != NULL )
        fe_fast = fe_fast->next;
    }
    while( fe_slow != NULL and fe_fast != fe_slow );

  if( fe_fast != NULL and fe_slow != NULL and fe_fast == fe_slow )
    *lfds710_freelist_validity = LFDS710_MISC_VALIDITY_INVALID_LOOP;

  /* TRD : now check for expected number of elements
           vi can be NULL, in which case we do not check
           we know we don't have a loop from our earlier check
  */

  if( *lfds710_freelist_validity == LFDS710_MISC_VALIDITY_VALID and vi != NULL )
  {
    lfds710_freelist_query( fs, LFDS710_FREELIST_QUERY_SINGLETHREADED_GET_COUNT, NULL, (void *) &number_elements );

    if( number_elements < vi->min_elements )
      *lfds710_freelist_validity = LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS;

    if( number_elements > vi->max_elements )
      *lfds710_freelist_validity = LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS;
  }

  return;
}

