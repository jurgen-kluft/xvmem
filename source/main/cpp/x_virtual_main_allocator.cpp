#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/private/x_strategy_fsablock.h"
#include "xvmem/private/x_strategy_coalesce.h"
#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator : public xalloc
    {
    public:
        void          init(xalloc*);
        virtual void* allocate(u32 size, u32 align);
        virtual void  deallocate(void* ptr);
        virtual void  release();

        xalloc* m_internal_heap;

        // Very small allocator, size < 1 KB
        u32              m_fvsa_min_size;   // 8
        u32              m_fvsa_step_size;  // 8
        u32              m_fvsa_max_size;   // 1 KB
        void*            m_fvsa_mem_base;   // A memory base pointer
        u64              m_fvsa_mem_range;  // 1 GB
        xfsapage_list_t* m_fvsa_pages_list; // 127 allocators

        // Small allocator, 1 KB < size <= 8 KB
        u32              m_fsa_min_size;   // 1 KB
        u32              m_fsa_step_size;  // 64
        u32              m_fsa_max_size;   // 8 KB
        void*            m_fsa_mem_base;   // A memory base pointer
        u64              m_fsa_mem_range;  // 1 GB
        xfsapage_list_t* m_fsa_pages_list; // 112 allocators

        // The page manager for fvsa and fsa
        u32             m_fsa_page_size; // 64 KB
        xfsapage_list_t m_fsa_freepages_list;
        xfsapages_t*    m_fsa_pages;

        // Medium allocator
        u32                      m_med_min_size;  // 8 KB
        u32                      m_med_step_size; // 256 (size alignment)
        u32                      m_med_max_size;  // 640 KB
        void*                    m_med_mem_base;  // A memory base pointer
        u64                      m_med_mem_range; // 768 MB
        xcoalescee::xinstance_t* m_med_allocator;

        // Segregated allocator
        u32                       m_seg_min_size;  // 640 KB
        u32                       m_seg_max_size;  // 32 MB
        void*                     m_seg_mem_base;  // A memory base pointer
        u64                       m_seg_mem_range; // 128 GB
        u64                       m_seg_mem_subrange;
        xsegregated::xinstance_t* m_seg_allocator;

        // Large allocator
        u32     m_large_min_size;  // 32MB
        void*   m_large_mem_base;  // A memory base pointer
        u64     m_large_mem_range; //
        xalloc* m_large_allocator;
    };

    void* xvmem_allocator::allocate(u32 size, u32 align)
    {
        if (size <= m_fvsa_max_size)
        {
            if (size < m_fvsa_min_size)
                size = m_fvsa_min_size;

            // TODO:
            // Determine alignment request, if large than size and reasonable then
            // set the size to the alignment request, otherwise let the allocation go
            u32 const        alloc_size  = (size + (m_fvsa_step_size - 1)) & ~(m_fvsa_step_size - 1);
            s32 const        alloc_index = (size - m_fvsa_min_size) / m_fvsa_step_size;
            xfsapage_list_t& page_list   = m_fvsa_pages_list[alloc_index];
            return alloc_elem(m_fsa_pages, page_list, alloc_size);
        }
        else if (size <= m_fsa_max_size)
        {
            if (size < m_fsa_min_size)
                size = m_fsa_min_size;

            // TODO:
            // Determine alignment request, if large than size and reasonable then
            // set the size to the alignment request, otherwise let the allocation go
            u32 const        alloc_size  = (size + (m_fsa_step_size - 1)) & ~(m_fsa_step_size - 1);
            s32 const        alloc_index = (size - m_fsa_min_size) / m_fsa_step_size;
            xfsapage_list_t& page_list   = m_fsa_pages_list[alloc_index];
            return alloc_elem(m_fsa_pages, page_list, alloc_size);
        }
        else if (size <= m_med_max_size)
        {
            if (size < m_med_min_size)
                size = m_med_min_size;
            return m_med_allocator->allocate(size, align);
        }
        else
        {
            return m_large_allocator->allocate(size, align);
        }
    }

    // Helper function to determine if a pointer belong to a certain memory range
    static inline bool helper_is_in_memrange(void* const base, u64 const range, void* const ptr)
    {
        xbyte* const begin = (xbyte*)base;
        xbyte* const end   = begin + range;
        xbyte* const p     = (xbyte*)ptr;
        return p >= begin && p < end;
    }

    void xvmem_allocator::deallocate(void* ptr)
    {
        if (helper_is_in_memrange(m_fvsa_mem_base, m_fvsa_mem_range, ptr))
        {
            s32 const        page_index  = (s32)(((u64)ptr - (u64)m_fvsa_mem_base) / m_fsa_page_size);
            u32 const        alloc_size  = sizeof_elem(m_fsa_pages, ptr);
            u32 const        alloc_index = (alloc_size - m_fvsa_min_size) / m_fvsa_step_size;
            xfsapage_list_t& page_list   = m_fvsa_pages_list[alloc_index];
            free_elem(m_fsa_pages, page_list, ptr, m_fsa_freepages_list);

            // TODO: Limit the number of free pages
        }
        else if (helper_is_in_memrange(m_fsa_mem_base, m_fsa_mem_range, ptr))
        {
            s32 const        page_index  = (s32)(((u64)ptr - (u64)m_fsa_mem_base) / m_fsa_page_size);
            u32 const        alloc_size  = sizeof_elem(m_fsa_pages, ptr);
            u32 const        alloc_index = (alloc_size - m_fsa_min_size) / m_fsa_step_size;
            xfsapage_list_t& page_list   = m_fsa_pages_list[alloc_index];
            free_elem(m_fsa_pages, page_list, ptr, m_fsa_freepages_list);

            // TODO: Limit the number of free pages
        }
        else if (helper_is_in_memrange(m_med_mem_base, m_med_mem_range, ptr))
        {
            m_med_allocator->deallocate(ptr);
        }
        else if (helper_is_in_memrange(m_large_mem_base, m_large_mem_range, ptr))
        {
            m_large_allocator->deallocate(ptr);
        }
        else
        {
            ASSERTS(false, "error: deallocating an address that is not owned by this allocator!");
        }
    }

    void xvmem_allocator::init(xalloc* internal_allocator)
    {
        u32 const page_size    = 64 * 1024;
        u64 const memory_range = (u64)256 * 1024 * 1024;
        m_fsa_pages            = create(internal_allocator, memory_range, page_size);

        // TODO: Reserve virtual memory for the fsa pages
        m_fvsa_mem_range    = (u64)256 * 1024 * 1024;
        void* fvsa_mem_base = nullptr;
        m_fvsa_mem_base     = fvsa_mem_base; // A memory base pointer

        // FVSA
        m_fvsa_min_size           = 8;
        m_fvsa_step_size          = 8;
        m_fvsa_max_size           = 1024;
        u32 const fvsa_size_slots = (m_fvsa_max_size - m_fvsa_min_size) / m_fvsa_step_size;
        m_fvsa_pages_list         = (xfsapage_list_t*)internal_allocator->allocate(sizeof(xfsapage_list_t) * fvsa_size_slots, sizeof(void*));

        // FSA
        m_fsa_mem_range    = (u64)256 * 1024 * 1024;
        void* fsa_mem_base = nullptr;
        m_fsa_mem_base     = fsa_mem_base; // A memory base pointer

        m_fsa_min_size           = 1024;
        m_fsa_step_size          = 64;
        m_fsa_max_size           = 8192;
        u32 const fsa_size_slots = (m_fsa_max_size - m_fsa_min_size) / m_fsa_step_size;
        m_fsa_pages_list         = (xfsapage_list_t*)internal_allocator->allocate(sizeof(xfsapage_list_t) * fvsa_size_slots, sizeof(void*));

        // TODO: Create node heap for nodes of size 16 and 32
        xfsadexed* node_heap_16 = nullptr;
        xfsadexed* node_heap_32 = nullptr;

        // TODO: Reserve physical memory for the fsa pages
        m_med_mem_range = (u64)768 * 1024 * 1024;
        m_med_mem_base  = nullptr;
        m_med_min_size  = 8192;
        m_med_step_size = 256;
        m_med_max_size  = 640 * 1024;

        u32 const sizeof_coalesce_strategy = xcoalescee::size_of(m_med_min_size, m_med_max_size, m_med_step_size);
        m_med_allocator                    = xcoalescee::create(internal_allocator, node_heap_32, m_med_mem_base, m_med_mem_range, m_med_min_size, m_med_max_size, m_med_step_size);

        // TODO: Reserve virtual memory for the segregated allocator
        m_seg_min_size     = (u32)640 * 1024;       // 640 KB
        m_seg_max_size     = (u32)32 * 1024 * 1024; // 32 MB
        m_seg_mem_base     = nullptr;
        m_seg_mem_range    = (u64)128 * 1024 * 1024 * 1024; // 128 GB
        m_seg_mem_subrange = (u64)1 * 1024 * 1024 * 1024;   // 1 GB
        m_seg_allocator    = xsegregated::create(internal_allocator, node_heap_32, m_seg_mem_base, m_seg_mem_range, m_seg_mem_subrange, m_seg_min_size, m_seg_max_size, m_seg_step_size, page_size);
    }

    xalloc* gCreateVmAllocator(xalloc* internal_allocator)
    {
        xvmem_allocator* main_allocator = internal_allocator->construct<xvmem_allocator>();
        main_allocator->init(internal_allocator);
        return main_allocator;
    }
}; // namespace xcore
