#ifndef _X_ALLOCATOR_FSA_STRATEGY_PAGE_H_
#define _X_ALLOCATOR_FSA_STRATEGY_PAGE_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_allocator.h"
#include "xvmem/private/x_doubly_linked_list.h"

namespace xcore
{
    struct xpage_t
    {
        // Constraints:
        // - maximum number of elements is (65535-1, 0xFFFF is considered NIL)
        // - minimum size of an element is 4 bytes
        // - maximum page-size is (65535-1) * sizeof-element

        u16 m_free_list;
        u16 m_free_index;
        u16 m_elem_used;
        u16 m_elem_total;
        u16 m_elem_size;

        void init(u32 pool_size, u32 elem_size)
        {
            m_free_list  = xalist_t::NIL;
            m_free_index = 0;
            m_elem_used  = 0;
            m_elem_total = pool_size / elem_size;
            m_elem_size  = elem_size;
        }

        void init() { init(0, 8); }

        bool is_full() const { return m_elem_used == m_elem_total; }
        bool is_empty() const { return m_elem_used == 0; }

        void* allocate(void* const block_base_address);
        void  deallocate(void* const block_base_address, void* const p);

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };


} // namespace xcore

#endif // _X_ALLOCATOR_FSA_STRATEGY_PAGE_H_