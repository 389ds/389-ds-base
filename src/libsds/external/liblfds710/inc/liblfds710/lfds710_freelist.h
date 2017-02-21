/***** defines *****/
#define LFDS710_FREELIST_GET_KEY_FROM_ELEMENT( freelist_element )             ( (freelist_element).key )
#define LFDS710_FREELIST_SET_KEY_IN_ELEMENT( freelist_element, new_key )      ( (freelist_element).key = (void *) (lfds710_pal_uint_t) (new_key) )
#define LFDS710_FREELIST_GET_VALUE_FROM_ELEMENT( freelist_element )           ( (freelist_element).value )
#define LFDS710_FREELIST_SET_VALUE_IN_ELEMENT( freelist_element, new_value )  ( (freelist_element).value = (void *) (lfds710_pal_uint_t) (new_value) )
#define LFDS710_FREELIST_GET_USER_STATE_FROM_STATE( freelist_state )          ( (freelist_state).user_state )

#define LFDS710_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS  ( LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES / sizeof(struct lfds710_freelist_element *) )

/***** enums *****/
enum lfds710_freelist_query
{
  LFDS710_FREELIST_QUERY_SINGLETHREADED_GET_COUNT,
  LFDS710_FREELIST_QUERY_SINGLETHREADED_VALIDATE,
  LFDS710_FREELIST_QUERY_GET_ELIMINATION_ARRAY_EXTRA_ELEMENTS_IN_FREELIST_ELEMENTS
};

/***** structures *****/
struct lfds710_freelist_element
{
  struct lfds710_freelist_element
    *next;

  void
    *key,
    *value;
};

struct lfds710_freelist_state
{
  struct lfds710_freelist_element LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    *volatile top[PAC_SIZE];

  lfds710_pal_uint_t LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    elimination_array_size_in_elements;

  struct lfds710_freelist_element * volatile
    (*elimination_array)[LFDS710_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS];

  void
    *user_state;

  struct lfds710_misc_backoff_state
    pop_backoff,
    push_backoff;
};

/***** public prototypes *****/
void lfds710_freelist_init_valid_on_current_logical_core( struct lfds710_freelist_state *fs,
                                                          struct lfds710_freelist_element * volatile (*elimination_array)[LFDS710_FREELIST_ELIMINATION_ARRAY_ELEMENT_SIZE_IN_FREELIST_ELEMENTS],
                                                          lfds710_pal_uint_t elimination_array_size_in_elements,
                                                          void *user_state );
  // TRD : used in conjunction with the #define LFDS710_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE

void lfds710_freelist_cleanup( struct lfds710_freelist_state *fs,
                               void (*element_cleanup_callback)(struct lfds710_freelist_state *fs, struct lfds710_freelist_element *fe) );

void lfds710_freelist_push( struct lfds710_freelist_state *fs,
                                   struct lfds710_freelist_element *fe,
                                   struct lfds710_prng_st_state *psts );

int lfds710_freelist_pop( struct lfds710_freelist_state *fs,
                          struct lfds710_freelist_element **fe,
                          struct lfds710_prng_st_state *psts );

void lfds710_freelist_query( struct lfds710_freelist_state *fs,
                             enum lfds710_freelist_query query_type,
                             void *query_input,
                             void *query_output );

