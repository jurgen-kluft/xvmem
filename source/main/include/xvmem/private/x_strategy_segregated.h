#ifndef _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#define _X_ALLOCATOR_STRATEGY_SEGREGATED_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;
    class xfsadexed;

    namespace xsegregatedstrat
    {
        struct xinstance_t;

        xinstance_t* create(xalloc* main_alloc, void* mem_address, u64 mem_space, u32 allocsize_min, u32 allocsize_max, u32 allocsize_align);
        void         destroy(xinstance_t*);
        void*        allocate(xinstance_t*, u32 size, u32 alignment);
        u32          deallocate(xinstance_t*, void* ptr);
    } // namespace xsegregatedstrat
} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_SEGREGATED_H_