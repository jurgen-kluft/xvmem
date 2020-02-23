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

    namespace xcoalescestrat
    {
        struct xinstance_t;

        xinstance_t* create(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
        void         destroy(xinstance_t*);
        void*        allocate(xinstance_t*, u32 size, u32 alignment);
        u32          deallocate(xinstance_t*, void* ptr);
    } // namespace xcoalescestrat

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_