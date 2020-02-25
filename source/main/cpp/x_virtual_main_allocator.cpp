#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_main_allocator.h"
#include "xvmem/private/x_strategy_fsablock.h"
#include "xvmem/private/x_strategy_coalesce.h"
#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/private/x_strategy_large.h"
#include "xvmem/x_virtual_allocator_small.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator : public xalloc
    {
    public:
        void          init(xalloc* heap_allocator, xvmem* vmem);

        virtual void* v_allocate(u32 size, u32 align);
        virtual void  v_deallocate(void* ptr);
        virtual void  v_release();

		XCORE_CLASS_PLACEMENT_NEW_DELETE

        xalloc*                        m_internal_heap;
        u32                            m_fvsa_min_size;  // 8
        u32                            m_fvsa_step_size; // 8
        u32                            m_fvsa_max_size;  // 1 KB
        void*                          m_fvsa_mem_base;  // A memory base pointer
        u64                            m_fvsa_mem_range; // 1 GB
        u32                            m_fvsa_pages_list_size;
        xfsastrat::xlist_t*               m_fvsa_pages_list; // 127 allocators
        u32                            m_fsa_min_size;    // 1 KB
        u32                            m_fsa_step_size;   // 64
        u32                            m_fsa_max_size;    // 8 KB
        u32                            m_fsa_pages_list_size;
        xfsastrat::xlist_t*               m_fsa_pages_list; // 112 allocators
        u32                            m_fsa_page_size;  // 64 KB
        xfsastrat::xlist_t                m_fsa_freepages_list;
        xfsastrat::xpages_t*                   m_fsa_pages;
        u32                            m_med_min_size;   // 8 KB
        u32                            m_med_step_size;  // 256 (size alignment)
        u32                            m_med_max_size;   // 640 KB
        void*                          m_med_mem_base;   // A memory base pointer
        u64                            m_med_mem_range;  // 768 MB
        xfsadexed*                     m_node_allocator; // 32B node allocator
        xcoalescestrat::xinstance_t*   m_med_allocator;
        u32                            m_seg_min_size;  // 640 KB
        u32                            m_seg_max_size;  // 32 MB
        u32                            m_seg_step_size; // 1 MB
        void*                          m_seg_mem_base;  // A memory base pointer
        u64                            m_seg_mem_range; // 128 GB
        u64                            m_seg_mem_subrange;
        xsegregatedstrat::xinstance_t* m_seg_allocator;
        u32                            m_large_min_size;  // 32MB
        void*                          m_large_mem_base;  // A memory base pointer
        u64                            m_large_mem_range; //
        xlargestrat::xinstance_t*      m_large_allocator;
    };

    void* xvmem_allocator::v_allocate(u32 size, u32 align)
    {
        // If the size request is less than the alignment request we will adjust
        // size to at least be the same size as the alignment request.
        if (align > size)
            size = align;

        if (size <= m_fvsa_max_size)
        {
            if (size < m_fvsa_min_size)
                size = m_fvsa_min_size;
            u32 const        alloc_size  = (size + (m_fvsa_step_size - 1)) & ~(m_fvsa_step_size - 1);
            s32 const        alloc_index = (alloc_size - m_fvsa_min_size) / m_fvsa_step_size;
            xfsastrat::xlist_t& page_list   = m_fvsa_pages_list[alloc_index];
            return alloc_elem(m_fsa_pages, page_list, alloc_size);
        }

        if (size <= m_fsa_max_size)
        {
            if (size < m_fsa_min_size)
                size = m_fsa_min_size;
            u32 const        alloc_size  = (size + (m_fsa_step_size - 1)) & ~(m_fsa_step_size - 1);
            s32 const        alloc_index = (alloc_size - m_fsa_min_size) / m_fsa_step_size;
            xfsastrat::xlist_t& page_list   = m_fsa_pages_list[alloc_index];
            return alloc_elem(m_fsa_pages, page_list, alloc_size);
        }

        if (size <= m_med_max_size)
        {
            if (size < m_med_min_size)
                size = m_med_min_size;
            return xcoalescestrat::allocate(m_med_allocator, size, align);
        }

        return xlargestrat::allocate(m_large_allocator, size, align);
    }

    // Helper function to determine if a pointer is inside a certain memory range
    static inline bool helper_is_in_memrange(void* const base, u64 const range, void* const ptr)
    {
        xbyte const* const begin = (xbyte const*)base;
        xbyte const* const end   = begin + range;
        xbyte const* const p     = (xbyte const*)ptr;
        return p >= begin && p < end;
    }

    void xvmem_allocator::v_deallocate(void* ptr)
    {
        if (helper_is_in_memrange(m_fvsa_mem_base, m_fvsa_mem_range, ptr))
        {
            u32 const alloc_size = sizeof_elem(m_fsa_pages, ptr);
            if (alloc_size >= m_fvsa_min_size && alloc_size <= m_fvsa_max_size)
            {
                u32 const        alloc_index = (alloc_size - m_fvsa_min_size) / m_fvsa_step_size;
                xfsastrat::xlist_t& page_list   = m_fvsa_pages_list[alloc_index];
                free_elem(m_fsa_pages, page_list, ptr, m_fsa_freepages_list);
            }
            else if (alloc_size >= m_fsa_min_size && alloc_size <= m_fsa_max_size)
            {
                u32 const        alloc_index = (alloc_size - m_fsa_min_size) / m_fsa_step_size;
                xfsastrat::xlist_t& page_list   = m_fsa_pages_list[alloc_index];
                free_elem(m_fsa_pages, page_list, ptr, m_fsa_freepages_list);
            }
            else
            {
                ASSERT(alloc_size >= m_fvsa_min_size && alloc_size <= m_fsa_max_size);
            }

            // TODO: Limit the number of free pages
        }
        else if (helper_is_in_memrange(m_med_mem_base, m_med_mem_range, ptr))
        {
            xcoalescestrat::deallocate(m_med_allocator, ptr);
        }
        else if (helper_is_in_memrange(m_large_mem_base, m_large_mem_range, ptr))
        {
            xlargestrat::deallocate(m_large_allocator, ptr);
        }
        else
        {
            ASSERTS(false, "error: deallocating an address that is not owned by this allocator!");
        }
    }

    void xvmem_allocator::v_release()
    {
        for (u32 i = 0; i < m_fvsa_pages_list_size; i++)
        {
            free_all_pages(m_fsa_pages, m_fvsa_pages_list[i]);
        }
        m_internal_heap->deallocate(m_fvsa_pages_list);
        for (u32 i = 0; i < m_fsa_pages_list_size; i++)
        {
            free_all_pages(m_fsa_pages, m_fsa_pages_list[i]);
        }
        m_internal_heap->deallocate(m_fsa_pages_list);

        xcoalescestrat::destroy(m_med_allocator);
        xsegregatedstrat::destroy(m_seg_allocator);
        xlargestrat::destroy(m_large_allocator);

        // TODO: release all reserved virtual memory

        m_node_allocator->release();
        xfsastrat::destroy(m_fsa_pages);
    }

    void xvmem_allocator::init(xalloc* internal_allocator, xvmem* vmem)
    {
        u32 const page_size = 64 * 1024;

        m_internal_heap = internal_allocator;

        m_fvsa_mem_range         = (u64)512 * 1024 * 1024;
        m_fvsa_mem_base          = nullptr; // A memory base pointer
        u32       fvsa_page_size = 0;
        u32 const fvsa_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_fvsa_mem_range, fvsa_page_size, fvsa_mem_attrs, m_fvsa_mem_base);
        m_fsa_pages = xfsastrat::create(internal_allocator, m_fvsa_mem_base, m_fvsa_mem_range, fvsa_page_size);

        // FVSA, every size has it's own 'used pages' list
        m_fvsa_min_size        = 8;
        m_fvsa_step_size       = 8;
        m_fvsa_max_size        = 1024;
        m_fvsa_pages_list_size = (m_fvsa_max_size - m_fvsa_min_size) / m_fvsa_step_size;
        m_fvsa_pages_list      = (xfsastrat::xlist_t*)internal_allocator->allocate(sizeof(xfsastrat::xlist_t) * m_fvsa_pages_list_size, sizeof(void*));

        // FSA, also every size has it's own 'used pages' list
        m_fsa_min_size           = 1024;
        m_fsa_step_size          = 64;
        m_fsa_max_size           = 8192;
        m_fsa_pages_list_size    = (m_fsa_max_size - m_fsa_min_size) / m_fsa_step_size;
        m_fsa_pages_list         = (xfsastrat::xlist_t*)internal_allocator->allocate(sizeof(xfsastrat::xlist_t) * m_fsa_pages_list_size, sizeof(void*));

        // Create node heap for nodes with size 32B
        // We prefer to create a separate virtual memory based FSA allocator.
        m_node_allocator = gCreateVMemBasedDexedFsa(internal_allocator, m_fsa_pages, 32);

        m_med_mem_range = (u64)768 * 1024 * 1024;
        m_med_mem_base  = nullptr;
        m_med_min_size  = 8192;
        m_med_step_size = 256;
        m_med_max_size  = 640 * 1024;

        // Reserve physical memory for the medium size allocator
        u32       med_page_size = 0;
        u32 const med_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_med_mem_range, med_page_size, med_mem_attrs, m_med_mem_base);
        vmem->commit(m_med_mem_base, med_page_size, (u32)(m_med_mem_range / med_page_size));

        m_med_allocator    = xcoalescestrat::create(internal_allocator, m_node_allocator, m_med_mem_base, m_med_mem_range, m_med_min_size, m_med_max_size, m_med_step_size);
        m_seg_min_size     = (u32)640 * 1024;       // 640 KB
        m_seg_max_size     = (u32)32 * 1024 * 1024; // 32 MB
        m_seg_mem_base     = nullptr;
        m_seg_mem_range    = (u64)128 * 1024 * 1024 * 1024; // 128 GB
        m_seg_mem_subrange = (u64)1 * 1024 * 1024 * 1024;   // 1 GB

        // Reserve virtual memory for the segregated allocator
        u32       seg_page_size = 0;
        u32 const seg_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_seg_mem_range, seg_page_size, seg_mem_attrs, m_seg_mem_base);
        m_seg_allocator = xsegregatedstrat::create(internal_allocator, m_node_allocator, m_seg_mem_base, m_seg_mem_range, m_seg_mem_subrange, m_seg_min_size, m_seg_max_size, m_seg_step_size, page_size);

        m_large_min_size          = (u32)32 * 1024 * 1024;         // 32 MB
        m_large_mem_base          = nullptr;                       // A memory base pointer
        m_large_mem_range         = (u64)128 * 1024 * 1024 * 1024; // 128 GB
        u32       large_page_size = 0;
        u32 const large_mem_attrs = 0; // Page/Memory attributes
        vmem->reserve(m_large_mem_range, large_page_size, large_mem_attrs, m_large_mem_base);

        const u32 max_num_large_allocations = 64;
        m_large_allocator                   = xlargestrat::create(internal_allocator, m_large_mem_base, m_large_mem_range, m_large_min_size, max_num_large_allocations);
    }

    xalloc* gCreateVmAllocator(xalloc* internal_allocator, xvmem* vmem)
    {
        xvmem_allocator* main_allocator = internal_allocator->construct<xvmem_allocator>();
        main_allocator->init(internal_allocator, vmem);
        return main_allocator;
    }
}; // namespace xcore
