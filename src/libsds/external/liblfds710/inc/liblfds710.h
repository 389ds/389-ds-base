#ifndef LIBLFDS710_H

  /***** defines *****/
  #define LIBLFDS710_H

  /***** pragmas on *****/
  // #pragma warning( push )
  // #pragma warning( disable : 4324 )                                          // TRD : 4324 disables MSVC warnings for structure alignment padding due to alignment specifiers
  // #pragma prefast( disable : 28113 28182 28183, "blah" )

  /***** includes *****/
  #include "liblfds710/lfds710_porting_abstraction_layer_compiler.h"
  #include "liblfds710/lfds710_porting_abstraction_layer_operating_system.h"
  #include "liblfds710/lfds710_porting_abstraction_layer_processor.h"

  #include "liblfds710/lfds710_prng.h"                                       // TRD : misc requires prng
  #include "liblfds710/lfds710_misc.h"                                       // TRD : everything after depends on misc
  // #include "liblfds710/lfds710_btree_addonly_unbalanced.h"                   // TRD : hash_addonly depends on btree_addonly_unbalanced
  // #include "liblfds710/lfds710_freelist.h"
  // #include "liblfds710/lfds710_hash_addonly.h"
  // #include "liblfds710/lfds710_list_addonly_singlylinked_ordered.h"
  // #include "liblfds710/lfds710_list_addonly_singlylinked_unordered.h"
  // #include "liblfds710/lfds710_queue_bounded_manyproducer_manyconsumer.h"
  // #include "liblfds710/lfds710_queue_bounded_singleproducer_singleconsumer.h"
  #include "liblfds710/lfds710_queue_unbounded_manyproducer_manyconsumer.h"
  // #include "liblfds710/lfds710_ringbuffer.h"
  // #include "liblfds710/lfds710_stack.h"

  /***** pragmas off *****/
  // #pragma warning( pop )

#endif

