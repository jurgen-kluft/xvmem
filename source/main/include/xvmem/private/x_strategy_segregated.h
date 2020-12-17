#ifndef _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#define _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class alloc_t;

    alloc_t* create_alloc_segregated(alloc_t* main_alloc, fsa_t* node_heap, void* mem_address, u64 mem_space, u32 allocsize_min, u32 allocsize_max, u32 allocsize_align);

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_SEGREGATED_H_