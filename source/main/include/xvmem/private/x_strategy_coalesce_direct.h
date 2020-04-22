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

	xalloc* create_alloc_coalesce_direct(xalloc* main_heap, xfsadexed* node_heap, void* mem_base, u64 mem_range, u32 min_size, u32 max_size, u32 step_size);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_