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

    namespace xcoalescestrat_direct
    {
        struct xinstance_t;

        // Min-Size / Max-Size / Step-Size / Region-Size
		// Node-Heap as a indexed fixed-size allocator needs to allocate elements with a size-of 16 bytes
        xinstance_t* create_4KB_64KB_256B_32MB(xalloc* main_heap, xfsadexed* node_heap);
        xinstance_t* create_64KB_512KB_2KB_64MB(xalloc* main_heap, xfsadexed* node_heap);

        // So a high-level allocator could do something like this
		// Where every allocator is either active or non-active
		// If an allocator is active than memory has been committed otherwise it is only reserved
        struct xcoalesce_allocator
        {
            void*        m_s2m_mem_base;      // The memory base address, reserved
            u64          m_s2m_mem_range;     // 32 MB * 8 = 256 MB
            xinstance_t* m_s2m_allocators[8]; // Small/Medium
            void*        m_m2l_mem_base;      // The memory base address, reserved
            u64          m_m2l_mem_range;     // 64 MB * 4 = 256 MB
            xinstance_t* m_m2l_allocators[4]; // Medium/Large
        };

        bool  is_empty(xinstance_t*);
        void  destroy(xinstance_t*, xalloc*);
        void* allocate(xinstance_t*, u32 size, u32 alignment);
        u32   deallocate(xinstance_t*, void* ptr);
    } // namespace xcoalescestrat_direct

} // namespace xcore

#endif // _X_ALLOCATOR_STRATEGY_COALESCE_H_