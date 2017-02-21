/***** defines *****/
#define LFDS710_STACK_GET_KEY_FROM_ELEMENT( stack_element )             ( (stack_element).key )
#define LFDS710_STACK_SET_KEY_IN_ELEMENT( stack_element, new_key )      ( (stack_element).key = (void *) (lfds710_pal_uint_t) (new_key) )
#define LFDS710_STACK_GET_VALUE_FROM_ELEMENT( stack_element )           ( (stack_element).value )
#define LFDS710_STACK_SET_VALUE_IN_ELEMENT( stack_element, new_value )  ( (stack_element).value = (void *) (lfds710_pal_uint_t) (new_value) )
#define LFDS710_STACK_GET_USER_STATE_FROM_STATE( stack_state )          ( (stack_state).user_state )

/***** enums *****/
enum lfds710_stack_query
{
  LFDS710_STACK_QUERY_SINGLETHREADED_GET_COUNT,
  LFDS710_STACK_QUERY_SINGLETHREADED_VALIDATE
};

/***** structures *****/
struct lfds710_stack_element
{
  struct lfds710_stack_element
    *next;

  void
    *key,
    *value;
};

struct lfds710_stack_state
{
  struct lfds710_stack_element LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    *volatile top[PAC_SIZE];

  void LFDS710_PAL_ALIGN(LFDS710_PAL_ATOMIC_ISOLATION_IN_BYTES)
    *user_state;

  struct lfds710_misc_backoff_state
    pop_backoff,
    push_backoff;
};

/***** public prototypes *****/
void lfds710_stack_init_valid_on_current_logical_core( struct lfds710_stack_state *ss,
                                                       void *user_state );
  // TRD : used in conjunction with the #define LFDS710_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE

void lfds710_stack_cleanup( struct lfds710_stack_state *ss,
                            void (*element_cleanup_callback)(struct lfds710_stack_state *ss, struct lfds710_stack_element *se) );

void lfds710_stack_push( struct lfds710_stack_state *ss,
                         struct lfds710_stack_element *se );

int lfds710_stack_pop( struct lfds710_stack_state *ss,
                       struct lfds710_stack_element **se );

void lfds710_stack_query( struct lfds710_stack_state *ss,
                          enum lfds710_stack_query query_type,
                          void *query_input,
                          void *query_output );


