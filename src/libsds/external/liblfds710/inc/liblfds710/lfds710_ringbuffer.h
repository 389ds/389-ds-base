/***** enums *****/
#define LFDS710_RINGBUFFER_GET_USER_STATE_FROM_STATE( ringbuffer_state )  ( (ringbuffer_state).user_state )

/***** enums *****/
enum lfds710_ringbuffer_query
{
  LFDS710_RINGBUFFER_QUERY_SINGLETHREADED_GET_COUNT,
  LFDS710_RINGBUFFER_QUERY_SINGLETHREADED_VALIDATE
};

/***** structures *****/
struct lfds710_ringbuffer_element
{
  struct lfds710_freelist_element
    fe;

  struct lfds710_queue_umm_element
    qumme;

  struct lfds710_queue_umm_element
    *qumme_use; // TRD : hack; we need a new queue with no dummy element

  void
    *key,
    *value;
};

struct lfds710_ringbuffer_state
{
  struct lfds710_freelist_state
    fs;

  struct lfds710_queue_umm_state
    qumms;

  void
    (*element_cleanup_callback)( struct lfds710_ringbuffer_state *rs, void *key, void *value, enum lfds710_misc_flag unread_flag ),
    *user_state;
};

/***** public prototypes *****/
void lfds710_ringbuffer_init_valid_on_current_logical_core( struct lfds710_ringbuffer_state *rs,
                                                            struct lfds710_ringbuffer_element *re_array_inc_dummy,
                                                            lfds710_pal_uint_t number_elements_inc_dummy,
                                                            void *user_state );
  // TRD : used in conjunction with the #define LFDS710_MISC_MAKE_VALID_ON_CURRENT_LOGICAL_CORE_INITS_COMPLETED_BEFORE_NOW_ON_ANY_OTHER_LOGICAL_CORE

void lfds710_ringbuffer_cleanup( struct lfds710_ringbuffer_state *rs,
                                 void (*element_cleanup_callback)(struct lfds710_ringbuffer_state *rs, void *key, void *value, enum lfds710_misc_flag unread_flag) );

int lfds710_ringbuffer_read( struct lfds710_ringbuffer_state *rs,
                             void **key,
                             void **value );

void lfds710_ringbuffer_write( struct lfds710_ringbuffer_state *rs,
                               void *key,
                               void *value,
                               enum lfds710_misc_flag *overwrite_occurred_flag,
                               void **overwritten_key,
                               void **overwritten_value );

void lfds710_ringbuffer_query( struct lfds710_ringbuffer_state *rs,
                               enum lfds710_ringbuffer_query query_type,
                               void *query_input,
                               void *query_output );

