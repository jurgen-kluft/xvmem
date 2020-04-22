#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_coalesce_direct.h"

namespace xcore
{
    class xvmem_allocator_coalesce_direct : public xalloc
    {
    public:
        xvmem_allocator_coalesce_direct()
            : m_vmem(nullptr)
        {
        }

        virtual void* v_allocate(u32 size, u32 alignment);
        virtual u32   v_deallocate(void* p);
        virtual void  v_release();

        xvmem*  m_vmem;
        xalloc* m_main_heap;
        void*   m_mem_base;  // The memory base address, reserved
        u64     m_mem_range; // 32 MB * 8 = 256 MB
        u32     m_min_size;
        u32     m_max_size;
        xalloc* m_allocator;

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void* xvmem_allocator_coalesce_direct::v_allocate(u32 size, u32 alignment)
    {
        ASSERT(size > m_min_size && size <= m_max_size);
        return m_allocator->allocate(size, alignment);
    }

    u32 xvmem_allocator_coalesce_direct::v_deallocate(void* p)
	{
		u32 const alloc_size = m_allocator->deallocate(p);
		return alloc_size; 
	}

    void xvmem_allocator_coalesce_direct::v_release()
    {
		m_allocator->release();
        m_vmem->release(m_mem_base);
    }

    static inline void* advance_ptr(void* ptr, u64 size) { return (void*)((uptr)ptr + size); }

    xalloc* gCreateVMemCoalesceBasedAllocator(xalloc* main_heap, xfsadexed* node_heap, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step)
    {
        xvmem_allocator_coalesce_direct* allocator = main_heap->construct<xvmem_allocator_coalesce_direct>();

        void*     mem_base = nullptr;
        u32       page_size;
        u32       attr         = 0;
        vmem->reserve(mem_size, page_size, attr, mem_base);
        allocator->m_vmem      = vmem;
        allocator->m_mem_base  = allocator->m_mem_base;
        allocator->m_mem_range = mem_size;
        allocator->m_allocator = create_alloc_coalesce_direct(main_heap, node_heap, mem_base, mem_size, alloc_size_min, alloc_size_max, alloc_size_step);

        return allocator;
    }

}; // namespace xcore
