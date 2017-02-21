/***** includes *****/
#include "lfds710_misc_internal.h"





/****************************************************************************/
#pragma warning( disable : 4100 )

void lfds710_misc_query( enum lfds710_misc_query query_type,
                         void *query_input,
                         void *query_output )
{
  // TRD : query type can be any value in its range
  // TRD : query_input can be NULL in some cases
  // TRD : query_outputput can be NULL in some cases

  switch( query_type )
  {
    case LFDS710_MISC_QUERY_GET_BUILD_AND_VERSION_STRING:
    {
      static char const
        * const build_and_version_string = "liblfds " LFDS710_MISC_VERSION_STRING " (" BUILD_TYPE_STRING ", " LFDS710_PAL_OS_STRING ", " MODE_TYPE_STRING ", " LFDS710_PAL_PROCESSOR_STRING ", " LFDS710_PAL_COMPILER_STRING ")";

      LFDS710_PAL_ASSERT( query_input == NULL );
      LFDS710_PAL_ASSERT( query_output != NULL );

      *(char const **) query_output = build_and_version_string;
    }
    break;
  }

  return;
}

#pragma warning( default : 4100 )

