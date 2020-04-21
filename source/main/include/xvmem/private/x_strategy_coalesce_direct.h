#ifndef _X_ALLOCATOR_STRATEGY_COALESCE_H_
#define _X_ALLOCATOR_STRATEGY_COALESCE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;
    class xfsadexed;

    // Min-Size / Max-Size / Step-Size / Region-Size
    // Node-Heap as a indexed fixed-size allocator needs to allocate elements with a size-of 16 bytes
    xalloc* create_alloc_coalesce_direct_4KB_64KB_256B_32MB(xalloc* main_heap, xfsadexed* node_heap);
    xalloc* create_alloc_coalesce_direct_64KB_512KB_2KB_64MB(xalloc* main_heap, xfsadexed* node_heap);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_