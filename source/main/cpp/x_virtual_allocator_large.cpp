#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_coalesce.h"

namespace xcore
{
    // Small number of large allocations (> 32 MiB)
    // - Small number of allocations, < 64
    // - Allocations aligned to page-size
    // - Allocations commit pages
    // - Deallocations decommit pages
    //
    // Note: Re-use the coalesce policy to manage the allocations!
    // Allocation step size (size alignment) is 64KiB
    // Address range is 128 GiB
    // Uses xcoalescee allocator

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

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xalloc*    m_main_heap;
        xvmem*     m_vmem;
        void*      m_memory_base;
        u64        m_memory_range;
        u32        m_page_size;
        xcoalescestrat::xinstance_t* m_coalesce;

        XCORE_CLASS_PLACEMENT_NEW_DELETE;
    };

    void* xvmem_allocator_large::allocate(u32 size, u32 alignment)
    {
        void* ptr = xcoalescestrat::allocate(m_coalesce, size, alignment);

        // The large allocator:
        // - commits pages on allocation
        // - doesn't cache any pages
        // - decommits pages immediately at deallocation
        u32 const page_count = (size + (m_page_size - 1)) / (m_page_size);
        m_vmem->commit(ptr, m_page_size, page_count);

        return ptr;
    }

    void xvmem_allocator_large::deallocate(void* p)
    {
        ASSERT(is_partof_memory_range(m_memory_base, m_memory_range, p));
        u32 const size = xcoalescestrat::deallocate(m_coalesce, p);

        u32 const page_count = (size + (m_page_size - 1)) / (m_page_size);
        m_vmem->decommit(p, m_page_size, page_count);
    }

    void xvmem_allocator_large::release()
    {
        xcoalescestrat::destroy(m_coalesce);
        m_vmem->release(m_memory_base);
        m_main_heap->deallocate(this);
    }

    xalloc* gCreateVMemLargeAllocator(xalloc* internal_heap, xfsadexed* node_heap, xvmem* vmem, u64 vmem_range, u32 alloc_size_min)
    {
        xvmem_allocator_large* large_allocator = internal_heap->construct<xvmem_allocator_large>(internal_heap);

        u32   page_size = 0;
        void* vmem_addr = nullptr;
        vmem->reserve(vmem_range, page_size, 0, vmem_addr);

        large_allocator->m_vmem         = vmem;
        large_allocator->m_memory_base  = vmem_addr;
        large_allocator->m_memory_range = vmem_range;
        large_allocator->m_page_size    = page_size;

		u32 const alloc_size_max  = 1 * 1024 * 1024 * 1024;
        u32 const alloc_size_step = 64 * 1024;
		large_allocator->m_coalesce = xcoalescestrat::create(internal_heap, node_heap, vmem_addr, vmem_range, alloc_size_min, alloc_size_max, alloc_size_step);

        return large_allocator;
    }

}; // namespace xcore
