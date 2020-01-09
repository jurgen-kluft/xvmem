#ifndef _X_ALLOCATOR_COALESCE_H_
#define _X_ALLOCATOR_COALESCE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"
#include "xbase/x_hibitset.h"

namespace xcore
{
    class xcoalescee
    {
    public:
        xcoalescee();

        void  initialize(xalloc* main_heap, xfsadexed* node_heap, void* mem_addr, u64 mem_size, u32 size_min, u32 size_max, u32 size_step);
        void  release();
        void* allocate(u32 size, u32 alignment);
        u32   deallocate(void* p);

        xalloc*    m_main_heap;
        xfsadexed* m_node_heap;
        void*      m_memory_addr;
        u64        m_memory_size;
        u32        m_alloc_size_min;
        u32        m_alloc_size_max;
        u32        m_alloc_size_step;
        u32        m_size_db_cnt;
        u32*       m_size_db;
        xhibitset  m_size_db_occupancy;
        u32        m_addr_db;
    };
} // namespace xcore

#endif // _X_ALLOCATOR_COALESCE_H_