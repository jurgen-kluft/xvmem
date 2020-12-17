#ifndef _X_ALLOCATOR_STRATEGY_COALESCE_H_
#define _X_ALLOCATOR_STRATEGY_COALESCE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;
    class fsadexed_t;

    alloc_t* create_alloc_coalesce(alloc_t* main_heap, fsadexed_t* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_