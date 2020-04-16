#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_integer.h"

#include "xvmem/private/x_strategy_segregated.h"
#include "xvmem/x_virtual_memory.h"

namespace xcore
{
    class xvmem_allocator_segregated : public xalloc
    {
    public:
        virtual void* v_allocate(u32 size, u32 alignment);
        virtual void  v_deallocate(void* p);
        virtual void  v_release();

        void initialize(xalloc* main_alloc, void* mem_address, u64 mem_space, u32 allocsize_min, u32 allocsize_max, u32 pagesize);

		XCORE_CLASS_PLACEMENT_NEW_DELETE

        xsegregatedstrat::xinstance_t* m_segregated;
    };

    void xvmem_allocator_segregated::initialize(xalloc* main_alloc, void* mem_address, u64 mem_space, u32 allocsize_min, u32 allocsize_max, u32 allocsize_align)
    {
        m_segregated = xsegregatedstrat::create(main_alloc, mem_address, mem_space, allocsize_min, allocsize_max, allocsize_align);
    }

    void* xvmem_allocator_segregated::v_allocate(u32 size, u32 alignment)
    {
        void* ptr = xsegregatedstrat::allocate(m_segregated, size, alignment);
        return ptr;
    }

    void xvmem_allocator_segregated::v_deallocate(void* ptr) { xsegregatedstrat::deallocate(m_segregated, ptr); }
    void xvmem_allocator_segregated::v_release() { xsegregatedstrat::destroy(m_segregated); }

    xalloc* gCreateVMemSegregatedAllocator(xalloc* internal_heap, xvmem* vmem, u64 mem_range, u32 alloc_size_min, u32 alloc_size_max)
    {
        xvmem_allocator_segregated* allocator = internal_heap->construct<xvmem_allocator_segregated>();

        u32   page_size = 0;
        void* mem_addr = nullptr;
        vmem->reserve(mem_range, page_size, 0, mem_addr);

		u32 const alloc_size_align = page_size;
		allocator->initialize(internal_heap, mem_addr, mem_range, alloc_size_min, alloc_size_max, alloc_size_align);

        return allocator;
    }

}; // namespace xcore
