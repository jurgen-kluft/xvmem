#ifndef _X_ALLOCATOR_LARGE_STRATEGY_H_
#define _X_ALLOCATOR_LARGE_STRATEGY_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;

    namespace xlarge
    {
        struct xinstance_t;

        xinstance_t* create(xalloc* main_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 max_num_allocs);
        void         destroy(xinstance_t*);
        void*        allocate(xinstance_t*, u32 size, u32 alignment);
        u64          deallocate(xinstance_t*, void* ptr);
    } // namespace xlarge

} // namespace xcore

#endif // _X_ALLOCATOR_LARGE_STRATEGY_H_