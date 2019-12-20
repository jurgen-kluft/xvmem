#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 align);
        virtual void  deallocate(void* ptr);

    protected:
        // Very small allocator
        u32        m_fvsa_min_size;  // 8
        u32        m_fvsa_step_size; // 8 (gives us 127 allocators)
        u32        m_fvsa_max_size;  // 1024
        void*      m_fvsa_mem_base;  // A memory base pointer
        u64        m_fvsa_mem_range; // 256 MB
        u32        m_fvsa_page_size; // 4 KB (gives us 64 KB pages)
        xfsa**     m_fvsa_alloc;
        xvpages_t* m_vfsa_vpages;

        // Small allocator, 1 KB < size <= 4KB
        u32        m_fsa_min_size;  // 1KB
        u32        m_fsa_step_size; // 16 (gives us 192 allocators)
        u32        m_fsa_max_size;  // 4KB
        void*      m_fsa_mem_base;  // A memory base pointer
        u64        m_fsa_mem_range; // 256 MB
        u32        m_fsa_page_size; // 64 KB (gives us 4KB pages)
        xfsa**     m_fsa_alloc;
        xvpages_t* m_fsa_vpages;

        // Medium allocator
        u32     m_med_min_size;  // 4KB
        u32     m_med_max_size;  // 32MB
        void*   m_med_mem_base;  // A memory base pointer
        u64     m_med_mem_range; // 768 MB
        xalloc* m_med_allocator;

        // Segregated allocator
        u32     m_seg_min_size;  //  1 MB
        u32     m_seg_max_size;  // 32 MB
        void*   m_seg_mem_base;  // A memory base pointer
        u64     m_seg_mem_range; // 8 MB
        xalloc* m_seg_allocator;

        // Large allocator
        u32     m_large_min_size; // 32MB
        void*   m_large_mem_base; // A memory base pointer
        u64     m_large_mem_range;
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
            // set the size to the alignment request, otherwise let the allocation
            // go

            s32 const fvsa_alloc_index = (size - m_fvsa_min_size) / m_fvsa_step_size;
            xfsa*     fvsa_alloc       = m_fvsa_alloc[fvsa_alloc_index];
            ASSERT(size <= fvsa_alloc->size());
            return fvsa_alloc->allocate();
        }
        else if (size <= m_fsa_max_size)
        {
            if (size < m_fsa_min_size)
                size = m_fsa_min_size;

            // TODO:
            // Determine alignment request, if large than size and reasonable then
            // set the size to the alignment request, otherwise let the allocation
            // go

            s32 const fsa_alloc_index = (size - m_fsa_min_size) / m_fsa_step_size;
            xfsa*     fsa_alloc       = m_fsa_alloc[fsa_alloc_index];
            ASSERT(size <= fsa_alloc->size());
            return fsa_alloc->allocate();
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
            s32 const page_index  = (s32)(((u64)ptr - (u64)m_fvsa_mem_base) / m_fvsa_page_size);
            u32 const alloc_size  = m_vfsa_vpages->address_to_allocsize(ptr);
            u32 const alloc_index = (alloc_size - m_fvsa_min_size) / m_fvsa_step_size;
            xfsa*     allocator   = m_fvsa_alloc[alloc_index];
            allocator->deallocate(ptr);
        }
        else if (helper_is_in_memrange(m_fsa_mem_base, m_fsa_mem_range, ptr))
        {
            s32 const page_index  = (s32)(((u64)ptr - (u64)m_fsa_mem_base) / m_fsa_page_size);
            u32 const alloc_size  = m_fsa_vpages->address_to_allocsize(ptr);
            u32 const alloc_index = (alloc_size - m_fsa_min_size) / m_fsa_step_size;
            xfsa*     allocator   = m_fsa_alloc[alloc_index];
            allocator->deallocate(ptr);
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

}; // namespace xcore
