#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_allocator_virtual.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{

    class xvmem_allocator : public xalloc
    {
    public:
        virtual void* allocate(xsize_t size, u32 align);
        virtual void  deallocate(void* ptr);

    protected:
        // TODO: Should the information about the page-to-index go into a page allocator ?

        // Very small allocator
        s32    m_fvsa_min_size;  // 8
        s32    m_fvsa_step_size; // 8 (gives us 127 allocators)
        s32    m_fvsa_max_size;  // 1024
        void*  m_fvsa_mem_base;  // A memory base pointer
        u64    m_fvsa_mem_range; // 256 MB
        u32    m_fvsa_page_size; // 4 KB (gives us 64 KB pages)
        xbyte* m_fvsa_alloc_map; // 64 KB
        xfsa** m_fvsa_alloc;

        // Small allocator, 1 KB < size <= 4KB
        s32    m_fsa_min_size;   // 1KB
        s32    m_fsa_step_size;  // 16 (gives us 192 allocators)
        s32    m_fsa_max_size;   // 4KB
        void*  m_fsa_mem_base;   // A memory base pointer
        u64    m_fsa_mem_range;  // 256 MB
        u32    m_fsa_page_size; // 64 KB (gives us 4KB pages)
        xbyte* m_fsa_alloc_map;  // 4 KB
        xfsa** m_fsa_alloc;

        // Medium allocator
        s32     m_med_min_size;
        s32     m_med_max_size;
        void*   m_med_mem_base;
        u64     m_med_mem_range;
        xalloc* m_med_allocator;

        // Large allocator
        s32     m_large_min_size;
        void*   m_large_mem_base;
        u64     m_large_mem_range;
        xalloc* m_large_allocator;
    };

    void* xvmem_allocator::allocate(xsize_t size, u32 align)
    {
        if (size <= m_max_fvsa_size)
        {
            if (size < m_min_fvsa_size)
                size = m_min_fvsa_size;

            // TODO:
            // Determine alignment request, if large than size and reasonable then
            // set the size to the alignment request, otherwise let the allocation
            // go

            s32 const fvsa_alloc_index = (size - m_min_fvsa_size) / m_step_fvsa_size;
            xfsa*     fvsa_alloc       = m_fvsa_allocators[fvsa_alloc_index];
            ASSERT(size <= fvsa_alloc->size());
            return fvsa_alloc->allocate();
        }
        else if (size <= m_max_fsa_size)
        {
            if (size < m_min_fsa_size)
                size = m_min_fsa_size;

            // TODO:
            // Determine alignment request, if large than size and reasonable then
            // set the size to the alignment request, otherwise let the allocation
            // go

            s32 const fsa_alloc_index = (size - m_min_fsa_size) / m_step_fsa_size;
            xfsa*     fsa_alloc       = m_fsa_allocators[fsa_alloc_index];
            ASSERT(size <= fsa_alloc->size());
            return fsa_alloc->allocate();
        }
        else if (size <= m_max_med_size)
        {
            if (size < m_min_med_size)
                size = m_min_med_size;

            return m_med_allocator->allocate(size, align);
        }
        else
        {
            return m_large_allocator->allocate(size, align);
        }
    }

    static inline bool is_in_memrange(void* base, u64 range, void* ptr)
    {
        xbyte* begin = (xbyte*)base;
        xbyte* end   = begin + range;
        xbyte* p     = (xbyte*)ptr;
        return p >= begin && p < end;
    }

    void xvmem_allocator::deallocate(void* ptr)
    {
        if (is_in_memrange(m_fvsa_mem_base, m_fvsa_mem_range, ptr))
        {
            s32 const page_index = (s32)(((u64)ptr - (u64)m_fvsa_mem_base) / m_fvsa_page_size);
            s32 alloc_index = m_fvsa_alloc_map[page_index];
            xfsa* allocator = m_fvsa_alloc[alloc_index];
            allocator->deallocate(ptr);

            // Here we need to know the specific m_fvsa_allocators[] that was used.
            // So the memory range has N pages, we have an array of bytes where
            // every byte is associated with a page. The byte will give you the
            // index of the fvsa allocator that was used.
        }
    }

}; // namespace xcore
