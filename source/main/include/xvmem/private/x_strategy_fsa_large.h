#ifndef _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#define _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

namespace xcore
{
    class xalloc;

    namespace xfsa_large
    {
        struct xinstance_t;

        xinstance_t* create(xalloc* main_alloc, void* mem_address, u64 mem_range, u32 pagesize, u32 allocsize);
        void         destroy(xinstance_t*);
        void*        allocate(xinstance_t*, u32 size, u32 alignment);
        u32          deallocate(xinstance_t*, void* ptr);
    } // namespace xfsa_large
} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_FSA_LARGE_H_