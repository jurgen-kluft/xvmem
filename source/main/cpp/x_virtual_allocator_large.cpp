#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_btree.h"

#include "xvmem/x_virtual_allocator.h"
#include "xvmem/x_virtual_pages.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    // Small number of large allocations (> 32 MiB)
    // - Allocations aligned to page-size
    // - Allocations commit pages
    // - Deallocations decommit pages
    // 
    // Note: Re-use the coalesce policy to manage the allocations.

    class xvmem_allocator_large : public xalloc
    {
    public:
        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xalloc*       m_internal_heap;
        xvmem*        m_vmem;
        void*         m_memory_base;
        u64           m_memory_range;
        u32           m_page_size;
        u32           m_allocsize_step;
        u32           m_allocsize_min;
        u32           m_allocsize_max;

    };

	xalloc*		gCreateVMemLargeAllocator(xalloc* internal_heap, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_cnt_max)
	{
		return nullptr;
	}

}; // namespace xcore
