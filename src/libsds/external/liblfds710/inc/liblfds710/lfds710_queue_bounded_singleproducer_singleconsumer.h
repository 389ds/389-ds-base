/***** defines *****/
#define LFDS710_QUEUE_BSS_GET_USER_STATE_FROM_STATE( queue_bss_state )  ( (queue_bss_state).user_state )

/***** enums *****/
enum lfds710_queue_bss_query
{
  LFDS710_QUEUE_BSS_QUERY_GET_POTENTIALLY_INACCURATE_COUNT,
  LFDS710_QUEUE_BSS_QUERY_VALIDATE
};

/***** structures *****/
struct lfds710_queue_bss_element
{
  void
    *volatile key,
    *volatile value;
};

struct lfds710_queue_bss_state
{
  lfds710_pal_uint_t
    number_elements,
    mask;

  lfds710_pal_uint_t volatile
    read_index,
    write_index;

  struct lfds710_queue_bss_element
    *element_array;

  void
    *user_state;
};

/***** public prototypes *****/
void lfds710_queue_bss_init_valid_on_current_logical_core( struct lfds710_queue_bss_state *qbsss, 
                                                           struct lfds710_queue_bss_element *element_array,
                                                           lfds710_pal_uint_t number_elements,
                                                           void *user_state );
  // TRD : number_elements must be a positive integer power of 2
  // TRD : used in conjunction with the #define LFDS710_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE

void lfds710_queue_bss_cleanup( struct lfds710_queue_bss_state *qbsss,
                                void (*element_cleanup_callback)(struct lfds710_queue_bss_state *qbsss, void *key, void *value) );

int lfds710_queue_bss_enqueue( struct lfds710_queue_bss_state *qbsss,
                               void *key,
                               void *value );

int lfds710_queue_bss_dequeue( struct lfds710_queue_bss_state *qbsss,
                               void **key,
                               void **value );

void lfds710_queue_bss_query( struct lfds710_queue_bss_state *qbsss,
                              enum lfds710_queue_bss_query query_type,
                              void *query_input,
                              void *query_output );

