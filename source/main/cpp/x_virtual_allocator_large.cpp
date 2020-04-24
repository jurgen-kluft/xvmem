#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_large.h"

namespace xcore
{
    // - Very large allocations (> 32 MiB)
    // - Small number of allocations, < 64
    // - Allocations aligned to page-size
    // - Allocations commit pages
    // - Deallocations decommit pages

    static inline bool is_partof_memory_range(void* mem_base, u64 mem_range, void* ptr)
    {
        u64 const i = (u64)ptr - (u64)mem_base;
        return i <= mem_range;
    }

    class xvmem_allocator_large : public xalloc
    {
    public:
        xvmem_allocator_large(xalloc* main_heap)
            : m_main_heap(main_heap)
            , m_vmem(nullptr)
            , m_memory_base(nullptr)
            , m_memory_range(0)
        {
        }

        virtual void* v_allocate(u32 size, u32 alignment);
        virtual u32   v_deallocate(void* p);
        virtual void  v_release();

        xalloc*    m_main_heap;
        xvmem*     m_vmem;
        void*      m_memory_base;
        u64        m_memory_range;
        u32        m_page_size;
		xalloc*    m_allocator;
        XCORE_CLASS_PLACEMENT_NEW_DELETE;
    };

    void* xvmem_allocator_large::v_allocate(u32 size, u32 alignment)
    {
        void* ptr = m_allocator->allocate(size, alignment);

        // The large allocator:
        // - commits pages on allocation
        // - doesn't cache any pages
        // - decommits pages immediately at deallocation
        u32 const page_count = (size + (m_page_size - 1)) / (m_page_size);
        m_vmem->commit(ptr, m_page_size, page_count);

        return ptr;
    }

    u32 xvmem_allocator_large::v_deallocate(void* p)
    {
        ASSERT(is_partof_memory_range(m_memory_base, m_memory_range, p));
        u32 const size = m_allocator->deallocate(p);

        u32 const page_count = (size + (m_page_size - 1)) / (m_page_size);
        m_vmem->decommit(p, m_page_size, page_count);

		return size;
    }

    void xvmem_allocator_large::v_release()
    {
        m_allocator->release();
        m_vmem->release(m_memory_base);
        m_main_heap->deallocate(this);
    }

    xalloc* gCreateVMemLargeAllocator(xalloc* internal_heap, xfsadexed* node_heap, xvmem* vmem, u64 mem_range, u32 alloc_size_min, u32 alloc_size_max)
    {
        xvmem_allocator_large* large_allocator = internal_heap->construct<xvmem_allocator_large>(internal_heap);

        u32   page_size = 0;
        void* mem_addr = nullptr;
        vmem->reserve(mem_range, page_size, 0, mem_addr);

        large_allocator->m_vmem         = vmem;
        large_allocator->m_memory_base  = mem_addr;
        large_allocator->m_memory_range = mem_range;
        large_allocator->m_page_size    = page_size;

		large_allocator->m_allocator = create_alloc_large(internal_heap, mem_addr, mem_range, 64);

        return large_allocator;
    }

}; // namespace xcore
