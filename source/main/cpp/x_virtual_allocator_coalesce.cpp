#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"
#include "xbase/x_memory.h"
#include "xbase/x_integer.h"
#include "xbase/x_hibitset.h"

#include "xvmem/x_virtual_memory.h"
#include "xvmem/private/x_strategy_coalesce.h"

namespace xcore
{

    class xvmem_allocator_coalesce : public xalloc
    {
    public:
        xvmem_allocator_coalesce()
            : m_vmem(nullptr)
        {
        }

        virtual void* allocate(u32 size, u32 alignment);
        virtual void  deallocate(void* p);
        virtual void  release();

        xvmem*     m_vmem;
		void* m_memory_addr;
        xcoalescestrat::xinstance_t* m_coalescee;

        XCORE_CLASS_PLACEMENT_NEW_DELETE
    };

    void* xvmem_allocator_coalesce::allocate(u32 size, u32 alignment) { return xcoalescestrat::allocate(m_coalescee, size, alignment); }
    void xvmem_allocator_coalesce::deallocate(void* p) { xcoalescestrat::deallocate(m_coalescee, p); }

    void xvmem_allocator_coalesce::release()
    {
		m_vmem->release(m_memory_addr);
        xcoalescestrat::destroy(m_coalescee);
    }

    xalloc* gCreateVMemCoalesceBasedAllocator(xalloc* internal_heap, xfsadexed* node_alloc, xvmem* vmem, u64 mem_size, u32 alloc_size_min, u32 alloc_size_max, u32 alloc_size_step)
    {
        xvmem_allocator_coalesce* allocator = internal_heap->construct<xvmem_allocator_coalesce>();
		allocator->m_vmem                   = vmem;

        void* memory_addr = nullptr;
        u32   page_size;
        u32   attr = 0;
        vmem->reserve(mem_size, page_size, attr, memory_addr);

		allocator->m_memory_addr = memory_addr;
        allocator->m_coalescee = xcoalescestrat::create(internal_heap, node_alloc, memory_addr, mem_size, alloc_size_min, alloc_size_max, alloc_size_step);

        return allocator;
    }

}; // namespace xcore
