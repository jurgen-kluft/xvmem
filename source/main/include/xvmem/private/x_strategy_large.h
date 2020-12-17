#ifndef _X_ALLOCATOR_STRATEGY_LARGE_H_
#define _X_ALLOCATOR_STRATEGY_LARGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;

    // e.g. If you want your largest allocation to be 512 MB and
    // you want to limit the number of allocations to 8 you need
    // to give it an address range of 8*512MB=4GB.

    alloc_t* create_alloc_large(alloc_t* main_heap, void* mem_addr, u64 mem_size, u32 max_num_allocs);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_LARGE_H_