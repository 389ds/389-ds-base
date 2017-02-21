/***** public prototypes *****/
#include "../inc/liblfds710.h"

/***** defines *****/
#define and &&
#define or  ||

#define NO_FLAGS 0x0

#define LFDS710_VERSION_STRING   "7.1.0"
#define LFDS710_VERSION_INTEGER  710

#if( defined KERNEL_MODE )
  #define MODE_TYPE_STRING "kernel-mode"
#endif

#if( !defined KERNEL_MODE )
  #define MODE_TYPE_STRING "user-mode"
#endif

#if( defined NDEBUG && !defined COVERAGE && !defined TSAN && !defined PROF )
  #define BUILD_TYPE_STRING "release"
#endif

#if( !defined NDEBUG && !defined COVERAGE && !defined TSAN && !defined PROF )
  #define BUILD_TYPE_STRING "debug"
#endif

#if( !defined NDEBUG && defined COVERAGE && !defined TSAN && !defined PROF )
  #define BUILD_TYPE_STRING "coverage"
#endif

#if( !defined NDEBUG && !defined COVERAGE && defined TSAN && !defined PROF )
  #define BUILD_TYPE_STRING "threadsanitizer"
#endif

#if( !defined NDEBUG && !defined COVERAGE && !defined TSAN && defined PROF )
  #define BUILD_TYPE_STRING "profiling"
#endif

#define LFDS710_BACKOFF_INITIAL_VALUE  0
#define LFDS710_BACKOFF_LIMIT          10

#define LFDS710_BACKOFF_EXPONENTIAL_BACKOFF( backoff_state, backoff_iteration )                \
{                                                                                              \
  lfds710_pal_uint_t volatile                                                                  \
    loop;                                                                                      \
                                                                                               \
  lfds710_pal_uint_t                                                                           \
    endloop;                                                                                   \
                                                                                               \
  if( (backoff_iteration) == LFDS710_BACKOFF_LIMIT )                                           \
    (backoff_iteration) = LFDS710_BACKOFF_INITIAL_VALUE;                                       \
  else                                                                                         \
  {                                                                                            \
    endloop = ( ((lfds710_pal_uint_t) 0x1) << (backoff_iteration) ) * (backoff_state).metric;  \
    for( loop = 0 ; loop < endloop ; loop++ );                                                 \
  }                                                                                            \
                                                                                               \
  (backoff_iteration)++;                                                                       \
}

#define LFDS710_BACKOFF_AUTOTUNE( bs, backoff_iteration )                                                                           \
{                                                                                                                                   \
  if( (backoff_iteration) < 2 )                                                                                                     \
    (bs).backoff_iteration_frequency_counters[(backoff_iteration)]++;                                                               \
                                                                                                                                    \
  if( ++(bs).total_operations >= 10000 and (bs).lock == LFDS710_MISC_FLAG_LOWERED )                                                 \
  {                                                                                                                                 \
    char unsigned                                                                                                                   \
      result;                                                                                                                       \
                                                                                                                                    \
    lfds710_pal_uint_t LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)                                                     \
      compare = LFDS710_MISC_FLAG_LOWERED;                                                                                          \
                                                                                                                                    \
    LFDS710_PAL_ATOMIC_CAS( &(bs).lock, &compare, LFDS710_MISC_FLAG_RAISED, LFDS710_MISC_CAS_STRENGTH_WEAK, result );               \
                                                                                                                                    \
    if( result == 1 )                                                                                                               \
    {                                                                                                                               \
      /* TRD : if E[1] is less than 1/100th of E[0], decrease the metric, to increase E[1] */                                       \
      if( (bs).backoff_iteration_frequency_counters[1] < (bs).backoff_iteration_frequency_counters[0] / 100 )                       \
      {                                                                                                                             \
        if( (bs).metric >= 11 )                                                                                                     \
          (bs).metric -= 10;                                                                                                        \
      }                                                                                                                             \
      else                                                                                                                          \
        (bs).metric += 10;                                                                                                          \
                                                                                                                                    \
      (bs).backoff_iteration_frequency_counters[0] = 0;                                                                             \
      (bs).backoff_iteration_frequency_counters[1] = 0;                                                                             \
      (bs).total_operations = 0;                                                                                                    \
                                                                                                                                    \
      LFDS710_MISC_BARRIER_STORE;                                                                                                   \
                                                                                                                                    \
      LFDS710_PAL_ATOMIC_SET( &(bs).lock, LFDS710_MISC_FLAG_LOWERED );                                                              \
    }                                                                                                                               \
  }                                                                                                                                 \
}

/***** library-wide prototypes *****/
void lfds710_misc_internal_backoff_init( struct lfds710_misc_backoff_state *bs );

