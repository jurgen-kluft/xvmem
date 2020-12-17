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

	alloc_t* create_alloc_coalesce_direct(alloc_t* main_heap, fsadexed_t* node_heap, void* mem_base, u32 mem_range, u32 min_size, u32 max_size, u32 step_size, u32 addr_cnt);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_