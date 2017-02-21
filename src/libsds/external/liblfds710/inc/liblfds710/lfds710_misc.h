/***** defines *****/
#define LFDS710_MISC_VERSION_STRING   "7.1.0"
#define LFDS710_MISC_VERSION_INTEGER  710

#ifndef NULL
  #define NULL ( (void *) 0 )
#endif

#define POINTER   0
#define COUNTER   1
#define PAC_SIZE  2

#define LFDS710_MISC_DELIBERATELY_CRASH  { char *c = 0; *c = 0; }

#if( !defined LFDS710_PAL_ATOMIC_ADD )
  #define LFDS710_PAL_NO_ATOMIC_ADD
  #define LFDS710_MISC_ATOMIC_SUPPORT_ADD 0
  #define LFDS710_PAL_ATOMIC_ADD( pointer_to_target, value, result, result_type )        \
  {                                                                                      \
    LFDS710_PAL_ASSERT( !"LFDS710_PAL_ATOMIC_ADD not implemented for this platform." );  \
    LFDS710_MISC_DELIBERATELY_CRASH;                                                     \
  }
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_ADD 1
#endif

#if( !defined LFDS710_PAL_ATOMIC_CAS )
  #define LFDS710_PAL_NO_ATOMIC_CAS
  #define LFDS710_MISC_ATOMIC_SUPPORT_CAS 0
  #define LFDS710_PAL_ATOMIC_CAS( pointer_to_destination, pointer_to_compare, new_destination, cas_strength, result )  \
  {                                                                                                                    \
    LFDS710_PAL_ASSERT( !"LFDS710_PAL_ATOMIC_CAS not implemented for this platform." );                                \
    (result) = 0;                                                                                                      \
    LFDS710_MISC_DELIBERATELY_CRASH;                                                                                   \
  }
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_CAS 1
#endif

#if( !defined LFDS710_PAL_ATOMIC_DWCAS )
  #define LFDS710_PAL_NO_ATOMIC_DWCAS
  #define LFDS710_MISC_ATOMIC_SUPPORT_DWCAS 0
  #define LFDS710_PAL_ATOMIC_DWCAS( pointer_to_destination, pointer_to_compare, pointer_to_new_destination, cas_strength, result )  \
  {                                                                                                                                 \
    LFDS710_PAL_ASSERT( !"LFDS710_PAL_ATOMIC_DWCAS not implemented for this platform." );                                           \
    (result) = 0;                                                                                                                   \
    LFDS710_MISC_DELIBERATELY_CRASH;                                                                                                \
  }
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_DWCAS 1
#endif

#if( !defined LFDS710_PAL_ATOMIC_EXCHANGE )
  #define LFDS710_PAL_NO_ATOMIC_EXCHANGE
  #define LFDS710_MISC_ATOMIC_SUPPORT_EXCHANGE 0
  #define LFDS710_PAL_ATOMIC_EXCHANGE( pointer_to_destination, new_value, original_value, value_type )  \
  {                                                                                                     \
    LFDS710_PAL_ASSERT( !"LFDS710_PAL_ATOMIC_EXCHANGE not implemented for this platform." );            \
    LFDS710_MISC_DELIBERATELY_CRASH;                                                                    \
  }
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_EXCHANGE 1
#endif

#if( !defined LFDS710_PAL_ATOMIC_SET )
  #define LFDS710_PAL_NO_ATOMIC_SET
  #define LFDS710_MISC_ATOMIC_SUPPORT_SET 0
  #define LFDS710_PAL_ATOMIC_SET( pointer_to_destination, new_value )                    \
  {                                                                                      \
    LFDS710_PAL_ASSERT( !"LFDS710_PAL_ATOMIC_SET not implemented for this platform." );  \
    LFDS710_MISC_DELIBERATELY_CRASH;                                                     \
  }
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_SET 1
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_LOAD && defined LFDS710_PAL_BARRIER_PROCESSOR_LOAD )
  #define LFDS710_MISC_BARRIER_LOAD  ( LFDS710_PAL_BARRIER_COMPILER_LOAD, LFDS710_PAL_BARRIER_PROCESSOR_LOAD, LFDS710_PAL_BARRIER_COMPILER_LOAD )
#endif

#if( (!defined LFDS710_PAL_BARRIER_COMPILER_LOAD || defined LFDS710_PAL_COMPILER_BARRIERS_MISSING_PRESUMED_HAVING_A_GOOD_TIME) && defined LFDS710_PAL_BARRIER_PROCESSOR_LOAD )
  #define LFDS710_MISC_BARRIER_LOAD  LFDS710_PAL_BARRIER_PROCESSOR_LOAD
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_LOAD && !defined LFDS710_PAL_BARRIER_PROCESSOR_LOAD )
  #define LFDS710_MISC_BARRIER_LOAD  LFDS710_PAL_BARRIER_COMPILER_LOAD
#endif

#if( !defined LFDS710_PAL_BARRIER_COMPILER_LOAD && !defined LFDS710_PAL_BARRIER_PROCESSOR_LOAD )
  #define LFDS710_MISC_BARRIER_LOAD
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_STORE && defined LFDS710_PAL_BARRIER_PROCESSOR_STORE )
  #define LFDS710_MISC_BARRIER_STORE  ( LFDS710_PAL_BARRIER_COMPILER_STORE, LFDS710_PAL_BARRIER_PROCESSOR_STORE, LFDS710_PAL_BARRIER_COMPILER_STORE )
#endif

#if( (!defined LFDS710_PAL_BARRIER_COMPILER_STORE || defined LFDS710_PAL_COMPILER_BARRIERS_MISSING_PRESUMED_HAVING_A_GOOD_TIME) && defined LFDS710_PAL_BARRIER_PROCESSOR_STORE )
  #define LFDS710_MISC_BARRIER_STORE  LFDS710_PAL_BARRIER_PROCESSOR_STORE
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_STORE && !defined LFDS710_PAL_BARRIER_PROCESSOR_STORE )
  #define LFDS710_MISC_BARRIER_STORE  LFDS710_PAL_BARRIER_COMPILER_STORE
#endif

#if( !defined LFDS710_PAL_BARRIER_COMPILER_STORE && !defined LFDS710_PAL_BARRIER_PROCESSOR_STORE )
  #define LFDS710_MISC_BARRIER_STORE
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_FULL && defined LFDS710_PAL_BARRIER_PROCESSOR_FULL )
  #define LFDS710_MISC_BARRIER_FULL  ( LFDS710_PAL_BARRIER_COMPILER_FULL, LFDS710_PAL_BARRIER_PROCESSOR_FULL, LFDS710_PAL_BARRIER_COMPILER_FULL )
#endif

#if( (!defined LFDS710_PAL_BARRIER_COMPILER_FULL || defined LFDS710_PAL_COMPILER_BARRIERS_MISSING_PRESUMED_HAVING_A_GOOD_TIME) && defined LFDS710_PAL_BARRIER_PROCESSOR_FULL )
  #define LFDS710_MISC_BARRIER_FULL  LFDS710_PAL_BARRIER_PROCESSOR_FULL
#endif

#if( defined LFDS710_PAL_BARRIER_COMPILER_FULL && !defined LFDS710_PAL_BARRIER_PROCESSOR_FULL )
  #define LFDS710_MISC_BARRIER_FULL  LFDS710_PAL_BARRIER_COMPILER_FULL
#endif

#if( !defined LFDS710_PAL_BARRIER_COMPILER_FULL && !defined LFDS710_PAL_BARRIER_PROCESSOR_FULL )
  #define LFDS710_MISC_BARRIER_FULL
#endif

#if( (defined LFDS710_PAL_BARRIER_COMPILER_LOAD && defined LFDS710_PAL_BARRIER_COMPILER_STORE && defined LFDS710_PAL_BARRIER_COMPILER_FULL) || (defined LFDS710_PAL_COMPILER_BARRIERS_MISSING_PRESUMED_HAVING_A_GOOD_TIME) )
  #define LFDS710_MISC_ATOMIC_SUPPORT_COMPILER_BARRIERS  1
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_COMPILER_BARRIERS  0
#endif

#if( defined LFDS710_PAL_BARRIER_PROCESSOR_LOAD && defined LFDS710_PAL_BARRIER_PROCESSOR_STORE && defined LFDS710_PAL_BARRIER_PROCESSOR_FULL )
  #define LFDS710_MISC_ATOMIC_SUPPORT_PROCESSOR_BARRIERS  1
#else
  #define LFDS710_MISC_ATOMIC_SUPPORT_PROCESSOR_BARRIERS  0
#endif

#define LFDS710_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE  LFDS710_MISC_BARRIER_LOAD
#define LFDS710_MISC_FLUSH                                                                                    { LFDS710_MISC_BARRIER_STORE; lfds710_misc_force_store(); }

/***** enums *****/
enum lfds710_misc_cas_strength
{
  // TRD : GCC defined values
  LFDS710_MISC_CAS_STRENGTH_STRONG = 0,
  LFDS710_MISC_CAS_STRENGTH_WEAK   = 1,
};

enum lfds710_misc_validity
{
  LFDS710_MISC_VALIDITY_UNKNOWN,
  LFDS710_MISC_VALIDITY_VALID,
  LFDS710_MISC_VALIDITY_INVALID_LOOP,
  LFDS710_MISC_VALIDITY_INVALID_MISSING_ELEMENTS,
  LFDS710_MISC_VALIDITY_INVALID_ADDITIONAL_ELEMENTS,
  LFDS710_MISC_VALIDITY_INVALID_TEST_DATA,
  LFDS710_MISC_VALIDITY_INVALID_ORDER,
  LFDS710_MISC_VALIDITY_INVALID_ATOMIC_FAILED,
  LFDS710_MISC_VALIDITY_INDETERMINATE_NONATOMIC_PASSED,
};

enum lfds710_misc_flag
{
  LFDS710_MISC_FLAG_LOWERED,
  LFDS710_MISC_FLAG_RAISED
};

enum lfds710_misc_query
{
  LFDS710_MISC_QUERY_GET_BUILD_AND_VERSION_STRING
};

enum lfds710_misc_data_structure
{
  LFDS710_MISC_DATA_STRUCTURE_BTREE_AU,
  LFDS710_MISC_DATA_STRUCTURE_FREELIST,
  LFDS710_MISC_DATA_STRUCTURE_HASH_A,
  LFDS710_MISC_DATA_STRUCTURE_LIST_AOS,
  LFDS710_MISC_DATA_STRUCTURE_LIST_ASU,
  LFDS710_MISC_DATA_STRUCTURE_QUEUE_BMM,
  LFDS710_MISC_DATA_STRUCTURE_QUEUE_BSS,
  LFDS710_MISC_DATA_STRUCTURE_QUEUE_UMM,
  LFDS710_MISC_DATA_STRUCTURE_RINGBUFFER,
  LFDS710_MISC_DATA_STRUCTURE_STACK,
  LFDS710_MISC_DATA_STRUCTURE_COUNT
};

/***** struct *****/
struct lfds710_misc_backoff_state
{
  lfds710_pal_uint_t volatile LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    lock;

  lfds710_pal_uint_t
    backoff_iteration_frequency_counters[2],
    metric,
    total_operations;
};

struct lfds710_misc_globals
{
  struct lfds710_prng_state
    ps;
};

struct lfds710_misc_validation_info
{
  lfds710_pal_uint_t
    min_elements,
    max_elements;
};

/***** externs *****/
extern struct lfds710_misc_globals
  lfds710_misc_globals;

/***** public prototypes *****/
static LFDS710_PAL_INLINE void lfds710_misc_force_store( void );

void lfds710_misc_query( enum lfds710_misc_query query_type, void *query_input, void *query_output );

/***** public in-line functions *****/
// #pragma prefast( disable : 28112, "blah" )

static LFDS710_PAL_INLINE void lfds710_misc_force_store()
{
  lfds710_pal_uint_t volatile LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    destination;

  LFDS710_PAL_ATOMIC_SET( &destination, 0 );

  return;
}

