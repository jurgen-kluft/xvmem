#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_allocator_coalesce.h"

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

    class xvmem_allocator_large : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xvmem*        m_vmem;
        void*         m_memory_base;
        u64           m_memory_range;
		xcoalescee	  m_coalesce;

		XCORE_CLASS_PLACEMENT_NEW_DELETE;
    };

	void*	xvmem_allocator_large::allocate(u32 size, u32 alignment)
	{
		void* ptr = 0;

		return ptr;
	}

    void  xvmem_allocator_large::deallocate(void* p)
	{
	}

    void  xvmem_allocator_large::release()
	{
	}

	xalloc*		gCreateVMemLargeAllocator(xalloc* internal_heap, xfsadexed* node_heap, xvmem* vmem, u64 vmem_range, u32 alloc_size_min)
	{
		xvmem_allocator_large* large_allocator = internal_heap->construct<xvmem_allocator_large>();

		// Reserve virtual memory range
		void* vmem_addr = nullptr;

		large_allocator->m_vmem = vmem;
		large_allocator->m_memory_base = vmem_addr;
		large_allocator->m_memory_range = vmem_range;

		u32 const alloc_size_max = 1*1024*1024*1024;
		u32 const alloc_size_step = 64 * 1024;
		large_allocator->m_coalesce.initialize(internal_heap, node_heap, vmem_addr, vmem_range, alloc_size_min, alloc_size_max, alloc_size_step);

		return large_allocator;
	}

}; // namespace xcore
