#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator_segregated : public alloc_t
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment);
        virtual u32   v_deallocate(void* p);
        virtual void  v_release();

        void initialize(alloc_t* main_alloc, fsa_t* node_heap, xvmem* vmem, u64 mem_range, u32 allocsize_min, u32 allocsize_max);

        XCORE_CLASS_PLACEMENT_NEW_DELETE

        alloc_t* m_main_heap;
        alloc_t* m_segregated;
        u32     m_page_size;
        void*   m_mem_base;
        u64     m_mem_range;
        xvmem*  m_vmem;
    };

    void xvmem_allocator_segregated::initialize(alloc_t* main_heap, fsa_t* node_heap, xvmem* vmem, u64 mem_range, u32 allocsize_min, u32 allocsize_max)
    {
        m_mem_range = mem_range;
        m_vmem      = vmem;
        m_vmem->reserve(m_mem_range, m_page_size, 0, m_mem_base);
        m_segregated = create_alloc_segregated(main_heap, node_heap, m_mem_base, m_mem_range, allocsize_min, allocsize_max, m_page_size);
    }

    void* xvmem_allocator_segregated::v_allocate(u32 size, u32 alignment)
    {
        void* ptr = m_segregated->allocate(size, alignment);
        // TODO: Commit virtual memory
        return ptr;
    }

    u32 xvmem_allocator_segregated::v_deallocate(void* ptr)
    {
        u32 alloc_size = m_segregated->deallocate(ptr);
        // TODO: Decommit virtual memory
        return alloc_size;
    }

    void xvmem_allocator_segregated::v_release()
    {
        m_vmem->release(m_mem_base, m_mem_range);
        m_segregated->release();
        m_main_heap->deallocate(this);
    }

    alloc_t* gCreateVMemSegregatedAllocator(alloc_t* main_heap, fsa_t* node_heap, xvmem* vmem, u64 mem_range, u32 alloc_size_min, u32 alloc_size_max)
    {
        xvmem_allocator_segregated* allocator = main_heap->construct<xvmem_allocator_segregated>();
        allocator->initialize(main_heap, node_heap, vmem, mem_range, alloc_size_min, alloc_size_max);
        return allocator;
    }

}; // namespace xcore
