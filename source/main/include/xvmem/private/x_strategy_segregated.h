#ifndef _X_ALLOCATOR_SEGREGATED_H_
#define _X_ALLOCATOR_SEGREGATED_H_
#include "xbase/x_target.h"
#ifdef USE_PRAGMA_ONCE
#pragma once
#endif

#include "xbase/x_debug.h"

namespace xcore
{
    class xalloc;
    class xfsadexed;

    namespace xsegregated
    {
        struct xspace_t;
        struct xspaces_t;
        struct xlevel_t;

        struct xspaces_t
        {
            void*        m_mem_address;
            u64          m_space_size;  // The size of a range (e.g. 1 GiB)
            u32          m_space_count; // The full address range is divided into 'n' spaces
            u32          m_page_size;
            u16          m_freelist; // The doubly linked list spaces that are 'empty'
            xspace_t*    m_spaces;   // The array of memory spaces that make up the full address range

            void init(xalloc* main_heap, void* mem_address, u64 mem_range, u64 space_size, u32 page_size);
            void release(xalloc* main_heap);

            bool         is_space_empty(void* addr) const;
            bool         is_space_full(void* addr) const;
            inline void* get_base_address() const { return m_mem_address; }
            inline u32   get_page_size() const { return m_page_size; }
            inline u64   get_space_size() const { return m_space_size; }
            void*        get_space_addr(void* addr) const;

            inline u16 ptr2addr(void* p) const
            {
                u16 const idx = (u16)(((u64)p - (u64)get_space_addr(p)) / get_page_size());
                ASSERT(idx < m_space_count);
                return idx;
            }

            void      insert_space_into_list(u16& head, u16 item);
            xspace_t* remove_space_from_list(u16& head, u16 item);
            bool      obtain_space(void*& addr, u16& ispace, xspace_t*& pspace);
            void      release_space(void*& addr);
            void      release_space(u16 ispace, xspace_t* pspace);
            u16       ptr2level(void* ptr) const;
            u16       ptr2space(void* ptr) const;
            void      register_alloc(void* addr);
            u16       register_dealloc(void* addr);
        };


        struct xlevels_t
        {
            void initialize(xalloc* main_alloc, xfsadexed* node_alloc, void* vmem_address, u64 vmem_space, u64 space_size, u32 allocsize_min, u32 allocsize_max, u32 allocsize_step, u32 page_size);
            void release();

            void* allocate(u32 size, u32 alignment);
            void deallocate(void* ptr);

            xalloc*    m_main_alloc;
            xfsadexed* m_node_alloc;
            void*      m_vmem_base_addr;
            u32        m_allocsize_min;
            u32        m_allocsize_max;
            u32        m_allocsize_step;
            u32        m_pagesize;
            u32        m_level_cnt;
            xlevel_t*  m_levels;
            xspaces_t  m_spaces;
        };

    } // namespace xsegregated
} // namespace xcore

#endif // _X_ALLOCATOR_SEGREGATED_H_