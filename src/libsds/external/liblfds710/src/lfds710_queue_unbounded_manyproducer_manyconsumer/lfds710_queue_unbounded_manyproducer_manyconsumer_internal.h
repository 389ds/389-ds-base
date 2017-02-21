/***** the library wide include file *****/
#include "../liblfds710_internal.h"

/***** enums *****/
enum lfds710_queue_umm_queue_state
{
  LFDS710_QUEUE_UMM_QUEUE_STATE_UNKNOWN, 
  LFDS710_QUEUE_UMM_QUEUE_STATE_EMPTY,
  LFDS710_QUEUE_UMM_QUEUE_STATE_ENQUEUE_OUT_OF_PLACE,
  LFDS710_QUEUE_UMM_QUEUE_STATE_ATTEMPT_DEQUEUE
};

/***** private prototypes *****/

